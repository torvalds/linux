// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *   copyright            : (C) 2001, 2002 by Frank Mori Hess
 ***************************************************************************/

#define dev_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "board.h"
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
#include <linux/spinlock.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB library code for NEC uPD7210");

int nec7210_enable_eos(struct gpib_board *board, struct nec7210_priv *priv, uint8_t eos_byte,
		       int compare_8_bits)
{
	write_byte(priv, eos_byte, EOSR);
	priv->auxa_bits |= HR_REOS;
	if (compare_8_bits)
		priv->auxa_bits |= HR_BIN;
	else
		priv->auxa_bits &= ~HR_BIN;
	write_byte(priv, priv->auxa_bits, AUXMR);
	return 0;
}
EXPORT_SYMBOL(nec7210_enable_eos);

void nec7210_disable_eos(struct gpib_board *board, struct nec7210_priv *priv)
{
	priv->auxa_bits &= ~HR_REOS;
	write_byte(priv, priv->auxa_bits, AUXMR);
}
EXPORT_SYMBOL(nec7210_disable_eos);

int nec7210_parallel_poll(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *result)
{
	int ret;

	clear_bit(COMMAND_READY_BN, &priv->state);

	// execute parallel poll
	write_byte(priv, AUX_EPP, AUXMR);
	// wait for result FIXME: support timeouts
	ret = wait_event_interruptible(board->wait, test_bit(COMMAND_READY_BN, &priv->state));
	if (ret) {
		dev_dbg(board->gpib_dev, "gpib: parallel poll interrupted\n");
		return -ERESTARTSYS;
	}
	*result = read_byte(priv, CPTR);

	return 0;
}
EXPORT_SYMBOL(nec7210_parallel_poll);

void nec7210_parallel_poll_configure(struct gpib_board *board,
				     struct nec7210_priv *priv, unsigned int configuration)
{
	write_byte(priv, PPR | configuration, AUXMR);
}
EXPORT_SYMBOL(nec7210_parallel_poll_configure);

void nec7210_parallel_poll_response(struct gpib_board *board, struct nec7210_priv *priv, int ist)
{
	if (ist)
		write_byte(priv, AUX_SPPF, AUXMR);
	else
		write_byte(priv, AUX_CPPF, AUXMR);
}
EXPORT_SYMBOL(nec7210_parallel_poll_response);
/* This is really only adequate for chips that do a 488.2 style reqt/reqf
 * based on bit 6 of the SPMR (see chapter 11.3.3 of 488.2). For simpler chips that simply
 * set rsv directly based on bit 6, we either need to do more hardware setup to expose
 * the 488.2 capability (for example with NI chips), or we need to implement the
 * 488.2 set srv state machine in the driver (if that is even viable).
 */
void nec7210_serial_poll_response(struct gpib_board *board,
				  struct nec7210_priv *priv, uint8_t status)
{
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);
	if (status & request_service_bit) {
		priv->srq_pending = 1;
		clear_bit(SPOLL_NUM, &board->status);

	} else {
		priv->srq_pending = 0;
	}
	write_byte(priv, status, SPMR);
	spin_unlock_irqrestore(&board->spinlock, flags);
}
EXPORT_SYMBOL(nec7210_serial_poll_response);

uint8_t nec7210_serial_poll_status(struct gpib_board *board, struct nec7210_priv *priv)
{
	return read_byte(priv, SPSR);
}
EXPORT_SYMBOL(nec7210_serial_poll_status);

int nec7210_primary_address(const struct gpib_board *board, struct nec7210_priv *priv,
			    unsigned int address)
{
	// put primary address in address0
	write_byte(priv, address & ADDRESS_MASK, ADR);
	return 0;
}
EXPORT_SYMBOL(nec7210_primary_address);

int nec7210_secondary_address(const struct gpib_board *board, struct nec7210_priv *priv,
			      unsigned int address, int enable)
{
	if (enable) {
		// put secondary address in address1
		write_byte(priv, HR_ARS | (address & ADDRESS_MASK), ADR);
		// go to address mode 2
		priv->reg_bits[ADMR] &= ~HR_ADM0;
		priv->reg_bits[ADMR] |= HR_ADM1;
	} else {
		// disable address1 register
		write_byte(priv, HR_ARS | HR_DT | HR_DL, ADR);
		// go to address mode 1
		priv->reg_bits[ADMR] |= HR_ADM0;
		priv->reg_bits[ADMR] &= ~HR_ADM1;
	}
	write_byte(priv, priv->reg_bits[ADMR], ADMR);
	return 0;
}
EXPORT_SYMBOL(nec7210_secondary_address);

static void update_talker_state(struct nec7210_priv *priv, unsigned int address_status_bits)
{
	if ((address_status_bits & HR_TA)) {
		if ((address_status_bits & HR_NATN)) {
			if (address_status_bits & HR_SPMS)
				priv->talker_state = serial_poll_active;
			else
				priv->talker_state = talker_active;
		} else {
			priv->talker_state = talker_addressed;
		}
	} else {
		priv->talker_state = talker_idle;
	}
}

static void update_listener_state(struct nec7210_priv *priv, unsigned int address_status_bits)
{
	if (address_status_bits & HR_LA) {
		if ((address_status_bits & HR_NATN))
			priv->listener_state = listener_active;
		else
			priv->listener_state = listener_addressed;
	} else {
		priv->listener_state = listener_idle;
	}
}

unsigned int nec7210_update_status_nolock(struct gpib_board *board, struct nec7210_priv *priv)
{
	int address_status_bits;
	u8 spoll_status;

	if (!priv)
		return 0;

	address_status_bits = read_byte(priv, ADSR);
	if (address_status_bits & HR_CIC)
		set_bit(CIC_NUM, &board->status);
	else
		clear_bit(CIC_NUM, &board->status);
	// check for talker/listener addressed
	update_talker_state(priv, address_status_bits);
	if (priv->talker_state == talker_active || priv->talker_state == talker_addressed)
		set_bit(TACS_NUM, &board->status);
	else
		clear_bit(TACS_NUM, &board->status);
	update_listener_state(priv, address_status_bits);
	if (priv->listener_state == listener_active ||
	    priv->listener_state == listener_addressed)
		set_bit(LACS_NUM, &board->status);
	else
		clear_bit(LACS_NUM, &board->status);
	if (address_status_bits & HR_NATN)
		clear_bit(ATN_NUM, &board->status);
	else
		set_bit(ATN_NUM, &board->status);
	spoll_status = nec7210_serial_poll_status(board, priv);
	if (priv->srq_pending && (spoll_status & request_service_bit) == 0) {
		priv->srq_pending = 0;
		set_bit(SPOLL_NUM, &board->status);
	}

	/* we rely on the interrupt handler to set the
	 * rest of the status bits
	 */

	return board->status;
}
EXPORT_SYMBOL(nec7210_update_status_nolock);

unsigned int nec7210_update_status(struct gpib_board *board, struct nec7210_priv *priv,
				   unsigned int clear_mask)
{
	unsigned long flags;
	unsigned int retval;

	spin_lock_irqsave(&board->spinlock, flags);
	board->status &= ~clear_mask;
	retval = nec7210_update_status_nolock(board, priv);
	spin_unlock_irqrestore(&board->spinlock, flags);

	return retval;
}
EXPORT_SYMBOL(nec7210_update_status);

unsigned int nec7210_set_reg_bits(struct nec7210_priv *priv, unsigned int reg,
				  unsigned int mask, unsigned int bits)
{
	priv->reg_bits[reg] &= ~mask;
	priv->reg_bits[reg] |= mask & bits;
	write_byte(priv, priv->reg_bits[reg], reg);
	return priv->reg_bits[reg];
}
EXPORT_SYMBOL(nec7210_set_reg_bits);

void nec7210_set_handshake_mode(struct gpib_board *board, struct nec7210_priv *priv, int mode)
{
	unsigned long flags;

	mode &= HR_HANDSHAKE_MASK;

	spin_lock_irqsave(&board->spinlock, flags);
	if ((priv->auxa_bits & HR_HANDSHAKE_MASK) != mode) {
		priv->auxa_bits &= ~HR_HANDSHAKE_MASK;
		priv->auxa_bits |= mode;
		write_byte(priv, priv->auxa_bits, AUXMR);
	}
	spin_unlock_irqrestore(&board->spinlock, flags);
}
EXPORT_SYMBOL(nec7210_set_handshake_mode);

uint8_t nec7210_read_data_in(struct gpib_board *board, struct nec7210_priv *priv, int *end)
{
	unsigned long flags;
	u8 data;

	spin_lock_irqsave(&board->spinlock, flags);
	data = read_byte(priv, DIR);
	clear_bit(READ_READY_BN, &priv->state);
	if (test_and_clear_bit(RECEIVED_END_BN, &priv->state))
		*end = 1;
	else
		*end = 0;
	spin_unlock_irqrestore(&board->spinlock, flags);

	return data;
}
EXPORT_SYMBOL(nec7210_read_data_in);

int nec7210_take_control(struct gpib_board *board, struct nec7210_priv *priv, int syncronous)
{
	int i;
	const int timeout = 100;
	int retval = 0;
	unsigned int adsr_bits = 0;

	if (syncronous)
		write_byte(priv, AUX_TCS, AUXMR);
	else
		write_byte(priv, AUX_TCA, AUXMR);
	// busy wait until ATN is asserted
	for (i = 0; i < timeout; i++) {
		adsr_bits = read_byte(priv, ADSR);
		if ((adsr_bits & HR_NATN) == 0)
			break;
		udelay(1);
	}
	if (i == timeout)
		return -ETIMEDOUT;

	clear_bit(WRITE_READY_BN, &priv->state);

	return retval;
}
EXPORT_SYMBOL(nec7210_take_control);

int nec7210_go_to_standby(struct gpib_board *board, struct nec7210_priv *priv)
{
	int i;
	const int timeout = 1000;
	unsigned int adsr_bits = 0;
	int retval = 0;

	write_byte(priv, AUX_GTS, AUXMR);
	// busy wait until ATN is released
	for (i = 0; i < timeout; i++) {
		adsr_bits = read_byte(priv, ADSR);
		if (adsr_bits & HR_NATN)
			break;
		udelay(1);
	}
	// if busy wait has failed, try sleeping
	if (i == timeout) {
		for (i = 0; i < HZ; i++) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (schedule_timeout(1))
				return -ERESTARTSYS;
			adsr_bits = read_byte(priv, ADSR);
			if (adsr_bits & HR_NATN)
				break;
		}
		if (i == HZ)
			return -ETIMEDOUT;
	}

	clear_bit(COMMAND_READY_BN, &priv->state);
	return retval;
}
EXPORT_SYMBOL(nec7210_go_to_standby);

void nec7210_request_system_control(struct gpib_board *board, struct nec7210_priv *priv,
				    int request_control)
{
	if (request_control == 0) {
		write_byte(priv, AUX_CREN, AUXMR);
		write_byte(priv, AUX_CIFC, AUXMR);
		write_byte(priv, AUX_DSC, AUXMR);
	}
}
EXPORT_SYMBOL(nec7210_request_system_control);

void nec7210_interface_clear(struct gpib_board *board, struct nec7210_priv *priv, int assert)
{
	if (assert)
		write_byte(priv, AUX_SIFC, AUXMR);
	else
		write_byte(priv, AUX_CIFC, AUXMR);
}
EXPORT_SYMBOL(nec7210_interface_clear);

void nec7210_remote_enable(struct gpib_board *board, struct nec7210_priv *priv, int enable)
{
	if (enable)
		write_byte(priv, AUX_SREN, AUXMR);
	else
		write_byte(priv, AUX_CREN, AUXMR);
}
EXPORT_SYMBOL(nec7210_remote_enable);

void nec7210_release_rfd_holdoff(struct gpib_board *board, struct nec7210_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);
	if (test_bit(RFD_HOLDOFF_BN, &priv->state) &&
	    test_bit(READ_READY_BN, &priv->state) == 0) {
		write_byte(priv, AUX_FH, AUXMR);
		clear_bit(RFD_HOLDOFF_BN, &priv->state);
	}
	spin_unlock_irqrestore(&board->spinlock, flags);
}
EXPORT_SYMBOL(nec7210_release_rfd_holdoff);

int nec7210_t1_delay(struct gpib_board *board, struct nec7210_priv *priv,
		     unsigned int nano_sec)
{
	unsigned int retval;

	if (nano_sec <= 500) {
		priv->auxb_bits |= HR_TRI;
		retval = 500;
	} else {
		priv->auxb_bits &= ~HR_TRI;
		retval = 2000;
	}
	write_byte(priv, priv->auxb_bits, AUXMR);

	return retval;
}
EXPORT_SYMBOL(nec7210_t1_delay);

void nec7210_return_to_local(const struct gpib_board *board, struct nec7210_priv *priv)
{
	write_byte(priv, AUX_RTL, AUXMR);
}
EXPORT_SYMBOL(nec7210_return_to_local);

static inline short nec7210_atn_has_changed(struct gpib_board *board, struct nec7210_priv *priv)
{
	short address_status_bits = read_byte(priv, ADSR);

	if (address_status_bits & HR_NATN) {
		if (test_bit(ATN_NUM, &board->status))
			return 1;
		else
			return 0;
	} else	{
		if (test_bit(ATN_NUM, &board->status))
			return 0;
		else
			return 1;
	}
	return -1;
}

int nec7210_command(struct gpib_board *board, struct nec7210_priv *priv, uint8_t
		    *buffer, size_t length, size_t *bytes_written)
{
	int retval = 0;
	unsigned long flags;

	*bytes_written = 0;

	clear_bit(BUS_ERROR_BN, &priv->state);

	while (*bytes_written < length)	{
		if (wait_event_interruptible(board->wait,
					     test_bit(COMMAND_READY_BN, &priv->state) ||
					     test_bit(BUS_ERROR_BN, &priv->state) ||
					     test_bit(TIMO_NUM, &board->status))) {
			dev_dbg(board->gpib_dev, "command wait interrupted\n");
			retval = -ERESTARTSYS;
			break;
		}
		if (test_bit(TIMO_NUM, &board->status))
			break;
		if (test_and_clear_bit(BUS_ERROR_BN, &priv->state))
			break;
		spin_lock_irqsave(&board->spinlock, flags);
		clear_bit(COMMAND_READY_BN, &priv->state);
		write_byte(priv, buffer[*bytes_written], CDOR);
		spin_unlock_irqrestore(&board->spinlock, flags);

		++(*bytes_written);

		if (need_resched())
			schedule();
	}
	// wait for last byte to get sent
	if (wait_event_interruptible(board->wait, test_bit(COMMAND_READY_BN, &priv->state) ||
				     test_bit(BUS_ERROR_BN, &priv->state) ||
				     test_bit(TIMO_NUM, &board->status)))
		retval = -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;

	if (test_and_clear_bit(BUS_ERROR_BN, &priv->state))
		retval = -EIO;

	return retval;
}
EXPORT_SYMBOL(nec7210_command);

static int pio_read(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
		    size_t length, int *end, size_t *bytes_read)
{
	ssize_t retval = 0;

	*bytes_read = 0;
	*end = 0;

	while (*bytes_read < length) {
		if (wait_event_interruptible(board->wait,
					     test_bit(READ_READY_BN, &priv->state) ||
					     test_bit(DEV_CLEAR_BN, &priv->state) ||
					     test_bit(TIMO_NUM, &board->status))) {
			retval = -ERESTARTSYS;
			break;
		}
		if (test_bit(READ_READY_BN, &priv->state)) {
			if (*bytes_read == 0)	{
				/* We set the handshake mode here because we know
				 * no new bytes will arrive (it has already arrived
				 * and is awaiting being read out of the chip) while we are changing
				 * modes.  This ensures we can reliably keep track
				 * of the holdoff state.
				 */
				nec7210_set_handshake_mode(board, priv, HR_HLDA);
			}
			buffer[(*bytes_read)++] = nec7210_read_data_in(board, priv, end);
			if (*end)
				break;
		}
		if (test_bit(TIMO_NUM, &board->status)) {
			retval = -ETIMEDOUT;
			break;
		}
		if (test_bit(DEV_CLEAR_BN, &priv->state)) {
			retval = -EINTR;
			break;
		}

		if (*bytes_read < length)
			nec7210_release_rfd_holdoff(board, priv);

		if (need_resched())
			schedule();
	}
	return retval;
}

#ifdef NEC_DMA
static ssize_t __dma_read(struct gpib_board *board, struct nec7210_priv *priv, size_t length)
{
	ssize_t retval = 0;
	size_t count = 0;
	unsigned long flags, dma_irq_flags;

	if (length == 0)
		return 0;

	spin_lock_irqsave(&board->spinlock, flags);

	dma_irq_flags = claim_dma_lock();
	disable_dma(priv->dma_channel);
	/* program dma controller */
	clear_dma_ff(priv->dma_channel);
	set_dma_count(priv->dma_channel, length);
	set_dma_addr(priv->dma_channel, priv->dma_buffer_addr);
	set_dma_mode(priv->dma_channel, DMA_MODE_READ);
	release_dma_lock(dma_irq_flags);

	enable_dma(priv->dma_channel);

	set_bit(DMA_READ_IN_PROGRESS_BN, &priv->state);
	clear_bit(READ_READY_BN, &priv->state);

	// enable nec7210 dma
	nec7210_set_reg_bits(priv, IMR2, HR_DMAI, HR_DMAI);

	spin_unlock_irqrestore(&board->spinlock, flags);

	// wait for data to transfer
	if (wait_event_interruptible(board->wait,
				     test_bit(DMA_READ_IN_PROGRESS_BN, &priv->state) == 0 ||
				     test_bit(DEV_CLEAR_BN, &priv->state) ||
				     test_bit(TIMO_NUM, &board->status)))
		retval = -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_bit(DEV_CLEAR_BN, &priv->state))
		retval = -EINTR;

	// disable nec7210 dma
	nec7210_set_reg_bits(priv, IMR2, HR_DMAI, 0);

	// record how many bytes we transferred
	flags = claim_dma_lock();
	clear_dma_ff(priv->dma_channel);
	disable_dma(priv->dma_channel);
	count += length - get_dma_residue(priv->dma_channel);
	release_dma_lock(flags);

	return retval ? retval : count;
}

static ssize_t dma_read(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
			size_t length)
{
	size_t remain = length;
	size_t transfer_size;
	ssize_t retval = 0;

	while (remain > 0) {
		transfer_size = (priv->dma_buffer_length < remain) ?
			priv->dma_buffer_length : remain;
		retval = __dma_read(board, priv, transfer_size);
		if (retval < 0)
			break;
		memcpy(buffer, priv->dma_buffer, transfer_size);
		remain -= retval;
		buffer += retval;
		if (test_bit(RECEIVED_END_BN, &priv->state))
			break;
	}

	if (retval < 0)
		return retval;

	return length - remain;
}
#endif

int nec7210_read(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
		 size_t length, int *end, size_t *bytes_read)
{
	ssize_t retval = 0;

	*end = 0;
	*bytes_read = 0;

	if (length == 0)
		return 0;

	clear_bit(DEV_CLEAR_BN, &priv->state); // XXX wrong

	nec7210_release_rfd_holdoff(board, priv);

	retval = pio_read(board, priv, buffer, length, end, bytes_read);

	return retval;
}
EXPORT_SYMBOL(nec7210_read);

static int pio_write_wait(struct gpib_board *board, struct nec7210_priv *priv,
			  short wake_on_lacs, short wake_on_atn, short wake_on_bus_error)
{
	// wait until byte is ready to be sent
	if (wait_event_interruptible(board->wait,
				     (test_bit(TACS_NUM, &board->status) &&
				      test_bit(WRITE_READY_BN, &priv->state)) ||
				     test_bit(DEV_CLEAR_BN, &priv->state) ||
				     (wake_on_bus_error && test_bit(BUS_ERROR_BN, &priv->state)) ||
				     (wake_on_lacs && test_bit(LACS_NUM, &board->status)) ||
				     (wake_on_atn && test_bit(ATN_NUM, &board->status)) ||
				     test_bit(TIMO_NUM, &board->status)))
		return -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		return -ETIMEDOUT;

	if (test_bit(DEV_CLEAR_BN, &priv->state))
		return -EINTR;

	if (wake_on_bus_error && test_and_clear_bit(BUS_ERROR_BN, &priv->state))
		return -EIO;

	return 0;
}

static int pio_write(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
		     size_t length, size_t *bytes_written)
{
	size_t last_count = 0;
	ssize_t retval = 0;
	unsigned long flags;
	const int max_bus_errors = (length > 1000) ? length : 1000;
	int bus_error_count = 0;
	*bytes_written = 0;

	clear_bit(BUS_ERROR_BN, &priv->state);

	while (*bytes_written < length) {
		if (need_resched())
			schedule();

		retval = pio_write_wait(board, priv, 0, 0, priv->type == NEC7210);
		if (retval == -EIO) {
			/* resend last byte on bus error */
			*bytes_written = last_count;
			/* we can get unrecoverable bus errors,
			 * so give up after a while
			 */
			bus_error_count++;
			if (bus_error_count > max_bus_errors)
				return retval;
			continue;
		} else {
			if (retval < 0)
				return retval;
		}
		spin_lock_irqsave(&board->spinlock, flags);
		clear_bit(BUS_ERROR_BN, &priv->state);
		clear_bit(WRITE_READY_BN, &priv->state);
		last_count = *bytes_written;
		write_byte(priv, buffer[(*bytes_written)++], CDOR);
		spin_unlock_irqrestore(&board->spinlock, flags);
	}
	retval = pio_write_wait(board, priv, 1, 1, priv->type == NEC7210);
	return retval;
}

#ifdef NEC_DMA
static ssize_t __dma_write(struct gpib_board *board, struct nec7210_priv *priv, dma_addr_t address,
			   size_t length)
{
	unsigned long flags, dma_irq_flags;
	int residue = 0;
	int retval = 0;

	spin_lock_irqsave(&board->spinlock, flags);

	/* program dma controller */
	dma_irq_flags = claim_dma_lock();
	disable_dma(priv->dma_channel);
	clear_dma_ff(priv->dma_channel);
	set_dma_count(priv->dma_channel, length);
	set_dma_addr(priv->dma_channel, address);
	set_dma_mode(priv->dma_channel, DMA_MODE_WRITE);
	enable_dma(priv->dma_channel);
	release_dma_lock(dma_irq_flags);

	// enable board's dma for output
	nec7210_set_reg_bits(priv, IMR2, HR_DMAO, HR_DMAO);

	clear_bit(WRITE_READY_BN, &priv->state);
	set_bit(DMA_WRITE_IN_PROGRESS_BN, &priv->state);

	spin_unlock_irqrestore(&board->spinlock, flags);

	// suspend until message is sent
	if (wait_event_interruptible(board->wait,
				     test_bit(DMA_WRITE_IN_PROGRESS_BN, &priv->state) == 0 ||
				     test_bit(BUS_ERROR_BN, &priv->state) ||
				     test_bit(DEV_CLEAR_BN, &priv->state) ||
				     test_bit(TIMO_NUM, &board->status)))
		retval = -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;
	if (test_and_clear_bit(DEV_CLEAR_BN, &priv->state))
		retval = -EINTR;
	if (test_and_clear_bit(BUS_ERROR_BN, &priv->state))
		retval = -EIO;

	// disable board's dma
	nec7210_set_reg_bits(priv, IMR2, HR_DMAO, 0);

	dma_irq_flags = claim_dma_lock();
	clear_dma_ff(priv->dma_channel);
	disable_dma(priv->dma_channel);
	residue = get_dma_residue(priv->dma_channel);
	release_dma_lock(dma_irq_flags);

	if (residue)
		retval = -EPIPE;

	return retval ? retval : length;
}

static ssize_t dma_write(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
			 size_t length)
{
	size_t remain = length;
	size_t transfer_size;
	ssize_t retval = 0;

	while (remain > 0) {
		transfer_size = (priv->dma_buffer_length < remain) ?
			priv->dma_buffer_length : remain;
		memcpy(priv->dma_buffer, buffer, transfer_size);
		retval = __dma_write(board, priv, priv->dma_buffer_addr, transfer_size);
		if (retval < 0)
			break;
		remain -= retval;
		buffer += retval;
	}

	if (retval < 0)
		return retval;

	return length - remain;
}
#endif
int nec7210_write(struct gpib_board *board, struct nec7210_priv *priv,
		  uint8_t *buffer, size_t length, int send_eoi,
		  size_t *bytes_written)
{
	int retval = 0;

	*bytes_written = 0;

	clear_bit(DEV_CLEAR_BN, &priv->state); //XXX

	if (send_eoi)
		length-- ; /* save the last byte for sending EOI */

	if (length > 0)	{
		// isa dma transfer
		if (0 /*priv->dma_channel*/) {
/*
 * dma writes are unreliable since they can't recover from bus errors
 * (which happen when ATN is asserted in the middle of a write)
 */
#ifdef NEC_DMA
			retval = dma_write(board, priv, buffer, length);
			if (retval < 0)
				return retval;
			count += retval;
#endif
		} else {	// PIO transfer
			size_t num_bytes;

			retval = pio_write(board, priv, buffer, length, &num_bytes);

			*bytes_written += num_bytes;
			if (retval < 0)
				return retval;
		}
	}
	if (send_eoi) {
		size_t num_bytes;

		/* We need to wait to make sure we will immediately be able to write the data byte
		 * into the chip before sending the associated AUX_SEOI command.  This is really
		 * only needed for length==1 since otherwise the earlier calls to pio_write
		 * will have dont the wait already.
		 */
		retval = pio_write_wait(board, priv, 0, 0, priv->type == NEC7210);
		if (retval < 0)
			return retval;
		/*send EOI */
		write_byte(priv, AUX_SEOI, AUXMR);

		retval = pio_write(board, priv, &buffer[*bytes_written], 1, &num_bytes);
		*bytes_written += num_bytes;
		if (retval < 0)
			return retval;
	}

	return retval;
}
EXPORT_SYMBOL(nec7210_write);

/*
 *  interrupt service routine
 */
irqreturn_t nec7210_interrupt(struct gpib_board *board, struct nec7210_priv *priv)
{
	int status1, status2;

	// read interrupt status (also clears status)
	status1 = read_byte(priv, ISR1);
	status2 = read_byte(priv, ISR2);

	return nec7210_interrupt_have_status(board, priv, status1, status2);
}
EXPORT_SYMBOL(nec7210_interrupt);

irqreturn_t nec7210_interrupt_have_status(struct gpib_board *board,
					  struct nec7210_priv *priv, int status1, int status2)
{
#ifdef NEC_DMA
	unsigned long dma_flags;
#endif
	int retval = IRQ_NONE;

	// record service request in status
	if (status2 & HR_SRQI)
		set_bit(SRQI_NUM, &board->status);

	// change in lockout status
	if (status2 & HR_LOKC) {
		if (status2 & HR_LOK)
			set_bit(LOK_NUM, &board->status);
		else
			clear_bit(LOK_NUM, &board->status);
	}

	// change in remote status
	if (status2 & HR_REMC) {
		if (status2 & HR_REM)
			set_bit(REM_NUM, &board->status);
		else
			clear_bit(REM_NUM, &board->status);
	}

	// record reception of END
	if (status1 & HR_END) {
		set_bit(RECEIVED_END_BN, &priv->state);
		if ((priv->auxa_bits & HR_HANDSHAKE_MASK) == HR_HLDE)
			set_bit(RFD_HOLDOFF_BN, &priv->state);
	}

	// get incoming data in PIO mode
	if ((status1 & HR_DI)) {
		set_bit(READ_READY_BN, &priv->state);
		if ((priv->auxa_bits & HR_HANDSHAKE_MASK) == HR_HLDA)
			set_bit(RFD_HOLDOFF_BN, &priv->state);
	}
#ifdef NEC_DMA
	// check for dma read transfer complete
	if (test_bit(DMA_READ_IN_PROGRESS_BN, &priv->state)) {
		dma_flags = claim_dma_lock();
		disable_dma(priv->dma_channel);
		clear_dma_ff(priv->dma_channel);
		if ((status1 & HR_END) || get_dma_residue(priv->dma_channel) == 0)
			clear_bit(DMA_READ_IN_PROGRESS_BN, &priv->state);
		else
			enable_dma(priv->dma_channel);
		release_dma_lock(dma_flags);
	}
#endif
	if ((status1 & HR_DO)) {
		if (test_bit(DMA_WRITE_IN_PROGRESS_BN, &priv->state) == 0)
			set_bit(WRITE_READY_BN, &priv->state);
#ifdef NEC_DMA
		if (test_bit(DMA_WRITE_IN_PROGRESS_BN, &priv->state)) {	// write data, isa dma mode
			// check if dma transfer is complete
			dma_flags = claim_dma_lock();
			disable_dma(priv->dma_channel);
			clear_dma_ff(priv->dma_channel);
			if (get_dma_residue(priv->dma_channel) == 0) {
				clear_bit(DMA_WRITE_IN_PROGRESS_BN, &priv->state);
			// XXX race? byte may still be in CDOR reg
			} else {
				clear_bit(WRITE_READY_BN, &priv->state);
				enable_dma(priv->dma_channel);
			}
			release_dma_lock(dma_flags);
		}
#endif
	}

	// outgoing command can be sent
	if (status2 & HR_CO)
		set_bit(COMMAND_READY_BN, &priv->state);

	// command pass through received
	if (status1 & HR_CPT)
		write_byte(priv, AUX_NVAL, AUXMR);

	if (status1 & HR_ERR)
		set_bit(BUS_ERROR_BN, &priv->state);

	if (status1 & HR_DEC) {
		unsigned short address_status_bits = read_byte(priv, ADSR);

		// ignore device clear events if we are controller in charge
		if ((address_status_bits & HR_CIC) == 0) {
			push_gpib_event(board, EventDevClr);
			set_bit(DEV_CLEAR_BN, &priv->state);
		}
	}

	if (status1 & HR_DET)
		push_gpib_event(board, EventDevTrg);

	// Addressing status has changed
	if (status2 & HR_ADSC)
		set_bit(ADR_CHANGE_BN, &priv->state);

	if ((status1 & priv->reg_bits[IMR1]) ||
	    (status2 & (priv->reg_bits[IMR2] & IMR2_ENABLE_INTR_MASK)) ||
	    nec7210_atn_has_changed(board, priv))	{
		nec7210_update_status_nolock(board, priv);
		dev_dbg(board->gpib_dev, "minor %i, stat %lx, isr1 0x%x, imr1 0x%x, isr2 0x%x, imr2 0x%x\n",
			board->minor, board->status, status1, priv->reg_bits[IMR1], status2,
			     priv->reg_bits[IMR2]);
		wake_up_interruptible(&board->wait); /* wake up sleeping process */
		retval = IRQ_HANDLED;
	}

	return retval;
}
EXPORT_SYMBOL(nec7210_interrupt_have_status);

void nec7210_board_reset(struct nec7210_priv *priv, const struct gpib_board *board)
{
	/* 7210 chip reset */
	write_byte(priv, AUX_CR, AUXMR);

	/* disable all interrupts */
	priv->reg_bits[IMR1] = 0;
	write_byte(priv, priv->reg_bits[IMR1], IMR1);
	priv->reg_bits[IMR2] = 0;
	write_byte(priv, priv->reg_bits[IMR2], IMR2);
	write_byte(priv, 0, SPMR);

	/* clear registers by reading */
	read_byte(priv, CPTR);
	read_byte(priv, ISR1);
	read_byte(priv, ISR2);

	/* parallel poll unconfigure */
	write_byte(priv, PPR | HR_PPU, AUXMR);

	priv->reg_bits[ADMR] = HR_TRM0 | HR_TRM1;

	priv->auxa_bits = AUXRA | HR_HLDA;
	write_byte(priv, priv->auxa_bits, AUXMR);

	write_byte(priv, AUXRE | 0, AUXMR);

	/* set INT pin to active high, enable command pass through of unknown commands */
	priv->auxb_bits = AUXRB | HR_CPTE;
	write_byte(priv, priv->auxb_bits, AUXMR);
	write_byte(priv, AUXRE, AUXMR);
}
EXPORT_SYMBOL(nec7210_board_reset);

void nec7210_board_online(struct nec7210_priv *priv, const struct gpib_board *board)
{
	/* set GPIB address */
	nec7210_primary_address(board, priv, board->pad);
	nec7210_secondary_address(board, priv, board->sad, board->sad >= 0);

	// enable interrupts
	priv->reg_bits[IMR1] = HR_ERRIE | HR_DECIE | HR_ENDIE |
		HR_DETIE | HR_CPTIE | HR_DOIE | HR_DIIE;
	priv->reg_bits[IMR2] = IMR2_ENABLE_INTR_MASK;
	write_byte(priv, priv->reg_bits[IMR1], IMR1);
	write_byte(priv, priv->reg_bits[IMR2], IMR2);

	write_byte(priv, AUX_PON, AUXMR);
}
EXPORT_SYMBOL(nec7210_board_online);

#ifdef CONFIG_HAS_IOPORT
/* wrappers for io */
uint8_t nec7210_ioport_read_byte(struct nec7210_priv *priv, unsigned int register_num)
{
	return inb(priv->iobase + register_num * priv->offset);
}
EXPORT_SYMBOL(nec7210_ioport_read_byte);

void nec7210_ioport_write_byte(struct nec7210_priv *priv, uint8_t data, unsigned int register_num)
{
	if (register_num == AUXMR)
		/* locking makes absolutely sure noone accesses the
		 * AUXMR register faster than once per microsecond
		 */
		nec7210_locking_ioport_write_byte(priv, data, register_num);
	else
		outb(data, priv->iobase + register_num * priv->offset);
}
EXPORT_SYMBOL(nec7210_ioport_write_byte);

/* locking variants of io wrappers, for chips that page-in registers */
uint8_t nec7210_locking_ioport_read_byte(struct nec7210_priv *priv, unsigned int register_num)
{
	u8 retval;
	unsigned long flags;

	spin_lock_irqsave(&priv->register_page_lock, flags);
	retval = inb(priv->iobase + register_num * priv->offset);
	spin_unlock_irqrestore(&priv->register_page_lock, flags);
	return retval;
}
EXPORT_SYMBOL(nec7210_locking_ioport_read_byte);

void nec7210_locking_ioport_write_byte(struct nec7210_priv *priv, uint8_t data,
				       unsigned int register_num)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->register_page_lock, flags);
	if (register_num == AUXMR)
		udelay(1);
	outb(data, priv->iobase + register_num * priv->offset);
	spin_unlock_irqrestore(&priv->register_page_lock, flags);
}
EXPORT_SYMBOL(nec7210_locking_ioport_write_byte);
#endif

uint8_t nec7210_iomem_read_byte(struct nec7210_priv *priv, unsigned int register_num)
{
	return readb(priv->mmiobase + register_num * priv->offset);
}
EXPORT_SYMBOL(nec7210_iomem_read_byte);

void nec7210_iomem_write_byte(struct nec7210_priv *priv, uint8_t data, unsigned int register_num)
{
	if (register_num == AUXMR)
		/* locking makes absolutely sure noone accesses the
		 * AUXMR register faster than once per microsecond
		 */
		nec7210_locking_iomem_write_byte(priv, data, register_num);
	else
		writeb(data, priv->mmiobase + register_num * priv->offset);
}
EXPORT_SYMBOL(nec7210_iomem_write_byte);

uint8_t nec7210_locking_iomem_read_byte(struct nec7210_priv *priv, unsigned int register_num)
{
	u8 retval;
	unsigned long flags;

	spin_lock_irqsave(&priv->register_page_lock, flags);
	retval = readb(priv->mmiobase + register_num * priv->offset);
	spin_unlock_irqrestore(&priv->register_page_lock, flags);
	return retval;
}
EXPORT_SYMBOL(nec7210_locking_iomem_read_byte);

void nec7210_locking_iomem_write_byte(struct nec7210_priv *priv, uint8_t data,
				      unsigned int register_num)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->register_page_lock, flags);
	if (register_num == AUXMR)
		udelay(1);
	writeb(data, priv->mmiobase + register_num * priv->offset);
	spin_unlock_irqrestore(&priv->register_page_lock, flags);
}
EXPORT_SYMBOL(nec7210_locking_iomem_write_byte);

static int __init nec7210_init_module(void)
{
	return 0;
}

static void __exit nec7210_exit_module(void)
{
}

module_init(nec7210_init_module);
module_exit(nec7210_exit_module);
