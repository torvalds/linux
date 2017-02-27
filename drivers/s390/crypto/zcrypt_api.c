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
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
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
module_param_named(hwrng_seed, zcrypt_hwrng_seed, int, S_IRUSR|S_IRGRP);
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
		ZCRYPT_DBF(DBF_INFO, "rescan count=%07d",
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
				       unsigned weight, unsigned pref_weight)
{
	if (!pref_zc)
		return false;
	weight += atomic_read(&zc->load);
	pref_weight += atomic_read(&pref_zc->load);
	if (weight == pref_weight)
		return atomic_read(&zc->card->total_request_count) >
			atomic_read(&pref_zc->card->total_request_count);
	return weight > pref_weight;
}

static inline bool zcrypt_queue_compare(struct zcrypt_queue *zq,
					struct zcrypt_queue *pref_zq,
					unsigned weight, unsigned pref_weight)
{
	if (!pref_zq)
		return false;
	weight += atomic_read(&zq->load);
	pref_weight += atomic_read(&pref_zq->load);
	if (weight == pref_weight)
		return &zq->queue->total_request_count >
			&pref_zq->queue->total_request_count;
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

static long zcrypt_send_cprb(struct ica_xcRB *xcRB)
{
	struct zcrypt_card *zc, *pref_zc;
	struct zcrypt_queue *zq, *pref_zq;
	struct ap_message ap_msg;
	unsigned int weight, pref_weight;
	unsigned int func_code;
	unsigned short *domain;
	int qid = 0, rc = -ENODEV;

	trace_s390_zcrypt_req(xcRB, TB_ZSECSENDCPRB);

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
	trace_s390_zcrypt_rep(xcRB, func_code, rc,
			      AP_QID_CARD(qid), AP_QID_QUEUE(qid));
	return rc;
}

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

	target_num = (unsigned short) xcrb->targets_num;

	/* empty list indicates autoselect (all available targets) */
	targets = NULL;
	if (target_num != 0) {
		struct ep11_target_dev __user *uptr;

		targets = kcalloc(target_num, sizeof(*targets), GFP_KERNEL);
		if (!targets) {
			rc = -ENOMEM;
			goto out;
		}

		uptr = (struct ep11_target_dev __force __user *) xcrb->targets;
		if (copy_from_user(targets, uptr,
				   target_num * sizeof(*targets))) {
			rc = -EFAULT;
			goto out;
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

	if (!pref_zq)
		return -ENODEV;

	qid = pref_zq->queue->qid;
	rc = pref_zq->ops->rng(pref_zq, buffer, &ap_msg);

	spin_lock(&zcrypt_list_lock);
	zcrypt_drop_queue(pref_zc, pref_zq, weight);
	spin_unlock(&zcrypt_list_lock);

out:
	trace_s390_zcrypt_rep(buffer, func_code, rc,
			      AP_QID_CARD(qid), AP_QID_QUEUE(qid));
	return rc;
}

static void zcrypt_device_status_mask(struct zcrypt_device_matrix *matrix)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	struct zcrypt_device_status *stat;

	memset(matrix, 0, sizeof(*matrix));
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			stat = matrix->device;
			stat += AP_QID_CARD(zq->queue->qid) * MAX_ZDEV_DOMAINS;
			stat += AP_QID_QUEUE(zq->queue->qid);
			stat->hwtype = zc->card->ap_dev.device_type;
			stat->functions = zc->card->functions >> 26;
			stat->qid = zq->queue->qid;
			stat->online = zq->online ? 0x01 : 0x00;
		}
	}
	spin_unlock(&zcrypt_list_lock);
}
EXPORT_SYMBOL(zcrypt_device_status_mask);

static void zcrypt_status_mask(char status[AP_DEVICES])
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;

	memset(status, 0, sizeof(char) * AP_DEVICES);
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			status[AP_QID_CARD(zq->queue->qid)] =
				zc->online ? zc->user_space_type : 0x0d;
		}
	}
	spin_unlock(&zcrypt_list_lock);
}

static void zcrypt_qdepth_mask(char qdepth[AP_DEVICES])
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;

	memset(qdepth, 0, sizeof(char)	* AP_DEVICES);
	spin_lock(&zcrypt_list_lock);
	local_bh_disable();
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			spin_lock(&zq->queue->lock);
			qdepth[AP_QID_CARD(zq->queue->qid)] =
				zq->queue->pendingq_count +
				zq->queue->requestq_count;
			spin_unlock(&zq->queue->lock);
		}
	}
	local_bh_enable();
	spin_unlock(&zcrypt_list_lock);
}

static void zcrypt_perdev_reqcnt(int reqcnt[AP_DEVICES])
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;

	memset(reqcnt, 0, sizeof(int) * AP_DEVICES);
	spin_lock(&zcrypt_list_lock);
	local_bh_disable();
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			spin_lock(&zq->queue->lock);
			reqcnt[AP_QID_CARD(zq->queue->qid)] =
				zq->queue->total_request_count;
			spin_unlock(&zq->queue->lock);
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

static int zcrypt_count_type(int type)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;
	int device_count;

	device_count = 0;
	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		if (zc->card->id != type)
			continue;
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			device_count++;
		}
	}
	spin_unlock(&zcrypt_list_lock);
	return device_count;
}

/**
 * zcrypt_ica_status(): Old, depracted combi status call.
 *
 * Old, deprecated combi status call.
 */
static long zcrypt_ica_status(struct file *filp, unsigned long arg)
{
	struct ica_z90_status *pstat;
	int ret;

	pstat = kzalloc(sizeof(*pstat), GFP_KERNEL);
	if (!pstat)
		return -ENOMEM;
	pstat->totalcount = zcrypt_device_count;
	pstat->leedslitecount = zcrypt_count_type(ZCRYPT_PCICA);
	pstat->leeds2count = zcrypt_count_type(ZCRYPT_PCICC);
	pstat->requestqWaitCount = zcrypt_requestq_count();
	pstat->pendingqWaitCount = zcrypt_pendingq_count();
	pstat->totalOpenCount = atomic_read(&zcrypt_open_count);
	pstat->cryptoDomain = ap_domain_index;
	zcrypt_status_mask(pstat->status);
	zcrypt_qdepth_mask(pstat->qdepth);
	ret = 0;
	if (copy_to_user((void __user *) arg, pstat, sizeof(*pstat)))
		ret = -EFAULT;
	kfree(pstat);
	return ret;
}

static long zcrypt_unlocked_ioctl(struct file *filp, unsigned int cmd,
				  unsigned long arg)
{
	int rc;

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
		if (rc)
			return rc;
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
		if (rc)
			return rc;
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
		if (copy_to_user(uxcrb, &xcrb, sizeof(xcrb)))
			return -EFAULT;
		return rc;
	}
	case ZDEVICESTATUS: {
		struct zcrypt_device_matrix *device_status;

		device_status = kzalloc(sizeof(struct zcrypt_device_matrix),
					GFP_KERNEL);
		if (!device_status)
			return -ENOMEM;

		zcrypt_device_status_mask(device_status);

		if (copy_to_user((char __user *) arg, device_status,
				 sizeof(struct zcrypt_device_matrix))) {
			kfree(device_status);
			return -EFAULT;
		}

		kfree(device_status);
		return 0;
	}
	case Z90STAT_STATUS_MASK: {
		char status[AP_DEVICES];
		zcrypt_status_mask(status);
		if (copy_to_user((char __user *) arg, status,
				 sizeof(char) * AP_DEVICES))
			return -EFAULT;
		return 0;
	}
	case Z90STAT_QDEPTH_MASK: {
		char qdepth[AP_DEVICES];
		zcrypt_qdepth_mask(qdepth);
		if (copy_to_user((char __user *) arg, qdepth,
				 sizeof(char) * AP_DEVICES))
			return -EFAULT;
		return 0;
	}
	case Z90STAT_PERDEV_REQCNT: {
		int reqcnt[AP_DEVICES];
		zcrypt_perdev_reqcnt(reqcnt);
		if (copy_to_user((int __user *) arg, reqcnt,
				 sizeof(int) * AP_DEVICES))
			return -EFAULT;
		return 0;
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
	 * Deprecated ioctls. Don't add another device count ioctl,
	 * you can count them yourself in the user space with the
	 * output of the Z90STAT_STATUS_MASK ioctl.
	 */
	case ICAZ90STATUS:
		return zcrypt_ica_status(filp, arg);
	case Z90STAT_TOTALCOUNT:
		return put_user(zcrypt_device_count, (int __user *) arg);
	case Z90STAT_PCICACOUNT:
		return put_user(zcrypt_count_type(ZCRYPT_PCICA),
				(int __user *) arg);
	case Z90STAT_PCICCCOUNT:
		return put_user(zcrypt_count_type(ZCRYPT_PCICC),
				(int __user *) arg);
	case Z90STAT_PCIXCCMCL2COUNT:
		return put_user(zcrypt_count_type(ZCRYPT_PCIXCC_MCL2),
				(int __user *) arg);
	case Z90STAT_PCIXCCMCL3COUNT:
		return put_user(zcrypt_count_type(ZCRYPT_PCIXCC_MCL3),
				(int __user *) arg);
	case Z90STAT_PCIXCCCOUNT:
		return put_user(zcrypt_count_type(ZCRYPT_PCIXCC_MCL2) +
				zcrypt_count_type(ZCRYPT_PCIXCC_MCL3),
				(int __user *) arg);
	case Z90STAT_CEX2CCOUNT:
		return put_user(zcrypt_count_type(ZCRYPT_CEX2C),
				(int __user *) arg);
	case Z90STAT_CEX2ACOUNT:
		return put_user(zcrypt_count_type(ZCRYPT_CEX2A),
				(int __user *) arg);
	default:
		/* unknown ioctl number */
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
	crt64.outputdata=  compat_ptr(crt32.outputdata);
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
	unsigned char	padding1[16 - sizeof (compat_uptr_t)];
	compat_uptr_t	request_control_blk_addr;
	unsigned int	request_data_length;
	char		padding2[16 - sizeof (compat_uptr_t)];
	compat_uptr_t	request_data_address;
	unsigned int	reply_control_blk_length;
	char		padding3[16 - sizeof (compat_uptr_t)];
	compat_uptr_t	reply_control_blk_addr;
	unsigned int	reply_data_length;
	char		padding4[16 - sizeof (compat_uptr_t)];
	compat_uptr_t	reply_data_addr;
	unsigned short	priority_window;
	unsigned int	status;
} __attribute__((packed));

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

/*
 * Deprecated /proc entry support.
 */
static struct proc_dir_entry *zcrypt_entry;

static void sprintcl(struct seq_file *m, unsigned char *addr, unsigned int len)
{
	int i;

	for (i = 0; i < len; i++)
		seq_printf(m, "%01x", (unsigned int) addr[i]);
	seq_putc(m, ' ');
}

static void sprintrw(struct seq_file *m, unsigned char *addr, unsigned int len)
{
	int inl, c, cx;

	seq_printf(m, "	   ");
	inl = 0;
	for (c = 0; c < (len / 16); c++) {
		sprintcl(m, addr+inl, 16);
		inl += 16;
	}
	cx = len%16;
	if (cx) {
		sprintcl(m, addr+inl, cx);
		inl += cx;
	}
	seq_putc(m, '\n');
}

static void sprinthx(unsigned char *title, struct seq_file *m,
		     unsigned char *addr, unsigned int len)
{
	int inl, r, rx;

	seq_printf(m, "\n%s\n", title);
	inl = 0;
	for (r = 0; r < (len / 64); r++) {
		sprintrw(m, addr+inl, 64);
		inl += 64;
	}
	rx = len % 64;
	if (rx) {
		sprintrw(m, addr+inl, rx);
		inl += rx;
	}
	seq_putc(m, '\n');
}

static void sprinthx4(unsigned char *title, struct seq_file *m,
		      unsigned int *array, unsigned int len)
{
	seq_printf(m, "\n%s\n", title);
	seq_hex_dump(m, "    ", DUMP_PREFIX_NONE, 32, 4, array, len, false);
	seq_putc(m, '\n');
}

static int zcrypt_proc_show(struct seq_file *m, void *v)
{
	char workarea[sizeof(int) * AP_DEVICES];

	seq_printf(m, "\nzcrypt version: %d.%d.%d\n",
		   ZCRYPT_VERSION, ZCRYPT_RELEASE, ZCRYPT_VARIANT);
	seq_printf(m, "Cryptographic domain: %d\n", ap_domain_index);
	seq_printf(m, "Total device count: %d\n", zcrypt_device_count);
	seq_printf(m, "PCICA count: %d\n", zcrypt_count_type(ZCRYPT_PCICA));
	seq_printf(m, "PCICC count: %d\n", zcrypt_count_type(ZCRYPT_PCICC));
	seq_printf(m, "PCIXCC MCL2 count: %d\n",
		   zcrypt_count_type(ZCRYPT_PCIXCC_MCL2));
	seq_printf(m, "PCIXCC MCL3 count: %d\n",
		   zcrypt_count_type(ZCRYPT_PCIXCC_MCL3));
	seq_printf(m, "CEX2C count: %d\n", zcrypt_count_type(ZCRYPT_CEX2C));
	seq_printf(m, "CEX2A count: %d\n", zcrypt_count_type(ZCRYPT_CEX2A));
	seq_printf(m, "CEX3C count: %d\n", zcrypt_count_type(ZCRYPT_CEX3C));
	seq_printf(m, "CEX3A count: %d\n", zcrypt_count_type(ZCRYPT_CEX3A));
	seq_printf(m, "requestq count: %d\n", zcrypt_requestq_count());
	seq_printf(m, "pendingq count: %d\n", zcrypt_pendingq_count());
	seq_printf(m, "Total open handles: %d\n\n",
		   atomic_read(&zcrypt_open_count));
	zcrypt_status_mask(workarea);
	sprinthx("Online devices: 1=PCICA 2=PCICC 3=PCIXCC(MCL2) "
		 "4=PCIXCC(MCL3) 5=CEX2C 6=CEX2A 7=CEX3C 8=CEX3A",
		 m, workarea, AP_DEVICES);
	zcrypt_qdepth_mask(workarea);
	sprinthx("Waiting work element counts", m, workarea, AP_DEVICES);
	zcrypt_perdev_reqcnt((int *) workarea);
	sprinthx4("Per-device successfully completed request counts",
		  m, (unsigned int *) workarea, AP_DEVICES);
	return 0;
}

static int zcrypt_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, zcrypt_proc_show, NULL);
}

static void zcrypt_disable_card(int index)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;

	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			zq->online = 0;
			ap_flush_queue(zq->queue);
		}
	}
	spin_unlock(&zcrypt_list_lock);
}

static void zcrypt_enable_card(int index)
{
	struct zcrypt_card *zc;
	struct zcrypt_queue *zq;

	spin_lock(&zcrypt_list_lock);
	for_each_zcrypt_card(zc) {
		for_each_zcrypt_queue(zq, zc) {
			if (AP_QID_QUEUE(zq->queue->qid) != ap_domain_index)
				continue;
			zq->online = 1;
			ap_flush_queue(zq->queue);
		}
	}
	spin_unlock(&zcrypt_list_lock);
}

static ssize_t zcrypt_proc_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *pos)
{
	unsigned char *lbuf, *ptr;
	size_t local_count;
	int j;

	if (count <= 0)
		return 0;

#define LBUFSIZE 1200UL
	lbuf = kmalloc(LBUFSIZE, GFP_KERNEL);
	if (!lbuf)
		return 0;

	local_count = min(LBUFSIZE - 1, count);
	if (copy_from_user(lbuf, buffer, local_count) != 0) {
		kfree(lbuf);
		return -EFAULT;
	}
	lbuf[local_count] = '\0';

	ptr = strstr(lbuf, "Online devices");
	if (!ptr)
		goto out;
	ptr = strstr(ptr, "\n");
	if (!ptr)
		goto out;
	ptr++;

	if (strstr(ptr, "Waiting work element counts") == NULL)
		goto out;

	for (j = 0; j < 64 && *ptr; ptr++) {
		/*
		 * '0' for no device, '1' for PCICA, '2' for PCICC,
		 * '3' for PCIXCC_MCL2, '4' for PCIXCC_MCL3,
		 * '5' for CEX2C and '6' for CEX2A'
		 * '7' for CEX3C and '8' for CEX3A
		 */
		if (*ptr >= '0' && *ptr <= '8')
			j++;
		else if (*ptr == 'd' || *ptr == 'D')
			zcrypt_disable_card(j++);
		else if (*ptr == 'e' || *ptr == 'E')
			zcrypt_enable_card(j++);
		else if (*ptr != ' ' && *ptr != '\t')
			break;
	}
out:
	kfree(lbuf);
	return count;
}

static const struct file_operations zcrypt_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= zcrypt_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= zcrypt_proc_write,
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
		zcrypt_rng_buffer_index = rc / sizeof *data;
	}
	*data = zcrypt_rng_buffer[--zcrypt_rng_buffer_index];
	return sizeof *data;
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

	atomic_set(&zcrypt_rescan_req, 0);

	/* Register the request sprayer. */
	rc = misc_register(&zcrypt_misc_device);
	if (rc < 0)
		goto out;

	/* Set up the proc file system */
	zcrypt_entry = proc_create("driver/z90crypt", 0644, NULL,
				   &zcrypt_proc_fops);
	if (!zcrypt_entry) {
		rc = -ENOMEM;
		goto out_misc;
	}

	zcrypt_msgtype6_init();
	zcrypt_msgtype50_init();
	return 0;

out_misc:
	misc_deregister(&zcrypt_misc_device);
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
	remove_proc_entry("driver/z90crypt", NULL);
	misc_deregister(&zcrypt_misc_device);
	zcrypt_msgtype6_exit();
	zcrypt_msgtype50_exit();
	zcrypt_debug_exit();
}

module_init(zcrypt_api_init);
module_exit(zcrypt_api_exit);
