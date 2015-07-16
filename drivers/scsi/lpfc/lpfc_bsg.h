/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2010-2015 Emulex.  All rights reserved.                *
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
/* bsg definitions
 * No pointers to user data are allowed, all application buffers and sizes will
 * derived through the bsg interface.
 *
 * These are the vendor unique structures passed in using the bsg
 * FC_BSG_HST_VENDOR message code type.
 */
#define LPFC_BSG_VENDOR_SET_CT_EVENT		1
#define LPFC_BSG_VENDOR_GET_CT_EVENT		2
#define LPFC_BSG_VENDOR_SEND_MGMT_RESP		3
#define LPFC_BSG_VENDOR_DIAG_MODE		4
#define LPFC_BSG_VENDOR_DIAG_RUN_LOOPBACK	5
#define LPFC_BSG_VENDOR_GET_MGMT_REV		6
#define LPFC_BSG_VENDOR_MBOX			7
#define LPFC_BSG_VENDOR_MENLO_CMD		8
#define LPFC_BSG_VENDOR_MENLO_DATA		9
#define LPFC_BSG_VENDOR_DIAG_MODE_END		10
#define LPFC_BSG_VENDOR_LINK_DIAG_TEST		11

struct set_ct_event {
	uint32_t command;
	uint32_t type_mask;
	uint32_t ev_req_id;
	uint32_t ev_reg_id;
};

struct get_ct_event {
	uint32_t command;
	uint32_t ev_reg_id;
	uint32_t ev_req_id;
};

struct get_ct_event_reply {
	uint32_t immed_data;
	uint32_t type;
};

struct send_mgmt_resp {
	uint32_t command;
	uint32_t tag;
};


#define INTERNAL_LOOP_BACK 0x1 /* adapter short cuts the loop internally */
#define EXTERNAL_LOOP_BACK 0x2 /* requires an external loopback plug */

struct diag_mode_set {
	uint32_t command;
	uint32_t type;
	uint32_t timeout;
};

struct sli4_link_diag {
	uint32_t command;
	uint32_t timeout;
	uint32_t test_id;
	uint32_t loops;
	uint32_t test_version;
	uint32_t error_action;
};

struct diag_mode_test {
	uint32_t command;
};

struct diag_status {
	uint32_t mbox_status;
	uint32_t shdr_status;
	uint32_t shdr_add_status;
};

#define LPFC_WWNN_TYPE		0
#define LPFC_WWPN_TYPE		1

struct get_mgmt_rev {
	uint32_t command;
};

#define MANAGEMENT_MAJOR_REV   1
#define MANAGEMENT_MINOR_REV   1

/* the MgmtRevInfo structure */
struct MgmtRevInfo {
	uint32_t a_Major;
	uint32_t a_Minor;
};

struct get_mgmt_rev_reply {
	struct MgmtRevInfo info;
};

#define BSG_MBOX_SIZE 4096 /* mailbox command plus extended data */

/* BSG mailbox request header */
struct dfc_mbox_req {
	uint32_t command;
	uint32_t mbOffset;
	uint32_t inExtWLen;
	uint32_t outExtWLen;
	uint32_t extMboxTag;
	uint32_t extSeqNum;
};

/* Used for menlo command or menlo data. The xri is only used for menlo data */
struct menlo_command {
	uint32_t cmd;
	uint32_t xri;
};

struct menlo_response {
	uint32_t xri; /* return the xri of the iocb exchange */
};

/*
 * macros and data structures for handling sli-config mailbox command
 * pass-through support, this header file is shared between user and
 * kernel spaces, note the set of macros are duplicates from lpfc_hw4.h,
 * with macro names prefixed with bsg_, as the macros defined in
 * lpfc_hw4.h are not accessible from user space.
 */

/* Macros to deal with bit fields. Each bit field must have 3 #defines
 * associated with it (_SHIFT, _MASK, and _WORD).
 * EG. For a bit field that is in the 7th bit of the "field4" field of a
 * structure and is 2 bits in size the following #defines must exist:
 *      struct temp {
 *              uint32_t        field1;
 *              uint32_t        field2;
 *              uint32_t        field3;
 *              uint32_t        field4;
 *      #define example_bit_field_SHIFT         7
 *      #define example_bit_field_MASK          0x03
 *      #define example_bit_field_WORD          field4
 *              uint32_t        field5;
 *      };
 * Then the macros below may be used to get or set the value of that field.
 * EG. To get the value of the bit field from the above example:
 *      struct temp t1;
 *      value = bsg_bf_get(example_bit_field, &t1);
 * And then to set that bit field:
 *      bsg_bf_set(example_bit_field, &t1, 2);
 * Or clear that bit field:
 *      bsg_bf_set(example_bit_field, &t1, 0);
 */
#define bsg_bf_get_le32(name, ptr) \
	((le32_to_cpu((ptr)->name##_WORD) >> name##_SHIFT) & name##_MASK)
#define bsg_bf_get(name, ptr) \
	(((ptr)->name##_WORD >> name##_SHIFT) & name##_MASK)
#define bsg_bf_set_le32(name, ptr, value) \
	((ptr)->name##_WORD = cpu_to_le32(((((value) & \
	name##_MASK) << name##_SHIFT) | (le32_to_cpu((ptr)->name##_WORD) & \
	~(name##_MASK << name##_SHIFT)))))
#define bsg_bf_set(name, ptr, value) \
	((ptr)->name##_WORD = ((((value) & name##_MASK) << name##_SHIFT) | \
	((ptr)->name##_WORD & ~(name##_MASK << name##_SHIFT))))

/*
 * The sli_config structure specified here is based on the following
 * restriction:
 *
 * -- SLI_CONFIG EMB=0, carrying MSEs, will carry subcommands without
 *    carrying HBD.
 * -- SLI_CONFIG EMB=1, not carrying MSE, will carry subcommands with or
 *    without carrying HBDs.
 */

struct lpfc_sli_config_mse {
	uint32_t pa_lo;
	uint32_t pa_hi;
	uint32_t buf_len;
#define lpfc_mbox_sli_config_mse_len_SHIFT	0
#define lpfc_mbox_sli_config_mse_len_MASK	0xffffff
#define lpfc_mbox_sli_config_mse_len_WORD	buf_len
};

struct lpfc_sli_config_hbd {
	uint32_t buf_len;
#define lpfc_mbox_sli_config_ecmn_hbd_len_SHIFT	0
#define lpfc_mbox_sli_config_ecmn_hbd_len_MASK	0xffffff
#define lpfc_mbox_sli_config_ecmn_hbd_len_WORD	buf_len
	uint32_t pa_lo;
	uint32_t pa_hi;
};

struct lpfc_sli_config_hdr {
	uint32_t word1;
#define lpfc_mbox_hdr_emb_SHIFT		0
#define lpfc_mbox_hdr_emb_MASK		0x00000001
#define lpfc_mbox_hdr_emb_WORD		word1
#define lpfc_mbox_hdr_mse_cnt_SHIFT	3
#define lpfc_mbox_hdr_mse_cnt_MASK	0x0000001f
#define lpfc_mbox_hdr_mse_cnt_WORD	word1
	uint32_t payload_length;
	uint32_t tag_lo;
	uint32_t tag_hi;
	uint32_t reserved5;
};

struct lpfc_sli_config_emb0_subsys {
	struct lpfc_sli_config_hdr	sli_config_hdr;
#define LPFC_MBX_SLI_CONFIG_MAX_MSE     19
	struct lpfc_sli_config_mse	mse[LPFC_MBX_SLI_CONFIG_MAX_MSE];
	uint32_t padding;
	uint32_t word64;
#define lpfc_emb0_subcmnd_opcode_SHIFT	0
#define lpfc_emb0_subcmnd_opcode_MASK	0xff
#define lpfc_emb0_subcmnd_opcode_WORD	word64
#define lpfc_emb0_subcmnd_subsys_SHIFT	8
#define lpfc_emb0_subcmnd_subsys_MASK	0xff
#define lpfc_emb0_subcmnd_subsys_WORD	word64
/* Subsystem FCOE (0x0C) OpCodes */
#define SLI_CONFIG_SUBSYS_FCOE		0x0C
#define FCOE_OPCODE_READ_FCF		0x08
#define FCOE_OPCODE_ADD_FCF		0x09
#define FCOE_OPCODE_SET_DPORT_MODE	0x27
#define FCOE_OPCODE_GET_DPORT_RESULTS	0x28
};

struct lpfc_sli_config_emb1_subsys {
	struct lpfc_sli_config_hdr	sli_config_hdr;
	uint32_t word6;
#define lpfc_emb1_subcmnd_opcode_SHIFT	0
#define lpfc_emb1_subcmnd_opcode_MASK	0xff
#define lpfc_emb1_subcmnd_opcode_WORD	word6
#define lpfc_emb1_subcmnd_subsys_SHIFT	8
#define lpfc_emb1_subcmnd_subsys_MASK	0xff
#define lpfc_emb1_subcmnd_subsys_WORD	word6
/* Subsystem COMN (0x01) OpCodes */
#define SLI_CONFIG_SUBSYS_COMN		0x01
#define COMN_OPCODE_GET_PROFILE_CONFIG	0xA4
#define COMN_OPCODE_READ_OBJECT		0xAB
#define COMN_OPCODE_WRITE_OBJECT	0xAC
#define COMN_OPCODE_READ_OBJECT_LIST	0xAD
#define COMN_OPCODE_DELETE_OBJECT	0xAE
#define COMN_OPCODE_GET_CNTL_ADDL_ATTRIBUTES	0x79
#define COMN_OPCODE_GET_CNTL_ATTRIBUTES	0x20
	uint32_t timeout;
	uint32_t request_length;
	uint32_t word9;
#define lpfc_subcmnd_version_SHIFT	0
#define lpfc_subcmnd_version_MASK	0xff
#define lpfc_subcmnd_version_WORD	word9
	uint32_t word10;
#define lpfc_subcmnd_ask_rd_len_SHIFT	0
#define lpfc_subcmnd_ask_rd_len_MASK	0xffffff
#define lpfc_subcmnd_ask_rd_len_WORD	word10
	uint32_t rd_offset;
	uint32_t obj_name[26];
	uint32_t hbd_count;
#define LPFC_MBX_SLI_CONFIG_MAX_HBD	8
	struct lpfc_sli_config_hbd	hbd[LPFC_MBX_SLI_CONFIG_MAX_HBD];
};

struct lpfc_sli_config_mbox {
	uint32_t word0;
#define lpfc_mqe_status_SHIFT		16
#define lpfc_mqe_status_MASK		0x0000FFFF
#define lpfc_mqe_status_WORD		word0
#define lpfc_mqe_command_SHIFT		8
#define lpfc_mqe_command_MASK		0x000000FF
#define lpfc_mqe_command_WORD		word0
	union {
		struct lpfc_sli_config_emb0_subsys sli_config_emb0_subsys;
		struct lpfc_sli_config_emb1_subsys sli_config_emb1_subsys;
	} un;
};

/* driver only */
#define SLI_CONFIG_NOT_HANDLED		0
#define SLI_CONFIG_HANDLED		1
