/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: key.h
 *
 * Purpose: Implement functions for 802.11i Key management
 *
 * Author: Jerry Chen
 *
 * Date: May 29, 2003
 *
 */

#ifndef __KEY_H__
#define __KEY_H__

#include "ttype.h"
#include "tether.h"
#include "80211mgr.h"

/*---------------------  Export Definitions -------------------------*/
#define MAX_GROUP_KEY       4
#define MAX_KEY_TABLE       11
#define MAX_KEY_LEN         32
#define AES_KEY_LEN         16

#define AUTHENTICATOR_KEY   0x10000000
#define USE_KEYRSC          0x20000000
#define PAIRWISE_KEY        0x40000000
#define TRANSMIT_KEY        0x80000000

#define GROUP_KEY           0x00000000

#define KEY_CTL_WEP         0x00
#define KEY_CTL_NONE        0x01
#define KEY_CTL_TKIP        0x02
#define KEY_CTL_CCMP        0x03
#define KEY_CTL_INVALID     0xFF

typedef struct tagSKeyItem
{
	bool bKeyValid;
	unsigned long uKeyLength;
	unsigned char abyKey[MAX_KEY_LEN];
	QWORD       KeyRSC;
	unsigned long dwTSC47_16;
	unsigned short wTSC15_0;
	unsigned char byCipherSuite;
	unsigned char byReserved0;
	unsigned long dwKeyIndex;
	void *pvKeyTable;
} SKeyItem, *PSKeyItem; //64

typedef struct tagSKeyTable
{
	unsigned char abyBSSID[ETH_ALEN];  //6
	unsigned char byReserved0[2];              //8
	SKeyItem    PairwiseKey;
	SKeyItem    GroupKey[MAX_GROUP_KEY]; //64*5 = 320, 320+8=328
	unsigned long dwGTKeyIndex;            // GroupTransmitKey Index
	bool bInUse;
	//2006-1116-01,<Modify> by NomadZhao
	//unsigned short wKeyCtl;
	//bool bSoftWEP;
	bool bSoftWEP;
	unsigned short wKeyCtl;      // for address of wKeyCtl at align 4

	unsigned char byReserved1[6];
} SKeyTable, *PSKeyTable; //348

typedef struct tagSKeyManagement
{
	SKeyTable   KeyTable[MAX_KEY_TABLE];
} SKeyManagement, *PSKeyManagement;

/*---------------------  Export Types  ------------------------------*/

/*---------------------  Export Macros ------------------------------*/

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

void KeyvInitTable(PSKeyManagement pTable, unsigned long dwIoBase);

bool KeybGetKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyIndex,
	PSKeyItem       *pKey
);

bool KeybSetKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyIndex,
	unsigned long uKeyLength,
	PQWORD          pKeyRSC,
	unsigned char *pbyKey,
	unsigned char byKeyDecMode,
	unsigned long dwIoBase,
	unsigned char byLocalID
);

bool KeybSetDefaultKey(
	PSKeyManagement pTable,
	unsigned long dwKeyIndex,
	unsigned long uKeyLength,
	PQWORD          pKeyRSC,
	unsigned char *pbyKey,
	unsigned char byKeyDecMode,
	unsigned long dwIoBase,
	unsigned char byLocalID
);

bool KeybRemoveKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyIndex,
	unsigned long dwIoBase
);

bool KeybGetTransmitKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyType,
	PSKeyItem       *pKey
);

bool KeybCheckPairewiseKey(
	PSKeyManagement pTable,
	PSKeyItem       *pKey
);

bool KeybRemoveAllKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwIoBase
);

void KeyvRemoveWEPKey(
	PSKeyManagement pTable,
	unsigned long dwKeyIndex,
	unsigned long dwIoBase
);

void KeyvRemoveAllWEPKey(
	PSKeyManagement pTable,
	unsigned long dwIoBase
);

bool KeybSetAllGroupKey(
	PSKeyManagement pTable,
	unsigned long dwKeyIndex,
	unsigned long uKeyLength,
	PQWORD          pKeyRSC,
	unsigned char *pbyKey,
	unsigned char byKeyDecMode,
	unsigned long dwIoBase,
	unsigned char byLocalID
);

#endif // __KEY_H__
