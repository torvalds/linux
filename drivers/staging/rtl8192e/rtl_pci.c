/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 *****************************************************************************/
#include "rtl_pci.h"
#include "rtl_core.h"

#if defined RTL8192CE || defined RTL8192SE
bool
rtl8192_get_LinkControl_field(
	struct net_device *dev,
	u8			BusNum,
	u8			DevNum,
	u8			FuncNum
	)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	RT_PCI_CAPABILITIES_HEADER		CapabilityHdr;
	unsigned char	CapabilityOffset, Num4Bytes;
	u32			PciCfgAddrPort=0;
	u8			LinkCtrlReg;
	bool			Status = false;

	if ( BusNum == 0xff && DevNum == 0xff && FuncNum == 0xff ){
		printk("GetLinkControlField(): Fail to find PCIe Capability\n");
		return false;
	}


	PciCfgAddrPort= (BusNum<< 16)|(DevNum << 11)|(FuncNum <<  8)|(1 << 31);


	Num4Bytes = 0x34/4;
	NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort+( Num4Bytes<< 2));
	NdisRawReadPortUchar(PCI_CONF_DATA, &CapabilityOffset);


	while (CapabilityOffset != 0)
	{
		Num4Bytes = CapabilityOffset/4;


		NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort+( Num4Bytes<< 2));
		NdisRawReadPortUshort(PCI_CONF_DATA, (u16*)&CapabilityHdr);

		if (CapabilityHdr.CapabilityID == PCI_CAPABILITY_ID_PCI_EXPRESS)
		{
			break;
		}
		else
		{
			CapabilityOffset = CapabilityHdr.Next;
		}
	}


	if (CapabilityHdr.CapabilityID == PCI_CAPABILITY_ID_PCI_EXPRESS)
	{
		Num4Bytes = (CapabilityOffset+0x10)/4;

		NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort+(Num4Bytes << 2));
		NdisRawReadPortUchar(PCI_CONF_DATA, &LinkCtrlReg);

		priv->NdisAdapter.PciBridgePCIeHdrOffset = CapabilityOffset;
		priv->NdisAdapter.PciBridgeLinkCtrlReg = LinkCtrlReg;

		Status = true;
	}
	else
	{
		printk("GetLinkControlField(): Cannot Find PCIe Capability\n");
	}

	return Status;
}

bool
rtl8192_get_pci_BusInfo(
	struct net_device *dev,
	u16			VendorId,
	u16			DeviceId,
	u8			IRQL,
	u8			BaseCode,
	u8			SubClass,
	u8			filed19val,
	u8*			BusNum,
	u8*			DevNum,
	u8*			FuncNum
	)
{

	u8			busNumIdx, deviceNumIdx, functionNumIdx;
	u32			PciCfgAddrPort=0;
	u32			devVenID = 0, classCode, field19, headertype;
	u16			venId, devId;
	u8			basec, subc, irqLine;
	u16			RegOffset;
	bool			bSingleFunc = false;
	bool			bBridgeChk = false;

	*BusNum = 0xFF;
	*DevNum = 0xFF;
	*FuncNum = 0xFF;

	if ((BaseCode == PCI_CLASS_BRIDGE_DEV) && (SubClass==PCI_SUBCLASS_BR_PCI_TO_PCI) && (filed19val==U1DONTCARE))
		bBridgeChk = true;

	for (busNumIdx = 0; busNumIdx < PCI_MAX_BRIDGE_NUMBER  ; busNumIdx++)
	{
		for (deviceNumIdx = 0; deviceNumIdx < PCI_MAX_DEVICES; deviceNumIdx ++)
		{
			bSingleFunc = false;
			for (functionNumIdx = 0; functionNumIdx < PCI_MAX_FUNCTION; functionNumIdx++)
			{

				if (functionNumIdx == 0)
				{
					PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31);
					NdisRawWritePortUlong(PCI_CONF_ADDRESS ,  PciCfgAddrPort + (3 << 2));
					NdisRawReadPortUlong(PCI_CONF_DATA, &headertype);
					headertype = ((headertype >> 16) & 0x0080) >> 7;
					if ( headertype == 0)
						bSingleFunc = true;
				}
				else
				{
					if (bSingleFunc == true) break;
				}

				PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31);

				NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort);
				NdisRawReadPortUlong(PCI_CONF_DATA, &devVenID);

				if ( devVenID == 0xFFFFFFFF||devVenID == 0 ) continue;

				RegOffset = 0x3C;
				PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31)|(RegOffset & 0xFFFFFFFC);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort);
				NdisRawReadPortUchar((PCI_CONF_DATA+ (RegOffset & 0x3)), &irqLine);

				venId = (u16)(devVenID >>  0)& 0xFFFF;
				devId = (u16)(devVenID >> 16)& 0xFFFF;

				if (!bBridgeChk && (venId != VendorId) && (VendorId != U2DONTCARE))
					continue;

				if (!bBridgeChk && (devId != DeviceId) && (DeviceId != U2DONTCARE))
					continue;

				if (!bBridgeChk && (irqLine != IRQL) && (IRQL != U1DONTCARE))
					continue;

				PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS ,  PciCfgAddrPort + (2 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &classCode);
				classCode = classCode >> 8;

				basec = (u8)(classCode >>16 ) & 0xFF;
				subc = (u8)(classCode >>8 ) & 0xFF;
				if (bBridgeChk && (venId != VendorId) &&(basec == BaseCode) && (subc== SubClass ) )
					return true;

				if (bBridgeChk && (venId != VendorId) && (VendorId != U2DONTCARE))
					continue;

				if (bBridgeChk && (devId != DeviceId) && (DeviceId != U2DONTCARE))
					continue;

				if (bBridgeChk && (irqLine != IRQL) && (IRQL != U1DONTCARE))
					continue;


				NdisRawWritePortUlong(PCI_CONF_ADDRESS ,  PciCfgAddrPort + (6 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &field19);
				field19 = (field19 >> 8)& 0xFF;

				if ((basec == BaseCode) && (subc== SubClass ) && ((field19 == filed19val) ||(filed19val==U1DONTCARE) ))
				{
					*BusNum = busNumIdx;
					*DevNum = deviceNumIdx;
					*FuncNum = functionNumIdx;

					printk( "GetPciBusInfo(): Find Device(%X:%X)  bus=%d dev=%d, func=%d\n",VendorId, DeviceId, busNumIdx, deviceNumIdx, functionNumIdx);
					return true;
				}
			}
		}
	}

	printk( "GetPciBusInfo(): Cannot Find Device(%X:%X:%X)\n",VendorId, DeviceId, devVenID);
	return false;
}

bool rtl8192_get_pci_BridegInfo(
	struct net_device *dev,
	u8			BaseCode,
	u8			SubClass,
	u8			filed19val,
	u8*			BusNum,
	u8*			DevNum,
	u8*			FuncNum,
	u16*		VendorId,
	u16*		DeviceId
	)

{

	u8			busNumIdx, deviceNumIdx, functionNumIdx;
	u32			PciCfgAddrPort=0;
	u32			devVenID, classCode, field19, headertype;
	u16			venId, devId;
	u8			basec, subc, irqLine;
	u16			RegOffset;
	bool			bSingleFunc = false;

	*BusNum = 0xFF;
	*DevNum = 0xFF;
	*FuncNum = 0xFF;

	for (busNumIdx = 0; busNumIdx < PCI_MAX_BRIDGE_NUMBER  ; busNumIdx++)
	{
		for (deviceNumIdx = 0; deviceNumIdx < PCI_MAX_DEVICES; deviceNumIdx ++)
		{
			bSingleFunc = false;
			for (functionNumIdx = 0; functionNumIdx < PCI_MAX_FUNCTION; functionNumIdx++)
			{

				if (functionNumIdx == 0)
				{
					PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31);
					NdisRawWritePortUlong(PCI_CONF_ADDRESS ,  PciCfgAddrPort + (3 << 2));
					NdisRawReadPortUlong(PCI_CONF_DATA, &headertype);
					headertype = ((headertype >> 16) & 0x0080) >> 7;
					if ( headertype == 0)
						bSingleFunc = true;
				}
				else
				{
					if ( bSingleFunc ==true ) break;
				}

				PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31);

				NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort);
				NdisRawReadPortUlong(PCI_CONF_DATA, &devVenID);

				RegOffset = 0x3C;
				PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31)|(RegOffset & 0xFFFFFFFC);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort);
				NdisRawReadPortUchar((PCI_CONF_DATA+ (RegOffset & 0x3)), &irqLine);

				venId = (u16)(devVenID >>  0)& 0xFFFF;
				devId = (u16)(devVenID >> 16)& 0xFFFF;

				PciCfgAddrPort= (busNumIdx << 16)|(deviceNumIdx << 11)|(functionNumIdx <<  8)|(1 << 31);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS ,  PciCfgAddrPort + (2 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &classCode);
				classCode = classCode >> 8;

				basec = (u8)(classCode >>16 ) & 0xFF;
				subc = (u8)(classCode >>8 ) & 0xFF;

				NdisRawWritePortUlong(PCI_CONF_ADDRESS ,  PciCfgAddrPort + (6 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &field19);
				field19 = (field19 >> 8)& 0xFF;

				if ((basec == BaseCode) && (subc== SubClass ) && ((field19 == filed19val) ||(filed19val==U1DONTCARE) ))
				{
					*BusNum = busNumIdx;
					*DevNum = deviceNumIdx;
					*FuncNum = functionNumIdx;
					*VendorId = venId;
					*DeviceId = devId;

					printk("GetPciBridegInfo : Find Device(%X:%X)  bus=%d dev=%d, func=%d\n",
						venId, devId, busNumIdx, deviceNumIdx, functionNumIdx);

					return true;
				}
			}
		}
	}

	printk( "GetPciBridegInfo(): Cannot Find PciBridge for Device\n");

	return false;
}


static u16 PciBridgeVendorArray[PCI_BRIDGE_VENDOR_MAX]
	= {INTEL_VENDOR_ID,ATI_VENDOR_ID,AMD_VENDOR_ID,SIS_VENDOR_ID};

void
rtl8192_pci_find_BridgeInfo(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	u8	PciBridgeBusNum = 0xff;
	u8	PciBridgeDevNum = 0xff;
	u8	PciBridgeFuncNum = 0xff;
	u16	PciBridgeVendorId= 0xff;
	u16	PciBridgeDeviceId = 0xff;
	u8	tmp = 0;

	rtl8192_get_pci_BridegInfo(dev,
		PCI_CLASS_BRIDGE_DEV,
		PCI_SUBCLASS_BR_PCI_TO_PCI ,
		priv->NdisAdapter.BusNumber,
		&PciBridgeBusNum,
		&PciBridgeDevNum,
		&PciBridgeFuncNum,
		&PciBridgeVendorId,
		&PciBridgeDeviceId);


	priv->NdisAdapter.PciBridgeVendor = PCI_BRIDGE_VENDOR_UNKNOWN;

	for (tmp = 0; tmp < PCI_BRIDGE_VENDOR_MAX; tmp++) {
		if (PciBridgeVendorId == PciBridgeVendorArray[tmp]) {
			priv->NdisAdapter.PciBridgeVendor = tmp;
			printk("Pci Bridge Vendor is found index: %d\n",tmp);
			break;
		}
	}
	printk("Pci Bridge Vendor is %x\n",PciBridgeVendorArray[tmp]);

	priv->NdisAdapter.PciBridgeBusNum = PciBridgeBusNum;
	priv->NdisAdapter.PciBridgeDevNum = PciBridgeDevNum;
	priv->NdisAdapter.PciBridgeFuncNum = PciBridgeFuncNum;
	priv->NdisAdapter.PciBridgeVendorId = PciBridgeVendorId;
	priv->NdisAdapter.PciBridgeDeviceId = PciBridgeDeviceId;


}
#endif

static void rtl8192_parse_pci_configuration(struct pci_dev *pdev, struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	u8 tmp;
	int pos;
	u8 LinkCtrlReg;

	pos = pci_find_capability(priv->pdev, PCI_CAP_ID_EXP);
	pci_read_config_byte(priv->pdev, pos + PCI_EXP_LNKCTL, &LinkCtrlReg);
	priv->NdisAdapter.LinkCtrlReg = LinkCtrlReg;

	RT_TRACE(COMP_INIT, "Link Control Register =%x\n", priv->NdisAdapter.LinkCtrlReg);

	pci_read_config_byte(pdev, 0x98, &tmp);
	tmp |=BIT4;
	pci_write_config_byte(pdev, 0x98, tmp);

	tmp = 0x17;
	pci_write_config_byte(pdev, 0x70f, tmp);
}

bool rtl8192_pci_findadapter(struct pci_dev *pdev, struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	u16 VenderID;
	u16 DeviceID;
	u8  RevisionID;
	u16 IrqLine;

	VenderID = pdev->vendor;
	DeviceID = pdev->device;
	RevisionID = pdev->revision;
	pci_read_config_word(pdev, 0x3C, &IrqLine);

	priv->card_8192 = priv->ops->nic_type;

	if (DeviceID == 0x8172) {
		switch (RevisionID) {
		case HAL_HW_PCI_REVISION_ID_8192PCIE:
			printk("Adapter(8192 PCI-E) is found - DeviceID=%x\n", DeviceID);
			priv->card_8192 = NIC_8192E;
			break;
		case HAL_HW_PCI_REVISION_ID_8192SE:
			printk("Adapter(8192SE) is found - DeviceID=%x\n", DeviceID);
			priv->card_8192 = NIC_8192SE;
			break;
		default:
			printk("UNKNOWN nic type(%4x:%4x)\n", pdev->vendor, pdev->device);
			priv->card_8192 = NIC_UNKNOWN;
			return false;
		}
	}

	if (priv->ops->nic_type != priv->card_8192) {
		printk("Detect info(%x) and hardware info(%x) not match!\n",
				priv->ops->nic_type, priv->card_8192);
		printk("Please select proper driver before install!!!!\n");
		return false;
	}

#if defined RTL8192CE || defined RTL8192SE
	rtl8192_get_pci_BusInfo(dev,
		VenderID,
		DeviceID,
		(u8)IrqLine,
		0x02,0x80, U1DONTCARE,
		&priv->NdisAdapter.BusNumber,
		&priv->NdisAdapter.DevNumber,
		&priv->NdisAdapter.FuncNumber);

	rtl8192_pci_find_BridgeInfo(dev);

#ifdef RTL8192SE
	if (priv->NdisAdapter.PciBridgeVendor != PCI_BRIDGE_VENDOR_UNKNOWN)
#endif
	{
		rtl8192_get_LinkControl_field(dev, priv->NdisAdapter.PciBridgeBusNum,
			priv->NdisAdapter.PciBridgeDevNum, priv->NdisAdapter.PciBridgeFuncNum);
	}
#endif

	rtl8192_parse_pci_configuration(pdev, dev);

	return true;
}
