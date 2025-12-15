// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "ras_aca.h"
#include "ras_core_status.h"
#include "ras_aca_v1_0.h"

struct ras_aca_hwip {
	int hwid;
	int mcatype;
};

static struct ras_aca_hwip aca_hwid_mcatypes[ACA_ECC_HWIP_COUNT] = {
	[ACA_ECC_HWIP__SMU] = {0x01, 0x01},
	[ACA_ECC_HWIP__PCS_XGMI] = {0x50, 0x00},
	[ACA_ECC_HWIP__UMC] = {0x96, 0x00},
};

static int aca_decode_bank_info(struct aca_block *aca_blk,
			struct aca_bank_reg *bank, struct aca_ecc_info *info)
{
	u64 ipid;
	u32 instidhi, instidlo;

	ipid = bank->regs[ACA_REG_IDX__IPID];
	info->hwid = ACA_REG_IPID_HARDWAREID(ipid);
	info->mcatype = ACA_REG_IPID_MCATYPE(ipid);
	/*
	 * Unified DieID Format: SAASS. A:AID, S:Socket.
	 * Unified DieID[4:4] = InstanceId[0:0]
	 * Unified DieID[0:3] = InstanceIdHi[0:3]
	 */
	instidhi = ACA_REG_IPID_INSTANCEIDHI(ipid);
	instidlo = ACA_REG_IPID_INSTANCEIDLO(ipid);
	info->die_id = ((instidhi >> 2) & 0x03);
	info->socket_id = ((instidlo & 0x1) << 2) | (instidhi & 0x03);

	if ((aca_blk->blk_info->hwip == ACA_ECC_HWIP__SMU) &&
	    (aca_blk->blk_info->ras_block_id == RAS_BLOCK_ID__GFX))
		info->xcd_id =
			((instidlo & GENMASK_ULL(31, 1)) == mmSMNAID_XCD0_MCA_SMU) ? 0 : 1;

	return 0;
}

static bool aca_check_bank_hwip(struct aca_bank_reg *bank, enum aca_ecc_hwip type)
{
	struct ras_aca_hwip *hwip;
	int hwid, mcatype;
	u64 ipid;

	if (!bank || (type == ACA_ECC_HWIP__UNKNOWN))
		return false;

	hwip = &aca_hwid_mcatypes[type];
	if (!hwip->hwid)
		return false;

	ipid = bank->regs[ACA_REG_IDX__IPID];
	hwid = ACA_REG_IPID_HARDWAREID(ipid);
	mcatype = ACA_REG_IPID_MCATYPE(ipid);

	return hwip->hwid == hwid && hwip->mcatype == mcatype;
}

static bool aca_match_bank_default(struct aca_block *aca_blk, void *data)
{
	return aca_check_bank_hwip((struct aca_bank_reg *)data, aca_blk->blk_info->hwip);
}

static bool aca_match_gfx_bank(struct aca_block *aca_blk, void *data)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	u32 instlo;

	if (!aca_check_bank_hwip(bank, aca_blk->blk_info->hwip))
		return false;

	instlo = ACA_REG_IPID_INSTANCEIDLO(bank->regs[ACA_REG_IDX__IPID]);
	instlo &= GENMASK_ULL(31, 1);
	switch (instlo) {
	case mmSMNAID_XCD0_MCA_SMU:
	case mmSMNAID_XCD1_MCA_SMU:
	case mmSMNXCD_XCD0_MCA_SMU:
		return true;
	default:
		break;
	}

	return false;
}

static bool aca_match_sdma_bank(struct aca_block *aca_blk, void *data)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	/* CODE_SDMA0 - CODE_SDMA4, reference to smu driver if header file */
	static int sdma_err_codes[] = { 33, 34, 35, 36 };
	u32 instlo;
	int errcode, i;

	if (!aca_check_bank_hwip(bank, aca_blk->blk_info->hwip))
		return false;

	instlo = ACA_REG_IPID_INSTANCEIDLO(bank->regs[ACA_REG_IDX__IPID]);
	instlo &= GENMASK_ULL(31, 1);
	if (instlo != mmSMNAID_AID0_MCA_SMU)
		return false;

	errcode = ACA_REG_SYND_ERRORINFORMATION(bank->regs[ACA_REG_IDX__SYND]);
	errcode &= 0xff;

	/* Check SDMA error codes */
	for (i = 0; i < ARRAY_SIZE(sdma_err_codes); i++) {
		if (errcode == sdma_err_codes[i])
			return true;
	}

	return false;
}

static bool aca_match_mmhub_bank(struct aca_block *aca_blk, void *data)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	/* reference to smu driver if header file */
	const int mmhub_err_codes[] = {
		0, 1, 2, 3, 4, /* CODE_DAGB0 - 4 */
		5, 6, 7, 8, 9, /* CODE_EA0 - 4 */
		10, /* CODE_UTCL2_ROUTER */
		11, /* CODE_VML2 */
		12, /* CODE_VML2_WALKER */
		13, /* CODE_MMCANE */
	};
	u32 instlo;
	int errcode, i;

	if (!aca_check_bank_hwip(bank, aca_blk->blk_info->hwip))
		return false;

	instlo = ACA_REG_IPID_INSTANCEIDLO(bank->regs[ACA_REG_IDX__IPID]);
	instlo &= GENMASK_ULL(31, 1);
	if (instlo != mmSMNAID_AID0_MCA_SMU)
		return false;

	errcode = ACA_REG_SYND_ERRORINFORMATION(bank->regs[ACA_REG_IDX__SYND]);
	errcode &= 0xff;

	/* Check MMHUB error codes */
	for (i = 0; i < ARRAY_SIZE(mmhub_err_codes); i++) {
		if (errcode == mmhub_err_codes[i])
			return true;
	}

	return false;
}

static bool aca_check_umc_de(struct ras_core_context *ras_core, uint64_t mc_umc_status)
{
	return (ras_core->poison_supported &&
		    ACA_REG_STATUS_VAL(mc_umc_status) &&
		    ACA_REG_STATUS_DEFERRED(mc_umc_status));
}

static bool aca_check_umc_ue(struct ras_core_context *ras_core, uint64_t mc_umc_status)
{
	if (aca_check_umc_de(ras_core, mc_umc_status))
		return false;

	return (ACA_REG_STATUS_VAL(mc_umc_status) &&
		    (ACA_REG_STATUS_PCC(mc_umc_status) ||
		     ACA_REG_STATUS_UC(mc_umc_status) ||
		     ACA_REG_STATUS_TCC(mc_umc_status)));
}

static bool aca_check_umc_ce(struct ras_core_context *ras_core, uint64_t mc_umc_status)
{
	if (aca_check_umc_de(ras_core, mc_umc_status))
		return false;

	return (ACA_REG_STATUS_VAL(mc_umc_status) &&
		    (ACA_REG_STATUS_CECC(mc_umc_status) ||
		     (ACA_REG_STATUS_UECC(mc_umc_status) &&
		      ACA_REG_STATUS_UC(mc_umc_status) == 0) ||
		/* Identify data parity error in replay mode */
		     ((ACA_REG_STATUS_ERRORCODEEXT(mc_umc_status) == 0x5 ||
		      ACA_REG_STATUS_ERRORCODEEXT(mc_umc_status) == 0xb) &&
		     !(aca_check_umc_ue(ras_core, mc_umc_status)))));
}

static int aca_parse_umc_bank(struct ras_core_context *ras_core,
			struct aca_block *ras_blk, void *data, void *buf)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	struct aca_bank_ecc *ecc = (struct aca_bank_ecc *)buf;
	struct aca_ecc_info bank_info;
	uint32_t ext_error_code;
	uint64_t status0;

	status0 = bank->regs[ACA_REG_IDX__STATUS];
	if (!ACA_REG_STATUS_VAL(status0))
		return 0;

	memset(&bank_info, 0, sizeof(bank_info));
	aca_decode_bank_info(ras_blk, bank, &bank_info);
	memcpy(&ecc->bank_info, &bank_info, sizeof(bank_info));
	ecc->bank_info.status = bank->regs[ACA_REG_IDX__STATUS];
	ecc->bank_info.ipid = bank->regs[ACA_REG_IDX__IPID];
	ecc->bank_info.addr = bank->regs[ACA_REG_IDX__ADDR];

	ext_error_code = ACA_REG_STATUS_ERRORCODEEXT(status0);

	if (aca_check_umc_de(ras_core, status0))
		ecc->de_count = 1;
	else if (aca_check_umc_ue(ras_core, status0))
		ecc->ue_count = ext_error_code ?
			1 : ACA_REG_MISC0_ERRCNT(bank->regs[ACA_REG_IDX__MISC0]);
	else if (aca_check_umc_ce(ras_core, status0))
		ecc->ce_count = ext_error_code ?
			1 : ACA_REG_MISC0_ERRCNT(bank->regs[ACA_REG_IDX__MISC0]);

	return 0;
}

static bool aca_check_bank_is_de(struct ras_core_context *ras_core,
				uint64_t status)
{
	return (ACA_REG_STATUS_POISON(status) ||
				ACA_REG_STATUS_DEFERRED(status));
}

static int aca_parse_bank_default(struct ras_core_context *ras_core,
				  struct aca_block *ras_blk,
				  void *data, void *buf)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	struct aca_bank_ecc *ecc = (struct aca_bank_ecc *)buf;
	struct aca_ecc_info bank_info;
	u64 misc0 = bank->regs[ACA_REG_IDX__MISC0];
	u64 status = bank->regs[ACA_REG_IDX__STATUS];

	memset(&bank_info, 0, sizeof(bank_info));
	aca_decode_bank_info(ras_blk, bank, &bank_info);
	memcpy(&ecc->bank_info, &bank_info, sizeof(bank_info));
	ecc->bank_info.status = status;
	ecc->bank_info.ipid = bank->regs[ACA_REG_IDX__IPID];
	ecc->bank_info.addr = bank->regs[ACA_REG_IDX__ADDR];

	if (aca_check_bank_is_de(ras_core, status)) {
		ecc->de_count = 1;
	} else {
		if (bank->ecc_type == RAS_ERR_TYPE__UE)
			ecc->ue_count = 1;
		else if (bank->ecc_type == RAS_ERR_TYPE__CE)
			ecc->ce_count = ACA_REG_MISC0_ERRCNT(misc0);
	}

	return 0;
}

static int aca_parse_xgmi_bank(struct ras_core_context *ras_core,
			       struct aca_block *ras_blk,
			       void *data, void *buf)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;
	struct aca_bank_ecc *ecc = (struct aca_bank_ecc *)buf;
	struct aca_ecc_info bank_info;
	u64 status, count;
	int ext_error_code;

	memset(&bank_info, 0, sizeof(bank_info));
	aca_decode_bank_info(ras_blk, bank, &bank_info);
	memcpy(&ecc->bank_info, &bank_info, sizeof(bank_info));
	ecc->bank_info.status = bank->regs[ACA_REG_IDX__STATUS];
	ecc->bank_info.ipid = bank->regs[ACA_REG_IDX__IPID];
	ecc->bank_info.addr = bank->regs[ACA_REG_IDX__ADDR];

	status = bank->regs[ACA_REG_IDX__STATUS];
	ext_error_code = ACA_REG_STATUS_ERRORCODEEXT(status);

	count = ACA_REG_MISC0_ERRCNT(bank->regs[ACA_REG_IDX__MISC0]);
	if (bank->ecc_type == RAS_ERR_TYPE__UE) {
		if (ext_error_code != 0 && ext_error_code != 9)
			count = 0ULL;
		ecc->ue_count = count;
	} else if (bank->ecc_type == RAS_ERR_TYPE__CE) {
		count = ext_error_code == 6 ? count : 0ULL;
		ecc->ce_count = count;
	}

	return 0;
}

static const struct aca_block_info aca_v1_0_umc = {
	.name = "umc",
	.ras_block_id = RAS_BLOCK_ID__UMC,
	.hwip = ACA_ECC_HWIP__UMC,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK | ACA_ERROR__DE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_umc_bank,
	},
};

static const struct aca_block_info aca_v1_0_gfx = {
	.name = "gfx",
	.ras_block_id = RAS_BLOCK_ID__GFX,
	.hwip = ACA_ECC_HWIP__SMU,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_gfx_bank,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v1_0_sdma = {
	.name = "sdma",
	.ras_block_id = RAS_BLOCK_ID__SDMA,
	.hwip = ACA_ECC_HWIP__SMU,
	.mask = ACA_ERROR__UE_MASK,
	.bank_ops = {
		.bank_match = aca_match_sdma_bank,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v1_0_mmhub = {
	.name = "mmhub",
	.ras_block_id = RAS_BLOCK_ID__MMHUB,
	.hwip = ACA_ECC_HWIP__SMU,
	.mask = ACA_ERROR__UE_MASK,
	.bank_ops = {
		.bank_match = aca_match_mmhub_bank,
		.bank_parse = aca_parse_bank_default,
	},
};

static const struct aca_block_info aca_v1_0_xgmi = {
	.name = "xgmi",
	.ras_block_id = RAS_BLOCK_ID__XGMI_WAFL,
	.hwip = ACA_ECC_HWIP__PCS_XGMI,
	.mask = ACA_ERROR__UE_MASK | ACA_ERROR__CE_MASK,
	.bank_ops = {
		.bank_match = aca_match_bank_default,
		.bank_parse = aca_parse_xgmi_bank,
	},
};

static const struct aca_block_info *aca_block_info_v1_0[] = {
	&aca_v1_0_umc,
	&aca_v1_0_gfx,
	&aca_v1_0_sdma,
	&aca_v1_0_mmhub,
	&aca_v1_0_xgmi,
};

const struct ras_aca_ip_func ras_aca_func_v1_0 = {
	.block_num = ARRAY_SIZE(aca_block_info_v1_0),
	.block_info = aca_block_info_v1_0,
};
