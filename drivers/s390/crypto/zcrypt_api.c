// SPDX-License-Identifier: GPL-2.0+
/*
 *  zcrypt 2.1.0
 *
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *	       Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/hw_random.h>
#include <linux/debugfs.h>
#include <asm/debug.h>

#define CREATE_TRACE_POINTS
#include <asm/trace/zcrypt.h>

#include "zcrypt_api.h"
#include "zcrypt_debug.h"

#include "zcrypt_msgtype6.h"
#include "zcrypt_msgtype50.h"

/*
 * Module description.
 */
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("Cryptographic Coprocessor interface, " \
		   "Copyright IBM Corp. 2001, 2012");
MODULE_LICENSE("GPL");

/*
 * zcrypt tracepoint functions
 */
EXPORT_TRACEPOINT_SYMBOL(s390_zcrypt_req);
EXPORT_TRACEPOINT_SYMBOL(s390_zcrypt_rep);

static int zcrypt_hwrng_seed = 1;
module_param_named(hwrng_seed, zcrypt_hwrng_seed, int, 0440);
MODULE_PARM_DESC(hwrng_seed, "Turn on/off hwrng auto seed, default is 1 (on).");

DEFINE_SPINLOCK(zcrypt_list_lock);
LIST_HEAD(zcrypt_card_list);
int zcrypt_device_count;

static atomic_t zcrypt_open_count = ATOMIC_INIT(0);
static atomic_t zcrypt_rescan_count = ATOMIC_INIT(0);

atomic_t zcrypt_rescan_req = ATOMIC_INIT(0);
EXPORT_SYMBOL(zcrypt_rescan_req);

static LIST_HEAD(zcrypt_ops_list);

/* Zcrypt related debug feature stuff. */
debug_info_t *zcrypt_dbf_info;

/**
 * Process a rescan of the transport layer.
 *
 * Returns 1, if the rescan has been processed, otherwise 0.
 */
static inline int zcrypt_process_rescan(void)
{
	if (atomic_read(&zcrypt_rescan_req)) {
		atomic_set(&zcrypt_rescan_req, 0);
		atomic_inc(&zcrypt_rescan_count);
		ap_bus_force_rescan();
		ZCRYPT_DBF(DBF_INFO, "rescan count=%07d\n",
			   atomic_inc_return(&zcrypt_rescan_count));
		return 1;
	}
	return 0;
}

void zcrypt_msgtype_register(struct zcrypt_ops *zops)
{
	list_add_tail(&zops->list, &zcrypt_ops_list);
}

void zcrypt_msgtype_unregister(struct zcrypt_ops *zops)
{
	list_del_init(&zops->list);
}

struct zcrypt_ops *zcrypt_msgtype(unsigned char *name, int variant)
{
	struct zcrypt_ops *zops;

	list_for_each_entry(zops, &zcrypt_ops_list, list)
		if ((zops->variant == variant) &&
		    (!strncmp(zops->name, name, sizeof(zops->name))))
			return zops;
	return NULL;
}
EXPORT_SYMBOL(zcrypt_msgtype);

/**
 * zcrypt_read (): Not supported beyond zcrypt 1.3.1.
 *
 * This function is not supported beyond zcrypt 1.3.1.
 */
static ssize_t zcrypt_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	return -EPERM;
}

/**
 * zcrypt_write(): Not allowed.
 *
 * Write is is not allowed
 */
static ssize_t zcrypt_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	return -EPERM;
}

/**
 * zcrypt_open(): Count number of users.
 *
 * Device open function to count number of users.
 */
static int zcrypt_open(struct inode *inode, struct file *filp)
{
	atomic_inc(&zcrypt_open_count);
	return nonseekable_open(inode, filp);
}

/**
 * zcrypt_release(): Count number of users.
 *
 * Device close function to count number of users.
 */
static int zcrypt_release(struct inode *inode, struct file *filp)
{
	atomic_dec(&zcrypt_open_count);
	return 0;
}

static inline struct zcrypt_queue *zcrypt_pick_queue(struct zcrypt_card *zc,
						     struct zcrypt_queue *zq,
						     unsigned int weight)
{
	if (!zq || !try_module_get(zq->queue->ap_dev.drv->driver.owner))
		return NULL;
	zcrypt_queue_get(zq);
	get_device(&zq->queue->ap_dev.device);
	atomic_add(weight, &zc->load);
	atomic_add(weight, &zq->load);
	zq->request_count++;
	return zq;
}

static inline void zcrypt_drop_queue(struct zcrypt_card *zc,
				     struct zcrypt_queue *zq,
				     unsigned int weight)
{
	struct module *mod = zq->queue->ap_dev.drv->driver.owner;

	zq->request_count--;
	atomic_sub(weight, &zc->load);
	atomic_sub(weight, &zq->load);
	put_device(&zq->queue->ap_dev.device);
	zcrypt_queue_put(zq);
	module_put(mod);
}

static inline bool zcrypt_card_compare(struct zcrypt_card *zc,
				       struct zcrypt_card *pref_zc,
				       unsigned int weight,
				       unsigned int pref_weight)
{
	if (!pref_zc)
		return false;
	weight += atomic_read(&zc->load);
	pref_weight += atomic_read(&pref_zc->load);
	if (weight == pref_weight)
		return atomic64_read(&zc->card->total_request_count) >
			atomic64_read(&pref_zc->card->total_request_count);
	return weight > pref_weight;
}

static inline bool zcrypt_queue_compare(struct zcrypt_queue *zq,
					struct zcrypt_queue *pref_zq,
					unsigned int weight,
					unsigned int pref_weight)
{
	if (!pref_zq)
		return false;
	weight += atomic_read(&zq->load);
	pref_weight += atomic_read(&pref_zq->load);
	if (weight == pref_weight)
		return zq->queue->total_request_count >
			pref_zq->queue->total_request_count;
	return weight > pref_weight;
}

/*
 * zcrypt ioctls.
 */
static long zcrypt_rsa_modexpo(struct ica_rsa_modexpo *mex)
{
	struct zcrypt_card *zc, *pref_zc;
	struct zcrypt_queue *zq, *pref_zq;
	unsigned int weight, pref_weight;
	unsigned int func_code;
	int qid = 0, rc = -ENODEV;

	trace_s390_zcrypt_req(mex, TP_ICARSAMODEXPO);

	if (mex->outputdatalength < mex->inputdatalength) {
		func_code = 0;
		rc = -EINVAL;
		goto out;
	}

	/*
	 * As long as outputdatalength is big enough, we can set the
	 * outputdatalength equal to the inputdatalength, since that is the
	 * number of bytes we will copy in any case
	 */
	mex->outputdatalength = mex->inputdatalength;

	rc = get_rsa_modex_fc(mex, &func_code);
	if (rc)
		goto out;

	pref_zc = NULL;
	pref_zq = NULL;
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		/* Check for online accelarator and CCA cards */
		if (!zc->online || !(zc->card->functions & 0x18000000))
			continue;
		/* Check for size limits */
		if (zc->min_mod_size > mex->inputdatalength ||
		    zc->max_mod_size < mex->inputdatalength)
			continue;
		/* get weight index of the card device	*/
		weight = zc->speed_rating[func_code];
		if (zcrypt_card_compare(zc, pref_zc, weight, pref_weight))
			continue;
		for_each_zcrypt_queue(zq, zc) {
			/* check if device is online and eligible */
			if (!zq->online || !zq->ops->rsa_modexpo)
				continue;
			if (zcrypt_queue_compare(zq, pref_zq,
						 weight, pref_weight))
				continue;
			pref_zc = zc;
			pref_zq = zq;
			pref_weight = weight;
		}
	}
	pref_zq = zcrypt_pick_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

	if (!pref_zq) {
		rc = -ENODEV;
		goto out;
	}

	qid = pref_zq->queue->qid;
	rc = pref_zq->ops->rsa_modexpo(pref_zq, mex);

	spin_lock(&zcrypt_list_lock);
	zcrypt_drop_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

out:
	trace_s390_zcrypt_rep(mex, func_code, rc,
			      AP_QID_CARD(qid), AP_QID_QUEUE(qid));
	return rc;
}

static long zcrypt_rsa_crt(struct ica_rsa_modexpo_crt *crt)
{
	struct zcrypt_card *zc, *pref_zc;
	struct zcrypt_queue *zq, *pref_zq;
	unsigned int weight, pref_weight;
	unsigned int func_code;
	int qid = 0, rc = -ENODEV;

	trace_s390_zcrypt_req(crt, TP_ICARSACRT);

	if (crt->outputdatalength < crt->inputdatalength) {
		func_code = 0;
		rc = -EINVAL;
		goto out;
	}

	/*
	 * As long as outputdatalength is big enough, we can set the
	 * outputdatalength equal to the inputdatalength, since that is the
	 * number of bytes we will copy in any case
	 */
	crt->outputdatalength = crt->inputdatalength;

	rc = get_rsa_crt_fc(crt, &func_code);
	if (rc)
		goto out;

	pref_zc = NULL;
	pref_zq = NULL;
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		/* Check for online accelarator and CCA cards */
		if (!zc->online || !(zc->card->functions & 0x18000000))
			continue;
		/* Check for size limits */
		if (zc->min_mod_size > crt->inputdatalength ||
		    zc->max_mod_size < crt->inputdatalength)
			continue;
		/* get weight index of the card device	*/
		weight = zc->speed_rating[func_code];
		if (zcrypt_card_compare(zc, pref_zc, weight, pref_weight))
			continue;
		for_each_zcrypt_queue(zq, zc) {
			/* check if device is online and eligible */
			if (!zq->online || !zq->ops->rsa_modexpo_crt)
				continue;
			if (zcrypt_queue_compare(zq, pref_zq,
						 weight, pref_weight))
				continue;
			pref_zc = zc;
			pref_zq = zq;
			pref_weight = weight;
		}
	}
	pref_zq = zcrypt_pick_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

	if (!pref_zq) {
		rc = -ENODEV;
		goto out;
	}

	qid = pref_zq->queue->qid;
	rc = pref_zq->ops->rsa_modexpo_crt(pref_zq, crt);

	spin_lock(&zcrypt_list_lock);
	zcrypt_drop_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

out:
	trace_s390_zcrypt_rep(crt, func_code, rc,
			      AP_QID_CARD(qid), AP_QID_QUEUE(qid));
	return rc;
}

long zcrypt_send_cprb(struct ica_xcRB *xcRB)
{
	struct zcrypt_card *zc, *pref_zc;
	struct zcrypt_queue *zq, *pref_zq;
	struct ap_message ap_msg;
	unsigned int weight, pref_weight;
	unsigned int func_code;
	unsigned short *domain;
	int qid = 0, rc = -ENODEV;

	trace_s390_zcrypt_req(xcRB, TB_ZSECSENDCPRB);

	ap_init_message(&ap_msg);
	rc = get_cprb_fc(xcRB, &ap_msg, &func_code, &domain);
	if (rc)
		goto out;

	pref_zc = NULL;
	pref_zq = NULL;
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		/* Check for online CCA cards */
		if (!zc->online || !(zc->card->functions & 0x10000000))
			continue;
		/* Check for user selected CCA card */
		if (xcRB->user_defined != AUTOSELECT &&
		    xcRB->user_defined != zc->card->id)
			continue;
		/* get weight index of the card device	*/
		weight = speed_idx_cca(func_code) * zc->speed_rating[SECKEY];
		if (zcrypt_card_compare(zc, pref_zc, weight, pref_weight))
			continue;
		for_each_zcrypt_queue(zq, zc) {
			/* check if device is online and eligible */
			if (!zq->online ||
			    !zq->ops->send_cprb ||
			    ((*domain != (unsigned short) AUTOSELECT) &&
			     (*domain != AP_QID_QUEUE(zq->queue->qid))))
				continue;
			if (zcrypt_queue_compare(zq, pref_zq,
						 weight, pref_weight))
				continue;
			pref_zc = zc;
			pref_zq = zq;
			pref_weight = weight;
		}
	}
	pref_zq = zcrypt_pick_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

	if (!pref_zq) {
		rc = -ENODEV;
		goto out;
	}

	/* in case of auto select, provide the correct domain */
	qid = pref_zq->queue->qid;
	if (*domain == (unsigned short) AUTOSELECT)
		*domain = AP_QID_QUEUE(qid);

	rc = pref_zq->ops->send_cprb(pref_zq, xcRB, &ap_msg);

	spin_lock(&zcrypt_list_lock);
	zcrypt_drop_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

out:
	ap_release_message(&ap_msg);
	trace_s390_zcrypt_rep(xcRB, func_code, rc,
			      AP_QID_CARD(qid), AP_QID_QUEUE(qid));
	return rc;
}
EXPORT_SYMBOL(zcrypt_send_cprb);

static bool is_desired_ep11_card(unsigned int dev_id,
				 unsigned short target_num,
				 struct ep11_target_dev *targets)
{
	while (target_num-- > 0) {
		if (dev_id == targets->ap_id)
			return true;
		targets++;
	}
	return false;
}

static bool is_desired_ep11_queue(unsigned int dev_qid,
				  unsigned short target_num,
				  struct ep11_target_dev *targets)
{
	while (target_num-- > 0) {
		if (AP_MKQID(targets->ap_id, targets->dom_id) == dev_qid)
			return true;
		targets++;
	}
	return false;
}

static long zcrypt_send_ep11_cprb(struct ep11_urb *xcrb)
{
	struct zcrypt_card *zc, *pref_zc;
	struct zcrypt_queue *zq, *pref_zq;
	struct ep11_target_dev *targets;
	unsigned short target_num;
	unsigned int weight, pref_weight;
	unsigned int func_code;
	struct ap_message ap_msg;
	int qid = 0, rc = -ENODEV;

	trace_s390_zcrypt_req(xcrb, TP_ZSENDEP11CPRB);

	ap_init_message(&ap_msg);

	target_num = (unsigned short) xcrb->targets_num;

	/* empty list indicates autoselect (all available targets) */
	targets = NULL;
	if (target_num != 0) {
		struct ep11_target_dev __user *uptr;

		targets = kcalloc(target_num, sizeof(*targets), GFP_KERNEL);
		if (!targets) {
			func_code = 0;
			rc = -ENOMEM;
			goto out;
		}

		uptr = (struct ep11_target_dev __force __user *) xcrb->targets;
		if (copy_from_user(targets, uptr,
				   target_num * sizeof(*targets))) {
			func_code = 0;
			rc = -EFAULT;
			goto out_free;
		}
	}

	rc = get_ep11cprb_fc(xcrb, &ap_msg, &func_code);
	if (rc)
		goto out_free;

	pref_zc = NULL;
	pref_zq = NULL;
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		/* Check for online EP11 cards */
		if (!zc->online || !(zc->card->functions & 0x04000000))
			continue;
		/* Check for user selected EP11 card */
		if (targets &&
		    !is_desired_ep11_card(zc->card->id, target_num, targets))
			continue;
		/* get weight index of the card device	*/
		weight = speed_idx_ep11(func_code) * zc->speed_rating[SECKEY];
		if (zcrypt_card_compare(zc, pref_zc, weight, pref_weight))
			continue;
		for_each_zcrypt_queue(zq, zc) {
			/* check if device is online and eligible */
			if (!zq->online ||
			    !zq->ops->send_ep11_cprb ||
			    (targets &&
			     !is_desired_ep11_queue(zq->queue->qid,
						    target_num, targets)))
				continue;
			if (zcrypt_queue_compare(zq, pref_zq,
						 weight, pref_weight))
				continue;
			pref_zc = zc;
			pref_zq = zq;
			pref_weight = weight;
		}
	}
	pref_zq = zcrypt_pick_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

	if (!pref_zq) {
		rc = -ENODEV;
		goto out_free;
	}

	qid = pref_zq->queue->qid;
	rc = pref_zq->ops->send_ep11_cprb(pref_zq, xcrb, &ap_msg);

	spin_lock(&zcrypt_list_lock);
	zcrypt_drop_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

out_free:
	kfree(targets);
out:
	ap_release_message(&ap_msg);
	trace_s390_zcrypt_rep(xcrb, func_code, rc,
			      AP_QID_CARD(qid), AP_QID_QUEUE(qid));
	return rc;
}

static long zcrypt_rng(char *buffer)
{
	struct zcrypt_card *zc, *pref_zc;
	struct zcrypt_queue *zq, *pref_zq;
	unsigned int weight, pref_weight;
	unsigned int func_code;
	struct ap_message ap_msg;
	unsigned int domain;
	int qid = 0, rc = -ENODEV;

	trace_s390_zcrypt_req(buffer, TP_HWRNGCPRB);

	ap_init_message(&ap_msg);
	rc = get_rng_fc(&ap_msg, &func_code, &domain);
	if (rc)
		goto out;

	pref_zc = NULL;
	pref_zq = NULL;
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		/* Check for online CCA cards */
		if (!zc->online || !(zc->card->functions & 0x10000000))
			continue;
		/* get weight index of the card device	*/
		weight = zc->speed_rating[func_code];
		if (zcrypt_card_compare(zc, pref_zc, weight, pref_weight))
			continue;
		for_each_zcrypt_queue(zq, zc) {
			/* check if device is online and eligible */
			if (!zq->online || !zq->ops->rng)
				continue;
			if (zcrypt_queue_compare(zq, pref_zq,
						 weight, pref_weight))
				continue;
			pref_zc = zc;
			pref_zq = zq;
			pref_weight = weight;
		}
	}
	pref_zq = zcrypt_pick_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

	if (!pref_zq) {
		rc = -ENODEV;
		goto out;
	}

	qid = pref_zq->queue->qid;
	rc = pref_zq->ops->rng(pref_zq, buffer, &ap_msg);

	spin_lock(&zcrypt_list_lock);
	zcrypt_drop_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

out:
	ap_release_message(&ap_msg);
	trace_s390_zcrypt_rep(buffer, func_code, rc,
			      AP_QID_CARD(qid), AP_QID_QUEUE(qid));
	return rc;
}

static void zcrypt_device_status_mask(struct zcrypt_device_status *devstatus)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	struct zcrypt_device_status *stat;
	int card, queue;

	memset(devstatus, 0, MAX_ZDEV_ENTRIES
	       * sizeof(struct zcrypt_device_status));

	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			card = AP_QID_CARD(zq->queue->qid);
			if (card >= MAX_ZDEV_CARDIDS)
				continue;
			queue = AP_QID_QUEUE(zq->queue->qid);
			stat = &devstatus[card * AP_DOMAINS + queue];
			stat->hwtype = zc->card->ap_dev.device_type;
			stat->functions = zc->card->functions >> 26;
			stat->qid = zq->queue->qid;
			stat->online = zq->online ? 0x01 : 0x00;
		}
	}
	spin_unlock(&zcrypt_list_lock);
}

void zcrypt_device_status_mask_ext(struct zcrypt_device_status_ext *devstatus)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	struct zcrypt_device_status_ext *stat;
	int card, queue;

	memset(devstatus, 0, MAX_ZDEV_ENTRIES_EXT
	       * sizeof(struct zcrypt_device_status_ext));

	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			card = AP_QID_CARD(zq->queue->qid);
			queue = AP_QID_QUEUE(zq->queue->qid);
			stat = &devstatus[card * AP_DOMAINS + queue];
			stat->hwtype = zc->card->ap_dev.device_type;
			stat->functions = zc->card->functions >> 26;
			stat->qid = zq->queue->qid;
			stat->online = zq->online ? 0x01 : 0x00;
		}
	}
	spin_unlock(&zcrypt_list_lock);
}
EXPORT_SYMBOL(zcrypt_device_status_mask_ext);

static void zcrypt_status_mask(char status[], size_t max_adapters)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	int card;

	memset(status, 0, max_adapters);
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			card = AP_QID_CARD(zq->queue->qid);
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index
			    || card >= max_adapters)
				continue;
			status[card] = zc->online ? zc->user_space_type : 0x0d;
		}
	}
	spin_unlock(&zcrypt_list_lock);
}

static void zcrypt_qdepth_mask(char qdepth[], size_t max_adapters)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	int card;

	memset(qdepth, 0, max_adapters);
	spin_lock(&zcrypt_list_lock);
	local_bh_disable();
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			card = AP_QID_CARD(zq->queue->qid);
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index
			    || card >= max_adapters)
				continue;
			spin_lock(&zq->queue->lock);
			qdepth[card] =
				zq->queue->pendingq_count +
				zq->queue->requestq_count;
			spin_unlock(&zq->queue->lock);
		}
	}
	local_bh_enable();
	spin_unlock(&zcrypt_list_lock);
}

static void zcrypt_perdev_reqcnt(u32 reqcnt[], size_t max_adapters)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	int card;
	u64 cnt;

	memset(reqcnt, 0, sizeof(int) * max_adapters);
	spin_lock(&zcrypt_list_lock);
	local_bh_disable();
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			card = AP_QID_CARD(zq->queue->qid);
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index
			    || card >= max_adapters)
				continue;
			spin_lock(&zq->queue->lock);
			cnt = zq->queue->total_request_count;
			spin_unlock(&zq->queue->lock);
			reqcnt[card] = (cnt < UINT_MAX) ? (u32) cnt : UINT_MAX;
		}
	}
	local_bh_enable();
	spin_unlock(&zcrypt_list_lock);
}

static int zcrypt_pendingq_count(void)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	int pendingq_count;

	pendingq_count = 0;
	spin_lock(&zcrypt_list_lock);
	local_bh_disable();
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			spin_lock(&zq->queue->lock);
			pendingq_count += zq->queue->pendingq_count;
			spin_unlock(&zq->queue->lock);
		}
	}
	local_bh_enable();
	spin_unlock(&zcrypt_list_lock);
	return pendingq_count;
}

static int zcrypt_requestq_count(void)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	int requestq_count;

	requestq_count = 0;
	spin_lock(&zcrypt_list_lock);
	local_bh_disable();
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			spin_lock(&zq->queue->lock);
			requestq_count += zq->queue->requestq_count;
			spin_unlock(&zq->queue->lock);
		}
	}
	local_bh_enable();
	spin_unlock(&zcrypt_list_lock);
	return requestq_count;
}

static long zcrypt_unlocked_ioctl(struct file *filp, unsigned int cmd,
				  unsigned long arg)
{
	int rc = 0;

	switch (cmd) {
	case ICARSAMODEXPO: {
		struct ica_rsa_modexpo __user *umex = (void __user *) arg;
		struct ica_rsa_modexpo mex;

		if (copy_from_user(&mex, umex, sizeof(mex)))
			return -EFAULT;
		do {
			rc = zcrypt_rsa_modexpo(&mex);
		} while (rc == -EAGAIN);
		/* on failure: retry once again after a requested rescan */
		if ((rc == -ENODEV) && (zcrypt_process_rescan()))
			do {
				rc = zcrypt_rsa_modexpo(&mex);
			} while (rc == -EAGAIN);
		if (rc) {
			ZCRYPT_DBF(DBF_DEBUG, "ioctl ICARSAMODEXPO rc=%d\n", rc);
			return rc;
		}
		return put_user(mex.outputdatalength, &umex->outputdatalength);
	}
	case ICARSACRT: {
		struct ica_rsa_modexpo_crt __user *ucrt = (void __user *) arg;
		struct ica_rsa_modexpo_crt crt;

		if (copy_from_user(&crt, ucrt, sizeof(crt)))
			return -EFAULT;
		do {
			rc = zcrypt_rsa_crt(&crt);
		} while (rc == -EAGAIN);
		/* on failure: retry once again after a requested rescan */
		if ((rc == -ENODEV) && (zcrypt_process_rescan()))
			do {
				rc = zcrypt_rsa_crt(&crt);
			} while (rc == -EAGAIN);
		if (rc) {
			ZCRYPT_DBF(DBF_DEBUG, "ioctl ICARSACRT rc=%d\n", rc);
			return rc;
		}
		return put_user(crt.outputdatalength, &ucrt->outputdatalength);
	}
	case ZSECSENDCPRB: {
		struct ica_xcRB __user *uxcRB = (void __user *) arg;
		struct ica_xcRB xcRB;

		if (copy_from_user(&xcRB, uxcRB, sizeof(xcRB)))
			return -EFAULT;
		do {
			rc = zcrypt_send_cprb(&xcRB);
		} while (rc == -EAGAIN);
		/* on failure: retry once again after a requested rescan */
		if ((rc == -ENODEV) && (zcrypt_process_rescan()))
			do {
				rc = zcrypt_send_cprb(&xcRB);
			} while (rc == -EAGAIN);
		if (rc)
			ZCRYPT_DBF(DBF_DEBUG, "ioctl ZSENDCPRB rc=%d\n", rc);
		if (copy_to_user(uxcRB, &xcRB, sizeof(xcRB)))
			return -EFAULT;
		return rc;
	}
	case ZSENDEP11CPRB: {
		struct ep11_urb __user *uxcrb = (void __user *)arg;
		struct ep11_urb xcrb;

		if (copy_from_user(&xcrb, uxcrb, sizeof(xcrb)))
			return -EFAULT;
		do {
			rc = zcrypt_send_ep11_cprb(&xcrb);
		} while (rc == -EAGAIN);
		/* on failure: retry once again after a requested rescan */
		if ((rc == -ENODEV) && (zcrypt_process_rescan()))
			do {
				rc = zcrypt_send_ep11_cprb(&xcrb);
			} while (rc == -EAGAIN);
		if (rc)
			ZCRYPT_DBF(DBF_DEBUG, "ioctl ZSENDEP11CPRB rc=%d\n", rc);
		if (copy_to_user(uxcrb, &xcrb, sizeof(xcrb)))
			return -EFAULT;
		return rc;
	}
	case ZCRYPT_DEVICE_STATUS: {
		struct zcrypt_device_status_ext *device_status;
		size_t total_size = MAX_ZDEV_ENTRIES_EXT
			* sizeof(struct zcrypt_device_status_ext);

		device_status = kzalloc(total_size, GFP_KERNEL);
		if (!device_status)
			return -ENOMEM;
		zcrypt_device_status_mask_ext(device_status);
		if (copy_to_user((char __user *) arg, device_status,
				 total_size))
			rc = -EFAULT;
		kfree(device_status);
		return rc;
	}
	case ZCRYPT_STATUS_MASK: {
		char status[AP_DEVICES];

		zcrypt_status_mask(status, AP_DEVICES);
		if (copy_to_user((char __user *) arg, status, sizeof(status)))
			return -EFAULT;
		return 0;
	}
	case ZCRYPT_QDEPTH_MASK: {
		char qdepth[AP_DEVICES];

		zcrypt_qdepth_mask(qdepth, AP_DEVICES);
		if (copy_to_user((char __user *) arg, qdepth, sizeof(qdepth)))
			return -EFAULT;
		return 0;
	}
	case ZCRYPT_PERDEV_REQCNT: {
		u32 *reqcnt;

		reqcnt = kcalloc(AP_DEVICES, sizeof(u32), GFP_KERNEL);
		if (!reqcnt)
			return -ENOMEM;
		zcrypt_perdev_reqcnt(reqcnt, AP_DEVICES);
		if (copy_to_user((int __user *) arg, reqcnt,
				 sizeof(u32) * AP_DEVICES))
			rc = -EFAULT;
		kfree(reqcnt);
		return rc;
	}
	case Z90STAT_REQUESTQ_COUNT:
		return put_user(zcrypt_requestq_count(), (int __user *) arg);
	case Z90STAT_PENDINGQ_COUNT:
		return put_user(zcrypt_pendingq_count(), (int __user *) arg);
	case Z90STAT_TOTALOPEN_COUNT:
		return put_user(atomic_read(&zcrypt_open_count),
				(int __user *) arg);
	case Z90STAT_DOMAIN_INDEX:
		return put_user(ap_domain_index, (int __user *) arg);
	/*
	 * Deprecated ioctls
	 */
	case ZDEVICESTATUS: {
		/* the old ioctl supports only 64 adapters */
		struct zcrypt_device_status *device_status;
		size_t total_size = MAX_ZDEV_ENTRIES
			* sizeof(struct zcrypt_device_status);

		device_status = kzalloc(total_size, GFP_KERNEL);
		if (!device_status)
			return -ENOMEM;
		zcrypt_device_status_mask(device_status);
		if (copy_to_user((char __user *) arg, device_status,
				 total_size))
			rc = -EFAULT;
		kfree(device_status);
		return rc;
	}
	case Z90STAT_STATUS_MASK: {
		/* the old ioctl supports only 64 adapters */
		char status[MAX_ZDEV_CARDIDS];

		zcrypt_status_mask(status, MAX_ZDEV_CARDIDS);
		if (copy_to_user((char __user *) arg, status, sizeof(status)))
			return -EFAULT;
		return 0;
	}
	case Z90STAT_QDEPTH_MASK: {
		/* the old ioctl supports only 64 adapters */
		char qdepth[MAX_ZDEV_CARDIDS];

		zcrypt_qdepth_mask(qdepth, MAX_ZDEV_CARDIDS);
		if (copy_to_user((char __user *) arg, qdepth, sizeof(qdepth)))
			return -EFAULT;
		return 0;
	}
	case Z90STAT_PERDEV_REQCNT: {
		/* the old ioctl supports only 64 adapters */
		u32 reqcnt[MAX_ZDEV_CARDIDS];

		zcrypt_perdev_reqcnt(reqcnt, MAX_ZDEV_CARDIDS);
		if (copy_to_user((int __user *) arg, reqcnt, sizeof(reqcnt)))
			return -EFAULT;
		return 0;
	}
	/* unknown ioctl number */
	default:
		ZCRYPT_DBF(DBF_DEBUG, "unknown ioctl 0x%08x\n", cmd);
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
/*
 * ioctl32 conversion routines
 */
struct compat_ica_rsa_modexpo {
	compat_uptr_t	inputdata;
	unsigned int	inputdatalength;
	compat_uptr_t	outputdata;
	unsigned int	outputdatalength;
	compat_uptr_t	b_key;
	compat_uptr_t	n_modulus;
};

static long trans_modexpo32(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	struct compat_ica_rsa_modexpo __user *umex32 = compat_ptr(arg);
	struct compat_ica_rsa_modexpo mex32;
	struct ica_rsa_modexpo mex64;
	long rc;

	if (copy_from_user(&mex32, umex32, sizeof(mex32)))
		return -EFAULT;
	mex64.inputdata = compat_ptr(mex32.inputdata);
	mex64.inputdatalength = mex32.inputdatalength;
	mex64.outputdata = compat_ptr(mex32.outputdata);
	mex64.outputdatalength = mex32.outputdatalength;
	mex64.b_key = compat_ptr(mex32.b_key);
	mex64.n_modulus = compat_ptr(mex32.n_modulus);
	do {
		rc = zcrypt_rsa_modexpo(&mex64);
	} while (rc == -EAGAIN);
	/* on failure: retry once again after a requested rescan */
	if ((rc == -ENODEV) && (zcrypt_process_rescan()))
		do {
			rc = zcrypt_rsa_modexpo(&mex64);
		} while (rc == -EAGAIN);
	if (rc)
		return rc;
	return put_user(mex64.outputdatalength,
			&umex32->outputdatalength);
}

struct compat_ica_rsa_modexpo_crt {
	compat_uptr_t	inputdata;
	unsigned int	inputdatalength;
	compat_uptr_t	outputdata;
	unsigned int	outputdatalength;
	compat_uptr_t	bp_key;
	compat_uptr_t	bq_key;
	compat_uptr_t	np_prime;
	compat_uptr_t	nq_prime;
	compat_uptr_t	u_mult_inv;
};

static long trans_modexpo_crt32(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct compat_ica_rsa_modexpo_crt __user *ucrt32 = compat_ptr(arg);
	struct compat_ica_rsa_modexpo_crt crt32;
	struct ica_rsa_modexpo_crt crt64;
	long rc;

	if (copy_from_user(&crt32, ucrt32, sizeof(crt32)))
		return -EFAULT;
	crt64.inputdata = compat_ptr(crt32.inputdata);
	crt64.inputdatalength = crt32.inputdatalength;
	crt64.outputdata = compat_ptr(crt32.outputdata);
	crt64.outputdatalength = crt32.outputdatalength;
	crt64.bp_key = compat_ptr(crt32.bp_key);
	crt64.bq_key = compat_ptr(crt32.bq_key);
	crt64.np_prime = compat_ptr(crt32.np_prime);
	crt64.nq_prime = compat_ptr(crt32.nq_prime);
	crt64.u_mult_inv = compat_ptr(crt32.u_mult_inv);
	do {
		rc = zcrypt_rsa_crt(&crt64);
	} while (rc == -EAGAIN);
	/* on failure: retry once again after a requested rescan */
	if ((rc == -ENODEV) && (zcrypt_process_rescan()))
		do {
			rc = zcrypt_rsa_crt(&crt64);
		} while (rc == -EAGAIN);
	if (rc)
		return rc;
	return put_user(crt64.outputdatalength,
			&ucrt32->outputdatalength);
}

struct compat_ica_xcRB {
	unsigned short	agent_ID;
	unsigned int	user_defined;
	unsigned short	request_ID;
	unsigned int	request_control_blk_length;
	unsigned char	padding1[16 - sizeof(compat_uptr_t)];
	compat_uptr_t	request_control_blk_addr;
	unsigned int	request_data_length;
	char		padding2[16 - sizeof(compat_uptr_t)];
	compat_uptr_t	request_data_address;
	unsigned int	reply_control_blk_length;
	char		padding3[16 - sizeof(compat_uptr_t)];
	compat_uptr_t	reply_control_blk_addr;
	unsigned int	reply_data_length;
	char		padding4[16 - sizeof(compat_uptr_t)];
	compat_uptr_t	reply_data_addr;
	unsigned short	priority_window;
	unsigned int	status;
} __packed;

static long trans_xcRB32(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	struct compat_ica_xcRB __user *uxcRB32 = compat_ptr(arg);
	struct compat_ica_xcRB xcRB32;
	struct ica_xcRB xcRB64;
	long rc;

	if (copy_from_user(&xcRB32, uxcRB32, sizeof(xcRB32)))
		return -EFAULT;
	xcRB64.agent_ID = xcRB32.agent_ID;
	xcRB64.user_defined = xcRB32.user_defined;
	xcRB64.request_ID = xcRB32.request_ID;
	xcRB64.request_control_blk_length =
		xcRB32.request_control_blk_length;
	xcRB64.request_control_blk_addr =
		compat_ptr(xcRB32.request_control_blk_addr);
	xcRB64.request_data_length =
		xcRB32.request_data_length;
	xcRB64.request_data_address =
		compat_ptr(xcRB32.request_data_address);
	xcRB64.reply_control_blk_length =
		xcRB32.reply_control_blk_length;
	xcRB64.reply_control_blk_addr =
		compat_ptr(xcRB32.reply_control_blk_addr);
	xcRB64.reply_data_length = xcRB32.reply_data_length;
	xcRB64.reply_data_addr =
		compat_ptr(xcRB32.reply_data_addr);
	xcRB64.priority_window = xcRB32.priority_window;
	xcRB64.status = xcRB32.status;
	do {
		rc = zcrypt_send_cprb(&xcRB64);
	} while (rc == -EAGAIN);
	/* on failure: retry once again after a requested rescan */
	if ((rc == -ENODEV) && (zcrypt_process_rescan()))
		do {
			rc = zcrypt_send_cprb(&xcRB64);
		} while (rc == -EAGAIN);
	xcRB32.reply_control_blk_length = xcRB64.reply_control_blk_length;
	xcRB32.reply_data_length = xcRB64.reply_data_length;
	xcRB32.status = xcRB64.status;
	if (copy_to_user(uxcRB32, &xcRB32, sizeof(xcRB32)))
		return -EFAULT;
	return rc;
}

static long zcrypt_compat_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	if (cmd == ICARSAMODEXPO)
		return trans_modexpo32(filp, cmd, arg);
	if (cmd == ICARSACRT)
		return trans_modexpo_crt32(filp, cmd, arg);
	if (cmd == ZSECSENDCPRB)
		return trans_xcRB32(filp, cmd, arg);
	return zcrypt_unlocked_ioctl(filp, cmd, arg);
}
#endif

/*
 * Misc device file operations.
 */
static const struct file_operations zcrypt_fops = {
	.owner		= THIS_MODULE,
	.read		= zcrypt_read,
	.write		= zcrypt_write,
	.unlocked_ioctl	= zcrypt_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= zcrypt_compat_ioctl,
#endif
	.open		= zcrypt_open,
	.release	= zcrypt_release,
	.llseek		= no_llseek,
};

/*
 * Misc device.
 */
static struct miscdevice zcrypt_misc_device = {
	.minor	    = MISC_DYNAMIC_MINOR,
	.name	    = "z90crypt",
	.fops	    = &zcrypt_fops,
};

static int zcrypt_rng_device_count;
static u32 *zcrypt_rng_buffer;
static int zcrypt_rng_buffer_index;
static DEFINE_MUTEX(zcrypt_rng_mutex);

static int zcrypt_rng_data_read(struct hwrng *rng, u32 *data)
{
	int rc;

	/*
	 * We don't need locking here because the RNG API guarantees serialized
	 * read method calls.
	 */
	if (zcrypt_rng_buffer_index == 0) {
		rc = zcrypt_rng((char *) zcrypt_rng_buffer);
		/* on failure: retry once again after a requested rescan */
		if ((rc == -ENODEV) && (zcrypt_process_rescan()))
			rc = zcrypt_rng((char *) zcrypt_rng_buffer);
		if (rc < 0)
			return -EIO;
		zcrypt_rng_buffer_index = rc / sizeof(*data);
	}
	*data = zcrypt_rng_buffer[--zcrypt_rng_buffer_index];
	return sizeof(*data);
}

static struct hwrng zcrypt_rng_dev = {
	.name		= "zcrypt",
	.data_read	= zcrypt_rng_data_read,
	.quality	= 990,
};

int zcrypt_rng_device_add(void)
{
	int rc = 0;

	mutex_lock(&zcrypt_rng_mutex);
	if (zcrypt_rng_device_count == 0) {
		zcrypt_rng_buffer = (u32 *) get_zeroed_page(GFP_KERNEL);
		if (!zcrypt_rng_buffer) {
			rc = -ENOMEM;
			goto out;
		}
		zcrypt_rng_buffer_index = 0;
		if (!zcrypt_hwrng_seed)
			zcrypt_rng_dev.quality = 0;
		rc = hwrng_register(&zcrypt_rng_dev);
		if (rc)
			goto out_free;
		zcrypt_rng_device_count = 1;
	} else
		zcrypt_rng_device_count++;
	mutex_unlock(&zcrypt_rng_mutex);
	return 0;

out_free:
	free_page((unsigned long) zcrypt_rng_buffer);
out:
	mutex_unlock(&zcrypt_rng_mutex);
	return rc;
}

void zcrypt_rng_device_remove(void)
{
	mutex_lock(&zcrypt_rng_mutex);
	zcrypt_rng_device_count--;
	if (zcrypt_rng_device_count == 0) {
		hwrng_unregister(&zcrypt_rng_dev);
		free_page((unsigned long) zcrypt_rng_buffer);
	}
	mutex_unlock(&zcrypt_rng_mutex);
}

int __init zcrypt_debug_init(void)
{
	zcrypt_dbf_info = debug_register("zcrypt", 1, 1,
					 DBF_MAX_SPRINTF_ARGS * sizeof(long));
	debug_register_view(zcrypt_dbf_info, &debug_sprintf_view);
	debug_set_level(zcrypt_dbf_info, DBF_ERR);

	return 0;
}

void zcrypt_debug_exit(void)
{
	debug_unregister(zcrypt_dbf_info);
}

/**
 * zcrypt_api_init(): Module initialization.
 *
 * The module initialization code.
 */
int __init zcrypt_api_init(void)
{
	int rc;

	rc = zcrypt_debug_init();
	if (rc)
		goto out;

	/* Register the request sprayer. */
	rc = misc_register(&zcrypt_misc_device);
	if (rc < 0)
		goto out;

	zcrypt_msgtype6_init();
	zcrypt_msgtype50_init();
	return 0;

out:
	return rc;
}

/**
 * zcrypt_api_exit(): Module termination.
 *
 * The module termination code.
 */
void __exit zcrypt_api_exit(void)
{
	misc_deregister(&zcrypt_misc_device);
	zcrypt_msgtype6_exit();
	zcrypt_msgtype50_exit();
	zcrypt_debug_exit();
}

module_init(zcrypt_api_init);
module_exit(zcrypt_api_exit);
