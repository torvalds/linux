/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HD64570_H
#define __HD64570_H

/* SCA HD64570 register definitions - all addresses for mode 0 (8086 MPU)
   and 1 (64180 MPU). For modes 2 and 3, XOR the address with 0x01.

   Source: HD64570 SCA User's Manual
*/



/* SCA Control Registers */
#define LPR    0x00		/* Low Power */

/* Wait controller registers */
#define PABR0  0x02		/* Physical Address Boundary 0 */
#define PABR1  0x03		/* Physical Address Boundary 1 */
#define WCRL   0x04		/* Wait Control L */
#define WCRM   0x05		/* Wait Control M */
#define WCRH   0x06		/* Wait Control H */

#define PCR    0x08		/* DMA Priority Control */
#define DMER   0x09		/* DMA Master Enable */


/* Interrupt registers */
#define ISR0   0x10		/* Interrupt Status 0  */
#define ISR1   0x11		/* Interrupt Status 1  */
#define ISR2   0x12		/* Interrupt Status 2  */

#define IER0   0x14		/* Interrupt Enable 0  */
#define IER1   0x15		/* Interrupt Enable 1  */
#define IER2   0x16		/* Interrupt Enable 2  */

#define ITCR   0x18		/* Interrupt Control */
#define IVR    0x1A		/* Interrupt Vector */
#define IMVR   0x1C		/* Interrupt Modified Vector */



/* MSCI channel (port) 0 registers - offset 0x20
   MSCI channel (port) 1 registers - offset 0x40 */

#define MSCI0_OFFSET 0x20
#define MSCI1_OFFSET 0x40

#define TRBL   0x00		/* TX/RX buffer L */ 
#define TRBH   0x01		/* TX/RX buffer H */ 
#define ST0    0x02		/* Status 0 */
#define ST1    0x03		/* Status 1 */
#define ST2    0x04		/* Status 2 */
#define ST3    0x05		/* Status 3 */
#define FST    0x06		/* Frame Status  */
#define IE0    0x08		/* Interrupt Enable 0 */
#define IE1    0x09		/* Interrupt Enable 1 */
#define IE2    0x0A		/* Interrupt Enable 2 */
#define FIE    0x0B		/* Frame Interrupt Enable  */
#define CMD    0x0C		/* Command */
#define MD0    0x0E		/* Mode 0 */
#define MD1    0x0F		/* Mode 1 */
#define MD2    0x10		/* Mode 2 */
#define CTL    0x11		/* Control */
#define SA0    0x12		/* Sync/Address 0 */
#define SA1    0x13		/* Sync/Address 1 */
#define IDL    0x14		/* Idle Pattern */
#define TMC    0x15		/* Time Constant */
#define RXS    0x16		/* RX Clock Source */
#define TXS    0x17		/* TX Clock Source */
#define TRC0   0x18		/* TX Ready Control 0 */ 
#define TRC1   0x19		/* TX Ready Control 1 */ 
#define RRC    0x1A		/* RX Ready Control */ 
#define CST0   0x1C		/* Current Status 0 */
#define CST1   0x1D		/* Current Status 1 */


/* Timer channel 0 (port 0 RX) registers - offset 0x60
   Timer channel 1 (port 0 TX) registers - offset 0x68
   Timer channel 2 (port 1 RX) registers - offset 0x70
   Timer channel 3 (port 1 TX) registers - offset 0x78
*/

#define TIMER0RX_OFFSET 0x60
#define TIMER0TX_OFFSET 0x68
#define TIMER1RX_OFFSET 0x70
#define TIMER1TX_OFFSET 0x78

#define TCNTL  0x00		/* Up-counter L */
#define TCNTH  0x01		/* Up-counter H */
#define TCONRL 0x02		/* Constant L */
#define TCONRH 0x03		/* Constant H */
#define TCSR   0x04		/* Control/Status */
#define TEPR   0x05		/* Expand Prescale */



/* DMA channel 0 (port 0 RX) registers - offset 0x80
   DMA channel 1 (port 0 TX) registers - offset 0xA0
   DMA channel 2 (port 1 RX) registers - offset 0xC0
   DMA channel 3 (port 1 TX) registers - offset 0xE0
*/

#define DMAC0RX_OFFSET 0x80
#define DMAC0TX_OFFSET 0xA0
#define DMAC1RX_OFFSET 0xC0
#define DMAC1TX_OFFSET 0xE0

#define BARL   0x00		/* Buffer Address L (chained block) */
#define BARH   0x01		/* Buffer Address H (chained block) */
#define BARB   0x02		/* Buffer Address B (chained block) */

#define DARL   0x00		/* RX Destination Addr L (single block) */
#define DARH   0x01		/* RX Destination Addr H (single block) */
#define DARB   0x02		/* RX Destination Addr B (single block) */

#define SARL   0x04		/* TX Source Address L (single block) */
#define SARH   0x05		/* TX Source Address H (single block) */
#define SARB   0x06		/* TX Source Address B (single block) */

#define CPB    0x06		/* Chain Pointer Base (chained block) */

#define CDAL   0x08		/* Current Descriptor Addr L (chained block) */
#define CDAH   0x09		/* Current Descriptor Addr H (chained block) */
#define EDAL   0x0A		/* Error Descriptor Addr L (chained block) */
#define EDAH   0x0B		/* Error Descriptor Addr H (chained block) */
#define BFLL   0x0C		/* RX Receive Buffer Length L (chained block)*/
#define BFLH   0x0D		/* RX Receive Buffer Length H (chained block)*/
#define BCRL   0x0E		/* Byte Count L */
#define BCRH   0x0F		/* Byte Count H */
#define DSR    0x10		/* DMA Status */
#define DSR_RX(node) (DSR + (node ? DMAC1RX_OFFSET : DMAC0RX_OFFSET))
#define DSR_TX(node) (DSR + (node ? DMAC1TX_OFFSET : DMAC0TX_OFFSET))
#define DMR    0x11		/* DMA Mode */
#define DMR_RX(node) (DMR + (node ? DMAC1RX_OFFSET : DMAC0RX_OFFSET))
#define DMR_TX(node) (DMR + (node ? DMAC1TX_OFFSET : DMAC0TX_OFFSET))
#define FCT    0x13		/* Frame End Interrupt Counter */
#define FCT_RX(node) (FCT + (node ? DMAC1RX_OFFSET : DMAC0RX_OFFSET))
#define FCT_TX(node) (FCT + (node ? DMAC1TX_OFFSET : DMAC0TX_OFFSET))
#define DIR    0x14		/* DMA Interrupt Enable */
#define DIR_RX(node) (DIR + (node ? DMAC1RX_OFFSET : DMAC0RX_OFFSET))
#define DIR_TX(node) (DIR + (node ? DMAC1TX_OFFSET : DMAC0TX_OFFSET))
#define DCR    0x15		/* DMA Command  */
#define DCR_RX(node) (DCR + (node ? DMAC1RX_OFFSET : DMAC0RX_OFFSET))
#define DCR_TX(node) (DCR + (node ? DMAC1TX_OFFSET : DMAC0TX_OFFSET))




/* Descriptor Structure */

typedef struct {
	u16 cp;			/* Chain Pointer */
	u32 bp;			/* Buffer Pointer (24 bits) */
	u16 len;		/* Data Length */
	u8 stat;		/* Status */
	u8 unused;		/* pads to 2-byte boundary */
}__packed pkt_desc;


/* Packet Descriptor Status bits */

#define ST_TX_EOM     0x80	/* End of frame */
#define ST_TX_EOT     0x01	/* End of transmission */

#define ST_RX_EOM     0x80	/* End of frame */
#define ST_RX_SHORT   0x40	/* Short frame */
#define ST_RX_ABORT   0x20	/* Abort */
#define ST_RX_RESBIT  0x10	/* Residual bit */
#define ST_RX_OVERRUN 0x08	/* Overrun */
#define ST_RX_CRC     0x04	/* CRC */

#define ST_ERROR_MASK 0x7C

#define DIR_EOTE      0x80      /* Transfer completed */
#define DIR_EOME      0x40      /* Frame Transfer Completed (chained-block) */
#define DIR_BOFE      0x20      /* Buffer Overflow/Underflow (chained-block)*/
#define DIR_COFE      0x10      /* Counter Overflow (chained-block) */


#define DSR_EOT       0x80      /* Transfer completed */
#define DSR_EOM       0x40      /* Frame Transfer Completed (chained-block) */
#define DSR_BOF       0x20      /* Buffer Overflow/Underflow (chained-block)*/
#define DSR_COF       0x10      /* Counter Overflow (chained-block) */
#define DSR_DE        0x02	/* DMA Enable */
#define DSR_DWE       0x01      /* DMA Write Disable */

/* DMA Master Enable Register (DMER) bits */
#define DMER_DME      0x80	/* DMA Master Enable */


#define CMD_RESET     0x21	/* Reset Channel */
#define CMD_TX_ENABLE 0x02	/* Start transmitter */
#define CMD_RX_ENABLE 0x12	/* Start receiver */

#define MD0_HDLC      0x80	/* Bit-sync HDLC mode */
#define MD0_CRC_ENA   0x04	/* Enable CRC code calculation */
#define MD0_CRC_CCITT 0x02	/* CCITT CRC instead of CRC-16 */
#define MD0_CRC_PR1   0x01	/* Initial all-ones instead of all-zeros */

#define MD0_CRC_NONE  0x00
#define MD0_CRC_16_0  0x04
#define MD0_CRC_16    0x05
#define MD0_CRC_ITU_0 0x06
#define MD0_CRC_ITU   0x07

#define MD2_NRZ	      0x00
#define MD2_NRZI      0x20
#define MD2_MANCHESTER 0x80
#define MD2_FM_MARK   0xA0
#define MD2_FM_SPACE  0xC0
#define MD2_LOOPBACK  0x03      /* Local data Loopback */

#define CTL_NORTS     0x01
#define CTL_IDLE      0x10	/* Transmit an idle pattern */
#define CTL_UDRNC     0x20	/* Idle after CRC or FCS+flag transmission */

#define ST0_TXRDY     0x02	/* TX ready */
#define ST0_RXRDY     0x01	/* RX ready */

#define ST1_UDRN      0x80	/* MSCI TX underrun */
#define ST1_CDCD      0x04	/* DCD level changed */

#define ST3_CTS       0x08	/* modem input - /CTS */
#define ST3_DCD       0x04	/* modem input - /DCD */

#define IE0_TXINT     0x80	/* TX INT MSCI interrupt enable */
#define IE0_RXINTA    0x40	/* RX INT A MSCI interrupt enable */
#define IE1_UDRN      0x80	/* TX underrun MSCI interrupt enable */
#define IE1_CDCD      0x04	/* DCD level changed */

#define DCR_ABORT     0x01	/* Software abort command */
#define DCR_CLEAR_EOF 0x02	/* Clear EOF interrupt */

/* TX and RX Clock Source - RXS and TXS */
#define CLK_BRG_MASK  0x0F
#define CLK_LINE_RX   0x00	/* TX/RX clock line input */
#define CLK_LINE_TX   0x00	/* TX/RX line input */
#define CLK_BRG_RX    0x40	/* internal baud rate generator */
#define CLK_BRG_TX    0x40	/* internal baud rate generator */
#define CLK_RXCLK_TX  0x60	/* TX clock from RX clock */

#endif
