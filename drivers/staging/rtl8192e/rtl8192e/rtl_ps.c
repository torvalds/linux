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

static void rtl8192_hw_sleep_down(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags = 0;
	spin_lock_irqsave(&priv->rf_ps_lock, flags);
	if (priv->RFChangeInProgress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
		RT_TRACE(COMP_DBG, "rtl8192_hw_sleep_down(): RF Change in "
			 "progress!\n");
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	RT_TRACE(COMP_DBG, "%s()============>come to sleep down\n", __func__);

	MgntActSet_RF_State(dev, eRfSleep, RF_CHANGE_BY_PS, false);
}

void rtl8192_hw_sleep_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, hw_sleep_wq);
	struct net_device *dev = ieee->dev;
	rtl8192_hw_sleep_down(dev);
}

void rtl8192_hw_wakeup(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags = 0;
	spin_lock_irqsave(&priv->rf_ps_lock, flags);
	if (priv->RFChangeInProgress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
		RT_TRACE(COMP_DBG, "rtl8192_hw_wakeup(): RF Change in "
			 "progress!\n");
		queue_delayed_work_rsl(priv->rtllib->wq,
				       &priv->rtllib->hw_wakeup_wq, MSECS(10));
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	RT_TRACE(COMP_PS, "%s()============>come to wake up\n", __func__);
	MgntActSet_RF_State(dev, eRfOn, RF_CHANGE_BY_PS, false);
}

void rtl8192_hw_wakeup_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, hw_wakeup_wq);
	struct net_device *dev = ieee->dev;
	rtl8192_hw_wakeup(dev);

}

#define MIN_SLEEP_TIME 50
#define MAX_SLEEP_TIME 10000
void rtl8192_hw_to_sleep(struct net_device *dev, u64 time)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	u32 tmp;
	unsigned long flags;

	spin_lock_irqsave(&priv->ps_lock, flags);

	time -= MSECS(8+16+7);

	if ((time - jiffies) <= MSECS(MIN_SLEEP_TIME)) {
		spin_unlock_irqrestore(&priv->ps_lock, flags);
		printk(KERN_INFO "too short to sleep::%lld < %ld\n",
		       time - jiffies, MSECS(MIN_SLEEP_TIME));
		return;
	}

	if ((time - jiffies) > MSECS(MAX_SLEEP_TIME)) {
		printk(KERN_INFO "========>too long to sleep:%lld > %ld\n",
		       time - jiffies,  MSECS(MAX_SLEEP_TIME));
		spin_unlock_irqrestore(&priv->ps_lock, flags);
		return;
	}
	tmp = time - jiffies;
	queue_delayed_work_rsl(priv->rtllib->wq,
			&priv->rtllib->hw_wakeup_wq, tmp);
	queue_delayed_work_rsl(priv->rtllib->wq,
			(void *)&priv->rtllib->hw_sleep_wq, 0);
	spin_unlock_irqrestore(&priv->ps_lock, flags);
}

static void InactivePsWorkItemCallback(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);

	RT_TRACE(COMP_PS, "InactivePsWorkItemCallback() --------->\n");
	pPSC->bSwRfProcessing = true;

	RT_TRACE(COMP_PS, "InactivePsWorkItemCallback(): Set RF to %s.\n",
		 pPSC->eInactivePowerState == eRfOff ? "OFF" : "ON");
	MgntActSet_RF_State(dev, pPSC->eInactivePowerState, RF_CHANGE_BY_IPS,
			    false);

	pPSC->bSwRfProcessing = false;
	RT_TRACE(COMP_PS, "InactivePsWorkItemCallback() <---------\n");
}

void IPSEnter(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);
	enum rt_rf_power_state rtState;

	if (pPSC->bInactivePs) {
		rtState = priv->rtllib->eRFPowerState;
		if (rtState == eRfOn && !pPSC->bSwRfProcessing &&
			(priv->rtllib->state != RTLLIB_LINKED) &&
			(priv->rtllib->iw_mode != IW_MODE_MASTER)) {
			RT_TRACE(COMP_PS, "IPSEnter(): Turn off RF.\n");
			pPSC->eInactivePowerState = eRfOff;
			priv->isRFOff = true;
			priv->bInPowerSaveMode = true;
			InactivePsWorkItemCallback(dev);
		}
	}
}

void IPSLeave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);
	enum rt_rf_power_state rtState;

	if (pPSC->bInactivePs) {
		rtState = priv->rtllib->eRFPowerState;
		if (rtState != eRfOn  && !pPSC->bSwRfProcessing &&
		    priv->rtllib->RfOffReason <= RF_CHANGE_BY_IPS) {
			RT_TRACE(COMP_PS, "IPSLeave(): Turn on RF.\n");
			pPSC->eInactivePowerState = eRfOn;
			priv->bInPowerSaveMode = false;
			InactivePsWorkItemCallback(dev);
		}
	}
}

void IPSLeave_wq(void *data)
{
	struct rtllib_device *ieee = container_of_work_rsl(data,
				     struct rtllib_device, ips_leave_wq);
	struct net_device *dev = ieee->dev;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	down(&priv->rtllib->ips_sem);
	IPSLeave(dev);
	up(&priv->rtllib->ips_sem);
}

void rtllib_ips_leave_wq(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	enum rt_rf_power_state rtState;
	rtState = priv->rtllib->eRFPowerState;

	if (priv->rtllib->PowerSaveControl.bInactivePs) {
		if (rtState == eRfOff) {
			if (priv->rtllib->RfOffReason > RF_CHANGE_BY_IPS) {
				RT_TRACE(COMP_ERR, "%s(): RF is OFF.\n",
					 __func__);
				return;
			} else {
				printk(KERN_INFO "=========>%s(): IPSLeave\n",
				       __func__);
				queue_work_rsl(priv->rtllib->wq,
					       &priv->rtllib->ips_leave_wq);
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

static bool MgntActSet_802_11_PowerSaveMode(struct net_device *dev,
					    u8 rtPsMode)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->rtllib->iw_mode == IW_MODE_ADHOC)
		return false;

	RT_TRACE(COMP_LPS, "%s(): set ieee->ps = %x\n", __func__, rtPsMode);
	if (!priv->ps_force)
		priv->rtllib->ps = rtPsMode;
	if (priv->rtllib->sta_sleep != LPS_IS_WAKE &&
	    rtPsMode == RTLLIB_PS_DISABLED) {
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
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);

	RT_TRACE(COMP_PS, "LeisurePSEnter()...\n");
	RT_TRACE(COMP_PS, "pPSC->bLeisurePs = %d, ieee->ps = %d,pPSC->LpsIdle"
		 "Count is %d,RT_CHECK_FOR_HANG_PERIOD is %d\n",
		 pPSC->bLeisurePs, priv->rtllib->ps, pPSC->LpsIdleCount,
		 RT_CHECK_FOR_HANG_PERIOD);

	if (!((priv->rtllib->iw_mode == IW_MODE_INFRA) &&
	    (priv->rtllib->state == RTLLIB_LINKED))
	    || (priv->rtllib->iw_mode == IW_MODE_ADHOC) ||
	    (priv->rtllib->iw_mode == IW_MODE_MASTER))
		return;

	if (pPSC->bLeisurePs) {
		if (pPSC->LpsIdleCount >= RT_CHECK_FOR_HANG_PERIOD) {

			if (priv->rtllib->ps == RTLLIB_PS_DISABLED) {

				RT_TRACE(COMP_LPS, "LeisurePSEnter(): Enter "
					 "802.11 power save mode...\n");

				if (!pPSC->bFwCtrlLPS) {
					if (priv->rtllib->SetFwCmdHandler)
						priv->rtllib->SetFwCmdHandler(
							dev, FW_CMD_LPS_ENTER);
				}
				MgntActSet_802_11_PowerSaveMode(dev,
							 RTLLIB_PS_MBCAST |
							 RTLLIB_PS_UNICAST);
			}
		} else
			pPSC->LpsIdleCount++;
	}
}

void LeisurePSLeave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);


	RT_TRACE(COMP_PS, "LeisurePSLeave()...\n");
	RT_TRACE(COMP_PS, "pPSC->bLeisurePs = %d, ieee->ps = %d\n",
		pPSC->bLeisurePs, priv->rtllib->ps);

	if (pPSC->bLeisurePs) {
		if (priv->rtllib->ps != RTLLIB_PS_DISABLED) {
			RT_TRACE(COMP_LPS, "LeisurePSLeave(): Busy Traffic , "
				 "Leave 802.11 power save..\n");
			MgntActSet_802_11_PowerSaveMode(dev,
					 RTLLIB_PS_DISABLED);

			if (!pPSC->bFwCtrlLPS) {
				if (priv->rtllib->SetFwCmdHandler)
					priv->rtllib->SetFwCmdHandler(dev,
							 FW_CMD_LPS_LEAVE);
		    }
		}
	}
}
