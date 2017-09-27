#ifndef __NVIF_CL9097_H__
#define __NVIF_CL9097_H__

#define FERMI_A_ZBC_COLOR                                                  0x00
#define FERMI_A_ZBC_DEPTH                                                  0x01

struct fermi_a_zbc_color_v0 {
	__u8  version;
#define FERMI_A_ZBC_COLOR_V0_FMT_ZERO                                      0x01
#define FERMI_A_ZBC_COLOR_V0_FMT_UNORM_ONE                                 0x02
#define FERMI_A_ZBC_COLOR_V0_FMT_RF32_GF32_BF32_AF32                       0x04
#define FERMI_A_ZBC_COLOR_V0_FMT_R16_G16_B16_A16                           0x08
#define FERMI_A_ZBC_COLOR_V0_FMT_RN16_GN16_BN16_AN16                       0x0c
#define FERMI_A_ZBC_COLOR_V0_FMT_RS16_GS16_BS16_AS16                       0x10
#define FERMI_A_ZBC_COLOR_V0_FMT_RU16_GU16_BU16_AU16                       0x14
#define FERMI_A_ZBC_COLOR_V0_FMT_RF16_GF16_BF16_AF16                       0x16
#define FERMI_A_ZBC_COLOR_V0_FMT_A8R8G8B8                                  0x18
#define FERMI_A_ZBC_COLOR_V0_FMT_A8RL8GL8BL8                               0x1c
#define FERMI_A_ZBC_COLOR_V0_FMT_A2B10G10R10                               0x20
#define FERMI_A_ZBC_COLOR_V0_FMT_AU2BU10GU10RU10                           0x24
#define FERMI_A_ZBC_COLOR_V0_FMT_A8B8G8R8                                  0x28
#define FERMI_A_ZBC_COLOR_V0_FMT_A8BL8GL8RL8                               0x2c
#define FERMI_A_ZBC_COLOR_V0_FMT_AN8BN8GN8RN8                              0x30
#define FERMI_A_ZBC_COLOR_V0_FMT_AS8BS8GS8RS8                              0x34
#define FERMI_A_ZBC_COLOR_V0_FMT_AU8BU8GU8RU8                              0x38
#define FERMI_A_ZBC_COLOR_V0_FMT_A2R10G10B10                               0x3c
#define FERMI_A_ZBC_COLOR_V0_FMT_BF10GF11RF11                              0x40
	__u8  format;
	__u8  index;
	__u8  pad03[5];
	__u32 ds[4];
	__u32 l2[4];
};

struct fermi_a_zbc_depth_v0 {
	__u8  version;
#define FERMI_A_ZBC_DEPTH_V0_FMT_FP32                                      0x01
	__u8  format;
	__u8  index;
	__u8  pad03[5];
	__u32 ds;
	__u32 l2;
};
#endif
