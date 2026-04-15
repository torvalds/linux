// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright (c) 2026 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_dump.h"
#include "pvr_rogue_fwif.h"

#include <drm/drm_print.h>
#include <linux/types.h>

static const char *
get_reset_reason_desc(enum rogue_context_reset_reason reason)
{
	switch (reason) {
	case ROGUE_CONTEXT_RESET_REASON_NONE:
		return "None";
	case ROGUE_CONTEXT_RESET_REASON_GUILTY_LOCKUP:
		return "Guilty lockup";
	case ROGUE_CONTEXT_RESET_REASON_INNOCENT_LOCKUP:
		return "Innocent lockup";
	case ROGUE_CONTEXT_RESET_REASON_GUILTY_OVERRUNING:
		return "Guilty overrunning";
	case ROGUE_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING:
		return "Innocent overrunning";
	case ROGUE_CONTEXT_RESET_REASON_HARD_CONTEXT_SWITCH:
		return "Hard context switch";
	case ROGUE_CONTEXT_RESET_REASON_WGP_CHECKSUM:
		return "CDM Mission/safety checksum mismatch";
	case ROGUE_CONTEXT_RESET_REASON_TRP_CHECKSUM:
		return "TRP checksum mismatch";
	case ROGUE_CONTEXT_RESET_REASON_GPU_ECC_OK:
		return "GPU ECC error (corrected, OK)";
	case ROGUE_CONTEXT_RESET_REASON_GPU_ECC_HWR:
		return "GPU ECC error (uncorrected, HWR)";
	case ROGUE_CONTEXT_RESET_REASON_FW_ECC_OK:
		return "Firmware ECC error (corrected, OK)";
	case ROGUE_CONTEXT_RESET_REASON_FW_ECC_ERR:
		return "Firmware ECC error (uncorrected, ERR)";
	case ROGUE_CONTEXT_RESET_REASON_FW_WATCHDOG:
		return "Firmware watchdog";
	case ROGUE_CONTEXT_RESET_REASON_FW_PAGEFAULT:
		return "Firmware pagefault";
	case ROGUE_CONTEXT_RESET_REASON_FW_EXEC_ERR:
		return "Firmware execution error";
	case ROGUE_CONTEXT_RESET_REASON_HOST_WDG_FW_ERR:
		return "Host watchdog";
	case ROGUE_CONTEXT_GEOM_OOM_DISABLED:
		return "Geometry OOM disabled";

	default:
		return "Unknown";
	}
}

static const char *
get_dm_name(u32 dm)
{
	switch (dm) {
	case PVR_FWIF_DM_GP:
		return "General purpose";
	/* PVR_FWIF_DM_TDM has the same index, but is discriminated by a device feature */
	case PVR_FWIF_DM_2D:
		return "2D or TDM";
	case PVR_FWIF_DM_GEOM:
		return "Geometry";
	case PVR_FWIF_DM_FRAG:
		return "Fragment";
	case PVR_FWIF_DM_CDM:
		return "Compute";
	case PVR_FWIF_DM_RAY:
		return "Raytracing";
	case PVR_FWIF_DM_GEOM2:
		return "Geometry 2";
	case PVR_FWIF_DM_GEOM3:
		return "Geometry 3";
	case PVR_FWIF_DM_GEOM4:
		return "Geometry 4";

	default:
		return "Unknown";
	}
}

/**
 * pvr_dump_context_reset_notification() - Handle context reset notification from FW
 * @pvr_dev: Device pointer.
 * @data: Data provided by FW.
 *
 * This will decode the data structure provided by FW and print the results via drm_info().
 */
void
pvr_dump_context_reset_notification(struct pvr_device *pvr_dev,
				    struct rogue_fwif_fwccb_cmd_context_reset_data *data)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);

	if (data->flags & ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_ALL_CTXS) {
		drm_info(drm_dev, "Received context reset notification for all contexts\n");
	} else {
		drm_info(drm_dev, "Received context reset notification on context %u\n",
			 data->server_common_context_id);
	}

	drm_info(drm_dev, "  Reset reason=%u (%s)\n", data->reset_reason,
		 get_reset_reason_desc((enum rogue_context_reset_reason)data->reset_reason));
	drm_info(drm_dev, "  Data Master=%u (%s)\n", data->dm, get_dm_name(data->dm));
	drm_info(drm_dev, "  Job ref=%u\n", data->reset_job_ref);

	if (data->flags & ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_PF) {
		drm_info(drm_dev, "  Page fault occurred, fault address=%llx\n",
			 data->fault_address);
	}
}
