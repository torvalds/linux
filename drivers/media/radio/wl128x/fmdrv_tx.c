/*
 *  FM Driver for Connectivity chip of Texas Instruments.
 *  This sub-module of FM driver implements FM TX functionality.
 *
 *  Copyright (C) 2011 Texas Instruments
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

#include <linux/delay.h>
#include "fmdrv.h"
#include "fmdrv_common.h"
#include "fmdrv_tx.h"

u32 fm_tx_set_stereo_mono(struct fmdev *fmdev, u16 mode)
{
	u16 payload;
	u32 ret;

	if (fmdev->tx_data.aud_mode == mode)
		return 0;

	fmdbg("stereo mode: %d\n", mode);

	/* Set Stereo/Mono mode */
	payload = (1 - mode);
	ret = fmc_send_cmd(fmdev, MONO_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fmdev->tx_data.aud_mode = mode;

	return ret;
}

static u32 set_rds_text(struct fmdev *fmdev, u8 *rds_text)
{
	u16 payload;
	u32 ret;

	ret = fmc_send_cmd(fmdev, RDS_DATA_SET, REG_WR, rds_text,
			strlen(rds_text), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Scroll mode */
	payload = (u16)0x1;
	ret = fmc_send_cmd(fmdev, DISPLAY_MODE, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

static u32 set_rds_data_mode(struct fmdev *fmdev, u8 mode)
{
	u16 payload;
	u32 ret;

	/* Setting unique PI TODO: how unique? */
	payload = (u16)0xcafe;
	ret = fmc_send_cmd(fmdev, PI_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Set decoder id */
	payload = (u16)0xa;
	ret = fmc_send_cmd(fmdev, DI_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* TODO: RDS_MODE_GET? */
	return 0;
}

static u32 set_rds_len(struct fmdev *fmdev, u8 type, u16 len)
{
	u16 payload;
	u32 ret;

	len |= type << 8;
	payload = len;
	ret = fmc_send_cmd(fmdev, RDS_CONFIG_DATA_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* TODO: LENGTH_GET? */
	return 0;
}

u32 fm_tx_set_rds_mode(struct fmdev *fmdev, u8 rds_en_dis)
{
	u16 payload;
	u32 ret;
	u8 rds_text[] = "Zoom2\n";

	fmdbg("rds_en_dis:%d(E:%d, D:%d)\n", rds_en_dis,
		   FM_RDS_ENABLE, FM_RDS_DISABLE);

	if (rds_en_dis == FM_RDS_ENABLE) {
		/* Set RDS length */
		set_rds_len(fmdev, 0, strlen(rds_text));

		/* Set RDS text */
		set_rds_text(fmdev, rds_text);

		/* Set RDS mode */
		set_rds_data_mode(fmdev, 0x0);
	}

	/* Send command to enable RDS */
	if (rds_en_dis == FM_RDS_ENABLE)
		payload = 0x01;
	else
		payload = 0x00;

	ret = fmc_send_cmd(fmdev, RDS_DATA_ENB, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	if (rds_en_dis == FM_RDS_ENABLE) {
		/* Set RDS length */
		set_rds_len(fmdev, 0, strlen(rds_text));

		/* Set RDS text */
		set_rds_text(fmdev, rds_text);
	}
	fmdev->tx_data.rds.flag = rds_en_dis;

	return 0;
}

u32 fm_tx_set_radio_text(struct fmdev *fmdev, u8 *rds_text, u8 rds_type)
{
	u16 payload;
	u32 ret;

	if (fmdev->curr_fmmode != FM_MODE_TX)
		return -EPERM;

	fm_tx_set_rds_mode(fmdev, 0);

	/* Set RDS length */
	set_rds_len(fmdev, rds_type, strlen(rds_text));

	/* Set RDS text */
	set_rds_text(fmdev, rds_text);

	/* Set RDS mode */
	set_rds_data_mode(fmdev, 0x0);

	payload = 1;
	ret = fmc_send_cmd(fmdev, RDS_DATA_ENB, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

u32 fm_tx_set_af(struct fmdev *fmdev, u32 af)
{
	u16 payload;
	u32 ret;

	if (fmdev->curr_fmmode != FM_MODE_TX)
		return -EPERM;

	fmdbg("AF: %d\n", af);

	af = (af - 87500) / 100;
	payload = (u16)af;
	ret = fmc_send_cmd(fmdev, TA_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

u32 fm_tx_set_region(struct fmdev *fmdev, u8 region)
{
	u16 payload;
	u32 ret;

	if (region != FM_BAND_EUROPE_US && region != FM_BAND_JAPAN) {
		fmerr("Invalid band\n");
		return -EINVAL;
	}

	/* Send command to set the band */
	payload = (u16)region;
	ret = fmc_send_cmd(fmdev, TX_BAND_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

u32 fm_tx_set_mute_mode(struct fmdev *fmdev, u8 mute_mode_toset)
{
	u16 payload;
	u32 ret;

	fmdbg("tx: mute mode %d\n", mute_mode_toset);

	payload = mute_mode_toset;
	ret = fmc_send_cmd(fmdev, MUTE, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

/* Set TX Audio I/O */
static u32 set_audio_io(struct fmdev *fmdev)
{
	struct fmtx_data *tx = &fmdev->tx_data;
	u16 payload;
	u32 ret;

	/* Set Audio I/O Enable */
	payload = tx->audio_io;
	ret = fmc_send_cmd(fmdev, AUDIO_IO_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* TODO: is audio set? */
	return 0;
}

/* Start TX Transmission */
static u32 enable_xmit(struct fmdev *fmdev, u8 new_xmit_state)
{
	struct fmtx_data *tx = &fmdev->tx_data;
	unsigned long timeleft;
	u16 payload;
	u32 ret;

	/* Enable POWER_ENB interrupts */
	payload = FM_POW_ENB_EVENT;
	ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Set Power Enable */
	payload = new_xmit_state;
	ret = fmc_send_cmd(fmdev, POWER_ENB_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* Wait for Power Enabled */
	init_completion(&fmdev->maintask_comp);
	timeleft = wait_for_completion_timeout(&fmdev->maintask_comp,
			FM_DRV_TX_TIMEOUT);
	if (!timeleft) {
		fmerr("Timeout(%d sec),didn't get tune ended interrupt\n",
			   jiffies_to_msecs(FM_DRV_TX_TIMEOUT) / 1000);
		return -ETIMEDOUT;
	}

	set_bit(FM_CORE_TX_XMITING, &fmdev->flag);
	tx->xmit_state = new_xmit_state;

	return 0;
}

/* Set TX power level */
u32 fm_tx_set_pwr_lvl(struct fmdev *fmdev, u8 new_pwr_lvl)
{
	u16 payload;
	struct fmtx_data *tx = &fmdev->tx_data;
	u32 ret;

	if (fmdev->curr_fmmode != FM_MODE_TX)
		return -EPERM;
	fmdbg("tx: pwr_level_to_set %ld\n", (long int)new_pwr_lvl);

	/* If the core isn't ready update global variable */
	if (!test_bit(FM_CORE_READY, &fmdev->flag)) {
		tx->pwr_lvl = new_pwr_lvl;
		return 0;
	}

	/* Set power level: Application will specify power level value in
	 * units of dB/uV, whereas range and step are specific to FM chip.
	 * For TI's WL chips, convert application specified power level value
	 * to chip specific value by subtracting 122 from it. Refer to TI FM
	 * data sheet for details.
	 * */

	payload = (FM_PWR_LVL_HIGH - new_pwr_lvl);
	ret = fmc_send_cmd(fmdev, POWER_LEV_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	/* TODO: is the power level set? */
	tx->pwr_lvl = new_pwr_lvl;

	return 0;
}

/*
 * Sets FM TX pre-emphasis filter value (OFF, 50us, or 75us)
 * Convert V4L2 specified filter values to chip specific filter values.
 */
u32 fm_tx_set_preemph_filter(struct fmdev *fmdev, u32 preemphasis)
{
	struct fmtx_data *tx = &fmdev->tx_data;
	u16 payload;
	u32 ret;

	if (fmdev->curr_fmmode != FM_MODE_TX)
		return -EPERM;

	switch (preemphasis) {
	case V4L2_PREEMPHASIS_DISABLED:
		payload = FM_TX_PREEMPH_OFF;
		break;
	case V4L2_PREEMPHASIS_50_uS:
		payload = FM_TX_PREEMPH_50US;
		break;
	case V4L2_PREEMPHASIS_75_uS:
		payload = FM_TX_PREEMPH_75US;
		break;
	}

	ret = fmc_send_cmd(fmdev, PREMPH_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	tx->preemph = payload;

	return ret;
}

/* Get the TX tuning capacitor value.*/
u32 fm_tx_get_tune_cap_val(struct fmdev *fmdev)
{
	u16 curr_val;
	u32 ret, resp_len;

	if (fmdev->curr_fmmode != FM_MODE_TX)
		return -EPERM;

	ret = fmc_send_cmd(fmdev, READ_FMANT_TUNE_VALUE, REG_RD,
			NULL, sizeof(curr_val), &curr_val, &resp_len);
	if (ret < 0)
		return ret;

	curr_val = be16_to_cpu(curr_val);

	return curr_val;
}

/* Set TX Frequency */
u32 fm_tx_set_freq(struct fmdev *fmdev, u32 freq_to_set)
{
	struct fmtx_data *tx = &fmdev->tx_data;
	u16 payload, chanl_index;
	u32 ret;

	if (test_bit(FM_CORE_TX_XMITING, &fmdev->flag)) {
		enable_xmit(fmdev, 0);
		clear_bit(FM_CORE_TX_XMITING, &fmdev->flag);
	}

	/* Enable FR, BL interrupts */
	payload = (FM_FR_EVENT | FM_BL_EVENT);
	ret = fmc_send_cmd(fmdev, INT_MASK_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	tx->tx_frq = (unsigned long)freq_to_set;
	fmdbg("tx: freq_to_set %ld\n", (long int)tx->tx_frq);

	chanl_index = freq_to_set / 10;

	/* Set current tuner channel */
	payload = chanl_index;
	ret = fmc_send_cmd(fmdev, CHANL_SET, REG_WR, &payload,
			sizeof(payload), NULL, NULL);
	if (ret < 0)
		return ret;

	fm_tx_set_pwr_lvl(fmdev, tx->pwr_lvl);
	fm_tx_set_preemph_filter(fmdev, tx->preemph);

	tx->audio_io = 0x01;	/* I2S */
	set_audio_io(fmdev);

	enable_xmit(fmdev, 0x01);	/* Enable transmission */

	tx->aud_mode = FM_STEREO_MODE;
	tx->rds.flag = FM_RDS_DISABLE;

	return 0;
}

