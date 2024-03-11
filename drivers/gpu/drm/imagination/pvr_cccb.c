// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_ccb.h"
#include "pvr_cccb.h"
#include "pvr_device.h"
#include "pvr_gem.h"
#include "pvr_hwrt.h"

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/types.h>

static __always_inline u32
get_ccb_space(u32 w_off, u32 r_off, u32 ccb_size)
{
	return (((r_off) - (w_off)) + ((ccb_size) - 1)) & ((ccb_size) - 1);
}

static void
cccb_ctrl_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_cccb_ctl *ctrl = cpu_ptr;
	struct pvr_cccb *pvr_cccb = priv;

	WRITE_ONCE(ctrl->write_offset, 0);
	WRITE_ONCE(ctrl->read_offset, 0);
	WRITE_ONCE(ctrl->dep_offset, 0);
	WRITE_ONCE(ctrl->wrap_mask, pvr_cccb->wrap_mask);
}

/**
 * pvr_cccb_init() - Initialise a Client CCB
 * @pvr_dev: Device pointer.
 * @pvr_cccb: Pointer to Client CCB structure to initialise.
 * @size_log2: Log2 size of Client CCB in bytes.
 * @name: Name of owner of Client CCB. Used for fence context.
 *
 * Return:
 *  * Zero on success, or
 *  * Any error code returned by pvr_fw_object_create_and_map().
 */
int
pvr_cccb_init(struct pvr_device *pvr_dev, struct pvr_cccb *pvr_cccb,
	      u32 size_log2, const char *name)
{
	size_t size = 1 << size_log2;
	int err;

	pvr_cccb->size = size;
	pvr_cccb->write_offset = 0;
	pvr_cccb->wrap_mask = size - 1;

	/*
	 * Map CCCB and control structure as uncached, so we don't have to flush
	 * CPU cache repeatedly when polling for space.
	 */
	pvr_cccb->ctrl = pvr_fw_object_create_and_map(pvr_dev, sizeof(*pvr_cccb->ctrl),
						      PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						      cccb_ctrl_init, pvr_cccb,
						      &pvr_cccb->ctrl_obj);
	if (IS_ERR(pvr_cccb->ctrl))
		return PTR_ERR(pvr_cccb->ctrl);

	pvr_cccb->cccb = pvr_fw_object_create_and_map(pvr_dev, size,
						      PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						      NULL, NULL, &pvr_cccb->cccb_obj);
	if (IS_ERR(pvr_cccb->cccb)) {
		err = PTR_ERR(pvr_cccb->cccb);
		goto err_free_ctrl;
	}

	pvr_fw_object_get_fw_addr(pvr_cccb->ctrl_obj, &pvr_cccb->ctrl_fw_addr);
	pvr_fw_object_get_fw_addr(pvr_cccb->cccb_obj, &pvr_cccb->cccb_fw_addr);

	return 0;

err_free_ctrl:
	pvr_fw_object_unmap_and_destroy(pvr_cccb->ctrl_obj);

	return err;
}

/**
 * pvr_cccb_fini() - Release Client CCB structure
 * @pvr_cccb: Client CCB to release.
 */
void
pvr_cccb_fini(struct pvr_cccb *pvr_cccb)
{
	pvr_fw_object_unmap_and_destroy(pvr_cccb->cccb_obj);
	pvr_fw_object_unmap_and_destroy(pvr_cccb->ctrl_obj);
}

/**
 * pvr_cccb_cmdseq_fits() - Check if a command sequence fits in the CCCB
 * @pvr_cccb: Target Client CCB.
 * @size: Size of the command sequence.
 *
 * Check if a command sequence fits in the CCCB we have at hand.
 *
 * Return:
 *  * true if the command sequence fits in the CCCB, or
 *  * false otherwise.
 */
bool pvr_cccb_cmdseq_fits(struct pvr_cccb *pvr_cccb, size_t size)
{
	struct rogue_fwif_cccb_ctl *ctrl = pvr_cccb->ctrl;
	u32 read_offset, remaining;
	bool fits = false;

	read_offset = READ_ONCE(ctrl->read_offset);
	remaining = pvr_cccb->size - pvr_cccb->write_offset;

	/* Always ensure we have enough room for a padding command at the end of the CCCB.
	 * If our command sequence does not fit, reserve the remaining space for a padding
	 * command.
	 */
	if (size + PADDING_COMMAND_SIZE > remaining)
		size += remaining;

	if (get_ccb_space(pvr_cccb->write_offset, read_offset, pvr_cccb->size) >= size)
		fits = true;

	return fits;
}

/**
 * pvr_cccb_write_command_with_header() - Write a command + command header to a
 *                                        Client CCB
 * @pvr_cccb: Target Client CCB.
 * @cmd_type: Client CCB command type. Must be one of %ROGUE_FWIF_CCB_CMD_TYPE_*.
 * @cmd_size: Size of command in bytes.
 * @cmd_data: Pointer to command to write.
 * @ext_job_ref: External job reference.
 * @int_job_ref: Internal job reference.
 *
 * Caller must make sure there's enough space in CCCB to queue this command. This
 * can be done by calling pvr_cccb_cmdseq_fits().
 *
 * This function is not protected by any lock. The caller must ensure there's
 * no concurrent caller, which should be guaranteed by the drm_sched model (job
 * submission is serialized in drm_sched_main()).
 */
void
pvr_cccb_write_command_with_header(struct pvr_cccb *pvr_cccb, u32 cmd_type, u32 cmd_size,
				   void *cmd_data, u32 ext_job_ref, u32 int_job_ref)
{
	u32 sz_with_hdr = pvr_cccb_get_size_of_cmd_with_hdr(cmd_size);
	struct rogue_fwif_ccb_cmd_header cmd_header = {
		.cmd_type = cmd_type,
		.cmd_size = ALIGN(cmd_size, 8),
		.ext_job_ref = ext_job_ref,
		.int_job_ref = int_job_ref,
	};
	struct rogue_fwif_cccb_ctl *ctrl = pvr_cccb->ctrl;
	u32 remaining = pvr_cccb->size - pvr_cccb->write_offset;
	u32 required_size, cccb_space, read_offset;

	/*
	 * Always ensure we have enough room for a padding command at the end of
	 * the CCCB.
	 */
	if (remaining < sz_with_hdr + PADDING_COMMAND_SIZE) {
		/*
		 * Command would need to wrap, so we need to pad the remainder
		 * of the CCCB.
		 */
		required_size = sz_with_hdr + remaining;
	} else {
		required_size = sz_with_hdr;
	}

	read_offset = READ_ONCE(ctrl->read_offset);
	cccb_space = get_ccb_space(pvr_cccb->write_offset, read_offset, pvr_cccb->size);
	if (WARN_ON(cccb_space < required_size))
		return;

	if (required_size != sz_with_hdr) {
		/* Add padding command */
		struct rogue_fwif_ccb_cmd_header pad_cmd = {
			.cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_PADDING,
			.cmd_size = remaining - sizeof(pad_cmd),
		};

		memcpy(&pvr_cccb->cccb[pvr_cccb->write_offset], &pad_cmd, sizeof(pad_cmd));
		pvr_cccb->write_offset = 0;
	}

	memcpy(&pvr_cccb->cccb[pvr_cccb->write_offset], &cmd_header, sizeof(cmd_header));
	memcpy(&pvr_cccb->cccb[pvr_cccb->write_offset + sizeof(cmd_header)], cmd_data, cmd_size);
	pvr_cccb->write_offset += sz_with_hdr;
}

static void fill_cmd_kick_data(struct pvr_cccb *cccb, u32 ctx_fw_addr,
			       struct pvr_hwrt_data *hwrt,
			       struct rogue_fwif_kccb_cmd_kick_data *k)
{
	k->context_fw_addr = ctx_fw_addr;
	k->client_woff_update = cccb->write_offset;
	k->client_wrap_mask_update = cccb->wrap_mask;

	if (hwrt) {
		u32 cleanup_state_offset = offsetof(struct rogue_fwif_hwrtdata, cleanup_state);

		pvr_fw_object_get_fw_addr_offset(hwrt->fw_obj, cleanup_state_offset,
						 &k->cleanup_ctl_fw_addr[k->num_cleanup_ctl++]);
	}
}

/**
 * pvr_cccb_send_kccb_kick: Send KCCB kick to trigger command processing
 * @pvr_dev: Device pointer.
 * @pvr_cccb: Pointer to CCCB to process.
 * @cctx_fw_addr: FW virtual address for context owning this Client CCB.
 * @hwrt: HWRT data set associated with this kick. May be %NULL.
 *
 * You must call pvr_kccb_reserve_slot() and wait for the returned fence to
 * signal (if this function didn't return NULL) before calling
 * pvr_cccb_send_kccb_kick().
 */
void
pvr_cccb_send_kccb_kick(struct pvr_device *pvr_dev,
			struct pvr_cccb *pvr_cccb, u32 cctx_fw_addr,
			struct pvr_hwrt_data *hwrt)
{
	struct rogue_fwif_kccb_cmd cmd_kick = {
		.cmd_type = ROGUE_FWIF_KCCB_CMD_KICK,
	};

	fill_cmd_kick_data(pvr_cccb, cctx_fw_addr, hwrt, &cmd_kick.cmd_data.cmd_kick_data);

	/* Make sure the writes to the CCCB are flushed before sending the KICK. */
	wmb();

	pvr_kccb_send_cmd_reserved_powered(pvr_dev, &cmd_kick, NULL);
}

void
pvr_cccb_send_kccb_combined_kick(struct pvr_device *pvr_dev,
				 struct pvr_cccb *geom_cccb,
				 struct pvr_cccb *frag_cccb,
				 u32 geom_ctx_fw_addr,
				 u32 frag_ctx_fw_addr,
				 struct pvr_hwrt_data *hwrt,
				 bool frag_is_pr)
{
	struct rogue_fwif_kccb_cmd cmd_kick = {
		.cmd_type = ROGUE_FWIF_KCCB_CMD_COMBINED_GEOM_FRAG_KICK,
	};

	fill_cmd_kick_data(geom_cccb, geom_ctx_fw_addr, hwrt,
			   &cmd_kick.cmd_data.combined_geom_frag_cmd_kick_data.geom_cmd_kick_data);

	/* If this is a partial-render job, we don't attach resources to cleanup-ctl array,
	 * because the resources are already retained by the geometry job.
	 */
	fill_cmd_kick_data(frag_cccb, frag_ctx_fw_addr, frag_is_pr ? NULL : hwrt,
			   &cmd_kick.cmd_data.combined_geom_frag_cmd_kick_data.frag_cmd_kick_data);

	/* Make sure the writes to the CCCB are flushed before sending the KICK. */
	wmb();

	pvr_kccb_send_cmd_reserved_powered(pvr_dev, &cmd_kick, NULL);
}
