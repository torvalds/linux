// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright IBM Corp. 2012, 2022
 *  Author(s): Holger Dengler <hd@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/mod_devicetable.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_msgtype6.h"
#include "zcrypt_msgtype50.h"
#include "zcrypt_error.h"
#include "zcrypt_cex4.h"
#include "zcrypt_ccamisc.h"
#include "zcrypt_ep11misc.h"

#define CEX4A_MIN_MOD_SIZE	  1	/*    8 bits	*/
#define CEX4A_MAX_MOD_SIZE_2K	256	/* 2048 bits	*/
#define CEX4A_MAX_MOD_SIZE_4K	512	/* 4096 bits	*/

#define CEX4C_MIN_MOD_SIZE	 16	/*  256 bits	*/
#define CEX4C_MAX_MOD_SIZE	512	/* 4096 bits	*/

/* Waiting time for requests to be processed.
 * Currently there are some types of request which are not deterministic.
 * But the maximum time limit managed by the stomper code is set to 60sec.
 * Hence we have to wait at least that time period.
 */
#define CEX4_CLEANUP_TIME	(900 * HZ)

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("CEX[45678] Cryptographic Card device driver, " \
		   "Copyright IBM Corp. 2022");
MODULE_LICENSE("GPL");

static struct ap_device_id zcrypt_cex4_card_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX4,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX5,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX6,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX7,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX8,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex4_card_ids);

static struct ap_device_id zcrypt_cex4_queue_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX4,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX5,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX6,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX7,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX8,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex4_queue_ids);

/*
 * CCA card additional device attributes
 */
static ssize_t cca_serialnr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	struct cca_info ci;

	memset(&ci, 0, sizeof(ci));

	if (ap_domain_index >= 0)
		cca_get_info(ac->id, ap_domain_index, &ci, 0);

	return sysfs_emit(buf, "%s\n", ci.serial);
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
	static const char * const new_state[] = { "empty", "partial", "full" };
	static const char * const cao_state[] = { "invalid", "valid" };
	struct zcrypt_queue *zq = dev_get_drvdata(dev);
	struct cca_info ci;
	int n = 0;

	memset(&ci, 0, sizeof(ci));

	cca_get_info(AP_QID_CARD(zq->queue->qid),
		     AP_QID_QUEUE(zq->queue->qid),
		     &ci, 0);

	if (ci.new_aes_mk_state >= '1' && ci.new_aes_mk_state <= '3')
		n += sysfs_emit_at(buf, n, "AES NEW: %s 0x%016llx\n",
				   new_state[ci.new_aes_mk_state - '1'],
				   ci.new_aes_mkvp);
	else
		n += sysfs_emit_at(buf, n, "AES NEW: - -\n");

	if (ci.cur_aes_mk_state >= '1' && ci.cur_aes_mk_state <= '2')
		n += sysfs_emit_at(buf, n, "AES CUR: %s 0x%016llx\n",
				   cao_state[ci.cur_aes_mk_state - '1'],
				   ci.cur_aes_mkvp);
	else
		n += sysfs_emit_at(buf, n, "AES CUR: - -\n");

	if (ci.old_aes_mk_state >= '1' && ci.old_aes_mk_state <= '2')
		n += sysfs_emit_at(buf, n, "AES OLD: %s 0x%016llx\n",
				   cao_state[ci.old_aes_mk_state - '1'],
				   ci.old_aes_mkvp);
	else
		n += sysfs_emit_at(buf, n, "AES OLD: - -\n");

	if (ci.new_apka_mk_state >= '1' && ci.new_apka_mk_state <= '3')
		n += sysfs_emit_at(buf, n, "APKA NEW: %s 0x%016llx\n",
				   new_state[ci.new_apka_mk_state - '1'],
				   ci.new_apka_mkvp);
	else
		n += sysfs_emit_at(buf, n, "APKA NEW: - -\n");

	if (ci.cur_apka_mk_state >= '1' && ci.cur_apka_mk_state <= '2')
		n += sysfs_emit_at(buf, n, "APKA CUR: %s 0x%016llx\n",
				   cao_state[ci.cur_apka_mk_state - '1'],
				   ci.cur_apka_mkvp);
	else
		n += sysfs_emit_at(buf, n, "APKA CUR: - -\n");

	if (ci.old_apka_mk_state >= '1' && ci.old_apka_mk_state <= '2')
		n += sysfs_emit_at(buf, n, "APKA OLD: %s 0x%016llx\n",
				   cao_state[ci.old_apka_mk_state - '1'],
				   ci.old_apka_mkvp);
	else
		n += sysfs_emit_at(buf, n, "APKA OLD: - -\n");

	if (ci.new_asym_mk_state >= '1' && ci.new_asym_mk_state <= '3')
		n += sysfs_emit_at(buf, n, "ASYM NEW: %s 0x%016llx%016llx\n",
				   new_state[ci.new_asym_mk_state - '1'],
				   *((u64 *)(ci.new_asym_mkvp)),
				   *((u64 *)(ci.new_asym_mkvp + sizeof(u64))));
	else
		n += sysfs_emit_at(buf, n, "ASYM NEW: - -\n");

	if (ci.cur_asym_mk_state >= '1' && ci.cur_asym_mk_state <= '2')
		n += sysfs_emit_at(buf, n, "ASYM CUR: %s 0x%016llx%016llx\n",
				   cao_state[ci.cur_asym_mk_state - '1'],
				   *((u64 *)(ci.cur_asym_mkvp)),
				   *((u64 *)(ci.cur_asym_mkvp + sizeof(u64))));
	else
		n += sysfs_emit_at(buf, n, "ASYM CUR: - -\n");

	if (ci.old_asym_mk_state >= '1' && ci.old_asym_mk_state <= '2')
		n += sysfs_emit_at(buf, n, "ASYM OLD: %s 0x%016llx%016llx\n",
				   cao_state[ci.old_asym_mk_state - '1'],
				   *((u64 *)(ci.old_asym_mkvp)),
				   *((u64 *)(ci.old_asym_mkvp + sizeof(u64))));
	else
		n += sysfs_emit_at(buf, n, "ASYM OLD: - -\n");

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
 * EP11 card additional device attributes
 */
static ssize_t ep11_api_ordinalnr_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	struct ep11_card_info ci;

	memset(&ci, 0, sizeof(ci));

	ep11_get_card_info(ac->id, &ci, 0);

	if (ci.API_ord_nr > 0)
		return sysfs_emit(buf, "%u\n", ci.API_ord_nr);
	else
		return sysfs_emit(buf, "\n");
}

static struct device_attribute dev_attr_ep11_api_ordinalnr =
	__ATTR(API_ordinalnr, 0444, ep11_api_ordinalnr_show, NULL);

static ssize_t ep11_fw_version_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	struct ep11_card_info ci;

	memset(&ci, 0, sizeof(ci));

	ep11_get_card_info(ac->id, &ci, 0);

	if (ci.FW_version > 0)
		return sysfs_emit(buf, "%d.%d\n",
				  (int)(ci.FW_version >> 8),
				  (int)(ci.FW_version & 0xFF));
	else
		return sysfs_emit(buf, "\n");
}

static struct device_attribute dev_attr_ep11_fw_version =
	__ATTR(FW_version, 0444, ep11_fw_version_show, NULL);

static ssize_t ep11_serialnr_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	struct ep11_card_info ci;

	memset(&ci, 0, sizeof(ci));

	ep11_get_card_info(ac->id, &ci, 0);

	if (ci.serial[0])
		return sysfs_emit(buf, "%16.16s\n", ci.serial);
	else
		return sysfs_emit(buf, "\n");
}

static struct device_attribute dev_attr_ep11_serialnr =
	__ATTR(serialnr, 0444, ep11_serialnr_show, NULL);

static const struct {
	int	    mode_bit;
	const char *mode_txt;
} ep11_op_modes[] = {
	{ 0, "FIPS2009" },
	{ 1, "BSI2009" },
	{ 2, "FIPS2011" },
	{ 3, "BSI2011" },
	{ 4, "SIGG-IMPORT" },
	{ 5, "SIGG" },
	{ 6, "BSICC2017" },
	{ 7, "FIPS2021" },
	{ 8, "FIPS2024" },
	{ 0, NULL }
};

static ssize_t ep11_card_op_modes_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	struct ep11_card_info ci;
	int i, n = 0;

	memset(&ci, 0, sizeof(ci));

	ep11_get_card_info(ac->id, &ci, 0);

	for (i = 0; ep11_op_modes[i].mode_txt; i++) {
		if (ci.op_mode & (1ULL << ep11_op_modes[i].mode_bit)) {
			if (n > 0)
				buf[n++] = ' ';
			n += sysfs_emit_at(buf, n, "%s",
					   ep11_op_modes[i].mode_txt);
		}
	}
	n += sysfs_emit_at(buf, n, "\n");

	return n;
}

static struct device_attribute dev_attr_ep11_card_op_modes =
	__ATTR(op_modes, 0444, ep11_card_op_modes_show, NULL);

static struct attribute *ep11_card_attrs[] = {
	&dev_attr_ep11_api_ordinalnr.attr,
	&dev_attr_ep11_fw_version.attr,
	&dev_attr_ep11_serialnr.attr,
	&dev_attr_ep11_card_op_modes.attr,
	NULL,
};

static const struct attribute_group ep11_card_attr_grp = {
	.attrs = ep11_card_attrs,
};

/*
 * EP11 queue additional device attributes
 */

static ssize_t ep11_mkvps_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct zcrypt_queue *zq = dev_get_drvdata(dev);
	int n = 0;
	struct ep11_domain_info di;
	static const char * const cwk_state[] = { "invalid", "valid" };
	static const char * const nwk_state[] = { "empty", "uncommitted",
						  "committed" };

	memset(&di, 0, sizeof(di));

	if (zq->online)
		ep11_get_domain_info(AP_QID_CARD(zq->queue->qid),
				     AP_QID_QUEUE(zq->queue->qid),
				     &di, 0);

	if (di.cur_wk_state == '0') {
		n = sysfs_emit(buf, "WK CUR: %s -\n",
			       cwk_state[di.cur_wk_state - '0']);
	} else if (di.cur_wk_state == '1') {
		n = sysfs_emit(buf, "WK CUR: %s 0x",
			       cwk_state[di.cur_wk_state - '0']);
		bin2hex(buf + n, di.cur_wkvp, sizeof(di.cur_wkvp));
		n += 2 * sizeof(di.cur_wkvp);
		n += sysfs_emit_at(buf, n, "\n");
	} else {
		n = sysfs_emit(buf, "WK CUR: - -\n");
	}

	if (di.new_wk_state == '0') {
		n += sysfs_emit_at(buf, n, "WK NEW: %s -\n",
				   nwk_state[di.new_wk_state - '0']);
	} else if (di.new_wk_state >= '1' && di.new_wk_state <= '2') {
		n += sysfs_emit_at(buf, n, "WK NEW: %s 0x",
				   nwk_state[di.new_wk_state - '0']);
		bin2hex(buf + n, di.new_wkvp, sizeof(di.new_wkvp));
		n += 2 * sizeof(di.new_wkvp);
		n += sysfs_emit_at(buf, n, "\n");
	} else {
		n += sysfs_emit_at(buf, n, "WK NEW: - -\n");
	}

	return n;
}

static struct device_attribute dev_attr_ep11_mkvps =
	__ATTR(mkvps, 0444, ep11_mkvps_show, NULL);

static ssize_t ep11_queue_op_modes_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct zcrypt_queue *zq = dev_get_drvdata(dev);
	int i, n = 0;
	struct ep11_domain_info di;

	memset(&di, 0, sizeof(di));

	if (zq->online)
		ep11_get_domain_info(AP_QID_CARD(zq->queue->qid),
				     AP_QID_QUEUE(zq->queue->qid),
				     &di, 0);

	for (i = 0; ep11_op_modes[i].mode_txt; i++) {
		if (di.op_mode & (1ULL << ep11_op_modes[i].mode_bit)) {
			if (n > 0)
				buf[n++] = ' ';
			n += sysfs_emit_at(buf, n, "%s",
					   ep11_op_modes[i].mode_txt);
		}
	}
	n += sysfs_emit_at(buf, n, "\n");

	return n;
}

static struct device_attribute dev_attr_ep11_queue_op_modes =
	__ATTR(op_modes, 0444, ep11_queue_op_modes_show, NULL);

static struct attribute *ep11_queue_attrs[] = {
	&dev_attr_ep11_mkvps.attr,
	&dev_attr_ep11_queue_op_modes.attr,
	NULL,
};

static const struct attribute_group ep11_queue_attr_grp = {
	.attrs = ep11_queue_attrs,
};

/*
 * Probe function for CEX[45678] card device. It always
 * accepts the AP device since the bus_match already checked
 * the hardware type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_cex4_card_probe(struct ap_device *ap_dev)
{
	/*
	 * Normalized speed ratings per crypto adapter
	 * MEX_1k, MEX_2k, MEX_4k, CRT_1k, CRT_2k, CRT_4k, RNG, SECKEY
	 */
	static const int CEX4A_SPEED_IDX[NUM_OPS] = {
		 14,  19, 249, 42, 228, 1458, 0, 0};
	static const int CEX5A_SPEED_IDX[NUM_OPS] = {
		  8,   9,  20, 18,  66,	 458, 0, 0};
	static const int CEX6A_SPEED_IDX[NUM_OPS] = {
		  6,   9,  20, 17,  65,	 438, 0, 0};
	static const int CEX7A_SPEED_IDX[NUM_OPS] = {
		  6,   8,  17, 15,  54,	 362, 0, 0};
	static const int CEX8A_SPEED_IDX[NUM_OPS] = {
		  6,   8,  17, 15,  54,	 362, 0, 0};

	static const int CEX4C_SPEED_IDX[NUM_OPS] = {
		 59,  69, 308, 83, 278, 2204, 209, 40};
	static const int CEX5C_SPEED_IDX[] = {
		 24,  31,  50, 37,  90,	 479,  27, 10};
	static const int CEX6C_SPEED_IDX[NUM_OPS] = {
		 16,  20,  32, 27,  77,	 455,  24,  9};
	static const int CEX7C_SPEED_IDX[NUM_OPS] = {
		 14,  16,  26, 23,  64,	 376,  23,  8};
	static const int CEX8C_SPEED_IDX[NUM_OPS] = {
		 14,  16,  26, 23,  64,	 376,  23,  8};

	static const int CEX4P_SPEED_IDX[NUM_OPS] = {
		  0,   0,   0,	 0,   0,   0,	0,  50};
	static const int CEX5P_SPEED_IDX[NUM_OPS] = {
		  0,   0,   0,	 0,   0,   0,	0,  10};
	static const int CEX6P_SPEED_IDX[NUM_OPS] = {
		  0,   0,   0,	 0,   0,   0,	0,   9};
	static const int CEX7P_SPEED_IDX[NUM_OPS] = {
		  0,   0,   0,	 0,   0,   0,	0,   8};
	static const int CEX8P_SPEED_IDX[NUM_OPS] = {
		  0,   0,   0,	 0,   0,   0,	0,   8};

	struct ap_card *ac = to_ap_card(&ap_dev->device);
	struct zcrypt_card *zc;
	int rc = 0;

	zc = zcrypt_card_alloc();
	if (!zc)
		return -ENOMEM;
	zc->card = ac;
	dev_set_drvdata(&ap_dev->device, zc);
	if (ac->hwinfo.accel) {
		if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX4) {
			zc->type_string = "CEX4A";
			zc->user_space_type = ZCRYPT_CEX4;
			zc->speed_rating = CEX4A_SPEED_IDX;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX5) {
			zc->type_string = "CEX5A";
			zc->user_space_type = ZCRYPT_CEX5;
			zc->speed_rating = CEX5A_SPEED_IDX;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX6) {
			zc->type_string = "CEX6A";
			zc->user_space_type = ZCRYPT_CEX6;
			zc->speed_rating = CEX6A_SPEED_IDX;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX7) {
			zc->type_string = "CEX7A";
			zc->speed_rating = CEX7A_SPEED_IDX;
			/* wrong user space type, just for compatibility
			 * with the ZCRYPT_STATUS_MASK ioctl.
			 */
			zc->user_space_type = ZCRYPT_CEX6;
		} else {
			zc->type_string = "CEX8A";
			zc->speed_rating = CEX8A_SPEED_IDX;
			/* wrong user space type, just for compatibility
			 * with the ZCRYPT_STATUS_MASK ioctl.
			 */
			zc->user_space_type = ZCRYPT_CEX6;
		}
		zc->min_mod_size = CEX4A_MIN_MOD_SIZE;
		if (ac->hwinfo.mex4k && ac->hwinfo.crt4k) {
			zc->max_mod_size = CEX4A_MAX_MOD_SIZE_4K;
			zc->max_exp_bit_length =
				CEX4A_MAX_MOD_SIZE_4K;
		} else {
			zc->max_mod_size = CEX4A_MAX_MOD_SIZE_2K;
			zc->max_exp_bit_length =
				CEX4A_MAX_MOD_SIZE_2K;
		}
	} else if (ac->hwinfo.cca) {
		if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX4) {
			zc->type_string = "CEX4C";
			zc->speed_rating = CEX4C_SPEED_IDX;
			/* wrong user space type, must be CEX3C
			 * just keep it for cca compatibility
			 */
			zc->user_space_type = ZCRYPT_CEX3C;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX5) {
			zc->type_string = "CEX5C";
			zc->speed_rating = CEX5C_SPEED_IDX;
			/* wrong user space type, must be CEX3C
			 * just keep it for cca compatibility
			 */
			zc->user_space_type = ZCRYPT_CEX3C;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX6) {
			zc->type_string = "CEX6C";
			zc->speed_rating = CEX6C_SPEED_IDX;
			/* wrong user space type, must be CEX3C
			 * just keep it for cca compatibility
			 */
			zc->user_space_type = ZCRYPT_CEX3C;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX7) {
			zc->type_string = "CEX7C";
			zc->speed_rating = CEX7C_SPEED_IDX;
			/* wrong user space type, must be CEX3C
			 * just keep it for cca compatibility
			 */
			zc->user_space_type = ZCRYPT_CEX3C;
		} else {
			zc->type_string = "CEX8C";
			zc->speed_rating = CEX8C_SPEED_IDX;
			/* wrong user space type, must be CEX3C
			 * just keep it for cca compatibility
			 */
			zc->user_space_type = ZCRYPT_CEX3C;
		}
		zc->min_mod_size = CEX4C_MIN_MOD_SIZE;
		zc->max_mod_size = CEX4C_MAX_MOD_SIZE;
		zc->max_exp_bit_length = CEX4C_MAX_MOD_SIZE;
	} else if (ac->hwinfo.ep11) {
		if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX4) {
			zc->type_string = "CEX4P";
			zc->user_space_type = ZCRYPT_CEX4;
			zc->speed_rating = CEX4P_SPEED_IDX;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX5) {
			zc->type_string = "CEX5P";
			zc->user_space_type = ZCRYPT_CEX5;
			zc->speed_rating = CEX5P_SPEED_IDX;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX6) {
			zc->type_string = "CEX6P";
			zc->user_space_type = ZCRYPT_CEX6;
			zc->speed_rating = CEX6P_SPEED_IDX;
		} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX7) {
			zc->type_string = "CEX7P";
			zc->speed_rating = CEX7P_SPEED_IDX;
			/* wrong user space type, just for compatibility
			 * with the ZCRYPT_STATUS_MASK ioctl.
			 */
			zc->user_space_type = ZCRYPT_CEX6;
		} else {
			zc->type_string = "CEX8P";
			zc->speed_rating = CEX8P_SPEED_IDX;
			/* wrong user space type, just for compatibility
			 * with the ZCRYPT_STATUS_MASK ioctl.
			 */
			zc->user_space_type = ZCRYPT_CEX6;
		}
		zc->min_mod_size = CEX4C_MIN_MOD_SIZE;
		zc->max_mod_size = CEX4C_MAX_MOD_SIZE;
		zc->max_exp_bit_length = CEX4C_MAX_MOD_SIZE;
	} else {
		zcrypt_card_free(zc);
		return -ENODEV;
	}
	zc->online = 1;

	rc = zcrypt_card_register(zc);
	if (rc) {
		zcrypt_card_free(zc);
		return rc;
	}

	if (ac->hwinfo.cca) {
		rc = sysfs_create_group(&ap_dev->device.kobj,
					&cca_card_attr_grp);
		if (rc) {
			zcrypt_card_unregister(zc);
			zcrypt_card_free(zc);
		}
	} else if (ac->hwinfo.ep11) {
		rc = sysfs_create_group(&ap_dev->device.kobj,
					&ep11_card_attr_grp);
		if (rc) {
			zcrypt_card_unregister(zc);
			zcrypt_card_free(zc);
		}
	}

	return rc;
}

/*
 * This is called to remove the CEX[45678] card driver
 * information if an AP card device is removed.
 */
static void zcrypt_cex4_card_remove(struct ap_device *ap_dev)
{
	struct zcrypt_card *zc = dev_get_drvdata(&ap_dev->device);
	struct ap_card *ac = to_ap_card(&ap_dev->device);

	if (ac->hwinfo.cca)
		sysfs_remove_group(&ap_dev->device.kobj, &cca_card_attr_grp);
	else if (ac->hwinfo.ep11)
		sysfs_remove_group(&ap_dev->device.kobj, &ep11_card_attr_grp);

	zcrypt_card_unregister(zc);
}

static struct ap_driver zcrypt_cex4_card_driver = {
	.probe = zcrypt_cex4_card_probe,
	.remove = zcrypt_cex4_card_remove,
	.ids = zcrypt_cex4_card_ids,
	.flags = AP_DRIVER_FLAG_DEFAULT,
};

/*
 * Probe function for CEX[45678] queue device. It always
 * accepts the AP device since the bus_match already checked
 * the hardware type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_cex4_queue_probe(struct ap_device *ap_dev)
{
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);
	struct zcrypt_queue *zq;
	int rc;

	if (aq->card->hwinfo.accel) {
		zq = zcrypt_queue_alloc(aq->card->maxmsgsize);
		if (!zq)
			return -ENOMEM;
		zq->ops = zcrypt_msgtype(MSGTYPE50_NAME,
					 MSGTYPE50_VARIANT_DEFAULT);
	} else if (aq->card->hwinfo.cca) {
		zq = zcrypt_queue_alloc(aq->card->maxmsgsize);
		if (!zq)
			return -ENOMEM;
		zq->ops = zcrypt_msgtype(MSGTYPE06_NAME,
					 MSGTYPE06_VARIANT_DEFAULT);
	} else if (aq->card->hwinfo.ep11) {
		zq = zcrypt_queue_alloc(aq->card->maxmsgsize);
		if (!zq)
			return -ENOMEM;
		zq->ops = zcrypt_msgtype(MSGTYPE06_NAME,
					 MSGTYPE06_VARIANT_EP11);
	} else {
		return -ENODEV;
	}

	zq->queue = aq;
	zq->online = 1;
	atomic_set(&zq->load, 0);
	ap_queue_init_state(aq);
	ap_queue_init_reply(aq, &zq->reply);
	aq->request_timeout = CEX4_CLEANUP_TIME;
	dev_set_drvdata(&ap_dev->device, zq);
	rc = zcrypt_queue_register(zq);
	if (rc) {
		zcrypt_queue_free(zq);
		return rc;
	}

	if (aq->card->hwinfo.cca) {
		rc = sysfs_create_group(&ap_dev->device.kobj,
					&cca_queue_attr_grp);
		if (rc) {
			zcrypt_queue_unregister(zq);
			zcrypt_queue_free(zq);
		}
	} else if (aq->card->hwinfo.ep11) {
		rc = sysfs_create_group(&ap_dev->device.kobj,
					&ep11_queue_attr_grp);
		if (rc) {
			zcrypt_queue_unregister(zq);
			zcrypt_queue_free(zq);
		}
	}

	return rc;
}

/*
 * This is called to remove the CEX[45678] queue driver
 * information if an AP queue device is removed.
 */
static void zcrypt_cex4_queue_remove(struct ap_device *ap_dev)
{
	struct zcrypt_queue *zq = dev_get_drvdata(&ap_dev->device);
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);

	if (aq->card->hwinfo.cca)
		sysfs_remove_group(&ap_dev->device.kobj, &cca_queue_attr_grp);
	else if (aq->card->hwinfo.ep11)
		sysfs_remove_group(&ap_dev->device.kobj, &ep11_queue_attr_grp);

	zcrypt_queue_unregister(zq);
}

static struct ap_driver zcrypt_cex4_queue_driver = {
	.probe = zcrypt_cex4_queue_probe,
	.remove = zcrypt_cex4_queue_remove,
	.ids = zcrypt_cex4_queue_ids,
	.flags = AP_DRIVER_FLAG_DEFAULT,
};

int __init zcrypt_cex4_init(void)
{
	int rc;

	rc = ap_driver_register(&zcrypt_cex4_card_driver,
				THIS_MODULE, "cex4card");
	if (rc)
		return rc;

	rc = ap_driver_register(&zcrypt_cex4_queue_driver,
				THIS_MODULE, "cex4queue");
	if (rc)
		ap_driver_unregister(&zcrypt_cex4_card_driver);

	return rc;
}

void __exit zcrypt_cex4_exit(void)
{
	ap_driver_unregister(&zcrypt_cex4_queue_driver);
	ap_driver_unregister(&zcrypt_cex4_card_driver);
}

module_init(zcrypt_cex4_init);
module_exit(zcrypt_cex4_exit);
