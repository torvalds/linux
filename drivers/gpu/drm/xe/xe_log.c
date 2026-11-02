// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <kunit/static_stub.h>
#include <kunit/visibility.h>

#include "abi/xe_log_abi.h"

#include "xe_device.h"
#include "xe_log.h"
#include "xe_pci_types.h"
#include "xe_printk.h"

static void log_emit_cper(struct pci_dev *pdev, int cper_sev, enum xe_sigid sigid,
			  u32 component, u32 location, const void *data, size_t len,
			  struct va_format *vaf)
{
	KUNIT_STATIC_STUB_REDIRECT(log_emit_cper, pdev, cper_sev, sigid,
				   component, location, data, len, vaf);
	/* TODO */
}

static const char *log_unknown_component_prefix(u32 component)
{
	u32 class = FIELD_GET(XE_LOG_COMPONENT_CLASS_MASK, component);
	u32 type = FIELD_GET(XE_LOG_COMPONENT_TYPE_MASK, component);

	WARN(IS_ENABLED(CONFIG_DRM_XE_DEBUG), "LOG: unrecognized component %u.%u\n", class, type);
	switch (class) {
#define MAKE_XE_LOG_COMPONENT_CLASS_PREFIX(_CLASS) \
	case XE_LOG_COMPONENT_CLASS_##_CLASS: return #_CLASS "? ";
	MAKE_XE_LOG_COMPONENT_CLASS_PREFIX(SYSTEM)
	MAKE_XE_LOG_COMPONENT_CLASS_PREFIX(DRIVER)
	MAKE_XE_LOG_COMPONENT_CLASS_PREFIX(FEATURE)
	MAKE_XE_LOG_COMPONENT_CLASS_PREFIX(FIRMWARE)
	MAKE_XE_LOG_COMPONENT_CLASS_PREFIX(HARDWARE)
#undef MAKE_XE_LOG_COMPONENT_CLASS_PREFIX
	}
	return "COMP? ";
}

static const char *log_component_prefix(u32 component)
{
	switch (component) {
#define MAKE_XE_LOG_COMPONENT_CASE_PREFIX(_CLASS, _ID, _TAG, _SIG, _NAME) \
	case XE_LOG_COMPONENT_##_TAG: return #_TAG ": ";
	DEFINE_XE_LOG_COMPONENTS(MAKE_XE_LOG_COMPONENT_CASE_PREFIX)
#undef MAKE_XE_LOG_COMPONENT_CASE_PREFIX
	}

	return component ? log_unknown_component_prefix(component) : "";
}

static bool allowed_tile_id(struct xe_device *xe, u8 tile_id)
{
	return tile_id < 1 + xe->desc->max_remote_tiles;
}

static bool allowed_gt_id(struct xe_device *xe, u8 gt_id)
{
	return gt_id < (1 + xe->desc->max_remote_tiles) * xe->desc->max_gt_per_tile;
}

static u8 gt_id_to_tile_id(struct xe_device *xe, u8 gt_id)
{
	return gt_id / xe->desc->max_gt_per_tile;
}

static const char *location_suffix(bool valid)
{
	return valid ? ":" : "?";
}

static const char *log_location_prefix(struct pci_dev *pdev, u32 location, char *buf, size_t size)
{
	u32 type = FIELD_GET(XE_LOG_LOCATION_TYPE_MASK, location);
	u32 id = FIELD_GET(XE_LOG_LOCATION_ID_MASK, location);

	if (!location || type == XE_LOG_LOCATION_TYPE_DEVICE) {
		if (id)
			goto unrecognized;
		strscpy(buf, "", size);
	} else if (type == XE_LOG_LOCATION_TYPE_TILE) {
		struct xe_device *xe = xe_any_to_xe(pdev);
		bool valid = xe ? allowed_tile_id(xe, id) : false;
		const char *pad = location_suffix(valid);

		pci_WARN(pdev, !valid && IS_ENABLED(CONFIG_DRM_XE_DEBUG),
			 "LOG: invalid tile identifier: %u\n", id);
		snprintf(buf, size, "Tile%u%s ", id, pad);
	} else if (type == XE_LOG_LOCATION_TYPE_GT) {
		struct xe_device *xe = xe_any_to_xe(pdev);
		bool valid = xe ? allowed_gt_id(xe, id) : false;
		const char *pad = location_suffix(valid);
		u8 tile_id = xe ? gt_id_to_tile_id(xe, id) : 0;

		pci_WARN(pdev, !valid && IS_ENABLED(CONFIG_DRM_XE_DEBUG),
			 "LOG: invalid GT identifier: %u\n", id);
		snprintf(buf, size, "Tile%u%s GT%u%s ", tile_id, pad, id, pad);
	} else {
		goto unrecognized;
	}

	return buf;

unrecognized:
	pci_WARN(pdev, IS_ENABLED(CONFIG_DRM_XE_DEBUG),
		 "LOG: unrecognized location %u.%u\n", type, id);
	snprintf(buf, size, "LOC%u.%u? ", type, id);
	return buf;
}

static bool is_hw_sigid(enum xe_sigid sigid)
{
	return (int)sigid >= INTEL_SIGID_GPU_XE_HARDWARE_START;
}

static bool is_sev_error(int cper_sev)
{
	return cper_sev != CPER_SEV_INFORMATIONAL;
}

static const char *log_hwe_prefix(int cper_sev, enum xe_sigid sigid)
{
	return is_sev_error(cper_sev) && is_hw_sigid(sigid) ? HW_ERR : "";
}

static const char *log_sev_prefix(int cper_sev)
{
	switch (cper_sev) {
	case CPER_SEV_FATAL:
		return "FATAL ";
	case CPER_SEV_RECOVERABLE:
		return "";
	case CPER_SEV_CORRECTED:
		return "CORRECTED ";
	case CPER_SEV_INFORMATIONAL:
		return "";
	default:
		WARN(IS_ENABLED(CONFIG_DRM_XE_DEBUG), "LOG: unknown severity %d\n", cper_sev);
		return "";
	}
}

#define __LOG_DRM_PRINTK_FMT(fmt, args...)	"[drm] " fmt, ##args
#define __LOG_DRM_PRINTK_ERR_FMT(fmt, args...)	__LOG_DRM_PRINTK_FMT("*ERROR* " fmt, args)

static void log_dmesg_vprintk(struct pci_dev *pdev, int cper_sev, struct va_format *vaf)
{
	KUNIT_STATIC_STUB_REDIRECT(log_dmesg_vprintk, pdev, cper_sev, vaf);

	if (cper_sev == CPER_SEV_INFORMATIONAL)
		pci_info(pdev, __LOG_DRM_PRINTK_FMT("%pV", vaf));
	else
		pci_err(pdev, __LOG_DRM_PRINTK_ERR_FMT("%pV", vaf));
}

static void log_dmesg_printf(struct pci_dev *pdev, int cper_sev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	log_dmesg_vprintk(pdev, cper_sev, &vaf);

	va_end(args);
}

static void log_emit_dmesg(struct pci_dev *pdev, int cper_sev, enum xe_sigid sigid,
			   u32 component, u32 location, const void *data, size_t len,
			   struct va_format *vaf)
{
	char buf[32];
	const char *loc_prefix = log_location_prefix(pdev, location, buf, sizeof(buf));
	const char *comp_prefix = log_component_prefix(component);
	const char *hwe_prefix = log_hwe_prefix(cper_sev, sigid);
	const char *sev_prefix = log_sev_prefix(cper_sev);

	if (IS_ERR(data))
		log_dmesg_printf(pdev, cper_sev, "SIGID=%u %s(%pe) %s%s%s%pV",
				 sigid, sev_prefix, data, hwe_prefix,
				 loc_prefix, comp_prefix, vaf);
	else if (data && len)
		log_dmesg_printf(pdev, cper_sev, "SIGID=%u %s(%*phN) %s%s%s%pV",
				 sigid, sev_prefix, (int)len, data, hwe_prefix,
				 loc_prefix, comp_prefix, vaf);
	else
		log_dmesg_printf(pdev, cper_sev, "SIGID=%u %s%s%s%s%pV",
				 sigid, sev_prefix, hwe_prefix,
				 loc_prefix, comp_prefix, vaf);
}

/**
 * __xe_log_emit() - Emit a structured SIGID log entry
 * @pdev: the &pci_dev device
 * @cper_sev: CPER severity (CPER_SEV_FATAL, CPER_SEV_RECOVERABLE, ...)
 * @sigid: signature identifier, see &enum xe_sigid
 * @component: component identifier
 * @location: location details of the @component
 * @data: pointer to the additional details, or ERR_PTR, or NULL if not applicable
 * @len: length of the @data in bytes, or 0 if not applicable
 * @fmt: printf-style format string
 * @...: format arguments
 *
 * Emits a dmesg line that includes a single stable, machine-matchable token
 * ``SIGID=<n>`` followed by the optional severity token (like ``FATAL``) and,
 * when @data pointer is set, either the error printed with %pe or a packed hex
 * dump of the @data binary blob. The dmesg line will also include printf-style
 * text message.
 *
 * Note that the full dmesg line, with the free text message, is only a debugging
 * aid, not an interface! Only the ``SIGID=<n>`` token is stable there.
 * The durable machine record is the CPER carrying the same SIGID.
 *
 * Note: generation of the CPER record is a planned follow-up.
 *
 * Examples::
 *
 *   <3> xe 0000:03:00.0: [drm] *ERROR* SIGID=104 FATAL (-EPROTO) Invalid GuC reply
 *   <3> xe 0000:03:00.0: [drm] *ERROR* SIGID=106 (-ETIMEDOUT) Engine 'rcs0' hung
 *   <6> xe 0000:03:00.0: [drm] SIGID=103 In survivability mode
 */
void __xe_log_emit(struct pci_dev *pdev, int cper_sev, enum xe_sigid sigid,
		   u32 component, u32 location, const void *data, size_t len,
		   const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	log_emit_dmesg(pdev, cper_sev, sigid, component, location, data, len, &vaf);
	log_emit_cper(pdev, cper_sev, sigid, component, location, data, len, &vaf);

	va_end(args);
}

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_log_kunit.c"
#endif
