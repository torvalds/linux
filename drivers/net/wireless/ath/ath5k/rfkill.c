/*
 * RFKILL support for ath5k
 *
 * Copyright (c) 2009 Tobias Doerffel <tobias.doerffel@gmail.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include "ath5k.h"


static inline void ath5k_rfkill_disable(struct ath5k_hw *ah)
{
	ATH5K_DBG(ah, ATH5K_DEBUG_ANY, "rfkill disable (gpio:%d polarity:%d)\n",
		ah->rf_kill.gpio, ah->rf_kill.polarity);
	ath5k_hw_set_gpio_output(ah, ah->rf_kill.gpio);
	ath5k_hw_set_gpio(ah, ah->rf_kill.gpio, !ah->rf_kill.polarity);
}


static inline void ath5k_rfkill_enable(struct ath5k_hw *ah)
{
	ATH5K_DBG(ah, ATH5K_DEBUG_ANY, "rfkill enable (gpio:%d polarity:%d)\n",
		ah->rf_kill.gpio, ah->rf_kill.polarity);
	ath5k_hw_set_gpio_output(ah, ah->rf_kill.gpio);
	ath5k_hw_set_gpio(ah, ah->rf_kill.gpio, ah->rf_kill.polarity);
}

static inline void ath5k_rfkill_set_intr(struct ath5k_hw *ah, bool enable)
{
	u32 curval;

	ath5k_hw_set_gpio_input(ah, ah->rf_kill.gpio);
	curval = ath5k_hw_get_gpio(ah, ah->rf_kill.gpio);
	ath5k_hw_set_gpio_intr(ah, ah->rf_kill.gpio, enable ?
					!!curval : !curval);
}

static bool
ath5k_is_rfkill_set(struct ath5k_hw *ah)
{
	/* configuring GPIO for input for some reason disables rfkill */
	/*ath5k_hw_set_gpio_input(ah, ah->rf_kill.gpio);*/
	return ath5k_hw_get_gpio(ah, ah->rf_kill.gpio) ==
							ah->rf_kill.polarity;
}

static void
ath5k_tasklet_rfkill_toggle(struct tasklet_struct *t)
{
	struct ath5k_hw *ah = from_tasklet(ah, t, rf_kill.toggleq);
	bool blocked;

	blocked = ath5k_is_rfkill_set(ah);
	wiphy_rfkill_set_hw_state(ah->hw->wiphy, blocked);
}


void
ath5k_rfkill_hw_start(struct ath5k_hw *ah)
{
	/* read rfkill GPIO configuration from EEPROM header */
	ah->rf_kill.gpio = ah->ah_capabilities.cap_eeprom.ee_rfkill_pin;
	ah->rf_kill.polarity = ah->ah_capabilities.cap_eeprom.ee_rfkill_pol;

	tasklet_setup(&ah->rf_kill.toggleq, ath5k_tasklet_rfkill_toggle);

	ath5k_rfkill_disable(ah);

	/* enable interrupt for rfkill switch */
	if (AR5K_EEPROM_HDR_RFKILL(ah->ah_capabilities.cap_eeprom.ee_header))
		ath5k_rfkill_set_intr(ah, true);
}


void
ath5k_rfkill_hw_stop(struct ath5k_hw *ah)
{
	/* disable interrupt for rfkill switch */
	if (AR5K_EEPROM_HDR_RFKILL(ah->ah_capabilities.cap_eeprom.ee_header))
		ath5k_rfkill_set_intr(ah, false);

	tasklet_kill(&ah->rf_kill.toggleq);

	/* enable RFKILL when stopping HW so Wifi LED is turned off */
	ath5k_rfkill_enable(ah);
}

