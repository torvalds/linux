/*
 * Data structures and definitions for the CAM system.
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * Copyright (c) 2000 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/cam.h#15 $
 */

#ifndef _AIC7XXX_CAM_H
#define _AIC7XXX_CAM_H 1

#include <linux/types.h>

#define	CAM_BUS_WILDCARD ((u_int)~0)
#define	CAM_TARGET_WILDCARD ((u_int)~0)
#define	CAM_LUN_WILDCARD ((u_int)~0)

/* CAM Status field values */
typedef enum {
	CAM_REQ_INPROG,		/* CCB request is in progress */
	CAM_REQ_CMP,		/* CCB request completed without error */
	CAM_REQ_ABORTED,	/* CCB request aborted by the host */
	CAM_UA_ABORT,		/* Unable to abort CCB request */
	CAM_REQ_CMP_ERR,	/* CCB request completed with an error */
	CAM_BUSY,		/* CAM subsytem is busy */
	CAM_REQ_INVALID,	/* CCB request was invalid */
	CAM_PATH_INVALID,	/* Supplied Path ID is invalid */
	CAM_SEL_TIMEOUT,	/* Target Selection Timeout */
	CAM_CMD_TIMEOUT,	/* Command timeout */
	CAM_SCSI_STATUS_ERROR,	/* SCSI error, look at error code in CCB */
	CAM_SCSI_BUS_RESET,	/* SCSI Bus Reset Sent/Received */
	CAM_UNCOR_PARITY,	/* Uncorrectable parity error occurred */
	CAM_AUTOSENSE_FAIL,	/* Autosense: request sense cmd fail */
	CAM_NO_HBA,		/* No HBA Detected Error */
	CAM_DATA_RUN_ERR,	/* Data Overrun error */
	CAM_UNEXP_BUSFREE,	/* Unexpected Bus Free */
	CAM_SEQUENCE_FAIL,	/* Protocol Violation */
	CAM_CCB_LEN_ERR,	/* CCB length supplied is inadequate */
	CAM_PROVIDE_FAIL,	/* Unable to provide requested capability */
	CAM_BDR_SENT,		/* A SCSI BDR msg was sent to target */
	CAM_REQ_TERMIO,		/* CCB request terminated by the host */
	CAM_UNREC_HBA_ERROR,	/* Unrecoverable Host Bus Adapter Error */
	CAM_REQ_TOO_BIG,	/* The request was too large for this host */
	CAM_UA_TERMIO,		/* Unable to terminate I/O CCB request */
	CAM_MSG_REJECT_REC,	/* Message Reject Received */
	CAM_DEV_NOT_THERE,	/* SCSI Device Not Installed/there */
	CAM_RESRC_UNAVAIL,	/* Resource Unavailable */
	/*
	 * This request should be requeued to preserve
	 * transaction ordering.  This typically occurs
	 * when the SIM recognizes an error that should
	 * freeze the queue and must place additional
	 * requests for the target at the sim level
	 * back into the XPT queue.
	 */
	CAM_REQUEUE_REQ,
	CAM_DEV_QFRZN		= 0x40,

	CAM_STATUS_MASK		= 0x3F
} cam_status;

/*
 * Definitions for the asynchronous callback CCB fields.
 */
typedef enum {
	AC_GETDEV_CHANGED	= 0x800,/* Getdev info might have changed */
	AC_INQ_CHANGED		= 0x400,/* Inquiry info might have changed */
	AC_TRANSFER_NEG		= 0x200,/* New transfer settings in effect */
	AC_LOST_DEVICE		= 0x100,/* A device went away */
	AC_FOUND_DEVICE		= 0x080,/* A new device was found */
	AC_PATH_DEREGISTERED	= 0x040,/* A path has de-registered */
	AC_PATH_REGISTERED	= 0x020,/* A new path has been registered */
	AC_SENT_BDR		= 0x010,/* A BDR message was sent to target */
	AC_SCSI_AEN		= 0x008,/* A SCSI AEN has been received */
	AC_UNSOL_RESEL		= 0x002,/* Unsolicited reselection occurred */
	AC_BUS_RESET		= 0x001 /* A SCSI bus reset occurred */
} ac_code;

typedef enum {
	CAM_DIR_IN		= SCSI_DATA_READ,
	CAM_DIR_OUT		= SCSI_DATA_WRITE,
	CAM_DIR_NONE		= SCSI_DATA_NONE
} ccb_flags;

#endif /* _AIC7XXX_CAM_H */
