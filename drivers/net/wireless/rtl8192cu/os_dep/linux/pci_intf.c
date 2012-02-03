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
#include <hal_init.h>
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

typedef struct _driver_priv{

	struct pci_driver rtw_pci_drv;
	int drv_registered;

}drv_priv, *pdrv_priv;


static drv_priv drvpriv = {
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
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	u8	bresult = _SUCCESS;
	int	error;

	value |= 0x40;

	error = pci_write_config_byte(pdvobjpriv->ppcidev, 0x80, value);

	if(error != 0)
	{
		bresult = _FALSE;
		DBG_8192C("rtw_pci_platform_switch_device_pci_aspm error (%d)\n",error);
	}

	return bresult;
}

// 
// When we set 0x01 to enable clk request. Set 0x0 to disable clk req.  
// 
static u8 rtw_pci_switch_clk_req(_adapter *padapter, u8 value)
{
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	u8	buffer, bresult = _SUCCESS;
	int	error;

	buffer = value;

	if(!padapter->hw_init_completed)
		return bresult;

	error = pci_write_config_byte(pdvobjpriv->ppcidev, 0x81, value);

	if(error != 0)
	{
		bresult = _FALSE;
		DBG_8192C("rtw_pci_switch_clk_req error (%d)\n",error);
	}

	return bresult;
}

#if 0
//Description: 
//Disable RTL8192SE ASPM & Disable Pci Bridge ASPM
void rtw_pci_disable_aspm(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
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
		DBG_8192C("PlatformEnableASPM(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return;
	}

	if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		DBG_8192C("%s(): Disable ASPM. Recognize the Bus of PCI(Bridge) as UNKNOWN.\n", __func__);
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

		DBG_8192C("rtw_pci_disable_aspm():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
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
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
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
		DBG_8192C("PlatformEnableASPM(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
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

	DBG_8192C("PlatformEnableASPM():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
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
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	struct rt_pci_capabilities_header capability_hdr;
	u8	capability_offset, num4bytes;
	u32	pcicfg_addrport = 0;
	u8	linkctrl_reg;
	u8	status = _FALSE;

	//If busnum, devnum, funcnum are set to 0xff.
	if (busnum == 0xff && devnum == 0xff && funcnum == 0xff) {
		DBG_8192C("GetLinkControlField(): Fail to find PCIe Capability\n");
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
		DBG_8192C("GetLinkControlField(): Cannot Find PCIe Capability\n");
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
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
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

	//DBG_8192C("==============>vendorid:%x,deviceid:%x,irql:%x\n", vendorid,deviceid,irql);
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

					DBG_8192C("GetPciBusInfo(): Find Device(%X:%X)  bus=%d dev=%d, func=%d\n",
						vendorid, deviceid, busnum_idx, devicenum_idx, functionnum_idx);
					return _TRUE;
				}
			}
		}
	}

	DBG_8192C("GetPciBusInfo(): Cannot Find Device(%X:%X:%X)\n", vendorid, deviceid, dev_venid);

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

					DBG_8192C("GetPciBridegInfo : Find Device(%X:%X)  bus=%d dev=%d, func=%d\n",
						venId, devId, busnum_idx, devicenum_idx, functionnum_idx);

					return _TRUE;
				}
			}
		}
	}

	DBG_8192C("GetPciBridegInfo(): Cannot Find PciBridge for Device\n");

	return _FALSE;
}				// end of GetPciBridegInfo

//
//Description: 
//To find specific bridge information.
//
static void rtw_find_bridge_info(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
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
			DBG_8192C("Pci Bridge Vendor is found index: %d\n", tmp);
			break;
		}
	}
	DBG_8192C("Pci Bridge Vendor is %x\n", pcibridge_vendors[tmp]);

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
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
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

		DBG_8192C("rtw_pci_disable_aspm():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
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
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
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
		DBG_8192C("rtw_pci_enable_aspm(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
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
		DBG_8192C("rtw_pci_enable_aspm(): Bridge ASPM support %x \n",u_device_aspmsupportsetting);
		if(((u_device_aspmsupportsetting & BIT(11)) != BIT(11)) || ((u_device_aspmsupportsetting & BIT(10)) != BIT(10)))
		{
			if(pdvobjpriv->const_devicepci_aspm_setting == 3)
			{
				DBG_8192C("rtw_pci_enable_aspm(): Bridge not support L0S or L1\n");
				return;
			}
			else if(pdvobjpriv->const_devicepci_aspm_setting == 2)
			{
				if((u_device_aspmsupportsetting & BIT(11)) != BIT(11))
				{
					DBG_8192C("rtw_pci_enable_aspm(): Bridge not support L1 \n");
					return;
				}
			}
			else if(pdvobjpriv->const_devicepci_aspm_setting == 1)
			{
				if((u_device_aspmsupportsetting & BIT(10)) != BIT(10))
				{
					DBG_8192C("rtw_pci_enable_aspm(): Bridge not support L0s \n");
					return;
				}

			}
		}
		else
		{
			DBG_8192C("rtw_pci_enable_aspm(): Bridge support L0s and L1 \n");
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

	DBG_8192C("PlatformEnableASPM():PciBridge busnumber[%x], DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
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

static u8 rtw_pci_get_amd_l1_patch(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
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

static void rtw_pci_get_linkcontrol_field(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	u8	capabilityoffset = pcipriv->pcibridge_pciehdr_offset;
	u8	linkctrl_reg;	
			
	/*Read  Link Control Register*/
	pci_read_config_byte(bridge_pdev, capabilityoffset + PCI_EXP_LNKCTL, &linkctrl_reg);

	pcipriv->pcibridge_linkctrlreg = linkctrl_reg;
}
#endif

static void rtw_pci_parse_configuration(struct pci_dev *pdev, _adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u8 tmp;
	int pos;
	u8 linkctrl_reg;

	//Link Control Register
	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_byte(pdev, pos + PCI_EXP_LNKCTL, &linkctrl_reg);
	pcipriv->linkctrl_reg = linkctrl_reg;

	//DBG_8192C("Link Control Register = %x\n", pcipriv->linkctrl_reg);

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
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;

	//reset pPSC->reg_rfps_level & priv->b_support_aspm
	pwrpriv->reg_rfps_level = 0;
	pwrpriv->b_support_aspm = 0;

	// Dynamic Mechanism, 
	//pAdapter->HalFunc.SetHalDefVarHandler(pAdapter, HAL_DEF_INIT_GAIN, &(pDevice->InitGainState));

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
	_adapter			*padapter = (_adapter *)priv;
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;


	if (pdvobjpriv->irq_enabled == 0) {
		return IRQ_HANDLED;
	}

	if(padapter->HalFunc.interrupt_handler(padapter) == _FAIL)
		return IRQ_HANDLED;
		//return IRQ_NONE;

	return IRQ_HANDLED;
}

static u32 pci_dvobj_init(_adapter *padapter)
{
	u32	status = _SUCCESS;
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	u8	tmp;

_func_enter_;

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
				DBG_8192C("Pci Bridge Vendor is found index: %d, %x\n", tmp, pcibridge_vendors[tmp]);
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

		rtw_pci_get_linkcontrol_field(padapter);
		
		if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_AMD) {
			pcipriv->amd_l1_patch = rtw_pci_get_amd_l1_patch(padapter);
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
	rtw_pci_parse_configuration(pdev, padapter);

	DBG_8192C("pcidev busnumber:devnumber:funcnumber:"
		"vendor:link_ctl %d:%d:%d:%x:%x\n",
		pcipriv->busnumber,
		pcipriv->devnumber,
		pcipriv->funcnumber,
		pdev->vendor,
		pcipriv->linkctrl_reg);

	DBG_8192C("pci_bridge busnumber:devnumber:funcnumber:vendor:"
		"pcie_cap:link_ctl_reg: %d:%d:%d:%x:%x:%x:%x\n", 
		pcipriv->pcibridge_busnum,
		pcipriv->pcibridge_devnum,
		pcipriv->pcibridge_funcnum,
		pcibridge_vendors[pcipriv->pcibridge_vendor],
		pcipriv->pcibridge_pciehdr_offset,
		pcipriv->pcibridge_linkctrlreg,
		pcipriv->amd_l1_patch);

	//.2
	if ((rtw_init_io_priv(padapter)) == _FAIL)
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" \n Can't init io_reqs\n"));
		status = _FAIL;
	}

	//.3
	intf_read_chip_version(padapter);
	//.4
	intf_chip_configure(padapter);

_func_exit_;

	return status;
}

static void pci_dvobj_deinit(_adapter * padapter)
{
	//struct dvobj_priv *pdvobjpriv=&padapter->dvobjpriv;

_func_enter_;

_func_exit_;
}


static void decide_chip_type_by_pci_device_id(_adapter *padapter, struct pci_dev *pdev)
{
	u16	venderid, deviceid, irqline;
	u8	revisionid;
	struct dvobj_priv	*pdvobjpriv=&padapter->dvobjpriv;


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
		DBG_8192C("Adapter (8185/8185B) is found- VendorID/DeviceID=%x/%x\n", venderid, deviceid);
		padapter->HardwareType=HARDWARE_TYPE_RTL8185;
	}
	else if (deviceid == HAL_HW_PCI_8190_DEVICE_ID ||
		deviceid == HAL_HW_PCI_0045_DEVICE_ID ||
		deviceid == HAL_HW_PCI_0046_DEVICE_ID ||
		deviceid == HAL_HW_PCI_DLINK_DEVICE_ID)
	{
		DBG_8192C("Adapter(8190 PCI) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
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
			DBG_8192C("Adapter(8192DE) is found - VendorID/DeviceID/RID=%X/%X/%X\n", venderid, deviceid, revisionid);
		}else{
			switch (revisionid) {
				case HAL_HW_PCI_REVISION_ID_8192PCIE:
					DBG_8192C("Adapter(8192 PCI-E) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
					padapter->HardwareType = HARDWARE_TYPE_RTL8192E;
					break;
				case HAL_HW_PCI_REVISION_ID_8192SE:
					DBG_8192C("Adapter(8192SE) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
					padapter->HardwareType = HARDWARE_TYPE_RTL8192SE;
					break;
				default:
					DBG_8192C("Err: Unknown device - vendorid/deviceid=%x/%x\n", venderid, deviceid);
					padapter->HardwareType = HARDWARE_TYPE_RTL8192SE;
					break;
			}
		}
	}
	else if(deviceid==HAL_HW_PCI_8723E_DEVICE_ID )
	{//RTL8723E may have the same device ID with RTL8192CET
		padapter->HardwareType = HARDWARE_TYPE_RTL8723E;
		DBG_8192C("Adapter(8723 PCI-E) is found - VendorID/DeviceID=%x/%x\n", venderid, deviceid);
	}
	else if (deviceid == HAL_HW_PCI_8192CET_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8192CE_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8191CE_DEVICE_ID ||
		deviceid == HAL_HW_PCI_8188CE_DEVICE_ID) 
	{
		DBG_8192C("Adapter(8192C PCI-E) is found - vendorid/deviceid=%x/%x\n", venderid, deviceid);
		padapter->HardwareType = HARDWARE_TYPE_RTL8192CE;
	}
	else if (deviceid == HAL_HW_PCI_8192DE_DEVICE_ID ||
		deviceid == HAL_HW_PCI_002B_DEVICE_ID ){
		padapter->HardwareType = HARDWARE_TYPE_RTL8192DE;
		DBG_8192C("Adapter(8192DE) is found - VendorID/DeviceID/RID=%X/%X/%X\n", venderid, deviceid, revisionid);
	}
	else
	{
		DBG_8192C("Err: Unknown device - vendorid/deviceid=%x/%x\n", venderid, deviceid);
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
	DBG_8192C("+pci_intf_start\n");

	//Enable hw interrupt
	padapter->HalFunc.enable_interrupt(padapter);

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-pci_intf_start\n"));
	DBG_8192C("-pci_intf_start\n");
}

static void pci_intf_stop(_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+pci_intf_stop\n"));

	//Disable hw interrupt
	if(padapter->bSurpriseRemoved == _FALSE)
	{
		//device still exists, so driver can do i/o operation
		padapter->HalFunc.disable_interrupt(padapter);
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("pci_intf_stop: SurpriseRemoved==_FALSE\n"));
	}
	else
	{
		// Clear irq_enabled to prevent handle interrupt function.
		padapter->dvobjpriv.irq_enabled = 0;
	}

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-pci_intf_stop\n"));

}


static void rtw_dev_unload(_adapter *padapter)
{
	struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_dev_unload\n"));

	if(padapter->bup == _TRUE)
	{
		DBG_8192C("+rtw_dev_unload\n");
		//s1.
/*		if(pnetdev)
		{
			netif_carrier_off(pnetdev);
			netif_stop_queue(pnetdev);
		}

		//s2.
		//s2-1.  issue rtw_disassoc_cmd to fw
		rtw_disassoc_cmd(padapter);
		//s2-2.  indicate disconnect to os
		rtw_indicate_disconnect(padapter);
		//s2-3.
		rtw_free_assoc_resources(padapter, 1);
		//s2-4.
		rtw_free_network_queue(padapter, _TRUE);*/

		padapter->bDriverStopped = _TRUE;

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
			DBG_8192C("r871x_dev_unload()->rtl871x_hal_deinit()\n");
			rtw_hal_deinit(padapter);

			padapter->bSurpriseRemoved = _TRUE;
		}

		padapter->bup = _FALSE;

	}
	else
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("r871x_dev_unload():padapter->bup == _FALSE\n" ));
	}

	DBG_8192C("-rtw_dev_unload\n");

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

#ifdef RTK_DMP_PLATFORM
#define pci_iounmap(x,y) iounmap(y)
#endif

extern char* ifname;

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
*/
static int rtw_drv_init(struct pci_dev *pdev, const struct pci_device_id *pdid)
{
	int i, err = -ENODEV;

	uint status;
	_adapter *padapter = NULL;
	struct dvobj_priv *pdvobjpriv;
	struct net_device *pnetdev;
	unsigned long pmem_start, pmem_len, pmem_flags;
	u8	bdma64 = _FALSE;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_drv_init\n"));
	//DBG_8192C("+rtw_drv_init\n");

	err = pci_enable_device(pdev);
	if (err) {
		DBG_8192C(KERN_ERR "%s : Cannot enable new PCI device\n", pci_name(pdev));
		return err;
	}

#ifdef CONFIG_64BIT_DMA
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		DBG_8192C("RTL819xCE: Using 64bit DMA\n");
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
			DBG_8192C(KERN_ERR "Unable to obtain 64bit DMA for consistent allocations\n");
			err = -ENOMEM;
			pci_disable_device(pdev);
			return err;
		}
		bdma64 = _TRUE;
	} else 
#endif
	{
		if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
			if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
				DBG_8192C(KERN_ERR "Unable to obtain 32bit DMA for consistent allocations\n");
				err = -ENOMEM;
				pci_disable_device(pdev);
				return err;
			}
		}
	}

	pci_set_master(pdev);

	//step 0.
	disable_ht_for_spec_devid(pdid);


	//step 1. set USB interface data
	// init data
	pnetdev = rtw_init_netdev(NULL);
	if (!pnetdev){
		err = -ENOMEM;
		goto fail1;
	}
	rtw_init_netdev_name(pnetdev,ifname);

	if(bdma64){
		pnetdev->features |= NETIF_F_HIGHDMA;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
	SET_NETDEV_DEV(pnetdev, &pdev->dev);
#endif

	padapter = rtw_netdev_priv(pnetdev);
	pdvobjpriv = &padapter->dvobjpriv;
	pdvobjpriv->padapter = padapter;
	pdvobjpriv->ppcidev = pdev;

	// set data
	pci_set_drvdata(pdev, pnetdev);

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		DBG_8192C(KERN_ERR "Can't obtain PCI resources\n");
		goto fail1;
	}
	//MEM map
	pmem_start = pci_resource_start(pdev, 2);
	pmem_len = pci_resource_len(pdev, 2);
	pmem_flags = pci_resource_flags(pdev, 2);

#ifdef RTK_DMP_PLATFORM
	pdvobjpriv->pci_mem_start = (unsigned long)ioremap_nocache( pmem_start, pmem_len);
#else
	pdvobjpriv->pci_mem_start = (unsigned long)pci_iomap(pdev, 2, pmem_len);	// shared mem start
#endif
	if (pdvobjpriv->pci_mem_start == 0) {
		DBG_8192C(KERN_ERR "Can't map PCI mem\n");
		goto fail2;
	}

	DBG_8192C("Memory mapped space start: 0x%08lx len:%08lx flags:%08lx, after map:0x%08lx\n",
		pmem_start, pmem_len, pmem_flags, pdvobjpriv->pci_mem_start);

	// Disable Clk Request */
	pci_write_config_byte(pdev, 0x81, 0);
	// leave D3 mode */
	pci_write_config_byte(pdev, 0x44, 0);
	pci_write_config_byte(pdev, 0x04, 0x06);
	pci_write_config_byte(pdev, 0x04, 0x07);


	//set interface_type to usb
	padapter->interface_type = RTW_PCIE;

	//step 1-1., decide the chip_type via vid/pid
	decide_chip_type_by_pci_device_id(padapter, pdev);

	//step 2.	
	if(padapter->chip_type== RTL8188C_8192C)
	{
#ifdef CONFIG_RTL8192C
		rtl8192ce_set_hal_ops(padapter);
#endif
	}
	else if(padapter->chip_type == RTL8192D)
	{
#ifdef CONFIG_RTL8192D
		rtl8192de_set_hal_ops(padapter);
#endif
	}
	else
	{
		status = _FAIL;
		goto error;
	}

	//step 3.	initialize the dvobj_priv 
	padapter->dvobj_init=&pci_dvobj_init;
	padapter->dvobj_deinit=&pci_dvobj_deinit;
	padapter->intf_start=&pci_intf_start;
	padapter->intf_stop=&pci_intf_stop;

	if (padapter->dvobj_init == NULL){
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n Initialize dvobjpriv.dvobj_init error!!!\n"));
		goto error;
	}

	status = padapter->dvobj_init(padapter);	
	if (status != _SUCCESS) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("initialize device object priv Failed!\n"));
		goto error;
	}

	pnetdev->irq = pdev->irq;

	//step 4. read efuse/eeprom data and get mac_addr
	intf_read_chip_info(padapter);	

	//step 5. 
	status = rtw_init_drv_sw(padapter);
	if(status ==_FAIL){
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize driver software resource Failed!\n"));
		goto error;
	}

	status = padapter->HalFunc.inirp_init(padapter);
	if(status ==_FAIL){
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize PCI desc ring Failed!\n"));
		goto error;
	}

	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);

	_rtw_memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);
	DBG_8192C("MAC Address from pnetdev->dev_addr= "MAC_FMT"\n", MAC_ARG(pnetdev->dev_addr));	


	padapter->HalFunc.disable_interrupt(padapter);

#if defined(IRQF_SHARED)
	err = request_irq(pdev->irq, &rtw_pci_interrupt, IRQF_SHARED, DRV_NAME, padapter);
#else
	err = request_irq(pdev->irq, &rtw_pci_interrupt, SA_SHIRQ, DRV_NAME, padapter);
#endif
	if (err) {
		DBG_8192C("Error allocating IRQ %d",pdev->irq);
		goto error;
	} else {
		pdvobjpriv->irq_alloc = 1;
		DBG_8192C("Request_irq OK, IRQ %d\n",pdev->irq);
	}

	//step 6. Init pci related configuration
	rtw_pci_initialize_adapter_common(padapter);

	//step 7.
	/* Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("register_netdev() failed\n"));
		goto error;
	}

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-drv_init - Adapter->bDriverStopped=%d, Adapter->bSurpriseRemoved=%d\n",padapter->bDriverStopped, padapter->bSurpriseRemoved));
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-871x_drv - drv_init, success!\n"));
	//DBG_8192C("-871x_drv - drv_init, success!\n");

#ifdef CONFIG_PROC_DEBUG
#ifdef RTK_DMP_PLATFORM
	rtw_proc_init_one(pnetdev);
#endif
#endif

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_init(padapter);
#endif

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_8192C("wlan link up\n");
	rtd2885_wlan_netlink_sendMsg("linkup", "8712");
#endif

	return 0;

error:

	pci_set_drvdata(pdev, NULL);

	if (pdvobjpriv->irq_alloc) {
		free_irq(pdev->irq, padapter);
		pdvobjpriv->irq_alloc = 0;
	}

	if (pdvobjpriv->pci_mem_start != 0) {
		pci_iounmap(pdev, (void *)pdvobjpriv->pci_mem_start);
	}

	pci_dvobj_deinit(padapter);

	if (pnetdev)
	{
		//unregister_netdev(pnetdev);
		rtw_free_netdev(pnetdev);
	}

fail2:
	pci_release_regions(pdev);

fail1:
	pci_disable_device(pdev);

	DBG_8192C("-871x_pci - drv_init, fail!\n");

	return err;
}

/*
 * dev_remove() - our device is being removed
*/
//rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove() => how to recognize both
static void rtw_dev_remove(struct pci_dev *pdev)
{
	struct net_device *pnetdev=pci_get_drvdata(pdev);
	_adapter *padapter = (_adapter*)rtw_netdev_priv(pnetdev);
	struct dvobj_priv *pdvobjpriv = &padapter->dvobjpriv;

_func_exit_;

	if (unlikely(!padapter)) {
		return;
	}

	DBG_8192C("+rtw_dev_remove\n");

	LeaveAllPowerSaveMode(padapter);

#ifdef RTK_DMP_PLATFORM    
	padapter->bSurpriseRemoved = _FALSE;	// always trate as device exists
                                                // this will let the driver to disable it's interrupt
#else	
	if(drvpriv.drv_registered == _TRUE)
	{
		//DBG_8192C("r871xu_dev_remove():padapter->bSurpriseRemoved == _TRUE\n");
		padapter->bSurpriseRemoved = _TRUE;
	}
	/*else
	{
		//DBG_8192C("r871xu_dev_remove():module removed\n");
		padapter->hw_init_completed = _FALSE;
	}*/
#endif


#ifdef CONFIG_AP_MODE
		free_mlme_ap_info(padapter);
#ifdef CONFIG_HOSTAPD_MLME
		hostapd_mode_unload(padapter);
#endif //CONFIG_HOSTAPD_MLME
#endif //CONFIG_AP_MODE

	if(pnetdev){
		unregister_netdev(pnetdev); //will call netdev_close()
#ifdef CONFIG_PROC_DEBUG
		rtw_proc_remove_one(pnetdev);
#endif
	}

	rtw_cancel_all_timer(padapter);

	rtw_dev_unload(padapter);

	DBG_8192C("+r871xu_dev_remove, hw_init_completed=%d\n", padapter->hw_init_completed);

	if (pdvobjpriv->irq_alloc) {
		free_irq(pdev->irq, padapter);
		pdvobjpriv->irq_alloc = 0;
	}

	if (pdvobjpriv->pci_mem_start != 0) {
		pci_iounmap(pdev, (void *)pdvobjpriv->pci_mem_start);
		pci_release_regions(pdev);
	}

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	padapter->HalFunc.inirp_deinit(padapter);
	//s6.
	if(padapter->dvobj_deinit)
	{
		padapter->dvobj_deinit(padapter);
	}
	else
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize hcipriv.hci_priv_init error!!!\n"));
	}
	
	rtw_free_drv_sw(padapter);

	//after rtw_free_drv_sw(), padapter has beed freed, don't refer to it.

	DBG_8192C("-r871xu_dev_remove, done\n");

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_8192C("wlan link down\n");
	rtd2885_wlan_netlink_sendMsg("linkdown", "8712");
#endif

_func_exit_;

	return;

}


static int __init rtw_drv_entry(void)
{
	int ret = 0;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_drv_entry\n"));
	DBG_8192C("rtw driver version=%s\n", DRIVERVERSION);
	drvpriv.drv_registered = _TRUE;
	ret = pci_register_driver(&drvpriv.rtw_pci_drv);
	if (ret) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, (": No device found\n"));
	}

	return ret;
}

static void __exit rtw_drv_halt(void)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_drv_halt\n"));
	DBG_8192C("+rtw_drv_halt\n");
	drvpriv.drv_registered = _FALSE;
	pci_unregister_driver(&drvpriv.rtw_pci_drv);
	DBG_8192C("-rtw_drv_halt\n");
}


module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);

