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
#include "rtl_ps.h"
#include "rtl_core.h"
#include "r8192E_phy.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h" /* RTL8225 Radio frontend */
#include "r8192E_cmdpkt.h"

void rtl8192_hw_sleep_down(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags = 0;
#ifdef CONFIG_ASPM_OR_D3
	PRT_POWER_SAVE_CONTROL	pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));
#endif
	spin_lock_irqsave(&priv->rf_ps_lock,flags);
	if (priv->RFChangeInProgress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock,flags);
		RT_TRACE(COMP_DBG, "rtl8192_hw_sleep_down(): RF Change in progress! \n");
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock,flags);
	RT_TRACE(COMP_DBG, "%s()============>come to sleep down\n", __func__);

#ifdef CONFIG_RTLWIFI_DEBUGFS
	if (priv->debug->hw_holding) {
		return;
	}
#endif
	MgntActSet_RF_State(dev, eRfSleep, RF_CHANGE_BY_PS,false);
#ifdef CONFIG_ASPM_OR_D3
	if (pPSC->RegRfPsLevel & RT_RF_LPS_LEVEL_ASPM)
	{
		RT_ENABLE_ASPM(dev);
		RT_SET_PS_LEVEL(pPSC, RT_RF_LPS_LEVEL_ASPM);
	}
#endif
}

void rtl8192_hw_sleep_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,struct rtllib_device,hw_sleep_wq);
	struct net_device *dev = ieee->dev;
        rtl8192_hw_sleep_down(dev);
}

void rtl8192_hw_wakeup(struct net_device* dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags = 0;
#ifdef CONFIG_ASPM_OR_D3
	PRT_POWER_SAVE_CONTROL	pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));
#endif
	spin_lock_irqsave(&priv->rf_ps_lock,flags);
	if (priv->RFChangeInProgress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock,flags);
		RT_TRACE(COMP_DBG, "rtl8192_hw_wakeup(): RF Change in progress! \n");
		queue_delayed_work_rsl(priv->rtllib->wq,&priv->rtllib->hw_wakeup_wq,MSECS(10));
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock,flags);
#ifdef CONFIG_ASPM_OR_D3
	if (pPSC->RegRfPsLevel & RT_RF_LPS_LEVEL_ASPM) {
		RT_DISABLE_ASPM(dev);
		RT_CLEAR_PS_LEVEL(pPSC, RT_RF_LPS_LEVEL_ASPM);
	}
#endif
	RT_TRACE(COMP_PS, "%s()============>come to wake up\n", __func__);
	MgntActSet_RF_State(dev, eRfOn, RF_CHANGE_BY_PS,false);
}

void rtl8192_hw_wakeup_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,struct rtllib_device,hw_wakeup_wq);
	struct net_device *dev = ieee->dev;
	rtl8192_hw_wakeup(dev);

}

#define MIN_SLEEP_TIME 50
#define MAX_SLEEP_TIME 10000
void rtl8192_hw_to_sleep(struct net_device *dev, u32 th, u32 tl)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	u32 rb = jiffies;
	unsigned long flags;

	spin_lock_irqsave(&priv->ps_lock,flags);

	tl -= MSECS(8+16+7);

	if (((tl>=rb)&& (tl-rb) <= MSECS(MIN_SLEEP_TIME))
			||((rb>tl)&& (rb-tl) < MSECS(MIN_SLEEP_TIME))) {
		spin_unlock_irqrestore(&priv->ps_lock,flags);
		printk("too short to sleep::%x, %x, %lx\n",tl, rb,  MSECS(MIN_SLEEP_TIME));
		return;
	}

	if (((tl > rb) && ((tl-rb) > MSECS(MAX_SLEEP_TIME)))||
			((tl < rb) && (tl>MSECS(69)) && ((rb-tl) > MSECS(MAX_SLEEP_TIME)))||
			((tl<rb)&&(tl<MSECS(69))&&((tl+0xffffffff-rb)>MSECS(MAX_SLEEP_TIME)))) {
		printk("========>too long to sleep:%x, %x, %lx\n", tl, rb,  MSECS(MAX_SLEEP_TIME));
		spin_unlock_irqrestore(&priv->ps_lock,flags);
		return;
	}
	{
		u32 tmp = (tl>rb)?(tl-rb):(rb-tl);
		queue_delayed_work_rsl(priv->rtllib->wq,
				&priv->rtllib->hw_wakeup_wq,tmp);
	}
	queue_delayed_work_rsl(priv->rtllib->wq,
			(void *)&priv->rtllib->hw_sleep_wq,0);
	spin_unlock_irqrestore(&priv->ps_lock,flags);
}

void InactivePsWorkItemCallback(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	PRT_POWER_SAVE_CONTROL	pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));

	RT_TRACE(COMP_PS, "InactivePsWorkItemCallback() ---------> \n");
	pPSC->bSwRfProcessing = true;

	RT_TRACE(COMP_PS, "InactivePsWorkItemCallback(): Set RF to %s.\n", \
			pPSC->eInactivePowerState == eRfOff?"OFF":"ON");
#ifdef CONFIG_ASPM_OR_D3
	if (pPSC->eInactivePowerState == eRfOn)
	{

		if ((pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_ASPM) && RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_ASPM))
		{
			RT_DISABLE_ASPM(dev);
			RT_CLEAR_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_ASPM);
		}
	}
#endif
	MgntActSet_RF_State(dev, pPSC->eInactivePowerState, RF_CHANGE_BY_IPS,false);

#ifdef CONFIG_ASPM_OR_D3
	if (pPSC->eInactivePowerState == eRfOff)
	{
		if (pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_ASPM)
		{
			RT_ENABLE_ASPM(dev);
			RT_SET_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_ASPM);
		}
	}
#endif

	pPSC->bSwRfProcessing = false;
	RT_TRACE(COMP_PS, "InactivePsWorkItemCallback() <--------- \n");
}

void
IPSEnter(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	PRT_POWER_SAVE_CONTROL		pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));
	RT_RF_POWER_STATE			rtState;

	if (pPSC->bInactivePs)
	{
		rtState = priv->rtllib->eRFPowerState;
		if (rtState == eRfOn && !pPSC->bSwRfProcessing &&\
			(priv->rtllib->state != RTLLIB_LINKED)&&\
			(priv->rtllib->iw_mode != IW_MODE_MASTER))
		{
			RT_TRACE(COMP_PS,"IPSEnter(): Turn off RF.\n");
			pPSC->eInactivePowerState = eRfOff;
			priv->isRFOff = true;
			priv->bInPowerSaveMode = true;
			InactivePsWorkItemCallback(dev);
		}
	}
}

void
IPSLeave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	PRT_POWER_SAVE_CONTROL	pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));
	RT_RF_POWER_STATE	rtState;

	if (pPSC->bInactivePs)
	{
		rtState = priv->rtllib->eRFPowerState;
		if (rtState != eRfOn  && !pPSC->bSwRfProcessing && priv->rtllib->RfOffReason <= RF_CHANGE_BY_IPS)
		{
			RT_TRACE(COMP_PS, "IPSLeave(): Turn on RF.\n");
			pPSC->eInactivePowerState = eRfOn;
			priv->bInPowerSaveMode = false;
			InactivePsWorkItemCallback(dev);
		}
	}
}
void IPSLeave_wq(void *data)
{
	struct rtllib_device *ieee = container_of_work_rsl(data,struct rtllib_device,ips_leave_wq);
	struct net_device *dev = ieee->dev;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	down(&priv->rtllib->ips_sem);
	IPSLeave(dev);
	up(&priv->rtllib->ips_sem);
}

void rtllib_ips_leave_wq(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	RT_RF_POWER_STATE	rtState;
	rtState = priv->rtllib->eRFPowerState;

	if (priv->rtllib->PowerSaveControl.bInactivePs){
		if (rtState == eRfOff){
			if (priv->rtllib->RfOffReason > RF_CHANGE_BY_IPS)
			{
				RT_TRACE(COMP_ERR, "%s(): RF is OFF.\n",__func__);
				return;
			}
			else{
				printk("=========>%s(): IPSLeave\n",__func__);
				queue_work_rsl(priv->rtllib->wq,&priv->rtllib->ips_leave_wq);
			}
		}
	}
}
void rtllib_ips_leave(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	down(&priv->rtllib->ips_sem);
	IPSLeave(dev);
	up(&priv->rtllib->ips_sem);
}

bool MgntActSet_802_11_PowerSaveMode(struct net_device *dev,	u8 rtPsMode)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->rtllib->iw_mode == IW_MODE_ADHOC)
		return false;

	RT_TRACE(COMP_LPS,"%s(): set ieee->ps = %x\n",__func__,rtPsMode);
	if (!priv->ps_force) {
		priv->rtllib->ps = rtPsMode;
	}
	if (priv->rtllib->sta_sleep != LPS_IS_WAKE && rtPsMode == RTLLIB_PS_DISABLED) {
                unsigned long flags;

		rtl8192_hw_wakeup(dev);
		priv->rtllib->sta_sleep = LPS_IS_WAKE;

                spin_lock_irqsave(&(priv->rtllib->mgmt_tx_lock), flags);
		RT_TRACE(COMP_DBG, "LPS leave: notify AP we are awaked"
			 " ++++++++++ SendNullFunctionData\n");
		rtllib_sta_ps_send_null_frame(priv->rtllib, 0);
                spin_unlock_irqrestore(&(priv->rtllib->mgmt_tx_lock), flags);
	}

	return true;
}


void LeisurePSEnter(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	PRT_POWER_SAVE_CONTROL pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));

	RT_TRACE(COMP_PS, "LeisurePSEnter()...\n");
	RT_TRACE(COMP_PS, "pPSC->bLeisurePs = %d, ieee->ps = %d,pPSC->LpsIdleCount is %d,RT_CHECK_FOR_HANG_PERIOD is %d\n",
		pPSC->bLeisurePs, priv->rtllib->ps,pPSC->LpsIdleCount,RT_CHECK_FOR_HANG_PERIOD);

	if (!((priv->rtllib->iw_mode == IW_MODE_INFRA) && (priv->rtllib->state == RTLLIB_LINKED))
	    || (priv->rtllib->iw_mode == IW_MODE_ADHOC) || (priv->rtllib->iw_mode == IW_MODE_MASTER))
		return;

	if (pPSC->bLeisurePs) {
		if (pPSC->LpsIdleCount >= RT_CHECK_FOR_HANG_PERIOD) {

			if (priv->rtllib->ps == RTLLIB_PS_DISABLED) {

				RT_TRACE(COMP_LPS, "LeisurePSEnter(): Enter 802.11 power save mode...\n");

				if (!pPSC->bFwCtrlLPS) {
					if (priv->rtllib->SetFwCmdHandler)
						priv->rtllib->SetFwCmdHandler(dev, FW_CMD_LPS_ENTER);
				}
				MgntActSet_802_11_PowerSaveMode(dev, RTLLIB_PS_MBCAST|RTLLIB_PS_UNICAST);
			}
		} else
			pPSC->LpsIdleCount++;
	}
}


void LeisurePSLeave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	PRT_POWER_SAVE_CONTROL pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));


	RT_TRACE(COMP_PS, "LeisurePSLeave()...\n");
	RT_TRACE(COMP_PS, "pPSC->bLeisurePs = %d, ieee->ps = %d\n",
		pPSC->bLeisurePs, priv->rtllib->ps);

	if (pPSC->bLeisurePs)
	{
		if (priv->rtllib->ps != RTLLIB_PS_DISABLED)
		{
#ifdef CONFIG_ASPM_OR_D3
			if (pPSC->RegRfPsLevel & RT_RF_LPS_LEVEL_ASPM && RT_IN_PS_LEVEL(pPSC, RT_RF_LPS_LEVEL_ASPM))
			{
				RT_DISABLE_ASPM(dev);
				RT_CLEAR_PS_LEVEL(pPSC, RT_RF_LPS_LEVEL_ASPM);
			}
#endif
			RT_TRACE(COMP_LPS, "LeisurePSLeave(): Busy Traffic , Leave 802.11 power save..\n");
			MgntActSet_802_11_PowerSaveMode(dev, RTLLIB_PS_DISABLED);

			if (!pPSC->bFwCtrlLPS)
			{
				if (priv->rtllib->SetFwCmdHandler)
				{
					priv->rtllib->SetFwCmdHandler(dev, FW_CMD_LPS_LEAVE);
				}
                    }
		}
	}
}

#ifdef CONFIG_ASPM_OR_D3

void
PlatformDisableHostL0s(struct net_device *dev)
{
	struct r8192_priv	*priv = (struct r8192_priv *)rtllib_priv(dev);
	u32				PciCfgAddrPort=0;
	u8				Num4Bytes;
	u8				uPciBridgeASPMSetting = 0;


	if ( (priv->NdisAdapter.BusNumber == 0xff && priv->NdisAdapter.DevNumber == 0xff && priv->NdisAdapter.FuncNumber == 0xff) ||
		(priv->NdisAdapter.PciBridgeBusNum == 0xff && priv->NdisAdapter.PciBridgeDevNum == 0xff && priv->NdisAdapter.PciBridgeFuncNum == 0xff) )
	{
		printk("PlatformDisableHostL0s(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return;
	}

	PciCfgAddrPort= (priv->NdisAdapter.PciBridgeBusNum << 16)|(priv->NdisAdapter.PciBridgeDevNum<< 11)|(priv->NdisAdapter.PciBridgeFuncNum <<  8)|(1 << 31);
	Num4Bytes = (priv->NdisAdapter.PciBridgePCIeHdrOffset+0x10)/4;


	NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort+(Num4Bytes << 2));

	NdisRawReadPortUchar(PCI_CONF_DATA, &uPciBridgeASPMSetting);

	if (uPciBridgeASPMSetting & BIT0)
		uPciBridgeASPMSetting &=  ~(BIT0);

	NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort+(Num4Bytes << 2));
	NdisRawWritePortUchar(PCI_CONF_DATA, uPciBridgeASPMSetting);

	udelay(50);

	printk("PlatformDisableHostL0s():PciBridge BusNumber[%x], DevNumbe[%x], FuncNumber[%x], Write reg[%x] = %x\n",
		priv->NdisAdapter.PciBridgeBusNum, priv->NdisAdapter.PciBridgeDevNum, priv->NdisAdapter.PciBridgeFuncNum,
		(priv->NdisAdapter.PciBridgePCIeHdrOffset+0x10), (priv->NdisAdapter.PciBridgeLinkCtrlReg | (priv->RegDevicePciASPMSetting&~BIT0)));
}

bool
PlatformEnable92CEBackDoor(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	bool			bResult = true;
	u8			value;

	if ( (priv->NdisAdapter.BusNumber == 0xff && priv->NdisAdapter.DevNumber == 0xff && priv->NdisAdapter.FuncNumber == 0xff) ||
		(priv->NdisAdapter.PciBridgeBusNum == 0xff && priv->NdisAdapter.PciBridgeDevNum == 0xff && priv->NdisAdapter.PciBridgeFuncNum == 0xff) )
	{
		RT_TRACE(COMP_INIT, "PlatformEnableASPM(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return false;
	}

	pci_read_config_byte(priv->pdev, 0x70f, &value);

	if (priv->NdisAdapter.PciBridgeVendor == PCI_BRIDGE_VENDOR_INTEL)
	{
	value |= BIT7;
	}
	else
	{
		value = 0x23;
	}

	pci_write_config_byte(priv->pdev, 0x70f, value);


	pci_read_config_byte(priv->pdev, 0x719, &value);
	value |= (BIT3|BIT4);
	pci_write_config_byte(priv->pdev, 0x719, value);


	return bResult;
}

bool PlatformSwitchDevicePciASPM(struct net_device *dev, u8 value)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	bool bResult = false;

	pci_write_config_byte(priv->pdev, 0x80, value);

	return bResult;
}

bool PlatformSwitchClkReq(struct net_device *dev, u8 value)
{
	bool bResult = false;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	u8	Buffer;

	Buffer= value;

#ifdef MERGE_TO_DO
	if (Adapter->bDriverIsGoingToPnpSetPowerSleep && pDevice->RegSupportLowPowerState
		&& value == 0x0)
		return false;
#endif

	pci_write_config_byte(priv->pdev,0x81,value);
	bResult = true;

	return bResult;
}

void
PlatformDisableASPM(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	PRT_POWER_SAVE_CONTROL	pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));
	u32	PciCfgAddrPort=0;
	u8	Num4Bytes;
	u8	LinkCtrlReg;
	u16	PciBridgeLinkCtrlReg, ASPMLevel=0;

	if (priv->NdisAdapter.PciBridgeVendor == PCI_BRIDGE_VENDOR_UNKNOWN)
	{
		RT_TRACE(COMP_POWER,  "%s(): Disable ASPM. Recognize the Bus of PCI(Bridge) as UNKNOWN.\n",__func__);
	}


	LinkCtrlReg = priv->NdisAdapter.LinkCtrlReg;
	PciBridgeLinkCtrlReg = priv->NdisAdapter.PciBridgeLinkCtrlReg;

	ASPMLevel |= BIT0|BIT1;
	LinkCtrlReg &=~ASPMLevel;
	PciBridgeLinkCtrlReg &=~(BIT0|BIT1);

	if ( (priv->NdisAdapter.BusNumber == 0xff && priv->NdisAdapter.DevNumber == 0xff && priv->NdisAdapter.FuncNumber == 0xff) ||
		(priv->NdisAdapter.PciBridgeBusNum == 0xff && priv->NdisAdapter.PciBridgeDevNum == 0xff && priv->NdisAdapter.PciBridgeFuncNum == 0xff) )
	{
	} else  {
		PciCfgAddrPort= (priv->NdisAdapter.PciBridgeBusNum << 16)|(priv->NdisAdapter.PciBridgeDevNum<< 11)|(priv->NdisAdapter.PciBridgeFuncNum <<  8)|(1 << 31);
		Num4Bytes = (priv->NdisAdapter.PciBridgePCIeHdrOffset+0x10)/4;

		NdisRawWritePortUlong(PCI_CONF_ADDRESS , PciCfgAddrPort+(Num4Bytes << 2));

		NdisRawWritePortUchar(PCI_CONF_DATA, PciBridgeLinkCtrlReg);
		RT_TRACE(COMP_POWER, "PlatformDisableASPM():PciBridge BusNumber[%x], DevNumbe[%x], FuncNumber[%x], Write reg[%x] = %x\n",
			priv->NdisAdapter.PciBridgeBusNum, priv->NdisAdapter.PciBridgeDevNum, priv->NdisAdapter.PciBridgeFuncNum,
			(priv->NdisAdapter.PciBridgePCIeHdrOffset+0x10), PciBridgeLinkCtrlReg);

		udelay(50);
	}
}

void PlatformEnableASPM(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	PRT_POWER_SAVE_CONTROL pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->rtllib->PowerSaveControl));
	u16	ASPMLevel = 0;
	u32	PciCfgAddrPort=0;
	u8	Num4Bytes;
	u8	uPciBridgeASPMSetting = 0;
	u8	uDeviceASPMSetting = 0;


	if ( (priv->NdisAdapter.BusNumber == 0xff && priv->NdisAdapter.DevNumber == 0xff && priv->NdisAdapter.FuncNumber == 0xff) ||
		(priv->NdisAdapter.PciBridgeBusNum == 0xff && priv->NdisAdapter.PciBridgeDevNum == 0xff && priv->NdisAdapter.PciBridgeFuncNum == 0xff) )
	{
		RT_TRACE(COMP_INIT, "PlatformEnableASPM(): Fail to enable ASPM. Cannot find the Bus of PCI(Bridge).\n");
		return;
	}

	ASPMLevel |= priv->RegDevicePciASPMSetting;
	uDeviceASPMSetting = priv->NdisAdapter.LinkCtrlReg;

	uDeviceASPMSetting |= ASPMLevel;

	PlatformSwitchDevicePciASPM(dev, uDeviceASPMSetting);

	if (pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_CLK_REQ) {
		PlatformSwitchClkReq(dev,(pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_CLK_REQ) ? 1 : 0);
		RT_SET_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_CLK_REQ);
	}
	udelay(100);

	udelay(100);
}

u32 PlatformResetPciSpace(struct net_device *dev,u8 Value)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	pci_write_config_byte(priv->pdev,0x04,Value);

	return 1;

}
bool PlatformSetPMCSR(struct net_device *dev,u8 value,bool bTempSetting)
{
	bool bResult = false;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	u8  Buffer;
	bool bActuallySet=false, bSetFunc=false;
	unsigned long flag;

	Buffer= value;
	spin_lock_irqsave(&priv->D3_lock,flag);
	if (bActuallySet) {
		if (Buffer) {
			PlatformSwitchClkReq(dev, 0x01);
		} else {
			PlatformSwitchClkReq(dev, 0x00);
		}

		pci_write_config_byte(priv->pdev,0x44,Buffer);
		RT_TRACE(COMP_POWER, "PlatformSetPMCSR(): D3(value: %d)\n", Buffer);

		bResult = true;
		if (!Buffer) {
			PlatformResetPciSpace(dev, 0x06);
			PlatformResetPciSpace(dev, 0x07);
		}

		if (bSetFunc) {
			if (Buffer) {
#ifdef TO_DO_LIST
				RT_DISABLE_FUNC(Adapter, DF_IO_D3_BIT);
#endif
			} else {
#ifdef TO_DO_LIST
				RT_ENABLE_FUNC(Adapter, DF_IO_D3_BIT);
#endif
			}
		}

	}
	spin_unlock_irqrestore(&priv->D3_lock,flag);
	return bResult;
}
#endif
