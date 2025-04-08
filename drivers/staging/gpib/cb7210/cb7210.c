// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 * Measurement Computing boards using cb7210.2 and cbi488.2 chips
 *    copyright            : (C) 2001, 2002 by Frank Mori Hess
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt
#define DRV_NAME KBUILD_MODNAME

#include "cb7210.h"
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/dma.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include "gpib_pci_ids.h"
#include "quancom_pci.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver Measurement Computing boards using cb7210.2 and cbi488.2");

static int cb7210_read(struct gpib_board *board, uint8_t *buffer, size_t length,
		       int *end, size_t *bytes_read);

	static inline int have_fifo_word(const struct cb7210_priv *cb_priv)
{
	if (((cb7210_read_byte(cb_priv, HS_STATUS)) &
	     (HS_RX_MSB_NOT_EMPTY | HS_RX_LSB_NOT_EMPTY)) ==
	    (HS_RX_MSB_NOT_EMPTY | HS_RX_LSB_NOT_EMPTY))
		return 1;
	else
		return 0;
}

static inline void input_fifo_enable(struct gpib_board *board, int enable)
{
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);

	if (enable) {
		cb_priv->in_fifo_half_full = 0;
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, 0);

		cb7210_write_byte(cb_priv, HS_RX_ENABLE | HS_TX_ENABLE | HS_CLR_SRQ_INT |
				  HS_CLR_EOI_EMPTY_INT | HS_CLR_HF_INT | cb_priv->hs_mode_bits,
				  HS_MODE);

		cb_priv->hs_mode_bits &= ~HS_ENABLE_MASK;
		cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits, HS_MODE);

		cb7210_write_byte(cb_priv, irq_bits(cb_priv->irq), HS_INT_LEVEL);

		cb_priv->hs_mode_bits |= HS_RX_ENABLE;
		cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits, HS_MODE);
	} else {
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, 0);

		cb_priv->hs_mode_bits &= ~HS_ENABLE_MASK;
		cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits, nec7210_iobase(cb_priv) +
				  HS_MODE);

		clear_bit(READ_READY_BN, &nec_priv->state);
	}

	spin_unlock_irqrestore(&board->spinlock, flags);
}

static int fifo_read(struct gpib_board *board, struct cb7210_priv *cb_priv, uint8_t *buffer,
		     size_t length, int *end, size_t *bytes_read)
{
	ssize_t retval = 0;
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;
	int hs_status;
	u16 word;
	unsigned long flags;

	*bytes_read = 0;
	if (cb_priv->fifo_iobase == 0)	{
		dev_err(board->gpib_dev, "fifo iobase is zero!\n");
		return -EIO;
	}
	*end = 0;
	if (length <= cb7210_fifo_size)	{
		dev_err(board->gpib_dev, " bug! fifo read length < fifo size\n");
		return -EINVAL;
	}

	input_fifo_enable(board, 1);

	while (*bytes_read + cb7210_fifo_size < length)	{
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, HR_DMAI);

		if (wait_event_interruptible(board->wait,
					     (cb_priv->in_fifo_half_full &&
					      have_fifo_word(cb_priv)) ||
					     test_bit(RECEIVED_END_BN, &nec_priv->state) ||
					     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
					     test_bit(TIMO_NUM, &board->status))) {
			retval = -ERESTARTSYS;
			nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, 0);
			break;
		}

		spin_lock_irqsave(&board->spinlock, flags);

		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, 0);

		while (have_fifo_word(cb_priv))	{
			word = inw(cb_priv->fifo_iobase + DIR);
			buffer[(*bytes_read)++] = word & 0xff;
			buffer[(*bytes_read)++] = (word >> 8) & 0xff;
		}

		cb_priv->in_fifo_half_full = 0;

		hs_status = cb7210_read_byte(cb_priv, HS_STATUS);

		spin_unlock_irqrestore(&board->spinlock, flags);

		if (test_and_clear_bit(RECEIVED_END_BN, &nec_priv->state)) {
			*end = 1;
			break;
		}
		if (hs_status & HS_FIFO_FULL)
			break;
		if (test_bit(TIMO_NUM, &board->status))	{
			retval = -ETIMEDOUT;
			break;
		}
		if (test_bit(DEV_CLEAR_BN, &nec_priv->state)) {
			retval = -EINTR;
			break;
		}
	}
	hs_status = cb7210_read_byte(cb_priv, HS_STATUS);
	if (hs_status & HS_RX_LSB_NOT_EMPTY) {
		word = inw(cb_priv->fifo_iobase + DIR);
		buffer[(*bytes_read)++] = word & 0xff;
	}

	input_fifo_enable(board, 0);

	if (wait_event_interruptible(board->wait,
				     test_bit(READ_READY_BN, &nec_priv->state) ||
				     test_bit(RECEIVED_END_BN, &nec_priv->state) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		retval = -ERESTARTSYS;
	}
	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
		retval = -EINTR;
	if (test_bit(READ_READY_BN, &nec_priv->state)) {
		nec7210_set_handshake_mode(board, nec_priv, HR_HLDA);
		buffer[(*bytes_read)++] = nec7210_read_data_in(board, nec_priv, end);
	}

	return retval;
}

static int cb7210_accel_read(struct gpib_board *board, uint8_t *buffer,
			     size_t length, int *end, size_t *bytes_read)
{
	ssize_t retval;
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;
	size_t num_bytes;

	*bytes_read = 0;
	// deal with limitations of fifo
	if (length < cb7210_fifo_size + 3 || (nec_priv->auxa_bits & HR_REOS))
		return cb7210_read(board, buffer, length, end, bytes_read);
	*end = 0;

	nec7210_release_rfd_holdoff(board, nec_priv);

	if (wait_event_interruptible(board->wait,
				     test_bit(READ_READY_BN, &nec_priv->state) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		return -ERESTARTSYS;
	}
	if (test_bit(TIMO_NUM, &board->status))
		return -ETIMEDOUT;
	if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
		return -EINTR;

	nec7210_set_handshake_mode(board, nec_priv, HR_HLDE);
	buffer[(*bytes_read)++] = nec7210_read_data_in(board, nec_priv, end);
	if (*end)
		return 0;

	nec7210_release_rfd_holdoff(board, nec_priv);

	retval = fifo_read(board, cb_priv, &buffer[*bytes_read], length - *bytes_read - 1,
			   end, &num_bytes);
	*bytes_read += num_bytes;
	if (retval < 0)
		return retval;
	if (*end)
		return 0;

	retval = cb7210_read(board, &buffer[*bytes_read], 1, end, &num_bytes);
	*bytes_read += num_bytes;
	if (retval < 0)
		return retval;

	return 0;
}

static int output_fifo_empty(const struct cb7210_priv *cb_priv)
{
	if ((cb7210_read_byte(cb_priv, HS_STATUS) & (HS_TX_MSB_NOT_EMPTY | HS_TX_LSB_NOT_EMPTY))
	    == 0)
		return 1;
	else
		return 0;
}

static inline void output_fifo_enable(struct gpib_board *board, int enable)
{
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);

	if (enable) {
		nec7210_set_reg_bits(nec_priv, IMR1, HR_DOIE, 0);
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAO, HR_DMAO);

		cb7210_write_byte(cb_priv, HS_RX_ENABLE | HS_TX_ENABLE | HS_CLR_SRQ_INT |
				  HS_CLR_EOI_EMPTY_INT | HS_CLR_HF_INT | cb_priv->hs_mode_bits,
				  HS_MODE);

		cb_priv->hs_mode_bits &= ~HS_ENABLE_MASK;
		cb_priv->hs_mode_bits |= HS_TX_ENABLE;
		cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits, HS_MODE);

		cb7210_write_byte(cb_priv, irq_bits(cb_priv->irq), HS_INT_LEVEL);

		clear_bit(WRITE_READY_BN, &nec_priv->state);

	} else {
		cb_priv->hs_mode_bits &= ~HS_ENABLE_MASK;
		cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits, HS_MODE);

		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAO, 0);
		nec7210_set_reg_bits(nec_priv, IMR1, HR_DOIE, HR_DOIE);
	}

	spin_unlock_irqrestore(&board->spinlock, flags);
}

static int fifo_write(struct gpib_board *board, uint8_t *buffer, size_t length,
		      size_t *bytes_written)
{
	size_t count = 0;
	ssize_t retval = 0;
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;
	unsigned int num_bytes, i;
	unsigned long flags;

	*bytes_written = 0;
	if (cb_priv->fifo_iobase == 0) {
		dev_err(board->gpib_dev, "fifo iobase is zero!\n");
		return -EINVAL;
	}
	if (length == 0)
		return 0;

	clear_bit(DEV_CLEAR_BN, &nec_priv->state);
	clear_bit(BUS_ERROR_BN, &nec_priv->state);

	output_fifo_enable(board, 1);

	while (count < length) {
		// wait until byte is ready to be sent
		if (wait_event_interruptible(board->wait,
					     cb_priv->out_fifo_half_empty ||
					     output_fifo_empty(cb_priv) ||
					     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
					     test_bit(BUS_ERROR_BN, &nec_priv->state) ||
					     test_bit(TIMO_NUM, &board->status))) {
			retval = -ERESTARTSYS;
			break;
		}
		if (test_bit(TIMO_NUM, &board->status) ||
		    test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
		    test_bit(BUS_ERROR_BN, &nec_priv->state))
			break;

		if (output_fifo_empty(cb_priv))
			num_bytes = cb7210_fifo_size - cb7210_fifo_width;
		else
			num_bytes = cb7210_fifo_size / 2;
		if (num_bytes + count > length)
			num_bytes = length - count;
		if (num_bytes % cb7210_fifo_width) {
			dev_err(board->gpib_dev, " bug! fifo write with odd number of bytes\n");
			retval = -EINVAL;
			break;
		}

		spin_lock_irqsave(&board->spinlock, flags);
		for (i = 0; i < num_bytes / cb7210_fifo_width; i++) {
			u16 word;

			word = buffer[count++] & 0xff;
			word |= (buffer[count++] << 8) & 0xff00;
			outw(word, cb_priv->fifo_iobase + CDOR);
		}
		cb_priv->out_fifo_half_empty = 0;
		cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits |
				  HS_CLR_EOI_EMPTY_INT | HS_CLR_HF_INT, HS_MODE);
		cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits, HS_MODE);
		spin_unlock_irqrestore(&board->spinlock, flags);
	}
	// wait last byte has been sent
	if (wait_event_interruptible(board->wait,
				     output_fifo_empty(cb_priv) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(BUS_ERROR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		retval = -ERESTARTSYS;
	}
	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_bit(BUS_ERROR_BN, &nec_priv->state))
		retval = -EIO;
	if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
		retval = -EINTR;

	output_fifo_enable(board, 0);

	*bytes_written = count;
	return retval;
}

static int cb7210_accel_write(struct gpib_board *board, uint8_t *buffer,
			      size_t length, int send_eoi, size_t *bytes_written)
{
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;
	unsigned long fast_chunk_size, leftover;
	int retval;
	size_t num_bytes;

	*bytes_written = 0;
	if (length > cb7210_fifo_width)
		fast_chunk_size = length - 1;
	else
		fast_chunk_size = 0;
	fast_chunk_size -= fast_chunk_size % cb7210_fifo_width;
	leftover = length - fast_chunk_size;

	retval = fifo_write(board, buffer, fast_chunk_size, &num_bytes);
	*bytes_written += num_bytes;
	if (retval < 0)
		return retval;

	retval = nec7210_write(board, nec_priv, buffer + fast_chunk_size, leftover,
			       send_eoi, &num_bytes);
	*bytes_written += num_bytes;
	return retval;
}

static int cb7210_line_status(const struct gpib_board *board)
{
	int status = VALID_ALL;
	int bsr_bits;
	struct cb7210_priv *cb_priv;

	cb_priv = board->private_data;

	bsr_bits = cb7210_paged_read_byte(cb_priv, BUS_STATUS, BUS_STATUS_PAGE);

	if ((bsr_bits & BSR_REN_BIT) == 0)
		status |= BUS_REN;
	if ((bsr_bits & BSR_IFC_BIT) == 0)
		status |= BUS_IFC;
	if ((bsr_bits & BSR_SRQ_BIT) == 0)
		status |= BUS_SRQ;
	if ((bsr_bits & BSR_EOI_BIT) == 0)
		status |= BUS_EOI;
	if ((bsr_bits & BSR_NRFD_BIT) == 0)
		status |= BUS_NRFD;
	if ((bsr_bits & BSR_NDAC_BIT) == 0)
		status |= BUS_NDAC;
	if ((bsr_bits & BSR_DAV_BIT) == 0)
		status |= BUS_DAV;
	if ((bsr_bits & BSR_ATN_BIT) == 0)
		status |= BUS_ATN;

	return status;
}

static int cb7210_t1_delay(struct gpib_board *board, unsigned int nano_sec)
{
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;
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

static irqreturn_t cb7210_locked_internal_interrupt(struct gpib_board *board);

/*
 * GPIB interrupt service routines
 */

static irqreturn_t cb_pci_interrupt(int irq, void *arg)
{
	int bits;
	struct gpib_board *board = arg;
	struct cb7210_priv *priv = board->private_data;

	// first task check if this is really our interrupt in a shared irq environment
	switch (priv->pci_chip)	{
	case PCI_CHIP_AMCC_S5933:
		if ((inl(priv->amcc_iobase + INTCSR_REG) &
		     (INBOX_INTR_CS_BIT | INTR_ASSERTED_BIT)) == 0)
			return IRQ_NONE;

		// read incoming mailbox to clear mailbox full flag
		inl(priv->amcc_iobase + INCOMING_MAILBOX_REG(3));
		// clear amccs5933 interrupt
		bits = INBOX_FULL_INTR_BIT | INBOX_BYTE_BITS(3) |
			INBOX_SELECT_BITS(3) |	INBOX_INTR_CS_BIT;
		outl(bits, priv->amcc_iobase + INTCSR_REG);
		break;
	case PCI_CHIP_QUANCOM:
		if ((inb(nec7210_iobase(priv) + QUANCOM_IRQ_CONTROL_STATUS_REG) &
		     QUANCOM_IRQ_ASSERTED_BIT))
			outb(QUANCOM_IRQ_ENABLE_BIT, nec7210_iobase(priv) +
			     QUANCOM_IRQ_CONTROL_STATUS_REG);
		break;
	default:
		break;
	}
	return cb7210_locked_internal_interrupt(arg);
}

static irqreturn_t cb7210_internal_interrupt(struct gpib_board *board)
{
	int hs_status, status1, status2;
	struct cb7210_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;
	int clear_bits;

	if ((priv->hs_mode_bits & HS_ENABLE_MASK)) {
		status1 = 0;
		hs_status = cb7210_read_byte(priv, HS_STATUS);
	} else {
		hs_status = 0;
		status1 = read_byte(nec_priv, ISR1);
	}
	status2 = read_byte(nec_priv, ISR2);
	nec7210_interrupt_have_status(board, nec_priv, status1, status2);

	dev_dbg(board->gpib_dev, "status 0x%x, mode 0x%x\n", hs_status, priv->hs_mode_bits);

	clear_bits = 0;

	if (hs_status & HS_HALF_FULL) {
		if (priv->hs_mode_bits & HS_TX_ENABLE)
			priv->out_fifo_half_empty = 1;
		else if (priv->hs_mode_bits & HS_RX_ENABLE)
			priv->in_fifo_half_full = 1;
		clear_bits |= HS_CLR_HF_INT;
	}

	if (hs_status & HS_SRQ_INT) {
		set_bit(SRQI_NUM, &board->status);
		clear_bits |= HS_CLR_SRQ_INT;
	}

	if ((hs_status & HS_EOI_INT)) {
		clear_bits |= HS_CLR_EOI_EMPTY_INT;
		set_bit(RECEIVED_END_BN, &nec_priv->state);
		if ((nec_priv->auxa_bits & HR_HANDSHAKE_MASK) == HR_HLDE)
			set_bit(RFD_HOLDOFF_BN, &nec_priv->state);
	}

	if ((priv->hs_mode_bits & HS_TX_ENABLE) &&
	    (hs_status & (HS_TX_MSB_NOT_EMPTY | HS_TX_LSB_NOT_EMPTY)) == 0)
		clear_bits |= HS_CLR_EOI_EMPTY_INT;

	if (clear_bits) {
		cb7210_write_byte(priv, priv->hs_mode_bits | clear_bits, HS_MODE);
		cb7210_write_byte(priv, priv->hs_mode_bits, HS_MODE);
		wake_up_interruptible(&board->wait);
	}

	return IRQ_HANDLED;
}

static irqreturn_t cb7210_locked_internal_interrupt(struct gpib_board *board)
{
	unsigned long flags;
	irqreturn_t retval;

	spin_lock_irqsave(&board->spinlock, flags);
	retval = cb7210_internal_interrupt(board);
	spin_unlock_irqrestore(&board->spinlock, flags);
	return retval;
}

static irqreturn_t cb7210_interrupt(int irq, void *arg)
{
	return cb7210_internal_interrupt(arg);
}

static int cb_pci_attach(struct gpib_board *board, const gpib_board_config_t *config);
static int cb_isa_attach(struct gpib_board *board, const gpib_board_config_t *config);

static void cb_pci_detach(struct gpib_board *board);
static void cb_isa_detach(struct gpib_board *board);

// wrappers for interface functions
static int cb7210_read(struct gpib_board *board, uint8_t *buffer, size_t length,
		       int *end, size_t *bytes_read)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_read(board, &priv->nec7210_priv, buffer, length, end, bytes_read);
}

static int cb7210_write(struct gpib_board *board, uint8_t *buffer, size_t length,
			int send_eoi, size_t *bytes_written)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_write(board, &priv->nec7210_priv, buffer, length, send_eoi, bytes_written);
}

static int cb7210_command(struct gpib_board *board, uint8_t *buffer, size_t length,
			  size_t *bytes_written)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_command(board, &priv->nec7210_priv, buffer, length, bytes_written);
}

static int cb7210_take_control(struct gpib_board *board, int synchronous)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_take_control(board, &priv->nec7210_priv, synchronous);
}

static int cb7210_go_to_standby(struct gpib_board *board)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_go_to_standby(board, &priv->nec7210_priv);
}

static void cb7210_request_system_control(struct gpib_board *board, int request_control)
{
	struct cb7210_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;

	if (request_control)
		priv->hs_mode_bits |= HS_SYS_CONTROL;
	else
		priv->hs_mode_bits &= ~HS_SYS_CONTROL;

	cb7210_write_byte(priv, priv->hs_mode_bits, HS_MODE);
	nec7210_request_system_control(board, nec_priv, request_control);
}

static void cb7210_interface_clear(struct gpib_board *board, int assert)
{
	struct cb7210_priv *priv = board->private_data;

	nec7210_interface_clear(board, &priv->nec7210_priv, assert);
}

static void cb7210_remote_enable(struct gpib_board *board, int enable)
{
	struct cb7210_priv *priv = board->private_data;

	nec7210_remote_enable(board, &priv->nec7210_priv, enable);
}

static int cb7210_enable_eos(struct gpib_board *board, uint8_t eos_byte, int compare_8_bits)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_enable_eos(board, &priv->nec7210_priv, eos_byte, compare_8_bits);
}

static void cb7210_disable_eos(struct gpib_board *board)
{
	struct cb7210_priv *priv = board->private_data;

	nec7210_disable_eos(board, &priv->nec7210_priv);
}

static unsigned int cb7210_update_status(struct gpib_board *board, unsigned int clear_mask)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_update_status(board, &priv->nec7210_priv, clear_mask);
}

static int cb7210_primary_address(struct gpib_board *board, unsigned int address)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_primary_address(board, &priv->nec7210_priv, address);
}

static int cb7210_secondary_address(struct gpib_board *board, unsigned int address, int enable)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_secondary_address(board, &priv->nec7210_priv, address, enable);
}

static int cb7210_parallel_poll(struct gpib_board *board, uint8_t *result)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_parallel_poll(board, &priv->nec7210_priv, result);
}

static void cb7210_parallel_poll_configure(struct gpib_board *board, uint8_t configuration)
{
	struct cb7210_priv *priv = board->private_data;

	nec7210_parallel_poll_configure(board, &priv->nec7210_priv, configuration);
}

static void cb7210_parallel_poll_response(struct gpib_board *board, int ist)
{
	struct cb7210_priv *priv = board->private_data;

	nec7210_parallel_poll_response(board, &priv->nec7210_priv, ist);
}

static void cb7210_serial_poll_response(struct gpib_board *board, uint8_t status)
{
	struct cb7210_priv *priv = board->private_data;

	nec7210_serial_poll_response(board, &priv->nec7210_priv, status);
}

static uint8_t cb7210_serial_poll_status(struct gpib_board *board)
{
	struct cb7210_priv *priv = board->private_data;

	return nec7210_serial_poll_status(board, &priv->nec7210_priv);
}

static void cb7210_return_to_local(struct gpib_board *board)
{
	struct cb7210_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;

	write_byte(nec_priv, AUX_RTL2, AUXMR);
	udelay(1);
	write_byte(nec_priv, AUX_RTL, AUXMR);
}

static gpib_interface_t cb_pci_unaccel_interface = {
	.name = "cbi_pci_unaccel",
	.attach = cb_pci_attach,
	.detach = cb_pci_detach,
	.read = cb7210_read,
	.write = cb7210_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static gpib_interface_t cb_pci_accel_interface = {
	.name = "cbi_pci_accel",
	.attach = cb_pci_attach,
	.detach = cb_pci_detach,
	.read = cb7210_accel_read,
	.write = cb7210_accel_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static gpib_interface_t cb_pci_interface = {
	.name = "cbi_pci",
	.attach = cb_pci_attach,
	.detach = cb_pci_detach,
	.read = cb7210_accel_read,
	.write = cb7210_accel_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static gpib_interface_t cb_isa_unaccel_interface = {
	.name = "cbi_isa_unaccel",
	.attach = cb_isa_attach,
	.detach = cb_isa_detach,
	.read = cb7210_read,
	.write = cb7210_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static gpib_interface_t cb_isa_interface = {
	.name = "cbi_isa",
	.attach = cb_isa_attach,
	.detach = cb_isa_detach,
	.read = cb7210_accel_read,
	.write = cb7210_accel_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static gpib_interface_t cb_isa_accel_interface = {
	.name = "cbi_isa_accel",
	.attach = cb_isa_attach,
	.detach = cb_isa_detach,
	.read = cb7210_accel_read,
	.write = cb7210_accel_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static int cb7210_allocate_private(struct gpib_board *board)
{
	struct cb7210_priv *priv;

	board->private_data = kmalloc(sizeof(struct cb7210_priv), GFP_KERNEL);
	if (!board->private_data)
		return -ENOMEM;
	priv = board->private_data;
	memset(priv, 0, sizeof(struct cb7210_priv));
	init_nec7210_private(&priv->nec7210_priv);
	return 0;
}

static void cb7210_generic_detach(struct gpib_board *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

// generic part of attach functions shared by all cb7210 boards
static int cb7210_generic_attach(struct gpib_board *board)
{
	struct cb7210_priv *cb_priv;
	struct nec7210_priv *nec_priv;

	board->status = 0;

	if (cb7210_allocate_private(board))
		return -ENOMEM;
	cb_priv = board->private_data;
	nec_priv = &cb_priv->nec7210_priv;
	nec_priv->read_byte = nec7210_locking_ioport_read_byte;
	nec_priv->write_byte = nec7210_locking_ioport_write_byte;
	nec_priv->offset = cb7210_reg_offset;
	nec_priv->type = CB7210;
	return 0;
}

static int cb7210_init(struct cb7210_priv *cb_priv, struct gpib_board *board)
{
	struct nec7210_priv *nec_priv = &cb_priv->nec7210_priv;

	cb7210_write_byte(cb_priv, HS_RESET7210, HS_INT_LEVEL);
	cb7210_write_byte(cb_priv, irq_bits(cb_priv->irq), HS_INT_LEVEL);

	nec7210_board_reset(nec_priv, board);
	cb7210_write_byte(cb_priv, HS_TX_ENABLE | HS_RX_ENABLE | HS_CLR_SRQ_INT |
			  HS_CLR_EOI_EMPTY_INT | HS_CLR_HF_INT, HS_MODE);

	cb_priv->hs_mode_bits = HS_HF_INT_EN;
	cb7210_write_byte(cb_priv, cb_priv->hs_mode_bits, HS_MODE);

	write_byte(nec_priv, AUX_LO_SPEED, AUXMR);
	/* set clock register for maximum (20 MHz) driving frequency
	 * ICR should be set to clock in megahertz (1-15) and to zero
	 * for clocks faster than 15 MHz (max 20MHz)
	 */
	write_byte(nec_priv, ICR | 0, AUXMR);

	if (cb_priv->pci_chip == PCI_CHIP_QUANCOM) {
		/* change interrupt polarity */
		nec_priv->auxb_bits |= HR_INV;
		write_byte(nec_priv, nec_priv->auxb_bits, AUXMR);
	}
	nec7210_board_online(nec_priv, board);

	/* poll so we can detect assertion of ATN */
	if (gpib_request_pseudo_irq(board, cb_pci_interrupt)) {
		pr_err("failed to allocate pseudo_irq\n");
		return -1;
	}
	return 0;
}

static int cb_pci_attach(struct gpib_board *board, const gpib_board_config_t *config)
{
	struct cb7210_priv *cb_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = 0;
	int bits;
	int retval;

	retval = cb7210_generic_attach(board);
	if (retval)
		return retval;

	cb_priv = board->private_data;
	nec_priv = &cb_priv->nec7210_priv;

	cb_priv->pci_device = gpib_pci_get_device(config, PCI_VENDOR_ID_CBOARDS,
						  PCI_DEVICE_ID_CBOARDS_PCI_GPIB, NULL);
	if (cb_priv->pci_device)
		cb_priv->pci_chip = PCI_CHIP_AMCC_S5933;
	if (!cb_priv->pci_device) {
		cb_priv->pci_device = gpib_pci_get_device(config, PCI_VENDOR_ID_CBOARDS,
							  PCI_DEVICE_ID_CBOARDS_CPCI_GPIB, NULL);
		if (cb_priv->pci_device)
			cb_priv->pci_chip = PCI_CHIP_AMCC_S5933;
	}
	if (!cb_priv->pci_device) {
		cb_priv->pci_device = gpib_pci_get_device(config, PCI_VENDOR_ID_QUANCOM,
							  PCI_DEVICE_ID_QUANCOM_GPIB, NULL);
		if (cb_priv->pci_device) {
			cb_priv->pci_chip = PCI_CHIP_QUANCOM;
			nec_priv->offset = 4;
		}
	}
	if (!cb_priv->pci_device) {
		dev_err(board->gpib_dev, "no supported boards found.\n");
		return -ENODEV;
	}

	if (pci_enable_device(cb_priv->pci_device)) {
		dev_err(board->gpib_dev, "error enabling pci device\n");
		return -EIO;
	}

	if (pci_request_regions(cb_priv->pci_device, DRV_NAME))
		return -EBUSY;
	switch (cb_priv->pci_chip) {
	case PCI_CHIP_AMCC_S5933:
		cb_priv->amcc_iobase = pci_resource_start(cb_priv->pci_device, 0);
		nec_priv->iobase = pci_resource_start(cb_priv->pci_device, 1);
		cb_priv->fifo_iobase = pci_resource_start(cb_priv->pci_device, 2);
		break;
	case PCI_CHIP_QUANCOM:
		nec_priv->iobase = pci_resource_start(cb_priv->pci_device, 0);
		cb_priv->fifo_iobase = nec_priv->iobase;
		break;
	default:
		dev_err(board->gpib_dev, "bug! unhandled pci_chip=%i\n", cb_priv->pci_chip);
		return -EIO;
	}
	isr_flags |= IRQF_SHARED;
	if (request_irq(cb_priv->pci_device->irq, cb_pci_interrupt, isr_flags, DRV_NAME, board)) {
		dev_err(board->gpib_dev, "can't request IRQ %d\n",
			cb_priv->pci_device->irq);
		return -EBUSY;
	}
	cb_priv->irq = cb_priv->pci_device->irq;

	switch (cb_priv->pci_chip) {
	case PCI_CHIP_AMCC_S5933:
		// make sure mailbox flags are clear
		inl(cb_priv->amcc_iobase + INCOMING_MAILBOX_REG(3));
		// enable interrupts on amccs5933 chip
		bits = INBOX_FULL_INTR_BIT | INBOX_BYTE_BITS(3) | INBOX_SELECT_BITS(3) |
			INBOX_INTR_CS_BIT;
		outl(bits, cb_priv->amcc_iobase + INTCSR_REG);
		break;
	default:
		break;
	}
	return cb7210_init(cb_priv, board);
}

static void cb_pci_detach(struct gpib_board *board)
{
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (cb_priv) {
		gpib_free_pseudo_irq(board);
		nec_priv = &cb_priv->nec7210_priv;
		if (cb_priv->irq) {
			// disable amcc interrupts
			outl(0, cb_priv->amcc_iobase + INTCSR_REG);
			free_irq(cb_priv->irq, board);
		}
		if (nec_priv->iobase) {
			nec7210_board_reset(nec_priv, board);
			pci_release_regions(cb_priv->pci_device);
		}
		if (cb_priv->pci_device)
			pci_dev_put(cb_priv->pci_device);
	}
	cb7210_generic_detach(board);
}

static int cb_isa_attach(struct gpib_board *board, const gpib_board_config_t *config)
{
	int isr_flags = 0;
	struct cb7210_priv *cb_priv;
	struct nec7210_priv *nec_priv;
	unsigned int bits;
	int retval;

	retval = cb7210_generic_attach(board);
	if (retval)
		return retval;
	cb_priv = board->private_data;
	nec_priv = &cb_priv->nec7210_priv;
	if (!request_region(config->ibbase, cb7210_iosize, DRV_NAME)) {
		dev_err(board->gpib_dev, "ioports starting at 0x%x are already in use\n",
			config->ibbase);
		return -EBUSY;
	}
	nec_priv->iobase = config->ibbase;
	cb_priv->fifo_iobase = nec7210_iobase(cb_priv);

	bits = irq_bits(config->ibirq);
	if (bits == 0)
		dev_err(board->gpib_dev, "board incapable of using irq %i, try 2-5, 7, 10, or 11\n",
			config->ibirq);

	// install interrupt handler
	if (request_irq(config->ibirq, cb7210_interrupt, isr_flags, DRV_NAME, board)) {
		dev_err(board->gpib_dev, "failed to obtain IRQ %d\n", config->ibirq);
		return -EBUSY;
	}
	cb_priv->irq = config->ibirq;

	return cb7210_init(cb_priv, board);
}

static void cb_isa_detach(struct gpib_board *board)
{
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (cb_priv) {
		gpib_free_pseudo_irq(board);
		nec_priv = &cb_priv->nec7210_priv;
		if (cb_priv->irq)
			free_irq(cb_priv->irq, board);
		if (nec_priv->iobase) {
			nec7210_board_reset(nec_priv, board);
			release_region(nec7210_iobase(cb_priv), cb7210_iosize);
		}
	}
	cb7210_generic_detach(board);
}

static int cb7210_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	return 0;
}

static const struct pci_device_id cb7210_pci_table[] = {
	{PCI_VENDOR_ID_CBOARDS, PCI_DEVICE_ID_CBOARDS_PCI_GPIB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{PCI_VENDOR_ID_CBOARDS, PCI_DEVICE_ID_CBOARDS_CPCI_GPIB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{PCI_VENDOR_ID_QUANCOM, PCI_DEVICE_ID_QUANCOM_GPIB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb7210_pci_table);

static struct pci_driver cb7210_pci_driver = {
	.name = DRV_NAME,
	.id_table = cb7210_pci_table,
	.probe = &cb7210_pci_probe
};

/***************************************************************************
 *  Support for computer boards pcmcia-gpib card
 *
 *  Based on gpib PCMCIA client driver written by Claus Schroeter
 *  (clausi@chemie.fu-berlin.de), which was adapted from the
 *  pcmcia skeleton example (presumably David Hinds)
 ***************************************************************************/

#ifdef CONFIG_GPIB_PCMCIA

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

/*
 * The event() function is this driver's Card Services event handler.
 * It will be called by Card Services when an appropriate card status
 * event is received.  The config() and release() entry points are
 * used to configure or release a socket, in response to card insertion
 * and ejection events.	 They are invoked from the gpib event
 * handler.
 */

static int cb_gpib_config(struct pcmcia_device	*link);
static void cb_gpib_release(struct pcmcia_device  *link);
static int cb_pcmcia_attach(struct gpib_board *board, const gpib_board_config_t *config);
static void cb_pcmcia_detach(struct gpib_board *board);

/*
 *  A linked list of "instances" of the gpib device.  Each actual
 *  PCMCIA card corresponds to one device instance, and is described
 *  by one dev_link_t structure (defined in ds.h).
 *
 *  You may not want to use a linked list for this -- for example, the
 *  memory card driver uses an array of dev_link_t pointers, where minor
 *  device numbers are used to derive the corresponding array index.
 */

static	struct pcmcia_device  *curr_dev;

/*
 *  A dev_link_t structure has fields for most things that are needed
 *  to keep track of a socket, but there will usually be some device
 *  specific information that also needs to be kept track of.  The
 *  'priv' pointer in a dev_link_t structure can be used to point to
 *  a device-specific private data structure, like this.
 *
 *  A driver needs to provide a dev_node_t structure for each device
 *  on a card.	In some cases, there is only one device per card (for
 *  example, ethernet cards, modems).  In other cases, there may be
 *  many actual or logical devices (SCSI adapters, memory cards with
 *  multiple partitions).  The dev_node_t structures need to be kept
 *  in a linked list starting at the 'dev' field of a dev_link_t
 *  structure.	We allocate them in the card's private data structure,
 * because they generally can't be allocated dynamically.
 */

struct local_info {
	struct pcmcia_device	*p_dev;
	struct gpib_board		*dev;
};

/*
 *  gpib_attach() creates an "instance" of the driver, allocating
 *  local data structures for one device.  The device is registered
 *  with Card Services.
 *
 *  The dev_link structure is initialized, but we don't actually
 *  configure the card at this point -- we wait until we receive a
 *  card insertion event.
 */

static int cb_gpib_probe(struct pcmcia_device *link)
{
	struct local_info *info;

//	int ret, i;

	/* Allocate space for private device-specific data */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->p_dev = link;
	link->priv = info;

	/* The io structure describes IO port mapping */
	link->resource[0]->end = 16;
	link->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	link->resource[0]->flags |= IO_DATA_PATH_WIDTH_AUTO;
	link->resource[1]->end = 16;
	link->resource[1]->flags &= ~IO_DATA_PATH_WIDTH;
	link->resource[1]->flags |= IO_DATA_PATH_WIDTH_16;
	link->io_lines = 10;

	/* General socket configuration */
	link->config_flags = CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;
	link->config_index = 1;
	link->config_regs = PRESENT_OPTION;

	/* Register with Card Services */
	curr_dev = link;
	return cb_gpib_config(link);
} /* gpib_attach */

/*
 *   This deletes a driver "instance".  The device is de-registered
 *   with Card Services.  If it has been released, all local data
 *   structures are freed.  Otherwise, the structures will be freed
 *   when the device is released.
 */

static void cb_gpib_remove(struct pcmcia_device *link)
{
	struct local_info *info = link->priv;
	//struct struct gpib_board *dev = info->dev;

	if (info->dev)
		cb_pcmcia_detach(info->dev);
	cb_gpib_release(link);

	//free_netdev(dev);
	kfree(info);
}

static int cb_gpib_config_iteration(struct pcmcia_device *link, void *priv_data)
{
	return pcmcia_request_io(link);
}

/*
 *   gpib_config() is scheduled to run after a CARD_INSERTION event
 *   is received, to configure the PCMCIA socket, and to make the
 *   ethernet device available to the system.
 */

static int cb_gpib_config(struct pcmcia_device  *link)
{
	struct pcmcia_device *handle;
	struct local_info *dev;
	int retval;

	handle = link;
	dev = link->priv;

	retval = pcmcia_loop_config(link, &cb_gpib_config_iteration, NULL);
	if (retval) {
		dev_warn(&link->dev, "no configuration found\n");
		cb_gpib_release(link);
		return -ENODEV;
	}

	/*
	 *  This actually configures the PCMCIA socket -- setting up
	 *  the I/O windows and the interrupt mapping.
	 */
	retval = pcmcia_enable_device(link);
	if (retval) {
		dev_warn(&link->dev, "pcmcia_enable_device failed\n");
		cb_gpib_release(link);
		return -ENODEV;
	}

	return 0;
} /* gpib_config */

/*
 *    After a card is removed, gpib_release() will unregister the net
 *   device, and release the PCMCIA configuration.  If the device is
 *   still open, this will be postponed until it is closed.
 */

static void cb_gpib_release(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
}

static int cb_gpib_suspend(struct pcmcia_device *link)
{
	//struct local_info *info = link->priv;
	//struct struct gpib_board *dev = info->dev;

	if (link->open)
		dev_warn(&link->dev, "Device still open\n");
	//netif_device_detach(dev);

	return 0;
}

static int cb_gpib_resume(struct pcmcia_device *link)
{
	//struct local_info *info = link->priv;
	//struct struct gpib_board *dev = info->dev;

	/*if (link->open) {
	 *	ni_gpib_probe(dev);	/ really?
	 *	//netif_device_attach(dev);
	 *
	 */
	return cb_gpib_config(link);
}

/*====================================================================*/

static struct pcmcia_device_id cb_pcmcia_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x0005),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, cb_pcmcia_ids);

static struct pcmcia_driver cb_gpib_cs_driver = {
	.name           = "cb_gpib_cs",
	.owner		= THIS_MODULE,
	.id_table	= cb_pcmcia_ids,
	.probe		= cb_gpib_probe,
	.remove		= cb_gpib_remove,
	.suspend	= cb_gpib_suspend,
	.resume		= cb_gpib_resume,
};

static void cb_pcmcia_cleanup_module(void)
{
	pcmcia_unregister_driver(&cb_gpib_cs_driver);
}

static gpib_interface_t cb_pcmcia_unaccel_interface = {
	.name = "cbi_pcmcia_unaccel",
	.attach = cb_pcmcia_attach,
	.detach = cb_pcmcia_detach,
	.read = cb7210_read,
	.write = cb7210_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static gpib_interface_t cb_pcmcia_interface = {
	.name = "cbi_pcmcia",
	.attach = cb_pcmcia_attach,
	.detach = cb_pcmcia_detach,
	.read = cb7210_accel_read,
	.write = cb7210_accel_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static gpib_interface_t cb_pcmcia_accel_interface = {
	.name = "cbi_pcmcia_accel",
	.attach = cb_pcmcia_attach,
	.detach = cb_pcmcia_detach,
	.read = cb7210_accel_read,
	.write = cb7210_accel_write,
	.command = cb7210_command,
	.take_control = cb7210_take_control,
	.go_to_standby = cb7210_go_to_standby,
	.request_system_control = cb7210_request_system_control,
	.interface_clear = cb7210_interface_clear,
	.remote_enable = cb7210_remote_enable,
	.enable_eos = cb7210_enable_eos,
	.disable_eos = cb7210_disable_eos,
	.parallel_poll = cb7210_parallel_poll,
	.parallel_poll_configure = cb7210_parallel_poll_configure,
	.parallel_poll_response = cb7210_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = cb7210_line_status,
	.update_status = cb7210_update_status,
	.primary_address = cb7210_primary_address,
	.secondary_address = cb7210_secondary_address,
	.serial_poll_response = cb7210_serial_poll_response,
	.serial_poll_status = cb7210_serial_poll_status,
	.t1_delay = cb7210_t1_delay,
	.return_to_local = cb7210_return_to_local,
};

static int cb_pcmcia_attach(struct gpib_board *board, const gpib_board_config_t *config)
{
	struct cb7210_priv *cb_priv;
	struct nec7210_priv *nec_priv;
	int retval;

	if (!curr_dev) {
		dev_err(board->gpib_dev, "no cb pcmcia cards found\n");
		return -ENODEV;
	}

	retval = cb7210_generic_attach(board);
	if (retval)
		return retval;

	cb_priv = board->private_data;
	nec_priv = &cb_priv->nec7210_priv;

	if (!request_region(curr_dev->resource[0]->start, resource_size(curr_dev->resource[0]),
			    DRV_NAME))	{
		dev_err(board->gpib_dev, "ioports starting at 0x%lx are already in use\n",
			(unsigned long)curr_dev->resource[0]->start);
		return -EBUSY;
	}
	nec_priv->iobase = curr_dev->resource[0]->start;
	cb_priv->fifo_iobase = curr_dev->resource[0]->start;

	if (request_irq(curr_dev->irq, cb7210_interrupt, IRQF_SHARED, DRV_NAME, board)) {
		dev_err(board->gpib_dev, "failed to request IRQ %d\n", curr_dev->irq);
		return -EBUSY;
	}
	cb_priv->irq = curr_dev->irq;

	return cb7210_init(cb_priv, board);
}

static void cb_pcmcia_detach(struct gpib_board *board)
{
	struct cb7210_priv *cb_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (cb_priv) {
		nec_priv = &cb_priv->nec7210_priv;
		gpib_free_pseudo_irq(board);
		if (cb_priv->irq)
			free_irq(cb_priv->irq, board);
		if (nec_priv->iobase) {
			nec7210_board_reset(nec_priv, board);
			release_region(nec7210_iobase(cb_priv), cb7210_iosize);
		}
	}
	cb7210_generic_detach(board);
}

#endif /* CONFIG_GPIB_PCMCIA */

static int __init cb7210_init_module(void)
{
	int ret;

	ret = pci_register_driver(&cb7210_pci_driver);
	if (ret) {
		pr_err("pci_register_driver failed: error = %d\n", ret);
		return ret;
	}

	ret = gpib_register_driver(&cb_pci_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_pci;
	}

	ret = gpib_register_driver(&cb_isa_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_isa;
	}

	ret = gpib_register_driver(&cb_pci_accel_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_pci_accel;
	}

	ret = gpib_register_driver(&cb_pci_unaccel_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_pci_unaccel;
	}

	ret = gpib_register_driver(&cb_isa_accel_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_isa_accel;
	}

	ret = gpib_register_driver(&cb_isa_unaccel_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_isa_unaccel;
	}

#ifdef CONFIG_GPIB_PCMCIA
	ret = gpib_register_driver(&cb_pcmcia_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_pcmcia;
	}

	ret = gpib_register_driver(&cb_pcmcia_accel_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_pcmcia_accel;
	}

	ret = gpib_register_driver(&cb_pcmcia_unaccel_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		goto err_pcmcia_unaccel;
	}

	ret = pcmcia_register_driver(&cb_gpib_cs_driver);
	if (ret) {
		pr_err("pcmcia_register_driver failed: error = %d\n", ret);
		goto err_pcmcia_driver;
	}
#endif

	return 0;

#ifdef CONFIG_GPIB_PCMCIA
err_pcmcia_driver:
	gpib_unregister_driver(&cb_pcmcia_unaccel_interface);
err_pcmcia_unaccel:
	gpib_unregister_driver(&cb_pcmcia_accel_interface);
err_pcmcia_accel:
	gpib_unregister_driver(&cb_pcmcia_interface);
err_pcmcia:
#endif
	gpib_unregister_driver(&cb_isa_unaccel_interface);
err_isa_unaccel:
	gpib_unregister_driver(&cb_isa_accel_interface);
err_isa_accel:
	gpib_unregister_driver(&cb_pci_unaccel_interface);
err_pci_unaccel:
	gpib_unregister_driver(&cb_pci_accel_interface);
err_pci_accel:
	gpib_unregister_driver(&cb_isa_interface);
err_isa:
	gpib_unregister_driver(&cb_pci_interface);
err_pci:
	pci_unregister_driver(&cb7210_pci_driver);

	return ret;
}

static void __exit cb7210_exit_module(void)
{
	gpib_unregister_driver(&cb_pci_interface);
	gpib_unregister_driver(&cb_isa_interface);
	gpib_unregister_driver(&cb_pci_accel_interface);
	gpib_unregister_driver(&cb_pci_unaccel_interface);
	gpib_unregister_driver(&cb_isa_accel_interface);
	gpib_unregister_driver(&cb_isa_unaccel_interface);
#ifdef CONFIG_GPIB_PCMCIA
	gpib_unregister_driver(&cb_pcmcia_interface);
	gpib_unregister_driver(&cb_pcmcia_accel_interface);
	gpib_unregister_driver(&cb_pcmcia_unaccel_interface);
	cb_pcmcia_cleanup_module();
#endif

	pci_unregister_driver(&cb7210_pci_driver);
}

module_init(cb7210_init_module);
module_exit(cb7210_exit_module);
