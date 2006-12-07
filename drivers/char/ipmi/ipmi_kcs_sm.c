/*
 * ipmi_kcs_sm.c
 *
 * State machine for handling IPMI KCS interfaces.
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * This state machine is taken from the state machine in the IPMI spec,
 * pretty much verbatim.  If you have questions about the states, see
 * that document.
 */

#include <linux/kernel.h> /* For printk. */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/ipmi_msgdefs.h>		/* for completion codes */
#include "ipmi_si_sm.h"

/* kcs_debug is a bit-field
 *	KCS_DEBUG_ENABLE -	turned on for now
 *	KCS_DEBUG_MSG    -	commands and their responses
 *	KCS_DEBUG_STATES -	state machine
 */
#define KCS_DEBUG_STATES	4
#define KCS_DEBUG_MSG		2
#define	KCS_DEBUG_ENABLE	1

static int kcs_debug;
module_param(kcs_debug, int, 0644);
MODULE_PARM_DESC(kcs_debug, "debug bitmask, 1=enable, 2=messages, 4=states");

/* The states the KCS driver may be in. */
enum kcs_states {
	KCS_IDLE,		/* The KCS interface is currently
                                   doing nothing. */
	KCS_START_OP,		/* We are starting an operation.  The
				   data is in the output buffer, but
				   nothing has been done to the
				   interface yet.  This was added to
				   the state machine in the spec to
				   wait for the initial IBF. */
	KCS_WAIT_WRITE_START,	/* We have written a write cmd to the
				   interface. */
	KCS_WAIT_WRITE,		/* We are writing bytes to the
                                   interface. */
	KCS_WAIT_WRITE_END,	/* We have written the write end cmd
                                   to the interface, and still need to
                                   write the last byte. */
	KCS_WAIT_READ,		/* We are waiting to read data from
				   the interface. */
	KCS_ERROR0,		/* State to transition to the error
				   handler, this was added to the
				   state machine in the spec to be
				   sure IBF was there. */
	KCS_ERROR1,		/* First stage error handler, wait for
                                   the interface to respond. */
	KCS_ERROR2,		/* The abort cmd has been written,
				   wait for the interface to
				   respond. */
	KCS_ERROR3,		/* We wrote some data to the
				   interface, wait for it to switch to
				   read mode. */
	KCS_HOSED		/* The hardware failed to follow the
				   state machine. */
};

#define MAX_KCS_READ_SIZE IPMI_MAX_MSG_LENGTH
#define MAX_KCS_WRITE_SIZE IPMI_MAX_MSG_LENGTH

/* Timeouts in microseconds. */
#define IBF_RETRY_TIMEOUT 1000000
#define OBF_RETRY_TIMEOUT 1000000
#define MAX_ERROR_RETRIES 10
#define ERROR0_OBF_WAIT_JIFFIES (2*HZ)

struct si_sm_data
{
	enum kcs_states  state;
	struct si_sm_io *io;
	unsigned char    write_data[MAX_KCS_WRITE_SIZE];
	int              write_pos;
	int              write_count;
	int              orig_write_count;
	unsigned char    read_data[MAX_KCS_READ_SIZE];
	int              read_pos;
	int	         truncated;

	unsigned int  error_retries;
	long          ibf_timeout;
	long          obf_timeout;
	unsigned long  error0_timeout;
};

static unsigned int init_kcs_data(struct si_sm_data *kcs,
				  struct si_sm_io *io)
{
	kcs->state = KCS_IDLE;
	kcs->io = io;
	kcs->write_pos = 0;
	kcs->write_count = 0;
	kcs->orig_write_count = 0;
	kcs->read_pos = 0;
	kcs->error_retries = 0;
	kcs->truncated = 0;
	kcs->ibf_timeout = IBF_RETRY_TIMEOUT;
	kcs->obf_timeout = OBF_RETRY_TIMEOUT;

	/* Reserve 2 I/O bytes. */
	return 2;
}

static inline unsigned char read_status(struct si_sm_data *kcs)
{
	return kcs->io->inputb(kcs->io, 1);
}

static inline unsigned char read_data(struct si_sm_data *kcs)
{
	return kcs->io->inputb(kcs->io, 0);
}

static inline void write_cmd(struct si_sm_data *kcs, unsigned char data)
{
	kcs->io->outputb(kcs->io, 1, data);
}

static inline void write_data(struct si_sm_data *kcs, unsigned char data)
{
	kcs->io->outputb(kcs->io, 0, data);
}

/* Control codes. */
#define KCS_GET_STATUS_ABORT	0x60
#define KCS_WRITE_START		0x61
#define KCS_WRITE_END		0x62
#define KCS_READ_BYTE		0x68

/* Status bits. */
#define GET_STATUS_STATE(status) (((status) >> 6) & 0x03)
#define KCS_IDLE_STATE	0
#define KCS_READ_STATE	1
#define KCS_WRITE_STATE	2
#define KCS_ERROR_STATE	3
#define GET_STATUS_ATN(status) ((status) & 0x04)
#define GET_STATUS_IBF(status) ((status) & 0x02)
#define GET_STATUS_OBF(status) ((status) & 0x01)


static inline void write_next_byte(struct si_sm_data *kcs)
{
	write_data(kcs, kcs->write_data[kcs->write_pos]);
	(kcs->write_pos)++;
	(kcs->write_count)--;
}

static inline void start_error_recovery(struct si_sm_data *kcs, char *reason)
{
	(kcs->error_retries)++;
	if (kcs->error_retries > MAX_ERROR_RETRIES) {
		if (kcs_debug & KCS_DEBUG_ENABLE)
			printk(KERN_DEBUG "ipmi_kcs_sm: kcs hosed: %s\n", reason);
		kcs->state = KCS_HOSED;
	} else {
		kcs->error0_timeout = jiffies + ERROR0_OBF_WAIT_JIFFIES;
		kcs->state = KCS_ERROR0;
	}
}

static inline void read_next_byte(struct si_sm_data *kcs)
{
	if (kcs->read_pos >= MAX_KCS_READ_SIZE) {
		/* Throw the data away and mark it truncated. */
		read_data(kcs);
		kcs->truncated = 1;
	} else {
		kcs->read_data[kcs->read_pos] = read_data(kcs);
		(kcs->read_pos)++;
	}
	write_data(kcs, KCS_READ_BYTE);
}

static inline int check_ibf(struct si_sm_data *kcs, unsigned char status,
			    long time)
{
	if (GET_STATUS_IBF(status)) {
		kcs->ibf_timeout -= time;
		if (kcs->ibf_timeout < 0) {
			start_error_recovery(kcs, "IBF not ready in time");
			kcs->ibf_timeout = IBF_RETRY_TIMEOUT;
			return 1;
		}
		return 0;
	}
	kcs->ibf_timeout = IBF_RETRY_TIMEOUT;
	return 1;
}

static inline int check_obf(struct si_sm_data *kcs, unsigned char status,
			    long time)
{
	if (!GET_STATUS_OBF(status)) {
		kcs->obf_timeout -= time;
		if (kcs->obf_timeout < 0) {
		    start_error_recovery(kcs, "OBF not ready in time");
		    return 1;
		}
		return 0;
	}
	kcs->obf_timeout = OBF_RETRY_TIMEOUT;
	return 1;
}

static void clear_obf(struct si_sm_data *kcs, unsigned char status)
{
	if (GET_STATUS_OBF(status))
		read_data(kcs);
}

static void restart_kcs_transaction(struct si_sm_data *kcs)
{
	kcs->write_count = kcs->orig_write_count;
	kcs->write_pos = 0;
	kcs->read_pos = 0;
	kcs->state = KCS_WAIT_WRITE_START;
	kcs->ibf_timeout = IBF_RETRY_TIMEOUT;
	kcs->obf_timeout = OBF_RETRY_TIMEOUT;
	write_cmd(kcs, KCS_WRITE_START);
}

static int start_kcs_transaction(struct si_sm_data *kcs, unsigned char *data,
				 unsigned int size)
{
	unsigned int i;

	if (size < 2)
		return IPMI_REQ_LEN_INVALID_ERR;
	if (size > MAX_KCS_WRITE_SIZE)
		return IPMI_REQ_LEN_EXCEEDED_ERR;

	if ((kcs->state != KCS_IDLE) && (kcs->state != KCS_HOSED))
		return IPMI_NOT_IN_MY_STATE_ERR;

	if (kcs_debug & KCS_DEBUG_MSG) {
		printk(KERN_DEBUG "start_kcs_transaction -");
		for (i = 0; i < size; i ++) {
			printk(" %02x", (unsigned char) (data [i]));
		}
		printk ("\n");
	}
	kcs->error_retries = 0;
	memcpy(kcs->write_data, data, size);
	kcs->write_count = size;
	kcs->orig_write_count = size;
	kcs->write_pos = 0;
	kcs->read_pos = 0;
	kcs->state = KCS_START_OP;
	kcs->ibf_timeout = IBF_RETRY_TIMEOUT;
	kcs->obf_timeout = OBF_RETRY_TIMEOUT;
	return 0;
}

static int get_kcs_result(struct si_sm_data *kcs, unsigned char *data,
			  unsigned int length)
{
	if (length < kcs->read_pos) {
		kcs->read_pos = length;
		kcs->truncated = 1;
	}

	memcpy(data, kcs->read_data, kcs->read_pos);

	if ((length >= 3) && (kcs->read_pos < 3)) {
		/* Guarantee that we return at least 3 bytes, with an
		   error in the third byte if it is too short. */
		data[2] = IPMI_ERR_UNSPECIFIED;
		kcs->read_pos = 3;
	}
	if (kcs->truncated) {
		/* Report a truncated error.  We might overwrite
		   another error, but that's too bad, the user needs
		   to know it was truncated. */
		data[2] = IPMI_ERR_MSG_TRUNCATED;
		kcs->truncated = 0;
	}

	return kcs->read_pos;
}

/* This implements the state machine defined in the IPMI manual, see
   that for details on how this works.  Divide that flowchart into
   sections delimited by "Wait for IBF" and this will become clear. */
static enum si_sm_result kcs_event(struct si_sm_data *kcs, long time)
{
	unsigned char status;
	unsigned char state;

	status = read_status(kcs);

	if (kcs_debug & KCS_DEBUG_STATES)
		printk(KERN_DEBUG "KCS: State = %d, %x\n", kcs->state, status);

	/* All states wait for ibf, so just do it here. */
	if (!check_ibf(kcs, status, time))
		return SI_SM_CALL_WITH_DELAY;

	/* Just about everything looks at the KCS state, so grab that, too. */
	state = GET_STATUS_STATE(status);

	switch (kcs->state) {
	case KCS_IDLE:
		/* If there's and interrupt source, turn it off. */
		clear_obf(kcs, status);

		if (GET_STATUS_ATN(status))
			return SI_SM_ATTN;
		else
			return SI_SM_IDLE;

	case KCS_START_OP:
		if (state != KCS_IDLE) {
			start_error_recovery(kcs,
					     "State machine not idle at start");
			break;
		}

		clear_obf(kcs, status);
		write_cmd(kcs, KCS_WRITE_START);
		kcs->state = KCS_WAIT_WRITE_START;
		break;

	case KCS_WAIT_WRITE_START:
		if (state != KCS_WRITE_STATE) {
			start_error_recovery(
				kcs,
				"Not in write state at write start");
			break;
		}
		read_data(kcs);
		if (kcs->write_count == 1) {
			write_cmd(kcs, KCS_WRITE_END);
			kcs->state = KCS_WAIT_WRITE_END;
		} else {
			write_next_byte(kcs);
			kcs->state = KCS_WAIT_WRITE;
		}
		break;

	case KCS_WAIT_WRITE:
		if (state != KCS_WRITE_STATE) {
			start_error_recovery(kcs,
					     "Not in write state for write");
			break;
		}
		clear_obf(kcs, status);
		if (kcs->write_count == 1) {
			write_cmd(kcs, KCS_WRITE_END);
			kcs->state = KCS_WAIT_WRITE_END;
		} else {
			write_next_byte(kcs);
		}
		break;
		
	case KCS_WAIT_WRITE_END:
		if (state != KCS_WRITE_STATE) {
			start_error_recovery(kcs,
					     "Not in write state for write end");
			break;
		}
		clear_obf(kcs, status);
		write_next_byte(kcs);
		kcs->state = KCS_WAIT_READ;
		break;

	case KCS_WAIT_READ:
		if ((state != KCS_READ_STATE) && (state != KCS_IDLE_STATE)) {
			start_error_recovery(
				kcs,
				"Not in read or idle in read state");
			break;
		}

		if (state == KCS_READ_STATE) {
			if (!check_obf(kcs, status, time))
				return SI_SM_CALL_WITH_DELAY;
			read_next_byte(kcs);
		} else {
			/* We don't implement this exactly like the state
			   machine in the spec.  Some broken hardware
			   does not write the final dummy byte to the
			   read register.  Thus obf will never go high
			   here.  We just go straight to idle, and we
			   handle clearing out obf in idle state if it
			   happens to come in. */
			clear_obf(kcs, status);
			kcs->orig_write_count = 0;
			kcs->state = KCS_IDLE;
			return SI_SM_TRANSACTION_COMPLETE;
		}
		break;

	case KCS_ERROR0:
		clear_obf(kcs, status);
		status = read_status(kcs);
		if  (GET_STATUS_OBF(status)) /* controller isn't responding */
			if (time_before(jiffies, kcs->error0_timeout))
				return SI_SM_CALL_WITH_TICK_DELAY;
		write_cmd(kcs, KCS_GET_STATUS_ABORT);
		kcs->state = KCS_ERROR1;
		break;

	case KCS_ERROR1:
		clear_obf(kcs, status);
		write_data(kcs, 0);
		kcs->state = KCS_ERROR2;
		break;
		
	case KCS_ERROR2:
		if (state != KCS_READ_STATE) {
			start_error_recovery(kcs,
					     "Not in read state for error2");
			break;
		}
		if (!check_obf(kcs, status, time))
			return SI_SM_CALL_WITH_DELAY;

		clear_obf(kcs, status);
		write_data(kcs, KCS_READ_BYTE);
		kcs->state = KCS_ERROR3;
		break;
		
	case KCS_ERROR3:
		if (state != KCS_IDLE_STATE) {
			start_error_recovery(kcs,
					     "Not in idle state for error3");
			break;
		}

		if (!check_obf(kcs, status, time))
			return SI_SM_CALL_WITH_DELAY;

		clear_obf(kcs, status);
		if (kcs->orig_write_count) {
			restart_kcs_transaction(kcs);
		} else {
			kcs->state = KCS_IDLE;
			return SI_SM_TRANSACTION_COMPLETE;
		}
		break;
			
	case KCS_HOSED:
		break;
	}

	if (kcs->state == KCS_HOSED) {
		init_kcs_data(kcs, kcs->io);
		return SI_SM_HOSED;
	}

	return SI_SM_CALL_WITHOUT_DELAY;
}

static int kcs_size(void)
{
	return sizeof(struct si_sm_data);
}

static int kcs_detect(struct si_sm_data *kcs)
{
	/* It's impossible for the KCS status register to be all 1's,
	   (assuming a properly functioning, self-initialized BMC)
	   but that's what you get from reading a bogus address, so we
	   test that first. */
	if (read_status(kcs) == 0xff)
		return 1;

	return 0;
}

static void kcs_cleanup(struct si_sm_data *kcs)
{
}

struct si_sm_handlers kcs_smi_handlers =
{
	.init_data         = init_kcs_data,
	.start_transaction = start_kcs_transaction,
	.get_result        = get_kcs_result,
	.event             = kcs_event,
	.detect            = kcs_detect,
	.cleanup           = kcs_cleanup,
	.size              = kcs_size,
};
