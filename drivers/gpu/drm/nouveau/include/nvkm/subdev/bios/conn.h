#ifndef __NVBIOS_CONN_H__
#define __NVBIOS_CONN_H__
enum dcb_connector_type {
	DCB_CONNECTOR_VGA = 0x00,
	DCB_CONNECTOR_TV_0 = 0x10,
	DCB_CONNECTOR_TV_1 = 0x11,
	DCB_CONNECTOR_TV_3 = 0x13,
	DCB_CONNECTOR_DVI_I = 0x30,
	DCB_CONNECTOR_DVI_D = 0x31,
	DCB_CONNECTOR_DMS59_0 = 0x38,
	DCB_CONNECTOR_DMS59_1 = 0x39,
	DCB_CONNECTOR_LVDS = 0x40,
	DCB_CONNECTOR_LVDS_SPWG = 0x41,
	DCB_CONNECTOR_DP = 0x46,
	DCB_CONNECTOR_eDP = 0x47,
	DCB_CONNECTOR_HDMI_0 = 0x60,
	DCB_CONNECTOR_HDMI_1 = 0x61,
	DCB_CONNECTOR_HDMI_C = 0x63,
	DCB_CONNECTOR_DMS59_DP0 = 0x64,
	DCB_CONNECTOR_DMS59_DP1 = 0x65,
	DCB_CONNECTOR_NONE = 0xff
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
