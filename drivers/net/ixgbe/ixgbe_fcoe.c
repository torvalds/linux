/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2009 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/


#include "ixgbe.h"
#include <linux/if_ether.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/libfc.h>
#include <scsi/libfcoe.h>

/**
 * ixgbe_configure_fcoe - configures registers for fcoe at start
 * @adapter: ptr to ixgbe adapter
 *
 * This sets up FCoE related registers
 *
 * Returns : none
 */
void ixgbe_configure_fcoe(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;

	/* L2 filter for FCoE: default to queue 0 */
	IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE),
			(ETH_P_FCOE | IXGBE_ETQF_FCOE | IXGBE_ETQF_FILTER_EN));
	IXGBE_WRITE_REG(hw, IXGBE_FCRECTL, 0);
	IXGBE_WRITE_REG(hw, IXGBE_ETQS(IXGBE_ETQF_FILTER_FCOE),
			IXGBE_ETQS_QUEUE_EN);
	IXGBE_WRITE_REG(hw, IXGBE_FCRXCTRL,
			IXGBE_FCRXCTRL_FCOELLI |
			IXGBE_FCRXCTRL_FCCRCBO |
			(FC_FCOE_VER << IXGBE_FCRXCTRL_FCOEVER_SHIFT));
}
