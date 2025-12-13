// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 * National Instruments boards using tnt4882 or compatible chips (at-gpib, etc).
 *    copyright            : (C) 2001, 2002 by Frank Mori Hess
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt
#define DRV_NAME KBUILD_MODNAME

#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/isapnp.h>

#include "nec7210.h"
#include "gpibP.h"
#include "mite.h"
#include "tnt4882_registers.h"

static const int ISAPNP_VENDOR_ID_NI = ISAPNP_VENDOR('N', 'I', 'C');
static const int ISAPNP_ID_NI_ATGPIB_TNT = 0xc601;
enum {
	PCI_DEVICE_ID_NI_GPIB = 0xc801,
	PCI_DEVICE_ID_NI_GPIB_PLUS = 0xc811,
	PCI_DEVICE_ID_NI_GPIB_PLUS2 = 0x71ad,
	PCI_DEVICE_ID_NI_PXIGPIB = 0xc821,
	PCI_DEVICE_ID_NI_PMCGPIB = 0xc831,
	PCI_DEVICE_ID_NI_PCIEGPIB = 0x70cf,
	PCI_DEVICE_ID_NI_PCIE2GPIB = 0x710e,
// Measurement Computing PCI-488 same design as PCI-GPIB with TNT5004
	PCI_DEVICE_ID_MC_PCI488 = 0x7259,
	PCI_DEVICE_ID_CEC_NI_GPIB = 0x7258
};

// struct which defines private_data for tnt4882 devices
struct tnt4882_priv {
	struct nec7210_priv nec7210_priv;
	struct mite_struct *mite;
	struct pnp_dev *pnp_dev;
	unsigned int irq;
	unsigned short imr0_bits;
	unsigned short imr3_bits;
	unsigned short auxg_bits;	// bits written to auxiliary register G
};

static irqreturn_t tnt4882_internal_interrupt(struct gpib_board *board);

// register offset for nec7210 compatible registers
static const int atgpib_reg_offset = 2;

// number of ioports used
static const int atgpib_iosize = 32;

/* paged io */
static inline unsigned int tnt_paged_readb(struct tnt4882_priv *priv, unsigned long offset)
{
	iowrite8(AUX_PAGEIN, priv->nec7210_priv.mmiobase + AUXMR * priv->nec7210_priv.offset);
	udelay(1);
	return ioread8(priv->nec7210_priv.mmiobase + offset);
}

static inline void tnt_paged_writeb(struct tnt4882_priv *priv, unsigned int value,
				    unsigned long offset)
{
	iowrite8(AUX_PAGEIN, priv->nec7210_priv.mmiobase + AUXMR * priv->nec7210_priv.offset);
	udelay(1);
	iowrite8(value, priv->nec7210_priv.mmiobase + offset);
}

/* readb/writeb wrappers */
static inline unsigned short tnt_readb(struct tnt4882_priv *priv, unsigned long offset)
{
	void __iomem *address = priv->nec7210_priv.mmiobase + offset;
	unsigned long flags;
	unsigned short retval;
	spinlock_t *register_lock = &priv->nec7210_priv.register_page_lock;

	spin_lock_irqsave(register_lock, flags);
	switch (offset) {
	case CSR:
	case SASR:
	case ISR0:
	case BSR:
		switch (priv->nec7210_priv.type) {
		case TNT4882:
		case TNT5004:
			retval = ioread8(address);
			break;
		case NAT4882:
			retval = tnt_paged_readb(priv, offset - tnt_pagein_offset);
			break;
		case NEC7210:
			retval = 0;
			break;
		default:
			retval = 0;
			break;
		}
		break;
	default:
		retval = ioread8(address);
		break;
	}
	spin_unlock_irqrestore(register_lock, flags);
	return retval;
}

static inline void tnt_writeb(struct tnt4882_priv *priv, unsigned short value, unsigned long offset)
{
	void __iomem *address = priv->nec7210_priv.mmiobase + offset;
	unsigned long flags;
	spinlock_t *register_lock = &priv->nec7210_priv.register_page_lock;

	spin_lock_irqsave(register_lock, flags);
	switch (offset)	{
	case KEYREG:
	case IMR0:
	case BCR:
		switch (priv->nec7210_priv.type) {
		case TNT4882:
		case TNT5004:
			iowrite8(value, address);
			break;
		case NAT4882:
			tnt_paged_writeb(priv, value, offset - tnt_pagein_offset);
			break;
		case NEC7210:
			break;
		default:
			break;
		}
		break;
	default:
		iowrite8(value, address);
		break;
	}
	spin_unlock_irqrestore(register_lock, flags);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver for National Instruments boards using tnt4882 or compatible chips");

static int tnt4882_line_status(const struct gpib_board *board)
{
	int status = VALID_ALL;
	int bcsr_bits;
	struct tnt4882_priv *tnt_priv;

	tnt_priv = board->private_data;

	bcsr_bits = tnt_readb(tnt_priv, BSR);

	if (bcsr_bits & BCSR_REN_BIT)
		status |= BUS_REN;
	if (bcsr_bits & BCSR_IFC_BIT)
		status |= BUS_IFC;
	if (bcsr_bits & BCSR_SRQ_BIT)
		status |= BUS_SRQ;
	if (bcsr_bits & BCSR_EOI_BIT)
		status |= BUS_EOI;
	if (bcsr_bits & BCSR_NRFD_BIT)
		status |= BUS_NRFD;
	if (bcsr_bits & BCSR_NDAC_BIT)
		status |= BUS_NDAC;
	if (bcsr_bits & BCSR_DAV_BIT)
		status |= BUS_DAV;
	if (bcsr_bits & BCSR_ATN_BIT)
		status |= BUS_ATN;

	return status;
}

static int tnt4882_t1_delay(struct gpib_board *board, unsigned int nano_sec)
{
	struct tnt4882_priv *tnt_priv = board->private_data;
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;
	unsigned int retval;

	retval = nec7210_t1_delay(board, nec_priv, nano_sec);
	if (nec_priv->type == NEC7210)
		return retval;

	if (nano_sec <= 350) {
		tnt_writeb(tnt_priv, MSTD, KEYREG);
		retval = 350;
	} else {
		tnt_writeb(tnt_priv, 0, KEYREG);
	}
	if (nano_sec > 500 && nano_sec <= 1100)	{
		write_byte(nec_priv, AUXRI | USTD, AUXMR);
		retval = 1100;
	} else {
		write_byte(nec_priv, AUXRI, AUXMR);
	}
	return retval;
}

static int fifo_word_available(struct tnt4882_priv *tnt_priv)
{
	int status2;
	int retval;

	status2 = tnt_readb(tnt_priv, STS2);
	retval = (status2 & AEFN) && (status2 & BEFN);

	return retval;
}

static int fifo_byte_available(struct tnt4882_priv *tnt_priv)
{
	int status2;
	int retval;

	status2 = tnt_readb(tnt_priv, STS2);
	retval = (status2 & AEFN) || (status2 & BEFN);

	return retval;
}

static int fifo_xfer_done(struct tnt4882_priv *tnt_priv)
{
	int status1;
	int retval;

	status1 = tnt_readb(tnt_priv, STS1);
	retval = status1 & (S_DONE | S_HALT);

	return retval;
}

static int drain_fifo_words(struct tnt4882_priv *tnt_priv, u8 *buffer, int num_bytes)
{
	int count = 0;
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;

	while (fifo_word_available(tnt_priv) && count + 2 <= num_bytes)	{
		short word;

		word = ioread16(nec_priv->mmiobase + FIFOB);
		buffer[count++] = word & 0xff;
		buffer[count++] = (word >> 8) & 0xff;
	}
	return count;
}

static void tnt4882_release_holdoff(struct gpib_board *board, struct tnt4882_priv *tnt_priv)
{
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;
	unsigned short sasr_bits;

	sasr_bits = tnt_readb(tnt_priv, SASR);

	/*
	 * tnt4882 not in one-chip mode won't always release holdoff unless we
	 * are in the right mode when release handshake command is given
	 */
	if (sasr_bits & AEHS_BIT) /* holding off due to holdoff on end mode*/	{
		nec7210_set_handshake_mode(board, nec_priv, HR_HLDE);
		write_byte(nec_priv, AUX_FH, AUXMR);
	} else if (sasr_bits & ANHS1_BIT) { /* held off due to holdoff on all data mode*/
		nec7210_set_handshake_mode(board, nec_priv, HR_HLDA);
		write_byte(nec_priv, AUX_FH, AUXMR);
		nec7210_set_handshake_mode(board, nec_priv, HR_HLDE);
	} else { /* held off due to holdoff immediately command */
		nec7210_set_handshake_mode(board, nec_priv, HR_HLDE);
		write_byte(nec_priv, AUX_FH, AUXMR);
	}
}

static int tnt4882_accel_read(struct gpib_board *board, u8 *buffer, size_t length, int *end,
			      size_t *bytes_read)
{
	size_t count = 0;
	ssize_t retval = 0;
	struct tnt4882_priv *tnt_priv = board->private_data;
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;
	unsigned int bits;
	s32 hw_count;
	unsigned long flags;

	*bytes_read = 0;
	// FIXME: really, DEV_CLEAR_BN should happen elsewhere to prevent race
	clear_bit(DEV_CLEAR_BN, &nec_priv->state);
	clear_bit(ADR_CHANGE_BN, &nec_priv->state);

	nec7210_set_reg_bits(nec_priv, IMR1, HR_ENDIE, HR_ENDIE);
	if (nec_priv->type != TNT4882 && nec_priv->type != TNT5004)
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, HR_DMAI);
	else
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, 0);
	tnt_writeb(tnt_priv, nec_priv->auxa_bits | HR_HLDA, CCR);
	bits = TNT_B_16BIT | TNT_IN | TNT_CCEN;
	tnt_writeb(tnt_priv, bits, CFG);
	tnt_writeb(tnt_priv, RESET_FIFO, CMDR);
	udelay(1);
	// load 2's complement of count into hardware counters
	hw_count = -length;
	tnt_writeb(tnt_priv, hw_count & 0xff, CNT0);
	tnt_writeb(tnt_priv, (hw_count >> 8) & 0xff, CNT1);
	tnt_writeb(tnt_priv, (hw_count >> 16) & 0xff, CNT2);
	tnt_writeb(tnt_priv, (hw_count >> 24) & 0xff, CNT3);

	tnt4882_release_holdoff(board, tnt_priv);

	tnt_writeb(tnt_priv, GO, CMDR);
	udelay(1);

	spin_lock_irqsave(&board->spinlock, flags);
	tnt_priv->imr3_bits |= HR_DONE | HR_NEF;
	tnt_writeb(tnt_priv, tnt_priv->imr3_bits, IMR3);
	spin_unlock_irqrestore(&board->spinlock, flags);

	while (count + 2 <= length &&
	       test_bit(RECEIVED_END_BN, &nec_priv->state) == 0 &&
	       fifo_xfer_done(tnt_priv) == 0) {
		// wait until a word is ready
		if (wait_event_interruptible(board->wait,
					     fifo_word_available(tnt_priv) ||
					     fifo_xfer_done(tnt_priv) ||
					     test_bit(RECEIVED_END_BN, &nec_priv->state) ||
					     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
					     test_bit(ADR_CHANGE_BN, &nec_priv->state) ||
					     test_bit(TIMO_NUM, &board->status))) {
			retval = -ERESTARTSYS;
			break;
		}
		if (test_bit(TIMO_NUM, &board->status))	{
			retval = -ETIMEDOUT;
			break;
		}
		if (test_bit(DEV_CLEAR_BN, &nec_priv->state)) {
			retval = -EINTR;
			break;
		}
		if (test_bit(ADR_CHANGE_BN, &nec_priv->state)) {
			retval = -EINTR;
			break;
		}

		spin_lock_irqsave(&board->spinlock, flags);
		count += drain_fifo_words(tnt_priv, &buffer[count], length - count);
		tnt_priv->imr3_bits |= HR_NEF;
		tnt_writeb(tnt_priv, tnt_priv->imr3_bits, IMR3);
		spin_unlock_irqrestore(&board->spinlock, flags);

		if (need_resched())
			schedule();
	}
	// wait for last byte
	if (count < length) {
		spin_lock_irqsave(&board->spinlock, flags);
		tnt_priv->imr3_bits |= HR_DONE | HR_NEF;
		tnt_writeb(tnt_priv, tnt_priv->imr3_bits, IMR3);
		spin_unlock_irqrestore(&board->spinlock, flags);

		if (wait_event_interruptible(board->wait,
					     fifo_xfer_done(tnt_priv) ||
					     test_bit(RECEIVED_END_BN, &nec_priv->state) ||
					     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
					     test_bit(ADR_CHANGE_BN, &nec_priv->state) ||
					     test_bit(TIMO_NUM, &board->status))) {
			retval = -ERESTARTSYS;
		}
		if (test_bit(TIMO_NUM, &board->status))
			retval = -ETIMEDOUT;
		if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
			retval = -EINTR;
		if (test_bit(ADR_CHANGE_BN, &nec_priv->state))
			retval = -EINTR;
		count += drain_fifo_words(tnt_priv, &buffer[count], length - count);
		if (fifo_byte_available(tnt_priv) && count < length)
			buffer[count++] = tnt_readb(tnt_priv, FIFOB);
	}
	if (count < length)
		tnt_writeb(tnt_priv, STOP, CMDR);
	udelay(1);

	nec7210_set_reg_bits(nec_priv, IMR1, HR_ENDIE, 0);
	nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAI, 0);
	/*
	 * force handling of any pending interrupts (seems to be needed
	 * to keep interrupts from getting hosed, plus for syncing
	 * with RECEIVED_END below)
	 */
	tnt4882_internal_interrupt(board);
	/* RECEIVED_END should be in sync now */
	if (test_and_clear_bit(RECEIVED_END_BN, &nec_priv->state))
		*end = 1;
	if (retval < 0)	{
		// force immediate holdoff
		write_byte(nec_priv, AUX_HLDI, AUXMR);

		set_bit(RFD_HOLDOFF_BN, &nec_priv->state);
	}
	*bytes_read = count;

	return retval;
}

static int fifo_space_available(struct tnt4882_priv *tnt_priv)
{
	int status2;
	int retval;

	status2 = tnt_readb(tnt_priv, STS2);
	retval = (status2 & AFFN) && (status2 & BFFN);

	return retval;
}

static unsigned int tnt_transfer_count(struct tnt4882_priv *tnt_priv)
{
	unsigned int count = 0;

	count |= tnt_readb(tnt_priv, CNT0) & 0xff;
	count |= (tnt_readb(tnt_priv, CNT1) << 8) & 0xff00;
	count |= (tnt_readb(tnt_priv, CNT2) << 16) & 0xff0000;
	count |= (tnt_readb(tnt_priv, CNT3) << 24) & 0xff000000;
	// return two's complement
	return -count;
};

static int write_wait(struct gpib_board *board, struct tnt4882_priv *tnt_priv,
		      int wait_for_done, int send_commands)
{
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;

	if (wait_event_interruptible(board->wait,
				     (!wait_for_done && fifo_space_available(tnt_priv)) ||
				     fifo_xfer_done(tnt_priv) ||
				     test_bit(BUS_ERROR_BN, &nec_priv->state) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status)))
		return -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		return -ETIMEDOUT;
	if (test_and_clear_bit(BUS_ERROR_BN, &nec_priv->state))
		return (send_commands) ? -ENOTCONN : -ECOMM;
	if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
		return -EINTR;
	return 0;
}

static int generic_write(struct gpib_board *board, u8 *buffer, size_t length,
			 int send_eoi, int send_commands, size_t *bytes_written)
{
	size_t count = 0;
	ssize_t retval = 0;
	struct tnt4882_priv *tnt_priv = board->private_data;
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;
	unsigned int bits;
	s32 hw_count;
	unsigned long flags;

	*bytes_written = 0;
	// FIXME: really, DEV_CLEAR_BN should happen elsewhere to prevent race
	clear_bit(DEV_CLEAR_BN, &nec_priv->state);

	nec7210_set_reg_bits(nec_priv, IMR1, HR_ERRIE, HR_ERRIE);

	if (nec_priv->type != TNT4882 && nec_priv->type != TNT5004)
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAO, HR_DMAO);
	else
		nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAO, 0);

	tnt_writeb(tnt_priv, RESET_FIFO, CMDR);
	udelay(1);

	bits = TNT_B_16BIT;
	if (send_eoi) {
		bits |= TNT_CCEN;
		if (nec_priv->type != TNT4882 && nec_priv->type != TNT5004)
			tnt_writeb(tnt_priv, AUX_SEOI, CCR);
	}
	if (send_commands)
		bits |= TNT_COMMAND;
	tnt_writeb(tnt_priv, bits, CFG);

	// load 2's complement of count into hardware counters
	hw_count = -length;
	tnt_writeb(tnt_priv, hw_count & 0xff, CNT0);
	tnt_writeb(tnt_priv, (hw_count >> 8) & 0xff, CNT1);
	tnt_writeb(tnt_priv, (hw_count >> 16) & 0xff, CNT2);
	tnt_writeb(tnt_priv, (hw_count >> 24) & 0xff, CNT3);

	tnt_writeb(tnt_priv, GO, CMDR);
	udelay(1);

	spin_lock_irqsave(&board->spinlock, flags);
	tnt_priv->imr3_bits |= HR_DONE;
	tnt_writeb(tnt_priv, tnt_priv->imr3_bits, IMR3);
	spin_unlock_irqrestore(&board->spinlock, flags);

	while (count < length)	{
		// wait until byte is ready to be sent
		retval = write_wait(board, tnt_priv, 0, send_commands);
		if (retval < 0)
			break;
		if (fifo_xfer_done(tnt_priv))
			break;
		spin_lock_irqsave(&board->spinlock, flags);
		while (fifo_space_available(tnt_priv) && count < length) {
			u16 word;

			word = buffer[count++] & 0xff;
			if (count < length)
				word |= (buffer[count++] << 8) & 0xff00;
			iowrite16(word, nec_priv->mmiobase + FIFOB);
		}
//  avoid unnecessary HR_NFF interrupts
//		tnt_priv->imr3_bits |= HR_NFF;
//		tnt_writeb(tnt_priv, tnt_priv->imr3_bits, IMR3);
		spin_unlock_irqrestore(&board->spinlock, flags);

		if (need_resched())
			schedule();
	}
	// wait last byte has been sent
	if (retval == 0)
		retval = write_wait(board, tnt_priv, 1, send_commands);

	tnt_writeb(tnt_priv, STOP, CMDR);
	udelay(1);

	nec7210_set_reg_bits(nec_priv, IMR1, HR_ERR, 0x0);
	nec7210_set_reg_bits(nec_priv, IMR2, HR_DMAO, 0x0);
	/*
	 * force handling of any interrupts that happened
	 * while they were masked (this appears to be needed)
	 */
	tnt4882_internal_interrupt(board);
	*bytes_written = length - tnt_transfer_count(tnt_priv);
	return retval;
}

static int tnt4882_accel_write(struct gpib_board *board, u8 *buffer,
			       size_t length, int send_eoi, size_t *bytes_written)
{
	return generic_write(board, buffer, length, send_eoi, 0, bytes_written);
}

static int tnt4882_command(struct gpib_board *board, u8 *buffer, size_t length,
			   size_t *bytes_written)
{
	return generic_write(board, buffer, length, 0, 1, bytes_written);
}

static irqreturn_t tnt4882_internal_interrupt(struct gpib_board *board)
{
	struct tnt4882_priv *priv = board->private_data;
	int isr0_bits, isr3_bits, imr3_bits;
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);

	nec7210_interrupt(board, &priv->nec7210_priv);

	isr0_bits = tnt_readb(priv, ISR0);
	isr3_bits = tnt_readb(priv, ISR3);
	imr3_bits = priv->imr3_bits;

	if (isr0_bits & TNT_IFCI_BIT)
		push_gpib_event(board, EVENT_IFC);
	// XXX don't need this wakeup, one below should do?
//		wake_up_interruptible(&board->wait);

	if (isr3_bits & HR_NFF)
		priv->imr3_bits &= ~HR_NFF;
	if (isr3_bits & HR_NEF)
		priv->imr3_bits &= ~HR_NEF;
	if (isr3_bits & HR_DONE)
		priv->imr3_bits &= ~HR_DONE;
	if (isr3_bits & (HR_INTR | HR_TLCI)) {
		dev_dbg(board->gpib_dev, "minor %i isr0 0x%x imr0 0x%x isr3 0x%x imr3 0x%x\n",
			board->minor, isr0_bits, priv->imr0_bits, isr3_bits, imr3_bits);
		tnt_writeb(priv, priv->imr3_bits, IMR3);
		wake_up_interruptible(&board->wait);
	}
	spin_unlock_irqrestore(&board->spinlock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t tnt4882_interrupt(int irq, void *arg)
{
	return tnt4882_internal_interrupt(arg);
}

// wrappers for interface functions
static int tnt4882_read(struct gpib_board *board, u8 *buffer, size_t length, int *end,
			size_t *bytes_read)
{
	struct tnt4882_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;
	int retval;
	int dummy;

	retval = nec7210_read(board, &priv->nec7210_priv, buffer, length, end, bytes_read);

	if (retval < 0)	{	// force immediate holdoff
		write_byte(nec_priv, AUX_HLDI, AUXMR);

		set_bit(RFD_HOLDOFF_BN, &nec_priv->state);

		nec7210_read_data_in(board, nec_priv, &dummy);
	}
	return retval;
}

static int tnt4882_write(struct gpib_board *board, u8 *buffer, size_t length, int send_eoi,
			 size_t *bytes_written)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_write(board, &priv->nec7210_priv, buffer, length, send_eoi, bytes_written);
}

static int tnt4882_command_unaccel(struct gpib_board *board, u8 *buffer,
				   size_t length, size_t *bytes_written)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_command(board, &priv->nec7210_priv, buffer, length, bytes_written);
}

static int tnt4882_take_control(struct gpib_board *board, int synchronous)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_take_control(board, &priv->nec7210_priv, synchronous);
}

static int tnt4882_go_to_standby(struct gpib_board *board)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_go_to_standby(board, &priv->nec7210_priv);
}

static int tnt4882_request_system_control(struct gpib_board *board, int request_control)
{
	struct tnt4882_priv *priv = board->private_data;
	int retval;

	if (request_control) {
		tnt_writeb(priv, SETSC, CMDR);
		udelay(1);
	}
	retval = nec7210_request_system_control(board, &priv->nec7210_priv, request_control);
	if (!request_control) {
		tnt_writeb(priv, CLRSC, CMDR);
		udelay(1);
	}
	return retval;
}

static void tnt4882_interface_clear(struct gpib_board *board, int assert)
{
	struct tnt4882_priv *priv = board->private_data;

	nec7210_interface_clear(board, &priv->nec7210_priv, assert);
}

static void tnt4882_remote_enable(struct gpib_board *board, int enable)
{
	struct tnt4882_priv *priv = board->private_data;

	nec7210_remote_enable(board, &priv->nec7210_priv, enable);
}

static int tnt4882_enable_eos(struct gpib_board *board, u8 eos_byte, int compare_8_bits)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_enable_eos(board, &priv->nec7210_priv, eos_byte, compare_8_bits);
}

static void tnt4882_disable_eos(struct gpib_board *board)
{
	struct tnt4882_priv *priv = board->private_data;

	nec7210_disable_eos(board, &priv->nec7210_priv);
}

static unsigned int tnt4882_update_status(struct gpib_board *board, unsigned int clear_mask)
{
	unsigned long flags;
	u8 line_status;
	struct tnt4882_priv *priv = board->private_data;

	spin_lock_irqsave(&board->spinlock, flags);
	board->status &= ~clear_mask;
	nec7210_update_status_nolock(board, &priv->nec7210_priv);
	/* set / clear SRQ state since it is not cleared by interrupt */
	line_status = tnt_readb(priv, BSR);
	if (line_status & BCSR_SRQ_BIT)
		set_bit(SRQI_NUM, &board->status);
	else
		clear_bit(SRQI_NUM, &board->status);
	spin_unlock_irqrestore(&board->spinlock, flags);
	return board->status;
}

static int tnt4882_primary_address(struct gpib_board *board, unsigned int address)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_primary_address(board, &priv->nec7210_priv, address);
}

static int tnt4882_secondary_address(struct gpib_board *board, unsigned int address, int enable)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_secondary_address(board, &priv->nec7210_priv, address, enable);
}

static int tnt4882_parallel_poll(struct gpib_board *board, u8 *result)
{
	struct tnt4882_priv *tnt_priv = board->private_data;

	if (tnt_priv->nec7210_priv.type != NEC7210) {
		tnt_priv->auxg_bits |= RPP2_BIT;
		write_byte(&tnt_priv->nec7210_priv, tnt_priv->auxg_bits, AUXMR);
		udelay(2);	// FIXME use parallel poll timeout
		*result = read_byte(&tnt_priv->nec7210_priv, CPTR);
		tnt_priv->auxg_bits &= ~RPP2_BIT;
		write_byte(&tnt_priv->nec7210_priv, tnt_priv->auxg_bits, AUXMR);
		return 0;
	} else {
		return nec7210_parallel_poll(board, &tnt_priv->nec7210_priv, result);
	}
}

static void tnt4882_parallel_poll_configure(struct gpib_board *board, u8 config)
{
	struct tnt4882_priv *priv = board->private_data;

	if (priv->nec7210_priv.type == TNT5004) {
		/* configure locally */
		write_byte(&priv->nec7210_priv, AUXRI | 0x4, AUXMR);
		if (config)
			/* set response + clear sense */
			write_byte(&priv->nec7210_priv, PPR | config, AUXMR);
		else
			/* disable ppoll */
			write_byte(&priv->nec7210_priv, PPR | 0x10, AUXMR);
	} else {
		nec7210_parallel_poll_configure(board, &priv->nec7210_priv, config);
	}
}

static void tnt4882_parallel_poll_response(struct gpib_board *board, int ist)
{
	struct tnt4882_priv *priv = board->private_data;

	nec7210_parallel_poll_response(board, &priv->nec7210_priv, ist);
}

/*
 * this is just used by the old nec7210 isa interfaces, the newer
 * boards use tnt4882_serial_poll_response2
 */
static void tnt4882_serial_poll_response(struct gpib_board *board, u8 status)
{
	struct tnt4882_priv *priv = board->private_data;

	nec7210_serial_poll_response(board, &priv->nec7210_priv, status);
}

static void tnt4882_serial_poll_response2(struct gpib_board *board, u8 status,
					  int new_reason_for_service)
{
	struct tnt4882_priv *priv = board->private_data;
	unsigned long flags;
	const int MSS = status & request_service_bit;
	const int reqt = MSS && new_reason_for_service;
	const int reqf = MSS == 0;

	spin_lock_irqsave(&board->spinlock, flags);
	if (reqt) {
		priv->nec7210_priv.srq_pending = 1;
		clear_bit(SPOLL_NUM, &board->status);
	} else {
		if (reqf)
			priv->nec7210_priv.srq_pending = 0;
	}
	if (reqt)
		/*
		 * It may seem like a race to issue reqt before updating
		 * the status byte, but it is not.  The chip does not
		 * issue the reqt until the SPMR is written to at
		 * a later time.
		 */
		write_byte(&priv->nec7210_priv, AUX_REQT, AUXMR);
	else if (reqf)
		write_byte(&priv->nec7210_priv, AUX_REQF, AUXMR);
	/*
	 * We need to always zero bit 6 of the status byte before writing it to
	 * the SPMR to insure we are using
	 * serial poll mode SP1, and not accidentally triggering mode SP3.
	 */
	write_byte(&priv->nec7210_priv, status & ~request_service_bit, SPMR);
	spin_unlock_irqrestore(&board->spinlock, flags);
}

static u8 tnt4882_serial_poll_status(struct gpib_board *board)
{
	struct tnt4882_priv *priv = board->private_data;

	return nec7210_serial_poll_status(board, &priv->nec7210_priv);
}

static void tnt4882_return_to_local(struct gpib_board *board)
{
	struct tnt4882_priv *priv = board->private_data;

	nec7210_return_to_local(board, &priv->nec7210_priv);
}

static void tnt4882_board_reset(struct tnt4882_priv *tnt_priv, struct gpib_board *board)
{
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;

	tnt_priv->imr0_bits = 0;
	tnt_writeb(tnt_priv, tnt_priv->imr0_bits, IMR0);
	tnt_priv->imr3_bits = 0;
	tnt_writeb(tnt_priv, tnt_priv->imr3_bits, IMR3);
	tnt_readb(tnt_priv, IMR0);
	tnt_readb(tnt_priv, IMR3);
	nec7210_board_reset(nec_priv, board);
}

static int tnt4882_allocate_private(struct gpib_board *board)
{
	struct tnt4882_priv *tnt_priv;

	board->private_data = kmalloc(sizeof(struct tnt4882_priv), GFP_KERNEL);
	if (!board->private_data)
		return -1;
	tnt_priv = board->private_data;
	memset(tnt_priv, 0, sizeof(struct tnt4882_priv));
	init_nec7210_private(&tnt_priv->nec7210_priv);
	return 0;
}

static void tnt4882_free_private(struct gpib_board *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

static void tnt4882_init(struct tnt4882_priv *tnt_priv, const struct gpib_board *board)
{
	struct nec7210_priv *nec_priv = &tnt_priv->nec7210_priv;

	/* Turbo488 software reset */
	tnt_writeb(tnt_priv, SOFT_RESET, CMDR);
	udelay(1);

	// turn off one-chip mode
	tnt_writeb(tnt_priv, NODMA, HSSEL);
	tnt_writeb(tnt_priv, 0, ACCWR);
	// make sure we are in 7210 mode
	tnt_writeb(tnt_priv, AUX_7210, AUXCR);
	udelay(1);
	// registers might be swapped, so write it to the swapped address too
	tnt_writeb(tnt_priv, AUX_7210, SWAPPED_AUXCR);
	udelay(1);
	// turn on one-chip mode
	if (nec_priv->type == TNT4882 || nec_priv->type == TNT5004)
		tnt_writeb(tnt_priv, NODMA | TNT_ONE_CHIP_BIT, HSSEL);
	else
		tnt_writeb(tnt_priv, NODMA, HSSEL);

	nec7210_board_reset(nec_priv, board);
	// read-clear isr0
	tnt_readb(tnt_priv, ISR0);

	// enable passing of nat4882 interrupts
	tnt_priv->imr3_bits = HR_TLCI;
	tnt_writeb(tnt_priv, tnt_priv->imr3_bits, IMR3);

	// enable interrupt
	tnt_writeb(tnt_priv, 0x1, INTRT);

	// force immediate holdoff
	write_byte(&tnt_priv->nec7210_priv, AUX_HLDI, AUXMR);

	set_bit(RFD_HOLDOFF_BN, &nec_priv->state);

	tnt_priv->auxg_bits = AUXRG | NTNL_BIT;
	write_byte(&tnt_priv->nec7210_priv, tnt_priv->auxg_bits, AUXMR);

	nec7210_board_online(nec_priv, board);
	// enable interface clear interrupt for event queue
	tnt_priv->imr0_bits = TNT_IMR0_ALWAYS_BITS | TNT_ATNI_BIT | TNT_IFCIE_BIT;
	tnt_writeb(tnt_priv, tnt_priv->imr0_bits, IMR0);
}

static int ni_pci_attach(struct gpib_board *board, const struct gpib_board_config *config)
{
	struct tnt4882_priv *tnt_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = IRQF_SHARED;
	int retval;
	struct mite_struct *mite;

	board->status = 0;

	if (tnt4882_allocate_private(board))
		return -ENOMEM;
	tnt_priv = board->private_data;
	nec_priv = &tnt_priv->nec7210_priv;
	nec_priv->type = TNT4882;
	nec_priv->read_byte = nec7210_locking_iomem_read_byte;
	nec_priv->write_byte = nec7210_locking_iomem_write_byte;
	nec_priv->offset = atgpib_reg_offset;

	if (!mite_devices)
		return -ENODEV;

	for (mite = mite_devices; mite; mite = mite->next) {
		short found_board;

		if (mite->used)
			continue;
		if (config->pci_bus >= 0 && config->pci_bus != mite->pcidev->bus->number)
			continue;
		if (config->pci_slot >= 0 && config->pci_slot != PCI_SLOT(mite->pcidev->devfn))
			continue;
		switch (mite_device_id(mite)) {
		case PCI_DEVICE_ID_NI_GPIB:
		case PCI_DEVICE_ID_NI_GPIB_PLUS:
		case PCI_DEVICE_ID_NI_GPIB_PLUS2:
		case PCI_DEVICE_ID_NI_PXIGPIB:
		case PCI_DEVICE_ID_NI_PMCGPIB:
		case PCI_DEVICE_ID_NI_PCIEGPIB:
		case PCI_DEVICE_ID_NI_PCIE2GPIB:
// support for Measurement Computing PCI-488
		case PCI_DEVICE_ID_MC_PCI488:
		case PCI_DEVICE_ID_CEC_NI_GPIB:
			found_board = 1;
			break;
		default:
			found_board = 0;
			break;
		}
		if (found_board)
			break;
	}
	if (!mite)
		return -ENODEV;

	tnt_priv->mite = mite;
	retval = mite_setup(tnt_priv->mite);
	if (retval < 0)
		return retval;

	nec_priv->mmiobase = tnt_priv->mite->daq_io_addr;

	// get irq
	retval = request_irq(mite_irq(tnt_priv->mite), tnt4882_interrupt, isr_flags, "ni-pci-gpib",
			     board);
	if (retval) {
		dev_err(board->gpib_dev, "failed to obtain pci irq %d\n", mite_irq(tnt_priv->mite));
		return retval;
	}
	tnt_priv->irq = mite_irq(tnt_priv->mite);

	// TNT5004 detection
	switch (tnt_readb(tnt_priv, CSR) & 0xf0) {
	case 0x30:
		nec_priv->type = TNT4882;
		break;
	case 0x40:
		nec_priv->type = TNT5004;
		break;
	}
	tnt4882_init(tnt_priv, board);

	return 0;
}

static void ni_pci_detach(struct gpib_board *board)
{
	struct tnt4882_priv *tnt_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (tnt_priv) {
		nec_priv = &tnt_priv->nec7210_priv;

		if (nec_priv->mmiobase)
			tnt4882_board_reset(tnt_priv, board);
		if (tnt_priv->irq)
			free_irq(tnt_priv->irq, board);
		if (tnt_priv->mite)
			mite_unsetup(tnt_priv->mite);
	}
	tnt4882_free_private(board);
}

static int ni_isapnp_find(struct pnp_dev **dev)
{
	*dev = pnp_find_dev(NULL, ISAPNP_VENDOR_ID_NI,
			    ISAPNP_FUNCTION(ISAPNP_ID_NI_ATGPIB_TNT), NULL);
	if (!*dev || !(*dev)->card)
		return -ENODEV;
	if (pnp_device_attach(*dev) < 0)
		return -EBUSY;
	if (pnp_activate_dev(*dev) < 0)	{
		pnp_device_detach(*dev);
		return -EAGAIN;
	}
	if (!pnp_port_valid(*dev, 0) || !pnp_irq_valid(*dev, 0)) {
		pnp_device_detach(*dev);
		return -EINVAL;
	}
	return 0;
}

static int ni_isa_attach_common(struct gpib_board *board, const struct gpib_board_config *config,
				enum nec7210_chipset chipset)
{
	struct tnt4882_priv *tnt_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = 0;
	u32 iobase;
	int irq;
	int retval;

	board->status = 0;

	if (tnt4882_allocate_private(board))
		return -ENOMEM;
	tnt_priv = board->private_data;
	nec_priv = &tnt_priv->nec7210_priv;
	nec_priv->type = chipset;
	nec_priv->read_byte = nec7210_locking_ioport_read_byte;
	nec_priv->write_byte = nec7210_locking_ioport_write_byte;
	nec_priv->offset = atgpib_reg_offset;

	// look for plug-n-play board
	if (config->ibbase == 0) {
		struct pnp_dev *dev;

		retval = ni_isapnp_find(&dev);
		if (retval < 0)
			return retval;
		tnt_priv->pnp_dev = dev;
		iobase = pnp_port_start(dev, 0);
		irq = pnp_irq(dev, 0);
	} else {
		iobase = config->ibbase;
		irq = config->ibirq;
	}
	// allocate ioports
	if (!request_region(iobase, atgpib_iosize, "atgpib"))
		return -EBUSY;

	nec_priv->mmiobase = ioport_map(iobase, atgpib_iosize);
	if (!nec_priv->mmiobase)
		return -EBUSY;

	// get irq
	retval = request_irq(irq, tnt4882_interrupt, isr_flags, "atgpib", board);
	if (retval) {
		dev_err(board->gpib_dev, "failed to request ISA irq %d\n", irq);
		return retval;
	}
	tnt_priv->irq = irq;

	tnt4882_init(tnt_priv, board);

	return 0;
}

static int ni_tnt_isa_attach(struct gpib_board *board, const struct gpib_board_config *config)
{
	return ni_isa_attach_common(board, config, TNT4882);
}

static int ni_nat4882_isa_attach(struct gpib_board *board, const struct gpib_board_config *config)
{
	return ni_isa_attach_common(board, config, NAT4882);
}

static int ni_nec_isa_attach(struct gpib_board *board, const struct gpib_board_config *config)
{
	return ni_isa_attach_common(board, config, NEC7210);
}

static void ni_isa_detach(struct gpib_board *board)
{
	struct tnt4882_priv *tnt_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (tnt_priv) {
		nec_priv = &tnt_priv->nec7210_priv;
		if (nec_priv->iobase)
			tnt4882_board_reset(tnt_priv, board);
		if (tnt_priv->irq)
			free_irq(tnt_priv->irq, board);
		if (nec_priv->mmiobase)
			ioport_unmap(nec_priv->mmiobase);
		if (nec_priv->iobase)
			release_region(nec_priv->iobase, atgpib_iosize);
		if (tnt_priv->pnp_dev)
			pnp_device_detach(tnt_priv->pnp_dev);
	}
	tnt4882_free_private(board);
}

static int tnt4882_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	return 0;
}

static struct gpib_interface ni_pci_interface = {
	.name = "ni_pci",
	.attach = ni_pci_attach,
	.detach = ni_pci_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response2 = tnt4882_serial_poll_response2,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_pci_accel_interface = {
	.name = "ni_pci_accel",
	.attach = ni_pci_attach,
	.detach = ni_pci_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response2 = tnt4882_serial_poll_response2,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_isa_interface = {
	.name = "ni_isa",
	.attach = ni_tnt_isa_attach,
	.detach = ni_isa_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response2 = tnt4882_serial_poll_response2,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_nat4882_isa_interface = {
	.name = "ni_nat4882_isa",
	.attach = ni_nat4882_isa_attach,
	.detach = ni_isa_detach,
	.read = tnt4882_read,
	.write = tnt4882_write,
	.command = tnt4882_command_unaccel,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response2 = tnt4882_serial_poll_response2,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_nec_isa_interface = {
	.name = "ni_nec_isa",
	.attach = ni_nec_isa_attach,
	.detach = ni_isa_detach,
	.read = tnt4882_read,
	.write = tnt4882_write,
	.command = tnt4882_command_unaccel,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = NULL,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response = tnt4882_serial_poll_response,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_isa_accel_interface = {
	.name = "ni_isa_accel",
	.attach = ni_tnt_isa_attach,
	.detach = ni_isa_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response2 = tnt4882_serial_poll_response2,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_nat4882_isa_accel_interface = {
	.name = "ni_nat4882_isa_accel",
	.attach = ni_nat4882_isa_attach,
	.detach = ni_isa_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command_unaccel,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response2 = tnt4882_serial_poll_response2,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_nec_isa_accel_interface = {
	.name = "ni_nec_isa_accel",
	.attach = ni_nec_isa_attach,
	.detach = ni_isa_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command_unaccel,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = NULL,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response = tnt4882_serial_poll_response,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static const struct pci_device_id tnt4882_pci_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_NI_GPIB)},
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_NI_GPIB_PLUS)},
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_NI_GPIB_PLUS2)},
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_NI_PXIGPIB)},
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_NI_PMCGPIB)},
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_NI_PCIEGPIB)},
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_NI_PCIE2GPIB)},
	// support for Measurement Computing PCI-488
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_MC_PCI488)},
	{PCI_DEVICE(PCI_VENDOR_ID_NATINST, PCI_DEVICE_ID_CEC_NI_GPIB)},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, tnt4882_pci_table);

static struct pci_driver tnt4882_pci_driver = {
	.name = DRV_NAME,
	.id_table = tnt4882_pci_table,
	.probe = &tnt4882_pci_probe
};

#if 0
/* unused, will be needed when the driver is turned into a pnp_driver */
static const struct pnp_device_id tnt4882_pnp_table[] = {
	{.id = "NICC601"},
	{.id = ""}
};
MODULE_DEVICE_TABLE(pnp, tnt4882_pnp_table);
#endif

#ifdef CONFIG_GPIB_PCMCIA
static struct gpib_interface ni_pcmcia_interface;
static struct gpib_interface ni_pcmcia_accel_interface;
static int __init init_ni_gpib_cs(void);
static void __exit exit_ni_gpib_cs(void);
#endif

static int __init tnt4882_init_module(void)
{
	int result;

	result = pci_register_driver(&tnt4882_pci_driver);
	if (result) {
		pr_err("pci_register_driver failed: error = %d\n", result);
		return result;
	}

	result = gpib_register_driver(&ni_isa_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_isa;
	}

	result = gpib_register_driver(&ni_isa_accel_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_isa_accel;
	}

	result = gpib_register_driver(&ni_nat4882_isa_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_nat4882_isa;
	}

	result = gpib_register_driver(&ni_nat4882_isa_accel_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_nat4882_isa_accel;
	}

	result = gpib_register_driver(&ni_nec_isa_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_nec_isa;
	}

	result = gpib_register_driver(&ni_nec_isa_accel_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_nec_isa_accel;
	}

	result = gpib_register_driver(&ni_pci_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_pci;
	}

	result = gpib_register_driver(&ni_pci_accel_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_pci_accel;
	}

#ifdef CONFIG_GPIB_PCMCIA
	result = gpib_register_driver(&ni_pcmcia_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_pcmcia;
	}

	result = gpib_register_driver(&ni_pcmcia_accel_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		goto err_pcmcia_accel;
	}

	result = init_ni_gpib_cs();
	if (result) {
		pr_err("pcmcia_register_driver failed: error = %d\n", result);
		goto err_pcmcia_driver;
	}
#endif

	mite_init();

	return 0;

#ifdef CONFIG_GPIB_PCMCIA
err_pcmcia_driver:
	gpib_unregister_driver(&ni_pcmcia_accel_interface);
err_pcmcia_accel:
	gpib_unregister_driver(&ni_pcmcia_interface);
err_pcmcia:
#endif
	gpib_unregister_driver(&ni_pci_accel_interface);
err_pci_accel:
	gpib_unregister_driver(&ni_pci_interface);
err_pci:
	gpib_unregister_driver(&ni_nec_isa_accel_interface);
err_nec_isa_accel:
	gpib_unregister_driver(&ni_nec_isa_interface);
err_nec_isa:
	gpib_unregister_driver(&ni_nat4882_isa_accel_interface);
err_nat4882_isa_accel:
	gpib_unregister_driver(&ni_nat4882_isa_interface);
err_nat4882_isa:
	gpib_unregister_driver(&ni_isa_accel_interface);
err_isa_accel:
	gpib_unregister_driver(&ni_isa_interface);
err_isa:
	pci_unregister_driver(&tnt4882_pci_driver);

	return result;
}

static void __exit tnt4882_exit_module(void)
{
	gpib_unregister_driver(&ni_isa_interface);
	gpib_unregister_driver(&ni_isa_accel_interface);
	gpib_unregister_driver(&ni_nat4882_isa_interface);
	gpib_unregister_driver(&ni_nat4882_isa_accel_interface);
	gpib_unregister_driver(&ni_nec_isa_interface);
	gpib_unregister_driver(&ni_nec_isa_accel_interface);
	gpib_unregister_driver(&ni_pci_interface);
	gpib_unregister_driver(&ni_pci_accel_interface);
#ifdef CONFIG_GPIB_PCMCIA
	gpib_unregister_driver(&ni_pcmcia_interface);
	gpib_unregister_driver(&ni_pcmcia_accel_interface);
	exit_ni_gpib_cs();
#endif

	mite_cleanup();

	pci_unregister_driver(&tnt4882_pci_driver);
}

#ifdef CONFIG_GPIB_PCMCIA

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static int ni_gpib_config(struct pcmcia_device  *link);
static void ni_gpib_release(struct pcmcia_device *link);
static void ni_pcmcia_detach(struct gpib_board *board);

/*
 * A linked list of "instances" of the dummy device.  Each actual
 * PCMCIA card corresponds to one device instance, and is described
 * by one dev_link_t structure (defined in ds.h).
 *
 * You may not want to use a linked list for this -- for example, the
 * memory card driver uses an array of dev_link_t pointers, where minor
 * device numbers are used to derive the corresponding array index.
 *
 * I think this dev_list is obsolete but the pointer is needed to keep
 * the module instance for the ni_pcmcia_attach function.
 */

static struct pcmcia_device   *curr_dev;

struct local_info_t {
	struct pcmcia_device	*p_dev;
	struct gpib_board		*dev;
	int			stop;
	struct bus_operations	*bus;
};

/*
 * ni_gpib_probe() creates an "instance" of the driver, allocating
 * local data structures for one device.  The device is registered
 * with Card Services.
 */

static int ni_gpib_probe(struct pcmcia_device *link)
{
	struct local_info_t *info;
	//struct struct gpib_board *dev;

	/* Allocate space for private device-specific data */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->p_dev = link;
	link->priv = info;

	/*
	 * General socket configuration defaults can go here.  In this
	 * client, we assume very little, and rely on the CIS for almost
	 * everything.  In most clients, many details (i.e., number, sizes,
	 * and attributes of IO windows) are fixed by the nature of the
	 * device, and can be hard-wired here.
	 */
	link->config_flags = CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	/* Register with Card Services */
	curr_dev = link;
	return ni_gpib_config(link);
}

/*
 * This deletes a driver "instance".  The device is de-registered
 * with Card Services.  If it has been released, all local data
 * structures are freed.  Otherwise, the structures will be freed
 * when the device is released.
 */
static void ni_gpib_remove(struct pcmcia_device *link)
{
	struct local_info_t *info = link->priv;
	//struct struct gpib_board *dev = info->dev;

	if (info->dev)
		ni_pcmcia_detach(info->dev);
	ni_gpib_release(link);

	//free_netdev(dev);
	kfree(info);
}

static int ni_gpib_config_iteration(struct pcmcia_device *link,	void *priv_data)
{
	int retval;

	retval = pcmcia_request_io(link);
	if (retval != 0)
		return retval;

	return 0;
}

/*
 * ni_gpib_config() is scheduled to run after a CARD_INSERTION event
 * is received, to configure the PCMCIA socket, and to make the
 * device available to the system.
 */
static int ni_gpib_config(struct pcmcia_device *link)
{
	//struct local_info_t *info = link->priv;
	//struct gpib_board *dev = info->dev;
	int last_ret;

	last_ret = pcmcia_loop_config(link, &ni_gpib_config_iteration, NULL);
	if (last_ret) {
		dev_warn(&link->dev, "no configuration found\n");
		ni_gpib_release(link);
		return last_ret;
	}

	last_ret = pcmcia_enable_device(link);
	if (last_ret) {
		ni_gpib_release(link);
		return last_ret;
	}
	return 0;
} /* ni_gpib_config */

/*
 * After a card is removed, ni_gpib_release() will unregister the
 * device, and release the PCMCIA configuration.  If the device is
 * still open, this will be postponed until it is closed.
 */
static void ni_gpib_release(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
} /* ni_gpib_release */

static int ni_gpib_suspend(struct pcmcia_device *link)
{
	//struct local_info_t *info = link->priv;
	//struct struct gpib_board *dev = info->dev;

	if (link->open)
		dev_warn(&link->dev, "Device still open\n");
	//netif_device_detach(dev);

	return 0;
}

static int ni_gpib_resume(struct pcmcia_device *link)
{
	//struct local_info_t *info = link->priv;
	//struct struct gpib_board *dev = info->dev;

	/*if (link->open) {
	 *	ni_gpib_probe(dev);	/ really?
	 *	//netif_device_attach(dev);
	 *}
	 */
	return ni_gpib_config(link);
}

static struct pcmcia_device_id ni_pcmcia_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x4882),
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0c71), // NI PCMCIA-GPIB+
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, ni_pcmcia_ids);

static struct pcmcia_driver ni_gpib_cs_driver = {
	.name           = "ni_gpib_cs",
	.owner		= THIS_MODULE,
	.drv = { .name = "ni_gpib_cs", },
	.id_table	= ni_pcmcia_ids,
	.probe		= ni_gpib_probe,
	.remove		= ni_gpib_remove,
	.suspend	= ni_gpib_suspend,
	.resume		= ni_gpib_resume,
};

static int __init init_ni_gpib_cs(void)
{
	return pcmcia_register_driver(&ni_gpib_cs_driver);
}

static void __exit exit_ni_gpib_cs(void)
{
	pcmcia_unregister_driver(&ni_gpib_cs_driver);
}

static const int pcmcia_gpib_iosize = 32;

static int ni_pcmcia_attach(struct gpib_board *board, const struct gpib_board_config *config)
{
	struct local_info_t *info;
	struct tnt4882_priv *tnt_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = IRQF_SHARED;
	int retval;

	if (!curr_dev)
		return -ENODEV;

	info = curr_dev->priv;
	info->dev = board;

	board->status = 0;

	if (tnt4882_allocate_private(board))
		return -ENOMEM;

	tnt_priv = board->private_data;
	nec_priv = &tnt_priv->nec7210_priv;
	nec_priv->type = TNT4882;
	nec_priv->read_byte = nec7210_locking_ioport_read_byte;
	nec_priv->write_byte = nec7210_locking_ioport_write_byte;
	nec_priv->offset = atgpib_reg_offset;

	if (!request_region(curr_dev->resource[0]->start, resource_size(curr_dev->resource[0]),
			    DRV_NAME))
		return -ENOMEM;

	nec_priv->mmiobase = ioport_map(curr_dev->resource[0]->start,
					resource_size(curr_dev->resource[0]));
	if (!nec_priv->mmiobase)
		return -ENOMEM;

	// get irq
	retval = request_irq(curr_dev->irq, tnt4882_interrupt, isr_flags, DRV_NAME, board);
	if (retval) {
		dev_err(board->gpib_dev, "failed to obtain PCMCIA irq %d\n", curr_dev->irq);
		return retval;
	}
	tnt_priv->irq = curr_dev->irq;

	tnt4882_init(tnt_priv, board);

	return 0;
}

static void ni_pcmcia_detach(struct gpib_board *board)
{
	struct tnt4882_priv *tnt_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (tnt_priv) {
		nec_priv = &tnt_priv->nec7210_priv;
		if (tnt_priv->irq)
			free_irq(tnt_priv->irq, board);
		if (nec_priv->mmiobase)
			ioport_unmap(nec_priv->mmiobase);
		if (nec_priv->iobase) {
			tnt4882_board_reset(tnt_priv, board);
			release_region(nec_priv->iobase, pcmcia_gpib_iosize);
		}
	}
	tnt4882_free_private(board);
}

static struct gpib_interface ni_pcmcia_interface = {
	.name = "ni_pcmcia",
	.attach = ni_pcmcia_attach,
	.detach = ni_pcmcia_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response = tnt4882_serial_poll_response,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

static struct gpib_interface ni_pcmcia_accel_interface = {
	.name = "ni_pcmcia_accel",
	.attach = ni_pcmcia_attach,
	.detach = ni_pcmcia_detach,
	.read = tnt4882_accel_read,
	.write = tnt4882_accel_write,
	.command = tnt4882_command,
	.take_control = tnt4882_take_control,
	.go_to_standby = tnt4882_go_to_standby,
	.request_system_control = tnt4882_request_system_control,
	.interface_clear = tnt4882_interface_clear,
	.remote_enable = tnt4882_remote_enable,
	.enable_eos = tnt4882_enable_eos,
	.disable_eos = tnt4882_disable_eos,
	.parallel_poll = tnt4882_parallel_poll,
	.parallel_poll_configure = tnt4882_parallel_poll_configure,
	.parallel_poll_response = tnt4882_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = tnt4882_line_status,
	.update_status = tnt4882_update_status,
	.primary_address = tnt4882_primary_address,
	.secondary_address = tnt4882_secondary_address,
	.serial_poll_response = tnt4882_serial_poll_response,
	.serial_poll_status = tnt4882_serial_poll_status,
	.t1_delay = tnt4882_t1_delay,
	.return_to_local = tnt4882_return_to_local,
};

#endif	// CONFIG_GPIB_PCMCIA

module_init(tnt4882_init_module);
module_exit(tnt4882_exit_module);
