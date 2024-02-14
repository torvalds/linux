/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_CCCB_H
#define PVR_CCCB_H

#include "pvr_rogue_fwif.h"
#include "pvr_rogue_fwif_shared.h"

#include <linux/mutex.h>
#include <linux/types.h>

#define PADDING_COMMAND_SIZE sizeof(struct rogue_fwif_ccb_cmd_header)

/* Forward declaration from pvr_device.h. */
struct pvr_device;

/* Forward declaration from pvr_gem.h. */
struct pvr_fw_object;

/* Forward declaration from pvr_hwrt.h. */
struct pvr_hwrt_data;

struct pvr_cccb {
	/** @ctrl_obj: FW object representing CCCB control structure. */
	struct pvr_fw_object *ctrl_obj;

	/** @ccb_obj: FW object representing CCCB. */
	struct pvr_fw_object *cccb_obj;

	/**
	 * @ctrl: Kernel mapping of CCCB control structure. @lock must be held
	 *        when accessing.
	 */
	struct rogue_fwif_cccb_ctl *ctrl;

	/** @cccb: Kernel mapping of CCCB. @lock must be held when accessing.*/
	u8 *cccb;

	/** @ctrl_fw_addr: FW virtual address of CCCB control structure. */
	u32 ctrl_fw_addr;
	/** @ccb_fw_addr: FW virtual address of CCCB. */
	u32 cccb_fw_addr;

	/** @size: Size of CCCB in bytes. */
	size_t size;

	/** @write_offset: CCCB write offset. */
	u32 write_offset;

	/** @wrap_mask: CCCB wrap mask. */
	u32 wrap_mask;
};

int pvr_cccb_init(struct pvr_device *pvr_dev, struct pvr_cccb *cccb,
		  u32 size_log2, const char *name);
void pvr_cccb_fini(struct pvr_cccb *cccb);

void pvr_cccb_write_command_with_header(struct pvr_cccb *pvr_cccb,
					u32 cmd_type, u32 cmd_size, void *cmd_data,
					u32 ext_job_ref, u32 int_job_ref);
void pvr_cccb_send_kccb_kick(struct pvr_device *pvr_dev,
			     struct pvr_cccb *pvr_cccb, u32 cctx_fw_addr,
			     struct pvr_hwrt_data *hwrt);
void pvr_cccb_send_kccb_combined_kick(struct pvr_device *pvr_dev,
				      struct pvr_cccb *geom_cccb,
				      struct pvr_cccb *frag_cccb,
				      u32 geom_ctx_fw_addr,
				      u32 frag_ctx_fw_addr,
				      struct pvr_hwrt_data *hwrt,
				      bool frag_is_pr);
bool pvr_cccb_cmdseq_fits(struct pvr_cccb *pvr_cccb, size_t size);

/**
 * pvr_cccb_get_size_of_cmd_with_hdr() - Get the size of a command and its header.
 * @cmd_size: Command size.
 *
 * Returns the size of the command and its header.
 */
static __always_inline u32
pvr_cccb_get_size_of_cmd_with_hdr(u32 cmd_size)
{
	WARN_ON(!IS_ALIGNED(cmd_size, 8));
	return sizeof(struct rogue_fwif_ccb_cmd_header) + ALIGN(cmd_size, 8);
}

/**
 * pvr_cccb_cmdseq_can_fit() - Check if a command sequence can fit in the CCCB.
 * @pvr_cccb: Target Client CCB.
 * @size: Command sequence size.
 *
 * Returns:
 *  * true it the CCCB is big enough to contain a command sequence, or
 *  * false otherwise.
 */
static __always_inline bool
pvr_cccb_cmdseq_can_fit(struct pvr_cccb *pvr_cccb, size_t size)
{
	/* We divide the capacity by two to simplify our CCCB fencing logic:
	 * we want to be sure that, no matter what we had queued before, we
	 * are able to either queue our command sequence at the end or add a
	 * padding command and queue the command sequence at the beginning
	 * of the CCCB. If the command sequence size is bigger than half the
	 * CCCB capacity, we'd have to queue the padding command and make sure
	 * the FW is done processing it before queueing our command sequence.
	 */
	return size + PADDING_COMMAND_SIZE <= pvr_cccb->size / 2;
}

#endif /* PVR_CCCB_H */
