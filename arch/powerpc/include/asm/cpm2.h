/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Communication Processor Module v2.
 *
 * This file contains structures and information for the communication
 * processor channels found in the dual port RAM or parameter RAM.
 * All CPM control and status is available through the CPM2 internal
 * memory map.  See immap_cpm2.h for details.
 */
#ifdef __KERNEL__
#ifndef __CPM2__
#define __CPM2__

#include <asm/immap_cpm2.h>
#include <asm/cpm.h>
#include <sysdev/fsl_soc.h>

/* CPM Command register.
*/
#define CPM_CR_RST	((uint)0x80000000)
#define CPM_CR_PAGE	((uint)0x7c000000)
#define CPM_CR_SBLOCK	((uint)0x03e00000)
#define CPM_CR_FLG	((uint)0x00010000)
#define CPM_CR_MCN	((uint)0x00003fc0)
#define CPM_CR_OPCODE	((uint)0x0000000f)

/* Device sub-block and page codes.
*/
#define CPM_CR_SCC1_SBLOCK	(0x04)
#define CPM_CR_SCC2_SBLOCK	(0x05)
#define CPM_CR_SCC3_SBLOCK	(0x06)
#define CPM_CR_SCC4_SBLOCK	(0x07)
#define CPM_CR_SMC1_SBLOCK	(0x08)
#define CPM_CR_SMC2_SBLOCK	(0x09)
#define CPM_CR_SPI_SBLOCK	(0x0a)
#define CPM_CR_I2C_SBLOCK	(0x0b)
#define CPM_CR_TIMER_SBLOCK	(0x0f)
#define CPM_CR_RAND_SBLOCK	(0x0e)
#define CPM_CR_FCC1_SBLOCK	(0x10)
#define CPM_CR_FCC2_SBLOCK	(0x11)
#define CPM_CR_FCC3_SBLOCK	(0x12)
#define CPM_CR_IDMA1_SBLOCK	(0x14)
#define CPM_CR_IDMA2_SBLOCK	(0x15)
#define CPM_CR_IDMA3_SBLOCK	(0x16)
#define CPM_CR_IDMA4_SBLOCK	(0x17)
#define CPM_CR_MCC1_SBLOCK	(0x1c)

#define CPM_CR_FCC_SBLOCK(x)	(x + 0x10)

#define CPM_CR_SCC1_PAGE	(0x00)
#define CPM_CR_SCC2_PAGE	(0x01)
#define CPM_CR_SCC3_PAGE	(0x02)
#define CPM_CR_SCC4_PAGE	(0x03)
#define CPM_CR_SMC1_PAGE	(0x07)
#define CPM_CR_SMC2_PAGE	(0x08)
#define CPM_CR_SPI_PAGE		(0x09)
#define CPM_CR_I2C_PAGE		(0x0a)
#define CPM_CR_TIMER_PAGE	(0x0a)
#define CPM_CR_RAND_PAGE	(0x0a)
#define CPM_CR_FCC1_PAGE	(0x04)
#define CPM_CR_FCC2_PAGE	(0x05)
#define CPM_CR_FCC3_PAGE	(0x06)
#define CPM_CR_IDMA1_PAGE	(0x07)
#define CPM_CR_IDMA2_PAGE	(0x08)
#define CPM_CR_IDMA3_PAGE	(0x09)
#define CPM_CR_IDMA4_PAGE	(0x0a)
#define CPM_CR_MCC1_PAGE	(0x07)
#define CPM_CR_MCC2_PAGE	(0x08)

#define CPM_CR_FCC_PAGE(x)	(x + 0x04)

/* CPM2-specific opcodes (see cpm.h for common opcodes)
*/
#define CPM_CR_START_IDMA	((ushort)0x0009)

#define mk_cr_cmd(PG, SBC, MCN, OP) \
	((PG << 26) | (SBC << 21) | (MCN << 6) | OP)

/* The number of pages of host memory we allocate for CPM.  This is
 * done early in kernel initialization to get physically contiguous
 * pages.
 */
#define NUM_CPM_HOST_PAGES	2

/* Export the base address of the communication processor registers
 * and dual port ram.
 */
extern cpm_cpm2_t __iomem *cpmp; /* Pointer to comm processor */

#define cpm_dpalloc cpm_muram_alloc
#define cpm_dpfree cpm_muram_free
#define cpm_dpram_addr cpm_muram_addr

extern void cpm2_reset(void);

/* Baud rate generators.
*/
#define CPM_BRG_RST		((uint)0x00020000)
#define CPM_BRG_EN		((uint)0x00010000)
#define CPM_BRG_EXTC_INT	((uint)0x00000000)
#define CPM_BRG_EXTC_CLK3_9	((uint)0x00004000)
#define CPM_BRG_EXTC_CLK5_15	((uint)0x00008000)
#define CPM_BRG_ATB		((uint)0x00002000)
#define CPM_BRG_CD_MASK		((uint)0x00001ffe)
#define CPM_BRG_DIV16		((uint)0x00000001)

#define CPM2_BRG_INT_CLK	(get_brgfreq())
#define CPM2_BRG_UART_CLK	(CPM2_BRG_INT_CLK/16)

extern void __cpm2_setbrg(uint brg, uint rate, uint clk, int div16, int src);

/* This function is used by UARTS, or anything else that uses a 16x
 * oversampled clock.
 */
static inline void cpm_setbrg(uint brg, uint rate)
{
	__cpm2_setbrg(brg, rate, CPM2_BRG_UART_CLK, 0, CPM_BRG_EXTC_INT);
}

/* This function is used to set high speed synchronous baud rate
 * clocks.
 */
static inline void cpm2_fastbrg(uint brg, uint rate, int div16)
{
	__cpm2_setbrg(brg, rate, CPM2_BRG_INT_CLK, div16, CPM_BRG_EXTC_INT);
}

/* Parameter RAM offsets from the base.
*/
#define PROFF_SCC1		((uint)0x8000)
#define PROFF_SCC2		((uint)0x8100)
#define PROFF_SCC3		((uint)0x8200)
#define PROFF_SCC4		((uint)0x8300)
#define PROFF_FCC1		((uint)0x8400)
#define PROFF_FCC2		((uint)0x8500)
#define PROFF_FCC3		((uint)0x8600)
#define PROFF_MCC1		((uint)0x8700)
#define PROFF_SMC1_BASE		((uint)0x87fc)
#define PROFF_IDMA1_BASE	((uint)0x87fe)
#define PROFF_MCC2		((uint)0x8800)
#define PROFF_SMC2_BASE		((uint)0x88fc)
#define PROFF_IDMA2_BASE	((uint)0x88fe)
#define PROFF_SPI_BASE		((uint)0x89fc)
#define PROFF_IDMA3_BASE	((uint)0x89fe)
#define PROFF_TIMERS		((uint)0x8ae0)
#define PROFF_REVNUM		((uint)0x8af0)
#define PROFF_RAND		((uint)0x8af8)
#define PROFF_I2C_BASE		((uint)0x8afc)
#define PROFF_IDMA4_BASE	((uint)0x8afe)

#define PROFF_SCC_SIZE		((uint)0x100)
#define PROFF_FCC_SIZE		((uint)0x100)
#define PROFF_SMC_SIZE		((uint)64)

/* The SMCs are relocated to any of the first eight DPRAM pages.
 * We will fix these at the first locations of DPRAM, until we
 * get some microcode patches :-).
 * The parameter ram space for the SMCs is fifty-some bytes, and
 * they are required to start on a 64 byte boundary.
 */
#define PROFF_SMC1	(0)
#define PROFF_SMC2	(64)


/* Define enough so I can at least use the serial port as a UART.
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
	uint	smc_stmp;	/* SDMA Temp */
} smc_uart_t;

/* SMC uart mode register (Internal memory map).
*/
#define SMCMR_REN	((ushort)0x0001)
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

/* SMC Event and Mask register.
*/
#define SMCM_BRKE       ((unsigned char)0x40)   /* When in UART Mode */
#define SMCM_BRK        ((unsigned char)0x10)   /* When in UART Mode */
#define SMCM_TXE	((unsigned char)0x10)
#define SMCM_BSY	((unsigned char)0x04)
#define SMCM_TX		((unsigned char)0x02)
#define SMCM_RX		((unsigned char)0x01)

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
#define SCCM_TXE	((unsigned char)0x10)
#define SCCM_BSY	((unsigned char)0x04)
#define SCCM_TX		((unsigned char)0x02)
#define SCCM_RX		((unsigned char)0x01)

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
#define SCC_EB	((u_char) 0x10)	/* Set big endian byte order */
#define SCC_GBL	((u_char) 0x20) /* Snooping enabled */

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


/* SCC Event register as used by Ethernet.
*/
#define SCCE_ENET_GRA	((ushort)0x0080)	/* Graceful stop complete */
#define SCCE_ENET_TXE	((ushort)0x0010)	/* Transmit Error */
#define SCCE_ENET_RXF	((ushort)0x0008)	/* Full frame received */
#define SCCE_ENET_BSY	((ushort)0x0004)	/* All incoming buffers full */
#define SCCE_ENET_TXB	((ushort)0x0002)	/* A buffer was transmitted */
#define SCCE_ENET_RXB	((ushort)0x0001)	/* A buffer was received */

/* SCC Mode Register (PSMR) as used by Ethernet.
*/
#define SCC_PSMR_HBC	((ushort)0x8000)	/* Enable heartbeat */
#define SCC_PSMR_FC	((ushort)0x4000)	/* Force collision */
#define SCC_PSMR_RSH	((ushort)0x2000)	/* Receive short frames */
#define SCC_PSMR_IAM	((ushort)0x1000)	/* Check individual hash */
#define SCC_PSMR_ENCRC	((ushort)0x0800)	/* Ethernet CRC mode */
#define SCC_PSMR_PRO	((ushort)0x0200)	/* Promiscuous mode */
#define SCC_PSMR_BRO	((ushort)0x0100)	/* Catch broadcast pkts */
#define SCC_PSMR_SBT	((ushort)0x0080)	/* Special backoff timer */
#define SCC_PSMR_LPB	((ushort)0x0040)	/* Set Loopback mode */
#define SCC_PSMR_SIP	((ushort)0x0020)	/* Sample Input Pins */
#define SCC_PSMR_LCW	((ushort)0x0010)	/* Late collision window */
#define SCC_PSMR_NIB22	((ushort)0x000a)	/* Start frame search */
#define SCC_PSMR_FDE	((ushort)0x0001)	/* Full duplex enable */

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

/* The SCC PSMR when used as a UART.
*/
#define SCU_PSMR_FLC		((ushort)0x8000)
#define SCU_PSMR_SL		((ushort)0x4000)
#define SCU_PSMR_CL		((ushort)0x3000)
#define SCU_PSMR_UM		((ushort)0x0c00)
#define SCU_PSMR_FRZ		((ushort)0x0200)
#define SCU_PSMR_RZS		((ushort)0x0100)
#define SCU_PSMR_SYN		((ushort)0x0080)
#define SCU_PSMR_DRT		((ushort)0x0040)
#define SCU_PSMR_PEN		((ushort)0x0010)
#define SCU_PSMR_RPM		((ushort)0x000c)
#define SCU_PSMR_REVP		((ushort)0x0008)
#define SCU_PSMR_TPM		((ushort)0x0003)
#define SCU_PSMR_TEVP		((ushort)0x0002)

/* CPM Transparent mode SCC.
 */
typedef struct scc_trans {
	sccp_t	st_genscc;
	uint	st_cpres;	/* Preset CRC */
	uint	st_cmask;	/* Constant mask for CRC */
} scc_trans_t;

/* How about some FCCs.....
*/
#define FCC_GFMR_DIAG_NORM	((uint)0x00000000)
#define FCC_GFMR_DIAG_LE	((uint)0x40000000)
#define FCC_GFMR_DIAG_AE	((uint)0x80000000)
#define FCC_GFMR_DIAG_ALE	((uint)0xc0000000)
#define FCC_GFMR_TCI		((uint)0x20000000)
#define FCC_GFMR_TRX		((uint)0x10000000)
#define FCC_GFMR_TTX		((uint)0x08000000)
#define FCC_GFMR_CDP		((uint)0x04000000)
#define FCC_GFMR_CTSP		((uint)0x02000000)
#define FCC_GFMR_CDS		((uint)0x01000000)
#define FCC_GFMR_CTSS		((uint)0x00800000)
#define FCC_GFMR_SYNL_NONE	((uint)0x00000000)
#define FCC_GFMR_SYNL_AUTO	((uint)0x00004000)
#define FCC_GFMR_SYNL_8		((uint)0x00008000)
#define FCC_GFMR_SYNL_16	((uint)0x0000c000)
#define FCC_GFMR_RTSM		((uint)0x00002000)
#define FCC_GFMR_RENC_NRZ	((uint)0x00000000)
#define FCC_GFMR_RENC_NRZI	((uint)0x00000800)
#define FCC_GFMR_REVD		((uint)0x00000400)
#define FCC_GFMR_TENC_NRZ	((uint)0x00000000)
#define FCC_GFMR_TENC_NRZI	((uint)0x00000100)
#define FCC_GFMR_TCRC_16	((uint)0x00000000)
#define FCC_GFMR_TCRC_32	((uint)0x00000080)
#define FCC_GFMR_ENR		((uint)0x00000020)
#define FCC_GFMR_ENT		((uint)0x00000010)
#define FCC_GFMR_MODE_ENET	((uint)0x0000000c)
#define FCC_GFMR_MODE_ATM	((uint)0x0000000a)
#define FCC_GFMR_MODE_HDLC	((uint)0x00000000)

/* Generic FCC parameter ram.
*/
typedef struct fcc_param {
	ushort	fcc_riptr;	/* Rx Internal temp pointer */
	ushort	fcc_tiptr;	/* Tx Internal temp pointer */
	ushort	fcc_res1;
	ushort	fcc_mrblr;	/* Max receive buffer length, mod 32 bytes */
	uint	fcc_rstate;	/* Upper byte is Func code, must be set */
	uint	fcc_rbase;	/* Receive BD base */
	ushort	fcc_rbdstat;	/* RxBD status */
	ushort	fcc_rbdlen;	/* RxBD down counter */
	uint	fcc_rdptr;	/* RxBD internal data pointer */
	uint	fcc_tstate;	/* Upper byte is Func code, must be set */
	uint	fcc_tbase;	/* Transmit BD base */
	ushort	fcc_tbdstat;	/* TxBD status */
	ushort	fcc_tbdlen;	/* TxBD down counter */
	uint	fcc_tdptr;	/* TxBD internal data pointer */
	uint	fcc_rbptr;	/* Rx BD Internal buf pointer */
	uint	fcc_tbptr;	/* Tx BD Internal buf pointer */
	uint	fcc_rcrc;	/* Rx temp CRC */
	uint	fcc_res2;
	uint	fcc_tcrc;	/* Tx temp CRC */
} fccp_t;


/* Ethernet controller through FCC.
*/
typedef struct fcc_enet {
	fccp_t	fen_genfcc;
	uint	fen_statbuf;	/* Internal status buffer */
	uint	fen_camptr;	/* CAM address */
	uint	fen_cmask;	/* Constant mask for CRC */
	uint	fen_cpres;	/* Preset CRC */
	uint	fen_crcec;	/* CRC Error counter */
	uint	fen_alec;	/* alignment error counter */
	uint	fen_disfc;	/* discard frame counter */
	ushort	fen_retlim;	/* Retry limit */
	ushort	fen_retcnt;	/* Retry counter */
	ushort	fen_pper;	/* Persistence */
	ushort	fen_boffcnt;	/* backoff counter */
	uint	fen_gaddrh;	/* Group address filter, high 32-bits */
	uint	fen_gaddrl;	/* Group address filter, low 32-bits */
	ushort	fen_tfcstat;	/* out of sequence TxBD */
	ushort	fen_tfclen;
	uint	fen_tfcptr;
	ushort	fen_mflr;	/* Maximum frame length (1518) */
	ushort	fen_paddrh;	/* MAC address */
	ushort	fen_paddrm;
	ushort	fen_paddrl;
	ushort	fen_ibdcount;	/* Internal BD counter */
	ushort	fen_ibdstart;	/* Internal BD start pointer */
	ushort	fen_ibdend;	/* Internal BD end pointer */
	ushort	fen_txlen;	/* Internal Tx frame length counter */
	uint	fen_ibdbase[8]; /* Internal use */
	uint	fen_iaddrh;	/* Individual address filter */
	uint	fen_iaddrl;
	ushort	fen_minflr;	/* Minimum frame length (64) */
	ushort	fen_taddrh;	/* Filter transfer MAC address */
	ushort	fen_taddrm;
	ushort	fen_taddrl;
	ushort	fen_padptr;	/* Pointer to pad byte buffer */
	ushort	fen_cftype;	/* control frame type */
	ushort	fen_cfrange;	/* control frame range */
	ushort	fen_maxb;	/* maximum BD count */
	ushort	fen_maxd1;	/* Max DMA1 length (1520) */
	ushort	fen_maxd2;	/* Max DMA2 length (1520) */
	ushort	fen_maxd;	/* internal max DMA count */
	ushort	fen_dmacnt;	/* internal DMA counter */
	uint	fen_octc;	/* Total octect counter */
	uint	fen_colc;	/* Total collision counter */
	uint	fen_broc;	/* Total broadcast packet counter */
	uint	fen_mulc;	/* Total multicast packet count */
	uint	fen_uspc;	/* Total packets < 64 bytes */
	uint	fen_frgc;	/* Total packets < 64 bytes with errors */
	uint	fen_ospc;	/* Total packets > 1518 */
	uint	fen_jbrc;	/* Total packets > 1518 with errors */
	uint	fen_p64c;	/* Total packets == 64 bytes */
	uint	fen_p65c;	/* Total packets 64 < bytes <= 127 */
	uint	fen_p128c;	/* Total packets 127 < bytes <= 255 */
	uint	fen_p256c;	/* Total packets 256 < bytes <= 511 */
	uint	fen_p512c;	/* Total packets 512 < bytes <= 1023 */
	uint	fen_p1024c;	/* Total packets 1024 < bytes <= 1518 */
	uint	fen_cambuf;	/* Internal CAM buffer pointer */
	ushort	fen_rfthr;	/* Received frames threshold */
	ushort	fen_rfcnt;	/* Received frames count */
} fcc_enet_t;

/* FCC Event/Mask register as used by Ethernet.
*/
#define FCC_ENET_GRA	((ushort)0x0080)	/* Graceful stop complete */
#define FCC_ENET_RXC	((ushort)0x0040)	/* Control Frame Received */
#define FCC_ENET_TXC	((ushort)0x0020)	/* Out of seq. Tx sent */
#define FCC_ENET_TXE	((ushort)0x0010)	/* Transmit Error */
#define FCC_ENET_RXF	((ushort)0x0008)	/* Full frame received */
#define FCC_ENET_BSY	((ushort)0x0004)	/* Busy.  Rx Frame dropped */
#define FCC_ENET_TXB	((ushort)0x0002)	/* A buffer was transmitted */
#define FCC_ENET_RXB	((ushort)0x0001)	/* A buffer was received */

/* FCC Mode Register (FPSMR) as used by Ethernet.
*/
#define FCC_PSMR_HBC	((uint)0x80000000)	/* Enable heartbeat */
#define FCC_PSMR_FC	((uint)0x40000000)	/* Force Collision */
#define FCC_PSMR_SBT	((uint)0x20000000)	/* Stop backoff timer */
#define FCC_PSMR_LPB	((uint)0x10000000)	/* Local protect. 1 = FDX */
#define FCC_PSMR_LCW	((uint)0x08000000)	/* Late collision select */
#define FCC_PSMR_FDE	((uint)0x04000000)	/* Full Duplex Enable */
#define FCC_PSMR_MON	((uint)0x02000000)	/* RMON Enable */
#define FCC_PSMR_PRO	((uint)0x00400000)	/* Promiscuous Enable */
#define FCC_PSMR_FCE	((uint)0x00200000)	/* Flow Control Enable */
#define FCC_PSMR_RSH	((uint)0x00100000)	/* Receive Short Frames */
#define FCC_PSMR_CAM	((uint)0x00000400)	/* CAM enable */
#define FCC_PSMR_BRO	((uint)0x00000200)	/* Broadcast pkt discard */
#define FCC_PSMR_ENCRC	((uint)0x00000080)	/* Use 32-bit CRC */

/* IIC parameter RAM.
*/
typedef struct iic {
	ushort	iic_rbase;	/* Rx Buffer descriptor base address */
	ushort	iic_tbase;	/* Tx Buffer descriptor base address */
	u_char	iic_rfcr;	/* Rx function code */
	u_char	iic_tfcr;	/* Tx function code */
	ushort	iic_mrblr;	/* Max receive buffer length */
	uint	iic_rstate;	/* Internal */
	uint	iic_rdp;	/* Internal */
	ushort	iic_rbptr;	/* Internal */
	ushort	iic_rbc;	/* Internal */
	uint	iic_rxtmp;	/* Internal */
	uint	iic_tstate;	/* Internal */
	uint	iic_tdp;	/* Internal */
	ushort	iic_tbptr;	/* Internal */
	ushort	iic_tbc;	/* Internal */
	uint	iic_txtmp;	/* Internal */
} iic_t;

/* IDMA parameter RAM
*/
typedef struct idma {
	ushort ibase;		/* IDMA buffer descriptor table base address */
	ushort dcm;		/* DMA channel mode */
	ushort ibdptr;		/* IDMA current buffer descriptor pointer */
	ushort dpr_buf;		/* IDMA transfer buffer base address */
	ushort buf_inv;		/* internal buffer inventory */
	ushort ss_max;		/* steady-state maximum transfer size */
	ushort dpr_in_ptr;	/* write pointer inside the internal buffer */
	ushort sts;		/* source transfer size */
	ushort dpr_out_ptr;	/* read pointer inside the internal buffer */
	ushort seob;		/* source end of burst */
	ushort deob;		/* destination end of burst */
	ushort dts;		/* destination transfer size */
	ushort ret_add;		/* return address when working in ERM=1 mode */
	ushort res0;		/* reserved */
	uint   bd_cnt;		/* internal byte count */
	uint   s_ptr;		/* source internal data pointer */
	uint   d_ptr;		/* destination internal data pointer */
	uint   istate;		/* internal state */
	u_char res1[20];	/* pad to 64-byte length */
} idma_t;

/* DMA channel mode bit fields
*/
#define IDMA_DCM_FB		((ushort)0x8000) /* fly-by mode */
#define IDMA_DCM_LP		((ushort)0x4000) /* low priority */
#define IDMA_DCM_TC2		((ushort)0x0400) /* value driven on TC[2] */
#define IDMA_DCM_DMA_WRAP_MASK	((ushort)0x01c0) /* mask for DMA wrap */
#define IDMA_DCM_DMA_WRAP_64	((ushort)0x0000) /* 64-byte DMA xfer buffer */
#define IDMA_DCM_DMA_WRAP_128	((ushort)0x0040) /* 128-byte DMA xfer buffer */
#define IDMA_DCM_DMA_WRAP_256	((ushort)0x0080) /* 256-byte DMA xfer buffer */
#define IDMA_DCM_DMA_WRAP_512	((ushort)0x00c0) /* 512-byte DMA xfer buffer */
#define IDMA_DCM_DMA_WRAP_1024	((ushort)0x0100) /* 1024-byte DMA xfer buffer */
#define IDMA_DCM_DMA_WRAP_2048	((ushort)0x0140) /* 2048-byte DMA xfer buffer */
#define IDMA_DCM_SINC		((ushort)0x0020) /* source inc addr */
#define IDMA_DCM_DINC		((ushort)0x0010) /* destination inc addr */
#define IDMA_DCM_ERM		((ushort)0x0008) /* external request mode */
#define IDMA_DCM_DT		((ushort)0x0004) /* DONE treatment */
#define IDMA_DCM_SD_MASK	((ushort)0x0003) /* mask for SD bit field */
#define IDMA_DCM_SD_MEM2MEM	((ushort)0x0000) /* memory-to-memory xfer */
#define IDMA_DCM_SD_PER2MEM	((ushort)0x0002) /* peripheral-to-memory xfer */
#define IDMA_DCM_SD_MEM2PER	((ushort)0x0001) /* memory-to-peripheral xfer */

/* IDMA Buffer Descriptors
*/
typedef struct idma_bd {
	uint flags;
	uint len;	/* data length */
	uint src;	/* source data buffer pointer */
	uint dst;	/* destination data buffer pointer */
} idma_bd_t;

/* IDMA buffer descriptor flag bit fields
*/
#define IDMA_BD_V	((uint)0x80000000)	/* valid */
#define IDMA_BD_W	((uint)0x20000000)	/* wrap */
#define IDMA_BD_I	((uint)0x10000000)	/* interrupt */
#define IDMA_BD_L	((uint)0x08000000)	/* last */
#define IDMA_BD_CM	((uint)0x02000000)	/* continuous mode */
#define IDMA_BD_SDN	((uint)0x00400000)	/* source done */
#define IDMA_BD_DDN	((uint)0x00200000)	/* destination done */
#define IDMA_BD_DGBL	((uint)0x00100000)	/* destination global */
#define IDMA_BD_DBO_LE	((uint)0x00040000)	/* little-end dest byte order */
#define IDMA_BD_DBO_BE	((uint)0x00080000)	/* big-end dest byte order */
#define IDMA_BD_DDTB	((uint)0x00010000)	/* destination data bus */
#define IDMA_BD_SGBL	((uint)0x00002000)	/* source global */
#define IDMA_BD_SBO_LE	((uint)0x00000800)	/* little-end src byte order */
#define IDMA_BD_SBO_BE	((uint)0x00001000)	/* big-end src byte order */
#define IDMA_BD_SDTB	((uint)0x00000200)	/* source data bus */

/* per-channel IDMA registers
*/
typedef struct im_idma {
	u_char idsr;			/* IDMAn event status register */
	u_char res0[3];
	u_char idmr;			/* IDMAn event mask register */
	u_char res1[3];
} im_idma_t;

/* IDMA event register bit fields
*/
#define IDMA_EVENT_SC	((unsigned char)0x08)	/* stop completed */
#define IDMA_EVENT_OB	((unsigned char)0x04)	/* out of buffers */
#define IDMA_EVENT_EDN	((unsigned char)0x02)	/* external DONE asserted */
#define IDMA_EVENT_BC	((unsigned char)0x01)	/* buffer descriptor complete */

/* RISC Controller Configuration Register (RCCR) bit fields
*/
#define RCCR_TIME	((uint)0x80000000) /* timer enable */
#define RCCR_TIMEP_MASK	((uint)0x3f000000) /* mask for timer period bit field */
#define RCCR_DR0M	((uint)0x00800000) /* IDMA0 request mode */
#define RCCR_DR1M	((uint)0x00400000) /* IDMA1 request mode */
#define RCCR_DR2M	((uint)0x00000080) /* IDMA2 request mode */
#define RCCR_DR3M	((uint)0x00000040) /* IDMA3 request mode */
#define RCCR_DR0QP_MASK	((uint)0x00300000) /* mask for IDMA0 req priority */
#define RCCR_DR0QP_HIGH ((uint)0x00000000) /* IDMA0 has high req priority */
#define RCCR_DR0QP_MED	((uint)0x00100000) /* IDMA0 has medium req priority */
#define RCCR_DR0QP_LOW	((uint)0x00200000) /* IDMA0 has low req priority */
#define RCCR_DR1QP_MASK	((uint)0x00030000) /* mask for IDMA1 req priority */
#define RCCR_DR1QP_HIGH ((uint)0x00000000) /* IDMA1 has high req priority */
#define RCCR_DR1QP_MED	((uint)0x00010000) /* IDMA1 has medium req priority */
#define RCCR_DR1QP_LOW	((uint)0x00020000) /* IDMA1 has low req priority */
#define RCCR_DR2QP_MASK	((uint)0x00000030) /* mask for IDMA2 req priority */
#define RCCR_DR2QP_HIGH ((uint)0x00000000) /* IDMA2 has high req priority */
#define RCCR_DR2QP_MED	((uint)0x00000010) /* IDMA2 has medium req priority */
#define RCCR_DR2QP_LOW	((uint)0x00000020) /* IDMA2 has low req priority */
#define RCCR_DR3QP_MASK	((uint)0x00000003) /* mask for IDMA3 req priority */
#define RCCR_DR3QP_HIGH ((uint)0x00000000) /* IDMA3 has high req priority */
#define RCCR_DR3QP_MED	((uint)0x00000001) /* IDMA3 has medium req priority */
#define RCCR_DR3QP_LOW	((uint)0x00000002) /* IDMA3 has low req priority */
#define RCCR_EIE	((uint)0x00080000) /* external interrupt enable */
#define RCCR_SCD	((uint)0x00040000) /* scheduler configuration */
#define RCCR_ERAM_MASK	((uint)0x0000e000) /* mask for enable RAM microcode */
#define RCCR_ERAM_0KB	((uint)0x00000000) /* use 0KB of dpram for microcode */
#define RCCR_ERAM_2KB	((uint)0x00002000) /* use 2KB of dpram for microcode */
#define RCCR_ERAM_4KB	((uint)0x00004000) /* use 4KB of dpram for microcode */
#define RCCR_ERAM_6KB	((uint)0x00006000) /* use 6KB of dpram for microcode */
#define RCCR_ERAM_8KB	((uint)0x00008000) /* use 8KB of dpram for microcode */
#define RCCR_ERAM_10KB	((uint)0x0000a000) /* use 10KB of dpram for microcode */
#define RCCR_ERAM_12KB	((uint)0x0000c000) /* use 12KB of dpram for microcode */
#define RCCR_EDM0	((uint)0x00000800) /* DREQ0 edge detect mode */
#define RCCR_EDM1	((uint)0x00000400) /* DREQ1 edge detect mode */
#define RCCR_EDM2	((uint)0x00000200) /* DREQ2 edge detect mode */
#define RCCR_EDM3	((uint)0x00000100) /* DREQ3 edge detect mode */
#define RCCR_DEM01	((uint)0x00000008) /* DONE0/DONE1 edge detect mode */
#define RCCR_DEM23	((uint)0x00000004) /* DONE2/DONE3 edge detect mode */

/*-----------------------------------------------------------------------
 * CMXFCR - CMX FCC Clock Route Register
 */
#define CMXFCR_FC1         0x40000000   /* FCC1 connection              */
#define CMXFCR_RF1CS_MSK   0x38000000   /* Receive FCC1 Clock Source Mask */
#define CMXFCR_TF1CS_MSK   0x07000000   /* Transmit FCC1 Clock Source Mask */
#define CMXFCR_FC2         0x00400000   /* FCC2 connection              */
#define CMXFCR_RF2CS_MSK   0x00380000   /* Receive FCC2 Clock Source Mask */
#define CMXFCR_TF2CS_MSK   0x00070000   /* Transmit FCC2 Clock Source Mask */
#define CMXFCR_FC3         0x00004000   /* FCC3 connection              */
#define CMXFCR_RF3CS_MSK   0x00003800   /* Receive FCC3 Clock Source Mask */
#define CMXFCR_TF3CS_MSK   0x00000700   /* Transmit FCC3 Clock Source Mask */

#define CMXFCR_RF1CS_BRG5  0x00000000   /* Receive FCC1 Clock Source is BRG5 */
#define CMXFCR_RF1CS_BRG6  0x08000000   /* Receive FCC1 Clock Source is BRG6 */
#define CMXFCR_RF1CS_BRG7  0x10000000   /* Receive FCC1 Clock Source is BRG7 */
#define CMXFCR_RF1CS_BRG8  0x18000000   /* Receive FCC1 Clock Source is BRG8 */
#define CMXFCR_RF1CS_CLK9  0x20000000   /* Receive FCC1 Clock Source is CLK9 */
#define CMXFCR_RF1CS_CLK10 0x28000000   /* Receive FCC1 Clock Source is CLK10 */
#define CMXFCR_RF1CS_CLK11 0x30000000   /* Receive FCC1 Clock Source is CLK11 */
#define CMXFCR_RF1CS_CLK12 0x38000000   /* Receive FCC1 Clock Source is CLK12 */

#define CMXFCR_TF1CS_BRG5  0x00000000   /* Transmit FCC1 Clock Source is BRG5 */
#define CMXFCR_TF1CS_BRG6  0x01000000   /* Transmit FCC1 Clock Source is BRG6 */
#define CMXFCR_TF1CS_BRG7  0x02000000   /* Transmit FCC1 Clock Source is BRG7 */
#define CMXFCR_TF1CS_BRG8  0x03000000   /* Transmit FCC1 Clock Source is BRG8 */
#define CMXFCR_TF1CS_CLK9  0x04000000   /* Transmit FCC1 Clock Source is CLK9 */
#define CMXFCR_TF1CS_CLK10 0x05000000   /* Transmit FCC1 Clock Source is CLK10 */
#define CMXFCR_TF1CS_CLK11 0x06000000   /* Transmit FCC1 Clock Source is CLK11 */
#define CMXFCR_TF1CS_CLK12 0x07000000   /* Transmit FCC1 Clock Source is CLK12 */

#define CMXFCR_RF2CS_BRG5  0x00000000   /* Receive FCC2 Clock Source is BRG5 */
#define CMXFCR_RF2CS_BRG6  0x00080000   /* Receive FCC2 Clock Source is BRG6 */
#define CMXFCR_RF2CS_BRG7  0x00100000   /* Receive FCC2 Clock Source is BRG7 */
#define CMXFCR_RF2CS_BRG8  0x00180000   /* Receive FCC2 Clock Source is BRG8 */
#define CMXFCR_RF2CS_CLK13 0x00200000   /* Receive FCC2 Clock Source is CLK13 */
#define CMXFCR_RF2CS_CLK14 0x00280000   /* Receive FCC2 Clock Source is CLK14 */
#define CMXFCR_RF2CS_CLK15 0x00300000   /* Receive FCC2 Clock Source is CLK15 */
#define CMXFCR_RF2CS_CLK16 0x00380000   /* Receive FCC2 Clock Source is CLK16 */

#define CMXFCR_TF2CS_BRG5  0x00000000   /* Transmit FCC2 Clock Source is BRG5 */
#define CMXFCR_TF2CS_BRG6  0x00010000   /* Transmit FCC2 Clock Source is BRG6 */
#define CMXFCR_TF2CS_BRG7  0x00020000   /* Transmit FCC2 Clock Source is BRG7 */
#define CMXFCR_TF2CS_BRG8  0x00030000   /* Transmit FCC2 Clock Source is BRG8 */
#define CMXFCR_TF2CS_CLK13 0x00040000   /* Transmit FCC2 Clock Source is CLK13 */
#define CMXFCR_TF2CS_CLK14 0x00050000   /* Transmit FCC2 Clock Source is CLK14 */
#define CMXFCR_TF2CS_CLK15 0x00060000   /* Transmit FCC2 Clock Source is CLK15 */
#define CMXFCR_TF2CS_CLK16 0x00070000   /* Transmit FCC2 Clock Source is CLK16 */

#define CMXFCR_RF3CS_BRG5  0x00000000   /* Receive FCC3 Clock Source is BRG5 */
#define CMXFCR_RF3CS_BRG6  0x00000800   /* Receive FCC3 Clock Source is BRG6 */
#define CMXFCR_RF3CS_BRG7  0x00001000   /* Receive FCC3 Clock Source is BRG7 */
#define CMXFCR_RF3CS_BRG8  0x00001800   /* Receive FCC3 Clock Source is BRG8 */
#define CMXFCR_RF3CS_CLK13 0x00002000   /* Receive FCC3 Clock Source is CLK13 */
#define CMXFCR_RF3CS_CLK14 0x00002800   /* Receive FCC3 Clock Source is CLK14 */
#define CMXFCR_RF3CS_CLK15 0x00003000   /* Receive FCC3 Clock Source is CLK15 */
#define CMXFCR_RF3CS_CLK16 0x00003800   /* Receive FCC3 Clock Source is CLK16 */

#define CMXFCR_TF3CS_BRG5  0x00000000   /* Transmit FCC3 Clock Source is BRG5 */
#define CMXFCR_TF3CS_BRG6  0x00000100   /* Transmit FCC3 Clock Source is BRG6 */
#define CMXFCR_TF3CS_BRG7  0x00000200   /* Transmit FCC3 Clock Source is BRG7 */
#define CMXFCR_TF3CS_BRG8  0x00000300   /* Transmit FCC3 Clock Source is BRG8 */
#define CMXFCR_TF3CS_CLK13 0x00000400   /* Transmit FCC3 Clock Source is CLK13 */
#define CMXFCR_TF3CS_CLK14 0x00000500   /* Transmit FCC3 Clock Source is CLK14 */
#define CMXFCR_TF3CS_CLK15 0x00000600   /* Transmit FCC3 Clock Source is CLK15 */
#define CMXFCR_TF3CS_CLK16 0x00000700   /* Transmit FCC3 Clock Source is CLK16 */

/*-----------------------------------------------------------------------
 * CMXSCR - CMX SCC Clock Route Register
 */
#define CMXSCR_GR1         0x80000000   /* Grant Support of SCC1        */
#define CMXSCR_SC1         0x40000000   /* SCC1 connection              */
#define CMXSCR_RS1CS_MSK   0x38000000   /* Receive SCC1 Clock Source Mask */
#define CMXSCR_TS1CS_MSK   0x07000000   /* Transmit SCC1 Clock Source Mask */
#define CMXSCR_GR2         0x00800000   /* Grant Support of SCC2        */
#define CMXSCR_SC2         0x00400000   /* SCC2 connection              */
#define CMXSCR_RS2CS_MSK   0x00380000   /* Receive SCC2 Clock Source Mask */
#define CMXSCR_TS2CS_MSK   0x00070000   /* Transmit SCC2 Clock Source Mask */
#define CMXSCR_GR3         0x00008000   /* Grant Support of SCC3        */
#define CMXSCR_SC3         0x00004000   /* SCC3 connection              */
#define CMXSCR_RS3CS_MSK   0x00003800   /* Receive SCC3 Clock Source Mask */
#define CMXSCR_TS3CS_MSK   0x00000700   /* Transmit SCC3 Clock Source Mask */
#define CMXSCR_GR4         0x00000080   /* Grant Support of SCC4        */
#define CMXSCR_SC4         0x00000040   /* SCC4 connection              */
#define CMXSCR_RS4CS_MSK   0x00000038   /* Receive SCC4 Clock Source Mask */
#define CMXSCR_TS4CS_MSK   0x00000007   /* Transmit SCC4 Clock Source Mask */

#define CMXSCR_RS1CS_BRG1  0x00000000   /* SCC1 Rx Clock Source is BRG1 */
#define CMXSCR_RS1CS_BRG2  0x08000000   /* SCC1 Rx Clock Source is BRG2 */
#define CMXSCR_RS1CS_BRG3  0x10000000   /* SCC1 Rx Clock Source is BRG3 */
#define CMXSCR_RS1CS_BRG4  0x18000000   /* SCC1 Rx Clock Source is BRG4 */
#define CMXSCR_RS1CS_CLK11 0x20000000   /* SCC1 Rx Clock Source is CLK11 */
#define CMXSCR_RS1CS_CLK12 0x28000000   /* SCC1 Rx Clock Source is CLK12 */
#define CMXSCR_RS1CS_CLK3  0x30000000   /* SCC1 Rx Clock Source is CLK3 */
#define CMXSCR_RS1CS_CLK4  0x38000000   /* SCC1 Rx Clock Source is CLK4 */

#define CMXSCR_TS1CS_BRG1  0x00000000   /* SCC1 Tx Clock Source is BRG1 */
#define CMXSCR_TS1CS_BRG2  0x01000000   /* SCC1 Tx Clock Source is BRG2 */
#define CMXSCR_TS1CS_BRG3  0x02000000   /* SCC1 Tx Clock Source is BRG3 */
#define CMXSCR_TS1CS_BRG4  0x03000000   /* SCC1 Tx Clock Source is BRG4 */
#define CMXSCR_TS1CS_CLK11 0x04000000   /* SCC1 Tx Clock Source is CLK11 */
#define CMXSCR_TS1CS_CLK12 0x05000000   /* SCC1 Tx Clock Source is CLK12 */
#define CMXSCR_TS1CS_CLK3  0x06000000   /* SCC1 Tx Clock Source is CLK3 */
#define CMXSCR_TS1CS_CLK4  0x07000000   /* SCC1 Tx Clock Source is CLK4 */

#define CMXSCR_RS2CS_BRG1  0x00000000   /* SCC2 Rx Clock Source is BRG1 */
#define CMXSCR_RS2CS_BRG2  0x00080000   /* SCC2 Rx Clock Source is BRG2 */
#define CMXSCR_RS2CS_BRG3  0x00100000   /* SCC2 Rx Clock Source is BRG3 */
#define CMXSCR_RS2CS_BRG4  0x00180000   /* SCC2 Rx Clock Source is BRG4 */
#define CMXSCR_RS2CS_CLK11 0x00200000   /* SCC2 Rx Clock Source is CLK11 */
#define CMXSCR_RS2CS_CLK12 0x00280000   /* SCC2 Rx Clock Source is CLK12 */
#define CMXSCR_RS2CS_CLK3  0x00300000   /* SCC2 Rx Clock Source is CLK3 */
#define CMXSCR_RS2CS_CLK4  0x00380000   /* SCC2 Rx Clock Source is CLK4 */

#define CMXSCR_TS2CS_BRG1  0x00000000   /* SCC2 Tx Clock Source is BRG1 */
#define CMXSCR_TS2CS_BRG2  0x00010000   /* SCC2 Tx Clock Source is BRG2 */
#define CMXSCR_TS2CS_BRG3  0x00020000   /* SCC2 Tx Clock Source is BRG3 */
#define CMXSCR_TS2CS_BRG4  0x00030000   /* SCC2 Tx Clock Source is BRG4 */
#define CMXSCR_TS2CS_CLK11 0x00040000   /* SCC2 Tx Clock Source is CLK11 */
#define CMXSCR_TS2CS_CLK12 0x00050000   /* SCC2 Tx Clock Source is CLK12 */
#define CMXSCR_TS2CS_CLK3  0x00060000   /* SCC2 Tx Clock Source is CLK3 */
#define CMXSCR_TS2CS_CLK4  0x00070000   /* SCC2 Tx Clock Source is CLK4 */

#define CMXSCR_RS3CS_BRG1  0x00000000   /* SCC3 Rx Clock Source is BRG1 */
#define CMXSCR_RS3CS_BRG2  0x00000800   /* SCC3 Rx Clock Source is BRG2 */
#define CMXSCR_RS3CS_BRG3  0x00001000   /* SCC3 Rx Clock Source is BRG3 */
#define CMXSCR_RS3CS_BRG4  0x00001800   /* SCC3 Rx Clock Source is BRG4 */
#define CMXSCR_RS3CS_CLK5  0x00002000   /* SCC3 Rx Clock Source is CLK5 */
#define CMXSCR_RS3CS_CLK6  0x00002800   /* SCC3 Rx Clock Source is CLK6 */
#define CMXSCR_RS3CS_CLK7  0x00003000   /* SCC3 Rx Clock Source is CLK7 */
#define CMXSCR_RS3CS_CLK8  0x00003800   /* SCC3 Rx Clock Source is CLK8 */

#define CMXSCR_TS3CS_BRG1  0x00000000   /* SCC3 Tx Clock Source is BRG1 */
#define CMXSCR_TS3CS_BRG2  0x00000100   /* SCC3 Tx Clock Source is BRG2 */
#define CMXSCR_TS3CS_BRG3  0x00000200   /* SCC3 Tx Clock Source is BRG3 */
#define CMXSCR_TS3CS_BRG4  0x00000300   /* SCC3 Tx Clock Source is BRG4 */
#define CMXSCR_TS3CS_CLK5  0x00000400   /* SCC3 Tx Clock Source is CLK5 */
#define CMXSCR_TS3CS_CLK6  0x00000500   /* SCC3 Tx Clock Source is CLK6 */
#define CMXSCR_TS3CS_CLK7  0x00000600   /* SCC3 Tx Clock Source is CLK7 */
#define CMXSCR_TS3CS_CLK8  0x00000700   /* SCC3 Tx Clock Source is CLK8 */

#define CMXSCR_RS4CS_BRG1  0x00000000   /* SCC4 Rx Clock Source is BRG1 */
#define CMXSCR_RS4CS_BRG2  0x00000008   /* SCC4 Rx Clock Source is BRG2 */
#define CMXSCR_RS4CS_BRG3  0x00000010   /* SCC4 Rx Clock Source is BRG3 */
#define CMXSCR_RS4CS_BRG4  0x00000018   /* SCC4 Rx Clock Source is BRG4 */
#define CMXSCR_RS4CS_CLK5  0x00000020   /* SCC4 Rx Clock Source is CLK5 */
#define CMXSCR_RS4CS_CLK6  0x00000028   /* SCC4 Rx Clock Source is CLK6 */
#define CMXSCR_RS4CS_CLK7  0x00000030   /* SCC4 Rx Clock Source is CLK7 */
#define CMXSCR_RS4CS_CLK8  0x00000038   /* SCC4 Rx Clock Source is CLK8 */

#define CMXSCR_TS4CS_BRG1  0x00000000   /* SCC4 Tx Clock Source is BRG1 */
#define CMXSCR_TS4CS_BRG2  0x00000001   /* SCC4 Tx Clock Source is BRG2 */
#define CMXSCR_TS4CS_BRG3  0x00000002   /* SCC4 Tx Clock Source is BRG3 */
#define CMXSCR_TS4CS_BRG4  0x00000003   /* SCC4 Tx Clock Source is BRG4 */
#define CMXSCR_TS4CS_CLK5  0x00000004   /* SCC4 Tx Clock Source is CLK5 */
#define CMXSCR_TS4CS_CLK6  0x00000005   /* SCC4 Tx Clock Source is CLK6 */
#define CMXSCR_TS4CS_CLK7  0x00000006   /* SCC4 Tx Clock Source is CLK7 */
#define CMXSCR_TS4CS_CLK8  0x00000007   /* SCC4 Tx Clock Source is CLK8 */

/*-----------------------------------------------------------------------
 * SIUMCR - SIU Module Configuration Register				 4-31
 */
#define SIUMCR_BBD	0x80000000	/* Bus Busy Disable		*/
#define SIUMCR_ESE	0x40000000	/* External Snoop Enable	*/
#define SIUMCR_PBSE	0x20000000	/* Parity Byte Select Enable	*/
#define SIUMCR_CDIS	0x10000000	/* Core Disable			*/
#define SIUMCR_DPPC00	0x00000000	/* Data Parity Pins Configuration*/
#define SIUMCR_DPPC01	0x04000000	/* - " -			*/
#define SIUMCR_DPPC10	0x08000000	/* - " -			*/
#define SIUMCR_DPPC11	0x0c000000	/* - " -			*/
#define SIUMCR_L2CPC00	0x00000000	/* L2 Cache Pins Configuration	*/
#define SIUMCR_L2CPC01	0x01000000	/* - " -			*/
#define SIUMCR_L2CPC10	0x02000000	/* - " -			*/
#define SIUMCR_L2CPC11	0x03000000	/* - " -			*/
#define SIUMCR_LBPC00	0x00000000	/* Local Bus Pins Configuration	*/
#define SIUMCR_LBPC01	0x00400000	/* - " -			*/
#define SIUMCR_LBPC10	0x00800000	/* - " -			*/
#define SIUMCR_LBPC11	0x00c00000	/* - " -			*/
#define SIUMCR_APPC00	0x00000000	/* Address Parity Pins Configuration*/
#define SIUMCR_APPC01	0x00100000	/* - " -			*/
#define SIUMCR_APPC10	0x00200000	/* - " -			*/
#define SIUMCR_APPC11	0x00300000	/* - " -			*/
#define SIUMCR_CS10PC00	0x00000000	/* CS10 Pin Configuration	*/
#define SIUMCR_CS10PC01	0x00040000	/* - " -			*/
#define SIUMCR_CS10PC10	0x00080000	/* - " -			*/
#define SIUMCR_CS10PC11	0x000c0000	/* - " -			*/
#define SIUMCR_BCTLC00	0x00000000	/* Buffer Control Configuration	*/
#define SIUMCR_BCTLC01	0x00010000	/* - " -			*/
#define SIUMCR_BCTLC10	0x00020000	/* - " -			*/
#define SIUMCR_BCTLC11	0x00030000	/* - " -			*/
#define SIUMCR_MMR00	0x00000000	/* Mask Masters Requests	*/
#define SIUMCR_MMR01	0x00004000	/* - " -			*/
#define SIUMCR_MMR10	0x00008000	/* - " -			*/
#define SIUMCR_MMR11	0x0000c000	/* - " -			*/
#define SIUMCR_LPBSE	0x00002000	/* LocalBus Parity Byte Select Enable*/

/*-----------------------------------------------------------------------
 * SCCR - System Clock Control Register					 9-8
*/
#define SCCR_PCI_MODE	0x00000100	/* PCI Mode	*/
#define SCCR_PCI_MODCK	0x00000080	/* Value of PCI_MODCK pin	*/
#define SCCR_PCIDF_MSK	0x00000078	/* PCI division factor	*/
#define SCCR_PCIDF_SHIFT 3

#ifndef CPM_IMMR_OFFSET
#define CPM_IMMR_OFFSET	0x101a8
#endif

#define FCC_PSMR_RMII	((uint)0x00020000)	/* Use RMII interface */

/* FCC iop & clock configuration. BSP code is responsible to define Fx_RXCLK & Fx_TXCLK
 * in order to use clock-computing stuff below for the FCC x
 */

/* Automatically generates register configurations */
#define PC_CLK(x)	((uint)(1<<(x-1)))	/* FCC CLK I/O ports */

#define CMXFCR_RF1CS(x)	((uint)((x-5)<<27))	/* FCC1 Receive Clock Source */
#define CMXFCR_TF1CS(x)	((uint)((x-5)<<24))	/* FCC1 Transmit Clock Source */
#define CMXFCR_RF2CS(x)	((uint)((x-9)<<19))	/* FCC2 Receive Clock Source */
#define CMXFCR_TF2CS(x) ((uint)((x-9)<<16))	/* FCC2 Transmit Clock Source */
#define CMXFCR_RF3CS(x)	((uint)((x-9)<<11))	/* FCC3 Receive Clock Source */
#define CMXFCR_TF3CS(x) ((uint)((x-9)<<8))	/* FCC3 Transmit Clock Source */

#define PC_F1RXCLK	PC_CLK(F1_RXCLK)
#define PC_F1TXCLK	PC_CLK(F1_TXCLK)
#define CMX1_CLK_ROUTE	(CMXFCR_RF1CS(F1_RXCLK) | CMXFCR_TF1CS(F1_TXCLK))
#define CMX1_CLK_MASK	((uint)0xff000000)

#define PC_F2RXCLK	PC_CLK(F2_RXCLK)
#define PC_F2TXCLK	PC_CLK(F2_TXCLK)
#define CMX2_CLK_ROUTE	(CMXFCR_RF2CS(F2_RXCLK) | CMXFCR_TF2CS(F2_TXCLK))
#define CMX2_CLK_MASK	((uint)0x00ff0000)

#define PC_F3RXCLK	PC_CLK(F3_RXCLK)
#define PC_F3TXCLK	PC_CLK(F3_TXCLK)
#define CMX3_CLK_ROUTE	(CMXFCR_RF3CS(F3_RXCLK) | CMXFCR_TF3CS(F3_TXCLK))
#define CMX3_CLK_MASK	((uint)0x0000ff00)

#define CPMUX_CLK_MASK (CMX3_CLK_MASK | CMX2_CLK_MASK)
#define CPMUX_CLK_ROUTE (CMX3_CLK_ROUTE | CMX2_CLK_ROUTE)

#define CLK_TRX (PC_F3TXCLK | PC_F3RXCLK | PC_F2TXCLK | PC_F2RXCLK)

/* I/O Pin assignment for FCC1.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PA1_COL		0x00000001U
#define PA1_CRS		0x00000002U
#define PA1_TXER	0x00000004U
#define PA1_TXEN	0x00000008U
#define PA1_RXDV	0x00000010U
#define PA1_RXER	0x00000020U
#define PA1_TXDAT	0x00003c00U
#define PA1_RXDAT	0x0003c000U
#define PA1_PSORA0	(PA1_RXDAT | PA1_TXDAT)
#define PA1_PSORA1	(PA1_COL | PA1_CRS | PA1_TXER | PA1_TXEN | \
		PA1_RXDV | PA1_RXER)
#define PA1_DIRA0	(PA1_RXDAT | PA1_CRS | PA1_COL | PA1_RXER | PA1_RXDV)
#define PA1_DIRA1	(PA1_TXDAT | PA1_TXEN | PA1_TXER)


/* I/O Pin assignment for FCC2.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PB2_TXER	0x00000001U
#define PB2_RXDV	0x00000002U
#define PB2_TXEN	0x00000004U
#define PB2_RXER	0x00000008U
#define PB2_COL		0x00000010U
#define PB2_CRS		0x00000020U
#define PB2_TXDAT	0x000003c0U
#define PB2_RXDAT	0x00003c00U
#define PB2_PSORB0	(PB2_RXDAT | PB2_TXDAT | PB2_CRS | PB2_COL | \
		PB2_RXER | PB2_RXDV | PB2_TXER)
#define PB2_PSORB1	(PB2_TXEN)
#define PB2_DIRB0	(PB2_RXDAT | PB2_CRS | PB2_COL | PB2_RXER | PB2_RXDV)
#define PB2_DIRB1	(PB2_TXDAT | PB2_TXEN | PB2_TXER)


/* I/O Pin assignment for FCC3.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PB3_RXDV	0x00004000U
#define PB3_RXER	0x00008000U
#define PB3_TXER	0x00010000U
#define PB3_TXEN	0x00020000U
#define PB3_COL		0x00040000U
#define PB3_CRS		0x00080000U
#define PB3_TXDAT	0x0f000000U
#define PC3_TXDAT	0x00000010U
#define PB3_RXDAT	0x00f00000U
#define PB3_PSORB0	(PB3_RXDAT | PB3_TXDAT | PB3_CRS | PB3_COL | \
		PB3_RXER | PB3_RXDV | PB3_TXER | PB3_TXEN)
#define PB3_PSORB1	0
#define PB3_DIRB0	(PB3_RXDAT | PB3_CRS | PB3_COL | PB3_RXER | PB3_RXDV)
#define PB3_DIRB1	(PB3_TXDAT | PB3_TXEN | PB3_TXER)
#define PC3_DIRC1	(PC3_TXDAT)

/* Handy macro to specify mem for FCCs*/
#define FCC_MEM_OFFSET(x) (CPM_FCC_SPECIAL_BASE + (x*128))
#define FCC1_MEM_OFFSET FCC_MEM_OFFSET(0)
#define FCC2_MEM_OFFSET FCC_MEM_OFFSET(1)
#define FCC3_MEM_OFFSET FCC_MEM_OFFSET(2)

/* Pipeline Maximum Depth */
#define MPC82XX_BCR_PLDP 0x00800000

/* Clocks and GRG's */

enum cpm_clk_dir {
	CPM_CLK_RX,
	CPM_CLK_TX,
	CPM_CLK_RTX
};

enum cpm_clk_target {
	CPM_CLK_SCC1,
	CPM_CLK_SCC2,
	CPM_CLK_SCC3,
	CPM_CLK_SCC4,
	CPM_CLK_FCC1,
	CPM_CLK_FCC2,
	CPM_CLK_FCC3,
	CPM_CLK_SMC1,
	CPM_CLK_SMC2,
};

enum cpm_clk {
	CPM_CLK_NONE = 0,
	CPM_BRG1,	/* Baud Rate Generator  1 */
	CPM_BRG2,	/* Baud Rate Generator  2 */
	CPM_BRG3,	/* Baud Rate Generator  3 */
	CPM_BRG4,	/* Baud Rate Generator  4 */
	CPM_BRG5,	/* Baud Rate Generator  5 */
	CPM_BRG6,	/* Baud Rate Generator  6 */
	CPM_BRG7,	/* Baud Rate Generator  7 */
	CPM_BRG8,	/* Baud Rate Generator  8 */
	CPM_CLK1,	/* Clock  1 */
	CPM_CLK2,	/* Clock  2 */
	CPM_CLK3,	/* Clock  3 */
	CPM_CLK4,	/* Clock  4 */
	CPM_CLK5,	/* Clock  5 */
	CPM_CLK6,	/* Clock  6 */
	CPM_CLK7,	/* Clock  7 */
	CPM_CLK8,	/* Clock  8 */
	CPM_CLK9,	/* Clock  9 */
	CPM_CLK10,	/* Clock 10 */
	CPM_CLK11,	/* Clock 11 */
	CPM_CLK12,	/* Clock 12 */
	CPM_CLK13,	/* Clock 13 */
	CPM_CLK14,	/* Clock 14 */
	CPM_CLK15,	/* Clock 15 */
	CPM_CLK16,	/* Clock 16 */
	CPM_CLK17,	/* Clock 17 */
	CPM_CLK18,	/* Clock 18 */
	CPM_CLK19,	/* Clock 19 */
	CPM_CLK20,	/* Clock 20 */
	CPM_CLK_DUMMY
};

int __init cpm2_clk_setup(enum cpm_clk_target target, int clock, int mode);
int __init cpm2_smc_clk_setup(enum cpm_clk_target target, int clock);

#define CPM_PIN_INPUT     0
#define CPM_PIN_OUTPUT    1
#define CPM_PIN_PRIMARY   0
#define CPM_PIN_SECONDARY 2
#define CPM_PIN_GPIO      4
#define CPM_PIN_OPENDRAIN 8

void __init cpm2_set_pin(int port, int pin, int flags);

#endif /* __CPM2__ */
#endif /* __KERNEL__ */
