// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "gem/i915_gem_lmem.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_print.h"
#include "gt/intel_ring.h"
#include "intel_gsc_binary_headers.h"
#include "intel_gsc_fw.h"
#include "intel_gsc_uc_heci_cmd_submit.h"
#include "i915_reg.h"

static bool gsc_is_in_reset(struct intel_uncore *uncore)
{
	u32 fw_status = intel_uncore_read(uncore, HECI_FWSTS(MTL_GSC_HECI1_BASE, 1));

	return REG_FIELD_GET(HECI1_FWSTS1_CURRENT_STATE, fw_status) ==
			HECI1_FWSTS1_CURRENT_STATE_RESET;
}

static u32 gsc_uc_get_fw_status(struct intel_uncore *uncore, bool needs_wakeref)
{
	intel_wakeref_t wakeref;
	u32 fw_status = 0;

	if (needs_wakeref)
		wakeref = intel_runtime_pm_get(uncore->rpm);

	fw_status = intel_uncore_read(uncore, HECI_FWSTS(MTL_GSC_HECI1_BASE, 1));

	if (needs_wakeref)
		intel_runtime_pm_put(uncore->rpm, wakeref);
	return fw_status;
}

bool intel_gsc_uc_fw_proxy_init_done(struct intel_gsc_uc *gsc, bool needs_wakeref)
{
	return REG_FIELD_GET(HECI1_FWSTS1_CURRENT_STATE,
			     gsc_uc_get_fw_status(gsc_uc_to_gt(gsc)->uncore,
						  needs_wakeref)) ==
	       HECI1_FWSTS1_PROXY_STATE_NORMAL;
}

int intel_gsc_uc_fw_proxy_get_status(struct intel_gsc_uc *gsc)
{
	if (!(IS_ENABLED(CONFIG_INTEL_MEI_GSC_PROXY)))
		return -ENODEV;
	if (!intel_uc_fw_is_loadable(&gsc->fw))
		return -ENODEV;
	if (__intel_uc_fw_status(&gsc->fw) == INTEL_UC_FIRMWARE_LOAD_FAIL)
		return -ENOLINK;
	if (!intel_gsc_uc_fw_proxy_init_done(gsc, true))
		return -EAGAIN;

	return 0;
}

bool intel_gsc_uc_fw_init_done(struct intel_gsc_uc *gsc)
{
	return gsc_uc_get_fw_status(gsc_uc_to_gt(gsc)->uncore, false) &
	       HECI1_FWSTS1_INIT_COMPLETE;
}

static inline u32 cpd_entry_offset(const struct intel_gsc_cpd_entry *entry)
{
	return entry->offset & INTEL_GSC_CPD_ENTRY_OFFSET_MASK;
}

int intel_gsc_fw_get_binary_info(struct intel_uc_fw *gsc_fw, const void *data, size_t size)
{
	struct intel_gsc_uc *gsc = container_of(gsc_fw, struct intel_gsc_uc, fw);
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	const struct intel_gsc_layout_pointers *layout = data;
	const struct intel_gsc_bpdt_header *bpdt_header = NULL;
	const struct intel_gsc_bpdt_entry *bpdt_entry = NULL;
	const struct intel_gsc_cpd_header_v2 *cpd_header = NULL;
	const struct intel_gsc_cpd_entry *cpd_entry = NULL;
	const struct intel_gsc_manifest_header *manifest;
	size_t min_size = sizeof(*layout);
	int i;

	if (size < min_size) {
		gt_err(gt, "GSC FW too small! %zu < %zu\n", size, min_size);
		return -ENODATA;
	}

	/*
	 * The GSC binary starts with the pointer layout, which contains the
	 * locations of the various partitions of the binary. The one we're
	 * interested in to get the version is the boot1 partition, where we can
	 * find a BPDT header followed by entries, one of which points to the
	 * RBE sub-section of the partition. From here, we can parse the CPD
	 * header and the following entries to find the manifest location
	 * (entry identified by the "RBEP.man" name), from which we can finally
	 * extract the version.
	 *
	 * --------------------------------------------------
	 * [  intel_gsc_layout_pointers                     ]
	 * [      ...                                       ]
	 * [      boot1.offset  >---------------------------]------o
	 * [      ...                                       ]      |
	 * --------------------------------------------------      |
	 *                                                         |
	 * --------------------------------------------------      |
	 * [  intel_gsc_bpdt_header                         ]<-----o
	 * --------------------------------------------------
	 * [  intel_gsc_bpdt_entry[]                        ]
	 * [      entry1                                    ]
	 * [      ...                                       ]
	 * [      entryX                                    ]
	 * [          type == GSC_RBE                       ]
	 * [          offset  >-----------------------------]------o
	 * [      ...                                       ]      |
	 * --------------------------------------------------      |
	 *                                                         |
	 * --------------------------------------------------      |
	 * [  intel_gsc_cpd_header_v2                       ]<-----o
	 * --------------------------------------------------
	 * [  intel_gsc_cpd_entry[]                         ]
	 * [      entry1                                    ]
	 * [      ...                                       ]
	 * [      entryX                                    ]
	 * [          "RBEP.man"                            ]
	 * [           ...                                  ]
	 * [           offset  >----------------------------]------o
	 * [      ...                                       ]      |
	 * --------------------------------------------------      |
	 *                                                         |
	 * --------------------------------------------------      |
	 * [ intel_gsc_manifest_header                      ]<-----o
	 * [  ...                                           ]
	 * [  intel_gsc_version     fw_version              ]
	 * [  ...                                           ]
	 * --------------------------------------------------
	 */

	min_size = layout->boot1.offset + layout->boot1.size;
	if (size < min_size) {
		gt_err(gt, "GSC FW too small for boot section! %zu < %zu\n",
		       size, min_size);
		return -ENODATA;
	}

	min_size = sizeof(*bpdt_header);
	if (layout->boot1.size < min_size) {
		gt_err(gt, "GSC FW boot section too small for BPDT header: %u < %zu\n",
		       layout->boot1.size, min_size);
		return -ENODATA;
	}

	bpdt_header = data + layout->boot1.offset;
	if (bpdt_header->signature != INTEL_GSC_BPDT_HEADER_SIGNATURE) {
		gt_err(gt, "invalid signature for BPDT header: 0x%08x!\n",
		       bpdt_header->signature);
		return -EINVAL;
	}

	min_size += sizeof(*bpdt_entry) * bpdt_header->descriptor_count;
	if (layout->boot1.size < min_size) {
		gt_err(gt, "GSC FW boot section too small for BPDT entries: %u < %zu\n",
		       layout->boot1.size, min_size);
		return -ENODATA;
	}

	bpdt_entry = (void *)bpdt_header + sizeof(*bpdt_header);
	for (i = 0; i < bpdt_header->descriptor_count; i++, bpdt_entry++) {
		if ((bpdt_entry->type & INTEL_GSC_BPDT_ENTRY_TYPE_MASK) !=
		    INTEL_GSC_BPDT_ENTRY_TYPE_GSC_RBE)
			continue;

		cpd_header = (void *)bpdt_header + bpdt_entry->sub_partition_offset;
		min_size = bpdt_entry->sub_partition_offset + sizeof(*cpd_header);
		break;
	}

	if (!cpd_header) {
		gt_err(gt, "couldn't find CPD header in GSC binary!\n");
		return -ENODATA;
	}

	if (layout->boot1.size < min_size) {
		gt_err(gt, "GSC FW boot section too small for CPD header: %u < %zu\n",
		       layout->boot1.size, min_size);
		return -ENODATA;
	}

	if (cpd_header->header_marker != INTEL_GSC_CPD_HEADER_MARKER) {
		gt_err(gt, "invalid marker for CPD header in GSC bin: 0x%08x!\n",
		       cpd_header->header_marker);
		return -EINVAL;
	}

	min_size += sizeof(*cpd_entry) * cpd_header->num_of_entries;
	if (layout->boot1.size < min_size) {
		gt_err(gt, "GSC FW boot section too small for CPD entries: %u < %zu\n",
		       layout->boot1.size, min_size);
		return -ENODATA;
	}

	cpd_entry = (void *)cpd_header + cpd_header->header_length;
	for (i = 0; i < cpd_header->num_of_entries; i++, cpd_entry++) {
		if (strcmp(cpd_entry->name, "RBEP.man") == 0) {
			manifest = (void *)cpd_header + cpd_entry_offset(cpd_entry);
			intel_uc_fw_version_from_gsc_manifest(&gsc->release,
							      manifest);
			gsc->security_version = manifest->security_version;
			break;
		}
	}

	return 0;
}

static int emit_gsc_fw_load(struct i915_request *rq, struct intel_gsc_uc *gsc)
{
	u32 offset = i915_ggtt_offset(gsc->local);
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GSC_FW_LOAD;
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);
	*cs++ = (gsc->local->size / SZ_4K) | HECI1_FW_LIMIT_VALID;

	intel_ring_advance(rq, cs);

	return 0;
}

static int gsc_fw_load(struct intel_gsc_uc *gsc)
{
	struct intel_context *ce = gsc->ce;
	struct i915_request *rq;
	int err;

	if (!ce)
		return -ENODEV;

	rq = i915_request_create(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (err)
			goto out_rq;
	}

	err = emit_gsc_fw_load(rq, gsc);
	if (err)
		goto out_rq;

	err = ce->engine->emit_flush(rq, 0);

out_rq:
	i915_request_get(rq);

	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);

	if (!err && i915_request_wait(rq, 0, msecs_to_jiffies(500)) < 0)
		err = -ETIME;

	i915_request_put(rq);

	if (err)
		gt_err(gsc_uc_to_gt(gsc), "Request submission for GSC load failed %pe\n",
		       ERR_PTR(err));

	return err;
}

static int gsc_fw_load_prepare(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	void *src;

	if (!gsc->local)
		return -ENODEV;

	if (gsc->local->size < gsc->fw.size)
		return -ENOSPC;

	src = i915_gem_object_pin_map_unlocked(gsc->fw.obj,
					       intel_gt_coherent_map_type(gt, gsc->fw.obj, true));
	if (IS_ERR(src))
		return PTR_ERR(src);

	memcpy_toio(gsc->local_vaddr, src, gsc->fw.size);
	memset_io(gsc->local_vaddr + gsc->fw.size, 0, gsc->local->size - gsc->fw.size);

	intel_guc_write_barrier(gt_to_guc(gt));

	i915_gem_object_unpin_map(gsc->fw.obj);

	return 0;
}

static int gsc_fw_wait(struct intel_gt *gt)
{
	return intel_wait_for_register(gt->uncore,
				       HECI_FWSTS(MTL_GSC_HECI1_BASE, 1),
				       HECI1_FWSTS1_INIT_COMPLETE,
				       HECI1_FWSTS1_INIT_COMPLETE,
				       500);
}

struct intel_gsc_mkhi_header {
	u8  group_id;
#define MKHI_GROUP_ID_GFX_SRV 0x30

	u8  command;
#define MKHI_GFX_SRV_GET_HOST_COMPATIBILITY_VERSION (0x42)

	u8  reserved;
	u8  result;
} __packed;

struct mtl_gsc_ver_msg_in {
	struct intel_gsc_mtl_header header;
	struct intel_gsc_mkhi_header mkhi;
} __packed;

struct mtl_gsc_ver_msg_out {
	struct intel_gsc_mtl_header header;
	struct intel_gsc_mkhi_header mkhi;
	u16 proj_major;
	u16 compat_major;
	u16 compat_minor;
	u16 reserved[5];
} __packed;

#define GSC_VER_PKT_SZ SZ_4K

static int gsc_fw_query_compatibility_version(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct mtl_gsc_ver_msg_in *msg_in;
	struct mtl_gsc_ver_msg_out *msg_out;
	struct i915_vma *vma;
	u64 offset;
	void *vaddr;
	int err;

	err = intel_guc_allocate_and_map_vma(gt_to_guc(gt), GSC_VER_PKT_SZ * 2,
					     &vma, &vaddr);
	if (err) {
		gt_err(gt, "failed to allocate vma for GSC version query\n");
		return err;
	}

	offset = i915_ggtt_offset(vma);
	msg_in = vaddr;
	msg_out = vaddr + GSC_VER_PKT_SZ;

	intel_gsc_uc_heci_cmd_emit_mtl_header(&msg_in->header,
					      HECI_MEADDRESS_MKHI,
					      sizeof(*msg_in), 0);
	msg_in->mkhi.group_id = MKHI_GROUP_ID_GFX_SRV;
	msg_in->mkhi.command = MKHI_GFX_SRV_GET_HOST_COMPATIBILITY_VERSION;

	err = intel_gsc_uc_heci_cmd_submit_packet(&gt->uc.gsc,
						  offset,
						  sizeof(*msg_in),
						  offset + GSC_VER_PKT_SZ,
						  GSC_VER_PKT_SZ);
	if (err) {
		gt_err(gt,
		       "failed to submit GSC request for compatibility version: %d\n",
		       err);
		goto out_vma;
	}

	if (msg_out->header.message_size != sizeof(*msg_out)) {
		gt_err(gt, "invalid GSC reply length %u [expected %zu], s=0x%x, f=0x%x, r=0x%x\n",
		       msg_out->header.message_size, sizeof(*msg_out),
		       msg_out->header.status, msg_out->header.flags, msg_out->mkhi.result);
		err = -EPROTO;
		goto out_vma;
	}

	gsc->fw.file_selected.ver.major = msg_out->compat_major;
	gsc->fw.file_selected.ver.minor = msg_out->compat_minor;

out_vma:
	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return err;
}

int intel_gsc_uc_fw_upload(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct intel_uc_fw *gsc_fw = &gsc->fw;
	int err;

	/* check current fw status */
	if (intel_gsc_uc_fw_init_done(gsc)) {
		if (GEM_WARN_ON(!intel_uc_fw_is_loaded(gsc_fw)))
			intel_uc_fw_change_status(gsc_fw, INTEL_UC_FIRMWARE_TRANSFERRED);
		return -EEXIST;
	}

	if (!intel_uc_fw_is_loadable(gsc_fw))
		return -ENOEXEC;

	/* FW blob is ok, so clean the status */
	intel_uc_fw_sanitize(&gsc->fw);

	if (!gsc_is_in_reset(gt->uncore))
		return -EIO;

	err = gsc_fw_load_prepare(gsc);
	if (err)
		goto fail;

	/*
	 * GSC is only killed by an FLR, so we need to trigger one on unload to
	 * make sure we stop it. This is because we assign a chunk of memory to
	 * the GSC as part of the FW load , so we need to make sure it stops
	 * using it when we release it to the system on driver unload. Note that
	 * this is not a problem of the unload per-se, because the GSC will not
	 * touch that memory unless there are requests for it coming from the
	 * driver; therefore, no accesses will happen while i915 is not loaded,
	 * but if we re-load the driver then the GSC might wake up and try to
	 * access that old memory location again.
	 * Given that an FLR is a very disruptive action (see the FLR function
	 * for details), we want to do it as the last action before releasing
	 * the access to the MMIO bar, which means we need to do it as part of
	 * the primary uncore cleanup.
	 * An alternative approach to the FLR would be to use a memory location
	 * that survives driver unload, like e.g. stolen memory, and keep the
	 * GSC loaded across reloads. However, this requires us to make sure we
	 * preserve that memory location on unload and then determine and
	 * reserve its offset on each subsequent load, which is not trivial, so
	 * it is easier to just kill everything and start fresh.
	 */
	intel_uncore_set_flr_on_fini(&gt->i915->uncore);

	err = gsc_fw_load(gsc);
	if (err)
		goto fail;

	err = gsc_fw_wait(gt);
	if (err)
		goto fail;

	err = gsc_fw_query_compatibility_version(gsc);
	if (err)
		goto fail;

	/* we only support compatibility version 1.0 at the moment */
	err = intel_uc_check_file_version(gsc_fw, NULL);
	if (err)
		goto fail;

	/* FW is not fully operational until we enable SW proxy */
	intel_uc_fw_change_status(gsc_fw, INTEL_UC_FIRMWARE_TRANSFERRED);

	gt_info(gt, "Loaded GSC firmware %s (cv%u.%u, r%u.%u.%u.%u, svn %u)\n",
		gsc_fw->file_selected.path,
		gsc_fw->file_selected.ver.major, gsc_fw->file_selected.ver.minor,
		gsc->release.major, gsc->release.minor,
		gsc->release.patch, gsc->release.build,
		gsc->security_version);

	return 0;

fail:
	return intel_uc_fw_mark_load_failed(gsc_fw, err);
}
