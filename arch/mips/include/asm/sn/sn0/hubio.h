/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Derived from IRIX <sys/SN/SN0/hubio.h>, Revision 1.80.
 *
 * Copyright (C) 1992 - 1997, 1999 Silicon Graphics, Inc.
 * Copyright (C) 1999 by Ralf Baechle
 */
#ifndef _ASM_SGI_SN_SN0_HUBIO_H
#define _ASM_SGI_SN_SN0_HUBIO_H

/*
 * Hub I/O interface registers
 *
 * All registers in this file are subject to change until Hub chip tapeout.
 * In general, the longer software name should be used when available.
 */

/*
 * Slightly friendlier names for some common registers.
 * The hardware definitions follow.
 */
#define IIO_WIDGET		IIO_WID	     /* Widget identification */
#define IIO_WIDGET_STAT		IIO_WSTAT    /* Widget status register */
#define IIO_WIDGET_CTRL		IIO_WCR	     /* Widget control register */
#define IIO_WIDGET_TOUT		IIO_WRTO     /* Widget request timeout */
#define IIO_WIDGET_FLUSH	IIO_WTFR     /* Widget target flush */
#define IIO_PROTECT		IIO_ILAPR    /* IO interface protection */
#define IIO_PROTECT_OVRRD	IIO_ILAPO    /* IO protect override */
#define IIO_OUTWIDGET_ACCESS	IIO_IOWA     /* Outbound widget access */
#define IIO_INWIDGET_ACCESS	IIO_IIWA     /* Inbound widget access */
#define IIO_INDEV_ERR_MASK	IIO_IIDEM    /* Inbound device error mask */
#define IIO_LLP_CSR		IIO_ILCSR    /* LLP control and status */
#define IIO_LLP_LOG		IIO_ILLR     /* LLP log */
#define IIO_XTALKCC_TOUT	IIO_IXCC     /* Xtalk credit count timeout*/
#define IIO_XTALKTT_TOUT	IIO_IXTT     /* Xtalk tail timeout */
#define IIO_IO_ERR_CLR		IIO_IECLR    /* IO error clear */
#define IIO_BTE_CRB_CNT		IIO_IBCN     /* IO BTE CRB count */

#define IIO_LLP_CSR_IS_UP		0x00002000
#define IIO_LLP_CSR_LLP_STAT_MASK	0x00003000
#define IIO_LLP_CSR_LLP_STAT_SHFT	12

/* key to IIO_PROTECT_OVRRD */
#define IIO_PROTECT_OVRRD_KEY	0x53474972756c6573ull	/* "SGIrules" */

/* BTE register names */
#define IIO_BTE_STAT_0		IIO_IBLS_0   /* Also BTE length/status 0 */
#define IIO_BTE_SRC_0		IIO_IBSA_0   /* Also BTE source address	 0 */
#define IIO_BTE_DEST_0		IIO_IBDA_0   /* Also BTE dest. address 0 */
#define IIO_BTE_CTRL_0		IIO_IBCT_0   /* Also BTE control/terminate 0 */
#define IIO_BTE_NOTIFY_0	IIO_IBNA_0   /* Also BTE notification 0 */
#define IIO_BTE_INT_0		IIO_IBIA_0   /* Also BTE interrupt 0 */
#define IIO_BTE_OFF_0		0	     /* Base offset from BTE 0 regs. */
#define IIO_BTE_OFF_1	IIO_IBLS_1 - IIO_IBLS_0 /* Offset from base to BTE 1 */

/* BTE register offsets from base */
#define BTEOFF_STAT		0
#define BTEOFF_SRC		(IIO_BTE_SRC_0 - IIO_BTE_STAT_0)
#define BTEOFF_DEST		(IIO_BTE_DEST_0 - IIO_BTE_STAT_0)
#define BTEOFF_CTRL		(IIO_BTE_CTRL_0 - IIO_BTE_STAT_0)
#define BTEOFF_NOTIFY		(IIO_BTE_NOTIFY_0 - IIO_BTE_STAT_0)
#define BTEOFF_INT		(IIO_BTE_INT_0 - IIO_BTE_STAT_0)


/*
 * The following definitions use the names defined in the IO interface
 * document for ease of reference.  When possible, software should
 * generally use the longer but clearer names defined above.
 */

#define IIO_BASE	0x400000
#define IIO_BASE_BTE0	0x410000
#define IIO_BASE_BTE1	0x420000
#define IIO_BASE_PERF	0x430000
#define IIO_PERF_CNT	0x430008

#define IO_PERF_SETS	32

#define IIO_WID		0x400000	/* Widget identification */
#define IIO_WSTAT	0x400008	/* Widget status */
#define IIO_WCR		0x400020	/* Widget control */

#define IIO_WSTAT_ECRAZY	(1ULL << 32)	/* Hub gone crazy */
#define IIO_WSTAT_TXRETRY	(1ULL << 9)	/* Hub Tx Retry timeout */
#define IIO_WSTAT_TXRETRY_MASK	(0x7F)
#define IIO_WSTAT_TXRETRY_SHFT	(16)
#define IIO_WSTAT_TXRETRY_CNT(w)	(((w) >> IIO_WSTAT_TXRETRY_SHFT) & \
					  IIO_WSTAT_TXRETRY_MASK)

#define IIO_ILAPR	0x400100	/* Local Access Protection */
#define IIO_ILAPO	0x400108	/* Protection override */
#define IIO_IOWA	0x400110	/* outbound widget access */
#define IIO_IIWA	0x400118	/* inbound widget access */
#define IIO_IIDEM	0x400120	/* Inbound Device Error Mask */
#define IIO_ILCSR	0x400128	/* LLP control and status */
#define IIO_ILLR	0x400130	/* LLP Log */
#define IIO_IIDSR	0x400138	/* Interrupt destination */

#define IIO_IIBUSERR	0x1400208	/* Reads here cause a bus error. */

/* IO Interrupt Destination Register */
#define IIO_IIDSR_SENT_SHIFT	28
#define IIO_IIDSR_SENT_MASK	0x10000000
#define IIO_IIDSR_ENB_SHIFT	24
#define IIO_IIDSR_ENB_MASK	0x01000000
#define IIO_IIDSR_NODE_SHIFT	8
#define IIO_IIDSR_NODE_MASK	0x0000ff00
#define IIO_IIDSR_LVL_SHIFT	0
#define IIO_IIDSR_LVL_MASK	0x0000003f


/* GFX Flow Control Node/Widget Register */
#define IIO_IGFX_0	0x400140	/* gfx node/widget register 0 */
#define IIO_IGFX_1	0x400148	/* gfx node/widget register 1 */
#define IIO_IGFX_W_NUM_BITS	4	/* size of widget num field */
#define IIO_IGFX_W_NUM_MASK	((1<<IIO_IGFX_W_NUM_BITS)-1)
#define IIO_IGFX_W_NUM_SHIFT	0
#define IIO_IGFX_N_NUM_BITS	9	/* size of node num field */
#define IIO_IGFX_N_NUM_MASK	((1<<IIO_IGFX_N_NUM_BITS)-1)
#define IIO_IGFX_N_NUM_SHIFT	4
#define IIO_IGFX_P_NUM_BITS	1	/* size of processor num field */
#define IIO_IGFX_P_NUM_MASK	((1<<IIO_IGFX_P_NUM_BITS)-1)
#define IIO_IGFX_P_NUM_SHIFT	16
#define IIO_IGFX_VLD_BITS	1	/* size of valid field */
#define IIO_IGFX_VLD_MASK	((1<<IIO_IGFX_VLD_BITS)-1)
#define IIO_IGFX_VLD_SHIFT	20
#define IIO_IGFX_INIT(widget, node, cpu, valid)				(\
	(((widget) & IIO_IGFX_W_NUM_MASK) << IIO_IGFX_W_NUM_SHIFT) |	 \
	(((node)   & IIO_IGFX_N_NUM_MASK) << IIO_IGFX_N_NUM_SHIFT) |	 \
	(((cpu)	   & IIO_IGFX_P_NUM_MASK) << IIO_IGFX_P_NUM_SHIFT) |	 \
	(((valid)  & IIO_IGFX_VLD_MASK)	  << IIO_IGFX_VLD_SHIFT)	 )

/* Scratch registers (not all bits available) */
#define IIO_SCRATCH_REG0	0x400150
#define IIO_SCRATCH_REG1	0x400158
#define IIO_SCRATCH_MASK	0x0000000f00f11fff

#define IIO_SCRATCH_BIT0_0	0x0000000800000000
#define IIO_SCRATCH_BIT0_1	0x0000000400000000
#define IIO_SCRATCH_BIT0_2	0x0000000200000000
#define IIO_SCRATCH_BIT0_3	0x0000000100000000
#define IIO_SCRATCH_BIT0_4	0x0000000000800000
#define IIO_SCRATCH_BIT0_5	0x0000000000400000
#define IIO_SCRATCH_BIT0_6	0x0000000000200000
#define IIO_SCRATCH_BIT0_7	0x0000000000100000
#define IIO_SCRATCH_BIT0_8	0x0000000000010000
#define IIO_SCRATCH_BIT0_9	0x0000000000001000
#define IIO_SCRATCH_BIT0_R	0x0000000000000fff

/* IO Translation Table Entries */
#define IIO_NUM_ITTES	7		/* ITTEs numbered 0..6 */
					/* Hw manuals number them 1..7! */

/*
 * As a permanent workaround for a bug in the PI side of the hub, we've
 * redefined big window 7 as small window 0.
 */
#define HUB_NUM_BIG_WINDOW	IIO_NUM_ITTES - 1

/*
 * Use the top big window as a surrogate for the first small window
 */
#define SWIN0_BIGWIN		HUB_NUM_BIG_WINDOW

#define ILCSR_WARM_RESET	0x100
/*
 * The IO LLP control status register and widget control register
 */
#ifndef __ASSEMBLER__

typedef union hubii_wid_u {
	u64	wid_reg_value;
	struct {
		u64	wid_rsvd:	32,	/* unused */
			wid_rev_num:	 4,	/* revision number */
			wid_part_num:	16,	/* the widget type: hub=c101 */
			wid_mfg_num:	11,	/* Manufacturer id (IBM) */
			wid_rsvd1:	 1;	/* Reserved */
	} wid_fields_s;
} hubii_wid_t;


typedef union hubii_wcr_u {
	u64	wcr_reg_value;
	struct {
		u64	wcr_rsvd:	41,	/* unused */
			wcr_e_thresh:	 5,	/* elasticity threshold */
			wcr_dir_con:	 1,	/* widget direct connect */
			wcr_f_bad_pkt:	 1,	/* Force bad llp pkt enable */
			wcr_xbar_crd:	 3,	/* LLP crossbar credit */
			wcr_rsvd1:	 8,	/* Reserved */
			wcr_tag_mode:	 1,	/* Tag mode */
			wcr_widget_id:	 4;	/* LLP crossbar credit */
	} wcr_fields_s;
} hubii_wcr_t;

#define iwcr_dir_con	wcr_fields_s.wcr_dir_con

typedef union hubii_wstat_u {
	u64	 reg_value;
	struct {
		u64	rsvd1:		31,
			crazy:		 1,	/* Crazy bit		*/
			rsvd2:		 8,
			llp_tx_cnt:	 8,	/* LLP Xmit retry counter */
			rsvd3:		 6,
			tx_max_rtry:	 1,	/* LLP Retry Timeout Signal */
			rsvd4:		 2,
			xt_tail_to:	 1,	/* Xtalk Tail Timeout	*/
			xt_crd_to:	 1,	/* Xtalk Credit Timeout */
			pending:	 4;	/* Pending Requests	*/
	} wstat_fields_s;
} hubii_wstat_t;


typedef union hubii_ilcsr_u {
	u64	icsr_reg_value;
	struct {
		u64	icsr_rsvd:	22,	/* unused */
			icsr_max_burst: 10,	/* max burst */
			icsr_rsvd4:	 6,	/* reserved */
			icsr_max_retry: 10,	/* max retry */
			icsr_rsvd3:	 2,	/* reserved */
			icsr_lnk_stat:	 2,	/* link status */
			icsr_bm8:	 1,	/* Bit mode 8 */
			icsr_llp_en:	 1,	/* LLP enable bit */
			icsr_rsvd2:	 1,	/* reserver */
			icsr_wrm_reset:	 1,	/* Warm reset bit */
			icsr_rsvd1:	 2,	/* Data ready offset */
			icsr_null_to:	 6;	/* Null timeout	  */

	} icsr_fields_s;
} hubii_ilcsr_t;


typedef union hubii_iowa_u {
	u64	iowa_reg_value;
	struct {
		u64	iowa_rsvd:	48,	/* unused */
			iowa_wxoac:	 8,	/* xtalk widget access bits */
			iowa_rsvd1:	 7,	/* xtalk widget access bits */
			iowa_w0oac:	 1;	/* xtalk widget access bits */
	} iowa_fields_s;
} hubii_iowa_t;

typedef union hubii_iiwa_u {
	u64	iiwa_reg_value;
	struct {
		u64	iiwa_rsvd:	48,	/* unused */
			iiwa_wxiac:	 8,	/* hub wid access bits */
			iiwa_rsvd1:	 7,	/* reserved */
			iiwa_w0iac:	 1;	/* hub wid0 access */
	} iiwa_fields_s;
} hubii_iiwa_t;

typedef union	hubii_illr_u {
	u64	illr_reg_value;
	struct {
		u64	illr_rsvd:	32,	/* unused */
			illr_cb_cnt:	16,	/* checkbit error count */
			illr_sn_cnt:	16;	/* sequence number count */
	} illr_fields_s;
} hubii_illr_t;

/* The structures below are defined to extract and modify the ii
performance registers */

/* io_perf_sel allows the caller to specify what tests will be
   performed */
typedef union io_perf_sel {
	u64 perf_sel_reg;
	struct {
		u64	perf_rsvd  : 48,
			perf_icct  :  8,
			perf_ippr1 :  4,
			perf_ippr0 :  4;
	} perf_sel_bits;
} io_perf_sel_t;

/* io_perf_cnt is to extract the count from the hub registers. Due to
   hardware problems there is only one counter, not two. */

typedef union io_perf_cnt {
	u64	perf_cnt;
	struct {
		u64	perf_rsvd1 : 32,
			perf_rsvd2 : 12,
			perf_cnt   : 20;
	} perf_cnt_bits;
} io_perf_cnt_t;

#endif /* !__ASSEMBLER__ */


#define LNK_STAT_WORKING	0x2

#define IIO_LLP_CB_MAX	0xffff
#define IIO_LLP_SN_MAX	0xffff

/* IO PRB Entries */
#define IIO_NUM_IPRBS	(9)
#define IIO_IOPRB_0	0x400198	/* PRB entry 0 */
#define IIO_IOPRB_8	0x4001a0	/* PRB entry 8 */
#define IIO_IOPRB_9	0x4001a8	/* PRB entry 9 */
#define IIO_IOPRB_A	0x4001b0	/* PRB entry a */
#define IIO_IOPRB_B	0x4001b8	/* PRB entry b */
#define IIO_IOPRB_C	0x4001c0	/* PRB entry c */
#define IIO_IOPRB_D	0x4001c8	/* PRB entry d */
#define IIO_IOPRB_E	0x4001d0	/* PRB entry e */
#define IIO_IOPRB_F	0x4001d8	/* PRB entry f */


#define IIO_IXCC	0x4001e0	/* Crosstalk credit count timeout */
#define IIO_IXTCC	IIO_IXCC
#define IIO_IMEM	0x4001e8	/* Miscellaneous Enable Mask */
#define IIO_IXTT	0x4001f0	/* Crosstalk tail timeout */
#define IIO_IECLR	0x4001f8	/* IO error clear */
#define IIO_IBCN	0x400200	/* IO BTE CRB count */

/*
 * IIO_IMEM Register fields.
 */
#define IIO_IMEM_W0ESD	0x1		/* Widget 0 shut down due to error */
#define IIO_IMEM_B0ESD	(1 << 4)	/* BTE 0 shut down due to error */
#define IIO_IMEM_B1ESD	(1 << 8)	/* BTE 1 Shut down due to error */

/* PIO Read address Table Entries */
#define IIO_IPCA	0x400300	/* PRB Counter adjust */
#define IIO_NUM_PRTES	8		/* Total number of PRB table entries */
#define IIO_PRTE_0	0x400308	/* PIO Read address table entry 0 */
#define IIO_PRTE(_x)	(IIO_PRTE_0 + (8 * (_x)))
#define IIO_WIDPRTE(x)	IIO_PRTE(((x) - 8)) /* widget ID to its PRTE num */
#define IIO_IPDR	0x400388	/* PIO table entry deallocation */
#define IIO_ICDR	0x400390	/* CRB Entry Deallocation */
#define IIO_IFDR	0x400398	/* IOQ FIFO Depth */
#define IIO_IIAP	0x4003a0	/* IIQ Arbitration Parameters */
#define IIO_IMMR	IIO_IIAP
#define IIO_ICMR	0x4003a8	/* CRB Management Register */
#define IIO_ICCR	0x4003b0	/* CRB Control Register */
#define IIO_ICTO	0x4003b8	/* CRB Time Out Register */
#define IIO_ICTP	0x4003c0	/* CRB Time Out Prescalar */


/*
 * ICMR register fields
 */
#define IIO_ICMR_PC_VLD_SHFT	36
#define IIO_ICMR_PC_VLD_MASK	(0x7fffUL << IIO_ICMR_PC_VLD_SHFT)

#define IIO_ICMR_CRB_VLD_SHFT	20
#define IIO_ICMR_CRB_VLD_MASK	(0x7fffUL << IIO_ICMR_CRB_VLD_SHFT)

#define IIO_ICMR_FC_CNT_SHFT	16
#define IIO_ICMR_FC_CNT_MASK	(0xf << IIO_ICMR_FC_CNT_SHFT)

#define IIO_ICMR_C_CNT_SHFT	4
#define IIO_ICMR_C_CNT_MASK	(0xf << IIO_ICMR_C_CNT_SHFT)

#define IIO_ICMR_P_CNT_SHFT	0
#define IIO_ICMR_P_CNT_MASK	(0xf << IIO_ICMR_P_CNT_SHFT)

#define IIO_ICMR_PRECISE	(1UL << 52)
#define IIO_ICMR_CLR_RPPD	(1UL << 13)
#define IIO_ICMR_CLR_RQPD	(1UL << 12)

/*
 * IIO PIO Deallocation register field masks : (IIO_IPDR)
 */
#define IIO_IPDR_PND	(1 << 4)

/*
 * IIO CRB deallocation register field masks: (IIO_ICDR)
 */
#define IIO_ICDR_PND	(1 << 4)

/*
 * IIO CRB control register Fields: IIO_ICCR
 */
#define IIO_ICCR_PENDING	(0x10000)
#define IIO_ICCR_CMD_MASK	(0xFF)
#define IIO_ICCR_CMD_SHFT	(7)
#define IIO_ICCR_CMD_NOP	(0x0)	/* No Op */
#define IIO_ICCR_CMD_WAKE	(0x100) /* Reactivate CRB entry and process */
#define IIO_ICCR_CMD_TIMEOUT	(0x200) /* Make CRB timeout & mark invalid */
#define IIO_ICCR_CMD_EJECT	(0x400) /* Contents of entry written to memory
					 * via a WB
					 */
#define IIO_ICCR_CMD_FLUSH	(0x800)

/*
 * CRB manipulation macros
 *	The CRB macros are slightly complicated, since there are up to
 *	four registers associated with each CRB entry.
 */
#define IIO_NUM_CRBS		15	/* Number of CRBs */
#define IIO_NUM_NORMAL_CRBS	12	/* Number of regular CRB entries */
#define IIO_NUM_PC_CRBS		4	/* Number of partial cache CRBs */
#define IIO_ICRB_OFFSET		8
#define IIO_ICRB_0		0x400400
/* XXX - This is now tuneable:
	#define IIO_FIRST_PC_ENTRY 12
 */

#define IIO_ICRB_A(_x)	(IIO_ICRB_0 + (4 * IIO_ICRB_OFFSET * (_x)))
#define IIO_ICRB_B(_x)	(IIO_ICRB_A(_x) + 1*IIO_ICRB_OFFSET)
#define IIO_ICRB_C(_x)	(IIO_ICRB_A(_x) + 2*IIO_ICRB_OFFSET)
#define IIO_ICRB_D(_x)	(IIO_ICRB_A(_x) + 3*IIO_ICRB_OFFSET)

/* XXX - IBUE register coming for Hub 2 */

/*
 *
 * CRB Register description.
 *
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 *
 * Many of the fields in CRB are status bits used by hardware
 * for implementation of the protocol. It's very dangerous to
 * mess around with the CRB registers.
 *
 * It's OK to read the CRB registers and try to make sense out of the
 * fields in CRB.
 *
 * Updating CRB requires all activities in Hub IIO to be quiesced.
 * otherwise, a write to CRB could corrupt other CRB entries.
 * CRBs are here only as a back door peek to hub IIO's status.
 * Quiescing implies  no dmas no PIOs
 * either directly from the cpu or from sn0net.
 * this is not something that can be done easily. So, AVOID updating
 * CRBs.
 */

/*
 * Fields in CRB Register A
 */
#ifndef __ASSEMBLER__
typedef union icrba_u {
	u64	reg_value;
	struct {
		u64	resvd:	6,
			stall_bte0: 1,	/* Stall BTE 0 */
			stall_bte1: 1,	/* Stall BTE 1 */
			error:	1,	/* CRB has an error	*/
			ecode:	3,	/* Error Code		*/
			lnetuce: 1,	/* SN0net Uncorrectable error */
			mark:	1,	/* CRB Has been marked	*/
			xerr:	1,	/* Error bit set in xtalk header */
			sidn:	4,	/* SIDN field from xtalk	*/
			tnum:	5,	/* TNUM field in xtalk		*/
			addr:	38,	/* Address of request	*/
			valid:	1,	/* Valid status		*/
			iow:	1;	/* IO Write operation	*/
	} icrba_fields_s;
} icrba_t;

/* This is an alternate typedef for the HUB1 CRB A in order to allow
   runtime selection of the format based on the REV_ID field of the
   NI_STATUS_REV_ID register. */
typedef union h1_icrba_u {
	u64	reg_value;

	struct {
		u64	resvd:	6,
			unused: 1,	/* Unused but RW!!	*/
			error:	1,	/* CRB has an error	*/
			ecode:	4,	/* Error Code		*/
			lnetuce: 1,	/* SN0net Uncorrectable error */
			mark:	1,	/* CRB Has been marked	*/
			xerr:	1,	/* Error bit set in xtalk header */
			sidn:	4,	/* SIDN field from xtalk	*/
			tnum:	5,	/* TNUM field in xtalk		*/
			addr:	38,	/* Address of request	*/
			valid:	1,	/* Valid status		*/
			iow:	1;	/* IO Write operation	*/
	} h1_icrba_fields_s;
} h1_icrba_t;

/* XXX - Is this still right?  Check the spec. */
#define ICRBN_A_CERR_SHFT	54
#define ICRBN_A_ERR_MASK	0x3ff

#endif /* !__ASSEMBLER__ */

#define IIO_ICRB_ADDR_SHFT	2	/* Shift to get proper address */

/*
 * values for "ecode" field
 */
#define IIO_ICRB_ECODE_DERR	0	/* Directory error due to IIO access */
#define IIO_ICRB_ECODE_PERR	1	/* Poison error on IO access */
#define IIO_ICRB_ECODE_WERR	2	/* Write error by IIO access
					 * e.g. WINV to a Read only line.
					 */
#define IIO_ICRB_ECODE_AERR	3	/* Access error caused by IIO access */
#define IIO_ICRB_ECODE_PWERR	4	/* Error on partial write	*/
#define IIO_ICRB_ECODE_PRERR	5	/* Error on partial read	*/
#define IIO_ICRB_ECODE_TOUT	6	/* CRB timeout before deallocating */
#define IIO_ICRB_ECODE_XTERR	7	/* Incoming xtalk pkt had error bit */



/*
 * Fields in CRB Register B
 */
#ifndef __ASSEMBLER__
typedef union icrbb_u {
	u64	reg_value;
	struct {
	    u64 rsvd1:	5,
		btenum: 1,	/* BTE to which entry belongs to */
		cohtrans: 1,	/* Coherent transaction */
		xtsize: 2,	/* Xtalk operation size
				 * 0: Double Word
				 * 1: 32 Bytes.
				 * 2: 128 Bytes,
				 * 3: Reserved.
				 */
		srcnode: 9,	/* Source Node ID		*/
		srcinit: 2,	/* Source Initiator:
				 * See below for field values.
				 */
		useold: 1,	/* Use OLD command for processing */
		imsgtype: 2,	/* Incoming message type
				 * see below for field values
				 */
		imsg:	8,	/* Incoming message	*/
		initator: 3,	/* Initiator of original request
				 * See below for field values.
				 */
		reqtype: 5,	/* Identifies type of request
				 * See below for field values.
				 */
		rsvd2:	7,
		ackcnt: 11,	/* Invalidate ack count */
		resp:	1,	/* data response  given to processor */
		ack:	1,	/* indicates data ack received	*/
		hold:	1,	/* entry is gathering inval acks */
		wb_pend:1,	/* waiting for writeback to complete */
		intvn:	1,	/* Intervention */
		stall_ib: 1,	/* Stall Ibuf (from crosstalk) */
		stall_intr: 1;	/* Stall internal interrupts */
	} icrbb_field_s;
} icrbb_t;

/* This is an alternate typedef for the HUB1 CRB B in order to allow
   runtime selection of the format based on the REV_ID field of the
   NI_STATUS_REV_ID register. */
typedef union h1_icrbb_u {
	u64	reg_value;
	struct {
		u64	rsvd1:	5,
			btenum: 1,	/* BTE to which entry belongs to */
			cohtrans: 1,	/* Coherent transaction */
			xtsize: 2,	/* Xtalk operation size
					 * 0: Double Word
					 * 1: 32 Bytes.
					 * 2: 128 Bytes,
					 * 3: Reserved.
					 */
			srcnode: 9,	/* Source Node ID		*/
			srcinit: 2,	/* Source Initiator:
					 * See below for field values.
					 */
			useold: 1,	/* Use OLD command for processing */
			imsgtype: 2,	/* Incoming message type
					 * see below for field values
					 */
			imsg:	8,	/* Incoming message	*/
			initator: 3,	/* Initiator of original request
					 * See below for field values.
					 */
			rsvd2:	1,
			pcache: 1,	/* entry belongs to partial cache */
			reqtype: 5,	/* Identifies type of request
					 * See below for field values.
					 */
			stl_ib: 1,	/* stall Ibus coming from xtalk */
			stl_intr: 1,	/* Stall internal interrupts */
			stl_bte0: 1,	/* Stall BTE 0	*/
			stl_bte1: 1,	/* Stall BTE 1	*/
			intrvn: 1,	/* Req was target of intervention */
			ackcnt: 11,	/* Invalidate ack count */
			resp:	1,	/* data response  given to processor */
			ack:	1,	/* indicates data ack received	*/
			hold:	1,	/* entry is gathering inval acks */
			wb_pend:1,	/* waiting for writeback to complete */
			sleep:	1,	/* xtalk req sleeping till IO-sync */
			pnd_reply: 1,	/* replies not issed due to IOQ full */
			pnd_req: 1;	/* reqs not issued due to IOQ full */
	} h1_icrbb_field_s;
} h1_icrbb_t;


#define b_imsgtype	icrbb_field_s.imsgtype
#define b_btenum	icrbb_field_s.btenum
#define b_cohtrans	icrbb_field_s.cohtrans
#define b_xtsize	icrbb_field_s.xtsize
#define b_srcnode	icrbb_field_s.srcnode
#define b_srcinit	icrbb_field_s.srcinit
#define b_imsgtype	icrbb_field_s.imsgtype
#define b_imsg		icrbb_field_s.imsg
#define b_initiator	icrbb_field_s.initiator

#endif /* !__ASSEMBLER__ */

/*
 * values for field xtsize
 */
#define IIO_ICRB_XTSIZE_DW	0	/* Xtalk operation size is 8 bytes  */
#define IIO_ICRB_XTSIZE_32	1	/* Xtalk operation size is 32 bytes */
#define IIO_ICRB_XTSIZE_128	2	/* Xtalk operation size is 128 bytes */

/*
 * values for field srcinit
 */
#define IIO_ICRB_PROC0		0	/* Source of request is Proc 0 */
#define IIO_ICRB_PROC1		1	/* Source of request is Proc 1 */
#define IIO_ICRB_GB_REQ		2	/* Source is Guaranteed BW request */
#define IIO_ICRB_IO_REQ		3	/* Source is Normal IO request	*/

/*
 * Values for field imsgtype
 */
#define IIO_ICRB_IMSGT_XTALK	0	/* Incoming Message from Xtalk */
#define IIO_ICRB_IMSGT_BTE	1	/* Incoming message from BTE	*/
#define IIO_ICRB_IMSGT_SN0NET	2	/* Incoming message from SN0 net */
#define IIO_ICRB_IMSGT_CRB	3	/* Incoming message from CRB ???  */

/*
 * values for field initiator.
 */
#define IIO_ICRB_INIT_XTALK	0	/* Message originated in xtalk	*/
#define IIO_ICRB_INIT_BTE0	0x1	/* Message originated in BTE 0	*/
#define IIO_ICRB_INIT_SN0NET	0x2	/* Message originated in SN0net */
#define IIO_ICRB_INIT_CRB	0x3	/* Message originated in CRB ?	*/
#define IIO_ICRB_INIT_BTE1	0x5	/* MEssage originated in BTE 1	*/

/*
 * Values for field reqtype.
 */
/* XXX - Need to fix this for Hub 2 */
#define IIO_ICRB_REQ_DWRD	0	/* Request type double word	*/
#define IIO_ICRB_REQ_QCLRD	1	/* Request is Qrtr Caceh line Rd */
#define IIO_ICRB_REQ_BLKRD	2	/* Request is block read	*/
#define IIO_ICRB_REQ_RSHU	6	/* Request is BTE block read	*/
#define IIO_ICRB_REQ_REXU	7	/* request is BTE Excl Read	*/
#define IIO_ICRB_REQ_RDEX	8	/* Request is Read Exclusive	*/
#define IIO_ICRB_REQ_WINC	9	/* Request is Write Invalidate	*/
#define IIO_ICRB_REQ_BWINV	10	/* Request is BTE Winv		*/
#define IIO_ICRB_REQ_PIORD	11	/* Request is PIO read		*/
#define IIO_ICRB_REQ_PIOWR	12	/* Request is PIO Write		*/
#define IIO_ICRB_REQ_PRDM	13	/* Request is Fetch&Op		*/
#define IIO_ICRB_REQ_PWRM	14	/* Request is Store &Op		*/
#define IIO_ICRB_REQ_PTPWR	15	/* Request is Peer to peer	*/
#define IIO_ICRB_REQ_WB		16	/* Request is Write back	*/
#define IIO_ICRB_REQ_DEX	17	/* Retained DEX Cache line	*/

/*
 * Fields in CRB Register C
 */

#ifndef __ASSEMBLER__

typedef union icrbc_s {
	u64	reg_value;
	struct {
		u64	rsvd:	6,
			sleep:	1,
			pricnt: 4,	/* Priority count sent with Read req */
			pripsc: 4,	/* Priority Pre scalar	*/
			bteop:	1,	/* BTE Operation	*/
			push_be: 34,	/* Push address Byte enable
					 * Holds push addr, if CRB is for BTE
					 * If CRB belongs to Partial cache,
					 * this contains byte enables bits
					 * ([47:46] = 0)
					 */
			suppl:	11,	/* Supplemental field	*/
			barrop: 1,	/* Barrier Op bit set in xtalk req */
			doresp: 1,	/* Xtalk req needs a response	*/
			gbr:	1;	/* GBR bit set in xtalk packet	*/
	} icrbc_field_s;
} icrbc_t;

#define c_pricnt	icrbc_field_s.pricnt
#define c_pripsc	icrbc_field_s.pripsc
#define c_bteop		icrbc_field_s.bteop
#define c_bteaddr	icrbc_field_s.push_be	/* push_be field has 2 names */
#define c_benable	icrbc_field_s.push_be	/* push_be field has 2 names */
#define c_suppl		icrbc_field_s.suppl
#define c_barrop	icrbc_field_s.barrop
#define c_doresp	icrbc_field_s.doresp
#define c_gbr	icrbc_field_s.gbr
#endif /* !__ASSEMBLER__ */

/*
 * Fields in CRB Register D
 */

#ifndef __ASSEMBLER__
typedef union icrbd_s {
	u64	reg_value;
	struct {
	    u64 rsvd:	38,
		toutvld: 1,	/* Timeout in progress for this CRB */
		ctxtvld: 1,	/* Context field below is valid */
		rsvd2:	1,
		context: 15,	/* Bit vector:
				 * Has a bit set for each CRB entry
				 * which needs to be deallocated
				 * before this CRB entry is processed.
				 * Set only for barrier operations.
				 */
		timeout: 8;	/* Timeout Upper 8 bits */
	} icrbd_field_s;
} icrbd_t;

#define icrbd_toutvld	icrbd_field_s.toutvld
#define icrbd_ctxtvld	icrbd_field_s.ctxtvld
#define icrbd_context	icrbd_field_s.context


typedef union hubii_ifdr_u {
	u64	hi_ifdr_value;
	struct {
		u64	ifdr_rsvd:	49,
			ifdr_maxrp:	 7,
			ifdr_rsvd1:	 1,
			ifdr_maxrq:	 7;
	} hi_ifdr_fields;
} hubii_ifdr_t;

#endif /* !__ASSEMBLER__ */

/*
 * Hardware designed names for the BTE control registers.
 */
#define IIO_IBLS_0	0x410000	/* BTE length/status 0 */
#define IIO_IBSA_0	0x410008	/* BTE source address 0 */
#define IIO_IBDA_0	0x410010	/* BTE destination address 0 */
#define IIO_IBCT_0	0x410018	/* BTE control/terminate 0 */
#define IIO_IBNA_0	0x410020	/* BTE notification address 0 */
#define IIO_IBNR_0	IIO_IBNA_0
#define IIO_IBIA_0	0x410028	/* BTE interrupt address 0 */

#define IIO_IBLS_1	0x420000	/* BTE length/status 1 */
#define IIO_IBSA_1	0x420008	/* BTE source address 1 */
#define IIO_IBDA_1	0x420010	/* BTE destination address 1 */
#define IIO_IBCT_1	0x420018	/* BTE control/terminate 1 */
#define IIO_IBNA_1	0x420020	/* BTE notification address 1 */
#define IIO_IBNR_1	IIO_IBNA_1
#define IIO_IBIA_1	0x420028	/* BTE interrupt address 1 */

/*
 * More miscellaneous registers
 */
#define IIO_IPCR	0x430000	/* Performance Control */
#define IIO_IPPR	0x430008	/* Performance Profiling */

/*
 * IO Error Clear register bit field definitions
 */
#define IECLR_BTE1		(1 << 18)  /* clear bte error 1 ??? */
#define IECLR_BTE0		(1 << 17)  /* clear bte error 0 ??? */
#define IECLR_CRAZY		(1 << 16)  /* clear crazy bit in wstat reg */
#define IECLR_PRB_F		(1 << 15)  /* clear err bit in PRB_F reg */
#define IECLR_PRB_E		(1 << 14)  /* clear err bit in PRB_E reg */
#define IECLR_PRB_D		(1 << 13)  /* clear err bit in PRB_D reg */
#define IECLR_PRB_C		(1 << 12)  /* clear err bit in PRB_C reg */
#define IECLR_PRB_B		(1 << 11)  /* clear err bit in PRB_B reg */
#define IECLR_PRB_A		(1 << 10)  /* clear err bit in PRB_A reg */
#define IECLR_PRB_9		(1 << 9)   /* clear err bit in PRB_9 reg */
#define IECLR_PRB_8		(1 << 8)   /* clear err bit in PRB_8 reg */
#define IECLR_PRB_0		(1 << 0)   /* clear err bit in PRB_0 reg */

/*
 * IO PIO Read Table Entry format
 */

#ifndef __ASSEMBLER__

typedef union iprte_a {
	u64	entry;
	struct {
	    u64 rsvd1	  : 7,	/* Reserved field		*/
		valid	  : 1,	/* Maps to a timeout entry	*/
		rsvd2	  : 1,
		srcnode	  : 9,	/* Node which did this PIO	*/
		initiator : 2,	/* If T5A or T5B or IO		*/
		rsvd3	  : 3,
		addr	  : 38, /* Physical address of PIO	*/
		rsvd4	  : 3;
	} iprte_fields;
} iprte_a_t;

#define iprte_valid	iprte_fields.valid
#define iprte_timeout	iprte_fields.timeout
#define iprte_srcnode	iprte_fields.srcnode
#define iprte_init	iprte_fields.initiator
#define iprte_addr	iprte_fields.addr

#endif /* !__ASSEMBLER__ */

#define IPRTE_ADDRSHFT	3

/*
 * Hub IIO PRB Register format.
 */

#ifndef __ASSEMBLER__
/*
 * Note: Fields bnakctr, anakctr, xtalkctrmode, ovflow fields are
 * "Status" fields, and should only be used in case of clean up after errors.
 */

typedef union iprb_u {
	u64	reg_value;
	struct {
	    u64 rsvd1:	15,
		error:	1,	/* Widget rcvd wr resp pkt w/ error */
		ovflow: 5,	/* Overflow count. perf measurement */
		fire_and_forget: 1, /* Launch Write without response */
		mode:	2,	/* Widget operation Mode	*/
		rsvd2:	2,
		bnakctr: 14,
		rsvd3:	2,
		anakctr: 14,
		xtalkctr: 8;
	} iprb_fields_s;
} iprb_t;

#define iprb_regval	reg_value

#define iprb_error	iprb_fields_s.error
#define iprb_ovflow	iprb_fields_s.ovflow
#define iprb_ff		iprb_fields_s.fire_and_forget
#define iprb_mode	iprb_fields_s.mode
#define iprb_bnakctr	iprb_fields_s.bnakctr
#define iprb_anakctr	iprb_fields_s.anakctr
#define iprb_xtalkctr	iprb_fields_s.xtalkctr

#endif /* !__ASSEMBLER__ */

/*
 * values for mode field in iprb_t.
 * For details of the meanings of NAK and Accept, refer the PIO flow
 * document
 */
#define IPRB_MODE_NORMAL	(0)
#define IPRB_MODE_COLLECT_A	(1)	/* PRB in collect A mode */
#define IPRB_MODE_SERVICE_A	(2)	/* NAK B and Accept A */
#define IPRB_MODE_SERVICE_B	(3)	/* NAK A and Accept B */

/*
 * IO CRB entry C_A to E_A : Partial (cache) CRBS
 */
#ifndef __ASSEMBLER__
typedef union icrbp_a {
	u64   ip_reg;	    /* the entire register value	*/
	struct {
	     u64 error: 1,  /*	  63, error occurred		*/
		ln_uce: 1,  /*	  62: uncorrectable memory	*/
		ln_ae:	1,  /*	  61: protection violation	*/
		ln_werr:1,  /*	  60: write access error	*/
		ln_aerr:1,  /*	  59: sn0net: Address error	*/
		ln_perr:1,  /*	  58: sn0net: poison error	*/
		timeout:1,  /*	  57: CRB timed out		*/
		l_bdpkt:1,  /*	  56: truncated pkt on sn0net	*/
		c_bdpkt:1,  /*	  55: truncated pkt on xtalk	*/
		c_err:	1,  /*	  54: incoming xtalk req, err set*/
		rsvd1: 12,  /* 53-42: reserved			*/
		valid:	1,  /*	  41: Valid status		*/
		sidn:	4,  /* 40-37: SIDN field of xtalk rqst	*/
		tnum:	5,  /* 36-32: TNUM of xtalk request	*/
		bo:	1,  /*	  31: barrier op set in xtalk rqst*/
		resprqd:1,  /*	  30: xtalk rqst requires response*/
		gbr:	1,  /*	  29: gbr bit set in xtalk rqst */
		size:	2,  /* 28-27: size of xtalk request	*/
		excl:	4,  /* 26-23: exclusive bit(s)		*/
		stall:	3,  /* 22-20: stall (xtalk, bte 0/1)	*/
		intvn:	1,  /*	  19: rqst target of intervention*/
		resp:	1,  /*	  18: Data response given to t5 */
		ack:	1,  /*	  17: Data ack received.	*/
		hold:	1,  /*	  16: crb gathering invalidate acks*/
		wb:	1,  /*	  15: writeback pending.	*/
		ack_cnt:11, /* 14-04: counter of invalidate acks*/
		tscaler:4;  /* 03-00: Timeout prescaler		*/
	} ip_fmt;
} icrbp_a_t;

#endif /* !__ASSEMBLER__ */

/*
 * A couple of defines to go with the above structure.
 */
#define ICRBP_A_CERR_SHFT	54
#define ICRBP_A_ERR_MASK	0x3ff

#ifndef __ASSEMBLER__
typedef union hubii_idsr {
	u64 iin_reg;
	struct {
		u64 rsvd1 : 35,
		    isent : 1,
		    rsvd2 : 3,
		    ienable: 1,
		    rsvd  : 7,
		    node  : 9,
		    rsvd4 : 1,
		    level : 7;
	} iin_fmt;
} hubii_idsr_t;
#endif /* !__ASSEMBLER__ */

/*
 * IO BTE Length/Status (IIO_IBLS) register bit field definitions
 */
#define IBLS_BUSY		(0x1 << 20)
#define IBLS_ERROR_SHFT		16
#define IBLS_ERROR		(0x1 << IBLS_ERROR_SHFT)
#define IBLS_LENGTH_MASK	0xffff

/*
 * IO BTE Control/Terminate register (IBCT) register bit field definitions
 */
#define IBCT_POISON		(0x1 << 8)
#define IBCT_NOTIFY		(0x1 << 4)
#define IBCT_ZFIL_MODE		(0x1 << 0)

/*
 * IO BTE Interrupt Address Register (IBIA) register bit field definitions
 */
#define IBIA_LEVEL_SHFT		16
#define IBIA_LEVEL_MASK		(0x7f << IBIA_LEVEL_SHFT)
#define IBIA_NODE_ID_SHFT	0
#define IBIA_NODE_ID_MASK	(0x1ff)

/*
 * Miscellaneous hub constants
 */

/* Number of widgets supported by hub */
#define HUB_NUM_WIDGET		9
#define HUB_WIDGET_ID_MIN	0x8
#define HUB_WIDGET_ID_MAX	0xf

#define HUB_WIDGET_PART_NUM	0xc101
#define MAX_HUBS_PER_XBOW	2

/*
 * Get a hub's widget id from widget control register
 */
#define IIO_WCR_WID_GET(nasid)	(REMOTE_HUB_L(nasid, III_WCR) & 0xf)
#define IIO_WST_ERROR_MASK	(UINT64_CAST 1 << 32) /* Widget status error */

/*
 * Number of credits Hub widget has while sending req/response to
 * xbow.
 * Value of 3 is required by Xbow 1.1
 * We may be able to increase this to 4 with Xbow 1.2.
 */
#define	      HUBII_XBOW_CREDIT	      3
#define	      HUBII_XBOW_REV2_CREDIT  4

#endif /* _ASM_SGI_SN_SN0_HUBIO_H */
