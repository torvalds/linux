/*

  Broadcom B43 wireless driver
  IEEE 802.11n LCN-PHY support

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

#include <linux/slab.h>

#include "b43.h"
#include "phy_lcn.h"
#include "tables_phy_lcn.h"
#include "main.h"

/**************************************************
 * PHY ops struct.
 **************************************************/

const struct b43_phy_operations b43_phyops_lcn = {
	/*
	.allocate		= b43_phy_lcn_op_allocate,
	.free			= b43_phy_lcn_op_free,
	.prepare_structs	= b43_phy_lcn_op_prepare_structs,
	.init			= b43_phy_lcn_op_init,
	.phy_read		= b43_phy_lcn_op_read,
	.phy_write		= b43_phy_lcn_op_write,
	.phy_maskset		= b43_phy_lcn_op_maskset,
	.radio_read		= b43_phy_lcn_op_radio_read,
	.radio_write		= b43_phy_lcn_op_radio_write,
	.software_rfkill	= b43_phy_lcn_op_software_rfkill,
	.switch_analog		= b43_phy_lcn_op_switch_analog,
	.switch_channel		= b43_phy_lcn_op_switch_channel,
	.get_default_chan	= b43_phy_lcn_op_get_default_chan,
	.recalc_txpower		= b43_phy_lcn_op_recalc_txpower,
	.adjust_txpower		= b43_phy_lcn_op_adjust_txpower,
	*/
};
