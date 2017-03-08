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

#include <drv_types.h>
#include <hal_data.h>

#include <linux/pci_regs.h>

#ifndef CONFIG_PCI_HCI

	#error "CONFIG_PCI_HCI shall be on!\n"

#endif


#if defined(PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

	#error "Shall be Linux or Windows, but not both!\n"

#endif

#ifdef CONFIG_80211N_HT
	extern int rtw_ht_enable;
	extern int rtw_bw_mode;
	extern int rtw_ampdu_enable;/* for enable tx_ampdu */
#endif

#ifdef CONFIG_GLOBAL_UI_PID
int ui_pid[3] = {0, 0, 0};
#endif

extern int pm_netdev_open(struct net_device *pnetdev, u8 bnormal);
int rtw_resume_process(_adapter *padapter);

#ifdef CONFIG_PM
	static int rtw_pci_suspend(struct pci_dev *pdev, pm_message_t state);
	static int rtw_pci_resume(struct pci_dev *pdev);
#endif

static int rtw_drv_init(struct pci_dev *pdev, const struct pci_device_id *pdid);
static void rtw_dev_remove(struct pci_dev *pdev);

static struct specific_device_id specific_device_id_tbl[] = {
	{.idVendor = 0x0b05, .idProduct = 0x1791, .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x13D3, .idProduct = 0x3311, .flags = SPEC_DEV_ID_DISABLE_HT},
	{}
};

struct pci_device_id rtw_pci_id_tbl[] = {
#ifdef CONFIG_RTL8188E
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8179), .driver_data = RTL8188E},
#endif
#ifdef CONFIG_RTL8812A
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8812), .driver_data = RTL8812},
#endif
#ifdef CONFIG_RTL8821A
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8821), .driver_data = RTL8821},
#endif
#ifdef CONFIG_RTL8192E
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x818B), .driver_data = RTL8192E},
#endif
#ifdef CONFIG_RTL8723B
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0xb723), .driver_data = RTL8723B},
#endif
#ifdef CONFIG_RTL8723D
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0xd723), .driver_data = RTL8723D},
#endif
#ifdef CONFIG_RTL8814A
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0x8813), .driver_data = RTL8814A},
#endif
#ifdef CONFIG_RTL8822B
	{PCI_DEVICE(PCI_VENDER_ID_REALTEK, 0xB822), .driver_data = RTL8822B},
#endif
	{},
};

struct pci_drv_priv {
	struct pci_driver rtw_pci_drv;
	int drv_registered;
};


static struct pci_drv_priv pci_drvpriv = {
	.rtw_pci_drv.name = (char *)DRV_NAME,
	.rtw_pci_drv.probe = rtw_drv_init,
	.rtw_pci_drv.remove = rtw_dev_remove,
	.rtw_pci_drv.shutdown = rtw_dev_remove,
	.rtw_pci_drv.id_table = rtw_pci_id_tbl,
#ifdef CONFIG_PM
	.rtw_pci_drv.suspend = rtw_pci_suspend,
	.rtw_pci_drv.resume = rtw_pci_resume,
#endif
};


MODULE_DEVICE_TABLE(pci, rtw_pci_id_tbl);


static u16 pcibridge_vendors[PCI_BRIDGE_VENDOR_MAX] = {
	INTEL_VENDOR_ID,
	ATI_VENDOR_ID,
	AMD_VENDOR_ID,
	SIS_VENDOR_ID,
	NVI_VENDOR_ID
};

#define PCI_PM_CAP_ID		0x01	/* The Capability ID for PME function */
void	PlatformClearPciPMEStatus(PADAPTER Adapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	BOOLEAN		PCIClkReq = _FALSE;
	u8	CapId = 0xff;
	u8	CapPointer = 0;
	/* u16	CapHdr; */
	RT_PCI_CAPABILITIES_HEADER CapHdr;
	u8	PMCSReg;
	int	result;

	/* Get the Capability pointer first, */
	/* the Capability Pointer is located at offset 0x34 from the Function Header */

	result = pci_read_config_byte(pdev, 0x34, &CapPointer);
	if (result != 0)
		RTW_INFO("%s() pci_read_config_byte 0x34 Failed!\n", __FUNCTION__);
	else {
		RTW_INFO("PlatformClearPciPMEStatus(): PCI configration 0x34 = 0x%2x\n", CapPointer);
		do {
			/* end of pci capability */
			if (CapPointer == 0x00) {
				CapId = 0xff;
				break;
			}

			/* result = pci_read_config_word(pdev, CapPointer, &CapHdr); */
			result = pci_read_config_byte(pdev, CapPointer, &CapHdr.CapabilityID);
			if (result != 0) {
				RTW_INFO("%s() pci_read_config_byte %x Failed!\n", __FUNCTION__, CapPointer);
				CapId = 0xff;
				break;
			}

			result = pci_read_config_byte(pdev, CapPointer + 1, &CapHdr.Next);
			if (result != 0) {
				RTW_INFO("%s() pci_read_config_byte %x Failed!\n", __FUNCTION__, CapPointer);
				CapId = 0xff;
				break;
			}

			/* CapId = CapHdr & 0xFF; */
			CapId = CapHdr.CapabilityID;

			RTW_INFO("PlatformClearPciPMEStatus(): in pci configration1, CapPointer%x = %x\n", CapPointer, CapId);

			if (CapId == PCI_PM_CAP_ID)
				break;
			else {
				/* point to next Capability */
				/* CapPointer = (CapHdr >> 8) & 0xFF; */
				CapPointer = CapHdr.Next;
			}
		} while (_TRUE);

		if (CapId == PCI_PM_CAP_ID) {
			/* Get the PM CSR (Control/Status Register), */
			/* The PME_Status is located at PM Capatibility offset 5, bit 7 */
			result = pci_read_config_byte(pdev, CapPointer + 5, &PMCSReg);
			if (PMCSReg & BIT7) {
				/* PME event occured, clear the PM_Status by write 1 */
				PMCSReg = PMCSReg | BIT7;

				pci_write_config_byte(pdev, CapPointer + 5, PMCSReg);
				PCIClkReq = _TRUE;
				/* Read it back to check */
				pci_read_config_byte(pdev, CapPointer + 5, &PMCSReg);
				RTW_INFO("PlatformClearPciPMEStatus(): Clear PME status 0x%2x to 0x%2x\n", CapPointer + 5, PMCSReg);
			} else
				RTW_INFO("PlatformClearPciPMEStatus(): PME status(0x%2x) = 0x%2x\n", CapPointer + 5, PMCSReg);
		} else
			RTW_INFO("PlatformClearPciPMEStatus(): Cannot find PME Capability\n");
	}

	RTW_INFO("PME, value_offset = %x, PME EN = %x\n", CapPointer + 5, PCIClkReq);
}

static u8 rtw_pci_platform_switch_device_pci_aspm(_adapter *padapter, u8 value)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	BOOLEAN		bResult = _FALSE;
	int	Result = 0;
	int	error;

	Result = pci_write_config_byte(pdvobjpriv->ppcidev, pcipriv->pciehdr_offset + 0x10, value);	/* enable I/O space */
	RTW_INFO("PlatformSwitchDevicePciASPM(0x%x) = 0x%x\n", pcipriv->pciehdr_offset + 0x10, value);
	if (Result != 0) {
		RTW_INFO("PlatformSwitchDevicePciASPM() Failed!\n");
		bResult = _FALSE;
	} else
		bResult = _TRUE;

	return bResult;
}

/*
 * When we set 0x01 to enable clk request. Set 0x0 to disable clk req.
 *   */
static u8 rtw_pci_switch_clk_req(_adapter *padapter, u8 value)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	u8	buffer, bResult = _FALSE;
	int	error;

	buffer = value;

	if (!rtw_is_hw_init_completed(padapter))
		return bResult;

	/* the clock request is located at offset 0x81, suppose the PCIE Capability register is located at offset 0x70 */
	/* the correct code should be: search the PCIE capability register first and then the clock request is located offset 0x11 */
	error = pci_write_config_byte(pdvobjpriv->ppcidev, 0x81, buffer);
	if (error != 0)
		RTW_INFO("rtw_pci_switch_clk_req error (%d)\n", error);
	else
		bResult = _TRUE;

	return bResult;
}

/*Disable RTL8192SE ASPM & Disable Pci Bridge ASPM*/
void rtw_pci_disable_aspm(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pwrctrl_priv	*pwrpriv = dvobj_to_pwrctl(pdvobjpriv);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u8	linkctrl_reg;
	u8	pcibridge_linkctrlreg, aspmlevel = 0;


	/* We shall check RF Off Level for ASPM function instead of registry settings, revised by Roger, 2013.03.29. */
	if (!(pwrpriv->reg_rfps_level & (RT_RF_LPS_LEVEL_ASPM | RT_RF_PS_LEVEL_ALWAYS_ASPM)))
		return;

	if (!rtw_is_hw_init_completed(padapter))
		return;

	if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s(): PCI(Bridge) UNKNOWN.\n", __FUNCTION__));
		return;
	}

	linkctrl_reg = pcipriv->linkctrl_reg;
	pcibridge_linkctrlreg = pcipriv->pcibridge_linkctrlreg;

	/* Set corresponding value. */
	aspmlevel |= BIT(0) | BIT(1);
	linkctrl_reg &= ~aspmlevel;
	pcibridge_linkctrlreg &= ~aspmlevel;

	/*  */
	/* 09/08/21 MH From Sd1 suggestion. we need to adjust ASPM enable sequence */
	/* CLK_REQ ==> delay 50us ==> Device ==> Host ==> delay 50us */
	/*  */

	if (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		RT_CLEAR_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_CLK_REQ);
		rtw_pci_switch_clk_req(padapter, 0x0);
	}

	{
		/*for promising device will in L0 state after an I/O.*/
		u8 tmp_u1b;
		pci_read_config_byte(pdev, (pcipriv->pciehdr_offset + 0x10), &tmp_u1b);
	}

	rtw_pci_platform_switch_device_pci_aspm(padapter, linkctrl_reg);

	rtw_udelay_os(50);

	/* When there exists anyone's BusNum, DevNum, and FuncNum that are set to 0xff, */
	/* we do not execute any action and return. Added by tynli. */
	if ((pcipriv->busnumber == 0xff && pcipriv->devnumber == 0xff && pcipriv->funcnumber == 0xff) ||
	    (pcipriv->pcibridge_busnum == 0xff && pcipriv->pcibridge_devnum == 0xff && pcipriv->pcibridge_funcnum == 0xff)) {
		/* Do Nothing!! */
	} else {
		/* 4  */ /* Disable Pci Bridge ASPM */
		pci_write_config_byte(bridge_pdev, (pcipriv->pcibridge_pciehdr_offset + 0x10), pcibridge_linkctrlreg);
		RTW_INFO("PlatformDisableASPM():PciBridge Write reg[%x] = %x\n",
			(pcipriv->pcibridge_pciehdr_offset + 0x10), pcibridge_linkctrlreg);
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
	struct pwrctrl_priv	*pwrpriv = dvobj_to_pwrctl(pdvobjpriv);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	u16	aspmlevel = 0;
	u8	u_pcibridge_aspmsetting = 0;
	u8	u_device_aspmsetting = 0;
	u32	u_device_aspmsupportsetting = 0;


	/* We shall check RF Off Level for ASPM function instead of registry settings, revised by Roger, 2013.03.29. */
	if (!(pwrpriv->reg_rfps_level & (RT_RF_LPS_LEVEL_ASPM | RT_RF_PS_LEVEL_ALWAYS_ASPM)))
		return;

	/* When there exists anyone's BusNum, DevNum, and FuncNum that are set to 0xff, */
	/* we do not execute any action and return. Added by tynli. */
	if ((pcipriv->busnumber == 0xff && pcipriv->devnumber == 0xff && pcipriv->funcnumber == 0xff) ||
	    (pcipriv->pcibridge_busnum == 0xff && pcipriv->pcibridge_devnum == 0xff && pcipriv->pcibridge_funcnum == 0xff)) {
		RTW_INFO("rtw_pci_enable_aspm(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return;
	}

	/* Get Bridge ASPM Support
	 * not to enable bridge aspm if bridge does not support
	 * Added by sherry 20100803	 */
	{
		/* Get the Link Capability, it ls located at offset 0x0c from the PCIE Capability */
		pci_read_config_dword(bridge_pdev, (pcipriv->pcibridge_pciehdr_offset + 0x0C), &u_device_aspmsupportsetting);

		RTW_INFO("rtw_pci_enable_aspm(): Bridge ASPM support %x\n", u_device_aspmsupportsetting);
		if (((u_device_aspmsupportsetting & BIT(11)) != BIT(11)) || ((u_device_aspmsupportsetting & BIT(10)) != BIT(10))) {
			if (pdvobjpriv->const_devicepci_aspm_setting == 3) {
				RTW_INFO("rtw_pci_enable_aspm(): Bridge not support L0S or L1\n");
				return;
			} else if (pdvobjpriv->const_devicepci_aspm_setting == 2) {
				if ((u_device_aspmsupportsetting & BIT(11)) != BIT(11)) {
					RTW_INFO("rtw_pci_enable_aspm(): Bridge not support L1\n");
					return;
				}
			} else if (pdvobjpriv->const_devicepci_aspm_setting == 1) {
				if ((u_device_aspmsupportsetting & BIT(10)) != BIT(10)) {
					RTW_INFO("rtw_pci_enable_aspm(): Bridge not support L0s\n");
					return;
				}

			}
		} else
			RTW_INFO("rtw_pci_enable_aspm(): Bridge support L0s and L1\n");
	}

	/*
	* Skip following settings if ASPM has already enabled, added by Roger, 2013.03.15.
	*   */
	if ((pcipriv->pcibridge_linkctrlreg & (BIT0 | BIT1)) &&
	    (pcipriv->linkctrl_reg & (BIT0 | BIT1))) {
		/* BIT0: L0S, BIT1:L1 */

		RTW_INFO("PlatformEnableASPM(): ASPM is already enabled, skip incoming settings!!\n");
		return;
	}

	/* 4 Enable Pci Bridge ASPM */
	/* Write PCI bridge PCIE-capability Link Control Register */
	/* Justin: Can we change the ASPM Control register ? */
	/* The system BIOS should set this register with a correct value */
	/* If we change the force enable the ASPM L1/L0s, this may cause the system hang */
	u_pcibridge_aspmsetting = pcipriv->pcibridge_linkctrlreg;
	u_pcibridge_aspmsetting |= pdvobjpriv->const_hostpci_aspm_setting;

	if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL ||
	    pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_SIS)
		u_pcibridge_aspmsetting &= ~BIT(0); /* for intel host 42 device 43 */

	pci_write_config_byte(bridge_pdev, (pcipriv->pcibridge_pciehdr_offset + 0x10), u_pcibridge_aspmsetting);
	RTW_INFO("PlatformEnableASPM():PciBridge Write reg[%x] = %x\n",
		 (pcipriv->pcibridge_pciehdr_offset + 0x10),
		 u_pcibridge_aspmsetting);

	rtw_udelay_os(50);

	/*Get ASPM level (with/without Clock Req)*/
	aspmlevel |= pdvobjpriv->const_devicepci_aspm_setting;
	u_device_aspmsetting = pcipriv->linkctrl_reg;
	u_device_aspmsetting |= aspmlevel; /* device 43 */

	rtw_pci_platform_switch_device_pci_aspm(padapter, u_device_aspmsetting);

	if (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		rtw_pci_switch_clk_req(padapter, (pwrpriv->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) ? 1 : 0);
		RT_SET_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_CLK_REQ);
	}

	rtw_udelay_os(50);
}

static u8 rtw_pci_get_amd_l1_patch(struct dvobj_priv *pdvobjpriv, struct pci_dev *pdev)
{
	u8	status = _FALSE;
	u8	offset_e0;
	u32	offset_e4;

	pci_write_config_byte(pdev, 0xE0, 0xA0);
	pci_read_config_byte(pdev, 0xE0, &offset_e0);

	if (offset_e0 == 0xA0) {
		pci_read_config_dword(pdev, 0xE4, &offset_e4);
		if (offset_e4 & BIT(23))
			status = _TRUE;
	}

	return status;
}

static s32	rtw_pci_get_linkcontrol_reg(struct pci_dev *pdev, u8 *LinkCtrlReg, u8 *HdrOffset)
{
	u8 CapabilityPointer;
	RT_PCI_CAPABILITIES_HEADER	CapabilityHdr;
	s32 status = _FAIL;

	/* get CapabilityOffset */
	pci_read_config_byte(pdev, 0x34, &CapabilityPointer);	/* the capability pointer is located offset 0x34 */

	/* Loop through the capabilities in search of the power management capability. */
	/* The list is NULL-terminated, so the last offset will always be zero. */

	while (CapabilityPointer != 0) {
		/* Read the header of the capability at  this offset. If the retrieved capability is not */
		/* the power management capability that we are looking for, follow the link to the  */
		/* next capability and continue looping. */

		/* 4 get CapabilityHdr */
		/* pci_read_config_word(pdev, CapabilityPointer, (u16 *)&CapabilityHdr); */
		pci_read_config_byte(pdev, CapabilityPointer, (u8 *)&CapabilityHdr.CapabilityID);
		pci_read_config_byte(pdev, CapabilityPointer + 1, (u8 *)&CapabilityHdr.Next);

		/* Found the PCI express capability */
		if (CapabilityHdr.CapabilityID == PCI_CAPABILITY_ID_PCI_EXPRESS)
			break;
		else {
			/* This is some other capability. Keep looking for the PCI express capability. */
			CapabilityPointer = CapabilityHdr.Next;
		}
	}

	/* Get the Link Control Register, it located at offset 0x10 from the Capability Header */
	if (CapabilityHdr.CapabilityID == PCI_CAPABILITY_ID_PCI_EXPRESS) {
		*HdrOffset = CapabilityPointer;
		pci_read_config_byte(pdev, CapabilityPointer + 0x10, LinkCtrlReg);

		status = _SUCCESS;
	} else {
		/* We didn't find a PCIe capability. */
		RTW_INFO("GetPciLinkCtrlReg(): Cannot Find PCIe Capability\n");
	}

	return status;
}

static s32	rtw_set_pci_cache_line_size(struct pci_dev *pdev, u8 CacheLineSizeToSet)
{
	u8	ucPciCacheLineSize;
	s32	Result;

	/* ucPciCacheLineSize  = pPciConfig->CacheLineSize; */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &ucPciCacheLineSize);

	if (ucPciCacheLineSize < 8 || ucPciCacheLineSize > 16) {
		RTW_INFO("Driver Sets default Cache Line Size...\n");

		ucPciCacheLineSize = CacheLineSizeToSet;

		Result = pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, ucPciCacheLineSize);

		if (Result != 0) {
			RTW_INFO("pci_write_config_byte (CacheLineSize) Result=%d\n", Result);
			goto _SET_CACHELINE_SIZE_FAIL;
		}

		Result = pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &ucPciCacheLineSize);
		if (Result != 0) {
			RTW_INFO("pci_read_config_byte (PciCacheLineSize) Result=%d\n", Result);
			goto _SET_CACHELINE_SIZE_FAIL;
		}

		if ((ucPciCacheLineSize != CacheLineSizeToSet)) {
			RTW_INFO("Failed to set Cache Line Size to 0x%x! ucPciCacheLineSize=%x\n", CacheLineSizeToSet, ucPciCacheLineSize);
			goto _SET_CACHELINE_SIZE_FAIL;
		}
	}

	return _SUCCESS;

_SET_CACHELINE_SIZE_FAIL:

	return _FAIL;
}


#define PCI_CMD_ENABLE_BUS_MASTER		BIT(2)
#define PCI_CMD_DISABLE_INTERRUPT		BIT(10)
#define CMD_BUS_MASTER				BIT(2)

static s32 rtw_pci_parse_configuration(struct pci_dev *pdev, struct dvobj_priv *pdvobjpriv)
{
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	/* PPCI_COMMON_CONFIG      	pPciConfig = (PPCI_COMMON_CONFIG) pucBuffer; */
	/* u16	usPciCommand = pPciConfig->Command; */
	u16	usPciCommand = 0;
	int	Result, ret;
	u8	CapabilityOffset;
	RT_PCI_CAPABILITIES_HEADER	CapabilityHdr;
	u8	PCIeCap;
	u8	LinkCtrlReg;
	u8	ClkReqReg;

	/* RTW_INFO("%s==>\n", __FUNCTION__); */

	pci_read_config_word(pdev, PCI_COMMAND, &usPciCommand);

	do {
		/* 3 Enable bus matering if it isn't enabled by the BIOS */
		if (!(usPciCommand & PCI_CMD_ENABLE_BUS_MASTER)) {
			RTW_INFO("Bus master is not enabled by BIOS! usPciCommand=%x\n", usPciCommand);

			usPciCommand |= CMD_BUS_MASTER;

			Result = pci_write_config_word(pdev, PCI_COMMAND, usPciCommand);
			if (Result != 0) {
				RTW_INFO("pci_write_config_word (Command) Result=%d\n", Result);
				ret = _FAIL;
				break;
			}

			Result = pci_read_config_word(pdev, PCI_COMMAND, &usPciCommand);
			if (Result != 0) {
				RTW_INFO("pci_read_config_word (Command) Result=%d\n", Result);
				ret = _FAIL;
				break;
			}

			if (!(usPciCommand & PCI_CMD_ENABLE_BUS_MASTER)) {
				RTW_INFO("Failed to enable bus master! usPciCommand=%x\n", usPciCommand);
				ret = _FAIL;
				break;
			}
		}
		RTW_INFO("Bus master is enabled. usPciCommand=%x\n", usPciCommand);

		/* 3 Enable interrupt */
		if ((usPciCommand & PCI_CMD_DISABLE_INTERRUPT)) {
			RTW_INFO("INTDIS==1 usPciCommand=%x\n", usPciCommand);

			usPciCommand &= (~PCI_CMD_DISABLE_INTERRUPT);

			Result = pci_write_config_word(pdev, PCI_COMMAND, usPciCommand);
			if (Result != 0) {
				RTW_INFO("pci_write_config_word (Command) Result=%d\n", Result);
				ret = _FAIL;
				break;
			}

			Result = pci_read_config_word(pdev, PCI_COMMAND, &usPciCommand);
			if (Result != 0) {
				RTW_INFO("pci_read_config_word (Command) Result=%d\n", Result);
				ret = _FAIL;
				break;
			}

			if ((usPciCommand & PCI_CMD_DISABLE_INTERRUPT)) {
				RTW_INFO("Failed to set INTDIS to 0! usPciCommand=%x\n", usPciCommand);
				ret = _FAIL;
				break;
			}
		}

		/*  */
		/* Description: Find PCI express capability offset. Porting from 818xB by tynli 2008.12.19 */
		/*  */
		/* ------------------------------------------------------------- */

		/* 3 PCIeCap */
		/* The device supports capability lists. Find the capabilities. */

		/* CapabilityOffset = pPciConfig->u.type0.CapabilitiesPtr; */
		pci_read_config_byte(pdev, PCI_CAPABILITY_LIST, &CapabilityOffset);

		/* Loop through the capabilities in search of the power management capability. */
		/* The list is NULL-terminated, so the last offset will always be zero. */

		while (CapabilityOffset != 0) {
			/* Read the header of the capability at  this offset. If the retrieved capability is not */
			/* the power management capability that we are looking for, follow the link to the */
			/* next capability and continue looping. */

			/* Result = pci_read_config_word(pdev, CapabilityOffset, (u16 *)&CapabilityHdr); */
			Result = pci_read_config_byte(pdev, CapabilityOffset, (u8 *)&CapabilityHdr.CapabilityID);
			if (Result != 0)
				break;

			Result = pci_read_config_byte(pdev, CapabilityOffset + 1, (u8 *)&CapabilityHdr.Next);
			if (Result != 0)
				break;

			/* Found the PCI express capability */
			if (CapabilityHdr.CapabilityID == PCI_CAPABILITY_ID_PCI_EXPRESS)
				break;
			else {
				/* This is some other capability. Keep looking for the PCI express capability. */
				CapabilityOffset = CapabilityHdr.Next;
			}
		}

		if (Result != 0) {
			RTW_INFO("pci_read_config_word (RT_PCI_CAPABILITIES_HEADER) Result=%d\n", Result);
			break;
		}

		if (CapabilityHdr.CapabilityID == PCI_CAPABILITY_ID_PCI_EXPRESS) {
			pcipriv->pciehdr_offset = CapabilityOffset;
			RTW_INFO("PCIe Header Offset =%x\n", CapabilityOffset);

			/* Skip past the capabilities header and read the PCI express capability */
			/* Justin: The PCI-e capability size should be 2 bytes, why we just get 1 byte */
			/* Beside, this PCIeCap seems no one reference it in the driver code */
			Result = pci_read_config_byte(pdev, CapabilityOffset + 2, &PCIeCap);

			if (Result != 0) {
				RTW_INFO("pci_read_config_byte (PCIE Capability) Result=%d\n", Result);
				break;
			}

			pcipriv->pcie_cap = PCIeCap;
			RTW_INFO("PCIe Capability =%x\n", PCIeCap);

			/* 3 Link Control Register */
			/* Read "Link Control Register" Field (80h ~81h) */
			Result = pci_read_config_byte(pdev, CapabilityOffset + 0x10, &LinkCtrlReg);
			if (Result != 0) {
				RTW_INFO("pci_read_config_byte (Link Control Register) Result=%d\n", Result);
				break;
			}

			pcipriv->linkctrl_reg = LinkCtrlReg;
			RTW_INFO("Link Control Register =%x\n", LinkCtrlReg);

			/* 3 Get Capability of PCI Clock Request */
			/* The clock request setting is located at 0x81[0] */
			Result = pci_read_config_byte(pdev, CapabilityOffset + 0x11, &ClkReqReg);
			if (Result != 0) {
				pcipriv->pci_clk_req = _FALSE;
				RTW_INFO("pci_read_config_byte (Clock Request Register) Result=%d\n", Result);
				break;
			}
			if (ClkReqReg & BIT(0))
				pcipriv->pci_clk_req = _TRUE;
			else
				pcipriv->pci_clk_req = _FALSE;
			RTW_INFO("Clock Request =%x\n", pcipriv->pci_clk_req);
		} else {
			/* We didn't find a PCIe capability. */
			RTW_INFO("Didn't Find PCIe Capability\n");
			break;
		}

		/* 3 Fill Cacheline */
		ret = rtw_set_pci_cache_line_size(pdev, 8);
		if (ret != _SUCCESS) {
			RTW_INFO("rtw_set_pci_cache_line_size fail\n");
			break;
		}

		/* Include 92C suggested by SD1. Added by tynli. 2009.11.25.
		 * Enable the Backdoor */
		{
			u8	tmp;

			Result = pci_read_config_byte(pdev, 0x98, &tmp);

			tmp |= BIT4;

			Result = pci_write_config_byte(pdev, 0x98, tmp);

		}
		ret = _SUCCESS;
	} while (_FALSE);

	return ret;
}

/*
 * Update PCI dependent default settings.
 *   */
static void rtw_pci_update_default_setting(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_priv	*pcipriv = &(pdvobjpriv->pcipriv);
	struct pwrctrl_priv	*pwrpriv = dvobj_to_pwrctl(pdvobjpriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	/* reset pPSC->reg_rfps_level & priv->b_support_aspm */
	pwrpriv->reg_rfps_level = 0;

	/* Update PCI ASPM setting */
	/* pwrpriv->const_amdpci_aspm = pdvobjpriv->const_amdpci_aspm; */
	switch (pdvobjpriv->const_pci_aspm) {
	case 0:		/* No ASPM */
		break;

	case 1:		/* ASPM dynamically enabled/disable. */
		pwrpriv->reg_rfps_level |= RT_RF_LPS_LEVEL_ASPM;
		break;

	case 2:		/* ASPM with Clock Req dynamically enabled/disable. */
		pwrpriv->reg_rfps_level |= (RT_RF_LPS_LEVEL_ASPM | RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 3:		/* Always enable ASPM and Clock Req from initialization to halt. */
		pwrpriv->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM);
		pwrpriv->reg_rfps_level |= (RT_RF_PS_LEVEL_ALWAYS_ASPM | RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 4:		/* Always enable ASPM without Clock Req from initialization to halt. */
		pwrpriv->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM | RT_RF_OFF_LEVL_CLK_REQ);
		pwrpriv->reg_rfps_level |= RT_RF_PS_LEVEL_ALWAYS_ASPM;
		break;

	case 5: /* Linux do not support ASPM OSC, added by Roger, 2013.03.27.	 */
		break;
	}

	pwrpriv->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;

	/* Update Radio OFF setting */
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

	/* Update Rx 2R setting */
	/* pPSC->reg_rfps_level |= ((pDevice->RegLPS2RDisable) ? RT_RF_LPS_DISALBE_2R : 0); */

	/*  */
	/* Set HW definition to determine if it supports ASPM. */
	/*  */
	switch (pdvobjpriv->const_support_pciaspm) {
	case 1: {	/* Support ASPM. */
		u8	b_support_backdoor = _TRUE;
		u8	b_support_l1_on_amd = _FALSE;

		rtw_hal_get_def_var(padapter, HAL_DEF_PCI_AMD_L1_SUPPORT, &b_support_l1_on_amd);

		if (pHalData->CustomerID == RT_CID_TOSHIBA &&
		    pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_AMD &&
		    !pcipriv->amd_l1_patch && !b_support_l1_on_amd) {
			RTW_INFO("%s(): Disable L1 Backdoor!!\n", __FUNCTION__);
			b_support_backdoor = _FALSE;
		}
		rtw_hal_set_def_var(padapter, HAL_DEF_PCI_SUUPORT_L1_BACKDOOR, &b_support_backdoor);
	}
	break;

	default:
		/* Do nothing. Set when finding the chipset. */
		break;
	}
}

static void rtw_pci_initialize_adapter_common(_adapter *padapter)
{
	struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter);

	rtw_pci_update_default_setting(padapter);

	if (pwrpriv->reg_rfps_level & RT_RF_PS_LEVEL_ALWAYS_ASPM) {
		/* Always enable ASPM & Clock Req. */
		rtw_pci_enable_aspm(padapter);
		RT_SET_PS_LEVEL(pwrpriv, RT_RF_PS_LEVEL_ALWAYS_ASPM);
	}

}

/*
 * 2009/10/28 MH Enable rtl8192ce DMA64 function. We need to enable 0x719 BIT5
 *   */
#ifdef CONFIG_64BIT_DMA
u8 PlatformEnableDMA64(PADAPTER Adapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	u8	bResult = _TRUE;
	u8	value;

	pci_read_config_byte(pdev, 0x719, &value);

	/* 0x719 Bit5 is DMA64 bit fetch. */
	value |= (BIT5);

	pci_write_config_byte(pdev, 0x719, value);

	return bResult;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18))
	#define rtw_pci_interrupt(x, y, z) rtw_pci_interrupt(x, y)
#endif

static irqreturn_t rtw_pci_interrupt(int irq, void *priv, struct pt_regs *regs)
{
	struct dvobj_priv *dvobj = (struct dvobj_priv *)priv;
	_adapter *adapter = dvobj->padapters[IFACE_ID0];

	if (dvobj->irq_enabled == 0)
		return IRQ_HANDLED;

	if (rtw_hal_interrupt_handler(adapter) == _FAIL)
		return IRQ_HANDLED;
	/* return IRQ_NONE; */

	return IRQ_HANDLED;
}

#ifdef RTK_DMP_PLATFORM
	#define pci_iounmap(x, y) iounmap(y)
#endif

int pci_alloc_irq(struct dvobj_priv *dvobj)
{
	int err;
	struct pci_dev *pdev = dvobj->ppcidev;
	int ret;

	ret = pci_enable_msi(pdev);

	RTW_INFO("pci_enable_msi ret=%d\n", ret);

#if defined(IRQF_SHARED)
	err = request_irq(pdev->irq, &rtw_pci_interrupt, IRQF_SHARED, DRV_NAME, dvobj);
#else
	err = request_irq(pdev->irq, &rtw_pci_interrupt, SA_SHIRQ, DRV_NAME, dvobj);
#endif
	if (err)
		RTW_INFO("Error allocating IRQ %d", pdev->irq);
	else {
		dvobj->irq_alloc = 1;
		dvobj->irq = pdev->irq;
		RTW_INFO("Request_irq OK, IRQ %d\n", pdev->irq);
	}

	return err ? _FAIL : _SUCCESS;
}

static void rtw_decide_chip_type_by_pci_driver_data(struct dvobj_priv *pdvobj, const struct pci_device_id *pdid)
{
	pdvobj->chip_type = pdid->driver_data;

#ifdef CONFIG_RTL8188E
	if (pdvobj->chip_type == RTL8188E) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8188EE;
		RTW_INFO("CHIP TYPE: RTL8188E\n");
	}
#endif

#ifdef CONFIG_RTL8812A
	if (pdvobj->chip_type == RTL8812) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8812E;
		RTW_INFO("CHIP TYPE: RTL8812AE\n");
	}
#endif

#ifdef CONFIG_RTL8821A
	if (pdvobj->chip_type == RTL8821) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8821E;
		RTW_INFO("CHIP TYPE: RTL8821AE\n");
	}
#endif

#ifdef CONFIG_RTL8723B
	if (pdvobj->chip_type == RTL8723B) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8723BE;
		RTW_INFO("CHIP TYPE: RTL8723BE\n");
	}
#endif
#ifdef CONFIG_RTL8723D
	if (pdvobj->chip_type == RTL8723D) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8723DE;
		RTW_INFO("CHIP TYPE: RTL8723DE\n");
	}
#endif
#ifdef CONFIG_RTL8192E
	if (pdvobj->chip_type == RTL8192E) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8192EE;
		RTW_INFO("CHIP TYPE: RTL8192EE\n");
	}
#endif

#ifdef CONFIG_RTL8814A
	if (pdvobj->chip_type == RTL8814A) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8814AE;
		RTW_INFO("CHIP TYPE: RTL8814AE\n");
	}
#endif

#if defined(CONFIG_RTL8822B)
	if (pdvobj->chip_type == RTL8822B) {
		pdvobj->HardwareType = HARDWARE_TYPE_RTL8822BE;
		RTW_INFO("CHIP TYPE: RTL8822BE\n");
	}
#endif
}

static struct dvobj_priv	*pci_dvobj_init(struct pci_dev *pdev, const struct pci_device_id *pdid)
{
	int err;
	u32	status = _FAIL;
	struct dvobj_priv	*dvobj = NULL;
	struct pci_priv	*pcipriv = NULL;
	struct pci_dev	*bridge_pdev = pdev->bus->self;
	/* u32	pci_cfg_space[16]; */
	unsigned long pmem_start, pmem_len, pmem_flags;
	u8	tmp;
	u8	PciBgVIdIdx;
	int	i;

	_func_enter_;

	dvobj = devobj_init();
	if (dvobj == NULL)
		goto exit;


	dvobj->ppcidev = pdev;
	pcipriv = &(dvobj->pcipriv);
	pci_set_drvdata(pdev, dvobj);


	err = pci_enable_device(pdev);
	if (err != 0) {
		RTW_ERR("%s : Cannot enable new PCI device\n", pci_name(pdev));
		goto free_dvobj;
	}

#ifdef CONFIG_64BIT_DMA
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		RTW_INFO("RTL819xCE: Using 64bit DMA\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err != 0) {
			RTW_ERR("Unable to obtain 64bit DMA for consistent allocations\n");
			goto disable_picdev;
		}
		dvobj->bdma64 = _TRUE;
	} else
#endif
	{
		if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
			err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
			if (err != 0) {
				RTW_ERR("Unable to obtain 32bit DMA for consistent allocations\n");
				goto disable_picdev;
			}
		}
	}

	pci_set_master(pdev);

	err = pci_request_regions(pdev, DRV_NAME);
	if (err != 0) {
		RTW_ERR("Can't obtain PCI resources\n");
		goto disable_picdev;
	}

#ifdef RTK_129X_PLATFORM
	if (pdev->bus->number == 0x00) {
		pmem_start = PCIE_SLOT1_MEM_START;
		pmem_len   = PCIE_SLOT1_MEM_LEN;
		pmem_flags = 0;
		RTW_PRINT("RTD129X: PCIE SLOT1\n");
	} else if (pdev->bus->number == 0x01) {
		pmem_start = PCIE_SLOT2_MEM_START;
		pmem_len   = PCIE_SLOT2_MEM_LEN;
		pmem_flags = 0;
		RTW_PRINT("RTD129X: PCIE SLOT2\n");
	} else {
		RTW_ERR(KERN_ERR "RTD129X: Wrong Slot Num\n");
		goto release_regions;
	}
#else
	/* Search for memory map resource (index 0~5) */
	for (i = 0 ; i < 6 ; i++) {
		pmem_start = pci_resource_start(pdev, i);
		pmem_len = pci_resource_len(pdev, i);
		pmem_flags = pci_resource_flags(pdev, i);

		if (pmem_flags & IORESOURCE_MEM)
			break;
	}

	if (i == 6) {
		RTW_ERR("%s: No MMIO resource found, abort!\n", __func__);
		goto release_regions;
	}
#endif /* RTK_DMP_PLATFORM */

#ifdef RTK_DMP_PLATFORM
	dvobj->pci_mem_start = (unsigned long)ioremap_nocache(pmem_start, pmem_len);
#elif defined(RTK_129X_PLATFORM)
	if (pdev->bus->number == 0x00)
		dvobj->ctrl_start =
			(unsigned long)ioremap(PCIE_SLOT1_CTRL_START, 0x200);
	else if (pdev->bus->number == 0x01)
		dvobj->ctrl_start =
			(unsigned long)ioremap(PCIE_SLOT2_CTRL_START, 0x200);

	if (dvobj->ctrl_start == 0) {
		RTW_ERR("RTD129X: Can't map CTRL mem\n");
		goto release_regions;
	}

	dvobj->mask_addr = dvobj->ctrl_start + PCIE_MASK_OFFSET;
	dvobj->tran_addr = dvobj->ctrl_start + PCIE_TRANSLATE_OFFSET;

	dvobj->pci_mem_start =
		(unsigned long)ioremap_nocache(pmem_start, pmem_len);
#else
	/* shared mem start */
	dvobj->pci_mem_start = (unsigned long)pci_iomap(pdev, i, pmem_len);
#endif
	if (dvobj->pci_mem_start == 0) {
		RTW_ERR("Can't map PCI mem\n");
		goto release_regions;
	}

	RTW_INFO("Memory mapped space start: 0x%08lx len:%08lx flags:%08lx, after map:0x%08lx\n",
		 pmem_start, pmem_len, pmem_flags, dvobj->pci_mem_start);

	/*find bus info*/
	pcipriv->busnumber = pdev->bus->number;
	pcipriv->devnumber = PCI_SLOT(pdev->devfn);
	pcipriv->funcnumber = PCI_FUNC(pdev->devfn);

	/*find bridge info*/
	if (bridge_pdev) {
		pcipriv->pcibridge_busnum = bridge_pdev->bus->number;
		pcipriv->pcibridge_devnum = PCI_SLOT(bridge_pdev->devfn);
		pcipriv->pcibridge_funcnum = PCI_FUNC(bridge_pdev->devfn);
		pcipriv->pcibridge_vendor = PCI_BRIDGE_VENDOR_UNKNOWN;
		pcipriv->pcibridge_vendorid = bridge_pdev->vendor;
		pcipriv->pcibridge_deviceid = bridge_pdev->device;
	}

#if 0
	/* Read PCI configuration Space Header */
	for (i = 0; i < 16; i++)
		pci_read_config_dword(pdev, (i << 2), &pci_cfg_space[i]);
#endif

	/*step 1-1., decide the chip_type via device info*/
	dvobj->interface_type = RTW_PCIE;
	rtw_decide_chip_type_by_pci_driver_data(dvobj, pdid);


	/* rtw_pci_parse_configuration(pdev, dvobj, (u8 *)&pci_cfg_space); */
	rtw_pci_parse_configuration(pdev, dvobj);

	for (PciBgVIdIdx = 0; PciBgVIdIdx < PCI_BRIDGE_VENDOR_MAX; PciBgVIdIdx++) {
		if (pcipriv->pcibridge_vendorid == pcibridge_vendors[PciBgVIdIdx]) {
			pcipriv->pcibridge_vendor = PciBgVIdIdx;
			RTW_INFO("Pci Bridge Vendor is found: VID=0x%x, VendorIdx=%d\n", pcipriv->pcibridge_vendorid, PciBgVIdIdx);
			break;
		}
	}

	if (pcipriv->pcibridge_vendor != PCI_BRIDGE_VENDOR_UNKNOWN) {
		rtw_pci_get_linkcontrol_reg(bridge_pdev, &pcipriv->pcibridge_linkctrlreg, &pcipriv->pcibridge_pciehdr_offset);

		if (pcipriv->pcibridge_vendor == PCI_BRIDGE_VENDOR_AMD)
			pcipriv->amd_l1_patch = rtw_pci_get_amd_l1_patch(dvobj, bridge_pdev);
	}

	status = _SUCCESS;

iounmap:
	if (status != _SUCCESS && dvobj->pci_mem_start != 0) {
#if 1/* def RTK_DMP_PLATFORM */
		pci_iounmap(pdev, (void *)dvobj->pci_mem_start);
#endif
		dvobj->pci_mem_start = 0;
	}

#ifdef RTK_129X_PLATFORM
	if (status != _SUCCESS && dvobj->ctrl_start != 0) {
		pci_iounmap(pdev, (void *)dvobj->ctrl_start);
		dvobj->ctrl_start = 0;
	}
#endif

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
			pci_disable_msi(pdev);
			dvobj->irq_alloc = 0;
		}

		if (dvobj->pci_mem_start != 0) {
#if 1/* def RTK_DMP_PLATFORM */
			pci_iounmap(pdev, (void *)dvobj->pci_mem_start);
#endif
			dvobj->pci_mem_start = 0;
		}

#ifdef RTK_129X_PLATFORM
		if (dvobj->ctrl_start != 0) {
			pci_iounmap(pdev, (void *)dvobj->ctrl_start);
			dvobj->ctrl_start = 0;
		}
#endif
		devobj_deinit(dvobj);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	_func_exit_;
}


u8 rtw_set_hal_ops(_adapter *padapter)
{
	/* alloc memory for HAL DATA */
	if (rtw_hal_data_init(padapter) == _FAIL)
		return _FAIL;

#ifdef CONFIG_RTL8188E
	if (rtw_get_chip_type(padapter) == RTL8188E)
		rtl8188ee_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
	if ((rtw_get_chip_type(padapter) == RTL8812) || (rtw_get_chip_type(padapter) == RTL8821))
		rtl8812ae_set_hal_ops(padapter);
#endif

#ifdef CONFIG_RTL8723B
	if (rtw_get_chip_type(padapter) == RTL8723B)
		rtl8723be_set_hal_ops(padapter);
#endif

#ifdef CONFIG_RTL8723D
	if (rtw_get_chip_type(padapter) == RTL8723D)
		rtl8723de_set_hal_ops(padapter);
#endif

#ifdef CONFIG_RTL8192E
	if (rtw_get_chip_type(padapter) == RTL8192E)
		rtl8192ee_set_hal_ops(padapter);
#endif

#ifdef CONFIG_RTL8814A
	if (rtw_get_chip_type(padapter) == RTL8814A)
		rtl8814ae_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8822B)
	if (rtw_get_chip_type(padapter) == RTL8822B)
		rtl8822be_set_hal_ops(padapter);
#endif

	if (rtw_hal_ops_check(padapter) == _FAIL)
		return _FAIL;

	if (hal_spec_init(padapter) == _FAIL)
		return _FAIL;

	return _SUCCESS;
}

void pci_set_intf_ops(_adapter *padapter, struct _io_ops *pops)
{
#ifdef CONFIG_RTL8188E
	if (rtw_get_chip_type(padapter) == RTL8188E)
		rtl8188ee_set_intf_ops(pops);
#endif

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
	if ((rtw_get_chip_type(padapter) == RTL8812) || (rtw_get_chip_type(padapter) == RTL8821))
		rtl8812ae_set_intf_ops(pops);
#endif

#ifdef CONFIG_RTL8723B
	if (rtw_get_chip_type(padapter) == RTL8723B)
		rtl8723be_set_intf_ops(pops);
#endif

#ifdef CONFIG_RTL8723D
	if (rtw_get_chip_type(padapter) == RTL8723D)
		rtl8723de_set_intf_ops(pops);
#endif

#ifdef CONFIG_RTL8192E
	if (rtw_get_chip_type(padapter) == RTL8192E)
		rtl8192ee_set_intf_ops(pops);
#endif

#ifdef CONFIG_RTL8814A
	if (rtw_get_chip_type(padapter) == RTL8814A)
		rtl8814ae_set_intf_ops(pops);
#endif

#if defined(CONFIG_RTL8822B)
	if (rtw_get_chip_type(padapter) == RTL8822B)
		rtl8822be_set_intf_ops(pops);
#endif

}

static void pci_intf_start(_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+pci_intf_start\n"));
	RTW_INFO("+pci_intf_start\n");

	/* Enable hw interrupt */
	rtw_hal_enable_interrupt(padapter);

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-pci_intf_start\n"));
	RTW_INFO("-pci_intf_start\n");
}
static void rtw_mi_pci_tasklets_kill(_adapter *padapter)
{
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
#ifndef CONFIG_NAPI
			tasklet_kill(&(padapter->recvpriv.recv_tasklet));
#endif
			tasklet_kill(&(padapter->recvpriv.irq_prepare_beacon_tasklet));
			tasklet_kill(&(padapter->xmitpriv.xmit_tasklet));
		}
	}
}

static void pci_intf_stop(_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+pci_intf_stop\n"));

	/* Disable hw interrupt */
	if (!rtw_is_surprise_removed(padapter)) {
		/* device still exists, so driver can do i/o operation */
		rtw_hal_disable_interrupt(padapter);
		rtw_mi_pci_tasklets_kill(padapter);

		rtw_hal_set_hwreg(padapter, HW_VAR_PCIE_STOP_TX_DMA, 0);

		rtw_hal_irp_reset(padapter);

		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("pci_intf_stop: SurpriseRemoved==_FALSE\n"));
	} else {
		/* Clear irq_enabled to prevent handle interrupt function. */
		adapter_to_dvobj(padapter)->irq_enabled = 0;
	}

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-pci_intf_stop\n"));

}

static void disable_ht_for_spec_devid(const struct pci_device_id *pdid)
{
#ifdef CONFIG_80211N_HT
	u16 vid, pid;
	u32 flags;
	int i;
	int num = sizeof(specific_device_id_tbl) / sizeof(struct specific_device_id);

	for (i = 0; i < num; i++) {
		vid = specific_device_id_tbl[i].idVendor;
		pid = specific_device_id_tbl[i].idProduct;
		flags = specific_device_id_tbl[i].flags;

		if ((pdid->vendor == vid) && (pdid->device == pid) && (flags & SPEC_DEV_ID_DISABLE_HT)) {
			rtw_ht_enable = 0;
			rtw_bw_mode = 0;
			rtw_ampdu_enable = 0;
		}

	}
#endif
}

#ifdef CONFIG_PM
static int rtw_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int ret = 0;
	struct dvobj_priv *dvobj = pci_get_drvdata(pdev);
	_adapter *padapter = dvobj->padapters[IFACE_ID0];

	ret = rtw_suspend_common(padapter);
	ret = pci_save_state(pdev);
	if (ret != 0) {
		RTW_INFO("%s Failed on pci_save_state (%d)\n", __FUNCTION__, ret);
		goto exit;
	}

#ifdef CONFIG_WOWLAN
	device_set_wakeup_enable(&pdev->dev, true);
#endif
	pci_disable_device(pdev);

#ifdef CONFIG_WOWLAN
	ret = pci_enable_wake(pdev, pci_choose_state(pdev, state), true);
	if (ret != 0)
		RTW_INFO("%s Failed on pci_enable_wake (%d)\n", __FUNCTION__, ret);
#endif
	ret = pci_set_power_state(pdev, pci_choose_state(pdev, state));
	if (ret != 0)
		RTW_INFO("%s Failed on pci_set_power_state (%d)\n", __FUNCTION__, ret);

exit:
	return ret;

}

int rtw_resume_process(_adapter *padapter)
{
	return rtw_resume_common(padapter);
}

static int rtw_pci_resume(struct pci_dev *pdev)
{
	struct dvobj_priv *dvobj = pci_get_drvdata(pdev);
	_adapter *padapter = dvobj->padapters[IFACE_ID0];
	struct net_device *pnetdev = padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	int	err = 0;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err != 0) {
		RTW_INFO("%s Failed on pci_set_power_state (%d)\n", __FUNCTION__, err);
		goto exit;
	}

	err = pci_enable_device(pdev);
	if (err != 0) {
		RTW_INFO("%s Failed on pci_enable_device (%d)\n", __FUNCTION__, err);
		goto exit;
	}


#ifdef CONFIG_WOWLAN
	err =  pci_enable_wake(pdev, PCI_D0, 0);
	if (err != 0) {
		RTW_INFO("%s Failed on pci_enable_wake (%d)\n", __FUNCTION__, err);
		goto exit;
	}
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 37))
	pci_restore_state(pdev);
#else
	err = pci_restore_state(pdev);
	if (err != 0) {
		RTW_INFO("%s Failed on pci_restore_state (%d)\n", __FUNCTION__, err);
		goto exit;
	}
#endif

#ifdef CONFIG_WOWLAN
	device_set_wakeup_enable(&pdev->dev, false);
#endif

	if (pwrpriv->bInternalAutoSuspend)
		err = rtw_resume_process(padapter);
	else {
		if (pwrpriv->wowlan_mode || pwrpriv->wowlan_ap_mode) {
			rtw_resume_lock_suspend();
			err = rtw_resume_process(padapter);
			rtw_resume_unlock_suspend();
		} else {
#ifdef CONFIG_RESUME_IN_WORKQUEUE
			rtw_resume_in_workqueue(pwrpriv);
#else
			if (rtw_is_earlysuspend_registered(pwrpriv)) {
				/* jeff: bypass resume here, do in late_resume */
				rtw_set_do_late_resume(pwrpriv, _TRUE);
			} else {
				rtw_resume_lock_suspend();
				err = rtw_resume_process(padapter);
				rtw_resume_unlock_suspend();
			}
#endif
		}
	}

exit:

	return err;
}
#endif/* CONFIG_PM */

_adapter *rtw_pci_primary_adapter_init(struct dvobj_priv *dvobj, struct pci_dev *pdev)
{
	_adapter *padapter = NULL;
	int status = _FAIL;

	padapter = (_adapter *)rtw_zvmalloc(sizeof(*padapter));
	if (padapter == NULL)
		goto exit;

	if (loadparam(padapter) != _SUCCESS)
		goto free_adapter;

	padapter->dvobj = dvobj;

	rtw_set_drv_stopped(padapter);/*init*/

	dvobj->padapters[dvobj->iface_nums++] = padapter;
	padapter->iface_id = IFACE_ID0;

	/* set adapter_type/iface type for primary padapter */
	padapter->isprimary = _TRUE;
	padapter->adapter_type = PRIMARY_ADAPTER;
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	padapter->hw_port = HW_PORT0;
#else
#ifndef CONFIG_HWPORT_SWAP
	padapter->hw_port = HW_PORT0;
#else /* CONFIG_HWPORT_SWAP */
	padapter->hw_port = HW_PORT1;
#endif /* !CONFIG_HWPORT_SWAP */
#endif

	if (rtw_init_io_priv(padapter, pci_set_intf_ops) == _FAIL)
		goto free_adapter;

	/* step 2.	hook HalFunc, allocate HalData */
	/* hal_set_hal_ops(padapter); */
	if (rtw_set_hal_ops(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("Initialize hal resource Failed!\n"));
		goto free_hal_data;
	}

	/* step 3. */
	padapter->intf_start = &pci_intf_start;
	padapter->intf_stop = &pci_intf_stop;

	/* .3 */
	rtw_hal_read_chip_version(padapter);

	/* .4 */
	rtw_hal_chip_configure(padapter);

	/* step 4. read efuse/eeprom data and get mac_addr */
	if (rtw_hal_read_chip_info(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("Initialize driver read chip info Failed!\n"));
		goto free_hal_data;
	}

	/* step 5. */
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("Initialize driver software resource Failed!\n"));
		goto free_hal_data;
	}

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_Initialize(padapter);
#endif /* CONFIG_BT_COEXIST */

	if (rtw_hal_inirp_init(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("Initialize PCI desc ring Failed!\n"));
		goto free_timer;
	}
	rtw_macaddr_cfg(adapter_mac_addr(padapter),  get_hal_mac_addr(padapter));

#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_mbid_camid_alloc(padapter, adapter_mac_addr(padapter));
#endif
#ifdef CONFIG_P2P
	rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));
#endif /* CONFIG_P2P */

	rtw_hal_disable_interrupt(padapter);

	/* step 6. Init pci related configuration */
	rtw_pci_initialize_adapter_common(padapter);

	RTW_INFO("bDriverStopped:%s, bSurpriseRemoved:%s, bup:%d, hw_init_completed:%s\n"
		 , rtw_is_drv_stopped(padapter) ? "True" : "False"
		 , rtw_is_surprise_removed(padapter) ? "True" : "False"
		 , padapter->bup
		 , rtw_is_hw_init_completed(padapter) ? "True" : "False"
		);

	status = _SUCCESS;

free_timer:
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	if (status != _SUCCESS)
		_cancel_timer_ex(&padapter->recvpriv.signal_stat_timer);
#endif

free_hal_data:
	if (status != _SUCCESS && padapter->HalData)
		rtw_hal_free_data(padapter);

free_adapter:
	if (status != _SUCCESS && padapter) {
		rtw_vmfree((u8 *)padapter, sizeof(*padapter));
		padapter = NULL;
	}
exit:
	return padapter;
}

static void rtw_pci_primary_adapter_deinit(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	/*	padapter->intf_stop(padapter); */

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(padapter, 0, _FALSE);

#ifdef CONFIG_AP_MODE
	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE) {
		free_mlme_ap_info(padapter);
#ifdef CONFIG_HOSTAPD_MLME
		hostapd_mode_unload(padapter);
#endif
	}
#endif

	/*rtw_cancel_all_timer(padapte);*/
#ifdef CONFIG_WOWLAN
	adapter_to_pwrctl(padapter)->wowlan_mode = _FALSE;
#endif /* CONFIG_WOWLAN */
	rtw_dev_unload(padapter);

	RTW_INFO("%s, hw_init_completed=%s\n", __func__, rtw_is_hw_init_completed(padapter) ? "_TRUE" : "_FALSE");

	rtw_hal_inirp_deinit(padapter);
	rtw_free_drv_sw(padapter);

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
	rtw_os_ndev_free(padapter);

#ifdef RTW_HALMAC
	rtw_halmac_deinit_adapter(adapter_to_dvobj(padapter));
#endif /* RTW_HALMAC */

	rtw_vmfree((u8 *)padapter, sizeof(_adapter));

#ifdef CONFIG_PLATFORM_RTD2880B
	RTW_INFO("wlan link down\n");
	rtd2885_wlan_netlink_sendMsg("linkdown", "8712");
#endif
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
*/
static int rtw_drv_init(struct pci_dev *pdev, const struct pci_device_id *pdid)
{
	int i, err = -ENODEV;

	int status = _FAIL;
	_adapter *padapter = NULL;
	struct dvobj_priv *dvobj;
	struct net_device *pnetdev;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_drv_init\n"));
	/* RTW_INFO("+rtw_drv_init\n"); */

	/* step 0. */
	disable_ht_for_spec_devid(pdid);

	/* Initialize dvobj_priv */
	dvobj = pci_dvobj_init(pdev, pdid);
	if (dvobj == NULL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("initialize device object priv Failed!\n"));
		goto exit;
	}

	/* Initialize primary adapter */
	padapter = rtw_pci_primary_adapter_init(dvobj, pdev);
	if (padapter == NULL) {
		RTW_INFO("rtw_pci_primary_adapter_init Failed!\n");
		goto free_dvobj;
	}

	/* Initialize virtual interface */
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->registrypriv.virtual_iface_num > (CONFIG_IFACE_NUMBER - 1))
		padapter->registrypriv.virtual_iface_num = (CONFIG_IFACE_NUMBER - 1);

	for (i = 0; i < padapter->registrypriv.virtual_iface_num; i++) {
		if (rtw_drv_add_vir_if(padapter, pci_set_intf_ops) == NULL) {
			RTW_INFO("rtw_drv_add_iface failed! (%d)\n", i);
			goto free_if_vir;
		}
	}
#endif

#ifdef CONFIG_GLOBAL_UI_PID
	if (ui_pid[1] != 0) {
		RTW_INFO("ui_pid[1]:%d\n", ui_pid[1]);
		rtw_signal_process(ui_pid[1], SIGUSR2);
	}
#endif

	/* dev_alloc_name && register_netdev */
	if (rtw_os_ndevs_init(dvobj) != _SUCCESS)
		goto free_if_vir;

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_init(padapter);
#endif

#ifdef CONFIG_PLATFORM_RTD2880B
	RTW_INFO("wlan link up\n");
	rtd2885_wlan_netlink_sendMsg("linkup", "8712");
#endif

	/* alloc irq */
	if (pci_alloc_irq(dvobj) != _SUCCESS)
		goto os_ndevs_deinit;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-871x_drv - drv_init, success!\n"));
	/* RTW_INFO("-871x_drv - drv_init, success!\n"); */

	status = _SUCCESS;

os_ndevs_deinit:
	if (status != _SUCCESS)
		rtw_os_ndevs_deinit(dvobj);
free_if_vir:
	if (status != _SUCCESS) {
#ifdef CONFIG_CONCURRENT_MODE
		rtw_drv_stop_vir_ifaces(dvobj);
		rtw_drv_free_vir_ifaces(dvobj);
#endif
	}

	if (status != _SUCCESS && padapter)
		rtw_pci_primary_adapter_deinit(padapter);

free_dvobj:
	if (status != _SUCCESS)
		pci_dvobj_deinit(pdev);
exit:
	return status == _SUCCESS ? 0 : -ENODEV;
}

/*
 * dev_remove() - our device is being removed
*/
/* rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove() => how to recognize both */
static void rtw_dev_remove(struct pci_dev *pdev)
{
	struct dvobj_priv *pdvobjpriv = pci_get_drvdata(pdev);
	_adapter *padapter = pdvobjpriv->padapters[IFACE_ID0];
	struct net_device *pnetdev = padapter->pnetdev;

	_func_exit_;

	if (pdvobjpriv->processing_dev_remove == _TRUE) {
		RTW_WARN("%s-line%d: Warning! device has been removed!\n", __FUNCTION__, __LINE__);
		return;
	}

	RTW_INFO("+rtw_dev_remove\n");

	pdvobjpriv->processing_dev_remove = _TRUE;

	if (unlikely(!padapter))
		return;

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
	rtw_os_ndevs_unregister(pdvobjpriv);
#ifdef CONFIG_NAPI
	netif_napi_del(&padapter->napi);
#endif

#if 0
#ifdef RTK_DMP_PLATFORM
	rtw_clr_surprise_removed(padapter);	/* always trate as device exists*/
	/* this will let the driver to disable it's interrupt */
#else
	if (pci_drvpriv.drv_registered == _TRUE) {
		/* RTW_INFO("r871xu_dev_remove():padapter->bSurpriseRemoved == _TRUE\n"); */
		rtw_set_surprise_removed(padapter);
	}
	/*else
	{

		GET_HAL_DATA(padapter)->hw_init_completed = _FALSE;

	}*/
#endif
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(dvobj_to_pwrctl(pdvobjpriv));
#endif

	if (padapter->bFWReady == _TRUE) {
		rtw_pm_set_ips(padapter, IPS_NONE);
		rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

		LeaveAllPowerSaveMode(padapter);
	}

	rtw_set_drv_stopped(padapter);	/*for stop thread*/
	rtw_stop_cmd_thread(padapter);
#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_stop_vir_ifaces(pdvobjpriv);
#endif

#ifdef CONFIG_BT_COEXIST
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	if (GET_HAL_DATA(padapter)->EEPROMBluetoothCoexist)
		rtw_btcoex_close_socket(padapter);
#endif
	rtw_btcoex_HaltNotify(padapter);
#endif

	rtw_pci_primary_adapter_deinit(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_free_vir_ifaces(pdvobjpriv);
#endif

	pci_dvobj_deinit(pdev);

	RTW_INFO("-r871xu_dev_remove, done\n");

	_func_exit_;
	return;
}


static int __init rtw_drv_entry(void)
{
	int ret = 0;

	RTW_PRINT("module init start\n");
	dump_drv_version(RTW_DBGDUMP);
#ifdef BTCOEXVERSION
	RTW_PRINT(DRV_NAME" BT-Coex version = %s\n", BTCOEXVERSION);
#endif /* BTCOEXVERSION */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24))
	/* console_suspend_enabled=0; */
#endif

	pci_drvpriv.drv_registered = _TRUE;
	rtw_suspend_lock_init();
	rtw_drv_proc_init();
	rtw_ndev_notifier_register();

	ret = pci_register_driver(&pci_drvpriv.rtw_pci_drv);

	if (ret != 0) {
		pci_drvpriv.drv_registered = _FALSE;
		rtw_suspend_lock_uninit();
		rtw_drv_proc_deinit();
		rtw_ndev_notifier_unregister();
		goto exit;
	}

exit:
	RTW_PRINT("module init ret=%d\n", ret);
	return ret;
}

static void __exit rtw_drv_halt(void)
{
	RTW_PRINT("module exit start\n");

	pci_drvpriv.drv_registered = _FALSE;

	pci_unregister_driver(&pci_drvpriv.rtw_pci_drv);

	rtw_suspend_lock_uninit();
	rtw_drv_proc_deinit();
	rtw_ndev_notifier_unregister();

	RTW_PRINT("module exit success\n");

	rtw_mstat_dump(RTW_DBGDUMP);
}


module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);
