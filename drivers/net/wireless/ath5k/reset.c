/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
 * Copyright (c) 2007-2008 Luis Rodriguez <mcgrof@winlab.rutgers.edu>
 * Copyright (c) 2007-2008 Pavel Roskin <proski@gnu.org>
 * Copyright (c) 2007-2008 Jiri Slaby <jirislaby@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
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
 *
 */

#define _ATH5K_RESET

/*****************************\
  Reset functions and helpers
\*****************************/

#include <linux/pci.h>
#include "ath5k.h"
#include "reg.h"
#include "base.h"
#include "debug.h"

/**
 * ath5k_hw_write_ofdm_timings - set OFDM timings on AR5212
 *
 * @ah: the &struct ath5k_hw
 * @channel: the currently set channel upon reset
 *
 * Write the OFDM timings for the AR5212 upon reset. This is a helper for
 * ath5k_hw_reset(). This seems to tune the PLL a specified frequency
 * depending on the bandwidth of the channel.
 *
 */
static inline int ath5k_hw_write_ofdm_timings(struct ath5k_hw *ah,
	struct ieee80211_channel *channel)
{
	/* Get exponent and mantissa and set it */
	u32 coef_scaled, coef_exp, coef_man,
		ds_coef_exp, ds_coef_man, clock;

	if (!(ah->ah_version == AR5K_AR5212) ||
		!(channel->hw_value & CHANNEL_OFDM))
		BUG();

	/* Seems there are two PLLs, one for baseband sampling and one
	 * for tuning. Tuning basebands are 40 MHz or 80MHz when in
	 * turbo. */
	clock = channel->hw_value & CHANNEL_TURBO ? 80 : 40;
	coef_scaled = ((5 * (clock << 24)) / 2) /
	channel->center_freq;

	for (coef_exp = 31; coef_exp > 0; coef_exp--)
		if ((coef_scaled >> coef_exp) & 0x1)
			break;

	if (!coef_exp)
		return -EINVAL;

	coef_exp = 14 - (coef_exp - 24);
	coef_man = coef_scaled +
		(1 << (24 - coef_exp - 1));
	ds_coef_man = coef_man >> (24 - coef_exp);
	ds_coef_exp = coef_exp - 16;

	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_3,
		AR5K_PHY_TIMING_3_DSC_MAN, ds_coef_man);
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_3,
		AR5K_PHY_TIMING_3_DSC_EXP, ds_coef_exp);

	return 0;
}


/*
 * index into rates for control rates, we can set it up like this because
 * this is only used for AR5212 and we know it supports G mode
 */
static int control_rates[] =
	{ 0, 1, 1, 1, 4, 4, 6, 6, 8, 8, 8, 8 };

/**
 * ath5k_hw_write_rate_duration - set rate duration during hw resets
 *
 * @ah: the &struct ath5k_hw
 * @mode: one of enum ath5k_driver_mode
 *
 * Write the rate duration table upon hw reset. This is a helper for
 * ath5k_hw_reset(). It seems all this is doing is setting an ACK timeout for
 * the hardware for the current mode for each rate. The rates which are capable
 * of short preamble (802.11b rates 2Mbps, 5.5Mbps, and 11Mbps) have another
 * register for the short preamble ACK timeout calculation.
 */
static inline void ath5k_hw_write_rate_duration(struct ath5k_hw *ah,
       unsigned int mode)
{
	struct ath5k_softc *sc = ah->ah_sc;
	struct ieee80211_rate *rate;
	unsigned int i;

	/* Write rate duration table */
	for (i = 0; i < sc->sbands[IEEE80211_BAND_2GHZ].n_bitrates; i++) {
		u32 reg;
		u16 tx_time;

		rate = &sc->sbands[IEEE80211_BAND_2GHZ].bitrates[control_rates[i]];

		/* Set ACK timeout */
		reg = AR5K_RATE_DUR(rate->hw_value);

		/* An ACK frame consists of 10 bytes. If you add the FCS,
		 * which ieee80211_generic_frame_duration() adds,
		 * its 14 bytes. Note we use the control rate and not the
		 * actual rate for this rate. See mac80211 tx.c
		 * ieee80211_duration() for a brief description of
		 * what rate we should choose to TX ACKs. */
		tx_time = le16_to_cpu(ieee80211_generic_frame_duration(sc->hw,
							sc->vif, 10, rate));

		ath5k_hw_reg_write(ah, tx_time, reg);

		if (!(rate->flags & IEEE80211_RATE_SHORT_PREAMBLE))
			continue;

		/*
		 * We're not distinguishing short preamble here,
		 * This is true, all we'll get is a longer value here
		 * which is not necessarilly bad. We could use
		 * export ieee80211_frame_duration() but that needs to be
		 * fixed first to be properly used by mac802111 drivers:
		 *
		 *  - remove erp stuff and let the routine figure ofdm
		 *    erp rates
		 *  - remove passing argument ieee80211_local as
		 *    drivers don't have access to it
		 *  - move drivers using ieee80211_generic_frame_duration()
		 *    to this
		 */
		ath5k_hw_reg_write(ah, tx_time,
			reg + (AR5K_SET_SHORT_PREAMBLE << 2));
	}
}

/*
 * Reset chipset
 */
static int ath5k_hw_nic_reset(struct ath5k_hw *ah, u32 val)
{
	int ret;
	u32 mask = val ? val : ~0U;

	ATH5K_TRACE(ah->ah_sc);

	/* Read-and-clear RX Descriptor Pointer*/
	ath5k_hw_reg_read(ah, AR5K_RXDP);

	/*
	 * Reset the device and wait until success
	 */
	ath5k_hw_reg_write(ah, val, AR5K_RESET_CTL);

	/* Wait at least 128 PCI clocks */
	udelay(15);

	if (ah->ah_version == AR5K_AR5210) {
		val &= AR5K_RESET_CTL_PCU | AR5K_RESET_CTL_DMA
			| AR5K_RESET_CTL_MAC | AR5K_RESET_CTL_PHY;
		mask &= AR5K_RESET_CTL_PCU | AR5K_RESET_CTL_DMA
			| AR5K_RESET_CTL_MAC | AR5K_RESET_CTL_PHY;
	} else {
		val &= AR5K_RESET_CTL_PCU | AR5K_RESET_CTL_BASEBAND;
		mask &= AR5K_RESET_CTL_PCU | AR5K_RESET_CTL_BASEBAND;
	}

	ret = ath5k_hw_register_timeout(ah, AR5K_RESET_CTL, mask, val, false);

	/*
	 * Reset configuration register (for hw byte-swap). Note that this
	 * is only set for big endian. We do the necessary magic in
	 * AR5K_INIT_CFG.
	 */
	if ((val & AR5K_RESET_CTL_PCU) == 0)
		ath5k_hw_reg_write(ah, AR5K_INIT_CFG, AR5K_CFG);

	return ret;
}

/*
 * Sleep control
 */
int ath5k_hw_set_power(struct ath5k_hw *ah, enum ath5k_power_mode mode,
		bool set_chip, u16 sleep_duration)
{
	unsigned int i;
	u32 staid, data;

	ATH5K_TRACE(ah->ah_sc);
	staid = ath5k_hw_reg_read(ah, AR5K_STA_ID1);

	switch (mode) {
	case AR5K_PM_AUTO:
		staid &= ~AR5K_STA_ID1_DEFAULT_ANTENNA;
		/* fallthrough */
	case AR5K_PM_NETWORK_SLEEP:
		if (set_chip)
			ath5k_hw_reg_write(ah,
				AR5K_SLEEP_CTL_SLE_ALLOW |
				sleep_duration,
				AR5K_SLEEP_CTL);

		staid |= AR5K_STA_ID1_PWR_SV;
		break;

	case AR5K_PM_FULL_SLEEP:
		if (set_chip)
			ath5k_hw_reg_write(ah, AR5K_SLEEP_CTL_SLE_SLP,
				AR5K_SLEEP_CTL);

		staid |= AR5K_STA_ID1_PWR_SV;
		break;

	case AR5K_PM_AWAKE:

		staid &= ~AR5K_STA_ID1_PWR_SV;

		if (!set_chip)
			goto commit;

		/* Preserve sleep duration */
		data = ath5k_hw_reg_read(ah, AR5K_SLEEP_CTL);
		if (data & 0xffc00000)
			data = 0;
		else
			data = data & 0xfffcffff;

		ath5k_hw_reg_write(ah, data, AR5K_SLEEP_CTL);
		udelay(15);

		for (i = 50; i > 0; i--) {
			/* Check if the chip did wake up */
			if ((ath5k_hw_reg_read(ah, AR5K_PCICFG) &
					AR5K_PCICFG_SPWR_DN) == 0)
				break;

			/* Wait a bit and retry */
			udelay(200);
			ath5k_hw_reg_write(ah, data, AR5K_SLEEP_CTL);
		}

		/* Fail if the chip didn't wake up */
		if (i <= 0)
			return -EIO;

		break;

	default:
		return -EINVAL;
	}

commit:
	ah->ah_power_mode = mode;
	ath5k_hw_reg_write(ah, staid, AR5K_STA_ID1);

	return 0;
}

/*
 * Bring up MAC + PHY Chips
 */
int ath5k_hw_nic_wakeup(struct ath5k_hw *ah, int flags, bool initial)
{
	struct pci_dev *pdev = ah->ah_sc->pdev;
	u32 turbo, mode, clock, bus_flags;
	int ret;

	turbo = 0;
	mode = 0;
	clock = 0;

	ATH5K_TRACE(ah->ah_sc);

	/* Wakeup the device */
	ret = ath5k_hw_set_power(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah->ah_sc, "failed to wakeup the MAC Chip\n");
		return ret;
	}

	if (ah->ah_version != AR5K_AR5210) {
		/*
		 * Get channel mode flags
		 */

		if (ah->ah_radio >= AR5K_RF5112) {
			mode = AR5K_PHY_MODE_RAD_RF5112;
			clock = AR5K_PHY_PLL_RF5112;
		} else {
			mode = AR5K_PHY_MODE_RAD_RF5111;	/*Zero*/
			clock = AR5K_PHY_PLL_RF5111;		/*Zero*/
		}

		if (flags & CHANNEL_2GHZ) {
			mode |= AR5K_PHY_MODE_FREQ_2GHZ;
			clock |= AR5K_PHY_PLL_44MHZ;

			if (flags & CHANNEL_CCK) {
				mode |= AR5K_PHY_MODE_MOD_CCK;
			} else if (flags & CHANNEL_OFDM) {
				/* XXX Dynamic OFDM/CCK is not supported by the
				 * AR5211 so we set MOD_OFDM for plain g (no
				 * CCK headers) operation. We need to test
				 * this, 5211 might support ofdm-only g after
				 * all, there are also initial register values
				 * in the code for g mode (see initvals.c). */
				if (ah->ah_version == AR5K_AR5211)
					mode |= AR5K_PHY_MODE_MOD_OFDM;
				else
					mode |= AR5K_PHY_MODE_MOD_DYN;
			} else {
				ATH5K_ERR(ah->ah_sc,
					"invalid radio modulation mode\n");
				return -EINVAL;
			}
		} else if (flags & CHANNEL_5GHZ) {
			mode |= AR5K_PHY_MODE_FREQ_5GHZ;
			clock |= AR5K_PHY_PLL_40MHZ;

			if (flags & CHANNEL_OFDM)
				mode |= AR5K_PHY_MODE_MOD_OFDM;
			else {
				ATH5K_ERR(ah->ah_sc,
					"invalid radio modulation mode\n");
				return -EINVAL;
			}
		} else {
			ATH5K_ERR(ah->ah_sc, "invalid radio frequency mode\n");
			return -EINVAL;
		}

		if (flags & CHANNEL_TURBO)
			turbo = AR5K_PHY_TURBO_MODE | AR5K_PHY_TURBO_SHORT;
	} else { /* Reset the device */

		/* ...enable Atheros turbo mode if requested */
		if (flags & CHANNEL_TURBO)
			ath5k_hw_reg_write(ah, AR5K_PHY_TURBO_MODE,
					AR5K_PHY_TURBO);
	}

	/* reseting PCI on PCI-E cards results card to hang
	 * and always return 0xffff... so we ingore that flag
	 * for PCI-E cards */
	bus_flags = (pdev->is_pcie) ? 0 : AR5K_RESET_CTL_PCI;

	/* Reset chipset */
	if (ah->ah_version == AR5K_AR5210) {
		ret = ath5k_hw_nic_reset(ah, AR5K_RESET_CTL_PCU |
			AR5K_RESET_CTL_MAC | AR5K_RESET_CTL_DMA |
			AR5K_RESET_CTL_PHY | AR5K_RESET_CTL_PCI);
			mdelay(2);
	} else {
		ret = ath5k_hw_nic_reset(ah, AR5K_RESET_CTL_PCU |
			AR5K_RESET_CTL_BASEBAND | bus_flags);
	}
	if (ret) {
		ATH5K_ERR(ah->ah_sc, "failed to reset the MAC Chip\n");
		return -EIO;
	}

	/* ...wakeup again!*/
	ret = ath5k_hw_set_power(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah->ah_sc, "failed to resume the MAC Chip\n");
		return ret;
	}

	/* ...final warm reset */
	if (ath5k_hw_nic_reset(ah, 0)) {
		ATH5K_ERR(ah->ah_sc, "failed to warm reset the MAC Chip\n");
		return -EIO;
	}

	if (ah->ah_version != AR5K_AR5210) {
		/* ...set the PHY operating mode */
		ath5k_hw_reg_write(ah, clock, AR5K_PHY_PLL);
		udelay(300);

		ath5k_hw_reg_write(ah, mode, AR5K_PHY_MODE);
		ath5k_hw_reg_write(ah, turbo, AR5K_PHY_TURBO);
	}

	return 0;
}

/*
 * Main reset function
 */
int ath5k_hw_reset(struct ath5k_hw *ah, enum nl80211_iftype op_mode,
	struct ieee80211_channel *channel, bool change_channel)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct pci_dev *pdev = ah->ah_sc->pdev;
	u32 data, s_seq, s_ant, s_led[3], dma_size;
	unsigned int i, mode, freq, ee_mode, ant[2];
	int ret;

	ATH5K_TRACE(ah->ah_sc);

	s_seq = 0;
	s_ant = 0;
	ee_mode = 0;
	freq = 0;
	mode = 0;

	/*
	 * Save some registers before a reset
	 */
	/*DCU/Antenna selection not available on 5210*/
	if (ah->ah_version != AR5K_AR5210) {
		if (change_channel) {
			/* Seq number for queue 0 -do this for all queues ? */
			s_seq = ath5k_hw_reg_read(ah,
					AR5K_QUEUE_DFS_SEQNUM(0));
			/*Default antenna*/
			s_ant = ath5k_hw_reg_read(ah, AR5K_DEFAULT_ANTENNA);
		}
	}

	/*GPIOs*/
	s_led[0] = ath5k_hw_reg_read(ah, AR5K_PCICFG) & AR5K_PCICFG_LEDSTATE;
	s_led[1] = ath5k_hw_reg_read(ah, AR5K_GPIOCR);
	s_led[2] = ath5k_hw_reg_read(ah, AR5K_GPIODO);

	if (change_channel && ah->ah_rf_banks != NULL)
		ath5k_hw_get_rf_gain(ah);


	/*Wakeup the device*/
	ret = ath5k_hw_nic_wakeup(ah, channel->hw_value, false);
	if (ret)
		return ret;

	/*
	 * Initialize operating mode
	 */
	ah->ah_op_mode = op_mode;

	/*
	 * 5111/5112 Settings
	 * 5210 only comes with RF5110
	 */
	if (ah->ah_version != AR5K_AR5210) {
		if (ah->ah_radio != AR5K_RF5111 &&
			ah->ah_radio != AR5K_RF5112 &&
			ah->ah_radio != AR5K_RF5413 &&
			ah->ah_radio != AR5K_RF2413 &&
			ah->ah_radio != AR5K_RF2425) {
			ATH5K_ERR(ah->ah_sc,
				"invalid phy radio: %u\n", ah->ah_radio);
			return -EINVAL;
		}

		switch (channel->hw_value & CHANNEL_MODES) {
		case CHANNEL_A:
			mode = AR5K_MODE_11A;
			freq = AR5K_INI_RFGAIN_5GHZ;
			ee_mode = AR5K_EEPROM_MODE_11A;
			break;
		case CHANNEL_G:
			mode = AR5K_MODE_11G;
			freq = AR5K_INI_RFGAIN_2GHZ;
			ee_mode = AR5K_EEPROM_MODE_11G;
			break;
		case CHANNEL_B:
			mode = AR5K_MODE_11B;
			freq = AR5K_INI_RFGAIN_2GHZ;
			ee_mode = AR5K_EEPROM_MODE_11B;
			break;
		case CHANNEL_T:
			mode = AR5K_MODE_11A_TURBO;
			freq = AR5K_INI_RFGAIN_5GHZ;
			ee_mode = AR5K_EEPROM_MODE_11A;
			break;
		/*Is this ok on 5211 too ?*/
		case CHANNEL_TG:
			mode = AR5K_MODE_11G_TURBO;
			freq = AR5K_INI_RFGAIN_2GHZ;
			ee_mode = AR5K_EEPROM_MODE_11G;
			break;
		case CHANNEL_XR:
			if (ah->ah_version == AR5K_AR5211) {
				ATH5K_ERR(ah->ah_sc,
					"XR mode not available on 5211");
				return -EINVAL;
			}
			mode = AR5K_MODE_XR;
			freq = AR5K_INI_RFGAIN_5GHZ;
			ee_mode = AR5K_EEPROM_MODE_11A;
			break;
		default:
			ATH5K_ERR(ah->ah_sc,
				"invalid channel: %d\n", channel->center_freq);
			return -EINVAL;
		}

		/* PHY access enable */
		ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_5GHZ, AR5K_PHY(0));

	}

	ret = ath5k_hw_write_initvals(ah, mode, change_channel);
	if (ret)
		return ret;

	/*
	 * 5211/5212 Specific
	 */
	if (ah->ah_version != AR5K_AR5210) {
		/*
		 * Write initial RF gain settings
		 * This should work for both 5111/5112
		 */
		ret = ath5k_hw_rfgain(ah, freq);
		if (ret)
			return ret;

		mdelay(1);

		/*
		 * Write some more initial register settings for revised chips
		 */
		if (ah->ah_version == AR5K_AR5212 &&
		    ah->ah_phy_revision > 0x41) {
			ath5k_hw_reg_write(ah, 0x0002a002, 0x982c);

			if (channel->hw_value == CHANNEL_G)
				if (ah->ah_mac_srev < AR5K_SREV_AR2413)
					ath5k_hw_reg_write(ah, 0x00f80d80,
								0x994c);
				else if (ah->ah_mac_srev < AR5K_SREV_AR5424)
					ath5k_hw_reg_write(ah, 0x00380140,
								0x994c);
				else if (ah->ah_mac_srev < AR5K_SREV_AR2425)
					ath5k_hw_reg_write(ah, 0x00fc0ec0,
								0x994c);
				else /* 2425 */
					ath5k_hw_reg_write(ah, 0x00fc0fc0,
								0x994c);
			else
				ath5k_hw_reg_write(ah, 0x00000000, 0x994c);

			/* Got this from legacy-hal */
			AR5K_REG_DISABLE_BITS(ah, 0xa228, 0x200);

			AR5K_REG_MASKED_BITS(ah, 0xa228, 0x800, 0xfffe03ff);

			/* Just write 0x9b5 ? */
			/* ath5k_hw_reg_write(ah, 0x000009b5, 0xa228); */
			ath5k_hw_reg_write(ah, 0x0000000f, AR5K_SEQ_MASK);
			ath5k_hw_reg_write(ah, 0x00000000, 0xa254);
			ath5k_hw_reg_write(ah, 0x0000000e, AR5K_PHY_SCAL);
		}

		/* Fix for first revision of the RF5112 RF chipset */
		if (ah->ah_radio >= AR5K_RF5112 &&
				ah->ah_radio_5ghz_revision <
				AR5K_SREV_RAD_5112A) {
			ath5k_hw_reg_write(ah, AR5K_PHY_CCKTXCTL_WORLD,
					AR5K_PHY_CCKTXCTL);
			if (channel->hw_value & CHANNEL_5GHZ)
				data = 0xffb81020;
			else
				data = 0xffb80d20;
			ath5k_hw_reg_write(ah, data, AR5K_PHY_FRAME_CTL);
			data = 0;
		}

		/*
		 * Set TX power (FIXME)
		 */
		ret = ath5k_hw_txpower(ah, channel, AR5K_TUNE_DEFAULT_TXPOWER);
		if (ret)
			return ret;

		/* Write rate duration table only on AR5212 and if
		 * virtual interface has already been brought up
		 * XXX: rethink this after new mode changes to
		 * mac80211 are integrated */
		if (ah->ah_version == AR5K_AR5212 &&
			ah->ah_sc->vif != NULL)
			ath5k_hw_write_rate_duration(ah, mode);

		/*
		 * Write RF registers
		 */
		ret = ath5k_hw_rfregs(ah, channel, mode);
		if (ret)
			return ret;

		/*
		 * Configure additional registers
		 */

		/* Write OFDM timings on 5212*/
		if (ah->ah_version == AR5K_AR5212 &&
			channel->hw_value & CHANNEL_OFDM) {
			ret = ath5k_hw_write_ofdm_timings(ah, channel);
			if (ret)
				return ret;
		}

		/*Enable/disable 802.11b mode on 5111
		(enable 2111 frequency converter + CCK)*/
		if (ah->ah_radio == AR5K_RF5111) {
			if (mode == AR5K_MODE_11B)
				AR5K_REG_ENABLE_BITS(ah, AR5K_TXCFG,
				    AR5K_TXCFG_B_MODE);
			else
				AR5K_REG_DISABLE_BITS(ah, AR5K_TXCFG,
				    AR5K_TXCFG_B_MODE);
		}

		/*
		 * Set channel and calibrate the PHY
		 */
		ret = ath5k_hw_channel(ah, channel);
		if (ret)
			return ret;

		/* Set antenna mode */
		AR5K_REG_MASKED_BITS(ah, AR5K_PHY_ANT_CTL,
			ah->ah_antenna[ee_mode][0], 0xfffffc06);

		/*
		 * In case a fixed antenna was set as default
		 * write the same settings on both AR5K_PHY_ANT_SWITCH_TABLE
		 * registers.
		 */
		if (s_ant != 0) {
			if (s_ant == AR5K_ANT_FIXED_A) /* 1 - Main */
				ant[0] = ant[1] = AR5K_ANT_FIXED_A;
			else	/* 2 - Aux */
				ant[0] = ant[1] = AR5K_ANT_FIXED_B;
		} else {
			ant[0] = AR5K_ANT_FIXED_A;
			ant[1] = AR5K_ANT_FIXED_B;
		}

		ath5k_hw_reg_write(ah, ah->ah_antenna[ee_mode][ant[0]],
			AR5K_PHY_ANT_SWITCH_TABLE_0);
		ath5k_hw_reg_write(ah, ah->ah_antenna[ee_mode][ant[1]],
			AR5K_PHY_ANT_SWITCH_TABLE_1);

		/* Commit values from EEPROM */
		if (ah->ah_radio == AR5K_RF5111)
			AR5K_REG_WRITE_BITS(ah, AR5K_PHY_FRAME_CTL,
			    AR5K_PHY_FRAME_CTL_TX_CLIP, ee->ee_tx_clip);

		ath5k_hw_reg_write(ah,
			AR5K_PHY_NF_SVAL(ee->ee_noise_floor_thr[ee_mode]),
			AR5K_PHY_NFTHRES);

		AR5K_REG_MASKED_BITS(ah, AR5K_PHY_SETTLING,
			(ee->ee_switch_settling[ee_mode] << 7) & 0x3f80,
			0xffffc07f);
		AR5K_REG_MASKED_BITS(ah, AR5K_PHY_GAIN,
			(ee->ee_atn_tx_rx[ee_mode] << 12) & 0x3f000,
			0xfffc0fff);
		AR5K_REG_MASKED_BITS(ah, AR5K_PHY_DESIRED_SIZE,
			(ee->ee_adc_desired_size[ee_mode] & 0x00ff) |
			((ee->ee_pga_desired_size[ee_mode] << 8) & 0xff00),
			0xffff0000);

		ath5k_hw_reg_write(ah,
			(ee->ee_tx_end2xpa_disable[ee_mode] << 24) |
			(ee->ee_tx_end2xpa_disable[ee_mode] << 16) |
			(ee->ee_tx_frm2xpa_enable[ee_mode] << 8) |
			(ee->ee_tx_frm2xpa_enable[ee_mode]), AR5K_PHY_RF_CTL4);

		AR5K_REG_MASKED_BITS(ah, AR5K_PHY_RF_CTL3,
			ee->ee_tx_end2xlna_enable[ee_mode] << 8, 0xffff00ff);
		AR5K_REG_MASKED_BITS(ah, AR5K_PHY_NF,
			(ee->ee_thr_62[ee_mode] << 12) & 0x7f000, 0xfff80fff);
		AR5K_REG_MASKED_BITS(ah, AR5K_PHY_OFDM_SELFCORR, 4, 0xffffff01);

		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_IQ,
		    AR5K_PHY_IQ_CORR_ENABLE |
		    (ee->ee_i_cal[ee_mode] << AR5K_PHY_IQ_CORR_Q_I_COFF_S) |
		    ee->ee_q_cal[ee_mode]);

		if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_1)
			AR5K_REG_WRITE_BITS(ah, AR5K_PHY_GAIN_2GHZ,
				AR5K_PHY_GAIN_2GHZ_MARGIN_TXRX,
				ee->ee_margin_tx_rx[ee_mode]);

	} else {
		mdelay(1);
		/* Disable phy and wait */
		ath5k_hw_reg_write(ah, AR5K_PHY_ACT_DISABLE, AR5K_PHY_ACT);
		mdelay(1);
	}

	/*
	 * Restore saved values
	 */
	/*DCU/Antenna selection not available on 5210*/
	if (ah->ah_version != AR5K_AR5210) {
		ath5k_hw_reg_write(ah, s_seq, AR5K_QUEUE_DFS_SEQNUM(0));
		ath5k_hw_reg_write(ah, s_ant, AR5K_DEFAULT_ANTENNA);
	}
	AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, s_led[0]);
	ath5k_hw_reg_write(ah, s_led[1], AR5K_GPIOCR);
	ath5k_hw_reg_write(ah, s_led[2], AR5K_GPIODO);

	/*
	 * Misc
	 */
	/* XXX: add ah->aid once mac80211 gives this to us */
	ath5k_hw_set_associd(ah, ah->ah_bssid, 0);

	ath5k_hw_set_opmode(ah);
	/*PISR/SISR Not available on 5210*/
	if (ah->ah_version != AR5K_AR5210) {
		ath5k_hw_reg_write(ah, 0xffffffff, AR5K_PISR);
		/* If we later allow tuning for this, store into sc structure */
		data = AR5K_TUNE_RSSI_THRES |
			AR5K_TUNE_BMISS_THRES << AR5K_RSSI_THR_BMISS_S;
		ath5k_hw_reg_write(ah, data, AR5K_RSSI_THR);
	}

	/*
	 * Set Rx/Tx DMA Configuration
	 *
	 * Set maximum DMA size (512) except for PCI-E cards since
	 * it causes rx overruns and tx errors (tested on 5424 but since
	 * rx overruns also occur on 5416/5418 with madwifi we set 128
	 * for all PCI-E cards to be safe).
	 *
	 * In dumps this is 128 for allchips.
	 *
	 * XXX: need to check 5210 for this
	 * TODO: Check out tx triger level, it's always 64 on dumps but I
	 * guess we can tweak it and see how it goes ;-)
	 */
	dma_size = (pdev->is_pcie) ? AR5K_DMASIZE_128B : AR5K_DMASIZE_512B;
	if (ah->ah_version != AR5K_AR5210) {
		AR5K_REG_WRITE_BITS(ah, AR5K_TXCFG,
			AR5K_TXCFG_SDMAMR, dma_size);
		AR5K_REG_WRITE_BITS(ah, AR5K_RXCFG,
			AR5K_RXCFG_SDMAMW, dma_size);
	}

	/*
	 * Enable the PHY and wait until completion
	 */
	ath5k_hw_reg_write(ah, AR5K_PHY_ACT_ENABLE, AR5K_PHY_ACT);

	/*
	 * On 5211+ read activation -> rx delay
	 * and use it.
	 */
	if (ah->ah_version != AR5K_AR5210) {
		data = ath5k_hw_reg_read(ah, AR5K_PHY_RX_DELAY) &
			AR5K_PHY_RX_DELAY_M;
		data = (channel->hw_value & CHANNEL_CCK) ?
			((data << 2) / 22) : (data / 10);

		udelay(100 + (2 * data));
		data = 0;
	} else {
		mdelay(1);
	}

	/*
	 * Perform ADC test (?)
	 */
	data = ath5k_hw_reg_read(ah, AR5K_PHY_TST1);
	ath5k_hw_reg_write(ah, AR5K_PHY_TST1_TXHOLD, AR5K_PHY_TST1);
	for (i = 0; i <= 20; i++) {
		if (!(ath5k_hw_reg_read(ah, AR5K_PHY_ADC_TEST) & 0x10))
			break;
		udelay(200);
	}
	ath5k_hw_reg_write(ah, data, AR5K_PHY_TST1);
	data = 0;

	/*
	 * Start automatic gain calibration
	 *
	 * During AGC calibration RX path is re-routed to
	 * a signal detector so we don't receive anything.
	 *
	 * This method is used to calibrate some static offsets
	 * used together with on-the fly I/Q calibration (the
	 * one performed via ath5k_hw_phy_calibrate), that doesn't
	 * interrupt rx path.
	 *
	 * If we are in a noisy environment AGC calibration may time
	 * out.
	 */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
				AR5K_PHY_AGCCTL_CAL);

	/* At the same time start I/Q calibration for QAM constellation
	 * -no need for CCK- */
	ah->ah_calibration = false;
	if (!(mode == AR5K_MODE_11B)) {
		ah->ah_calibration = true;
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_IQ,
				AR5K_PHY_IQ_CAL_NUM_LOG_MAX, 15);
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_IQ,
				AR5K_PHY_IQ_RUN);
	}

	/* Wait for gain calibration to finish (we check for I/Q calibration
	 * during ath5k_phy_calibrate) */
	if (ath5k_hw_register_timeout(ah, AR5K_PHY_AGCCTL,
			AR5K_PHY_AGCCTL_CAL, 0, false)) {
		ATH5K_ERR(ah->ah_sc, "gain calibration timeout (%uMHz)\n",
			channel->center_freq);
		return -EAGAIN;
	}

	/*
	 * Start noise floor calibration
	 *
	 * If we run NF calibration before AGC, it always times out.
	 * Binary HAL starts NF and AGC calibration at the same time
	 * and only waits for AGC to finish. I believe that's wrong because
	 * during NF calibration, rx path is also routed to a detector, so if
	 * it doesn't finish we won't have RX.
	 *
	 * XXX: Find an interval that's OK for all cards...
	 */
	ath5k_hw_noise_floor_calibration(ah, channel->center_freq);

	/*
	 * Reset queues and start beacon timers at the end of the reset routine
	 */
	for (i = 0; i < ah->ah_capabilities.cap_queues.q_tx_num; i++) {
		/*No QCU on 5210*/
		if (ah->ah_version != AR5K_AR5210)
			AR5K_REG_WRITE_Q(ah, AR5K_QUEUE_QCUMASK(i), i);

		ret = ath5k_hw_reset_tx_queue(ah, i);
		if (ret) {
			ATH5K_ERR(ah->ah_sc,
				"failed to reset TX queue #%d\n", i);
			return ret;
		}
	}

	/* Pre-enable interrupts on 5211/5212*/
	if (ah->ah_version != AR5K_AR5210)
		ath5k_hw_set_imr(ah, ah->ah_imr);

	/*
	 * Set RF kill flags if supported by the device (read from the EEPROM)
	 * Disable gpio_intr for now since it results system hang.
	 * TODO: Handle this in ath5k_intr
	 */
#if 0
	if (AR5K_EEPROM_HDR_RFKILL(ah->ah_capabilities.cap_eeprom.ee_header)) {
		ath5k_hw_set_gpio_input(ah, 0);
		ah->ah_gpio[0] = ath5k_hw_get_gpio(ah, 0);
		if (ah->ah_gpio[0] == 0)
			ath5k_hw_set_gpio_intr(ah, 0, 1);
		else
			ath5k_hw_set_gpio_intr(ah, 0, 0);
	}
#endif

	/*
	 * Set the 32MHz reference clock on 5212 phy clock sleep register
	 *
	 * TODO: Find out how to switch to external 32Khz clock to save power
	 */
	if (ah->ah_version == AR5K_AR5212) {
		ath5k_hw_reg_write(ah, AR5K_PHY_SCR_32MHZ, AR5K_PHY_SCR);
		ath5k_hw_reg_write(ah, AR5K_PHY_SLMT_32MHZ, AR5K_PHY_SLMT);
		ath5k_hw_reg_write(ah, AR5K_PHY_SCAL_32MHZ, AR5K_PHY_SCAL);
		ath5k_hw_reg_write(ah, AR5K_PHY_SCLOCK_32MHZ, AR5K_PHY_SCLOCK);
		ath5k_hw_reg_write(ah, AR5K_PHY_SDELAY_32MHZ, AR5K_PHY_SDELAY);
		ath5k_hw_reg_write(ah, ah->ah_phy_spending, AR5K_PHY_SPENDING);

		data = ath5k_hw_reg_read(ah, AR5K_USEC_5211) & 0xffffc07f ;
		data |= (ah->ah_phy_spending == AR5K_PHY_SPENDING_18) ?
						0x00000f80 : 0x00001380 ;
		ath5k_hw_reg_write(ah, data, AR5K_USEC_5211);
		data = 0;
	}

	if (ah->ah_version == AR5K_AR5212) {
		ath5k_hw_reg_write(ah, 0x000100aa, 0x8118);
		ath5k_hw_reg_write(ah, 0x00003210, 0x811c);
		ath5k_hw_reg_write(ah, 0x00000052, 0x8108);
		if (ah->ah_mac_srev >= AR5K_SREV_AR2413)
			ath5k_hw_reg_write(ah, 0x00000004, 0x8120);
	}

	/*
	 * Disable beacons and reset the register
	 */
	AR5K_REG_DISABLE_BITS(ah, AR5K_BEACON, AR5K_BEACON_ENABLE |
			AR5K_BEACON_RESET_TSF);

	return 0;
}

#undef _ATH5K_RESET
