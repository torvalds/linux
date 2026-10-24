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
#include "ras_umc_v12_0.h"
#include "ras_umc_v13_0.h"
#include "ras_umc_v15_0.h"

#define MAX_ECC_NUM_PER_RETIREMENT  16

/* bad page timestamp format
 * yy[31:27] mm[26:23] day[22:17] hh[16:12] mm[11:6] ss[5:0]
 */
#define EEPROM_TIMESTAMP_MINUTE  6
#define EEPROM_TIMESTAMP_HOUR    12
#define EEPROM_TIMESTAMP_DAY     17
#define EEPROM_TIMESTAMP_MONTH   23
#define EEPROM_TIMESTAMP_YEAR    27

static uint64_t ras_umc_get_eeprom_timestamp(struct ras_core_context *ras_core)
{
	struct ras_time tm = {0};
	uint64_t utc_timestamp = 0;
	uint64_t eeprom_timestamp = 0;

	utc_timestamp = ras_core_get_utc_second_timestamp(ras_core);
	if (!utc_timestamp)
		return utc_timestamp;

	ras_core_convert_timestamp_to_time(ras_core, utc_timestamp, &tm);

	/* the year range is 2000 ~ 2031, set the year if not in the range */
	if (tm.tm_year < 2000)
		tm.tm_year = 2000;
	if (tm.tm_year > 2031)
		tm.tm_year = 2031;

	tm.tm_year -= 2000;

	eeprom_timestamp = tm.tm_sec + (tm.tm_min << EEPROM_TIMESTAMP_MINUTE)
				+ (tm.tm_hour << EEPROM_TIMESTAMP_HOUR)
				+ (tm.tm_mday << EEPROM_TIMESTAMP_DAY)
				+ (tm.tm_mon << EEPROM_TIMESTAMP_MONTH)
				+ (tm.tm_year << EEPROM_TIMESTAMP_YEAR);
	eeprom_timestamp &= 0xffffffff;

	return eeprom_timestamp;
}

static const struct ras_umc_ip_func *ras_umc_get_ip_func(
				struct ras_core_context *ras_core, uint32_t ip_version)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;

	switch (ip_version) {
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 5, 0):
		ras_umc->max_pages_per_row = 16;
		return &ras_umc_func_v12_0;
	case IP_VERSION(15, 0, 0):
		ras_umc->max_pages_per_row = 128;
		return &ras_umc_func_v15_0;
	case IP_VERSION(13, 1, 0):
		ras_umc->max_pages_per_row = 128;
		return &ras_umc_func_v13_0;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"UMC ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

int ras_umc_ras_ta_translate_addr(struct ras_core_context *ras_core,
		struct umc_mca_addr *in, struct umc_phy_addr *out,
		uint32_t nps)
{
	struct ras_ta_query_address_input addr_in;
	struct ras_ta_query_address_output addr_out;
	int ret;

	if (!in)
		return -EINVAL;

	memset(&addr_in, 0, sizeof(addr_in));
	memset(&addr_out, 0, sizeof(addr_out));

	addr_in.ma.err_addr = in->err_addr;
	addr_in.ma.ch_inst = in->ch_inst;
	addr_in.ma.umc_inst = in->umc_inst;
	addr_in.ma.node_inst = in->node_inst;
	addr_in.ma.socket_id = in->socket_id;

	addr_in.addr_type = RAS_TA_MCA_TO_PA;

	ret = ras_psp_query_address(ras_core, &addr_in, &addr_out);
	if (ret) {
		RAS_DEV_WARN(ras_core->dev,
			"Failed to query RAS physical address for 0x%llx, ret:%d",
			in->err_addr, ret);
		return -EREMOTEIO;
	}

	if (out) {
		out->pa = addr_out.pa.pa;
		out->bank = addr_out.pa.bank;
		out->channel_idx = addr_out.pa.channel_idx;
	}

	return 0;
}

int ras_umc_psp_translate_addr(struct ras_core_context *ras_core,
		struct umc_mca_addr *in, struct umc_phy_addr *out,
		uint32_t nps)
{
	struct ras_psp_addr_trans_in psp_in = {0};
	struct ras_psp_addr_trans_out psp_out = {0};
	int ret;

	psp_in.mca_addr = in->mca_addr;
	psp_in.ipid = in->ipid;
	psp_in.nps = nps;

	ret = ras_psp_translate_addr(ras_core, &psp_in, &psp_out);
	if (ret)
		return ret;

	out->pa = psp_out.row_pa;
	out->pa_flip_mask = psp_out.pa_flip_mask;

	return 0;
}

static int ras_umc_log_ecc(struct ras_core_context *ras_core,
		unsigned long idx, void *data)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int ret;

	mutex_lock(&ras_umc->tree_lock);
	ret = radix_tree_insert(&ras_umc->root, idx, data);
	mutex_unlock(&ras_umc->tree_lock);

	return ret;
}

int ras_umc_clear_logged_ecc(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	uint64_t buf[8] = {0};
	void  **slot;
	void *data;
	void *iter = buf;

	mutex_lock(&ras_umc->tree_lock);
	radix_tree_for_each_slot(slot, &ras_umc->root, iter, 0) {
		data = ras_radix_tree_delete_iter(&ras_umc->root, iter);
		kfree(data);
	}
	mutex_unlock(&ras_umc->tree_lock);

	return 0;
}

int ras_umc_alloc_row_pages(struct ras_core_context *ras_core,
		uint64_t **page_pfns, uint32_t *nr_page_pfns)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	uint64_t *address;
	uint64_t page_num;

	if (!page_pfns || !nr_page_pfns)
		return -EINVAL;

	if (!ras_umc->max_pages_per_row) {
		RAS_DEV_ERR(ras_core->dev, "max_pages_per_row was not initialized!\n");
		return -EPERM;
	}

	page_num = ras_umc->max_pages_per_row;

	address = kcalloc(page_num, sizeof(*address), GFP_KERNEL);
	if (!address)
		return -ENOMEM;

	*page_pfns = address;
	*nr_page_pfns = page_num;

	return 0;
}

int ras_umc_free_row_pages(struct ras_core_context *ras_core,
		uint64_t *page_pfns)
{
	if (!page_pfns)
		return -EINVAL;

	kfree(page_pfns);

	return 0;
}

static int ras_umc_expand_row_pages(struct ras_core_context *ras_core,
	struct eeprom_umc_record *record, uint64_t *page_pfns, uint32_t nr_page_pfns)
{
	uint64_t retired_addr = RAS_PFN_TO_ADDR(record->cur_nps_retired_row_pfn);
	uint64_t flip_mask = record->cur_nps_pa_flip_mask;
	uint64_t subset;
	uint64_t row_pa, addr;
	uint32_t count = 0;

	if (!retired_addr || !flip_mask || !page_pfns || !nr_page_pfns)
		return -ENOEXEC;

	row_pa = retired_addr & ~(flip_mask);

	if ((count < nr_page_pfns) &&
	    !ras_core_check_address_sanity(ras_core, row_pa))
		page_pfns[count++] = RAS_ADDR_TO_PFN(row_pa);

	subset = flip_mask;
	while (subset) {
		addr = row_pa ^ subset;

		if (count >= nr_page_pfns)
			break;

		if (!ras_core_check_address_sanity(ras_core, addr))
			page_pfns[count++] = RAS_ADDR_TO_PFN(addr);

		subset = (subset - 1) & flip_mask;
	};

	return count;
}

int ras_umc_convert_record_to_row_pages(struct ras_core_context *ras_core,
	struct eeprom_umc_record *record, uint64_t *page_pfns, uint32_t nr_page_pfns)
{
	uint32_t new_nps;
	int ret, count = 0;

	if (!page_pfns || !nr_page_pfns || !record ||
	    (record->cur_nps > UMC_MEMORY_PARTITION_MODE_NPS8))
		return -EINVAL;

	if (!record->cur_nps || !record->cur_nps_retired_row_pfn ||
	    !record->cur_nps_pa_flip_mask) {
		new_nps = record->cur_nps ?
			record->cur_nps : ras_core_get_curr_nps_mode(ras_core);
		ret = ras_umc_record_to_nps_record(ras_core, record, new_nps);
		if (ret)
			return ret;
	}

	count = ras_umc_expand_row_pages(ras_core, record, page_pfns, nr_page_pfns);
	if (count >= 0)
		record->cur_nps_valid_page_num = count;

	return count;
}

static void ras_umc_reserve_row_pages(struct ras_core_context *ras_core,
	struct eeprom_umc_record *record, uint64_t *pages, uint32_t nr_pages)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int i;

	if (!pages || !nr_pages ||
		(nr_pages > ras_umc->max_pages_per_row))
		return;

	/* Reserve memory */
	for (i = 0; i < nr_pages; i++)
		ras_core_event_notify(ras_core, ras_core_in_early_init(ras_core) ?
			RAS_EVENT_ID__EARLY_INIT_RESERVE_PAGE : RAS_EVENT_ID__RESERVE_BAD_PAGE,
			&pages[i]);
}

/* When gpu reset is ongoing, ecc logging operations will be pended.
 *
 * The pending list is bounded by RAS_UMC_PENDING_ECC_MAX so that an ECC
 * storm or repeated UMC error injection cannot make this list (and the
 * kernel allocations behind it) grow without bound. Once the limit is
 * reached, additional events are dropped and counted in
 * pending_ecc_dropped, with a rate-limited warning emitted.
 */
int ras_umc_log_bad_bank_pending(struct ras_core_context *ras_core, struct ras_bank_ecc *bank)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct ras_bank_ecc_node *ecc_node;

	mutex_lock(&ras_umc->pending_ecc_lock);
	if (ras_umc->pending_ecc_count >= RAS_UMC_PENDING_ECC_MAX) {
		ras_umc->pending_ecc_dropped++;
		mutex_unlock(&ras_umc->pending_ecc_lock);
		RAS_DEV_WARN_RATELIMITED(ras_core->dev,
			"pending ECC list full (%u), dropping bad bank event (total dropped:%u)\n",
			RAS_UMC_PENDING_ECC_MAX, ras_umc->pending_ecc_dropped);
		return -ENOSPC;
	}
	mutex_unlock(&ras_umc->pending_ecc_lock);

	ecc_node = kzalloc_obj(*ecc_node);
	if (!ecc_node)
		return -ENOMEM;

	memcpy(&ecc_node->ecc, bank, sizeof(ecc_node->ecc));

	mutex_lock(&ras_umc->pending_ecc_lock);
	/* re-check under the lock to honor the cap across concurrent callers */
	if (ras_umc->pending_ecc_count >= RAS_UMC_PENDING_ECC_MAX) {
		ras_umc->pending_ecc_dropped++;
		mutex_unlock(&ras_umc->pending_ecc_lock);
		kfree(ecc_node);
		return -ENOSPC;
	}
	list_add_tail(&ecc_node->node, &ras_umc->pending_ecc_list);
	ras_umc->pending_ecc_count++;
	mutex_unlock(&ras_umc->pending_ecc_lock);

	return 0;
}

/* After gpu reset is complete, re-log the pending error banks.
 */
int ras_umc_log_pending_bad_bank(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct ras_bank_ecc_node *ecc_node, *tmp;

	mutex_lock(&ras_umc->pending_ecc_lock);
	list_for_each_entry_safe(ecc_node,
		tmp, &ras_umc->pending_ecc_list, node){
		if (!ras_umc_log_bad_bank(ras_core, &ecc_node->ecc)) {
			list_del(&ecc_node->node);
			kfree(ecc_node);
			if (ras_umc->pending_ecc_count)
				ras_umc->pending_ecc_count--;
		}
	}
	if (ras_umc->pending_ecc_dropped) {
		RAS_DEV_WARN(ras_core->dev,
			"%u pending ECC bad-bank events were dropped during GPU reset\n",
			ras_umc->pending_ecc_dropped);
		ras_umc->pending_ecc_dropped = 0;
	}
	mutex_unlock(&ras_umc->pending_ecc_lock);

	return 0;
}

int ras_umc_log_bad_bank(struct ras_core_context *ras_core, struct ras_bank_ecc *bank)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_umc_record umc_rec = {0};
	uint32_t c = 0;
	int ret;

	mutex_lock(&ras_umc->bank_log_lock);
	ret = ras_umc_bank_to_umc_record(ras_core, bank, &umc_rec);
	if (ret)
		goto out;

	ret = ras_umc_add_bad_pages(ras_core, &umc_rec, 1, &c);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to log bad bank! ret:%x\n", ret);
		goto out;
	}

	if (c)
		ret = ras_core_event_notify(ras_core,
				RAS_EVENT_ID__BAD_PAGE_DETECTED, NULL);

out:
	mutex_unlock(&ras_umc->bank_log_lock);
	return ret;
}

int ras_umc_ma2pa(struct ras_core_context *ras_core,
	struct umc_mca_addr *addr_in, struct umc_phy_addr *addr_out,
	uint32_t nps)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int ret;

	if (ras_psp_check_supported_cmd(ras_core, RAS_TA_CMD_ID__QUERY_ADDRESS)) {
		ret = ras_umc_ras_ta_translate_addr(ras_core, addr_in, addr_out, nps);
	} else {
		if (ras_umc->ip_func && ras_umc->ip_func->ma2pa) {
			ret = ras_umc->ip_func->ma2pa(ras_core, addr_in, addr_out, nps);
		} else {
			RAS_DEV_ERR(ras_core->dev, "ma2pa is not supported!\n");
			ret = -EOPNOTSUPP;
		}
	}

	return ret;
}

static int ras_umc_pa2ma(struct ras_core_context *ras_core, uint64_t pa,
	uint64_t *mca, uint32_t nps)
{
	struct ras_ta_query_address_input addr_in;
	struct ras_ta_query_address_output addr_out;
	int ret;

	if (!ras_psp_check_supported_cmd(ras_core, RAS_TA_CMD_ID__QUERY_ADDRESS))
		return -EOPNOTSUPP;

	memset(&addr_in, 0, sizeof(addr_in));
	memset(&addr_out, 0, sizeof(addr_out));
	/* nps: the pa belongs to, always NPS1 for legacy eeprom data */
	addr_in.pa.pa = pa | ((uint64_t)nps << UMC_PA_NPS_SHIFT);
	addr_in.addr_type = RAS_TA_PA_TO_MCA;
	ret = ras_psp_query_address(ras_core, &addr_in, &addr_out);
	if (ret) {
		RAS_DEV_WARN_RATELIMITED(ras_core->dev,
			"Failed to query RAS MCA address for 0x%llx, ret:%d\n", pa, ret);

		return -EREMOTEIO;
	}

	*mca = addr_out.ma.err_addr;
	return 0;
}

static int __ras_umc_eeprom_rec2nps_addr(struct ras_core_context *ras_core,
	struct eeprom_umc_record *record, uint64_t *pa,
	uint32_t nps, uint32_t die_id)
{
	struct device_system_info dev_info = {0};
	struct umc_mca_addr addr_in;
	struct umc_phy_addr addr_out;
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int ret;

	memset(&addr_in, 0, sizeof(addr_in));
	memset(&addr_out, 0, sizeof(addr_out));

	ras_core_get_device_system_info(ras_core, &dev_info);

	addr_in.err_addr = record->address;
	addr_in.ch_inst = record->mem_channel;
	addr_in.umc_inst = record->mcumc_id;
	addr_in.node_inst = die_id;
	addr_in.socket_id = dev_info.socket_id;

	ret = ras_umc_ma2pa(ras_core, &addr_in, &addr_out, nps);
	if (ret)
		return ret;

	if (ras_umc->ip_func && ras_umc->ip_func->nps_pa_to_row_pa) {
		*pa = ras_umc->ip_func->nps_pa_to_row_pa(ras_core, addr_out.pa,
			nps, false);
	} else {
		RAS_DEV_ERR(ras_core->dev, "nps_pa_to_row_pa is not supported!\n");
		return -EOPNOTSUPP;
	}

	return ret;
}

static int ras_umc_eeprom_rec2nps_addr(struct ras_core_context *ras_core,
		struct eeprom_umc_record *record, uint64_t *pa, uint32_t nps)
{
	return __ras_umc_eeprom_rec2nps_addr(ras_core, record, pa, nps,
			UMC_INV_AID_NODE);
}

/* For legacy eeprom data format, the scope of channel index is
 * limited to umc instance, and die id is not stored, have to
 * get it from PA
 */
static int ras_umc_eeprom_rec2nps_addr_legacy(struct ras_core_context *ras_core,
		struct eeprom_umc_record *record, uint64_t *pa, uint32_t nps)
{
	uint32_t die_id;

	/* the die id is derived from an NPS1-mode PA(legacy-format EEPROMs
	 * only ever existed on NPS1 systems)
	 */
	if (ras_core->ras_umc.ip_func && ras_core->ras_umc.ip_func->get_die_id) {
		die_id = ras_core->ras_umc.ip_func->get_die_id(ras_core,
				record->address,
				RAS_PFN_TO_ADDR(EEPROM_RECORD_UMC_ADDR_PFN(record)));
	} else {
		RAS_DEV_ERR(ras_core->dev, "get_die_id is not supported!\n");
		return -EOPNOTSUPP;
	}

	return __ras_umc_eeprom_rec2nps_addr(ras_core, record, pa, nps, die_id);
}

static int ras_umc_eeprom_rec2nps_rec(struct ras_core_context *ras_core,
	struct eeprom_umc_record *record, uint32_t nps)
{
	uint64_t ch_idx_v2, pa = 0;
	uint32_t save_nps;
	int ret = 0;

	save_nps = EEPROM_RECORD_UMC_NPS_MODE(record);
	/* eeprom v2 has no stored nps, always convert if the flag is set */
	ch_idx_v2 = record->retired_row_pfn & UMC_CHANNEL_IDX_V2;
	record->cur_nps = nps;

	if (save_nps || ch_idx_v2) {
		if ((nps == save_nps) &&
		    !ras_eeprom_mgr_fw_record_enabled(ras_core)) {
			record->cur_nps_retired_row_pfn =
				EEPROM_RECORD_UMC_ADDR_PFN(record);
		} else {
			ret = ras_umc_eeprom_rec2nps_addr(ras_core, record, &pa, nps);
			if (!ret)
				record->cur_nps_retired_row_pfn = RAS_ADDR_TO_PFN(pa);
		}
	} else {
		/* for specific old eeprom data, mca address is not stored(0 is
		 * default value), calc it from pa(it's nps1 in this case, other
		 * nps modes are introduced later)
		 */
		if (record->address == 0) {
			ret = ras_umc_pa2ma(ras_core,
				RAS_PFN_TO_ADDR(EEPROM_RECORD_UMC_ADDR_PFN(record)),
				&record->address, UMC_MEMORY_PARTITION_MODE_NPS1);
			if (ret)
				return ret;
		}

		/* old eeprom data format, the scope of channel index is
		 * limited to umc instance
		 */
		ret = ras_umc_eeprom_rec2nps_addr_legacy(ras_core, record, &pa, nps);
		if (!ret)
			record->cur_nps_retired_row_pfn = RAS_ADDR_TO_PFN(pa);
	}

	return ret;
}

static bool ras_umc_check_logged_record(struct ras_core_context *ras_core,
			struct eeprom_umc_record *record)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	void *res = NULL;

	mutex_lock(&ras_umc->tree_lock);
	res = radix_tree_lookup(&ras_umc->root, record->cur_nps_retired_row_pfn);
	mutex_unlock(&ras_umc->tree_lock);

	return res ? true : false;
}

static bool ras_umc_check_retired_record(struct ras_core_context *ras_core,
				struct eeprom_umc_record *record)
{
	uint32_t nps = 0;
	int ret;

	nps = ras_core_get_curr_nps_mode(ras_core);
	ret = ras_umc_record_to_nps_record(ras_core, record, nps);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to translate nps record! ret:%d\n", ret);
		return true;
	}

	if (ras_umc_check_logged_record(ras_core, record))
		return true;

	return false;
}

static int ras_umc_log_record(struct ras_core_context *ras_core,
				struct eeprom_umc_record *record)
{
	struct eeprom_umc_record *rec;
	int ret;

	rec = kzalloc(sizeof(*rec), GFP_KERNEL);
	if (!rec)
		return -ENOMEM;

	memcpy(rec, record, sizeof(*rec));

	ret = ras_umc_log_ecc(ras_core, rec->cur_nps_retired_row_pfn, rec);
	if (ret)
		kfree(rec);

	return ret;
}

/* alloc/realloc bps array */
static int ras_umc_realloc_err_data_space(struct ras_core_context *ras_core,
		struct eeprom_store_record *data, int pages)
{
	unsigned int old_space = data->count + data->space_left;
	unsigned int new_space = old_space + pages;
	unsigned int align_space = ALIGN(new_space, 512);
	void *bps = kzalloc(align_space * sizeof(*data->bps), GFP_KERNEL);

	if (!bps)
		return -ENOMEM;

	if (data->bps) {
		memcpy(bps, data->bps,
				data->count * sizeof(*data->bps));
		kfree(data->bps);
	}

	data->bps = bps;
	data->space_left += align_space - old_space;
	return 0;
}

static int ras_umc_update_eeprom_rom_data(struct ras_core_context *ras_core,
		struct eeprom_umc_record *bps)
{
	struct eeprom_store_record *data = &ras_core->ras_umc.umc_err_data.rom_data;

	if (!data->space_left &&
		ras_umc_realloc_err_data_space(ras_core, data, 256)) {
		return	-ENOMEM;
	}

	memcpy(&data->bps[data->count], bps, sizeof(*data->bps));
	data->count++;
	data->space_left--;

	/* update bad channel bitmap */
	if (bps->mem_channel < BITS_PER_TYPE(data->umc_channel_bitmap))
		data->umc_channel_bitmap |= 0x1ULL << bps->mem_channel;

	return 0;
}

static int ras_umc_update_eeprom_ram_data(struct ras_core_context *ras_core,
		struct eeprom_umc_record *bps, uint64_t *page_pfns, uint32_t nr_page_pfns)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_store_record *data = &ras_umc->umc_err_data.ram_data;
	int j;

	if (!bps || !page_pfns ||
		(nr_page_pfns > ras_umc->max_pages_per_row))
		return -EINVAL;

	if (!nr_page_pfns)
		return 0;

	if (!data->space_left &&
		ras_umc_realloc_err_data_space(ras_core, data, 256))
		return -ENOMEM;

	for (j = 0; j < nr_page_pfns; j++) {
		bps->cur_nps_retired_row_pfn = page_pfns[j];
		memcpy(&data->bps[data->count], bps, sizeof(*data->bps));
		data->count++;
		data->space_left--;
	}

	/* update bad channel bitmap */
	if (bps->mem_channel < BITS_PER_TYPE(data->umc_channel_bitmap))
		data->umc_channel_bitmap |= 0x1ULL << bps->mem_channel;

	return 0;
}

void ras_umc_report_badpage_info(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_store_record *data = &ras_umc->umc_err_data.ram_data;

	if (ras_umc->last_record_count != data->count) {
		ras_umc->last_record_count = data->count;
		ras_core_event_notify(ras_core, RAS_EVENT_ID__UPDATE_BAD_PAGE_NUM,
			&ras_umc->last_record_count);
	}

	if (ras_umc->last_channel_bitmap != data->umc_channel_bitmap) {
		ras_umc->last_channel_bitmap = data->umc_channel_bitmap;
		ras_core_event_notify(ras_core, RAS_EVENT_ID__UPDATE_BAD_CHANNEL_BITMAP,
			&ras_umc->last_channel_bitmap);
	}

	if (ras_core->is_rma)
		ras_core_event_notify(ras_core, RAS_EVENT_ID__DEVICE_RMA, NULL);
}

int ras_umc_add_bad_pages(struct ras_core_context *ras_core,
	struct eeprom_umc_record *bps, uint32_t bps_sz, uint32_t *valid_sz)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	uint64_t *page_pfns = NULL;
	uint32_t nr_page_pfns = 0;
	int nr_valid_pfns = 0;
	uint32_t i, c = 0;
	int ret = 0;

	if (!bps || !bps_sz || !valid_sz)
		return -EINVAL;

	ret = ras_umc_alloc_row_pages(ras_core, &page_pfns, &nr_page_pfns);
	if (ret)
		return ret;

	mutex_lock(&ras_umc->umc_lock);
	for (i = 0; i < bps_sz; i++) {
		if (ras_umc_check_retired_record(ras_core, &bps[i]))
			continue;

		nr_valid_pfns = ras_umc_convert_record_to_row_pages(ras_core,
					&bps[i], page_pfns, nr_page_pfns);
		if (nr_valid_pfns < 0) {
			RAS_DEV_ERR(ras_core->dev,
				"Failed to lookup record bad pages! %d\n", nr_valid_pfns);
			ret = nr_valid_pfns;
			goto out;
		}

		ret = ras_umc_update_eeprom_rom_data(ras_core, &bps[i]);
		if (ret)
			goto out;

		ret = ras_umc_log_record(ras_core, &bps[i]);
		if (ret)
			goto out;

		ras_umc_reserve_row_pages(ras_core,
			&bps[i], page_pfns, nr_valid_pfns);

		ret = ras_umc_update_eeprom_ram_data(ras_core,
				&bps[i], page_pfns, nr_valid_pfns);
		if (ret)
			goto out;
		c++;
	}

	*valid_sz = c;

	if (c) {
		ras_eeprom_mgr_check_and_report_status(ras_core, true);
		if (!ras_core_in_early_init(ras_core))
			ras_umc_report_badpage_info(ras_core);
	}

out:
	mutex_unlock(&ras_umc->umc_lock);
	ras_umc_free_row_pages(ras_core, page_pfns);
	return ret;
}

/*
 * read error record array in eeprom and reserve enough space for
 * storing new bad pages
 */
int ras_umc_load_bad_pages(struct ras_core_context *ras_core)
{
	struct eeprom_umc_record *bps;
	uint32_t c = 0;
	int ras_num_recs, ret;

	ras_num_recs = ras_eeprom_mgr_get_record_count(ras_core);
	if (ras_num_recs <= 0)
		return ras_num_recs;

	bps = kzalloc_objs(*bps, ras_num_recs);
	if (!bps)
		return -ENOMEM;

	ret = ras_eeprom_mgr_get_records(ras_core, 0, bps, ras_num_recs);
	if (ret)
		RAS_DEV_ERR(ras_core->dev,
			"Failed to load EEPROM table records! ret:%d\n", ret);
	else
		ret = ras_umc_add_bad_pages(ras_core, bps, ras_num_recs, &c);

	kfree(bps);
	return ret;
}

static int ras_umc_count_valid_pages(struct ras_core_context *ras_core,
		struct eeprom_umc_record *records, const u32 nr_records)
{
	int count = 0, i;

	for (i = 0; i < nr_records; i++)
		count += records[i].cur_nps_valid_page_num;

	return count;
}

/*
 * write error record array to eeprom, the function should be
 * protected by recovery_lock
 * new_cnt: new added UE count, excluding reserved bad pages, can be NULL
 */
static int ras_umc_save_bad_pages(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_store_record *data = &ras_umc->umc_err_data.rom_data;
	int eeprom_record_num;
	int save_count;
	int ret = -ENODATA;

	/* Not need to save bad pages when FW management EEPROM is enabled. */
	if (ras_eeprom_mgr_fw_record_enabled(ras_core))
		return 0;

	if (!data->bps)
		return -EINVAL;

	eeprom_record_num = ras_eeprom_mgr_get_record_count(ras_core);
	if (eeprom_record_num < 0)
		return eeprom_record_num;

	mutex_lock(&ras_umc->umc_lock);
	save_count = data->count - eeprom_record_num;
	/* only new entries are saved */
	if (save_count > 0) {
		ret = ras_eeprom_mgr_append_records(ras_core,
				&data->bps[eeprom_record_num], save_count);
		if (ret) {
			RAS_DEV_ERR(ras_core->dev,
				"Failed to save EEPROM table data! ret:%d\n", ret);
			ret = -EIO;
			goto exit;
		}

		RAS_DEV_INFO(ras_core->dev, "Saved %d pages to EEPROM table.\n",
			ras_umc_count_valid_pages(ras_core,
				&data->bps[eeprom_record_num], save_count));
	}

exit:
	mutex_unlock(&ras_umc->umc_lock);
	return ret;
}

int ras_umc_handle_bad_pages(struct ras_core_context *ras_core, void *data)
{
	return ras_umc_save_bad_pages(ras_core);
}

int ras_umc_sw_init(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;

	memset(ras_umc, 0, sizeof(*ras_umc));

	INIT_LIST_HEAD(&ras_umc->pending_ecc_list);

	INIT_RADIX_TREE(&ras_umc->root, GFP_KERNEL);

	mutex_init(&ras_umc->tree_lock);
	mutex_init(&ras_umc->pending_ecc_lock);
	mutex_init(&ras_umc->umc_lock);
	mutex_init(&ras_umc->bank_log_lock);

	ras_umc->umc_ip_version = ras_core->config->umc_ip_version;
	ras_umc->ip_func = ras_umc_get_ip_func(ras_core, ras_umc->umc_ip_version);
	if (!ras_umc->ip_func) {
		RAS_DEV_ERR(ras_core->dev, "Failed to get umc ip function!\n");
		return -EINVAL;
	}

	return 0;
}

int ras_umc_sw_fini(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct ras_umc_err_data *umc_err_data = &ras_umc->umc_err_data;
	struct ras_bank_ecc_node *ecc_node, *tmp;

	mutex_destroy(&ras_umc->umc_lock);
	mutex_destroy(&ras_umc->bank_log_lock);

	if (umc_err_data->rom_data.bps) {
		umc_err_data->rom_data.count = 0;
		kfree(umc_err_data->rom_data.bps);
		umc_err_data->rom_data.bps = NULL;
		umc_err_data->rom_data.space_left = 0;
	}

	if (umc_err_data->ram_data.bps) {
		umc_err_data->ram_data.count = 0;
		kfree(umc_err_data->ram_data.bps);
		umc_err_data->ram_data.bps = NULL;
		umc_err_data->ram_data.space_left = 0;
	}

	ras_umc_clear_logged_ecc(ras_core);

	mutex_lock(&ras_umc->pending_ecc_lock);
	list_for_each_entry_safe(ecc_node,
		tmp, &ras_umc->pending_ecc_list, node){
		list_del(&ecc_node->node);
		kfree(ecc_node);
	}
	ras_umc->pending_ecc_count = 0;
	ras_umc->pending_ecc_dropped = 0;
	mutex_unlock(&ras_umc->pending_ecc_lock);

	mutex_destroy(&ras_umc->tree_lock);
	mutex_destroy(&ras_umc->pending_ecc_lock);

	return 0;
}

int ras_umc_hw_init(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int count = ras_umc_get_badpage_count(ras_core);

	ras_umc->num_umc = ras_core->config->umc_cfg.num_umc;
	ras_umc->pa_base = ras_core->config->umc_cfg.pa_base;
	ras_umc->lfb_size = ras_core->config->umc_cfg.lfb_size;

	/* For preloaded bad pages case, bad page info is
	 * deferred to report in hw init.
	 */
	if (count > 0)
		ras_umc_report_badpage_info(ras_core);

	return 0;
}

int ras_umc_hw_fini(struct ras_core_context *ras_core)
{
	return 0;
}

int ras_umc_clean_badpage_data(struct ras_core_context *ras_core)
{
	struct ras_umc_err_data *data = &ras_core->ras_umc.umc_err_data;

	mutex_lock(&ras_core->ras_umc.umc_lock);

	kfree(data->rom_data.bps);
	kfree(data->ram_data.bps);

	memset(data, 0, sizeof(*data));
	mutex_unlock(&ras_core->ras_umc.umc_lock);

	return 0;
}

int ras_umc_fill_eeprom_record(struct ras_core_context *ras_core,
		uint64_t err_addr, uint32_t umc_inst, struct umc_phy_addr *cur_nps_addr,
		enum umc_memory_partition_mode cur_nps, struct eeprom_umc_record *record)
{
	struct eeprom_umc_record *err_rec = record;

	/* Set bad page pfn and nps mode */
	EEPROM_RECORD_SETUP_UMC_ADDR_AND_NPS(err_rec,
			RAS_ADDR_TO_PFN(cur_nps_addr->pa), cur_nps);

	err_rec->address = err_addr;
	err_rec->ts = ras_umc_get_eeprom_timestamp(ras_core);
	err_rec->err_type = RAS_EEPROM_ERR_NON_RECOVERABLE;
	err_rec->cu = 0;
	err_rec->mem_channel = cur_nps_addr->channel_idx;
	err_rec->mcumc_id = umc_inst;
	err_rec->cur_nps_retired_row_pfn = RAS_ADDR_TO_PFN(cur_nps_addr->pa);
	err_rec->cur_nps_pa_flip_mask = cur_nps_addr->pa_flip_mask;
	err_rec->cur_nps_bank = cur_nps_addr->bank;
	err_rec->cur_nps = cur_nps;
	return 0;
}

int ras_umc_get_saved_eeprom_count(struct ras_core_context *ras_core)
{
	struct ras_umc_err_data *err_data = &ras_core->ras_umc.umc_err_data;

	return err_data->rom_data.count;
}

int ras_umc_get_badpage_count(struct ras_core_context *ras_core)
{
	struct eeprom_store_record *data = &ras_core->ras_umc.umc_err_data.ram_data;

	return data->count;
}

int ras_umc_get_badpage_record(struct ras_core_context *ras_core, uint32_t index, void *record)
{
	struct eeprom_store_record *data = &ras_core->ras_umc.umc_err_data.ram_data;

	if (index >= data->count)
		return -EINVAL;

	memcpy(record, &data->bps[index], sizeof(struct eeprom_umc_record));
	return 0;
}

bool ras_umc_check_retired_addr(struct ras_core_context *ras_core, uint64_t addr)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_store_record *data = &ras_umc->umc_err_data.ram_data;
	uint64_t page_pfn = RAS_ADDR_TO_PFN(addr);
	int i, ret = false;

	mutex_lock(&ras_umc->umc_lock);
	for (i = 0; i < data->count; i++) {
		if (data->bps[i].cur_nps_retired_row_pfn == page_pfn) {
			ret = true;
			break;
		}
	}
	mutex_unlock(&ras_umc->umc_lock);

	return ret;
}

int ras_umc_translate_soc_pa_and_bank(struct ras_core_context *ras_core,
	uint64_t *soc_pa, struct umc_bank_addr *bank_addr, bool bank_to_pa)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int ret = 0;

	if (bank_to_pa)
		ret = ras_umc->ip_func->bank_to_soc_pa(ras_core, *bank_addr, soc_pa);
	else
		ret = ras_umc->ip_func->soc_pa_to_bank(ras_core, *soc_pa, bank_addr);

	return ret;
}

uint32_t ras_umc_bit_wise_xor(uint32_t val)
{
	uint32_t result = 0;
	int i;

	for (i = 0; i < 32; i++)
		result = result ^ ((val >> i) & 0x1);

	return result;
}

int ras_umc_bank_to_umc_record(struct ras_core_context *ras_core,
		struct ras_bank_ecc *bank, struct eeprom_umc_record *record)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int ret;

	if (!bank || !record || !ras_umc->ip_func ||
	    !ras_umc->ip_func->bank_to_eeprom_record)
		return -EINVAL;

	ret = ras_umc->ip_func->bank_to_eeprom_record(ras_core, bank, record);
	if (ret)
		return ret;

	record->ipid = bank->ipid;

	return 0;
}

int ras_umc_record_to_nps_record(struct ras_core_context *ras_core,
		struct eeprom_umc_record *record,  uint32_t nps)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	uint64_t ch_idx_v2;
	uint32_t save_nps;

	if (!record || !nps ||
		(nps >= UMC_MEMORY_PARTITION_MODE_UNKNOWN))
		return -EINVAL;

	/* Avoid redundant conversion for the same NPS mode */
	if ((record->cur_nps == nps) && record->cur_nps_retired_row_pfn &&
	    record->cur_nps_pa_flip_mask)
		return 0;

	save_nps = EEPROM_RECORD_UMC_NPS_MODE(record);
	ch_idx_v2 = record->retired_row_pfn & UMC_CHANNEL_IDX_V2;
	if (!save_nps && !ch_idx_v2)
		return ras_umc_eeprom_rec2nps_rec(ras_core, record, nps);

	if (!ras_umc->ip_func || !ras_umc->ip_func->eeprom_record_to_nps_record)
		return -EOPNOTSUPP;

	return ras_umc->ip_func->eeprom_record_to_nps_record(ras_core, record, nps);
}

int ras_umc_dump_fw_records(struct ras_core_context *ras_core)
{
	struct eeprom_umc_record rec;
	int eeprom_count, umc_count, new_count = 0;
	uint32_t c;
	int i, ret;

	eeprom_count = ras_eeprom_mgr_get_record_count(ras_core);
	/* no bad page record, skip eeprom access */
	if (eeprom_count <= 0)
		return eeprom_count;

	umc_count = ras_umc_get_saved_eeprom_count(ras_core);
	if (umc_count == eeprom_count) {
		return 0;
	} else if (umc_count > eeprom_count) {
		RAS_DEV_ERR(ras_core->dev, "Invalid error count: eeprom:%d, umc:%d\n",
			eeprom_count, umc_count);
		return 0;
	}

	for (i = umc_count; i < eeprom_count; i++) {
		memset(&rec, 0, sizeof(rec));
		ret = ras_eeprom_mgr_get_records(ras_core, i, &rec, 1);
		if (ret)
			return 0;

		c = 0;
		ret = ras_umc_add_bad_pages(ras_core, &rec, 1, &c);
		if (!ret)
			new_count += c;
	}

	return new_count;
}
