// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright IBM Corp. 2001, 2018
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
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
#include "zcrypt_cex2c.h"
#include "zcrypt_cca_key.h"
#include "zcrypt_ccamisc.h"

#define CEX2C_MIN_MOD_SIZE	 16	/*  128 bits	*/
#define CEX2C_MAX_MOD_SIZE	256	/* 2048 bits	*/
#define CEX3C_MIN_MOD_SIZE	 16	/*  128 bits	*/
#define CEX3C_MAX_MOD_SIZE	512	/* 4096 bits	*/
#define CEX2C_MAX_XCRB_MESSAGE_SIZE (12 * 1024)
#define CEX2C_CLEANUP_TIME	(15 * HZ)

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("CEX2C/CEX3C Cryptographic Coprocessor device driver, " \
		   "Copyright IBM Corp. 2001, 2018");
MODULE_LICENSE("GPL");

static struct ap_device_id zcrypt_cex2c_card_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX2C,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX3C,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex2c_card_ids);

static struct ap_device_id zcrypt_cex2c_queue_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX2C,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX3C,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex2c_queue_ids);

/*
 * CCA card additional device attributes
 */
static ssize_t cca_serialnr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct zcrypt_card *zc = dev_get_drvdata(dev);
	struct cca_info ci;
	struct ap_card *ac = to_ap_card(dev);

	memset(&ci, 0, sizeof(ci));

	if (ap_domain_index >= 0)
		cca_get_info(ac->id, ap_domain_index, &ci, zc->online);

	return scnprintf(buf, PAGE_SIZE, "%s\n", ci.serial);
}

static struct device_attribute dev_attr_cca_serialnr =
	__ATTR(serialnr, 0444, cca_serialnr_show, NULL);

static struct attribute *cca_card_attrs[] = {
	&dev_attr_cca_serialnr.attr,
	NULL,
};

static const struct attribute_group cca_card_attr_grp = {
	.attrs = cca_card_attrs,
};

 /*
  * CCA queue additional device attributes
  */
static ssize_t cca_mkvps_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct zcrypt_queue *zq = dev_get_drvdata(dev);
	int n = 0;
	struct cca_info ci;
	static const char * const cao_state[] = { "invalid", "valid" };
	static const char * const new_state[] = { "empty", "partial", "full" };

	memset(&ci, 0, sizeof(ci));

	cca_get_info(AP_QID_CARD(zq->queue->qid),
		     AP_QID_QUEUE(zq->queue->qid),
		     &ci, zq->online);

	if (ci.new_aes_mk_state >= '1' && ci.new_aes_mk_state <= '3')
		n = scnprintf(buf, PAGE_SIZE, "AES NEW: %s 0x%016llx\n",
			      new_state[ci.new_aes_mk_state - '1'],
			      ci.new_aes_mkvp);
	else
		n = scnprintf(buf, PAGE_SIZE, "AES NEW: - -\n");

	if (ci.cur_aes_mk_state >= '1' && ci.cur_aes_mk_state <= '2')
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "AES CUR: %s 0x%016llx\n",
			       cao_state[ci.cur_aes_mk_state - '1'],
			       ci.cur_aes_mkvp);
	else
		n += scnprintf(buf + n, PAGE_SIZE - n, "AES CUR: - -\n");

	if (ci.old_aes_mk_state >= '1' && ci.old_aes_mk_state <= '2')
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "AES OLD: %s 0x%016llx\n",
			       cao_state[ci.old_aes_mk_state - '1'],
			       ci.old_aes_mkvp);
	else
		n += scnprintf(buf + n, PAGE_SIZE - n, "AES OLD: - -\n");

	if (ci.new_apka_mk_state >= '1' && ci.new_apka_mk_state <= '3')
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "APKA NEW: %s 0x%016llx\n",
			       new_state[ci.new_apka_mk_state - '1'],
			       ci.new_apka_mkvp);
	else
		n += scnprintf(buf + n, PAGE_SIZE - n, "APKA NEW: - -\n");

	if (ci.cur_apka_mk_state >= '1' && ci.cur_apka_mk_state <= '2')
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "APKA CUR: %s 0x%016llx\n",
			       cao_state[ci.cur_apka_mk_state - '1'],
			       ci.cur_apka_mkvp);
	else
		n += scnprintf(buf + n, PAGE_SIZE - n, "APKA CUR: - -\n");

	if (ci.old_apka_mk_state >= '1' && ci.old_apka_mk_state <= '2')
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "APKA OLD: %s 0x%016llx\n",
			       cao_state[ci.old_apka_mk_state - '1'],
			       ci.old_apka_mkvp);
	else
		n += scnprintf(buf + n, PAGE_SIZE - n, "APKA OLD: - -\n");

	return n;
}

static struct device_attribute dev_attr_cca_mkvps =
	__ATTR(mkvps, 0444, cca_mkvps_show, NULL);

static struct attribute *cca_queue_attrs[] = {
	&dev_attr_cca_mkvps.attr,
	NULL,
};

static const struct attribute_group cca_queue_attr_grp = {
	.attrs = cca_queue_attrs,
};

/*
 * Large random number detection function. Its sends a message to a CEX2C/CEX3C
 * card to find out if large random numbers are supported.
 * @ap_dev: pointer to the AP device.
 *
 * Returns 1 if large random numbers are supported, 0 if not and < 0 on error.
 */
static int zcrypt_cex2c_rng_supported(struct ap_queue *aq)
{
	struct ap_message ap_msg;
	unsigned long long psmid;
	unsigned int domain;
	struct {
		struct type86_hdr hdr;
		struct type86_fmt2_ext fmt2;
		struct CPRBX cprbx;
	} __packed *reply;
	struct {
		struct type6_hdr hdr;
		struct CPRBX cprbx;
		char function_code[2];
		short int rule_length;
		char rule[8];
		short int verb_length;
		short int key_length;
	} __packed *msg;
	int rc, i;

	ap_init_message(&ap_msg);
	ap_msg.msg = (void *)get_zeroed_page(GFP_KERNEL);
	if (!ap_msg.msg)
		return -ENOMEM;

	rng_type6cprb_msgx(&ap_msg, 4, &domain);

	msg = ap_msg.msg;
	msg->cprbx.domain = AP_QID_QUEUE(aq->qid);

	rc = ap_send(aq->qid, 0x0102030405060708ULL, ap_msg.msg, ap_msg.len);
	if (rc)
		goto out_free;

	/* Wait for the test message to complete. */
	for (i = 0; i < 2 * HZ; i++) {
		msleep(1000 / HZ);
		rc = ap_recv(aq->qid, &psmid, ap_msg.msg, 4096);
		if (rc == 0 && psmid == 0x0102030405060708ULL)
			break;
	}

	if (i >= 2 * HZ) {
		/* Got no answer. */
		rc = -ENODEV;
		goto out_free;
	}

	reply = ap_msg.msg;
	if (reply->cprbx.ccp_rtcode == 0 && reply->cprbx.ccp_rscode == 0)
		rc = 1;
	else
		rc = 0;
out_free:
	free_page((unsigned long)ap_msg.msg);
	return rc;
}

/*
 * Probe function for CEX2C/CEX3C card devices. It always accepts the
 * AP device since the bus_match already checked the hardware type.
 * @ap_dev: pointer to the AP card device.
 */
static int zcrypt_cex2c_card_probe(struct ap_device *ap_dev)
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
	dev_set_drvdata(&ap_dev->device, zc);
	switch (ac->ap_dev.device_type) {
	case AP_DEVICE_TYPE_CEX2C:
		zc->user_space_type = ZCRYPT_CEX2C;
		zc->type_string = "CEX2C";
		zc->speed_rating = CEX2C_SPEED_IDX;
		zc->min_mod_size = CEX2C_MIN_MOD_SIZE;
		zc->max_mod_size = CEX2C_MAX_MOD_SIZE;
		zc->max_exp_bit_length = CEX2C_MAX_MOD_SIZE;
		break;
	case AP_DEVICE_TYPE_CEX3C:
		zc->user_space_type = ZCRYPT_CEX3C;
		zc->type_string = "CEX3C";
		zc->speed_rating = CEX3C_SPEED_IDX;
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
		zcrypt_card_free(zc);
		return rc;
	}

	if (ap_test_bit(&ac->functions, AP_FUNC_COPRO)) {
		rc = sysfs_create_group(&ap_dev->device.kobj,
					&cca_card_attr_grp);
		if (rc) {
			zcrypt_card_unregister(zc);
			zcrypt_card_free(zc);
		}
	}

	return rc;
}

/*
 * This is called to remove the CEX2C/CEX3C card driver information
 * if an AP card device is removed.
 */
static void zcrypt_cex2c_card_remove(struct ap_device *ap_dev)
{
	struct zcrypt_card *zc = dev_get_drvdata(&ap_dev->device);
	struct ap_card *ac = to_ap_card(&ap_dev->device);

	if (ap_test_bit(&ac->functions, AP_FUNC_COPRO))
		sysfs_remove_group(&ap_dev->device.kobj, &cca_card_attr_grp);

	zcrypt_card_unregister(zc);
}

static struct ap_driver zcrypt_cex2c_card_driver = {
	.probe = zcrypt_cex2c_card_probe,
	.remove = zcrypt_cex2c_card_remove,
	.ids = zcrypt_cex2c_card_ids,
	.flags = AP_DRIVER_FLAG_DEFAULT,
};

/*
 * Probe function for CEX2C/CEX3C queue devices. It always accepts the
 * AP device since the bus_match already checked the hardware type.
 * @ap_dev: pointer to the AP card device.
 */
static int zcrypt_cex2c_queue_probe(struct ap_device *ap_dev)
{
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);
	struct zcrypt_queue *zq;
	int rc;

	zq = zcrypt_queue_alloc(CEX2C_MAX_XCRB_MESSAGE_SIZE);
	if (!zq)
		return -ENOMEM;
	zq->queue = aq;
	zq->online = 1;
	atomic_set(&zq->load, 0);
	ap_rapq(aq->qid);
	rc = zcrypt_cex2c_rng_supported(aq);
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
	ap_queue_init_state(aq);
	ap_queue_init_reply(aq, &zq->reply);
	aq->request_timeout = CEX2C_CLEANUP_TIME;
	dev_set_drvdata(&ap_dev->device, zq);
	rc = zcrypt_queue_register(zq);
	if (rc) {
		zcrypt_queue_free(zq);
		return rc;
	}

	if (ap_test_bit(&aq->card->functions, AP_FUNC_COPRO)) {
		rc = sysfs_create_group(&ap_dev->device.kobj,
					&cca_queue_attr_grp);
		if (rc) {
			zcrypt_queue_unregister(zq);
			zcrypt_queue_free(zq);
		}
	}

	return rc;
}

/*
 * This is called to remove the CEX2C/CEX3C queue driver information
 * if an AP queue device is removed.
 */
static void zcrypt_cex2c_queue_remove(struct ap_device *ap_dev)
{
	struct zcrypt_queue *zq = dev_get_drvdata(&ap_dev->device);
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);

	if (ap_test_bit(&aq->card->functions, AP_FUNC_COPRO))
		sysfs_remove_group(&ap_dev->device.kobj, &cca_queue_attr_grp);

	zcrypt_queue_unregister(zq);
}

static struct ap_driver zcrypt_cex2c_queue_driver = {
	.probe = zcrypt_cex2c_queue_probe,
	.remove = zcrypt_cex2c_queue_remove,
	.ids = zcrypt_cex2c_queue_ids,
	.flags = AP_DRIVER_FLAG_DEFAULT,
};

int __init zcrypt_cex2c_init(void)
{
	int rc;

	rc = ap_driver_register(&zcrypt_cex2c_card_driver,
				THIS_MODULE, "cex2card");
	if (rc)
		return rc;

	rc = ap_driver_register(&zcrypt_cex2c_queue_driver,
				THIS_MODULE, "cex2cqueue");
	if (rc)
		ap_driver_unregister(&zcrypt_cex2c_card_driver);

	return rc;
}

void zcrypt_cex2c_exit(void)
{
	ap_driver_unregister(&zcrypt_cex2c_queue_driver);
	ap_driver_unregister(&zcrypt_cex2c_card_driver);
}

module_init(zcrypt_cex2c_init);
module_exit(zcrypt_cex2c_exit);
