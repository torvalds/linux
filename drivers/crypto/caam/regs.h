/*
 * CAAM hardware register-level view
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 */

#ifndef REGS_H
#define REGS_H

#include <linux/types.h>
#include <linux/io.h>

/*
 * Architecture-specific register access methods
 *
 * CAAM's bus-addressable registers are 64 bits internally.
 * They have been wired to be safely accessible on 32-bit
 * architectures, however. Registers were organized such
 * that (a) they can be contained in 32 bits, (b) if not, then they
 * can be treated as two 32-bit entities, or finally (c) if they
 * must be treated as a single 64-bit value, then this can safely
 * be done with two 32-bit cycles.
 *
 * For 32-bit operations on 64-bit values, CAAM follows the same
 * 64-bit register access conventions as it's predecessors, in that
 * writes are "triggered" by a write to the register at the numerically
 * higher address, thus, a full 64-bit write cycle requires a write
 * to the lower address, followed by a write to the higher address,
 * which will latch/execute the write cycle.
 *
 * For example, let's assume a SW reset of CAAM through the master
 * configuration register.
 * - SWRST is in bit 31 of MCFG.
 * - MCFG begins at base+0x0000.
 * - Bits 63-32 are a 32-bit word at base+0x0000 (numerically-lower)
 * - Bits 31-0 are a 32-bit word at base+0x0004 (numerically-higher)
 *
 * (and on Power, the convention is 0-31, 32-63, I know...)
 *
 * Assuming a 64-bit write to this MCFG to perform a software reset
 * would then require a write of 0 to base+0x0000, followed by a
 * write of 0x80000000 to base+0x0004, which would "execute" the
 * reset.
 *
 * Of course, since MCFG 63-32 is all zero, we could cheat and simply
 * write 0x8000000 to base+0x0004, and the reset would work fine.
 * However, since CAAM does contain some write-and-read-intended
 * 64-bit registers, this code defines 64-bit access methods for
 * the sake of internal consistency and simplicity, and so that a
 * clean transition to 64-bit is possible when it becomes necessary.
 *
 * There are limitations to this that the developer must recognize.
 * 32-bit architectures cannot enforce an atomic-64 operation,
 * Therefore:
 *
 * - On writes, since the HW is assumed to latch the cycle on the
 *   write of the higher-numeric-address word, then ordered
 *   writes work OK.
 *
 * - For reads, where a register contains a relevant value of more
 *   that 32 bits, the hardware employs logic to latch the other
 *   "half" of the data until read, ensuring an accurate value.
 *   This is of particular relevance when dealing with CAAM's
 *   performance counters.
 *
 */

#ifdef __BIG_ENDIAN
#define wr_reg32(reg, data) out_be32(reg, data)
#define rd_reg32(reg) in_be32(reg)
#ifdef CONFIG_64BIT
#define wr_reg64(reg, data) out_be64(reg, data)
#define rd_reg64(reg) in_be64(reg)
#endif
#else
#ifdef __LITTLE_ENDIAN
#define wr_reg32(reg, data) __raw_writel(reg, data)
#define rd_reg32(reg) __raw_readl(reg)
#ifdef CONFIG_64BIT
#define wr_reg64(reg, data) __raw_writeq(reg, data)
#define rd_reg64(reg) __raw_readq(reg)
#endif
#endif
#endif

#ifndef CONFIG_64BIT
static inline void wr_reg64(u64 __iomem *reg, u64 data)
{
	wr_reg32((u32 __iomem *)reg, (data & 0xffffffff00000000ull) >> 32);
	wr_reg32((u32 __iomem *)reg + 1, data & 0x00000000ffffffffull);
}

static inline u64 rd_reg64(u64 __iomem *reg)
{
	return (((u64)rd_reg32((u32 __iomem *)reg)) << 32) |
		((u64)rd_reg32((u32 __iomem *)reg + 1));
}
#endif

/*
 * jr_outentry
 * Represents each entry in a JobR output ring
 */
struct jr_outentry {
	dma_addr_t desc;/* Pointer to completed descriptor */
	u32 jrstatus;	/* Status for completed descriptor */
} __packed;

/*
 * caam_perfmon - Performance Monitor/Secure Memory Status/
 *                CAAM Global Status/Component Version IDs
 *
 * Spans f00-fff wherever instantiated
 */

/* Number of DECOs */
#define CHA_NUM_DECONUM_SHIFT	56
#define CHA_NUM_DECONUM_MASK	(0xfull << CHA_NUM_DECONUM_SHIFT)

struct caam_perfmon {
	/* Performance Monitor Registers			f00-f9f */
	u64 req_dequeued;	/* PC_REQ_DEQ - Dequeued Requests	     */
	u64 ob_enc_req;	/* PC_OB_ENC_REQ - Outbound Encrypt Requests */
	u64 ib_dec_req;	/* PC_IB_DEC_REQ - Inbound Decrypt Requests  */
	u64 ob_enc_bytes;	/* PC_OB_ENCRYPT - Outbound Bytes Encrypted  */
	u64 ob_prot_bytes;	/* PC_OB_PROTECT - Outbound Bytes Protected  */
	u64 ib_dec_bytes;	/* PC_IB_DECRYPT - Inbound Bytes Decrypted   */
	u64 ib_valid_bytes;	/* PC_IB_VALIDATED Inbound Bytes Validated   */
	u64 rsvd[13];

	/* CAAM Hardware Instantiation Parameters		fa0-fbf */
	u64 cha_rev;		/* CRNR - CHA Revision Number		*/
#define CTPR_QI_SHIFT		57
#define CTPR_QI_MASK		(0x1ull << CTPR_QI_SHIFT)
	u64 comp_parms;	/* CTPR - Compile Parameters Register	*/
	u64 rsvd1[2];

	/* CAAM Global Status					fc0-fdf */
	u64 faultaddr;	/* FAR  - Fault Address		*/
	u32 faultliodn;	/* FALR - Fault Address LIODN	*/
	u32 faultdetail;	/* FADR - Fault Addr Detail	*/
	u32 rsvd2;
	u32 status;		/* CSTA - CAAM Status */
	u64 rsvd3;

	/* Component Instantiation Parameters			fe0-fff */
	u32 rtic_id;		/* RVID - RTIC Version ID	*/
	u32 ccb_id;		/* CCBVID - CCB Version ID	*/
	u64 cha_id;		/* CHAVID - CHA Version ID	*/
	u64 cha_num;		/* CHANUM - CHA Number		*/
	u64 caam_id;		/* CAAMVID - CAAM Version ID	*/
};

/* LIODN programming for DMA configuration */
#define MSTRID_LOCK_LIODN	0x80000000
#define MSTRID_LOCK_MAKETRUSTED	0x00010000	/* only for JR masterid */

#define MSTRID_LIODN_MASK	0x0fff
struct masterid {
	u32 liodn_ms;	/* lock and make-trusted control bits */
	u32 liodn_ls;	/* LIODN for non-sequence and seq access */
};

/* Partition ID for DMA configuration */
struct partid {
	u32 rsvd1;
	u32 pidr;	/* partition ID, DECO */
};

/* RNG test mode (replicated twice in some configurations) */
/* Padded out to 0x100 */
struct rngtst {
	u32 mode;		/* RTSTMODEx - Test mode */
	u32 rsvd1[3];
	u32 reset;		/* RTSTRESETx - Test reset control */
	u32 rsvd2[3];
	u32 status;		/* RTSTSSTATUSx - Test status */
	u32 rsvd3;
	u32 errstat;		/* RTSTERRSTATx - Test error status */
	u32 rsvd4;
	u32 errctl;		/* RTSTERRCTLx - Test error control */
	u32 rsvd5;
	u32 entropy;		/* RTSTENTROPYx - Test entropy */
	u32 rsvd6[15];
	u32 verifctl;	/* RTSTVERIFCTLx - Test verification control */
	u32 rsvd7;
	u32 verifstat;	/* RTSTVERIFSTATx - Test verification status */
	u32 rsvd8;
	u32 verifdata;	/* RTSTVERIFDx - Test verification data */
	u32 rsvd9;
	u32 xkey;		/* RTSTXKEYx - Test XKEY */
	u32 rsvd10;
	u32 oscctctl;	/* RTSTOSCCTCTLx - Test osc. counter control */
	u32 rsvd11;
	u32 oscct;		/* RTSTOSCCTx - Test oscillator counter */
	u32 rsvd12;
	u32 oscctstat;	/* RTSTODCCTSTATx - Test osc counter status */
	u32 rsvd13[2];
	u32 ofifo[4];	/* RTSTOFIFOx - Test output FIFO */
	u32 rsvd14[15];
};

/*
 * caam_ctrl - basic core configuration
 * starts base + 0x0000 padded out to 0x1000
 */

#define KEK_KEY_SIZE		8
#define TKEK_KEY_SIZE		8
#define TDSK_KEY_SIZE		8

#define DECO_RESET	1	/* Use with DECO reset/availability regs */
#define DECO_RESET_0	(DECO_RESET << 0)
#define DECO_RESET_1	(DECO_RESET << 1)
#define DECO_RESET_2	(DECO_RESET << 2)
#define DECO_RESET_3	(DECO_RESET << 3)
#define DECO_RESET_4	(DECO_RESET << 4)

struct caam_ctrl {
	/* Basic Configuration Section				000-01f */
	/* Read/Writable					        */
	u32 rsvd1;
	u32 mcr;		/* MCFG      Master Config Register  */
	u32 rsvd2[2];

	/* Bus Access Configuration Section			010-11f */
	/* Read/Writable                                                */
	struct masterid jr_mid[4];	/* JRxLIODNR - JobR LIODN setup */
	u32 rsvd3[12];
	struct masterid rtic_mid[4];	/* RTICxLIODNR - RTIC LIODN setup */
	u32 rsvd4[7];
	u32 deco_rq;			/* DECORR - DECO Request */
	struct partid deco_mid[5];	/* DECOxLIODNR - 1 per DECO */
	u32 rsvd5[22];

	/* DECO Availability/Reset Section			120-3ff */
	u32 deco_avail;		/* DAR - DECO availability */
	u32 deco_reset;		/* DRR - DECO reset */
	u32 rsvd6[182];

	/* Key Encryption/Decryption Configuration              400-5ff */
	/* Read/Writable only while in Non-secure mode                  */
	u32 kek[KEK_KEY_SIZE];	/* JDKEKR - Key Encryption Key */
	u32 tkek[TKEK_KEY_SIZE];	/* TDKEKR - Trusted Desc KEK */
	u32 tdsk[TDSK_KEY_SIZE];	/* TDSKR - Trusted Desc Signing Key */
	u32 rsvd7[32];
	u64 sknonce;			/* SKNR - Secure Key Nonce */
	u32 rsvd8[70];

	/* RNG Test/Verification/Debug Access                   600-7ff */
	/* (Useful in Test/Debug modes only...)                         */
	struct rngtst rtst[2];

	u32 rsvd9[448];

	/* Performance Monitor                                  f00-fff */
	struct caam_perfmon perfmon;
};

/*
 * Controller master config register defs
 */
#define MCFGR_SWRESET		0x80000000 /* software reset */
#define MCFGR_WDENABLE		0x40000000 /* DECO watchdog enable */
#define MCFGR_WDFAIL		0x20000000 /* DECO watchdog force-fail */
#define MCFGR_DMA_RESET		0x10000000
#define MCFGR_LONG_PTR		0x00010000 /* Use >32-bit desc addressing */

/* AXI read cache control */
#define MCFGR_ARCACHE_SHIFT	12
#define MCFGR_ARCACHE_MASK	(0xf << MCFGR_ARCACHE_SHIFT)

/* AXI write cache control */
#define MCFGR_AWCACHE_SHIFT	8
#define MCFGR_AWCACHE_MASK	(0xf << MCFGR_AWCACHE_SHIFT)

/* AXI pipeline depth */
#define MCFGR_AXIPIPE_SHIFT	4
#define MCFGR_AXIPIPE_MASK	(0xf << MCFGR_AXIPIPE_SHIFT)

#define MCFGR_AXIPRI		0x00000008 /* Assert AXI priority sideband */
#define MCFGR_BURST_64		0x00000001 /* Max burst size */

/*
 * caam_job_ring - direct job ring setup
 * 1-4 possible per instantiation, base + 1000/2000/3000/4000
 * Padded out to 0x1000
 */
struct caam_job_ring {
	/* Input ring */
	u64 inpring_base;	/* IRBAx -  Input desc ring baseaddr */
	u32 rsvd1;
	u32 inpring_size;	/* IRSx - Input ring size */
	u32 rsvd2;
	u32 inpring_avail;	/* IRSAx - Input ring room remaining */
	u32 rsvd3;
	u32 inpring_jobadd;	/* IRJAx - Input ring jobs added */

	/* Output Ring */
	u64 outring_base;	/* ORBAx - Output status ring base addr */
	u32 rsvd4;
	u32 outring_size;	/* ORSx - Output ring size */
	u32 rsvd5;
	u32 outring_rmvd;	/* ORJRx - Output ring jobs removed */
	u32 rsvd6;
	u32 outring_used;	/* ORSFx - Output ring slots full */

	/* Status/Configuration */
	u32 rsvd7;
	u32 jroutstatus;	/* JRSTAx - JobR output status */
	u32 rsvd8;
	u32 jrintstatus;	/* JRINTx - JobR interrupt status */
	u32 rconfig_hi;	/* JRxCFG - Ring configuration */
	u32 rconfig_lo;

	/* Indices. CAAM maintains as "heads" of each queue */
	u32 rsvd9;
	u32 inp_rdidx;	/* IRRIx - Input ring read index */
	u32 rsvd10;
	u32 out_wtidx;	/* ORWIx - Output ring write index */

	/* Command/control */
	u32 rsvd11;
	u32 jrcommand;	/* JRCRx - JobR command */

	u32 rsvd12[932];

	/* Performance Monitor                                  f00-fff */
	struct caam_perfmon perfmon;
};

#define JR_RINGSIZE_MASK	0x03ff
/*
 * jrstatus - Job Ring Output Status
 * All values in lo word
 * Also note, same values written out as status through QI
 * in the command/status field of a frame descriptor
 */
#define JRSTA_SSRC_SHIFT            28
#define JRSTA_SSRC_MASK             0xf0000000

#define JRSTA_SSRC_NONE             0x00000000
#define JRSTA_SSRC_CCB_ERROR        0x20000000
#define JRSTA_SSRC_JUMP_HALT_USER   0x30000000
#define JRSTA_SSRC_DECO             0x40000000
#define JRSTA_SSRC_JRERROR          0x60000000
#define JRSTA_SSRC_JUMP_HALT_CC     0x70000000

#define JRSTA_DECOERR_JUMP          0x08000000
#define JRSTA_DECOERR_INDEX_SHIFT   8
#define JRSTA_DECOERR_INDEX_MASK    0xff00
#define JRSTA_DECOERR_ERROR_MASK    0x00ff

#define JRSTA_DECOERR_NONE          0x00
#define JRSTA_DECOERR_LINKLEN       0x01
#define JRSTA_DECOERR_LINKPTR       0x02
#define JRSTA_DECOERR_JRCTRL        0x03
#define JRSTA_DECOERR_DESCCMD       0x04
#define JRSTA_DECOERR_ORDER         0x05
#define JRSTA_DECOERR_KEYCMD        0x06
#define JRSTA_DECOERR_LOADCMD       0x07
#define JRSTA_DECOERR_STORECMD      0x08
#define JRSTA_DECOERR_OPCMD         0x09
#define JRSTA_DECOERR_FIFOLDCMD     0x0a
#define JRSTA_DECOERR_FIFOSTCMD     0x0b
#define JRSTA_DECOERR_MOVECMD       0x0c
#define JRSTA_DECOERR_JUMPCMD       0x0d
#define JRSTA_DECOERR_MATHCMD       0x0e
#define JRSTA_DECOERR_SHASHCMD      0x0f
#define JRSTA_DECOERR_SEQCMD        0x10
#define JRSTA_DECOERR_DECOINTERNAL  0x11
#define JRSTA_DECOERR_SHDESCHDR     0x12
#define JRSTA_DECOERR_HDRLEN        0x13
#define JRSTA_DECOERR_BURSTER       0x14
#define JRSTA_DECOERR_DESCSIGNATURE 0x15
#define JRSTA_DECOERR_DMA           0x16
#define JRSTA_DECOERR_BURSTFIFO     0x17
#define JRSTA_DECOERR_JRRESET       0x1a
#define JRSTA_DECOERR_JOBFAIL       0x1b
#define JRSTA_DECOERR_DNRERR        0x80
#define JRSTA_DECOERR_UNDEFPCL      0x81
#define JRSTA_DECOERR_PDBERR        0x82
#define JRSTA_DECOERR_ANRPLY_LATE   0x83
#define JRSTA_DECOERR_ANRPLY_REPLAY 0x84
#define JRSTA_DECOERR_SEQOVF        0x85
#define JRSTA_DECOERR_INVSIGN       0x86
#define JRSTA_DECOERR_DSASIGN       0x87

#define JRSTA_CCBERR_JUMP           0x08000000
#define JRSTA_CCBERR_INDEX_MASK     0xff00
#define JRSTA_CCBERR_INDEX_SHIFT    8
#define JRSTA_CCBERR_CHAID_MASK     0x00f0
#define JRSTA_CCBERR_CHAID_SHIFT    4
#define JRSTA_CCBERR_ERRID_MASK     0x000f

#define JRSTA_CCBERR_CHAID_AES      (0x01 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_DES      (0x02 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_ARC4     (0x03 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_MD       (0x04 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_RNG      (0x05 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_SNOW     (0x06 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_KASUMI   (0x07 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_PK       (0x08 << JRSTA_CCBERR_CHAID_SHIFT)
#define JRSTA_CCBERR_CHAID_CRC      (0x09 << JRSTA_CCBERR_CHAID_SHIFT)

#define JRSTA_CCBERR_ERRID_NONE     0x00
#define JRSTA_CCBERR_ERRID_MODE     0x01
#define JRSTA_CCBERR_ERRID_DATASIZ  0x02
#define JRSTA_CCBERR_ERRID_KEYSIZ   0x03
#define JRSTA_CCBERR_ERRID_PKAMEMSZ 0x04
#define JRSTA_CCBERR_ERRID_PKBMEMSZ 0x05
#define JRSTA_CCBERR_ERRID_SEQUENCE 0x06
#define JRSTA_CCBERR_ERRID_PKDIVZRO 0x07
#define JRSTA_CCBERR_ERRID_PKMODEVN 0x08
#define JRSTA_CCBERR_ERRID_KEYPARIT 0x09
#define JRSTA_CCBERR_ERRID_ICVCHK   0x0a
#define JRSTA_CCBERR_ERRID_HARDWARE 0x0b
#define JRSTA_CCBERR_ERRID_CCMAAD   0x0c
#define JRSTA_CCBERR_ERRID_INVCHA   0x0f

#define JRINT_ERR_INDEX_MASK        0x3fff0000
#define JRINT_ERR_INDEX_SHIFT       16
#define JRINT_ERR_TYPE_MASK         0xf00
#define JRINT_ERR_TYPE_SHIFT        8
#define JRINT_ERR_HALT_MASK         0xc
#define JRINT_ERR_HALT_SHIFT        2
#define JRINT_ERR_HALT_INPROGRESS   0x4
#define JRINT_ERR_HALT_COMPLETE     0x8
#define JRINT_JR_ERROR              0x02
#define JRINT_JR_INT                0x01

#define JRINT_ERR_TYPE_WRITE        1
#define JRINT_ERR_TYPE_BAD_INPADDR  3
#define JRINT_ERR_TYPE_BAD_OUTADDR  4
#define JRINT_ERR_TYPE_INV_INPWRT   5
#define JRINT_ERR_TYPE_INV_OUTWRT   6
#define JRINT_ERR_TYPE_RESET        7
#define JRINT_ERR_TYPE_REMOVE_OFL   8
#define JRINT_ERR_TYPE_ADD_OFL      9

#define JRCFG_SOE		0x04
#define JRCFG_ICEN		0x02
#define JRCFG_IMSK		0x01
#define JRCFG_ICDCT_SHIFT	8
#define JRCFG_ICTT_SHIFT	16

#define JRCR_RESET                  0x01

/*
 * caam_assurance - Assurance Controller View
 * base + 0x6000 padded out to 0x1000
 */

struct rtic_element {
	u64 address;
	u32 rsvd;
	u32 length;
};

struct rtic_block {
	struct rtic_element element[2];
};

struct rtic_memhash {
	u32 memhash_be[32];
	u32 memhash_le[32];
};

struct caam_assurance {
    /* Status/Command/Watchdog */
	u32 rsvd1;
	u32 status;		/* RSTA - Status */
	u32 rsvd2;
	u32 cmd;		/* RCMD - Command */
	u32 rsvd3;
	u32 ctrl;		/* RCTL - Control */
	u32 rsvd4;
	u32 throttle;	/* RTHR - Throttle */
	u32 rsvd5[2];
	u64 watchdog;	/* RWDOG - Watchdog Timer */
	u32 rsvd6;
	u32 rend;		/* REND - Endian corrections */
	u32 rsvd7[50];

	/* Block access/configuration @ 100/110/120/130 */
	struct rtic_block memblk[4];	/* Memory Blocks A-D */
	u32 rsvd8[32];

	/* Block hashes @ 200/300/400/500 */
	struct rtic_memhash hash[4];	/* Block hash values A-D */
	u32 rsvd_3[640];
};

/*
 * caam_queue_if - QI configuration and control
 * starts base + 0x7000, padded out to 0x1000 long
 */

struct caam_queue_if {
	u32 qi_control_hi;	/* QICTL  - QI Control */
	u32 qi_control_lo;
	u32 rsvd1;
	u32 qi_status;	/* QISTA  - QI Status */
	u32 qi_deq_cfg_hi;	/* QIDQC  - QI Dequeue Configuration */
	u32 qi_deq_cfg_lo;
	u32 qi_enq_cfg_hi;	/* QISEQC - QI Enqueue Command     */
	u32 qi_enq_cfg_lo;
	u32 rsvd2[1016];
};

/* QI control bits - low word */
#define QICTL_DQEN      0x01              /* Enable frame pop          */
#define QICTL_STOP      0x02              /* Stop dequeue/enqueue      */
#define QICTL_SOE       0x04              /* Stop on error             */

/* QI control bits - high word */
#define QICTL_MBSI	0x01
#define QICTL_MHWSI	0x02
#define QICTL_MWSI	0x04
#define QICTL_MDWSI	0x08
#define QICTL_CBSI	0x10		/* CtrlDataByteSwapInput     */
#define QICTL_CHWSI	0x20		/* CtrlDataHalfSwapInput     */
#define QICTL_CWSI	0x40		/* CtrlDataWordSwapInput     */
#define QICTL_CDWSI	0x80		/* CtrlDataDWordSwapInput    */
#define QICTL_MBSO	0x0100
#define QICTL_MHWSO	0x0200
#define QICTL_MWSO	0x0400
#define QICTL_MDWSO	0x0800
#define QICTL_CBSO	0x1000		/* CtrlDataByteSwapOutput    */
#define QICTL_CHWSO	0x2000		/* CtrlDataHalfSwapOutput    */
#define QICTL_CWSO	0x4000		/* CtrlDataWordSwapOutput    */
#define QICTL_CDWSO     0x8000		/* CtrlDataDWordSwapOutput   */
#define QICTL_DMBS	0x010000
#define QICTL_EPO	0x020000

/* QI status bits */
#define QISTA_PHRDERR   0x01              /* PreHeader Read Error      */
#define QISTA_CFRDERR   0x02              /* Compound Frame Read Error */
#define QISTA_OFWRERR   0x04              /* Output Frame Read Error   */
#define QISTA_BPDERR    0x08              /* Buffer Pool Depleted      */
#define QISTA_BTSERR    0x10              /* Buffer Undersize          */
#define QISTA_CFWRERR   0x20              /* Compound Frame Write Err  */
#define QISTA_STOPD     0x80000000        /* QI Stopped (see QICTL)    */

/* deco_sg_table - DECO view of scatter/gather table */
struct deco_sg_table {
	u64 addr;		/* Segment Address */
	u32 elen;		/* E, F bits + 30-bit length */
	u32 bpid_offset;	/* Buffer Pool ID + 16-bit length */
};

/*
 * caam_deco - descriptor controller - CHA cluster block
 *
 * Only accessible when direct DECO access is turned on
 * (done in DECORR, via MID programmed in DECOxMID
 *
 * 5 typical, base + 0x8000/9000/a000/b000
 * Padded out to 0x1000 long
 */
struct caam_deco {
	u32 rsvd1;
	u32 cls1_mode;	/* CxC1MR -  Class 1 Mode */
	u32 rsvd2;
	u32 cls1_keysize;	/* CxC1KSR - Class 1 Key Size */
	u32 cls1_datasize_hi;	/* CxC1DSR - Class 1 Data Size */
	u32 cls1_datasize_lo;
	u32 rsvd3;
	u32 cls1_icvsize;	/* CxC1ICVSR - Class 1 ICV size */
	u32 rsvd4[5];
	u32 cha_ctrl;	/* CCTLR - CHA control */
	u32 rsvd5;
	u32 irq_crtl;	/* CxCIRQ - CCB interrupt done/error/clear */
	u32 rsvd6;
	u32 clr_written;	/* CxCWR - Clear-Written */
	u32 ccb_status_hi;	/* CxCSTA - CCB Status/Error */
	u32 ccb_status_lo;
	u32 rsvd7[3];
	u32 aad_size;	/* CxAADSZR - Current AAD Size */
	u32 rsvd8;
	u32 cls1_iv_size;	/* CxC1IVSZR - Current Class 1 IV Size */
	u32 rsvd9[7];
	u32 pkha_a_size;	/* PKASZRx - Size of PKHA A */
	u32 rsvd10;
	u32 pkha_b_size;	/* PKBSZRx - Size of PKHA B */
	u32 rsvd11;
	u32 pkha_n_size;	/* PKNSZRx - Size of PKHA N */
	u32 rsvd12;
	u32 pkha_e_size;	/* PKESZRx - Size of PKHA E */
	u32 rsvd13[24];
	u32 cls1_ctx[16];	/* CxC1CTXR - Class 1 Context @100 */
	u32 rsvd14[48];
	u32 cls1_key[8];	/* CxC1KEYR - Class 1 Key @200 */
	u32 rsvd15[121];
	u32 cls2_mode;	/* CxC2MR - Class 2 Mode */
	u32 rsvd16;
	u32 cls2_keysize;	/* CxX2KSR - Class 2 Key Size */
	u32 cls2_datasize_hi;	/* CxC2DSR - Class 2 Data Size */
	u32 cls2_datasize_lo;
	u32 rsvd17;
	u32 cls2_icvsize;	/* CxC2ICVSZR - Class 2 ICV Size */
	u32 rsvd18[56];
	u32 cls2_ctx[18];	/* CxC2CTXR - Class 2 Context @500 */
	u32 rsvd19[46];
	u32 cls2_key[32];	/* CxC2KEYR - Class2 Key @600 */
	u32 rsvd20[84];
	u32 inp_infofifo_hi;	/* CxIFIFO - Input Info FIFO @7d0 */
	u32 inp_infofifo_lo;
	u32 rsvd21[2];
	u64 inp_datafifo;	/* CxDFIFO - Input Data FIFO */
	u32 rsvd22[2];
	u64 out_datafifo;	/* CxOFIFO - Output Data FIFO */
	u32 rsvd23[2];
	u32 jr_ctl_hi;	/* CxJRR - JobR Control Register      @800 */
	u32 jr_ctl_lo;
	u64 jr_descaddr;	/* CxDADR - JobR Descriptor Address */
	u32 op_status_hi;	/* DxOPSTA - DECO Operation Status */
	u32 op_status_lo;
	u32 rsvd24[2];
	u32 liodn;		/* DxLSR - DECO LIODN Status - non-seq */
	u32 td_liodn;	/* DxLSR - DECO LIODN Status - trustdesc */
	u32 rsvd26[6];
	u64 math[4];		/* DxMTH - Math register */
	u32 rsvd27[8];
	struct deco_sg_table gthr_tbl[4];	/* DxGTR - Gather Tables */
	u32 rsvd28[16];
	struct deco_sg_table sctr_tbl[4];	/* DxSTR - Scatter Tables */
	u32 rsvd29[48];
	u32 descbuf[64];	/* DxDESB - Descriptor buffer */
	u32 rsvd30[320];
};

/*
 * Current top-level view of memory map is:
 *
 * 0x0000 - 0x0fff - CAAM Top-Level Control
 * 0x1000 - 0x1fff - Job Ring 0
 * 0x2000 - 0x2fff - Job Ring 1
 * 0x3000 - 0x3fff - Job Ring 2
 * 0x4000 - 0x4fff - Job Ring 3
 * 0x5000 - 0x5fff - (unused)
 * 0x6000 - 0x6fff - Assurance Controller
 * 0x7000 - 0x7fff - Queue Interface
 * 0x8000 - 0x8fff - DECO-CCB 0
 * 0x9000 - 0x9fff - DECO-CCB 1
 * 0xa000 - 0xafff - DECO-CCB 2
 * 0xb000 - 0xbfff - DECO-CCB 3
 * 0xc000 - 0xcfff - DECO-CCB 4
 *
 * caam_full describes the full register view of CAAM if useful,
 * although many configurations may choose to implement parts of
 * the register map separately, in differing privilege regions
 */
struct caam_full {
	struct caam_ctrl __iomem ctrl;
	struct caam_job_ring jr[4];
	u64 rsvd[512];
	struct caam_assurance assure;
	struct caam_queue_if qi;
	struct caam_deco *deco;
};

#endif /* REGS_H */
