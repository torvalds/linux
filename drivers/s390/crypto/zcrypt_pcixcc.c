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
#include <asm/uaccess.h>

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

#define PCIXCC_MCL2_SPEED_RATING	7870
#define PCIXCC_MCL3_SPEED_RATING	7870
#define CEX2C_SPEED_RATING		7000
#define CEX3C_SPEED_RATING		6500

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

static struct ap_device_id zcrypt_pcixcc_ids[] = {
	{ AP_DEVICE(AP_DEVICE_TYPE_PCIXCC) },
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX2C) },
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX3C) },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_pcixcc_ids);
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("PCIXCC Cryptographic Coprocessor device driver, " \
		   "Copyright IBM Corp. 2001, 2012");
MODULE_LICENSE("GPL");

static int zcrypt_pcixcc_probe(struct ap_device *ap_dev);
static void zcrypt_pcixcc_remove(struct ap_device *ap_dev);

static struct ap_driver zcrypt_pcixcc_driver = {
	.probe = zcrypt_pcixcc_probe,
	.remove = zcrypt_pcixcc_remove,
	.ids = zcrypt_pcixcc_ids,
	.request_timeout = PCIXCC_CLEANUP_TIME,
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
		msleep(300);
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

	zdev = zcrypt_device_alloc(PCIXCC_MAX_XCRB_MESSAGE_SIZE);
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
		zdev->ops = zcrypt_msgtype_request(MSGTYPE06_NAME,
						   MSGTYPE06_VARIANT_DEFAULT);
	else
		zdev->ops = zcrypt_msgtype_request(MSGTYPE06_NAME,
						   MSGTYPE06_VARIANT_NORNG);
	ap_dev->reply = &zdev->reply;
	ap_dev->private = zdev;
	rc = zcrypt_device_register(zdev);
	if (rc)
		goto out_free;
	return 0;

 out_free:
	ap_dev->private = NULL;
	zcrypt_msgtype_release(zdev->ops);
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
	struct zcrypt_ops *zops = zdev->ops;

	zcrypt_device_unregister(zdev);
	zcrypt_msgtype_release(zops);
}

int __init zcrypt_pcixcc_init(void)
{
	return ap_driver_register(&zcrypt_pcixcc_driver, THIS_MODULE, "pcixcc");
}

void zcrypt_pcixcc_exit(void)
{
	ap_driver_unregister(&zcrypt_pcixcc_driver);
}

module_init(zcrypt_pcixcc_init);
module_exit(zcrypt_pcixcc_exit);
