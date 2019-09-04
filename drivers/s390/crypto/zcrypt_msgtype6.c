// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "zcrypt"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_msgtype6.h"
#include "zcrypt_cca_key.h"

#define CEXXC_MAX_ICA_RESPONSE_SIZE 0x77c /* max size type86 v2 reply	    */

#define CEIL4(x) ((((x)+3)/4)*4)

struct response_type {
	struct completion work;
	int type;
};
#define CEXXC_RESPONSE_TYPE_ICA  0
#define CEXXC_RESPONSE_TYPE_XCRB 1
#define CEXXC_RESPONSE_TYPE_EP11 2

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("Cryptographic Coprocessor (message type 6), " \
		   "Copyright IBM Corp. 2001, 2012");
MODULE_LICENSE("GPL");

/**
 * CPRB
 *	  Note that all shorts, ints and longs are little-endian.
 *	  All pointer fields are 32-bits long, and mean nothing
 *
 *	  A request CPRB is followed by a request_parameter_block.
 *
 *	  The request (or reply) parameter block is organized thus:
 *	    function code
 *	    VUD block
 *	    key block
 */
struct CPRB {
	unsigned short cprb_len;	/* CPRB length			 */
	unsigned char cprb_ver_id;	/* CPRB version id.		 */
	unsigned char pad_000;		/* Alignment pad byte.		 */
	unsigned char srpi_rtcode[4];	/* SRPI return code LELONG	 */
	unsigned char srpi_verb;	/* SRPI verb type		 */
	unsigned char flags;		/* flags			 */
	unsigned char func_id[2];	/* function id			 */
	unsigned char checkpoint_flag;	/*				 */
	unsigned char resv2;		/* reserved			 */
	unsigned short req_parml;	/* request parameter buffer	 */
					/* length 16-bit little endian	 */
	unsigned char req_parmp[4];	/* request parameter buffer	 *
					 * pointer (means nothing: the	 *
					 * parameter buffer follows	 *
					 * the CPRB).			 */
	unsigned char req_datal[4];	/* request data buffer		 */
					/* length	  ULELONG	 */
	unsigned char req_datap[4];	/* request data buffer		 */
					/* pointer			 */
	unsigned short rpl_parml;	/* reply  parameter buffer	 */
					/* length 16-bit little endian	 */
	unsigned char pad_001[2];	/* Alignment pad bytes. ULESHORT */
	unsigned char rpl_parmp[4];	/* reply parameter buffer	 *
					 * pointer (means nothing: the	 *
					 * parameter buffer follows	 *
					 * the CPRB).			 */
	unsigned char rpl_datal[4];	/* reply data buffer len ULELONG */
	unsigned char rpl_datap[4];	/* reply data buffer		 */
					/* pointer			 */
	unsigned short ccp_rscode;	/* server reason code	ULESHORT */
	unsigned short ccp_rtcode;	/* server return code	ULESHORT */
	unsigned char repd_parml[2];	/* replied parameter len ULESHORT*/
	unsigned char mac_data_len[2];	/* Mac Data Length	ULESHORT */
	unsigned char repd_datal[4];	/* replied data length	ULELONG	 */
	unsigned char req_pc[2];	/* PC identifier		 */
	unsigned char res_origin[8];	/* resource origin		 */
	unsigned char mac_value[8];	/* Mac Value			 */
	unsigned char logon_id[8];	/* Logon Identifier		 */
	unsigned char usage_domain[2];	/* cdx				 */
	unsigned char resv3[18];	/* reserved for requestor	 */
	unsigned short svr_namel;	/* server name length  ULESHORT	 */
	unsigned char svr_name[8];	/* server name			 */
} __packed;

struct function_and_rules_block {
	unsigned char function_code[2];
	unsigned short ulen;
	unsigned char only_rule[8];
} __packed;

/**
 * The following is used to initialize the CPRBX passed to the CEXxC/CEXxP
 * card in a type6 message. The 3 fields that must be filled in at execution
 * time are  req_parml, rpl_parml and usage_domain.
 * Everything about this interface is ascii/big-endian, since the
 * device does *not* have 'Intel inside'.
 *
 * The CPRBX is followed immediately by the parm block.
 * The parm block contains:
 * - function code ('PD' 0x5044 or 'PK' 0x504B)
 * - rule block (one of:)
 *   + 0x000A 'PKCS-1.2' (MCL2 'PD')
 *   + 0x000A 'ZERO-PAD' (MCL2 'PK')
 *   + 0x000A 'ZERO-PAD' (MCL3 'PD' or CEX2C 'PD')
 *   + 0x000A 'MRP     ' (MCL3 'PK' or CEX2C 'PK')
 * - VUD block
 */
static const struct CPRBX static_cprbx = {
	.cprb_len	=  0x00DC,
	.cprb_ver_id	=  0x02,
	.func_id	= {0x54, 0x32},
};

int speed_idx_cca(int req_type)
{
	switch (req_type) {
	case 0x4142:
	case 0x4149:
	case 0x414D:
	case 0x4341:
	case 0x4344:
	case 0x4354:
	case 0x4358:
	case 0x444B:
	case 0x4558:
	case 0x4643:
	case 0x4651:
	case 0x4C47:
	case 0x4C4B:
	case 0x4C51:
	case 0x4F48:
	case 0x504F:
	case 0x5053:
	case 0x5058:
	case 0x5343:
	case 0x5344:
	case 0x5345:
	case 0x5350:
		return LOW;
	case 0x414B:
	case 0x4345:
	case 0x4349:
	case 0x434D:
	case 0x4847:
	case 0x4849:
	case 0x484D:
	case 0x4850:
	case 0x4851:
	case 0x4954:
	case 0x4958:
	case 0x4B43:
	case 0x4B44:
	case 0x4B45:
	case 0x4B47:
	case 0x4B48:
	case 0x4B49:
	case 0x4B4E:
	case 0x4B50:
	case 0x4B52:
	case 0x4B54:
	case 0x4B58:
	case 0x4D50:
	case 0x4D53:
	case 0x4D56:
	case 0x4D58:
	case 0x5044:
	case 0x5045:
	case 0x5046:
	case 0x5047:
	case 0x5049:
	case 0x504B:
	case 0x504D:
	case 0x5254:
	case 0x5347:
	case 0x5349:
	case 0x534B:
	case 0x534D:
	case 0x5356:
	case 0x5358:
	case 0x5443:
	case 0x544B:
	case 0x5647:
		return HIGH;
	default:
		return MEDIUM;
	}
}

int speed_idx_ep11(int req_type)
{
	switch (req_type) {
	case  1:
	case  2:
	case 36:
	case 37:
	case 38:
	case 39:
	case 40:
		return LOW;
	case 17:
	case 18:
	case 19:
	case 20:
	case 21:
	case 22:
	case 26:
	case 30:
	case 31:
	case 32:
	case 33:
	case 34:
	case 35:
		return HIGH;
	default:
		return MEDIUM;
	}
}


/**
 * Convert a ICAMEX message to a type6 MEX message.
 *
 * @zq: crypto device pointer
 * @ap_msg: pointer to AP message
 * @mex: pointer to user input data
 *
 * Returns 0 on success or negative errno value.
 */
static int ICAMEX_msg_to_type6MEX_msgX(struct zcrypt_queue *zq,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo *mex)
{
	static struct type6_hdr static_type6_hdrX = {
		.type		=  0x06,
		.offset1	=  0x00000058,
		.agent_id	= {'C', 'A',},
		.function_code	= {'P', 'K'},
	};
	static struct function_and_rules_block static_pke_fnr = {
		.function_code	= {'P', 'K'},
		.ulen		= 10,
		.only_rule	= {'M', 'R', 'P', ' ', ' ', ' ', ' ', ' '}
	};
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		struct function_and_rules_block fr;
		unsigned short length;
		char text[0];
	} __packed * msg = ap_msg->message;
	int size;

	/*
	 * The inputdatalength was a selection criteria in the dispatching
	 * function zcrypt_rsa_modexpo(). However, make sure the following
	 * copy_from_user() never exceeds the allocated buffer space.
	 */
	if (WARN_ON_ONCE(mex->inputdatalength > PAGE_SIZE))
		return -EINVAL;

	/* VUD.ciphertext */
	msg->length = mex->inputdatalength + 2;
	if (copy_from_user(msg->text, mex->inputdata, mex->inputdatalength))
		return -EFAULT;

	/* Set up key which is located after the variable length text. */
	size = zcrypt_type6_mex_key_en(mex, msg->text+mex->inputdatalength);
	if (size < 0)
		return size;
	size += sizeof(*msg) + mex->inputdatalength;

	/* message header, cprbx and f&r */
	msg->hdr = static_type6_hdrX;
	msg->hdr.ToCardLen1 = size - sizeof(msg->hdr);
	msg->hdr.FromCardLen1 = CEXXC_MAX_ICA_RESPONSE_SIZE - sizeof(msg->hdr);

	msg->cprbx = static_cprbx;
	msg->cprbx.domain = AP_QID_QUEUE(zq->queue->qid);
	msg->cprbx.rpl_msgbl = msg->hdr.FromCardLen1;

	msg->fr = static_pke_fnr;

	msg->cprbx.req_parml = size - sizeof(msg->hdr) - sizeof(msg->cprbx);

	ap_msg->length = size;
	return 0;
}

/**
 * Convert a ICACRT message to a type6 CRT message.
 *
 * @zq: crypto device pointer
 * @ap_msg: pointer to AP message
 * @crt: pointer to user input data
 *
 * Returns 0 on success or negative errno value.
 */
static int ICACRT_msg_to_type6CRT_msgX(struct zcrypt_queue *zq,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo_crt *crt)
{
	static struct type6_hdr static_type6_hdrX = {
		.type		=  0x06,
		.offset1	=  0x00000058,
		.agent_id	= {'C', 'A',},
		.function_code	= {'P', 'D'},
	};
	static struct function_and_rules_block static_pkd_fnr = {
		.function_code	= {'P', 'D'},
		.ulen		= 10,
		.only_rule	= {'Z', 'E', 'R', 'O', '-', 'P', 'A', 'D'}
	};

	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		struct function_and_rules_block fr;
		unsigned short length;
		char text[0];
	} __packed * msg = ap_msg->message;
	int size;

	/*
	 * The inputdatalength was a selection criteria in the dispatching
	 * function zcrypt_rsa_crt(). However, make sure the following
	 * copy_from_user() never exceeds the allocated buffer space.
	 */
	if (WARN_ON_ONCE(crt->inputdatalength > PAGE_SIZE))
		return -EINVAL;

	/* VUD.ciphertext */
	msg->length = crt->inputdatalength + 2;
	if (copy_from_user(msg->text, crt->inputdata, crt->inputdatalength))
		return -EFAULT;

	/* Set up key which is located after the variable length text. */
	size = zcrypt_type6_crt_key(crt, msg->text + crt->inputdatalength);
	if (size < 0)
		return size;
	size += sizeof(*msg) + crt->inputdatalength;	/* total size of msg */

	/* message header, cprbx and f&r */
	msg->hdr = static_type6_hdrX;
	msg->hdr.ToCardLen1 = size -  sizeof(msg->hdr);
	msg->hdr.FromCardLen1 = CEXXC_MAX_ICA_RESPONSE_SIZE - sizeof(msg->hdr);

	msg->cprbx = static_cprbx;
	msg->cprbx.domain = AP_QID_QUEUE(zq->queue->qid);
	msg->cprbx.req_parml = msg->cprbx.rpl_msgbl =
		size - sizeof(msg->hdr) - sizeof(msg->cprbx);

	msg->fr = static_pkd_fnr;

	ap_msg->length = size;
	return 0;
}

/**
 * Convert a XCRB message to a type6 CPRB message.
 *
 * @zq: crypto device pointer
 * @ap_msg: pointer to AP message
 * @xcRB: pointer to user input data
 *
 * Returns 0 on success or -EFAULT, -EINVAL.
 */
struct type86_fmt2_msg {
	struct type86_hdr hdr;
	struct type86_fmt2_ext fmt2;
} __packed;

static int XCRB_msg_to_type6CPRB_msgX(struct ap_message *ap_msg,
				      struct ica_xcRB *xcRB,
				      unsigned int *fcode,
				      unsigned short **dom)
{
	static struct type6_hdr static_type6_hdrX = {
		.type		=  0x06,
		.offset1	=  0x00000058,
	};
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
	} __packed * msg = ap_msg->message;

	int rcblen = CEIL4(xcRB->request_control_blk_length);
	int replylen, req_sumlen, resp_sumlen;
	char *req_data = ap_msg->message + sizeof(struct type6_hdr) + rcblen;
	char *function_code;

	if (CEIL4(xcRB->request_control_blk_length) <
			xcRB->request_control_blk_length)
		return -EINVAL; /* overflow after alignment*/

	/* length checks */
	ap_msg->length = sizeof(struct type6_hdr) +
		CEIL4(xcRB->request_control_blk_length) +
		xcRB->request_data_length;
	if (ap_msg->length > MSGTYPE06_MAX_MSG_SIZE)
		return -EINVAL;

	/*
	 * Overflow check
	 * sum must be greater (or equal) than the largest operand
	 */
	req_sumlen = CEIL4(xcRB->request_control_blk_length) +
			xcRB->request_data_length;
	if ((CEIL4(xcRB->request_control_blk_length) <=
						xcRB->request_data_length) ?
		(req_sumlen < xcRB->request_data_length) :
		(req_sumlen < CEIL4(xcRB->request_control_blk_length))) {
		return -EINVAL;
	}

	if (CEIL4(xcRB->reply_control_blk_length) <
			xcRB->reply_control_blk_length)
		return -EINVAL; /* overflow after alignment*/

	replylen = sizeof(struct type86_fmt2_msg) +
		CEIL4(xcRB->reply_control_blk_length) +
		xcRB->reply_data_length;
	if (replylen > MSGTYPE06_MAX_MSG_SIZE)
		return -EINVAL;

	/*
	 * Overflow check
	 * sum must be greater (or equal) than the largest operand
	 */
	resp_sumlen = CEIL4(xcRB->reply_control_blk_length) +
			xcRB->reply_data_length;
	if ((CEIL4(xcRB->reply_control_blk_length) <= xcRB->reply_data_length) ?
		(resp_sumlen < xcRB->reply_data_length) :
		(resp_sumlen < CEIL4(xcRB->reply_control_blk_length))) {
		return -EINVAL;
	}

	/* prepare type6 header */
	msg->hdr = static_type6_hdrX;
	memcpy(msg->hdr.agent_id, &(xcRB->agent_ID), sizeof(xcRB->agent_ID));
	msg->hdr.ToCardLen1 = xcRB->request_control_blk_length;
	if (xcRB->request_data_length) {
		msg->hdr.offset2 = msg->hdr.offset1 + rcblen;
		msg->hdr.ToCardLen2 = xcRB->request_data_length;
	}
	msg->hdr.FromCardLen1 = xcRB->reply_control_blk_length;
	msg->hdr.FromCardLen2 = xcRB->reply_data_length;

	/* prepare CPRB */
	if (copy_from_user(&(msg->cprbx), xcRB->request_control_blk_addr,
		    xcRB->request_control_blk_length))
		return -EFAULT;
	if (msg->cprbx.cprb_len + sizeof(msg->hdr.function_code) >
	    xcRB->request_control_blk_length)
		return -EINVAL;
	function_code = ((unsigned char *)&msg->cprbx) + msg->cprbx.cprb_len;
	memcpy(msg->hdr.function_code, function_code,
	       sizeof(msg->hdr.function_code));

	*fcode = (msg->hdr.function_code[0] << 8) | msg->hdr.function_code[1];
	*dom = (unsigned short *)&msg->cprbx.domain;

	if (memcmp(function_code, "US", 2) == 0
	    || memcmp(function_code, "AU", 2) == 0)
		ap_msg->special = 1;
	else
		ap_msg->special = 0;

	/* copy data block */
	if (xcRB->request_data_length &&
	    copy_from_user(req_data, xcRB->request_data_address,
		xcRB->request_data_length))
		return -EFAULT;

	return 0;
}

static int xcrb_msg_to_type6_ep11cprb_msgx(struct ap_message *ap_msg,
				       struct ep11_urb *xcRB,
				       unsigned int *fcode)
{
	unsigned int lfmt;
	static struct type6_hdr static_type6_ep11_hdr = {
		.type		=  0x06,
		.rqid		= {0x00, 0x01},
		.function_code	= {0x00, 0x00},
		.agent_id[0]	=  0x58,	/* {'X'} */
		.agent_id[1]	=  0x43,	/* {'C'} */
		.offset1	=  0x00000058,
	};

	struct {
		struct type6_hdr hdr;
		struct ep11_cprb cprbx;
		unsigned char	pld_tag;	/* fixed value 0x30 */
		unsigned char	pld_lenfmt;	/* payload length format */
	} __packed * msg = ap_msg->message;

	struct pld_hdr {
		unsigned char	func_tag;	/* fixed value 0x4 */
		unsigned char	func_len;	/* fixed value 0x4 */
		unsigned int	func_val;	/* function ID	   */
		unsigned char	dom_tag;	/* fixed value 0x4 */
		unsigned char	dom_len;	/* fixed value 0x4 */
		unsigned int	dom_val;	/* domain id	   */
	} __packed * payload_hdr = NULL;

	if (CEIL4(xcRB->req_len) < xcRB->req_len)
		return -EINVAL; /* overflow after alignment*/

	/* length checks */
	ap_msg->length = sizeof(struct type6_hdr) + xcRB->req_len;
	if (CEIL4(xcRB->req_len) > MSGTYPE06_MAX_MSG_SIZE -
				   (sizeof(struct type6_hdr)))
		return -EINVAL;

	if (CEIL4(xcRB->resp_len) < xcRB->resp_len)
		return -EINVAL; /* overflow after alignment*/

	if (CEIL4(xcRB->resp_len) > MSGTYPE06_MAX_MSG_SIZE -
				    (sizeof(struct type86_fmt2_msg)))
		return -EINVAL;

	/* prepare type6 header */
	msg->hdr = static_type6_ep11_hdr;
	msg->hdr.ToCardLen1   = xcRB->req_len;
	msg->hdr.FromCardLen1 = xcRB->resp_len;

	/* Import CPRB data from the ioctl input parameter */
	if (copy_from_user(&(msg->cprbx.cprb_len),
			   (char __force __user *)xcRB->req, xcRB->req_len)) {
		return -EFAULT;
	}

	if ((msg->pld_lenfmt & 0x80) == 0x80) { /*ext.len.fmt 2 or 3*/
		switch (msg->pld_lenfmt & 0x03) {
		case 1:
			lfmt = 2;
			break;
		case 2:
			lfmt = 3;
			break;
		default:
			return -EINVAL;
		}
	} else {
		lfmt = 1; /* length format #1 */
	}
	payload_hdr = (struct pld_hdr *)((&(msg->pld_lenfmt))+lfmt);
	*fcode = payload_hdr->func_val & 0xFFFF;

	/* enable special processing based on the cprbs flags special bit */
	if (msg->cprbx.flags & 0x20)
		ap_msg->special = 1;

	return 0;
}

/**
 * Copy results from a type 86 ICA reply message back to user space.
 *
 * @zq: crypto device pointer
 * @reply: reply AP message.
 * @data: pointer to user output data
 * @length: size of user output data
 *
 * Returns 0 on success or -EINVAL, -EFAULT, -EAGAIN in case of an error.
 */
struct type86x_reply {
	struct type86_hdr hdr;
	struct type86_fmt2_ext fmt2;
	struct CPRBX cprbx;
	unsigned char pad[4];	/* 4 byte function code/rules block ? */
	unsigned short length;
	char text[0];
} __packed;

struct type86_ep11_reply {
	struct type86_hdr hdr;
	struct type86_fmt2_ext fmt2;
	struct ep11_cprb cprbx;
} __packed;

static int convert_type86_ica(struct zcrypt_queue *zq,
			  struct ap_message *reply,
			  char __user *outputdata,
			  unsigned int outputdatalength)
{
	static unsigned char static_pad[] = {
		0x00, 0x02,
		0x1B, 0x7B, 0x5D, 0xB5, 0x75, 0x01, 0x3D, 0xFD,
		0x8D, 0xD1, 0xC7, 0x03, 0x2D, 0x09, 0x23, 0x57,
		0x89, 0x49, 0xB9, 0x3F, 0xBB, 0x99, 0x41, 0x5B,
		0x75, 0x21, 0x7B, 0x9D, 0x3B, 0x6B, 0x51, 0x39,
		0xBB, 0x0D, 0x35, 0xB9, 0x89, 0x0F, 0x93, 0xA5,
		0x0B, 0x47, 0xF1, 0xD3, 0xBB, 0xCB, 0xF1, 0x9D,
		0x23, 0x73, 0x71, 0xFF, 0xF3, 0xF5, 0x45, 0xFB,
		0x61, 0x29, 0x23, 0xFD, 0xF1, 0x29, 0x3F, 0x7F,
		0x17, 0xB7, 0x1B, 0xA9, 0x19, 0xBD, 0x57, 0xA9,
		0xD7, 0x95, 0xA3, 0xCB, 0xED, 0x1D, 0xDB, 0x45,
		0x7D, 0x11, 0xD1, 0x51, 0x1B, 0xED, 0x71, 0xE9,
		0xB1, 0xD1, 0xAB, 0xAB, 0x21, 0x2B, 0x1B, 0x9F,
		0x3B, 0x9F, 0xF7, 0xF7, 0xBD, 0x63, 0xEB, 0xAD,
		0xDF, 0xB3, 0x6F, 0x5B, 0xDB, 0x8D, 0xA9, 0x5D,
		0xE3, 0x7D, 0x77, 0x49, 0x47, 0xF5, 0xA7, 0xFD,
		0xAB, 0x2F, 0x27, 0x35, 0x77, 0xD3, 0x49, 0xC9,
		0x09, 0xEB, 0xB1, 0xF9, 0xBF, 0x4B, 0xCB, 0x2B,
		0xEB, 0xEB, 0x05, 0xFF, 0x7D, 0xC7, 0x91, 0x8B,
		0x09, 0x83, 0xB9, 0xB9, 0x69, 0x33, 0x39, 0x6B,
		0x79, 0x75, 0x19, 0xBF, 0xBB, 0x07, 0x1D, 0xBD,
		0x29, 0xBF, 0x39, 0x95, 0x93, 0x1D, 0x35, 0xC7,
		0xC9, 0x4D, 0xE5, 0x97, 0x0B, 0x43, 0x9B, 0xF1,
		0x16, 0x93, 0x03, 0x1F, 0xA5, 0xFB, 0xDB, 0xF3,
		0x27, 0x4F, 0x27, 0x61, 0x05, 0x1F, 0xB9, 0x23,
		0x2F, 0xC3, 0x81, 0xA9, 0x23, 0x71, 0x55, 0x55,
		0xEB, 0xED, 0x41, 0xE5, 0xF3, 0x11, 0xF1, 0x43,
		0x69, 0x03, 0xBD, 0x0B, 0x37, 0x0F, 0x51, 0x8F,
		0x0B, 0xB5, 0x89, 0x5B, 0x67, 0xA9, 0xD9, 0x4F,
		0x01, 0xF9, 0x21, 0x77, 0x37, 0x73, 0x79, 0xC5,
		0x7F, 0x51, 0xC1, 0xCF, 0x97, 0xA1, 0x75, 0xAD,
		0x35, 0x9D, 0xD3, 0xD3, 0xA7, 0x9D, 0x5D, 0x41,
		0x6F, 0x65, 0x1B, 0xCF, 0xA9, 0x87, 0x91, 0x09
	};
	struct type86x_reply *msg = reply->message;
	unsigned short service_rc, service_rs;
	unsigned int reply_len, pad_len;
	char *data;

	service_rc = msg->cprbx.ccp_rtcode;
	if (unlikely(service_rc != 0)) {
		service_rs = msg->cprbx.ccp_rscode;
		if ((service_rc == 8 && service_rs == 66) ||
		    (service_rc == 8 && service_rs == 65) ||
		    (service_rc == 8 && service_rs == 72) ||
		    (service_rc == 8 && service_rs == 770) ||
		    (service_rc == 12 && service_rs == 769)) {
			ZCRYPT_DBF(DBF_DEBUG,
				   "device=%02x.%04x rc/rs=%d/%d => rc=EINVAL\n",
				   AP_QID_CARD(zq->queue->qid),
				   AP_QID_QUEUE(zq->queue->qid),
				   (int) service_rc, (int) service_rs);
			return -EINVAL;
		}
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       AP_QID_CARD(zq->queue->qid),
		       AP_QID_QUEUE(zq->queue->qid));
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x rc/rs=%d/%d => online=0 rc=EAGAIN\n",
			   AP_QID_CARD(zq->queue->qid),
			   AP_QID_QUEUE(zq->queue->qid),
			   (int) service_rc, (int) service_rs);
		return -EAGAIN;	/* repeat the request on a different device. */
	}
	data = msg->text;
	reply_len = msg->length - 2;
	if (reply_len > outputdatalength)
		return -EINVAL;
	/*
	 * For all encipher requests, the length of the ciphertext (reply_len)
	 * will always equal the modulus length. For MEX decipher requests
	 * the output needs to get padded. Minimum pad size is 10.
	 *
	 * Currently, the cases where padding will be added is for:
	 * - PCIXCC_MCL2 using a CRT form token (since PKD didn't support
	 *   ZERO-PAD and CRT is only supported for PKD requests)
	 * - PCICC, always
	 */
	pad_len = outputdatalength - reply_len;
	if (pad_len > 0) {
		if (pad_len < 10)
			return -EINVAL;
		/* 'restore' padding left in the CEXXC card. */
		if (copy_to_user(outputdata, static_pad, pad_len - 1))
			return -EFAULT;
		if (put_user(0, outputdata + pad_len - 1))
			return -EFAULT;
	}
	/* Copy the crypto response to user space. */
	if (copy_to_user(outputdata + pad_len, data, reply_len))
		return -EFAULT;
	return 0;
}

/**
 * Copy results from a type 86 XCRB reply message back to user space.
 *
 * @zq: crypto device pointer
 * @reply: reply AP message.
 * @xcRB: pointer to XCRB
 *
 * Returns 0 on success or -EINVAL, -EFAULT, -EAGAIN in case of an error.
 */
static int convert_type86_xcrb(struct zcrypt_queue *zq,
			       struct ap_message *reply,
			       struct ica_xcRB *xcRB)
{
	struct type86_fmt2_msg *msg = reply->message;
	char *data = reply->message;

	/* Copy CPRB to user */
	if (copy_to_user(xcRB->reply_control_blk_addr,
		data + msg->fmt2.offset1, msg->fmt2.count1))
		return -EFAULT;
	xcRB->reply_control_blk_length = msg->fmt2.count1;

	/* Copy data buffer to user */
	if (msg->fmt2.count2)
		if (copy_to_user(xcRB->reply_data_addr,
			data + msg->fmt2.offset2, msg->fmt2.count2))
			return -EFAULT;
	xcRB->reply_data_length = msg->fmt2.count2;
	return 0;
}

/**
 * Copy results from a type 86 EP11 XCRB reply message back to user space.
 *
 * @zq: crypto device pointer
 * @reply: reply AP message.
 * @xcRB: pointer to EP11 user request block
 *
 * Returns 0 on success or -EINVAL, -EFAULT, -EAGAIN in case of an error.
 */
static int convert_type86_ep11_xcrb(struct zcrypt_queue *zq,
				    struct ap_message *reply,
				    struct ep11_urb *xcRB)
{
	struct type86_fmt2_msg *msg = reply->message;
	char *data = reply->message;

	if (xcRB->resp_len < msg->fmt2.count1)
		return -EINVAL;

	/* Copy response CPRB to user */
	if (copy_to_user((char __force __user *)xcRB->resp,
			 data + msg->fmt2.offset1, msg->fmt2.count1))
		return -EFAULT;
	xcRB->resp_len = msg->fmt2.count1;
	return 0;
}

static int convert_type86_rng(struct zcrypt_queue *zq,
			  struct ap_message *reply,
			  char *buffer)
{
	struct {
		struct type86_hdr hdr;
		struct type86_fmt2_ext fmt2;
		struct CPRBX cprbx;
	} __packed * msg = reply->message;
	char *data = reply->message;

	if (msg->cprbx.ccp_rtcode != 0 || msg->cprbx.ccp_rscode != 0)
		return -EINVAL;
	memcpy(buffer, data + msg->fmt2.offset2, msg->fmt2.count2);
	return msg->fmt2.count2;
}

static int convert_response_ica(struct zcrypt_queue *zq,
			    struct ap_message *reply,
			    char __user *outputdata,
			    unsigned int outputdatalength)
{
	struct type86x_reply *msg = reply->message;

	switch (msg->hdr.type) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		return convert_error(zq, reply);
	case TYPE86_RSP_CODE:
		if (msg->cprbx.ccp_rtcode &&
		   (msg->cprbx.ccp_rscode == 0x14f) &&
		   (outputdatalength > 256)) {
			if (zq->zcard->max_exp_bit_length <= 17) {
				zq->zcard->max_exp_bit_length = 17;
				return -EAGAIN;
			} else
				return -EINVAL;
		}
		if (msg->hdr.reply_code)
			return convert_error(zq, reply);
		if (msg->cprbx.cprb_ver_id == 0x02)
			return convert_type86_ica(zq, reply,
						  outputdata, outputdatalength);
		/* fall through - wrong cprb version is an unknown response */
	default: /* Unknown response type, this should NEVER EVER happen */
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       AP_QID_CARD(zq->queue->qid),
		       AP_QID_QUEUE(zq->queue->qid));
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x rtype=0x%02x => online=0 rc=EAGAIN\n",
			   AP_QID_CARD(zq->queue->qid),
			   AP_QID_QUEUE(zq->queue->qid),
			   (int) msg->hdr.type);
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

static int convert_response_xcrb(struct zcrypt_queue *zq,
			    struct ap_message *reply,
			    struct ica_xcRB *xcRB)
{
	struct type86x_reply *msg = reply->message;

	switch (msg->hdr.type) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		xcRB->status = 0x0008044DL; /* HDD_InvalidParm */
		return convert_error(zq, reply);
	case TYPE86_RSP_CODE:
		if (msg->hdr.reply_code) {
			memcpy(&(xcRB->status), msg->fmt2.apfs, sizeof(u32));
			return convert_error(zq, reply);
		}
		if (msg->cprbx.cprb_ver_id == 0x02)
			return convert_type86_xcrb(zq, reply, xcRB);
		/* fall through - wrong cprb version is an unknown response */
	default: /* Unknown response type, this should NEVER EVER happen */
		xcRB->status = 0x0008044DL; /* HDD_InvalidParm */
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       AP_QID_CARD(zq->queue->qid),
		       AP_QID_QUEUE(zq->queue->qid));
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x rtype=0x%02x => online=0 rc=EAGAIN\n",
			   AP_QID_CARD(zq->queue->qid),
			   AP_QID_QUEUE(zq->queue->qid),
			   (int) msg->hdr.type);
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

static int convert_response_ep11_xcrb(struct zcrypt_queue *zq,
	struct ap_message *reply, struct ep11_urb *xcRB)
{
	struct type86_ep11_reply *msg = reply->message;

	switch (msg->hdr.type) {
	case TYPE82_RSP_CODE:
	case TYPE87_RSP_CODE:
		return convert_error(zq, reply);
	case TYPE86_RSP_CODE:
		if (msg->hdr.reply_code)
			return convert_error(zq, reply);
		if (msg->cprbx.cprb_ver_id == 0x04)
			return convert_type86_ep11_xcrb(zq, reply, xcRB);
		/* fall through - wrong cprb version is an unknown resp */
	default: /* Unknown response type, this should NEVER EVER happen */
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       AP_QID_CARD(zq->queue->qid),
		       AP_QID_QUEUE(zq->queue->qid));
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x rtype=0x%02x => online=0 rc=EAGAIN\n",
			   AP_QID_CARD(zq->queue->qid),
			   AP_QID_QUEUE(zq->queue->qid),
			   (int) msg->hdr.type);
		return -EAGAIN; /* repeat the request on a different device. */
	}
}

static int convert_response_rng(struct zcrypt_queue *zq,
				 struct ap_message *reply,
				 char *data)
{
	struct type86x_reply *msg = reply->message;

	switch (msg->hdr.type) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		return -EINVAL;
	case TYPE86_RSP_CODE:
		if (msg->hdr.reply_code)
			return -EINVAL;
		if (msg->cprbx.cprb_ver_id == 0x02)
			return convert_type86_rng(zq, reply, data);
		/* fall through - wrong cprb version is an unknown response */
	default: /* Unknown response type, this should NEVER EVER happen */
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       AP_QID_CARD(zq->queue->qid),
		       AP_QID_QUEUE(zq->queue->qid));
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x rtype=0x%02x => online=0 rc=EAGAIN\n",
			   AP_QID_CARD(zq->queue->qid),
			   AP_QID_QUEUE(zq->queue->qid),
			   (int) msg->hdr.type);
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

/**
 * This function is called from the AP bus code after a crypto request
 * "msg" has finished with the reply message "reply".
 * It is called from tasklet context.
 * @aq: pointer to the AP queue
 * @msg: pointer to the AP message
 * @reply: pointer to the AP reply message
 */
static void zcrypt_msgtype6_receive(struct ap_queue *aq,
				  struct ap_message *msg,
				  struct ap_message *reply)
{
	static struct error_hdr error_reply = {
		.type = TYPE82_RSP_CODE,
		.reply_code = REP82_ERROR_MACHINE_FAILURE,
	};
	struct response_type *resp_type =
		(struct response_type *) msg->private;
	struct type86x_reply *t86r;
	int length;

	/* Copy the reply message to the request message buffer. */
	if (!reply)
		goto out;	/* ap_msg->rc indicates the error */
	t86r = reply->message;
	if (t86r->hdr.type == TYPE86_RSP_CODE &&
		 t86r->cprbx.cprb_ver_id == 0x02) {
		switch (resp_type->type) {
		case CEXXC_RESPONSE_TYPE_ICA:
			length = sizeof(struct type86x_reply)
				+ t86r->length - 2;
			length = min(CEXXC_MAX_ICA_RESPONSE_SIZE, length);
			memcpy(msg->message, reply->message, length);
			break;
		case CEXXC_RESPONSE_TYPE_XCRB:
			length = t86r->fmt2.offset2 + t86r->fmt2.count2;
			length = min(MSGTYPE06_MAX_MSG_SIZE, length);
			memcpy(msg->message, reply->message, length);
			break;
		default:
			memcpy(msg->message, &error_reply,
			       sizeof(error_reply));
		}
	} else
		memcpy(msg->message, reply->message, sizeof(error_reply));
out:
	complete(&(resp_type->work));
}

/**
 * This function is called from the AP bus code after a crypto request
 * "msg" has finished with the reply message "reply".
 * It is called from tasklet context.
 * @aq: pointer to the AP queue
 * @msg: pointer to the AP message
 * @reply: pointer to the AP reply message
 */
static void zcrypt_msgtype6_receive_ep11(struct ap_queue *aq,
					 struct ap_message *msg,
					 struct ap_message *reply)
{
	static struct error_hdr error_reply = {
		.type = TYPE82_RSP_CODE,
		.reply_code = REP82_ERROR_MACHINE_FAILURE,
	};
	struct response_type *resp_type =
		(struct response_type *)msg->private;
	struct type86_ep11_reply *t86r;
	int length;

	/* Copy the reply message to the request message buffer. */
	if (!reply)
		goto out;	/* ap_msg->rc indicates the error */
	t86r = reply->message;
	if (t86r->hdr.type == TYPE86_RSP_CODE &&
	    t86r->cprbx.cprb_ver_id == 0x04) {
		switch (resp_type->type) {
		case CEXXC_RESPONSE_TYPE_EP11:
			length = t86r->fmt2.offset1 + t86r->fmt2.count1;
			length = min(MSGTYPE06_MAX_MSG_SIZE, length);
			memcpy(msg->message, reply->message, length);
			break;
		default:
			memcpy(msg->message, &error_reply, sizeof(error_reply));
		}
	} else {
		memcpy(msg->message, reply->message, sizeof(error_reply));
	}
out:
	complete(&(resp_type->work));
}

static atomic_t zcrypt_step = ATOMIC_INIT(0);

/**
 * The request distributor calls this function if it picked the CEXxC
 * device to handle a modexpo request.
 * @zq: pointer to zcrypt_queue structure that identifies the
 *	CEXxC device to the request distributor
 * @mex: pointer to the modexpo request buffer
 */
static long zcrypt_msgtype6_modexpo(struct zcrypt_queue *zq,
				  struct ica_rsa_modexpo *mex)
{
	struct ap_message ap_msg;
	struct response_type resp_type = {
		.type = CEXXC_RESPONSE_TYPE_ICA,
	};
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.receive = zcrypt_msgtype6_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &resp_type;
	rc = ICAMEX_msg_to_type6MEX_msgX(zq, &ap_msg, mex);
	if (rc)
		goto out_free;
	init_completion(&resp_type.work);
	ap_queue_message(zq->queue, &ap_msg);
	rc = wait_for_completion_interruptible(&resp_type.work);
	if (rc == 0) {
		rc = ap_msg.rc;
		if (rc == 0)
			rc = convert_response_ica(zq, &ap_msg,
						  mex->outputdata,
						  mex->outputdatalength);
	} else
		/* Signal pending. */
		ap_cancel_message(zq->queue, &ap_msg);
out_free:
	free_page((unsigned long) ap_msg.message);
	return rc;
}

/**
 * The request distributor calls this function if it picked the CEXxC
 * device to handle a modexpo_crt request.
 * @zq: pointer to zcrypt_queue structure that identifies the
 *	CEXxC device to the request distributor
 * @crt: pointer to the modexpoc_crt request buffer
 */
static long zcrypt_msgtype6_modexpo_crt(struct zcrypt_queue *zq,
				      struct ica_rsa_modexpo_crt *crt)
{
	struct ap_message ap_msg;
	struct response_type resp_type = {
		.type = CEXXC_RESPONSE_TYPE_ICA,
	};
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.receive = zcrypt_msgtype6_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &resp_type;
	rc = ICACRT_msg_to_type6CRT_msgX(zq, &ap_msg, crt);
	if (rc)
		goto out_free;
	init_completion(&resp_type.work);
	ap_queue_message(zq->queue, &ap_msg);
	rc = wait_for_completion_interruptible(&resp_type.work);
	if (rc == 0) {
		rc = ap_msg.rc;
		if (rc == 0)
			rc = convert_response_ica(zq, &ap_msg,
						  crt->outputdata,
						  crt->outputdatalength);
	} else {
		/* Signal pending. */
		ap_cancel_message(zq->queue, &ap_msg);
	}
out_free:
	free_page((unsigned long) ap_msg.message);
	return rc;
}

/**
 * Fetch function code from cprb.
 * Extracting the fc requires to copy the cprb from userspace.
 * So this function allocates memory and needs an ap_msg prepared
 * by the caller with ap_init_message(). Also the caller has to
 * make sure ap_release_message() is always called even on failure.
 */
unsigned int get_cprb_fc(struct ica_xcRB *xcRB,
				struct ap_message *ap_msg,
				unsigned int *func_code, unsigned short **dom)
{
	struct response_type resp_type = {
		.type = CEXXC_RESPONSE_TYPE_XCRB,
	};

	ap_msg->message = kmalloc(MSGTYPE06_MAX_MSG_SIZE, GFP_KERNEL);
	if (!ap_msg->message)
		return -ENOMEM;
	ap_msg->receive = zcrypt_msgtype6_receive;
	ap_msg->psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg->private = kmemdup(&resp_type, sizeof(resp_type), GFP_KERNEL);
	if (!ap_msg->private)
		return -ENOMEM;
	return XCRB_msg_to_type6CPRB_msgX(ap_msg, xcRB, func_code, dom);
}

/**
 * The request distributor calls this function if it picked the CEXxC
 * device to handle a send_cprb request.
 * @zq: pointer to zcrypt_queue structure that identifies the
 *	CEXxC device to the request distributor
 * @xcRB: pointer to the send_cprb request buffer
 */
static long zcrypt_msgtype6_send_cprb(struct zcrypt_queue *zq,
				    struct ica_xcRB *xcRB,
				    struct ap_message *ap_msg)
{
	int rc;
	struct response_type *rtype = (struct response_type *)(ap_msg->private);

	init_completion(&rtype->work);
	ap_queue_message(zq->queue, ap_msg);
	rc = wait_for_completion_interruptible(&rtype->work);
	if (rc == 0) {
		rc = ap_msg->rc;
		if (rc == 0)
			rc = convert_response_xcrb(zq, ap_msg, xcRB);
	} else
		/* Signal pending. */
		ap_cancel_message(zq->queue, ap_msg);

	return rc;
}

/**
 * Fetch function code from ep11 cprb.
 * Extracting the fc requires to copy the ep11 cprb from userspace.
 * So this function allocates memory and needs an ap_msg prepared
 * by the caller with ap_init_message(). Also the caller has to
 * make sure ap_release_message() is always called even on failure.
 */
unsigned int get_ep11cprb_fc(struct ep11_urb *xcrb,
				    struct ap_message *ap_msg,
				    unsigned int *func_code)
{
	struct response_type resp_type = {
		.type = CEXXC_RESPONSE_TYPE_EP11,
	};

	ap_msg->message = kmalloc(MSGTYPE06_MAX_MSG_SIZE, GFP_KERNEL);
	if (!ap_msg->message)
		return -ENOMEM;
	ap_msg->receive = zcrypt_msgtype6_receive_ep11;
	ap_msg->psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg->private = kmemdup(&resp_type, sizeof(resp_type), GFP_KERNEL);
	if (!ap_msg->private)
		return -ENOMEM;
	return xcrb_msg_to_type6_ep11cprb_msgx(ap_msg, xcrb, func_code);
}

/**
 * The request distributor calls this function if it picked the CEX4P
 * device to handle a send_ep11_cprb request.
 * @zq: pointer to zcrypt_queue structure that identifies the
 *	  CEX4P device to the request distributor
 * @xcRB: pointer to the ep11 user request block
 */
static long zcrypt_msgtype6_send_ep11_cprb(struct zcrypt_queue *zq,
					   struct ep11_urb *xcrb,
					   struct ap_message *ap_msg)
{
	int rc;
	unsigned int lfmt;
	struct response_type *rtype = (struct response_type *)(ap_msg->private);
	struct {
		struct type6_hdr hdr;
		struct ep11_cprb cprbx;
		unsigned char	pld_tag;	/* fixed value 0x30 */
		unsigned char	pld_lenfmt;	/* payload length format */
	} __packed * msg = ap_msg->message;
	struct pld_hdr {
		unsigned char	func_tag;	/* fixed value 0x4 */
		unsigned char	func_len;	/* fixed value 0x4 */
		unsigned int	func_val;	/* function ID	   */
		unsigned char	dom_tag;	/* fixed value 0x4 */
		unsigned char	dom_len;	/* fixed value 0x4 */
		unsigned int	dom_val;	/* domain id	   */
	} __packed * payload_hdr = NULL;


	/**
	 * The target domain field within the cprb body/payload block will be
	 * replaced by the usage domain for non-management commands only.
	 * Therefore we check the first bit of the 'flags' parameter for
	 * management command indication.
	 *   0 - non management command
	 *   1 - management command
	 */
	if (!((msg->cprbx.flags & 0x80) == 0x80)) {
		msg->cprbx.target_id = (unsigned int)
					AP_QID_QUEUE(zq->queue->qid);

		if ((msg->pld_lenfmt & 0x80) == 0x80) { /*ext.len.fmt 2 or 3*/
			switch (msg->pld_lenfmt & 0x03) {
			case 1:
				lfmt = 2;
				break;
			case 2:
				lfmt = 3;
				break;
			default:
				return -EINVAL;
			}
		} else {
			lfmt = 1; /* length format #1 */
		}
		payload_hdr = (struct pld_hdr *)((&(msg->pld_lenfmt))+lfmt);
		payload_hdr->dom_val = (unsigned int)
					AP_QID_QUEUE(zq->queue->qid);
	}

	init_completion(&rtype->work);
	ap_queue_message(zq->queue, ap_msg);
	rc = wait_for_completion_interruptible(&rtype->work);
	if (rc == 0) {
		rc = ap_msg->rc;
		if (rc == 0)
			rc = convert_response_ep11_xcrb(zq, ap_msg, xcrb);
	} else
		/* Signal pending. */
		ap_cancel_message(zq->queue, ap_msg);

	return rc;
}

unsigned int get_rng_fc(struct ap_message *ap_msg, int *func_code,
						   unsigned int *domain)
{
	struct response_type resp_type = {
		.type = CEXXC_RESPONSE_TYPE_XCRB,
	};

	ap_msg->message = kmalloc(MSGTYPE06_MAX_MSG_SIZE, GFP_KERNEL);
	if (!ap_msg->message)
		return -ENOMEM;
	ap_msg->receive = zcrypt_msgtype6_receive;
	ap_msg->psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg->private = kmemdup(&resp_type, sizeof(resp_type), GFP_KERNEL);
	if (!ap_msg->private)
		return -ENOMEM;

	rng_type6CPRB_msgX(ap_msg, ZCRYPT_RNG_BUFFER_SIZE, domain);

	*func_code = HWRNG;
	return 0;
}

/**
 * The request distributor calls this function if it picked the CEXxC
 * device to generate random data.
 * @zq: pointer to zcrypt_queue structure that identifies the
 *	CEXxC device to the request distributor
 * @buffer: pointer to a memory page to return random data
 */
static long zcrypt_msgtype6_rng(struct zcrypt_queue *zq,
				char *buffer, struct ap_message *ap_msg)
{
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		char function_code[2];
		short int rule_length;
		char rule[8];
		short int verb_length;
		short int key_length;
	} __packed * msg = ap_msg->message;
	struct response_type *rtype = (struct response_type *)(ap_msg->private);
	int rc;

	msg->cprbx.domain = AP_QID_QUEUE(zq->queue->qid);

	init_completion(&rtype->work);
	ap_queue_message(zq->queue, ap_msg);
	rc = wait_for_completion_interruptible(&rtype->work);
	if (rc == 0) {
		rc = ap_msg->rc;
		if (rc == 0)
			rc = convert_response_rng(zq, ap_msg, buffer);
	} else
		/* Signal pending. */
		ap_cancel_message(zq->queue, ap_msg);

	return rc;
}

/**
 * The crypto operations for a CEXxC card.
 */
static struct zcrypt_ops zcrypt_msgtype6_norng_ops = {
	.owner = THIS_MODULE,
	.name = MSGTYPE06_NAME,
	.variant = MSGTYPE06_VARIANT_NORNG,
	.rsa_modexpo = zcrypt_msgtype6_modexpo,
	.rsa_modexpo_crt = zcrypt_msgtype6_modexpo_crt,
	.send_cprb = zcrypt_msgtype6_send_cprb,
};

static struct zcrypt_ops zcrypt_msgtype6_ops = {
	.owner = THIS_MODULE,
	.name = MSGTYPE06_NAME,
	.variant = MSGTYPE06_VARIANT_DEFAULT,
	.rsa_modexpo = zcrypt_msgtype6_modexpo,
	.rsa_modexpo_crt = zcrypt_msgtype6_modexpo_crt,
	.send_cprb = zcrypt_msgtype6_send_cprb,
	.rng = zcrypt_msgtype6_rng,
};

static struct zcrypt_ops zcrypt_msgtype6_ep11_ops = {
	.owner = THIS_MODULE,
	.name = MSGTYPE06_NAME,
	.variant = MSGTYPE06_VARIANT_EP11,
	.rsa_modexpo = NULL,
	.rsa_modexpo_crt = NULL,
	.send_ep11_cprb = zcrypt_msgtype6_send_ep11_cprb,
};

void __init zcrypt_msgtype6_init(void)
{
	zcrypt_msgtype_register(&zcrypt_msgtype6_norng_ops);
	zcrypt_msgtype_register(&zcrypt_msgtype6_ops);
	zcrypt_msgtype_register(&zcrypt_msgtype6_ep11_ops);
}

void __exit zcrypt_msgtype6_exit(void)
{
	zcrypt_msgtype_unregister(&zcrypt_msgtype6_norng_ops);
	zcrypt_msgtype_unregister(&zcrypt_msgtype6_ops);
	zcrypt_msgtype_unregister(&zcrypt_msgtype6_ep11_ops);
}
