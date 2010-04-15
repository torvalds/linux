/*
 * Copyright (c) 2010 Atheros Communications Inc.
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

#ifndef ATH9K_HW_OPS_H
#define ATH9K_HW_OPS_H

#include "hw.h"

/* Hardware core and driver accessible callbacks */

static inline void ath9k_hw_configpcipowersave(struct ath_hw *ah,
					       int restore,
					       int power_off)
{
	ath9k_hw_ops(ah)->config_pci_powersave(ah, restore, power_off);
}

static inline void ath9k_hw_rxena(struct ath_hw *ah)
{
	ath9k_hw_ops(ah)->rx_enable(ah);
}

static inline void ath9k_hw_set_desc_link(struct ath_hw *ah, void *ds,
					  u32 link)
{
	ath9k_hw_ops(ah)->set_desc_link(ds, link);
}

static inline void ath9k_hw_get_desc_link(struct ath_hw *ah, void *ds,
					  u32 **link)
{
	ath9k_hw_ops(ah)->get_desc_link(ds, link);
}
/* Private hardware call ops */

/* PHY ops */

static inline int ath9k_hw_rf_set_freq(struct ath_hw *ah,
				       struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->rf_set_freq(ah, chan);
}

static inline void ath9k_hw_spur_mitigate_freq(struct ath_hw *ah,
					       struct ath9k_channel *chan)
{
	ath9k_hw_private_ops(ah)->spur_mitigate_freq(ah, chan);
}

static inline int ath9k_hw_rf_alloc_ext_banks(struct ath_hw *ah)
{
	if (!ath9k_hw_private_ops(ah)->rf_alloc_ext_banks)
		return 0;

	return ath9k_hw_private_ops(ah)->rf_alloc_ext_banks(ah);
}

static inline void ath9k_hw_rf_free_ext_banks(struct ath_hw *ah)
{
	if (!ath9k_hw_private_ops(ah)->rf_free_ext_banks)
		return;

	ath9k_hw_private_ops(ah)->rf_free_ext_banks(ah);
}

static inline bool ath9k_hw_set_rf_regs(struct ath_hw *ah,
					struct ath9k_channel *chan,
					u16 modesIndex)
{
	if (!ath9k_hw_private_ops(ah)->set_rf_regs)
		return true;

	return ath9k_hw_private_ops(ah)->set_rf_regs(ah, chan, modesIndex);
}

static inline void ath9k_hw_init_bb(struct ath_hw *ah,
				    struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->init_bb(ah, chan);
}

static inline void ath9k_hw_set_channel_regs(struct ath_hw *ah,
					     struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->set_channel_regs(ah, chan);
}

static inline int ath9k_hw_process_ini(struct ath_hw *ah,
					struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->process_ini(ah, chan);
}

static inline void ath9k_olc_init(struct ath_hw *ah)
{
	if (!ath9k_hw_private_ops(ah)->olc_init)
		return;

	return ath9k_hw_private_ops(ah)->olc_init(ah);
}

static inline void ath9k_hw_set_rfmode(struct ath_hw *ah,
				       struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->set_rfmode(ah, chan);
}

static inline void ath9k_hw_mark_phy_inactive(struct ath_hw *ah)
{
	return ath9k_hw_private_ops(ah)->mark_phy_inactive(ah);
}

static inline void ath9k_hw_set_delta_slope(struct ath_hw *ah,
					    struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->set_delta_slope(ah, chan);
}

static inline bool ath9k_hw_rfbus_req(struct ath_hw *ah)
{
	return ath9k_hw_private_ops(ah)->rfbus_req(ah);
}

static inline void ath9k_hw_rfbus_done(struct ath_hw *ah)
{
	return ath9k_hw_private_ops(ah)->rfbus_done(ah);
}

static inline void ath9k_enable_rfkill(struct ath_hw *ah)
{
	return ath9k_hw_private_ops(ah)->enable_rfkill(ah);
}

static inline void ath9k_hw_restore_chainmask(struct ath_hw *ah)
{
	if (!ath9k_hw_private_ops(ah)->restore_chainmask)
		return;

	return ath9k_hw_private_ops(ah)->restore_chainmask(ah);
}

static inline void ath9k_hw_set_diversity(struct ath_hw *ah, bool value)
{
	return ath9k_hw_private_ops(ah)->set_diversity(ah, value);
}

static inline bool ath9k_hw_ani_control(struct ath_hw *ah,
					enum ath9k_ani_cmd cmd, int param)
{
	return ath9k_hw_private_ops(ah)->ani_control(ah, cmd, param);
}

#endif /* ATH9K_HW_OPS_H */
