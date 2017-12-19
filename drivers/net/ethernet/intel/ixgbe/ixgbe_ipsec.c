/*******************************************************************************
 *
 * Intel 10 Gigabit PCI Express Linux driver
 * Copyright(c) 2017 Oracle and/or its affiliates. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Linux NICS <linux.nics@intel.com>
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include "ixgbe.h"

/**
 * ixgbe_ipsec_set_tx_sa - set the Tx SA registers
 * @hw: hw specific details
 * @idx: register index to write
 * @key: key byte array
 * @salt: salt bytes
 **/
static void ixgbe_ipsec_set_tx_sa(struct ixgbe_hw *hw, u16 idx,
				  u32 key[], u32 salt)
{
	u32 reg;
	int i;

	for (i = 0; i < 4; i++)
		IXGBE_WRITE_REG(hw, IXGBE_IPSTXKEY(i), cpu_to_be32(key[3 - i]));
	IXGBE_WRITE_REG(hw, IXGBE_IPSTXSALT, cpu_to_be32(salt));
	IXGBE_WRITE_FLUSH(hw);

	reg = IXGBE_READ_REG(hw, IXGBE_IPSTXIDX);
	reg &= IXGBE_RXTXIDX_IPS_EN;
	reg |= idx << IXGBE_RXTXIDX_IDX_SHIFT | IXGBE_RXTXIDX_WRITE;
	IXGBE_WRITE_REG(hw, IXGBE_IPSTXIDX, reg);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_ipsec_set_rx_item - set an Rx table item
 * @hw: hw specific details
 * @idx: register index to write
 * @tbl: table selector
 *
 * Trigger the device to store into a particular Rx table the
 * data that has already been loaded into the input register
 **/
static void ixgbe_ipsec_set_rx_item(struct ixgbe_hw *hw, u16 idx,
				    enum ixgbe_ipsec_tbl_sel tbl)
{
	u32 reg;

	reg = IXGBE_READ_REG(hw, IXGBE_IPSRXIDX);
	reg &= IXGBE_RXTXIDX_IPS_EN;
	reg |= tbl << IXGBE_RXIDX_TBL_SHIFT |
	       idx << IXGBE_RXTXIDX_IDX_SHIFT |
	       IXGBE_RXTXIDX_WRITE;
	IXGBE_WRITE_REG(hw, IXGBE_IPSRXIDX, reg);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_ipsec_set_rx_sa - set up the register bits to save SA info
 * @hw: hw specific details
 * @idx: register index to write
 * @spi: security parameter index
 * @key: key byte array
 * @salt: salt bytes
 * @mode: rx decrypt control bits
 * @ip_idx: index into IP table for related IP address
 **/
static void ixgbe_ipsec_set_rx_sa(struct ixgbe_hw *hw, u16 idx, __be32 spi,
				  u32 key[], u32 salt, u32 mode, u32 ip_idx)
{
	int i;

	/* store the SPI (in bigendian) and IPidx */
	IXGBE_WRITE_REG(hw, IXGBE_IPSRXSPI, cpu_to_le32(spi));
	IXGBE_WRITE_REG(hw, IXGBE_IPSRXIPIDX, ip_idx);
	IXGBE_WRITE_FLUSH(hw);

	ixgbe_ipsec_set_rx_item(hw, idx, ips_rx_spi_tbl);

	/* store the key, salt, and mode */
	for (i = 0; i < 4; i++)
		IXGBE_WRITE_REG(hw, IXGBE_IPSRXKEY(i), cpu_to_be32(key[3 - i]));
	IXGBE_WRITE_REG(hw, IXGBE_IPSRXSALT, cpu_to_be32(salt));
	IXGBE_WRITE_REG(hw, IXGBE_IPSRXMOD, mode);
	IXGBE_WRITE_FLUSH(hw);

	ixgbe_ipsec_set_rx_item(hw, idx, ips_rx_key_tbl);
}

/**
 * ixgbe_ipsec_set_rx_ip - set up the register bits to save SA IP addr info
 * @hw: hw specific details
 * @idx: register index to write
 * @addr: IP address byte array
 **/
static void ixgbe_ipsec_set_rx_ip(struct ixgbe_hw *hw, u16 idx, __be32 addr[])
{
	int i;

	/* store the ip address */
	for (i = 0; i < 4; i++)
		IXGBE_WRITE_REG(hw, IXGBE_IPSRXIPADDR(i), cpu_to_le32(addr[i]));
	IXGBE_WRITE_FLUSH(hw);

	ixgbe_ipsec_set_rx_item(hw, idx, ips_rx_ip_tbl);
}

/**
 * ixgbe_ipsec_clear_hw_tables - because some tables don't get cleared on reset
 * @adapter: board private structure
 **/
static void ixgbe_ipsec_clear_hw_tables(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 buf[4] = {0, 0, 0, 0};
	u16 idx;

	/* disable Rx and Tx SA lookup */
	IXGBE_WRITE_REG(hw, IXGBE_IPSRXIDX, 0);
	IXGBE_WRITE_REG(hw, IXGBE_IPSTXIDX, 0);

	/* scrub the tables - split the loops for the max of the IP table */
	for (idx = 0; idx < IXGBE_IPSEC_MAX_RX_IP_COUNT; idx++) {
		ixgbe_ipsec_set_tx_sa(hw, idx, buf, 0);
		ixgbe_ipsec_set_rx_sa(hw, idx, 0, buf, 0, 0, 0);
		ixgbe_ipsec_set_rx_ip(hw, idx, (__be32 *)buf);
	}
	for (; idx < IXGBE_IPSEC_MAX_SA_COUNT; idx++) {
		ixgbe_ipsec_set_tx_sa(hw, idx, buf, 0);
		ixgbe_ipsec_set_rx_sa(hw, idx, 0, buf, 0, 0, 0);
	}
}

/**
 * ixgbe_init_ipsec_offload - initialize security registers for IPSec operation
 * @adapter: board private structure
 **/
void ixgbe_init_ipsec_offload(struct ixgbe_adapter *adapter)
{
	ixgbe_ipsec_clear_hw_tables(adapter);
}
