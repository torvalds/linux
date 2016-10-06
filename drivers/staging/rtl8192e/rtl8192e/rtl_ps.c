/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
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
#include <linux/jiffies.h>

static void _rtl92e_hw_sleep(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->rf_ps_lock, flags);
	if (priv->RFChangeInProgress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
		RT_TRACE(COMP_DBG,
			 "_rtl92e_hw_sleep(): RF Change in progress!\n");
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	RT_TRACE(COMP_DBG, "%s()============>come to sleep down\n", __func__);

	rtl92e_set_rf_state(dev, eRfSleep, RF_CHANGE_BY_PS);
}

void rtl92e_hw_sleep_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, hw_sleep_wq);
	struct net_device *dev = ieee->dev;

	_rtl92e_hw_sleep(dev);
}

void rtl92e_hw_wakeup(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->rf_ps_lock, flags);
	if (priv->RFChangeInProgress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
		RT_TRACE(COMP_DBG,
			 "rtl92e_hw_wakeup(): RF Change in progress!\n");
		schedule_delayed_work(&priv->rtllib->hw_wakeup_wq,
				      msecs_to_jiffies(10));
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	RT_TRACE(COMP_PS, "%s()============>come to wake up\n", __func__);
	rtl92e_set_rf_state(dev, eRfOn, RF_CHANGE_BY_PS);
}

void rtl92e_hw_wakeup_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, hw_wakeup_wq);
	struct net_device *dev = ieee->dev;

	rtl92e_hw_wakeup(dev);
}

#define MIN_SLEEP_TIME 50
#define MAX_SLEEP_TIME 10000
void rtl92e_enter_sleep(struct net_device *dev, u64 time)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	u32 tmp;
	unsigned long flags;
	unsigned long timeout;

	spin_lock_irqsave(&priv->ps_lock, flags);

	time -= msecs_to_jiffies(8 + 16 + 7);

	timeout = jiffies + msecs_to_jiffies(MIN_SLEEP_TIME);
	if (time_before((unsigned long)time, timeout)) {
		spin_unlock_irqrestore(&priv->ps_lock, flags);
		netdev_info(dev, "too short to sleep::%lld < %ld\n",
			    time - jiffies, msecs_to_jiffies(MIN_SLEEP_TIME));
		return;
	}
	timeout = jiffies + msecs_to_jiffies(MAX_SLEEP_TIME);
	if (time_after((unsigned long)time, timeout)) {
		netdev_info(dev, "========>too long to sleep:%lld > %ld\n",
			    time - jiffies, msecs_to_jiffies(MAX_SLEEP_TIME));
		spin_unlock_irqrestore(&priv->ps_lock, flags);
		return;
	}
	tmp = time - jiffies;
	schedule_delayed_work(&priv->rtllib->hw_wakeup_wq, tmp);
	schedule_delayed_work(&priv->rtllib->hw_sleep_wq, 0);
	spin_unlock_irqrestore(&priv->ps_lock, flags);
}

static void _rtl92e_ps_update_rf_state(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);

	RT_TRACE(COMP_PS, "_rtl92e_ps_update_rf_state() --------->\n");
	pPSC->bSwRfProcessing = true;

	RT_TRACE(COMP_PS, "_rtl92e_ps_update_rf_state(): Set RF to %s.\n",
		 pPSC->eInactivePowerState == eRfOff ? "OFF" : "ON");
	rtl92e_set_rf_state(dev, pPSC->eInactivePowerState, RF_CHANGE_BY_IPS);

	pPSC->bSwRfProcessing = false;
	RT_TRACE(COMP_PS, "_rtl92e_ps_update_rf_state() <---------\n");
}

void rtl92e_ips_enter(struct net_device *dev)
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
			RT_TRACE(COMP_PS, "rtl92e_ips_enter(): Turn off RF.\n");
			pPSC->eInactivePowerState = eRfOff;
			priv->isRFOff = true;
			priv->bInPowerSaveMode = true;
			_rtl92e_ps_update_rf_state(dev);
		}
	}
}

void rtl92e_ips_leave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);
	enum rt_rf_power_state rtState;

	if (pPSC->bInactivePs) {
		rtState = priv->rtllib->eRFPowerState;
		if (rtState != eRfOn  && !pPSC->bSwRfProcessing &&
		    priv->rtllib->RfOffReason <= RF_CHANGE_BY_IPS) {
			RT_TRACE(COMP_PS, "rtl92e_ips_leave(): Turn on RF.\n");
			pPSC->eInactivePowerState = eRfOn;
			priv->bInPowerSaveMode = false;
			_rtl92e_ps_update_rf_state(dev);
		}
	}
}

void rtl92e_ips_leave_wq(void *data)
{
	struct rtllib_device *ieee = container_of_work_rsl(data,
				     struct rtllib_device, ips_leave_wq);
	struct net_device *dev = ieee->dev;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	mutex_lock(&priv->rtllib->ips_mutex);
	rtl92e_ips_leave(dev);
	mutex_unlock(&priv->rtllib->ips_mutex);
}

void rtl92e_rtllib_ips_leave_wq(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	enum rt_rf_power_state rtState;

	rtState = priv->rtllib->eRFPowerState;

	if (priv->rtllib->PowerSaveControl.bInactivePs) {
		if (rtState == eRfOff) {
			if (priv->rtllib->RfOffReason > RF_CHANGE_BY_IPS) {
				netdev_warn(dev, "%s(): RF is OFF.\n",
					    __func__);
				return;
			}
			netdev_info(dev, "=========>%s(): rtl92e_ips_leave\n",
				    __func__);
			schedule_work(&priv->rtllib->ips_leave_wq);
		}
	}
}

void rtl92e_rtllib_ips_leave(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	mutex_lock(&priv->rtllib->ips_mutex);
	rtl92e_ips_leave(dev);
	mutex_unlock(&priv->rtllib->ips_mutex);
}

static bool _rtl92e_ps_set_mode(struct net_device *dev, u8 rtPsMode)
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

		rtl92e_hw_wakeup(dev);
		priv->rtllib->sta_sleep = LPS_IS_WAKE;

		spin_lock_irqsave(&(priv->rtllib->mgmt_tx_lock), flags);
		RT_TRACE(COMP_DBG,
			 "LPS leave: notify AP we are awaked ++++++++++ SendNullFunctionData\n");
		rtllib_sta_ps_send_null_frame(priv->rtllib, 0);
		spin_unlock_irqrestore(&(priv->rtllib->mgmt_tx_lock), flags);
	}

	return true;
}

void rtl92e_leisure_ps_enter(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);

	RT_TRACE(COMP_PS, "rtl92e_leisure_ps_enter()...\n");
	RT_TRACE(COMP_PS,
		 "pPSC->bLeisurePs = %d, ieee->ps = %d,pPSC->LpsIdleCount is %d,RT_CHECK_FOR_HANG_PERIOD is %d\n",
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

				RT_TRACE(COMP_LPS,
					 "rtl92e_leisure_ps_enter(): Enter 802.11 power save mode...\n");

				if (!pPSC->bFwCtrlLPS) {
					if (priv->rtllib->SetFwCmdHandler)
						priv->rtllib->SetFwCmdHandler(
							dev, FW_CMD_LPS_ENTER);
				}
				_rtl92e_ps_set_mode(dev, RTLLIB_PS_MBCAST |
							 RTLLIB_PS_UNICAST);
			}
		} else
			pPSC->LpsIdleCount++;
	}
}

void rtl92e_leisure_ps_leave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&(priv->rtllib->PowerSaveControl);


	RT_TRACE(COMP_PS, "rtl92e_leisure_ps_leave()...\n");
	RT_TRACE(COMP_PS, "pPSC->bLeisurePs = %d, ieee->ps = %d\n",
		pPSC->bLeisurePs, priv->rtllib->ps);

	if (pPSC->bLeisurePs) {
		if (priv->rtllib->ps != RTLLIB_PS_DISABLED) {
			RT_TRACE(COMP_LPS,
				 "rtl92e_leisure_ps_leave(): Busy Traffic , Leave 802.11 power save..\n");
			_rtl92e_ps_set_mode(dev, RTLLIB_PS_DISABLED);

			if (!pPSC->bFwCtrlLPS) {
				if (priv->rtllib->SetFwCmdHandler)
					priv->rtllib->SetFwCmdHandler(dev,
							 FW_CMD_LPS_LEAVE);
		    }
		}
	}
}
