/*
 * Copyright 2007-2008 Nouveau Project
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NOUVEAU_DISPBIOS_H__
#define __NOUVEAU_DISPBIOS_H__

#include "nvreg.h"

#define DCB_MAX_NUM_ENTRIES 16
#define DCB_MAX_NUM_I2C_ENTRIES 16
#define DCB_MAX_NUM_GPIO_ENTRIES 32
#define DCB_MAX_NUM_CONNECTOR_ENTRIES 16

#define DCB_LOC_ON_CHIP 0

#define ROM16(x) le16_to_cpu(*(u16 *)&(x))
#define ROM32(x) le32_to_cpu(*(u32 *)&(x))
#define ROM48(x) ({ u8 *p = &(x); (u64)ROM16(p[4]) << 32 | ROM32(p[0]); })
#define ROM64(x) le64_to_cpu(*(u64 *)&(x))
#define ROMPTR(d,x) ({            \
	struct nouveau_drm *drm = nouveau_drm((d)); \
	ROM16(x) ? &drm->vbios.data[ROM16(x)] : NULL; \
})

struct bit_entry {
	uint8_t  id;
	uint8_t  version;
	uint16_t length;
	uint16_t offset;
	uint8_t *data;
};

int bit_table(struct drm_device *, u8 id, struct bit_entry *);

#include <subdev/bios/dcb.h>
#include <subdev/bios/conn.h>

struct dcb_table {
	uint8_t version;
	int entries;
	struct dcb_output entry[DCB_MAX_NUM_ENTRIES];
};

enum nouveau_or {
	DCB_OUTPUT_A = (1 << 0),
	DCB_OUTPUT_B = (1 << 1),
	DCB_OUTPUT_C = (1 << 2)
};

enum LVDS_script {
	/* Order *does* matter here */
	LVDS_INIT = 1,
	LVDS_RESET,
	LVDS_BACKLIGHT_ON,
	LVDS_BACKLIGHT_OFF,
	LVDS_PANEL_ON,
	LVDS_PANEL_OFF
};

struct nvbios {
	struct drm_device *dev;
	enum {
		NVBIOS_BMP,
		NVBIOS_BIT
	} type;
	uint16_t offset;
	uint32_t length;
	uint8_t *data;

	uint8_t chip_version;

	uint32_t dactestval;
	uint32_t tvdactestval;
	uint8_t digital_min_front_porch;
	bool fp_no_ddc;

	spinlock_t lock;

	bool execute;

	uint8_t major_version;
	uint8_t feature_byte;
	bool is_mobile;

	uint32_t fmaxvco, fminvco;

	bool old_style_init;
	uint16_t init_script_tbls_ptr;
	uint16_t extra_init_script_tbl_ptr;
	uint16_t macro_index_tbl_ptr;
	uint16_t macro_tbl_ptr;
	uint16_t condition_tbl_ptr;
	uint16_t io_condition_tbl_ptr;
	uint16_t io_flag_condition_tbl_ptr;
	uint16_t init_function_tbl_ptr;

	uint16_t pll_limit_tbl_ptr;
	uint16_t ram_restrict_tbl_ptr;
	uint8_t ram_restrict_group_count;

	uint16_t some_script_ptr; /* BIT I + 14 */
	uint16_t init96_tbl_ptr; /* BIT I + 16 */

	struct dcb_table dcb;

	struct {
		int crtchead;
	} state;

	struct {
		uint16_t fptablepointer;	/* also used by tmds */
		uint16_t fpxlatetableptr;
		int xlatwidth;
		uint16_t lvdsmanufacturerpointer;
		uint16_t fpxlatemanufacturertableptr;
		uint16_t mode_ptr;
		uint16_t xlated_entry;
		bool power_off_for_reset;
		bool reset_after_pclk_change;
		bool dual_link;
		bool link_c_increment;
		bool if_is_24bit;
		int duallink_transition_clk;
		uint8_t strapless_is_24bit;
		uint8_t *edid;

		/* will need resetting after suspend */
		int last_script_invoc;
		bool lvds_init_run;
	} fp;

	struct {
		uint16_t output0_script_ptr;
		uint16_t output1_script_ptr;
	} tmds;

	struct {
		uint16_t mem_init_tbl_ptr;
		uint16_t sdr_seq_tbl_ptr;
		uint16_t ddr_seq_tbl_ptr;

		struct {
			uint8_t crt, tv, panel;
		} i2c_indices;

		uint16_t lvds_single_a_script_ptr;
	} legacy;
};

void *olddcb_table(struct drm_device *);
void *olddcb_outp(struct drm_device *, u8 idx);
int olddcb_outp_foreach(struct drm_device *, void *data,
		     int (*)(struct drm_device *, void *, int idx, u8 *outp));
u8 *olddcb_conntab(struct drm_device *);
u8 *olddcb_conn(struct drm_device *, u8 idx);

int nouveau_bios_init(struct drm_device *);
void nouveau_bios_takedown(struct drm_device *dev);
int nouveau_run_vbios_init(struct drm_device *);
struct dcb_connector_table_entry *
nouveau_bios_connector_entry(struct drm_device *, int index);
bool nouveau_bios_fp_mode(struct drm_device *, struct drm_display_mode *);
uint8_t *nouveau_bios_embedded_edid(struct drm_device *);
int nouveau_bios_parse_lvds_table(struct drm_device *, int pxclk,
					 bool *dl, bool *if_is_24bit);
int run_tmds_table(struct drm_device *, struct dcb_output *,
			  int head, int pxclk);
int call_lvds_script(struct drm_device *, struct dcb_output *, int head,
			    enum LVDS_script, int pxclk);

#endif
