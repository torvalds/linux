/*
 * M66592 UDC (USB gadget)
 *
 * Copyright (C) 2006-2007 Renesas Solutions Corp.
 *
 * Author : Yoshihiro Shimoda <shimoda.yoshihiro@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __M66592_UDC_H__
#define __M66592_UDC_H__

#define M66592_SYSCFG		0x00
#define	M66592_XTAL		0xC000	/* b15-14: Crystal selection */
#define	  M66592_XTAL48		 0x8000		  /* 48MHz */
#define   M66592_XTAL24		 0x4000		  /* 24MHz */
#define	  M66592_XTAL12		 0x0000		  /* 12MHz */
#define	M66592_XCKE		0x2000	/* b13: External clock enable */
#define	M66592_RCKE		0x1000	/* b12: Register clock enable */
#define	M66592_PLLC		0x0800	/* b11: PLL control */
#define	M66592_SCKE		0x0400	/* b10: USB clock enable */
#define	M66592_ATCKM		0x0100	/* b8: Automatic supply functional enable */
#define	M66592_HSE		0x0080	/* b7: Hi-speed enable */
#define	M66592_DCFM		0x0040	/* b6: Controller function select  */
#define	M66592_DMRPD		0x0020	/* b5: D- pull down control */
#define	M66592_DPRPU		0x0010	/* b4: D+ pull up control */
#define	M66592_FSRPC		0x0004	/* b2: Full-speed receiver enable */
#define	M66592_PCUT		0x0002	/* b1: Low power sleep enable */
#define	M66592_USBE		0x0001	/* b0: USB module operation enable */

#define M66592_SYSSTS		0x02
#define	M66592_LNST		0x0003	/* b1-0: D+, D- line status */
#define	  M66592_SE1		 0x0003		  /* SE1 */
#define	  M66592_KSTS		 0x0002		  /* K State */
#define	  M66592_JSTS		 0x0001		  /* J State */
#define	  M66592_SE0		 0x0000		  /* SE0 */

#define M66592_DVSTCTR		0x04
#define	M66592_WKUP		0x0100	/* b8: Remote wakeup */
#define	M66592_RWUPE		0x0080	/* b7: Remote wakeup sense */
#define	M66592_USBRST		0x0040	/* b6: USB reset enable */
#define	M66592_RESUME		0x0020	/* b5: Resume enable */
#define	M66592_UACT		0x0010	/* b4: USB bus enable */
#define	M66592_RHST		0x0003	/* b1-0: Reset handshake status */
#define	  M66592_HSMODE		 0x0003		  /* Hi-Speed mode */
#define	  M66592_FSMODE		 0x0002		  /* Full-Speed mode */
#define	  M66592_HSPROC		 0x0001		  /* HS handshake is processing */

#define M66592_TESTMODE		0x06
#define	M66592_UTST		0x000F	/* b4-0: Test select */
#define	  M66592_H_TST_PACKET	 0x000C		  /* HOST TEST Packet */
#define	  M66592_H_TST_SE0_NAK	 0x000B		  /* HOST TEST SE0 NAK */
#define	  M66592_H_TST_K	 0x000A		  /* HOST TEST K */
#define	  M66592_H_TST_J	 0x0009		  /* HOST TEST J */
#define	  M66592_H_TST_NORMAL	 0x0000		  /* HOST Normal Mode */
#define	  M66592_P_TST_PACKET	 0x0004		  /* PERI TEST Packet */
#define	  M66592_P_TST_SE0_NAK	 0x0003		  /* PERI TEST SE0 NAK */
#define	  M66592_P_TST_K	 0x0002		  /* PERI TEST K */
#define	  M66592_P_TST_J	 0x0001		  /* PERI TEST J */
#define	  M66592_P_TST_NORMAL	 0x0000		  /* PERI Normal Mode */

#define M66592_PINCFG		0x0A
#define	M66592_LDRV		0x8000	/* b15: Drive Current Adjust */
#define	M66592_BIGEND		0x0100	/* b8: Big endian mode */

#define M66592_DMA0CFG		0x0C
#define M66592_DMA1CFG		0x0E
#define	M66592_DREQA		0x4000	/* b14: Dreq active select */
#define	M66592_BURST		0x2000	/* b13: Burst mode */
#define	M66592_DACKA		0x0400	/* b10: Dack active select */
#define	M66592_DFORM		0x0380	/* b9-7: DMA mode select */
#define	  M66592_CPU_ADR_RD_WR	 0x0000		  /* Address + RD/WR mode (CPU bus) */
#define	  M66592_CPU_DACK_RD_WR	 0x0100		  /* DACK + RD/WR mode (CPU bus) */
#define	  M66592_CPU_DACK_ONLY	 0x0180		  /* DACK only mode (CPU bus) */
#define	  M66592_SPLIT_DACK_ONLY	 0x0200		  /* DACK only mode (SPLIT bus) */
#define	  M66592_SPLIT_DACK_DSTB	 0x0300		  /* DACK + DSTB0 mode (SPLIT bus) */
#define	M66592_DENDA		0x0040	/* b6: Dend active select */
#define	M66592_PKTM		0x0020	/* b5: Packet mode */
#define	M66592_DENDE		0x0010	/* b4: Dend enable */
#define	M66592_OBUS		0x0004	/* b2: OUTbus mode */

#define M66592_CFIFO		0x10
#define M66592_D0FIFO		0x14
#define M66592_D1FIFO		0x18

#define M66592_CFIFOSEL		0x1E
#define M66592_D0FIFOSEL	0x24
#define M66592_D1FIFOSEL	0x2A
#define	M66592_RCNT		0x8000	/* b15: Read count mode */
#define	M66592_REW		0x4000	/* b14: Buffer rewind */
#define	M66592_DCLRM		0x2000	/* b13: DMA buffer clear mode */
#define	M66592_DREQE		0x1000	/* b12: DREQ output enable */
#define	M66592_MBW		0x0400	/* b10: Maximum bit width for FIFO access */
#define	  M66592_MBW_8		 0x0000	  /*  8bit */
#define	  M66592_MBW_16		 0x0400		  /* 16bit */
#define	M66592_TRENB		0x0200	/* b9: Transaction counter enable */
#define	M66592_TRCLR		0x0100	/* b8: Transaction counter clear */
#define	M66592_DEZPM		0x0080	/* b7: Zero-length packet additional mode */
#define	M66592_ISEL		0x0020	/* b5: DCP FIFO port direction select */
#define	M66592_CURPIPE		0x0007	/* b2-0: PIPE select */

#define M66592_CFIFOCTR		0x20
#define M66592_D0FIFOCTR	0x26
#define M66592_D1FIFOCTR	0x2c
#define	M66592_BVAL		0x8000	/* b15: Buffer valid flag */
#define	M66592_BCLR		0x4000	/* b14: Buffer clear */
#define	M66592_FRDY		0x2000	/* b13: FIFO ready */
#define	M66592_DTLN		0x0FFF	/* b11-0: FIFO received data length */

#define M66592_CFIFOSIE		0x22
#define	M66592_TGL		0x8000	/* b15: Buffer toggle */
#define	M66592_SCLR		0x4000	/* b14: Buffer clear */
#define	M66592_SBUSY		0x2000	/* b13: SIE_FIFO busy */

#define M66592_D0FIFOTRN	0x28
#define M66592_D1FIFOTRN	0x2E
#define	M66592_TRNCNT		0xFFFF	/* b15-0: Transaction counter */

#define M66592_INTENB0	0x30
#define	M66592_VBSE	0x8000	/* b15: VBUS interrupt */
#define	M66592_RSME	0x4000	/* b14: Resume interrupt */
#define	M66592_SOFE	0x2000	/* b13: Frame update interrupt */
#define	M66592_DVSE	0x1000	/* b12: Device state transition interrupt */
#define	M66592_CTRE	0x0800	/* b11: Control transfer stage transition interrupt */
#define	M66592_BEMPE	0x0400	/* b10: Buffer empty interrupt */
#define	M66592_NRDYE	0x0200	/* b9: Buffer not ready interrupt */
#define	M66592_BRDYE	0x0100	/* b8: Buffer ready interrupt */
#define	M66592_URST	0x0080	/* b7: USB reset detected interrupt */
#define	M66592_SADR	0x0040	/* b6: Set address executed interrupt */
#define	M66592_SCFG	0x0020	/* b5: Set configuration executed interrupt */
#define	M66592_SUSP	0x0010	/* b4: Suspend detected interrupt */
#define	M66592_WDST	0x0008	/* b3: Control write data stage completed interrupt */
#define	M66592_RDST	0x0004	/* b2: Control read data stage completed interrupt */
#define	M66592_CMPL	0x0002	/* b1: Control transfer complete interrupt */
#define	M66592_SERR	0x0001	/* b0: Sequence error interrupt */

#define M66592_INTENB1	0x32
#define	M66592_BCHGE	0x4000	/* b14: USB us chenge interrupt */
#define	M66592_DTCHE	0x1000	/* b12: Detach sense interrupt */
#define	M66592_SIGNE	0x0020	/* b5: SETUP IGNORE interrupt */
#define	M66592_SACKE	0x0010	/* b4: SETUP ACK interrupt */
#define	M66592_BRDYM	0x0004	/* b2: BRDY clear timing */
#define	M66592_INTL	0x0002	/* b1: Interrupt sense select */
#define	M66592_PCSE	0x0001	/* b0: PCUT enable by CS assert */

#define M66592_BRDYENB		0x36
#define M66592_BRDYSTS		0x46
#define	M66592_BRDY7		0x0080	/* b7: PIPE7 */
#define	M66592_BRDY6		0x0040	/* b6: PIPE6 */
#define	M66592_BRDY5		0x0020	/* b5: PIPE5 */
#define	M66592_BRDY4		0x0010	/* b4: PIPE4 */
#define	M66592_BRDY3		0x0008	/* b3: PIPE3 */
#define	M66592_BRDY2		0x0004	/* b2: PIPE2 */
#define	M66592_BRDY1		0x0002	/* b1: PIPE1 */
#define	M66592_BRDY0		0x0001	/* b1: PIPE0 */

#define M66592_NRDYENB		0x38
#define M66592_NRDYSTS		0x48
#define	M66592_NRDY7		0x0080	/* b7: PIPE7 */
#define	M66592_NRDY6		0x0040	/* b6: PIPE6 */
#define	M66592_NRDY5		0x0020	/* b5: PIPE5 */
#define	M66592_NRDY4		0x0010	/* b4: PIPE4 */
#define	M66592_NRDY3		0x0008	/* b3: PIPE3 */
#define	M66592_NRDY2		0x0004	/* b2: PIPE2 */
#define	M66592_NRDY1		0x0002	/* b1: PIPE1 */
#define	M66592_NRDY0		0x0001	/* b1: PIPE0 */

#define M66592_BEMPENB		0x3A
#define M66592_BEMPSTS		0x4A
#define	M66592_BEMP7		0x0080	/* b7: PIPE7 */
#define	M66592_BEMP6		0x0040	/* b6: PIPE6 */
#define	M66592_BEMP5		0x0020	/* b5: PIPE5 */
#define	M66592_BEMP4		0x0010	/* b4: PIPE4 */
#define	M66592_BEMP3		0x0008	/* b3: PIPE3 */
#define	M66592_BEMP2		0x0004	/* b2: PIPE2 */
#define	M66592_BEMP1		0x0002	/* b1: PIPE1 */
#define	M66592_BEMP0		0x0001	/* b0: PIPE0 */

#define M66592_SOFCFG		0x3C
#define	M66592_SOFM		0x000C	/* b3-2: SOF palse mode */
#define	  M66592_SOF_125US	 0x0008		  /* SOF OUT 125us uFrame Signal */
#define	  M66592_SOF_1MS	 0x0004		  /* SOF OUT 1ms Frame Signal */
#define	  M66592_SOF_DISABLE	 0x0000		  /* SOF OUT Disable */

#define M66592_INTSTS0		0x40
#define	M66592_VBINT		0x8000	/* b15: VBUS interrupt */
#define	M66592_RESM		0x4000	/* b14: Resume interrupt */
#define	M66592_SOFR		0x2000	/* b13: SOF frame update interrupt */
#define	M66592_DVST		0x1000	/* b12: Device state transition interrupt */
#define	M66592_CTRT		0x0800	/* b11: Control transfer stage transition interrupt */
#define	M66592_BEMP		0x0400	/* b10: Buffer empty interrupt */
#define	M66592_NRDY		0x0200	/* b9: Buffer not ready interrupt */
#define	M66592_BRDY		0x0100	/* b8: Buffer ready interrupt */
#define	M66592_VBSTS		0x0080	/* b7: VBUS input port */
#define	M66592_DVSQ		0x0070	/* b6-4: Device state */
#define	  M66592_DS_SPD_CNFG	 0x0070		  /* Suspend Configured */
#define	  M66592_DS_SPD_ADDR	 0x0060		  /* Suspend Address */
#define	  M66592_DS_SPD_DFLT	 0x0050		  /* Suspend Default */
#define	  M66592_DS_SPD_POWR	 0x0040		  /* Suspend Powered */
#define	  M66592_DS_SUSP	 0x0040		  /* Suspend */
#define	  M66592_DS_CNFG	 0x0030		  /* Configured */
#define	  M66592_DS_ADDS	 0x0020		  /* Address */
#define	  M66592_DS_DFLT	 0x0010		  /* Default */
#define	  M66592_DS_POWR	 0x0000		  /* Powered */
#define	M66592_DVSQS		0x0030	/* b5-4: Device state */
#define	M66592_VALID		0x0008	/* b3: Setup packet detected flag */
#define	M66592_CTSQ		0x0007	/* b2-0: Control transfer stage */
#define	  M66592_CS_SQER	 0x0006		  /* Sequence error */
#define	  M66592_CS_WRND	 0x0005		  /* Control write nodata status stage */
#define	  M66592_CS_WRSS	 0x0004		  /* Control write status stage */
#define	  M66592_CS_WRDS	 0x0003		  /* Control write data stage */
#define	  M66592_CS_RDSS	 0x0002		  /* Control read status stage */
#define	  M66592_CS_RDDS	 0x0001		  /* Control read data stage */
#define	  M66592_CS_IDST	 0x0000		  /* Idle or setup stage */

#define M66592_INTSTS1		0x42
#define	M66592_BCHG		0x4000	/* b14: USB bus chenge interrupt */
#define	M66592_DTCH		0x1000	/* b12: Detach sense interrupt */
#define	M66592_SIGN		0x0020	/* b5: SETUP IGNORE interrupt */
#define	M66592_SACK		0x0010	/* b4: SETUP ACK interrupt */

#define M66592_FRMNUM		0x4C
#define	M66592_OVRN		0x8000	/* b15: Overrun error */
#define	M66592_CRCE		0x4000	/* b14: Received data error */
#define	M66592_SOFRM		0x0800	/* b11: SOF output mode */
#define	M66592_FRNM		0x07FF	/* b10-0: Frame number */

#define M66592_UFRMNUM		0x4E
#define	M66592_UFRNM		0x0007	/* b2-0: Micro frame number */

#define M66592_RECOVER		0x50
#define	M66592_STSRECOV		0x0700	/* Status recovery */
#define	  M66592_STSR_HI	 0x0400		  /* FULL(0) or HI(1) Speed */
#define	  M66592_STSR_DEFAULT	 0x0100		  /* Default state */
#define	  M66592_STSR_ADDRESS	 0x0200		  /* Address state */
#define	  M66592_STSR_CONFIG	 0x0300		  /* Configured state */
#define	M66592_USBADDR		0x007F	/* b6-0: USB address */

#define M66592_USBREQ			0x54
#define	M66592_bRequest			0xFF00	/* b15-8: bRequest */
#define	  M66592_GET_STATUS		 0x0000
#define	  M66592_CLEAR_FEATURE		 0x0100
#define	  M66592_ReqRESERVED		 0x0200
#define	  M66592_SET_FEATURE		 0x0300
#define	  M66592_ReqRESERVED1		 0x0400
#define	  M66592_SET_ADDRESS		 0x0500
#define	  M66592_GET_DESCRIPTOR		 0x0600
#define	  M66592_SET_DESCRIPTOR		 0x0700
#define	  M66592_GET_CONFIGURATION	 0x0800
#define	  M66592_SET_CONFIGURATION	 0x0900
#define	  M66592_GET_INTERFACE		 0x0A00
#define	  M66592_SET_INTERFACE		 0x0B00
#define	  M66592_SYNCH_FRAME		 0x0C00
#define	M66592_bmRequestType		0x00FF	/* b7-0: bmRequestType */
#define	M66592_bmRequestTypeDir		0x0080	/* b7  : Data transfer direction */
#define	  M66592_HOST_TO_DEVICE		 0x0000
#define	  M66592_DEVICE_TO_HOST		 0x0080
#define	M66592_bmRequestTypeType	0x0060	/* b6-5: Type */
#define	  M66592_STANDARD		 0x0000
#define	  M66592_CLASS			 0x0020
#define	  M66592_VENDOR			 0x0040
#define	M66592_bmRequestTypeRecip	0x001F	/* b4-0: Recipient */
#define	  M66592_DEVICE			 0x0000
#define	  M66592_INTERFACE		 0x0001
#define	  M66592_ENDPOINT		 0x0002

#define M66592_USBVAL				0x56
#define	M66592_wValue				0xFFFF	/* b15-0: wValue */
/* Standard Feature Selector */
#define	  M66592_ENDPOINT_HALT			0x0000
#define	  M66592_DEVICE_REMOTE_WAKEUP		0x0001
#define	  M66592_TEST_MODE			0x0002
/* Descriptor Types */
#define	M66592_DT_TYPE				0xFF00
#define	M66592_GET_DT_TYPE(v)			(((v) & DT_TYPE) >> 8)
#define	  M66592_DT_DEVICE			0x01
#define	  M66592_DT_CONFIGURATION		0x02
#define	  M66592_DT_STRING			0x03
#define	  M66592_DT_INTERFACE			0x04
#define	  M66592_DT_ENDPOINT			0x05
#define	  M66592_DT_DEVICE_QUALIFIER		0x06
#define	  M66592_DT_OTHER_SPEED_CONFIGURATION	0x07
#define	  M66592_DT_INTERFACE_POWER		0x08
#define	M66592_DT_INDEX				0x00FF
#define	M66592_CONF_NUM				0x00FF
#define	M66592_ALT_SET				0x00FF

#define M66592_USBINDEX			0x58
#define	M66592_wIndex			0xFFFF	/* b15-0: wIndex */
#define	M66592_TEST_SELECT		0xFF00	/* b15-b8: Test Mode Selectors */
#define	  M66592_TEST_J			 0x0100		  /* Test_J */
#define	  M66592_TEST_K			 0x0200		  /* Test_K */
#define	  M66592_TEST_SE0_NAK		 0x0300		  /* Test_SE0_NAK */
#define	  M66592_TEST_PACKET		 0x0400		  /* Test_Packet */
#define	  M66592_TEST_FORCE_ENABLE	 0x0500		  /* Test_Force_Enable */
#define	  M66592_TEST_STSelectors	 0x0600		  /* Standard test selectors */
#define	  M66592_TEST_Reserved		 0x4000		  /* Reserved */
#define	  M66592_TEST_VSTModes		 0xC000		  /* Vendor-specific test modes */
#define	M66592_EP_DIR			0x0080	/* b7: Endpoint Direction */
#define	  M66592_EP_DIR_IN		 0x0080
#define	  M66592_EP_DIR_OUT		 0x0000

#define M66592_USBLENG		0x5A
#define	M66592_wLength		0xFFFF	/* b15-0: wLength */

#define M66592_DCPCFG		0x5C
#define	M66592_CNTMD		0x0100	/* b8: Continuous transfer mode select */
#define	M66592_DIR		0x0010	/* b4: Control transfer DIR select */

#define M66592_DCPMAXP		0x5E
#define	M66592_DEVSEL		0xC000	/* b15-14: Device address select */
#define	  M66592_DEVICE_0	 0x0000		  /* Device address 0 */
#define	  M66592_DEVICE_1	 0x4000		  /* Device address 1 */
#define	  M66592_DEVICE_2	 0x8000		  /* Device address 2 */
#define	  M66592_DEVICE_3	 0xC000		  /* Device address 3 */
#define	M66592_MAXP		0x007F	/* b6-0: Maxpacket size of default control pipe */

#define M66592_DCPCTR		0x60
#define	M66592_BSTS		0x8000	/* b15: Buffer status */
#define	M66592_SUREQ		0x4000	/* b14: Send USB request  */
#define	M66592_SQCLR		0x0100	/* b8: Sequence toggle bit clear */
#define	M66592_SQSET		0x0080	/* b7: Sequence toggle bit set */
#define	M66592_SQMON		0x0040	/* b6: Sequence toggle bit monitor */
#define	M66592_CCPL		0x0004	/* b2: Enable control transfer complete */
#define	M66592_PID		0x0003	/* b1-0: Response PID */
#define	  M66592_PID_STALL	 0x0002		  /* STALL */
#define	  M66592_PID_BUF	 0x0001		  /* BUF */
#define	  M66592_PID_NAK	 0x0000		  /* NAK */

#define M66592_PIPESEL		0x64
#define	M66592_PIPENM		0x0007	/* b2-0: Pipe select */
#define	  M66592_PIPE0		 0x0000		  /* PIPE 0 */
#define	  M66592_PIPE1		 0x0001		  /* PIPE 1 */
#define	  M66592_PIPE2		 0x0002		  /* PIPE 2 */
#define	  M66592_PIPE3		 0x0003		  /* PIPE 3 */
#define	  M66592_PIPE4		 0x0004		  /* PIPE 4 */
#define	  M66592_PIPE5		 0x0005		  /* PIPE 5 */
#define	  M66592_PIPE6		 0x0006		  /* PIPE 6 */
#define	  M66592_PIPE7		 0x0007		  /* PIPE 7 */

#define M66592_PIPECFG		0x66
#define	M66592_TYP		0xC000	/* b15-14: Transfer type */
#define	  M66592_ISO		 0xC000		  /* Isochronous */
#define	  M66592_INT		 0x8000		  /* Interrupt */
#define	  M66592_BULK		 0x4000		  /* Bulk */
#define	M66592_BFRE		0x0400	/* b10: Buffer ready interrupt mode select */
#define	M66592_DBLB		0x0200	/* b9: Double buffer mode select */
#define	M66592_CNTMD		0x0100	/* b8: Continuous transfer mode select */
#define	M66592_SHTNAK		0x0080	/* b7: Transfer end NAK */
#define	M66592_DIR		0x0010	/* b4: Transfer direction select */
#define	  M66592_DIR_H_OUT	 0x0010		  /* HOST OUT */
#define	  M66592_DIR_P_IN	 0x0010		  /* PERI IN */
#define	  M66592_DIR_H_IN	 0x0000		  /* HOST IN */
#define	  M66592_DIR_P_OUT	 0x0000		  /* PERI OUT */
#define	M66592_EPNUM		0x000F	/* b3-0: Eendpoint number select */
#define	  M66592_EP1		 0x0001
#define	  M66592_EP2		 0x0002
#define	  M66592_EP3		 0x0003
#define	  M66592_EP4		 0x0004
#define	  M66592_EP5		 0x0005
#define	  M66592_EP6		 0x0006
#define	  M66592_EP7		 0x0007
#define	  M66592_EP8		 0x0008
#define	  M66592_EP9		 0x0009
#define	  M66592_EP10		 0x000A
#define	  M66592_EP11		 0x000B
#define	  M66592_EP12		 0x000C
#define	  M66592_EP13		 0x000D
#define	  M66592_EP14		 0x000E
#define	  M66592_EP15		 0x000F

#define M66592_PIPEBUF		0x68
#define	M66592_BUFSIZE		0x7C00	/* b14-10: Pipe buffer size */
#define	M66592_BUF_SIZE(x)	((((x) / 64) - 1) << 10)
#define	M66592_BUFNMB		0x00FF	/* b7-0: Pipe buffer number */

#define M66592_PIPEMAXP		0x6A
#define	M66592_MXPS		0x07FF	/* b10-0: Maxpacket size */

#define M66592_PIPEPERI		0x6C
#define	M66592_IFIS		0x1000	/* b12: Isochronous in-buffer flush mode select */
#define	M66592_IITV		0x0007	/* b2-0: Isochronous interval */

#define M66592_PIPE1CTR		0x70
#define M66592_PIPE2CTR		0x72
#define M66592_PIPE3CTR		0x74
#define M66592_PIPE4CTR		0x76
#define M66592_PIPE5CTR		0x78
#define M66592_PIPE6CTR		0x7A
#define M66592_PIPE7CTR		0x7C
#define	M66592_BSTS		0x8000	/* b15: Buffer status */
#define	M66592_INBUFM		0x4000	/* b14: IN buffer monitor (Only for PIPE1 to 5) */
#define	M66592_ACLRM		0x0200	/* b9: Out buffer auto clear mode */
#define	M66592_SQCLR		0x0100	/* b8: Sequence toggle bit clear */
#define	M66592_SQSET		0x0080	/* b7: Sequence toggle bit set */
#define	M66592_SQMON		0x0040	/* b6: Sequence toggle bit monitor */
#define	M66592_PID		0x0003	/* b1-0: Response PID */

#define M66592_INVALID_REG	0x7E


#define __iomem

#define get_pipectr_addr(pipenum)	(M66592_PIPE1CTR + (pipenum - 1) * 2)

#define M66592_MAX_SAMPLING	10

#define M66592_MAX_NUM_PIPE	8
#define M66592_MAX_NUM_BULK	3
#define M66592_MAX_NUM_ISOC	2
#define M66592_MAX_NUM_INT	2

#define M66592_BASE_PIPENUM_BULK	3
#define M66592_BASE_PIPENUM_ISOC	1
#define M66592_BASE_PIPENUM_INT		6

#define M66592_BASE_BUFNUM	6
#define M66592_MAX_BUFNUM	0x4F

struct m66592_pipe_info {
	u16	pipe;
	u16	epnum;
	u16	maxpacket;
	u16	type;
	u16	interval;
	u16	dir_in;
};

struct m66592_request {
	struct usb_request	req;
	struct list_head	queue;
};

struct m66592_ep {
	struct usb_ep		ep;
	struct m66592		*m66592;

	struct list_head	queue;
	unsigned 		busy:1;
	unsigned		internal_ccpl:1;	/* use only control */

	/* this member can able to after m66592_enable */
	unsigned		use_dma:1;
	u16			pipenum;
	u16			type;
	const struct usb_endpoint_descriptor	*desc;
	/* register address */
	unsigned long		fifoaddr;
	unsigned long		fifosel;
	unsigned long		fifoctr;
	unsigned long		fifotrn;
	unsigned long		pipectr;
};

struct m66592 {
	spinlock_t		lock;
	void __iomem		*reg;

	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;

	struct m66592_ep	ep[M66592_MAX_NUM_PIPE];
	struct m66592_ep	*pipenum2ep[M66592_MAX_NUM_PIPE];
	struct m66592_ep	*epaddr2ep[16];

	struct usb_request	*ep0_req;	/* for internal request */
	u16			*ep0_buf;	/* for internal request */

	struct timer_list	timer;

	u16			old_vbus;
	int			scount;

	int			old_dvsq;

	/* pipe config */
	int bulk;
	int interrupt;
	int isochronous;
	int num_dma;
	int bi_bufnum;	/* bulk and isochronous's bufnum */
};

#define gadget_to_m66592(_gadget) container_of(_gadget, struct m66592, gadget)
#define m66592_to_gadget(m66592) (&m66592->gadget)

#define is_bulk_pipe(pipenum)	\
	((pipenum >= M66592_BASE_PIPENUM_BULK) && \
	 (pipenum < (M66592_BASE_PIPENUM_BULK + M66592_MAX_NUM_BULK)))
#define is_interrupt_pipe(pipenum)	\
	((pipenum >= M66592_BASE_PIPENUM_INT) && \
	 (pipenum < (M66592_BASE_PIPENUM_INT + M66592_MAX_NUM_INT)))
#define is_isoc_pipe(pipenum)	\
	((pipenum >= M66592_BASE_PIPENUM_ISOC) && \
	 (pipenum < (M66592_BASE_PIPENUM_ISOC + M66592_MAX_NUM_ISOC)))

#define enable_irq_ready(m66592, pipenum)	\
	enable_pipe_irq(m66592, pipenum, M66592_BRDYENB)
#define disable_irq_ready(m66592, pipenum)	\
	disable_pipe_irq(m66592, pipenum, M66592_BRDYENB)
#define enable_irq_empty(m66592, pipenum)	\
	enable_pipe_irq(m66592, pipenum, M66592_BEMPENB)
#define disable_irq_empty(m66592, pipenum)	\
	disable_pipe_irq(m66592, pipenum, M66592_BEMPENB)
#define enable_irq_nrdy(m66592, pipenum)	\
	enable_pipe_irq(m66592, pipenum, M66592_NRDYENB)
#define disable_irq_nrdy(m66592, pipenum)	\
	disable_pipe_irq(m66592, pipenum, M66592_NRDYENB)

/*-------------------------------------------------------------------------*/
static inline u16 m66592_read(struct m66592 *m66592, unsigned long offset)
{
	return inw((unsigned long)m66592->reg + offset);
}

static inline void m66592_read_fifo(struct m66592 *m66592,
				    unsigned long offset,
				    void *buf, unsigned long len)
{
	unsigned long fifoaddr = (unsigned long)m66592->reg + offset;

	len = (len + 1) / 2;
	insw(fifoaddr, buf, len);
}

static inline void m66592_write(struct m66592 *m66592, u16 val,
				unsigned long offset)
{
	outw(val, (unsigned long)m66592->reg + offset);
}

static inline void m66592_write_fifo(struct m66592 *m66592,
				     unsigned long offset,
				     void *buf, unsigned long len)
{
	unsigned long fifoaddr = (unsigned long)m66592->reg + offset;
	unsigned long odd = len & 0x0001;

	len = len / 2;
	outsw(fifoaddr, buf, len);
	if (odd) {
		unsigned char *p = buf + len*2;
		outb(*p, fifoaddr);
	}
}

static inline void m66592_mdfy(struct m66592 *m66592, u16 val, u16 pat,
			       unsigned long offset)
{
	u16 tmp;
	tmp = m66592_read(m66592, offset);
	tmp = tmp & (~pat);
	tmp = tmp | val;
	m66592_write(m66592, tmp, offset);
}

#define m66592_bclr(m66592, val, offset)	\
			m66592_mdfy(m66592, 0, val, offset)
#define m66592_bset(m66592, val, offset)	\
			m66592_mdfy(m66592, val, 0, offset)

#endif	/* ifndef __M66592_UDC_H__ */


