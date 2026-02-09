/* SPDX-License-Identifier: MIT */
#ifndef __NVBIOS_CONN_H__
#define __NVBIOS_CONN_H__

/*
 * An enumerator representing all of the possible VBIOS connector types defined
 * by Nvidia at
 * https://nvidia.github.io/open-gpu-doc/DCB/DCB-4.x-Specification.html.
 *
 * [1] Nvidia's documentation actually claims DCB_CONNECTOR_HDMI_0 is a "3-Pin
 *     DIN Stereo Connector". This seems very likely to be a documentation typo
 *     or some sort of funny historical baggage, because we've treated this
 *     connector type as HDMI for years without issue.
 *     TODO: Check with Nvidia what's actually happening here.
 */
enum dcb_connector_type {
	/* Analog outputs */
	DCB_CONNECTOR_VGA = 0x00,		// VGA 15-pin connector
	DCB_CONNECTOR_DVI_A = 0x01,		// DVI-A
	DCB_CONNECTOR_POD_VGA = 0x02,		// Pod - VGA 15-pin connector
	DCB_CONNECTOR_TV_0 = 0x10,		// TV - Composite Out
	DCB_CONNECTOR_TV_1 = 0x11,		// TV - S-Video Out
	DCB_CONNECTOR_TV_2 = 0x12,		// TV - S-Video Breakout - Composite
	DCB_CONNECTOR_TV_3 = 0x13,		// HDTV Component - YPrPb
	DCB_CONNECTOR_TV_SCART = 0x14,		// TV - SCART Connector
	DCB_CONNECTOR_TV_SCART_D = 0x16,	// TV - Composite SCART over D-connector
	DCB_CONNECTOR_TV_DTERM = 0x17,		// HDTV - D-connector (EIAJ4120)
	DCB_CONNECTOR_POD_TV_3 = 0x18,		// Pod - HDTV - YPrPb
	DCB_CONNECTOR_POD_TV_1 = 0x19,		// Pod - S-Video
	DCB_CONNECTOR_POD_TV_0 = 0x1a,		// Pod - Composite

	/* DVI digital outputs */
	DCB_CONNECTOR_DVI_I_TV_1 = 0x20,	// DVI-I-TV-S-Video
	DCB_CONNECTOR_DVI_I_TV_0 = 0x21,	// DVI-I-TV-Composite
	DCB_CONNECTOR_DVI_I_TV_2 = 0x22,	// DVI-I-TV-S-Video Breakout-Composite
	DCB_CONNECTOR_DVI_I = 0x30,		// DVI-I
	DCB_CONNECTOR_DVI_D = 0x31,		// DVI-D
	DCB_CONNECTOR_DVI_ADC = 0x32,		// Apple Display Connector (ADC)
	DCB_CONNECTOR_DMS59_0 = 0x38,		// LFH-DVI-I-1
	DCB_CONNECTOR_DMS59_1 = 0x39,		// LFH-DVI-I-2
	DCB_CONNECTOR_BNC = 0x3c,		// BNC Connector [for SDI?]

	/* LVDS / TMDS digital outputs */
	DCB_CONNECTOR_LVDS = 0x40,		// LVDS-SPWG-Attached [is this name correct?]
	DCB_CONNECTOR_LVDS_SPWG = 0x41,		// LVDS-OEM-Attached (non-removable)
	DCB_CONNECTOR_LVDS_REM = 0x42,		// LVDS-SPWG-Detached [following naming above]
	DCB_CONNECTOR_LVDS_SPWG_REM = 0x43,	// LVDS-OEM-Detached (removable)
	DCB_CONNECTOR_TMDS = 0x45,		// TMDS-OEM-Attached (non-removable)

	/* DP digital outputs */
	DCB_CONNECTOR_DP = 0x46,		// DisplayPort External Connector
	DCB_CONNECTOR_eDP = 0x47,		// DisplayPort Internal Connector
	DCB_CONNECTOR_mDP = 0x48,		// DisplayPort (Mini) External Connector

	/* Dock outputs (not used) */
	DCB_CONNECTOR_DOCK_VGA_0 = 0x50,	// VGA 15-pin if not docked
	DCB_CONNECTOR_DOCK_VGA_1 = 0x51,	// VGA 15-pin if docked
	DCB_CONNECTOR_DOCK_DVI_I_0 = 0x52,	// DVI-I if not docked
	DCB_CONNECTOR_DOCK_DVI_I_1 = 0x53,	// DVI-I if docked
	DCB_CONNECTOR_DOCK_DVI_D_0 = 0x54,	// DVI-D if not docked
	DCB_CONNECTOR_DOCK_DVI_D_1 = 0x55,	// DVI-D if docked
	DCB_CONNECTOR_DOCK_DP_0 = 0x56,		// DisplayPort if not docked
	DCB_CONNECTOR_DOCK_DP_1 = 0x57,		// DisplayPort if docked
	DCB_CONNECTOR_DOCK_mDP_0 = 0x58,	// DisplayPort (Mini) if not docked
	DCB_CONNECTOR_DOCK_mDP_1 = 0x59,	// DisplayPort (Mini) if docked

	/* HDMI? digital outputs */
	DCB_CONNECTOR_HDMI_0 = 0x60,		// HDMI? See [1] in top-level enum comment above
	DCB_CONNECTOR_HDMI_1 = 0x61,		// HDMI-A connector
	DCB_CONNECTOR_SPDIF = 0x62,		// Audio S/PDIF connector
	DCB_CONNECTOR_HDMI_C = 0x63,		// HDMI-C (Mini) connector

	/* Misc. digital outputs */
	DCB_CONNECTOR_DMS59_DP0 = 0x64,		// LFH-DP-1
	DCB_CONNECTOR_DMS59_DP1 = 0x65,		// LFH-DP-2
	DCB_CONNECTOR_WFD = 0x70,		// Virtual connector for Wifi Display (WFD)
	DCB_CONNECTOR_USB_C = 0x71,		// [DP over USB-C; not present in docs]
	DCB_CONNECTOR_NONE = 0xff		// Skip Entry
};

struct nvbios_connT {
};

u32 nvbios_connTe(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u32 nvbios_connTp(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		  struct nvbios_connT *info);

struct nvbios_connE {
	u8 type;
	u8 location;
	u8 hpd;
	u8 dp;
	u8 di;
	u8 sr;
	u8 lcdid;
};

u32 nvbios_connEe(struct nvkm_bios *bios, u8 idx, u8 *ver, u8 *hdr);
u32 nvbios_connEp(struct nvkm_bios *bios, u8 idx, u8 *ver, u8 *hdr,
		  struct nvbios_connE *info);
#endif
