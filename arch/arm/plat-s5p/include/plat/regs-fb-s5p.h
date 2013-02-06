/* linux/arch/arm/plat-s5p/include/plat/regs-fb-s5p.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register definition file for Samsung Display Controller (FIMD) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_REGS_FB_S5P_H
#define __ASM_PLAT_REGS_FB_S5P_H __FILE__

#define S3C_WINCON(x)		(0x0020 + (x * 0x04))
#define S3C_VIDOSD_A(x)		(0x0040 + (x * 0x10))
#define S3C_VIDOSD_B(x)		(0x0044 + (x * 0x10))
#define S3C_VIDOSD_C(x)		(0x0048 + (x * 0x10))
#define S3C_VIDOSD_D(x)		(0x004C + (x * 0x10))
#define S3C_VIDADDR_START0(x)	(0x00A0 + (x * 0x08))
#define S3C_VIDADDR_START1(x)	(0x00A4 + (x * 0x08))
#define S3C_VIDADDR_END0(x)	(0x00D0 + (x * 0x08))
#define S3C_VIDADDR_END1(x)	(0x00D4 + (x * 0x08))
#define S3C_VIDADDR_SIZE(x)	(0x0100 + (x * 0x04))
#define S3C_KEYCON(x)		(0x0140 + ((x - 1) * 0x08))
#define S3C_KEYVAL(x)		(0x0144 + ((x - 1) * 0x08))
#define S3C_WINMAP(x)		(0x0180 + (x * 0x04))

/*
 * Register Map
*/
#define S3C_VIDCON0		(0x0000)	/* Video control 0 */
#define S3C_VIDCON1		(0x0004)	/* Video control 1 */
#define S3C_VIDCON2		(0x0008)	/* Video control 2 */
#define S3C_PRTCON		(0x000C)	/* Protect control */

#define S3C_VIDTCON0		(0x0010)	/* Video time control 0 */
#define S3C_VIDTCON1		(0x0014)	/* Video time control 1 */
#define S3C_VIDTCON2		(0x0018)	/* Video time control 2 */

#define S3C_WINCON0		(0x0020)	/* Window control 0 */
#define S3C_WINCON1		(0x0024)	/* Window control 1 */
#define S3C_WINCON2		(0x0028)	/* Window control 2 */
#define S3C_WINCON3		(0x002C)	/* Window control 3 */
#define S3C_WINCON4		(0x0030)	/* Window control 4 */

#define S3C_WINSHMAP		(0x0034)	/* Window Shadow control */

#define S3C_VIDOSD0A		(0x0040)	/* Video Window 0 position control */
#define S3C_VIDOSD0B		(0x0044)	/* Video Window 0 position control1 */
#define S3C_VIDOSD0C		(0x0048)	/* Video Window 0 position control */

#define S3C_VIDOSD1A		(0x0050)	/* Video Window 1 position control */
#define S3C_VIDOSD1B		(0x0054)	/* Video Window 1 position control */
#define S3C_VIDOSD1C		(0x0058)	/* Video Window 1 position control */
#define S3C_VIDOSD1D		(0x005C)	/* Video Window 1 position control */

#define S3C_VIDOSD2A		(0x0060)	/* Video Window 2 position control */
#define S3C_VIDOSD2B		(0x0064)	/* Video Window 2 position control */
#define S3C_VIDOSD2C		(0x0068)	/* Video Window 2 position control */
#define S3C_VIDOSD2D		(0x006C)	/* Video Window 2 position control */

#define S3C_VIDOSD3A		(0x0070)	/* Video Window 3 position control */
#define S3C_VIDOSD3B		(0x0074)	/* Video Window 3 position control */
#define S3C_VIDOSD3C		(0x0078)	/* Video Window 3 position control */

#define S3C_VIDOSD4A		(0x0080)	/* Video Window 4 position control */
#define S3C_VIDOSD4B		(0x0084)	/* Video Window 4 position control */
#define S3C_VIDOSD4C		(0x0088)	/* Video Window 4 position control */

#define S3C_VIDW00ADD0B0	(0x00A0)	/* Window 0 buffer start address, buffer 0 */
#define S3C_VIDW00ADD0B1	(0x00A4)	/* Window 0 buffer start address, buffer 1 */
#define S3C_VIDW01ADD0B0	(0x00A8)	/* Window 1 buffer start address, buffer 0 */
#define S3C_VIDW01ADD0B1	(0x00AC)	/* Window 1 buffer start address, buffer 1 */
#define S3C_VIDW02ADD0		(0x00B0)	/* Window 2 buffer start address, buffer 0 */
#define S3C_VIDW03ADD0		(0x00B8)	/* Window 3 buffer start address, buffer 0 */
#define S3C_VIDW04ADD0		(0x00C0)	/* Window 4 buffer start address, buffer 0 */
#define S3C_VIDW00ADD1B0	(0x00D0)	/* Window 0 buffer end address, buffer 0 */
#define S3C_VIDW00ADD1B1	(0x00D4)	/* Window 0 buffer end address, buffer 1 */
#define S3C_VIDW01ADD1B0	(0x00D8)	/* Window 1 buffer end address, buffer 0 */
#define S3C_VIDW01ADD1B1	(0x00DC)	/* Window 1 buffer end address, buffer 1 */
#define S3C_VIDW02ADD1		(0x00E0)	/* Window 2 buffer end address */
#define S3C_VIDW03ADD1		(0x00E8)	/* Window 3 buffer end address */
#define S3C_VIDW04ADD1		(0x00F0)	/* Window 4 buffer end address */
#define S3C_VIDW00ADD2		(0x0100)	/* Window 0 buffer size */
#define S3C_VIDW01ADD2		(0x0104)	/* Window 1 buffer size */
#define S3C_VIDW02ADD2		(0x0108)	/* Window 2 buffer size */
#define S3C_VIDW03ADD2		(0x010C)	/* Window 3 buffer size */
#define S3C_VIDW04ADD2		(0x0110)	/* Window 4 buffer size */

#define S3C_VP1TCON0		(0x0118)	/* VP1 interface timing control 0 */
#define S3C_VP1TCON1		(0x011C)	/* VP1 interface timing control 1 */

#define S3C_VIDINTCON0		(0x0130)	/* Indicate the Video interrupt control */
#define S3C_VIDINTCON1		(0x0134)	/* Video Interrupt Pending */

#define S3C_W1KEYCON0		(0x0140)	/* Color key control */
#define S3C_W1KEYCON1		(0x0144)	/* Color key value (transparent value) */
#define S3C_W2KEYCON0		(0x0148)	/* Color key control */
#define S3C_W2KEYCON1		(0x014C)	/* Color key value (transparent value) */
#define S3C_W3KEYCON0		(0x0150)	/* Color key control */
#define S3C_W3KEYCON1		(0x0154)	/* Color key value (transparent value) */
#define S3C_W4KEYCON0		(0x0158)	/* Color key control */
#define S3C_W4KEYCON1		(0x015C)	/* Color key value (transparent value) */

#define S3C_W1KEYALPHA		(0x0160)	/* Color key alpha value */
#define S3C_W2KEYALPHA		(0x0164)	/* Color key alpha value */
#define S3C_W3KEYALPHA		(0x0168)	/* Color key alpha value */
#define S3C_W4KEYALPHA		(0x016C)	/* Color key alpha value */

#define S3C_DITHMODE		(0x0170)	/* Dithering mode */

#define S3C_WIN0MAP		(0x0180)	/* Window color control	*/
#define S3C_WIN1MAP		(0x0184)	/* Window color control	*/
#define S3C_WIN2MAP		(0x0188)	/* Window color control	*/
#define S3C_WIN3MAP		(0x018C)	/* Window color control	*/
#define S3C_WIN4MAP		(0x0190)	/* Window color control	*/

#define S3C_WPALCON_H		(0x019C)	/* Window Palette control */
#define S3C_WPALCON_L		(0x01A0)	/* Window Palette control */
#define S3C_TRIGCON		(0x01A4)	/* I80 / RGB Trigger Control Regiter	*/
#define S3C_I80IFCONA0		(0x01B0)	/* I80 Interface control 0 for Main LDI */
#define S3C_I80IFCONA1		(0x01B4)	/* I80 Interface control 0 for Sub LDI */
#define S3C_I80IFCONB0		(0x01B8)	/* I80 Interface control 1 for Main LDI	*/
#define S3C_I80IFCONB1		(0x01BC)	/* I80 Interface control 1 for Sub LDI	*/
#define S3C_LDI_CMDCON0		(0x01D0)	/* I80 Interface LDI Command Control 0	*/
#define S3C_LDI_CMDCON1		(0x01D4)	/* I80 Interface LDI Command Control 1	*/
#define S3C_SIFCCON0		(0x01E0)	/* LCD i80 System Interface Command Control 0	*/
#define S3C_SIFCCON1		(0x01E4)	/* LCD i80 System Interface Command Control 1	*/
#define S3C_SIFCCON2		(0x01E8)	/* LCD i80 System Interface Command Control 2	*/

#define S3C_VIDW0ALPHA0		(0x0200)	/* Window 0 alpha value 0 */
#define S3C_VIDW0ALPHA1		(0x0204)	/* Window 0 alpha value 1 */
#define S3C_VIDW1ALPHA0		(0x0208)	/* Window 1 alpha value 0 */
#define S3C_VIDW1ALPHA1		(0x020C)	/* Window 1 alpha value 1 */
#define S3C_VIDW2ALPHA0		(0x0210)	/* Window 2 alpha value 0 */
#define S3C_VIDW2ALPHA1		(0x0214)	/* Window 2 alpha value 1 */
#define S3C_VIDW3ALPHA0		(0x0218)	/* Window 3 alpha value 0 */
#define S3C_VIDW3ALPHA1		(0x021C)	/* Window 3 alpha value 1 */
#define S3C_VIDW4ALPHA0		(0x0220)	/* Window 4 alpha value 0 */
#define S3C_VIDW4ALPHA1		(0x0224)	/* Window 4 alpha value 1 */

#define S3C_BLENDEQ1		(0x0244)	/* Window 1 blending equation control */
#define S3C_BLENDEQ2		(0x0248)	/* Window 2 blending equation control */
#define S3C_BLENDEQ3		(0x024C)	/* Window 3 blending equation control */
#define S3C_BLENDEQ4		(0x0250)	/* Window 4 blending equation control */
#define S3C_BLENDCON		(0x0260)	/* Blending control */
#define S3C_DUALRGB		(0x027C)	/* DUALRGB INTERFACE SETTING REGISTER */
#define S3C_SHD_WIN_BASE	(0x4000)	/* Shadow Window control reg Base */
/*
 * Bit Definitions
*/

/* VIDCON0 */
#define S3C_VIDCON0_DSI_DISABLE			(0 << 30)
#define S3C_VIDCON0_DSI_ENABLE			(1 << 30)
#define S3C_VIDCON0_SCAN_PROGRESSIVE		(0 << 29)
#define S3C_VIDCON0_SCAN_INTERLACE		(1 << 29)
#define S3C_VIDCON0_SCAN_MASK			(1 << 29)
#define S3C_VIDCON0_VIDOUT_RGB			(0 << 26)
#define S3C_VIDCON0_VIDOUT_ITU			(1 << 26)
#define S3C_VIDCON0_VIDOUT_I80LDI0		(2 << 26)
#define S3C_VIDCON0_VIDOUT_I80LDI1		(3 << 26)
#define S3C_VIDCON0_VIDOUT_WB_RGB		(4 << 26)
#define S3C_VIDCON0_VIDOUT_WB_I80LDI0		(6 << 26)
#define S3C_VIDCON0_VIDOUT_WB_I80LDI1		(7 << 26)
#define S3C_VIDCON0_VIDOUT_MASK			(7 << 26)
#define S3C_VIDCON0_PNRMODE_RGB_P		(0 << 17)
#define S3C_VIDCON0_PNRMODE_BGR_P		(1 << 17)
#define S3C_VIDCON0_PNRMODE_RGB_S		(2 << 17)
#define S3C_VIDCON0_PNRMODE_BGR_S		(3 << 17)
#define S3C_VIDCON0_PNRMODE_MASK		(3 << 17)
#define S3C_VIDCON0_PNRMODE_SHIFT		(17)
#define S3C_VIDCON0_CLKVALUP_ALWAYS		(0 << 16)
#define S3C_VIDCON0_CLKVALUP_START_FRAME	(1 << 16)
#define S3C_VIDCON0_CLKVALUP_MASK		(1 << 16)
#define S3C_VIDCON0_CLKVAL_F(x)			(((x) & 0xff) << 6)
#define S3C_VIDCON0_VCLKEN_NORMAL		(0 << 5)
#define S3C_VIDCON0_VCLKEN_FREERUN		(1 << 5)
#define S3C_VIDCON0_VCLKEN_MASK			(1 << 5)
#define S3C_VIDCON0_CLKDIR_DIRECTED		(0 << 4)
#define S3C_VIDCON0_CLKDIR_DIVIDED		(1 << 4)
#define S3C_VIDCON0_CLKDIR_MASK			(1 << 4)
#define S3C_VIDCON0_CLKSEL_HCLK			(0 << 2)
#define S3C_VIDCON0_CLKSEL_SCLK			(1 << 2)
#define S3C_VIDCON0_CLKSEL_MASK			(1 << 2)
#define S3C_VIDCON0_ENVID_ENABLE		(1 << 1)
#define S3C_VIDCON0_ENVID_DISABLE		(0 << 1)
#define S3C_VIDCON0_ENVID_F_ENABLE		(1 << 0)
#define S3C_VIDCON0_ENVID_F_DISABLE		(0 << 0)

/* VIDCON1 */
#define S3C_VIDCON1_FIXVCLK_VCLK_HOLD		(0 << 9)
#define S3C_VIDCON1_FIXVCLK_VCLK_RUN		(1 << 9)
#define S3C_VIDCON1_FIXVCLK_VCLK_RUN_VDEN_DIS	(3 << 9)
#define S3C_VIDCON1_FIXVCLK_MASK		(3 << 9)
#define S3C_VIDCON1_IVCLK_FALLING_EDGE		(0 << 7)
#define S3C_VIDCON1_IVCLK_RISING_EDGE		(1 << 7)
#define S3C_VIDCON1_IHSYNC_NORMAL		(0 << 6)
#define S3C_VIDCON1_IHSYNC_INVERT		(1 << 6)
#define S3C_VIDCON1_IVSYNC_NORMAL		(0 << 5)
#define S3C_VIDCON1_IVSYNC_INVERT		(1 << 5)
#define S3C_VIDCON1_IVDEN_NORMAL		(0 << 4)
#define S3C_VIDCON1_IVDEN_INVERT		(1 << 4)
#define S3C_VIDCON1_VSTATUS_VSYNC		(0 << 13)
#define S3C_VIDCON1_VSTATUS_BACK		(1 << 13)
#define S3C_VIDCON1_VSTATUS_ACTIVE		(2 << 13)
#define S3C_VIDCON1_VSTATUS_FRONT		(3 << 13)
#define S3C_VIDCON1_VSTATUS_MASK		(3 << 13)


/* VIDCON2 */
#define S3C_VIDCON2_EN601_DISABLE		(0 << 23)
#define S3C_VIDCON2_EN601_ENABLE		(1 << 23)
#define S3C_VIDCON2_EN601_MASK			(1 << 23)
#define S3C_VIDCON2_WB_DISABLE			(0 << 15)
#define S3C_VIDCON2_WB_ENABLE			(1 << 15)
#define S3C_VIDCON2_WB_MASK			(1 << 15)
#define S3C_VIDCON2_TVFORMATSEL_HW		(0 << 14)
#define S3C_VIDCON2_TVFORMATSEL_SW		(1 << 14)
#define S3C_VIDCON2_TVFORMATSEL_MASK		(1 << 14)
#define S3C_VIDCON2_TVFORMATSEL_YUV422		(1 << 12)
#define S3C_VIDCON2_TVFORMATSEL_YUV444		(2 << 12)
#define S3C_VIDCON2_TVFORMATSEL_YUV_MASK	(3 << 12)
#define S3C_VIDCON2_ORGYUV_YCBCR		(0 << 8)
#define S3C_VIDCON2_ORGYUV_CBCRY		(1 << 8)
#define S3C_VIDCON2_ORGYUV_MASK			(1 << 8)
#define S3C_VIDCON2_YUVORD_CBCR			(0 << 7)
#define S3C_VIDCON2_YUVORD_CRCB			(1 << 7)
#define S3C_VIDCON2_YUVORD_MASK			(1 << 7)

/* PRTCON */
#define S3C_PRTCON_UPDATABLE			(0 << 11)
#define S3C_PRTCON_PROTECT			(1 << 11)

/* VIDTCON0 */
#define S3C_VIDTCON0_VBPDE(x)			(((x) & 0xff) << 24)
#define S3C_VIDTCON0_VBPD(x)			(((x) & 0xff) << 16)
#define S3C_VIDTCON0_VFPD(x)			(((x) & 0xff) << 8)
#define S3C_VIDTCON0_VSPW(x)			(((x) & 0xff) << 0)

/* VIDTCON1 */
#define S3C_VIDTCON1_VFPDE(x)			(((x) & 0xff) << 24)
#define S3C_VIDTCON1_HBPD(x)			(((x) & 0xff) << 16)
#define S3C_VIDTCON1_HFPD(x)			(((x) & 0xff) << 8)
#define S3C_VIDTCON1_HSPW(x)			(((x) & 0xff) << 0)

/* VIDTCON2 */
#define S3C_VIDTCON2_LINEVAL(x)			(((x) & 0x7ff) << 11)
#define S3C_VIDTCON2_HOZVAL(x)			(((x) & 0x7ff) << 0)

/* Window 0~4 Control - WINCONx */
#define S3C_WINCON_DATAPATH_DMA			(0 << 22)
#define S3C_WINCON_DATAPATH_LOCAL		(1 << 22)
#define S3C_WINCON_DATAPATH_MASK		(1 << 22)
#define S3C_WINCON_BUFSEL_0			(0 << 20)
#define S3C_WINCON_BUFSEL_1			(1 << 20)
#define S3C_WINCON_BUFSEL_MASK			(1 << 20)
#define S3C_WINCON_BUFSEL_SHIFT			(20)
#define S3C_WINCON_BUFAUTO_DISABLE		(0 << 19)
#define S3C_WINCON_BUFAUTO_ENABLE		(1 << 19)
#define S3C_WINCON_BUFAUTO_MASK			(1 << 19)
#define S3C_WINCON_BITSWP_DISABLE		(0 << 18)
#define S3C_WINCON_BITSWP_ENABLE		(1 << 18)
#define S3C_WINCON_BITSWP_SHIFT			(18)
#define S3C_WINCON_BYTESWP_DISABLE		(0 << 17)
#define S3C_WINCON_BYTESWP_ENABLE		(1 << 17)
#define S3C_WINCON_BYTESWP_SHIFT		(17)
#define S3C_WINCON_HAWSWP_DISABLE		(0 << 16)
#define S3C_WINCON_HAWSWP_ENABLE		(1 << 16)
#define S3C_WINCON_HAWSWP_SHIFT			(16)
#define S3C_WINCON_WSWP_DISABLE			(0 << 15)
#define S3C_WINCON_WSWP_ENABLE			(1 << 15)
#define S3C_WINCON_WSWP_SHIFT			(15)
#define S3C_WINCON_INRGB_RGB			(0 << 13)
#define S3C_WINCON_INRGB_YUV			(1 << 13)
#define S3C_WINCON_INRGB_MASK			(1 << 13)
#define S3C_WINCON_BURSTLEN_16WORD		(0 << 9)
#define S3C_WINCON_BURSTLEN_8WORD		(1 << 9)
#define S3C_WINCON_BURSTLEN_4WORD		(2 << 9)
#define S3C_WINCON_BURSTLEN_MASK		(3 << 9)
#define S3C_WINCON_ALPHA_MULTI_DISABLE		(0 << 7)
#define S3C_WINCON_ALPHA_MULTI_ENABLE		(1 << 7)
#define S3C_WINCON_BLD_PLANE			(0 << 6)
#define S3C_WINCON_BLD_PIXEL			(1 << 6)
#define S3C_WINCON_BLD_MASK			(1 << 6)
#define S3C_WINCON_BPPMODE_1BPP			(0 << 2)
#define S3C_WINCON_BPPMODE_2BPP			(1 << 2)
#define S3C_WINCON_BPPMODE_4BPP			(2 << 2)
#define S3C_WINCON_BPPMODE_8BPP_PAL		(3 << 2)
#define S3C_WINCON_BPPMODE_8BPP			(4 << 2)
#define S3C_WINCON_BPPMODE_16BPP_565		(5 << 2)
#define S3C_WINCON_BPPMODE_16BPP_A555		(6 << 2)
#define S3C_WINCON_BPPMODE_18BPP_666		(8 << 2)
#define S3C_WINCON_BPPMODE_18BPP_A665		(9 << 2)
#define S3C_WINCON_BPPMODE_24BPP_888		(0xb << 2)
#define S3C_WINCON_BPPMODE_24BPP_A887		(0xc << 2)
#define S3C_WINCON_BPPMODE_32BPP		(0xd << 2)
#define S3C_WINCON_BPPMODE_16BPP_A444		(0xe << 2)
#define S3C_WINCON_BPPMODE_15BPP_555		(0xf << 2)
#define S3C_WINCON_BPPMODE_MASK			(0xf << 2)
#define S3C_WINCON_BPPMODE_SHIFT		(2)
#define S3C_WINCON_ALPHA0_SEL			(0 << 1)
#define S3C_WINCON_ALPHA1_SEL			(1 << 1)
#define S3C_WINCON_ALPHA_SEL_MASK		(1 << 1)
#define S3C_WINCON_ENWIN_DISABLE		(0 << 0)
#define S3C_WINCON_ENWIN_ENABLE			(1 << 0)

/* WINCON1 special */
#define S3C_WINCON1_VP_DISABLE			(0 << 24)
#define S3C_WINCON1_VP_ENABLE			(1 << 24)
#define S3C_WINCON1_LOCALSEL_FIMC1		(0 << 23)
#define S3C_WINCON1_LOCALSEL_VP			(1 << 23)
#define S3C_WINCON1_LOCALSEL_MASK		(1 << 23)

/* WINSHMAP */
#define S3C_WINSHMAP_PROTECT(x)			(((x) & 0x1f) << 10)
#define S3C_WINSHMAP_CH_ENABLE(x)		(1 << (x))
#define S3C_WINSHMAP_CH_DISABLE(x)		(1 << (x))
#define S3C_WINSHMAP_LOCAL_ENABLE(x)		(0x20 << (x))
#define S3C_WINSHMAP_LOCAL_DISABLE(x)		(0x20 << (x))


/* VIDOSDxA, VIDOSDxB */
#define S3C_VIDOSD_LEFT_X(x)			(((x) & 0x7ff) << 11)
#define S3C_VIDOSD_TOP_Y(x)			(((x) & 0x7ff) << 0)
#define S3C_VIDOSD_RIGHT_X(x)			(((x) & 0x7ff) << 11)
#define S3C_VIDOSD_BOTTOM_Y(x)			(((x) & 0x7ff) << 0)

/* VIDOSD0C, VIDOSDxD */
#define S3C_VIDOSD_SIZE(x)			(((x) & 0xffffff) << 0)

/* VIDOSDxC (1~4) */
#define S3C_VIDOSD_ALPHA0_R(x)			(((x) & 0xf) << 20)
#define S3C_VIDOSD_ALPHA0_G(x)			(((x) & 0xf) << 16)
#define S3C_VIDOSD_ALPHA0_B(x)			(((x) & 0xf) << 12)
#define S3C_VIDOSD_ALPHA1_R(x)			(((x) & 0xf) << 8)
#define S3C_VIDOSD_ALPHA1_G(x)			(((x) & 0xf) << 4)
#define S3C_VIDOSD_ALPHA1_B(x)			(((x) & 0xf) << 0)
#define S3C_VIDOSD_ALPHA0_SHIFT			(12)
#define S3C_VIDOSD_ALPHA1_SHIFT			(0)

/* Start Address */
#define S3C_VIDADDR_START_VBANK(x)		(((x) & 0xff) << 24)
#define S3C_VIDADDR_START_VBASEU(x)		(((x) & 0xffffff) << 0)

/* End Address */
#define S3C_VIDADDR_END_VBASEL(x)		(((x) & 0xffffff) << 0)

/* Buffer Size */
#define S3C_VIDADDR_OFFSIZE(x)			(((x) & 0x1fff) << 13)
#define S3C_VIDADDR_PAGEWIDTH(x)		(((x) & 0x1fff) << 0)

/* WIN Color Map */
#define S3C_WINMAP_COLOR(x)			((x) & 0xffffff)

/* VIDINTCON0 */
#define S3C_VIDINTCON0_SYSMAINCON_DISABLE	(0 << 19)
#define S3C_VIDINTCON0_SYSMAINCON_ENABLE	(1 << 19)
#define S3C_VIDINTCON0_SYSSUBCON_DISABLE	(0 << 18)
#define S3C_VIDINTCON0_SYSSUBCON_ENABLE		(1 << 18)
#define S3C_VIDINTCON0_SYSIFDONE_DISABLE	(0 << 17)
#define S3C_VIDINTCON0_SYSIFDONE_ENABLE		(1 << 17)
#define S3C_VIDINTCON0_FRAMESEL0_BACK		(0 << 15)
#define S3C_VIDINTCON0_FRAMESEL0_VSYNC		(1 << 15)
#define S3C_VIDINTCON0_FRAMESEL0_ACTIVE		(2 << 15)
#define S3C_VIDINTCON0_FRAMESEL0_FRONT		(3 << 15)
#define S3C_VIDINTCON0_FRAMESEL0_MASK		(3 << 15)
#define S3C_VIDINTCON0_FRAMESEL1_NONE		(0 << 13)
#define S3C_VIDINTCON0_FRAMESEL1_BACK		(1 << 13)
#define S3C_VIDINTCON0_FRAMESEL1_VSYNC		(2 << 13)
#define S3C_VIDINTCON0_FRAMESEL1_FRONT		(3 << 13)
#define S3C_VIDINTCON0_INTFRMEN_DISABLE		(0 << 12)
#define S3C_VIDINTCON0_INTFRMEN_ENABLE		(1 << 12)
#define S3C_VIDINTCON0_FIFOSEL_WIN4		(1 << 11)
#define S3C_VIDINTCON0_FIFOSEL_WIN3		(1 << 10)
#define S3C_VIDINTCON0_FIFOSEL_WIN2		(1 << 9)
#define S3C_VIDINTCON0_FIFOSEL_WIN1		(1 << 6)
#define S3C_VIDINTCON0_FIFOSEL_WIN0		(1 << 5)
#define S3C_VIDINTCON0_FIFOSEL_ALL		(0x73 << 5)
#define S3C_VIDINTCON0_FIFOSEL_MASK		(0x73 << 5)
#define S3C_VIDINTCON0_FIFOLEVEL_25		(0 << 2)
#define S3C_VIDINTCON0_FIFOLEVEL_50		(1 << 2)
#define S3C_VIDINTCON0_FIFOLEVEL_75		(2 << 2)
#define S3C_VIDINTCON0_FIFOLEVEL_EMPTY		(3 << 2)
#define S3C_VIDINTCON0_FIFOLEVEL_FULL		(4 << 2)
#define S3C_VIDINTCON0_FIFOLEVEL_MASK		(7 << 2)
#define S3C_VIDINTCON0_INTFIFO_DISABLE		(0 << 1)
#define S3C_VIDINTCON0_INTFIFO_ENABLE		(1 << 1)
#define S3C_VIDINTCON0_INT_DISABLE		(0 << 0)
#define S3C_VIDINTCON0_INT_ENABLE		(1 << 0)
#define S3C_VIDINTCON0_INT_MASK			(1 << 0)

/* VIDINTCON1 */
#define S3C_VIDINTCON1_INTVPPEND		(1 << 5)
#define S3C_VIDINTCON1_INTI80PEND		(1 << 2)
#define S3C_VIDINTCON1_INTFRMPEND		(1 << 1)
#define S3C_VIDINTCON1_INTFIFOPEND		(1 << 0)

/* WINMAP */
#define S3C_WINMAP_ENABLE			(1 << 24)

/* WxKEYCON0 (1~4) */
#define S3C_KEYCON0_KEYBLEN_DISABLE		(0 << 26)
#define S3C_KEYCON0_KEYBLEN_ENABLE		(1 << 26)
#define S3C_KEYCON0_KEY_DISABLE			(0 << 25)
#define S3C_KEYCON0_KEY_ENABLE			(1 << 25)
#define S3C_KEYCON0_DIRCON_MATCH_FG		(0 << 24)
#define S3C_KEYCON0_DIRCON_MATCH_BG		(1 << 24)
#define S3C_KEYCON0_COMPKEY(x)			(((x) & 0xffffff) << 0)

/* WxKEYCON1 (1~4) */
#define S3C_KEYCON1_COLVAL(x)			(((x) & 0xffffff) << 0)

/* DUALRGB */
#define S3C_DUALRGB_BYPASS_SINGLE	(0 << 0)
#define S3C_DUALRGB_BYPASS_DUAL	(1 << 0)
#define S3C_DUALRGB_MIE_DUAL		(2 << 0)
#define S3C_DUALRGB_MDNIE		(3 << 0)

#endif /* __ASM_PLAT_REGS_FB_S5P_H */
