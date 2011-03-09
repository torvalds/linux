/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "msm_fb.h"

#include <linux/memory.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include "linux/proc_fs.h"

#include <linux/delay.h>

#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>

/* #define TMD20QVGA_LCD_18BPP */
#define QVGA_WIDTH        240
#define QVGA_HEIGHT       320

#ifdef TMD20QVGA_LCD_18BPP
#define DISP_QVGA_18BPP(x)  ((((x)<<2) & 0x3FC00)|(( (x)<<1)& 0x1FE))
#define DISP_REG(name)  uint32 register_##name;
#define OUTPORT(x, y)  outpdw(x, y)
#define INPORT(x)   inpdw(x)
#else
#define DISP_QVGA_18BPP(x)  (x)
#define DISP_REG(name)  uint16 register_##name;
#define OUTPORT(x, y)  outpw(x, y)
#define INPORT(x)   intpw(x)
#endif

static void *DISP_CMD_PORT;
static void *DISP_DATA_PORT;

#define DISP_RNTI         0x10

#define DISP_CMD_OUT(cmd) OUTPORT(DISP_CMD_PORT, DISP_QVGA_18BPP(cmd))
#define DISP_DATA_OUT(data) OUTPORT(DISP_DATA_PORT, data)
#define DISP_DATA_IN() INPORT(DISP_DATA_PORT)

#if (defined(TMD20QVGA_LCD_18BPP))
#define DISP_DATA_OUT_16TO18BPP(x) \
	DISP_DATA_OUT((((x)&0xf800)<<2|((x)&0x80000)>>3) \
		     | (((x)&0x7e0)<<1) \
		     | (((x)&0x1F)<<1|((x)&0x10)>>4))
#else
#define DISP_DATA_OUT_16TO18BPP(x) \
	DISP_DATA_OUT(x)
#endif

#define DISP_WRITE_OUT(addr, data) \
   register_##addr = DISP_QVGA_18BPP(data); \
   DISP_CMD_OUT(addr); \
   DISP_DATA_OUT(register_##addr);

#define DISP_UPDATE_VALUE(addr, bitmask, data) \
   DISP_WRITE_OUT(##addr, (register_##addr & ~(bitmask)) | (data));

#define DISP_VAL_IF(bitvalue, bitmask) \
   ((bitvalue) ? (bitmask) : 0)

/* QVGA = 256 x 320 */
/* actual display is 240 x 320...offset by 0x10 */
#define DISP_ROW_COL_TO_ADDR(row, col) ((row) * 0x100 + col)
#define DISP_SET_RECT(ulhc_row, lrhc_row, ulhc_col, lrhc_col) \
   { \
   DISP_WRITE_OUT(DISP_HORZ_RAM_ADDR_POS_1_ADDR, (ulhc_col) + tmd20qvga_panel_offset); \
   DISP_WRITE_OUT(DISP_HORZ_RAM_ADDR_POS_2_ADDR, (lrhc_col) + tmd20qvga_panel_offset); \
   DISP_WRITE_OUT(DISP_VERT_RAM_ADDR_POS_1_ADDR, (ulhc_row)); \
   DISP_WRITE_OUT(DISP_VERT_RAM_ADDR_POS_2_ADDR, (lrhc_row)); \
   DISP_WRITE_OUT(DISP_RAM_ADDR_SET_1_ADDR, (ulhc_col) + tmd20qvga_panel_offset); \
   DISP_WRITE_OUT(DISP_RAM_ADDR_SET_2_ADDR, (ulhc_row)); \
   }

#define WAIT_MSEC(msec) mdelay(msec)

/*
 * TMD QVGA Address
 */
/* Display Control */
#define DISP_START_OSCILLATION_ADDR     0x000
DISP_REG(DISP_START_OSCILLATION_ADDR)
#define DISP_DRIVER_OUTPUT_CTL_ADDR     0x001
    DISP_REG(DISP_DRIVER_OUTPUT_CTL_ADDR)
#define DISP_LCD_DRIVING_SIG_ADDR     0x002
    DISP_REG(DISP_LCD_DRIVING_SIG_ADDR)
#define DISP_ENTRY_MODE_ADDR            0x003
    DISP_REG(DISP_ENTRY_MODE_ADDR)
#define DISP_DISPLAY_CTL_1_ADDR         0x007
    DISP_REG(DISP_DISPLAY_CTL_1_ADDR)
#define DISP_DISPLAY_CTL_2_ADDR         0x008
    DISP_REG(DISP_DISPLAY_CTL_2_ADDR)

/* DISPLAY MODE 0x009 partial display not supported */
#define DISP_POWER_SUPPLY_INTF_ADDR     0x00A
    DISP_REG(DISP_POWER_SUPPLY_INTF_ADDR)

/* DISPLAY MODE 0x00B xZoom feature is not supported */
#define DISP_EXT_DISPLAY_CTL_1_ADDR     0x00C
    DISP_REG(DISP_EXT_DISPLAY_CTL_1_ADDR)

#define DISP_FRAME_CYCLE_CTL_ADDR       0x00D
    DISP_REG(DISP_FRAME_CYCLE_CTL_ADDR)

#define DISP_EXT_DISPLAY_CTL_2_ADDR     0x00E
    DISP_REG(DISP_EXT_DISPLAY_CTL_2_ADDR)

#define DISP_EXT_DISPLAY_CTL_3_ADDR     0x00F
    DISP_REG(DISP_EXT_DISPLAY_CTL_3_ADDR)

#define DISP_LTPS_CTL_1_ADDR            0x012
    DISP_REG(DISP_LTPS_CTL_1_ADDR)
#define DISP_LTPS_CTL_2_ADDR            0x013
    DISP_REG(DISP_LTPS_CTL_2_ADDR)
#define DISP_LTPS_CTL_3_ADDR            0x014
    DISP_REG(DISP_LTPS_CTL_3_ADDR)
#define DISP_LTPS_CTL_4_ADDR            0x018
    DISP_REG(DISP_LTPS_CTL_4_ADDR)
#define DISP_LTPS_CTL_5_ADDR            0x019
    DISP_REG(DISP_LTPS_CTL_5_ADDR)
#define DISP_LTPS_CTL_6_ADDR            0x01A
    DISP_REG(DISP_LTPS_CTL_6_ADDR)
#define DISP_AMP_SETTING_ADDR           0x01C
    DISP_REG(DISP_AMP_SETTING_ADDR)
#define DISP_MODE_SETTING_ADDR          0x01D
    DISP_REG(DISP_MODE_SETTING_ADDR)
#define DISP_POFF_LN_SETTING_ADDR       0x01E
    DISP_REG(DISP_POFF_LN_SETTING_ADDR)
/* Power Contol */
#define DISP_POWER_CTL_1_ADDR           0x100
    DISP_REG(DISP_POWER_CTL_1_ADDR)
#define DISP_POWER_CTL_2_ADDR           0x101
    DISP_REG(DISP_POWER_CTL_2_ADDR)
#define DISP_POWER_CTL_3_ADDR           0x102
    DISP_REG(DISP_POWER_CTL_3_ADDR)
#define DISP_POWER_CTL_4_ADDR           0x103
    DISP_REG(DISP_POWER_CTL_4_ADDR)
#define DISP_POWER_CTL_5_ADDR           0x104
    DISP_REG(DISP_POWER_CTL_5_ADDR)
#define DISP_POWER_CTL_6_ADDR           0x105
    DISP_REG(DISP_POWER_CTL_6_ADDR)
#define DISP_POWER_CTL_7_ADDR           0x106
    DISP_REG(DISP_POWER_CTL_7_ADDR)
/* RAM Access */
#define DISP_RAM_ADDR_SET_1_ADDR        0x200
    DISP_REG(DISP_RAM_ADDR_SET_1_ADDR)
#define DISP_RAM_ADDR_SET_2_ADDR        0x201
    DISP_REG(DISP_RAM_ADDR_SET_2_ADDR)
#define DISP_CMD_RAMRD                  DISP_CMD_RAMWR
#define DISP_CMD_RAMWR                  0x202
    DISP_REG(DISP_CMD_RAMWR)
#define DISP_RAM_DATA_MASK_1_ADDR       0x203
    DISP_REG(DISP_RAM_DATA_MASK_1_ADDR)
#define DISP_RAM_DATA_MASK_2_ADDR       0x204
    DISP_REG(DISP_RAM_DATA_MASK_2_ADDR)
/* Gamma Control, Contrast, Gray Scale Setting */
#define DISP_GAMMA_CONTROL_1_ADDR       0x300
    DISP_REG(DISP_GAMMA_CONTROL_1_ADDR)
#define DISP_GAMMA_CONTROL_2_ADDR       0x301
    DISP_REG(DISP_GAMMA_CONTROL_2_ADDR)
#define DISP_GAMMA_CONTROL_3_ADDR       0x302
    DISP_REG(DISP_GAMMA_CONTROL_3_ADDR)
#define DISP_GAMMA_CONTROL_4_ADDR       0x303
    DISP_REG(DISP_GAMMA_CONTROL_4_ADDR)
#define DISP_GAMMA_CONTROL_5_ADDR       0x304
    DISP_REG(DISP_GAMMA_CONTROL_5_ADDR)
/* Coordinate Control */
#define DISP_VERT_SCROLL_CTL_1_ADDR     0x400
    DISP_REG(DISP_VERT_SCROLL_CTL_1_ADDR)
#define DISP_VERT_SCROLL_CTL_2_ADDR     0x401
    DISP_REG(DISP_VERT_SCROLL_CTL_2_ADDR)
#define DISP_SCREEN_1_DRV_POS_1_ADDR    0x402
    DISP_REG(DISP_SCREEN_1_DRV_POS_1_ADDR)
#define DISP_SCREEN_1_DRV_POS_2_ADDR    0x403
    DISP_REG(DISP_SCREEN_1_DRV_POS_2_ADDR)
#define DISP_SCREEN_2_DRV_POS_1_ADDR    0x404
    DISP_REG(DISP_SCREEN_2_DRV_POS_1_ADDR)
#define DISP_SCREEN_2_DRV_POS_2_ADDR    0x405
    DISP_REG(DISP_SCREEN_2_DRV_POS_2_ADDR)
#define DISP_HORZ_RAM_ADDR_POS_1_ADDR   0x406
    DISP_REG(DISP_HORZ_RAM_ADDR_POS_1_ADDR)
#define DISP_HORZ_RAM_ADDR_POS_2_ADDR   0x407
    DISP_REG(DISP_HORZ_RAM_ADDR_POS_2_ADDR)
#define DISP_VERT_RAM_ADDR_POS_1_ADDR   0x408
    DISP_REG(DISP_VERT_RAM_ADDR_POS_1_ADDR)
#define DISP_VERT_RAM_ADDR_POS_2_ADDR   0x409
    DISP_REG(DISP_VERT_RAM_ADDR_POS_2_ADDR)
#define DISP_TMD_700_ADDR               0x700	/*  0x700 */
    DISP_REG(DISP_TMD_700_ADDR)
#define DISP_TMD_015_ADDR               0x015	/*  0x700 */
    DISP_REG(DISP_TMD_015_ADDR)
#define DISP_TMD_305_ADDR               0x305	/*  0x700 */
    DISP_REG(DISP_TMD_305_ADDR)

/*
 * TMD QVGA Bit Definations
 */

#define DISP_BIT_IB15              0x8000
#define DISP_BIT_IB14              0x4000
#define DISP_BIT_IB13              0x2000
#define DISP_BIT_IB12              0x1000
#define DISP_BIT_IB11              0x0800
#define DISP_BIT_IB10              0x0400
#define DISP_BIT_IB09              0x0200
#define DISP_BIT_IB08              0x0100
#define DISP_BIT_IB07              0x0080
#define DISP_BIT_IB06              0x0040
#define DISP_BIT_IB05              0x0020
#define DISP_BIT_IB04              0x0010
#define DISP_BIT_IB03              0x0008
#define DISP_BIT_IB02              0x0004
#define DISP_BIT_IB01              0x0002
#define DISP_BIT_IB00              0x0001
/*
 * Display Control
 * DISP_START_OSCILLATION_ADDR     Start Oscillation
 * DISP_DRIVER_OUTPUT_CTL_ADDR     Driver Output Control
 */
#define DISP_BITMASK_SS            DISP_BIT_IB08
#define DISP_BITMASK_NL5           DISP_BIT_IB05
#define DISP_BITMASK_NL4           DISP_BIT_IB04
#define DISP_BITMASK_NL3           DISP_BIT_IB03
#define DISP_BITMASK_NL2           DISP_BIT_IB02
#define DISP_BITMASK_NL1           DISP_BIT_IB01
#define DISP_BITMASK_NL0           DISP_BIT_IB00
/* DISP_LCD_DRIVING_SIG_ADDR       LCD Driving Signal Setting */
#define DISP_BITMASK_BC            DISP_BIT_IB09
/* DISP_ENTRY_MODE_ADDR            Entry Mode */
#define DISP_BITMASK_TRI           DISP_BIT_IB15
#define DISP_BITMASK_DFM1          DISP_BIT_IB14
#define DISP_BITMASK_DFM0          DISP_BIT_IB13
#define DISP_BITMASK_BGR           DISP_BIT_IB12
#define DISP_BITMASK_HWM0          DISP_BIT_IB08
#define DISP_BITMASK_ID1           DISP_BIT_IB05
#define DISP_BITMASK_ID0           DISP_BIT_IB04
#define DISP_BITMASK_AM            DISP_BIT_IB03
/* DISP_DISPLAY_CTL_1_ADDR         Display Control (1) */
#define DISP_BITMASK_COL1          DISP_BIT_IB15
#define DISP_BITMASK_COL0          DISP_BIT_IB14
#define DISP_BITMASK_VLE2          DISP_BIT_IB10
#define DISP_BITMASK_VLE1          DISP_BIT_IB09
#define DISP_BITMASK_SPT           DISP_BIT_IB08
#define DISP_BITMASK_PT1           DISP_BIT_IB07
#define DISP_BITMASK_PT0           DISP_BIT_IB06
#define DISP_BITMASK_REV           DISP_BIT_IB02
/* DISP_DISPLAY_CTL_2_ADDR         Display Control (2) */
#define DISP_BITMASK_FP3           DISP_BIT_IB11
#define DISP_BITMASK_FP2           DISP_BIT_IB10
#define DISP_BITMASK_FP1           DISP_BIT_IB09
#define DISP_BITMASK_FP0           DISP_BIT_IB08
#define DISP_BITMASK_BP3           DISP_BIT_IB03
#define DISP_BITMASK_BP2           DISP_BIT_IB02
#define DISP_BITMASK_BP1           DISP_BIT_IB01
#define DISP_BITMASK_BP0           DISP_BIT_IB00
/* DISP_POWER_SUPPLY_INTF_ADDR     Power Supply IC Interface Control */
#define DISP_BITMASK_CSE           DISP_BIT_IB12
#define DISP_BITMASK_TE            DISP_BIT_IB08
#define DISP_BITMASK_IX3           DISP_BIT_IB03
#define DISP_BITMASK_IX2           DISP_BIT_IB02
#define DISP_BITMASK_IX1           DISP_BIT_IB01
#define DISP_BITMASK_IX0           DISP_BIT_IB00
/* DISP_EXT_DISPLAY_CTL_1_ADDR     External Display Interface Control (1) */
#define DISP_BITMASK_RM            DISP_BIT_IB08
#define DISP_BITMASK_DM1           DISP_BIT_IB05
#define DISP_BITMASK_DM0           DISP_BIT_IB04
#define DISP_BITMASK_RIM1          DISP_BIT_IB01
#define DISP_BITMASK_RIM0          DISP_BIT_IB00
/* DISP_FRAME_CYCLE_CTL_ADDR       Frame Frequency Adjustment Control */
#define DISP_BITMASK_DIVI1         DISP_BIT_IB09
#define DISP_BITMASK_DIVI0         DISP_BIT_IB08
#define DISP_BITMASK_RTNI4         DISP_BIT_IB04
#define DISP_BITMASK_RTNI3         DISP_BIT_IB03
#define DISP_BITMASK_RTNI2         DISP_BIT_IB02
#define DISP_BITMASK_RTNI1         DISP_BIT_IB01
#define DISP_BITMASK_RTNI0         DISP_BIT_IB00
/* DISP_EXT_DISPLAY_CTL_2_ADDR     External Display Interface Control (2) */
#define DISP_BITMASK_DIVE1         DISP_BIT_IB09
#define DISP_BITMASK_DIVE0         DISP_BIT_IB08
#define DISP_BITMASK_RTNE7         DISP_BIT_IB07
#define DISP_BITMASK_RTNE6         DISP_BIT_IB06
#define DISP_BITMASK_RTNE5         DISP_BIT_IB05
#define DISP_BITMASK_RTNE4         DISP_BIT_IB04
#define DISP_BITMASK_RTNE3         DISP_BIT_IB03
#define DISP_BITMASK_RTNE2         DISP_BIT_IB02
#define DISP_BITMASK_RTNE1         DISP_BIT_IB01
#define DISP_BITMASK_RTNE0         DISP_BIT_IB00
/* DISP_EXT_DISPLAY_CTL_3_ADDR     External Display Interface Control (3) */
#define DISP_BITMASK_VSPL          DISP_BIT_IB04
#define DISP_BITMASK_HSPL          DISP_BIT_IB03
#define DISP_BITMASK_VPL           DISP_BIT_IB02
#define DISP_BITMASK_EPL           DISP_BIT_IB01
#define DISP_BITMASK_DPL           DISP_BIT_IB00
/* DISP_LTPS_CTL_1_ADDR            LTPS Interface Control (1) */
#define DISP_BITMASK_CLWI3         DISP_BIT_IB11
#define DISP_BITMASK_CLWI2         DISP_BIT_IB10
#define DISP_BITMASK_CLWI1         DISP_BIT_IB09
#define DISP_BITMASK_CLWI0         DISP_BIT_IB08
#define DISP_BITMASK_CLTI1         DISP_BIT_IB01
#define DISP_BITMASK_CLTI0         DISP_BIT_IB00
/* DISP_LTPS_CTL_2_ADDR            LTPS Interface Control (2) */
#define DISP_BITMASK_OEVBI1        DISP_BIT_IB09
#define DISP_BITMASK_OEVBI0        DISP_BIT_IB08
#define DISP_BITMASK_OEVFI1        DISP_BIT_IB01
#define DISP_BITMASK_OEVFI0        DISP_BIT_IB00
/* DISP_LTPS_CTL_3_ADDR            LTPS Interface Control (3) */
#define DISP_BITMASK_SHI1          DISP_BIT_IB01
#define DISP_BITMASK_SHI0          DISP_BIT_IB00
/* DISP_LTPS_CTL_4_ADDR            LTPS Interface Control (4) */
#define DISP_BITMASK_CLWE5         DISP_BIT_IB13
#define DISP_BITMASK_CLWE4         DISP_BIT_IB12
#define DISP_BITMASK_CLWE3         DISP_BIT_IB11
#define DISP_BITMASK_CLWE2         DISP_BIT_IB10
#define DISP_BITMASK_CLWE1         DISP_BIT_IB09
#define DISP_BITMASK_CLWE0         DISP_BIT_IB08
#define DISP_BITMASK_CLTE3         DISP_BIT_IB03
#define DISP_BITMASK_CLTE2         DISP_BIT_IB02
#define DISP_BITMASK_CLTE1         DISP_BIT_IB01
#define DISP_BITMASK_CLTE0         DISP_BIT_IB00
/* DISP_LTPS_CTL_5_ADDR            LTPS Interface Control (5) */
#define DISP_BITMASK_OEVBE3        DISP_BIT_IB11
#define DISP_BITMASK_OEVBE2        DISP_BIT_IB10
#define DISP_BITMASK_OEVBE1        DISP_BIT_IB09
#define DISP_BITMASK_OEVBE0        DISP_BIT_IB08
#define DISP_BITMASK_OEVFE3        DISP_BIT_IB03
#define DISP_BITMASK_OEVFE2        DISP_BIT_IB02
#define DISP_BITMASK_OEVFE1        DISP_BIT_IB01
#define DISP_BITMASK_OEVFE0        DISP_BIT_IB00
/* DISP_LTPS_CTL_6_ADDR            LTPS Interface Control (6) */
#define DISP_BITMASK_SHE3          DISP_BIT_IB03
#define DISP_BITMASK_SHE2          DISP_BIT_IB02
#define DISP_BITMASK_SHE1          DISP_BIT_IB01
#define DISP_BITMASK_SHE0          DISP_BIT_IB00
/* DISP_AMP_SETTING_ADDR           Amplify Setting */
#define DISP_BITMASK_ABSW1         DISP_BIT_IB01
#define DISP_BITMASK_ABSW0         DISP_BIT_IB00
/* DISP_MODE_SETTING_ADDR          Mode Setting */
#define DISP_BITMASK_DSTB          DISP_BIT_IB02
#define DISP_BITMASK_STB           DISP_BIT_IB00
/* DISP_POFF_LN_SETTING_ADDR       Power Off Line Setting */
#define DISP_BITMASK_POFH3         DISP_BIT_IB03
#define DISP_BITMASK_POFH2         DISP_BIT_IB02
#define DISP_BITMASK_POFH1         DISP_BIT_IB01
#define DISP_BITMASK_POFH0         DISP_BIT_IB00

/* Power Contol */
/* DISP_POWER_CTL_1_ADDR           Power Control (1) */
#define DISP_BITMASK_PO            DISP_BIT_IB11
#define DISP_BITMASK_VCD           DISP_BIT_IB09
#define DISP_BITMASK_VSC           DISP_BIT_IB08
#define DISP_BITMASK_CON           DISP_BIT_IB07
#define DISP_BITMASK_ASW1          DISP_BIT_IB06
#define DISP_BITMASK_ASW0          DISP_BIT_IB05
#define DISP_BITMASK_OEV           DISP_BIT_IB04
#define DISP_BITMASK_OEVE          DISP_BIT_IB03
#define DISP_BITMASK_FR            DISP_BIT_IB02
#define DISP_BITMASK_D1            DISP_BIT_IB01
#define DISP_BITMASK_D0            DISP_BIT_IB00
/* DISP_POWER_CTL_2_ADDR           Power Control (2) */
#define DISP_BITMASK_DC4           DISP_BIT_IB15
#define DISP_BITMASK_DC3           DISP_BIT_IB14
#define DISP_BITMASK_SAP2          DISP_BIT_IB13
#define DISP_BITMASK_SAP1          DISP_BIT_IB12
#define DISP_BITMASK_SAP0          DISP_BIT_IB11
#define DISP_BITMASK_BT2           DISP_BIT_IB10
#define DISP_BITMASK_BT1           DISP_BIT_IB09
#define DISP_BITMASK_BT0           DISP_BIT_IB08
#define DISP_BITMASK_DC2           DISP_BIT_IB07
#define DISP_BITMASK_DC1           DISP_BIT_IB06
#define DISP_BITMASK_DC0           DISP_BIT_IB05
#define DISP_BITMASK_AP2           DISP_BIT_IB04
#define DISP_BITMASK_AP1           DISP_BIT_IB03
#define DISP_BITMASK_AP0           DISP_BIT_IB02
/* DISP_POWER_CTL_3_ADDR           Power Control (3) */
#define DISP_BITMASK_VGL4          DISP_BIT_IB10
#define DISP_BITMASK_VGL3          DISP_BIT_IB09
#define DISP_BITMASK_VGL2          DISP_BIT_IB08
#define DISP_BITMASK_VGL1          DISP_BIT_IB07
#define DISP_BITMASK_VGL0          DISP_BIT_IB06
#define DISP_BITMASK_VGH4          DISP_BIT_IB04
#define DISP_BITMASK_VGH3          DISP_BIT_IB03
#define DISP_BITMASK_VGH2          DISP_BIT_IB02
#define DISP_BITMASK_VGH1          DISP_BIT_IB01
#define DISP_BITMASK_VGH0          DISP_BIT_IB00
/* DISP_POWER_CTL_4_ADDR           Power Control (4) */
#define DISP_BITMASK_VC2           DISP_BIT_IB02
#define DISP_BITMASK_VC1           DISP_BIT_IB01
#define DISP_BITMASK_VC0           DISP_BIT_IB00
/* DISP_POWER_CTL_5_ADDR           Power Control (5) */
#define DISP_BITMASK_VRL3          DISP_BIT_IB11
#define DISP_BITMASK_VRL2          DISP_BIT_IB10
#define DISP_BITMASK_VRL1          DISP_BIT_IB09
#define DISP_BITMASK_VRL0          DISP_BIT_IB08
#define DISP_BITMASK_PON           DISP_BIT_IB04
#define DISP_BITMASK_VRH3          DISP_BIT_IB03
#define DISP_BITMASK_VRH2          DISP_BIT_IB02
#define DISP_BITMASK_VRH1          DISP_BIT_IB01
#define DISP_BITMASK_VRH0          DISP_BIT_IB00
/* DISP_POWER_CTL_6_ADDR           Power Control (6) */
#define DISP_BITMASK_VCOMG         DISP_BIT_IB13
#define DISP_BITMASK_VDV4          DISP_BIT_IB12
#define DISP_BITMASK_VDV3          DISP_BIT_IB11
#define DISP_BITMASK_VDV2          DISP_BIT_IB10
#define DISP_BITMASK_VDV1          DISP_BIT_IB09
#define DISP_BITMASK_VDV0          DISP_BIT_IB08
#define DISP_BITMASK_VCM4          DISP_BIT_IB04
#define DISP_BITMASK_VCM3          DISP_BIT_IB03
#define DISP_BITMASK_VCM2          DISP_BIT_IB02
#define DISP_BITMASK_VCM1          DISP_BIT_IB01
#define DISP_BITMASK_VCM0          DISP_BIT_IB00
/* RAM Access */
/* DISP_RAM_ADDR_SET_1_ADDR        RAM Address Set (1) */
#define DISP_BITMASK_AD7           DISP_BIT_IB07
#define DISP_BITMASK_AD6           DISP_BIT_IB06
#define DISP_BITMASK_AD5           DISP_BIT_IB05
#define DISP_BITMASK_AD4           DISP_BIT_IB04
#define DISP_BITMASK_AD3           DISP_BIT_IB03
#define DISP_BITMASK_AD2           DISP_BIT_IB02
#define DISP_BITMASK_AD1           DISP_BIT_IB01
#define DISP_BITMASK_AD0           DISP_BIT_IB00
/* DISP_RAM_ADDR_SET_2_ADDR        RAM Address Set (2) */
#define DISP_BITMASK_AD16          DISP_BIT_IB08
#define DISP_BITMASK_AD15          DISP_BIT_IB07
#define DISP_BITMASK_AD14          DISP_BIT_IB06
#define DISP_BITMASK_AD13          DISP_BIT_IB05
#define DISP_BITMASK_AD12          DISP_BIT_IB04
#define DISP_BITMASK_AD11          DISP_BIT_IB03
#define DISP_BITMASK_AD10          DISP_BIT_IB02
#define DISP_BITMASK_AD9           DISP_BIT_IB01
#define DISP_BITMASK_AD8           DISP_BIT_IB00
/*
 * DISP_CMD_RAMWR       RAM Data Read/Write
 * Use Data Bit Configuration
 */
/* DISP_RAM_DATA_MASK_1_ADDR       RAM Write Data Mask (1) */
#define DISP_BITMASK_WM11          DISP_BIT_IB13
#define DISP_BITMASK_WM10          DISP_BIT_IB12
#define DISP_BITMASK_WM9           DISP_BIT_IB11
#define DISP_BITMASK_WM8           DISP_BIT_IB10
#define DISP_BITMASK_WM7           DISP_BIT_IB09
#define DISP_BITMASK_WM6           DISP_BIT_IB08
#define DISP_BITMASK_WM5           DISP_BIT_IB05
#define DISP_BITMASK_WM4           DISP_BIT_IB04
#define DISP_BITMASK_WM3           DISP_BIT_IB03
#define DISP_BITMASK_WM2           DISP_BIT_IB02
#define DISP_BITMASK_WM1           DISP_BIT_IB01
#define DISP_BITMASK_WM0           DISP_BIT_IB00
/* DISP_RAM_DATA_MASK_2_ADDR       RAM Write Data Mask (2) */
#define DISP_BITMASK_WM17          DISP_BIT_IB05
#define DISP_BITMASK_WM16          DISP_BIT_IB04
#define DISP_BITMASK_WM15          DISP_BIT_IB03
#define DISP_BITMASK_WM14          DISP_BIT_IB02
#define DISP_BITMASK_WM13          DISP_BIT_IB01
#define DISP_BITMASK_WM12          DISP_BIT_IB00
/*Gamma Control */
/* DISP_GAMMA_CONTROL_1_ADDR       Gamma Control (1) */
#define DISP_BITMASK_PKP12         DISP_BIT_IB10
#define DISP_BITMASK_PKP11         DISP_BIT_IB08
#define DISP_BITMASK_PKP10         DISP_BIT_IB09
#define DISP_BITMASK_PKP02         DISP_BIT_IB02
#define DISP_BITMASK_PKP01         DISP_BIT_IB01
#define DISP_BITMASK_PKP00         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_2_ADDR       Gamma Control (2) */
#define DISP_BITMASK_PKP32         DISP_BIT_IB10
#define DISP_BITMASK_PKP31         DISP_BIT_IB09
#define DISP_BITMASK_PKP30         DISP_BIT_IB08
#define DISP_BITMASK_PKP22         DISP_BIT_IB02
#define DISP_BITMASK_PKP21         DISP_BIT_IB01
#define DISP_BITMASK_PKP20         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_3_ADDR       Gamma Control (3) */
#define DISP_BITMASK_PKP52         DISP_BIT_IB10
#define DISP_BITMASK_PKP51         DISP_BIT_IB09
#define DISP_BITMASK_PKP50         DISP_BIT_IB08
#define DISP_BITMASK_PKP42         DISP_BIT_IB02
#define DISP_BITMASK_PKP41         DISP_BIT_IB01
#define DISP_BITMASK_PKP40         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_4_ADDR       Gamma Control (4) */
#define DISP_BITMASK_PRP12         DISP_BIT_IB10
#define DISP_BITMASK_PRP11         DISP_BIT_IB08
#define DISP_BITMASK_PRP10         DISP_BIT_IB09
#define DISP_BITMASK_PRP02         DISP_BIT_IB02
#define DISP_BITMASK_PRP01         DISP_BIT_IB01
#define DISP_BITMASK_PRP00         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_5_ADDR       Gamma Control (5) */
#define DISP_BITMASK_VRP14         DISP_BIT_IB12
#define DISP_BITMASK_VRP13         DISP_BIT_IB11
#define DISP_BITMASK_VRP12         DISP_BIT_IB10
#define DISP_BITMASK_VRP11         DISP_BIT_IB08
#define DISP_BITMASK_VRP10         DISP_BIT_IB09
#define DISP_BITMASK_VRP03         DISP_BIT_IB03
#define DISP_BITMASK_VRP02         DISP_BIT_IB02
#define DISP_BITMASK_VRP01         DISP_BIT_IB01
#define DISP_BITMASK_VRP00         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_6_ADDR       Gamma Control (6) */
#define DISP_BITMASK_PKN12         DISP_BIT_IB10
#define DISP_BITMASK_PKN11         DISP_BIT_IB08
#define DISP_BITMASK_PKN10         DISP_BIT_IB09
#define DISP_BITMASK_PKN02         DISP_BIT_IB02
#define DISP_BITMASK_PKN01         DISP_BIT_IB01
#define DISP_BITMASK_PKN00         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_7_ADDR       Gamma Control (7) */
#define DISP_BITMASK_PKN32         DISP_BIT_IB10
#define DISP_BITMASK_PKN31         DISP_BIT_IB08
#define DISP_BITMASK_PKN30         DISP_BIT_IB09
#define DISP_BITMASK_PKN22         DISP_BIT_IB02
#define DISP_BITMASK_PKN21         DISP_BIT_IB01
#define DISP_BITMASK_PKN20         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_8_ADDR       Gamma Control (8) */
#define DISP_BITMASK_PKN52         DISP_BIT_IB10
#define DISP_BITMASK_PKN51         DISP_BIT_IB08
#define DISP_BITMASK_PKN50         DISP_BIT_IB09
#define DISP_BITMASK_PKN42         DISP_BIT_IB02
#define DISP_BITMASK_PKN41         DISP_BIT_IB01
#define DISP_BITMASK_PKN40         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_9_ADDR       Gamma Control (9) */
#define DISP_BITMASK_PRN12         DISP_BIT_IB10
#define DISP_BITMASK_PRN11         DISP_BIT_IB08
#define DISP_BITMASK_PRN10         DISP_BIT_IB09
#define DISP_BITMASK_PRN02         DISP_BIT_IB02
#define DISP_BITMASK_PRN01         DISP_BIT_IB01
#define DISP_BITMASK_PRN00         DISP_BIT_IB00
/* DISP_GAMMA_CONTROL_10_ADDR      Gamma Control (10) */
#define DISP_BITMASK_VRN14         DISP_BIT_IB12
#define DISP_BITMASK_VRN13         DISP_BIT_IB11
#define DISP_BITMASK_VRN12         DISP_BIT_IB10
#define DISP_BITMASK_VRN11         DISP_BIT_IB08
#define DISP_BITMASK_VRN10         DISP_BIT_IB09
#define DISP_BITMASK_VRN03         DISP_BIT_IB03
#define DISP_BITMASK_VRN02         DISP_BIT_IB02
#define DISP_BITMASK_VRN01         DISP_BIT_IB01
#define DISP_BITMASK_VRN00         DISP_BIT_IB00
/* Coordinate Control */
/* DISP_VERT_SCROLL_CTL_1_ADDR     Vertical Scroll Control (1) */
#define DISP_BITMASK_VL18          DISP_BIT_IB08
#define DISP_BITMASK_VL17          DISP_BIT_IB07
#define DISP_BITMASK_VL16          DISP_BIT_IB06
#define DISP_BITMASK_VL15          DISP_BIT_IB05
#define DISP_BITMASK_VL14          DISP_BIT_IB04
#define DISP_BITMASK_VL13          DISP_BIT_IB03
#define DISP_BITMASK_VL12          DISP_BIT_IB02
#define DISP_BITMASK_VL11          DISP_BIT_IB01
#define DISP_BITMASK_VL10          DISP_BIT_IB00
/* DISP_VERT_SCROLL_CTL_2_ADDR     Vertical Scroll Control (2) */
#define DISP_BITMASK_VL28          DISP_BIT_IB08
#define DISP_BITMASK_VL27          DISP_BIT_IB07
#define DISP_BITMASK_VL26          DISP_BIT_IB06
#define DISP_BITMASK_VL25          DISP_BIT_IB05
#define DISP_BITMASK_VL24          DISP_BIT_IB04
#define DISP_BITMASK_VL23          DISP_BIT_IB03
#define DISP_BITMASK_VL22          DISP_BIT_IB02
#define DISP_BITMASK_VL21          DISP_BIT_IB01
#define DISP_BITMASK_VL20          DISP_BIT_IB00
/* DISP_SCREEN_1_DRV_POS_1_ADDR    First Screen Driving Position (1) */
#define DISP_BITMASK_SS18          DISP_BIT_IB08
#define DISP_BITMASK_SS17          DISP_BIT_IB07
#define DISP_BITMASK_SS16          DISP_BIT_IB06
#define DISP_BITMASK_SS15          DISP_BIT_IB05
#define DISP_BITMASK_SS14          DISP_BIT_IB04
#define DISP_BITMASK_SS13          DISP_BIT_IB03
#define DISP_BITMASK_SS12          DISP_BIT_IB02
#define DISP_BITMASK_SS11          DISP_BIT_IB01
#define DISP_BITMASK_SS10          DISP_BIT_IB00
/* DISP_SCREEN_1_DRV_POS_2_ADDR    First Screen Driving Position (2) */
#define DISP_BITMASK_SE18          DISP_BIT_IB08
#define DISP_BITMASK_SE17          DISP_BIT_IB07
#define DISP_BITMASK_SE16          DISP_BIT_IB06
#define DISP_BITMASK_SE15          DISP_BIT_IB05
#define DISP_BITMASK_SE14          DISP_BIT_IB04
#define DISP_BITMASK_SE13          DISP_BIT_IB03
#define DISP_BITMASK_SE12          DISP_BIT_IB02
#define DISP_BITMASK_SE11          DISP_BIT_IB01
#define DISP_BITMASK_SE10          DISP_BIT_IB00
/* DISP_SCREEN_2_DRV_POS_1_ADDR    Second Screen Driving Position (1) */
#define DISP_BITMASK_SS28          DISP_BIT_IB08
#define DISP_BITMASK_SS27          DISP_BIT_IB07
#define DISP_BITMASK_SS26          DISP_BIT_IB06
#define DISP_BITMASK_SS25          DISP_BIT_IB05
#define DISP_BITMASK_SS24          DISP_BIT_IB04
#define DISP_BITMASK_SS23          DISP_BIT_IB03
#define DISP_BITMASK_SS22          DISP_BIT_IB02
#define DISP_BITMASK_SS21          DISP_BIT_IB01
#define DISP_BITMASK_SS20          DISP_BIT_IB00
/* DISP_SCREEN_3_DRV_POS_2_ADDR    Second Screen Driving Position (2) */
#define DISP_BITMASK_SE28          DISP_BIT_IB08
#define DISP_BITMASK_SE27          DISP_BIT_IB07
#define DISP_BITMASK_SE26          DISP_BIT_IB06
#define DISP_BITMASK_SE25          DISP_BIT_IB05
#define DISP_BITMASK_SE24          DISP_BIT_IB04
#define DISP_BITMASK_SE23          DISP_BIT_IB03
#define DISP_BITMASK_SE22          DISP_BIT_IB02
#define DISP_BITMASK_SE21          DISP_BIT_IB01
#define DISP_BITMASK_SE20          DISP_BIT_IB00
/* DISP_HORZ_RAM_ADDR_POS_1_ADDR   Horizontal RAM Address Position (1) */
#define DISP_BITMASK_HSA7          DISP_BIT_IB07
#define DISP_BITMASK_HSA6          DISP_BIT_IB06
#define DISP_BITMASK_HSA5          DISP_BIT_IB05
#define DISP_BITMASK_HSA4          DISP_BIT_IB04
#define DISP_BITMASK_HSA3          DISP_BIT_IB03
#define DISP_BITMASK_HSA2          DISP_BIT_IB02
#define DISP_BITMASK_HSA1          DISP_BIT_IB01
#define DISP_BITMASK_HSA0          DISP_BIT_IB00
/* DISP_HORZ_RAM_ADDR_POS_2_ADDR   Horizontal RAM Address Position (2) */
#define DISP_BITMASK_HEA7          DISP_BIT_IB07
#define DISP_BITMASK_HEA6          DISP_BIT_IB06
#define DISP_BITMASK_HEA5          DISP_BIT_IB05
#define DISP_BITMASK_HEA4          DISP_BIT_IB04
#define DISP_BITMASK_HEA3          DISP_BIT_IB03
#define DISP_BITMASK_HEA2          DISP_BIT_IB02
#define DISP_BITMASK_HEA1          DISP_BIT_IB01
#define DISP_BITMASK_HEA0          DISP_BIT_IB00
/* DISP_VERT_RAM_ADDR_POS_1_ADDR   Vertical RAM Address Position (1) */
#define DISP_BITMASK_VSA8          DISP_BIT_IB08
#define DISP_BITMASK_VSA7          DISP_BIT_IB07
#define DISP_BITMASK_VSA6          DISP_BIT_IB06
#define DISP_BITMASK_VSA5          DISP_BIT_IB05
#define DISP_BITMASK_VSA4          DISP_BIT_IB04
#define DISP_BITMASK_VSA3          DISP_BIT_IB03
#define DISP_BITMASK_VSA2          DISP_BIT_IB02
#define DISP_BITMASK_VSA1          DISP_BIT_IB01
#define DISP_BITMASK_VSA0          DISP_BIT_IB00
/* DISP_VERT_RAM_ADDR_POS_2_ADDR   Vertical RAM Address Position (2) */
#define DISP_BITMASK_VEA8          DISP_BIT_IB08
#define DISP_BITMASK_VEA7          DISP_BIT_IB07
#define DISP_BITMASK_VEA6          DISP_BIT_IB06
#define DISP_BITMASK_VEA5          DISP_BIT_IB05
#define DISP_BITMASK_VEA4          DISP_BIT_IB04
#define DISP_BITMASK_VEA3          DISP_BIT_IB03
#define DISP_BITMASK_VEA2          DISP_BIT_IB02
#define DISP_BITMASK_VEA1          DISP_BIT_IB01
#define DISP_BITMASK_VEA0          DISP_BIT_IB00
static word disp_area_start_row;
static word disp_area_end_row;
static boolean disp_initialized = FALSE;
/* For some reason the contrast set at init time is not good. Need to do
* it again
*/
static boolean display_on = FALSE;

static uint32 tmd20qvga_lcd_rev;
uint16 tmd20qvga_panel_offset;

#ifdef DISP_DEVICE_8BPP
static word convert_8_to_16_tbl[256] = {
	0x0000, 0x2000, 0x4000, 0x6000, 0x8000, 0xA000, 0xC000, 0xE000,
	0x0100, 0x2100, 0x4100, 0x6100, 0x8100, 0xA100, 0xC100, 0xE100,
	0x0200, 0x2200, 0x4200, 0x6200, 0x8200, 0xA200, 0xC200, 0xE200,
	0x0300, 0x2300, 0x4300, 0x6300, 0x8300, 0xA300, 0xC300, 0xE300,
	0x0400, 0x2400, 0x4400, 0x6400, 0x8400, 0xA400, 0xC400, 0xE400,
	0x0500, 0x2500, 0x4500, 0x6500, 0x8500, 0xA500, 0xC500, 0xE500,
	0x0600, 0x2600, 0x4600, 0x6600, 0x8600, 0xA600, 0xC600, 0xE600,
	0x0700, 0x2700, 0x4700, 0x6700, 0x8700, 0xA700, 0xC700, 0xE700,
	0x0008, 0x2008, 0x4008, 0x6008, 0x8008, 0xA008, 0xC008, 0xE008,
	0x0108, 0x2108, 0x4108, 0x6108, 0x8108, 0xA108, 0xC108, 0xE108,
	0x0208, 0x2208, 0x4208, 0x6208, 0x8208, 0xA208, 0xC208, 0xE208,
	0x0308, 0x2308, 0x4308, 0x6308, 0x8308, 0xA308, 0xC308, 0xE308,
	0x0408, 0x2408, 0x4408, 0x6408, 0x8408, 0xA408, 0xC408, 0xE408,
	0x0508, 0x2508, 0x4508, 0x6508, 0x8508, 0xA508, 0xC508, 0xE508,
	0x0608, 0x2608, 0x4608, 0x6608, 0x8608, 0xA608, 0xC608, 0xE608,
	0x0708, 0x2708, 0x4708, 0x6708, 0x8708, 0xA708, 0xC708, 0xE708,
	0x0010, 0x2010, 0x4010, 0x6010, 0x8010, 0xA010, 0xC010, 0xE010,
	0x0110, 0x2110, 0x4110, 0x6110, 0x8110, 0xA110, 0xC110, 0xE110,
	0x0210, 0x2210, 0x4210, 0x6210, 0x8210, 0xA210, 0xC210, 0xE210,
	0x0310, 0x2310, 0x4310, 0x6310, 0x8310, 0xA310, 0xC310, 0xE310,
	0x0410, 0x2410, 0x4410, 0x6410, 0x8410, 0xA410, 0xC410, 0xE410,
	0x0510, 0x2510, 0x4510, 0x6510, 0x8510, 0xA510, 0xC510, 0xE510,
	0x0610, 0x2610, 0x4610, 0x6610, 0x8610, 0xA610, 0xC610, 0xE610,
	0x0710, 0x2710, 0x4710, 0x6710, 0x8710, 0xA710, 0xC710, 0xE710,
	0x0018, 0x2018, 0x4018, 0x6018, 0x8018, 0xA018, 0xC018, 0xE018,
	0x0118, 0x2118, 0x4118, 0x6118, 0x8118, 0xA118, 0xC118, 0xE118,
	0x0218, 0x2218, 0x4218, 0x6218, 0x8218, 0xA218, 0xC218, 0xE218,
	0x0318, 0x2318, 0x4318, 0x6318, 0x8318, 0xA318, 0xC318, 0xE318,
	0x0418, 0x2418, 0x4418, 0x6418, 0x8418, 0xA418, 0xC418, 0xE418,
	0x0518, 0x2518, 0x4518, 0x6518, 0x8518, 0xA518, 0xC518, 0xE518,
	0x0618, 0x2618, 0x4618, 0x6618, 0x8618, 0xA618, 0xC618, 0xE618,
	0x0718, 0x2718, 0x4718, 0x6718, 0x8718, 0xA718, 0xC718, 0xE718
};
#endif /* DISP_DEVICE_8BPP */

static void tmd20qvga_disp_set_rect(int x, int y, int xres, int yres);
static void tmd20qvga_disp_init(struct platform_device *pdev);
static void tmd20qvga_disp_set_contrast(void);
static void tmd20qvga_disp_set_display_area(word start_row, word end_row);
static int tmd20qvga_disp_off(struct platform_device *pdev);
static int tmd20qvga_disp_on(struct platform_device *pdev);
static void tmd20qvga_set_revId(int);

/* future use */
void tmd20qvga_disp_clear_screen_area(word start_row, word end_row,
				      word start_column, word end_column);

static void tmd20qvga_set_revId(int id)
{

	tmd20qvga_lcd_rev = id;

	if (tmd20qvga_lcd_rev == 1)
		tmd20qvga_panel_offset = 0x10;
	else
		tmd20qvga_panel_offset = 0;
}

static void tmd20qvga_disp_init(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	if (disp_initialized)
		return;

	mfd = platform_get_drvdata(pdev);

	DISP_CMD_PORT = mfd->cmd_port;
	DISP_DATA_PORT = mfd->data_port;

#ifdef TMD20QVGA_LCD_18BPP
	tmd20qvga_set_revId(2);
#else
	tmd20qvga_set_revId(1);
#endif

	disp_initialized = TRUE;
	tmd20qvga_disp_set_contrast();
	tmd20qvga_disp_set_display_area(0, QVGA_HEIGHT - 1);
}

static void tmd20qvga_disp_set_rect(int x, int y, int xres, int yres)
{
	if (!disp_initialized)
		return;

	DISP_SET_RECT(y, y + yres - 1, x, x + xres - 1);

	DISP_CMD_OUT(DISP_CMD_RAMWR);
}

static void tmd20qvga_disp_set_display_area(word start_row, word end_row)
{
	word start_driving = start_row;
	word end_driving = end_row;

	if (!disp_initialized)
		return;

	/* Range checking
	 */
	if (end_driving >= QVGA_HEIGHT)
		end_driving = QVGA_HEIGHT - 1;
	if (start_driving > end_driving) {
		/* Probably Backwards Switch */
		start_driving = end_driving;
		end_driving = start_row;	/* Has not changed */
		if (end_driving >= QVGA_HEIGHT)
			end_driving = QVGA_HEIGHT - 1;
	}

	if ((start_driving == disp_area_start_row)
	    && (end_driving == disp_area_end_row))
		return;

	disp_area_start_row = start_driving;
	disp_area_end_row = end_driving;

	DISP_WRITE_OUT(DISP_SCREEN_1_DRV_POS_1_ADDR,
		       DISP_VAL_IF(start_driving & 0x100,
				   DISP_BITMASK_SS18) |
		       DISP_VAL_IF(start_driving & 0x080,
				   DISP_BITMASK_SS17) |
		       DISP_VAL_IF(start_driving & 0x040,
				   DISP_BITMASK_SS16) |
		       DISP_VAL_IF(start_driving & 0x020,
				   DISP_BITMASK_SS15) |
		       DISP_VAL_IF(start_driving & 0x010,
				   DISP_BITMASK_SS14) |
		       DISP_VAL_IF(start_driving & 0x008,
				   DISP_BITMASK_SS13) |
		       DISP_VAL_IF(start_driving & 0x004,
				   DISP_BITMASK_SS12) |
		       DISP_VAL_IF(start_driving & 0x002,
				   DISP_BITMASK_SS11) |
		       DISP_VAL_IF(start_driving & 0x001, DISP_BITMASK_SS10));

	DISP_WRITE_OUT(DISP_SCREEN_1_DRV_POS_2_ADDR,
			DISP_VAL_IF(end_driving & 0x100, DISP_BITMASK_SE18) |
			DISP_VAL_IF(end_driving & 0x080, DISP_BITMASK_SE17) |
			DISP_VAL_IF(end_driving & 0x040, DISP_BITMASK_SE16) |
			DISP_VAL_IF(end_driving & 0x020, DISP_BITMASK_SE15) |
			DISP_VAL_IF(end_driving & 0x010, DISP_BITMASK_SE14) |
			DISP_VAL_IF(end_driving & 0x008, DISP_BITMASK_SE13) |
			DISP_VAL_IF(end_driving & 0x004, DISP_BITMASK_SE12) |
			DISP_VAL_IF(end_driving & 0x002, DISP_BITMASK_SE11) |
			DISP_VAL_IF(end_driving & 0x001, DISP_BITMASK_SE10));
}

static int tmd20qvga_disp_off(struct platform_device *pdev)
{
	if (!disp_initialized)
		tmd20qvga_disp_init(pdev);

	if (display_on) {
		if (tmd20qvga_lcd_rev == 2) {
			DISP_WRITE_OUT(DISP_POFF_LN_SETTING_ADDR, 0x000A);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0xFFEE);
			WAIT_MSEC(40);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0xF812);
			WAIT_MSEC(40);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0xE811);
			WAIT_MSEC(40);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0xC011);
			WAIT_MSEC(40);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x4011);
			WAIT_MSEC(20);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x0010);

		} else {
			DISP_WRITE_OUT(DISP_POFF_LN_SETTING_ADDR, 0x000F);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x0BFE);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0100);
			WAIT_MSEC(40);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x0BED);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0100);
			WAIT_MSEC(40);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x00CD);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0100);
			WAIT_MSEC(20);
			DISP_WRITE_OUT(DISP_START_OSCILLATION_ADDR, 0x0);
		}

		DISP_WRITE_OUT(DISP_MODE_SETTING_ADDR, 0x0004);
		DISP_WRITE_OUT(DISP_MODE_SETTING_ADDR, 0x0000);

		display_on = FALSE;
	}

	return 0;
}

static int tmd20qvga_disp_on(struct platform_device *pdev)
{
	if (!disp_initialized)
		tmd20qvga_disp_init(pdev);

	if (!display_on) {
		/* Deep Stand-by -> Stand-by */
		DISP_CMD_OUT(DISP_START_OSCILLATION_ADDR);
		WAIT_MSEC(1);
		DISP_CMD_OUT(DISP_START_OSCILLATION_ADDR);
		WAIT_MSEC(1);
		DISP_CMD_OUT(DISP_START_OSCILLATION_ADDR);
		WAIT_MSEC(1);

		/* OFF -> Deep Stan-By -> Stand-by */
		/* let's change the state from "Stand-by" to "Sleep" */
		DISP_WRITE_OUT(DISP_MODE_SETTING_ADDR, 0x0005);
		WAIT_MSEC(1);

		/* Sleep -> Displaying */
		DISP_WRITE_OUT(DISP_START_OSCILLATION_ADDR, 0x0001);
		DISP_WRITE_OUT(DISP_DRIVER_OUTPUT_CTL_ADDR, 0x0127);
		DISP_WRITE_OUT(DISP_LCD_DRIVING_SIG_ADDR, 0x200);
		/* fast write mode */
		DISP_WRITE_OUT(DISP_ENTRY_MODE_ADDR, 0x0130);
		if (tmd20qvga_lcd_rev == 2)
			DISP_WRITE_OUT(DISP_TMD_700_ADDR, 0x0003);
		/* back porch = 14 + front porch = 2 --> 16 lines */
		if (tmd20qvga_lcd_rev == 2) {
#ifdef TMD20QVGA_LCD_18BPP
			/* 256k color */
			DISP_WRITE_OUT(DISP_DISPLAY_CTL_1_ADDR, 0x0000);
#else
			/* 65k color */
			DISP_WRITE_OUT(DISP_DISPLAY_CTL_1_ADDR, 0x4000);
#endif
			DISP_WRITE_OUT(DISP_DISPLAY_CTL_2_ADDR, 0x0302);
		} else {
#ifdef TMD20QVGA_LCD_18BPP
			/* 256k color */
			DISP_WRITE_OUT(DISP_DISPLAY_CTL_1_ADDR, 0x0004);
#else
			/* 65k color */
			DISP_WRITE_OUT(DISP_DISPLAY_CTL_1_ADDR, 0x4004);
#endif
			DISP_WRITE_OUT(DISP_DISPLAY_CTL_2_ADDR, 0x020E);
		}
		/* 16 bit one transfer */
		if (tmd20qvga_lcd_rev == 2) {
			DISP_WRITE_OUT(DISP_EXT_DISPLAY_CTL_1_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_FRAME_CYCLE_CTL_ADDR, 0x0010);
			DISP_WRITE_OUT(DISP_LTPS_CTL_1_ADDR, 0x0302);
			DISP_WRITE_OUT(DISP_LTPS_CTL_2_ADDR, 0x0102);
			DISP_WRITE_OUT(DISP_LTPS_CTL_3_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_TMD_015_ADDR, 0x2000);

			DISP_WRITE_OUT(DISP_AMP_SETTING_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_1_ADDR, 0x0403);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_2_ADDR, 0x0304);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_3_ADDR, 0x0403);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_4_ADDR, 0x0303);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_5_ADDR, 0x0101);
			DISP_WRITE_OUT(DISP_TMD_305_ADDR, 0);

			DISP_WRITE_OUT(DISP_SCREEN_1_DRV_POS_1_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_SCREEN_1_DRV_POS_2_ADDR, 0x013F);

			DISP_WRITE_OUT(DISP_POWER_CTL_3_ADDR, 0x077D);

			DISP_WRITE_OUT(DISP_POWER_CTL_4_ADDR, 0x0005);
			DISP_WRITE_OUT(DISP_POWER_CTL_5_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_POWER_CTL_6_ADDR, 0x0015);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0xC010);
			WAIT_MSEC(1);

			DISP_WRITE_OUT(DISP_POWER_CTL_2_ADDR, 0x0001);
			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0xFFFE);
			WAIT_MSEC(60);
		} else {
			DISP_WRITE_OUT(DISP_EXT_DISPLAY_CTL_1_ADDR, 0x0001);
			DISP_WRITE_OUT(DISP_FRAME_CYCLE_CTL_ADDR, 0x0010);
			DISP_WRITE_OUT(DISP_LTPS_CTL_1_ADDR, 0x0301);
			DISP_WRITE_OUT(DISP_LTPS_CTL_2_ADDR, 0x0001);
			DISP_WRITE_OUT(DISP_LTPS_CTL_3_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_AMP_SETTING_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_1_ADDR, 0x0507);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_2_ADDR, 0x0405);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_3_ADDR, 0x0607);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_4_ADDR, 0x0502);
			DISP_WRITE_OUT(DISP_GAMMA_CONTROL_5_ADDR, 0x0301);
			DISP_WRITE_OUT(DISP_SCREEN_1_DRV_POS_1_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_SCREEN_1_DRV_POS_2_ADDR, 0x013F);
			DISP_WRITE_OUT(DISP_POWER_CTL_3_ADDR, 0x0795);

			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0102);
			WAIT_MSEC(1);

			DISP_WRITE_OUT(DISP_POWER_CTL_4_ADDR, 0x0450);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0103);
			WAIT_MSEC(1);

			DISP_WRITE_OUT(DISP_POWER_CTL_5_ADDR, 0x0008);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0104);
			WAIT_MSEC(1);

			DISP_WRITE_OUT(DISP_POWER_CTL_6_ADDR, 0x0C00);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0105);
			WAIT_MSEC(1);

			DISP_WRITE_OUT(DISP_POWER_CTL_7_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0106);
			WAIT_MSEC(1);

			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x0801);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0100);
			WAIT_MSEC(1);

			DISP_WRITE_OUT(DISP_POWER_CTL_2_ADDR, 0x001F);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0101);
			WAIT_MSEC(60);

			DISP_WRITE_OUT(DISP_POWER_CTL_2_ADDR, 0x009F);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0101);
			WAIT_MSEC(10);

			DISP_WRITE_OUT(DISP_HORZ_RAM_ADDR_POS_1_ADDR, 0x0010);
			DISP_WRITE_OUT(DISP_HORZ_RAM_ADDR_POS_2_ADDR, 0x00FF);
			DISP_WRITE_OUT(DISP_VERT_RAM_ADDR_POS_1_ADDR, 0x0000);
			DISP_WRITE_OUT(DISP_VERT_RAM_ADDR_POS_2_ADDR, 0x013F);
			/* RAM starts at address 0x10 */
			DISP_WRITE_OUT(DISP_RAM_ADDR_SET_1_ADDR, 0x0010);
			DISP_WRITE_OUT(DISP_RAM_ADDR_SET_2_ADDR, 0x0000);

			/* lcd controller uses internal clock, not ext. vsync */
			DISP_CMD_OUT(DISP_CMD_RAMWR);

			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x0881);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0100);
			WAIT_MSEC(40);

			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x0BE1);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0100);
			WAIT_MSEC(40);

			DISP_WRITE_OUT(DISP_POWER_CTL_1_ADDR, 0x0BFF);
			DISP_WRITE_OUT(DISP_POWER_SUPPLY_INTF_ADDR, 0x0100);
		}
		display_on = TRUE;
	}

	return 0;
}

static void tmd20qvga_disp_set_contrast(void)
{
#if (defined(TMD20QVGA_LCD_18BPP))

	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_1_ADDR, 0x0403);
	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_2_ADDR, 0x0302);
	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_3_ADDR, 0x0403);
	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_4_ADDR, 0x0303);
	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_5_ADDR, 0x0F07);

#else
	int newcontrast = 0x46;

	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_1_ADDR, 0x0403);

	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_2_ADDR,
			DISP_VAL_IF(newcontrast & 0x0001, DISP_BITMASK_PKP20) |
			DISP_VAL_IF(newcontrast & 0x0002, DISP_BITMASK_PKP21) |
			DISP_VAL_IF(newcontrast & 0x0004, DISP_BITMASK_PKP22) |
			DISP_VAL_IF(newcontrast & 0x0010, DISP_BITMASK_PKP30) |
			DISP_VAL_IF(newcontrast & 0x0020, DISP_BITMASK_PKP31) |
			DISP_VAL_IF(newcontrast & 0x0040, DISP_BITMASK_PKP32));

	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_3_ADDR,
			DISP_VAL_IF(newcontrast & 0x0010, DISP_BITMASK_PKP40) |
			DISP_VAL_IF(newcontrast & 0x0020, DISP_BITMASK_PKP41) |
			DISP_VAL_IF(newcontrast & 0x0040, DISP_BITMASK_PKP42) |
			DISP_VAL_IF(newcontrast & 0x0001, DISP_BITMASK_PKP50) |
			DISP_VAL_IF(newcontrast & 0x0002, DISP_BITMASK_PKP51) |
			DISP_VAL_IF(newcontrast & 0x0004, DISP_BITMASK_PKP52));

	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_4_ADDR, 0x0303);
	DISP_WRITE_OUT(DISP_GAMMA_CONTROL_5_ADDR, 0x0F07);

#endif /* defined(TMD20QVGA_LCD_18BPP) */

}	/* End disp_set_contrast */

void tmd20qvga_disp_clear_screen_area
    (word start_row, word end_row, word start_column, word end_column) {
	int32 i;

	/* Clear the display screen */
	DISP_SET_RECT(start_row, end_row, start_column, end_column);
	DISP_CMD_OUT(DISP_CMD_RAMWR);
	i = (end_row - start_row + 1) * (end_column - start_column + 1);
	for (; i > 0; i--)
		DISP_DATA_OUT_16TO18BPP(0x0);
}

static int __init tmd20qvga_probe(struct platform_device *pdev)
{
	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = tmd20qvga_probe,
	.driver = {
		.name   = "ebi2_tmd_qvga",
	},
};

static struct msm_fb_panel_data tmd20qvga_panel_data = {
	.on = tmd20qvga_disp_on,
	.off = tmd20qvga_disp_off,
	.set_rect = tmd20qvga_disp_set_rect,
};

static struct platform_device this_device = {
	.name   = "ebi2_tmd_qvga",
	.id	= 0,
	.dev	= {
		.platform_data = &tmd20qvga_panel_data,
	}
};

static int __init tmd20qvga_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;

	ret = platform_driver_register(&this_driver);
	if (!ret) {
		pinfo = &tmd20qvga_panel_data.panel_info;
		pinfo->xres = 240;
		pinfo->yres = 320;
		pinfo->type = EBI2_PANEL;
		pinfo->pdest = DISPLAY_1;
		pinfo->wait_cycle = 0x808000;
#ifdef TMD20QVGA_LCD_18BPP
		pinfo->bpp = 18;
#else
		pinfo->bpp = 16;
#endif
		pinfo->fb_num = 2;
		pinfo->lcd.vsync_enable = TRUE;
		pinfo->lcd.refx100 = 6000;
		pinfo->lcd.v_back_porch = 16;
		pinfo->lcd.v_front_porch = 4;
		pinfo->lcd.v_pulse_width = 0;
		pinfo->lcd.hw_vsync_mode = FALSE;
		pinfo->lcd.vsync_notifier_period = 0;

		ret = platform_device_register(&this_device);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return ret;
}

module_init(tmd20qvga_init);
