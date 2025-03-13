// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 * GPIB Driver for Fluke cda devices.  Basically, its a driver for a (bugfixed)
 * cb7210 connected to channel 0 of a pl330 dma controller.
 *    Author: Frank Mori Hess <fmh6jj@gmail.com>
 *   copyright: (C) 2006, 2010, 2015 Fluke Corporation
 ***************************************************************************/

#include "fluke_gpib.h"

#include "gpibP.h"
#include <linux/dma-mapping.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB Driver for Fluke cda devices");

static int fluke_attach_holdoff_all(gpib_board_t *board, const gpib_board_config_t *config);
static int fluke_attach_holdoff_end(gpib_board_t *board, const gpib_board_config_t *config);
static void fluke_detach(gpib_board_t *board);
static int fluke_config_dma(gpib_board_t *board, int output);
static irqreturn_t fluke_gpib_internal_interrupt(gpib_board_t *board);

static struct platform_device *fluke_gpib_pdev;

static uint8_t fluke_locking_read_byte(struct nec7210_priv *nec_priv, unsigned int register_number)
{
	u8 retval;
	unsigned long flags;

	spin_lock_irqsave(&nec_priv->register_page_lock, flags);
	retval = fluke_read_byte_nolock(nec_priv, register_number);
	spin_unlock_irqrestore(&nec_priv->register_page_lock, flags);
	return retval;
}

static void fluke_locking_write_byte(struct nec7210_priv *nec_priv, uint8_t byte,
				     unsigned int register_number)
{
	unsigned long flags;

	spin_lock_irqsave(&nec_priv->register_page_lock, flags);
	fluke_write_byte_nolock(nec_priv, byte, register_number);
	spin_unlock_irqrestore(&nec_priv->register_page_lock, flags);
}

// wrappers for interface functions
static int fluke_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end,
		      size_t *bytes_read)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_read(board, &priv->nec7210_priv, buffer, length, end, bytes_read);
}

static int fluke_write(gpib_board_t *board, uint8_t *buffer, size_t length,
		       int send_eoi, size_t *bytes_written)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_write(board, &priv->nec7210_priv, buffer, length, send_eoi, bytes_written);
}

static int fluke_command(gpib_board_t *board, uint8_t *buffer, size_t length, size_t *bytes_written)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_command(board, &priv->nec7210_priv, buffer, length, bytes_written);
}

static int fluke_take_control(gpib_board_t *board, int synchronous)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_take_control(board, &priv->nec7210_priv, synchronous);
}

static int fluke_go_to_standby(gpib_board_t *board)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_go_to_standby(board, &priv->nec7210_priv);
}

static void fluke_request_system_control(gpib_board_t *board, int request_control)
{
	struct fluke_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;

	nec7210_request_system_control(board, nec_priv, request_control);
}

static void fluke_interface_clear(gpib_board_t *board, int assert)
{
	struct fluke_priv *priv = board->private_data;

	nec7210_interface_clear(board, &priv->nec7210_priv, assert);
}

static void fluke_remote_enable(gpib_board_t *board, int enable)
{
	struct fluke_priv *priv = board->private_data;

	nec7210_remote_enable(board, &priv->nec7210_priv, enable);
}

static int fluke_enable_eos(gpib_board_t *board, uint8_t eos_byte, int compare_8_bits)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_enable_eos(board, &priv->nec7210_priv, eos_byte, compare_8_bits);
}

static void fluke_disable_eos(gpib_board_t *board)
{
	struct fluke_priv *priv = board->private_data;

	nec7210_disable_eos(board, &priv->nec7210_priv);
}

static unsigned int fluke_update_status(gpib_board_t *board, unsigned int clear_mask)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_update_status(board, &priv->nec7210_priv, clear_mask);
}

static int fluke_primary_address(gpib_board_t *board, unsigned int address)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_primary_address(board, &priv->nec7210_priv, address);
}

static int fluke_secondary_address(gpib_board_t *board, unsigned int address, int enable)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_secondary_address(board, &priv->nec7210_priv, address, enable);
}

static int fluke_parallel_poll(gpib_board_t *board, uint8_t *result)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_parallel_poll(board, &priv->nec7210_priv, result);
}

static void fluke_parallel_poll_configure(gpib_board_t *board, uint8_t configuration)
{
	struct fluke_priv *priv = board->private_data;

	nec7210_parallel_poll_configure(board, &priv->nec7210_priv, configuration);
}

static void fluke_parallel_poll_response(gpib_board_t *board, int ist)
{
	struct fluke_priv *priv = board->private_data;

	nec7210_parallel_poll_response(board, &priv->nec7210_priv, ist);
}

static void fluke_serial_poll_response(gpib_board_t *board, uint8_t status)
{
	struct fluke_priv *priv = board->private_data;

	nec7210_serial_poll_response(board, &priv->nec7210_priv, status);
}

static uint8_t fluke_serial_poll_status(gpib_board_t *board)
{
	struct fluke_priv *priv = board->private_data;

	return nec7210_serial_poll_status(board, &priv->nec7210_priv);
}

static void fluke_return_to_local(gpib_board_t *board)
{
	struct fluke_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;

	write_byte(nec_priv, AUX_RTL2, AUXMR);
	udelay(1);
	write_byte(nec_priv, AUX_RTL, AUXMR);
}

static int fluke_line_status(const gpib_board_t *board)
{
	int status = ValidALL;
	int bsr_bits;
	struct fluke_priv *e_priv;
	struct nec7210_priv *nec_priv;

	e_priv = board->private_data;
	nec_priv = &e_priv->nec7210_priv;

	bsr_bits = fluke_paged_read_byte(e_priv, BUS_STATUS, BUS_STATUS_PAGE);

	if ((bsr_bits & BSR_REN_BIT) == 0)
		status |= BusREN;
	if ((bsr_bits & BSR_IFC_BIT) == 0)
		status |= BusIFC;
	if ((bsr_bits & BSR_SRQ_BIT) == 0)
		status |= BusSRQ;
	if ((bsr_bits & BSR_EOI_BIT) == 0)
		status |= BusEOI;
	if ((bsr_bits & BSR_NRFD_BIT) == 0)
		status |= BusNRFD;
	if ((bsr_bits & BSR_NDAC_BIT) == 0)
		status |= BusNDAC;
	if ((bsr_bits & BSR_DAV_BIT) == 0)
		status |= BusDAV;
	if ((bsr_bits & BSR_ATN_BIT) == 0)
		status |= BusATN;

	return status;
}

static unsigned int fluke_t1_delay(gpib_board_t *board, unsigned int nano_sec)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	unsigned int retval;

	retval = nec7210_t1_delay(board, nec_priv, nano_sec);

	if (nano_sec <= 350) {
		write_byte(nec_priv, AUX_HI_SPEED, AUXMR);
		retval = 350;
	} else {
		write_byte(nec_priv, AUX_LO_SPEED, AUXMR);
	}
	return retval;
}

static int lacs_or_read_ready(gpib_board_t *board)
{
	const struct fluke_priv *e_priv = board->private_data;
	const struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&board->spinlock, flags);
	retval = test_bit(LACS_NUM, &board->status) || test_bit(READ_READY_BN, &nec_priv->state);
	spin_unlock_irqrestore(&board->spinlock, flags);
	return retval;
}

/* Wait until it is possible for a read to do something useful.  This
 * is not essential, it only exists to prevent RFD holdoff from being released pointlessly.
 */
static int wait_for_read(gpib_board_t *board)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	int retval = 0;

	if (wait_event_interruptible(board->wait,
				     lacs_or_read_ready(board) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		retval = -ERESTARTSYS;
	}
	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_and_clear_bit(DEV_CLEAR_BN, &nec_priv->state))
		retval = -EINTR;
	return retval;
}

/* Check if the SH state machine is in SGNS.  We check twice since there is a very small chance
 * we could be blowing through SGNS from SIDS to SDYS if there is already a
 * byte available in the handshake state machine.  We are interested
 * in the case where the handshake is stuck in SGNS due to no byte being
 * available to the chip (and thus we can be confident a dma transfer will
 * result in at least one byte making it into the chip).  This matters
 * because we want to be confident before sending a "send eoi" auxilary
 * command that we will be able to also put the associated data byte
 * in the chip before any potential timeout.
 */
static int source_handshake_is_sgns(struct fluke_priv *e_priv)
{
	int i;

	for (i = 0; i < 2; ++i)	{
		if ((fluke_paged_read_byte(e_priv, STATE1_REG, STATE1_PAGE) &
		     SOURCE_HANDSHAKE_MASK) != SOURCE_HANDSHAKE_SGNS_BITS) {
			return 0;
		}
	}
	return 1;
}

static int source_handshake_is_sids_or_sgns(struct fluke_priv *e_priv)
{
	unsigned int source_handshake_bits;

	source_handshake_bits = fluke_paged_read_byte(e_priv, STATE1_REG, STATE1_PAGE) &
		SOURCE_HANDSHAKE_MASK;

	return (source_handshake_bits == SOURCE_HANDSHAKE_SGNS_BITS) ||
		(source_handshake_bits == SOURCE_HANDSHAKE_SIDS_BITS);
}

/* Wait until the gpib chip is ready to accept a data out byte.
 * If the chip is SGNS it is probably waiting for a a byte to
 * be written to it.
 */
static int wait_for_data_out_ready(gpib_board_t *board)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	int retval = 0;
//	printk("%s: enter\n", __FUNCTION__);

	if (wait_event_interruptible(board->wait,
				     (test_bit(TACS_NUM, &board->status) &&
				      source_handshake_is_sgns(e_priv)) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		retval = -ERESTARTSYS;
	}
	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_and_clear_bit(DEV_CLEAR_BN, &nec_priv->state))
		retval = -EINTR;
//	printk("%s: exit, retval=%i\n", __FUNCTION__, retval);
	return retval;
}

static int wait_for_sids_or_sgns(gpib_board_t *board)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	int retval = 0;
//	printk("%s: enter\n", __FUNCTION__);

	if (wait_event_interruptible(board->wait,
				     source_handshake_is_sids_or_sgns(e_priv) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status)))
		retval = -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_and_clear_bit(DEV_CLEAR_BN, &nec_priv->state))
		retval = -EINTR;
//	printk("%s: exit, retval=%i\n", __FUNCTION__, retval);
	return retval;
}

static void fluke_dma_callback(void *arg)
{
	gpib_board_t *board = arg;
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);
//	printk("%s: enter\n", __FUNCTION__);

	nec7210_set_reg_bits(nec_priv, IMR1, HR_DOIE | HR_DIIE, HR_DOIE | HR_DIIE);
	wake_up_interruptible(&board->wait);

	fluke_gpib_internal_interrupt(board);
	clear_bit(DMA_WRITE_IN_PROGRESS_BN, &nec_priv->state);
	clear_bit(DMA_READ_IN_PROGRESS_BN, &nec_priv->state);
//	printk("%s: exit\n", __FUNCTION__);
	spin_unlock_irqrestore(&board->spinlock, flags);
}

static int fluke_dma_write(gpib_board_t *board, uint8_t *buffer, size_t length,
			   size_t *bytes_written)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	unsigned long flags;
	int retval = 0;
	dma_addr_t address;
	struct dma_async_tx_descriptor *tx_desc;

	*bytes_written = 0;
//	printk("%s: enter\n", __FUNCTION__);
	if (WARN_ON_ONCE(length > e_priv->dma_buffer_size))
		return -EFAULT;
	dmaengine_terminate_all(e_priv->dma_channel);
	// write-clear counter
	writel(0x0, e_priv->write_transfer_counter);

	memcpy(e_priv->dma_buffer, buffer, length);
	address = dma_map_single(board->dev, e_priv->dma_buffer,
				 length, DMA_TO_DEVICE);
	/* program dma controller */
	retval = fluke_config_dma(board, 1);
	if (retval)
		goto cleanup;

	tx_desc = dmaengine_prep_slave_single(e_priv->dma_channel, address, length, DMA_MEM_TO_DEV,
					      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx_desc) {
		pr_err("fluke_gpib: failed to allocate dma transmit descriptor\n");
		retval = -ENOMEM;
		goto cleanup;
	}
	tx_desc->callback = fluke_dma_callback;
	tx_desc->callback_param = board;

	spin_lock_irqsave(&board->spinlock, flags);
	nec7210_set_reg_bits(nec_priv, IMR1, HR_DOIE, 0);
	nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAO, HR_DMAO);
	dmaengine_submit(tx_desc);
	dma_async_issue_pending(e_priv->dma_channel);

	clear_bit(WRITE_READY_BN, &nec_priv->state);
	set_bit(DMA_WRITE_IN_PROGRESS_BN, &nec_priv->state);

	//	printk("%s: in spin lock\n", __FUNCTION__);
	spin_unlock_irqrestore(&board->spinlock, flags);

//	printk("%s: waiting for write.\n", __FUNCTION__);
	// suspend until message is sent
	if (wait_event_interruptible(board->wait,
				     ((readl(e_priv->write_transfer_counter) &
				       write_transfer_counter_mask) == length) ||
				     test_bit(BUS_ERROR_BN, &nec_priv->state) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		dev_dbg(board->gpib_dev, "gpib write interrupted!\n");
		retval = -ERESTARTSYS;
	}
	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_and_clear_bit(DEV_CLEAR_BN, &nec_priv->state))
		retval = -EINTR;
	if (test_and_clear_bit(BUS_ERROR_BN, &nec_priv->state))
		retval = -EIO;
	// disable board's dma
	nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAO, 0);

	dmaengine_terminate_all(e_priv->dma_channel);
	// make sure fluke_dma_callback got called
	if (test_bit(DMA_WRITE_IN_PROGRESS_BN, &nec_priv->state))
		fluke_dma_callback(board);

	/* if everything went fine, try to wait until last byte is actually
	 * transmitted across gpib (but don't try _too_ hard)
	 */
	if (retval == 0)
		retval = wait_for_sids_or_sgns(board);

	*bytes_written = readl(e_priv->write_transfer_counter) & write_transfer_counter_mask;
	if (WARN_ON_ONCE(*bytes_written > length))
		return -EFAULT;

cleanup:
	dma_unmap_single(board->dev, address, length, DMA_TO_DEVICE);
//	printk("%s: exit, retval=%d\n", __FUNCTION__, retval);
	return retval;
}

static int fluke_accel_write(gpib_board_t *board, uint8_t *buffer, size_t length,
			     int send_eoi, size_t *bytes_written)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	size_t remainder = length;
	size_t transfer_size;
	ssize_t retval = 0;
	size_t dma_remainder = remainder;

	if (!e_priv->dma_channel) {
		pr_err("fluke_gpib: No dma channel available, cannot do accel write.");
		return -ENXIO;
	}

	*bytes_written = 0;
	if (length < 1)
		return 0;

	clear_bit(DEV_CLEAR_BN, &nec_priv->state); // XXX FIXME

	if (send_eoi)
		--dma_remainder;
//	printk("%s: entering while loop\n", __FUNCTION__);

	while (dma_remainder > 0) {
		size_t num_bytes;

		retval = wait_for_data_out_ready(board);
		if (retval < 0)
			break;

		transfer_size = (e_priv->dma_buffer_size < dma_remainder) ?
			e_priv->dma_buffer_size : dma_remainder;
		retval = fluke_dma_write(board, buffer, transfer_size, &num_bytes);
		*bytes_written += num_bytes;
		if (retval < 0)
			break;
		dma_remainder -= num_bytes;
		remainder -= num_bytes;
		buffer += num_bytes;
		if (need_resched())
			schedule();
	}
	if (retval < 0)
		return retval;
	//handle sending of last byte with eoi
	if (send_eoi) {
		size_t num_bytes;
		//		printk("%s: handling last byte\n", __FUNCTION__);
		if (WARN_ON_ONCE(remainder != 1))
			return -EFAULT;

		/* wait until we are sure we will be able to write the data byte
		 * into the chip before we send AUX_SEOI.  This prevents a timeout
		 * scenerio where we send AUX_SEOI but then timeout without getting
		 * any bytes into the gpib chip.  This will result in the first byte
		 * of the next write having a spurious EOI set on the first byte.
		 */
		retval = wait_for_data_out_ready(board);
		if (retval < 0)
			return retval;

		write_byte(nec_priv, AUX_SEOI, AUXMR);
		retval = fluke_dma_write(board, buffer, remainder, &num_bytes);
		*bytes_written += num_bytes;
		if (retval < 0)
			return retval;
		remainder -= num_bytes;
	}
//	printk("%s: bytes send=%i\n", __FUNCTION__, (int)(length - remainder));
	return 0;
}

static int fluke_get_dma_residue(struct dma_chan *chan, dma_cookie_t cookie)
{
	struct dma_tx_state state;
	int result;

	result = dmaengine_pause(chan);
	if (result < 0) {
		pr_err("fluke_gpib: dma pause failed?\n");
		return result;
	}
	dmaengine_tx_status(chan, cookie, &state);
	// hardware doesn't support resume, so dont call this
	// method unless the dma transfer is done.
	return state.residue;
}

static int fluke_dma_read(gpib_board_t *board, uint8_t *buffer,
			  size_t length, int *end, size_t *bytes_read)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	int retval = 0;
	unsigned long flags;
	int residue;
	dma_addr_t bus_address;
	struct dma_async_tx_descriptor *tx_desc;
	dma_cookie_t dma_cookie;
	int i;
	static const int timeout = 10;

	//	printk("%s: enter, bus_address=0x%x, length=%i\n", __FUNCTION__,
	//	       (unsigned)bus_address,
	//	       (int)length);

	*bytes_read = 0;
	*end = 0;
	if (length == 0)
		return 0;

	bus_address = dma_map_single(board->dev, e_priv->dma_buffer,
				     length, DMA_FROM_DEVICE);

	/* program dma controller */
	retval = fluke_config_dma(board, 0);
	if (retval) {
		dma_unmap_single(board->dev, bus_address, length, DMA_FROM_DEVICE);
		return retval;
	}
	tx_desc = dmaengine_prep_slave_single(e_priv->dma_channel,
					      bus_address, length, DMA_DEV_TO_MEM,
					      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx_desc) {
		pr_err("fluke_gpib: failed to allocate dma transmit descriptor\n");
		dma_unmap_single(NULL, bus_address, length, DMA_FROM_DEVICE);
		return -EIO;
	}
	tx_desc->callback = fluke_dma_callback;
	tx_desc->callback_param = board;

	spin_lock_irqsave(&board->spinlock, flags);
	// enable nec7210 dma
	nec7210_set_reg_bits(nec_priv, IMR1, HR_DIIE, 0);
	nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, HR_DMAI);

	dma_cookie = dmaengine_submit(tx_desc);
	dma_async_issue_pending(e_priv->dma_channel);

	set_bit(DMA_READ_IN_PROGRESS_BN, &nec_priv->state);
	clear_bit(READ_READY_BN, &nec_priv->state);

	spin_unlock_irqrestore(&board->spinlock, flags);
//	printk("waiting for data transfer.\n");
	// wait for data to transfer
	if (wait_event_interruptible(board->wait,
				     test_bit(DMA_READ_IN_PROGRESS_BN, &nec_priv->state) == 0 ||
				     test_bit(RECEIVED_END_BN, &nec_priv->state) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		pr_warn("fluke: dma read wait interrupted\n");
		retval = -ERESTARTSYS;
	}
	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
		retval = -EINTR;

	/* If we woke up because of end, wait until the dma transfer has pulled
	 * the data byte associated with the end before we cancel the dma transfer.
	 */
	if (test_bit(RECEIVED_END_BN, &nec_priv->state)) {
		for (i = 0; i < timeout; ++i) {
			if (test_bit(DMA_READ_IN_PROGRESS_BN, &nec_priv->state) == 0)
				break;
			if ((read_byte(nec_priv, ADR0) & DATA_IN_STATUS) == 0)
				break;
			usleep_range(10, 15);
		}
		if (i == timeout)
			pr_warn("fluke_gpib: timeout waiting for dma to transfer end data byte.\n");
	}

	// stop the dma transfer
	nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, 0);
	/* delay a little just to make sure any bytes in dma controller's fifo get
	 * written to memory before we disable it
	 */
	usleep_range(10, 15);
	residue = fluke_get_dma_residue(e_priv->dma_channel, dma_cookie);
	if (WARN_ON_ONCE(residue > length || residue < 0))
		return -EFAULT;
	*bytes_read += length - residue;
	dmaengine_terminate_all(e_priv->dma_channel);
	// make sure fluke_dma_callback got called
	if (test_bit(DMA_READ_IN_PROGRESS_BN, &nec_priv->state))
		fluke_dma_callback(board);

	dma_unmap_single(board->dev, bus_address, length, DMA_FROM_DEVICE);
	memcpy(buffer, e_priv->dma_buffer, *bytes_read);

	/* If we got an end interrupt, figure out if it was
	 * associated with the last byte we dma'd or with a
	 * byte still sitting on the cb7210.
	 */
	spin_lock_irqsave(&board->spinlock, flags);
	if (test_bit(READ_READY_BN, &nec_priv->state) == 0) {
		// There is no byte sitting on the cb7210.  If we
		// saw an end interrupt, we need to deal with it now
		if (test_and_clear_bit(RECEIVED_END_BN, &nec_priv->state))
			*end = 1;
	}
	spin_unlock_irqrestore(&board->spinlock, flags);

	return retval;
}

static int fluke_accel_read(gpib_board_t *board, uint8_t *buffer, size_t length,
			    int *end, size_t *bytes_read)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	size_t remain = length;
	size_t transfer_size;
	int retval = 0;
	size_t dma_nbytes;

/*	printk("%s: enter, buffer=0x%p, length=%i\n", __FUNCTION__,
 *		   buffer, (int)length);
 *	printk("\t dma_buffer=0x%p\n", e_priv->dma_buffer);
 */
	*end = 0;
	*bytes_read = 0;

	smp_mb__before_atomic();
	clear_bit(DEV_CLEAR_BN, &nec_priv->state); // XXX FIXME
	smp_mb__after_atomic();

	retval = wait_for_read(board);
	if (retval < 0)
		return retval;

	nec7210_release_rfd_holdoff(board, nec_priv);

//	printk("%s: entering while loop\n", __FUNCTION__);
	while (remain > 0) {
		transfer_size = (e_priv->dma_buffer_size < remain) ?
			e_priv->dma_buffer_size : remain;
		retval = fluke_dma_read(board, buffer, transfer_size, end, &dma_nbytes);
		remain -= dma_nbytes;
		buffer += dma_nbytes;
		*bytes_read += dma_nbytes;
		if (*end)
			break;
		if (retval < 0)	{
//			printk("%s: early exit, retval=%i\n", __FUNCTION__, (int)retval);
			return retval;
		}
		if (need_resched())
			schedule();
	}
//	printk("%s: exit, retval=%i\n", __FUNCTION__, (int)retval);
	return retval;
}

static gpib_interface_t fluke_unaccel_interface = {
	.name = "fluke_unaccel",
	.attach = fluke_attach_holdoff_all,
	.detach = fluke_detach,
	.read = fluke_read,
	.write = fluke_write,
	.command = fluke_command,
	.take_control = fluke_take_control,
	.go_to_standby = fluke_go_to_standby,
	.request_system_control = fluke_request_system_control,
	.interface_clear = fluke_interface_clear,
	.remote_enable = fluke_remote_enable,
	.enable_eos = fluke_enable_eos,
	.disable_eos = fluke_disable_eos,
	.parallel_poll = fluke_parallel_poll,
	.parallel_poll_configure = fluke_parallel_poll_configure,
	.parallel_poll_response = fluke_parallel_poll_response,
	.line_status = fluke_line_status,
	.update_status = fluke_update_status,
	.primary_address = fluke_primary_address,
	.secondary_address = fluke_secondary_address,
	.serial_poll_response = fluke_serial_poll_response,
	.serial_poll_status = fluke_serial_poll_status,
	.t1_delay = fluke_t1_delay,
	.return_to_local = fluke_return_to_local,
};

/* fluke_hybrid uses dma for writes but not for reads.  Added
 * to deal with occasional corruption of bytes seen when doing dma
 * reads.  From looking at the cb7210 vhdl, I believe the corruption
 * is due to a hardware bug triggered by the cpu reading a cb7210
 *		}
 * register just as the dma controller is also doing a read.
 */

static gpib_interface_t fluke_hybrid_interface = {
	.name = "fluke_hybrid",
	.attach = fluke_attach_holdoff_all,
	.detach = fluke_detach,
	.read = fluke_read,
	.write = fluke_accel_write,
	.command = fluke_command,
	.take_control = fluke_take_control,
	.go_to_standby = fluke_go_to_standby,
	.request_system_control = fluke_request_system_control,
	.interface_clear = fluke_interface_clear,
	.remote_enable = fluke_remote_enable,
	.enable_eos = fluke_enable_eos,
	.disable_eos = fluke_disable_eos,
	.parallel_poll = fluke_parallel_poll,
	.parallel_poll_configure = fluke_parallel_poll_configure,
	.parallel_poll_response = fluke_parallel_poll_response,
	.line_status = fluke_line_status,
	.update_status = fluke_update_status,
	.primary_address = fluke_primary_address,
	.secondary_address = fluke_secondary_address,
	.serial_poll_response = fluke_serial_poll_response,
	.serial_poll_status = fluke_serial_poll_status,
	.t1_delay = fluke_t1_delay,
	.return_to_local = fluke_return_to_local,
};

static gpib_interface_t fluke_interface = {
	.name = "fluke",
	.attach = fluke_attach_holdoff_end,
	.detach = fluke_detach,
	.read = fluke_accel_read,
	.write = fluke_accel_write,
	.command = fluke_command,
	.take_control = fluke_take_control,
	.go_to_standby = fluke_go_to_standby,
	.request_system_control = fluke_request_system_control,
	.interface_clear = fluke_interface_clear,
	.remote_enable = fluke_remote_enable,
	.enable_eos = fluke_enable_eos,
	.disable_eos = fluke_disable_eos,
	.parallel_poll = fluke_parallel_poll,
	.parallel_poll_configure = fluke_parallel_poll_configure,
	.parallel_poll_response = fluke_parallel_poll_response,
	.line_status = fluke_line_status,
	.update_status = fluke_update_status,
	.primary_address = fluke_primary_address,
	.secondary_address = fluke_secondary_address,
	.serial_poll_response = fluke_serial_poll_response,
	.serial_poll_status = fluke_serial_poll_status,
	.t1_delay = fluke_t1_delay,
	.return_to_local = fluke_return_to_local,
};

irqreturn_t fluke_gpib_internal_interrupt(gpib_board_t *board)
{
	int status0, status1, status2;
	struct fluke_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;
	int retval = IRQ_NONE;

	if (read_byte(nec_priv, ADR0) & DATA_IN_STATUS)
		set_bit(READ_READY_BN, &nec_priv->state);

	status0 = fluke_paged_read_byte(priv, ISR0_IMR0, ISR0_IMR0_PAGE);
	status1 = read_byte(nec_priv, ISR1);
	status2 = read_byte(nec_priv, ISR2);

	if (status0 & FLUKE_IFCI_BIT) {
		push_gpib_event(board, EventIFC);
		retval = IRQ_HANDLED;
	}

	if (nec7210_interrupt_have_status(board, nec_priv, status1, status2) == IRQ_HANDLED)
		retval = IRQ_HANDLED;
/*
 *	if((status1 & nec_priv->reg_bits[IMR1]) ||
 *		(status2 & (nec_priv->reg_bits[IMR2] & IMR2_ENABLE_INTR_MASK)))
 *	{
 *		printk("fluke: status1 0x%x, status2 0x%x\n", status1, status2);
 *	}
 */

	if (read_byte(nec_priv, ADR0) & DATA_IN_STATUS)	{
		if (test_bit(RFD_HOLDOFF_BN, &nec_priv->state))
			set_bit(READ_READY_BN, &nec_priv->state);
		else
			clear_bit(READ_READY_BN, &nec_priv->state);
	}

	if (retval == IRQ_HANDLED)
		wake_up_interruptible(&board->wait);

	return retval;
}

static irqreturn_t fluke_gpib_interrupt(int irq, void *arg)
{
	gpib_board_t *board = arg;
	unsigned long flags;
	irqreturn_t retval;

	spin_lock_irqsave(&board->spinlock, flags);
	retval = fluke_gpib_internal_interrupt(board);
	spin_unlock_irqrestore(&board->spinlock, flags);
	return retval;
}

static int fluke_allocate_private(gpib_board_t *board)
{
	struct fluke_priv *priv;

	board->private_data = kmalloc(sizeof(struct fluke_priv), GFP_KERNEL);
	if (!board->private_data)
		return -ENOMEM;
	priv = board->private_data;
	memset(priv, 0, sizeof(struct fluke_priv));
	init_nec7210_private(&priv->nec7210_priv);
	priv->dma_buffer_size = 0x7ff;
	priv->dma_buffer = kmalloc(priv->dma_buffer_size, GFP_KERNEL);
	if (!priv->dma_buffer)
		return -ENOMEM;
	return 0;
}

static void fluke_generic_detach(gpib_board_t *board)
{
	if (board->private_data) {
		struct fluke_priv *e_priv = board->private_data;

		kfree(e_priv->dma_buffer);
		kfree(board->private_data);
		board->private_data = NULL;
	}
}

// generic part of attach functions shared by all cb7210 boards
static int fluke_generic_attach(gpib_board_t *board)
{
	struct fluke_priv *e_priv;
	struct nec7210_priv *nec_priv;
	int retval;

	board->status = 0;

	retval = fluke_allocate_private(board);
	if (retval < 0)
		return retval;
	e_priv = board->private_data;
	nec_priv = &e_priv->nec7210_priv;
	nec_priv->read_byte = fluke_locking_read_byte;
	nec_priv->write_byte = fluke_locking_write_byte;
	nec_priv->offset = fluke_reg_offset;
	nec_priv->type = CB7210;
	return 0;
}

static int fluke_config_dma(gpib_board_t *board, int output)
{
	struct fluke_priv *e_priv = board->private_data;
	struct dma_slave_config config;

	config.src_maxburst = 1;
	config.dst_maxburst = 1;
	config.device_fc = true;

	if (output) {
		config.direction = DMA_MEM_TO_DEV;
		config.src_addr = 0;
		config.dst_addr = e_priv->dma_port_res->start;
		config.src_addr_width = 1;
		config.dst_addr_width = 1;
	} else {
		config.direction = DMA_DEV_TO_MEM;
		config.src_addr = e_priv->dma_port_res->start;
		config.dst_addr = 0;
		config.src_addr_width = 1;
		config.dst_addr_width = 1;
	}
	return dmaengine_slave_config(e_priv->dma_channel, &config);
}

static int fluke_init(struct fluke_priv *e_priv, gpib_board_t *board, int handshake_mode)
{
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;

	nec7210_board_reset(nec_priv, board);
	write_byte(nec_priv, AUX_LO_SPEED, AUXMR);
	/* set clock register for driving frequency
	 * ICR should be set to clock in megahertz (1-15) and to zero
	 * for clocks faster than 15 MHz (max 20MHz)
	 */
	write_byte(nec_priv, ICR | 10, AUXMR);
	nec7210_set_handshake_mode(board, nec_priv, handshake_mode);

	nec7210_board_online(nec_priv, board);

	/* poll so we can detect ATN changes */
	if (gpib_request_pseudo_irq(board, fluke_gpib_interrupt)) {
		pr_err("fluke_gpib: failed to allocate pseudo_irq\n");
		return -EINVAL;
	}

	fluke_paged_write_byte(e_priv, FLUKE_IFCIE_BIT, ISR0_IMR0, ISR0_IMR0_PAGE);
	return 0;
}

/* This function is passed to dma_request_channel() in order to
 * select the pl330 dma channel which has been hardwired to
 * the gpib controller.
 */
static bool gpib_dma_channel_filter(struct dma_chan *chan, void *filter_param)
{
	// select the channel which is wired to the gpib chip
	return chan->chan_id == 0;
}

static int fluke_attach_impl(gpib_board_t *board, const gpib_board_config_t *config,
			     unsigned int handshake_mode)
{
	struct fluke_priv *e_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = 0;
	int retval;
	int irq;
	struct resource *res;
	dma_cap_mask_t dma_cap;

	if (!fluke_gpib_pdev) {
		pr_err("No gpib platform device was found, attach failed.\n");
		return -ENODEV;
	}

	retval = fluke_generic_attach(board);
	if (retval)
		return retval;

	e_priv = board->private_data;
	nec_priv = &e_priv->nec7210_priv;
	nec_priv->offset = fluke_reg_offset;
	board->dev = &fluke_gpib_pdev->dev;

	res = platform_get_resource(fluke_gpib_pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&fluke_gpib_pdev->dev, "Unable to locate mmio resource for cb7210 gpib\n");
		return -ENODEV;
	}

	if (request_mem_region(res->start,
			       resource_size(res),
			       fluke_gpib_pdev->name) == NULL) {
		dev_err(&fluke_gpib_pdev->dev, "cannot claim registers\n");
		return -ENXIO;
	}
	e_priv->gpib_iomem_res = res;

	nec_priv->mmiobase = ioremap(e_priv->gpib_iomem_res->start,
				   resource_size(e_priv->gpib_iomem_res));
	pr_info("gpib: mmiobase %llx remapped to %p, length=%d\n",
		(u64)e_priv->gpib_iomem_res->start,
		nec_priv->mmiobase, (int)resource_size(e_priv->gpib_iomem_res));
	if (!nec_priv->mmiobase) {
		dev_err(&fluke_gpib_pdev->dev, "Could not map I/O memory\n");
		return -ENOMEM;
	}

	res = platform_get_resource(fluke_gpib_pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&fluke_gpib_pdev->dev, "Unable to locate mmio resource for gpib dma port\n");
		return -ENODEV;
	}
	if (request_mem_region(res->start,
			       resource_size(res),
			       fluke_gpib_pdev->name) == NULL) {
		dev_err(&fluke_gpib_pdev->dev, "cannot claim registers\n");
		return -ENXIO;
	}
	e_priv->dma_port_res = res;

	res = platform_get_resource(fluke_gpib_pdev, IORESOURCE_MEM, 2);
	if (!res) {
		dev_err(&fluke_gpib_pdev->dev, "Unable to locate mmio resource for write transfer counter\n");
		return -ENODEV;
	}

	if (request_mem_region(res->start,
			       resource_size(res),
			       fluke_gpib_pdev->name) == NULL) {
		dev_err(&fluke_gpib_pdev->dev, "cannot claim registers\n");
		return -ENXIO;
	}
	e_priv->write_transfer_counter_res = res;

	e_priv->write_transfer_counter = ioremap(e_priv->write_transfer_counter_res->start,
						 resource_size(e_priv->write_transfer_counter_res));
	pr_info("gpib: write transfer counter %lx remapped to %p, length=%d\n",
		(unsigned long)e_priv->write_transfer_counter_res->start,
		e_priv->write_transfer_counter,
		(int)resource_size(e_priv->write_transfer_counter_res));
	if (!e_priv->write_transfer_counter) {
		dev_err(&fluke_gpib_pdev->dev, "Could not map I/O memory\n");
		return -ENOMEM;
	}

	irq = platform_get_irq(fluke_gpib_pdev, 0);
	pr_info("gpib: irq %d\n", irq);
	if (irq < 0) {
		dev_err(&fluke_gpib_pdev->dev, "fluke_gpib: request for IRQ failed\n");
		return -EBUSY;
	}
	retval = request_irq(irq, fluke_gpib_interrupt, isr_flags, fluke_gpib_pdev->name, board);
	if (retval) {
		dev_err(&fluke_gpib_pdev->dev,
			"cannot register interrupt handler err=%d\n",
			retval);
		return retval;
	}
	e_priv->irq = irq;

	dma_cap_zero(dma_cap);
	dma_cap_set(DMA_SLAVE, dma_cap);
	e_priv->dma_channel = dma_request_channel(dma_cap, gpib_dma_channel_filter, NULL);
	if (!e_priv->dma_channel) {
		pr_err("fluke_gpib: failed to allocate a dma channel.\n");
		// we don't error out here because unaccel interface will still
		// work without dma
	}

	return fluke_init(e_priv, board, handshake_mode);
}

int fluke_attach_holdoff_all(gpib_board_t *board, const gpib_board_config_t *config)
{
	return fluke_attach_impl(board, config, HR_HLDA);
}

int fluke_attach_holdoff_end(gpib_board_t *board, const gpib_board_config_t *config)
{
	return fluke_attach_impl(board, config, HR_HLDE);
}

void fluke_detach(gpib_board_t *board)
{
	struct fluke_priv *e_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (e_priv) {
		if (e_priv->dma_channel)
			dma_release_channel(e_priv->dma_channel);
		gpib_free_pseudo_irq(board);
		nec_priv = &e_priv->nec7210_priv;

		if (nec_priv->mmiobase) {
			fluke_paged_write_byte(e_priv, 0, ISR0_IMR0, ISR0_IMR0_PAGE);
			nec7210_board_reset(nec_priv, board);
		}
		if (e_priv->irq)
			free_irq(e_priv->irq, board);
		if (e_priv->write_transfer_counter_res) {
			release_mem_region(e_priv->write_transfer_counter_res->start,
					   resource_size(e_priv->write_transfer_counter_res));
		}
		if (e_priv->dma_port_res) {
			release_mem_region(e_priv->dma_port_res->start,
					   resource_size(e_priv->dma_port_res));
		}
		if (e_priv->gpib_iomem_res)
			release_mem_region(e_priv->gpib_iomem_res->start,
					   resource_size(e_priv->gpib_iomem_res));
	}
	fluke_generic_detach(board);
}

static int fluke_gpib_probe(struct platform_device *pdev)
{
	fluke_gpib_pdev = pdev;
	return 0;
}

static const struct of_device_id fluke_gpib_of_match[] = {
	{ .compatible = "flk,fgpib-4.0"},
	{ {0} }
};
MODULE_DEVICE_TABLE(of, fluke_gpib_of_match);

static struct platform_driver fluke_gpib_platform_driver = {
	.driver = {
		.name = "fluke_gpib",
		.owner = THIS_MODULE,
		.of_match_table = fluke_gpib_of_match,
	},
	.probe = &fluke_gpib_probe
};

static int __init fluke_init_module(void)
{
	int result;

	result = platform_driver_register(&fluke_gpib_platform_driver);
	if (result) {
		pr_err("fluke_gpib: platform_driver_register failed: error = %d\n", result);
		return result;
	}

	result = gpib_register_driver(&fluke_unaccel_interface, THIS_MODULE);
	if (result) {
		pr_err("fluke_gpib: gpib_register_driver failed: error = %d\n", result);
		goto err_unaccel;
	}

	result = gpib_register_driver(&fluke_hybrid_interface, THIS_MODULE);
	if (result) {
		pr_err("fluke_gpib: gpib_register_driver failed: error = %d\n", result);
		goto err_hybrid;
	}

	result = gpib_register_driver(&fluke_interface, THIS_MODULE);
	if (result) {
		pr_err("fluke_gpib: gpib_register_driver failed: error = %d\n", result);
		goto err_interface;
	}

	return 0;

err_interface:
	gpib_unregister_driver(&fluke_hybrid_interface);
err_hybrid:
	gpib_unregister_driver(&fluke_unaccel_interface);
err_unaccel:
	platform_driver_unregister(&fluke_gpib_platform_driver);

	return result;
}

static void __exit fluke_exit_module(void)
{
	gpib_unregister_driver(&fluke_unaccel_interface);
	gpib_unregister_driver(&fluke_hybrid_interface);
	gpib_unregister_driver(&fluke_interface);
	platform_driver_unregister(&fluke_gpib_platform_driver);
}

module_init(fluke_init_module);
module_exit(fluke_exit_module);
