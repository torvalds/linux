/*
 * drivers/usb/sunxi_usb/include/sunxi_usb_bsp.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  __SUNXI_USB_BSP_H__
#define  __SUNXI_USB_BSP_H__

//-----------------------------------------------------------------------
//   读写寄存器, 8bit, 16bit, 32bit
//-----------------------------------------------------------------------

#define  USBC_Readb(reg)	                    (*(volatile unsigned char *)(reg))
#define  USBC_Readw(reg)	                    (*(volatile unsigned short *)(reg))
#define  USBC_Readl(reg)	                    (*(volatile unsigned long *)(reg))

#define  USBC_Writeb(value, reg)                (*(volatile unsigned char *)(reg) = (value))
#define  USBC_Writew(value, reg)	            (*(volatile unsigned short *)(reg) = (value))
#define  USBC_Writel(value, reg)	            (*(volatile unsigned long *)(reg) = (value))

#define  USBC_REG_test_bit_b(bp, reg)         	(USBC_Readb(reg) & (1 << (bp)))
#define  USBC_REG_test_bit_w(bp, reg)   	    (USBC_Readw(reg) & (1 << (bp)))
#define  USBC_REG_test_bit_l(bp, reg)   	    (USBC_Readl(reg) & (1 << (bp)))

#define  USBC_REG_set_bit_b(bp, reg) 			(USBC_Writeb((USBC_Readb(reg) | (1 << (bp))) , (reg)))
#define  USBC_REG_set_bit_w(bp, reg) 	 		(USBC_Writew((USBC_Readw(reg) | (1 << (bp))) , (reg)))
#define  USBC_REG_set_bit_l(bp, reg) 	 		(USBC_Writel((USBC_Readl(reg) | (1 << (bp))) , (reg)))

#define  USBC_REG_clear_bit_b(bp, reg)	 	 	(USBC_Writeb((USBC_Readb(reg) & (~ (1 << (bp)))) , (reg)))
#define  USBC_REG_clear_bit_w(bp, reg)	 	 	(USBC_Writew((USBC_Readw(reg) & (~ (1 << (bp)))) , (reg)))
#define  USBC_REG_clear_bit_l(bp, reg)	 	 	(USBC_Writel((USBC_Readl(reg) & (~ (1 << (bp)))) , (reg)))

//---------------------------------------------------------------
//   reg base
//---------------------------------------------------------------
#define  USBC0_BASE                 0x01c13000
#define  USBC1_BASE                 0x01c14000
#define  USBC2_BASE                 0x01c1E000

//-----------------------------------------------------------------------
//   reg offset
//-----------------------------------------------------------------------

#define  USBC_REG_o_FADDR		    0x0098
#define  USBC_REG_o_PCTL		    0x0040
#define  USBC_REG_o_INTTx		    0x0044
#define  USBC_REG_o_INTRx		    0x0046
#define  USBC_REG_o_INTTxE		    0x0048
#define  USBC_REG_o_INTRxE		    0x004A
#define  USBC_REG_o_INTUSB		    0x004C
#define  USBC_REG_o_INTUSBE         0x0050
#define  USBC_REG_o_FRNUM		    0x0054
#define  USBC_REG_o_EPIND		    0x0042
#define  USBC_REG_o_TMCTL		    0x007C

#define  USBC_REG_o_TXMAXP		    0x0080
#define  USBC_REG_o_CSR0		    0x0082
#define  USBC_REG_o_TXCSR		    0x0082
#define  USBC_REG_o_RXMAXP		    0x0084
#define  USBC_REG_o_RXCSR		    0x0086
#define  USBC_REG_o_COUNT0		    0x0088
#define  USBC_REG_o_RXCOUNT		    0x0088
#define  USBC_REG_o_EP0TYPE		    0x008C
#define  USBC_REG_o_TXTYPE		    0x008C
#define  USBC_REG_o_NAKLIMIT0	    0x008D
#define  USBC_REG_o_TXINTERVAL      0x008D
#define  USBC_REG_o_RXTYPE		    0x008E
#define  USBC_REG_o_RXINTERVAL	    0x008F

#define  USBC_REG_o_CONFIGDATA		0x00c0   //

#define  USBC_REG_o_EPFIFO0		    0x0000
#define  USBC_REG_o_EPFIFO1		    0x0004
#define  USBC_REG_o_EPFIFO2		    0x0008
#define  USBC_REG_o_EPFIFO3		    0x000C
#define  USBC_REG_o_EPFIFO4		    0x0010
#define  USBC_REG_o_EPFIFO5		    0x0014
#define  USBC_REG_o_EPFIFOx(n)	    (0x0000 + (n<<2))

#define  USBC_REG_o_DEVCTL		    0x0041

#define  USBC_REG_o_TXFIFOSZ	    0x0090
#define  USBC_REG_o_RXFIFOSZ	    0x0094
#define  USBC_REG_o_TXFIFOAD	    0x0092
#define  USBC_REG_o_RXFIFOAD	    0x0096

#define  USBC_REG_o_VEND0		    0x0043
#define  USBC_REG_o_VEND1		    0x007D
#define  USBC_REG_o_VEND3		    0x007E

//#define  USBC_REG_o_PHYCTL		0x006C
#define  USBC_REG_o_EPINFO		    0x0078
#define  USBC_REG_o_RAMINFO		    0x0079
#define  USBC_REG_o_LINKINFO	    0x007A
#define  USBC_REG_o_VPLEN		    0x007B
#define  USBC_REG_o_HSEOF		    0x007C
#define  USBC_REG_o_FSEOF		    0x007D
#define  USBC_REG_o_LSEOF		    0x007E

//new
#define  USBC_REG_o_FADDR0          0x0098
#define  USBC_REG_o_HADDR0          0x009A
#define  USBC_REG_o_HPORT0          0x009B
#define  USBC_REG_o_TXFADDRx 		0x0098
#define  USBC_REG_o_TXHADDRx		0x009A
#define  USBC_REG_o_TXHPORTx		0x009B
#define  USBC_REG_o_RXFADDRx		0x009C
#define  USBC_REG_o_RXHADDRx		0x009E
#define  USBC_REG_o_RXHPORTx		0x009F


#define  USBC_REG_o_RPCOUNT			0x008A

//new
#define  USBC_REG_o_ISCR            0x0400
#define  USBC_REG_o_PHYCTL          0x0404
#define  USBC_REG_o_PHYBIST         0x0408
#define  USBC_REG_o_PHYTUNE         0x040c


//-----------------------------------------------------------------------
//   registers
//-----------------------------------------------------------------------

#define  USBC_REG_FADDR(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_FADDR		)
#define  USBC_REG_PCTL(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_PCTL			)
#define  USBC_REG_INTTx(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_INTTx		)
#define  USBC_REG_INTRx(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_INTRx		)
#define  USBC_REG_INTTxE(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_INTTxE     	)
#define  USBC_REG_INTRxE(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_INTRxE     	)
#define  USBC_REG_INTUSB(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_INTUSB     	)
#define  USBC_REG_INTUSBE(usbc_base_addr)           ((usbc_base_addr) + USBC_REG_o_INTUSBE    	)
#define  USBC_REG_FRNUM(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_FRNUM      	)
#define  USBC_REG_EPIND(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_EPIND      	)
#define  USBC_REG_TMCTL(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_TMCTL      	)
#define  USBC_REG_TXMAXP(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_TXMAXP     	)

#define  USBC_REG_CSR0(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_CSR0       	)
#define  USBC_REG_TXCSR(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_TXCSR      	)

#define  USBC_REG_RXMAXP(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_RXMAXP     	)
#define  USBC_REG_RXCSR(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_RXCSR      	)

#define  USBC_REG_COUNT0(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_COUNT0     	)
#define  USBC_REG_RXCOUNT(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_RXCOUNT    	)

#define  USBC_REG_EP0TYPE(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EP0TYPE		)
#define  USBC_REG_TXTYPE(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_TXTYPE     	)

#define  USBC_REG_NAKLIMIT0(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_NAKLIMIT0  	)
#define  USBC_REG_TXINTERVAL(usbc_base_addr)        ((usbc_base_addr) + USBC_REG_o_TXINTERVAL	)

#define  USBC_REG_RXTYPE(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_RXTYPE		)
#define  USBC_REG_RXINTERVAL(usbc_base_addr)	    ((usbc_base_addr) + USBC_REG_o_RXINTERVAL	)
#define  USBC_REG_CONFIGDATA(usbc_base_addr)	    ((usbc_base_addr) + USBC_REG_o_CONFIGDATA	)
#define  USBC_REG_EPFIFO0(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EPFIFO0		)
#define  USBC_REG_EPFIFO1(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EPFIFO1		)
#define  USBC_REG_EPFIFO2(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EPFIFO2		)
#define  USBC_REG_EPFIFO3(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EPFIFO3		)
#define  USBC_REG_EPFIFO4(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EPFIFO4		)
#define  USBC_REG_EPFIFO5(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EPFIFO5		)
#define  USBC_REG_EPFIFOx(usbc_base_addr, n)	    ((usbc_base_addr) + USBC_REG_o_EPFIFOx(n)	)
#define  USBC_REG_DEVCTL(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_DEVCTL		)
#define  USBC_REG_TXFIFOSZ(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_TXFIFOSZ		)
#define  USBC_REG_RXFIFOSZ(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_RXFIFOSZ		)
#define  USBC_REG_TXFIFOAD(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_TXFIFOAD		)
#define  USBC_REG_RXFIFOAD(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_RXFIFOAD		)
#define  USBC_REG_VEND0(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_VEND0		)
#define  USBC_REG_VEND1(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_VEND1		)
#define  USBC_REG_EPINFO(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_EPINFO		)
#define  USBC_REG_RAMINFO(usbc_base_addr)		    ((usbc_base_addr) + USBC_REG_o_RAMINFO		)
#define  USBC_REG_LINKINFO(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_LINKINFO		)
#define  USBC_REG_VPLEN(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_VPLEN		)
#define  USBC_REG_HSEOF(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_HSEOF		)
#define  USBC_REG_FSEOF(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_FSEOF		)
#define  USBC_REG_LSEOF(usbc_base_addr)		        ((usbc_base_addr) + USBC_REG_o_LSEOF		)

#define  USBC_REG_FADDR0(usbc_base_addr)            ((usbc_base_addr) + USBC_REG_o_FADDR0		)
#define  USBC_REG_HADDR0(usbc_base_addr)            ((usbc_base_addr) + USBC_REG_o_HADDR0		)
#define  USBC_REG_HPORT0(usbc_base_addr)            ((usbc_base_addr) + USBC_REG_o_HPORT0		)

#define  USBC_REG_TXFADDRx(usbc_base_addr, n)		((usbc_base_addr) + USBC_REG_o_TXFADDRx		)
#define  USBC_REG_TXHADDRx(usbc_base_addr, n)		((usbc_base_addr) + USBC_REG_o_TXHADDRx		)
#define  USBC_REG_TXHPORTx(usbc_base_addr, n)		((usbc_base_addr) + USBC_REG_o_TXHPORTx		)
#define  USBC_REG_RXFADDRx(usbc_base_addr, n)		((usbc_base_addr) + USBC_REG_o_RXFADDRx		)
#define  USBC_REG_RXHADDRx(usbc_base_addr, n)		((usbc_base_addr) + USBC_REG_o_RXHADDRx		)
#define  USBC_REG_RXHPORTx(usbc_base_addr, n)		((usbc_base_addr) + USBC_REG_o_RXHPORTx		)

#define  USBC_REG_RPCOUNTx(usbc_base_addr, n)	    ((usbc_base_addr) + USBC_REG_o_RPCOUNT		)

#define  USBC_REG_ISCR(usbc_base_addr)	    	    ((usbc_base_addr) + USBC_REG_o_ISCR			)
#define  USBC_REG_PHYCTL(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_PHYCTL		)
#define  USBC_REG_PHYBIST(usbc_base_addr)	        ((usbc_base_addr) + USBC_REG_o_PHYBIST		)
#define  USBC_REG_PHYTUNE(usbc_base_addr)           ((usbc_base_addr) + USBC_REG_o_PHYTUNE		)

//-----------------------------------------------------------------------
//  registers extern
//-----------------------------------------------------------------------
#define  USBC_REG_EX_USB_EPFIFOn(usbc_base_addr)    ((usbc_base_addr) + (0x00 + (n << 2)))
#define  USBC_REG_EX_USB_GCS(usbc_base_addr)        ((usbc_base_addr) + 0x0040)
#define  USBC_REG_EX_USB_EPINTF(usbc_base_addr)     ((usbc_base_addr) + 0x0044)
#define  USBC_REG_EX_USB_EPINTE(usbc_base_addr)     ((usbc_base_addr) + 0x0048)
#define  USBC_REG_EX_USB_BUSINTF(usbc_base_addr)    ((usbc_base_addr) + 0x004C)
#define  USBC_REG_EX_USB_BUSINTE(usbc_base_addr)    ((usbc_base_addr) + 0x0050)
#define  USBC_REG_EX_USB_FNUM(usbc_base_addr)       ((usbc_base_addr) + 0x0054)
#define  USBC_REG_EX_USB_TESTC(usbc_base_addr)      ((usbc_base_addr) + 0x007C)

#define  USBC_REG_EX_USB_CSR0(usbc_base_addr)       ((usbc_base_addr) + 0x0080)
#define  USBC_REG_EX_USB_TXCSR(usbc_base_addr)      ((usbc_base_addr) + 0x0080)
#define  USBC_REG_EX_USB_RXCSR(usbc_base_addr)      ((usbc_base_addr) + 0x0084)
#define  USBC_REG_EX_USB_COUNT0(usbc_base_addr)     ((usbc_base_addr) + 0x0088)
#define  USBC_REG_EX_USB_RXCOUNT(usbc_base_addr)    ((usbc_base_addr) + 0x0088)
#define  USBC_REG_EX_USB_ATTR0(usbc_base_addr)      ((usbc_base_addr) + 0x008C)
#define  USBC_REG_EX_USB_EPATTR(usbc_base_addr)     ((usbc_base_addr) + 0x008C)
#define  USBC_REG_EX_USB_TXFIFO(usbc_base_addr)     ((usbc_base_addr) + 0x0090)
#define  USBC_REG_EX_USB_RXFIFO(usbc_base_addr)     ((usbc_base_addr) + 0x0094)
#define  USBC_REG_EX_USB_FADDR(usbc_base_addr)      ((usbc_base_addr) + 0x0098)
#define  USBC_REG_EX_USB_TXFADDR(usbc_base_addr)    ((usbc_base_addr) + 0x0098)
#define  USBC_REG_EX_USB_RXFADDR(usbc_base_addr)    ((usbc_base_addr) + 0x009C)

#define  USBC_REG_EX_USB_CONFIGINFO(usbc_base_addr) ((usbc_base_addr) + 0x00C0)
#define  USBC_REG_EX_USB_LINKTIM(usbc_base_addr)    ((usbc_base_addr) + 0x00C4)
#define  USBC_REG_EX_USB_OTGTIM(usbc_base_addr)     ((usbc_base_addr) + 0x00C8)

#define  USBC_REG_EX_USB_ISCR(usbc_base_addr)       ((usbc_base_addr) + 0x0400)
#define  USBC_REG_EX_USB_PHYCTL(usbc_base_addr)     ((usbc_base_addr) + 0x0404)
#define  USBC_REG_EX_USB_PHYBIST(usbc_base_addr)    ((usbc_base_addr) + 0x0408)


//-----------------------------------------------------------------------
//   bit position
//-----------------------------------------------------------------------

/* USB Power Control for Host only  */
#define  USBC_BP_POWER_H_HIGH_SPEED_EN			5
#define  USBC_BP_POWER_H_HIGH_SPEED_FLAG		4
#define  USBC_BP_POWER_H_RESET					3
#define  USBC_BP_POWER_H_RESUME					2
#define  USBC_BP_POWER_H_SUSPEND				1
#define  USBC_BP_POWER_H_SUEPEND_EN				0

/* USB Power Control for device only  */
#define  USBC_BP_POWER_D_ISO_UPDATE_EN			7
#define  USBC_BP_POWER_D_SOFT_CONNECT			6
#define  USBC_BP_POWER_D_HIGH_SPEED_EN			5
#define  USBC_BP_POWER_D_HIGH_SPEED_FLAG		4
#define  USBC_BP_POWER_D_RESET_FLAG				3
#define  USBC_BP_POWER_D_RESUME					2
#define  USBC_BP_POWER_D_SUSPEND				1
#define  USBC_BP_POWER_D_ENABLE_SUSPENDM		0

/* interrupt flags for ep0 and the Tx ep1~4 */
#define  USBC_BP_INTTx_FLAG_EP5				    5
#define  USBC_BP_INTTx_FLAG_EP4				    4
#define  USBC_BP_INTTx_FLAG_EP3				    3
#define  USBC_BP_INTTx_FLAG_EP2		    		2
#define  USBC_BP_INTTx_FLAG_EP1  				1
#define  USBC_BP_INTTx_FLAG_EP0					0

/* interrupt flags for Rx ep1~4 */
#define  USBC_BP_INTRx_FLAG_EP5				    5
#define  USBC_BP_INTRx_FLAG_EP4				    4
#define  USBC_BP_INTRx_FLAG_EP3				    3
#define  USBC_BP_INTRx_FLAG_EP2				    2
#define  USBC_BP_INTRx_FLAG_EP1				    1

/* interrupt enable for Tx ep0~4 */
#define  USBC_BP_INTTxE_EN_EP5				    5
#define  USBC_BP_INTTxE_EN_EP4				    4
#define  USBC_BP_INTTxE_EN_EP3				    3
#define  USBC_BP_INTTxE_EN_EP2				    2
#define  USBC_BP_INTTxE_EN_EP1				    1
#define  USBC_BP_INTTxE_EN_EP0					0

/* interrupt enable for Rx ep1~4 */
#define  USBC_BP_INTRxE_EN_EP5				    5
#define  USBC_BP_INTRxE_EN_EP4				    4
#define  USBC_BP_INTRxE_EN_EP3				    3
#define  USBC_BP_INTRxE_EN_EP2			    	2
#define  USBC_BP_INTRxE_EN_EP1  		    	1

/* USB interrupt */
#define  USBC_BP_INTUSB_VBUS_ERROR				7
#define  USBC_BP_INTUSB_SESSION_REQ				6
#define  USBC_BP_INTUSB_DISCONNECT				5
#define  USBC_BP_INTUSB_CONNECT					4
#define  USBC_BP_INTUSB_SOF						3
#define  USBC_BP_INTUSB_RESET					2
#define  USBC_BP_INTUSB_RESUME					1
#define  USBC_BP_INTUSB_SUSPEND					0

/* USB interrupt enable */
#define  USBC_BP_INTUSBE_EN_VBUS_ERROR			7
#define  USBC_BP_INTUSBE_EN_SESSION_REQ			6
#define  USBC_BP_INTUSBE_EN_DISCONNECT			5
#define  USBC_BP_INTUSBE_EN_CONNECT				4
#define  USBC_BP_INTUSBE_EN_SOF					3
#define  USBC_BP_INTUSBE_EN_RESET				2
#define  USBC_BP_INTUSBE_EN_RESUME				1
#define  USBC_BP_INTUSBE_EN_SUSPEND				0

/* Test Mode Control */
#define  USBC_BP_TMCTL_FORCE_HOST               7
#define  USBC_BP_TMCTL_FIFO_ACCESS              6
#define  USBC_BP_TMCTL_FORCE_FS                 5
#define  USBC_BP_TMCTL_FORCE_HS                 4
#define  USBC_BP_TMCTL_TEST_PACKET              3
#define  USBC_BP_TMCTL_TEST_K                   2
#define  USBC_BP_TMCTL_TEST_J                   1
#define  USBC_BP_TMCTL_TEST_SE0_NAK             0

/* Tx Max packet */
#define  USBC_BP_TXMAXP_PACKET_COUNT            11
#define  USBC_BP_TXMAXP_MAXIMUM_PAYLOAD         0

/* Control and Status Register for ep0 for Host only */
#define  USBC_BP_CSR0_H_DisPing 				11
#define  USBC_BP_CSR0_H_FlushFIFO				8
#define  USBC_BP_CSR0_H_NAK_Timeout				7
#define  USBC_BP_CSR0_H_StatusPkt				6
#define  USBC_BP_CSR0_H_ReqPkt					5
#define  USBC_BP_CSR0_H_Error					4
#define  USBC_BP_CSR0_H_SetupPkt				3
#define  USBC_BP_CSR0_H_RxStall					2
#define  USBC_BP_CSR0_H_TxPkRdy					1
#define  USBC_BP_CSR0_H_RxPkRdy					0

/* Control and Status Register for ep0 for device only */
#define  USBC_BP_CSR0_D_FLUSH_FIFO				8
#define  USBC_BP_CSR0_D_SERVICED_SETUP_END		7
#define  USBC_BP_CSR0_D_SERVICED_RX_PKT_READY   6
#define  USBC_BP_CSR0_D_SEND_STALL				5
#define  USBC_BP_CSR0_D_SETUP_END				4
#define  USBC_BP_CSR0_D_DATA_END				3
#define  USBC_BP_CSR0_D_SENT_STALL				2
#define  USBC_BP_CSR0_D_TX_PKT_READY			1
#define  USBC_BP_CSR0_D_RX_PKT_READY			0

/* Tx ep Control and Status Register for Host only */
#define  USBC_BP_TXCSR_H_AUTOSET				15
#define  USBC_BP_TXCSR_H_RESERVED				14
#define  USBC_BP_TXCSR_H_MODE				    13
#define  USBC_BP_TXCSR_H_DMA_REQ_EN			 	12
#define  USBC_BP_TXCSR_H_FORCE_DATA_TOGGLE		11
#define  USBC_BP_TXCSR_H_DMA_REQ_MODE			10
#define  USBC_BP_TXCSR_H_DATA_TOGGLE_WR_EN      9
#define  USBC_BP_TXCSR_H_DATA_TOGGLE            8
#define  USBC_BP_TXCSR_H_NAK_TIMEOUT			7
#define  USBC_BP_TXCSR_H_CLEAR_DATA_TOGGLE		6
#define  USBC_BP_TXCSR_H_TX_STALL				5
#define  USBC_BP_TXCSR_H_FLUSH_FIFO				3
#define  USBC_BP_TXCSR_H_ERROR					2
#define  USBC_BP_TXCSR_H_FIFO_NOT_EMPTY 		1
#define  USBC_BP_TXCSR_H_TX_READY				0

#define  USBC_TXCSR_H_WZC_BITS                  ((1 << USBC_BP_TXCSR_H_NAK_TIMEOUT) | (1 << USBC_BP_TXCSR_H_TX_STALL) \
                                                | (1 << USBC_BP_TXCSR_H_ERROR) | (1 << USBC_BP_TXCSR_H_FIFO_NOT_EMPTY))

/* Tx ep Control and Status Register for Device only */
#define  USBC_BP_TXCSR_D_AUTOSET				15
#define  USBC_BP_TXCSR_D_ISO					14
#define  USBC_BP_TXCSR_D_MODE					13
#define  USBC_BP_TXCSR_D_DMA_REQ_EN				12
#define  USBC_BP_TXCSR_D_FORCE_DATA_TOGGLE		11
#define  USBC_BP_TXCSR_D_DMA_REQ_MODE			10
#define  USBC_BP_TXCSR_D_INCOMPLETE				7
#define  USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE		6
#define  USBC_BP_TXCSR_D_SENT_STALL				5
#define  USBC_BP_TXCSR_D_SEND_STALL				4
#define  USBC_BP_TXCSR_D_FLUSH_FIFO				3
#define  USBC_BP_TXCSR_D_UNDER_RUN				2
#define  USBC_BP_TXCSR_D_FIFO_NOT_EMPTY 		1
#define  USBC_BP_TXCSR_D_TX_READY				0

/* Rx Max Packet */
#define  USBC_BP_RXMAXP_PACKET_COUNT            11
#define  USBC_BP_RXMAXP_MAXIMUM_PAYLOAD         0

/* Rx ep Control and Status Register for Host only */
#define  USBC_BP_RXCSR_H_AUTO_CLEAR			    15
#define  USBC_BP_RXCSR_H_AUTO_REQ			    14
#define  USBC_BP_RXCSR_H_DMA_REQ_EN			    13
#define  USBC_BP_RXCSR_H_DISNYET                12  /* dis nyet */
#define  USBC_BP_RXCSR_H_PID_ERROR			    12  /* pid error */
#define  USBC_BP_RXCSR_H_DMA_REQ_MODE		    11
#define  USBC_BP_RXCSR_H_DATA_TOGGLE_WR_EN      10
#define  USBC_BP_RXCSR_H_DATA_TOGGLE            9
#define  USBC_BP_RXCSR_H_INCOMPLETE			    8
#define  USBC_BP_RXCSR_H_CLEAR_DATA_TOGGLE	    7
#define  USBC_BP_RXCSR_H_RX_STALL			    6
#define  USBC_BP_RXCSR_H_REQ_PACKET			    5
#define  USBC_BP_RXCSR_H_FLUSH_FIFO			    4
#define  USBC_BP_RXCSR_H_DATA_ERR               3   /* iso */
#define  USBC_BP_RXCSR_H_NAK_TIMEOUT		    3   /* bulk */
#define  USBC_BP_RXCSR_H_ERROR				    2
#define  USBC_BP_RXCSR_H_FIFO_FULL			    1
#define  USBC_BP_RXCSR_H_RX_PKT_READY		    0

#define  USBC_RXCSR_H_WZC_BITS                  ((1 << USBC_BP_RXCSR_H_RX_STALL) | (1 << USBC_BP_RXCSR_H_ERROR) \
                                                | (1 << USBC_BP_RXCSR_H_DATA_ERR) | (1 << USBC_BP_RXCSR_H_RX_PKT_READY))

/* Rx ep Control and Status Register for Device only */
#define  USBC_BP_RXCSR_D_AUTO_CLEAR			    15
#define  USBC_BP_RXCSR_D_ISO				    14
#define  USBC_BP_RXCSR_D_DMA_REQ_EN			    13
#define  USBC_BP_RXCSR_D_DISABLE_NYET		    12
#define  USBC_BP_RXCSR_D_DMA_REQ_MODE		    11

#define  USBC_BP_RXCSR_D_INCOMPLETE			    8
#define  USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE	    7
#define  USBC_BP_RXCSR_D_SENT_STALL			    6
#define  USBC_BP_RXCSR_D_SEND_STALL			    5
#define  USBC_BP_RXCSR_D_FLUSH_FIFO			    4
#define  USBC_BP_RXCSR_D_DATA_ERROR			    3
#define  USBC_BP_RXCSR_D_OVERRUN			    2
#define  USBC_BP_RXCSR_D_FIFO_FULL			    1
#define  USBC_BP_RXCSR_D_RX_PKT_READY		    0

/* Tx Type Register for host only */
#define  USBC_BP_TXTYPE_SPEED	                6              //new
#define  USBC_BP_TXTYPE_PROROCOL	            4
#define  USBC_BP_TXTYPE_TARGET_EP_NUM           0

/* Rx Type Register for host only */
#define  USBC_BP_RXTYPE_SPEED		            6              //new
#define  USBC_BP_RXTYPE_PROROCOL	            4
#define  USBC_BP_RXTYPE_TARGET_EP_NUM           0

/* Core Configueation */
#define  USBC_BP_CONFIGDATA_MPRXE               7
#define  USBC_BP_CONFIGDATA_MPTXE               6
#define  USBC_BP_CONFIGDATA_BIGENDIAN		    5
#define  USBC_BP_CONFIGDATA_HBRXE			    4
#define  USBC_BP_CONFIGDATA_HBTXE			    3
#define  USBC_BP_CONFIGDATA_DYNFIFO_SIZING	    2
#define  USBC_BP_CONFIGDATA_SOFTCONE		    1
#define  USBC_BP_CONFIGDATA_UTMI_DATAWIDTH	    0

/* OTG Device Control */
#define  USBC_BP_DEVCTL_B_DEVICE			    7
#define  USBC_BP_DEVCTL_FS_DEV				    6
#define  USBC_BP_DEVCTL_LS_DEV				    5

#define  USBC_BP_DEVCTL_VBUS				    3
#define  USBC_BP_DEVCTL_HOST_MODE			    2
#define  USBC_BP_DEVCTL_HOST_REQ			    1
#define  USBC_BP_DEVCTL_SESSION				    0

/* Tx EP FIFO size control */
#define  USBC_BP_TXFIFOSZ_DPB				    4
#define  USBC_BP_TXFIFOSZ_SZ				    0

/* Rx EP FIFO size control */
#define  USBC_BP_RXFIFOSZ_DPB				    4
#define  USBC_BP_RXFIFOSZ_SZ				    0

/* vendor0 */
#define  USBC_BP_VEND0_DRQ_SEL				    1
#define  USBC_BP_VEND0_BUS_SEL				    0

/* hub address */
#define  USBC_BP_HADDR_MULTI_TT					7

/* Interface Status and Control */
#define  USBC_BP_ISCR_VBUS_VALID_FROM_DATA		30
#define  USBC_BP_ISCR_VBUS_VALID_FROM_VBUS		29
#define  USBC_BP_ISCR_EXT_ID_STATUS				28
#define  USBC_BP_ISCR_EXT_DM_STATUS				27
#define  USBC_BP_ISCR_EXT_DP_STATUS				26
#define  USBC_BP_ISCR_MERGED_VBUS_STATUS		25
#define  USBC_BP_ISCR_MERGED_ID_STATUS			24

#define  USBC_BP_ISCR_ID_PULLUP_EN				17
#define  USBC_BP_ISCR_DPDM_PULLUP_EN			16
#define  USBC_BP_ISCR_FORCE_ID					14
#define  USBC_BP_ISCR_FORCE_VBUS_VALID			12
#define  USBC_BP_ISCR_VBUS_VALID_SRC			10

#define  USBC_BP_ISCR_HOSC_EN                 	7
#define  USBC_BP_ISCR_VBUS_CHANGE_DETECT      	6
#define  USBC_BP_ISCR_ID_CHANGE_DETECT        	5
#define  USBC_BP_ISCR_DPDM_CHANGE_DETECT      	4
#define  USBC_BP_ISCR_IRQ_ENABLE              	3
#define  USBC_BP_ISCR_VBUS_CHANGE_DETECT_EN   	2
#define  USBC_BP_ISCR_ID_CHANGE_DETECT_EN     	1
#define  USBC_BP_ISCR_DPDM_CHANGE_DETECT_EN   	0

//-----------------------------------------------------------------------
//   自定义
//-----------------------------------------------------------------------

/* usb资源描述 */
#define  USBC_MAX_CTL_NUM		3
#define  USBC_MAX_EP_NUM      	6   	/* 能够支持的最大ep号, ep0~5 */
#define  USBC0_MAX_FIFO_SIZE   	(8 * 1024)
#define  USBC_EP0_FIFOSIZE	  	64	/* This is non-configurable */

/* usb OTG mode */
#define  USBC_OTG_HOST        0
#define  USBC_OTG_DEVICE      1

/* usb device type */
#define  USBC_DEVICE_HSDEV    0
#define  USBC_DEVICE_FSDEV    1
#define  USBC_DEVICE_LSDEV    2

/*  usb transfer type  */
#define  USBC_TS_TYPE_IDLE    0
#define  USBC_TS_TYPE_CTRL    1
#define  USBC_TS_TYPE_ISO     2
#define  USBC_TS_TYPE_INT     3
#define  USBC_TS_TYPE_BULK    4

/*  usb transfer mode  */
#define  USBC_TS_MODE_UNKOWN  0
#define  USBC_TS_MODE_LS      1
#define  USBC_TS_MODE_FS      2
#define  USBC_TS_MODE_HS      3

/* usb Vbus status */
#define  USBC_VBUS_STATUS_BELOW_SESSIONEND                 0
#define  USBC_VBUS_STATUS_ABOVE_SESSIONEND_BELOW_AVALID    1
#define  USBC_VBUS_STATUS_ABOVE_AVALID_BELOW_VBUSVALID     2
#define  USBC_VBUS_STATUS_ABOVE_VBUSVALID                  3

/* usb io type */
#define  USBC_IO_TYPE_PIO    		0
#define  USBC_IO_TYPE_DMA    		1

/* usb ep type */
#define  USBC_EP_TYPE_IDLE    		0
#define  USBC_EP_TYPE_EP0    		1
#define  USBC_EP_TYPE_TX     		2
#define  USBC_EP_TYPE_RX     		3

/* usb id type */
#define  USBC_ID_TYPE_DISABLE      	0
#define  USBC_ID_TYPE_HOST         	1
#define  USBC_ID_TYPE_DEVICE       	2

/* usb vbus valid type */
#define  USBC_VBUS_TYPE_DISABLE    	0
#define  USBC_VBUS_TYPE_LOW       	1
#define  USBC_VBUS_TYPE_HIGH       	2

/* usb a valid source */
#define  USBC_A_VALID_SOURCE_UTMI_AVALID	0
#define  USBC_A_VALID_SOURCE_UTMI_VBUS    	1

/* usb device switch */
#define  USBC_DEVICE_SWITCH_OFF    0
#define  USBC_DEVICE_SWITCH_ON     1

/* usb fifo config mode */
#define  USBC_FIFO_MODE_4K    0
#define  USBC_FIFO_MODE_8K    1

/*
 **************************************************
 *  usb interrupt mask
 *
 **************************************************
 */

/* interrupt flags for ep0 and the Tx ep1~4 */
#define  USBC_INTTx_FLAG_EP5				    (1 << USBC_BP_INTTx_FLAG_EP5)
#define  USBC_INTTx_FLAG_EP4				    (1 << USBC_BP_INTTx_FLAG_EP4)
#define  USBC_INTTx_FLAG_EP3				    (1 << USBC_BP_INTTx_FLAG_EP3)
#define  USBC_INTTx_FLAG_EP2		    		(1 << USBC_BP_INTTx_FLAG_EP2)
#define  USBC_INTTx_FLAG_EP1  				    (1 << USBC_BP_INTTx_FLAG_EP1)
#define  USBC_INTTx_FLAG_EP0					(1 << USBC_BP_INTTx_FLAG_EP0)

/* interrupt flags for Rx ep1~4 */
#define  USBC_INTRx_FLAG_EP5				    (1 << USBC_BP_INTRx_FLAG_EP5)
#define  USBC_INTRx_FLAG_EP4				    (1 << USBC_BP_INTRx_FLAG_EP4)
#define  USBC_INTRx_FLAG_EP3				    (1 << USBC_BP_INTRx_FLAG_EP3)
#define  USBC_INTRx_FLAG_EP2				    (1 << USBC_BP_INTRx_FLAG_EP2)
#define  USBC_INTRx_FLAG_EP1				    (1 << USBC_BP_INTRx_FLAG_EP1)

/* USB interrupt */
#define  USBC_INTUSB_VBUS_ERROR				    (1 << USBC_BP_INTUSB_VBUS_ERROR)
#define  USBC_INTUSB_SESSION_REQ				(1 << USBC_BP_INTUSB_SESSION_REQ)
#define  USBC_INTUSB_DISCONNECT				    (1 << USBC_BP_INTUSB_DISCONNECT)
#define  USBC_INTUSB_CONNECT					(1 << USBC_BP_INTUSB_CONNECT)
#define  USBC_INTUSB_SOF						(1 << USBC_BP_INTUSB_SOF)
#define  USBC_INTUSB_RESET					    (1 << USBC_BP_INTUSB_RESET)
#define  USBC_INTUSB_RESUME					    (1 << USBC_BP_INTUSB_RESUME)
#define  USBC_INTUSB_SUSPEND					(1 << USBC_BP_INTUSB_SUSPEND)


//-----------------------------------------------------------------------
//   USB host 操作部分
//-----------------------------------------------------------------------
void USBC_Host_SetFunctionAddress_Deafult(__hdle hUSB, __u32 ep_type, __u32 ep_index);
void USBC_Host_SetFunctionAddress(__hdle hUSB,
								  __u32 EpType,
								  __u32 EpIndex,
								  __u32 FunctionAdress,
								  __u32 MultiTT,
								  __u32 HubAddress,
								  __u32 HubPortNumber);
void USBC_Host_SetHubAddress_Deafult(__hdle hUSB, __u32 ep_type, __u32 ep_index);
void USBC_Host_SetHubAddress(__hdle hUSB, __u32 ep_type, __u32 ep_index, __u32 is_mutli_tt, __u8 address);
void USBC_Host_SetHPortAddress_Deafult(__hdle hUSB, __u32 ep_type, __u32 ep_index);
void USBC_Host_SetHPortAddress(__hdle hUSB, __u32 ep_type, __u32 ep_index, __u8 address);

__u32 USBC_Host_QueryTransferMode(__hdle hUSB);
void USBC_Host_ConfigTransferMode(__hdle hUSB, __u32 speed_mode);

void USBC_Host_ResetPort(__hdle hUSB);
void USBC_Host_ClearResetPortFlag(__hdle hUSB);
void USBC_Host_RusumePort(__hdle hUSB);
void USBC_Host_ClearRusumePortFlag(__hdle hUSB);
void USBC_Host_SuspendPort(__hdle hUSB);
__u32 USBC_Host_QueryPowerStatus(__hdle hUSB);

void USBC_Host_EnablePing(__hdle hUSB);
void USBC_Host_DisablePing(__hdle hUSB);
__u32 USBC_Host_IsReqPktSet(__hdle hUSB, __u32 ep_type);
void USBC_Host_StartInToken(__hdle hUSB, __u32 ep_type);
void USBC_Host_StopInToken(__hdle hUSB, __u32 ep_type);
void USBC_Host_SendCtrlStatus(__hdle hUSB, __u32 is_after_in);
void USBC_Host_SendSetupPkt(__hdle hUSB);

void USBC_Host_StartSession(__hdle hUSB);
void USBC_Host_EndSession(__hdle hUSB);
void USBC_Host_ConfigRqPktCount(__hdle hUSB, __u32 ep_index, __u32 RqPktCount);
void USBC_Host_ClearRqPktCount(__hdle hUSB, __u32 ep_index);

__u32 USBC_Host_PeripheralType(__hdle hUSB);

void USBC_Host_FlushFifo(__hdle hUSB, __u32 ep_type);
void USBC_Host_ConfigEp_Default(__hdle hUSB, __u32 ep_type);
void USBC_Host_ConfigEp(__hdle hUSB, __u32 ep_type, __u32 ep_index, __u32 ts_mode, __u32 ts_type, __u32 is_double_fifo, __u32 ep_MaxPkt, __u32 interval);
void USBC_Host_ConfigEpDma(__hdle hUSB, __u32 ep_type);
void USBC_Host_ClearEpDma(__hdle hUSB, __u32 ep_type);

__u32 USBC_Host_IsEpStall(__hdle hUSB, __u32 ep_type);
void USBC_Host_ClearEpStall(__hdle hUSB, __u32 ep_type);
__u32 USBC_Host_IsEpNakTimeOut(__hdle hUSB, __u32 ep_type);
void USBC_Host_ClearEpNakTimeOut(__hdle hUSB, __u32 ep_type);
__u32 USBC_Host_IsEpError(__hdle hUSB, __u32 ep_type);
void USBC_Host_ClearEpError(__hdle hUSB, __u32 ep_type);
void USBC_Host_ClearEpCSR(__hdle hUSB, __u32 ep_type);

__u32 USBC_Host_IsReadDataReady(__hdle hUSB, __u32 ep_type);
__u32 USBC_Host_IsWriteDataReady(__hdle hUSB, __u32 ep_type);
__s32 USBC_Host_ReadDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete);
__s32 USBC_Host_WriteDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete);


//-----------------------------------------------------------------------
//   USB device 操作部分
//-----------------------------------------------------------------------
void USBC_Dev_SetAddress_default(__hdle hUSB);
void USBC_Dev_SetAddress(__hdle hUSB, __u8 address);

__u32 USBC_Dev_QueryTransferMode(__hdle hUSB);
void USBC_Dev_ConfigTransferMode(__hdle hUSB, __u8 ts_type, __u8 speed_mode);
void USBC_Dev_ConectSwitch(__hdle hUSB, __u32 is_on);
__u32 USBC_Dev_QueryPowerStatus(__hdle hUSB);

__s32 USBC_Dev_ConfigEp(__hdle hUSB, __u32 ts_type, __u32 ep_type, __u32 is_double_fifo, __u32 ep_MaxPkt);
__s32 USBC_Dev_ConfigEp_Default(__hdle hUSB, __u32 ep_type);
__s32 USBC_Dev_ConfigEpDma(__hdle hUSB, __u32 ep_type);
__s32 USBC_Dev_ClearEpDma(__hdle hUSB, __u32 ep_type);

__s32 USBC_Dev_IsEpStall(__hdle hUSB, __u32 ep_type);
__s32 USBC_Dev_EpSendStall(__hdle hUSB, __u32 ep_type);
__s32 USBC_Dev_EpClearStall(__hdle hUSB, __u32 ep_type);

__u32 USBC_Dev_Ctrl_IsSetupEnd(__hdle hUSB);
void USBC_Dev_Ctrl_ClearSetupEnd(__hdle hUSB);

__u32 USBC_Dev_IsReadDataReady(__hdle hUSB, __u32 ep_type);
__u32 USBC_Dev_IsWriteDataReady(__hdle hUSB, __u32 ep_type);
__s32 USBC_Dev_WriteDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete);
__s32 USBC_Dev_ReadDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete);


//-----------------------------------------------------------------------
//   USB 公共操作部分
//-----------------------------------------------------------------------

/* USB传输类型选择, 读写数据等 */
void USBC_OTG_SelectMode(__hdle hUSB, __u32 mode);

__u32 USBC_ReadLenFromFifo(__hdle hUSB, __u32 ep_type);
__u32 USBC_WritePacket(__hdle hUSB, __u32 fifo, __u32 cnt, void *buff);
__u32 USBC_ReadPacket(__hdle hUSB, __u32 fifo, __u32 cnt, void *buff);

void USBC_ConfigFIFO_Base(__hdle hUSB, __u32 sram_base, __u32 fifo_mode);
__u32 USBC_GetPortFifoStartAddr(__hdle hUSB);
__u32 USBC_GetPortFifoSize(__hdle hUSB);
__u32 USBC_SelectFIFO(__hdle hUSB, __u32 ep_index);
void USBC_ConfigFifo_Default(__hdle hUSB, __u32 ep_type);
void USBC_ConfigFifo(__hdle hUSB, __u32 ep_type, __u32 is_double_fifo, __u32 fifo_size, __u32 fifo_addr);

void USBC_SelectBus(__hdle hUSB, __u32 io_type, __u32 ep_type, __u32 ep_index);

__u32 USBC_GetActiveEp(__hdle hUSB);
void USBC_SelectActiveEp(__hdle hUSB, __u8 ep_index);

void USBC_EnhanceSignal(__hdle hUSB);

__u32 USBC_GetLastFrameNumber(__hdle hUSB);


/* usb 中断操作部分 */
__u32 USBC_INT_EpPending(__hdle hUSB, __u32 ep_type);
__u32 USBC_INT_MiscPending(__hdle hUSB);
void USBC_INT_ClearEpPending(__hdle hUSB, __u32 ep_type, __u8 ep_index);
void USBC_INT_ClearMiscPending(__hdle hUSB, __u32 mask);
void USBC_INT_ClearEpPendingAll(__hdle hUSB, __u32 ep_type);
void USBC_INT_ClearMiscPendingAll(__hdle hUSB);

void USBC_INT_EnableEp(__hdle hUSB, __u32 ep_type, __u8 ep_index);
void USBC_INT_EnableUsbMiscUint(__hdle hUSB, __u32 mask);

void USBC_INT_DisableEp(__hdle hUSB, __u32 ep_type, __u8 ep_index);
void USBC_INT_DisableUsbMiscUint(__hdle hUSB, __u32 mask);

void USBC_INT_DisableEpAll(__hdle hUSB, __u32 ep_type);
void USBC_INT_DisableUsbMiscAll(__hdle hUSB);


/* usb 控制操作部分 */
__u32 USBC_GetVbusStatus(__hdle hUSB);
__u32 USBC_GetStatus_Dp(__hdle hUSB);
__u32 USBC_GetStatus_Dm(__hdle hUSB);
__u32 USBC_GetStatus_DpDm(__hdle hUSB);

__u32 USBC_GetOtgMode_Form_ID(__hdle hUSB);
__u32 USBC_GetOtgMode_Form_BDevice(__hdle hUSB);

void USBC_SetWakeUp_Default(__hdle hUSB);

void USBC_EnableIdPullUp(__hdle hUSB);
void USBC_DisableIdPullUp(__hdle hUSB);
void USBC_EnableDpDmPullUp(__hdle hUSB);
void USBC_DisableDpDmPullUp(__hdle hUSB);

void USBC_ForceId(__hdle hUSB, __u32 id_type);
void USBC_ForceVbusValid(__hdle hUSB, __u32 vbus_type);

void USBC_A_valid_InputSelect(__hdle hUSB, __u32 source);

void USBC_EnableUsbLineStateBypass(__hdle hUSB);
void USBC_DisableUsbLineStateBypass(__hdle hUSB);
void USBC_EnableHosc(__hdle hUSB);
void USBC_DisableHosc(__hdle hUSB);

__u32 USBC_IsVbusChange(__hdle hUSB);
__u32 USBC_IsIdChange(__hdle hUSB);
__u32 USBC_IsDpDmChange(__hdle hUSB);

void USBC_DisableWakeIrq(__hdle hUSB);
void USBC_DisableVbusChange(__hdle hUSB);
void USBC_DisableIdChange(__hdle hUSB);
void USBC_DisableDpDmChange(__hdle hUSB);

void USBC_EnableWakeIrq(__hdle hUSB);
void USBC_EnableVbusChange(__hdle hUSB);
void USBC_EnableIdChange(__hdle hUSB);
void USBC_EnableDpDmChange(__hdle hUSB);

/* usb phy control*/
void USBC_PHY_SetCommonConfig(void);
void USBC_PHY_SetPrivateConfig(__hdle hUSB);
__u32 USBC_PHY_GetCommonConfig(void);
void USBC_phy_Standby(__hdle hUSB, __u32 phy_index);
void USBC_Phy_Standby_Recover(__hdle hUSB, __u32 phy_index);
void UsbPhyInit(__u32 usbc_no);
void UsbPhyEndReset(__u32 usbc_no);

/* usb 测试模式 */
void USBC_EnterMode_TestPacket(__hdle hUSB);
void USBC_EnterMode_Test_K(__hdle hUSB);
void USBC_EnterMode_Test_J(__hdle hUSB);
void USBC_EnterMode_Test_SE0_NAK(__hdle hUSB);
void USBC_EnterMode_Idle(__hdle hUSB);

__u32 USBC_TestMode_ReadReg(__hdle hUSB, __u32 offset, __u32 reg_width);


//---------------------------------------------------------------
//  bsp入口
//---------------------------------------------------------------
/* 控制器信息 */
typedef struct tag_usbc_info{
    __u32 num;      /* 控制器编号 */
    __u32 base;     /* 控制器基址 */
}usbc_info_t;

typedef struct tag_bsp_usbc{
    usbc_info_t usbc_info[USBC_MAX_CTL_NUM];
    __u32 sram_base;
}bsp_usbc_t;

/* 打开设备 */
__hdle USBC_open_otg(__u32 otg_no);
__s32  USBC_close_otg(__hdle hUSB);

/* 初始化 */
__s32 USBC_init(bsp_usbc_t *usbc);
__s32 USBC_exit(bsp_usbc_t *usbc);


#endif   //__SUNXI_USB_BSP_H__

