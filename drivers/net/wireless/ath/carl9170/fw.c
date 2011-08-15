/*
 * Atheros CARL9170 driver
 *
 * firmware parser
 *
 * Copyright 2009, 2010, Christian Lamparter <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/crc32.h>
#include "carl9170.h"
#include "fwcmd.h"
#include "version.h"

#define MAKE_STR(symbol) #symbol
#define TO_STR(symbol) MAKE_STR(symbol)
#define CARL9170FW_API_VER_STR TO_STR(CARL9170FW_API_MAX_VER)
MODULE_VERSION(CARL9170FW_API_VER_STR ":" CARL9170FW_VERSION_GIT);

static const u8 otus_magic[4] = { OTUS_MAGIC };

static const void *carl9170_fw_find_desc(struct ar9170 *ar, const u8 descid[4],
	const unsigned int len, const u8 compatible_revision)
{
	const struct carl9170fw_desc_head *iter;

	carl9170fw_for_each_hdr(iter, ar->fw.desc) {
		if (carl9170fw_desc_cmp(iter, descid, len,
					compatible_revision))
			return (void *)iter;
	}

	/* needed to find the LAST desc */
	if (carl9170fw_desc_cmp(iter, descid, len,
				compatible_revision))
		return (void *)iter;

	return NULL;
}

static int carl9170_fw_verify_descs(struct ar9170 *ar,
	const struct carl9170fw_desc_head *head, unsigned int max_len)
{
	const struct carl9170fw_desc_head *pos;
	unsigned long pos_addr, end_addr;
	unsigned int pos_length;

	if (max_len < sizeof(*pos))
		return -ENODATA;

	max_len = min_t(unsigned int, CARL9170FW_DESC_MAX_LENGTH, max_len);

	pos = head;
	pos_addr = (unsigned long) pos;
	end_addr = pos_addr + max_len;

	while (pos_addr < end_addr) {
		if (pos_addr + sizeof(*head) > end_addr)
			return -E2BIG;

		pos_length = le16_to_cpu(pos->length);

		if (pos_length < sizeof(*head))
			return -EBADMSG;

		if (pos_length > max_len)
			return -EOVERFLOW;

		if (pos_addr + pos_length > end_addr)
			return -EMSGSIZE;

		if (carl9170fw_desc_cmp(pos, LAST_MAGIC,
					CARL9170FW_LAST_DESC_SIZE,
					CARL9170FW_LAST_DESC_CUR_VER))
			return 0;

		pos_addr += pos_length;
		pos = (void *)pos_addr;
		max_len -= pos_length;
	}
	return -EINVAL;
}

static void carl9170_fw_info(struct ar9170 *ar)
{
	const struct carl9170fw_motd_desc *motd_desc;
	unsigned int str_ver_len;
	u32 fw_date;

	dev_info(&ar->udev->dev, "driver   API: %s 2%03d-%02d-%02d [%d-%d]\n",
		CARL9170FW_VERSION_GIT, CARL9170FW_VERSION_YEAR,
		CARL9170FW_VERSION_MONTH, CARL9170FW_VERSION_DAY,
		CARL9170FW_API_MIN_VER, CARL9170FW_API_MAX_VER);

	motd_desc = carl9170_fw_find_desc(ar, MOTD_MAGIC,
		sizeof(*motd_desc), CARL9170FW_MOTD_DESC_CUR_VER);

	if (motd_desc) {
		str_ver_len = strnlen(motd_desc->release,
			CARL9170FW_MOTD_RELEASE_LEN);

		fw_date = le32_to_cpu(motd_desc->fw_year_month_day);

		dev_info(&ar->udev->dev, "firmware API: %.*s 2%03d-%02d-%02d\n",
			 str_ver_len, motd_desc->release,
			 CARL9170FW_GET_YEAR(fw_date),
			 CARL9170FW_GET_MONTH(fw_date),
			 CARL9170FW_GET_DAY(fw_date));

		strlcpy(ar->hw->wiphy->fw_version, motd_desc->release,
			sizeof(ar->hw->wiphy->fw_version));
	}
}

static bool valid_dma_addr(const u32 address)
{
	if (address >= AR9170_SRAM_OFFSET &&
	    address < (AR9170_SRAM_OFFSET + AR9170_SRAM_SIZE))
		return true;

	return false;
}

static bool valid_cpu_addr(const u32 address)
{
	if (valid_dma_addr(address) || (address >= AR9170_PRAM_OFFSET &&
	    address < (AR9170_PRAM_OFFSET + AR9170_PRAM_SIZE)))
		return true;

	return false;
}

static int carl9170_fw(struct ar9170 *ar, const __u8 *data, size_t len)
{
	const struct carl9170fw_otus_desc *otus_desc;
	const struct carl9170fw_chk_desc *chk_desc;
	const struct carl9170fw_last_desc *last_desc;
	const struct carl9170fw_txsq_desc *txsq_desc;
	u16 if_comb_types;

	last_desc = carl9170_fw_find_desc(ar, LAST_MAGIC,
		sizeof(*last_desc), CARL9170FW_LAST_DESC_CUR_VER);
	if (!last_desc)
		return -EINVAL;

	otus_desc = carl9170_fw_find_desc(ar, OTUS_MAGIC,
		sizeof(*otus_desc), CARL9170FW_OTUS_DESC_CUR_VER);
	if (!otus_desc) {
		dev_err(&ar->udev->dev, "failed to find compatible firmware "
			"descriptor.\n");
		return -ENODATA;
	}

	chk_desc = carl9170_fw_find_desc(ar, CHK_MAGIC,
		sizeof(*chk_desc), CARL9170FW_CHK_DESC_CUR_VER);

	if (chk_desc) {
		unsigned long fin, diff;
		unsigned int dsc_len;
		u32 crc32;

		dsc_len = min_t(unsigned int, len,
			(unsigned long)chk_desc - (unsigned long)otus_desc);

		fin = (unsigned long) last_desc + sizeof(*last_desc);
		diff = fin - (unsigned long) otus_desc;

		if (diff < len)
			len -= diff;

		if (len < 256)
			return -EIO;

		crc32 = crc32_le(~0, data, len);
		if (cpu_to_le32(crc32) != chk_desc->fw_crc32) {
			dev_err(&ar->udev->dev, "fw checksum test failed.\n");
			return -ENOEXEC;
		}

		crc32 = crc32_le(crc32, (void *)otus_desc, dsc_len);
		if (cpu_to_le32(crc32) != chk_desc->hdr_crc32) {
			dev_err(&ar->udev->dev, "descriptor check failed.\n");
			return -EINVAL;
		}
	} else {
		dev_warn(&ar->udev->dev, "Unprotected firmware image.\n");
	}

#define SUPP(feat)						\
	(carl9170fw_supports(otus_desc->feature_set, feat))

	if (!SUPP(CARL9170FW_DUMMY_FEATURE)) {
		dev_err(&ar->udev->dev, "invalid firmware descriptor "
			"format detected.\n");
		return -EINVAL;
	}

	ar->fw.api_version = otus_desc->api_ver;

	if (ar->fw.api_version < CARL9170FW_API_MIN_VER ||
	    ar->fw.api_version > CARL9170FW_API_MAX_VER) {
		dev_err(&ar->udev->dev, "unsupported firmware api version.\n");
		return -EINVAL;
	}

	if (!SUPP(CARL9170FW_COMMAND_PHY) || SUPP(CARL9170FW_UNUSABLE) ||
	    !SUPP(CARL9170FW_HANDLE_BACK_REQ)) {
		dev_err(&ar->udev->dev, "firmware does support "
			"mandatory features.\n");
		return -ECANCELED;
	}

	if (ilog2(le32_to_cpu(otus_desc->feature_set)) >=
		__CARL9170FW_FEATURE_NUM) {
		dev_warn(&ar->udev->dev, "driver does not support all "
			 "firmware features.\n");
	}

	if (!SUPP(CARL9170FW_COMMAND_CAM)) {
		dev_info(&ar->udev->dev, "crypto offloading is disabled "
			 "by firmware.\n");
		ar->disable_offload = true;
	}

	if (SUPP(CARL9170FW_PSM) && SUPP(CARL9170FW_FIXED_5GHZ_PSM))
		ar->hw->flags |= IEEE80211_HW_SUPPORTS_PS;

	if (!SUPP(CARL9170FW_USB_INIT_FIRMWARE)) {
		dev_err(&ar->udev->dev, "firmware does not provide "
			"mandatory interfaces.\n");
		return -EINVAL;
	}

	if (SUPP(CARL9170FW_MINIBOOT))
		ar->fw.offset = le16_to_cpu(otus_desc->miniboot_size);
	else
		ar->fw.offset = 0;

	if (SUPP(CARL9170FW_USB_DOWN_STREAM)) {
		ar->hw->extra_tx_headroom += sizeof(struct ar9170_stream);
		ar->fw.tx_stream = true;
	}

	if (SUPP(CARL9170FW_USB_UP_STREAM))
		ar->fw.rx_stream = true;

	if (SUPP(CARL9170FW_RX_FILTER)) {
		ar->fw.rx_filter = true;
		ar->rx_filter_caps = FIF_FCSFAIL | FIF_PLCPFAIL |
			FIF_CONTROL | FIF_PSPOLL | FIF_OTHER_BSS |
			FIF_PROMISC_IN_BSS;
	}

	if (SUPP(CARL9170FW_HW_COUNTERS))
		ar->fw.hw_counters = true;

	if (SUPP(CARL9170FW_WOL))
		device_set_wakeup_enable(&ar->udev->dev, true);

	if_comb_types = BIT(NL80211_IFTYPE_STATION) |
			BIT(NL80211_IFTYPE_P2P_CLIENT);

	ar->fw.vif_num = otus_desc->vif_num;
	ar->fw.cmd_bufs = otus_desc->cmd_bufs;
	ar->fw.address = le32_to_cpu(otus_desc->fw_address);
	ar->fw.rx_size = le16_to_cpu(otus_desc->rx_max_frame_len);
	ar->fw.mem_blocks = min_t(unsigned int, otus_desc->tx_descs, 0xfe);
	atomic_set(&ar->mem_free_blocks, ar->fw.mem_blocks);
	ar->fw.mem_block_size = le16_to_cpu(otus_desc->tx_frag_len);

	if (ar->fw.vif_num >= AR9170_MAX_VIRTUAL_MAC || !ar->fw.vif_num ||
	    ar->fw.mem_blocks < 16 || !ar->fw.cmd_bufs ||
	    ar->fw.mem_block_size < 64 || ar->fw.mem_block_size > 512 ||
	    ar->fw.rx_size > 32768 || ar->fw.rx_size < 4096 ||
	    !valid_cpu_addr(ar->fw.address)) {
		dev_err(&ar->udev->dev, "firmware shows obvious signs of "
			"malicious tampering.\n");
		return -EINVAL;
	}

	ar->fw.beacon_addr = le32_to_cpu(otus_desc->bcn_addr);
	ar->fw.beacon_max_len = le16_to_cpu(otus_desc->bcn_len);

	if (valid_dma_addr(ar->fw.beacon_addr) && ar->fw.beacon_max_len >=
	    AR9170_MAC_BCN_LENGTH_MAX) {
		ar->hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_ADHOC);

		if (SUPP(CARL9170FW_WLANTX_CAB)) {
			if_comb_types |=
				BIT(NL80211_IFTYPE_AP) |
				BIT(NL80211_IFTYPE_P2P_GO);
		}
	}

	ar->if_comb_limits[0].max = ar->fw.vif_num;
	ar->if_comb_limits[0].types = if_comb_types;

	ar->if_combs[0].num_different_channels = 1;
	ar->if_combs[0].max_interfaces = ar->fw.vif_num;
	ar->if_combs[0].limits = ar->if_comb_limits;
	ar->if_combs[0].n_limits = ARRAY_SIZE(ar->if_comb_limits);

	ar->hw->wiphy->iface_combinations = ar->if_combs;
	ar->hw->wiphy->n_iface_combinations = ARRAY_SIZE(ar->if_combs);

	ar->hw->wiphy->interface_modes |= if_comb_types;

	txsq_desc = carl9170_fw_find_desc(ar, TXSQ_MAGIC,
		sizeof(*txsq_desc), CARL9170FW_TXSQ_DESC_CUR_VER);

	if (txsq_desc) {
		ar->fw.tx_seq_table = le32_to_cpu(txsq_desc->seq_table_addr);
		if (!valid_cpu_addr(ar->fw.tx_seq_table))
			return -EINVAL;
	} else {
		ar->fw.tx_seq_table = 0;
	}

#undef SUPPORTED
	return 0;
}

static struct carl9170fw_desc_head *
carl9170_find_fw_desc(struct ar9170 *ar, const __u8 *fw_data, const size_t len)

{
	int scan = 0, found = 0;

	if (!carl9170fw_size_check(len)) {
		dev_err(&ar->udev->dev, "firmware size is out of bound.\n");
		return NULL;
	}

	while (scan < len - sizeof(struct carl9170fw_desc_head)) {
		if (fw_data[scan++] == otus_magic[found])
			found++;
		else
			found = 0;

		if (scan >= len)
			break;

		if (found == sizeof(otus_magic))
			break;
	}

	if (found != sizeof(otus_magic))
		return NULL;

	return (void *)&fw_data[scan - found];
}

int carl9170_fw_fix_eeprom(struct ar9170 *ar)
{
	const struct carl9170fw_fix_desc *fix_desc = NULL;
	unsigned int i, n, off;
	u32 *data = (void *)&ar->eeprom;

	fix_desc = carl9170_fw_find_desc(ar, FIX_MAGIC,
		sizeof(*fix_desc), CARL9170FW_FIX_DESC_CUR_VER);

	if (!fix_desc)
		return 0;

	n = (le16_to_cpu(fix_desc->head.length) - sizeof(*fix_desc)) /
	    sizeof(struct carl9170fw_fix_entry);

	for (i = 0; i < n; i++) {
		off = le32_to_cpu(fix_desc->data[i].address) -
		      AR9170_EEPROM_START;

		if (off >= sizeof(struct ar9170_eeprom) || (off & 3)) {
			dev_err(&ar->udev->dev, "Skip invalid entry %d\n", i);
			continue;
		}

		data[off / sizeof(*data)] &=
			le32_to_cpu(fix_desc->data[i].mask);
		data[off / sizeof(*data)] |=
			le32_to_cpu(fix_desc->data[i].value);
	}

	return 0;
}

int carl9170_parse_firmware(struct ar9170 *ar)
{
	const struct carl9170fw_desc_head *fw_desc = NULL;
	const struct firmware *fw = ar->fw.fw;
	unsigned long header_offset = 0;
	int err;

	if (WARN_ON(!fw))
		return -EINVAL;

	fw_desc = carl9170_find_fw_desc(ar, fw->data, fw->size);

	if (!fw_desc) {
		dev_err(&ar->udev->dev, "unsupported firmware.\n");
		return -ENODATA;
	}

	header_offset = (unsigned long)fw_desc - (unsigned long)fw->data;

	err = carl9170_fw_verify_descs(ar, fw_desc, fw->size - header_offset);
	if (err) {
		dev_err(&ar->udev->dev, "damaged firmware (%d).\n", err);
		return err;
	}

	ar->fw.desc = fw_desc;

	carl9170_fw_info(ar);

	err = carl9170_fw(ar, fw->data, fw->size);
	if (err) {
		dev_err(&ar->udev->dev, "failed to parse firmware (%d).\n",
			err);
		return err;
	}

	return 0;
}
