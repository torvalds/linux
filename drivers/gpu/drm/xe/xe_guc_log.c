// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_log.h"

#include <linux/fault-inject.h>

#include <linux/utsname.h>
#include <drm/drm_managed.h>

#include "abi/guc_lfd_abi.h"
#include "regs/xe_guc_regs.h"
#include "xe_bo.h"
#include "xe_devcoredump.h"
#include "xe_force_wake.h"
#include "xe_gt_printk.h"
#include "xe_gt_types.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_module.h"

#define GUC_LOG_CHUNK_SIZE			SZ_2M

/* Magic keys define */
#define GUC_LFD_DRIVER_KEY_STREAMING		0x8086AAAA474C5346
#define GUC_LFD_LOG_BUFFER_MARKER_2		0xDEADFEED
#define GUC_LFD_CRASH_DUMP_BUFFER_MARKER_2	0x8086DEAD
#define GUC_LFD_STATE_CAPTURE_BUFFER_MARKER_2	0xBEEFFEED
#define GUC_LFD_LOG_BUFFER_MARKER_1V2		0xCABBA9E6
#define GUC_LFD_STATE_CAPTURE_BUFFER_MARKER_1V2	0xCABBA9F7
#define GUC_LFD_DATA_HEADER_MAGIC		0x8086

/* LFD supported LIC type range */
#define GUC_LIC_TYPE_FIRST			GUC_LIC_TYPE_GUC_SW_VERSION
#define GUC_LIC_TYPE_LAST			GUC_LIC_TYPE_BUILD_PLATFORM_ID
#define GUC_LFD_TYPE_FW_RANGE_FIRST		GUC_LFD_TYPE_FW_VERSION
#define GUC_LFD_TYPE_FW_RANGE_LAST		GUC_LFD_TYPE_BUILD_PLATFORM_ID

#define GUC_LOG_BUFFER_STATE_HEADER_LENGTH	4096
#define GUC_LOG_BUFFER_INIT_CONFIG		3

struct guc_log_buffer_entry_list {
	u32 offset;
	u32 rd_ptr;
	u32 wr_ptr;
	u32 wrap_offset;
	u32 buf_size;
};

struct guc_lic_save {
	u32 version;
	/*
	 * Array of init config KLV values.
	 * Range from GUC_LOG_LIC_TYPE_FIRST to GUC_LOG_LIC_TYPE_LAST
	 */
	u32 values[GUC_LIC_TYPE_LAST - GUC_LIC_TYPE_FIRST + 1];
	struct guc_log_buffer_entry_list entry[GUC_LOG_BUFFER_INIT_CONFIG];
};

static struct guc_log_buffer_entry_markers {
	u32 key[2];
} const entry_markers[GUC_LOG_BUFFER_INIT_CONFIG + 1] = {
	{{
		GUC_LFD_LOG_BUFFER_MARKER_1V2,
		GUC_LFD_LOG_BUFFER_MARKER_2
	}},
	{{
		GUC_LFD_LOG_BUFFER_MARKER_1V2,
		GUC_LFD_CRASH_DUMP_BUFFER_MARKER_2
	}},
	{{
		GUC_LFD_STATE_CAPTURE_BUFFER_MARKER_1V2,
		GUC_LFD_STATE_CAPTURE_BUFFER_MARKER_2
	}},
	{{
		GUC_LIC_MAGIC,
		(FIELD_PREP_CONST(GUC_LIC_VERSION_MASK_MAJOR, GUC_LIC_VERSION_MAJOR) |
		 FIELD_PREP_CONST(GUC_LIC_VERSION_MASK_MINOR, GUC_LIC_VERSION_MINOR))
	}}
};

static struct guc_log_lic_lfd_map {
	u32 lic;
	u32 lfd;
} const lic_lfd_type_map[] = {
	{GUC_LIC_TYPE_GUC_SW_VERSION,		GUC_LFD_TYPE_FW_VERSION},
	{GUC_LIC_TYPE_GUC_DEVICE_ID,		GUC_LFD_TYPE_GUC_DEVICE_ID},
	{GUC_LIC_TYPE_TSC_FREQUENCY,		GUC_LFD_TYPE_TSC_FREQUENCY},
	{GUC_LIC_TYPE_GMD_ID,			GUC_LFD_TYPE_GMD_ID},
	{GUC_LIC_TYPE_BUILD_PLATFORM_ID,	GUC_LFD_TYPE_BUILD_PLATFORM_ID}
};

static struct xe_guc *
log_to_guc(struct xe_guc_log *log)
{
	return container_of(log, struct xe_guc, log);
}

static struct xe_gt *
log_to_gt(struct xe_guc_log *log)
{
	return container_of(log, struct xe_gt, uc.guc.log);
}

static struct xe_device *
log_to_xe(struct xe_guc_log *log)
{
	return gt_to_xe(log_to_gt(log));
}

static struct xe_guc_log_snapshot *xe_guc_log_snapshot_alloc(struct xe_guc_log *log, bool atomic)
{
	struct xe_guc_log_snapshot *snapshot;
	size_t remain;
	int i;

	snapshot = kzalloc(sizeof(*snapshot), atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!snapshot)
		return NULL;

	/*
	 * NB: kmalloc has a hard limit well below the maximum GuC log buffer size.
	 * Also, can't use vmalloc as might be called from atomic context. So need
	 * to break the buffer up into smaller chunks that can be allocated.
	 */
	snapshot->size = xe_bo_size(log->bo);
	snapshot->num_chunks = DIV_ROUND_UP(snapshot->size, GUC_LOG_CHUNK_SIZE);

	snapshot->copy = kcalloc(snapshot->num_chunks, sizeof(*snapshot->copy),
				 atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!snapshot->copy)
		goto fail_snap;

	remain = snapshot->size;
	for (i = 0; i < snapshot->num_chunks; i++) {
		size_t size = min(GUC_LOG_CHUNK_SIZE, remain);

		snapshot->copy[i] = kmalloc(size, atomic ? GFP_ATOMIC : GFP_KERNEL);
		if (!snapshot->copy[i])
			goto fail_copy;
		remain -= size;
	}

	return snapshot;

fail_copy:
	for (i = 0; i < snapshot->num_chunks; i++)
		kfree(snapshot->copy[i]);
	kfree(snapshot->copy);
fail_snap:
	kfree(snapshot);
	return NULL;
}

/**
 * xe_guc_log_snapshot_free - free a previously captured GuC log snapshot
 * @snapshot: GuC log snapshot structure
 *
 * Return: pointer to a newly allocated snapshot object or null if out of memory. Caller is
 * responsible for calling xe_guc_log_snapshot_free when done with the snapshot.
 */
void xe_guc_log_snapshot_free(struct xe_guc_log_snapshot *snapshot)
{
	int i;

	if (!snapshot)
		return;

	if (snapshot->copy) {
		for (i = 0; i < snapshot->num_chunks; i++)
			kfree(snapshot->copy[i]);
		kfree(snapshot->copy);
	}

	kfree(snapshot);
}

/**
 * xe_guc_log_snapshot_capture - create a new snapshot copy the GuC log for later dumping
 * @log: GuC log structure
 * @atomic: is the call inside an atomic section of some kind?
 *
 * Return: pointer to a newly allocated snapshot object or null if out of memory. Caller is
 * responsible for calling xe_guc_log_snapshot_free when done with the snapshot.
 */
struct xe_guc_log_snapshot *xe_guc_log_snapshot_capture(struct xe_guc_log *log, bool atomic)
{
	struct xe_guc_log_snapshot *snapshot;
	struct xe_device *xe = log_to_xe(log);
	struct xe_guc *guc = log_to_guc(log);
	struct xe_gt *gt = log_to_gt(log);
	size_t remain;
	int i;

	if (!log->bo)
		return NULL;

	snapshot = xe_guc_log_snapshot_alloc(log, atomic);
	if (!snapshot)
		return NULL;

	remain = snapshot->size;
	for (i = 0; i < snapshot->num_chunks; i++) {
		size_t size = min(GUC_LOG_CHUNK_SIZE, remain);

		xe_map_memcpy_from(xe, snapshot->copy[i], &log->bo->vmap,
				   i * GUC_LOG_CHUNK_SIZE, size);
		remain -= size;
	}

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref.domains)
		snapshot->stamp = ~0ULL;
	else
		snapshot->stamp = xe_mmio_read64_2x32(&gt->mmio, GUC_PMTIMESTAMP_LO);

	snapshot->ktime = ktime_get_boottime_ns();
	snapshot->level = log->level;
	snapshot->ver_found = guc->fw.versions.found[XE_UC_FW_VER_RELEASE];
	snapshot->ver_want = guc->fw.versions.wanted;
	snapshot->path = guc->fw.path;

	return snapshot;
}

/**
 * xe_guc_log_snapshot_print - dump a previously saved copy of the GuC log to some useful location
 * @snapshot: a snapshot of the GuC log
 * @p: the printer object to output to
 */
void xe_guc_log_snapshot_print(struct xe_guc_log_snapshot *snapshot, struct drm_printer *p)
{
	size_t remain;
	int i;

	if (!snapshot) {
		drm_printf(p, "GuC log snapshot not allocated!\n");
		return;
	}

	drm_printf(p, "GuC firmware: %s\n", snapshot->path);
	drm_printf(p, "GuC version: %u.%u.%u (wanted %u.%u.%u)\n",
		   snapshot->ver_found.major, snapshot->ver_found.minor, snapshot->ver_found.patch,
		   snapshot->ver_want.major, snapshot->ver_want.minor, snapshot->ver_want.patch);
	drm_printf(p, "Kernel timestamp: 0x%08llX [%llu]\n", snapshot->ktime, snapshot->ktime);
	drm_printf(p, "GuC timestamp: 0x%08llX [%llu]\n", snapshot->stamp, snapshot->stamp);
	drm_printf(p, "Log level: %u\n", snapshot->level);

	drm_printf(p, "[LOG].length: 0x%zx\n", snapshot->size);
	remain = snapshot->size;
	for (i = 0; i < snapshot->num_chunks; i++) {
		size_t size = min(GUC_LOG_CHUNK_SIZE, remain);
		const char *prefix = i ? NULL : "[LOG].data";
		char suffix = i == snapshot->num_chunks - 1 ? '\n' : 0;

		xe_print_blob_ascii85(p, prefix, suffix, snapshot->copy[i], 0, size);
		remain -= size;
	}
}

static inline void lfd_output_binary(struct drm_printer *p, char *buf, int buf_size)
{
	seq_write(p->arg, buf, buf_size);
}

static inline int xe_guc_log_add_lfd_header(struct guc_lfd_data *lfd)
{
	lfd->header = FIELD_PREP_CONST(GUC_LFD_DATA_HEADER_MASK_MAGIC, GUC_LFD_DATA_HEADER_MAGIC);
	return offsetof(struct guc_lfd_data, data);
}

static int xe_guc_log_add_typed_payload(struct drm_printer *p, u32 type,
					u32 data_len, void *data)
{
	struct guc_lfd_data lfd;
	int len;

	len = xe_guc_log_add_lfd_header(&lfd);
	lfd.header |= FIELD_PREP(GUC_LFD_DATA_HEADER_MASK_TYPE, type);
	/* make length DW aligned */
	lfd.data_count = DIV_ROUND_UP(data_len, sizeof(u32));
	lfd_output_binary(p, (char *)&lfd, len);

	lfd_output_binary(p, data, data_len);
	len += lfd.data_count * sizeof(u32);

	return len;
}

static inline int lic_type_to_index(u32 lic_type)
{
	XE_WARN_ON(lic_type < GUC_LIC_TYPE_FIRST || lic_type > GUC_LIC_TYPE_LAST);

	return lic_type - GUC_LIC_TYPE_FIRST;
}

static inline int lfd_type_to_index(u32 lfd_type)
{
	int i, lic_type = 0;

	XE_WARN_ON(lfd_type < GUC_LFD_TYPE_FW_RANGE_FIRST || lfd_type > GUC_LFD_TYPE_FW_RANGE_LAST);

	for (i = 0; i < ARRAY_SIZE(lic_lfd_type_map); i++)
		if (lic_lfd_type_map[i].lfd == lfd_type)
			lic_type = lic_lfd_type_map[i].lic;

	/* If not found, lic_type_to_index will warning invalid type */
	return lic_type_to_index(lic_type);
}

static int xe_guc_log_add_klv(struct drm_printer *p, u32 lfd_type,
			      struct guc_lic_save *config)
{
	int klv_index = lfd_type_to_index(lfd_type);

	return xe_guc_log_add_typed_payload(p, lfd_type, sizeof(u32), &config->values[klv_index]);
}

static int xe_guc_log_add_os_id(struct drm_printer *p, u32 id)
{
	struct guc_lfd_data_os_info os_id;
	struct guc_lfd_data lfd;
	int len, info_len, section_len;
	char *version;
	u32 blank = 0;

	len = xe_guc_log_add_lfd_header(&lfd);
	lfd.header |= FIELD_PREP(GUC_LFD_DATA_HEADER_MASK_TYPE, GUC_LFD_TYPE_OS_ID);

	os_id.os_id = id;
	section_len = offsetof(struct guc_lfd_data_os_info, build_version);

	version = init_utsname()->release;
	info_len = strlen(version);

	/* make length DW aligned */
	lfd.data_count = DIV_ROUND_UP(section_len + info_len, sizeof(u32));
	lfd_output_binary(p, (char *)&lfd, len);
	lfd_output_binary(p, (char *)&os_id, section_len);
	lfd_output_binary(p, version, info_len);

	/* Padding with 0 */
	section_len = lfd.data_count * sizeof(u32) - section_len - info_len;
	if (section_len)
		lfd_output_binary(p, (char *)&blank, section_len);

	len +=  lfd.data_count * sizeof(u32);
	return len;
}

static void xe_guc_log_loop_log_init(struct guc_lic *init, struct guc_lic_save *config)
{
	struct guc_klv_generic_dw_t *p = (void *)init->data;
	int i;

	for (i = 0; i < init->data_count;) {
		int klv_len = FIELD_GET(GUC_KLV_0_LEN, p->kl) + 1;
		int key = FIELD_GET(GUC_KLV_0_KEY, p->kl);

		if (key < GUC_LIC_TYPE_FIRST || key > GUC_LIC_TYPE_LAST) {
			XE_WARN_ON(key < GUC_LIC_TYPE_FIRST || key > GUC_LIC_TYPE_LAST);
			break;
		}
		config->values[lic_type_to_index(key)] = p->value;
		i += klv_len + 1; /* Whole KLV structure length in dwords */
		p = (void *)((u32 *)p + klv_len);
	}
}

static int find_marker(u32 mark0, u32 mark1)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(entry_markers); i++)
		if (mark0 == entry_markers[i].key[0] && mark1 == entry_markers[i].key[1])
			return i;

	return ARRAY_SIZE(entry_markers);
}

static void xe_guc_log_load_lic(void *guc_log, struct guc_lic_save *config)
{
	u32 offset = GUC_LOG_BUFFER_STATE_HEADER_LENGTH;
	struct guc_log_buffer_state *p = guc_log;

	config->version = p->version;
	while (p->marker[0]) {
		int index;

		index = find_marker(p->marker[0], p->marker[1]);

		if (index < ARRAY_SIZE(entry_markers)) {
			if (index == GUC_LOG_BUFFER_INIT_CONFIG) {
				/* Load log init config */
				xe_guc_log_loop_log_init((void *)p, config);

				/* LIC structure is the last */
				return;
			}
			config->entry[index].offset = offset;
			config->entry[index].rd_ptr = p->read_ptr;
			config->entry[index].wr_ptr = p->write_ptr;
			config->entry[index].wrap_offset = p->wrap_offset;
			config->entry[index].buf_size = p->size;
		}
		offset += p->size;
		p++;
	}
}

static int
xe_guc_log_output_lfd_init(struct drm_printer *p, struct xe_guc_log_snapshot *snapshot,
			   struct guc_lic_save *config)
{
	int type, len;
	size_t size = 0;

	/* FW required types */
	for (type = GUC_LFD_TYPE_FW_RANGE_FIRST; type <= GUC_LFD_TYPE_FW_RANGE_LAST; type++)
		size += xe_guc_log_add_klv(p, type, config);

	/* KMD required type(s) */
	len = xe_guc_log_add_os_id(p, GUC_LFD_OS_TYPE_OSID_LIN);
	size += len;

	return size;
}

static void
xe_guc_log_print_chunks(struct drm_printer *p, struct xe_guc_log_snapshot *snapshot,
			u32 from, u32 to)
{
	int chunk_from = from % GUC_LOG_CHUNK_SIZE;
	int chunk_id = from / GUC_LOG_CHUNK_SIZE;
	int to_chunk_id = to / GUC_LOG_CHUNK_SIZE;
	int chunk_to = to % GUC_LOG_CHUNK_SIZE;
	int pos = from;

	do {
		size_t size = (to_chunk_id == chunk_id ? chunk_to : GUC_LOG_CHUNK_SIZE) -
			      chunk_from;

		lfd_output_binary(p, snapshot->copy[chunk_id] + chunk_from, size);
		pos += size;
		chunk_id++;
		chunk_from = 0;
	} while (pos < to);
}

static inline int
xe_guc_log_add_log_event(struct drm_printer *p, struct xe_guc_log_snapshot *snapshot,
			 struct guc_lic_save *config)
{
	size_t size;
	u32 data_len, section_len;
	struct guc_lfd_data lfd;
	struct guc_log_buffer_entry_list *entry;
	struct guc_lfd_data_log_events_buf events_buf;

	entry = &config->entry[GUC_LOG_TYPE_EVENT_DATA];

	/* Skip empty log */
	if (entry->rd_ptr == entry->wr_ptr)
		return 0;

	size = xe_guc_log_add_lfd_header(&lfd);
	lfd.header |= FIELD_PREP(GUC_LFD_DATA_HEADER_MASK_TYPE, GUC_LFD_TYPE_LOG_EVENTS_BUFFER);
	events_buf.log_events_format_version = config->version;

	/* Adjust to log_format_buf */
	section_len = offsetof(struct guc_lfd_data_log_events_buf, log_event);
	data_len = section_len;

	/* Calculate data length */
	data_len += entry->rd_ptr < entry->wr_ptr ? (entry->wr_ptr - entry->rd_ptr) :
		(entry->wr_ptr + entry->wrap_offset - entry->rd_ptr);
	/* make length u32 aligned */
	lfd.data_count = DIV_ROUND_UP(data_len, sizeof(u32));

	/* Output GUC_LFD_TYPE_LOG_EVENTS_BUFFER header */
	lfd_output_binary(p, (char *)&lfd, size);
	lfd_output_binary(p, (char *)&events_buf, section_len);

	/* Output data from guc log chunks directly */
	if (entry->rd_ptr < entry->wr_ptr) {
		xe_guc_log_print_chunks(p, snapshot, entry->offset + entry->rd_ptr,
					entry->offset + entry->wr_ptr);
	} else {
		/* 1st, print from rd to wrap offset */
		xe_guc_log_print_chunks(p, snapshot, entry->offset + entry->rd_ptr,
					entry->offset + entry->wrap_offset);

		/* 2nd, print from buf start to wr */
		xe_guc_log_print_chunks(p, snapshot, entry->offset, entry->offset + entry->wr_ptr);
	}
	return size;
}

static int
xe_guc_log_add_crash_dump(struct drm_printer *p, struct xe_guc_log_snapshot *snapshot,
			  struct guc_lic_save *config)
{
	struct guc_log_buffer_entry_list *entry;
	int chunk_from, chunk_id;
	int from, to, i;
	size_t size = 0;
	u32 *buf32;

	entry = &config->entry[GUC_LOG_TYPE_CRASH_DUMP];

	/* Skip zero sized crash dump */
	if (!entry->buf_size)
		return 0;

	/* Check if crash dump section are all zero */
	from = entry->offset;
	to = entry->offset + entry->buf_size;
	chunk_from = from % GUC_LOG_CHUNK_SIZE;
	chunk_id = from / GUC_LOG_CHUNK_SIZE;
	buf32 = snapshot->copy[chunk_id] + chunk_from;

	for (i = 0; i < entry->buf_size / sizeof(u32); i++)
		if (buf32[i])
			break;

	/* Buffer has non-zero data? */
	if (i < entry->buf_size / sizeof(u32)) {
		struct guc_lfd_data lfd;

		size = xe_guc_log_add_lfd_header(&lfd);
		lfd.header |= FIELD_PREP(GUC_LFD_DATA_HEADER_MASK_TYPE, GUC_LFD_TYPE_FW_CRASH_DUMP);
		/* Calculate data length */
		lfd.data_count = DIV_ROUND_UP(entry->buf_size, sizeof(u32));
		/* Output GUC_LFD_TYPE_FW_CRASH_DUMP header */
		lfd_output_binary(p, (char *)&lfd, size);

		/* rd/wr ptr is not used for crash dump */
		xe_guc_log_print_chunks(p, snapshot, from, to);
	}
	return size;
}

static void
xe_guc_log_snapshot_print_lfd(struct xe_guc_log_snapshot *snapshot, struct drm_printer *p)
{
	struct guc_lfd_file_header header;
	struct guc_lic_save config;
	size_t size;

	if (!snapshot || !snapshot->size)
		return;

	header.magic = GUC_LFD_DRIVER_KEY_STREAMING;
	header.version = FIELD_PREP_CONST(GUC_LFD_FILE_HEADER_VERSION_MASK_MINOR,
					  GUC_LFD_FORMAT_VERSION_MINOR) |
			 FIELD_PREP_CONST(GUC_LFD_FILE_HEADER_VERSION_MASK_MAJOR,
					  GUC_LFD_FORMAT_VERSION_MAJOR);

	/* Output LFD file header */
	lfd_output_binary(p, (char *)&header,
			  offsetof(struct guc_lfd_file_header, stream));

	/* Output LFD stream */
	xe_guc_log_load_lic(snapshot->copy[0], &config);
	size = xe_guc_log_output_lfd_init(p, snapshot, &config);
	if (!size)
		return;

	xe_guc_log_add_log_event(p, snapshot, &config);
	xe_guc_log_add_crash_dump(p, snapshot, &config);
}

/**
 * xe_guc_log_print_dmesg - dump a copy of the GuC log to dmesg
 * @log: GuC log structure
 */
void xe_guc_log_print_dmesg(struct xe_guc_log *log)
{
	struct xe_gt *gt = log_to_gt(log);
	static int g_count;
	struct drm_printer ip = xe_gt_info_printer(gt);
	struct drm_printer lp = drm_line_printer(&ip, "Capture", ++g_count);

	drm_printf(&lp, "Dumping GuC log for %ps...\n", __builtin_return_address(0));

	xe_guc_log_print(log, &lp);

	drm_printf(&lp, "Done.\n");
}

/**
 * xe_guc_log_print - dump a copy of the GuC log to some useful location
 * @log: GuC log structure
 * @p: the printer object to output to
 */
void xe_guc_log_print(struct xe_guc_log *log, struct drm_printer *p)
{
	struct xe_guc_log_snapshot *snapshot;

	drm_printf(p, "**** GuC Log ****\n");

	snapshot = xe_guc_log_snapshot_capture(log, false);
	drm_printf(p, "CS reference clock: %u\n", log_to_gt(log)->info.reference_clock);
	xe_guc_log_snapshot_print(snapshot, p);
	xe_guc_log_snapshot_free(snapshot);
}

/**
 * xe_guc_log_print_lfd - dump a copy of the GuC log in LFD format
 * @log: GuC log structure
 * @p: the printer object to output to
 */
void xe_guc_log_print_lfd(struct xe_guc_log *log, struct drm_printer *p)
{
	struct xe_guc_log_snapshot *snapshot;

	snapshot = xe_guc_log_snapshot_capture(log, false);
	xe_guc_log_snapshot_print_lfd(snapshot, p);
	xe_guc_log_snapshot_free(snapshot);
}

int xe_guc_log_init(struct xe_guc_log *log)
{
	struct xe_device *xe = log_to_xe(log);
	struct xe_tile *tile = gt_to_tile(log_to_gt(log));
	struct xe_bo *bo;

	bo = xe_managed_bo_create_pin_map(xe, tile, GUC_LOG_SIZE,
					  XE_BO_FLAG_SYSTEM |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE |
					  XE_BO_FLAG_PINNED_NORESTORE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	xe_map_memset(xe, &bo->vmap, 0, 0, xe_bo_size(bo));
	log->bo = bo;
	log->level = xe_modparam.guc_log_level;

	return 0;
}

ALLOW_ERROR_INJECTION(xe_guc_log_init, ERRNO); /* See xe_pci_probe() */

/**
 * xe_guc_check_log_buf_overflow - Check if log buffer overflowed
 * @log: The log object.
 * @type: The log buffer type
 * @full_cnt: The count of buffer full
 *
 * This function will check count of buffer full against previous, mismatch
 * indicate overflowed.
 * Update the sampled_overflow counter, if the 4 bit counter overflowed, add
 * up 16 to correct the value.
 *
 * Return: True if overflowed.
 */
bool xe_guc_check_log_buf_overflow(struct xe_guc_log *log, enum guc_log_type type,
				   unsigned int full_cnt)
{
	unsigned int prev_full_cnt = log->stats[type].sampled_overflow;
	bool overflow = false;

	if (full_cnt != prev_full_cnt) {
		overflow = true;

		log->stats[type].overflow = full_cnt;
		log->stats[type].sampled_overflow += full_cnt - prev_full_cnt;

		if (full_cnt < prev_full_cnt) {
			/* buffer_full_cnt is a 4 bit counter */
			log->stats[type].sampled_overflow += 16;
		}
		xe_gt_notice(log_to_gt(log), "log buffer overflow\n");
	}

	return overflow;
}
