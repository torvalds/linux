// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *   copyright            : (C) 2002, 2004 by Frank Mori Hess              *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt
#define DRV_NAME KBUILD_MODNAME

#include "agilent_82350b.h"
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/dma.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/wait.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver for Agilent 82350b");

static int read_transfer_counter(struct agilent_82350b_priv *a_priv);
static unsigned short read_and_clear_event_status(struct gpib_board *board);
static void set_transfer_counter(struct agilent_82350b_priv *a_priv, int count);
static int agilent_82350b_write(struct gpib_board *board, u8 *buffer,
				size_t length, int send_eoi, size_t *bytes_written);

static int agilent_82350b_accel_read(struct gpib_board *board, u8 *buffer,
				     size_t length, int *end, size_t *bytes_read)

{
	struct agilent_82350b_priv *a_priv = board->private_data;
	struct tms9914_priv *tms_priv = &a_priv->tms9914_priv;
	int retval = 0;
	unsigned short event_status;
	int i, num_fifo_bytes;
	/* hardware doesn't support checking for end-of-string character when using fifo */
	if (tms_priv->eos_flags & REOS)
		return tms9914_read(board, tms_priv, buffer, length, end, bytes_read);

	clear_bit(DEV_CLEAR_BN, &tms_priv->state);

	read_and_clear_event_status(board);
	*end = 0;
	*bytes_read = 0;
	if (length == 0)
		return 0;
	/* disable fifo for the moment */
	writeb(DIRECTION_GPIB_TO_HOST, a_priv->gpib_base + SRAM_ACCESS_CONTROL_REG);
	/* handle corner case of board not in holdoff and one byte might slip in early */
	if (tms_priv->holdoff_active == 0 && length > 1) {
		size_t num_bytes;

		retval = tms9914_read(board, tms_priv, buffer, 1, end, &num_bytes);
		*bytes_read += num_bytes;
		if (retval < 0 || *end)
			return retval;
		++buffer;
		--length;
	}
	tms9914_set_holdoff_mode(tms_priv, TMS9914_HOLDOFF_EOI);
	tms9914_release_holdoff(tms_priv);
	i = 0;
	num_fifo_bytes = length - 1;
	/* disable BI interrupts */
	write_byte(tms_priv, tms_priv->imr0_bits & ~HR_BIIE, IMR0);
	while (i < num_fifo_bytes && *end == 0) {
		int block_size;
		int j;
		int count;

		block_size = min(num_fifo_bytes - i, agilent_82350b_fifo_size);
		set_transfer_counter(a_priv, block_size);
		writeb(ENABLE_TI_TO_SRAM | DIRECTION_GPIB_TO_HOST,
		       a_priv->gpib_base + SRAM_ACCESS_CONTROL_REG);
		if (agilent_82350b_fifo_is_halted(a_priv))
			writeb(RESTART_STREAM_BIT, a_priv->gpib_base + STREAM_STATUS_REG);

		clear_bit(READ_READY_BN, &tms_priv->state);

		retval = wait_event_interruptible(board->wait,
						  ((event_status =
						    read_and_clear_event_status(board)) &
						   (TERM_COUNT_STATUS_BIT |
						    BUFFER_END_STATUS_BIT)) ||
						  test_bit(DEV_CLEAR_BN, &tms_priv->state) ||
						  test_bit(TIMO_NUM, &board->status));
		if (retval) {
			retval = -ERESTARTSYS;
			break;
		}
		count = block_size - read_transfer_counter(a_priv);
		for (j = 0; j < count && i < num_fifo_bytes; ++j)
			buffer[i++] = readb(a_priv->sram_base + j);
		if (event_status & BUFFER_END_STATUS_BIT) {
			clear_bit(RECEIVED_END_BN, &tms_priv->state);

			tms_priv->holdoff_active = 1;
			*end = 1;
		}
		if (test_bit(TIMO_NUM, &board->status)) {
			retval = -ETIMEDOUT;
			break;
		}
		if (test_bit(DEV_CLEAR_BN, &tms_priv->state)) {
			retval = -EINTR;
			break;
		}
	}
	/* re-enable BI interrupts */
	write_byte(tms_priv, tms_priv->imr0_bits, IMR0);
	*bytes_read += i;
	buffer += i;
	length -= i;
	writeb(DIRECTION_GPIB_TO_HOST, a_priv->gpib_base + SRAM_ACCESS_CONTROL_REG);
	if (retval < 0)
		return retval;
	/* read last bytes if we havn't received an END yet */
	if (*end == 0) {
		size_t num_bytes;
		/* try to make sure we holdoff after last byte read */
		retval = tms9914_read(board, tms_priv, buffer, length, end, &num_bytes);
		*bytes_read += num_bytes;
		if (retval < 0)
			return retval;
	}
	return 0;
}

static int translate_wait_return_value(struct gpib_board *board, int retval)

{
	struct agilent_82350b_priv *a_priv = board->private_data;
	struct tms9914_priv *tms_priv = &a_priv->tms9914_priv;

	if (retval)
		return -ERESTARTSYS;
	if (test_bit(TIMO_NUM, &board->status))
		return -ETIMEDOUT;
	if (test_bit(DEV_CLEAR_BN, &tms_priv->state))
		return -EINTR;
	return 0;
}

static int agilent_82350b_accel_write(struct gpib_board *board, u8 *buffer,
				      size_t length, int send_eoi,
				      size_t *bytes_written)
{
	struct agilent_82350b_priv *a_priv = board->private_data;
	struct tms9914_priv *tms_priv = &a_priv->tms9914_priv;
	int i, j;
	unsigned short event_status;
	int retval = 0;
	int fifotransferlength = length;
	int block_size = 0;
	size_t num_bytes;

	*bytes_written = 0;
	if (send_eoi)
		--fifotransferlength;

	clear_bit(DEV_CLEAR_BN, &tms_priv->state);

	writeb(0, a_priv->gpib_base + SRAM_ACCESS_CONTROL_REG);

	event_status = read_and_clear_event_status(board);

#ifdef EXPERIMENTAL
	/* wait for previous BO to complete if any */
	retval = wait_event_interruptible(board->wait,
					  test_bit(DEV_CLEAR_BN, &tms_priv->state) ||
					  test_bit(WRITE_READY_BN, &tms_priv->state) ||
					  test_bit(TIMO_NUM, &board->status));
	retval = translate_wait_return_value(board, retval);

	if (retval)
		return retval;
#endif

	if (fifotransferlength > 0) {
		retval = agilent_82350b_write(board, buffer, 1, 0, &num_bytes);
		*bytes_written += num_bytes;
		if (retval < 0)
			return retval;
	}

	write_byte(tms_priv, tms_priv->imr0_bits & ~HR_BOIE, IMR0);
	for (i = 1; i < fifotransferlength;) {
		clear_bit(WRITE_READY_BN, &tms_priv->state);

		block_size = min(fifotransferlength - i, agilent_82350b_fifo_size);
		set_transfer_counter(a_priv, block_size);
		for (j = 0; j < block_size; ++j, ++i) {
			/* load data into board's sram */
			writeb(buffer[i], a_priv->sram_base + j);
		}
		writeb(ENABLE_TI_TO_SRAM, a_priv->gpib_base + SRAM_ACCESS_CONTROL_REG);

		if (agilent_82350b_fifo_is_halted(a_priv))
			writeb(RESTART_STREAM_BIT, a_priv->gpib_base + STREAM_STATUS_REG);

		retval = wait_event_interruptible(board->wait,
						  ((event_status =
						    read_and_clear_event_status(board)) &
						   TERM_COUNT_STATUS_BIT) ||
						  test_bit(DEV_CLEAR_BN, &tms_priv->state) ||
						  test_bit(TIMO_NUM, &board->status));
		writeb(0, a_priv->gpib_base + SRAM_ACCESS_CONTROL_REG);
		num_bytes = block_size - read_transfer_counter(a_priv);

		*bytes_written += num_bytes;
		retval = translate_wait_return_value(board, retval);
		if (retval)
			break;
	}
	write_byte(tms_priv, tms_priv->imr0_bits, IMR0);
	if (retval < 0)
		return retval;

	if (send_eoi) {
		retval = agilent_82350b_write(board, buffer + fifotransferlength, 1, send_eoi,
					      &num_bytes);
		*bytes_written += num_bytes;
		if (retval < 0)
			return retval;
	}
	return 0;
}

static unsigned short read_and_clear_event_status(struct gpib_board *board)
{
	struct agilent_82350b_priv *a_priv = board->private_data;
	unsigned long flags;
	unsigned short status;

	spin_lock_irqsave(&board->spinlock, flags);
	status = a_priv->event_status_bits;
	a_priv->event_status_bits = 0;
	spin_unlock_irqrestore(&board->spinlock, flags);
	return status;
}

static irqreturn_t agilent_82350b_interrupt(int irq, void *arg)

{
	int tms9914_status1 = 0, tms9914_status2 = 0;
	int event_status;
	struct gpib_board *board = arg;
	struct agilent_82350b_priv *a_priv = board->private_data;
	unsigned long flags;
	irqreturn_t retval = IRQ_NONE;

	spin_lock_irqsave(&board->spinlock, flags);
	event_status = readb(a_priv->gpib_base + EVENT_STATUS_REG);
	if (event_status & IRQ_STATUS_BIT)
		retval = IRQ_HANDLED;

	if (event_status & TMS9914_IRQ_STATUS_BIT) {
		tms9914_status1 = read_byte(&a_priv->tms9914_priv, ISR0);
		tms9914_status2 = read_byte(&a_priv->tms9914_priv, ISR1);
		tms9914_interrupt_have_status(board, &a_priv->tms9914_priv, tms9914_status1,
					      tms9914_status2);
	}
	/* write-clear status bits */
	if (event_status & (BUFFER_END_STATUS_BIT | TERM_COUNT_STATUS_BIT)) {
		writeb(event_status & (BUFFER_END_STATUS_BIT | TERM_COUNT_STATUS_BIT),
		       a_priv->gpib_base + EVENT_STATUS_REG);
		a_priv->event_status_bits |= event_status;
		wake_up_interruptible(&board->wait);
	}
	spin_unlock_irqrestore(&board->spinlock, flags);
	return retval;
}

static void agilent_82350b_detach(struct gpib_board *board);

static int read_transfer_counter(struct agilent_82350b_priv *a_priv)
{
	int lo, mid, value;

	lo = readb(a_priv->gpib_base + XFER_COUNT_LO_REG);
	mid = readb(a_priv->gpib_base + XFER_COUNT_MID_REG);
	value = (lo & 0xff) | ((mid << 8) & 0x7f00);
	value = ~(value - 1) & 0x7fff;
	return value;
}

static void set_transfer_counter(struct agilent_82350b_priv *a_priv, int count)
{
	int complement = -count;

	writeb(complement & 0xff, a_priv->gpib_base + XFER_COUNT_LO_REG);
	writeb((complement >> 8) & 0xff, a_priv->gpib_base + XFER_COUNT_MID_REG);
	/* I don't think the hi count reg is even used, but oh well */
	writeb((complement >> 16) & 0xf, a_priv->gpib_base + XFER_COUNT_HI_REG);
}

/* wrappers for interface functions */
static int agilent_82350b_read(struct gpib_board *board, u8 *buffer,
			       size_t length, int *end, size_t *bytes_read)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_read(board, &priv->tms9914_priv, buffer, length, end, bytes_read);
}

static int agilent_82350b_write(struct gpib_board *board, u8 *buffer,
				size_t length, int send_eoi, size_t *bytes_written)

{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_write(board, &priv->tms9914_priv, buffer, length, send_eoi, bytes_written);
}

static int agilent_82350b_command(struct gpib_board *board, u8 *buffer,
				  size_t length, size_t *bytes_written)

{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_command(board, &priv->tms9914_priv, buffer, length, bytes_written);
}

static int agilent_82350b_take_control(struct gpib_board *board, int synchronous)

{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_take_control_workaround(board, &priv->tms9914_priv, synchronous);
}

static int agilent_82350b_go_to_standby(struct gpib_board *board)

{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_go_to_standby(board, &priv->tms9914_priv);
}

static int agilent_82350b_request_system_control(struct gpib_board *board, int request_control)
{
	struct agilent_82350b_priv *a_priv = board->private_data;

	if (request_control) {
		a_priv->card_mode_bits |= CM_SYSTEM_CONTROLLER_BIT;
		if (a_priv->model != MODEL_82350A)
			writeb(IC_SYSTEM_CONTROLLER_BIT, a_priv->gpib_base + INTERNAL_CONFIG_REG);
	} else {
		a_priv->card_mode_bits &= ~CM_SYSTEM_CONTROLLER_BIT;
		if (a_priv->model != MODEL_82350A)
			writeb(0, a_priv->gpib_base + INTERNAL_CONFIG_REG);
	}
	writeb(a_priv->card_mode_bits, a_priv->gpib_base + CARD_MODE_REG);
	return tms9914_request_system_control(board, &a_priv->tms9914_priv, request_control);
}

static void agilent_82350b_interface_clear(struct gpib_board *board, int assert)

{
	struct agilent_82350b_priv *priv = board->private_data;

	tms9914_interface_clear(board, &priv->tms9914_priv, assert);
}

static void agilent_82350b_remote_enable(struct gpib_board *board, int enable)
{
	struct agilent_82350b_priv *priv = board->private_data;

	tms9914_remote_enable(board, &priv->tms9914_priv, enable);
}

static int agilent_82350b_enable_eos(struct gpib_board *board, u8 eos_byte,
				     int compare_8_bits)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_enable_eos(board, &priv->tms9914_priv, eos_byte, compare_8_bits);
}

static void agilent_82350b_disable_eos(struct gpib_board *board)
{
	struct agilent_82350b_priv *priv = board->private_data;

	tms9914_disable_eos(board, &priv->tms9914_priv);
}

static unsigned int agilent_82350b_update_status(struct gpib_board *board,
						 unsigned int clear_mask)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_update_status(board, &priv->tms9914_priv, clear_mask);
}

static int agilent_82350b_primary_address(struct gpib_board *board,
					  unsigned int address)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_primary_address(board, &priv->tms9914_priv, address);
}

static int agilent_82350b_secondary_address(struct gpib_board *board,
					    unsigned int address, int enable)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_secondary_address(board, &priv->tms9914_priv, address, enable);
}

static int agilent_82350b_parallel_poll(struct gpib_board *board, u8 *result)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_parallel_poll(board, &priv->tms9914_priv, result);
}

static void agilent_82350b_parallel_poll_configure(struct gpib_board *board,
						   u8 config)
{
	struct agilent_82350b_priv *priv = board->private_data;

	tms9914_parallel_poll_configure(board, &priv->tms9914_priv, config);
}

static void agilent_82350b_parallel_poll_response(struct gpib_board *board, int ist)
{
	struct agilent_82350b_priv *priv = board->private_data;

	tms9914_parallel_poll_response(board, &priv->tms9914_priv, ist);
}

static void agilent_82350b_serial_poll_response(struct gpib_board *board, u8 status)
{
	struct agilent_82350b_priv *priv = board->private_data;

	tms9914_serial_poll_response(board, &priv->tms9914_priv, status);
}

static u8 agilent_82350b_serial_poll_status(struct gpib_board *board)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_serial_poll_status(board, &priv->tms9914_priv);
}

static int agilent_82350b_line_status(const struct gpib_board *board)
{
	struct agilent_82350b_priv *priv = board->private_data;

	return tms9914_line_status(board, &priv->tms9914_priv);
}

static int agilent_82350b_t1_delay(struct gpib_board *board, unsigned int nanosec)
{
	struct agilent_82350b_priv *a_priv = board->private_data;
	static const int nanosec_per_clock = 30;
	unsigned int value;

	tms9914_t1_delay(board, &a_priv->tms9914_priv, nanosec);

	value = (nanosec + nanosec_per_clock - 1) / nanosec_per_clock;
	if (value > 0xff)
		value = 0xff;
	writeb(value, a_priv->gpib_base + T1_DELAY_REG);
	return value * nanosec_per_clock;
}

static void agilent_82350b_return_to_local(struct gpib_board *board)
{
	struct agilent_82350b_priv *priv = board->private_data;

	tms9914_return_to_local(board, &priv->tms9914_priv);
}

static int agilent_82350b_allocate_private(struct gpib_board *board)
{
	board->private_data = kzalloc(sizeof(struct agilent_82350b_priv), GFP_KERNEL);
	if (!board->private_data)
		return -ENOMEM;
	return 0;
}

static void agilent_82350b_free_private(struct gpib_board *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

static int init_82350a_hardware(struct gpib_board *board,
				const struct gpib_board_config *config)
{
	struct agilent_82350b_priv *a_priv = board->private_data;
	static const unsigned int firmware_length = 5302;
	unsigned int borg_status;
	static const unsigned int timeout = 1000;
	int i, j;
	const char *firmware_data = config->init_data;
	const unsigned int plx_cntrl_static_bits = PLX9050_WAITO_NOT_USER0_SELECT_BIT |
		PLX9050_USER0_OUTPUT_BIT |
		PLX9050_LLOCK_NOT_USER1_SELECT_BIT |
		PLX9050_USER1_OUTPUT_BIT |
		PLX9050_USER2_OUTPUT_BIT |
		PLX9050_USER3_OUTPUT_BIT |
		PLX9050_PCI_READ_MODE_BIT |
		PLX9050_PCI_WRITE_MODE_BIT |
		PLX9050_PCI_RETRY_DELAY_BITS(64) |
		PLX9050_DIRECT_SLAVE_LOCK_ENABLE_BIT;

	/* load borg data */
	borg_status = readb(a_priv->borg_base);
	if ((borg_status & BORG_DONE_BIT))
		return 0;
	/* need to programme borg */
	if (!config->init_data || config->init_data_length != firmware_length) {
		dev_err(board->gpib_dev, "the 82350A board requires firmware after powering on.\n");
		return -EIO;
	}
	dev_dbg(board->gpib_dev, "Loading firmware...\n");

	/* tickle the borg */
	writel(plx_cntrl_static_bits | PLX9050_USER3_DATA_BIT,
	       a_priv->plx_base + PLX9050_CNTRL_REG);
	usleep_range(1000, 2000);
	writel(plx_cntrl_static_bits, a_priv->plx_base + PLX9050_CNTRL_REG);
	usleep_range(1000, 2000);
	writel(plx_cntrl_static_bits | PLX9050_USER3_DATA_BIT,
	       a_priv->plx_base + PLX9050_CNTRL_REG);
	usleep_range(1000, 2000);

	for (i = 0; i < config->init_data_length; ++i) {
		for (j = 0; j < timeout && (readb(a_priv->borg_base) & BORG_READY_BIT) == 0; ++j) {
			if (need_resched())
				schedule();
			usleep_range(10, 20);
		}
		if (j == timeout) {
			dev_err(board->gpib_dev, "timed out loading firmware.\n");
			return -ETIMEDOUT;
		}
		writeb(firmware_data[i], a_priv->gpib_base + CONFIG_DATA_REG);
	}
	for (j = 0; j < timeout && (readb(a_priv->borg_base) & BORG_DONE_BIT) == 0; ++j) {
		if (need_resched())
			schedule();
		usleep_range(10, 20);
	}
	if (j == timeout) {
		dev_err(board->gpib_dev, "timed out waiting for firmware load to complete.\n");
		return -ETIMEDOUT;
	}
	dev_dbg(board->gpib_dev, " ...done.\n");
	return 0;
}

static int test_sram(struct gpib_board *board)

{
	struct agilent_82350b_priv *a_priv = board->private_data;
	unsigned int i;
	const unsigned int sram_length = pci_resource_len(a_priv->pci_device, SRAM_82350A_REGION);
	/* test SRAM */
	const unsigned int byte_mask = 0xff;

	for (i = 0; i < sram_length; ++i) {
		writeb(i & byte_mask, a_priv->sram_base + i);
		if (need_resched())
			schedule();
	}
	for (i = 0; i < sram_length; ++i) {
		unsigned int read_value = readb(a_priv->sram_base + i);

		if ((i & byte_mask) != read_value) {
			dev_err(board->gpib_dev, "SRAM test failed at %d wanted %d got %d\n",
				i, (i & byte_mask), read_value);
			return -EIO;
		}
		if (need_resched())
			schedule();
	}
	dev_dbg(board->gpib_dev, "SRAM test passed 0x%x bytes checked\n", sram_length);
	return 0;
}

static int agilent_82350b_generic_attach(struct gpib_board *board,
					 const struct gpib_board_config *config,
					 int use_fifos)

{
	struct agilent_82350b_priv *a_priv;
	struct tms9914_priv *tms_priv;
	int retval;

	board->status = 0;

	if (agilent_82350b_allocate_private(board))
		return -ENOMEM;
	a_priv = board->private_data;
	a_priv->using_fifos = use_fifos;
	tms_priv = &a_priv->tms9914_priv;
	tms_priv->read_byte = tms9914_iomem_read_byte;
	tms_priv->write_byte = tms9914_iomem_write_byte;
	tms_priv->offset = 1;

	/* find board */
	a_priv->pci_device = gpib_pci_get_device(config, PCI_VENDOR_ID_AGILENT,
						 PCI_DEVICE_ID_82350B, NULL);
	if (a_priv->pci_device) {
		a_priv->model = MODEL_82350B;
		dev_dbg(board->gpib_dev, "Agilent 82350B board found\n");

	} else	{
		a_priv->pci_device = gpib_pci_get_device(config, PCI_VENDOR_ID_AGILENT,
							 PCI_DEVICE_ID_82351A, NULL);
		if (a_priv->pci_device)	{
			a_priv->model = MODEL_82351A;
			dev_dbg(board->gpib_dev, "Agilent 82351B board found\n");

		} else {
			a_priv->pci_device = gpib_pci_get_subsys(config, PCI_VENDOR_ID_PLX,
								 PCI_DEVICE_ID_PLX_9050,
								 PCI_VENDOR_ID_HP,
								 PCI_SUBDEVICE_ID_82350A,
								 a_priv->pci_device);
			if (a_priv->pci_device) {
				a_priv->model = MODEL_82350A;
				dev_dbg(board->gpib_dev, "HP/Agilent 82350A board found\n");
			} else {
				dev_err(board->gpib_dev, "no 82350/82351 board found\n");
				return -ENODEV;
			}
		}
	}
	if (pci_enable_device(a_priv->pci_device)) {
		dev_err(board->gpib_dev, "error enabling pci device\n");
		return -EIO;
	}
	if (pci_request_regions(a_priv->pci_device, DRV_NAME))
		return -ENOMEM;
	switch (a_priv->model) {
	case MODEL_82350A:
		a_priv->plx_base = ioremap(pci_resource_start(a_priv->pci_device, PLX_MEM_REGION),
					   pci_resource_len(a_priv->pci_device, PLX_MEM_REGION));
		dev_dbg(board->gpib_dev, "plx base address remapped to 0x%p\n", a_priv->plx_base);
		a_priv->gpib_base = ioremap(pci_resource_start(a_priv->pci_device,
							       GPIB_82350A_REGION),
					    pci_resource_len(a_priv->pci_device,
							     GPIB_82350A_REGION));
		dev_dbg(board->gpib_dev, "chip base address remapped to 0x%p\n", a_priv->gpib_base);
		tms_priv->mmiobase = a_priv->gpib_base + TMS9914_BASE_REG;
		a_priv->sram_base = ioremap(pci_resource_start(a_priv->pci_device,
							       SRAM_82350A_REGION),
					    pci_resource_len(a_priv->pci_device,
							     SRAM_82350A_REGION));
		dev_dbg(board->gpib_dev, "sram base address remapped to 0x%p\n", a_priv->sram_base);
		a_priv->borg_base = ioremap(pci_resource_start(a_priv->pci_device,
							       BORG_82350A_REGION),
					    pci_resource_len(a_priv->pci_device,
							     BORG_82350A_REGION));
		dev_dbg(board->gpib_dev, "borg base address remapped to 0x%p\n", a_priv->borg_base);

		retval = init_82350a_hardware(board, config);
		if (retval < 0)
			return retval;
		break;
	case MODEL_82350B:
	case MODEL_82351A:
		a_priv->gpib_base = ioremap(pci_resource_start(a_priv->pci_device, GPIB_REGION),
					    pci_resource_len(a_priv->pci_device, GPIB_REGION));
		dev_dbg(board->gpib_dev, "chip base address remapped to 0x%p\n", a_priv->gpib_base);
		tms_priv->mmiobase = a_priv->gpib_base + TMS9914_BASE_REG;
		a_priv->sram_base = ioremap(pci_resource_start(a_priv->pci_device, SRAM_REGION),
					    pci_resource_len(a_priv->pci_device, SRAM_REGION));
		dev_dbg(board->gpib_dev, "sram base address remapped to 0x%p\n", a_priv->sram_base);
		a_priv->misc_base = ioremap(pci_resource_start(a_priv->pci_device, MISC_REGION),
					    pci_resource_len(a_priv->pci_device, MISC_REGION));
		dev_dbg(board->gpib_dev, "misc base address remapped to 0x%p\n", a_priv->misc_base);
		break;
	default:
		dev_err(board->gpib_dev, "invalid board\n");
		return -ENODEV;
	}

	retval = test_sram(board);
	if (retval < 0)
		return retval;

	if (request_irq(a_priv->pci_device->irq, agilent_82350b_interrupt,
			IRQF_SHARED, DRV_NAME, board)) {
		dev_err(board->gpib_dev, "failed to obtain irq %d\n", a_priv->pci_device->irq);
		return -EIO;
	}
	a_priv->irq = a_priv->pci_device->irq;
	dev_dbg(board->gpib_dev, " IRQ %d\n", a_priv->irq);

	writeb(0, a_priv->gpib_base + SRAM_ACCESS_CONTROL_REG);
	a_priv->card_mode_bits = ENABLE_PCI_IRQ_BIT;
	writeb(a_priv->card_mode_bits, a_priv->gpib_base + CARD_MODE_REG);

	if (a_priv->model == MODEL_82350A) {
		/* enable PCI interrupts for 82350a */
		writel(PLX9050_LINTR1_EN_BIT | PLX9050_LINTR2_POLARITY_BIT |
		       PLX9050_PCI_INTR_EN_BIT,
		       a_priv->plx_base + PLX9050_INTCSR_REG);
	}

	if (use_fifos) {
		writeb(ENABLE_BUFFER_END_EVENTS_BIT | ENABLE_TERM_COUNT_EVENTS_BIT,
		       a_priv->gpib_base + EVENT_ENABLE_REG);
		writeb(ENABLE_TERM_COUNT_INTERRUPT_BIT | ENABLE_BUFFER_END_INTERRUPT_BIT |
		       ENABLE_TMS9914_INTERRUPTS_BIT, a_priv->gpib_base + INTERRUPT_ENABLE_REG);
		/* write-clear event status bits */
		writeb(BUFFER_END_STATUS_BIT | TERM_COUNT_STATUS_BIT,
		       a_priv->gpib_base + EVENT_STATUS_REG);
	} else {
		writeb(0, a_priv->gpib_base + EVENT_ENABLE_REG);
		writeb(ENABLE_TMS9914_INTERRUPTS_BIT,
		       a_priv->gpib_base + INTERRUPT_ENABLE_REG);
	}
	board->t1_nano_sec = agilent_82350b_t1_delay(board, 2000);
	tms9914_board_reset(tms_priv);

	tms9914_online(board, tms_priv);

	return 0;
}

static int agilent_82350b_unaccel_attach(struct gpib_board *board,
					 const struct gpib_board_config *config)
{
	return agilent_82350b_generic_attach(board, config, 0);
}

static int agilent_82350b_accel_attach(struct gpib_board *board,
				       const struct gpib_board_config *config)
{
	return agilent_82350b_generic_attach(board, config, 1);
}

static void agilent_82350b_detach(struct gpib_board *board)
{
	struct agilent_82350b_priv *a_priv = board->private_data;
	struct tms9914_priv *tms_priv;

	if (a_priv) {
		if (a_priv->plx_base) /* disable interrupts */
			writel(0, a_priv->plx_base + PLX9050_INTCSR_REG);

		tms_priv = &a_priv->tms9914_priv;
		if (a_priv->irq)
			free_irq(a_priv->irq, board);
		if (a_priv->gpib_base) {
			tms9914_board_reset(tms_priv);
			if (a_priv->misc_base)
				iounmap(a_priv->misc_base);
			if (a_priv->borg_base)
				iounmap(a_priv->borg_base);
			if (a_priv->sram_base)
				iounmap(a_priv->sram_base);
			if (a_priv->gpib_base)
				iounmap(a_priv->gpib_base);
			if (a_priv->plx_base)
				iounmap(a_priv->plx_base);
			pci_release_regions(a_priv->pci_device);
		}
		if (a_priv->pci_device)
			pci_dev_put(a_priv->pci_device);
	}
	agilent_82350b_free_private(board);
}

static struct gpib_interface agilent_82350b_unaccel_interface = {
	.name = "agilent_82350b_unaccel",
	.attach = agilent_82350b_unaccel_attach,
	.detach = agilent_82350b_detach,
	.read = agilent_82350b_read,
	.write = agilent_82350b_write,
	.command = agilent_82350b_command,
	.request_system_control = agilent_82350b_request_system_control,
	.take_control = agilent_82350b_take_control,
	.go_to_standby = agilent_82350b_go_to_standby,
	.interface_clear = agilent_82350b_interface_clear,
	.remote_enable = agilent_82350b_remote_enable,
	.enable_eos = agilent_82350b_enable_eos,
	.disable_eos = agilent_82350b_disable_eos,
	.parallel_poll = agilent_82350b_parallel_poll,
	.parallel_poll_configure = agilent_82350b_parallel_poll_configure,
	.parallel_poll_response = agilent_82350b_parallel_poll_response,
	.local_parallel_poll_mode = NULL, /* XXX */
	.line_status = agilent_82350b_line_status,
	.update_status = agilent_82350b_update_status,
	.primary_address = agilent_82350b_primary_address,
	.secondary_address = agilent_82350b_secondary_address,
	.serial_poll_response = agilent_82350b_serial_poll_response,
	.serial_poll_status = agilent_82350b_serial_poll_status,
	.t1_delay = agilent_82350b_t1_delay,
	.return_to_local = agilent_82350b_return_to_local,
};

static struct gpib_interface agilent_82350b_interface = {
	.name = "agilent_82350b",
	.attach = agilent_82350b_accel_attach,
	.detach = agilent_82350b_detach,
	.read = agilent_82350b_accel_read,
	.write = agilent_82350b_accel_write,
	.command = agilent_82350b_command,
	.request_system_control = agilent_82350b_request_system_control,
	.take_control = agilent_82350b_take_control,
	.go_to_standby = agilent_82350b_go_to_standby,
	.interface_clear = agilent_82350b_interface_clear,
	.remote_enable = agilent_82350b_remote_enable,
	.enable_eos = agilent_82350b_enable_eos,
	.disable_eos = agilent_82350b_disable_eos,
	.parallel_poll = agilent_82350b_parallel_poll,
	.parallel_poll_configure = agilent_82350b_parallel_poll_configure,
	.parallel_poll_response = agilent_82350b_parallel_poll_response,
	.local_parallel_poll_mode = NULL, /* XXX */
	.line_status = agilent_82350b_line_status,
	.update_status = agilent_82350b_update_status,
	.primary_address = agilent_82350b_primary_address,
	.secondary_address = agilent_82350b_secondary_address,
	.serial_poll_response = agilent_82350b_serial_poll_response,
	.serial_poll_status = agilent_82350b_serial_poll_status,
	.t1_delay = agilent_82350b_t1_delay,
	.return_to_local = agilent_82350b_return_to_local,
};

static int agilent_82350b_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)

{
	return 0;
}

static const struct pci_device_id agilent_82350b_pci_table[] = {
	{ PCI_VENDOR_ID_PLX,     PCI_DEVICE_ID_PLX_9050, PCI_VENDOR_ID_HP,
	  PCI_SUBDEVICE_ID_82350A, 0, 0, 0 },
	{ PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_82350B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_82351A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, agilent_82350b_pci_table);

static struct pci_driver agilent_82350b_pci_driver = {
	.name = DRV_NAME,
	.id_table = agilent_82350b_pci_table,
	.probe = &agilent_82350b_pci_probe
};

static int __init agilent_82350b_init_module(void)
{
	int result;

	result = pci_register_driver(&agilent_82350b_pci_driver);
	if (result) {
		pr_err("pci_register_driver failed: error = %d\n", result);
		return result;
	}

	result = gpib_register_driver(&agilent_82350b_unaccel_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_unaccel;
	}

	result = gpib_register_driver(&agilent_82350b_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_interface;
	}

	return 0;

err_interface:
	gpib_unregister_driver(&agilent_82350b_unaccel_interface);
err_unaccel:
	pci_unregister_driver(&agilent_82350b_pci_driver);

	return result;
}

static void __exit agilent_82350b_exit_module(void)
{
	gpib_unregister_driver(&agilent_82350b_interface);
	gpib_unregister_driver(&agilent_82350b_unaccel_interface);

	pci_unregister_driver(&agilent_82350b_pci_driver);
}

module_init(agilent_82350b_init_module);
module_exit(agilent_82350b_exit_module);
