/*
 * Copyright © 2006-2016 Intel Corporation
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

/*
 * This information is private to VBT parsing in intel_bios.c.
 *
 * Please do NOT include anywhere else.
 */
#ifndef _INTEL_BIOS_PRIVATE
#error "intel_vbt_defs.h is private to intel_bios.c"
#endif

#ifndef _INTEL_VBT_DEFS_H_
#define _INTEL_VBT_DEFS_H_

#include "intel_dsi_vbt_defs.h"

/* EDID derived structures */
struct bdb_edid_pnp_id {
	u16 mfg_name;
	u16 product_code;
	u32 serial;
	u8 mfg_week;
	u8 mfg_year;
} __packed;

struct bdb_edid_product_name {
	char name[13];
} __packed;

struct bdb_edid_dtd {
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
	u8 hsync_pulse_width_lo;
	u8 vsync_pulse_width_lo:4;
	u8 vsync_off_lo:4;
	u8 vsync_pulse_width_hi:2;
	u8 vsync_off_hi:2;
	u8 hsync_pulse_width_hi:2;
	u8 hsync_off_hi:2;
	u8 himage_lo;
	u8 vimage_lo;
	u8 vimage_hi:4;
	u8 himage_hi:4;
	u8 h_border;
	u8 v_border;
	u8 rsvd1:3;
	u8 digital:2;
	u8 vsync_positive:1;
	u8 hsync_positive:1;
	u8 non_interlaced:1;
} __packed;

/**
 * struct vbt_header - VBT Header structure
 * @signature:		VBT signature, always starts with "$VBT"
 * @version:		Version of this structure
 * @header_size:	Size of this structure
 * @vbt_size:		Size of VBT (VBT Header, BDB Header and data blocks)
 * @vbt_checksum:	Checksum
 * @reserved0:		Reserved
 * @bdb_offset:		Offset of &struct bdb_header from beginning of VBT
 * @aim_offset:		Offsets of add-in data blocks from beginning of VBT
 */
struct vbt_header {
	u8 signature[20];
	u16 version;
	u16 header_size;
	u16 vbt_size;
	u8 vbt_checksum;
	u8 reserved0;
	u32 bdb_offset;
	u32 aim_offset[4];
} __packed;

/**
 * struct bdb_header - BDB Header structure
 * @signature:		BDB signature "BIOS_DATA_BLOCK"
 * @version:		Version of the data block definitions
 * @header_size:	Size of this structure
 * @bdb_size:		Size of BDB (BDB Header and data blocks)
 */
struct bdb_header {
	u8 signature[16];
	u16 version;
	u16 header_size;
	u16 bdb_size;
} __packed;

/*
 * BDB version number dependencies are documented as:
 *
 * <start>+
 *    indicates the field was introduced in version <start>
 *    and is still valid
 *
 * <start>-<end>
 *    indicates the field was introduced in version <start>
 *    and obsoleted in version <end>+1.
 *
 * ??? indicates the specific version number is unknown
 */

/*
 * There are several types of BIOS data blocks (BDBs), each block has
 * an ID and size in the first 3 bytes (ID in first, size in next 2).
 * Known types are listed below.
 */
enum bdb_block_id {
	BDB_GENERAL_FEATURES		= 1,
	BDB_GENERAL_DEFINITIONS		= 2,
	BDB_DISPLAY_TOGGLE		= 3,
	BDB_MODE_SUPPORT_LIST		= 4,
	BDB_GENERIC_MODE_TABLE		= 5,
	BDB_EXT_MMIO_REGS		= 6, /* VBIOS only */
	BDB_SWF_IO			= 7, /* VBIOS only */
	BDB_SWF_MMIO			= 8, /* VBIOS only */
	BDB_DOT_CLOCK_OVERRIDE_ALM	= 9,
	BDB_PSR				= 9, /* 165+ */
	BDB_MODE_REMOVAL_TABLE		= 10,
	BDB_CHILD_DEVICE_TABLE		= 11,
	BDB_DRIVER_FEATURES		= 12,
	BDB_DRIVER_PERSISTENCE		= 13,
	BDB_EXT_TABLE_PTRS		= 14, /* VBIOS only */
	BDB_DOT_CLOCK_OVERRIDE		= 15,
	BDB_DISPLAY_SELECT_OLD		= 16,
	BDB_SV_TEST_FUNCTIONS		= 17,
	BDB_DRIVER_ROTATION		= 18,
	BDB_DISPLAY_REMOVE_OLD		= 19,
	BDB_OEM_CUSTOM			= 20,
	BDB_EFP_LIST			= 21, /* workarounds for VGA hsync/vsync */
	BDB_SDVO_LVDS_OPTIONS		= 22,
	BDB_SDVO_LVDS_DTD		= 23,
	BDB_SDVO_LVDS_PNP_ID		= 24,
	BDB_SDVO_LVDS_PPS		= 25,
	BDB_TV_OPTIONS			= 26,
	BDB_EDP				= 27,
	BDB_EFP_DTD			= 28, /* 161+ */
	BDB_DISPLAY_SELECT_IVB		= 29, /* 164+ */
	BDB_DISPLAY_REMOVE_IVB		= 30, /* 164+ */
	BDB_DISPLAY_SELECT_HSW		= 31, /* 166+ */
	BDB_DISPLAY_REMOVE_HSW		= 32, /* 166+ */
	BDB_LFP_OPTIONS			= 40,
	BDB_LFP_DATA_PTRS		= 41,
	BDB_LFP_DATA			= 42,
	BDB_LFP_BACKLIGHT		= 43,
	BDB_LFP_POWER			= 44,
	BDB_EDP_BFI			= 45, /* 160+ */
	BDB_CHROMATICITY		= 46, /* 169+ */
	BDB_MIPI			= 50, /* 170-172 */
	BDB_FIXED_SET_MODE		= 51, /* 172+ */
	BDB_MIPI_CONFIG			= 52, /* 175+ */
	BDB_MIPI_SEQUENCE		= 53, /* 177+ */
	BDB_RGB_PALETTE			= 54, /* 180+ */
	BDB_COMPRESSION_PARAMETERS_OLD	= 55, /* 198-212 */
	BDB_COMPRESSION_PARAMETERS	= 56, /* 213+ */
	BDB_VSWING_PREEMPH		= 57, /* 218+ */
	BDB_GENERIC_DTD			= 58, /* 229+ */
	BDB_INT15_HOOK			= 252, /* VBIOS only */
	BDB_PRD_TABLE			= 253,
	BDB_SKIP			= 254, /* VBIOS only */
};

/*
 * Block 1 - General Bit Definitions
 */

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
	u8 underscan_vga_timings:1;
	u8 display_clock_mode:1;
	u8 vbios_hotplug_support:1;

        /* bits 3 */
	u8 disable_smooth_vision:1;
	u8 single_dvi:1;
	u8 rotate_180:1;					/* 181+ */
	u8 fdi_rx_polarity_inverted:1;
	u8 vbios_extended_mode:1;				/* 160+ */
	u8 copy_ilfp_dtd_to_sdvo_lvds_dtd:1;			/* 160+ */
	u8 panel_best_fit_timing:1;				/* 160+ */
	u8 ignore_strap_state:1;				/* 160+ */

        /* bits 4 */
	u8 legacy_monitor_detect;

        /* bits 5 */
	u8 int_crt_support:1;
	u8 int_tv_support:1;
	u8 int_efp_support:1;
	u8 dp_ssc_enable:1;	/* PCH attached eDP supports SSC */
	u8 dp_ssc_freq:1;	/* SSC freq for PCH attached eDP */
	u8 dp_ssc_dongle_supported:1;
	u8 rsvd11:2; /* finish byte */

	/* bits 6 */
	u8 tc_hpd_retry_timeout:7;				/* 242+ */
	u8 rsvd12:1;

	/* bits 7 */
	u8 afc_startup_config:2;				/* 249+ */
	u8 rsvd13:6;
} __packed;

/*
 * Block 2 - General Bytes Definition
 */

/* pre-915 */
#define GPIO_PIN_DVI_LVDS	0x03 /* "DVI/LVDS DDC GPIO pins" */
#define GPIO_PIN_ADD_I2C	0x05 /* "ADDCARD I2C GPIO pins" */
#define GPIO_PIN_ADD_DDC	0x04 /* "ADDCARD DDC GPIO pins" */
#define GPIO_PIN_ADD_DDC_I2C	0x06 /* "ADDCARD DDC/I2C GPIO pins" */

/* Device handle */
#define DEVICE_HANDLE_CRT	0x0001
#define DEVICE_HANDLE_TV	0x0002 /* ???-214 */
#define DEVICE_HANDLE_EFP1	0x0004
#define DEVICE_HANDLE_EFP2	0x0040
#define DEVICE_HANDLE_EFP3	0x0020
#define DEVICE_HANDLE_EFP4	0x0010
#define DEVICE_HANDLE_EFP5	0x0002 /* 215+ */
#define DEVICE_HANDLE_EFP6	0x0001 /* 217+ */
#define DEVICE_HANDLE_EFP7	0x0100 /* 217+ */
#define DEVICE_HANDLE_EFP8	0x0200 /* 217+ */
#define DEVICE_HANDLE_LFP1	0x0008
#define DEVICE_HANDLE_LFP2	0x0080

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

/* Add the device class for LFP, TV, HDMI */
#define DEVICE_TYPE_INT_LFP		0x1022
#define DEVICE_TYPE_INT_TV		0x1009
#define DEVICE_TYPE_HDMI		0x60D2
#define DEVICE_TYPE_DP			0x68C6
#define DEVICE_TYPE_DP_DUAL_MODE	0x60D6
#define DEVICE_TYPE_eDP			0x78C6

#define DEVICE_TYPE_CLASS_EXTENSION	(1 << 15)
#define DEVICE_TYPE_POWER_MANAGEMENT	(1 << 14)
#define DEVICE_TYPE_HOTPLUG_SIGNALING	(1 << 13)
#define DEVICE_TYPE_INTERNAL_CONNECTOR	(1 << 12)
#define DEVICE_TYPE_NOT_HDMI_OUTPUT	(1 << 11)
#define DEVICE_TYPE_MIPI_OUTPUT		(1 << 10)
#define DEVICE_TYPE_COMPOSITE_OUTPUT	(1 << 9)
#define DEVICE_TYPE_DUAL_CHANNEL	(1 << 8)
#define DEVICE_TYPE_HIGH_SPEED_LINK	(1 << 6)
#define DEVICE_TYPE_LVDS_SIGNALING	(1 << 5)
#define DEVICE_TYPE_TMDS_DVI_SIGNALING	(1 << 4)
#define DEVICE_TYPE_VIDEO_SIGNALING	(1 << 3)
#define DEVICE_TYPE_DISPLAYPORT_OUTPUT	(1 << 2)
#define DEVICE_TYPE_DIGITAL_OUTPUT	(1 << 1)
#define DEVICE_TYPE_ANALOG_OUTPUT	(1 << 0)

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

/* dvo_port pre BDB 155 */
#define DEVICE_PORT_DVOA	0x00 /* none on 845+ */
#define DEVICE_PORT_DVOB	0x01
#define DEVICE_PORT_DVOC	0x02

/* dvo_port BDB 155+ */
#define DVO_PORT_HDMIA		0
#define DVO_PORT_HDMIB		1
#define DVO_PORT_HDMIC		2
#define DVO_PORT_HDMID		3
#define DVO_PORT_LVDS		4
#define DVO_PORT_TV		5
#define DVO_PORT_CRT		6
#define DVO_PORT_DPB		7
#define DVO_PORT_DPC		8
#define DVO_PORT_DPD		9
#define DVO_PORT_DPA		10
#define DVO_PORT_DPE		11				/* 193+ */
#define DVO_PORT_HDMIE		12				/* 193+ */
#define DVO_PORT_DPF		13				/* N/A */
#define DVO_PORT_HDMIF		14				/* N/A */
#define DVO_PORT_DPG		15				/* 217+ */
#define DVO_PORT_HDMIG		16				/* 217+ */
#define DVO_PORT_DPH		17				/* 217+ */
#define DVO_PORT_HDMIH		18				/* 217+ */
#define DVO_PORT_DPI		19				/* 217+ */
#define DVO_PORT_HDMII		20				/* 217+ */
#define DVO_PORT_MIPIA		21				/* 171+ */
#define DVO_PORT_MIPIB		22				/* 171+ */
#define DVO_PORT_MIPIC		23				/* 171+ */
#define DVO_PORT_MIPID		24				/* 171+ */

#define HDMI_MAX_DATA_RATE_PLATFORM	0			/* 204+ */
#define HDMI_MAX_DATA_RATE_297		1			/* 204+ */
#define HDMI_MAX_DATA_RATE_165		2			/* 204+ */
#define HDMI_MAX_DATA_RATE_594		3			/* 249+ */
#define HDMI_MAX_DATA_RATE_340		4			/* 249+ */
#define HDMI_MAX_DATA_RATE_300		5			/* 249+ */

#define LEGACY_CHILD_DEVICE_CONFIG_SIZE		33

/* DDC Bus DDI Type 155+ */
enum vbt_gmbus_ddi {
	DDC_BUS_DDI_B = 0x1,
	DDC_BUS_DDI_C,
	DDC_BUS_DDI_D,
	DDC_BUS_DDI_F,
	ICL_DDC_BUS_DDI_A = 0x1,
	ICL_DDC_BUS_DDI_B,
	TGL_DDC_BUS_DDI_C,
	RKL_DDC_BUS_DDI_D = 0x3,
	RKL_DDC_BUS_DDI_E,
	ICL_DDC_BUS_PORT_1 = 0x4,
	ICL_DDC_BUS_PORT_2,
	ICL_DDC_BUS_PORT_3,
	ICL_DDC_BUS_PORT_4,
	TGL_DDC_BUS_PORT_5,
	TGL_DDC_BUS_PORT_6,
	ADLS_DDC_BUS_PORT_TC1 = 0x2,
	ADLS_DDC_BUS_PORT_TC2,
	ADLS_DDC_BUS_PORT_TC3,
	ADLS_DDC_BUS_PORT_TC4,
	ADLP_DDC_BUS_PORT_TC1 = 0x3,
	ADLP_DDC_BUS_PORT_TC2,
	ADLP_DDC_BUS_PORT_TC3,
	ADLP_DDC_BUS_PORT_TC4

};

#define DP_AUX_A 0x40
#define DP_AUX_B 0x10
#define DP_AUX_C 0x20
#define DP_AUX_D 0x30
#define DP_AUX_E 0x50
#define DP_AUX_F 0x60
#define DP_AUX_G 0x70
#define DP_AUX_H 0x80
#define DP_AUX_I 0x90

/* DP max link rate 216+ */
#define BDB_216_VBT_DP_MAX_LINK_RATE_HBR3	0
#define BDB_216_VBT_DP_MAX_LINK_RATE_HBR2	1
#define BDB_216_VBT_DP_MAX_LINK_RATE_HBR	2
#define BDB_216_VBT_DP_MAX_LINK_RATE_LBR	3

/* DP max link rate 230+ */
#define BDB_230_VBT_DP_MAX_LINK_RATE_DEF	0
#define BDB_230_VBT_DP_MAX_LINK_RATE_LBR	1
#define BDB_230_VBT_DP_MAX_LINK_RATE_HBR	2
#define BDB_230_VBT_DP_MAX_LINK_RATE_HBR2	3
#define BDB_230_VBT_DP_MAX_LINK_RATE_HBR3	4
#define BDB_230_VBT_DP_MAX_LINK_RATE_UHBR10	5
#define BDB_230_VBT_DP_MAX_LINK_RATE_UHBR13P5	6
#define BDB_230_VBT_DP_MAX_LINK_RATE_UHBR20	7

/* EDP link rate 263+ */
#define BDB_263_VBT_EDP_LINK_RATE_1_62		BIT_U32(0)
#define BDB_263_VBT_EDP_LINK_RATE_2_16		BIT_U32(1)
#define BDB_263_VBT_EDP_LINK_RATE_2_43		BIT_U32(2)
#define BDB_263_VBT_EDP_LINK_RATE_2_7		BIT_U32(3)
#define BDB_263_VBT_EDP_LINK_RATE_3_24		BIT_U32(4)
#define BDB_263_VBT_EDP_LINK_RATE_4_32		BIT_U32(5)
#define BDB_263_VBT_EDP_LINK_RATE_5_4		BIT_U32(6)
#define BDB_263_VBT_EDP_LINK_RATE_6_75		BIT_U32(7)
#define BDB_263_VBT_EDP_LINK_RATE_8_1		BIT_U32(8)
#define BDB_263_VBT_EDP_LINK_RATE_10		BIT_U32(9)
#define BDB_263_VBT_EDP_LINK_RATE_13_5		BIT_U32(10)
#define BDB_263_VBT_EDP_LINK_RATE_20		BIT_U32(11)
#define BDB_263_VBT_EDP_NUM_RATES		12
#define BDB_263_VBT_EDP_RATES_MASK		GENMASK(BDB_263_VBT_EDP_NUM_RATES - 1, 0)

/*
 * The child device config, aka the display device data structure, provides a
 * description of a port and its configuration on the platform.
 *
 * The child device config size has been increased, and fields have been added
 * and their meaning has changed over time. Care must be taken when accessing
 * basically any of the fields to ensure the correct interpretation for the BDB
 * version in question.
 *
 * When we copy the child device configs to display->vbt.child_dev, we
 * reserve space for the full structure below, and initialize the tail not
 * actually present in VBT to zeros. Accessing those fields is fine, as long as
 * the default zero is taken into account, again according to the BDB version.
 *
 * BDB versions 155 and below are considered legacy, and version 155 seems to be
 * a baseline for some of the VBT documentation. When adding new fields, please
 * include the BDB version when the field was added, if it's above that.
 */
struct child_device_config {
	u16 handle;
	u16 device_type; /* See DEVICE_TYPE_* above */

	union {
		u8  device_id[10]; /* ascii string */
		struct {
			u8 i2c_speed;
			u8 dp_onboard_redriver_preemph:3;	/* 158+ */
			u8 dp_onboard_redriver_vswing:3;	/* 158+ */
			u8 dp_onboard_redriver_present:1;	/* 158+ */
			u8 reserved0:1;
			u8 dp_ondock_redriver_preemph:3;	/* 158+ */
			u8 dp_ondock_redriver_vswing:3;		/* 158+ */
			u8 dp_ondock_redriver_present:1;	/* 158+ */
			u8 reserved1:1;
			u8 hdmi_level_shifter_value:5;		/* 158+ */
			u8 hdmi_max_data_rate:3;		/* 204+ */
			u16 dtd_buf_ptr;			/* 161+ */
			u8 edidless_efp:1;			/* 161+ */
			u8 compression_enable:1;		/* 198+ */
			u8 compression_method_cps:1;		/* 198+ */
			u8 ganged_edp:1;			/* 202+ */
			u8 lttpr_non_transparent:1;		/* 235+ */
			u8 disable_compression_for_ext_disp:1;	/* 251+ */
			u8 reserved2:2;
			u8 compression_structure_index:4;	/* 198+ */
			u8 reserved3:4;
			u8 hdmi_max_frl_rate:4;			/* 237+ */
			u8 hdmi_max_frl_rate_valid:1;		/* 237+ */
			u8 reserved4:3;				/* 237+ */
			u8 reserved5;
		} __packed;
	} __packed;

	u16 addin_offset;
	u8 dvo_port; /* See DEVICE_PORT_* and DVO_PORT_* above */
	u8 i2c_pin;
	u8 target_addr;
	u8 ddc_pin;
	u16 edid_ptr;
	u8 dvo_cfg; /* See DEVICE_CFG_* above */

	union {
		struct {
			u8 dvo2_port;
			u8 i2c2_pin;
			u8 target2_addr;
			u8 ddc2_pin;
		} __packed;
		struct {
			u8 efp_routed:1;			/* 158+ */
			u8 lane_reversal:1;			/* 184+ */
			u8 lspcon:1;				/* 192+ */
			u8 iboost:1;				/* 196+ */
			u8 hpd_invert:1;			/* 196+ */
			u8 use_vbt_vswing:1;			/* 218+ */
			u8 dp_max_lane_count:2;			/* 244+ */
			u8 hdmi_support:1;			/* 158+ */
			u8 dp_support:1;			/* 158+ */
			u8 tmds_support:1;			/* 158+ */
			u8 support_reserved:5;
			u8 aux_channel;
			u8 dongle_detect;
		} __packed;
	} __packed;

	u8 pipe_cap:2;
	u8 sdvo_stall:1;					/* 158+ */
	u8 hpd_status:2;
	u8 integrated_encoder:1;
	u8 capabilities_reserved:2;
	u8 dvo_wiring; /* See DEVICE_WIRE_* above */

	union {
		u8 dvo2_wiring;
		u8 mipi_bridge_type;				/* 171+ */
	} __packed;

	u16 extended_type;
	u8 dvo_function;
	u8 dp_usb_type_c:1;					/* 195+ */
	u8 tbt:1;						/* 209+ */
	u8 flags2_reserved:2;					/* 195+ */
	u8 dp_port_trace_length:4;				/* 209+ */
	u8 dp_gpio_index;					/* 195+ */
	u16 dp_gpio_pin_num;					/* 195+ */
	u8 dp_iboost_level:4;					/* 196+ */
	u8 hdmi_iboost_level:4;					/* 196+ */
	u8 dp_max_link_rate:3;					/* 216+ */
	u8 dp_max_link_rate_reserved:5;				/* 216+ */
	u8 efp_index;						/* 256+ */
	u32 edp_data_rate_override:12;				/* 263+ */
	u32 edp_data_rate_override_reserved:20;			/* 263+ */
} __packed;

struct bdb_general_definitions {
	/* DDC GPIO */
	u8 crt_ddc_gmbus_pin;

	/* DPMS bits */
	u8 dpms_non_acpi:1;
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
	 *	     defs->child_dev_size;
	 */
	u8 devices[];
} __packed;

/*
 * Block 3 - Display Toggle Option Block
 */

struct bdb_display_toggle {
	u8 feature_bits;
	u16 num_entries;					/* ALM only */
	u16 list[];						/* ALM only */
} __packed;

/*
 * Block 4 - Mode Support List
 */

struct bdb_mode_support_list {
	u8 intel_mode_number[0];
	u16 mode_list_length;
} __packed;

/*
 * Block 5 - Generic Mode Table
 */

struct generic_mode_table {
	u16 x_res;
	u16 y_res;
	u8 color_depths;
	u8 refresh_rate[3];
	u8 reserved;
	u8 text_cols;
	u8 text_rows;
	u8 font_height;
	u16 page_size;
	u8 misc;
} __packed;

struct generic_mode_timings {
	u32 dotclock_khz;
	u16 hdisplay;
	u16 htotal;
	u16 hblank_start;
	u16 hblank_end;
	u16 hsync_start;
	u16 hsync_end;
	u16 vdisplay;
	u16 vtotal;
	u16 vblank_start;
	u16 vblank_end;
	u16 vsync_start;
	u16 vsync_end;
} __packed;

struct generic_mode_timings_alm {
	struct generic_mode_timings timings;
	u8 wm_8bpp;
	u8 burst_8bpp;
	u8 wm_16bpp;
	u8 burst_16bpp;
	u8 wm_32bpp;
	u8 burst_32bpp;
} __packed;

struct bdb_generic_mode_table_alm {
	struct generic_mode_table table;
	struct generic_mode_timings_alm timings[3];
} __packed;

struct bdb_generic_mode_table_mgm {
	u16 mode_flag;
	struct generic_mode_table table;
	struct generic_mode_timings timings[3];
} __packed;

/*
 * Block 6 - Extended MMIO Register Table, VBIOS only
 * Block 7 - IO Software Flag Table, VBIOS only
 * Block 8 - MMIO SWF Register Table, VBIOS only
 */
struct bdb_reg_table {
	u16 table_id;
	u8 data_access_size;
	/*
	 * offset,value tuples:
	 * data_access_size==0xce -> u8,u8
	 * data_access_size==0x02 -> u32,u32
	 */
	/* u16 table_end_marker; */
} __packed;

/*
 * Block 9 - Undocumented table (ALM only)
 */

struct dot_clock_override_entry_gen2 {
	u32 dotclock;
	u8 n;
	u8 m1;
	u8 m2;
	u8 p1:5;
	u8 p1_div_by_2:1;
	u8 reserved:1;
	u8 p2_div_by_4:1;
} __packed;

struct bdb_dot_clock_override_alm {
	struct dot_clock_override_entry_gen2 t[0];
} __packed;

/*
 * Block 9 - SRD Feature Block
 */

struct psr_table {
	/* Feature bits */
	u8 full_link:1;						/* 165+ */
	u8 require_aux_to_wakeup:1;				/* 165+ */
	u8 feature_bits_rsvd:6;

	/* Wait times */
	u8 idle_frames:4;					/* 165+ */
	u8 lines_to_wait:3;					/* 165+ */
	u8 wait_times_rsvd:1;

	/* TP wake up time in multiple of 100 */
	u16 tp1_wakeup_time;					/* 165+ */
	u16 tp2_tp3_wakeup_time;				/* 165+ */
} __packed;

struct bdb_psr {
	struct psr_table psr_table[16];

	/* PSR2 TP2/TP3 wakeup time for 16 panels */
	u32 psr2_tp2_tp3_wakeup_time;				/* 226+ */
} __packed;

/*
 * Block 10 - Mode Removal Table
 */

struct mode_removal_table {
	u16 x_res;
	u16 y_res;
	u8 bpp;
	u16 refresh_rate;
	u8 removal_flags;
	u16 panel_flags;
} __packed;

struct bdb_mode_removal {
	u8 row_size; /* 8 or 10 bytes */
	/*
	 * VBT spec says this is always 20 entries,
	 * but ALM seems to have only 15 entries.
	 */
	struct mode_removal_table modes[];
	/* u16 terminator; 0x0000 */
} __packed;

/*
 * Block 12 - Driver Features Data Block
 */

#define BDB_DRIVER_FEATURE_NO_LVDS		0
#define BDB_DRIVER_FEATURE_INT_LVDS		1
#define BDB_DRIVER_FEATURE_SDVO_LVDS		2
#define BDB_DRIVER_FEATURE_INT_SDVO_LVDS	3

struct bdb_driver_features {
	/* Driver bits */
	u8 boot_dev_algorithm:1;
	u8 allow_display_switch_dvd:1;
	u8 allow_display_switch_dos:1;
	u8 hotplug_dvo:1;
	u8 dual_view_zoom:1;
	u8 int15h_hook:1;
	u8 sprite_in_clone:1;
	u8 primary_lfp_id:1;

	u16 boot_mode_x;
	u16 boot_mode_y;
	u8 boot_mode_bpp;
	u8 boot_mode_refresh;

	/* Extended Driver Bits 1 */
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

	/* Driver Flags 1 */
	u8 static_display:1;					/* 163+ */
	u8 embedded_platform:1;					/* 163+ */
	u8 display_subsystem_enable:1;				/* 163+ */
	u8 reserved0:5;

	u16 legacy_crt_max_x;
	u16 legacy_crt_max_y;
	u8 legacy_crt_max_refresh;

	/* Extended Driver Bits 2 */
	u8 hdmi_termination:1;
	u8 cea861d_hdmi_support:1;
	u8 self_refresh_enable:1;
	u8 reserved1:5;

	u8 custom_vbt_version;					/* 155+ */

	/* Driver Feature Flags */
	u16 rmpm_enabled:1;					/* 159+ */
	u16 s2ddt_enabled:1;					/* 159+ */
	u16 dpst_enabled:1;					/* 159-227 */
	u16 bltclt_enabled:1;					/* 159+ */
	u16 adb_enabled:1;					/* 159-227 */
	u16 drrs_enabled:1;					/* 159-227 */
	u16 grs_enabled:1;					/* 159+ */
	u16 gpmt_enabled:1;					/* 159+ */
	u16 tbt_enabled:1;					/* 159+ */
	u16 psr_enabled:1;					/* 165-227 */
	u16 ips_enabled:1;					/* 165+ */
	u16 dfps_enabled:1;					/* 165+ */
	u16 dmrrs_enabled:1;					/* 174-227 */
	u16 adt_enabled:1;					/* ???-228 */
	u16 hpd_wake:1;						/* 201-240 */
	u16 pc_feature_valid:1;					/* 159+ */
} __packed;

/*
 * Block 13 - Driver Persistent Algorithm
 */

struct bdb_driver_persistence {
	u16 hotkey_persistent_algorithm:1;
	u16 lid_switch_persistent_algorithm:1;
	u16 power_management_persistent_algorithm:1;
	u16 hotkey_persistent_on_mds_twin:1;
	u16 hotkey_persistent_on_refresh_rate:1;
	u16 hotkey_persistent_on_restore_pipe:1;
	u16 hotkey_persistent_on_mode:1;
	u16 edid_persistent_on_mode:1;
	u16 dvo_hotplug_persistent_on_mode:1;
	u16 docking_persistent_algorithm:1;
	u16 rsvd:6;
	u8 persistent_max_config;
} __packed;

/*
 * Block 15 - Dot Clock Override Table
 */

struct dot_clock_override_entry_gen3 {
	u32 dotclock;
	u8 n;
	u8 m1;
	u8 m2;
	u8 p1;
	u8 p2;
} __packed;

struct bdb_dot_clock_override {
	u8 row_size; /* 8 == gen2, 9 == gen3+ */
	u8 num_rows;
	struct dot_clock_override_entry_gen3 table[]; /* or _gen2 */
} __packed;

/*
 * Block 16 - Toggle List Block (pre-HSW)
 */

struct toggle_list_entry_old {
	u8 display_select_pipe_a;
	u8 display_select_pipe_b;
	u8 caps;
} __packed;

struct toggle_list_table_old {
	u16 num_entries;
	u8 entry_size;
	struct toggle_list_entry_old list[];
} __packed;

struct bdb_display_select_old {
	/* each table has variable size! */
	struct toggle_list_table_old tables[4];
} __packed;

/*
 * Block 17 - SV Test Functions
 */

struct bdb_sv_test_functions {
	u8 sv_bits[8];
} __packed;

/*
 * Block 18 - Driver Rotation
 */

struct bdb_driver_rotation {
	u8 rotation_enable;
	u8 rotation_flags_1;
	u16 rotation_flags_2;
	u32 rotation_flags_3;
	u32 rotation_flags_4;
} __packed;

/*
 * Block 19 - Display Configuration Removal Table (pre-IVB)
 */

struct display_remove_entry_old {
	u8 display_select_pipe_a;
	u8 display_select_pipe_b;
} __packed;

struct bdb_display_remove_old {
	u8 num_entries;
	u8 entry_size;
	struct display_remove_entry_old table[];
} __packed;

/*
 * Block 20 - OEM Customizable Modes
 */

struct oem_mode {
	u8 enable_in_vbios:1;
	u8 enable_in_os:1;
	u8 enable_in_gop:1;					/* 207+ */
	u8 reserved:5;
	u8 display_flags;					/* ???-216 */
	u16 x_res;
	u16 y_res;
	u8 color_depth;
	u8 refresh_rate;
	struct bdb_edid_dtd dtd;
	u16 display_flags_2;					/* 217+ */
} __packed;

struct bdb_oem_custom {
	u8 num_entries;
	u8 entry_size;
	struct oem_mode modes[];
} __packed;

/*
 * Block 21 - EFP List
 */

struct efp_entry {
	u16 mfg_name;
	u16 product_code;
} __packed;

struct bdb_efp_list {
	u8 num_entries;
	u8 entry_size;
	struct efp_entry efp[];
} __packed;

/*
 * Block 22 - SDVO LVDS General Options
 */

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

/*
 * Block 23 - SDVO LVDS DTD
 */

struct bdb_sdvo_lvds_dtd {
	struct bdb_edid_dtd dtd[4];
} __packed;

/*
 * Block 24 - SDVO LVDS PnP ID
 */

struct bdb_sdvo_lvds_pnp_id {
	struct bdb_edid_pnp_id pnp_id[4];
} __packed;

/*
 * Block 25 - SDVO LVDS PPS
 */

struct sdvo_lvds_pps {
	u16 t0; /* power on */
	u16 t1; /* backlight on */
	u16 t2; /* backlight off */
	u16 t3; /* power off */
	u16 t4; /* power cycle */
} __packed;

struct bdb_sdvo_lvds_pps {
	struct sdvo_lvds_pps pps[4];
} __packed;

/*
 * Block 26 - TV Options Block
 */

struct bdb_tv_options {
	u16 underscan_overscan_hdtv_component:2;
	u16 rsvd1:10;
	u16 underscan_overscan_hdtv_dvi:2;
	u16 add_modes_to_avoid_overscan_issue:1;
	u16 d_connector_support:1;
} __packed;

/*
 * Block 27 - eDP VBT Block
 */

struct edp_power_seq {
	u16 t1_t3;
	u16 t8;
	u16 t9;
	u16 t10;
	u16 t11_t12;
} __packed;

#define EDP_18BPP	0
#define EDP_24BPP	1
#define EDP_30BPP	2
#define EDP_RATE_1_62	0
#define EDP_RATE_2_7	1
#define EDP_RATE_5_4	2
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


struct edp_fast_link_params {
	u8 rate:4;						/* ???-223 */
	u8 lanes:4;
	u8 preemphasis:4;
	u8 vswing:4;
} __packed;

struct edp_pwm_delays {
	u16 pwm_on_to_backlight_enable;
	u16 backlight_disable_to_pwm_off;
} __packed;

struct edp_full_link_params {
	u8 preemphasis:4;
	u8 vswing:4;
} __packed;

struct edp_apical_params {
	u32 panel_oui;
	u32 dpcd_base_address;
	u32 dpcd_idridix_control_0;
	u32 dpcd_option_select;
	u32 dpcd_backlight;
	u32 ambient_light;
	u32 backlight_scale;
} __packed;

struct bdb_edp {
	struct edp_power_seq power_seqs[16];
	u32 color_depth;
	struct edp_fast_link_params fast_link_params[16];
	u32 sdrrs_msa_timing_delay;

	/* ith bit indicates enabled/disabled for (i+1)th panel */
	u16 edp_s3d_feature;					/* 162+ */
	u16 edp_t3_optimization;				/* 165+ */
	u64 edp_vswing_preemph;					/* 173+ */
	u16 fast_link_training;					/* 182+ */
	u16 dpcd_600h_write_required;				/* 185+ */
	struct edp_pwm_delays pwm_delays[16];			/* 186+ */
	u16 full_link_params_provided;				/* 199+ */
	struct edp_full_link_params full_link_params[16];	/* 199+ */
	u16 apical_enable;					/* 203+ */
	struct edp_apical_params apical_params[16];		/* 203+ */
	u16 edp_fast_link_training_rate[16];			/* 224+ */
	u16 edp_max_port_link_rate[16];				/* 244+ */
	u16 edp_dsc_disable;					/* 251+ */
	u16 t6_delay_support;					/* 260+ */
	u16 link_idle_time[16];					/* 260+ */
} __packed;

/*
 * Block 28 - EFP DTD Block
 */

struct bdb_efp_dtd {
	struct bdb_edid_dtd dtd[3];
} __packed;

/*
 * Block 29 - Toggle List Block (IVB)
 */

struct toggle_list_entry_ivb {
	u8 display_select;
} __packed;

struct toggle_list_table_ivb {
	u16 num_entries;
	u8 entry_size;
	struct toggle_list_entry_ivb list[];
} __packed;

struct bdb_display_select_ivb {
	/* each table has variable size! */
	struct toggle_list_table_ivb tables[4];
} __packed;

/*
 * Block 30 - Display Configuration Removal Table (IVB)
 */

struct display_remove_entry_ivb {
	u8 display_select;
} __packed;

struct bdb_display_remove_ivb {
	u8 num_entries;
	u8 entry_size;
	struct display_remove_entry_ivb table[];
} __packed;

/*
 * Block 31 - Toggle List Block (HSW+)
 */

struct toggle_list_entry_hsw {
	u16 display_select;
} __packed;

struct toggle_list_table_hsw {
	u16 num_entries;
	u8 entry_size;
	struct toggle_list_entry_hsw list[];
} __packed;

struct bdb_display_select_hsw {
	/* each table has variable size! */
	struct toggle_list_table_hsw tables[4];
} __packed;

/*
 * Block 32 - Display Configuration Removal Table (HSW+)
 */

struct display_remove_entry_hsw {
	u16 display_select;
} __packed;

struct bdb_display_remove_hsw {
	u8 num_entries;
	u8 entry_size;
	struct display_remove_entry_hsw table[];
} __packed;

/*
 * Block 40 - LFP Data Block
 */

struct bdb_lfp_options {
	u8 panel_type;
	u8 panel_type2;						/* 212+ */
	/* LVDS capabilities, stored in a dword */
	u8 pfit_mode:2;
	u8 pfit_text_mode_enhanced:1;
	u8 pfit_gfx_mode_enhanced:1;
	u8 pfit_ratio_auto:1;
	u8 pixel_dither:1;
	u8 lvds_edid:1;						/* ???-240 */
	u8 rsvd2:1;
	u8 rsvd4;
	/* LVDS Panel channel bits stored here */
	u32 lvds_panel_channel_bits;
	/* LVDS SSC (Spread Spectrum Clock) bits stored here. */
	u16 ssc_bits;
	u16 ssc_freq;
	u16 ssc_ddt;
	/* Panel color depth defined here */
	u16 panel_color_depth;
	/* LVDS panel type bits stored here */
	u32 dps_panel_type_bits;
	/* LVDS backlight control type bits stored here */
	u32 blt_control_type_bits;				/* ???-240 */

	u16 lcdvcc_s0_enable;					/* 200+ */
	u32 rotation;						/* 228+ */
	u32 position;						/* 240+ */
} __packed;

/*
 * Block 41 - LFP Data Table Pointers
 */
struct lfp_data_ptr_table {
	u16 offset; /* offsets are from start of bdb */
	u8 table_size;
} __packed;

/* LFP pointer table contains entries to the struct below */
struct lfp_data_ptr {
	struct lfp_data_ptr_table fp_timing;
	struct lfp_data_ptr_table dvo_timing;
	struct lfp_data_ptr_table panel_pnp_id;
} __packed;

struct bdb_lfp_data_ptrs {
	u8 num_entries;
	struct lfp_data_ptr ptr[16];
	struct lfp_data_ptr_table panel_name;		/* (156-163?)+ */
} __packed;

/*
 * Block 42 - LFP Data Tables
 */

/* LFP data has 3 blocks per entry */
struct fp_timing {
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

/*
 * For reference only. fp_timing has variable size so
 * the data must be accessed using the data table pointers.
 * Do not use this directly!
 */
struct lfp_data_entry {
	struct fp_timing fp_timing;
	struct bdb_edid_dtd dvo_timing;
	struct bdb_edid_pnp_id pnp_id;
} __packed;

struct bdb_lfp_data {
	struct lfp_data_entry data[16];
} __packed;

struct lfp_black_border {
	u8 top;		/* 227+ */
	u8 bottom;	/* 227+ */
	u8 left;	/* 238+ */
	u8 right;	/* 238+ */
} __packed;

struct bdb_lfp_data_tail {
	struct bdb_edid_product_name panel_name[16];		/* (156-163?)+ */
	u16 scaling_enable;					/* 187+ */
	u8 seamless_drrs_min_refresh_rate[16];			/* 188+ */
	u8 pixel_overlap_count[16];				/* 208+ */
	struct lfp_black_border black_border[16];		/* 227+ */
	u16 dual_lfp_port_sync_enable;				/* 231+ */
	u16 gpu_dithering_for_banding_artifacts;		/* 245+ */
} __packed;

/*
 * Block 43 - LFP Backlight Control Data Block
 */

#define BDB_BACKLIGHT_TYPE_NONE	0
#define BDB_BACKLIGHT_TYPE_PWM	2

struct lfp_backlight_data_entry {
	u8 type:2;
	u8 active_low_pwm:1;
	u8 i2c_pin:3;						/* obsolete since ? */
	u8 i2c_speed:2;						/* obsolete since ? */
	u16 pwm_freq_hz;
	u8 min_brightness;					/* ???-233 */
	u8 i2c_address;						/* obsolete since ? */
	u8 i2c_command;						/* obsolete since ? */
} __packed;

struct lfp_backlight_control_method {
	u8 type:4;
	u8 controller:4;
} __packed;

struct lfp_brightness_level {
	u16 level;
	u16 reserved;
} __packed;

struct bdb_lfp_backlight {
	u8 entry_size;
	struct lfp_backlight_data_entry data[16];
	u8 level[16];							/* 162-233 */
	struct lfp_backlight_control_method backlight_control[16];	/* 191+ */
	struct lfp_brightness_level brightness_level[16];		/* 234+ */
	struct lfp_brightness_level brightness_min_level[16];		/* 234+ */
	u8 brightness_precision_bits[16];				/* 236+ */
	u16 hdr_dpcd_refresh_timeout[16];				/* 239+ */
} __packed;

/*
 * Block 44 - LFP Power Conservation Features Block
 */
struct lfp_power_features {
	u8 dpst_support:1;						/* ???-159 */
	u8 power_conservation_pref:3;
	u8 reserved2:1;
	u8 lace_enabled_status:1;					/* 210+ */
	u8 lace_support:1;						/* 210+ */
	u8 als_enable:1;
} __packed;

struct als_data_entry {
	u16 backlight_adjust;
	u16 lux;
} __packed;

struct aggressiveness_profile_entry {
	u8 dpst_aggressiveness : 4;		/* (228/252)-256 */
	u8 lace_aggressiveness : 4;
} __packed;

struct aggressiveness_profile2_entry {
	u8 opst_aggressiveness : 4;
	u8 elp_aggressiveness : 4;
} __packed;

struct aggressiveness_profile3_entry {
	u8 apd_aggressiveness:4;
	u8 pixoptix_aggressiveness:4;
} __packed;

struct aggressiveness_profile4_entry {
	u8 xpst_aggressiveness:4;
	u8 tcon_aggressiveness:4;
} __packed;

struct panel_identification {
	u8 panel_technology:4;
	u8 reserved:4;
} __packed;

struct bdb_lfp_power {
	struct lfp_power_features features;				/* ???-227 */
	struct als_data_entry als[5];
	u8 lace_aggressiveness_profile:3;				/* 210-227 */
	u8 reserved1:5;
	u16 dpst;							/* 228-256 */
	u16 psr;							/* 228+ */
	u16 drrs;							/* 228+ */
	u16 lace_support;						/* 228+ */
	u16 adt;							/* 228+ */
	u16 dmrrs;							/* 228+ */
	u16 adb;							/* 228+ */
	u16 lace_enabled_status;					/* 228+ */
	struct aggressiveness_profile_entry aggressiveness[16];
	u16 hobl;							/* 232+ */
	u16 vrr_feature_enabled;					/* 233+ */
	u16 elp;							/* 247-256 */
	u16 opst;							/* 247-256 */
	struct aggressiveness_profile2_entry aggressiveness2[16];	/* 247-256 */
	u16 apd;							/* 253-256 */
	u16 pixoptix;							/* 253-256 */
	struct aggressiveness_profile3_entry aggressiveness3[16];	/* 253-256 */
	struct panel_identification panel_identification[16];		/* 257+ */
	u16 xpst_support;						/* 257+ */
	u16 tcon_based_backlight_optimization;				/* 257+ */
	struct aggressiveness_profile4_entry aggressiveness4[16];	/* 257+ */
	u16 tcon_backlight_xpst_coexistence;				/* 257+ */
} __packed;

/*
 * Block 45 - eDP BFI Block
 */

struct edp_bfi {
	u8 enable_bfi_in_driver:1;
	u8 enable_brightness_control_in_cui:1;
	u8 reserved:6;
	u8 brightness_percentage_when_bfi_disabled;
} __packed;

struct bdb_edp_bfi {
	u8 bfi_structure_size;
	struct edp_bfi bfi[16];
} __packed;

/*
 * Block 46 - Chromaticity For Narrow Gamut Panel Configuration Block
 */

struct chromaticity {
	u8 chromaticity_enable:1;
	u8 chromaticity_from_edid_base_block:1;
	u8 rsvd:6;

	u8 green_y_lo:2;
	u8 green_x_lo:2;
	u8 red_y_lo:2;
	u8 red_x_lo:2;
	u8 white_y_lo:2;
	u8 white_x_lo:2;
	u8 blue_y_lo:2;
	u8 blue_x_lo:2;

	u8 red_x_hi;
	u8 red_y_hi;
	u8 green_x_hi;
	u8 green_y_hi;
	u8 blue_x_hi;
	u8 blue_y_hi;
	u8 white_x_hi;
	u8 white_y_hi;
} __packed;

struct luminance_and_gamma {
	u8 luminance_enable:1;						/* 211+ */
	u8 gamma_enable:1;						/* 211+ */
	u8 rsvd:6;

	u16 min_luminance;						/* 211+ */
	u16 max_luminance;						/* 211+ */
	u16 one_percent_max_luminance;					/* 211+ */
	u8 gamma;							/* 211+ */
} __packed;

struct bdb_chromaticity {
	struct chromaticity chromaticity[16];
	struct luminance_and_gamma luminance_and_gamma[16];		/* 211+ */
} __packed;

/*
 * Block 50 - MIPI Block
 */

struct mipi_data {
	u16 panel_identifier;
	u16 bridge_revision;

	u32 dithering:1;
	u32 pixel_format_18bpp:1;
	u32 reserved1:1;
	u32 dphy_params_valid:1;
	u32 reserved2:28;

	u16 port_info;

	u16 reserved3:2;
	u16 num_lanes:2;
	u16 reserved4:12;

	u16 virtual_channel_num:2;
	u16 video_transfer_mode:2;
	u16 reserved5:12;

	u32 dsi_ddr_clock;
	u32 renesas_bridge_ref_clock;
	u16 power_conservation;

	u32 prepare_count:5;
	u32 reserved6:3;
	u32 clk_zero_count:8;
	u32 trail_count:5;
	u32 reserved7:3;
	u32 exit_zero_count:6;
	u32 reserved8:2;

	u32 high_low_switch_count;
	u32 lp_byte_clock;
	u32 clock_lane_switch_time_counter;
	u32 panel_color_depth;
} __packed;

struct bdb_mipi {
	struct mipi_data mipi[16];
} __packed;

/*
 * Block 51 - Fixed Set Mode Table
 */

struct bdb_fixed_set_mode {
	u8 enable;
	u32 x_res;
	u32 y_res;
} __packed;

/*
 * Block 52 - MIPI Configuration Block
 */

#define MAX_MIPI_CONFIGURATIONS	6

struct bdb_mipi_config {
	struct mipi_config config[MAX_MIPI_CONFIGURATIONS];		/* 175+ */
	struct mipi_pps_data pps[MAX_MIPI_CONFIGURATIONS];		/* 177+ */
	struct edp_pwm_delays pwm_delays[MAX_MIPI_CONFIGURATIONS];	/* 186+ */
	u8 pmic_i2c_bus_number[MAX_MIPI_CONFIGURATIONS];		/* 190+ */
} __packed;

/*
 * Block 53 - MIPI Sequence Block
 */

struct bdb_mipi_sequence {
	u8 version;
	u8 data[]; /* up to 6 variable length blocks */
} __packed;

/*
 * Block 55 - RGB Palette Table
 */

struct bdb_rgb_palette {
	u8 is_enabled;
	u8 red[256];
	u8 blue[256];
	u8 green[256];
} __packed;

/*
 * Block 56 - Compression Parameters
 */

#define VBT_RC_BUFFER_BLOCK_SIZE_1KB	0
#define VBT_RC_BUFFER_BLOCK_SIZE_4KB	1
#define VBT_RC_BUFFER_BLOCK_SIZE_16KB	2
#define VBT_RC_BUFFER_BLOCK_SIZE_64KB	3

#define VBT_DSC_LINE_BUFFER_DEPTH(vbt_value)	((vbt_value) + 8) /* bits */
#define VBT_DSC_MAX_BPP(vbt_value)		(6 + (vbt_value) * 2)

struct dsc_compression_parameters_entry {
	u8 version_major:4;
	u8 version_minor:4;

	u8 rc_buffer_block_size:2;
	u8 reserved1:6;

	/*
	 * Buffer size in bytes:
	 *
	 * 4 ^ rc_buffer_block_size * 1024 * (rc_buffer_size + 1) bytes
	 */
	u8 rc_buffer_size;
	u32 slices_per_line;

	u8 line_buffer_depth:4;
	u8 reserved2:4;

	/* Flag Bits 1 */
	u8 block_prediction_enable:1;
	u8 reserved3:7;

	u8 max_bpp; /* mapping */

	/* Color depth capabilities */
	u8 reserved4:1;
	u8 support_8bpc:1;
	u8 support_10bpc:1;
	u8 support_12bpc:1;
	u8 reserved5:4;

	u16 slice_height;
} __packed;

struct bdb_compression_parameters {
	u16 entry_size;
	struct dsc_compression_parameters_entry data[16];
} __packed;

/*
 * Block 57 -  Vswing PreEmphasis Table
 */

struct bdb_vswing_preemph {
	u8 num_tables;
	u8 num_columns;
	u32 tables[];
} __packed;

/*
 * Block 58 - Generic DTD Block
 */

struct generic_dtd_entry {
	u32 pixel_clock;
	u16 hactive;
	u16 hblank;
	u16 hfront_porch;
	u16 hsync;
	u16 vactive;
	u16 vblank;
	u16 vfront_porch;
	u16 vsync;
	u16 width_mm;
	u16 height_mm;

	/* Flags */
	u8 rsvd_flags:6;
	u8 vsync_positive_polarity:1;
	u8 hsync_positive_polarity:1;

	u8 rsvd[3];
} __packed;

struct bdb_generic_dtd {
	u16 gdtd_size;
	struct generic_dtd_entry dtd[];	/* up to 24 DTD's */
} __packed;

/*
 * Block 253 - PRD Table
 */

struct prd_entry_old {
	u8 displays_attached;
	u8 display_in_pipe_a;
	u8 display_in_pipe_b;
} __packed;

struct bdb_prd_table_old {
	struct prd_entry_old list[0];				/* ???-216 */
	u16 num_entries;					/* ???-216 */
} __packed;

struct prd_entry_new {
	u16 primary_display;
	u16 secondary_display;
} __packed;

struct bdb_prd_table_new {
	u16 num_entries;					/* 217+ */
	struct prd_entry_new list[];				/* 217+ */
} __packed;

#endif /* _INTEL_VBT_DEFS_H_ */
