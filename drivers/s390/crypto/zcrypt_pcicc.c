/*
 *  linux/drivers/s390/crypto/zcrypt_pcicc.c
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
#include <asm/atomic.h>
#include <asm/uaccess.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_pcicc.h"
#include "zcrypt_cca_key.h"

#define PCICC_MIN_MOD_SIZE	 64	/*  512 bits */
#define PCICC_MAX_MOD_SIZE_OLD	128	/* 1024 bits */
#define PCICC_MAX_MOD_SIZE	256	/* 2048 bits */

/*
 * PCICC cards need a speed rating of 0. This keeps them at the end of
 * the zcrypt device list (see zcrypt_api.c). PCICC cards are only
 * used if no other cards are present because they are slow and can only
 * cope with PKCS12 padded requests. The logic is queer. PKCS11 padded
 * requests are rejected. The modexpo function encrypts PKCS12 padded data
 * and decrypts any non-PKCS12 padded data (except PKCS11) in the assumption
 * that it's encrypted PKCS12 data. The modexpo_crt function always decrypts
 * the data in the assumption that its PKCS12 encrypted data.
 */
#define PCICC_SPEED_RATING	0

#define PCICC_MAX_MESSAGE_SIZE 0x710	/* max size type6 v1 crt message */
#define PCICC_MAX_RESPONSE_SIZE 0x710	/* max size type86 v1 reply	 */

#define PCICC_CLEANUP_TIME	(15*HZ)

static struct ap_device_id zcrypt_pcicc_ids[] = {
	{ AP_DEVICE(AP_DEVICE_TYPE_PCICC) },
	{ /* end of list */ },
};

#ifndef CONFIG_ZCRYPT_MONOLITHIC
MODULE_DEVICE_TABLE(ap, zcrypt_pcicc_ids);
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("PCICC Cryptographic Coprocessor device driver, "
		   "Copyright 2001, 2006 IBM Corporation");
MODULE_LICENSE("GPL");
#endif

static int zcrypt_pcicc_probe(struct ap_device *ap_dev);
static void zcrypt_pcicc_remove(struct ap_device *ap_dev);
static void zcrypt_pcicc_receive(struct ap_device *, struct ap_message *,
				 struct ap_message *);

static struct ap_driver zcrypt_pcicc_driver = {
	.probe = zcrypt_pcicc_probe,
	.remove = zcrypt_pcicc_remove,
	.receive = zcrypt_pcicc_receive,
	.ids = zcrypt_pcicc_ids,
	.request_timeout = PCICC_CLEANUP_TIME,
};

/**
 * The following is used to initialize the CPRB passed to the PCICC card
 * in a type6 message. The 3 fields that must be filled in at execution
 * time are  req_parml, rpl_parml and usage_domain. Note that all three
 * fields are *little*-endian. Actually, everything about this interface
 * is ascii/little-endian, since the device has 'Intel inside'.
 *
 * The CPRB is followed immediately by the parm block.
 * The parm block contains:
 * - function code ('PD' 0x5044 or 'PK' 0x504B)
 * - rule block (0x0A00 'PKCS-1.2' or 0x0A00 'ZERO-PAD')
 * - VUD block
 */
static struct CPRB static_cprb = {
	.cprb_len	= __constant_cpu_to_le16(0x0070),
	.cprb_ver_id	=  0x41,
	.func_id	= {0x54,0x32},
	.checkpoint_flag=  0x01,
	.svr_namel	= __constant_cpu_to_le16(0x0008),
	.svr_name	= {'I','C','S','F',' ',' ',' ',' '}
};

/**
 * Check the message for PKCS11 padding.
 */
static inline int is_PKCS11_padded(unsigned char *buffer, int length)
{
	int i;
	if ((buffer[0] != 0x00) || (buffer[1] != 0x01))
		return 0;
	for (i = 2; i < length; i++)
		if (buffer[i] != 0xFF)
			break;
	if (i < 10 || i == length)
		return 0;
	if (buffer[i] != 0x00)
		return 0;
	return 1;
}

/**
 * Check the message for PKCS12 padding.
 */
static inline int is_PKCS12_padded(unsigned char *buffer, int length)
{
	int i;
	if ((buffer[0] != 0x00) || (buffer[1] != 0x02))
		return 0;
	for (i = 2; i < length; i++)
		if (buffer[i] == 0x00)
			break;
	if ((i < 10) || (i == length))
		return 0;
	if (buffer[i] != 0x00)
		return 0;
	return 1;
}

/**
 * Convert a ICAMEX message to a type6 MEX message.
 *
 * @zdev: crypto device pointer
 * @zreq: crypto request pointer
 * @mex: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICAMEX_msg_to_type6MEX_msg(struct zcrypt_device *zdev,
				      struct ap_message *ap_msg,
				      struct ica_rsa_modexpo *mex)
{
	static struct type6_hdr static_type6_hdr = {
		.type		=  0x06,
		.offset1	=  0x00000058,
		.agent_id	= {0x01,0x00,0x43,0x43,0x41,0x2D,0x41,0x50,
				   0x50,0x4C,0x20,0x20,0x20,0x01,0x01,0x01},
		.function_code	= {'P','K'},
	};
	static struct function_and_rules_block static_pke_function_and_rules ={
		.function_code	= {'P','K'},
		.ulen		= __constant_cpu_to_le16(10),
		.only_rule	= {'P','K','C','S','-','1','.','2'}
	};
	struct {
		struct type6_hdr hdr;
		struct CPRB cprb;
		struct function_and_rules_block fr;
		unsigned short length;
		char text[0];
	} __attribute__((packed)) *msg = ap_msg->message;
	int vud_len, pad_len, size;

	/* VUD.ciphertext */
	if (copy_from_user(msg->text, mex->inputdata, mex->inputdatalength))
		return -EFAULT;

	if (is_PKCS11_padded(msg->text, mex->inputdatalength))
		return -EINVAL;

	/* static message header and f&r */
	msg->hdr = static_type6_hdr;
	msg->fr = static_pke_function_and_rules;

	if (is_PKCS12_padded(msg->text, mex->inputdatalength)) {
		/* strip the padding and adjust the data length */
		pad_len = strnlen(msg->text + 2, mex->inputdatalength - 2) + 3;
		if (pad_len <= 9 || pad_len >= mex->inputdatalength)
			return -ENODEV;
		vud_len = mex->inputdatalength - pad_len;
		memmove(msg->text, msg->text + pad_len, vud_len);
		msg->length = cpu_to_le16(vud_len + 2);

		/* Set up key after the variable length text. */
		size = zcrypt_type6_mex_key_en(mex, msg->text + vud_len, 0);
		if (size < 0)
			return size;
		size += sizeof(*msg) + vud_len;	/* total size of msg */
	} else {
		vud_len = mex->inputdatalength;
		msg->length = cpu_to_le16(2 + vud_len);

		msg->hdr.function_code[1] = 'D';
		msg->fr.function_code[1] = 'D';

		/* Set up key after the variable length text. */
		size = zcrypt_type6_mex_key_de(mex, msg->text + vud_len, 0);
		if (size < 0)
			return size;
		size += sizeof(*msg) + vud_len;	/* total size of msg */
	}

	/* message header, cprb and f&r */
	msg->hdr.ToCardLen1 = (size - sizeof(msg->hdr) + 3) & -4;
	msg->hdr.FromCardLen1 = PCICC_MAX_RESPONSE_SIZE - sizeof(msg->hdr);

	msg->cprb = static_cprb;
	msg->cprb.usage_domain[0]= AP_QID_QUEUE(zdev->ap_dev->qid);
	msg->cprb.req_parml = cpu_to_le16(size - sizeof(msg->hdr) -
					   sizeof(msg->cprb));
	msg->cprb.rpl_parml = cpu_to_le16(msg->hdr.FromCardLen1);

	ap_msg->length = (size + 3) & -4;
	return 0;
}

/**
 * Convert a ICACRT message to a type6 CRT message.
 *
 * @zdev: crypto device pointer
 * @zreq: crypto request pointer
 * @crt: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICACRT_msg_to_type6CRT_msg(struct zcrypt_device *zdev,
				      struct ap_message *ap_msg,
				      struct ica_rsa_modexpo_crt *crt)
{
	static struct type6_hdr static_type6_hdr = {
		.type		=  0x06,
		.offset1	=  0x00000058,
		.agent_id	= {0x01,0x00,0x43,0x43,0x41,0x2D,0x41,0x50,
				   0x50,0x4C,0x20,0x20,0x20,0x01,0x01,0x01},
		.function_code	= {'P','D'},
	};
	static struct function_and_rules_block static_pkd_function_and_rules ={
		.function_code	= {'P','D'},
		.ulen		= __constant_cpu_to_le16(10),
		.only_rule	= {'P','K','C','S','-','1','.','2'}
	};
	struct {
		struct type6_hdr hdr;
		struct CPRB cprb;
		struct function_and_rules_block fr;
		unsigned short length;
		char text[0];
	} __attribute__((packed)) *msg = ap_msg->message;
	int size;

	/* VUD.ciphertext */
	msg->length = cpu_to_le16(2 + crt->inputdatalength);
	if (copy_from_user(msg->text, crt->inputdata, crt->inputdatalength))
		return -EFAULT;

	if (is_PKCS11_padded(msg->text, crt->inputdatalength))
		return -EINVAL;

	/* Set up key after the variable length text. */
	size = zcrypt_type6_crt_key(crt, msg->text + crt->inputdatalength, 0);
	if (size < 0)
		return size;
	size += sizeof(*msg) + crt->inputdatalength;	/* total size of msg */

	/* message header, cprb and f&r */
	msg->hdr = static_type6_hdr;
	msg->hdr.ToCardLen1 = (size -  sizeof(msg->hdr) + 3) & -4;
	msg->hdr.FromCardLen1 = PCICC_MAX_RESPONSE_SIZE - sizeof(msg->hdr);

	msg->cprb = static_cprb;
	msg->cprb.usage_domain[0] = AP_QID_QUEUE(zdev->ap_dev->qid);
	msg->cprb.req_parml = msg->cprb.rpl_parml =
		cpu_to_le16(size - sizeof(msg->hdr) - sizeof(msg->cprb));

	msg->fr = static_pkd_function_and_rules;

	ap_msg->length = (size + 3) & -4;
	return 0;
}

/**
 * Copy results from a type 86 reply message back to user space.
 *
 * @zdev: crypto device pointer
 * @reply: reply AP message.
 * @data: pointer to user output data
 * @length: size of user output data
 *
 * Returns 0 on success or -EINVAL, -EFAULT, -EAGAIN in case of an error.
 */
struct type86_reply {
	struct type86_hdr hdr;
	struct type86_fmt2_ext fmt2;
	struct CPRB cprb;
	unsigned char pad[4];	/* 4 byte function code/rules block ? */
	unsigned short length;
	char text[0];
} __attribute__((packed));

static int convert_type86(struct zcrypt_device *zdev,
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
	struct type86_reply *msg = reply->message;
	unsigned short service_rc, service_rs;
	unsigned int reply_len, pad_len;
	char *data;

	service_rc = le16_to_cpu(msg->cprb.ccp_rtcode);
	if (unlikely(service_rc != 0)) {
		service_rs = le16_to_cpu(msg->cprb.ccp_rscode);
		if (service_rc == 8 && service_rs == 66)
			return -EINVAL;
		if (service_rc == 8 && service_rs == 65)
			return -EINVAL;
		if (service_rc == 8 && service_rs == 770) {
			zdev->max_mod_size = PCICC_MAX_MOD_SIZE_OLD;
			return -EAGAIN;
		}
		if (service_rc == 8 && service_rs == 783) {
			zdev->max_mod_size = PCICC_MAX_MOD_SIZE_OLD;
			return -EAGAIN;
		}
		if (service_rc == 8 && service_rs == 72)
			return -EINVAL;
		zdev->online = 0;
		return -EAGAIN;	/* repeat the request on a different device. */
	}
	data = msg->text;
	reply_len = le16_to_cpu(msg->length) - 2;
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

static int convert_response(struct zcrypt_device *zdev,
			    struct ap_message *reply,
			    char __user *outputdata,
			    unsigned int outputdatalength)
{
	struct type86_reply *msg = reply->message;

	/* Response type byte is the second byte in the response. */
	switch (msg->hdr.type) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		return convert_error(zdev, reply);
	case TYPE86_RSP_CODE:
		if (msg->hdr.reply_code)
			return convert_error(zdev, reply);
		if (msg->cprb.cprb_ver_id == 0x01)
			return convert_type86(zdev, reply,
					      outputdata, outputdatalength);
		/* no break, incorrect cprb version is an unknown response */
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
static void zcrypt_pcicc_receive(struct ap_device *ap_dev,
				 struct ap_message *msg,
				 struct ap_message *reply)
{
	static struct error_hdr error_reply = {
		.type = TYPE82_RSP_CODE,
		.reply_code = REP82_ERROR_MACHINE_FAILURE,
	};
	struct type86_reply *t86r;
	int length;

	/* Copy the reply message to the request message buffer. */
	if (IS_ERR(reply)) {
		memcpy(msg->message, &error_reply, sizeof(error_reply));
		goto out;
	}
	t86r = reply->message;
	if (t86r->hdr.type == TYPE86_RSP_CODE &&
		 t86r->cprb.cprb_ver_id == 0x01) {
		length = sizeof(struct type86_reply) + t86r->length - 2;
		length = min(PCICC_MAX_RESPONSE_SIZE, length);
		memcpy(msg->message, reply->message, length);
	} else
		memcpy(msg->message, reply->message, sizeof error_reply);
out:
	complete((struct completion *) msg->private);
}

static atomic_t zcrypt_step = ATOMIC_INIT(0);

/**
 * The request distributor calls this function if it picked the PCICC
 * device to handle a modexpo request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCICC device to the request distributor
 * @mex: pointer to the modexpo request buffer
 */
static long zcrypt_pcicc_modexpo(struct zcrypt_device *zdev,
				 struct ica_rsa_modexpo *mex)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.length = PAGE_SIZE;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICAMEX_msg_to_type6MEX_msg(zdev, &ap_msg, mex);
	if (rc)
		goto out_free;
	init_completion(&work);
	ap_queue_message(zdev->ap_dev, &ap_msg);
	rc = wait_for_completion_interruptible(&work);
	if (rc == 0)
		rc = convert_response(zdev, &ap_msg, mex->outputdata,
				      mex->outputdatalength);
	else
		/* Signal pending. */
		ap_cancel_message(zdev->ap_dev, &ap_msg);
out_free:
	free_page((unsigned long) ap_msg.message);
	return rc;
}

/**
 * The request distributor calls this function if it picked the PCICC
 * device to handle a modexpo_crt request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCICC device to the request distributor
 * @crt: pointer to the modexpoc_crt request buffer
 */
static long zcrypt_pcicc_modexpo_crt(struct zcrypt_device *zdev,
				     struct ica_rsa_modexpo_crt *crt)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.length = PAGE_SIZE;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICACRT_msg_to_type6CRT_msg(zdev, &ap_msg, crt);
	if (rc)
		goto out_free;
	init_completion(&work);
	ap_queue_message(zdev->ap_dev, &ap_msg);
	rc = wait_for_completion_interruptible(&work);
	if (rc == 0)
		rc = convert_response(zdev, &ap_msg, crt->outputdata,
				      crt->outputdatalength);
	else
		/* Signal pending. */
		ap_cancel_message(zdev->ap_dev, &ap_msg);
out_free:
	free_page((unsigned long) ap_msg.message);
	return rc;
}

/**
 * The crypto operations for a PCICC card.
 */
static struct zcrypt_ops zcrypt_pcicc_ops = {
	.rsa_modexpo = zcrypt_pcicc_modexpo,
	.rsa_modexpo_crt = zcrypt_pcicc_modexpo_crt,
};

/**
 * Probe function for PCICC cards. It always accepts the AP device
 * since the bus_match already checked the hardware type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_pcicc_probe(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev;
	int rc;

	zdev = zcrypt_device_alloc(PCICC_MAX_RESPONSE_SIZE);
	if (!zdev)
		return -ENOMEM;
	zdev->ap_dev = ap_dev;
	zdev->ops = &zcrypt_pcicc_ops;
	zdev->online = 1;
	zdev->user_space_type = ZCRYPT_PCICC;
	zdev->type_string = "PCICC";
	zdev->min_mod_size = PCICC_MIN_MOD_SIZE;
	zdev->max_mod_size = PCICC_MAX_MOD_SIZE;
	zdev->speed_rating = PCICC_SPEED_RATING;
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
 * This is called to remove the extended PCICC driver information
 * if an AP device is removed.
 */
static void zcrypt_pcicc_remove(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = ap_dev->private;

	zcrypt_device_unregister(zdev);
}

int __init zcrypt_pcicc_init(void)
{
	return ap_driver_register(&zcrypt_pcicc_driver, THIS_MODULE, "pcicc");
}

void zcrypt_pcicc_exit(void)
{
	ap_driver_unregister(&zcrypt_pcicc_driver);
}

#ifndef CONFIG_ZCRYPT_MONOLITHIC
module_init(zcrypt_pcicc_init);
module_exit(zcrypt_pcicc_exit);
#endif
