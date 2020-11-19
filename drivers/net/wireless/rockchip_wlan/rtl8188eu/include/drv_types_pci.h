/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __DRV_TYPES_PCI_H__
#define __DRV_TYPES_PCI_H__


#ifdef PLATFORM_LINUX
	#include <linux/pci.h>
#endif


#define	INTEL_VENDOR_ID				0x8086
#define	SIS_VENDOR_ID					0x1039
#define	ATI_VENDOR_ID					0x1002
#define	ATI_DEVICE_ID					0x7914
#define	AMD_VENDOR_ID					0x1022

#define	PCI_MAX_BRIDGE_NUMBER			255
#define	PCI_MAX_DEVICES				32
#define	PCI_MAX_FUNCTION				8

#define	PCI_CONF_ADDRESS   				0x0CF8   /* PCI Configuration Space Address */
#define	PCI_CONF_DATA					0x0CFC   /* PCI Configuration Space Data */

#define	PCI_CLASS_BRIDGE_DEV			0x06
#define	PCI_SUBCLASS_BR_PCI_TO_PCI	0x04

#define	PCI_CAPABILITY_ID_PCI_EXPRESS	0x10

#define	U1DONTCARE					0xFF
#define	U2DONTCARE					0xFFFF
#define	U4DONTCARE					0xFFFFFFFF

#define PCI_VENDER_ID_REALTEK		0x10ec

#define HAL_HW_PCI_8180_DEVICE_ID	0x8180
#define HAL_HW_PCI_8185_DEVICE_ID           	0x8185	/* 8185 or 8185b */
#define HAL_HW_PCI_8188_DEVICE_ID           	0x8188	/* 8185b		 */
#define HAL_HW_PCI_8198_DEVICE_ID           	0x8198	/* 8185b		 */
#define HAL_HW_PCI_8190_DEVICE_ID           	0x8190	/* 8190 */
#define HAL_HW_PCI_8723E_DEVICE_ID		0x8723	/* 8723E */
#define HAL_HW_PCI_8192_DEVICE_ID           	0x8192	/* 8192 PCI-E */
#define HAL_HW_PCI_8192SE_DEVICE_ID		0x8192	/* 8192 SE */
#define HAL_HW_PCI_8174_DEVICE_ID           	0x8174	/* 8192 SE */
#define HAL_HW_PCI_8173_DEVICE_ID           	0x8173	/* 8191 SE Crab */
#define HAL_HW_PCI_8172_DEVICE_ID           	0x8172	/* 8191 SE RE */
#define HAL_HW_PCI_8171_DEVICE_ID           	0x8171	/* 8191 SE Unicron */
#define HAL_HW_PCI_0045_DEVICE_ID			0x0045	/* 8190 PCI for Ceraga */
#define HAL_HW_PCI_0046_DEVICE_ID			0x0046	/* 8190 Cardbus for Ceraga */
#define HAL_HW_PCI_0044_DEVICE_ID			0x0044	/* 8192e PCIE for Ceraga */
#define HAL_HW_PCI_0047_DEVICE_ID			0x0047	/* 8192e Express Card for Ceraga */
#define HAL_HW_PCI_700F_DEVICE_ID			0x700F
#define HAL_HW_PCI_701F_DEVICE_ID			0x701F
#define HAL_HW_PCI_DLINK_DEVICE_ID		0x3304
#define HAL_HW_PCI_8188EE_DEVICE_ID		0x8179

#define HAL_MEMORY_MAPPED_IO_RANGE_8190PCI 		0x1000     /* 8190 support 16 pages of IO registers */
#define HAL_HW_PCI_REVISION_ID_8190PCI			0x00
#define HAL_MEMORY_MAPPED_IO_RANGE_8192PCIE	0x4000	/* 8192 support 16 pages of IO registers */
#define HAL_HW_PCI_REVISION_ID_8192PCIE			0x01
#define HAL_MEMORY_MAPPED_IO_RANGE_8192SE		0x4000	/* 8192 support 16 pages of IO registers */
#define HAL_HW_PCI_REVISION_ID_8192SE			0x10
#define HAL_HW_PCI_REVISION_ID_8192CE			0x1
#define HAL_MEMORY_MAPPED_IO_RANGE_8192CE		0x4000	/* 8192 support 16 pages of IO registers */
#define HAL_HW_PCI_REVISION_ID_8192DE			0x0
#define HAL_MEMORY_MAPPED_IO_RANGE_8192DE		0x4000	/* 8192 support 16 pages of IO registers */

enum pci_bridge_vendor {
	PCI_BRIDGE_VENDOR_INTEL = 0x0,/* 0b'0000,0001 */
	PCI_BRIDGE_VENDOR_ATI, /* = 0x02, */ /* 0b'0000,0010 */
	PCI_BRIDGE_VENDOR_AMD, /* = 0x04, */ /* 0b'0000,0100 */
	PCI_BRIDGE_VENDOR_SIS ,/* = 0x08, */ /* 0b'0000,1000 */
	PCI_BRIDGE_VENDOR_UNKNOWN, /* = 0x40, */ /* 0b'0100,0000 */
	PCI_BRIDGE_VENDOR_MAX ,/* = 0x80 */
} ;

/* copy this data structor defination from MSDN SDK */
typedef struct _PCI_COMMON_CONFIG {
	u16	VendorID;
	u16	DeviceID;
	u16	Command;
	u16	Status;
	u8	RevisionID;
	u8	ProgIf;
	u8	SubClass;
	u8	BaseClass;
	u8	CacheLineSize;
	u8	LatencyTimer;
	u8	HeaderType;
	u8	BIST;

	union {
		struct _PCI_HEADER_TYPE_0 {
			u32	BaseAddresses[6];
			u32	CIS;
			u16	SubVendorID;
			u16	SubSystemID;
			u32	ROMBaseAddress;
			u8	CapabilitiesPtr;
			u8	Reserved1[3];
			u32	Reserved2;

			u8	InterruptLine;
			u8	InterruptPin;
			u8	MinimumGrant;
			u8	MaximumLatency;
		} type0;
#if 0
		struct _PCI_HEADER_TYPE_1 {
			u32 BaseAddresses[PCI_TYPE1_ADDRESSES];
			u8 PrimaryBusNumber;
			u8 SecondaryBusNumber;
			u8 SubordinateBusNumber;
			u8 SecondaryLatencyTimer;
			u8 IOBase;
			u8 IOLimit;
			u16 SecondaryStatus;
			u16 MemoryBase;
			u16 MemoryLimit;
			u16 PrefetchableMemoryBase;
			u16 PrefetchableMemoryLimit;
			u32 PrefetchableMemoryBaseUpper32;
			u32 PrefetchableMemoryLimitUpper32;
			u16 IOBaseUpper;
			u16 IOLimitUpper;
			u32 Reserved2;
			u32 ExpansionROMBase;
			u8 InterruptLine;
			u8 InterruptPin;
			u16 BridgeControl;
		} type1;

		struct _PCI_HEADER_TYPE_2 {
			u32 BaseAddress;
			u8 CapabilitiesPtr;
			u8 Reserved2;
			u16 SecondaryStatus;
			u8 PrimaryBusNumber;
			u8 CardbusBusNumber;
			u8 SubordinateBusNumber;
			u8 CardbusLatencyTimer;
			u32 MemoryBase0;
			u32 MemoryLimit0;
			u32 MemoryBase1;
			u32 MemoryLimit1;
			u16 IOBase0_LO;
			u16 IOBase0_HI;
			u16 IOLimit0_LO;
			u16 IOLimit0_HI;
			u16 IOBase1_LO;
			u16 IOBase1_HI;
			u16 IOLimit1_LO;
			u16 IOLimit1_HI;
			u8 InterruptLine;
			u8 InterruptPin;
			u16 BridgeControl;
			u16 SubVendorID;
			u16 SubSystemID;
			u32 LegacyBaseAddress;
			u8 Reserved3[56];
			u32 SystemControl;
			u8 MultiMediaControl;
			u8 GeneralStatus;
			u8 Reserved4[2];
			u8 GPIO0Control;
			u8 GPIO1Control;
			u8 GPIO2Control;
			u8 GPIO3Control;
			u32 IRQMuxRouting;
			u8 RetryStatus;
			u8 CardControl;
			u8 DeviceControl;
			u8 Diagnostic;
		} type2;
#endif
	} u;

	u8	DeviceSpecific[108];
} PCI_COMMON_CONFIG , *PPCI_COMMON_CONFIG;

typedef struct _RT_PCI_CAPABILITIES_HEADER {
	u8   CapabilityID;
	u8   Next;
} RT_PCI_CAPABILITIES_HEADER, *PRT_PCI_CAPABILITIES_HEADER;

struct pci_priv {
	BOOLEAN		pci_clk_req;

	u8	pciehdr_offset;
	/* PCIeCap is only differece between B-cut and C-cut. */
	/* Configuration Space offset 72[7:4] */
	/* 0: A/B cut */
	/* 1: C cut and later. */
	u8	pcie_cap;
	u8	linkctrl_reg;

	u8	busnumber;
	u8	devnumber;
	u8	funcnumber;

	u8	pcibridge_busnum;
	u8	pcibridge_devnum;
	u8	pcibridge_funcnum;
	u8	pcibridge_vendor;
	u16	pcibridge_vendorid;
	u16	pcibridge_deviceid;
	u8	pcibridge_pciehdr_offset;
	u8	pcibridge_linkctrlreg;

	u8	amd_l1_patch;
};

typedef struct _RT_ISR_CONTENT {
	union {
		u32			IntArray[2];
		u32			IntReg4Byte;
		u16			IntReg2Byte;
	};
} RT_ISR_CONTENT, *PRT_ISR_CONTENT;

/* #define RegAddr(addr)           (addr + 0xB2000000UL) */
/* some platform macros will def here */
static inline void NdisRawWritePortUlong(u32 port,  u32 val)
{
	outl(val, port);
	/* writel(val, (u8 *)RegAddr(port));	 */
}

static inline void NdisRawWritePortUchar(u32 port,  u8 val)
{
	outb(val, port);
	/* writeb(val, (u8 *)RegAddr(port)); */
}

static inline void NdisRawReadPortUchar(u32 port, u8 *pval)
{
	*pval = inb(port);
	/* *pval = readb((u8 *)RegAddr(port)); */
}

static inline void NdisRawReadPortUshort(u32 port, u16 *pval)
{
	*pval = inw(port);
	/* *pval = readw((u8 *)RegAddr(port)); */
}

static inline void NdisRawReadPortUlong(u32 port, u32 *pval)
{
	*pval = inl(port);
	/* *pval = readl((u8 *)RegAddr(port)); */
}


#endif
