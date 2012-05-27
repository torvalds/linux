/*
 *  FM Driver for Connectivity chip of Texas Instruments.
 *  This sub-module of FM driver implements FM RX functionality.
 *
 *  Copyright (C) 2011 Texas Instruments
 *  Author: Raja Mani <raja_mani@ti.com>
 *  Author: Manjunatha Halli <manjunatha_halli@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "fmdrv.h"
#include "fmdrv_common.h"
#include "fmdrv_rx.h"

void fm_rx_reset_rds_cache(struct fmdev *fmdev)
{
	fmdev->rx.rds.flag = FM_RDS_DISABLE;
	fmdev->rx.rds.last_blk_idx = 0;
	fmdev->rx.rds.wr_idx = 0;
	fmdev->rx.rds.rd_idx = 0;

	if (fmdev->rx.af_mode == FM_RX_RDS_AF_SWITCH_MODE_ON)
		fmdev->irq_info.mask |= FM_LEV_EVENT;
}

void fm_rx_reset_station_info(struct fmdev *fmdev)
{
	fmdev->rx.stat_info.picode = FM_NO_PI_CODE;
	fmdev->rx.stat_info.afcache_size = 0;
	fmdev->rx.stat_info.af_list_max = 0;
}

int fm_rx_set_freq(struct fmdev *fmdev, u32 freq)
{
	unsigned long timeleft;
	u16 payload, curr_frq, intr_flag;
	u32 curr_frq_in_khz;
	u32 resp_len;
	int ret;

	if (freq < fmdev->rx.region.bot_freq || freq > fmdev->rx.region.top_freq) {
		fmerr("Invalid frequency %d\n", freq);
		return -EINVAL;
	}

	/* Set audio enable */
	payload = FM_RX_AUDIO_ENABLE_I2S_AND_ANALOG;

	ret = fmc_send_cmd(fmdev, AUDIO_ENABLE_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Set hilo to automatic selection */
	payload = FM_RX_IFFREQ_HILO_AUTOMATIC;
	ret = fmc_send_cmd(fmdev, HILO_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Calculate frequency index and set*/
	payload = (freq - fmdev->rx.region.bot_freq) / FM_FREQ_MUL;

	ret = fmc_send_cmd(fmdev, FREQ_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Read flags - just to clear any pending interrupts if we had */
	ret = fmc_send_cmd(fmdev, FLAG_GET, REG_RD, NULL, 2, NULL, NULL);
	if (ret < 0)
		return ret;

	/* Enable FR, BL interrupts */
	intr_flag = fmdev->irq_info.mask;
	fmdev->irq_info.mask = (FM_FR_EVENT | FM_BL_EVENT);
	payload = fmdev->irq_info.mask;
	ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Start tune */
	payload = FM_TUNER_PRESET_MODE;
	ret = fmc_send_cmd(fmdev, TUNER_MODE_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		goto exit;

	/* Wait for tune ended interrupt */
	init_completion(&fmdev->maintask_comp);
	timeleft = wait_for_completion_timeout(&fmdev->maintask_comp,
			FM_DRV_TX_TIMEOUT);
	if (!timeleft) {
		fmerr("Timeout(%d sec),didn't get tune ended int\n",
			   jiffies_to_msecs(FM_DRV_TX_TIMEOUT) / 1000);
		ret = -ETIMEDOUT;
		goto exit;
	}

	/* Read freq back to confirm */
	ret = fmc_send_cmd(fmdev, FREQ_SET, REG_RD, NULL, 2, &curr_frq, &resp_len);
	if (ret < 0)
		goto exit;

	curr_frq = be16_to_cpu(curr_frq);
	curr_frq_in_khz = (fmdev->rx.region.bot_freq + ((u32)curr_frq * FM_FREQ_MUL));

	if (curr_frq_in_khz != freq) {
		pr_info("Frequency is set to (%d) but "
			   "requested freq is (%d)\n", curr_frq_in_khz, freq);
	}

	/* Update local cache  */
	fmdev->rx.freq = curr_frq_in_khz;
exit:
	/* Re-enable default FM interrupts */
	fmdev->irq_info.mask = intr_flag;
	payload = fmdev->irq_info.mask;
	ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Reset RDS cache and current station pointers */
	fm_rx_reset_rds_cache(fmdev);
	fm_rx_reset_station_info(fmdev);

	return ret;
}

static int fm_rx_set_channel_spacing(struct fmdev *fmdev, u32 spacing)
{
	u16 payload;
	int ret;

	if (spacing > 0 && spacing <= 50000)
		spacing = FM_CHANNEL_SPACING_50KHZ;
	else if (spacing > 50000 && spacing <= 100000)
		spacing = FM_CHANNEL_SPACING_100KHZ;
	else
		spacing = FM_CHANNEL_SPACING_200KHZ;

	/* set channel spacing */
	payload = spacing;
	ret = fmc_send_cmd(fmdev, CHANL_BW_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmdev->rx.region.chanl_space = spacing * FM_FREQ_MUL;

	return ret;
}

int fm_rx_seek(struct fmdev *fmdev, u32 seek_upward,
		u32 wrap_around, u32 spacing)
{
	u32 resp_len;
	u16 curr_frq, next_frq, last_frq;
	u16 payload, int_reason, intr_flag;
	u16 offset, space_idx;
	unsigned long timeleft;
	int ret;

	/* Set channel spacing */
	ret = fm_rx_set_channel_spacing(fmdev, spacing);
	if (ret < 0) {
		fmerr("Failed to set channel spacing\n");
		return ret;
	}

	/* Read the current frequency from chip */
	ret = fmc_send_cmd(fmdev, FREQ_SET, REG_RD, NULL,
			sizeof(curr_frq), &curr_frq, &resp_len);
	if (ret < 0)
		return ret;

	curr_frq = be16_to_cpu(curr_frq);
	last_frq = (fmdev->rx.region.top_freq - fmdev->rx.region.bot_freq) / FM_FREQ_MUL;

	/* Check the offset in order to be aligned to the channel spacing*/
	space_idx = fmdev->rx.region.chanl_space / FM_FREQ_MUL;
	offset = curr_frq % space_idx;

	next_frq = seek_upward ? curr_frq + space_idx /* Seek Up */ :
				curr_frq - space_idx /* Seek Down */ ;

	/*
	 * Add or subtract offset in order to stay aligned to the channel
	 * spacing.
	 */
	if ((short)next_frq < 0)
		next_frq = last_frq - offset;
	else if (next_frq > last_frq)
		next_frq = 0 + offset;

again:
	/* Set calculated next frequency to perform seek */
	payload = next_frq;
	ret = fmc_send_cmd(fmdev, FREQ_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Set search direction (0:Seek Down, 1:Seek Up) */
	payload = (seek_upward ? FM_SEARCH_DIRECTION_UP : FM_SEARCH_DIRECTION_DOWN);
	ret = fmc_send_cmd(fmdev, SEARCH_DIR_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Read flags - just to clear any pending interrupts if we had */
	ret = fmc_send_cmd(fmdev, FLAG_GET, REG_RD, NULL, 2, NULL, NULL);
	if (ret < 0)
		return ret;

	/* Enable FR, BL interrupts */
	intr_flag = fmdev->irq_info.mask;
	fmdev->irq_info.mask = (FM_FR_EVENT | FM_BL_EVENT);
	payload = fmdev->irq_info.mask;
	ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Start seek */
	payload = FM_TUNER_AUTONOMOUS_SEARCH_MODE;
	ret = fmc_send_cmd(fmdev, TUNER_MODE_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Wait for tune ended/band limit reached interrupt */
	init_completion(&fmdev->maintask_comp);
	timeleft = wait_for_completion_timeout(&fmdev->maintask_comp,
			FM_DRV_RX_SEEK_TIMEOUT);
	if (!timeleft) {
		fmerr("Timeout(%d sec),didn't get tune ended int\n",
			   jiffies_to_msecs(FM_DRV_RX_SEEK_TIMEOUT) / 1000);
		return -ENODATA;
	}

	int_reason = fmdev->irq_info.flag & (FM_TUNE_COMPLETE | FM_BAND_LIMIT);

	/* Re-enable default FM interrupts */
	fmdev->irq_info.mask = intr_flag;
	payload = fmdev->irq_info.mask;
	ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	if (int_reason & FM_BL_EVENT) {
		if (wrap_around == 0) {
			fmdev->rx.freq = seek_upward ?
				fmdev->rx.region.top_freq :
				fmdev->rx.region.bot_freq;
		} else {
			fmdev->rx.freq = seek_upward ?
				fmdev->rx.region.bot_freq :
				fmdev->rx.region.top_freq;
			/* Calculate frequency index to write */
			next_frq = (fmdev->rx.freq -
					fmdev->rx.region.bot_freq) / FM_FREQ_MUL;
			goto again;
		}
	} else {
		/* Read freq to know where operation tune operation stopped */
		ret = fmc_send_cmd(fmdev, FREQ_SET, REG_RD, NULL, 2,
				&curr_frq, &resp_len);
		if (ret < 0)
			return ret;

		curr_frq = be16_to_cpu(curr_frq);
		fmdev->rx.freq = (fmdev->rx.region.bot_freq +
				((u32)curr_frq * FM_FREQ_MUL));

	}
	/* Reset RDS cache and current station pointers */
	fm_rx_reset_rds_cache(fmdev);
	fm_rx_reset_station_info(fmdev);

	return ret;
}

int fm_rx_set_volume(struct fmdev *fmdev, u16 vol_to_set)
{
	u16 payload;
	int ret;

	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (vol_to_set < FM_RX_VOLUME_MIN || vol_to_set > FM_RX_VOLUME_MAX) {
		fmerr("Volume is not within(%d-%d) range\n",
			   FM_RX_VOLUME_MIN, FM_RX_VOLUME_MAX);
		return -EINVAL;
	}
	vol_to_set *= FM_RX_VOLUME_GAIN_STEP;

	payload = vol_to_set;
	ret = fmc_send_cmd(fmdev, VOLUME_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmdev->rx.volume = vol_to_set;
	return ret;
}

/* Get volume */
int fm_rx_get_volume(struct fmdev *fmdev, u16 *curr_vol)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (curr_vol == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*curr_vol = fmdev->rx.volume / FM_RX_VOLUME_GAIN_STEP;

	return 0;
}

/* To get current band's bottom and top frequency */
int fm_rx_get_band_freq_range(struct fmdev *fmdev, u32 *bot_freq, u32 *top_freq)
{
	if (bot_freq != NULL)
		*bot_freq = fmdev->rx.region.bot_freq;

	if (top_freq != NULL)
		*top_freq = fmdev->rx.region.top_freq;

	return 0;
}

/* Returns current band index (0-Europe/US; 1-Japan) */
void fm_rx_get_region(struct fmdev *fmdev, u8 *region)
{
	*region = fmdev->rx.region.fm_band;
}

/* Sets band (0-Europe/US; 1-Japan) */
int fm_rx_set_region(struct fmdev *fmdev, u8 region_to_set)
{
	u16 payload;
	u32 new_frq = 0;
	int ret;

	if (region_to_set != FM_BAND_EUROPE_US &&
	    region_to_set != FM_BAND_JAPAN) {
		fmerr("Invalid band\n");
		return -EINVAL;
	}

	if (fmdev->rx.region.fm_band == region_to_set) {
		fmerr("Requested band is already configured\n");
		return 0;
	}

	/* Send cmd to set the band  */
	payload = (u16)region_to_set;
	ret = fmc_send_cmd(fmdev, BAND_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmc_update_region_info(fmdev, region_to_set);

	/* Check whether current RX frequency is within band boundary */
	if (fmdev->rx.freq < fmdev->rx.region.bot_freq)
		new_frq = fmdev->rx.region.bot_freq;
	else if (fmdev->rx.freq > fmdev->rx.region.top_freq)
		new_frq = fmdev->rx.region.top_freq;

	if (new_frq) {
		fmdbg("Current freq is not within band limit boundary,"
				"switching to %d KHz\n", new_frq);
		 /* Current RX frequency is not in range. So, update it */
		ret = fm_rx_set_freq(fmdev, new_frq);
	}

	return ret;
}

/* Reads current mute mode (Mute Off/On/Attenuate)*/
int fm_rx_get_mute_mode(struct fmdev *fmdev, u8 *curr_mute_mode)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (curr_mute_mode == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*curr_mute_mode = fmdev->rx.mute_mode;

	return 0;
}

static int fm_config_rx_mute_reg(struct fmdev *fmdev)
{
	u16 payload, muteval;
	int ret;

	muteval = 0;
	switch (fmdev->rx.mute_mode) {
	case FM_MUTE_ON:
		muteval = FM_RX_AC_MUTE_MODE;
		break;

	case FM_MUTE_OFF:
		muteval = FM_RX_UNMUTE_MODE;
		break;

	case FM_MUTE_ATTENUATE:
		muteval = FM_RX_SOFT_MUTE_FORCE_MODE;
		break;
	}
	if (fmdev->rx.rf_depend_mute == FM_RX_RF_DEPENDENT_MUTE_ON)
		muteval |= FM_RX_RF_DEP_MODE;
	else
		muteval &= ~FM_RX_RF_DEP_MODE;

	payload = muteval;
	ret = fmc_send_cmd(fmdev, MUTE_STATUS_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

/* Configures mute mode (Mute Off/On/Attenuate) */
int fm_rx_set_mute_mode(struct fmdev *fmdev, u8 mute_mode_toset)
{
	u8 org_state;
	int ret;

	if (fmdev->rx.mute_mode == mute_mode_toset)
		return 0;

	org_state = fmdev->rx.mute_mode;
	fmdev->rx.mute_mode = mute_mode_toset;

	ret = fm_config_rx_mute_reg(fmdev);
	if (ret < 0) {
		fmdev->rx.mute_mode = org_state;
		return ret;
	}

	return 0;
}

/* Gets RF dependent soft mute mode enable/disable status */
int fm_rx_get_rfdepend_softmute(struct fmdev *fmdev, u8 *curr_mute_mode)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (curr_mute_mode == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*curr_mute_mode = fmdev->rx.rf_depend_mute;

	return 0;
}

/* Sets RF dependent soft mute mode */
int fm_rx_set_rfdepend_softmute(struct fmdev *fmdev, u8 rfdepend_mute)
{
	u8 org_state;
	int ret;

	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (rfdepend_mute != FM_RX_RF_DEPENDENT_MUTE_ON &&
	    rfdepend_mute != FM_RX_RF_DEPENDENT_MUTE_OFF) {
		fmerr("Invalid RF dependent soft mute\n");
		return -EINVAL;
	}
	if (fmdev->rx.rf_depend_mute == rfdepend_mute)
		return 0;

	org_state = fmdev->rx.rf_depend_mute;
	fmdev->rx.rf_depend_mute = rfdepend_mute;

	ret = fm_config_rx_mute_reg(fmdev);
	if (ret < 0) {
		fmdev->rx.rf_depend_mute = org_state;
		return ret;
	}

	return 0;
}

/* Returns the signal strength level of current channel */
int fm_rx_get_rssi_level(struct fmdev *fmdev, u16 *rssilvl)
{
	u16 curr_rssi_lel;
	u32 resp_len;
	int ret;

	if (rssilvl == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}
	/* Read current RSSI level */
	ret = fmc_send_cmd(fmdev, RSSI_LVL_GET, REG_RD, NULL, 2,
			&curr_rssi_lel, &resp_len);
	if (ret < 0)
		return ret;

	*rssilvl = be16_to_cpu(curr_rssi_lel);

	return 0;
}

/*
 * Sets the signal strength level that once reached
 * will stop the auto search process
 */
int fm_rx_set_rssi_threshold(struct fmdev *fmdev, short rssi_lvl_toset)
{
	u16 payload;
	int ret;

	if (rssi_lvl_toset < FM_RX_RSSI_THRESHOLD_MIN ||
			rssi_lvl_toset > FM_RX_RSSI_THRESHOLD_MAX) {
		fmerr("Invalid RSSI threshold level\n");
		return -EINVAL;
	}
	payload = (u16)rssi_lvl_toset;
	ret = fmc_send_cmd(fmdev, SEARCH_LVL_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmdev->rx.rssi_threshold = rssi_lvl_toset;

	return 0;
}

/* Returns current RX RSSI threshold value */
int fm_rx_get_rssi_threshold(struct fmdev *fmdev, short *curr_rssi_lvl)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (curr_rssi_lvl == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*curr_rssi_lvl = fmdev->rx.rssi_threshold;

	return 0;
}

/* Sets RX stereo/mono modes */
int fm_rx_set_stereo_mono(struct fmdev *fmdev, u16 mode)
{
	u16 payload;
	int ret;

	if (mode != FM_STEREO_MODE && mode != FM_MONO_MODE) {
		fmerr("Invalid mode\n");
		return -EINVAL;
	}

	/* Set stereo/mono mode */
	payload = (u16)mode;
	ret = fmc_send_cmd(fmdev, MOST_MODE_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Set stereo blending mode */
	payload = FM_STEREO_SOFT_BLEND;
	ret = fmc_send_cmd(fmdev, MOST_BLEND_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

/* Gets current RX stereo/mono mode */
int fm_rx_get_stereo_mono(struct fmdev *fmdev, u16 *mode)
{
	u16 curr_mode;
	u32 resp_len;
	int ret;

	if (mode == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	ret = fmc_send_cmd(fmdev, MOST_MODE_SET, REG_RD, NULL, 2,
			&curr_mode, &resp_len);
	if (ret < 0)
		return ret;

	*mode = be16_to_cpu(curr_mode);

	return 0;
}

/* Choose RX de-emphasis filter mode (50us/75us) */
int fm_rx_set_deemphasis_mode(struct fmdev *fmdev, u16 mode)
{
	u16 payload;
	int ret;

	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (mode != FM_RX_EMPHASIS_FILTER_50_USEC &&
			mode != FM_RX_EMPHASIS_FILTER_75_USEC) {
		fmerr("Invalid rx de-emphasis mode (%d)\n", mode);
		return -EINVAL;
	}

	payload = mode;
	ret = fmc_send_cmd(fmdev, DEMPH_MODE_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmdev->rx.deemphasis_mode = mode;

	return 0;
}

/* Gets current RX de-emphasis filter mode */
int fm_rx_get_deemph_mode(struct fmdev *fmdev, u16 *curr_deemphasis_mode)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (curr_deemphasis_mode == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*curr_deemphasis_mode = fmdev->rx.deemphasis_mode;

	return 0;
}

/* Enable/Disable RX RDS */
int fm_rx_set_rds_mode(struct fmdev *fmdev, u8 rds_en_dis)
{
	u16 payload;
	int ret;

	if (rds_en_dis != FM_RDS_ENABLE && rds_en_dis != FM_RDS_DISABLE) {
		fmerr("Invalid rds option\n");
		return -EINVAL;
	}

	if (rds_en_dis == FM_RDS_ENABLE
	    && fmdev->rx.rds.flag == FM_RDS_DISABLE) {
		/* Turn on RX RDS and RDS circuit */
		payload = FM_RX_PWR_SET_FM_AND_RDS_BLK_ON;
		ret = fmc_send_cmd(fmdev, POWER_SET, REG_WR, &payload,
				sizeof(payload), NULL, NULL);
		if (ret < 0)
			return ret;

		/* Clear and reset RDS FIFO */
		payload = FM_RX_RDS_FLUSH_FIFO;
		ret = fmc_send_cmd(fmdev, RDS_CNTRL_SET, REG_WR, &payload,
		sizeof(payload), NULL, NULL);
		if (ret < 0)
			return ret;

		/* Read flags - just to clear any pending interrupts. */
		ret = fmc_send_cmd(fmdev, FLAG_GET, REG_RD, NULL, 2,
				NULL, NULL);
		if (ret < 0)
			return ret;

		/* Set RDS FIFO threshold value */
		payload = FM_RX_RDS_FIFO_THRESHOLD;
		ret = fmc_send_cmd(fmdev, RDS_MEM_SET, REG_WR, &payload,
		sizeof(payload), NULL, NULL);
		if (ret < 0)
			return ret;

		/* Enable RDS interrupt */
		fmdev->irq_info.mask |= FM_RDS_EVENT;
		payload = fmdev->irq_info.mask;
		ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
				sizeof(payload), NULL, NULL);
		if (ret < 0) {
			fmdev->irq_info.mask &= ~FM_RDS_EVENT;
			return ret;
		}

		/* Update our local flag */
		fmdev->rx.rds.flag = FM_RDS_ENABLE;
	} else if (rds_en_dis == FM_RDS_DISABLE
		   && fmdev->rx.rds.flag == FM_RDS_ENABLE) {
		/* Turn off RX RDS */
		payload = FM_RX_PWR_SET_FM_ON_RDS_OFF;
		ret = fmc_send_cmd(fmdev, POWER_SET, REG_WR, &payload,
				sizeof(payload), NULL, NULL);
		if (ret < 0)
			return ret;

		/* Reset RDS pointers */
		fmdev->rx.rds.last_blk_idx = 0;
		fmdev->rx.rds.wr_idx = 0;
		fmdev->rx.rds.rd_idx = 0;
		fm_rx_reset_station_info(fmdev);

		/* Update RDS local cache */
		fmdev->irq_info.mask &= ~(FM_RDS_EVENT);
		fmdev->rx.rds.flag = FM_RDS_DISABLE;
	}

	return 0;
}

/* Returns current RX RDS enable/disable status */
int fm_rx_get_rds_mode(struct fmdev *fmdev, u8 *curr_rds_en_dis)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (curr_rds_en_dis == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*curr_rds_en_dis = fmdev->rx.rds.flag;

	return 0;
}

/* Sets RDS operation mode (RDS/RDBS) */
int fm_rx_set_rds_system(struct fmdev *fmdev, u8 rds_mode)
{
	u16 payload;
	int ret;

	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (rds_mode != FM_RDS_SYSTEM_RDS && rds_mode != FM_RDS_SYSTEM_RBDS) {
		fmerr("Invalid rds mode\n");
		return -EINVAL;
	}
	/* Set RDS operation mode */
	payload = (u16)rds_mode;
	ret = fmc_send_cmd(fmdev, RDS_SYSTEM_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmdev->rx.rds_mode = rds_mode;

	return 0;
}

/* Returns current RDS operation mode */
int fm_rx_get_rds_system(struct fmdev *fmdev, u8 *rds_mode)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (rds_mode == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*rds_mode = fmdev->rx.rds_mode;

	return 0;
}

/* Configures Alternate Frequency switch mode */
int fm_rx_set_af_switch(struct fmdev *fmdev, u8 af_mode)
{
	u16 payload;
	int ret;

	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (af_mode != FM_RX_RDS_AF_SWITCH_MODE_ON &&
	    af_mode != FM_RX_RDS_AF_SWITCH_MODE_OFF) {
		fmerr("Invalid af mode\n");
		return -EINVAL;
	}
	/* Enable/disable low RSSI interrupt based on af_mode */
	if (af_mode == FM_RX_RDS_AF_SWITCH_MODE_ON)
		fmdev->irq_info.mask |= FM_LEV_EVENT;
	else
		fmdev->irq_info.mask &= ~FM_LEV_EVENT;

	payload = fmdev->irq_info.mask;
	ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmdev->rx.af_mode = af_mode;

	return 0;
}

/* Returns Alternate Frequency switch status */
int fm_rx_get_af_switch(struct fmdev *fmdev, u8 *af_mode)
{
	if (fmdev->curr_fmmode != FM_MODE_RX)
		return -EPERM;

	if (af_mode == NULL) {
		fmerr("Invalid memory\n");
		return -ENOMEM;
	}

	*af_mode = fmdev->rx.af_mode;

	return 0;
}
