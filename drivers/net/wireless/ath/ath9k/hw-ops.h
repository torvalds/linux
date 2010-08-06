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
static inline bool ath9k_hw_calibrate(struct ath_hw *ah,
				      struct ath9k_channel *chan,
				      u8 rxchainmask,
				      bool longcal)
{
	return ath9k_hw_ops(ah)->calibrate(ah, chan, rxchainmask, longcal);
}

static inline bool ath9k_hw_getisr(struct ath_hw *ah, enum ath9k_int *masked)
{
	return ath9k_hw_ops(ah)->get_isr(ah, masked);
}

static inline void ath9k_hw_filltxdesc(struct ath_hw *ah, void *ds, u32 seglen,
				  bool is_firstseg, bool is_lastseg,
				  const void *ds0, dma_addr_t buf_addr,
				  unsigned int qcu)
{
	ath9k_hw_ops(ah)->fill_txdesc(ah, ds, seglen, is_firstseg, is_lastseg,
				      ds0, buf_addr, qcu);
}

static inline int ath9k_hw_txprocdesc(struct ath_hw *ah, void *ds,
				      struct ath_tx_status *ts)
{
	return ath9k_hw_ops(ah)->proc_txdesc(ah, ds, ts);
}

static inline void ath9k_hw_set11n_txdesc(struct ath_hw *ah, void *ds,
					  u32 pktLen, enum ath9k_pkt_type type,
					  u32 txPower, u32 keyIx,
					  enum ath9k_key_type keyType,
					  u32 flags)
{
	ath9k_hw_ops(ah)->set11n_txdesc(ah, ds, pktLen, type, txPower, keyIx,
				      keyType, flags);
}

static inline void ath9k_hw_set11n_ratescenario(struct ath_hw *ah, void *ds,
					void *lastds,
					u32 durUpdateEn, u32 rtsctsRate,
					u32 rtsctsDuration,
					struct ath9k_11n_rate_series series[],
					u32 nseries, u32 flags)
{
	ath9k_hw_ops(ah)->set11n_ratescenario(ah, ds, lastds, durUpdateEn,
					    rtsctsRate, rtsctsDuration, series,
					    nseries, flags);
}

static inline void ath9k_hw_set11n_aggr_first(struct ath_hw *ah, void *ds,
					u32 aggrLen)
{
	ath9k_hw_ops(ah)->set11n_aggr_first(ah, ds, aggrLen);
}

static inline void ath9k_hw_set11n_aggr_middle(struct ath_hw *ah, void *ds,
					       u32 numDelims)
{
	ath9k_hw_ops(ah)->set11n_aggr_middle(ah, ds, numDelims);
}

static inline void ath9k_hw_set11n_aggr_last(struct ath_hw *ah, void *ds)
{
	ath9k_hw_ops(ah)->set11n_aggr_last(ah, ds);
}

static inline void ath9k_hw_clr11n_aggr(struct ath_hw *ah, void *ds)
{
	ath9k_hw_ops(ah)->clr11n_aggr(ah, ds);
}

static inline void ath9k_hw_set11n_burstduration(struct ath_hw *ah, void *ds,
						 u32 burstDuration)
{
	ath9k_hw_ops(ah)->set11n_burstduration(ah, ds, burstDuration);
}

static inline void ath9k_hw_set11n_virtualmorefrag(struct ath_hw *ah, void *ds,
						   u32 vmf)
{
	ath9k_hw_ops(ah)->set11n_virtualmorefrag(ah, ds, vmf);
}

static inline void ath9k_hw_procmibevent(struct ath_hw *ah)
{
	ath9k_hw_ops(ah)->ani_proc_mib_event(ah);
}

static inline void ath9k_hw_ani_monitor(struct ath_hw *ah,
					struct ath9k_channel *chan)
{
	ath9k_hw_ops(ah)->ani_monitor(ah, chan);
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

static inline void ath9k_hw_do_getnf(struct ath_hw *ah,
				     int16_t nfarray[NUM_NF_READINGS])
{
	ath9k_hw_private_ops(ah)->do_getnf(ah, nfarray);
}

static inline bool ath9k_hw_init_cal(struct ath_hw *ah,
				     struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->init_cal(ah, chan);
}

static inline void ath9k_hw_setup_calibration(struct ath_hw *ah,
					      struct ath9k_cal_list *currCal)
{
	ath9k_hw_private_ops(ah)->setup_calibration(ah, currCal);
}

static inline bool ath9k_hw_iscal_supported(struct ath_hw *ah,
					    enum ath9k_cal_types calType)
{
	return ath9k_hw_private_ops(ah)->iscal_supported(ah, calType);
}

static inline void ath9k_ani_reset(struct ath_hw *ah, bool is_scanning)
{
	ath9k_hw_private_ops(ah)->ani_reset(ah, is_scanning);
}

#endif /* ATH9K_HW_OPS_H */
