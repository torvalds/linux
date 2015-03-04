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

/****************************\
  Reset function and helpers
\****************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/unaligned.h>

#include <linux/pci.h>		/* To determine if a card is pci-e */
#include <linux/log2.h>
#include <linux/platform_device.h>
#include "ath5k.h"
#include "reg.h"
#include "debug.h"


/**
 * DOC: Reset function and helpers
 *
 * Here we implement the main reset routine, used to bring the card
 * to a working state and ready to receive. We also handle routines
 * that don't fit on other places such as clock, sleep and power control
 */


/******************\
* Helper functions *
\******************/

/**
 * ath5k_hw_register_timeout() - Poll a register for a flag/field change
 * @ah: The &struct ath5k_hw
 * @reg: The register to read
 * @flag: The flag/field to check on the register
 * @val: The field value we expect (if we check a field)
 * @is_set: Instead of checking if the flag got cleared, check if it got set
 *
 * Some registers contain flags that indicate that an operation is
 * running. We use this function to poll these registers and check
 * if these flags get cleared. We also use it to poll a register
 * field (containing multiple flags) until it gets a specific value.
 *
 * Returns -EAGAIN if we exceeded AR5K_TUNE_REGISTER_TIMEOUT * 15us or 0
 */
int
ath5k_hw_register_timeout(struct ath5k_hw *ah, u32 reg, u32 flag, u32 val,
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


/*************************\
* Clock related functions *
\*************************/

/**
 * ath5k_hw_htoclock() - Translate usec to hw clock units
 * @ah: The &struct ath5k_hw
 * @usec: value in microseconds
 *
 * Translate usecs to hw clock units based on the current
 * hw clock rate.
 *
 * Returns number of clock units
 */
unsigned int
ath5k_hw_htoclock(struct ath5k_hw *ah, unsigned int usec)
{
	struct ath_common *common = ath5k_hw_common(ah);
	return usec * common->clockrate;
}

/**
 * ath5k_hw_clocktoh() - Translate hw clock units to usec
 * @ah: The &struct ath5k_hw
 * @clock: value in hw clock units
 *
 * Translate hw clock units to usecs based on the current
 * hw clock rate.
 *
 * Returns number of usecs
 */
unsigned int
ath5k_hw_clocktoh(struct ath5k_hw *ah, unsigned int clock)
{
	struct ath_common *common = ath5k_hw_common(ah);
	return clock / common->clockrate;
}

/**
 * ath5k_hw_init_core_clock() - Initialize core clock
 * @ah: The &struct ath5k_hw
 *
 * Initialize core clock parameters (usec, usec32, latencies etc),
 * based on current bwmode and chipset properties.
 */
static void
ath5k_hw_init_core_clock(struct ath5k_hw *ah)
{
	struct ieee80211_channel *channel = ah->ah_current_channel;
	struct ath_common *common = ath5k_hw_common(ah);
	u32 usec_reg, txlat, rxlat, usec, clock, sclock, txf2txs;

	/*
	 * Set core clock frequency
	 */
	switch (channel->hw_value) {
	case AR5K_MODE_11A:
		clock = 40;
		break;
	case AR5K_MODE_11B:
		clock = 22;
		break;
	case AR5K_MODE_11G:
	default:
		clock = 44;
		break;
	}

	/* Use clock multiplier for non-default
	 * bwmode */
	switch (ah->ah_bwmode) {
	case AR5K_BWMODE_40MHZ:
		clock *= 2;
		break;
	case AR5K_BWMODE_10MHZ:
		clock /= 2;
		break;
	case AR5K_BWMODE_5MHZ:
		clock /= 4;
		break;
	default:
		break;
	}

	common->clockrate = clock;

	/*
	 * Set USEC parameters
	 */
	/* Set USEC counter on PCU*/
	usec = clock - 1;
	usec = AR5K_REG_SM(usec, AR5K_USEC_1);

	/* Set usec duration on DCU */
	if (ah->ah_version != AR5K_AR5210)
		AR5K_REG_WRITE_BITS(ah, AR5K_DCU_GBL_IFS_MISC,
					AR5K_DCU_GBL_IFS_MISC_USEC_DUR,
					clock);

	/* Set 32MHz USEC counter */
	if ((ah->ah_radio == AR5K_RF5112) ||
	    (ah->ah_radio == AR5K_RF2413) ||
	    (ah->ah_radio == AR5K_RF5413) ||
	    (ah->ah_radio == AR5K_RF2316) ||
	    (ah->ah_radio == AR5K_RF2317))
		/* Remain on 40MHz clock ? */
		sclock = 40 - 1;
	else
		sclock = 32 - 1;
	sclock = AR5K_REG_SM(sclock, AR5K_USEC_32);

	/*
	 * Set tx/rx latencies
	 */
	usec_reg = ath5k_hw_reg_read(ah, AR5K_USEC_5211);
	txlat = AR5K_REG_MS(usec_reg, AR5K_USEC_TX_LATENCY_5211);
	rxlat = AR5K_REG_MS(usec_reg, AR5K_USEC_RX_LATENCY_5211);

	/*
	 * Set default Tx frame to Tx data start delay
	 */
	txf2txs = AR5K_INIT_TXF2TXD_START_DEFAULT;

	/*
	 * 5210 initvals don't include usec settings
	 * so we need to use magic values here for
	 * tx/rx latencies
	 */
	if (ah->ah_version == AR5K_AR5210) {
		/* same for turbo */
		txlat = AR5K_INIT_TX_LATENCY_5210;
		rxlat = AR5K_INIT_RX_LATENCY_5210;
	}

	if (ah->ah_mac_srev < AR5K_SREV_AR5211) {
		/* 5311 has different tx/rx latency masks
		 * from 5211, since we deal 5311 the same
		 * as 5211 when setting initvals, shift
		 * values here to their proper locations
		 *
		 * Note: Initvals indicate tx/rx/ latencies
		 * are the same for turbo mode */
		txlat = AR5K_REG_SM(txlat, AR5K_USEC_TX_LATENCY_5210);
		rxlat = AR5K_REG_SM(rxlat, AR5K_USEC_RX_LATENCY_5210);
	} else
	switch (ah->ah_bwmode) {
	case AR5K_BWMODE_10MHZ:
		txlat = AR5K_REG_SM(txlat * 2,
				AR5K_USEC_TX_LATENCY_5211);
		rxlat = AR5K_REG_SM(AR5K_INIT_RX_LAT_MAX,
				AR5K_USEC_RX_LATENCY_5211);
		txf2txs = AR5K_INIT_TXF2TXD_START_DELAY_10MHZ;
		break;
	case AR5K_BWMODE_5MHZ:
		txlat = AR5K_REG_SM(txlat * 4,
				AR5K_USEC_TX_LATENCY_5211);
		rxlat = AR5K_REG_SM(AR5K_INIT_RX_LAT_MAX,
				AR5K_USEC_RX_LATENCY_5211);
		txf2txs = AR5K_INIT_TXF2TXD_START_DELAY_5MHZ;
		break;
	case AR5K_BWMODE_40MHZ:
		txlat = AR5K_INIT_TX_LAT_MIN;
		rxlat = AR5K_REG_SM(rxlat / 2,
				AR5K_USEC_RX_LATENCY_5211);
		txf2txs = AR5K_INIT_TXF2TXD_START_DEFAULT;
		break;
	default:
		break;
	}

	usec_reg = (usec | sclock | txlat | rxlat);
	ath5k_hw_reg_write(ah, usec_reg, AR5K_USEC);

	/* On 5112 set tx frame to tx data start delay */
	if (ah->ah_radio == AR5K_RF5112) {
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_RF_CTL2,
					AR5K_PHY_RF_CTL2_TXF2TXD_START,
					txf2txs);
	}
}

/**
 * ath5k_hw_set_sleep_clock() - Setup sleep clock operation
 * @ah: The &struct ath5k_hw
 * @enable: Enable sleep clock operation (false to disable)
 *
 * If there is an external 32KHz crystal available, use it
 * as ref. clock instead of 32/40MHz clock and baseband clocks
 * to save power during sleep or restore normal 32/40MHz
 * operation.
 *
 * NOTE: When operating on 32KHz certain PHY registers (27 - 31,
 * 123 - 127) require delay on access.
 */
static void
ath5k_hw_set_sleep_clock(struct ath5k_hw *ah, bool enable)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 scal, spending, sclock;

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
		(ah->ah_radio == AR5K_RF2316) ||
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

		/* Set DAC/ADC delays */
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
		(ah->ah_radio == AR5K_RF2316) ||
		(ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4)))
			spending = 0x14;
		else
			spending = 0x18;
		ath5k_hw_reg_write(ah, spending, AR5K_PHY_SPENDING);

		/* Set up tsf increment on each cycle */
		AR5K_REG_WRITE_BITS(ah, AR5K_TSF_PARM, AR5K_TSF_PARM_INC, 1);

		if ((ah->ah_radio == AR5K_RF5112) ||
			(ah->ah_radio == AR5K_RF5413) ||
			(ah->ah_radio == AR5K_RF2316) ||
			(ah->ah_radio == AR5K_RF2317))
			sclock = 40 - 1;
		else
			sclock = 32 - 1;
		AR5K_REG_WRITE_BITS(ah, AR5K_USEC_5211, AR5K_USEC_32, sclock);
	}
}


/*********************\
* Reset/Sleep control *
\*********************/

/**
 * ath5k_hw_nic_reset() - Reset the various chipset units
 * @ah: The &struct ath5k_hw
 * @val: Mask to indicate what units to reset
 *
 * To reset the various chipset units we need to write
 * the mask to AR5K_RESET_CTL and poll the register until
 * all flags are cleared.
 *
 * Returns 0 if we are O.K. or -EAGAIN (from athk5_hw_register_timeout)
 */
static int
ath5k_hw_nic_reset(struct ath5k_hw *ah, u32 val)
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
	usleep_range(15, 20);

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

/**
 * ath5k_hw_wisoc_reset() -  Reset AHB chipset
 * @ah: The &struct ath5k_hw
 * @flags: Mask to indicate what units to reset
 *
 * Same as ath5k_hw_nic_reset but for AHB based devices
 *
 * Returns 0 if we are O.K. or -EAGAIN (from athk5_hw_register_timeout)
 */
static int
ath5k_hw_wisoc_reset(struct ath5k_hw *ah, u32 flags)
{
	u32 mask = flags ? flags : ~0U;
	u32 __iomem *reg;
	u32 regval;
	u32 val = 0;

	/* ah->ah_mac_srev is not available at this point yet */
	if (ah->devid >= AR5K_SREV_AR2315_R6) {
		reg = (u32 __iomem *) AR5K_AR2315_RESET;
		if (mask & AR5K_RESET_CTL_PCU)
			val |= AR5K_AR2315_RESET_WMAC;
		if (mask & AR5K_RESET_CTL_BASEBAND)
			val |= AR5K_AR2315_RESET_BB_WARM;
	} else {
		reg = (u32 __iomem *) AR5K_AR5312_RESET;
		if (to_platform_device(ah->dev)->id == 0) {
			if (mask & AR5K_RESET_CTL_PCU)
				val |= AR5K_AR5312_RESET_WMAC0;
			if (mask & AR5K_RESET_CTL_BASEBAND)
				val |= AR5K_AR5312_RESET_BB0_COLD |
				       AR5K_AR5312_RESET_BB0_WARM;
		} else {
			if (mask & AR5K_RESET_CTL_PCU)
				val |= AR5K_AR5312_RESET_WMAC1;
			if (mask & AR5K_RESET_CTL_BASEBAND)
				val |= AR5K_AR5312_RESET_BB1_COLD |
				       AR5K_AR5312_RESET_BB1_WARM;
		}
	}

	/* Put BB/MAC into reset */
	regval = ioread32(reg);
	iowrite32(regval | val, reg);
	regval = ioread32(reg);
	udelay(100);	/* NB: should be atomic */

	/* Bring BB/MAC out of reset */
	iowrite32(regval & ~val, reg);
	regval = ioread32(reg);

	/*
	 * Reset configuration register (for hw byte-swap). Note that this
	 * is only set for big endian. We do the necessary magic in
	 * AR5K_INIT_CFG.
	 */
	if ((flags & AR5K_RESET_CTL_PCU) == 0)
		ath5k_hw_reg_write(ah, AR5K_INIT_CFG, AR5K_CFG);

	return 0;
}

/**
 * ath5k_hw_set_power_mode() - Set power mode
 * @ah: The &struct ath5k_hw
 * @mode: One of enum ath5k_power_mode
 * @set_chip: Set to true to write sleep control register
 * @sleep_duration: How much time the device is allowed to sleep
 * when sleep logic is enabled (in 128 microsecond increments).
 *
 * This function is used to configure sleep policy and allowed
 * sleep modes. For more information check out the sleep control
 * register on reg.h and STA_ID1.
 *
 * Returns 0 on success, -EIO if chip didn't wake up or -EINVAL if an invalid
 * mode is requested.
 */
static int
ath5k_hw_set_power_mode(struct ath5k_hw *ah, enum ath5k_power_mode mode,
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
		usleep_range(15, 20);

		for (i = 200; i > 0; i--) {
			/* Check if the chip did wake up */
			if ((ath5k_hw_reg_read(ah, AR5K_PCICFG) &
					AR5K_PCICFG_SPWR_DN) == 0)
				break;

			/* Wait a bit and retry */
			usleep_range(50, 75);
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

/**
 * ath5k_hw_on_hold() - Put device on hold
 * @ah: The &struct ath5k_hw
 *
 * Put MAC and Baseband on warm reset and keep that state
 * (don't clean sleep control register). After this MAC
 * and Baseband are disabled and a full reset is needed
 * to come back. This way we save as much power as possible
 * without putting the card on full sleep.
 *
 * Returns 0 on success or -EIO on error
 */
int
ath5k_hw_on_hold(struct ath5k_hw *ah)
{
	struct pci_dev *pdev = ah->pdev;
	u32 bus_flags;
	int ret;

	if (ath5k_get_bus_type(ah) == ATH_AHB)
		return 0;

	/* Make sure device is awake */
	ret = ath5k_hw_set_power_mode(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah, "failed to wakeup the MAC Chip\n");
		return ret;
	}

	/*
	 * Put chipset on warm reset...
	 *
	 * Note: putting PCI core on warm reset on PCI-E cards
	 * results card to hang and always return 0xffff... so
	 * we ignore that flag for PCI-E cards. On PCI cards
	 * this flag gets cleared after 64 PCI clocks.
	 */
	bus_flags = (pdev && pci_is_pcie(pdev)) ? 0 : AR5K_RESET_CTL_PCI;

	if (ah->ah_version == AR5K_AR5210) {
		ret = ath5k_hw_nic_reset(ah, AR5K_RESET_CTL_PCU |
			AR5K_RESET_CTL_MAC | AR5K_RESET_CTL_DMA |
			AR5K_RESET_CTL_PHY | AR5K_RESET_CTL_PCI);
			usleep_range(2000, 2500);
	} else {
		ret = ath5k_hw_nic_reset(ah, AR5K_RESET_CTL_PCU |
			AR5K_RESET_CTL_BASEBAND | bus_flags);
	}

	if (ret) {
		ATH5K_ERR(ah, "failed to put device on warm reset\n");
		return -EIO;
	}

	/* ...wakeup again!*/
	ret = ath5k_hw_set_power_mode(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah, "failed to put device on hold\n");
		return ret;
	}

	return ret;
}

/**
 * ath5k_hw_nic_wakeup() - Force card out of sleep
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * Bring up MAC + PHY Chips and program PLL
 * NOTE: Channel is NULL for the initial wakeup.
 *
 * Returns 0 on success, -EIO on hw failure or -EINVAL for false channel infos
 */
int
ath5k_hw_nic_wakeup(struct ath5k_hw *ah, struct ieee80211_channel *channel)
{
	struct pci_dev *pdev = ah->pdev;
	u32 turbo, mode, clock, bus_flags;
	int ret;

	turbo = 0;
	mode = 0;
	clock = 0;

	if ((ath5k_get_bus_type(ah) != ATH_AHB) || channel) {
		/* Wakeup the device */
		ret = ath5k_hw_set_power_mode(ah, AR5K_PM_AWAKE, true, 0);
		if (ret) {
			ATH5K_ERR(ah, "failed to wakeup the MAC Chip\n");
			return ret;
		}
	}

	/*
	 * Put chipset on warm reset...
	 *
	 * Note: putting PCI core on warm reset on PCI-E cards
	 * results card to hang and always return 0xffff... so
	 * we ignore that flag for PCI-E cards. On PCI cards
	 * this flag gets cleared after 64 PCI clocks.
	 */
	bus_flags = (pdev && pci_is_pcie(pdev)) ? 0 : AR5K_RESET_CTL_PCI;

	if (ah->ah_version == AR5K_AR5210) {
		ret = ath5k_hw_nic_reset(ah, AR5K_RESET_CTL_PCU |
			AR5K_RESET_CTL_MAC | AR5K_RESET_CTL_DMA |
			AR5K_RESET_CTL_PHY | AR5K_RESET_CTL_PCI);
			usleep_range(2000, 2500);
	} else {
		if (ath5k_get_bus_type(ah) == ATH_AHB)
			ret = ath5k_hw_wisoc_reset(ah, AR5K_RESET_CTL_PCU |
				AR5K_RESET_CTL_BASEBAND);
		else
			ret = ath5k_hw_nic_reset(ah, AR5K_RESET_CTL_PCU |
				AR5K_RESET_CTL_BASEBAND | bus_flags);
	}

	if (ret) {
		ATH5K_ERR(ah, "failed to reset the MAC Chip\n");
		return -EIO;
	}

	/* ...wakeup again!...*/
	ret = ath5k_hw_set_power_mode(ah, AR5K_PM_AWAKE, true, 0);
	if (ret) {
		ATH5K_ERR(ah, "failed to resume the MAC Chip\n");
		return ret;
	}

	/* ...reset configuration register on Wisoc ...
	 * ...clear reset control register and pull device out of
	 * warm reset on others */
	if (ath5k_get_bus_type(ah) == ATH_AHB)
		ret = ath5k_hw_wisoc_reset(ah, 0);
	else
		ret = ath5k_hw_nic_reset(ah, 0);

	if (ret) {
		ATH5K_ERR(ah, "failed to warm reset the MAC Chip\n");
		return -EIO;
	}

	/* On initialization skip PLL programming since we don't have
	 * a channel / mode set yet */
	if (!channel)
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

		if (channel->band == IEEE80211_BAND_2GHZ) {
			mode |= AR5K_PHY_MODE_FREQ_2GHZ;
			clock |= AR5K_PHY_PLL_44MHZ;

			if (channel->hw_value == AR5K_MODE_11B) {
				mode |= AR5K_PHY_MODE_MOD_CCK;
			} else {
				/* XXX Dynamic OFDM/CCK is not supported by the
				 * AR5211 so we set MOD_OFDM for plain g (no
				 * CCK headers) operation. We need to test
				 * this, 5211 might support ofdm-only g after
				 * all, there are also initial register values
				 * in the code for g mode (see initvals.c).
				 */
				if (ah->ah_version == AR5K_AR5211)
					mode |= AR5K_PHY_MODE_MOD_OFDM;
				else
					mode |= AR5K_PHY_MODE_MOD_DYN;
			}
		} else if (channel->band == IEEE80211_BAND_5GHZ) {
			mode |= (AR5K_PHY_MODE_FREQ_5GHZ |
				 AR5K_PHY_MODE_MOD_OFDM);

			/* Different PLL setting for 5413 */
			if (ah->ah_radio == AR5K_RF5413)
				clock = AR5K_PHY_PLL_40MHZ_5413;
			else
				clock |= AR5K_PHY_PLL_40MHZ;
		} else {
			ATH5K_ERR(ah, "invalid radio frequency mode\n");
			return -EINVAL;
		}

		/*XXX: Can bwmode be used with dynamic mode ?
		 * (I don't think it supports 44MHz) */
		/* On 2425 initvals TURBO_SHORT is not present */
		if (ah->ah_bwmode == AR5K_BWMODE_40MHZ) {
			turbo = AR5K_PHY_TURBO_MODE;
			if (ah->ah_radio != AR5K_RF2425)
				turbo |= AR5K_PHY_TURBO_SHORT;
		} else if (ah->ah_bwmode != AR5K_BWMODE_DEFAULT) {
			if (ah->ah_radio == AR5K_RF5413) {
				mode |= (ah->ah_bwmode == AR5K_BWMODE_10MHZ) ?
					AR5K_PHY_MODE_HALF_RATE :
					AR5K_PHY_MODE_QUARTER_RATE;
			} else if (ah->ah_version == AR5K_AR5212) {
				clock |= (ah->ah_bwmode == AR5K_BWMODE_10MHZ) ?
					AR5K_PHY_PLL_HALF_RATE :
					AR5K_PHY_PLL_QUARTER_RATE;
			}
		}

	} else { /* Reset the device */

		/* ...enable Atheros turbo mode if requested */
		if (ah->ah_bwmode == AR5K_BWMODE_40MHZ)
			ath5k_hw_reg_write(ah, AR5K_PHY_TURBO_MODE,
					AR5K_PHY_TURBO);
	}

	if (ah->ah_version != AR5K_AR5210) {

		/* ...update PLL if needed */
		if (ath5k_hw_reg_read(ah, AR5K_PHY_PLL) != clock) {
			ath5k_hw_reg_write(ah, clock, AR5K_PHY_PLL);
			usleep_range(300, 350);
		}

		/* ...set the PHY operating mode */
		ath5k_hw_reg_write(ah, mode, AR5K_PHY_MODE);
		ath5k_hw_reg_write(ah, turbo, AR5K_PHY_TURBO);
	}

	return 0;
}


/**************************************\
* Post-initvals register modifications *
\**************************************/

/**
 * ath5k_hw_tweak_initval_settings() - Tweak initial settings
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * Some settings are not handled on initvals, e.g. bwmode
 * settings, some phy settings, workarounds etc that in general
 * don't fit anywhere else or are too small to introduce a separate
 * function for each one. So we have this function to handle
 * them all during reset and complete card's initialization.
 */
static void
ath5k_hw_tweak_initval_settings(struct ath5k_hw *ah,
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

	/* Set fast ADC */
	if ((ah->ah_radio == AR5K_RF5413) ||
		(ah->ah_radio == AR5K_RF2317) ||
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
		if (channel->band == IEEE80211_BAND_5GHZ)
			data = 0xffb81020;
		else
			data = 0xffb80d20;
		ath5k_hw_reg_write(ah, data, AR5K_PHY_FRAME_CTL);
	}

	if (ah->ah_mac_srev < AR5K_SREV_AR5211) {
		/* Clear QCU/DCU clock gating register */
		ath5k_hw_reg_write(ah, 0, AR5K_QCUDCU_CLKGT);
		/* Set DAC/ADC delays */
		ath5k_hw_reg_write(ah, AR5K_PHY_SCAL_32MHZ_5311,
						AR5K_PHY_SCAL);
		/* Enable PCU FIFO corruption ECO */
		AR5K_REG_ENABLE_BITS(ah, AR5K_DIAG_SW_5211,
					AR5K_DIAG_SW_ECO_ENABLE);
	}

	if (ah->ah_bwmode) {
		/* Increase PHY switch and AGC settling time
		 * on turbo mode (ath5k_hw_commit_eeprom_settings
		 * will override settling time if available) */
		if (ah->ah_bwmode == AR5K_BWMODE_40MHZ) {

			AR5K_REG_WRITE_BITS(ah, AR5K_PHY_SETTLING,
						AR5K_PHY_SETTLING_AGC,
						AR5K_AGC_SETTLING_TURBO);

			/* XXX: Initvals indicate we only increase
			 * switch time on AR5212, 5211 and 5210
			 * only change agc time (bug?) */
			if (ah->ah_version == AR5K_AR5212)
				AR5K_REG_WRITE_BITS(ah, AR5K_PHY_SETTLING,
						AR5K_PHY_SETTLING_SWITCH,
						AR5K_SWITCH_SETTLING_TURBO);

			if (ah->ah_version == AR5K_AR5210) {
				/* Set Frame Control Register */
				ath5k_hw_reg_write(ah,
					(AR5K_PHY_FRAME_CTL_INI |
					AR5K_PHY_TURBO_MODE |
					AR5K_PHY_TURBO_SHORT | 0x2020),
					AR5K_PHY_FRAME_CTL_5210);
			}
		/* On 5413 PHY force window length for half/quarter rate*/
		} else if ((ah->ah_mac_srev >= AR5K_SREV_AR5424) &&
		(ah->ah_mac_srev <= AR5K_SREV_AR5414)) {
			AR5K_REG_WRITE_BITS(ah, AR5K_PHY_FRAME_CTL_5211,
						AR5K_PHY_FRAME_CTL_WIN_LEN,
						3);
		}
	} else if (ah->ah_version == AR5K_AR5210) {
		/* Set Frame Control Register for normal operation */
		ath5k_hw_reg_write(ah, (AR5K_PHY_FRAME_CTL_INI | 0x1020),
						AR5K_PHY_FRAME_CTL_5210);
	}
}

/**
 * ath5k_hw_commit_eeprom_settings() - Commit settings from EEPROM
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * Use settings stored on EEPROM to properly initialize the card
 * based on various infos and per-mode calibration data.
 */
static void
ath5k_hw_commit_eeprom_settings(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	s16 cck_ofdm_pwr_delta;
	u8 ee_mode;

	/* TODO: Add support for AR5210 EEPROM */
	if (ah->ah_version == AR5K_AR5210)
		return;

	ee_mode = ath5k_eeprom_mode_from_channel(ah, channel);

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
		if (channel->hw_value == AR5K_MODE_11G)
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

	if ((ah->ah_bwmode == AR5K_BWMODE_40MHZ) &&
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


/*********************\
* Main reset function *
\*********************/

/**
 * ath5k_hw_reset() - The main reset function
 * @ah: The &struct ath5k_hw
 * @op_mode: One of enum nl80211_iftype
 * @channel: The &struct ieee80211_channel
 * @fast: Enable fast channel switching
 * @skip_pcu: Skip pcu initialization
 *
 * This is the function we call each time we want to (re)initialize the
 * card and pass new settings to hw. We also call it when hw runs into
 * trouble to make it come back to a working state.
 *
 * Returns 0 on success, -EINVAL on false op_mode or channel infos, or -EIO
 * on failure.
 */
int
ath5k_hw_reset(struct ath5k_hw *ah, enum nl80211_iftype op_mode,
		struct ieee80211_channel *channel, bool fast, bool skip_pcu)
{
	u32 s_seq[10], s_led[3], tsf_up, tsf_lo;
	u8 mode;
	int i, ret;

	tsf_up = 0;
	tsf_lo = 0;
	mode = 0;

	/*
	 * Sanity check for fast flag
	 * Fast channel change only available
	 * on AR2413/AR5413.
	 */
	if (fast && (ah->ah_radio != AR5K_RF2413) &&
	(ah->ah_radio != AR5K_RF5413))
		fast = false;

	/* Disable sleep clock operation
	 * to avoid register access delay on certain
	 * PHY registers */
	if (ah->ah_version == AR5K_AR5212)
		ath5k_hw_set_sleep_clock(ah, false);

	/*
	 * Stop PCU
	 */
	ath5k_hw_stop_rx_pcu(ah);

	/*
	 * Stop DMA
	 *
	 * Note: If DMA didn't stop continue
	 * since only a reset will fix it.
	 */
	ret = ath5k_hw_dma_stop(ah);

	/* RF Bus grant won't work if we have pending
	 * frames */
	if (ret && fast) {
		ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
			"DMA didn't stop, falling back to normal reset\n");
		fast = false;
		/* Non fatal, just continue with
		 * normal reset */
		ret = 0;
	}

	mode = channel->hw_value;
	switch (mode) {
	case AR5K_MODE_11A:
		break;
	case AR5K_MODE_11G:
		if (ah->ah_version <= AR5K_AR5211) {
			ATH5K_ERR(ah,
				"G mode not available on 5210/5211");
			return -EINVAL;
		}
		break;
	case AR5K_MODE_11B:
		if (ah->ah_version < AR5K_AR5211) {
			ATH5K_ERR(ah,
				"B mode not available on 5210");
			return -EINVAL;
		}
		break;
	default:
		ATH5K_ERR(ah,
			"invalid channel: %d\n", channel->center_freq);
		return -EINVAL;
	}

	/*
	 * If driver requested fast channel change and DMA has stopped
	 * go on. If it fails continue with a normal reset.
	 */
	if (fast) {
		ret = ath5k_hw_phy_init(ah, channel, mode, true);
		if (ret) {
			ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
				"fast chan change failed, falling back to normal reset\n");
			/* Non fatal, can happen eg.
			 * on mode change */
			ret = 0;
		} else {
			ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
				"fast chan change successful\n");
			return 0;
		}
	}

	/*
	 * Save some registers before a reset
	 */
	if (ah->ah_version != AR5K_AR5210) {
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

		/* TSF accelerates on AR5211 during reset
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


	/*GPIOs*/
	s_led[0] = ath5k_hw_reg_read(ah, AR5K_PCICFG) &
					AR5K_PCICFG_LEDSTATE;
	s_led[1] = ath5k_hw_reg_read(ah, AR5K_GPIOCR);
	s_led[2] = ath5k_hw_reg_read(ah, AR5K_GPIODO);


	/*
	 * Since we are going to write rf buffer
	 * check if we have any pending gain_F
	 * optimization settings
	 */
	if (ah->ah_version == AR5K_AR5212 &&
	(ah->ah_radio <= AR5K_RF5112)) {
		if (!fast && ah->ah_rf_banks != NULL)
				ath5k_hw_gainf_calibrate(ah);
	}

	/* Wakeup the device */
	ret = ath5k_hw_nic_wakeup(ah, channel);
	if (ret)
		return ret;

	/* PHY access enable */
	if (ah->ah_mac_srev >= AR5K_SREV_AR5211)
		ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_5GHZ, AR5K_PHY(0));
	else
		ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_5GHZ | 0x40,
							AR5K_PHY(0));

	/* Write initial settings */
	ret = ath5k_hw_write_initvals(ah, mode, skip_pcu);
	if (ret)
		return ret;

	/* Initialize core clock settings */
	ath5k_hw_init_core_clock(ah);

	/*
	 * Tweak initval settings for revised
	 * chipsets and add some more config
	 * bits
	 */
	ath5k_hw_tweak_initval_settings(ah, channel);

	/* Commit values from EEPROM */
	ath5k_hw_commit_eeprom_settings(ah, channel);


	/*
	 * Restore saved values
	 */

	/* Seqnum, TSF */
	if (ah->ah_version != AR5K_AR5210) {
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

	/* Ledstate */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, s_led[0]);

	/* Gpio settings */
	ath5k_hw_reg_write(ah, s_led[1], AR5K_GPIOCR);
	ath5k_hw_reg_write(ah, s_led[2], AR5K_GPIODO);

	/*
	 * Initialize PCU
	 */
	ath5k_hw_pcu_init(ah, op_mode);

	/*
	 * Initialize PHY
	 */
	ret = ath5k_hw_phy_init(ah, channel, mode, false);
	if (ret) {
		ATH5K_ERR(ah,
			"failed to initialize PHY (%i) !\n", ret);
		return ret;
	}

	/*
	 * Configure QCUs/DCUs
	 */
	ret = ath5k_hw_init_queues(ah);
	if (ret)
		return ret;


	/*
	 * Initialize DMA/Interrupts
	 */
	ath5k_hw_dma_init(ah);


	/*
	 * Enable 32KHz clock function for AR5212+ chips
	 * Set clocks to 32KHz operation and use an
	 * external 32KHz crystal when sleeping if one
	 * exists.
	 * Disabled by default because it is also disabled in
	 * other drivers and it is known to cause stability
	 * issues on some devices
	 */
	if (ah->ah_use_32khz_clock && ah->ah_version == AR5K_AR5212 &&
	    op_mode != NL80211_IFTYPE_AP)
		ath5k_hw_set_sleep_clock(ah, true);

	/*
	 * Disable beacons and reset the TSF
	 */
	AR5K_REG_DISABLE_BITS(ah, AR5K_BEACON, AR5K_BEACON_ENABLE);
	ath5k_hw_reset_tsf(ah);
	return 0;
}
