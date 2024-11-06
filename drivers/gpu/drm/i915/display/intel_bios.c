/*
 * Copyright © 2006 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <linux/debugfs.h>
#include <linux/firmware.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>

#include "soc/intel_rom.h"

#include "i915_drv.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_gmbus.h"

#define _INTEL_BIOS_PRIVATE
#include "intel_vbt_defs.h"

/**
 * DOC: Video BIOS Table (VBT)
 *
 * The Video BIOS Table, or VBT, provides platform and board specific
 * configuration information to the driver that is not discoverable or available
 * through other means. The configuration is mostly related to display
 * hardware. The VBT is available via the ACPI OpRegion or, on older systems, in
 * the PCI ROM.
 *
 * The VBT consists of a VBT Header (defined as &struct vbt_header), a BDB
 * Header (&struct bdb_header), and a number of BIOS Data Blocks (BDB) that
 * contain the actual configuration information. The VBT Header, and thus the
 * VBT, begins with "$VBT" signature. The VBT Header contains the offset of the
 * BDB Header. The data blocks are concatenated after the BDB Header. The data
 * blocks have a 1-byte Block ID, 2-byte Block Size, and Block Size bytes of
 * data. (Block 53, the MIPI Sequence Block is an exception.)
 *
 * The driver parses the VBT during load. The relevant information is stored in
 * driver private data for ease of use, and the actual VBT is not read after
 * that.
 */

/* Wrapper for VBT child device config */
struct intel_bios_encoder_data {
	struct intel_display *display;

	struct child_device_config child;
	struct dsc_compression_parameters_entry *dsc;
	struct list_head node;
};

#define	TARGET_ADDR1	0x70
#define	TARGET_ADDR2	0x72

/* Get BDB block size given a pointer to Block ID. */
static u32 _get_blocksize(const u8 *block_base)
{
	/* The MIPI Sequence Block v3+ has a separate size field. */
	if (*block_base == BDB_MIPI_SEQUENCE && *(block_base + 3) >= 3)
		return *((const u32 *)(block_base + 4));
	else
		return *((const u16 *)(block_base + 1));
}

/* Get BDB block size give a pointer to data after Block ID and Block Size. */
static u32 get_blocksize(const void *block_data)
{
	return _get_blocksize(block_data - 3);
}

static const void *
find_raw_section(const void *_bdb, enum bdb_block_id section_id)
{
	const struct bdb_header *bdb = _bdb;
	const u8 *base = _bdb;
	int index = 0;
	u32 total, current_size;
	enum bdb_block_id current_id;

	/* skip to first section */
	index += bdb->header_size;
	total = bdb->bdb_size;

	/* walk the sections looking for section_id */
	while (index + 3 < total) {
		current_id = *(base + index);
		current_size = _get_blocksize(base + index);
		index += 3;

		if (index + current_size > total)
			return NULL;

		if (current_id == section_id)
			return base + index;

		index += current_size;
	}

	return NULL;
}

/*
 * Offset from the start of BDB to the start of the
 * block data (just past the block header).
 */
static u32 raw_block_offset(const void *bdb, enum bdb_block_id section_id)
{
	const void *block;

	block = find_raw_section(bdb, section_id);
	if (!block)
		return 0;

	return block - bdb;
}

struct bdb_block_entry {
	struct list_head node;
	enum bdb_block_id section_id;
	u8 data[];
};

static const void *
bdb_find_section(struct intel_display *display,
		 enum bdb_block_id section_id)
{
	struct bdb_block_entry *entry;

	list_for_each_entry(entry, &display->vbt.bdb_blocks, node) {
		if (entry->section_id == section_id)
			return entry->data + 3;
	}

	return NULL;
}

static const struct {
	enum bdb_block_id section_id;
	size_t min_size;
} bdb_blocks[] = {
	{ .section_id = BDB_GENERAL_FEATURES,
	  .min_size = sizeof(struct bdb_general_features), },
	{ .section_id = BDB_GENERAL_DEFINITIONS,
	  .min_size = sizeof(struct bdb_general_definitions), },
	{ .section_id = BDB_PSR,
	  .min_size = sizeof(struct bdb_psr), },
	{ .section_id = BDB_DRIVER_FEATURES,
	  .min_size = sizeof(struct bdb_driver_features), },
	{ .section_id = BDB_SDVO_LVDS_OPTIONS,
	  .min_size = sizeof(struct bdb_sdvo_lvds_options), },
	{ .section_id = BDB_SDVO_LVDS_DTD,
	  .min_size = sizeof(struct bdb_sdvo_lvds_dtd), },
	{ .section_id = BDB_EDP,
	  .min_size = sizeof(struct bdb_edp), },
	{ .section_id = BDB_LFP_OPTIONS,
	  .min_size = sizeof(struct bdb_lfp_options), },
	/*
	 * BDB_LFP_DATA depends on BDB_LFP_DATA_PTRS,
	 * so keep the two ordered.
	 */
	{ .section_id = BDB_LFP_DATA_PTRS,
	  .min_size = sizeof(struct bdb_lfp_data_ptrs), },
	{ .section_id = BDB_LFP_DATA,
	  .min_size = 0, /* special case */ },
	{ .section_id = BDB_LFP_BACKLIGHT,
	  .min_size = sizeof(struct bdb_lfp_backlight), },
	{ .section_id = BDB_LFP_POWER,
	  .min_size = sizeof(struct bdb_lfp_power), },
	{ .section_id = BDB_MIPI_CONFIG,
	  .min_size = sizeof(struct bdb_mipi_config), },
	{ .section_id = BDB_MIPI_SEQUENCE,
	  .min_size = sizeof(struct bdb_mipi_sequence) },
	{ .section_id = BDB_COMPRESSION_PARAMETERS,
	  .min_size = sizeof(struct bdb_compression_parameters), },
	{ .section_id = BDB_GENERIC_DTD,
	  .min_size = sizeof(struct bdb_generic_dtd), },
};

static size_t lfp_data_min_size(struct intel_display *display)
{
	const struct bdb_lfp_data_ptrs *ptrs;
	size_t size;

	ptrs = bdb_find_section(display, BDB_LFP_DATA_PTRS);
	if (!ptrs)
		return 0;

	size = sizeof(struct bdb_lfp_data);
	if (ptrs->panel_name.table_size)
		size = max(size, ptrs->panel_name.offset +
			   sizeof(struct bdb_lfp_data_tail));

	return size;
}

static bool validate_lfp_data_ptrs(const void *bdb,
				   const struct bdb_lfp_data_ptrs *ptrs)
{
	int fp_timing_size, dvo_timing_size, panel_pnp_id_size, panel_name_size;
	int data_block_size, lfp_data_size;
	const void *data_block;
	int i;

	data_block = find_raw_section(bdb, BDB_LFP_DATA);
	if (!data_block)
		return false;

	data_block_size = get_blocksize(data_block);
	if (data_block_size == 0)
		return false;

	/* always 3 indicating the presence of fp_timing+dvo_timing+panel_pnp_id */
	if (ptrs->num_entries != 3)
		return false;

	fp_timing_size = ptrs->ptr[0].fp_timing.table_size;
	dvo_timing_size = ptrs->ptr[0].dvo_timing.table_size;
	panel_pnp_id_size = ptrs->ptr[0].panel_pnp_id.table_size;
	panel_name_size = ptrs->panel_name.table_size;

	/* fp_timing has variable size */
	if (fp_timing_size < 32 ||
	    dvo_timing_size != sizeof(struct bdb_edid_dtd) ||
	    panel_pnp_id_size != sizeof(struct bdb_edid_pnp_id))
		return false;

	/* panel_name is not present in old VBTs */
	if (panel_name_size != 0 &&
	    panel_name_size != sizeof(struct bdb_edid_product_name))
		return false;

	lfp_data_size = ptrs->ptr[1].fp_timing.offset - ptrs->ptr[0].fp_timing.offset;
	if (16 * lfp_data_size > data_block_size)
		return false;

	/* make sure the table entries have uniform size */
	for (i = 1; i < 16; i++) {
		if (ptrs->ptr[i].fp_timing.table_size != fp_timing_size ||
		    ptrs->ptr[i].dvo_timing.table_size != dvo_timing_size ||
		    ptrs->ptr[i].panel_pnp_id.table_size != panel_pnp_id_size)
			return false;

		if (ptrs->ptr[i].fp_timing.offset - ptrs->ptr[i-1].fp_timing.offset != lfp_data_size ||
		    ptrs->ptr[i].dvo_timing.offset - ptrs->ptr[i-1].dvo_timing.offset != lfp_data_size ||
		    ptrs->ptr[i].panel_pnp_id.offset - ptrs->ptr[i-1].panel_pnp_id.offset != lfp_data_size)
			return false;
	}

	/*
	 * Except for vlv/chv machines all real VBTs seem to have 6
	 * unaccounted bytes in the fp_timing table. And it doesn't
	 * appear to be a really intentional hole as the fp_timing
	 * 0xffff terminator is always within those 6 missing bytes.
	 */
	if (fp_timing_size + 6 + dvo_timing_size + panel_pnp_id_size == lfp_data_size)
		fp_timing_size += 6;

	if (fp_timing_size + dvo_timing_size + panel_pnp_id_size != lfp_data_size)
		return false;

	if (ptrs->ptr[0].fp_timing.offset + fp_timing_size != ptrs->ptr[0].dvo_timing.offset ||
	    ptrs->ptr[0].dvo_timing.offset + dvo_timing_size != ptrs->ptr[0].panel_pnp_id.offset ||
	    ptrs->ptr[0].panel_pnp_id.offset + panel_pnp_id_size != lfp_data_size)
		return false;

	/* make sure the tables fit inside the data block */
	for (i = 0; i < 16; i++) {
		if (ptrs->ptr[i].fp_timing.offset + fp_timing_size > data_block_size ||
		    ptrs->ptr[i].dvo_timing.offset + dvo_timing_size > data_block_size ||
		    ptrs->ptr[i].panel_pnp_id.offset + panel_pnp_id_size > data_block_size)
			return false;
	}

	if (ptrs->panel_name.offset + 16 * panel_name_size > data_block_size)
		return false;

	/* make sure fp_timing terminators are present at expected locations */
	for (i = 0; i < 16; i++) {
		const u16 *t = data_block + ptrs->ptr[i].fp_timing.offset +
			fp_timing_size - 2;

		if (*t != 0xffff)
			return false;
	}

	return true;
}

/* make the data table offsets relative to the data block */
static bool fixup_lfp_data_ptrs(const void *bdb, void *ptrs_block)
{
	struct bdb_lfp_data_ptrs *ptrs = ptrs_block;
	u32 offset;
	int i;

	offset = raw_block_offset(bdb, BDB_LFP_DATA);

	for (i = 0; i < 16; i++) {
		if (ptrs->ptr[i].fp_timing.offset < offset ||
		    ptrs->ptr[i].dvo_timing.offset < offset ||
		    ptrs->ptr[i].panel_pnp_id.offset < offset)
			return false;

		ptrs->ptr[i].fp_timing.offset -= offset;
		ptrs->ptr[i].dvo_timing.offset -= offset;
		ptrs->ptr[i].panel_pnp_id.offset -= offset;
	}

	if (ptrs->panel_name.table_size) {
		if (ptrs->panel_name.offset < offset)
			return false;

		ptrs->panel_name.offset -= offset;
	}

	return validate_lfp_data_ptrs(bdb, ptrs);
}

static int make_lfp_data_ptr(struct lfp_data_ptr_table *table,
			     int table_size, int total_size)
{
	if (total_size < table_size)
		return total_size;

	table->table_size = table_size;
	table->offset = total_size - table_size;

	return total_size - table_size;
}

static void next_lfp_data_ptr(struct lfp_data_ptr_table *next,
			      const struct lfp_data_ptr_table *prev,
			      int size)
{
	next->table_size = prev->table_size;
	next->offset = prev->offset + size;
}

static void *generate_lfp_data_ptrs(struct intel_display *display,
				    const void *bdb)
{
	int i, size, table_size, block_size, offset, fp_timing_size;
	struct bdb_lfp_data_ptrs *ptrs;
	const void *block;
	void *ptrs_block;

	/*
	 * The hardcoded fp_timing_size is only valid for
	 * modernish VBTs. All older VBTs definitely should
	 * include block 41 and thus we don't need to
	 * generate one.
	 */
	if (display->vbt.version < 155)
		return NULL;

	fp_timing_size = 38;

	block = find_raw_section(bdb, BDB_LFP_DATA);
	if (!block)
		return NULL;

	drm_dbg_kms(display->drm, "Generating LFP data table pointers\n");

	block_size = get_blocksize(block);

	size = fp_timing_size + sizeof(struct bdb_edid_dtd) +
		sizeof(struct bdb_edid_pnp_id);
	if (size * 16 > block_size)
		return NULL;

	ptrs_block = kzalloc(sizeof(*ptrs) + 3, GFP_KERNEL);
	if (!ptrs_block)
		return NULL;

	*(u8 *)(ptrs_block + 0) = BDB_LFP_DATA_PTRS;
	*(u16 *)(ptrs_block + 1) = sizeof(*ptrs);
	ptrs = ptrs_block + 3;

	table_size = sizeof(struct bdb_edid_pnp_id);
	size = make_lfp_data_ptr(&ptrs->ptr[0].panel_pnp_id, table_size, size);

	table_size = sizeof(struct bdb_edid_dtd);
	size = make_lfp_data_ptr(&ptrs->ptr[0].dvo_timing, table_size, size);

	table_size = fp_timing_size;
	size = make_lfp_data_ptr(&ptrs->ptr[0].fp_timing, table_size, size);

	if (ptrs->ptr[0].fp_timing.table_size)
		ptrs->num_entries++;
	if (ptrs->ptr[0].dvo_timing.table_size)
		ptrs->num_entries++;
	if (ptrs->ptr[0].panel_pnp_id.table_size)
		ptrs->num_entries++;

	if (size != 0 || ptrs->num_entries != 3) {
		kfree(ptrs_block);
		return NULL;
	}

	size = fp_timing_size + sizeof(struct bdb_edid_dtd) +
		sizeof(struct bdb_edid_pnp_id);
	for (i = 1; i < 16; i++) {
		next_lfp_data_ptr(&ptrs->ptr[i].fp_timing, &ptrs->ptr[i-1].fp_timing, size);
		next_lfp_data_ptr(&ptrs->ptr[i].dvo_timing, &ptrs->ptr[i-1].dvo_timing, size);
		next_lfp_data_ptr(&ptrs->ptr[i].panel_pnp_id, &ptrs->ptr[i-1].panel_pnp_id, size);
	}

	table_size = sizeof(struct bdb_edid_product_name);

	if (16 * (size + table_size) <= block_size) {
		ptrs->panel_name.table_size = table_size;
		ptrs->panel_name.offset = size * 16;
	}

	offset = block - bdb;

	for (i = 0; i < 16; i++) {
		ptrs->ptr[i].fp_timing.offset += offset;
		ptrs->ptr[i].dvo_timing.offset += offset;
		ptrs->ptr[i].panel_pnp_id.offset += offset;
	}

	if (ptrs->panel_name.table_size)
		ptrs->panel_name.offset += offset;

	return ptrs_block;
}

static void
init_bdb_block(struct intel_display *display,
	       const void *bdb, enum bdb_block_id section_id,
	       size_t min_size)
{
	struct bdb_block_entry *entry;
	void *temp_block = NULL;
	const void *block;
	size_t block_size;

	block = find_raw_section(bdb, section_id);

	/* Modern VBTs lack the LFP data table pointers block, make one up */
	if (!block && section_id == BDB_LFP_DATA_PTRS) {
		temp_block = generate_lfp_data_ptrs(display, bdb);
		if (temp_block)
			block = temp_block + 3;
	}
	if (!block)
		return;

	drm_WARN(display->drm, min_size == 0,
		 "Block %d min_size is zero\n", section_id);

	block_size = get_blocksize(block);

	/*
	 * Version number and new block size are considered
	 * part of the header for MIPI sequenece block v3+.
	 */
	if (section_id == BDB_MIPI_SEQUENCE && *(const u8 *)block >= 3)
		block_size += 5;

	entry = kzalloc(struct_size(entry, data, max(min_size, block_size) + 3),
			GFP_KERNEL);
	if (!entry) {
		kfree(temp_block);
		return;
	}

	entry->section_id = section_id;
	memcpy(entry->data, block - 3, block_size + 3);

	kfree(temp_block);

	drm_dbg_kms(display->drm,
		    "Found BDB block %d (size %zu, min size %zu)\n",
		    section_id, block_size, min_size);

	if (section_id == BDB_LFP_DATA_PTRS &&
	    !fixup_lfp_data_ptrs(bdb, entry->data + 3)) {
		drm_err(display->drm,
			"VBT has malformed LFP data table pointers\n");
		kfree(entry);
		return;
	}

	list_add_tail(&entry->node, &display->vbt.bdb_blocks);
}

static void init_bdb_blocks(struct intel_display *display,
			    const void *bdb)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bdb_blocks); i++) {
		enum bdb_block_id section_id = bdb_blocks[i].section_id;
		size_t min_size = bdb_blocks[i].min_size;

		if (section_id == BDB_LFP_DATA)
			min_size = lfp_data_min_size(display);

		init_bdb_block(display, bdb, section_id, min_size);
	}
}

static void
fill_detail_timing_data(struct intel_display *display,
			struct drm_display_mode *panel_fixed_mode,
			const struct bdb_edid_dtd *dvo_timing)
{
	panel_fixed_mode->hdisplay = (dvo_timing->hactive_hi << 8) |
		dvo_timing->hactive_lo;
	panel_fixed_mode->hsync_start = panel_fixed_mode->hdisplay +
		((dvo_timing->hsync_off_hi << 8) | dvo_timing->hsync_off_lo);
	panel_fixed_mode->hsync_end = panel_fixed_mode->hsync_start +
		((dvo_timing->hsync_pulse_width_hi << 8) |
			dvo_timing->hsync_pulse_width_lo);
	panel_fixed_mode->htotal = panel_fixed_mode->hdisplay +
		((dvo_timing->hblank_hi << 8) | dvo_timing->hblank_lo);

	panel_fixed_mode->vdisplay = (dvo_timing->vactive_hi << 8) |
		dvo_timing->vactive_lo;
	panel_fixed_mode->vsync_start = panel_fixed_mode->vdisplay +
		((dvo_timing->vsync_off_hi << 4) | dvo_timing->vsync_off_lo);
	panel_fixed_mode->vsync_end = panel_fixed_mode->vsync_start +
		((dvo_timing->vsync_pulse_width_hi << 4) |
			dvo_timing->vsync_pulse_width_lo);
	panel_fixed_mode->vtotal = panel_fixed_mode->vdisplay +
		((dvo_timing->vblank_hi << 8) | dvo_timing->vblank_lo);
	panel_fixed_mode->clock = dvo_timing->clock * 10;
	panel_fixed_mode->type = DRM_MODE_TYPE_PREFERRED;

	if (dvo_timing->hsync_positive)
		panel_fixed_mode->flags |= DRM_MODE_FLAG_PHSYNC;
	else
		panel_fixed_mode->flags |= DRM_MODE_FLAG_NHSYNC;

	if (dvo_timing->vsync_positive)
		panel_fixed_mode->flags |= DRM_MODE_FLAG_PVSYNC;
	else
		panel_fixed_mode->flags |= DRM_MODE_FLAG_NVSYNC;

	panel_fixed_mode->width_mm = (dvo_timing->himage_hi << 8) |
		dvo_timing->himage_lo;
	panel_fixed_mode->height_mm = (dvo_timing->vimage_hi << 8) |
		dvo_timing->vimage_lo;

	/* Some VBTs have bogus h/vsync_end values */
	if (panel_fixed_mode->hsync_end > panel_fixed_mode->htotal) {
		drm_dbg_kms(display->drm, "reducing hsync_end %d->%d\n",
			    panel_fixed_mode->hsync_end, panel_fixed_mode->htotal);
		panel_fixed_mode->hsync_end = panel_fixed_mode->htotal;
	}
	if (panel_fixed_mode->vsync_end > panel_fixed_mode->vtotal) {
		drm_dbg_kms(display->drm, "reducing vsync_end %d->%d\n",
			    panel_fixed_mode->vsync_end, panel_fixed_mode->vtotal);
		panel_fixed_mode->vsync_end = panel_fixed_mode->vtotal;
	}

	drm_mode_set_name(panel_fixed_mode);
}

static const struct bdb_edid_dtd *
get_lfp_dvo_timing(const struct bdb_lfp_data *data,
		   const struct bdb_lfp_data_ptrs *ptrs,
		   int index)
{
	return (const void *)data + ptrs->ptr[index].dvo_timing.offset;
}

static const struct fp_timing *
get_lfp_fp_timing(const struct bdb_lfp_data *data,
		  const struct bdb_lfp_data_ptrs *ptrs,
		  int index)
{
	return (const void *)data + ptrs->ptr[index].fp_timing.offset;
}

static const struct drm_edid_product_id *
get_lfp_pnp_id(const struct bdb_lfp_data *data,
	       const struct bdb_lfp_data_ptrs *ptrs,
	       int index)
{
	/* These two are supposed to have the same layout in memory. */
	BUILD_BUG_ON(sizeof(struct bdb_edid_pnp_id) != sizeof(struct drm_edid_product_id));

	return (const void *)data + ptrs->ptr[index].panel_pnp_id.offset;
}

static const struct bdb_lfp_data_tail *
get_lfp_data_tail(const struct bdb_lfp_data *data,
		  const struct bdb_lfp_data_ptrs *ptrs)
{
	if (ptrs->panel_name.table_size)
		return (const void *)data + ptrs->panel_name.offset;
	else
		return NULL;
}

static int opregion_get_panel_type(struct intel_display *display,
				   const struct intel_bios_encoder_data *devdata,
				   const struct drm_edid *drm_edid, bool use_fallback)
{
	return intel_opregion_get_panel_type(display);
}

static int vbt_get_panel_type(struct intel_display *display,
			      const struct intel_bios_encoder_data *devdata,
			      const struct drm_edid *drm_edid, bool use_fallback)
{
	const struct bdb_lfp_options *lfp_options;

	lfp_options = bdb_find_section(display, BDB_LFP_OPTIONS);
	if (!lfp_options)
		return -1;

	if (lfp_options->panel_type > 0xf &&
	    lfp_options->panel_type != 0xff) {
		drm_dbg_kms(display->drm, "Invalid VBT panel type 0x%x\n",
			    lfp_options->panel_type);
		return -1;
	}

	if (devdata && devdata->child.handle == DEVICE_HANDLE_LFP2)
		return lfp_options->panel_type2;

	drm_WARN_ON(display->drm,
		    devdata && devdata->child.handle != DEVICE_HANDLE_LFP1);

	return lfp_options->panel_type;
}

static int pnpid_get_panel_type(struct intel_display *display,
				const struct intel_bios_encoder_data *devdata,
				const struct drm_edid *drm_edid, bool use_fallback)
{
	const struct bdb_lfp_data *data;
	const struct bdb_lfp_data_ptrs *ptrs;
	struct drm_edid_product_id product_id, product_id_nodate;
	struct drm_printer p;
	int i, best = -1;

	if (!drm_edid)
		return -1;

	drm_edid_get_product_id(drm_edid, &product_id);

	product_id_nodate = product_id;
	product_id_nodate.week_of_manufacture = 0;
	product_id_nodate.year_of_manufacture = 0;

	p = drm_dbg_printer(display->drm, DRM_UT_KMS, "EDID");
	drm_edid_print_product_id(&p, &product_id, true);

	ptrs = bdb_find_section(display, BDB_LFP_DATA_PTRS);
	if (!ptrs)
		return -1;

	data = bdb_find_section(display, BDB_LFP_DATA);
	if (!data)
		return -1;

	for (i = 0; i < 16; i++) {
		const struct drm_edid_product_id *vbt_id =
			get_lfp_pnp_id(data, ptrs, i);

		/* full match? */
		if (!memcmp(vbt_id, &product_id, sizeof(*vbt_id)))
			return i;

		/*
		 * Accept a match w/o date if no full match is found,
		 * and the VBT entry does not specify a date.
		 */
		if (best < 0 &&
		    !memcmp(vbt_id, &product_id_nodate, sizeof(*vbt_id)))
			best = i;
	}

	return best;
}

static int fallback_get_panel_type(struct intel_display *display,
				   const struct intel_bios_encoder_data *devdata,
				   const struct drm_edid *drm_edid, bool use_fallback)
{
	return use_fallback ? 0 : -1;
}

enum panel_type {
	PANEL_TYPE_OPREGION,
	PANEL_TYPE_VBT,
	PANEL_TYPE_PNPID,
	PANEL_TYPE_FALLBACK,
};

static int get_panel_type(struct intel_display *display,
			  const struct intel_bios_encoder_data *devdata,
			  const struct drm_edid *drm_edid, bool use_fallback)
{
	struct {
		const char *name;
		int (*get_panel_type)(struct intel_display *display,
				      const struct intel_bios_encoder_data *devdata,
				      const struct drm_edid *drm_edid, bool use_fallback);
		int panel_type;
	} panel_types[] = {
		[PANEL_TYPE_OPREGION] = {
			.name = "OpRegion",
			.get_panel_type = opregion_get_panel_type,
		},
		[PANEL_TYPE_VBT] = {
			.name = "VBT",
			.get_panel_type = vbt_get_panel_type,
		},
		[PANEL_TYPE_PNPID] = {
			.name = "PNPID",
			.get_panel_type = pnpid_get_panel_type,
		},
		[PANEL_TYPE_FALLBACK] = {
			.name = "fallback",
			.get_panel_type = fallback_get_panel_type,
		},
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(panel_types); i++) {
		panel_types[i].panel_type = panel_types[i].get_panel_type(display, devdata,
									  drm_edid, use_fallback);

		drm_WARN_ON(display->drm, panel_types[i].panel_type > 0xf &&
			    panel_types[i].panel_type != 0xff);

		if (panel_types[i].panel_type >= 0)
			drm_dbg_kms(display->drm, "Panel type (%s): %d\n",
				    panel_types[i].name, panel_types[i].panel_type);
	}

	if (panel_types[PANEL_TYPE_OPREGION].panel_type >= 0)
		i = PANEL_TYPE_OPREGION;
	else if (panel_types[PANEL_TYPE_VBT].panel_type == 0xff &&
		 panel_types[PANEL_TYPE_PNPID].panel_type >= 0)
		i = PANEL_TYPE_PNPID;
	else if (panel_types[PANEL_TYPE_VBT].panel_type != 0xff &&
		 panel_types[PANEL_TYPE_VBT].panel_type >= 0)
		i = PANEL_TYPE_VBT;
	else
		i = PANEL_TYPE_FALLBACK;

	drm_dbg_kms(display->drm, "Selected panel type (%s): %d\n",
		    panel_types[i].name, panel_types[i].panel_type);

	return panel_types[i].panel_type;
}

static unsigned int panel_bits(unsigned int value, int panel_type, int num_bits)
{
	return (value >> (panel_type * num_bits)) & (BIT(num_bits) - 1);
}

static bool panel_bool(unsigned int value, int panel_type)
{
	return panel_bits(value, panel_type, 1);
}

/* Parse general panel options */
static void
parse_panel_options(struct intel_display *display,
		    struct intel_panel *panel)
{
	const struct bdb_lfp_options *lfp_options;
	int panel_type = panel->vbt.panel_type;
	int drrs_mode;

	lfp_options = bdb_find_section(display, BDB_LFP_OPTIONS);
	if (!lfp_options)
		return;

	panel->vbt.lvds_dither = lfp_options->pixel_dither;

	/*
	 * Empirical evidence indicates the block size can be
	 * either 4,14,16,24+ bytes. For older VBTs no clear
	 * relationship between the block size vs. BDB version.
	 */
	if (get_blocksize(lfp_options) < 16)
		return;

	drrs_mode = panel_bits(lfp_options->dps_panel_type_bits,
			       panel_type, 2);
	/*
	 * VBT has static DRRS = 0 and seamless DRRS = 2.
	 * The below piece of code is required to adjust vbt.drrs_type
	 * to match the enum drrs_support_type.
	 */
	switch (drrs_mode) {
	case 0:
		panel->vbt.drrs_type = DRRS_TYPE_STATIC;
		drm_dbg_kms(display->drm, "DRRS supported mode is static\n");
		break;
	case 2:
		panel->vbt.drrs_type = DRRS_TYPE_SEAMLESS;
		drm_dbg_kms(display->drm,
			    "DRRS supported mode is seamless\n");
		break;
	default:
		panel->vbt.drrs_type = DRRS_TYPE_NONE;
		drm_dbg_kms(display->drm,
			    "DRRS not supported (VBT input)\n");
		break;
	}
}

static void
parse_lfp_panel_dtd(struct intel_display *display,
		    struct intel_panel *panel,
		    const struct bdb_lfp_data *lfp_data,
		    const struct bdb_lfp_data_ptrs *lfp_data_ptrs)
{
	const struct bdb_edid_dtd *panel_dvo_timing;
	const struct fp_timing *fp_timing;
	struct drm_display_mode *panel_fixed_mode;
	int panel_type = panel->vbt.panel_type;

	panel_dvo_timing = get_lfp_dvo_timing(lfp_data,
					      lfp_data_ptrs,
					      panel_type);

	panel_fixed_mode = kzalloc(sizeof(*panel_fixed_mode), GFP_KERNEL);
	if (!panel_fixed_mode)
		return;

	fill_detail_timing_data(display, panel_fixed_mode, panel_dvo_timing);

	panel->vbt.lfp_vbt_mode = panel_fixed_mode;

	drm_dbg_kms(display->drm,
		    "Found panel mode in BIOS VBT legacy lfp table: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(panel_fixed_mode));

	fp_timing = get_lfp_fp_timing(lfp_data,
				      lfp_data_ptrs,
				      panel_type);

	/* check the resolution, just to be sure */
	if (fp_timing->x_res == panel_fixed_mode->hdisplay &&
	    fp_timing->y_res == panel_fixed_mode->vdisplay) {
		panel->vbt.bios_lvds_val = fp_timing->lvds_reg_val;
		drm_dbg_kms(display->drm,
			    "VBT initial LVDS value %x\n",
			    panel->vbt.bios_lvds_val);
	}
}

static void
parse_lfp_data(struct intel_display *display,
	       struct intel_panel *panel)
{
	const struct bdb_lfp_data *data;
	const struct bdb_lfp_data_tail *tail;
	const struct bdb_lfp_data_ptrs *ptrs;
	const struct drm_edid_product_id *pnp_id;
	struct drm_printer p;
	int panel_type = panel->vbt.panel_type;

	ptrs = bdb_find_section(display, BDB_LFP_DATA_PTRS);
	if (!ptrs)
		return;

	data = bdb_find_section(display, BDB_LFP_DATA);
	if (!data)
		return;

	if (!panel->vbt.lfp_vbt_mode)
		parse_lfp_panel_dtd(display, panel, data, ptrs);

	pnp_id = get_lfp_pnp_id(data, ptrs, panel_type);

	p = drm_dbg_printer(display->drm, DRM_UT_KMS, "Panel");
	drm_edid_print_product_id(&p, pnp_id, false);

	tail = get_lfp_data_tail(data, ptrs);
	if (!tail)
		return;

	drm_dbg_kms(display->drm, "Panel name: %.*s\n",
		    (int)sizeof(tail->panel_name[0].name),
		    tail->panel_name[panel_type].name);

	if (display->vbt.version >= 188) {
		panel->vbt.seamless_drrs_min_refresh_rate =
			tail->seamless_drrs_min_refresh_rate[panel_type];
		drm_dbg_kms(display->drm,
			    "Seamless DRRS min refresh rate: %d Hz\n",
			    panel->vbt.seamless_drrs_min_refresh_rate);
	}
}

static void
parse_generic_dtd(struct intel_display *display,
		  struct intel_panel *panel)
{
	const struct bdb_generic_dtd *generic_dtd;
	const struct generic_dtd_entry *dtd;
	struct drm_display_mode *panel_fixed_mode;
	int num_dtd;

	/*
	 * Older VBTs provided DTD information for internal displays through
	 * the "LFP panel tables" block (42).  As of VBT revision 229 the
	 * DTD information should be provided via a newer "generic DTD"
	 * block (58).  Just to be safe, we'll try the new generic DTD block
	 * first on VBT >= 229, but still fall back to trying the old LFP
	 * block if that fails.
	 */
	if (display->vbt.version < 229)
		return;

	generic_dtd = bdb_find_section(display, BDB_GENERIC_DTD);
	if (!generic_dtd)
		return;

	if (generic_dtd->gdtd_size < sizeof(struct generic_dtd_entry)) {
		drm_err(display->drm, "GDTD size %u is too small.\n",
			generic_dtd->gdtd_size);
		return;
	} else if (generic_dtd->gdtd_size !=
		   sizeof(struct generic_dtd_entry)) {
		drm_err(display->drm, "Unexpected GDTD size %u\n",
			generic_dtd->gdtd_size);
		/* DTD has unknown fields, but keep going */
	}

	num_dtd = (get_blocksize(generic_dtd) -
		   sizeof(struct bdb_generic_dtd)) / generic_dtd->gdtd_size;
	if (panel->vbt.panel_type >= num_dtd) {
		drm_err(display->drm,
			"Panel type %d not found in table of %d DTD's\n",
			panel->vbt.panel_type, num_dtd);
		return;
	}

	dtd = &generic_dtd->dtd[panel->vbt.panel_type];

	panel_fixed_mode = kzalloc(sizeof(*panel_fixed_mode), GFP_KERNEL);
	if (!panel_fixed_mode)
		return;

	panel_fixed_mode->hdisplay = dtd->hactive;
	panel_fixed_mode->hsync_start =
		panel_fixed_mode->hdisplay + dtd->hfront_porch;
	panel_fixed_mode->hsync_end =
		panel_fixed_mode->hsync_start + dtd->hsync;
	panel_fixed_mode->htotal =
		panel_fixed_mode->hdisplay + dtd->hblank;

	panel_fixed_mode->vdisplay = dtd->vactive;
	panel_fixed_mode->vsync_start =
		panel_fixed_mode->vdisplay + dtd->vfront_porch;
	panel_fixed_mode->vsync_end =
		panel_fixed_mode->vsync_start + dtd->vsync;
	panel_fixed_mode->vtotal =
		panel_fixed_mode->vdisplay + dtd->vblank;

	panel_fixed_mode->clock = dtd->pixel_clock;
	panel_fixed_mode->width_mm = dtd->width_mm;
	panel_fixed_mode->height_mm = dtd->height_mm;

	panel_fixed_mode->type = DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(panel_fixed_mode);

	if (dtd->hsync_positive_polarity)
		panel_fixed_mode->flags |= DRM_MODE_FLAG_PHSYNC;
	else
		panel_fixed_mode->flags |= DRM_MODE_FLAG_NHSYNC;

	if (dtd->vsync_positive_polarity)
		panel_fixed_mode->flags |= DRM_MODE_FLAG_PVSYNC;
	else
		panel_fixed_mode->flags |= DRM_MODE_FLAG_NVSYNC;

	drm_dbg_kms(display->drm,
		    "Found panel mode in BIOS VBT generic dtd table: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(panel_fixed_mode));

	panel->vbt.lfp_vbt_mode = panel_fixed_mode;
}

static void
parse_lfp_backlight(struct intel_display *display,
		    struct intel_panel *panel)
{
	const struct bdb_lfp_backlight *backlight_data;
	const struct lfp_backlight_data_entry *entry;
	int panel_type = panel->vbt.panel_type;
	u16 level;

	backlight_data = bdb_find_section(display, BDB_LFP_BACKLIGHT);
	if (!backlight_data)
		return;

	if (backlight_data->entry_size != sizeof(backlight_data->data[0])) {
		drm_dbg_kms(display->drm,
			    "Unsupported backlight data entry size %u\n",
			    backlight_data->entry_size);
		return;
	}

	entry = &backlight_data->data[panel_type];

	panel->vbt.backlight.present = entry->type == BDB_BACKLIGHT_TYPE_PWM;
	if (!panel->vbt.backlight.present) {
		drm_dbg_kms(display->drm,
			    "PWM backlight not present in VBT (type %u)\n",
			    entry->type);
		return;
	}

	panel->vbt.backlight.type = INTEL_BACKLIGHT_DISPLAY_DDI;
	panel->vbt.backlight.controller = 0;
	if (display->vbt.version >= 191) {
		const struct lfp_backlight_control_method *method;

		method = &backlight_data->backlight_control[panel_type];
		panel->vbt.backlight.type = method->type;
		panel->vbt.backlight.controller = method->controller;
	}

	panel->vbt.backlight.pwm_freq_hz = entry->pwm_freq_hz;
	panel->vbt.backlight.active_low_pwm = entry->active_low_pwm;

	if (display->vbt.version >= 234) {
		u16 min_level;
		bool scale;

		level = backlight_data->brightness_level[panel_type].level;
		min_level = backlight_data->brightness_min_level[panel_type].level;

		if (display->vbt.version >= 236)
			scale = backlight_data->brightness_precision_bits[panel_type] == 16;
		else
			scale = level > 255;

		if (scale)
			min_level = min_level / 255;

		if (min_level > 255) {
			drm_warn(display->drm, "Brightness min level > 255\n");
			level = 255;
		}
		panel->vbt.backlight.min_brightness = min_level;

		panel->vbt.backlight.brightness_precision_bits =
			backlight_data->brightness_precision_bits[panel_type];
	} else {
		level = backlight_data->level[panel_type];
		panel->vbt.backlight.min_brightness = entry->min_brightness;
	}

	if (display->vbt.version >= 239)
		panel->vbt.backlight.hdr_dpcd_refresh_timeout =
			DIV_ROUND_UP(backlight_data->hdr_dpcd_refresh_timeout[panel_type], 100);
	else
		panel->vbt.backlight.hdr_dpcd_refresh_timeout = 30;

	drm_dbg_kms(display->drm,
		    "VBT backlight PWM modulation frequency %u Hz, "
		    "active %s, min brightness %u, level %u, controller %u\n",
		    panel->vbt.backlight.pwm_freq_hz,
		    panel->vbt.backlight.active_low_pwm ? "low" : "high",
		    panel->vbt.backlight.min_brightness,
		    level,
		    panel->vbt.backlight.controller);
}

static void
parse_sdvo_lvds_data(struct intel_display *display,
		     struct intel_panel *panel)
{
	const struct bdb_sdvo_lvds_dtd *dtd;
	struct drm_display_mode *panel_fixed_mode;
	int index;

	index = display->params.vbt_sdvo_panel_type;
	if (index == -2) {
		drm_dbg_kms(display->drm,
			    "Ignore SDVO LVDS mode from BIOS VBT tables.\n");
		return;
	}

	if (index == -1) {
		const struct bdb_sdvo_lvds_options *sdvo_lvds_options;

		sdvo_lvds_options = bdb_find_section(display, BDB_SDVO_LVDS_OPTIONS);
		if (!sdvo_lvds_options)
			return;

		index = sdvo_lvds_options->panel_type;
	}

	dtd = bdb_find_section(display, BDB_SDVO_LVDS_DTD);
	if (!dtd)
		return;

	/*
	 * This should not happen, as long as the panel_type
	 * enumeration doesn't grow over 4 items.  But if it does, it
	 * could lead to hard-to-detect bugs, so better double-check
	 * it here to be sure.
	 */
	if (index >= ARRAY_SIZE(dtd->dtd)) {
		drm_err(display->drm,
			"index %d is larger than dtd->dtd[4] array\n",
			index);
		return;
	}

	panel_fixed_mode = kzalloc(sizeof(*panel_fixed_mode), GFP_KERNEL);
	if (!panel_fixed_mode)
		return;

	fill_detail_timing_data(display, panel_fixed_mode, &dtd->dtd[index]);

	panel->vbt.sdvo_lvds_vbt_mode = panel_fixed_mode;

	drm_dbg_kms(display->drm,
		    "Found SDVO LVDS mode in BIOS VBT tables: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(panel_fixed_mode));
}

static int intel_bios_ssc_frequency(struct intel_display *display,
				    bool alternate)
{
	switch (DISPLAY_VER(display)) {
	case 2:
		return alternate ? 66667 : 48000;
	case 3:
	case 4:
		return alternate ? 100000 : 96000;
	default:
		return alternate ? 100000 : 120000;
	}
}

static void
parse_general_features(struct intel_display *display)
{
	const struct bdb_general_features *general;

	general = bdb_find_section(display, BDB_GENERAL_FEATURES);
	if (!general)
		return;

	display->vbt.int_tv_support = general->int_tv_support;
	/* int_crt_support can't be trusted on earlier platforms */
	if (display->vbt.version >= 155 &&
	    (HAS_DDI(display) || display->platform.valleyview))
		display->vbt.int_crt_support = general->int_crt_support;
	display->vbt.lvds_use_ssc = general->enable_ssc;
	display->vbt.lvds_ssc_freq =
		intel_bios_ssc_frequency(display, general->ssc_freq);
	display->vbt.display_clock_mode = general->display_clock_mode;
	display->vbt.fdi_rx_polarity_inverted = general->fdi_rx_polarity_inverted;
	if (display->vbt.version >= 181) {
		display->vbt.orientation = general->rotate_180 ?
			DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP :
			DRM_MODE_PANEL_ORIENTATION_NORMAL;
	} else {
		display->vbt.orientation = DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
	}

	if (display->vbt.version >= 249 && general->afc_startup_config) {
		display->vbt.override_afc_startup = true;
		display->vbt.override_afc_startup_val = general->afc_startup_config == 1 ? 0 : 7;
	}

	drm_dbg_kms(display->drm,
		    "BDB_GENERAL_FEATURES int_tv_support %d int_crt_support %d lvds_use_ssc %d lvds_ssc_freq %d display_clock_mode %d fdi_rx_polarity_inverted %d\n",
		    display->vbt.int_tv_support,
		    display->vbt.int_crt_support,
		    display->vbt.lvds_use_ssc,
		    display->vbt.lvds_ssc_freq,
		    display->vbt.display_clock_mode,
		    display->vbt.fdi_rx_polarity_inverted);
}

static const struct child_device_config *
child_device_ptr(const struct bdb_general_definitions *defs, int i)
{
	return (const void *) &defs->devices[i * defs->child_dev_size];
}

static void
parse_sdvo_device_mapping(struct intel_display *display)
{
	const struct intel_bios_encoder_data *devdata;
	int count = 0;

	/*
	 * Only parse SDVO mappings on gens that could have SDVO. This isn't
	 * accurate and doesn't have to be, as long as it's not too strict.
	 */
	if (!IS_DISPLAY_VER(display, 3, 7)) {
		drm_dbg_kms(display->drm, "Skipping SDVO device mapping\n");
		return;
	}

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		const struct child_device_config *child = &devdata->child;
		struct sdvo_device_mapping *mapping;

		if (child->target_addr != TARGET_ADDR1 &&
		    child->target_addr != TARGET_ADDR2) {
			/*
			 * If the target address is neither 0x70 nor 0x72,
			 * it is not a SDVO device. Skip it.
			 */
			continue;
		}
		if (child->dvo_port != DEVICE_PORT_DVOB &&
		    child->dvo_port != DEVICE_PORT_DVOC) {
			/* skip the incorrect SDVO port */
			drm_dbg_kms(display->drm,
				    "Incorrect SDVO port. Skip it\n");
			continue;
		}
		drm_dbg_kms(display->drm,
			    "the SDVO device with target addr %2x is found on"
			    " %s port\n",
			    child->target_addr,
			    (child->dvo_port == DEVICE_PORT_DVOB) ?
			    "SDVOB" : "SDVOC");
		mapping = &display->vbt.sdvo_mappings[child->dvo_port - 1];
		if (!mapping->initialized) {
			mapping->dvo_port = child->dvo_port;
			mapping->target_addr = child->target_addr;
			mapping->dvo_wiring = child->dvo_wiring;
			mapping->ddc_pin = child->ddc_pin;
			mapping->i2c_pin = child->i2c_pin;
			mapping->initialized = 1;
			drm_dbg_kms(display->drm,
				    "SDVO device: dvo=%x, addr=%x, wiring=%d, ddc_pin=%d, i2c_pin=%d\n",
				    mapping->dvo_port, mapping->target_addr,
				    mapping->dvo_wiring, mapping->ddc_pin,
				    mapping->i2c_pin);
		} else {
			drm_dbg_kms(display->drm,
				    "Maybe one SDVO port is shared by "
				    "two SDVO device.\n");
		}
		if (child->target2_addr) {
			/* Maybe this is a SDVO device with multiple inputs */
			/* And the mapping info is not added */
			drm_dbg_kms(display->drm,
				    "there exists the target2_addr. Maybe this"
				    " is a SDVO device with multiple inputs.\n");
		}
		count++;
	}

	if (!count) {
		/* No SDVO device info is found */
		drm_dbg_kms(display->drm,
			    "No SDVO device info is found in VBT\n");
	}
}

static void
parse_driver_features(struct intel_display *display)
{
	const struct bdb_driver_features *driver;

	driver = bdb_find_section(display, BDB_DRIVER_FEATURES);
	if (!driver)
		return;

	if (DISPLAY_VER(display) >= 5) {
		/*
		 * Note that we consider BDB_DRIVER_FEATURE_INT_SDVO_LVDS
		 * to mean "eDP". The VBT spec doesn't agree with that
		 * interpretation, but real world VBTs seem to.
		 */
		if (driver->lvds_config != BDB_DRIVER_FEATURE_INT_LVDS)
			display->vbt.int_lvds_support = 0;
	} else {
		/*
		 * FIXME it's not clear which BDB version has the LVDS config
		 * bits defined. Revision history in the VBT spec says:
		 * "0.92 | Add two definitions for VBT value of LVDS Active
		 *  Config (00b and 11b values defined) | 06/13/2005"
		 * but does not the specify the BDB version.
		 *
		 * So far version 134 (on i945gm) is the oldest VBT observed
		 * in the wild with the bits correctly populated. Version
		 * 108 (on i85x) does not have the bits correctly populated.
		 */
		if (display->vbt.version >= 134 &&
		    driver->lvds_config != BDB_DRIVER_FEATURE_INT_LVDS &&
		    driver->lvds_config != BDB_DRIVER_FEATURE_INT_SDVO_LVDS)
			display->vbt.int_lvds_support = 0;
	}
}

static void
parse_panel_driver_features(struct intel_display *display,
			    struct intel_panel *panel)
{
	const struct bdb_driver_features *driver;

	driver = bdb_find_section(display, BDB_DRIVER_FEATURES);
	if (!driver)
		return;

	if (display->vbt.version < 228) {
		drm_dbg_kms(display->drm, "DRRS State Enabled:%d\n",
			    driver->drrs_enabled);
		/*
		 * If DRRS is not supported, drrs_type has to be set to 0.
		 * This is because, VBT is configured in such a way that
		 * static DRRS is 0 and DRRS not supported is represented by
		 * driver->drrs_enabled=false
		 */
		if (!driver->drrs_enabled && panel->vbt.drrs_type != DRRS_TYPE_NONE) {
			/*
			 * FIXME Should DMRRS perhaps be treated as seamless
			 * but without the automatic downclocking?
			 */
			if (driver->dmrrs_enabled)
				panel->vbt.drrs_type = DRRS_TYPE_STATIC;
			else
				panel->vbt.drrs_type = DRRS_TYPE_NONE;
		}

		panel->vbt.psr.enable = driver->psr_enabled;
	}
}

static void
parse_power_conservation_features(struct intel_display *display,
				  struct intel_panel *panel)
{
	const struct bdb_lfp_power *power;
	u8 panel_type = panel->vbt.panel_type;

	panel->vbt.vrr = true; /* matches Windows behaviour */

	if (display->vbt.version < 228)
		return;

	power = bdb_find_section(display, BDB_LFP_POWER);
	if (!power)
		return;

	panel->vbt.psr.enable = panel_bool(power->psr, panel_type);

	/*
	 * If DRRS is not supported, drrs_type has to be set to 0.
	 * This is because, VBT is configured in such a way that
	 * static DRRS is 0 and DRRS not supported is represented by
	 * power->drrs & BIT(panel_type)=false
	 */
	if (!panel_bool(power->drrs, panel_type) && panel->vbt.drrs_type != DRRS_TYPE_NONE) {
		/*
		 * FIXME Should DMRRS perhaps be treated as seamless
		 * but without the automatic downclocking?
		 */
		if (panel_bool(power->dmrrs, panel_type))
			panel->vbt.drrs_type = DRRS_TYPE_STATIC;
		else
			panel->vbt.drrs_type = DRRS_TYPE_NONE;
	}

	if (display->vbt.version >= 232)
		panel->vbt.edp.hobl = panel_bool(power->hobl, panel_type);

	if (display->vbt.version >= 233)
		panel->vbt.vrr = panel_bool(power->vrr_feature_enabled,
					    panel_type);
}

static void vbt_edp_to_pps_delays(struct intel_pps_delays *pps,
				  const struct edp_power_seq *edp_pps)
{
	pps->power_up = edp_pps->t1_t3;
	pps->backlight_on = edp_pps->t8;
	pps->backlight_off = edp_pps->t9;
	pps->power_down = edp_pps->t10;
	pps->power_cycle = edp_pps->t11_t12;
}

static void
parse_edp(struct intel_display *display,
	  struct intel_panel *panel)
{
	const struct bdb_edp *edp;
	const struct edp_fast_link_params *edp_link_params;
	int panel_type = panel->vbt.panel_type;

	edp = bdb_find_section(display, BDB_EDP);
	if (!edp)
		return;

	switch (panel_bits(edp->color_depth, panel_type, 2)) {
	case EDP_18BPP:
		panel->vbt.edp.bpp = 18;
		break;
	case EDP_24BPP:
		panel->vbt.edp.bpp = 24;
		break;
	case EDP_30BPP:
		panel->vbt.edp.bpp = 30;
		break;
	}

	/* Get the eDP sequencing and link info */
	edp_link_params = &edp->fast_link_params[panel_type];

	vbt_edp_to_pps_delays(&panel->vbt.edp.pps,
			      &edp->power_seqs[panel_type]);

	if (display->vbt.version >= 224) {
		panel->vbt.edp.rate =
			edp->edp_fast_link_training_rate[panel_type] * 20;
	} else {
		switch (edp_link_params->rate) {
		case EDP_RATE_1_62:
			panel->vbt.edp.rate = 162000;
			break;
		case EDP_RATE_2_7:
			panel->vbt.edp.rate = 270000;
			break;
		case EDP_RATE_5_4:
			panel->vbt.edp.rate = 540000;
			break;
		default:
			drm_dbg_kms(display->drm,
				    "VBT has unknown eDP link rate value %u\n",
				    edp_link_params->rate);
			break;
		}
	}

	switch (edp_link_params->lanes) {
	case EDP_LANE_1:
		panel->vbt.edp.lanes = 1;
		break;
	case EDP_LANE_2:
		panel->vbt.edp.lanes = 2;
		break;
	case EDP_LANE_4:
		panel->vbt.edp.lanes = 4;
		break;
	default:
		drm_dbg_kms(display->drm,
			    "VBT has unknown eDP lane count value %u\n",
			    edp_link_params->lanes);
		break;
	}

	switch (edp_link_params->preemphasis) {
	case EDP_PREEMPHASIS_NONE:
		panel->vbt.edp.preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_0;
		break;
	case EDP_PREEMPHASIS_3_5dB:
		panel->vbt.edp.preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_1;
		break;
	case EDP_PREEMPHASIS_6dB:
		panel->vbt.edp.preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_2;
		break;
	case EDP_PREEMPHASIS_9_5dB:
		panel->vbt.edp.preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_3;
		break;
	default:
		drm_dbg_kms(display->drm,
			    "VBT has unknown eDP pre-emphasis value %u\n",
			    edp_link_params->preemphasis);
		break;
	}

	switch (edp_link_params->vswing) {
	case EDP_VSWING_0_4V:
		panel->vbt.edp.vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_0;
		break;
	case EDP_VSWING_0_6V:
		panel->vbt.edp.vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_1;
		break;
	case EDP_VSWING_0_8V:
		panel->vbt.edp.vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
		break;
	case EDP_VSWING_1_2V:
		panel->vbt.edp.vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
		break;
	default:
		drm_dbg_kms(display->drm,
			    "VBT has unknown eDP voltage swing value %u\n",
			    edp_link_params->vswing);
		break;
	}

	if (display->vbt.version >= 173) {
		u8 vswing;

		/* Don't read from VBT if module parameter has valid value*/
		if (display->params.edp_vswing) {
			panel->vbt.edp.low_vswing =
				display->params.edp_vswing == 1;
		} else {
			vswing = (edp->edp_vswing_preemph >> (panel_type * 4)) & 0xF;
			panel->vbt.edp.low_vswing = vswing == 0;
		}
	}

	panel->vbt.edp.drrs_msa_timing_delay =
		panel_bits(edp->sdrrs_msa_timing_delay, panel_type, 2);

	if (display->vbt.version >= 244)
		panel->vbt.edp.max_link_rate =
			edp->edp_max_port_link_rate[panel_type] * 20;

	if (display->vbt.version >= 251)
		panel->vbt.edp.dsc_disable =
			panel_bool(edp->edp_dsc_disable, panel_type);
}

static void
parse_psr(struct intel_display *display,
	  struct intel_panel *panel)
{
	const struct bdb_psr *psr;
	const struct psr_table *psr_table;
	int panel_type = panel->vbt.panel_type;

	psr = bdb_find_section(display, BDB_PSR);
	if (!psr) {
		drm_dbg_kms(display->drm, "No PSR BDB found.\n");
		return;
	}

	psr_table = &psr->psr_table[panel_type];

	panel->vbt.psr.full_link = psr_table->full_link;
	panel->vbt.psr.require_aux_wakeup = psr_table->require_aux_to_wakeup;

	/* Allowed VBT values goes from 0 to 15 */
	panel->vbt.psr.idle_frames = psr_table->idle_frames < 0 ? 0 :
		psr_table->idle_frames > 15 ? 15 : psr_table->idle_frames;

	/*
	 * New psr options 0=500us, 1=100us, 2=2500us, 3=0us
	 * Old decimal value is wake up time in multiples of 100 us.
	 */
	if (display->vbt.version >= 205 &&
	    (DISPLAY_VER(display) >= 9 && !display->platform.broxton)) {
		switch (psr_table->tp1_wakeup_time) {
		case 0:
			panel->vbt.psr.tp1_wakeup_time_us = 500;
			break;
		case 1:
			panel->vbt.psr.tp1_wakeup_time_us = 100;
			break;
		case 3:
			panel->vbt.psr.tp1_wakeup_time_us = 0;
			break;
		default:
			drm_dbg_kms(display->drm,
				    "VBT tp1 wakeup time value %d is outside range[0-3], defaulting to max value 2500us\n",
				    psr_table->tp1_wakeup_time);
			fallthrough;
		case 2:
			panel->vbt.psr.tp1_wakeup_time_us = 2500;
			break;
		}

		switch (psr_table->tp2_tp3_wakeup_time) {
		case 0:
			panel->vbt.psr.tp2_tp3_wakeup_time_us = 500;
			break;
		case 1:
			panel->vbt.psr.tp2_tp3_wakeup_time_us = 100;
			break;
		case 3:
			panel->vbt.psr.tp2_tp3_wakeup_time_us = 0;
			break;
		default:
			drm_dbg_kms(display->drm,
				    "VBT tp2_tp3 wakeup time value %d is outside range[0-3], defaulting to max value 2500us\n",
				    psr_table->tp2_tp3_wakeup_time);
			fallthrough;
		case 2:
			panel->vbt.psr.tp2_tp3_wakeup_time_us = 2500;
		break;
		}
	} else {
		panel->vbt.psr.tp1_wakeup_time_us = psr_table->tp1_wakeup_time * 100;
		panel->vbt.psr.tp2_tp3_wakeup_time_us = psr_table->tp2_tp3_wakeup_time * 100;
	}

	if (display->vbt.version >= 226) {
		u32 wakeup_time = psr->psr2_tp2_tp3_wakeup_time;

		wakeup_time = panel_bits(wakeup_time, panel_type, 2);
		switch (wakeup_time) {
		case 0:
			wakeup_time = 500;
			break;
		case 1:
			wakeup_time = 100;
			break;
		case 3:
			wakeup_time = 50;
			break;
		default:
		case 2:
			wakeup_time = 2500;
			break;
		}
		panel->vbt.psr.psr2_tp2_tp3_wakeup_time_us = wakeup_time;
	} else {
		/* Reusing PSR1 wakeup time for PSR2 in older VBTs */
		panel->vbt.psr.psr2_tp2_tp3_wakeup_time_us = panel->vbt.psr.tp2_tp3_wakeup_time_us;
	}
}

static void parse_dsi_backlight_ports(struct intel_display *display,
				      struct intel_panel *panel,
				      enum port port)
{
	enum port port_bc = DISPLAY_VER(display) >= 11 ? PORT_B : PORT_C;

	if (!panel->vbt.dsi.config->dual_link || display->vbt.version < 197) {
		panel->vbt.dsi.bl_ports = BIT(port);
		if (panel->vbt.dsi.config->cabc_supported)
			panel->vbt.dsi.cabc_ports = BIT(port);

		return;
	}

	switch (panel->vbt.dsi.config->dl_dcs_backlight_ports) {
	case DL_DCS_PORT_A:
		panel->vbt.dsi.bl_ports = BIT(PORT_A);
		break;
	case DL_DCS_PORT_C:
		panel->vbt.dsi.bl_ports = BIT(port_bc);
		break;
	default:
	case DL_DCS_PORT_A_AND_C:
		panel->vbt.dsi.bl_ports = BIT(PORT_A) | BIT(port_bc);
		break;
	}

	if (!panel->vbt.dsi.config->cabc_supported)
		return;

	switch (panel->vbt.dsi.config->dl_dcs_cabc_ports) {
	case DL_DCS_PORT_A:
		panel->vbt.dsi.cabc_ports = BIT(PORT_A);
		break;
	case DL_DCS_PORT_C:
		panel->vbt.dsi.cabc_ports = BIT(port_bc);
		break;
	default:
	case DL_DCS_PORT_A_AND_C:
		panel->vbt.dsi.cabc_ports =
					BIT(PORT_A) | BIT(port_bc);
		break;
	}
}

static void
parse_mipi_config(struct intel_display *display,
		  struct intel_panel *panel)
{
	const struct bdb_mipi_config *start;
	const struct mipi_config *config;
	const struct mipi_pps_data *pps;
	int panel_type = panel->vbt.panel_type;
	enum port port;

	/* parse MIPI blocks only if LFP type is MIPI */
	if (!intel_bios_is_dsi_present(display, &port))
		return;

	/* Initialize this to undefined indicating no generic MIPI support */
	panel->vbt.dsi.panel_id = MIPI_DSI_UNDEFINED_PANEL_ID;

	start = bdb_find_section(display, BDB_MIPI_CONFIG);
	if (!start) {
		drm_dbg_kms(display->drm, "No MIPI config BDB found");
		return;
	}

	drm_dbg_kms(display->drm, "Found MIPI Config block, panel index = %d\n",
		    panel_type);

	/*
	 * get hold of the correct configuration block and pps data as per
	 * the panel_type as index
	 */
	config = &start->config[panel_type];
	pps = &start->pps[panel_type];

	/* store as of now full data. Trim when we realise all is not needed */
	panel->vbt.dsi.config = kmemdup(config, sizeof(struct mipi_config), GFP_KERNEL);
	if (!panel->vbt.dsi.config)
		return;

	panel->vbt.dsi.pps = kmemdup(pps, sizeof(struct mipi_pps_data), GFP_KERNEL);
	if (!panel->vbt.dsi.pps) {
		kfree(panel->vbt.dsi.config);
		return;
	}

	parse_dsi_backlight_ports(display, panel, port);

	/* FIXME is the 90 vs. 270 correct? */
	switch (config->rotation) {
	case ENABLE_ROTATION_0:
		/*
		 * Most (all?) VBTs claim 0 degrees despite having
		 * an upside down panel, thus we do not trust this.
		 */
		panel->vbt.dsi.orientation =
			DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
		break;
	case ENABLE_ROTATION_90:
		panel->vbt.dsi.orientation =
			DRM_MODE_PANEL_ORIENTATION_RIGHT_UP;
		break;
	case ENABLE_ROTATION_180:
		panel->vbt.dsi.orientation =
			DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP;
		break;
	case ENABLE_ROTATION_270:
		panel->vbt.dsi.orientation =
			DRM_MODE_PANEL_ORIENTATION_LEFT_UP;
		break;
	}

	/* We have mandatory mipi config blocks. Initialize as generic panel */
	panel->vbt.dsi.panel_id = MIPI_DSI_GENERIC_PANEL_ID;
}

/* Find the sequence block and size for the given panel. */
static const u8 *
find_panel_sequence_block(struct intel_display *display,
			  const struct bdb_mipi_sequence *sequence,
			  u16 panel_id, u32 *seq_size)
{
	u32 total = get_blocksize(sequence);
	const u8 *data = &sequence->data[0];
	u8 current_id;
	u32 current_size;
	int header_size = sequence->version >= 3 ? 5 : 3;
	int index = 0;
	int i;

	/* skip new block size */
	if (sequence->version >= 3)
		data += 4;

	for (i = 0; i < MAX_MIPI_CONFIGURATIONS && index < total; i++) {
		if (index + header_size > total) {
			drm_err(display->drm,
				"Invalid sequence block (header)\n");
			return NULL;
		}

		current_id = *(data + index);
		if (sequence->version >= 3)
			current_size = *((const u32 *)(data + index + 1));
		else
			current_size = *((const u16 *)(data + index + 1));

		index += header_size;

		if (index + current_size > total) {
			drm_err(display->drm, "Invalid sequence block\n");
			return NULL;
		}

		if (current_id == panel_id) {
			*seq_size = current_size;
			return data + index;
		}

		index += current_size;
	}

	drm_err(display->drm,
		"Sequence block detected but no valid configuration\n");

	return NULL;
}

static int goto_next_sequence(struct intel_display *display,
			      const u8 *data, int index, int total)
{
	u16 len;

	/* Skip Sequence Byte. */
	for (index = index + 1; index < total; index += len) {
		u8 operation_byte = *(data + index);
		index++;

		switch (operation_byte) {
		case MIPI_SEQ_ELEM_END:
			return index;
		case MIPI_SEQ_ELEM_SEND_PKT:
			if (index + 4 > total)
				return 0;

			len = *((const u16 *)(data + index + 2)) + 4;
			break;
		case MIPI_SEQ_ELEM_DELAY:
			len = 4;
			break;
		case MIPI_SEQ_ELEM_GPIO:
			len = 2;
			break;
		case MIPI_SEQ_ELEM_I2C:
			if (index + 7 > total)
				return 0;
			len = *(data + index + 6) + 7;
			break;
		default:
			drm_err(display->drm, "Unknown operation byte\n");
			return 0;
		}
	}

	return 0;
}

static int goto_next_sequence_v3(struct intel_display *display,
				 const u8 *data, int index, int total)
{
	int seq_end;
	u16 len;
	u32 size_of_sequence;

	/*
	 * Could skip sequence based on Size of Sequence alone, but also do some
	 * checking on the structure.
	 */
	if (total < 5) {
		drm_err(display->drm, "Too small sequence size\n");
		return 0;
	}

	/* Skip Sequence Byte. */
	index++;

	/*
	 * Size of Sequence. Excludes the Sequence Byte and the size itself,
	 * includes MIPI_SEQ_ELEM_END byte, excludes the final MIPI_SEQ_END
	 * byte.
	 */
	size_of_sequence = *((const u32 *)(data + index));
	index += 4;

	seq_end = index + size_of_sequence;
	if (seq_end > total) {
		drm_err(display->drm, "Invalid sequence size\n");
		return 0;
	}

	for (; index < total; index += len) {
		u8 operation_byte = *(data + index);
		index++;

		if (operation_byte == MIPI_SEQ_ELEM_END) {
			if (index != seq_end) {
				drm_err(display->drm,
					"Invalid element structure\n");
				return 0;
			}
			return index;
		}

		len = *(data + index);
		index++;

		/*
		 * FIXME: Would be nice to check elements like for v1/v2 in
		 * goto_next_sequence() above.
		 */
		switch (operation_byte) {
		case MIPI_SEQ_ELEM_SEND_PKT:
		case MIPI_SEQ_ELEM_DELAY:
		case MIPI_SEQ_ELEM_GPIO:
		case MIPI_SEQ_ELEM_I2C:
		case MIPI_SEQ_ELEM_SPI:
		case MIPI_SEQ_ELEM_PMIC:
			break;
		default:
			drm_err(display->drm, "Unknown operation byte %u\n",
				operation_byte);
			break;
		}
	}

	return 0;
}

/*
 * Get len of pre-fixed deassert fragment from a v1 init OTP sequence,
 * skip all delay + gpio operands and stop at the first DSI packet op.
 */
static int get_init_otp_deassert_fragment_len(struct intel_display *display,
					      struct intel_panel *panel)
{
	const u8 *data = panel->vbt.dsi.sequence[MIPI_SEQ_INIT_OTP];
	int index, len;

	if (drm_WARN_ON(display->drm,
			!data || panel->vbt.dsi.seq_version != 1))
		return 0;

	/* index = 1 to skip sequence byte */
	for (index = 1; data[index] != MIPI_SEQ_ELEM_END; index += len) {
		switch (data[index]) {
		case MIPI_SEQ_ELEM_SEND_PKT:
			return index == 1 ? 0 : index;
		case MIPI_SEQ_ELEM_DELAY:
			len = 5; /* 1 byte for operand + uint32 */
			break;
		case MIPI_SEQ_ELEM_GPIO:
			len = 3; /* 1 byte for op, 1 for gpio_nr, 1 for value */
			break;
		default:
			return 0;
		}
	}

	return 0;
}

/*
 * Some v1 VBT MIPI sequences do the deassert in the init OTP sequence.
 * The deassert must be done before calling intel_dsi_device_ready, so for
 * these devices we split the init OTP sequence into a deassert sequence and
 * the actual init OTP part.
 */
static void vlv_fixup_mipi_sequences(struct intel_display *display,
				     struct intel_panel *panel)
{
	u8 *init_otp;
	int len;

	/* Limit this to v1 vid-mode sequences */
	if (panel->vbt.dsi.config->is_cmd_mode ||
	    panel->vbt.dsi.seq_version != 1)
		return;

	/* Only do this if there are otp and assert seqs and no deassert seq */
	if (!panel->vbt.dsi.sequence[MIPI_SEQ_INIT_OTP] ||
	    !panel->vbt.dsi.sequence[MIPI_SEQ_ASSERT_RESET] ||
	    panel->vbt.dsi.sequence[MIPI_SEQ_DEASSERT_RESET])
		return;

	/* The deassert-sequence ends at the first DSI packet */
	len = get_init_otp_deassert_fragment_len(display, panel);
	if (!len)
		return;

	drm_dbg_kms(display->drm,
		    "Using init OTP fragment to deassert reset\n");

	/* Copy the fragment, update seq byte and terminate it */
	init_otp = (u8 *)panel->vbt.dsi.sequence[MIPI_SEQ_INIT_OTP];
	panel->vbt.dsi.deassert_seq = kmemdup(init_otp, len + 1, GFP_KERNEL);
	if (!panel->vbt.dsi.deassert_seq)
		return;
	panel->vbt.dsi.deassert_seq[0] = MIPI_SEQ_DEASSERT_RESET;
	panel->vbt.dsi.deassert_seq[len] = MIPI_SEQ_ELEM_END;
	/* Use the copy for deassert */
	panel->vbt.dsi.sequence[MIPI_SEQ_DEASSERT_RESET] =
		panel->vbt.dsi.deassert_seq;
	/* Replace the last byte of the fragment with init OTP seq byte */
	init_otp[len - 1] = MIPI_SEQ_INIT_OTP;
	/* And make MIPI_MIPI_SEQ_INIT_OTP point to it */
	panel->vbt.dsi.sequence[MIPI_SEQ_INIT_OTP] = init_otp + len - 1;
}

/*
 * Some machines (eg. Lenovo 82TQ) appear to have broken
 * VBT sequences:
 * - INIT_OTP is not present at all
 * - what should be in INIT_OTP is in DISPLAY_ON
 * - what should be in DISPLAY_ON is in BACKLIGHT_ON
 *   (along with the actual backlight stuff)
 *
 * To make those work we simply swap DISPLAY_ON and INIT_OTP.
 *
 * TODO: Do we need to limit this to specific machines,
 *       or examine the contents of the sequences to
 *       avoid false positives?
 */
static void icl_fixup_mipi_sequences(struct intel_display *display,
				     struct intel_panel *panel)
{
	if (!panel->vbt.dsi.sequence[MIPI_SEQ_INIT_OTP] &&
	    panel->vbt.dsi.sequence[MIPI_SEQ_DISPLAY_ON]) {
		drm_dbg_kms(display->drm,
			    "Broken VBT: Swapping INIT_OTP and DISPLAY_ON sequences\n");

		swap(panel->vbt.dsi.sequence[MIPI_SEQ_INIT_OTP],
		     panel->vbt.dsi.sequence[MIPI_SEQ_DISPLAY_ON]);
	}
}

static void fixup_mipi_sequences(struct intel_display *display,
				 struct intel_panel *panel)
{
	if (DISPLAY_VER(display) >= 11)
		icl_fixup_mipi_sequences(display, panel);
	else if (display->platform.valleyview)
		vlv_fixup_mipi_sequences(display, panel);
}

static void
parse_mipi_sequence(struct intel_display *display,
		    struct intel_panel *panel)
{
	int panel_type = panel->vbt.panel_type;
	const struct bdb_mipi_sequence *sequence;
	const u8 *seq_data;
	u32 seq_size;
	u8 *data;
	int index = 0;

	/* Only our generic panel driver uses the sequence block. */
	if (panel->vbt.dsi.panel_id != MIPI_DSI_GENERIC_PANEL_ID)
		return;

	sequence = bdb_find_section(display, BDB_MIPI_SEQUENCE);
	if (!sequence) {
		drm_dbg_kms(display->drm,
			    "No MIPI Sequence found, parsing complete\n");
		return;
	}

	/* Fail gracefully for forward incompatible sequence block. */
	if (sequence->version >= 4) {
		drm_err(display->drm,
			"Unable to parse MIPI Sequence Block v%u\n",
			sequence->version);
		return;
	}

	drm_dbg_kms(display->drm, "Found MIPI sequence block v%u\n",
		    sequence->version);

	seq_data = find_panel_sequence_block(display, sequence, panel_type, &seq_size);
	if (!seq_data)
		return;

	data = kmemdup(seq_data, seq_size, GFP_KERNEL);
	if (!data)
		return;

	/* Parse the sequences, store pointers to each sequence. */
	for (;;) {
		u8 seq_id = *(data + index);
		if (seq_id == MIPI_SEQ_END)
			break;

		if (seq_id >= MIPI_SEQ_MAX) {
			drm_err(display->drm, "Unknown sequence %u\n",
				seq_id);
			goto err;
		}

		/* Log about presence of sequences we won't run. */
		if (seq_id == MIPI_SEQ_TEAR_ON || seq_id == MIPI_SEQ_TEAR_OFF)
			drm_dbg_kms(display->drm,
				    "Unsupported sequence %u\n", seq_id);

		panel->vbt.dsi.sequence[seq_id] = data + index;

		if (sequence->version >= 3)
			index = goto_next_sequence_v3(display, data, index, seq_size);
		else
			index = goto_next_sequence(display, data, index, seq_size);
		if (!index) {
			drm_err(display->drm, "Invalid sequence %u\n",
				seq_id);
			goto err;
		}
	}

	panel->vbt.dsi.data = data;
	panel->vbt.dsi.size = seq_size;
	panel->vbt.dsi.seq_version = sequence->version;

	fixup_mipi_sequences(display, panel);

	drm_dbg_kms(display->drm, "MIPI related VBT parsing complete\n");
	return;

err:
	kfree(data);
	memset(panel->vbt.dsi.sequence, 0, sizeof(panel->vbt.dsi.sequence));
}

static void
parse_compression_parameters(struct intel_display *display)
{
	const struct bdb_compression_parameters *params;
	struct intel_bios_encoder_data *devdata;
	u16 block_size;
	int index;

	if (display->vbt.version < 198)
		return;

	params = bdb_find_section(display, BDB_COMPRESSION_PARAMETERS);
	if (params) {
		/* Sanity checks */
		if (params->entry_size != sizeof(params->data[0])) {
			drm_dbg_kms(display->drm,
				    "VBT: unsupported compression param entry size\n");
			return;
		}

		block_size = get_blocksize(params);
		if (block_size < sizeof(*params)) {
			drm_dbg_kms(display->drm,
				    "VBT: expected 16 compression param entries\n");
			return;
		}
	}

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		const struct child_device_config *child = &devdata->child;

		if (!child->compression_enable)
			continue;

		if (!params) {
			drm_dbg_kms(display->drm,
				    "VBT: compression params not available\n");
			continue;
		}

		if (child->compression_method_cps) {
			drm_dbg_kms(display->drm,
				    "VBT: CPS compression not supported\n");
			continue;
		}

		index = child->compression_structure_index;

		devdata->dsc = kmemdup(&params->data[index],
				       sizeof(*devdata->dsc), GFP_KERNEL);
	}
}

static u8 translate_iboost(struct intel_display *display, u8 val)
{
	static const u8 mapping[] = { 1, 3, 7 }; /* See VBT spec */

	if (val >= ARRAY_SIZE(mapping)) {
		drm_dbg_kms(display->drm,
			    "Unsupported I_boost value found in VBT (%d), display may not work properly\n", val);
		return 0;
	}
	return mapping[val];
}

static const u8 cnp_ddc_pin_map[] = {
	[0] = 0, /* N/A */
	[GMBUS_PIN_1_BXT] = DDC_BUS_DDI_B,
	[GMBUS_PIN_2_BXT] = DDC_BUS_DDI_C,
	[GMBUS_PIN_4_CNP] = DDC_BUS_DDI_D, /* sic */
	[GMBUS_PIN_3_BXT] = DDC_BUS_DDI_F, /* sic */
};

static const u8 icp_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_2_BXT] = ICL_DDC_BUS_DDI_B,
	[GMBUS_PIN_3_BXT] = TGL_DDC_BUS_DDI_C,
	[GMBUS_PIN_9_TC1_ICP] = ICL_DDC_BUS_PORT_1,
	[GMBUS_PIN_10_TC2_ICP] = ICL_DDC_BUS_PORT_2,
	[GMBUS_PIN_11_TC3_ICP] = ICL_DDC_BUS_PORT_3,
	[GMBUS_PIN_12_TC4_ICP] = ICL_DDC_BUS_PORT_4,
	[GMBUS_PIN_13_TC5_TGP] = TGL_DDC_BUS_PORT_5,
	[GMBUS_PIN_14_TC6_TGP] = TGL_DDC_BUS_PORT_6,
};

static const u8 rkl_pch_tgp_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_2_BXT] = ICL_DDC_BUS_DDI_B,
	[GMBUS_PIN_9_TC1_ICP] = RKL_DDC_BUS_DDI_D,
	[GMBUS_PIN_10_TC2_ICP] = RKL_DDC_BUS_DDI_E,
};

static const u8 adls_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_9_TC1_ICP] = ADLS_DDC_BUS_PORT_TC1,
	[GMBUS_PIN_10_TC2_ICP] = ADLS_DDC_BUS_PORT_TC2,
	[GMBUS_PIN_11_TC3_ICP] = ADLS_DDC_BUS_PORT_TC3,
	[GMBUS_PIN_12_TC4_ICP] = ADLS_DDC_BUS_PORT_TC4,
};

static const u8 gen9bc_tgp_ddc_pin_map[] = {
	[GMBUS_PIN_2_BXT] = DDC_BUS_DDI_B,
	[GMBUS_PIN_9_TC1_ICP] = DDC_BUS_DDI_C,
	[GMBUS_PIN_10_TC2_ICP] = DDC_BUS_DDI_D,
};

static const u8 adlp_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_2_BXT] = ICL_DDC_BUS_DDI_B,
	[GMBUS_PIN_9_TC1_ICP] = ADLP_DDC_BUS_PORT_TC1,
	[GMBUS_PIN_10_TC2_ICP] = ADLP_DDC_BUS_PORT_TC2,
	[GMBUS_PIN_11_TC3_ICP] = ADLP_DDC_BUS_PORT_TC3,
	[GMBUS_PIN_12_TC4_ICP] = ADLP_DDC_BUS_PORT_TC4,
};

static u8 map_ddc_pin(struct intel_display *display, u8 vbt_pin)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	const u8 *ddc_pin_map;
	int i, n_entries;

	if (INTEL_PCH_TYPE(i915) >= PCH_MTL || display->platform.alderlake_p) {
		ddc_pin_map = adlp_ddc_pin_map;
		n_entries = ARRAY_SIZE(adlp_ddc_pin_map);
	} else if (display->platform.alderlake_s) {
		ddc_pin_map = adls_ddc_pin_map;
		n_entries = ARRAY_SIZE(adls_ddc_pin_map);
	} else if (INTEL_PCH_TYPE(i915) >= PCH_DG1) {
		return vbt_pin;
	} else if (display->platform.rocketlake && INTEL_PCH_TYPE(i915) == PCH_TGP) {
		ddc_pin_map = rkl_pch_tgp_ddc_pin_map;
		n_entries = ARRAY_SIZE(rkl_pch_tgp_ddc_pin_map);
	} else if (HAS_PCH_TGP(i915) && DISPLAY_VER(display) == 9) {
		ddc_pin_map = gen9bc_tgp_ddc_pin_map;
		n_entries = ARRAY_SIZE(gen9bc_tgp_ddc_pin_map);
	} else if (INTEL_PCH_TYPE(i915) >= PCH_ICP) {
		ddc_pin_map = icp_ddc_pin_map;
		n_entries = ARRAY_SIZE(icp_ddc_pin_map);
	} else if (HAS_PCH_CNP(i915)) {
		ddc_pin_map = cnp_ddc_pin_map;
		n_entries = ARRAY_SIZE(cnp_ddc_pin_map);
	} else {
		/* Assuming direct map */
		return vbt_pin;
	}

	for (i = 0; i < n_entries; i++) {
		if (ddc_pin_map[i] == vbt_pin)
			return i;
	}

	drm_dbg_kms(display->drm,
		    "Ignoring alternate pin: VBT claims DDC pin %d, which is not valid for this platform\n",
		    vbt_pin);
	return 0;
}

static u8 dvo_port_type(u8 dvo_port)
{
	switch (dvo_port) {
	case DVO_PORT_HDMIA:
	case DVO_PORT_HDMIB:
	case DVO_PORT_HDMIC:
	case DVO_PORT_HDMID:
	case DVO_PORT_HDMIE:
	case DVO_PORT_HDMIF:
	case DVO_PORT_HDMIG:
	case DVO_PORT_HDMIH:
	case DVO_PORT_HDMII:
		return DVO_PORT_HDMIA;
	case DVO_PORT_DPA:
	case DVO_PORT_DPB:
	case DVO_PORT_DPC:
	case DVO_PORT_DPD:
	case DVO_PORT_DPE:
	case DVO_PORT_DPF:
	case DVO_PORT_DPG:
	case DVO_PORT_DPH:
	case DVO_PORT_DPI:
		return DVO_PORT_DPA;
	case DVO_PORT_MIPIA:
	case DVO_PORT_MIPIB:
	case DVO_PORT_MIPIC:
	case DVO_PORT_MIPID:
		return DVO_PORT_MIPIA;
	default:
		return dvo_port;
	}
}

static enum port __dvo_port_to_port(int n_ports, int n_dvo,
				    const int port_mapping[][3], u8 dvo_port)
{
	enum port port;
	int i;

	for (port = PORT_A; port < n_ports; port++) {
		for (i = 0; i < n_dvo; i++) {
			if (port_mapping[port][i] == -1)
				break;

			if (dvo_port == port_mapping[port][i])
				return port;
		}
	}

	return PORT_NONE;
}

static enum port dvo_port_to_port(struct intel_display *display,
				  u8 dvo_port)
{
	/*
	 * Each DDI port can have more than one value on the "DVO Port" field,
	 * so look for all the possible values for each port.
	 */
	static const int port_mapping[][3] = {
		[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
		[PORT_B] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
		[PORT_C] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
		[PORT_D] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
		[PORT_E] = { DVO_PORT_HDMIE, DVO_PORT_DPE, DVO_PORT_CRT },
		[PORT_F] = { DVO_PORT_HDMIF, DVO_PORT_DPF, -1 },
		[PORT_G] = { DVO_PORT_HDMIG, DVO_PORT_DPG, -1 },
		[PORT_H] = { DVO_PORT_HDMIH, DVO_PORT_DPH, -1 },
		[PORT_I] = { DVO_PORT_HDMII, DVO_PORT_DPI, -1 },
	};
	/*
	 * RKL VBT uses PHY based mapping. Combo PHYs A,B,C,D
	 * map to DDI A,B,TC1,TC2 respectively.
	 */
	static const int rkl_port_mapping[][3] = {
		[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
		[PORT_B] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
		[PORT_C] = { -1 },
		[PORT_TC1] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
		[PORT_TC2] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
	};
	/*
	 * Alderlake S ports used in the driver are PORT_A, PORT_D, PORT_E,
	 * PORT_F and PORT_G, we need to map that to correct VBT sections.
	 */
	static const int adls_port_mapping[][3] = {
		[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
		[PORT_B] = { -1 },
		[PORT_C] = { -1 },
		[PORT_TC1] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
		[PORT_TC2] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
		[PORT_TC3] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
		[PORT_TC4] = { DVO_PORT_HDMIE, DVO_PORT_DPE, -1 },
	};
	static const int xelpd_port_mapping[][3] = {
		[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
		[PORT_B] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
		[PORT_C] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
		[PORT_D_XELPD] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
		[PORT_E_XELPD] = { DVO_PORT_HDMIE, DVO_PORT_DPE, -1 },
		[PORT_TC1] = { DVO_PORT_HDMIF, DVO_PORT_DPF, -1 },
		[PORT_TC2] = { DVO_PORT_HDMIG, DVO_PORT_DPG, -1 },
		[PORT_TC3] = { DVO_PORT_HDMIH, DVO_PORT_DPH, -1 },
		[PORT_TC4] = { DVO_PORT_HDMII, DVO_PORT_DPI, -1 },
	};

	if (DISPLAY_VER(display) >= 13)
		return __dvo_port_to_port(ARRAY_SIZE(xelpd_port_mapping),
					  ARRAY_SIZE(xelpd_port_mapping[0]),
					  xelpd_port_mapping,
					  dvo_port);
	else if (display->platform.alderlake_s)
		return __dvo_port_to_port(ARRAY_SIZE(adls_port_mapping),
					  ARRAY_SIZE(adls_port_mapping[0]),
					  adls_port_mapping,
					  dvo_port);
	else if (display->platform.dg1 || display->platform.rocketlake)
		return __dvo_port_to_port(ARRAY_SIZE(rkl_port_mapping),
					  ARRAY_SIZE(rkl_port_mapping[0]),
					  rkl_port_mapping,
					  dvo_port);
	else
		return __dvo_port_to_port(ARRAY_SIZE(port_mapping),
					  ARRAY_SIZE(port_mapping[0]),
					  port_mapping,
					  dvo_port);
}

static enum port
dsi_dvo_port_to_port(struct intel_display *display, u8 dvo_port)
{
	switch (dvo_port) {
	case DVO_PORT_MIPIA:
		return PORT_A;
	case DVO_PORT_MIPIC:
		if (DISPLAY_VER(display) >= 11)
			return PORT_B;
		else
			return PORT_C;
	default:
		return PORT_NONE;
	}
}

enum port intel_bios_encoder_port(const struct intel_bios_encoder_data *devdata)
{
	struct intel_display *display = devdata->display;
	const struct child_device_config *child = &devdata->child;
	enum port port;

	port = dvo_port_to_port(display, child->dvo_port);
	if (port == PORT_NONE && DISPLAY_VER(display) >= 11)
		port = dsi_dvo_port_to_port(display, child->dvo_port);

	return port;
}

static int parse_bdb_230_dp_max_link_rate(const int vbt_max_link_rate)
{
	switch (vbt_max_link_rate) {
	default:
	case BDB_230_VBT_DP_MAX_LINK_RATE_DEF:
		return 0;
	case BDB_230_VBT_DP_MAX_LINK_RATE_UHBR20:
		return 2000000;
	case BDB_230_VBT_DP_MAX_LINK_RATE_UHBR13P5:
		return 1350000;
	case BDB_230_VBT_DP_MAX_LINK_RATE_UHBR10:
		return 1000000;
	case BDB_230_VBT_DP_MAX_LINK_RATE_HBR3:
		return 810000;
	case BDB_230_VBT_DP_MAX_LINK_RATE_HBR2:
		return 540000;
	case BDB_230_VBT_DP_MAX_LINK_RATE_HBR:
		return 270000;
	case BDB_230_VBT_DP_MAX_LINK_RATE_LBR:
		return 162000;
	}
}

static int parse_bdb_216_dp_max_link_rate(const int vbt_max_link_rate)
{
	switch (vbt_max_link_rate) {
	default:
	case BDB_216_VBT_DP_MAX_LINK_RATE_HBR3:
		return 810000;
	case BDB_216_VBT_DP_MAX_LINK_RATE_HBR2:
		return 540000;
	case BDB_216_VBT_DP_MAX_LINK_RATE_HBR:
		return 270000;
	case BDB_216_VBT_DP_MAX_LINK_RATE_LBR:
		return 162000;
	}
}

int intel_bios_dp_max_link_rate(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || devdata->display->vbt.version < 216)
		return 0;

	if (devdata->display->vbt.version >= 230)
		return parse_bdb_230_dp_max_link_rate(devdata->child.dp_max_link_rate);
	else
		return parse_bdb_216_dp_max_link_rate(devdata->child.dp_max_link_rate);
}

int intel_bios_dp_max_lane_count(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || devdata->display->vbt.version < 244)
		return 0;

	return devdata->child.dp_max_lane_count + 1;
}

static void sanitize_device_type(struct intel_bios_encoder_data *devdata,
				 enum port port)
{
	struct intel_display *display = devdata->display;
	bool is_hdmi;

	if (port != PORT_A || DISPLAY_VER(display) >= 12)
		return;

	if (!intel_bios_encoder_supports_dvi(devdata))
		return;

	is_hdmi = intel_bios_encoder_supports_hdmi(devdata);

	drm_dbg_kms(display->drm, "VBT claims port A supports DVI%s, ignoring\n",
		    is_hdmi ? "/HDMI" : "");

	devdata->child.device_type &= ~DEVICE_TYPE_TMDS_DVI_SIGNALING;
	devdata->child.device_type |= DEVICE_TYPE_NOT_HDMI_OUTPUT;
}

static void sanitize_hdmi_level_shift(struct intel_bios_encoder_data *devdata,
				      enum port port)
{
	struct intel_display *display = devdata->display;

	if (!intel_bios_encoder_supports_dvi(devdata))
		return;

	/*
	 * Some BDW machines (eg. HP Pavilion 15-ab) shipped
	 * with a HSW VBT where the level shifter value goes
	 * up to 11, whereas the BDW max is 9.
	 */
	if (display->platform.broadwell && devdata->child.hdmi_level_shifter_value > 9) {
		drm_dbg_kms(display->drm,
			    "Bogus port %c VBT HDMI level shift %d, adjusting to %d\n",
			    port_name(port), devdata->child.hdmi_level_shifter_value, 9);

		devdata->child.hdmi_level_shifter_value = 9;
	}
}

static bool
intel_bios_encoder_supports_crt(const struct intel_bios_encoder_data *devdata)
{
	return devdata->child.device_type & DEVICE_TYPE_ANALOG_OUTPUT;
}

bool
intel_bios_encoder_supports_dvi(const struct intel_bios_encoder_data *devdata)
{
	return devdata->child.device_type & DEVICE_TYPE_TMDS_DVI_SIGNALING;
}

bool
intel_bios_encoder_supports_hdmi(const struct intel_bios_encoder_data *devdata)
{
	return intel_bios_encoder_supports_dvi(devdata) &&
		(devdata->child.device_type & DEVICE_TYPE_NOT_HDMI_OUTPUT) == 0;
}

bool
intel_bios_encoder_supports_dp(const struct intel_bios_encoder_data *devdata)
{
	return devdata->child.device_type & DEVICE_TYPE_DISPLAYPORT_OUTPUT;
}

bool
intel_bios_encoder_supports_edp(const struct intel_bios_encoder_data *devdata)
{
	return intel_bios_encoder_supports_dp(devdata) &&
		devdata->child.device_type & DEVICE_TYPE_INTERNAL_CONNECTOR;
}

bool
intel_bios_encoder_supports_dsi(const struct intel_bios_encoder_data *devdata)
{
	return devdata->child.device_type & DEVICE_TYPE_MIPI_OUTPUT;
}

bool
intel_bios_encoder_is_lspcon(const struct intel_bios_encoder_data *devdata)
{
	return devdata && HAS_LSPCON(devdata->display) && devdata->child.lspcon;
}

/* This is an index in the HDMI/DVI DDI buffer translation table, or -1 */
int intel_bios_hdmi_level_shift(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || devdata->display->vbt.version < 158 ||
	    DISPLAY_VER(devdata->display) >= 14)
		return -1;

	return devdata->child.hdmi_level_shifter_value;
}

int intel_bios_hdmi_max_tmds_clock(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || devdata->display->vbt.version < 204)
		return 0;

	switch (devdata->child.hdmi_max_data_rate) {
	default:
		MISSING_CASE(devdata->child.hdmi_max_data_rate);
		fallthrough;
	case HDMI_MAX_DATA_RATE_PLATFORM:
		return 0;
	case HDMI_MAX_DATA_RATE_594:
		return 594000;
	case HDMI_MAX_DATA_RATE_340:
		return 340000;
	case HDMI_MAX_DATA_RATE_300:
		return 300000;
	case HDMI_MAX_DATA_RATE_297:
		return 297000;
	case HDMI_MAX_DATA_RATE_165:
		return 165000;
	}
}

static bool is_port_valid(struct intel_display *display, enum port port)
{
	/*
	 * On some ICL SKUs port F is not present, but broken VBTs mark
	 * the port as present. Only try to initialize port F for the
	 * SKUs that may actually have it.
	 */
	if (port == PORT_F && display->platform.icelake)
		return display->platform.icelake_port_f;

	return true;
}

static void print_ddi_port(const struct intel_bios_encoder_data *devdata)
{
	struct intel_display *display = devdata->display;
	const struct child_device_config *child = &devdata->child;
	bool is_dvi, is_hdmi, is_dp, is_edp, is_dsi, is_crt, supports_typec_usb, supports_tbt;
	int dp_boost_level, dp_max_link_rate, hdmi_boost_level, hdmi_level_shift, max_tmds_clock;
	enum port port;

	port = intel_bios_encoder_port(devdata);
	if (port == PORT_NONE)
		return;

	is_dvi = intel_bios_encoder_supports_dvi(devdata);
	is_dp = intel_bios_encoder_supports_dp(devdata);
	is_crt = intel_bios_encoder_supports_crt(devdata);
	is_hdmi = intel_bios_encoder_supports_hdmi(devdata);
	is_edp = intel_bios_encoder_supports_edp(devdata);
	is_dsi = intel_bios_encoder_supports_dsi(devdata);

	supports_typec_usb = intel_bios_encoder_supports_typec_usb(devdata);
	supports_tbt = intel_bios_encoder_supports_tbt(devdata);

	drm_dbg_kms(display->drm,
		    "Port %c VBT info: CRT:%d DVI:%d HDMI:%d DP:%d eDP:%d DSI:%d DP++:%d LSPCON:%d USB-Type-C:%d TBT:%d DSC:%d\n",
		    port_name(port), is_crt, is_dvi, is_hdmi, is_dp, is_edp, is_dsi,
		    intel_bios_encoder_supports_dp_dual_mode(devdata),
		    intel_bios_encoder_is_lspcon(devdata),
		    supports_typec_usb, supports_tbt,
		    devdata->dsc != NULL);

	hdmi_level_shift = intel_bios_hdmi_level_shift(devdata);
	if (hdmi_level_shift >= 0) {
		drm_dbg_kms(display->drm,
			    "Port %c VBT HDMI level shift: %d\n",
			    port_name(port), hdmi_level_shift);
	}

	max_tmds_clock = intel_bios_hdmi_max_tmds_clock(devdata);
	if (max_tmds_clock)
		drm_dbg_kms(display->drm,
			    "Port %c VBT HDMI max TMDS clock: %d kHz\n",
			    port_name(port), max_tmds_clock);

	/* I_boost config for SKL and above */
	dp_boost_level = intel_bios_dp_boost_level(devdata);
	if (dp_boost_level)
		drm_dbg_kms(display->drm,
			    "Port %c VBT (e)DP boost level: %d\n",
			    port_name(port), dp_boost_level);

	hdmi_boost_level = intel_bios_hdmi_boost_level(devdata);
	if (hdmi_boost_level)
		drm_dbg_kms(display->drm,
			    "Port %c VBT HDMI boost level: %d\n",
			    port_name(port), hdmi_boost_level);

	dp_max_link_rate = intel_bios_dp_max_link_rate(devdata);
	if (dp_max_link_rate)
		drm_dbg_kms(display->drm,
			    "Port %c VBT DP max link rate: %d\n",
			    port_name(port), dp_max_link_rate);

	/*
	 * FIXME need to implement support for VBT
	 * vswing/preemph tables should this ever trigger.
	 */
	drm_WARN(display->drm, child->use_vbt_vswing,
		 "Port %c asks to use VBT vswing/preemph tables\n",
		 port_name(port));
}

static void parse_ddi_port(struct intel_bios_encoder_data *devdata)
{
	struct intel_display *display = devdata->display;
	enum port port;

	port = intel_bios_encoder_port(devdata);
	if (port == PORT_NONE)
		return;

	if (!is_port_valid(display, port)) {
		drm_dbg_kms(display->drm,
			    "VBT reports port %c as supported, but that can't be true: skipping\n",
			    port_name(port));
		return;
	}

	sanitize_device_type(devdata, port);
	sanitize_hdmi_level_shift(devdata, port);
}

static bool has_ddi_port_info(struct intel_display *display)
{
	return DISPLAY_VER(display) >= 5 || display->platform.g4x;
}

static void parse_ddi_ports(struct intel_display *display)
{
	struct intel_bios_encoder_data *devdata;

	if (!has_ddi_port_info(display))
		return;

	list_for_each_entry(devdata, &display->vbt.display_devices, node)
		parse_ddi_port(devdata);

	list_for_each_entry(devdata, &display->vbt.display_devices, node)
		print_ddi_port(devdata);
}

static int child_device_expected_size(u16 version)
{
	BUILD_BUG_ON(sizeof(struct child_device_config) < 40);

	if (version > 256)
		return -ENOENT;
	else if (version >= 256)
		return 40;
	else if (version >= 216)
		return 39;
	else if (version >= 196)
		return 38;
	else if (version >= 195)
		return 37;
	else if (version >= 111)
		return LEGACY_CHILD_DEVICE_CONFIG_SIZE;
	else if (version >= 106)
		return 27;
	else
		return 22;
}

static bool child_device_size_valid(struct intel_display *display, int size)
{
	int expected_size;

	expected_size = child_device_expected_size(display->vbt.version);
	if (expected_size < 0) {
		expected_size = sizeof(struct child_device_config);
		drm_dbg_kms(display->drm,
			    "Expected child device config size for VBT version %u not known; assuming %d\n",
			    display->vbt.version, expected_size);
	}

	/* Flag an error for unexpected size, but continue anyway. */
	if (size != expected_size)
		drm_err(display->drm,
			"Unexpected child device config size %d (expected %d for VBT version %u)\n",
			size, expected_size, display->vbt.version);

	/* The legacy sized child device config is the minimum we need. */
	if (size < LEGACY_CHILD_DEVICE_CONFIG_SIZE) {
		drm_dbg_kms(display->drm,
			    "Child device config size %d is too small.\n",
			    size);
		return false;
	}

	return true;
}

static void
parse_general_definitions(struct intel_display *display)
{
	const struct bdb_general_definitions *defs;
	struct intel_bios_encoder_data *devdata;
	const struct child_device_config *child;
	int i, child_device_num;
	u16 block_size;
	int bus_pin;

	defs = bdb_find_section(display, BDB_GENERAL_DEFINITIONS);
	if (!defs) {
		drm_dbg_kms(display->drm,
			    "No general definition block is found, no devices defined.\n");
		return;
	}

	block_size = get_blocksize(defs);
	if (block_size < sizeof(*defs)) {
		drm_dbg_kms(display->drm,
			    "General definitions block too small (%u)\n",
			    block_size);
		return;
	}

	bus_pin = defs->crt_ddc_gmbus_pin;
	drm_dbg_kms(display->drm, "crt_ddc_bus_pin: %d\n", bus_pin);
	if (intel_gmbus_is_valid_pin(display, bus_pin))
		display->vbt.crt_ddc_pin = bus_pin;

	if (!child_device_size_valid(display, defs->child_dev_size))
		return;

	/* get the number of child device */
	child_device_num = (block_size - sizeof(*defs)) / defs->child_dev_size;

	for (i = 0; i < child_device_num; i++) {
		child = child_device_ptr(defs, i);
		if (!child->device_type)
			continue;

		drm_dbg_kms(display->drm,
			    "Found VBT child device with type 0x%x\n",
			    child->device_type);

		devdata = kzalloc(sizeof(*devdata), GFP_KERNEL);
		if (!devdata)
			break;

		devdata->display = display;

		/*
		 * Copy as much as we know (sizeof) and is available
		 * (child_dev_size) of the child device config. Accessing the
		 * data must depend on VBT version.
		 */
		memcpy(&devdata->child, child,
		       min_t(size_t, defs->child_dev_size, sizeof(*child)));

		list_add_tail(&devdata->node, &display->vbt.display_devices);
	}

	if (list_empty(&display->vbt.display_devices))
		drm_dbg_kms(display->drm,
			    "no child dev is parsed from VBT\n");
}

/* Common defaults which may be overridden by VBT. */
static void
init_vbt_defaults(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	display->vbt.crt_ddc_pin = GMBUS_PIN_VGADDC;

	/* general features */
	display->vbt.int_tv_support = 1;
	display->vbt.int_crt_support = 1;

	/* driver features */
	display->vbt.int_lvds_support = 1;

	/* Default to using SSC */
	display->vbt.lvds_use_ssc = 1;
	/*
	 * Core/SandyBridge/IvyBridge use alternative (120MHz) reference
	 * clock for LVDS.
	 */
	display->vbt.lvds_ssc_freq = intel_bios_ssc_frequency(display,
							      !HAS_PCH_SPLIT(i915));
	drm_dbg_kms(display->drm, "Set default to SSC at %d kHz\n",
		    display->vbt.lvds_ssc_freq);
}

/* Common defaults which may be overridden by VBT. */
static void
init_vbt_panel_defaults(struct intel_panel *panel)
{
	/* Default to having backlight */
	panel->vbt.backlight.present = true;

	/* LFP panel data */
	panel->vbt.lvds_dither = true;
}

/* Defaults to initialize only if there is no VBT. */
static void
init_vbt_missing_defaults(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	unsigned int ports = DISPLAY_RUNTIME_INFO(display)->port_mask;
	enum port port;

	if (!HAS_DDI(display) && !display->platform.cherryview)
		return;

	for_each_port_masked(port, ports) {
		struct intel_bios_encoder_data *devdata;
		struct child_device_config *child;
		enum phy phy = intel_port_to_phy(i915, port);

		/*
		 * VBT has the TypeC mode (native,TBT/USB) and we don't want
		 * to detect it.
		 */
		if (intel_phy_is_tc(i915, phy))
			continue;

		/* Create fake child device config */
		devdata = kzalloc(sizeof(*devdata), GFP_KERNEL);
		if (!devdata)
			break;

		devdata->display = display;
		child = &devdata->child;

		if (port == PORT_F)
			child->dvo_port = DVO_PORT_HDMIF;
		else if (port == PORT_E)
			child->dvo_port = DVO_PORT_HDMIE;
		else
			child->dvo_port = DVO_PORT_HDMIA + port;

		if (port != PORT_A && port != PORT_E)
			child->device_type |= DEVICE_TYPE_TMDS_DVI_SIGNALING;

		if (port != PORT_E)
			child->device_type |= DEVICE_TYPE_DISPLAYPORT_OUTPUT;

		if (port == PORT_A)
			child->device_type |= DEVICE_TYPE_INTERNAL_CONNECTOR;

		list_add_tail(&devdata->node, &display->vbt.display_devices);

		drm_dbg_kms(display->drm,
			    "Generating default VBT child device with type 0x%04x on port %c\n",
			    child->device_type, port_name(port));
	}

	/* Bypass some minimum baseline VBT version checks */
	display->vbt.version = 155;
}

static const struct bdb_header *get_bdb_header(const struct vbt_header *vbt)
{
	const void *_vbt = vbt;

	return _vbt + vbt->bdb_offset;
}

static const char vbt_signature[] = "$VBT";
static const int vbt_signature_len = 4;

/**
 * intel_bios_is_valid_vbt - does the given buffer contain a valid VBT
 * @display:	display device
 * @buf:	pointer to a buffer to validate
 * @size:	size of the buffer
 *
 * Returns true on valid VBT.
 */
bool intel_bios_is_valid_vbt(struct intel_display *display,
			     const void *buf, size_t size)
{
	const struct vbt_header *vbt = buf;
	const struct bdb_header *bdb;

	if (!vbt)
		return false;

	if (sizeof(struct vbt_header) > size) {
		drm_dbg_kms(display->drm, "VBT header incomplete\n");
		return false;
	}

	if (memcmp(vbt->signature, vbt_signature, vbt_signature_len)) {
		drm_dbg_kms(display->drm, "VBT invalid signature\n");
		return false;
	}

	if (vbt->vbt_size > size) {
		drm_dbg_kms(display->drm,
			    "VBT incomplete (vbt_size overflows)\n");
		return false;
	}

	size = vbt->vbt_size;

	if (range_overflows_t(size_t,
			      vbt->bdb_offset,
			      sizeof(struct bdb_header),
			      size)) {
		drm_dbg_kms(display->drm, "BDB header incomplete\n");
		return false;
	}

	bdb = get_bdb_header(vbt);
	if (range_overflows_t(size_t, vbt->bdb_offset, bdb->bdb_size, size)) {
		drm_dbg_kms(display->drm, "BDB incomplete\n");
		return false;
	}

	return vbt;
}

static struct vbt_header *firmware_get_vbt(struct intel_display *display,
					   size_t *size)
{
	struct vbt_header *vbt = NULL;
	const struct firmware *fw = NULL;
	const char *name = display->params.vbt_firmware;
	int ret;

	if (!name || !*name)
		return NULL;

	ret = request_firmware(&fw, name, display->drm->dev);
	if (ret) {
		drm_err(display->drm,
			"Requesting VBT firmware \"%s\" failed (%d)\n",
			name, ret);
		return NULL;
	}

	if (intel_bios_is_valid_vbt(display, fw->data, fw->size)) {
		vbt = kmemdup(fw->data, fw->size, GFP_KERNEL);
		if (vbt) {
			drm_dbg_kms(display->drm,
				    "Found valid VBT firmware \"%s\"\n", name);
			if (size)
				*size = fw->size;
		}
	} else {
		drm_dbg_kms(display->drm, "Invalid VBT firmware \"%s\"\n",
			    name);
	}

	release_firmware(fw);

	return vbt;
}

static struct vbt_header *oprom_get_vbt(struct intel_display *display,
					struct intel_rom *rom,
					size_t *size, const char *type)
{
	struct vbt_header *vbt;
	size_t vbt_size;
	loff_t offset;

	if (!rom)
		return NULL;

	BUILD_BUG_ON(vbt_signature_len != sizeof(vbt_signature) - 1);
	BUILD_BUG_ON(vbt_signature_len != sizeof(u32));

	offset = intel_rom_find(rom, *(const u32 *)vbt_signature);
	if (offset < 0)
		goto err_free_rom;

	if (sizeof(struct vbt_header) > intel_rom_size(rom) - offset) {
		drm_dbg_kms(display->drm, "VBT header incomplete\n");
		goto err_free_rom;
	}

	BUILD_BUG_ON(sizeof(vbt->vbt_size) != sizeof(u16));

	vbt_size = intel_rom_read16(rom, offset + offsetof(struct vbt_header, vbt_size));
	if (vbt_size > intel_rom_size(rom) - offset) {
		drm_dbg_kms(display->drm, "VBT incomplete (vbt_size overflows)\n");
		goto err_free_rom;
	}

	vbt = kzalloc(round_up(vbt_size, 4), GFP_KERNEL);
	if (!vbt)
		goto err_free_rom;

	intel_rom_read_block(rom, vbt, offset, vbt_size);

	if (!intel_bios_is_valid_vbt(display, vbt, vbt_size))
		goto err_free_vbt;

	drm_dbg_kms(display->drm, "Found valid VBT in %s\n", type);

	if (size)
		*size = vbt_size;

	intel_rom_free(rom);

	return vbt;

err_free_vbt:
	kfree(vbt);
err_free_rom:
	intel_rom_free(rom);
	return NULL;
}

static const struct vbt_header *intel_bios_get_vbt(struct intel_display *display,
						   size_t *sizep)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	const struct vbt_header *vbt = NULL;
	intel_wakeref_t wakeref;

	vbt = firmware_get_vbt(display, sizep);

	if (!vbt)
		vbt = intel_opregion_get_vbt(display, sizep);

	/*
	 * If the OpRegion does not have VBT, look in SPI flash
	 * through MMIO or PCI mapping
	 */
	if (!vbt && IS_DGFX(i915))
		with_intel_runtime_pm(&i915->runtime_pm, wakeref)
			vbt = oprom_get_vbt(display, intel_rom_spi(i915), sizep, "SPI flash");

	if (!vbt)
		with_intel_runtime_pm(&i915->runtime_pm, wakeref)
			vbt = oprom_get_vbt(display, intel_rom_pci(i915), sizep, "PCI ROM");

	return vbt;
}

/**
 * intel_bios_init - find VBT and initialize settings from the BIOS
 * @display: display device instance
 *
 * Parse and initialize settings from the Video BIOS Tables (VBT). If the VBT
 * was not found in ACPI OpRegion, try to find it in PCI ROM first. Also
 * initialize some defaults if the VBT is not present at all.
 */
void intel_bios_init(struct intel_display *display)
{
	const struct vbt_header *vbt;
	const struct bdb_header *bdb;

	INIT_LIST_HEAD(&display->vbt.display_devices);
	INIT_LIST_HEAD(&display->vbt.bdb_blocks);

	if (!HAS_DISPLAY(display)) {
		drm_dbg_kms(display->drm,
			    "Skipping VBT init due to disabled display.\n");
		return;
	}

	init_vbt_defaults(display);

	vbt = intel_bios_get_vbt(display, NULL);

	if (!vbt)
		goto out;

	bdb = get_bdb_header(vbt);
	display->vbt.version = bdb->version;

	drm_dbg_kms(display->drm,
		    "VBT signature \"%.*s\", BDB version %d\n",
		    (int)sizeof(vbt->signature), vbt->signature,
		    display->vbt.version);

	init_bdb_blocks(display, bdb);

	/* Grab useful general definitions */
	parse_general_features(display);
	parse_general_definitions(display);
	parse_driver_features(display);

	/* Depends on child device list */
	parse_compression_parameters(display);

out:
	if (!vbt) {
		drm_info(display->drm,
			 "Failed to find VBIOS tables (VBT)\n");
		init_vbt_missing_defaults(display);
	}

	/* Further processing on pre-parsed or generated child device data */
	parse_sdvo_device_mapping(display);
	parse_ddi_ports(display);

	kfree(vbt);
}

static void intel_bios_init_panel(struct intel_display *display,
				  struct intel_panel *panel,
				  const struct intel_bios_encoder_data *devdata,
				  const struct drm_edid *drm_edid,
				  bool use_fallback)
{
	/* already have it? */
	if (panel->vbt.panel_type >= 0) {
		drm_WARN_ON(display->drm, !use_fallback);
		return;
	}

	panel->vbt.panel_type = get_panel_type(display, devdata,
					       drm_edid, use_fallback);
	if (panel->vbt.panel_type < 0) {
		drm_WARN_ON(display->drm, use_fallback);
		return;
	}

	init_vbt_panel_defaults(panel);

	parse_panel_options(display, panel);
	parse_generic_dtd(display, panel);
	parse_lfp_data(display, panel);
	parse_lfp_backlight(display, panel);
	parse_sdvo_lvds_data(display, panel);
	parse_panel_driver_features(display, panel);
	parse_power_conservation_features(display, panel);
	parse_edp(display, panel);
	parse_psr(display, panel);
	parse_mipi_config(display, panel);
	parse_mipi_sequence(display, panel);
}

void intel_bios_init_panel_early(struct intel_display *display,
				 struct intel_panel *panel,
				 const struct intel_bios_encoder_data *devdata)
{
	intel_bios_init_panel(display, panel, devdata, NULL, false);
}

void intel_bios_init_panel_late(struct intel_display *display,
				struct intel_panel *panel,
				const struct intel_bios_encoder_data *devdata,
				const struct drm_edid *drm_edid)
{
	intel_bios_init_panel(display, panel, devdata, drm_edid, true);
}

/**
 * intel_bios_driver_remove - Free any resources allocated by intel_bios_init()
 * @display: display device instance
 */
void intel_bios_driver_remove(struct intel_display *display)
{
	struct intel_bios_encoder_data *devdata, *nd;
	struct bdb_block_entry *entry, *ne;

	list_for_each_entry_safe(devdata, nd, &display->vbt.display_devices,
				 node) {
		list_del(&devdata->node);
		kfree(devdata->dsc);
		kfree(devdata);
	}

	list_for_each_entry_safe(entry, ne, &display->vbt.bdb_blocks, node) {
		list_del(&entry->node);
		kfree(entry);
	}
}

void intel_bios_fini_panel(struct intel_panel *panel)
{
	kfree(panel->vbt.sdvo_lvds_vbt_mode);
	panel->vbt.sdvo_lvds_vbt_mode = NULL;
	kfree(panel->vbt.lfp_vbt_mode);
	panel->vbt.lfp_vbt_mode = NULL;
	kfree(panel->vbt.dsi.data);
	panel->vbt.dsi.data = NULL;
	kfree(panel->vbt.dsi.pps);
	panel->vbt.dsi.pps = NULL;
	kfree(panel->vbt.dsi.config);
	panel->vbt.dsi.config = NULL;
	kfree(panel->vbt.dsi.deassert_seq);
	panel->vbt.dsi.deassert_seq = NULL;
}

/**
 * intel_bios_is_tv_present - is integrated TV present in VBT
 * @display: display device instance
 *
 * Return true if TV is present. If no child devices were parsed from VBT,
 * assume TV is present.
 */
bool intel_bios_is_tv_present(struct intel_display *display)
{
	const struct intel_bios_encoder_data *devdata;

	if (!display->vbt.int_tv_support)
		return false;

	if (list_empty(&display->vbt.display_devices))
		return true;

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		const struct child_device_config *child = &devdata->child;

		/*
		 * If the device type is not TV, continue.
		 */
		switch (child->device_type) {
		case DEVICE_TYPE_INT_TV:
		case DEVICE_TYPE_TV:
		case DEVICE_TYPE_TV_SVIDEO_COMPOSITE:
			break;
		default:
			continue;
		}
		/* Only when the addin_offset is non-zero, it is regarded
		 * as present.
		 */
		if (child->addin_offset)
			return true;
	}

	return false;
}

/**
 * intel_bios_is_lvds_present - is LVDS present in VBT
 * @display: display device instance
 * @i2c_pin:	i2c pin for LVDS if present
 *
 * Return true if LVDS is present. If no child devices were parsed from VBT,
 * assume LVDS is present.
 */
bool intel_bios_is_lvds_present(struct intel_display *display, u8 *i2c_pin)
{
	const struct intel_bios_encoder_data *devdata;

	if (list_empty(&display->vbt.display_devices))
		return true;

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		const struct child_device_config *child = &devdata->child;

		/* If the device type is not LFP, continue.
		 * We have to check both the new identifiers as well as the
		 * old for compatibility with some BIOSes.
		 */
		if (child->device_type != DEVICE_TYPE_INT_LFP &&
		    child->device_type != DEVICE_TYPE_LFP)
			continue;

		if (intel_gmbus_is_valid_pin(display, child->i2c_pin))
			*i2c_pin = child->i2c_pin;

		/* However, we cannot trust the BIOS writers to populate
		 * the VBT correctly.  Since LVDS requires additional
		 * information from AIM blocks, a non-zero addin offset is
		 * a good indicator that the LVDS is actually present.
		 */
		if (child->addin_offset)
			return true;

		/* But even then some BIOS writers perform some black magic
		 * and instantiate the device without reference to any
		 * additional data.  Trust that if the VBT was written into
		 * the OpRegion then they have validated the LVDS's existence.
		 */
		return intel_opregion_vbt_present(display);
	}

	return false;
}

/**
 * intel_bios_is_port_present - is the specified digital port present
 * @display: display device instance
 * @port:	port to check
 *
 * Return true if the device in %port is present.
 */
bool intel_bios_is_port_present(struct intel_display *display, enum port port)
{
	const struct intel_bios_encoder_data *devdata;

	if (WARN_ON(!has_ddi_port_info(display)))
		return true;

	if (!is_port_valid(display, port))
		return false;

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		const struct child_device_config *child = &devdata->child;

		if (dvo_port_to_port(display, child->dvo_port) == port)
			return true;
	}

	return false;
}

bool intel_bios_encoder_supports_dp_dual_mode(const struct intel_bios_encoder_data *devdata)
{
	const struct child_device_config *child = &devdata->child;

	if (!devdata)
		return false;

	if (!intel_bios_encoder_supports_dp(devdata) ||
	    !intel_bios_encoder_supports_hdmi(devdata))
		return false;

	if (dvo_port_type(child->dvo_port) == DVO_PORT_DPA)
		return true;

	/* Only accept a HDMI dvo_port as DP++ if it has an AUX channel */
	if (dvo_port_type(child->dvo_port) == DVO_PORT_HDMIA &&
	    child->aux_channel != 0)
		return true;

	return false;
}

/**
 * intel_bios_is_dsi_present - is DSI present in VBT
 * @display: display device instance
 * @port:	port for DSI if present
 *
 * Return true if DSI is present, and return the port in %port.
 */
bool intel_bios_is_dsi_present(struct intel_display *display,
			       enum port *port)
{
	const struct intel_bios_encoder_data *devdata;

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		const struct child_device_config *child = &devdata->child;
		u8 dvo_port = child->dvo_port;

		if (!(child->device_type & DEVICE_TYPE_MIPI_OUTPUT))
			continue;

		if (dsi_dvo_port_to_port(display, dvo_port) == PORT_NONE) {
			drm_dbg_kms(display->drm,
				    "VBT has unsupported DSI port %c\n",
				    port_name(dvo_port - DVO_PORT_MIPIA));
			continue;
		}

		if (port)
			*port = dsi_dvo_port_to_port(display, dvo_port);
		return true;
	}

	return false;
}

static void fill_dsc(struct intel_crtc_state *crtc_state,
		     struct dsc_compression_parameters_entry *dsc,
		     int dsc_max_bpc)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	int bpc = 8;

	vdsc_cfg->dsc_version_major = dsc->version_major;
	vdsc_cfg->dsc_version_minor = dsc->version_minor;

	if (dsc->support_12bpc && dsc_max_bpc >= 12)
		bpc = 12;
	else if (dsc->support_10bpc && dsc_max_bpc >= 10)
		bpc = 10;
	else if (dsc->support_8bpc && dsc_max_bpc >= 8)
		bpc = 8;
	else
		drm_dbg_kms(display->drm, "VBT: Unsupported BPC %d for DCS\n",
			    dsc_max_bpc);

	crtc_state->pipe_bpp = bpc * 3;

	crtc_state->dsc.compressed_bpp_x16 = fxp_q4_from_int(min(crtc_state->pipe_bpp,
								 VBT_DSC_MAX_BPP(dsc->max_bpp)));

	/*
	 * FIXME: This is ugly, and slice count should take DSC engine
	 * throughput etc. into account.
	 *
	 * Also, per spec DSI supports 1, 2, 3 or 4 horizontal slices.
	 */
	if (dsc->slices_per_line & BIT(2)) {
		crtc_state->dsc.slice_count = 4;
	} else if (dsc->slices_per_line & BIT(1)) {
		crtc_state->dsc.slice_count = 2;
	} else {
		/* FIXME */
		if (!(dsc->slices_per_line & BIT(0)))
			drm_dbg_kms(display->drm,
				    "VBT: Unsupported DSC slice count for DSI\n");

		crtc_state->dsc.slice_count = 1;
	}

	if (crtc_state->hw.adjusted_mode.crtc_hdisplay %
	    crtc_state->dsc.slice_count != 0)
		drm_dbg_kms(display->drm,
			    "VBT: DSC hdisplay %d not divisible by slice count %d\n",
			    crtc_state->hw.adjusted_mode.crtc_hdisplay,
			    crtc_state->dsc.slice_count);

	/*
	 * The VBT rc_buffer_block_size and rc_buffer_size definitions
	 * correspond to DP 1.4 DPCD offsets 0x62 and 0x63.
	 */
	vdsc_cfg->rc_model_size = drm_dsc_dp_rc_buffer_size(dsc->rc_buffer_block_size,
							    dsc->rc_buffer_size);

	/* FIXME: DSI spec says bpc + 1 for this one */
	vdsc_cfg->line_buf_depth = VBT_DSC_LINE_BUFFER_DEPTH(dsc->line_buffer_depth);

	vdsc_cfg->block_pred_enable = dsc->block_prediction_enable;

	vdsc_cfg->slice_height = dsc->slice_height;
}

/* FIXME: initially DSI specific */
bool intel_bios_get_dsc_params(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state,
			       int dsc_max_bpc)
{
	struct intel_display *display = to_intel_display(encoder);
	const struct intel_bios_encoder_data *devdata;

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		const struct child_device_config *child = &devdata->child;

		if (!(child->device_type & DEVICE_TYPE_MIPI_OUTPUT))
			continue;

		if (dsi_dvo_port_to_port(display, child->dvo_port) == encoder->port) {
			if (!devdata->dsc)
				return false;

			fill_dsc(crtc_state, devdata->dsc, dsc_max_bpc);

			return true;
		}
	}

	return false;
}

static const u8 adlp_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_B] = DP_AUX_B,
	[AUX_CH_C] = DP_AUX_C,
	[AUX_CH_D_XELPD] = DP_AUX_D,
	[AUX_CH_E_XELPD] = DP_AUX_E,
	[AUX_CH_USBC1] = DP_AUX_F,
	[AUX_CH_USBC2] = DP_AUX_G,
	[AUX_CH_USBC3] = DP_AUX_H,
	[AUX_CH_USBC4] = DP_AUX_I,
};

/*
 * ADL-S VBT uses PHY based mapping. Combo PHYs A,B,C,D,E
 * map to DDI A,TC1,TC2,TC3,TC4 respectively.
 */
static const u8 adls_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_USBC1] = DP_AUX_B,
	[AUX_CH_USBC2] = DP_AUX_C,
	[AUX_CH_USBC3] = DP_AUX_D,
	[AUX_CH_USBC4] = DP_AUX_E,
};

/*
 * RKL/DG1 VBT uses PHY based mapping. Combo PHYs A,B,C,D
 * map to DDI A,B,TC1,TC2 respectively.
 */
static const u8 rkl_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_B] = DP_AUX_B,
	[AUX_CH_USBC1] = DP_AUX_C,
	[AUX_CH_USBC2] = DP_AUX_D,
};

static const u8 direct_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_B] = DP_AUX_B,
	[AUX_CH_C] = DP_AUX_C,
	[AUX_CH_D] = DP_AUX_D, /* aka AUX_CH_USBC1 */
	[AUX_CH_E] = DP_AUX_E, /* aka AUX_CH_USBC2 */
	[AUX_CH_F] = DP_AUX_F, /* aka AUX_CH_USBC3 */
	[AUX_CH_G] = DP_AUX_G, /* aka AUX_CH_USBC4 */
	[AUX_CH_H] = DP_AUX_H, /* aka AUX_CH_USBC5 */
	[AUX_CH_I] = DP_AUX_I, /* aka AUX_CH_USBC6 */
};

static enum aux_ch map_aux_ch(struct intel_display *display, u8 aux_channel)
{
	const u8 *aux_ch_map;
	int i, n_entries;

	if (DISPLAY_VER(display) >= 13) {
		aux_ch_map = adlp_aux_ch_map;
		n_entries = ARRAY_SIZE(adlp_aux_ch_map);
	} else if (display->platform.alderlake_s) {
		aux_ch_map = adls_aux_ch_map;
		n_entries = ARRAY_SIZE(adls_aux_ch_map);
	} else if (display->platform.dg1 || display->platform.rocketlake) {
		aux_ch_map = rkl_aux_ch_map;
		n_entries = ARRAY_SIZE(rkl_aux_ch_map);
	} else {
		aux_ch_map = direct_aux_ch_map;
		n_entries = ARRAY_SIZE(direct_aux_ch_map);
	}

	for (i = 0; i < n_entries; i++) {
		if (aux_ch_map[i] == aux_channel)
			return i;
	}

	drm_dbg_kms(display->drm,
		    "Ignoring alternate AUX CH: VBT claims AUX 0x%x, which is not valid for this platform\n",
		    aux_channel);

	return AUX_CH_NONE;
}

enum aux_ch intel_bios_dp_aux_ch(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || !devdata->child.aux_channel)
		return AUX_CH_NONE;

	return map_aux_ch(devdata->display, devdata->child.aux_channel);
}

bool intel_bios_dp_has_shared_aux_ch(const struct intel_bios_encoder_data *devdata)
{
	struct intel_display *display;
	u8 aux_channel;
	int count = 0;

	if (!devdata || !devdata->child.aux_channel)
		return false;

	display = devdata->display;
	aux_channel = devdata->child.aux_channel;

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		if (intel_bios_encoder_supports_dp(devdata) &&
		    aux_channel == devdata->child.aux_channel)
			count++;
	}

	return count > 1;
}

int intel_bios_dp_boost_level(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || devdata->display->vbt.version < 196 || !devdata->child.iboost)
		return 0;

	return translate_iboost(devdata->display, devdata->child.dp_iboost_level);
}

int intel_bios_hdmi_boost_level(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || devdata->display->vbt.version < 196 || !devdata->child.iboost)
		return 0;

	return translate_iboost(devdata->display, devdata->child.hdmi_iboost_level);
}

int intel_bios_hdmi_ddc_pin(const struct intel_bios_encoder_data *devdata)
{
	if (!devdata || !devdata->child.ddc_pin)
		return 0;

	return map_ddc_pin(devdata->display, devdata->child.ddc_pin);
}

bool intel_bios_encoder_supports_typec_usb(const struct intel_bios_encoder_data *devdata)
{
	return devdata->display->vbt.version >= 195 && devdata->child.dp_usb_type_c;
}

bool intel_bios_encoder_supports_tbt(const struct intel_bios_encoder_data *devdata)
{
	return devdata->display->vbt.version >= 209 && devdata->child.tbt;
}

bool intel_bios_encoder_lane_reversal(const struct intel_bios_encoder_data *devdata)
{
	return devdata && devdata->child.lane_reversal;
}

bool intel_bios_encoder_hpd_invert(const struct intel_bios_encoder_data *devdata)
{
	return devdata && devdata->child.hpd_invert;
}

const struct intel_bios_encoder_data *
intel_bios_encoder_data_lookup(struct intel_display *display, enum port port)
{
	struct intel_bios_encoder_data *devdata;

	list_for_each_entry(devdata, &display->vbt.display_devices, node) {
		if (intel_bios_encoder_port(devdata) == port)
			return devdata;
	}

	return NULL;
}

void intel_bios_for_each_encoder(struct intel_display *display,
				 void (*func)(struct intel_display *display,
					      const struct intel_bios_encoder_data *devdata))
{
	struct intel_bios_encoder_data *devdata;

	list_for_each_entry(devdata, &display->vbt.display_devices, node)
		func(display, devdata);
}

static int intel_bios_vbt_show(struct seq_file *m, void *unused)
{
	struct intel_display *display = m->private;
	const void *vbt;
	size_t vbt_size;

	vbt = intel_bios_get_vbt(display, &vbt_size);

	if (vbt) {
		seq_write(m, vbt, vbt_size);
		kfree(vbt);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(intel_bios_vbt);

void intel_bios_debugfs_register(struct intel_display *display)
{
	struct drm_minor *minor = display->drm->primary;

	debugfs_create_file("i915_vbt", 0444, minor->debugfs_root,
			    display, &intel_bios_vbt_fops);
}
