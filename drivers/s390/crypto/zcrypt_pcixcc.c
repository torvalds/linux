// SPDX-License-Identifier: GPL-2.0+
/*
 *  zcrypt 2.1.0
 *
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
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
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/mod_devicetable.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_msgtype6.h"
#include "zcrypt_pcixcc.h"
#include "zcrypt_cca_key.h"

#define PCIXCC_MIN_MOD_SIZE	 16	/*  128 bits	*/
#define PCIXCC_MIN_MOD_SIZE_OLD	 64	/*  512 bits	*/
#define PCIXCC_MAX_MOD_SIZE	256	/* 2048 bits	*/
#define CEX3C_MIN_MOD_SIZE	PCIXCC_MIN_MOD_SIZE
#define CEX3C_MAX_MOD_SIZE	512	/* 4096 bits	*/

#define PCIXCC_MAX_ICA_MESSAGE_SIZE 0x77c  /* max size type6 v2 crt message */
#define PCIXCC_MAX_ICA_RESPONSE_SIZE 0x77c /* max size type86 v2 reply	    */

#define PCIXCC_MAX_XCRB_MESSAGE_SIZE (12*1024)

#define PCIXCC_CLEANUP_TIME	(15*HZ)

#define CEIL4(x) ((((x)+3)/4)*4)

struct response_type {
	struct completion work;
	int type;
};
#define PCIXCC_RESPONSE_TYPE_ICA  0
#define PCIXCC_RESPONSE_TYPE_XCRB 1

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("PCIXCC Cryptographic Coprocessor device driver, " \
		   "Copyright IBM Corp. 2001, 2012");
MODULE_LICENSE("GPL");

static struct ap_device_id zcrypt_pcixcc_card_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_PCIXCC,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX2C,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX3C,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_pcixcc_card_ids);

static struct ap_device_id zcrypt_pcixcc_queue_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_PCIXCC,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX2C,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX3C,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_pcixcc_queue_ids);

/**
 * Large random number detection function. Its sends a message to a pcixcc
 * card to find out if large random numbers are supported.
 * @ap_dev: pointer to the AP device.
 *
 * Returns 1 if large random numbers are supported, 0 if not and < 0 on error.
 */
static int zcrypt_pcixcc_rng_supported(struct ap_queue *aq)
{
	struct ap_message ap_msg;
	unsigned long long psmid;
	unsigned int domain;
	struct {
		struct type86_hdr hdr;
		struct type86_fmt2_ext fmt2;
		struct CPRBX cprbx;
	} __attribute__((packed)) *reply;
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		char function_code[2];
		short int rule_length;
		char rule[8];
		short int verb_length;
		short int key_length;
	} __packed * msg;
	int rc, i;

	ap_init_message(&ap_msg);
	ap_msg.message = (void *) get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.message)
		return -ENOMEM;

	rng_type6CPRB_msgX(&ap_msg, 4, &domain);

	msg = ap_msg.message;
	msg->cprbx.domain = AP_QID_QUEUE(aq->qid);

	rc = ap_send(aq->qid, 0x0102030405060708ULL, ap_msg.message,
		     ap_msg.length);
	if (rc)
		goto out_free;

	/* Wait for the test message to complete. */
	for (i = 0; i < 2 * HZ; i++) {
		msleep(1000 / HZ);
		rc = ap_recv(aq->qid, &psmid, ap_msg.message, 4096);
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
 * Probe function for PCIXCC/CEX2C card devices. It always accepts the
 * AP device since the bus_match already checked the hardware type. The
 * PCIXCC cards come in two flavours: micro code level 2 and micro code
 * level 3. This is checked by sending a test message to the device.
 * @ap_dev: pointer to the AP card device.
 */
static int zcrypt_pcixcc_card_probe(struct ap_device *ap_dev)
{
	/*
	 * Normalized speed ratings per crypto adapter
	 * MEX_1k, MEX_2k, MEX_4k, CRT_1k, CRT_2k, CRT_4k, RNG, SECKEY
	 */
	static const int CEX2C_SPEED_IDX[] = {
		1000, 1400, 2400, 1100, 1500, 2600, 100, 12};
	static const int CEX3C_SPEED_IDX[] = {
		500,  700, 1400,  550,	800, 1500,  80, 10};

	struct ap_card *ac = to_ap_card(&ap_dev->device);
	struct zcrypt_card *zc;
	int rc = 0;

	zc = zcrypt_card_alloc();
	if (!zc)
		return -ENOMEM;
	zc->card = ac;
	ac->private = zc;
	switch (ac->ap_dev.device_type) {
	case AP_DEVICE_TYPE_CEX2C:
		zc->user_space_type = ZCRYPT_CEX2C;
		zc->type_string = "CEX2C";
		memcpy(zc->speed_rating, CEX2C_SPEED_IDX,
		       sizeof(CEX2C_SPEED_IDX));
		zc->min_mod_size = PCIXCC_MIN_MOD_SIZE;
		zc->max_mod_size = PCIXCC_MAX_MOD_SIZE;
		zc->max_exp_bit_length = PCIXCC_MAX_MOD_SIZE;
		break;
	case AP_DEVICE_TYPE_CEX3C:
		zc->user_space_type = ZCRYPT_CEX3C;
		zc->type_string = "CEX3C";
		memcpy(zc->speed_rating, CEX3C_SPEED_IDX,
		       sizeof(CEX3C_SPEED_IDX));
		zc->min_mod_size = CEX3C_MIN_MOD_SIZE;
		zc->max_mod_size = CEX3C_MAX_MOD_SIZE;
		zc->max_exp_bit_length = CEX3C_MAX_MOD_SIZE;
		break;
	default:
		zcrypt_card_free(zc);
		return -ENODEV;
	}
	zc->online = 1;

	rc = zcrypt_card_register(zc);
	if (rc) {
		ac->private = NULL;
		zcrypt_card_free(zc);
	}

	return rc;
}

/**
 * This is called to remove the PCIXCC/CEX2C card driver information
 * if an AP card device is removed.
 */
static void zcrypt_pcixcc_card_remove(struct ap_device *ap_dev)
{
	struct zcrypt_card *zc = to_ap_card(&ap_dev->device)->private;

	if (zc)
		zcrypt_card_unregister(zc);
}

static struct ap_driver zcrypt_pcixcc_card_driver = {
	.probe = zcrypt_pcixcc_card_probe,
	.remove = zcrypt_pcixcc_card_remove,
	.ids = zcrypt_pcixcc_card_ids,
};

/**
 * Probe function for PCIXCC/CEX2C queue devices. It always accepts the
 * AP device since the bus_match already checked the hardware type. The
 * PCIXCC cards come in two flavours: micro code level 2 and micro code
 * level 3. This is checked by sending a test message to the device.
 * @ap_dev: pointer to the AP card device.
 */
static int zcrypt_pcixcc_queue_probe(struct ap_device *ap_dev)
{
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);
	struct zcrypt_queue *zq;
	int rc;

	zq = zcrypt_queue_alloc(PCIXCC_MAX_XCRB_MESSAGE_SIZE);
	if (!zq)
		return -ENOMEM;
	zq->queue = aq;
	zq->online = 1;
	atomic_set(&zq->load, 0);
	rc = zcrypt_pcixcc_rng_supported(aq);
	if (rc < 0) {
		zcrypt_queue_free(zq);
		return rc;
	}
	if (rc)
		zq->ops = zcrypt_msgtype(MSGTYPE06_NAME,
					 MSGTYPE06_VARIANT_DEFAULT);
	else
		zq->ops = zcrypt_msgtype(MSGTYPE06_NAME,
					 MSGTYPE06_VARIANT_NORNG);
	ap_queue_init_reply(aq, &zq->reply);
	aq->request_timeout = PCIXCC_CLEANUP_TIME,
	aq->private = zq;
	rc = zcrypt_queue_register(zq);
	if (rc) {
		aq->private = NULL;
		zcrypt_queue_free(zq);
	}
	return rc;
}

/**
 * This is called to remove the PCIXCC/CEX2C queue driver information
 * if an AP queue device is removed.
 */
static void zcrypt_pcixcc_queue_remove(struct ap_device *ap_dev)
{
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);
	struct zcrypt_queue *zq = aq->private;

	ap_queue_remove(aq);
	if (zq)
		zcrypt_queue_unregister(zq);
}

static struct ap_driver zcrypt_pcixcc_queue_driver = {
	.probe = zcrypt_pcixcc_queue_probe,
	.remove = zcrypt_pcixcc_queue_remove,
	.suspend = ap_queue_suspend,
	.resume = ap_queue_resume,
	.ids = zcrypt_pcixcc_queue_ids,
};

int __init zcrypt_pcixcc_init(void)
{
	int rc;

	rc = ap_driver_register(&zcrypt_pcixcc_card_driver,
				THIS_MODULE, "pcixcccard");
	if (rc)
		return rc;

	rc = ap_driver_register(&zcrypt_pcixcc_queue_driver,
				THIS_MODULE, "pcixccqueue");
	if (rc)
		ap_driver_unregister(&zcrypt_pcixcc_card_driver);

	return rc;
}

void zcrypt_pcixcc_exit(void)
{
	ap_driver_unregister(&zcrypt_pcixcc_queue_driver);
	ap_driver_unregister(&zcrypt_pcixcc_card_driver);
}

module_init(zcrypt_pcixcc_init);
module_exit(zcrypt_pcixcc_exit);
