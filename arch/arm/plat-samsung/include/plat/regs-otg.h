/* linux/arch/arm/plat-samsung/include/plat/regs-otg.h
 *
 * Copyright (C) 2004 Herbert Poetzl <herbert@13thfloor.at>
 *
 * This include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
*/

#ifndef __ASM_ARCH_REGS_USB_OTG_HS_H
#define __ASM_ARCH_REGS_USB_OTG_HS_H

/* USB2.0 OTG Controller register */
#define S3C_USBOTG_PHYREG(x)		((x) + S3C_VA_HSPHY)
#define S3C_USBOTG_PHYPWR		S3C_USBOTG_PHYREG(0x0)
#define S3C_USBOTG_PHYCLK		S3C_USBOTG_PHYREG(0x4)
#define S3C_USBOTG_RSTCON		S3C_USBOTG_PHYREG(0x8)
#define S3C_USBOTG_PHY1CON		S3C_USBOTG_PHYREG(0x34)

/* USB2.0 OTG Controller register */
#define S3C_USBOTGREG(x) (x)
/* Core Global Registers */
#define S3C_UDC_OTG_GOTGCTL		S3C_USBOTGREG(0x000)
#define S3C_UDC_OTG_GOTGINT		S3C_USBOTGREG(0x004)
#define S3C_UDC_OTG_GAHBCFG		S3C_USBOTGREG(0x008)
#define S3C_UDC_OTG_GUSBCFG		S3C_USBOTGREG(0x00C)
#define S3C_UDC_OTG_GRSTCTL		S3C_USBOTGREG(0x010)
#define S3C_UDC_OTG_GINTSTS		S3C_USBOTGREG(0x014)
#define S3C_UDC_OTG_GINTMSK		S3C_USBOTGREG(0x018)
#define S3C_UDC_OTG_GRXSTSR		S3C_USBOTGREG(0x01C)
#define S3C_UDC_OTG_GRXSTSP		S3C_USBOTGREG(0x020)
#define S3C_UDC_OTG_GRXFSIZ		S3C_USBOTGREG(0x024)
#define S3C_UDC_OTG_GNPTXFSIZ		S3C_USBOTGREG(0x028)
#define S3C_UDC_OTG_GNPTXSTS		S3C_USBOTGREG(0x02C)

#define S3C_UDC_OTG_HPTXFSIZ		S3C_USBOTGREG(0x100)
#define S3C_UDC_OTG_DIEPTXF(n)		S3C_USBOTGREG(0x104 + (n-1)*0x4)

/* Host Mode Registers */
/* Host Global Registers */
#define S3C_UDC_OTG_HCFG		S3C_USBOTGREG(0x400)
#define S3C_UDC_OTG_HFIR		S3C_USBOTGREG(0x404)
#define S3C_UDC_OTG_HFNUM		S3C_USBOTGREG(0x408)
#define S3C_UDC_OTG_HPTXSTS		S3C_USBOTGREG(0x410)
#define S3C_UDC_OTG_HAINT		S3C_USBOTGREG(0x414)
#define S3C_UDC_OTG_HAINTMSK		S3C_USBOTGREG(0x418)

/* Host Port Control & Status Registers */
#define S3C_UDC_OTG_HPRT		S3C_USBOTGREG(0x440)

/* Host Channel-Specific Registers */
#define S3C_UDC_OTG_HCCHAR0		S3C_USBOTGREG(0x500)
#define S3C_UDC_OTG_HCSPLT0		S3C_USBOTGREG(0x504)
#define S3C_UDC_OTG_HCINT0		S3C_USBOTGREG(0x508)
#define S3C_UDC_OTG_HCINTMSK0		S3C_USBOTGREG(0x50C)
#define S3C_UDC_OTG_HCTSIZ0		S3C_USBOTGREG(0x510)
#define S3C_UDC_OTG_HCDMA0		S3C_USBOTGREG(0x514)

/* Device Mode Registers */
/*------------------------------------------------ */
/* Device Global Registers */
#define S3C_UDC_OTG_DCFG		S3C_USBOTGREG(0x800)
#define S3C_UDC_OTG_DCTL		S3C_USBOTGREG(0x804)
#define S3C_UDC_OTG_DSTS		S3C_USBOTGREG(0x808)
#define S3C_UDC_OTG_DIEPMSK		S3C_USBOTGREG(0x810)
#define S3C_UDC_OTG_DOEPMSK		S3C_USBOTGREG(0x814)
#define S3C_UDC_OTG_DAINT		S3C_USBOTGREG(0x818)
#define S3C_UDC_OTG_DAINTMSK		S3C_USBOTGREG(0x81C)
#define S3C_UDC_OTG_DTKNQR1		S3C_USBOTGREG(0x820)
#define S3C_UDC_OTG_DTKNQR2		S3C_USBOTGREG(0x824)
#define S3C_UDC_OTG_DVBUSDIS		S3C_USBOTGREG(0x828)
#define S3C_UDC_OTG_DVBUSPULSE		S3C_USBOTGREG(0x82C)
#define S3C_UDC_OTG_DTKNQR3		S3C_USBOTGREG(0x830)
#define S3C_UDC_OTG_DTKNQR4		S3C_USBOTGREG(0x834)

/*------------------------------------------------ */
/* Device Logical IN Endpoint-Specific Registers */
#define S3C_UDC_OTG_DIEPCTL(n)		S3C_USBOTGREG(0x900 + n*0x20)
#define S3C_UDC_OTG_DIEPINT(n)		S3C_USBOTGREG(0x908 + n*0x20)
#define S3C_UDC_OTG_DIEPTSIZ(n)		S3C_USBOTGREG(0x910 + n*0x20)
#define S3C_UDC_OTG_DIEPDMA(n)		S3C_USBOTGREG(0x914 + n*0x20)

/*------------------------------------------------ */
/* Device Logical OUT Endpoint-Specific Registers */
#define S3C_UDC_OTG_DOEPCTL(n)		S3C_USBOTGREG(0xB00 + n*0x20)
#define S3C_UDC_OTG_DOEPINT(n)		S3C_USBOTGREG(0xB08 + n*0x20)
#define S3C_UDC_OTG_DOEPTSIZ(n)		S3C_USBOTGREG(0xB10 + n*0x20)
#define S3C_UDC_OTG_DOEPDMA(n)		S3C_USBOTGREG(0xB14 + n*0x20)

/*------------------------------------------------ */
/* Endpoint FIFO address */
#define S3C_UDC_OTG_EP0_FIFO		S3C_USBOTGREG(0x1000)
#define S3C_UDC_OTG_EP1_FIFO		S3C_USBOTGREG(0x2000)
#define S3C_UDC_OTG_EP2_FIFO		S3C_USBOTGREG(0x3000)
#define S3C_UDC_OTG_EP3_FIFO		S3C_USBOTGREG(0x4000)
#define S3C_UDC_OTG_EP4_FIFO		S3C_USBOTGREG(0x5000)
#define S3C_UDC_OTG_EP5_FIFO		S3C_USBOTGREG(0x6000)
#define S3C_UDC_OTG_EP6_FIFO		S3C_USBOTGREG(0x7000)
#define S3C_UDC_OTG_EP7_FIFO		S3C_USBOTGREG(0x8000)
#define S3C_UDC_OTG_EP8_FIFO		S3C_USBOTGREG(0x9000)
#define S3C_UDC_OTG_EP9_FIFO		S3C_USBOTGREG(0xA000)
#define S3C_UDC_OTG_EP10_FIFO		S3C_USBOTGREG(0xB000)
#define S3C_UDC_OTG_EP11_FIFO		S3C_USBOTGREG(0xC000)
#define S3C_UDC_OTG_EP12_FIFO		S3C_USBOTGREG(0xD000)
#define S3C_UDC_OTG_EP13_FIFO		S3C_USBOTGREG(0xE000)
#define S3C_UDC_OTG_EP14_FIFO		S3C_USBOTGREG(0xF000)
#define S3C_UDC_OTG_EP15_FIFO		S3C_USBOTGREG(0x10000)

/*definitions related to CSR setting */

/* S3C_UDC_OTG_GOTGCTL */
#define B_SESSION_VALID			(0x1<<19)
#define A_SESSION_VALID			(0x1<<18)

/* S3C_UDC_OTG_GAHBCFG */
#define PTXFE_HALF			(0<<8)
#define PTXFE_ZERO			(1<<8)
#define NPTXFE_HALF			(0<<7)
#define NPTXFE_ZERO			(1<<7)
#define MODE_SLAVE			(0<<5)
#define MODE_DMA			(1<<5)
#define BURST_SINGLE			(0<<1)
#define BURST_INCR			(1<<1)
#define BURST_INCR4			(3<<1)
#define BURST_INCR8			(5<<1)
#define BURST_INCR16			(7<<1)
#define GBL_INT_UNMASK			(1<<0)
#define GBL_INT_MASK			(0<<0)

/* S3C_UDC_OTG_GRSTCTL */
#define AHB_MASTER_IDLE			(1u<<31)
#define CORE_SOFT_RESET			(0x1<<0)

/* S3C_UDC_OTG_GINTSTS/S3C_UDC_OTG_GINTMSK core interrupt register */
#define INT_RESUME			(1u<<31)
#define INT_DISCONN			(0x1<<29)
#define INT_CONN_ID_STS_CNG		(0x1<<28)
#define INT_OUT_EP			(0x1<<19)
#define INT_IN_EP			(0x1<<18)
#define INT_ENUMDONE			(0x1<<13)
#define INT_RESET			(0x1<<12)
#define INT_SUSPEND			(0x1<<11)
#define INT_EARLY_SUSPEND		(0x1<<10)
#define INT_NP_TX_FIFO_EMPTY		(0x1<<5)
#define INT_RX_FIFO_NOT_EMPTY		(0x1<<4)
#define INT_SOF				(0x1<<3)
#define INT_DEV_MODE			(0x0<<0)
#define INT_HOST_MODE			(0x1<<1)
#define INT_GOUTNakEff			(0x01<<7)
#define INT_GINNakEff			(0x01<<6)

#define FULL_SPEED_CONTROL_PKT_SIZE	8
#define FULL_SPEED_BULK_PKT_SIZE	64

#define HIGH_SPEED_CONTROL_PKT_SIZE	64
#define HIGH_SPEED_BULK_PKT_SIZE	512

#ifdef CONFIG_CPU_S5P6450
#define RX_FIFO_SIZE			(4096>>2)
#define NPTX_FIFO_START_ADDR		RX_FIFO_SIZE
#define NPTX_FIFO_SIZE			(4096>>2)
#define PTX_FIFO_SIZE			(1520>>2)
#else
#define RX_FIFO_SIZE			(4096>>2)
#define NPTX_FIFO_START_ADDR		RX_FIFO_SIZE
#define NPTX_FIFO_SIZE			(4096>>2)
#define PTX_FIFO_SIZE			(1024>>2)
#endif

/* Enumeration speed */
#define USB_HIGH_30_60MHZ		(0x0<<1)
#define USB_FULL_30_60MHZ		(0x1<<1)
#define USB_LOW_6MHZ			(0x2<<1)
#define USB_FULL_48MHZ			(0x3<<1)

/* S3C_UDC_OTG_GRXSTSP STATUS */
#define OUT_PKT_RECEIVED		(0x2<<17)
#define OUT_TRANSFER_COMPLELTED		(0x3<<17)
#define SETUP_TRANSACTION_COMPLETED	(0x4<<17)
#define SETUP_PKT_RECEIVED		(0x6<<17)
#define GLOBAL_OUT_NAK			(0x1<<17)

/* S3C_UDC_OTG_DCTL device control register */
#define NORMAL_OPERATION		(0x1<<0)
#define SOFT_DISCONNECT			(0x1<<1)
#define TEST_CONTROL_MASK		(0x7<<4)
#define TEST_J_MODE			(0x1<<4)
#define TEST_K_MODE			(0x2<<4)
#define TEST_SE0_NAK_MODE		(0x3<<4)
#define TEST_PACKET_MODE		(0x4<<4)
#define TEST_FORCE_ENABLE_MODE		(0x5<<4)

/* S3C_UDC_OTG_DAINT device all endpoint interrupt register */
#define DAINT_OUT_BIT			(16)
#define DAINT_MASK			(0xFFFF)

/* S3C_UDC_OTG_DIEPCTL0/DOEPCTL0 device control
   IN/OUT endpoint 0 control register */
#define DEPCTL_EPENA			(0x1<<31)
#define DEPCTL_EPDIS			(0x1<<30)
#define DEPCTL_SETD1PID			(0x1<<29)
#define DEPCTL_SET_ODD_FRM		(0x1<<29)
#define DEPCTL_SETD0PID			(0x1<<28)
#define DEPCTL_SET_EVEN_FRM		(0x1<<28)
#define DEPCTL_SNAK			(0x1<<27)
#define DEPCTL_CNAK			(0x1<<26)
#define DEPCTL_STALL			(0x1<<21)
#define DEPCTL_TYPE_BIT			(18)
#define DEPCTL_TXFNUM_BIT		(22)
#define DEPCTL_TXFNUM_MASK		(0xF<<22)
#define DEPCTL_TYPE_MASK		(0x3<<18)
#define DEPCTL_CTRL_TYPE		(0x0<<18)
#define DEPCTL_ISO_TYPE			(0x1<<18)
#define DEPCTL_BULK_TYPE		(0x2<<18)
#define DEPCTL_INTR_TYPE		(0x3<<18)
#define DEPCTL_NAKSTS                   (0x1<<17)
#define DEPCTL_DPID			(0x1<<16)
#define DEPCTL_EO_FRNUM			(0x1<<16)
#define DEPCTL_USBACTEP			(0x1<<15)
#define DEPCTL_NEXT_EP_BIT		(11)
#define DEPCTL_MPS_BIT			(0)
#define DEPCTL_MPS_MASK			(0x7FF)

#define DEPCTL0_MPS_64			(0x0<<0)
#define DEPCTL0_MPS_32			(0x1<<0)
#define DEPCTL0_MPS_16			(0x2<<0)
#define DEPCTL0_MPS_8			(0x3<<0)
#define DEPCTL_MPS_BULK_512		(512<<0)
#define DEPCTL_MPS_INT_MPS_16		(16<<0)

#define DIEPCTL0_NEXT_EP_BIT		(11)

/* S3C_UDC_OTG_DIEPCTLn/DOEPCTLn device control
   IN/OUT endpoint n control register */

/* S3C_UDC_OTG_DIEPMSK/DOEPMSK device
   IN/OUT endpoint common interrupt mask register */
/* S3C_UDC_OTG_DIEPINTn/DOEPINTn device
   IN/OUT endpoint interrupt register */
#define BACK2BACK_SETUP_RECEIVED	(0x1<<6)
#define INTKNEPMIS			(0x1<<5)
#define INTKN_TXFEMP			(0x1<<4)
#define NON_ISO_IN_EP_TIMEOUT		(0x1<<3)
#define CTRL_OUT_EP_SETUP_PHASE_DONE	(0x1<<3)
#define AHB_ERROR			(0x1<<2)
#define EPDISBLD			(0x1<<1)
#define TRANSFER_DONE			(0x1<<0)

/*DIEPTSIZ0 / DOEPTSIZ0 */

/* DEPTSIZ common bit */
#define DEPTSIZ_PKT_CNT_BIT		(19)
#define DEPTSIZ_XFER_SIZE_BIT		(0)

#define DEPTSIZ_SETUP_PKCNT_1		(1<<29)
#define DEPTSIZ_SETUP_PKCNT_2		(2<<29)
#define DEPTSIZ_SETUP_PKCNT_3		(3<<29)

#endif
