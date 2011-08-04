/*
 *  linux/drivers/s390/crypto/zcrypt_pcixcc.c
 *
 *  zcrypt 2.1.0
 *
 *  Copyright (C)  2001, 2006 IBM Corporation
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <asm/uaccess.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_pcicc.h"
#include "zcrypt_pcixcc.h"
#include "zcrypt_cca_key.h"

#define PCIXCC_MIN_MOD_SIZE	 16	/*  128 bits	*/
#define PCIXCC_MIN_MOD_SIZE_OLD	 64	/*  512 bits	*/
#define PCIXCC_MAX_MOD_SIZE	256	/* 2048 bits	*/
#define CEX3C_MIN_MOD_SIZE	PCIXCC_MIN_MOD_SIZE
#define CEX3C_MAX_MOD_SIZE	512	/* 4096 bits	*/

#define PCIXCC_MCL2_SPEED_RATING	7870
#define PCIXCC_MCL3_SPEED_RATING	7870
#define CEX2C_SPEED_RATING		7000
#define CEX3C_SPEED_RATING		6500

#define PCIXCC_MAX_ICA_MESSAGE_SIZE 0x77c  /* max size type6 v2 crt message */
#define PCIXCC_MAX_ICA_RESPONSE_SIZE 0x77c /* max size type86 v2 reply	    */

#define PCIXCC_MAX_XCRB_MESSAGE_SIZE (12*1024)
#define PCIXCC_MAX_XCRB_RESPONSE_SIZE PCIXCC_MAX_XCRB_MESSAGE_SIZE
#define PCIXCC_MAX_XCRB_DATA_SIZE (11*1024)
#define PCIXCC_MAX_XCRB_REPLY_SIZE (5*1024)

#define PCIXCC_MAX_RESPONSE_SIZE PCIXCC_MAX_XCRB_RESPONSE_SIZE

#define PCIXCC_CLEANUP_TIME	(15*HZ)

#define CEIL4(x) ((((x)+3)/4)*4)

struct response_type {
	struct completion work;
	int type;
};
#define PCIXCC_RESPONSE_TYPE_ICA  0
#define PCIXCC_RESPONSE_TYPE_XCRB 1

static struct ap_device_id zcrypt_pcixcc_ids[] = {
	{ AP_DEVICE(AP_DEVICE_TYPE_PCIXCC) },
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX2C) },
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX3C) },
	{ /* end of list */ },
};

#ifndef CONFIG_ZCRYPT_MONOLITHIC
MODULE_DEVICE_TABLE(ap, zcrypt_pcixcc_ids);
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("PCIXCC Cryptographic Coprocessor device driver, "
		   "Copyright 2001, 2006 IBM Corporation");
MODULE_LICENSE("GPL");
#endif

static int zcrypt_pcixcc_probe(struct ap_device *ap_dev);
static void zcrypt_pcixcc_remove(struct ap_device *ap_dev);
static void zcrypt_pcixcc_receive(struct ap_device *, struct ap_message *,
				 struct ap_message *);

static struct ap_driver zcrypt_pcixcc_driver = {
	.probe = zcrypt_pcixcc_probe,
	.remove = zcrypt_pcixcc_remove,
	.receive = zcrypt_pcixcc_receive,
	.ids = zcrypt_pcixcc_ids,
	.request_timeout = PCIXCC_CLEANUP_TIME,
};

/**
 * The following is used to initialize the CPRBX passed to the PCIXCC/CEX2C
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
static struct CPRBX static_cprbx = {
	.cprb_len	=  0x00DC,
	.cprb_ver_id	=  0x02,
	.func_id	= {0x54,0x32},
};

/**
 * Convert a ICAMEX message to a type6 MEX message.
 *
 * @zdev: crypto device pointer
 * @ap_msg: pointer to AP message
 * @mex: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICAMEX_msg_to_type6MEX_msgX(struct zcrypt_device *zdev,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo *mex)
{
	static struct type6_hdr static_type6_hdrX = {
		.type		=  0x06,
		.offset1	=  0x00000058,
		.agent_id	= {'C','A',},
		.function_code	= {'P','K'},
	};
	static struct function_and_rules_block static_pke_fnr = {
		.function_code	= {'P','K'},
		.ulen		= 10,
		.only_rule	= {'M','R','P',' ',' ',' ',' ',' '}
	};
	static struct function_and_rules_block static_pke_fnr_MCL2 = {
		.function_code	= {'P','K'},
		.ulen		= 10,
		.only_rule	= {'Z','E','R','O','-','P','A','D'}
	};
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		struct function_and_rules_block fr;
		unsigned short length;
		char text[0];
	} __attribute__((packed)) *msg = ap_msg->message;
	int size;

	/* VUD.ciphertext */
	msg->length = mex->inputdatalength + 2;
	if (copy_from_user(msg->text, mex->inputdata, mex->inputdatalength))
		return -EFAULT;

	/* Set up key which is located after the variable length text. */
	size = zcrypt_type6_mex_key_en(mex, msg->text+mex->inputdatalength, 1);
	if (size < 0)
		return size;
	size += sizeof(*msg) + mex->inputdatalength;

	/* message header, cprbx and f&r */
	msg->hdr = static_type6_hdrX;
	msg->hdr.ToCardLen1 = size - sizeof(msg->hdr);
	msg->hdr.FromCardLen1 = PCIXCC_MAX_ICA_RESPONSE_SIZE - sizeof(msg->hdr);

	msg->cprbx = static_cprbx;
	msg->cprbx.domain = AP_QID_QUEUE(zdev->ap_dev->qid);
	msg->cprbx.rpl_msgbl = msg->hdr.FromCardLen1;

	msg->fr = (zdev->user_space_type == ZCRYPT_PCIXCC_MCL2) ?
		static_pke_fnr_MCL2 : static_pke_fnr;

	msg->cprbx.req_parml = size - sizeof(msg->hdr) - sizeof(msg->cprbx);

	ap_msg->length = size;
	return 0;
}

/**
 * Convert a ICACRT message to a type6 CRT message.
 *
 * @zdev: crypto device pointer
 * @ap_msg: pointer to AP message
 * @crt: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICACRT_msg_to_type6CRT_msgX(struct zcrypt_device *zdev,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo_crt *crt)
{
	static struct type6_hdr static_type6_hdrX = {
		.type		=  0x06,
		.offset1	=  0x00000058,
		.agent_id	= {'C','A',},
		.function_code	= {'P','D'},
	};
	static struct function_and_rules_block static_pkd_fnr = {
		.function_code	= {'P','D'},
		.ulen		= 10,
		.only_rule	= {'Z','E','R','O','-','P','A','D'}
	};

	static struct function_and_rules_block static_pkd_fnr_MCL2 = {
		.function_code	= {'P','D'},
		.ulen		= 10,
		.only_rule	= {'P','K','C','S','-','1','.','2'}
	};
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		struct function_and_rules_block fr;
		unsigned short length;
		char text[0];
	} __attribute__((packed)) *msg = ap_msg->message;
	int size;

	/* VUD.ciphertext */
	msg->length = crt->inputdatalength + 2;
	if (copy_from_user(msg->text, crt->inputdata, crt->inputdatalength))
		return -EFAULT;

	/* Set up key which is located after the variable length text. */
	size = zcrypt_type6_crt_key(crt, msg->text + crt->inputdatalength, 1);
	if (size < 0)
		return size;
	size += sizeof(*msg) + crt->inputdatalength;	/* total size of msg */

	/* message header, cprbx and f&r */
	msg->hdr = static_type6_hdrX;
	msg->hdr.ToCardLen1 = size -  sizeof(msg->hdr);
	msg->hdr.FromCardLen1 = PCIXCC_MAX_ICA_RESPONSE_SIZE - sizeof(msg->hdr);

	msg->cprbx = static_cprbx;
	msg->cprbx.domain = AP_QID_QUEUE(zdev->ap_dev->qid);
	msg->cprbx.req_parml = msg->cprbx.rpl_msgbl =
		size - sizeof(msg->hdr) - sizeof(msg->cprbx);

	msg->fr = (zdev->user_space_type == ZCRYPT_PCIXCC_MCL2) ?
		static_pkd_fnr_MCL2 : static_pkd_fnr;

	ap_msg->length = size;
	return 0;
}

/**
 * Convert a XCRB message to a type6 CPRB message.
 *
 * @zdev: crypto device pointer
 * @ap_msg: pointer to AP message
 * @xcRB: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
struct type86_fmt2_msg {
	struct type86_hdr hdr;
	struct type86_fmt2_ext fmt2;
} __attribute__((packed));

static int XCRB_msg_to_type6CPRB_msgX(struct zcrypt_device *zdev,
				       struct ap_message *ap_msg,
				       struct ica_xcRB *xcRB)
{
	static struct type6_hdr static_type6_hdrX = {
		.type		=  0x06,
		.offset1	=  0x00000058,
	};
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
	} __attribute__((packed)) *msg = ap_msg->message;

	int rcblen = CEIL4(xcRB->request_control_blk_length);
	int replylen;
	char *req_data = ap_msg->message + sizeof(struct type6_hdr) + rcblen;
	char *function_code;

	/* length checks */
	ap_msg->length = sizeof(struct type6_hdr) +
		CEIL4(xcRB->request_control_blk_length) +
		xcRB->request_data_length;
	if (ap_msg->length > PCIXCC_MAX_XCRB_MESSAGE_SIZE)
		return -EFAULT;
	if (CEIL4(xcRB->reply_control_blk_length) > PCIXCC_MAX_XCRB_REPLY_SIZE)
		return -EFAULT;
	if (CEIL4(xcRB->reply_data_length) > PCIXCC_MAX_XCRB_DATA_SIZE)
		return -EFAULT;
	replylen = CEIL4(xcRB->reply_control_blk_length) +
		CEIL4(xcRB->reply_data_length) +
		sizeof(struct type86_fmt2_msg);
	if (replylen > PCIXCC_MAX_XCRB_RESPONSE_SIZE) {
		xcRB->reply_control_blk_length = PCIXCC_MAX_XCRB_RESPONSE_SIZE -
			(sizeof(struct type86_fmt2_msg) +
			    CEIL4(xcRB->reply_data_length));
	}

	/* prepare type6 header */
	msg->hdr = static_type6_hdrX;
	memcpy(msg->hdr.agent_id , &(xcRB->agent_ID), sizeof(xcRB->agent_ID));
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
		return -EFAULT;
	function_code = ((unsigned char *)&msg->cprbx) + msg->cprbx.cprb_len;
	memcpy(msg->hdr.function_code, function_code, sizeof(msg->hdr.function_code));

	if (memcmp(function_code, "US", 2) == 0)
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

/**
 * Prepare a type6 CPRB message for random number generation
 *
 * @ap_dev: AP device pointer
 * @ap_msg: pointer to AP message
 */
static void rng_type6CPRB_msgX(struct ap_device *ap_dev,
			       struct ap_message *ap_msg,
			       unsigned random_number_length)
{
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		char function_code[2];
		short int rule_length;
		char rule[8];
		short int verb_length;
		short int key_length;
	} __attribute__((packed)) *msg = ap_msg->message;
	static struct type6_hdr static_type6_hdrX = {
		.type		= 0x06,
		.offset1	= 0x00000058,
		.agent_id	= {'C', 'A'},
		.function_code	= {'R', 'L'},
		.ToCardLen1	= sizeof *msg - sizeof(msg->hdr),
		.FromCardLen1	= sizeof *msg - sizeof(msg->hdr),
	};
	static struct CPRBX local_cprbx = {
		.cprb_len	= 0x00dc,
		.cprb_ver_id	= 0x02,
		.func_id	= {0x54, 0x32},
		.req_parml	= sizeof *msg - sizeof(msg->hdr) -
				  sizeof(msg->cprbx),
		.rpl_msgbl	= sizeof *msg - sizeof(msg->hdr),
	};

	msg->hdr = static_type6_hdrX;
	msg->hdr.FromCardLen2 = random_number_length,
	msg->cprbx = local_cprbx;
	msg->cprbx.rpl_datal = random_number_length,
	msg->cprbx.domain = AP_QID_QUEUE(ap_dev->qid);
	memcpy(msg->function_code, msg->hdr.function_code, 0x02);
	msg->rule_length = 0x0a;
	memcpy(msg->rule, "RANDOM  ", 8);
	msg->verb_length = 0x02;
	msg->key_length = 0x02;
	ap_msg->length = sizeof *msg;
}

/**
 * Copy results from a type 86 ICA reply message back to user space.
 *
 * @zdev: crypto device pointer
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
} __attribute__((packed));

static int convert_type86_ica(struct zcrypt_device *zdev,
			  struct ap_message *reply,
			  char __user *outputdata,
			  unsigned int outputdatalength)
{
	static unsigned char static_pad[] = {
		0x00,0x02,
		0x1B,0x7B,0x5D,0xB5,0x75,0x01,0x3D,0xFD,
		0x8D,0xD1,0xC7,0x03,0x2D,0x09,0x23,0x57,
		0x89,0x49,0xB9,0x3F,0xBB,0x99,0x41,0x5B,
		0x75,0x21,0x7B,0x9D,0x3B,0x6B,0x51,0x39,
		0xBB,0x0D,0x35,0xB9,0x89,0x0F,0x93,0xA5,
		0x0B,0x47,0xF1,0xD3,0xBB,0xCB,0xF1,0x9D,
		0x23,0x73,0x71,0xFF,0xF3,0xF5,0x45,0xFB,
		0x61,0x29,0x23,0xFD,0xF1,0x29,0x3F,0x7F,
		0x17,0xB7,0x1B,0xA9,0x19,0xBD,0x57,0xA9,
		0xD7,0x95,0xA3,0xCB,0xED,0x1D,0xDB,0x45,
		0x7D,0x11,0xD1,0x51,0x1B,0xED,0x71,0xE9,
		0xB1,0xD1,0xAB,0xAB,0x21,0x2B,0x1B,0x9F,
		0x3B,0x9F,0xF7,0xF7,0xBD,0x63,0xEB,0xAD,
		0xDF,0xB3,0x6F,0x5B,0xDB,0x8D,0xA9,0x5D,
		0xE3,0x7D,0x77,0x49,0x47,0xF5,0xA7,0xFD,
		0xAB,0x2F,0x27,0x35,0x77,0xD3,0x49,0xC9,
		0x09,0xEB,0xB1,0xF9,0xBF,0x4B,0xCB,0x2B,
		0xEB,0xEB,0x05,0xFF,0x7D,0xC7,0x91,0x8B,
		0x09,0x83,0xB9,0xB9,0x69,0x33,0x39,0x6B,
		0x79,0x75,0x19,0xBF,0xBB,0x07,0x1D,0xBD,
		0x29,0xBF,0x39,0x95,0x93,0x1D,0x35,0xC7,
		0xC9,0x4D,0xE5,0x97,0x0B,0x43,0x9B,0xF1,
		0x16,0x93,0x03,0x1F,0xA5,0xFB,0xDB,0xF3,
		0x27,0x4F,0x27,0x61,0x05,0x1F,0xB9,0x23,
		0x2F,0xC3,0x81,0xA9,0x23,0x71,0x55,0x55,
		0xEB,0xED,0x41,0xE5,0xF3,0x11,0xF1,0x43,
		0x69,0x03,0xBD,0x0B,0x37,0x0F,0x51,0x8F,
		0x0B,0xB5,0x89,0x5B,0x67,0xA9,0xD9,0x4F,
		0x01,0xF9,0x21,0x77,0x37,0x73,0x79,0xC5,
		0x7F,0x51,0xC1,0xCF,0x97,0xA1,0x75,0xAD,
		0x35,0x9D,0xD3,0xD3,0xA7,0x9D,0x5D,0x41,
		0x6F,0x65,0x1B,0xCF,0xA9,0x87,0x91,0x09
	};
	struct type86x_reply *msg = reply->message;
	unsigned short service_rc, service_rs;
	unsigned int reply_len, pad_len;
	char *data;

	service_rc = msg->cprbx.ccp_rtcode;
	if (unlikely(service_rc != 0)) {
		service_rs = msg->cprbx.ccp_rscode;
		if (service_rc == 8 && service_rs == 66)
			return -EINVAL;
		if (service_rc == 8 && service_rs == 65)
			return -EINVAL;
		if (service_rc == 8 && service_rs == 770)
			return -EINVAL;
		if (service_rc == 8 && service_rs == 783) {
			zdev->min_mod_size = PCIXCC_MIN_MOD_SIZE_OLD;
			return -EAGAIN;
		}
		if (service_rc == 12 && service_rs == 769)
			return -EINVAL;
		if (service_rc == 8 && service_rs == 72)
			return -EINVAL;
		zdev->online = 0;
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
		/* 'restore' padding left in the PCICC/PCIXCC card. */
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
 * @zdev: crypto device pointer
 * @reply: reply AP message.
 * @xcRB: pointer to XCRB
 *
 * Returns 0 on success or -EINVAL, -EFAULT, -EAGAIN in case of an error.
 */
static int convert_type86_xcrb(struct zcrypt_device *zdev,
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

static int convert_type86_rng(struct zcrypt_device *zdev,
			  struct ap_message *reply,
			  char *buffer)
{
	struct {
		struct type86_hdr hdr;
		struct type86_fmt2_ext fmt2;
		struct CPRBX cprbx;
	} __attribute__((packed)) *msg = reply->message;
	char *data = reply->message;

	if (msg->cprbx.ccp_rtcode != 0 || msg->cprbx.ccp_rscode != 0)
		return -EINVAL;
	memcpy(buffer, data + msg->fmt2.offset2, msg->fmt2.count2);
	return msg->fmt2.count2;
}

static int convert_response_ica(struct zcrypt_device *zdev,
			    struct ap_message *reply,
			    char __user *outputdata,
			    unsigned int outputdatalength)
{
	struct type86x_reply *msg = reply->message;

	/* Response type byte is the second byte in the response. */
	switch (((unsigned char *) reply->message)[1]) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		return convert_error(zdev, reply);
	case TYPE86_RSP_CODE:
		if (msg->cprbx.ccp_rtcode &&
		   (msg->cprbx.ccp_rscode == 0x14f) &&
		   (outputdatalength > 256)) {
			if (zdev->max_exp_bit_length <= 17) {
				zdev->max_exp_bit_length = 17;
				return -EAGAIN;
			} else
				return -EINVAL;
		}
		if (msg->hdr.reply_code)
			return convert_error(zdev, reply);
		if (msg->cprbx.cprb_ver_id == 0x02)
			return convert_type86_ica(zdev, reply,
						  outputdata, outputdatalength);
		/* Fall through, no break, incorrect cprb version is an unknown
		 * response */
	default: /* Unknown response type, this should NEVER EVER happen */
		zdev->online = 0;
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

static int convert_response_xcrb(struct zcrypt_device *zdev,
			    struct ap_message *reply,
			    struct ica_xcRB *xcRB)
{
	struct type86x_reply *msg = reply->message;

	/* Response type byte is the second byte in the response. */
	switch (((unsigned char *) reply->message)[1]) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		xcRB->status = 0x0008044DL; /* HDD_InvalidParm */
		return convert_error(zdev, reply);
	case TYPE86_RSP_CODE:
		if (msg->hdr.reply_code) {
			memcpy(&(xcRB->status), msg->fmt2.apfs, sizeof(u32));
			return convert_error(zdev, reply);
		}
		if (msg->cprbx.cprb_ver_id == 0x02)
			return convert_type86_xcrb(zdev, reply, xcRB);
		/* Fall through, no break, incorrect cprb version is an unknown
		 * response */
	default: /* Unknown response type, this should NEVER EVER happen */
		xcRB->status = 0x0008044DL; /* HDD_InvalidParm */
		zdev->online = 0;
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

static int convert_response_rng(struct zcrypt_device *zdev,
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
			return convert_type86_rng(zdev, reply, data);
		/* Fall through, no break, incorrect cprb version is an unknown
		 * response */
	default: /* Unknown response type, this should NEVER EVER happen */
		zdev->online = 0;
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

/**
 * This function is called from the AP bus code after a crypto request
 * "msg" has finished with the reply message "reply".
 * It is called from tasklet context.
 * @ap_dev: pointer to the AP device
 * @msg: pointer to the AP message
 * @reply: pointer to the AP reply message
 */
static void zcrypt_pcixcc_receive(struct ap_device *ap_dev,
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
	if (IS_ERR(reply)) {
		memcpy(msg->message, &error_reply, sizeof(error_reply));
		goto out;
	}
	t86r = reply->message;
	if (t86r->hdr.type == TYPE86_RSP_CODE &&
		 t86r->cprbx.cprb_ver_id == 0x02) {
		switch (resp_type->type) {
		case PCIXCC_RESPONSE_TYPE_ICA:
			length = sizeof(struct type86x_reply)
				+ t86r->length - 2;
			length = min(PCIXCC_MAX_ICA_RESPONSE_SIZE, length);
			memcpy(msg->message, reply->message, length);
			break;
		case PCIXCC_RESPONSE_TYPE_XCRB:
			length = t86r->fmt2.offset2 + t86r->fmt2.count2;
			length = min(PCIXCC_MAX_XCRB_RESPONSE_SIZE, length);
			memcpy(msg->message, reply->message, length);
			break;
		default:
			memcpy(msg->message, &error_reply, sizeof error_reply);
		}
	} else
		memcpy(msg->message, reply->message, sizeof error_reply);
out:
	complete(&(resp_type->work));
}

static atomic_t zcrypt_step = ATOMIC_INIT(0);

/**
 * The request distributor calls this function if it picked the PCIXCC/CEX2C
 * device to handle a modexpo request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCIXCC/CEX2C device to the request distributor
 * @mex: pointer to the modexpo request buffer
 */
static long zcrypt_pcixcc_modexpo(struct zcrypt_device *zdev,
				  struct ica_rsa_modexpo *mex)
{
	struct ap_message ap_msg;
	struct response_type resp_type = {
		.type = PCIXCC_RESPONSE_TYPE_ICA,
	};
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &resp_type;
	rc = ICAMEX_msg_to_type6MEX_msgX(zdev, &ap_msg, mex);
	if (rc)
		goto out_free;
	init_completion(&resp_type.work);
	ap_queue_message(zdev->ap_dev, &ap_msg);
	rc = wait_for_completion_interruptible(&resp_type.work);
	if (rc == 0)
		rc = convert_response_ica(zdev, &ap_msg, mex->outputdata,
					  mex->outputdatalength);
	else
		/* Signal pending. */
		ap_cancel_message(zdev->ap_dev, &ap_msg);
out_free:
	free_page((unsigned long) ap_msg.message);
	return rc;
}

/**
 * The request distributor calls this function if it picked the PCIXCC/CEX2C
 * device to handle a modexpo_crt request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCIXCC/CEX2C device to the request distributor
 * @crt: pointer to the modexpoc_crt request buffer
 */
static long zcrypt_pcixcc_modexpo_crt(struct zcrypt_device *zdev,
				      struct ica_rsa_modexpo_crt *crt)
{
	struct ap_message ap_msg;
	struct response_type resp_type = {
		.type = PCIXCC_RESPONSE_TYPE_ICA,
	};
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &resp_type;
	rc = ICACRT_msg_to_type6CRT_msgX(zdev, &ap_msg, crt);
	if (rc)
		goto out_free;
	init_completion(&resp_type.work);
	ap_queue_message(zdev->ap_dev, &ap_msg);
	rc = wait_for_completion_interruptible(&resp_type.work);
	if (rc == 0)
		rc = convert_response_ica(zdev, &ap_msg, crt->outputdata,
					  crt->outputdatalength);
	else
		/* Signal pending. */
		ap_cancel_message(zdev->ap_dev, &ap_msg);
out_free:
	free_page((unsigned long) ap_msg.message);
	return rc;
}

/**
 * The request distributor calls this function if it picked the PCIXCC/CEX2C
 * device to handle a send_cprb request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCIXCC/CEX2C device to the request distributor
 * @xcRB: pointer to the send_cprb request buffer
 */
static long zcrypt_pcixcc_send_cprb(struct zcrypt_device *zdev,
				    struct ica_xcRB *xcRB)
{
	struct ap_message ap_msg;
	struct response_type resp_type = {
		.type = PCIXCC_RESPONSE_TYPE_XCRB,
	};
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = kmalloc(PCIXCC_MAX_XCRB_MESSAGE_SIZE, GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &resp_type;
	rc = XCRB_msg_to_type6CPRB_msgX(zdev, &ap_msg, xcRB);
	if (rc)
		goto out_free;
	init_completion(&resp_type.work);
	ap_queue_message(zdev->ap_dev, &ap_msg);
	rc = wait_for_completion_interruptible(&resp_type.work);
	if (rc == 0)
		rc = convert_response_xcrb(zdev, &ap_msg, xcRB);
	else
		/* Signal pending. */
		ap_cancel_message(zdev->ap_dev, &ap_msg);
out_free:
	kzfree(ap_msg.message);
	return rc;
}

/**
 * The request distributor calls this function if it picked the PCIXCC/CEX2C
 * device to generate random data.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCIXCC/CEX2C device to the request distributor
 * @buffer: pointer to a memory page to return random data
 */

static long zcrypt_pcixcc_rng(struct zcrypt_device *zdev,
				    char *buffer)
{
	struct ap_message ap_msg;
	struct response_type resp_type = {
		.type = PCIXCC_RESPONSE_TYPE_XCRB,
	};
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = kmalloc(PCIXCC_MAX_XCRB_MESSAGE_SIZE, GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &resp_type;
	rng_type6CPRB_msgX(zdev->ap_dev, &ap_msg, ZCRYPT_RNG_BUFFER_SIZE);
	init_completion(&resp_type.work);
	ap_queue_message(zdev->ap_dev, &ap_msg);
	rc = wait_for_completion_interruptible(&resp_type.work);
	if (rc == 0)
		rc = convert_response_rng(zdev, &ap_msg, buffer);
	else
		/* Signal pending. */
		ap_cancel_message(zdev->ap_dev, &ap_msg);
	kfree(ap_msg.message);
	return rc;
}

/**
 * The crypto operations for a PCIXCC/CEX2C card.
 */
static struct zcrypt_ops zcrypt_pcixcc_ops = {
	.rsa_modexpo = zcrypt_pcixcc_modexpo,
	.rsa_modexpo_crt = zcrypt_pcixcc_modexpo_crt,
	.send_cprb = zcrypt_pcixcc_send_cprb,
};

static struct zcrypt_ops zcrypt_pcixcc_with_rng_ops = {
	.rsa_modexpo = zcrypt_pcixcc_modexpo,
	.rsa_modexpo_crt = zcrypt_pcixcc_modexpo_crt,
	.send_cprb = zcrypt_pcixcc_send_cprb,
	.rng = zcrypt_pcixcc_rng,
};

/**
 * Micro-code detection function. Its sends a message to a pcixcc card
 * to find out the microcode level.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_pcixcc_mcl(struct ap_device *ap_dev)
{
	static unsigned char msg[] = {
		0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x58,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x43,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x50,0x4B,0x00,0x00,
		0x00,0x00,0x01,0xC4,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x07,0x24,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0xDC,0x02,0x00,0x00,0x00,0x54,0x32,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xE8,
		0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x24,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x50,0x4B,0x00,0x0A,
		0x4D,0x52,0x50,0x20,0x20,0x20,0x20,0x20,
		0x00,0x42,0x00,0x01,0x02,0x03,0x04,0x05,
		0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,
		0x0E,0x0F,0x00,0x11,0x22,0x33,0x44,0x55,
		0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,
		0xEE,0xFF,0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,
		0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,
		0x11,0x00,0x01,0x23,0x45,0x67,0x89,0xAB,
		0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,
		0x32,0x10,0x00,0x9A,0x00,0x98,0x00,0x00,
		0x1E,0x00,0x00,0x94,0x00,0x00,0x00,0x00,
		0x04,0x00,0x00,0x8C,0x00,0x00,0x00,0x40,
		0x02,0x00,0x00,0x40,0xBA,0xE8,0x23,0x3C,
		0x75,0xF3,0x91,0x61,0xD6,0x73,0x39,0xCF,
		0x7B,0x6D,0x8E,0x61,0x97,0x63,0x9E,0xD9,
		0x60,0x55,0xD6,0xC7,0xEF,0xF8,0x1E,0x63,
		0x95,0x17,0xCC,0x28,0x45,0x60,0x11,0xC5,
		0xC4,0x4E,0x66,0xC6,0xE6,0xC3,0xDE,0x8A,
		0x19,0x30,0xCF,0x0E,0xD7,0xAA,0xDB,0x01,
		0xD8,0x00,0xBB,0x8F,0x39,0x9F,0x64,0x28,
		0xF5,0x7A,0x77,0x49,0xCC,0x6B,0xA3,0x91,
		0x97,0x70,0xE7,0x60,0x1E,0x39,0xE1,0xE5,
		0x33,0xE1,0x15,0x63,0x69,0x08,0x80,0x4C,
		0x67,0xC4,0x41,0x8F,0x48,0xDF,0x26,0x98,
		0xF1,0xD5,0x8D,0x88,0xD9,0x6A,0xA4,0x96,
		0xC5,0x84,0xD9,0x30,0x49,0x67,0x7D,0x19,
		0xB1,0xB3,0x45,0x4D,0xB2,0x53,0x9A,0x47,
		0x3C,0x7C,0x55,0xBF,0xCC,0x85,0x00,0x36,
		0xF1,0x3D,0x93,0x53
	};
	unsigned long long psmid;
	struct CPRBX *cprbx;
	char *reply;
	int rc, i;

	reply = (void *) get_zeroed_page(GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	rc = ap_send(ap_dev->qid, 0x0102030405060708ULL, msg, sizeof(msg));
	if (rc)
		goto out_free;

	/* Wait for the test message to complete. */
	for (i = 0; i < 6; i++) {
		mdelay(300);
		rc = ap_recv(ap_dev->qid, &psmid, reply, 4096);
		if (rc == 0 && psmid == 0x0102030405060708ULL)
			break;
	}

	if (i >= 6) {
		/* Got no answer. */
		rc = -ENODEV;
		goto out_free;
	}

	cprbx = (struct CPRBX *) (reply + 48);
	if (cprbx->ccp_rtcode == 8 && cprbx->ccp_rscode == 33)
		rc = ZCRYPT_PCIXCC_MCL2;
	else
		rc = ZCRYPT_PCIXCC_MCL3;
out_free:
	free_page((unsigned long) reply);
	return rc;
}

/**
 * Large random number detection function. Its sends a message to a pcixcc
 * card to find out if large random numbers are supported.
 * @ap_dev: pointer to the AP device.
 *
 * Returns 1 if large random numbers are supported, 0 if not and < 0 on error.
 */
static int zcrypt_pcixcc_rng_supported(struct ap_device *ap_dev)
{
	struct ap_message ap_msg;
	unsigned long long psmid;
	struct {
		struct type86_hdr hdr;
		struct type86_fmt2_ext fmt2;
		struct CPRBX cprbx;
	} __attribute__((packed)) *reply;
	int rc, i;

	ap_init_message(&ap_msg);
	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;

	rng_type6CPRB_msgX(ap_dev, &ap_msg, 4);
	rc = ap_send(ap_dev->qid, 0x0102030405060708ULL, ap_msg.message,
		     ap_msg.length);
	if (rc)
		goto out_free;

	/* Wait for the test message to complete. */
	for (i = 0; i < 2 * HZ; i++) {
		msleep(1000 / HZ);
		rc = ap_recv(ap_dev->qid, &psmid, ap_msg.message, 4096);
		if (rc == 0 && psmid == 0x0102030405060708ULL)
			break;
	}

	if (i >= 2 * HZ) {
		/* Got no answer. */
		rc = -ENODEV;
		goto out_free;
	}

	reply = ap_msg.message;
	if (reply->cprbx.ccp_rtcode == 0 && reply->cprbx.ccp_rscode == 0)
		rc = 1;
	else
		rc = 0;
out_free:
	free_page((unsigned long) ap_msg.message);
	return rc;
}

/**
 * Probe function for PCIXCC/CEX2C cards. It always accepts the AP device
 * since the bus_match already checked the hardware type. The PCIXCC
 * cards come in two flavours: micro code level 2 and micro code level 3.
 * This is checked by sending a test message to the device.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_pcixcc_probe(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev;
	int rc = 0;

	zdev = zcrypt_device_alloc(PCIXCC_MAX_RESPONSE_SIZE);
	if (!zdev)
		return -ENOMEM;
	zdev->ap_dev = ap_dev;
	zdev->online = 1;
	switch (ap_dev->device_type) {
	case AP_DEVICE_TYPE_PCIXCC:
		rc = zcrypt_pcixcc_mcl(ap_dev);
		if (rc < 0) {
			zcrypt_device_free(zdev);
			return rc;
		}
		zdev->user_space_type = rc;
		if (rc == ZCRYPT_PCIXCC_MCL2) {
			zdev->type_string = "PCIXCC_MCL2";
			zdev->speed_rating = PCIXCC_MCL2_SPEED_RATING;
			zdev->min_mod_size = PCIXCC_MIN_MOD_SIZE_OLD;
			zdev->max_mod_size = PCIXCC_MAX_MOD_SIZE;
			zdev->max_exp_bit_length = PCIXCC_MAX_MOD_SIZE;
		} else {
			zdev->type_string = "PCIXCC_MCL3";
			zdev->speed_rating = PCIXCC_MCL3_SPEED_RATING;
			zdev->min_mod_size = PCIXCC_MIN_MOD_SIZE;
			zdev->max_mod_size = PCIXCC_MAX_MOD_SIZE;
			zdev->max_exp_bit_length = PCIXCC_MAX_MOD_SIZE;
		}
		break;
	case AP_DEVICE_TYPE_CEX2C:
		zdev->user_space_type = ZCRYPT_CEX2C;
		zdev->type_string = "CEX2C";
		zdev->speed_rating = CEX2C_SPEED_RATING;
		zdev->min_mod_size = PCIXCC_MIN_MOD_SIZE;
		zdev->max_mod_size = PCIXCC_MAX_MOD_SIZE;
		zdev->max_exp_bit_length = PCIXCC_MAX_MOD_SIZE;
		break;
	case AP_DEVICE_TYPE_CEX3C:
		zdev->user_space_type = ZCRYPT_CEX3C;
		zdev->type_string = "CEX3C";
		zdev->speed_rating = CEX3C_SPEED_RATING;
		zdev->min_mod_size = CEX3C_MIN_MOD_SIZE;
		zdev->max_mod_size = CEX3C_MAX_MOD_SIZE;
		zdev->max_exp_bit_length = CEX3C_MAX_MOD_SIZE;
		break;
	default:
		goto out_free;
	}

	rc = zcrypt_pcixcc_rng_supported(ap_dev);
	if (rc < 0) {
		zcrypt_device_free(zdev);
		return rc;
	}
	if (rc)
		zdev->ops = &zcrypt_pcixcc_with_rng_ops;
	else
		zdev->ops = &zcrypt_pcixcc_ops;
	ap_dev->reply = &zdev->reply;
	ap_dev->private = zdev;
	rc = zcrypt_device_register(zdev);
	if (rc)
		goto out_free;
	return 0;

 out_free:
	ap_dev->private = NULL;
	zcrypt_device_free(zdev);
	return rc;
}

/**
 * This is called to remove the extended PCIXCC/CEX2C driver information
 * if an AP device is removed.
 */
static void zcrypt_pcixcc_remove(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = ap_dev->private;

	zcrypt_device_unregister(zdev);
}

int __init zcrypt_pcixcc_init(void)
{
	return ap_driver_register(&zcrypt_pcixcc_driver, THIS_MODULE, "pcixcc");
}

void zcrypt_pcixcc_exit(void)
{
	ap_driver_unregister(&zcrypt_pcixcc_driver);
}

#ifndef CONFIG_ZCRYPT_MONOLITHIC
module_init(zcrypt_pcixcc_init);
module_exit(zcrypt_pcixcc_exit);
#endif
