/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_FWIF_SHARED_H
#define PVR_ROGUE_FWIF_SHARED_H

#include <linux/compiler.h>
#include <linux/types.h>

#define ROGUE_FWIF_NUM_RTDATAS 2U
#define ROGUE_FWIF_NUM_GEOMDATAS 1U
#define ROGUE_FWIF_NUM_RTDATA_FREELISTS 2U
#define ROGUE_NUM_GEOM_CORES 1U

#define ROGUE_NUM_GEOM_CORES_SIZE 2U

/*
 * Maximum number of UFOs in a CCB command.
 * The number is based on having 32 sync prims (as originally), plus 32 sync
 * checkpoints.
 * Once the use of sync prims is no longer supported, we will retain
 * the same total (64) as the number of sync checkpoints which may be
 * supporting a fence is not visible to the client driver and has to
 * allow for the number of different timelines involved in fence merges.
 */
#define ROGUE_FWIF_CCB_CMD_MAX_UFOS (32U + 32U)

/*
 * This is a generic limit imposed on any DM (GEOMETRY,FRAGMENT,CDM,TDM,2D,TRANSFER)
 * command passed through the bridge.
 * Just across the bridge in the server, any incoming kick command size is
 * checked against this maximum limit.
 * In case the incoming command size is larger than the specified limit,
 * the bridge call is retired with error.
 */
#define ROGUE_FWIF_DM_INDEPENDENT_KICK_CMD_SIZE (1024U)

#define ROGUE_FWIF_PRBUFFER_START (0)
#define ROGUE_FWIF_PRBUFFER_ZSBUFFER (0)
#define ROGUE_FWIF_PRBUFFER_MSAABUFFER (1)
#define ROGUE_FWIF_PRBUFFER_MAXSUPPORTED (2)

struct rogue_fwif_dma_addr {
	aligned_u64 dev_addr;
	u32 fw_addr;
	u32 padding;
} __aligned(8);

struct rogue_fwif_ufo {
	u32 addr;
	u32 value;
};

#define ROGUE_FWIF_UFO_ADDR_IS_SYNC_CHECKPOINT (1)

struct rogue_fwif_sync_checkpoint {
	u32 state;
	u32 fw_ref_count;
};

struct rogue_fwif_cleanup_ctl {
	/* Number of commands received by the FW */
	u32 submitted_commands;
	/* Number of commands executed by the FW */
	u32 executed_commands;
} __aligned(8);

/*
 * Used to share frame numbers across UM-KM-FW,
 * frame number is set in UM,
 * frame number is required in both KM for HTB and FW for FW trace.
 *
 * May be used to house Kick flags in the future.
 */
struct rogue_fwif_cmd_common {
	/* associated frame number */
	u32 frame_num;
};

/*
 * Geometry and fragment commands require set of firmware addresses that are stored in the Kernel.
 * Client has handle(s) to Kernel containers storing these addresses, instead of raw addresses. We
 * have to patch/write these addresses in KM to prevent UM from controlling FW addresses directly.
 * Typedefs for geometry and fragment commands are shared between Client and Firmware (both
 * single-BVNC). Kernel is implemented in a multi-BVNC manner, so it can't use geometry|fragment
 * CMD type definitions directly. Therefore we have a SHARED block that is shared between UM-KM-FW
 * across all BVNC configurations.
 */
struct rogue_fwif_cmd_geom_frag_shared {
	/* Common command attributes */
	struct rogue_fwif_cmd_common cmn;

	/*
	 * RTData associated with this command, this is used for context
	 * selection and for storing out HW-context, when TA is switched out for
	 * continuing later
	 */
	u32 hwrt_data_fw_addr;

	/* Supported PR Buffers like Z/S/MSAA Scratch */
	u32 pr_buffer_fw_addr[ROGUE_FWIF_PRBUFFER_MAXSUPPORTED];
};

/*
 * Client Circular Command Buffer (CCCB) control structure.
 * This is shared between the Server and the Firmware and holds byte offsets
 * into the CCCB as well as the wrapping mask to aid wrap around. A given
 * snapshot of this queue with Cmd 1 running on the GPU might be:
 *
 *          Roff                           Doff                 Woff
 * [..........|-1----------|=2===|=3===|=4===|~5~~~~|~6~~~~|~7~~~~|..........]
 *            <      runnable commands       ><   !ready to run   >
 *
 * Cmd 1    : Currently executing on the GPU data master.
 * Cmd 2,3,4: Fence dependencies met, commands runnable.
 * Cmd 5... : Fence dependency not met yet.
 */
struct rogue_fwif_cccb_ctl {
	/* Host write offset into CCB. This must be aligned to 16 bytes. */
	u32 write_offset;
	/*
	 * Firmware read offset into CCB. Points to the command that is runnable
	 * on GPU, if R!=W
	 */
	u32 read_offset;
	/*
	 * Firmware fence dependency offset. Points to commands not ready, i.e.
	 * fence dependencies are not met.
	 */
	u32 dep_offset;
	/* Offset wrapping mask, total capacity in bytes of the CCB-1 */
	u32 wrap_mask;

	/* Only used if SUPPORT_AGP is present. */
	u32 read_offset2;

	/* Only used if SUPPORT_AGP4 is present. */
	u32 read_offset3;
	/* Only used if SUPPORT_AGP4 is present. */
	u32 read_offset4;

	u32 padding;
} __aligned(8);

#define ROGUE_FW_LOCAL_FREELIST (0)
#define ROGUE_FW_GLOBAL_FREELIST (1)
#define ROGUE_FW_FREELIST_TYPE_LAST ROGUE_FW_GLOBAL_FREELIST
#define ROGUE_FW_MAX_FREELISTS (ROGUE_FW_FREELIST_TYPE_LAST + 1U)

struct rogue_fwif_geom_registers_caswitch {
	u64 geom_reg_vdm_context_state_base_addr;
	u64 geom_reg_vdm_context_state_resume_addr;
	u64 geom_reg_ta_context_state_base_addr;

	struct {
		u64 geom_reg_vdm_context_store_task0;
		u64 geom_reg_vdm_context_store_task1;
		u64 geom_reg_vdm_context_store_task2;

		/* VDM resume state update controls */
		u64 geom_reg_vdm_context_resume_task0;
		u64 geom_reg_vdm_context_resume_task1;
		u64 geom_reg_vdm_context_resume_task2;

		u64 geom_reg_vdm_context_store_task3;
		u64 geom_reg_vdm_context_store_task4;

		u64 geom_reg_vdm_context_resume_task3;
		u64 geom_reg_vdm_context_resume_task4;
	} geom_state[2];
};

#define ROGUE_FWIF_GEOM_REGISTERS_CSWITCH_SIZE \
	sizeof(struct rogue_fwif_geom_registers_caswitch)

struct rogue_fwif_cdm_registers_cswitch {
	u64 cdmreg_cdm_context_pds0;
	u64 cdmreg_cdm_context_pds1;
	u64 cdmreg_cdm_terminate_pds;
	u64 cdmreg_cdm_terminate_pds1;

	/* CDM resume controls */
	u64 cdmreg_cdm_resume_pds0;
	u64 cdmreg_cdm_context_pds0_b;
	u64 cdmreg_cdm_resume_pds0_b;
};

struct rogue_fwif_static_rendercontext_state {
	/* Geom registers for ctx switch */
	struct rogue_fwif_geom_registers_caswitch ctxswitch_regs[ROGUE_NUM_GEOM_CORES_SIZE]
		__aligned(8);
};

#define ROGUE_FWIF_STATIC_RENDERCONTEXT_SIZE \
	sizeof(struct rogue_fwif_static_rendercontext_state)

struct rogue_fwif_static_computecontext_state {
	/* CDM registers for ctx switch */
	struct rogue_fwif_cdm_registers_cswitch ctxswitch_regs __aligned(8);
};

#define ROGUE_FWIF_STATIC_COMPUTECONTEXT_SIZE \
	sizeof(struct rogue_fwif_static_computecontext_state)

enum rogue_fwif_prbuffer_state {
	ROGUE_FWIF_PRBUFFER_UNBACKED = 0,
	ROGUE_FWIF_PRBUFFER_BACKED,
	ROGUE_FWIF_PRBUFFER_BACKING_PENDING,
	ROGUE_FWIF_PRBUFFER_UNBACKING_PENDING,
};

struct rogue_fwif_prbuffer {
	/* Buffer ID*/
	u32 buffer_id;
	/* Needs On-demand Z/S/MSAA Buffer allocation */
	bool on_demand __aligned(4);
	/* Z/S/MSAA -Buffer state */
	enum rogue_fwif_prbuffer_state state;
	/* Cleanup state */
	struct rogue_fwif_cleanup_ctl cleanup_sate;
	/* Compatibility and other flags */
	u32 prbuffer_flags;
} __aligned(8);

/* Last reset reason for a context. */
enum rogue_context_reset_reason {
	/* No reset reason recorded */
	ROGUE_CONTEXT_RESET_REASON_NONE = 0,
	/* Caused a reset due to locking up */
	ROGUE_CONTEXT_RESET_REASON_GUILTY_LOCKUP = 1,
	/* Affected by another context locking up */
	ROGUE_CONTEXT_RESET_REASON_INNOCENT_LOCKUP = 2,
	/* Overran the global deadline */
	ROGUE_CONTEXT_RESET_REASON_GUILTY_OVERRUNING = 3,
	/* Affected by another context overrunning */
	ROGUE_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING = 4,
	/* Forced reset to ensure scheduling requirements */
	ROGUE_CONTEXT_RESET_REASON_HARD_CONTEXT_SWITCH = 5,
	/* FW Safety watchdog triggered */
	ROGUE_CONTEXT_RESET_REASON_FW_WATCHDOG = 12,
	/* FW page fault (no HWR) */
	ROGUE_CONTEXT_RESET_REASON_FW_PAGEFAULT = 13,
	/* FW execution error (GPU reset requested) */
	ROGUE_CONTEXT_RESET_REASON_FW_EXEC_ERR = 14,
	/* Host watchdog detected FW error */
	ROGUE_CONTEXT_RESET_REASON_HOST_WDG_FW_ERR = 15,
	/* Geometry DM OOM event is not allowed */
	ROGUE_CONTEXT_GEOM_OOM_DISABLED = 16,
};

struct rogue_context_reset_reason_data {
	enum rogue_context_reset_reason reset_reason;
	u32 reset_ext_job_ref;
};

#include "pvr_rogue_fwif_shared_check.h"

#endif /* PVR_ROGUE_FWIF_SHARED_H */
