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
#include "ras_umc.h"
#include "ras_core_status.h"
#include "ras_umc_v12_0.h"

#define NumDieInterleaved 4

static const uint32_t umc_v12_0_channel_idx_tbl[]
	[UMC_V12_0_UMC_INSTANCE_NUM][UMC_V12_0_CHANNEL_INSTANCE_NUM] = {
	{{3,   7,   11,  15,  2,   6,   10,  14},  {1,   5,   9,   13,  0,   4,   8,   12},
	 {19,  23,  27,  31,  18,  22,  26,  30},  {17,  21,  25,  29,  16,  20,  24,  28}},
	{{47,  43,  39,  35,  46,  42,  38,  34},  {45,  41,  37,  33,  44,  40,  36,  32},
	 {63,  59,  55,  51,  62,  58,  54,  50},  {61,  57,  53,  49,  60,  56,  52,  48}},
	{{79,  75,  71,  67,  78,  74,  70,  66},  {77,  73,  69,  65,  76,  72,  68,  64},
	 {95,  91,  87,  83,  94,  90,  86,  82},  {93,  89,  85,  81,  92,  88,  84,  80}},
	{{99,  103, 107, 111, 98,  102, 106, 110}, {97,  101, 105, 109, 96,  100, 104, 108},
	 {115, 119, 123, 127, 114, 118, 122, 126}, {113, 117, 121, 125, 112, 116, 120, 124}}
};

/* mapping of MCA error address to normalized address */
static const uint32_t umc_v12_0_ma2na_mapping[] = {
	0,  5,  6,  8,  9,  14, 12, 13,
	10, 11, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28,
	24, 7,  29, 30,
};

static bool umc_v12_0_bit_wise_xor(uint32_t val)
{
	bool result = 0;
	int i;

	for (i = 0; i < 32; i++)
		result = result ^ ((val >> i) & 0x1);

	return result;
}

static void __get_nps_pa_flip_bits(struct ras_core_context *ras_core,
			enum umc_memory_partition_mode nps,
			struct umc_flip_bits *flip_bits)
{
	uint32_t vram_type = ras_core->ras_umc.umc_vram_type;

	/* default setting */
	flip_bits->flip_bits_in_pa[0] = UMC_V12_0_PA_C2_BIT;
	flip_bits->flip_bits_in_pa[1] = UMC_V12_0_PA_C3_BIT;
	flip_bits->flip_bits_in_pa[2] = UMC_V12_0_PA_C4_BIT;
	flip_bits->flip_bits_in_pa[3] = UMC_V12_0_PA_R13_BIT;
	flip_bits->flip_row_bit = 13;
	flip_bits->bit_num = 4;
	flip_bits->r13_in_pa = UMC_V12_0_PA_R13_BIT;

	if (nps == UMC_MEMORY_PARTITION_MODE_NPS2) {
		flip_bits->flip_bits_in_pa[0] = UMC_V12_0_PA_CH5_BIT;
		flip_bits->flip_bits_in_pa[1] = UMC_V12_0_PA_C2_BIT;
		flip_bits->flip_bits_in_pa[2] = UMC_V12_0_PA_B1_BIT;
		flip_bits->r13_in_pa = UMC_V12_0_PA_R12_BIT;
	} else if (nps == UMC_MEMORY_PARTITION_MODE_NPS4) {
		flip_bits->flip_bits_in_pa[0] = UMC_V12_0_PA_CH4_BIT;
		flip_bits->flip_bits_in_pa[1] = UMC_V12_0_PA_CH5_BIT;
		flip_bits->flip_bits_in_pa[2] = UMC_V12_0_PA_B0_BIT;
		flip_bits->r13_in_pa = UMC_V12_0_PA_R11_BIT;
	}

	switch (vram_type) {
	case UMC_VRAM_TYPE_HBM:
		/* other nps modes are taken as nps1 */
		if (nps == UMC_MEMORY_PARTITION_MODE_NPS2)
			flip_bits->flip_bits_in_pa[3] = UMC_V12_0_PA_R12_BIT;
		else if (nps == UMC_MEMORY_PARTITION_MODE_NPS4)
			flip_bits->flip_bits_in_pa[3] = UMC_V12_0_PA_R11_BIT;

		break;
	case UMC_VRAM_TYPE_HBM3E:
		flip_bits->flip_bits_in_pa[3] = UMC_V12_0_PA_R12_BIT;
		flip_bits->flip_row_bit = 12;

		if (nps == UMC_MEMORY_PARTITION_MODE_NPS2)
			flip_bits->flip_bits_in_pa[3] = UMC_V12_0_PA_R11_BIT;
		else if (nps == UMC_MEMORY_PARTITION_MODE_NPS4)
			flip_bits->flip_bits_in_pa[3] = UMC_V12_0_PA_R10_BIT;

		break;
	default:
		RAS_DEV_WARN(ras_core->dev,
			"Unknown HBM type, set RAS retire flip bits to the value in NPS1 mode.\n");
		break;
	}
}

static uint64_t  convert_nps_pa_to_row_pa(struct ras_core_context *ras_core,
		uint64_t pa, enum umc_memory_partition_mode nps, bool zero_pfn_ok)
{
	struct umc_flip_bits flip_bits = {0};
	uint64_t row_pa;
	int i;

	__get_nps_pa_flip_bits(ras_core, nps, &flip_bits);

	row_pa = pa;
	/* clear loop bits in soc physical address */
	for (i = 0; i < flip_bits.bit_num; i++)
		row_pa &= ~BIT_ULL(flip_bits.flip_bits_in_pa[i]);

	if (!zero_pfn_ok && !RAS_ADDR_TO_PFN(row_pa))
		row_pa |= BIT_ULL(flip_bits.flip_bits_in_pa[2]);

	return row_pa;
}

static int lookup_bad_pages_in_a_row(struct ras_core_context *ras_core,
		struct eeprom_umc_record *record, uint32_t nps,
		uint64_t *pfns, uint32_t num,
		uint64_t seq_no, bool dump)
{
	uint32_t col, col_lower, row, row_lower, idx, row_high;
	uint64_t soc_pa, row_pa, column, err_addr;
	uint64_t retired_addr = RAS_PFN_TO_ADDR(record->cur_nps_retired_row_pfn);
	struct umc_flip_bits flip_bits = {0};
	uint32_t retire_unit;
	uint32_t i;

	__get_nps_pa_flip_bits(ras_core, nps, &flip_bits);

	row_pa = convert_nps_pa_to_row_pa(ras_core, retired_addr, nps, true);

	err_addr = record->address;
	/* get column bit 0 and 1 in mca address */
	col_lower = (err_addr >> 1) & 0x3ULL;
	/* MA_R13_BIT will be handled later */
	row_lower = (err_addr >> UMC_V12_0_MCA_R0_BIT) & 0x1fffULL;
	row_lower &= ~BIT_ULL(flip_bits.flip_row_bit);

	if (ras_core->ras_gfx.gfx_ip_version >= IP_VERSION(9, 5, 0)) {
		row_high = (row_pa >> flip_bits.r13_in_pa) & 0x3ULL;
		/* it's 2.25GB in each channel, from MCA address to PA
		 * [R14 R13] is converted if the two bits value are 0x3,
		 * get them from PA instead of MCA address.
		 */
		row_lower |= (row_high << 13);
	}

	idx = 0;
	row = 0;
	retire_unit = 0x1 << flip_bits.bit_num;
	/* loop for all possibilities of retire bits */
	for (column = 0; column < retire_unit; column++) {
		soc_pa = row_pa;
		for (i = 0; i < flip_bits.bit_num; i++)
			soc_pa |= (((column >> i) & 0x1ULL) << flip_bits.flip_bits_in_pa[i]);

		col = ((column & 0x7) << 2) | col_lower;

		/* add row bit 13 */
		if (flip_bits.bit_num == UMC_PA_FLIP_BITS_NUM)
			row = ((column >> 3) << flip_bits.flip_row_bit) | row_lower;

		if (dump)
			RAS_DEV_INFO(ras_core->dev,
				"{%llu} Error Address(PA):0x%-10llx Row:0x%-4x Col:0x%-2x Bank:0x%x Channel:0x%x\n",
				seq_no, soc_pa, row, col,
				record->cur_nps_bank, record->mem_channel);


		if (pfns && (idx < num))
			pfns[idx++] = RAS_ADDR_TO_PFN(soc_pa);
	}

	return idx;
}

static int umc_v12_convert_ma_to_pa(struct ras_core_context *ras_core,
			struct umc_mca_addr *addr_in, struct umc_phy_addr *addr_out,
			uint32_t nps)
{
	uint32_t i, na_shift;
	uint64_t soc_pa, na, na_nps;
	uint32_t bank_hash0, bank_hash1, bank_hash2, bank_hash3, col, row;
	uint32_t bank0, bank1, bank2, bank3, bank;
	uint32_t ch_inst = addr_in->ch_inst;
	uint32_t umc_inst = addr_in->umc_inst;
	uint32_t node_inst = addr_in->node_inst;
	uint32_t socket_id = addr_in->socket_id;
	uint32_t channel_index;
	uint64_t err_addr = addr_in->err_addr;

	if (node_inst != UMC_INV_AID_NODE) {
		if (ch_inst >= UMC_V12_0_CHANNEL_INSTANCE_NUM ||
			umc_inst >= UMC_V12_0_UMC_INSTANCE_NUM ||
			node_inst >= UMC_V12_0_AID_NUM_MAX ||
			socket_id >= UMC_V12_0_SOCKET_NUM_MAX)
			return -EINVAL;
	} else {
		if (socket_id >= UMC_V12_0_SOCKET_NUM_MAX ||
			ch_inst >= UMC_V12_0_TOTAL_CHANNEL_NUM)
			return -EINVAL;
	}

	bank_hash0 = (err_addr >> UMC_V12_0_MCA_B0_BIT) & 0x1ULL;
	bank_hash1 = (err_addr >> UMC_V12_0_MCA_B1_BIT) & 0x1ULL;
	bank_hash2 = (err_addr >> UMC_V12_0_MCA_B2_BIT) & 0x1ULL;
	bank_hash3 = (err_addr >> UMC_V12_0_MCA_B3_BIT) & 0x1ULL;
	col = (err_addr >> 1) & 0x1fULL;
	row = (err_addr >> 10) & 0x3fffULL;

	/* apply bank hash algorithm */
	bank0 =
		bank_hash0 ^ (UMC_V12_0_XOR_EN0 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR0) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR0))));
	bank1 =
		bank_hash1 ^ (UMC_V12_0_XOR_EN1 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR1) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR1))));
	bank2 =
		bank_hash2 ^ (UMC_V12_0_XOR_EN2 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR2) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR2))));
	bank3 =
		bank_hash3 ^ (UMC_V12_0_XOR_EN3 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR3) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR3))));

	bank = bank0 | (bank1 << 1) | (bank2 << 2) | (bank3 << 3);
	err_addr &= ~0x3c0ULL;
	err_addr |= (bank << UMC_V12_0_MCA_B0_BIT);

	na_nps = 0x0;
	/* convert mca error address to normalized address */
	for (i = 1; i < ARRAY_SIZE(umc_v12_0_ma2na_mapping); i++)
		na_nps |= ((err_addr >> i) & 0x1ULL) << umc_v12_0_ma2na_mapping[i];

	if (nps == UMC_MEMORY_PARTITION_MODE_NPS1)
		na_shift = 8;
	else if (nps == UMC_MEMORY_PARTITION_MODE_NPS2)
		na_shift = 9;
	else if (nps == UMC_MEMORY_PARTITION_MODE_NPS4)
		na_shift = 10;
	else if (nps == UMC_MEMORY_PARTITION_MODE_NPS8)
		na_shift = 11;
	else
		return -EINVAL;

	na = ((na_nps >> na_shift) << 8) | (na_nps & 0xff);

	if (node_inst != UMC_INV_AID_NODE)
		channel_index =
			umc_v12_0_channel_idx_tbl[node_inst][umc_inst][ch_inst];
	else {
		channel_index = ch_inst;
		node_inst = channel_index /
			(UMC_V12_0_UMC_INSTANCE_NUM * UMC_V12_0_CHANNEL_INSTANCE_NUM);
	}

	/* translate umc channel address to soc pa, 3 parts are included */
	soc_pa = ADDR_OF_32KB_BLOCK(na) |
		ADDR_OF_256B_BLOCK(channel_index) |
		OFFSET_IN_256B_BLOCK(na);

	/* calc channel hash based on absolute address */
	soc_pa += socket_id * SOCKET_LFB_SIZE;
	/* the umc channel bits are not original values, they are hashed */
	UMC_V12_0_SET_CHANNEL_HASH(channel_index, soc_pa);
	/* restore pa */
	soc_pa -= socket_id * SOCKET_LFB_SIZE;

	/* get some channel bits from na_nps directly and
	 * add nps section offset
	 */
	if (nps == UMC_MEMORY_PARTITION_MODE_NPS2) {
		soc_pa &= ~(0x1ULL << UMC_V12_0_PA_CH5_BIT);
		soc_pa |= ((na_nps & 0x100) << 5);
		soc_pa += (node_inst >> 1) * (SOCKET_LFB_SIZE >> 1);
	} else if (nps == UMC_MEMORY_PARTITION_MODE_NPS4) {
		soc_pa &= ~(0x3ULL << UMC_V12_0_PA_CH4_BIT);
		soc_pa |= ((na_nps & 0x300) << 4);
		soc_pa += node_inst * (SOCKET_LFB_SIZE >> 2);
	} else if (nps == UMC_MEMORY_PARTITION_MODE_NPS8) {
		soc_pa &= ~(0x7ULL << UMC_V12_0_PA_CH4_BIT);
		soc_pa |= ((na_nps & 0x700) << 4);
		soc_pa += node_inst * (SOCKET_LFB_SIZE >> 2) +
			(channel_index >> 4) * (SOCKET_LFB_SIZE >> 3);
	}

	addr_out->pa = soc_pa;
	addr_out->bank = bank;
	addr_out->channel_idx = channel_index;

	return 0;
}

static int convert_ma_to_pa(struct ras_core_context *ras_core,
			struct umc_mca_addr *addr_in, struct umc_phy_addr *addr_out,
			uint32_t nps)
{
	int ret;

	if (ras_psp_check_supported_cmd(ras_core, RAS_TA_CMD_ID__QUERY_ADDRESS))
		ret = ras_umc_psp_convert_ma_to_pa(ras_core,
				addr_in, addr_out, nps);
	else
		ret = umc_v12_convert_ma_to_pa(ras_core,
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

	ret = convert_ma_to_pa(ras_core, &addr_in, &addr_out, nps);
	if (!ret) {
		pa_addr->pa =
			convert_nps_pa_to_row_pa(ras_core, addr_out.pa, nps, false);
		pa_addr->channel_idx = addr_out.channel_idx;
		pa_addr->bank = addr_out.bank;
	}

	return ret;
}

static int umc_v12_0_bank_to_eeprom_record(struct ras_core_context *ras_core,
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

	lookup_bad_pages_in_a_row(ras_core, record,
		bank->nps, NULL, 0, bank->seq_no, true);

	return 0;
}

static int convert_eeprom_record_to_nps_addr(struct ras_core_context *ras_core,
			struct eeprom_umc_record *record, uint64_t *pa, uint32_t nps)
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

	*pa = convert_nps_pa_to_row_pa(ras_core, addr_out.pa, nps, false);

	return 0;
}

static int umc_v12_0_eeprom_record_to_nps_record(struct ras_core_context *ras_core,
				struct eeprom_umc_record *record, uint32_t nps)
{
	uint64_t pa = 0;
	int ret = 0;

	if (nps == EEPROM_RECORD_UMC_NPS_MODE(record)) {
		record->cur_nps_retired_row_pfn = EEPROM_RECORD_UMC_ADDR_PFN(record);
	} else {
		ret = convert_eeprom_record_to_nps_addr(ras_core,
				record, &pa, nps);
		if (!ret)
			record->cur_nps_retired_row_pfn = RAS_ADDR_TO_PFN(pa);
	}

	record->cur_nps = nps;

	return ret;
}

static int umc_v12_0_eeprom_record_to_nps_pages(struct ras_core_context *ras_core,
			struct eeprom_umc_record *record, uint32_t nps,
			uint64_t *pfns, uint32_t num)
{
	return lookup_bad_pages_in_a_row(ras_core,
				record, nps, pfns, num, 0, false);
}

static int umc_12_0_soc_pa_to_bank(struct ras_core_context *ras_core,
			uint64_t soc_pa,
			struct umc_bank_addr *bank_addr)
{

	int channel_hashed = 0;
	int channel_real = 0;
	int channel_reversed = 0;
	int i = 0;

	bank_addr->stack_id = UMC_V12_0_SOC_PA_TO_SID(soc_pa);
	bank_addr->bank_group = 0; /* This is a combination of SID & Bank. Needed?? */
	bank_addr->bank = UMC_V12_0_SOC_PA_TO_BANK(soc_pa);
	bank_addr->row = UMC_V12_0_SOC_PA_TO_ROW(soc_pa);
	bank_addr->column = UMC_V12_0_SOC_PA_TO_COL(soc_pa);

	/* Channel bits 4-6 are hashed. Bruteforce reverse the hash */
	channel_hashed = (soc_pa >> UMC_V12_0_PA_CH4_BIT) & 0x7;

	for (i = 0; i < 8; i++) {
		channel_reversed = 0;
		channel_reversed |= UMC_V12_0_CHANNEL_HASH_CH4((i << 4), soc_pa);
		channel_reversed |= (UMC_V12_0_CHANNEL_HASH_CH5((i << 4), soc_pa) << 1);
		channel_reversed |= (UMC_V12_0_CHANNEL_HASH_CH6((i << 4), soc_pa) << 2);
		if (channel_reversed == channel_hashed)
			channel_real = ((i << 4)) | ((soc_pa >> UMC_V12_0_PA_CH0_BIT) & 0xf);
	}

	bank_addr->channel = channel_real;
	bank_addr->subchannel = UMC_V12_0_SOC_PA_TO_PC(soc_pa);

	return 0;
}

static int umc_12_0_bank_to_soc_pa(struct ras_core_context *ras_core,
			struct umc_bank_addr bank_addr,
			uint64_t *soc_pa)
{
	uint64_t na = 0;
	uint64_t tmp_pa = 0;
	*soc_pa = 0;

	tmp_pa |= UMC_V12_0_SOC_SID_TO_PA(bank_addr.stack_id);
	tmp_pa |= UMC_V12_0_SOC_BANK_TO_PA(bank_addr.bank);
	tmp_pa |= UMC_V12_0_SOC_ROW_TO_PA(bank_addr.row);
	tmp_pa |= UMC_V12_0_SOC_COL_TO_PA(bank_addr.column);
	tmp_pa |= UMC_V12_0_SOC_CH_TO_PA(bank_addr.channel);
	tmp_pa |= UMC_V12_0_SOC_PC_TO_PA(bank_addr.subchannel);

	/* Get the NA */
	na = ((tmp_pa >> UMC_V12_0_PA_C2_BIT) << UMC_V12_0_NA_C2_BIT);
	na |= tmp_pa & 0xff;

	/* translate umc channel address to soc pa, 3 parts are included */
	tmp_pa = ADDR_OF_32KB_BLOCK(na) |
		ADDR_OF_256B_BLOCK(bank_addr.channel) |
		OFFSET_IN_256B_BLOCK(na);

	/* the umc channel bits are not original values, they are hashed */
	UMC_V12_0_SET_CHANNEL_HASH(bank_addr.channel, tmp_pa);

	*soc_pa = tmp_pa;

	return 0;
}

const struct ras_umc_ip_func ras_umc_func_v12_0 = {
	.bank_to_eeprom_record = umc_v12_0_bank_to_eeprom_record,
	.eeprom_record_to_nps_record = umc_v12_0_eeprom_record_to_nps_record,
	.eeprom_record_to_nps_pages = umc_v12_0_eeprom_record_to_nps_pages,
	.bank_to_soc_pa = umc_12_0_bank_to_soc_pa,
	.soc_pa_to_bank = umc_12_0_soc_pa_to_bank,
};

