/** @file KeyMgmtApTypes_rom.h
 *
 *  @brief This file contains the key management type for ap
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#ifndef _KEYMGMTAPTYPES_ROM_H_
#define _KEYMGMTAPTYPES_ROM_H_

#include "wltypes.h"
#include "keyMgmtStaTypes.h"

#define KDE_IE_SIZE  (2)	// type+length of KDE_t
#define KDE_SIZE     (KDE_IE_SIZE + 4 )	// OUI+datatype of KDE_t
#define GTK_IE_SIZE (2)
#define KEYDATA_SIZE (4 + GTK_IE_SIZE + TK_SIZE)	//OUI+datatype+ GTK_IE+ GTK

typedef enum {
	HSK_NOT_STARTED,
	MSG1_PENDING,
	WAITING_4_MSG2,
	MSG3_PENDING,
	WAITING_4_MSG4,
	GRPMSG1_PENDING,
	WAITING_4_GRPMSG2,
	GRP_REKEY_MSG1_PENDING,
	WAITING_4_GRP_REKEY_MSG2,
	/* the relative positions of the different enum elements
	 ** should not be changed since FW code checks for even/odd
	 ** values at certain places.
	 */
	HSK_DUMMY_STATE,
	HSK_END
} keyMgmtState_e;

/* This sturcture is being accessed in rom code and should be kept intact. */
typedef struct {
	UINT16 staRsnCap;
	SecurityMode_t staSecType;
	Cipher_t staUcstCipher;
	UINT8 staAkmType;
	keyMgmtState_e keyMgmtState;
} apKeyMgmtInfoStaRom_t;

#endif
