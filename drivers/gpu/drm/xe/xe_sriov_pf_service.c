// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2025 Intel Corporation
 */

#include "abi/guc_relay_actions_abi.h"

#include "xe_device_types.h"
#include "xe_sriov.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_printk.h"

#include "xe_sriov_pf_service.h"
#include "xe_sriov_pf_service_types.h"

/**
 * xe_sriov_pf_service_init - Early initialization of the SR-IOV PF service.
 * @xe: the &xe_device to initialize
 *
 * Performs early initialization of the SR-IOV PF service.
 *
 * This function can only be called on PF.
 */
void xe_sriov_pf_service_init(struct xe_device *xe)
{
	BUILD_BUG_ON(!GUC_RELAY_VERSION_BASE_MAJOR && !GUC_RELAY_VERSION_BASE_MINOR);
	BUILD_BUG_ON(GUC_RELAY_VERSION_BASE_MAJOR > GUC_RELAY_VERSION_LATEST_MAJOR);

	xe_assert(xe, IS_SRIOV_PF(xe));

	/* base versions may differ between platforms */
	xe->sriov.pf.service.version.base.major = GUC_RELAY_VERSION_BASE_MAJOR;
	xe->sriov.pf.service.version.base.minor = GUC_RELAY_VERSION_BASE_MINOR;

	/* latest version is same for all platforms */
	xe->sriov.pf.service.version.latest.major = GUC_RELAY_VERSION_LATEST_MAJOR;
	xe->sriov.pf.service.version.latest.minor = GUC_RELAY_VERSION_LATEST_MINOR;
}

/* Return: 0 on success or a negative error code on failure. */
static int pf_negotiate_version(struct xe_device *xe,
				u32 wanted_major, u32 wanted_minor,
				u32 *major, u32 *minor)
{
	struct xe_sriov_pf_service_version base = xe->sriov.pf.service.version.base;
	struct xe_sriov_pf_service_version latest = xe->sriov.pf.service.version.latest;

	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, base.major);
	xe_assert(xe, base.major <= latest.major);
	xe_assert(xe, (base.major < latest.major) || (base.minor <= latest.minor));

	/* VF doesn't care - return our latest  */
	if (wanted_major == VF2PF_HANDSHAKE_MAJOR_ANY &&
	    wanted_minor == VF2PF_HANDSHAKE_MINOR_ANY) {
		*major = latest.major;
		*minor = latest.minor;
		return 0;
	}

	/* VF wants newer than our - return our latest  */
	if (wanted_major > latest.major) {
		*major = latest.major;
		*minor = latest.minor;
		return 0;
	}

	/* VF wants older than min required - reject */
	if (wanted_major < base.major ||
	    (wanted_major == base.major && wanted_minor < base.minor)) {
		return -EPERM;
	}

	/* previous major - return wanted, as we should still support it */
	if (wanted_major < latest.major) {
		/* XXX: we are not prepared for multi-versions yet */
		xe_assert(xe, base.major == latest.major);
		return -ENOPKG;
	}

	/* same major - return common minor */
	*major = wanted_major;
	*minor = min_t(u32, latest.minor, wanted_minor);
	return 0;
}

static void pf_connect(struct xe_device *xe, u32 vfid, u32 major, u32 minor)
{
	xe_sriov_pf_assert_vfid(xe, vfid);
	xe_assert(xe, major || minor);

	xe->sriov.pf.vfs[vfid].version.major = major;
	xe->sriov.pf.vfs[vfid].version.minor = minor;
}

static void pf_disconnect(struct xe_device *xe, u32 vfid)
{
	xe_sriov_pf_assert_vfid(xe, vfid);

	xe->sriov.pf.vfs[vfid].version.major = 0;
	xe->sriov.pf.vfs[vfid].version.minor = 0;
}

/**
 * xe_sriov_pf_service_is_negotiated - Check if VF has negotiated given ABI version.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @major: the major version to check
 * @minor: the minor version to check
 *
 * Performs early initialization of the SR-IOV PF service.
 *
 * This function can only be called on PF.
 *
 * Returns: true if VF can use given ABI version functionality.
 */
bool xe_sriov_pf_service_is_negotiated(struct xe_device *xe, u32 vfid, u32 major, u32 minor)
{
	xe_sriov_pf_assert_vfid(xe, vfid);

	return major == xe->sriov.pf.vfs[vfid].version.major &&
	       minor <= xe->sriov.pf.vfs[vfid].version.minor;
}

/**
 * xe_sriov_pf_service_handshake_vf - Confirm a connection with the VF.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @wanted_major: the major service version expected by the VF
 * @wanted_minor: the minor service version expected by the VF
 * @major: the major service version to be used by the VF
 * @minor: the minor service version to be used by the VF
 *
 * Negotiate a VF/PF ABI version to allow VF use the PF services.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_service_handshake_vf(struct xe_device *xe, u32 vfid,
				     u32 wanted_major, u32 wanted_minor,
				     u32 *major, u32 *minor)
{
	int err;

	xe_sriov_dbg_verbose(xe, "VF%u wants ABI version %u.%u\n",
			     vfid, wanted_major, wanted_minor);

	err = pf_negotiate_version(xe, wanted_major, wanted_minor, major, minor);

	if (err < 0) {
		xe_sriov_notice(xe, "VF%u failed to negotiate ABI %u.%u (%pe)\n",
				vfid, wanted_major, wanted_minor, ERR_PTR(err));
		pf_disconnect(xe, vfid);
	} else {
		xe_sriov_dbg(xe, "VF%u negotiated ABI version %u.%u\n",
			     vfid, *major, *minor);
		pf_connect(xe, vfid, *major, *minor);
	}

	return err;
}

/**
 * xe_sriov_pf_service_reset_vf - Reset a connection with the VF.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * Reset a VF driver negotiated VF/PF ABI version.
 *
 * After that point, the VF driver will have to perform new version handshake
 * to continue use of the PF services again.
 *
 * This function can only be called on PF.
 */
void xe_sriov_pf_service_reset_vf(struct xe_device *xe, unsigned int vfid)
{
	pf_disconnect(xe, vfid);
}

static void print_pf_version(struct drm_printer *p, const char *name,
			     const struct xe_sriov_pf_service_version *version)
{
	drm_printf(p, "%s:\t%u.%u\n", name, version->major, version->minor);
}

/**
 * xe_sriov_pf_service_print_versions - Print ABI versions negotiated with VFs.
 * @xe: the &xe_device
 * @p: the &drm_printer
 *
 * This function is for PF use only.
 */
void xe_sriov_pf_service_print_versions(struct xe_device *xe, struct drm_printer *p)
{
	unsigned int n, total_vfs = xe_sriov_pf_get_totalvfs(xe);
	struct xe_sriov_pf_service_version *version;
	char name[8];

	xe_assert(xe, IS_SRIOV_PF(xe));

	print_pf_version(p, "base", &xe->sriov.pf.service.version.base);
	print_pf_version(p, "latest", &xe->sriov.pf.service.version.latest);

	for (n = 1; n <= total_vfs; n++) {
		version = &xe->sriov.pf.vfs[n].version;
		if (!version->major && !version->minor)
			continue;

		print_pf_version(p, xe_sriov_function_name(n, name, sizeof(name)), version);
	}
}

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_sriov_pf_service_kunit.c"
#endif
