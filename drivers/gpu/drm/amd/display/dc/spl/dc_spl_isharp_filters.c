// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "spl_debug.h"
#include "dc_spl_filters.h"
#include "dc_spl_isharp_filters.h"

//========================================
// Delta Gain 1DLUT
// LUT content is packed as 4-bytes into one DWORD/entry
// A_start = 0.000000
// A_end   = 10.000000
// A_gain  = 2.000000
// B_start = 11.000000
// B_end   = 86.000000
// C_start = 40.000000
// C_end   = 64.000000
//========================================
static const uint32_t filter_isharp_1D_lut_0[ISHARP_LUT_TABLE_SIZE] = {
0x02010000,
0x0A070503,
0x1614100D,
0x1C1B1918,
0x22211F1E,
0x27262423,
0x2A2A2928,
0x2D2D2C2B,
0x302F2F2E,
0x31313030,
0x31313131,
0x31313131,
0x30303031,
0x292D2F2F,
0x191D2125,
0x050A0F14,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
0x00000000,
};
//========================================
// Delta Gain 1DLUT
// LUT content is packed as 4-bytes into one DWORD/entry
// A_start = 0.000000
// A_end   = 10.000000
// A_gain  = 0.500000
// B_start = 11.000000
// B_end   = 127.000000
// C_start = 96.000000
// C_end   = 127.000000
//========================================

static const uint32_t filter_isharp_1D_lut_0p5x[ISHARP_LUT_TABLE_SIZE] = {
0x00000000,
0x02020101,
0x06050403,
0x07070606,
0x09080808,
0x0A0A0A09,
0x0C0B0B0B,
0x0D0D0C0C,
0x0E0E0D0D,
0x0F0F0E0E,
0x100F0F0F,
0x10101010,
0x11111010,
0x11111111,
0x11111111,
0x11111111,
0x11111111,
0x11111111,
0x11111111,
0x10101111,
0x10101010,
0x0F0F0F10,
0x0E0E0F0F,
0x0D0D0E0E,
0x0C0C0D0D,
0x0B0B0B0C,
0x090A0A0A,
0x08080809,
0x06060707,
0x04050506,
0x02030304,
0x00010102,
};
//========================================
// Delta Gain 1DLUT
// LUT content is packed as 4-bytes into one DWORD/entry
// A_start = 0.000000
// A_end   = 10.000000
// A_gain  = 1.000000
// B_start = 11.000000
// B_end   = 127.000000
// C_start = 96.000000
// C_end   = 127.000000
//========================================
static const uint32_t filter_isharp_1D_lut_1p0x[ISHARP_LUT_TABLE_SIZE] = {
0x01000000,
0x05040302,
0x0B0A0806,
0x0E0E0D0C,
0x1211100F,
0x15141312,
0x17171615,
0x1A191918,
0x1C1B1B1A,
0x1E1D1D1C,
0x1F1F1E1E,
0x2020201F,
0x21212121,
0x22222222,
0x23232222,
0x23232323,
0x23232323,
0x22222323,
0x22222222,
0x21212121,
0x1F202020,
0x1E1E1F1F,
0x1C1D1D1E,
0x1A1B1B1C,
0x1819191A,
0x15161717,
0x12131415,
0x0F101112,
0x0C0D0E0E,
0x08090A0B,
0x04050607,
0x00010203,
};
//========================================
// Delta Gain 1DLUT
// LUT content is packed as 4-bytes into one DWORD/entry
// A_start = 0.000000
// A_end   = 10.000000
// A_gain  = 1.500000
// B_start = 11.000000
// B_end   = 127.000000
// C_start = 96.000000
// C_end   = 127.000000
//========================================
static const uint32_t filter_isharp_1D_lut_1p5x[ISHARP_LUT_TABLE_SIZE] = {
0x01010000,
0x07050402,
0x110F0C0A,
0x16141312,
0x1B191817,
0x1F1E1D1C,
0x23222120,
0x26262524,
0x2A292827,
0x2C2C2B2A,
0x2F2E2E2D,
0x3130302F,
0x32323131,
0x33333332,
0x34343433,
0x34343434,
0x34343434,
0x33343434,
0x32333333,
0x31313232,
0x2F303031,
0x2D2E2E2F,
0x2A2B2C2C,
0x2728292A,
0x24252626,
0x20212223,
0x1C1D1E1F,
0x1718191B,
0x12131416,
0x0C0E0F10,
0x0608090B,
0x00020305
};
//========================================
// Delta Gain 1DLUT
// LUT content is packed as 4-bytes into one DWORD/entry
// A_start = 0.000000
// A_end   = 10.000000
// A_gain  = 2.000000
// B_start = 11.000000
// B_end   = 127.000000
// C_start = 40.000000
// C_end   = 127.000000
//========================================
static const uint32_t filter_isharp_1D_lut_2p0x[ISHARP_LUT_TABLE_SIZE] = {
0x02010000,
0x0A070503,
0x1614100D,
0x1D1B1A18,
0x2322201F,
0x29282625,
0x2F2D2C2B,
0x33323130,
0x38373534,
0x3B3A3938,
0x3E3E3D3C,
0x4140403F,
0x43424241,
0x44444443,
0x45454545,
0x46454545,
0x45454546,
0x45454545,
0x43444444,
0x41424243,
0x3F404041,
0x3C3D3E3E,
0x38393A3B,
0x34353738,
0x30313233,
0x2B2C2D2F,
0x25262829,
0x1F202223,
0x181A1B1D,
0x10121416,
0x080B0D0E,
0x00020406,
};
//========================================
// Delta Gain 1DLUT
// LUT content is packed as 4-bytes into one DWORD/entry
// A_start = 0.000000
// A_end   = 10.000000
// A_gain  = 3.000000
// B_start = 11.000000
// B_end   = 127.000000
// C_start = 40.000000
// C_end   = 127.000000
//========================================
static const uint32_t filter_isharp_1D_lut_3p0x[ISHARP_LUT_TABLE_SIZE] = {
0x03010000,
0x0F0B0805,
0x211E1813,
0x2B292624,
0x3533302E,
0x3E3C3A37,
0x46444240,
0x4D4B4A48,
0x5352504F,
0x59575655,
0x5D5C5B5A,
0x61605F5E,
0x64646362,
0x66666565,
0x68686767,
0x68686868,
0x68686868,
0x67676868,
0x65656666,
0x62636464,
0x5E5F6061,
0x5A5B5C5D,
0x55565759,
0x4F505253,
0x484A4B4D,
0x40424446,
0x373A3C3E,
0x2E303335,
0x2426292B,
0x191B1E21,
0x0D101316,
0x0003060A,
};

//========================================
// Wide scaler coefficients
//========================================================
// <using>			gen_scaler_coeffs.m
// <date>			15-Dec-2021
// <coeffDescrip>	6t_64p_LanczosEd_p_1_p_10qb_
// <num_taps>		6
// <num_phases>		64
// <CoefType>		LanczosEd
// <CoefQuant>		S1.10
//========================================================
static const uint16_t filter_isharp_wide_6tap_64p[198] = {
0x0000, 0x0000, 0x0400, 0x0000, 0x0000, 0x0000,
0x0003, 0x0FF3, 0x0400, 0x000D, 0x0FFD, 0x0000,
0x0006, 0x0FE7, 0x03FE, 0x001C, 0x0FF9, 0x0000,
0x0009, 0x0FDB, 0x03FC, 0x002B, 0x0FF5, 0x0000,
0x000C, 0x0FD0, 0x03F9, 0x003A, 0x0FF1, 0x0000,
0x000E, 0x0FC5, 0x03F5, 0x004A, 0x0FED, 0x0001,
0x0011, 0x0FBB, 0x03F0, 0x005A, 0x0FE9, 0x0001,
0x0013, 0x0FB2, 0x03EB, 0x006A, 0x0FE5, 0x0001,
0x0015, 0x0FA9, 0x03E4, 0x007B, 0x0FE1, 0x0002,
0x0017, 0x0FA1, 0x03DD, 0x008D, 0x0FDC, 0x0002,
0x0018, 0x0F99, 0x03D4, 0x00A0, 0x0FD8, 0x0003,
0x001A, 0x0F92, 0x03CB, 0x00B2, 0x0FD3, 0x0004,
0x001B, 0x0F8C, 0x03C1, 0x00C6, 0x0FCE, 0x0004,
0x001C, 0x0F86, 0x03B7, 0x00D9, 0x0FC9, 0x0005,
0x001D, 0x0F80, 0x03AB, 0x00EE, 0x0FC4, 0x0006,
0x001E, 0x0F7C, 0x039F, 0x0101, 0x0FBF, 0x0007,
0x001F, 0x0F78, 0x0392, 0x0115, 0x0FBA, 0x0008,
0x001F, 0x0F74, 0x0385, 0x012B, 0x0FB5, 0x0008,
0x0020, 0x0F71, 0x0376, 0x0140, 0x0FB0, 0x0009,
0x0020, 0x0F6E, 0x0367, 0x0155, 0x0FAB, 0x000B,
0x0020, 0x0F6C, 0x0357, 0x016B, 0x0FA6, 0x000C,
0x0020, 0x0F6A, 0x0347, 0x0180, 0x0FA2, 0x000D,
0x0020, 0x0F69, 0x0336, 0x0196, 0x0F9D, 0x000E,
0x0020, 0x0F69, 0x0325, 0x01AB, 0x0F98, 0x000F,
0x001F, 0x0F68, 0x0313, 0x01C3, 0x0F93, 0x0010,
0x001F, 0x0F69, 0x0300, 0x01D8, 0x0F8F, 0x0011,
0x001E, 0x0F69, 0x02ED, 0x01EF, 0x0F8B, 0x0012,
0x001D, 0x0F6A, 0x02D9, 0x0205, 0x0F87, 0x0014,
0x001D, 0x0F6C, 0x02C5, 0x021A, 0x0F83, 0x0015,
0x001C, 0x0F6E, 0x02B1, 0x0230, 0x0F7F, 0x0016,
0x001B, 0x0F70, 0x029C, 0x0247, 0x0F7B, 0x0017,
0x001A, 0x0F72, 0x0287, 0x025D, 0x0F78, 0x0018,
0x0019, 0x0F75, 0x0272, 0x0272, 0x0F75, 0x0019
};
// Blur and scale coefficients
//========================================================
// <using>			gen_BlurScale_coeffs.m
// <date>			25-Apr-2022
// <num_taps>		4
// <num_phases>		64
// <CoefType>		Blur & Scale LPF
// <CoefQuant>		S1.10
//========================================================
static const uint16_t filter_isharp_bs_4tap_in_6_64p[198] = {
0x0000, 0x00E5, 0x0237, 0x00E4, 0x0000, 0x0000,
0x0000, 0x00DE, 0x0237, 0x00EB, 0x0000, 0x0000,
0x0000, 0x00D7, 0x0236, 0x00F2, 0x0001, 0x0000,
0x0000, 0x00D0, 0x0235, 0x00FA, 0x0001, 0x0000,
0x0000, 0x00C9, 0x0234, 0x0101, 0x0002, 0x0000,
0x0000, 0x00C2, 0x0233, 0x0108, 0x0003, 0x0000,
0x0000, 0x00BB, 0x0232, 0x0110, 0x0003, 0x0000,
0x0000, 0x00B5, 0x0230, 0x0117, 0x0004, 0x0000,
0x0000, 0x00AE, 0x022E, 0x011F, 0x0005, 0x0000,
0x0000, 0x00A8, 0x022C, 0x0126, 0x0006, 0x0000,
0x0000, 0x00A2, 0x022A, 0x012D, 0x0007, 0x0000,
0x0000, 0x009C, 0x0228, 0x0134, 0x0008, 0x0000,
0x0000, 0x0096, 0x0225, 0x013C, 0x0009, 0x0000,
0x0000, 0x0090, 0x0222, 0x0143, 0x000B, 0x0000,
0x0000, 0x008A, 0x021F, 0x014B, 0x000C, 0x0000,
0x0000, 0x0085, 0x021C, 0x0151, 0x000E, 0x0000,
0x0000, 0x007F, 0x0218, 0x015A, 0x000F, 0x0000,
0x0000, 0x007A, 0x0215, 0x0160, 0x0011, 0x0000,
0x0000, 0x0074, 0x0211, 0x0168, 0x0013, 0x0000,
0x0000, 0x006F, 0x020D, 0x016F, 0x0015, 0x0000,
0x0000, 0x006A, 0x0209, 0x0176, 0x0017, 0x0000,
0x0000, 0x0065, 0x0204, 0x017E, 0x0019, 0x0000,
0x0000, 0x0060, 0x0200, 0x0185, 0x001B, 0x0000,
0x0000, 0x005C, 0x01FB, 0x018C, 0x001D, 0x0000,
0x0000, 0x0057, 0x01F6, 0x0193, 0x0020, 0x0000,
0x0000, 0x0053, 0x01F1, 0x019A, 0x0022, 0x0000,
0x0000, 0x004E, 0x01EC, 0x01A1, 0x0025, 0x0000,
0x0000, 0x004A, 0x01E6, 0x01A8, 0x0028, 0x0000,
0x0000, 0x0046, 0x01E1, 0x01AF, 0x002A, 0x0000,
0x0000, 0x0042, 0x01DB, 0x01B6, 0x002D, 0x0000,
0x0000, 0x003F, 0x01D5, 0x01BB, 0x0031, 0x0000,
0x0000, 0x003B, 0x01CF, 0x01C2, 0x0034, 0x0000,
0x0000, 0x0037, 0x01C9, 0x01C9, 0x0037, 0x0000
};
//========================================================
// <using>			gen_BlurScale_coeffs.m
// <date>			25-Apr-2022
// <num_taps>		4
// <num_phases>		64
// <CoefType>		Blur & Scale LPF
// <CoefQuant>		S1.10
//========================================================
static const uint16_t filter_isharp_bs_4tap_64p[132] = {
0x00E5, 0x0237, 0x00E4, 0x0000,
0x00DE, 0x0237, 0x00EB, 0x0000,
0x00D7, 0x0236, 0x00F2, 0x0001,
0x00D0, 0x0235, 0x00FA, 0x0001,
0x00C9, 0x0234, 0x0101, 0x0002,
0x00C2, 0x0233, 0x0108, 0x0003,
0x00BB, 0x0232, 0x0110, 0x0003,
0x00B5, 0x0230, 0x0117, 0x0004,
0x00AE, 0x022E, 0x011F, 0x0005,
0x00A8, 0x022C, 0x0126, 0x0006,
0x00A2, 0x022A, 0x012D, 0x0007,
0x009C, 0x0228, 0x0134, 0x0008,
0x0096, 0x0225, 0x013C, 0x0009,
0x0090, 0x0222, 0x0143, 0x000B,
0x008A, 0x021F, 0x014B, 0x000C,
0x0085, 0x021C, 0x0151, 0x000E,
0x007F, 0x0218, 0x015A, 0x000F,
0x007A, 0x0215, 0x0160, 0x0011,
0x0074, 0x0211, 0x0168, 0x0013,
0x006F, 0x020D, 0x016F, 0x0015,
0x006A, 0x0209, 0x0176, 0x0017,
0x0065, 0x0204, 0x017E, 0x0019,
0x0060, 0x0200, 0x0185, 0x001B,
0x005C, 0x01FB, 0x018C, 0x001D,
0x0057, 0x01F6, 0x0193, 0x0020,
0x0053, 0x01F1, 0x019A, 0x0022,
0x004E, 0x01EC, 0x01A1, 0x0025,
0x004A, 0x01E6, 0x01A8, 0x0028,
0x0046, 0x01E1, 0x01AF, 0x002A,
0x0042, 0x01DB, 0x01B6, 0x002D,
0x003F, 0x01D5, 0x01BB, 0x0031,
0x003B, 0x01CF, 0x01C2, 0x0034,
0x0037, 0x01C9, 0x01C9, 0x0037,
};
//========================================================
// <using>			gen_BlurScale_coeffs.m
// <date>			09-Jun-2022
// <num_taps>		3
// <num_phases>		64
// <CoefType>		Blur & Scale LPF
// <CoefQuant>		S1.10
//========================================================
static const uint16_t filter_isharp_bs_3tap_64p[99] = {
0x0200, 0x0200, 0x0000,
0x01F6, 0x0206, 0x0004,
0x01EC, 0x020B, 0x0009,
0x01E2, 0x0211, 0x000D,
0x01D8, 0x0216, 0x0012,
0x01CE, 0x021C, 0x0016,
0x01C4, 0x0221, 0x001B,
0x01BA, 0x0226, 0x0020,
0x01B0, 0x022A, 0x0026,
0x01A6, 0x022F, 0x002B,
0x019C, 0x0233, 0x0031,
0x0192, 0x0238, 0x0036,
0x0188, 0x023C, 0x003C,
0x017E, 0x0240, 0x0042,
0x0174, 0x0244, 0x0048,
0x016A, 0x0248, 0x004E,
0x0161, 0x024A, 0x0055,
0x0157, 0x024E, 0x005B,
0x014D, 0x0251, 0x0062,
0x0144, 0x0253, 0x0069,
0x013A, 0x0256, 0x0070,
0x0131, 0x0258, 0x0077,
0x0127, 0x025B, 0x007E,
0x011E, 0x025C, 0x0086,
0x0115, 0x025E, 0x008D,
0x010B, 0x0260, 0x0095,
0x0102, 0x0262, 0x009C,
0x00F9, 0x0263, 0x00A4,
0x00F0, 0x0264, 0x00AC,
0x00E7, 0x0265, 0x00B4,
0x00DF, 0x0264, 0x00BD,
0x00D6, 0x0265, 0x00C5,
0x00CD, 0x0266, 0x00CD,
};

/* Converted Blur & Scale coeff tables from S1.10 to S1.12 */
static uint16_t filter_isharp_bs_4tap_in_6_64p_s1_12[198];
static uint16_t filter_isharp_bs_4tap_64p_s1_12[132];
static uint16_t filter_isharp_bs_3tap_64p_s1_12[99];

/* Pre-generated 1DLUT for given setup and sharpness level */
struct isharp_1D_lut_pregen filter_isharp_1D_lut_pregen[NUM_SHARPNESS_SETUPS] = {
	{
		0, 0,
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
		}
	},
	{
		0, 0,
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
		}
	},
	{
		0, 0,
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
		}
	},
	{
		0, 0,
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
		}
	},
};

struct scale_ratio_to_sharpness_level_adj sharpness_level_adj[NUM_SHARPNESS_ADJ_LEVELS] = {
	{1125, 1000, 0},
	{11, 10, 1},
	{1075, 1000, 2},
	{105, 100, 3},
	{1025, 1000, 4},
	{1, 1, 5},
};

const uint32_t *spl_get_filter_isharp_1D_lut_0(void)
{
	return filter_isharp_1D_lut_0;
}
const uint32_t *spl_get_filter_isharp_1D_lut_0p5x(void)
{
	return filter_isharp_1D_lut_0p5x;
}
const uint32_t *spl_get_filter_isharp_1D_lut_1p0x(void)
{
	return filter_isharp_1D_lut_1p0x;
}
const uint32_t *spl_get_filter_isharp_1D_lut_1p5x(void)
{
	return filter_isharp_1D_lut_1p5x;
}
const uint32_t *spl_get_filter_isharp_1D_lut_2p0x(void)
{
	return filter_isharp_1D_lut_2p0x;
}
const uint32_t *spl_get_filter_isharp_1D_lut_3p0x(void)
{
	return filter_isharp_1D_lut_3p0x;
}
const uint16_t *spl_get_filter_isharp_wide_6tap_64p(void)
{
	return filter_isharp_wide_6tap_64p;
}
uint16_t *spl_get_filter_isharp_bs_4tap_in_6_64p(void)
{
	return filter_isharp_bs_4tap_in_6_64p_s1_12;
}
uint16_t *spl_get_filter_isharp_bs_4tap_64p(void)
{
	return filter_isharp_bs_4tap_64p_s1_12;
}
uint16_t *spl_get_filter_isharp_bs_3tap_64p(void)
{
	return filter_isharp_bs_3tap_64p_s1_12;
}

static unsigned int spl_calculate_sharpness_level_adj(struct spl_fixed31_32 ratio)
{
	int j;
	struct spl_fixed31_32 ratio_level;
	struct scale_ratio_to_sharpness_level_adj *lookup_ptr;
	unsigned int sharpness_level_down_adj;

	/*
	 * Adjust sharpness level based on current scaling ratio
	 *
	 * We have 5 discrete scaling ratios which we will use to adjust the
	 *  sharpness level down by 1 as we pass each ratio.  The ratios
	 *  are
	 *
	 * 1.125 upscale and higher - no adj
	 * 1.100 - under 1.125 - adj level down 1
	 * 1.075 - under 1.100 - adj level down 2
	 * 1.050 - under 1.075 - adj level down 3
	 * 1.025 - under 1.050 - adj level down 4
	 * 1.000 - under 1.025 - adj level down 5
	 *
	 */
	j = 0;
	sharpness_level_down_adj = 0;
	lookup_ptr = sharpness_level_adj;
	while (j < NUM_SHARPNESS_ADJ_LEVELS) {
		ratio_level = spl_fixpt_from_fraction(lookup_ptr->ratio_numer,
			lookup_ptr->ratio_denom);
		if (ratio.value >= ratio_level.value) {
			sharpness_level_down_adj = lookup_ptr->level_down_adj;
			break;
		}
		lookup_ptr++;
		j++;
	}
	return sharpness_level_down_adj;
}

static unsigned int spl_calculate_sharpness_level(struct spl_fixed31_32 ratio,
		int discrete_sharpness_level, enum system_setup setup,
		struct spl_sharpness_range sharpness_range,
		enum scale_to_sharpness_policy scale_to_sharpness_policy)
{
	unsigned int sharpness_level = 0;
	unsigned int sharpness_level_down_adj = 0;

	int min_sharpness, max_sharpness, mid_sharpness;

	/*
	 * Adjust sharpness level if policy requires we adjust it based on
	 *  scale ratio.  Based on scale ratio, we may adjust the sharpness
	 *  level down by a certain number of steps.  We will not select
	 *  a sharpness value of 0 so the lowest sharpness level will be
	 *  0 or 1 depending on what the min_sharpness is
	 *
	 * If the policy is no required, this code maybe removed at a later
	 *  date
	 */
	switch (setup) {

	case HDR_L:
		min_sharpness = sharpness_range.hdr_rgb_min;
		max_sharpness = sharpness_range.hdr_rgb_max;
		mid_sharpness = sharpness_range.hdr_rgb_mid;
		if (scale_to_sharpness_policy == SCALE_TO_SHARPNESS_ADJ_ALL)
			sharpness_level_down_adj = spl_calculate_sharpness_level_adj(ratio);
		break;
	case HDR_NL:
		/* currently no use case, use Non-linear SDR values for now */
	case SDR_NL:
		min_sharpness = sharpness_range.sdr_yuv_min;
		max_sharpness = sharpness_range.sdr_yuv_max;
		mid_sharpness = sharpness_range.sdr_yuv_mid;
		if (scale_to_sharpness_policy >= SCALE_TO_SHARPNESS_ADJ_YUV)
			sharpness_level_down_adj = spl_calculate_sharpness_level_adj(ratio);
		break;
	case SDR_L:
	default:
		min_sharpness = sharpness_range.sdr_rgb_min;
		max_sharpness = sharpness_range.sdr_rgb_max;
		mid_sharpness = sharpness_range.sdr_rgb_mid;
		if (scale_to_sharpness_policy == SCALE_TO_SHARPNESS_ADJ_ALL)
			sharpness_level_down_adj = spl_calculate_sharpness_level_adj(ratio);
		break;
	}

	if ((min_sharpness == 0) && (sharpness_level_down_adj >= discrete_sharpness_level))
		discrete_sharpness_level = 1;
	else if (sharpness_level_down_adj >= discrete_sharpness_level)
		discrete_sharpness_level = 0;
	else
		discrete_sharpness_level -= sharpness_level_down_adj;

	int lower_half_step_size = (mid_sharpness - min_sharpness) / 5;
	int upper_half_step_size = (max_sharpness - mid_sharpness) / 5;

	// lower half linear approximation
	if (discrete_sharpness_level < 5)
		sharpness_level = min_sharpness + (lower_half_step_size * discrete_sharpness_level);
	// upper half linear approximation
	else
		sharpness_level = mid_sharpness + (upper_half_step_size * (discrete_sharpness_level - 5));

	return sharpness_level;
}

void spl_build_isharp_1dlut_from_reference_curve(struct spl_fixed31_32 ratio, enum system_setup setup,
	struct adaptive_sharpness sharpness, enum scale_to_sharpness_policy scale_to_sharpness_policy)
{
	uint8_t *byte_ptr_1dlut_src, *byte_ptr_1dlut_dst;
	struct spl_fixed31_32 sharp_base, sharp_calc, sharp_level;
	int j;
	int size_1dlut;
	int sharp_calc_int;
	uint32_t filter_pregen_store[ISHARP_LUT_TABLE_SIZE];

	/* Custom sharpnessX1000 value */
	unsigned int sharpnessX1000 = spl_calculate_sharpness_level(ratio,
			sharpness.sharpness_level, setup,
			sharpness.sharpness_range, scale_to_sharpness_policy);
	sharp_level = spl_fixpt_from_fraction(sharpnessX1000, 1000);

	/*
	 * Check if pregen 1dlut table is already precalculated
	 * If numer/denom is different, then recalculate
	 */
	if ((filter_isharp_1D_lut_pregen[setup].sharpness_numer == sharpnessX1000) &&
		(filter_isharp_1D_lut_pregen[setup].sharpness_denom == 1000))
		return;

	/*
	 * Calculate LUT_128_gained with this equation:
	 *
	 * LUT_128_gained[i] = (uint8)(0.5 + min(255,(double)(LUT_128[i])*sharpLevel/iGain))
	 *  where LUT_128[i] is contents of 3p0x isharp 1dlut
	 *  where sharpLevel is desired sharpness level
	 *  where iGain is base sharpness level 3.0
	 *  where LUT_128_gained[i] is adjusted 1dlut value based on desired sharpness level
	 */
	byte_ptr_1dlut_src = (uint8_t *)filter_isharp_1D_lut_3p0x;
	byte_ptr_1dlut_dst = (uint8_t *)filter_pregen_store;
	size_1dlut = sizeof(filter_isharp_1D_lut_3p0x);
	memset(byte_ptr_1dlut_dst, 0, size_1dlut);
	for (j = 0; j < size_1dlut; j++) {
		sharp_base = spl_fixpt_from_int((int)*byte_ptr_1dlut_src);
		sharp_calc = spl_fixpt_mul(sharp_base, sharp_level);
		sharp_calc = spl_fixpt_div(sharp_calc, spl_fixpt_from_int(3));
		sharp_calc = spl_fixpt_min(spl_fixpt_from_int(255), sharp_calc);
		sharp_calc = spl_fixpt_add(sharp_calc, spl_fixpt_from_fraction(1, 2));
		sharp_calc_int = spl_fixpt_floor(sharp_calc);
		/* Clamp it at 0x7F so it doesn't wrap */
		if (sharp_calc_int > 127)
			sharp_calc_int = 127;
		*byte_ptr_1dlut_dst = (uint8_t)sharp_calc_int;

		byte_ptr_1dlut_src++;
		byte_ptr_1dlut_dst++;
	}

	/* Update 1dlut table and sharpness level */
	memcpy((void *)filter_isharp_1D_lut_pregen[setup].value, (void *)filter_pregen_store, size_1dlut);
	filter_isharp_1D_lut_pregen[setup].sharpness_numer = sharpnessX1000;
	filter_isharp_1D_lut_pregen[setup].sharpness_denom = 1000;
}

uint32_t *spl_get_pregen_filter_isharp_1D_lut(enum system_setup setup)
{
	return filter_isharp_1D_lut_pregen[setup].value;
}

void spl_init_blur_scale_coeffs(void)
{
	convert_filter_s1_10_to_s1_12(filter_isharp_bs_3tap_64p,
		filter_isharp_bs_3tap_64p_s1_12, 3);
	convert_filter_s1_10_to_s1_12(filter_isharp_bs_4tap_64p,
		filter_isharp_bs_4tap_64p_s1_12, 4);
	convert_filter_s1_10_to_s1_12(filter_isharp_bs_4tap_in_6_64p,
		filter_isharp_bs_4tap_in_6_64p_s1_12, 6);
}

uint16_t *spl_dscl_get_blur_scale_coeffs_64p(int taps)
{
	if (taps == 3)
		return spl_get_filter_isharp_bs_3tap_64p();
	else if (taps == 4)
		return spl_get_filter_isharp_bs_4tap_64p();
	else if (taps == 6)
		return spl_get_filter_isharp_bs_4tap_in_6_64p();
	else {
		/* should never happen, bug */
		SPL_BREAK_TO_DEBUGGER();
		return NULL;
	}
}

void spl_set_blur_scale_data(struct dscl_prog_data *dscl_prog_data,
		const struct spl_scaler_data *data)
{
	dscl_prog_data->filter_blur_scale_h =
		spl_dscl_get_blur_scale_coeffs_64p(data->taps.h_taps);

	dscl_prog_data->filter_blur_scale_v =
		spl_dscl_get_blur_scale_coeffs_64p(data->taps.v_taps);
}

