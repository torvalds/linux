// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/xarray.h>
#include <drm/drm_mm.h>
#include <linux/random.h>
#include "amdgpu.h"
#include "amdgpu_sdma.h"
#include "amdgpu_ualink.h"
#include "amdgpu_xgmi.h"
#include "amdgpu_dma_buf.h"
#include "psp_gfx_if.h"
#include <linux/math.h>
#include <linux/sysfs.h>
#include <linux/string.h>

static void deactivate_accelerator(struct amdgpu_device *adev);
static int amdgpu_ualink_remote_interrupt(struct amdgpu_device *adev,
				u32 remote_accel_id, u32 dw0, u32 dw1,
				u32 dw2, u32 dw3);
static void amdgpu_ualink_flush_tlb(struct amdgpu_device *adev,
				    u32 flush_type);
static int amdgpu_ualink_reserve_npa_vm_and_bos(struct amdgpu_device *adev,
						struct amdgpu_bo *bos[], u32 n_bos,
						struct drm_exec *exec,
						bool interruptible);
static void amdgpu_ualink_unreserve_npa_vm_and_bos(struct amdgpu_device *adev,
						   struct drm_exec *exec);
static void amdgpu_ualink_handle_connection_reset(struct amdgpu_device *adev,
						  u32 remote_accel_id, u32 state,
						  u32 generation_count);
static void amdgpu_ualink_invalidate_import_mappings(struct amdgpu_bo *bo);
static int amdgpu_ualink_remote_shootdown(struct amdgpu_device *adev,
					  u32 remote_accel_id, u64 addr,
					  u32 size_in_pages, u32 flush_type);
static void __amdgpu_ualink_activate_vpod_locked(struct amdgpu_device *adev);
static bool amdgpu_ualink_vpod_membership_changed(struct amdgpu_device *adev);
static void amdgpu_ualink_reset_peer_rings(struct amdgpu_device *adev,
					   u32 remote_accel_id);
static int amdgpu_ualink_ring_resync_ptrs(struct amdgpu_device *adev,
					  u32 remote_accel_id);

#define STRIP_NPA(addr)						\
	(((u64)(addr) & ~AMDGPU_UALINK_NPA_ADDR_GPUID_MASK))

#define GENERATE_NPA(addr, remote_acc_id)			\
		((u64)(((u64)(addr)) |				\
		 ((u64)(remote_acc_id) << AMDGPU_UALINK_NPA_ADDR_GPUID_SHIFT)))

static const struct drm_client_funcs ualink_client_funcs = {
	.unregister	= drm_client_release,
};

static int amdgpu_ualink_drm_client_create(struct amdgpu_device *adev)
{
	int ret;

	ret = drm_client_init(&adev->ddev, &adev->ualink.client, "ualink",
			      &ualink_client_funcs);
	if (ret) {
		dev_err(adev->dev, "Failed to init UALink DRM client: %d\n",
			ret);
		return ret;
	}

	drm_client_register(&adev->ualink.client);

	return 0;
}

static void amdgpu_ualink_object_fini(struct amdgpu_device *adev)
{
	if (!adev->ualink.info)
		return;

	kobject_put(&adev->ualink.stations->kobj);
	kobject_put(&adev->ualink.config->kobj);
	kobject_put(&adev->ualink.setup->kobj);
	kobject_put(&adev->ualink.info->kobj);
	adev->ualink.stations = NULL;
	adev->ualink.config = NULL;
	adev->ualink.setup = NULL;
	adev->ualink.info = NULL;
}

static bool __check_ppod_info(struct amdgpu_device *adev,
			      const struct amdgpu_ualink_info *info)
{
	const struct amdgpu_ualink_ppod_info *ppod = &info->ppod;

	if (ppod->size <= 0 || ppod->size > AMDGPU_UALINK_ACCEL_MAX) {
		dev_dbg(adev->dev, "pPod size %u out of range [1..%u]\n",
			ppod->size, AMDGPU_UALINK_ACCEL_MAX);
		return false;
	}
	if (ppod->accel_id >= ppod->size) {
		dev_dbg(adev->dev,
			"Accelerator ID %u greater or equal pPod size %u\n",
			ppod->accel_id, ppod->size);
		return false;
	}

	return true;
}

static bool __check_vpod_info(struct amdgpu_device *adev,
			      const struct amdgpu_ualink_info *info)
{
	const struct amdgpu_ualink_ppod_info *ppod = &info->ppod;
	const struct amdgpu_ualink_vpod_info *vpod = &info->vpod;
	unsigned int weight;

	if (vpod->size == 0 || vpod->size > ppod->size) {
		dev_warn(adev->dev,
			 "UALINK: vPod size %u out of range [1..%u]\n",
			 vpod->size, ppod->size);
		return false;
	}
	if (vpod->addr_mode >= AMDGPU_UALINK_ADDR_MODE_MAX) {
		dev_warn(adev->dev,
			 "UALINK: invalid addr mode %u\n",
			 vpod->addr_mode);
		return false;
	}
	weight =
		bitmap_weight(vpod->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX);
	if (weight != vpod->size) {
		dev_warn(adev->dev,
			 "UALINK: vPod size doesn't match vpod_active_accels list: %u != %u\n",
			 vpod->size, weight);
		return false;
	}
	if (!test_bit(ppod->accel_id, vpod->active_accel_bits)) {
		dev_warn(adev->dev,
			 "UALINK: accelerator ID %u not listed in vpod_active_accels\n",
			 ppod->accel_id);
		return false;
	}

	return true;
}

static void
amdgpu_ualink_info_set_accel_state(struct amdgpu_device *adev,
				   struct amdgpu_ualink_info *info,
				   enum psp_gfx_ual_config_state cfg_state)
{
	enum amdgpu_ualink_accel_state cur;
	enum amdgpu_ualink_accel_state target;
	bool ppod_validated;
	bool vpod_validated;

	if (!info)
		return;

	cur = info->accel_state;

	ppod_validated = cur >= AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED &&
			 cur <= AMDGPU_UALINK_ACCEL_STATE_ACTIVE;
	vpod_validated = cur >= AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED &&
			 cur <= AMDGPU_UALINK_ACCEL_STATE_ACTIVE;

	switch (cfg_state) {
	case UAL_CFG_IDLE:
		return;
	case UAL_CFG_PPOD:
		target = AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED;
		break;
	case UAL_CFG_VPOD:
	case UAL_CFG_STATION:
		target = AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED;
		break;
	case UAL_CFG_COMPLETE:
		target = AMDGPU_UALINK_ACCEL_STATE_READY;
		break;
	default:
		dev_dbg(adev->dev, "invalid configuration state %u", cfg_state);
		return;
	}

	/* ppod stage: should be part of a ppod first */
	if (!ppod_validated && !__check_ppod_info(adev, info)) {
		if (target == AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED) {
			info->accel_state =
				AMDGPU_UALINK_ACCEL_STATE_UNCONFIGURED;
		} else {
			info->accel_state = AMDGPU_UALINK_ACCEL_STATE_ERROR;
			dev_err(adev->dev,
				"ppod configuration is invalid, setting to error state");
		}
		return;
	}
	if (target == AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED) {
		if (!ppod_validated)
			info->accel_state =
				AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED;
		return;
	}

	/* A vpod_id of 0 means the GPU is not part of any vPod */
	if (info->vpod.id == AMDGPU_UALINK_VPOD_ID_INVALID) {
		info->accel_state = AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED;
		return;
	}

	/* vpod stage: required to reach vpod_configured or ready */
	if (!vpod_validated && !__check_vpod_info(adev, info)) {
		info->accel_state = AMDGPU_UALINK_ACCEL_STATE_ERROR;
		dev_err(adev->dev,
			"vpod configuration is invalid, setting to error state");
		return;
	}
	if (target == AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED) {
		if (!vpod_validated)
			info->accel_state =
				AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED;
		return;
	}

	/* complete stage: advance to ready unless already ready/active */
	if (cur < AMDGPU_UALINK_ACCEL_STATE_READY ||
	    cur > AMDGPU_UALINK_ACCEL_STATE_ACTIVE)
		info->accel_state = AMDGPU_UALINK_ACCEL_STATE_READY;
}

static int amdgpu_ualink_update_vpod_config(struct amdgpu_device *adev)
{
	/* TBD: Do updates/cleanup based on updated vpod configuration */
	return 0;
}

static int amdgpu_ualink_update_accel_state(
	struct amdgpu_device *adev, enum amdgpu_ualink_accel_state prev_state,
	u32 prev_vpod_id, enum psp_gfx_ual_config_state cfg_state)
{
	dev_info(adev->dev,
		 "UALINK: update_accel_state: prev_state=%d prev_vpod_id=%u new_vpod_id=%u cfg_state=%d\n",
		 prev_state, prev_vpod_id, adev->ualink.info->vpod.id,
		 cfg_state);

	/* If the device is already active and its vpod_id is unchanged, the
	 * update does not affect vpod membership. Skip the local vpod
	 * integrity check and re-activation.
	 */
	if (prev_state == AMDGPU_UALINK_ACCEL_STATE_ACTIVE &&
	    adev->ualink.info->vpod.id == prev_vpod_id) {
		amdgpu_ualink_update_vpod_config(adev);

		/* Same vpod_id, but the member set may have changed (vPod grown
		 * or shrunk while this GPU stayed ACTIVE). If so, run the local
		 * vpod activation path: once every local peer has committed the
		 * new config (integrity passes), it bounces the affected ACTIVE
		 * peers to rebuild links/GART for the new member set.
		 */
		if (amdgpu_ualink_vpod_membership_changed(adev)) {
			dev_info(adev->dev,
				 "UALINK: update_accel_state: ACTIVE vpod_id=%u membership changed, reconfiguring\n",
				 adev->ualink.info->vpod.id);
			scoped_guard(mutex, &mgpu_info.mutex)
				__amdgpu_ualink_activate_vpod_locked(adev);
		} else {
			dev_info(adev->dev,
				 "UALINK: update_accel_state: already ACTIVE, vpod_id/membership unchanged\n");
		}
		return 0;
	}

	/* A new vpod_id of 0 means this GPU was removed from the vPod. */
	if (adev->ualink.info->vpod.id == AMDGPU_UALINK_VPOD_ID_INVALID) {
		dev_info(adev->dev,
			 "UALINK: update_accel_state: vpod_id=0, removing accelerator from vPod\n");
		amdgpu_ualink_update_vpod_config(adev);
		scoped_guard(mutex, &mgpu_info.mutex)
			deactivate_accelerator(adev);
		return 0;
	}

	/* GPU joining a new vpod should be with invalid vpod id*/
	scoped_guard(mutex, &mgpu_info.mutex) {
		if (prev_vpod_id == AMDGPU_UALINK_VPOD_ID_INVALID) {
			dev_info(adev->dev,
				 "UALINK: update_accel_state: joining vpod_id=%u\n",
				 adev->ualink.info->vpod.id);
			amdgpu_ualink_info_set_accel_state(
				adev, adev->ualink.info, cfg_state);
			__amdgpu_ualink_activate_vpod_locked(adev);
		} else {
			/* GPU should first get removal which will set invalid vpod_id
			* and then join a new vpod
			*/
			dev_err(adev->dev,
				"Invalid vpod transition from %u to %u\n",
				prev_vpod_id, adev->ualink.info->vpod.id);
			return -EINVAL;
		}
	}

	return 0;
}

int amdgpu_ualink_config_update_handler(struct amdgpu_device *adev)
{
	enum amdgpu_ualink_accel_state prev_state;
	enum psp_gfx_ual_config_state cfg_state;
	u32 prev_vpod_id;
	int r, qerr;

	/* TBD: Stop ASP interrupts if driver faced an issue */
	if (adev->ualink.mgr_state != AMDGPU_UALINK_INIT_COMPLETE) {
		u32 status;

		dev_dbg(adev->dev,
			"UALink not initialized, skipping config update\n");
		status = !!(adev->ualink.mgr_state == AMDGPU_UALINK_INIT_ERROR);
		return psp_ual_send_completion(
			&adev->psp, adev->ualink.psp_if_ver,
			PSP_GFX_INT_CTXT_UAL_CMD_CFG_UPDATE_ID, status);
	}

	prev_state = adev->ualink.info->accel_state;
	prev_vpod_id = adev->ualink.info->vpod.id;

	qerr = psp_ual_query_info(&adev->psp, adev->ualink.psp_if_ver,
				  adev->ualink.info, &cfg_state);

	/*TBD: find the right value of status to be sent to ASP*/
	r = psp_ual_send_completion(&adev->psp, adev->ualink.psp_if_ver,
				    PSP_GFX_INT_CTXT_UAL_CMD_CFG_UPDATE_ID, 0);
	if (r || qerr)
		goto err;

	r = amdgpu_ualink_update_accel_state(adev, prev_state, prev_vpod_id,
					     cfg_state);
	if (r)
		goto err;

	return 0;

err:
	scoped_guard(mutex, &mgpu_info.mutex) {
		deactivate_accelerator(adev);
		adev->ualink.info->accel_state =
			AMDGPU_UALINK_ACCEL_STATE_ERROR;
	}
	dev_err(adev->dev,
		"UALink config update failed, setting to error state");

	return r;
}

int amdgpu_ualink_pause_handler(struct amdgpu_device *adev)
{
	u32 status = 0;

	if (adev->ualink.mgr_state != AMDGPU_UALINK_INIT_COMPLETE) {
		dev_dbg(adev->dev,
			"UALink not initialized, skipping pause update\n");
		status = !!(adev->ualink.mgr_state == AMDGPU_UALINK_INIT_ERROR);
		goto out;
	}

	dev_dbg(adev->dev, "UALink pause command is not handled\n");

out:
	return psp_ual_send_completion(&adev->psp, adev->ualink.psp_if_ver,
				       PSP_GFX_INT_CTXT_UAL_CMD_PAUSE_ID,
				       status);
}

int amdgpu_ualink_resume_handler(struct amdgpu_device *adev)
{
	u32 status = 0;

	if (adev->ualink.mgr_state != AMDGPU_UALINK_INIT_COMPLETE) {
		dev_dbg(adev->dev,
			"UALink not initialized, skipping pause update\n");
		status = !!(adev->ualink.mgr_state == AMDGPU_UALINK_INIT_ERROR);
		goto out;
	}

	dev_dbg(adev->dev, "UALink resume command is not handled\n");

out:
	return psp_ual_send_completion(&adev->psp, adev->ualink.psp_if_ver,
				       PSP_GFX_INT_CTXT_UAL_CMD_RESUME_ID,
				       status);
}

int ualink_ip_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	if (!adev->ualink.info)
		return 0;

	adev->ualink.info->accel_state = AMDGPU_UALINK_ACCEL_STATE_UNCONFIGURED;
	r = psp_ual_get_interface_version(&adev->psp, &adev->ualink.psp_if_ver);
	if (r) {
		adev->ualink.psp_if_ver = 0xffffffff;
		goto disable;
	}
	dev_info(adev->dev, "Found UALink interface version 0x%x\n",
		 adev->ualink.psp_if_ver);

	adev->ualink.mgr_state = AMDGPU_UALINK_INIT_HW;

	return 0;
disable:
	adev->ualink.mgr_state = AMDGPU_UALINK_INIT_ERROR;
	return 0;
}

int ualink_ip_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (adev->ualink.mgr_state != AMDGPU_UALINK_INIT_COMPLETE)
		return 0;

	scoped_guard(mutex, &mgpu_info.mutex)
		deactivate_accelerator(adev);

	return 0;
}

int ualink_ip_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	enum psp_gfx_ual_config_state cfg_state;
	int r;

	if (adev->ualink.mgr_state != AMDGPU_UALINK_INIT_HW)
		return 0;

	r = psp_ual_query_info(&adev->psp, adev->ualink.psp_if_ver,
			       adev->ualink.info, &cfg_state);
	if (r)
		return r;

	scoped_guard(mutex, &mgpu_info.mutex) {
		amdgpu_ualink_info_set_accel_state(adev, adev->ualink.info,
						   cfg_state);
		__amdgpu_ualink_activate_vpod_locked(adev);
	}

	r = amdgpu_ualink_drm_client_create(adev);
	if (r) {
		dev_err(adev->dev, "Failed to create UALink DRM client: %d\n",
			r);
		goto error;
	}

	/* Consider UALink initialized only at this stage */
	adev->ualink.mgr_state = AMDGPU_UALINK_INIT_COMPLETE;

	return 0;

error:
	adev->ualink.mgr_state = AMDGPU_UALINK_INIT_ERROR;
	return r;
}

/****************************************************************************
 * UALink info and configuration in sysfs
 *
 * Using kobj_attribute and not device_attribute here because the UALink
 * attributes are part of their own nested kobjects and not the amdgpu
 * device itself or a subdevice.
 ****************************************************************************/

/* UALink configuration APIs are being deprecated before upstreaming and are
 * not enabled by default. They are replaced by a unified in-band/side-band
 * approach through the IFoE driver. Querying of the current configuration
 * will continue to be supported in sysfs.
 *
 * Uncomment the following line to enable the deprecated configuration APIs
 * for testing purposes only. The code to support these APIs will be removed
 * in a future patch.
 */
/* #define UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS */

#define UALINK_VALUE_SHOW(prefix, name, field, format)			\
static ssize_t ualink_##prefix##_##name##_show(struct kobject *kobj,	\
					       struct kobj_attribute *attr,\
					       char *buf)		\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
									\
	return sysfs_emit(buf, format"\n", info->field);		\
}
#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
#define UALINK_VALUE_STORE(prefix, name, field, type, base)		\
static ssize_t ualink_##prefix##_##name##_store(struct kobject *kobj,	\
						struct kobj_attribute *attr,\
						const char *buf, size_t count)\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
	int r;								\
									\
	r = kstrto##type(buf, base, &info->field);			\
	if (r < 0)							\
		return r;						\
	return count;							\
}
#endif

static ssize_t show_idbits(unsigned long *bits, unsigned int nbits, char *buf)
{
	ssize_t len = 0;
	unsigned int id;

	for_each_set_bit(id, bits, nbits)
		len += sysfs_emit_at(buf, len, "%u ", id);
	if (len)
		len--;
	return len + sysfs_emit_at(buf, len, "\n");
}
#define UALINK_IDBITS_SHOW(prefix, name, field)				\
static ssize_t ualink_##prefix##_##name##_show(struct kobject *kobj,	\
					       struct kobj_attribute *attr,\
					       char *buf)		\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
									\
	return show_idbits(info->field, sizeof(info->field)*8, buf);	\
}

#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
static ssize_t store_idbits(unsigned long *bits, unsigned int nbits,
			    const char *buf, size_t count)
{
	char *dup_buf, *str, *tok;
	ssize_t r;
	u32 id;

	bitmap_zero(bits, nbits);
	if (!count)
		return 0;

	dup_buf = kstrndup(buf, count, GFP_KERNEL);
	if (unlikely(!dup_buf))
		return -ENOMEM;
	str = dup_buf;
	do {
		str += strspn(str, " ");
		tok = strsep(&str, ", ");
		r = kstrtou32(tok, 10, &id);
		if (r < 0)
			goto err;
		if (id >= nbits) {
			r = -ERANGE;
			goto err;
		}
		set_bit(id, bits);
	} while (str);
	kfree(dup_buf);

	return count;

err:
	kfree(dup_buf);
	return r;
}
#define UALINK_IDBITS_STORE(prefix, name, field, nbits)			\
static ssize_t ualink_##prefix##_##name##_store(struct kobject *kobj,	\
						struct kobj_attribute *attr,\
						const char *buf, size_t count)\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
	DECLARE_BITMAP(bits, nbits);					\
	ssize_t r = store_idbits(bits, nbits, buf, count);		\
									\
	if (r >= 0)							\
		bitmap_copy(info->field, bits, nbits);			\
	return r;							\
}
#endif

static ssize_t show_idarray(const u32 *array, u32 size, char *buf)
{
	ssize_t len = 0;
	unsigned int i;

	for (i = 0; i < size; i++)
		len += sysfs_emit_at(buf, len, "%u ", array[i]);
	if (len)
		len--;
	return len + sysfs_emit_at(buf, len, "\n");
}
#define UALINK_IDARRAY_SHOW(prefix, name, field, size)			\
static ssize_t ualink_##prefix##_##name##_show(struct kobject *kobj,	\
					       struct kobj_attribute *attr,\
					       char *buf)		\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
									\
	return show_idarray(info->field, info->size, buf);		\
}

#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
static ssize_t store_idarray(u32 *array, u32 *size, u32 max_id, u32 max_size,
			     const char *buf, size_t count)
{
	char *dup_buf, *str, *tok;
	unsigned int index = 0;
	ssize_t r;
	u32 id;

	*size = 0;
	if (!count)
		return 0;

	dup_buf = kstrndup(buf, count, GFP_KERNEL);
	if (unlikely(!dup_buf))
		return -ENOMEM;
	str = dup_buf;
	do {
		str += strspn(str, " ");
		tok = strsep(&str, ", ");
		r = kstrtou32(tok, 10, &id);
		if (r < 0)
			goto err;
		if (index >= max_size) {
			r = -ENOSPC;
			goto err;
		}
		if (id >= max_id) {
			r = -ERANGE;
			goto err;
		}
		array[index++] = id;
	} while (str);
	kfree(dup_buf);

	*size = index;
	return count;

err:
	kfree(dup_buf);
	return r;
}
#define UALINK_IDARRAY_STORE(prefix, name, field, size, max_id)		\
static ssize_t ualink_##prefix##_##name##_store(struct kobject *kobj,	\
						struct kobj_attribute *attr,\
						const char *buf, size_t count)\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
	u32 new_array[ARRAY_SIZE(info->field)];				\
	u32 new_size;							\
	ssize_t r = store_idarray(new_array, &new_size, max_id,		\
				  ARRAY_SIZE(new_array), buf, count);	\
									\
	if (r >= 0) {							\
		memcpy(info->field, new_array, sizeof(new_array));	\
		info->size = new_size;					\
	}								\
	return r;							\
}
#endif

#define UALINK_UUID_SHOW(prefix, name, field)				\
static ssize_t ualink_##prefix##_##name##_show(struct kobject *kobj,	\
					       struct kobj_attribute *attr,\
					       char *buf)		\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
									\
	return sysfs_emit(buf, "%pU\n", &info->field);			\
}
#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
#define UALINK_UUID_STORE(prefix, name, field)				\
static ssize_t ualink_##prefix##_##name##_store(struct kobject *kobj,	\
						struct kobj_attribute *attr,\
						const char *buf, size_t count)\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
	int r;								\
									\
	r = uuid_parse(buf, &info->field);				\
	return r ? r : count;						\
}
#endif

static ssize_t show_enum(u32 x, const char * const values[], unsigned int n,
			 char *buf)
{
	return sysfs_emit(buf, "%s\n", x < n && values[x] ? values[x] : "invalid");
}
#define UALINK_ENUM_SHOW(prefix, name, field)				\
static ssize_t ualink_##prefix##_##name##_show(struct kobject *kobj,	\
					       struct kobj_attribute *attr,\
					       char *buf)		\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
									\
	return show_enum(info->field, ualink_##name##_values,		\
			 ARRAY_SIZE(ualink_##name##_values), buf);	\
}

#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
static ssize_t store_enum(u32 *x, const char * const values[], unsigned int n,
			  const char *buf, size_t count)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (values[i] && sysfs_streq(buf, values[i])) {
			*x = i;
			return count;
		}
	}
	return -EINVAL;
}
#define UALINK_ENUM_STORE(prefix, name, field)				\
static ssize_t ualink_##prefix##_##name##_store(struct kobject *kobj,	\
						struct kobj_attribute *attr,\
						const char *buf, size_t count)\
{									\
	struct amdgpu_ualink_##prefix *info = to_ualink_##prefix(kobj);	\
									\
	return store_enum(&info->field, ualink_##name##_values,		\
			 ARRAY_SIZE(ualink_##name##_values),		\
			 buf, count);					\
}
#endif
static const char * const ualink_link_type_values[] = {
	"UALoE", "UALink"
};
static const char * const ualink_addr_mode_values[] = {
	"source-aliasing", "source-identification"
};
static const char * const ualink_accel_state_values[] = {
	[AMDGPU_UALINK_ACCEL_STATE_UNCONFIGURED]	= "unconfigured",
	[AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED]	= "ppod_configured",
	[AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED]	= "vpod_configured",
	[AMDGPU_UALINK_ACCEL_STATE_READY]		= "ready",
	[AMDGPU_UALINK_ACCEL_STATE_ACTIVE]		= "active",
	[AMDGPU_UALINK_ACCEL_STATE_ERROR]		= "error",
};
static_assert(ARRAY_SIZE(ualink_accel_state_values) ==
	      AMDGPU_UALINK_ACCEL_STATE_MAX);

UALINK_ENUM_SHOW(info, link_type, link_type);
UALINK_VALUE_SHOW(info, accel_id,  ppod.accel_id,  "%u");
UALINK_VALUE_SHOW(info, bandwidth, ppod.bandwidth, "%u");
UALINK_VALUE_SHOW(info, latency,   ppod.latency,   "%u");
UALINK_UUID_SHOW(info, ppod_id, ppod.id);
UALINK_VALUE_SHOW(info, ppod_size, ppod.size,      "%u");
UALINK_VALUE_SHOW(info, vpod_id,   vpod.id,        "%u");
UALINK_VALUE_SHOW(info, vpod_size, vpod.size,      "%u");
UALINK_IDBITS_SHOW(info, vpod_active_accels, vpod.active_accel_bits);
UALINK_ENUM_SHOW(info, addr_mode, vpod.addr_mode);
UALINK_ENUM_SHOW(info, accel_state, accel_state);
UALINK_IDARRAY_SHOW(info, local_accels, local_accels, n_local_accels);

static struct amdgpu_device *ualink_xcp_kobj_to_adev(struct kobject *kobj)
{
	struct amdgpu_xcp *xcp = container_of(kobj, struct amdgpu_xcp,
					      ualink.kobj);

	return xcp->xcp_mgr->adev;
}

#define UALINK_XCP_INFO_SHOW(name)					\
static ssize_t ualink_xcp_info_##name##_show(struct kobject *kobj,	\
		struct kobj_attribute *attr, char *buf)			\
{									\
	struct amdgpu_device *adev = ualink_xcp_kobj_to_adev(kobj);	\
									\
	return ualink_info_##name##_show(&adev->ualink.info->kobj,	\
					 attr, buf);			\
}

UALINK_XCP_INFO_SHOW(link_type)
UALINK_XCP_INFO_SHOW(accel_id)
UALINK_XCP_INFO_SHOW(bandwidth)
UALINK_XCP_INFO_SHOW(latency)
UALINK_XCP_INFO_SHOW(ppod_id)
UALINK_XCP_INFO_SHOW(ppod_size)
UALINK_XCP_INFO_SHOW(vpod_id)
UALINK_XCP_INFO_SHOW(vpod_size)
UALINK_XCP_INFO_SHOW(vpod_active_accels)
UALINK_XCP_INFO_SHOW(addr_mode)
UALINK_XCP_INFO_SHOW(accel_state)
UALINK_XCP_INFO_SHOW(local_accels)

#define UALINK_INFO_ATTR(name) __ATTR(name, 0444, ualink_info_##name##_show, NULL)
static struct kobj_attribute ualink_info_link_type = UALINK_INFO_ATTR(link_type);
static struct kobj_attribute ualink_info_accel_id  = UALINK_INFO_ATTR(accel_id);
static struct kobj_attribute ualink_info_bandwidth = UALINK_INFO_ATTR(bandwidth);
static struct kobj_attribute ualink_info_latency   = UALINK_INFO_ATTR(latency);
static struct kobj_attribute ualink_info_ppod_id   = UALINK_INFO_ATTR(ppod_id);
static struct kobj_attribute ualink_info_ppod_size = UALINK_INFO_ATTR(ppod_size);
static struct kobj_attribute ualink_info_vpod_id   = UALINK_INFO_ATTR(vpod_id);
static struct kobj_attribute ualink_info_vpod_size = UALINK_INFO_ATTR(vpod_size);
static struct kobj_attribute ualink_info_vpod_active_accels = UALINK_INFO_ATTR(vpod_active_accels);
static struct kobj_attribute ualink_info_addr_mode = UALINK_INFO_ATTR(addr_mode);
static struct kobj_attribute ualink_info_accel_state = UALINK_INFO_ATTR(accel_state);
static struct kobj_attribute ualink_info_local_accels = UALINK_INFO_ATTR(local_accels);

static const struct attribute *ualink_info_attrs[] = {
	&ualink_info_link_type.attr,
	&ualink_info_accel_id.attr,
	&ualink_info_bandwidth.attr,
	&ualink_info_latency.attr,
	&ualink_info_ppod_id.attr,
	&ualink_info_ppod_size.attr,
	&ualink_info_vpod_id.attr,
	&ualink_info_vpod_size.attr,
	&ualink_info_vpod_active_accels.attr,
	&ualink_info_addr_mode.attr,
	&ualink_info_accel_state.attr,
	&ualink_info_local_accels.attr,
	NULL
};

#define UALINK_XCP_INFO_ATTR(name) \
	__ATTR(name, 0444, ualink_xcp_info_##name##_show, NULL)
static struct kobj_attribute ualink_xcp_info_link_type = UALINK_XCP_INFO_ATTR(link_type);
static struct kobj_attribute ualink_xcp_info_accel_id  = UALINK_XCP_INFO_ATTR(accel_id);
static struct kobj_attribute ualink_xcp_info_bandwidth = UALINK_XCP_INFO_ATTR(bandwidth);
static struct kobj_attribute ualink_xcp_info_latency   = UALINK_XCP_INFO_ATTR(latency);
static struct kobj_attribute ualink_xcp_info_ppod_id   = UALINK_XCP_INFO_ATTR(ppod_id);
static struct kobj_attribute ualink_xcp_info_ppod_size = UALINK_XCP_INFO_ATTR(ppod_size);
static struct kobj_attribute ualink_xcp_info_vpod_id   = UALINK_XCP_INFO_ATTR(vpod_id);
static struct kobj_attribute ualink_xcp_info_vpod_size = UALINK_XCP_INFO_ATTR(vpod_size);
static struct kobj_attribute ualink_xcp_info_vpod_active_accels = UALINK_XCP_INFO_ATTR(vpod_active_accels);
static struct kobj_attribute ualink_xcp_info_addr_mode = UALINK_XCP_INFO_ATTR(addr_mode);
static struct kobj_attribute ualink_xcp_info_accel_state = UALINK_XCP_INFO_ATTR(accel_state);
static struct kobj_attribute ualink_xcp_info_local_accels = UALINK_XCP_INFO_ATTR(local_accels);

static struct attribute *ualink_xcp_info_attrs[] = {
	&ualink_xcp_info_link_type.attr,
	&ualink_xcp_info_accel_id.attr,
	&ualink_xcp_info_bandwidth.attr,
	&ualink_xcp_info_latency.attr,
	&ualink_xcp_info_ppod_id.attr,
	&ualink_xcp_info_ppod_size.attr,
	&ualink_xcp_info_vpod_id.attr,
	&ualink_xcp_info_vpod_size.attr,
	&ualink_xcp_info_vpod_active_accels.attr,
	&ualink_xcp_info_addr_mode.attr,
	&ualink_xcp_info_accel_state.attr,
	&ualink_xcp_info_local_accels.attr,
	NULL
};

static umode_t ualink_xcp_info_is_visible(struct kobject *kobj,
					  struct attribute *attr, int n)
{
	struct amdgpu_xcp *xcp = container_of(kobj, struct amdgpu_xcp,
					      ualink.kobj);

	if (!xcp->valid)
		return 0;

	return attr->mode;
}

static const struct attribute_group ualink_xcp_info_group = {
	.attrs = ualink_xcp_info_attrs,
	.is_visible = ualink_xcp_info_is_visible,
};

static const struct kobj_type ualink_xcp_info_ktype = {
	.sysfs_ops = &kobj_sysfs_ops
};

static void ualink_info_release(struct kobject *kobj)
{
	kfree(to_ualink_info(kobj));
}

static const struct kobj_type ualink_info_ktype = {
	.release = ualink_info_release,
	.sysfs_ops = &kobj_sysfs_ops
};

#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
UALINK_VALUE_SHOW(ppod_setup, accel_id,  ppod.accel_id,  "%u");
UALINK_VALUE_SHOW(ppod_setup, bandwidth, ppod.bandwidth, "%u");
UALINK_VALUE_SHOW(ppod_setup, latency,   ppod.latency,   "%u");
UALINK_UUID_SHOW(ppod_setup, ppod_id, ppod.id);
UALINK_VALUE_SHOW(ppod_setup, ppod_size, ppod.size,      "%u");
UALINK_IDARRAY_SHOW(ppod_setup, local_accels, local_accels, n_local_accels);

UALINK_VALUE_STORE(ppod_setup, accel_id,  ppod.accel_id,  u32, 10);
UALINK_VALUE_STORE(ppod_setup, bandwidth, ppod.bandwidth, u32, 10);
UALINK_VALUE_STORE(ppod_setup, latency,   ppod.latency,   u32, 10);
UALINK_UUID_STORE(ppod_setup, ppod_id,   ppod.id);
UALINK_VALUE_STORE(ppod_setup, ppod_size, ppod.size,      u32, 10);
UALINK_IDARRAY_STORE(ppod_setup, local_accels, local_accels, n_local_accels,
		     AMDGPU_UALINK_ACCEL_MAX);

static enum amdgpu_ualink_accel_state
check_ppod_state(struct amdgpu_device *adev,
		 const struct amdgpu_ualink_ppod_setup *setup)
{
	if (setup->ppod.size <= 0 || setup->ppod.size > AMDGPU_UALINK_ACCEL_MAX) {
		dev_dbg(adev->dev, "pPod size %u out of range [1..%u]\n",
			setup->ppod.size, AMDGPU_UALINK_ACCEL_MAX);
		return AMDGPU_UALINK_ACCEL_STATE_UNCONFIGURED;
	}
	if (setup->ppod.accel_id >= setup->ppod.size) {
		dev_dbg(adev->dev, "Accelerator ID %u greater or equal pPod size %u\n",
			setup->ppod.accel_id, setup->ppod.size);
		return AMDGPU_UALINK_ACCEL_STATE_UNCONFIGURED;
	}
	return AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED;
}

static ssize_t ualink_ppod_setup_commit_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	struct amdgpu_ualink_ppod_setup *setup = to_ualink_ppod_setup(kobj);
	struct amdgpu_ualink_info *info = to_ualink_info(kobj->parent);
	struct device *dev = kobj_to_dev(info->kobj.parent);
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int r;

	if (!sysfs_streq(buf, "true"))
		return -EINVAL;

	r = psp_ual_set_ppod_config(&adev->psp, adev->ualink.psp_if_ver,
				    setup);
	if (r)
		return r;
	r = psp_ual_query_info(&adev->psp, adev->ualink.psp_if_ver, info, NULL);
	if (r)
		return r;

	/*
	 * Hold mgpu_info.mutex to serialize with activate_local_vpod() in
	 * vpod_config_commit_store which also holds this lock. Without it,
	 * deactivate_accelerator() can race with activate_accelerator(),
	 * causing concurrent vm_fini / vm_init on the same NPA VM.
	 */
	mutex_lock(&mgpu_info.mutex);
	if (info->accel_state >= AMDGPU_UALINK_ACCEL_STATE_READY)
		deactivate_accelerator(adev);
	info->accel_state = check_ppod_state(adev, setup);
	/* PPOD is expected to be configured first */
	info->vpod.id = AMDGPU_UALINK_VPOD_ID_INVALID;
	mutex_unlock(&mgpu_info.mutex);

	/* TODO: If accel_state was ACTIVE, reset all connections */

	return count;
}

#define UALINK_PPOD_SETUP_ATTR(name) __ATTR(name, 0600,			\
		ualink_ppod_setup_##name##_show,			\
		ualink_ppod_setup_##name##_store)
static struct kobj_attribute ualink_ppod_setup_accel_id  = UALINK_PPOD_SETUP_ATTR(accel_id);
static struct kobj_attribute ualink_ppod_setup_bandwidth = UALINK_PPOD_SETUP_ATTR(bandwidth);
static struct kobj_attribute ualink_ppod_setup_latency   = UALINK_PPOD_SETUP_ATTR(latency);
static struct kobj_attribute ualink_ppod_setup_ppod_id   = UALINK_PPOD_SETUP_ATTR(ppod_id);
static struct kobj_attribute ualink_ppod_setup_ppod_size = UALINK_PPOD_SETUP_ATTR(ppod_size);
static struct kobj_attribute ualink_ppod_setup_local_accels = UALINK_PPOD_SETUP_ATTR(local_accels);
static struct kobj_attribute ualink_ppod_setup_commit = __ATTR(commit, 0200, NULL,
							       ualink_ppod_setup_commit_store);

static const struct attribute *ualink_ppod_setup_attrs[] = {
	&ualink_ppod_setup_accel_id.attr,
	&ualink_ppod_setup_bandwidth.attr,
	&ualink_ppod_setup_latency.attr,
	&ualink_ppod_setup_ppod_id.attr,
	&ualink_ppod_setup_ppod_size.attr,
	&ualink_ppod_setup_local_accels.attr,
	&ualink_ppod_setup_commit.attr,
	NULL
};
#endif

static void ualink_ppod_setup_release(struct kobject *kobj)
{
	kfree(to_ualink_ppod_setup(kobj));
}

static const struct kobj_type ualink_ppod_setup_ktype = {
	.release = ualink_ppod_setup_release,
	.sysfs_ops = &kobj_sysfs_ops
};

static struct amdgpu_device *find_peer_adev(unsigned int accel_id)
{
	unsigned int i;

	for (i = 0; i < mgpu_info.num_gpu; i++) {
		struct amdgpu_device *peer_adev = mgpu_info.gpu_ins[i].adev;

		if (peer_adev->ualink.info &&
		    peer_adev->ualink.info->ppod.accel_id == accel_id)
			return peer_adev;
	}

	return NULL;
}

static bool amdgpu_ualink_is_local_accel(struct amdgpu_device *adev,
					 u32 accel_id)
{
	struct amdgpu_ualink_info *info = adev->ualink.info;
	unsigned int i;

	if (!info)
		return false;

	for (i = 0; i < info->n_local_accels; i++) {
		if (info->local_accels[i] == accel_id)
			return true;
	}
	return false;
}

static void activate_accelerator(struct amdgpu_device *adev)
{
	int r;

	if (adev->ualink.info->accel_state >= AMDGPU_UALINK_ACCEL_STATE_ACTIVE)
		return;

	dev_info(adev->dev,
		 "UALINK: activating accelerator accel_id=%u (accel_state=%d)\n",
		 adev->ualink.info->ppod.accel_id,
		 adev->ualink.info->accel_state);

	/* Enable incoming NPA address translation with NPA VMID */
	r = psp_ual_set_npa_config(&adev->psp, adev->ualink.psp_if_ver,
				   adev->vm_manager.npa_vmid, true);
	if (r) {
		dev_err(adev->dev, "Failed to set NPA config\n");
		return;
	}

	r = amdgpu_ualink_manager_start(adev);
	if (r) {
		dev_err(adev->dev, "Failed to start UALink manager\n");
		return;
	}

	r = amdgpu_ualink_sw_init(adev);
	if (r) {
		dev_err(adev->dev, "Failed to init UALink sw r=%d\n", r);
		amdgpu_ualink_manager_stop(adev);
		return;
	}

	adev->ualink.info->accel_state = AMDGPU_UALINK_ACCEL_STATE_ACTIVE;
	dev_info(adev->dev,
		 "UALINK: accelerator accel_id=%u is now ACTIVE\n",
		 adev->ualink.info->ppod.accel_id);
}

static void deactivate_accelerator(struct amdgpu_device *adev)
{
	if (adev->ualink.info->accel_state < AMDGPU_UALINK_ACCEL_STATE_ACTIVE)
		return;

	dev_info(adev->dev,
		 "UALINK: deactivating accelerator accel_id=%u (accel_state=%d -> PPOD_CONFIGURED)\n",
		 adev->ualink.info->ppod.accel_id,
		 adev->ualink.info->accel_state);

	/* Disable incoming NPA address translation with NPA VMID */
	psp_ual_set_npa_config(&adev->psp, adev->ualink.psp_if_ver,
			       adev->vm_manager.npa_vmid, false);
	/* ignore return value */
	adev->ualink.info->accel_state =
		AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED;

	amdgpu_ualink_sw_fini(adev);
	amdgpu_ualink_manager_stop(adev);
}

static void activate_local_vpod(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_info *info = adev->ualink.info;
	struct amdgpu_device *peer_adev;
	unsigned int accel_id;
	unsigned int i;

	for (i = 0; i < info->n_local_accels; i++) {
		accel_id = info->local_accels[i];

		peer_adev = find_peer_adev(accel_id);
		if (WARN_ON(!peer_adev || !peer_adev->ualink.info))
			/* info->local_accels is corrupted? */
			continue;

		/* Bring the peer up to match the current vPod membership.
		 *
		 * A peer that is not yet ACTIVE (or whose ACTIVE membership
		 * changed on a grow/shrink) has its remote metadata, links and
		 * GART mappings built for a stale member set (or none at all).
		 * Firmware only accepts a full metadata reload while halted, so
		 * surgical per-peer deltas are not possible: fully bounce the
		 * accelerator. deactivate_accelerator() is a no-op when the peer
		 * is not ACTIVE, so this handles first-time bring-up too.
		 * Unchanged ACTIVE peers are left untouched.
		 */
		if (amdgpu_ualink_vpod_membership_changed(peer_adev)) {
			dev_info(peer_adev->dev,
				 "UALINK: (re)configuring vpod for accel_id=%u\n",
				 peer_adev->ualink.info->ppod.accel_id);
			deactivate_accelerator(peer_adev);
			activate_accelerator(peer_adev);
		}
	}
}

static inline bool __is_vpod_peer(struct amdgpu_ualink_info *info,
				  struct amdgpu_ualink_info *peer_info)
{
	return peer_info->vpod.id == info->vpod.id &&
	       uuid_equal(&peer_info->ppod.id, &info->ppod.id);
}

static int __check_local_vpod_integrity(struct amdgpu_device *adev)
{
	DECLARE_BITMAP(local_accel_ids, AMDGPU_UALINK_ACCEL_MAX);
	struct amdgpu_ualink_info *info = adev->ualink.info;
	u32 local_accels[AMDGPU_UALINK_LOCAL_ACCELS_MAX];
	struct amdgpu_ualink_info *peer_info;
	struct amdgpu_device *peer_adev;
	unsigned int i, n_local_accels;
	unsigned int accel_id;
	/* Check that all local accelerators listed in vpod_active_accels have
	 * matching pod IDs
	 */
	bitmap_zero(local_accel_ids, AMDGPU_UALINK_ACCEL_MAX);
	n_local_accels = 0;
	__set_bit(info->ppod.accel_id, local_accel_ids);
	local_accels[n_local_accels++] = info->ppod.accel_id;

	for (i = 0; i < mgpu_info.num_gpu; i++) {
		peer_adev = mgpu_info.gpu_ins[i].adev;
		if (peer_adev == adev || !peer_adev->ualink.info)
			continue;

		peer_info = peer_adev->ualink.info;
		/* peer device ppod not configured */
		if (peer_info->accel_state <
		    AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED) {
			dev_info(adev->dev,
				 "UALINK: integrity EAGAIN: peer accel_id=%u not ppod-configured (accel_state=%d)\n",
				 peer_info->ppod.accel_id,
				 peer_info->accel_state);
			return -EAGAIN;
		}

		accel_id = peer_info->ppod.accel_id;
		if (!test_bit(accel_id, info->vpod.active_accel_bits))
			continue;
		/* peer device vpod not configured */
		if (peer_info->accel_state <
		    AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED) {
			dev_info(adev->dev,
				 "UALINK: integrity EAGAIN: vpod peer accel_id=%u not vpod-configured (accel_state=%d)\n",
				 accel_id, peer_info->accel_state);
			return -EAGAIN;
		}

		if (!uuid_equal(&peer_info->ppod.id, &info->ppod.id)) {
			dev_warn(adev->dev,
				 "UALINK: integrity fail: peer %u ppod_id doesn't match: %pU != %pU\n",
				 peer_info->ppod.accel_id, &peer_info->ppod.id,
				 &info->ppod.id);
			return -EINVAL;
		}

		if (peer_info->ppod.size != info->ppod.size) {
			dev_warn(adev->dev,
				 "UALINK: integrity fail: peer %u ppod_size doesn't match: %u != %u\n",
				 accel_id, peer_info->ppod.size,
				 info->ppod.size);
			return -EINVAL;
		}

		if (peer_info->vpod.id != info->vpod.id) {
			dev_warn(adev->dev,
				 "UALINK: integrity fail: peer %u vpod_id doesn't match: %u != %u\n",
				 accel_id, peer_info->vpod.id, info->vpod.id);
			return -EINVAL;
		}
		if (peer_info->vpod.size != info->vpod.size) {
			dev_warn(adev->dev,
				 "UALINK: integrity fail: peer %u vpod_size doesn't match: %u != %u\n",
				 accel_id, peer_info->vpod.size,
				 info->vpod.size);
			return -EINVAL;
		}
		if (peer_info->vpod.addr_mode != info->vpod.addr_mode) {
			dev_warn(adev->dev,
				 "UALINK: integrity fail: peer %u addr_mode doesn't match: %u != %u\n",
				 accel_id, peer_info->vpod.addr_mode,
				 info->vpod.addr_mode);
			return -EINVAL;
		}
		if (!bitmap_equal(peer_info->vpod.active_accel_bits,
				  info->vpod.active_accel_bits,
				  AMDGPU_UALINK_ACCEL_MAX)) {
			dev_warn(adev->dev,
				 "UALINK: integrity fail: peer %u vpod_active_accels don't match\n",
				 accel_id);
			return -EINVAL;
		}

		if (__test_and_set_bit(accel_id, local_accel_ids)) {
			dev_warn(adev->dev,
				 "UALINK: integrity fail: duplicate accel_id %u among local vpod peers\n",
				 accel_id);
			return -EINVAL;
		}
		local_accels[n_local_accels++] = accel_id;
	}

	for (i = 0; i < mgpu_info.num_gpu; i++) {
		peer_adev = mgpu_info.gpu_ins[i].adev;
		peer_info = peer_adev->ualink.info;

		if (!peer_info)
			continue;
		if (peer_adev != adev && !__is_vpod_peer(info, peer_info))
			continue;

		peer_info->n_local_accels = n_local_accels;
		memcpy(peer_info->local_accels, local_accels,
		       sizeof(local_accels));
	}

	return 0;
}

static void __amdgpu_ualink_activate_vpod_locked(struct amdgpu_device *adev)
{
	int ret;

	if (adev->ualink.info->accel_state <
		    AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED ||
	    adev->ualink.info->accel_state ==
		    AMDGPU_UALINK_ACCEL_STATE_ERROR)
		return;

	ret = __check_local_vpod_integrity(adev);
	if (ret && ret != -EAGAIN) {
		dev_err(adev->dev,
			"Local vpod integrity check failed: %d\n", ret);
		return;
	}
	if (ret == -EAGAIN) {
		dev_info(adev->dev,
			 "UALINK: activate deferred, waiting for local vpod peers to reach VPOD_CONFIGURED\n");
		return;
	}

	dev_info(adev->dev,
		 "UALINK: integrity OK, applying local vpod (%u local accels)\n",
		 adev->ualink.info->n_local_accels);
	activate_local_vpod(adev);
}

#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
UALINK_VALUE_SHOW(vpod_config, vpod_id,   vpod.id,   "%u");
UALINK_VALUE_SHOW(vpod_config, vpod_size, vpod.size, "%u");
UALINK_IDBITS_SHOW(vpod_config, vpod_active_accels, vpod.active_accel_bits);
UALINK_ENUM_SHOW(vpod_config, addr_mode, vpod.addr_mode);

UALINK_VALUE_STORE(vpod_config, vpod_id,   vpod.id,   u32, 10);
UALINK_VALUE_STORE(vpod_config, vpod_size, vpod.size, u32, 10);
UALINK_IDBITS_STORE(vpod_config, vpod_active_accels, vpod.active_accel_bits,
		    AMDGPU_UALINK_ACCEL_MAX);
UALINK_ENUM_STORE(vpod_config, addr_mode, vpod.addr_mode);

static ssize_t ualink_vpod_config_commit_store(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       const char *buf, size_t count)
{
	struct amdgpu_ualink_vpod_config *config = to_ualink_vpod_config(kobj);
	struct amdgpu_ualink_info *info = to_ualink_info(kobj->parent);
	struct device *dev = kobj_to_dev(info->kobj.parent);
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	enum amdgpu_ualink_accel_state prev_state;
	u32 prev_vpod_id;
	int r;

	if (!sysfs_streq(buf, "true"))
		return -EINVAL;

	dev_dbg(adev->dev,
		"UALINK: vpod-commit enter: accel_state=%d staged vpod_id=%u vpod_size=%u addr_mode=%u\n",
		info->accel_state, config->vpod.id, config->vpod.size,
		config->vpod.addr_mode);

	if (info->accel_state < AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED) {
		dev_warn(adev->dev,
			 "UALINK: vpod-commit rejected, ppod not configured (accel_state=%d)\n",
			 info->accel_state);
		return -EINVAL;
	}

	prev_state = info->accel_state;
	prev_vpod_id = info->vpod.id;
	r = psp_ual_set_vpod_config(&adev->psp, adev->ualink.psp_if_ver,
				    config);
	if (r) {
		dev_warn(adev->dev,
			 "UALINK: vpod-commit psp_ual_set_vpod_config failed: %d\n",
			 r);
		return r;
	}

	r = psp_ual_query_info(&adev->psp, adev->ualink.psp_if_ver, info, NULL);
	if (r) {
		dev_warn(adev->dev,
			 "UALINK: vpod-commit psp_ual_query_info failed: %d\n",
			 r);
		return r;
	}

	dev_dbg(adev->dev,
		"UALINK: vpod-commit fw read-back: vpod_id=%u vpod_size=%u accel_id=%u ppod.size=%u addr_mode=%u\n",
		info->vpod.id, info->vpod.size, info->ppod.accel_id,
		info->ppod.size, info->vpod.addr_mode);

	if (info->vpod.id != AMDGPU_UALINK_VPOD_ID_INVALID &&
	    !__check_vpod_info(adev, info)) {
		dev_err(adev->dev,
			"UALINK: vpod-commit __check_vpod_info() failed: %d\n",
			r);
		return -EINVAL;
	}

	/* The integrity check makes sure each new GPU is consistent with the
	 * other GPUs already in the vPod. All known local GPUs can become
	 * "ready" at the same time.
	 *
	 * Misconfiguration of one GPU does not reduce the state of other GPUs
	 * already in the vPod. GPU is intentionally not put to ERROR state if
	 * misconfiguration occurs.
	 */
	r = amdgpu_ualink_update_accel_state(adev, prev_state, prev_vpod_id,
					     UAL_CFG_VPOD);
	if (r)
		return r;

	return count;
}

#define UALINK_VPOD_CONFIG_ATTR(name) __ATTR(name, 0600,		\
		ualink_vpod_config_##name##_show,			\
		ualink_vpod_config_##name##_store)
static struct kobj_attribute ualink_vpod_config_vpod_id   = UALINK_VPOD_CONFIG_ATTR(vpod_id);
static struct kobj_attribute ualink_vpod_config_vpod_size = UALINK_VPOD_CONFIG_ATTR(vpod_size);
static struct kobj_attribute ualink_vpod_config_vpod_active_accels =
						UALINK_VPOD_CONFIG_ATTR(vpod_active_accels);
static struct kobj_attribute ualink_vpod_config_addr_mode = UALINK_VPOD_CONFIG_ATTR(addr_mode);
static struct kobj_attribute ualink_vpod_config_commit = __ATTR(commit, 0200, NULL,
								ualink_vpod_config_commit_store);

static const struct attribute *ualink_vpod_config_attrs[] = {
	&ualink_vpod_config_vpod_id.attr,
	&ualink_vpod_config_vpod_size.attr,
	&ualink_vpod_config_vpod_active_accels.attr,
	&ualink_vpod_config_addr_mode.attr,
	&ualink_vpod_config_commit.attr,
	NULL
};
#endif

static void ualink_vpod_config_release(struct kobject *kobj)
{
	kfree(to_ualink_vpod_config(kobj));
}

static const struct kobj_type ualink_vpod_config_ktype = {
	.release = ualink_vpod_config_release,
	.sysfs_ops = &kobj_sysfs_ops
};

#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
UALINK_VALUE_SHOW(station_config, flags, flags, "0x%x");

UALINK_VALUE_STORE(station_config, flags, flags, u8, 16);

static size_t lane_bitmap_to_string(char *str,
		const struct amdgpu_ualink_station_config *stations)
{
	size_t i;

	for (i = 0; i < stations->n_stations; i++)
		str[i] = stations->lane_en_bitmap[i] +
			(stations->lane_en_bitmap[i] < 10 ? '0' : 'a' - 10);
	str[i] = '\0';

	return i;
}
static ssize_t
ualink_station_config_lane_en_bitmap_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	struct amdgpu_ualink_station_config *stations = to_ualink_station_config(kobj);
	char str[AMDGPU_UALINK_STATIONS_MAX + 1];

	lane_bitmap_to_string(str, stations);
	return sysfs_emit(buf, "%s\n", str);
}
static ssize_t
ualink_station_config_lane_en_bitmap_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	struct amdgpu_ualink_station_config *stations = to_ualink_station_config(kobj);
	u8 lane_en_bitmap[AMDGPU_UALINK_STATIONS_MAX];
	u32 n_stations = 0;
	bool end = false;
	size_t i;

	for (i = 0; i < count; i++) {
		/* Accept any number of \n in the end */
		if (buf[i] == '\n') {
			end = true;
			continue;
		}
		/* Don't accept any more data after \n */
		if (end)
			return -EINVAL;
		/* Don't accept more data than the array size */
		if (i >= sizeof(lane_en_bitmap))
			return -ENOSPC;
		/* Accept hex digits */
		if (buf[i] >= '0' && buf[i] <= '9')
			lane_en_bitmap[n_stations++] = buf[i] - '0';
		else if (buf[i] >= 'a' && buf[i] <= 'f')
			lane_en_bitmap[n_stations++] = buf[i] + 10 - 'a';
		else if (buf[i] >= 'A' && buf[i] <= 'F')
			lane_en_bitmap[n_stations++] = buf[i] + 10 - 'A';
		else
			return -EINVAL;
	}

	memcpy(stations->lane_en_bitmap, lane_en_bitmap, sizeof(lane_en_bitmap));
	stations->n_stations = n_stations;
	return i;
}

static ssize_t ualink_station_config_commit_store(struct kobject *kobj,
						  struct kobj_attribute *attr,
						  const char *buf, size_t count)
{
	struct amdgpu_ualink_station_config *stations = to_ualink_station_config(kobj);
	struct amdgpu_ualink_info *info = to_ualink_info(kobj->parent);
	struct device *dev = kobj_to_dev(info->kobj.parent);
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int r;

	/* DF reconfiguration does not interact with accelerator state */

	if (!sysfs_streq(buf, "true"))
		return -EINVAL;

	r = psp_ual_set_station_config(&adev->psp, adev->ualink.psp_if_ver, stations);
	if (r)
		return r;

	return count;
}

#define UALINK_STATION_CONFIG_ATTR(name) __ATTR(name, 0600,		\
		ualink_station_config_##name##_show,			\
		ualink_station_config_##name##_store)
static struct kobj_attribute ualink_station_config_flags  = UALINK_STATION_CONFIG_ATTR(flags);
static struct kobj_attribute ualink_station_config_lane_en_bitmap =
							UALINK_STATION_CONFIG_ATTR(lane_en_bitmap);
static struct kobj_attribute ualink_station_config_commit =
				__ATTR(commit, 0200, NULL, ualink_station_config_commit_store);

static const struct attribute *ualink_station_config_attrs[] = {
	&ualink_station_config_flags.attr,
	&ualink_station_config_lane_en_bitmap.attr,
	&ualink_station_config_commit.attr,
	NULL
};
#endif

static void ualink_station_config_release(struct kobject *kobj)
{
	kfree(to_ualink_station_config(kobj));
}

static const struct kobj_type ualink_station_config_ktype = {
	.release = ualink_station_config_release,
	.sysfs_ops = &kobj_sysfs_ops
};

int ualink_ip_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ualink_station_config *stations;
	struct amdgpu_ualink_vpod_config *vpod_config;
	struct amdgpu_ualink_ppod_setup *ppod_setup;
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ualink_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	ppod_setup = kzalloc(sizeof(*ppod_setup), GFP_KERNEL);
	vpod_config = kzalloc(sizeof(*vpod_config), GFP_KERNEL);
	stations = kzalloc(sizeof(*stations), GFP_KERNEL);
	if (!info || !ppod_setup || !vpod_config || !stations) {
		kfree(info);
		kfree(ppod_setup);
		kfree(vpod_config);
		kfree(stations);
		return -ENOMEM;
	}

	info->ppod.accel_id = 0xffffffff;
	info->ppod.bandwidth = 0xffffffff;
	info->ppod.latency = 0xffffffff;
	info->vpod.id = AMDGPU_UALINK_VPOD_ID_INVALID;
	info->vpod.addr_mode = AMDGPU_UALINK_ADDR_MODE_MAX;

	/*
	 * Initialize the kobjects here so their lifetime is tied to the UALink
	 * manager software state. amdgpu_ualink_object_fini() drops the final
	 * reference via kobject_put().
	 */
	kobject_init(&info->kobj, &ualink_info_ktype);
	kobject_init(&ppod_setup->kobj, &ualink_ppod_setup_ktype);
	kobject_init(&vpod_config->kobj, &ualink_vpod_config_ktype);
	kobject_init(&stations->kobj, &ualink_station_config_ktype);

	adev->ualink.info = info;
	adev->ualink.setup = ppod_setup;
	adev->ualink.config = vpod_config;
	adev->ualink.stations = stations;

	return 0;
}

int ualink_ip_sw_fini(struct amdgpu_ip_block *ip_block)
{
	amdgpu_ualink_object_fini(ip_block->adev);
	return 0;
}

static int ualink_kobj_add(struct kobject *kobj, struct kobject *parent,
			   const char *name, const struct attribute **attrs)
{
	int r;

	r = kobject_add(kobj, parent, "%s", name);
	if (r)
		return r;
	r = sysfs_create_files(kobj, attrs);
	if (r)
		kobject_del(kobj);

	return r;
}

static void amdgpu_ualink_xcp_sysfs_init(struct amdgpu_device *adev);
static void amdgpu_ualink_xcp_sysfs_fini(struct amdgpu_device *adev);

int amdgpu_ualink_sysfs_init(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_info *info = adev->ualink.info;
	int r;

	if (adev->ualink.mgr_state != AMDGPU_UALINK_INIT_COMPLETE)
		return 0;

	/* ualink parent node */
	r = ualink_kobj_add(&info->kobj, &adev->dev->kobj, "ualink",
			    ualink_info_attrs);
	if (r)
		goto err;

#ifdef UALINK_ENABLE_DEPRECATED_CONFIG_SYSFS
	r = ualink_kobj_add(&adev->ualink.setup->kobj, &info->kobj, "setup",
			    ualink_ppod_setup_attrs);
	if (r)
		goto err_info;

	r = ualink_kobj_add(&adev->ualink.config->kobj, &info->kobj, "config",
			    ualink_vpod_config_attrs);
	if (r)
		goto err_setup;

	r = ualink_kobj_add(&adev->ualink.stations->kobj, &info->kobj,
			    "stations", ualink_station_config_attrs);
	if (r)
		goto err_config;
#else
	r = kobject_add(&adev->ualink.setup->kobj, &info->kobj, "%s",
			"setup");
	if (r)
		goto err_info;

	r = kobject_add(&adev->ualink.config->kobj, &info->kobj, "%s",
			"config");
	if (r)
		goto err_setup;

	r = kobject_add(&adev->ualink.stations->kobj, &info->kobj, "%s",
			"stations");
	if (r)
		goto err_config;
#endif

	amdgpu_ualink_xcp_sysfs_init(adev);
	adev->ualink.sysfs_init = true;

	return 0;

err_config:
	kobject_del(&adev->ualink.config->kobj);
err_setup:
	kobject_del(&adev->ualink.setup->kobj);
err_info:
	kobject_del(&info->kobj);
err:
	dev_warn(adev->dev, "Failed to create UALink sysfs: %d\n", r);
	return 0;
}

void amdgpu_ualink_sysfs_fini(struct amdgpu_device *adev)
{
	if (!adev->ualink.sysfs_init)
		return;

	amdgpu_ualink_xcp_sysfs_fini(adev);
	kobject_del(&adev->ualink.stations->kobj);
	kobject_del(&adev->ualink.config->kobj);
	kobject_del(&adev->ualink.setup->kobj);
	kobject_del(&adev->ualink.info->kobj);
	adev->ualink.sysfs_init = false;
}

static int amdgpu_ualink_xcp_sysfs_add(struct amdgpu_xcp *xcp)
{
	struct amdgpu_device *adev = xcp->xcp_mgr->adev;
	int r;

	r = kobject_init_and_add(&xcp->ualink.kobj, &ualink_xcp_info_ktype,
				 &xcp->ddev->dev->kobj, "ualink");
	if (r)
		goto err;

	r = sysfs_create_group(&xcp->ualink.kobj, &ualink_xcp_info_group);
	if (r)
		goto err;

	/* pin info so it outlives this node regardless of teardown order */
	kobject_get(&adev->ualink.info->kobj);
	xcp->ualink.sysfs = true;
	return 0;
err:
	kobject_put(&xcp->ualink.kobj);
	return r;
}

void amdgpu_ualink_xcp_sysfs_update(struct amdgpu_xcp *xcp)
{
	if (!xcp->ualink.sysfs)
		return;

	sysfs_update_group(&xcp->ualink.kobj, &ualink_xcp_info_group);
}

static void amdgpu_ualink_xcp_sysfs_remove(struct amdgpu_xcp *xcp)
{
	struct amdgpu_device *adev = xcp->xcp_mgr->adev;

	if (!xcp->ualink.sysfs)
		return;

	/* group is only populated for valid partitions */
	if (xcp->valid)
		sysfs_remove_group(&xcp->ualink.kobj, &ualink_xcp_info_group);
	kobject_put(&adev->ualink.info->kobj);
	kobject_put(&xcp->ualink.kobj);
	xcp->ualink.sysfs = false;
}

static void amdgpu_ualink_xcp_sysfs_init(struct amdgpu_device *adev)
{
	struct amdgpu_xcp *xcp;
	int i;

	if (!adev->xcp_mgr)
		return;

	for (i = 0; i < MAX_XCP; i++) {
		xcp = &adev->xcp_mgr->xcp[i];
		if (!xcp->ddev || amdgpu_xcp_is_primary(xcp))
			continue;
		amdgpu_ualink_xcp_sysfs_add(xcp);
	}
}

static void amdgpu_ualink_xcp_sysfs_fini(struct amdgpu_device *adev)
{
	struct amdgpu_xcp *xcp;
	int i;

	if (!adev->xcp_mgr)
		return;

	for (i = 0; i < MAX_XCP; i++) {
		xcp = &adev->xcp_mgr->xcp[i];
		amdgpu_ualink_xcp_sysfs_remove(xcp);
	}
}

static int amdgpu_ualink_npa_alloc_va(struct amdgpu_device *adev,
			       struct drm_mm_node *mm_node,
			       u64 va, u64 range_start,
			       u64 range_end, int size)
{
	struct amdgpu_ualink_npa_mm *npa_mm = &adev->ualink.npa_mm;
	u64 alignment = 0;
	int ret;

	mutex_lock(&npa_mm->mm_lock);
	if (va) {
		mm_node->start = va;
		mm_node->size = size;
		ret = drm_mm_reserve_node(&npa_mm->mm, mm_node);
	} else {
		if (!range_start && !range_end) {
			range_start = npa_mm->va_start;
			range_end = npa_mm->va_start + npa_mm->va_size;
		}

		/* If size is greater than 2MB (or 512 pages),
		 * align it to 2MB (or 512 pages) granularity.
		 */
		if (size >= 0x200)
			alignment = 0x200;

		ret = drm_mm_insert_node_in_range(&npa_mm->mm, mm_node, size,
						  alignment, 0, range_start,
						  range_end, 0);
	}
	mutex_unlock(&npa_mm->mm_lock);

	if (ret)
		dev_err(adev->dev, "Failed to allocate NPA address\n");

	return ret;
}

static void amdgpu_ualink_npa_free_va(struct amdgpu_device *adev,
			       struct drm_mm_node *mm_node)
{
	struct amdgpu_ualink_npa_mm *npa_mm = &adev->ualink.npa_mm;

	mutex_lock(&npa_mm->mm_lock);
	drm_mm_remove_node(mm_node);
	mutex_unlock(&npa_mm->mm_lock);
}

static void amdgpu_ualink_npa_mm_init(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_npa_mm *npa_mm = &adev->ualink.npa_mm;
	u32 addr_mode = adev->ualink.info->vpod.addr_mode;
	u64 va_size;

	mutex_init(&npa_mm->mm_lock);

	if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT) {
		/* 51 bit address space in Source-Identification mode.
		 * 39 bits in page granularity.
		 */
		npa_mm->va_start = 0;
		va_size = GENMASK_ULL(38, 0);
	} else {
		/* 41 bit address space in Source-Alias mode.
		 * 29 bits in page granularity.
		 */
		/* First 8MB is reserved for Metadata NPAs */
		npa_mm->va_start = 0x800;
		va_size = GENMASK_ULL(28, 0);
	}

	npa_mm->va_size = va_size - npa_mm->va_start;
	drm_mm_init(&npa_mm->mm, npa_mm->va_start, npa_mm->va_size);
}

static void amdgpu_ualink_npa_mm_fini(struct amdgpu_device *adev)
{
	mutex_destroy(&adev->ualink.npa_mm.mm_lock);
	drm_mm_takedown(&adev->ualink.npa_mm.mm);
}

/* The caller of this function is expected to hold the XA lock when calling
 * this function.
 */
static void amdgpu_generate_ualink_handle(struct amdgpu_device *adev,
				   struct amdgpu_ualink_handle *handle)
{
	bool unique = false;

	do {
		handle->handle_lo = get_random_u64();
		/* Replace bottom 10 bits in handle_lo with accId */
		handle->handle_lo &= ~AMDGPU_UALINK_HANDLE_ACCID_MASK;
		handle->handle_lo |= adev->ualink.info->ppod.accel_id;

		/* Don't generate/store a Handle with value 0. */
		if (!handle->handle_lo)
			continue;

		/* Find if the handle already exists in the exporter xarray.
		 * If it already exists, then regenerate the handle since we
		 * want the handle to be unique.
		 */
		unique = !xa_load(&adev->ualink.exp_xa, handle->handle_lo);
	} while (!unique);

	handle->handle_hi = get_random_u64();
	dev_dbg(adev->dev, "GENERATE-HANDLE: generated handle: %llx:%llx\n",
		handle->handle_hi, handle->handle_lo);
}

static void amdgpu_ualink_cleanup_exp_xa_node(struct kref *ref)
{
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	struct amdgpu_device *adev;

	exp_xa_node = container_of(ref, struct amdgpu_ualink_exp_xa_node,
				   refcount);
	adev = amdgpu_ttm_adev(exp_xa_node->bo->tbo.bdev);
	queue_work(adev->ualink.npa_wq, &exp_xa_node->cleanup_work);
}

static void amdgpu_ualink_cleanup_imp_xa_node(struct kref *ref)
{
	struct amdgpu_ualink_imp_xa_node *imp_xa_node;
	struct amdgpu_device *adev;

	imp_xa_node = container_of(ref, struct amdgpu_ualink_imp_xa_node,
				   refcount);
	adev = imp_xa_node->adev;

	dev_dbg(adev->dev, "IMP-CLEANUP: Remove entry from XA for handle:%llx:%llx\n",
		imp_xa_node->handle.handle_hi, imp_xa_node->handle.handle_lo);

	/* Remove node from the Xarray */
	if (!xa_erase(&adev->ualink.imp_xa, imp_xa_node->handle.handle_lo))
		dev_err(adev->dev, "IMP-CLEANUP: Failed to find entry in XA for handle:%llx:%llx\n",
			imp_xa_node->handle.handle_hi, imp_xa_node->handle.handle_lo);

	if (imp_xa_node->npa_addr && imp_xa_node->size && imp_xa_node->dmabuf) {
		dev_dbg(adev->dev,
			"IMP-CLEANUP: dmabuf free for NPA:%llx handle:%llx:%llx, fc:%lu\n",
			imp_xa_node->npa_addr, imp_xa_node->handle.handle_hi,
			imp_xa_node->handle.handle_lo,
			file_count(imp_xa_node->dmabuf->file));
		dma_buf_put(imp_xa_node->dmabuf);
		drm_gem_handle_delete(adev->ualink.client.file,
				      imp_xa_node->gem_handle);

	}

	dev_dbg(adev->dev, "IMP-CLEANUP: Freeing XA entry for handle:%llx:%llx\n",
		imp_xa_node->handle.handle_hi, imp_xa_node->handle.handle_lo);

	kfree(imp_xa_node);
}

static int amdgpu_ualink_exp_xa_entry_get(struct amdgpu_ualink_exp_xa_node *exp_xa_node)
{
	return kref_get_unless_zero(&exp_xa_node->refcount);
}

static void amdgpu_ualink_exp_xa_entry_put(struct amdgpu_ualink_exp_xa_node *exp_xa_node)
{
	kref_put(&exp_xa_node->refcount, amdgpu_ualink_cleanup_exp_xa_node);
}

static int amdgpu_ualink_imp_xa_entry_get(struct amdgpu_ualink_imp_xa_node *imp_xa_node)
{
	return kref_get_unless_zero(&imp_xa_node->refcount);
}

static void amdgpu_ualink_imp_xa_entry_put(struct amdgpu_ualink_imp_xa_node *imp_xa_node)
{
	kref_put(&imp_xa_node->refcount, amdgpu_ualink_cleanup_imp_xa_node);
}

static int amdgpu_ualink_send_npa_release_msg(struct amdgpu_device *adev,
				    u32 remote_acc_id,
				    struct amdgpu_ualink_handle handle)
{
	u32 dw0, dw1, dw2, dw3;

	dw0 = lower_32_bits(handle.handle_lo);
	dw0 &= ~AMDGPU_UALINK_MESSAGE_HEADER_MASK;
	dw0 |= AMDGPU_UALINK_NPA_RELEASE_MSG;

	dw1 = upper_32_bits(handle.handle_lo);
	dw2 = lower_32_bits(handle.handle_hi);
	dw3 = upper_32_bits(handle.handle_hi);

	dev_dbg(adev->dev, "SEND NPA-RELEASE: remote_acc_id %u handle %llx:%llx dw[0-3] 0x%x 0x%x 0x%x 0x%x\n",
		remote_acc_id, handle.handle_hi, handle.handle_lo, dw0, dw1, dw2, dw3);

	return amdgpu_ualink_remote_interrupt(adev, remote_acc_id, dw0, dw1,
					      dw2, dw3);
}

static int amdgpu_ualink_send_npa_revoke_msg(struct amdgpu_device *adev,
					u32 remote_acc_id,
					struct amdgpu_ualink_handle handle)
{
	u32 dw0, dw1, dw2, dw3;

	dw0 = lower_32_bits(handle.handle_lo);
	dw0 &= ~AMDGPU_UALINK_MESSAGE_HEADER_MASK;
	dw0 |= AMDGPU_UALINK_NPA_REVOKE_MSG;

	dw1 = upper_32_bits(handle.handle_lo);
	dw2 = lower_32_bits(handle.handle_hi);
	dw3 = upper_32_bits(handle.handle_hi);

	dev_dbg(adev->dev, "SEND NPA-REVOKE: remote_acc_id %u handle %llx:%llx dw[0-3] 0x%x 0x%x 0x%x 0x%x\n",
		remote_acc_id, handle.handle_hi, handle.handle_lo, dw0, dw1, dw2, dw3);

	return amdgpu_ualink_remote_interrupt(adev, remote_acc_id, dw0, dw1,
					      dw2, dw3);
}

static int amdgpu_ualink_send_npa_fail_msg(struct amdgpu_device *adev,
				    u32 remote_acc_id,
				    struct amdgpu_ualink_handle handle,
				    u32 fail_reason)
{
	u32 dw0, dw1, dw2, dw3;

	dw0 = lower_32_bits(handle.handle_lo);
	dw0 &= ~AMDGPU_UALINK_MESSAGE_HEADER_MASK;
	dw0 |= AMDGPU_UALINK_NPA_FAIL_MSG;

	dw1 = upper_32_bits(handle.handle_lo);
	dw2 = fail_reason & 0xFF;
	dw3 = 0;

	dev_dbg(adev->dev, "SEND NPA-FAIL: remote_acc_id %u handle 0x%llx:%llx dw[0-3] 0x%x 0x%x 0x%x 0x%x\n",
		remote_acc_id, handle.handle_hi, handle.handle_lo, dw0, dw1, dw2, dw3);

	return amdgpu_ualink_remote_interrupt(adev, remote_acc_id, dw0, dw1,
					      dw2, dw3);
}

static int amdgpu_ualink_send_npa_rsp_msg(struct amdgpu_device *adev,
					  u32 remote_acc_id,
					  struct amdgpu_ualink_handle handle,
					  u32 npa_addr, u32 size)
{
	u32 dw0, dw1, dw2, dw3;

	dw0 = lower_32_bits(handle.handle_lo);
	dw0 &= ~AMDGPU_UALINK_MESSAGE_HEADER_MASK;
	dw0 |= AMDGPU_UALINK_NPA_RSP_MSG;

	dw1 = upper_32_bits(handle.handle_lo);
	dw2 = size;
	dw3 = npa_addr;

	dev_dbg(adev->dev, "SEND NPA-RSP: remote_acc_id %u handle %llx:%llx dw[0-3] 0x%x 0x%x 0x%x 0x%x\n",
		remote_acc_id, handle.handle_hi, handle.handle_lo, dw0, dw1, dw2, dw3);

	return amdgpu_ualink_remote_interrupt(adev, remote_acc_id, dw0, dw1,
					      dw2, dw3);
}

static int amdgpu_ualink_send_npa_req_msg(struct amdgpu_device *adev,
					  u32 remote_acc_id,
					  struct amdgpu_ualink_handle handle)
{
	u32 dw0, dw1, dw2, dw3;

	dw0 =  lower_32_bits(handle.handle_lo);
	dw0 &= ~AMDGPU_UALINK_MESSAGE_HEADER_MASK;
	dw0 |= AMDGPU_UALINK_NPA_REQ_MSG;

	dw1 = upper_32_bits(handle.handle_lo);
	dw2 = lower_32_bits(handle.handle_hi);
	dw3 = upper_32_bits(handle.handle_hi);

	dev_dbg(adev->dev, "SEND NPA-REQ: remote_acc_id %u handle 0x%llx:%llx dw[0-3] 0x%x 0x%x 0x%x 0x%x\n",
		remote_acc_id, handle.handle_hi, handle.handle_lo, dw0, dw1, dw2, dw3);

	return amdgpu_ualink_remote_interrupt(adev, remote_acc_id, dw0, dw1,
					      dw2, dw3);
}

static u64 amdgpu_ualink_get_export_pte_flags(struct amdgpu_device *adev,
				       struct amdgpu_bo *bo,
				       u64 mapping_flags)
{
	u64 pte_flags = adev->gmc.init_pte_flags;

	pte_flags |= (AMDGPU_PTE_VALID | AMDGPU_PTE_READABLE |
		      AMDGPU_PTE_WRITEABLE);
	mapping_flags |= AMDGPU_VM_MTYPE_DEFAULT;

	amdgpu_gmc_get_vm_pte(adev, &adev->ualink.npa_vm, bo, mapping_flags,
			      &pte_flags);

	return pte_flags;
}

static int amdgpu_ualink_unmap_npa_addr(struct amdgpu_device *adev,
					struct amdgpu_bo *bo,
					u64 npa_addr, u64 size)
{
	struct amdgpu_vm *vm = &adev->ualink.npa_vm;
	uint64_t pte_value = adev->gmc.noretry_flags;
	struct amdgpu_bo *bos[] = { bo };
	struct dma_fence *fence = NULL;
	struct drm_exec exec;
	int r;

	r = amdgpu_ualink_reserve_npa_vm_and_bos(adev, bos, ARRAY_SIZE(bos), &exec, false);
	if (unlikely(r)) {
		dev_err(adev->dev, "Failed to reserve VM and BO in unmap_npa_addr\n");
		return r;
	}

	r = amdgpu_vm_update_range(adev, vm, false, false, true,
				false, NULL, npa_addr, npa_addr + size - 1,
				pte_value, 0, 0, NULL, NULL, &vm->last_update);
	if (r) {
		dev_err(adev->dev,
			"Failed to unmap NPA addr (%llx) from NPA VM\n", npa_addr);
		goto out;
	}

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r) {
		dev_err(adev->dev,
			"Failed %d to update page directories during unmapping NPA: 0x%llx\n",
			r, npa_addr);
		goto out;
	}

	fence = dma_fence_get(vm->last_update);
	if (fence) {
		r = dma_fence_wait(fence, false);
		dma_fence_put(fence);
		fence = NULL;
		if (r) {
			dev_dbg(adev->dev,
				"UNMAP-NPA: dma fence wait failed, error: %d\n", r);
			goto out;
		}
	}

	amdgpu_ualink_flush_tlb(adev, TLB_FLUSH_HEAVYWEIGHT);
out:
	amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);

	return r;
}

static int amdgpu_ualink_map_npa_addr(struct amdgpu_device *adev, u64 npa_addr,
				u64 size, struct amdgpu_bo *bo, u64 offset,
				u64 pte_flags)
{
	struct amdgpu_vm *vm = &adev->ualink.npa_vm;
	struct amdgpu_bo *bos[] = { bo };
	struct dma_fence *fence = NULL;
	struct drm_exec exec;
	int r;

	r = amdgpu_ualink_reserve_npa_vm_and_bos(adev, bos, ARRAY_SIZE(bos), &exec, false);
	if (unlikely(r)) {
		dev_err(adev->dev, "Failed to reserve VM and BO in map_npa_addr\n");
		return r;
	}

	r = amdgpu_vm_update_range(adev, vm, false, false, true,
				false, NULL, npa_addr, npa_addr + size - 1,
				pte_flags, offset, adev->vm_manager.vram_base_offset,
				bo->tbo.resource, NULL, &vm->last_update);
	if (r) {
		dev_warn(adev->dev,
			"Failed to map NPA addr (%llx) into NPA VM\n", npa_addr);
		amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);
		goto out;
	}

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r) {
		dev_err(adev->dev,
			"failed %d to update page directories for NPA: 0x%llx\n",
			r, npa_addr);
		amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);
		goto unmap_npa;
	}

	fence = dma_fence_get(vm->last_update);
	if (fence) {
		r = dma_fence_wait(fence, false);
		dma_fence_put(fence);
		fence = NULL;
		if (r) {
			pr_debug("failed %d to dma fence wait\n", r);
			amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);
			goto unmap_npa;
		}
	}

	amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);

	/* TLB flush may be needed after updated page directories */
	amdgpu_ualink_flush_tlb(adev, TLB_FLUSH_HEAVYWEIGHT);

	return 0;

unmap_npa:
	amdgpu_ualink_unmap_npa_addr(adev, bo, npa_addr, size);
out:
	return r;
}

static int amdgpu_ualink_send_hello_ack_msg(struct amdgpu_device *adev,
					    u32 remote_acc_id)
{
	dev_dbg(adev->dev, "SEND HELLO-ACK: HELLO-ACK message to remote AccId:%u\n",
		remote_acc_id);
	return amdgpu_ualink_remote_interrupt(adev, remote_acc_id,
		AMDGPU_UALINK_HELLO_ACK_MSG,
		0, 0, 0);
}

static int amdgpu_ualink_send_hello_msg(struct amdgpu_device *adev,
					u32 remote_acc_id)
{
	u32 dw0;

	dw0 = AMDGPU_UALINK_HELLO_MSG;
	dw0 |= (remote_acc_id << AMDGPU_UALINK_HELLO_MSG_RECV_ACCID_SHIFT);
	dw0 |= (adev->ualink.info->ppod.accel_id <<
		AMDGPU_UALINK_HELLO_MSG_SENDER_ACCID_SHIFT);

	dev_dbg(adev->dev, "SEND HELLO: HELLO message to remote AccId:%u\n",
		remote_acc_id);
	return amdgpu_ualink_remote_interrupt(adev, remote_acc_id, dw0, 0,
					      0, 0);
}

static u32 amdgpu_ualink_check_conn_ready(struct amdgpu_device *adev,
					  u32 remote_acc_id, u32 gen_count)
{
	struct amdgpu_ualink_connection *conn_state;
	u32 current_gen_count = 0;

	conn_state = &adev->ualink.conn_state[remote_acc_id];

	/* Check if the connection is established. */
	mutex_lock(&conn_state->lock);
	if ((conn_state->state == AMDGPU_UALINK_CONN_ESTABLISHED) &&
	    (!gen_count || conn_state->generation_count == gen_count))
		current_gen_count = conn_state->generation_count;
	mutex_unlock(&conn_state->lock);

	return current_gen_count;
}

static void amdgpu_ualink_process_hello_ack_msg(struct amdgpu_device *adev,
					       u32 sender_acc_id)
{
	struct amdgpu_ualink_connection *conn_state;

	if (sender_acc_id >= AMDGPU_UALINK_ACCEL_MAX) {
		dev_err(adev->dev,
			"HELLO-ACK: sender AccId out of range:%u\n",
			sender_acc_id);
		return;
	}

	conn_state = &adev->ualink.conn_state[sender_acc_id];

	/* If we are in IN_PROGRESS state, then transition the connection
	 * state to established and signal that the HELLO ACK is received.
	 * Otherwise, ignore the HELLO ACK.
	 */
	mutex_lock(&conn_state->lock);

	if (conn_state->state == AMDGPU_UALINK_CONN_IN_PROGRESS) {
		conn_state->state = AMDGPU_UALINK_CONN_ESTABLISHED;
		conn_state->generation_count++;
		complete(&conn_state->hello_done);
	} else {
		dev_dbg(adev->dev,
			"HELLO-ACK: already connected, ignoring from AccId:%u\n",
			sender_acc_id);
	}
	mutex_unlock(&conn_state->lock);
}

static void amdgpu_ualink_process_hello_msg(struct amdgpu_device *adev,
					   u32 receiver_acc_id,
					   u32 sender_acc_id,
					   u32 src_acc_id)
{
	struct amdgpu_ualink_connection *conn_state;
	u32 generation_count;
	int r;

	if (receiver_acc_id != adev->ualink.info->ppod.accel_id) {
		dev_err(adev->dev,
			"HELLO: receiver AccId mismatch got:%u self:%u\n",
			receiver_acc_id, adev->ualink.info->ppod.accel_id);
		return;
	}

	/* src_acc_id is the AccId received in IH cookie. Confirm it
	 * matches with the sender AccId.
	 */
	if (sender_acc_id != src_acc_id) {
		dev_err(adev->dev,
			"HELLO: sender AccId mismatch sender:%u IH cookie:%u\n",
			sender_acc_id, src_acc_id);
		return;
	}

	if (sender_acc_id >= AMDGPU_UALINK_ACCEL_MAX) {
		dev_err(adev->dev,
			"HELLO: sender AccId out of range:%u\n",
			sender_acc_id);
		return;
	}

	conn_state = &adev->ualink.conn_state[sender_acc_id];

	/* Check if connection is already established. If yes, then receiving HELLO msg
	 * triggers a reset handling scenario.
	 * If the connection is not ready, and we receive a HELLO msg, then
	 * transition the state to PENDING.
	 * Otherwise, leave it IN_PROGRESS.
	 */
	mutex_lock(&conn_state->lock);
	if (conn_state->state != AMDGPU_UALINK_CONN_ESTABLISHED) {
		if (conn_state->state == AMDGPU_UALINK_CONN_NOT_READY)
			conn_state->state = AMDGPU_UALINK_CONN_PENDING;
		/* otherwise, leave it IN_PROGRESS to signal the completion below */
		mutex_unlock(&conn_state->lock);
	} else {
		/* Leave state ESTABLISHED so handle_connection_reset()
		 * performs the transition and imp/exp XA cleanup.
		 */
		generation_count = conn_state->generation_count;
		mutex_unlock(&conn_state->lock);

		amdgpu_ualink_handle_connection_reset(adev, sender_acc_id,
						AMDGPU_UALINK_CONN_PENDING,
						generation_count);
	}

	/* Peer may have rebooted. */
	r = amdgpu_ualink_ring_resync_ptrs(adev, sender_acc_id);
	if (r < 0) {
		dev_err(adev->dev,
			"HELLO: resync sender AccId %u ptrs failed %d\n",
			sender_acc_id, r);
		goto out_set_state;
	}
	if (r)
		amdgpu_ualink_reset_peer_rings(adev, sender_acc_id);

	r = amdgpu_ualink_send_hello_ack_msg(adev, sender_acc_id);
	if (r)
		dev_err(adev->dev, "HELLO-ACK: send failed to remote AccId:%u\n",
			sender_acc_id);

out_set_state:
	mutex_lock(&conn_state->lock);
	if (r) {
		conn_state->state = AMDGPU_UALINK_CONN_NOT_READY;
	} else {
		/* If we are in IN_PROGRESS state and we receivied the HELLO message,
		 * upon receiving the HELLO message, transition the state to ESTABLISHED
		 * and signal the completion.
		 */
		if (conn_state->state == AMDGPU_UALINK_CONN_IN_PROGRESS)
			complete(&conn_state->hello_done);
		conn_state->state = AMDGPU_UALINK_CONN_ESTABLISHED;
		conn_state->generation_count++;
	}
	mutex_unlock(&conn_state->lock);
}

/* True if nothing is exported to, or imported from, this remote accelerator. */
static bool amdgpu_ualink_peer_idle(struct amdgpu_device *adev, u32 remote_accel_id)
{
	/* Unlocked: a racing import at worst costs one redundant ring resync. */
	return list_empty(&adev->ualink.imp_handles_list[remote_accel_id]) &&
	       list_empty(&adev->ualink.exp_handles_list[remote_accel_id]);
}

static int amdgpu_ualink_setup_connection(struct amdgpu_device *adev,
					  u32 remote_acc_id)
{
	struct amdgpu_ualink_connection *conn_state;
	int r;

	conn_state = &adev->ualink.conn_state[remote_acc_id];

	/* Connection state management goes through different states.
	 * The states are:
	 * - NOT_READY: The connection is not ready.
	 * - IN_PROGRESS: GPU sent HELLO message and is waiting for the HELLO_ACK.
	 * - PENDING: GPU received HELLO message and is in the process of sending
	 *            the HELLO_ACK.
	 * - ESTABLISHED: The connection is established.
	 */

	/* Grab the lock and check if connection establishment was
	 * already done by another thread.
	 */
	mutex_lock(&conn_state->lock);
	if (conn_state->state == AMDGPU_UALINK_CONN_IN_PROGRESS ||
	    conn_state->state == AMDGPU_UALINK_CONN_PENDING) {
		r = -EAGAIN;
		goto out;
	}

	if (conn_state->state == AMDGPU_UALINK_CONN_ESTABLISHED &&
	    !amdgpu_ualink_peer_idle(adev, remote_acc_id)) {
		r = 0;
		goto out;
	}

	/* Peer is idle or not connected, it may have rebooted. Skip the
	 * handshake if the rings are still in sync, it would reset the remote.
	 */
	r = amdgpu_ualink_ring_resync_ptrs(adev, remote_acc_id);
	if (r < 0)
		goto out;
	if (!r && conn_state->state == AMDGPU_UALINK_CONN_ESTABLISHED)
		goto out;

	amdgpu_ualink_reset_peer_rings(adev, remote_acc_id);

	conn_state->state = AMDGPU_UALINK_CONN_IN_PROGRESS;

	mutex_unlock(&conn_state->lock);

	/* Send HELLO message */
	r = amdgpu_ualink_send_hello_msg(adev, remote_acc_id);
	if (r) {
		dev_warn(adev->dev,
			"HELLO: Send failed to remote AccId:%u\n",
			remote_acc_id);
		goto reset_state;
	}

	/* Wait for the HELLO_ACK to come back */
	/* complete(conn_state->hello_done) should be called from the IRQ
	 * handler when the HELLO_ACK is received.
	 */
	r = wait_for_completion_interruptible_timeout(&conn_state->hello_done,
				msecs_to_jiffies(AMDGPU_UALINK_RESP_TIMEOUT));
	if (r == -ERESTARTSYS) {
		dev_err_ratelimited(adev->dev,
			"HELLO-ACK: interrupted by signal\n");
		goto reset_state;
	} else if (r == 0) {
		dev_warn(adev->dev,
			"HELLO-ACK: Timeout from remote AccId:%u\n",
			remote_acc_id);
		r = -ETIMEDOUT;
		goto reset_state;
	}

	return 0;

reset_state:
	mutex_lock(&conn_state->lock);
	conn_state->state = AMDGPU_UALINK_CONN_NOT_READY;
out:
	mutex_unlock(&conn_state->lock);

	return r;
}

/* Invalidate an importer node's mappings and drop its last ref (frees the
 * node, dma-buf and GEM handle). Caller must have unlinked it from the
 * per-remote list and must not hold the imp_xa lock.
 */
static void amdgpu_ualink_release_imp_xa_node(struct amdgpu_device *adev,
				struct amdgpu_ualink_imp_xa_node *imp_xa_node)
{
	struct amdgpu_bo *bo;

	dev_dbg(adev->dev,
		"IMP-CLEANUP: handle:%llx:%llx npa:%llx size:%llx\n",
		imp_xa_node->handle.handle_hi, imp_xa_node->handle.handle_lo,
		imp_xa_node->npa_addr, imp_xa_node->size);

	if (imp_xa_node->dmabuf) {
		bo = gem_to_amdgpu_bo(imp_xa_node->dmabuf->priv);
		amdgpu_ualink_invalidate_import_mappings(bo);
	}

	amdgpu_ualink_imp_xa_entry_put(imp_xa_node);
}

static void amdgpu_ualink_cleanup_imp_xa_entries(struct amdgpu_device *adev,
						 u32 remote_acc_id)
{
	struct amdgpu_ualink_imp_xa_node *imp_xa_node;
	struct list_head *imp_handles_list;

	dev_dbg(adev->dev,
		"IMP-RESET: Cleaning up all XA entries for remote:%u\n",
		remote_acc_id);

	imp_handles_list = &adev->ualink.imp_handles_list[remote_acc_id];

	xa_lock(&adev->ualink.imp_xa);
	while (!list_empty(imp_handles_list)) {
		imp_xa_node = list_first_entry(imp_handles_list,
					struct amdgpu_ualink_imp_xa_node, list);
		list_del_init(&imp_xa_node->list);
		WRITE_ONCE(imp_xa_node->node_state, AMDGPU_UALINK_NODE_TEARDOWN);
		xa_unlock(&adev->ualink.imp_xa);

		amdgpu_ualink_release_imp_xa_node(adev, imp_xa_node);

		xa_lock(&adev->ualink.imp_xa);
	}
	xa_unlock(&adev->ualink.imp_xa);
}

static void amdgpu_ualink_cleanup_exp_xa_entries(struct amdgpu_device *adev,
						 u32 remote_acc_id)
{
	struct amdgpu_ualink_importer_entry *importer_entry;
	u32 addr_mode = adev->ualink.info->vpod.addr_mode;
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	struct list_head *exp_handles_list;
	struct drm_mm_node *mm_node;
	u64 npa_addr, size;

	/* Get the list head for the list containing all the handles
	 * exported to this remote GPU.
	 */
	exp_handles_list = &adev->ualink.exp_handles_list[remote_acc_id];

	dev_dbg(adev->dev,
		"EXP-RESET: Cleaning up all XA entries for remote:%u\n",
		remote_acc_id);

	xa_lock(&adev->ualink.exp_xa);
	while (!list_empty(exp_handles_list)) {
		importer_entry = list_first_entry(exp_handles_list,
					struct amdgpu_ualink_importer_entry, list);
		list_del_init(&importer_entry->list);

		exp_xa_node = importer_entry->parent;
		if (!amdgpu_ualink_exp_xa_entry_get(exp_xa_node))
			continue;

		xa_unlock(&adev->ualink.exp_xa);
		/* Clear the bit corresponding to this remote GPU in
		 * the importer bitmap.
		 */
		if (!test_and_clear_bit(remote_acc_id,
					exp_xa_node->importers_bitmap)) {
			amdgpu_ualink_exp_xa_entry_put(exp_xa_node);
			xa_lock(&adev->ualink.exp_xa);
			continue;
		}

		mutex_lock(&exp_xa_node->node_lock);
		size = amdgpu_bo_ngpu_pages(exp_xa_node->bo);
		/* Unpin the BO */
		if (likely(!amdgpu_bo_reserve(exp_xa_node->bo, true))) {
			amdgpu_bo_unpin(exp_xa_node->bo);
			amdgpu_bo_unreserve(exp_xa_node->bo);
		} else {
			dev_warn(adev->dev,
				"EXP-RESET: BO reserve to unpin failed handle:%llx:%llx\n",
				exp_xa_node->handle.handle_hi, exp_xa_node->handle.handle_lo);
		}

		dev_dbg(adev->dev,
			"EXP-RESET: handle:%llx:%llx pin_count:%d, importers:%d\n",
			exp_xa_node->handle.handle_hi, exp_xa_node->handle.handle_lo,
			exp_xa_node->bo->tbo.pin_count,
			bitmap_weight(exp_xa_node->importers_bitmap,
			AMDGPU_UALINK_ACCEL_MAX));

		WARN_ON(exp_xa_node->bo->tbo.pin_count <
			bitmap_weight(exp_xa_node->importers_bitmap,
			AMDGPU_UALINK_ACCEL_MAX));

		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT) {
			npa_addr = importer_entry->npa_addr;
			mm_node = importer_entry->mm_node;
			mutex_unlock(&exp_xa_node->node_lock);

			dev_dbg(adev->dev,
				"EXP-RESET: Unmap NPA:%llx size: %llx remote:%u handle:%llx:%llx\n",
				npa_addr, size, remote_acc_id,
				exp_xa_node->handle.handle_hi,
				exp_xa_node->handle.handle_lo);
			amdgpu_ualink_unmap_npa_addr(adev, exp_xa_node->bo,
						     npa_addr, size);
			amdgpu_ualink_npa_free_va(adev, mm_node);
			kfree(mm_node);

			/* Reset the NPA addr and mm_node */
			mutex_lock(&exp_xa_node->node_lock);
			importer_entry->npa_addr = 0;
			importer_entry->mm_node = NULL;
		} else if (bitmap_empty(exp_xa_node->importers_bitmap,
				AMDGPU_UALINK_ACCEL_MAX)) {
			/* In Source-Aliasing mode, if there are no importers
			 * for this handle, then we can unmap and free the
			 * NPA address.
			 */
			mm_node = exp_xa_node->importer_entries[0].mm_node;
			npa_addr = exp_xa_node->importer_entries[0].npa_addr;
			mutex_unlock(&exp_xa_node->node_lock);

			dev_dbg(adev->dev,
				"EXP-RESET: Unmap NPA:%llx size: %llx handle:%llx:%llx\n",
				npa_addr, size, exp_xa_node->handle.handle_hi,
				exp_xa_node->handle.handle_lo);
			amdgpu_ualink_unmap_npa_addr(adev, exp_xa_node->bo,
						     npa_addr, size);
			amdgpu_ualink_npa_free_va(adev, mm_node);
			kfree(mm_node);
			mutex_lock(&exp_xa_node->node_lock);
			exp_xa_node->importer_entries[0].npa_addr = 0;
			exp_xa_node->importer_entries[0].mm_node = NULL;
		}

		mutex_unlock(&exp_xa_node->node_lock);
		xa_lock(&adev->ualink.exp_xa);
		amdgpu_ualink_exp_xa_entry_put(exp_xa_node);
	}
	xa_unlock(&adev->ualink.exp_xa);
}

static void amdgpu_ualink_handle_connection_reset(struct amdgpu_device *adev,
						u32 remote_acc_id, u32 state,
						u32 generation_count)
{
	struct amdgpu_ualink_connection *conn_state;

	conn_state = &adev->ualink.conn_state[remote_acc_id];

	mutex_lock(&conn_state->lock);
	if ((conn_state->state == AMDGPU_UALINK_CONN_ESTABLISHED) &&
	     (conn_state->generation_count == generation_count)) {
		conn_state->state = state;
		mutex_unlock(&conn_state->lock);

		amdgpu_ualink_cleanup_imp_xa_entries(adev, remote_acc_id);
		amdgpu_ualink_cleanup_exp_xa_entries(adev, remote_acc_id);
	} else {
		mutex_unlock(&conn_state->lock);
	}
}

/* Set PTE.X = 1 for all importer entries to retry RPCs. */
static void amdgpu_ualink_force_retry_rpcs(struct amdgpu_device *adev,
				struct amdgpu_ualink_exp_xa_node *exp_xa_node)
{
	u32 addr_mode = adev->ualink.info->vpod.addr_mode;
	struct amdgpu_ualink_importer_entry *imp_entry;
	struct amdgpu_vm *vm = &adev->ualink.npa_vm;
	u64 pte_flags, npa_addr, size;
	struct dma_fence *fence = NULL;
	struct amdgpu_bo *bo;
	struct drm_exec exec;
	u32 remote_acc_id;
	int r;

	bo = exp_xa_node->bo;
	size = amdgpu_bo_ngpu_pages(bo);

	r = amdgpu_ualink_reserve_npa_vm_and_bos(adev, &bo, 1, &exec, false);
	if (unlikely(r)) {
		dev_err(adev->dev, "Failed to reserve VM and BO in force_retry_rpcs\n");
		return;
	}

	for_each_set_bit(remote_acc_id, exp_xa_node->importers_bitmap,
			 AMDGPU_UALINK_ACCEL_MAX) {
		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
			imp_entry = &exp_xa_node->importer_entries[0];
		else
			imp_entry = &exp_xa_node->importer_entries[remote_acc_id];
		npa_addr = imp_entry->npa_addr;

		/* If the connection state changed while we are freeing
		 * the BO, then ignore this importer. We will unmap this
		 * address eventually in amdgpu_ualink_unmap_all_npa_addr()
		 * function. Also, we will clear the corresponding bit in
		 * the importer_bitmap in the same function.
		 */
		if (!amdgpu_ualink_check_conn_ready(adev, remote_acc_id,
					imp_entry->generation_count))
			continue;

		/* Set PTE.X = 1 */
		pte_flags = amdgpu_ualink_get_export_pte_flags(adev, bo,
					AMDGPU_VM_PAGE_EXECUTABLE);
		dev_dbg(adev->dev,
			"RETRY-RPC: setting PTE.X=1 for NPA:%llx remote:%u pte:0x%llx\n",
			npa_addr, remote_acc_id, pte_flags);

		r = amdgpu_vm_update_range(adev, vm, false, false, true,
					false, NULL, npa_addr, npa_addr + size - 1,
					pte_flags, 0, adev->vm_manager.vram_base_offset,
					bo->tbo.resource, NULL, &vm->last_update);

		if (r)
			dev_warn(adev->dev,
				"RETRY-RPC: PTE.X update failed for NPA:%llx remote:%u, r: %d\n",
				npa_addr, remote_acc_id, r);

		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
			break;
	}

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r) {
		dev_err(adev->dev,
			"Failed %d to update page directories during force retry rpcs\n",
			r);
		amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);
		return;
	}

	fence = dma_fence_get(vm->last_update);
	if (fence) {
		r = dma_fence_wait(fence, false);
		dma_fence_put(fence);
		fence = NULL;
		if (r)
			dev_dbg(adev->dev, "RETRY-RPC: dma fence wait failed err:%d\n", r);
	}

	amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);

	amdgpu_ualink_flush_tlb(adev, TLB_FLUSH_HEAVYWEIGHT);
}

static void amdgpu_ualink_send_tlb_shootdown(struct amdgpu_device *adev,
				struct amdgpu_ualink_exp_xa_node *exp_xa_node)
{
	u32 addr_mode = adev->ualink.info->vpod.addr_mode;
	u32 accel_id = adev->ualink.info->ppod.accel_id;
	struct amdgpu_ualink_importer_entry *imp_entry;
	u64 npa_addr, size;
	u32 remote_acc_id;
	int r;

	size = amdgpu_bo_ngpu_pages(exp_xa_node->bo);

	for_each_set_bit(remote_acc_id, exp_xa_node->importers_bitmap,
			 AMDGPU_UALINK_ACCEL_MAX) {
		/*
		 * Replace the GPU-id bits with local accel_id
		 */
		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
			imp_entry = &exp_xa_node->importer_entries[0];
		else
			imp_entry = &exp_xa_node->importer_entries[remote_acc_id];

		npa_addr = STRIP_NPA(imp_entry->npa_addr);
		npa_addr = GENERATE_NPA(npa_addr, accel_id);

		if (!amdgpu_ualink_check_conn_ready(adev, remote_acc_id,
					imp_entry->generation_count)) {
			clear_bit(remote_acc_id,
				  exp_xa_node->importers_bitmap);
			continue;
		}

		dev_dbg(adev->dev,
			"EXP-CLEANUP: Sending TLB-shootdown to remote:%u\n",
			remote_acc_id);
		r = amdgpu_ualink_remote_shootdown(adev, remote_acc_id,
					npa_addr, size,
					AMDGPU_UALINK_HEAVYWEIGHT_TLB_SHOOTDOWN);
		if (r) {
			dev_err(adev->dev,
				"EXP-CLEANUP: TLB shootdown send failed to remote:%u\n",
				remote_acc_id);

			/* Send failed: mark the connection NOT_READY so the
			 * rest of this cleanup (and other cleanups) skip the
			 * unreachable peer via amdgpu_ualink_check_conn_ready().
			 */
			amdgpu_ualink_handle_connection_reset(adev, remote_acc_id,
						AMDGPU_UALINK_CONN_NOT_READY,
						imp_entry->generation_count);
		}
	}
}

/* Unmap all NPA addresses associated with a BO (UALink handle). This function is used
 * only in Source Identification mode.
 */
static void amdgpu_ualink_unmap_all_npa_addr(struct amdgpu_device *adev,
				struct amdgpu_ualink_exp_xa_node *exp_xa_node)
{
	struct amdgpu_vm *vm = &adev->ualink.npa_vm;
	u32 addr_mode = adev->ualink.info->vpod.addr_mode;
	struct amdgpu_ualink_importer_entry *imp_entry;
	u64 pte_value = adev->gmc.noretry_flags;
	struct dma_fence *fence = NULL;
	struct drm_exec exec;
	u64 npa_addr, size;
	u32 remote_acc_id;
	int r;

	size = amdgpu_bo_ngpu_pages(exp_xa_node->bo);

	r = amdgpu_ualink_reserve_npa_vm_and_bos(adev, &exp_xa_node->bo, 1,
						 &exec, false);
	if (unlikely(r)) {
		dev_err(adev->dev, "Failed to reserve VM and BO in unmap_all_npa_addr\n");
		return;
	}

	for_each_set_bit(remote_acc_id, exp_xa_node->importers_bitmap,
			 AMDGPU_UALINK_ACCEL_MAX) {
		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
			imp_entry = &exp_xa_node->importer_entries[0];
		else
			imp_entry = &exp_xa_node->importer_entries[remote_acc_id];
		npa_addr = imp_entry->npa_addr;

		if (!amdgpu_ualink_check_conn_ready(adev, remote_acc_id,
					imp_entry->generation_count)) {
			clear_bit(remote_acc_id,
				  exp_xa_node->importers_bitmap);
			continue;
		}

		dev_dbg(adev->dev,
			"UNMAP-NPA: Unmapping NPA:%llx, handle:%llx:%llx remote:%u pte:0x%llx\n",
			npa_addr, exp_xa_node->handle.handle_hi, exp_xa_node->handle.handle_lo,
			remote_acc_id, pte_value);

		r = amdgpu_vm_update_range(adev, vm, false,
					   false, true, false, NULL, npa_addr,
					   npa_addr + size - 1, pte_value, 0,
					   0, NULL, NULL, &vm->last_update);

		if (r)
			dev_err(adev->dev,
				"UNMAP-NPA: Unmap failed NPA:%llx, handle:%llx:%llx remote:%u\n",
				npa_addr, exp_xa_node->handle.handle_hi,
				exp_xa_node->handle.handle_lo, remote_acc_id);

		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
			break;
	}

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r) {
		dev_err(adev->dev,
			"Failed %d to update page directories during all NPA addresses unmapping\n",
			r);
		amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);
		return;
	}

	fence = dma_fence_get(vm->last_update);
	if (fence) {
		r = dma_fence_wait(fence, false);
		dma_fence_put(fence);
		fence = NULL;
		if (r)
			dev_err(adev->dev,
				"UNMAP-NPA: dma fence wait failed\n");
	}

	amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);

	amdgpu_ualink_flush_tlb(adev, TLB_FLUSH_HEAVYWEIGHT);
}

static void amdgpu_ualink_free_all_npa_va(struct amdgpu_device *adev,
				struct amdgpu_ualink_exp_xa_node *exp_xa_node,
				unsigned long *importers_bitmap)
{
	u32 addr_mode = adev->ualink.info->vpod.addr_mode;
	struct drm_mm_node *mm_node;
	u32 remote_acc_id;

	for_each_set_bit(remote_acc_id, importers_bitmap, AMDGPU_UALINK_ACCEL_MAX) {
		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
			mm_node = exp_xa_node->importer_entries[0].mm_node;
		else
			mm_node = exp_xa_node->importer_entries[remote_acc_id].mm_node;

		if (!mm_node) {
			if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
				break;
			continue;
		}

		dev_dbg(adev->dev,
			"FREE-NPA: freeing NPA address:%llx, handle:%llx:%llx remote:%u\n",
			mm_node->start, exp_xa_node->handle.handle_hi,
			exp_xa_node->handle.handle_lo, remote_acc_id);

		amdgpu_ualink_npa_free_va(adev, mm_node);
		kfree(mm_node);

		if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
			break;
	}
}

static void amdgpu_ualink_exp_cleanup_worker(struct work_struct *work)
{
	DECLARE_BITMAP(orig_importers_bitmap, AMDGPU_UALINK_ACCEL_MAX);
	struct amdgpu_ualink_importer_entry *imp_entry;
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	struct amdgpu_ualink_handle handle;
	struct amdgpu_device *adev;
	struct amdgpu_bo *bo;
	u32 remote_acc_id;
	int r;

	exp_xa_node = container_of(work, struct amdgpu_ualink_exp_xa_node,
				   cleanup_work);
	bo = exp_xa_node->bo;
	adev = amdgpu_ttm_adev(bo->tbo.bdev);
	handle = exp_xa_node->handle;

	/* Revoking access to an exported memory follows the steps:
	 * 1. Set PTE.X = 1 to retry for RPCs.
	 * 2. Send Remote TLB Shootdowns to all importers.
	 * 3. Unmap the NPA address from NPA VM.
	 * 4. Drop the ref count for the BO.
	 * 5. Send NPA_REVOKE to all importers.
	 * 6. Wait for NPA_RELEASE from all importers.
	 * 7. Free the NPA address once all responses are received.
	 */
	/* If there are no importers for this BO/handle */
	if (bitmap_empty(exp_xa_node->importers_bitmap,
			 AMDGPU_UALINK_ACCEL_MAX)) {
		/* Release the dma_buf created at export time (used for the
		 * local-import shortcut). The has-importers path below drops
		 * it too; this branch must not skip it or the dma_buf and the
		 * BO it pins are leaked.
		 */
		dma_buf_put(exp_xa_node->dmabuf);
		/* Drop the BO reference so it can be freed. */
		amdgpu_bo_unref(&bo);
		exp_xa_node->bo = NULL;
		goto free_node;
	}

	dev_dbg(adev->dev,
		"EXP-CLEANUP: handle:%llx:%llx importers bitmap: %*pbl\n",
		handle.handle_hi, handle.handle_lo,
		AMDGPU_UALINK_ACCEL_MAX, exp_xa_node->importers_bitmap);

	bitmap_copy(orig_importers_bitmap, exp_xa_node->importers_bitmap,
		    AMDGPU_UALINK_ACCEL_MAX);

	/* Set PTE.X = 1 for NPA addresses from all importers*/
	amdgpu_ualink_force_retry_rpcs(adev, exp_xa_node);

	/* Send TLB-shootdown to all importer GPUs */
	amdgpu_ualink_send_tlb_shootdown(adev, exp_xa_node);

	/* Unmap all NPA addresses for this BO from NPA VM */
	amdgpu_ualink_unmap_all_npa_addr(adev, exp_xa_node);

	dev_dbg(adev->dev, "EXP-CLEANUP: handle:%llx:%llx Unpin BO, pin_count:%u, importers:%u\n",
		handle.handle_hi, handle.handle_lo, bo->tbo.pin_count,
		bitmap_weight(orig_importers_bitmap, AMDGPU_UALINK_ACCEL_MAX));
	WARN_ON(bo->tbo.pin_count < bitmap_weight(orig_importers_bitmap,
						AMDGPU_UALINK_ACCEL_MAX));

	/* Unpin the BO */
	if (likely(!amdgpu_bo_reserve(bo, true))) {
		bo->ualink_handle_lo = 0ULL;
		for_each_set_bit(remote_acc_id, orig_importers_bitmap,
				 AMDGPU_UALINK_ACCEL_MAX)
			amdgpu_bo_unpin(bo);
		amdgpu_bo_unreserve(bo);
	} else {
		dev_warn(adev->dev,
			"EXP-CLEANUP: BO reserve to unpin failed for handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
	}

	/* Free the DMABuf */
	dma_buf_put(exp_xa_node->dmabuf);
	/* Drop the reference to the BO so it can be freed. */
	amdgpu_bo_unref(&bo);
	exp_xa_node->bo = NULL;

	/* Seed npa_release_bitmap with all importers before any NPA-REVOKE, so
	 * the release IRQ only sees it empty once every responder has replied.
	 * Dead peers are dropped from it in the send loop below.
	 */
	bitmap_copy(exp_xa_node->npa_release_bitmap,
		    exp_xa_node->importers_bitmap, AMDGPU_UALINK_ACCEL_MAX);

	/* Send NPA-REVOKE to all importers which have imported this memory.
	 * On send failure clear the bit (no response will arrive) and mark the
	 * connection NOT_READY so other cleanups skip this unreachable peer.
	 */
	for_each_set_bit(remote_acc_id, exp_xa_node->importers_bitmap,
				 AMDGPU_UALINK_ACCEL_MAX) {
		imp_entry = &exp_xa_node->importer_entries[remote_acc_id];

		/* Re-check the connection right before the send: concurrent
		 * cleanup workers all seed the bitmap while the peer is still
		 * ESTABLISHED, so once its egress dies and the first worker
		 * marks it NOT_READY, this skips the rest instead of flooding
		 * the log with failed LSDMA sends to the same dead peer.
		 */
		if (!amdgpu_ualink_check_conn_ready(adev, remote_acc_id,
					imp_entry->generation_count)) {
			clear_bit(remote_acc_id,
				  exp_xa_node->npa_release_bitmap);
			continue;
		}

		dev_dbg(adev->dev,
			"EXP-CLEANUP: Sending NPA-REVOKE to remote:%u\n",
			remote_acc_id);
		r = amdgpu_ualink_send_npa_revoke_msg(adev, remote_acc_id, handle);
		if (r) {
			dev_err(adev->dev,
				"EXP-CLEANUP: NPA-REVOKE send failed to remote:%u\n",
				remote_acc_id);
			clear_bit(remote_acc_id, exp_xa_node->npa_release_bitmap);

			amdgpu_ualink_handle_connection_reset(adev, remote_acc_id,
						AMDGPU_UALINK_CONN_NOT_READY,
						imp_entry->generation_count);
		}
	}

	/* Wait for the NPA_RELEASE to come back from all importers */
	if (!bitmap_empty(exp_xa_node->npa_release_bitmap,
			AMDGPU_UALINK_ACCEL_MAX)) {
		dev_dbg(adev->dev,
			"EXP-CLEANUP: handle:%llx:%llx NPA-RELEASE bitmap: %*pbl\n",
			handle.handle_hi, handle.handle_lo,
			AMDGPU_UALINK_ACCEL_MAX, exp_xa_node->npa_release_bitmap);

		/* Wait for the NPA_RELEASE to come back from all importers */
		r = wait_for_completion_timeout(&exp_xa_node->npa_done,
					msecs_to_jiffies(AMDGPU_UALINK_RESP_TIMEOUT));

		if (r == 0)
			dev_warn(adev->dev,
				"EXP-CLEANUP: NPA-RELEASE timeout for handle:%llx:%llx\n",
				handle.handle_hi, handle.handle_lo);
	}

	/* Free the NPA addresses given to all the importers */
	amdgpu_ualink_free_all_npa_va(adev, exp_xa_node,
				      orig_importers_bitmap);

	/* Warn about all importers that didn't respond back with
	 * NPA-RELEASE message and trigger connection timeout handling.
	 */
	for_each_set_bit(remote_acc_id, exp_xa_node->npa_release_bitmap,
			 AMDGPU_UALINK_ACCEL_MAX) {
		dev_warn(adev->dev,
			"EXP-CLEANUP: handle:%llx:%llx NPA-RELEASE timeout from remote:%u\n",
			handle.handle_hi, handle.handle_lo, remote_acc_id);
		imp_entry = &exp_xa_node->importer_entries[remote_acc_id];
		amdgpu_ualink_handle_connection_reset(adev, remote_acc_id,
						AMDGPU_UALINK_CONN_NOT_READY,
						imp_entry->generation_count);
	}

free_node:
	xa_erase(&adev->ualink.handle_invalid_xa, handle.handle_lo);
	mutex_destroy(&exp_xa_node->node_lock);
	kfree(exp_xa_node);
}

/* This function is a copy of amdgpu_dma_buf_move_notify() function.
 * amdgpu_dma_buf_move_notify is only called for import attachments.
 * But NPA DMABufs don't use attachments because they are imported
 * on the same device. So we need to invalidate the GPUVM mappings
 * manually.
 */
static void amdgpu_ualink_invalidate_import_mappings(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_operation_ctx ctx = { false, false };
	struct ttm_placement placement = {};
	struct amdgpu_vm_bo_base *bo_base;
	struct drm_exec exec;
	int r;

	/*
	 * Lock the BO together with every client VM page directory it is
	 * mapped into in a single drm_exec transaction.
	 */
	drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES, 0);
	drm_exec_until_all_locked(&exec) {
		r = drm_exec_lock_obj(&exec, &bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto fini;

		for (bo_base = bo->vm_bo; bo_base; bo_base = bo_base->next) {
			r = amdgpu_vm_lock_pd(bo_base->vm, &exec, 0);
			drm_exec_retry_on_contention(&exec);
			if (unlikely(r))
				goto fini;
		}
	}

	amdgpu_vm_bo_move(bo, NULL, false);

	r = ttm_bo_validate(&bo->tbo, &placement, &ctx);
	if (r) {
		dev_err(adev->dev, "Failed to invalidate NPA DMA-buf import (%d)\n",
			r);
		goto fini;
	}

	for (bo_base = bo->vm_bo; bo_base; bo_base = bo_base->next) {
		struct amdgpu_vm *vm = bo_base->vm;

		/*
		 * Fences for the two SDMA page table updates were already
		 * reserved by amdgpu_vm_lock_pd() above (it reserves 2 +
		 * num_fences on the same VM root PD dma_resv).
		 */
		r = amdgpu_vm_clear_freed(adev, vm, NULL);
		if (!r)
			r = amdgpu_vm_handle_moved(adev, vm, &exec.ticket);

		if (r && r != -EBUSY)
			dev_err(adev->dev, "Failed to invalidate VM page tables (%d))\n",
				r);
	}

fini:
	drm_exec_fini(&exec);
}

static int amdgpu_ualink_map_npa_to_dmabuf(struct amdgpu_device *adev,
				struct amdgpu_ualink_imp_xa_node *imp_xa_node)
{
	struct ttm_operation_ctx ctx = { false, false };
	u32 initial_domain = AMDGPU_GEM_DOMAIN_NPA;
	struct drm_gem_object *gobj = NULL;
	u64 alloc_flags, npa_addr, size;
	struct dma_buf *dmabuf;
	struct amdgpu_bo *bo;
	u32 handle;
	int r;

	npa_addr = imp_xa_node->npa_addr;
	size = imp_xa_node->size;

	dev_dbg(adev->dev, "Create NPA BO addr 0x%llx size in pages 0x%llx\n",
		npa_addr, size);

	/*
	 * Create the BO directly in the NPA domain.
	 */
	alloc_flags = AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	r = amdgpu_gem_object_create(adev, size * AMDGPU_GPU_PAGE_SIZE, 1,
				     initial_domain, alloc_flags,
				     ttm_bo_type_device, NULL, &gobj, 0);
	if (r) {
		dev_err(adev->dev, "Failed to create NPA BO. ret %d\n", r);
		return r;
	}

	bo = gem_to_amdgpu_bo(gobj);

	r = amdgpu_bo_reserve(bo, false);
	if (unlikely(r != 0)) {
		dev_err(adev->dev, "Failed to reserve NPA BO, r: %d\n", r);
		goto err_reserve_failed;
	}

	/*
	 * Drop the arbitrarily-placed NPA node and re-create it at the exact
	 * remote window, mirroring amdgpu_bo_create_kernel_at().
	 */
	ttm_resource_free(&bo->tbo, &bo->tbo.resource);

	bo->placements[0].fpfn = npa_addr;
	bo->placements[0].lpfn = npa_addr + size;

	r = ttm_bo_mem_space(&bo->tbo, &bo->placement, &bo->tbo.resource, &ctx);
	amdgpu_bo_unreserve(bo);
	if (r) {
		dev_err(adev->dev,
			"Failed to place NPA BO at 0x%llx, r: %d\n", npa_addr, r);
		goto err_validate_failed;
	}

	r = drm_gem_handle_create(adev->ualink.client.file, gobj, &handle);
	if (r) {
		dev_err(adev->dev,
			"Failed to get handle for NPA GEM object, r: %d\n", r);
		goto err_validate_failed;
	}
	drm_gem_object_put(gobj);

	dmabuf = drm_gem_prime_handle_to_dmabuf(&adev->ddev, adev->ualink.client.file,
						handle, DRM_CLOEXEC | DRM_RDWR);
	if (IS_ERR(dmabuf)) {
		r = PTR_ERR(dmabuf);
		dev_err(adev->dev,
			"Failed to generate DMABuf for NPA GEM object\n");
		goto err_dmabuf_failed;
	}

	imp_xa_node->dmabuf = dmabuf;
	imp_xa_node->gem_handle = handle;

	return 0;

err_dmabuf_failed:
	drm_gem_handle_delete(adev->ualink.client.file, handle);
	return r;
err_validate_failed:
err_reserve_failed:
	drm_gem_object_put(gobj);

	return r;
}

void amdgpu_ualink_revoke_exported_memory(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_ualink_importer_entry *imp_entry;
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	u32 remote_acc_id;
	int r;

	if (!bo->ualink_handle_lo)
		return;

	/* Remove the entry from the Xarray. */
	xa_lock(&adev->ualink.exp_xa);
	exp_xa_node = __xa_erase(&adev->ualink.exp_xa,
				 bo->ualink_handle_lo);
	if (!exp_xa_node) {
		xa_unlock(&adev->ualink.exp_xa);
		dev_warn(adev->dev,
			 "Exp XA: handle_lo:%llx not found\n",
			 bo->ualink_handle_lo);
		return;
	}

	for_each_set_bit(remote_acc_id, exp_xa_node->importers_bitmap,
			 AMDGPU_UALINK_ACCEL_MAX) {
		imp_entry = &exp_xa_node->importer_entries[remote_acc_id];
		list_del_init(&imp_entry->list);
	}
	xa_unlock(&adev->ualink.exp_xa);

	/* Add it to the Handle_Invalid xarray */
	r = xa_err(xa_store(&adev->ualink.handle_invalid_xa,
			    bo->ualink_handle_lo,
			    exp_xa_node, GFP_KERNEL));
	if (r)
		dev_err(adev->dev,
			"Handle_Invalid XA store failed handle:%llx:%llx error:%d\n",
			exp_xa_node->handle.handle_hi,
			exp_xa_node->handle.handle_lo, r);

	amdgpu_ualink_exp_xa_entry_put(exp_xa_node);
}

static void amdgpu_ualink_process_npa_release_msg(struct amdgpu_device *adev,
						 u32 remote_acc_id,
						 struct amdgpu_ualink_handle handle)
{
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;

	xa_lock(&adev->ualink.handle_invalid_xa);
	exp_xa_node = xa_load(&adev->ualink.handle_invalid_xa, handle.handle_lo);
	if (!exp_xa_node) {
		dev_warn(adev->dev,
			 "NPA-RELEASE: Handle (%llx:%llx) not found\n",
			 handle.handle_hi, handle.handle_lo);
		goto out;
	}

	/* Confirm that the complete handle matches. */
	if (handle.handle_hi != exp_xa_node->handle.handle_hi) {
		dev_warn(adev->dev,
			 "NPA-RELEASE: handle_hi mismatch exp:%llx got:%llx:%llx remote:%u\n",
			 exp_xa_node->handle.handle_hi,
			 handle.handle_hi, handle.handle_lo, remote_acc_id);
		goto out;
	}

	/* Test and clear the bit corresponding to the remote GPU id to signal
	 * the arrival of NPA_RELEASE message from it.
	 * If the corresponding bit wasn't set, then raise a warning and ignore
	 * the NPA-Release message from the remote GPU.
	 */
	if (!test_and_clear_bit(remote_acc_id, exp_xa_node->npa_release_bitmap)) {
		dev_warn(adev->dev,
			 "NPA-RELEASE: unexpected from remote:%u handle:%llx:%llx\n",
			 remote_acc_id, handle.handle_hi, handle.handle_lo);
		goto out;
	}

	/* Signal completion if NPA_Release received from all importers */
	if (bitmap_empty(exp_xa_node->npa_release_bitmap,
			 AMDGPU_UALINK_ACCEL_MAX))
		complete(&exp_xa_node->npa_done);

out:
	xa_unlock(&adev->ualink.handle_invalid_xa);
}

static void amdgpu_ualink_process_npa_revoke_msg(struct amdgpu_device *adev,
						u32 remote_acc_id,
						struct amdgpu_ualink_handle handle)
{
	struct amdgpu_ualink_imp_xa_node *imp_xa_node;
	struct amdgpu_bo *bo;
	int r = 0;

	/* Remove the entry from the Xarray. */
	xa_lock(&adev->ualink.imp_xa);
	imp_xa_node = xa_load(&adev->ualink.imp_xa, handle.handle_lo);
	if (!imp_xa_node) {
		xa_unlock(&adev->ualink.imp_xa);
		dev_warn(adev->dev,
			 "NPA-REVOKE: Handle (%llx:%llx) not found\n",
			 handle.handle_hi, handle.handle_lo);
		return;
	}

	/* Confirm that the complete handle matches. */
	if (handle.handle_hi != imp_xa_node->handle.handle_hi) {
		xa_unlock(&adev->ualink.imp_xa);
		dev_warn(adev->dev,
			 "NPA-REVOKE: handle_hi mismatch exp:%llx got:%llx:%llx remote:%u\n",
			 imp_xa_node->handle.handle_hi,
			 handle.handle_hi, handle.handle_lo, remote_acc_id);
		return;
	}

	switch (READ_ONCE(imp_xa_node->node_state)) {
	case AMDGPU_UALINK_NODE_READY:
		WRITE_ONCE(imp_xa_node->node_state, AMDGPU_UALINK_NODE_TEARDOWN);
		list_del_init(&imp_xa_node->list);
		xa_unlock(&adev->ualink.imp_xa);

		/* Invalidate the GPUVM mappings */
		bo = gem_to_amdgpu_bo(imp_xa_node->dmabuf->priv);
		amdgpu_ualink_invalidate_import_mappings(bo);

		/* Drop the refcount for the node */
		amdgpu_ualink_imp_xa_entry_put(imp_xa_node);
		break;
	case AMDGPU_UALINK_NODE_PENDING:
		/* The import is still building the dma-buf and nothing has
		 * been handed to user-space yet. The importing thread sees
		 * the teardown state and unwinds.
		 */
		WRITE_ONCE(imp_xa_node->node_state, AMDGPU_UALINK_NODE_TEARDOWN);
		xa_unlock(&adev->ualink.imp_xa);
		break;
	default:
		/* NPA-REVOKE always follows NPA-RSP, so a NOT_READY node means
		 * a stale revoke, and a node in teardown is already being
		 * released by whoever moved it there.
		 */
		xa_unlock(&adev->ualink.imp_xa);
		return;
	}

	r = amdgpu_ualink_send_npa_release_msg(adev, remote_acc_id, handle);
	if (r)
		dev_err(adev->dev,
			"NPA-Release send failed remote:%u handle:%llx:%llx error:%d\n",
			remote_acc_id, handle.handle_hi, handle.handle_lo, r);
}

static void amdgpu_ualink_process_npa_fail_msg(struct amdgpu_device *adev,
				       u32 remote_acc_id, u64 partial_handle,
				       u32 fail_reason)
{
	struct amdgpu_ualink_imp_xa_node *imp_xa_node;
	int r = 0;

	if (!amdgpu_ualink_check_conn_ready(adev, remote_acc_id, 0)) {
		dev_warn(adev->dev,
			"NPA-FAIL: no connection with remote AccId:%u\n",
			remote_acc_id);
		goto conn_setup;
	}

	xa_lock(&adev->ualink.imp_xa);
	imp_xa_node = xa_load(&adev->ualink.imp_xa, partial_handle);
	if (!imp_xa_node) {
		xa_unlock(&adev->ualink.imp_xa);
		dev_warn(adev->dev,
			"NPA-FAIL: imp XA handle not found:%llx\n",
			partial_handle);
		return;
	}

	imp_xa_node->fail_reason = fail_reason;
	/* Signal completion done to signal response received for NPA-REQ
	 * message.
	 * If the node is in NOT_READY state, then set the node state to
	 * PENDING and signal the completion. If the node is not in NOT_READY
	 * state, then it is an unsolicited NPA-FAIL message and we
	 * log a debug message.
	 */
	if (READ_ONCE(imp_xa_node->node_state) == AMDGPU_UALINK_NODE_NOT_READY) {
		WRITE_ONCE(imp_xa_node->node_state, AMDGPU_UALINK_NODE_PENDING);
		complete(&imp_xa_node->npa_done);
	} else {
		dev_dbg(adev->dev,
			"NPA-FAIL: unsolicited for handle:%llx:%llx from AccId:%u\n",
			imp_xa_node->handle.handle_hi, imp_xa_node->handle.handle_lo,
			remote_acc_id);
	}
	xa_unlock(&adev->ualink.imp_xa);

	return;

conn_setup:
	r = amdgpu_ualink_setup_connection(adev, remote_acc_id);
	if (r)
		dev_warn(adev->dev,
			"NPA-FAIL: connection setup failed with remote AccId:%u\n",
			remote_acc_id);
}

static void amdgpu_ualink_process_npa_rsp_msg(struct amdgpu_device *adev,
				      u32 remote_acc_id, u64 partial_handle,
				      u64 npa_addr, u64 size)
{
	struct amdgpu_ualink_imp_xa_node *imp_xa_node;
	int r = 0;

	/* Check if the connection is established. If it is not, then start
	 * connection setup.
	 */
	if (!amdgpu_ualink_check_conn_ready(adev, remote_acc_id, 0)) {
		dev_warn(adev->dev,
			"NPA-RSP: no connection with remote AccId:%u\n",
			remote_acc_id);
		goto conn_setup;
	}

	xa_lock(&adev->ualink.imp_xa);
	imp_xa_node = xa_load(&adev->ualink.imp_xa, partial_handle);
	if (!imp_xa_node) {
		xa_unlock(&adev->ualink.imp_xa);
		dev_warn(adev->dev,
			"NPA-RSP: imp XA handle not found:%llx\n", partial_handle);
		return;
	}

	/* NPA addr received in NPA-RSP is page aligned and without the remote
	 * GPU-id in Bits 41-50. Assemble back the NPA address before storing
	 * it.
	 */
	imp_xa_node->npa_addr = GENERATE_NPA(npa_addr, remote_acc_id);
	/* Size is in number of GPU pages granularity. */
	imp_xa_node->size = size;

	/* Signal completion done to signal NPA_RSP received.
	 * If the node is in NOT_READY state, then set the node state to
	 * PENDING and signal the completion. If the node is not in NOT_READY
	 * state, then it is an unsolicited NPA-RSP message and we
	 * log a debug message.
	 */
	if (READ_ONCE(imp_xa_node->node_state) == AMDGPU_UALINK_NODE_NOT_READY) {
		WRITE_ONCE(imp_xa_node->node_state, AMDGPU_UALINK_NODE_PENDING);
		complete(&imp_xa_node->npa_done);
	} else {
		dev_dbg(adev->dev,
			"NPA-RSP: unsolicited for handle:%llx:%llx from AccId:%u\n",
			imp_xa_node->handle.handle_hi, imp_xa_node->handle.handle_lo,
			remote_acc_id);
	}
	xa_unlock(&adev->ualink.imp_xa);

	return;

conn_setup:
	r = amdgpu_ualink_setup_connection(adev, remote_acc_id);
	if (r)
		dev_warn(adev->dev,
			"NPA-RSP: connection setup failed with remote AccId:%u\n",
			remote_acc_id);
}

static void amdgpu_ualink_process_npa_req_msg(struct amdgpu_device *adev,
				      u32 remote_acc_id,
				      struct amdgpu_ualink_handle handle)
{
	struct amdgpu_ualink_importer_entry *importer_entry, *npa_addr_entry;
	u32 addr_mode = adev->ualink.info->vpod.addr_mode;
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	u64 range_start, range_end, pte_flags;
	struct drm_mm_node *mm_node = NULL;
	int r = 0, fail_reason = 0;
	bool send_npa_fail = true;
	u64 npa_addr = 0, size;
	struct amdgpu_bo *bo;
	u32 gen_count;

	/* Check if the connection is established. If it is not, then start
	 * connection setup.
	 */
	gen_count = amdgpu_ualink_check_conn_ready(adev, remote_acc_id, 0);
	if (!gen_count) {
		dev_warn(adev->dev,
			"NPA-REQ: no connection with remote AccId:%u\n",
			remote_acc_id);
		goto conn_setup;
	}

	/* Check entry exists in Exporter XA. If yes, increase the refcount
	 * for the node.
	 */
	xa_lock(&adev->ualink.exp_xa);
	exp_xa_node = xa_load(&adev->ualink.exp_xa, handle.handle_lo);
	if (!exp_xa_node  || (handle.handle_hi != exp_xa_node->handle.handle_hi) ||
	    !amdgpu_ualink_exp_xa_entry_get(exp_xa_node)) {
		xa_unlock(&adev->ualink.exp_xa);
		dev_warn(adev->dev,
			"NPA-REQ: exp XA handle not found handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
		fail_reason = AMDGPU_UALINK_NPA_FAIL_INVALID_HANDLE;
		goto handle_invalid_fail;
	}
	xa_unlock(&adev->ualink.exp_xa);

	bo = exp_xa_node->bo;
	size = amdgpu_bo_ngpu_pages(bo);

	/* Pin the BO */
	r = amdgpu_bo_reserve(bo, true);
	if (unlikely(r)) {
		dev_warn(adev->dev,
			"NPA-REQ: BO reserve failed handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
		fail_reason = AMDGPU_UALINK_NPA_FAIL_ERROR;
		goto bo_reserve_fail;
	}
	r = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_VRAM);
	amdgpu_bo_unreserve(bo);
	if (r) {
		dev_warn(adev->dev,
			"NPA-REQ: BO pin failed handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
		fail_reason = AMDGPU_UALINK_NPA_FAIL_ERROR;
		goto bo_pin_fail;
	}

	if (addr_mode == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT) {
		mutex_lock(&exp_xa_node->node_lock);
		importer_entry = &exp_xa_node->importer_entries[remote_acc_id];
		npa_addr_entry = importer_entry;
		mutex_unlock(&exp_xa_node->node_lock);
		/* Check if NPA address is already allocated for this importer.
		 * If yes, then send the NPA-FAIL message back to the remote GPU.
		 */
		if (importer_entry->npa_addr) {
			fail_reason = AMDGPU_UALINK_NPA_FAIL_DUPLICATE;
			goto npa_duplicate_fail;
		}

		range_start = ((u64)remote_acc_id << AMDGPU_UALINK_NPA_ADDR_GPUID_SHIFT) |
				AMDGPU_UALINK_NPA_ADDR_RANGE_RESERVED;
		range_end = range_start | AMDGPU_UALINK_NPA_ADDR_RANGE_MASK;
	} else {
		/* We store NPA-address in importer_entries[0] in
		 * Source-Aliasing mode.
		 */
		mutex_lock(&exp_xa_node->node_lock);
		npa_addr_entry = &exp_xa_node->importer_entries[0];
		importer_entry = &exp_xa_node->importer_entries[remote_acc_id];
		/* Check if NPA address is already allocated for this importer.
		 * If yes, then set the corresponding bit in the importers_bitmap,
		 * set the generation count and send the NPA-RSP back to the remote GPU.
		 */
		if (npa_addr_entry->npa_addr) {
			npa_addr = npa_addr_entry->npa_addr;
			set_bit(remote_acc_id, exp_xa_node->importers_bitmap);
			importer_entry->generation_count = gen_count;
			mutex_unlock(&exp_xa_node->node_lock);
			dev_dbg(adev->dev,
				"NPA-REQ: NPA:%llx size:%llx handle:%llx:%llx\n",
				npa_addr, size, handle.handle_hi, handle.handle_lo);

			goto send_npa_rsp;
		}
		mutex_unlock(&exp_xa_node->node_lock);

		range_start = 0;
		range_end = 0;
	}

	mm_node = kzalloc(sizeof(*mm_node), GFP_KERNEL);
	if (!mm_node) {
		dev_warn(adev->dev,
			"NPA-REQ: mm_node alloc failed handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
		fail_reason = AMDGPU_UALINK_NPA_FAIL_NOSPACE;
		goto mem_alloc_fail;
	}

	/* Allocate NPA address */
	r = amdgpu_ualink_npa_alloc_va(adev, mm_node, 0, range_start,
					range_end, size);
	if (r) {
		dev_warn(adev->dev,
			"NPA-REQ: NPA addr alloc failed handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
		fail_reason = AMDGPU_UALINK_NPA_FAIL_NOSPACE;
		goto npa_alloc_fail;
	}
	npa_addr = mm_node->start;

	pte_flags = amdgpu_ualink_get_export_pte_flags(adev, bo, 0);
	dev_dbg(adev->dev,
		"NPA-REQ: Allocated NPA:%llx size:%llx PTE:%llx handle:%llx:%llx\n",
		npa_addr, size, pte_flags, handle.handle_hi, handle.handle_lo);

	/* Map the NPA address into NPA VM*/
	r = amdgpu_ualink_map_npa_addr(adev, npa_addr, size, bo, 0, pte_flags);
	if (r) {
		fail_reason = AMDGPU_UALINK_NPA_FAIL_ERROR;
		dev_warn(adev->dev,
			"NPA-REQ: NPA addr (%llx) map failed handle:%llx:%llx\n",
			npa_addr, handle.handle_hi, handle.handle_lo);
		goto map_npa_fail;
	}

	dev_dbg(adev->dev,
		"NPA-REQ: Mapped NPA:%llx size:%llx pte:%llx handle:%llx:%llx\n",
		npa_addr, size, pte_flags, handle.handle_hi, handle.handle_lo);
send_npa_rsp:
	/* Send NPA-RSP back to the remote GPU */
	r = amdgpu_ualink_send_npa_rsp_msg(adev, remote_acc_id, handle,
					   STRIP_NPA(npa_addr), size);
	if (r) {
		dev_warn(adev->dev,
			"NPA-REQ: send NPA-RSP failed remote:%u handle:%llx:%llx\n",
			remote_acc_id, handle.handle_hi, handle.handle_lo);
		send_npa_fail = false;
		goto send_npa_rsp_fail;
	}

	dev_dbg(adev->dev,
		"NPA-REQ: Sent NPA-RSP with NPA:%llx size:%llx handle:%llx:%llx\n",
		npa_addr, size, handle.handle_hi, handle.handle_lo);

	/* If this is the first time we are setting the bit for this importer,
	 * then store the NPA address, mm_node and generation count.
	 */
	mutex_lock(&exp_xa_node->node_lock);
	if (!test_and_set_bit(remote_acc_id, exp_xa_node->importers_bitmap)) {
		npa_addr_entry->npa_addr = npa_addr;
		npa_addr_entry->mm_node = mm_node;
		importer_entry->generation_count = gen_count;
	}
	mutex_unlock(&exp_xa_node->node_lock);

	dev_dbg(adev->dev,
		"NPA-REQ: BO pin_count:%d, importers:%d, handle:%llx:%llx\n",
		bo->tbo.pin_count, bitmap_weight(exp_xa_node->importers_bitmap,
		AMDGPU_UALINK_ACCEL_MAX), handle.handle_hi, handle.handle_lo);
	WARN_ON(bo->tbo.pin_count < bitmap_weight(exp_xa_node->importers_bitmap,
						   AMDGPU_UALINK_ACCEL_MAX));

	/* Add this node to the exported handles list for the remote GPU,
	 * but only if the node is still in exp_xa. If revoke already erased
	 * it, skip the list_add to avoid a dangling list entry. The cleanup
	 * worker is guaranteed to run after we drop our ref, so it will see
	 * this importer in the bitmap and send NPA-REVOKE.
	 */
	xa_lock(&adev->ualink.exp_xa);
	if (xa_load(&adev->ualink.exp_xa, exp_xa_node->handle.handle_lo) == exp_xa_node)
		list_add(&importer_entry->list, &adev->ualink.exp_handles_list[remote_acc_id]);
	xa_unlock(&adev->ualink.exp_xa);

	amdgpu_ualink_exp_xa_entry_put(exp_xa_node);

	return;

send_npa_rsp_fail:
	mutex_lock(&exp_xa_node->node_lock);
	clear_bit(remote_acc_id, exp_xa_node->importers_bitmap);
	mutex_unlock(&exp_xa_node->node_lock);
	if (mm_node)
		amdgpu_ualink_unmap_npa_addr(adev, bo, npa_addr, size);

map_npa_fail:
	if (mm_node)
		amdgpu_ualink_npa_free_va(adev, mm_node);

npa_alloc_fail:
	kfree(mm_node);
mem_alloc_fail:
npa_duplicate_fail:
	r = amdgpu_bo_reserve(bo, true);
	if (likely(!r)) {
		amdgpu_bo_unpin(bo);
		amdgpu_bo_unreserve(bo);
	} else {
		dev_warn(adev->dev,
			"NPA-REQ: BO reserve to unpin failed for handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
	}

bo_pin_fail:
bo_reserve_fail:
	amdgpu_ualink_exp_xa_entry_put(exp_xa_node);

handle_invalid_fail:
	if (send_npa_fail) {
		r = amdgpu_ualink_send_npa_fail_msg(adev, remote_acc_id,
						    handle, fail_reason);
		if (r)
			dev_warn(adev->dev,
				"NPA-REQ: send NPA-FAIL failed remote:%u handle:%llx:%llx\n",
				remote_acc_id, handle.handle_hi, handle.handle_lo);
	}
	return;
conn_setup:
	r = amdgpu_ualink_setup_connection(adev, remote_acc_id);
	if (r)
		dev_warn(adev->dev,
			"NPA-REQ: connection setup failed with remote AccId:%u\n",
			remote_acc_id);
}

static int amdgpu_ualink_translate_npa_fail_reason(struct amdgpu_device *adev,
						   u32 fail_reason)
{
	switch (fail_reason) {
	case AMDGPU_UALINK_NPA_FAIL_NOSPACE:
		return -ENOSPC;
	case AMDGPU_UALINK_NPA_FAIL_INVALID_HANDLE:
	case AMDGPU_UALINK_NPA_FAIL_DUPLICATE:
	case AMDGPU_UALINK_NPA_FAIL_ERROR:
		return -EINVAL;
	default:
		dev_err(adev->dev,
			"IMPORT: invalid NPA-FAIL reason:%u\n",
			fail_reason);
		return -EINVAL;
	}
}

static int amdgpu_ualink_do_import_handle(struct amdgpu_device *adev,
				struct amdgpu_ualink_imp_xa_node *imp_xa_node,
				u32 remote_acc_id)
{
	struct amdgpu_ualink_handle handle = imp_xa_node->handle;
	u32 generation_count;
	int r;

	/* First check if the connection is setup with the
	 * remote GPU. If yes, then initiate the NPA protocol to
	 * get the NPA address.
	 * If not, then initiate the HELLO protocol to first setup
	 * the connection and once the connection is setup, then
	 * initiate the NPA protocol.
	 */
	r = amdgpu_ualink_setup_connection(adev, remote_acc_id);
	if (r) {
		if (r != -EAGAIN)
			dev_warn(adev->dev,
			"IMPORT: connection setup failed with remote AccId:%u\n",
			remote_acc_id);
		return r;
	}

	/* Send NPA_REQ message */
	r = amdgpu_ualink_send_npa_req_msg(adev, remote_acc_id, handle);
	if (r) {
		dev_warn(adev->dev,
			"IMPORT: NPA-REQ send failed to remote AccId:%u\n",
			remote_acc_id);
		goto reset_conn;
	}

	/* Wait for the NPA_RSP to come back */
	r = wait_for_completion_interruptible_timeout(&imp_xa_node->npa_done,
				msecs_to_jiffies(AMDGPU_UALINK_RESP_TIMEOUT));
	if (r == -ERESTARTSYS) {
		dev_err_ratelimited(adev->dev,
			"IMPORT: NPA-RSP wait interrupted by signal\n");
		return r;
	} else if (r == 0) {
		dev_warn(adev->dev,
			"IMPORT: NPA-RSP timeout from remote AccId:%u\n",
			remote_acc_id);
		r = -ETIMEDOUT;
		goto reset_conn;
	}

	/* If the NPA addr/size isn't filled with valid values, then either
	 * we got a NPA_FAIL or something bad happened. In either case, we
	 * return the error back to user-space.
	 */
	if (imp_xa_node->fail_reason) {
		dev_warn(adev->dev,
			"IMPORT: NPA-REQ failed with fail_reason:%d handle:%llx:%llx\n",
			imp_xa_node->fail_reason, handle.handle_hi, handle.handle_lo);
		return amdgpu_ualink_translate_npa_fail_reason(adev,
						imp_xa_node->fail_reason);
	}

	if (!imp_xa_node->npa_addr || !imp_xa_node->size) {
		dev_warn(adev->dev,
			"IMPORT: invalid npa:%llx or size:%llx\n",
			imp_xa_node->npa_addr, imp_xa_node->size);
		imp_xa_node->npa_addr = 0;
		imp_xa_node->size = 0;
		return -EINVAL;
	}

	r = amdgpu_ualink_map_npa_to_dmabuf(adev, imp_xa_node);
	if (r) {
		dev_warn(adev->dev,
			"IMPORT: dmabuf creation failed npa:%llx size:%llx\n",
			imp_xa_node->npa_addr, imp_xa_node->size);
		imp_xa_node->npa_addr = 0;
		imp_xa_node->size = 0;
		return r;
	}

	/* Add this node to the imported handles list for the remote GPU,
	 * unless the exporter revoked the handle while the import was in
	 * flight. The dmabuf is released with the last node reference.
	 */
	xa_lock(&adev->ualink.imp_xa);
	if (READ_ONCE(imp_xa_node->node_state) == AMDGPU_UALINK_NODE_TEARDOWN) {
		xa_unlock(&adev->ualink.imp_xa);
		dev_dbg(adev->dev,
			"IMPORT: handle:%llx:%llx revoked during import\n",
			handle.handle_hi, handle.handle_lo);
		return -EINVAL;
	}
	list_add(&imp_xa_node->list, &adev->ualink.imp_handles_list[remote_acc_id]);
	WRITE_ONCE(imp_xa_node->node_state, AMDGPU_UALINK_NODE_READY);
	xa_unlock(&adev->ualink.imp_xa);

	return 0;

reset_conn:
	dev_dbg(adev->dev,
		"IMPORT: Resetting connection for remote:%u\n", remote_acc_id);
	generation_count = amdgpu_ualink_check_conn_ready(adev, remote_acc_id, 0);
	amdgpu_ualink_handle_connection_reset(adev, remote_acc_id,
					      AMDGPU_UALINK_CONN_NOT_READY,
					      generation_count);
	return r;
}

static int amdgpu_ualink_local_import(struct amdgpu_device *adev,
				      u32 remote_acc_id,
				      struct amdgpu_ualink_handle *handle,
				      int *fd_out)
{
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	struct amdgpu_device *peer_adev;
	int fd;

	mutex_lock(&mgpu_info.mutex);
	peer_adev = find_peer_adev(remote_acc_id);
	mutex_unlock(&mgpu_info.mutex);
	if (!peer_adev) {
		dev_err(adev->dev,
			"IMPORT LOCAL: peer adev not found for AccId:%u\n",
			remote_acc_id);
		return -ENODEV;
	}

	xa_lock(&peer_adev->ualink.exp_xa);
	exp_xa_node = xa_load(&peer_adev->ualink.exp_xa, handle->handle_lo);
	if (!exp_xa_node ||
	    exp_xa_node->handle.handle_hi != handle->handle_hi ||
	    !amdgpu_ualink_exp_xa_entry_get(exp_xa_node)) {
		xa_unlock(&peer_adev->ualink.exp_xa);
		dev_err(adev->dev,
			"IMPORT LOCAL: handle:%llx:%llx not found in peer exp_xa\n",
			handle->handle_hi, handle->handle_lo);
		return -EINVAL;
	}
	xa_unlock(&peer_adev->ualink.exp_xa);

	get_dma_buf(exp_xa_node->dmabuf);
	/* Get a new fd for the DMABuf */
	fd = dma_buf_fd(exp_xa_node->dmabuf, O_CLOEXEC | O_RDWR);
	if (fd < 0) {
		dev_err(adev->dev,
			"IMPORT LOCAL: dma-buf fd failed handle:%llx:%llx\n",
			handle->handle_hi, handle->handle_lo);
		dma_buf_put(exp_xa_node->dmabuf);
		amdgpu_ualink_exp_xa_entry_put(exp_xa_node);
		return fd;
	}

	amdgpu_ualink_exp_xa_entry_put(exp_xa_node);
	*fd_out = fd;
	return 0;
}

int amdgpu_ualink_import_handle(struct drm_device *dev,
				const struct amdgpu_ualink_handle *ualink_handle,
				int *fd_out)
{
	struct amdgpu_ualink_imp_xa_node *imp_xa_node;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_ualink_handle handle = *ualink_handle;
	u32 remote_acc_id, node_state;
	int r = 0, fd;

	remote_acc_id = (handle.handle_lo &
			AMDGPU_UALINK_HANDLE_ACCID_MASK);

	if (remote_acc_id >= AMDGPU_UALINK_ACCEL_MAX) {
		dev_err(adev->dev,
			"IMPORT: invalid remote AccId:%u\n", remote_acc_id);
		return -EINVAL;
	}

	if (amdgpu_ualink_is_local_accel(adev, remote_acc_id))
		return amdgpu_ualink_local_import(adev, remote_acc_id,
						  &handle, fd_out);

	xa_lock(&adev->ualink.imp_xa);
	imp_xa_node = xa_load(&adev->ualink.imp_xa, handle.handle_lo);

	if (imp_xa_node) {
		/* If node state is Not_ready/Pending, then some other
		 * thread is already trying the NPA protocol for the same
		 * ualink handle. Back off and let the thread finish.
		 * If the node state is Teardown, then it means this
		 * node is about to be removed. So let user-space know
		 * that this handle is invalid.
		 */
		node_state = READ_ONCE(imp_xa_node->node_state);
		if (node_state == AMDGPU_UALINK_NODE_NOT_READY ||
		    node_state == AMDGPU_UALINK_NODE_PENDING) {
			xa_unlock(&adev->ualink.imp_xa);
			r = -EAGAIN;
			goto out;
		} else if (node_state == AMDGPU_UALINK_NODE_TEARDOWN) {
			xa_unlock(&adev->ualink.imp_xa);
			r = -EINVAL;
			goto out;
		}

		/* Increase the refcount while we are processing the request */
		r = amdgpu_ualink_imp_xa_entry_get(imp_xa_node) ? 0 : -EINVAL;
		xa_unlock(&adev->ualink.imp_xa);

		/* If the refcount has become 0 but the entry is not yet
		 * removed from the Xarray, then return error to user-space.
		 */
		if (r)
			goto out;
	} else {
		xa_unlock(&adev->ualink.imp_xa);
		/* if the partial handle doesn't exist in the Importer xarray then
		 * initiate the NPA protocol and generate the DMABuf corresponding
		 * to the NPA address.
		 * First store the entry in the Xarray.
		 */
		imp_xa_node = kzalloc(sizeof(*imp_xa_node), GFP_KERNEL);
		if (!imp_xa_node) {
			r = -ENOMEM;
			goto out;
		}
		imp_xa_node->adev = adev;
		imp_xa_node->node_state = AMDGPU_UALINK_NODE_NOT_READY;
		imp_xa_node->handle = handle;
		init_completion(&imp_xa_node->npa_done);
		INIT_LIST_HEAD(&imp_xa_node->list);
		kref_init(&imp_xa_node->refcount);

		/* Take an extra reference to store in the Xarray. The error
		 * handling paths will drop both these references, while a
		 * successful path will drop only one reference to the Xarray
		 * entry.
		 */
		amdgpu_ualink_imp_xa_entry_get(imp_xa_node);

		/* Check if another thread created a node for the same handle while
		 * we were trying to create and initialize the node.
		 */
		r = xa_insert(&adev->ualink.imp_xa, handle.handle_lo,
			      imp_xa_node, GFP_KERNEL);
		if (r) {
			kfree(imp_xa_node);
			/* -EBUSY means another thread raced us and inserted a
			 * node for the same handle. Ask user-space to retry.
			 */
			if (r == -EBUSY)
				r = -EAGAIN;
			else
				dev_err(adev->dev,
					"IMPORT: XA insert failed for handle:%llx:%llx err:%d\n",
					handle.handle_hi, handle.handle_lo, r);
			goto out;
		}

		r = amdgpu_ualink_do_import_handle(adev, imp_xa_node, remote_acc_id);

		/* If error is returned, then cleanup the xarray entry before returning
		 * the error back to user-space
		 */
		if (r) {
			amdgpu_ualink_imp_xa_entry_put(imp_xa_node);
			if (r != -EAGAIN)
				dev_err(adev->dev,
					"IMPORT: XA import failed for handle:%llx:%llx\n",
					handle.handle_hi, handle.handle_lo);
			goto cleanup;
		}
	}

	/* dma_buf_fd consumes a reference and assigns it to the fd.
	 * Therefore take an extra reference to be consumed. It will be
	 * released when user mode closes the fd.
	 */
	get_dma_buf(imp_xa_node->dmabuf);

	fd = dma_buf_fd(imp_xa_node->dmabuf, O_CLOEXEC | O_RDWR);
	if (fd >= 0) {
		*fd_out = fd;
	} else {
		dma_buf_put(imp_xa_node->dmabuf);
		r = fd;
		dev_err(adev->dev,
			"IMPORT: dma-buf fd creation failed handle:%llx:%llx\n",
			handle.handle_hi, handle.handle_lo);
	}

cleanup:
	amdgpu_ualink_imp_xa_entry_put(imp_xa_node);
out:
	return r;
}

int amdgpu_ualink_export_handle(struct drm_device *dev, struct drm_file *filp,
				u32 gem_handle,
				struct amdgpu_ualink_handle *handle_out)
{
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	struct amdgpu_ualink_handle handle;
	struct drm_gem_object *gobj;
	struct amdgpu_device *adev;
	struct amdgpu_bo *robj;
	int r = 0, i;

	gobj = drm_gem_object_lookup(filp, gem_handle);
	if (!gobj)
		return -ENOENT;

	robj = gem_to_amdgpu_bo(gobj);
	adev = amdgpu_ttm_adev(robj->tbo.bdev);

	if (!(robj->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM)) {
		dev_err(adev->dev, "Only VRAM BOs can be exported\n");
		r = -EOPNOTSUPP;
		goto out;
	}

	if (!robj->ualink_handle_lo) {
		/* If no ualink handle generated for BO, then generate one and
		 * add it to the exporter Xarray.
		 */
		exp_xa_node = kzalloc(sizeof(*exp_xa_node), GFP_KERNEL);
		if (!exp_xa_node) {
			dev_err(adev->dev, "Failed to allocate exp_xa_node\n");
			r = -ENOMEM;
			goto out;
		}

		amdgpu_bo_ref(robj);
		exp_xa_node->bo = robj;
		init_completion(&exp_xa_node->npa_done);
		bitmap_zero(exp_xa_node->importers_bitmap,
			    AMDGPU_UALINK_ACCEL_MAX);
		bitmap_zero(exp_xa_node->npa_release_bitmap,
			AMDGPU_UALINK_ACCEL_MAX);
		kref_init(&exp_xa_node->refcount);
		mutex_init(&exp_xa_node->node_lock);
		INIT_WORK(&exp_xa_node->cleanup_work,
			  amdgpu_ualink_exp_cleanup_worker);
		for (i = 0; i < AMDGPU_UALINK_ACCEL_MAX; i++) {
			INIT_LIST_HEAD(&exp_xa_node->importer_entries[i].list);
			exp_xa_node->importer_entries[i].parent = exp_xa_node;
		}
		/* DMABuf handle for local import of fabric handles */
		exp_xa_node->dmabuf = drm_gem_prime_handle_to_dmabuf(&adev->ddev, filp,
						gem_handle, DRM_CLOEXEC | DRM_RDWR);
		if (IS_ERR(exp_xa_node->dmabuf)) {
			r = PTR_ERR(exp_xa_node->dmabuf);
			dev_err(adev->dev, "Failed to generate DMABuf for the BO\n");
			goto err_unref_bo;
		}

		xa_lock(&adev->ualink.exp_xa);
		amdgpu_generate_ualink_handle(adev, &handle);
		exp_xa_node->handle = handle;
		r = __xa_insert(&adev->ualink.exp_xa, handle.handle_lo,
				exp_xa_node, GFP_KERNEL);
		xa_unlock(&adev->ualink.exp_xa);
		if (r) {
			dev_err(adev->dev, "Failed to insert exp_xa_node into XA: %d\n", r);
			goto err_put_dmabuf;
		}

		r = amdgpu_bo_reserve(robj, false);
		if (r)
			goto err_erase_xa;

		if (robj->ualink_handle_lo) {
			amdgpu_bo_unreserve(robj);
			r = -EAGAIN;
			goto err_erase_xa;
		}

		robj->ualink_handle_lo = handle.handle_lo;
		amdgpu_bo_unreserve(robj);

		/* Return the generated handle back to the caller */
		*handle_out = handle;
	} else {
		handle_out->handle_lo = robj->ualink_handle_lo;

		/* Do a sanity check to ensure the handle exists in the XA */
		xa_lock(&adev->ualink.exp_xa);
		exp_xa_node = xa_load(&adev->ualink.exp_xa,
				      robj->ualink_handle_lo);
		xa_unlock(&adev->ualink.exp_xa);
		WARN(!exp_xa_node, "Exp XA: Handle_Lo: %llx not found",
		     robj->ualink_handle_lo);
		if (exp_xa_node)
			handle_out->handle_hi = exp_xa_node->handle.handle_hi;
	}

	drm_gem_object_put(gobj);
	return 0;

err_erase_xa:
	xa_erase(&adev->ualink.exp_xa, handle.handle_lo);
err_put_dmabuf:
	dma_buf_put(exp_xa_node->dmabuf);
err_unref_bo:
	amdgpu_bo_unref(&exp_xa_node->bo);
	kfree(exp_xa_node);
out:
	drm_gem_object_put(gobj);
	return r;
}

int amdgpu_gem_ualink_handle_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *filp)
{
	union drm_amdgpu_ualink_handle *args = data;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_ualink_handle handle = {};
	u32 gem_handle;
	int r, fd = -1;

	/* The ioctl is registered for every amdgpu device, but the UALink
	 * software state only exists on ASICs that instantiate the UALink IP
	 * block. Bail out if it is absent, and short-circuit before the
	 * accel_state dereference below.
	 */
	if (!adev->ualink.info ||
	    adev->ualink.info->accel_state != AMDGPU_UALINK_ACCEL_STATE_ACTIVE) {
		dev_dbg(adev->dev,
			"ualink device not available or not in active state\n");
		return -EOPNOTSUPP;
	}

	/* The input and output members of the ioctl argument alias each other.
	 * Latch every input field before invoking the handlers, and only write
	 * the output fields afterwards.
	 */
	switch (args->in.op) {
	case DRM_AMDGPU_UALINK_HANDLE_OP_EXPORT:
		gem_handle = args->in.gem_handle;
		r = amdgpu_ualink_export_handle(dev, filp, gem_handle, &handle);
		if (!r) {
			args->out.export_ualink_handle[0] = handle.handle_lo;
			args->out.export_ualink_handle[1] = handle.handle_hi;
		}
		break;
	case DRM_AMDGPU_UALINK_HANDLE_OP_IMPORT:
		handle.handle_lo = args->in.import_ualink_handle[0];
		handle.handle_hi = args->in.import_ualink_handle[1];
		r = amdgpu_ualink_import_handle(dev, &handle, &fd);
		if (!r)
			args->out.import_dmabuf_handle = fd;
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

int amdgpu_ualink_manager_start(struct amdgpu_device *adev)
{
	int i, r;

	r = amdgpu_vm_init(adev, &adev->ualink.npa_vm, 0);
	if (r)
		goto out;

	/* For pinning page tables and using CPU for page table updates. */
	r = amdgpu_vm_make_npa(adev, &adev->ualink.npa_vm);
	if (r)
		goto uninit_vm;

	adev->ualink.npa_wq = alloc_workqueue("NPA WQ", WQ_UNBOUND, 0);
	if (unlikely(!adev->ualink.npa_wq)) {
		dev_err(adev->dev, "Failed to allocate NPA WQ\n");
		r = -ENOMEM;
		goto uninit_vm;
	}

	/* Map this VM to NPA VMID */
	adev->mmhub.funcs->setup_vm_pt_regs(adev, adev->vm_manager.npa_vmid,
			amdgpu_gmc_pd_addr(adev->ualink.npa_vm.root.bo));

	xa_init(&adev->ualink.exp_xa);
	xa_init(&adev->ualink.imp_xa);
	xa_init(&adev->ualink.handle_invalid_xa);

	for (i = 0; i < AMDGPU_UALINK_ACCEL_MAX; i++) {
		init_completion(&adev->ualink.conn_state[i].hello_done);
		mutex_init(&adev->ualink.conn_state[i].lock);
		adev->ualink.conn_state[i].state = AMDGPU_UALINK_CONN_NOT_READY;
		INIT_LIST_HEAD(&adev->ualink.exp_handles_list[i]);
		INIT_LIST_HEAD(&adev->ualink.imp_handles_list[i]);
	}

	amdgpu_ualink_npa_mm_init(adev);

	return 0;

uninit_vm:
	amdgpu_vm_fini(adev, &adev->ualink.npa_vm);
out:
	return r;
}

/* Free every importer entry left in imp_xa at manager stop. */
static void amdgpu_ualink_teardown_imp_xa_entries(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_imp_xa_node *imp_xa_node;
	unsigned long index;

	xa_for_each(&adev->ualink.imp_xa, index, imp_xa_node) {
		list_del_init(&imp_xa_node->list);
		WRITE_ONCE(imp_xa_node->node_state, AMDGPU_UALINK_NODE_TEARDOWN);
		amdgpu_ualink_release_imp_xa_node(adev, imp_xa_node);
	}
}

/* Free a single exporter node at manager stop. This is the message-free
 * portion of amdgpu_ualink_exp_cleanup_worker().
 */
static void amdgpu_ualink_teardown_exp_xa_node(struct amdgpu_device *adev,
				struct amdgpu_ualink_exp_xa_node *exp_xa_node)
{
	DECLARE_BITMAP(importers_bitmap, AMDGPU_UALINK_ACCEL_MAX);
	struct amdgpu_bo *bo = exp_xa_node->bo;
	u32 remote_acc_id;

	bitmap_copy(importers_bitmap, exp_xa_node->importers_bitmap,
		    AMDGPU_UALINK_ACCEL_MAX);

	dev_dbg(adev->dev,
		"EXP-STOP: handle:%llx:%llx importers bitmap: %*pbl\n",
		exp_xa_node->handle.handle_hi, exp_xa_node->handle.handle_lo,
		AMDGPU_UALINK_ACCEL_MAX, importers_bitmap);

	if (!bitmap_empty(importers_bitmap, AMDGPU_UALINK_ACCEL_MAX)) {
		amdgpu_ualink_unmap_all_npa_addr(adev, exp_xa_node);

		/* Unlink importers and unpin the BO once per importer. */
		if (likely(!amdgpu_bo_reserve(bo, true))) {
			bo->ualink_handle_lo = 0ULL;
			for_each_set_bit(remote_acc_id, importers_bitmap,
					 AMDGPU_UALINK_ACCEL_MAX) {
				list_del_init(&exp_xa_node->importer_entries[remote_acc_id].list);
				amdgpu_bo_unpin(bo);
			}
			amdgpu_bo_unreserve(bo);
		} else {
			dev_warn(adev->dev,
				"EXP-STOP: BO reserve to unpin failed handle:%llx:%llx\n",
				exp_xa_node->handle.handle_hi,
				exp_xa_node->handle.handle_lo);
			for_each_set_bit(remote_acc_id, importers_bitmap,
					 AMDGPU_UALINK_ACCEL_MAX)
				list_del_init(&exp_xa_node->importer_entries[remote_acc_id].list);
		}

		amdgpu_ualink_free_all_npa_va(adev, exp_xa_node, importers_bitmap);
	}

	/* Release the export dma-buf and drop the BO ref. */
	dma_buf_put(exp_xa_node->dmabuf);
	amdgpu_bo_unref(&bo);
	exp_xa_node->bo = NULL;

	mutex_destroy(&exp_xa_node->node_lock);
	kfree(exp_xa_node);
}

/* Free every exporter entry left in exp_xa at manager stop. These were
 * exported but never revoked, so no cleanup worker was queued for them.
 */
static void amdgpu_ualink_teardown_exp_xa_entries(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_exp_xa_node *exp_xa_node;
	unsigned long index;

	xa_for_each(&adev->ualink.exp_xa, index, exp_xa_node) {
		xa_erase(&adev->ualink.exp_xa, index);
		amdgpu_ualink_teardown_exp_xa_node(adev, exp_xa_node);
	}
}

void amdgpu_ualink_manager_stop(struct amdgpu_device *adev)
{
	int i;

	adev->mmhub.funcs->setup_vm_pt_regs(adev, adev->vm_manager.npa_vmid, 0);

	/* Mark connections down so the drained workers and the teardown below
	 * take the message-free path in amdgpu_ualink_check_conn_ready().
	 */
	for (i = 0; i < AMDGPU_UALINK_ACCEL_MAX; i++) {
		mutex_lock(&adev->ualink.conn_state[i].lock);
		adev->ualink.conn_state[i].state = AMDGPU_UALINK_CONN_NOT_READY;
		mutex_unlock(&adev->ualink.conn_state[i].lock);
	}

	/* Drain in-flight exporter cleanup work so revoked nodes in
	 * handle_invalid_xa free themselves and empty that xarray.
	 */
	drain_workqueue(adev->ualink.npa_wq);

	/* Free live entries before the NPA allocator is torn down, so NPA
	 * addresses can still be unmapped and freed.
	 */
	amdgpu_ualink_teardown_imp_xa_entries(adev);
	amdgpu_ualink_teardown_exp_xa_entries(adev);

	amdgpu_ualink_npa_mm_fini(adev);

	/* All three xarrays are empty by now. */
	xa_destroy(&adev->ualink.exp_xa);
	xa_destroy(&adev->ualink.imp_xa);
	xa_destroy(&adev->ualink.handle_invalid_xa);

	for (i = 0; i < AMDGPU_UALINK_ACCEL_MAX; i++)
		mutex_destroy(&adev->ualink.conn_state[i].lock);

	destroy_workqueue(adev->ualink.npa_wq);
	amdgpu_vm_fini(adev, &adev->ualink.npa_vm);
}

/*
 * UALink remote interrupt and shootdown
 */

/* UALINK ring buffer size, same for both remote shootdown and interrupt ring */
#define AMDGPU_UALINK_RB_SIZE	4096

#define AMDGPU_UALINK_METADATA_HEADER	0x4E485446

/* 2MB NPA start address for 2MB page mapping */
#define AMDGPU_UALINK_SOURCE_ALIAS_NPA_OFFSET SZ_2M

struct amdgpu_ualink_metadata {
	u32 header;

	/*
	 * "ring entries" as unit. So the ring-size-in-bytes could be calculated
	 * as entry-size * 2^RBsize. This would make the minimum size a single
	 * entry and the maximum size 32786 entries.
	 *
	 * u32 rb_size:4;
	 * u32 reserved0:4;
	 * u32 vmid:4;
	 * u32 reserved1:20;
	 */
	u32 rb_size;

	/* ring buffer base address for remote interrupt */
	u64 ri_rb;

	/* tlb invalidate ring buffer base address for remote shootdown */
	u64 tlb_inv_rb;

	/* remote interrupt Tail pointer, write pointer address */
	u64 tailptr_ri;

	/* TLB invalidate ring buffer's Tail pointer, write pointer address */
	u64 tailptr_tlb_inv;
};

struct amdgpu_ualink_wb {
	/* last finished command seq number */
	u32 data;

	/* command complete error code */
	u32 status;

	/* ring rptr updated by FW */
	u64 rptr;
};

/*
 * For remote interrupt and shootdown
 */
struct amdgpu_ualink_ring {
	u32 rb_size;

	/* writeback data */
	u32 seq;

	/* local copy */
	u64 wptr, rptr;

	/* NPA gart mapping for SDMA */
	u64 rb_npa_gart;
	u64 wptr_npa_gart;
	u64 doorbell_npa_gart;

	/* gart mapping node */
	struct drm_mm_node mm_node_rb;
	struct drm_mm_node mm_node_wptr;
	struct drm_mm_node mm_node_doorbell;

	/* seq, complete status write back cpu address */
	struct amdgpu_ualink_wb *wb_cpu;

	/* true if fw write back address updated successfully */
	bool ready;
};

struct amdgpu_ualink_peer {
	/* remote interrupt and shootdown uses same SDMA entity */
	struct mutex lock;

	/* SDMA engine to send remote command via NPA */
	struct drm_sched_entity entity;

	/* to select different DXS ports, cycles through different values */
	u32 dxs_port;

	struct amdgpu_ualink_ring interrupt;
	struct amdgpu_ualink_ring shootdown;
};

struct amdgpu_ualink_remote {
	/* ualink metadata passed to MPNHT FW */
	struct amdgpu_bo		*metadata_bo;
	u64				metadata_gpu_addr;
	void				*metadata_cpu_addr;

	/* ualink ring, tlb ring buffer, wptr */
	struct amdgpu_bo		*ring_bo;
	u64				rb_gpu_addr;
	void				*rb_cpu_addr;

	/* ualink rptr, wb data, statuss for address alias mode */
	struct amdgpu_bo		*rptr_bo;
	u64				rptr_gpu_addr;
	void				*rptr_cpu_addr;
	u64				rptr_npa;

	/* address alias mode alloc npa address for shared wb */
	struct drm_mm_node		rptr_mm_node;

	/*
	 * Owned snapshot of the vPod's active accelerator bitmap, taken at
	 * sw_init time. Must NOT alias info->vpod.active_accel_bits, which
	 * psp_ual_query_info() refreshes on every commit: aliasing it would let
	 * teardown unmap a different set than setup mapped, leaking drm_mm nodes
	 * in the shared GTT manager.
	 */
	DECLARE_BITMAP(active_accel_bits, AMDGPU_UALINK_ACCEL_MAX);
	u32				num_accel;

	/* remote GPUs ring buffer, read, write pointer local copy and gart mapping */
	struct amdgpu_ualink_peer	peer[AMDGPU_UALINK_ACCEL_MAX];

	/* use lsdma write to NPA address for remote interrupt */
	bool			       use_lsdma;
};

static inline struct amdgpu_ualink_remote *to_remote(struct amdgpu_device *adev)
{
	return adev->ualink.remote;
}

/*
 * Returns true if the vPod membership most recently reported by firmware
 * (info->vpod.active_accel_bits, read back via psp_ual_query_info() on the
 * committing GPU) differs from the owned snapshot captured when this
 * accelerator was last activated (remote->active_accel_bits). Used to
 * decide whether an accelerator must be (re)built to match the new member
 * set. A NULL remote means the accelerator has never been activated (no
 * snapshot yet), which also counts as "changed" so it gets brought up.
 */
static bool amdgpu_ualink_vpod_membership_changed(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);

	if (!remote)
		return true;

	return !bitmap_equal(remote->active_accel_bits,
			     adev->ualink.info->vpod.active_accel_bits,
			     AMDGPU_UALINK_ACCEL_MAX);
}

static inline u32 ualink_accel_id(struct amdgpu_device *adev)
{
	return adev->ualink.info->ppod.accel_id;
}

static inline enum amdgpu_ualink_addr_mode ualink_addr_mode(struct amdgpu_device *adev)
{
	return adev->ualink.info->vpod.addr_mode;
}

/*
 * address mode and NPA address for ring wptr, rptr, wb status, wb data
 *
 * source identification address mode
 *
 * we need to export different tail pointers for different remote GPU accel_id,
 * hence we need to allocate a whole page for each remote GPU separately.
 *
 * rptr, wb data, status share the whole page with wptr.
 *
 *     1 page reserved NPA address for 1 active remote GPU, max 255 pages
 *     starting address: wptr NPA address + accel_id * 4K
 *     offset:
 *         remote interrupt ring wptr 0
 *         remote shootdown ring wptr 8
 *         remote interrupt wb status 16
 *         remote interrupt wb data   20
 *         remote interrupt ring rptr 24
 *         remote shootdown wb status 32
 *         remote shootdown wb data   36
 *         remote shootdown ring rptr 40
 *
 * source aliasing address mode
 *
 * wptr for different remote GPU can share same reserved 1 page NPA address page.
 *
 * to save reserved NPA address space, rptr, wb data, status for different
 * remote GPU share same allocated 2 pages NPA address.
 *
 *     1 page reserved NPA address for wptr of all active remote GPUs
 *     offset:
 *         remote interrupt ring wptr accel_id * 16
 *         remote shootdown ring wptr accel_id * 16 + 8
 *
 *     2 page allocated NPA address for rptr, writeback of all active remote GPUs
 *     starting address: at accel_id * 32 for each remote GPU
 *     offset:
 *         remote interrupt wb status 0
 *         remote interrupt wb data   4
 *         remote interrupt ring rptr 8
 *         remote shootdown wb status 16
 *         remote shootdown wb data   20
 *         remote shootdown ring rptr 24
 */
static inline u32 ualink_wptr_size(struct amdgpu_device *adev)
{
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT)
		return AMDGPU_GPU_PAGE_SIZE;
	else
		return 0;	/* wptr uses the ring buffer for itself */
}

static inline u32 ualink_wb_size(struct amdgpu_device *adev)
{
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT)
		return 0;	/* share same page with wptr */
	else
		return 2 * sizeof(struct amdgpu_ualink_wb);
}

static inline u32 ualink_wptr_offset(struct amdgpu_device *adev, u32 accel_id)
{
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT)
		return 0;	/* one separate page per GPU */
	else
		return accel_id * 2 * sizeof(u64);	/* two pointers per GPU */
}

static inline u32 ualink_tlb_wptr_offset(struct amdgpu_device *adev, u32 accel_id)
{
	return ualink_wptr_offset(adev, accel_id) + sizeof(u64);
}

static inline u32 ualink_wb_offset(struct amdgpu_device *adev, u32 accel_id)
{
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT)
		return 2 * sizeof(u64);	/* after two wptr pointers */
	else
		return accel_id * ualink_wb_size(adev);
}

static inline u32 ualink_tlb_wb_offset(struct amdgpu_device *adev, u32 accel_id)
{
	return ualink_wb_offset(adev, accel_id) + sizeof(struct amdgpu_ualink_wb);
}

static void amdgpu_ualink_flush_tlb(struct amdgpu_device *adev, u32 flush_type)
{
	u32 bit;

	bit = AMDGPU_MMHUB0_START;

	for_each_set_bit_from(bit, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS)
		amdgpu_gmc_flush_gpu_tlb(adev, adev->vm_manager.npa_vmid,
					bit, flush_type);
}

/**
 * amdgpu_ualink_npa_vm_map_range - Map a range in the NPA VM
 * @adev: amdgpu device pointer
 * @bo: buffer object backing the mapping
 * @pte_flags: page table entry flags
 * @offset: offset into the buffer object in bytes
 * @size_in_pages: size of the range to map in pages
 * @npa_in_pages: NPA target address in pages
 *
 * Maps a buffer object range into the NPA VM page table at the specified
 * NPA address.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_npa_vm_map_range(struct amdgpu_device *adev, struct amdgpu_bo *bo,
				   u64 pte_flags, u64 offset, u64 size_in_pages,
				   u64 npa_in_pages)
{
	struct amdgpu_vm *npa_vm = &adev->ualink.npa_vm;
	int r;

	dev_dbg(adev->dev, "offset 0x%llx size 0x%llx flags 0x%llx npa 0x%llx\n",
		offset, size_in_pages << AMDGPU_GPU_PAGE_SHIFT, pte_flags,
		npa_in_pages << AMDGPU_GPU_PAGE_SHIFT);

	r = amdgpu_vm_update_range(adev, npa_vm, false, false, true,
				   false, NULL, npa_in_pages,
				   npa_in_pages + size_in_pages - 1,
				   pte_flags, offset, adev->vm_manager.vram_base_offset,
				   bo->tbo.resource, NULL, &npa_vm->last_update);
	if (r)
		dev_dbg(adev->dev, "failed %d to map npa 0x%llx to NPA VM\n", r,
			npa_in_pages << AMDGPU_GPU_PAGE_SHIFT);
	return r;
}

/**
 * amdgpu_npa_vm_unmap_range - Unmap a range from the NPA VM
 * @adev: amdgpu device pointer
 * @bo: buffer object backing the mapping
 * @pte_flags: page table entry flags
 * @offset: offset into the buffer object in bytes
 * @size_in_pages: size of the range to unmap in pages
 * @npa_in_pages: NPA target address in pages
 *
 * Removes a previously established mapping from the NPA VM page table.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_npa_vm_unmap_range(struct amdgpu_device *adev, struct amdgpu_bo *bo,
				     u64 pte_flags, u64 offset, u64 size_in_pages,
				     u64 npa_in_pages)
{
	struct amdgpu_vm *npa_vm = &adev->ualink.npa_vm;
	int r;

	dev_dbg(adev->dev, "offset 0x%llx size 0x%llx flags 0x%llx npa 0x%llx\n",
		offset, size_in_pages << AMDGPU_GPU_PAGE_SHIFT, pte_flags,
		npa_in_pages << AMDGPU_GPU_PAGE_SHIFT);

	r = amdgpu_vm_update_range(adev, npa_vm, false, false, true,
				   false, NULL, npa_in_pages,
				   npa_in_pages + size_in_pages - 1,
				   pte_flags, offset, 0, bo->tbo.resource, NULL,
				   &npa_vm->last_update);
	if (r)
		dev_dbg(adev->dev, "failed %d to unmap npa 0x%llx from NPA VM\n", r,
			npa_in_pages << AMDGPU_GPU_PAGE_SHIFT);
	return r;
}

/*
 * Reserved NPA space for remote shootdown and interrupt ring buffer,
 * write pointers, read pointers and writeback buffers
 *
 * NPA ring type: 0 = shootdown, 1 = interrupt, 2 = tailptr (wptr)
 */
enum ring_buffer_type {
	RB_TYPE_TLB_INV			= 0,
	RB_TYPE_REMOTE_INTERRUPT	= 1,
	RB_TYPE_TAILPTR			= 2
};

/**
 * amdgpu_ualink_npa_addr - Get reserved NPA address for ring buffer
 * @adev: amdgpu device pointer
 * @type: Ring buffer type (TLB_INV, REMOTE_INTERRUPT, or TAILPTR)
 * @src_accel_id: Source accelerator ID
 * @dst_accel_id: Destination accelerator ID
 *
 * Return: NPA address for the specified ring buffer type and GPUs
 */
static u64 amdgpu_ualink_npa_addr(struct amdgpu_device *adev, u32 type,
				  u32 src_accel_id, u32 dst_accel_id)
{
	u32 addr_mode = ualink_addr_mode(adev);
	u64 npa;

	WARN_ON_ONCE(src_accel_id >= AMDGPU_UALINK_ACCEL_MAX ||
		     dst_accel_id >= AMDGPU_UALINK_ACCEL_MAX ||
		     type > RB_TYPE_TAILPTR ||
		     (addr_mode != AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT &&
		     addr_mode != AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS));

	switch (addr_mode) {
	case AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT:
		npa = (u64)src_accel_id << 41 | type << 12;
		break;

	case AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS:
	default:
		if (type == RB_TYPE_TAILPTR)
			npa = (u64)dst_accel_id << 13;
		else
			npa = (u64)src_accel_id << 13 | type << 12;

		/*
		 * In order to avoid address conflicts between source-identification
		 * and source-aliasing mode, add 2MB to the buffer addresses in
		 * source-aliasing mode. This way in a misconfigured cluster, a GPU
		 * configured in the wrong address mode will access unmapped NPA addresses
		 * rather than the wrong buffer mappings.
		 */
		npa += AMDGPU_UALINK_SOURCE_ALIAS_NPA_OFFSET;
		break;
	}

	dev_dbg(adev->dev, "addr mode %d from accel %d to accel %d type %d NPA 0x%llx\n",
		addr_mode, src_accel_id, dst_accel_id, type, npa);

	return npa;
}

/**
 * amdgpu_ualink_gart_npa_addr - Get GART-mapped NPA address for ring buffer
 * @adev: amdgpu device pointer
 * @type: Ring buffer type (TLB_INV, REMOTE_INTERRUPT, or TAILPTR)
 * @src_accel_id: Source accelerator ID
 * @dst_accel_id: Destination accelerator ID
 *
 * Computes the NPA address suitable for GART mapping by adding dst_accid_id to
 * NPA address to access remote by SDMA.

 * clears the source GPU ID bits from the NPA address which is set for
 * address-identification mode.
 *
 * Return: NPA address adjusted for GART mapping
 */

#define AMDGPU_UALINK_GART_NPA_ADDR_GPUID_SHIFT		41
#define AMDGPU_UALINK_GART_NPA_ADDR_GPUID_MASK		GENMASK_ULL(50, 41)

static u64 amdgpu_ualink_gart_npa_addr(struct amdgpu_device *adev, u32 type,
					u32 src_accel_id, u32 dst_accel_id)
{
	u64 npa;

	npa = amdgpu_ualink_npa_addr(adev, type, src_accel_id, dst_accel_id);

	/* wiping out source accelerator id */
	npa &= ~AMDGPU_UALINK_GART_NPA_ADDR_GPUID_MASK;

	return npa | ((u64)dst_accel_id << AMDGPU_UALINK_GART_NPA_ADDR_GPUID_SHIFT);
}

/**
 * amdgpu_ualink_reserve_npa_vm_and_bos - Reserve the NPA VM page directory and BOs
 * @adev: amdgpu device pointer
 * @bos: array of buffer objects to reserve
 * @n_bos: number of entries in @bos
 * @exec: drm_exec context to initialize and use for locking
 * @interruptible: true to use the interruptible dma_resv_lock
 *
 * Initializes @exec and uses it to lock all BOs in @bos together with the
 * NPA VM page directory, retrying on contention. On failure, @exec is
 * finalized and the error code is returned.
 *
 * Returns: 0 on success, negative error code on failure.
 */
static int amdgpu_ualink_reserve_npa_vm_and_bos(struct amdgpu_device *adev,
						struct amdgpu_bo *bos[], u32 n_bos,
						struct drm_exec *exec,
						bool interruptible)
{
	u32 flags = DRM_EXEC_IGNORE_DUPLICATES;
	int i, r = 0;

	if (interruptible)
		flags |= DRM_EXEC_INTERRUPTIBLE_WAIT;

	dev_dbg(adev->dev, "reserve NPA vm and %d bos\n", n_bos);

	drm_exec_init(exec, flags, 0);

	drm_exec_until_all_locked(exec) {
		for (i = 0; i < n_bos; i++) {
			r = drm_exec_lock_obj(exec, &bos[i]->tbo.base);
			drm_exec_retry_on_contention(exec);
			if (unlikely(r))
				goto out;
		}

		r = amdgpu_vm_lock_pd(&adev->ualink.npa_vm, exec, 0);
		drm_exec_retry_on_contention(exec);
		if (unlikely(r))
			goto out;
	}

out:
	if (r)
		drm_exec_fini(exec);
	return r;

}

/**
 * amdgpu_ualink_unreserve_npa_vm_and_bos - Release the NPA VM page directory and BOs
 * @adev: amdgpu device pointer
 * @exec: drm_exec context previously initialized by
 *        amdgpu_ualink_reserve_npa_vm_and_bos()
 *
 * Finalizes @exec, releasing all locks on the NPA VM page directory and
 * the associated BOs acquired during reservation.
 */
static void amdgpu_ualink_unreserve_npa_vm_and_bos(struct amdgpu_device *adev,
						   struct drm_exec *exec)
{
	dev_dbg(adev->dev, "unreserve NPA vm and bos\n");
	drm_exec_fini(exec);
}

/**
 * amdgpu_ualink_metadata_npa_unmapping - Tear down NPA address mappings
 * @adev: amdgpu device pointer
 *
 * Unmaps all NPA address mappings from the NPA VM for ring buffers,
 * write pointers, and read pointers. Waits for outstanding DMA fences
 * and flushes the TLB.
 */
static void amdgpu_ualink_metadata_npa_unmapping(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	u64 timeout = msecs_to_jiffies(2000);
	u32 dst_accel_id = ualink_accel_id(adev);
	struct amdgpu_bo *bos[2];
	u32 n_bos;
	struct dma_fence *fence;
	struct drm_exec exec;
	u32 rptr_size, rptr_size_in_pages;
	u32 rb_size, rb_size_in_pages;
	u64 pte_flags = adev->gmc.noretry_flags;
	u64 npa;
	int r;

	if (!remote->ring_bo)
		return;

	rb_size = AMDGPU_UALINK_RB_SIZE;
	rb_size_in_pages = rb_size >> AMDGPU_GPU_PAGE_SHIFT;

	bos[0] = remote->ring_bo;
	n_bos = 1;
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS) {
		bos[1] = remote->rptr_bo;
		n_bos = 2;
	}

	r = amdgpu_ualink_reserve_npa_vm_and_bos(adev, bos, n_bos, &exec, false);
	if (unlikely(r))
		return;

	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS) {
		/* rptr npa mapping, up to allocated 2 pages npa address */
		rptr_size = ualink_wb_size(adev);
		rptr_size = AMDGPU_GPU_PAGE_ALIGN(rptr_size * remote->num_accel);
		rptr_size_in_pages = rptr_size >> AMDGPU_GPU_PAGE_SHIFT;

		npa = remote->rptr_npa;

		amdgpu_ualink_npa_vm_unmap_range(adev, remote->rptr_bo,
						 pte_flags, 0, rptr_size_in_pages,
						 npa >> AMDGPU_GPU_PAGE_SHIFT);
		amdgpu_ualink_npa_free_va(adev, &remote->rptr_mm_node);

		/*
		 * unmap wptr, remote interrupt, shootdown ring.
		 * wptr page is part of the single 2MB mapping for remote GPUs
		 * interrupt and shootdown ring buffer
		 */
		npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_TLB_INV,
					     0, dst_accel_id);
		amdgpu_ualink_npa_vm_unmap_range(adev, remote->ring_bo, pte_flags,
						 0,
						 2 * rb_size_in_pages * AMDGPU_UALINK_ACCEL_MAX,
						 npa >> AMDGPU_GPU_PAGE_SHIFT);
	} else {
		u32 wptr_offset = 2 * rb_size * remote->num_accel;
		u32 idx = 0;
		u32 accel_id;

		/* source identification mode */
		for_each_set_bit(accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX) {
			if (accel_id == dst_accel_id)
				continue;

			/* remote interrupt ring npa mapping */
			npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_REMOTE_INTERRUPT,
						     accel_id, dst_accel_id);
			amdgpu_ualink_npa_vm_unmap_range(adev, remote->ring_bo,
							 pte_flags, idx * 2 * rb_size,
							 rb_size_in_pages,
							 npa >> AMDGPU_GPU_PAGE_SHIFT);

			/* remote shootdown ring npa mapping */
			npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_TLB_INV, accel_id,
						     dst_accel_id);
			amdgpu_ualink_npa_vm_unmap_range(adev, remote->ring_bo,
							 pte_flags, (idx * 2 + 1) * rb_size,
							 rb_size_in_pages,
							 npa >> AMDGPU_GPU_PAGE_SHIFT);

			/* wptr, rptr npa mapping */
			npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_TAILPTR, accel_id,
						      dst_accel_id);
			amdgpu_ualink_npa_vm_unmap_range(adev, remote->ring_bo,
						  pte_flags, wptr_offset + idx * PAGE_SIZE,
						  1, npa >> AMDGPU_GPU_PAGE_SHIFT);
			idx++;
		}
	}

	r = amdgpu_vm_update_pdes(adev, &adev->ualink.npa_vm, false);
	if (r) {
		dev_dbg(adev->dev, "failed %d to update directories\n", r);
		goto out_unreserve;
	}

	fence = dma_fence_get(adev->ualink.npa_vm.last_update);
	if (fence) {
		r = dma_fence_wait_timeout(fence, true, timeout);
		dma_fence_put(fence);
		if (r <= 0)
			dev_dbg(adev->dev, "failed %d to dma fence wait\n", r);
	}

	amdgpu_ualink_flush_tlb(adev, TLB_FLUSH_HEAVYWEIGHT);
out_unreserve:
	amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);
}

/**
 * amdgpu_ualink_metadata_npa_mapping - Setup NPA address mapping in VM
 * @adev: amdgpu device pointer
 *
 * Maps NPA addresses to GPA for metadata ring buffers, write pointers,
 * on NPA VMID 15.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_metadata_npa_mapping(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	u32 dst_accel_id = ualink_accel_id(adev);
	u64 timeout = msecs_to_jiffies(2000);
	struct amdgpu_bo *bos[2];
	u32 n_bos;
	struct dma_fence *fence;
	struct drm_exec exec;
	u32 rb_size, rb_size_in_pages;
	u64 npa, npa_in_pages, pte_flags;
	int r;

	rb_size = AMDGPU_UALINK_RB_SIZE;
	rb_size_in_pages = rb_size >> AMDGPU_GPU_PAGE_SHIFT;

	bos[0] = remote->ring_bo;
	n_bos = 1;
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS) {
		bos[1] = remote->rptr_bo;
		n_bos = 2;
	}

	r = amdgpu_ualink_reserve_npa_vm_and_bos(adev, bos, n_bos, &exec, false);
	if (unlikely(r))
		return r;

	pte_flags = amdgpu_ttm_tt_pte_flags(adev, remote->ring_bo->tbo.ttm,
					    remote->ring_bo->tbo.resource);
	dev_dbg(adev->dev, "init pte_flags 0x%llx\n", pte_flags);

	amdgpu_gmc_get_vm_pte(adev, &adev->ualink.npa_vm, remote->ring_bo,
			      AMDGPU_VM_MTYPE_DEFAULT, &pte_flags);
	dev_dbg(adev->dev, "after get coherent pte_flags 0x%llx\n", pte_flags);

	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS) {
		u32 rptr_size, rptr_size_in_pages;

		/* rptr npa mapping, up to allocated 2 pages npa address */
		rptr_size = ualink_wb_size(adev);
		rptr_size = AMDGPU_GPU_PAGE_ALIGN(rptr_size * remote->num_accel);
		rptr_size_in_pages = rptr_size >> AMDGPU_GPU_PAGE_SHIFT;

		r = amdgpu_ualink_npa_alloc_va(adev, &remote->rptr_mm_node,
					       0, 0, 0,
					       rptr_size_in_pages);
		if (r)
			goto out;

		npa_in_pages = remote->rptr_mm_node.start;

		dev_dbg(adev->dev, "source aliasing rptr alloc 0x%llx and map to npa vm\n",
			npa_in_pages << AMDGPU_GPU_PAGE_SHIFT);

		r = amdgpu_ualink_npa_vm_map_range(adev, remote->rptr_bo, pte_flags, 0,
						   rptr_size_in_pages,
						   npa_in_pages);
		if (r)
			goto error_npa_mapping;

		remote->rptr_npa = npa_in_pages << AMDGPU_GPU_PAGE_SHIFT;

		/* wptr, remote interrupt, shootdown ring, single big 2MB mapping */
		npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_TLB_INV,
					     0, dst_accel_id);
		r = amdgpu_ualink_npa_vm_map_range(adev, remote->ring_bo, pte_flags,
						   0,
						   2 * rb_size_in_pages * AMDGPU_UALINK_ACCEL_MAX,
						   npa >> AMDGPU_GPU_PAGE_SHIFT);
		if (r)
			goto error_npa_mapping;

	} else {
		u32 wptr_offset = 2 * rb_size * remote->num_accel;
		u32 accel_id, idx = 0;

		/* Source identification mode */
		for_each_set_bit(accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX) {
			if (accel_id == dst_accel_id)
				continue;

			/* remote interrupt ring npa mapping */
			npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_REMOTE_INTERRUPT,
						      accel_id, dst_accel_id);
			r = amdgpu_ualink_npa_vm_map_range(adev, remote->ring_bo, pte_flags,
							   idx * 2 * rb_size,
							   rb_size_in_pages,
							   npa >> AMDGPU_GPU_PAGE_SHIFT);
			if (r)
				goto error_npa_mapping;

			/* remote shootdown ring npa mapping */
			npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_TLB_INV, accel_id,
						     dst_accel_id);
			r = amdgpu_ualink_npa_vm_map_range(adev, remote->ring_bo, pte_flags,
							   (idx * 2 + 1) * rb_size,
							   rb_size_in_pages,
							   npa >> AMDGPU_GPU_PAGE_SHIFT);
			if (r)
				goto error_npa_mapping;

			/* wptr, rptr npa mapping */
			npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_TAILPTR, accel_id,
						     dst_accel_id);
			r = amdgpu_ualink_npa_vm_map_range(adev, remote->ring_bo,
							   pte_flags,
							   wptr_offset + idx * PAGE_SIZE,
							   1, npa >> AMDGPU_GPU_PAGE_SHIFT);
			if (r)
				goto error_npa_mapping;

			idx++;
		}
	}

	r = amdgpu_vm_update_pdes(adev, &adev->ualink.npa_vm, false);
	if (r) {
		dev_dbg(adev->dev, "failed %d to update directories\n", r);
		goto error_npa_mapping;
	}

	/* TODO: only wait the last fence, then flush TLB */
	fence = dma_fence_get(adev->ualink.npa_vm.last_update);
	if (fence) {
		r = dma_fence_wait_timeout(fence, true, timeout);
		dma_fence_put(fence);
		if (r <= 0)
			dev_dbg(adev->dev, "failed %d to dma fence wait\n", r);
	}

	amdgpu_ualink_flush_tlb(adev, TLB_FLUSH_HEAVYWEIGHT);

	amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);
	return 0;

error_npa_mapping:
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
		amdgpu_ualink_npa_free_va(adev, &remote->rptr_mm_node);
	if (r)
		dev_dbg(adev->dev, "failed %d to map NPA vm\n", r);

out:
	amdgpu_ualink_unreserve_npa_vm_and_bos(adev, &exec);

	return r;
}

/**
 * amdgpu_ualink_sdma_entities_init - Initialize SDMA job scheduler entities
 * @adev: amdgpu device pointer
 *
 * Sets up SDMA job scheduler entities for remote interrupt and shootdown.
 * Each entity uses one SDMA scheduler/ring.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_sdma_entities_init(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct drm_gpu_scheduler *sched;
	u32 accel_id;
	int i = 0, r;

	dev_dbg(adev->dev, "enter\n");

	for_each_set_bit(accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX) {
		sched = &adev->sdma.instance[accel_id % adev->sdma.num_instances].ring.sched;

		r = drm_sched_entity_init(&remote->peer[accel_id].entity, DRM_SCHED_PRIORITY_HIGH,
					  &sched, 1, NULL);
		if (r)
			goto out_free;
		i++;
	}

	remote->use_lsdma = true;

	dev_dbg(adev->dev, "exit\n");
	return 0;

out_free:
	for_each_set_bit(accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX) {
		if (i--)
			drm_sched_entity_destroy(&remote->peer[accel_id].entity);
		else
			break;
	}

	return r;
}

static void amdgpu_ualink_sdma_entities_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	u32 accel_id;

	dev_dbg(adev->dev, "enter\n");
	for_each_set_bit(accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX)
		drm_sched_entity_destroy(&remote->peer[accel_id].entity);
	dev_dbg(adev->dev, "exit\n");
}

/**
 * amdgpu_ualink_gart_map - Allocate GART entry and map NPA address
 * @adev: amdgpu device pointer
 * @npages: Number of pages to map
 * @npa: NPA address to map
 * @mm_node: DRM memory manager node for GART allocation
 * @pte_flags: the GART mapping flags
 *
 * Allocates GART entries and sets up NPA address mapping without TTM BO.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_gart_map(struct amdgpu_device *adev, u64 npages,
				   u64 npa, struct drm_mm_node *mm_node,
				   u64 pte_flags)
{
	struct ttm_resource_manager *man =
				ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);
	struct amdgpu_gtt_mgr *mgr =
				container_of(man, struct amdgpu_gtt_mgr, manager);
	dma_addr_t *dma_addr;
	int i, r;

	dev_dbg(adev->dev, "npa 0x%llx npages 0x%llx\n", npa, npages);

	if (npages > 1) {
		dma_addr = kmalloc_array(npages, sizeof(*dma_addr), GFP_KERNEL);
		if (!dma_addr)
			return -ENOMEM;

		for (i = 0; i < npages; i++)
			dma_addr[i] = npa + i * AMDGPU_GPU_PAGE_SIZE;
	} else {
		dma_addr = (dma_addr_t *)&npa;
	}

	r = amdgpu_gtt_mgr_alloc_entries(mgr, mm_node, npages, DRM_MM_INSERT_BEST);
	if (r)
		goto out;
	amdgpu_gart_bind(adev, mm_node->start << AMDGPU_GPU_PAGE_SHIFT, npages, dma_addr,
			 pte_flags);

out:
	dev_dbg(adev->dev, "npa 0x%llx mapped to 0x%llx npages 0x%llx r=%d\n",
		npa, mm_node->start << AMDGPU_GPU_PAGE_SHIFT, npages, r);

	if (npages > 1)
		kfree(dma_addr);
	return r;
}

/**
 * amdgpu_ualink_gart_unmap - Unmap and free GART entry
 * @adev: amdgpu device pointer
 * @npages: Number of pages to unmap
 * @mm_node: DRM memory manager node for GART allocation
 *
 * Unbinds GART mapping and frees allocated entries.
 */
static void amdgpu_ualink_gart_unmap(struct amdgpu_device *adev, u64 npages,
				      struct drm_mm_node *mm_node)
{
	struct ttm_resource_manager *man = ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);
	struct amdgpu_gtt_mgr *mgr =
				container_of(man, struct amdgpu_gtt_mgr, manager);

	dev_dbg(adev->dev, "0x%llx npages 0x%llx\n", mm_node->start << AMDGPU_GPU_PAGE_SHIFT,
		npages);

	if (!drm_mm_node_allocated(mm_node))
		return;
	amdgpu_gart_unbind(adev, mm_node->start << AMDGPU_GPU_PAGE_SHIFT, npages);
	amdgpu_gtt_mgr_free_entries(mgr, mm_node);
}

/**
 * amdgpu_ualink_metadata_fini - Clean up ualink metadata structures
 * @adev: amdgpu device pointer
 *
 * Unmaps NPA addresses and frees all allocated buffers for metadata,
 * ring buffers, and pointers.
 */
static void amdgpu_ualink_metadata_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);

	dev_dbg(adev->dev, "accel_id %u\n", ualink_accel_id(adev));
	amdgpu_bo_free_kernel(&remote->rptr_bo, &remote->rptr_gpu_addr,
				      &remote->rptr_cpu_addr);
	amdgpu_bo_free_kernel(&remote->ring_bo, &remote->rb_gpu_addr,
			      &remote->rb_cpu_addr);
	amdgpu_bo_free_kernel(&remote->metadata_bo, &remote->metadata_gpu_addr,
			      &remote->metadata_cpu_addr);
}

/**
 * amdgpu_ualink_metadata_init - Initialize ualink metadata structures
 * @adev: amdgpu device pointer
 *
 * Allocates and initializes metadata structures, ring buffers, and write/read
 * pointers for multi-GPU ualink. Communicates with firmware to load metadata.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_metadata_init(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct amdgpu_ualink_metadata *metadata;
	u32 size, rb_size, wptr_size, rptr_size, metadata_size;
	u64 rb_gpu_addr, wptr_gpu_addr;
	u32 status, accel_id;
	int r;

	bitmap_copy(remote->active_accel_bits,
		    adev->ualink.info->vpod.active_accel_bits,
		    AMDGPU_UALINK_ACCEL_MAX);
	dev_dbg(adev->dev, "%d active accelerators config in vpod\n",
		bitmap_weight(remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX));

	/*
	 * allocate metadata entries and ring buffer for all remote GPUs,
	 * to get 2MB page ring buffer NPA mapping for remote access.
	 */
	remote->num_accel = AMDGPU_UALINK_ACCEL_MAX;

	status = adev->ualink.msg_ctl->check_status(adev);
	if (status != AMDGPU_NHT_FW_ST_PREINIT &&
	    status != AMDGPU_NHT_FW_ST_HALT) {
		dev_dbg(adev->dev, "fw status 0x%x not preinit or halt\n", status);
		return -ENODEV;
	}

	dev_dbg(adev->dev, "accel_id %u addr_mode %d fw status 0x%x\n",
		ualink_accel_id(adev), ualink_addr_mode(adev), status);

	/* Alloc metadata structure for all GPUs */
	metadata_size = sizeof(struct amdgpu_ualink_metadata) * AMDGPU_UALINK_ACCEL_MAX;
	metadata_size = AMDGPU_GPU_PAGE_ALIGN(metadata_size);

	dev_dbg(adev->dev, "metadata size 0x%x\n", metadata_size);

	/* pinned system memory */
	r = amdgpu_bo_create_kernel(adev, metadata_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &remote->metadata_bo, &remote->metadata_gpu_addr,
				    &remote->metadata_cpu_addr);
	if (r)
		goto out;

	memset(remote->metadata_cpu_addr, 0, metadata_size);
	metadata = remote->metadata_cpu_addr;

	dev_dbg(adev->dev, "metadata gpu address 0x%llx\n", remote->metadata_gpu_addr);

	/* Allocate ring buffers, wptr for remote interrupt and shootdown */
	rb_size = AMDGPU_GPU_PAGE_ALIGN(2 * AMDGPU_UALINK_RB_SIZE);
	wptr_size = ualink_wptr_size(adev);
	size = (rb_size + wptr_size) * remote->num_accel;
	size = AMDGPU_GPU_PAGE_ALIGN(size);

	dev_dbg(adev->dev, "rb_size 0x%x wptr_size 0x%x total alloc size 0x%x\n",
		rb_size, wptr_size, size);

	/* pinned VRAM */
	r = amdgpu_bo_create_kernel(adev, size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM,
				    &remote->ring_bo,
				    &remote->rb_gpu_addr,
				    &remote->rb_cpu_addr);
	if (r)
		goto out;

	memset(remote->rb_cpu_addr, 0, size);

	dev_dbg(adev->dev, "rb gpu addr 0x%llx cpu addr 0x%p vram_start 0x%llx vram_base 0x%llx\n",
		remote->rb_gpu_addr, remote->rb_cpu_addr, adev->gmc.vram_start,
		adev->vm_manager.vram_base_offset);

	/*
	 * address aliasing mode, shared wptr pagee left is not enough for wb data,
	 * wb status and rptr, alloc another BO, and then alloc npa address and map
	 * to npa vm, for remote to access.
	 *
	 * No gart mapping required for wb data, status and rptr because this is
	 * updated by firmware.
	 */
	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS) {
		rptr_size = AMDGPU_GPU_PAGE_ALIGN(ualink_wb_size(adev) * remote->num_accel);
		dev_dbg(adev->dev, "source aliasing mode rptr_size 0x%x\n", rptr_size);

		/* pinned VRAM */
		r = amdgpu_bo_create_kernel(adev, rptr_size, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_VRAM,
					    &remote->rptr_bo,
					    &remote->rptr_gpu_addr,
					    &remote->rptr_cpu_addr);
		if (r)
			goto out;

		memset(remote->rptr_cpu_addr, 0, rptr_size);

		dev_dbg(adev->dev, "source aliasing rptr gpu addr 0x%llx cpu addr 0x%p\n",
			remote->rptr_gpu_addr, remote->rptr_cpu_addr);
	}

	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS)
		wptr_gpu_addr = remote->rb_gpu_addr + rb_size * ualink_accel_id(adev);

	for_each_set_bit(accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX) {
		dev_dbg(adev->dev, "init for accel_id %u\n", accel_id);

		if (accel_id == ualink_accel_id(adev))
			continue;

		rb_gpu_addr = remote->rb_gpu_addr + rb_size * accel_id;

		if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT) {
			wptr_gpu_addr = remote->rb_gpu_addr + rb_size * remote->num_accel;
			wptr_gpu_addr += wptr_size * accel_id;
		}

		metadata[accel_id].header = AMDGPU_UALINK_METADATA_HEADER;
		metadata[accel_id].rb_size = fls(AMDGPU_UALINK_RB_SIZE / 64) - 1;

		/*
		 * remote shootdown type is 0, remote command type is 1
		 * with 2MB ring buffer mapping, remote shootdown ring NPA is before interrupt
		 */
		metadata[accel_id].ri_rb = rb_gpu_addr + AMDGPU_UALINK_RB_SIZE;
		metadata[accel_id].tlb_inv_rb = rb_gpu_addr;

		metadata[accel_id].tailptr_ri = wptr_gpu_addr +
						ualink_wptr_offset(adev, accel_id);
		metadata[accel_id].tailptr_tlb_inv = wptr_gpu_addr +
						     ualink_tlb_wptr_offset(adev, accel_id);

		dev_dbg(adev->dev, "init from accel_id %u to accel_id %u, rb_size 0x%x\n",
			accel_id, ualink_accel_id(adev), metadata[accel_id].rb_size);
		dev_dbg(adev->dev, "rb 0x%llx tlb rb 0x%llx\n",
			metadata[accel_id].ri_rb, metadata[accel_id].tlb_inv_rb);
		dev_dbg(adev->dev, "rb wptr at 0x%llx tlb wptr at 0x%llx\n",
			metadata[accel_id].tailptr_ri, metadata[accel_id].tailptr_tlb_inv);
	}

	r = adev->ualink.msg_ctl->send_metadata(adev,
						    remote->metadata_gpu_addr,
						    AMDGPU_UALINK_ACCEL_MAX << 8);

out:
	if (r)
		amdgpu_ualink_metadata_fini(adev);

	dev_dbg(adev->dev, "ret 0x%x\n", r);
	return r;
}

/*
 * nHT Firmware Error code
 *
 * Success				0x0
 * FIFO overflow			0x1
 * Invalid RB command			0x2
 * Timeout				0x3
 * Invalid metadata entry		0x4
 * RB overflow				0x5
 * Invalid wptr address			0x6
 * Interrupt cookie send failure	0x7
 * Invalid RB address			0x8
 * Invalid writeback address		0x9
 */
static void amdgpu_ualink_remote_error(struct amdgpu_device *adev, u32 status)
{
	const char *fw_err_code_msg[] = {
		"Unknown FW error status",
		"FIFO overflow",		/* error code 1 */
		"Invalid RB command",
		"Timeout",
		"Invalid metadata entry",
		"RB overflow",
		"Invalid wptr address",
		"Interrupt cookie send failure",
		"Invalid RB address",
		"Invalid writeback address"	/* error code 9 */
		};

	if (status >= ARRAY_SIZE(fw_err_code_msg))
		status = 0;

	dev_err(adev->dev, "remote error %s\n", fw_err_code_msg[status]);
	return;
}

/**
 * amdgpu_ualink_remote_wait_timeout - Wait for remote operation completion
 * @remote_accel_id: remote accelator id to wait for reply
 * @adev: amdgpu device pointer
 * @wb_cpu: CPU virtual address of writeback buffer
 * @seq: Sequence number to wait for
 *
 * Polls the writeback buffer waiting for the sequence number to reach or
 * exceed the expected value. The writeback buffer contains status and data
 * fields updated by firmware to indicate completion and error conditions.
 *
 * Return: 0 on success
 *         -ETIME on timeout
 *         -ECOMM on firmware return error status
 */
static long amdgpu_ualink_remote_wait_timeout(struct amdgpu_device *adev,
					      u32 remote_accel_id,
					      struct amdgpu_ualink_wb *wb_cpu,
					      u32 seq)
{
	/* 2 seconds timeout, long enough for FW to reply */
	ktime_t timeout = ktime_add_us(ktime_get(), 2 * USEC_PER_SEC);
	u32 status, data;
	u64 rptr;

	while (true) {
		data = READ_ONCE(wb_cpu->data);
		if (data >= seq) {
			status = READ_ONCE(wb_cpu->status);
			rptr = READ_ONCE(wb_cpu->rptr);
			break;
		}

		if (ktime_after(ktime_get(), timeout)) {
			dev_dbg(adev->dev, "remote %d wait timeout\n", remote_accel_id);
			return -ETIME;
		}

		usleep_range(10, 50);
	}

	if (status) {
		dev_dbg(adev->dev, "remote %u status 0x%x wb data 0x%x seq 0x%x rptr 0x%llx\n",
			remote_accel_id, status, data, seq, rptr);

		amdgpu_ualink_remote_error(adev, status);
		return -ECOMM;
	}

	dev_dbg(adev->dev, "remote %u succeed seq 0x%x wb data 0x%x rptr 0x%llx\n",
		remote_accel_id, seq, data, rptr);
	return 0;
}

#define AMDGPU_UALINK_REMOTE_OP_TLB_INV		0x1
#define AMDGPU_UALINK_REMOTE_OP_INT		0x2
#define AMDGPU_UALINK_REMOTE_OP_UPDATE_WB	0x3

static void amdgpu_ualink_emit_shootdown(u32 **cpu_addr_p, u32 wb_data,
					 u32 dw0,  u32 dw1, u32 dw2, u32 dw3)
{
	u64 addr = (((u64)dw1 << 32) | (u64)dw2) << AMDGPU_GPU_PAGE_SHIFT;
	u32 *cpu_addr = *cpu_addr_p;
	u32 flush_type = dw0;
	u32 size_in_pages = dw3;
	u32 cmd;

	cmd = AMDGPU_UALINK_REMOTE_OP_TLB_INV;
	cmd |= 1 << 28;	/* headptr update */
	cmd |= 1 << 29;	/* wb enable */

	*cpu_addr++ = cmd;
	*cpu_addr++ = wb_data;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0; /* metadata */
	*cpu_addr++ = 0; /* metadata */

	pr_debug("NPA addr 0x%llx npages 0x%x\n", addr, size_in_pages);

	/* Always shootdown everything with full address size s-field coding */
	addr = GENMASK_ULL(51, 11);

	*cpu_addr++ = lower_32_bits(addr) | flush_type;
	*cpu_addr++ = upper_32_bits(addr);

	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr_p = cpu_addr;
}

static void amdgpu_ualink_emit_interrupt(u32 **cpu_addr_p, u32 wb_data,
					 u32 dw0, u32 dw1, u32 dw2, u32 dw3)
{
	u32 *cpu_addr = *cpu_addr_p;
	u32 cmd;

	cmd = AMDGPU_UALINK_REMOTE_OP_INT;
	cmd |= 1 << 28;	/* headptr_update */
	cmd |= 1 << 29; /* wb enable */

	*cpu_addr++ = cmd;
	*cpu_addr++ = wb_data;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0; /* metadata */
	*cpu_addr++ = 0; /* metadata */
	/* updated by f/w, overwrite PASID with GPU ID */
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	/* IH ContextID 4 dwords */
	*cpu_addr++ = dw0;
	*cpu_addr++ = dw1;
	*cpu_addr++ = dw2;
	*cpu_addr++ = dw3;
	*cpu_addr_p = cpu_addr;
}

static void amdgpu_ualink_emit_update_wb_addr(u32 **cpu_addr_p, u32 wb_data,
					      u32 dw0, u32 dw1, u32 dw2, u32 dw3)
{
	u32 *cpu_addr = *cpu_addr_p;
	u32 cmd;

	cmd = AMDGPU_UALINK_REMOTE_OP_UPDATE_WB;
	cmd |= 1 << 28;	/* headptr update */
	cmd |= 1 << 29;	/* wb enable */

	*cpu_addr++ = cmd;
	*cpu_addr++ = wb_data;
	*cpu_addr++ = dw1 & 0xFFFFFFFC;	/* lower32 wb_npa */
	*cpu_addr++ = dw0 & 0xFFFFF;	/* upper32 wb_npa */
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0; /* metadata */
	*cpu_addr++ = 0; /* metadata */
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr++ = 0;
	*cpu_addr_p = cpu_addr;
}

/*
 * Function proto to generate packet using above different emit_ functions
 */
typedef void (*ualink_emit_packet)(u32 **cpu_addr_p, u32 wb, u32 dw0,
				   u32 dw1, u32 dw2, u32 dw3);

/*
 * Refresh rptr from the writeback and check the ring has room for one more
 * command. The caller must hold the peer lock.
 *
 * Return 0 on success or a negative error code.
 */
static int amdgpu_ualink_ring_check_space(struct amdgpu_device *adev,
					  u32 remote_accel_id,
					  struct amdgpu_ualink_ring *ring,
					  struct amdgpu_ualink_wb *wb_cpu)
{
	ring->rptr = READ_ONCE(wb_cpu->rptr);

	if (WARN_ON_ONCE(ring->rptr > ring->wptr)) {
		dev_err(adev->dev, "accel_id %u ring overflow wptr 0x%llx rptr 0x%llx\n",
			remote_accel_id, ring->wptr, ring->rptr);
		return -EFAULT;
	}

	if ((ring->wptr + 1 - ring->rptr) >= ring->rb_size) {
		dev_err(adev->dev, "accel_id %u command ring full wptr 0x%llx rptr 0x%llx\n",
			remote_accel_id, ring->wptr, ring->rptr);
		return -ENOSPC;
	}

	return 0;
}

static int amdgpu_ualink_send_command(struct amdgpu_device *adev,
				      u32 remote_accel_id,
				      struct amdgpu_ualink_ring *ring,
				      struct amdgpu_ualink_wb *wb_cpu,
				      ualink_emit_packet emit_func,
				      u32 dw0, u32 dw1, u32 dw2, u32 dw3)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct amdgpu_ualink_peer *peer;
	struct amdgpu_ring *sdma_ring;
	struct dma_fence *fence;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	u32 seq, ndw, ndw_copy_cmd, rem;
	u64 doorbell_npa_gart;
	u32 *src_cpu;
	u64 src;
	int r;

	/* Refuse sends unless active */
	if (adev->ualink.info->accel_state != AMDGPU_UALINK_ACCEL_STATE_ACTIVE)
		return -ESHUTDOWN;

	peer = &remote->peer[remote_accel_id];

	r = amdgpu_ualink_ring_check_space(adev, remote_accel_id, ring, wb_cpu);
	if (r)
		goto out;

	/*
	 * 3 sdma copy commands: write data to ring buffer, update wptr, ring doorbell
	 *
	 * insert 2 NOP commands to break SDMA back-to-back copy command overlap, to
	 * ensure the ordering of 3 sdma copy commands execution.
	 */

	/* buffer after sdma commands, starting at 8 dwords boundary */
	ndw_copy_cmd = ALIGN(3 * adev->mman.buffer_funcs->copy_num_dw + 2, 8);

	 /* remote command 16 dwords, wptr 2 dwords, doorbell 1 dword */
	ndw = ndw_copy_cmd + 16 + 2 + 1;
	r = amdgpu_job_alloc_with_ib(adev, &peer->entity, AMDGPU_FENCE_OWNER_VM,
				     ndw * 4, AMDGPU_IB_POOL_IMMEDIATE,
				     AMDGPU_KERNEL_JOB_ID_TTM_COPY_BUFFER, &job);
	if (r)
		goto out;

	ib = &job->ibs[0];
	src = ib->gpu_addr + ndw_copy_cmd * 4;
	src_cpu = ib->ptr + ndw_copy_cmd;

	seq = ++ring->seq;

	div_u64_rem(ring->wptr, ring->rb_size, &rem);
	dev_dbg(adev->dev, "src 0x%llx to npa gart rb 0x%llx wptr 0x%llx doorbell 0x%llx seq 0x%x\n",
		src, ring->rb_npa_gart + rem * 64,
		ring->wptr_npa_gart, ring->doorbell_npa_gart, seq);

	dev_dbg(adev->dev, "ring wptr 0x%llx rptr 0x%llx\n", ring->wptr, ring->rptr);

	/* remote command packet */
	emit_func(&src_cpu, seq, dw0, dw1, dw2, dw3);

	/* wptr value */
	*src_cpu++ = lower_32_bits(ring->wptr + 1);
	*src_cpu++ = upper_32_bits(ring->wptr + 1);

	/* doorbell value */
	*src_cpu++ = lower_32_bits(ring->wptr + 1);

	/*
	 * Add an offset to the doorbell address that cycles through different
	 * values of bit 11:8 to use different DXS ports.
	 */
	peer->dxs_port = (peer->dxs_port + 1) & 0xF;
	doorbell_npa_gart = ring->doorbell_npa_gart | (peer->dxs_port << 8);
	dev_dbg(adev->dev, "dxs_port 0x%x, doorbell_npa_gart 0x%llx -> 0x%llx\n",
		peer->dxs_port, ring->doorbell_npa_gart, doorbell_npa_gart);

	dev_dbg(adev->dev, "use %s\n", remote->use_lsdma ? "lsdma" : "sdma");

	if (remote->use_lsdma) {
		div_u64_rem(ring->wptr, ring->rb_size, &rem);
		r = amdgpu_lsdma_copy_mem(adev, src,
					  ring->rb_npa_gart + rem * 64,
					  64);
		if (!r)
			r = amdgpu_lsdma_copy_mem(adev, src + 64,
						  ring->wptr_npa_gart, 8);
		if (!r)
			r = amdgpu_lsdma_copy_mem(adev, src + 72,
						  doorbell_npa_gart, 4);

		amdgpu_job_free(job);

		/* Copy failed: command never reached the remote, so no
		 * writeback will arrive. Return now instead of polling the
		 * completion for the full remote timeout.
		 */
		if (r) {
			dev_dbg(adev->dev,
				"remote %u lsdma copy failed (r %d), skip completion wait\n",
				remote_accel_id, r);
			return r;
		}

		goto out_wait_complete;
	}

	sdma_ring = &adev->sdma.instance[0].ring;

	/*
	 * SDMA commands copy from ib to NPA, insert NOPs to ensure SDMA copy commands
	 * execution doesn't overlap, doorbell update is after data and wptr update
	 * finished.
	 */
	div_u64_rem(ring->wptr, ring->rb_size, &rem);
	amdgpu_emit_copy_buffer(adev, ib, src,
				ring->rb_npa_gart + rem * 64,
				64, 0);

	ib->ptr[ib->length_dw++] = sdma_ring->funcs->nop;

	amdgpu_emit_copy_buffer(adev, ib, src + 64, ring->wptr_npa_gart, 8, 0);

	ib->ptr[ib->length_dw++] = sdma_ring->funcs->nop;

	amdgpu_emit_copy_buffer(adev, ib, src + 72, doorbell_npa_gart, 4, 0);

	amdgpu_ring_pad_ib(sdma_ring, ib);
	WARN_ON(ib->length_dw > ndw_copy_cmd);

	fence = amdgpu_job_submit(job);
	r = dma_fence_wait_timeout(fence, false, AMDGPU_FENCE_JIFFIES_TIMEOUT);
	dma_fence_put(fence);
	if (r <= 0) {
		dev_dbg(adev->dev, "remote %u sdma fence wait return r %d\n",
			remote_accel_id, r);
		if (r == 0)
			r = -ETIME;
		goto out;
	}

out_wait_complete:
	/*
	 * poll remote completion writeback data
	 */
	r = amdgpu_ualink_remote_wait_timeout(adev, remote_accel_id, wb_cpu, seq);

	/*
	 * Advance wptr unless the remote timed out, even if the FW returned an
	 * error status. On timeout the caller resets the connection, which
	 * resyncs wptr from the remote in amdgpu_ualink_setup_connection.
	 */
	if (r != -ETIME)
		ring->wptr++;

out:
	dev_dbg(adev->dev, "ret r = %d\n", r);
	return r;
}

static void amdgpu_ualink_get_wb_addr(struct amdgpu_device *adev,
				      u32 remote_accel_id,
				      struct amdgpu_ualink_wb **wb_cpu,
				      u64 *wb_npa, u32 type)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	u32 accel_id = ualink_accel_id(adev);
	u32 rb_size = AMDGPU_GPU_PAGE_ALIGN(2 * AMDGPU_UALINK_RB_SIZE);
	uintptr_t wb;
	u64 npa;
	u32 offset = 0;

	if (ualink_addr_mode(adev) == AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT) {
		wb = (uintptr_t)remote->rb_cpu_addr + rb_size * remote->num_accel;
		npa = amdgpu_ualink_npa_addr(adev, RB_TYPE_TAILPTR,
					     remote_accel_id, accel_id);
	} else {
		wb = (uintptr_t)remote->rptr_cpu_addr;
		npa = remote->rptr_npa;
		npa |= (u64)accel_id << AMDGPU_UALINK_GART_NPA_ADDR_GPUID_SHIFT;
	}

	if (type == RB_TYPE_TLB_INV)
		offset = ualink_tlb_wb_offset(adev, remote_accel_id);
	else if (type == RB_TYPE_REMOTE_INTERRUPT)
		offset = ualink_wb_offset(adev, remote_accel_id);

	*wb_cpu = (struct amdgpu_ualink_wb *)(wb + offset);
	if (wb_npa)
		*wb_npa = npa + offset;

	dev_dbg(adev->dev, "source %d remote %d wb npa 0x%llx\n", accel_id,
		remote_accel_id, npa + offset);
}

struct amdgpu_ualink_wptr_fence_cb {
	struct dma_fence_cb base;
	u64 *wptr_cpu;
	u64 result;
};

static void amdgpu_ualink_read_wptr_fence_cb(struct dma_fence *fence,
					     struct dma_fence_cb *cb)
{
	struct amdgpu_ualink_wptr_fence_cb *wptr_cb;

	wptr_cb = container_of(cb, typeof(*wptr_cb), base);
	wptr_cb->result = *wptr_cb->wptr_cpu;
}

static int amdgpu_ualink_read_remote_wptr(struct amdgpu_device *adev,
					  u32 remote_accel_id,
					  struct amdgpu_ualink_ring *ring,
					  u64 *wptr)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct amdgpu_ualink_peer *peer = &remote->peer[remote_accel_id];
	struct amdgpu_ualink_wptr_fence_cb wptr_cb = { };
	struct amdgpu_ring *sdma_ring;
	struct dma_fence *fence;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	u32 ndw, ndw_copy_cmd;
	u64 wptr_gpu, *wptr_cpu;
	int r;

	*wptr = 0;

	/* Refuse remote access unless active */
	if (adev->ualink.info->accel_state != AMDGPU_UALINK_ACCEL_STATE_ACTIVE)
		return -ESHUTDOWN;

	/* 1 sdma copy command to read wptr */
	ndw_copy_cmd = ALIGN(adev->mman.buffer_funcs->copy_num_dw, 8);

	/* wptr 2 dwords */
	ndw = ndw_copy_cmd + 2;
	r = amdgpu_job_alloc_with_ib(adev, &peer->entity, AMDGPU_FENCE_OWNER_VM,
				     ndw * 4, AMDGPU_IB_POOL_IMMEDIATE,
				     AMDGPU_KERNEL_JOB_ID_TTM_COPY_BUFFER, &job);
	if (r)
		return r;

	ib = &job->ibs[0];
	wptr_gpu = ib->gpu_addr + ndw_copy_cmd * 4;
	wptr_cpu = (u64 *)(ib->ptr + ndw_copy_cmd);

	dev_dbg(adev->dev, "read wptr from remote %u npa gart wptr 0x%llx use %s\n",
		remote_accel_id, ring->wptr_npa_gart,
		remote->use_lsdma ? "lsdma" : "sdma");

	if (remote->use_lsdma) {
		r = amdgpu_lsdma_copy_mem(adev, ring->wptr_npa_gart, wptr_gpu, 8);
		if (!r)
			*wptr = *wptr_cpu;
		amdgpu_job_free(job);
		goto out;
	}

	wptr_cb.wptr_cpu = wptr_cpu;

	amdgpu_emit_copy_buffer(adev, ib, ring->wptr_npa_gart, wptr_gpu, 8, 0);

	sdma_ring = &adev->sdma.instance[0].ring;
	amdgpu_ring_pad_ib(sdma_ring, ib);
	WARN_ON(ib->length_dw > ndw_copy_cmd);

	fence = amdgpu_job_submit(job);

	r = dma_fence_add_callback(fence, &wptr_cb.base,
				   amdgpu_ualink_read_wptr_fence_cb);
	if (r == -ENOENT) {
		/* Fence already signaled; the callback won't run, do it inline. */
		amdgpu_ualink_read_wptr_fence_cb(fence, &wptr_cb.base);
		r = 0;
		goto out_put_fence;
	} else if (r) {
		/* -EINVAL: NULL fence or func, not expected here. */
		goto out_put_fence;
	}

	r = dma_fence_wait_timeout(fence, false, AMDGPU_FENCE_JIFFIES_TIMEOUT);
	if (r > 0) {
		r = 0;	/* read wptr successfully */
		goto out_put_fence;
	}

	dev_dbg(adev->dev, "remote %u sdma fence wait return r %d\n",
		remote_accel_id, r);

	if (r == 0)
		r = -ETIME;

	/*
	 * Timed out: the callback is still armed and wptr_cb is on our stack.
	 * Cancel it so it can't fire after we return and write into the freed
	 * stack frame.
	 */
	dma_fence_remove_callback(fence, &wptr_cb.base);

out_put_fence:
	if (!r)
		*wptr = wptr_cb.result;
	dma_fence_put(fence);

out:
	dev_dbg(adev->dev, "remote %u wptr 0x%llx return %d\n",
		remote_accel_id, *wptr, r);
	return r;
}

/**
 * amdgpu_ualink_ring_resync_ptrs - sync local ring pointers from remote
 * @adev: amdgpu device pointer
 * @remote_accel_id: remote accelerator ID
 *
 * After a reboot on either side the two ends disagree on wptr. Read the remote
 * wptr of both peer rings and adopt it as the local wptr and rptr.
 *
 * Return 0 if both rings already match, 1 if they were resynced, or a negative
 * error code if a remote read failed, leaving the rings untouched.
 */
static int amdgpu_ualink_ring_resync_ptrs(struct amdgpu_device *adev,
					  u32 remote_accel_id)
{
	struct amdgpu_ualink_peer *peer = &to_remote(adev)->peer[remote_accel_id];
	struct amdgpu_ualink_wb *int_wb, *tlb_wb;
	u64 int_wptr, tlb_wptr;
	int r;

	/* ring->wptr and ring->rptr are owned by the send path. */
	guard(mutex)(&peer->lock);

	/* Read both before updating either, so a failure changes nothing. */
	r = amdgpu_ualink_read_remote_wptr(adev, remote_accel_id,
					   &peer->interrupt, &int_wptr);
	if (!r)
		r = amdgpu_ualink_read_remote_wptr(adev, remote_accel_id,
						   &peer->shootdown, &tlb_wptr);
	if (r) {
		dev_dbg(adev->dev, "accel_id %u read remote %u wptr failed %d\n",
			ualink_accel_id(adev), remote_accel_id, r);
		return r;
	}

	if (peer->interrupt.wptr == int_wptr && peer->shootdown.wptr == tlb_wptr) {
		dev_dbg(adev->dev, "accel_id %u wptr 0x%llx 0x%llx is same as remote %u\n",
			ualink_accel_id(adev), int_wptr, tlb_wptr, remote_accel_id);
		return 0;
	}

	dev_dbg(adev->dev,
		"accel_id %u wptr 0x%llx 0x%llx sync to remote %u wptr 0x%llx 0x%llx\n",
		ualink_accel_id(adev), peer->interrupt.wptr, peer->shootdown.wptr,
		remote_accel_id, int_wptr, tlb_wptr);

	amdgpu_ualink_get_wb_addr(adev, remote_accel_id, &int_wb, NULL,
				  RB_TYPE_REMOTE_INTERRUPT);
	amdgpu_ualink_get_wb_addr(adev, remote_accel_id, &tlb_wb, NULL,
				  RB_TYPE_TLB_INV);

	peer->interrupt.wptr = int_wptr;
	peer->interrupt.rptr = int_wptr;
	peer->shootdown.wptr = tlb_wptr;
	peer->shootdown.rptr = tlb_wptr;

	/* Drop the stale writeback rptr, it disagrees with the resynced wptr. */
	WRITE_ONCE(int_wb->rptr, int_wptr);
	WRITE_ONCE(tlb_wb->rptr, tlb_wptr);
	return 1;
}

static void amdgpu_ualink_reset_peer_rings(struct amdgpu_device *adev,
					   u32 remote_accel_id)
{
	struct amdgpu_ualink_peer *peer = &to_remote(adev)->peer[remote_accel_id];

	dev_dbg(adev->dev, "remote accel_id %u\n", remote_accel_id);

	guard(mutex)(&peer->lock);

	peer->interrupt.ready = false;
	peer->shootdown.ready = false;
}

static int amdgpu_ualink_update_wb_address(struct amdgpu_device *adev,
					   u32 remote_accel_id, u32 ring_type)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct amdgpu_ualink_peer *peer;
	struct amdgpu_ualink_ring *ring;
	struct amdgpu_ualink_wb *wb_cpu;
	u64 wb_npa;
	int r;

	peer = &remote->peer[remote_accel_id];
	if (ring_type == RB_TYPE_REMOTE_INTERRUPT)
		ring = &peer->interrupt;
	else if (ring_type == RB_TYPE_TLB_INV)
		ring = &peer->shootdown;
	else
		return -EINVAL;

	amdgpu_ualink_get_wb_addr(adev, remote_accel_id, &wb_cpu, &wb_npa,
				  ring_type);

	r = amdgpu_ualink_send_command(adev, remote_accel_id, ring, wb_cpu,
				       amdgpu_ualink_emit_update_wb_addr,
				       upper_32_bits(wb_npa),
				       lower_32_bits(wb_npa),
				       0, 0);
	if (!r) {
		ring->wb_cpu = wb_cpu;
		ring->ready = true;
	}
	return r;
}

/**
 * amdgpu_ualink_remote_shootdown - Send a remote TLB invalidation request
 * @adev: amdgpu device pointer
 * @remote_accel_id: accelerator ID of the remote GPU to shootdown
 * @addr: page-aligned address to invalidate
 * @size_in_pages: number of pages to invalidate, 0 to invalidate entire TLB
 * @flush_type: type of TLB flush to perform
 *
 * Sends a remote TLB shootdown command to the specified peer GPU via the
 * UALink ring buffer. The address and size are translated using the S-field
 * encoding before being written to the ring.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_remote_shootdown(struct amdgpu_device *adev,
				   u32 remote_accel_id, u64 addr,
				   u32 size_in_pages, u32 flush_type)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct amdgpu_ualink_peer *peer;
	struct amdgpu_ualink_ring *ring;
	int r;

	dev_dbg(adev->dev, "skip remote shootdown to remote_accel_id %u\n",
		remote_accel_id);
	return 0;

	peer = &remote->peer[remote_accel_id];
	ring = &peer->shootdown;

	guard(mutex)(&peer->lock);

	if (!ring->ready) {
		dev_dbg(adev->dev, "accel_id %u ring not ready\n", remote_accel_id);
		r = amdgpu_ualink_update_wb_address(adev, remote_accel_id,
						    RB_TYPE_TLB_INV);
		if (r)
			return r;
	}

	return amdgpu_ualink_send_command(adev, remote_accel_id, ring,
					  ring->wb_cpu,
					  amdgpu_ualink_emit_shootdown,
					  flush_type, upper_32_bits(addr),
					  lower_32_bits(addr), size_in_pages);
}

/**
 * amdgpu_ualink_remote_interrupt - Send a remote interrupt to a peer GPU
 * @adev: amdgpu device pointer
 * @remote_accel_id: accelerator ID of the remote GPU to interrupt
 * @dw0: IH context ID dword 0
 * @dw1: IH context ID dword 1
 * @dw2: IH context ID dword 2
 * @dw3: IH context ID dword 3
 *
 * Sends a remote interrupt command to the specified peer GPU via the
 * UALink ring buffer. The four context ID dwords are delivered to the
 * remote GPU's interrupt handler.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_remote_interrupt(struct amdgpu_device *adev,
				   u32 remote_accel_id, u32 dw0, u32 dw1,
				   u32 dw2, u32 dw3)
{
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct amdgpu_ualink_peer *peer;
	struct amdgpu_ualink_ring *ring;
	int r;

	peer = &remote->peer[remote_accel_id];
	ring = &peer->interrupt;

	guard(mutex)(&peer->lock);

	if (!ring->ready) {
		dev_dbg(adev->dev, "accel_id %u ring not ready\n", remote_accel_id);
		r = amdgpu_ualink_update_wb_address(adev, remote_accel_id,
						    RB_TYPE_REMOTE_INTERRUPT);
		if (r)
			return r;
	}

	return amdgpu_ualink_send_command(adev, remote_accel_id, ring,
					  ring->wb_cpu,
					  amdgpu_ualink_emit_interrupt,
					  dw0, dw1, dw2, dw3);
}

/**
 * amdgpu_ualink_peer_remote_init - Initialize remote peer GPU connections
 * @adev: amdgpu device pointer
 *
 * Sets up ring buffers, GART mappings, and control structures for
 * communicating with remote GPUs in the fabric.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amdgpu_ualink_peer_remote_init(struct amdgpu_device *adev)
{
	u32 rb_pages = AMDGPU_UALINK_RB_SIZE >> AMDGPU_GPU_PAGE_SHIFT;
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	struct amdgpu_bo *bo = remote->ring_bo;
	struct amdgpu_ualink_peer *peer;
	struct amdgpu_ualink_ring *interrupt;
	struct amdgpu_ualink_ring *shootdown;
	u32 src_accel_id = ualink_accel_id(adev);
	u32 dst_accel_id;
	u64 npa, flags;
	int r = 0;

	/* NPA mapping PTE flags VSCT = 0011 */
	flags = amdgpu_ttm_tt_pte_flags(adev, bo->tbo.ttm, bo->tbo.resource);
	flags |= AMDGPU_PTE_SNOOPED | AMDGPU_PTE_PRT_GFX12 | AMDGPU_PTE_BUS_ATOMICS;
	flags &= ~AMDGPU_PTE_VALID;
	flags &= ~AMDGPU_PTE_SYSTEM;
	/* PTE.X=0 turn off RPC checks for RBs, wptr and doorbell NPA address */
	flags &= ~AMDGPU_PTE_EXECUTABLE;

	dev_dbg(adev->dev, "src_accel_id %u gart mapping flags 0x%llx\n",
		src_accel_id, flags);

	for_each_set_bit(dst_accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX) {
		if (dst_accel_id == src_accel_id)
			continue;

		dev_dbg(adev->dev, "peer_remote to dst_accel_id %u\n", dst_accel_id);

		peer = &remote->peer[dst_accel_id];
		mutex_init(&peer->lock);

		interrupt = &peer->interrupt;
		shootdown = &peer->shootdown;

		/* ring buffer npa gart mapping */
		npa = amdgpu_ualink_gart_npa_addr(adev, RB_TYPE_REMOTE_INTERRUPT,
						   src_accel_id, dst_accel_id);
		r = amdgpu_ualink_gart_map(adev, rb_pages, npa, &interrupt->mm_node_rb, flags);
		if (r)
			break;
		interrupt->rb_npa_gart = adev->gmc.gart_start +
					 (interrupt->mm_node_rb.start << AMDGPU_GPU_PAGE_SHIFT);

		dev_dbg(adev->dev, "rb npa 0x%llx mapped on gart 0x%llx\n",
			npa, interrupt->rb_npa_gart);

		/* tlb invalidate ring buffer npa gart mapping */
		npa = amdgpu_ualink_gart_npa_addr(adev, RB_TYPE_TLB_INV, src_accel_id,
						  dst_accel_id);
		r = amdgpu_ualink_gart_map(adev, rb_pages, npa, &shootdown->mm_node_rb, flags);
		if (r)
			break;
		shootdown->rb_npa_gart = adev->gmc.gart_start +
					 (shootdown->mm_node_rb.start << AMDGPU_GPU_PAGE_SHIFT);

		dev_dbg(adev->dev, "tlb rb npa 0x%llx mapped on gart 0x%llx\n",
			npa, shootdown->rb_npa_gart);

		/* wptr npa gart mapping */
		npa = amdgpu_ualink_gart_npa_addr(adev, RB_TYPE_TAILPTR, src_accel_id,
						  dst_accel_id);
		r = amdgpu_ualink_gart_map(adev, 1, npa, &interrupt->mm_node_wptr, flags);
		if (r)
			break;

		interrupt->wptr_npa_gart = adev->gmc.gart_start +
					   (interrupt->mm_node_wptr.start << AMDGPU_GPU_PAGE_SHIFT)
					    + ualink_wptr_offset(adev, src_accel_id);
		shootdown->wptr_npa_gart = adev->gmc.gart_start +
					   (interrupt->mm_node_wptr.start << AMDGPU_GPU_PAGE_SHIFT)
					    + ualink_tlb_wptr_offset(adev, src_accel_id);

		dev_dbg(adev->dev, "rb wptr npa 0x%llx on gart 0x%llx, tlb wptr on gart 0x%llx\n",
			npa, interrupt->wptr_npa_gart, shootdown->wptr_npa_gart);

		/*
		 * doorbell npa gart mapping, doorbell NPA address coding
		 *
		 * ReqAddr[51:40] = 0xFFF
		 * ReqAddr[39:30] = GPU accel_id
		 * ReqAddr[29:12] = 18'h00000
		 * ReqAddr[11:8]  = Random value
		 * ReqAddr[7:6]   = PMI Index
		 *     0 - PMI0 Remote Interrupt
		 *     1 - PMI1 Remote TLB Shootdown
		 * ReqAddr[5:0]   = 6'h0
		 */
		npa = 0xFFFULL << 40 | (u64)dst_accel_id << 30;
		r = amdgpu_ualink_gart_map(adev, 1, npa, &interrupt->mm_node_doorbell, flags);
		if (r)
			break;

		interrupt->doorbell_npa_gart = adev->gmc.gart_start +
			(interrupt->mm_node_doorbell.start << AMDGPU_GPU_PAGE_SHIFT);

		dev_dbg(adev->dev, "rb doorbell npa 0x%llx mapped on gart 0x%llx\n",
			npa, interrupt->doorbell_npa_gart);

		shootdown->doorbell_npa_gart = adev->gmc.gart_start+
			(interrupt->mm_node_doorbell.start << AMDGPU_GPU_PAGE_SHIFT) + (1 << 6);

		dev_dbg(adev->dev, "tlb doorbell npa 0x%llx mapped on gart 0x%llx\n",
			npa, shootdown->doorbell_npa_gart);

		interrupt->rb_size = AMDGPU_UALINK_RB_SIZE / 64;
		interrupt->wptr = 0;
		interrupt->rptr = 0;
		interrupt->seq = 0;

		shootdown->rb_size = AMDGPU_UALINK_RB_SIZE / 64;
		shootdown->wptr = 0;
		shootdown->rptr = 0;
		shootdown->seq = 0;
	}

	dev_dbg(adev->dev, "peer remote init done r=%d\n", r);
	return r;
}

/**
 * amdgpu_ualink_peer_remote_fini - Clean up remote peer GPU connections
 * @adev: amdgpu device pointer
 *
 * Unmaps GART entries and frees resources for remote GPU communication.
 */
static void amdgpu_ualink_peer_remote_fini(struct amdgpu_device *adev)
{
	u32 rb_pages = AMDGPU_UALINK_RB_SIZE >> AMDGPU_GPU_PAGE_SHIFT;
	struct amdgpu_ualink_remote *remote = to_remote(adev);
	u32 src_accel_id = ualink_accel_id(adev);
	u32 dst_accel_id;
	struct amdgpu_ualink_peer *peer;
	struct amdgpu_ualink_ring *ring;

	dev_dbg(adev->dev, "src_accel_id %u\n", src_accel_id);

	for_each_set_bit(dst_accel_id, remote->active_accel_bits, AMDGPU_UALINK_ACCEL_MAX) {
		dev_dbg(adev->dev, "dst_accel_id %u\n", dst_accel_id);
		if (dst_accel_id == src_accel_id)
			continue;

		peer = &remote->peer[dst_accel_id];
		mutex_destroy(&peer->lock);

		ring = &peer->interrupt;
		amdgpu_ualink_gart_unmap(adev, rb_pages, &ring->mm_node_rb);
		amdgpu_ualink_gart_unmap(adev, 1, &ring->mm_node_wptr);
		amdgpu_ualink_gart_unmap(adev, 1, &ring->mm_node_doorbell);

		ring = &peer->shootdown;
		amdgpu_ualink_gart_unmap(adev, rb_pages, &ring->mm_node_rb);
	}
}

/**
 * amdgpu_ualink_sw_init - Initialize UALink software resources
 * @adev: amdgpu device pointer
 *
 * Performs full UALink software initialization including metadata allocation,
 * NPA address mapping, SDMA scheduler entity setup, and remote peer GPU
 * connection initialization. On failure, all previously initialized resources
 * are cleaned up.
 *
 * Return: 0 on success, negative error code on failure
 */
int amdgpu_ualink_sw_init(struct amdgpu_device *adev)
{
	struct amdgpu_ualink_remote *remote;
	int r;

	remote = kzalloc(sizeof(*remote), GFP_KERNEL);
	if (!remote)
		return -ENOMEM;
	adev->ualink.remote = remote;

	r = amdgpu_ualink_metadata_init(adev);
	if (r)
		goto out;

	r = amdgpu_ualink_metadata_npa_mapping(adev);
	if (r)
		goto out_metadata_fini;

	r = amdgpu_ualink_sdma_entities_init(adev);
	if (r)
		goto out_npa_unmap;

	r = amdgpu_ualink_peer_remote_init(adev);
	if (r)
		goto out_sdma_entities_fini;

	dev_dbg(adev->dev, "ualink sw init succeed\n");
	return 0;

out_sdma_entities_fini:
	amdgpu_ualink_sdma_entities_fini(adev);
out_npa_unmap:
	amdgpu_ualink_metadata_npa_unmapping(adev);
out_metadata_fini:
	amdgpu_ualink_metadata_fini(adev);
out:
	kfree(adev->ualink.remote);
	adev->ualink.remote = NULL;
	dev_dbg(adev->dev, "ualink sw init failed %d\n", r);
	return r;
}

/**
 * amdgpu_ualink_sw_fini - Tear down UALink software resources
 * @adev: amdgpu device pointer
 *
 * Cleans up all UALink software resources in reverse order of initialization:
 * remote peer connections, SDMA entities, NPA mappings, and metadata.
 */
void amdgpu_ualink_sw_fini(struct amdgpu_device *adev)
{
	dev_dbg(adev->dev, "halt accel_id %u addr_mode %d\n", ualink_accel_id(adev),
		ualink_addr_mode(adev));

	adev->ualink.msg_ctl->send_halt(adev);

	amdgpu_ualink_peer_remote_fini(adev);
	amdgpu_ualink_sdma_entities_fini(adev);
	amdgpu_ualink_metadata_npa_unmapping(adev);
	amdgpu_ualink_metadata_fini(adev);
	kfree(adev->ualink.remote);
	adev->ualink.remote = NULL;
}

int ualink_send_hello(struct amdgpu_device *adev, u32 remote_accel_id)
{
	return amdgpu_ualink_send_hello_msg(adev, remote_accel_id);
}

/*
 * The low bits of handle_lo carry the message-header field on the wire.
 * Strip those bits and splice in @acc_id to recover the
 * fully-qualified handle_lo value.
 */
static inline u64 amdgpu_ualink_reassemble_handle(u64 handle_lo, u32 acc_id)
{
	return (handle_lo & ~AMDGPU_UALINK_MESSAGE_HEADER_MASK) |
	       (acc_id & AMDGPU_UALINK_HANDLE_ACCID_MASK);
}

static int amdgpu_ualink_process_irq(struct amdgpu_device *adev,
				     struct amdgpu_irq_src *source,
				     struct amdgpu_iv_entry *entry)
{
	u32 sender_acc_id, receiver_acc_id, msg_type, src_acc_id;
	enum amdgpu_ualink_accel_state accel_state;
	struct amdgpu_ualink_handle handle;
	u32 size, npa_addr, fail_reason;
	u32 dw0, dw1, dw2, dw3;
	u32 local_acc_id;
	int handled = 1;

	if (unlikely(adev->ualink.mgr_state != AMDGPU_UALINK_INIT_COMPLETE)) {
		dev_dbg(adev->dev,
			"UALink manager not initialized, dropping irq\n");
		return handled;
	}

	/* Only handle interrupts while ACTIVE. Otherwise (e.g. during teardown)
	 * consume stale ring entries but drop them, so they don't touch state
	 * being freed.
	 */
	accel_state = adev->ualink.info->accel_state;
	if (accel_state != AMDGPU_UALINK_ACCEL_STATE_ACTIVE) {
		dev_dbg(adev->dev,
			"Dropping stale UALink interrupt, accel_state %d\n",
			accel_state);
		return handled;
	}

	dev_dbg(adev->dev, "%s client_id 0x%x src_id 0x%x ih\n",
		entry->ih == &adev->irq.ih ? "ring" : "ualink soft ring",
		entry->client_id, entry->src_id);

	/* ContextID 4 dwords */
	src_acc_id = entry->pasid;
	dw0 = entry->src_data[0];
	dw1 = entry->src_data[1];
	dw2 = entry->src_data[2];
	dw3 = entry->src_data[3];

	/* Copy IH entry into ualink soft ring. */
	if (entry->ih == &adev->irq.ih) {
		dev_dbg(adev->dev, "src accel_id %u context id 0x%x 0x%x 0x%x 0x%x\n",
			src_acc_id, dw0, dw1, dw2, dw3);
		dev_dbg(adev->dev, "delegate to ualink irq soft ring\n");
		amdgpu_irq_ualink_delegate(adev, entry, 8);
		return handled;
	}

	msg_type = dw0 & AMDGPU_UALINK_MESSAGE_HEADER_MASK;
	local_acc_id = adev->ualink.info->ppod.accel_id;
	dev_dbg(adev->dev, "Got MSG: remote acc_id %u msg_type %u\n",
		src_acc_id, msg_type);

	/* Debug hook: if the bit corresponding to this msg_type is set in
	 * drop_msg_bitmap, drop this single packet and clear the bit so that
	 * any subsequent incoming messages are processed normally. Used to
	 * exercise the connection reset/recovery paths via debugfs.
	 */
	if (msg_type < BITS_PER_LONG &&
	    test_and_clear_bit(msg_type, &adev->ualink.drop_msg_bitmap)) {
		dev_warn(adev->dev,
			 "DROP MSG (debugfs): src acc_id %u msg_type %u dw[0-3] 0x%x 0x%x 0x%x 0x%x\n",
			 src_acc_id, msg_type, dw0, dw1, dw2, dw3);
		return handled;
	}

	switch (msg_type) {
	case AMDGPU_UALINK_HELLO_MSG:
		receiver_acc_id = (dw0 >> AMDGPU_UALINK_HELLO_MSG_RECV_ACCID_SHIFT) &
					AMDGPU_UALINK_HELLO_MSG_ACCID_MASK;
		sender_acc_id = (dw0 >> AMDGPU_UALINK_HELLO_MSG_SENDER_ACCID_SHIFT) &
					AMDGPU_UALINK_HELLO_MSG_ACCID_MASK;
		dev_dbg(adev->dev,
			"Got HELLO MSG: src acc_id %u receiver_acc_id %u sender_acc_id %u\n",
			src_acc_id, receiver_acc_id, sender_acc_id);
		amdgpu_ualink_process_hello_msg(adev, receiver_acc_id, sender_acc_id,
						src_acc_id);
		break;
	case AMDGPU_UALINK_HELLO_ACK_MSG:
		dev_dbg(adev->dev,
			"Got HELLO-ACK MSG: remote acc_id %u\n", src_acc_id);
		amdgpu_ualink_process_hello_ack_msg(adev, src_acc_id);
		break;
	case AMDGPU_UALINK_NPA_REQ_MSG:
		memcpy(&handle, entry->src_data, sizeof(struct amdgpu_ualink_handle));
		handle.handle_lo = amdgpu_ualink_reassemble_handle(handle.handle_lo,
								   local_acc_id);
		dev_dbg(adev->dev,
			"Got NPA-REQ MSG: remote acc_id %u handle %llx:%llx\n",
			src_acc_id, handle.handle_hi, handle.handle_lo);
		amdgpu_ualink_process_npa_req_msg(adev, src_acc_id, handle);
		break;
	case AMDGPU_UALINK_NPA_RSP_MSG:
		memcpy(&handle.handle_lo, entry->src_data, sizeof(handle.handle_lo));
		handle.handle_lo = amdgpu_ualink_reassemble_handle(handle.handle_lo,
								   src_acc_id);
		size = entry->src_data[2];
		npa_addr = entry->src_data[3];
		dev_dbg(adev->dev,
			"Got NPA-RSP MSG: remote acc_id %u handle_lo %llx size 0x%x npa_addr 0x%x\n",
			src_acc_id, handle.handle_lo, size, npa_addr);
		amdgpu_ualink_process_npa_rsp_msg(adev, src_acc_id,
						  handle.handle_lo,
						  npa_addr, size);
		break;
	case AMDGPU_UALINK_NPA_FAIL_MSG:
		memcpy(&handle.handle_lo, entry->src_data, sizeof(handle.handle_lo));
		handle.handle_lo = amdgpu_ualink_reassemble_handle(handle.handle_lo,
								   src_acc_id);
		fail_reason = entry->src_data[2] &
				AMDGPU_UALINK_NPA_FAIL_MSG_FAIL_REASON_MASK;
		dev_dbg(adev->dev,
			"Got NPA-FAIL MSG: remote acc_id %u handle_lo %llx fail_reason %u\n",
			src_acc_id, handle.handle_lo, fail_reason);
		amdgpu_ualink_process_npa_fail_msg(adev, src_acc_id,
						   handle.handle_lo,
						   fail_reason);
		break;
	case AMDGPU_UALINK_NPA_REVOKE_MSG:
		memcpy(&handle, entry->src_data, sizeof(struct amdgpu_ualink_handle));
		handle.handle_lo = amdgpu_ualink_reassemble_handle(handle.handle_lo,
								   src_acc_id);
		dev_dbg(adev->dev,
			"Got NPA-REVOKE MSG: remote acc_id %u handle %llx:%llx\n",
			src_acc_id, handle.handle_hi, handle.handle_lo);
		amdgpu_ualink_process_npa_revoke_msg(adev, src_acc_id, handle);
		break;
	case AMDGPU_UALINK_NPA_RELEASE_MSG:
		memcpy(&handle, entry->src_data, sizeof(struct amdgpu_ualink_handle));
		handle.handle_lo = amdgpu_ualink_reassemble_handle(handle.handle_lo,
								   local_acc_id);
		dev_dbg(adev->dev,
			"Got NPA-RELEASE MSG: remote acc_id %u handle %llx:%llx\n",
			src_acc_id, handle.handle_hi, handle.handle_lo);
		amdgpu_ualink_process_npa_release_msg(adev, src_acc_id, handle);
		break;
	default:
		dev_err(adev->dev, "Unknown message type (%u)\n", msg_type);
		break;
	}

	return handled;
}

static int amdgpu_ualink_set_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					u32 type,
					enum amdgpu_interrupt_state state)
{
	/*
	 * Don't set register to enable/disable nHT controller interrupt.
	 *
	 * F/W running on MP2, which can always send cookie to IH block to
	 * interrupt driver.
	 */
	dev_dbg(adev->dev, "ualink interrupt %s\n",
		state == AMDGPU_IRQ_STATE_ENABLE ? "enable" : "disable");
	return 0;
}

static const struct amdgpu_irq_src_funcs ualink_irq_funcs = {
	.set = amdgpu_ualink_set_irq_state,
	.process = amdgpu_ualink_process_irq,
};

/**
 * amdgpu_ualink_init_interrupt - register the UALink IRQ source
 * @adev: amdgpu device pointer
 * @client_id: IH client ID for the interrupt source
 * @src_id: source ID for the interrupt source
 *
 * Registers the UALink interrupt source with the IH subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int amdgpu_ualink_init_interrupt(struct amdgpu_device *adev,
				 unsigned int client_id, unsigned int src_id)
{
	int r;

	dev_dbg(adev->dev, "init ualink irq client_id 0x%x src_id 0x%x\n",
		client_id, src_id);

	adev->ualink.irq.num_types = 1;
	adev->ualink.irq.funcs = &ualink_irq_funcs;

	r = amdgpu_irq_add_id(adev, client_id, src_id, &adev->ualink.irq);
	return r;
}

