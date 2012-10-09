/*
 *  zcrypt 2.1.0
 *
 *  Copyright IBM Corp. 2001, 2006
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
#include "zcrypt_cex2a.h"

#define CEX2A_MIN_MOD_SIZE	  1	/*    8 bits	*/
#define CEX2A_MAX_MOD_SIZE	256	/* 2048 bits	*/
#define CEX3A_MIN_MOD_SIZE	CEX2A_MIN_MOD_SIZE
#define CEX3A_MAX_MOD_SIZE	512	/* 4096 bits	*/

#define CEX2A_SPEED_RATING	970
#define CEX3A_SPEED_RATING	900 /* Fixme: Needs finetuning */

#define CEX2A_MAX_MESSAGE_SIZE	0x390	/* sizeof(struct type50_crb2_msg)    */
#define CEX2A_MAX_RESPONSE_SIZE 0x110	/* max outputdatalength + type80_hdr */

#define CEX3A_MAX_RESPONSE_SIZE	0x210	/* 512 bit modulus
					 * (max outputdatalength) +
					 * type80_hdr*/
#define CEX3A_MAX_MESSAGE_SIZE	sizeof(struct type50_crb3_msg)

#define CEX2A_CLEANUP_TIME	(15*HZ)
#define CEX3A_CLEANUP_TIME	CEX2A_CLEANUP_TIME

static struct ap_device_id zcrypt_cex2a_ids[] = {
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX2A) },
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX3A) },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex2a_ids);
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("CEX2A Cryptographic Coprocessor device driver, "
		   "Copyright IBM Corp. 2001, 2006");
MODULE_LICENSE("GPL");

static int zcrypt_cex2a_probe(struct ap_device *ap_dev);
static void zcrypt_cex2a_remove(struct ap_device *ap_dev);
static void zcrypt_cex2a_receive(struct ap_device *, struct ap_message *,
				 struct ap_message *);

static struct ap_driver zcrypt_cex2a_driver = {
	.probe = zcrypt_cex2a_probe,
	.remove = zcrypt_cex2a_remove,
	.ids = zcrypt_cex2a_ids,
	.request_timeout = CEX2A_CLEANUP_TIME,
};

/**
 * Convert a ICAMEX message to a type50 MEX message.
 *
 * @zdev: crypto device pointer
 * @zreq: crypto request pointer
 * @mex: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICAMEX_msg_to_type50MEX_msg(struct zcrypt_device *zdev,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo *mex)
{
	unsigned char *mod, *exp, *inp;
	int mod_len;

	mod_len = mex->inputdatalength;

	if (mod_len <= 128) {
		struct type50_meb1_msg *meb1 = ap_msg->message;
		memset(meb1, 0, sizeof(*meb1));
		ap_msg->length = sizeof(*meb1);
		meb1->header.msg_type_code = TYPE50_TYPE_CODE;
		meb1->header.msg_len = sizeof(*meb1);
		meb1->keyblock_type = TYPE50_MEB1_FMT;
		mod = meb1->modulus + sizeof(meb1->modulus) - mod_len;
		exp = meb1->exponent + sizeof(meb1->exponent) - mod_len;
		inp = meb1->message + sizeof(meb1->message) - mod_len;
	} else if (mod_len <= 256) {
		struct type50_meb2_msg *meb2 = ap_msg->message;
		memset(meb2, 0, sizeof(*meb2));
		ap_msg->length = sizeof(*meb2);
		meb2->header.msg_type_code = TYPE50_TYPE_CODE;
		meb2->header.msg_len = sizeof(*meb2);
		meb2->keyblock_type = TYPE50_MEB2_FMT;
		mod = meb2->modulus + sizeof(meb2->modulus) - mod_len;
		exp = meb2->exponent + sizeof(meb2->exponent) - mod_len;
		inp = meb2->message + sizeof(meb2->message) - mod_len;
	} else {
		/* mod_len > 256 = 4096 bit RSA Key */
		struct type50_meb3_msg *meb3 = ap_msg->message;
		memset(meb3, 0, sizeof(*meb3));
		ap_msg->length = sizeof(*meb3);
		meb3->header.msg_type_code = TYPE50_TYPE_CODE;
		meb3->header.msg_len = sizeof(*meb3);
		meb3->keyblock_type = TYPE50_MEB3_FMT;
		mod = meb3->modulus + sizeof(meb3->modulus) - mod_len;
		exp = meb3->exponent + sizeof(meb3->exponent) - mod_len;
		inp = meb3->message + sizeof(meb3->message) - mod_len;
	}

	if (copy_from_user(mod, mex->n_modulus, mod_len) ||
	    copy_from_user(exp, mex->b_key, mod_len) ||
	    copy_from_user(inp, mex->inputdata, mod_len))
		return -EFAULT;
	return 0;
}

/**
 * Convert a ICACRT message to a type50 CRT message.
 *
 * @zdev: crypto device pointer
 * @zreq: crypto request pointer
 * @crt: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICACRT_msg_to_type50CRT_msg(struct zcrypt_device *zdev,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo_crt *crt)
{
	int mod_len, short_len, long_len, long_offset, limit;
	unsigned char *p, *q, *dp, *dq, *u, *inp;

	mod_len = crt->inputdatalength;
	short_len = mod_len / 2;
	long_len = mod_len / 2 + 8;

	/*
	 * CEX2A cannot handle p, dp, or U > 128 bytes.
	 * If we have one of these, we need to do extra checking.
	 * For CEX3A the limit is 256 bytes.
	 */
	if (zdev->max_mod_size == CEX3A_MAX_MOD_SIZE)
		limit = 256;
	else
		limit = 128;

	if (long_len > limit) {
		/*
		 * zcrypt_rsa_crt already checked for the leading
		 * zeroes of np_prime, bp_key and u_mult_inc.
		 */
		long_offset = long_len - limit;
		long_len = limit;
	} else
		long_offset = 0;

	/*
	 * Instead of doing extra work for p, dp, U > 64 bytes, we'll just use
	 * the larger message structure.
	 */
	if (long_len <= 64) {
		struct type50_crb1_msg *crb1 = ap_msg->message;
		memset(crb1, 0, sizeof(*crb1));
		ap_msg->length = sizeof(*crb1);
		crb1->header.msg_type_code = TYPE50_TYPE_CODE;
		crb1->header.msg_len = sizeof(*crb1);
		crb1->keyblock_type = TYPE50_CRB1_FMT;
		p = crb1->p + sizeof(crb1->p) - long_len;
		q = crb1->q + sizeof(crb1->q) - short_len;
		dp = crb1->dp + sizeof(crb1->dp) - long_len;
		dq = crb1->dq + sizeof(crb1->dq) - short_len;
		u = crb1->u + sizeof(crb1->u) - long_len;
		inp = crb1->message + sizeof(crb1->message) - mod_len;
	} else if (long_len <= 128) {
		struct type50_crb2_msg *crb2 = ap_msg->message;
		memset(crb2, 0, sizeof(*crb2));
		ap_msg->length = sizeof(*crb2);
		crb2->header.msg_type_code = TYPE50_TYPE_CODE;
		crb2->header.msg_len = sizeof(*crb2);
		crb2->keyblock_type = TYPE50_CRB2_FMT;
		p = crb2->p + sizeof(crb2->p) - long_len;
		q = crb2->q + sizeof(crb2->q) - short_len;
		dp = crb2->dp + sizeof(crb2->dp) - long_len;
		dq = crb2->dq + sizeof(crb2->dq) - short_len;
		u = crb2->u + sizeof(crb2->u) - long_len;
		inp = crb2->message + sizeof(crb2->message) - mod_len;
	} else {
		/* long_len >= 256 */
		struct type50_crb3_msg *crb3 = ap_msg->message;
		memset(crb3, 0, sizeof(*crb3));
		ap_msg->length = sizeof(*crb3);
		crb3->header.msg_type_code = TYPE50_TYPE_CODE;
		crb3->header.msg_len = sizeof(*crb3);
		crb3->keyblock_type = TYPE50_CRB3_FMT;
		p = crb3->p + sizeof(crb3->p) - long_len;
		q = crb3->q + sizeof(crb3->q) - short_len;
		dp = crb3->dp + sizeof(crb3->dp) - long_len;
		dq = crb3->dq + sizeof(crb3->dq) - short_len;
		u = crb3->u + sizeof(crb3->u) - long_len;
		inp = crb3->message + sizeof(crb3->message) - mod_len;
	}

	if (copy_from_user(p, crt->np_prime + long_offset, long_len) ||
	    copy_from_user(q, crt->nq_prime, short_len) ||
	    copy_from_user(dp, crt->bp_key + long_offset, long_len) ||
	    copy_from_user(dq, crt->bq_key, short_len) ||
	    copy_from_user(u, crt->u_mult_inv + long_offset, long_len) ||
	    copy_from_user(inp, crt->inputdata, mod_len))
		return -EFAULT;

	return 0;
}

/**
 * Copy results from a type 80 reply message back to user space.
 *
 * @zdev: crypto device pointer
 * @reply: reply AP message.
 * @data: pointer to user output data
 * @length: size of user output data
 *
 * Returns 0 on success or -EFAULT.
 */
static int convert_type80(struct zcrypt_device *zdev,
			  struct ap_message *reply,
			  char __user *outputdata,
			  unsigned int outputdatalength)
{
	struct type80_hdr *t80h = reply->message;
	unsigned char *data;

	if (t80h->len < sizeof(*t80h) + outputdatalength) {
		/* The result is too short, the CEX2A card may not do that.. */
		zdev->online = 0;
		return -EAGAIN;	/* repeat the request on a different device. */
	}
	if (zdev->user_space_type == ZCRYPT_CEX2A)
		BUG_ON(t80h->len > CEX2A_MAX_RESPONSE_SIZE);
	else
		BUG_ON(t80h->len > CEX3A_MAX_RESPONSE_SIZE);
	data = reply->message + t80h->len - outputdatalength;
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
	case TYPE80_RSP_CODE:
		return convert_type80(zdev, reply,
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
static void zcrypt_cex2a_receive(struct ap_device *ap_dev,
				 struct ap_message *msg,
				 struct ap_message *reply)
{
	static struct error_hdr error_reply = {
		.type = TYPE82_RSP_CODE,
		.reply_code = REP82_ERROR_MACHINE_FAILURE,
	};
	struct type80_hdr *t80h;
	int length;

	/* Copy the reply message to the request message buffer. */
	if (IS_ERR(reply)) {
		memcpy(msg->message, &error_reply, sizeof(error_reply));
		goto out;
	}
	t80h = reply->message;
	if (t80h->type == TYPE80_RSP_CODE) {
		if (ap_dev->device_type == AP_DEVICE_TYPE_CEX2A)
			length = min(CEX2A_MAX_RESPONSE_SIZE, (int) t80h->len);
		else
			length = min(CEX3A_MAX_RESPONSE_SIZE, (int) t80h->len);
		memcpy(msg->message, reply->message, length);
	} else
		memcpy(msg->message, reply->message, sizeof error_reply);
out:
	complete((struct completion *) msg->private);
}

static atomic_t zcrypt_step = ATOMIC_INIT(0);

/**
 * The request distributor calls this function if it picked the CEX2A
 * device to handle a modexpo request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  CEX2A device to the request distributor
 * @mex: pointer to the modexpo request buffer
 */
static long zcrypt_cex2a_modexpo(struct zcrypt_device *zdev,
				 struct ica_rsa_modexpo *mex)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_init_message(&ap_msg);
	if (zdev->user_space_type == ZCRYPT_CEX2A)
		ap_msg.message = kmalloc(CEX2A_MAX_MESSAGE_SIZE, GFP_KERNEL);
	else
		ap_msg.message = kmalloc(CEX3A_MAX_MESSAGE_SIZE, GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.receive = zcrypt_cex2a_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICAMEX_msg_to_type50MEX_msg(zdev, &ap_msg, mex);
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
 * The request distributor calls this function if it picked the CEX2A
 * device to handle a modexpo_crt request.
 * @zdev: pointer to zcrypt_device structure that identifies the
 *	  CEX2A device to the request distributor
 * @crt: pointer to the modexpoc_crt request buffer
 */
static long zcrypt_cex2a_modexpo_crt(struct zcrypt_device *zdev,
				     struct ica_rsa_modexpo_crt *crt)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_init_message(&ap_msg);
	if (zdev->user_space_type == ZCRYPT_CEX2A)
		ap_msg.message = kmalloc(CEX2A_MAX_MESSAGE_SIZE, GFP_KERNEL);
	else
		ap_msg.message = kmalloc(CEX3A_MAX_MESSAGE_SIZE, GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;
	ap_msg.receive = zcrypt_cex2a_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICACRT_msg_to_type50CRT_msg(zdev, &ap_msg, crt);
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
 * The crypto operations for a CEX2A card.
 */
static struct zcrypt_ops zcrypt_cex2a_ops = {
	.rsa_modexpo = zcrypt_cex2a_modexpo,
	.rsa_modexpo_crt = zcrypt_cex2a_modexpo_crt,
};

/**
 * Probe function for CEX2A cards. It always accepts the AP device
 * since the bus_match already checked the hardware type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_cex2a_probe(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = NULL;
	int rc = 0;

	switch (ap_dev->device_type) {
	case AP_DEVICE_TYPE_CEX2A:
		zdev = zcrypt_device_alloc(CEX2A_MAX_RESPONSE_SIZE);
		if (!zdev)
			return -ENOMEM;
		zdev->user_space_type = ZCRYPT_CEX2A;
		zdev->type_string = "CEX2A";
		zdev->min_mod_size = CEX2A_MIN_MOD_SIZE;
		zdev->max_mod_size = CEX2A_MAX_MOD_SIZE;
		zdev->short_crt = 1;
		zdev->speed_rating = CEX2A_SPEED_RATING;
		zdev->max_exp_bit_length = CEX2A_MAX_MOD_SIZE;
		break;
	case AP_DEVICE_TYPE_CEX3A:
		zdev = zcrypt_device_alloc(CEX3A_MAX_RESPONSE_SIZE);
		if (!zdev)
			return -ENOMEM;
		zdev->user_space_type = ZCRYPT_CEX3A;
		zdev->type_string = "CEX3A";
		zdev->min_mod_size = CEX2A_MIN_MOD_SIZE;
		zdev->max_mod_size = CEX2A_MAX_MOD_SIZE;
		zdev->max_exp_bit_length = CEX2A_MAX_MOD_SIZE;
		if (ap_4096_commands_available(ap_dev->qid)) {
			zdev->max_mod_size = CEX3A_MAX_MOD_SIZE;
			zdev->max_exp_bit_length = CEX3A_MAX_MOD_SIZE;
		}
		zdev->short_crt = 1;
		zdev->speed_rating = CEX3A_SPEED_RATING;
		break;
	}
	if (zdev != NULL) {
		zdev->ap_dev = ap_dev;
		zdev->ops = &zcrypt_cex2a_ops;
		zdev->online = 1;
		ap_dev->reply = &zdev->reply;
		ap_dev->private = zdev;
		rc = zcrypt_device_register(zdev);
	}
	if (rc) {
		ap_dev->private = NULL;
		zcrypt_device_free(zdev);
	}
	return rc;
}

/**
 * This is called to remove the extended CEX2A driver information
 * if an AP device is removed.
 */
static void zcrypt_cex2a_remove(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = ap_dev->private;

	zcrypt_device_unregister(zdev);
}

int __init zcrypt_cex2a_init(void)
{
	return ap_driver_register(&zcrypt_cex2a_driver, THIS_MODULE, "cex2a");
}

void __exit zcrypt_cex2a_exit(void)
{
	ap_driver_unregister(&zcrypt_cex2a_driver);
}

module_init(zcrypt_cex2a_init);
module_exit(zcrypt_cex2a_exit);
