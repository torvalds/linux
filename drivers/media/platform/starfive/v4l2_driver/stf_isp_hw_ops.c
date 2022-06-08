// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 *
 * linux/drivers/media/platform/starfive/stf_isp.c
 *
 * PURPOSE:	This files contains the driver of VPP.
 */
#include "stfcamss.h"
#include <linux/io.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <video/stf-vin.h>
#include "stf_isp_ioctl.h"
#include "stf_isp.h"
#include <linux/delay.h>
#include <linux/clk.h>
#define USE_NEW_CONFIG_SETTING

static const struct regval_t isp_reg_init_config_list[] = {
	/* config DC(0040H~0044H) */
	{0x00000044, 0x00000000, 0, 0},
	/* config DEC(0030H) */
	{0x00000030, 0x00000000, 0, 0},
	/* config OBC(0034H, 02E0H~02FCH) */
	{0x00000034, 0x000000BB, 0, 0},
	{0x000002E0, 0x40404040, 0, 0},
	{0x000002E4, 0x40404040, 0, 0},
	{0x000002E8, 0x40404040, 0, 0},
	{0x000002EC, 0x40404040, 0, 0},
	{0x000002F0, 0x00000000, 0, 0},
	{0x000002F4, 0x00000000, 0, 0},
	{0x000002F8, 0x00000000, 0, 0},
	{0x000002FC, 0x00000000, 0, 0},
	/* config LCBQ(0074H, 007CH, 0300H~039FH, and 0400H~049FH) */
	{0x00000074, 0x00009900, 0, 0},
	{0x0000007C, 0x01E40040, 0, 0},
	{0x00000300, 0x01000100, 0, 0},
	{0x00000304, 0x01000100, 0, 0},
	{0x00000308, 0x01000100, 0, 0},
	{0x0000030C, 0x01000100, 0, 0},
	{0x00000310, 0x01000100, 0, 0},
	{0x00000314, 0x01000100, 0, 0},
	{0x00000318, 0x01000100, 0, 0},
	{0x0000031C, 0x01000100, 0, 0},
	{0x00000320, 0x01000100, 0, 0},
	{0x00000324, 0x01000100, 0, 0},
	{0x00000328, 0x01000100, 0, 0},
	{0x0000032C, 0x01000100, 0, 0},
	{0x00000330, 0x00000100, 0, 0},
	{0x00000334, 0x01000100, 0, 0},
	{0x00000338, 0x01000100, 0, 0},
	{0x0000033C, 0x01000100, 0, 0},
	{0x00000340, 0x01000100, 0, 0},
	{0x00000344, 0x01000100, 0, 0},
	{0x00000348, 0x01000100, 0, 0},
	{0x0000034C, 0x01000100, 0, 0},
	{0x00000350, 0x01000100, 0, 0},
	{0x00000354, 0x01000100, 0, 0},
	{0x00000358, 0x01000100, 0, 0},
	{0x0000035C, 0x01000100, 0, 0},
	{0x00000360, 0x01000100, 0, 0},
	{0x00000364, 0x00000100, 0, 0},
	{0x00000368, 0x01000100, 0, 0},
	{0x0000036C, 0x01000100, 0, 0},
	{0x00000370, 0x01000100, 0, 0},
	{0x00000374, 0x01000100, 0, 0},
	{0x00000378, 0x01000100, 0, 0},
	{0x0000037C, 0x01000100, 0, 0},
	{0x00000380, 0x01000100, 0, 0},
	{0x00000384, 0x01000100, 0, 0},
	{0x00000388, 0x01000100, 0, 0},
	{0x0000038C, 0x01000100, 0, 0},
	{0x00000390, 0x01000100, 0, 0},
	{0x00000394, 0x01000100, 0, 0},
	{0x00000398, 0x00000100, 0, 0},
	{0x0000039C, 0x01000100, 0, 0},
	{0x000003A0, 0x01000100, 0, 0},
	{0x000003A4, 0x01000100, 0, 0},
	{0x000003A8, 0x01000100, 0, 0},
	{0x000003AC, 0x01000100, 0, 0},
	{0x000003B0, 0x01000100, 0, 0},
	{0x000003B4, 0x01000100, 0, 0},
	{0x000003B8, 0x01000100, 0, 0},
	{0x000003BC, 0x01000100, 0, 0},
	{0x000003C0, 0x01000100, 0, 0},
	{0x000003C4, 0x01000100, 0, 0},
	{0x000003C8, 0x01000100, 0, 0},
	{0x000003CC, 0x00000100, 0, 0},
	{0x00000400, 0x00000000, 0, 0},
	{0x00000404, 0x00000000, 0, 0},
	{0x00000408, 0x00000000, 0, 0},
	{0x0000040C, 0x00000000, 0, 0},
	{0x00000410, 0x00000000, 0, 0},
	{0x00000414, 0x00000000, 0, 0},
	{0x00000418, 0x00000000, 0, 0},
	{0x0000041C, 0x00000000, 0, 0},
	{0x00000420, 0x00000000, 0, 0},
	{0x00000424, 0x00000000, 0, 0},
	{0x00000428, 0x00000000, 0, 0},
	{0x0000042C, 0x00000000, 0, 0},
	{0x00000430, 0x00000000, 0, 0},
	{0x00000434, 0x00000000, 0, 0},
	{0x00000438, 0x00000000, 0, 0},
	{0x0000043C, 0x00000000, 0, 0},
	{0x00000440, 0x00000000, 0, 0},
	{0x00000444, 0x00000000, 0, 0},
	{0x00000448, 0x00000000, 0, 0},
	{0x0000044C, 0x00000000, 0, 0},
	{0x00000450, 0x00000000, 0, 0},
	{0x00000454, 0x00000000, 0, 0},
	{0x00000458, 0x00000000, 0, 0},
	{0x0000045C, 0x00000000, 0, 0},
	{0x00000460, 0x00000000, 0, 0},
	{0x00000464, 0x00000000, 0, 0},
	{0x00000468, 0x00000000, 0, 0},
	{0x0000046C, 0x00000000, 0, 0},
	{0x00000470, 0x00000000, 0, 0},
	{0x00000474, 0x00000000, 0, 0},
	{0x00000478, 0x00000000, 0, 0},
	{0x0000047C, 0x00000000, 0, 0},
	{0x00000480, 0x00000000, 0, 0},
	{0x00000484, 0x00000000, 0, 0},
	{0x00000488, 0x00000000, 0, 0},
	{0x0000048C, 0x00000000, 0, 0},
	{0x00000490, 0x00000000, 0, 0},
	{0x00000494, 0x00000000, 0, 0},
	{0x00000498, 0x00000000, 0, 0},
	{0x0000049C, 0x00000000, 0, 0},
	{0x000004A0, 0x00000000, 0, 0},
	{0x000004A4, 0x00000000, 0, 0},
	{0x000004A8, 0x00000000, 0, 0},
	{0x000004AC, 0x00000000, 0, 0},
	{0x000004B0, 0x00000000, 0, 0},
	{0x000004B4, 0x00000000, 0, 0},
	{0x000004B8, 0x00000000, 0, 0},
	{0x000004BC, 0x00000000, 0, 0},
	{0x000004C0, 0x00000000, 0, 0},
	{0x000004C4, 0x00000000, 0, 0},
	{0x000004C8, 0x00000000, 0, 0},
	{0x000004CC, 0x00000000, 0, 0},
	/* config OECF(0100H~027CH) */
	{0x00000100, 0x00100000, 0, 0},
	{0x00000104, 0x00400020, 0, 0},
	{0x00000108, 0x00800060, 0, 0},
	{0x0000010C, 0x00C000A0, 0, 0},
	{0x00000110, 0x010000E0, 0, 0},
	{0x00000114, 0x02000180, 0, 0},
	{0x00000118, 0x03000280, 0, 0},
	{0x0000011C, 0x03FE0380, 0, 0},
	{0x00000120, 0x00100000, 0, 0},
	{0x00000124, 0x00400020, 0, 0},
	{0x00000128, 0x00800060, 0, 0},
	{0x0000012C, 0x00C000A0, 0, 0},
	{0x00000130, 0x010000E0, 0, 0},
	{0x00000134, 0x02000180, 0, 0},
	{0x00000138, 0x03000280, 0, 0},
	{0x0000013C, 0x03FE0380, 0, 0},
	{0x00000140, 0x00100000, 0, 0},
	{0x00000144, 0x00400020, 0, 0},
	{0x00000148, 0x00800060, 0, 0},
	{0x0000014C, 0x00C000A0, 0, 0},
	{0x00000150, 0x010000E0, 0, 0},
	{0x00000154, 0x02000180, 0, 0},
	{0x00000158, 0x03000280, 0, 0},
	{0x0000015C, 0x03FE0380, 0, 0},
	{0x00000160, 0x00100000, 0, 0},
	{0x00000164, 0x00400020, 0, 0},
	{0x00000168, 0x00800060, 0, 0},
	{0x0000016C, 0x00C000A0, 0, 0},
	{0x00000170, 0x010000E0, 0, 0},
	{0x00000174, 0x02000180, 0, 0},
	{0x00000178, 0x03000280, 0, 0},
	{0x0000017C, 0x03FE0380, 0, 0},
	{0x00000180, 0x00100000, 0, 0},
	{0x00000184, 0x00400020, 0, 0},
	{0x00000188, 0x00800060, 0, 0},
	{0x0000018C, 0x00C000A0, 0, 0},
	{0x00000190, 0x010000E0, 0, 0},
	{0x00000194, 0x02000180, 0, 0},
	{0x00000198, 0x03000280, 0, 0},
	{0x0000019C, 0x03FE0380, 0, 0},
	{0x000001A0, 0x00100000, 0, 0},
	{0x000001A4, 0x00400020, 0, 0},
	{0x000001A8, 0x00800060, 0, 0},
	{0x000001AC, 0x00C000A0, 0, 0},
	{0x000001B0, 0x010000E0, 0, 0},
	{0x000001B4, 0x02000180, 0, 0},
	{0x000001B8, 0x03000280, 0, 0},
	{0x000001BC, 0x03FE0380, 0, 0},
	{0x000001C0, 0x00100000, 0, 0},
	{0x000001C4, 0x00400020, 0, 0},
	{0x000001C8, 0x00800060, 0, 0},
	{0x000001CC, 0x00C000A0, 0, 0},
	{0x000001D0, 0x010000E0, 0, 0},
	{0x000001D4, 0x02000180, 0, 0},
	{0x000001D8, 0x03000280, 0, 0},
	{0x000001DC, 0x03FE0380, 0, 0},
	{0x000001E0, 0x00100000, 0, 0},
	{0x000001E4, 0x00400020, 0, 0},
	{0x000001E8, 0x00800060, 0, 0},
	{0x000001EC, 0x00C000A0, 0, 0},
	{0x000001F0, 0x010000E0, 0, 0},
	{0x000001F4, 0x02000180, 0, 0},
	{0x000001F8, 0x03000280, 0, 0},
	{0x000001FC, 0x03FE0380, 0, 0},
	{0x00000200, 0x00800080, 0, 0},
	{0x00000204, 0x00800080, 0, 0},
	{0x00000208, 0x00800080, 0, 0},
	{0x0000020C, 0x00800080, 0, 0},
	{0x00000210, 0x00800080, 0, 0},
	{0x00000214, 0x00800080, 0, 0},
	{0x00000218, 0x00800080, 0, 0},
	{0x0000021C, 0x00800080, 0, 0},
	{0x00000220, 0x00800080, 0, 0},
	{0x00000224, 0x00800080, 0, 0},
	{0x00000228, 0x00800080, 0, 0},
	{0x0000022C, 0x00800080, 0, 0},
	{0x00000230, 0x00800080, 0, 0},
	{0x00000234, 0x00800080, 0, 0},
	{0x00000238, 0x00800080, 0, 0},
	{0x0000023C, 0x00800080, 0, 0},
	{0x00000240, 0x00800080, 0, 0},
	{0x00000244, 0x00800080, 0, 0},
	{0x00000248, 0x00800080, 0, 0},
	{0x0000024C, 0x00800080, 0, 0},
	{0x00000250, 0x00800080, 0, 0},
	{0x00000254, 0x00800080, 0, 0},
	{0x00000258, 0x00800080, 0, 0},
	{0x0000025C, 0x00800080, 0, 0},
	{0x00000260, 0x00800080, 0, 0},
	{0x00000264, 0x00800080, 0, 0},
	{0x00000268, 0x00800080, 0, 0},
	{0x0000026C, 0x00800080, 0, 0},
	{0x00000270, 0x00800080, 0, 0},
	{0x00000274, 0x00800080, 0, 0},
	{0x00000278, 0x00800080, 0, 0},
	{0x0000027C, 0x00800080, 0, 0},
	/* config OECFHM(03D0H~03E4H) */
	{0x000003D0, 0x04000000, 0, 0},
	{0x000003D4, 0x0C000800, 0, 0},
	{0x000003D8, 0x00000FFF, 0, 0},
	{0x000003DC, 0x08000800, 0, 0},
	{0x000003E0, 0x08000800, 0, 0},
	{0x000003E4, 0x00000800, 0, 0},
	/* config LCCF(0050H, 0058H, 00E0H~00ECH) */
	{0x00000050, 0x021C03C0, 0, 0},
	{0x00000058, 0x0000000B, 0, 0},
	{0x000000E0, 0x00000000, 0, 0},
	{0x000000E4, 0x00000000, 0, 0},
	{0x000000E8, 0x00000000, 0, 0},
	{0x000000EC, 0x00000000, 0, 0},
	/* config AWB(0280H~02DCH) */
	{0x00000280, 0x00000000, 0, 0},
	{0x00000284, 0x00000000, 0, 0},
	{0x00000288, 0x00000000, 0, 0},
	{0x0000028C, 0x00000000, 0, 0},
	{0x00000290, 0x00000000, 0, 0},
	{0x00000294, 0x00000000, 0, 0},
	{0x00000298, 0x00000000, 0, 0},
	{0x0000029C, 0x00000000, 0, 0},
	{0x000002A0, 0x00000000, 0, 0},
	{0x000002A4, 0x00000000, 0, 0},
	{0x000002A8, 0x00000000, 0, 0},
	{0x000002AC, 0x00000000, 0, 0},
	{0x000002B0, 0x00000000, 0, 0},
	{0x000002B4, 0x00000000, 0, 0},
	{0x000002B8, 0x00000000, 0, 0},
	{0x000002BC, 0x00000000, 0, 0},
	{0x000002C0, 0x00800080, 0, 0},
	{0x000002C4, 0x00800080, 0, 0},
	{0x000002C8, 0x00800080, 0, 0},
	{0x000002CC, 0x00800080, 0, 0},
	{0x000002D0, 0x00800080, 0, 0},
	{0x000002D4, 0x00800080, 0, 0},
	{0x000002D8, 0x00800080, 0, 0},
	{0x000002DC, 0x00800080, 0, 0},
	/* config CTC(0A10H) and DBC(0A14H) filter */
	{0x00000A10, 0x41400040, 0, 0},
	{0x00000A14, 0x02000200, 0, 0},
	/* config CFA(0018H, 0A1CH) */
	{0x00000018, 0x000011BB, 0, 0},
	{0x00000A1C, 0x00000032, 0, 0},
	/* config CCM(0C40H~0CA4H) */
	{0x00000C40, 0x00060000, 0, 0},
	{0x00000C44, 0x00000000, 0, 0},
	{0x00000C48, 0x00000000, 0, 0},
	{0x00000C4C, 0x00000000, 0, 0},
	{0x00000C50, 0x00000000, 0, 0},
	{0x00000C54, 0x00000000, 0, 0},
	{0x00000C58, 0x00000000, 0, 0},
	{0x00000C5C, 0x00000000, 0, 0},
	{0x00000C60, 0x00000000, 0, 0},
	{0x00000C64, 0x00000000, 0, 0},
	{0x00000C68, 0x00000000, 0, 0},
	{0x00000C6C, 0x00000000, 0, 0},
	{0x00000C70, 0x00000080, 0, 0},
	{0x00000C74, 0x00000000, 0, 0},
	{0x00000C78, 0x00000000, 0, 0},
	{0x00000C7C, 0x00000000, 0, 0},
	{0x00000C80, 0x00000080, 0, 0},
	{0x00000C84, 0x00000000, 0, 0},
	{0x00000C88, 0x00000000, 0, 0},
	{0x00000C8C, 0x00000000, 0, 0},
	{0x00000C90, 0x00000080, 0, 0},
	{0x00000C94, 0x00000000, 0, 0},
	{0x00000C98, 0x00000000, 0, 0},
	{0x00000C9C, 0x00000000, 0, 0},
	{0x00000CA0, 0x00000700, 0, 0},
	{0x00000CA4, 0x00000200, 0, 0},
	/* config GMARGB(0E00H~0E38H) */
	{0x00000E00, 0x24000000, 0, 0},
	{0x00000E04, 0x08000020, 0, 0},
	{0x00000E08, 0x08000040, 0, 0},
	{0x00000E0C, 0x08000060, 0, 0},
	{0x00000E10, 0x08000080, 0, 0},
	{0x00000E14, 0x080000A0, 0, 0},
	{0x00000E18, 0x080000C0, 0, 0},
	{0x00000E1C, 0x080000E0, 0, 0},
	{0x00000E20, 0x08000100, 0, 0},
	{0x00000E24, 0x08000180, 0, 0},
	{0x00000E28, 0x08000200, 0, 0},
	{0x00000E2C, 0x08000280, 0, 0},
	{0x00000E30, 0x08000300, 0, 0},
	{0x00000E34, 0x08000380, 0, 0},
	{0x00000E38, 0x080003FE, 0, 0},
	/* config R2Y(0E40H~0E60H) */
	{0x00000E40, 0x0000004C, 0, 0},
	{0x00000E44, 0x00000097, 0, 0},
	{0x00000E48, 0x0000001D, 0, 0},
	{0x00000E4C, 0x000001D5, 0, 0},
	{0x00000E50, 0x000001AC, 0, 0},
	{0x00000E54, 0x00000080, 0, 0},
	{0x00000E58, 0x00000080, 0, 0},
	{0x00000E5C, 0x00000194, 0, 0},
	{0x00000E60, 0x000001EC, 0, 0},
	/* config YCRV(0F00H~0FFCH) */
	{0x00000F00, 0x00000000, 0, 0},
	{0x00000F04, 0x00000010, 0, 0},
	{0x00000F08, 0x00000020, 0, 0},
	{0x00000F0C, 0x00000030, 0, 0},
	{0x00000F10, 0x00000040, 0, 0},
	{0x00000F14, 0x00000050, 0, 0},
	{0x00000F18, 0x00000060, 0, 0},
	{0x00000F1C, 0x00000070, 0, 0},
	{0x00000F20, 0x00000080, 0, 0},
	{0x00000F24, 0x00000090, 0, 0},
	{0x00000F28, 0x000000A0, 0, 0},
	{0x00000F2C, 0x000000B0, 0, 0},
	{0x00000F30, 0x000000C0, 0, 0},
	{0x00000F34, 0x000000D0, 0, 0},
	{0x00000F38, 0x000000E0, 0, 0},
	{0x00000F3C, 0x000000F0, 0, 0},
	{0x00000F40, 0x00000100, 0, 0},
	{0x00000F44, 0x00000110, 0, 0},
	{0x00000F48, 0x00000120, 0, 0},
	{0x00000F4C, 0x00000130, 0, 0},
	{0x00000F50, 0x00000140, 0, 0},
	{0x00000F54, 0x00000150, 0, 0},
	{0x00000F58, 0x00000160, 0, 0},
	{0x00000F5C, 0x00000170, 0, 0},
	{0x00000F60, 0x00000180, 0, 0},
	{0x00000F64, 0x00000190, 0, 0},
	{0x00000F68, 0x000001A0, 0, 0},
	{0x00000F6C, 0x000001B0, 0, 0},
	{0x00000F70, 0x000001C0, 0, 0},
	{0x00000F74, 0x000001D0, 0, 0},
	{0x00000F78, 0x000001E0, 0, 0},
	{0x00000F7C, 0x000001F0, 0, 0},
	{0x00000F80, 0x00000200, 0, 0},
	{0x00000F84, 0x00000210, 0, 0},
	{0x00000F88, 0x00000220, 0, 0},
	{0x00000F8C, 0x00000230, 0, 0},
	{0x00000F90, 0x00000240, 0, 0},
	{0x00000F94, 0x00000250, 0, 0},
	{0x00000F98, 0x00000260, 0, 0},
	{0x00000F9C, 0x00000270, 0, 0},
	{0x00000FA0, 0x00000280, 0, 0},
	{0x00000FA4, 0x00000290, 0, 0},
	{0x00000FA8, 0x000002A0, 0, 0},
	{0x00000FAC, 0x000002B0, 0, 0},
	{0x00000FB0, 0x000002C0, 0, 0},
	{0x00000FB4, 0x000002D0, 0, 0},
	{0x00000FB8, 0x000002E0, 0, 0},
	{0x00000FBC, 0x000002F0, 0, 0},
	{0x00000FC0, 0x00000300, 0, 0},
	{0x00000FC4, 0x00000310, 0, 0},
	{0x00000FC8, 0x00000320, 0, 0},
	{0x00000FCC, 0x00000330, 0, 0},
	{0x00000FD0, 0x00000340, 0, 0},
	{0x00000FD4, 0x00000350, 0, 0},
	{0x00000FD8, 0x00000360, 0, 0},
	{0x00000FDC, 0x00000370, 0, 0},
	{0x00000FE0, 0x00000380, 0, 0},
	{0x00000FE4, 0x00000390, 0, 0},
	{0x00000FE8, 0x000003A0, 0, 0},
	{0x00000FEC, 0x000003B0, 0, 0},
	{0x00000FF0, 0x000003C0, 0, 0},
	{0x00000FF4, 0x000003D0, 0, 0},
	{0x00000FF8, 0x000003E0, 0, 0},
	{0x00000FFC, 0x000003F0, 0, 0},
	/* config Shrp(0E80H~0EE8H) */
	{0x00000E80, 0x00070F00, 0, 0},
	{0x00000E84, 0x00180F00, 0, 0},
	{0x00000E88, 0x00800F00, 0, 0},
	{0x00000E8C, 0x01000F00, 0, 0},
	{0x00000E90, 0x00100F00, 0, 0},
	{0x00000E94, 0x00600F00, 0, 0},
	{0x00000E98, 0x01000F00, 0, 0},
	{0x00000E9C, 0x01900F00, 0, 0},
	{0x00000EA0, 0x00000F00, 0, 0},
	{0x00000EA4, 0x00000F00, 0, 0},
	{0x00000EA8, 0x00000F00, 0, 0},
	{0x00000EAC, 0x00000F00, 0, 0},
	{0x00000EB0, 0x00000F00, 0, 0},
	{0x00000EB4, 0x00000F00, 0, 0},
	{0x00000EB8, 0x00000F00, 0, 0},
	{0x00000EBC, 0x10000000, 0, 0},
	{0x00000EC0, 0x10000000, 0, 0},
	{0x00000EC4, 0x10000000, 0, 0},
	{0x00000EC8, 0x10000000, 0, 0},
	{0x00000ECC, 0x10000000, 0, 0},
	{0x00000ED0, 0x10000000, 0, 0},
	{0x00000ED4, 0x88000D7C, 0, 0},
	{0x00000ED8, 0x00C00040, 0, 0},
	{0x00000EDC, 0xFF000000, 0, 0},
	{0x00000EE0, 0x00A00040, 0, 0},
	{0x00000EE4, 0x00000000, 0, 0},
	{0x00000EE8, 0x00000000, 0, 0},
	/* config DNYUV(0C00H~0C24H) */
	{0x00000C00, 0x00777777, 0, 0},
	{0x00000C04, 0x00007777, 0, 0},
	{0x00000C08, 0x00777777, 0, 0},
	{0x00000C0C, 0x00007777, 0, 0},
	{0x00000C10, 0x00600040, 0, 0},
	{0x00000C14, 0x00D80090, 0, 0},
	{0x00000C18, 0x01E60144, 0, 0},
	{0x00000C1C, 0x00600040, 0, 0},
	{0x00000C20, 0x00D80090, 0, 0},
	{0x00000C24, 0x01E60144, 0, 0},
	/* config SAT(0A30H~0A40H, 0A54H~0A58H) */
	{0x00000A30, 0x00000100, 0, 0},
	{0x00000A34, 0x001F0001, 0, 0},
	{0x00000A38, 0x00000000, 0, 0},
	{0x00000A3C, 0x00000100, 0, 0},
	{0x00000A40, 0x00000008, 0, 0},
	{0x00000A54, 0x04010001, 0, 0},
	{0x00000A58, 0x03FF0001, 0, 0},
	/* config OBA(0090H~0094H) */
	{0x00000090, 0x04380000, 0, 0},
	{0x00000094, 0x04390780, 0, 0},
	/* config SC(0098H~009CH, 00B8H~00BCH,
	 * 00C0H, 0C4H~0D4H, 04D0H~054CH, 5D0H~5D4H)
	 */
	{0x0000009C, 0x01000000, 0, 0},
	{0x000000B8, 0x000C0000, 0, 0},
	{0x000000BC, 0xC010151D, 0, 0},
	{0x000000C0, 0x01F1BF08, 0, 0},
	{0x000000C4, 0xFF00FF00, 0, 0},
	{0x000000C8, 0xFF00FF00, 0, 0},
	{0x000000CC, 0xFFFF0000, 0, 0},
	{0x000000D0, 0xFFFF0000, 0, 0},
	{0x000000D4, 0xFFFF0000, 0, 0},
	{0x000000D8, 0x01050107, 0, 0},
	{0x000004D0, 0x00000000, 0, 0},
	{0x000004D4, 0x00000000, 0, 0},
	{0x000004D8, 0x00000000, 0, 0},
	{0x000004DC, 0x00000000, 0, 0},
	{0x000004E0, 0x00000000, 0, 0},
	{0x000004E4, 0x00000000, 0, 0},
	{0x000004E8, 0x00000000, 0, 0},
	{0x000004EC, 0x00000000, 0, 0},
	{0x000004F0, 0x00100000, 0, 0},
	{0x000004F4, 0x00000000, 0, 0},
	{0x000004F8, 0x03D20000, 0, 0},
	{0x000004FC, 0x00000000, 0, 0},
	{0x00000500, 0x00950000, 0, 0},
	{0x00000504, 0x00000000, 0, 0},
	{0x00000508, 0x00253000, 0, 0},
	{0x0000050C, 0x00000000, 0, 0},
	{0x00000510, 0x00000000, 0, 0},
	{0x00000514, 0x00000000, 0, 0},
	{0x00000518, 0x00000000, 0, 0},
	{0x0000051C, 0x00000000, 0, 0},
	{0x00000520, 0x00000000, 0, 0},
	{0x00000524, 0x00000000, 0, 0},
	{0x00000528, 0x00000000, 0, 0},
	{0x0000052C, 0x00000000, 0, 0},
	{0x00000530, 0x00000000, 0, 0},
	{0x00000534, 0x00000000, 0, 0},
	{0x00000538, 0xFFFFFFF0, 0, 0},
	{0x0000053C, 0x8FFFFFFF, 0, 0},
	{0x00000540, 0x0000001E, 0, 0},
	{0x00000544, 0x00000000, 0, 0},
	{0x00000548, 0x00000000, 0, 0},
	{0x0000054C, 0xF0F20000, 0, 0},
	{0x000005D0, 0xFF00FF00, 0, 0},
	{0x000005D4, 0xFF00FF00, 0, 0},
	/* config YHIST(0CC8H~0CD8H) */
	{0x00000CC8, 0x00000000, 0, 0},
	{0x00000CCC, 0x0437077F, 0, 0},
	{0x00000CD0, 0x00010002, 0, 0},
	{0x00000CD4, 0x00000000, 0, 0},
	/* config CBAR(0600H-0653H) */
	{0x00000600, 0x043E0782, 0, 0},
	{0x00000604, 0x00000000, 0, 0},
	{0x00000608, 0x0437077F, 0, 0},
	{0x0000060C, 0x00443150, 0, 0},
	{0x00000610, 0x00000000, 0, 0},
	{0x00000614, 0x08880888, 0, 0},
	{0x00000618, 0x02220222, 0, 0},
	{0x0000061C, 0x04440444, 0, 0},
	{0x00000620, 0x08880888, 0, 0},
	{0x00000624, 0x0AAA0AAA, 0, 0},
	{0x00000628, 0x0CCC0CCC, 0, 0},
	{0x0000062C, 0x0EEE0EEE, 0, 0},
	{0x00000630, 0x0FFF0FFF, 0, 0},
	{0x00000634, 0x08880888, 0, 0},
	{0x00000638, 0x02220222, 0, 0},
	{0x0000063C, 0x04440444, 0, 0},
	{0x00000640, 0x08880888, 0, 0},
	{0x00000644, 0x0AAA0AAA, 0, 0},
	{0x00000648, 0x0CCC0CCC, 0, 0},
	{0x0000064C, 0x0EEE0EEE, 0, 0},
	{0x00000650, 0x0FFF0FFF, 0, 0},
	/* config sensor(0014H) */
	{0x00000014, 0x0000000c, 0, 0},
	/* config CROP(001CH, 0020H) */
	{0x0000001C, 0x00000000, 0, 0},
	{0x00000020, 0x0437077F, 0, 0},
	/* config isp pileline X/Y size(A0CH) */
	{0x00000A0C, 0x04380780, 0, 0},
	/* config CSI dump (24H/28H) */
	{0x00000028, 0x00030B80, 0, 0},
	/* Video Output */
	/* config UO(0A80H~0A90H) */
	{0x00000A88, 0x00000780, 0, 0},
	/* NV12 */
	{0x00000A8C, 0x00000000, 0, 0},
	/* NV21
	 *{0x00000A8C, 0x00000020, 0, 0},
	 */
	{0x00000A90, 0x00000000, 0, 0},
	{0x00000A9C, 0x00000780, 0, 0},
	{0x00000AA0, 0x00000002, 0, 0},
	{0x00000AA4, 0x00000002, 0, 0},
	{0x00000AA8, 0x07800438, 0, 0},
	{0x00000AB4, 0x00000780, 0, 0},
	{0x00000AB8, 0x00000002, 0, 0},
	{0x00000ABC, 0x00000002, 0, 0},
	{0x00000AC0, 0x07800438, 0, 0},
	{0x00000AC4, 0x00000000, 0, 0},
	/* config TIL(0B20H~0B48H) */
	{0x00000B20, 0x04380780, 0, 0},
	{0x00000B24, 0x00000960, 0, 0},
	{0x00000B38, 0x00030003, 0, 0},
	{0x00000B3C, 0x00000960, 0, 0},
	{0x00000B44, 0x00000000, 0, 0},
	{0x00000B48, 0x00000000, 0, 0},
	/* Enable DEC/OBC/OECF/LCCF/AWB/SC/DUMP */
	{0x00000010, 0x000A00D6, 0x00000000, 0x00},
	/* Enable CFA/CAR/CCM/GMARGB/R2Y/SHRP/SAT/DNYUV/YCRV/YHIST/CTC/DBC */
	{0x00000A08, 0x107A01BE, 0x00000000, 0x00},
};

const struct reg_table isp_reg_init_settings[] = {
	{isp_reg_init_config_list,
	ARRAY_SIZE(isp_reg_init_config_list)},
};

static const struct regval_t isp_reg_start_config_list[] = {
#if defined(ENABLE_SS0_SS1)
	/* ENABLE UO/SS0/SS1/Multi-Frame and Reset ISP */
	{0x00000A00, 0x00121802, 0x00000000, 0x0A},
	/* ENABLE UO/SS0/SS1/Multi-Frame and Leave ISP reset */
	{0x00000A00, 0x00121800, 0x00000000, 0x0A},
#else
	/* ENABLE UO/Multi-Frame and Reset ISP */
	{0x00000A00, 0x00120002, 0x00000000, 0x0A},
	/* ENABLE UO/Multi-Frame and Leave ISP reset */
	{0x00000A00, 0x00120000, 0x00000000, 0x0A},
#endif
	/* Config ISP shadow mode as next-vsync */
	{0x00000A50, 0x00000002, 0x00000000, 0x00},
#if defined(ENABLE_SS0_SS1)
	/* ENABLE UO/SS0/SS1/Multi-Frame and Enable ISP */
	{0x00000A00, 0x00121801, 0x00000000, 0x0A},
#else
	/* ENABLE UO/Multi-Frame and Enable ISP */
	{0x00000A00, 0x00120001, 0x00000000, 0x0A},
#endif
	/* Config CSI shadow mode as immediate to fetch current setting */
	{0x00000008, 0x00010004, 0x00000000, 0x0A},
	/* Config CSI shadow mode as next-vsync */
	{0x00000008, 0x00020004, 0x00000000, 0x00},
	/* Enable CSI */
	{0x00000000, 0x00000001, 0x00000000, 0x0A},
};

const struct reg_table isp_reg_start_settings[] = {
	{isp_reg_start_config_list,
	ARRAY_SIZE(isp_reg_start_config_list)},
};

static const struct regval_t isp_imx_219_reg_config_list[] = {
	/* MIPI sensor */
	{0x00000014, 0x0000000D, 0, 0},
	/* config CFA(0018H, 0A1CH) */
	{0x00000A1C, 0x00000032, 0, 0},
	{0x00000A8C, 0x00000000, 0, 0},
	{0x00000A90, 0x00000000, 0, 0},
	/* config R2Y(0E40H~0E60H) */
	{0x00000E40, 0x0000004C, 0, 0},
	{0x00000E44, 0x00000097, 0, 0},
	{0x00000E48, 0x0000001D, 0, 0},
	{0x00000E4C, 0x000001D5, 0, 0},
	{0x00000E50, 0x000001AC, 0, 0},
	{0x00000E54, 0x00000080, 0, 0},
	{0x00000E58, 0x00000080, 0, 0},
	{0x00000E5C, 0x00000194, 0, 0},
	{0x00000E60, 0x000001EC, 0, 0},
	/* Config AWB(0280H~02DCH). Fixed WB gain for IMX-219 sensor. */
	{0x00000280, 0x00000000, 0, 0},
	{0x00000284, 0x00000000, 0, 0},
	{0x00000288, 0x00000000, 0, 0},
	{0x0000028C, 0x00000000, 0, 0},
	{0x00000290, 0x00000000, 0, 0},
	{0x00000294, 0x00000000, 0, 0},
	{0x00000298, 0x00000000, 0, 0},
	{0x0000029C, 0x00000000, 0, 0},
	{0x000002A0, 0x00000000, 0, 0},
	{0x000002A4, 0x00000000, 0, 0},
	{0x000002A8, 0x00000000, 0, 0},
	{0x000002AC, 0x00000000, 0, 0},
	{0x000002B0, 0x00000000, 0, 0},
	{0x000002B4, 0x00000000, 0, 0},
	{0x000002B8, 0x00000000, 0, 0},
	{0x000002BC, 0x00000000, 0, 0},
	{0x000002C0, 0x00F000F0, 0, 0},
	{0x000002C4, 0x00F000F0, 0, 0},
	{0x000002C8, 0x00800080, 0, 0},
	{0x000002CC, 0x00800080, 0, 0},
	{0x000002D0, 0x00800080, 0, 0},
	{0x000002D4, 0x00800080, 0, 0},
	{0x000002D8, 0x00B000B0, 0, 0},
	{0x000002DC, 0x00B000B0, 0, 0},
	/* config GMARGB(0E00H~0E38H)
	 * Gamma RGB 1.9 for IMX-219 sensor
	 */
	{0x00000E00, 0x24000000, 0, 0},
	{0x00000E04, 0x159500A5, 0, 0},
	{0x00000E08, 0x0F9900EE, 0, 0},
	{0x00000E0C, 0x0CE40127, 0, 0},
	{0x00000E10, 0x0B410157, 0, 0},
	{0x00000E14, 0x0A210181, 0, 0},
	{0x00000E18, 0x094B01A8, 0, 0},
	{0x00000E1C, 0x08A401CC, 0, 0},
	{0x00000E20, 0x081D01EE, 0, 0},
	{0x00000E24, 0x06B20263, 0, 0},
	{0x00000E28, 0x05D802C7, 0, 0},
	{0x00000E2C, 0x05420320, 0, 0},
	{0x00000E30, 0x04D30370, 0, 0},
	{0x00000E34, 0x047C03BB, 0, 0},
	{0x00000E38, 0x043703FF, 0, 0},
	{0x00000010, 0x00000080, 0, 0},
	/* Enable CFA/GMARGB/R2Y */
	{0x00000A08, 0x10000032, 0x0FFFFFFF, 0x00},
	{0x00000A00, 0x00120002, 0, 0},
	{0x00000A00, 0x00120000, 0, 0},
	{0x00000A50, 0x00000002, 0, 0},
	{0x00000008, 0x00010000, 0, 0},
	{0x00000008, 0x0002000A, 0, 0},
	{0x00000000, 0x00000001, 0, 0},
};

const struct reg_table isp_imx_219_settings[] = {
	{isp_imx_219_reg_config_list,
	ARRAY_SIZE(isp_imx_219_reg_config_list)},
};

static struct regval_t isp_format_reg_list[] = {
	{0x0000001C, 0x00000000, 0x00000000, 0},
	{0x00000020, 0x0437077F, 0x00000000, 0},
	{0x00000A0C, 0x04380780, 0x00000000, 0},
	{0x00000A88, 0x00000780, 0x00000000, 0},
	{0x00000018, 0x000011BB, 0x00000000, 0},
	{0x00000A08, 0x10000000, 0xF0000000, 0},
	{0x00000028, 0x00030B80, 0x0003FFFF, 0},
	{0x00000AA8, 0x07800438, 0x00000000, 0},
	{0x00000A9C, 0x00000780, 0x00000000, 0},
	{0x00000AC0, 0x07800438, 0x00000000, 0},
	{0x00000AB4, 0x00000780, 0x00000000, 0},
	{0x00000B20, 0x04380780, 0x00000000, 0},
	{0x00000B24, 0x00000960, 0x00000000, 0},
	{0x00000B3C, 0x00000960, 0x00000000, 0},
	{0x00000014, 0x00000008, 0x00000000, 0},
};

const struct reg_table  isp_format_settings[] = {
	{isp_format_reg_list,
	ARRAY_SIZE(isp_format_reg_list)},
};

#if defined(USE_NEW_CONFIG_SETTING)
#else
static struct reg_table  *isp_settings = (struct reg_table *)isp_imx_219_settings;
#endif

static void isp_load_regs(void __iomem *ispbase, const struct reg_table *table)
{
	int j;
	u32 delay_ms, reg_addr, mask, val;

	for (j = 0; j < table->regval_num; j++) {
		delay_ms = table->regval[j].delay_ms;
		reg_addr = table->regval[j].addr;
		val = table->regval[j].val;
		mask = table->regval[j].mask;

		if (reg_addr % 4
			|| reg_addr > STF_ISP_REG_OFFSET_MAX
			|| delay_ms > STF_ISP_REG_DELAY_MAX)
			continue;

		if (mask)
			reg_set_bit(ispbase, reg_addr, mask, val);
		else
			reg_write(ispbase, reg_addr, val);
		if (delay_ms)
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
	}
}

static void isp_load_regs_exclude_csi_isp_enable(
			void __iomem *ispbase,
			const struct reg_table *table)
{
	int j;
	u32 delay_ms, reg_addr, mask, val;

	for (j = 0; j < table->regval_num; j++) {
		delay_ms = table->regval[j].delay_ms;
		reg_addr = table->regval[j].addr;
		val = table->regval[j].val;
		mask = table->regval[j].mask;

		if (reg_addr % 4
			|| reg_addr > STF_ISP_REG_OFFSET_MAX
			|| delay_ms > STF_ISP_REG_DELAY_MAX
			|| ((reg_addr == ISP_REG_CSI_INPUT_EN_AND_STATUS) && (val & 0x01))
			|| ((reg_addr == ISP_REG_ISP_CTRL_0) && (val & 0x01)))
			continue;

		if (mask)
			reg_set_bit(ispbase, reg_addr, mask, val);
		else
			reg_write(ispbase, reg_addr, val);
		if (delay_ms)
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
	}
}

static int stf_isp_clk_enable(struct stf_isp_dev *isp_dev)
{
	struct stfcamss *stfcamss = isp_dev->stfcamss;

	if (isp_dev->id == 0) {
		clk_prepare_enable(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk);
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_C].rstc);
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_P].rstc);
	} else {
		st_err(ST_ISP, "please check isp id :%d\n", isp_dev->id);
	}

	return 0;
}

static int stf_isp_clk_disable(struct stf_isp_dev *isp_dev)
{
	struct stfcamss *stfcamss = isp_dev->stfcamss;

	if (isp_dev->id == 0) {
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_C].rstc);
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_P].rstc);
		clk_disable_unprepare(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk);
	} else {
		st_err(ST_ISP, "please check isp id :%d\n", isp_dev->id);
	}

	return 0;
}

static  void __iomem *stf_isp_get_ispbase(
		unsigned int isp_id, struct stf_vin_dev *vin)
{
	void __iomem *base = vin->isp_base;

	return base;
}

static int stf_isp_reset(struct stf_isp_dev *isp_dev)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	reg_set_bit(ispbase, ISP_REG_ISP_CTRL_0, BIT(1), BIT(1));
	reg_set_bit(ispbase, ISP_REG_ISP_CTRL_0, BIT(1), 0);

	return 0;
}

static int stf_isp_config_set(struct stf_isp_dev *isp_dev)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	st_debug(ST_ISP, "%s, isp_id = %d\n", __func__, isp_dev->id);

#if defined(USE_NEW_CONFIG_SETTING)
	mutex_lock(&isp_dev->setfile_lock);
	isp_load_regs(ispbase, isp_reg_init_settings);
	if (isp_dev->setfile.state) {
		st_info(ST_ISP, "%s, Program extra ISP setting!\n", __func__);
		isp_load_regs_exclude_csi_isp_enable(ispbase,
			&isp_dev->setfile.settings);
	}

	mutex_unlock(&isp_dev->setfile_lock);
#else
	mutex_lock(&isp_dev->setfile_lock);
	if (isp_dev->setfile.state)
		isp_load_regs(ispbase, &isp_dev->setfile.settings);
	else
		isp_load_regs(ispbase, isp_settings);
	mutex_unlock(&isp_dev->setfile_lock);

	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[0].addr,
			isp_format_reg_list[0].val);
	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[1].addr,
			isp_format_reg_list[1].val);
	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[2].addr,
			isp_format_reg_list[2].val);
	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[3].addr,
			isp_format_reg_list[3].val);
#endif

	return 0;
}

static int stf_isp_set_format(struct stf_isp_dev *isp_dev,
		struct isp_stream_format *crop_array, u32 mcode,
		int type)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	struct stf_dvp_dev *dvp_dev = isp_dev->stfcamss->dvp_dev;
	struct v4l2_rect *crop = &crop_array[ISP_COMPOSE].rect;
	u32 bpp = crop_array[ISP_COMPOSE].bpp;
	void __iomem *ispbase;
	u32 val, val1;

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	st_debug(ST_ISP, "interface type is %d(%s)\n",
			type, type == CSI_SENSOR ? "CSI" : "DVP");

	if (type == DVP_SENSOR) {
		unsigned int flags = dvp_dev->dvp->flags;

		st_debug(ST_ISP, "dvp flags = 0x%x, hsync active is %s, vsync active is %s\n",
			flags, flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH ? "high" : "low",
			flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH ? "high" : "low");
	}

	val = crop->left + (crop->top << 16);
	isp_format_reg_list[0].addr = ISP_REG_PIC_CAPTURE_START_CFG;
	isp_format_reg_list[0].val = val;

	val = (crop->width + crop->left - 1)
		+ ((crop->height + crop->top - 1) << 16);
	isp_format_reg_list[1].addr = ISP_REG_PIC_CAPTURE_END_CFG;
	isp_format_reg_list[1].val = val;

	val = crop->width + (crop->height << 16);
	isp_format_reg_list[2].addr = ISP_REG_PIPELINE_XY_SIZE;
	isp_format_reg_list[2].val = val;

	isp_format_reg_list[3].addr = ISP_REG_STRIDE;
	isp_format_reg_list[3].val = ALIGN(crop->width * bpp / 8, STFCAMSS_FRAME_WIDTH_ALIGN_8);

	switch (mcode) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		// 3 2 3 2 1 0 1 0 B Gb B Gb Gr R Gr R
		val = 0x0000EE44;
		val1 = 0x00000000;
		break;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		// 2 3 2 3 0 1 0 1, Gb B Gb B R Gr R Gr
		val = 0x0000BB11;
		val1 = 0x20000000;
		break;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		// 1 0 1 0 3 2 3 2, Gr R Gr R B Gb B Gb
		val = 0x000044EE;
		val1 = 0x30000000;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		// 0 1 0 1 2 3 2 3 R Gr R Gr Gb B Gb B
		val = 0x000011BB;
		val1 = 0x10000000;
		break;
	default:
		st_err(ST_ISP, "UNKNOW format\n");
		val = 0x000011BB;
		val1 = 0x10000000;
		break;
	}

	isp_format_reg_list[4].addr = ISP_REG_RAW_FORMAT_CFG;
	isp_format_reg_list[4].val = val;

	isp_format_reg_list[5].addr = ISP_REG_ISP_CTRL_1;
	isp_format_reg_list[5].val = val1;
	isp_format_reg_list[5].mask = 0xF0000000;

	st_info(ST_ISP, "src left: %d, top: %d, width = %d, height = %d, bpp = %d\n",
		crop->left, crop->top, crop->width, crop->height, bpp);

	crop = &crop_array[ISP_CROP].rect;
	bpp = crop_array[ISP_CROP].bpp;
	val = ALIGN(crop->width * bpp / 8, STFCAMSS_FRAME_WIDTH_ALIGN_128);
	isp_format_reg_list[6].addr = ISP_REG_DUMP_CFG_1;
	isp_format_reg_list[6].val = val | 3 << 16;
	isp_format_reg_list[6].mask = 0x0003FFFF;

	st_info(ST_ISP, "raw left: %d, top: %d, width = %d, height = %d, bpp = %d\n",
		crop->left, crop->top, crop->width, crop->height, bpp);

	crop = &crop_array[ISP_SCALE_SS0].rect;
	bpp = crop_array[ISP_SCALE_SS0].bpp;
	isp_format_reg_list[7].addr = ISP_REG_SS0IW;
	isp_format_reg_list[7].val = (crop->width << 16) + crop->height;
	isp_format_reg_list[8].addr = ISP_REG_SS0S;
	isp_format_reg_list[8].val = ALIGN(crop->width * bpp / 8, STFCAMSS_FRAME_WIDTH_ALIGN_8);

	st_info(ST_ISP, "ss0 left: %d, top: %d, width = %d, height = %d, bpp = %d\n",
		crop->left, crop->top, crop->width, crop->height, bpp);

	crop = &crop_array[ISP_SCALE_SS1].rect;
	bpp = crop_array[ISP_SCALE_SS1].bpp;
	isp_format_reg_list[9].addr = ISP_REG_SS1IW;
	isp_format_reg_list[9].val = (crop->width << 16) + crop->height;
	isp_format_reg_list[10].addr = ISP_REG_SS1S;
	isp_format_reg_list[10].val = ALIGN(crop->width * bpp / 8, STFCAMSS_FRAME_WIDTH_ALIGN_8);

	crop = &crop_array[ISP_ITIWS].rect;
	bpp = crop_array[ISP_ITIWS].bpp;
	isp_format_reg_list[11].addr = ISP_REG_ITIIWSR;
	isp_format_reg_list[11].val = (crop->height << 16) + crop->width;
	isp_format_reg_list[12].addr = ISP_REG_ITIDWLSR;
	isp_format_reg_list[12].val = ALIGN(crop->width * bpp / 8, STFCAMSS_FRAME_WIDTH_ALIGN_8);
	isp_format_reg_list[13].addr = ISP_REG_ITIDRLSR;
	isp_format_reg_list[13].val = ALIGN(crop->width * bpp / 8, STFCAMSS_FRAME_WIDTH_ALIGN_8);

	st_info(ST_ISP, "iti left: %d, top: %d, width = %d, height = %d, bpp = %d\n",
		crop->left, crop->top, crop->width, crop->height, bpp);

	isp_format_reg_list[14].addr = ISP_REG_SENSOR;
	isp_format_reg_list[14].val = 0x00000000;
	if (type == DVP_SENSOR) {
		unsigned int flags = dvp_dev->dvp->flags;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
			isp_format_reg_list[14].val |= 0x08;
		if (flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
			isp_format_reg_list[14].val |= 0x04;
	} else {
		isp_format_reg_list[14].val |= 0x01;
	}

	isp_load_regs(ispbase, isp_format_settings);
	return 0;
}

static int stf_isp_stream_set(struct stf_isp_dev *isp_dev, int on)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;

	void __iomem *ispbase;

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	if (on) {
#if defined(USE_NEW_CONFIG_SETTING)
		isp_load_regs(ispbase, isp_reg_start_settings);
#else
		reg_set_bit(ispbase, ISP_REG_CSIINTS_ADDR, 0x3FFFF, 0x3000a);
		reg_set_bit(ispbase, ISP_REG_IESHD_ADDR, BIT(1) | BIT(0), 0x3);
		reg_set_bit(ispbase, ISP_REG_ISP_CTRL_0, BIT(0), 1);
#endif //#if defined(USE_NEW_CONFIG_SETTING)
	}
	//else  //disable crash
	//	reg_set_bit(ispbase, ISP_REG_ISP_CTRL_0, BIT(0), 0);
	return 0;
}

static union reg_buf reg_buf;
static int stf_isp_reg_read(struct stf_isp_dev *isp_dev, void *arg)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;
	struct isp_reg_param *reg_param = arg;
	u32 size;
	unsigned long r;

	if (reg_param->reg_buf == NULL) {
		st_err(ST_ISP, "Failed to access register. The pointer is NULL!!!\n");
		return -EINVAL;
	}

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	size = 0;
	switch (reg_param->reg_info.method) {
	case STF_ISP_REG_METHOD_ONE_REG:
		break;

	case STF_ISP_REG_METHOD_SERIES:
		if (reg_param->reg_info.length > STF_ISP_REG_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_BUF_SIZE);
			return -EINVAL;
		}
		break;

	case STF_ISP_REG_METHOD_MODULE:
		/* This mode is not supported in the V4L2 version. */
		st_err(ST_ISP, "Reg Read - Failed to access register. The method = \
			STF_ISP_REG_METHOD_MODULE is not supported!!!\n");
		return -ENOTTY;

	case STF_ISP_REG_METHOD_TABLE:
		if (reg_param->reg_info.length > STF_ISP_REG_TBL_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_TBL_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length * 2;
		break;

	case STF_ISP_REG_METHOD_TABLE_2:
		if (reg_param->reg_info.length > STF_ISP_REG_TBL_2_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_TBL_2_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length * 3;
		break;

	case STF_ISP_REG_METHOD_TABLE_3:
		if (reg_param->reg_info.length > STF_ISP_REG_TBL_3_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_TBL_3_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length * 4;
		break;

	case STF_ISP_REG_METHOD_SMPL_PACK:
		st_err(ST_ISP, "Reg Read - Failed to access register. The method = \
			STF_ISP_REG_METHOD_SMPL_PACK is not supported!!!\n");
		return -ENOTTY;

	case STF_ISP_REG_METHOD_SOFT_RDMA:
		// This mode is not supported in the V4L2 version.
		st_err(ST_ISP, "Reg Read - Failed to access register. The method = \
			STF_ISP_REG_METHOD_SOFT_RDMA is not supported!!!\n");
		return -ENOTTY;

	default:
		st_err(ST_ISP, "Failed to access register. The method=%d \
			is not supported!!!\n", reg_param->reg_info.method);
		return -ENOTTY;
	}

	memset(&reg_buf, 0, sizeof(union reg_buf));
	if (size) {
		r = copy_from_user((u8 *)reg_buf.buffer,
			(u8 *)reg_param->reg_buf->buffer, size);
		if (r) {
			st_err(ST_ISP, "Failed to call copy_from_user for the \
				reg_param->reg_buf value\n");
			return -EIO;
		}
	}

	size = 0;
	switch (reg_param->reg_info.method) {
	case STF_ISP_REG_METHOD_ONE_REG:
		reg_buf.buffer[0] = reg_read(ispbase, reg_param->reg_info.offset);
		size = sizeof(u32);
		break;

	case STF_ISP_REG_METHOD_SERIES:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			reg_buf.buffer[r] = reg_read(ispbase,
				reg_param->reg_info.offset + (r * 4));
		}
		size = sizeof(u32) * reg_param->reg_info.length;
		break;

	case STF_ISP_REG_METHOD_MODULE:
		break;

	case STF_ISP_REG_METHOD_TABLE:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			reg_buf.reg_tbl[r].value = reg_read(ispbase,
				reg_buf.reg_tbl[r].offset);
		}
		size = sizeof(u32) * reg_param->reg_info.length * 2;
		break;

	case STF_ISP_REG_METHOD_TABLE_2:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			if (reg_buf.reg_tbl2[r].mask) {
				reg_buf.reg_tbl2[r].value = (reg_read(ispbase,
					reg_buf.reg_tbl2[r].offset)
						& reg_buf.reg_tbl2[r].mask);
			} else {
				reg_buf.reg_tbl2[r].value = reg_read(ispbase,
					reg_buf.reg_tbl2[r].offset);
			}
		}
		size = sizeof(u32) * reg_param->reg_info.length * 3;
		break;

	case STF_ISP_REG_METHOD_TABLE_3:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			if (reg_buf.reg_tbl3[r].mask) {
				reg_buf.reg_tbl3[r].value = (reg_read(ispbase,
					reg_buf.reg_tbl3[r].offset)
						& reg_buf.reg_tbl3[r].mask);
			} else {
				reg_buf.reg_tbl3[r].value = reg_read(ispbase,
					reg_buf.reg_tbl3[r].offset);
			}
			if (reg_buf.reg_tbl3[r].delay_ms) {
				usleep_range(1000 * reg_buf.reg_tbl3[r].delay_ms,
					1000 * reg_buf.reg_tbl3[r].delay_ms + 100);
			}
		}
		size = sizeof(u32) * reg_param->reg_info.length * 4;
		break;

	case STF_ISP_REG_METHOD_SMPL_PACK:
		break;

	case STF_ISP_REG_METHOD_SOFT_RDMA:
		break;

	default:
		break;
	}

	r = copy_to_user((u8 *)reg_param->reg_buf->buffer, (u8 *)reg_buf.buffer,
		size);
	if (r) {
		st_err(ST_ISP, "Failed to call copy_to_user for the \
			reg_param->buffer value\n");
		return -EIO;
	}

	return 0;
}

static int stf_isp_soft_rdma(struct stf_isp_dev *isp_dev, u32 rdma_addr)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;
	struct isp_rdma_info *rdma_info = NULL;
	s32 len;
	u32 offset;
	int ret = 0;

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	rdma_info = phys_to_virt(rdma_addr);
	while (1) {
		if (rdma_info->tag == RDMA_WR_ONE) {
			reg_write(ispbase, rdma_info->offset, rdma_info->param);
			rdma_info++;
		} else if (rdma_info->tag == RDMA_WR_SRL) {
			offset = rdma_info->offset;
			len = rdma_info->param;
			rdma_info++;
			while (len > 0) {
				reg_write(ispbase, offset, rdma_info->param);
				offset += 4;
				len--;
				if (len > 0) {
					reg_write(ispbase, offset, rdma_info->value);
					len--;
				}
				offset += 4;
				rdma_info++;
			}
		} else if (rdma_info->tag == RDMA_LINK) {
			rdma_info = phys_to_virt(rdma_info->param);
		} else if (rdma_info->tag == RDMA_SINT) {
			/* Software not support this command. */
			rdma_info++;
		} else if (rdma_info->tag == RDMA_END) {
			break;
		} else
			rdma_info++;
	}

	return ret;
}

static int stf_isp_reg_write(struct stf_isp_dev *isp_dev, void *arg)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;
	struct isp_reg_param *reg_param = arg;
	struct isp_rdma_info *rdma_info = NULL;
	s32 len;
	u32 offset;
	u32 size;
	unsigned long r;
	int ret = 0;

	if ((reg_param->reg_buf == NULL)
		&& (reg_param->reg_info.method != STF_ISP_REG_METHOD_SOFT_RDMA)) {
		st_err(ST_ISP, "Failed to access register. \
			The register buffer pointer is NULL!!!\n");
		return -EINVAL;
	}

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	size = 0;
	switch (reg_param->reg_info.method) {
	case STF_ISP_REG_METHOD_ONE_REG:
		size = sizeof(u32);
		break;

	case STF_ISP_REG_METHOD_SERIES:
		if (reg_param->reg_info.length > STF_ISP_REG_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length;
		break;

	case STF_ISP_REG_METHOD_MODULE:
		// This mode is not supported in the V4L2 version.
		st_err(ST_ISP, "Reg Write - Failed to access register. \
			The method = STF_ISP_REG_METHOD_MODULE is not supported!!!\n");
		return -ENOTTY;

	case STF_ISP_REG_METHOD_TABLE:
		if (reg_param->reg_info.length > STF_ISP_REG_TBL_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_TBL_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length * 2;
		break;

	case STF_ISP_REG_METHOD_TABLE_2:
		if (reg_param->reg_info.length > STF_ISP_REG_TBL_2_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_TBL_2_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length * 3;
		break;

	case STF_ISP_REG_METHOD_TABLE_3:
		if (reg_param->reg_info.length > STF_ISP_REG_TBL_3_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_TBL_3_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length * 4;
		break;

	case STF_ISP_REG_METHOD_SMPL_PACK:
		if (reg_param->reg_info.length > STF_ISP_REG_SMPL_PACK_BUF_SIZE) {
			st_err(ST_ISP, "Failed to access register. \
				The (length=0x%08X > 0x%08X) is out of size!!!\n",
				reg_param->reg_info.length, STF_ISP_REG_SMPL_PACK_BUF_SIZE);
			return -EINVAL;
		}
		size = sizeof(u32) * reg_param->reg_info.length * 2;
		break;

	case STF_ISP_REG_METHOD_SOFT_RDMA:
		break;

	default:
		st_err(ST_ISP, "Failed to access register. The method=%d \
			is not supported!!!\n", reg_param->reg_info.method);
		return -ENOTTY;
	}

	memset(&reg_buf, 0, sizeof(union reg_buf));
	if (size) {
		r = copy_from_user((u8 *)reg_buf.buffer,
			(u8 *)reg_param->reg_buf->buffer, size);
		if (r) {
			st_err(ST_ISP, "Failed to call copy_from_user for the \
				reg_param->reg_buf value\n");
			return -EIO;
		}
	}

	switch (reg_param->reg_info.method) {
	case STF_ISP_REG_METHOD_ONE_REG:
		reg_write(ispbase, reg_param->reg_info.offset, reg_buf.buffer[0]);
		break;

	case STF_ISP_REG_METHOD_SERIES:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			reg_write(ispbase, reg_param->reg_info.offset + (r * 4),
				reg_buf.buffer[r]);
		}
		break;

	case STF_ISP_REG_METHOD_MODULE:
		/* This mode is not supported in the V4L2 version. */
		break;

	case STF_ISP_REG_METHOD_TABLE:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			reg_write(ispbase, reg_buf.reg_tbl[r].offset,
				reg_buf.reg_tbl[r].value);
		}
		break;

	case STF_ISP_REG_METHOD_TABLE_2:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			if (reg_buf.reg_tbl2[r].mask) {
				reg_set_bit(ispbase, reg_buf.reg_tbl2[r].offset,
					reg_buf.reg_tbl2[r].mask, reg_buf.reg_tbl2[r].value);
			} else {
				reg_write(ispbase, reg_buf.reg_tbl2[r].offset,
					reg_buf.reg_tbl2[r].value);
			}
		}
		break;

	case STF_ISP_REG_METHOD_TABLE_3:
		for (r = 0; r < reg_param->reg_info.length; r++) {
			if (reg_buf.reg_tbl3[r].mask) {
				reg_set_bit(ispbase, reg_buf.reg_tbl3[r].offset,
					reg_buf.reg_tbl3[r].mask, reg_buf.reg_tbl3[r].value);
			} else {
				reg_write(ispbase, reg_buf.reg_tbl3[r].offset,
					reg_buf.reg_tbl3[r].value);
			}
			if (reg_buf.reg_tbl3[r].delay_ms) {
				usleep_range(1000 * reg_buf.reg_tbl3[r].delay_ms,
					1000 * reg_buf.reg_tbl3[r].delay_ms + 100);
			}
		}
		break;

	case STF_ISP_REG_METHOD_SMPL_PACK:
		size = reg_param->reg_info.length;
		rdma_info = &reg_buf.rdma_cmd[0];
		while (size) {
			if (rdma_info->tag == RDMA_WR_ONE) {
				reg_write(ispbase, rdma_info->offset, rdma_info->param);
				rdma_info++;
				size--;
			} else if (rdma_info->tag == RDMA_WR_SRL) {
				offset = rdma_info->offset;
				len = rdma_info->param;
				rdma_info++;
				size--;
				while (size && (len > 0)) {
					reg_write(ispbase, offset, rdma_info->param);
					offset += 4;
					len--;
					if (len > 0) {
						reg_write(ispbase, offset, rdma_info->value);
						len--;
					}
					offset += 4;
					rdma_info++;
					size--;
				}
			} else if (rdma_info->tag == RDMA_END) {
				break;
			} else {
				rdma_info++;
				size--;
			}
		}
		break;

	case STF_ISP_REG_METHOD_SOFT_RDMA:
		/*
		 * Simulation the hardware RDMA behavior to debug and verify
		 * the RDMA chain.
		 */
		ret = stf_isp_soft_rdma(isp_dev, reg_param->reg_info.offset);
		break;

	default:
		break;
	}

	return ret;
}

static int stf_isp_shadow_trigger(struct stf_isp_dev *isp_dev)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;

	ispbase = stf_isp_get_ispbase(isp_dev->id, vin);

	// shadow update
	reg_set_bit(ispbase, ISP_REG_CSIINTS_ADDR, (BIT(17) | BIT(16)), 0x30000);
	reg_set_bit(ispbase, ISP_REG_IESHD_ADDR, (BIT(1) | BIT(0)), 0x3);
	return 0;
}

void dump_isp_reg(void *__iomem ispbase, int id)
{
	int j;
	u32 addr, val;

	st_debug(ST_ISP, "DUMP ISP%d register:\n", id);

	for (j = 0; j < isp_reg_init_settings->regval_num; j++) {
		addr = isp_reg_init_settings->regval[j].addr;
		val = ioread32(ispbase + addr);
		st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", addr, val);
	}

	for (j = 0; j < isp_format_settings->regval_num; j++) {
		addr = isp_format_settings->regval[j].addr;
		val = ioread32(ispbase + addr);
		st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", addr, val);
	}

	val = ioread32(ispbase + ISP_REG_Y_PLANE_START_ADDR);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_Y_PLANE_START_ADDR, val);
	val = ioread32(ispbase + ISP_REG_UV_PLANE_START_ADDR);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_UV_PLANE_START_ADDR, val);
	val = ioread32(ispbase + ISP_REG_DUMP_CFG_0);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_DUMP_CFG_0, val);
	val = ioread32(ispbase + ISP_REG_DUMP_CFG_1);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_DUMP_CFG_1, val);

	for (j = 0; j < isp_reg_start_settings->regval_num; j++) {
		addr = isp_reg_start_settings->regval[j].addr;
		val = ioread32(ispbase + addr);
		st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", addr, val);
	}
}

struct isp_hw_ops isp_ops = {
	.isp_clk_enable        = stf_isp_clk_enable,
	.isp_clk_disable       = stf_isp_clk_disable,
	.isp_reset             = stf_isp_reset,
	.isp_config_set        = stf_isp_config_set,
	.isp_set_format        = stf_isp_set_format,
	.isp_stream_set        = stf_isp_stream_set,
	.isp_reg_read          = stf_isp_reg_read,
	.isp_reg_write         = stf_isp_reg_write,
	.isp_shadow_trigger    = stf_isp_shadow_trigger,
};
