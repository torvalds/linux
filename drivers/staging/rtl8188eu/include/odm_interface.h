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

#ifndef	__ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__

enum odm_h2c_cmd {
	ODM_H2C_RSSI_REPORT = 0,
	ODM_H2C_PSD_RESULT = 1,
	ODM_H2C_PathDiv = 2,
	ODM_MAX_H2CCMD
};

/*  ODM FW relative API. */
u32 ODM_FillH2CCmd(u8 *pH2CBuffer, u32 H2CBufferLen, u32 CmdNum,
		   u32 *pElementID, u32 *pCmdLen, u8 **pCmbBuffer,
		   u8 *CmdStartSeq);

#endif	/*  __ODM_INTERFACE_H__ */
