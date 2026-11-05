// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "ras.h"
#include "aca.h"
#include "core_status.h"
#include "aca_v5_0.h"

#define ACA_HWIP_MAP(ip, hwid, acatypes) \
	{ACA_ECC_HWIP__##ip, hwid, acatypes, ARRAY_SIZE(acatypes)}

#define ACA_GFX_XCD_HWID  0x2E2
struct ras_aca_hwip_v5 {
	enum aca_ecc_hwip hwip;
	const u32 hwid;
	const u32 *types;
	u32 types_sz;
};

static const u32 smu_aca_types[] = {0x01};
static const u32 pcs_xgmi_aca_types[] = {0x00};
static const u32 umc_aca_types[] = {0x00};
static const u32 gfx_aid_aca_types[] = {0x00, 0x01};
static const u32 gfx_xcd_aca_types[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x12, 0x13, 0x14,
};
static const u32 gfx_sdma_aca_types[] = {0x11};
static const u32 sdma_aca_types[] =   {0x00};
static const u32 mmhub_aca_types[] =  {0x00};
static const u32 mpifoe_aca_types[] = {0x00};
static const u32 pcie_pl_aca_types[] = {0x00};
static const u32 dacc_be_aca_types[] = {0x00};
static const u32 ucie_pcs_aca_types[] = {0x00};
static const u32 lsdma_aca_types[] = {0x00};

static struct ras_aca_hwip_v5 aca_hwip_maps[] = {
	ACA_HWIP_MAP(SMU,      0x01,  smu_aca_types),
	ACA_HWIP_MAP(PCS_XGMI, 0x50,  pcs_xgmi_aca_types),
	ACA_HWIP_MAP(UMC,      0x96,  umc_aca_types),
	ACA_HWIP_MAP(GFX,      0x0B,  gfx_aid_aca_types),
	ACA_HWIP_MAP(GFX,      0x2E2, gfx_xcd_aca_types),
	ACA_HWIP_MAP(SDMA,     0x2E2, gfx_sdma_aca_types),
	ACA_HWIP_MAP(SDMA,     0x15A, sdma_aca_types),
	ACA_HWIP_MAP(MMHUB,    0x73,  mmhub_aca_types),
	ACA_HWIP_MAP(MPIFOE,   0x1FD, mpifoe_aca_types),
	ACA_HWIP_MAP(PCIE_PL,  0x1E1, pcie_pl_aca_types),
	ACA_HWIP_MAP(DACC_BE,  0x164, dacc_be_aca_types),
	ACA_HWIP_MAP(UCIE_PCS, 0x16C, ucie_pcs_aca_types),
	ACA_HWIP_MAP(LSDMA,    0x346, lsdma_aca_types),
};

static void aca_decode_bank_info(struct aca_block *aca_blk,
			struct aca_bank_reg *bank, struct aca_ecc_info *info)
{
	u64 ipid;
	u32 instidhi, instidlo;

	ipid = bank->regs[ACA_REG_IDX__IPID];
	info->hwid = ACA_V5_REG_IPID_HARDWAREID(ipid);
	info->mcatype = ACA_V5_REG_IPID_ACATYPE(ipid);

	instidhi = ACA_V5_REG_IPID_INSTANCEIDHI(ipid);
	instidlo = ACA_V5_REG_IPID_INSTANCEIDLO(ipid);
	info->die_id = instidhi & 0xF;
	info->socket_id = instidlo & 0xFF;
	/* xcd/aid are unique in die_id, not need per-XCD decode for accounting */
	info->xcd_valid = false;
}

static bool aca_check_bank_hwip(struct aca_bank_reg *bank, enum aca_ecc_hwip type)
{
	struct ras_aca_hwip_v5 *hwip = NULL;
	u32 hwid, acatype;
	u64 ipid;
	int i, t;

	if (!bank || (type == ACA_ECC_HWIP__UNKNOWN))
		return false;

	ipid = bank->regs[ACA_REG_IDX__IPID];
	hwid = ACA_V5_REG_IPID_HARDWAREID(ipid);
	acatype = ACA_V5_REG_IPID_ACATYPE(ipid);

	for (i = 0; i < ARRAY_SIZE(aca_hwip_maps); i++) {
		if (aca_hwip_maps[i].hwid != hwid)
			continue;

		for (t = 0; t < aca_hwip_maps[i].types_sz; t++) {
			if (aca_hwip_maps[i].types &&
			    (aca_hwip_maps[i].types[t] == acatype)) {
				hwip = &aca_hwip_maps[i];
				goto out;
			}
		}
	}

out:
	return hwip ? (hwip->hwip == type) : false;
}

static bool aca_match_bank_default(struct aca_block *aca_blk, void *data)
{
	return aca_check_bank_hwip((struct aca_bank_reg *)data, aca_blk->blk_info->hwip);
}

static bool aca_check_umc_de(struct ras_core_context *ras_core, u64 mc_umc_status)
{
	return (ras_core_poison_supported(ras_core) &&
		    ACA_V5_REG_STATUS_VAL(mc_umc_status) &&
		    ACA_V5_REG_STATUS_DEFERRED(mc_umc_status));
}

static bool aca_check_umc_ue(struct ras_core_context *ras_core, u64 mc_umc_status)
{
	if (aca_check_umc_de(ras_core, mc_umc_status))
		return false;

	return (ACA_V5_REG_STATUS_VAL(mc_umc_status) &&
		    (ACA_V5_REG_STATUS_PCC(mc_umc_status) ||
		     ACA_V5_REG_STATUS_UC(mc_umc_status) ||
		     ACA_V5_REG_STATUS_TCC(mc_umc_status)));
}

static bool aca_check_umc_ce(struct ras_core_context *ras_core, u64 mc_umc_status)
{
	if (aca_check_umc_de(ras_core, mc_umc_status))
		return false;

	return (ACA_V5_REG_STATUS_VAL(mc_umc_status) &&
		    (ACA_V5_REG_STATUS_CECC(mc_umc_status) ||
		     (ACA_V5_REG_STATUS_UECC(mc_umc_status) &&
		      ACA_V5_REG_STATUS_UC(mc_umc_status) == 0) ||
		/* Identify data parity error in replay mode */
		     ((ACA_V5_REG_STATUS_ERRORCODEEXT(mc_umc_status) == 0x5 ||
		      ACA_V5_REG_STATUS_ERRORCODEEXT(mc_umc_status) == 0xb) &&
		     !(aca_check_umc_ue(ras_core, mc_umc_status)))));
}

static int aca_parse_umc_bank(struct ras_core_context *ras_core,
			struct aca_block *aca_blk, void *data, void *buf)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	struct aca_bank_ecc *err = (struct aca_bank_ecc *)buf;
	u32 ext_error_code, misc0_errcnt;
	u64 status;

	if (!ras_core || !aca_blk || !data || !buf)
		return -EINVAL;

	status = bank->regs[ACA_REG_IDX__STATUS];
	if (!ACA_V5_REG_STATUS_VAL(status))
		return 0;

	memset(err, 0, sizeof(*err));
	aca_decode_bank_info(aca_blk, bank, &err->bank_info);
	err->bank_info.status = bank->regs[ACA_REG_IDX__STATUS];
	err->bank_info.ipid = bank->regs[ACA_REG_IDX__IPID];
	err->bank_info.addr = bank->regs[ACA_REG_IDX__ADDR];

	ext_error_code = ACA_V5_REG_STATUS_ERRORCODEEXT(status);
	misc0_errcnt = ACA_V5_REG_MISC0_ERRCNT(bank->regs[ACA_REG_IDX__MISC0]);
	if (bank->ecc_type == RAS_ERR_TYPE__MCE &&
	    ACA_V5_REG_STATUS_DEFERRED(status)) {
		err->de_count = 1;
		return 0;
	}

	if (aca_check_umc_de(ras_core, status))
		err->de_count = misc0_errcnt ? misc0_errcnt : 1;
	else if (aca_check_umc_ue(ras_core, status))
		err->ue_count = ext_error_code ? 1 : misc0_errcnt;
	else if (aca_check_umc_ce(ras_core, status))
		err->ce_count = ext_error_code ? 1 : misc0_errcnt;

	if (bank->ecc_type != RAS_ERR_TYPE__MCE || err->de_count ||
	    err->ue_count || err->ce_count)
		return 0;

	if (ACA_V5_REG_STATUS_DEFERRED(status))
		err->de_count = 1;
	else if (ACA_V5_REG_STATUS_UC(status) || ACA_V5_REG_STATUS_PCC(status) ||
		 ACA_V5_REG_STATUS_TCC(status))
		err->ue_count = 1;
	else if (ACA_V5_REG_STATUS_CECC(status) || ACA_V5_REG_STATUS_UECC(status))
		err->ce_count = 1;

	return 0;
}

static bool aca_check_bank_is_de(struct ras_core_context *ras_core,
				u64 status)
{
	return (ACA_V5_REG_STATUS_POISON(status) ||
				ACA_V5_REG_STATUS_DEFERRED(status));
}

static int aca_parse_bank_default(struct ras_core_context *ras_core,
				  struct aca_block *aca_blk,
				  void *data, void *buf)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	struct aca_bank_ecc *err = (struct aca_bank_ecc *)buf;

	u64 misc0, status;

	if (!ras_core || !aca_blk || !data || !buf)
		return -EINVAL;

	misc0 = bank->regs[ACA_REG_IDX__MISC0];
	status = bank->regs[ACA_REG_IDX__STATUS];

	memset(err, 0, sizeof(*err));
	aca_decode_bank_info(aca_blk, bank, &err->bank_info);
	err->bank_info.status = status;
	err->bank_info.ipid = bank->regs[ACA_REG_IDX__IPID];
	err->bank_info.addr = bank->regs[ACA_REG_IDX__ADDR];

	if (aca_check_bank_is_de(ras_core, status)) {
		err->de_count = 1;
	} else {
		if (bank->ecc_type == RAS_ERR_TYPE__UE)
			err->ue_count = 1;
		else if ((bank->ecc_type == RAS_ERR_TYPE__CE) ||
			(bank->ecc_type == RAS_ERR_TYPE__MCE))
			err->ce_count = ACA_V5_REG_MISC0_ERRCNT(misc0);
	}

	return 0;
}

static int aca_parse_xgmi_bank(struct ras_core_context *ras_core,
			       struct aca_block *aca_blk,
			       void *data, void *buf)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	struct aca_bank_ecc *err = (struct aca_bank_ecc *)buf;
	u64 status, count;
	int ext_error_code;

	if (!ras_core || !aca_blk || !data || !buf)
		return -EINVAL;

	memset(err, 0, sizeof(*err));
	aca_decode_bank_info(aca_blk, bank, &err->bank_info);
	err->bank_info.status = bank->regs[ACA_REG_IDX__STATUS];
	err->bank_info.ipid = bank->regs[ACA_REG_IDX__IPID];
	err->bank_info.addr = bank->regs[ACA_REG_IDX__ADDR];

	status = bank->regs[ACA_REG_IDX__STATUS];
	ext_error_code = ACA_V5_REG_STATUS_ERRORCODEEXT(status);

	count = ACA_V5_REG_MISC0_ERRCNT(bank->regs[ACA_REG_IDX__MISC0]);
	if (bank->ecc_type == RAS_ERR_TYPE__UE) {
		if (ext_error_code != 0 && ext_error_code != 1 && ext_error_code != 9)
			count = 0ULL;
		err->ue_count = count;
	} else if ((bank->ecc_type == RAS_ERR_TYPE__CE) ||
		(bank->ecc_type == RAS_ERR_TYPE__MCE)) {
		count = ext_error_code == 6 ? count : 0ULL;
		err->ce_count = count;
	}

	return 0;
}

static const struct aca_block_info aca_v5_0_umc = {
	.name = "umc",
	.ras_block_id = RAS_BLOCK_ID__UMC,
	.hwip = ACA_ECC_HWIP__UMC,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK | ACA_ERROR__DE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_umc_bank,
	},
};

static const struct aca_block_info aca_v5_0_gfx = {
	.name = "gfx",
	.ras_block_id = RAS_BLOCK_ID__GFX,
	.hwip = ACA_ECC_HWIP__GFX,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v5_0_sdma = {
	.name = "sdma",
	.ras_block_id = RAS_BLOCK_ID__SDMA,
	.hwip = ACA_ECC_HWIP__SDMA,
	.mask = ACA_ERROR__UE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v5_0_mmhub = {
	.name = "mmhub",
	.ras_block_id = RAS_BLOCK_ID__MMHUB,
	.hwip = ACA_ECC_HWIP__MMHUB,
	.mask = ACA_ERROR__UE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v5_0_xgmi = {
	.name = "xgmi",
	.ras_block_id = RAS_BLOCK_ID__XGMI_WAFL,
	.hwip = ACA_ECC_HWIP__PCS_XGMI,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_xgmi_bank,
	},
};

static const struct aca_block_info aca_v5_0_pcie_pl = {
	.name = "pcie_pl",
	.ras_block_id = RAS_BLOCK_ID__PCIE_PL,
	.hwip = ACA_ECC_HWIP__PCIE_PL,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v5_0_dacc_be = {
	.name = "dacc_be",
	.ras_block_id = RAS_BLOCK_ID__DACC_BE,
	.hwip = ACA_ECC_HWIP__DACC_BE,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v5_0_ucie_pcs = {
	.name = "ucie_pcs",
	.ras_block_id = RAS_BLOCK_ID__UCIE_PCS,
	.hwip = ACA_ECC_HWIP__UCIE_PCS,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v5_0_lsdma = {
	.name = "lsdma",
	.ras_block_id = RAS_BLOCK_ID__LSDMA,
	.hwip = ACA_ECC_HWIP__LSDMA,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v5_0_pcs_xgmi = {
	.name = "pcs_xgmi",
	.ras_block_id = RAS_BLOCK_ID__PCS_XGMI,
	.hwip = ACA_ECC_HWIP__PCS_XGMI,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info *aca_block_info_v5_0[] = {
	&aca_v5_0_umc,
	&aca_v5_0_gfx,
	&aca_v5_0_sdma,
	&aca_v5_0_mmhub,
	&aca_v5_0_xgmi,
	&aca_v5_0_pcie_pl,
	&aca_v5_0_dacc_be,
	&aca_v5_0_ucie_pcs,
	&aca_v5_0_lsdma,
	&aca_v5_0_pcs_xgmi,
};

static u64 aca_parse_ras_caps_v5_0(struct ras_core_context *ras_core)
{
	u64 parser_supported_mask = 0;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(aca_block_info_v5_0); i++)
		parser_supported_mask |=
			BIT_ULL(aca_block_info_v5_0[i]->ras_block_id);

	return parser_supported_mask;
}

static int aca_fill_rma_bank_v5_0(struct ras_core_context *ras_core, struct aca_bank_reg *bank)
{
	struct device_system_info dev_info = {0};
	int ret;

	if (!bank)
		return -EINVAL;

	ret = ras_core_get_device_system_info(ras_core, &dev_info);
	if (ret)
		return ret;

	memset(bank->regs, 0, sizeof(bank->regs));
	bank->regs[ACA_REG_IDX__CTL]    = 0x1ULL;
	bank->regs[ACA_REG_IDX__STATUS] = 0xB000000000000137ULL;
	bank->regs[ACA_REG_IDX__CONFG]  = 0x1ff00000002ULL;
	bank->regs[ACA_REG_IDX__IPID]   = 0x9600000000ULL | (dev_info.socket_id & 0xFF);

	return 0;
}

const struct ras_aca_ip_func ras_aca_func_v5_0 = {
	.block_num = ARRAY_SIZE(aca_block_info_v5_0),
	.block_info = aca_block_info_v5_0,
	.aca_parse_ras_caps = aca_parse_ras_caps_v5_0,
	.fill_rma_bank = aca_fill_rma_bank_v5_0,
};
