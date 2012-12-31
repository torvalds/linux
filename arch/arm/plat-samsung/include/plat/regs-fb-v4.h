/* arch/arm/plat-samsung/include/plat/regs-fb-v4.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      http://armlinux.simtec.co.uk/
 *      Ben Dooks <ben@simtec.co.uk>
 *
 * S3C64XX - new-style framebuffer register definitions
 *
 * This is the register set for the new style framebuffer interface
 * found from the S3C2443 onwards and specifically the S3C64XX series
 * S3C6400 and S3C6410.
 *
 * The file contains the cpu specific items which change between whichever
 * architecture is selected. See <plat/regs-fb.h> for the core definitions
 * that are the same.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* include the core definitions here, in case we really do need to
 * override them at a later date.
*/

#include <plat/regs-fb.h>

#define S3C_FB_MAX_WIN (5)  /* number of hardware windows available. */
#define VIDCON1_FSTATUS_EVEN	(1 << 15)

/* Video timing controls */
#ifdef CONFIG_FB_EXYNOS_FIMD_V8
#define VIDTCON0				(0x20010)
#define VIDTCON1				(0x20014)
#define VIDTCON2				(0x20018)
#define VIDTCON3				(0x2001C)
#else
#define VIDTCON0				(0x10)
#define VIDTCON1				(0x14)
#define VIDTCON2				(0x18)
#define VIDTCON3				(0x1C)
#endif

/* Window position controls */

#define WINCON(_win)				(0x20 + ((_win) * 4))

/* OSD1 and OSD4 do not have register D */

#define VIDOSD_BASE				(0x40)

#define VIDINTCON0				(0x130)
#define VIDINTCON1				(0x134)

/* WINCONx */

#define WINCONx_CSC_CON_EQ709			(1 << 28)
#define WINCONx_CSC_CON_EQ601			(0 << 28)
#define WINCONx_CSCWIDTH_MASK			(0x3 << 26)
#define WINCONx_CSCWIDTH_SHIFT			(26)
#define WINCONx_CSCWIDTH_WIDE			(0x0 << 26)
#define WINCONx_CSCWIDTH_NARROW			(0x3 << 26)

#define WINCONx_ENLOCAL				(1 << 22)
#define WINCONx_BUFSTATUS			(1 << 21)
#define WINCONx_BUFSEL				(1 << 20)
#define WINCONx_BUFAUTOEN			(1 << 19)
#define WINCONx_YCbCr				(1 << 13)

#define WINCON1_LOCALSEL_CAMIF			(1 << 23)

#define WINCON2_LOCALSEL_CAMIF			(1 << 23)
#define WINCON2_BLD_PIX				(1 << 6)

#define WINCON2_ALPHA_SEL			(1 << 1)
#define WINCON2_BPPMODE_MASK			(0xf << 2)
#define WINCON2_BPPMODE_SHIFT			(2)
#define WINCON2_BPPMODE_1BPP			(0x0 << 2)
#define WINCON2_BPPMODE_2BPP			(0x1 << 2)
#define WINCON2_BPPMODE_4BPP			(0x2 << 2)
#define WINCON2_BPPMODE_8BPP_1232		(0x4 << 2)
#define WINCON2_BPPMODE_16BPP_565		(0x5 << 2)
#define WINCON2_BPPMODE_16BPP_A1555		(0x6 << 2)
#define WINCON2_BPPMODE_16BPP_I1555		(0x7 << 2)
#define WINCON2_BPPMODE_18BPP_666		(0x8 << 2)
#define WINCON2_BPPMODE_18BPP_A1665		(0x9 << 2)
#define WINCON2_BPPMODE_19BPP_A1666		(0xa << 2)
#define WINCON2_BPPMODE_24BPP_888		(0xb << 2)
#define WINCON2_BPPMODE_24BPP_A1887		(0xc << 2)
#define WINCON2_BPPMODE_25BPP_A1888		(0xd << 2)
#define WINCON2_BPPMODE_28BPP_A4888		(0xd << 2)

#define WINCON3_BLD_PIX				(1 << 6)

#define WINCON3_ALPHA_SEL			(1 << 1)
#define WINCON3_BPPMODE_MASK			(0xf << 2)
#define WINCON3_BPPMODE_SHIFT			(2)
#define WINCON3_BPPMODE_1BPP			(0x0 << 2)
#define WINCON3_BPPMODE_2BPP			(0x1 << 2)
#define WINCON3_BPPMODE_4BPP			(0x2 << 2)
#define WINCON3_BPPMODE_16BPP_565		(0x5 << 2)
#define WINCON3_BPPMODE_16BPP_A1555		(0x6 << 2)
#define WINCON3_BPPMODE_16BPP_I1555		(0x7 << 2)
#define WINCON3_BPPMODE_18BPP_666		(0x8 << 2)
#define WINCON3_BPPMODE_18BPP_A1665		(0x9 << 2)
#define WINCON3_BPPMODE_19BPP_A1666		(0xa << 2)
#define WINCON3_BPPMODE_24BPP_888		(0xb << 2)
#define WINCON3_BPPMODE_24BPP_A1887		(0xc << 2)
#define WINCON3_BPPMODE_25BPP_A1888		(0xd << 2)
#define WINCON3_BPPMODE_28BPP_A4888		(0xd << 2)

#define VIDINTCON0_FIFIOSEL_WINDOW2		(0x10 << 5)
#define VIDINTCON0_FIFIOSEL_WINDOW3		(0x20 << 5)
#define VIDINTCON0_FIFIOSEL_WINDOW4		(0x40 << 5)

#define DITHMODE				(0x170)
#define WINxMAP(_win)				(0x180 + ((_win) * 4))


#define DITHMODE_R_POS_MASK			(0x3 << 5)
#define DITHMODE_R_POS_SHIFT			(5)
#define DITHMODE_R_POS_8BIT			(0x0 << 5)
#define DITHMODE_R_POS_6BIT			(0x1 << 5)
#define DITHMODE_R_POS_5BIT			(0x2 << 5)

#define DITHMODE_G_POS_MASK			(0x3 << 3)
#define DITHMODE_G_POS_SHIFT			(3)
#define DITHMODE_G_POS_8BIT			(0x0 << 3)
#define DITHMODE_G_POS_6BIT			(0x1 << 3)
#define DITHMODE_G_POS_5BIT			(0x2 << 3)

#define DITHMODE_B_POS_MASK			(0x3 << 1)
#define DITHMODE_B_POS_SHIFT			(1)
#define DITHMODE_B_POS_8BIT			(0x0 << 1)
#define DITHMODE_B_POS_6BIT			(0x1 << 1)
#define DITHMODE_B_POS_5BIT			(0x2 << 1)

#define DITHMODE_DITH_EN			(1 << 0)

#define WPALCON					(0x1A0)

/* Palette control */
/* Note for S5PC100: you can still use those macros on WPALCON (aka WPALCON_L),
 * but make sure that WPALCON_H W2PAL-W4PAL entries are zeroed out */
#define WPALCON_W4PAL_16BPP_A555		(1 << 8)
#define WPALCON_W3PAL_16BPP_A555		(1 << 7)
#define WPALCON_W2PAL_16BPP_A555		(1 << 6)


/* Notes on per-window bpp settings
 *
 * Value	Win0	 Win1	  Win2	   Win3	    Win 4
 * 0000		1(P)	 1(P)	  1(P)	   1(P)	    1(P)
 * 0001		2(P)	 2(P)     2(P)	   2(P)	    2(P)
 * 0010		4(P)	 4(P)     4(P)	   4(P)     -none-
 * 0011		8(P)	 8(P)     -none-   -none-   -none-
 * 0100		-none-	 8(A232)  8(A232)  -none-   -none-
 * 0101		16(565)	 16(565)  16(565)  16(565)   16(565)
 * 0110		-none-	 16(A555) 16(A555) 16(A555)  16(A555)
 * 0111		16(I555) 16(I565) 16(I555) 16(I555)  16(I555)
 * 1000		18(666)	 18(666)  18(666)  18(666)   18(666)
 * 1001		-none-	 18(A665) 18(A665) 18(A665)  16(A665)
 * 1010		-none-	 19(A666) 19(A666) 19(A666)  19(A666)
 * 1011		24(888)	 24(888)  24(888)  24(888)   24(888)
 * 1100		-none-	 24(A887) 24(A887) 24(A887)  24(A887)
 * 1101		-none-	 25(A888) 25(A888) 25(A888)  25(A888)
 * 1110		-none-	 -none-	  -none-   -none-    -none-
 * 1111		-none-	 -none-   -none-   -none-    -none-
*/
