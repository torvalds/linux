/************************************************************************/
/*									*/
/*	Title		:	RIO Host Card Hardware Definitions	*/
/*									*/
/*	Author		:	N.P.Vassallo				*/
/*									*/
/*	Creation	:	26th April 1999				*/
/*									*/
/*	Version		:	1.0.0					*/
/*									*/
/*	Copyright	:	(c) Specialix International Ltd. 1999	*
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *									*/
/*	Description	:	Prototypes, structures and definitions	*/
/*				describing the RIO board hardware	*/
/*									*/
/************************************************************************/

#ifndef	_rioboard_h		/* If RIOBOARD.H not already defined */
#define	_rioboard_h    1

/*****************************************************************************
***********************                                ***********************
***********************   Hardware Control Registers   ***********************
***********************                                ***********************
*****************************************************************************/

/* Hardware Registers... */

#define	RIO_REG_BASE	0x7C00	/* Base of control registers */

#define	RIO_CONFIG	RIO_REG_BASE + 0x0000	/* WRITE: Configuration Register */
#define	RIO_INTSET	RIO_REG_BASE + 0x0080	/* WRITE: Interrupt Set */
#define	RIO_RESET	RIO_REG_BASE + 0x0100	/* WRITE: Host Reset */
#define	RIO_INTRESET	RIO_REG_BASE + 0x0180	/* WRITE: Interrupt Reset */

#define	RIO_VPD_ROM	RIO_REG_BASE + 0x0000	/* READ: Vital Product Data ROM */
#define	RIO_INTSTAT	RIO_REG_BASE + 0x0080	/* READ: Interrupt Status (Jet boards only) */
#define	RIO_RESETSTAT	RIO_REG_BASE + 0x0100	/* READ: Reset Status (Jet boards only) */

/* RIO_VPD_ROM definitions... */
#define	VPD_SLX_ID1	0x00	/* READ: Specialix Identifier #1 */
#define	VPD_SLX_ID2	0x01	/* READ: Specialix Identifier #2 */
#define	VPD_HW_REV	0x02	/* READ: Hardware Revision */
#define	VPD_HW_ASSEM	0x03	/* READ: Hardware Assembly Level */
#define	VPD_UNIQUEID4	0x04	/* READ: Unique Identifier #4 */
#define	VPD_UNIQUEID3	0x05	/* READ: Unique Identifier #3 */
#define	VPD_UNIQUEID2	0x06	/* READ: Unique Identifier #2 */
#define	VPD_UNIQUEID1	0x07	/* READ: Unique Identifier #1 */
#define	VPD_MANU_YEAR	0x08	/* READ: Year Of Manufacture (0 = 1970) */
#define	VPD_MANU_WEEK	0x09	/* READ: Week Of Manufacture (0 = week 1 Jan) */
#define	VPD_HWFEATURE1	0x0A	/* READ: Hardware Feature Byte 1 */
#define	VPD_HWFEATURE2	0x0B	/* READ: Hardware Feature Byte 2 */
#define	VPD_HWFEATURE3	0x0C	/* READ: Hardware Feature Byte 3 */
#define	VPD_HWFEATURE4	0x0D	/* READ: Hardware Feature Byte 4 */
#define	VPD_HWFEATURE5	0x0E	/* READ: Hardware Feature Byte 5 */
#define	VPD_OEMID	0x0F	/* READ: OEM Identifier */
#define	VPD_IDENT	0x10	/* READ: Identifier string (16 bytes) */
#define	VPD_IDENT_LEN	0x10

/* VPD ROM Definitions... */
#define	SLX_ID1		0x4D
#define	SLX_ID2		0x98

#define	PRODUCT_ID(a)	((a>>4)&0xF)	/* Use to obtain Product ID from VPD_UNIQUEID1 */

#define	ID_SX_ISA	0x2
#define	ID_RIO_EISA	0x3
#define	ID_SX_PCI	0x5
#define	ID_SX_EISA	0x7
#define	ID_RIO_RTA16	0x9
#define	ID_RIO_ISA	0xA
#define	ID_RIO_MCA	0xB
#define	ID_RIO_SBUS	0xC
#define	ID_RIO_PCI	0xD
#define	ID_RIO_RTA8	0xE

/* Transputer bootstrap definitions... */

#define	BOOTLOADADDR		(0x8000 - 6)
#define	BOOTINDICATE		(0x8000 - 2)

/* Firmware load position... */

#define	FIRMWARELOADADDR	0x7C00	/* Firmware is loaded _before_ this address */

/*****************************************************************************
*****************************                    *****************************
*****************************   RIO (Rev1) ISA   *****************************
*****************************                    *****************************
*****************************************************************************/

/* Control Register Definitions... */
#define	RIO_ISA_IDENT	"JBJGPGGHINSMJPJR"

#define	RIO_ISA_CFG_BOOTRAM	0x01	/* Boot from RAM, else Link */
#define	RIO_ISA_CFG_BUSENABLE	0x02	/* Enable processor bus */
#define	RIO_ISA_CFG_IRQMASK	0x30	/* Interrupt mask */
#define	  RIO_ISA_CFG_IRQ12	0x10	/* Interrupt Level 12 */
#define	  RIO_ISA_CFG_IRQ11	0x20	/* Interrupt Level 11 */
#define	  RIO_ISA_CFG_IRQ9	0x30	/* Interrupt Level 9 */
#define	RIO_ISA_CFG_LINK20	0x40	/* 20Mbps link, else 10Mbps */
#define	RIO_ISA_CFG_WAITSTATE0	0x80	/* 0 waitstates, else 1 */

/*****************************************************************************
*****************************                    *****************************
*****************************   RIO (Rev2) ISA   *****************************
*****************************                    *****************************
*****************************************************************************/

/* Control Register Definitions... */
#define	RIO_ISA2_IDENT	"JBJGPGGHINSMJPJR"

#define	RIO_ISA2_CFG_BOOTRAM	0x01	/* Boot from RAM, else Link */
#define	RIO_ISA2_CFG_BUSENABLE	0x02	/* Enable processor bus */
#define	RIO_ISA2_CFG_INTENABLE	0x04	/* Interrupt enable, else disable */
#define	RIO_ISA2_CFG_16BIT	0x08	/* 16bit mode, else 8bit */
#define	RIO_ISA2_CFG_IRQMASK	0x30	/* Interrupt mask */
#define	  RIO_ISA2_CFG_IRQ15	0x00	/* Interrupt Level 15 */
#define	  RIO_ISA2_CFG_IRQ12	0x10	/* Interrupt Level 12 */
#define	  RIO_ISA2_CFG_IRQ11	0x20	/* Interrupt Level 11 */
#define	  RIO_ISA2_CFG_IRQ9	0x30	/* Interrupt Level 9 */
#define	RIO_ISA2_CFG_LINK20	0x40	/* 20Mbps link, else 10Mbps */
#define	RIO_ISA2_CFG_WAITSTATE0	0x80	/* 0 waitstates, else 1 */

/*****************************************************************************
*****************************                   ******************************
*****************************   RIO (Jet) ISA   ******************************
*****************************                   ******************************
*****************************************************************************/

/* Control Register Definitions... */
#define	RIO_ISA3_IDENT	"JET HOST BY KEV#"

#define	RIO_ISA3_CFG_BUSENABLE	0x02	/* Enable processor bus */
#define	RIO_ISA3_CFG_INTENABLE	0x04	/* Interrupt enable, else disable */
#define	RIO_ISA32_CFG_IRQMASK	0xF30	/* Interrupt mask */
#define	  RIO_ISA3_CFG_IRQ15	0xF0	/* Interrupt Level 15 */
#define	  RIO_ISA3_CFG_IRQ12	0xC0	/* Interrupt Level 12 */
#define	  RIO_ISA3_CFG_IRQ11	0xB0	/* Interrupt Level 11 */
#define	  RIO_ISA3_CFG_IRQ10	0xA0	/* Interrupt Level 10 */
#define	  RIO_ISA3_CFG_IRQ9	0x90	/* Interrupt Level 9 */

/*****************************************************************************
*********************************             ********************************
*********************************   RIO MCA   ********************************
*********************************             ********************************
*****************************************************************************/

/* Control Register Definitions... */
#define	RIO_MCA_IDENT	"JBJGPGGHINSMJPJR"

#define	RIO_MCA_CFG_BOOTRAM	0x01	/* Boot from RAM, else Link */
#define	RIO_MCA_CFG_BUSENABLE	0x02	/* Enable processor bus */
#define	RIO_MCA_CFG_LINK20	0x40	/* 20Mbps link, else 10Mbps */

/*****************************************************************************
********************************              ********************************
********************************   RIO EISA   ********************************
********************************              ********************************
*****************************************************************************/

/* EISA Configuration Space Definitions... */
#define	EISA_PRODUCT_ID1	0xC80
#define	EISA_PRODUCT_ID2	0xC81
#define	EISA_PRODUCT_NUMBER	0xC82
#define	EISA_REVISION_NUMBER	0xC83
#define	EISA_CARD_ENABLE	0xC84
#define	EISA_VPD_UNIQUEID4	0xC88	/* READ: Unique Identifier #4 */
#define	EISA_VPD_UNIQUEID3	0xC8A	/* READ: Unique Identifier #3 */
#define	EISA_VPD_UNIQUEID2	0xC90	/* READ: Unique Identifier #2 */
#define	EISA_VPD_UNIQUEID1	0xC92	/* READ: Unique Identifier #1 */
#define	EISA_VPD_MANU_YEAR	0xC98	/* READ: Year Of Manufacture (0 = 1970) */
#define	EISA_VPD_MANU_WEEK	0xC9A	/* READ: Week Of Manufacture (0 = week 1 Jan) */
#define	EISA_MEM_ADDR_23_16	0xC00
#define	EISA_MEM_ADDR_31_24	0xC01
#define	EISA_RIO_CONFIG		0xC02	/* WRITE: Configuration Register */
#define	EISA_RIO_INTSET		0xC03	/* WRITE: Interrupt Set */
#define	EISA_RIO_INTRESET	0xC03	/* READ:  Interrupt Reset */

/* Control Register Definitions... */
#define	RIO_EISA_CFG_BOOTRAM	0x01	/* Boot from RAM, else Link */
#define	RIO_EISA_CFG_LINK20	0x02	/* 20Mbps link, else 10Mbps */
#define	RIO_EISA_CFG_BUSENABLE	0x04	/* Enable processor bus */
#define	RIO_EISA_CFG_PROCRUN	0x08	/* Processor running, else reset */
#define	RIO_EISA_CFG_IRQMASK	0xF0	/* Interrupt mask */
#define	  RIO_EISA_CFG_IRQ15	0xF0	/* Interrupt Level 15 */
#define	  RIO_EISA_CFG_IRQ14	0xE0	/* Interrupt Level 14 */
#define	  RIO_EISA_CFG_IRQ12	0xC0	/* Interrupt Level 12 */
#define	  RIO_EISA_CFG_IRQ11	0xB0	/* Interrupt Level 11 */
#define	  RIO_EISA_CFG_IRQ10	0xA0	/* Interrupt Level 10 */
#define	  RIO_EISA_CFG_IRQ9	0x90	/* Interrupt Level 9 */
#define	  RIO_EISA_CFG_IRQ7	0x70	/* Interrupt Level 7 */
#define	  RIO_EISA_CFG_IRQ6	0x60	/* Interrupt Level 6 */
#define	  RIO_EISA_CFG_IRQ5	0x50	/* Interrupt Level 5 */
#define	  RIO_EISA_CFG_IRQ4	0x40	/* Interrupt Level 4 */
#define	  RIO_EISA_CFG_IRQ3	0x30	/* Interrupt Level 3 */

/*****************************************************************************
********************************              ********************************
********************************   RIO SBus   ********************************
********************************              ********************************
*****************************************************************************/

/* Control Register Definitions... */
#define	RIO_SBUS_IDENT	"JBPGK#\0\0\0\0\0\0\0\0\0\0"

#define	RIO_SBUS_CFG_BOOTRAM	0x01	/* Boot from RAM, else Link */
#define	RIO_SBUS_CFG_BUSENABLE	0x02	/* Enable processor bus */
#define	RIO_SBUS_CFG_INTENABLE	0x04	/* Interrupt enable, else disable */
#define	RIO_SBUS_CFG_IRQMASK	0x38	/* Interrupt mask */
#define	  RIO_SBUS_CFG_IRQNONE	0x00	/* No Interrupt */
#define	  RIO_SBUS_CFG_IRQ7	0x38	/* Interrupt Level 7 */
#define	  RIO_SBUS_CFG_IRQ6	0x30	/* Interrupt Level 6 */
#define	  RIO_SBUS_CFG_IRQ5	0x28	/* Interrupt Level 5 */
#define	  RIO_SBUS_CFG_IRQ4	0x20	/* Interrupt Level 4 */
#define	  RIO_SBUS_CFG_IRQ3	0x18	/* Interrupt Level 3 */
#define	  RIO_SBUS_CFG_IRQ2	0x10	/* Interrupt Level 2 */
#define	  RIO_SBUS_CFG_IRQ1	0x08	/* Interrupt Level 1 */
#define	RIO_SBUS_CFG_LINK20	0x40	/* 20Mbps link, else 10Mbps */
#define	RIO_SBUS_CFG_PROC25	0x80	/* 25Mhz processor clock, else 20Mhz */

/*****************************************************************************
*********************************             ********************************
*********************************   RIO PCI   ********************************
*********************************             ********************************
*****************************************************************************/

/* Control Register Definitions... */
#define	RIO_PCI_IDENT	"ECDDPGJGJHJRGSK#"

#define	RIO_PCI_CFG_BOOTRAM	0x01	/* Boot from RAM, else Link */
#define	RIO_PCI_CFG_BUSENABLE	0x02	/* Enable processor bus */
#define	RIO_PCI_CFG_INTENABLE	0x04	/* Interrupt enable, else disable */
#define	RIO_PCI_CFG_LINK20	0x40	/* 20Mbps link, else 10Mbps */
#define	RIO_PCI_CFG_PROC25	0x80	/* 25Mhz processor clock, else 20Mhz */

/* PCI Definitions... */
#define	SPX_VENDOR_ID		0x11CB	/* Assigned by the PCI SIG */
#define	SPX_DEVICE_ID		0x8000	/* RIO bridge boards */
#define	SPX_PLXDEVICE_ID	0x2000	/* PLX bridge boards */
#define	SPX_SUB_VENDOR_ID	SPX_VENDOR_ID	/* Same as vendor id */
#define	RIO_SUB_SYS_ID		0x0800	/* RIO PCI board */

/*****************************************************************************
*****************************                   ******************************
*****************************   RIO (Jet) PCI   ******************************
*****************************                   ******************************
*****************************************************************************/

/* Control Register Definitions... */
#define	RIO_PCI2_IDENT	"JET HOST BY KEV#"

#define	RIO_PCI2_CFG_BUSENABLE	0x02	/* Enable processor bus */
#define	RIO_PCI2_CFG_INTENABLE	0x04	/* Interrupt enable, else disable */

/* PCI Definitions... */
#define	RIO2_SUB_SYS_ID		0x0100	/* RIO (Jet) PCI board */

#endif						/*_rioboard_h */

/* End of RIOBOARD.H */
