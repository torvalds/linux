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
	switch (ip_version) {
	case IP_VERSION(12, 0, 0):
		return &ras_umc_func_v12_0;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"UMC ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

int ras_umc_psp_convert_ma_to_pa(struct ras_core_context *ras_core,
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

static int ras_umc_log_ecc(struct ras_core_context *ras_core,
		unsigned long idx, void *data)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	int ret;

	mutex_lock(&ras_umc->tree_lock);
	ret = radix_tree_insert(&ras_umc->root, idx, data);
	if (!ret)
		radix_tree_tag_set(&ras_umc->root, idx, UMC_ECC_NEW_DETECTED_TAG);
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

static void ras_umc_reserve_eeprom_record(struct ras_core_context *ras_core,
				struct eeprom_umc_record *record)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	uint64_t page_pfn[16];
	int count = 0, i;

	memset(page_pfn, 0, sizeof(page_pfn));
	if (ras_umc->ip_func && ras_umc->ip_func->eeprom_record_to_nps_pages) {
		count = ras_umc->ip_func->eeprom_record_to_nps_pages(ras_core,
					record, record->cur_nps, page_pfn, ARRAY_SIZE(page_pfn));
		if (count <= 0) {
			RAS_DEV_ERR(ras_core->dev,
				"Fail to convert error address! count:%d\n", count);
			return;
		}
	}

	/* Reserve memory */
	for (i = 0; i < count; i++)
		ras_core_event_notify(ras_core,
			RAS_EVENT_ID__RESERVE_BAD_PAGE, &page_pfn[i]);
}

/* When gpu reset is ongoing, ecc logging operations will be pended.
 */
int ras_umc_log_bad_bank_pending(struct ras_core_context *ras_core, struct ras_bank_ecc *bank)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct ras_bank_ecc_node *ecc_node;

	ecc_node = kzalloc(sizeof(*ecc_node), GFP_KERNEL);
	if (!ecc_node)
		return -ENOMEM;

	memcpy(&ecc_node->ecc, bank, sizeof(ecc_node->ecc));

	mutex_lock(&ras_umc->pending_ecc_lock);
	list_add_tail(&ecc_node->node, &ras_umc->pending_ecc_list);
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
		if (ecc_node && !ras_umc_log_bad_bank(ras_core, &ecc_node->ecc)) {
			list_del(&ecc_node->node);
			kfree(ecc_node);
		}
	}
	mutex_unlock(&ras_umc->pending_ecc_lock);

	return 0;
}

int ras_umc_log_bad_bank(struct ras_core_context *ras_core, struct ras_bank_ecc *bank)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_umc_record umc_rec;
	struct eeprom_umc_record *err_rec;
	int ret;

	memset(&umc_rec, 0, sizeof(umc_rec));

	mutex_lock(&ras_umc->bank_log_lock);
	ret = ras_umc->ip_func->bank_to_eeprom_record(ras_core, bank, &umc_rec);
	if (ret)
		goto out;

	err_rec = kzalloc(sizeof(*err_rec), GFP_KERNEL);
	if (!err_rec) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(err_rec, &umc_rec, sizeof(umc_rec));
	ret = ras_umc_log_ecc(ras_core, err_rec->cur_nps_retired_row_pfn, err_rec);
	if (ret) {
		if (ret == -EEXIST) {
			RAS_DEV_INFO(ras_core->dev, "The bad pages have been logged before.\n");
			ret = 0;
		}

		kfree(err_rec);
		goto out;
	}

	ras_umc_reserve_eeprom_record(ras_core, err_rec);

	ret = ras_core_event_notify(ras_core,
			RAS_EVENT_ID__BAD_PAGE_DETECTED, NULL);

out:
	mutex_unlock(&ras_umc->bank_log_lock);
	return ret;
}

static int ras_umc_get_new_records(struct ras_core_context *ras_core,
			struct eeprom_umc_record *records, u32 num)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_umc_record *entries[MAX_ECC_NUM_PER_RETIREMENT];
	u32 entry_num = num < MAX_ECC_NUM_PER_RETIREMENT ? num : MAX_ECC_NUM_PER_RETIREMENT;
	int count = 0;
	int new_detected, i;

	mutex_lock(&ras_umc->tree_lock);
	new_detected = radix_tree_gang_lookup_tag(&ras_umc->root, (void **)entries,
			0, entry_num, UMC_ECC_NEW_DETECTED_TAG);
	for (i = 0; i < new_detected; i++) {
		if (!entries[i])
			continue;

		memcpy(&records[i], entries[i], sizeof(struct eeprom_umc_record));
		count++;
		radix_tree_tag_clear(&ras_umc->root,
				entries[i]->cur_nps_retired_row_pfn, UMC_ECC_NEW_DETECTED_TAG);
	}
	mutex_unlock(&ras_umc->tree_lock);

	return count;
}

static bool ras_umc_check_retired_record(struct ras_core_context *ras_core,
				struct eeprom_umc_record *record, bool from_eeprom)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_store_record *data = &ras_umc->umc_err_data.rom_data;
	uint32_t nps = 0;
	int i, ret;

	if (from_eeprom) {
		nps = ras_umc->umc_err_data.umc_nps_mode;
		if (ras_umc->ip_func && ras_umc->ip_func->eeprom_record_to_nps_record) {
			ret = ras_umc->ip_func->eeprom_record_to_nps_record(ras_core, record, nps);
			if (ret)
				RAS_DEV_WARN(ras_core->dev,
					"Failed to adjust eeprom record, ret:%d", ret);
		}
		return false;
	}

	for (i = 0; i < data->count; i++) {
		if ((data->bps[i].retired_row_pfn == record->retired_row_pfn) &&
		    (data->bps[i].cur_nps_retired_row_pfn == record->cur_nps_retired_row_pfn))
			return true;
	}

	return false;
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
	return 0;
}

static int ras_umc_update_eeprom_ram_data(struct ras_core_context *ras_core,
				struct eeprom_umc_record *bps)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct eeprom_store_record *data = &ras_umc->umc_err_data.ram_data;
	uint64_t page_pfn[16];
	int count = 0, j;

	if (!data->space_left &&
		ras_umc_realloc_err_data_space(ras_core, data, 256)) {
		return	-ENOMEM;
	}

	memset(page_pfn, 0, sizeof(page_pfn));
	if (ras_umc->ip_func && ras_umc->ip_func->eeprom_record_to_nps_pages)
		count = ras_umc->ip_func->eeprom_record_to_nps_pages(ras_core,
					bps, bps->cur_nps, page_pfn, ARRAY_SIZE(page_pfn));

	if (count > 0) {
		for (j = 0; j < count; j++) {
			bps->cur_nps_retired_row_pfn = page_pfn[j];
			memcpy(&data->bps[data->count], bps, sizeof(*data->bps));
			data->count++;
			data->space_left--;
		}
	} else {
		memcpy(&data->bps[data->count], bps, sizeof(*data->bps));
		data->count++;
		data->space_left--;
	}

	return 0;
}

/* it deal with vram only. */
static int ras_umc_add_bad_pages(struct ras_core_context *ras_core,
				 struct eeprom_umc_record *bps,
				 int pages, bool from_eeprom)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	struct ras_umc_err_data *data = &ras_umc->umc_err_data;
	int i, ret = 0;

	if (!bps || pages <= 0)
		return 0;

	mutex_lock(&ras_umc->umc_lock);
	for (i = 0; i < pages; i++) {
		if (ras_umc_check_retired_record(ras_core, &bps[i], from_eeprom))
			continue;

		ret = ras_umc_update_eeprom_rom_data(ras_core, &bps[i]);
		if (ret)
			goto out;

		if (data->last_retired_pfn == bps[i].cur_nps_retired_row_pfn)
			continue;

		data->last_retired_pfn = bps[i].cur_nps_retired_row_pfn;

		if (from_eeprom)
			ras_umc_reserve_eeprom_record(ras_core, &bps[i]);

		ret = ras_umc_update_eeprom_ram_data(ras_core, &bps[i]);
		if (ret)
			goto out;
	}
out:
	mutex_unlock(&ras_umc->umc_lock);

	return ret;
}

/*
 * read error record array in eeprom and reserve enough space for
 * storing new bad pages
 */
int ras_umc_load_bad_pages(struct ras_core_context *ras_core)
{
	struct eeprom_umc_record *bps;
	uint32_t ras_num_recs;
	int ret;

	ras_num_recs = ras_eeprom_get_record_count(ras_core);
	/* no bad page record, skip eeprom access */
	if (!ras_num_recs ||
	    ras_core->ras_eeprom.record_threshold_config == DISABLE_RETIRE_PAGE)
		return 0;

	bps = kcalloc(ras_num_recs, sizeof(*bps), GFP_KERNEL);
	if (!bps)
		return -ENOMEM;

	ret = ras_eeprom_read(ras_core, bps, ras_num_recs);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to load EEPROM table records!");
	} else {
		ras_core->ras_umc.umc_err_data.last_retired_pfn = UMC_INV_MEM_PFN;
		ret = ras_umc_add_bad_pages(ras_core, bps, ras_num_recs, true);
	}

	kfree(bps);
	return ret;
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
	uint32_t eeprom_record_num;
	int save_count;
	int ret = 0;

	if (!data->bps)
		return 0;

	eeprom_record_num = ras_eeprom_get_record_count(ras_core);
	mutex_lock(&ras_umc->umc_lock);
	save_count = data->count - eeprom_record_num;
	/* only new entries are saved */
	if (save_count > 0) {
		if (ras_eeprom_append(ras_core,
					   &data->bps[eeprom_record_num],
					   save_count)) {
			RAS_DEV_ERR(ras_core->dev, "Failed to save EEPROM table data!");
			ret = -EIO;
			goto exit;
		}

		RAS_DEV_INFO(ras_core->dev, "Saved %d pages to EEPROM table.\n", save_count);
	}

exit:
	mutex_unlock(&ras_umc->umc_lock);
	return ret;
}

int ras_umc_handle_bad_pages(struct ras_core_context *ras_core, void *data)
{
	struct eeprom_umc_record records[MAX_ECC_NUM_PER_RETIREMENT];
	int count, ret;

	memset(records, 0, sizeof(records));
	count = ras_umc_get_new_records(ras_core, records, ARRAY_SIZE(records));
	if (count <= 0)
		return -ENODATA;

	ret = ras_umc_add_bad_pages(ras_core, records, count, false);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to add ras bad page!\n");
		return -EINVAL;
	}

	ret = ras_umc_save_bad_pages(ras_core);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to save ras bad page\n");
		return -EINVAL;
	}

	return 0;
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
	mutex_unlock(&ras_umc->pending_ecc_lock);

	mutex_destroy(&ras_umc->tree_lock);
	mutex_destroy(&ras_umc->pending_ecc_lock);

	return 0;
}

int ras_umc_hw_init(struct ras_core_context *ras_core)
{
	struct ras_umc *ras_umc = &ras_core->ras_umc;
	uint32_t nps;

	nps = ras_core_get_curr_nps_mode(ras_core);

	if (!nps || (nps >= UMC_MEMORY_PARTITION_MODE_UNKNOWN)) {
		RAS_DEV_ERR(ras_core->dev, "Invalid memory NPS mode: %u!\n", nps);
		return -ENODATA;
	}

	ras_umc->umc_err_data.umc_nps_mode = nps;

	ras_umc->umc_vram_type = ras_core->config->umc_cfg.umc_vram_type;
	if (!ras_umc->umc_vram_type) {
		RAS_DEV_ERR(ras_core->dev, "Invalid UMC VRAM Type: %u!\n",
			ras_umc->umc_vram_type);
		return -ENODATA;
	}

	ras_umc->umc_ip_version = ras_core->config->umc_ip_version;
	ras_umc->ip_func = ras_umc_get_ip_func(ras_core, ras_umc->umc_ip_version);
	if (!ras_umc->ip_func)
		return -EINVAL;

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
