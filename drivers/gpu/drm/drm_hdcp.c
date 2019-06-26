// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Intel Corporation.
 *
 * Authors:
 * Ramalingam C <ramalingam.c@intel.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/firmware.h>

#include <drm/drm_hdcp.h>
#include <drm/drm_sysfs.h>
#include <drm/drm_print.h>
#include <drm/drm_device.h>
#include <drm/drm_property.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_connector.h>

#include "drm_internal.h"

static struct hdcp_srm {
	u32 revoked_ksv_cnt;
	u8 *revoked_ksv_list;

	/* Mutex to protect above struct member */
	struct mutex mutex;
} *srm_data;

static inline void drm_hdcp_print_ksv(const u8 *ksv)
{
	DRM_DEBUG("\t%#02x, %#02x, %#02x, %#02x, %#02x\n",
		  ksv[0], ksv[1], ksv[2], ksv[3], ksv[4]);
}

static u32 drm_hdcp_get_revoked_ksv_count(const u8 *buf, u32 vrls_length)
{
	u32 parsed_bytes = 0, ksv_count = 0, vrl_ksv_cnt, vrl_sz;

	while (parsed_bytes < vrls_length) {
		vrl_ksv_cnt = *buf;
		ksv_count += vrl_ksv_cnt;

		vrl_sz = (vrl_ksv_cnt * DRM_HDCP_KSV_LEN) + 1;
		buf += vrl_sz;
		parsed_bytes += vrl_sz;
	}

	/*
	 * When vrls are not valid, ksvs are not considered.
	 * Hence SRM will be discarded.
	 */
	if (parsed_bytes != vrls_length)
		ksv_count = 0;

	return ksv_count;
}

static u32 drm_hdcp_get_revoked_ksvs(const u8 *buf, u8 *revoked_ksv_list,
				     u32 vrls_length)
{
	u32 parsed_bytes = 0, ksv_count = 0;
	u32 vrl_ksv_cnt, vrl_ksv_sz, vrl_idx = 0;

	do {
		vrl_ksv_cnt = *buf;
		vrl_ksv_sz = vrl_ksv_cnt * DRM_HDCP_KSV_LEN;

		buf++;

		DRM_DEBUG("vrl: %d, Revoked KSVs: %d\n", vrl_idx++,
			  vrl_ksv_cnt);
		memcpy(revoked_ksv_list, buf, vrl_ksv_sz);

		ksv_count += vrl_ksv_cnt;
		revoked_ksv_list += vrl_ksv_sz;
		buf += vrl_ksv_sz;

		parsed_bytes += (vrl_ksv_sz + 1);
	} while (parsed_bytes < vrls_length);

	return ksv_count;
}

static inline u32 get_vrl_length(const u8 *buf)
{
	return drm_hdcp_be24_to_cpu(buf);
}

static int drm_hdcp_parse_hdcp1_srm(const u8 *buf, size_t count)
{
	struct hdcp_srm_header *header;
	u32 vrl_length, ksv_count;

	if (count < (sizeof(struct hdcp_srm_header) +
	    DRM_HDCP_1_4_VRL_LENGTH_SIZE + DRM_HDCP_1_4_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length\n");
		return -EINVAL;
	}

	header = (struct hdcp_srm_header *)buf;
	DRM_DEBUG("SRM ID: 0x%x, SRM Ver: 0x%x, SRM Gen No: 0x%x\n",
		  header->srm_id,
		  be16_to_cpu(header->srm_version), header->srm_gen_no);

	WARN_ON(header->reserved);

	buf = buf + sizeof(*header);
	vrl_length = get_vrl_length(buf);
	if (count < (sizeof(struct hdcp_srm_header) + vrl_length) ||
	    vrl_length < (DRM_HDCP_1_4_VRL_LENGTH_SIZE +
			  DRM_HDCP_1_4_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length or vrl length\n");
		return -EINVAL;
	}

	/* Length of the all vrls combined */
	vrl_length -= (DRM_HDCP_1_4_VRL_LENGTH_SIZE +
		       DRM_HDCP_1_4_DCP_SIG_SIZE);

	if (!vrl_length) {
		DRM_ERROR("No vrl found\n");
		return -EINVAL;
	}

	buf += DRM_HDCP_1_4_VRL_LENGTH_SIZE;
	ksv_count = drm_hdcp_get_revoked_ksv_count(buf, vrl_length);
	if (!ksv_count) {
		DRM_DEBUG("Revoked KSV count is 0\n");
		return count;
	}

	kfree(srm_data->revoked_ksv_list);
	srm_data->revoked_ksv_list = kcalloc(ksv_count, DRM_HDCP_KSV_LEN,
					     GFP_KERNEL);
	if (!srm_data->revoked_ksv_list) {
		DRM_ERROR("Out of Memory\n");
		return -ENOMEM;
	}

	if (drm_hdcp_get_revoked_ksvs(buf, srm_data->revoked_ksv_list,
				      vrl_length) != ksv_count) {
		srm_data->revoked_ksv_cnt = 0;
		kfree(srm_data->revoked_ksv_list);
		return -EINVAL;
	}

	srm_data->revoked_ksv_cnt = ksv_count;
	return count;
}

static int drm_hdcp_parse_hdcp2_srm(const u8 *buf, size_t count)
{
	struct hdcp_srm_header *header;
	u32 vrl_length, ksv_count, ksv_sz;

	if (count < (sizeof(struct hdcp_srm_header) +
	    DRM_HDCP_2_VRL_LENGTH_SIZE + DRM_HDCP_2_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length\n");
		return -EINVAL;
	}

	header = (struct hdcp_srm_header *)buf;
	DRM_DEBUG("SRM ID: 0x%x, SRM Ver: 0x%x, SRM Gen No: 0x%x\n",
		  header->srm_id & DRM_HDCP_SRM_ID_MASK,
		  be16_to_cpu(header->srm_version), header->srm_gen_no);

	if (header->reserved)
		return -EINVAL;

	buf = buf + sizeof(*header);
	vrl_length = get_vrl_length(buf);

	if (count < (sizeof(struct hdcp_srm_header) + vrl_length) ||
	    vrl_length < (DRM_HDCP_2_VRL_LENGTH_SIZE +
	    DRM_HDCP_2_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length or vrl length\n");
		return -EINVAL;
	}

	/* Length of the all vrls combined */
	vrl_length -= (DRM_HDCP_2_VRL_LENGTH_SIZE +
		       DRM_HDCP_2_DCP_SIG_SIZE);

	if (!vrl_length) {
		DRM_ERROR("No vrl found\n");
		return -EINVAL;
	}

	buf += DRM_HDCP_2_VRL_LENGTH_SIZE;
	ksv_count = (*buf << 2) | DRM_HDCP_2_KSV_COUNT_2_LSBITS(*(buf + 1));
	if (!ksv_count) {
		DRM_DEBUG("Revoked KSV count is 0\n");
		return count;
	}

	kfree(srm_data->revoked_ksv_list);
	srm_data->revoked_ksv_list = kcalloc(ksv_count, DRM_HDCP_KSV_LEN,
					     GFP_KERNEL);
	if (!srm_data->revoked_ksv_list) {
		DRM_ERROR("Out of Memory\n");
		return -ENOMEM;
	}

	ksv_sz = ksv_count * DRM_HDCP_KSV_LEN;
	buf += DRM_HDCP_2_NO_OF_DEV_PLUS_RESERVED_SZ;

	DRM_DEBUG("Revoked KSVs: %d\n", ksv_count);
	memcpy(srm_data->revoked_ksv_list, buf, ksv_sz);

	srm_data->revoked_ksv_cnt = ksv_count;
	return count;
}

static inline bool is_srm_version_hdcp1(const u8 *buf)
{
	return *buf == (u8)(DRM_HDCP_1_4_SRM_ID << 4);
}

static inline bool is_srm_version_hdcp2(const u8 *buf)
{
	return *buf == (u8)(DRM_HDCP_2_SRM_ID << 4 | DRM_HDCP_2_INDICATOR);
}

static void drm_hdcp_srm_update(const u8 *buf, size_t count)
{
	if (count < sizeof(struct hdcp_srm_header))
		return;

	if (is_srm_version_hdcp1(buf))
		drm_hdcp_parse_hdcp1_srm(buf, count);
	else if (is_srm_version_hdcp2(buf))
		drm_hdcp_parse_hdcp2_srm(buf, count);
}

static void drm_hdcp_request_srm(struct drm_device *drm_dev)
{
	char fw_name[36] = "display_hdcp_srm.bin";
	const struct firmware *fw;

	int ret;

	ret = request_firmware_direct(&fw, (const char *)fw_name,
				      drm_dev->dev);
	if (ret < 0)
		goto exit;

	if (fw->size && fw->data)
		drm_hdcp_srm_update(fw->data, fw->size);

exit:
	release_firmware(fw);
}

/**
 * drm_hdcp_check_ksvs_revoked - Check the revoked status of the IDs
 *
 * @drm_dev: drm_device for which HDCP revocation check is requested
 * @ksvs: List of KSVs (HDCP receiver IDs)
 * @ksv_count: KSV count passed in through @ksvs
 *
 * This function reads the HDCP System renewability Message(SRM Table)
 * from userspace as a firmware and parses it for the revoked HDCP
 * KSVs(Receiver IDs) detected by DCP LLC. Once the revoked KSVs are known,
 * revoked state of the KSVs in the list passed in by display drivers are
 * decided and response is sent.
 *
 * SRM should be presented in the name of "display_hdcp_srm.bin".
 *
 * Returns:
 * TRUE on any of the KSV is revoked, else FALSE.
 */
bool drm_hdcp_check_ksvs_revoked(struct drm_device *drm_dev, u8 *ksvs,
				 u32 ksv_count)
{
	u32 rev_ksv_cnt, cnt, i, j;
	u8 *rev_ksv_list;

	if (!srm_data)
		return false;

	mutex_lock(&srm_data->mutex);
	drm_hdcp_request_srm(drm_dev);

	rev_ksv_cnt = srm_data->revoked_ksv_cnt;
	rev_ksv_list = srm_data->revoked_ksv_list;

	/* If the Revoked ksv list is empty */
	if (!rev_ksv_cnt || !rev_ksv_list) {
		mutex_unlock(&srm_data->mutex);
		return false;
	}

	for  (cnt = 0; cnt < ksv_count; cnt++) {
		rev_ksv_list = srm_data->revoked_ksv_list;
		for (i = 0; i < rev_ksv_cnt; i++) {
			for (j = 0; j < DRM_HDCP_KSV_LEN; j++)
				if (ksvs[j] != rev_ksv_list[j]) {
					break;
				} else if (j == (DRM_HDCP_KSV_LEN - 1)) {
					DRM_DEBUG("Revoked KSV is ");
					drm_hdcp_print_ksv(ksvs);
					mutex_unlock(&srm_data->mutex);
					return true;
				}
			/* Move the offset to next KSV in the revoked list */
			rev_ksv_list += DRM_HDCP_KSV_LEN;
		}

		/* Iterate to next ksv_offset */
		ksvs += DRM_HDCP_KSV_LEN;
	}
	mutex_unlock(&srm_data->mutex);
	return false;
}
EXPORT_SYMBOL_GPL(drm_hdcp_check_ksvs_revoked);

int drm_setup_hdcp_srm(struct class *drm_class)
{
	srm_data = kzalloc(sizeof(*srm_data), GFP_KERNEL);
	if (!srm_data)
		return -ENOMEM;
	mutex_init(&srm_data->mutex);

	return 0;
}

void drm_teardown_hdcp_srm(struct class *drm_class)
{
	if (srm_data) {
		kfree(srm_data->revoked_ksv_list);
		kfree(srm_data);
	}
}

static struct drm_prop_enum_list drm_cp_enum_list[] = {
	{ DRM_MODE_CONTENT_PROTECTION_UNDESIRED, "Undesired" },
	{ DRM_MODE_CONTENT_PROTECTION_DESIRED, "Desired" },
	{ DRM_MODE_CONTENT_PROTECTION_ENABLED, "Enabled" },
};
DRM_ENUM_NAME_FN(drm_get_content_protection_name, drm_cp_enum_list)

/**
 * drm_connector_attach_content_protection_property - attach content protection
 * property
 *
 * @connector: connector to attach CP property on.
 *
 * This is used to add support for content protection on select connectors.
 * Content Protection is intentionally vague to allow for different underlying
 * technologies, however it is most implemented by HDCP.
 *
 * The content protection will be set to &drm_connector_state.content_protection
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_connector_attach_content_protection_property(
		struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_property *prop =
			dev->mode_config.content_protection_property;

	if (!prop)
		prop = drm_property_create_enum(dev, 0, "Content Protection",
						drm_cp_enum_list,
						ARRAY_SIZE(drm_cp_enum_list));
	if (!prop)
		return -ENOMEM;

	drm_object_attach_property(&connector->base, prop,
				   DRM_MODE_CONTENT_PROTECTION_UNDESIRED);
	dev->mode_config.content_protection_property = prop;

	return 0;
}
EXPORT_SYMBOL(drm_connector_attach_content_protection_property);
