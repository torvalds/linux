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
#define _HCI_INTF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_intf.h>
#include <rtw_version.h>

#ifndef CONFIG_PCI_HCI

#error "CONFIG_PCI_HCI shall be on!\n"

#endif

#include <pci_ops.h>
#include <pci_osintf.h>
#include <pci_hal.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#ifdef CONFIG_80211N_HT
extern int rtw_ht_enable;
extern int rtw_cbw40_enable;
extern int rtw_ampdu_enable;//for enable tx_ampdu
#endif

#ifdef CONFIG_PM
extern int pm_netdev_open(struct net_device *pnetdev);
static int rtw_suspend(struct pci_dev *pdev, pm_message_t state);
static int rtw_resume(struct pci_dev *pdev);
#endif


static int rtw_drv_init(struct pci_dev *pdev, const struct pci_device_id *pdid);
static void rtw_dev_remove(struct pci_dev *pdev);

static struct specific_device_id specific_device_id_tbl[] = {
	{.idVendor=0x0b05, .idProduct=0x1791, .flags=SPEC_DEV_ID_DISABLE_HT},
	{.idVendor=0x13D3, .idProduct=0x3311, .flags=SPEC_DEV_ID_DISABLE_HT},
	{}
};

struct pci_device_id rtw_pci_id_tbl[] = {
#ifdef CONFIG_RTL8192C
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8191)},
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8178)},
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8177)},
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8176)},
#endif
#ifdef CONFIG_RTL8192D
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8193)},
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x002B)},
#endif
	{},
};

struct pci_drv_priv {
	struct pci_driver rtw_pci_drv;
	int drv_registered;
};


static struct pci_drv_priv pci_drvpriv = {
	.rtw_pci_drv.name = (char*)DRV_NAME,
	.rtw_pci_drv.probe = rtw_drv_init,
	.rtw_pci_drv.remove = rtw_dev_remove,
	.rtw_pci_drv.id_table = rtw_pci_id_tbl,
#ifdef CONFIG_PM
	.rtw_pci_drv.suspend = rtw_suspend,
	.rtw_pci_drv.resume = rtw_resume,
#else
	.rtw_pci_drv.suspend = NULL,
	.rtw_pci_drv.resume = NULL,
#endif
};


MODULE_DEVICE_TABLE(pci, rtw_pci_id_tbl);


static u16 pcibridge_vendors[PCI_BRIDGE_VENDOR_MAX] = {
	INTEL_VENDOR_ID,
	ATI_VENDOR_ID,
	AMD_VENDOR_ID,
	SIS_VENDOR_ID
};

static u8 rtw_pci_platform_switch_device_pci_aspm(_adapter *padapter, u8 value)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	u8	bresult = _SUCCESS;
	int	error;

	value |= 0x40;

	error = pci_write_config_byte(pdvobjpriv->ppcidev, 0x80, value);

	if(error != 0)
	{
		bresult = _FALSE;
		DBG_871X("rtw_pci_platform_switch_device_pci_aspm error (%d)\n",error);
	}

	return bresult;
}

//
// When we set 0x01 to enable clk request. Set 0x0 to disable clk req.
//
static u8 rtw_pci_switch_clk_req(_adapter *padapter, u8 value)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	u8	buffer, bresult = _SUCCESS;
	int	error;

	buffer = value;

	if(!padapter->hw_init_completed)
		return bresult;

	error = pci_write_config_byte(pdvobjpriv->ppcidev, 0x81, value);

	if(error != 0)
	{
		bresult = _FALSE;
		DBG_871X("rtw_pci_switch_clk_req error (%d)\n",error);
	}

	return bresult;
}

#if 0
//Description:
//Disable RTL8192SE ASPM & Disable Pci Bridge ASPM
void rtw_pci_disable_aspm(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u32	pcicfg_addrport = 0;
	u8	num4bytes;
	u8	linkctrl_reg;
	u16	pcibridge_linkctrlreg, aspmlevel = 0;

	// When there exists anyone's busnum, devnum, and funcnum that are set to 0xff,
	// we do not execute any action and return.
	// if it is not intel bus then don't enable ASPM.
	if ((pcipriv->busnumber == 0xff
		&& pcipriv->devnumber == 0xff
		&& pcipriv->funcnumber == 0xff)
		|| (pcipriv->pcibridge_busnum == 0xff
		&& pcipriv->pcibridge_devnum == 0xff
		&& pcipriv->pcibridge_funcnum == 0xff))
	{
		DBG_871X("PlatformEnableASPM(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return;
	}

	if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		DBG_871X("%s(): Disable ASPM. Recognize the Bus of PCI(Bridge) as UNKNOWN.\n", __func__);
	}

	if (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		RT_CLEAR_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_CLK_REQ);
		rtw_pci_switch_clk_req(padapter, 0x0);
	}

	{
		// Suggested by SD1 for promising device will in L0 state after an I/O.
		u8 tmp_u1b;

		pci_read_config_byte(pdvobjpriv->ppcidev, 0x80, &tmp_u1b);
	}

	// Retrieve original configuration settings.
	linkctrl_reg = pcipriv->linkctrl_reg;
	pcibridge_linkctrlreg = pcipriv->pcibridge_linkctrlreg;

	// Set corresponding value.
	aspmlevel |= BIT(0) | BIT(1);
	linkctrl_reg &= ~aspmlevel;
	pcibridge_linkctrlreg &= ~(BIT(0) | BIT(1));

	rtw_pci_platform_switch_device_pci_aspm(padapter, linkctrl_reg);
	rtw_udelay_os(50);

	//When there exists anyone's busnum, devnum, and funcnum that are set to 0xff,
	// we do not execute any action and return.
	if ((pcipriv->busnumber == 0xff &&
		pcipriv->devnumber == 0xff &&
		pcipriv->funcnumber == 0xff) ||
		(pcipriv->pcibridge_busnum == 0xff &&
		pcipriv->pcibridge_devnum == 0xff
		&& pcipriv->pcibridge_funcnum == 0xff))
	{
		//Do Nothing!!
	}
	else
	{
		//4 //Disable Pci Bridge ASPM
		pcicfg_addrport = (pcipriv->pcibridge_busnum << 16) |
						(pcipriv->pcibridge_devnum << 11) |
						(pcipriv->pcibridge_funcnum << 8) | (1 << 31);
		num4bytes = (pcipriv->pcibridge_pciehdr_offset + 0x10) / 4;

		// set up address port at 0xCF8 offset field= 0 (dev|vend)
		NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (num4bytes << 2));

		// now grab data port with device|vendor 4 byte dword
		NdisRawWritePortUchar(PCI_CONF_DATA, pcibridge_linkctrlreg);

		DBG_871X("rtw_pci_disable_aspm():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
			pcipriv->pcibridge_busnum, pcipriv->pcibridge_devnum,
			pcipriv->pcibridge_funcnum,
			(pcipriv->pcibridge_pciehdr_offset+0x10), pcibridge_linkctrlreg);

		rtw_udelay_os(50);
	}
}

//[ASPM]
//Description:
//              Enable RTL8192SE ASPM & Enable Pci Bridge ASPM for power saving
//              We should follow the sequence to enable RTL8192SE first then enable Pci Bridge ASPM
//              or the system will show bluescreen.
void rtw_pci_enable_aspm(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u16	aspmlevel = 0;
	u32	pcicfg_addrport = 0;
	u8	num4bytes;
	u8	u_pcibridge_aspmsetting = 0;
	u8	u_device_aspmsetting = 0;

	// When there exists anyone's busnum, devnum, and funcnum that are set to 0xff,
	// we do not execute any action and return.
	// if it is not intel bus then don't enable ASPM.

	if ((pcipriv->busnumber == 0xff
		&& pcipriv->devnumber == 0xff
		&& pcipriv->funcnumber == 0xff)
		|| (pcipriv->pcibridge_busnum == 0xff
		&& pcipriv->pcibridge_devnum == 0xff
		&& pcipriv->pcibridge_funcnum == 0xff))
	{
		DBG_871X("PlatformEnableASPM(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return;
	}

	//4 Enable Pci Bridge ASPM
	pcicfg_addrport = (pcipriv->pcibridge_busnum << 16)
					| (pcipriv->pcibridge_devnum << 11)
					| (pcipriv->pcibridge_funcnum << 8) | (1 << 31);
	num4bytes = (pcipriv->pcibridge_pciehdr_offset + 0x10) / 4;
	// set up address port at 0xCF8 offset field= 0 (dev|vend)
	NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (num4bytes << 2));
	// now grab data port with device|vendor 4 byte dword

	u_pcibridge_aspmsetting = pcipriv->pcibridge_linkctrlreg | pdvobjpriv->const_hostpci_aspm_setting;

	if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL ||
		pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_SIS)
		u_pcibridge_aspmsetting &= ~BIT(0);

	NdisRawWritePortUchar(PCI_CONF_DATA, u_pcibridge_aspmsetting);

	DBG_871X("PlatformEnableASPM():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
		pcipriv->pcibridge_busnum,
		pcipriv->pcibridge_devnum,
		pcipriv->pcibridge_funcnum,
		(pcipriv->pcibridge_pciehdr_offset+0x10),
		u_pcibridge_aspmsetting);

	rtw_udelay_os(50);

	// Get ASPM level (with/without Clock Req)
	aspmlevel |= pdvobjpriv->const_devicepci_aspm_setting;
	u_device_aspmsetting = pcipriv->linkctrl_reg;
	u_device_aspmsetting |= aspmlevel;

	rtw_pci_platform_switch_device_pci_aspm(padapter, u_device_aspmsetting);	//(priv->linkctrl_reg | ASPMLevel));

	if (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		rtw_pci_switch_clk_req(padapter, (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) ? 1 : 0);
		RT_SET_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_CLK_REQ);
	}

	rtw_udelay_os(50);
}

//
//Description:
//To get link control field by searching from PCIe capability lists.
//
static u8
rtw_get_link_control_field(_adapter *padapter, u8 busnum, u8 devnum,
				u8 funcnum)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	struct rt_pci_capabilities_header capability_hdr;
	u8	capability_offset, num4bytes;
	u32	pcicfg_addrport = 0;
	u8	linkctrl_reg;
	u8	status = _FALSE;

	//If busnum, devnum, funcnum are set to 0xff.
	if (busnum == 0xff && devnum == 0xff && funcnum == 0xff) {
		DBG_871X("GetLinkControlField(): Fail to find PCIe Capability\n");
		return _FALSE;
	}

	pcicfg_addrport = (busnum << 16) | (devnum << 11) | (funcnum << 8) | (1 << 31);

	//2PCIeCap

	// The device supports capability lists. Find the capabilities.
	num4bytes = 0x34 / 4;
	//get capability_offset
	// set up address port at 0xCF8 offset field= 0 (dev|vend)
	NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (num4bytes << 2));
	// now grab data port with device|vendor 4 byte dword
	NdisRawReadPortUchar(PCI_CONF_DATA, &capability_offset);

	// Loop through the capabilities in search of the power management capability.
	// The list is NULL-terminated, so the last offset will always be zero.

	while (capability_offset != 0) {
		// First find the number of 4 Byte.
		num4bytes = capability_offset / 4;

		// Read the header of the capability at  this offset. If the retrieved capability is not
		// the power management capability that we are looking for, follow the link to the
		// next capability and continue looping.

		//4 get capability_hdr
		// set up address port at 0xCF8 offset field= 0 (dev|vend)
		NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (num4bytes << 2));
		// now grab data port with device|vendor 4 byte dword
		NdisRawReadPortUshort(PCI_CONF_DATA, (u16 *) & capability_hdr);

		// Found the PCI express capability
		if (capability_hdr.capability_id == PCI_CAPABILITY_ID_PCI_EXPRESS)
		{
			break;
		}
		else
		{
			// This is some other capability. Keep looking for the PCI express capability.
			capability_offset = capability_hdr.next;
		}
	}

	if (capability_hdr.capability_id == PCI_CAPABILITY_ID_PCI_EXPRESS)	//
	{
		num4bytes = (capability_offset + 0x10) / 4;

		//4 Read  Link Control Register
		// set up address port at 0xCF8 offset field= 0 (dev|vend)
		NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (num4bytes << 2));
		// now grab data port with device|vendor 4 byte dword
		NdisRawReadPortUchar(PCI_CONF_DATA, &linkctrl_reg);

		pcipriv->pcibridge_pciehdr_offset = capability_offset;
		pcipriv->pcibridge_linkctrlreg = linkctrl_reg;

		status = _TRUE;
	}
	else
	{
		// We didn't find a PCIe capability.
		DBG_871X("GetLinkControlField(): Cannot Find PCIe Capability\n");
	}

	return status;
}

//
//Description:
//To get PCI bus infomation and return busnum, devnum, and funcnum about
//the bus(bridge) which the device binds.
//
static u8
rtw_get_pci_bus_info(_adapter *padapter,
			  u16 vendorid,
			  u16 deviceid,
			  u8 irql, u8 basecode, u8 subclass, u8 filed19val,
			  u8 * busnum, u8 * devnum, u8 * funcnum)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	u8	busnum_idx, devicenum_idx, functionnum_idx;
	u32	pcicfg_addrport = 0;
	u32	dev_venid = 0, classcode, field19, headertype;
	u16	venId, devId;
	u8	basec, subc, irqline;
	u16	regoffset;
	u8	b_singlefunc = _FALSE;
	u8	b_bridgechk = _FALSE;

	*busnum = 0xFF;
	*devnum = 0xFF;
	*funcnum = 0xFF;

	//DBG_871X("==============>vendorid:%x,deviceid:%x,irql:%x\n", vendorid,deviceid,irql);
	if ((basecode == PCI_CLASS_BRIDGE_DEV) &&
		(subclass == PCI_SUBCLASS_BR_PCI_TO_PCI)
		&& (filed19val == U1DONTCARE))
		b_bridgechk = _TRUE;

	// perform a complete pci bus scan operation
	for (busnum_idx = 0; busnum_idx < PCI_MAX_BRIDGE_NUMBER; busnum_idx++)	//255
	{
		for (devicenum_idx = 0; devicenum_idx < PCI_MAX_DEVICES; devicenum_idx++)	//32
		{
			b_singlefunc = _FALSE;
			for (functionnum_idx = 0; functionnum_idx < PCI_MAX_FUNCTION; functionnum_idx++)	//8
			{
				//
				// <Roger_Notes> We have to skip redundant Bus scan to prevent unexpected system hang
				// if single function is present in this device.
				// 2009.02.26.
				//
				if (functionnum_idx == 0) {
					//4 get header type (DWORD #3)
					pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31);
					NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (3 << 2));
					NdisRawReadPortUlong(PCI_CONF_DATA, &headertype);
					headertype = ((headertype >> 16) & 0x0080) >> 7;	// address 0x0e[7].
					if (headertype == 0)	//Single function
						b_singlefunc = _TRUE;
				}
				else
				{//By pass the following scan process.
					if (b_singlefunc == _TRUE)
						break;
				}

				// Set access enable control.
				pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31);

				//4 // Get vendorid/ deviceid
				// set up address port at 0xCF8 offset field= 0 (dev|vend)
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport);
				// now grab data port with device|vendor 4 byte dword
				NdisRawReadPortUlong(PCI_CONF_DATA, &dev_venid);

				// if data port is full of 1s, no device is present
				// some broken boards return 0 if a slot is empty:
				if (dev_venid == 0xFFFFFFFF || dev_venid == 0)
					continue;	//PCI_INVALID_VENDORID

				// 4 // Get irql
				regoffset = 0x3C;
				pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31) | (regoffset & 0xFFFFFFFC);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport);
				NdisRawReadPortUchar((PCI_CONF_DATA +(regoffset & 0x3)), &irqline);

				venId = (u16) (dev_venid >> 0) & 0xFFFF;
				devId = (u16) (dev_venid >> 16) & 0xFFFF;

				// Check Vendor ID
				if (!b_bridgechk && (venId != vendorid) && (vendorid != U2DONTCARE))
					continue;

				// Check Device ID
				if (!b_bridgechk && (devId != deviceid) && (deviceid != U2DONTCARE))
					continue;

				// Check irql
				if (!b_bridgechk && (irqline != irql) && (irql != U1DONTCARE))
					continue;

				//4 get Class Code
				pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (2 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &classcode);
				classcode = classcode >> 8;

				basec = (u8) (classcode >> 16) & 0xFF;
				subc = (u8) (classcode >> 8) & 0xFF;
				if (b_bridgechk && (venId != vendorid) && (basec == basecode) && (subc == subclass))
					return _TRUE;

				// Check Vendor ID
				if (b_bridgechk && (venId != vendorid) && (vendorid != U2DONTCARE))
					continue;

				// Check Device ID
				if (b_bridgechk && (devId != deviceid) && (deviceid != U2DONTCARE))
					continue;

				// Check irql
				if (b_bridgechk && (irqline != irql) && (irql != U1DONTCARE))
					continue;

				//4 get field 0x19 value  (DWORD #6)
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (6 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &field19);
				field19 = (field19 >> 8) & 0xFF;

				//4 Matching Class Code and filed19.
				if ((basec == basecode) && (subc == subclass) && ((field19 == filed19val) || (filed19val == U1DONTCARE))) {
					*busnum = busnum_idx;
					*devnum = devicenum_idx;
					*funcnum = functionnum_idx;

					DBG_871X("GetPciBusInfo(): Find Device(%X:%X)  bus=%d dev=%d, func=%d\n",
						vendorid, deviceid, busnum_idx, devicenum_idx, functionnum_idx);
					return _TRUE;
				}
			}
		}
	}

	DBG_871X("GetPciBusInfo(): Cannot Find Device(%X:%X:%X)\n", vendorid, deviceid, dev_venid);

	return _FALSE;
}

static u8
rtw_get_pci_brideg_info(_adapter *padapter,
			     u8 basecode,
			     u8 subclass,
			     u8 filed19val, u8 * busnum, u8 * devnum,
			     u8 * funcnum, u16 * vendorid, u16 * deviceid)
{
	u8	busnum_idx, devicenum_idx, functionnum_idx;
	u32	pcicfg_addrport = 0;
	u32	dev_venid, classcode, field19, headertype;
	u16	venId, devId;
	u8	basec, subc, irqline;
	u16	regoffset;
	u8	b_singlefunc = _FALSE;

	*busnum = 0xFF;
	*devnum = 0xFF;
	*funcnum = 0xFF;

	// perform a complete pci bus scan operation
	for (busnum_idx = 0; busnum_idx < PCI_MAX_BRIDGE_NUMBER; busnum_idx++)	//255
	{
		for (devicenum_idx = 0; devicenum_idx < PCI_MAX_DEVICES; devicenum_idx++)	//32
		{
			b_singlefunc = _FALSE;
			for (functionnum_idx = 0; functionnum_idx < PCI_MAX_FUNCTION; functionnum_idx++)	//8
			{
				//
				// <Roger_Notes> We have to skip redundant Bus scan to prevent unexpected system hang
				// if single function is present in this device.
				// 2009.02.26.
				//
				if (functionnum_idx == 0)
				{
					//4 get header type (DWORD #3)
					pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31);
					//NdisRawWritePortUlong((ULONG_PTR)PCI_CONF_ADDRESS ,  pcicfg_addrport + (3 << 2));
					//NdisRawReadPortUlong((ULONG_PTR)PCI_CONF_DATA, &headertype);
					NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (3 << 2));
					NdisRawReadPortUlong(PCI_CONF_DATA, &headertype);
					headertype = ((headertype >> 16) & 0x0080) >> 7;	// address 0x0e[7].
					if (headertype == 0)	//Single function
						b_singlefunc = _TRUE;
				}
				else
				{//By pass the following scan process.
					if (b_singlefunc == _TRUE)
						break;
				}

				pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31);

				//4 // Get vendorid/ deviceid
				// set up address port at 0xCF8 offset field= 0 (dev|vend)
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport);
				// now grab data port with device|vendor 4 byte dword
				NdisRawReadPortUlong(PCI_CONF_DATA, &dev_venid);

				//4 Get irql
				regoffset = 0x3C;
				pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31) | (regoffset & 0xFFFFFFFC);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport);
				NdisRawReadPortUchar((PCI_CONF_DATA + (regoffset & 0x3)), &irqline);

				venId = (u16) (dev_venid >> 0) & 0xFFFF;
				devId = (u16) (dev_venid >> 16) & 0xFFFF;

				//4 get Class Code
				pcicfg_addrport = (busnum_idx << 16) | (devicenum_idx << 11) | (functionnum_idx << 8) | (1 << 31);
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (2 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &classcode);
				classcode = classcode >> 8;

				basec = (u8) (classcode >> 16) & 0xFF;
				subc = (u8) (classcode >> 8) & 0xFF;

				//4 get field 0x19 value  (DWORD #6)
				NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (6 << 2));
				NdisRawReadPortUlong(PCI_CONF_DATA, &field19);
				field19 = (field19 >> 8) & 0xFF;

				//4 Matching Class Code and filed19.
				if ((basec == basecode) && (subc == subclass) && ((field19 == filed19val) || (filed19val == U1DONTCARE))) {
					*busnum = busnum_idx;
					*devnum = devicenum_idx;
					*funcnum = functionnum_idx;
					*vendorid = venId;
					*deviceid = devId;

					DBG_871X("GetPciBridegInfo : Find Device(%X:%X)  bus=%d dev=%d, func=%d\n",
						venId, devId, busnum_idx, devicenum_idx, functionnum_idx);

					return _TRUE;
				}
			}
		}
	}

	DBG_871X("GetPciBridegInfo(): Cannot Find PciBridge for Device\n");

	return _FALSE;
}				// end of GetPciBridegInfo

//
//Description:
//To find specific bridge information.
//
static void rtw_find_bridge_info(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u8	pcibridge_busnum = 0xff;
	u8	pcibridge_devnum = 0xff;
	u8	pcibridge_funcnum = 0xff;
	u16	pcibridge_vendorid = 0xff;
	u16	pcibridge_deviceid = 0xff;
	u8	tmp = 0;

	rtw_get_pci_brideg_info(padapter,
				     PCI_CLASS_BRIDGE_DEV,
				     PCI_SUBCLASS_BR_PCI_TO_PCI,
				     pcipriv->busnumber,
				     &pcibridge_busnum,
				     &pcibridge_devnum, &pcibridge_funcnum,
				     &pcibridge_vendorid, &pcibridge_deviceid);

	// match the array of vendor id and regonize which chipset is used.
	pcipriv->pcibridge_vendor = PCI_BRIDGE_VENDOR_UNKNOWN;

	for (tmp = 0; tmp < PCI_BRIDGE_VENDOR_MAX; tmp++) {
		if (pcibridge_vendorid == pcibridge_vendors[tmp]) {
			pcipriv->pcibridge_vendor = tmp;
			DBG_871X("Pci Bridge Vendor is found index: %d\n", tmp);
			break;
		}
	}
	DBG_871X("Pci Bridge Vendor is %x\n", pcibridge_vendors[tmp]);

	// Update corresponding PCI bus info.
	pcipriv->pcibridge_busnum = pcibridge_busnum;
	pcipriv->pcibridge_devnum = pcibridge_devnum;
	pcipriv->pcibridge_funcnum = pcibridge_funcnum;
	pcipriv->pcibridge_vendorid = pcibridge_vendorid;
	pcipriv->pcibridge_deviceid = pcibridge_deviceid;

}

static u8
rtw_get_amd_l1_patch(_adapter *padapter, u8 busnum, u8 devnum,
			  u8 funcnum)
{
	u8	status = _FALSE;
	u8	offset_e0;
	unsigned	offset_e4;
	u32	pcicfg_addrport = 0;

	pcicfg_addrport = (busnum << 16) | (devnum << 11) | (funcnum << 8) | (1 << 31);

	NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + 0xE0);
	NdisRawWritePortUchar(PCI_CONF_DATA, 0xA0);

	NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + 0xE0);
	NdisRawReadPortUchar(PCI_CONF_DATA, &offset_e0);

	if (offset_e0 == 0xA0)
	{
		NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + 0xE4);
		NdisRawReadPortUlong(PCI_CONF_DATA, &offset_e4);
		//DbgPrint("Offset E4 %x\n", offset_e4);
		if (offset_e4 & BIT(23))
			status = _TRUE;
	}

	return status;
}
#else
/*Disable RTL8192SE ASPM & Disable Pci Bridge ASPM*/
void rtw_pci_disable_aspm(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u8	linkctrl_reg;
	u16	pcibridge_linkctrlreg;
	u16	aspmlevel = 0;

	// We do not diable/enable ASPM by driver, in the future, the BIOS will enable host and NIC ASPM.
	// Advertised by SD1 victorh. Added by tynli. 2009.11.23.
	if(pdvobjpriv->const_pci_aspm == 0)
		return;

	if(!padapter->hw_init_completed)
		return;

	if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s(): PCI(Bridge) UNKNOWN.\n", __FUNCTION__));
		return;
	}

	linkctrl_reg = pcipriv->linkctrl_reg;
	pcibridge_linkctrlreg = pcipriv->pcibridge_linkctrlreg;

	// Set corresponding value.
	aspmlevel |= BIT(0) | BIT(1);
	linkctrl_reg &=~aspmlevel;
	pcibridge_linkctrlreg &=~aspmlevel;

	if (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		RT_CLEAR_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_CLK_REQ);
		rtw_pci_switch_clk_req(padapter, 0x0);
	}

	{
		/*for promising device will in L0 state after an I/O.*/
		u8 tmp_u1b;
		pci_read_config_byte(pdev, 0x80, &tmp_u1b);
	}

	rtw_pci_platform_switch_device_pci_aspm(padapter, linkctrl_reg);
	rtw_udelay_os(50);

	//When there exists anyone's BusNum, DevNum, and FuncNum that are set to 0xff,
	// we do not execute any action and return. Added by tynli.
	if( (pcipriv->busnumber == 0xff && pcipriv->devnumber == 0xff && pcipriv->funcnumber == 0xff) ||
		(pcipriv->pcibridge_busnum == 0xff && pcipriv->pcibridge_devnum == 0xff && pcipriv->pcibridge_funcnum == 0xff) )
	{
		// Do Nothing!!
	}
	else
	{
		/*Disable Pci Bridge ASPM*/
		//NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + (num4bytes << 2));
		//NdisRawWritePortUchar(PCI_CONF_DATA, pcibridge_linkctrlreg);
		pci_write_config_byte(bridge_pdev, pcipriv->pcibridge_pciehdr_offset + 0x10, pcibridge_linkctrlreg);

		DBG_871X("rtw_pci_disable_aspm():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
			pcipriv->pcibridge_busnum, pcipriv->pcibridge_devnum,
			pcipriv->pcibridge_funcnum,
			(pcipriv->pcibridge_pciehdr_offset+0x10), pcibridge_linkctrlreg);

		rtw_udelay_os(50);
	}

}

/*Enable RTL8192SE ASPM & Enable Pci Bridge ASPM for
power saving We should follow the sequence to enable
RTL8192SE first then enable Pci Bridge ASPM
or the system will show bluescreen.*/
void rtw_pci_enable_aspm(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u16	aspmlevel = 0;
	u8	u_pcibridge_aspmsetting = 0;
	u8	u_device_aspmsetting = 0;
	u32	u_device_aspmsupportsetting = 0;

	// We do not diable/enable ASPM by driver, in the future, the BIOS will enable host and NIC ASPM.
	// Advertised by SD1 victorh. Added by tynli. 2009.11.23.
	if(pdvobjpriv->const_pci_aspm == 0)
		return;

	//When there exists anyone's BusNum, DevNum, and FuncNum that are set to 0xff,
	// we do not execute any action and return. Added by tynli.
	if( (pcipriv->busnumber == 0xff && pcipriv->devnumber == 0xff && pcipriv->funcnumber == 0xff) ||
		(pcipriv->pcibridge_busnum == 0xff && pcipriv->pcibridge_devnum == 0xff && pcipriv->pcibridge_funcnum == 0xff) )
	{
		DBG_871X("rtw_pci_enable_aspm(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return;
	}

//Get Bridge ASPM Support
//not to enable bridge aspm if bridge does not support
//Added by sherry 20100803
	if (IS_HARDWARE_TYPE_8192DE(padapter))
	{
		//PciCfgAddrPort = (pcipriv->pcibridge_busnum << 16)|(pcipriv->pcibridge_devnum<< 11)|(pcipriv->pcibridge_funcnum <<  8)|(1 << 31);
		//Num4Bytes = (pcipriv->pcibridge_pciehdr_offset+0x0C)/4;
		//NdisRawWritePortUlong((ULONG_PTR)PCI_CONF_ADDRESS , PciCfgAddrPort+(Num4Bytes << 2));
		//NdisRawReadPortUlong((ULONG_PTR)PCI_CONF_DATA,&uDeviceASPMSupportSetting);
		pci_read_config_dword(bridge_pdev, (pcipriv->pcibridge_pciehdr_offset+0x0C), &u_device_aspmsupportsetting);
		DBG_871X("rtw_pci_enable_aspm(): Bridge ASPM support %x \n",u_device_aspmsupportsetting);
		if(((u_device_aspmsupportsetting & BIT(11)) != BIT(11)) || ((u_device_aspmsupportsetting & BIT(10)) != BIT(10)))
		{
			if(pdvobjpriv->const_devicepci_aspm_setting == 3)
			{
				DBG_871X("rtw_pci_enable_aspm(): Bridge not support L0S or L1\n");
				return;
			}
			else if(pdvobjpriv->const_devicepci_aspm_setting == 2)
			{
				if((u_device_aspmsupportsetting & BIT(11)) != BIT(11))
				{
					DBG_871X("rtw_pci_enable_aspm(): Bridge not support L1 \n");
					return;
				}
			}
			else if(pdvobjpriv->const_devicepci_aspm_setting == 1)
			{
				if((u_device_aspmsupportsetting & BIT(10)) != BIT(10))
				{
					DBG_871X("rtw_pci_enable_aspm(): Bridge not support L0s \n");
					return;
				}

			}
		}
		else
		{
			DBG_871X("rtw_pci_enable_aspm(): Bridge support L0s and L1 \n");
		}
	}


	/*Enable Pci Bridge ASPM*/
	//PciCfgAddrPort = (pcipriv->pcibridge_busnum << 16)|(pcipriv->pcibridge_devnum<< 11) |(pcipriv->pcibridge_funcnum <<  8)|(1 << 31);
	//Num4Bytes = (pcipriv->pcibridge_pciehdr_offset+0x10)/4;
	// set up address port at 0xCF8 offset field= 0 (dev|vend)
	//NdisRawWritePortUlong(PCI_CONF_ADDRESS, PciCfgAddrPort + (Num4Bytes << 2));
	// now grab data port with device|vendor 4 byte dword

	u_pcibridge_aspmsetting = pcipriv->pcibridge_linkctrlreg;
	u_pcibridge_aspmsetting |= pdvobjpriv->const_hostpci_aspm_setting;

	if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL ||
		pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_SIS )
		u_pcibridge_aspmsetting &= ~BIT(0); // for intel host 42 device 43

	//NdisRawWritePortUchar(PCI_CONF_DATA, u_pcibridge_aspmsetting);
	pci_write_config_byte(bridge_pdev, (pcipriv->pcibridge_pciehdr_offset+0x10), u_pcibridge_aspmsetting);

	DBG_871X("PlatformEnableASPM():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
		pcipriv->pcibridge_busnum, pcipriv->pcibridge_devnum, pcipriv->pcibridge_funcnum,
		(pcipriv->pcibridge_pciehdr_offset+0x10),
		u_pcibridge_aspmsetting);

	rtw_udelay_os(50);

	/*Get ASPM level (with/without Clock Req)*/
	aspmlevel |= pdvobjpriv->const_devicepci_aspm_setting;
	u_device_aspmsetting = pcipriv->linkctrl_reg;
	u_device_aspmsetting |= aspmlevel; // device 43

	rtw_pci_platform_switch_device_pci_aspm(padapter, u_device_aspmsetting);

	if (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		rtw_pci_switch_clk_req(padapter, (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) ? 1 : 0);
		RT_SET_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_CLK_REQ);
	}

	rtw_udelay_os(50);
}

static u8 rtw_pci_get_amd_l1_patch(struct dvobj_priv *dvobj)
{
	struct pci_dev	*pdev = dvobj->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	u8	status = _FALSE;
	u8	offset_e0;
	u32	offset_e4;

	//NdisRawWritePortUlong(PCI_CONF_ADDRESS,pcicfg_addrport + 0xE0);
	//NdisRawWritePortUchar(PCI_CONF_DATA, 0xA0);
	pci_write_config_byte(bridge_pdev, 0xE0, 0xA0);

	//NdisRawWritePortUlong(PCI_CONF_ADDRESS,pcicfg_addrport + 0xE0);
	//NdisRawReadPortUchar(PCI_CONF_DATA, &offset_e0);
	pci_read_config_byte(bridge_pdev, 0xE0, &offset_e0);

	if (offset_e0 == 0xA0) {
		//NdisRawWritePortUlong(PCI_CONF_ADDRESS, pcicfg_addrport + 0xE4);
		//NdisRawReadPortUlong(PCI_CONF_DATA, &offset_e4);
		pci_read_config_dword(bridge_pdev, 0xE4, &offset_e4);
		if (offset_e4 & BIT(23))
			status = _TRUE;
	}

	return status;
}

static void rtw_pci_get_linkcontrol_field(struct dvobj_priv *dvobj)
{
	struct pci_priv	*pcipriv = &(dvobj->pcipriv);
	struct pci_dev	*pdev = dvobj->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	u8	capabilityoffset = pcipriv->pcibridge_pciehdr_offset;
	u8	linkctrl_reg;

	/*Read  Link Control Register*/
	pci_read_config_byte(bridge_pdev, capabilityoffset + PCI_EXP_LNKCTL, &linkctrl_reg);

	pcipriv->pcibridge_linkctrlreg = linkctrl_reg;
}
#endif

static void rtw_pci_parse_configuration(struct dvobj_priv *dvobj)
{
	struct pci_dev	*pdev = dvobj->ppcidev;
	struct pci_priv	*pcipriv = &(dvobj->pcipriv);
	u8 tmp;
	int pos;
	u8 linkctrl_reg;

	//Link Control Register
	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_byte(pdev, pos + PCI_EXP_LNKCTL, &linkctrl_reg);
	pcipriv->linkctrl_reg = linkctrl_reg;

	//DBG_871X("Link Control Register = %x\n", pcipriv->linkctrl_reg);

	pci_read_config_byte(pdev, 0x98, &tmp);
	tmp |= BIT(4);
	pci_write_config_byte(pdev, 0x98, tmp);

	//tmp = 0x17;
	//pci_write_config_byte(pdev, 0x70f, tmp);
}

//
// Update PCI dependent default settings.
//
static void rtw_pci_update_default_setting(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;

	//reset pPSC->reg_rfps_level & priv->b_support_aspm
	pwrpriv->reg_rfps_level = 0;
	pwrpriv->b_support_aspm = 0;

	// Dynamic Mechanism,
	//rtw_hal_set_def_var(pAdapter, HAL_DEF_INIT_GAIN, &(pDevice->InitGainState));

	// Update PCI ASPM setting
	pwrpriv->const_amdpci_aspm = pdvobjpriv->const_amdpci_aspm;
	switch (pdvobjpriv->const_pci_aspm) {
		case 0:		// No ASPM
			break;

		case 1:		// ASPM dynamically enabled/disable.
			pwrpriv->reg_rfps_level |= RT_RF_LPS_LEVEL_ASPM;
			break;

		case 2:		// ASPM with Clock Req dynamically enabled/disable.
			pwrpriv->reg_rfps_level |= (RT_RF_LPS_LEVEL_ASPM | RT_RF_OFF_LEVL_CLK_REQ);
			break;

		case 3:		// Always enable ASPM and Clock Req from initialization to halt.
			pwrpriv->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM);
			pwrpriv->reg_rfps_level |= (RT_RF_PS_LEVEL_ALWAYS_ASPM | RT_RF_OFF_LEVL_CLK_REQ);
			break;

		case 4:		// Always enable ASPM without Clock Req from initialization to halt.
			pwrpriv->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM | RT_RF_OFF_LEVL_CLK_REQ);
			pwrpriv->reg_rfps_level |= RT_RF_PS_LEVEL_ALWAYS_ASPM;
			break;
	}

	pwrpriv->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;

	// Update Radio OFF setting
	switch (pdvobjpriv->const_hwsw_rfoff_d3) {
		case 1:
			if (pwrpriv->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
				pwrpriv->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
			break;

		case 2:
			if (pwrpriv->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
				pwrpriv->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
			pwrpriv->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;
			break;

		case 3:
			pwrpriv->reg_rfps_level |= RT_RF_OFF_LEVL_PCI_D3;
			break;
	}

	// Update Rx 2R setting
	//pPSC->reg_rfps_level |= ((pDevice->RegLPS2RDisable) ? RT_RF_LPS_DISALBE_2R : 0);

	//
	// Set HW definition to determine if it supports ASPM.
	//
	switch (pdvobjpriv->const_support_pciaspm) {
		case 0:	// Not support ASPM.
			{
				u8	b_support_aspm = _FALSE;
				pwrpriv->b_support_aspm = b_support_aspm;
			}
			break;

		case 1:	// Support ASPM.
			{
				u8	b_support_aspm = _TRUE;
				u8	b_support_backdoor = _TRUE;

				pwrpriv->b_support_aspm = b_support_aspm;

				/*if(pAdapter->MgntInfo.CustomerID == RT_CID_TOSHIBA &&
					pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_AMD &&
					!pcipriv->amd_l1_patch)
					b_support_backdoor = _FALSE;*/

				pwrpriv->b_support_backdoor = b_support_backdoor;
			}
			break;

		case 2:	// Set by Chipset.
			// ASPM value set by chipset.
			if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL) {
				u8	b_support_aspm = _TRUE;
				pwrpriv->b_support_aspm = b_support_aspm;
			}
			break;

		default:
			// Do nothing. Set when finding the chipset.
			break;
	}
}

static void rtw_pci_initialize_adapter_common(_adapter *padapter)
{
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;

	rtw_pci_update_default_setting(padapter);

	if (pwrpriv->reg_rfps_level & RT_RF_PS_LEVEL_ALWAYS_ASPM) {
		// Always enable ASPM & Clock Req.
		rtw_pci_enable_aspm(padapter);
		RT_SET_PS_LEVEL(pwrpriv, RT_RF_PS_LEVEL_ALWAYS_ASPM);
	}

}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
#define rtw_pci_interrupt(x,y,z) rtw_pci_interrupt(x,y)
#endif

static irqreturn_t rtw_pci_interrupt(int irq, void *priv, struct pt_regs *regs)
{
	struct dvobj_priv *dvobj = (struct dvobj_priv *)priv;
	_adapter *adapter = dvobj->if1;

	if (dvobj->irq_enabled == 0) {
		return IRQ_HANDLED;
	}

	if(rtw_hal_interrupt_handler(adapter) == _FAIL)
		return IRQ_HANDLED;
		//return IRQ_NONE;

	return IRQ_HANDLED;
}

#ifdef RTK_DMP_PLATFORM
#define pci_iounmap(x,y) iounmap(y)
#endif

int pci_alloc_irq(struct dvobj_priv *dvobj)
{
	int err;
	struct pci_dev *pdev = dvobj->ppcidev;

#if defined(IRQF_SHARED)
	err = request_irq(pdev->irq, &rtw_pci_interrupt, IRQF_SHARED, DRV_NAME, dvobj);
#else
	err = request_irq(pdev->irq, &rtw_pci_interrupt, SA_SHIRQ, DRV_NAME, dvobj);
#endif
	if (err) {
		DBG_871X("Error allocating IRQ %d",pdev->irq);
	} else {
		dvobj->irq_alloc = 1;
		DBG_871X("Request_irq OK, IRQ %d\n",pdev->irq);
	}

	return err?_FAIL:_SUCCESS;
}

static struct dvobj_priv *pci_dvobj_init(struct pci_dev *pdev)
{
	int err;
	u32	status = _FAIL;
	struct dvobj_priv *dvobj = NULL;
	struct pci_priv	*pcipriv = NULL;
	struct pci_dev *bridge_pdev = pdev->bus->self;
	unsigned long pmem_start, pmem_len, pmem_flags;
	u8	tmp;

_func_enter_;

	if ((dvobj = devobj_init()) == NULL) {
		goto exit;
	}
	dvobj->ppcidev = pdev;
	pcipriv = &(dvobj->pcipriv);
	pci_set_drvdata(pdev, dvobj);

	if ( (err = pci_enable_device(pdev)) != 0) {
		DBG_871X(KERN_ERR "%s : Cannot enable new PCI device\n", pci_name(pdev));
		goto free_dvobj;
	}

#ifdef CONFIG_64BIT_DMA
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		DBG_871X("RTL819xCE: Using 64bit DMA\n");
		if ((err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) != 0) {
			DBG_871X(KERN_ERR "Unable to obtain 64bit DMA for consistent allocations\n");
			goto disable_picdev;
		}
		dvobj->bdma64 = _TRUE;
	} else
#endif
	{
		if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
			if ((err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) != 0) {
				DBG_871X(KERN_ERR "Unable to obtain 32bit DMA for consistent allocations\n");
				goto disable_picdev;
			}
		}
	}

	pci_set_master(pdev);

	if ((err = pci_request_regions(pdev, DRV_NAME)) != 0) {
		DBG_871X(KERN_ERR "Can't obtain PCI resources\n");
		goto disable_picdev;
	}
	//MEM map
	pmem_start = pci_resource_start(pdev, 2);
	pmem_len = pci_resource_len(pdev, 2);
	pmem_flags = pci_resource_flags(pdev, 2);

#ifdef RTK_DMP_PLATFORM
	dvobj->pci_mem_start = (unsigned long)ioremap_nocache(pmem_start, pmem_len);
#else
	dvobj->pci_mem_start = (unsigned long)pci_iomap(pdev, 2, pmem_len); /* shared mem start */
#endif
	if (dvobj->pci_mem_start == 0) {
		DBG_871X(KERN_ERR "Can't map PCI mem\n");
		goto release_regions;
	}

	DBG_871X("Memory mapped space start: 0x%08lx len:%08lx flags:%08lx, after map:0x%08lx\n",
		pmem_start, pmem_len, pmem_flags, dvobj->pci_mem_start);

	// Disable Clk Request */
	pci_write_config_byte(pdev, 0x81, 0);
	// leave D3 mode */
	pci_write_config_byte(pdev, 0x44, 0);
	pci_write_config_byte(pdev, 0x04, 0x06);
	pci_write_config_byte(pdev, 0x04, 0x07);


#if 1
	/*find bus info*/
	pcipriv->busnumber = pdev->bus->number;
	pcipriv->devnumber = PCI_SLOT(pdev->devfn);
	pcipriv->funcnumber = PCI_FUNC(pdev->devfn);

	/*find bridge info*/
	pcipriv->pcibridge_vendor = PCI_BRIDGE_VENDOR_UNKNOWN;
	if(bridge_pdev){
		pcipriv->pcibridge_vendorid = bridge_pdev->vendor;
		for (tmp = 0; tmp < PCI_BRIDGE_VENDOR_MAX; tmp++) {
			if (bridge_pdev->vendor == pcibridge_vendors[tmp]) {
				pcipriv->pcibridge_vendor = tmp;
				DBG_871X("Pci Bridge Vendor is found index: %d, %x\n", tmp, pcibridge_vendors[tmp]);
				break;
			}
		}
	}

	//if (pcipriv->pcibridge_vendor != PCI_BRIDGE_VENDOR_UNKNOWN) {
	if(bridge_pdev){
		pcipriv->pcibridge_busnum = bridge_pdev->bus->number;
		pcipriv->pcibridge_devnum = PCI_SLOT(bridge_pdev->devfn);
		pcipriv->pcibridge_funcnum = PCI_FUNC(bridge_pdev->devfn);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34))
		pcipriv->pcibridge_pciehdr_offset = pci_find_capability(bridge_pdev, PCI_CAP_ID_EXP);
#else
		pcipriv->pcibridge_pciehdr_offset = bridge_pdev->pcie_cap;
#endif

		rtw_pci_get_linkcontrol_field(dvobj);

		if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_AMD) {
			pcipriv->amd_l1_patch = rtw_pci_get_amd_l1_patch(dvobj);
		}
	}
#else
	//
	// Find bridge related info.
	//
	rtw_get_pci_bus_info(padapter,
				  pdev->vendor,
				  pdev->device,
				  (u8) pdvobjpriv->irqline,
				  0x02, 0x80, U1DONTCARE,
				  &pcipriv->busnumber,
				  &pcipriv->devnumber,
				  &pcipriv->funcnumber);

	rtw_find_bridge_info(padapter);

	if (pcipriv->pcibridge_vendor != PCI_BRIDGE_VENDOR_UNKNOWN) {
		rtw_get_link_control_field(padapter,
						pcipriv->pcibridge_busnum,
						pcipriv->pcibridge_devnum,
						pcipriv->pcibridge_funcnum);

		if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_AMD) {
			pcipriv->amd_l1_patch =
				rtw_get_amd_l1_patch(padapter,
							pcipriv->pcibridge_busnum,
							pcipriv->pcibridge_devnum,
							pcipriv->pcibridge_funcnum);
		}
	}
#endif

	//
	// Allow the hardware to look at PCI config information.
	//
	rtw_pci_parse_configuration(dvobj);

	DBG_871X("pcidev busnumber:devnumber:funcnumber:"
		"vendor:link_ctl %d:%d:%d:%x:%x\n",
		pcipriv->busnumber,
		pcipriv->devnumber,
		pcipriv->funcnumber,
		pdev->vendor,
		pcipriv->linkctrl_reg);

	DBG_871X("pci_bridge busnumber:devnumber:funcnumber:vendor:"
		"pcie_cap:link_ctl_reg: %d:%d:%d:%x:%x:%x:%x\n",
		pcipriv->pcibridge_busnum,
		pcipriv->pcibridge_devnum,
		pcipriv->pcibridge_funcnum,
		pcibridge_vendors[pcipriv->pcibridge_vendor],
		pcipriv->pcibridge_pciehdr_offset,
		pcipriv->pcibridge_linkctrlreg,
		pcipriv->amd_l1_patch);

	status = _SUCCESS;

iounmap:
	if (status != _SUCCESS && dvobj->pci_mem_start != 0) {
		pci_iounmap(pdev, (void *)dvobj->pci_mem_start);
		dvobj->pci_mem_start = 0;
	}
release_regions:
	if (status != _SUCCESS)
		pci_release_regions(pdev);
disable_picdev:
	if (status != _SUCCESS)
		pci_disable_device(pdev);
free_dvobj:
	if (status != _SUCCESS && dvobj) {
		pci_set_drvdata(pdev, NULL);
		devobj_deinit(dvobj);
		dvobj = NULL;
	}
exit:
_func_exit_;
	return dvobj;
}

static void pci_dvobj_deinit(struct pci_dev *pdev)
{
	struct dvobj_priv *dvobj = pci_get_drvdata(pdev);
_func_enter_;

	pci_set_drvdata(pdev, NULL);
	if (dvobj) {
		if (dvobj->irq_alloc) {
			free_irq(pdev->irq, dvobj);
			dvobj->irq_alloc = 0;
		}

		if (dvobj->pci_mem_start != 0) {
			pci_iounmap(pdev, (void *)dvobj->pci_mem_start);
			dvobj->pci_mem_start = 0;
		}

		devobj_deinit(dvobj);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);

_func_exit_;
}

static void decide_chip_type_by_pci_device_id(_adapter *padapter, struct pci_dev *pdev)
{
	u16	venderid, deviceid, irqline;
	u8	revisionid;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);


	venderid = pdev->vendor;
	deviceid = pdev->device;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	pci_read_config_byte(pdev, PCI_REVISION_ID, &revisionid); // PCI_REVISION_ID 0x08
#else
	revisionid = pdev->revision;
#endif
	pci_read_config_word(pdev, PCI_INTERRUPT_LINE, &irqline); // PCI_INTERRUPT_LINE 0x3c
	pdvobjpriv->irqline = irqline;


	//
	// Decide hardware type here.
	//
	if( deviceid == HAL_HW_PCI_8185_DEVICE_ID ||
	    deviceid == HAL_HW_PCI_8188_DEVICE_ID ||
	    deviceid == HAL_HW_PCI_8198_DEVICE_ID)
	{
		DBG_871X("Adapter (8185/8185B) is found- VendorID/DeviceID=%x/%x\n", venderid, deviceid);
		padapter->HardwareType=HARDWARE_TYPE_RTL8185;
	}
	else if (deviceid == HAL_HW_PCI_8190_DEVICE_ID ||
		deviceid == HAL_HW_PCI_0045_DEVICE_ID ||
		deviceid == HAL_HW_PCI_0046_DEVICE_ID ||
		deviceid == HAL_HW_PCI_DLINK_DEVICE_ID)
	{
		DBG_871X("Adapter(8190 PCI) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
		padapter->HardwareType = HARDWARE_TYPE_RTL8190P;
	}
	else if (deviceid == HAL_HW_PCI_8192_DEVICE_ID ||
		deviceid == HAL_HW_PCI_0044_DEVICE_ID ||
		deviceid == HAL_HW_PCI_0047_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8192SE_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8174_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8173_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8172_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8171_DEVICE_ID)
	{
		// 8192e and and 8192se may have the same device ID 8192. However, their Revision
		// ID is different
		// Added for 92DE. We deferentiate it from SVID,SDID.
		if( pdev->subsystem_vendor == 0x10EC && pdev->subsystem_device == 0xE020){
			padapter->HardwareType = HARDWARE_TYPE_RTL8192DE;
			DBG_871X("Adapter(8192DE) is found - VendorID/DeviceID/RID=%X/%X/%X\n", venderid, deviceid, revisionid);
		}else{
			switch (revisionid) {
				case HAL_HW_PCI_REVISION_ID_8192PCIE:
					DBG_871X("Adapter(8192 PCI-E) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
					padapter->HardwareType = HARDWARE_TYPE_RTL8192E;
					break;
				case HAL_HW_PCI_REVISION_ID_8192SE:
					DBG_871X("Adapter(8192SE) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
					padapter->HardwareType = HARDWARE_TYPE_RTL8192SE;
					break;
				default:
					DBG_871X("Err: Unknown device - vendorid/deviceid=%x/%x\n", venderid, deviceid);
					padapter->HardwareType = HARDWARE_TYPE_RTL8192SE;
					break;
			}
		}
	}
	else if(deviceid==HAL_HW_PCI_8723E_DEVICE_ID )
	{//RTL8723E may have the same device ID with RTL8192CET
		padapter->HardwareType = HARDWARE_TYPE_RTL8723AE;
		DBG_871X("Adapter(8723 PCI-E) is found - VendorID/DeviceID=%x/%x\n", venderid, deviceid);
	}
	else if (deviceid == HAL_HW_PCI_8192CET_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8192CE_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8191CE_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8188CE_DEVICE_ID)
	{
		DBG_871X("Adapter(8192C PCI-E) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
		padapter->HardwareType = HARDWARE_TYPE_RTL8192CE;
	}
	else if (deviceid == HAL_HW_PCI_8192DE_DEVICE_ID ||
		deviceid == HAL_HW_PCI_002B_DEVICE_ID ){
		padapter->HardwareType = HARDWARE_TYPE_RTL8192DE;
		DBG_871X("Adapter(8192DE) is found - VendorID/DeviceID/RID=%X/%X/%X\n", venderid, deviceid, revisionid);
	}
	else
	{
		DBG_871X("Err: Unknown device - vendorid/deviceid=%x/%x\n", venderid, deviceid);
		//padapter->HardwareType = HAL_DEFAULT_HARDWARE_TYPE;
	}


	padapter->chip_type = NULL_CHIP_TYPE;

	//TODO:
#ifdef CONFIG_RTL8192C
	padapter->chip_type = RTL8188C_8192C;
	padapter->HardwareType = HARDWARE_TYPE_RTL8192CE;
#endif
#ifdef CONFIG_RTL8192D
	pdvobjpriv->InterfaceNumber = revisionid;

	padapter->chip_type = RTL8192D;
	padapter->HardwareType = HARDWARE_TYPE_RTL8192DE;
#endif

}

static void pci_intf_start(_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+pci_intf_start\n"));
	DBG_871X("+pci_intf_start\n");

#ifdef CONFIG_PCILED_BLINK
	rtw_led_control(padapter, LED_CTL_NO_LINK);
#endif
	//Enable hw interrupt
	rtw_hal_enable_interrupt(padapter);

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-pci_intf_start\n"));
	DBG_871X("-pci_intf_start\n");
}

static void pci_intf_stop(_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+pci_intf_stop\n"));

	//Disable hw interrupt
	if(padapter->bSurpriseRemoved == _FALSE)
	{
		//device still exists, so driver can do i/o operation
		rtw_hal_disable_interrupt(padapter);
		tasklet_disable(&(padapter->recvpriv.recv_tasklet));
		tasklet_disable(&(padapter->recvpriv.irq_prepare_beacon_tasklet));
		tasklet_disable(&(padapter->xmitpriv.xmit_tasklet));

#ifdef CONFIG_CONCURRENT_MODE
		/*	This function only be called at driver removing. disable buddy_adapter too
			don't disable interrupt of buddy_adapter because it is same as primary.
		*/
		if (padapter->pbuddy_adapter){
			tasklet_disable(&(padapter->pbuddy_adapter->recvpriv.recv_tasklet));
			tasklet_disable(&(padapter->pbuddy_adapter->recvpriv.irq_prepare_beacon_tasklet));
			tasklet_disable(&(padapter->pbuddy_adapter->xmitpriv.xmit_tasklet));
		}
#endif
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("pci_intf_stop: SurpriseRemoved==_FALSE\n"));
	}
	else
	{
		// Clear irq_enabled to prevent handle interrupt function.
		adapter_to_dvobj(padapter)->irq_enabled = 0;
	}

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-pci_intf_stop\n"));

}


static void rtw_dev_unload(_adapter *padapter)
{
	struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_dev_unload\n"));

	if(padapter->bup == _TRUE)
	{
		DBG_871X("+rtw_dev_unload\n");

		padapter->bDriverStopped = _TRUE;
		#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		#endif

		//s3.
		if(padapter->intf_stop)
		{
			padapter->intf_stop(padapter);
		}

		//s4.
		rtw_stop_drv_threads(padapter);


		//s5.
		if(padapter->bSurpriseRemoved == _FALSE)
		{
			DBG_871X("r871x_dev_unload()->rtl871x_hal_deinit()\n");
			rtw_hal_deinit(padapter);

			padapter->bSurpriseRemoved = _TRUE;
		}

		padapter->bup = _FALSE;

	}
	else
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("r871x_dev_unload():padapter->bup == _FALSE\n" ));
	}

	DBG_871X("-rtw_dev_unload\n");

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-rtw_dev_unload\n"));

}

static void disable_ht_for_spec_devid(const struct pci_device_id *pdid)
{
#ifdef CONFIG_80211N_HT
	u16 vid, pid;
	u32 flags;
	int i;
	int num = sizeof(specific_device_id_tbl)/sizeof(struct specific_device_id);

	for(i=0; i<num; i++)
	{
		vid = specific_device_id_tbl[i].idVendor;
		pid = specific_device_id_tbl[i].idProduct;
		flags = specific_device_id_tbl[i].flags;

		if((pdid->vendor==vid) && (pdid->device==pid) && (flags&SPEC_DEV_ID_DISABLE_HT))
		{
			 rtw_ht_enable = 0;
			 rtw_cbw40_enable = 0;
			 rtw_ampdu_enable = 0;
		}

	}
#endif
}

#ifdef CONFIG_PM
static int rtw_suspend(struct pci_dev *pdev, pm_message_t state)
{
	_func_enter_;


	_func_exit_;
	return 0;
}

static int rtw_resume(struct pci_dev *pdev)
{
	_func_enter_;


	_func_exit_;

	return 0;
}
#endif

_adapter *rtw_pci_if1_init(struct dvobj_priv * dvobj, struct pci_dev *pdev,
	const struct pci_device_id *pdid)
{
	_adapter *padapter = NULL;
	struct net_device *pnetdev = NULL;
	int status = _FAIL;

	if ((padapter = (_adapter *)rtw_zvmalloc(sizeof(*padapter))) == NULL) {
		goto exit;
	}
	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped=_TRUE;

	dvobj->padapters[dvobj->iface_nums++] = padapter;
	padapter->iface_id = IFACE_ID0;

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	//set adapter_type/iface type for primary padapter
	padapter->isprimary = _TRUE;
	padapter->adapter_type = PRIMARY_ADAPTER;
	#ifndef CONFIG_HWPORT_SWAP
	padapter->iface_type = IFACE_PORT0;
	#else
	padapter->iface_type = IFACE_PORT1;
	#endif
#endif

	//step 1-1., decide the chip_type via vid/pid
	padapter->interface_type = RTW_PCIE;
	decide_chip_type_by_pci_device_id(padapter, pdev);

	if((pnetdev = rtw_init_netdev(padapter)) == NULL) {
		goto free_adapter;
	}
	if (dvobj->bdma64)
		pnetdev->features |= NETIF_F_HIGHDMA;
	pnetdev->irq = pdev->irq;
	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));
	padapter = rtw_netdev_priv(pnetdev);

	//step 2. hook HalFunc, allocate HalData
	if (padapter->chip_type == RTL8188C_8192C) {
		#ifdef CONFIG_RTL8192C
		rtl8192ce_set_hal_ops(padapter);
		#endif
	} else if (padapter->chip_type == RTL8192D) {
		#ifdef CONFIG_RTL8192D
		rtl8192de_set_hal_ops(padapter);
		#endif
	} else {
		DBG_871X("Detect NULL_CHIP_TYPE\n");
		goto free_hal_data;
	}

	//step 3.	initialize the dvobj_priv
	padapter->intf_start=&pci_intf_start;
	padapter->intf_stop=&pci_intf_stop;


	//.2
	if ((rtw_init_io_priv(padapter, pci_set_intf_ops)) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" \n Can't init io_reqs\n"));
		goto free_hal_data;
	}

	//.3
	rtw_hal_read_chip_version(padapter);

	//.4
	rtw_hal_chip_configure(padapter);

	//step 4. read efuse/eeprom data and get mac_addr
	rtw_hal_read_chip_info(padapter);

	if (rtw_handle_dualmac(padapter, 1) != _SUCCESS)
		goto free_hal_data;

#ifdef CONFIG_IOCTL_CFG80211
	if(rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj)) != 0) {
		goto handle_dualmac;
	}
#endif

	//step 5.
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize driver software resource Failed!\n"));
		goto free_wdev;
	}

	status = rtw_hal_inirp_init(padapter);
	if(status ==_FAIL){
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize PCI desc ring Failed!\n"));
		goto free_drv_sw;
	}

	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);
	rtw_init_wifidirect_addrs(padapter, padapter->eeprompriv.mac_addr, padapter->eeprompriv.mac_addr);


	rtw_hal_disable_interrupt(padapter);

	//step 6. Init pci related configuration
	rtw_pci_initialize_adapter_common(padapter);

	DBG_871X("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n"
		,padapter->bDriverStopped
		,padapter->bSurpriseRemoved
		,padapter->bup
		,padapter->hw_init_completed
	);

	status = _SUCCESS;

inirp_deinit:
	if (status != _SUCCESS)
		rtw_hal_inirp_deinit(padapter);
free_drv_sw:
	if (status != _SUCCESS)
		rtw_free_drv_sw(padapter);
free_wdev:
	if (status != _SUCCESS) {
		#ifdef CONFIG_IOCTL_CFG80211
		rtw_wdev_unregister(padapter->rtw_wdev);
		rtw_wdev_free(padapter->rtw_wdev);
		#endif
	}
handle_dualmac:
	if (status != _SUCCESS)
		rtw_handle_dualmac(padapter, 0);
free_hal_data:
	if (status != _SUCCESS && padapter->HalData)
		rtw_mfree(padapter->HalData, sizeof(*(padapter->HalData)));
free_adapter:
	if (status != _SUCCESS) {
		if (pnetdev)
			rtw_free_netdev(pnetdev);
		else if (padapter)
			rtw_vmfree((u8*)padapter, sizeof(*padapter));
		padapter = NULL;
	}
exit:
	return padapter;
}

static void rtw_pci_if1_deinit(_adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv= &if1->mlmepriv;

	//	padapter->intf_stop(padapter);

	if(check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, _FALSE);

#ifdef CONFIG_AP_MODE
	free_mlme_ap_info(if1);
	#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_unload(if1);
	#endif
#endif

	rtw_cancel_all_timer(if1);
#ifdef CONFIG_WOWLAN
	if1->pwrctrlpriv.wowlan_mode=_FALSE;
#endif //CONFIG_WOWLAN
	rtw_dev_unload(if1);

	DBG_871X("%s, hw_init_completed=%d\n", __func__, if1->hw_init_completed);

	//s6.
	rtw_handle_dualmac(if1, 0);

#ifdef CONFIG_IOCTL_CFG80211
	if (if1->rtw_wdev)
		rtw_wdev_free(if1->rtw_wdev);
#endif //CONFIG_IOCTL_CFG80211

	rtw_hal_inirp_deinit(if1);
	rtw_free_drv_sw(if1);

	if(pnetdev)
		rtw_free_netdev(pnetdev);

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_871X("wlan link down\n");
	rtd2885_wlan_netlink_sendMsg("linkdown", "8712");
#endif
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
*/
static int rtw_drv_init(struct pci_dev *pdev, const struct pci_device_id *did)
{
	int i, err = -ENODEV;

	int status;
	_adapter *if1 = NULL, *if2 = NULL;
	struct dvobj_priv *dvobj;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_drv_init\n"));

	//step 0.
	disable_ht_for_spec_devid(did);

	/* Initialize dvobj_priv */
	if ((dvobj = pci_dvobj_init(pdev)) == NULL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("initialize device object priv Failed!\n"));
		goto exit;
	}

	/* Initialize if1 */
	if ((if1 = rtw_pci_if1_init(dvobj, pdev, did)) == NULL) {
		DBG_871X("rtw_pci_if1_init Failed!\n");
		goto free_dvobj;
	}

	/* Initialize if2 */
#ifdef CONFIG_CONCURRENT_MODE
	if((if2 = rtw_drv_if2_init(if1, pci_set_intf_ops)) == NULL) {
		goto free_if1;
	}
#endif

#ifdef CONFIG_GLOBAL_UI_PID
	if (ui_pid[1]!=0) {
		DBG_871X("ui_pid[1]:%d\n",ui_pid[1]);
		rtw_signal_process(ui_pid[1], SIGUSR2);
	}
#endif

	//dev_alloc_name && register_netdev
	if((status = rtw_drv_register_netdev(if1)) != _SUCCESS) {
		goto free_if1;
	}

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_init(if1);
#endif

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_871X("wlan link up\n");
	rtd2885_wlan_netlink_sendMsg("linkup", "8712");
#endif

#ifdef RTK_DMP_PLATFORM
	rtw_proc_init_one(if1->pnetdev);
#endif


	/* alloc irq */
	if (pci_alloc_irq(dvobj) != _SUCCESS)
		goto free_if2;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-871x_drv - drv_init, success!\n"));
	//DBG_871X("-871x_drv - drv_init, success!\n");

	status = _SUCCESS;

free_if2:
	if(status != _SUCCESS && if2) {
		#ifdef CONFIG_CONCURRENT_MODE
		rtw_drv_if2_stop(if2);
		rtw_drv_if2_free(if2);
		#endif
	}
free_if1:
	if (status != _SUCCESS && if1) {
		rtw_pci_if1_deinit(if1);
	}
free_dvobj:
	if (status != _SUCCESS)
		pci_dvobj_deinit(pdev);
exit:
	return status == _SUCCESS?0:-ENODEV;
}

/*
 * dev_remove() - our device is being removed
*/
//rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove() => how to recognize both
static void rtw_dev_remove(struct pci_dev *pdev)
{
	struct dvobj_priv *pdvobjpriv = pci_get_drvdata(pdev);
	_adapter *padapter = pdvobjpriv->if1;
	struct net_device *pnetdev = padapter->pnetdev;

_func_exit_;

	DBG_871X("+rtw_dev_remove\n");

	pdvobjpriv->processing_dev_remove = _TRUE;

	if (unlikely(!padapter)) {
		return;
	}

	rtw_unregister_netdevs(pdvobjpriv);

	#if 0
#ifdef RTK_DMP_PLATFORM
	padapter->bSurpriseRemoved = _FALSE;	// always trate as device exists
											// this will let the driver to disable it's interrupt
#else
	if(pci_drvpriv.drv_registered == _TRUE)
	{
		//DBG_871X("r871xu_dev_remove():padapter->bSurpriseRemoved == _TRUE\n");
		padapter->bSurpriseRemoved = _TRUE;
	}
	/*else
	{
		//DBG_871X("r871xu_dev_remove():module removed\n");
		padapter->hw_init_completed = _FALSE;
	}*/
#endif
	#endif

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(&padapter->pwrctrlpriv);
#endif

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

	rtw_hal_disable_interrupt(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_stop(pdvobjpriv->if2);
#endif //CONFIG_CONCURRENT_MODE

	rtw_pci_if1_deinit(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_free(pdvobjpriv->if2);
#endif

	pci_dvobj_deinit(pdev);

	DBG_871X("-r871xu_dev_remove, done\n");

_func_exit_;
	return;
}


static int __init rtw_drv_entry(void)
{
	int ret = 0;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_drv_entry\n"));
	DBG_871X("rtw driver version=%s\n", DRIVERVERSION);
	DBG_871X("Build at: %s %s\n", __DATE__, __TIME__);
	pci_drvpriv.drv_registered = _TRUE;

	rtw_suspend_lock_init();

	ret = pci_register_driver(&pci_drvpriv.rtw_pci_drv);
	if (ret) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, (": No device found\n"));
	}

	return ret;
}

static void __exit rtw_drv_halt(void)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_drv_halt\n"));
	DBG_871X("+rtw_drv_halt\n");

	pci_drvpriv.drv_registered = _FALSE;

	pci_unregister_driver(&pci_drvpriv.rtw_pci_drv);

	rtw_suspend_lock_uninit();
	DBG_871X("-rtw_drv_halt\n");

	rtw_mstat_dump();
}


module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);

