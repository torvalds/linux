/*

  Broadcom B43 wireless driver

  Copyright (c) 2010 Rafał Miłecki <zajec5@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#ifndef B43_RADIO_2056_H_
#define B43_RADIO_2056_H_

#include <linux/types.h>

#include "tables_nphy.h"

struct b43_nphy_channeltab_entry_rev3 {
	/* The channel frequency in MHz */
	u16 freq;
	/* Radio register values on channelswitch */
	u8 radio_syn_pll_vcocal1;
	u8 radio_syn_pll_vcocal2;
	u8 radio_syn_pll_refdiv;
	u8 radio_syn_pll_mmd2;
	u8 radio_syn_pll_mmd1;
	u8 radio_syn_pll_loopfilter1;
	u8 radio_syn_pll_loopfilter2;
	u8 radio_syn_pll_loopfilter3;
	u8 radio_syn_pll_loopfilter4;
	u8 radio_syn_pll_loopfilter5;
	u8 radio_syn_reserved_addr27;
	u8 radio_syn_reserved_addr28;
	u8 radio_syn_reserved_addr29;
	u8 radio_syn_logen_vcobuf1;
	u8 radio_syn_logen_mixer2;
	u8 radio_syn_logen_buf3;
	u8 radio_syn_logen_buf4;
	u8 radio_rx0_lnaa_tune;
	u8 radio_rx0_lnag_tune;
	u8 radio_tx0_intpaa_boost_tune;
	u8 radio_tx0_intpag_boost_tune;
	u8 radio_tx0_pada_boost_tune;
	u8 radio_tx0_padg_boost_tune;
	u8 radio_tx0_pgaa_boost_tune;
	u8 radio_tx0_pgag_boost_tune;
	u8 radio_tx0_mixa_boost_tune;
	u8 radio_tx0_mixg_boost_tune;
	u8 radio_rx1_lnaa_tune;
	u8 radio_rx1_lnag_tune;
	u8 radio_tx1_intpaa_boost_tune;
	u8 radio_tx1_intpag_boost_tune;
	u8 radio_tx1_pada_boost_tune;
	u8 radio_tx1_padg_boost_tune;
	u8 radio_tx1_pgaa_boost_tune;
	u8 radio_tx1_pgag_boost_tune;
	u8 radio_tx1_mixa_boost_tune;
	u8 radio_tx1_mixg_boost_tune;
	/* PHY register values on channelswitch */
	struct b43_phy_n_sfo_cfg phy_regs;
};

#endif /* B43_RADIO_2056_H_ */
