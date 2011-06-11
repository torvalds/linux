/*
 * Register definitions for the m88alp01 camera interface.  Offsets in bytes
 * as given in the spec.
 *
 * Copyright 2006 One Laptop Per Child Association, Inc.
 *
 * Written by Jonathan Corbet, corbet@lwn.net.
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */
#define REG_Y0BAR	0x00
#define REG_Y1BAR	0x04
#define REG_Y2BAR	0x08
/* ... */

#define REG_IMGPITCH	0x24	/* Image pitch register */
#define   IMGP_YP_SHFT	  2		/* Y pitch params */
#define   IMGP_YP_MASK	  0x00003ffc	/* Y pitch field */
#define	  IMGP_UVP_SHFT	  18		/* UV pitch (planar) */
#define   IMGP_UVP_MASK   0x3ffc0000
#define REG_IRQSTATRAW	0x28	/* RAW IRQ Status */
#define   IRQ_EOF0	  0x00000001	/* End of frame 0 */
#define   IRQ_EOF1	  0x00000002	/* End of frame 1 */
#define   IRQ_EOF2	  0x00000004	/* End of frame 2 */
#define   IRQ_SOF0	  0x00000008	/* Start of frame 0 */
#define   IRQ_SOF1	  0x00000010	/* Start of frame 1 */
#define   IRQ_SOF2	  0x00000020	/* Start of frame 2 */
#define   IRQ_OVERFLOW	  0x00000040	/* FIFO overflow */
#define   IRQ_TWSIW	  0x00010000	/* TWSI (smbus) write */
#define   IRQ_TWSIR	  0x00020000	/* TWSI read */
#define   IRQ_TWSIE	  0x00040000	/* TWSI error */
#define   TWSIIRQS (IRQ_TWSIW|IRQ_TWSIR|IRQ_TWSIE)
#define   FRAMEIRQS (IRQ_EOF0|IRQ_EOF1|IRQ_EOF2|IRQ_SOF0|IRQ_SOF1|IRQ_SOF2)
#define   ALLIRQS (TWSIIRQS|FRAMEIRQS|IRQ_OVERFLOW)
#define REG_IRQMASK	0x2c	/* IRQ mask - same bits as IRQSTAT */
#define REG_IRQSTAT	0x30	/* IRQ status / clear */

#define REG_IMGSIZE	0x34	/* Image size */
#define  IMGSZ_V_MASK	  0x1fff0000
#define  IMGSZ_V_SHIFT	  16
#define	 IMGSZ_H_MASK	  0x00003fff
#define REG_IMGOFFSET	0x38	/* IMage offset */

#define REG_CTRL0	0x3c	/* Control 0 */
#define   C0_ENABLE	  0x00000001	/* Makes the whole thing go */

/* Mask for all the format bits */
#define   C0_DF_MASK	  0x00fffffc    /* Bits 2-23 */

/* RGB ordering */
#define   C0_RGB4_RGBX	  0x00000000
#define	  C0_RGB4_XRGB	  0x00000004
#define	  C0_RGB4_BGRX	  0x00000008
#define   C0_RGB4_XBGR	  0x0000000c
#define   C0_RGB5_RGGB	  0x00000000
#define	  C0_RGB5_GRBG	  0x00000004
#define	  C0_RGB5_GBRG	  0x00000008
#define   C0_RGB5_BGGR	  0x0000000c

/* Spec has two fields for DIN and DOUT, but they must match, so
   combine them here. */
#define   C0_DF_YUV	  0x00000000    /* Data is YUV	    */
#define   C0_DF_RGB	  0x000000a0	/* ... RGB		    */
#define   C0_DF_BAYER     0x00000140	/* ... Bayer                */
/* 8-8-8 must be missing from the below - ask */
#define   C0_RGBF_565	  0x00000000
#define   C0_RGBF_444	  0x00000800
#define   C0_RGB_BGR	  0x00001000	/* Blue comes first */
#define   C0_YUV_PLANAR	  0x00000000	/* YUV 422 planar format */
#define   C0_YUV_PACKED	  0x00008000	/* YUV 422 packed	*/
#define   C0_YUV_420PL	  0x0000a000	/* YUV 420 planar	*/
/* Think that 420 packed must be 111 - ask */
#define	  C0_YUVE_YUYV	  0x00000000	/* Y1CbY0Cr 		*/
#define	  C0_YUVE_YVYU	  0x00010000	/* Y1CrY0Cb 		*/
#define	  C0_YUVE_VYUY	  0x00020000	/* CrY1CbY0 		*/
#define	  C0_YUVE_UYVY	  0x00030000	/* CbY1CrY0 		*/
#define   C0_YUVE_XYUV	  0x00000000    /* 420: .YUV		*/
#define	  C0_YUVE_XYVU	  0x00010000	/* 420: .YVU 		*/
#define	  C0_YUVE_XUVY	  0x00020000	/* 420: .UVY 		*/
#define	  C0_YUVE_XVUY	  0x00030000	/* 420: .VUY 		*/
/* Bayer bits 18,19 if needed */
#define   C0_HPOL_LOW	  0x01000000	/* HSYNC polarity active low */
#define   C0_VPOL_LOW	  0x02000000	/* VSYNC polarity active low */
#define   C0_VCLK_LOW	  0x04000000	/* VCLK on falling edge */
#define   C0_DOWNSCALE	  0x08000000	/* Enable downscaler */
#define	  C0_SIFM_MASK	  0xc0000000	/* SIF mode bits */
#define   C0_SIF_HVSYNC	  0x00000000	/* Use H/VSYNC */
#define   CO_SOF_NOSYNC	  0x40000000	/* Use inband active signaling */


#define REG_CTRL1	0x40	/* Control 1 */
#define   C1_444ALPHA	  0x00f00000	/* Alpha field in RGB444 */
#define   C1_ALPHA_SHFT	  20
#define   C1_DMAB32	  0x00000000	/* 32-byte DMA burst */
#define   C1_DMAB16	  0x02000000	/* 16-byte DMA burst */
#define	  C1_DMAB64	  0x04000000	/* 64-byte DMA burst */
#define	  C1_DMAB_MASK	  0x06000000
#define   C1_TWOBUFS	  0x08000000	/* Use only two DMA buffers */
#define   C1_PWRDWN	  0x10000000	/* Power down */

#define REG_CLKCTRL	0x88	/* Clock control */
#define   CLK_DIV_MASK	  0x0000ffff	/* Upper bits RW "reserved" */

#define REG_GPR		0xb4	/* General purpose register.  This
				   controls inputs to the power and reset
				   pins on the OV7670 used with OLPC;
				   other deployments could differ.  */
#define   GPR_C1EN	  0x00000020	/* Pad 1 (power down) enable */
#define   GPR_C0EN	  0x00000010	/* Pad 0 (reset) enable */
#define	  GPR_C1	  0x00000002	/* Control 1 value */
/*
 * Control 0 is wired to reset on OLPC machines.  For ov7x sensors,
 * it is active low, for 0v6x, instead, it's active high.  What
 * fun.
 */
#define   GPR_C0	  0x00000001	/* Control 0 value */

#define REG_TWSIC0	0xb8	/* TWSI (smbus) control 0 */
#define   TWSIC0_EN       0x00000001	/* TWSI enable */
#define   TWSIC0_MODE	  0x00000002	/* 1 = 16-bit, 0 = 8-bit */
#define   TWSIC0_SID	  0x000003fc	/* Slave ID */
#define   TWSIC0_SID_SHIFT 2
#define   TWSIC0_CLKDIV   0x0007fc00	/* Clock divider */
#define   TWSIC0_MASKACK  0x00400000	/* Mask ack from sensor */
#define   TWSIC0_OVMAGIC  0x00800000	/* Make it work on OV sensors */

#define REG_TWSIC1	0xbc	/* TWSI control 1 */
#define   TWSIC1_DATA	  0x0000ffff	/* Data to/from camchip */
#define   TWSIC1_ADDR	  0x00ff0000	/* Address (register) */
#define   TWSIC1_ADDR_SHIFT 16
#define   TWSIC1_READ	  0x01000000	/* Set for read op */
#define   TWSIC1_WSTAT	  0x02000000	/* Write status */
#define   TWSIC1_RVALID	  0x04000000	/* Read data valid */
#define   TWSIC1_ERROR	  0x08000000	/* Something screwed up */


#define REG_UBAR	0xc4	/* Upper base address register */

/*
 * Here's the weird global control registers which are said to live
 * way up here.
 */
#define REG_GL_CSR     0x3004  /* Control/status register */
#define   GCSR_SRS	 0x00000001	/* SW Reset set */
#define   GCSR_SRC  	 0x00000002	/* SW Reset clear */
#define	  GCSR_MRS	 0x00000004	/* Master reset set */
#define	  GCSR_MRC	 0x00000008	/* HW Reset clear */
#define   GCSR_CCIC_EN   0x00004000    /* CCIC Clock enable */
#define REG_GL_IMASK   0x300c  /* Interrupt mask register */
#define   GIMSK_CCIC_EN          0x00000004    /* CCIC Interrupt enable */

#define REG_GL_FCR	0x3038  /* GPIO functional control register */
#define	  GFCR_GPIO_ON	  0x08		/* Camera GPIO enabled */
#define REG_GL_GPIOR	0x315c	/* GPIO register */
#define   GGPIO_OUT  		0x80000	/* GPIO output */
#define   GGPIO_VAL  		0x00008	/* Output pin value */

#define REG_LEN                REG_GL_IMASK + 4


/*
 * Useful stuff that probably belongs somewhere global.
 */
#define VGA_WIDTH	640
#define VGA_HEIGHT	480
