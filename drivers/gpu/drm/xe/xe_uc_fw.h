/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_UC_FW_H_
#define _XE_UC_FW_H_

#include <linux/errno.h>

#include "xe_macros.h"
#include "xe_uc_fw_abi.h"
#include "xe_uc_fw_types.h"

struct drm_printer;

int xe_uc_fw_init(struct xe_uc_fw *uc_fw);
size_t xe_uc_fw_copy_rsa(struct xe_uc_fw *uc_fw, void *dst, u32 max_len);
int xe_uc_fw_upload(struct xe_uc_fw *uc_fw, u32 offset, u32 dma_flags);
int xe_uc_fw_check_version_requirements(struct xe_uc_fw *uc_fw);
void xe_uc_fw_print(struct xe_uc_fw *uc_fw, struct drm_printer *p);

static inline u32 xe_uc_fw_rsa_offset(struct xe_uc_fw *uc_fw)
{
	return sizeof(struct uc_css_header) + uc_fw->ucode_size + uc_fw->css_offset;
}

static inline void xe_uc_fw_change_status(struct xe_uc_fw *uc_fw,
					  enum xe_uc_fw_status status)
{
	uc_fw->__status = status;
}

static inline
const char *xe_uc_fw_status_repr(enum xe_uc_fw_status status)
{
	switch (status) {
	case XE_UC_FIRMWARE_NOT_SUPPORTED:
		return "N/A";
	case XE_UC_FIRMWARE_UNINITIALIZED:
		return "UNINITIALIZED";
	case XE_UC_FIRMWARE_DISABLED:
		return "DISABLED";
	case XE_UC_FIRMWARE_SELECTED:
		return "SELECTED";
	case XE_UC_FIRMWARE_MISSING:
		return "MISSING";
	case XE_UC_FIRMWARE_ERROR:
		return "ERROR";
	case XE_UC_FIRMWARE_AVAILABLE:
		return "AVAILABLE";
	case XE_UC_FIRMWARE_INIT_FAIL:
		return "INIT FAIL";
	case XE_UC_FIRMWARE_LOADABLE:
		return "LOADABLE";
	case XE_UC_FIRMWARE_LOAD_FAIL:
		return "LOAD FAIL";
	case XE_UC_FIRMWARE_TRANSFERRED:
		return "TRANSFERRED";
	case XE_UC_FIRMWARE_RUNNING:
		return "RUNNING";
	case XE_UC_FIRMWARE_PRELOADED:
		return "PRELOADED";
	}
	return "<invalid>";
}

static inline int xe_uc_fw_status_to_error(enum xe_uc_fw_status status)
{
	switch (status) {
	case XE_UC_FIRMWARE_NOT_SUPPORTED:
		return -ENODEV;
	case XE_UC_FIRMWARE_UNINITIALIZED:
		return -EACCES;
	case XE_UC_FIRMWARE_DISABLED:
		return -EPERM;
	case XE_UC_FIRMWARE_MISSING:
		return -ENOENT;
	case XE_UC_FIRMWARE_ERROR:
		return -ENOEXEC;
	case XE_UC_FIRMWARE_INIT_FAIL:
	case XE_UC_FIRMWARE_LOAD_FAIL:
		return -EIO;
	case XE_UC_FIRMWARE_SELECTED:
		return -ESTALE;
	case XE_UC_FIRMWARE_AVAILABLE:
	case XE_UC_FIRMWARE_LOADABLE:
	case XE_UC_FIRMWARE_TRANSFERRED:
	case XE_UC_FIRMWARE_RUNNING:
	case XE_UC_FIRMWARE_PRELOADED:
		return 0;
	}
	return -EINVAL;
}

static inline const char *xe_uc_fw_type_repr(enum xe_uc_fw_type type)
{
	switch (type) {
	case XE_UC_FW_TYPE_GUC:
		return "GuC";
	case XE_UC_FW_TYPE_HUC:
		return "HuC";
	case XE_UC_FW_TYPE_GSC:
		return "GSC";
	default:
		return "uC";
	}
}

static inline enum xe_uc_fw_status
__xe_uc_fw_status(struct xe_uc_fw *uc_fw)
{
	/* shouldn't call this before checking hw/blob availability */
	XE_WARN_ON(uc_fw->status == XE_UC_FIRMWARE_UNINITIALIZED);
	return uc_fw->status;
}

static inline bool xe_uc_fw_is_supported(struct xe_uc_fw *uc_fw)
{
	return __xe_uc_fw_status(uc_fw) != XE_UC_FIRMWARE_NOT_SUPPORTED;
}

static inline bool xe_uc_fw_is_enabled(struct xe_uc_fw *uc_fw)
{
	return __xe_uc_fw_status(uc_fw) > XE_UC_FIRMWARE_DISABLED;
}

static inline bool xe_uc_fw_is_disabled(struct xe_uc_fw *uc_fw)
{
	return __xe_uc_fw_status(uc_fw) == XE_UC_FIRMWARE_DISABLED;
}

static inline bool xe_uc_fw_is_available(struct xe_uc_fw *uc_fw)
{
	return __xe_uc_fw_status(uc_fw) >= XE_UC_FIRMWARE_AVAILABLE;
}

static inline bool xe_uc_fw_is_loadable(struct xe_uc_fw *uc_fw)
{
	return __xe_uc_fw_status(uc_fw) >= XE_UC_FIRMWARE_LOADABLE &&
		__xe_uc_fw_status(uc_fw) != XE_UC_FIRMWARE_PRELOADED;
}

static inline bool xe_uc_fw_is_loaded(struct xe_uc_fw *uc_fw)
{
	return __xe_uc_fw_status(uc_fw) >= XE_UC_FIRMWARE_TRANSFERRED;
}

static inline bool xe_uc_fw_is_running(struct xe_uc_fw *uc_fw)
{
	return __xe_uc_fw_status(uc_fw) >= XE_UC_FIRMWARE_RUNNING;
}

static inline bool xe_uc_fw_is_overridden(const struct xe_uc_fw *uc_fw)
{
	return uc_fw->user_overridden;
}

static inline void xe_uc_fw_sanitize(struct xe_uc_fw *uc_fw)
{
	if (xe_uc_fw_is_loadable(uc_fw))
		xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_LOADABLE);
}

static inline u32 __xe_uc_fw_get_upload_size(struct xe_uc_fw *uc_fw)
{
	return sizeof(struct uc_css_header) + uc_fw->ucode_size;
}

/**
 * xe_uc_fw_get_upload_size() - Get size of firmware needed to be uploaded.
 * @uc_fw: uC firmware.
 *
 * Get the size of the firmware and header that will be uploaded to WOPCM.
 *
 * Return: Upload firmware size, or zero on firmware fetch failure.
 */
static inline u32 xe_uc_fw_get_upload_size(struct xe_uc_fw *uc_fw)
{
	if (!xe_uc_fw_is_available(uc_fw))
		return 0;

	return __xe_uc_fw_get_upload_size(uc_fw);
}

#define XE_UC_FIRMWARE_URL "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git"

#endif
