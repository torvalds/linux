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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_msgtype50.h"

/* >= CEX3A: 4096 bits */
#define CEX3A_MAX_MOD_SIZE 512

/* CEX2A: max outputdatalength + type80_hdr */
#define CEX2A_MAX_RESPONSE_SIZE 0x110

/* >= CEX3A: 512 bit modulus, (max outputdatalength) + type80_hdr */
#define CEX3A_MAX_RESPONSE_SIZE 0x210

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("Cryptographic Accelerator (message type 50), " \
		   "Copyright IBM Corp. 2001, 2012");
MODULE_LICENSE("GPL");

/**
 * The type 50 message family is associated with a CEXxA cards.
 *
 * The four members of the family are described below.
 *
 * Note that all unsigned char arrays are right-justified and left-padded
 * with zeroes.
 *
 * Note that all reserved fields must be zeroes.
 */
struct type50_hdr {
	unsigned char	reserved1;
	unsigned char	msg_type_code;	/* 0x50 */
	unsigned short	msg_len;
	unsigned char	reserved2;
	unsigned char	ignored;
	unsigned short	reserved3;
} __packed;

#define TYPE50_TYPE_CODE	0x50

#define TYPE50_MEB1_FMT		0x0001
#define TYPE50_MEB2_FMT		0x0002
#define TYPE50_MEB3_FMT		0x0003
#define TYPE50_CRB1_FMT		0x0011
#define TYPE50_CRB2_FMT		0x0012
#define TYPE50_CRB3_FMT		0x0013

/* Mod-Exp, with a small modulus */
struct type50_meb1_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0001 */
	unsigned char	reserved[6];
	unsigned char	exponent[128];
	unsigned char	modulus[128];
	unsigned char	message[128];
} __packed;

/* Mod-Exp, with a large modulus */
struct type50_meb2_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0002 */
	unsigned char	reserved[6];
	unsigned char	exponent[256];
	unsigned char	modulus[256];
	unsigned char	message[256];
} __packed;

/* Mod-Exp, with a larger modulus */
struct type50_meb3_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0003 */
	unsigned char	reserved[6];
	unsigned char	exponent[512];
	unsigned char	modulus[512];
	unsigned char	message[512];
} __packed;

/* CRT, with a small modulus */
struct type50_crb1_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0011 */
	unsigned char	reserved[6];
	unsigned char	p[64];
	unsigned char	q[64];
	unsigned char	dp[64];
	unsigned char	dq[64];
	unsigned char	u[64];
	unsigned char	message[128];
} __packed;

/* CRT, with a large modulus */
struct type50_crb2_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0012 */
	unsigned char	reserved[6];
	unsigned char	p[128];
	unsigned char	q[128];
	unsigned char	dp[128];
	unsigned char	dq[128];
	unsigned char	u[128];
	unsigned char	message[256];
} __packed;

/* CRT, with a larger modulus */
struct type50_crb3_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0013 */
	unsigned char	reserved[6];
	unsigned char	p[256];
	unsigned char	q[256];
	unsigned char	dp[256];
	unsigned char	dq[256];
	unsigned char	u[256];
	unsigned char	message[512];
} __packed;

/**
 * The type 80 response family is associated with a CEXxA cards.
 *
 * Note that all unsigned char arrays are right-justified and left-padded
 * with zeroes.
 *
 * Note that all reserved fields must be zeroes.
 */

#define TYPE80_RSP_CODE 0x80

struct type80_hdr {
	unsigned char	reserved1;
	unsigned char	type;		/* 0x80 */
	unsigned short	len;
	unsigned char	code;		/* 0x00 */
	unsigned char	reserved2[3];
	unsigned char	reserved3[8];
} __packed;

unsigned int get_rsa_modex_fc(struct ica_rsa_modexpo *mex, int *fcode)
{

	if (!mex->inputdatalength)
		return -EINVAL;

	if (mex->inputdatalength <= 128)	/* 1024 bit */
		*fcode = MEX_1K;
	else if (mex->inputdatalength <= 256)	/* 2048 bit */
		*fcode = MEX_2K;
	else					/* 4096 bit */
		*fcode = MEX_4K;

	return 0;
}

unsigned int get_rsa_crt_fc(struct ica_rsa_modexpo_crt *crt, int *fcode)
{

	if (!crt->inputdatalength)
		return -EINVAL;

	if (crt->inputdatalength <= 128)	/* 1024 bit */
		*fcode = CRT_1K;
	else if (crt->inputdatalength <= 256)	/* 2048 bit */
		*fcode = CRT_2K;
	else					/* 4096 bit */
		*fcode = CRT_4K;

	return 0;
}

/**
 * Convert a ICAMEX message to a type50 MEX message.
 *
 * @zq: crypto queue pointer
 * @ap_msg: crypto request pointer
 * @mex: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICAMEX_msg_to_type50MEX_msg(struct zcrypt_queue *zq,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo *mex)
{
	unsigned char *mod, *exp, *inp;
	int mod_len;

	mod_len = mex->inputdatalength;

	if (mod_len <= 128) {
		struct type50_meb1_msg *meb1 = ap_msg->msg;

		memset(meb1, 0, sizeof(*meb1));
		ap_msg->len = sizeof(*meb1);
		meb1->header.msg_type_code = TYPE50_TYPE_CODE;
		meb1->header.msg_len = sizeof(*meb1);
		meb1->keyblock_type = TYPE50_MEB1_FMT;
		mod = meb1->modulus + sizeof(meb1->modulus) - mod_len;
		exp = meb1->exponent + sizeof(meb1->exponent) - mod_len;
		inp = meb1->message + sizeof(meb1->message) - mod_len;
	} else if (mod_len <= 256) {
		struct type50_meb2_msg *meb2 = ap_msg->msg;

		memset(meb2, 0, sizeof(*meb2));
		ap_msg->len = sizeof(*meb2);
		meb2->header.msg_type_code = TYPE50_TYPE_CODE;
		meb2->header.msg_len = sizeof(*meb2);
		meb2->keyblock_type = TYPE50_MEB2_FMT;
		mod = meb2->modulus + sizeof(meb2->modulus) - mod_len;
		exp = meb2->exponent + sizeof(meb2->exponent) - mod_len;
		inp = meb2->message + sizeof(meb2->message) - mod_len;
	} else if (mod_len <= 512) {
		struct type50_meb3_msg *meb3 = ap_msg->msg;

		memset(meb3, 0, sizeof(*meb3));
		ap_msg->len = sizeof(*meb3);
		meb3->header.msg_type_code = TYPE50_TYPE_CODE;
		meb3->header.msg_len = sizeof(*meb3);
		meb3->keyblock_type = TYPE50_MEB3_FMT;
		mod = meb3->modulus + sizeof(meb3->modulus) - mod_len;
		exp = meb3->exponent + sizeof(meb3->exponent) - mod_len;
		inp = meb3->message + sizeof(meb3->message) - mod_len;
	} else
		return -EINVAL;

	if (copy_from_user(mod, mex->n_modulus, mod_len) ||
	    copy_from_user(exp, mex->b_key, mod_len) ||
	    copy_from_user(inp, mex->inputdata, mod_len))
		return -EFAULT;
	return 0;
}

/**
 * Convert a ICACRT message to a type50 CRT message.
 *
 * @zq: crypto queue pointer
 * @ap_msg: crypto request pointer
 * @crt: pointer to user input data
 *
 * Returns 0 on success or -EFAULT.
 */
static int ICACRT_msg_to_type50CRT_msg(struct zcrypt_queue *zq,
				       struct ap_message *ap_msg,
				       struct ica_rsa_modexpo_crt *crt)
{
	int mod_len, short_len;
	unsigned char *p, *q, *dp, *dq, *u, *inp;

	mod_len = crt->inputdatalength;
	short_len = (mod_len + 1) / 2;

	/*
	 * CEX2A and CEX3A w/o FW update can handle requests up to
	 * 256 byte modulus (2k keys).
	 * CEX3A with FW update and newer CEXxA cards are able to handle
	 * 512 byte modulus (4k keys).
	 */
	if (mod_len <= 128) {		/* up to 1024 bit key size */
		struct type50_crb1_msg *crb1 = ap_msg->msg;

		memset(crb1, 0, sizeof(*crb1));
		ap_msg->len = sizeof(*crb1);
		crb1->header.msg_type_code = TYPE50_TYPE_CODE;
		crb1->header.msg_len = sizeof(*crb1);
		crb1->keyblock_type = TYPE50_CRB1_FMT;
		p = crb1->p + sizeof(crb1->p) - short_len;
		q = crb1->q + sizeof(crb1->q) - short_len;
		dp = crb1->dp + sizeof(crb1->dp) - short_len;
		dq = crb1->dq + sizeof(crb1->dq) - short_len;
		u = crb1->u + sizeof(crb1->u) - short_len;
		inp = crb1->message + sizeof(crb1->message) - mod_len;
	} else if (mod_len <= 256) {	/* up to 2048 bit key size */
		struct type50_crb2_msg *crb2 = ap_msg->msg;

		memset(crb2, 0, sizeof(*crb2));
		ap_msg->len = sizeof(*crb2);
		crb2->header.msg_type_code = TYPE50_TYPE_CODE;
		crb2->header.msg_len = sizeof(*crb2);
		crb2->keyblock_type = TYPE50_CRB2_FMT;
		p = crb2->p + sizeof(crb2->p) - short_len;
		q = crb2->q + sizeof(crb2->q) - short_len;
		dp = crb2->dp + sizeof(crb2->dp) - short_len;
		dq = crb2->dq + sizeof(crb2->dq) - short_len;
		u = crb2->u + sizeof(crb2->u) - short_len;
		inp = crb2->message + sizeof(crb2->message) - mod_len;
	} else if ((mod_len <= 512) &&	/* up to 4096 bit key size */
		   (zq->zcard->max_mod_size == CEX3A_MAX_MOD_SIZE)) {
		struct type50_crb3_msg *crb3 = ap_msg->msg;

		memset(crb3, 0, sizeof(*crb3));
		ap_msg->len = sizeof(*crb3);
		crb3->header.msg_type_code = TYPE50_TYPE_CODE;
		crb3->header.msg_len = sizeof(*crb3);
		crb3->keyblock_type = TYPE50_CRB3_FMT;
		p = crb3->p + sizeof(crb3->p) - short_len;
		q = crb3->q + sizeof(crb3->q) - short_len;
		dp = crb3->dp + sizeof(crb3->dp) - short_len;
		dq = crb3->dq + sizeof(crb3->dq) - short_len;
		u = crb3->u + sizeof(crb3->u) - short_len;
		inp = crb3->message + sizeof(crb3->message) - mod_len;
	} else
		return -EINVAL;

	/*
	 * correct the offset of p, bp and mult_inv according zcrypt.h
	 * block size right aligned (skip the first byte)
	 */
	if (copy_from_user(p, crt->np_prime + MSGTYPE_ADJUSTMENT, short_len) ||
	    copy_from_user(q, crt->nq_prime, short_len) ||
	    copy_from_user(dp, crt->bp_key + MSGTYPE_ADJUSTMENT, short_len) ||
	    copy_from_user(dq, crt->bq_key, short_len) ||
	    copy_from_user(u, crt->u_mult_inv + MSGTYPE_ADJUSTMENT, short_len) ||
	    copy_from_user(inp, crt->inputdata, mod_len))
		return -EFAULT;

	return 0;
}

/**
 * Copy results from a type 80 reply message back to user space.
 *
 * @zq: crypto device pointer
 * @reply: reply AP message.
 * @data: pointer to user output data
 * @length: size of user output data
 *
 * Returns 0 on success or -EFAULT.
 */
static int convert_type80(struct zcrypt_queue *zq,
			  struct ap_message *reply,
			  char __user *outputdata,
			  unsigned int outputdatalength)
{
	struct type80_hdr *t80h = reply->msg;
	unsigned char *data;

	if (t80h->len < sizeof(*t80h) + outputdatalength) {
		/* The result is too short, the CEXxA card may not do that.. */
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       AP_QID_CARD(zq->queue->qid),
		       AP_QID_QUEUE(zq->queue->qid));
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x code=0x%02x => online=0 rc=EAGAIN\n",
			   AP_QID_CARD(zq->queue->qid),
			   AP_QID_QUEUE(zq->queue->qid),
			   t80h->code);
		return -EAGAIN;	/* repeat the request on a different device. */
	}
	if (zq->zcard->user_space_type == ZCRYPT_CEX2A)
		BUG_ON(t80h->len > CEX2A_MAX_RESPONSE_SIZE);
	else
		BUG_ON(t80h->len > CEX3A_MAX_RESPONSE_SIZE);
	data = reply->msg + t80h->len - outputdatalength;
	if (copy_to_user(outputdata, data, outputdatalength))
		return -EFAULT;
	return 0;
}

static int convert_response(struct zcrypt_queue *zq,
			    struct ap_message *reply,
			    char __user *outputdata,
			    unsigned int outputdatalength)
{
	/* Response type byte is the second byte in the response. */
	unsigned char rtype = ((unsigned char *) reply->msg)[1];

	switch (rtype) {
	case TYPE82_RSP_CODE:
	case TYPE88_RSP_CODE:
		return convert_error(zq, reply);
	case TYPE80_RSP_CODE:
		return convert_type80(zq, reply,
				      outputdata, outputdatalength);
	default: /* Unknown response type, this should NEVER EVER happen */
		zq->online = 0;
		pr_err("Cryptographic device %02x.%04x failed and was set offline\n",
		       AP_QID_CARD(zq->queue->qid),
		       AP_QID_QUEUE(zq->queue->qid));
		ZCRYPT_DBF(DBF_ERR,
			   "device=%02x.%04x rtype=0x%02x => online=0 rc=EAGAIN\n",
			   AP_QID_CARD(zq->queue->qid),
			   AP_QID_QUEUE(zq->queue->qid),
			   (unsigned int) rtype);
		return -EAGAIN;	/* repeat the request on a different device. */
	}
}

/**
 * This function is called from the AP bus code after a crypto request
 * "msg" has finished with the reply message "reply".
 * It is called from tasklet context.
 * @aq: pointer to the AP device
 * @msg: pointer to the AP message
 * @reply: pointer to the AP reply message
 */
static void zcrypt_cex2a_receive(struct ap_queue *aq,
				 struct ap_message *msg,
				 struct ap_message *reply)
{
	static struct error_hdr error_reply = {
		.type = TYPE82_RSP_CODE,
		.reply_code = REP82_ERROR_MACHINE_FAILURE,
	};
	struct type80_hdr *t80h;
	int len;

	/* Copy the reply message to the request message buffer. */
	if (!reply)
		goto out;	/* ap_msg->rc indicates the error */
	t80h = reply->msg;
	if (t80h->type == TYPE80_RSP_CODE) {
		if (aq->ap_dev.device_type == AP_DEVICE_TYPE_CEX2A)
			len = min_t(int, CEX2A_MAX_RESPONSE_SIZE, t80h->len);
		else
			len = min_t(int, CEX3A_MAX_RESPONSE_SIZE, t80h->len);
		memcpy(msg->msg, reply->msg, len);
	} else
		memcpy(msg->msg, reply->msg, sizeof(error_reply));
out:
	complete((struct completion *) msg->private);
}

static atomic_t zcrypt_step = ATOMIC_INIT(0);

/**
 * The request distributor calls this function if it picked the CEXxA
 * device to handle a modexpo request.
 * @zq: pointer to zcrypt_queue structure that identifies the
 *	CEXxA device to the request distributor
 * @mex: pointer to the modexpo request buffer
 */
static long zcrypt_cex2a_modexpo(struct zcrypt_queue *zq,
				 struct ica_rsa_modexpo *mex)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_init_message(&ap_msg);
	if (zq->zcard->user_space_type == ZCRYPT_CEX2A)
		ap_msg.msg = kmalloc(MSGTYPE50_CRB2_MAX_MSG_SIZE, GFP_KERNEL);
	else
		ap_msg.msg = kmalloc(MSGTYPE50_CRB3_MAX_MSG_SIZE, GFP_KERNEL);
	if (!ap_msg.msg)
		return -ENOMEM;
	ap_msg.receive = zcrypt_cex2a_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICAMEX_msg_to_type50MEX_msg(zq, &ap_msg, mex);
	if (rc)
		goto out_free;
	init_completion(&work);
	rc = ap_queue_message(zq->queue, &ap_msg);
	if (rc)
		goto out_free;
	rc = wait_for_completion_interruptible(&work);
	if (rc == 0) {
		rc = ap_msg.rc;
		if (rc == 0)
			rc = convert_response(zq, &ap_msg, mex->outputdata,
					      mex->outputdatalength);
	} else
		/* Signal pending. */
		ap_cancel_message(zq->queue, &ap_msg);
out_free:
	kfree(ap_msg.msg);
	return rc;
}

/**
 * The request distributor calls this function if it picked the CEXxA
 * device to handle a modexpo_crt request.
 * @zq: pointer to zcrypt_queue structure that identifies the
 *	CEXxA device to the request distributor
 * @crt: pointer to the modexpoc_crt request buffer
 */
static long zcrypt_cex2a_modexpo_crt(struct zcrypt_queue *zq,
				     struct ica_rsa_modexpo_crt *crt)
{
	struct ap_message ap_msg;
	struct completion work;
	int rc;

	ap_init_message(&ap_msg);
	if (zq->zcard->user_space_type == ZCRYPT_CEX2A)
		ap_msg.msg = kmalloc(MSGTYPE50_CRB2_MAX_MSG_SIZE, GFP_KERNEL);
	else
		ap_msg.msg = kmalloc(MSGTYPE50_CRB3_MAX_MSG_SIZE, GFP_KERNEL);
	if (!ap_msg.msg)
		return -ENOMEM;
	ap_msg.receive = zcrypt_cex2a_receive;
	ap_msg.psmid = (((unsigned long long) current->pid) << 32) +
				atomic_inc_return(&zcrypt_step);
	ap_msg.private = &work;
	rc = ICACRT_msg_to_type50CRT_msg(zq, &ap_msg, crt);
	if (rc)
		goto out_free;
	init_completion(&work);
	rc = ap_queue_message(zq->queue, &ap_msg);
	if (rc)
		goto out_free;
	rc = wait_for_completion_interruptible(&work);
	if (rc == 0) {
		rc = ap_msg.rc;
		if (rc == 0)
			rc = convert_response(zq, &ap_msg, crt->outputdata,
					      crt->outputdatalength);
	} else
		/* Signal pending. */
		ap_cancel_message(zq->queue, &ap_msg);
out_free:
	kfree(ap_msg.msg);
	return rc;
}

/**
 * The crypto operations for message type 50.
 */
static struct zcrypt_ops zcrypt_msgtype50_ops = {
	.rsa_modexpo = zcrypt_cex2a_modexpo,
	.rsa_modexpo_crt = zcrypt_cex2a_modexpo_crt,
	.owner = THIS_MODULE,
	.name = MSGTYPE50_NAME,
	.variant = MSGTYPE50_VARIANT_DEFAULT,
};

void __init zcrypt_msgtype50_init(void)
{
	zcrypt_msgtype_register(&zcrypt_msgtype50_ops);
}

void __exit zcrypt_msgtype50_exit(void)
{
	zcrypt_msgtype_unregister(&zcrypt_msgtype50_ops);
}
