/*
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "coredump.h"

#include <linux/devcoredump.h>
#include <linux/utsname.h>

#include "debug.h"

#ifdef CONFIG_DEV_COREDUMP

struct ath10k_fw_crash_data *ath10k_coredump_new(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->debug.fw_crash_data;

	lockdep_assert_held(&ar->data_lock);

	crash_data->crashed_since_read = true;
	guid_gen(&crash_data->guid);
	ktime_get_real_ts64(&crash_data->timestamp);

	return crash_data;
}
EXPORT_SYMBOL(ath10k_coredump_new);

static struct ath10k_dump_file_data *ath10k_coredump_build(struct ath10k *ar,
							   bool mark_read)
{
	struct ath10k_fw_crash_data *crash_data = ar->debug.fw_crash_data;
	struct ath10k_ce_crash_hdr *ce_hdr;
	struct ath10k_dump_file_data *dump_data;
	struct ath10k_tlv_dump_data *dump_tlv;
	size_t hdr_len = sizeof(*dump_data);
	size_t len, sofar = 0;
	unsigned char *buf;

	len = hdr_len;
	len += sizeof(*dump_tlv) + sizeof(crash_data->registers);
	len += sizeof(*dump_tlv) + sizeof(*ce_hdr) +
		CE_COUNT * sizeof(ce_hdr->entries[0]);

	sofar += hdr_len;

	/* This is going to get big when we start dumping FW RAM and such,
	 * so go ahead and use vmalloc.
	 */
	buf = vzalloc(len);
	if (!buf)
		return NULL;

	spin_lock_bh(&ar->data_lock);

	if (!crash_data->crashed_since_read) {
		spin_unlock_bh(&ar->data_lock);
		vfree(buf);
		return NULL;
	}

	dump_data = (struct ath10k_dump_file_data *)(buf);
	strlcpy(dump_data->df_magic, "ATH10K-FW-DUMP",
		sizeof(dump_data->df_magic));
	dump_data->len = cpu_to_le32(len);

	dump_data->version = cpu_to_le32(ATH10K_FW_CRASH_DUMP_VERSION);

	guid_copy(&dump_data->guid, &crash_data->guid);
	dump_data->chip_id = cpu_to_le32(ar->chip_id);
	dump_data->bus_type = cpu_to_le32(0);
	dump_data->target_version = cpu_to_le32(ar->target_version);
	dump_data->fw_version_major = cpu_to_le32(ar->fw_version_major);
	dump_data->fw_version_minor = cpu_to_le32(ar->fw_version_minor);
	dump_data->fw_version_release = cpu_to_le32(ar->fw_version_release);
	dump_data->fw_version_build = cpu_to_le32(ar->fw_version_build);
	dump_data->phy_capability = cpu_to_le32(ar->phy_capability);
	dump_data->hw_min_tx_power = cpu_to_le32(ar->hw_min_tx_power);
	dump_data->hw_max_tx_power = cpu_to_le32(ar->hw_max_tx_power);
	dump_data->ht_cap_info = cpu_to_le32(ar->ht_cap_info);
	dump_data->vht_cap_info = cpu_to_le32(ar->vht_cap_info);
	dump_data->num_rf_chains = cpu_to_le32(ar->num_rf_chains);

	strlcpy(dump_data->fw_ver, ar->hw->wiphy->fw_version,
		sizeof(dump_data->fw_ver));

	dump_data->kernel_ver_code = 0;
	strlcpy(dump_data->kernel_ver, init_utsname()->release,
		sizeof(dump_data->kernel_ver));

	dump_data->tv_sec = cpu_to_le64(crash_data->timestamp.tv_sec);
	dump_data->tv_nsec = cpu_to_le64(crash_data->timestamp.tv_nsec);

	/* Gather crash-dump */
	dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
	dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_REGISTERS);
	dump_tlv->tlv_len = cpu_to_le32(sizeof(crash_data->registers));
	memcpy(dump_tlv->tlv_data, &crash_data->registers,
	       sizeof(crash_data->registers));
	sofar += sizeof(*dump_tlv) + sizeof(crash_data->registers);

	dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
	dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_CE_DATA);
	dump_tlv->tlv_len = cpu_to_le32(sizeof(*ce_hdr) +
					CE_COUNT * sizeof(ce_hdr->entries[0]));
	ce_hdr = (struct ath10k_ce_crash_hdr *)(dump_tlv->tlv_data);
	ce_hdr->ce_count = cpu_to_le32(CE_COUNT);
	memset(ce_hdr->reserved, 0, sizeof(ce_hdr->reserved));
	memcpy(ce_hdr->entries, crash_data->ce_crash_data,
	       CE_COUNT * sizeof(ce_hdr->entries[0]));
	sofar += sizeof(*dump_tlv) + sizeof(*ce_hdr) +
		 CE_COUNT * sizeof(ce_hdr->entries[0]);

	ar->debug.fw_crash_data->crashed_since_read = !mark_read;

	spin_unlock_bh(&ar->data_lock);

	return dump_data;
}

int ath10k_coredump_submit(struct ath10k *ar)
{
	struct ath10k_dump_file_data *dump;
	void *dump_ptr;
	u32 dump_len;

	/* To keep the dump file available also for debugfs don't mark the
	 * file read, only debugfs should do that.
	 */
	dump = ath10k_coredump_build(ar, false);
	if (!dump) {
		ath10k_warn(ar, "no crash dump data found for devcoredump");
		return -ENODATA;
	}

	/* Make a copy of the dump file for dev_coredumpv() as during the
	 * transition period we need to own the original file. Once
	 * fw_crash_dump debugfs file is removed no need to have a copy
	 * anymore.
	 */
	dump_len = le32_to_cpu(dump->len);
	dump_ptr = vzalloc(dump_len);

	if (!dump_ptr)
		return -ENOMEM;

	memcpy(dump_ptr, dump, dump_len);

	dev_coredumpv(ar->dev, dump_ptr, dump_len, GFP_KERNEL);

	return 0;
}

#endif /* CONFIG_DEV_COREDUMP */
