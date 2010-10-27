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

/*****************************\
  Reset functions and helpers
\*****************************/

#include <asm/unaligned.h>

#include <linux/pci.h> 		/* To determine if a card is pci-e */
#include <linux/log2.h>
#include "ath5k.h"
#include "reg.h"
#include "base.h"
#include "debug.h"

/*
 * Check if a register write has been completed
 */
int ath5k_hw_register_timeout(struct ath5k_hw *ah, u32 reg, u32 flag, u32 val,
			      bool is_set)
{
	int i;
	u32 data;

	for (i = AR5K_TUNE_REGISTER_TIMEOUT; i > 0; i--) {
		data = ath5k_hw_reg_read(ah, reg);
		if (is_set && (data & flag))
			break;
		else if ((data & flag) == val)
			break;
		udelay(15);
	}

	return (i <= 0) ? -EAGAIN : 0;
}

/**
 * ath5k_hw_write_ofdm_timings - set OFDM timings on AR5212
 *
 * @ah: the &struct ath5k_hw
 * @channel: the currently set channel upon reset
 *
 * Write the delta slope coefficient (used on pilot tracking ?) for OFDM
 * operation on the AR5212 upon reset. This is a helper for ath5k_hw_reset().
 *
 * Since delta slope is floating point we split it on its exponent and
 * mantissa and provide these values on hw.
 *
 * For more infos i think this patent is related
 * http://www.freepatentsonline.com/7184495.html
 */
static inline int ath5k_hw_write_ofdm_timings(struct ath5k_hw *ah,
	struct ieee80211_channel *channel)
{
	/* Get exponent and mantissa and set it */
	u32 coef_scaled, coef_exp, coef_man,
		ds_coef_exp, ds_coef_man, clock;

	BUG_ON(!(ah->ah_version == AR5K_AR5212) ||
		!(channel->hw_value & CHANNEL_OFDM));

	/* Get coefficient
	 * ALGO: coef = (5 * clock / carrier_freq) / 2
	 * we scale coef by shifting clock value by 24 for
	 * better precision since we use integers */
	/* TODO: Half/quarter rate */
	clock =  (channel->hw_value & CHANNEL_TURBO) ? 80 : 40;
	coef_scaled = ((5 * (clock << 24)) / 2) / channel->center_freq;

	/* Get exponent
	 * ALGO: coef_exp = 14 - highest set bit position */
	coef_exp = ilog2(coef_scaled);

	/* Doesn't make sense if it's zero*/
	if (!coef_scaled || !coef_exp)
		return -EINVAL;

	/* Note: we've shifted coef_scaled by 24 */
	coef_exp = 14 - (coef_exp - 24);


	/* Get mantissa (significant digits)
	 * ALGO: coef_mant = floor(coef_scaled* 2^coef_exp+0.5) */
	coef_man = coef_scaled +
		(1 << (24 - coef_exp - 1));

	/* Calculate delta slope coefficient exponent
	 * and mantissa (remove scaling) and set them on hw */
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
static const unsigned int control_rates[] =
	{ 0, 1, 1, 1, 4, 4, 6, 6, 8, 8, 8, 8 };

/**
 * ath5k_hw_write_rate_duration - fill rate code to duration table
 *
 * @ah: the &struct ath5k_hw
 * @mode: one of enum ath5k_driver_mode
 *
 * Write the rate code to duration table upon hw reset. This is a helper for
 * ath5k_hw_reset(). It seems all this is doing is setting an ACK timeout on
 * the hardware, based on current mode, for each rate. The rates which are
 * capable of short preamble (802.11b rates 2Mbps, 5.5Mbps, and 11Mbps) have
 * different rate code so we write their value twice (one for long preample
 * and one for short).
 *
 * Note: Band doesn't matter here, if we set the values for OFDM it works
 * on both a and g modes. So all we have to do is set values for all g rates
 * that include all OFDM and CCK rates. If we operate in turbo or xr/half/
 * quarter rate mode, we need to use another set of bitrates (that's why we
 * need the mode parameter) but we don't handle these proprietary modes yet.
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
static int ath5k_hw_set_power(struct ath5k_hw *ah, enum ath5k_power_mode mode,
			      bool set_chip, u16 sleep_duration)
{
	unsigned int i;
	u32 staid, data;

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

		data = ath5k_hw_reg_read(ah, AR5K_SLEEP_CTL);

		/* If card is down we 'll get 0xffff... so we
		 * need to clean this up before we write the register
		 */
		if (data & 0xffc00000)
			data = 0;
		else
			/* Preserve sleep duration etc */
			data = data & ~AR5K_SLEEP_CTL_SLE;

		ath5k_hw_reg_write(ah, data | AR5K_SLEEP_CTL_SLE_WAKE,
							AR5K_SLEEP_CTL);
		udelay(15);

		for (i = 200; i > 0; i--) {
			/* Check if the chip did wake up */
			if ((ath5k_hw_reg_read(ah, AR5K_PCICFG) &
					AR5K_PCICFG_SPWR_DN) == 0)
				break;

			/* Wait a bit and retry */
			udelay(50);
			ath5k_hw_reg_write(ah, data | AR5K_SLEEP_CTL_SLE_WAKE,
							AR5K_SLEEP_CTL);
		}

		/* Fail if the chip didn't wake up */
		if (i == 0)
			return -EIO;

		break;

	default:
		return -EINVAL;
	}

commit:
	ath5k_hw_reg_write(ah, staid, AR5K_STA_ID1);

	return 0;
}

/*
 * Put device on hold
 *
 * Put MAC and Baseband on warm reset and
 * keep that state (don't clean sleep control
 * register). After this MAC and Baseband are
 * disabled and a full reset is needed to come
 * back. This way we save as much power as possible
 * without puting the card on full sleep.
 */
int ath5k_hw_on_hold(struct ath5k_hw *ah)
{
	struct pci_dev *pdev = ah->ah_sc->pdev;
	u32 bus_flags;
	int ret;

	/* Make sure device is awake */
	ret = ath5k_hw_set_power(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah->ah_sc, "failed to wakeup the MAC Chip\n");
		return ret;
	}

	/*
	 * Put chipset on warm reset...
	 *
	 * Note: puting PCI core on warm reset on PCI-E cards
	 * results card to hang and always return 0xffff... so
	 * we ingore that flag for PCI-E cards. On PCI cards
	 * this flag gets cleared after 64 PCI clocks.
	 */
	bus_flags = (pdev->is_pcie) ? 0 : AR5K_RESET_CTL_PCI;

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
		ATH5K_ERR(ah->ah_sc, "failed to put device on warm reset\n");
		return -EIO;
	}

	/* ...wakeup again!*/
	ret = ath5k_hw_set_power(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah->ah_sc, "failed to put device on hold\n");
		return ret;
	}

	return ret;
}

/*
 * Bring up MAC + PHY Chips and program PLL
 * TODO: Half/Quarter rate support
 */
int ath5k_hw_nic_wakeup(struct ath5k_hw *ah, int flags, bool initial)
{
	struct pci_dev *pdev = ah->ah_sc->pdev;
	u32 turbo, mode, clock, bus_flags;
	int ret;

	turbo = 0;
	mode = 0;
	clock = 0;

	/* Wakeup the device */
	ret = ath5k_hw_set_power(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah->ah_sc, "failed to wakeup the MAC Chip\n");
		return ret;
	}

	/*
	 * Put chipset on warm reset...
	 *
	 * Note: puting PCI core on warm reset on PCI-E cards
	 * results card to hang and always return 0xffff... so
	 * we ingore that flag for PCI-E cards. On PCI cards
	 * this flag gets cleared after 64 PCI clocks.
	 */
	bus_flags = (pdev->is_pcie) ? 0 : AR5K_RESET_CTL_PCI;

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

	/* ...wakeup again!...*/
	ret = ath5k_hw_set_power(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah->ah_sc, "failed to resume the MAC Chip\n");
		return ret;
	}

	/* ...clear reset control register and pull device out of
	 * warm reset */
	if (ath5k_hw_nic_reset(ah, 0)) {
		ATH5K_ERR(ah->ah_sc, "failed to warm reset the MAC Chip\n");
		return -EIO;
	}

	/* On initialization skip PLL programming since we don't have
	 * a channel / mode set yet */
	if (initial)
		return 0;

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

			if (ah->ah_radio == AR5K_RF5413)
				clock = AR5K_PHY_PLL_40MHZ_5413;
			else
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

	if (ah->ah_version != AR5K_AR5210) {

		/* ...update PLL if needed */
		if (ath5k_hw_reg_read(ah, AR5K_PHY_PLL) != clock) {
			ath5k_hw_reg_write(ah, clock, AR5K_PHY_PLL);
			udelay(300);
		}

		/* ...set the PHY operating mode */
		ath5k_hw_reg_write(ah, mode, AR5K_PHY_MODE);
		ath5k_hw_reg_write(ah, turbo, AR5K_PHY_TURBO);
	}

	return 0;
}

/*
 * If there is an external 32KHz crystal available, use it
 * as ref. clock instead of 32/40MHz clock and baseband clocks
 * to save power during sleep or restore normal 32/40MHz
 * operation.
 *
 * XXX: When operating on 32KHz certain PHY registers (27 - 31,
 * 	123 - 127) require delay on access.
 */
static void ath5k_hw_set_sleep_clock(struct ath5k_hw *ah, bool enable)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 scal, spending, usec32;

	/* Only set 32KHz settings if we have an external
	 * 32KHz crystal present */
	if ((AR5K_EEPROM_HAS32KHZCRYSTAL(ee->ee_misc1) ||
	AR5K_EEPROM_HAS32KHZCRYSTAL_OLD(ee->ee_misc1)) &&
	enable) {

		/* 1 usec/cycle */
		AR5K_REG_WRITE_BITS(ah, AR5K_USEC_5211, AR5K_USEC_32, 1);
		/* Set up tsf increment on each cycle */
		AR5K_REG_WRITE_BITS(ah, AR5K_TSF_PARM, AR5K_TSF_PARM_INC, 61);

		/* Set baseband sleep control registers
		 * and sleep control rate */
		ath5k_hw_reg_write(ah, 0x1f, AR5K_PHY_SCR);

		if ((ah->ah_radio == AR5K_RF5112) ||
		(ah->ah_radio == AR5K_RF5413) ||
		(ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4)))
			spending = 0x14;
		else
			spending = 0x18;
		ath5k_hw_reg_write(ah, spending, AR5K_PHY_SPENDING);

		if ((ah->ah_radio == AR5K_RF5112) ||
		(ah->ah_radio == AR5K_RF5413) ||
		(ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4))) {
			ath5k_hw_reg_write(ah, 0x26, AR5K_PHY_SLMT);
			ath5k_hw_reg_write(ah, 0x0d, AR5K_PHY_SCAL);
			ath5k_hw_reg_write(ah, 0x07, AR5K_PHY_SCLOCK);
			ath5k_hw_reg_write(ah, 0x3f, AR5K_PHY_SDELAY);
			AR5K_REG_WRITE_BITS(ah, AR5K_PCICFG,
				AR5K_PCICFG_SLEEP_CLOCK_RATE, 0x02);
		} else {
			ath5k_hw_reg_write(ah, 0x0a, AR5K_PHY_SLMT);
			ath5k_hw_reg_write(ah, 0x0c, AR5K_PHY_SCAL);
			ath5k_hw_reg_write(ah, 0x03, AR5K_PHY_SCLOCK);
			ath5k_hw_reg_write(ah, 0x20, AR5K_PHY_SDELAY);
			AR5K_REG_WRITE_BITS(ah, AR5K_PCICFG,
				AR5K_PCICFG_SLEEP_CLOCK_RATE, 0x03);
		}

		/* Enable sleep clock operation */
		AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG,
				AR5K_PCICFG_SLEEP_CLOCK_EN);

	} else {

		/* Disable sleep clock operation and
		 * restore default parameters */
		AR5K_REG_DISABLE_BITS(ah, AR5K_PCICFG,
				AR5K_PCICFG_SLEEP_CLOCK_EN);

		AR5K_REG_WRITE_BITS(ah, AR5K_PCICFG,
				AR5K_PCICFG_SLEEP_CLOCK_RATE, 0);

		ath5k_hw_reg_write(ah, 0x1f, AR5K_PHY_SCR);
		ath5k_hw_reg_write(ah, AR5K_PHY_SLMT_32MHZ, AR5K_PHY_SLMT);

		if (ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4))
			scal = AR5K_PHY_SCAL_32MHZ_2417;
		else if (ee->ee_is_hb63)
			scal = AR5K_PHY_SCAL_32MHZ_HB63;
		else
			scal = AR5K_PHY_SCAL_32MHZ;
		ath5k_hw_reg_write(ah, scal, AR5K_PHY_SCAL);

		ath5k_hw_reg_write(ah, AR5K_PHY_SCLOCK_32MHZ, AR5K_PHY_SCLOCK);
		ath5k_hw_reg_write(ah, AR5K_PHY_SDELAY_32MHZ, AR5K_PHY_SDELAY);

		if ((ah->ah_radio == AR5K_RF5112) ||
		(ah->ah_radio == AR5K_RF5413) ||
		(ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4)))
			spending = 0x14;
		else
			spending = 0x18;
		ath5k_hw_reg_write(ah, spending, AR5K_PHY_SPENDING);

		if ((ah->ah_radio == AR5K_RF5112) ||
		(ah->ah_radio == AR5K_RF5413))
			usec32 = 39;
		else
			usec32 = 31;
		AR5K_REG_WRITE_BITS(ah, AR5K_USEC_5211, AR5K_USEC_32, usec32);

		AR5K_REG_WRITE_BITS(ah, AR5K_TSF_PARM, AR5K_TSF_PARM_INC, 1);
	}
}

/* TODO: Half/Quarter rate */
static void ath5k_hw_tweak_initval_settings(struct ath5k_hw *ah,
				struct ieee80211_channel *channel)
{
	if (ah->ah_version == AR5K_AR5212 &&
	    ah->ah_phy_revision >= AR5K_SREV_PHY_5212A) {

		/* Setup ADC control */
		ath5k_hw_reg_write(ah,
				(AR5K_REG_SM(2,
				AR5K_PHY_ADC_CTL_INBUFGAIN_OFF) |
				AR5K_REG_SM(2,
				AR5K_PHY_ADC_CTL_INBUFGAIN_ON) |
				AR5K_PHY_ADC_CTL_PWD_DAC_OFF |
				AR5K_PHY_ADC_CTL_PWD_ADC_OFF),
				AR5K_PHY_ADC_CTL);



		/* Disable barker RSSI threshold */
		AR5K_REG_DISABLE_BITS(ah, AR5K_PHY_DAG_CCK_CTL,
				AR5K_PHY_DAG_CCK_CTL_EN_RSSI_THR);

		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_DAG_CCK_CTL,
			AR5K_PHY_DAG_CCK_CTL_RSSI_THR, 2);

		/* Set the mute mask */
		ath5k_hw_reg_write(ah, 0x0000000f, AR5K_SEQ_MASK);
	}

	/* Clear PHY_BLUETOOTH to allow RX_CLEAR line debug */
	if (ah->ah_phy_revision >= AR5K_SREV_PHY_5212B)
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_BLUETOOTH);

	/* Enable DCU double buffering */
	if (ah->ah_phy_revision > AR5K_SREV_PHY_5212B)
		AR5K_REG_DISABLE_BITS(ah, AR5K_TXCFG,
				AR5K_TXCFG_DCU_DBL_BUF_DIS);

	/* Set DAC/ADC delays */
	if (ah->ah_version == AR5K_AR5212) {
		u32 scal;
		struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
		if (ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4))
			scal = AR5K_PHY_SCAL_32MHZ_2417;
		else if (ee->ee_is_hb63)
			scal = AR5K_PHY_SCAL_32MHZ_HB63;
		else
			scal = AR5K_PHY_SCAL_32MHZ;
		ath5k_hw_reg_write(ah, scal, AR5K_PHY_SCAL);
	}

	/* Set fast ADC */
	if ((ah->ah_radio == AR5K_RF5413) ||
	(ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4))) {
		u32 fast_adc = true;

		if (channel->center_freq == 2462 ||
		channel->center_freq == 2467)
			fast_adc = 0;

		/* Only update if needed */
		if (ath5k_hw_reg_read(ah, AR5K_PHY_FAST_ADC) != fast_adc)
				ath5k_hw_reg_write(ah, fast_adc,
						AR5K_PHY_FAST_ADC);
	}

	/* Fix for first revision of the RF5112 RF chipset */
	if (ah->ah_radio == AR5K_RF5112 &&
			ah->ah_radio_5ghz_revision <
			AR5K_SREV_RAD_5112A) {
		u32 data;
		ath5k_hw_reg_write(ah, AR5K_PHY_CCKTXCTL_WORLD,
				AR5K_PHY_CCKTXCTL);
		if (channel->hw_value & CHANNEL_5GHZ)
			data = 0xffb81020;
		else
			data = 0xffb80d20;
		ath5k_hw_reg_write(ah, data, AR5K_PHY_FRAME_CTL);
	}

	if (ah->ah_mac_srev < AR5K_SREV_AR5211) {
		u32 usec_reg;
		/* 5311 has different tx/rx latency masks
		 * from 5211, since we deal 5311 the same
		 * as 5211 when setting initvals, shift
		 * values here to their proper locations */
		usec_reg = ath5k_hw_reg_read(ah, AR5K_USEC_5211);
		ath5k_hw_reg_write(ah, usec_reg & (AR5K_USEC_1 |
				AR5K_USEC_32 |
				AR5K_USEC_TX_LATENCY_5211 |
				AR5K_REG_SM(29,
				AR5K_USEC_RX_LATENCY_5210)),
				AR5K_USEC_5211);
		/* Clear QCU/DCU clock gating register */
		ath5k_hw_reg_write(ah, 0, AR5K_QCUDCU_CLKGT);
		/* Set DAC/ADC delays */
		ath5k_hw_reg_write(ah, 0x08, AR5K_PHY_SCAL);
		/* Enable PCU FIFO corruption ECO */
		AR5K_REG_ENABLE_BITS(ah, AR5K_DIAG_SW_5211,
					AR5K_DIAG_SW_ECO_ENABLE);
	}
}

static void ath5k_hw_commit_eeprom_settings(struct ath5k_hw *ah,
		struct ieee80211_channel *channel, u8 ee_mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	s16 cck_ofdm_pwr_delta;

	/* Adjust power delta for channel 14 */
	if (channel->center_freq == 2484)
		cck_ofdm_pwr_delta =
			((ee->ee_cck_ofdm_power_delta -
			ee->ee_scaled_cck_delta) * 2) / 10;
	else
		cck_ofdm_pwr_delta =
			(ee->ee_cck_ofdm_power_delta * 2) / 10;

	/* Set CCK to OFDM power delta on tx power
	 * adjustment register */
	if (ah->ah_phy_revision >= AR5K_SREV_PHY_5212A) {
		if (channel->hw_value == CHANNEL_G)
			ath5k_hw_reg_write(ah,
			AR5K_REG_SM((ee->ee_cck_ofdm_gain_delta * -1),
				AR5K_PHY_TX_PWR_ADJ_CCK_GAIN_DELTA) |
			AR5K_REG_SM((cck_ofdm_pwr_delta * -1),
				AR5K_PHY_TX_PWR_ADJ_CCK_PCDAC_INDEX),
				AR5K_PHY_TX_PWR_ADJ);
		else
			ath5k_hw_reg_write(ah, 0, AR5K_PHY_TX_PWR_ADJ);
	} else {
		/* For older revs we scale power on sw during tx power
		 * setup */
		ah->ah_txpower.txp_cck_ofdm_pwr_delta = cck_ofdm_pwr_delta;
		ah->ah_txpower.txp_cck_ofdm_gainf_delta =
						ee->ee_cck_ofdm_gain_delta;
	}

	/* XXX: necessary here? is called from ath5k_hw_set_antenna_mode()
	 * too */
	ath5k_hw_set_antenna_switch(ah, ee_mode);

	/* Noise floor threshold */
	ath5k_hw_reg_write(ah,
		AR5K_PHY_NF_SVAL(ee->ee_noise_floor_thr[ee_mode]),
		AR5K_PHY_NFTHRES);

	if ((channel->hw_value & CHANNEL_TURBO) &&
	(ah->ah_ee_version >= AR5K_EEPROM_VERSION_5_0)) {
		/* Switch settling time (Turbo) */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_SETTLING,
				AR5K_PHY_SETTLING_SWITCH,
				ee->ee_switch_settling_turbo[ee_mode]);

		/* Tx/Rx attenuation (Turbo) */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_GAIN,
				AR5K_PHY_GAIN_TXRX_ATTEN,
				ee->ee_atn_tx_rx_turbo[ee_mode]);

		/* ADC/PGA desired size (Turbo) */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_DESIRED_SIZE,
				AR5K_PHY_DESIRED_SIZE_ADC,
				ee->ee_adc_desired_size_turbo[ee_mode]);

		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_DESIRED_SIZE,
				AR5K_PHY_DESIRED_SIZE_PGA,
				ee->ee_pga_desired_size_turbo[ee_mode]);

		/* Tx/Rx margin (Turbo) */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_GAIN_2GHZ,
				AR5K_PHY_GAIN_2GHZ_MARGIN_TXRX,
				ee->ee_margin_tx_rx_turbo[ee_mode]);

	} else {
		/* Switch settling time */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_SETTLING,
				AR5K_PHY_SETTLING_SWITCH,
				ee->ee_switch_settling[ee_mode]);

		/* Tx/Rx attenuation */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_GAIN,
				AR5K_PHY_GAIN_TXRX_ATTEN,
				ee->ee_atn_tx_rx[ee_mode]);

		/* ADC/PGA desired size */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_DESIRED_SIZE,
				AR5K_PHY_DESIRED_SIZE_ADC,
				ee->ee_adc_desired_size[ee_mode]);

		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_DESIRED_SIZE,
				AR5K_PHY_DESIRED_SIZE_PGA,
				ee->ee_pga_desired_size[ee_mode]);

		/* Tx/Rx margin */
		if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_1)
			AR5K_REG_WRITE_BITS(ah, AR5K_PHY_GAIN_2GHZ,
				AR5K_PHY_GAIN_2GHZ_MARGIN_TXRX,
				ee->ee_margin_tx_rx[ee_mode]);
	}

	/* XPA delays */
	ath5k_hw_reg_write(ah,
		(ee->ee_tx_end2xpa_disable[ee_mode] << 24) |
		(ee->ee_tx_end2xpa_disable[ee_mode] << 16) |
		(ee->ee_tx_frm2xpa_enable[ee_mode] << 8) |
		(ee->ee_tx_frm2xpa_enable[ee_mode]), AR5K_PHY_RF_CTL4);

	/* XLNA delay */
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_RF_CTL3,
			AR5K_PHY_RF_CTL3_TXE2XLNA_ON,
			ee->ee_tx_end2xlna_enable[ee_mode]);

	/* Thresh64 (ANI) */
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_NF,
			AR5K_PHY_NF_THRESH62,
			ee->ee_thr_62[ee_mode]);

	/* False detect backoff for channels
	 * that have spur noise. Write the new
	 * cyclic power RSSI threshold. */
	if (ath5k_hw_chan_has_spur_noise(ah, channel))
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_OFDM_SELFCORR,
				AR5K_PHY_OFDM_SELFCORR_CYPWR_THR1,
				AR5K_INIT_CYCRSSI_THR1 +
				ee->ee_false_detect[ee_mode]);
	else
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_OFDM_SELFCORR,
				AR5K_PHY_OFDM_SELFCORR_CYPWR_THR1,
				AR5K_INIT_CYCRSSI_THR1);

	/* I/Q correction (set enable bit last to match HAL sources) */
	/* TODO: Per channel i/q infos ? */
	if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_0) {
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_IQ, AR5K_PHY_IQ_CORR_Q_I_COFF,
			    ee->ee_i_cal[ee_mode]);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_IQ, AR5K_PHY_IQ_CORR_Q_Q_COFF,
			    ee->ee_q_cal[ee_mode]);
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_IQ, AR5K_PHY_IQ_CORR_ENABLE);
	}

	/* Heavy clipping -disable for now */
	if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_5_1)
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_HEAVY_CLIP_ENABLE);
}

/*
 * Main reset function
 */
int ath5k_hw_reset(struct ath5k_hw *ah, enum nl80211_iftype op_mode,
	struct ieee80211_channel *channel, bool change_channel)
{
	struct ath_common *common = ath5k_hw_common(ah);
	u32 s_seq[10], s_led[3], staid1_flags, tsf_up, tsf_lo;
	u32 phy_tst1;
	u8 mode, freq, ee_mode;
	int i, ret;

	ee_mode = 0;
	staid1_flags = 0;
	tsf_up = 0;
	tsf_lo = 0;
	freq = 0;
	mode = 0;

	/*
	 * Save some registers before a reset
	 */
	/*DCU/Antenna selection not available on 5210*/
	if (ah->ah_version != AR5K_AR5210) {

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
		case CHANNEL_TG:
			if (ah->ah_version == AR5K_AR5211) {
				ATH5K_ERR(ah->ah_sc,
					"TurboG mode not available on 5211");
				return -EINVAL;
			}
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

		if (change_channel) {
			/*
			 * Save frame sequence count
			 * For revs. after Oahu, only save
			 * seq num for DCU 0 (Global seq num)
			 */
			if (ah->ah_mac_srev < AR5K_SREV_AR5211) {

				for (i = 0; i < 10; i++)
					s_seq[i] = ath5k_hw_reg_read(ah,
						AR5K_QUEUE_DCU_SEQNUM(i));

			} else {
				s_seq[0] = ath5k_hw_reg_read(ah,
						AR5K_QUEUE_DCU_SEQNUM(0));
			}

			/* TSF accelerates on AR5211 durring reset
			 * As a workaround save it here and restore
			 * it later so that it's back in time after
			 * reset. This way it'll get re-synced on the
			 * next beacon without breaking ad-hoc.
			 *
			 * On AR5212 TSF is almost preserved across a
			 * reset so it stays back in time anyway and
			 * we don't have to save/restore it.
			 *
			 * XXX: Since this breaks power saving we have
			 * to disable power saving until we receive the
			 * next beacon, so we can resync beacon timers */
			if (ah->ah_version == AR5K_AR5211) {
				tsf_up = ath5k_hw_reg_read(ah, AR5K_TSF_U32);
				tsf_lo = ath5k_hw_reg_read(ah, AR5K_TSF_L32);
			}
		}

		if (ah->ah_version == AR5K_AR5212) {
			/* Restore normal 32/40MHz clock operation
			 * to avoid register access delay on certain
			 * PHY registers */
			ath5k_hw_set_sleep_clock(ah, false);

			/* Since we are going to write rf buffer
			 * check if we have any pending gain_F
			 * optimization settings */
			if (change_channel && ah->ah_rf_banks != NULL)
				ath5k_hw_gainf_calibrate(ah);
		}
	}

	/*GPIOs*/
	s_led[0] = ath5k_hw_reg_read(ah, AR5K_PCICFG) &
					AR5K_PCICFG_LEDSTATE;
	s_led[1] = ath5k_hw_reg_read(ah, AR5K_GPIOCR);
	s_led[2] = ath5k_hw_reg_read(ah, AR5K_GPIODO);

	/* AR5K_STA_ID1 flags, only preserve antenna
	 * settings and ack/cts rate mode */
	staid1_flags = ath5k_hw_reg_read(ah, AR5K_STA_ID1) &
			(AR5K_STA_ID1_DEFAULT_ANTENNA |
			AR5K_STA_ID1_DESC_ANTENNA |
			AR5K_STA_ID1_RTS_DEF_ANTENNA |
			AR5K_STA_ID1_ACKCTS_6MB |
			AR5K_STA_ID1_BASE_RATE_11B |
			AR5K_STA_ID1_SELFGEN_DEF_ANT);

	/* Wakeup the device */
	ret = ath5k_hw_nic_wakeup(ah, channel->hw_value, false);
	if (ret)
		return ret;

	/* PHY access enable */
	if (ah->ah_mac_srev >= AR5K_SREV_AR5211)
		ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_5GHZ, AR5K_PHY(0));
	else
		ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_5GHZ | 0x40,
							AR5K_PHY(0));

	/* Write initial settings */
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
		ret = ath5k_hw_rfgain_init(ah, freq);
		if (ret)
			return ret;

		mdelay(1);

		/*
		 * Tweak initval settings for revised
		 * chipsets and add some more config
		 * bits
		 */
		ath5k_hw_tweak_initval_settings(ah, channel);

		/*
		 * Set TX power
		 */
		ret = ath5k_hw_txpower(ah, channel, ee_mode,
					ah->ah_txpower.txp_max_pwr / 2);
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
		 * Write RF buffer
		 */
		ret = ath5k_hw_rfregs_init(ah, channel, mode);
		if (ret)
			return ret;


		/* Write OFDM timings on 5212*/
		if (ah->ah_version == AR5K_AR5212 &&
			channel->hw_value & CHANNEL_OFDM) {

			ret = ath5k_hw_write_ofdm_timings(ah, channel);
			if (ret)
				return ret;

			/* Spur info is available only from EEPROM versions
			 * bigger than 5.3 but but the EEPOM routines will use
			 * static values for older versions */
			if (ah->ah_mac_srev >= AR5K_SREV_AR5424)
				ath5k_hw_set_spur_mitigation_filter(ah,
								    channel);
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

		/* Commit values from EEPROM */
		ath5k_hw_commit_eeprom_settings(ah, channel, ee_mode);

	} else {
		/*
		 * For 5210 we do all initialization using
		 * initvals, so we don't have to modify
		 * any settings (5210 also only supports
		 * a/aturbo modes)
		 */
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

		if (change_channel) {
			if (ah->ah_mac_srev < AR5K_SREV_AR5211) {
				for (i = 0; i < 10; i++)
					ath5k_hw_reg_write(ah, s_seq[i],
						AR5K_QUEUE_DCU_SEQNUM(i));
			} else {
				ath5k_hw_reg_write(ah, s_seq[0],
					AR5K_QUEUE_DCU_SEQNUM(0));
			}


			if (ah->ah_version == AR5K_AR5211) {
				ath5k_hw_reg_write(ah, tsf_up, AR5K_TSF_U32);
				ath5k_hw_reg_write(ah, tsf_lo, AR5K_TSF_L32);
			}
		}
	}

	/* Ledstate */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, s_led[0]);

	/* Gpio settings */
	ath5k_hw_reg_write(ah, s_led[1], AR5K_GPIOCR);
	ath5k_hw_reg_write(ah, s_led[2], AR5K_GPIODO);

	/* Restore sta_id flags and preserve our mac address*/
	ath5k_hw_reg_write(ah,
			   get_unaligned_le32(common->macaddr),
			   AR5K_STA_ID0);
	ath5k_hw_reg_write(ah,
			   staid1_flags | get_unaligned_le16(common->macaddr + 4),
			   AR5K_STA_ID1);


	/*
	 * Configure PCU
	 */

	/* Restore bssid and bssid mask */
	ath5k_hw_set_associd(ah);

	/* Set PCU config */
	ath5k_hw_set_opmode(ah, op_mode);

	/* Clear any pending interrupts
	 * PISR/SISR Not available on 5210 */
	if (ah->ah_version != AR5K_AR5210)
		ath5k_hw_reg_write(ah, 0xffffffff, AR5K_PISR);

	/* Set RSSI/BRSSI thresholds
	 *
	 * Note: If we decide to set this value
	 * dynamicaly, have in mind that when AR5K_RSSI_THR
	 * register is read it might return 0x40 if we haven't
	 * wrote anything to it plus BMISS RSSI threshold is zeroed.
	 * So doing a save/restore procedure here isn't the right
	 * choice. Instead store it on ath5k_hw */
	ath5k_hw_reg_write(ah, (AR5K_TUNE_RSSI_THRES |
				AR5K_TUNE_BMISS_THRES <<
				AR5K_RSSI_THR_BMISS_S),
				AR5K_RSSI_THR);

	/* MIC QoS support */
	if (ah->ah_mac_srev >= AR5K_SREV_AR2413) {
		ath5k_hw_reg_write(ah, 0x000100aa, AR5K_MIC_QOS_CTL);
		ath5k_hw_reg_write(ah, 0x00003210, AR5K_MIC_QOS_SEL);
	}

	/* QoS NOACK Policy */
	if (ah->ah_version == AR5K_AR5212) {
		ath5k_hw_reg_write(ah,
			AR5K_REG_SM(2, AR5K_QOS_NOACK_2BIT_VALUES) |
			AR5K_REG_SM(5, AR5K_QOS_NOACK_BIT_OFFSET)  |
			AR5K_REG_SM(0, AR5K_QOS_NOACK_BYTE_OFFSET),
			AR5K_QOS_NOACK);
	}


	/*
	 * Configure PHY
	 */

	/* Set channel on PHY */
	ret = ath5k_hw_channel(ah, channel);
	if (ret)
		return ret;

	/*
	 * Enable the PHY and wait until completion
	 * This includes BaseBand and Synthesizer
	 * activation.
	 */
	ath5k_hw_reg_write(ah, AR5K_PHY_ACT_ENABLE, AR5K_PHY_ACT);

	/*
	 * On 5211+ read activation -> rx delay
	 * and use it.
	 *
	 * TODO: Half/quarter rate support
	 */
	if (ah->ah_version != AR5K_AR5210) {
		u32 delay;
		delay = ath5k_hw_reg_read(ah, AR5K_PHY_RX_DELAY) &
			AR5K_PHY_RX_DELAY_M;
		delay = (channel->hw_value & CHANNEL_CCK) ?
			((delay << 2) / 22) : (delay / 10);

		udelay(100 + (2 * delay));
	} else {
		mdelay(1);
	}

	/*
	 * Perform ADC test to see if baseband is ready
	 * Set tx hold and check adc test register
	 */
	phy_tst1 = ath5k_hw_reg_read(ah, AR5K_PHY_TST1);
	ath5k_hw_reg_write(ah, AR5K_PHY_TST1_TXHOLD, AR5K_PHY_TST1);
	for (i = 0; i <= 20; i++) {
		if (!(ath5k_hw_reg_read(ah, AR5K_PHY_ADC_TEST) & 0x10))
			break;
		udelay(200);
	}
	ath5k_hw_reg_write(ah, phy_tst1, AR5K_PHY_TST1);

	/*
	 * Start automatic gain control calibration
	 *
	 * During AGC calibration RX path is re-routed to
	 * a power detector so we don't receive anything.
	 *
	 * This method is used to calibrate some static offsets
	 * used together with on-the fly I/Q calibration (the
	 * one performed via ath5k_hw_phy_calibrate), that doesn't
	 * interrupt rx path.
	 *
	 * While rx path is re-routed to the power detector we also
	 * start a noise floor calibration, to measure the
	 * card's noise floor (the noise we measure when we are not
	 * transmiting or receiving anything).
	 *
	 * If we are in a noisy environment AGC calibration may time
	 * out and/or noise floor calibration might timeout.
	 */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
				AR5K_PHY_AGCCTL_CAL | AR5K_PHY_AGCCTL_NF);

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
	}

	/* Restore antenna mode */
	ath5k_hw_set_antenna_mode(ah, ah->ah_ant_mode);

	/* Restore slot time and ACK timeouts */
	if (ah->ah_coverage_class > 0)
		ath5k_hw_set_coverage_class(ah, ah->ah_coverage_class);

	/*
	 * Configure QCUs/DCUs
	 */

	/* TODO: HW Compression support for data queues */
	/* TODO: Burst prefetch for data queues */

	/*
	 * Reset queues and start beacon timers at the end of the reset routine
	 * This also sets QCU mask on each DCU for 1:1 qcu to dcu mapping
	 * Note: If we want we can assign multiple qcus on one dcu.
	 */
	for (i = 0; i < ah->ah_capabilities.cap_queues.q_tx_num; i++) {
		ret = ath5k_hw_reset_tx_queue(ah, i);
		if (ret) {
			ATH5K_ERR(ah->ah_sc,
				"failed to reset TX queue #%d\n", i);
			return ret;
		}
	}


	/*
	 * Configure DMA/Interrupts
	 */

	/*
	 * Set Rx/Tx DMA Configuration
	 *
	 * Set standard DMA size (128). Note that
	 * a DMA size of 512 causes rx overruns and tx errors
	 * on pci-e cards (tested on 5424 but since rx overruns
	 * also occur on 5416/5418 with madwifi we set 128
	 * for all PCI-E cards to be safe).
	 *
	 * XXX: need to check 5210 for this
	 * TODO: Check out tx triger level, it's always 64 on dumps but I
	 * guess we can tweak it and see how it goes ;-)
	 */
	if (ah->ah_version != AR5K_AR5210) {
		AR5K_REG_WRITE_BITS(ah, AR5K_TXCFG,
			AR5K_TXCFG_SDMAMR, AR5K_DMASIZE_128B);
		AR5K_REG_WRITE_BITS(ah, AR5K_RXCFG,
			AR5K_RXCFG_SDMAMW, AR5K_DMASIZE_128B);
	}

	/* Pre-enable interrupts on 5211/5212*/
	if (ah->ah_version != AR5K_AR5210)
		ath5k_hw_set_imr(ah, ah->ah_imr);

	/* Enable 32KHz clock function for AR5212+ chips
	 * Set clocks to 32KHz operation and use an
	 * external 32KHz crystal when sleeping if one
	 * exists */
	if (ah->ah_version == AR5K_AR5212 &&
	    op_mode != NL80211_IFTYPE_AP)
		ath5k_hw_set_sleep_clock(ah, true);

	/*
	 * Disable beacons and reset the TSF
	 */
	AR5K_REG_DISABLE_BITS(ah, AR5K_BEACON, AR5K_BEACON_ENABLE);
	ath5k_hw_reset_tsf(ah);
	return 0;
}
