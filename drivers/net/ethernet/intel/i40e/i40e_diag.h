/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_DIAG_H_
#define _I40E_DIAG_H_

#include "i40e_type.h"

enum i40e_lb_mode {
	I40E_LB_MODE_NONE       = 0x0,
	I40E_LB_MODE_PHY_LOCAL  = I40E_AQ_LB_PHY_LOCAL,
	I40E_LB_MODE_PHY_REMOTE = I40E_AQ_LB_PHY_REMOTE,
	I40E_LB_MODE_MAC_LOCAL  = I40E_AQ_LB_MAC_LOCAL,
};

struct i40e_diag_reg_test_info {
	u32 offset;	/* the base register */
	u32 mask;	/* bits that can be tested */
	u32 elements;	/* number of elements if array */
	u32 stride;	/* bytes between each element */
};

extern struct i40e_diag_reg_test_info i40e_reg_list[];

i40e_status i40e_diag_reg_test(struct i40e_hw *hw);
i40e_status i40e_diag_eeprom_test(struct i40e_hw *hw);

#endif /* _I40E_DIAG_H_ */
