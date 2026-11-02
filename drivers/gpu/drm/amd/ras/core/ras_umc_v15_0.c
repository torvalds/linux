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
#include "ras_umc.h"
#include "core_status.h"
#include "ras_umc_v15_0.h"

static void __get_nps_pa_flip_bits(struct ras_core_context *ras_core,
			enum umc_memory_partition_mode nps,
			struct umc_flip_bits *flip_bits)
{

}

static uint64_t  __get_nps_pa_flip_mask(struct ras_core_context *ras_core,
		enum umc_memory_partition_mode nps)
{
	struct umc_flip_bits flip_bits = {0};
	uint64_t flip_mask = 0;
	int i;

	__get_nps_pa_flip_bits(ras_core, nps, &flip_bits);

	for (i = 0; i < flip_bits.bit_num; i++)
		flip_mask |= BIT_ULL(flip_bits.flip_bits_in_pa[i]);

	return flip_mask;
}

static int lookup_bad_pages_in_a_row(struct ras_core_context *ras_core,
		struct eeprom_umc_record *record, uint32_t nps,
		uint64_t *pfns, uint32_t num,
		uint64_t seq_no, bool dump)
{
	return 0;
}

static int umc_v15_convert_ma_to_pa(struct ras_core_context *ras_core,
			struct umc_mca_addr *addr_in, struct umc_phy_addr *addr_out,
			uint32_t nps)
{

	return 0;
}

static int convert_ma_to_pa(struct ras_core_context *ras_core,
			struct umc_mca_addr *addr_in, struct umc_phy_addr *addr_out,
			uint32_t nps)
{
	int ret;

	ret = ras_umc_psp_translate_addr(ras_core,
				addr_in, addr_out, nps);
	if (ret != -EOPNOTSUPP)
		return ret;

	if (ras_psp_check_supported_cmd(ras_core, RAS_TA_CMD_ID__QUERY_ADDRESS))
		ret = ras_umc_ras_ta_translate_addr(ras_core,
				addr_in, addr_out, nps);
	else
		ret = umc_v15_convert_ma_to_pa(ras_core,
				addr_in, addr_out, nps);

	return ret;
}

static int convert_bank_to_nps_addr(struct ras_core_context *ras_core,
			struct ras_bank_ecc *bank, struct umc_phy_addr *pa_addr, uint32_t nps)
{
	struct umc_mca_addr addr_in;
	struct umc_phy_addr addr_out;
	int ret;

	memset(&addr_in, 0, sizeof(addr_in));
	memset(&addr_out, 0, sizeof(addr_out));

	addr_in.err_addr = ACA_ADDR_2_ERR_ADDR(bank->addr);
	addr_in.ch_inst = ACA_IPID_2_UMC_CH(bank->ipid);
	addr_in.umc_inst = ACA_IPID_2_UMC_INST(bank->ipid);
	addr_in.node_inst = ACA_IPID_2_DIE_ID(bank->ipid);
	addr_in.socket_id = ACA_IPID_2_SOCKET_ID(bank->ipid);
	addr_in.mca_addr = bank->addr;
	addr_in.ipid = bank->ipid;

	ret = convert_ma_to_pa(ras_core, &addr_in, &addr_out, nps);
	if (!ret) {
		pa_addr->pa_flip_mask = addr_out.pa_flip_mask;
		pa_addr->pa = addr_out.pa | pa_addr->pa_flip_mask;
		pa_addr->channel_idx = addr_out.channel_idx;
		pa_addr->bank = addr_out.bank;
	}

	return ret;
}

static int umc_v15_0_bank_to_eeprom_record(struct ras_core_context *ras_core,
		struct ras_bank_ecc *bank, struct eeprom_umc_record *record)
{
	struct umc_phy_addr nps_addr;
	int ret;

	memset(&nps_addr, 0, sizeof(nps_addr));

	ret = convert_bank_to_nps_addr(ras_core, bank,
			&nps_addr, bank->nps);
	if (ret)
		return ret;

	ras_umc_fill_eeprom_record(ras_core,
		ACA_ADDR_2_ERR_ADDR(bank->addr), ACA_IPID_2_UMC_INST(bank->ipid),
		&nps_addr, bank->nps, record);

	/* If the bank being converted already has a timestamp,
	 * then use the bank's timestamp.
	 */
	if (bank->timestamp)
		record->ts = bank->timestamp;

	lookup_bad_pages_in_a_row(ras_core, record,
		bank->nps, NULL, 0, bank->seq_no, true);

	return 0;
}

static int convert_eeprom_record_to_nps_addr(struct ras_core_context *ras_core,
	struct eeprom_umc_record *record, uint64_t *pa, uint64_t *pa_flip_mask, uint32_t nps)
{
	struct device_system_info dev_info = {0};
	struct umc_mca_addr addr_in;
	struct umc_phy_addr addr_out;
	int ret;

	memset(&addr_in, 0, sizeof(addr_in));
	memset(&addr_out, 0, sizeof(addr_out));

	ras_core_get_device_system_info(ras_core, &dev_info);

	addr_in.err_addr = record->address;
	addr_in.ch_inst = record->mem_channel;
	addr_in.umc_inst = record->mcumc_id;
	addr_in.node_inst = UMC_INV_AID_NODE;
	addr_in.socket_id = dev_info.socket_id;

	ret = convert_ma_to_pa(ras_core, &addr_in, &addr_out, nps);
	if (ret)
		return ret;

	*pa_flip_mask = addr_out.pa_flip_mask;
	*pa = addr_out.pa | addr_out.pa_flip_mask;

	return 0;
}

static int umc_v15_0_eeprom_record_to_nps_record(struct ras_core_context *ras_core,
				struct eeprom_umc_record *record, uint32_t nps)
{
	uint64_t pa = 0, flip_mask = 0;
	uint64_t row_pfn;
	int ret = 0;

	if (nps == EEPROM_RECORD_UMC_NPS_MODE(record)) {
		record->cur_nps_pa_flip_mask = __get_nps_pa_flip_mask(ras_core, nps);
		row_pfn = EEPROM_RECORD_UMC_ADDR_PFN(record);
		record->cur_nps_retired_row_pfn =
			RAS_ADDR_TO_PFN(RAS_PFN_TO_ADDR(row_pfn) | record->cur_nps_pa_flip_mask);
	} else {
		ret = convert_eeprom_record_to_nps_addr(ras_core,
				record, &pa, &flip_mask, nps);
		if (!ret) {
			record->cur_nps_retired_row_pfn = RAS_ADDR_TO_PFN(pa);
			record->cur_nps_pa_flip_mask = flip_mask;
		}
	}

	record->cur_nps = nps;

	return ret;
}

static int umc_v15_0_soc_pa_to_bank(struct ras_core_context *ras_core,
			uint64_t soc_pa,
			struct umc_bank_addr *bank_addr)
{
	return 0;
}

static int umc_v15_0_bank_to_soc_pa(struct ras_core_context *ras_core,
			struct umc_bank_addr bank_addr,
			uint64_t *soc_pa)
{
	return 0;
}

const struct ras_umc_ip_func ras_umc_func_v15_0 = {
	.bank_to_eeprom_record = umc_v15_0_bank_to_eeprom_record,
	.eeprom_record_to_nps_record = umc_v15_0_eeprom_record_to_nps_record,
	.bank_to_soc_pa = umc_v15_0_bank_to_soc_pa,
	.soc_pa_to_bank = umc_v15_0_soc_pa_to_bank,
};
