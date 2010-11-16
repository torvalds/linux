/* $XConsortium: nvreg.h /main/2 1996/10/28 05:13:41 kaleb $ */
/*
 * Copyright 1996-1997  David J. McKay
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
 * DAVID J. MCKAY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nvreg.h,v 1.6 2002/01/25 21:56:06 tsi Exp $ */

#ifndef __NVREG_H_
#define __NVREG_H_

#define NV_PMC_OFFSET               0x00000000
#define NV_PMC_SIZE                 0x00001000

#define NV_PBUS_OFFSET              0x00001000
#define NV_PBUS_SIZE                0x00001000

#define NV_PFIFO_OFFSET             0x00002000
#define NV_PFIFO_SIZE               0x00002000

#define NV_HDIAG_OFFSET             0x00005000
#define NV_HDIAG_SIZE               0x00001000

#define NV_PRAM_OFFSET              0x00006000
#define NV_PRAM_SIZE                0x00001000

#define NV_PVIDEO_OFFSET            0x00008000
#define NV_PVIDEO_SIZE              0x00001000

#define NV_PTIMER_OFFSET            0x00009000
#define NV_PTIMER_SIZE              0x00001000

#define NV_PPM_OFFSET               0x0000A000
#define NV_PPM_SIZE                 0x00001000

#define NV_PTV_OFFSET               0x0000D000
#define NV_PTV_SIZE                 0x00001000

#define NV_PRMVGA_OFFSET            0x000A0000
#define NV_PRMVGA_SIZE              0x00020000

#define NV_PRMVIO0_OFFSET           0x000C0000
#define NV_PRMVIO_SIZE              0x00002000
#define NV_PRMVIO1_OFFSET           0x000C2000

#define NV_PFB_OFFSET               0x00100000
#define NV_PFB_SIZE                 0x00001000

#define NV_PEXTDEV_OFFSET           0x00101000
#define NV_PEXTDEV_SIZE             0x00001000

#define NV_PME_OFFSET               0x00200000
#define NV_PME_SIZE                 0x00001000

#define NV_PROM_OFFSET              0x00300000
#define NV_PROM_SIZE                0x00010000

#define NV_PGRAPH_OFFSET            0x00400000
#define NV_PGRAPH_SIZE              0x00010000

#define NV_PCRTC0_OFFSET            0x00600000
#define NV_PCRTC0_SIZE              0x00002000 /* empirical */

#define NV_PRMCIO0_OFFSET           0x00601000
#define NV_PRMCIO_SIZE              0x00002000
#define NV_PRMCIO1_OFFSET           0x00603000

#define NV50_DISPLAY_OFFSET           0x00610000
#define NV50_DISPLAY_SIZE             0x0000FFFF

#define NV_PRAMDAC0_OFFSET          0x00680000
#define NV_PRAMDAC0_SIZE            0x00002000

#define NV_PRMDIO0_OFFSET           0x00681000
#define NV_PRMDIO_SIZE              0x00002000
#define NV_PRMDIO1_OFFSET           0x00683000

#define NV_PRAMIN_OFFSET            0x00700000
#define NV_PRAMIN_SIZE              0x00100000

#define NV_FIFO_OFFSET              0x00800000
#define NV_FIFO_SIZE                0x00800000

#define NV_PMC_BOOT_0			0x00000000
#define NV_PMC_ENABLE			0x00000200

#define NV_VIO_VSE2			0x000003c3
#define NV_VIO_SRX			0x000003c4

#define NV_CIO_CRX__COLOR		0x000003d4
#define NV_CIO_CR__COLOR		0x000003d5

#define NV_PBUS_DEBUG_1			0x00001084
#define NV_PBUS_DEBUG_4			0x00001098
#define NV_PBUS_DEBUG_DUALHEAD_CTL	0x000010f0
#define NV_PBUS_POWERCTRL_1		0x00001584
#define NV_PBUS_POWERCTRL_2		0x00001588
#define NV_PBUS_POWERCTRL_4		0x00001590
#define NV_PBUS_PCI_NV_19		0x0000184C
#define NV_PBUS_PCI_NV_20		0x00001850
#	define NV_PBUS_PCI_NV_20_ROM_SHADOW_DISABLED	(0 << 0)
#	define NV_PBUS_PCI_NV_20_ROM_SHADOW_ENABLED	(1 << 0)

#define NV_PFIFO_RAMHT			0x00002210

#define NV_PTV_TV_INDEX			0x0000d220
#define NV_PTV_TV_DATA			0x0000d224
#define NV_PTV_HFILTER			0x0000d310
#define NV_PTV_HFILTER2			0x0000d390
#define NV_PTV_VFILTER			0x0000d510

#define NV_PRMVIO_MISC__WRITE		0x000c03c2
#define NV_PRMVIO_SRX			0x000c03c4
#define NV_PRMVIO_SR			0x000c03c5
#	define NV_VIO_SR_RESET_INDEX		0x00
#	define NV_VIO_SR_CLOCK_INDEX		0x01
#	define NV_VIO_SR_PLANE_MASK_INDEX	0x02
#	define NV_VIO_SR_CHAR_MAP_INDEX		0x03
#	define NV_VIO_SR_MEM_MODE_INDEX		0x04
#define NV_PRMVIO_MISC__READ		0x000c03cc
#define NV_PRMVIO_GRX			0x000c03ce
#define NV_PRMVIO_GX			0x000c03cf
#	define NV_VIO_GX_SR_INDEX		0x00
#	define NV_VIO_GX_SREN_INDEX		0x01
#	define NV_VIO_GX_CCOMP_INDEX		0x02
#	define NV_VIO_GX_ROP_INDEX		0x03
#	define NV_VIO_GX_READ_MAP_INDEX		0x04
#	define NV_VIO_GX_MODE_INDEX		0x05
#	define NV_VIO_GX_MISC_INDEX		0x06
#	define NV_VIO_GX_DONT_CARE_INDEX	0x07
#	define NV_VIO_GX_BIT_MASK_INDEX		0x08

#define NV_PCRTC_INTR_0					0x00600100
#	define NV_PCRTC_INTR_0_VBLANK				(1 << 0)
#define NV_PCRTC_INTR_EN_0				0x00600140
#define NV_PCRTC_START					0x00600800
#define NV_PCRTC_CONFIG					0x00600804
#	define NV_PCRTC_CONFIG_START_ADDRESS_NON_VGA		(1 << 0)
#	define NV_PCRTC_CONFIG_START_ADDRESS_HSYNC		(2 << 0)
#define NV_PCRTC_CURSOR_CONFIG				0x00600810
#	define NV_PCRTC_CURSOR_CONFIG_ENABLE_ENABLE		(1 << 0)
#	define NV_PCRTC_CURSOR_CONFIG_DOUBLE_SCAN_ENABLE	(1 << 4)
#	define NV_PCRTC_CURSOR_CONFIG_ADDRESS_SPACE_PNVM	(1 << 8)
#	define NV_PCRTC_CURSOR_CONFIG_CUR_BPP_32		(1 << 12)
#	define NV_PCRTC_CURSOR_CONFIG_CUR_PIXELS_64		(1 << 16)
#	define NV_PCRTC_CURSOR_CONFIG_CUR_LINES_32		(2 << 24)
#	define NV_PCRTC_CURSOR_CONFIG_CUR_LINES_64		(4 << 24)
#	define NV_PCRTC_CURSOR_CONFIG_CUR_BLEND_ALPHA		(1 << 28)

/* note: PCRTC_GPIO is not available on nv10, and in fact aliases 0x600810 */
#define NV_PCRTC_GPIO					0x00600818
#define NV_PCRTC_GPIO_EXT				0x0060081c
#define NV_PCRTC_830					0x00600830
#define NV_PCRTC_834					0x00600834
#define NV_PCRTC_850					0x00600850
#define NV_PCRTC_ENGINE_CTRL				0x00600860
#	define NV_CRTC_FSEL_I2C					(1 << 4)
#	define NV_CRTC_FSEL_OVERLAY				(1 << 12)

#define NV_PRMCIO_ARX			0x006013c0
#define NV_PRMCIO_AR__WRITE		0x006013c0
#define NV_PRMCIO_AR__READ		0x006013c1
#	define NV_CIO_AR_MODE_INDEX		0x10
#	define NV_CIO_AR_OSCAN_INDEX		0x11
#	define NV_CIO_AR_PLANE_INDEX		0x12
#	define NV_CIO_AR_HPP_INDEX		0x13
#	define NV_CIO_AR_CSEL_INDEX		0x14
#define NV_PRMCIO_INP0			0x006013c2
#define NV_PRMCIO_CRX__COLOR		0x006013d4
#define NV_PRMCIO_CR__COLOR		0x006013d5
	/* Standard VGA CRTC registers */
#	define NV_CIO_CR_HDT_INDEX		0x00	/* horizontal display total */
#	define NV_CIO_CR_HDE_INDEX		0x01	/* horizontal display end */
#	define NV_CIO_CR_HBS_INDEX		0x02	/* horizontal blanking start */
#	define NV_CIO_CR_HBE_INDEX		0x03	/* horizontal blanking end */
#		define NV_CIO_CR_HBE_4_0		4:0
#	define NV_CIO_CR_HRS_INDEX		0x04	/* horizontal retrace start */
#	define NV_CIO_CR_HRE_INDEX		0x05	/* horizontal retrace end */
#		define NV_CIO_CR_HRE_4_0		4:0
#		define NV_CIO_CR_HRE_HBE_5		7:7
#	define NV_CIO_CR_VDT_INDEX		0x06	/* vertical display total */
#	define NV_CIO_CR_OVL_INDEX		0x07	/* overflow bits */
#		define NV_CIO_CR_OVL_VDT_8		0:0
#		define NV_CIO_CR_OVL_VDE_8		1:1
#		define NV_CIO_CR_OVL_VRS_8		2:2
#		define NV_CIO_CR_OVL_VBS_8		3:3
#		define NV_CIO_CR_OVL_VDT_9		5:5
#		define NV_CIO_CR_OVL_VDE_9		6:6
#		define NV_CIO_CR_OVL_VRS_9		7:7
#	define NV_CIO_CR_RSAL_INDEX		0x08	/* normally "preset row scan" */
#	define NV_CIO_CR_CELL_HT_INDEX		0x09	/* cell height?! normally "max scan line" */
#		define NV_CIO_CR_CELL_HT_VBS_9		5:5
#		define NV_CIO_CR_CELL_HT_SCANDBL	7:7
#	define NV_CIO_CR_CURS_ST_INDEX		0x0a	/* cursor start */
#	define NV_CIO_CR_CURS_END_INDEX		0x0b	/* cursor end */
#	define NV_CIO_CR_SA_HI_INDEX		0x0c	/* screen start address high */
#	define NV_CIO_CR_SA_LO_INDEX		0x0d	/* screen start address low */
#	define NV_CIO_CR_TCOFF_HI_INDEX		0x0e	/* cursor offset high */
#	define NV_CIO_CR_TCOFF_LO_INDEX		0x0f	/* cursor offset low */
#	define NV_CIO_CR_VRS_INDEX		0x10	/* vertical retrace start */
#	define NV_CIO_CR_VRE_INDEX		0x11	/* vertical retrace end */
#		define NV_CIO_CR_VRE_3_0		3:0
#	define NV_CIO_CR_VDE_INDEX		0x12	/* vertical display end */
#	define NV_CIO_CR_OFFSET_INDEX		0x13	/* sets screen pitch */
#	define NV_CIO_CR_ULINE_INDEX		0x14	/* underline location */
#	define NV_CIO_CR_VBS_INDEX		0x15	/* vertical blank start */
#	define NV_CIO_CR_VBE_INDEX		0x16	/* vertical blank end */
#	define NV_CIO_CR_MODE_INDEX		0x17	/* crtc mode control */
#	define NV_CIO_CR_LCOMP_INDEX		0x18	/* line compare */
	/* Extended VGA CRTC registers */
#	define NV_CIO_CRE_RPC0_INDEX		0x19	/* repaint control 0 */
#		define NV_CIO_CRE_RPC0_OFFSET_10_8	7:5
#	define NV_CIO_CRE_RPC1_INDEX		0x1a	/* repaint control 1 */
#		define NV_CIO_CRE_RPC1_LARGE		2:2
#	define NV_CIO_CRE_FF_INDEX		0x1b	/* fifo control */
#	define NV_CIO_CRE_ENH_INDEX		0x1c	/* enhanced? */
#	define NV_CIO_SR_LOCK_INDEX		0x1f	/* crtc lock */
#		define NV_CIO_SR_UNLOCK_RW_VALUE	0x57
#		define NV_CIO_SR_LOCK_VALUE		0x99
#	define NV_CIO_CRE_FFLWM__INDEX		0x20	/* fifo low water mark */
#	define NV_CIO_CRE_21			0x21	/* vga shadow crtc lock */
#	define NV_CIO_CRE_LSR_INDEX		0x25	/* ? */
#		define NV_CIO_CRE_LSR_VDT_10		0:0
#		define NV_CIO_CRE_LSR_VDE_10		1:1
#		define NV_CIO_CRE_LSR_VRS_10		2:2
#		define NV_CIO_CRE_LSR_VBS_10		3:3
#		define NV_CIO_CRE_LSR_HBE_6		4:4
#	define NV_CIO_CR_ARX_INDEX		0x26	/* attribute index -- ro copy of 0x60.3c0 */
#	define NV_CIO_CRE_CHIP_ID_INDEX		0x27	/* chip revision */
#	define NV_CIO_CRE_PIXEL_INDEX		0x28
#		define NV_CIO_CRE_PIXEL_FORMAT		1:0
#	define NV_CIO_CRE_HEB__INDEX		0x2d	/* horizontal extra bits? */
#		define NV_CIO_CRE_HEB_HDT_8		0:0
#		define NV_CIO_CRE_HEB_HDE_8		1:1
#		define NV_CIO_CRE_HEB_HBS_8		2:2
#		define NV_CIO_CRE_HEB_HRS_8		3:3
#		define NV_CIO_CRE_HEB_ILC_8		4:4
#	define NV_CIO_CRE_2E			0x2e	/* some scratch or dummy reg to force writes to sink in */
#	define NV_CIO_CRE_HCUR_ADDR2_INDEX	0x2f	/* cursor */
#	define NV_CIO_CRE_HCUR_ADDR0_INDEX	0x30		/* pixmap */
#		define NV_CIO_CRE_HCUR_ADDR0_ADR	6:0
#		define NV_CIO_CRE_HCUR_ASI		7:7
#	define NV_CIO_CRE_HCUR_ADDR1_INDEX	0x31			/* address */
#		define NV_CIO_CRE_HCUR_ADDR1_ENABLE	0:0
#		define NV_CIO_CRE_HCUR_ADDR1_CUR_DBL	1:1
#		define NV_CIO_CRE_HCUR_ADDR1_ADR	7:2
#	define NV_CIO_CRE_LCD__INDEX		0x33
#		define NV_CIO_CRE_LCD_LCD_SELECT	0:0
#		define NV_CIO_CRE_LCD_ROUTE_MASK	0x3b
#	define NV_CIO_CRE_DDC0_STATUS__INDEX	0x36
#	define NV_CIO_CRE_DDC0_WR__INDEX	0x37
#	define NV_CIO_CRE_ILACE__INDEX		0x39	/* interlace */
#	define NV_CIO_CRE_SCRATCH3__INDEX	0x3b
#	define NV_CIO_CRE_SCRATCH4__INDEX	0x3c
#	define NV_CIO_CRE_DDC_STATUS__INDEX	0x3e
#	define NV_CIO_CRE_DDC_WR__INDEX		0x3f
#	define NV_CIO_CRE_EBR_INDEX		0x41	/* extra bits ? (vertical) */
#		define NV_CIO_CRE_EBR_VDT_11		0:0
#		define NV_CIO_CRE_EBR_VDE_11		2:2
#		define NV_CIO_CRE_EBR_VRS_11		4:4
#		define NV_CIO_CRE_EBR_VBS_11		6:6
#	define NV_CIO_CRE_43			0x43
#	define NV_CIO_CRE_44			0x44	/* head control */
#	define NV_CIO_CRE_CSB			0x45	/* colour saturation boost */
#	define NV_CIO_CRE_RCR			0x46
#		define NV_CIO_CRE_RCR_ENDIAN_BIG	7:7
#	define NV_CIO_CRE_47			0x47	/* extended fifo lwm, used on nv30+ */
#	define NV_CIO_CRE_49			0x49
#	define NV_CIO_CRE_4B			0x4b	/* given patterns in 0x[2-3][a-c] regs, probably scratch 6 */
#	define NV_CIO_CRE_TVOUT_LATENCY		0x52
#	define NV_CIO_CRE_53			0x53	/* `fp_htiming' according to Haiku */
#	define NV_CIO_CRE_54			0x54	/* `fp_vtiming' according to Haiku */
#	define NV_CIO_CRE_57			0x57	/* index reg for cr58 */
#	define NV_CIO_CRE_58			0x58	/* data reg for cr57 */
#	define NV_CIO_CRE_59			0x59	/* related to on/off-chip-ness of digital outputs */
#	define NV_CIO_CRE_5B			0x5B	/* newer colour saturation reg */
#	define NV_CIO_CRE_85			0x85
#	define NV_CIO_CRE_86			0x86
#define NV_PRMCIO_INP0__COLOR		0x006013da

#define NV_PRAMDAC_CU_START_POS				0x00680300
#	define NV_PRAMDAC_CU_START_POS_X			15:0
#	define NV_PRAMDAC_CU_START_POS_Y			31:16
#define NV_RAMDAC_NV10_CURSYNC				0x00680404

#define NV_PRAMDAC_NVPLL_COEFF				0x00680500
#define NV_PRAMDAC_MPLL_COEFF				0x00680504
#define NV_PRAMDAC_VPLL_COEFF				0x00680508
#	define NV30_RAMDAC_ENABLE_VCO2				(8 << 4)

#define NV_PRAMDAC_PLL_COEFF_SELECT			0x0068050c
#	define NV_PRAMDAC_PLL_COEFF_SELECT_USE_VPLL2_TRUE	(4 << 0)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_MPLL	(1 << 8)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_VPLL	(2 << 8)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_NVPLL	(4 << 8)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_PLL_SOURCE_VPLL2	(8 << 8)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_TV_VSCLK1		(1 << 16)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_TV_PCLK1		(2 << 16)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_TV_VSCLK2		(4 << 16)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_TV_PCLK2		(8 << 16)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_TV_CLK_SOURCE_VIP	(1 << 20)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_VCLK_RATIO_DB2	(1 << 28)
#	define NV_PRAMDAC_PLL_COEFF_SELECT_VCLK2_RATIO_DB2	(2 << 28)

#define NV_PRAMDAC_PLL_SETUP_CONTROL			0x00680510
#define NV_RAMDAC_VPLL2					0x00680520
#define NV_PRAMDAC_SEL_CLK				0x00680524
#define NV_RAMDAC_DITHER_NV11				0x00680528
#define NV_PRAMDAC_DACCLK				0x0068052c
#	define NV_PRAMDAC_DACCLK_SEL_DACCLK			(1 << 0)

#define NV_RAMDAC_NVPLL_B				0x00680570
#define NV_RAMDAC_MPLL_B				0x00680574
#define NV_RAMDAC_VPLL_B				0x00680578
#define NV_RAMDAC_VPLL2_B				0x0068057c
#	define NV31_RAMDAC_ENABLE_VCO2				(8 << 28)
#define NV_PRAMDAC_580					0x00680580
#	define NV_RAMDAC_580_VPLL1_ACTIVE			(1 << 8)
#	define NV_RAMDAC_580_VPLL2_ACTIVE			(1 << 28)

#define NV_PRAMDAC_GENERAL_CONTROL			0x00680600
#	define NV_PRAMDAC_GENERAL_CONTROL_PIXMIX_ON		(3 << 4)
#	define NV_PRAMDAC_GENERAL_CONTROL_VGA_STATE_SEL		(1 << 8)
#	define NV_PRAMDAC_GENERAL_CONTROL_ALT_MODE_SEL		(1 << 12)
#	define NV_PRAMDAC_GENERAL_CONTROL_TERMINATION_75OHM	(2 << 16)
#	define NV_PRAMDAC_GENERAL_CONTROL_BPC_8BITS		(1 << 20)
#	define NV_PRAMDAC_GENERAL_CONTROL_PIPE_LONG		(2 << 28)
#define NV_PRAMDAC_TEST_CONTROL				0x00680608
#	define NV_PRAMDAC_TEST_CONTROL_TP_INS_EN_ASSERTED	(1 << 12)
#	define NV_PRAMDAC_TEST_CONTROL_PWRDWN_DAC_OFF		(1 << 16)
#	define NV_PRAMDAC_TEST_CONTROL_SENSEB_ALLHI		(1 << 28)
#define NV_PRAMDAC_TESTPOINT_DATA			0x00680610
#	define NV_PRAMDAC_TESTPOINT_DATA_NOTBLANK		(8 << 28)
#define NV_PRAMDAC_630					0x00680630
#define NV_PRAMDAC_634					0x00680634

#define NV_PRAMDAC_TV_SETUP				0x00680700
#define NV_PRAMDAC_TV_VTOTAL				0x00680720
#define NV_PRAMDAC_TV_VSKEW				0x00680724
#define NV_PRAMDAC_TV_VSYNC_DELAY			0x00680728
#define NV_PRAMDAC_TV_HTOTAL				0x0068072c
#define NV_PRAMDAC_TV_HSKEW				0x00680730
#define NV_PRAMDAC_TV_HSYNC_DELAY			0x00680734
#define NV_PRAMDAC_TV_HSYNC_DELAY2			0x00680738

#define NV_PRAMDAC_TV_SETUP                             0x00680700

#define NV_PRAMDAC_FP_VDISPLAY_END			0x00680800
#define NV_PRAMDAC_FP_VTOTAL				0x00680804
#define NV_PRAMDAC_FP_VCRTC				0x00680808
#define NV_PRAMDAC_FP_VSYNC_START			0x0068080c
#define NV_PRAMDAC_FP_VSYNC_END				0x00680810
#define NV_PRAMDAC_FP_VVALID_START			0x00680814
#define NV_PRAMDAC_FP_VVALID_END			0x00680818
#define NV_PRAMDAC_FP_HDISPLAY_END			0x00680820
#define NV_PRAMDAC_FP_HTOTAL				0x00680824
#define NV_PRAMDAC_FP_HCRTC				0x00680828
#define NV_PRAMDAC_FP_HSYNC_START			0x0068082c
#define NV_PRAMDAC_FP_HSYNC_END				0x00680830
#define NV_PRAMDAC_FP_HVALID_START			0x00680834
#define NV_PRAMDAC_FP_HVALID_END			0x00680838

#define NV_RAMDAC_FP_DITHER				0x0068083c
#define NV_PRAMDAC_FP_TG_CONTROL			0x00680848
#	define NV_PRAMDAC_FP_TG_CONTROL_VSYNC_POS		(1 << 0)
#	define NV_PRAMDAC_FP_TG_CONTROL_VSYNC_DISABLE		(2 << 0)
#	define NV_PRAMDAC_FP_TG_CONTROL_HSYNC_POS		(1 << 4)
#	define NV_PRAMDAC_FP_TG_CONTROL_HSYNC_DISABLE		(2 << 4)
#	define NV_PRAMDAC_FP_TG_CONTROL_MODE_SCALE		(0 << 8)
#	define NV_PRAMDAC_FP_TG_CONTROL_MODE_CENTER		(1 << 8)
#	define NV_PRAMDAC_FP_TG_CONTROL_MODE_NATIVE		(2 << 8)
#	define NV_PRAMDAC_FP_TG_CONTROL_READ_PROG		(1 << 20)
#	define NV_PRAMDAC_FP_TG_CONTROL_WIDTH_12		(1 << 24)
#	define NV_PRAMDAC_FP_TG_CONTROL_DISPEN_POS		(1 << 28)
#	define NV_PRAMDAC_FP_TG_CONTROL_DISPEN_DISABLE		(2 << 28)
#define NV_PRAMDAC_FP_MARGIN_COLOR			0x0068084c
#define NV_PRAMDAC_850					0x00680850
#define NV_PRAMDAC_85C					0x0068085c
#define NV_PRAMDAC_FP_DEBUG_0				0x00680880
#	define NV_PRAMDAC_FP_DEBUG_0_XSCALE_ENABLE		(1 << 0)
#	define NV_PRAMDAC_FP_DEBUG_0_YSCALE_ENABLE		(1 << 4)
/* This doesn't seem to be essential for tmds, but still often set */
#	define NV_RAMDAC_FP_DEBUG_0_TMDS_ENABLED		(8 << 4)
#	define NV_PRAMDAC_FP_DEBUG_0_XINTERP_BILINEAR		(1 << 8)
#	define NV_PRAMDAC_FP_DEBUG_0_YINTERP_BILINEAR		(1 << 12)
#	define NV_PRAMDAC_FP_DEBUG_0_XWEIGHT_ROUND		(1 << 20)
#	define NV_PRAMDAC_FP_DEBUG_0_YWEIGHT_ROUND		(1 << 24)
#       define NV_PRAMDAC_FP_DEBUG_0_PWRDOWN_FPCLK              (1 << 28)
#define NV_PRAMDAC_FP_DEBUG_1				0x00680884
#	define NV_PRAMDAC_FP_DEBUG_1_XSCALE_VALUE		11:0
#	define NV_PRAMDAC_FP_DEBUG_1_XSCALE_TESTMODE_ENABLE	(1 << 12)
#	define NV_PRAMDAC_FP_DEBUG_1_YSCALE_VALUE		27:16
#	define NV_PRAMDAC_FP_DEBUG_1_YSCALE_TESTMODE_ENABLE	(1 << 28)
#define NV_PRAMDAC_FP_DEBUG_2				0x00680888
#define NV_PRAMDAC_FP_DEBUG_3				0x0068088C

/* see NV_PRAMDAC_INDIR_TMDS in rules.xml */
#define NV_PRAMDAC_FP_TMDS_CONTROL			0x006808b0
#	define NV_PRAMDAC_FP_TMDS_CONTROL_WRITE_DISABLE		(1 << 16)
#define NV_PRAMDAC_FP_TMDS_DATA				0x006808b4

#define NV_PRAMDAC_8C0                                  0x006808c0

/* Some kind of switch */
#define NV_PRAMDAC_900					0x00680900
#define NV_PRAMDAC_A20					0x00680A20
#define NV_PRAMDAC_A24					0x00680A24
#define NV_PRAMDAC_A34					0x00680A34

#define NV_PRAMDAC_CTV					0x00680c00

/* names fabricated from NV_USER_DAC info */
#define NV_PRMDIO_PIXEL_MASK		0x006813c6
#	define NV_PRMDIO_PIXEL_MASK_MASK	0xff
#define NV_PRMDIO_READ_MODE_ADDRESS	0x006813c7
#define NV_PRMDIO_WRITE_MODE_ADDRESS	0x006813c8
#define NV_PRMDIO_PALETTE_DATA		0x006813c9

#define NV_PGRAPH_DEBUG_0		0x00400080
#define NV_PGRAPH_DEBUG_1		0x00400084
#define NV_PGRAPH_DEBUG_2_NV04		0x00400088
#define NV_PGRAPH_DEBUG_2		0x00400620
#define NV_PGRAPH_DEBUG_3		0x0040008c
#define NV_PGRAPH_DEBUG_4		0x00400090
#define NV_PGRAPH_INTR			0x00400100
#define NV_PGRAPH_INTR_EN		0x00400140
#define NV_PGRAPH_CTX_CONTROL		0x00400144
#define NV_PGRAPH_CTX_CONTROL_NV04	0x00400170
#define NV_PGRAPH_ABS_UCLIP_XMIN	0x0040053C
#define NV_PGRAPH_ABS_UCLIP_YMIN	0x00400540
#define NV_PGRAPH_ABS_UCLIP_XMAX	0x00400544
#define NV_PGRAPH_ABS_UCLIP_YMAX	0x00400548
#define NV_PGRAPH_BETA_AND		0x00400608
#define NV_PGRAPH_LIMIT_VIOL_PIX	0x00400610
#define NV_PGRAPH_BOFFSET0		0x00400640
#define NV_PGRAPH_BOFFSET1		0x00400644
#define NV_PGRAPH_BOFFSET2		0x00400648
#define NV_PGRAPH_BLIMIT0		0x00400684
#define NV_PGRAPH_BLIMIT1		0x00400688
#define NV_PGRAPH_BLIMIT2		0x0040068c
#define NV_PGRAPH_STATUS		0x00400700
#define NV_PGRAPH_SURFACE		0x00400710
#define NV_PGRAPH_STATE			0x00400714
#define NV_PGRAPH_FIFO			0x00400720
#define NV_PGRAPH_PATTERN_SHAPE		0x00400810
#define NV_PGRAPH_TILE			0x00400b00

#define NV_PVIDEO_INTR_EN		0x00008140
#define NV_PVIDEO_BUFFER		0x00008700
#define NV_PVIDEO_STOP			0x00008704
#define NV_PVIDEO_UVPLANE_BASE(buff)	(0x00008800+(buff)*4)
#define NV_PVIDEO_UVPLANE_LIMIT(buff)	(0x00008808+(buff)*4)
#define NV_PVIDEO_UVPLANE_OFFSET_BUFF(buff)	(0x00008820+(buff)*4)
#define NV_PVIDEO_BASE(buff)		(0x00008900+(buff)*4)
#define NV_PVIDEO_LIMIT(buff)		(0x00008908+(buff)*4)
#define NV_PVIDEO_LUMINANCE(buff)	(0x00008910+(buff)*4)
#define NV_PVIDEO_CHROMINANCE(buff)	(0x00008918+(buff)*4)
#define NV_PVIDEO_OFFSET_BUFF(buff)	(0x00008920+(buff)*4)
#define NV_PVIDEO_SIZE_IN(buff)		(0x00008928+(buff)*4)
#define NV_PVIDEO_POINT_IN(buff)	(0x00008930+(buff)*4)
#define NV_PVIDEO_DS_DX(buff)		(0x00008938+(buff)*4)
#define NV_PVIDEO_DT_DY(buff)		(0x00008940+(buff)*4)
#define NV_PVIDEO_POINT_OUT(buff)	(0x00008948+(buff)*4)
#define NV_PVIDEO_SIZE_OUT(buff)	(0x00008950+(buff)*4)
#define NV_PVIDEO_FORMAT(buff)		(0x00008958+(buff)*4)
#	define NV_PVIDEO_FORMAT_PLANAR			(1 << 0)
#	define NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8	(1 << 16)
#	define NV_PVIDEO_FORMAT_DISPLAY_COLOR_KEY	(1 << 20)
#	define NV_PVIDEO_FORMAT_MATRIX_ITURBT709	(1 << 24)
#define NV_PVIDEO_COLOR_KEY		0x00008B00

/* NV04 overlay defines from VIDIX & Haiku */
#define NV_PVIDEO_INTR_EN_0		0x00680140
#define NV_PVIDEO_STEP_SIZE		0x00680200
#define NV_PVIDEO_CONTROL_Y		0x00680204
#define NV_PVIDEO_CONTROL_X		0x00680208
#define NV_PVIDEO_BUFF0_START_ADDRESS	0x0068020c
#define NV_PVIDEO_BUFF0_PITCH_LENGTH	0x00680214
#define NV_PVIDEO_BUFF0_OFFSET		0x0068021c
#define NV_PVIDEO_BUFF1_START_ADDRESS	0x00680210
#define NV_PVIDEO_BUFF1_PITCH_LENGTH	0x00680218
#define NV_PVIDEO_BUFF1_OFFSET		0x00680220
#define NV_PVIDEO_OE_STATE		0x00680224
#define NV_PVIDEO_SU_STATE		0x00680228
#define NV_PVIDEO_RM_STATE		0x0068022c
#define NV_PVIDEO_WINDOW_START		0x00680230
#define NV_PVIDEO_WINDOW_SIZE		0x00680234
#define NV_PVIDEO_FIFO_THRES_SIZE	0x00680238
#define NV_PVIDEO_FIFO_BURST_LENGTH	0x0068023c
#define NV_PVIDEO_KEY			0x00680240
#define NV_PVIDEO_OVERLAY		0x00680244
#define NV_PVIDEO_RED_CSC_OFFSET	0x00680280
#define NV_PVIDEO_GREEN_CSC_OFFSET	0x00680284
#define NV_PVIDEO_BLUE_CSC_OFFSET	0x00680288
#define NV_PVIDEO_CSC_ADJUST		0x0068028c

#endif
