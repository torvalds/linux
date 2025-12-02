/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Derived from IRIX <sys/SN/SN0/hubni.h>, Revision 1.27.
 *
 * Copyright (C) 1992-1997, 1999 Silicon Graphics, Inc.
 * Copyright (C) 1999 by Ralf Baechle
 */
#ifndef _ASM_SGI_SN0_HUBNI_H
#define _ASM_SGI_SN0_HUBNI_H

#ifndef __ASSEMBLER__
#include <linux/types.h>
#endif

/*
 * Hub Network Interface registers
 *
 * All registers in this file are subject to change until Hub chip tapeout.
 */

#define NI_BASE			0x600000
#define NI_BASE_TABLES		0x630000

#define NI_STATUS_REV_ID	0x600000 /* Hub network status, rev, and ID */
#define NI_PORT_RESET		0x600008 /* Reset the network interface	    */
#define NI_PROTECTION		0x600010 /* NI register access permissions  */
#define NI_GLOBAL_PARMS		0x600018 /* LLP parameters		    */
#define NI_SCRATCH_REG0		0x600100 /* Scratch register 0 (64 bits)    */
#define NI_SCRATCH_REG1		0x600108 /* Scratch register 1 (64 bits)    */
#define NI_DIAG_PARMS		0x600110 /* Parameters for diags	    */

#define NI_VECTOR_PARMS		0x600200 /* Vector PIO routing parameters   */
#define NI_VECTOR		0x600208 /* Vector PIO route		    */
#define NI_VECTOR_DATA		0x600210 /* Vector PIO data		    */
#define NI_VECTOR_STATUS	0x600300 /* Vector PIO return status	    */
#define NI_RETURN_VECTOR	0x600308 /* Vector PIO return vector	    */
#define NI_VECTOR_READ_DATA	0x600310 /* Vector PIO read data	    */
#define NI_VECTOR_CLEAR		0x600380 /* Vector PIO read & clear status  */

#define NI_IO_PROTECT		0x600400 /* PIO protection bits		    */
#define NI_IO_PROT_OVRRD	0x600408 /* PIO protection bit override	    */

#define NI_AGE_CPU0_MEMORY	0x600500 /* CPU 0 memory age control	    */
#define NI_AGE_CPU0_PIO		0x600508 /* CPU 0 PIO age control	    */
#define NI_AGE_CPU1_MEMORY	0x600510 /* CPU 1 memory age control	    */
#define NI_AGE_CPU1_PIO		0x600518 /* CPU 1 PIO age control	    */
#define NI_AGE_GBR_MEMORY	0x600520 /* GBR memory age control	    */
#define NI_AGE_GBR_PIO		0x600528 /* GBR PIO age control		    */
#define NI_AGE_IO_MEMORY	0x600530 /* IO memory age control	    */
#define NI_AGE_IO_PIO		0x600538 /* IO PIO age control		    */
#define NI_AGE_REG_MIN		NI_AGE_CPU0_MEMORY
#define NI_AGE_REG_MAX		NI_AGE_IO_PIO

#define NI_PORT_PARMS		0x608000 /* LLP Parameters		    */
#define NI_PORT_ERROR		0x608008 /* LLP Errors			    */
#define NI_PORT_ERROR_CLEAR	0x608088 /* Clear the error bits	    */

#define NI_META_TABLE0		0x638000 /* First meta routing table entry  */
#define NI_META_TABLE(_x)	(NI_META_TABLE0 + (8 * (_x)))
#define NI_META_ENTRIES		32

#define NI_LOCAL_TABLE0		0x638100 /* First local routing table entry */
#define NI_LOCAL_TABLE(_x)	(NI_LOCAL_TABLE0 + (8 * (_x)))
#define NI_LOCAL_ENTRIES	16

/*
 * NI_STATUS_REV_ID mask and shift definitions
 * Have to use UINT64_CAST instead of 'L' suffix, for assembler.
 */

#define NSRI_8BITMODE_SHFT	30
#define NSRI_8BITMODE_MASK	(UINT64_CAST 0x1 << 30)
#define NSRI_LINKUP_SHFT	29
#define NSRI_LINKUP_MASK	(UINT64_CAST 0x1 << 29)
#define NSRI_DOWNREASON_SHFT	28		/* 0=failed, 1=never came   */
#define NSRI_DOWNREASON_MASK	(UINT64_CAST 0x1 << 28) /*    out of reset. */
#define NSRI_MORENODES_SHFT	18
#define NSRI_MORENODES_MASK	(UINT64_CAST 1 << 18)	/* Max. # of nodes  */
#define	 MORE_MEMORY		0
#define	 MORE_NODES		1
#define NSRI_REGIONSIZE_SHFT	17
#define NSRI_REGIONSIZE_MASK	(UINT64_CAST 1 << 17)	/* Granularity	    */
#define	 REGIONSIZE_FINE	1
#define	 REGIONSIZE_COARSE	0
#define NSRI_NODEID_SHFT	8
#define NSRI_NODEID_MASK	(UINT64_CAST 0x1ff << 8)/* Node (Hub) ID    */
#define NSRI_REV_SHFT		4
#define NSRI_REV_MASK		(UINT64_CAST 0xf << 4)	/* Chip Revision    */
#define NSRI_CHIPID_SHFT	0
#define NSRI_CHIPID_MASK	(UINT64_CAST 0xf)	/* Chip type ID	    */

/*
 * In fine mode, each node is a region.	 In coarse mode, there are
 * eight nodes per region.
 */
#define NASID_TO_FINEREG_SHFT	0
#define NASID_TO_COARSEREG_SHFT 3

/* NI_PORT_RESET mask definitions */

#define NPR_PORTRESET		(UINT64_CAST 1 << 7)	/* Send warm reset  */
#define NPR_LINKRESET		(UINT64_CAST 1 << 1)	/* Send link reset  */
#define NPR_LOCALRESET		(UINT64_CAST 1)		/* Reset entire hub */

/* NI_PROTECTION mask and shift definitions */

#define NPROT_RESETOK		(UINT64_CAST 1)

/* NI_GLOBAL_PARMS mask and shift definitions */

#define NGP_MAXRETRY_SHFT	48		/* Maximum retries	    */
#define NGP_MAXRETRY_MASK	(UINT64_CAST 0x3ff << 48)
#define NGP_TAILTOWRAP_SHFT	32		/* Tail timeout wrap	    */
#define NGP_TAILTOWRAP_MASK	(UINT64_CAST 0xffff << 32)

#define NGP_CREDITTOVAL_SHFT	16		/* Tail timeout wrap	    */
#define NGP_CREDITTOVAL_MASK	(UINT64_CAST 0xf << 16)
#define NGP_TAILTOVAL_SHFT	4		/* Tail timeout value	    */
#define NGP_TAILTOVAL_MASK	(UINT64_CAST 0xf << 4)

/* NI_DIAG_PARMS mask and shift definitions */

#define NDP_PORTTORESET		(UINT64_CAST 1 << 18)	/* Port tmout reset */
#define NDP_LLP8BITMODE		(UINT64_CAST 1 << 12)	/* LLP 8-bit mode   */
#define NDP_PORTDISABLE		(UINT64_CAST 1 <<  6)	/* Port disable	    */
#define NDP_SENDERROR		(UINT64_CAST 1)		/* Send data error  */

/*
 * NI_VECTOR_PARMS mask and shift definitions.
 * TYPE may be any of the first four PIOTYPEs defined under NI_VECTOR_STATUS.
 */

#define NVP_PIOID_SHFT		40
#define NVP_PIOID_MASK		(UINT64_CAST 0x3ff << 40)
#define NVP_WRITEID_SHFT	32
#define NVP_WRITEID_MASK	(UINT64_CAST 0xff << 32)
#define NVP_ADDRESS_MASK	(UINT64_CAST 0xffff8)	/* Bits 19:3	    */
#define NVP_TYPE_SHFT		0
#define NVP_TYPE_MASK		(UINT64_CAST 0x3)

/* NI_VECTOR_STATUS mask and shift definitions */

#define NVS_VALID		(UINT64_CAST 1 << 63)
#define NVS_OVERRUN		(UINT64_CAST 1 << 62)
#define NVS_TARGET_SHFT		51
#define NVS_TARGET_MASK		(UINT64_CAST 0x3ff << 51)
#define NVS_PIOID_SHFT		40
#define NVS_PIOID_MASK		(UINT64_CAST 0x3ff << 40)
#define NVS_WRITEID_SHFT	32
#define NVS_WRITEID_MASK	(UINT64_CAST 0xff << 32)
#define NVS_ADDRESS_MASK	(UINT64_CAST 0xfffffff8)   /* Bits 31:3	    */
#define NVS_TYPE_SHFT		0
#define NVS_TYPE_MASK		(UINT64_CAST 0x7)
#define NVS_ERROR_MASK		(UINT64_CAST 0x4)  /* bit set means error */


#define	 PIOTYPE_READ		0	/* VECTOR_PARMS and VECTOR_STATUS   */
#define	 PIOTYPE_WRITE		1	/* VECTOR_PARMS and VECTOR_STATUS   */
#define	 PIOTYPE_UNDEFINED	2	/* VECTOR_PARMS and VECTOR_STATUS   */
#define	 PIOTYPE_EXCHANGE	3	/* VECTOR_PARMS and VECTOR_STATUS   */
#define	 PIOTYPE_ADDR_ERR	4	/* VECTOR_STATUS only		    */
#define	 PIOTYPE_CMD_ERR	5	/* VECTOR_STATUS only		    */
#define	 PIOTYPE_PROT_ERR	6	/* VECTOR_STATUS only		    */
#define	 PIOTYPE_UNKNOWN	7	/* VECTOR_STATUS only		    */

/* NI_AGE_XXX mask and shift definitions */

#define NAGE_VCH_SHFT		10
#define NAGE_VCH_MASK		(UINT64_CAST 3 << 10)
#define NAGE_CC_SHFT		8
#define NAGE_CC_MASK		(UINT64_CAST 3 << 8)
#define NAGE_AGE_SHFT		0
#define NAGE_AGE_MASK		(UINT64_CAST 0xff)
#define NAGE_MASK		(NAGE_VCH_MASK | NAGE_CC_MASK | NAGE_AGE_MASK)

#define	 VCHANNEL_A		0
#define	 VCHANNEL_B		1
#define	 VCHANNEL_ANY		2

/* NI_PORT_PARMS mask and shift definitions */

#define NPP_NULLTO_SHFT		10
#define NPP_NULLTO_MASK		(UINT64_CAST 0x3f << 16)
#define NPP_MAXBURST_SHFT	0
#define NPP_MAXBURST_MASK	(UINT64_CAST 0x3ff)
#define NPP_RESET_DFLT_HUB20	((UINT64_CAST 1	    << NPP_NULLTO_SHFT) | \
				 (UINT64_CAST 0x3f0 << NPP_MAXBURST_SHFT))
#define NPP_RESET_DEFAULTS	((UINT64_CAST 6	    << NPP_NULLTO_SHFT) | \
				 (UINT64_CAST 0x3f0 << NPP_MAXBURST_SHFT))


/* NI_PORT_ERROR mask and shift definitions */

#define NPE_LINKRESET		(UINT64_CAST 1 << 37)
#define NPE_INTERNALERROR	(UINT64_CAST 1 << 36)
#define NPE_BADMESSAGE		(UINT64_CAST 1 << 35)
#define NPE_BADDEST		(UINT64_CAST 1 << 34)
#define NPE_FIFOOVERFLOW	(UINT64_CAST 1 << 33)
#define NPE_CREDITTO_SHFT	28
#define NPE_CREDITTO_MASK	(UINT64_CAST 0xf << 28)
#define NPE_TAILTO_SHFT		24
#define NPE_TAILTO_MASK		(UINT64_CAST 0xf << 24)
#define NPE_RETRYCOUNT_SHFT	16
#define NPE_RETRYCOUNT_MASK	(UINT64_CAST 0xff << 16)
#define NPE_CBERRCOUNT_SHFT	8
#define NPE_CBERRCOUNT_MASK	(UINT64_CAST 0xff << 8)
#define NPE_SNERRCOUNT_SHFT	0
#define NPE_SNERRCOUNT_MASK	(UINT64_CAST 0xff << 0)
#define NPE_MASK		0x3effffffff

#define NPE_COUNT_MAX		0xff

#define NPE_FATAL_ERRORS	(NPE_LINKRESET | NPE_INTERNALERROR |	\
				 NPE_BADMESSAGE | NPE_BADDEST |		\
				 NPE_FIFOOVERFLOW | NPE_CREDITTO_MASK | \
				 NPE_TAILTO_MASK)

/* NI_META_TABLE mask and shift definitions */

#define NMT_EXIT_PORT_MASK (UINT64_CAST 0xf)

/* NI_LOCAL_TABLE mask and shift definitions */

#define NLT_EXIT_PORT_MASK (UINT64_CAST 0xf)

#ifndef __ASSEMBLER__

typedef union	hubni_port_error_u {
	u64	nipe_reg_value;
	struct {
	    u64 nipe_rsvd:	26,	/* unused */
		nipe_lnk_reset:	 1,	/* link reset */
		nipe_intl_err:	 1,	/* internal error */
		nipe_bad_msg:	 1,	/* bad message */
		nipe_bad_dest:	 1,	/* bad dest	*/
		nipe_fifo_ovfl:	 1,	/* fifo overflow */
		nipe_rsvd1:	 1,	/* unused */
		nipe_credit_to:	 4,	/* credit timeout */
		nipe_tail_to:	 4,	/* tail timeout */
		nipe_retry_cnt:	 8,	/* retry error count */
		nipe_cb_cnt:	 8,	/* checkbit error count */
		nipe_sn_cnt:	 8;	/* sequence number count */
	} nipe_fields_s;
} hubni_port_error_t;

#define NI_LLP_RETRY_MAX	0xff
#define NI_LLP_CB_MAX		0xff
#define NI_LLP_SN_MAX		0xff

static inline int get_region_shift(void)
{
	if (LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_REGIONSIZE_MASK)
		return NASID_TO_FINEREG_SHFT;

	return NASID_TO_COARSEREG_SHFT;
}

#endif /* !__ASSEMBLER__ */

#endif /* _ASM_SGI_SN0_HUBNI_H */
