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

#ifndef _I830_BIOS_H_
#define _I830_BIOS_H_

#include "drmP.h"

struct vbt_header {
	u8 signature[20];		/**< Always starts with 'VBT$' */
	u16 version;			/**< decimal */
	u16 header_size;		/**< in bytes */
	u16 vbt_size;			/**< in bytes */
	u8 vbt_checksum;
	u8 reserved0;
	u32 bdb_offset;			/**< from beginning of VBT */
	u32 aim_offset[4];		/**< from beginning of VBT */
} __attribute__((packed));

struct bdb_header {
	u8 signature[16];		/**< Always 'BIOS_DATA_BLOCK' */
	u16 version;			/**< decimal */
	u16 header_size;		/**< in bytes */
	u16 bdb_size;			/**< in bytes */
};

/* strictly speaking, this is a "skip" block, but it has interesting info */
struct vbios_data {
	u8 type; /* 0 == desktop, 1 == mobile */
	u8 relstage;
	u8 chipset;
	u8 lvds_present:1;
	u8 tv_present:1;
	u8 rsvd2:6; /* finish byte */
	u8 rsvd3[4];
	u8 signon[155];
	u8 copyright[61];
	u16 code_segment;
	u8 dos_boot_mode;
	u8 bandwidth_percent;
	u8 rsvd4; /* popup memory size */
	u8 resize_pci_bios;
	u8 rsvd5; /* is crt already on ddc2 */
} __attribute__((packed));

/*
 * There are several types of BIOS data blocks (BDBs), each block has
 * an ID and size in the first 3 bytes (ID in first, size in next 2).
 * Known types are listed below.
 */
#define BDB_GENERAL_FEATURES	  1
#define BDB_GENERAL_DEFINITIONS	  2
#define BDB_OLD_TOGGLE_LIST	  3
#define BDB_MODE_SUPPORT_LIST	  4
#define BDB_GENERIC_MODE_TABLE	  5
#define BDB_EXT_MMIO_REGS	  6
#define BDB_SWF_IO		  7
#define BDB_SWF_MMIO		  8
#define BDB_DOT_CLOCK_TABLE	  9
#define BDB_MODE_REMOVAL_TABLE	 10
#define BDB_CHILD_DEVICE_TABLE	 11
#define BDB_DRIVER_FEATURES	 12
#define BDB_DRIVER_PERSISTENCE	 13
#define BDB_EXT_TABLE_PTRS	 14
#define BDB_DOT_CLOCK_OVERRIDE	 15
#define BDB_DISPLAY_SELECT	 16
/* 17 rsvd */
#define BDB_DRIVER_ROTATION	 18
#define BDB_DISPLAY_REMOVE	 19
#define BDB_OEM_CUSTOM		 20
#define BDB_EFP_LIST		 21 /* workarounds for VGA hsync/vsync */
#define BDB_SDVO_LVDS_OPTIONS	 22
#define BDB_SDVO_PANEL_DTDS	 23
#define BDB_SDVO_LVDS_PNP_IDS	 24
#define BDB_SDVO_LVDS_POWER_SEQ	 25
#define BDB_TV_OPTIONS		 26
#define BDB_LVDS_OPTIONS	 40
#define BDB_LVDS_LFP_DATA_PTRS	 41
#define BDB_LVDS_LFP_DATA	 42
#define BDB_LVDS_BACKLIGHT	 43
#define BDB_LVDS_POWER		 44
#define BDB_SKIP		254 /* VBIOS private block, ignore */

struct bdb_general_features {
        /* bits 1 */
	u8 panel_fitting:2;
	u8 flexaim:1;
	u8 msg_enable:1;
	u8 clear_screen:3;
	u8 color_flip:1;

        /* bits 2 */
	u8 download_ext_vbt:1;
	u8 enable_ssc:1;
	u8 ssc_freq:1;
	u8 enable_lfp_on_override:1;
	u8 disable_ssc_ddt:1;
	u8 rsvd8:3; /* finish byte */

        /* bits 3 */
	u8 disable_smooth_vision:1;
	u8 single_dvi:1;
	u8 rsvd9:6; /* finish byte */

        /* bits 4 */
	u8 legacy_monitor_detect;

        /* bits 5 */
	u8 int_crt_support:1;
	u8 int_tv_support:1;
	u8 rsvd11:6; /* finish byte */
} __attribute__((packed));

struct bdb_general_definitions {
	/* DDC GPIO */
	u8 crt_ddc_gmbus_pin;

	/* DPMS bits */
	u8 dpms_acpi:1;
	u8 skip_boot_crt_detect:1;
	u8 dpms_aim:1;
	u8 rsvd1:5; /* finish byte */

	/* boot device bits */
	u8 boot_display[2];
	u8 child_dev_size;

	/* device info */
	u8 tv_or_lvds_info[33];
	u8 dev1[33];
	u8 dev2[33];
	u8 dev3[33];
	u8 dev4[33];
	/* may be another device block here on some platforms */
};

struct bdb_lvds_options {
	u8 panel_type;
	u8 rsvd1;
	/* LVDS capabilities, stored in a dword */
	u8 pfit_mode:2;
	u8 pfit_text_mode_enhanced:1;
	u8 pfit_gfx_mode_enhanced:1;
	u8 pfit_ratio_auto:1;
	u8 pixel_dither:1;
	u8 lvds_edid:1;
	u8 rsvd2:1;
	u8 rsvd4;
} __attribute__((packed));

/* LFP pointer table contains entries to the struct below */
struct bdb_lvds_lfp_data_ptr {
	u16 fp_timing_offset; /* offsets are from start of bdb */
	u8 fp_table_size;
	u16 dvo_timing_offset;
	u8 dvo_table_size;
	u16 panel_pnp_id_offset;
	u8 pnp_table_size;
} __attribute__((packed));

struct bdb_lvds_lfp_data_ptrs {
	u8 lvds_entries; /* followed by one or more lvds_data_ptr structs */
	struct bdb_lvds_lfp_data_ptr ptr[16];
} __attribute__((packed));

/* LFP data has 3 blocks per entry */
struct lvds_fp_timing {
	u16 x_res;
	u16 y_res;
	u32 lvds_reg;
	u32 lvds_reg_val;
	u32 pp_on_reg;
	u32 pp_on_reg_val;
	u32 pp_off_reg;
	u32 pp_off_reg_val;
	u32 pp_cycle_reg;
	u32 pp_cycle_reg_val;
	u32 pfit_reg;
	u32 pfit_reg_val;
	u16 terminator;
} __attribute__((packed));

struct lvds_dvo_timing {
	u16 clock;		/**< In 10khz */
	u8 hactive_lo;
	u8 hblank_lo;
	u8 hblank_hi:4;
	u8 hactive_hi:4;
	u8 vactive_lo;
	u8 vblank_lo;
	u8 vblank_hi:4;
	u8 vactive_hi:4;
	u8 hsync_off_lo;
	u8 hsync_pulse_width;
	u8 vsync_pulse_width:4;
	u8 vsync_off:4;
	u8 rsvd0:6;
	u8 hsync_off_hi:2;
	u8 h_image;
	u8 v_image;
	u8 max_hv;
	u8 h_border;
	u8 v_border;
	u8 rsvd1:3;
	u8 digital:2;
	u8 vsync_positive:1;
	u8 hsync_positive:1;
	u8 rsvd2:1;
} __attribute__((packed));

struct lvds_pnp_id {
	u16 mfg_name;
	u16 product_code;
	u32 serial;
	u8 mfg_week;
	u8 mfg_year;
} __attribute__((packed));

struct bdb_lvds_lfp_data_entry {
	struct lvds_fp_timing fp_timing;
	struct lvds_dvo_timing dvo_timing;
	struct lvds_pnp_id pnp_id;
} __attribute__((packed));

struct bdb_lvds_lfp_data {
	struct bdb_lvds_lfp_data_entry data[16];
} __attribute__((packed));

struct aimdb_header {
	char signature[16];
	char oem_device[20];
	u16 aimdb_version;
	u16 aimdb_header_size;
	u16 aimdb_size;
} __attribute__((packed));

struct aimdb_block {
	u8 aimdb_id;
	u16 aimdb_size;
} __attribute__((packed));

struct vch_panel_data {
	u16 fp_timing_offset;
	u8 fp_timing_size;
	u16 dvo_timing_offset;
	u8 dvo_timing_size;
	u16 text_fitting_offset;
	u8 text_fitting_size;
	u16 graphics_fitting_offset;
	u8 graphics_fitting_size;
} __attribute__((packed));

struct vch_bdb_22 {
	struct aimdb_block aimdb_block;
	struct vch_panel_data panels[16];
} __attribute__((packed));

bool intel_init_bios(struct drm_device *dev);

/*
 * Driver<->VBIOS interaction occurs through scratch bits in
 * GR18 & SWF*.
 */

/* GR18 bits are set on display switch and hotkey events */
#define GR18_DRIVER_SWITCH_EN	(1<<7) /* 0: VBIOS control, 1: driver control */
#define GR18_HOTKEY_MASK	0x78 /* See also SWF4 15:0 */
#define   GR18_HK_NONE		(0x0<<3)
#define   GR18_HK_LFP_STRETCH	(0x1<<3)
#define   GR18_HK_TOGGLE_DISP	(0x2<<3)
#define   GR18_HK_DISP_SWITCH	(0x4<<3) /* see SWF14 15:0 for what to enable */
#define   GR18_HK_POPUP_DISABLED (0x6<<3)
#define   GR18_HK_POPUP_ENABLED	(0x7<<3)
#define   GR18_HK_PFIT		(0x8<<3)
#define   GR18_HK_APM_CHANGE	(0xa<<3)
#define   GR18_HK_MULTIPLE	(0xc<<3)
#define GR18_USER_INT_EN	(1<<2)
#define GR18_A0000_FLUSH_EN	(1<<1)
#define GR18_SMM_EN		(1<<0)

/* Set by driver, cleared by VBIOS */
#define SWF00_YRES_SHIFT	16
#define SWF00_XRES_SHIFT	0
#define SWF00_RES_MASK		0xffff

/* Set by VBIOS at boot time and driver at runtime */
#define SWF01_TV2_FORMAT_SHIFT	8
#define SWF01_TV1_FORMAT_SHIFT	0
#define SWF01_TV_FORMAT_MASK	0xffff

#define SWF10_VBIOS_BLC_I2C_EN	(1<<29)
#define SWF10_GTT_OVERRIDE_EN	(1<<28)
#define SWF10_LFP_DPMS_OVR	(1<<27) /* override DPMS on display switch */
#define SWF10_ACTIVE_TOGGLE_LIST_MASK (7<<24)
#define   SWF10_OLD_TOGGLE	0x0
#define   SWF10_TOGGLE_LIST_1	0x1
#define   SWF10_TOGGLE_LIST_2	0x2
#define   SWF10_TOGGLE_LIST_3	0x3
#define   SWF10_TOGGLE_LIST_4	0x4
#define SWF10_PANNING_EN	(1<<23)
#define SWF10_DRIVER_LOADED	(1<<22)
#define SWF10_EXTENDED_DESKTOP	(1<<21)
#define SWF10_EXCLUSIVE_MODE	(1<<20)
#define SWF10_OVERLAY_EN	(1<<19)
#define SWF10_PLANEB_HOLDOFF	(1<<18)
#define SWF10_PLANEA_HOLDOFF	(1<<17)
#define SWF10_VGA_HOLDOFF	(1<<16)
#define SWF10_ACTIVE_DISP_MASK	0xffff
#define   SWF10_PIPEB_LFP2	(1<<15)
#define   SWF10_PIPEB_EFP2	(1<<14)
#define   SWF10_PIPEB_TV2	(1<<13)
#define   SWF10_PIPEB_CRT2	(1<<12)
#define   SWF10_PIPEB_LFP	(1<<11)
#define   SWF10_PIPEB_EFP	(1<<10)
#define   SWF10_PIPEB_TV	(1<<9)
#define   SWF10_PIPEB_CRT	(1<<8)
#define   SWF10_PIPEA_LFP2	(1<<7)
#define   SWF10_PIPEA_EFP2	(1<<6)
#define   SWF10_PIPEA_TV2	(1<<5)
#define   SWF10_PIPEA_CRT2	(1<<4)
#define   SWF10_PIPEA_LFP	(1<<3)
#define   SWF10_PIPEA_EFP	(1<<2)
#define   SWF10_PIPEA_TV	(1<<1)
#define   SWF10_PIPEA_CRT	(1<<0)

#define SWF11_MEMORY_SIZE_SHIFT	16
#define SWF11_SV_TEST_EN	(1<<15)
#define SWF11_IS_AGP		(1<<14)
#define SWF11_DISPLAY_HOLDOFF	(1<<13)
#define SWF11_DPMS_REDUCED	(1<<12)
#define SWF11_IS_VBE_MODE	(1<<11)
#define SWF11_PIPEB_ACCESS	(1<<10) /* 0 here means pipe a */
#define SWF11_DPMS_MASK		0x07
#define   SWF11_DPMS_OFF	(1<<2)
#define   SWF11_DPMS_SUSPEND	(1<<1)
#define   SWF11_DPMS_STANDBY	(1<<0)
#define   SWF11_DPMS_ON		0

#define SWF14_GFX_PFIT_EN	(1<<31)
#define SWF14_TEXT_PFIT_EN	(1<<30)
#define SWF14_LID_STATUS_CLOSED	(1<<29) /* 0 here means open */
#define SWF14_POPUP_EN		(1<<28)
#define SWF14_DISPLAY_HOLDOFF	(1<<27)
#define SWF14_DISP_DETECT_EN	(1<<26)
#define SWF14_DOCKING_STATUS_DOCKED (1<<25) /* 0 here means undocked */
#define SWF14_DRIVER_STATUS	(1<<24)
#define SWF14_OS_TYPE_WIN9X	(1<<23)
#define SWF14_OS_TYPE_WINNT	(1<<22)
/* 21:19 rsvd */
#define SWF14_PM_TYPE_MASK	0x00070000
#define   SWF14_PM_ACPI_VIDEO	(0x4 << 16)
#define   SWF14_PM_ACPI		(0x3 << 16)
#define   SWF14_PM_APM_12	(0x2 << 16)
#define   SWF14_PM_APM_11	(0x1 << 16)
#define SWF14_HK_REQUEST_MASK	0x0000ffff /* see GR18 6:3 for event type */
          /* if GR18 indicates a display switch */
#define   SWF14_DS_PIPEB_LFP2_EN (1<<15)
#define   SWF14_DS_PIPEB_EFP2_EN (1<<14)
#define   SWF14_DS_PIPEB_TV2_EN  (1<<13)
#define   SWF14_DS_PIPEB_CRT2_EN (1<<12)
#define   SWF14_DS_PIPEB_LFP_EN  (1<<11)
#define   SWF14_DS_PIPEB_EFP_EN  (1<<10)
#define   SWF14_DS_PIPEB_TV_EN   (1<<9)
#define   SWF14_DS_PIPEB_CRT_EN  (1<<8)
#define   SWF14_DS_PIPEA_LFP2_EN (1<<7)
#define   SWF14_DS_PIPEA_EFP2_EN (1<<6)
#define   SWF14_DS_PIPEA_TV2_EN  (1<<5)
#define   SWF14_DS_PIPEA_CRT2_EN (1<<4)
#define   SWF14_DS_PIPEA_LFP_EN  (1<<3)
#define   SWF14_DS_PIPEA_EFP_EN  (1<<2)
#define   SWF14_DS_PIPEA_TV_EN   (1<<1)
#define   SWF14_DS_PIPEA_CRT_EN  (1<<0)
          /* if GR18 indicates a panel fitting request */
#define   SWF14_PFIT_EN		(1<<0) /* 0 means disable */
          /* if GR18 indicates an APM change request */
#define   SWF14_APM_HIBERNATE	0x4
#define   SWF14_APM_SUSPEND	0x3
#define   SWF14_APM_STANDBY	0x1
#define   SWF14_APM_RESTORE	0x0

#endif /* _I830_BIOS_H_ */
