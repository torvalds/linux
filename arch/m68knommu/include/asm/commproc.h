
/*
 * 68360 Communication Processor Module.
 * Copyright (c) 2000 Michael Leslie <mleslie@lineo.com> (mc68360) after:
 * Copyright (c) 1997 Dan Malek <dmalek@jlc.net> (mpc8xx)
 *
 * This file contains structures and information for the communication
 * processor channels.  Some CPM control and status is available
 * through the 68360 internal memory map.  See include/asm/360_immap.h for details.
 * This file is not a complete map of all of the 360 QUICC's capabilities
 *
 * On the MBX board, EPPC-Bug loads CPM microcode into the first 512
 * bytes of the DP RAM and relocates the I2C parameter area to the
 * IDMA1 space.  The remaining DP RAM is available for buffer descriptors
 * or other use.
 */
#ifndef __CPM_360__
#define __CPM_360__


/* CPM Command register masks: */
#define CPM_CR_RST	((ushort)0x8000)
#define CPM_CR_OPCODE	((ushort)0x0f00)
#define CPM_CR_CHAN	((ushort)0x00f0)
#define CPM_CR_FLG	((ushort)0x0001)

/* CPM Command set (opcodes): */
#define CPM_CR_INIT_TRX		((ushort)0x0000)
#define CPM_CR_INIT_RX		((ushort)0x0001)
#define CPM_CR_INIT_TX		((ushort)0x0002)
#define CPM_CR_HUNT_MODE	((ushort)0x0003)
#define CPM_CR_STOP_TX		((ushort)0x0004)
#define CPM_CR_GRSTOP_TX	((ushort)0x0005)
#define CPM_CR_RESTART_TX	((ushort)0x0006)
#define CPM_CR_CLOSE_RXBD	((ushort)0x0007)
#define CPM_CR_SET_GADDR	((ushort)0x0008)
#define CPM_CR_GCI_TIMEOUT	((ushort)0x0009)
#define CPM_CR_GCI_ABORT	((ushort)0x000a)
#define CPM_CR_RESET_BCS	((ushort)0x000a)

/* CPM Channel numbers. */
#define CPM_CR_CH_SCC1	((ushort)0x0000)
#define CPM_CR_CH_SCC2	((ushort)0x0004)
#define CPM_CR_CH_SPI	((ushort)0x0005)	/* SPI / Timers */
#define CPM_CR_CH_TMR	((ushort)0x0005)
#define CPM_CR_CH_SCC3	((ushort)0x0008)
#define CPM_CR_CH_SMC1	((ushort)0x0009)	/* SMC1 / IDMA1 */
#define CPM_CR_CH_IDMA1	((ushort)0x0009)
#define CPM_CR_CH_SCC4	((ushort)0x000c)
#define CPM_CR_CH_SMC2	((ushort)0x000d)	/* SMC2 / IDMA2 */
#define CPM_CR_CH_IDMA2	((ushort)0x000d)


#define mk_cr_cmd(CH, CMD)	((CMD << 8) | (CH << 4))

#if 1 /* mleslie: I dinna think we have any such restrictions on
       * DP RAM aboard the 360 board - see the MC68360UM p.3-3 */

/* The dual ported RAM is multi-functional.  Some areas can be (and are
 * being) used for microcode.  There is an area that can only be used
 * as data ram for buffer descriptors, which is all we use right now.
 * Currently the first 512 and last 256 bytes are used for microcode.
 */
/* mleslie: The uCquicc board is using no extra microcode in DPRAM */
#define CPM_DATAONLY_BASE	((uint)0x0000)
#define CPM_DATAONLY_SIZE	((uint)0x0800)
#define CPM_DP_NOSPACE		((uint)0x7fffffff)

#endif


/* Export the base address of the communication processor registers
 * and dual port ram. */
/* extern	cpm360_t	*cpmp; */		/* Pointer to comm processor */
extern QUICC *pquicc;
uint         m360_cpm_dpalloc(uint size);
/* void         *m360_cpm_hostalloc(uint size); */
void	      m360_cpm_setbrg(uint brg, uint rate);

#if 0 /* use QUICC_BD declared in include/asm/m68360_quicc.h  */
/* Buffer descriptors used by many of the CPM protocols. */
typedef struct cpm_buf_desc {
	ushort	cbd_sc;		/* Status and Control */
	ushort	cbd_datlen;	/* Data length in buffer */
	uint	cbd_bufaddr;	/* Buffer address in host memory */
} cbd_t;
#endif


/* rx bd status/control bits */
#define BD_SC_EMPTY	((ushort)0x8000)	/* Recieve is empty */
#define BD_SC_WRAP	((ushort)0x2000)	/* Last buffer descriptor in table */
#define BD_SC_INTRPT	((ushort)0x1000)	/* Interrupt on change */
#define BD_SC_LAST	((ushort)0x0800)	/* Last buffer in frame OR control char */

#define BD_SC_FIRST	((ushort)0x0400)	/* 1st buffer in an HDLC frame */
#define BD_SC_ADDR	((ushort)0x0400)	/* 1st byte is a multidrop address */

#define BD_SC_CM	((ushort)0x0200)	/* Continous mode */
#define BD_SC_ID	((ushort)0x0100)	/* Received too many idles */

#define BD_SC_AM	((ushort)0x0080)	/* Multidrop address match */
#define BD_SC_DE	((ushort)0x0080)	/* DPLL Error (HDLC) */

#define BD_SC_BR	((ushort)0x0020)	/* Break received */
#define BD_SC_LG	((ushort)0x0020)	/* Frame length violation (HDLC) */

#define BD_SC_FR	((ushort)0x0010)	/* Framing error */
#define BD_SC_NO	((ushort)0x0010)	/* Nonoctet aligned frame (HDLC) */

#define BD_SC_PR	((ushort)0x0008)	/* Parity error */
#define BD_SC_AB	((ushort)0x0008)	/* Received abort Sequence (HDLC) */

#define BD_SC_OV	((ushort)0x0002)	/* Overrun */
#define BD_SC_CD	((ushort)0x0001)	/* Carrier Detect lost */

/* tx bd status/control bits (as differ from rx bd) */
#define BD_SC_READY	((ushort)0x8000)	/* Transmit is ready */
#define BD_SC_TC	((ushort)0x0400)	/* Transmit CRC */
#define BD_SC_P		((ushort)0x0100)	/* xmt preamble */
#define BD_SC_UN	((ushort)0x0002)	/* Underrun */




/* Parameter RAM offsets. */



/* In 2.4 ppc, the PROFF_S?C? are used as byte offsets into DPRAM.
 * In 2.0, we use a more structured C struct map of DPRAM, and so 
 * instead, we need only a parameter ram `slot'  */

#define PRSLOT_SCC1	0
#define PRSLOT_SCC2	1
#define PRSLOT_SCC3	2
#define PRSLOT_SMC1	2
#define PRSLOT_SCC4	3
#define PRSLOT_SMC2	3


/* #define PROFF_SCC1	((uint)0x0000) */
/* #define PROFF_SCC2	((uint)0x0100) */
/* #define PROFF_SCC3	((uint)0x0200) */
/* #define PROFF_SMC1	((uint)0x0280) */
/* #define PROFF_SCC4	((uint)0x0300) */
/* #define PROFF_SMC2	((uint)0x0380) */


/* Define enough so I can at least use the serial port as a UART.
 * The MBX uses SMC1 as the host serial port.
 */
typedef struct smc_uart {
	ushort	smc_rbase;	/* Rx Buffer descriptor base address */
	ushort	smc_tbase;	/* Tx Buffer descriptor base address */
	u_char	smc_rfcr;	/* Rx function code */
	u_char	smc_tfcr;	/* Tx function code */
	ushort	smc_mrblr;	/* Max receive buffer length */
	uint	smc_rstate;	/* Internal */
	uint	smc_idp;	/* Internal */
	ushort	smc_rbptr;	/* Internal */
	ushort	smc_ibc;	/* Internal */
	uint	smc_rxtmp;	/* Internal */
	uint	smc_tstate;	/* Internal */
	uint	smc_tdp;	/* Internal */
	ushort	smc_tbptr;	/* Internal */
	ushort	smc_tbc;	/* Internal */
	uint	smc_txtmp;	/* Internal */
	ushort	smc_maxidl;	/* Maximum idle characters */
	ushort	smc_tmpidl;	/* Temporary idle counter */
	ushort	smc_brklen;	/* Last received break length */
	ushort	smc_brkec;	/* rcv'd break condition counter */
	ushort	smc_brkcr;	/* xmt break count register */
	ushort	smc_rmask;	/* Temporary bit mask */
} smc_uart_t;

/* Function code bits.
*/
#define SMC_EB	((u_char)0x10)	/* Set big endian byte order */

/* SMC uart mode register.
*/
#define	SMCMR_REN	((ushort)0x0001)
#define SMCMR_TEN	((ushort)0x0002)
#define SMCMR_DM	((ushort)0x000c)
#define SMCMR_SM_GCI	((ushort)0x0000)
#define SMCMR_SM_UART	((ushort)0x0020)
#define SMCMR_SM_TRANS	((ushort)0x0030)
#define SMCMR_SM_MASK	((ushort)0x0030)
#define SMCMR_PM_EVEN	((ushort)0x0100)	/* Even parity, else odd */
#define SMCMR_REVD	SMCMR_PM_EVEN
#define SMCMR_PEN	((ushort)0x0200)	/* Parity enable */
#define SMCMR_BS	SMCMR_PEN
#define SMCMR_SL	((ushort)0x0400)	/* Two stops, else one */
#define SMCR_CLEN_MASK	((ushort)0x7800)	/* Character length */
#define smcr_mk_clen(C)	(((C) << 11) & SMCR_CLEN_MASK)

/* SMC2 as Centronics parallel printer.  It is half duplex, in that
 * it can only receive or transmit.  The parameter ram values for
 * each direction are either unique or properly overlap, so we can
 * include them in one structure.
 */
typedef struct smc_centronics {
	ushort	scent_rbase;
	ushort	scent_tbase;
	u_char	scent_cfcr;
	u_char	scent_smask;
	ushort	scent_mrblr;
	uint	scent_rstate;
	uint	scent_r_ptr;
	ushort	scent_rbptr;
	ushort	scent_r_cnt;
	uint	scent_rtemp;
	uint	scent_tstate;
	uint	scent_t_ptr;
	ushort	scent_tbptr;
	ushort	scent_t_cnt;
	uint	scent_ttemp;
	ushort	scent_max_sl;
	ushort	scent_sl_cnt;
	ushort	scent_character1;
	ushort	scent_character2;
	ushort	scent_character3;
	ushort	scent_character4;
	ushort	scent_character5;
	ushort	scent_character6;
	ushort	scent_character7;
	ushort	scent_character8;
	ushort	scent_rccm;
	ushort	scent_rccr;
} smc_cent_t;

/* Centronics Status Mask Register.
*/
#define SMC_CENT_F	((u_char)0x08)
#define SMC_CENT_PE	((u_char)0x04)
#define SMC_CENT_S	((u_char)0x02)

/* SMC Event and Mask register.
*/
#define	SMCM_BRKE	((unsigned char)0x40)	/* When in UART Mode */
#define	SMCM_BRK	((unsigned char)0x10)	/* When in UART Mode */
#define	SMCM_TXE	((unsigned char)0x10)	/* When in Transparent Mode */
#define	SMCM_BSY	((unsigned char)0x04)
#define	SMCM_TX		((unsigned char)0x02)
#define	SMCM_RX		((unsigned char)0x01)

/* Baud rate generators.
*/
#define CPM_BRG_RST		((uint)0x00020000)
#define CPM_BRG_EN		((uint)0x00010000)
#define CPM_BRG_EXTC_INT	((uint)0x00000000)
#define CPM_BRG_EXTC_CLK2	((uint)0x00004000)
#define CPM_BRG_EXTC_CLK6	((uint)0x00008000)
#define CPM_BRG_ATB		((uint)0x00002000)
#define CPM_BRG_CD_MASK		((uint)0x00001ffe)
#define CPM_BRG_DIV16		((uint)0x00000001)

/* SCCs.
*/
#define SCC_GSMRH_IRP		((uint)0x00040000)
#define SCC_GSMRH_GDE		((uint)0x00010000)
#define SCC_GSMRH_TCRC_CCITT	((uint)0x00008000)
#define SCC_GSMRH_TCRC_BISYNC	((uint)0x00004000)
#define SCC_GSMRH_TCRC_HDLC	((uint)0x00000000)
#define SCC_GSMRH_REVD		((uint)0x00002000)
#define SCC_GSMRH_TRX		((uint)0x00001000)
#define SCC_GSMRH_TTX		((uint)0x00000800)
#define SCC_GSMRH_CDP		((uint)0x00000400)
#define SCC_GSMRH_CTSP		((uint)0x00000200)
#define SCC_GSMRH_CDS		((uint)0x00000100)
#define SCC_GSMRH_CTSS		((uint)0x00000080)
#define SCC_GSMRH_TFL		((uint)0x00000040)
#define SCC_GSMRH_RFW		((uint)0x00000020)
#define SCC_GSMRH_TXSY		((uint)0x00000010)
#define SCC_GSMRH_SYNL16	((uint)0x0000000c)
#define SCC_GSMRH_SYNL8		((uint)0x00000008)
#define SCC_GSMRH_SYNL4		((uint)0x00000004)
#define SCC_GSMRH_RTSM		((uint)0x00000002)
#define SCC_GSMRH_RSYN		((uint)0x00000001)

#define SCC_GSMRL_SIR		((uint)0x80000000)	/* SCC2 only */
#define SCC_GSMRL_EDGE_NONE	((uint)0x60000000)
#define SCC_GSMRL_EDGE_NEG	((uint)0x40000000)
#define SCC_GSMRL_EDGE_POS	((uint)0x20000000)
#define SCC_GSMRL_EDGE_BOTH	((uint)0x00000000)
#define SCC_GSMRL_TCI		((uint)0x10000000)
#define SCC_GSMRL_TSNC_3	((uint)0x0c000000)
#define SCC_GSMRL_TSNC_4	((uint)0x08000000)
#define SCC_GSMRL_TSNC_14	((uint)0x04000000)
#define SCC_GSMRL_TSNC_INF	((uint)0x00000000)
#define SCC_GSMRL_RINV		((uint)0x02000000)
#define SCC_GSMRL_TINV		((uint)0x01000000)
#define SCC_GSMRL_TPL_128	((uint)0x00c00000)
#define SCC_GSMRL_TPL_64	((uint)0x00a00000)
#define SCC_GSMRL_TPL_48	((uint)0x00800000)
#define SCC_GSMRL_TPL_32	((uint)0x00600000)
#define SCC_GSMRL_TPL_16	((uint)0x00400000)
#define SCC_GSMRL_TPL_8		((uint)0x00200000)
#define SCC_GSMRL_TPL_NONE	((uint)0x00000000)
#define SCC_GSMRL_TPP_ALL1	((uint)0x00180000)
#define SCC_GSMRL_TPP_01	((uint)0x00100000)
#define SCC_GSMRL_TPP_10	((uint)0x00080000)
#define SCC_GSMRL_TPP_ZEROS	((uint)0x00000000)
#define SCC_GSMRL_TEND		((uint)0x00040000)
#define SCC_GSMRL_TDCR_32	((uint)0x00030000)
#define SCC_GSMRL_TDCR_16	((uint)0x00020000)
#define SCC_GSMRL_TDCR_8	((uint)0x00010000)
#define SCC_GSMRL_TDCR_1	((uint)0x00000000)
#define SCC_GSMRL_RDCR_32	((uint)0x0000c000)
#define SCC_GSMRL_RDCR_16	((uint)0x00008000)
#define SCC_GSMRL_RDCR_8	((uint)0x00004000)
#define SCC_GSMRL_RDCR_1	((uint)0x00000000)
#define SCC_GSMRL_RENC_DFMAN	((uint)0x00003000)
#define SCC_GSMRL_RENC_MANCH	((uint)0x00002000)
#define SCC_GSMRL_RENC_FM0	((uint)0x00001000)
#define SCC_GSMRL_RENC_NRZI	((uint)0x00000800)
#define SCC_GSMRL_RENC_NRZ	((uint)0x00000000)
#define SCC_GSMRL_TENC_DFMAN	((uint)0x00000600)
#define SCC_GSMRL_TENC_MANCH	((uint)0x00000400)
#define SCC_GSMRL_TENC_FM0	((uint)0x00000200)
#define SCC_GSMRL_TENC_NRZI	((uint)0x00000100)
#define SCC_GSMRL_TENC_NRZ	((uint)0x00000000)
#define SCC_GSMRL_DIAG_LE	((uint)0x000000c0)	/* Loop and echo */
#define SCC_GSMRL_DIAG_ECHO	((uint)0x00000080)
#define SCC_GSMRL_DIAG_LOOP	((uint)0x00000040)
#define SCC_GSMRL_DIAG_NORM	((uint)0x00000000)
#define SCC_GSMRL_ENR		((uint)0x00000020)
#define SCC_GSMRL_ENT		((uint)0x00000010)
#define SCC_GSMRL_MODE_ENET	((uint)0x0000000c)
#define SCC_GSMRL_MODE_DDCMP	((uint)0x00000009)
#define SCC_GSMRL_MODE_BISYNC	((uint)0x00000008)
#define SCC_GSMRL_MODE_V14	((uint)0x00000007)
#define SCC_GSMRL_MODE_AHDLC	((uint)0x00000006)
#define SCC_GSMRL_MODE_PROFIBUS	((uint)0x00000005)
#define SCC_GSMRL_MODE_UART	((uint)0x00000004)
#define SCC_GSMRL_MODE_SS7	((uint)0x00000003)
#define SCC_GSMRL_MODE_ATALK	((uint)0x00000002)
#define SCC_GSMRL_MODE_HDLC	((uint)0x00000000)

#define SCC_TODR_TOD		((ushort)0x8000)

/* SCC Event and Mask register.
*/
#define	SCCM_TXE	((unsigned char)0x10)
#define	SCCM_BSY	((unsigned char)0x04)
#define	SCCM_TX		((unsigned char)0x02)
#define	SCCM_RX		((unsigned char)0x01)

typedef struct scc_param {
	ushort	scc_rbase;	/* Rx Buffer descriptor base address */
	ushort	scc_tbase;	/* Tx Buffer descriptor base address */
	u_char	scc_rfcr;	/* Rx function code */
	u_char	scc_tfcr;	/* Tx function code */
	ushort	scc_mrblr;	/* Max receive buffer length */
	uint	scc_rstate;	/* Internal */
	uint	scc_idp;	/* Internal */
	ushort	scc_rbptr;	/* Internal */
	ushort	scc_ibc;	/* Internal */
	uint	scc_rxtmp;	/* Internal */
	uint	scc_tstate;	/* Internal */
	uint	scc_tdp;	/* Internal */
	ushort	scc_tbptr;	/* Internal */
	ushort	scc_tbc;	/* Internal */
	uint	scc_txtmp;	/* Internal */
	uint	scc_rcrc;	/* Internal */
	uint	scc_tcrc;	/* Internal */
} sccp_t;


/* Function code bits.
 */
#define SCC_EB	((u_char)0x10)	/* Set big endian byte order */
#define SCC_FC_DMA ((u_char)0x08) /* Set SDMA */

/* CPM Ethernet through SCC1.
 */
typedef struct scc_enet {
	sccp_t	sen_genscc;
	uint	sen_cpres;	/* Preset CRC */
	uint	sen_cmask;	/* Constant mask for CRC */
	uint	sen_crcec;	/* CRC Error counter */
	uint	sen_alec;	/* alignment error counter */
	uint	sen_disfc;	/* discard frame counter */
	ushort	sen_pads;	/* Tx short frame pad character */
	ushort	sen_retlim;	/* Retry limit threshold */
	ushort	sen_retcnt;	/* Retry limit counter */
	ushort	sen_maxflr;	/* maximum frame length register */
	ushort	sen_minflr;	/* minimum frame length register */
	ushort	sen_maxd1;	/* maximum DMA1 length */
	ushort	sen_maxd2;	/* maximum DMA2 length */
	ushort	sen_maxd;	/* Rx max DMA */
	ushort	sen_dmacnt;	/* Rx DMA counter */
	ushort	sen_maxb;	/* Max BD byte count */
	ushort	sen_gaddr1;	/* Group address filter */
	ushort	sen_gaddr2;
	ushort	sen_gaddr3;
	ushort	sen_gaddr4;
	uint	sen_tbuf0data0;	/* Save area 0 - current frame */
	uint	sen_tbuf0data1;	/* Save area 1 - current frame */
	uint	sen_tbuf0rba;	/* Internal */
	uint	sen_tbuf0crc;	/* Internal */
	ushort	sen_tbuf0bcnt;	/* Internal */
	ushort	sen_paddrh;	/* physical address (MSB) */
	ushort	sen_paddrm;
	ushort	sen_paddrl;	/* physical address (LSB) */
	ushort	sen_pper;	/* persistence */
	ushort	sen_rfbdptr;	/* Rx first BD pointer */
	ushort	sen_tfbdptr;	/* Tx first BD pointer */
	ushort	sen_tlbdptr;	/* Tx last BD pointer */
	uint	sen_tbuf1data0;	/* Save area 0 - current frame */
	uint	sen_tbuf1data1;	/* Save area 1 - current frame */
	uint	sen_tbuf1rba;	/* Internal */
	uint	sen_tbuf1crc;	/* Internal */
	ushort	sen_tbuf1bcnt;	/* Internal */
	ushort	sen_txlen;	/* Tx Frame length counter */
	ushort	sen_iaddr1;	/* Individual address filter */
	ushort	sen_iaddr2;
	ushort	sen_iaddr3;
	ushort	sen_iaddr4;
	ushort	sen_boffcnt;	/* Backoff counter */

	/* NOTE: Some versions of the manual have the following items
	 * incorrectly documented.  Below is the proper order.
	 */
	ushort	sen_taddrh;	/* temp address (MSB) */
	ushort	sen_taddrm;
	ushort	sen_taddrl;	/* temp address (LSB) */
} scc_enet_t;



#if defined (CONFIG_UCQUICC)
/* uCquicc has the following signals connected to Ethernet:
 *  68360    - lxt905
 * PA0/RXD1  - rxd
 * PA1/TXD1  - txd
 * PA8/CLK1  - tclk
 * PA9/CLK2  - rclk
 * PC0/!RTS1 - t_en
 * PC1/!CTS1 - col
 * PC5/!CD1  - cd
 */
#define PA_ENET_RXD	PA_RXD1
#define PA_ENET_TXD	PA_TXD1
#define PA_ENET_TCLK	PA_CLK1
#define PA_ENET_RCLK	PA_CLK2
#define PC_ENET_TENA	PC_RTS1
#define PC_ENET_CLSN	PC_CTS1
#define PC_ENET_RENA	PC_CD1

/* Control bits in the SICR to route TCLK (CLK1) and RCLK (CLK2) to
 * SCC1.
 */
#define SICR_ENET_MASK	((uint)0x000000ff)
#define SICR_ENET_CLKRT	((uint)0x0000002c)

#endif /* config_ucquicc */


#ifdef MBX
/* Bits in parallel I/O port registers that have to be set/cleared
 * to configure the pins for SCC1 use.  The TCLK and RCLK seem unique
 * to the MBX860 board.  Any two of the four available clocks could be
 * used, and the MPC860 cookbook manual has an example using different
 * clock pins.
 */
#define PA_ENET_RXD	((ushort)0x0001)
#define PA_ENET_TXD	((ushort)0x0002)
#define PA_ENET_TCLK	((ushort)0x0200)
#define PA_ENET_RCLK	((ushort)0x0800)
#define PC_ENET_TENA	((ushort)0x0001)
#define PC_ENET_CLSN	((ushort)0x0010)
#define PC_ENET_RENA	((ushort)0x0020)

/* Control bits in the SICR to route TCLK (CLK2) and RCLK (CLK4) to
 * SCC1.  Also, make sure GR1 (bit 24) and SC1 (bit 25) are zero.
 */
#define SICR_ENET_MASK	((uint)0x000000ff)
#define SICR_ENET_CLKRT	((uint)0x0000003d)
#endif

#ifdef CONFIG_RPXLITE
/* This ENET stuff is for the MPC850 with ethernet on SCC2.  Some of
 * this may be unique to the RPX-Lite configuration.
 * Note TENA is on Port B.
 */
#define PA_ENET_RXD	((ushort)0x0004)
#define PA_ENET_TXD	((ushort)0x0008)
#define PA_ENET_TCLK	((ushort)0x0200)
#define PA_ENET_RCLK	((ushort)0x0800)
#define PB_ENET_TENA	((uint)0x00002000)
#define PC_ENET_CLSN	((ushort)0x0040)
#define PC_ENET_RENA	((ushort)0x0080)

#define SICR_ENET_MASK	((uint)0x0000ff00)
#define SICR_ENET_CLKRT	((uint)0x00003d00)
#endif

#ifdef CONFIG_BSEIP
/* This ENET stuff is for the MPC823 with ethernet on SCC2.
 * This is unique to the BSE ip-Engine board.
 */
#define PA_ENET_RXD	((ushort)0x0004)
#define PA_ENET_TXD	((ushort)0x0008)
#define PA_ENET_TCLK	((ushort)0x0100)
#define PA_ENET_RCLK	((ushort)0x0200)
#define PB_ENET_TENA	((uint)0x00002000)
#define PC_ENET_CLSN	((ushort)0x0040)
#define PC_ENET_RENA	((ushort)0x0080)

/* BSE uses port B and C bits for PHY control also.
*/
#define PB_BSE_POWERUP	((uint)0x00000004)
#define PB_BSE_FDXDIS	((uint)0x00008000)
#define PC_BSE_LOOPBACK	((ushort)0x0800)

#define SICR_ENET_MASK	((uint)0x0000ff00)
#define SICR_ENET_CLKRT	((uint)0x00002c00)
#endif

/* SCC Event register as used by Ethernet.
*/
#define SCCE_ENET_GRA	((ushort)0x0080)	/* Graceful stop complete */
#define SCCE_ENET_TXE	((ushort)0x0010)	/* Transmit Error */
#define SCCE_ENET_RXF	((ushort)0x0008)	/* Full frame received */
#define SCCE_ENET_BSY	((ushort)0x0004)	/* All incoming buffers full */
#define SCCE_ENET_TXB	((ushort)0x0002)	/* A buffer was transmitted */
#define SCCE_ENET_RXB	((ushort)0x0001)	/* A buffer was received */

/* SCC Mode Register (PMSR) as used by Ethernet.
*/
#define SCC_PMSR_HBC	((ushort)0x8000)	/* Enable heartbeat */
#define SCC_PMSR_FC	((ushort)0x4000)	/* Force collision */
#define SCC_PMSR_RSH	((ushort)0x2000)	/* Receive short frames */
#define SCC_PMSR_IAM	((ushort)0x1000)	/* Check individual hash */
#define SCC_PMSR_ENCRC	((ushort)0x0800)	/* Ethernet CRC mode */
#define SCC_PMSR_PRO	((ushort)0x0200)	/* Promiscuous mode */
#define SCC_PMSR_BRO	((ushort)0x0100)	/* Catch broadcast pkts */
#define SCC_PMSR_SBT	((ushort)0x0080)	/* Special backoff timer */
#define SCC_PMSR_LPB	((ushort)0x0040)	/* Set Loopback mode */
#define SCC_PMSR_SIP	((ushort)0x0020)	/* Sample Input Pins */
#define SCC_PMSR_LCW	((ushort)0x0010)	/* Late collision window */
#define SCC_PMSR_NIB22	((ushort)0x000a)	/* Start frame search */
#define SCC_PMSR_FDE	((ushort)0x0001)	/* Full duplex enable */

/* Buffer descriptor control/status used by Ethernet receive.
*/
#define BD_ENET_RX_EMPTY	((ushort)0x8000)
#define BD_ENET_RX_WRAP		((ushort)0x2000)
#define BD_ENET_RX_INTR		((ushort)0x1000)
#define BD_ENET_RX_LAST		((ushort)0x0800)
#define BD_ENET_RX_FIRST	((ushort)0x0400)
#define BD_ENET_RX_MISS		((ushort)0x0100)
#define BD_ENET_RX_LG		((ushort)0x0020)
#define BD_ENET_RX_NO		((ushort)0x0010)
#define BD_ENET_RX_SH		((ushort)0x0008)
#define BD_ENET_RX_CR		((ushort)0x0004)
#define BD_ENET_RX_OV		((ushort)0x0002)
#define BD_ENET_RX_CL		((ushort)0x0001)
#define BD_ENET_RX_STATS	((ushort)0x013f)	/* All status bits */

/* Buffer descriptor control/status used by Ethernet transmit.
*/
#define BD_ENET_TX_READY	((ushort)0x8000)
#define BD_ENET_TX_PAD		((ushort)0x4000)
#define BD_ENET_TX_WRAP		((ushort)0x2000)
#define BD_ENET_TX_INTR		((ushort)0x1000)
#define BD_ENET_TX_LAST		((ushort)0x0800)
#define BD_ENET_TX_TC		((ushort)0x0400)
#define BD_ENET_TX_DEF		((ushort)0x0200)
#define BD_ENET_TX_HB		((ushort)0x0100)
#define BD_ENET_TX_LC		((ushort)0x0080)
#define BD_ENET_TX_RL		((ushort)0x0040)
#define BD_ENET_TX_RCMASK	((ushort)0x003c)
#define BD_ENET_TX_UN		((ushort)0x0002)
#define BD_ENET_TX_CSL		((ushort)0x0001)
#define BD_ENET_TX_STATS	((ushort)0x03ff)	/* All status bits */

/* SCC as UART
*/
typedef struct scc_uart {
	sccp_t	scc_genscc;
	uint	scc_res1;	/* Reserved */
	uint	scc_res2;	/* Reserved */
	ushort	scc_maxidl;	/* Maximum idle chars */
	ushort	scc_idlc;	/* temp idle counter */
	ushort	scc_brkcr;	/* Break count register */
	ushort	scc_parec;	/* receive parity error counter */
	ushort	scc_frmec;	/* receive framing error counter */
	ushort	scc_nosec;	/* receive noise counter */
	ushort	scc_brkec;	/* receive break condition counter */
	ushort	scc_brkln;	/* last received break length */
	ushort	scc_uaddr1;	/* UART address character 1 */
	ushort	scc_uaddr2;	/* UART address character 2 */
	ushort	scc_rtemp;	/* Temp storage */
	ushort	scc_toseq;	/* Transmit out of sequence char */
	ushort	scc_char1;	/* control character 1 */
	ushort	scc_char2;	/* control character 2 */
	ushort	scc_char3;	/* control character 3 */
	ushort	scc_char4;	/* control character 4 */
	ushort	scc_char5;	/* control character 5 */
	ushort	scc_char6;	/* control character 6 */
	ushort	scc_char7;	/* control character 7 */
	ushort	scc_char8;	/* control character 8 */
	ushort	scc_rccm;	/* receive control character mask */
	ushort	scc_rccr;	/* receive control character register */
	ushort	scc_rlbc;	/* receive last break character */
} scc_uart_t;

/* SCC Event and Mask registers when it is used as a UART.
*/
#define UART_SCCM_GLR		((ushort)0x1000)
#define UART_SCCM_GLT		((ushort)0x0800)
#define UART_SCCM_AB		((ushort)0x0200)
#define UART_SCCM_IDL		((ushort)0x0100)
#define UART_SCCM_GRA		((ushort)0x0080)
#define UART_SCCM_BRKE		((ushort)0x0040)
#define UART_SCCM_BRKS		((ushort)0x0020)
#define UART_SCCM_CCR		((ushort)0x0008)
#define UART_SCCM_BSY		((ushort)0x0004)
#define UART_SCCM_TX		((ushort)0x0002)
#define UART_SCCM_RX		((ushort)0x0001)

/* The SCC PMSR when used as a UART.
*/
#define SCU_PMSR_FLC		((ushort)0x8000)
#define SCU_PMSR_SL		((ushort)0x4000)
#define SCU_PMSR_CL		((ushort)0x3000)
#define SCU_PMSR_UM		((ushort)0x0c00)
#define SCU_PMSR_FRZ		((ushort)0x0200)
#define SCU_PMSR_RZS		((ushort)0x0100)
#define SCU_PMSR_SYN		((ushort)0x0080)
#define SCU_PMSR_DRT		((ushort)0x0040)
#define SCU_PMSR_PEN		((ushort)0x0010)
#define SCU_PMSR_RPM		((ushort)0x000c)
#define SCU_PMSR_REVP		((ushort)0x0008)
#define SCU_PMSR_TPM		((ushort)0x0003)
#define SCU_PMSR_TEVP		((ushort)0x0003)

/* CPM Transparent mode SCC.
 */
typedef struct scc_trans {
	sccp_t	st_genscc;
	uint	st_cpres;	/* Preset CRC */
	uint	st_cmask;	/* Constant mask for CRC */
} scc_trans_t;

#define BD_SCC_TX_LAST		((ushort)0x0800)



/* CPM interrupts.  There are nearly 32 interrupts generated by CPM
 * channels or devices.  All of these are presented to the PPC core
 * as a single interrupt.  The CPM interrupt handler dispatches its
 * own handlers, in a similar fashion to the PPC core handler.  We
 * use the table as defined in the manuals (i.e. no special high
 * priority and SCC1 == SCCa, etc...).
 */
/* #define CPMVEC_NR		32 */
/* #define	CPMVEC_PIO_PC15		((ushort)0x1f) */
/* #define	CPMVEC_SCC1		((ushort)0x1e) */
/* #define	CPMVEC_SCC2		((ushort)0x1d) */
/* #define	CPMVEC_SCC3		((ushort)0x1c) */
/* #define	CPMVEC_SCC4		((ushort)0x1b) */
/* #define	CPMVEC_PIO_PC14		((ushort)0x1a) */
/* #define	CPMVEC_TIMER1		((ushort)0x19) */
/* #define	CPMVEC_PIO_PC13		((ushort)0x18) */
/* #define	CPMVEC_PIO_PC12		((ushort)0x17) */
/* #define	CPMVEC_SDMA_CB_ERR	((ushort)0x16) */
/* #define CPMVEC_IDMA1		((ushort)0x15) */
/* #define CPMVEC_IDMA2		((ushort)0x14) */
/* #define CPMVEC_TIMER2		((ushort)0x12) */
/* #define CPMVEC_RISCTIMER	((ushort)0x11) */
/* #define CPMVEC_I2C		((ushort)0x10) */
/* #define	CPMVEC_PIO_PC11		((ushort)0x0f) */
/* #define	CPMVEC_PIO_PC10		((ushort)0x0e) */
/* #define CPMVEC_TIMER3		((ushort)0x0c) */
/* #define	CPMVEC_PIO_PC9		((ushort)0x0b) */
/* #define	CPMVEC_PIO_PC8		((ushort)0x0a) */
/* #define	CPMVEC_PIO_PC7		((ushort)0x09) */
/* #define CPMVEC_TIMER4		((ushort)0x07) */
/* #define	CPMVEC_PIO_PC6		((ushort)0x06) */
/* #define	CPMVEC_SPI		((ushort)0x05) */
/* #define	CPMVEC_SMC1		((ushort)0x04) */
/* #define	CPMVEC_SMC2		((ushort)0x03) */
/* #define	CPMVEC_PIO_PC5		((ushort)0x02) */
/* #define	CPMVEC_PIO_PC4		((ushort)0x01) */
/* #define	CPMVEC_ERROR		((ushort)0x00) */

extern void cpm_install_handler(int vec, void (*handler)(void *), void *dev_id);

/* CPM interrupt configuration vector.
*/
#define	CICR_SCD_SCC4		((uint)0x00c00000)	/* SCC4 @ SCCd */
#define	CICR_SCC_SCC3		((uint)0x00200000)	/* SCC3 @ SCCc */
#define	CICR_SCB_SCC2		((uint)0x00040000)	/* SCC2 @ SCCb */
#define	CICR_SCA_SCC1		((uint)0x00000000)	/* SCC1 @ SCCa */
#define CICR_IRL_MASK		((uint)0x0000e000)	/* Core interrupt */
#define CICR_HP_MASK		((uint)0x00001f00)	/* Hi-pri int. */
#define CICR_IEN		((uint)0x00000080)	/* Int. enable */
#define CICR_SPS		((uint)0x00000001)	/* SCC Spread */
#endif /* __CPM_360__ */
