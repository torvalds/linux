#include "r8192E.h"
#include "r8192E_hw.h"
#include "r819xE_phyreg.h"
#include "r8190_rtl8256.h"
#include "r819xE_phy.h"
#include "r8192E_dm.h"
#ifdef ENABLE_DOT11D
#include "ieee80211/dot11d.h"
#endif
static u32 RF_CHANNEL_TABLE_ZEBRA[] = {
	0,
	0x085c, //2412 1
	0x08dc, //2417 2
	0x095c, //2422 3
	0x09dc, //2427 4
	0x0a5c, //2432 5
	0x0adc, //2437 6
	0x0b5c, //2442 7
	0x0bdc, //2447 8
	0x0c5c, //2452 9
	0x0cdc, //2457 10
	0x0d5c, //2462 11
	0x0ddc, //2467 12
	0x0e5c, //2472 13
	0x0f72, //2484
};
#ifdef RTL8190P
u32 Rtl8190PciMACPHY_Array[] = {
0x03c,0xffff0000,0x00000f0f,
0x340,0xffffffff,0x161a1a1a,
0x344,0xffffffff,0x12121416,
0x348,0x0000ffff,0x00001818,
0x12c,0xffffffff,0x04000802,
0x318,0x00000fff,0x00000800,
};
u32 Rtl8190PciMACPHY_Array_PG[] = {
0x03c,0xffff0000,0x00000f0f,
0x340,0xffffffff,0x0a0c0d0f,
0x344,0xffffffff,0x06070809,
0x344,0xffffffff,0x06070809,
0x348,0x0000ffff,0x00000000,
0x12c,0xffffffff,0x04000802,
0x318,0x00000fff,0x00000800,
};

u32 Rtl8190PciAGCTAB_Array[AGCTAB_ArrayLength] = {
0xc78,0x7d000001,
0xc78,0x7d010001,
0xc78,0x7d020001,
0xc78,0x7d030001,
0xc78,0x7c040001,
0xc78,0x7b050001,
0xc78,0x7a060001,
0xc78,0x79070001,
0xc78,0x78080001,
0xc78,0x77090001,
0xc78,0x760a0001,
0xc78,0x750b0001,
0xc78,0x740c0001,
0xc78,0x730d0001,
0xc78,0x720e0001,
0xc78,0x710f0001,
0xc78,0x70100001,
0xc78,0x6f110001,
0xc78,0x6e120001,
0xc78,0x6d130001,
0xc78,0x6c140001,
0xc78,0x6b150001,
0xc78,0x6a160001,
0xc78,0x69170001,
0xc78,0x68180001,
0xc78,0x67190001,
0xc78,0x661a0001,
0xc78,0x651b0001,
0xc78,0x641c0001,
0xc78,0x491d0001,
0xc78,0x481e0001,
0xc78,0x471f0001,
0xc78,0x46200001,
0xc78,0x45210001,
0xc78,0x44220001,
0xc78,0x43230001,
0xc78,0x28240001,
0xc78,0x27250001,
0xc78,0x26260001,
0xc78,0x25270001,
0xc78,0x24280001,
0xc78,0x23290001,
0xc78,0x222a0001,
0xc78,0x212b0001,
0xc78,0x202c0001,
0xc78,0x0a2d0001,
0xc78,0x082e0001,
0xc78,0x062f0001,
0xc78,0x05300001,
0xc78,0x04310001,
0xc78,0x03320001,
0xc78,0x02330001,
0xc78,0x01340001,
0xc78,0x00350001,
0xc78,0x00360001,
0xc78,0x00370001,
0xc78,0x00380001,
0xc78,0x00390001,
0xc78,0x003a0001,
0xc78,0x003b0001,
0xc78,0x003c0001,
0xc78,0x003d0001,
0xc78,0x003e0001,
0xc78,0x003f0001,
0xc78,0x7d400001,
0xc78,0x7d410001,
0xc78,0x7d420001,
0xc78,0x7d430001,
0xc78,0x7c440001,
0xc78,0x7b450001,
0xc78,0x7a460001,
0xc78,0x79470001,
0xc78,0x78480001,
0xc78,0x77490001,
0xc78,0x764a0001,
0xc78,0x754b0001,
0xc78,0x744c0001,
0xc78,0x734d0001,
0xc78,0x724e0001,
0xc78,0x714f0001,
0xc78,0x70500001,
0xc78,0x6f510001,
0xc78,0x6e520001,
0xc78,0x6d530001,
0xc78,0x6c540001,
0xc78,0x6b550001,
0xc78,0x6a560001,
0xc78,0x69570001,
0xc78,0x68580001,
0xc78,0x67590001,
0xc78,0x665a0001,
0xc78,0x655b0001,
0xc78,0x645c0001,
0xc78,0x495d0001,
0xc78,0x485e0001,
0xc78,0x475f0001,
0xc78,0x46600001,
0xc78,0x45610001,
0xc78,0x44620001,
0xc78,0x43630001,
0xc78,0x28640001,
0xc78,0x27650001,
0xc78,0x26660001,
0xc78,0x25670001,
0xc78,0x24680001,
0xc78,0x23690001,
0xc78,0x226a0001,
0xc78,0x216b0001,
0xc78,0x206c0001,
0xc78,0x0a6d0001,
0xc78,0x086e0001,
0xc78,0x066f0001,
0xc78,0x05700001,
0xc78,0x04710001,
0xc78,0x03720001,
0xc78,0x02730001,
0xc78,0x01740001,
0xc78,0x00750001,
0xc78,0x00760001,
0xc78,0x00770001,
0xc78,0x00780001,
0xc78,0x00790001,
0xc78,0x007a0001,
0xc78,0x007b0001,
0xc78,0x007c0001,
0xc78,0x007d0001,
0xc78,0x007e0001,
0xc78,0x007f0001,
0xc78,0x3600001e,
0xc78,0x3601001e,
0xc78,0x3602001e,
0xc78,0x3603001e,
0xc78,0x3604001e,
0xc78,0x3605001e,
0xc78,0x3a06001e,
0xc78,0x3c07001e,
0xc78,0x3e08001e,
0xc78,0x4209001e,
0xc78,0x430a001e,
0xc78,0x450b001e,
0xc78,0x470c001e,
0xc78,0x480d001e,
0xc78,0x490e001e,
0xc78,0x4b0f001e,
0xc78,0x4c10001e,
0xc78,0x4d11001e,
0xc78,0x4d12001e,
0xc78,0x4e13001e,
0xc78,0x4f14001e,
0xc78,0x5015001e,
0xc78,0x5116001e,
0xc78,0x5117001e,
0xc78,0x5218001e,
0xc78,0x5219001e,
0xc78,0x531a001e,
0xc78,0x541b001e,
0xc78,0x541c001e,
0xc78,0x551d001e,
0xc78,0x561e001e,
0xc78,0x561f001e,
0xc78,0x5720001e,
0xc78,0x5821001e,
0xc78,0x5822001e,
0xc78,0x5923001e,
0xc78,0x5924001e,
0xc78,0x5a25001e,
0xc78,0x5b26001e,
0xc78,0x5b27001e,
0xc78,0x5c28001e,
0xc78,0x5c29001e,
0xc78,0x5d2a001e,
0xc78,0x5d2b001e,
0xc78,0x5e2c001e,
0xc78,0x5e2d001e,
0xc78,0x5f2e001e,
0xc78,0x602f001e,
0xc78,0x6030001e,
0xc78,0x6131001e,
0xc78,0x6132001e,
0xc78,0x6233001e,
0xc78,0x6234001e,
0xc78,0x6335001e,
0xc78,0x6336001e,
0xc78,0x6437001e,
0xc78,0x6538001e,
0xc78,0x6639001e,
0xc78,0x663a001e,
0xc78,0x673b001e,
0xc78,0x683c001e,
0xc78,0x693d001e,
0xc78,0x6a3e001e,
0xc78,0x6b3f001e,
};

u32 Rtl8190PciPHY_REGArray[PHY_REGArrayLength] = {
0x800,0x00050060,
0x804,0x00000005,
0x808,0x0000fc00,
0x80c,0x0000001c,
0x810,0x801010aa,
0x814,0x000908c0,
0x818,0x00000000,
0x81c,0x00000000,
0x820,0x00000004,
0x824,0x00690000,
0x828,0x00000004,
0x82c,0x00e90000,
0x830,0x00000004,
0x834,0x00690000,
0x838,0x00000004,
0x83c,0x00e90000,
0x840,0x00000000,
0x844,0x00000000,
0x848,0x00000000,
0x84c,0x00000000,
0x850,0x00000000,
0x854,0x00000000,
0x858,0x65a965a9,
0x85c,0x65a965a9,
0x860,0x001f0010,
0x864,0x007f0010,
0x868,0x001f0010,
0x86c,0x007f0010,
0x870,0x0f100f70,
0x874,0x0f100f70,
0x878,0x00000000,
0x87c,0x00000000,
0x880,0x5c385eb8,
0x884,0x6357060d,
0x888,0x0460c341,
0x88c,0x0000ff00,
0x890,0x00000000,
0x894,0xfffffffe,
0x898,0x4c42382f,
0x89c,0x00656056,
0x8b0,0x00000000,
0x8e0,0x00000000,
0x8e4,0x00000000,
0x900,0x00000000,
0x904,0x00000023,
0x908,0x00000000,
0x90c,0x35541545,
0xa00,0x00d0c7d8,
0xa04,0xab1f0008,
0xa08,0x80cd8300,
0xa0c,0x2e62740f,
0xa10,0x95009b78,
0xa14,0x11145008,
0xa18,0x00881117,
0xa1c,0x89140fa0,
0xa20,0x1a1b0000,
0xa24,0x090e1317,
0xa28,0x00000204,
0xa2c,0x00000000,
0xc00,0x00000040,
0xc04,0x0000500f,
0xc08,0x000000e4,
0xc0c,0x6c6c6c6c,
0xc10,0x08000000,
0xc14,0x40000100,
0xc18,0x08000000,
0xc1c,0x40000100,
0xc20,0x08000000,
0xc24,0x40000100,
0xc28,0x08000000,
0xc2c,0x40000100,
0xc30,0x6de9ac44,
0xc34,0x164052cd,
0xc38,0x00070a14,
0xc3c,0x0a969764,
0xc40,0x1f7c403f,
0xc44,0x000100b7,
0xc48,0xec020000,
0xc4c,0x00000300,
0xc50,0x69543420,
0xc54,0x433c0094,
0xc58,0x69543420,
0xc5c,0x433c0094,
0xc60,0x69543420,
0xc64,0x433c0094,
0xc68,0x69543420,
0xc6c,0x433c0094,
0xc70,0x2c7f000d,
0xc74,0x0186175b,
0xc78,0x0000001f,
0xc7c,0x00b91612,
0xc80,0x40000100,
0xc84,0x00000000,
0xc88,0x40000100,
0xc8c,0x08000000,
0xc90,0x40000100,
0xc94,0x00000000,
0xc98,0x40000100,
0xc9c,0x00000000,
0xca0,0x00492492,
0xca4,0x00000000,
0xca8,0x00000000,
0xcac,0x00000000,
0xcb0,0x00000000,
0xcb4,0x00000000,
0xcb8,0x00000000,
0xcbc,0x00492492,
0xcc0,0x00000000,
0xcc4,0x00000000,
0xcc8,0x00000000,
0xccc,0x00000000,
0xcd0,0x00000000,
0xcd4,0x00000000,
0xcd8,0x64b22427,
0xcdc,0x00766932,
0xce0,0x00222222,
0xd00,0x00000740,
0xd04,0x0000040f,
0xd08,0x0000803f,
0xd0c,0x00000001,
0xd10,0xa0633333,
0xd14,0x33333c63,
0xd18,0x6a8f5b6b,
0xd1c,0x00000000,
0xd20,0x00000000,
0xd24,0x00000000,
0xd28,0x00000000,
0xd2c,0xcc979975,
0xd30,0x00000000,
0xd34,0x00000000,
0xd38,0x00000000,
0xd3c,0x00027293,
0xd40,0x00000000,
0xd44,0x00000000,
0xd48,0x00000000,
0xd4c,0x00000000,
0xd50,0x6437140a,
0xd54,0x024dbd02,
0xd58,0x00000000,
0xd5c,0x14032064,
};
u32 Rtl8190PciPHY_REG_1T2RArray[PHY_REG_1T2RArrayLength] = {
0x800,0x00050060,
0x804,0x00000004,
0x808,0x0000fc00,
0x80c,0x0000001c,
0x810,0x801010aa,
0x814,0x000908c0,
0x818,0x00000000,
0x81c,0x00000000,
0x820,0x00000004,
0x824,0x00690000,
0x828,0x00000004,
0x82c,0x00e90000,
0x830,0x00000004,
0x834,0x00690000,
0x838,0x00000004,
0x83c,0x00e90000,
0x840,0x00000000,
0x844,0x00000000,
0x848,0x00000000,
0x84c,0x00000000,
0x850,0x00000000,
0x854,0x00000000,
0x858,0x65a965a9,
0x85c,0x65a965a9,
0x860,0x001f0000,
0x864,0x007f0000,
0x868,0x001f0010,
0x86c,0x007f0010,
0x870,0x0f100f70,
0x874,0x0f100f70,
0x878,0x00000000,
0x87c,0x00000000,
0x880,0x5c385898,
0x884,0x6357060d,
0x888,0x0460c341,
0x88c,0x0000fc00,
0x890,0x00000000,
0x894,0xfffffffe,
0x898,0x4c42382f,
0x89c,0x00656056,
0x8b0,0x00000000,
0x8e0,0x00000000,
0x8e4,0x00000000,
0x900,0x00000000,
0x904,0x00000023,
0x908,0x00000000,
0x90c,0x34441444,
0xa00,0x00d0c7d8,
0xa04,0x2b1f0008,
0xa08,0x80cd8300,
0xa0c,0x2e62740f,
0xa10,0x95009b78,
0xa14,0x11145008,
0xa18,0x00881117,
0xa1c,0x89140fa0,
0xa20,0x1a1b0000,
0xa24,0x090e1317,
0xa28,0x00000204,
0xa2c,0x00000000,
0xc00,0x00000040,
0xc04,0x0000500c,
0xc08,0x000000e4,
0xc0c,0x6c6c6c6c,
0xc10,0x08000000,
0xc14,0x40000100,
0xc18,0x08000000,
0xc1c,0x40000100,
0xc20,0x08000000,
0xc24,0x40000100,
0xc28,0x08000000,
0xc2c,0x40000100,
0xc30,0x6de9ac44,
0xc34,0x164052cd,
0xc38,0x00070a14,
0xc3c,0x0a969764,
0xc40,0x1f7c403f,
0xc44,0x000100b7,
0xc48,0xec020000,
0xc4c,0x00000300,
0xc50,0x69543420,
0xc54,0x433c0094,
0xc58,0x69543420,
0xc5c,0x433c0094,
0xc60,0x69543420,
0xc64,0x433c0094,
0xc68,0x69543420,
0xc6c,0x433c0094,
0xc70,0x2c7f000d,
0xc74,0x0186175b,
0xc78,0x0000001f,
0xc7c,0x00b91612,
0xc80,0x40000100,
0xc84,0x00000000,
0xc88,0x40000100,
0xc8c,0x08000000,
0xc90,0x40000100,
0xc94,0x00000000,
0xc98,0x40000100,
0xc9c,0x00000000,
0xca0,0x00492492,
0xca4,0x00000000,
0xca8,0x00000000,
0xcac,0x00000000,
0xcb0,0x00000000,
0xcb4,0x00000000,
0xcb8,0x00000000,
0xcbc,0x00492492,
0xcc0,0x00000000,
0xcc4,0x00000000,
0xcc8,0x00000000,
0xccc,0x00000000,
0xcd0,0x00000000,
0xcd4,0x00000000,
0xcd8,0x64b22427,
0xcdc,0x00766932,
0xce0,0x00222222,
0xd00,0x00000740,
0xd04,0x0000040c,
0xd08,0x0000803f,
0xd0c,0x00000001,
0xd10,0xa0633333,
0xd14,0x33333c63,
0xd18,0x6a8f5b6b,
0xd1c,0x00000000,
0xd20,0x00000000,
0xd24,0x00000000,
0xd28,0x00000000,
0xd2c,0xcc979975,
0xd30,0x00000000,
0xd34,0x00000000,
0xd38,0x00000000,
0xd3c,0x00027293,
0xd40,0x00000000,
0xd44,0x00000000,
0xd48,0x00000000,
0xd4c,0x00000000,
0xd50,0x6437140a,
0xd54,0x024dbd02,
0xd58,0x00000000,
0xd5c,0x14032064,
};

u32 Rtl8190PciRadioA_Array[RadioA_ArrayLength] = {
0x019,0x00000003,
0x000,0x000000bf,
0x001,0x00000ee0,
0x002,0x0000004c,
0x003,0x000007f1,
0x004,0x00000975,
0x005,0x00000c58,
0x006,0x00000ae6,
0x007,0x000000ca,
0x008,0x00000e1c,
0x009,0x000007f0,
0x00a,0x000009d0,
0x00b,0x000001ba,
0x00c,0x00000240,
0x00e,0x00000020,
0x00f,0x00000990,
0x012,0x00000806,
0x014,0x000005ab,
0x015,0x00000f80,
0x016,0x00000020,
0x017,0x00000597,
0x018,0x0000050a,
0x01a,0x00000f80,
0x01b,0x00000f5e,
0x01c,0x00000008,
0x01d,0x00000607,
0x01e,0x000006cc,
0x01f,0x00000000,
0x020,0x000001a5,
0x01f,0x00000001,
0x020,0x00000165,
0x01f,0x00000002,
0x020,0x000000c6,
0x01f,0x00000003,
0x020,0x00000086,
0x01f,0x00000004,
0x020,0x00000046,
0x01f,0x00000005,
0x020,0x000001e6,
0x01f,0x00000006,
0x020,0x000001a6,
0x01f,0x00000007,
0x020,0x00000166,
0x01f,0x00000008,
0x020,0x000000c7,
0x01f,0x00000009,
0x020,0x00000087,
0x01f,0x0000000a,
0x020,0x000000f7,
0x01f,0x0000000b,
0x020,0x000000d7,
0x01f,0x0000000c,
0x020,0x000000b7,
0x01f,0x0000000d,
0x020,0x00000097,
0x01f,0x0000000e,
0x020,0x00000077,
0x01f,0x0000000f,
0x020,0x00000057,
0x01f,0x00000010,
0x020,0x00000037,
0x01f,0x00000011,
0x020,0x000000fb,
0x01f,0x00000012,
0x020,0x000000db,
0x01f,0x00000013,
0x020,0x000000bb,
0x01f,0x00000014,
0x020,0x000000ff,
0x01f,0x00000015,
0x020,0x000000e3,
0x01f,0x00000016,
0x020,0x000000c3,
0x01f,0x00000017,
0x020,0x000000a3,
0x01f,0x00000018,
0x020,0x00000083,
0x01f,0x00000019,
0x020,0x00000063,
0x01f,0x0000001a,
0x020,0x00000043,
0x01f,0x0000001b,
0x020,0x00000023,
0x01f,0x0000001c,
0x020,0x00000003,
0x01f,0x0000001d,
0x020,0x000001e3,
0x01f,0x0000001e,
0x020,0x000001c3,
0x01f,0x0000001f,
0x020,0x000001a3,
0x01f,0x00000020,
0x020,0x00000183,
0x01f,0x00000021,
0x020,0x00000163,
0x01f,0x00000022,
0x020,0x00000143,
0x01f,0x00000023,
0x020,0x00000123,
0x01f,0x00000024,
0x020,0x00000103,
0x023,0x00000203,
0x024,0x00000200,
0x00b,0x000001ba,
0x02c,0x000003d7,
0x02d,0x00000ff0,
0x000,0x00000037,
0x004,0x00000160,
0x007,0x00000080,
0x002,0x0000088d,
0x0fe,0x00000000,
0x0fe,0x00000000,
0x016,0x00000200,
0x016,0x00000380,
0x016,0x00000020,
0x016,0x000001a0,
0x000,0x000000bf,
0x00d,0x0000001f,
0x00d,0x00000c9f,
0x002,0x0000004d,
0x000,0x00000cbf,
0x004,0x00000975,
0x007,0x00000700,
};
u32 Rtl8190PciRadioB_Array[RadioB_ArrayLength] = {
0x019,0x00000003,
0x000,0x000000bf,
0x001,0x000006e0,
0x002,0x0000004c,
0x003,0x000007f1,
0x004,0x00000975,
0x005,0x00000c58,
0x006,0x00000ae6,
0x007,0x000000ca,
0x008,0x00000e1c,
0x000,0x000000b7,
0x00a,0x00000850,
0x000,0x000000bf,
0x00b,0x000001ba,
0x00c,0x00000240,
0x00e,0x00000020,
0x015,0x00000f80,
0x016,0x00000020,
0x017,0x00000597,
0x018,0x0000050a,
0x01a,0x00000e00,
0x01b,0x00000f5e,
0x01d,0x00000607,
0x01e,0x000006cc,
0x00b,0x000001ba,
0x023,0x00000203,
0x024,0x00000200,
0x000,0x00000037,
0x004,0x00000160,
0x016,0x00000200,
0x016,0x00000380,
0x016,0x00000020,
0x016,0x000001a0,
0x00d,0x00000ccc,
0x000,0x000000bf,
0x002,0x0000004d,
0x000,0x00000cbf,
0x004,0x00000975,
0x007,0x00000700,
};
u32 Rtl8190PciRadioC_Array[RadioC_ArrayLength] = {
0x019,0x00000003,
0x000,0x000000bf,
0x001,0x00000ee0,
0x002,0x0000004c,
0x003,0x000007f1,
0x004,0x00000975,
0x005,0x00000c58,
0x006,0x00000ae6,
0x007,0x000000ca,
0x008,0x00000e1c,
0x009,0x000007f0,
0x00a,0x000009d0,
0x00b,0x000001ba,
0x00c,0x00000240,
0x00e,0x00000020,
0x00f,0x00000990,
0x012,0x00000806,
0x014,0x000005ab,
0x015,0x00000f80,
0x016,0x00000020,
0x017,0x00000597,
0x018,0x0000050a,
0x01a,0x00000f80,
0x01b,0x00000f5e,
0x01c,0x00000008,
0x01d,0x00000607,
0x01e,0x000006cc,
0x01f,0x00000000,
0x020,0x000001a5,
0x01f,0x00000001,
0x020,0x00000165,
0x01f,0x00000002,
0x020,0x000000c6,
0x01f,0x00000003,
0x020,0x00000086,
0x01f,0x00000004,
0x020,0x00000046,
0x01f,0x00000005,
0x020,0x000001e6,
0x01f,0x00000006,
0x020,0x000001a6,
0x01f,0x00000007,
0x020,0x00000166,
0x01f,0x00000008,
0x020,0x000000c7,
0x01f,0x00000009,
0x020,0x00000087,
0x01f,0x0000000a,
0x020,0x000000f7,
0x01f,0x0000000b,
0x020,0x000000d7,
0x01f,0x0000000c,
0x020,0x000000b7,
0x01f,0x0000000d,
0x020,0x00000097,
0x01f,0x0000000e,
0x020,0x00000077,
0x01f,0x0000000f,
0x020,0x00000057,
0x01f,0x00000010,
0x020,0x00000037,
0x01f,0x00000011,
0x020,0x000000fb,
0x01f,0x00000012,
0x020,0x000000db,
0x01f,0x00000013,
0x020,0x000000bb,
0x01f,0x00000014,
0x020,0x000000ff,
0x01f,0x00000015,
0x020,0x000000e3,
0x01f,0x00000016,
0x020,0x000000c3,
0x01f,0x00000017,
0x020,0x000000a3,
0x01f,0x00000018,
0x020,0x00000083,
0x01f,0x00000019,
0x020,0x00000063,
0x01f,0x0000001a,
0x020,0x00000043,
0x01f,0x0000001b,
0x020,0x00000023,
0x01f,0x0000001c,
0x020,0x00000003,
0x01f,0x0000001d,
0x020,0x000001e3,
0x01f,0x0000001e,
0x020,0x000001c3,
0x01f,0x0000001f,
0x020,0x000001a3,
0x01f,0x00000020,
0x020,0x00000183,
0x01f,0x00000021,
0x020,0x00000163,
0x01f,0x00000022,
0x020,0x00000143,
0x01f,0x00000023,
0x020,0x00000123,
0x01f,0x00000024,
0x020,0x00000103,
0x023,0x00000203,
0x024,0x00000200,
0x00b,0x000001ba,
0x02c,0x000003d7,
0x02d,0x00000ff0,
0x000,0x00000037,
0x004,0x00000160,
0x007,0x00000080,
0x002,0x0000088d,
0x0fe,0x00000000,
0x0fe,0x00000000,
0x016,0x00000200,
0x016,0x00000380,
0x016,0x00000020,
0x016,0x000001a0,
0x000,0x000000bf,
0x00d,0x0000001f,
0x00d,0x00000c9f,
0x002,0x0000004d,
0x000,0x00000cbf,
0x004,0x00000975,
0x007,0x00000700,
};
u32 Rtl8190PciRadioD_Array[RadioD_ArrayLength] = {
0x019,0x00000003,
0x000,0x000000bf,
0x001,0x000006e0,
0x002,0x0000004c,
0x003,0x000007f1,
0x004,0x00000975,
0x005,0x00000c58,
0x006,0x00000ae6,
0x007,0x000000ca,
0x008,0x00000e1c,
0x000,0x000000b7,
0x00a,0x00000850,
0x000,0x000000bf,
0x00b,0x000001ba,
0x00c,0x00000240,
0x00e,0x00000020,
0x015,0x00000f80,
0x016,0x00000020,
0x017,0x00000597,
0x018,0x0000050a,
0x01a,0x00000e00,
0x01b,0x00000f5e,
0x01d,0x00000607,
0x01e,0x000006cc,
0x00b,0x000001ba,
0x023,0x00000203,
0x024,0x00000200,
0x000,0x00000037,
0x004,0x00000160,
0x016,0x00000200,
0x016,0x00000380,
0x016,0x00000020,
0x016,0x000001a0,
0x00d,0x00000ccc,
0x000,0x000000bf,
0x002,0x0000004d,
0x000,0x00000cbf,
0x004,0x00000975,
0x007,0x00000700,
};
#endif
#ifdef RTL8192E
static u32 Rtl8192PciEMACPHY_Array[] = {
0x03c,0xffff0000,0x00000f0f,
0x340,0xffffffff,0x161a1a1a,
0x344,0xffffffff,0x12121416,
0x348,0x0000ffff,0x00001818,
0x12c,0xffffffff,0x04000802,
0x318,0x00000fff,0x00000100,
};
static u32 Rtl8192PciEMACPHY_Array_PG[] = {
0x03c,0xffff0000,0x00000f0f,
0xe00,0xffffffff,0x06090909,
0xe04,0xffffffff,0x00030306,
0xe08,0x0000ff00,0x00000000,
0xe10,0xffffffff,0x0a0c0d0f,
0xe14,0xffffffff,0x06070809,
0xe18,0xffffffff,0x0a0c0d0f,
0xe1c,0xffffffff,0x06070809,
0x12c,0xffffffff,0x04000802,
0x318,0x00000fff,0x00000800,
};
static u32 Rtl8192PciEAGCTAB_Array[AGCTAB_ArrayLength] = {
0xc78,0x7d000001,
0xc78,0x7d010001,
0xc78,0x7d020001,
0xc78,0x7d030001,
0xc78,0x7d040001,
0xc78,0x7d050001,
0xc78,0x7c060001,
0xc78,0x7b070001,
0xc78,0x7a080001,
0xc78,0x79090001,
0xc78,0x780a0001,
0xc78,0x770b0001,
0xc78,0x760c0001,
0xc78,0x750d0001,
0xc78,0x740e0001,
0xc78,0x730f0001,
0xc78,0x72100001,
0xc78,0x71110001,
0xc78,0x70120001,
0xc78,0x6f130001,
0xc78,0x6e140001,
0xc78,0x6d150001,
0xc78,0x6c160001,
0xc78,0x6b170001,
0xc78,0x6a180001,
0xc78,0x69190001,
0xc78,0x681a0001,
0xc78,0x671b0001,
0xc78,0x661c0001,
0xc78,0x651d0001,
0xc78,0x641e0001,
0xc78,0x491f0001,
0xc78,0x48200001,
0xc78,0x47210001,
0xc78,0x46220001,
0xc78,0x45230001,
0xc78,0x44240001,
0xc78,0x43250001,
0xc78,0x28260001,
0xc78,0x27270001,
0xc78,0x26280001,
0xc78,0x25290001,
0xc78,0x242a0001,
0xc78,0x232b0001,
0xc78,0x222c0001,
0xc78,0x212d0001,
0xc78,0x202e0001,
0xc78,0x0a2f0001,
0xc78,0x08300001,
0xc78,0x06310001,
0xc78,0x05320001,
0xc78,0x04330001,
0xc78,0x03340001,
0xc78,0x02350001,
0xc78,0x01360001,
0xc78,0x00370001,
0xc78,0x00380001,
0xc78,0x00390001,
0xc78,0x003a0001,
0xc78,0x003b0001,
0xc78,0x003c0001,
0xc78,0x003d0001,
0xc78,0x003e0001,
0xc78,0x003f0001,
0xc78,0x7d400001,
0xc78,0x7d410001,
0xc78,0x7d420001,
0xc78,0x7d430001,
0xc78,0x7d440001,
0xc78,0x7d450001,
0xc78,0x7c460001,
0xc78,0x7b470001,
0xc78,0x7a480001,
0xc78,0x79490001,
0xc78,0x784a0001,
0xc78,0x774b0001,
0xc78,0x764c0001,
0xc78,0x754d0001,
0xc78,0x744e0001,
0xc78,0x734f0001,
0xc78,0x72500001,
0xc78,0x71510001,
0xc78,0x70520001,
0xc78,0x6f530001,
0xc78,0x6e540001,
0xc78,0x6d550001,
0xc78,0x6c560001,
0xc78,0x6b570001,
0xc78,0x6a580001,
0xc78,0x69590001,
0xc78,0x685a0001,
0xc78,0x675b0001,
0xc78,0x665c0001,
0xc78,0x655d0001,
0xc78,0x645e0001,
0xc78,0x495f0001,
0xc78,0x48600001,
0xc78,0x47610001,
0xc78,0x46620001,
0xc78,0x45630001,
0xc78,0x44640001,
0xc78,0x43650001,
0xc78,0x28660001,
0xc78,0x27670001,
0xc78,0x26680001,
0xc78,0x25690001,
0xc78,0x246a0001,
0xc78,0x236b0001,
0xc78,0x226c0001,
0xc78,0x216d0001,
0xc78,0x206e0001,
0xc78,0x0a6f0001,
0xc78,0x08700001,
0xc78,0x06710001,
0xc78,0x05720001,
0xc78,0x04730001,
0xc78,0x03740001,
0xc78,0x02750001,
0xc78,0x01760001,
0xc78,0x00770001,
0xc78,0x00780001,
0xc78,0x00790001,
0xc78,0x007a0001,
0xc78,0x007b0001,
0xc78,0x007c0001,
0xc78,0x007d0001,
0xc78,0x007e0001,
0xc78,0x007f0001,
0xc78,0x2e00001e,
0xc78,0x2e01001e,
0xc78,0x2e02001e,
0xc78,0x2e03001e,
0xc78,0x2e04001e,
0xc78,0x2e05001e,
0xc78,0x3006001e,
0xc78,0x3407001e,
0xc78,0x3908001e,
0xc78,0x3c09001e,
0xc78,0x3f0a001e,
0xc78,0x420b001e,
0xc78,0x440c001e,
0xc78,0x450d001e,
0xc78,0x460e001e,
0xc78,0x460f001e,
0xc78,0x4710001e,
0xc78,0x4811001e,
0xc78,0x4912001e,
0xc78,0x4a13001e,
0xc78,0x4b14001e,
0xc78,0x4b15001e,
0xc78,0x4c16001e,
0xc78,0x4d17001e,
0xc78,0x4e18001e,
0xc78,0x4f19001e,
0xc78,0x4f1a001e,
0xc78,0x501b001e,
0xc78,0x511c001e,
0xc78,0x521d001e,
0xc78,0x521e001e,
0xc78,0x531f001e,
0xc78,0x5320001e,
0xc78,0x5421001e,
0xc78,0x5522001e,
0xc78,0x5523001e,
0xc78,0x5624001e,
0xc78,0x5725001e,
0xc78,0x5726001e,
0xc78,0x5827001e,
0xc78,0x5828001e,
0xc78,0x5929001e,
0xc78,0x592a001e,
0xc78,0x5a2b001e,
0xc78,0x5b2c001e,
0xc78,0x5c2d001e,
0xc78,0x5c2e001e,
0xc78,0x5d2f001e,
0xc78,0x5e30001e,
0xc78,0x5f31001e,
0xc78,0x6032001e,
0xc78,0x6033001e,
0xc78,0x6134001e,
0xc78,0x6235001e,
0xc78,0x6336001e,
0xc78,0x6437001e,
0xc78,0x6438001e,
0xc78,0x6539001e,
0xc78,0x663a001e,
0xc78,0x673b001e,
0xc78,0x673c001e,
0xc78,0x683d001e,
0xc78,0x693e001e,
0xc78,0x6a3f001e,
};
static u32 Rtl8192PciEPHY_REGArray[PHY_REGArrayLength] = {
0x0, };
static u32 Rtl8192PciEPHY_REG_1T2RArray[PHY_REG_1T2RArrayLength] = {
0x800,0x00000000,
0x804,0x00000001,
0x808,0x0000fc00,
0x80c,0x0000001c,
0x810,0x801010aa,
0x814,0x008514d0,
0x818,0x00000040,
0x81c,0x00000000,
0x820,0x00000004,
0x824,0x00690000,
0x828,0x00000004,
0x82c,0x00e90000,
0x830,0x00000004,
0x834,0x00690000,
0x838,0x00000004,
0x83c,0x00e90000,
0x840,0x00000000,
0x844,0x00000000,
0x848,0x00000000,
0x84c,0x00000000,
0x850,0x00000000,
0x854,0x00000000,
0x858,0x65a965a9,
0x85c,0x65a965a9,
0x860,0x001f0010,
0x864,0x007f0010,
0x868,0x001f0010,
0x86c,0x007f0010,
0x870,0x0f100f70,
0x874,0x0f100f70,
0x878,0x00000000,
0x87c,0x00000000,
0x880,0x6870e36c,
0x884,0xe3573600,
0x888,0x4260c340,
0x88c,0x0000ff00,
0x890,0x00000000,
0x894,0xfffffffe,
0x898,0x4c42382f,
0x89c,0x00656056,
0x8b0,0x00000000,
0x8e0,0x00000000,
0x8e4,0x00000000,
0x900,0x00000000,
0x904,0x00000023,
0x908,0x00000000,
0x90c,0x31121311,
0xa00,0x00d0c7d8,
0xa04,0x811f0008,
0xa08,0x80cd8300,
0xa0c,0x2e62740f,
0xa10,0x95009b78,
0xa14,0x11145008,
0xa18,0x00881117,
0xa1c,0x89140fa0,
0xa20,0x1a1b0000,
0xa24,0x090e1317,
0xa28,0x00000204,
0xa2c,0x00000000,
0xc00,0x00000040,
0xc04,0x00005433,
0xc08,0x000000e4,
0xc0c,0x6c6c6c6c,
0xc10,0x08800000,
0xc14,0x40000100,
0xc18,0x08000000,
0xc1c,0x40000100,
0xc20,0x08000000,
0xc24,0x40000100,
0xc28,0x08000000,
0xc2c,0x40000100,
0xc30,0x6de9ac44,
0xc34,0x465c52cd,
0xc38,0x497f5994,
0xc3c,0x0a969764,
0xc40,0x1f7c403f,
0xc44,0x000100b7,
0xc48,0xec020000,
0xc4c,0x00000300,
0xc50,0x69543420,
0xc54,0x433c0094,
0xc58,0x69543420,
0xc5c,0x433c0094,
0xc60,0x69543420,
0xc64,0x433c0094,
0xc68,0x69543420,
0xc6c,0x433c0094,
0xc70,0x2c7f000d,
0xc74,0x0186175b,
0xc78,0x0000001f,
0xc7c,0x00b91612,
0xc80,0x40000100,
0xc84,0x20000000,
0xc88,0x40000100,
0xc8c,0x20200000,
0xc90,0x40000100,
0xc94,0x00000000,
0xc98,0x40000100,
0xc9c,0x00000000,
0xca0,0x00492492,
0xca4,0x00000000,
0xca8,0x00000000,
0xcac,0x00000000,
0xcb0,0x00000000,
0xcb4,0x00000000,
0xcb8,0x00000000,
0xcbc,0x00492492,
0xcc0,0x00000000,
0xcc4,0x00000000,
0xcc8,0x00000000,
0xccc,0x00000000,
0xcd0,0x00000000,
0xcd4,0x00000000,
0xcd8,0x64b22427,
0xcdc,0x00766932,
0xce0,0x00222222,
0xd00,0x00000750,
0xd04,0x00000403,
0xd08,0x0000907f,
0xd0c,0x00000001,
0xd10,0xa0633333,
0xd14,0x33333c63,
0xd18,0x6a8f5b6b,
0xd1c,0x00000000,
0xd20,0x00000000,
0xd24,0x00000000,
0xd28,0x00000000,
0xd2c,0xcc979975,
0xd30,0x00000000,
0xd34,0x00000000,
0xd38,0x00000000,
0xd3c,0x00027293,
0xd40,0x00000000,
0xd44,0x00000000,
0xd48,0x00000000,
0xd4c,0x00000000,
0xd50,0x6437140a,
0xd54,0x024dbd02,
0xd58,0x00000000,
0xd5c,0x04032064,
0xe00,0x161a1a1a,
0xe04,0x12121416,
0xe08,0x00001800,
0xe0c,0x00000000,
0xe10,0x161a1a1a,
0xe14,0x12121416,
0xe18,0x161a1a1a,
0xe1c,0x12121416,
};
static u32 Rtl8192PciERadioA_Array[RadioA_ArrayLength] = {
0x019,0x00000003,
0x000,0x000000bf,
0x001,0x00000ee0,
0x002,0x0000004c,
0x003,0x000007f1,
0x004,0x00000975,
0x005,0x00000c58,
0x006,0x00000ae6,
0x007,0x000000ca,
0x008,0x00000e1c,
0x009,0x000007f0,
0x00a,0x000009d0,
0x00b,0x000001ba,
0x00c,0x00000240,
0x00e,0x00000020,
0x00f,0x00000990,
0x012,0x00000806,
0x014,0x000005ab,
0x015,0x00000f80,
0x016,0x00000020,
0x017,0x00000597,
0x018,0x0000050a,
0x01a,0x00000f80,
0x01b,0x00000f5e,
0x01c,0x00000008,
0x01d,0x00000607,
0x01e,0x000006cc,
0x01f,0x00000000,
0x020,0x000001a5,
0x01f,0x00000001,
0x020,0x00000165,
0x01f,0x00000002,
0x020,0x000000c6,
0x01f,0x00000003,
0x020,0x00000086,
0x01f,0x00000004,
0x020,0x00000046,
0x01f,0x00000005,
0x020,0x000001e6,
0x01f,0x00000006,
0x020,0x000001a6,
0x01f,0x00000007,
0x020,0x00000166,
0x01f,0x00000008,
0x020,0x000000c7,
0x01f,0x00000009,
0x020,0x00000087,
0x01f,0x0000000a,
0x020,0x000000f7,
0x01f,0x0000000b,
0x020,0x000000d7,
0x01f,0x0000000c,
0x020,0x000000b7,
0x01f,0x0000000d,
0x020,0x00000097,
0x01f,0x0000000e,
0x020,0x00000077,
0x01f,0x0000000f,
0x020,0x00000057,
0x01f,0x00000010,
0x020,0x00000037,
0x01f,0x00000011,
0x020,0x000000fb,
0x01f,0x00000012,
0x020,0x000000db,
0x01f,0x00000013,
0x020,0x000000bb,
0x01f,0x00000014,
0x020,0x000000ff,
0x01f,0x00000015,
0x020,0x000000e3,
0x01f,0x00000016,
0x020,0x000000c3,
0x01f,0x00000017,
0x020,0x000000a3,
0x01f,0x00000018,
0x020,0x00000083,
0x01f,0x00000019,
0x020,0x00000063,
0x01f,0x0000001a,
0x020,0x00000043,
0x01f,0x0000001b,
0x020,0x00000023,
0x01f,0x0000001c,
0x020,0x00000003,
0x01f,0x0000001d,
0x020,0x000001e3,
0x01f,0x0000001e,
0x020,0x000001c3,
0x01f,0x0000001f,
0x020,0x000001a3,
0x01f,0x00000020,
0x020,0x00000183,
0x01f,0x00000021,
0x020,0x00000163,
0x01f,0x00000022,
0x020,0x00000143,
0x01f,0x00000023,
0x020,0x00000123,
0x01f,0x00000024,
0x020,0x00000103,
0x023,0x00000203,
0x024,0x00000100,
0x00b,0x000001ba,
0x02c,0x000003d7,
0x02d,0x00000ff0,
0x000,0x00000037,
0x004,0x00000160,
0x007,0x00000080,
0x002,0x0000088d,
0x0fe,0x00000000,
0x0fe,0x00000000,
0x016,0x00000200,
0x016,0x00000380,
0x016,0x00000020,
0x016,0x000001a0,
0x000,0x000000bf,
0x00d,0x0000001f,
0x00d,0x00000c9f,
0x002,0x0000004d,
0x000,0x00000cbf,
0x004,0x00000975,
0x007,0x00000700,
};
static u32 Rtl8192PciERadioB_Array[RadioB_ArrayLength] = {
0x019,0x00000003,
0x000,0x000000bf,
0x001,0x000006e0,
0x002,0x0000004c,
0x003,0x000007f1,
0x004,0x00000975,
0x005,0x00000c58,
0x006,0x00000ae6,
0x007,0x000000ca,
0x008,0x00000e1c,
0x000,0x000000b7,
0x00a,0x00000850,
0x000,0x000000bf,
0x00b,0x000001ba,
0x00c,0x00000240,
0x00e,0x00000020,
0x015,0x00000f80,
0x016,0x00000020,
0x017,0x00000597,
0x018,0x0000050a,
0x01a,0x00000e00,
0x01b,0x00000f5e,
0x01d,0x00000607,
0x01e,0x000006cc,
0x00b,0x000001ba,
0x023,0x00000203,
0x024,0x00000100,
0x000,0x00000037,
0x004,0x00000160,
0x016,0x00000200,
0x016,0x00000380,
0x016,0x00000020,
0x016,0x000001a0,
0x00d,0x00000ccc,
0x000,0x000000bf,
0x002,0x0000004d,
0x000,0x00000cbf,
0x004,0x00000975,
0x007,0x00000700,
};
static u32 Rtl8192PciERadioC_Array[RadioC_ArrayLength] = {
0x0,  };
static u32 Rtl8192PciERadioD_Array[RadioD_ArrayLength] = {
0x0, };
#endif

/*************************Define local function prototype**********************/

static u32 phy_FwRFSerialRead(struct net_device* dev,RF90_RADIO_PATH_E	eRFPath,u32 Offset);
static void phy_FwRFSerialWrite(struct net_device* dev,RF90_RADIO_PATH_E eRFPath,u32 Offset,u32	Data);
/*************************Define local function prototype**********************/
/******************************************************************************
 *function:  This function read BB parameters from Header file we gen,
 *	     and do register read/write
 *   input:  u32	dwBitMask  //taget bit pos in the addr to be modified
 *  output:  none
 *  return:  u32	return the shift bit bit position of the mask
 * ****************************************************************************/
static u32 rtl8192_CalculateBitShift(u32 dwBitMask)
{
	u32 i;
	for (i=0; i<=31; i++)
	{
		if (((dwBitMask>>i)&0x1) == 1)
			break;
	}
	return i;
}
/******************************************************************************
 *function:  This function check different RF type to execute legal judgement. If RF Path is illegal, we will return false.
 *   input:  none
 *  output:  none
 *  return:  0(illegal, false), 1(legal,true)
 * ***************************************************************************/
u8 rtl8192_phy_CheckIsLegalRFPath(struct net_device* dev, u32 eRFPath)
{
	u8 ret = 1;
	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef RTL8190P
	if(priv->rf_type == RF_2T4R)
	{
		ret= 1;
	}
	else if (priv->rf_type == RF_1T2R)
	{
		if(eRFPath == RF90_PATH_A || eRFPath == RF90_PATH_B)
			ret = 0;
		else if(eRFPath == RF90_PATH_C || eRFPath == RF90_PATH_D)
			ret =  1;
	}
#else
	#ifdef RTL8192E
	if (priv->rf_type == RF_2T4R)
		ret = 0;
	else if (priv->rf_type == RF_1T2R)
	{
		if (eRFPath == RF90_PATH_A || eRFPath == RF90_PATH_B)
			ret = 1;
		else if (eRFPath == RF90_PATH_C || eRFPath == RF90_PATH_D)
			ret = 0;
	}
	#endif
#endif
	return ret;
}
/******************************************************************************
 *function:  This function set specific bits to BB register
 *   input:  net_device dev
 *           u32	dwRegAddr  //target addr to be modified
 *           u32	dwBitMask  //taget bit pos in the addr to be modified
 *           u32	dwData     //value to be write
 *  output:  none
 *  return:  none
 *  notice:
 * ****************************************************************************/
void rtl8192_setBBreg(struct net_device* dev, u32 dwRegAddr, u32 dwBitMask, u32 dwData)
{

	u32 OriginalValue, BitShift, NewValue;

	if(dwBitMask!= bMaskDWord)
	{//if not "double word" write
		OriginalValue = read_nic_dword(dev, dwRegAddr);
		BitShift = rtl8192_CalculateBitShift(dwBitMask);
            	NewValue = (((OriginalValue) & (~dwBitMask)) | (dwData << BitShift));
		write_nic_dword(dev, dwRegAddr, NewValue);
	}else
		write_nic_dword(dev, dwRegAddr, dwData);
	return;
}
/******************************************************************************
 *function:  This function reads specific bits from BB register
 *   input:  net_device dev
 *           u32	dwRegAddr  //target addr to be readback
 *           u32	dwBitMask  //taget bit pos in the addr to be readback
 *  output:  none
 *  return:  u32	Data	//the readback register value
 *  notice:
 * ****************************************************************************/
u32 rtl8192_QueryBBReg(struct net_device* dev, u32 dwRegAddr, u32 dwBitMask)
{
	u32 Ret = 0, OriginalValue, BitShift;

	OriginalValue = read_nic_dword(dev, dwRegAddr);
	BitShift = rtl8192_CalculateBitShift(dwBitMask);
	Ret = (OriginalValue & dwBitMask) >> BitShift;

	return (Ret);
}
/******************************************************************************
 *function:  This function read register from RF chip
 *   input:  net_device dev
 *   	     RF90_RADIO_PATH_E eRFPath //radio path of A/B/C/D
 *           u32	Offset     //target address to be read
 *  output:  none
 *  return:  u32 	readback value
 *  notice:  There are three types of serial operations:(1) Software serial write.(2)Hardware LSSI-Low Speed Serial Interface.(3)Hardware HSSI-High speed serial write. Driver here need to implement (1) and (2)---need more spec for this information.
 * ****************************************************************************/
static u32 rtl8192_phy_RFSerialRead(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 Offset)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 ret = 0;
	u32 NewOffset = 0;
	BB_REGISTER_DEFINITION_T* pPhyReg = &priv->PHYRegDef[eRFPath];
	//rtl8192_setBBreg(dev, pPhyReg->rfLSSIReadBack, bLSSIReadBackData, 0);
	//make sure RF register offset is correct
	Offset &= 0x3f;

	//switch page for 8256 RF IC
	if (priv->rf_chip == RF_8256)
	{
#ifdef RTL8190P
		//analog to digital off, for protection
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);// 0x88c[11:8]
#else
	#ifdef RTL8192E
		//analog to digital off, for protection
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);// 0x88c[11:8]
	#endif
#endif
		if (Offset >= 31)
		{
			priv->RfReg0Value[eRFPath] |= 0x140;
			//Switch to Reg_Mode2 for Reg 31-45
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset, bMaskDWord, (priv->RfReg0Value[eRFPath]<<16) );
			//modify offset
			NewOffset = Offset -30;
		}
		else if (Offset >= 16)
		{
			priv->RfReg0Value[eRFPath] |= 0x100;
			priv->RfReg0Value[eRFPath] &= (~0x40);
			//Switch to Reg_Mode 1 for Reg16-30
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset, bMaskDWord, (priv->RfReg0Value[eRFPath]<<16) );

			NewOffset = Offset - 15;
		}
		else
			NewOffset = Offset;
	}
	else
	{
		RT_TRACE((COMP_PHY|COMP_ERR), "check RF type here, need to be 8256\n");
		NewOffset = Offset;
	}
	//put desired read addr to LSSI control Register
	rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, bLSSIReadAddress, NewOffset);
	//Issue a posedge trigger
	//
	rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x0);
	rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x1);


	// TODO: we should not delay such a  long time. Ask help from SD3
	msleep(1);

	ret = rtl8192_QueryBBReg(dev, pPhyReg->rfLSSIReadBack, bLSSIReadBackData);


	// Switch back to Reg_Mode0;
	if(priv->rf_chip == RF_8256)
	{
		priv->RfReg0Value[eRFPath] &= 0xebf;

		rtl8192_setBBreg(
			dev,
			pPhyReg->rf3wireOffset,
			bMaskDWord,
			(priv->RfReg0Value[eRFPath] << 16));

#ifdef RTL8190P
		if(priv->rf_type == RF_2T4R)
		{
			//analog to digital on
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00, 0xf);// 0x88c[11:8]
		}
		else if(priv->rf_type == RF_1T2R)
		{
			//analog to digital on
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xc00, 0x3);// 0x88c[11:10]
		}
#else
	#ifdef RTL8192E
		//analog to digital on
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0x300, 0x3);// 0x88c[9:8]
	#endif
#endif
	}


	return ret;

}

/******************************************************************************
 *function:  This function write data to RF register
 *   input:  net_device dev
 *   	     RF90_RADIO_PATH_E eRFPath //radio path of A/B/C/D
 *           u32	Offset     //target address to be written
 *           u32	Data	//The new register data to be written
 *  output:  none
 *  return:  none
 *  notice:  For RF8256 only.
  ===========================================================
 *Reg Mode	RegCTL[1]	RegCTL[0]		Note
 *		(Reg00[12])	(Reg00[10])
 *===========================================================
 *Reg_Mode0	0		x			Reg 0 ~15(0x0 ~ 0xf)
 *------------------------------------------------------------------
 *Reg_Mode1	1		0			Reg 16 ~30(0x1 ~ 0xf)
 *------------------------------------------------------------------
 * Reg_Mode2	1		1			Reg 31 ~ 45(0x1 ~ 0xf)
 *------------------------------------------------------------------
 * ****************************************************************************/
static void rtl8192_phy_RFSerialWrite(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 Offset, u32 Data)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 DataAndAddr = 0, NewOffset = 0;
	BB_REGISTER_DEFINITION_T	*pPhyReg = &priv->PHYRegDef[eRFPath];

	Offset &= 0x3f;
	if (priv->rf_chip == RF_8256)
	{

#ifdef RTL8190P
		//analog to digital off, for protection
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);// 0x88c[11:8]
#else
	#ifdef RTL8192E
		//analog to digital off, for protection
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);// 0x88c[11:8]
	#endif
#endif

		if (Offset >= 31)
		{
			priv->RfReg0Value[eRFPath] |= 0x140;
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset, bMaskDWord, (priv->RfReg0Value[eRFPath] << 16));
			NewOffset = Offset - 30;
		}
		else if (Offset >= 16)
		{
			priv->RfReg0Value[eRFPath] |= 0x100;
			priv->RfReg0Value[eRFPath] &= (~0x40);
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset, bMaskDWord, (priv->RfReg0Value[eRFPath]<<16));
			NewOffset = Offset - 15;
		}
		else
			NewOffset = Offset;
	}
	else
	{
		RT_TRACE((COMP_PHY|COMP_ERR), "check RF type here, need to be 8256\n");
		NewOffset = Offset;
	}

	// Put write addr in [5:0]  and write data in [31:16]
	DataAndAddr = (Data<<16) | (NewOffset&0x3f);

	// Write Operation
	rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);


	if(Offset==0x0)
		priv->RfReg0Value[eRFPath] = Data;

	// Switch back to Reg_Mode0;
 	if(priv->rf_chip == RF_8256)
	{
		if(Offset != 0)
		{
			priv->RfReg0Value[eRFPath] &= 0xebf;
			rtl8192_setBBreg(
				dev,
				pPhyReg->rf3wireOffset,
				bMaskDWord,
				(priv->RfReg0Value[eRFPath] << 16));
		}
#ifdef RTL8190P
		if(priv->rf_type == RF_2T4R)
		{
			//analog to digital on
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00, 0xf);// 0x88c[11:8]
		}
		else if(priv->rf_type == RF_1T2R)
		{
			//analog to digital on
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xc00, 0x3);// 0x88c[11:10]
		}
#else
	#ifdef RTL8192E
		//analog to digital on
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0x300, 0x3);// 0x88c[9:8]
	#endif
#endif
	}

	return;
}

/******************************************************************************
 *function:  This function set specific bits to RF register
 *   input:  net_device dev
 *   	     RF90_RADIO_PATH_E eRFPath //radio path of A/B/C/D
 *           u32	RegAddr  //target addr to be modified
 *           u32	BitMask  //taget bit pos in the addr to be modified
 *           u32	Data     //value to be write
 *  output:  none
 *  return:  none
 *  notice:
 * ****************************************************************************/
void rtl8192_phy_SetRFReg(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 Original_Value, BitShift, New_Value;
//	u8	time = 0;

	if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
		return;
#ifdef RTL8192E
	if(priv->ieee80211->eRFPowerState != eRfOn && !priv->being_init_adapter)
		return;
#endif
	//spin_lock_irqsave(&priv->rf_lock, flags);
	//down(&priv->rf_sem);

	RT_TRACE(COMP_PHY, "FW RF CTRL is not ready now\n");
	if (priv->Rf_Mode == RF_OP_By_FW)
	{
		if (BitMask != bMask12Bits) // RF data is 12 bits only
		{
			Original_Value = phy_FwRFSerialRead(dev, eRFPath, RegAddr);
			BitShift =  rtl8192_CalculateBitShift(BitMask);
			New_Value = (((Original_Value) & (~BitMask)) | (Data<< BitShift));

			phy_FwRFSerialWrite(dev, eRFPath, RegAddr, New_Value);
		}else
			phy_FwRFSerialWrite(dev, eRFPath, RegAddr, Data);
		udelay(200);

	}
	else
	{
		if (BitMask != bMask12Bits) // RF data is 12 bits only
   	        {
			Original_Value = rtl8192_phy_RFSerialRead(dev, eRFPath, RegAddr);
      			BitShift =  rtl8192_CalculateBitShift(BitMask);
      			New_Value = (((Original_Value) & (~BitMask)) | (Data<< BitShift));

			rtl8192_phy_RFSerialWrite(dev, eRFPath, RegAddr, New_Value);
	        }else
			rtl8192_phy_RFSerialWrite(dev, eRFPath, RegAddr, Data);
	}
	//spin_unlock_irqrestore(&priv->rf_lock, flags);
	//up(&priv->rf_sem);
	return;
}

/******************************************************************************
 *function:  This function reads specific bits from RF register
 *   input:  net_device dev
 *           u32	RegAddr  //target addr to be readback
 *           u32	BitMask  //taget bit pos in the addr to be readback
 *  output:  none
 *  return:  u32	Data	//the readback register value
 *  notice:
 * ****************************************************************************/
u32 rtl8192_phy_QueryRFReg(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask)
{
	u32 Original_Value, Readback_Value, BitShift;
	struct r8192_priv *priv = ieee80211_priv(dev);
	if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
		return 0;
#ifdef RTL8192E
	if(priv->ieee80211->eRFPowerState != eRfOn && !priv->being_init_adapter)
		return	0;
#endif
	down(&priv->rf_sem);
	if (priv->Rf_Mode == RF_OP_By_FW)
	{
		Original_Value = phy_FwRFSerialRead(dev, eRFPath, RegAddr);
		udelay(200);
	}
	else
	{
		Original_Value = rtl8192_phy_RFSerialRead(dev, eRFPath, RegAddr);

	}
	BitShift =  rtl8192_CalculateBitShift(BitMask);
   	Readback_Value = (Original_Value & BitMask) >> BitShift;
	up(&priv->rf_sem);
//	udelay(200);
	return (Readback_Value);
}

/******************************************************************************
 *function:  We support firmware to execute RF-R/W.
 *   input:  dev
 *  output:  none
 *  return:  none
 *  notice:
 * ***************************************************************************/
static u32 phy_FwRFSerialRead(
	struct net_device* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32				Offset	)
{
	u32		retValue = 0;
	u32		Data = 0;
	u8		time = 0;
	//DbgPrint("FW RF CTRL\n\r");
	/* 2007/11/02 MH Firmware RF Write control. By Francis' suggestion, we can
	   not execute the scheme in the initial step. Otherwise, RF-R/W will waste
	   much time. This is only for site survey. */
	// 1. Read operation need not insert data. bit 0-11
	//Data &= bMask12Bits;
	// 2. Write RF register address. Bit 12-19
	Data |= ((Offset&0xFF)<<12);
	// 3. Write RF path.  bit 20-21
	Data |= ((eRFPath&0x3)<<20);
	// 4. Set RF read indicator. bit 22=0
	//Data |= 0x00000;
	// 5. Trigger Fw to operate the command. bit 31
	Data |= 0x80000000;
	// 6. We can not execute read operation if bit 31 is 1.
	while (read_nic_dword(dev, QPNR)&0x80000000)
	{
		// If FW can not finish RF-R/W for more than ?? times. We must reset FW.
		if (time++ < 100)
		{
			//DbgPrint("FW not finish RF-R Time=%d\n\r", time);
			udelay(10);
		}
		else
			break;
	}
	// 7. Execute read operation.
	write_nic_dword(dev, QPNR, Data);
	// 8. Check if firmawre send back RF content.
	while (read_nic_dword(dev, QPNR)&0x80000000)
	{
		// If FW can not finish RF-R/W for more than ?? times. We must reset FW.
		if (time++ < 100)
		{
			//DbgPrint("FW not finish RF-W Time=%d\n\r", time);
			udelay(10);
		}
		else
			return	(0);
	}
	retValue = read_nic_dword(dev, RF_DATA);

	return	(retValue);

}	/* phy_FwRFSerialRead */

/******************************************************************************
 *function:  We support firmware to execute RF-R/W.
 *   input:  dev
 *  output:  none
 *  return:  none
 *  notice:
 * ***************************************************************************/
static void
phy_FwRFSerialWrite(
		struct net_device* dev,
		RF90_RADIO_PATH_E	eRFPath,
		u32				Offset,
		u32				Data	)
{
	u8	time = 0;

	//DbgPrint("N FW RF CTRL RF-%d OF%02x DATA=%03x\n\r", eRFPath, Offset, Data);
	/* 2007/11/02 MH Firmware RF Write control. By Francis' suggestion, we can
	   not execute the scheme in the initial step. Otherwise, RF-R/W will waste
	   much time. This is only for site survey. */

	// 1. Set driver write bit and 12 bit data. bit 0-11
	//Data &= bMask12Bits;	// Done by uper layer.
	// 2. Write RF register address. bit 12-19
	Data |= ((Offset&0xFF)<<12);
	// 3. Write RF path.  bit 20-21
	Data |= ((eRFPath&0x3)<<20);
	// 4. Set RF write indicator. bit 22=1
	Data |= 0x400000;
	// 5. Trigger Fw to operate the command. bit 31=1
	Data |= 0x80000000;

	// 6. Write operation. We can not write if bit 31 is 1.
	while (read_nic_dword(dev, QPNR)&0x80000000)
	{
		// If FW can not finish RF-R/W for more than ?? times. We must reset FW.
		if (time++ < 100)
		{
			//DbgPrint("FW not finish RF-W Time=%d\n\r", time);
			udelay(10);
		}
		else
			break;
	}
	// 7. No matter check bit. We always force the write. Because FW will
	//    not accept the command.
	write_nic_dword(dev, QPNR, Data);
	/* 2007/11/02 MH Acoording to test, we must delay 20us to wait firmware
	   to finish RF write operation. */
	/* 2008/01/17 MH We support delay in firmware side now. */
	//delay_us(20);

}	/* phy_FwRFSerialWrite */


/******************************************************************************
 *function:  This function read BB parameters from Header file we gen,
 *	     and do register read/write
 *   input:  dev
 *  output:  none
 *  return:  none
 *  notice:  BB parameters may change all the time, so please make
 *           sure it has been synced with the newest.
 * ***************************************************************************/
void rtl8192_phy_configmac(struct net_device* dev)
{
	u32 dwArrayLen = 0, i = 0;
	u32* pdwArray = NULL;
	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef TO_DO_LIST
if(Adapter->bInHctTest)
	{
		RT_TRACE(COMP_PHY, "Rtl819XMACPHY_ArrayDTM\n");
		dwArrayLen = MACPHY_ArrayLengthDTM;
		pdwArray = Rtl819XMACPHY_ArrayDTM;
	}
	else if(priv->bTXPowerDataReadFromEEPORM)
#endif
	 if(priv->bTXPowerDataReadFromEEPORM)
	{
		RT_TRACE(COMP_PHY, "Rtl819XMACPHY_Array_PG\n");
		dwArrayLen = MACPHY_Array_PGLength;
		pdwArray = Rtl819XMACPHY_Array_PG;

	}
	else
	{
		RT_TRACE(COMP_PHY,"Read rtl819XMACPHY_Array\n");
		dwArrayLen = MACPHY_ArrayLength;
		pdwArray = Rtl819XMACPHY_Array;
	}
	for(i = 0; i<dwArrayLen; i=i+3){
		RT_TRACE(COMP_DBG, "The Rtl8190MACPHY_Array[0] is %x Rtl8190MACPHY_Array[1] is %x Rtl8190MACPHY_Array[2] is %x\n",
				pdwArray[i], pdwArray[i+1], pdwArray[i+2]);
		if(pdwArray[i] == 0x318)
		{
			pdwArray[i+2] = 0x00000800;
			//DbgPrint("ptrArray[i], ptrArray[i+1], ptrArray[i+2] = %x, %x, %x\n",
			//	ptrArray[i], ptrArray[i+1], ptrArray[i+2]);
		}
		rtl8192_setBBreg(dev, pdwArray[i], pdwArray[i+1], pdwArray[i+2]);
	}
	return;

}

/******************************************************************************
 *function:  This function do dirty work
 *   input:  dev
 *  output:  none
 *  return:  none
 *  notice:  BB parameters may change all the time, so please make
 *           sure it has been synced with the newest.
 * ***************************************************************************/

void rtl8192_phyConfigBB(struct net_device* dev, u8 ConfigType)
{
	int i;
	//u8 ArrayLength;
	u32*	Rtl819XPHY_REGArray_Table = NULL;
	u32*	Rtl819XAGCTAB_Array_Table = NULL;
	u16	AGCTAB_ArrayLen, PHY_REGArrayLen = 0;
	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef TO_DO_LIST
	u32 *rtl8192PhyRegArrayTable = NULL, *rtl8192AgcTabArrayTable = NULL;
	if(Adapter->bInHctTest)
	{
		AGCTAB_ArrayLen = AGCTAB_ArrayLengthDTM;
		Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_ArrayDTM;

		if(priv->RF_Type == RF_2T4R)
		{
			PHY_REGArrayLen = PHY_REGArrayLengthDTM;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REGArrayDTM;
		}
		else if (priv->RF_Type == RF_1T2R)
		{
			PHY_REGArrayLen = PHY_REG_1T2RArrayLengthDTM;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_1T2RArrayDTM;
		}
	}
	else
#endif
	{
		AGCTAB_ArrayLen = AGCTAB_ArrayLength;
		Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_Array;
		if(priv->rf_type == RF_2T4R)
		{
			PHY_REGArrayLen = PHY_REGArrayLength;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REGArray;
		}
		else if (priv->rf_type == RF_1T2R)
		{
			PHY_REGArrayLen = PHY_REG_1T2RArrayLength;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_1T2RArray;
		}
	}

	if (ConfigType == BaseBand_Config_PHY_REG)
	{
		for (i=0; i<PHY_REGArrayLen; i+=2)
		{
			rtl8192_setBBreg(dev, Rtl819XPHY_REGArray_Table[i], bMaskDWord, Rtl819XPHY_REGArray_Table[i+1]);
			RT_TRACE(COMP_DBG, "i: %x, The Rtl819xUsbPHY_REGArray[0] is %x Rtl819xUsbPHY_REGArray[1] is %x \n",i, Rtl819XPHY_REGArray_Table[i], Rtl819XPHY_REGArray_Table[i+1]);
		}
	}
	else if (ConfigType == BaseBand_Config_AGC_TAB)
	{
		for (i=0; i<AGCTAB_ArrayLen; i+=2)
		{
			rtl8192_setBBreg(dev, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);
			RT_TRACE(COMP_DBG, "i:%x, The rtl819XAGCTAB_Array[0] is %x rtl819XAGCTAB_Array[1] is %x \n",i, Rtl819XAGCTAB_Array_Table[i], Rtl819XAGCTAB_Array_Table[i+1]);
		}
	}
	return;


}
/******************************************************************************
 *function:  This function initialize Register definition offset for Radio Path
 *	     A/B/C/D
 *   input:  net_device dev
 *  output:  none
 *  return:  none
 *  notice:  Initialization value here is constant and it should never be changed
 * ***************************************************************************/
static void rtl8192_InitBBRFRegDef(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
// RF Interface Sowrtware Control
	priv->PHYRegDef[RF90_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 LSBs if read 32-bit from 0x870
	priv->PHYRegDef[RF90_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872)
	priv->PHYRegDef[RF90_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 LSBs if read 32-bit from 0x874
	priv->PHYRegDef[RF90_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 MSBs if read 32-bit from 0x874 (16-bit for 0x876)

	// RF Interface Readback Value
	priv->PHYRegDef[RF90_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB; // 16 LSBs if read 32-bit from 0x8E0
	priv->PHYRegDef[RF90_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2)
	priv->PHYRegDef[RF90_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 LSBs if read 32-bit from 0x8E4
	priv->PHYRegDef[RF90_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6)

	// RF Interface Output (and Enable)
	priv->PHYRegDef[RF90_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x860
	priv->PHYRegDef[RF90_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x864
	priv->PHYRegDef[RF90_PATH_C].rfintfo = rFPGA0_XC_RFInterfaceOE;// 16 LSBs if read 32-bit from 0x868
	priv->PHYRegDef[RF90_PATH_D].rfintfo = rFPGA0_XD_RFInterfaceOE;// 16 LSBs if read 32-bit from 0x86C

	// RF Interface (Output and)  Enable
	priv->PHYRegDef[RF90_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862)
	priv->PHYRegDef[RF90_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866)
	priv->PHYRegDef[RF90_PATH_C].rfintfe = rFPGA0_XC_RFInterfaceOE;// 16 MSBs if read 32-bit from 0x86A (16-bit for 0x86A)
	priv->PHYRegDef[RF90_PATH_D].rfintfe = rFPGA0_XD_RFInterfaceOE;// 16 MSBs if read 32-bit from 0x86C (16-bit for 0x86E)

	//Addr of LSSI. Wirte RF register by driver
	priv->PHYRegDef[RF90_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; //LSSI Parameter
	priv->PHYRegDef[RF90_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_C].rf3wireOffset = rFPGA0_XC_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_D].rf3wireOffset = rFPGA0_XD_LSSIParameter;

	// RF parameter
	priv->PHYRegDef[RF90_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;  //BB Band Select
	priv->PHYRegDef[RF90_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	priv->PHYRegDef[RF90_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	priv->PHYRegDef[RF90_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	// Tx AGC Gain Stage (same for all path. Should we remove this?)
	priv->PHYRegDef[RF90_PATH_A].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	priv->PHYRegDef[RF90_PATH_B].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	priv->PHYRegDef[RF90_PATH_C].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	priv->PHYRegDef[RF90_PATH_D].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage

	// Tranceiver A~D HSSI Parameter-1
	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;  //wire control parameter1
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;  //wire control parameter1
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara1 = rFPGA0_XC_HSSIParameter1;  //wire control parameter1
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara1 = rFPGA0_XD_HSSIParameter1;  //wire control parameter1

	// Tranceiver A~D HSSI Parameter-2
	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  //wire control parameter2
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  //wire control parameter2
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara2 = rFPGA0_XC_HSSIParameter2;  //wire control parameter2
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara2 = rFPGA0_XD_HSSIParameter2;  //wire control parameter1

	// RF switch Control
	priv->PHYRegDef[RF90_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl; //TR/Ant switch control
	priv->PHYRegDef[RF90_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	priv->PHYRegDef[RF90_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	priv->PHYRegDef[RF90_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	// AGC control 1
	priv->PHYRegDef[RF90_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	// AGC control 2
	priv->PHYRegDef[RF90_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	// RX AFE control 1
	priv->PHYRegDef[RF90_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;

	// RX AFE control 1
	priv->PHYRegDef[RF90_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;

	// Tx AFE control 1
	priv->PHYRegDef[RF90_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;

	// Tx AFE control 2
	priv->PHYRegDef[RF90_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;

	// Tranceiver LSSI Readback
	priv->PHYRegDef[RF90_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;

}
/******************************************************************************
 *function:  This function is to write register and then readback to make sure whether BB and RF is OK
 *   input:  net_device dev
 *   	     HW90_BLOCK_E CheckBlock
 *   	     RF90_RADIO_PATH_E eRFPath  //only used when checkblock is HW90_BLOCK_RF
 *  output:  none
 *  return:  return whether BB and RF is ok(0:OK; 1:Fail)
 *  notice:  This function may be removed in the ASIC
 * ***************************************************************************/
RT_STATUS rtl8192_phy_checkBBAndRF(struct net_device* dev, HW90_BLOCK_E CheckBlock, RF90_RADIO_PATH_E eRFPath)
{
	//struct r8192_priv *priv = ieee80211_priv(dev);
//	BB_REGISTER_DEFINITION_T *pPhyReg = &priv->PHYRegDef[eRFPath];
	RT_STATUS ret = RT_STATUS_SUCCESS;
	u32 i, CheckTimes = 4, dwRegRead = 0;
	u32 WriteAddr[4];
	u32 WriteData[] = {0xfffff027, 0xaa55a02f, 0x00000027, 0x55aa502f};
	// Initialize register address offset to be checked
	WriteAddr[HW90_BLOCK_MAC] = 0x100;
	WriteAddr[HW90_BLOCK_PHY0] = 0x900;
	WriteAddr[HW90_BLOCK_PHY1] = 0x800;
	WriteAddr[HW90_BLOCK_RF] = 0x3;
	RT_TRACE(COMP_PHY, "=======>%s(), CheckBlock:%d\n", __FUNCTION__, CheckBlock);
	for(i=0 ; i < CheckTimes ; i++)
	{

		//
		// Write Data to register and readback
		//
		switch(CheckBlock)
		{
		case HW90_BLOCK_MAC:
			RT_TRACE(COMP_ERR, "PHY_CheckBBRFOK(): Never Write 0x100 here!");
			break;

		case HW90_BLOCK_PHY0:
		case HW90_BLOCK_PHY1:
			write_nic_dword(dev, WriteAddr[CheckBlock], WriteData[i]);
			dwRegRead = read_nic_dword(dev, WriteAddr[CheckBlock]);
			break;

		case HW90_BLOCK_RF:
			WriteData[i] &= 0xfff;
			rtl8192_phy_SetRFReg(dev, eRFPath, WriteAddr[HW90_BLOCK_RF], bMask12Bits, WriteData[i]);
			// TODO: we should not delay for such a long time. Ask SD3
			mdelay(10);
			dwRegRead = rtl8192_phy_QueryRFReg(dev, eRFPath, WriteAddr[HW90_BLOCK_RF], bMaskDWord);
			mdelay(10);
			break;

		default:
			ret = RT_STATUS_FAILURE;
			break;
		}


		//
		// Check whether readback data is correct
		//
		if(dwRegRead != WriteData[i])
		{
			RT_TRACE(COMP_ERR, "====>error=====dwRegRead: %x, WriteData: %x \n", dwRegRead, WriteData[i]);
			ret = RT_STATUS_FAILURE;
			break;
		}
	}

	return ret;
}


/******************************************************************************
 *function:  This function initialize BB&RF
 *   input:  net_device dev
 *  output:  none
 *  return:  none
 *  notice:  Initialization value may change all the time, so please make
 *           sure it has been synced with the newest.
 * ***************************************************************************/
static RT_STATUS rtl8192_BB_Config_ParaFile(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	RT_STATUS rtStatus = RT_STATUS_SUCCESS;
	u8 bRegValue = 0, eCheckItem = 0;
	u32 dwRegValue = 0;
	/**************************************
	//<1>Initialize BaseBand
	**************************************/

	/*--set BB Global Reset--*/
	bRegValue = read_nic_byte(dev, BB_GLOBAL_RESET);
	write_nic_byte(dev, BB_GLOBAL_RESET,(bRegValue|BB_GLOBAL_RESET_BIT));

	/*---set BB reset Active---*/
	dwRegValue = read_nic_dword(dev, CPU_GEN);
	write_nic_dword(dev, CPU_GEN, (dwRegValue&(~CPU_GEN_BB_RST)));

	/*----Ckeck FPGAPHY0 and PHY1 board is OK----*/
	// TODO: this function should be removed on ASIC , Emily 2007.2.2
	for(eCheckItem=(HW90_BLOCK_E)HW90_BLOCK_PHY0; eCheckItem<=HW90_BLOCK_PHY1; eCheckItem++)
	{
		rtStatus  = rtl8192_phy_checkBBAndRF(dev, (HW90_BLOCK_E)eCheckItem, (RF90_RADIO_PATH_E)0); //don't care RF path
		if(rtStatus != RT_STATUS_SUCCESS)
		{
			RT_TRACE((COMP_ERR | COMP_PHY), "PHY_RF8256_Config():Check PHY%d Fail!!\n", eCheckItem-1);
			return rtStatus;
		}
	}
	/*---- Set CCK and OFDM Block "OFF"----*/
	rtl8192_setBBreg(dev, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x0);
	/*----BB Register Initilazation----*/
	//==m==>Set PHY REG From Header<==m==
	rtl8192_phyConfigBB(dev, BaseBand_Config_PHY_REG);

	/*----Set BB reset de-Active----*/
	dwRegValue = read_nic_dword(dev, CPU_GEN);
	write_nic_dword(dev, CPU_GEN, (dwRegValue|CPU_GEN_BB_RST));

 	/*----BB AGC table Initialization----*/
	//==m==>Set PHY REG From Header<==m==
	rtl8192_phyConfigBB(dev, BaseBand_Config_AGC_TAB);

	if (priv->card_8192_version  > VERSION_8190_BD)
	{
		if(priv->rf_type == RF_2T4R)
		{
		// Antenna gain offset from B/C/D to A
		dwRegValue = (  priv->AntennaTxPwDiff[2]<<8 |
						priv->AntennaTxPwDiff[1]<<4 |
						priv->AntennaTxPwDiff[0]);
		}
		else
			dwRegValue = 0x0;	//Antenna gain offset doesn't make sense in RF 1T2R.
		rtl8192_setBBreg(dev, rFPGA0_TxGainStage,
			(bXBTxAGC|bXCTxAGC|bXDTxAGC), dwRegValue);


		//XSTALLCap
#ifdef RTL8190P
	dwRegValue = priv->CrystalCap & 0x3;	// bit0~1 of crystal cap
	rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, bXtalCap01, dwRegValue);
	dwRegValue = ((priv->CrystalCap & 0xc)>>2);	// bit2~3 of crystal cap
	rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, bXtalCap23, dwRegValue);
#else
	#ifdef RTL8192E
		dwRegValue = priv->CrystalCap;
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, bXtalCap92x, dwRegValue);
	#endif
#endif

	}

	// Check if the CCK HighPower is turned ON.
	// This is used to calculate PWDB.
//	priv->bCckHighPower = (u8)(rtl8192_QueryBBReg(dev, rFPGA0_XA_HSSIParameter2, 0x200));
	return rtStatus;
}
/******************************************************************************
 *function:  This function initialize BB&RF
 *   input:  net_device dev
 *  output:  none
 *  return:  none
 *  notice:  Initialization value may change all the time, so please make
 *           sure it has been synced with the newest.
 * ***************************************************************************/
RT_STATUS rtl8192_BBConfig(struct net_device* dev)
{
	RT_STATUS	rtStatus = RT_STATUS_SUCCESS;
	rtl8192_InitBBRFRegDef(dev);
	//config BB&RF. As hardCode based initialization has not been well
	//implemented, so use file first.FIXME:should implement it for hardcode?
	rtStatus = rtl8192_BB_Config_ParaFile(dev);
	return rtStatus;
}

/******************************************************************************
 *function:  This function obtains the initialization value of Tx power Level offset
 *   input:  net_device dev
 *  output:  none
 *  return:  none
 * ***************************************************************************/
void rtl8192_phy_getTxPower(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef RTL8190P
	priv->MCSTxPowerLevelOriginalOffset[0] =
		read_nic_dword(dev, MCS_TXAGC);
	priv->MCSTxPowerLevelOriginalOffset[1] =
		read_nic_dword(dev, (MCS_TXAGC+4));
	priv->CCKTxPowerLevelOriginalOffset =
		read_nic_dword(dev, CCK_TXAGC);
#else
	#ifdef RTL8192E
	priv->MCSTxPowerLevelOriginalOffset[0] =
		read_nic_dword(dev, rTxAGC_Rate18_06);
	priv->MCSTxPowerLevelOriginalOffset[1] =
		read_nic_dword(dev, rTxAGC_Rate54_24);
	priv->MCSTxPowerLevelOriginalOffset[2] =
		read_nic_dword(dev, rTxAGC_Mcs03_Mcs00);
	priv->MCSTxPowerLevelOriginalOffset[3] =
		read_nic_dword(dev, rTxAGC_Mcs07_Mcs04);
	priv->MCSTxPowerLevelOriginalOffset[4] =
		read_nic_dword(dev, rTxAGC_Mcs11_Mcs08);
	priv->MCSTxPowerLevelOriginalOffset[5] =
		read_nic_dword(dev, rTxAGC_Mcs15_Mcs12);
	#endif
#endif

	// read rx initial gain
	priv->DefaultInitialGain[0] = read_nic_byte(dev, rOFDM0_XAAGCCore1);
	priv->DefaultInitialGain[1] = read_nic_byte(dev, rOFDM0_XBAGCCore1);
	priv->DefaultInitialGain[2] = read_nic_byte(dev, rOFDM0_XCAGCCore1);
	priv->DefaultInitialGain[3] = read_nic_byte(dev, rOFDM0_XDAGCCore1);
	RT_TRACE(COMP_INIT, "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x) \n",
		priv->DefaultInitialGain[0], priv->DefaultInitialGain[1],
		priv->DefaultInitialGain[2], priv->DefaultInitialGain[3]);

	// read framesync
	priv->framesync = read_nic_byte(dev, rOFDM0_RxDetector3);
	priv->framesyncC34 = read_nic_dword(dev, rOFDM0_RxDetector2);
	RT_TRACE(COMP_INIT, "Default framesync (0x%x) = 0x%x \n",
		rOFDM0_RxDetector3, priv->framesync);
	// read SIFS (save the value read fome MACPHY_REG.txt)
	priv->SifsTime = read_nic_word(dev, SIFS);
	return;
}

/******************************************************************************
 *function:  This function obtains the initialization value of Tx power Level offset
 *   input:  net_device dev
 *  output:  none
 *  return:  none
 * ***************************************************************************/
void rtl8192_phy_setTxPower(struct net_device* dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	powerlevel = 0,powerlevelOFDM24G = 0;
	char ant_pwr_diff;
	u32	u4RegValue;

	if(priv->epromtype == EPROM_93c46)
	{
		powerlevel = priv->TxPowerLevelCCK[channel-1];
		powerlevelOFDM24G = priv->TxPowerLevelOFDM24G[channel-1];
	}
	else if(priv->epromtype == EPROM_93c56)
	{
		if(priv->rf_type == RF_1T2R)
		{
			powerlevel = priv->TxPowerLevelCCK_C[channel-1];
			powerlevelOFDM24G = priv->TxPowerLevelOFDM24G_C[channel-1];
		}
		else if(priv->rf_type == RF_2T4R)
		{
			// Mainly we use RF-A Tx Power to write the Tx Power registers, but the RF-C Tx
			// Power must be calculated by the antenna diff.
			// So we have to rewrite Antenna gain offset register here.
			powerlevel = priv->TxPowerLevelCCK_A[channel-1];
			powerlevelOFDM24G = priv->TxPowerLevelOFDM24G_A[channel-1];

			ant_pwr_diff = priv->TxPowerLevelOFDM24G_C[channel-1]
						-priv->TxPowerLevelOFDM24G_A[channel-1];
			ant_pwr_diff &= 0xf;
			//DbgPrint(" ant_pwr_diff = 0x%x", (u8)(ant_pwr_diff));
			priv->RF_C_TxPwDiff = ant_pwr_diff;

			priv->AntennaTxPwDiff[2] = 0;// RF-D, don't care
			priv->AntennaTxPwDiff[1] = (u8)(ant_pwr_diff);// RF-C
			priv->AntennaTxPwDiff[0] = 0;// RF-B, don't care

			// Antenna gain offset from B/C/D to A
			u4RegValue = (  priv->AntennaTxPwDiff[2]<<8 |
						priv->AntennaTxPwDiff[1]<<4 |
						priv->AntennaTxPwDiff[0]);

			rtl8192_setBBreg(dev, rFPGA0_TxGainStage,
			(bXBTxAGC|bXCTxAGC|bXDTxAGC), u4RegValue);
		}
	}
#ifdef TODO
	//
	// CCX 2 S31, AP control of client transmit power:
	// 1. We shall not exceed Cell Power Limit as possible as we can.
	// 2. Tolerance is +/- 5dB.
	// 3. 802.11h Power Contraint takes higher precedence over CCX Cell Power Limit.
	//
	// TODO:
	// 1. 802.11h power contraint
	//
	// 071011, by rcnjko.
	//
	if(	pMgntInfo->OpMode == RT_OP_MODE_INFRASTRUCTURE &&
		pMgntInfo->bWithCcxCellPwr &&
		channel == pMgntInfo->dot11CurrentChannelNumber)
	{
		u8	CckCellPwrIdx = DbmToTxPwrIdx(Adapter, WIRELESS_MODE_B, pMgntInfo->CcxCellPwr);
		u8	LegacyOfdmCellPwrIdx = DbmToTxPwrIdx(Adapter, WIRELESS_MODE_G, pMgntInfo->CcxCellPwr);
		u8	OfdmCellPwrIdx = DbmToTxPwrIdx(Adapter, WIRELESS_MODE_N_24G, pMgntInfo->CcxCellPwr);

		RT_TRACE(COMP_TXAGC, DBG_LOUD,
			("CCX Cell Limit: %d dbm => CCK Tx power index : %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n",
			pMgntInfo->CcxCellPwr, CckCellPwrIdx, LegacyOfdmCellPwrIdx, OfdmCellPwrIdx));
		RT_TRACE(COMP_TXAGC, DBG_LOUD,
			("EEPROM channel(%d) => CCK Tx power index: %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n",
			channel, powerlevel, powerlevelOFDM24G + pHalData->LegacyHTTxPowerDiff, powerlevelOFDM24G));

		// CCK
		if(powerlevel > CckCellPwrIdx)
			powerlevel = CckCellPwrIdx;
		// Legacy OFDM, HT OFDM
		if(powerlevelOFDM24G + pHalData->LegacyHTTxPowerDiff > OfdmCellPwrIdx)
		{
			if((OfdmCellPwrIdx - pHalData->LegacyHTTxPowerDiff) > 0)
			{
				powerlevelOFDM24G = OfdmCellPwrIdx - pHalData->LegacyHTTxPowerDiff;
			}
			else
			{
				LegacyOfdmCellPwrIdx = 0;
			}
		}

		RT_TRACE(COMP_TXAGC, DBG_LOUD,
			("Altered CCK Tx power index : %d, Legacy OFDM Tx power index: %d, OFDM Tx power index: %d\n",
			powerlevel, powerlevelOFDM24G + pHalData->LegacyHTTxPowerDiff, powerlevelOFDM24G));
	}

	pHalData->CurrentCckTxPwrIdx = powerlevel;
	pHalData->CurrentOfdm24GTxPwrIdx = powerlevelOFDM24G;
#endif
	switch(priv->rf_chip)
	{
	case RF_8225:
	//	PHY_SetRF8225CckTxPower(Adapter, powerlevel);
	//	PHY_SetRF8225OfdmTxPower(Adapter, powerlevelOFDM24G);
		break;
	case RF_8256:
		PHY_SetRF8256CCKTxPower(dev, powerlevel); //need further implement
		PHY_SetRF8256OFDMTxPower(dev, powerlevelOFDM24G);
		break;
	case RF_8258:
		break;
	default:
		RT_TRACE(COMP_ERR, "unknown rf chip in funtion %s()\n", __FUNCTION__);
		break;
	}
	return;
}

/******************************************************************************
 *function:  This function check Rf chip to do RF config
 *   input:  net_device dev
 *  output:  none
 *  return:  only 8256 is supported
 * ***************************************************************************/
RT_STATUS rtl8192_phy_RFConfig(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	RT_STATUS rtStatus = RT_STATUS_SUCCESS;
	switch(priv->rf_chip)
	{
		case RF_8225:
//			rtStatus = PHY_RF8225_Config(Adapter);
			break;
		case RF_8256:
			rtStatus = PHY_RF8256_Config(dev);
			break;

		case RF_8258:
			break;
		case RF_PSEUDO_11N:
		//rtStatus = PHY_RF8225_Config(Adapter);
		break;

		default:
			RT_TRACE(COMP_ERR, "error chip id\n");
			break;
	}
	return rtStatus;
}

/******************************************************************************
 *function:  This function update Initial gain
 *   input:  net_device dev
 *  output:  none
 *  return:  As Windows has not implemented this, wait for complement
 * ***************************************************************************/
void rtl8192_phy_updateInitGain(struct net_device* dev)
{
	return;
}

/******************************************************************************
 *function:  This function read RF parameters from general head file, and do RF 3-wire
 *   input:  net_device dev
 *  output:  none
 *  return:  return code show if RF configuration is successful(0:pass, 1:fail)
 *    Note:  Delay may be required for RF configuration
 * ***************************************************************************/
u8 rtl8192_phy_ConfigRFWithHeaderFile(struct net_device* dev, RF90_RADIO_PATH_E	eRFPath)
{

	int i;
	//u32* pRFArray;
	u8 ret = 0;

	switch(eRFPath){
		case RF90_PATH_A:
			for(i = 0;i<RadioA_ArrayLength; i=i+2){

				if(Rtl819XRadioA_Array[i] == 0xfe){
						msleep(100);
						continue;
				}
				rtl8192_phy_SetRFReg(dev, eRFPath, Rtl819XRadioA_Array[i], bMask12Bits, Rtl819XRadioA_Array[i+1]);
				//msleep(1);

			}
			break;
		case RF90_PATH_B:
			for(i = 0;i<RadioB_ArrayLength; i=i+2){

				if(Rtl819XRadioB_Array[i] == 0xfe){
						msleep(100);
						continue;
				}
				rtl8192_phy_SetRFReg(dev, eRFPath, Rtl819XRadioB_Array[i], bMask12Bits, Rtl819XRadioB_Array[i+1]);
				//msleep(1);

			}
			break;
		case RF90_PATH_C:
			for(i = 0;i<RadioC_ArrayLength; i=i+2){

				if(Rtl819XRadioC_Array[i] == 0xfe){
						msleep(100);
						continue;
				}
				rtl8192_phy_SetRFReg(dev, eRFPath, Rtl819XRadioC_Array[i], bMask12Bits, Rtl819XRadioC_Array[i+1]);
				//msleep(1);

			}
			break;
		case RF90_PATH_D:
			for(i = 0;i<RadioD_ArrayLength; i=i+2){

				if(Rtl819XRadioD_Array[i] == 0xfe){
						msleep(100);
						continue;
				}
				rtl8192_phy_SetRFReg(dev, eRFPath, Rtl819XRadioD_Array[i], bMask12Bits, Rtl819XRadioD_Array[i+1]);
				//msleep(1);

			}
			break;
		default:
			break;
	}

	return ret;;

}
/******************************************************************************
 *function:  This function set Tx Power of the channel
 *   input:  struct net_device *dev
 *   	     u8 		channel
 *  output:  none
 *  return:  none
 *    Note:
 * ***************************************************************************/
static void rtl8192_SetTxPowerLevel(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	powerlevel = priv->TxPowerLevelCCK[channel-1];
	u8	powerlevelOFDM24G = priv->TxPowerLevelOFDM24G[channel-1];

	switch(priv->rf_chip)
	{
	case RF_8225:
#ifdef TO_DO_LIST
		PHY_SetRF8225CckTxPower(Adapter, powerlevel);
		PHY_SetRF8225OfdmTxPower(Adapter, powerlevelOFDM24G);
#endif
		break;

	case RF_8256:
		PHY_SetRF8256CCKTxPower(dev, powerlevel);
		PHY_SetRF8256OFDMTxPower(dev, powerlevelOFDM24G);
		break;

	case RF_8258:
		break;
	default:
		RT_TRACE(COMP_ERR, "unknown rf chip ID in rtl8192_SetTxPowerLevel()\n");
		break;
	}
	return;
}
/****************************************************************************************
 *function:  This function set command table variable(struct SwChnlCmd).
 *   input:  SwChnlCmd*		CmdTable 	//table to be set.
 *   	     u32		CmdTableIdx 	//variable index in table to be set
 *   	     u32		CmdTableSz	//table size.
 *   	     SwChnlCmdID	CmdID		//command ID to set.
 *	     u32		Para1
 *	     u32		Para2
 *	     u32		msDelay
 *  output:
 *  return:  true if finished, false otherwise
 *    Note:
 * ************************************************************************************/
static u8 rtl8192_phy_SetSwChnlCmdArray(
	SwChnlCmd*		CmdTable,
	u32			CmdTableIdx,
	u32			CmdTableSz,
	SwChnlCmdID		CmdID,
	u32			Para1,
	u32			Para2,
	u32			msDelay
	)
{
	SwChnlCmd* pCmd;

	if(CmdTable == NULL)
	{
		RT_TRACE(COMP_ERR, "phy_SetSwChnlCmdArray(): CmdTable cannot be NULL.\n");
		return false;
	}
	if(CmdTableIdx >= CmdTableSz)
	{
		RT_TRACE(COMP_ERR, "phy_SetSwChnlCmdArray(): Access invalid index, please check size of the table, CmdTableIdx:%d, CmdTableSz:%d\n",
				CmdTableIdx, CmdTableSz);
		return false;
	}

	pCmd = CmdTable + CmdTableIdx;
	pCmd->CmdID = CmdID;
	pCmd->Para1 = Para1;
	pCmd->Para2 = Para2;
	pCmd->msDelay = msDelay;

	return true;
}
/******************************************************************************
 *function:  This function set channel step by step
 *   input:  struct net_device *dev
 *   	     u8 		channel
 *   	     u8* 		stage //3 stages
 *   	     u8* 		step  //
 *   	     u32* 		delay //whether need to delay
 *  output:  store new stage, step and delay for next step(combine with function above)
 *  return:  true if finished, false otherwise
 *    Note:  Wait for simpler function to replace it //wb
 * ***************************************************************************/
static u8 rtl8192_phy_SwChnlStepByStep(struct net_device *dev, u8 channel, u8* stage, u8* step, u32* delay)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
//	PCHANNEL_ACCESS_SETTING	pChnlAccessSetting;
	SwChnlCmd				PreCommonCmd[MAX_PRECMD_CNT];
	u32					PreCommonCmdCnt;
	SwChnlCmd				PostCommonCmd[MAX_POSTCMD_CNT];
	u32					PostCommonCmdCnt;
	SwChnlCmd				RfDependCmd[MAX_RFDEPENDCMD_CNT];
	u32					RfDependCmdCnt;
	SwChnlCmd				*CurrentCmd = NULL;
	//RF90_RADIO_PATH_E		eRFPath;
	u8		eRFPath;
//	u32		RfRetVal;
//	u8		RetryCnt;

	RT_TRACE(COMP_TRACE, "====>%s()====stage:%d, step:%d, channel:%d\n", __FUNCTION__, *stage, *step, channel);
//	RT_ASSERT(IsLegalChannel(Adapter, channel), ("illegal channel: %d\n", channel));

#ifdef ENABLE_DOT11D
	if (!IsLegalChannel(priv->ieee80211, channel))
	{
		RT_TRACE(COMP_ERR, "=============>set to illegal channel:%d\n", channel);
		return true; //return true to tell upper caller function this channel setting is finished! Or it will in while loop.
	}
#endif

	//for(eRFPath = RF90_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	//for(eRFPath = 0; eRFPath <RF90_PATH_MAX; eRFPath++)
	{
		//if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
		//	return false;
		// <1> Fill up pre common command.
		PreCommonCmdCnt = 0;
		rtl8192_phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++, MAX_PRECMD_CNT,
					CmdID_SetTxPowerLevel, 0, 0, 0);
		rtl8192_phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++, MAX_PRECMD_CNT,
					CmdID_End, 0, 0, 0);

		// <2> Fill up post common command.
		PostCommonCmdCnt = 0;

		rtl8192_phy_SetSwChnlCmdArray(PostCommonCmd, PostCommonCmdCnt++, MAX_POSTCMD_CNT,
					CmdID_End, 0, 0, 0);

		// <3> Fill up RF dependent command.
		RfDependCmdCnt = 0;
		switch( priv->rf_chip )
		{
		case RF_8225:
			if (!(channel >= 1 && channel <= 14))
			{
				RT_TRACE(COMP_ERR, "illegal channel for Zebra 8225: %d\n", channel);
				return false;
			}
			rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
				CmdID_RF_WriteReg, rZebra1_Channel, RF_CHANNEL_TABLE_ZEBRA[channel], 10);
			rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
				CmdID_End, 0, 0, 0);
			break;

		case RF_8256:
			// TEST!! This is not the table for 8256!!
			if (!(channel >= 1 && channel <= 14))
			{
				RT_TRACE(COMP_ERR, "illegal channel for Zebra 8256: %d\n", channel);
				return false;
			}
			rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
				CmdID_RF_WriteReg, rZebra1_Channel, channel, 10);
			rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_End, 0, 0, 0);
			break;

		case RF_8258:
			break;

		default:
			RT_TRACE(COMP_ERR, "Unknown RFChipID: %d\n", priv->rf_chip);
			return false;
			break;
		}


		do{
			switch(*stage)
			{
			case 0:
				CurrentCmd=&PreCommonCmd[*step];
				break;
			case 1:
				CurrentCmd=&RfDependCmd[*step];
				break;
			case 2:
				CurrentCmd=&PostCommonCmd[*step];
				break;
			}

			if(CurrentCmd->CmdID==CmdID_End)
			{
				if((*stage)==2)
				{
					return true;
				}
				else
				{
					(*stage)++;
					(*step)=0;
					continue;
				}
			}

			switch(CurrentCmd->CmdID)
			{
			case CmdID_SetTxPowerLevel:
				if(priv->card_8192_version > (u8)VERSION_8190_BD) //xiong: consider it later!
					rtl8192_SetTxPowerLevel(dev,channel);
				break;
			case CmdID_WritePortUlong:
				write_nic_dword(dev, CurrentCmd->Para1, CurrentCmd->Para2);
				break;
			case CmdID_WritePortUshort:
				write_nic_word(dev, CurrentCmd->Para1, (u16)CurrentCmd->Para2);
				break;
			case CmdID_WritePortUchar:
				write_nic_byte(dev, CurrentCmd->Para1, (u8)CurrentCmd->Para2);
				break;
			case CmdID_RF_WriteReg:
				for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
					rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, CurrentCmd->Para1, bMask12Bits, CurrentCmd->Para2<<7);
				break;
			default:
				break;
			}

			break;
		}while(true);
	}/*for(Number of RF paths)*/

	(*delay)=CurrentCmd->msDelay;
	(*step)++;
	return false;
}

/******************************************************************************
 *function:  This function does acturally set channel work
 *   input:  struct net_device *dev
 *   	     u8 		channel
 *  output:  none
 *  return:  noin
 *    Note:  We should not call this function directly
 * ***************************************************************************/
static void rtl8192_phy_FinishSwChnlNow(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32	delay = 0;

	while(!rtl8192_phy_SwChnlStepByStep(dev,channel,&priv->SwChnlStage,&priv->SwChnlStep,&delay))
	{
		if(delay>0)
			msleep(delay);//or mdelay? need further consideration
                if(!priv->up)
		        break;
	}
}
/******************************************************************************
 *function:  Callback routine of the work item for switch channel.
 *   input:
 *
 *  output:  none
 *  return:  noin
 * ***************************************************************************/
void rtl8192_SwChnl_WorkItem(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);

	RT_TRACE(COMP_TRACE, "==> SwChnlCallback819xUsbWorkItem()\n");

	RT_TRACE(COMP_TRACE, "=====>--%s(), set chan:%d, priv:%p\n", __FUNCTION__, priv->chan, priv);

	rtl8192_phy_FinishSwChnlNow(dev , priv->chan);

	RT_TRACE(COMP_TRACE, "<== SwChnlCallback819xUsbWorkItem()\n");
}

/******************************************************************************
 *function:  This function scheduled actural workitem to set channel
 *   input:  net_device dev
 *   	     u8		channel //channel to set
 *  output:  none
 *  return:  return code show if workitem is scheduled(1:pass, 0:fail)
 *    Note:  Delay may be required for RF configuration
 * ***************************************************************************/
u8 rtl8192_phy_SwChnl(struct net_device* dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	RT_TRACE(COMP_PHY, "=====>%s()\n", __FUNCTION__);
        if(!priv->up)
		return false;
	if(priv->SwChnlInProgress)
		return false;

//	if(pHalData->SetBWModeInProgress)
//		return;

	//--------------------------------------------
	switch(priv->ieee80211->mode)
	{
	case WIRELESS_MODE_A:
	case WIRELESS_MODE_N_5G:
		if (channel<=14){
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_A but channel<=14");
			return false;
		}
		break;
	case WIRELESS_MODE_B:
		if (channel>14){
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_B but channel>14");
			return false;
		}
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		if (channel>14){
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_G but channel>14");
			return false;
		}
		break;
	}
	//--------------------------------------------

	priv->SwChnlInProgress = true;
	if(channel == 0)
		channel = 1;

	priv->chan=channel;

	priv->SwChnlStage=0;
	priv->SwChnlStep=0;
//	schedule_work(&(priv->SwChnlWorkItem));
//	rtl8192_SwChnl_WorkItem(dev);
	if(priv->up) {
//		queue_work(priv->priv_wq,&(priv->SwChnlWorkItem));
	rtl8192_SwChnl_WorkItem(dev);
	}
        priv->SwChnlInProgress = false;
	return true;
}

static void CCK_Tx_Power_Track_BW_Switch_TSSI(struct net_device *dev	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	switch(priv->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
	//added by vivi, cck,tx power track, 20080703
			priv->CCKPresentAttentuation =
				priv->CCKPresentAttentuation_20Mdefault + priv->CCKPresentAttentuation_difference;

			if(priv->CCKPresentAttentuation > (CCKTxBBGainTableLength-1))
				priv->CCKPresentAttentuation = CCKTxBBGainTableLength-1;
			if(priv->CCKPresentAttentuation < 0)
				priv->CCKPresentAttentuation = 0;

			RT_TRACE(COMP_POWER_TRACKING, "20M, priv->CCKPresentAttentuation = %d\n", priv->CCKPresentAttentuation);

			if(priv->ieee80211->current_network.channel== 14 && !priv->bcck_in_ch14)
			{
				priv->bcck_in_ch14 = TRUE;
				dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
			}
			else if(priv->ieee80211->current_network.channel != 14 && priv->bcck_in_ch14)
			{
				priv->bcck_in_ch14 = FALSE;
				dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
			}
			else
				dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
		break;

		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_20_40:
			//added by vivi, cck,tx power track, 20080703
			priv->CCKPresentAttentuation =
				priv->CCKPresentAttentuation_40Mdefault + priv->CCKPresentAttentuation_difference;

			RT_TRACE(COMP_POWER_TRACKING, "40M, priv->CCKPresentAttentuation = %d\n", priv->CCKPresentAttentuation);
			if(priv->CCKPresentAttentuation > (CCKTxBBGainTableLength-1))
				priv->CCKPresentAttentuation = CCKTxBBGainTableLength-1;
			if(priv->CCKPresentAttentuation < 0)
				priv->CCKPresentAttentuation = 0;

			if(priv->ieee80211->current_network.channel == 14 && !priv->bcck_in_ch14)
			{
				priv->bcck_in_ch14 = TRUE;
				dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
			}
			else if(priv->ieee80211->current_network.channel != 14 && priv->bcck_in_ch14)
			{
				priv->bcck_in_ch14 = FALSE;
				dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
			}
			else
				dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
		break;
	}
}

#ifndef RTL8190P
static void CCK_Tx_Power_Track_BW_Switch_ThermalMeter(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	if(priv->ieee80211->current_network.channel == 14 && !priv->bcck_in_ch14)
		priv->bcck_in_ch14 = TRUE;
	else if(priv->ieee80211->current_network.channel != 14 && priv->bcck_in_ch14)
		priv->bcck_in_ch14 = FALSE;

	//write to default index and tx power track will be done in dm.
	switch(priv->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
			if(priv->Record_CCK_20Mindex == 0)
				priv->Record_CCK_20Mindex = 6;	//set default value.
			priv->CCK_index = priv->Record_CCK_20Mindex;//6;
			RT_TRACE(COMP_POWER_TRACKING, "20MHz, CCK_Tx_Power_Track_BW_Switch_ThermalMeter(),CCK_index = %d\n", priv->CCK_index);
		break;

		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_20_40:
			priv->CCK_index = priv->Record_CCK_40Mindex;//0;
			RT_TRACE(COMP_POWER_TRACKING, "40MHz, CCK_Tx_Power_Track_BW_Switch_ThermalMeter(), CCK_index = %d\n", priv->CCK_index);
		break;
	}
	dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
}
#endif

static void CCK_Tx_Power_Track_BW_Switch(struct net_device *dev)
{
#ifdef RTL8192E
	struct r8192_priv *priv = ieee80211_priv(dev);
#endif

#ifdef RTL8190P
	CCK_Tx_Power_Track_BW_Switch_TSSI(dev);
#else
	//if(pHalData->bDcut == TRUE)
	if(priv->IC_Cut >= IC_VersionCut_D)
		CCK_Tx_Power_Track_BW_Switch_TSSI(dev);
	else
		CCK_Tx_Power_Track_BW_Switch_ThermalMeter(dev);
#endif
}


//
/******************************************************************************
 *function:  Callback routine of the work item for set bandwidth mode.
 *   input:  struct net_device *dev
 *   	     HT_CHANNEL_WIDTH	Bandwidth  //20M or 40M
 *   	     HT_EXTCHNL_OFFSET Offset 	   //Upper, Lower, or Don't care
 *  output:  none
 *  return:  none
 *    Note:  I doubt whether SetBWModeInProgress flag is necessary as we can
 *    	     test whether current work in the queue or not.//do I?
 * ***************************************************************************/
void rtl8192_SetBWModeWorkItem(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 regBwOpMode;

	RT_TRACE(COMP_SWBW, "==>rtl8192_SetBWModeWorkItem()  Switch to %s bandwidth\n", \
					priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz")


	if(priv->rf_chip== RF_PSEUDO_11N)
	{
		priv->SetBWModeInProgress= false;
		return;
	}
	if(!priv->up)
	{
		priv->SetBWModeInProgress= false;
		return;
	}
	//<1>Set MAC register
	regBwOpMode = read_nic_byte(dev, BW_OPMODE);

	switch(priv->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
		       // 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);
			break;

		case HT_CHANNEL_WIDTH_20_40:
			regBwOpMode &= ~BW_OPMODE_20MHZ;
        		// 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);
			break;

		default:
			RT_TRACE(COMP_ERR, "SetChannelBandwidth819xUsb(): unknown Bandwidth: %#X\n",priv->CurrentChannelBW);
			break;
	}

	//<2>Set PHY related register
	switch(priv->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			// Add by Vivi 20071119
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x0);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x0);
//			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x00100000, 1);

			// Correct the tx power for CCK rate in 20M. Suggest by YN, 20071207
//			write_nic_dword(dev, rCCK0_TxFilter1, 0x1a1b0000);
//			write_nic_dword(dev, rCCK0_TxFilter2, 0x090e1317);
//			write_nic_dword(dev, rCCK0_DebugPort, 0x00000204);
			if(!priv->btxpower_tracking)
			{
				write_nic_dword(dev, rCCK0_TxFilter1, 0x1a1b0000);
				write_nic_dword(dev, rCCK0_TxFilter2, 0x090e1317);
				write_nic_dword(dev, rCCK0_DebugPort, 0x00000204);
			}
			else
				CCK_Tx_Power_Track_BW_Switch(dev);

#ifdef RTL8190P
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, bADClkPhase, 1);
			rtl8192_setBBreg(dev, rOFDM0_RxDetector1, bMaskByte0, 0x44); 	// 0xc30 is for 8190 only, Emily
#else
	#ifdef RTL8192E
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x00100000, 1);
	#endif
#endif

			break;
		case HT_CHANNEL_WIDTH_20_40:
			// Add by Vivi 20071119
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x1);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x1);
			//rtl8192_setBBreg(dev, rCCK0_System, bCCKSideBand, (priv->nCur40MhzPrimeSC>>1));
                    //rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x00100000, 0);
			//rtl8192_setBBreg(dev, rOFDM1_LSTF, 0xC00, priv->nCur40MhzPrimeSC);

			// Correct the tx power for CCK rate in 40M. Suggest by YN, 20071207
			//write_nic_dword(dev, rCCK0_TxFilter1, 0x35360000);
			//write_nic_dword(dev, rCCK0_TxFilter2, 0x121c252e);
			//write_nic_dword(dev, rCCK0_DebugPort, 0x00000409);
			if(!priv->btxpower_tracking)
			{
				write_nic_dword(dev, rCCK0_TxFilter1, 0x35360000);
				write_nic_dword(dev, rCCK0_TxFilter2, 0x121c252e);
				write_nic_dword(dev, rCCK0_DebugPort, 0x00000409);
			}
			else
				CCK_Tx_Power_Track_BW_Switch(dev);

			// Set Control channel to upper or lower. These settings are required only for 40MHz
			rtl8192_setBBreg(dev, rCCK0_System, bCCKSideBand, (priv->nCur40MhzPrimeSC>>1));
			rtl8192_setBBreg(dev, rOFDM1_LSTF, 0xC00, priv->nCur40MhzPrimeSC);


#ifdef RTL8190P
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, bADClkPhase, 0);
			rtl8192_setBBreg(dev, rOFDM0_RxDetector1, bMaskByte0, 0x42);	// 0xc30 is for 8190 only, Emily

			// Set whether CCK should be sent in upper or lower channel. Suggest by YN. 20071207
			// It is set in Tx descriptor for 8192x series
			if(priv->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			{
				rtl8192_setBBreg(dev, rFPGA0_RFMOD, (BIT6|BIT5), 0x01);
			}else if(priv->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			{
				rtl8192_setBBreg(dev, rFPGA0_RFMOD, (BIT6|BIT5), 0x02);
			}

#else
	#ifdef RTL8192E
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x00100000, 0);
	#endif
#endif
			break;
		default:
			RT_TRACE(COMP_ERR, "SetChannelBandwidth819xUsb(): unknown Bandwidth: %#X\n" ,priv->CurrentChannelBW);
			break;

	}
	//Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315

#if 1
	//<3>Set RF related register
	switch( priv->rf_chip )
	{
		case RF_8225:
#ifdef TO_DO_LIST
			PHY_SetRF8225Bandwidth(Adapter, pHalData->CurrentChannelBW);
#endif
			break;

		case RF_8256:
			PHY_SetRF8256Bandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_8258:
			// PHY_SetRF8258Bandwidth();
			break;

		case RF_PSEUDO_11N:
			// Do Nothing
			break;

		default:
			RT_TRACE(COMP_ERR, "Unknown RFChipID: %d\n", priv->rf_chip);
			break;
	}
#endif
	atomic_dec(&(priv->ieee80211->atm_swbw));
	priv->SetBWModeInProgress= false;

	RT_TRACE(COMP_SWBW, "<==SetBWMode819xUsb()");
}

/******************************************************************************
 *function:  This function schedules bandwith switch work.
 *   input:  struct net_device *dev
 *   	     HT_CHANNEL_WIDTH	Bandwidth  //20M or 40M
 *   	     HT_EXTCHNL_OFFSET Offset 	   //Upper, Lower, or Don't care
 *  output:  none
 *  return:  none
 *    Note:  I doubt whether SetBWModeInProgress flag is necessary as we can
 *    	     test whether current work in the queue or not.//do I?
 * ***************************************************************************/
void rtl8192_SetBWMode(struct net_device *dev, HT_CHANNEL_WIDTH	Bandwidth, HT_EXTCHNL_OFFSET Offset)
{
	struct r8192_priv *priv = ieee80211_priv(dev);


	if(priv->SetBWModeInProgress)
		return;

	 atomic_inc(&(priv->ieee80211->atm_swbw));
	priv->SetBWModeInProgress= true;

	priv->CurrentChannelBW = Bandwidth;

	if(Offset==HT_EXTCHNL_OFFSET_LOWER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if(Offset==HT_EXTCHNL_OFFSET_UPPER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	//queue_work(priv->priv_wq, &(priv->SetBWModeWorkItem));
	//	schedule_work(&(priv->SetBWModeWorkItem));
	rtl8192_SetBWModeWorkItem(dev);

}


void InitialGain819xPci(struct net_device *dev, u8 Operation)
{
#define SCAN_RX_INITIAL_GAIN	0x17
#define POWER_DETECTION_TH	0x08
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32					BitMask;
	u8					initial_gain;

	if(priv->up)
	{
		switch(Operation)
		{
			case IG_Backup:
			RT_TRACE(COMP_SCAN, "IG_Backup, backup the initial gain.\n");
				initial_gain = SCAN_RX_INITIAL_GAIN;//pHalData->DefaultInitialGain[0];//
				BitMask = bMaskByte0;
				if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
					rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	// FW DIG OFF
				priv->initgain_backup.xaagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XAAGCCore1, BitMask);
				priv->initgain_backup.xbagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XBAGCCore1, BitMask);
				priv->initgain_backup.xcagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XCAGCCore1, BitMask);
				priv->initgain_backup.xdagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XDAGCCore1, BitMask);
				BitMask  = bMaskByte2;
				priv->initgain_backup.cca		= (u8)rtl8192_QueryBBReg(dev, rCCK0_CCA, BitMask);

			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc50 is %x\n",priv->initgain_backup.xaagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc58 is %x\n",priv->initgain_backup.xbagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc60 is %x\n",priv->initgain_backup.xcagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc68 is %x\n",priv->initgain_backup.xdagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xa0a is %x\n",priv->initgain_backup.cca);

			RT_TRACE(COMP_SCAN, "Write scan initial gain = 0x%x \n", initial_gain);
				write_nic_byte(dev, rOFDM0_XAAGCCore1, initial_gain);
				write_nic_byte(dev, rOFDM0_XBAGCCore1, initial_gain);
				write_nic_byte(dev, rOFDM0_XCAGCCore1, initial_gain);
				write_nic_byte(dev, rOFDM0_XDAGCCore1, initial_gain);
				RT_TRACE(COMP_SCAN, "Write scan 0xa0a = 0x%x \n", POWER_DETECTION_TH);
				write_nic_byte(dev, 0xa0a, POWER_DETECTION_TH);
				break;
			case IG_Restore:
			RT_TRACE(COMP_SCAN, "IG_Restore, restore the initial gain.\n");
				BitMask = 0x7f; //Bit0~ Bit6
				if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
					rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	// FW DIG OFF

				rtl8192_setBBreg(dev, rOFDM0_XAAGCCore1, BitMask, (u32)priv->initgain_backup.xaagccore1);
				rtl8192_setBBreg(dev, rOFDM0_XBAGCCore1, BitMask, (u32)priv->initgain_backup.xbagccore1);
				rtl8192_setBBreg(dev, rOFDM0_XCAGCCore1, BitMask, (u32)priv->initgain_backup.xcagccore1);
				rtl8192_setBBreg(dev, rOFDM0_XDAGCCore1, BitMask, (u32)priv->initgain_backup.xdagccore1);
				BitMask  = bMaskByte2;
				rtl8192_setBBreg(dev, rCCK0_CCA, BitMask, (u32)priv->initgain_backup.cca);

			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc50 is %x\n",priv->initgain_backup.xaagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc58 is %x\n",priv->initgain_backup.xbagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc60 is %x\n",priv->initgain_backup.xcagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc68 is %x\n",priv->initgain_backup.xdagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xa0a is %x\n",priv->initgain_backup.cca);

				rtl8192_phy_setTxPower(dev,priv->ieee80211->current_network.channel);


				if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
					rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x1);	// FW DIG ON
				break;
			default:
			RT_TRACE(COMP_SCAN, "Unknown IG Operation. \n");
				break;
		}
	}
}

