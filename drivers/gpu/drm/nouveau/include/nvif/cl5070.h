#ifndef __NVIF_CL5070_H__
#define __NVIF_CL5070_H__

#define NV50_DISP_MTHD                                                     0x00

struct nv50_disp_mthd_v0 {
	__u8  version;
#define NV50_DISP_SCANOUTPOS                                               0x00
	__u8  method;
	__u8  head;
	__u8  pad03[5];
};

struct nv50_disp_scanoutpos_v0 {
	__u8  version;
	__u8  pad01[7];
	__s64 time[2];
	__u16 vblanks;
	__u16 vblanke;
	__u16 vtotal;
	__u16 vline;
	__u16 hblanks;
	__u16 hblanke;
	__u16 htotal;
	__u16 hline;
};

struct nv50_disp_mthd_v1 {
	__u8  version;
#define NV50_DISP_MTHD_V1_DAC_PWR                                          0x10
#define NV50_DISP_MTHD_V1_DAC_LOAD                                         0x11
#define NV50_DISP_MTHD_V1_SOR_PWR                                          0x20
#define NV50_DISP_MTHD_V1_SOR_HDA_ELD                                      0x21
#define NV50_DISP_MTHD_V1_SOR_HDMI_PWR                                     0x22
#define NV50_DISP_MTHD_V1_SOR_LVDS_SCRIPT                                  0x23
#define NV50_DISP_MTHD_V1_SOR_DP_PWR                                       0x24
#define NV50_DISP_MTHD_V1_PIOR_PWR                                         0x30
	__u8  method;
	__u16 hasht;
	__u16 hashm;
	__u8  pad06[2];
};

struct nv50_disp_dac_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  data;
	__u8  vsync;
	__u8  hsync;
	__u8  pad05[3];
};

struct nv50_disp_dac_load_v0 {
	__u8  version;
	__u8  load;
	__u8  pad02[2];
	__u32 data;
};

struct nv50_disp_sor_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  pad02[6];
};

struct nv50_disp_sor_hda_eld_v0 {
	__u8  version;
	__u8  pad01[7];
	__u8  data[];
};

struct nv50_disp_sor_hdmi_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  max_ac_packet;
	__u8  rekey;
	__u8  pad04[4];
};

struct nv50_disp_sor_lvds_script_v0 {
	__u8  version;
	__u8  pad01[1];
	__u16 script;
	__u8  pad04[4];
};

struct nv50_disp_sor_dp_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  pad02[6];
};

struct nv50_disp_pior_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  type;
	__u8  pad03[5];
};
#endif
