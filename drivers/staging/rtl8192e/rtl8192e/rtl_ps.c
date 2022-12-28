// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
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
	if (priv->rf_change_in_progress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	rtl92e_set_rf_state(dev, rf_sleep, RF_CHANGE_BY_PS);
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
	if (priv->rf_change_in_progress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
		schedule_delayed_work(&priv->rtllib->hw_wakeup_wq,
				      msecs_to_jiffies(10));
		return;
	}
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	rtl92e_set_rf_state(dev, rf_on, RF_CHANGE_BY_PS);
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
	struct rt_pwr_save_ctrl *psc = (struct rt_pwr_save_ctrl *)
					&priv->rtllib->pwr_save_ctrl;

	psc->bSwRfProcessing = true;
	rtl92e_set_rf_state(dev, psc->eInactivePowerState, RF_CHANGE_BY_IPS);

	psc->bSwRfProcessing = false;
}

void rtl92e_ips_enter(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *psc = (struct rt_pwr_save_ctrl *)
					&priv->rtllib->pwr_save_ctrl;
	enum rt_rf_power_state rt_state;

	rt_state = priv->rtllib->rf_power_state;
	if (rt_state == rf_on && !psc->bSwRfProcessing &&
		(priv->rtllib->state != RTLLIB_LINKED) &&
		(priv->rtllib->iw_mode != IW_MODE_MASTER)) {
		psc->eInactivePowerState = rf_off;
		_rtl92e_ps_update_rf_state(dev);
	}
}

void rtl92e_ips_leave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *psc = (struct rt_pwr_save_ctrl *)
					&priv->rtllib->pwr_save_ctrl;
	enum rt_rf_power_state rt_state;

	rt_state = priv->rtllib->rf_power_state;
	if (rt_state != rf_on  && !psc->bSwRfProcessing &&
	    priv->rtllib->rf_off_reason <= RF_CHANGE_BY_IPS) {
		psc->eInactivePowerState = rf_on;
		_rtl92e_ps_update_rf_state(dev);
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
	enum rt_rf_power_state rt_state;

	rt_state = priv->rtllib->rf_power_state;
	if (rt_state == rf_off) {
		if (priv->rtllib->rf_off_reason > RF_CHANGE_BY_IPS) {
			netdev_warn(dev, "%s(): RF is OFF.\n",
				    __func__);
			return;
		}
		netdev_info(dev, "=========>%s(): rtl92e_ips_leave\n",
			    __func__);
		schedule_work(&priv->rtllib->ips_leave_wq);
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

	if (!priv->ps_force)
		priv->rtllib->ps = rtPsMode;
	if (priv->rtllib->sta_sleep != LPS_IS_WAKE &&
	    rtPsMode == RTLLIB_PS_DISABLED) {
		unsigned long flags;

		rtl92e_hw_wakeup(dev);
		priv->rtllib->sta_sleep = LPS_IS_WAKE;

		spin_lock_irqsave(&(priv->rtllib->mgmt_tx_lock), flags);
		rtllib_sta_ps_send_null_frame(priv->rtllib, 0);
		spin_unlock_irqrestore(&(priv->rtllib->mgmt_tx_lock), flags);
	}

	return true;
}

void rtl92e_leisure_ps_enter(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *psc = (struct rt_pwr_save_ctrl *)
					&priv->rtllib->pwr_save_ctrl;

	if (!((priv->rtllib->iw_mode == IW_MODE_INFRA) &&
	    (priv->rtllib->state == RTLLIB_LINKED))
	    || (priv->rtllib->iw_mode == IW_MODE_ADHOC) ||
	    (priv->rtllib->iw_mode == IW_MODE_MASTER))
		return;

	if (psc->bLeisurePs) {
		if (psc->LpsIdleCount >= RT_CHECK_FOR_HANG_PERIOD) {

			if (priv->rtllib->ps == RTLLIB_PS_DISABLED) {
				if (priv->rtllib->SetFwCmdHandler)
					priv->rtllib->SetFwCmdHandler(dev, FW_CMD_LPS_ENTER);
				_rtl92e_ps_set_mode(dev, RTLLIB_PS_MBCAST |
							 RTLLIB_PS_UNICAST);
			}
		} else
			psc->LpsIdleCount++;
	}
}

void rtl92e_leisure_ps_leave(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *psc = (struct rt_pwr_save_ctrl *)
					&priv->rtllib->pwr_save_ctrl;

	if (psc->bLeisurePs) {
		if (priv->rtllib->ps != RTLLIB_PS_DISABLED) {
			_rtl92e_ps_set_mode(dev, RTLLIB_PS_DISABLED);
			if (priv->rtllib->SetFwCmdHandler)
				priv->rtllib->SetFwCmdHandler(dev, FW_CMD_LPS_LEAVE);
		}
	}
}
