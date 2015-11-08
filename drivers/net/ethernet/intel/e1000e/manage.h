/* Intel PRO/1000 Linux driver
 * Copyright(c) 1999 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Linux NICS <linux.nics@intel.com>
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _E1000E_MANAGE_H_
#define _E1000E_MANAGE_H_

bool e1000e_check_mng_mode_generic(struct e1000_hw *hw);
bool e1000e_enable_tx_pkt_filtering(struct e1000_hw *hw);
s32 e1000e_mng_write_dhcp_info(struct e1000_hw *hw, u8 *buffer, u16 length);
bool e1000e_enable_mng_pass_thru(struct e1000_hw *hw);

enum e1000_mng_mode {
	e1000_mng_mode_none = 0,
	e1000_mng_mode_asf,
	e1000_mng_mode_pt,
	e1000_mng_mode_ipmi,
	e1000_mng_mode_host_if_only
};

#define E1000_FACTPS_MNGCG			0x20000000

#define E1000_FWSM_MODE_MASK			0xE
#define E1000_FWSM_MODE_SHIFT			1

#define E1000_MNG_IAMT_MODE			0x3
#define E1000_MNG_DHCP_COOKIE_LENGTH		0x10
#define E1000_MNG_DHCP_COOKIE_OFFSET		0x6F0
#define E1000_MNG_DHCP_COMMAND_TIMEOUT		10
#define E1000_MNG_DHCP_TX_PAYLOAD_CMD		64
#define E1000_MNG_DHCP_COOKIE_STATUS_PARSING	0x1
#define E1000_MNG_DHCP_COOKIE_STATUS_VLAN	0x2

#define E1000_VFTA_ENTRY_SHIFT			5
#define E1000_VFTA_ENTRY_MASK			0x7F
#define E1000_VFTA_ENTRY_BIT_SHIFT_MASK		0x1F

#define E1000_HICR_EN			0x01	/* Enable bit - RO */
/* Driver sets this bit when done to put command in RAM */
#define E1000_HICR_C			0x02
#define E1000_HICR_SV			0x04	/* Status Validity */
#define E1000_HICR_FW_RESET_ENABLE	0x40
#define E1000_HICR_FW_RESET		0x80

/* Intel(R) Active Management Technology signature */
#define E1000_IAMT_SIGNATURE		0x544D4149

#endif
