/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2006 Intel Corporation
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 */

#ifndef _INTEL_BIOS_H_
#define _INTEL_BIOS_H_

struct drm_device;

struct vbt_header {
	u8 signature[20];		/**< Always starts with 'VBT$' */
	u16 version;			/**< decimal */
	u16 header_size;		/**< in bytes */
	u16 vbt_size;			/**< in bytes */
	u8 vbt_checksum;
	u8 reserved0;
	u32 bdb_offset;			/**< from beginning of VBT */
	u32 aim_offset[4];		/**< from beginning of VBT */
} __packed;


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
} __packed;

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
#define BDB_EDP			 27
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
	u8 int_efp_support:1;
	u8 dp_ssc_enb:1;	/* PCH attached eDP supports SSC */
	u8 dp_ssc_freq:1;	/* SSC freq for PCH attached eDP */
	u8 rsvd11:3; /* finish byte */
} __packed;

/* pre-915 */
#define GPIO_PIN_DVI_LVDS	0x03 /* "DVI/LVDS DDC GPIO pins" */
#define GPIO_PIN_ADD_I2C	0x05 /* "ADDCARD I2C GPIO pins" */
#define GPIO_PIN_ADD_DDC	0x04 /* "ADDCARD DDC GPIO pins" */
#define GPIO_PIN_ADD_DDC_I2C	0x06 /* "ADDCARD DDC/I2C GPIO pins" */

/* Pre 915 */
#define DEVICE_TYPE_NONE	0x00
#define DEVICE_TYPE_CRT		0x01
#define DEVICE_TYPE_TV		0x09
#define DEVICE_TYPE_EFP		0x12
#define DEVICE_TYPE_LFP		0x22
/* On 915+ */
#define DEVICE_TYPE_CRT_DPMS		0x6001
#define DEVICE_TYPE_CRT_DPMS_HOTPLUG	0x4001
#define DEVICE_TYPE_TV_COMPOSITE	0x0209
#define DEVICE_TYPE_TV_MACROVISION	0x0289
#define DEVICE_TYPE_TV_RF_COMPOSITE	0x020c
#define DEVICE_TYPE_TV_SVIDEO_COMPOSITE	0x0609
#define DEVICE_TYPE_TV_SCART		0x0209
#define DEVICE_TYPE_TV_CODEC_HOTPLUG_PWR 0x6009
#define DEVICE_TYPE_EFP_HOTPLUG_PWR	0x6012
#define DEVICE_TYPE_EFP_DVI_HOTPLUG_PWR	0x6052
#define DEVICE_TYPE_EFP_DVI_I		0x6053
#define DEVICE_TYPE_EFP_DVI_D_DUAL	0x6152
#define DEVICE_TYPE_EFP_DVI_D_HDCP	0x60d2
#define DEVICE_TYPE_OPENLDI_HOTPLUG_PWR	0x6062
#define DEVICE_TYPE_OPENLDI_DUALPIX	0x6162
#define DEVICE_TYPE_LFP_PANELLINK	0x5012
#define DEVICE_TYPE_LFP_CMOS_PWR	0x5042
#define DEVICE_TYPE_LFP_LVDS_PWR	0x5062
#define DEVICE_TYPE_LFP_LVDS_DUAL	0x5162
#define DEVICE_TYPE_LFP_LVDS_DUAL_HDCP	0x51e2

#define DEVICE_CFG_NONE		0x00
#define DEVICE_CFG_12BIT_DVOB	0x01
#define DEVICE_CFG_12BIT_DVOC	0x02
#define DEVICE_CFG_24BIT_DVOBC	0x09
#define DEVICE_CFG_24BIT_DVOCB	0x0a
#define DEVICE_CFG_DUAL_DVOB	0x11
#define DEVICE_CFG_DUAL_DVOC	0x12
#define DEVICE_CFG_DUAL_DVOBC	0x13
#define DEVICE_CFG_DUAL_LINK_DVOBC	0x19
#define DEVICE_CFG_DUAL_LINK_DVOCB	0x1a

#define DEVICE_WIRE_NONE	0x00
#define DEVICE_WIRE_DVOB	0x01
#define DEVICE_WIRE_DVOC	0x02
#define DEVICE_WIRE_DVOBC	0x03
#define DEVICE_WIRE_DVOBB	0x05
#define DEVICE_WIRE_DVOCC	0x06
#define DEVICE_WIRE_DVOB_MASTER 0x0d
#define DEVICE_WIRE_DVOC_MASTER 0x0e

#define DEVICE_PORT_DVOA	0x00 /* none on 845+ */
#define DEVICE_PORT_DVOB	0x01
#define DEVICE_PORT_DVOC	0x02

struct child_device_config {
	u16 handle;
	u16 device_type;
	u8  device_id[10]; /* ascii string */
	u16 addin_offset;
	u8  dvo_port; /* See Device_PORT_* above */
	u8  i2c_pin;
	u8  slave_addr;
	u8  ddc_pin;
	u16 edid_ptr;
	u8  dvo_cfg; /* See DEVICE_CFG_* above */
	u8  dvo2_port;
	u8  i2c2_pin;
	u8  slave2_addr;
	u8  ddc2_pin;
	u8  capabilities;
	u8  dvo_wiring;/* See DEVICE_WIRE_* above */
	u8  dvo2_wiring;
	u16 extended_type;
	u8  dvo_function;
} __packed;


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

	/*
	 * Device info:
	 * If TV is present, it'll be at devices[0].
	 * LVDS will be next, either devices[0] or [1], if present.
	 * On some platforms the number of device is 6. But could be as few as
	 * 4 if both TV and LVDS are missing.
	 * And the device num is related with the size of general definition
	 * block. It is obtained by using the following formula:
	 * number = (block_size - sizeof(bdb_general_definitions))/
	 *	     sizeof(child_device_config);
	 */
	struct child_device_config devices[0];
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
} __packed;

struct bdb_lvds_backlight {
	u8 type:2;
	u8 pol:1;
	u8 gpio:3;
	u8 gmbus:2;
	u16 freq;
	u8 minbrightness;
	u8 i2caddr;
	u8 brightnesscmd;
	/*FIXME: more...*/
} __packed;

/* LFP pointer table contains entries to the struct below */
struct bdb_lvds_lfp_data_ptr {
	u16 fp_timing_offset; /* offsets are from start of bdb */
	u8 fp_table_size;
	u16 dvo_timing_offset;
	u8 dvo_table_size;
	u16 panel_pnp_id_offset;
	u8 pnp_table_size;
} __packed;

struct bdb_lvds_lfp_data_ptrs {
	u8 lvds_entries; /* followed by one or more lvds_data_ptr structs */
	struct bdb_lvds_lfp_data_ptr ptr[16];
} __packed;

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
} __packed;

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
} __packed;

struct lvds_pnp_id {
	u16 mfg_name;
	u16 product_code;
	u32 serial;
	u8 mfg_week;
	u8 mfg_year;
} __packed;

struct bdb_lvds_lfp_data_entry {
	struct lvds_fp_timing fp_timing;
	struct lvds_dvo_timing dvo_timing;
	struct lvds_pnp_id pnp_id;
} __packed;

struct bdb_lvds_lfp_data {
	struct bdb_lvds_lfp_data_entry data[16];
} __packed;

struct aimdb_header {
	char signature[16];
	char oem_device[20];
	u16 aimdb_version;
	u16 aimdb_header_size;
	u16 aimdb_size;
} __packed;

struct aimdb_block {
	u8 aimdb_id;
	u16 aimdb_size;
} __packed;

struct vch_panel_data {
	u16 fp_timing_offset;
	u8 fp_timing_size;
	u16 dvo_timing_offset;
	u8 dvo_timing_size;
	u16 text_fitting_offset;
	u8 text_fitting_size;
	u16 graphics_fitting_offset;
	u8 graphics_fitting_size;
} __packed;

struct vch_bdb_22 {
	struct aimdb_block aimdb_block;
	struct vch_panel_data panels[16];
} __packed;

struct bdb_sdvo_lvds_options {
	u8 panel_backlight;
	u8 h40_set_panel_type;
	u8 panel_type;
	u8 ssc_clk_freq;
	u16 als_low_trip;
	u16 als_high_trip;
	u8 sclalarcoeff_tab_row_num;
	u8 sclalarcoeff_tab_row_size;
	u8 coefficient[8];
	u8 panel_misc_bits_1;
	u8 panel_misc_bits_2;
	u8 panel_misc_bits_3;
	u8 panel_misc_bits_4;
} __packed;

#define BDB_DRIVER_FEATURE_NO_LVDS		0
#define BDB_DRIVER_FEATURE_INT_LVDS		1
#define BDB_DRIVER_FEATURE_SDVO_LVDS		2
#define BDB_DRIVER_FEATURE_EDP			3

struct bdb_driver_features {
	u8 boot_dev_algorithm:1;
	u8 block_display_switch:1;
	u8 allow_display_switch:1;
	u8 hotplug_dvo:1;
	u8 dual_view_zoom:1;
	u8 int15h_hook:1;
	u8 sprite_in_clone:1;
	u8 primary_lfp_id:1;

	u16 boot_mode_x;
	u16 boot_mode_y;
	u8 boot_mode_bpp;
	u8 boot_mode_refresh;

	u16 enable_lfp_primary:1;
	u16 selective_mode_pruning:1;
	u16 dual_frequency:1;
	u16 render_clock_freq:1; /* 0: high freq; 1: low freq */
	u16 nt_clone_support:1;
	u16 power_scheme_ui:1; /* 0: CUI; 1: 3rd party */
	u16 sprite_display_assign:1; /* 0: secondary; 1: primary */
	u16 cui_aspect_scaling:1;
	u16 preserve_aspect_ratio:1;
	u16 sdvo_device_power_down:1;
	u16 crt_hotplug:1;
	u16 lvds_config:2;
	u16 tv_hotplug:1;
	u16 hdmi_config:2;

	u8 static_display:1;
	u8 reserved2:7;
	u16 legacy_crt_max_x;
	u16 legacy_crt_max_y;
	u8 legacy_crt_max_refresh;

	u8 hdmi_termination;
	u8 custom_vbt_version;
} __packed;

#define EDP_18BPP	0
#define EDP_24BPP	1
#define EDP_30BPP	2
#define EDP_RATE_1_62	0
#define EDP_RATE_2_7	1
#define EDP_LANE_1	0
#define EDP_LANE_2	1
#define EDP_LANE_4	3
#define EDP_PREEMPHASIS_NONE	0
#define EDP_PREEMPHASIS_3_5dB	1
#define EDP_PREEMPHASIS_6dB	2
#define EDP_PREEMPHASIS_9_5dB	3
#define EDP_VSWING_0_4V		0
#define EDP_VSWING_0_6V		1
#define EDP_VSWING_0_8V		2
#define EDP_VSWING_1_2V		3

struct edp_power_seq {
	u16 t1_t3;
	u16 t8;
	u16 t9;
	u16 t10;
	u16 t11_t12;
} __attribute__ ((packed));

struct edp_link_params {
	u8 rate:4;
	u8 lanes:4;
	u8 preemphasis:4;
	u8 vswing:4;
} __attribute__ ((packed));

struct bdb_edp {
	struct edp_power_seq power_seqs[16];
	u32 color_depth;
	u32 sdrrs_msa_timing_delay;
	struct edp_link_params link_params[16];
} __attribute__ ((packed));

extern int psb_intel_init_bios(struct drm_device *dev);
extern void psb_intel_destroy_bios(struct drm_device *dev);

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
#define   SWF14_DS_PIPEB_TV_EN	 (1<<9)
#define   SWF14_DS_PIPEB_CRT_EN  (1<<8)
#define   SWF14_DS_PIPEA_LFP2_EN (1<<7)
#define   SWF14_DS_PIPEA_EFP2_EN (1<<6)
#define   SWF14_DS_PIPEA_TV2_EN  (1<<5)
#define   SWF14_DS_PIPEA_CRT2_EN (1<<4)
#define   SWF14_DS_PIPEA_LFP_EN  (1<<3)
#define   SWF14_DS_PIPEA_EFP_EN  (1<<2)
#define   SWF14_DS_PIPEA_TV_EN	 (1<<1)
#define   SWF14_DS_PIPEA_CRT_EN  (1<<0)
	  /* if GR18 indicates a panel fitting request */
#define   SWF14_PFIT_EN		(1<<0) /* 0 means disable */
	  /* if GR18 indicates an APM change request */
#define   SWF14_APM_HIBERNATE	0x4
#define   SWF14_APM_SUSPEND	0x3
#define   SWF14_APM_STANDBY	0x1
#define   SWF14_APM_RESTORE	0x0

/* Add the device class for LFP, TV, HDMI */
#define	 DEVICE_TYPE_INT_LFP	0x1022
#define	 DEVICE_TYPE_INT_TV	0x1009
#define	 DEVICE_TYPE_HDMI	0x60D2
#define	 DEVICE_TYPE_DP		0x68C6
#define	 DEVICE_TYPE_eDP	0x78C6

/* define the DVO port for HDMI output type */
#define		DVO_B		1
#define		DVO_C		2
#define		DVO_D		3

/* define the PORT for DP output type */
#define		PORT_IDPB	7
#define		PORT_IDPC	8
#define		PORT_IDPD	9

#endif /* _INTEL_BIOS_H_ */
