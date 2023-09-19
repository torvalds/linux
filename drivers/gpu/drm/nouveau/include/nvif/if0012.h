/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0012_H__
#define __NVIF_IF0012_H__

#include <drm/display/drm_dp.h>

union nvif_outp_args {
	struct nvif_outp_v0 {
		__u8 version;
		__u8 id;	/* DCB device index. */
		__u8 pad02[6];
	} v0;
};

#define NVIF_OUTP_V0_DETECT        0x00
#define NVIF_OUTP_V0_EDID_GET      0x01

#define NVIF_OUTP_V0_INHERIT       0x10
#define NVIF_OUTP_V0_ACQUIRE       0x11
#define NVIF_OUTP_V0_RELEASE       0x12

#define NVIF_OUTP_V0_LOAD_DETECT   0x20

#define NVIF_OUTP_V0_BL_GET        0x30
#define NVIF_OUTP_V0_BL_SET        0x31

#define NVIF_OUTP_V0_LVDS          0x40

#define NVIF_OUTP_V0_HDMI          0x50

#define NVIF_OUTP_V0_INFOFRAME     0x60
#define NVIF_OUTP_V0_HDA_ELD       0x61

#define NVIF_OUTP_V0_DP_AUX_PWR    0x70
#define NVIF_OUTP_V0_DP_AUX_XFER   0x71
#define NVIF_OUTP_V0_DP_RATES      0x72
#define NVIF_OUTP_V0_DP_RETRAIN    0x73
#define NVIF_OUTP_V0_DP_MST_VCPI   0x78

union nvif_outp_detect_args {
	struct nvif_outp_detect_v0 {
		__u8 version;
#define NVIF_OUTP_DETECT_V0_NOT_PRESENT 0x00
#define NVIF_OUTP_DETECT_V0_PRESENT     0x01
#define NVIF_OUTP_DETECT_V0_UNKNOWN     0x02
		__u8 status;
	} v0;
};

union nvif_outp_edid_get_args {
	struct nvif_outp_edid_get_v0 {
		__u8  version;
		__u8  pad01;
		__u16 size;
		__u8  data[2048];
	} v0;
};

union nvif_outp_load_detect_args {
	struct nvif_outp_load_detect_v0 {
		__u8  version;
		__u8  load;
		__u8  pad02[2];
		__u32 data; /*TODO: move vbios loadval parsing into nvkm */
	} v0;
};

union nvif_outp_acquire_args {
	struct nvif_outp_acquire_v0 {
		__u8 version;
#define NVIF_OUTP_ACQUIRE_V0_DAC  0x00
#define NVIF_OUTP_ACQUIRE_V0_SOR  0x01
#define NVIF_OUTP_ACQUIRE_V0_PIOR 0x02
#define NVIF_OUTP_ACQUIRE_V0_DP      0x04
		__u8 type;
		__u8 or;
		__u8 link;
		__u8 pad04[4];
		union {
			struct {
				__u8 hda;
			} sor;
			struct {
				__u8 link_nr; /* 0 = highest possible. */
				__u8 link_bw; /* 0 = highest possible, DP BW code otherwise. */
				__u8 hda;
				__u8 mst;
				__u8 pad04[4];
				__u8 dpcd[DP_RECEIVER_CAP_SIZE];
			} dp;
		};
	} v0;
};

union nvif_outp_inherit_args {
	struct nvif_outp_inherit_v0 {
		__u8 version;
#define NVIF_OUTP_INHERIT_V0_RGB_CRT 0x00
#define NVIF_OUTP_INHERIT_V0_TV      0x01
#define NVIF_OUTP_INHERIT_V0_TMDS    0x02
#define NVIF_OUTP_INHERIT_V0_LVDS    0x03
#define NVIF_OUTP_INHERIT_V0_DP      0x04
		// In/out. Input is one of the above values, output is the actual hw protocol
		__u8 proto;
		__u8 or;
		__u8 link;
		__u8 head;
		union {
			struct {
				// TODO: Figure out padding, and whether we even want this field
				__u8 hda;
			} tmds;
		};
	} v0;
};

union nvif_outp_release_args {
	struct nvif_outp_release_vn {
	} vn;
};

union nvif_outp_bl_get_args {
	struct nvif_outp_bl_get_v0 {
		__u8  version;
		__u8  level;
	} v0;
};

union nvif_outp_bl_set_args {
	struct nvif_outp_bl_set_v0 {
		__u8  version;
		__u8  level;
	} v0;
};

union nvif_outp_lvds_args {
	struct nvif_outp_lvds_v0 {
		__u8  version;
		__u8  dual;
		__u8  bpc8;
	} v0;
};

union nvif_outp_hdmi_args {
	struct nvif_outp_hdmi_v0 {
		__u8 version;
		__u8 head;
		__u8 enable;
		__u8 max_ac_packet;
		__u8 rekey;
		__u8 scdc;
		__u8 scdc_scrambling;
		__u8 scdc_low_rates;
		__u32 khz;
	} v0;
};

union nvif_outp_infoframe_args {
	struct nvif_outp_infoframe_v0 {
		__u8 version;
#define NVIF_OUTP_INFOFRAME_V0_AVI 0
#define NVIF_OUTP_INFOFRAME_V0_VSI 1
		__u8 type;
		__u8 head;
		__u8 pad03[5];
		__u8 data[];
	} v0;
};

union nvif_outp_hda_eld_args {
	struct nvif_outp_hda_eld_v0 {
		__u8  version;
		__u8  head;
		__u8  pad02[6];
		__u8  data[];
	} v0;
};

union nvif_outp_dp_aux_pwr_args {
	struct nvif_outp_dp_aux_pwr_v0 {
		__u8 version;
		__u8 state;
		__u8 pad02[6];
	} v0;
};

union nvif_outp_dp_aux_xfer_args {
	struct nvif_outp_dp_aux_xfer_v0 {
		__u8  version;
		__u8  pad01;
		__u8  type;
		__u8  size;
		__u32 addr;
		__u8  data[16];
	} v0;
};

union nvif_outp_dp_rates_args {
	struct nvif_outp_dp_rates_v0 {
		__u8  version;
		__u8  pad01[6];
		__u8  rates;
		struct {
			__s8  dpcd;
			__u32 rate;
		} rate[8];
	} v0;
};

union nvif_outp_dp_retrain_args {
	struct nvif_outp_dp_retrain_vn {
	} vn;
};

union nvif_outp_dp_mst_vcpi_args {
	struct nvif_outp_dp_mst_vcpi_v0 {
		__u8  version;
		__u8  head;
		__u8  start_slot;
		__u8  num_slots;
		__u16 pbn;
		__u16 aligned_pbn;
	} v0;
};
#endif
