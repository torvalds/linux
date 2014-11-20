/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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

#include "common.h"

static ssize_t read_file_modal_eeprom(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath_hw *ah = file->private_data;
	u32 len = 0, size = 6000;
	char *buf;
	size_t retval;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = ah->eep_ops->dump_eeprom(ah, false, buf, len, size);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_modal_eeprom = {
	.read = read_file_modal_eeprom,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};


void ath9k_cmn_debug_modal_eeprom(struct dentry *debugfs_phy,
				  struct ath_hw *ah)
{
	debugfs_create_file("modal_eeprom", S_IRUSR, debugfs_phy, ah,
			    &fops_modal_eeprom);
}
EXPORT_SYMBOL(ath9k_cmn_debug_modal_eeprom);

static ssize_t read_file_base_eeprom(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath_hw *ah = file->private_data;
	u32 len = 0, size = 1500;
	ssize_t retval = 0;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = ah->eep_ops->dump_eeprom(ah, true, buf, len, size);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_base_eeprom = {
	.read = read_file_base_eeprom,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_cmn_debug_base_eeprom(struct dentry *debugfs_phy,
				 struct ath_hw *ah)
{
	debugfs_create_file("base_eeprom", S_IRUSR, debugfs_phy, ah,
			    &fops_base_eeprom);
}
EXPORT_SYMBOL(ath9k_cmn_debug_base_eeprom);

void ath9k_cmn_debug_stat_rx(struct ath_rx_stats *rxstats,
			     struct ath_rx_status *rs)
{
#define RX_PHY_ERR_INC(c) rxstats->phy_err_stats[c]++
#define RX_CMN_STAT_INC(c) (rxstats->c++)

	RX_CMN_STAT_INC(rx_pkts_all);
	rxstats->rx_bytes_all += rs->rs_datalen;

	if (rs->rs_status & ATH9K_RXERR_CRC)
		RX_CMN_STAT_INC(crc_err);
	if (rs->rs_status & ATH9K_RXERR_DECRYPT)
		RX_CMN_STAT_INC(decrypt_crc_err);
	if (rs->rs_status & ATH9K_RXERR_MIC)
		RX_CMN_STAT_INC(mic_err);
	if (rs->rs_status & ATH9K_RX_DELIM_CRC_PRE)
		RX_CMN_STAT_INC(pre_delim_crc_err);
	if (rs->rs_status & ATH9K_RX_DELIM_CRC_POST)
		RX_CMN_STAT_INC(post_delim_crc_err);
	if (rs->rs_status & ATH9K_RX_DECRYPT_BUSY)
		RX_CMN_STAT_INC(decrypt_busy_err);

	if (rs->rs_status & ATH9K_RXERR_PHY) {
		RX_CMN_STAT_INC(phy_err);
		if (rs->rs_phyerr < ATH9K_PHYERR_MAX)
			RX_PHY_ERR_INC(rs->rs_phyerr);
	}

#undef RX_CMN_STAT_INC
#undef RX_PHY_ERR_INC
}
EXPORT_SYMBOL(ath9k_cmn_debug_stat_rx);

static ssize_t read_file_recv(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
#define RXS_ERR(s, e)					\
	do {						\
		len += scnprintf(buf + len, size - len,	\
				 "%18s : %10u\n", s,	\
				 rxstats->e);		\
	} while (0)

	struct ath_rx_stats *rxstats = file->private_data;
	char *buf;
	unsigned int len = 0, size = 1600;
	ssize_t retval = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	RXS_ERR("PKTS-ALL", rx_pkts_all);
	RXS_ERR("BYTES-ALL", rx_bytes_all);
	RXS_ERR("BEACONS", rx_beacons);
	RXS_ERR("FRAGS", rx_frags);
	RXS_ERR("SPECTRAL", rx_spectral);

	RXS_ERR("CRC ERR", crc_err);
	RXS_ERR("DECRYPT CRC ERR", decrypt_crc_err);
	RXS_ERR("PHY ERR", phy_err);
	RXS_ERR("MIC ERR", mic_err);
	RXS_ERR("PRE-DELIM CRC ERR", pre_delim_crc_err);
	RXS_ERR("POST-DELIM CRC ERR", post_delim_crc_err);
	RXS_ERR("DECRYPT BUSY ERR", decrypt_busy_err);
	RXS_ERR("LENGTH-ERR", rx_len_err);
	RXS_ERR("OOM-ERR", rx_oom_err);
	RXS_ERR("RATE-ERR", rx_rate_err);
	RXS_ERR("TOO-MANY-FRAGS", rx_too_many_frags_err);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;

#undef RXS_ERR
}

static const struct file_operations fops_recv = {
	.read = read_file_recv,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_cmn_debug_recv(struct dentry *debugfs_phy,
			  struct ath_rx_stats *rxstats)
{
	debugfs_create_file("recv", S_IRUSR, debugfs_phy, rxstats,
			    &fops_recv);
}
EXPORT_SYMBOL(ath9k_cmn_debug_recv);

static ssize_t read_file_phy_err(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
#define PHY_ERR(s, p) \
	len += scnprintf(buf + len, size - len, "%22s : %10u\n", s, \
			 rxstats->phy_err_stats[p]);

	struct ath_rx_stats *rxstats = file->private_data;
	char *buf;
	unsigned int len = 0, size = 1600;
	ssize_t retval = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	PHY_ERR("UNDERRUN ERR", ATH9K_PHYERR_UNDERRUN);
	PHY_ERR("TIMING ERR", ATH9K_PHYERR_TIMING);
	PHY_ERR("PARITY ERR", ATH9K_PHYERR_PARITY);
	PHY_ERR("RATE ERR", ATH9K_PHYERR_RATE);
	PHY_ERR("LENGTH ERR", ATH9K_PHYERR_LENGTH);
	PHY_ERR("RADAR ERR", ATH9K_PHYERR_RADAR);
	PHY_ERR("SERVICE ERR", ATH9K_PHYERR_SERVICE);
	PHY_ERR("TOR ERR", ATH9K_PHYERR_TOR);
	PHY_ERR("OFDM-TIMING ERR", ATH9K_PHYERR_OFDM_TIMING);
	PHY_ERR("OFDM-SIGNAL-PARITY ERR", ATH9K_PHYERR_OFDM_SIGNAL_PARITY);
	PHY_ERR("OFDM-RATE ERR", ATH9K_PHYERR_OFDM_RATE_ILLEGAL);
	PHY_ERR("OFDM-LENGTH ERR", ATH9K_PHYERR_OFDM_LENGTH_ILLEGAL);
	PHY_ERR("OFDM-POWER-DROP ERR", ATH9K_PHYERR_OFDM_POWER_DROP);
	PHY_ERR("OFDM-SERVICE ERR", ATH9K_PHYERR_OFDM_SERVICE);
	PHY_ERR("OFDM-RESTART ERR", ATH9K_PHYERR_OFDM_RESTART);
	PHY_ERR("FALSE-RADAR-EXT ERR", ATH9K_PHYERR_FALSE_RADAR_EXT);
	PHY_ERR("CCK-TIMING ERR", ATH9K_PHYERR_CCK_TIMING);
	PHY_ERR("CCK-HEADER-CRC ERR", ATH9K_PHYERR_CCK_HEADER_CRC);
	PHY_ERR("CCK-RATE ERR", ATH9K_PHYERR_CCK_RATE_ILLEGAL);
	PHY_ERR("CCK-SERVICE ERR", ATH9K_PHYERR_CCK_SERVICE);
	PHY_ERR("CCK-RESTART ERR", ATH9K_PHYERR_CCK_RESTART);
	PHY_ERR("CCK-LENGTH ERR", ATH9K_PHYERR_CCK_LENGTH_ILLEGAL);
	PHY_ERR("CCK-POWER-DROP ERR", ATH9K_PHYERR_CCK_POWER_DROP);
	PHY_ERR("HT-CRC ERR", ATH9K_PHYERR_HT_CRC_ERROR);
	PHY_ERR("HT-LENGTH ERR", ATH9K_PHYERR_HT_LENGTH_ILLEGAL);
	PHY_ERR("HT-RATE ERR", ATH9K_PHYERR_HT_RATE_ILLEGAL);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;

#undef PHY_ERR
}

static const struct file_operations fops_phy_err = {
	.read = read_file_phy_err,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_cmn_debug_phy_err(struct dentry *debugfs_phy,
			     struct ath_rx_stats *rxstats)
{
	debugfs_create_file("phy_err", S_IRUSR, debugfs_phy, rxstats,
			    &fops_phy_err);
}
EXPORT_SYMBOL(ath9k_cmn_debug_phy_err);
