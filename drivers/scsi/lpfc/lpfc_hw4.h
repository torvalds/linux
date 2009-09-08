/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2009 Emulex.  All rights reserved.                *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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
#define bf_get(name, ptr) \
	(((ptr)->name##_WORD >> name##_SHIFT) & name##_MASK)
#define bf_set(name, ptr, value) \
	((ptr)->name##_WORD = ((((value) & name##_MASK) << name##_SHIFT) | \
		 ((ptr)->name##_WORD & ~(name##_MASK << name##_SHIFT))))

struct dma_address {
	uint32_t addr_lo;
	uint32_t addr_hi;
};

#define LPFC_SLI4_BAR0		1
#define LPFC_SLI4_BAR1		2
#define LPFC_SLI4_BAR2		4

#define LPFC_SLI4_MBX_EMBED	true
#define LPFC_SLI4_MBX_NEMBED	false

#define LPFC_SLI4_MB_WORD_COUNT		64
#define LPFC_MAX_MQ_PAGE		8
#define LPFC_MAX_WQ_PAGE		8
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

/* Active interrupt test count */
#define LPFC_ACT_INTR_CNT	4

/* Delay Multiplier constant */
#define LPFC_DMULT_CONST       651042
#define LPFC_MIM_IMAX          636
#define LPFC_FP_DEF_IMAX       10000
#define LPFC_SP_DEF_IMAX       10000

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

struct lpfc_sli4_flags {
	uint32_t word0;
#define lpfc_fip_flag_SHIFT 0
#define lpfc_fip_flag_MASK 0x00000001
#define lpfc_fip_flag_WORD word0
};

/* event queue entry structure */
struct lpfc_eqe {
	uint32_t word0;
#define lpfc_eqe_resource_id_SHIFT	16
#define lpfc_eqe_resource_id_MASK	0x000000FF
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

/* Status returned by hardware (valid only if status = CQE_STATUS_SUCCESS). */
#define CQE_HW_STATUS_NO_ERR		0x0
#define CQE_HW_STATUS_UNDERRUN		0x1
#define CQE_HW_STATUS_OVERRUN		0x2

/* Completion Queue Entry Codes */
#define CQE_CODE_COMPL_WQE		0x1
#define CQE_CODE_RELEASE_WQE		0x2
#define CQE_CODE_RECEIVE		0x4
#define CQE_CODE_XRI_ABORTED		0x5

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
	uint32_t total_data_placed;
	uint32_t parameter;
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
#define lpfc_wcqe_c_priority_MASK		0x00000007
#define lpfc_wcqe_c_priority_WORD		word3
#define lpfc_wcqe_c_code_SHIFT		lpfc_cqe_code_SHIFT
#define lpfc_wcqe_c_code_MASK		lpfc_cqe_code_MASK
#define lpfc_wcqe_c_code_WORD		lpfc_cqe_code_WORD
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
	uint32_t reserved1;
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

struct lpfc_wqe_generic{
	struct ulp_bde64 bde;
	uint32_t word3;
	uint32_t word4;
	uint32_t word5;
	uint32_t word6;
#define lpfc_wqe_gen_context_SHIFT	16
#define lpfc_wqe_gen_context_MASK	0x0000FFFF
#define lpfc_wqe_gen_context_WORD	word6
#define lpfc_wqe_gen_xri_SHIFT		0
#define lpfc_wqe_gen_xri_MASK		0x0000FFFF
#define lpfc_wqe_gen_xri_WORD		word6
	uint32_t word7;
#define lpfc_wqe_gen_lnk_SHIFT		23
#define lpfc_wqe_gen_lnk_MASK		0x00000001
#define lpfc_wqe_gen_lnk_WORD		word7
#define lpfc_wqe_gen_erp_SHIFT		22
#define lpfc_wqe_gen_erp_MASK		0x00000001
#define lpfc_wqe_gen_erp_WORD		word7
#define lpfc_wqe_gen_pu_SHIFT		20
#define lpfc_wqe_gen_pu_MASK		0x00000003
#define lpfc_wqe_gen_pu_WORD		word7
#define lpfc_wqe_gen_class_SHIFT	16
#define lpfc_wqe_gen_class_MASK		0x00000007
#define lpfc_wqe_gen_class_WORD		word7
#define lpfc_wqe_gen_command_SHIFT	8
#define lpfc_wqe_gen_command_MASK	0x000000FF
#define lpfc_wqe_gen_command_WORD	word7
#define lpfc_wqe_gen_status_SHIFT	4
#define lpfc_wqe_gen_status_MASK	0x0000000F
#define lpfc_wqe_gen_status_WORD	word7
#define lpfc_wqe_gen_ct_SHIFT		2
#define lpfc_wqe_gen_ct_MASK		0x00000007
#define lpfc_wqe_gen_ct_WORD		word7
	uint32_t abort_tag;
	uint32_t word9;
#define lpfc_wqe_gen_request_tag_SHIFT	0
#define lpfc_wqe_gen_request_tag_MASK	0x0000FFFF
#define lpfc_wqe_gen_request_tag_WORD	word9
	uint32_t word10;
#define lpfc_wqe_gen_ccp_SHIFT		24
#define lpfc_wqe_gen_ccp_MASK		0x000000FF
#define lpfc_wqe_gen_ccp_WORD		word10
#define lpfc_wqe_gen_ccpe_SHIFT		23
#define lpfc_wqe_gen_ccpe_MASK		0x00000001
#define lpfc_wqe_gen_ccpe_WORD		word10
#define lpfc_wqe_gen_pv_SHIFT		19
#define lpfc_wqe_gen_pv_MASK		0x00000001
#define lpfc_wqe_gen_pv_WORD		word10
#define lpfc_wqe_gen_pri_SHIFT		16
#define lpfc_wqe_gen_pri_MASK		0x00000007
#define lpfc_wqe_gen_pri_WORD		word10
	uint32_t word11;
#define lpfc_wqe_gen_cq_id_SHIFT	16
#define lpfc_wqe_gen_cq_id_MASK		0x0000FFFF
#define lpfc_wqe_gen_cq_id_WORD		word11
#define LPFC_WQE_CQ_ID_DEFAULT	0xffff
#define lpfc_wqe_gen_wqec_SHIFT		7
#define lpfc_wqe_gen_wqec_MASK		0x00000001
#define lpfc_wqe_gen_wqec_WORD		word11
#define lpfc_wqe_gen_cmd_type_SHIFT	0
#define lpfc_wqe_gen_cmd_type_MASK	0x0000000F
#define lpfc_wqe_gen_cmd_type_WORD	word11
	uint32_t payload[4];
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

#define LPFC_UERR_STATUS_HI		0x00A4
#define LPFC_UERR_STATUS_LO		0x00A0
#define LPFC_ONLINE0			0x00B0
#define LPFC_ONLINE1			0x00B4
#define LPFC_SCRATCHPAD			0x0058

/* BAR0 Registers */
#define LPFC_HST_STATE			0x00AC
#define lpfc_hst_state_perr_SHIFT	31
#define lpfc_hst_state_perr_MASK	0x1
#define lpfc_hst_state_perr_WORD	word0
#define lpfc_hst_state_sfi_SHIFT	30
#define lpfc_hst_state_sfi_MASK		0x1
#define lpfc_hst_state_sfi_WORD		word0
#define lpfc_hst_state_nip_SHIFT	29
#define lpfc_hst_state_nip_MASK		0x1
#define lpfc_hst_state_nip_WORD		word0
#define lpfc_hst_state_ipc_SHIFT	28
#define lpfc_hst_state_ipc_MASK		0x1
#define lpfc_hst_state_ipc_WORD		word0
#define lpfc_hst_state_xrom_SHIFT	27
#define lpfc_hst_state_xrom_MASK	0x1
#define lpfc_hst_state_xrom_WORD	word0
#define lpfc_hst_state_dl_SHIFT		26
#define lpfc_hst_state_dl_MASK		0x1
#define lpfc_hst_state_dl_WORD		word0
#define lpfc_hst_state_port_status_SHIFT	0
#define lpfc_hst_state_port_status_MASK		0xFFFF
#define lpfc_hst_state_port_status_WORD		word0

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
#define LPFC_POST_STAGE_ARMFW_READY			0xC000
#define LPFC_POST_STAGE_ARMFW_UE 			0xF000

#define lpfc_scratchpad_slirev_SHIFT			4
#define lpfc_scratchpad_slirev_MASK			0xF
#define lpfc_scratchpad_slirev_WORD			word0
#define lpfc_scratchpad_chiptype_SHIFT			8
#define lpfc_scratchpad_chiptype_MASK			0xFF
#define lpfc_scratchpad_chiptype_WORD			word0
#define lpfc_scratchpad_featurelevel1_SHIFT		16
#define lpfc_scratchpad_featurelevel1_MASK		0xFF
#define lpfc_scratchpad_featurelevel1_WORD		word0
#define lpfc_scratchpad_featurelevel2_SHIFT		24
#define lpfc_scratchpad_featurelevel2_MASK		0xFF
#define lpfc_scratchpad_featurelevel2_WORD		word0

/* BAR1 Registers */
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

/* BAR2 Registers */
#define LPFC_RQ_DOORBELL		0x00A0
#define lpfc_rq_doorbell_num_posted_SHIFT	16
#define lpfc_rq_doorbell_num_posted_MASK	0x3FFF
#define lpfc_rq_doorbell_num_posted_WORD	word0
#define LPFC_RQ_POST_BATCH		8	/* RQEs to post at one time */
#define lpfc_rq_doorbell_id_SHIFT		0
#define lpfc_rq_doorbell_id_MASK		0x03FF
#define lpfc_rq_doorbell_id_WORD		word0

#define LPFC_WQ_DOORBELL		0x0040
#define lpfc_wq_doorbell_num_posted_SHIFT	24
#define lpfc_wq_doorbell_num_posted_MASK	0x00FF
#define lpfc_wq_doorbell_num_posted_WORD	word0
#define lpfc_wq_doorbell_index_SHIFT		16
#define lpfc_wq_doorbell_index_MASK		0x00FF
#define lpfc_wq_doorbell_index_WORD		word0
#define lpfc_wq_doorbell_id_SHIFT		0
#define lpfc_wq_doorbell_id_MASK		0xFFFF
#define lpfc_wq_doorbell_id_WORD		word0

#define LPFC_EQCQ_DOORBELL		0x0120
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
#define lpfc_eqcq_doorbell_cqid_SHIFT		0
#define lpfc_eqcq_doorbell_cqid_MASK		0x03FF
#define lpfc_eqcq_doorbell_cqid_WORD		word0
#define lpfc_eqcq_doorbell_eqid_SHIFT		0
#define lpfc_eqcq_doorbell_eqid_MASK		0x01FF
#define lpfc_eqcq_doorbell_eqid_WORD		word0

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
#define lpfc_mq_doorbell_id_MASK		0x03FF
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
#define lpfc_mbox_hdr_opcode_SHIFT		0
#define lpfc_mbox_hdr_opcode_MASK		0x000000FF
#define lpfc_mbox_hdr_opcode_WORD		word6
#define lpfc_mbox_hdr_subsystem_SHIFT		8
#define lpfc_mbox_hdr_subsystem_MASK		0x000000FF
#define lpfc_mbox_hdr_subsystem_WORD		word6
#define lpfc_mbox_hdr_port_number_SHIFT		16
#define lpfc_mbox_hdr_port_number_MASK		0x000000FF
#define lpfc_mbox_hdr_port_number_WORD		word6
#define lpfc_mbox_hdr_domain_SHIFT		24
#define lpfc_mbox_hdr_domain_MASK		0x000000FF
#define lpfc_mbox_hdr_domain_WORD		word6
		uint32_t timeout;
		uint32_t request_length;
		uint32_t reserved9;
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

/* Mailbox structures */
struct mbox_header {
	struct lpfc_sli4_cfg_mhdr cfg_mhdr;
	union  lpfc_sli4_cfg_shdr cfg_shdr;
};

/* Subsystem Definitions */
#define LPFC_MBOX_SUBSYSTEM_COMMON	0x1
#define LPFC_MBOX_SUBSYSTEM_FCOE	0xC

/* Device Specific Definitions */

/* The HOST ENDIAN defines are in Big Endian format. */
#define HOST_ENDIAN_LOW_WORD0   0xFF3412FF
#define HOST_ENDIAN_HIGH_WORD1	0xFF7856FF

/* Common Opcodes */
#define LPFC_MBOX_OPCODE_CQ_CREATE		0x0C
#define LPFC_MBOX_OPCODE_EQ_CREATE		0x0D
#define LPFC_MBOX_OPCODE_MQ_CREATE		0x15
#define LPFC_MBOX_OPCODE_GET_CNTL_ATTRIBUTES	0x20
#define LPFC_MBOX_OPCODE_NOP			0x21
#define LPFC_MBOX_OPCODE_MQ_DESTROY		0x35
#define LPFC_MBOX_OPCODE_CQ_DESTROY		0x36
#define LPFC_MBOX_OPCODE_EQ_DESTROY		0x37
#define LPFC_MBOX_OPCODE_FUNCTION_RESET		0x3D

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
#define lpfc_cq_eq_id_SHIFT		22
#define lpfc_cq_eq_id_MASK		0x000000FF
#define lpfc_cq_eq_id_WORD		word1
	uint32_t reserved0;
	uint32_t reserved1;
};

struct lpfc_mbx_cq_create {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
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
		struct {
			uint32_t word0;
#define lpfc_mbx_wq_create_num_pages_SHIFT	0
#define lpfc_mbx_wq_create_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_wq_create_num_pages_WORD	word0
#define lpfc_mbx_wq_create_cq_id_SHIFT		16
#define lpfc_mbx_wq_create_cq_id_MASK		0x0000FFFF
#define lpfc_mbx_wq_create_cq_id_WORD		word0
			struct dma_address page[LPFC_MAX_WQ_PAGE];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_wq_create_q_id_SHIFT	0
#define lpfc_mbx_wq_create_q_id_MASK	0x0000FFFF
#define lpfc_mbx_wq_create_q_id_WORD	word0
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
#define LPFC_DATA_BUF_SIZE 4096
struct rq_context {
	uint32_t word0;
#define lpfc_rq_context_rq_size_SHIFT	16
#define lpfc_rq_context_rq_size_MASK	0x0000000F
#define lpfc_rq_context_rq_size_WORD	word0
#define LPFC_RQ_RING_SIZE_512		9	/* 512 entries */
#define LPFC_RQ_RING_SIZE_1024		10	/* 1024 entries */
#define LPFC_RQ_RING_SIZE_2048		11	/* 2048 entries */
#define LPFC_RQ_RING_SIZE_4096		12	/* 4096 entries */
	uint32_t reserved1;
	uint32_t word2;
#define lpfc_rq_context_cq_id_SHIFT	16
#define lpfc_rq_context_cq_id_MASK	0x000003FF
#define lpfc_rq_context_cq_id_WORD	word2
#define lpfc_rq_context_buf_size_SHIFT	0
#define lpfc_rq_context_buf_size_MASK	0x0000FFFF
#define lpfc_rq_context_buf_size_WORD	word2
	uint32_t reserved3;
};

struct lpfc_mbx_rq_create {
	struct mbox_header header;
	union {
		struct {
			uint32_t word0;
#define lpfc_mbx_rq_create_num_pages_SHIFT	0
#define lpfc_mbx_rq_create_num_pages_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_num_pages_WORD	word0
			struct rq_context context;
			struct dma_address page[LPFC_MAX_WQ_PAGE];
		} request;
		struct {
			uint32_t word0;
#define lpfc_mbx_rq_create_q_id_SHIFT	0
#define lpfc_mbx_rq_create_q_id_MASK	0x0000FFFF
#define lpfc_mbx_rq_create_q_id_WORD	word0
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
#define lpfc_mq_context_cq_id_SHIFT	22
#define lpfc_mq_context_cq_id_MASK	0x000003FF
#define lpfc_mq_context_cq_id_WORD	word0
#define lpfc_mq_context_count_SHIFT	16
#define lpfc_mq_context_count_MASK	0x0000000F
#define lpfc_mq_context_count_WORD	word0
#define LPFC_MQ_CNT_16		0x5
#define LPFC_MQ_CNT_32		0x6
#define LPFC_MQ_CNT_64		0x7
#define LPFC_MQ_CNT_128		0x8
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
#define lpfc_sli4_sge_offset_SHIFT	0 /* Offset of buffer - Not used*/
#define lpfc_sli4_sge_offset_MASK	0x00FFFFFF
#define lpfc_sli4_sge_offset_WORD	word2
#define lpfc_sli4_sge_last_SHIFT	31 /* Last SEG in the SGL sets
						this  flag !! */
#define lpfc_sli4_sge_last_MASK		0x00000001
#define lpfc_sli4_sge_last_WORD		word2
	uint32_t word3;
#define lpfc_sli4_sge_len_SHIFT		0
#define lpfc_sli4_sge_len_MASK		0x0001FFFF
#define lpfc_sli4_sge_len_WORD		word3
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
#define lpfc_fcf_record_fcf_valid_MASK		0x000000FF
#define lpfc_fcf_record_fcf_valid_WORD		word7
	uint32_t word8;
#define lpfc_fcf_record_fcf_index_SHIFT		0
#define lpfc_fcf_record_fcf_index_MASK		0x0000FFFF
#define lpfc_fcf_record_fcf_index_WORD		word8
#define lpfc_fcf_record_fcf_state_SHIFT		16
#define lpfc_fcf_record_fcf_state_MASK		0x0000FFFF
#define lpfc_fcf_record_fcf_state_WORD		word8
	uint8_t vlan_bitmap[512];
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
#define lpfc_init_vfi_vfi_SHIFT		0
#define lpfc_init_vfi_vfi_MASK		0x0000FFFF
#define lpfc_init_vfi_vfi_WORD		word1
	uint32_t word2;
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

struct lpfc_mbx_reg_vfi {
	uint32_t word1;
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
	uint32_t word3_rsvd;
	uint32_t word4_rsvd;
	struct ulp_bde64 bde;
	uint32_t word8_rsvd;
	uint32_t word9_rsvd;
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
#define lpfc_resume_rpi_rpi_SHIFT	0
#define lpfc_resume_rpi_rpi_MASK	0x0000FFFF
#define lpfc_resume_rpi_rpi_WORD	word1
	uint32_t event_tag;
	uint32_t word3_rsvd;
	uint32_t word4_rsvd;
	uint32_t word5_rsvd;
	uint32_t word6;
#define lpfc_resume_rpi_vpi_SHIFT	0
#define lpfc_resume_rpi_vpi_MASK	0x0000FFFF
#define lpfc_resume_rpi_vpi_WORD	word6
#define lpfc_resume_rpi_vfi_SHIFT	16
#define lpfc_resume_rpi_vfi_MASK	0x0000FFFF
#define lpfc_resume_rpi_vfi_WORD	word6
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
#define lpfc_mbx_rd_conf_max_bbc_SHIFT		0
#define lpfc_mbx_rd_conf_max_bbc_MASK		0x000000FF
#define lpfc_mbx_rd_conf_max_bbc_WORD		word1
#define lpfc_mbx_rd_conf_init_bbc_SHIFT		8
#define lpfc_mbx_rd_conf_init_bbc_MASK		0x000000FF
#define lpfc_mbx_rd_conf_init_bbc_WORD		word1
	uint32_t word2;
#define lpfc_mbx_rd_conf_nport_did_SHIFT	0
#define lpfc_mbx_rd_conf_nport_did_MASK		0x00FFFFFF
#define lpfc_mbx_rd_conf_nport_did_WORD		word2
#define lpfc_mbx_rd_conf_topology_SHIFT		24
#define lpfc_mbx_rd_conf_topology_MASK		0x000000FF
#define lpfc_mbx_rd_conf_topology_WORD		word2
	uint32_t word3;
#define lpfc_mbx_rd_conf_ao_SHIFT		0
#define lpfc_mbx_rd_conf_ao_MASK		0x00000001
#define lpfc_mbx_rd_conf_ao_WORD		word3
#define lpfc_mbx_rd_conf_bb_scn_SHIFT		8
#define lpfc_mbx_rd_conf_bb_scn_MASK		0x0000000F
#define lpfc_mbx_rd_conf_bb_scn_WORD		word3
#define lpfc_mbx_rd_conf_cbb_scn_SHIFT		12
#define lpfc_mbx_rd_conf_cbb_scn_MASK		0x0000000F
#define lpfc_mbx_rd_conf_cbb_scn_WORD		word3
#define lpfc_mbx_rd_conf_mc_SHIFT		29
#define lpfc_mbx_rd_conf_mc_MASK		0x00000001
#define lpfc_mbx_rd_conf_mc_WORD		word3
	uint32_t word4;
#define lpfc_mbx_rd_conf_e_d_tov_SHIFT		0
#define lpfc_mbx_rd_conf_e_d_tov_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_e_d_tov_WORD		word4
	uint32_t word5;
#define lpfc_mbx_rd_conf_lp_tov_SHIFT		0
#define lpfc_mbx_rd_conf_lp_tov_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_lp_tov_WORD		word5
	uint32_t word6;
#define lpfc_mbx_rd_conf_r_a_tov_SHIFT		0
#define lpfc_mbx_rd_conf_r_a_tov_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_r_a_tov_WORD		word6
	uint32_t word7;
#define lpfc_mbx_rd_conf_r_t_tov_SHIFT		0
#define lpfc_mbx_rd_conf_r_t_tov_MASK		0x000000FF
#define lpfc_mbx_rd_conf_r_t_tov_WORD		word7
	uint32_t word8;
#define lpfc_mbx_rd_conf_al_tov_SHIFT		0
#define lpfc_mbx_rd_conf_al_tov_MASK		0x0000000F
#define lpfc_mbx_rd_conf_al_tov_WORD		word8
	uint32_t word9;
#define lpfc_mbx_rd_conf_lmt_SHIFT		0
#define lpfc_mbx_rd_conf_lmt_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_lmt_WORD		word9
	uint32_t word10;
#define lpfc_mbx_rd_conf_max_alpa_SHIFT		0
#define lpfc_mbx_rd_conf_max_alpa_MASK		0x000000FF
#define lpfc_mbx_rd_conf_max_alpa_WORD		word10
	uint32_t word11_rsvd;
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
#define lpfc_mbx_rd_conf_fcfi_base_SHIFT	0
#define lpfc_mbx_rd_conf_fcfi_base_MASK		0x0000FFFF
#define lpfc_mbx_rd_conf_fcfi_base_WORD		word16
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
};

/* Mailbox Completion Queue Error Messages */
#define MB_CQE_STATUS_SUCCESS 			0x0
#define MB_CQE_STATUS_INSUFFICIENT_PRIVILEGES	0x1
#define MB_CQE_STATUS_INVALID_PARAMETER		0x2
#define MB_CQE_STATUS_INSUFFICIENT_RESOURCES	0x3
#define MB_CEQ_STATUS_QUEUE_FLUSHING		0x4
#define MB_CQE_STATUS_DMA_FAILED		0x5

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
		struct lpfc_mbx_reg_fcfi reg_fcfi;
		struct lpfc_mbx_unreg_fcfi unreg_fcfi;
		struct lpfc_mbx_mq_create mq_create;
		struct lpfc_mbx_eq_create eq_create;
		struct lpfc_mbx_cq_create cq_create;
		struct lpfc_mbx_wq_create wq_create;
		struct lpfc_mbx_rq_create rq_create;
		struct lpfc_mbx_mq_destroy mq_destroy;
		struct lpfc_mbx_eq_destroy eq_destroy;
		struct lpfc_mbx_cq_destroy cq_destroy;
		struct lpfc_mbx_wq_destroy wq_destroy;
		struct lpfc_mbx_rq_destroy rq_destroy;
		struct lpfc_mbx_post_sgl_pages post_sgl_pages;
		struct lpfc_mbx_nembed_cmd nembed_cmd;
		struct lpfc_mbx_read_rev read_rev;
		struct lpfc_mbx_read_vpi read_vpi;
		struct lpfc_mbx_read_config rd_config;
		struct lpfc_mbx_request_features req_ftrs;
		struct lpfc_mbx_post_hdr_tmpl hdr_tmpl;
		struct lpfc_mbx_nop nop;
	} un;
};

struct lpfc_mcqe {
	uint32_t word0;
#define lpfc_mcqe_status_SHIFT		0
#define lpfc_mcqe_status_MASK		0x0000FFFF
#define lpfc_mcqe_status_WORD		word0
#define lpfc_mcqe_ext_status_SHIFT	16
#define lpfc_mcqe_ext_status_MASK  	0x0000FFFF
#define lpfc_mcqe_ext_status_WORD 	word0
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
#define lpfc_acqe_link_physical_SHIFT		0
#define lpfc_acqe_link_physical_MASK		0x000000FF
#define lpfc_acqe_link_physical_WORD		word0
#define LPFC_ASYNC_LINK_PORT_A			0x0
#define LPFC_ASYNC_LINK_PORT_B			0x1
	uint32_t word1;
#define lpfc_acqe_link_fault_SHIFT	0
#define lpfc_acqe_link_fault_MASK	0x000000FF
#define lpfc_acqe_link_fault_WORD	word1
#define LPFC_ASYNC_LINK_FAULT_NONE	0x0
#define LPFC_ASYNC_LINK_FAULT_LOCAL	0x1
#define LPFC_ASYNC_LINK_FAULT_REMOTE	0x2
	uint32_t event_tag;
	uint32_t trailer;
};

struct lpfc_acqe_fcoe {
	uint32_t fcf_index;
	uint32_t word1;
#define lpfc_acqe_fcoe_fcf_count_SHIFT		0
#define lpfc_acqe_fcoe_fcf_count_MASK		0x0000FFFF
#define lpfc_acqe_fcoe_fcf_count_WORD		word1
#define lpfc_acqe_fcoe_event_type_SHIFT		16
#define lpfc_acqe_fcoe_event_type_MASK		0x0000FFFF
#define lpfc_acqe_fcoe_event_type_WORD		word1
#define LPFC_FCOE_EVENT_TYPE_NEW_FCF		0x1
#define LPFC_FCOE_EVENT_TYPE_FCF_TABLE_FULL	0x2
#define LPFC_FCOE_EVENT_TYPE_FCF_DEAD		0x3
	uint32_t event_tag;
	uint32_t trailer;
};

struct lpfc_acqe_dcbx {
	uint32_t tlv_ttl;
	uint32_t reserved;
	uint32_t event_tag;
	uint32_t trailer;
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
#define NO_XRI ((uint16_t)-1)
struct wqe_common {
	uint32_t word6;
#define wqe_xri_SHIFT         0
#define wqe_xri_MASK          0x0000FFFF
#define wqe_xri_WORD          word6
#define wqe_ctxt_tag_SHIFT    16
#define wqe_ctxt_tag_MASK     0x0000FFFF
#define wqe_ctxt_tag_WORD     word6
	uint32_t word7;
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
#define wqe_pu_SHIFT          20
#define wqe_pu_MASK           0x00000003
#define wqe_pu_WORD           word7
#define wqe_erp_SHIFT         22
#define wqe_erp_MASK          0x00000001
#define wqe_erp_WORD          word7
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
#define wqe_rcvoxid_SHIFT     16
#define wqe_rcvoxid_MASK       0x0000FFFF
#define wqe_rcvoxid_WORD       word9
	uint32_t word10;
#define wqe_pri_SHIFT         16
#define wqe_pri_MASK          0x00000007
#define wqe_pri_WORD          word10
#define wqe_pv_SHIFT          19
#define wqe_pv_MASK           0x00000001
#define wqe_pv_WORD           word10
#define wqe_xc_SHIFT          21
#define wqe_xc_MASK           0x00000001
#define wqe_xc_WORD           word10
#define wqe_ccpe_SHIFT        23
#define wqe_ccpe_MASK         0x00000001
#define wqe_ccpe_WORD         word10
#define wqe_ccp_SHIFT         24
#define wqe_ccp_MASK         0x000000ff
#define wqe_ccp_WORD         word10
	uint32_t word11;
#define wqe_cmd_type_SHIFT  0
#define wqe_cmd_type_MASK   0x0000000f
#define wqe_cmd_type_WORD   word11
#define wqe_wqec_SHIFT      7
#define wqe_wqec_MASK       0x00000001
#define wqe_wqec_WORD       word11
#define wqe_cqid_SHIFT      16
#define wqe_cqid_MASK       0x000003ff
#define wqe_cqid_WORD       word11
};

struct wqe_did {
	uint32_t word5;
#define wqe_els_did_SHIFT         0
#define wqe_els_did_MASK          0x00FFFFFF
#define wqe_els_did_WORD          word5
#define wqe_xmit_bls_ar_SHIFT         30
#define wqe_xmit_bls_ar_MASK          0x00000001
#define wqe_xmit_bls_ar_WORD          word5
#define wqe_xmit_bls_xo_SHIFT         31
#define wqe_xmit_bls_xo_MASK          0x00000001
#define wqe_xmit_bls_xo_WORD          word5
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
	uint32_t reserved[2];
};

struct xmit_els_rsp64_wqe {
	struct ulp_bde64 bde;
	uint32_t rsvd3;
	uint32_t rsvd4;
	struct wqe_did	wqe_dest;
	struct wqe_common wqe_com; /* words 6-11 */
	uint32_t rsvd_12_15[4];
};

struct xmit_bls_rsp64_wqe {
	uint32_t payload0;
	uint32_t word1;
#define xmit_bls_rsp64_rxid_SHIFT  0
#define xmit_bls_rsp64_rxid_MASK   0x0000ffff
#define xmit_bls_rsp64_rxid_WORD   word1
#define xmit_bls_rsp64_oxid_SHIFT  16
#define xmit_bls_rsp64_oxid_MASK   0x0000ffff
#define xmit_bls_rsp64_oxid_WORD   word1
	uint32_t word2;
#define xmit_bls_rsp64_seqcntlo_SHIFT  0
#define xmit_bls_rsp64_seqcntlo_MASK   0x0000ffff
#define xmit_bls_rsp64_seqcntlo_WORD   word2
#define xmit_bls_rsp64_seqcnthi_SHIFT  16
#define xmit_bls_rsp64_seqcnthi_MASK   0x0000ffff
#define xmit_bls_rsp64_seqcnthi_WORD   word2
	uint32_t rsrvd3;
	uint32_t rsrvd4;
	struct wqe_did	wqe_dest;
	struct wqe_common wqe_com; /* words 6-11 */
	uint32_t rsvd_12_15[4];
};
struct wqe_rctl_dfctl {
	uint32_t word5;
#define wqe_si_SHIFT 2
#define wqe_si_MASK  0x000000001
#define wqe_si_WORD  word5
#define wqe_la_SHIFT 3
#define wqe_la_MASK  0x000000001
#define wqe_la_WORD  word5
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
	uint32_t paylaod_offset;
	uint32_t relative_offset;
	struct wqe_rctl_dfctl wge_ctl;
	struct wqe_common wqe_com; /* words 6-11 */
	/* Note: word10 different REVISIT */
	uint32_t xmit_len;
	uint32_t rsvd_12_15[3];
};
struct xmit_bcast64_wqe {
	struct ulp_bde64 bde;
	uint32_t paylaod_len;
	uint32_t rsvd4;
	struct wqe_rctl_dfctl wge_ctl; /* word 5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];
};

struct gen_req64_wqe {
	struct ulp_bde64 bde;
	uint32_t command_len;
	uint32_t payload_len;
	struct wqe_rctl_dfctl wge_ctl; /* word 5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];
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
	uint32_t payload_len;
	uint32_t total_xfer_len;
	uint32_t initial_xfer_len;
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];         /* word 12-15 */
};

struct fcp_iread64_wqe {
	struct ulp_bde64 bde;
	uint32_t payload_len;          /* word 3 */
	uint32_t total_xfer_len;       /* word 4 */
	uint32_t rsrvd5;               /* word 5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];         /* word 12-15 */
};

struct fcp_icmnd64_wqe {
	struct ulp_bde64 bde;	 /* words 0-2 */
	uint32_t rsrvd[3];             /* words 3-5 */
	struct wqe_common wqe_com;     /* words 6-11 */
	uint32_t rsvd_12_15[4];         /* word 12-15 */
};


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
};

#define FCP_COMMAND 0x0
#define FCP_COMMAND_DATA_OUT 0x1
#define ELS_COMMAND_NON_FIP 0xC
#define ELS_COMMAND_FIP 0xD
#define OTHER_COMMAND 0x8

