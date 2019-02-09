/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Ajay Kumar <ajaykumar.rs@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef EXYNOS_REGS_DECON7_H
#define EXYNOS_REGS_DECON7_H

/* VIDCON0 */
#define VIDCON0					0x00

#define VIDCON0_SWRESET				(1 << 28)
#define VIDCON0_DECON_STOP_STATUS		(1 << 2)
#define VIDCON0_ENVID				(1 << 1)
#define VIDCON0_ENVID_F				(1 << 0)

/* VIDOUTCON0 */
#define VIDOUTCON0				0x4

#define VIDOUTCON0_DUAL_MASK			(0x3 << 24)
#define VIDOUTCON0_DUAL_ON			(0x3 << 24)
#define VIDOUTCON0_DISP_IF_1_ON			(0x2 << 24)
#define VIDOUTCON0_DISP_IF_0_ON			(0x1 << 24)
#define VIDOUTCON0_DUAL_OFF			(0x0 << 24)
#define VIDOUTCON0_IF_SHIFT			23
#define VIDOUTCON0_IF_MASK			(0x1 << 23)
#define VIDOUTCON0_RGBIF			(0x0 << 23)
#define VIDOUTCON0_I80IF			(0x1 << 23)

/* VIDCON3 */
#define VIDCON3					0x8

/* VIDCON4 */
#define VIDCON4					0xC
#define VIDCON4_FIFOCNT_START_EN		(1 << 0)

/* VCLKCON0 */
#define VCLKCON0				0x10
#define VCLKCON0_CLKVALUP			(1 << 8)
#define VCLKCON0_VCLKFREE			(1 << 0)

/* VCLKCON */
#define VCLKCON1				0x14
#define VCLKCON1_CLKVAL_NUM_VCLK(val)		(((val) & 0xff) << 0)
#define VCLKCON2				0x18

/* SHADOWCON */
#define SHADOWCON				0x30

#define SHADOWCON_WINx_PROTECT(_win)		(1 << (10 + (_win)))

/* WINCONx */
#define WINCON(_win)				(0x50 + ((_win) * 4))

#define WINCONx_BUFSTATUS			(0x3 << 30)
#define WINCONx_BUFSEL_MASK			(0x3 << 28)
#define WINCONx_BUFSEL_SHIFT			28
#define WINCONx_TRIPLE_BUF_MODE			(0x1 << 18)
#define WINCONx_DOUBLE_BUF_MODE			(0x0 << 18)
#define WINCONx_BURSTLEN_16WORD			(0x0 << 11)
#define WINCONx_BURSTLEN_8WORD			(0x1 << 11)
#define WINCONx_BURSTLEN_MASK			(0x1 << 11)
#define WINCONx_BURSTLEN_SHIFT			11
#define WINCONx_BLD_PLANE			(0 << 8)
#define WINCONx_BLD_PIX				(1 << 8)
#define WINCONx_ALPHA_MUL			(1 << 7)

#define WINCONx_BPPMODE_MASK			(0xf << 2)
#define WINCONx_BPPMODE_SHIFT			2
#define WINCONx_BPPMODE_16BPP_565		(0x8 << 2)
#define WINCONx_BPPMODE_24BPP_BGRx		(0x7 << 2)
#define WINCONx_BPPMODE_24BPP_RGBx		(0x6 << 2)
#define WINCONx_BPPMODE_24BPP_xBGR		(0x5 << 2)
#define WINCONx_BPPMODE_24BPP_xRGB		(0x4 << 2)
#define WINCONx_BPPMODE_32BPP_BGRA		(0x3 << 2)
#define WINCONx_BPPMODE_32BPP_RGBA		(0x2 << 2)
#define WINCONx_BPPMODE_32BPP_ABGR		(0x1 << 2)
#define WINCONx_BPPMODE_32BPP_ARGB		(0x0 << 2)
#define WINCONx_ALPHA_SEL			(1 << 1)
#define WINCONx_ENWIN				(1 << 0)

#define WINCON1_ALPHA_MUL_F			(1 << 7)
#define WINCON2_ALPHA_MUL_F			(1 << 7)
#define WINCON3_ALPHA_MUL_F			(1 << 7)
#define WINCON4_ALPHA_MUL_F			(1 << 7)

/*  VIDOSDxH: The height for the OSD image(READ ONLY)*/
#define VIDOSD_H(_x)				(0x80 + ((_x) * 4))

/* Frame buffer start addresses: VIDWxxADD0n */
#define VIDW_BUF_START(_win)			(0x80 + ((_win) * 0x10))
#define VIDW_BUF_START1(_win)			(0x84 + ((_win) * 0x10))
#define VIDW_BUF_START2(_win)			(0x88 + ((_win) * 0x10))

#define VIDW_WHOLE_X(_win)			(0x0130 + ((_win) * 8))
#define VIDW_WHOLE_Y(_win)			(0x0134 + ((_win) * 8))
#define VIDW_OFFSET_X(_win)			(0x0170 + ((_win) * 8))
#define VIDW_OFFSET_Y(_win)			(0x0174 + ((_win) * 8))
#define VIDW_BLKOFFSET(_win)			(0x01B0 + ((_win) * 4))
#define VIDW_BLKSIZE(win)			(0x0200 + ((_win) * 4))

/* Interrupt controls register */
#define VIDINTCON2				0x228

#define VIDINTCON1_INTEXTRA1_EN			(1 << 1)
#define VIDINTCON1_INTEXTRA0_EN			(1 << 0)

/* Interrupt controls and status register */
#define VIDINTCON3				0x22C

#define VIDINTCON1_INTEXTRA1_PEND		(1 << 1)
#define VIDINTCON1_INTEXTRA0_PEND		(1 << 0)

/* VIDOSDxA ~ VIDOSDxE */
#define VIDOSD_BASE				0x230

#define OSD_STRIDE				0x20

#define VIDOSD_A(_win)				(VIDOSD_BASE + \
						((_win) * OSD_STRIDE) + 0x00)
#define VIDOSD_B(_win)				(VIDOSD_BASE + \
						((_win) * OSD_STRIDE) + 0x04)
#define VIDOSD_C(_win)				(VIDOSD_BASE + \
						((_win) * OSD_STRIDE) + 0x08)
#define VIDOSD_D(_win)				(VIDOSD_BASE + \
						((_win) * OSD_STRIDE) + 0x0C)
#define VIDOSD_E(_win)				(VIDOSD_BASE + \
						((_win) * OSD_STRIDE) + 0x10)

#define VIDOSDxA_TOPLEFT_X_MASK			(0x1fff << 13)
#define VIDOSDxA_TOPLEFT_X_SHIFT		13
#define VIDOSDxA_TOPLEFT_X_LIMIT		0x1fff
#define VIDOSDxA_TOPLEFT_X(_x)			(((_x) & 0x1fff) << 13)

#define VIDOSDxA_TOPLEFT_Y_MASK			(0x1fff << 0)
#define VIDOSDxA_TOPLEFT_Y_SHIFT		0
#define VIDOSDxA_TOPLEFT_Y_LIMIT		0x1fff
#define VIDOSDxA_TOPLEFT_Y(_x)			(((_x) & 0x1fff) << 0)

#define VIDOSDxB_BOTRIGHT_X_MASK		(0x1fff << 13)
#define VIDOSDxB_BOTRIGHT_X_SHIFT		13
#define VIDOSDxB_BOTRIGHT_X_LIMIT		0x1fff
#define VIDOSDxB_BOTRIGHT_X(_x)			(((_x) & 0x1fff) << 13)

#define VIDOSDxB_BOTRIGHT_Y_MASK		(0x1fff << 0)
#define VIDOSDxB_BOTRIGHT_Y_SHIFT		0
#define VIDOSDxB_BOTRIGHT_Y_LIMIT		0x1fff
#define VIDOSDxB_BOTRIGHT_Y(_x)			(((_x) & 0x1fff) << 0)

#define VIDOSDxC_ALPHA0_R_F(_x)			(((_x) & 0xFF) << 16)
#define VIDOSDxC_ALPHA0_G_F(_x)			(((_x) & 0xFF) << 8)
#define VIDOSDxC_ALPHA0_B_F(_x)			(((_x) & 0xFF) << 0)

#define VIDOSDxD_ALPHA1_R_F(_x)			(((_x) & 0xFF) << 16)
#define VIDOSDxD_ALPHA1_G_F(_x)			(((_x) & 0xFF) << 8)
#define VIDOSDxD_ALPHA1_B_F(_x)			(((_x) & 0xFF) >> 0)

/* Window MAP (Color map) */
#define WINxMAP(_win)				(0x340 + ((_win) * 4))

#define WINxMAP_MAP				(1 << 24)
#define WINxMAP_MAP_COLOUR_MASK			(0xffffff << 0)
#define WINxMAP_MAP_COLOUR_SHIFT		0
#define WINxMAP_MAP_COLOUR_LIMIT		0xffffff
#define WINxMAP_MAP_COLOUR(_x)			((_x) << 0)

/* Window colour-key control registers */
#define WKEYCON					0x370

#define WKEYCON0				0x00
#define WKEYCON1				0x04
#define WxKEYCON0_KEYBL_EN			(1 << 26)
#define WxKEYCON0_KEYEN_F			(1 << 25)
#define WxKEYCON0_DIRCON			(1 << 24)
#define WxKEYCON0_COMPKEY_MASK			(0xffffff << 0)
#define WxKEYCON0_COMPKEY_SHIFT			0
#define WxKEYCON0_COMPKEY_LIMIT			0xffffff
#define WxKEYCON0_COMPKEY(_x)			((_x) << 0)
#define WxKEYCON1_COLVAL_MASK			(0xffffff << 0)
#define WxKEYCON1_COLVAL_SHIFT			0
#define WxKEYCON1_COLVAL_LIMIT			0xffffff
#define WxKEYCON1_COLVAL(_x)			((_x) << 0)

/* color key control register for hardware window 1 ~ 4. */
#define WKEYCON0_BASE(x)		((WKEYCON + WKEYCON0) + ((x - 1) * 8))
/* color key value register for hardware window 1 ~ 4. */
#define WKEYCON1_BASE(x)		((WKEYCON + WKEYCON1) + ((x - 1) * 8))

/* Window KEY Alpha value */
#define WxKEYALPHA(_win)			(0x3A0 + (((_win) - 1) * 0x4))

#define Wx_KEYALPHA_R_F_SHIFT			16
#define Wx_KEYALPHA_G_F_SHIFT			8
#define Wx_KEYALPHA_B_F_SHIFT			0

/* Blending equation */
#define BLENDE(_win)				(0x03C0 + ((_win) * 4))
#define BLENDE_COEF_ZERO			0x0
#define BLENDE_COEF_ONE				0x1
#define BLENDE_COEF_ALPHA_A			0x2
#define BLENDE_COEF_ONE_MINUS_ALPHA_A		0x3
#define BLENDE_COEF_ALPHA_B			0x4
#define BLENDE_COEF_ONE_MINUS_ALPHA_B		0x5
#define BLENDE_COEF_ALPHA0			0x6
#define BLENDE_COEF_A				0xA
#define BLENDE_COEF_ONE_MINUS_A			0xB
#define BLENDE_COEF_B				0xC
#define BLENDE_COEF_ONE_MINUS_B			0xD
#define BLENDE_Q_FUNC(_v)			((_v) << 18)
#define BLENDE_P_FUNC(_v)			((_v) << 12)
#define BLENDE_B_FUNC(_v)			((_v) << 6)
#define BLENDE_A_FUNC(_v)			((_v) << 0)

/* Blending equation control */
#define BLENDCON				0x3D8
#define BLENDCON_NEW_MASK			(1 << 0)
#define BLENDCON_NEW_8BIT_ALPHA_VALUE		(1 << 0)
#define BLENDCON_NEW_4BIT_ALPHA_VALUE		(0 << 0)

/* Interrupt control register */
#define VIDINTCON0				0x500

#define VIDINTCON0_WAKEUP_MASK			(0x3f << 26)
#define VIDINTCON0_INTEXTRAEN			(1 << 21)

#define VIDINTCON0_FRAMESEL0_SHIFT		15
#define VIDINTCON0_FRAMESEL0_MASK		(0x3 << 15)
#define VIDINTCON0_FRAMESEL0_BACKPORCH		(0x0 << 15)
#define VIDINTCON0_FRAMESEL0_VSYNC		(0x1 << 15)
#define VIDINTCON0_FRAMESEL0_ACTIVE		(0x2 << 15)
#define VIDINTCON0_FRAMESEL0_FRONTPORCH		(0x3 << 15)

#define VIDINTCON0_INT_FRAME			(1 << 11)

#define VIDINTCON0_FIFOLEVEL_MASK		(0x7 << 3)
#define VIDINTCON0_FIFOLEVEL_SHIFT		3
#define VIDINTCON0_FIFOLEVEL_EMPTY		(0x0 << 3)
#define VIDINTCON0_FIFOLEVEL_TO25PC		(0x1 << 3)
#define VIDINTCON0_FIFOLEVEL_TO50PC		(0x2 << 3)
#define VIDINTCON0_FIFOLEVEL_FULL		(0x4 << 3)

#define VIDINTCON0_FIFOSEL_MAIN_EN		(1 << 1)
#define VIDINTCON0_INT_FIFO			(1 << 1)

#define VIDINTCON0_INT_ENABLE			(1 << 0)

/* Interrupt controls and status register */
#define VIDINTCON1				0x504

#define VIDINTCON1_INT_EXTRA			(1 << 3)
#define VIDINTCON1_INT_I80			(1 << 2)
#define VIDINTCON1_INT_FRAME			(1 << 1)
#define VIDINTCON1_INT_FIFO			(1 << 0)

/* VIDCON1 */
#define VIDCON1(_x)				(0x0600 + ((_x) * 0x50))
#define VIDCON1_LINECNT_GET(_v)			(((_v) >> 17) & 0x1fff)
#define VIDCON1_VCLK_MASK			(0x3 << 9)
#define VIDCON1_VCLK_HOLD			(0x0 << 9)
#define VIDCON1_VCLK_RUN			(0x1 << 9)
#define VIDCON1_VCLK_RUN_VDEN_DISABLE		(0x3 << 9)
#define VIDCON1_RGB_ORDER_O_MASK		(0x7 << 4)
#define VIDCON1_RGB_ORDER_O_RGB			(0x0 << 4)
#define VIDCON1_RGB_ORDER_O_GBR			(0x1 << 4)
#define VIDCON1_RGB_ORDER_O_BRG			(0x2 << 4)
#define VIDCON1_RGB_ORDER_O_BGR			(0x4 << 4)
#define VIDCON1_RGB_ORDER_O_RBG			(0x5 << 4)
#define VIDCON1_RGB_ORDER_O_GRB			(0x6 << 4)

/* VIDTCON0 */
#define VIDTCON0				0x610

#define VIDTCON0_VBPD_MASK			(0xffff << 16)
#define VIDTCON0_VBPD_SHIFT			16
#define VIDTCON0_VBPD_LIMIT			0xffff
#define VIDTCON0_VBPD(_x)			((_x) << 16)

#define VIDTCON0_VFPD_MASK			(0xffff << 0)
#define VIDTCON0_VFPD_SHIFT			0
#define VIDTCON0_VFPD_LIMIT			0xffff
#define VIDTCON0_VFPD(_x)			((_x) << 0)

/* VIDTCON1 */
#define VIDTCON1				0x614

#define VIDTCON1_VSPW_MASK			(0xffff << 16)
#define VIDTCON1_VSPW_SHIFT			16
#define VIDTCON1_VSPW_LIMIT			0xffff
#define VIDTCON1_VSPW(_x)			((_x) << 16)

/* VIDTCON2 */
#define VIDTCON2				0x618

#define VIDTCON2_HBPD_MASK			(0xffff << 16)
#define VIDTCON2_HBPD_SHIFT			16
#define VIDTCON2_HBPD_LIMIT			0xffff
#define VIDTCON2_HBPD(_x)			((_x) << 16)

#define VIDTCON2_HFPD_MASK			(0xffff << 0)
#define VIDTCON2_HFPD_SHIFT			0
#define VIDTCON2_HFPD_LIMIT			0xffff
#define VIDTCON2_HFPD(_x)			((_x) << 0)

/* VIDTCON3 */
#define VIDTCON3				0x61C

#define VIDTCON3_HSPW_MASK			(0xffff << 16)
#define VIDTCON3_HSPW_SHIFT			16
#define VIDTCON3_HSPW_LIMIT			0xffff
#define VIDTCON3_HSPW(_x)			((_x) << 16)

/* VIDTCON4 */
#define VIDTCON4				0x620

#define VIDTCON4_LINEVAL_MASK			(0xfff << 16)
#define VIDTCON4_LINEVAL_SHIFT			16
#define VIDTCON4_LINEVAL_LIMIT			0xfff
#define VIDTCON4_LINEVAL(_x)			(((_x) & 0xfff) << 16)

#define VIDTCON4_HOZVAL_MASK			(0xfff << 0)
#define VIDTCON4_HOZVAL_SHIFT			0
#define VIDTCON4_HOZVAL_LIMIT			0xfff
#define VIDTCON4_HOZVAL(_x)			(((_x) & 0xfff) << 0)

/* LINECNT OP THRSHOLD*/
#define LINECNT_OP_THRESHOLD			0x630

/* CRCCTRL */
#define CRCCTRL					0x6C8
#define CRCCTRL_CRCCLKEN			(0x1 << 2)
#define CRCCTRL_CRCSTART_F			(0x1 << 1)
#define CRCCTRL_CRCEN				(0x1 << 0)

/* DECON_CMU */
#define DECON_CMU				0x704

#define DECON_CMU_ALL_CLKGATE_ENABLE		0x3
#define DECON_CMU_SE_CLKGATE_ENABLE		(0x1 << 2)
#define DECON_CMU_SFR_CLKGATE_ENABLE		(0x1 << 1)
#define DECON_CMU_MEM_CLKGATE_ENABLE		(0x1 << 0)

/* DECON_UPDATE */
#define DECON_UPDATE				0x710

#define DECON_UPDATE_SLAVE_SYNC			(1 << 4)
#define DECON_UPDATE_STANDALONE_F		(1 << 0)

#endif /* EXYNOS_REGS_DECON7_H */
