/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_UC_FW_TYPES_H_
#define _XE_UC_FW_TYPES_H_

#include <linux/types.h>

struct xe_bo;

/*
 * +------------+---------------------------------------------------+
 * |   PHASE    |           FIRMWARE STATUS TRANSITIONS             |
 * +============+===================================================+
 * |            |               UNINITIALIZED                       |
 * +------------+-               /   |   \                         -+
 * |            |   DISABLED <--/    |    \--> NOT_SUPPORTED        |
 * | init_early |                    V                              |
 * |            |                 SELECTED                          |
 * +------------+-               /   |   \                         -+
 * |            |    MISSING <--/    |    \--> ERROR                |
 * |   fetch    |                    V                              |
 * |            |                 AVAILABLE                         |
 * +------------+-                   |   \                         -+
 * |            |                    |    \--> INIT FAIL            |
 * |   init     |                    V                              |
 * |            |        /------> LOADABLE <----<-----------\       |
 * +------------+-       \         /    \        \           \     -+
 * |            |    LOAD FAIL <--<      \--> TRANSFERRED     \     |
 * |   upload   |                  \           /   \          /     |
 * |            |                   \---------/     \--> RUNNING    |
 * +------------+---------------------------------------------------+
 */

/*
 * FIXME: Ported from the i915 and this is state machine is way too complicated.
 * Circle back and simplify this.
 */
enum xe_uc_fw_status {
	XE_UC_FIRMWARE_NOT_SUPPORTED = -1, /* no uc HW */
	XE_UC_FIRMWARE_UNINITIALIZED = 0, /* used to catch checks done too early */
	XE_UC_FIRMWARE_DISABLED, /* disabled */
	XE_UC_FIRMWARE_SELECTED, /* selected the blob we want to load */
	XE_UC_FIRMWARE_MISSING, /* blob not found on the system */
	XE_UC_FIRMWARE_ERROR, /* invalid format or version */
	XE_UC_FIRMWARE_AVAILABLE, /* blob found and copied in mem */
	XE_UC_FIRMWARE_INIT_FAIL, /* failed to prepare fw objects for load */
	XE_UC_FIRMWARE_LOADABLE, /* all fw-required objects are ready */
	XE_UC_FIRMWARE_LOAD_FAIL, /* failed to xfer or init/auth the fw */
	XE_UC_FIRMWARE_TRANSFERRED, /* dma xfer done */
	XE_UC_FIRMWARE_RUNNING, /* init/auth done */
	XE_UC_FIRMWARE_PRELOADED, /* preloaded by the PF driver */
};

enum xe_uc_fw_type {
	XE_UC_FW_TYPE_GUC = 0,
	XE_UC_FW_TYPE_HUC,
	XE_UC_FW_TYPE_GSC,
	XE_UC_FW_NUM_TYPES
};

/**
 * struct xe_uc_fw_version - Version for XE micro controller firmware
 */
struct xe_uc_fw_version {
	/** @major: major version of the FW */
	u16 major;
	/** @minor: minor version of the FW */
	u16 minor;
	/** @patch: patch version of the FW */
	u16 patch;
	/** @build: build version of the FW (not always available) */
	u16 build;
};

enum xe_uc_fw_version_types {
	XE_UC_FW_VER_RELEASE,
	XE_UC_FW_VER_COMPATIBILITY,
	XE_UC_FW_VER_TYPE_COUNT
};

/**
 * struct xe_uc_fw - XE micro controller firmware
 */
struct xe_uc_fw {
	/** @type: type uC firmware */
	enum xe_uc_fw_type type;
	union {
		/** @status: firmware load status */
		const enum xe_uc_fw_status status;
		/**
		 * @__status: private firmware load status - only to be used
		 * by firmware laoding code
		 */
		enum xe_uc_fw_status __status;
	};
	/** @path: path to uC firmware */
	const char *path;
	/** @user_overridden: user provided path to uC firmware via modparam */
	bool user_overridden;
	/**
	 * @full_ver_required: driver still under development and not ready
	 * for backward-compatible firmware. To be used only for **new**
	 * platforms, i.e. still under require_force_probe protection and not
	 * supported by i915.
	 */
	bool full_ver_required;
	/** @size: size of uC firmware including css header */
	size_t size;

	/** @bo: XE BO for uC firmware */
	struct xe_bo *bo;

	/** @has_gsc_headers: whether the FW image starts with GSC headers */
	bool has_gsc_headers;

	/*
	 * The firmware build process will generate a version header file with
	 * major and minor version defined. The versions are built into CSS
	 * header of firmware. The xe kernel driver set the minimal firmware
	 * version required per platform.
	 */

	/** @versions: FW versions wanted and found */
	struct {
		/** @versions.wanted: firmware version wanted by platform */
		struct xe_uc_fw_version wanted;
		/**
		 * @versions.wanted_type: type of firmware version wanted
		 * (release vs compatibility)
		 */
		enum xe_uc_fw_version_types wanted_type;
		/** @versions.found: fw versions found in firmware blob */
		struct xe_uc_fw_version found[XE_UC_FW_VER_TYPE_COUNT];
	} versions;

	/** @rsa_size: RSA size */
	u32 rsa_size;
	/** @ucode_size: micro kernel size */
	u32 ucode_size;
	/** @css_offset: offset within the blob at which the CSS is located */
	u32 css_offset;

	/** @private_data_size: size of private data found in uC css header */
	u32 private_data_size;
};

#endif
