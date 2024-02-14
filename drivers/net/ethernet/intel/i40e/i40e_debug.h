/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Intel Corporation. */

#ifndef _I40E_DEBUG_H_
#define _I40E_DEBUG_H_

#include <linux/dev_printk.h>

/* debug masks - set these bits in hw->debug_mask to control output */
enum i40e_debug_mask {
	I40E_DEBUG_INIT			= 0x00000001,
	I40E_DEBUG_RELEASE		= 0x00000002,

	I40E_DEBUG_LINK			= 0x00000010,
	I40E_DEBUG_PHY			= 0x00000020,
	I40E_DEBUG_HMC			= 0x00000040,
	I40E_DEBUG_NVM			= 0x00000080,
	I40E_DEBUG_LAN			= 0x00000100,
	I40E_DEBUG_FLOW			= 0x00000200,
	I40E_DEBUG_DCB			= 0x00000400,
	I40E_DEBUG_DIAG			= 0x00000800,
	I40E_DEBUG_FD			= 0x00001000,
	I40E_DEBUG_PACKAGE		= 0x00002000,
	I40E_DEBUG_IWARP		= 0x00F00000,
	I40E_DEBUG_AQ_MESSAGE		= 0x01000000,
	I40E_DEBUG_AQ_DESCRIPTOR	= 0x02000000,
	I40E_DEBUG_AQ_DESC_BUFFER	= 0x04000000,
	I40E_DEBUG_AQ_COMMAND		= 0x06000000,
	I40E_DEBUG_AQ			= 0x0F000000,

	I40E_DEBUG_USER			= 0xF0000000,

	I40E_DEBUG_ALL			= 0xFFFFFFFF
};

struct i40e_hw;
struct device *i40e_hw_to_dev(struct i40e_hw *hw);

#define hw_dbg(hw, S, A...) dev_dbg(i40e_hw_to_dev(hw), S, ##A)
#define hw_warn(hw, S, A...) dev_warn(i40e_hw_to_dev(hw), S, ##A)

#define i40e_debug(h, m, s, ...)				\
do {								\
	if (((m) & (h)->debug_mask))				\
		dev_info(i40e_hw_to_dev(hw), s, ##__VA_ARGS__);	\
} while (0)

#endif /* _I40E_DEBUG_H_ */
