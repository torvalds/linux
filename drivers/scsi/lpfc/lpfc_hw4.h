/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017 Broadcom. All Rights Reserved. The term      *
 * “Broadcom” refers to Broadcom Limited and/or its subsidiaries.  *
 * Copyright (C) 2009-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

/* Macros to deal with bit fields. Each bit field must have 3 #defines
 * associated with it (_SHIFT, _MASK, and _WORD).
 * EG. For a bit field that is in the 7th bit of the "field4" field of a
 * structure and is 2 bits in size the following #defines must exist:
 *	struct temp {
 *		uint32_t	field1;
 *		uint32_t	field2;
 *		uint32_t	field3;
 *		uint32_t	field4;
 *	#define example_bit_field_SHIFT		7
 *	#define example_bit_field_MASK		0x03
 *	#define example_bit_field_WORD		field4
 *		uint32_t	field5;
 *	};
 * Then the macros below may be used to get or set the value of that field.
 * EG. To get the value of the bit field from the above example:
 *	struct temp t1;
 *	value = bf_get(example_bit_field, &t1);
 * And then to set that bit field:
 *	bf_set(example_bit_field, &t1, 2);
 * Or clear that bit field:
 *	bf_set(example_bit_field, &t1, 0);
 */
#define bf_get_be32(name, ptr) \
	((be32_to_cpu((ptr)->name##_WORD) >> name##_SHIFT) & name##_MASK)
#define bf_get_le32(name, ptr) \
	((le32_to_cpu((ptr)->name##_WORD) >> name##_SHIFT) & name##_MASK)
#define bf_get(name, ptr) \
	(((ptr)->name##_WORD >> name##_SHIFT) & name##_MASK)
#define bf_set_le32(name, ptr, value) \
	((ptr)->name##_WORD = cpu_to_le32(((((value) & \
	name##_MASK) << name##_SHIFT) | (le32_to_cpu((ptr)->name##_WORD) & \
	~(name##_MASK << name##_SHIFT)))))
#define bf_set(name, ptr, value) \
	((ptr)->name##_WORD = ((((value) & name##_MASK) << name##_SHIFT) | \
		 ((ptr)->name##_WORD & ~(name##_MASK << name##_SHIFT))))

struct dma_address {
	uint32_t addr_lo;
	uint32_t addr_hi;
};

struct lpfc_sli_intf {
	uint32_t word0;
#define lpfc_sli_intf_valid_SHIFT		29
#define lpfc_sli_intf_valid_MASK		0x00000007
#define lpfc_sli_intf_valid_WORD		word0
#define LPFC_SLI_INTF_VALID		6
#define lpfc_sli_intf_sli_hint2_SHIFT		24
#define lpfc_sli_intf_sli_hint2_MASK		0x0000001F
#define lpfc_sli_intf_sli_hint2_WORD		word0
#define LPFC_SLI_INTF_SLI_HINT2_NONE	0
#define lpfc_sli_intf_sli_hint1_SHIFT		16
#define lpfc_sli_intf_sli_hint1_MASK		0x000000FF
#define lpfc_sli_intf_sli_hint1_WORD		word0
#define LPFC_SLI_INTF_SLI_HINT1_NONE	0
#define LPFC_SLI_INTF_SLI_HINT1_1	1
#define LPFC_SLI_INTF_SLI_HINT1_2	2
#define lpfc_sli_intf_if_type_SHIFT		12
#define lpfc_sli_intf_if_type_MASK		0x0000000F
#define lpfc_sli_intf_if_type_WORD		word0
#define LPFC_SLI_INTF_IF_TYPE_0		0
#define LPFC_SLI_INTF_IF_TYPE_1		1
#define LPFC_SLI_INTF_IF_TYPE_2		2
#define lpfc_sli_intf_sli_family_SHIFT		8
#define lpfc_sli_intf_sli_family_MASK		0x0000000F
#define lpfc_sli_intf_sli_family_WORD		word0
#define LPFC_SLI_INTF_FAMILY_BE2	0x0
#define LPFC_SLI_INTF_FAMILY_BE3	0x1
#define LPFC_SLI_INTF_FAMILY_LNCR_A0	0xa
#define LPFC_SLI_INTF_FAMILY_LNCR_B0	0xb
#define lpfc_sli_intf_slirev_SHIFT		4
#define lpfc_sli_intf_slirev_MASK		0x0000000F
#define lpfc_sli_intf_slirev_WORD		word0
#define LPFC_SLI_INTF_REV_SLI3		3
#define LPFC_SLI_INTF_REV_SLI4		4
#define lpfc_sli_intf_func_type_SHIFT		0
#define lpfc_sli_intf_func_type_MASK		0x00000001
#define lpfc_sli_intf_func_type_WORD		word0
#define LPFC_SLI_INTF_IF_TYPE_PHYS	0
#define LPFC_SLI_INTF_IF_TYPE_VIRT	1
};

#define LPFC_SLI4_MBX_EMBED	true
#define LPFC_SLI4_MBX_NEMBED	false

#define LPFC_SLI4_MB_WORD_COUNT		64
#define LPFC_MAX_MQ_PAGE		8
#define LPFC_MAX_WQ_PAGE_V0		4
#define LPFC_MAX_WQ_PAGE		8
#define LPFC_MAX_RQ_PAGE		8
#define LPFC_MAX_CQ_PAGE		4
#define LPFC_MAX_EQ_PAGE		8

#define LPFC_VIR_FUNC_MAX       32 /* Maximum number of virtual functions */
#define LPFC_PCI_FUNC_MAX        5 /* Maximum number of PCI functions */
#define LPFC_VFR_PAGE_SIZE	0x1000 /* 4KB BAR2 per-VF register page size */

/* Define SLI4 Alignment requirements. */
#define LPFC_ALIGN_16_BYTE	16
#define LPFC_ALIGN_64_BYTE	64

/* Define SLI4 specific definitions. */
#define LPFC_MQ_CQE_BYTE_OFFSET	256
#define LPFC_MBX_CMD_HDR_LENGTH 16
#define LPFC_MBX_ERROR_RANGE	0x4000
#define LPFC_BMBX_BIT1_ADDR_HI	0x2
#define LPFC_BMBX_BIT1_ADDR_LO	0
#define LPFC_RPI_HDR_COUNT	64
#define LPFC_HDR_TEMPLATE_SIZE	4096
#define LPFC_RPI_ALLOC_ERROR 	0xFFFF
#define LPFC_FCF_RECORD_WD_CNT	132
#define LPFC_ENTIRE_FCF_DATABASE 0
#define LPFC_DFLT_FCF_INDEX	 0

/* Virtual function numbers */
#define LPFC_VF0		0
#define LPFC_VF1		1
#define LPFC_VF2		2
#define LPFC_VF3		3
#define LPFC_VF4		4
#define LPFC_VF5		5
#define LPFC_VF6		6
#define LPFC_VF7		7
#define LPFC_VF8		8
#define LPFC_VF9		9
#define LPFC_VF10		10
#define LPFC_VF11		11
#define LPFC_VF12		12
#define LPFC_VF13		13
#define LPFC_VF14		14
#define LPFC_VF15		15
#define LPFC_VF16		16
#define LPFC_VF17		17
#define LPFC_VF18		18
#define LPFC_VF19		19
#define LPFC_VF20		20
#define LPFC_VF21		21
#define LPFC_VF22		22
#define LPFC_VF23		23
#define LPFC_VF24		24
#define LPFC_VF25		25
#define LPFC_VF26		26
#define LPFC_VF27		27
#define LPFC_VF28		28
#define LPFC_VF29		29
#define LPFC_VF30		30
#define LPFC_VF31		31

/* PCI function numbers */
#define LPFC_PCI_FUNC0		0
#define LPFC_PCI_FUNC1		1
#define LPFC_PCI_FUNC2		2
#define LPFC_PCI_FUNC3		3
#define LPFC_PCI_FUNC4		4

/* SLI4 interface type-2 PDEV_CTL register */
#define LPFC_CTL_PDEV_CTL_OFFSET	0x414
#define LPFC_CTL_PDEV_CTL_DRST		0x00000001
#define LPFC_CTL_PDEV_CTL_FRST		0x00000002
#define LPFC_CTL_PDEV_CTL_DD		0x00000004
#define LPFC_CTL_PDEV_CTL_LC		0x00000008
#define LPFC_CTL_PDEV_CTL_FRL_ALL	0x00
#define LPFC_CTL_PDEV_CTL_FRL_FC_FCOE	0x10
#define LPFC_CTL_PDEV_CTL_FRL_NIC	0x20

#define LPFC_FW_DUMP_REQUEST    (LPFC_CTL_PDEV_CTL_DD | LPFC_CTL_PDEV_CTL_FRST)

/* Active interrupt test count */
#define LPFC_ACT_INTR_CNT	4

/* Algrithmns for scheduling FCP commands to WQs */
#define	LPFC_FCP_SCHED_ROUND_ROBIN	0
#define	LPFC_FCP_SCHED_BY_CPU		1

/* Delay Multiplier constant */
#define LPFC_DMULT_CONST       651042

/* Configuration of Interrupts / sec for entire HBA port */
#define LPFC_MIN_IMAX          5000
#define LPFC_MAX_IMAX          5000000
#define LPFC_DEF_IMAX          150000

#define LPFC_MIN_CPU_MAP       0
#define LPFC_MAX_CPU_MAP       2
#define LPFC_HBA_CPU_MAP       1
#define LPFC_DRIVER_CPU_MAP    2  /* Default */

/* PORT_CAPABILITIES constants. */
#define LPFC_MAX_SUPPORTED_PAGES	8

struct ulp_bde64 {
	union ULP_BDE_TUS {
		uint32_t w;
		struct {
#ifdef __BIG_ENDIAN_BITFIELD
			uint32_t bdeFlags:8;	/* BDE Flags 0 IS A SUPPORTED
						   VALUE !! */
			uint32_t bdeSize:24;	/* Size of buffer (in bytes) */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
			uint32_t bdeSize:24;	/* Size of buffer (in bytes) */
			uint32_t bdeFlags:8;	/* BDE Flags 0 IS A SUPPORTED
						   VALUE !! */
#endif
#define BUFF_TYPE_BDE_64    0x00	/* BDE (Host_resident) */
#define BUFF_TYPE_BDE_IMMED 0x01	/* Immediate Data BDE */
#define BUFF_TYPE_BDE_64P   0x02	/* BDE (Port-resident) */
#define BUFF_TYPE_BDE_64I   0x08	/* Input BDE (Host-resident) */
#define BUFF_TYPE_BDE_64IP  0x0A	/* Input BDE (Port-resident) */
#define BUFF_TYPE_BLP_64    0x40	/* BLP (Host-resident) */
#define BUFF_TYPE_BLP_64P   0x42	/* BLP (Port-resident) */
		} f;
	} tus;
	uint32_t addrLow;
	uint32_t addrHigh;
};

/* Maximun size of immediate data that can fit into a 128 byte WQE */
#define LPFC_MAX_BDE_IMM_SIZE	64

struct lpfc_sli4_flags {
	uint32_t word0;
#define lpfc_idx_rsrc_rdy_SHIFT		0
#define lpfc_idx_rsrc_rdy_MASK		0x00000001
#define lpfc_idx_rsrc_rdy_WORD		word0
#define LPFC_IDX_RSRC_RDY		1
#define lpfc_rpi_rsrc_rdy_SHIFT		1
#define lpfc_rpi_rsrc_rdy_MASK		0x00000001
#define lpfc_rpi_rsrc_rdy_WORD		word0
#define LPFC_RPI_RSRC_RDY		1
#define lpfc_vpi_rsrc_rdy_SHIFT		2
#define lpfc_vpi_rsrc_rdy_MASK		0x00000001
#define lpfc_vpi_rsrc_rdy_WORD		word0
#define LPFC_VPI_RSRC_RDY		1
#define lpfc_vfi_rsrc_rdy_SHIFT		3
#define lpfc_vfi_rsrc_rdy_MASK		0x00000001
#define lpfc_vfi_rsrc_rdy_WORD		word0
#define LPFC_VFI_RSRC_RDY		1
};

struct sli4_bls_rsp {
	uint32_t word0_rsvd;      /* Word0 must be reserved */
	uint32_t word1;
#define lpfc_abts_orig_SHIFT      0
#define lpfc_abts_orig_MASK       0x00000001
#define lpfc_abts_orig_WORD       word1
#define LPFC_ABTS_UNSOL_RSP       1
#define LPFC_ABTS_UNSOL_INT       0
	uint32_t word2;
#define lpfc_abts_rxid_SHIFT      0
#define lpfc_abts_rxid_MASK       0x0000FFFF
#define lpfc_abts_rxid_WORD       word2
#define lpfc_abts_oxid_SHIFT      16
#define lpfc_abts_oxid_MASK       0x0000FFFF
#define lpfc_abts_oxid_WORD       word2
	uint32_t word3;
#define lpfc_vndr_code_SHIFT	0
#define lpfc_vndr_code_MASK	0x000000FF
#define lpfc_vndr_code_WORD	word3
#define lpfc_rsn_expln_SHIFT	8
#define lpfc_rsn_expln_MASK	0x000000FF
#define lpfc_rsn_expln_WORD	word3
#define lpfc_rsn_code_SHIFT	16
#define lpfc_rsn_code_MASK	0x000000FF
#define lpfc_rsn_code_WORD	word3

	uint32_t word4;
	uint32_t word5_rsvd;	/* Word5 must be reserved */
};

/* event queue entry structure */
struct lpfc_eqe {
	uint32_t word0;
#define lpfc_eqe_resource_id_SHIFT	16
#define lpfc_eqe_resource_id_MASK	0x0000FFFF
#define lpfc_eqe_resource_id_WORD	word0
#define lpfc_eqe_minor_code_SHIFT	4
#define lpfc_eqe_minor_code_MASK	0x00000FFF
#define lpfc_eqe_minor_code_WORD	word0
#define lpfc_eqe_major_code_SHIFT	1
#define lpfc_eqe_major_code_MASK	0x00000007
#define lpfc_eqe_major_code_WORD	word0
#define lpfc_eqe_valid_SHIFT		0
#define lpfc_eqe_valid_MASK		0x00000001
#define lpfc_eqe_valid_WORD		word0
};

/* completion queue entry structure (common fields for all cqe types) */
struct lpfc_cqe {
	uint32_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t word3;
#define lpfc_cqe_valid_SHIFT		31
#define lpfc_cqe_valid_MASK		0x00000001
#define lpfc_cqe_valid_WORD		word3
#define lpfc_cqe_code_SHIFT		16
#define lpfc_cqe_code_MASK		0x000000FF
#define lpfc_cqe_code_WORD		word3
};

/* Completion Queue Entry Status Codes */
#define CQE_STATUS_SUCCESS		0x0
#define CQE_STATUS_FCP_RSP_FAILURE	0x1
#define CQE_STATUS_REMOTE_STOP		0x2
#define CQE_STATUS_LOCAL_REJECT		0x3
#define CQE_STATUS_NPORT_RJT		0x4
#define CQE_STATUS_FABRIC_RJT		0x5
#define CQE_STATUS_NPORT_BSY		0x6
#define CQE_STATUS_FABRIC_BSY		0x7
#define CQE_STATUS_INTERMED_RSP		0x8
#define CQE_STATUS_LS_RJT		0x9
#define CQE_STATUS_CMD_REJECT		0xb
#define CQE_STATUS_FCP_TGT_LENCHECK	0xc
#define CQE_STATUS_NEED_BUFF_ENTRY	0xf
#define CQE_STATUS_DI_ERROR		0x16

/* Used when mapping CQE status to IOCB */
#define LPFC_IOCB_STATUS_MASK		0xf

/* Status returned by hardware (valid only if status = CQE_STATUS_SUCCESS). */
#define CQE_HW_STATUS_NO_ERR		0x0
#define CQE_HW_STATUS_UNDERRUN		0x1
#define CQE_HW_STATUS_OVERRUN		0x2

/* Completion Queue Entry Codes */
#define CQE_CODE_COMPL_WQE		0x1
#define CQE_CODE_RELEASE_WQE		0x2
#define CQE_CODE_RECEIVE		0x4
#define CQE_CODE_XRI_ABORTED		0x5
#define CQE_CODE_RECEIVE_V1		0x9
#define CQE_CODE_NVME_ERSP		0xd

/*
 * Define mask value for xri_aborted and wcqe completed CQE extended status.
 * Currently, extended status is limited to 9 bits (0x0 -> 0x103) .
 */
#define WCQE_PARAM_MASK		0x1FF

/* completion queue entry for wqe completions */
struct lpfc_wcqe_complete {
	uint32_t word0;
#define lpfc_wcqe_c_request_tag_SHIFT	16
#define lpfc_wcqe_c_request_tag_MASK	0x0000FFFF
#define lpfc_wcqe_c_request_tag_WORD	word0
#define lpfc_wcqe_c_status_SHIFT	8
#define lpfc_wcqe_c_status_MASK		0x000000FF
#define lpfc_wcqe_c_status_WORD		word0
#define lpfc_wcqe_c_hw_status_SHIFT	0
#define lpfc_wcqe_c_hw_status_MASK	0x000000FF
#define lpfc_wcqe_c_hw_status_WORD	word0
#define lpfc_wcqe_c_ersp0_SHIFT		0
#define lpfc_wcqe_c_ersp0_MASK		0x0000FFFF
#define lpfc_wcqe_c_ersp0_WORD		word0
	uint32_t total_data_placed;
	uint32_t parameter;
#define lpfc_wcqe_c_bg_edir_SHIFT	5
#define lpfc_wcqe_c_bg_edir_MASK	0x00000001
#define lpfc_wcqe_c_bg_edir_WORD	parameter
#define lpfc_wcqe_c_bg_tdpv_SHIFT	3
#define lpfc_wcqe_c_bg_tdpv_MASK	0x00000001
#define lpfc_wcqe_c_bg_tdpv_WORD	parameter
#define lpfc_wcqe_c_bg_re_SHIFT		2
#define lpfc_wcqe_c_bg_re_MASK		0x00000001
#define lpfc_wcqe_c_bg_re_WORD		parameter
#define lpfc_wcqe_c_bg_ae_SHIFT		1
#define lpfc_wcqe_c_bg_ae_MASK		0x00000001
#define lpfc_wcqe_c_bg_ae_WORD		parameter
#define lpfc_wcqe_c_bg_ge_SHIFT		0
#define lpfc_wcqe_c_bg_ge_MASK		0x00000001
#define lpfc_wcqe_c_bg_ge_WORD		parameter
	uint32_t word3;
#define lpfc_wcqe_c_valid_SHIFT		lpfc_cqe_valid_SHIFT
#define lpfc_wcqe_c_valid_MASK		lpfc_cqe_valid_MASK
#define lpfc_wcqe_c_valid_WORD		lpfc_cqe_valid_WORD
#define lpfc_wcqe_c_xb_SHIFT		28
#define lpfc_wcqe_c_xb_MASK		0x00000001
#define lpfc_wcqe_c_xb_WORD		word3
#define lpfc_wcqe_c_pv_SHIFT		27
#define lpfc_wcqe_c_pv_MASK		0x00000001
#define lpfc_wcqe_c_pv_WORD		word3
#define lpfc_wcqe_c_priority_SHIFT	24
#define lpfc_wcqe_c_priority_MASK	0x00000007
#define lpfc_wcqe_c_priority_WORD	word3
#define lpfc_wcqe_c_code_SHIFT		lpfc_cqe_code_SHIFT
#define lpfc_wcqe_c_code_MASK		lpfc_cqe_code_MASK
#define lpfc_wcqe_c_code_WORD		lpfc_cqe_code_WORD
#define lpfc_wcqe_c_sqhead_SHIFT	0
#define lpfc_wcqe_c_sqhead_MASK		0x0000FFFF
#define lpfc_wcqe_c_sqhead_WORD		word3
};

/* completion queue entry for wqe release */
struct lpfc_wcqe_release {
	uint32_t reserved0;
	uint32_t reserved1;
	uint32_t word2;
#define lpfc_wcqe_r_wq_id_SHIFT		16
#define lpfc_wcqe_r_wq_id_MASK		0x0000FFFF
#define lpfc_wcqe_r_wq_id_WORD		word2
#define lpfc_wcqe_r_wqe_index_SHIFT	0
#define lpfc_wcqe_r_wqe_index_MASK	0x0000FFFF
#define lpfc_wcqe_r_wqe_index_WORD	word2
	uint32_t word3;
#define lpfc_wcqe_r_valid_SHIFT		lpfc_cqe_valid_SHIFT
#define lpfc_wcqe_r_valid_MASK		lpfc_cqe_valid_MASK
#define lpfc_wcqe_r_valid_WORD		lpfc_cqe_valid_WORD
#define lpfc_wcqe_r_code_SHIFT		lpfc_cqe_code_SHIFT
#define lpfc_wcqe_r_code_MASK		lpfc_cqe_code_MASK
#define lpfc_wcqe_r_code_WORD		lpfc_cqe_code_WORD
};

struct sli4_wcqe_xri_aborted {
	uint32_t word0;
#define lpfc_wcqe_xa_status_SHIFT		8
#define lpfc_wcqe_xa_status_MASK		0x000000FF
#define lpfc_wcqe_xa_status_WORD		word0
	uint32_t parameter;
	uint32_t word2;
#define lpfc_wcqe_xa_remote_xid_SHIFT	16
#define lpfc_wcqe_xa_remote_xid_MASK	0x0000FFFF
#define lpfc_wcqe_xa_remote_xid_WORD	word2
#define lpfc_wcqe_xa_xri_SHIFT		0
#define lpfc_wcqe_xa_xri_MASK		0x0000FFFF
#define lpfc_wcqe_xa_xri_WORD		word2
	uint32_t word3;
#define lpfc_wcqe_xa_valid_SHIFT	lpfc_cqe_valid_SHIFT
#define lpfc_wcqe_xa_valid_MASK		lpfc_cqe_valid_MASK
#define lpfc_wcqe_xa_valid_WORD		lpfc_cqe_valid_WORD
#define lpfc_wcqe_xa_ia_SHIFT		30
#define lpfc_wcqe_xa_ia_MASK		0x00000001
#define lpfc_wcqe_xa_ia_WORD		word3
#define CQE_XRI_ABORTED_IA_REMOTE	0
#define CQE_XRI_ABORTED_IA_LOCAL	1
#define lpfc_wcqe_xa_br_SHIFT		29
#define lpfc_wcqe_xa_br_MASK		0x00000001
#define lpfc_wcqe_xa_br_WORD		word3
#define CQE_XRI_ABORTED_BR_BA_ACC	0
#define CQE_XRI_ABORTED_BR_BA_RJT	1
#define lpfc_wcqe_xa_eo_SHIFT		28
#define lpfc_wcqe_xa_eo_MASK		0x00000001
#define lpfc_wcqe_xa_eo_WORD		word3
#define CQE_XRI_ABORTED_EO_REMOTE	0
#define CQE_XRI_ABORTED_EO_LOCAL	1
#define lpfc_wcqe_xa_code_SHIFT		lpfc_cqe_code_SHIFT
#define lpfc_wcqe_xa_code_MASK		lpfc_cqe_code_MASK
#define lpfc_wcqe_xa_code_WORD		lpfc_cqe_code_WORD
};

/* completion queue entry structure for rqe completion */
struct lpfc_rcqe {
	uint32_t word0;
#define lpfc_rcqe_bindex_SHIFT		16
#define lpfc_rcqe_bindex_MASK		0x0000FFF
#define lpfc_rcqe_bindex_WORD		word0
#define lpfc_rcqe_status_SHIFT		8
#define lpfc_rcqe_status_MASK		0x000000FF
#define lpfc_rcqe_status_WORD		word0
#define FC_STATUS_RQ_SUCCESS		0x10 /* Async receive successful */
#define FC_STATUS_RQ_BUF_LEN_EXCEEDED 	0x11 /* payload truncated */
#define FC_STATUS_INSUFF_BUF_NEED_BUF 	0x12 /* Insufficient buffers */
#define FC_STATUS_INSUFF_BUF_FRM_DISC 	0x13 /* Frame Discard */
	uint32_t word1;
#define lpfc_rcqe_fcf_id_v1_SHIFT	0
#define lpfc_rcqe_fcf_id_v1_MASK	0x0000003F
#define lpfc_rcqe_fcf_id_v1_WORD	word1
	uint32_t word2;
#define lpfc_rcqe_length_SHIFT		16
#define lpfc_rcqe_length_MASK		0x0000FFFF
#define lpfc_rcqe_length_WORD		word2
#define lpfc_rcqe_rq_id_SHIFT		6
#define lpfc_rcqe_rq_id_MASK		0x000003FF
#define lpfc_rcqe_rq_id_WORD		word2
#define lpfc_rcqe_fcf_id_SHIFT		0
#define lpfc_rcqe_fcf_id_MASK		0x0000003F
#define lpfc_rcqe_fcf_id_WORD		word2
#define lpfc_rcqe_rq_id_v1_SHIFT	0
#define lpfc_rcqe_rq_id_v1_MASK		0x0000FFFF
#define lpfc_rcqe_rq_id_v1_WORD		word2
	uint32_t word3;
#define lpfc_rcqe_valid_SHIFT		lpfc_cqe_valid_SHIFT
#define lpfc_rcqe_valid_MASK		lpfc_cqe_valid_MASK
#define lpfc_rcqe_valid_WORD		lpfc_cqe_valid_WORD
#define lpfc_rcqe_port_SHIFT		30
#define lpfc_rcqe_port_MASK		0x00000001
#define lpfc_rcqe_port_WORD		word3
#define lpfc_rcqe_hdr_length_SHIFT	24
#define lpfc_rcqe_hdr_length_MASK	0x0000001F
#define lpfc_rcqe_hdr_length_WORD	word3
#define lpfc_rcqe_code_SHIFT		lpfc_cqe_code_SHIFT
#define lpfc_rcqe_code_MASK		lpfc_cqe_code_MASK
#define lpfc_rcqe_code_WORD		lpfc_cqe_code_WORD
#define lpfc_rcqe_eof_SHIFT		8
#define lpfc_rcqe_eof_MASK		0x000000FF
#define lpfc_rcqe_eof_WORD		word3
#define FCOE_EOFn	0x41
#define FCOE_EOFt	0x42
#define FCOE_EOFni	0x49
#define FCOE_EOFa	0x50
#define lpfc_rcqe_sof_SHIFT		0
#define lpfc_rcqe_sof_MASK		0x000000FF
#define lpfc_rcqe_sof_WORD		word3
#define FCOE_SOFi2	0x2d
#define FCOE_SOFi3	0x2e
#define FCOE_SOFn2	0x35
#define FCOE_SOFn3	0x36
};

struct lpfc_rqe {
	uint32_t address_hi;
	uint32_t address_lo;
};

/* buffer descriptors */
struct lpfc_bde4 {
	uint32_t addr_hi;
	uint32_t addr_lo;
	uint32_t word2;
#define lpfc_bde4_last_SHIFT		31
#define lpfc_bde4_last_MASK		0x00000001
#define lpfc_bde4_last_WORD		word2
#define lpfc_bde4_sge_offset_SHIFT	0
#define lpfc_bde4_sge_offset_MASK	0x000003FF
#define lpfc_bde4_sge_offset_WORD	word2
	uint32_t word3;
#define lpfc_bde4_length_SHIFT		0
#define lpfc_bde4_length_MASK		0x000000FF
#define lpfc_bde4_length_WORD		word3
};

struct lpfc_register {
	uint32_t word0;
};

#define LPFC_PORT_SEM_UE_RECOVERABLE    0xE000
#define LPFC_PORT_SEM_MASK		0xF000
/* The following BAR0 Registers apply to SLI4 if_type 0 UCNAs. */
#define LPFC_UERR_STATUS_HI		0x00A4
#define LPFC_UERR_STATUS_LO		0x00A0
#define LPFC_UE_MASK_HI			0x00AC
#define LPFC_UE_MASK_LO			0x00A8

/* The following BAR0 register sets are defined for if_type 0 and 2 UCNAs. */
#define LPFC_SLI_INTF			0x0058

#define LPFC_CTL_PORT_SEM_OFFSET	0x400
#define lpfc_port_smphr_perr_SHIFT	31
#define lpfc_port_smphr_perr_MASK	0x1
#define lpfc_port_smphr_perr_WORD	word0
#define lpfc_port_smphr_sfi_SHIFT	30
#define lpfc_port_smphr_sfi_MASK	0x1
#define lpfc_port_smphr_sfi_WORD	word0
#define lpfc_port_smphr_nip_SHIFT	29
#define lpfc_port_smphr_nip_MASK	0x1
#define lpfc_port_smphr_nip_WORD	word0
#define lpfc_port_smphr_ipc_SHIFT	28
#define lpfc_port_smphr_ipc_MASK	0x1
#define lpfc_port_smphr_ipc_WORD	word0
#define lpfc_port_smphr_scr1_SHIFT	27
#define lpfc_port_smphr_scr1_MASK	0x1
#define lpfc_port_smphr_scr1_WORD	word0
#define lpfc_port_smphr_scr2_SHIFT	26
#define lpfc_port_smphr_scr2_MASK	0x1
#define lpfc_port_smphr_scr2_WORD	word0
#define lpfc_port_smphr_host_scratch_SHIFT	16
#define lpfc_port_smphr_host_scratch_MASK	0xFF
#define lpfc_port_smphr_host_scratch_WORD	word0
#define lpfc_port_smphr_port_status_SHIFT	0
#define lpfc_port_smphr_port_status_MASK	0xFFFF
#define lpfc_port_smphr_port_status_WORD	word0

#define LPFC_POST_STAGE_POWER_ON_RESET			0x0000
#define LPFC_POST_STAGE_AWAITING_HOST_RDY		0x0001
#define LPFC_POST_STAGE_HOST_RDY			0x0002
#define LPFC_POST_STAGE_BE_RESET			0x0003
#define LPFC_POST_STAGE_SEEPROM_CS_START		0x0100
#define LPFC_POST_STAGE_SEEPROM_CS_DONE			0x0101
#define LPFC_POST_STAGE_DDR_CONFIG_START		0x0200
#define LPFC_POST_STAGE_DDR_CONFIG_DONE			0x0201
#define LPFC_POST_STAGE_DDR_CALIBRATE_START		0x0300
#define LPFC_POST_STAGE_DDR_CALIBRATE_DONE		0x0301
#define LPFC_POST_STAGE_DDR_TEST_START			0x0400
#define LPFC_POST_STAGE_DDR_TEST_DONE			0x0401
#define LPFC_POST_STAGE_REDBOOT_INIT_START		0x0600
#define LPFC_POST_STAGE_REDBOOT_INIT_DONE		0x0601
#define LPFC_POST_STAGE_FW_IMAGE_LOAD_START		0x0700
#define LPFC_POST_STAGE_FW_IMAGE_LOAD_DONE		0x0701
#define LPFC_POST_STAGE_ARMFW_START			0x0800
#define LPFC_POST_STAGE_DHCP_QUERY_START		0x0900
#define LPFC_POST_STAGE_DHCP_QUERY_DONE			0x0901
#define LPFC_POST_STAGE_BOOT_TARGET_DISCOVERY_START	0x0A00
#define LPFC_POST_STAGE_BOOT_TARGET_DISCOVERY_DONE	0x0A01
#define LPFC_POST_STAGE_RC_OPTION_SET			0x0B00
#define LPFC_POST_STAGE_SWITCH_LINK			0x0B01
#define LPFC_POST_STAGE_SEND_ICDS_MESSAGE		0x0B02
#define LPFC_POST_STAGE_PERFROM_TFTP			0x0B03
#define LPFC_POST_STAGE_PARSE_XML			0x0B04
#define LPFC_POST_STAGE_DOWNLOAD_IMAGE			0x0B05
#define LPFC_POST_STAGE_FLASH_IMAGE			0x0B06
#define LPFC_POST_STAGE_RC_DONE				0x0B07
#define LPFC_POST_STAGE_REBOOT_SYSTEM			0x0B08
#define LPFC_POST_STAGE_MAC_ADDRESS			0x0C00
#define LPFC_POST_STAGE_PORT_READY			0xC000
#define LPFC_POST_STAGE_PORT_UE 			0xF000

#define LPFC_CTL_PORT_STA_OFFSET	0x404
#define lpfc_sliport_status_err_SHIFT	31
#define lpfc_sliport_status_err_MASK	0x1
#define lpfc_sliport_status_err_WORD	word0
#define lpfc_sliport_status_end_SHIFT	30
#define lpfc_sliport_status_end_MASK	0x1
#define lpfc_sliport_status_end_WORD	word0
#define lpfc_sliport_status_oti_SHIFT	29
#define lpfc_sliport_status_oti_MASK	0x1
#define lpfc_sliport_status_oti_WORD	word0
#define lpfc_sliport_status_rn_SHIFT	24
#define lpfc_sliport_status_rn_MASK	0x1
#define lpfc_sliport_status_rn_WORD	word0
#define lpfc_sliport_status_rdy_SHIFT	23
#define lpfc_sliport_status_rdy_MASK	0x1
#define lpfc_sliport_status_rdy_WORD	word0
#define MAX_IF_TYPE_2_RESETS		6

#define LPFC_CTL_PORT_CTL_OFFSET	0x408
#define lpfc_sliport_ctrl_end_SHIFT	30
#define lpfc_sliport_ctrl_end_MASK	0x1
#define lpfc_sliport_ctrl_end_WORD	word0
#define LPFC_SLIPORT_LITTLE_ENDIAN 0
#define LPFC_SLIPORT_BIG_ENDIAN	   1
#define lpfc_sliport_ctrl_ip_SHIFT	27
#define lpfc_sliport_ctrl_ip_MASK	0x1
#define lpfc_sliport_ctrl_ip_WORD	word0
#define LPFC_SLIPORT_INIT_PORT	1

#define LPFC_CTL_PORT_ER1_OFFSET	0x40C
#define LPFC_CTL_PORT_ER2_OFFSET	0x410

/* The following Registers apply to SLI4 if_type 0 UCNAs. They typically
 * reside in BAR 2.
 */
#define LPFC_SLIPORT_IF0_SMPHR	0x00AC

#define LPFC_IMR_MASK_ALL	0xFFFFFFFF
#define LPFC_ISCR_CLEAR_ALL	0xFFFFFFFF

#define LPFC_HST_ISR0		0x0C18
#define LPFC_HST_ISR1		0x0C1C
#define LPFC_HST_ISR2		0x0C20
#define LPFC_HST_ISR3		0x0C24
#define LPFC_HST_ISR4		0x0C28

#define LPFC_HST_IMR0		0x0C48
#define LPFC_HST_IMR1		0x0C4C
#define LPFC_HST_IMR2		0x0C50
#define LPFC_HST_IMR3		0x0C54
#define LPFC_HST_IMR4		0x0C58

#define LPFC_HST_ISCR0		0x0C78
#define LPFC_HST_ISCR1		0x0C7C
#define LPFC_HST_ISCR2		0x0C80
#define LPFC_HST_ISCR3		0x0C84
#define LPFC_HST_ISCR4		0x0C88

#define LPFC_SLI4_INTR0			BIT0
#define LPFC_SLI4_INTR1			BIT1
#define LPFC_SLI4_INTR2			BIT2
#define LPFC_SLI4_INTR3			BIT3
#define LPFC_SLI4_INTR4			BIT4
#define LPFC_SLI4_INTR5			BIT5
#define LPFC_SLI4_INTR6			BIT6
#define LPFC_SLI4_INTR7			BIT7
#define LPFC_SLI4_INTR8			BIT8
#define LPFC_SLI4_INTR9			BIT9
#define LPFC_SLI4_INTR10		BIT10
#define LPFC_SLI4_INTR11		BIT11
#define LPFC_SLI4_INTR12		BIT12
#define LPFC_SLI4_INTR13		BIT13
#define LPFC_SLI4_INTR14		BIT14
#define LPFC_SLI4_INTR15		BIT15
#define LPFC_SLI4_INTR16		BIT16
#define LPFC_SLI4_INTR17		BIT17
#define LPFC_SLI4_INTR18		BIT18
#define LPFC_SLI4_INTR19		BIT19
#define LPFC_SLI4_INTR20		BIT20
#define LPFC_SLI4_INTR21		BIT21
#define LPFC_SLI4_INTR22		BIT22
#define LPFC_SLI4_INTR23		BIT23
#define LPFC_SLI4_INTR24		BIT24
#define LPFC_SLI4_INTR25		BIT25
#define LPFC_SLI4_INTR26		BIT26
#define LPFC_SLI4_INTR27		BIT27
#define LPFC_SLI4_INTR28		BIT28
#define LPFC_SLI4_INTR29		BIT29
#define LPFC_SLI4_INTR30		BIT30
#define LPFC_SLI4_INTR31		BIT31

/*
 * The Doorbell registers defined here exist in different BAR
 * register sets depending on the UCNA Port's reported if_type
 * value.  For UCNA ports running SLI4 and if_type 0, they reside in
 * BAR4.  For UCNA ports running SLI4 and if_type 2, they reside in
 * BAR0.  The offsets are the same so the driver must account for
 * any base address difference.
 */
#define LPFC_ULP0_RQ_DOORBELL		0x00A0
#define LPFC_ULP1_RQ_DOORBELL		0x00C0
#define lpfc_rq_db_list_fm_num_posted_SHIFT	24
#define lpfc_rq_db_list_fm_num_posted_MASK	0x00FF
#define lpfc_rq_db_list_fm_num_posted_WORD	word0
#define lpfc_rq_db_list_fm_index_SHIFT		16
#define lpfc_rq_db_list_fm_index_MASK		0x00FF
#define lpfc_rq_db_list_fm_index_WORD		word0
#define lpfc_rq_db_list_fm_id_SHIFT		0
#define lpfc_rq_db_list_fm_id_MASK		0xFFFF
#define lpfc_rq_db_list_fm_id_WORD		word0
#define lpfc_rq_db_ring_fm_num_posted_SHIFT	16
#define lpfc_rq_db_ring_fm_num_posted_MASK	0x3FFF
#define lpfc_rq_db_ring_fm_num_posted_WORD	word0
#define lpfc_rq_db_ring_fm_id_SHIFT		0
#define lpfc_rq_db_ring_fm_id_MASK		0xFFFF
#define lpfc_rq_db_ring_fm_id_WORD		word0

#define LPFC_ULP0_WQ_DOORBELL		0x0040
#define LPFC_ULP1_WQ_DOORBELL		0x0060
#define lpfc_wq_db_list_fm_num_posted_SHIFT	24
#define lpfc_wq_db_list_fm_num_posted_MASK	0x00FF
#define lpfc_wq_db_list_fm_num_posted_WORD	word0
#define lpfc_wq_db_list_fm_index_SHIFT		16
#define lpfc_wq_db_list_fm_index_MASK		0x00FF
#define lpfc_wq_db_list_fm_index_WORD		word0
#define lpfc_wq_db_list_fm_id_SHIFT		0
#define lpfc_wq_db_list_fm_id_MASK		0xFFFF
#define lpfc_wq_db_list_fm_id_WORD		word0
#define lpfc_wq_db_ring_fm_num_posted_SHIFT     16
#define lpfc_wq_db_ring_fm_num_posted_MASK      0x3FFF
#define lpfc_wq_db_ring_fm_num_posted_WORD      word0
#define lpfc_wq_db_ring_fm_id_SHIFT             0
#define lpfc_wq_db_ring_fm_id_MASK              0xFFFF
#define lpfc_wq_db_ring_fm_id_WORD              word0

#define LPFC_EQCQ_DOORBELL		0x0120
#define lpfc_eqcq_doorbell_se_SHIFT		31
#define lpfc_eqcq_doorbell_se_MASK		0x0001
#define lpfc_eqcq_doorbell_se_WORD		word0
#define LPFC_EQCQ_SOLICIT_ENABLE_OFF	0
#define LPFC_EQCQ_SOLICIT_ENABLE_ON	1
#define lpfc_eqcq_doorbell_arm_SHIFT		29
#define lpfc_eqcq_doorbell_arm_MASK		0x0001
#define lpfc_eqcq_doorbell_arm_WORD		word0
#define lpfc_eqcq_doorbell_num_released_SHIFT	16
#define lpfc_eqcq_doorbell_num_released_MASK	0x1FFF
#define lpfc_eqcq_doorbell_num_released_WORD	word0
#define lpfc_eqcq_doorbell_qt_SHIFT		10
#define lpfc_eqcq_doorbell_qt_MASK		0x0001
#define lpfc_eqcq_doorbell_qt_WORD		word0
#define LPFC_QUEUE_TYPE_COMPLETION	0
#define LPFC_QUEUE_TYPE_EVENT		1
#define lpfc_eqcq_doorbell_eqci_SHIFT		9
#define lpfc_eqcq_doorbell_eqci_MASK		0x0001
#define lpfc_eqcq_doorbell_eqci_WORD		word0
#define lpfc_eqcq_doorbell_cqid_lo_SHIFT	0
#define lpfc_eqcq_doorbell_cqid_lo_MASK		0x03FF
#define lpfc_eqcq_doorbell_cqid_lo_WORD		word0
#define lpfc_eqcq_doorbell_cqid_hi_SHIFT	11
#define lpfc_eqcq_doorbell_cqid_hi_MASK		0x001F
#define lpfc_eqcq_doorbell_cqid_hi_WORD		word0
#define lpfc_eqcq_doorbell_eqid_lo_SHIFT	0
#define lpfc_eqcq_doorbell_eqid_lo_MASK		0x01FF
#define lpfc_eqcq_doorbell_eqid_lo_WORD		word0
#define lpfc_eqcq_doorbell_eqid_hi_SHIFT	11
#define lpfc_eqcq_doorbell_eqid_hi_MASK		0x001F
#define lpfc_eqcq_doorbell_eqid_hi_WORD		word0
#define LPFC_CQID_HI_FIELD_SHIFT		10
#define LPFC_EQID_HI_FIELD_SHIFT		9

#define LPFC_BMBX			0x0160
#define lpfc_bmbx_addr_SHIFT		2
#define lpfc_bmbx_addr_MASK		0x3FFFFFFF
#define lpfc_bmbx_addr_WORD		word0
#define lpfc_bmbx_hi_SHIFT		1
#define lpfc_bmbx_hi_MASK		0x0001
#define lpfc_bmbx_hi_WORD		word0
#define lpfc_bmbx_rdy_SHIFT		0
#define lpfc_bmbx_rdy_MASK		0x0001
#define lpfc_bmbx_rdy_WORD		word0

#define LPFC_MQ_DOORBELL			0x0140
#define lpfc_mq_doorbell_num_posted_SHIFT	16
#define lpfc_mq_doorbell_num_posted_MASK	0x3FFF
#define lpfc_mq_doorbell_num_posted_WORD	word0
#define lpfc_mq_doorbell_id_SHIFT		0
#define lpfc_mq_doorbell_id_MASK		0xFFFF
#define lpfc_mq_doorbell_id_WORD		word0

struct lpfc_sli4_cfg_mhdr {
	uint32_t word1;
#define lpfc_mbox_hdr_emb_SHIFT		0
#define lpfc_mbox_hdr_emb_MASK		0x00000001
#define lpfc_mbox_hdr_emb_WORD		word1
#define lpfc_mbox_hdr_sge_cnt_SHIFT	3
#define lpfc_mbox_hdr_sge_cnt_MASK	0x0000001F
#define lpfc_mbox_hdr_sge_cnt_WORD	word1
	uint32_t payload_length;
	uint32_t tag_lo;
	uint32_t tag_hi;
	uint32_t reserved5;
};

union lpfc_sli4_cfg_shdr {
	struct {
		uint32_t word6;
#define lpfc_mbox_hdr_opcode_SHIFT	0
#define lpfc_mbox_hdr_opcode_MASK	0x000000FF
#define lpfc_mbox_hdr_opcode_WORD	word6
#define lpfc_mbox_hdr_subsystem_SHIFT	8
#define lpfc_mbox_hdr_subsystem_MASK	0x000000FF
#define lpfc_mbox_hdr_subsystem_WORD	word6
#define lpfc_mbox_hdr_port_number_SHIFT	16
#define lpfc_mbox_hdr_port_number_MASK	0x000000FF
#define lpfc_mbox_hdr_port_number_WORD	word6
#define lpfc_mbox_hdr_domain_SHIFT	24
#define lpfc_mbox_hdr_domain_MASK	0x000000FF
#define lpfc_mbox_hdr_domain_WORD	word6
		uint32_t timeout;
		uint32_t request_length;
		uint32_t word9;
#define lpfc_mbox_hdr_version_SHIFT	0
#define lpfc_mbox_hdr_version_MASK	0x000000FF
#define lpfc_mbox_hdr_version_WORD	word9
#define lpfc_mbox_hdr_pf_num_SHIFT	16
#define lpfc_mbox_hdr_pf_num_MASK	0x000000FF
#define lpfc_mbox_hdr_pf_num_WORD	word9
#define lpfc_mbox_hdr_vh_num_SHIFT	24
#define lpfc_mbox_hdr_vh_num_MASK	0x000000FF
#define lpfc_mbox_hdr_vh_num_WORD	word9
#define LPFC_Q_CREATE_VERSION_2	2
#define LPFC_Q_CREATE_VERSION_1	1
#define LPFC_Q_CREATE_VERSION_0	0
#define LPFC_OPCODE_VERSION_0	0
#define LPFC_OPCODE_VERSION_1	1
	} request;
	struct {
		uint32_t word6;
#define lpfc_mbox_hdr_opcode_SHIFT		0
#define lpfc_mbox_hdr_opcode_MASK		0x000000FF
#define lpfc_mbox_hdr_opcode_WORD		word6
#define lpfc_mbox_hdr_subsystem_SHIFT		8
#define lpfc_mbox_hdr_subsystem_MASK		0x000000FF
#define lpfc_mbox_hdr_subsystem_WORD		word6
#define lpfc_mbox_hdr_domain_SHIFT		24
#define lpfc_mbox_hdr_domain_MASK		0x000000FF
#define lpfc_mbox_hdr_domain_WORD		word6
		uint32_t word7;
#define lpfc_mbox_hdr_status_SHIFT		0
#define lpfc_mbox_hdr_status_MASK		0x000000FF
#define lpfc_mbox_hdr_status_WORD		word7
#define lpfc_mbox_hdr_add_status_SHIFT		8
#define lpfc_mbox_hdr_add_status_MASK		0x000000FF
#define lpfc_mbox_hdr_add_status_WORD		word7
		uint32_t response_length;
		uint32_t actual_response_length;
	} response;
};

/* Mailbox Header structures.
 * struct mbox_header is defined for first generation SLI4_CFG mailbox
 * calls deployed for BE-based ports.
 *
 * struct sli4_mbox_header is defined for second generation SLI4
 * ports that don't deploy the SLI4_CFG mechanism.
 */
struct mbox_header {
	struct lpfc_sli4_cfg_mhdr cfg_mhdr;
	union  lpfc_sli4_cfg_shdr cfg_shdr;
};

#define LPFC_EXTENT_LOCAL		0
#define LPFC_TIMEOUT_DEFAULT		0
#define LPFC_EXTENT_VERSION_DEFAULT	0

/* Subsystem Definitions */
#define LPFC_MBOX_SUBSYSTEM_NA		0x0
#define LPFC_MBOX_SUBSYSTEM_COMMON	0x1
#define LPFC_MBOX_SUBSYSTEM_FCOE	0xC

/* Device Specific Definitions */

/* The HOST ENDIAN defines are in Big Endian format. */
#define HOST_ENDIAN_LOW_WORD0   0xFF3412FF
#define HOST_ENDIAN_HIGH_WORD1	0xFF7856FF

/* Common Opcodes */
#define LPFC_MBOX_OPCODE_NA				0x00
#define LPFC_MBOX_OPCODE_CQ_CREATE			0x0C
#define LPFC_MBOX_OPCODE_EQ_CREATE			0x0D
#define LPFC_MBOX_OPCODE_MQ_CREATE			0x15
#define LPFC_MBOX_OPCODE_GET_CNTL_ATTRIBUTES		0x20
#define LPFC_MBOX_OPCODE_NOP				0x21
#define LPFC_MBOX_OPCODE_MODIFY_EQ_DELAY		0x29
#define LPFC_MBOX_OPCODE_MQ_DESTROY			0x35
#define LPFC_MBOX_OPCODE_CQ_DESTROY			0x36
#define LPFC_MBOX_OPCODE_EQ_DESTROY			0x37
#define LPFC_MBOX_OPCODE_QUERY_FW_CFG			0x3A
#define LPFC_MBOX_OPCODE_FUNCTION_RESET			0x3D
#define LPFC_MBOX_OPCODE_SET_PHYSICAL_LINK_CONFIG	0x3E
#define LPFC_MBOX_OPCODE_SET_BOOT_CONFIG		0x43
#define LPFC_MBOX_OPCODE_SET_BEACON_CONFIG              0x45
#define LPFC_MBOX_OPCODE_GET_BEACON_CONFIG              0x46
#define LPFC_MBOX_OPCODE_GET_PORT_NAME			0x4D
#define LPFC_MBOX_OPCODE_MQ_CREATE_EXT			0x5A
#define LPFC_MBOX_OPCODE_GET_VPD_DATA			0x5B
#define LPFC_MBOX_OPCODE_SET_HOST_DATA			0x5D
#define LPFC_MBOX_OPCODE_SEND_ACTIVATION		0x73
#define LPFC_MBOX_OPCODE_RESET_LICENSES			0x74
#define LPFC_MBOX_OPCODE_GET_RSRC_EXTENT_INFO		0x9A
#define LPFC_MBOX_OPCODE_GET_ALLOC_RSRC_EXTENT		0x9B
#define LPFC_MBOX_OPCODE_ALLOC_RSRC_EXTENT		0x9C
#define LPFC_MBOX_OPCODE_DEALLOC_RSRC_EXTENT		0x9D
#define LPFC_MBOX_OPCODE_GET_FUNCTION_CONFIG		0xA0
#define LPFC_MBOX_OPCODE_GET_PROFILE_CAPACITIES		0xA1
#define LPFC_MBOX_OPCODE_GET_PROFILE_CONFIG		0xA4
#define LPFC_MBOX_OPCODE_SET_PROFILE_CONFIG		0xA5
#define LPFC_MBOX_OPCODE_GET_PROFILE_LIST		0xA6
#define LPFC_MBOX_OPCODE_SET_ACT_PROFILE		0xA8
#define LPFC_MBOX_OPCODE_GET_FACTORY_PROFILE_CONFIG	0xA9
#define LPFC_MBOX_OPCODE_READ_OBJECT			0xAB
#define LPFC_MBOX_OPCODE_WRITE_OBJECT			0xAC
#define LPFC_MBOX_OPCODE_READ_OBJECT_LIST		0xAD
#define LPFC_MBOX_OPCODE_DELETE_OBJECT			0xAE
#define LPFC_MBOX_OPCODE_GET_SLI4_PARAMETERS		0xB5
#define LPFC_MBOX_OPCODE_SET_FEATURES                   0xBF

/* FCoE Opcodes */
#define LPFC_MBOX_OPCODE_FCOE_WQ_CREATE			0x01
#define LPFC_MBOX_OPCODE_FCOE_WQ_DESTROY		0x02
#define LPFC_MBOX_OPCODE_FCOE_POST_SGL_PAGES		0x03
#define LPFC_MBOX_OPCODE_FCOE_REMOVE_SGL_PAGES		0x04
#define LPFC_MBOX_OPCODE_FCOE_RQ_CREATE			0x05
#define LPFC_MBOX_OPCODE_FCOE_RQ_DESTROY		0x06
#define LPFC_MBOX_OPCODE_FCOE_READ_FCF_TABLE		0x08
#define LPFC_MBOX_OPCODE_FCOE_ADD_FCF			0x09
#define LPFC_MBOX_OPCODE_FCOE_DELETE_FCF		0x0A
#define LPFC_MBOX_OPCODE_FCOE_POST_HDR_TEMPLATE		0x0B
#define LPFC_MBOX_OPCODE_FCOE_REDISCOVER_FCF		0x10
#define LPFC_MBOX_OPCODE_FCOE_CQ_CREATE_SET		0x1D
#define LPFC_MBOX_OPCODE_FCOE_SET_FCLINK_SETTINGS	0x21
#define LPFC_MBOX_OPCODE_FCOE_LINK_DIAG_STATE		0x22
#define LPFC_MBOX_OPCODE_FCOE_LINK_DIAG_LOOPBACK	0x23

/* Mailbox command structures */
struct eq_context {
	uint32_t word0;
#define lpfc_eq_context_size_SHIFT	31
#define lpfc_eq_context_size_MASK	0x00000001
#define lpfc_eq_context_size_WORD	word0
#define LPFC_EQE_SIZE_4			0x0
#define LPFC_EQE_SIZE_16		0x1
#define lpfc_eq_context_valid_SHIFT	29
#define lpfc_eq_context_valid_MASK	0x00000001
#define lpfc_eq_context_valid_WORD	word0
	uint32_t word1;
#define lpfc_eq_context_count_SHIFT	26
#define lpfc_eq_context_count_MASK	0x00000003
#define lpfc_eq_context_count_WORD	word1
#define LPFC_EQ_CNT_256		0x0
#define LPFC_EQ_CNT_512		0x1
#define LPFC_EQ_CNT_1024	0x2
#define LPFC_EQ_CNT_2048	0x3
#define LPFC_EQ_CNT_4096	0x4
	uint32_t word2;
#define lpfc_eq_context_delay_multi_SHIFT	13
#define lpfc_eq_context_delay_multi_MASK	0x000003FF
#define lpfc_eq_context_delay_multi_WORD	word2
	uint32_t reserved3;
};

struct eq_delay_info {
	uint32_t eq_id;
	uint32_t phase;
	uint32_t delay_multi;
};
#define	LPFC_MAX_EQ_DELAY	8

struct sgl_page_pairs {
	uint32_t sgl_pg0_addr_lo;
	uint32_t sgl_pg0_addr_hi;
	uint32_t sgl_pg1_addr_lo;
	uint32_t sgl_pg1_addr_hi;
};

struct lpfc_mbx_post_sgl_pages {
	struct mbox_header header;
	uint32_t word0;
#define lpfc_post_sgl_pages_xri_SHIFT	0
#define lpfc_post_sgl_pages_xri_MASK	0x0000FFFF
#define lpfc_post_sgl_pages_xri_WORD	word0
#define lpfc_post_sgl_pages_xricnt_SHIFT	16
#define lpfc_post_sgl_pages_xricnt_MASK	0x0000FFFF
#define lpfc_post_sgl_pages_xricnt_WORD	word0
	struct sgl_page_pairs  sgl_pg_pairs[1];
};

/* word0 of page-1 struct shares the same SHIFT/MASK/WORD defines as above */
struct lpfc_mbx_post_uembed_sgl_page1 {
	union  lpfc_sli4_cfg_shdr cfg_shdr;
	uint32_t word0;
	struct sgl_page_pairs sgl_pg_pairs;
};

struct lpfc_mbx_sge {
	uint32_t pa_lo;
	uint32_t pa_hi;
	uint32_t length;
};

struct lpfc_mbx_nembed_cmd {
	struct lpfc_sli4_cfg_mhdr cfg_mhdr;
#define LPFC_SLI4_MBX_SGE_MAX_PAGES	19
	struct lpfc_mbx_sge sge[LPFC_SLI4_MBX_SGE_MAX_PAGES];
};

struct lpfc_mbx_nembed_sge_virt {
	void *addr[LPFC_SLI4_MBX_SGE_MAX_PAGES];
};

struct lpfc_mbx_eq_create {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_eq_create_num_pages_SHIFT	0
#define lpfc_mbx_eq_create_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_eq_create_num_pages_WORD	word0
			struct eq_context context;
			struct dma_address page[LPFC_MAX_EQ_PAGE];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_eq_create_q_id_SHIFT	0
#define lpfc_mbx_eq_create_q_id_MASK	0x0000FFFF
#define lpfc_mbx_eq_create_q_id_WORD	word0
		} response;
	} u;
};

struct lpfc_mbx_modify_eq_delay {
	struct mbox_header header;
	union {
		struct {
			uint32_t num_eq;
			struct eq_delay_info eq[LPFC_MAX_EQ_DELAY];
		} request;
		struct {
			uint32_t word0;
		} response;
	} u;
};

struct lpfc_mbx_eq_destroy {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_eq_destroy_q_id_SHIFT	0
#define lpfc_mbx_eq_destroy_q_id_MASK	0x0000FFFF
#define lpfc_mbx_eq_destroy_q_id_WORD	word0
		} request;
		struct {
			uint32_t word0;
		} response;
	} u;
};

struct lpfc_mbx_nop {
	struct mbox_header header;
	uint32_t context[2];
};

struct cq_context {
	uint32_t word0;
#define lpfc_cq_context_event_SHIFT	31
#define lpfc_cq_context_event_MASK	0x00000001
#define lpfc_cq_context_event_WORD	word0
#define lpfc_cq_context_valid_SHIFT	29
#define lpfc_cq_context_valid_MASK	0x00000001
#define lpfc_cq_context_valid_WORD	word0
#define lpfc_cq_context_count_SHIFT	27
#define lpfc_cq_context_count_MASK	0x00000003
#define lpfc_cq_context_count_WORD	word0
#define LPFC_CQ_CNT_256		0x0
#define LPFC_CQ_CNT_512		0x1
#define LPFC_CQ_CNT_1024	0x2
	uint32_t word1;
#define lpfc_cq_eq_id_SHIFT		22	/* Version 0 Only */
#define lpfc_cq_eq_id_MASK		0x000000FF
#define lpfc_cq_eq_id_WORD		word1
#define lpfc_cq_eq_id_2_SHIFT		0 	/* Version 2 Only */
#define lpfc_cq_eq_id_2_MASK		0x0000FFFF
#define lpfc_cq_eq_id_2_WORD		word1
	uint32_t reserved0;
	uint32_t reserved1;
};

struct lpfc_mbx_cq_create {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_cq_create_page_size_SHIFT	16	/* Version 2 Only */
#define lpfc_mbx_cq_create_page_size_MASK	0x000000FF
#define lpfc_mbx_cq_create_page_size_WORD	word0
#define lpfc_mbx_cq_create_num_pages_SHIFT	0
#define lpfc_mbx_cq_create_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_num_pages_WORD	word0
			struct cq_context context;
			struct dma_address page[LPFC_MAX_CQ_PAGE];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_cq_create_q_id_SHIFT	0
#define lpfc_mbx_cq_create_q_id_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_q_id_WORD	word0
		} response;
	} u;
};

struct lpfc_mbx_cq_create_set {
	union  lpfc_sli4_cfg_shdr cfg_shdr;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_cq_create_set_page_size_SHIFT	16	/* Version 2 Only */
#define lpfc_mbx_cq_create_set_page_size_MASK	0x000000FF
#define lpfc_mbx_cq_create_set_page_size_WORD	word0
#define lpfc_mbx_cq_create_set_num_pages_SHIFT	0
#define lpfc_mbx_cq_create_set_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_num_pages_WORD	word0
			uint32_t word1;
#define lpfc_mbx_cq_create_set_evt_SHIFT	31
#define lpfc_mbx_cq_create_set_evt_MASK		0x00000001
#define lpfc_mbx_cq_create_set_evt_WORD		word1
#define lpfc_mbx_cq_create_set_valid_SHIFT	29
#define lpfc_mbx_cq_create_set_valid_MASK	0x00000001
#define lpfc_mbx_cq_create_set_valid_WORD	word1
#define lpfc_mbx_cq_create_set_cqe_cnt_SHIFT	27
#define lpfc_mbx_cq_create_set_cqe_cnt_MASK	0x00000003
#define lpfc_mbx_cq_create_set_cqe_cnt_WORD	word1
#define lpfc_mbx_cq_create_set_cqe_size_SHIFT	25
#define lpfc_mbx_cq_create_set_cqe_size_MASK	0x00000003
#define lpfc_mbx_cq_create_set_cqe_size_WORD	word1
#define lpfc_mbx_cq_create_set_auto_SHIFT	15
#define lpfc_mbx_cq_create_set_auto_MASK	0x0000001
#define lpfc_mbx_cq_create_set_auto_WORD	word1
#define lpfc_mbx_cq_create_set_nodelay_SHIFT	14
#define lpfc_mbx_cq_create_set_nodelay_MASK	0x00000001
#define lpfc_mbx_cq_create_set_nodelay_WORD	word1
#define lpfc_mbx_cq_create_set_clswm_SHIFT	12
#define lpfc_mbx_cq_create_set_clswm_MASK	0x00000003
#define lpfc_mbx_cq_create_set_clswm_WORD	word1
			uint32_t word2;
#define lpfc_mbx_cq_create_set_arm_SHIFT	31
#define lpfc_mbx_cq_create_set_arm_MASK		0x00000001
#define lpfc_mbx_cq_create_set_arm_WORD		word2
#define lpfc_mbx_cq_create_set_num_cq_SHIFT	0
#define lpfc_mbx_cq_create_set_num_cq_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_num_cq_WORD	word2
			uint32_t word3;
#define lpfc_mbx_cq_create_set_eq_id1_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id1_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id1_WORD	word3
#define lpfc_mbx_cq_create_set_eq_id0_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id0_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id0_WORD	word3
			uint32_t word4;
#define lpfc_mbx_cq_create_set_eq_id3_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id3_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id3_WORD	word4
#define lpfc_mbx_cq_create_set_eq_id2_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id2_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id2_WORD	word4
			uint32_t word5;
#define lpfc_mbx_cq_create_set_eq_id5_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id5_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id5_WORD	word5
#define lpfc_mbx_cq_create_set_eq_id4_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id4_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id4_WORD	word5
			uint32_t word6;
#define lpfc_mbx_cq_create_set_eq_id7_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id7_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id7_WORD	word6
#define lpfc_mbx_cq_create_set_eq_id6_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id6_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id6_WORD	word6
			uint32_t word7;
#define lpfc_mbx_cq_create_set_eq_id9_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id9_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id9_WORD	word7
#define lpfc_mbx_cq_create_set_eq_id8_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id8_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id8_WORD	word7
			uint32_t word8;
#define lpfc_mbx_cq_create_set_eq_id11_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id11_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id11_WORD	word8
#define lpfc_mbx_cq_create_set_eq_id10_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id10_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id10_WORD	word8
			uint32_t word9;
#define lpfc_mbx_cq_create_set_eq_id13_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id13_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id13_WORD	word9
#define lpfc_mbx_cq_create_set_eq_id12_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id12_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id12_WORD	word9
			uint32_t word10;
#define lpfc_mbx_cq_create_set_eq_id15_SHIFT	16
#define lpfc_mbx_cq_create_set_eq_id15_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id15_WORD	word10
#define lpfc_mbx_cq_create_set_eq_id14_SHIFT	0
#define lpfc_mbx_cq_create_set_eq_id14_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_eq_id14_WORD	word10
			struct dma_address page[1];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_cq_create_set_num_alloc_SHIFT	16
#define lpfc_mbx_cq_create_set_num_alloc_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_num_alloc_WORD	word0
#define lpfc_mbx_cq_create_set_base_id_SHIFT	0
#define lpfc_mbx_cq_create_set_base_id_MASK	0x0000FFFF
#define lpfc_mbx_cq_create_set_base_id_WORD	word0
		} response;
	} u;
};

struct lpfc_mbx_cq_destroy {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_cq_destroy_q_id_SHIFT	0
#define lpfc_mbx_cq_destroy_q_id_MASK	0x0000FFFF
#define lpfc_mbx_cq_destroy_q_id_WORD	word0
		} request;
		struct {
			uint32_t word0;
		} response;
	} u;
};

struct wq_context {
	uint32_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t reserved3;
};

struct lpfc_mbx_wq_create {
	struct mbox_header header;
	union {
		struct {	/* Version 0 Request */
			uint32_t word0;
#define lpfc_mbx_wq_create_num_pages_SHIFT	0
#define lpfc_mbx_wq_create_num_pages_MASK	0x000000FF
#define lpfc_mbx_wq_create_num_pages_WORD	word0
#define lpfc_mbx_wq_create_dua_SHIFT		8
#define lpfc_mbx_wq_create_dua_MASK		0x00000001
#define lpfc_mbx_wq_create_dua_WORD		word0
#define lpfc_mbx_wq_create_cq_id_SHIFT		16
#define lpfc_mbx_wq_create_cq_id_MASK		0x0000FFFF
#define lpfc_mbx_wq_create_cq_id_WORD		word0
			struct dma_address page[LPFC_MAX_WQ_PAGE_V0];
			uint32_t word9;
#define lpfc_mbx_wq_create_bua_SHIFT		0
#define lpfc_mbx_wq_create_bua_MASK		0x00000001
#define lpfc_mbx_wq_create_bua_WORD		word9
#define lpfc_mbx_wq_create_ulp_num_SHIFT	8
#define lpfc_mbx_wq_create_ulp_num_MASK		0x000000FF
#define lpfc_mbx_wq_create_ulp_num_WORD		word9
		} request;
		struct {	/* Version 1 Request */
			uint32_t word0;	/* Word 0 is the same as in v0 */
			uint32_t word1;
#define lpfc_mbx_wq_create_page_size_SHIFT	0
#define lpfc_mbx_wq_create_page_size_MASK	0x000000FF
#define lpfc_mbx_wq_create_page_size_WORD	word1
#define LPFC_WQ_PAGE_SIZE_4096	0x1
#define lpfc_mbx_wq_create_wqe_size_SHIFT	8
#define lpfc_mbx_wq_create_wqe_size_MASK	0x0000000F
#define lpfc_mbx_wq_create_wqe_size_WORD	word1
#define LPFC_WQ_WQE_SIZE_64	0x5
#define LPFC_WQ_WQE_SIZE_128	0x6
#define lpfc_mbx_wq_create_wqe_count_SHIFT	16
#define lpfc_mbx_wq_create_wqe_count_MASK	0x0000FFFF
#define lpfc_mbx_wq_create_wqe_count_WORD	word1
			uint32_t word2;
			struct dma_address page[LPFC_MAX_WQ_PAGE-1];
		} request_1;
		struct {
			uint32_t word0;
#define lpfc_mbx_wq_create_q_id_SHIFT	0
#define lpfc_mbx_wq_create_q_id_MASK	0x0000FFFF
#define lpfc_mbx_wq_create_q_id_WORD	word0
			uint32_t doorbell_offset;
			uint32_t word2;
#define lpfc_mbx_wq_create_bar_set_SHIFT	0
#define lpfc_mbx_wq_create_bar_set_MASK		0x0000FFFF
#define lpfc_mbx_wq_create_bar_set_WORD		word2
#define WQ_PCI_BAR_0_AND_1	0x00
#define WQ_PCI_BAR_2_AND_3	0x01
#define WQ_PCI_BAR_4_AND_5	0x02
#define lpfc_mbx_wq_create_db_format_SHIFT	16
#define lpfc_mbx_wq_create_db_format_MASK	0x0000FFFF
#define lpfc_mbx_wq_create_db_format_WORD	word2
		} response;
	} u;
};

struct lpfc_mbx_wq_destroy {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_wq_destroy_q_id_SHIFT	0
#define lpfc_mbx_wq_destroy_q_id_MASK	0x0000FFFF
#define lpfc_mbx_wq_destroy_q_id_WORD	word0
		} request;
		struct {
			uint32_t word0;
		} response;
	} u;
};

#define LPFC_HDR_BUF_SIZE 128
#define LPFC_DATA_BUF_SIZE 2048
struct rq_context {
	uint32_t word0;
#define lpfc_rq_context_rqe_count_SHIFT	16	/* Version 0 Only */
#define lpfc_rq_context_rqe_count_MASK	0x0000000F
#define lpfc_rq_context_rqe_count_WORD	word0
#define LPFC_RQ_RING_SIZE_512		9	/* 512 entries */
#define LPFC_RQ_RING_SIZE_1024		10	/* 1024 entries */
#define LPFC_RQ_RING_SIZE_2048		11	/* 2048 entries */
#define LPFC_RQ_RING_SIZE_4096		12	/* 4096 entries */
#define lpfc_rq_context_rqe_count_1_SHIFT	16	/* Version 1-2 Only */
#define lpfc_rq_context_rqe_count_1_MASK	0x0000FFFF
#define lpfc_rq_context_rqe_count_1_WORD	word0
#define lpfc_rq_context_rqe_size_SHIFT	8		/* Version 1-2 Only */
#define lpfc_rq_context_rqe_size_MASK	0x0000000F
#define lpfc_rq_context_rqe_size_WORD	word0
#define LPFC_RQE_SIZE_8		2
#define LPFC_RQE_SIZE_16	3
#define LPFC_RQE_SIZE_32	4
#define LPFC_RQE_SIZE_64	5
#define LPFC_RQE_SIZE_128	6
#define lpfc_rq_context_page_size_SHIFT	0		/* Version 1 Only */
#define lpfc_rq_context_page_size_MASK	0x000000FF
#define lpfc_rq_context_page_size_WORD	word0
#define	LPFC_RQ_PAGE_SIZE_4096	0x1
	uint32_t word1;
#define lpfc_rq_context_data_size_SHIFT	16		/* Version 2 Only */
#define lpfc_rq_context_data_size_MASK	0x0000FFFF
#define lpfc_rq_context_data_size_WORD	word1
#define lpfc_rq_context_hdr_size_SHIFT	0		/* Version 2 Only */
#define lpfc_rq_context_hdr_size_MASK	0x0000FFFF
#define lpfc_rq_context_hdr_size_WORD	word1
	uint32_t word2;
#define lpfc_rq_context_cq_id_SHIFT	16
#define lpfc_rq_context_cq_id_MASK	0x000003FF
#define lpfc_rq_context_cq_id_WORD	word2
#define lpfc_rq_context_buf_size_SHIFT	0
#define lpfc_rq_context_buf_size_MASK	0x0000FFFF
#define lpfc_rq_context_buf_size_WORD	word2
#define lpfc_rq_context_base_cq_SHIFT	0		/* Version 2 Only */
#define lpfc_rq_context_base_cq_MASK	0x0000FFFF
#define lpfc_rq_context_base_cq_WORD	word2
	uint32_t buffer_size;				/* Version 1 Only */
};

struct lpfc_mbx_rq_create {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_rq_create_num_pages_SHIFT	0
#define lpfc_mbx_rq_create_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_num_pages_WORD	word0
#define lpfc_mbx_rq_create_dua_SHIFT		16
#define lpfc_mbx_rq_create_dua_MASK		0x00000001
#define lpfc_mbx_rq_create_dua_WORD		word0
#define lpfc_mbx_rq_create_bqu_SHIFT		17
#define lpfc_mbx_rq_create_bqu_MASK		0x00000001
#define lpfc_mbx_rq_create_bqu_WORD		word0
#define lpfc_mbx_rq_create_ulp_num_SHIFT	24
#define lpfc_mbx_rq_create_ulp_num_MASK		0x000000FF
#define lpfc_mbx_rq_create_ulp_num_WORD		word0
			struct rq_context context;
			struct dma_address page[LPFC_MAX_RQ_PAGE];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_rq_create_q_cnt_v2_SHIFT	16
#define lpfc_mbx_rq_create_q_cnt_v2_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_q_cnt_v2_WORD	word0
#define lpfc_mbx_rq_create_q_id_SHIFT		0
#define lpfc_mbx_rq_create_q_id_MASK		0x0000FFFF
#define lpfc_mbx_rq_create_q_id_WORD		word0
			uint32_t doorbell_offset;
			uint32_t word2;
#define lpfc_mbx_rq_create_bar_set_SHIFT	0
#define lpfc_mbx_rq_create_bar_set_MASK		0x0000FFFF
#define lpfc_mbx_rq_create_bar_set_WORD		word2
#define lpfc_mbx_rq_create_db_format_SHIFT	16
#define lpfc_mbx_rq_create_db_format_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_db_format_WORD	word2
		} response;
	} u;
};

struct lpfc_mbx_rq_create_v2 {
	union  lpfc_sli4_cfg_shdr cfg_shdr;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_rq_create_num_pages_SHIFT	0
#define lpfc_mbx_rq_create_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_num_pages_WORD	word0
#define lpfc_mbx_rq_create_rq_cnt_SHIFT		16
#define lpfc_mbx_rq_create_rq_cnt_MASK		0x000000FF
#define lpfc_mbx_rq_create_rq_cnt_WORD		word0
#define lpfc_mbx_rq_create_dua_SHIFT		16
#define lpfc_mbx_rq_create_dua_MASK		0x00000001
#define lpfc_mbx_rq_create_dua_WORD		word0
#define lpfc_mbx_rq_create_bqu_SHIFT		17
#define lpfc_mbx_rq_create_bqu_MASK		0x00000001
#define lpfc_mbx_rq_create_bqu_WORD		word0
#define lpfc_mbx_rq_create_ulp_num_SHIFT	24
#define lpfc_mbx_rq_create_ulp_num_MASK		0x000000FF
#define lpfc_mbx_rq_create_ulp_num_WORD		word0
#define lpfc_mbx_rq_create_dim_SHIFT		29
#define lpfc_mbx_rq_create_dim_MASK		0x00000001
#define lpfc_mbx_rq_create_dim_WORD		word0
#define lpfc_mbx_rq_create_dfd_SHIFT		30
#define lpfc_mbx_rq_create_dfd_MASK		0x00000001
#define lpfc_mbx_rq_create_dfd_WORD		word0
#define lpfc_mbx_rq_create_dnb_SHIFT		31
#define lpfc_mbx_rq_create_dnb_MASK		0x00000001
#define lpfc_mbx_rq_create_dnb_WORD		word0
			struct rq_context context;
			struct dma_address page[1];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_rq_create_q_cnt_v2_SHIFT	16
#define lpfc_mbx_rq_create_q_cnt_v2_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_q_cnt_v2_WORD	word0
#define lpfc_mbx_rq_create_q_id_SHIFT		0
#define lpfc_mbx_rq_create_q_id_MASK		0x0000FFFF
#define lpfc_mbx_rq_create_q_id_WORD		word0
			uint32_t doorbell_offset;
			uint32_t word2;
#define lpfc_mbx_rq_create_bar_set_SHIFT	0
#define lpfc_mbx_rq_create_bar_set_MASK		0x0000FFFF
#define lpfc_mbx_rq_create_bar_set_WORD		word2
#define lpfc_mbx_rq_create_db_format_SHIFT	16
#define lpfc_mbx_rq_create_db_format_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_db_format_WORD	word2
		} response;
	} u;
};

struct lpfc_mbx_rq_destroy {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_rq_destroy_q_id_SHIFT	0
#define lpfc_mbx_rq_destroy_q_id_MASK	0x0000FFFF
#define lpfc_mbx_rq_destroy_q_id_WORD	word0
		} request;
		struct {
			uint32_t word0;
		} response;
	} u;
};

struct mq_context {
	uint32_t word0;
#define lpfc_mq_context_cq_id_SHIFT	22 	/* Version 0 Only */
#define lpfc_mq_context_cq_id_MASK	0x000003FF
#define lpfc_mq_context_cq_id_WORD	word0
#define lpfc_mq_context_ring_size_SHIFT	16
#define lpfc_mq_context_ring_size_MASK	0x0000000F
#define lpfc_mq_context_ring_size_WORD	word0
#define LPFC_MQ_RING_SIZE_16		0x5
#define LPFC_MQ_RING_SIZE_32		0x6
#define LPFC_MQ_RING_SIZE_64		0x7
#define LPFC_MQ_RING_SIZE_128		0x8
	uint32_t word1;
#define lpfc_mq_context_valid_SHIFT	31
#define lpfc_mq_context_valid_MASK	0x00000001
#define lpfc_mq_context_valid_WORD	word1
	uint32_t reserved2;
	uint32_t reserved3;
};

struct lpfc_mbx_mq_create {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_mq_create_num_pages_SHIFT	0
#define lpfc_mbx_mq_create_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_mq_create_num_pages_WORD	word0
			struct mq_context context;
			struct dma_address page[LPFC_MAX_MQ_PAGE];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_mq_create_q_id_SHIFT	0
#define lpfc_mbx_mq_create_q_id_MASK	0x0000FFFF
#define lpfc_mbx_mq_create_q_id_WORD	word0
		} response;
	} u;
};

struct lpfc_mbx_mq_create_ext {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_mq_create_ext_num_pages_SHIFT	0
#define lpfc_mbx_mq_create_ext_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_mq_create_ext_num_pages_WORD	word0
#define lpfc_mbx_mq_create_ext_cq_id_SHIFT	16	/* Version 1 Only */
#define lpfc_mbx_mq_create_ext_cq_id_MASK	0x0000FFFF
#define lpfc_mbx_mq_create_ext_cq_id_WORD	word0
			uint32_t async_evt_bmap;
#define lpfc_mbx_mq_create_ext_async_evt_link_SHIFT	LPFC_TRAILER_CODE_LINK
#define lpfc_mbx_mq_create_ext_async_evt_link_MASK	0x00000001
#define lpfc_mbx_mq_create_ext_async_evt_link_WORD	async_evt_bmap
#define LPFC_EVT_CODE_LINK_NO_LINK	0x0
#define LPFC_EVT_CODE_LINK_10_MBIT	0x1
#define LPFC_EVT_CODE_LINK_100_MBIT	0x2
#define LPFC_EVT_CODE_LINK_1_GBIT	0x3
#define LPFC_EVT_CODE_LINK_10_GBIT	0x4
#define lpfc_mbx_mq_create_ext_async_evt_fip_SHIFT	LPFC_TRAILER_CODE_FCOE
#define lpfc_mbx_mq_create_ext_async_evt_fip_MASK	0x00000001
#define lpfc_mbx_mq_create_ext_async_evt_fip_WORD	async_evt_bmap
#define lpfc_mbx_mq_create_ext_async_evt_group5_SHIFT	LPFC_TRAILER_CODE_GRP5
#define lpfc_mbx_mq_create_ext_async_evt_group5_MASK	0x00000001
#define lpfc_mbx_mq_create_ext_async_evt_group5_WORD	async_evt_bmap
#define lpfc_mbx_mq_create_ext_async_evt_fc_SHIFT	LPFC_TRAILER_CODE_FC
#define lpfc_mbx_mq_create_ext_async_evt_fc_MASK	0x00000001
#define lpfc_mbx_mq_create_ext_async_evt_fc_WORD	async_evt_bmap
#define LPFC_EVT_CODE_FC_NO_LINK	0x0
#define LPFC_EVT_CODE_FC_1_GBAUD	0x1
#define LPFC_EVT_CODE_FC_2_GBAUD	0x2
#define LPFC_EVT_CODE_FC_4_GBAUD	0x4
#define LPFC_EVT_CODE_FC_8_GBAUD	0x8
#define LPFC_EVT_CODE_FC_10_GBAUD	0xA
#define LPFC_EVT_CODE_FC_16_GBAUD	0x10
#define lpfc_mbx_mq_create_ext_async_evt_sli_SHIFT	LPFC_TRAILER_CODE_SLI
#define lpfc_mbx_mq_create_ext_async_evt_sli_MASK	0x00000001
#define lpfc_mbx_mq_create_ext_async_evt_sli_WORD	async_evt_bmap
			struct mq_context context;
			struct dma_address page[LPFC_MAX_MQ_PAGE];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_mq_create_q_id_SHIFT	0
#define lpfc_mbx_mq_create_q_id_MASK	0x0000FFFF
#define lpfc_mbx_mq_create_q_id_WORD	word0
		} response;
	} u;
#define LPFC_ASYNC_EVENT_LINK_STATE	0x2
#define LPFC_ASYNC_EVENT_FCF_STATE	0x4
#define LPFC_ASYNC_EVENT_GROUP5		0x20
};

struct lpfc_mbx_mq_destroy {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_mq_destroy_q_id_SHIFT	0
#define lpfc_mbx_mq_destroy_q_id_MASK	0x0000FFFF
#define lpfc_mbx_mq_destroy_q_id_WORD	word0
		} request;
		struct {
			uint32_t word0;
		} response;
	} u;
};

/* Start Gen 2 SLI4 Mailbox definitions: */

/* Define allocate-ready Gen 2 SLI4 FCoE Resource Extent Types. */
#define LPFC_RSC_TYPE_FCOE_VFI	0x20
#define LPFC_RSC_TYPE_FCOE_VPI	0x21
#define LPFC_RSC_TYPE_FCOE_RPI	0x22
#define LPFC_RSC_TYPE_FCOE_XRI	0x23

struct lpfc_mbx_get_rsrc_extent_info {
	struct mbox_header header;
	union {
		struct {
			uint32_t word4;
#define lpfc_mbx_get_rsrc_extent_info_type_SHIFT	0
#define lpfc_mbx_get_rsrc_extent_info_type_MASK		0x0000FFFF
#define lpfc_mbx_get_rsrc_extent_info_type_WORD		word4
		} req;
		struct {
			uint32_t word4;
#define lpfc_mbx_get_rsrc_extent_info_cnt_SHIFT		0
#define lpfc_mbx_get_rsrc_extent_info_cnt_MASK		0x0000FFFF
#define lpfc_mbx_get_rsrc_extent_info_cnt_WORD		word4
#define lpfc_mbx_get_rsrc_extent_info_size_SHIFT	16
#define lpfc_mbx_get_rsrc_extent_info_size_MASK		0x0000FFFF
#define lpfc_mbx_get_rsrc_extent_info_size_WORD		word4
		} rsp;
	} u;
};

struct lpfc_mbx_query_fw_config {
	struct mbox_header header;
	struct {
		uint32_t config_number;
#define	LPFC_FC_FCOE		0x00000007
		uint32_t asic_revision;
		uint32_t physical_port;
		uint32_t function_mode;
#define LPFC_FCOE_INI_MODE	0x00000040
#define LPFC_FCOE_TGT_MODE	0x00000080
#define LPFC_DUA_MODE		0x00000800
		uint32_t ulp0_mode;
#define LPFC_ULP_FCOE_INIT_MODE	0x00000040
#define LPFC_ULP_FCOE_TGT_MODE	0x00000080
		uint32_t ulp0_nap_words[12];
		uint32_t ulp1_mode;
		uint32_t ulp1_nap_words[12];
		uint32_t function_capabilities;
		uint32_t cqid_base;
		uint32_t cqid_tot;
		uint32_t eqid_base;
		uint32_t eqid_tot;
		uint32_t ulp0_nap2_words[2];
		uint32_t ulp1_nap2_words[2];
	} rsp;
};

struct lpfc_mbx_set_beacon_config {
	struct mbox_header header;
	uint32_t word4;
#define lpfc_mbx_set_beacon_port_num_SHIFT		0
#define lpfc_mbx_set_beacon_port_num_MASK		0x0000003F
#define lpfc_mbx_set_beacon_port_num_WORD		word4
#define lpfc_mbx_set_beacon_port_type_SHIFT		6
#define lpfc_mbx_set_beacon_port_type_MASK		0x00000003
#define lpfc_mbx_set_beacon_port_type_WORD		word4
#define lpfc_mbx_set_beacon_state_SHIFT			8
#define lpfc_mbx_set_beacon_state_MASK			0x000000FF
#define lpfc_mbx_set_beacon_state_WORD			word4
#define lpfc_mbx_set_beacon_duration_SHIFT		16
#define lpfc_mbx_set_beacon_duration_MASK		0x000000FF
#define lpfc_mbx_set_beacon_duration_WORD		word4
#define lpfc_mbx_set_beacon_status_duration_SHIFT	24
#define lpfc_mbx_set_beacon_status_duration_MASK	0x000000FF
#define lpfc_mbx_set_beacon_status_duration_WORD	word4
};

struct lpfc_id_range {
	uint32_t word5;
#define lpfc_mbx_rsrc_id_word4_0_SHIFT	0
#define lpfc_mbx_rsrc_id_word4_0_MASK	0x0000FFFF
#define lpfc_mbx_rsrc_id_word4_0_WORD	word5
#define lpfc_mbx_rsrc_id_word4_1_SHIFT	16
#define lpfc_mbx_rsrc_id_word4_1_MASK	0x0000FFFF
#define lpfc_mbx_rsrc_id_word4_1_WORD	word5
};

struct lpfc_mbx_set_link_diag_state {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_set_diag_state_diag_SHIFT	0
#define lpfc_mbx_set_diag_state_diag_MASK	0x00000001
#define lpfc_mbx_set_diag_state_diag_WORD	word0
#define lpfc_mbx_set_diag_state_diag_bit_valid_SHIFT	2
#define lpfc_mbx_set_diag_state_diag_bit_valid_MASK	0x00000001
#define lpfc_mbx_set_diag_state_diag_bit_valid_WORD	word0
#define LPFC_DIAG_STATE_DIAG_BIT_VALID_NO_CHANGE	0
#define LPFC_DIAG_STATE_DIAG_BIT_VALID_CHANGE		1
#define lpfc_mbx_set_diag_state_link_num_SHIFT	16
#define lpfc_mbx_set_diag_state_link_num_MASK	0x0000003F
#define lpfc_mbx_set_diag_state_link_num_WORD	word0
#define lpfc_mbx_set_diag_state_link_type_SHIFT 22
#define lpfc_mbx_set_diag_state_link_type_MASK	0x00000003
#define lpfc_mbx_set_diag_state_link_type_WORD	word0
		} req;
		struct {
			uint32_t word0;
		} rsp;
	} u;
};

struct lpfc_mbx_set_link_diag_loopback {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_set_diag_lpbk_type_SHIFT	0
#define lpfc_mbx_set_diag_lpbk_type_MASK	0x00000003
#define lpfc_mbx_set_diag_lpbk_type_WORD	word0
#define LPFC_DIAG_LOOPBACK_TYPE_DISABLE		0x0
#define LPFC_DIAG_LOOPBACK_TYPE_INTERNAL	0x1
#define LPFC_DIAG_LOOPBACK_TYPE_SERDES		0x2
#define lpfc_mbx_set_diag_lpbk_link_num_SHIFT	16
#define lpfc_mbx_set_diag_lpbk_link_num_MASK	0x0000003F
#define lpfc_mbx_set_diag_lpbk_link_num_WORD	word0
#define lpfc_mbx_set_diag_lpbk_link_type_SHIFT	22
#define lpfc_mbx_set_diag_lpbk_link_type_MASK	0x00000003
#define lpfc_mbx_set_diag_lpbk_link_type_WORD	word0
		} req;
		struct {
			uint32_t word0;
		} rsp;
	} u;
};

struct lpfc_mbx_run_link_diag_test {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_run_diag_test_link_num_SHIFT	16
#define lpfc_mbx_run_diag_test_link_num_MASK	0x0000003F
#define lpfc_mbx_run_diag_test_link_num_WORD	word0
#define lpfc_mbx_run_diag_test_link_type_SHIFT	22
#define lpfc_mbx_run_diag_test_link_type_MASK	0x00000003
#define lpfc_mbx_run_diag_test_link_type_WORD	word0
			uint32_t word1;
#define lpfc_mbx_run_diag_test_test_id_SHIFT	0
#define lpfc_mbx_run_diag_test_test_id_MASK	0x0000FFFF
#define lpfc_mbx_run_diag_test_test_id_WORD	word1
#define lpfc_mbx_run_diag_test_loops_SHIFT	16
#define lpfc_mbx_run_diag_test_loops_MASK	0x0000FFFF
#define lpfc_mbx_run_diag_test_loops_WORD	word1
			uint32_t word2;
#define lpfc_mbx_run_diag_test_test_ver_SHIFT	0
#define lpfc_mbx_run_diag_test_test_ver_MASK	0x0000FFFF
#define lpfc_mbx_run_diag_test_test_ver_WORD	word2
#define lpfc_mbx_run_diag_test_err_act_SHIFT	16
#define lpfc_mbx_run_diag_test_err_act_MASK	0x000000FF
#define lpfc_mbx_run_diag_test_err_act_WORD	word2
		} req;
		struct {
			uint32_t word0;
		} rsp;
	} u;
};

/*
 * struct lpfc_mbx_alloc_rsrc_extents:
 * A mbox is generically 256 bytes long. An SLI4_CONFIG mailbox requires
 * 6 words of header + 4 words of shared subcommand header +
 * 1 words of Extent-Opcode-specific header = 11 words or 44 bytes total.
 *
 * An embedded version of SLI4_CONFIG therefore has 256 - 44 = 212 bytes
 * for extents payload.
 *
 * 212/2 (bytes per extent) = 106 extents.
 * 106/2 (extents per word) = 53 words.
 * lpfc_id_range id is statically size to 53.
 *
 * This mailbox definition is used for ALLOC or GET_ALLOCATED
 * extent ranges.  For ALLOC, the type and cnt are required.
 * For GET_ALLOCATED, only the type is required.
 */
struct lpfc_mbx_alloc_rsrc_extents {
	struct mbox_header header;
	union {
		struct {
			uint32_t word4;
#define lpfc_mbx_alloc_rsrc_extents_type_SHIFT	0
#define lpfc_mbx_alloc_rsrc_extents_type_MASK	0x0000FFFF
#define lpfc_mbx_alloc_rsrc_extents_type_WORD	word4
#define lpfc_mbx_alloc_rsrc_extents_cnt_SHIFT	16
#define lpfc_mbx_alloc_rsrc_extents_cnt_MASK	0x0000FFFF
#define lpfc_mbx_alloc_rsrc_extents_cnt_WORD	word4
		} req;
		struct {
			uint32_t word4;
#define lpfc_mbx_rsrc_cnt_SHIFT	0
#define lpfc_mbx_rsrc_cnt_MASK	0x0000FFFF
#define lpfc_mbx_rsrc_cnt_WORD	word4
			struct lpfc_id_range id[53];
		} rsp;
	} u;
};

/*
 * This is the non-embedded version of ALLOC or GET RSRC_EXTENTS. Word4 in this
 * structure shares the same SHIFT/MASK/WORD defines provided in the
 * mbx_alloc_rsrc_extents and mbx_get_alloc_rsrc_extents, word4, provided in
 * the structures defined above.  This non-embedded structure provides for the
 * maximum number of extents supported by the port.
 */
struct lpfc_mbx_nembed_rsrc_extent {
	union  lpfc_sli4_cfg_shdr cfg_shdr;
	uint32_t word4;
	struct lpfc_id_range id;
};

struct lpfc_mbx_dealloc_rsrc_extents {
	struct mbox_header header;
	struct {
		uint32_t word4;
#define lpfc_mbx_dealloc_rsrc_extents_type_SHIFT	0
#define lpfc_mbx_dealloc_rsrc_extents_type_MASK		0x0000FFFF
#define lpfc_mbx_dealloc_rsrc_extents_type_WORD		word4
	} req;

};

/* Start SLI4 FCoE specific mbox structures. */

struct lpfc_mbx_post_hdr_tmpl {
	struct mbox_header header;
	uint32_t word10;
#define lpfc_mbx_post_hdr_tmpl_rpi_offset_SHIFT  0
#define lpfc_mbx_post_hdr_tmpl_rpi_offset_MASK   0x0000FFFF
#define lpfc_mbx_post_hdr_tmpl_rpi_offset_WORD   word10
#define lpfc_mbx_post_hdr_tmpl_page_cnt_SHIFT   16
#define lpfc_mbx_post_hdr_tmpl_page_cnt_MASK    0x0000FFFF
#define lpfc_mbx_post_hdr_tmpl_page_cnt_WORD    word10
	uint32_t rpi_paddr_lo;
	uint32_t rpi_paddr_hi;
};

struct sli4_sge {	/* SLI-4 */
	uint32_t addr_hi;
	uint32_t addr_lo;

	uint32_t word2;
#define lpfc_sli4_sge_offset_SHIFT	0
#define lpfc_sli4_sge_offset_MASK	0x07FFFFFF
#define lpfc_sli4_sge_offset_WORD	word2
#define lpfc_sli4_sge_type_SHIFT	27
#define lpfc_sli4_sge_type_MASK		0x0000000F
#define lpfc_sli4_sge_type_WORD		word2
#define LPFC_SGE_TYPE_DATA		0x0
#define LPFC_SGE_TYPE_DIF		0x4
#define LPFC_SGE_TYPE_LSP		0x5
#define LPFC_SGE_TYPE_PEDIF		0x6
#define LPFC_SGE_TYPE_PESEED		0x7
#define LPFC_SGE_TYPE_DISEED		0x8
#define LPFC_SGE_TYPE_ENC		0x9
#define LPFC_SGE_TYPE_ATM		0xA
#define LPFC_SGE_TYPE_SKIP		0xC
#define lpfc_sli4_sge_last_SHIFT	31 /* Last SEG in the SGL sets it */
#define lpfc_sli4_sge_last_MASK		0x00000001
#define lpfc_sli4_sge_last_WORD		word2
	uint32_t sge_len;
};

struct sli4_sge_diseed {	/* SLI-4 */
	uint32_t ref_tag;
	uint32_t ref_tag_tran;

	uint32_t word2;
#define lpfc_sli4_sge_dif_apptran_SHIFT	0
#define lpfc_sli4_sge_dif_apptran_MASK	0x0000FFFF
#define lpfc_sli4_sge_dif_apptran_WORD	word2
#define lpfc_sli4_sge_dif_af_SHIFT	24
#define lpfc_sli4_sge_dif_af_MASK	0x00000001
#define lpfc_sli4_sge_dif_af_WORD	word2
#define lpfc_sli4_sge_dif_na_SHIFT	25
#define lpfc_sli4_sge_dif_na_MASK	0x00000001
#define lpfc_sli4_sge_dif_na_WORD	word2
#define lpfc_sli4_sge_dif_hi_SHIFT	26
#define lpfc_sli4_sge_dif_hi_MASK	0x00000001
#define lpfc_sli4_sge_dif_hi_WORD	word2
#define lpfc_sli4_sge_dif_type_SHIFT	27
#define lpfc_sli4_sge_dif_type_MASK	0x0000000F
#define lpfc_sli4_sge_dif_type_WORD	word2
#define lpfc_sli4_sge_dif_last_SHIFT	31 /* Last SEG in the SGL sets it */
#define lpfc_sli4_sge_dif_last_MASK	0x00000001
#define lpfc_sli4_sge_dif_last_WORD	word2
	uint32_t word3;
#define lpfc_sli4_sge_dif_apptag_SHIFT	0
#define lpfc_sli4_sge_dif_apptag_MASK	0x0000FFFF
#define lpfc_sli4_sge_dif_apptag_WORD	word3
#define lpfc_sli4_sge_dif_bs_SHIFT	16
#define lpfc_sli4_sge_dif_bs_MASK	0x00000007
#define lpfc_sli4_sge_dif_bs_WORD	word3
#define lpfc_sli4_sge_dif_ai_SHIFT	19
#define lpfc_sli4_sge_dif_ai_MASK	0x00000001
#define lpfc_sli4_sge_dif_ai_WORD	word3
#define lpfc_sli4_sge_dif_me_SHIFT	20
#define lpfc_sli4_sge_dif_me_MASK	0x00000001
#define lpfc_sli4_sge_dif_me_WORD	word3
#define lpfc_sli4_sge_dif_re_SHIFT	21
#define lpfc_sli4_sge_dif_re_MASK	0x00000001
#define lpfc_sli4_sge_dif_re_WORD	word3
#define lpfc_sli4_sge_dif_ce_SHIFT	22
#define lpfc_sli4_sge_dif_ce_MASK	0x00000001
#define lpfc_sli4_sge_dif_ce_WORD	word3
#define lpfc_sli4_sge_dif_nr_SHIFT	23
#define lpfc_sli4_sge_dif_nr_MASK	0x00000001
#define lpfc_sli4_sge_dif_nr_WORD	word3
#define lpfc_sli4_sge_dif_oprx_SHIFT	24
#define lpfc_sli4_sge_dif_oprx_MASK	0x0000000F
#define lpfc_sli4_sge_dif_oprx_WORD	word3
#define lpfc_sli4_sge_dif_optx_SHIFT	28
#define lpfc_sli4_sge_dif_optx_MASK	0x0000000F
#define lpfc_sli4_sge_dif_optx_WORD	word3
/* optx and oprx use BG_OP_IN defines in lpfc_hw.h */
};

struct fcf_record {
	uint32_t max_rcv_size;
	uint32_t fka_adv_period;
	uint32_t fip_priority;
	uint32_t word3;
#define lpfc_fcf_record_mac_0_SHIFT		0
#define lpfc_fcf_record_mac_0_MASK		0x000000FF
#define lpfc_fcf_record_mac_0_WORD		word3
#define lpfc_fcf_record_mac_1_SHIFT		8
#define lpfc_fcf_record_mac_1_MASK		0x000000FF
#define lpfc_fcf_record_mac_1_WORD		word3
#define lpfc_fcf_record_mac_2_SHIFT		16
#define lpfc_fcf_record_mac_2_MASK		0x000000FF
#define lpfc_fcf_record_mac_2_WORD		word3
#define lpfc_fcf_record_mac_3_SHIFT		24
#define lpfc_fcf_record_mac_3_MASK		0x000000FF
#define lpfc_fcf_record_mac_3_WORD		word3
	uint32_t word4;
#define lpfc_fcf_record_mac_4_SHIFT		0
#define lpfc_fcf_record_mac_4_MASK		0x000000FF
#define lpfc_fcf_record_mac_4_WORD		word4
#define lpfc_fcf_record_mac_5_SHIFT		8
#define lpfc_fcf_record_mac_5_MASK		0x000000FF
#define lpfc_fcf_record_mac_5_WORD		word4
#define lpfc_fcf_record_fcf_avail_SHIFT		16
#define lpfc_fcf_record_fcf_avail_MASK		0x000000FF
#define lpfc_fcf_record_fcf_avail_WORD		word4
#define lpfc_fcf_record_mac_addr_prov_SHIFT	24
#define lpfc_fcf_record_mac_addr_prov_MASK	0x000000FF
#define lpfc_fcf_record_mac_addr_prov_WORD	word4
#define LPFC_FCF_FPMA           1 	/* Fabric Provided MAC Address */
#define LPFC_FCF_SPMA           2       /* Server Provided MAC Address */
	uint32_t word5;
#define lpfc_fcf_record_fab_name_0_SHIFT	0
#define lpfc_fcf_record_fab_name_0_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_0_WORD		word5
#define lpfc_fcf_record_fab_name_1_SHIFT	8
#define lpfc_fcf_record_fab_name_1_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_1_WORD		word5
#define lpfc_fcf_record_fab_name_2_SHIFT	16
#define lpfc_fcf_record_fab_name_2_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_2_WORD		word5
#define lpfc_fcf_record_fab_name_3_SHIFT	24
#define lpfc_fcf_record_fab_name_3_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_3_WORD		word5
	uint32_t word6;
#define lpfc_fcf_record_fab_name_4_SHIFT	0
#define lpfc_fcf_record_fab_name_4_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_4_WORD		word6
#define lpfc_fcf_record_fab_name_5_SHIFT	8
#define lpfc_fcf_record_fab_name_5_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_5_WORD		word6
#define lpfc_fcf_record_fab_name_6_SHIFT	16
#define lpfc_fcf_record_fab_name_6_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_6_WORD		word6
#define lpfc_fcf_record_fab_name_7_SHIFT	24
#define lpfc_fcf_record_fab_name_7_MASK		0x000000FF
#define lpfc_fcf_record_fab_name_7_WORD		word6
	uint32_t word7;
#define lpfc_fcf_record_fc_map_0_SHIFT		0
#define lpfc_fcf_record_fc_map_0_MASK		0x000000FF
#define lpfc_fcf_record_fc_map_0_WORD		word7
#define lpfc_fcf_record_fc_map_1_SHIFT		8
#define lpfc_fcf_record_fc_map_1_MASK		0x000000FF
#define lpfc_fcf_record_fc_map_1_WORD		word7
#define lpfc_fcf_record_fc_map_2_SHIFT		16
#define lpfc_fcf_record_fc_map_2_MASK		0x000000FF
#define lpfc_fcf_record_fc_map_2_WORD		word7
#define lpfc_fcf_record_fcf_valid_SHIFT		24
#define lpfc_fcf_record_fcf_valid_MASK		0x00000001
#define lpfc_fcf_record_fcf_valid_WORD		word7
#define lpfc_fcf_record_fcf_fc_SHIFT		25
#define lpfc_fcf_record_fcf_fc_MASK		0x00000001
#define lpfc_fcf_record_fcf_fc_WORD		word7
#define lpfc_fcf_record_fcf_sol_SHIFT		31
#define lpfc_fcf_record_fcf_sol_MASK		0x00000001
#define lpfc_fcf_record_fcf_sol_WORD		word7
	uint32_t word8;
#define lpfc_fcf_record_fcf_index_SHIFT		0
#define lpfc_fcf_record_fcf_index_MASK		0x0000FFFF
#define lpfc_fcf_record_fcf_index_WORD		word8
#define lpfc_fcf_record_fcf_state_SHIFT		16
#define lpfc_fcf_record_fcf_state_MASK		0x0000FFFF
#define lpfc_fcf_record_fcf_state_WORD		word8
	uint8_t vlan_bitmap[512];
	uint32_t word137;
#define lpfc_fcf_record_switch_name_0_SHIFT	0
#define lpfc_fcf_record_switch_name_0_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_0_WORD	word137
#define lpfc_fcf_record_switch_name_1_SHIFT	8
#define lpfc_fcf_record_switch_name_1_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_1_WORD	word137
#define lpfc_fcf_record_switch_name_2_SHIFT	16
#define lpfc_fcf_record_switch_name_2_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_2_WORD	word137
#define lpfc_fcf_record_switch_name_3_SHIFT	24
#define lpfc_fcf_record_switch_name_3_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_3_WORD	word137
	uint32_t word138;
#define lpfc_fcf_record_switch_name_4_SHIFT	0
#define lpfc_fcf_record_switch_name_4_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_4_WORD	word138
#define lpfc_fcf_record_switch_name_5_SHIFT	8
#define lpfc_fcf_record_switch_name_5_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_5_WORD	word138
#define lpfc_fcf_record_switch_name_6_SHIFT	16
#define lpfc_fcf_record_switch_name_6_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_6_WORD	word138
#define lpfc_fcf_record_switch_name_7_SHIFT	24
#define lpfc_fcf_record_switch_name_7_MASK	0x000000FF
#define lpfc_fcf_record_switch_name_7_WORD	word138
};

struct lpfc_mbx_read_fcf_tbl {
	union lpfc_sli4_cfg_shdr cfg_shdr;
	union {
		struct {
			uint32_t word10;
#define lpfc_mbx_read_fcf_tbl_indx_SHIFT	0
#define lpfc_mbx_read_fcf_tbl_indx_MASK		0x0000FFFF
#define lpfc_mbx_read_fcf_tbl_indx_WORD		word10
		} request;
		struct {
			uint32_t eventag;
		} response;
	} u;
	uint32_t word11;
#define lpfc_mbx_read_fcf_tbl_nxt_vindx_SHIFT	0
#define lpfc_mbx_read_fcf_tbl_nxt_vindx_MASK	0x0000FFFF
#define lpfc_mbx_read_fcf_tbl_nxt_vindx_WORD	word11
};

struct lpfc_mbx_add_fcf_tbl_entry {
	union lpfc_sli4_cfg_shdr cfg_shdr;
	uint32_t word10;
#define lpfc_mbx_add_fcf_tbl_fcfi_SHIFT        0
#define lpfc_mbx_add_fcf_tbl_fcfi_MASK         0x0000FFFF
#define lpfc_mbx_add_fcf_tbl_fcfi_WORD         word10
	struct lpfc_mbx_sge fcf_sge;
};

struct lpfc_mbx_del_fcf_tbl_entry {
	struct mbox_header header;
	uint32_t word10;
#define lpfc_mbx_del_fcf_tbl_count_SHIFT	0
#define lpfc_mbx_del_fcf_tbl_count_MASK		0x0000FFFF
#define lpfc_mbx_del_fcf_tbl_count_WORD		word10
#define lpfc_mbx_del_fcf_tbl_index_SHIFT	16
#define lpfc_mbx_del_fcf_tbl_index_MASK		0x0000FFFF
#define lpfc_mbx_del_fcf_tbl_index_WORD		word10
};

struct lpfc_mbx_redisc_fcf_tbl {
	struct mbox_header header;
	uint32_t word10;
#define lpfc_mbx_redisc_fcf_count_SHIFT		0
#define lpfc_mbx_redisc_fcf_count_MASK		0x0000FFFF
#define lpfc_mbx_redisc_fcf_count_WORD		word10
	uint32_t resvd;
	uint32_t word12;
#define lpfc_mbx_redisc_fcf_index_SHIFT		0
#define lpfc_mbx_redisc_fcf_index_MASK		0x0000FFFF
#define lpfc_mbx_redisc_fcf_index_WORD		word12
};

/* Status field for embedded SLI_CONFIG mailbox command */
#define STATUS_SUCCESS					0x0
#define STATUS_FAILED 					0x1
#define STATUS_ILLEGAL_REQUEST				0x2
#define STATUS_ILLEGAL_FIELD				0x3
#define STATUS_INSUFFICIENT_BUFFER 			0x4
#define STATUS_UNAUTHORIZED_REQUEST			0x5
#define STATUS_FLASHROM_SAVE_FAILED			0x17
#define STATUS_FLASHROM_RESTORE_FAILED			0x18
#define STATUS_ICCBINDEX_ALLOC_FAILED			0x1a
#define STATUS_IOCTLHANDLE_ALLOC_FAILED 		0x1b
#define STATUS_INVALID_PHY_ADDR_FROM_OSM		0x1c
#define STATUS_INVALID_PHY_ADDR_LEN_FROM_OSM		0x1d
#define STATUS_ASSERT_FAILED				0x1e
#define STATUS_INVALID_SESSION				0x1f
#define STATUS_INVALID_CONNECTION			0x20
#define STATUS_BTL_PATH_EXCEEDS_OSM_LIMIT		0x21
#define STATUS_BTL_NO_FREE_SLOT_PATH			0x24
#define STATUS_BTL_NO_FREE_SLOT_TGTID			0x25
#define STATUS_OSM_DEVSLOT_NOT_FOUND			0x26
#define STATUS_FLASHROM_READ_FAILED			0x27
#define STATUS_POLL_IOCTL_TIMEOUT			0x28
#define STATUS_ERROR_ACITMAIN				0x2a
#define STATUS_REBOOT_REQUIRED				0x2c
#define STATUS_FCF_IN_USE				0x3a
#define STATUS_FCF_TABLE_EMPTY				0x43

/*
 * Additional status field for embedded SLI_CONFIG mailbox
 * command.
 */
#define ADD_STATUS_OPERATION_ALREADY_ACTIVE		0x67

struct lpfc_mbx_sli4_config {
	struct mbox_header header;
};

struct lpfc_mbx_init_vfi {
	uint32_t word1;
#define lpfc_init_vfi_vr_SHIFT		31
#define lpfc_init_vfi_vr_MASK		0x00000001
#define lpfc_init_vfi_vr_WORD		word1
#define lpfc_init_vfi_vt_SHIFT		30
#define lpfc_init_vfi_vt_MASK		0x00000001
#define lpfc_init_vfi_vt_WORD		word1
#define lpfc_init_vfi_vf_SHIFT		29
#define lpfc_init_vfi_vf_MASK		0x00000001
#define lpfc_init_vfi_vf_WORD		word1
#define lpfc_init_vfi_vp_SHIFT		28
#define lpfc_init_vfi_vp_MASK		0x00000001
#define lpfc_init_vfi_vp_WORD		word1
#define lpfc_init_vfi_vfi_SHIFT		0
#define lpfc_init_vfi_vfi_MASK		0x0000FFFF
#define lpfc_init_vfi_vfi_WORD		word1
	uint32_t word2;
#define lpfc_init_vfi_vpi_SHIFT		16
#define lpfc_init_vfi_vpi_MASK		0x0000FFFF
#define lpfc_init_vfi_vpi_WORD		word2
#define lpfc_init_vfi_fcfi_SHIFT	0
#define lpfc_init_vfi_fcfi_MASK		0x0000FFFF
#define lpfc_init_vfi_fcfi_WORD		word2
	uint32_t word3;
#define lpfc_init_vfi_pri_SHIFT		13
#define lpfc_init_vfi_pri_MASK		0x00000007
#define lpfc_init_vfi_pri_WORD		word3
#define lpfc_init_vfi_vf_id_SHIFT	1
#define lpfc_init_vfi_vf_id_MASK	0x00000FFF
#define lpfc_init_vfi_vf_id_WORD	word3
	uint32_t word4;
#define lpfc_init_vfi_hop_count_SHIFT	24
#define lpfc_init_vfi_hop_count_MASK	0x000000FF
#define lpfc_init_vfi_hop_count_WORD	word4
};
#define MBX_VFI_IN_USE			0x9F02


struct lpfc_mbx_reg_vfi {
	uint32_t word1;
#define lpfc_reg_vfi_upd_SHIFT		29
#define lpfc_reg_vfi_upd_MASK		0x00000001
#define lpfc_reg_vfi_upd_WORD		word1
#define lpfc_reg_vfi_vp_SHIFT		28
#define lpfc_reg_vfi_vp_MASK		0x00000001
#define lpfc_reg_vfi_vp_WORD		word1
#define lpfc_reg_vfi_vfi_SHIFT		0
#define lpfc_reg_vfi_vfi_MASK		0x0000FFFF
#define lpfc_reg_vfi_vfi_WORD		word1
	uint32_t word2;
#define lpfc_reg_vfi_vpi_SHIFT		16
#define lpfc_reg_vfi_vpi_MASK		0x0000FFFF
#define lpfc_reg_vfi_vpi_WORD		word2
#define lpfc_reg_vfi_fcfi_SHIFT		0
#define lpfc_reg_vfi_fcfi_MASK		0x0000FFFF
#define lpfc_reg_vfi_fcfi_WORD		word2
	uint32_t wwn[2];
	struct ulp_bde64 bde;
	uint32_t e_d_tov;
	uint32_t r_a_tov;
	uint32_t word10;
#define lpfc_reg_vfi_nport_id_SHIFT		0
#define lpfc_reg_vfi_nport_id_MASK		0x00FFFFFF
#define lpfc_reg_vfi_nport_id_WORD		word10
};

struct lpfc_mbx_init_vpi {
	uint32_t word1;
#define lpfc_init_vpi_vfi_SHIFT		16
#define lpfc_init_vpi_vfi_MASK		0x0000FFFF
#define lpfc_init_vpi_vfi_WORD		word1
#define lpfc_init_vpi_vpi_SHIFT		0
#define lpfc_init_vpi_vpi_MASK		0x0000FFFF
#define lpfc_init_vpi_vpi_WORD		word1
};

struct lpfc_mbx_read_vpi {
	uint32_t word1_rsvd;
	uint32_t word2;
#define lpfc_mbx_read_vpi_vnportid_SHIFT	0
#define lpfc_mbx_read_vpi_vnportid_MASK		0x00FFFFFF
#define lpfc_mbx_read_vpi_vnportid_WORD		word2
	uint32_t word3_rsvd;
	uint32_t word4;
#define lpfc_mbx_read_vpi_acq_alpa_SHIFT	0
#define lpfc_mbx_read_vpi_acq_alpa_MASK		0x000000FF
#define lpfc_mbx_read_vpi_acq_alpa_WORD		word4
#define lpfc_mbx_read_vpi_pb_SHIFT		15
#define lpfc_mbx_read_vpi_pb_MASK		0x00000001
#define lpfc_mbx_read_vpi_pb_WORD		word4
#define lpfc_mbx_read_vpi_spec_alpa_SHIFT	16
#define lpfc_mbx_read_vpi_spec_alpa_MASK	0x000000FF
#define lpfc_mbx_read_vpi_spec_alpa_WORD	word4
#define lpfc_mbx_read_vpi_ns_SHIFT		30
#define lpfc_mbx_read_vpi_ns_MASK		0x00000001
#define lpfc_mbx_read_vpi_ns_WORD		word4
#define lpfc_mbx_read_vpi_hl_SHIFT		31
#define lpfc_mbx_read_vpi_hl_MASK		0x00000001
#define lpfc_mbx_read_vpi_hl_WORD		word4
	uint32_t word5_rsvd;
	uint32_t word6;
#define lpfc_mbx_read_vpi_vpi_SHIFT		0
#define lpfc_mbx_read_vpi_vpi_MASK		0x0000FFFF
#define lpfc_mbx_read_vpi_vpi_WORD		word6
	uint32_t word7;
#define lpfc_mbx_read_vpi_mac_0_SHIFT		0
#define lpfc_mbx_read_vpi_mac_0_MASK		0x000000FF
#define lpfc_mbx_read_vpi_mac_0_WORD		word7
#define lpfc_mbx_read_vpi_mac_1_SHIFT		8
#define lpfc_mbx_read_vpi_mac_1_MASK		0x000000FF
#define lpfc_mbx_read_vpi_mac_1_WORD		word7
#define lpfc_mbx_read_vpi_mac_2_SHIFT		16
#define lpfc_mbx_read_vpi_mac_2_MASK		0x000000FF
#define lpfc_mbx_read_vpi_mac_2_WORD		word7
#define lpfc_mbx_read_vpi_mac_3_SHIFT		24
#define lpfc_mbx_read_vpi_mac_3_MASK		0x000000FF
#define lpfc_mbx_read_vpi_mac_3_WORD		word7
	uint32_t word8;
#define lpfc_mbx_read_vpi_mac_4_SHIFT		0
#define lpfc_mbx_read_vpi_mac_4_MASK		0x000000FF
#define lpfc_mbx_read_vpi_mac_4_WORD		word8
#define lpfc_mbx_read_vpi_mac_5_SHIFT		8
#define lpfc_mbx_read_vpi_mac_5_MASK		0x000000FF
#define lpfc_mbx_read_vpi_mac_5_WORD		word8
#define lpfc_mbx_read_vpi_vlan_tag_SHIFT	16
#define lpfc_mbx_read_vpi_vlan_tag_MASK		0x00000FFF
#define lpfc_mbx_read_vpi_vlan_tag_WORD		word8
#define lpfc_mbx_read_vpi_vv_SHIFT		28
#define lpfc_mbx_read_vpi_vv_MASK		0x0000001
#define lpfc_mbx_read_vpi_vv_WORD		word8
};

struct lpfc_mbx_unreg_vfi {
	uint32_t word1_rsvd;
	uint32_t word2;
#define lpfc_unreg_vfi_vfi_SHIFT	0
#define lpfc_unreg_vfi_vfi_MASK		0x0000FFFF
#define lpfc_unreg_vfi_vfi_WORD		word2
};

struct lpfc_mbx_resume_rpi {
	uint32_t word1;
#define lpfc_resume_rpi_index_SHIFT	0
#define lpfc_resume_rpi_index_MASK	0x0000FFFF
#define lpfc_resume_rpi_index_WORD	word1
#define lpfc_resume_rpi_ii_SHIFT	30
#define lpfc_resume_rpi_ii_MASK		0x00000003
#define lpfc_resume_rpi_ii_WORD		word1
#define RESUME_INDEX_RPI		0
#define RESUME_INDEX_VPI		1
#define RESUME_INDEX_VFI		2
#define RESUME_INDEX_FCFI		3
	uint32_t event_tag;
};

#define REG_FCF_INVALID_QID	0xFFFF
struct lpfc_mbx_reg_fcfi {
	uint32_t word1;
#define lpfc_reg_fcfi_info_index_SHIFT	0
#define lpfc_reg_fcfi_info_index_MASK	0x0000FFFF
#define lpfc_reg_fcfi_info_index_WORD	word1
#define lpfc_reg_fcfi_fcfi_SHIFT	16
#define lpfc_reg_fcfi_fcfi_MASK		0x0000FFFF
#define lpfc_reg_fcfi_fcfi_WORD		word1
	uint32_t word2;
#define lpfc_reg_fcfi_rq_id1_SHIFT	0
#define lpfc_reg_fcfi_rq_id1_MASK	0x0000FFFF
#define lpfc_reg_fcfi_rq_id1_WORD	word2
#define lpfc_reg_fcfi_rq_id0_SHIFT	16
#define lpfc_reg_fcfi_rq_id0_MASK	0x0000FFFF
#define lpfc_reg_fcfi_rq_id0_WORD	word2
	uint32_t word3;
#define lpfc_reg_fcfi_rq_id3_SHIFT	0
#define lpfc_reg_fcfi_rq_id3_MASK	0x0000FFFF
#define lpfc_reg_fcfi_rq_id3_WORD	word3
#define lpfc_reg_fcfi_rq_id2_SHIFT	16
#define lpfc_reg_fcfi_rq_id2_MASK	0x0000FFFF
#define lpfc_reg_fcfi_rq_id2_WORD	word3
	uint32_t word4;
#define lpfc_reg_fcfi_type_match0_SHIFT	24
#define lpfc_reg_fcfi_type_match0_MASK	0x000000FF
#define lpfc_reg_fcfi_type_match0_WORD	word4
#define lpfc_reg_fcfi_type_mask0_SHIFT	16
#define lpfc_reg_fcfi_type_mask0_MASK	0x000000FF
#define lpfc_reg_fcfi_type_mask0_WORD	word4
#define lpfc_reg_fcfi_rctl_match0_SHIFT	8
#define lpfc_reg_fcfi_rctl_match0_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_match0_WORD	word4
#define lpfc_reg_fcfi_rctl_mask0_SHIFT	0
#define lpfc_reg_fcfi_rctl_mask0_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_mask0_WORD	word4
	uint32_t word5;
#define lpfc_reg_fcfi_type_match1_SHIFT	24
#define lpfc_reg_fcfi_type_match1_MASK	0x000000FF
#define lpfc_reg_fcfi_type_match1_WORD	word5
#define lpfc_reg_fcfi_type_mask1_SHIFT	16
#define lpfc_reg_fcfi_type_mask1_MASK	0x000000FF
#define lpfc_reg_fcfi_type_mask1_WORD	word5
#define lpfc_reg_fcfi_rctl_match1_SHIFT	8
#define lpfc_reg_fcfi_rctl_match1_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_match1_WORD	word5
#define lpfc_reg_fcfi_rctl_mask1_SHIFT	0
#define lpfc_reg_fcfi_rctl_mask1_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_mask1_WORD	word5
	uint32_t word6;
#define lpfc_reg_fcfi_type_match2_SHIFT	24
#define lpfc_reg_fcfi_type_match2_MASK	0x000000FF
#define lpfc_reg_fcfi_type_match2_WORD	word6
#define lpfc_reg_fcfi_type_mask2_SHIFT	16
#define lpfc_reg_fcfi_type_mask2_MASK	0x000000FF
#define lpfc_reg_fcfi_type_mask2_WORD	word6
#define lpfc_reg_fcfi_rctl_match2_SHIFT	8
#define lpfc_reg_fcfi_rctl_match2_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_match2_WORD	word6
#define lpfc_reg_fcfi_rctl_mask2_SHIFT	0
#define lpfc_reg_fcfi_rctl_mask2_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_mask2_WORD	word6
	uint32_t word7;
#define lpfc_reg_fcfi_type_match3_SHIFT	24
#define lpfc_reg_fcfi_type_match3_MASK	0x000000FF
#define lpfc_reg_fcfi_type_match3_WORD	word7
#define lpfc_reg_fcfi_type_mask3_SHIFT	16
#define lpfc_reg_fcfi_type_mask3_MASK	0x000000FF
#define lpfc_reg_fcfi_type_mask3_WORD	word7
#define lpfc_reg_fcfi_rctl_match3_SHIFT	8
#define lpfc_reg_fcfi_rctl_match3_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_match3_WORD	word7
#define lpfc_reg_fcfi_rctl_mask3_SHIFT	0
#define lpfc_reg_fcfi_rctl_mask3_MASK	0x000000FF
#define lpfc_reg_fcfi_rctl_mask3_WORD	word7
	uint32_t word8;
#define lpfc_reg_fcfi_mam_SHIFT		13
#define lpfc_reg_fcfi_mam_MASK		0x00000003
#define lpfc_reg_fcfi_mam_WORD		word8
#define LPFC_MAM_BOTH		0	/* Both SPMA and FPMA */
#define LPFC_MAM_SPMA		1	/* Server Provided MAC Address */
#define LPFC_MAM_FPMA		2	/* Fabric Provided MAC Address */
#define lpfc_reg_fcfi_vv_SHIFT		12
#define lpfc_reg_fcfi_vv_MASK		0x00000001
#define lpfc_reg_fcfi_vv_WORD		word8
#define lpfc_reg_fcfi_vlan_tag_SHIFT	0
#define lpfc_reg_fcfi_vlan_tag_MASK	0x00000FFF
#define lpfc_reg_fcfi_vlan_tag_WORD	word8
};

struct lpfc_mbx_reg_fcfi_mrq {
	uint32_t word1;
#define lpfc_reg_fcfi_mrq_info_index_SHIFT	0
#define lpfc_reg_fcfi_mrq_info_index_MASK	0x0000FFFF
#define lpfc_reg_fcfi_mrq_info_index_WORD	word1
#define lpfc_reg_fcfi_mrq_fcfi_SHIFT		16
#define lpfc_reg_fcfi_mrq_fcfi_MASK		0x0000FFFF
#define lpfc_reg_fcfi_mrq_fcfi_WORD		word1
	uint32_t word2;
#define lpfc_reg_fcfi_mrq_rq_id1_SHIFT		0
#define lpfc_reg_fcfi_mrq_rq_id1_MASK		0x0000FFFF
#define lpfc_reg_fcfi_mrq_rq_id1_WORD		word2
#define lpfc_reg_fcfi_mrq_rq_id0_SHIFT		16
#define lpfc_reg_fcfi_mrq_rq_id0_MASK		0x0000FFFF
#define lpfc_reg_fcfi_mrq_rq_id0_WORD		word2
	uint32_t word3;
#define lpfc_reg_fcfi_mrq_rq_id3_SHIFT		0
#define lpfc_reg_fcfi_mrq_rq_id3_MASK		0x0000FFFF
#define lpfc_reg_fcfi_mrq_rq_id3_WORD		word3
#define lpfc_reg_fcfi_mrq_rq_id2_SHIFT		16
#define lpfc_reg_fcfi_mrq_rq_id2_MASK		0x0000FFFF
#define lpfc_reg_fcfi_mrq_rq_id2_WORD		word3
	uint32_t word4;
#define lpfc_reg_fcfi_mrq_type_match0_SHIFT	24
#define lpfc_reg_fcfi_mrq_type_match0_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_match0_WORD	word4
#define lpfc_reg_fcfi_mrq_type_mask0_SHIFT	16
#define lpfc_reg_fcfi_mrq_type_mask0_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_mask0_WORD	word4
#define lpfc_reg_fcfi_mrq_rctl_match0_SHIFT	8
#define lpfc_reg_fcfi_mrq_rctl_match0_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_match0_WORD	word4
#define lpfc_reg_fcfi_mrq_rctl_mask0_SHIFT	0
#define lpfc_reg_fcfi_mrq_rctl_mask0_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_mask0_WORD	word4
	uint32_t word5;
#define lpfc_reg_fcfi_mrq_type_match1_SHIFT	24
#define lpfc_reg_fcfi_mrq_type_match1_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_match1_WORD	word5
#define lpfc_reg_fcfi_mrq_type_mask1_SHIFT	16
#define lpfc_reg_fcfi_mrq_type_mask1_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_mask1_WORD	word5
#define lpfc_reg_fcfi_mrq_rctl_match1_SHIFT	8
#define lpfc_reg_fcfi_mrq_rctl_match1_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_match1_WORD	word5
#define lpfc_reg_fcfi_mrq_rctl_mask1_SHIFT	0
#define lpfc_reg_fcfi_mrq_rctl_mask1_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_mask1_WORD	word5
	uint32_t word6;
#define lpfc_reg_fcfi_mrq_type_match2_SHIFT	24
#define lpfc_reg_fcfi_mrq_type_match2_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_match2_WORD	word6
#define lpfc_reg_fcfi_mrq_type_mask2_SHIFT	16
#define lpfc_reg_fcfi_mrq_type_mask2_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_mask2_WORD	word6
#define lpfc_reg_fcfi_mrq_rctl_match2_SHIFT	8
#define lpfc_reg_fcfi_mrq_rctl_match2_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_match2_WORD	word6
#define lpfc_reg_fcfi_mrq_rctl_mask2_SHIFT	0
#define lpfc_reg_fcfi_mrq_rctl_mask2_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_mask2_WORD	word6
	uint32_t word7;
#define lpfc_reg_fcfi_mrq_type_match3_SHIFT	24
#define lpfc_reg_fcfi_mrq_type_match3_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_match3_WORD	word7
#define lpfc_reg_fcfi_mrq_type_mask3_SHIFT	16
#define lpfc_reg_fcfi_mrq_type_mask3_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_type_mask3_WORD	word7
#define lpfc_reg_fcfi_mrq_rctl_match3_SHIFT	8
#define lpfc_reg_fcfi_mrq_rctl_match3_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_match3_WORD	word7
#define lpfc_reg_fcfi_mrq_rctl_mask3_SHIFT	0
#define lpfc_reg_fcfi_mrq_rctl_mask3_MASK	0x000000FF
#define lpfc_reg_fcfi_mrq_rctl_mask3_WORD	word7
	uint32_t word8;
#define lpfc_reg_fcfi_mrq_ptc7_SHIFT		31
#define lpfc_reg_fcfi_mrq_ptc7_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc7_WORD		word8
#define lpfc_reg_fcfi_mrq_ptc6_SHIFT		30
#define lpfc_reg_fcfi_mrq_ptc6_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc6_WORD		word8
#define lpfc_reg_fcfi_mrq_ptc5_SHIFT		29
#define lpfc_reg_fcfi_mrq_ptc5_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc5_WORD		word8
#define lpfc_reg_fcfi_mrq_ptc4_SHIFT		28
#define lpfc_reg_fcfi_mrq_ptc4_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc4_WORD		word8
#define lpfc_reg_fcfi_mrq_ptc3_SHIFT		27
#define lpfc_reg_fcfi_mrq_ptc3_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc3_WORD		word8
#define lpfc_reg_fcfi_mrq_ptc2_SHIFT		26
#define lpfc_reg_fcfi_mrq_ptc2_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc2_WORD		word8
#define lpfc_reg_fcfi_mrq_ptc1_SHIFT		25
#define lpfc_reg_fcfi_mrq_ptc1_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc1_WORD		word8
#define lpfc_reg_fcfi_mrq_ptc0_SHIFT		24
#define lpfc_reg_fcfi_mrq_ptc0_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_ptc0_WORD		word8
#define lpfc_reg_fcfi_mrq_pt7_SHIFT		23
#define lpfc_reg_fcfi_mrq_pt7_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt7_WORD		word8
#define lpfc_reg_fcfi_mrq_pt6_SHIFT		22
#define lpfc_reg_fcfi_mrq_pt6_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt6_WORD		word8
#define lpfc_reg_fcfi_mrq_pt5_SHIFT		21
#define lpfc_reg_fcfi_mrq_pt5_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt5_WORD		word8
#define lpfc_reg_fcfi_mrq_pt4_SHIFT		20
#define lpfc_reg_fcfi_mrq_pt4_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt4_WORD		word8
#define lpfc_reg_fcfi_mrq_pt3_SHIFT		19
#define lpfc_reg_fcfi_mrq_pt3_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt3_WORD		word8
#define lpfc_reg_fcfi_mrq_pt2_SHIFT		18
#define lpfc_reg_fcfi_mrq_pt2_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt2_WORD		word8
#define lpfc_reg_fcfi_mrq_pt1_SHIFT		17
#define lpfc_reg_fcfi_mrq_pt1_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt1_WORD		word8
#define lpfc_reg_fcfi_mrq_pt0_SHIFT		16
#define lpfc_reg_fcfi_mrq_pt0_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_pt0_WORD		word8
#define lpfc_reg_fcfi_mrq_xmv_SHIFT		15
#define lpfc_reg_fcfi_mrq_xmv_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_xmv_WORD		word8
#define lpfc_reg_fcfi_mrq_mode_SHIFT		13
#define lpfc_reg_fcfi_mrq_mode_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_mode_WORD		word8
#define lpfc_reg_fcfi_mrq_vv_SHIFT		12
#define lpfc_reg_fcfi_mrq_vv_MASK		0x00000001
#define lpfc_reg_fcfi_mrq_vv_WORD		word8
#define lpfc_reg_fcfi_mrq_vlan_tag_SHIFT	0
#define lpfc_reg_fcfi_mrq_vlan_tag_MASK		0x00000FFF
#define lpfc_reg_fcfi_mrq_vlan_tag_WORD		word8
	uint32_t word9;
#define lpfc_reg_fcfi_mrq_policy_SHIFT		12
#define lpfc_reg_fcfi_mrq_policy_MASK		0x0000000F
#define lpfc_reg_fcfi_mrq_policy_WORD		word9
#define lpfc_reg_fcfi_mrq_filter_SHIFT		8
#define lpfc_reg_fcfi_mrq_filter_MASK		0x0000000F
#define lpfc_reg_fcfi_mrq_filter_WORD		word9
#define lpfc_reg_fcfi_mrq_npairs_SHIFT		0
#define lpfc_reg_fcfi_mrq_npairs_MASK		0x000000FF
#define lpfc_reg_fcfi_mrq_npairs_WORD		word9
	uint32_t word10;
	uint32_t word11;
	uint32_t word12;
	uint32_t word13;
	uint32_t word14;
	uint32_t word15;
	uint32_t word16;
};

struct lpfc_mbx_unreg_fcfi {
	uint32_t word1_rsv;
	uint32_t word2;
#define lpfc_unreg_fcfi_SHIFT		0
#define lpfc_unreg_fcfi_MASK		0x0000FFFF
#define lpfc_unreg_fcfi_WORD		word2
};

struct lpfc_mbx_read_rev {
	uint32_t word1;
#define lpfc_mbx_rd_rev_sli_lvl_SHIFT  		16
#define lpfc_mbx_rd_rev_sli_lvl_MASK   		0x0000000F
#define lpfc_mbx_rd_rev_sli_lvl_WORD   		word1
#define lpfc_mbx_rd_rev_fcoe_SHIFT		20
#define lpfc_mbx_rd_rev_fcoe_MASK		0x00000001
#define lpfc_mbx_rd_rev_fcoe_WORD		word1
#define lpfc_mbx_rd_rev_cee_ver_SHIFT		21
#define lpfc_mbx_rd_rev_cee_ver_MASK		0x00000003
#define lpfc_mbx_rd_rev_cee_ver_WORD		word1
#define LPFC_PREDCBX_CEE_MODE	0
#define LPFC_DCBX_CEE_MODE	1
#define lpfc_mbx_rd_rev_vpd_SHIFT		29
#define lpfc_mbx_rd_rev_vpd_MASK		0x00000001
#define lpfc_mbx_rd_rev_vpd_WORD		word1
	uint32_t first_hw_rev;
	uint32_t second_hw_rev;
	uint32_t word4_rsvd;
	uint32_t third_hw_rev;
	uint32_t word6;
#define lpfc_mbx_rd_rev_fcph_low_SHIFT		0
#define lpfc_mbx_rd_rev_fcph_low_MASK		0x000000FF
#define lpfc_mbx_rd_rev_fcph_low_WORD		word6
#define lpfc_mbx_rd_rev_fcph_high_SHIFT		8
#define lpfc_mbx_rd_rev_fcph_high_MASK		0x000000FF
#define lpfc_mbx_rd_rev_fcph_high_WORD		word6
#define lpfc_mbx_rd_rev_ftr_lvl_low_SHIFT	16
#define lpfc_mbx_rd_rev_ftr_lvl_low_MASK	0x000000FF
#define lpfc_mbx_rd_rev_ftr_lvl_low_WORD	word6
#define lpfc_mbx_rd_rev_ftr_lvl_high_SHIFT	24
#define lpfc_mbx_rd_rev_ftr_lvl_high_MASK	0x000000FF
#define lpfc_mbx_rd_rev_ftr_lvl_high_WORD	word6
	uint32_t word7_rsvd;
	uint32_t fw_id_rev;
	uint8_t  fw_name[16];
	uint32_t ulp_fw_id_rev;
	uint8_t  ulp_fw_name[16];
	uint32_t word18_47_rsvd[30];
	uint32_t word48;
#define lpfc_mbx_rd_rev_avail_len_SHIFT		0
#define lpfc_mbx_rd_rev_avail_len_MASK		0x00FFFFFF
#define lpfc_mbx_rd_rev_avail_len_WORD		word48
	uint32_t vpd_paddr_low;
	uint32_t vpd_paddr_high;
	uint32_t avail_vpd_len;
	uint32_t rsvd_52_63[12];
};

struct lpfc_mbx_read_config {
	uint32_t word1;
#define lpfc_mbx_rd_conf_extnts_inuse_SHIFT	31
#define lpfc_mbx_rd_conf_extnts_inuse_MASK	0x00000001
#define lpfc_mbx_rd_conf_extnts_inuse_WORD	word1
	uint32_t word2;
#define lpfc_mbx_rd_conf_lnk_numb_SHIFT		0
#define lpfc_mbx_rd_conf_lnk_numb_MASK		0x0000003F
#define lpfc_mbx_rd_conf_lnk_numb_WORD		word2
#define lpfc_mbx_rd_conf_lnk_type_SHIFT		6
#define lpfc_mbx_rd_conf_lnk_type_MASK		0x00000003
#define lpfc_mbx_rd_conf_lnk_type_WORD		word2
#define LPFC_LNK_TYPE_GE	0
#define LPFC_LNK_TYPE_FC	1
#define lpfc_mbx_rd_conf_lnk_ldv_SHIFT		8
#define lpfc_mbx_rd_conf_lnk_ldv_MASK		0x00000001
#define lpfc_mbx_rd_conf_lnk_ldv_WORD		word2
#define lpfc_mbx_rd_conf_topology_SHIFT		24
#define lpfc_mbx_rd_conf_topology_MASK		0x000000FF
#define lpfc_mbx_rd_conf_topology_WORD		word2
	uint32_t rsvd_3;
	uint32_t word4;
#define lpfc_mbx_rd_conf_e_d_tov_SHIFT		0
#define lpfc_mbx_rd_conf_e_d_tov_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_e_d_tov_WORD		word4
	uint32_t rsvd_5;
	uint32_t word6;
#define lpfc_mbx_rd_conf_r_a_tov_SHIFT		0
#define lpfc_mbx_rd_conf_r_a_tov_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_r_a_tov_WORD		word6
#define lpfc_mbx_rd_conf_link_speed_SHIFT	16
#define lpfc_mbx_rd_conf_link_speed_MASK	0x0000FFFF
#define lpfc_mbx_rd_conf_link_speed_WORD	word6
	uint32_t rsvd_7;
	uint32_t rsvd_8;
	uint32_t word9;
#define lpfc_mbx_rd_conf_lmt_SHIFT		0
#define lpfc_mbx_rd_conf_lmt_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_lmt_WORD		word9
	uint32_t rsvd_10;
	uint32_t rsvd_11;
	uint32_t word12;
#define lpfc_mbx_rd_conf_xri_base_SHIFT		0
#define lpfc_mbx_rd_conf_xri_base_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_xri_base_WORD		word12
#define lpfc_mbx_rd_conf_xri_count_SHIFT	16
#define lpfc_mbx_rd_conf_xri_count_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_xri_count_WORD		word12
	uint32_t word13;
#define lpfc_mbx_rd_conf_rpi_base_SHIFT		0
#define lpfc_mbx_rd_conf_rpi_base_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_rpi_base_WORD		word13
#define lpfc_mbx_rd_conf_rpi_count_SHIFT	16
#define lpfc_mbx_rd_conf_rpi_count_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_rpi_count_WORD		word13
	uint32_t word14;
#define lpfc_mbx_rd_conf_vpi_base_SHIFT		0
#define lpfc_mbx_rd_conf_vpi_base_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_vpi_base_WORD		word14
#define lpfc_mbx_rd_conf_vpi_count_SHIFT	16
#define lpfc_mbx_rd_conf_vpi_count_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_vpi_count_WORD		word14
	uint32_t word15;
#define lpfc_mbx_rd_conf_vfi_base_SHIFT         0
#define lpfc_mbx_rd_conf_vfi_base_MASK          0x0000FFFF
#define lpfc_mbx_rd_conf_vfi_base_WORD          word15
#define lpfc_mbx_rd_conf_vfi_count_SHIFT        16
#define lpfc_mbx_rd_conf_vfi_count_MASK         0x0000FFFF
#define lpfc_mbx_rd_conf_vfi_count_WORD         word15
	uint32_t word16;
#define lpfc_mbx_rd_conf_fcfi_count_SHIFT	16
#define lpfc_mbx_rd_conf_fcfi_count_MASK	0x0000FFFF
#define lpfc_mbx_rd_conf_fcfi_count_WORD	word16
	uint32_t word17;
#define lpfc_mbx_rd_conf_rq_count_SHIFT		0
#define lpfc_mbx_rd_conf_rq_count_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_rq_count_WORD		word17
#define lpfc_mbx_rd_conf_eq_count_SHIFT		16
#define lpfc_mbx_rd_conf_eq_count_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_eq_count_WORD		word17
	uint32_t word18;
#define lpfc_mbx_rd_conf_wq_count_SHIFT		0
#define lpfc_mbx_rd_conf_wq_count_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_wq_count_WORD		word18
#define lpfc_mbx_rd_conf_cq_count_SHIFT		16
#define lpfc_mbx_rd_conf_cq_count_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_cq_count_WORD		word18
};

struct lpfc_mbx_request_features {
	uint32_t word1;
#define lpfc_mbx_rq_ftr_qry_SHIFT		0
#define lpfc_mbx_rq_ftr_qry_MASK		0x00000001
#define lpfc_mbx_rq_ftr_qry_WORD		word1
	uint32_t word2;
#define lpfc_mbx_rq_ftr_rq_iaab_SHIFT		0
#define lpfc_mbx_rq_ftr_rq_iaab_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_iaab_WORD		word2
#define lpfc_mbx_rq_ftr_rq_npiv_SHIFT		1
#define lpfc_mbx_rq_ftr_rq_npiv_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_npiv_WORD		word2
#define lpfc_mbx_rq_ftr_rq_dif_SHIFT		2
#define lpfc_mbx_rq_ftr_rq_dif_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_dif_WORD		word2
#define lpfc_mbx_rq_ftr_rq_vf_SHIFT		3
#define lpfc_mbx_rq_ftr_rq_vf_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_vf_WORD		word2
#define lpfc_mbx_rq_ftr_rq_fcpi_SHIFT		4
#define lpfc_mbx_rq_ftr_rq_fcpi_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_fcpi_WORD		word2
#define lpfc_mbx_rq_ftr_rq_fcpt_SHIFT		5
#define lpfc_mbx_rq_ftr_rq_fcpt_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_fcpt_WORD		word2
#define lpfc_mbx_rq_ftr_rq_fcpc_SHIFT		6
#define lpfc_mbx_rq_ftr_rq_fcpc_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_fcpc_WORD		word2
#define lpfc_mbx_rq_ftr_rq_ifip_SHIFT		7
#define lpfc_mbx_rq_ftr_rq_ifip_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_ifip_WORD		word2
#define lpfc_mbx_rq_ftr_rq_perfh_SHIFT		11
#define lpfc_mbx_rq_ftr_rq_perfh_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_perfh_WORD		word2
#define lpfc_mbx_rq_ftr_rq_mrqp_SHIFT		16
#define lpfc_mbx_rq_ftr_rq_mrqp_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rq_mrqp_WORD		word2
	uint32_t word3;
#define lpfc_mbx_rq_ftr_rsp_iaab_SHIFT		0
#define lpfc_mbx_rq_ftr_rsp_iaab_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_iaab_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_npiv_SHIFT		1
#define lpfc_mbx_rq_ftr_rsp_npiv_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_npiv_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_dif_SHIFT		2
#define lpfc_mbx_rq_ftr_rsp_dif_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_dif_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_vf_SHIFT		3
#define lpfc_mbx_rq_ftr_rsp_vf__MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_vf_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_fcpi_SHIFT		4
#define lpfc_mbx_rq_ftr_rsp_fcpi_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_fcpi_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_fcpt_SHIFT		5
#define lpfc_mbx_rq_ftr_rsp_fcpt_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_fcpt_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_fcpc_SHIFT		6
#define lpfc_mbx_rq_ftr_rsp_fcpc_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_fcpc_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_ifip_SHIFT		7
#define lpfc_mbx_rq_ftr_rsp_ifip_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_ifip_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_perfh_SHIFT		11
#define lpfc_mbx_rq_ftr_rsp_perfh_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_perfh_WORD		word3
#define lpfc_mbx_rq_ftr_rsp_mrqp_SHIFT		16
#define lpfc_mbx_rq_ftr_rsp_mrqp_MASK		0x00000001
#define lpfc_mbx_rq_ftr_rsp_mrqp_WORD		word3
};

struct lpfc_mbx_supp_pages {
	uint32_t word1;
#define qs_SHIFT 				0
#define qs_MASK					0x00000001
#define qs_WORD					word1
#define wr_SHIFT				1
#define wr_MASK 				0x00000001
#define wr_WORD					word1
#define pf_SHIFT				8
#define pf_MASK					0x000000ff
#define pf_WORD					word1
#define cpn_SHIFT				16
#define cpn_MASK				0x000000ff
#define cpn_WORD				word1
	uint32_t word2;
#define list_offset_SHIFT 			0
#define list_offset_MASK			0x000000ff
#define list_offset_WORD			word2
#define next_offset_SHIFT			8
#define next_offset_MASK			0x000000ff
#define next_offset_WORD			word2
#define elem_cnt_SHIFT				16
#define elem_cnt_MASK				0x000000ff
#define elem_cnt_WORD				word2
	uint32_t word3;
#define pn_0_SHIFT				24
#define pn_0_MASK  				0x000000ff
#define pn_0_WORD				word3
#define pn_1_SHIFT				16
#define pn_1_MASK				0x000000ff
#define pn_1_WORD				word3
#define pn_2_SHIFT				8
#define pn_2_MASK				0x000000ff
#define pn_2_WORD				word3
#define pn_3_SHIFT				0
#define pn_3_MASK				0x000000ff
#define pn_3_WORD				word3
	uint32_t word4;
#define pn_4_SHIFT				24
#define pn_4_MASK				0x000000ff
#define pn_4_WORD				word4
#define pn_5_SHIFT				16
#define pn_5_MASK				0x000000ff
#define pn_5_WORD				word4
#define pn_6_SHIFT				8
#define pn_6_MASK				0x000000ff
#define pn_6_WORD				word4
#define pn_7_SHIFT				0
#define pn_7_MASK				0x000000ff
#define pn_7_WORD				word4
	uint32_t rsvd[27];
#define LPFC_SUPP_PAGES			0
#define LPFC_BLOCK_GUARD_PROFILES	1
#define LPFC_SLI4_PARAMETERS		2
};

struct lpfc_mbx_memory_dump_type3 {
	uint32_t word1;
#define lpfc_mbx_memory_dump_type3_type_SHIFT    0
#define lpfc_mbx_memory_dump_type3_type_MASK     0x0000000f
#define lpfc_mbx_memory_dump_type3_type_WORD     word1
#define lpfc_mbx_memory_dump_type3_link_SHIFT    24
#define lpfc_mbx_memory_dump_type3_link_MASK     0x000000ff
#define lpfc_mbx_memory_dump_type3_link_WORD     word1
	uint32_t word2;
#define lpfc_mbx_memory_dump_type3_page_no_SHIFT  0
#define lpfc_mbx_memory_dump_type3_page_no_MASK   0x0000ffff
#define lpfc_mbx_memory_dump_type3_page_no_WORD   word2
#define lpfc_mbx_memory_dump_type3_offset_SHIFT   16
#define lpfc_mbx_memory_dump_type3_offset_MASK    0x0000ffff
#define lpfc_mbx_memory_dump_type3_offset_WORD    word2
	uint32_t word3;
#define lpfc_mbx_memory_dump_type3_length_SHIFT  0
#define lpfc_mbx_memory_dump_type3_length_MASK   0x00ffffff
#define lpfc_mbx_memory_dump_type3_length_WORD   word3
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t return_len;
};

#define DMP_PAGE_A0             0xa0
#define DMP_PAGE_A2             0xa2
#define DMP_SFF_PAGE_A0_SIZE	256
#define DMP_SFF_PAGE_A2_SIZE	256

#define SFP_WAVELENGTH_LC1310	1310
#define SFP_WAVELENGTH_LL1550	1550


/*
 *  * SFF-8472 TABLE 3.4
 *   */
#define  SFF_PG0_CONNECTOR_UNKNOWN    0x00   /* Unknown  */
#define  SFF_PG0_CONNECTOR_SC         0x01   /* SC       */
#define  SFF_PG0_CONNECTOR_FC_COPPER1 0x02   /* FC style 1 copper connector */
#define  SFF_PG0_CONNECTOR_FC_COPPER2 0x03   /* FC style 2 copper connector */
#define  SFF_PG0_CONNECTOR_BNC        0x04   /* BNC / TNC */
#define  SFF_PG0_CONNECTOR__FC_COAX   0x05   /* FC coaxial headers */
#define  SFF_PG0_CONNECTOR_FIBERJACK  0x06   /* FiberJack */
#define  SFF_PG0_CONNECTOR_LC         0x07   /* LC        */
#define  SFF_PG0_CONNECTOR_MT         0x08   /* MT - RJ   */
#define  SFF_PG0_CONNECTOR_MU         0x09   /* MU        */
#define  SFF_PG0_CONNECTOR_SF         0x0A   /* SG        */
#define  SFF_PG0_CONNECTOR_OPTICAL_PIGTAIL 0x0B /* Optical pigtail */
#define  SFF_PG0_CONNECTOR_OPTICAL_PARALLEL 0x0C /* MPO Parallel Optic */
#define  SFF_PG0_CONNECTOR_HSSDC_II   0x20   /* HSSDC II */
#define  SFF_PG0_CONNECTOR_COPPER_PIGTAIL 0x21 /* Copper pigtail */
#define  SFF_PG0_CONNECTOR_RJ45       0x22  /* RJ45 */

/* SFF-8472 Table 3.1 Diagnostics: Data Fields Address/Page A0 */

#define SSF_IDENTIFIER			0
#define SSF_EXT_IDENTIFIER		1
#define SSF_CONNECTOR			2
#define SSF_TRANSCEIVER_CODE_B0		3
#define SSF_TRANSCEIVER_CODE_B1		4
#define SSF_TRANSCEIVER_CODE_B2		5
#define SSF_TRANSCEIVER_CODE_B3		6
#define SSF_TRANSCEIVER_CODE_B4		7
#define SSF_TRANSCEIVER_CODE_B5		8
#define SSF_TRANSCEIVER_CODE_B6		9
#define SSF_TRANSCEIVER_CODE_B7		10
#define SSF_ENCODING			11
#define SSF_BR_NOMINAL			12
#define SSF_RATE_IDENTIFIER		13
#define SSF_LENGTH_9UM_KM		14
#define SSF_LENGTH_9UM			15
#define SSF_LENGTH_50UM_OM2		16
#define SSF_LENGTH_62UM_OM1		17
#define SFF_LENGTH_COPPER		18
#define SSF_LENGTH_50UM_OM3		19
#define SSF_VENDOR_NAME			20
#define SSF_VENDOR_OUI			36
#define SSF_VENDOR_PN			40
#define SSF_VENDOR_REV			56
#define SSF_WAVELENGTH_B1		60
#define SSF_WAVELENGTH_B0		61
#define SSF_CC_BASE			63
#define SSF_OPTIONS_B1			64
#define SSF_OPTIONS_B0			65
#define SSF_BR_MAX			66
#define SSF_BR_MIN			67
#define SSF_VENDOR_SN			68
#define SSF_DATE_CODE			84
#define SSF_MONITORING_TYPEDIAGNOSTIC	92
#define SSF_ENHANCED_OPTIONS		93
#define SFF_8472_COMPLIANCE		94
#define SSF_CC_EXT			95
#define SSF_A0_VENDOR_SPECIFIC		96

/* SFF-8472 Table 3.1a Diagnostics: Data Fields Address/Page A2 */

#define SSF_TEMP_HIGH_ALARM		0
#define SSF_TEMP_LOW_ALARM		2
#define SSF_TEMP_HIGH_WARNING		4
#define SSF_TEMP_LOW_WARNING		6
#define SSF_VOLTAGE_HIGH_ALARM		8
#define SSF_VOLTAGE_LOW_ALARM		10
#define SSF_VOLTAGE_HIGH_WARNING	12
#define SSF_VOLTAGE_LOW_WARNING		14
#define SSF_BIAS_HIGH_ALARM		16
#define SSF_BIAS_LOW_ALARM		18
#define SSF_BIAS_HIGH_WARNING		20
#define SSF_BIAS_LOW_WARNING		22
#define SSF_TXPOWER_HIGH_ALARM		24
#define SSF_TXPOWER_LOW_ALARM		26
#define SSF_TXPOWER_HIGH_WARNING	28
#define SSF_TXPOWER_LOW_WARNING		30
#define SSF_RXPOWER_HIGH_ALARM		32
#define SSF_RXPOWER_LOW_ALARM		34
#define SSF_RXPOWER_HIGH_WARNING	36
#define SSF_RXPOWER_LOW_WARNING		38
#define SSF_EXT_CAL_CONSTANTS		56
#define SSF_CC_DMI			95
#define SFF_TEMPERATURE_B1		96
#define SFF_TEMPERATURE_B0		97
#define SFF_VCC_B1			98
#define SFF_VCC_B0			99
#define SFF_TX_BIAS_CURRENT_B1		100
#define SFF_TX_BIAS_CURRENT_B0		101
#define SFF_TXPOWER_B1			102
#define SFF_TXPOWER_B0			103
#define SFF_RXPOWER_B1			104
#define SFF_RXPOWER_B0			105
#define SSF_STATUS_CONTROL		110
#define SSF_ALARM_FLAGS			112
#define SSF_WARNING_FLAGS		116
#define SSF_EXT_TATUS_CONTROL_B1	118
#define SSF_EXT_TATUS_CONTROL_B0	119
#define SSF_A2_VENDOR_SPECIFIC		120
#define SSF_USER_EEPROM			128
#define SSF_VENDOR_CONTROL		148


/*
 * Tranceiver codes Fibre Channel SFF-8472
 * Table 3.5.
 */

struct sff_trasnceiver_codes_byte0 {
	uint8_t inifiband:4;
	uint8_t teng_ethernet:4;
};

struct sff_trasnceiver_codes_byte1 {
	uint8_t  sonet:6;
	uint8_t  escon:2;
};

struct sff_trasnceiver_codes_byte2 {
	uint8_t  soNet:8;
};

struct sff_trasnceiver_codes_byte3 {
	uint8_t ethernet:8;
};

struct sff_trasnceiver_codes_byte4 {
	uint8_t fc_el_lo:1;
	uint8_t fc_lw_laser:1;
	uint8_t fc_sw_laser:1;
	uint8_t fc_md_distance:1;
	uint8_t fc_lg_distance:1;
	uint8_t fc_int_distance:1;
	uint8_t fc_short_distance:1;
	uint8_t fc_vld_distance:1;
};

struct sff_trasnceiver_codes_byte5 {
	uint8_t reserved1:1;
	uint8_t reserved2:1;
	uint8_t fc_sfp_active:1;  /* Active cable   */
	uint8_t fc_sfp_passive:1; /* Passive cable  */
	uint8_t fc_lw_laser:1;     /* Longwave laser */
	uint8_t fc_sw_laser_sl:1;
	uint8_t fc_sw_laser_sn:1;
	uint8_t fc_el_hi:1;        /* Electrical enclosure high bit */
};

struct sff_trasnceiver_codes_byte6 {
	uint8_t fc_tm_sm:1;      /* Single Mode */
	uint8_t reserved:1;
	uint8_t fc_tm_m6:1;       /* Multimode, 62.5um (M6) */
	uint8_t fc_tm_tv:1;      /* Video Coax (TV) */
	uint8_t fc_tm_mi:1;      /* Miniature Coax (MI) */
	uint8_t fc_tm_tp:1;      /* Twisted Pair (TP) */
	uint8_t fc_tm_tw:1;      /* Twin Axial Pair  */
};

struct sff_trasnceiver_codes_byte7 {
	uint8_t fc_sp_100MB:1;   /*  100 MB/sec */
	uint8_t reserve:1;
	uint8_t fc_sp_200mb:1;   /*  200 MB/sec */
	uint8_t fc_sp_3200MB:1;  /* 3200 MB/sec */
	uint8_t fc_sp_400MB:1;   /*  400 MB/sec */
	uint8_t fc_sp_1600MB:1;  /* 1600 MB/sec */
	uint8_t fc_sp_800MB:1;   /*  800 MB/sec */
	uint8_t fc_sp_1200MB:1;  /* 1200 MB/sec */
};

/* User writable non-volatile memory, SFF-8472 Table 3.20 */
struct user_eeprom {
	uint8_t vendor_name[16];
	uint8_t vendor_oui[3];
	uint8_t vendor_pn[816];
	uint8_t vendor_rev[4];
	uint8_t vendor_sn[16];
	uint8_t datecode[6];
	uint8_t lot_code[2];
	uint8_t reserved191[57];
};

struct lpfc_mbx_pc_sli4_params {
	uint32_t word1;
#define qs_SHIFT				0
#define qs_MASK					0x00000001
#define qs_WORD					word1
#define wr_SHIFT				1
#define wr_MASK					0x00000001
#define wr_WORD					word1
#define pf_SHIFT				8
#define pf_MASK					0x000000ff
#define pf_WORD					word1
#define cpn_SHIFT				16
#define cpn_MASK				0x000000ff
#define cpn_WORD				word1
	uint32_t word2;
#define if_type_SHIFT				0
#define if_type_MASK				0x00000007
#define if_type_WORD				word2
#define sli_rev_SHIFT				4
#define sli_rev_MASK				0x0000000f
#define sli_rev_WORD				word2
#define sli_family_SHIFT			8
#define sli_family_MASK				0x000000ff
#define sli_family_WORD				word2
#define featurelevel_1_SHIFT			16
#define featurelevel_1_MASK			0x000000ff
#define featurelevel_1_WORD			word2
#define featurelevel_2_SHIFT			24
#define featurelevel_2_MASK			0x0000001f
#define featurelevel_2_WORD			word2
	uint32_t word3;
#define fcoe_SHIFT 				0
#define fcoe_MASK				0x00000001
#define fcoe_WORD				word3
#define fc_SHIFT				1
#define fc_MASK					0x00000001
#define fc_WORD					word3
#define nic_SHIFT				2
#define nic_MASK				0x00000001
#define nic_WORD				word3
#define iscsi_SHIFT				3
#define iscsi_MASK				0x00000001
#define iscsi_WORD				word3
#define rdma_SHIFT				4
#define rdma_MASK				0x00000001
#define rdma_WORD				word3
	uint32_t sge_supp_len;
#define SLI4_PAGE_SIZE 4096
	uint32_t word5;
#define if_page_sz_SHIFT			0
#define if_page_sz_MASK				0x0000ffff
#define if_page_sz_WORD				word5
#define loopbk_scope_SHIFT			24
#define loopbk_scope_MASK			0x0000000f
#define loopbk_scope_WORD			word5
#define rq_db_window_SHIFT			28
#define rq_db_window_MASK			0x0000000f
#define rq_db_window_WORD			word5
	uint32_t word6;
#define eq_pages_SHIFT				0
#define eq_pages_MASK				0x0000000f
#define eq_pages_WORD				word6
#define eqe_size_SHIFT				8
#define eqe_size_MASK				0x000000ff
#define eqe_size_WORD				word6
	uint32_t word7;
#define cq_pages_SHIFT				0
#define cq_pages_MASK				0x0000000f
#define cq_pages_WORD				word7
#define cqe_size_SHIFT				8
#define cqe_size_MASK				0x000000ff
#define cqe_size_WORD				word7
	uint32_t word8;
#define mq_pages_SHIFT				0
#define mq_pages_MASK				0x0000000f
#define mq_pages_WORD				word8
#define mqe_size_SHIFT				8
#define mqe_size_MASK				0x000000ff
#define mqe_size_WORD				word8
#define mq_elem_cnt_SHIFT			16
#define mq_elem_cnt_MASK			0x000000ff
#define mq_elem_cnt_WORD			word8
	uint32_t word9;
#define wq_pages_SHIFT				0
#define wq_pages_MASK				0x0000ffff
#define wq_pages_WORD				word9
#define wqe_size_SHIFT				8
#define wqe_size_MASK				0x000000ff
#define wqe_size_WORD				word9
	uint32_t word10;
#define rq_pages_SHIFT				0
#define rq_pages_MASK				0x0000ffff
#define rq_pages_WORD				word10
#define rqe_size_SHIFT				8
#define rqe_size_MASK				0x000000ff
#define rqe_size_WORD				word10
	uint32_t word11;
#define hdr_pages_SHIFT				0
#define hdr_pages_MASK				0x0000000f
#define hdr_pages_WORD				word11
#define hdr_size_SHIFT				8
#define hdr_size_MASK				0x0000000f
#define hdr_size_WORD				word11
#define hdr_pp_align_SHIFT			16
#define hdr_pp_align_MASK			0x0000ffff
#define hdr_pp_align_WORD			word11
	uint32_t word12;
#define sgl_pages_SHIFT				0
#define sgl_pages_MASK				0x0000000f
#define sgl_pages_WORD				word12
#define sgl_pp_align_SHIFT			16
#define sgl_pp_align_MASK			0x0000ffff
#define sgl_pp_align_WORD			word12
	uint32_t rsvd_13_63[51];
};
#define SLI4_PAGE_ALIGN(addr) (((addr)+((SLI4_PAGE_SIZE)-1)) \
			       &(~((SLI4_PAGE_SIZE)-1)))

struct lpfc_sli4_parameters {
	uint32_t word0;
#define cfg_prot_type_SHIFT			0
#define cfg_prot_type_MASK			0x000000FF
#define cfg_prot_type_WORD			word0
	uint32_t word1;
#define cfg_ft_SHIFT				0
#define cfg_ft_MASK				0x00000001
#define cfg_ft_WORD				word1
#define cfg_sli_rev_SHIFT			4
#define cfg_sli_rev_MASK			0x0000000f
#define cfg_sli_rev_WORD			word1
#define cfg_sli_family_SHIFT			8
#define cfg_sli_family_MASK			0x0000000f
#define cfg_sli_family_WORD			word1
#define cfg_if_type_SHIFT			12
#define cfg_if_type_MASK			0x0000000f
#define cfg_if_type_WORD			word1
#define cfg_sli_hint_1_SHIFT			16
#define cfg_sli_hint_1_MASK			0x000000ff
#define cfg_sli_hint_1_WORD			word1
#define cfg_sli_hint_2_SHIFT			24
#define cfg_sli_hint_2_MASK			0x0000001f
#define cfg_sli_hint_2_WORD			word1
	uint32_t word2;
	uint32_t word3;
	uint32_t word4;
#define cfg_cqv_SHIFT				14
#define cfg_cqv_MASK				0x00000003
#define cfg_cqv_WORD				word4
	uint32_t word5;
	uint32_t word6;
#define cfg_mqv_SHIFT				14
#define cfg_mqv_MASK				0x00000003
#define cfg_mqv_WORD				word6
	uint32_t word7;
	uint32_t word8;
#define cfg_wqpcnt_SHIFT			0
#define cfg_wqpcnt_MASK				0x0000000f
#define cfg_wqpcnt_WORD				word8
#define cfg_wqsize_SHIFT			8
#define cfg_wqsize_MASK				0x0000000f
#define cfg_wqsize_WORD				word8
#define cfg_wqv_SHIFT				14
#define cfg_wqv_MASK				0x00000003
#define cfg_wqv_WORD				word8
#define cfg_wqpsize_SHIFT			16
#define cfg_wqpsize_MASK			0x000000ff
#define cfg_wqpsize_WORD			word8
	uint32_t word9;
	uint32_t word10;
#define cfg_rqv_SHIFT				14
#define cfg_rqv_MASK				0x00000003
#define cfg_rqv_WORD				word10
	uint32_t word11;
#define cfg_rq_db_window_SHIFT			28
#define cfg_rq_db_window_MASK			0x0000000f
#define cfg_rq_db_window_WORD			word11
	uint32_t word12;
#define cfg_fcoe_SHIFT				0
#define cfg_fcoe_MASK				0x00000001
#define cfg_fcoe_WORD				word12
#define cfg_ext_SHIFT				1
#define cfg_ext_MASK				0x00000001
#define cfg_ext_WORD				word12
#define cfg_hdrr_SHIFT				2
#define cfg_hdrr_MASK				0x00000001
#define cfg_hdrr_WORD				word12
#define cfg_phwq_SHIFT				15
#define cfg_phwq_MASK				0x00000001
#define cfg_phwq_WORD				word12
#define cfg_oas_SHIFT				25
#define cfg_oas_MASK				0x00000001
#define cfg_oas_WORD				word12
#define cfg_loopbk_scope_SHIFT			28
#define cfg_loopbk_scope_MASK			0x0000000f
#define cfg_loopbk_scope_WORD			word12
	uint32_t sge_supp_len;
	uint32_t word14;
#define cfg_sgl_page_cnt_SHIFT			0
#define cfg_sgl_page_cnt_MASK			0x0000000f
#define cfg_sgl_page_cnt_WORD			word14
#define cfg_sgl_page_size_SHIFT			8
#define cfg_sgl_page_size_MASK			0x000000ff
#define cfg_sgl_page_size_WORD			word14
#define cfg_sgl_pp_align_SHIFT			16
#define cfg_sgl_pp_align_MASK			0x000000ff
#define cfg_sgl_pp_align_WORD			word14
	uint32_t word15;
	uint32_t word16;
	uint32_t word17;
	uint32_t word18;
	uint32_t word19;
#define cfg_ext_embed_cb_SHIFT			0
#define cfg_ext_embed_cb_MASK			0x00000001
#define cfg_ext_embed_cb_WORD			word19
#define cfg_mds_diags_SHIFT			1
#define cfg_mds_diags_MASK			0x00000001
#define cfg_mds_diags_WORD			word19
#define cfg_nvme_SHIFT				3
#define cfg_nvme_MASK				0x00000001
#define cfg_nvme_WORD				word19
#define cfg_xib_SHIFT				4
#define cfg_xib_MASK				0x00000001
#define cfg_xib_WORD				word19
};

#define LPFC_SET_UE_RECOVERY		0x10
#define LPFC_SET_MDS_DIAGS		0x11
struct lpfc_mbx_set_feature {
	struct mbox_header header;
	uint32_t feature;
	uint32_t param_len;
	uint32_t word6;
#define lpfc_mbx_set_feature_UER_SHIFT  0
#define lpfc_mbx_set_feature_UER_MASK   0x00000001
#define lpfc_mbx_set_feature_UER_WORD   word6
#define lpfc_mbx_set_feature_mds_SHIFT  0
#define lpfc_mbx_set_feature_mds_MASK   0x00000001
#define lpfc_mbx_set_feature_mds_WORD   word6
#define lpfc_mbx_set_feature_mds_deep_loopbk_SHIFT  1
#define lpfc_mbx_set_feature_mds_deep_loopbk_MASK   0x00000001
#define lpfc_mbx_set_feature_mds_deep_loopbk_WORD   word6
	uint32_t word7;
#define lpfc_mbx_set_feature_UERP_SHIFT 0
#define lpfc_mbx_set_feature_UERP_MASK  0x0000ffff
#define lpfc_mbx_set_feature_UERP_WORD  word7
#define lpfc_mbx_set_feature_UESR_SHIFT 16
#define lpfc_mbx_set_feature_UESR_MASK  0x0000ffff
#define lpfc_mbx_set_feature_UESR_WORD  word7
};


#define LPFC_SET_HOST_OS_DRIVER_VERSION    0x2
struct lpfc_mbx_set_host_data {
#define LPFC_HOST_OS_DRIVER_VERSION_SIZE   48
	struct mbox_header header;
	uint32_t param_id;
	uint32_t param_len;
	uint8_t  data[LPFC_HOST_OS_DRIVER_VERSION_SIZE];
};


struct lpfc_mbx_get_sli4_parameters {
	struct mbox_header header;
	struct lpfc_sli4_parameters sli4_parameters;
};

struct lpfc_rscr_desc_generic {
#define LPFC_RSRC_DESC_WSIZE			22
	uint32_t desc[LPFC_RSRC_DESC_WSIZE];
};

struct lpfc_rsrc_desc_pcie {
	uint32_t word0;
#define lpfc_rsrc_desc_pcie_type_SHIFT		0
#define lpfc_rsrc_desc_pcie_type_MASK		0x000000ff
#define lpfc_rsrc_desc_pcie_type_WORD		word0
#define LPFC_RSRC_DESC_TYPE_PCIE		0x40
#define lpfc_rsrc_desc_pcie_length_SHIFT	8
#define lpfc_rsrc_desc_pcie_length_MASK		0x000000ff
#define lpfc_rsrc_desc_pcie_length_WORD		word0
	uint32_t word1;
#define lpfc_rsrc_desc_pcie_pfnum_SHIFT		0
#define lpfc_rsrc_desc_pcie_pfnum_MASK		0x000000ff
#define lpfc_rsrc_desc_pcie_pfnum_WORD		word1
	uint32_t reserved;
	uint32_t word3;
#define lpfc_rsrc_desc_pcie_sriov_sta_SHIFT	0
#define lpfc_rsrc_desc_pcie_sriov_sta_MASK	0x000000ff
#define lpfc_rsrc_desc_pcie_sriov_sta_WORD	word3
#define lpfc_rsrc_desc_pcie_pf_sta_SHIFT	8
#define lpfc_rsrc_desc_pcie_pf_sta_MASK		0x000000ff
#define lpfc_rsrc_desc_pcie_pf_sta_WORD		word3
#define lpfc_rsrc_desc_pcie_pf_type_SHIFT	16
#define lpfc_rsrc_desc_pcie_pf_type_MASK	0x000000ff
#define lpfc_rsrc_desc_pcie_pf_type_WORD	word3
	uint32_t word4;
#define lpfc_rsrc_desc_pcie_nr_virtfn_SHIFT	0
#define lpfc_rsrc_desc_pcie_nr_virtfn_MASK	0x0000ffff
#define lpfc_rsrc_desc_pcie_nr_virtfn_WORD	word4
};

struct lpfc_rsrc_desc_fcfcoe {
	uint32_t word0;
#define lpfc_rsrc_desc_fcfcoe_type_SHIFT	0
#define lpfc_rsrc_desc_fcfcoe_type_MASK		0x000000ff
#define lpfc_rsrc_desc_fcfcoe_type_WORD		word0
#define LPFC_RSRC_DESC_TYPE_FCFCOE		0x43
#define lpfc_rsrc_desc_fcfcoe_length_SHIFT	8
#define lpfc_rsrc_desc_fcfcoe_length_MASK	0x000000ff
#define lpfc_rsrc_desc_fcfcoe_length_WORD	word0
#define LPFC_RSRC_DESC_TYPE_FCFCOE_V0_RSVD	0
#define LPFC_RSRC_DESC_TYPE_FCFCOE_V0_LENGTH	72
#define LPFC_RSRC_DESC_TYPE_FCFCOE_V1_LENGTH	88
	uint32_t word1;
#define lpfc_rsrc_desc_fcfcoe_vfnum_SHIFT	0
#define lpfc_rsrc_desc_fcfcoe_vfnum_MASK	0x000000ff
#define lpfc_rsrc_desc_fcfcoe_vfnum_WORD	word1
#define lpfc_rsrc_desc_fcfcoe_pfnum_SHIFT	16
#define lpfc_rsrc_desc_fcfcoe_pfnum_MASK        0x000007ff
#define lpfc_rsrc_desc_fcfcoe_pfnum_WORD        word1
	uint32_t word2;
#define lpfc_rsrc_desc_fcfcoe_rpi_cnt_SHIFT	0
#define lpfc_rsrc_desc_fcfcoe_rpi_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_rpi_cnt_WORD	word2
#define lpfc_rsrc_desc_fcfcoe_xri_cnt_SHIFT	16
#define lpfc_rsrc_desc_fcfcoe_xri_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_xri_cnt_WORD	word2
	uint32_t word3;
#define lpfc_rsrc_desc_fcfcoe_wq_cnt_SHIFT	0
#define lpfc_rsrc_desc_fcfcoe_wq_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_wq_cnt_WORD	word3
#define lpfc_rsrc_desc_fcfcoe_rq_cnt_SHIFT	16
#define lpfc_rsrc_desc_fcfcoe_rq_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_rq_cnt_WORD	word3
	uint32_t word4;
#define lpfc_rsrc_desc_fcfcoe_cq_cnt_SHIFT	0
#define lpfc_rsrc_desc_fcfcoe_cq_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_cq_cnt_WORD	word4
#define lpfc_rsrc_desc_fcfcoe_vpi_cnt_SHIFT	16
#define lpfc_rsrc_desc_fcfcoe_vpi_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_vpi_cnt_WORD	word4
	uint32_t word5;
#define lpfc_rsrc_desc_fcfcoe_fcfi_cnt_SHIFT	0
#define lpfc_rsrc_desc_fcfcoe_fcfi_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_fcfi_cnt_WORD	word5
#define lpfc_rsrc_desc_fcfcoe_vfi_cnt_SHIFT	16
#define lpfc_rsrc_desc_fcfcoe_vfi_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_vfi_cnt_WORD	word5
	uint32_t word6;
	uint32_t word7;
	uint32_t word8;
	uint32_t word9;
	uint32_t word10;
	uint32_t word11;
	uint32_t word12;
	uint32_t word13;
#define lpfc_rsrc_desc_fcfcoe_lnk_nr_SHIFT	0
#define lpfc_rsrc_desc_fcfcoe_lnk_nr_MASK	0x0000003f
#define lpfc_rsrc_desc_fcfcoe_lnk_nr_WORD	word13
#define lpfc_rsrc_desc_fcfcoe_lnk_tp_SHIFT      6
#define lpfc_rsrc_desc_fcfcoe_lnk_tp_MASK	0x00000003
#define lpfc_rsrc_desc_fcfcoe_lnk_tp_WORD	word13
#define lpfc_rsrc_desc_fcfcoe_lmc_SHIFT		8
#define lpfc_rsrc_desc_fcfcoe_lmc_MASK		0x00000001
#define lpfc_rsrc_desc_fcfcoe_lmc_WORD		word13
#define lpfc_rsrc_desc_fcfcoe_lld_SHIFT		9
#define lpfc_rsrc_desc_fcfcoe_lld_MASK		0x00000001
#define lpfc_rsrc_desc_fcfcoe_lld_WORD		word13
#define lpfc_rsrc_desc_fcfcoe_eq_cnt_SHIFT	16
#define lpfc_rsrc_desc_fcfcoe_eq_cnt_MASK	0x0000ffff
#define lpfc_rsrc_desc_fcfcoe_eq_cnt_WORD	word13
/* extended FC/FCoE Resource Descriptor when length = 88 bytes */
	uint32_t bw_min;
	uint32_t bw_max;
	uint32_t iops_min;
	uint32_t iops_max;
	uint32_t reserved[4];
};

struct lpfc_func_cfg {
#define LPFC_RSRC_DESC_MAX_NUM			2
	uint32_t rsrc_desc_count;
	struct lpfc_rscr_desc_generic desc[LPFC_RSRC_DESC_MAX_NUM];
};

struct lpfc_mbx_get_func_cfg {
	struct mbox_header header;
#define LPFC_CFG_TYPE_PERSISTENT_OVERRIDE	0x0
#define LPFC_CFG_TYPE_FACTURY_DEFAULT		0x1
#define LPFC_CFG_TYPE_CURRENT_ACTIVE		0x2
	struct lpfc_func_cfg func_cfg;
};

struct lpfc_prof_cfg {
#define LPFC_RSRC_DESC_MAX_NUM			2
	uint32_t rsrc_desc_count;
	struct lpfc_rscr_desc_generic desc[LPFC_RSRC_DESC_MAX_NUM];
};

struct lpfc_mbx_get_prof_cfg {
	struct mbox_header header;
#define LPFC_CFG_TYPE_PERSISTENT_OVERRIDE	0x0
#define LPFC_CFG_TYPE_FACTURY_DEFAULT		0x1
#define LPFC_CFG_TYPE_CURRENT_ACTIVE		0x2
	union {
		struct {
			uint32_t word10;
#define lpfc_mbx_get_prof_cfg_prof_id_SHIFT	0
#define lpfc_mbx_get_prof_cfg_prof_id_MASK	0x000000ff
#define lpfc_mbx_get_prof_cfg_prof_id_WORD	word10
#define lpfc_mbx_get_prof_cfg_prof_tp_SHIFT	8
#define lpfc_mbx_get_prof_cfg_prof_tp_MASK	0x00000003
#define lpfc_mbx_get_prof_cfg_prof_tp_WORD	word10
		} request;
		struct {
			struct lpfc_prof_cfg prof_cfg;
		} response;
	} u;
};

struct lpfc_controller_attribute {
	uint32_t version_string[8];
	uint32_t manufacturer_name[8];
	uint32_t supported_modes;
	uint32_t word17;
#define lpfc_cntl_attr_eprom_ver_lo_SHIFT	0
#define lpfc_cntl_attr_eprom_ver_lo_MASK	0x000000ff
#define lpfc_cntl_attr_eprom_ver_lo_WORD	word17
#define lpfc_cntl_attr_eprom_ver_hi_SHIFT	8
#define lpfc_cntl_attr_eprom_ver_hi_MASK	0x000000ff
#define lpfc_cntl_attr_eprom_ver_hi_WORD	word17
	uint32_t mbx_da_struct_ver;
	uint32_t ep_fw_da_struct_ver;
	uint32_t ncsi_ver_str[3];
	uint32_t dflt_ext_timeout;
	uint32_t model_number[8];
	uint32_t description[16];
	uint32_t serial_number[8];
	uint32_t ip_ver_str[8];
	uint32_t fw_ver_str[8];
	uint32_t bios_ver_str[8];
	uint32_t redboot_ver_str[8];
	uint32_t driver_ver_str[8];
	uint32_t flash_fw_ver_str[8];
	uint32_t functionality;
	uint32_t word105;
#define lpfc_cntl_attr_max_cbd_len_SHIFT	0
#define lpfc_cntl_attr_max_cbd_len_MASK		0x0000ffff
#define lpfc_cntl_attr_max_cbd_len_WORD		word105
#define lpfc_cntl_attr_asic_rev_SHIFT		16
#define lpfc_cntl_attr_asic_rev_MASK		0x000000ff
#define lpfc_cntl_attr_asic_rev_WORD		word105
#define lpfc_cntl_attr_gen_guid0_SHIFT		24
#define lpfc_cntl_attr_gen_guid0_MASK		0x000000ff
#define lpfc_cntl_attr_gen_guid0_WORD		word105
	uint32_t gen_guid1_12[3];
	uint32_t word109;
#define lpfc_cntl_attr_gen_guid13_14_SHIFT	0
#define lpfc_cntl_attr_gen_guid13_14_MASK	0x0000ffff
#define lpfc_cntl_attr_gen_guid13_14_WORD	word109
#define lpfc_cntl_attr_gen_guid15_SHIFT		16
#define lpfc_cntl_attr_gen_guid15_MASK		0x000000ff
#define lpfc_cntl_attr_gen_guid15_WORD		word109
#define lpfc_cntl_attr_hba_port_cnt_SHIFT	24
#define lpfc_cntl_attr_hba_port_cnt_MASK	0x000000ff
#define lpfc_cntl_attr_hba_port_cnt_WORD	word109
	uint32_t word110;
#define lpfc_cntl_attr_dflt_lnk_tmo_SHIFT	0
#define lpfc_cntl_attr_dflt_lnk_tmo_MASK	0x0000ffff
#define lpfc_cntl_attr_dflt_lnk_tmo_WORD	word110
#define lpfc_cntl_attr_multi_func_dev_SHIFT	24
#define lpfc_cntl_attr_multi_func_dev_MASK	0x000000ff
#define lpfc_cntl_attr_multi_func_dev_WORD	word110
	uint32_t word111;
#define lpfc_cntl_attr_cache_valid_SHIFT	0
#define lpfc_cntl_attr_cache_valid_MASK		0x000000ff
#define lpfc_cntl_attr_cache_valid_WORD		word111
#define lpfc_cntl_attr_hba_status_SHIFT		8
#define lpfc_cntl_attr_hba_status_MASK		0x000000ff
#define lpfc_cntl_attr_hba_status_WORD		word111
#define lpfc_cntl_attr_max_domain_SHIFT		16
#define lpfc_cntl_attr_max_domain_MASK		0x000000ff
#define lpfc_cntl_attr_max_domain_WORD		word111
#define lpfc_cntl_attr_lnk_numb_SHIFT		24
#define lpfc_cntl_attr_lnk_numb_MASK		0x0000003f
#define lpfc_cntl_attr_lnk_numb_WORD		word111
#define lpfc_cntl_attr_lnk_type_SHIFT		30
#define lpfc_cntl_attr_lnk_type_MASK		0x00000003
#define lpfc_cntl_attr_lnk_type_WORD		word111
	uint32_t fw_post_status;
	uint32_t hba_mtu[8];
	uint32_t word121;
	uint32_t reserved1[3];
	uint32_t word125;
#define lpfc_cntl_attr_pci_vendor_id_SHIFT	0
#define lpfc_cntl_attr_pci_vendor_id_MASK	0x0000ffff
#define lpfc_cntl_attr_pci_vendor_id_WORD	word125
#define lpfc_cntl_attr_pci_device_id_SHIFT	16
#define lpfc_cntl_attr_pci_device_id_MASK	0x0000ffff
#define lpfc_cntl_attr_pci_device_id_WORD	word125
	uint32_t word126;
#define lpfc_cntl_attr_pci_subvdr_id_SHIFT	0
#define lpfc_cntl_attr_pci_subvdr_id_MASK	0x0000ffff
#define lpfc_cntl_attr_pci_subvdr_id_WORD	word126
#define lpfc_cntl_attr_pci_subsys_id_SHIFT	16
#define lpfc_cntl_attr_pci_subsys_id_MASK	0x0000ffff
#define lpfc_cntl_attr_pci_subsys_id_WORD	word126
	uint32_t word127;
#define lpfc_cntl_attr_pci_bus_num_SHIFT	0
#define lpfc_cntl_attr_pci_bus_num_MASK		0x000000ff
#define lpfc_cntl_attr_pci_bus_num_WORD		word127
#define lpfc_cntl_attr_pci_dev_num_SHIFT	8
#define lpfc_cntl_attr_pci_dev_num_MASK		0x000000ff
#define lpfc_cntl_attr_pci_dev_num_WORD		word127
#define lpfc_cntl_attr_pci_fnc_num_SHIFT	16
#define lpfc_cntl_attr_pci_fnc_num_MASK		0x000000ff
#define lpfc_cntl_attr_pci_fnc_num_WORD		word127
#define lpfc_cntl_attr_inf_type_SHIFT		24
#define lpfc_cntl_attr_inf_type_MASK		0x000000ff
#define lpfc_cntl_attr_inf_type_WORD		word127
	uint32_t unique_id[2];
	uint32_t word130;
#define lpfc_cntl_attr_num_netfil_SHIFT		0
#define lpfc_cntl_attr_num_netfil_MASK		0x000000ff
#define lpfc_cntl_attr_num_netfil_WORD		word130
	uint32_t reserved2[4];
};

struct lpfc_mbx_get_cntl_attributes {
	union  lpfc_sli4_cfg_shdr cfg_shdr;
	struct lpfc_controller_attribute cntl_attr;
};

struct lpfc_mbx_get_port_name {
	struct mbox_header header;
	union {
		struct {
			uint32_t word4;
#define lpfc_mbx_get_port_name_lnk_type_SHIFT	0
#define lpfc_mbx_get_port_name_lnk_type_MASK	0x00000003
#define lpfc_mbx_get_port_name_lnk_type_WORD	word4
		} request;
		struct {
			uint32_t word4;
#define lpfc_mbx_get_port_name_name0_SHIFT	0
#define lpfc_mbx_get_port_name_name0_MASK	0x000000FF
#define lpfc_mbx_get_port_name_name0_WORD	word4
#define lpfc_mbx_get_port_name_name1_SHIFT	8
#define lpfc_mbx_get_port_name_name1_MASK	0x000000FF
#define lpfc_mbx_get_port_name_name1_WORD	word4
#define lpfc_mbx_get_port_name_name2_SHIFT	16
#define lpfc_mbx_get_port_name_name2_MASK	0x000000FF
#define lpfc_mbx_get_port_name_name2_WORD	word4
#define lpfc_mbx_get_port_name_name3_SHIFT	24
#define lpfc_mbx_get_port_name_name3_MASK	0x000000FF
#define lpfc_mbx_get_port_name_name3_WORD	word4
#define LPFC_LINK_NUMBER_0			0
#define LPFC_LINK_NUMBER_1			1
#define LPFC_LINK_NUMBER_2			2
#define LPFC_LINK_NUMBER_3			3
		} response;
	} u;
};

/* Mailbox Completion Queue Error Messages */
#define MB_CQE_STATUS_SUCCESS			0x0
#define MB_CQE_STATUS_INSUFFICIENT_PRIVILEGES	0x1
#define MB_CQE_STATUS_INVALID_PARAMETER		0x2
#define MB_CQE_STATUS_INSUFFICIENT_RESOURCES	0x3
#define MB_CEQ_STATUS_QUEUE_FLUSHING		0x4
#define MB_CQE_STATUS_DMA_FAILED		0x5

#define LPFC_MBX_WR_CONFIG_MAX_BDE		8
struct lpfc_mbx_wr_object {
	struct mbox_header header;
	union {
		struct {
			uint32_t word4;
#define lpfc_wr_object_eof_SHIFT		31
#define lpfc_wr_object_eof_MASK			0x00000001
#define lpfc_wr_object_eof_WORD			word4
#define lpfc_wr_object_write_length_SHIFT	0
#define lpfc_wr_object_write_length_MASK	0x00FFFFFF
#define lpfc_wr_object_write_length_WORD	word4
			uint32_t write_offset;
			uint32_t object_name[26];
			uint32_t bde_count;
			struct ulp_bde64 bde[LPFC_MBX_WR_CONFIG_MAX_BDE];
		} request;
		struct {
			uint32_t actual_write_length;
		} response;
	} u;
};

/* mailbox queue entry structure */
struct lpfc_mqe {
	uint32_t word0;
#define lpfc_mqe_status_SHIFT		16
#define lpfc_mqe_status_MASK		0x0000FFFF
#define lpfc_mqe_status_WORD		word0
#define lpfc_mqe_command_SHIFT		8
#define lpfc_mqe_command_MASK		0x000000FF
#define lpfc_mqe_command_WORD		word0
	union {
		uint32_t mb_words[LPFC_SLI4_MB_WORD_COUNT - 1];
		/* sli4 mailbox commands */
		struct lpfc_mbx_sli4_config sli4_config;
		struct lpfc_mbx_init_vfi init_vfi;
		struct lpfc_mbx_reg_vfi reg_vfi;
		struct lpfc_mbx_reg_vfi unreg_vfi;
		struct lpfc_mbx_init_vpi init_vpi;
		struct lpfc_mbx_resume_rpi resume_rpi;
		struct lpfc_mbx_read_fcf_tbl read_fcf_tbl;
		struct lpfc_mbx_add_fcf_tbl_entry add_fcf_entry;
		struct lpfc_mbx_del_fcf_tbl_entry del_fcf_entry;
		struct lpfc_mbx_redisc_fcf_tbl redisc_fcf_tbl;
		struct lpfc_mbx_reg_fcfi reg_fcfi;
		struct lpfc_mbx_reg_fcfi_mrq reg_fcfi_mrq;
		struct lpfc_mbx_unreg_fcfi unreg_fcfi;
		struct lpfc_mbx_mq_create mq_create;
		struct lpfc_mbx_mq_create_ext mq_create_ext;
		struct lpfc_mbx_eq_create eq_create;
		struct lpfc_mbx_modify_eq_delay eq_delay;
		struct lpfc_mbx_cq_create cq_create;
		struct lpfc_mbx_cq_create_set cq_create_set;
		struct lpfc_mbx_wq_create wq_create;
		struct lpfc_mbx_rq_create rq_create;
		struct lpfc_mbx_rq_create_v2 rq_create_v2;
		struct lpfc_mbx_mq_destroy mq_destroy;
		struct lpfc_mbx_eq_destroy eq_destroy;
		struct lpfc_mbx_cq_destroy cq_destroy;
		struct lpfc_mbx_wq_destroy wq_destroy;
		struct lpfc_mbx_rq_destroy rq_destroy;
		struct lpfc_mbx_get_rsrc_extent_info rsrc_extent_info;
		struct lpfc_mbx_alloc_rsrc_extents alloc_rsrc_extents;
		struct lpfc_mbx_dealloc_rsrc_extents dealloc_rsrc_extents;
		struct lpfc_mbx_post_sgl_pages post_sgl_pages;
		struct lpfc_mbx_nembed_cmd nembed_cmd;
		struct lpfc_mbx_read_rev read_rev;
		struct lpfc_mbx_read_vpi read_vpi;
		struct lpfc_mbx_read_config rd_config;
		struct lpfc_mbx_request_features req_ftrs;
		struct lpfc_mbx_post_hdr_tmpl hdr_tmpl;
		struct lpfc_mbx_query_fw_config query_fw_cfg;
		struct lpfc_mbx_set_beacon_config beacon_config;
		struct lpfc_mbx_supp_pages supp_pages;
		struct lpfc_mbx_pc_sli4_params sli4_params;
		struct lpfc_mbx_get_sli4_parameters get_sli4_parameters;
		struct lpfc_mbx_set_link_diag_state link_diag_state;
		struct lpfc_mbx_set_link_diag_loopback link_diag_loopback;
		struct lpfc_mbx_run_link_diag_test link_diag_test;
		struct lpfc_mbx_get_func_cfg get_func_cfg;
		struct lpfc_mbx_get_prof_cfg get_prof_cfg;
		struct lpfc_mbx_wr_object wr_object;
		struct lpfc_mbx_get_port_name get_port_name;
		struct lpfc_mbx_set_feature  set_feature;
		struct lpfc_mbx_memory_dump_type3 mem_dump_type3;
		struct lpfc_mbx_set_host_data set_host_data;
		struct lpfc_mbx_nop nop;
	} un;
};

struct lpfc_mcqe {
	uint32_t word0;
#define lpfc_mcqe_status_SHIFT		0
#define lpfc_mcqe_status_MASK		0x0000FFFF
#define lpfc_mcqe_status_WORD		word0
#define lpfc_mcqe_ext_status_SHIFT	16
#define lpfc_mcqe_ext_status_MASK	0x0000FFFF
#define lpfc_mcqe_ext_status_WORD	word0
	uint32_t mcqe_tag0;
	uint32_t mcqe_tag1;
	uint32_t trailer;
#define lpfc_trailer_valid_SHIFT	31
#define lpfc_trailer_valid_MASK		0x00000001
#define lpfc_trailer_valid_WORD		trailer
#define lpfc_trailer_async_SHIFT	30
#define lpfc_trailer_async_MASK		0x00000001
#define lpfc_trailer_async_WORD		trailer
#define lpfc_trailer_hpi_SHIFT		29
#define lpfc_trailer_hpi_MASK		0x00000001
#define lpfc_trailer_hpi_WORD		trailer
#define lpfc_trailer_completed_SHIFT	28
#define lpfc_trailer_completed_MASK	0x00000001
#define lpfc_trailer_completed_WORD	trailer
#define lpfc_trailer_consumed_SHIFT	27
#define lpfc_trailer_consumed_MASK	0x00000001
#define lpfc_trailer_consumed_WORD	trailer
#define lpfc_trailer_type_SHIFT		16
#define lpfc_trailer_type_MASK		0x000000FF
#define lpfc_trailer_type_WORD		trailer
#define lpfc_trailer_code_SHIFT		8
#define lpfc_trailer_code_MASK		0x000000FF
#define lpfc_trailer_code_WORD		trailer
#define LPFC_TRAILER_CODE_LINK	0x1
#define LPFC_TRAILER_CODE_FCOE	0x2
#define LPFC_TRAILER_CODE_DCBX	0x3
#define LPFC_TRAILER_CODE_GRP5	0x5
#define LPFC_TRAILER_CODE_FC	0x10
#define LPFC_TRAILER_CODE_SLI	0x11
};

struct lpfc_acqe_link {
	uint32_t word0;
#define lpfc_acqe_link_speed_SHIFT		24
#define lpfc_acqe_link_speed_MASK		0x000000FF
#define lpfc_acqe_link_speed_WORD		word0
#define LPFC_ASYNC_LINK_SPEED_ZERO		0x0
#define LPFC_ASYNC_LINK_SPEED_10MBPS		0x1
#define LPFC_ASYNC_LINK_SPEED_100MBPS		0x2
#define LPFC_ASYNC_LINK_SPEED_1GBPS		0x3
#define LPFC_ASYNC_LINK_SPEED_10GBPS		0x4
#define LPFC_ASYNC_LINK_SPEED_20GBPS		0x5
#define LPFC_ASYNC_LINK_SPEED_25GBPS		0x6
#define LPFC_ASYNC_LINK_SPEED_40GBPS		0x7
#define LPFC_ASYNC_LINK_SPEED_100GBPS		0x8
#define lpfc_acqe_link_duplex_SHIFT		16
#define lpfc_acqe_link_duplex_MASK		0x000000FF
#define lpfc_acqe_link_duplex_WORD		word0
#define LPFC_ASYNC_LINK_DUPLEX_NONE		0x0
#define LPFC_ASYNC_LINK_DUPLEX_HALF		0x1
#define LPFC_ASYNC_LINK_DUPLEX_FULL		0x2
#define lpfc_acqe_link_status_SHIFT		8
#define lpfc_acqe_link_status_MASK		0x000000FF
#define lpfc_acqe_link_status_WORD		word0
#define LPFC_ASYNC_LINK_STATUS_DOWN		0x0
#define LPFC_ASYNC_LINK_STATUS_UP		0x1
#define LPFC_ASYNC_LINK_STATUS_LOGICAL_DOWN	0x2
#define LPFC_ASYNC_LINK_STATUS_LOGICAL_UP	0x3
#define lpfc_acqe_link_type_SHIFT		6
#define lpfc_acqe_link_type_MASK		0x00000003
#define lpfc_acqe_link_type_WORD		word0
#define lpfc_acqe_link_number_SHIFT		0
#define lpfc_acqe_link_number_MASK		0x0000003F
#define lpfc_acqe_link_number_WORD		word0
	uint32_t word1;
#define lpfc_acqe_link_fault_SHIFT	0
#define lpfc_acqe_link_fault_MASK	0x000000FF
#define lpfc_acqe_link_fault_WORD	word1
#define LPFC_ASYNC_LINK_FAULT_NONE	0x0
#define LPFC_ASYNC_LINK_FAULT_LOCAL	0x1
#define LPFC_ASYNC_LINK_FAULT_REMOTE	0x2
#define lpfc_acqe_logical_link_speed_SHIFT	16
#define lpfc_acqe_logical_link_speed_MASK	0x0000FFFF
#define lpfc_acqe_logical_link_speed_WORD	word1
	uint32_t event_tag;
	uint32_t trailer;
#define LPFC_LINK_EVENT_TYPE_PHYSICAL	0x0
#define LPFC_LINK_EVENT_TYPE_VIRTUAL	0x1
};

struct lpfc_acqe_fip {
	uint32_t index;
	uint32_t word1;
#define lpfc_acqe_fip_fcf_count_SHIFT		0
#define lpfc_acqe_fip_fcf_count_MASK		0x0000FFFF
#define lpfc_acqe_fip_fcf_count_WORD		word1
#define lpfc_acqe_fip_event_type_SHIFT		16
#define lpfc_acqe_fip_event_type_MASK		0x0000FFFF
#define lpfc_acqe_fip_event_type_WORD		word1
	uint32_t event_tag;
	uint32_t trailer;
#define LPFC_FIP_EVENT_TYPE_NEW_FCF		0x1
#define LPFC_FIP_EVENT_TYPE_FCF_TABLE_FULL	0x2
#define LPFC_FIP_EVENT_TYPE_FCF_DEAD		0x3
#define LPFC_FIP_EVENT_TYPE_CVL			0x4
#define LPFC_FIP_EVENT_TYPE_FCF_PARAM_MOD	0x5
};

struct lpfc_acqe_dcbx {
	uint32_t tlv_ttl;
	uint32_t reserved;
	uint32_t event_tag;
	uint32_t trailer;
};

struct lpfc_acqe_grp5 {
	uint32_t word0;
#define lpfc_acqe_grp5_type_SHIFT		6
#define lpfc_acqe_grp5_type_MASK		0x00000003
#define lpfc_acqe_grp5_type_WORD		word0
#define lpfc_acqe_grp5_number_SHIFT		0
#define lpfc_acqe_grp5_number_MASK		0x0000003F
#define lpfc_acqe_grp5_number_WORD		word0
	uint32_t word1;
#define lpfc_acqe_grp5_llink_spd_SHIFT	16
#define lpfc_acqe_grp5_llink_spd_MASK	0x0000FFFF
#define lpfc_acqe_grp5_llink_spd_WORD	word1
	uint32_t event_tag;
	uint32_t trailer;
};

struct lpfc_acqe_fc_la {
	uint32_t word0;
#define lpfc_acqe_fc_la_speed_SHIFT		24
#define lpfc_acqe_fc_la_speed_MASK		0x000000FF
#define lpfc_acqe_fc_la_speed_WORD		word0
#define LPFC_FC_LA_SPEED_UNKNOWN		0x0
#define LPFC_FC_LA_SPEED_1G		0x1
#define LPFC_FC_LA_SPEED_2G		0x2
#define LPFC_FC_LA_SPEED_4G		0x4
#define LPFC_FC_LA_SPEED_8G		0x8
#define LPFC_FC_LA_SPEED_10G		0xA
#define LPFC_FC_LA_SPEED_16G		0x10
#define LPFC_FC_LA_SPEED_32G            0x20
#define lpfc_acqe_fc_la_topology_SHIFT		16
#define lpfc_acqe_fc_la_topology_MASK		0x000000FF
#define lpfc_acqe_fc_la_topology_WORD		word0
#define LPFC_FC_LA_TOP_UNKOWN		0x0
#define LPFC_FC_LA_TOP_P2P		0x1
#define LPFC_FC_LA_TOP_FCAL		0x2
#define LPFC_FC_LA_TOP_INTERNAL_LOOP	0x3
#define LPFC_FC_LA_TOP_SERDES_LOOP	0x4
#define lpfc_acqe_fc_la_att_type_SHIFT		8
#define lpfc_acqe_fc_la_att_type_MASK		0x000000FF
#define lpfc_acqe_fc_la_att_type_WORD		word0
#define LPFC_FC_LA_TYPE_LINK_UP		0x1
#define LPFC_FC_LA_TYPE_LINK_DOWN	0x2
#define LPFC_FC_LA_TYPE_NO_HARD_ALPA	0x3
#define LPFC_FC_LA_TYPE_MDS_LINK_DOWN	0x4
#define LPFC_FC_LA_TYPE_MDS_LOOPBACK	0x5
#define lpfc_acqe_fc_la_port_type_SHIFT		6
#define lpfc_acqe_fc_la_port_type_MASK		0x00000003
#define lpfc_acqe_fc_la_port_type_WORD		word0
#define LPFC_LINK_TYPE_ETHERNET		0x0
#define LPFC_LINK_TYPE_FC		0x1
#define lpfc_acqe_fc_la_port_number_SHIFT	0
#define lpfc_acqe_fc_la_port_number_MASK	0x0000003F
#define lpfc_acqe_fc_la_port_number_WORD	word0
	uint32_t word1;
#define lpfc_acqe_fc_la_llink_spd_SHIFT		16
#define lpfc_acqe_fc_la_llink_spd_MASK		0x0000FFFF
#define lpfc_acqe_fc_la_llink_spd_WORD		word1
#define lpfc_acqe_fc_la_fault_SHIFT		0
#define lpfc_acqe_fc_la_fault_MASK		0x000000FF
#define lpfc_acqe_fc_la_fault_WORD		word1
#define LPFC_FC_LA_FAULT_NONE		0x0
#define LPFC_FC_LA_FAULT_LOCAL		0x1
#define LPFC_FC_LA_FAULT_REMOTE		0x2
	uint32_t event_tag;
	uint32_t trailer;
#define LPFC_FC_LA_EVENT_TYPE_FC_LINK		0x1
#define LPFC_FC_LA_EVENT_TYPE_SHARED_LINK	0x2
};

struct lpfc_acqe_misconfigured_event {
	struct {
	uint32_t word0;
#define lpfc_sli_misconfigured_port0_state_SHIFT	0
#define lpfc_sli_misconfigured_port0_state_MASK		0x000000FF
#define lpfc_sli_misconfigured_port0_state_WORD		word0
#define lpfc_sli_misconfigured_port1_state_SHIFT	8
#define lpfc_sli_misconfigured_port1_state_MASK		0x000000FF
#define lpfc_sli_misconfigured_port1_state_WORD		word0
#define lpfc_sli_misconfigured_port2_state_SHIFT	16
#define lpfc_sli_misconfigured_port2_state_MASK		0x000000FF
#define lpfc_sli_misconfigured_port2_state_WORD		word0
#define lpfc_sli_misconfigured_port3_state_SHIFT	24
#define lpfc_sli_misconfigured_port3_state_MASK		0x000000FF
#define lpfc_sli_misconfigured_port3_state_WORD		word0
	uint32_t word1;
#define lpfc_sli_misconfigured_port0_op_SHIFT		0
#define lpfc_sli_misconfigured_port0_op_MASK		0x00000001
#define lpfc_sli_misconfigured_port0_op_WORD		word1
#define lpfc_sli_misconfigured_port0_severity_SHIFT	1
#define lpfc_sli_misconfigured_port0_severity_MASK	0x00000003
#define lpfc_sli_misconfigured_port0_severity_WORD	word1
#define lpfc_sli_misconfigured_port1_op_SHIFT		8
#define lpfc_sli_misconfigured_port1_op_MASK		0x00000001
#define lpfc_sli_misconfigured_port1_op_WORD		word1
#define lpfc_sli_misconfigured_port1_severity_SHIFT	9
#define lpfc_sli_misconfigured_port1_severity_MASK	0x00000003
#define lpfc_sli_misconfigured_port1_severity_WORD	word1
#define lpfc_sli_misconfigured_port2_op_SHIFT		16
#define lpfc_sli_misconfigured_port2_op_MASK		0x00000001
#define lpfc_sli_misconfigured_port2_op_WORD		word1
#define lpfc_sli_misconfigured_port2_severity_SHIFT	17
#define lpfc_sli_misconfigured_port2_severity_MASK	0x00000003
#define lpfc_sli_misconfigured_port2_severity_WORD	word1
#define lpfc_sli_misconfigured_port3_op_SHIFT		24
#define lpfc_sli_misconfigured_port3_op_MASK		0x00000001
#define lpfc_sli_misconfigured_port3_op_WORD		word1
#define lpfc_sli_misconfigured_port3_severity_SHIFT	25
#define lpfc_sli_misconfigured_port3_severity_MASK	0x00000003
#define lpfc_sli_misconfigured_port3_severity_WORD	word1
	} theEvent;
#define LPFC_SLI_EVENT_STATUS_VALID			0x00
#define LPFC_SLI_EVENT_STATUS_NOT_PRESENT	0x01
#define LPFC_SLI_EVENT_STATUS_WRONG_TYPE	0x02
#define LPFC_SLI_EVENT_STATUS_UNSUPPORTED	0x03
#define LPFC_SLI_EVENT_STATUS_UNQUALIFIED	0x04
#define LPFC_SLI_EVENT_STATUS_UNCERTIFIED	0x05
};

struct lpfc_acqe_sli {
	uint32_t event_data1;
	uint32_t event_data2;
	uint32_t reserved;
	uint32_t trailer;
#define LPFC_SLI_EVENT_TYPE_PORT_ERROR		0x1
#define LPFC_SLI_EVENT_TYPE_OVER_TEMP		0x2
#define LPFC_SLI_EVENT_TYPE_NORM_TEMP		0x3
#define LPFC_SLI_EVENT_TYPE_NVLOG_POST		0x4
#define LPFC_SLI_EVENT_TYPE_DIAG_DUMP		0x5
#define LPFC_SLI_EVENT_TYPE_MISCONFIGURED	0x9
#define LPFC_SLI_EVENT_TYPE_REMOTE_DPORT	0xA
};

/*
 * Define the bootstrap mailbox (bmbx) region used to communicate
 * mailbox command between the host and port. The mailbox consists
 * of a payload area of 256 bytes and a completion queue of length
 * 16 bytes.
 */
struct lpfc_bmbx_create {
	struct lpfc_mqe mqe;
	struct lpfc_mcqe mcqe;
};

#define SGL_ALIGN_SZ 64
#define SGL_PAGE_SIZE 4096
/* align SGL addr on a size boundary - adjust address up */
#define NO_XRI  0xffff

struct wqe_common {
	uint32_t word6;
#define wqe_xri_tag_SHIFT     0
#define wqe_xri_tag_MASK      0x0000FFFF
#define wqe_xri_tag_WORD      word6
#define wqe_ctxt_tag_SHIFT    16
#define wqe_ctxt_tag_MASK     0x0000FFFF
#define wqe_ctxt_tag_WORD     word6
	uint32_t word7;
#define wqe_dif_SHIFT         0
#define wqe_dif_MASK          0x00000003
#define wqe_dif_WORD          word7
#define LPFC_WQE_DIF_PASSTHRU	1
#define LPFC_WQE_DIF_STRIP	2
#define LPFC_WQE_DIF_INSERT	3
#define wqe_ct_SHIFT          2
#define wqe_ct_MASK           0x00000003
#define wqe_ct_WORD           word7
#define wqe_status_SHIFT      4
#define wqe_status_MASK       0x0000000f
#define wqe_status_WORD       word7
#define wqe_cmnd_SHIFT        8
#define wqe_cmnd_MASK         0x000000ff
#define wqe_cmnd_WORD         word7
#define wqe_class_SHIFT       16
#define wqe_class_MASK        0x00000007
#define wqe_class_WORD        word7
#define wqe_ar_SHIFT          19
#define wqe_ar_MASK           0x00000001
#define wqe_ar_WORD           word7
#define wqe_ag_SHIFT          wqe_ar_SHIFT
#define wqe_ag_MASK           wqe_ar_MASK
#define wqe_ag_WORD           wqe_ar_WORD
#define wqe_pu_SHIFT          20
#define wqe_pu_MASK           0x00000003
#define wqe_pu_WORD           word7
#define wqe_erp_SHIFT         22
#define wqe_erp_MASK          0x00000001
#define wqe_erp_WORD          word7
#define wqe_conf_SHIFT        wqe_erp_SHIFT
#define wqe_conf_MASK         wqe_erp_MASK
#define wqe_conf_WORD         wqe_erp_WORD
#define wqe_lnk_SHIFT         23
#define wqe_lnk_MASK          0x00000001
#define wqe_lnk_WORD          word7
#define wqe_tmo_SHIFT         24
#define wqe_tmo_MASK          0x000000ff
#define wqe_tmo_WORD          word7
	uint32_t abort_tag; /* word 8 in WQE */
	uint32_t word9;
#define wqe_reqtag_SHIFT      0
#define wqe_reqtag_MASK       0x0000FFFF
#define wqe_reqtag_WORD       word9
#define wqe_temp_rpi_SHIFT    16
#define wqe_temp_rpi_MASK     0x0000FFFF
#define wqe_temp_rpi_WORD     word9
#define wqe_rcvoxid_SHIFT     16
#define wqe_rcvoxid_MASK      0x0000FFFF
#define wqe_rcvoxid_WORD      word9
	uint32_t word10;
#define wqe_ebde_cnt_SHIFT    0
#define wqe_ebde_cnt_MASK     0x0000000f
#define wqe_ebde_cnt_WORD     word10
#define wqe_nvme_SHIFT        4
#define wqe_nvme_MASK         0x00000001
#define wqe_nvme_WORD         word10
#define wqe_oas_SHIFT         6
#define wqe_oas_MASK          0x00000001
#define wqe_oas_WORD          word10
#define wqe_lenloc_SHIFT      7
#define wqe_lenloc_MASK       0x00000003
#define wqe_lenloc_WORD       word10
#define LPFC_WQE_LENLOC_NONE		0
#define LPFC_WQE_LENLOC_WORD3	1
#define LPFC_WQE_LENLOC_WORD12	2
#define LPFC_WQE_LENLOC_WORD4	3
#define wqe_qosd_SHIFT        9
#define wqe_qosd_MASK         0x00000001
#define wqe_qosd_WORD         word10
#define wqe_xbl_SHIFT         11
#define wqe_xbl_MASK          0x00000001
#define wqe_xbl_WORD          word10
#define wqe_iod_SHIFT         13
#define wqe_iod_MASK          0x00000001
#define wqe_iod_WORD          word10
#define LPFC_WQE_IOD_WRITE	0
#define LPFC_WQE_IOD_READ	1
#define wqe_dbde_SHIFT        14
#define wqe_dbde_MASK         0x00000001
#define wqe_dbde_WORD         word10
#define wqe_wqes_SHIFT        15
#define wqe_wqes_MASK         0x00000001
#define wqe_wqes_WORD         word10
/* Note that this field overlaps above fields */
#define wqe_wqid_SHIFT        1
#define wqe_wqid_MASK         0x00007fff
#define wqe_wqid_WORD         word10
#define wqe_pri_SHIFT         16
#define wqe_pri_MASK          0x00000007
#define wqe_pri_WORD          word10
#define wqe_pv_SHIFT          19
#define wqe_pv_MASK           0x00000001
#define wqe_pv_WORD           word10
#define wqe_xc_SHIFT          21
#define wqe_xc_MASK           0x00000001
#define wqe_xc_WORD           word10
#define wqe_sr_SHIFT          22
#define wqe_sr_MASK           0x00000001
#define wqe_sr_WORD           word10
#define wqe_ccpe_SHIFT        23
#define wqe_ccpe_MASK         0x00000001
#define wqe_ccpe_WORD         word10
#define wqe_ccp_SHIFT         24
#define wqe_ccp_MASK          0x000000ff
#define wqe_ccp_WORD          word10
	uint32_t word11;
#define wqe_cmd_type_SHIFT    0
#define wqe_cmd_type_MASK     0x0000000f
#define wqe_cmd_type_WORD     word11
#define wqe_els_id_SHIFT      4
#define wqe_els_id_MASK       0x00000003
#define wqe_els_id_WORD       word11
#define LPFC_ELS_ID_FLOGI	3
#define LPFC_ELS_ID_FDISC	2
#define LPFC_ELS_ID_LOGO	1
#define LPFC_ELS_ID_DEFAULT	0
#define wqe_irsp_SHIFT        4
#define wqe_irsp_MASK         0x00000001
#define wqe_irsp_WORD         word11
#define wqe_sup_SHIFT         6
#define wqe_sup_MASK          0x00000001
#define wqe_sup_WORD          word11
#define wqe_wqec_SHIFT        7
#define wqe_wqec_MASK         0x00000001
#define wqe_wqec_WORD         word11
#define wqe_irsplen_SHIFT     8
#define wqe_irsplen_MASK      0x0000000f
#define wqe_irsplen_WORD      word11
#define wqe_cqid_SHIFT        16
#define wqe_cqid_MASK         0x0000ffff
#define wqe_cqid_WORD         word11
#define LPFC_WQE_CQ_ID_DEFAULT	0xffff
};

struct wqe_did {
	uint32_t word5;
#define wqe_els_did_SHIFT         0
#define wqe_els_did_MASK          0x00FFFFFF
#define wqe_els_did_WORD          word5
#define wqe_xmit_bls_pt_SHIFT         28
#define wqe_xmit_bls_pt_MASK          0x00000003
#define wqe_xmit_bls_pt_WORD          word5
#define wqe_xmit_bls_ar_SHIFT         30
#define wqe_xmit_bls_ar_MASK          0x00000001
#define wqe_xmit_bls_ar_WORD          word5
#define wqe_xmit_bls_xo_SHIFT         31
#define wqe_xmit_bls_xo_MASK          0x00000001
#define wqe_xmit_bls_xo_WORD          word5
};

struct lpfc_wqe_generic{
	struct ulp_bde64 bde;
	uint32_t word3;
	uint32_t word4;
	uint32_t word5;
	struct wqe_common wqe_com;
	uint32_t payload[4];
};

struct els_request64_wqe {
	struct ulp_bde64 bde;
	uint32_t payload_len;
	uint32_t word4;
#define els_req64_sid_SHIFT         0
#define els_req64_sid_MASK          0x00FFFFFF
#define els_req64_sid_WORD          word4
#define els_req64_sp_SHIFT          24
#define els_req64_sp_MASK           0x00000001
#define els_req64_sp_WORD           word4
#define els_req64_vf_SHIFT          25
#define els_req64_vf_MASK           0x00000001
#define els_req64_vf_WORD           word4
	struct wqe_did	wqe_dest;
	struct wqe_common wqe_com; /* words 6-11 */
	uint32_t word12;
#define els_req64_vfid_SHIFT        1
#define els_req64_vfid_MASK         0x00000FFF
#define els_req64_vfid_WORD         word12
#define els_req64_pri_SHIFT         13
#define els_req64_pri_MASK          0x00000007
#define els_req64_pri_WORD          word12
	uint32_t word13;
#define els_req64_hopcnt_SHIFT      24
#define els_req64_hopcnt_MASK       0x000000ff
#define els_req64_hopcnt_WORD       word13
	uint32_t word14;
	uint32_t max_response_payload_len;
};

struct xmit_els_rsp64_wqe {
	struct ulp_bde64 bde;
	uint32_t response_payload_len;
	uint32_t word4;
#define els_rsp64_sid_SHIFT         0
#define els_rsp64_sid_MASK          0x00FFFFFF
#define els_rsp64_sid_WORD          word4
#define els_rsp64_sp_SHIFT          24
#define els_rsp64_sp_MASK           0x00000001
#define els_rsp64_sp_WORD           word4
	struct wqe_did wqe_dest;
	struct wqe_common wqe_com; /* words 6-11 */
	uint32_t word12;
#define wqe_rsp_temp_rpi_SHIFT    0
#define wqe_rsp_temp_rpi_MASK     0x0000FFFF
#define wqe_rsp_temp_rpi_WORD     word12
	uint32_t rsvd_13_15[3];
};

struct xmit_bls_rsp64_wqe {
	uint32_t payload0;
/* Payload0 for BA_ACC */
#define xmit_bls_rsp64_acc_seq_id_SHIFT        16
#define xmit_bls_rsp64_acc_seq_id_MASK         0x000000ff
#define xmit_bls_rsp64_acc_seq_id_WORD         payload0
#define xmit_bls_rsp64_acc_seq_id_vald_SHIFT   24
#define xmit_bls_rsp64_acc_seq_id_vald_MASK    0x000000ff
#define xmit_bls_rsp64_acc_seq_id_vald_WORD    payload0
/* Payload0 for BA_RJT */
#define xmit_bls_rsp64_rjt_vspec_SHIFT   0
#define xmit_bls_rsp64_rjt_vspec_MASK    0x000000ff
#define xmit_bls_rsp64_rjt_vspec_WORD    payload0
#define xmit_bls_rsp64_rjt_expc_SHIFT    8
#define xmit_bls_rsp64_rjt_expc_MASK     0x000000ff
#define xmit_bls_rsp64_rjt_expc_WORD     payload0
#define xmit_bls_rsp64_rjt_rsnc_SHIFT    16
#define xmit_bls_rsp64_rjt_rsnc_MASK     0x000000ff
#define xmit_bls_rsp64_rjt_rsnc_WORD     payload0
	uint32_t word1;
#define xmit_bls_rsp64_rxid_SHIFT  0
#define xmit_bls_rsp64_rxid_MASK   0x0000ffff
#define xmit_bls_rsp64_rxid_WORD   word1
#define xmit_bls_rsp64_oxid_SHIFT  16
#define xmit_bls_rsp64_oxid_MASK   0x0000ffff
#define xmit_bls_rsp64_oxid_WORD   word1
	uint32_t word2;
#define xmit_bls_rsp64_seqcnthi_SHIFT  0
#define xmit_bls_rsp64_seqcnthi_MASK   0x0000ffff
#define xmit_bls_rsp64_seqcnthi_WORD   word2
#define xmit_bls_rsp64_seqcntlo_SHIFT  16
#define xmit_bls_rsp64_seqcntlo_MASK   0x0000ffff
#define xmit_bls_rsp64_seqcntlo_WORD   word2
	uint32_t rsrvd3;
	uint32_t rsrvd4;
	struct wqe_did	wqe_dest;
	struct wqe_common wqe_com; /* words 6-11 */
	uint32_t word12;
#define xmit_bls_rsp64_temprpi_SHIFT  0
#define xmit_bls_rsp64_temprpi_MASK   0x0000ffff
#define xmit_bls_rsp64_temprpi_WORD   word12
	uint32_t rsvd_13_15[3];
};

struct wqe_rctl_dfctl {
	uint32_t word5;
#define wqe_si_SHIFT 2
#define wqe_si_MASK  0x000000001
#define wqe_si_WORD  word5
#define wqe_la_SHIFT 3
#define wqe_la_MASK  0x000000001
#define wqe_la_WORD  word5
#define wqe_xo_SHIFT	6
#define wqe_xo_MASK	0x000000001
#define wqe_xo_WORD	word5
#define wqe_ls_SHIFT 7
#define wqe_ls_MASK  0x000000001
#define wqe_ls_WORD  word5
#define wqe_dfctl_SHIFT 8
#define wqe_dfctl_MASK  0x0000000ff
#define wqe_dfctl_WORD  word5
#define wqe_type_SHIFT 16
#define wqe_type_MASK  0x0000000ff
#define wqe_type_WORD  word5
#define wqe_rctl_SHIFT 24
#define wqe_rctl_MASK  0x0000000ff
#define wqe_rctl_WORD  word5
};

struct xmit_seq64_wqe {
	struct ulp_bde64 bde;
	uint32_t rsvd3;
	uint32_t relative_offset;
	struct wqe_rctl_dfctl wge_ctl;
	struct wqe_common wqe_com; /* words 6-11 */
	uint32_t xmit_len;
	uint32_t rsvd_12_15[3];
};
struct xmit_bcast64_wqe {
	struct ulp_bde64 bde;
	uint32_t seq_payload_len;
	uint32_t rsvd4;
	struct wqe_rctl_dfctl wge_ctl; /* word 5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];
};

struct gen_req64_wqe {
	struct ulp_bde64 bde;
	uint32_t request_payload_len;
	uint32_t relative_offset;
	struct wqe_rctl_dfctl wge_ctl; /* word 5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_14[3];
	uint32_t max_response_payload_len;
};

/* Define NVME PRLI request to fabric. NVME is a
 * fabric-only protocol.
 * Updated to red-lined v1.08 on Sept 16, 2016
 */
struct lpfc_nvme_prli {
	uint32_t word1;
	/* The Response Code is defined in the FCP PRLI lpfc_hw.h */
#define prli_acc_rsp_code_SHIFT         8
#define prli_acc_rsp_code_MASK          0x0000000f
#define prli_acc_rsp_code_WORD          word1
#define prli_estabImagePair_SHIFT       13
#define prli_estabImagePair_MASK        0x00000001
#define prli_estabImagePair_WORD        word1
#define prli_type_code_ext_SHIFT        16
#define prli_type_code_ext_MASK         0x000000ff
#define prli_type_code_ext_WORD         word1
#define prli_type_code_SHIFT            24
#define prli_type_code_MASK             0x000000ff
#define prli_type_code_WORD             word1
	uint32_t word_rsvd2;
	uint32_t word_rsvd3;
	uint32_t word4;
#define prli_fba_SHIFT                  0
#define prli_fba_MASK                   0x00000001
#define prli_fba_WORD                   word4
#define prli_disc_SHIFT                 3
#define prli_disc_MASK                  0x00000001
#define prli_disc_WORD                  word4
#define prli_tgt_SHIFT                  4
#define prli_tgt_MASK                   0x00000001
#define prli_tgt_WORD                   word4
#define prli_init_SHIFT                 5
#define prli_init_MASK                  0x00000001
#define prli_init_WORD                  word4
#define prli_recov_SHIFT                8
#define prli_recov_MASK                 0x00000001
#define prli_recov_WORD                 word4
	uint32_t word5;
#define prli_fb_sz_SHIFT                0
#define prli_fb_sz_MASK                 0x0000ffff
#define prli_fb_sz_WORD                 word5
#define LPFC_NVMET_FB_SZ_MAX  65536   /* Driver target mode only. */
};

struct create_xri_wqe {
	uint32_t rsrvd[5];           /* words 0-4 */
	struct wqe_did	wqe_dest;  /* word 5 */
	struct wqe_common wqe_com; /* words 6-11 */
	uint32_t rsvd_12_15[4];         /* word 12-15 */
};

#define T_REQUEST_TAG 3
#define T_XRI_TAG 1

struct abort_cmd_wqe {
	uint32_t rsrvd[3];
	uint32_t word3;
#define	abort_cmd_ia_SHIFT  0
#define	abort_cmd_ia_MASK  0x000000001
#define	abort_cmd_ia_WORD  word3
#define	abort_cmd_criteria_SHIFT  8
#define	abort_cmd_criteria_MASK  0x0000000ff
#define	abort_cmd_criteria_WORD  word3
	uint32_t rsrvd4;
	uint32_t rsrvd5;
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];         /* word 12-15 */
};

struct fcp_iwrite64_wqe {
	struct ulp_bde64 bde;
	uint32_t word3;
#define	cmd_buff_len_SHIFT  16
#define	cmd_buff_len_MASK  0x00000ffff
#define	cmd_buff_len_WORD  word3
#define payload_offset_len_SHIFT 0
#define payload_offset_len_MASK 0x0000ffff
#define payload_offset_len_WORD word3
	uint32_t total_xfer_len;
	uint32_t initial_xfer_len;
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsrvd12;
	struct ulp_bde64 ph_bde;       /* words 13-15 */
};

struct fcp_iread64_wqe {
	struct ulp_bde64 bde;
	uint32_t word3;
#define	cmd_buff_len_SHIFT  16
#define	cmd_buff_len_MASK  0x00000ffff
#define	cmd_buff_len_WORD  word3
#define payload_offset_len_SHIFT 0
#define payload_offset_len_MASK 0x0000ffff
#define payload_offset_len_WORD word3
	uint32_t total_xfer_len;       /* word 4 */
	uint32_t rsrvd5;               /* word 5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsrvd12;
	struct ulp_bde64 ph_bde;       /* words 13-15 */
};

struct fcp_icmnd64_wqe {
	struct ulp_bde64 bde;          /* words 0-2 */
	uint32_t word3;
#define	cmd_buff_len_SHIFT  16
#define	cmd_buff_len_MASK  0x00000ffff
#define	cmd_buff_len_WORD  word3
#define payload_offset_len_SHIFT 0
#define payload_offset_len_MASK 0x0000ffff
#define payload_offset_len_WORD word3
	uint32_t rsrvd4;               /* word 4 */
	uint32_t rsrvd5;               /* word 5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];        /* word 12-15 */
};

struct fcp_trsp64_wqe {
	struct ulp_bde64 bde;
	uint32_t response_len;
	uint32_t rsvd_4_5[2];
	struct wqe_common wqe_com;      /* words 6-11 */
	uint32_t rsvd_12_15[4];         /* word 12-15 */
};

struct fcp_tsend64_wqe {
	struct ulp_bde64 bde;
	uint32_t payload_offset_len;
	uint32_t relative_offset;
	uint32_t reserved;
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t fcp_data_len;         /* word 12 */
	uint32_t rsvd_13_15[3];        /* word 13-15 */
};

struct fcp_treceive64_wqe {
	struct ulp_bde64 bde;
	uint32_t payload_offset_len;
	uint32_t relative_offset;
	uint32_t reserved;
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t fcp_data_len;         /* word 12 */
	uint32_t rsvd_13_15[3];        /* word 13-15 */
};
#define TXRDY_PAYLOAD_LEN      12


union lpfc_wqe {
	uint32_t words[16];
	struct lpfc_wqe_generic generic;
	struct fcp_icmnd64_wqe fcp_icmd;
	struct fcp_iread64_wqe fcp_iread;
	struct fcp_iwrite64_wqe fcp_iwrite;
	struct abort_cmd_wqe abort_cmd;
	struct create_xri_wqe create_xri;
	struct xmit_bcast64_wqe xmit_bcast64;
	struct xmit_seq64_wqe xmit_sequence;
	struct xmit_bls_rsp64_wqe xmit_bls_rsp;
	struct xmit_els_rsp64_wqe xmit_els_rsp;
	struct els_request64_wqe els_req;
	struct gen_req64_wqe gen_req;
	struct fcp_trsp64_wqe fcp_trsp;
	struct fcp_tsend64_wqe fcp_tsend;
	struct fcp_treceive64_wqe fcp_treceive;

};

union lpfc_wqe128 {
	uint32_t words[32];
	struct lpfc_wqe_generic generic;
	struct fcp_icmnd64_wqe fcp_icmd;
	struct fcp_iread64_wqe fcp_iread;
	struct fcp_iwrite64_wqe fcp_iwrite;
	struct fcp_trsp64_wqe fcp_trsp;
	struct fcp_tsend64_wqe fcp_tsend;
	struct fcp_treceive64_wqe fcp_treceive;
	struct xmit_seq64_wqe xmit_sequence;
	struct gen_req64_wqe gen_req;
};

#define LPFC_GROUP_OJECT_MAGIC_G5		0xfeaa0001
#define LPFC_GROUP_OJECT_MAGIC_G6		0xfeaa0003
#define LPFC_FILE_TYPE_GROUP			0xf7
#define LPFC_FILE_ID_GROUP			0xa2
struct lpfc_grp_hdr {
	uint32_t size;
	uint32_t magic_number;
	uint32_t word2;
#define lpfc_grp_hdr_file_type_SHIFT	24
#define lpfc_grp_hdr_file_type_MASK	0x000000FF
#define lpfc_grp_hdr_file_type_WORD	word2
#define lpfc_grp_hdr_id_SHIFT		16
#define lpfc_grp_hdr_id_MASK		0x000000FF
#define lpfc_grp_hdr_id_WORD		word2
	uint8_t rev_name[128];
	uint8_t date[12];
	uint8_t revision[32];
};

/* Defines for WQE command type */
#define FCP_COMMAND		0x0
#define NVME_READ_CMD		0x0
#define FCP_COMMAND_DATA_OUT	0x1
#define NVME_WRITE_CMD		0x1
#define FCP_COMMAND_TRECEIVE	0x2
#define FCP_COMMAND_TRSP	0x3
#define FCP_COMMAND_TSEND	0x7
#define OTHER_COMMAND		0x8
#define ELS_COMMAND_NON_FIP	0xC
#define ELS_COMMAND_FIP		0xD

#define LPFC_NVME_EMBED_CMD	0x0
#define LPFC_NVME_EMBED_WRITE	0x1
#define LPFC_NVME_EMBED_READ	0x2

/* WQE Commands */
#define CMD_ABORT_XRI_WQE       0x0F
#define CMD_XMIT_SEQUENCE64_WQE 0x82
#define CMD_XMIT_BCAST64_WQE    0x84
#define CMD_ELS_REQUEST64_WQE   0x8A
#define CMD_XMIT_ELS_RSP64_WQE  0x95
#define CMD_XMIT_BLS_RSP64_WQE  0x97
#define CMD_FCP_IWRITE64_WQE    0x98
#define CMD_FCP_IREAD64_WQE     0x9A
#define CMD_FCP_ICMND64_WQE     0x9C
#define CMD_FCP_TSEND64_WQE     0x9F
#define CMD_FCP_TRECEIVE64_WQE  0xA1
#define CMD_FCP_TRSP64_WQE      0xA3
#define CMD_GEN_REQUEST64_WQE   0xC2

#define CMD_WQE_MASK            0xff


#define LPFC_FW_DUMP	1
#define LPFC_FW_RESET	2
#define LPFC_DV_RESET	3
