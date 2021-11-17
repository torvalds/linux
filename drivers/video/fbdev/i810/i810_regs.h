/*-*- linux-c -*-
 *  linux/drivers/video/i810_regs.h -- Intel 810/815 Register List
 *
 *      Copyright (C) 2001 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved      
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */


/*
 * Intel 810 Chipset Family PRM 15 3.1 
 * GC Register Memory Address Map 
 *
 * Based on:
 * Intel (R) 810 Chipset Family 
 * Programmer s Reference Manual 
 * November 1999 
 * Revision 1.0 
 * Order Number: 298026-001 R
 *
 * All GC registers are memory-mapped. In addition, the VGA and extended VGA registers 
 * are I/O mapped. 
 */
 
#ifndef __I810_REGS_H__
#define __I810_REGS_H__

/*  Instruction and Interrupt Control Registers (01000h 02FFFh) */
#define FENCE                 0x02000                
#define PGTBL_CTL             0x02020 
#define PGTBL_ER              0x02024               
#define    LRING              0x02030
#define    IRING              0x02040
#define HWS_PGA               0x02080 
#define IPEIR                 0x02088
#define IPEHR                 0x0208C 
#define INSTDONE              0x02090 
#define NOPID                 0x02094
#define HWSTAM                0x02098 
#define IER                   0x020A0
#define IIR                   0x020A4
#define IMR                   0x020A8 
#define ISR                   0x020AC 
#define EIR                   0x020B0 
#define EMR                   0x020B4 
#define ESR                   0x020B8 
#define INSTPM                0x020C0
#define INSTPS                0x020C4 
#define BBP_PTR               0x020C8 
#define ABB_SRT               0x020CC
#define ABB_END               0x020D0
#define DMA_FADD              0x020D4 
#define FW_BLC                0x020D8
#define MEM_MODE              0x020DC        

/*  Memory Control Registers (03000h 03FFFh) */
#define DRT                   0x03000
#define DRAMCL                0x03001
#define DRAMCH                0x03002
 

/* Span Cursor Registers (04000h 04FFFh) */
#define UI_SC_CTL             0x04008 

/* I/O Control Registers (05000h 05FFFh) */
#define HVSYNC                0x05000 
#define GPIOA                 0x05010
#define GPIOB                 0x05014 
#define GPIOC                 0x0501C

/* Clock Control and Power Management Registers (06000h 06FFFh) */
#define DCLK_0D               0x06000
#define DCLK_1D               0x06004
#define DCLK_2D               0x06008
#define LCD_CLKD              0x0600C
#define DCLK_0DS              0x06010
#define PWR_CLKC              0x06014

/* Graphics Translation Table Range Definition (10000h 1FFFFh) */
#define GTT                   0x10000  

/*  Overlay Registers (30000h 03FFFFh) */
#define OVOADDR               0x30000
#define DOVOSTA               0x30008
#define GAMMA                 0x30010
#define OBUF_0Y               0x30100
#define OBUF_1Y               0x30104
#define OBUF_0U               0x30108
#define OBUF_0V               0x3010C
#define OBUF_1U               0x30110
#define OBUF_1V               0x30114 
#define OVOSTRIDE             0x30118
#define YRGB_VPH              0x3011C
#define UV_VPH                0x30120
#define HORZ_PH               0x30124
#define INIT_PH               0x30128
#define DWINPOS               0x3012C 
#define DWINSZ                0x30130
#define SWID                  0x30134
#define SWIDQW                0x30138
#define SHEIGHT               0x3013F
#define YRGBSCALE             0x30140 
#define UVSCALE               0x30144
#define OVOCLRCO              0x30148
#define OVOCLRC1              0x3014C
#define DCLRKV                0x30150
#define DLCRKM                0x30154
#define SCLRKVH               0x30158
#define SCLRKVL               0x3015C
#define SCLRKM                0x30160
#define OVOCONF               0x30164
#define OVOCMD                0x30168
#define AWINPOS               0x30170
#define AWINZ                 0x30174

/*  BLT Engine Status (40000h 4FFFFh) (Software Debug) */
#define BR00                  0x40000
#define BRO1                  0x40004
#define BR02                  0x40008
#define BR03                  0x4000C
#define BR04                  0x40010
#define BR05                  0x40014
#define BR06                  0x40018
#define BR07                  0x4001C
#define BR08                  0x40020
#define BR09                  0x40024
#define BR10                  0x40028
#define BR11                  0x4002C
#define BR12                  0x40030
#define BR13                  0x40034
#define BR14                  0x40038
#define BR15                  0x4003C
#define BR16                  0x40040
#define BR17                  0x40044
#define BR18                  0x40048
#define BR19                  0x4004C
#define SSLADD                0x40074
#define DSLH                  0x40078
#define DSLRADD               0x4007C


/* LCD/TV-Out and HW DVD Registers (60000h 6FFFFh) */
/* LCD/TV-Out */
#define HTOTAL                0x60000
#define HBLANK                0x60004
#define HSYNC                 0x60008
#define VTOTAL                0x6000C
#define VBLANK                0x60010
#define VSYNC                 0x60014
#define LCDTV_C               0x60018
#define OVRACT                0x6001C
#define BCLRPAT               0x60020

/*  Display and Cursor Control Registers (70000h 7FFFFh) */
#define DISP_SL               0x70000
#define DISP_SLC              0x70004
#define PIXCONF               0x70008
#define PIXCONF1              0x70009
#define BLTCNTL               0x7000C
#define SWF                   0x70014
#define DPLYBASE              0x70020
#define DPLYSTAS              0x70024
#define CURCNTR               0x70080
#define CURBASE               0x70084
#define CURPOS                0x70088


/* VGA Registers */

/* SMRAM Registers */
#define SMRAM                 0x10

/* Graphics Control Registers */
#define GR_INDEX              0x3CE
#define GR_DATA               0x3CF

#define GR10                  0x10
#define GR11                  0x11

/* CRT Controller Registers */
#define CR_INDEX_MDA          0x3B4
#define CR_INDEX_CGA          0x3D4
#define CR_DATA_MDA           0x3B5
#define CR_DATA_CGA           0x3D5

#define CR30                  0x30
#define CR31                  0x31
#define CR32                  0x32
#define CR33                  0x33
#define CR35                  0x35
#define CR39                  0x39
#define CR40                  0x40
#define CR41                  0x41
#define CR42                  0x42
#define CR70                  0x70
#define CR80                  0x80 
#define CR81                  0x82

/* Extended VGA Registers */

/* General Control and Status Registers */
#define ST00                  0x3C2
#define ST01_MDA              0x3BA
#define ST01_CGA              0x3DA
#define FRC_READ              0x3CA
#define FRC_WRITE_MDA         0x3BA
#define FRC_WRITE_CGA         0x3DA
#define MSR_READ              0x3CC
#define MSR_WRITE             0x3C2

/* Sequencer Registers */
#define SR_INDEX              0x3C4
#define SR_DATA               0x3C5

#define SR01                  0x01
#define SR02                  0x02
#define SR03                  0x03
#define SR04                  0x04
#define SR07                  0x07

/* Graphics Controller Registers */
#define GR00                  0x00   
#define GR01                  0x01
#define GR02                  0x02
#define GR03                  0x03
#define GR04                  0x04
#define GR05                  0x05
#define GR06                  0x06
#define GR07                  0x07
#define GR08                  0x08  

/* Attribute Controller Registers */
#define ATTR_WRITE              0x3C0
#define ATTR_READ               0x3C1

/* VGA Color Palette Registers */

/* CLUT */
#define CLUT_DATA             0x3C9        /* DACDATA */
#define CLUT_INDEX_READ       0x3C7        /* DACRX */
#define CLUT_INDEX_WRITE      0x3C8        /* DACWX */
#define DACMASK               0x3C6

/* CRT Controller Registers */
#define CR00                  0x00
#define CR01                  0x01
#define CR02                  0x02
#define CR03                  0x03
#define CR04                  0x04
#define CR05                  0x05
#define CR06                  0x06
#define CR07                  0x07
#define CR08                  0x08
#define CR09                  0x09
#define CR0A                  0x0A
#define CR0B                  0x0B
#define CR0C                  0x0C
#define CR0D                  0x0D
#define CR0E                  0x0E
#define CR0F                  0x0F
#define CR10                  0x10
#define CR11                  0x11
#define CR12                  0x12
#define CR13                  0x13
#define CR14                  0x14
#define CR15                  0x15
#define CR16                  0x16
#define CR17                  0x17
#define CR18                  0x18

#endif /* __I810_REGS_H__ */
