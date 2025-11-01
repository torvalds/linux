// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *   copyright		  : (C) 2001, 2002 by Frank Mori Hess
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt

#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/dma.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

#include "gpibP.h"
#include "tms9914.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB library for tms9914");

static unsigned int update_status_nolock(struct gpib_board *board, struct tms9914_priv *priv);

int tms9914_take_control(struct gpib_board *board, struct tms9914_priv *priv, int synchronous)
{
	int i;
	const int timeout = 100;

	if (synchronous)
		write_byte(priv, AUX_TCS, AUXCR);
	else
		write_byte(priv, AUX_TCA, AUXCR);
	// busy wait until ATN is asserted
	for (i = 0; i < timeout; i++) {
		if ((read_byte(priv, ADSR) & HR_ATN))
			break;
		udelay(1);
	}
	if (i == timeout)
		return -ETIMEDOUT;

	clear_bit(WRITE_READY_BN, &priv->state);

	return 0;
}
EXPORT_SYMBOL_GPL(tms9914_take_control);

/*
 * The agilent 82350B has a buggy implementation of tcs which interferes with the
 * operation of tca.  It appears to be based on the controller state machine
 * described in the TI 9900 TMS9914A data manual published in 1982.  This
 * manual describes tcs as putting the controller into a CWAS
 * state where it waits indefinitely for ANRS and ignores tca.	Since a
 * functioning tca is far more important than tcs, we work around the
 * problem by never issuing tcs.
 *
 * I don't know if this problem exists in the real tms9914a or just in the fpga
 * of the 82350B.  For now, only the agilent_82350b uses this workaround.
 * The rest of the tms9914 based drivers still use tms9914_take_control
 * directly (which does issue tcs).
 */
int tms9914_take_control_workaround(struct gpib_board *board,
				    struct tms9914_priv *priv, int synchronous)
{
	if (synchronous)
		return -ETIMEDOUT;
	return tms9914_take_control(board, priv, synchronous);
}
EXPORT_SYMBOL_GPL(tms9914_take_control_workaround);

int tms9914_go_to_standby(struct gpib_board *board, struct tms9914_priv *priv)
{
	int i;
	const int timeout = 1000;

	write_byte(priv, AUX_GTS, AUXCR);
	// busy wait until ATN is released
	for (i = 0; i < timeout; i++) {
		if ((read_byte(priv, ADSR) & HR_ATN) == 0)
			break;
		udelay(1);
	}
	if (i == timeout)
		return -ETIMEDOUT;

	clear_bit(COMMAND_READY_BN, &priv->state);

	return 0;
}
EXPORT_SYMBOL_GPL(tms9914_go_to_standby);

void tms9914_interface_clear(struct gpib_board *board, struct tms9914_priv *priv, int assert)
{
	if (assert) {
		write_byte(priv, AUX_SIC | AUX_CS, AUXCR);

		set_bit(CIC_NUM, &board->status);
	} else {
		write_byte(priv, AUX_SIC, AUXCR);
	}
}
EXPORT_SYMBOL_GPL(tms9914_interface_clear);

void tms9914_remote_enable(struct gpib_board *board, struct tms9914_priv *priv, int enable)
{
	if (enable)
		write_byte(priv, AUX_SRE | AUX_CS, AUXCR);
	else
		write_byte(priv, AUX_SRE, AUXCR);
}
EXPORT_SYMBOL_GPL(tms9914_remote_enable);

int tms9914_request_system_control(struct gpib_board *board, struct tms9914_priv *priv,
				   int request_control)
{
	if (request_control) {
		write_byte(priv, AUX_RQC, AUXCR);
	} else {
		clear_bit(CIC_NUM, &board->status);
		write_byte(priv, AUX_RLC, AUXCR);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(tms9914_request_system_control);

unsigned int tms9914_t1_delay(struct gpib_board *board, struct tms9914_priv *priv,
			      unsigned int nano_sec)
{
	static const int clock_period = 200;	// assuming 5Mhz input clock
	int num_cycles;

	num_cycles = 12;

	if (nano_sec <= 8 * clock_period) {
		write_byte(priv, AUX_STDL | AUX_CS, AUXCR);
		num_cycles = 8;
	} else {
		write_byte(priv, AUX_STDL, AUXCR);
	}

	if (nano_sec <= 4 * clock_period) {
		write_byte(priv, AUX_VSTDL | AUX_CS, AUXCR);
		num_cycles = 4;
	} else {
		write_byte(priv, AUX_VSTDL, AUXCR);
	}

	return num_cycles * clock_period;
}
EXPORT_SYMBOL_GPL(tms9914_t1_delay);

void tms9914_return_to_local(const struct gpib_board *board, struct tms9914_priv *priv)
{
	write_byte(priv, AUX_RTL, AUXCR);
}
EXPORT_SYMBOL_GPL(tms9914_return_to_local);

void tms9914_set_holdoff_mode(struct tms9914_priv *priv, enum tms9914_holdoff_mode mode)
{
	switch (mode) {
	case TMS9914_HOLDOFF_NONE:
		write_byte(priv, AUX_HLDE, AUXCR);
		write_byte(priv, AUX_HLDA, AUXCR);
		break;
	case TMS9914_HOLDOFF_EOI:
		write_byte(priv, AUX_HLDE | AUX_CS, AUXCR);
		write_byte(priv, AUX_HLDA, AUXCR);
		break;
	case TMS9914_HOLDOFF_ALL:
		write_byte(priv, AUX_HLDE, AUXCR);
		write_byte(priv, AUX_HLDA | AUX_CS, AUXCR);
		break;
	default:
		pr_err("bug! bad holdoff mode %i\n", mode);
		break;
	}
	priv->holdoff_mode = mode;
}
EXPORT_SYMBOL_GPL(tms9914_set_holdoff_mode);

void tms9914_release_holdoff(struct tms9914_priv *priv)
{
	if (priv->holdoff_active) {
		write_byte(priv, AUX_RHDF, AUXCR);
		priv->holdoff_active = 0;
	}
}
EXPORT_SYMBOL_GPL(tms9914_release_holdoff);

int tms9914_enable_eos(struct gpib_board *board, struct tms9914_priv *priv, u8 eos_byte,
		       int compare_8_bits)
{
	priv->eos = eos_byte;
	priv->eos_flags = REOS;
	if (compare_8_bits)
		priv->eos_flags |= BIN;
	return 0;
}
EXPORT_SYMBOL(tms9914_enable_eos);

void tms9914_disable_eos(struct gpib_board *board, struct tms9914_priv *priv)
{
	priv->eos_flags &= ~REOS;
}
EXPORT_SYMBOL(tms9914_disable_eos);

int tms9914_parallel_poll(struct gpib_board *board, struct tms9914_priv *priv, u8 *result)
{
	// execute parallel poll
	write_byte(priv, AUX_CS | AUX_RPP, AUXCR);
	udelay(2);
	*result = read_byte(priv, CPTR);
	// clear parallel poll state
	write_byte(priv, AUX_RPP, AUXCR);
	return 0;
}
EXPORT_SYMBOL(tms9914_parallel_poll);

static void set_ppoll_reg(struct tms9914_priv *priv, int enable,
			  unsigned int dio_line, int sense, int ist)
{
	u8 dio_byte;

	if (enable && ((sense && ist) || (!sense && !ist))) {
		dio_byte = 1 << (dio_line - 1);
		write_byte(priv, dio_byte, PPR);
	} else {
		write_byte(priv, 0, PPR);
	}
}

void tms9914_parallel_poll_configure(struct gpib_board *board,
				     struct tms9914_priv *priv, u8 config)
{
	priv->ppoll_enable = (config & PPC_DISABLE) == 0;
	priv->ppoll_line = (config & PPC_DIO_MASK) + 1;
	priv->ppoll_sense = (config & PPC_SENSE) != 0;
	set_ppoll_reg(priv, priv->ppoll_enable, priv->ppoll_line, priv->ppoll_sense, board->ist);
}
EXPORT_SYMBOL(tms9914_parallel_poll_configure);

void tms9914_parallel_poll_response(struct gpib_board *board,
				    struct tms9914_priv *priv, int ist)
{
	set_ppoll_reg(priv, priv->ppoll_enable, priv->ppoll_line, priv->ppoll_sense, ist);
}
EXPORT_SYMBOL(tms9914_parallel_poll_response);

void tms9914_serial_poll_response(struct gpib_board *board,
				  struct tms9914_priv *priv, u8 status)
{
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);
	write_byte(priv, status, SPMR);
	priv->spoll_status = status;
	if (status & request_service_bit)
		write_byte(priv, AUX_RSV2 | AUX_CS, AUXCR);
	else
		write_byte(priv, AUX_RSV2, AUXCR);
	spin_unlock_irqrestore(&board->spinlock, flags);
}
EXPORT_SYMBOL(tms9914_serial_poll_response);

u8 tms9914_serial_poll_status(struct gpib_board *board, struct tms9914_priv *priv)
{
	u8 status;
	unsigned long flags;

	spin_lock_irqsave(&board->spinlock, flags);
	status = priv->spoll_status;
	spin_unlock_irqrestore(&board->spinlock, flags);

	return status;
}
EXPORT_SYMBOL(tms9914_serial_poll_status);

int tms9914_primary_address(struct gpib_board *board,
			    struct tms9914_priv *priv, unsigned int address)
{
	// put primary address in address0
	write_byte(priv, address & ADDRESS_MASK, ADR);
	return 0;
}
EXPORT_SYMBOL(tms9914_primary_address);

int tms9914_secondary_address(struct gpib_board *board, struct tms9914_priv *priv,
			      unsigned int address, int enable)
{
	if (enable)
		priv->imr1_bits |= HR_APTIE;
	else
		priv->imr1_bits &= ~HR_APTIE;

	write_byte(priv, priv->imr1_bits, IMR1);
	return 0;
}
EXPORT_SYMBOL(tms9914_secondary_address);

unsigned int tms9914_update_status(struct gpib_board *board, struct tms9914_priv *priv,
				   unsigned int clear_mask)
{
	unsigned long flags;
	unsigned int retval;

	spin_lock_irqsave(&board->spinlock, flags);
	retval = update_status_nolock(board, priv);
	board->status &= ~clear_mask;
	spin_unlock_irqrestore(&board->spinlock, flags);

	return retval;
}
EXPORT_SYMBOL(tms9914_update_status);

static void update_talker_state(struct tms9914_priv *priv, unsigned int address_status_bits)
{
	if (address_status_bits & HR_TA)	{
		if (address_status_bits & HR_ATN)
			priv->talker_state = talker_addressed;
		else
			/*
			 * this could also be serial_poll_active, but the tms9914 provides no
			 * way to distinguish, so we'll assume talker_active
			 */
			priv->talker_state = talker_active;
	} else {
		priv->talker_state = talker_idle;
	}
}

static void update_listener_state(struct tms9914_priv *priv, unsigned int address_status_bits)
{
	if (address_status_bits & HR_LA)	{
		if (address_status_bits & HR_ATN)
			priv->listener_state = listener_addressed;
		else
			priv->listener_state = listener_active;
	} else {
		priv->listener_state = listener_idle;
	}
}

static unsigned int update_status_nolock(struct gpib_board *board, struct tms9914_priv *priv)
{
	int address_status;
	int bsr_bits;

	address_status = read_byte(priv, ADSR);

	// check for remote/local
	if (address_status & HR_REM)
		set_bit(REM_NUM, &board->status);
	else
		clear_bit(REM_NUM, &board->status);
	// check for lockout
	if (address_status & HR_LLO)
		set_bit(LOK_NUM, &board->status);
	else
		clear_bit(LOK_NUM, &board->status);
	// check for ATN
	if (address_status & HR_ATN)
		set_bit(ATN_NUM, &board->status);
	else
		clear_bit(ATN_NUM, &board->status);
	// check for talker/listener addressed
	update_talker_state(priv, address_status);
	if (priv->talker_state == talker_active || priv->talker_state == talker_addressed)
		set_bit(TACS_NUM, &board->status);
	else
		clear_bit(TACS_NUM, &board->status);

	update_listener_state(priv, address_status);
	if (priv->listener_state == listener_active || priv->listener_state == listener_addressed)
		set_bit(LACS_NUM, &board->status);
	else
		clear_bit(LACS_NUM, &board->status);
	// Check for SRQI - not reset elsewhere except in autospoll
	if (board->status & SRQI) {
		bsr_bits = read_byte(priv, BSR);
		if (!(bsr_bits & BSR_SRQ_BIT))
			clear_bit(SRQI_NUM, &board->status);
	}

	dev_dbg(board->gpib_dev, "status 0x%lx, state 0x%lx\n", board->status, priv->state);

	return board->status;
}

int tms9914_line_status(const struct gpib_board *board, struct tms9914_priv *priv)
{
	int bsr_bits;
	int status = VALID_ALL;

	bsr_bits = read_byte(priv, BSR);

	if (bsr_bits & BSR_REN_BIT)
		status |= BUS_REN;
	if (bsr_bits & BSR_IFC_BIT)
		status |= BUS_IFC;
	if (bsr_bits & BSR_SRQ_BIT)
		status |= BUS_SRQ;
	if (bsr_bits & BSR_EOI_BIT)
		status |= BUS_EOI;
	if (bsr_bits & BSR_NRFD_BIT)
		status |= BUS_NRFD;
	if (bsr_bits & BSR_NDAC_BIT)
		status |= BUS_NDAC;
	if (bsr_bits & BSR_DAV_BIT)
		status |= BUS_DAV;
	if (bsr_bits & BSR_ATN_BIT)
		status |= BUS_ATN;

	return status;
}
EXPORT_SYMBOL(tms9914_line_status);

static int check_for_eos(struct tms9914_priv *priv, u8 byte)
{
	static const u8 seven_bit_compare_mask = 0x7f;

	if ((priv->eos_flags & REOS) == 0)
		return 0;

	if (priv->eos_flags & BIN) {
		if (priv->eos == byte)
			return 1;
	} else	{
		if ((priv->eos & seven_bit_compare_mask) == (byte & seven_bit_compare_mask))
			return 1;
	}
	return 0;
}

static int wait_for_read_byte(struct gpib_board *board, struct tms9914_priv *priv)
{
	if (wait_event_interruptible(board->wait,
				     test_bit(READ_READY_BN, &priv->state) ||
				     test_bit(DEV_CLEAR_BN, &priv->state) ||
				     test_bit(TIMO_NUM, &board->status)))
		return -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		return -ETIMEDOUT;

	if (test_bit(DEV_CLEAR_BN, &priv->state))
		return -EINTR;
	return 0;
}

static inline u8 tms9914_read_data_in(struct gpib_board *board,
				      struct tms9914_priv *priv, int *end)
{
	unsigned long flags;
	u8 data;

	spin_lock_irqsave(&board->spinlock, flags);
	clear_bit(READ_READY_BN, &priv->state);
	data = read_byte(priv, DIR);
	if (test_and_clear_bit(RECEIVED_END_BN, &priv->state))
		*end = 1;
	else
		*end = 0;
	switch (priv->holdoff_mode) {
	case TMS9914_HOLDOFF_EOI:
		if (*end)
			priv->holdoff_active = 1;
		break;
	case TMS9914_HOLDOFF_ALL:
		priv->holdoff_active = 1;
		break;
	case TMS9914_HOLDOFF_NONE:
		break;
	default:
		dev_err(board->gpib_dev, "bug! bad holdoff mode %i\n", priv->holdoff_mode);
		break;
	}
	spin_unlock_irqrestore(&board->spinlock, flags);

	return data;
}

static int pio_read(struct gpib_board *board, struct tms9914_priv *priv, u8 *buffer,
		    size_t length, int *end, size_t *bytes_read)
{
	ssize_t retval = 0;

	*bytes_read = 0;
	*end = 0;
	while (*bytes_read < length && *end == 0) {
		tms9914_release_holdoff(priv);
		retval = wait_for_read_byte(board, priv);
		if (retval < 0)
			return retval;
		buffer[(*bytes_read)++] = tms9914_read_data_in(board, priv, end);

		if (check_for_eos(priv, buffer[*bytes_read - 1]))
			*end = 1;
	}

	return retval;
}

int tms9914_read(struct gpib_board *board, struct tms9914_priv *priv, u8 *buffer,
		 size_t length, int *end, size_t *bytes_read)
{
	ssize_t retval = 0;
	size_t num_bytes;

	*end = 0;
	*bytes_read = 0;
	if (length == 0)
		return 0;

	clear_bit(DEV_CLEAR_BN, &priv->state);

	// transfer data (except for last byte)
	if (length > 1)	{
		if (priv->eos_flags & REOS)
			tms9914_set_holdoff_mode(priv, TMS9914_HOLDOFF_ALL);
		else
			tms9914_set_holdoff_mode(priv, TMS9914_HOLDOFF_EOI);
		// PIO transfer
		retval = pio_read(board, priv, buffer, length - 1, end, &num_bytes);
		*bytes_read += num_bytes;
		if (retval < 0)
			return retval;
		buffer += num_bytes;
		length -= num_bytes;
	}
	// read last bytes if we havn't received an END yet
	if (*end == 0) {
		// make sure we holdoff after last byte read
		tms9914_set_holdoff_mode(priv, TMS9914_HOLDOFF_ALL);
		retval = pio_read(board, priv, buffer, length, end, &num_bytes);
		*bytes_read += num_bytes;
		if (retval < 0)
			return retval;
	}
	return 0;
}
EXPORT_SYMBOL(tms9914_read);

static int pio_write_wait(struct gpib_board *board, struct tms9914_priv *priv)
{
	// wait until next byte is ready to be sent
	if (wait_event_interruptible(board->wait,
				     test_bit(WRITE_READY_BN, &priv->state) ||
				     test_bit(BUS_ERROR_BN, &priv->state) ||
				     test_bit(DEV_CLEAR_BN, &priv->state) ||
				     test_bit(TIMO_NUM, &board->status)))
		return -ERESTARTSYS;

	if (test_bit(TIMO_NUM, &board->status))
		return -ETIMEDOUT;
	if (test_bit(BUS_ERROR_BN, &priv->state))
		return -EIO;
	if (test_bit(DEV_CLEAR_BN, &priv->state))
		return -EINTR;

	return 0;
}

static int pio_write(struct gpib_board *board, struct tms9914_priv *priv, u8 *buffer,
		     size_t length, size_t *bytes_written)
{
	ssize_t retval = 0;
	unsigned long flags;

	*bytes_written = 0;
	while (*bytes_written < length) {
		retval = pio_write_wait(board, priv);
		if (retval < 0)
			break;

		spin_lock_irqsave(&board->spinlock, flags);
		clear_bit(WRITE_READY_BN, &priv->state);
		write_byte(priv, buffer[(*bytes_written)++], CDOR);
		spin_unlock_irqrestore(&board->spinlock, flags);
	}
	retval = pio_write_wait(board, priv);
	if (retval < 0)
		return retval;

	return length;
}

int tms9914_write(struct gpib_board *board, struct tms9914_priv *priv,
		  u8 *buffer, size_t length, int send_eoi, size_t *bytes_written)
{
	ssize_t retval = 0;

	*bytes_written = 0;
	if (length == 0)
		return 0;

	clear_bit(BUS_ERROR_BN, &priv->state);
	clear_bit(DEV_CLEAR_BN, &priv->state);

	if (send_eoi)
		length-- ; /* save the last byte for sending EOI */

	if (length > 0)	{
		size_t num_bytes;
		// PIO transfer
		retval = pio_write(board, priv, buffer, length, &num_bytes);
		*bytes_written += num_bytes;
		if (retval < 0)
			return retval;
	}
	if (send_eoi) {
		size_t num_bytes;
		/*send EOI */
		write_byte(priv, AUX_SEOI, AUXCR);

		retval = pio_write(board, priv, &buffer[*bytes_written], 1, &num_bytes);
		*bytes_written += num_bytes;
	}
	return retval;
}
EXPORT_SYMBOL(tms9914_write);

static void check_my_address_state(struct gpib_board *board,
				   struct tms9914_priv *priv, int cmd_byte)
{
	if (cmd_byte == MLA(board->pad)) {
		priv->primary_listen_addressed = 1;
		// become active listener
		if (board->sad < 0)
			write_byte(priv, AUX_LON | AUX_CS, AUXCR);
	} else if (board->sad >= 0 && priv->primary_listen_addressed &&
		  cmd_byte == MSA(board->sad)) {
		// become active listener
		write_byte(priv, AUX_LON | AUX_CS, AUXCR);
	} else if (cmd_byte != MLA(board->pad) && (cmd_byte & 0xe0) == LAD) {
		priv->primary_listen_addressed = 0;
	} else if (cmd_byte == UNL) {
		priv->primary_listen_addressed = 0;
		write_byte(priv, AUX_LON, AUXCR);
	} else if (cmd_byte == MTA(board->pad))	{
		priv->primary_talk_addressed = 1;
		if (board->sad < 0)
			// make active talker
			write_byte(priv, AUX_TON | AUX_CS, AUXCR);
	} else if (board->sad >= 0 && priv->primary_talk_addressed &&
		   cmd_byte == MSA(board->sad)) {
		// become active talker
		write_byte(priv, AUX_TON | AUX_CS, AUXCR);
	} else if (cmd_byte != MTA(board->pad) && (cmd_byte & 0xe0) == TAD) {
		// Other Talk Address
		priv->primary_talk_addressed = 0;
		write_byte(priv, AUX_TON, AUXCR);
	} else if (cmd_byte == UNT) {
		priv->primary_talk_addressed = 0;
		write_byte(priv, AUX_TON, AUXCR);
	}
}

int tms9914_command(struct gpib_board *board, struct tms9914_priv *priv,  u8 *buffer,
		    size_t length, size_t *bytes_written)
{
	int retval = 0;
	unsigned long flags;

	*bytes_written = 0;
	while (*bytes_written < length) {
		if (wait_event_interruptible(board->wait,
					     test_bit(COMMAND_READY_BN,
						      &priv->state) ||
					     test_bit(TIMO_NUM, &board->status)))
			break;
		if (test_bit(TIMO_NUM, &board->status))
			break;

		spin_lock_irqsave(&board->spinlock, flags);
		clear_bit(COMMAND_READY_BN, &priv->state);
		write_byte(priv, buffer[*bytes_written], CDOR);
		spin_unlock_irqrestore(&board->spinlock, flags);

		check_my_address_state(board, priv, buffer[*bytes_written]);

		++(*bytes_written);
	}
	// wait until last command byte is written
	if (wait_event_interruptible(board->wait,
				     test_bit(COMMAND_READY_BN,
					      &priv->state) || test_bit(TIMO_NUM, &board->status)))
		retval = -ERESTARTSYS;
	if (test_bit(TIMO_NUM, &board->status))
		retval = -ETIMEDOUT;

	return retval;
}
EXPORT_SYMBOL(tms9914_command);

irqreturn_t tms9914_interrupt(struct gpib_board *board, struct tms9914_priv *priv)
{
	int status0, status1;

	// read interrupt status (also clears status)
	status0 = read_byte(priv, ISR0);
	status1 = read_byte(priv, ISR1);
	return tms9914_interrupt_have_status(board, priv, status0, status1);
}
EXPORT_SYMBOL(tms9914_interrupt);

irqreturn_t tms9914_interrupt_have_status(struct gpib_board *board, struct tms9914_priv *priv,
					  int status0, int status1)
{
	// record reception of END
	if (status0 & HR_END)
		set_bit(RECEIVED_END_BN, &priv->state);
	// get incoming data in PIO mode
	if ((status0 & HR_BI))
		set_bit(READ_READY_BN, &priv->state);
	if ((status0 & HR_BO))	{
		if (read_byte(priv, ADSR) & HR_ATN)
			set_bit(COMMAND_READY_BN, &priv->state);
		else
			set_bit(WRITE_READY_BN, &priv->state);
	}

	if (status0 & HR_SPAS) {
		priv->spoll_status &= ~request_service_bit;
		write_byte(priv, priv->spoll_status, SPMR);
		// FIXME: set SPOLL status bit
	}
	// record service request in status
	if (status1 & HR_SRQ)
		set_bit(SRQI_NUM, &board->status);
	// have been addressed (with secondary addressing disabled)
	if (status1 & HR_MA)
		// clear dac holdoff
		write_byte(priv, AUX_VAL, AUXCR);
	// unrecognized command received
	if (status1 & HR_UNC) {
		unsigned short command_byte = read_byte(priv, CPTR) & gpib_command_mask;

		switch (command_byte) {
		case PP_CONFIG:
			priv->ppoll_configure_state = 1;
			/*
			 * AUX_PTS generates another UNC interrupt on the next command byte
			 * if it is in the secondary address group (such as PPE and PPD).
			 */
			write_byte(priv, AUX_PTS, AUXCR);
			write_byte(priv, AUX_VAL, AUXCR);
			break;
		case PPU:
			tms9914_parallel_poll_configure(board, priv, command_byte);
			write_byte(priv, AUX_VAL, AUXCR);
			break;
		default:
			if (is_PPE(command_byte) || is_PPD(command_byte)) {
				if (priv->ppoll_configure_state) {
					tms9914_parallel_poll_configure(board, priv, command_byte);
					write_byte(priv, AUX_VAL, AUXCR);
				} else	{// bad parallel poll configure byte
					// clear dac holdoff
					write_byte(priv, AUX_INVAL, AUXCR);
				}
			} else	{
				// clear dac holdoff
				write_byte(priv, AUX_INVAL, AUXCR);
			}
			break;
		}

		if (in_primary_command_group(command_byte) && command_byte != PP_CONFIG)
			priv->ppoll_configure_state = 0;
	}

	if (status1 & HR_ERR) {
		dev_dbg(board->gpib_dev, "gpib bus error\n");
		set_bit(BUS_ERROR_BN, &priv->state);
	}

	if (status1 & HR_IFC) {
		push_gpib_event(board, EVENT_IFC);
		clear_bit(CIC_NUM, &board->status);
	}

	if (status1 & HR_GET) {
		push_gpib_event(board, EVENT_DEV_TRG);
		// clear dac holdoff
		write_byte(priv, AUX_VAL, AUXCR);
	}

	if (status1 & HR_DCAS) {
		push_gpib_event(board, EVENT_DEV_CLR);
		// clear dac holdoff
		write_byte(priv, AUX_VAL, AUXCR);
		set_bit(DEV_CLEAR_BN, &priv->state);
	}

	// check for being addressed with secondary addressing
	if (status1 & HR_APT) {
		if (board->sad < 0)
			dev_err(board->gpib_dev, "bug, APT interrupt without secondary addressing?\n");
		if ((read_byte(priv, CPTR) & gpib_command_mask) == MSA(board->sad))
			write_byte(priv, AUX_VAL, AUXCR);
		else
			write_byte(priv, AUX_INVAL, AUXCR);
	}

	if ((status0 & priv->imr0_bits) || (status1 & priv->imr1_bits))	{
		dev_dbg(board->gpib_dev, "isr0 0x%x, imr0 0x%x, isr1 0x%x, imr1 0x%x\n",
			status0, priv->imr0_bits, status1, priv->imr1_bits);
		update_status_nolock(board, priv);
		wake_up_interruptible(&board->wait);
	}
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(tms9914_interrupt_have_status);

void tms9914_board_reset(struct tms9914_priv *priv)
{
	/* chip reset */
	write_byte(priv, AUX_CHIP_RESET | AUX_CS, AUXCR);

	/* disable all interrupts */
	priv->imr0_bits = 0;
	write_byte(priv, priv->imr0_bits, IMR0);
	priv->imr1_bits = 0;
	write_byte(priv, priv->imr1_bits, IMR1);
	write_byte(priv, AUX_DAI | AUX_CS, AUXCR);

	/* clear registers by reading */
	read_byte(priv, CPTR);
	read_byte(priv, ISR0);
	read_byte(priv, ISR1);

	write_byte(priv, 0, SPMR);

	/* parallel poll unconfigure */
	write_byte(priv, 0, PPR);
	/* request for data holdoff */
	tms9914_set_holdoff_mode(priv, TMS9914_HOLDOFF_ALL);
}
EXPORT_SYMBOL_GPL(tms9914_board_reset);

void tms9914_online(struct gpib_board *board, struct tms9914_priv *priv)
{
	/* set GPIB address */
	tms9914_primary_address(board, priv, board->pad);
	tms9914_secondary_address(board, priv, board->sad, board->sad >= 0);

	/* enable tms9914 interrupts */
	priv->imr0_bits |= HR_MACIE | HR_RLCIE | HR_ENDIE | HR_BOIE | HR_BIIE |
		HR_SPASIE;
	priv->imr1_bits |= HR_MAIE | HR_SRQIE | HR_UNCIE | HR_ERRIE | HR_IFCIE |
		HR_GETIE | HR_DCASIE;
	write_byte(priv, priv->imr0_bits, IMR0);
	write_byte(priv, priv->imr1_bits, IMR1);
	write_byte(priv, AUX_DAI, AUXCR);

	/* turn off reset state */
	write_byte(priv, AUX_CHIP_RESET, AUXCR);
}
EXPORT_SYMBOL_GPL(tms9914_online);

#ifdef CONFIG_HAS_IOPORT
// wrapper for inb
u8 tms9914_ioport_read_byte(struct tms9914_priv *priv, unsigned int register_num)
{
	return inb(priv->iobase + register_num * priv->offset);
}
EXPORT_SYMBOL_GPL(tms9914_ioport_read_byte);

// wrapper for outb
void tms9914_ioport_write_byte(struct tms9914_priv *priv, u8 data, unsigned int register_num)
{
	outb(data, priv->iobase + register_num * priv->offset);
	if (register_num == AUXCR)
		udelay(1);
}
EXPORT_SYMBOL_GPL(tms9914_ioport_write_byte);
#endif

// wrapper for readb
u8 tms9914_iomem_read_byte(struct tms9914_priv *priv, unsigned int register_num)
{
	return readb(priv->mmiobase + register_num * priv->offset);
}
EXPORT_SYMBOL_GPL(tms9914_iomem_read_byte);

// wrapper for writeb
void tms9914_iomem_write_byte(struct tms9914_priv *priv, u8 data, unsigned int register_num)
{
	writeb(data, priv->mmiobase + register_num * priv->offset);
	if (register_num == AUXCR)
		udelay(1);
}
EXPORT_SYMBOL_GPL(tms9914_iomem_write_byte);

static int __init tms9914_init_module(void)
{
	return 0;
}

static void __exit tms9914_exit_module(void)
{
}

module_init(tms9914_init_module);
module_exit(tms9914_exit_module);

