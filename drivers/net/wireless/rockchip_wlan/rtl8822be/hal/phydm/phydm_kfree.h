
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

#ifndef	__PHYDMKFREE_H__
#define    __PHYDKFREE_H__

#define KFREE_VERSION	"1.0"

typedef enum tag_phydm_kfree_channeltosw {
	PHYDM_2G = 0,
	PHYDM_5GLB1 = 1,
	PHYDM_5GLB2 = 2,
	PHYDM_5GMB1 = 3,
	PHYDM_5GMB2 = 4,
	PHYDM_5GHB = 5,
} PHYDM_KFREE_CHANNELTOSW;


VOID
phydm_ConfigKFree(
	IN	PVOID	pDM_VOID,
	IN	u1Byte	channelToSW,
	IN	pu1Byte	kfreeTable
);


#endif
