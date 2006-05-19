/*
 * Provides the Hypervisor PCI calls for iSeries Linux Parition.
 * Copyright (C) 2001  <Wayne G Holm> <IBM Corporation>
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
 * along with this program; if not, write to the:
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA  02111-1307  USA
 *
 * Change Activity:
 *   Created, Jan 9, 2001
 */

#ifndef _PLATFORMS_ISERIES_CALL_PCI_H
#define _PLATFORMS_ISERIES_CALL_PCI_H

#include <asm/iseries/hv_call_sc.h>
#include <asm/iseries/hv_types.h>

/*
 * DSA == Direct Select Address
 * this struct must be 64 bits in total
 */
struct HvCallPci_DsaAddr {
	u16		busNumber;		/* PHB index? */
	u8		subBusNumber;		/* PCI bus number? */
	u8		deviceId;		/* device and function? */
	u8		barNumber;
	u8		reserved[3];
};

union HvDsaMap {
	u64	DsaAddr;
	struct HvCallPci_DsaAddr Dsa;
};

struct HvCallPci_LoadReturn {
	u64		rc;
	u64		value;
};

enum HvCallPci_DeviceType {
	HvCallPci_NodeDevice	= 1,
	HvCallPci_SpDevice	= 2,
	HvCallPci_IopDevice     = 3,
	HvCallPci_BridgeDevice	= 4,
	HvCallPci_MultiFunctionDevice = 5,
	HvCallPci_IoaDevice	= 6
};


struct HvCallPci_DeviceInfo {
	u32	deviceType;		/* See DeviceType enum for values */
};

struct HvCallPci_BusUnitInfo {
	u32	sizeReturned;		/* length of data returned */
	u32	deviceType;		/* see DeviceType enum for values */
};

struct HvCallPci_BridgeInfo {
	struct HvCallPci_BusUnitInfo busUnitInfo;  /* Generic bus unit info */
	u8		subBusNumber;	/* Bus number of secondary bus */
	u8		maxAgents;	/* Max idsels on secondary bus */
        u8              maxSubBusNumber; /* Max Sub Bus */
	u8		logicalSlotNumber; /* Logical Slot Number for IOA */
};


/*
 * Maximum BusUnitInfo buffer size.  Provided for clients so
 * they can allocate a buffer big enough for any type of bus
 * unit.  Increase as needed.
 */
enum {HvCallPci_MaxBusUnitInfoSize = 128};

struct HvCallPci_BarParms {
	u64		vaddr;
	u64		raddr;
	u64		size;
	u64		protectStart;
	u64		protectEnd;
	u64		relocationOffset;
	u64		pciAddress;
	u64		reserved[3];
};

enum HvCallPci_VpdType {
	HvCallPci_BusVpd	= 1,
	HvCallPci_BusAdapterVpd	= 2
};

#define HvCallPciConfigLoad8		HvCallPci + 0
#define HvCallPciConfigLoad16		HvCallPci + 1
#define HvCallPciConfigLoad32		HvCallPci + 2
#define HvCallPciConfigStore8		HvCallPci + 3
#define HvCallPciConfigStore16		HvCallPci + 4
#define HvCallPciConfigStore32		HvCallPci + 5
#define HvCallPciEoi			HvCallPci + 16
#define HvCallPciGetBarParms		HvCallPci + 18
#define HvCallPciMaskFisr		HvCallPci + 20
#define HvCallPciUnmaskFisr		HvCallPci + 21
#define HvCallPciSetSlotReset		HvCallPci + 25
#define HvCallPciGetDeviceInfo		HvCallPci + 27
#define HvCallPciGetCardVpd		HvCallPci + 28
#define HvCallPciBarLoad8		HvCallPci + 40
#define HvCallPciBarLoad16		HvCallPci + 41
#define HvCallPciBarLoad32		HvCallPci + 42
#define HvCallPciBarLoad64		HvCallPci + 43
#define HvCallPciBarStore8		HvCallPci + 44
#define HvCallPciBarStore16		HvCallPci + 45
#define HvCallPciBarStore32		HvCallPci + 46
#define HvCallPciBarStore64		HvCallPci + 47
#define HvCallPciMaskInterrupts		HvCallPci + 48
#define HvCallPciUnmaskInterrupts	HvCallPci + 49
#define HvCallPciGetBusUnitInfo		HvCallPci + 50

static inline u64 HvCallPci_configLoad16(u16 busNumber, u8 subBusNumber,
		u8 deviceId, u32 offset, u16 *value)
{
	struct HvCallPci_DsaAddr dsa;
	struct HvCallPci_LoadReturn retVal;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumber;
	dsa.subBusNumber = subBusNumber;
	dsa.deviceId = deviceId;

	HvCall3Ret16(HvCallPciConfigLoad16, &retVal, *(u64 *)&dsa, offset, 0);

	*value = retVal.value;

	return retVal.rc;
}

static inline u64 HvCallPci_configLoad32(u16 busNumber, u8 subBusNumber,
		u8 deviceId, u32 offset, u32 *value)
{
	struct HvCallPci_DsaAddr dsa;
	struct HvCallPci_LoadReturn retVal;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumber;
	dsa.subBusNumber = subBusNumber;
	dsa.deviceId = deviceId;

	HvCall3Ret16(HvCallPciConfigLoad32, &retVal, *(u64 *)&dsa, offset, 0);

	*value = retVal.value;

	return retVal.rc;
}

static inline u64 HvCallPci_configStore8(u16 busNumber, u8 subBusNumber,
		u8 deviceId, u32 offset, u8 value)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumber;
	dsa.subBusNumber = subBusNumber;
	dsa.deviceId = deviceId;

	return HvCall4(HvCallPciConfigStore8, *(u64 *)&dsa, offset, value, 0);
}

static inline u64 HvCallPci_eoi(u16 busNumberParm, u8 subBusParm,
		u8 deviceIdParm)
{
	struct HvCallPci_DsaAddr dsa;
	struct HvCallPci_LoadReturn retVal;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceIdParm;

	HvCall1Ret16(HvCallPciEoi, &retVal, *(u64*)&dsa);

	return retVal.rc;
}

static inline u64 HvCallPci_getBarParms(u16 busNumberParm, u8 subBusParm,
		u8 deviceIdParm, u8 barNumberParm, u64 parms, u32 sizeofParms)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceIdParm;
	dsa.barNumber = barNumberParm;

	return HvCall3(HvCallPciGetBarParms, *(u64*)&dsa, parms, sizeofParms);
}

static inline u64 HvCallPci_maskFisr(u16 busNumberParm, u8 subBusParm,
		u8 deviceIdParm, u64 fisrMask)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceIdParm;

	return HvCall2(HvCallPciMaskFisr, *(u64*)&dsa, fisrMask);
}

static inline u64 HvCallPci_unmaskFisr(u16 busNumberParm, u8 subBusParm,
		u8 deviceIdParm, u64 fisrMask)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceIdParm;

	return HvCall2(HvCallPciUnmaskFisr, *(u64*)&dsa, fisrMask);
}

static inline u64 HvCallPci_getDeviceInfo(u16 busNumberParm, u8 subBusParm,
		u8 deviceNumberParm, u64 parms, u32 sizeofParms)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceNumberParm << 4;

	return HvCall3(HvCallPciGetDeviceInfo, *(u64*)&dsa, parms, sizeofParms);
}

static inline u64 HvCallPci_maskInterrupts(u16 busNumberParm, u8 subBusParm,
		u8 deviceIdParm, u64 interruptMask)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceIdParm;

	return HvCall2(HvCallPciMaskInterrupts, *(u64*)&dsa, interruptMask);
}

static inline u64 HvCallPci_unmaskInterrupts(u16 busNumberParm, u8 subBusParm,
		u8 deviceIdParm, u64 interruptMask)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceIdParm;

	return HvCall2(HvCallPciUnmaskInterrupts, *(u64*)&dsa, interruptMask);
}

static inline u64 HvCallPci_getBusUnitInfo(u16 busNumberParm, u8 subBusParm,
		u8 deviceIdParm, u64 parms, u32 sizeofParms)
{
	struct HvCallPci_DsaAddr dsa;

	*((u64*)&dsa) = 0;

	dsa.busNumber = busNumberParm;
	dsa.subBusNumber = subBusParm;
	dsa.deviceId = deviceIdParm;

	return HvCall3(HvCallPciGetBusUnitInfo, *(u64*)&dsa, parms,
			sizeofParms);
}

static inline int HvCallPci_getBusVpd(u16 busNumParm, u64 destParm,
		u16 sizeParm)
{
	u64 xRc = HvCall4(HvCallPciGetCardVpd, busNumParm, destParm,
			sizeParm, HvCallPci_BusVpd);
	if (xRc == -1)
		return -1;
	else
		return xRc & 0xFFFF;
}

#endif /* _PLATFORMS_ISERIES_CALL_PCI_H */
