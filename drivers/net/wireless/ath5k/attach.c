/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
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

/*************************************\
* Attach/Detach Functions and helpers *
\*************************************/

#include <linux/pci.h>
#include "ath5k.h"
#include "reg.h"
#include "debug.h"
#include "base.h"

/**
 * ath5k_hw_post - Power On Self Test helper function
 *
 * @ah: The &struct ath5k_hw
 */
static int ath5k_hw_post(struct ath5k_hw *ah)
{

	int i, c;
	u16 cur_reg;
	u16 regs[2] = {AR5K_STA_ID0, AR5K_PHY(8)};
	u32 var_pattern;
	u32 static_pattern[4] = {
		0x55555555,	0xaaaaaaaa,
		0x66666666,	0x99999999
	};
	u32 init_val;
	u32 cur_val;

	for (c = 0; c < 2; c++) {

		cur_reg = regs[c];

		/* Save previous value */
		init_val = ath5k_hw_reg_read(ah, cur_reg);

		for (i = 0; i < 256; i++) {
			var_pattern = i << 16 | i;
			ath5k_hw_reg_write(ah, var_pattern, cur_reg);
			cur_val = ath5k_hw_reg_read(ah, cur_reg);

			if (cur_val != var_pattern) {
				ATH5K_ERR(ah->ah_sc, "POST Failed !!!\n");
				return -EAGAIN;
			}

			/* Found on ndiswrapper dumps */
			var_pattern = 0x0039080f;
			ath5k_hw_reg_write(ah, var_pattern, cur_reg);
		}

		for (i = 0; i < 4; i++) {
			var_pattern = static_pattern[i];
			ath5k_hw_reg_write(ah, var_pattern, cur_reg);
			cur_val = ath5k_hw_reg_read(ah, cur_reg);

			if (cur_val != var_pattern) {
				ATH5K_ERR(ah->ah_sc, "POST Failed !!!\n");
				return -EAGAIN;
			}

			/* Found on ndiswrapper dumps */
			var_pattern = 0x003b080f;
			ath5k_hw_reg_write(ah, var_pattern, cur_reg);
		}

		/* Restore previous value */
		ath5k_hw_reg_write(ah, init_val, cur_reg);

	}

	return 0;

}

/**
 * ath5k_hw_attach - Check if hw is supported and init the needed structs
 *
 * @sc: The &struct ath5k_softc we got from the driver's attach function
 * @mac_version: The mac version id (check out ath5k.h) based on pci id
 *
 * Check if the device is supported, perform a POST and initialize the needed
 * structs. Returns -ENOMEM if we don't have memory for the needed structs,
 * -ENODEV if the device is not supported or prints an error msg if something
 * else went wrong.
 */
struct ath5k_hw *ath5k_hw_attach(struct ath5k_softc *sc, u8 mac_version)
{
	struct ath5k_hw *ah;
	struct pci_dev *pdev = sc->pdev;
	u8 mac[ETH_ALEN];
	int ret;
	u32 srev;

	/*If we passed the test malloc a ath5k_hw struct*/
	ah = kzalloc(sizeof(struct ath5k_hw), GFP_KERNEL);
	if (ah == NULL) {
		ret = -ENOMEM;
		ATH5K_ERR(sc, "out of memory\n");
		goto err;
	}

	ah->ah_sc = sc;
	ah->ah_iobase = sc->iobase;

	/*
	 * HW information
	 */
	ah->ah_op_mode = IEEE80211_IF_TYPE_STA;
	ah->ah_radar.r_enabled = AR5K_TUNE_RADAR_ALERT;
	ah->ah_turbo = false;
	ah->ah_txpower.txp_tpc = AR5K_TUNE_TPC_TXPOWER;
	ah->ah_imr = 0;
	ah->ah_atim_window = 0;
	ah->ah_aifs = AR5K_TUNE_AIFS;
	ah->ah_cw_min = AR5K_TUNE_CWMIN;
	ah->ah_limit_tx_retries = AR5K_INIT_TX_RETRY;
	ah->ah_software_retry = false;
	ah->ah_ant_diversity = AR5K_TUNE_ANT_DIVERSITY;

	/*
	 * Set the mac revision based on the pci id
	 */
	ah->ah_version = mac_version;

	/*Fill the ath5k_hw struct with the needed functions*/
	ret = ath5k_hw_init_desc_functions(ah);
	if (ret)
		goto err_free;

	/* Bring device out of sleep and reset it's units */
	ret = ath5k_hw_nic_wakeup(ah, CHANNEL_B, true);
	if (ret)
		goto err_free;

	/* Get MAC, PHY and RADIO revisions */
	srev = ath5k_hw_reg_read(ah, AR5K_SREV);
	ah->ah_mac_srev = srev;
	ah->ah_mac_version = AR5K_REG_MS(srev, AR5K_SREV_VER);
	ah->ah_mac_revision = AR5K_REG_MS(srev, AR5K_SREV_REV);
	ah->ah_phy_revision = ath5k_hw_reg_read(ah, AR5K_PHY_CHIP_ID) &
			0xffffffff;
	ah->ah_radio_5ghz_revision = ath5k_hw_radio_revision(ah,
			CHANNEL_5GHZ);

	if (ah->ah_version == AR5K_AR5210)
		ah->ah_radio_2ghz_revision = 0;
	else
		ah->ah_radio_2ghz_revision = ath5k_hw_radio_revision(ah,
				CHANNEL_2GHZ);

	/* Return on unsuported chips (unsupported eeprom etc) */
	if ((srev >= AR5K_SREV_VER_AR5416) &&
	(srev < AR5K_SREV_VER_AR2425)) {
		ATH5K_ERR(sc, "Device not yet supported.\n");
		ret = -ENODEV;
		goto err_free;
	} else if (srev == AR5K_SREV_VER_AR2425) {
		ATH5K_WARN(sc, "Support for RF2425 is under development.\n");
	}

	/* Identify single chip solutions */
	if (((srev <= AR5K_SREV_VER_AR5414) &&
	(srev >= AR5K_SREV_VER_AR2413)) ||
	(srev == AR5K_SREV_VER_AR2425)) {
		ah->ah_single_chip = true;
	} else {
		ah->ah_single_chip = false;
	}

	/* Single chip radio */
	if (ah->ah_radio_2ghz_revision == ah->ah_radio_5ghz_revision)
		ah->ah_radio_2ghz_revision = 0;

	/* Identify the radio chip*/
	if (ah->ah_version == AR5K_AR5210) {
		ah->ah_radio = AR5K_RF5110;
	/*
	 * Register returns 0x0/0x04 for radio revision
	 * so ath5k_hw_radio_revision doesn't parse the value
	 * correctly. For now we are based on mac's srev to
	 * identify RF2425 radio.
	 */
	} else if (srev == AR5K_SREV_VER_AR2425) {
		ah->ah_radio = AR5K_RF2425;
		ah->ah_phy_spending = AR5K_PHY_SPENDING_RF2425;
	} else if (ah->ah_radio_5ghz_revision < AR5K_SREV_RAD_5112) {
		ah->ah_radio = AR5K_RF5111;
		ah->ah_phy_spending = AR5K_PHY_SPENDING_RF5111;
	} else if (ah->ah_radio_5ghz_revision < AR5K_SREV_RAD_SC0) {
		ah->ah_radio = AR5K_RF5112;
		ah->ah_phy_spending = AR5K_PHY_SPENDING_RF5112;
	} else if (ah->ah_radio_5ghz_revision < AR5K_SREV_RAD_SC1) {
		ah->ah_radio = AR5K_RF2413;
		ah->ah_phy_spending = AR5K_PHY_SPENDING_RF2413;
	} else if (ah->ah_radio_5ghz_revision < AR5K_SREV_RAD_SC2) {
		ah->ah_radio = AR5K_RF5413;
		ah->ah_phy_spending = AR5K_PHY_SPENDING_RF5413;
	} else if (ah->ah_radio_5ghz_revision < AR5K_SREV_RAD_5133) {
		/* AR5424 */
		if (srev >= AR5K_SREV_VER_AR5424) {
			ah->ah_radio = AR5K_RF5413;
			ah->ah_phy_spending = AR5K_PHY_SPENDING_RF5413;
		/* AR2424 */
		} else {
			ah->ah_radio = AR5K_RF2413; /* For testing */
			ah->ah_phy_spending = AR5K_PHY_SPENDING_RF2413;
		}
	}
	ah->ah_phy = AR5K_PHY(0);

	/*
	 * Write PCI-E power save settings
	 */
	if ((ah->ah_version == AR5K_AR5212) && (pdev->is_pcie)) {
		ath5k_hw_reg_write(ah, 0x9248fc00, 0x4080);
		ath5k_hw_reg_write(ah, 0x24924924, 0x4080);
		ath5k_hw_reg_write(ah, 0x28000039, 0x4080);
		ath5k_hw_reg_write(ah, 0x53160824, 0x4080);
		ath5k_hw_reg_write(ah, 0xe5980579, 0x4080);
		ath5k_hw_reg_write(ah, 0x001defff, 0x4080);
		ath5k_hw_reg_write(ah, 0x1aaabe40, 0x4080);
		ath5k_hw_reg_write(ah, 0xbe105554, 0x4080);
		ath5k_hw_reg_write(ah, 0x000e3007, 0x4080);
		ath5k_hw_reg_write(ah, 0x00000000, 0x4084);
	}

	/*
	 * POST
	 */
	ret = ath5k_hw_post(ah);
	if (ret)
		goto err_free;

	/* Write AR5K_PCICFG_UNK on 2112B and later chips */
	if (ah->ah_radio_5ghz_revision > AR5K_SREV_RAD_2112B ||
	srev > AR5K_SREV_VER_AR2413) {
		ath5k_hw_reg_write(ah, AR5K_PCICFG_UNK, AR5K_PCICFG);
	}

	/*
	 * Get card capabilities, values, ...
	 */
	ret = ath5k_eeprom_init(ah);
	if (ret) {
		ATH5K_ERR(sc, "unable to init EEPROM\n");
		goto err_free;
	}

	/* Get misc capabilities */
	ret = ath5k_hw_set_capabilities(ah);
	if (ret) {
		ATH5K_ERR(sc, "unable to get device capabilities: 0x%04x\n",
			sc->pdev->device);
		goto err_free;
	}

	/* Get MAC address */
	ret = ath5k_eeprom_read_mac(ah, mac);
	if (ret) {
		ATH5K_ERR(sc, "unable to read address from EEPROM: 0x%04x\n",
			sc->pdev->device);
		goto err_free;
	}

	ath5k_hw_set_lladdr(ah, mac);
	/* Set BSSID to bcast address: ff:ff:ff:ff:ff:ff for now */
	memset(ah->ah_bssid, 0xff, ETH_ALEN);
	ath5k_hw_set_associd(ah, ah->ah_bssid, 0);
	ath5k_hw_set_opmode(ah);

	ath5k_hw_set_rfgain_opt(ah);

	return ah;
err_free:
	kfree(ah);
err:
	return ERR_PTR(ret);
}

/**
 * ath5k_hw_detach - Free the ath5k_hw struct
 *
 * @ah: The &struct ath5k_hw
 */
void ath5k_hw_detach(struct ath5k_hw *ah)
{
	ATH5K_TRACE(ah->ah_sc);

	__set_bit(ATH_STAT_INVALID, ah->ah_sc->status);

	if (ah->ah_rf_banks != NULL)
		kfree(ah->ah_rf_banks);

	/* assume interrupts are down */
	kfree(ah);
}
