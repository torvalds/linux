/*
 *  linux/drivers/s390/crypto/zcrypt_pcica.c
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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/atomic.h>
#include <asm/uaccess.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_pcica.h"

#define PCICA_MIN_MOD_SIZE	  1	/*    8 bits	*/
#define PCICA_MAX_MOD_SIZE	256	/* 2048 bits	*/

#define PCICA_SPEED_RATING	2800

#define PCICA_MAX_MESSAGE_SIZE	0x3a0	/* sizeof(struct type4_lcr)	     */
#define PCICA_MAX_RESPONSE_SIZE 0x110	/* max outputdatalength + type80_hdr */

#define PCICA_CLEANUP_TIME	(15*HZ)

static struct ap_device_id zcrypt_pcica_ids[] = {
	{ AP_DEVICE(AP_DEVICE_TYPE_PCICA) },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_pcica_ids);
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("PCICA Cryptographic Coprocessor device driver, "
		   "Copyright 2001, 2006 IBM Corporation");
MODULE_LICENSE("GPL");

static int zcrypt_pcica_probe(struct ap_device *ap_dev);
static void zcrypt_pcica_remove(struct ap_device *ap_dev);
static void zcrypt_pcica_receive(struct ap_device *, struct ap_message *,
				 struct ap_message *);

static struct ap_driver zcrypt_pcica_driver = {
	.probe = zcrypt_pcica_probe,
	.remove = zcrypt_pcica_remove,
	.ids = zcrypt_pcica_ids,
	.request_timeout = PCICA_CLEANUP_TIME,
};

/**
 * Convert a ICAMEX message to a type4 MEX message.
 *
 * @zdev: crypto device pointer
 * @zreq: crypto request pointer
 * @mex: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICAMEX_msg_to_type4MEX_msg(struct zcrypt_device *zdev,
				      struct ap_message *ap_msg,
				      struct ica_rsa_modexpo *mex)
{
	unsigned char *modulus, *exponent, *message;
	int mod_len;

	mod_len = mex->inputdatalength;

	if (mod_len <= 128) {
		struct type4_sme *sme = ap_msg->message;
		memset(sme, 0, sizeof(*sme));
		ap_msg->length = sizeof(*sme);
		sme->header.msg_fmt = TYPE4_SME_FMT;
		sme->header.msg_len = sizeof(*sme);
		sme->header.msg_type_code = TYPE4_TYPE_CODE;
		sme->header.request_code = TYPE4_REQU_CODE;
		modulus = sme->modulus + sizeof(sme->modulus) - mod_len;
		exponent = sme->exponent + sizeof(sme->exponent) - mod_len;
		message = sme->message + sizeof(sme->message) - mod_len;
	} else {
		struct type4_lme *lme = ap_msg->message;
		memset(lme, 0, sizeof(*lme));
		ap_msg->length = sizeof(*lme);
		lme->header.msg_fmt = TYPE4_LME_FMT;
		lme->header.msg_len = sizeof(*lme);
		lme->header.msg_type_code = TYPE4_TYPE_CODE;
		lme->header.request_code = TYPE4_REQU_CODE;
		modulus = lme->modulus + sizeof(lme->modulus) - mod_len;
		exponent = lme->exponent + sizeof(lme->exponent) - mod_len;
		message = lme->message + sizeof(lme->message) - mod_len;
	}

	if (copy_from_user(modulus, mex->n_modulus, mod_len) ||
	    copy_from_user(exponent, mex->b_key, mod_len) ||
	    copy_from_user(message, mex->inputdata, mod_len))
		return -EFAULT;
	return 0;
}

/**
 * Convert a ICACRT message to a type4 CRT message.
 *
 * @zdev: crypto device pointer
 * @zreq: crypto request pointer
 * @crt: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICACRT_msg_to_type4CRT_msg(struct zcrypt_device *zdev,
				      struct ap_message *ap_msg,
				      struct ica_rsa_modexpo_crt *crt)
{
	unsigned char *p, *q, *dp, *dq, *u, *inp;
	int mod_len, short_len, long_len;

	mod_len = crt->inputdatalength;
	short_len = mod_len / 2;
	long_len = mod_len / 2 + 8;

	if (mod_len <= 128) {
		struct type4_scr *scr = ap_msg->message;
		memset(scr, 0, sizeof(*scr));
		ap_msg->length = sizeof(*scr);
		scr->header.msg_type_code = TYPE4_TYPE_CODE;
		scr->header.request_code = TYPE4_REQU_CODE;
		scr->header.msg_fmt = TYPE4_SCR_FMT;
		scr->header.msg_len = sizeof(*scr);
		p = scr->p + sizeof(scr->p) - long_len;
		q = scr->q + sizeof(scr->q) - short_len;
		dp = scr->dp + sizeof(scr->dp) - long_len;
		dq = scr->dq + sizeof(scr->dq) - short_len;
		u = scr->u + sizeof(scr->u) - long_len;
		inp = scr->message + sizeof(scr->message) - mod_len;
	} else {
		struct type4_lcr *lcr = ap_msg->message;
		memset(lcr, 0, sizeof(*lcr));
		ap_msg->length = sizeof(*lcr);
		lcr->header.msg_type_code = TYPE4_TYPE_CODE;
		lcr->header.request_code = TYPE4_REQU_CODE;
		lcr->header.msg_fmt = TYPE4_LCR_FMT;
		lcr->header.msg_len = sizeof(*lcr);
		p = lcr->p + sizeof(lcr->p) - long_len;
		q = lcr->q + sizeof(lcr->q) - short_len;
		dp = lcr->dp + sizeof(lcr->dp) - long_len;
		dq = lcr->dq + sizeof(lcr->dq) - short_len;
		u = lcr->u + sizeof(lcr->u) - long_len;
		inp = lcr->message + sizeof(lcr->message) - mod_len;
	}

	if (copy_from_user(p, crt->np_prime, long_len) ||
	    copy_from_user(q, crt->nq_prime, short_len) ||
	    copy_from_user(dp, crt->bp_key, long_len) ||
	    copy_from_user(dq, crt->bq_key, short_len) ||
	    copy_from_user(u, crt->u_mult_inv, long_len) ||
	    copy_from_user(inp, crt->inputdata, mod_len))
		return -EFAULT;
	return 0;
}

/**
 * Copy results from a type 84 reply message back to user space.
 *
 * @zdev: crypto device pointer
 * @reply: reply AP message.
 * @data: pointer to user output data
 * @length: size of user output data
 *
 * Returns 0 on success or -EFAULT.
 */
static int convert_type84(struct zcrypt_device *zdev,
			  struct ap_message *reply,
			  char __user *outputdata,
			  unsigned int outputdatalength)
{
	struct type84_hdr *t84h = reply->message;
	char *data;

	if (t84h->len < sizeof(*t84h) + outputdatalength) {
		/* The result is too short, the PCICA card may not do that.. */
		zdev->online = 0;
		return -EAGAIN;	/* repeat the request on a different device. */
	}
	BUG_ON(t84h->len > PCICA_MAX_RESPONSE_SIZE);
	data = reply->message + t84h->len - outputdatalength;
	if (copy_to_user(outputdata, data, outputdatalength))
		return -EFAULT;
	return 0;
}

static int convert_response(struct zcrypt_device *zdev,
			    struct ap_message *reply,
			    char __user *outputdata,
			    unsigned int outputdatalength)
{
	/* Response type byte is the second byte in the response. */
	switch (((unsigned char *) reply->message)[1]) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		return convert_error(zdev, reply);
	case TYPE84_RSP_CODE:
		return convert_type84(zdev, reply,
				      outputdata, outputdatalength);
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
static void zcrypt_pcica_receive(struct ap_device *ap_dev,
				 struct ap_message *msg,
				 struct ap_message *reply)
{
	static struct error_hdr error_reply = {
		.type = TYPE82_RSP_CODE,
		.reply_code = REP82_ERROR_MACHINE_FAILURE,
	};
	struct type84_hdr *t84h;
	int length;

	/* Copy the reply message to the request message buffer. */
	if (IS_ERR(reply)) {
		memcpy(msg->message, &error_reply, sizeof(error_reply));
		goto out;
	}
	t84h = reply->message;
	if (t84h->code == TYPE84_RSP_CODE) {
		length = min(PCICA_MAX_RESPONSE_SIZE, (int) t84h->len);
		memcpy(msg->message, reply->message, length);
	} else
		memcpy(msg->message, reply->message, sizeof error_reply);
out:
	complete((struct completion *) msg->private);
}

static atomic_t zcrypt_step = ATOMIC_INIT(0);

/**
 * The request distributor calls this function if it picked the PCICA
 * device to handle a modexpo request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCICA device to the request distributor
 * @mex: pointer to the modexpo request buffer
 */
static long zcrypt_pcica_modexpo(struct zcrypt_device *zdev,
				 struct ica_rsa_modexpo *mex)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = kmalloc(PCICA_MAX_MESSAGE_SIZE, GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.receive = zcrypt_pcica_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICAMEX_msg_to_type4MEX_msg(zdev, &ap_msg, mex);
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
	kfree(ap_msg.message);
	return rc;
}

/**
 * The request distributor calls this function if it picked the PCICA
 * device to handle a modexpo_crt request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  PCICA device to the request distributor
 * @crt: pointer to the modexpoc_crt request buffer
 */
static long zcrypt_pcica_modexpo_crt(struct zcrypt_device *zdev,
				     struct ica_rsa_modexpo_crt *crt)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_init_message(&ap_msg);
	ap_msg.message = kmalloc(PCICA_MAX_MESSAGE_SIZE, GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.receive = zcrypt_pcica_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICACRT_msg_to_type4CRT_msg(zdev, &ap_msg, crt);
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
	kfree(ap_msg.message);
	return rc;
}

/**
 * The crypto operations for a PCICA card.
 */
static struct zcrypt_ops zcrypt_pcica_ops = {
	.rsa_modexpo = zcrypt_pcica_modexpo,
	.rsa_modexpo_crt = zcrypt_pcica_modexpo_crt,
};

/**
 * Probe function for PCICA cards. It always accepts the AP device
 * since the bus_match already checked the hardware type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_pcica_probe(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev;
	int rc;

	zdev = zcrypt_device_alloc(PCICA_MAX_RESPONSE_SIZE);
	if (!zdev)
		return -ENOMEM;
	zdev->ap_dev = ap_dev;
	zdev->ops = &zcrypt_pcica_ops;
	zdev->online = 1;
	zdev->user_space_type = ZCRYPT_PCICA;
	zdev->type_string = "PCICA";
	zdev->min_mod_size = PCICA_MIN_MOD_SIZE;
	zdev->max_mod_size = PCICA_MAX_MOD_SIZE;
	zdev->speed_rating = PCICA_SPEED_RATING;
	zdev->max_exp_bit_length = PCICA_MAX_MOD_SIZE;
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
 * This is called to remove the extended PCICA driver information
 * if an AP device is removed.
 */
static void zcrypt_pcica_remove(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = ap_dev->private;

	zcrypt_device_unregister(zdev);
}

int __init zcrypt_pcica_init(void)
{
	return ap_driver_register(&zcrypt_pcica_driver, THIS_MODULE, "pcica");
}

void zcrypt_pcica_exit(void)
{
	ap_driver_unregister(&zcrypt_pcica_driver);
}

module_init(zcrypt_pcica_init);
module_exit(zcrypt_pcica_exit);
