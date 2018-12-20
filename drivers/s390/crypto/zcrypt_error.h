/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright IBM Corp. 2001, 2006
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _ZCRYPT_ERROR_H_
#define _ZCRYPT_ERROR_H_

#include <linux/atomic.h>
#include "zcrypt_debug.h"
#include "zcrypt_api.h"
#include "zcrypt_msgtype6.h"

/**
 * Reply Messages
 *
 * Error reply messages are of two types:
 *    82:  Error (see below)
 *    88:  Error (see below)
 * Both type 82 and type 88 have the same structure in the header.
 *
 * Request reply messages are of three known types:
 *    80:  Reply from a Type 50 Request (see CEX2A-RELATED STRUCTS)
 *    84:  Reply from a Type 4 Request (see PCICA-RELATED STRUCTS)
 *    86:  Reply from a Type 6 Request (see PCICC/PCIXCC/CEX2C-RELATED STRUCTS)
 *
 */
struct error_hdr {
	unsigned char reserved1;	/* 0x00			*/
	unsigned char type;		/* 0x82 or 0x88		*/
	unsigned char reserved2[2];	/* 0x0000		*/
	unsigned char reply_code;	/* reply code		*/
	unsigned char reserved3[3];	/* 0x000000		*/
};

#define TYPE82_RSP_CODE 0x82
#define TYPE88_RSP_CODE 0x88

#define REP82_ERROR_MACHINE_FAILURE	    0x10
#define REP82_ERROR_PREEMPT_FAILURE	    0x12
#define REP82_ERROR_CHECKPT_FAILURE	    0x14
#define REP82_ERROR_MESSAGE_TYPE	    0x20
#define REP82_ERROR_INVALID_COMM_CD	    0x21 /* Type 84	*/
#define REP82_ERROR_INVALID_MSG_LEN	    0x23
#define REP82_ERROR_RESERVD_FIELD	    0x24 /* was 0x50	*/
#define REP82_ERROR_FORMAT_FIELD	    0x29
#define REP82_ERROR_INVALID_COMMAND	    0x30
#define REP82_ERROR_MALFORMED_MSG	    0x40
#define REP82_ERROR_INVALID_DOMAIN_PRECHECK 0x42
#define REP82_ERROR_RESERVED_FIELDO	    0x50 /* old value	*/
#define REP82_ERROR_WORD_ALIGNMENT	    0x60
#define REP82_ERROR_MESSAGE_LENGTH	    0x80
#define REP82_ERROR_OPERAND_INVALID	    0x82
#define REP82_ERROR_OPERAND_SIZE	    0x84
#define REP82_ERROR_EVEN_MOD_IN_OPND	    0x85
#define REP82_ERROR_RESERVED_FIELD	    0x88
#define REP82_ERROR_INVALID_DOMAIN_PENDING  0x8A
#define REP82_ERROR_TRANSPORT_FAIL	    0x90
#define REP82_ERROR_PACKET_TRUNCATED	    0xA0
#define REP82_ERROR_ZERO_BUFFER_LEN	    0xB0

#define REP88_ERROR_MODULE_FAILURE	    0x10

#define REP88_ERROR_MESSAGE_TYPE	    0x20
#define REP88_ERROR_MESSAGE_MALFORMD	    0x22
#define REP88_ERROR_MESSAGE_LENGTH	    0x23
#define REP88_ERROR_RESERVED_FIELD	    0x24
#define REP88_ERROR_KEY_TYPE		    0x34
#define REP88_ERROR_INVALID_KEY	    0x82 /* CEX2A	*/
#define REP88_ERROR_OPERAND		    0x84 /* CEX2A	*/
#define REP88_ERROR_OPERAND_EVEN_MOD	    0x85 /* CEX2A	*/

static inline int convert_error(struct zcrypt_queue *zq,
				struct ap_message *reply)
{
	struct error_hdr *ehdr = reply->message;
	int card = AP_QID_CARD(zq->queue->qid);
	int queue = AP_QID_QUEUE(zq->queue->qid);

	switch (ehdr->reply_code) {
	case REP82_ERROR_OPERAND_INVALID:
	case REP82_ERROR_OPERAND_SIZE:
	case REP82_ERROR_EVEN_MOD_IN_OPND:
	case REP88_ERROR_MESSAGE_MALFORMD:
	case REP82_ERROR_INVALID_DOMAIN_PRECHECK:
	case REP82_ERROR_INVALID_DOMAIN_PENDING:
	//   REP88_ERROR_INVALID_KEY		// '82' CEX2A
	//   REP88_ERROR_OPERAND		// '84' CEX2A
	//   REP88_ERROR_OPERAND_EVEN_MOD	// '85' CEX2A
		/* Invalid input data. */
		ZCRYPT_DBF(DBF_WARN,
			   "device=%02x.%04x reply=0x%02x => rc=EINVAL\n",
			   card, queue, ehdr->reply_code);
		return -EINVAL;
	case REP82_ERROR_MESSAGE_TYPE:
	//   REP88_ERROR_MESSAGE_TYPE		// '20' CEX2A
		/*
		 * To sent a message of the wrong type is a bug in the
		 * device driver. Send error msg, disable the device
		 * and then repeat the request.
		 */
		atomic_set(&zcrypt_rescan_req, 1);
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       card, queue);
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x reply=0x%02x => online=0 rc=EAGAIN\n",
			   card, queue, ehdr->reply_code);
		return -EAGAIN;
	case REP82_ERROR_TRANSPORT_FAIL:
		/* Card or infrastructure failure, disable card */
		atomic_set(&zcrypt_rescan_req, 1);
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       card, queue);
		/* For type 86 response show the apfs value (failure reason) */
		if (ehdr->type == TYPE86_RSP_CODE) {
			struct {
				struct type86_hdr hdr;
				struct type86_fmt2_ext fmt2;
			} __packed * head = reply->message;
			unsigned int apfs = *((u32 *)head->fmt2.apfs);

			ZCRYPT_DBF(DBF_ERR,
				   "device=%02x.%04x reply=0x%02x apfs=0x%x => online=0 rc=EAGAIN\n",
				   card, queue, apfs, ehdr->reply_code);
		} else
			ZCRYPT_DBF(DBF_ERR,
				   "device=%02x.%04x reply=0x%02x => online=0 rc=EAGAIN\n",
				   card, queue, ehdr->reply_code);
		return -EAGAIN;
	case REP82_ERROR_MACHINE_FAILURE:
	//   REP88_ERROR_MODULE_FAILURE		// '10' CEX2A
		/* If a card fails disable it and repeat the request. */
		atomic_set(&zcrypt_rescan_req, 1);
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       card, queue);
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x reply=0x%02x => online=0 rc=EAGAIN\n",
			   card, queue, ehdr->reply_code);
		return -EAGAIN;
	default:
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       card, queue);
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x reply=0x%02x => online=0 rc=EAGAIN\n",
			   card, queue, ehdr->reply_code);
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

#endif /* _ZCRYPT_ERROR_H_ */
