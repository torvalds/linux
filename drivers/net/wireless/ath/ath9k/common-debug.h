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



/**
 * struct ath_rx_stats - RX Statistics
 * @rx_pkts_all:  No. of total frames received, including ones that
	may have had errors.
 * @rx_bytes_all:  No. of total bytes received, including ones that
	may have had errors.
 * @crc_err: No. of frames with incorrect CRC value
 * @decrypt_crc_err: No. of frames whose CRC check failed after
	decryption process completed
 * @phy_err: No. of frames whose reception failed because the PHY
	encountered an error
 * @mic_err: No. of frames with incorrect TKIP MIC verification failure
 * @pre_delim_crc_err: Pre-Frame delimiter CRC error detections
 * @post_delim_crc_err: Post-Frame delimiter CRC error detections
 * @decrypt_busy_err: Decryption interruptions counter
 * @phy_err_stats: Individual PHY error statistics
 * @rx_len_err:  No. of frames discarded due to bad length.
 * @rx_oom_err:  No. of frames dropped due to OOM issues.
 * @rx_rate_err:  No. of frames dropped due to rate errors.
 * @rx_too_many_frags_err:  Frames dropped due to too-many-frags received.
 * @rx_beacons:  No. of beacons received.
 * @rx_frags:  No. of rx-fragements received.
 * @rx_spectral: No of spectral packets received.
 * @rx_spectral_sample_good: No. of good spectral samples
 * @rx_spectral_sample_err: No. of good spectral samples
 */
struct ath_rx_stats {
	u32 rx_pkts_all;
	u32 rx_bytes_all;
	u32 crc_err;
	u32 decrypt_crc_err;
	u32 phy_err;
	u32 mic_err;
	u32 pre_delim_crc_err;
	u32 post_delim_crc_err;
	u32 decrypt_busy_err;
	u32 phy_err_stats[ATH9K_PHYERR_MAX];
	u32 rx_len_err;
	u32 rx_oom_err;
	u32 rx_rate_err;
	u32 rx_too_many_frags_err;
	u32 rx_beacons;
	u32 rx_frags;
	u32 rx_spectral;
	u32 rx_spectral_sample_good;
	u32 rx_spectral_sample_err;
};

#ifdef CONFIG_ATH9K_COMMON_DEBUG
void ath9k_cmn_debug_modal_eeprom(struct dentry *debugfs_phy,
				  struct ath_hw *ah);
void ath9k_cmn_debug_base_eeprom(struct dentry *debugfs_phy,
				 struct ath_hw *ah);
void ath9k_cmn_debug_stat_rx(struct ath_rx_stats *rxstats,
			     struct ath_rx_status *rs);
void ath9k_cmn_debug_recv(struct dentry *debugfs_phy,
			  struct ath_rx_stats *rxstats);
void ath9k_cmn_debug_phy_err(struct dentry *debugfs_phy,
			     struct ath_rx_stats *rxstats);
#else
static inline void ath9k_cmn_debug_modal_eeprom(struct dentry *debugfs_phy,
						struct ath_hw *ah)
{
}

static inline void ath9k_cmn_debug_base_eeprom(struct dentry *debugfs_phy,
					       struct ath_hw *ah)
{
}

static inline void ath9k_cmn_debug_stat_rx(struct ath_rx_stats *rxstats,
					   struct ath_rx_status *rs)
{
}

static inline void ath9k_cmn_debug_recv(struct dentry *debugfs_phy,
					struct ath_rx_stats *rxstats)
{
}

static inline void ath9k_cmn_debug_phy_err(struct dentry *debugfs_phy,
					   struct ath_rx_stats *rxstats)
{
}
#endif /* CONFIG_ATH9K_COMMON_DEBUG */
