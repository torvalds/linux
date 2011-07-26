/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

/* MID device specific descriptors */

struct mrst_vbt {
	s8 signature[4];	/*4 bytes,"$GCT" */
	u8 revision;
	u8 size;
	u8 checksum;
	void *mrst_gct;
} __packed;

struct mrst_timing_info {
	u16 pixel_clock;
	u8 hactive_lo;
	u8 hblank_lo;
	u8 hblank_hi:4;
	u8 hactive_hi:4;
	u8 vactive_lo;
	u8 vblank_lo;
	u8 vblank_hi:4;
	u8 vactive_hi:4;
	u8 hsync_offset_lo;
	u8 hsync_pulse_width_lo;
	u8 vsync_pulse_width_lo:4;
	u8 vsync_offset_lo:4;
	u8 vsync_pulse_width_hi:2;
	u8 vsync_offset_hi:2;
	u8 hsync_pulse_width_hi:2;
	u8 hsync_offset_hi:2;
	u8 width_mm_lo;
	u8 height_mm_lo;
	u8 height_mm_hi:4;
	u8 width_mm_hi:4;
	u8 hborder;
	u8 vborder;
	u8 unknown0:1;
	u8 hsync_positive:1;
	u8 vsync_positive:1;
	u8 separate_sync:2;
	u8 stereo:1;
	u8 unknown6:1;
	u8 interlaced:1;
} __packed;

struct gct_r10_timing_info {
	u16 pixel_clock;
	u32 hactive_lo:8;
	u32 hactive_hi:4;
	u32 hblank_lo:8;
	u32 hblank_hi:4;
	u32 hsync_offset_lo:8;
	u16 hsync_offset_hi:2;
	u16 hsync_pulse_width_lo:8;
	u16 hsync_pulse_width_hi:2;
	u16 hsync_positive:1;
	u16 rsvd_1:3;
	u8  vactive_lo:8;
	u16 vactive_hi:4;
	u16 vblank_lo:8;
	u16 vblank_hi:4;
	u16 vsync_offset_lo:4;
	u16 vsync_offset_hi:2;
	u16 vsync_pulse_width_lo:4;
	u16 vsync_pulse_width_hi:2;
	u16 vsync_positive:1;
	u16 rsvd_2:3;
} __packed;

struct mrst_panel_descriptor_v1 {
	u32 Panel_Port_Control; /* 1 dword, Register 0x61180 if LVDS */
				/* 0x61190 if MIPI */
	u32 Panel_Power_On_Sequencing;/*1 dword,Register 0x61208,*/
	u32 Panel_Power_Off_Sequencing;/*1 dword,Register 0x6120C,*/
	u32 Panel_Power_Cycle_Delay_and_Reference_Divisor;/* 1 dword */
						/* Register 0x61210 */
	struct mrst_timing_info DTD;/*18 bytes, Standard definition */
	u16 Panel_Backlight_Inverter_Descriptor;/* 16 bits, as follows */
				/* Bit 0, Frequency, 15 bits,0 - 32767Hz */
			/* Bit 15, Polarity, 1 bit, 0: Normal, 1: Inverted */
	u16 Panel_MIPI_Display_Descriptor;
			/*16 bits, Defined as follows: */
			/* if MIPI, 0x0000 if LVDS */
			/* Bit 0, Type, 2 bits, */
			/* 0: Type-1, */
			/* 1: Type-2, */
			/* 2: Type-3, */
			/* 3: Type-4 */
			/* Bit 2, Pixel Format, 4 bits */
			/* Bit0: 16bpp (not supported in LNC), */
			/* Bit1: 18bpp loosely packed, */
			/* Bit2: 18bpp packed, */
			/* Bit3: 24bpp */
			/* Bit 6, Reserved, 2 bits, 00b */
			/* Bit 8, Minimum Supported Frame Rate, 6 bits, 0 - 63Hz */
			/* Bit 14, Reserved, 2 bits, 00b */
} __packed;

struct mrst_panel_descriptor_v2 {
	u32 Panel_Port_Control; /* 1 dword, Register 0x61180 if LVDS */
				/* 0x61190 if MIPI */
	u32 Panel_Power_On_Sequencing;/*1 dword,Register 0x61208,*/
	u32 Panel_Power_Off_Sequencing;/*1 dword,Register 0x6120C,*/
	u8 Panel_Power_Cycle_Delay_and_Reference_Divisor;/* 1 byte */
						/* Register 0x61210 */
	struct mrst_timing_info DTD;/*18 bytes, Standard definition */
	u16 Panel_Backlight_Inverter_Descriptor;/*16 bits, as follows*/
				/*Bit 0, Frequency, 16 bits, 0 - 32767Hz*/
	u8 Panel_Initial_Brightness;/* [7:0] 0 - 100% */
			/*Bit 7, Polarity, 1 bit,0: Normal, 1: Inverted*/
	u16 Panel_MIPI_Display_Descriptor;
			/*16 bits, Defined as follows: */
			/* if MIPI, 0x0000 if LVDS */
			/* Bit 0, Type, 2 bits, */
			/* 0: Type-1, */
			/* 1: Type-2, */
			/* 2: Type-3, */
			/* 3: Type-4 */
			/* Bit 2, Pixel Format, 4 bits */
			/* Bit0: 16bpp (not supported in LNC), */
			/* Bit1: 18bpp loosely packed, */
			/* Bit2: 18bpp packed, */
			/* Bit3: 24bpp */
			/* Bit 6, Reserved, 2 bits, 00b */
			/* Bit 8, Minimum Supported Frame Rate, 6 bits, 0 - 63Hz */
			/* Bit 14, Reserved, 2 bits, 00b */
} __packed;

union mrst_panel_rx {
	struct {
		u16 NumberOfLanes:2; /*Num of Lanes, 2 bits,0 = 1 lane,*/
			/* 1 = 2 lanes, 2 = 3 lanes, 3 = 4 lanes. */
		u16 MaxLaneFreq:3; /* 0: 100MHz, 1: 200MHz, 2: 300MHz, */
		/*3: 400MHz, 4: 500MHz, 5: 600MHz, 6: 700MHz, 7: 800MHz.*/
		u16 SupportedVideoTransferMode:2; /*0: Non-burst only */
					/* 1: Burst and non-burst */
					/* 2/3: Reserved */
		u16 HSClkBehavior:1; /*0: Continuous, 1: Non-continuous*/
		u16 DuoDisplaySupport:1; /*1 bit,0: No, 1: Yes*/
		u16 ECC_ChecksumCapabilities:1;/*1 bit,0: No, 1: Yes*/
		u16 BidirectionalCommunication:1;/*1 bit,0: No, 1: Yes */
		u16 Rsvd:5;/*5 bits,00000b */
	} panelrx;
	u16 panel_receiver;
} __packed;

struct mrst_gct_v1 {
	union { /*8 bits,Defined as follows: */
		struct {
			u8 PanelType:4; /*4 bits, Bit field for panels*/
					/* 0 - 3: 0 = LVDS, 1 = MIPI*/
					/*2 bits,Specifies which of the*/
			u8 BootPanelIndex:2;
					/* 4 panels to use by default*/
			u8 BootMIPI_DSI_RxIndex:2;/*Specifies which of*/
					/* the 4 MIPI DSI receivers to use*/
		} PD;
		u8 PanelDescriptor;
	};
	struct mrst_panel_descriptor_v1 panel[4];/*panel descrs,38 bytes each*/
	union mrst_panel_rx panelrx[4]; /* panel receivers*/
} __packed;

struct mrst_gct_v2 {
	union { /*8 bits,Defined as follows: */
		struct {
			u8 PanelType:4; /*4 bits, Bit field for panels*/
					/* 0 - 3: 0 = LVDS, 1 = MIPI*/
					/*2 bits,Specifies which of the*/
			u8 BootPanelIndex:2;
					/* 4 panels to use by default*/
			u8 BootMIPI_DSI_RxIndex:2;/*Specifies which of*/
					/* the 4 MIPI DSI receivers to use*/
		} PD;
		u8 PanelDescriptor;
	};
	struct mrst_panel_descriptor_v2 panel[4];/*panel descrs,38 bytes each*/
	union mrst_panel_rx panelrx[4]; /* panel receivers*/
} __packed;

struct mrst_gct_data {
	u8 bpi; /* boot panel index, number of panel used during boot */
	u8 pt; /* panel type, 4 bit field, 0=lvds, 1=mipi */
	struct mrst_timing_info DTD; /* timing info for the selected panel */
	u32 Panel_Port_Control;
	u32 PP_On_Sequencing;/*1 dword,Register 0x61208,*/
	u32 PP_Off_Sequencing;/*1 dword,Register 0x6120C,*/
	u32 PP_Cycle_Delay;
	u16 Panel_Backlight_Inverter_Descriptor;
	u16 Panel_MIPI_Display_Descriptor;
} __packed;

#define MODE_SETTING_IN_CRTC		0x1
#define MODE_SETTING_IN_ENCODER		0x2
#define MODE_SETTING_ON_GOING		0x3
#define MODE_SETTING_IN_DSR		0x4
#define MODE_SETTING_ENCODER_DONE	0x8

#define GCT_R10_HEADER_SIZE		16
#define GCT_R10_DISPLAY_DESC_SIZE	28

/*
 *	Moorestown HDMI interfaces
 */

struct mrst_hdmi_dev {
	struct pci_dev *dev;
	void __iomem *regs;
	unsigned int mmio, mmio_len;
	int dpms_mode;
	struct hdmi_i2c_dev *i2c_dev;

	/* register state */
	u32 saveDPLL_CTRL;
	u32 saveDPLL_DIV_CTRL;
	u32 saveDPLL_ADJUST;
	u32 saveDPLL_UPDATE;
	u32 saveDPLL_CLK_ENABLE;
	u32 savePCH_HTOTAL_B;
	u32 savePCH_HBLANK_B;
	u32 savePCH_HSYNC_B;
	u32 savePCH_VTOTAL_B;
	u32 savePCH_VBLANK_B;
	u32 savePCH_VSYNC_B;
	u32 savePCH_PIPEBCONF;
	u32 savePCH_PIPEBSRC;
};

extern void mrst_hdmi_setup(struct drm_device *dev);
extern void mrst_hdmi_teardown(struct drm_device *dev);
extern int  mrst_hdmi_i2c_init(struct pci_dev *dev);
extern void mrst_hdmi_i2c_exit(struct pci_dev *dev);
extern void mrst_hdmi_save(struct drm_device *dev);
extern void mrst_hdmi_restore(struct drm_device *dev);
extern void mrst_hdmi_init(struct drm_device *dev, struct psb_intel_mode_device *mode_dev);
