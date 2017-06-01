/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"
#include "qed_vf.h"

struct qed_pi_info {
	qed_int_comp_cb_t	comp_cb;
	void			*cookie;
};

struct qed_sb_sp_info {
	struct qed_sb_info	sb_info;

	/* per protocol index data */
	struct qed_pi_info	pi_info_arr[PIS_PER_SB];
};

enum qed_attention_type {
	QED_ATTN_TYPE_ATTN,
	QED_ATTN_TYPE_PARITY,
};

#define SB_ATTN_ALIGNED_SIZE(p_hwfn) \
	ALIGNED_TYPE_SIZE(struct atten_status_block, p_hwfn)

struct aeu_invert_reg_bit {
	char bit_name[30];

#define ATTENTION_PARITY                (1 << 0)

#define ATTENTION_LENGTH_MASK           (0x00000ff0)
#define ATTENTION_LENGTH_SHIFT          (4)
#define ATTENTION_LENGTH(flags)         (((flags) & ATTENTION_LENGTH_MASK) >> \
					 ATTENTION_LENGTH_SHIFT)
#define ATTENTION_SINGLE                (1 << ATTENTION_LENGTH_SHIFT)
#define ATTENTION_PAR                   (ATTENTION_SINGLE | ATTENTION_PARITY)
#define ATTENTION_PAR_INT               ((2 << ATTENTION_LENGTH_SHIFT) | \
					 ATTENTION_PARITY)

/* Multiple bits start with this offset */
#define ATTENTION_OFFSET_MASK           (0x000ff000)
#define ATTENTION_OFFSET_SHIFT          (12)

#define ATTENTION_BB_MASK               (0x00700000)
#define ATTENTION_BB_SHIFT              (20)
#define ATTENTION_BB(value)             (value << ATTENTION_BB_SHIFT)
#define ATTENTION_BB_DIFFERENT          BIT(23)

	unsigned int flags;

	/* Callback to call if attention will be triggered */
	int (*cb)(struct qed_hwfn *p_hwfn);

	enum block_id block_index;
};

struct aeu_invert_reg {
	struct aeu_invert_reg_bit bits[32];
};

#define MAX_ATTN_GRPS           (8)
#define NUM_ATTN_REGS           (9)

/* Specific HW attention callbacks */
static int qed_mcp_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, MCP_REG_CPU_STATE);

	/* This might occur on certain instances; Log it once then mask it */
	DP_INFO(p_hwfn->cdev, "MCP_REG_CPU_STATE: %08x - Masking...\n",
		tmp);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, MCP_REG_CPU_EVENT_MASK,
	       0xffffffff);

	return 0;
}

#define QED_PSWHST_ATTENTION_INCORRECT_ACCESS		(0x1)
#define ATTENTION_INCORRECT_ACCESS_WR_MASK		(0x1)
#define ATTENTION_INCORRECT_ACCESS_WR_SHIFT		(0)
#define ATTENTION_INCORRECT_ACCESS_CLIENT_MASK		(0xf)
#define ATTENTION_INCORRECT_ACCESS_CLIENT_SHIFT		(1)
#define ATTENTION_INCORRECT_ACCESS_VF_VALID_MASK	(0x1)
#define ATTENTION_INCORRECT_ACCESS_VF_VALID_SHIFT	(5)
#define ATTENTION_INCORRECT_ACCESS_VF_ID_MASK		(0xff)
#define ATTENTION_INCORRECT_ACCESS_VF_ID_SHIFT		(6)
#define ATTENTION_INCORRECT_ACCESS_PF_ID_MASK		(0xf)
#define ATTENTION_INCORRECT_ACCESS_PF_ID_SHIFT		(14)
#define ATTENTION_INCORRECT_ACCESS_BYTE_EN_MASK		(0xff)
#define ATTENTION_INCORRECT_ACCESS_BYTE_EN_SHIFT	(18)
static int qed_pswhst_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			 PSWHST_REG_INCORRECT_ACCESS_VALID);

	if (tmp & QED_PSWHST_ATTENTION_INCORRECT_ACCESS) {
		u32 addr, data, length;

		addr = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			      PSWHST_REG_INCORRECT_ACCESS_ADDRESS);
		data = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			      PSWHST_REG_INCORRECT_ACCESS_DATA);
		length = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				PSWHST_REG_INCORRECT_ACCESS_LENGTH);

		DP_INFO(p_hwfn->cdev,
			"Incorrect access to %08x of length %08x - PF [%02x] VF [%04x] [valid %02x] client [%02x] write [%02x] Byte-Enable [%04x] [%08x]\n",
			addr, length,
			(u8) GET_FIELD(data, ATTENTION_INCORRECT_ACCESS_PF_ID),
			(u8) GET_FIELD(data, ATTENTION_INCORRECT_ACCESS_VF_ID),
			(u8) GET_FIELD(data,
				       ATTENTION_INCORRECT_ACCESS_VF_VALID),
			(u8) GET_FIELD(data,
				       ATTENTION_INCORRECT_ACCESS_CLIENT),
			(u8) GET_FIELD(data, ATTENTION_INCORRECT_ACCESS_WR),
			(u8) GET_FIELD(data,
				       ATTENTION_INCORRECT_ACCESS_BYTE_EN),
			data);
	}

	return 0;
}

#define QED_GRC_ATTENTION_VALID_BIT	(1 << 0)
#define QED_GRC_ATTENTION_ADDRESS_MASK	(0x7fffff)
#define QED_GRC_ATTENTION_ADDRESS_SHIFT	(0)
#define QED_GRC_ATTENTION_RDWR_BIT	(1 << 23)
#define QED_GRC_ATTENTION_MASTER_MASK	(0xf)
#define QED_GRC_ATTENTION_MASTER_SHIFT	(24)
#define QED_GRC_ATTENTION_PF_MASK	(0xf)
#define QED_GRC_ATTENTION_PF_SHIFT	(0)
#define QED_GRC_ATTENTION_VF_MASK	(0xff)
#define QED_GRC_ATTENTION_VF_SHIFT	(4)
#define QED_GRC_ATTENTION_PRIV_MASK	(0x3)
#define QED_GRC_ATTENTION_PRIV_SHIFT	(14)
#define QED_GRC_ATTENTION_PRIV_VF	(0)
static const char *attn_master_to_str(u8 master)
{
	switch (master) {
	case 1: return "PXP";
	case 2: return "MCP";
	case 3: return "MSDM";
	case 4: return "PSDM";
	case 5: return "YSDM";
	case 6: return "USDM";
	case 7: return "TSDM";
	case 8: return "XSDM";
	case 9: return "DBU";
	case 10: return "DMAE";
	default:
		return "Unknown";
	}
}

static int qed_grc_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp, tmp2;

	/* We've already cleared the timeout interrupt register, so we learn
	 * of interrupts via the validity register
	 */
	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     GRC_REG_TIMEOUT_ATTN_ACCESS_VALID);
	if (!(tmp & QED_GRC_ATTENTION_VALID_BIT))
		goto out;

	/* Read the GRC timeout information */
	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     GRC_REG_TIMEOUT_ATTN_ACCESS_DATA_0);
	tmp2 = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		      GRC_REG_TIMEOUT_ATTN_ACCESS_DATA_1);

	DP_INFO(p_hwfn->cdev,
		"GRC timeout [%08x:%08x] - %s Address [%08x] [Master %s] [PF: %02x %s %02x]\n",
		tmp2, tmp,
		(tmp & QED_GRC_ATTENTION_RDWR_BIT) ? "Write to" : "Read from",
		GET_FIELD(tmp, QED_GRC_ATTENTION_ADDRESS) << 2,
		attn_master_to_str(GET_FIELD(tmp, QED_GRC_ATTENTION_MASTER)),
		GET_FIELD(tmp2, QED_GRC_ATTENTION_PF),
		(GET_FIELD(tmp2, QED_GRC_ATTENTION_PRIV) ==
		 QED_GRC_ATTENTION_PRIV_VF) ? "VF" : "(Ireelevant)",
		GET_FIELD(tmp2, QED_GRC_ATTENTION_VF));

out:
	/* Regardles of anything else, clean the validity bit */
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt,
	       GRC_REG_TIMEOUT_ATTN_ACCESS_VALID, 0);
	return 0;
}

#define PGLUE_ATTENTION_VALID			(1 << 29)
#define PGLUE_ATTENTION_RD_VALID		(1 << 26)
#define PGLUE_ATTENTION_DETAILS_PFID_MASK	(0xf)
#define PGLUE_ATTENTION_DETAILS_PFID_SHIFT	(20)
#define PGLUE_ATTENTION_DETAILS_VF_VALID_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS_VF_VALID_SHIFT	(19)
#define PGLUE_ATTENTION_DETAILS_VFID_MASK	(0xff)
#define PGLUE_ATTENTION_DETAILS_VFID_SHIFT	(24)
#define PGLUE_ATTENTION_DETAILS2_WAS_ERR_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS2_WAS_ERR_SHIFT	(21)
#define PGLUE_ATTENTION_DETAILS2_BME_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS2_BME_SHIFT	(22)
#define PGLUE_ATTENTION_DETAILS2_FID_EN_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS2_FID_EN_SHIFT	(23)
#define PGLUE_ATTENTION_ICPL_VALID		(1 << 23)
#define PGLUE_ATTENTION_ZLR_VALID		(1 << 25)
#define PGLUE_ATTENTION_ILT_VALID		(1 << 23)
static int qed_pglub_rbc_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp;

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_TX_ERR_WR_DETAILS2);
	if (tmp & PGLUE_ATTENTION_VALID) {
		u32 addr_lo, addr_hi, details;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_WR_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_WR_ADD_63_32);
		details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_WR_DETAILS);

		DP_INFO(p_hwfn,
			"Illegal write by chip to [%08x:%08x] blocked.\n"
			"Details: %08x [PFID %02x, VFID %02x, VF_VALID %02x]\n"
			"Details2 %08x [Was_error %02x BME deassert %02x FID_enable deassert %02x]\n",
			addr_hi, addr_lo, details,
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_PFID),
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_VFID),
			GET_FIELD(details,
				  PGLUE_ATTENTION_DETAILS_VF_VALID) ? 1 : 0,
			tmp,
			GET_FIELD(tmp,
				  PGLUE_ATTENTION_DETAILS2_WAS_ERR) ? 1 : 0,
			GET_FIELD(tmp,
				  PGLUE_ATTENTION_DETAILS2_BME) ? 1 : 0,
			GET_FIELD(tmp,
				  PGLUE_ATTENTION_DETAILS2_FID_EN) ? 1 : 0);
	}

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_TX_ERR_RD_DETAILS2);
	if (tmp & PGLUE_ATTENTION_RD_VALID) {
		u32 addr_lo, addr_hi, details;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_RD_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_RD_ADD_63_32);
		details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_RD_DETAILS);

		DP_INFO(p_hwfn,
			"Illegal read by chip from [%08x:%08x] blocked.\n"
			" Details: %08x [PFID %02x, VFID %02x, VF_VALID %02x]\n"
			" Details2 %08x [Was_error %02x BME deassert %02x FID_enable deassert %02x]\n",
			addr_hi, addr_lo, details,
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_PFID),
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_VFID),
			GET_FIELD(details,
				  PGLUE_ATTENTION_DETAILS_VF_VALID) ? 1 : 0,
			tmp,
			GET_FIELD(tmp, PGLUE_ATTENTION_DETAILS2_WAS_ERR) ? 1
									 : 0,
			GET_FIELD(tmp, PGLUE_ATTENTION_DETAILS2_BME) ? 1 : 0,
			GET_FIELD(tmp, PGLUE_ATTENTION_DETAILS2_FID_EN) ? 1
									: 0);
	}

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_TX_ERR_WR_DETAILS_ICPL);
	if (tmp & PGLUE_ATTENTION_ICPL_VALID)
		DP_INFO(p_hwfn, "ICPL eror - %08x\n", tmp);

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_MASTER_ZLR_ERR_DETAILS);
	if (tmp & PGLUE_ATTENTION_ZLR_VALID) {
		u32 addr_hi, addr_lo;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_MASTER_ZLR_ERR_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_MASTER_ZLR_ERR_ADD_63_32);

		DP_INFO(p_hwfn, "ZLR eror - %08x [Address %08x:%08x]\n",
			tmp, addr_hi, addr_lo);
	}

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_VF_ILT_ERR_DETAILS2);
	if (tmp & PGLUE_ATTENTION_ILT_VALID) {
		u32 addr_hi, addr_lo, details;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_VF_ILT_ERR_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_VF_ILT_ERR_ADD_63_32);
		details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_VF_ILT_ERR_DETAILS);

		DP_INFO(p_hwfn,
			"ILT error - Details %08x Details2 %08x [Address %08x:%08x]\n",
			details, tmp, addr_hi, addr_lo);
	}

	/* Clear the indications */
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt,
	       PGLUE_B_REG_LATCHED_ERRORS_CLR, (1 << 2));

	return 0;
}

#define QED_DORQ_ATTENTION_REASON_MASK	(0xfffff)
#define QED_DORQ_ATTENTION_OPAQUE_MASK (0xffff)
#define QED_DORQ_ATTENTION_SIZE_MASK	(0x7f)
#define QED_DORQ_ATTENTION_SIZE_SHIFT	(16)
static int qed_dorq_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 reason;

	reason = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, DORQ_REG_DB_DROP_REASON) &
			QED_DORQ_ATTENTION_REASON_MASK;
	if (reason) {
		u32 details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				     DORQ_REG_DB_DROP_DETAILS);

		DP_INFO(p_hwfn->cdev,
			"DORQ db_drop: address 0x%08x Opaque FID 0x%04x Size [bytes] 0x%08x Reason: 0x%08x\n",
			qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			       DORQ_REG_DB_DROP_DETAILS_ADDRESS),
			(u16)(details & QED_DORQ_ATTENTION_OPAQUE_MASK),
			GET_FIELD(details, QED_DORQ_ATTENTION_SIZE) * 4,
			reason);
	}

	return -EINVAL;
}

/* Instead of major changes to the data-structure, we have a some 'special'
 * identifiers for sources that changed meaning between adapters.
 */
enum aeu_invert_reg_special_type {
	AEU_INVERT_REG_SPECIAL_CNIG_0,
	AEU_INVERT_REG_SPECIAL_CNIG_1,
	AEU_INVERT_REG_SPECIAL_CNIG_2,
	AEU_INVERT_REG_SPECIAL_CNIG_3,
	AEU_INVERT_REG_SPECIAL_MAX,
};

static struct aeu_invert_reg_bit
aeu_descs_special[AEU_INVERT_REG_SPECIAL_MAX] = {
	{"CNIG port 0", ATTENTION_SINGLE, NULL, BLOCK_CNIG},
	{"CNIG port 1", ATTENTION_SINGLE, NULL, BLOCK_CNIG},
	{"CNIG port 2", ATTENTION_SINGLE, NULL, BLOCK_CNIG},
	{"CNIG port 3", ATTENTION_SINGLE, NULL, BLOCK_CNIG},
};

/* Notice aeu_invert_reg must be defined in the same order of bits as HW;  */
static struct aeu_invert_reg aeu_descs[NUM_ATTN_REGS] = {
	{
		{       /* After Invert 1 */
			{"GPIO0 function%d",
			 (32 << ATTENTION_LENGTH_SHIFT), NULL, MAX_BLOCK_ID},
		}
	},

	{
		{       /* After Invert 2 */
			{"PGLUE config_space", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"PGLUE misc_flr", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"PGLUE B RBC", ATTENTION_PAR_INT,
			 qed_pglub_rbc_attn_cb, BLOCK_PGLUE_B},
			{"PGLUE misc_mctp", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"Flash event", ATTENTION_SINGLE, NULL, MAX_BLOCK_ID},
			{"SMB event", ATTENTION_SINGLE,	NULL, MAX_BLOCK_ID},
			{"Main Power", ATTENTION_SINGLE, NULL, MAX_BLOCK_ID},
			{"SW timers #%d", (8 << ATTENTION_LENGTH_SHIFT) |
					  (1 << ATTENTION_OFFSET_SHIFT),
			 NULL, MAX_BLOCK_ID},
			{"PCIE glue/PXP VPD %d",
			 (16 << ATTENTION_LENGTH_SHIFT), NULL, BLOCK_PGLCS},
		}
	},

	{
		{       /* After Invert 3 */
			{"General Attention %d",
			 (32 << ATTENTION_LENGTH_SHIFT), NULL, MAX_BLOCK_ID},
		}
	},

	{
		{       /* After Invert 4 */
			{"General Attention 32", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"General Attention %d",
			 (2 << ATTENTION_LENGTH_SHIFT) |
			 (33 << ATTENTION_OFFSET_SHIFT), NULL, MAX_BLOCK_ID},
			{"General Attention 35", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"NWS Parity",
			 ATTENTION_PAR | ATTENTION_BB_DIFFERENT |
			 ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_0),
			 NULL, BLOCK_NWS},
			{"NWS Interrupt",
			 ATTENTION_SINGLE | ATTENTION_BB_DIFFERENT |
			 ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_1),
			 NULL, BLOCK_NWS},
			{"NWM Parity",
			 ATTENTION_PAR | ATTENTION_BB_DIFFERENT |
			 ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_2),
			 NULL, BLOCK_NWM},
			{"NWM Interrupt",
			 ATTENTION_SINGLE | ATTENTION_BB_DIFFERENT |
			 ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_3),
			 NULL, BLOCK_NWM},
			{"MCP CPU", ATTENTION_SINGLE,
			 qed_mcp_attn_cb, MAX_BLOCK_ID},
			{"MCP Watchdog timer", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"MCP M2P", ATTENTION_SINGLE, NULL, MAX_BLOCK_ID},
			{"AVS stop status ready", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"MSTAT", ATTENTION_PAR_INT, NULL, MAX_BLOCK_ID},
			{"MSTAT per-path", ATTENTION_PAR_INT,
			 NULL, MAX_BLOCK_ID},
			{"Reserved %d", (6 << ATTENTION_LENGTH_SHIFT),
			 NULL, MAX_BLOCK_ID},
			{"NIG", ATTENTION_PAR_INT, NULL, BLOCK_NIG},
			{"BMB/OPTE/MCP", ATTENTION_PAR_INT, NULL, BLOCK_BMB},
			{"BTB",	ATTENTION_PAR_INT, NULL, BLOCK_BTB},
			{"BRB",	ATTENTION_PAR_INT, NULL, BLOCK_BRB},
			{"PRS",	ATTENTION_PAR_INT, NULL, BLOCK_PRS},
		}
	},

	{
		{       /* After Invert 5 */
			{"SRC", ATTENTION_PAR_INT, NULL, BLOCK_SRC},
			{"PB Client1", ATTENTION_PAR_INT, NULL, BLOCK_PBF_PB1},
			{"PB Client2", ATTENTION_PAR_INT, NULL, BLOCK_PBF_PB2},
			{"RPB", ATTENTION_PAR_INT, NULL, BLOCK_RPB},
			{"PBF", ATTENTION_PAR_INT, NULL, BLOCK_PBF},
			{"QM", ATTENTION_PAR_INT, NULL, BLOCK_QM},
			{"TM", ATTENTION_PAR_INT, NULL, BLOCK_TM},
			{"MCM",  ATTENTION_PAR_INT, NULL, BLOCK_MCM},
			{"MSDM", ATTENTION_PAR_INT, NULL, BLOCK_MSDM},
			{"MSEM", ATTENTION_PAR_INT, NULL, BLOCK_MSEM},
			{"PCM", ATTENTION_PAR_INT, NULL, BLOCK_PCM},
			{"PSDM", ATTENTION_PAR_INT, NULL, BLOCK_PSDM},
			{"PSEM", ATTENTION_PAR_INT, NULL, BLOCK_PSEM},
			{"TCM", ATTENTION_PAR_INT, NULL, BLOCK_TCM},
			{"TSDM", ATTENTION_PAR_INT, NULL, BLOCK_TSDM},
			{"TSEM", ATTENTION_PAR_INT, NULL, BLOCK_TSEM},
		}
	},

	{
		{       /* After Invert 6 */
			{"UCM", ATTENTION_PAR_INT, NULL, BLOCK_UCM},
			{"USDM", ATTENTION_PAR_INT, NULL, BLOCK_USDM},
			{"USEM", ATTENTION_PAR_INT, NULL, BLOCK_USEM},
			{"XCM",	ATTENTION_PAR_INT, NULL, BLOCK_XCM},
			{"XSDM", ATTENTION_PAR_INT, NULL, BLOCK_XSDM},
			{"XSEM", ATTENTION_PAR_INT, NULL, BLOCK_XSEM},
			{"YCM",	ATTENTION_PAR_INT, NULL, BLOCK_YCM},
			{"YSDM", ATTENTION_PAR_INT, NULL, BLOCK_YSDM},
			{"YSEM", ATTENTION_PAR_INT, NULL, BLOCK_YSEM},
			{"XYLD", ATTENTION_PAR_INT, NULL, BLOCK_XYLD},
			{"TMLD", ATTENTION_PAR_INT, NULL, BLOCK_TMLD},
			{"MYLD", ATTENTION_PAR_INT, NULL, BLOCK_MULD},
			{"YULD", ATTENTION_PAR_INT, NULL, BLOCK_YULD},
			{"DORQ", ATTENTION_PAR_INT,
			 qed_dorq_attn_cb, BLOCK_DORQ},
			{"DBG", ATTENTION_PAR_INT, NULL, BLOCK_DBG},
			{"IPC",	ATTENTION_PAR_INT, NULL, BLOCK_IPC},
		}
	},

	{
		{       /* After Invert 7 */
			{"CCFC", ATTENTION_PAR_INT, NULL, BLOCK_CCFC},
			{"CDU", ATTENTION_PAR_INT, NULL, BLOCK_CDU},
			{"DMAE", ATTENTION_PAR_INT, NULL, BLOCK_DMAE},
			{"IGU", ATTENTION_PAR_INT, NULL, BLOCK_IGU},
			{"ATC", ATTENTION_PAR_INT, NULL, MAX_BLOCK_ID},
			{"CAU", ATTENTION_PAR_INT, NULL, BLOCK_CAU},
			{"PTU", ATTENTION_PAR_INT, NULL, BLOCK_PTU},
			{"PRM", ATTENTION_PAR_INT, NULL, BLOCK_PRM},
			{"TCFC", ATTENTION_PAR_INT, NULL, BLOCK_TCFC},
			{"RDIF", ATTENTION_PAR_INT, NULL, BLOCK_RDIF},
			{"TDIF", ATTENTION_PAR_INT, NULL, BLOCK_TDIF},
			{"RSS", ATTENTION_PAR_INT, NULL, BLOCK_RSS},
			{"MISC", ATTENTION_PAR_INT, NULL, BLOCK_MISC},
			{"MISCS", ATTENTION_PAR_INT, NULL, BLOCK_MISCS},
			{"PCIE", ATTENTION_PAR, NULL, BLOCK_PCIE},
			{"Vaux PCI core", ATTENTION_SINGLE, NULL, BLOCK_PGLCS},
			{"PSWRQ", ATTENTION_PAR_INT, NULL, BLOCK_PSWRQ},
		}
	},

	{
		{       /* After Invert 8 */
			{"PSWRQ (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWRQ2},
			{"PSWWR", ATTENTION_PAR_INT, NULL, BLOCK_PSWWR},
			{"PSWWR (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWWR2},
			{"PSWRD", ATTENTION_PAR_INT, NULL, BLOCK_PSWRD},
			{"PSWRD (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWRD2},
			{"PSWHST", ATTENTION_PAR_INT,
			 qed_pswhst_attn_cb, BLOCK_PSWHST},
			{"PSWHST (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWHST2},
			{"GRC",	ATTENTION_PAR_INT,
			 qed_grc_attn_cb, BLOCK_GRC},
			{"CPMU", ATTENTION_PAR_INT, NULL, BLOCK_CPMU},
			{"NCSI", ATTENTION_PAR_INT, NULL, BLOCK_NCSI},
			{"MSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"PSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"TSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"USEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"XSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"YSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"pxp_misc_mps", ATTENTION_PAR, NULL, BLOCK_PGLCS},
			{"PCIE glue/PXP Exp. ROM", ATTENTION_SINGLE,
			 NULL, BLOCK_PGLCS},
			{"PERST_B assertion", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"PERST_B deassertion", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"Reserved %d", (2 << ATTENTION_LENGTH_SHIFT),
			 NULL, MAX_BLOCK_ID},
		}
	},

	{
		{       /* After Invert 9 */
			{"MCP Latched memory", ATTENTION_PAR,
			 NULL, MAX_BLOCK_ID},
			{"MCP Latched scratchpad cache", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"MCP Latched ump_tx", ATTENTION_PAR,
			 NULL, MAX_BLOCK_ID},
			{"MCP Latched scratchpad", ATTENTION_PAR,
			 NULL, MAX_BLOCK_ID},
			{"Reserved %d", (28 << ATTENTION_LENGTH_SHIFT),
			 NULL, MAX_BLOCK_ID},
		}
	},
};

static struct aeu_invert_reg_bit *
qed_int_aeu_translate(struct qed_hwfn *p_hwfn,
		      struct aeu_invert_reg_bit *p_bit)
{
	if (!QED_IS_BB(p_hwfn->cdev))
		return p_bit;

	if (!(p_bit->flags & ATTENTION_BB_DIFFERENT))
		return p_bit;

	return &aeu_descs_special[(p_bit->flags & ATTENTION_BB_MASK) >>
				  ATTENTION_BB_SHIFT];
}

static bool qed_int_is_parity_flag(struct qed_hwfn *p_hwfn,
				   struct aeu_invert_reg_bit *p_bit)
{
	return !!(qed_int_aeu_translate(p_hwfn, p_bit)->flags &
		   ATTENTION_PARITY);
}

#define ATTN_STATE_BITS         (0xfff)
#define ATTN_BITS_MASKABLE      (0x3ff)
struct qed_sb_attn_info {
	/* Virtual & Physical address of the SB */
	struct atten_status_block       *sb_attn;
	dma_addr_t			sb_phys;

	/* Last seen running index */
	u16				index;

	/* A mask of the AEU bits resulting in a parity error */
	u32				parity_mask[NUM_ATTN_REGS];

	/* A pointer to the attention description structure */
	struct aeu_invert_reg		*p_aeu_desc;

	/* Previously asserted attentions, which are still unasserted */
	u16				known_attn;

	/* Cleanup address for the link's general hw attention */
	u32				mfw_attn_addr;
};

static inline u16 qed_attn_update_idx(struct qed_hwfn *p_hwfn,
				      struct qed_sb_attn_info *p_sb_desc)
{
	u16 rc = 0, index;

	/* Make certain HW write took affect */
	mmiowb();

	index = le16_to_cpu(p_sb_desc->sb_attn->sb_index);
	if (p_sb_desc->index != index) {
		p_sb_desc->index	= index;
		rc		      = QED_SB_ATT_IDX;
	}

	/* Make certain we got a consistent view with HW */
	mmiowb();

	return rc;
}

/**
 *  @brief qed_int_assertion - handles asserted attention bits
 *
 *  @param p_hwfn
 *  @param asserted_bits newly asserted bits
 *  @return int
 */
static int qed_int_assertion(struct qed_hwfn *p_hwfn, u16 asserted_bits)
{
	struct qed_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 igu_mask;

	/* Mask the source of the attention in the IGU */
	igu_mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE);
	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "IGU mask: 0x%08x --> 0x%08x\n",
		   igu_mask, igu_mask & ~(asserted_bits & ATTN_BITS_MASKABLE));
	igu_mask &= ~(asserted_bits & ATTN_BITS_MASKABLE);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, igu_mask);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "inner known ATTN state: 0x%04x --> 0x%04x\n",
		   sb_attn_sw->known_attn,
		   sb_attn_sw->known_attn | asserted_bits);
	sb_attn_sw->known_attn |= asserted_bits;

	/* Handle MCP events */
	if (asserted_bits & 0x100) {
		qed_mcp_handle_events(p_hwfn, p_hwfn->p_dpc_ptt);
		/* Clean the MCP attention */
		qed_wr(p_hwfn, p_hwfn->p_dpc_ptt,
		       sb_attn_sw->mfw_attn_addr, 0);
	}

	DIRECT_REG_WR((u8 __iomem *)p_hwfn->regview +
		      GTT_BAR0_MAP_REG_IGU_CMD +
		      ((IGU_CMD_ATTN_BIT_SET_UPPER -
			IGU_CMD_INT_ACK_BASE) << 3),
		      (u32)asserted_bits);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "set cmd IGU: 0x%04x\n",
		   asserted_bits);

	return 0;
}

static void qed_int_attn_print(struct qed_hwfn *p_hwfn,
			       enum block_id id,
			       enum dbg_attn_type type, bool b_clear)
{
	struct dbg_attn_block_result attn_results;
	enum dbg_status status;

	memset(&attn_results, 0, sizeof(attn_results));

	status = qed_dbg_read_attn(p_hwfn, p_hwfn->p_dpc_ptt, id, type,
				   b_clear, &attn_results);
	if (status != DBG_STATUS_OK)
		DP_NOTICE(p_hwfn,
			  "Failed to parse attention information [status: %s]\n",
			  qed_dbg_get_status_str(status));
	else
		qed_dbg_parse_attn(p_hwfn, &attn_results);
}

/**
 * @brief qed_int_deassertion_aeu_bit - handles the effects of a single
 * cause of the attention
 *
 * @param p_hwfn
 * @param p_aeu - descriptor of an AEU bit which caused the attention
 * @param aeu_en_reg - register offset of the AEU enable reg. which configured
 *  this bit to this group.
 * @param bit_index - index of this bit in the aeu_en_reg
 *
 * @return int
 */
static int
qed_int_deassertion_aeu_bit(struct qed_hwfn *p_hwfn,
			    struct aeu_invert_reg_bit *p_aeu,
			    u32 aeu_en_reg,
			    const char *p_bit_name, u32 bitmask)
{
	bool b_fatal = false;
	int rc = -EINVAL;
	u32 val;

	DP_INFO(p_hwfn, "Deasserted attention `%s'[%08x]\n",
		p_bit_name, bitmask);

	/* Call callback before clearing the interrupt status */
	if (p_aeu->cb) {
		DP_INFO(p_hwfn, "`%s (attention)': Calling Callback function\n",
			p_bit_name);
		rc = p_aeu->cb(p_hwfn);
	}

	if (rc)
		b_fatal = true;

	/* Print HW block interrupt registers */
	if (p_aeu->block_index != MAX_BLOCK_ID)
		qed_int_attn_print(p_hwfn, p_aeu->block_index,
				   ATTN_TYPE_INTERRUPT, !b_fatal);


	/* If the attention is benign, no need to prevent it */
	if (!rc)
		goto out;

	/* Prevent this Attention from being asserted in the future */
	val = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg, (val & ~bitmask));
	DP_INFO(p_hwfn, "`%s' - Disabled future attentions\n",
		p_bit_name);

out:
	return rc;
}

/**
 * @brief qed_int_deassertion_parity - handle a single parity AEU source
 *
 * @param p_hwfn
 * @param p_aeu - descriptor of an AEU bit which caused the parity
 * @param aeu_en_reg - address of the AEU enable register
 * @param bit_index
 */
static void qed_int_deassertion_parity(struct qed_hwfn *p_hwfn,
				       struct aeu_invert_reg_bit *p_aeu,
				       u32 aeu_en_reg, u8 bit_index)
{
	u32 block_id = p_aeu->block_index, mask, val;

	DP_NOTICE(p_hwfn->cdev,
		  "%s parity attention is set [address 0x%08x, bit %d]\n",
		  p_aeu->bit_name, aeu_en_reg, bit_index);

	if (block_id != MAX_BLOCK_ID) {
		qed_int_attn_print(p_hwfn, block_id, ATTN_TYPE_PARITY, false);

		/* In BB, there's a single parity bit for several blocks */
		if (block_id == BLOCK_BTB) {
			qed_int_attn_print(p_hwfn, BLOCK_OPTE,
					   ATTN_TYPE_PARITY, false);
			qed_int_attn_print(p_hwfn, BLOCK_MCP,
					   ATTN_TYPE_PARITY, false);
		}
	}

	/* Prevent this parity error from being re-asserted */
	mask = ~BIT(bit_index);
	val = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg, val & mask);
	DP_INFO(p_hwfn, "`%s' - Disabled future parity errors\n",
		p_aeu->bit_name);
}

/**
 * @brief - handles deassertion of previously asserted attentions.
 *
 * @param p_hwfn
 * @param deasserted_bits - newly deasserted bits
 * @return int
 *
 */
static int qed_int_deassertion(struct qed_hwfn  *p_hwfn,
			       u16 deasserted_bits)
{
	struct qed_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 aeu_inv_arr[NUM_ATTN_REGS], aeu_mask, aeu_en, en;
	u8 i, j, k, bit_idx;
	int rc = 0;

	/* Read the attention registers in the AEU */
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		aeu_inv_arr[i] = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
					MISC_REG_AEU_AFTER_INVERT_1_IGU +
					i * 0x4);
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "Deasserted bits [%d]: %08x\n",
			   i, aeu_inv_arr[i]);
	}

	/* Find parity attentions first */
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		struct aeu_invert_reg *p_aeu = &sb_attn_sw->p_aeu_desc[i];
		u32 parities;

		aeu_en = MISC_REG_AEU_ENABLE1_IGU_OUT_0 + i * sizeof(u32);
		en = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en);

		/* Skip register in which no parity bit is currently set */
		parities = sb_attn_sw->parity_mask[i] & aeu_inv_arr[i] & en;
		if (!parities)
			continue;

		for (j = 0, bit_idx = 0; bit_idx < 32; j++) {
			struct aeu_invert_reg_bit *p_bit = &p_aeu->bits[j];

			if (qed_int_is_parity_flag(p_hwfn, p_bit) &&
			    !!(parities & BIT(bit_idx)))
				qed_int_deassertion_parity(p_hwfn, p_bit,
							   aeu_en, bit_idx);

			bit_idx += ATTENTION_LENGTH(p_bit->flags);
		}
	}

	/* Find non-parity cause for attention and act */
	for (k = 0; k < MAX_ATTN_GRPS; k++) {
		struct aeu_invert_reg_bit *p_aeu;

		/* Handle only groups whose attention is currently deasserted */
		if (!(deasserted_bits & (1 << k)))
			continue;

		for (i = 0; i < NUM_ATTN_REGS; i++) {
			u32 bits;

			aeu_en = MISC_REG_AEU_ENABLE1_IGU_OUT_0 +
				 i * sizeof(u32) +
				 k * sizeof(u32) * NUM_ATTN_REGS;

			en = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en);
			bits = aeu_inv_arr[i] & en;

			/* Skip if no bit from this group is currently set */
			if (!bits)
				continue;

			/* Find all set bits from current register which belong
			 * to current group, making them responsible for the
			 * previous assertion.
			 */
			for (j = 0, bit_idx = 0; bit_idx < 32; j++) {
				long unsigned int bitmask;
				u8 bit, bit_len;

				p_aeu = &sb_attn_sw->p_aeu_desc[i].bits[j];
				p_aeu = qed_int_aeu_translate(p_hwfn, p_aeu);

				bit = bit_idx;
				bit_len = ATTENTION_LENGTH(p_aeu->flags);
				if (qed_int_is_parity_flag(p_hwfn, p_aeu)) {
					/* Skip Parity */
					bit++;
					bit_len--;
				}

				bitmask = bits & (((1 << bit_len) - 1) << bit);
				bitmask >>= bit;

				if (bitmask) {
					u32 flags = p_aeu->flags;
					char bit_name[30];
					u8 num;

					num = (u8)find_first_bit(&bitmask,
								 bit_len);

					/* Some bits represent more than a
					 * a single interrupt. Correctly print
					 * their name.
					 */
					if (ATTENTION_LENGTH(flags) > 2 ||
					    ((flags & ATTENTION_PAR_INT) &&
					     ATTENTION_LENGTH(flags) > 1))
						snprintf(bit_name, 30,
							 p_aeu->bit_name, num);
					else
						strncpy(bit_name,
							p_aeu->bit_name, 30);

					/* We now need to pass bitmask in its
					 * correct position.
					 */
					bitmask <<= bit;

					/* Handle source of the attention */
					qed_int_deassertion_aeu_bit(p_hwfn,
								    p_aeu,
								    aeu_en,
								    bit_name,
								    bitmask);
				}

				bit_idx += ATTENTION_LENGTH(p_aeu->flags);
			}
		}
	}

	/* Clear IGU indication for the deasserted bits */
	DIRECT_REG_WR((u8 __iomem *)p_hwfn->regview +
				    GTT_BAR0_MAP_REG_IGU_CMD +
				    ((IGU_CMD_ATTN_BIT_CLR_UPPER -
				      IGU_CMD_INT_ACK_BASE) << 3),
				    ~((u32)deasserted_bits));

	/* Unmask deasserted attentions in IGU */
	aeu_mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE);
	aeu_mask |= (deasserted_bits & ATTN_BITS_MASKABLE);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, aeu_mask);

	/* Clear deassertion from inner state */
	sb_attn_sw->known_attn &= ~deasserted_bits;

	return rc;
}

static int qed_int_attentions(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_attn_info *p_sb_attn_sw = p_hwfn->p_sb_attn;
	struct atten_status_block *p_sb_attn = p_sb_attn_sw->sb_attn;
	u32 attn_bits = 0, attn_acks = 0;
	u16 asserted_bits, deasserted_bits;
	__le16 index;
	int rc = 0;

	/* Read current attention bits/acks - safeguard against attentions
	 * by guaranting work on a synchronized timeframe
	 */
	do {
		index = p_sb_attn->sb_index;
		attn_bits = le32_to_cpu(p_sb_attn->atten_bits);
		attn_acks = le32_to_cpu(p_sb_attn->atten_ack);
	} while (index != p_sb_attn->sb_index);
	p_sb_attn->sb_index = index;

	/* Attention / Deassertion are meaningful (and in correct state)
	 * only when they differ and consistent with known state - deassertion
	 * when previous attention & current ack, and assertion when current
	 * attention with no previous attention
	 */
	asserted_bits = (attn_bits & ~attn_acks & ATTN_STATE_BITS) &
		~p_sb_attn_sw->known_attn;
	deasserted_bits = (~attn_bits & attn_acks & ATTN_STATE_BITS) &
		p_sb_attn_sw->known_attn;

	if ((asserted_bits & ~0x100) || (deasserted_bits & ~0x100)) {
		DP_INFO(p_hwfn,
			"Attention: Index: 0x%04x, Bits: 0x%08x, Acks: 0x%08x, asserted: 0x%04x, De-asserted 0x%04x [Prev. known: 0x%04x]\n",
			index, attn_bits, attn_acks, asserted_bits,
			deasserted_bits, p_sb_attn_sw->known_attn);
	} else if (asserted_bits == 0x100) {
		DP_INFO(p_hwfn, "MFW indication via attention\n");
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "MFW indication [deassertion]\n");
	}

	if (asserted_bits) {
		rc = qed_int_assertion(p_hwfn, asserted_bits);
		if (rc)
			return rc;
	}

	if (deasserted_bits)
		rc = qed_int_deassertion(p_hwfn, deasserted_bits);

	return rc;
}

static void qed_sb_ack_attn(struct qed_hwfn *p_hwfn,
			    void __iomem *igu_addr, u32 ack_cons)
{
	struct igu_prod_cons_update igu_ack = { 0 };

	igu_ack.sb_id_and_flags =
		((ack_cons << IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT) |
		 (1 << IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT) |
		 (IGU_INT_NOP << IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT) |
		 (IGU_SEG_ACCESS_ATTN <<
		  IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT));

	DIRECT_REG_WR(igu_addr, igu_ack.sb_id_and_flags);

	/* Both segments (interrupts & acks) are written to same place address;
	 * Need to guarantee all commands will be received (in-order) by HW.
	 */
	mmiowb();
	barrier();
}

void qed_int_sp_dpc(unsigned long hwfn_cookie)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)hwfn_cookie;
	struct qed_pi_info *pi_info = NULL;
	struct qed_sb_attn_info *sb_attn;
	struct qed_sb_info *sb_info;
	int arr_size;
	u16 rc = 0;

	if (!p_hwfn->p_sp_sb) {
		DP_ERR(p_hwfn->cdev, "DPC called - no p_sp_sb\n");
		return;
	}

	sb_info = &p_hwfn->p_sp_sb->sb_info;
	arr_size = ARRAY_SIZE(p_hwfn->p_sp_sb->pi_info_arr);
	if (!sb_info) {
		DP_ERR(p_hwfn->cdev,
		       "Status block is NULL - cannot ack interrupts\n");
		return;
	}

	if (!p_hwfn->p_sb_attn) {
		DP_ERR(p_hwfn->cdev, "DPC called - no p_sb_attn");
		return;
	}
	sb_attn = p_hwfn->p_sb_attn;

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "DPC Called! (hwfn %p %d)\n",
		   p_hwfn, p_hwfn->my_id);

	/* Disable ack for def status block. Required both for msix +
	 * inta in non-mask mode, in inta does no harm.
	 */
	qed_sb_ack(sb_info, IGU_INT_DISABLE, 0);

	/* Gather Interrupts/Attentions information */
	if (!sb_info->sb_virt) {
		DP_ERR(p_hwfn->cdev,
		       "Interrupt Status block is NULL - cannot check for new interrupts!\n");
	} else {
		u32 tmp_index = sb_info->sb_ack;

		rc = qed_sb_update_sb_idx(sb_info);
		DP_VERBOSE(p_hwfn->cdev, NETIF_MSG_INTR,
			   "Interrupt indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_info->sb_ack);
	}

	if (!sb_attn || !sb_attn->sb_attn) {
		DP_ERR(p_hwfn->cdev,
		       "Attentions Status block is NULL - cannot check for new attentions!\n");
	} else {
		u16 tmp_index = sb_attn->index;

		rc |= qed_attn_update_idx(p_hwfn, sb_attn);
		DP_VERBOSE(p_hwfn->cdev, NETIF_MSG_INTR,
			   "Attention indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_attn->index);
	}

	/* Check if we expect interrupts at this time. if not just ack them */
	if (!(rc & QED_SB_EVENT_MASK)) {
		qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	/* Check the validity of the DPC ptt. If not ack interrupts and fail */
	if (!p_hwfn->p_dpc_ptt) {
		DP_NOTICE(p_hwfn->cdev, "Failed to allocate PTT\n");
		qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	if (rc & QED_SB_ATT_IDX)
		qed_int_attentions(p_hwfn);

	if (rc & QED_SB_IDX) {
		int pi;

		/* Look for a free index */
		for (pi = 0; pi < arr_size; pi++) {
			pi_info = &p_hwfn->p_sp_sb->pi_info_arr[pi];
			if (pi_info->comp_cb)
				pi_info->comp_cb(p_hwfn, pi_info->cookie);
		}
	}

	if (sb_attn && (rc & QED_SB_ATT_IDX))
		/* This should be done before the interrupts are enabled,
		 * since otherwise a new attention will be generated.
		 */
		qed_sb_ack_attn(p_hwfn, sb_info->igu_addr, sb_attn->index);

	qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
}

static void qed_int_sb_attn_free(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_attn_info *p_sb = p_hwfn->p_sb_attn;

	if (!p_sb)
		return;

	if (p_sb->sb_attn)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  SB_ATTN_ALIGNED_SIZE(p_hwfn),
				  p_sb->sb_attn, p_sb->sb_phys);
	kfree(p_sb);
	p_hwfn->p_sb_attn = NULL;
}

static void qed_int_sb_attn_setup(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt)
{
	struct qed_sb_attn_info *sb_info = p_hwfn->p_sb_attn;

	memset(sb_info->sb_attn, 0, sizeof(*sb_info->sb_attn));

	sb_info->index = 0;
	sb_info->known_attn = 0;

	/* Configure Attention Status Block in IGU */
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_L,
	       lower_32_bits(p_hwfn->p_sb_attn->sb_phys));
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_H,
	       upper_32_bits(p_hwfn->p_sb_attn->sb_phys));
}

static void qed_int_sb_attn_init(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 void *sb_virt_addr, dma_addr_t sb_phy_addr)
{
	struct qed_sb_attn_info *sb_info = p_hwfn->p_sb_attn;
	int i, j, k;

	sb_info->sb_attn = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	/* Set the pointer to the AEU descriptors */
	sb_info->p_aeu_desc = aeu_descs;

	/* Calculate Parity Masks */
	memset(sb_info->parity_mask, 0, sizeof(u32) * NUM_ATTN_REGS);
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		/* j is array index, k is bit index */
		for (j = 0, k = 0; k < 32; j++) {
			struct aeu_invert_reg_bit *p_aeu;

			p_aeu = &aeu_descs[i].bits[j];
			if (qed_int_is_parity_flag(p_hwfn, p_aeu))
				sb_info->parity_mask[i] |= 1 << k;

			k += ATTENTION_LENGTH(p_aeu->flags);
		}
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "Attn Mask [Reg %d]: 0x%08x\n",
			   i, sb_info->parity_mask[i]);
	}

	/* Set the address of cleanup for the mcp attention */
	sb_info->mfw_attn_addr = (p_hwfn->rel_pf_id << 3) +
				 MISC_REG_AEU_GENERAL_ATTN_0;

	qed_int_sb_attn_setup(p_hwfn, p_ptt);
}

static int qed_int_sb_attn_alloc(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	struct qed_sb_attn_info *p_sb;
	dma_addr_t p_phys = 0;
	void *p_virt;

	/* SB struct */
	p_sb = kmalloc(sizeof(*p_sb), GFP_KERNEL);
	if (!p_sb)
		return -ENOMEM;

	/* SB ring  */
	p_virt = dma_alloc_coherent(&cdev->pdev->dev,
				    SB_ATTN_ALIGNED_SIZE(p_hwfn),
				    &p_phys, GFP_KERNEL);

	if (!p_virt) {
		kfree(p_sb);
		return -ENOMEM;
	}

	/* Attention setup */
	p_hwfn->p_sb_attn = p_sb;
	qed_int_sb_attn_init(p_hwfn, p_ptt, p_virt, p_phys);

	return 0;
}

/* coalescing timeout = timeset << (timer_res + 1) */
#define QED_CAU_DEF_RX_USECS 24
#define QED_CAU_DEF_TX_USECS 48

void qed_init_cau_sb_entry(struct qed_hwfn *p_hwfn,
			   struct cau_sb_entry *p_sb_entry,
			   u8 pf_id, u16 vf_number, u8 vf_valid)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	u32 cau_state;
	u8 timer_res;

	memset(p_sb_entry, 0, sizeof(*p_sb_entry));

	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_PF_NUMBER, pf_id);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_NUMBER, vf_number);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_VALID, vf_valid);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET0, 0x7F);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET1, 0x7F);

	cau_state = CAU_HC_DISABLE_STATE;

	if (cdev->int_coalescing_mode == QED_COAL_MODE_ENABLE) {
		cau_state = CAU_HC_ENABLE_STATE;
		if (!cdev->rx_coalesce_usecs)
			cdev->rx_coalesce_usecs = QED_CAU_DEF_RX_USECS;
		if (!cdev->tx_coalesce_usecs)
			cdev->tx_coalesce_usecs = QED_CAU_DEF_TX_USECS;
	}

	/* Coalesce = (timeset << timer-res), timeset is 7bit wide */
	if (cdev->rx_coalesce_usecs <= 0x7F)
		timer_res = 0;
	else if (cdev->rx_coalesce_usecs <= 0xFF)
		timer_res = 1;
	else
		timer_res = 2;
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES0, timer_res);

	if (cdev->tx_coalesce_usecs <= 0x7F)
		timer_res = 0;
	else if (cdev->tx_coalesce_usecs <= 0xFF)
		timer_res = 1;
	else
		timer_res = 2;
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES1, timer_res);

	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE0, cau_state);
	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE1, cau_state);
}

static void qed_int_cau_conf_pi(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				u16 igu_sb_id,
				u32 pi_index,
				enum qed_coalescing_fsm coalescing_fsm,
				u8 timeset)
{
	struct cau_pi_entry pi_entry;
	u32 sb_offset, pi_offset;

	if (IS_VF(p_hwfn->cdev))
		return;

	sb_offset = igu_sb_id * PIS_PER_SB;
	memset(&pi_entry, 0, sizeof(struct cau_pi_entry));

	SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_PI_TIMESET, timeset);
	if (coalescing_fsm == QED_COAL_RX_STATE_MACHINE)
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 0);
	else
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 1);

	pi_offset = sb_offset + pi_index;
	if (p_hwfn->hw_init_done) {
		qed_wr(p_hwfn, p_ptt,
		       CAU_REG_PI_MEMORY + pi_offset * sizeof(u32),
		       *((u32 *)&(pi_entry)));
	} else {
		STORE_RT_REG(p_hwfn,
			     CAU_REG_PI_MEMORY_RT_OFFSET + pi_offset,
			     *((u32 *)&(pi_entry)));
	}
}

void qed_int_cau_conf_sb(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 dma_addr_t sb_phys,
			 u16 igu_sb_id, u16 vf_number, u8 vf_valid)
{
	struct cau_sb_entry sb_entry;

	qed_init_cau_sb_entry(p_hwfn, &sb_entry, p_hwfn->rel_pf_id,
			      vf_number, vf_valid);

	if (p_hwfn->hw_init_done) {
		/* Wide-bus, initialize via DMAE */
		u64 phys_addr = (u64)sb_phys;

		qed_dmae_host2grc(p_hwfn, p_ptt, (u64)(uintptr_t)&phys_addr,
				  CAU_REG_SB_ADDR_MEMORY +
				  igu_sb_id * sizeof(u64), 2, 0);
		qed_dmae_host2grc(p_hwfn, p_ptt, (u64)(uintptr_t)&sb_entry,
				  CAU_REG_SB_VAR_MEMORY +
				  igu_sb_id * sizeof(u64), 2, 0);
	} else {
		/* Initialize Status Block Address */
		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_ADDR_MEMORY_RT_OFFSET +
				 igu_sb_id * 2,
				 sb_phys);

		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_VAR_MEMORY_RT_OFFSET +
				 igu_sb_id * 2,
				 sb_entry);
	}

	/* Configure pi coalescing if set */
	if (p_hwfn->cdev->int_coalescing_mode == QED_COAL_MODE_ENABLE) {
		u8 num_tc = p_hwfn->hw_info.num_hw_tc;
		u8 timeset, timer_res;
		u8 i;

		/* timeset = (coalesce >> timer-res), timeset is 7bit wide */
		if (p_hwfn->cdev->rx_coalesce_usecs <= 0x7F)
			timer_res = 0;
		else if (p_hwfn->cdev->rx_coalesce_usecs <= 0xFF)
			timer_res = 1;
		else
			timer_res = 2;
		timeset = (u8)(p_hwfn->cdev->rx_coalesce_usecs >> timer_res);
		qed_int_cau_conf_pi(p_hwfn, p_ptt, igu_sb_id, RX_PI,
				    QED_COAL_RX_STATE_MACHINE, timeset);

		if (p_hwfn->cdev->tx_coalesce_usecs <= 0x7F)
			timer_res = 0;
		else if (p_hwfn->cdev->tx_coalesce_usecs <= 0xFF)
			timer_res = 1;
		else
			timer_res = 2;
		timeset = (u8)(p_hwfn->cdev->tx_coalesce_usecs >> timer_res);
		for (i = 0; i < num_tc; i++) {
			qed_int_cau_conf_pi(p_hwfn, p_ptt,
					    igu_sb_id, TX_PI(i),
					    QED_COAL_TX_STATE_MACHINE,
					    timeset);
		}
	}
}

void qed_int_sb_setup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, struct qed_sb_info *sb_info)
{
	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	memset(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	if (IS_PF(p_hwfn->cdev))
		qed_int_cau_conf_sb(p_hwfn, p_ptt, sb_info->sb_phys,
				    sb_info->igu_sb_id, 0, 0);
}

static u16 qed_get_pf_igu_sb_id(struct qed_hwfn *p_hwfn, u16 vector_id)
{
	struct qed_igu_block *p_block;
	u16 igu_id;

	for (igu_id = 0; igu_id < QED_MAPPING_MEMORY_SIZE(p_hwfn->cdev);
	     igu_id++) {
		p_block = &p_hwfn->hw_info.p_igu_info->entry[igu_id];

		if (!(p_block->status & QED_IGU_STATUS_VALID) ||
		    !p_block->is_pf ||
		    p_block->vector_number != vector_id)
			continue;

		return igu_id;
	}

	return QED_SB_INVALID_IDX;
}

static u16 qed_get_igu_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id)
{
	u16 igu_sb_id;

	/* Assuming continuous set of IGU SBs dedicated for given PF */
	if (sb_id == QED_SP_SB_ID)
		igu_sb_id = p_hwfn->hw_info.p_igu_info->igu_dsb_id;
	else if (IS_PF(p_hwfn->cdev))
		igu_sb_id = qed_get_pf_igu_sb_id(p_hwfn, sb_id + 1);
	else
		igu_sb_id = qed_vf_get_igu_sb_id(p_hwfn, sb_id);

	if (sb_id == QED_SP_SB_ID)
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "Slowpath SB index in IGU is 0x%04x\n", igu_sb_id);
	else
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "SB [%04x] <--> IGU SB [%04x]\n", sb_id, igu_sb_id);

	return igu_sb_id;
}

int qed_int_sb_init(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_sb_info *sb_info,
		    void *sb_virt_addr, dma_addr_t sb_phy_addr, u16 sb_id)
{
	sb_info->sb_virt = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	sb_info->igu_sb_id = qed_get_igu_sb_id(p_hwfn, sb_id);

	if (sb_id != QED_SP_SB_ID) {
		p_hwfn->sbs_info[sb_id] = sb_info;
		p_hwfn->num_sbs++;
	}

	sb_info->cdev = p_hwfn->cdev;

	/* The igu address will hold the absolute address that needs to be
	 * written to for a specific status block
	 */
	if (IS_PF(p_hwfn->cdev)) {
		sb_info->igu_addr = (u8 __iomem *)p_hwfn->regview +
						  GTT_BAR0_MAP_REG_IGU_CMD +
						  (sb_info->igu_sb_id << 3);
	} else {
		sb_info->igu_addr = (u8 __iomem *)p_hwfn->regview +
						  PXP_VF_BAR0_START_IGU +
						  ((IGU_CMD_INT_ACK_BASE +
						    sb_info->igu_sb_id) << 3);
	}

	sb_info->flags |= QED_SB_INFO_INIT;

	qed_int_sb_setup(p_hwfn, p_ptt, sb_info);

	return 0;
}

int qed_int_sb_release(struct qed_hwfn *p_hwfn,
		       struct qed_sb_info *sb_info, u16 sb_id)
{
	if (sb_id == QED_SP_SB_ID) {
		DP_ERR(p_hwfn, "Do Not free sp sb using this function");
		return -EINVAL;
	}

	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	memset(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	if (p_hwfn->sbs_info[sb_id] != NULL) {
		p_hwfn->sbs_info[sb_id] = NULL;
		p_hwfn->num_sbs--;
	}

	return 0;
}

static void qed_int_sp_sb_free(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_sp_info *p_sb = p_hwfn->p_sp_sb;

	if (!p_sb)
		return;

	if (p_sb->sb_info.sb_virt)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  SB_ALIGNED_SIZE(p_hwfn),
				  p_sb->sb_info.sb_virt,
				  p_sb->sb_info.sb_phys);
	kfree(p_sb);
	p_hwfn->p_sp_sb = NULL;
}

static int qed_int_sp_sb_alloc(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_sb_sp_info *p_sb;
	dma_addr_t p_phys = 0;
	void *p_virt;

	/* SB struct */
	p_sb = kmalloc(sizeof(*p_sb), GFP_KERNEL);
	if (!p_sb)
		return -ENOMEM;

	/* SB ring  */
	p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    SB_ALIGNED_SIZE(p_hwfn),
				    &p_phys, GFP_KERNEL);
	if (!p_virt) {
		kfree(p_sb);
		return -ENOMEM;
	}

	/* Status Block setup */
	p_hwfn->p_sp_sb = p_sb;
	qed_int_sb_init(p_hwfn, p_ptt, &p_sb->sb_info, p_virt,
			p_phys, QED_SP_SB_ID);

	memset(p_sb->pi_info_arr, 0, sizeof(p_sb->pi_info_arr));

	return 0;
}

int qed_int_register_cb(struct qed_hwfn *p_hwfn,
			qed_int_comp_cb_t comp_cb,
			void *cookie, u8 *sb_idx, __le16 **p_fw_cons)
{
	struct qed_sb_sp_info *p_sp_sb = p_hwfn->p_sp_sb;
	int rc = -ENOMEM;
	u8 pi;

	/* Look for a free index */
	for (pi = 0; pi < ARRAY_SIZE(p_sp_sb->pi_info_arr); pi++) {
		if (p_sp_sb->pi_info_arr[pi].comp_cb)
			continue;

		p_sp_sb->pi_info_arr[pi].comp_cb = comp_cb;
		p_sp_sb->pi_info_arr[pi].cookie = cookie;
		*sb_idx = pi;
		*p_fw_cons = &p_sp_sb->sb_info.sb_virt->pi_array[pi];
		rc = 0;
		break;
	}

	return rc;
}

int qed_int_unregister_cb(struct qed_hwfn *p_hwfn, u8 pi)
{
	struct qed_sb_sp_info *p_sp_sb = p_hwfn->p_sp_sb;

	if (p_sp_sb->pi_info_arr[pi].comp_cb == NULL)
		return -ENOMEM;

	p_sp_sb->pi_info_arr[pi].comp_cb = NULL;
	p_sp_sb->pi_info_arr[pi].cookie = NULL;

	return 0;
}

u16 qed_int_get_sp_sb_id(struct qed_hwfn *p_hwfn)
{
	return p_hwfn->p_sp_sb->sb_info.igu_sb_id;
}

void qed_int_igu_enable_int(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, enum qed_int_mode int_mode)
{
	u32 igu_pf_conf = IGU_PF_CONF_FUNC_EN | IGU_PF_CONF_ATTN_BIT_EN;

	p_hwfn->cdev->int_mode = int_mode;
	switch (p_hwfn->cdev->int_mode) {
	case QED_INT_MODE_INTA:
		igu_pf_conf |= IGU_PF_CONF_INT_LINE_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case QED_INT_MODE_MSI:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case QED_INT_MODE_MSIX:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		break;
	case QED_INT_MODE_POLL:
		break;
	}

	qed_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, igu_pf_conf);
}

static void qed_int_igu_enable_attn(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt)
{

	/* Configure AEU signal change to produce attentions */
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTENTION_ENABLE, 0);
	qed_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0xfff);
	qed_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0xfff);
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTENTION_ENABLE, 0xfff);

	/* Flush the writes to IGU */
	mmiowb();

	/* Unmask AEU signals toward IGU */
	qed_wr(p_hwfn, p_ptt, MISC_REG_AEU_MASK_ATTN_IGU, 0xff);
}

int
qed_int_igu_enable(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt, enum qed_int_mode int_mode)
{
	int rc = 0;

	qed_int_igu_enable_attn(p_hwfn, p_ptt);

	if ((int_mode != QED_INT_MODE_INTA) || IS_LEAD_HWFN(p_hwfn)) {
		rc = qed_slowpath_irq_req(p_hwfn);
		if (rc) {
			DP_NOTICE(p_hwfn, "Slowpath IRQ request failed\n");
			return -EINVAL;
		}
		p_hwfn->b_int_requested = true;
	}
	/* Enable interrupt Generation */
	qed_int_igu_enable_int(p_hwfn, p_ptt, int_mode);
	p_hwfn->b_int_enabled = 1;

	return rc;
}

void qed_int_igu_disable_int(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	p_hwfn->b_int_enabled = 0;

	if (IS_VF(p_hwfn->cdev))
		return;

	qed_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, 0);
}

#define IGU_CLEANUP_SLEEP_LENGTH                (1000)
static void qed_int_igu_cleanup_sb(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   u16 igu_sb_id,
				   bool cleanup_set, u16 opaque_fid)
{
	u32 cmd_ctrl = 0, val = 0, sb_bit = 0, sb_bit_addr = 0, data = 0;
	u32 pxp_addr = IGU_CMD_INT_ACK_BASE + igu_sb_id;
	u32 sleep_cnt = IGU_CLEANUP_SLEEP_LENGTH;

	/* Set the data field */
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_SET, cleanup_set ? 1 : 0);
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_TYPE, 0);
	SET_FIELD(data, IGU_CLEANUP_COMMAND_TYPE, IGU_COMMAND_TYPE_SET);

	/* Set the control register */
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_PXP_ADDR, pxp_addr);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_FID, opaque_fid);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_TYPE, IGU_CTRL_CMD_TYPE_WR);

	qed_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_32LSB_DATA, data);

	barrier();

	qed_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl);

	/* Flush the write to IGU */
	mmiowb();

	/* calculate where to read the status bit from */
	sb_bit = 1 << (igu_sb_id % 32);
	sb_bit_addr = igu_sb_id / 32 * sizeof(u32);

	sb_bit_addr += IGU_REG_CLEANUP_STATUS_0;

	/* Now wait for the command to complete */
	do {
		val = qed_rd(p_hwfn, p_ptt, sb_bit_addr);

		if ((val & sb_bit) == (cleanup_set ? sb_bit : 0))
			break;

		usleep_range(5000, 10000);
	} while (--sleep_cnt);

	if (!sleep_cnt)
		DP_NOTICE(p_hwfn,
			  "Timeout waiting for clear status 0x%08x [for sb %d]\n",
			  val, igu_sb_id);
}

void qed_int_igu_init_pure_rt_single(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u16 igu_sb_id, u16 opaque, bool b_set)
{
	int pi, i;

	/* Set */
	if (b_set)
		qed_int_igu_cleanup_sb(p_hwfn, p_ptt, igu_sb_id, 1, opaque);

	/* Clear */
	qed_int_igu_cleanup_sb(p_hwfn, p_ptt, igu_sb_id, 0, opaque);

	/* Wait for the IGU SB to cleanup */
	for (i = 0; i < IGU_CLEANUP_SLEEP_LENGTH; i++) {
		u32 val;

		val = qed_rd(p_hwfn, p_ptt,
			     IGU_REG_WRITE_DONE_PENDING +
			     ((igu_sb_id / 32) * 4));
		if (val & BIT((igu_sb_id % 32)))
			usleep_range(10, 20);
		else
			break;
	}
	if (i == IGU_CLEANUP_SLEEP_LENGTH)
		DP_NOTICE(p_hwfn,
			  "Failed SB[0x%08x] still appearing in WRITE_DONE_PENDING\n",
			  igu_sb_id);

	/* Clear the CAU for the SB */
	for (pi = 0; pi < 12; pi++)
		qed_wr(p_hwfn, p_ptt,
		       CAU_REG_PI_MEMORY + (igu_sb_id * 12 + pi) * 4, 0);
}

void qed_int_igu_init_pure_rt(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      bool b_set, bool b_slowpath)
{
	u32 igu_base_sb = p_hwfn->hw_info.p_igu_info->igu_base_sb;
	u32 igu_sb_cnt = p_hwfn->hw_info.p_igu_info->igu_sb_cnt;
	u32 igu_sb_id = 0, val = 0;

	val = qed_rd(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION);
	val |= IGU_REG_BLOCK_CONFIGURATION_VF_CLEANUP_EN;
	val &= ~IGU_REG_BLOCK_CONFIGURATION_PXP_TPH_INTERFACE_EN;
	qed_wr(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION, val);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "IGU cleaning SBs [%d,...,%d]\n",
		   igu_base_sb, igu_base_sb + igu_sb_cnt - 1);

	for (igu_sb_id = igu_base_sb; igu_sb_id < igu_base_sb + igu_sb_cnt;
	     igu_sb_id++)
		qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt, igu_sb_id,
						p_hwfn->hw_info.opaque_fid,
						b_set);

	if (!b_slowpath)
		return;

	igu_sb_id = p_hwfn->hw_info.p_igu_info->igu_dsb_id;
	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "IGU cleaning slowpath SB [%d]\n", igu_sb_id);
	qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt, igu_sb_id,
					p_hwfn->hw_info.opaque_fid, b_set);
}

static void qed_int_igu_read_cam_block(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt, u16 igu_sb_id)
{
	u32 val = qed_rd(p_hwfn, p_ptt,
			 IGU_REG_MAPPING_MEMORY + sizeof(u32) * igu_sb_id);
	struct qed_igu_block *p_block;

	p_block = &p_hwfn->hw_info.p_igu_info->entry[igu_sb_id];

	/* Fill the block information */
	p_block->function_id = GET_FIELD(val, IGU_MAPPING_LINE_FUNCTION_NUMBER);
	p_block->is_pf = GET_FIELD(val, IGU_MAPPING_LINE_PF_VALID);
	p_block->vector_number = GET_FIELD(val, IGU_MAPPING_LINE_VECTOR_NUMBER);
}

int qed_int_igu_read_cam(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_igu_info *p_igu_info;
	struct qed_igu_block *p_block;
	u32 min_vf = 0, max_vf = 0;
	u16 igu_sb_id;

	p_hwfn->hw_info.p_igu_info = kzalloc(sizeof(*p_igu_info), GFP_KERNEL);
	if (!p_hwfn->hw_info.p_igu_info)
		return -ENOMEM;

	p_igu_info = p_hwfn->hw_info.p_igu_info;

       /* Initialize base sb / sb cnt for PFs and VFs */
	p_igu_info->igu_base_sb = 0xffff;
	p_igu_info->igu_sb_cnt = 0;
	p_igu_info->igu_base_sb_iov = 0xffff;

	/* Distinguish between existent and non-existent default SB */
	p_igu_info->igu_dsb_id = QED_SB_INVALID_IDX;

	/* Find the range of VF ids whose SB belong to this PF */
	if (p_hwfn->cdev->p_iov_info) {
		struct qed_hw_sriov_info *p_iov = p_hwfn->cdev->p_iov_info;

		min_vf	= p_iov->first_vf_in_pf;
		max_vf	= p_iov->first_vf_in_pf + p_iov->total_vfs;
	}

	for (igu_sb_id = 0;
	     igu_sb_id < QED_MAPPING_MEMORY_SIZE(p_hwfn->cdev); igu_sb_id++) {
		/* Read current entry; Notice it might not belong to this PF */
		qed_int_igu_read_cam_block(p_hwfn, p_ptt, igu_sb_id);
		p_block = &p_igu_info->entry[igu_sb_id];

		if ((p_block->is_pf) &&
		    (p_block->function_id == p_hwfn->rel_pf_id)) {
			p_block->status = QED_IGU_STATUS_PF |
					  QED_IGU_STATUS_VALID |
					  QED_IGU_STATUS_FREE;

			if (p_igu_info->igu_dsb_id != QED_SB_INVALID_IDX) {
				if (p_igu_info->igu_base_sb == 0xffff)
					p_igu_info->igu_base_sb = igu_sb_id;
				p_igu_info->igu_sb_cnt++;
			}
		} else if (!(p_block->is_pf) &&
			   (p_block->function_id >= min_vf) &&
			   (p_block->function_id < max_vf)) {
			/* Available for VFs of this PF */
			p_block->status = QED_IGU_STATUS_VALID |
					  QED_IGU_STATUS_FREE;

			if (p_igu_info->igu_base_sb_iov == 0xffff)
				p_igu_info->igu_base_sb_iov = igu_sb_id;
			p_igu_info->free_blks++;
		}

		/* Mark the First entry belonging to the PF or its VFs
		 * as the default SB.
		 */
		if ((p_block->status & QED_IGU_STATUS_VALID) &&
		    (p_igu_info->igu_dsb_id == QED_SB_INVALID_IDX)) {
			p_igu_info->igu_dsb_id = igu_sb_id;
			p_block->status |= QED_IGU_STATUS_DSB;
		}

		/* limit number of prints by having each PF print only its
		 * entries with the exception of PF0 which would print
		 * everything.
		 */
		if ((p_block->status & QED_IGU_STATUS_VALID) ||
		    (p_hwfn->abs_pf_id == 0)) {
			DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
				   "IGU_BLOCK: [SB 0x%04x] func_id = %d is_pf = %d vector_num = 0x%x\n",
				   igu_sb_id, p_block->function_id,
				   p_block->is_pf, p_block->vector_number);
		}
	}

	if (p_igu_info->igu_dsb_id == QED_SB_INVALID_IDX) {
		DP_NOTICE(p_hwfn,
			  "IGU CAM returned invalid values igu_dsb_id=0x%x\n",
			  p_igu_info->igu_dsb_id);
		return -EINVAL;
	}

	/* All non default SB are considered free at this point */
	p_igu_info->igu_sb_cnt_iov = p_igu_info->free_blks;

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "igu_dsb_id=0x%x, num Free SBs - PF: %04x VF: %04x\n",
		   p_igu_info->igu_dsb_id,
		   p_igu_info->igu_sb_cnt, p_igu_info->igu_sb_cnt_iov);

	return 0;
}

/**
 * @brief Initialize igu runtime registers
 *
 * @param p_hwfn
 */
void qed_int_igu_init_rt(struct qed_hwfn *p_hwfn)
{
	u32 igu_pf_conf = IGU_PF_CONF_FUNC_EN;

	STORE_RT_REG(p_hwfn, IGU_REG_PF_CONFIGURATION_RT_OFFSET, igu_pf_conf);
}

u64 qed_int_igu_read_sisr_reg(struct qed_hwfn *p_hwfn)
{
	u32 lsb_igu_cmd_addr = IGU_REG_SISR_MDPC_WMASK_LSB_UPPER -
			       IGU_CMD_INT_ACK_BASE;
	u32 msb_igu_cmd_addr = IGU_REG_SISR_MDPC_WMASK_MSB_UPPER -
			       IGU_CMD_INT_ACK_BASE;
	u32 intr_status_hi = 0, intr_status_lo = 0;
	u64 intr_status = 0;

	intr_status_lo = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				lsb_igu_cmd_addr * 8);
	intr_status_hi = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				msb_igu_cmd_addr * 8);
	intr_status = ((u64)intr_status_hi << 32) + (u64)intr_status_lo;

	return intr_status;
}

static void qed_int_sp_dpc_setup(struct qed_hwfn *p_hwfn)
{
	tasklet_init(p_hwfn->sp_dpc,
		     qed_int_sp_dpc, (unsigned long)p_hwfn);
	p_hwfn->b_sp_dpc_enabled = true;
}

static int qed_int_sp_dpc_alloc(struct qed_hwfn *p_hwfn)
{
	p_hwfn->sp_dpc = kmalloc(sizeof(*p_hwfn->sp_dpc), GFP_KERNEL);
	if (!p_hwfn->sp_dpc)
		return -ENOMEM;

	return 0;
}

static void qed_int_sp_dpc_free(struct qed_hwfn *p_hwfn)
{
	kfree(p_hwfn->sp_dpc);
	p_hwfn->sp_dpc = NULL;
}

int qed_int_alloc(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	int rc = 0;

	rc = qed_int_sp_dpc_alloc(p_hwfn);
	if (rc)
		return rc;

	rc = qed_int_sp_sb_alloc(p_hwfn, p_ptt);
	if (rc)
		return rc;

	rc = qed_int_sb_attn_alloc(p_hwfn, p_ptt);

	return rc;
}

void qed_int_free(struct qed_hwfn *p_hwfn)
{
	qed_int_sp_sb_free(p_hwfn);
	qed_int_sb_attn_free(p_hwfn);
	qed_int_sp_dpc_free(p_hwfn);
}

void qed_int_setup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	qed_int_sb_setup(p_hwfn, p_ptt, &p_hwfn->p_sp_sb->sb_info);
	qed_int_sb_attn_setup(p_hwfn, p_ptt);
	qed_int_sp_dpc_setup(p_hwfn);
}

void qed_int_get_num_sbs(struct qed_hwfn	*p_hwfn,
			 struct qed_sb_cnt_info *p_sb_cnt_info)
{
	struct qed_igu_info *info = p_hwfn->hw_info.p_igu_info;

	if (!info || !p_sb_cnt_info)
		return;

	p_sb_cnt_info->sb_cnt		= info->igu_sb_cnt;
	p_sb_cnt_info->sb_iov_cnt	= info->igu_sb_cnt_iov;
	p_sb_cnt_info->sb_free_blk	= info->free_blks;
}

u16 qed_int_queue_id_from_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id)
{
	struct qed_igu_info *p_info = p_hwfn->hw_info.p_igu_info;

	/* Determine origin of SB id */
	if ((sb_id >= p_info->igu_base_sb) &&
	    (sb_id < p_info->igu_base_sb + p_info->igu_sb_cnt)) {
		return sb_id - p_info->igu_base_sb;
	} else if ((sb_id >= p_info->igu_base_sb_iov) &&
		   (sb_id < p_info->igu_base_sb_iov + p_info->igu_sb_cnt_iov)) {
		/* We want the first VF queue to be adjacent to the
		 * last PF queue. Since L2 queues can be partial to
		 * SBs, we'll use the feature instead.
		 */
		return sb_id - p_info->igu_base_sb_iov +
		       FEAT_NUM(p_hwfn, QED_PF_L2_QUE);
	} else {
		DP_NOTICE(p_hwfn, "SB %d not in range for function\n", sb_id);
		return 0;
	}
}

void qed_int_disable_post_isr_release(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i)
		cdev->hwfns[i].b_int_requested = false;
}

int qed_int_set_timer_res(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			  u8 timer_res, u16 sb_id, bool tx)
{
	struct cau_sb_entry sb_entry;
	int rc;

	if (!p_hwfn->hw_init_done) {
		DP_ERR(p_hwfn, "hardware not initialized yet\n");
		return -EINVAL;
	}

	rc = qed_dmae_grc2host(p_hwfn, p_ptt, CAU_REG_SB_VAR_MEMORY +
			       sb_id * sizeof(u64),
			       (u64)(uintptr_t)&sb_entry, 2, 0);
	if (rc) {
		DP_ERR(p_hwfn, "dmae_grc2host failed %d\n", rc);
		return rc;
	}

	if (tx)
		SET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES1, timer_res);
	else
		SET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES0, timer_res);

	rc = qed_dmae_host2grc(p_hwfn, p_ptt,
			       (u64)(uintptr_t)&sb_entry,
			       CAU_REG_SB_VAR_MEMORY +
			       sb_id * sizeof(u64), 2, 0);
	if (rc) {
		DP_ERR(p_hwfn, "dmae_host2grc failed %d\n", rc);
		return rc;
	}

	return rc;
}
