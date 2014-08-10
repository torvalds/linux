/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTW_IOL_H_
#define __RTW_IOL_H_

#include <osdep_service.h>
#include <drv_types.h>

#define IOREG_CMD_END_LEN	4

struct ioreg_cfg {
	u8	length;
	u8	cmd_id;
	__le16	address;
	__le32	data;
	__le32  mask;
};

enum ioreg_cmd {
	IOREG_CMD_LLT		= 0x01,
	IOREG_CMD_REFUSE	= 0x02,
	IOREG_CMD_EFUSE_PATH	= 0x03,
	IOREG_CMD_WB_REG	= 0x04,
	IOREG_CMD_WW_REG	= 0x05,
	IOREG_CMD_WD_REG	= 0x06,
	IOREG_CMD_W_RF		= 0x07,
	IOREG_CMD_DELAY_US	= 0x10,
	IOREG_CMD_DELAY_MS	= 0x11,
	IOREG_CMD_END		= 0xFF,
};

bool rtw_IOL_applied(struct adapter  *adapter);

#endif /* __RTW_IOL_H_ */
