/*
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _ISERIES_MAIN_STORE_H
#define _ISERIES_MAIN_STORE_H

/* Main Store Vpd for Condor,iStar,sStar */
struct IoHriMainStoreSegment4 {
	u8	msArea0Exists:1;
	u8	msArea1Exists:1;
	u8	msArea2Exists:1;
	u8	msArea3Exists:1;
	u8	reserved1:4;
	u8	reserved2;

	u8	msArea0Functional:1;
	u8	msArea1Functional:1;
	u8	msArea2Functional:1;
	u8	msArea3Functional:1;
	u8	reserved3:4;
	u8	reserved4;

	u32	totalMainStore;

	u64	msArea0Ptr;
	u64	msArea1Ptr;
	u64	msArea2Ptr;
	u64	msArea3Ptr;

	u32	cardProductionLevel;

	u32	msAdrHole;

	u8	msArea0HasRiserVpd:1;
	u8	msArea1HasRiserVpd:1;
	u8	msArea2HasRiserVpd:1;
	u8	msArea3HasRiserVpd:1;
	u8	reserved5:4;
	u8	reserved6;
	u16	reserved7;

	u8	reserved8[28];

	u64	nonInterleavedBlocksStartAdr;
	u64	nonInterleavedBlocksEndAdr;
};

/* Main Store VPD for Power4 */
struct __attribute((packed)) IoHriMainStoreChipInfo1 {
	u32	chipMfgID;
	char	chipECLevel[4];
};

struct IoHriMainStoreVpdIdData {
	char	typeNumber[4];
	char	modelNumber[4];
	char	partNumber[12];
	char	serialNumber[12];
};

struct	__attribute((packed)) IoHriMainStoreVpdFruData {
	char	fruLabel[8];
	u8	numberOfSlots;
	u8	pluggingType;
	u16	slotMapIndex;
};

struct  __attribute((packed)) IoHriMainStoreAdrRangeBlock {
	void	*blockStart;
	void	*blockEnd;
	u32	blockProcChipId;
};

#define MaxAreaAdrRangeBlocks 4

struct __attribute((packed)) IoHriMainStoreArea4 {
	u32	msVpdFormat;
	u8	containedVpdType;
	u8	reserved1;
	u16	reserved2;

	u64	msExists;
	u64	msFunctional;

	u32	memorySize;
	u32	procNodeId;

	u32	numAdrRangeBlocks;
	struct IoHriMainStoreAdrRangeBlock xAdrRangeBlock[MaxAreaAdrRangeBlocks];

	struct IoHriMainStoreChipInfo1	chipInfo0;
	struct IoHriMainStoreChipInfo1	chipInfo1;
	struct IoHriMainStoreChipInfo1	chipInfo2;
	struct IoHriMainStoreChipInfo1	chipInfo3;
	struct IoHriMainStoreChipInfo1	chipInfo4;
	struct IoHriMainStoreChipInfo1	chipInfo5;
	struct IoHriMainStoreChipInfo1	chipInfo6;
	struct IoHriMainStoreChipInfo1	chipInfo7;

	void	*msRamAreaArray;
	u32	msRamAreaArrayNumEntries;
	u32	msRamAreaArrayEntrySize;

	u32	numaDimmExists;
	u32	numaDimmFunctional;
	void	*numaDimmArray;
	u32	numaDimmArrayNumEntries;
	u32	numaDimmArrayEntrySize;

	struct IoHriMainStoreVpdIdData idData;

	u64	powerData;
	u64	cardAssemblyPartNum;
	u64	chipSerialNum;

	u64	reserved3;
	char	reserved4[16];

	struct IoHriMainStoreVpdFruData fruData;

	u8	vpdPortNum;
	u8	reserved5;
	u8	frameId;
	u8	rackUnit;
	char	asciiKeywordVpd[256];
	u32	reserved6;
};


struct IoHriMainStoreSegment5 {
	u16	reserved1;
	u8	reserved2;
	u8	msVpdFormat;

	u32	totalMainStore;
	u64	maxConfiguredMsAdr;

	struct IoHriMainStoreArea4	*msAreaArray;
	u32	msAreaArrayNumEntries;
	u32	msAreaArrayEntrySize;

	u32	msAreaExists;
	u32	msAreaFunctional;

	u64	reserved3;
};

extern u64	xMsVpd[];

#endif	/* _ISERIES_MAIN_STORE_H */
