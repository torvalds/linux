// SPDX-License-Identifier: GPL-2.0+
/*
 * ipmi_smic_sm.c
 *
 * The state-machine driver for an IPMI SMIC driver
 *
 * It started as a copy of Corey Minyard's driver for the KSC interface
 * and the kernel patch "mmcdev-patch-245" by HP
 *
 * modified by:	Hannes Schulz <schulz@schwaar.com>
 *		ipmi@schwaar.com
 *
 *
 * Corey Minyard's driver for the KSC interface has the following
 * copyright notice:
 *   Copyright 2002 MontaVista Software Inc.
 *
 * the kernel patch "mmcdev-patch-245" by HP has the following
 * copyright notice:
 * (c) Copyright 2001 Grant Grundler (c) Copyright
 * 2001 Hewlett-Packard Company
 */

#include <linux/kernel.h> /* For printk. */
#include <linux/string.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ipmi_msgdefs.h>		/* for completion codes */
#include "ipmi_si_sm.h"

/* smic_debug is a bit-field
 *	SMIC_DEBUG_ENABLE -	turned on for now
 *	SMIC_DEBUG_MSG -	commands and their responses
 *	SMIC_DEBUG_STATES -	state machine
*/
#define SMIC_DEBUG_STATES	4
#define SMIC_DEBUG_MSG		2
#define	SMIC_DEBUG_ENABLE	1

static int smic_debug = 1;
module_param(smic_debug, int, 0644);
MODULE_PARM_DESC(smic_debug, "debug bitmask, 1=enable, 2=messages, 4=states");

enum smic_states {
	SMIC_IDLE,
	SMIC_START_OP,
	SMIC_OP_OK,
	SMIC_WRITE_START,
	SMIC_WRITE_NEXT,
	SMIC_WRITE_END,
	SMIC_WRITE2READ,
	SMIC_READ_START,
	SMIC_READ_NEXT,
	SMIC_READ_END,
	SMIC_HOSED
};

#define MAX_SMIC_READ_SIZE 80
#define MAX_SMIC_WRITE_SIZE 80
#define SMIC_MAX_ERROR_RETRIES 3

/* Timeouts in microseconds. */
#define SMIC_RETRY_TIMEOUT (2*USEC_PER_SEC)

/* SMIC Flags Register Bits */
#define SMIC_RX_DATA_READY	0x80
#define SMIC_TX_DATA_READY	0x40

/*
 * SMIC_SMI and SMIC_EVM_DATA_AVAIL are only used by
 * a few systems, and then only by Systems Management
 * Interrupts, not by the OS.  Always ignore these bits.
 *
 */
#define SMIC_SMI		0x10
#define SMIC_EVM_DATA_AVAIL	0x08
#define SMIC_SMS_DATA_AVAIL	0x04
#define SMIC_FLAG_BSY		0x01

/* SMIC Error Codes */
#define	EC_NO_ERROR		0x00
#define	EC_ABORTED		0x01
#define	EC_ILLEGAL_CONTROL	0x02
#define	EC_NO_RESPONSE		0x03
#define	EC_ILLEGAL_COMMAND	0x04
#define	EC_BUFFER_FULL		0x05

struct si_sm_data {
	enum smic_states state;
	struct si_sm_io *io;
	unsigned char	 write_data[MAX_SMIC_WRITE_SIZE];
	int		 write_pos;
	int		 write_count;
	int		 orig_write_count;
	unsigned char	 read_data[MAX_SMIC_READ_SIZE];
	int		 read_pos;
	int		 truncated;
	unsigned int	 error_retries;
	long		 smic_timeout;
};

static unsigned int init_smic_data(struct si_sm_data *smic,
				   struct si_sm_io *io)
{
	smic->state = SMIC_IDLE;
	smic->io = io;
	smic->write_pos = 0;
	smic->write_count = 0;
	smic->orig_write_count = 0;
	smic->read_pos = 0;
	smic->error_retries = 0;
	smic->truncated = 0;
	smic->smic_timeout = SMIC_RETRY_TIMEOUT;

	/* We use 3 bytes of I/O. */
	return 3;
}

static int start_smic_transaction(struct si_sm_data *smic,
				  unsigned char *data, unsigned int size)
{
	unsigned int i;

	if (size < 2)
		return IPMI_REQ_LEN_INVALID_ERR;
	if (size > MAX_SMIC_WRITE_SIZE)
		return IPMI_REQ_LEN_EXCEEDED_ERR;

	if ((smic->state != SMIC_IDLE) && (smic->state != SMIC_HOSED))
		return IPMI_NOT_IN_MY_STATE_ERR;

	if (smic_debug & SMIC_DEBUG_MSG) {
		printk(KERN_DEBUG "start_smic_transaction -");
		for (i = 0; i < size; i++)
			printk(" %02x", (unsigned char) data[i]);
		printk("\n");
	}
	smic->error_retries = 0;
	memcpy(smic->write_data, data, size);
	smic->write_count = size;
	smic->orig_write_count = size;
	smic->write_pos = 0;
	smic->read_pos = 0;
	smic->state = SMIC_START_OP;
	smic->smic_timeout = SMIC_RETRY_TIMEOUT;
	return 0;
}

static int smic_get_result(struct si_sm_data *smic,
			   unsigned char *data, unsigned int length)
{
	int i;

	if (smic_debug & SMIC_DEBUG_MSG) {
		printk(KERN_DEBUG "smic_get result -");
		for (i = 0; i < smic->read_pos; i++)
			printk(" %02x", smic->read_data[i]);
		printk("\n");
	}
	if (length < smic->read_pos) {
		smic->read_pos = length;
		smic->truncated = 1;
	}
	memcpy(data, smic->read_data, smic->read_pos);

	if ((length >= 3) && (smic->read_pos < 3)) {
		data[2] = IPMI_ERR_UNSPECIFIED;
		smic->read_pos = 3;
	}
	if (smic->truncated) {
		data[2] = IPMI_ERR_MSG_TRUNCATED;
		smic->truncated = 0;
	}
	return smic->read_pos;
}

static inline unsigned char read_smic_flags(struct si_sm_data *smic)
{
	return smic->io->inputb(smic->io, 2);
}

static inline unsigned char read_smic_status(struct si_sm_data *smic)
{
	return smic->io->inputb(smic->io, 1);
}

static inline unsigned char read_smic_data(struct si_sm_data *smic)
{
	return smic->io->inputb(smic->io, 0);
}

static inline void write_smic_flags(struct si_sm_data *smic,
				    unsigned char   flags)
{
	smic->io->outputb(smic->io, 2, flags);
}

static inline void write_smic_control(struct si_sm_data *smic,
				      unsigned char   control)
{
	smic->io->outputb(smic->io, 1, control);
}

static inline void write_si_sm_data(struct si_sm_data *smic,
				    unsigned char   data)
{
	smic->io->outputb(smic->io, 0, data);
}

static inline void start_error_recovery(struct si_sm_data *smic, char *reason)
{
	(smic->error_retries)++;
	if (smic->error_retries > SMIC_MAX_ERROR_RETRIES) {
		if (smic_debug & SMIC_DEBUG_ENABLE)
			printk(KERN_WARNING
			       "ipmi_smic_drv: smic hosed: %s\n", reason);
		smic->state = SMIC_HOSED;
	} else {
		smic->write_count = smic->orig_write_count;
		smic->write_pos = 0;
		smic->read_pos = 0;
		smic->state = SMIC_START_OP;
		smic->smic_timeout = SMIC_RETRY_TIMEOUT;
	}
}

static inline void write_next_byte(struct si_sm_data *smic)
{
	write_si_sm_data(smic, smic->write_data[smic->write_pos]);
	(smic->write_pos)++;
	(smic->write_count)--;
}

static inline void read_next_byte(struct si_sm_data *smic)
{
	if (smic->read_pos >= MAX_SMIC_READ_SIZE) {
		read_smic_data(smic);
		smic->truncated = 1;
	} else {
		smic->read_data[smic->read_pos] = read_smic_data(smic);
		smic->read_pos++;
	}
}

/*  SMIC Control/Status Code Components */
#define	SMIC_GET_STATUS		0x00	/* Control form's name */
#define	SMIC_READY		0x00	/* Status  form's name */
#define	SMIC_WR_START		0x01	/* Unified Control/Status names... */
#define	SMIC_WR_NEXT		0x02
#define	SMIC_WR_END		0x03
#define	SMIC_RD_START		0x04
#define	SMIC_RD_NEXT		0x05
#define	SMIC_RD_END		0x06
#define	SMIC_CODE_MASK		0x0f

#define	SMIC_CONTROL		0x00
#define	SMIC_STATUS		0x80
#define	SMIC_CS_MASK		0x80

#define	SMIC_SMS		0x40
#define	SMIC_SMM		0x60
#define	SMIC_STREAM_MASK	0x60

/*  SMIC Control Codes */
#define	SMIC_CC_SMS_GET_STATUS	(SMIC_CONTROL|SMIC_SMS|SMIC_GET_STATUS)
#define	SMIC_CC_SMS_WR_START	(SMIC_CONTROL|SMIC_SMS|SMIC_WR_START)
#define	SMIC_CC_SMS_WR_NEXT	(SMIC_CONTROL|SMIC_SMS|SMIC_WR_NEXT)
#define	SMIC_CC_SMS_WR_END	(SMIC_CONTROL|SMIC_SMS|SMIC_WR_END)
#define	SMIC_CC_SMS_RD_START	(SMIC_CONTROL|SMIC_SMS|SMIC_RD_START)
#define	SMIC_CC_SMS_RD_NEXT	(SMIC_CONTROL|SMIC_SMS|SMIC_RD_NEXT)
#define	SMIC_CC_SMS_RD_END	(SMIC_CONTROL|SMIC_SMS|SMIC_RD_END)

#define	SMIC_CC_SMM_GET_STATUS	(SMIC_CONTROL|SMIC_SMM|SMIC_GET_STATUS)
#define	SMIC_CC_SMM_WR_START	(SMIC_CONTROL|SMIC_SMM|SMIC_WR_START)
#define	SMIC_CC_SMM_WR_NEXT	(SMIC_CONTROL|SMIC_SMM|SMIC_WR_NEXT)
#define	SMIC_CC_SMM_WR_END	(SMIC_CONTROL|SMIC_SMM|SMIC_WR_END)
#define	SMIC_CC_SMM_RD_START	(SMIC_CONTROL|SMIC_SMM|SMIC_RD_START)
#define	SMIC_CC_SMM_RD_NEXT	(SMIC_CONTROL|SMIC_SMM|SMIC_RD_NEXT)
#define	SMIC_CC_SMM_RD_END	(SMIC_CONTROL|SMIC_SMM|SMIC_RD_END)

/*  SMIC Status Codes */
#define	SMIC_SC_SMS_READY	(SMIC_STATUS|SMIC_SMS|SMIC_READY)
#define	SMIC_SC_SMS_WR_START	(SMIC_STATUS|SMIC_SMS|SMIC_WR_START)
#define	SMIC_SC_SMS_WR_NEXT	(SMIC_STATUS|SMIC_SMS|SMIC_WR_NEXT)
#define	SMIC_SC_SMS_WR_END	(SMIC_STATUS|SMIC_SMS|SMIC_WR_END)
#define	SMIC_SC_SMS_RD_START	(SMIC_STATUS|SMIC_SMS|SMIC_RD_START)
#define	SMIC_SC_SMS_RD_NEXT	(SMIC_STATUS|SMIC_SMS|SMIC_RD_NEXT)
#define	SMIC_SC_SMS_RD_END	(SMIC_STATUS|SMIC_SMS|SMIC_RD_END)

#define	SMIC_SC_SMM_READY	(SMIC_STATUS|SMIC_SMM|SMIC_READY)
#define	SMIC_SC_SMM_WR_START	(SMIC_STATUS|SMIC_SMM|SMIC_WR_START)
#define	SMIC_SC_SMM_WR_NEXT	(SMIC_STATUS|SMIC_SMM|SMIC_WR_NEXT)
#define	SMIC_SC_SMM_WR_END	(SMIC_STATUS|SMIC_SMM|SMIC_WR_END)
#define	SMIC_SC_SMM_RD_START	(SMIC_STATUS|SMIC_SMM|SMIC_RD_START)
#define	SMIC_SC_SMM_RD_NEXT	(SMIC_STATUS|SMIC_SMM|SMIC_RD_NEXT)
#define	SMIC_SC_SMM_RD_END	(SMIC_STATUS|SMIC_SMM|SMIC_RD_END)

/* these are the control/status codes we actually use
	SMIC_CC_SMS_GET_STATUS	0x40
	SMIC_CC_SMS_WR_START	0x41
	SMIC_CC_SMS_WR_NEXT	0x42
	SMIC_CC_SMS_WR_END	0x43
	SMIC_CC_SMS_RD_START	0x44
	SMIC_CC_SMS_RD_NEXT	0x45
	SMIC_CC_SMS_RD_END	0x46

	SMIC_SC_SMS_READY	0xC0
	SMIC_SC_SMS_WR_START	0xC1
	SMIC_SC_SMS_WR_NEXT	0xC2
	SMIC_SC_SMS_WR_END	0xC3
	SMIC_SC_SMS_RD_START	0xC4
	SMIC_SC_SMS_RD_NEXT	0xC5
	SMIC_SC_SMS_RD_END	0xC6
*/

static enum si_sm_result smic_event(struct si_sm_data *smic, long time)
{
	unsigned char status;
	unsigned char flags;
	unsigned char data;

	if (smic->state == SMIC_HOSED) {
		init_smic_data(smic, smic->io);
		return SI_SM_HOSED;
	}
	if (smic->state != SMIC_IDLE) {
		if (smic_debug & SMIC_DEBUG_STATES)
			printk(KERN_DEBUG
			       "smic_event - smic->smic_timeout = %ld,"
			       " time = %ld\n",
			       smic->smic_timeout, time);
		/*
		 * FIXME: smic_event is sometimes called with time >
		 * SMIC_RETRY_TIMEOUT
		 */
		if (time < SMIC_RETRY_TIMEOUT) {
			smic->smic_timeout -= time;
			if (smic->smic_timeout < 0) {
				start_error_recovery(smic, "smic timed out.");
				return SI_SM_CALL_WITH_DELAY;
			}
		}
	}
	flags = read_smic_flags(smic);
	if (flags & SMIC_FLAG_BSY)
		return SI_SM_CALL_WITH_DELAY;

	status = read_smic_status(smic);
	if (smic_debug & SMIC_DEBUG_STATES)
		printk(KERN_DEBUG
		       "smic_event - state = %d, flags = 0x%02x,"
		       " status = 0x%02x\n",
		       smic->state, flags, status);

	switch (smic->state) {
	case SMIC_IDLE:
		/* in IDLE we check for available messages */
		if (flags & SMIC_SMS_DATA_AVAIL)
			return SI_SM_ATTN;
		return SI_SM_IDLE;

	case SMIC_START_OP:
		/* sanity check whether smic is really idle */
		write_smic_control(smic, SMIC_CC_SMS_GET_STATUS);
		write_smic_flags(smic, flags | SMIC_FLAG_BSY);
		smic->state = SMIC_OP_OK;
		break;

	case SMIC_OP_OK:
		if (status != SMIC_SC_SMS_READY) {
			/* this should not happen */
			start_error_recovery(smic,
					     "state = SMIC_OP_OK,"
					     " status != SMIC_SC_SMS_READY");
			return SI_SM_CALL_WITH_DELAY;
		}
		/* OK so far; smic is idle let us start ... */
		write_smic_control(smic, SMIC_CC_SMS_WR_START);
		write_next_byte(smic);
		write_smic_flags(smic, flags | SMIC_FLAG_BSY);
		smic->state = SMIC_WRITE_START;
		break;

	case SMIC_WRITE_START:
		if (status != SMIC_SC_SMS_WR_START) {
			start_error_recovery(smic,
					     "state = SMIC_WRITE_START, "
					     "status != SMIC_SC_SMS_WR_START");
			return SI_SM_CALL_WITH_DELAY;
		}
		/*
		 * we must not issue WR_(NEXT|END) unless
		 * TX_DATA_READY is set
		 * */
		if (flags & SMIC_TX_DATA_READY) {
			if (smic->write_count == 1) {
				/* last byte */
				write_smic_control(smic, SMIC_CC_SMS_WR_END);
				smic->state = SMIC_WRITE_END;
			} else {
				write_smic_control(smic, SMIC_CC_SMS_WR_NEXT);
				smic->state = SMIC_WRITE_NEXT;
			}
			write_next_byte(smic);
			write_smic_flags(smic, flags | SMIC_FLAG_BSY);
		} else
			return SI_SM_CALL_WITH_DELAY;
		break;

	case SMIC_WRITE_NEXT:
		if (status != SMIC_SC_SMS_WR_NEXT) {
			start_error_recovery(smic,
					     "state = SMIC_WRITE_NEXT, "
					     "status != SMIC_SC_SMS_WR_NEXT");
			return SI_SM_CALL_WITH_DELAY;
		}
		/* this is the same code as in SMIC_WRITE_START */
		if (flags & SMIC_TX_DATA_READY) {
			if (smic->write_count == 1) {
				write_smic_control(smic, SMIC_CC_SMS_WR_END);
				smic->state = SMIC_WRITE_END;
			} else {
				write_smic_control(smic, SMIC_CC_SMS_WR_NEXT);
				smic->state = SMIC_WRITE_NEXT;
			}
			write_next_byte(smic);
			write_smic_flags(smic, flags | SMIC_FLAG_BSY);
		} else
			return SI_SM_CALL_WITH_DELAY;
		break;

	case SMIC_WRITE_END:
		if (status != SMIC_SC_SMS_WR_END) {
			start_error_recovery(smic,
					     "state = SMIC_WRITE_END, "
					     "status != SMIC_SC_SMS_WR_END");
			return SI_SM_CALL_WITH_DELAY;
		}
		/* data register holds an error code */
		data = read_smic_data(smic);
		if (data != 0) {
			if (smic_debug & SMIC_DEBUG_ENABLE)
				printk(KERN_DEBUG
				       "SMIC_WRITE_END: data = %02x\n", data);
			start_error_recovery(smic,
					     "state = SMIC_WRITE_END, "
					     "data != SUCCESS");
			return SI_SM_CALL_WITH_DELAY;
		} else
			smic->state = SMIC_WRITE2READ;
		break;

	case SMIC_WRITE2READ:
		/*
		 * we must wait for RX_DATA_READY to be set before we
		 * can continue
		 */
		if (flags & SMIC_RX_DATA_READY) {
			write_smic_control(smic, SMIC_CC_SMS_RD_START);
			write_smic_flags(smic, flags | SMIC_FLAG_BSY);
			smic->state = SMIC_READ_START;
		} else
			return SI_SM_CALL_WITH_DELAY;
		break;

	case SMIC_READ_START:
		if (status != SMIC_SC_SMS_RD_START) {
			start_error_recovery(smic,
					     "state = SMIC_READ_START, "
					     "status != SMIC_SC_SMS_RD_START");
			return SI_SM_CALL_WITH_DELAY;
		}
		if (flags & SMIC_RX_DATA_READY) {
			read_next_byte(smic);
			write_smic_control(smic, SMIC_CC_SMS_RD_NEXT);
			write_smic_flags(smic, flags | SMIC_FLAG_BSY);
			smic->state = SMIC_READ_NEXT;
		} else
			return SI_SM_CALL_WITH_DELAY;
		break;

	case SMIC_READ_NEXT:
		switch (status) {
		/*
		 * smic tells us that this is the last byte to be read
		 * --> clean up
		 */
		case SMIC_SC_SMS_RD_END:
			read_next_byte(smic);
			write_smic_control(smic, SMIC_CC_SMS_RD_END);
			write_smic_flags(smic, flags | SMIC_FLAG_BSY);
			smic->state = SMIC_READ_END;
			break;
		case SMIC_SC_SMS_RD_NEXT:
			if (flags & SMIC_RX_DATA_READY) {
				read_next_byte(smic);
				write_smic_control(smic, SMIC_CC_SMS_RD_NEXT);
				write_smic_flags(smic, flags | SMIC_FLAG_BSY);
				smic->state = SMIC_READ_NEXT;
			} else
				return SI_SM_CALL_WITH_DELAY;
			break;
		default:
			start_error_recovery(
				smic,
				"state = SMIC_READ_NEXT, "
				"status != SMIC_SC_SMS_RD_(NEXT|END)");
			return SI_SM_CALL_WITH_DELAY;
		}
		break;

	case SMIC_READ_END:
		if (status != SMIC_SC_SMS_READY) {
			start_error_recovery(smic,
					     "state = SMIC_READ_END, "
					     "status != SMIC_SC_SMS_READY");
			return SI_SM_CALL_WITH_DELAY;
		}
		data = read_smic_data(smic);
		/* data register holds an error code */
		if (data != 0) {
			if (smic_debug & SMIC_DEBUG_ENABLE)
				printk(KERN_DEBUG
				       "SMIC_READ_END: data = %02x\n", data);
			start_error_recovery(smic,
					     "state = SMIC_READ_END, "
					     "data != SUCCESS");
			return SI_SM_CALL_WITH_DELAY;
		} else {
			smic->state = SMIC_IDLE;
			return SI_SM_TRANSACTION_COMPLETE;
		}

	case SMIC_HOSED:
		init_smic_data(smic, smic->io);
		return SI_SM_HOSED;

	default:
		if (smic_debug & SMIC_DEBUG_ENABLE) {
			printk(KERN_DEBUG "smic->state = %d\n", smic->state);
			start_error_recovery(smic, "state = UNKNOWN");
			return SI_SM_CALL_WITH_DELAY;
		}
	}
	smic->smic_timeout = SMIC_RETRY_TIMEOUT;
	return SI_SM_CALL_WITHOUT_DELAY;
}

static int smic_detect(struct si_sm_data *smic)
{
	/*
	 * It's impossible for the SMIC fnags register to be all 1's,
	 * (assuming a properly functioning, self-initialized BMC)
	 * but that's what you get from reading a bogus address, so we
	 * test that first.
	 */
	if (read_smic_flags(smic) == 0xff)
		return 1;

	return 0;
}

static void smic_cleanup(struct si_sm_data *kcs)
{
}

static int smic_size(void)
{
	return sizeof(struct si_sm_data);
}

const struct si_sm_handlers smic_smi_handlers = {
	.init_data         = init_smic_data,
	.start_transaction = start_smic_transaction,
	.get_result        = smic_get_result,
	.event             = smic_event,
	.detect            = smic_detect,
	.cleanup           = smic_cleanup,
	.size              = smic_size,
};
