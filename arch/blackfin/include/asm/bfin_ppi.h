/*
 * bfin_ppi.h - interface to Blackfin PPIs
 *
 * Copyright 2005-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_PPI_H__
#define __ASM_BFIN_PPI_H__

#include <linux/types.h>
#include <asm/blackfin.h>

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m

/*
 * bfin ppi registers layout
 */
struct bfin_ppi_regs {
	__BFP(control);
	__BFP(status);
	__BFP(count);
	__BFP(delay);
	__BFP(frame);
};

/*
 * bfin eppi registers layout
 */
struct bfin_eppi_regs {
	__BFP(status);
	__BFP(hcount);
	__BFP(hdelay);
	__BFP(vcount);
	__BFP(vdelay);
	__BFP(frame);
	__BFP(line);
	__BFP(clkdiv);
	u32 control;
	u32 fs1w_hbl;
	u32 fs1p_avpl;
	u32 fs2w_lvb;
	u32 fs2p_lavf;
	u32 clip;
};

/*
 * bfin eppi3 registers layout
 */
struct bfin_eppi3_regs {
	u32 stat;
	u32 hcnt;
	u32 hdly;
	u32 vcnt;
	u32 vdly;
	u32 frame;
	u32 line;
	u32 clkdiv;
	u32 ctl;
	u32 fs1_wlhb;
	u32 fs1_paspl;
	u32 fs2_wlvb;
	u32 fs2_palpf;
	u32 imsk;
	u32 oddclip;
	u32 evenclip;
	u32 fs1_dly;
	u32 fs2_dly;
	u32 ctl2;
};

#undef __BFP

#ifdef EPPI0_CTL2
#define EPPI_STAT_CFIFOERR              0x00000001    /* Chroma FIFO Error */
#define EPPI_STAT_YFIFOERR              0x00000002    /* Luma FIFO Error */
#define EPPI_STAT_LTERROVR              0x00000004    /* Line Track Overflow */
#define EPPI_STAT_LTERRUNDR             0x00000008    /* Line Track Underflow */
#define EPPI_STAT_FTERROVR              0x00000010    /* Frame Track Overflow */
#define EPPI_STAT_FTERRUNDR             0x00000020    /* Frame Track Underflow */
#define EPPI_STAT_ERRNCOR               0x00000040    /* Preamble Error Not Corrected */
#define EPPI_STAT_PXPERR                0x00000080    /* PxP Ready Error */
#define EPPI_STAT_ERRDET                0x00004000    /* Preamble Error Detected */
#define EPPI_STAT_FLD                   0x00008000    /* Current Field Received by EPPI */

#define EPPI_HCNT_VALUE                 0x0000FFFF    /* Holds the number of samples to read in or write out per line, after PPIx_HDLY number of cycles have expired since the last assertion of PPIx_FS1 */

#define EPPI_HDLY_VALUE                 0x0000FFFF    /* Number of PPIx_CLK cycles to delay after assertion of PPIx_FS1 before starting to read or write data */

#define EPPI_VCNT_VALUE                 0x0000FFFF    /* Holds the number of lines to read in or write out, after PPIx_VDLY number of lines from the start of frame */

#define EPPI_VDLY_VALUE                 0x0000FFFF    /* Number of lines to wait after the start of a new frame before starting to read/transmit data */

#define EPPI_FRAME_VALUE                0x0000FFFF    /* Holds the number of lines expected per frame of data */

#define EPPI_LINE_VALUE                 0x0000FFFF    /* Holds the number of samples expected per line */

#define EPPI_CLKDIV_VALUE               0x0000FFFF    /* Internal clock divider */

#define EPPI_CTL_EN                     0x00000001    /* PPI Enable */
#define EPPI_CTL_DIR                    0x00000002    /* PPI Direction */
#define EPPI_CTL_XFRTYPE                0x0000000C    /* PPI Operating Mode */
#define EPPI_CTL_ACTIVE656              0x00000000    /* XFRTYPE: ITU656 Active Video Only Mode */
#define EPPI_CTL_ENTIRE656              0x00000004    /* XFRTYPE: ITU656 Entire Field Mode */
#define EPPI_CTL_VERT656                0x00000008    /* XFRTYPE: ITU656 Vertical Blanking Only Mode */
#define EPPI_CTL_NON656                 0x0000000C    /* XFRTYPE: Non-ITU656 Mode (GP Mode) */
#define EPPI_CTL_FSCFG                  0x00000030    /* Frame Sync Configuration */
#define EPPI_CTL_SYNC0                  0x00000000    /* FSCFG: Sync Mode 0 */
#define EPPI_CTL_SYNC1                  0x00000010    /* FSCFG: Sync Mode 1 */
#define EPPI_CTL_SYNC2                  0x00000020    /* FSCFG: Sync Mode 2 */
#define EPPI_CTL_SYNC3                  0x00000030    /* FSCFG: Sync Mode 3 */
#define EPPI_CTL_FLDSEL                 0x00000040    /* Field Select/Trigger */
#define EPPI_CTL_ITUTYPE                0x00000080    /* ITU Interlace or Progressive */
#define EPPI_CTL_BLANKGEN               0x00000100    /* ITU Output Mode with Internal Blanking Generation */
#define EPPI_CTL_ICLKGEN                0x00000200    /* Internal Clock Generation */
#define EPPI_CTL_IFSGEN                 0x00000400    /* Internal Frame Sync Generation */
#define EPPI_CTL_SIGNEXT                0x00000800    /* Sign Extension */
#define EPPI_CTL_POLC                   0x00003000    /* Frame Sync and Data Driving and Sampling Edges */
#define EPPI_CTL_POLC0                  0x00000000    /* POLC: Clock/Sync polarity mode 0 */
#define EPPI_CTL_POLC1                  0x00001000    /* POLC: Clock/Sync polarity mode 1 */
#define EPPI_CTL_POLC2                  0x00002000    /* POLC: Clock/Sync polarity mode 2 */
#define EPPI_CTL_POLC3                  0x00003000    /* POLC: Clock/Sync polarity mode 3 */
#define EPPI_CTL_POLS                   0x0000C000    /* Frame Sync Polarity */
#define EPPI_CTL_FS1HI_FS2HI            0x00000000    /* POLS: FS1 and FS2 are active high */
#define EPPI_CTL_FS1LO_FS2HI            0x00004000    /* POLS: FS1 is active low. FS2 is active high */
#define EPPI_CTL_FS1HI_FS2LO            0x00008000    /* POLS: FS1 is active high. FS2 is active low */
#define EPPI_CTL_FS1LO_FS2LO            0x0000C000    /* POLS: FS1 and FS2 are active low */
#define EPPI_CTL_DLEN                   0x00070000    /* Data Length */
#define EPPI_CTL_DLEN08                 0x00000000    /* DLEN: 8 bits */
#define EPPI_CTL_DLEN10                 0x00010000    /* DLEN: 10 bits */
#define EPPI_CTL_DLEN12                 0x00020000    /* DLEN: 12 bits */
#define EPPI_CTL_DLEN14                 0x00030000    /* DLEN: 14 bits */
#define EPPI_CTL_DLEN16                 0x00040000    /* DLEN: 16 bits */
#define EPPI_CTL_DLEN18                 0x00050000    /* DLEN: 18 bits */
#define EPPI_CTL_DLEN20                 0x00060000    /* DLEN: 20 bits */
#define EPPI_CTL_DLEN24                 0x00070000    /* DLEN: 24 bits */
#define EPPI_CTL_DMIRR                  0x00080000    /* Data Mirroring */
#define EPPI_CTL_SKIPEN                 0x00100000    /* Skip Enable */
#define EPPI_CTL_SKIPEO                 0x00200000    /* Skip Even or Odd */
#define EPPI_CTL_PACKEN                 0x00400000    /* Pack/Unpack Enable */
#define EPPI_CTL_SWAPEN                 0x00800000    /* Swap Enable */
#define EPPI_CTL_SPLTEO                 0x01000000    /* Split Even and Odd Data Samples */
#define EPPI_CTL_SUBSPLTODD             0x02000000    /* Sub-Split Odd Samples */
#define EPPI_CTL_SPLTWRD                0x04000000    /* Split Word */
#define EPPI_CTL_RGBFMTEN               0x08000000    /* RGB Formatting Enable */
#define EPPI_CTL_DMACFG                 0x10000000    /* One or Two DMA Channels Mode */
#define EPPI_CTL_DMAFINEN               0x20000000    /* DMA Finish Enable */
#define EPPI_CTL_MUXSEL                 0x40000000    /* MUX Select */
#define EPPI_CTL_CLKGATEN               0x80000000    /* Clock Gating Enable */

#define EPPI_FS2_WLVB_F2VBAD            0xFF000000    /* In GP transmit mode with BLANKGEN = 1, contains number of lines of vertical blanking after field 2 */
#define EPPI_FS2_WLVB_F2VBBD            0x00FF0000    /* In GP transmit mode with BLANKGEN = 1, contains number of lines of vertical blanking before field 2 */
#define EPPI_FS2_WLVB_F1VBAD            0x0000FF00    /* In GP transmit mode with, BLANKGEN = 1, contains number of lines of vertical blanking after field 1 */
#define EPPI_FS2_WLVB_F1VBBD            0x000000FF    /* In GP 2, or 3 FS modes used to generate PPIx_FS2 width (32-bit). In GP Transmit mode, with BLANKGEN=1, contains the number of lines of Vertical blanking before field 1. */

#define EPPI_FS2_PALPF_F2ACT            0xFFFF0000    /* Number of lines of Active Data in Field 2 */
#define EPPI_FS2_PALPF_F1ACT            0x0000FFFF    /* Number of lines of Active Data in Field 1 */

#define EPPI_IMSK_CFIFOERR              0x00000001    /* Mask CFIFO Underflow or Overflow Error Interrupt */
#define EPPI_IMSK_YFIFOERR              0x00000002    /* Mask YFIFO Underflow or Overflow Error Interrupt */
#define EPPI_IMSK_LTERROVR              0x00000004    /* Mask Line Track Overflow Error Interrupt */
#define EPPI_IMSK_LTERRUNDR             0x00000008    /* Mask Line Track Underflow Error Interrupt */
#define EPPI_IMSK_FTERROVR              0x00000010    /* Mask Frame Track Overflow Error Interrupt */
#define EPPI_IMSK_FTERRUNDR             0x00000020    /* Mask Frame Track Underflow Error Interrupt */
#define EPPI_IMSK_ERRNCOR               0x00000040    /* Mask ITU Preamble Error Not Corrected Interrupt */
#define EPPI_IMSK_PXPERR                0x00000080    /* Mask PxP Ready Error Interrupt */

#define EPPI_ODDCLIP_HIGHODD            0xFFFF0000
#define EPPI_ODDCLIP_LOWODD             0x0000FFFF

#define EPPI_EVENCLIP_HIGHEVEN          0xFFFF0000
#define EPPI_EVENCLIP_LOWEVEN           0x0000FFFF

#define EPPI_CTL2_FS1FINEN              0x00000002    /* HSYNC Finish Enable */
#endif
#endif
