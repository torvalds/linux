/*
 * Copyright 2012-17 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "reg_helper.h"
#include "fixed31_32.h"
#include "resource.h"
#include "dwb.h"
#include "dcn20_dwb.h"

#define NUM_PHASES    16
#define HORZ_MAX_TAPS 12
#define VERT_MAX_TAPS 12

#define REG(reg)\
	dwbc20->dwbc_regs->reg

#define CTX \
	dwbc20->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dwbc20->dwbc_shift->field_name, dwbc20->dwbc_mask->field_name

#define TO_DCN20_DWBC(dwbc_base) \
	container_of(dwbc_base, struct dcn20_dwbc, base)


static const uint16_t filter_3tap_16p_upscale[27] = {
	2048, 2048, 0,
	1708, 2424, 16348,
	1372, 2796, 16308,
	1056, 3148, 16272,
	768, 3464, 16244,
	512, 3728, 16236,
	296, 3928, 16252,
	124, 4052, 16296,
	0, 4096, 0
};

static const uint16_t filter_3tap_16p_117[27] = {
	2048, 2048, 0,
	1824, 2276, 16376,
	1600, 2496, 16380,
	1376, 2700, 16,
	1156, 2880, 52,
	948, 3032, 108,
	756, 3144, 192,
	580, 3212, 296,
	428, 3236, 428
};

static const uint16_t filter_3tap_16p_150[27] = {
	2048, 2048, 0,
	1872, 2184, 36,
	1692, 2308, 88,
	1516, 2420, 156,
	1340, 2516, 236,
	1168, 2592, 328,
	1004, 2648, 440,
	844, 2684, 560,
	696, 2696, 696
};

static const uint16_t filter_3tap_16p_183[27] = {
	2048, 2048, 0,
	1892, 2104, 92,
	1744, 2152, 196,
	1592, 2196, 300,
	1448, 2232, 412,
	1304, 2256, 528,
	1168, 2276, 648,
	1032, 2288, 772,
	900, 2292, 900
};

static const uint16_t filter_4tap_16p_upscale[36] = {
	0, 4096, 0, 0,
	16240, 4056, 180, 16380,
	16136, 3952, 404, 16364,
	16072, 3780, 664, 16344,
	16040, 3556, 952, 16312,
	16036, 3284, 1268, 16272,
	16052, 2980, 1604, 16224,
	16084, 2648, 1952, 16176,
	16128, 2304, 2304, 16128
};

static const uint16_t filter_4tap_16p_117[36] = {
	428, 3236, 428, 0,
	276, 3232, 604, 16364,
	148, 3184, 800, 16340,
	44, 3104, 1016, 16312,
	16344, 2984, 1244, 16284,
	16284, 2832, 1488, 16256,
	16244, 2648, 1732, 16236,
	16220, 2440, 1976, 16220,
	16212, 2216, 2216, 16212
};

static const uint16_t filter_4tap_16p_150[36] = {
	696, 2700, 696, 0,
	560, 2700, 848, 16364,
	436, 2676, 1008, 16348,
	328, 2628, 1180, 16336,
	232, 2556, 1356, 16328,
	152, 2460, 1536, 16328,
	84, 2344, 1716, 16332,
	28, 2208, 1888, 16348,
	16376, 2052, 2052, 16376
};

static const uint16_t filter_4tap_16p_183[36] = {
	940, 2208, 940, 0,
	832, 2200, 1052, 4,
	728, 2180, 1164, 16,
	628, 2148, 1280, 36,
	536, 2100, 1392, 60,
	448, 2044, 1504, 92,
	368, 1976, 1612, 132,
	296, 1900, 1716, 176,
	232, 1812, 1812, 232
};

static const uint16_t filter_5tap_16p_upscale[45] = {
	15936, 2496, 2496, 15936, 0,
	15992, 2128, 2832, 15896, 12,
	16056, 1760, 3140, 15876, 24,
	16120, 1404, 3420, 15876, 36,
	16188, 1060, 3652, 15908, 44,
	16248, 744, 3844, 15972, 44,
	16304, 460, 3980, 16072, 40,
	16348, 212, 4064, 16208, 24,
	0, 0, 4096, 0, 0,
};

static const uint16_t filter_5tap_16p_117[45] = {
	16056, 2372, 2372, 16056, 0,
	16052, 2124, 2600, 16076, 0,
	16060, 1868, 2808, 16120, 0,
	16080, 1612, 2992, 16180, 16376,
	16112, 1356, 3144, 16268, 16364,
	16144, 1108, 3268, 16376, 16344,
	16184, 872, 3356, 124, 16320,
	16220, 656, 3412, 276, 16292,
	16256, 456, 3428, 456, 16256,
};

static const uint16_t filter_5tap_16p_150[45] = {
	16368, 2064, 2064, 16368, 0,
	16316, 1924, 2204, 44, 16372,
	16280, 1772, 2328, 116, 16356,
	16256, 1616, 2440, 204, 16340,
	16240, 1456, 2536, 304, 16320,
	16232, 1296, 2612, 416, 16300,
	16232, 1132, 2664, 544, 16284,
	16240, 976, 2700, 680, 16264,
	16248, 824, 2708, 824, 16248,
};

static const uint16_t filter_5tap_16p_183[45] = {
	228, 1816, 1816, 228, 0,
	168, 1728, 1904, 300, 16372,
	116, 1632, 1988, 376, 16360,
	72, 1528, 2060, 460, 16348,
	36, 1424, 2120, 552, 16340,
	4, 1312, 2168, 652, 16336,
	16368, 1200, 2204, 752, 16332,
	16352, 1084, 2224, 860, 16332,
	16340, 972, 2232, 972, 16340,
};

static const uint16_t filter_6tap_16p_upscale[54] = {
	0, 0, 4092, 0, 0, 0,
	44, 16188, 4064, 228, 16324, 0,
	80, 16036, 3980, 492, 16256, 4,
	108, 15916, 3844, 788, 16184, 16,
	120, 15836, 3656, 1108, 16104, 28,
	128, 15792, 3420, 1448, 16024, 44,
	124, 15776, 3144, 1800, 15948, 64,
	112, 15792, 2836, 2152, 15880, 80,
	100, 15828, 2504, 2504, 15828, 100,
};

static const uint16_t filter_6tap_16p_117[54] = {
	16168, 476, 3568, 476, 16168, 0,
	16216, 280, 3540, 692, 16116, 8,
	16264, 104, 3472, 924, 16068, 16,
	16304, 16340, 3372, 1168, 16024, 28,
	16344, 16212, 3236, 1424, 15988, 36,
	16372, 16112, 3072, 1680, 15956, 44,
	12, 16036, 2880, 1936, 15940, 48,
	28, 15984, 2668, 2192, 15936, 48,
	40, 15952, 2436, 2436, 15952, 40,
};

static const uint16_t filter_6tap_16p_150[54] = {
	16148, 920, 2724, 920, 16148, 0,
	16156, 768, 2712, 1072, 16144, 0,
	16172, 628, 2684, 1232, 16148, 16380,
	16192, 492, 2632, 1388, 16160, 16372,
	16212, 368, 2564, 1548, 16180, 16364,
	16232, 256, 2480, 1704, 16212, 16352,
	16256, 156, 2380, 1856, 16256, 16336,
	16276, 64, 2268, 2004, 16308, 16320,
	16300, 16372, 2140, 2140, 16372, 16300,
};

static const uint16_t filter_6tap_16p_183[54] = {
	16296, 1032, 2196, 1032, 16296, 0,
	16284, 924, 2196, 1144, 16320, 16376,
	16272, 820, 2180, 1256, 16348, 16364,
	16268, 716, 2156, 1364, 16380, 16352,
	16264, 620, 2116, 1472, 36, 16340,
	16268, 524, 2068, 1576, 88, 16328,
	16272, 436, 2008, 1680, 144, 16316,
	16280, 352, 1940, 1772, 204, 16304,
	16292, 276, 1860, 1860, 276, 16292,
};

static const uint16_t filter_7tap_16p_upscale[63] = {
	176, 15760, 2488, 2488, 15760, 176, 0,
	160, 15812, 2152, 2816, 15728, 192, 16376,
	136, 15884, 1812, 3124, 15720, 196, 16368,
	108, 15964, 1468, 3400, 15740, 196, 16364,
	84, 16048, 1132, 3640, 15792, 180, 16360,
	56, 16140, 812, 3832, 15884, 152, 16360,
	32, 16228, 512, 3976, 16012, 116, 16364,
	12, 16308, 240, 4064, 16180, 60, 16372,
	0, 0, 0, 4096, 0, 0, 0,
};

static const uint16_t filter_7tap_16p_117[63] = {
	92, 15868, 2464, 2464, 15868, 92, 0,
	108, 15852, 2216, 2700, 15904, 72, 0,
	112, 15856, 1960, 2916, 15964, 44, 0,
	116, 15876, 1696, 3108, 16048, 8, 8,
	112, 15908, 1428, 3268, 16156, 16348, 12,
	104, 15952, 1168, 3400, 16288, 16300, 24,
	92, 16004, 916, 3496, 64, 16244, 36,
	80, 16064, 676, 3556, 248, 16184, 48,
	64, 16124, 452, 3576, 452, 16124, 64,
};

static const uint16_t filter_7tap_16p_150[63] = {
	16224, 16380, 2208, 2208, 16380, 16224, 0,
	16252, 16304, 2072, 2324, 84, 16196, 4,
	16276, 16240, 1924, 2432, 184, 16172, 8,
	16300, 16184, 1772, 2524, 296, 16144, 12,
	16324, 16144, 1616, 2600, 416, 16124, 12,
	16344, 16112, 1456, 2660, 548, 16104, 12,
	16360, 16092, 1296, 2704, 688, 16088, 12,
	16372, 16080, 1140, 2732, 832, 16080, 8,
	0, 16076, 984, 2740, 984, 16076, 0,
};

static const uint16_t filter_7tap_16p_183[63] = {
	16216, 324, 1884, 1884, 324, 16216, 0,
	16228, 248, 1804, 1960, 408, 16212, 16380,
	16240, 176, 1716, 2028, 496, 16208, 16376,
	16252, 112, 1624, 2084, 588, 16208, 16372,
	16264, 56, 1524, 2132, 684, 16212, 16364,
	16280, 4, 1424, 2168, 788, 16220, 16356,
	16292, 16344, 1320, 2196, 892, 16232, 16344,
	16308, 16308, 1212, 2212, 996, 16252, 16332,
	16320, 16276, 1104, 2216, 1104, 16276, 16320,
};

static const uint16_t filter_8tap_16p_upscale[72] = {
	0, 0, 0, 4096, 0, 0, 0, 0,
	16360, 76, 16172, 4064, 244, 16296, 24, 16380,
	16340, 136, 15996, 3980, 524, 16204, 56, 16380,
	16328, 188, 15860, 3844, 828, 16104, 92, 16372,
	16320, 224, 15760, 3656, 1156, 16008, 128, 16368,
	16320, 248, 15696, 3428, 1496, 15912, 160, 16360,
	16320, 256, 15668, 3156, 1844, 15828, 192, 16348,
	16324, 256, 15672, 2856, 2192, 15756, 220, 16340,
	16332, 244, 15704, 2532, 2532, 15704, 244, 16332,
};

static const uint16_t filter_8tap_16p_117[72] = {
	116, 16100, 428, 3564, 428, 16100, 116, 0,
	96, 16168, 220, 3548, 656, 16032, 136, 16376,
	76, 16236, 32, 3496, 904, 15968, 152, 16372,
	56, 16300, 16252, 3408, 1164, 15908, 164, 16368,
	36, 16360, 16116, 3284, 1428, 15856, 172, 16364,
	20, 28, 16000, 3124, 1700, 15820, 176, 16364,
	4, 76, 15912, 2940, 1972, 15800, 172, 16364,
	16380, 112, 15848, 2724, 2236, 15792, 160, 16364,
	16372, 140, 15812, 2488, 2488, 15812, 140, 16372,
};

static const uint16_t filter_8tap_16p_150[72] = {
	16380, 16020, 1032, 2756, 1032, 16020, 16380, 0,
	12, 16020, 876, 2744, 1184, 16032, 16364, 4,
	24, 16028, 728, 2716, 1344, 16052, 16340, 8,
	36, 16040, 584, 2668, 1500, 16080, 16316, 16,
	40, 16060, 448, 2608, 1652, 16120, 16288, 20,
	44, 16080, 320, 2528, 1804, 16168, 16260, 28,
	48, 16108, 204, 2436, 1948, 16232, 16228, 32,
	44, 16136, 100, 2328, 2084, 16304, 16200, 40,
	44, 16168, 4, 2212, 2212, 4, 16168, 44,
};

static const uint16_t filter_8tap_16p_183[72] = {
	16264, 16264, 1164, 2244, 1164, 16264, 16264, 0,
	16280, 16232, 1056, 2236, 1268, 16300, 16248, 0,
	16296, 16204, 948, 2220, 1372, 16348, 16232, 0,
	16312, 16184, 844, 2192, 1472, 12, 16216, 4,
	16328, 16172, 740, 2156, 1572, 72, 16200, 0,
	16340, 16160, 640, 2108, 1668, 136, 16188, 0,
	16352, 16156, 544, 2052, 1756, 204, 16176, 16380,
	16360, 16156, 452, 1988, 1840, 280, 16164, 16376,
	16368, 16160, 364, 1920, 1920, 364, 16160, 16368,
};

static const uint16_t filter_9tap_16p_upscale[81] = {
	16284, 296, 15660, 2572, 2572, 15660, 296, 16284, 0,
	16296, 272, 15712, 2228, 2896, 15632, 304, 16276, 4,
	16308, 240, 15788, 1876, 3192, 15632, 304, 16276, 4,
	16320, 204, 15876, 1520, 3452, 15664, 288, 16280, 8,
	16336, 164, 15976, 1176, 3676, 15732, 260, 16288, 12,
	16348, 120, 16080, 844, 3856, 15840, 216, 16300, 12,
	16364, 76, 16188, 532, 3988, 15984, 156, 16324, 8,
	16376, 36, 16288, 252, 4068, 16164, 84, 16352, 4,
	0, 0, 0, 0, 4096, 0, 0, 0, 0,
};

static const uint16_t filter_9tap_16p_117[81] = {
	16356, 172, 15776, 2504, 2504, 15776, 172, 16356, 0,
	16344, 200, 15756, 2252, 2740, 15816, 136, 16372, 16380,
	16336, 216, 15756, 1988, 2956, 15884, 92, 8, 16380,
	16332, 224, 15780, 1720, 3144, 15976, 40, 28, 16376,
	16328, 224, 15816, 1448, 3304, 16096, 16364, 52, 16372,
	16328, 216, 15868, 1180, 3432, 16240, 16296, 80, 16364,
	16332, 200, 15928, 916, 3524, 24, 16224, 108, 16356,
	16336, 184, 15996, 668, 3580, 220, 16148, 132, 16352,
	16344, 160, 16072, 436, 3600, 436, 16072, 160, 16344,
};

static const uint16_t filter_9tap_16p_150[81] = {
	84, 16128, 0, 2216, 2216, 0, 16128, 84, 0,
	80, 16160, 16296, 2088, 2332, 100, 16092, 84, 0,
	76, 16196, 16220, 1956, 2432, 208, 16064, 80, 0,
	72, 16232, 16152, 1812, 2524, 328, 16036, 76, 4,
	64, 16264, 16096, 1664, 2600, 460, 16012, 64, 8,
	56, 16300, 16052, 1508, 2656, 596, 15996, 52, 12,
	48, 16328, 16020, 1356, 2700, 740, 15984, 36, 20,
	40, 16356, 15996, 1196, 2728, 888, 15980, 20, 24,
	32, 0, 15984, 1044, 2736, 1044, 15984, 0, 32,
};

static const uint16_t filter_9tap_16p_183[81] = {
	16356, 16112, 388, 1952, 1952, 388, 16112, 16356, 0,
	16368, 16116, 304, 1876, 2020, 480, 16112, 16344, 4,
	16376, 16124, 224, 1792, 2080, 576, 16116, 16328, 8,
	0, 16136, 148, 1700, 2132, 672, 16124, 16312, 8,
	8, 16148, 80, 1604, 2176, 772, 16140, 16296, 12,
	12, 16164, 16, 1504, 2208, 876, 16156, 16276, 16,
	16, 16180, 16344, 1404, 2232, 980, 16184, 16256, 20,
	20, 16200, 16296, 1300, 2244, 1088, 16212, 16240, 20,
	20, 16220, 16252, 1196, 2252, 1196, 16252, 16220, 20,
};

static const uint16_t filter_10tap_16p_upscale[90] = {
	0, 0, 0, 0, 4096, 0, 0, 0, 0, 0,
	12, 16344, 88, 16160, 4068, 252, 16280, 44, 16368, 0,
	24, 16308, 168, 15976, 3988, 540, 16176, 92, 16348, 0,
	32, 16280, 236, 15828, 3852, 852, 16064, 140, 16328, 4,
	36, 16260, 284, 15720, 3672, 1184, 15956, 188, 16308, 8,
	36, 16244, 320, 15648, 3448, 1528, 15852, 236, 16288, 12,
	36, 16240, 336, 15612, 3184, 1880, 15764, 276, 16272, 20,
	32, 16240, 340, 15608, 2888, 2228, 15688, 308, 16256, 24,
	28, 16244, 332, 15636, 2568, 2568, 15636, 332, 16244, 28,
};

static const uint16_t filter_10tap_16p_117[90] = {
	16308, 196, 16048, 440, 3636, 440, 16048, 196, 16308, 0,
	16316, 164, 16132, 220, 3612, 676, 15972, 220, 16300, 0,
	16324, 132, 16212, 20, 3552, 932, 15900, 240, 16296, 4,
	16336, 100, 16292, 16232, 3456, 1192, 15836, 256, 16296, 4,
	16348, 68, 16364, 16084, 3324, 1464, 15784, 264, 16296, 8,
	16356, 36, 48, 15960, 3164, 1736, 15748, 260, 16304, 4,
	16364, 8, 108, 15864, 2972, 2008, 15728, 252, 16312, 4,
	16372, 16368, 160, 15792, 2756, 2268, 15724, 228, 16328, 0,
	16380, 16344, 200, 15748, 2520, 2520, 15748, 200, 16344, 16380,
};

static const uint16_t filter_10tap_16p_150[90] = {
	64, 0, 15956, 1048, 2716, 1048, 15956, 0, 64, 0,
	52, 24, 15952, 896, 2708, 1204, 15972, 16356, 72, 16380,
	44, 48, 15952, 748, 2684, 1360, 16000, 16320, 84, 16380,
	32, 68, 15964, 604, 2644, 1516, 16032, 16288, 92, 16376,
	24, 88, 15980, 464, 2588, 1668, 16080, 16248, 100, 16376,
	16, 100, 16004, 332, 2516, 1816, 16140, 16212, 108, 16376,
	8, 108, 16032, 212, 2428, 1956, 16208, 16172, 112, 16376,
	4, 116, 16060, 100, 2328, 2092, 16288, 16132, 116, 16380,
	0, 116, 16096, 16380, 2216, 2216, 16380, 16096, 116, 0,
};

static const uint16_t filter_10tap_16p_183[90] = {
	40, 16180, 16240, 1216, 2256, 1216, 16240, 16180, 40, 0,
	44, 16204, 16200, 1112, 2252, 1320, 16288, 16160, 36, 0,
	44, 16224, 16168, 1004, 2236, 1424, 16344, 16144, 28, 4,
	44, 16248, 16136, 900, 2208, 1524, 16, 16124, 24, 8,
	44, 16268, 16116, 796, 2176, 1620, 84, 16108, 12, 12,
	40, 16288, 16100, 692, 2132, 1712, 156, 16096, 4, 16,
	36, 16308, 16088, 592, 2080, 1796, 232, 16088, 16376, 20,
	32, 16328, 16080, 496, 2020, 1876, 316, 16080, 16360, 24,
	28, 16344, 16080, 404, 1952, 1952, 404, 16080, 16344, 28,
};

static const uint16_t filter_11tap_16p_upscale[99] = {
	60, 16216, 356, 15620, 2556, 2556, 15620, 356, 16216, 60, 0,
	52, 16224, 336, 15672, 2224, 2876, 15592, 368, 16208, 64, 16380,
	44, 16244, 304, 15744, 1876, 3176, 15596, 364, 16212, 64, 16376,
	36, 16264, 260, 15836, 1532, 3440, 15636, 340, 16220, 60, 16376,
	28, 16288, 212, 15940, 1188, 3668, 15708, 304, 16236, 56, 16376,
	20, 16312, 160, 16052, 856, 3848, 15820, 248, 16264, 48, 16376,
	12, 16336, 104, 16164, 544, 3984, 15968, 180, 16296, 36, 16376,
	4, 16360, 48, 16276, 256, 4068, 16160, 96, 16336, 16, 16380,
	0, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0,
};

static const uint16_t filter_11tap_16p_117[99] = {
	16380, 16332, 220, 15728, 2536, 2536, 15728, 220, 16332, 16380, 0,
	4, 16308, 256, 15704, 2280, 2768, 15772, 176, 16360, 16368, 0,
	12, 16292, 280, 15704, 2016, 2984, 15848, 120, 8, 16356, 0,
	20, 16276, 292, 15724, 1744, 3172, 15948, 56, 40, 16340, 4,
	24, 16268, 292, 15760, 1468, 3328, 16072, 16368, 80, 16324, 8,
	24, 16264, 288, 15816, 1196, 3456, 16224, 16288, 116, 16312, 12,
	24, 16264, 272, 15880, 932, 3548, 16, 16208, 152, 16296, 16,
	24, 16268, 248, 15956, 676, 3604, 216, 16120, 188, 16284, 20,
	24, 16276, 220, 16036, 436, 3624, 436, 16036, 220, 16276, 24,
};

static const uint16_t filter_11tap_16p_150[99] = {
	0, 144, 16072, 0, 2212, 2212, 0, 16072, 144, 0, 0,
	16376, 144, 16112, 16288, 2092, 2324, 104, 16036, 140, 8, 16380,
	16368, 144, 16152, 16204, 1960, 2424, 216, 16004, 132, 16, 16376,
	16364, 140, 16192, 16132, 1820, 2512, 340, 15976, 116, 28, 16376,
	16364, 132, 16232, 16072, 1676, 2584, 476, 15952, 100, 40, 16372,
	16360, 124, 16272, 16020, 1528, 2644, 612, 15936, 80, 52, 16368,
	16360, 116, 16312, 15980, 1372, 2684, 760, 15928, 56, 64, 16364,
	16360, 104, 16348, 15952, 1216, 2712, 908, 15928, 28, 76, 16364,
	16360, 92, 0, 15936, 1064, 2720, 1064, 15936, 0, 92, 16360,
};

static const uint16_t filter_11tap_16p_183[99] = {
	60, 16336, 16052, 412, 1948, 1948, 412, 16052, 16336, 60, 0,
	56, 16356, 16052, 324, 1876, 2016, 504, 16056, 16316, 64, 0,
	48, 16372, 16060, 240, 1796, 2072, 604, 16064, 16292, 64, 0,
	44, 4, 16068, 160, 1712, 2124, 700, 16080, 16272, 68, 0,
	40, 20, 16080, 84, 1620, 2164, 804, 16096, 16248, 68, 4,
	32, 32, 16096, 16, 1524, 2200, 908, 16124, 16224, 68, 4,
	28, 40, 16112, 16340, 1428, 2220, 1012, 16152, 16200, 64, 8,
	24, 52, 16132, 16284, 1328, 2236, 1120, 16192, 16176, 64, 12,
	16, 56, 16156, 16236, 1224, 2240, 1224, 16236, 16156, 56, 16,
};

static const uint16_t filter_12tap_16p_upscale[108] = {
	0, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0, 0,
	16376, 24, 16332, 100, 16156, 4068, 260, 16272, 56, 16356, 8, 0,
	16368, 44, 16284, 188, 15964, 3988, 548, 16156, 112, 16328, 20, 16380,
	16360, 64, 16248, 260, 15812, 3856, 864, 16040, 172, 16296, 32, 16380,
	16360, 76, 16216, 320, 15696, 3672, 1196, 15928, 228, 16268, 44, 16376,
	16356, 84, 16196, 360, 15620, 3448, 1540, 15820, 280, 16240, 56, 16372,
	16356, 88, 16184, 384, 15580, 3188, 1888, 15728, 324, 16216, 68, 16368,
	16360, 88, 16180, 392, 15576, 2892, 2236, 15652, 360, 16200, 80, 16364,
	16360, 84, 16188, 384, 15600, 2576, 2576, 15600, 384, 16188, 84, 16360,
};

static const uint16_t filter_12tap_16p_117[108] = {
	48, 16248, 240, 16028, 436, 3612, 436, 16028, 240, 16248, 48, 0,
	44, 16260, 208, 16116, 212, 3596, 676, 15944, 272, 16240, 48, 16380,
	40, 16276, 168, 16204, 12, 3540, 932, 15868, 296, 16240, 48, 16380,
	36, 16292, 128, 16288, 16220, 3452, 1196, 15800, 312, 16240, 44, 16380,
	28, 16308, 84, 16372, 16064, 3324, 1472, 15748, 316, 16244, 40, 16380,
	24, 16328, 44, 64, 15936, 3168, 1744, 15708, 312, 16256, 32, 16380,
	16, 16344, 8, 132, 15836, 2980, 2016, 15688, 300, 16272, 20, 0,
	12, 16364, 16356, 188, 15760, 2768, 2280, 15688, 272, 16296, 8, 4,
	8, 16380, 16324, 236, 15712, 2532, 2532, 15712, 236, 16324, 16380, 8,
};

static const uint16_t filter_12tap_16p_150[108] = {
	16340, 116, 0, 15916, 1076, 2724, 1076, 15916, 0, 116, 16340, 0,
	16340, 100, 32, 15908, 920, 2716, 1232, 15936, 16344, 128, 16340, 0,
	16344, 84, 64, 15908, 772, 2692, 1388, 15968, 16304, 140, 16344, 16380,
	16344, 68, 92, 15912, 624, 2652, 1540, 16008, 16264, 152, 16344, 16380,
	16348, 52, 112, 15928, 484, 2592, 1688, 16060, 16220, 160, 16348, 16380,
	16352, 40, 132, 15952, 348, 2520, 1836, 16124, 16176, 168, 16356, 16376,
	16356, 24, 148, 15980, 224, 2436, 1976, 16200, 16132, 172, 16364, 16372,
	16360, 12, 160, 16012, 108, 2336, 2104, 16288, 16088, 172, 16372, 16368,
	16364, 0, 168, 16048, 0, 2228, 2228, 0, 16048, 168, 0, 16364,
};

static const uint16_t filter_12tap_16p_183[108] = {
	36, 72, 16132, 16228, 1224, 2224, 1224, 16228, 16132, 72, 36, 0,
	28, 80, 16156, 16184, 1120, 2224, 1328, 16280, 16112, 64, 40, 16380,
	24, 84, 16180, 16144, 1016, 2208, 1428, 16340, 16092, 52, 48, 16380,
	16, 88, 16208, 16112, 912, 2188, 1524, 16, 16072, 36, 56, 16380,
	12, 92, 16232, 16084, 812, 2156, 1620, 88, 16056, 24, 64, 16380,
	8, 92, 16256, 16064, 708, 2116, 1708, 164, 16044, 4, 68, 16380,
	4, 88, 16280, 16048, 608, 2068, 1792, 244, 16036, 16372, 76, 16380,
	0, 88, 16308, 16036, 512, 2008, 1872, 328, 16032, 16352, 80, 16380,
	0, 84, 16328, 16032, 416, 1944, 1944, 416, 16032, 16328, 84, 0,
};

static const uint16_t *wbscl_get_filter_3tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_3tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_3tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_3tap_16p_150;
	else
		return filter_3tap_16p_183;
}

static const uint16_t *wbscl_get_filter_4tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_4tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_4tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_4tap_16p_150;
	else
		return filter_4tap_16p_183;
}

static const uint16_t *wbscl_get_filter_5tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_5tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_5tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_5tap_16p_150;
	else
		return filter_5tap_16p_183;
}

static const uint16_t *wbscl_get_filter_6tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_6tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_6tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_6tap_16p_150;
	else
		return filter_6tap_16p_183;
}

static const uint16_t *wbscl_get_filter_7tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_7tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_7tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_7tap_16p_150;
	else
		return filter_7tap_16p_183;
}

static const uint16_t *wbscl_get_filter_8tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_8tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_8tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_8tap_16p_150;
	else
		return filter_8tap_16p_183;
}

static const uint16_t *wbscl_get_filter_9tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_9tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_9tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_9tap_16p_150;
	else
		return filter_9tap_16p_183;
}
static const uint16_t *wbscl_get_filter_10tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_10tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_10tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_10tap_16p_150;
	else
		return filter_10tap_16p_183;
}

static const uint16_t *wbscl_get_filter_11tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_11tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_11tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_11tap_16p_150;
	else
		return filter_11tap_16p_183;
}

static const uint16_t *wbscl_get_filter_12tap_16p(struct fixed31_32 ratio)
{
	if (ratio.value < dc_fixpt_one.value)
		return filter_12tap_16p_upscale;
	else if (ratio.value < dc_fixpt_from_fraction(4, 3).value)
		return filter_12tap_16p_117;
	else if (ratio.value < dc_fixpt_from_fraction(5, 3).value)
		return filter_12tap_16p_150;
	else
		return filter_12tap_16p_183;
}

static const uint16_t *wbscl_get_filter_coeffs_16p(int taps, struct fixed31_32 ratio)
{
	if (taps == 12)
		return wbscl_get_filter_12tap_16p(ratio);
	else if (taps == 11)
		return wbscl_get_filter_11tap_16p(ratio);
	else if (taps == 10)
		return wbscl_get_filter_10tap_16p(ratio);
	else if (taps == 9)
		return wbscl_get_filter_9tap_16p(ratio);
	else if (taps == 8)
		return wbscl_get_filter_8tap_16p(ratio);
	else if (taps == 7)
		return wbscl_get_filter_7tap_16p(ratio);
	else if (taps == 6)
		return wbscl_get_filter_6tap_16p(ratio);
	else if (taps == 5)
		return wbscl_get_filter_5tap_16p(ratio);
	else if (taps == 4)
		return wbscl_get_filter_4tap_16p(ratio);
	else if (taps == 3)
		return wbscl_get_filter_3tap_16p(ratio);
	else if (taps == 2)
		return get_filter_2tap_16p();
	else if (taps == 1)
		return NULL;
	else {
		/* should never happen, bug */
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

static void wbscl_set_scaler_filter(
	struct dcn20_dwbc *dwbc20,
	uint32_t taps,
	enum wbscl_coef_filter_type_sel filter_type,
	const uint16_t *filter)
{
	const int tap_pairs = (taps + 1) / 2;
	int phase;
	int pair;
	uint16_t odd_coef, even_coef;

	for (phase = 0; phase < (NUM_PHASES / 2 + 1); phase++) {
		for (pair = 0; pair < tap_pairs; pair++) {
			even_coef = filter[phase * taps + 2 * pair];
			if ((pair * 2 + 1) < taps)
				odd_coef = filter[phase * taps + 2 * pair + 1];
			else
				odd_coef = 0;

			REG_SET_3(WBSCL_COEF_RAM_SELECT, 0,
				WBSCL_COEF_RAM_TAP_PAIR_IDX, pair,
				WBSCL_COEF_RAM_PHASE, phase,
				WBSCL_COEF_RAM_FILTER_TYPE, filter_type);

			REG_SET_4(WBSCL_COEF_RAM_TAP_DATA, 0,
				/* Even tap coefficient (bits 1:0 fixed to 0) */
				WBSCL_COEF_RAM_EVEN_TAP_COEF, even_coef,
				/* Write/read control for even coefficient */
				WBSCL_COEF_RAM_EVEN_TAP_COEF_EN, 1,
				/* Odd tap coefficient (bits 1:0 fixed to 0) */
				WBSCL_COEF_RAM_ODD_TAP_COEF, odd_coef,
				/* Write/read control for odd coefficient */
				WBSCL_COEF_RAM_ODD_TAP_COEF_EN, 1);
		}
	}
}

bool dwb_program_horz_scalar(struct dcn20_dwbc *dwbc20,
		uint32_t src_width,
		uint32_t dest_width,
		struct scaling_taps num_taps)
{
	uint32_t h_ratio_luma = 1;
	uint32_t h_taps_luma = num_taps.h_taps;
	uint32_t h_taps_chroma = num_taps.h_taps_c;
	int32_t h_init_phase_luma = 0;
	int32_t h_init_phase_chroma = 0;
	uint32_t h_init_phase_luma_int = 0;
	uint32_t h_init_phase_luma_frac = 0;
	uint32_t h_init_phase_chroma_int = 0;
	uint32_t h_init_phase_chroma_frac = 0;
	const uint16_t *filter_h = NULL;
	const uint16_t *filter_h_c = NULL;


	struct fixed31_32 tmp_h_init_phase_luma = dc_fixpt_from_int(0);
	struct fixed31_32 tmp_h_init_phase_chroma = dc_fixpt_from_int(0);


	/*Calculate ratio*/
	struct fixed31_32 tmp_h_ratio_luma = dc_fixpt_from_fraction(
		src_width, dest_width);

	if (dc_fixpt_floor(tmp_h_ratio_luma) == 8)
		h_ratio_luma = -1;
	else
		h_ratio_luma = dc_fixpt_u3d19(tmp_h_ratio_luma) << 5;

	/*Program ratio*/
	REG_UPDATE(WBSCL_HORZ_FILTER_SCALE_RATIO, WBSCL_H_SCALE_RATIO, h_ratio_luma);

	/* Program taps*/
	REG_UPDATE(WBSCL_TAP_CONTROL, WBSCL_H_NUM_OF_TAPS_Y_RGB, h_taps_luma - 1);
	REG_UPDATE(WBSCL_TAP_CONTROL, WBSCL_H_NUM_OF_TAPS_CBCR, h_taps_chroma - 1);

	/* Calculate phase*/
	tmp_h_init_phase_luma = dc_fixpt_add_int(tmp_h_ratio_luma, h_taps_luma + 1);
	tmp_h_init_phase_luma = dc_fixpt_div_int(tmp_h_init_phase_luma, 2);
	tmp_h_init_phase_luma = dc_fixpt_sub_int(tmp_h_init_phase_luma, h_taps_luma);

	h_init_phase_luma = dc_fixpt_s4d19(tmp_h_init_phase_luma);
	h_init_phase_luma_int = (h_init_phase_luma >> 19) & 0x1f;
	h_init_phase_luma_frac = (h_init_phase_luma & 0x7ffff) << 5;

	tmp_h_init_phase_chroma = dc_fixpt_mul_int(tmp_h_ratio_luma, 2);
	tmp_h_init_phase_chroma = dc_fixpt_add_int(tmp_h_init_phase_chroma, h_taps_chroma + 1);
	tmp_h_init_phase_chroma = dc_fixpt_div_int(tmp_h_init_phase_chroma, 2);
	tmp_h_init_phase_chroma = dc_fixpt_sub_int(tmp_h_init_phase_chroma, h_taps_chroma);
	tmp_h_init_phase_chroma = dc_fixpt_add(tmp_h_init_phase_chroma, dc_fixpt_from_fraction(1, 4));

	h_init_phase_chroma = dc_fixpt_s4d19(tmp_h_init_phase_chroma);
	h_init_phase_chroma_int = (h_init_phase_chroma >> 19) & 0x1f;
	h_init_phase_chroma_frac = (h_init_phase_chroma & 0x7ffff) << 5;

	/* Program phase*/
	REG_UPDATE(WBSCL_HORZ_FILTER_INIT_Y_RGB, WBSCL_H_INIT_INT_Y_RGB, h_init_phase_luma_int);
	REG_UPDATE(WBSCL_HORZ_FILTER_INIT_Y_RGB, WBSCL_H_INIT_FRAC_Y_RGB, h_init_phase_luma_frac);
	REG_UPDATE(WBSCL_HORZ_FILTER_INIT_CBCR, WBSCL_H_INIT_INT_CBCR, h_init_phase_chroma_int);
	REG_UPDATE(WBSCL_HORZ_FILTER_INIT_CBCR, WBSCL_H_INIT_FRAC_CBCR, h_init_phase_chroma_frac);

	/* Program LUT coefficients*/
	filter_h = wbscl_get_filter_coeffs_16p(
		h_taps_luma, tmp_h_ratio_luma);
	filter_h_c = wbscl_get_filter_coeffs_16p(
		h_taps_chroma, dc_fixpt_from_int(h_ratio_luma * 2));

	wbscl_set_scaler_filter(dwbc20, h_taps_luma,
		WBSCL_COEF_LUMA_HORZ_FILTER, filter_h);

	wbscl_set_scaler_filter(dwbc20, h_taps_chroma,
		WBSCL_COEF_CHROMA_HORZ_FILTER, filter_h_c);

	return true;
}

bool dwb_program_vert_scalar(struct dcn20_dwbc *dwbc20,
		uint32_t src_height,
		uint32_t dest_height,
		struct scaling_taps num_taps,
		enum dwb_subsample_position subsample_position)
{
	uint32_t v_ratio_luma = 1;
	uint32_t v_taps_luma = num_taps.v_taps;
	uint32_t v_taps_chroma = num_taps.v_taps_c;
	int32_t v_init_phase_luma = 0;
	int32_t v_init_phase_chroma = 0;
	uint32_t v_init_phase_luma_int = 0;
	uint32_t v_init_phase_luma_frac = 0;
	uint32_t v_init_phase_chroma_int = 0;
	uint32_t v_init_phase_chroma_frac = 0;

	const uint16_t *filter_v = NULL;
	const uint16_t *filter_v_c = NULL;

	struct fixed31_32 tmp_v_init_phase_luma = dc_fixpt_from_int(0);
	struct fixed31_32 tmp_v_init_phase_chroma = dc_fixpt_from_int(0);

	/*Calculate ratio*/
	struct fixed31_32 tmp_v_ratio_luma = dc_fixpt_from_fraction(
		src_height, dest_height);

	if (dc_fixpt_floor(tmp_v_ratio_luma) == 8)
		v_ratio_luma = -1;
	else
		v_ratio_luma = dc_fixpt_u3d19(tmp_v_ratio_luma) << 5;

	/*Program ratio*/
	REG_UPDATE(WBSCL_VERT_FILTER_SCALE_RATIO, WBSCL_V_SCALE_RATIO, v_ratio_luma);

	/* Program taps*/
	REG_UPDATE(WBSCL_TAP_CONTROL, WBSCL_V_NUM_OF_TAPS_Y_RGB, v_taps_luma - 1);
	REG_UPDATE(WBSCL_TAP_CONTROL, WBSCL_V_NUM_OF_TAPS_CBCR, v_taps_chroma - 1);

	/* Calculate phase*/
	tmp_v_init_phase_luma = dc_fixpt_add_int(tmp_v_ratio_luma, v_taps_luma + 1);
	tmp_v_init_phase_luma = dc_fixpt_div_int(tmp_v_init_phase_luma, 2);
	tmp_v_init_phase_luma = dc_fixpt_sub_int(tmp_v_init_phase_luma, v_taps_luma);

	v_init_phase_luma = dc_fixpt_s4d19(tmp_v_init_phase_luma);
	v_init_phase_luma_int = (v_init_phase_luma >> 19) & 0x1f;
	v_init_phase_luma_frac = (v_init_phase_luma & 0x7ffff) << 5;

	tmp_v_init_phase_chroma = dc_fixpt_mul_int(tmp_v_ratio_luma, 2);
	tmp_v_init_phase_chroma = dc_fixpt_add_int(tmp_v_init_phase_chroma, v_taps_chroma + 1);
	tmp_v_init_phase_chroma = dc_fixpt_div_int(tmp_v_init_phase_chroma, 2);
	tmp_v_init_phase_chroma = dc_fixpt_sub_int(tmp_v_init_phase_chroma, v_taps_chroma);
	if (subsample_position == DWB_COSITED_SUBSAMPLING)
		tmp_v_init_phase_chroma = dc_fixpt_add(tmp_v_init_phase_chroma, dc_fixpt_from_fraction(1, 4));

	v_init_phase_chroma = dc_fixpt_s4d19(tmp_v_init_phase_chroma);
	v_init_phase_chroma_int = (v_init_phase_chroma >> 19) & 0x1f;
	v_init_phase_chroma_frac = (v_init_phase_chroma & 0x7ffff) << 5;

	/* Program phase*/
	REG_UPDATE(WBSCL_VERT_FILTER_INIT_Y_RGB, WBSCL_V_INIT_INT_Y_RGB, v_init_phase_luma_int);
	REG_UPDATE(WBSCL_VERT_FILTER_INIT_Y_RGB, WBSCL_V_INIT_FRAC_Y_RGB, v_init_phase_luma_frac);
	REG_UPDATE(WBSCL_VERT_FILTER_INIT_CBCR, WBSCL_V_INIT_INT_CBCR, v_init_phase_chroma_int);
	REG_UPDATE(WBSCL_VERT_FILTER_INIT_CBCR, WBSCL_V_INIT_FRAC_CBCR, v_init_phase_chroma_frac);


	/* Program LUT coefficients*/
	filter_v  = wbscl_get_filter_coeffs_16p(
		v_taps_luma, tmp_v_ratio_luma);
	filter_v_c = wbscl_get_filter_coeffs_16p(
		v_taps_chroma, dc_fixpt_from_int(v_ratio_luma * 2));
	wbscl_set_scaler_filter(dwbc20, v_taps_luma,
		WBSCL_COEF_LUMA_VERT_FILTER, filter_v);

	wbscl_set_scaler_filter(dwbc20, v_taps_chroma,
		WBSCL_COEF_CHROMA_VERT_FILTER, filter_v_c);
	return true;
}
