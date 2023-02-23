// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include <linux/uaccess.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/ieee80211.h>
#include "rtl_core.h"
#include "r8192E_phy.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h"
#include "r8192E_cmdpkt.h"

#include "rtl_wx.h"
#include "rtl_dm.h"

#include "rtl_pm.h"

int hwwep = 1;
static char *ifname = "wlan%d";

static const struct rtl819x_ops rtl819xp_ops = {
	.nic_type			= NIC_8192E,
	.get_eeprom_size		= rtl92e_get_eeprom_size,
	.init_adapter_variable		= rtl92e_init_variables,
	.initialize_adapter		= rtl92e_start_adapter,
	.link_change			= rtl92e_link_change,
	.tx_fill_descriptor		= rtl92e_fill_tx_desc,
	.tx_fill_cmd_descriptor		= rtl92e_fill_tx_cmd_desc,
	.rx_query_status_descriptor	= rtl92e_get_rx_stats,
	.rx_command_packet_handler = NULL,
	.stop_adapter			= rtl92e_stop_adapter,
	.update_ratr_table		= rtl92e_update_ratr_table,
	.irq_enable			= rtl92e_enable_irq,
	.irq_disable			= rtl92e_disable_irq,
	.irq_clear			= rtl92e_clear_irq,
	.rx_enable			= rtl92e_enable_rx,
	.tx_enable			= rtl92e_enable_tx,
	.interrupt_recognized		= rtl92e_ack_irq,
	.tx_check_stuck_handler	= rtl92e_is_tx_stuck,
	.rx_check_stuck_handler	= rtl92e_is_rx_stuck,
};

static struct pci_device_id rtl8192_pci_id_tbl[] = {
	{PCI_DEVICE(0x10ec, 0x8192)},
	{PCI_DEVICE(0x07aa, 0x0044)},
	{PCI_DEVICE(0x07aa, 0x0047)},
	{}
};

MODULE_DEVICE_TABLE(pci, rtl8192_pci_id_tbl);

static int _rtl92e_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id);
static void _rtl92e_pci_disconnect(struct pci_dev *pdev);
static irqreturn_t _rtl92e_irq(int irq, void *netdev);

static SIMPLE_DEV_PM_OPS(rtl92e_pm_ops, rtl92e_suspend, rtl92e_resume);

static struct pci_driver rtl8192_pci_driver = {
	.name = DRV_NAME,	/* Driver name   */
	.id_table = rtl8192_pci_id_tbl,	/* PCI_ID table  */
	.probe	= _rtl92e_pci_probe,	/* probe fn      */
	.remove	 = _rtl92e_pci_disconnect,	/* remove fn */
	.driver.pm = &rtl92e_pm_ops,
};

static short _rtl92e_is_tx_queue_empty(struct net_device *dev);
static void _rtl92e_watchdog_wq_cb(void *data);
static void _rtl92e_watchdog_timer_cb(struct timer_list *t);
static void _rtl92e_hard_data_xmit(struct sk_buff *skb, struct net_device *dev,
				   int rate);
static int _rtl92e_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void _rtl92e_tx_cmd(struct net_device *dev, struct sk_buff *skb);
static short _rtl92e_tx(struct net_device *dev, struct sk_buff *skb);
static short _rtl92e_pci_initdescring(struct net_device *dev);
static void _rtl92e_irq_tx_tasklet(struct tasklet_struct *t);
static void _rtl92e_irq_rx_tasklet(struct tasklet_struct *t);
static void _rtl92e_cancel_deferred_work(struct r8192_priv *priv);
static int _rtl92e_up(struct net_device *dev, bool is_silent_reset);
static int _rtl92e_try_up(struct net_device *dev);
static int _rtl92e_down(struct net_device *dev, bool shutdownrf);
static void _rtl92e_restart(void *data);

/****************************************************************************
 *  -----------------------------IO STUFF-------------------------
 ****************************************************************************/

u8 rtl92e_readb(struct net_device *dev, int x)
{
	return 0xff & readb((u8 __iomem *)dev->mem_start + x);
}

u32 rtl92e_readl(struct net_device *dev, int x)
{
	return readl((u8 __iomem *)dev->mem_start + x);
}

u16 rtl92e_readw(struct net_device *dev, int x)
{
	return readw((u8 __iomem *)dev->mem_start + x);
}

void rtl92e_writeb(struct net_device *dev, int x, u8 y)
{
	writeb(y, (u8 __iomem *)dev->mem_start + x);

	udelay(20);
}

void rtl92e_writel(struct net_device *dev, int x, u32 y)
{
	writel(y, (u8 __iomem *)dev->mem_start + x);

	udelay(20);
}

void rtl92e_writew(struct net_device *dev, int x, u16 y)
{
	writew(y, (u8 __iomem *)dev->mem_start + x);

	udelay(20);
}

/****************************************************************************
 *  -----------------------------GENERAL FUNCTION-------------------------
 ****************************************************************************/
bool rtl92e_set_rf_state(struct net_device *dev,
			 enum rt_rf_power_state state_to_set,
			 RT_RF_CHANGE_SOURCE change_source)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	bool action_allowed = false;
	bool connect_by_ssid = false;
	enum rt_rf_power_state rt_state;
	u16 rf_wait_counter = 0;
	unsigned long flag;

	while (true) {
		spin_lock_irqsave(&priv->rf_ps_lock, flag);
		if (priv->rf_change_in_progress) {
			spin_unlock_irqrestore(&priv->rf_ps_lock, flag);

			while (priv->rf_change_in_progress) {
				rf_wait_counter++;
				mdelay(1);

				if (rf_wait_counter > 100) {
					netdev_warn(dev,
						    "%s(): Timeout waiting for RF change.\n",
						    __func__);
					return false;
				}
			}
		} else {
			priv->rf_change_in_progress = true;
			spin_unlock_irqrestore(&priv->rf_ps_lock, flag);
			break;
		}
	}

	rt_state = priv->rtllib->rf_power_state;

	switch (state_to_set) {
	case rf_on:
		priv->rtllib->rf_off_reason &= (~change_source);

		if ((change_source == RF_CHANGE_BY_HW) && priv->hw_radio_off)
			priv->hw_radio_off = false;

		if (!priv->rtllib->rf_off_reason) {
			priv->rtllib->rf_off_reason = 0;
			action_allowed = true;

			if (rt_state == rf_off &&
			    change_source >= RF_CHANGE_BY_HW)
				connect_by_ssid = true;
		}
		break;

	case rf_off:

		if ((priv->rtllib->iw_mode == IW_MODE_INFRA) ||
		    (priv->rtllib->iw_mode == IW_MODE_ADHOC)) {
			if ((priv->rtllib->rf_off_reason > RF_CHANGE_BY_IPS) ||
			    (change_source > RF_CHANGE_BY_IPS)) {
				if (ieee->state == RTLLIB_LINKED)
					priv->blinked_ingpio = true;
				else
					priv->blinked_ingpio = false;
				rtllib_MgntDisconnect(priv->rtllib,
						      WLAN_REASON_DISASSOC_STA_HAS_LEFT);
			}
		}
		if ((change_source == RF_CHANGE_BY_HW) && !priv->hw_radio_off)
			priv->hw_radio_off = true;
		priv->rtllib->rf_off_reason |= change_source;
		action_allowed = true;
		break;

	case rf_sleep:
		priv->rtllib->rf_off_reason |= change_source;
		action_allowed = true;
		break;

	default:
		break;
	}

	if (action_allowed) {
		rtl92e_set_rf_power_state(dev, state_to_set);
		if (state_to_set == rf_on) {
			if (connect_by_ssid && priv->blinked_ingpio) {
				schedule_delayed_work(
					 &ieee->associate_procedure_wq, 0);
				priv->blinked_ingpio = false;
			}
		}
	}

	spin_lock_irqsave(&priv->rf_ps_lock, flag);
	priv->rf_change_in_progress = false;
	spin_unlock_irqrestore(&priv->rf_ps_lock, flag);
	return action_allowed;
}

static short _rtl92e_check_nic_enough_desc(struct net_device *dev, int prio)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtl8192_tx_ring *ring = &priv->tx_ring[prio];

	if (ring->entries - skb_queue_len(&ring->queue) >= 2)
		return 1;
	return 0;
}

static void _rtl92e_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	schedule_work(&priv->reset_wq);
	netdev_info(dev, "TXTIMEOUT");
}

void rtl92e_irq_enable(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->irq_enabled = 1;

	priv->ops->irq_enable(dev);
}

void rtl92e_irq_disable(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->ops->irq_disable(dev);

	priv->irq_enabled = 0;
}

static void _rtl92e_set_chan(struct net_device *dev, short ch)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->chan_forced)
		return;

	priv->chan = ch;

	if (priv->rf_set_chan)
		priv->rf_set_chan(dev, priv->chan);
}

static void _rtl92e_update_cap(struct net_device *dev, u16 cap)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_network *net = &priv->rtllib->current_network;
	bool		ShortPreamble;

	if (cap & WLAN_CAPABILITY_SHORT_PREAMBLE) {
		if (priv->dot11_current_preamble_mode != PREAMBLE_SHORT) {
			ShortPreamble = true;
			priv->dot11_current_preamble_mode = PREAMBLE_SHORT;
			priv->rtllib->SetHwRegHandler(dev, HW_VAR_ACK_PREAMBLE,
					(unsigned char *)&ShortPreamble);
		}
	} else {
		if (priv->dot11_current_preamble_mode != PREAMBLE_LONG) {
			ShortPreamble = false;
			priv->dot11_current_preamble_mode = PREAMBLE_LONG;
			priv->rtllib->SetHwRegHandler(dev, HW_VAR_ACK_PREAMBLE,
					      (unsigned char *)&ShortPreamble);
		}
	}

	if (net->mode & (IEEE_G | IEEE_N_24G)) {
		u8	slot_time_val;
		u8	cur_slot_time = priv->slot_time;

		if ((cap & WLAN_CAPABILITY_SHORT_SLOT_TIME) &&
		   (!priv->rtllib->pHTInfo->current_rt2rt_long_slot_time)) {
			if (cur_slot_time != SHORT_SLOT_TIME) {
				slot_time_val = SHORT_SLOT_TIME;
				priv->rtllib->SetHwRegHandler(dev,
					 HW_VAR_SLOT_TIME, &slot_time_val);
			}
		} else {
			if (cur_slot_time != NON_SHORT_SLOT_TIME) {
				slot_time_val = NON_SHORT_SLOT_TIME;
				priv->rtllib->SetHwRegHandler(dev,
					 HW_VAR_SLOT_TIME, &slot_time_val);
			}
		}
	}
}

static const struct rtllib_qos_parameters def_qos_parameters = {
	{cpu_to_le16(3), cpu_to_le16(3), cpu_to_le16(3), cpu_to_le16(3)},
	{cpu_to_le16(7), cpu_to_le16(7), cpu_to_le16(7), cpu_to_le16(7)},
	{2, 2, 2, 2},
	{0, 0, 0, 0},
	{0, 0, 0, 0}
};

static void _rtl92e_update_beacon(void *data)
{
	struct r8192_priv *priv = container_of_work_rsl(data, struct r8192_priv,
				  update_beacon_wq.work);
	struct net_device *dev = priv->rtllib->dev;
	struct rtllib_device *ieee = priv->rtllib;
	struct rtllib_network *net = &ieee->current_network;

	if (ieee->pHTInfo->bCurrentHTSupport)
		HT_update_self_and_peer_setting(ieee, net);
	ieee->pHTInfo->current_rt2rt_long_slot_time = net->bssht.bd_rt2rt_long_slot_time;
	ieee->pHTInfo->RT2RT_HT_Mode = net->bssht.rt2rt_ht_mode;
	_rtl92e_update_cap(dev, net->capability);
}

static void _rtl92e_qos_activate(void *data)
{
	struct r8192_priv *priv = container_of_work_rsl(data, struct r8192_priv,
				  qos_activate);
	struct net_device *dev = priv->rtllib->dev;
	int i;

	mutex_lock(&priv->mutex);
	if (priv->rtllib->state != RTLLIB_LINKED)
		goto success;

	for (i = 0; i <  QOS_QUEUE_NUM; i++)
		priv->rtllib->SetHwRegHandler(dev, HW_VAR_AC_PARAM, (u8 *)(&i));

success:
	mutex_unlock(&priv->mutex);
}

static int _rtl92e_qos_handle_probe_response(struct r8192_priv *priv,
					     int active_network,
					     struct rtllib_network *network)
{
	int ret = 0;
	u32 size = sizeof(struct rtllib_qos_parameters);

	if (priv->rtllib->state != RTLLIB_LINKED)
		return ret;

	if (priv->rtllib->iw_mode != IW_MODE_INFRA)
		return ret;

	if (network->flags & NETWORK_HAS_QOS_MASK) {
		if (active_network &&
				(network->flags & NETWORK_HAS_QOS_PARAMETERS))
			network->qos_data.active = network->qos_data.supported;

		if ((network->qos_data.active == 1) && (active_network == 1) &&
				(network->flags & NETWORK_HAS_QOS_PARAMETERS) &&
				(network->qos_data.old_param_count !=
				network->qos_data.param_count)) {
			network->qos_data.old_param_count =
				network->qos_data.param_count;
			priv->rtllib->wmm_acm = network->qos_data.wmm_acm;
			schedule_work(&priv->qos_activate);
		}
	} else {
		memcpy(&priv->rtllib->current_network.qos_data.parameters,
		       &def_qos_parameters, size);

		if ((network->qos_data.active == 1) && (active_network == 1))
			schedule_work(&priv->qos_activate);

		network->qos_data.active = 0;
		network->qos_data.supported = 0;
	}

	return 0;
}

static int _rtl92e_handle_beacon(struct net_device *dev,
				 struct rtllib_beacon *beacon,
				 struct rtllib_network *network)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	_rtl92e_qos_handle_probe_response(priv, 1, network);

	schedule_delayed_work(&priv->update_beacon_wq, 0);
	return 0;
}

static int _rtl92e_qos_assoc_resp(struct r8192_priv *priv,
				  struct rtllib_network *network)
{
	unsigned long flags;
	u32 size = sizeof(struct rtllib_qos_parameters);
	int set_qos_param = 0;

	if (!priv || !network)
		return 0;

	if (priv->rtllib->state != RTLLIB_LINKED)
		return 0;

	if (priv->rtllib->iw_mode != IW_MODE_INFRA)
		return 0;

	spin_lock_irqsave(&priv->rtllib->lock, flags);
	if (network->flags & NETWORK_HAS_QOS_PARAMETERS) {
		memcpy(&priv->rtllib->current_network.qos_data.parameters,
		       &network->qos_data.parameters,
		       sizeof(struct rtllib_qos_parameters));
		priv->rtllib->current_network.qos_data.active = 1;
		priv->rtllib->wmm_acm = network->qos_data.wmm_acm;
		set_qos_param = 1;
		priv->rtllib->current_network.qos_data.old_param_count =
			priv->rtllib->current_network.qos_data.param_count;
		priv->rtllib->current_network.qos_data.param_count =
			network->qos_data.param_count;
	} else {
		memcpy(&priv->rtllib->current_network.qos_data.parameters,
		&def_qos_parameters, size);
		priv->rtllib->current_network.qos_data.active = 0;
		priv->rtllib->current_network.qos_data.supported = 0;
		set_qos_param = 1;
	}

	spin_unlock_irqrestore(&priv->rtllib->lock, flags);

	if (set_qos_param == 1) {
		rtl92e_dm_init_edca_turbo(priv->rtllib->dev);
		schedule_work(&priv->qos_activate);
	}
	return 0;
}

static int _rtl92e_handle_assoc_response(struct net_device *dev,
				 struct rtllib_assoc_response_frame *resp,
				 struct rtllib_network *network)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	_rtl92e_qos_assoc_resp(priv, network);
	return 0;
}

static void _rtl92e_prepare_beacon(struct tasklet_struct *t)
{
	struct r8192_priv *priv = from_tasklet(priv, t,
					       irq_prepare_beacon_tasklet);
	struct net_device *dev = priv->rtllib->dev;
	struct sk_buff *pskb = NULL, *pnewskb = NULL;
	struct cb_desc *tcb_desc = NULL;
	struct rtl8192_tx_ring *ring = NULL;
	struct tx_desc *pdesc = NULL;

	ring = &priv->tx_ring[BEACON_QUEUE];
	pskb = __skb_dequeue(&ring->queue);
	kfree_skb(pskb);

	pnewskb = rtllib_get_beacon(priv->rtllib);
	if (!pnewskb)
		return;

	tcb_desc = (struct cb_desc *)(pnewskb->cb + 8);
	tcb_desc->queue_index = BEACON_QUEUE;
	tcb_desc->data_rate = 2;
	tcb_desc->RATRIndex = 7;
	tcb_desc->bTxDisableRateFallBack = 1;
	tcb_desc->bTxUseDriverAssingedRate = 1;
	skb_push(pnewskb, priv->rtllib->tx_headroom);

	pdesc = &ring->desc[0];
	priv->ops->tx_fill_descriptor(dev, pdesc, tcb_desc, pnewskb);
	__skb_queue_tail(&ring->queue, pnewskb);
	pdesc->OWN = 1;
}

static void _rtl92e_stop_beacon(struct net_device *dev)
{
}

void rtl92e_config_rate(struct net_device *dev, u16 *rate_config)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_network *net;
	u8 i = 0, basic_rate = 0;

	net = &priv->rtllib->current_network;

	for (i = 0; i < net->rates_len; i++) {
		basic_rate = net->rates[i] & 0x7f;
		switch (basic_rate) {
		case MGN_1M:
			*rate_config |= RRSR_1M;
			break;
		case MGN_2M:
			*rate_config |= RRSR_2M;
			break;
		case MGN_5_5M:
			*rate_config |= RRSR_5_5M;
			break;
		case MGN_11M:
			*rate_config |= RRSR_11M;
			break;
		case MGN_6M:
			*rate_config |= RRSR_6M;
			break;
		case MGN_9M:
			*rate_config |= RRSR_9M;
			break;
		case MGN_12M:
			*rate_config |= RRSR_12M;
			break;
		case MGN_18M:
			*rate_config |= RRSR_18M;
			break;
		case MGN_24M:
			*rate_config |= RRSR_24M;
			break;
		case MGN_36M:
			*rate_config |= RRSR_36M;
			break;
		case MGN_48M:
			*rate_config |= RRSR_48M;
			break;
		case MGN_54M:
			*rate_config |= RRSR_54M;
			break;
		}
	}

	for (i = 0; i < net->rates_ex_len; i++) {
		basic_rate = net->rates_ex[i] & 0x7f;
		switch (basic_rate) {
		case MGN_1M:
			*rate_config |= RRSR_1M;
			break;
		case MGN_2M:
			*rate_config |= RRSR_2M;
			break;
		case MGN_5_5M:
			*rate_config |= RRSR_5_5M;
			break;
		case MGN_11M:
			*rate_config |= RRSR_11M;
			break;
		case MGN_6M:
			*rate_config |= RRSR_6M;
			break;
		case MGN_9M:
			*rate_config |= RRSR_9M;
			break;
		case MGN_12M:
			*rate_config |= RRSR_12M;
			break;
		case MGN_18M:
			*rate_config |= RRSR_18M;
			break;
		case MGN_24M:
			*rate_config |= RRSR_24M;
			break;
		case MGN_36M:
			*rate_config |= RRSR_36M;
			break;
		case MGN_48M:
			*rate_config |= RRSR_48M;
			break;
		case MGN_54M:
			*rate_config |= RRSR_54M;
			break;
		}
	}
}

static void _rtl92e_refresh_support_rate(struct r8192_priv *priv)
{
	struct rtllib_device *ieee = priv->rtllib;

	if (ieee->mode == WIRELESS_MODE_N_24G ||
	    ieee->mode == WIRELESS_MODE_N_5G) {
		memcpy(ieee->Regdot11HTOperationalRateSet,
		       ieee->RegHTSuppRateSet, 16);
		memcpy(ieee->Regdot11TxHTOperationalRateSet,
		       ieee->RegHTSuppRateSet, 16);

	} else {
		memset(ieee->Regdot11HTOperationalRateSet, 0, 16);
	}
}

static u8 _rtl92e_get_supported_wireless_mode(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 ret = 0;

	switch (priv->rf_chip) {
	case RF_8225:
	case RF_8256:
	case RF_6052:
	case RF_PSEUDO_11N:
		ret = (WIRELESS_MODE_N_24G | WIRELESS_MODE_G | WIRELESS_MODE_B);
		break;
	case RF_8258:
		ret = (WIRELESS_MODE_A | WIRELESS_MODE_N_5G);
		break;
	default:
		ret = WIRELESS_MODE_B;
		break;
	}
	return ret;
}

void rtl92e_set_wireless_mode(struct net_device *dev, u8 wireless_mode)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 bSupportMode = _rtl92e_get_supported_wireless_mode(dev);

	if ((wireless_mode == WIRELESS_MODE_AUTO) ||
	    ((wireless_mode & bSupportMode) == 0)) {
		if (bSupportMode & WIRELESS_MODE_N_24G) {
			wireless_mode = WIRELESS_MODE_N_24G;
		} else if (bSupportMode & WIRELESS_MODE_N_5G) {
			wireless_mode = WIRELESS_MODE_N_5G;
		} else if ((bSupportMode & WIRELESS_MODE_A)) {
			wireless_mode = WIRELESS_MODE_A;
		} else if ((bSupportMode & WIRELESS_MODE_G)) {
			wireless_mode = WIRELESS_MODE_G;
		} else if ((bSupportMode & WIRELESS_MODE_B)) {
			wireless_mode = WIRELESS_MODE_B;
		} else {
			netdev_info(dev,
				    "%s(): Unsupported mode requested. Fallback to 802.11b\n",
				    __func__);
			wireless_mode = WIRELESS_MODE_B;
		}
	}

	if ((wireless_mode & (WIRELESS_MODE_B | WIRELESS_MODE_G)) ==
	    (WIRELESS_MODE_G | WIRELESS_MODE_B))
		wireless_mode = WIRELESS_MODE_G;

	priv->rtllib->mode = wireless_mode;

	if ((wireless_mode == WIRELESS_MODE_N_24G) ||
	    (wireless_mode == WIRELESS_MODE_N_5G)) {
		priv->rtllib->pHTInfo->bEnableHT = 1;
	} else {
		priv->rtllib->pHTInfo->bEnableHT = 0;
	}
	_rtl92e_refresh_support_rate(priv);
}

static int _rtl92e_sta_up(struct net_device *dev, bool is_silent_reset)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					(&priv->rtllib->PowerSaveControl);
	bool init_status;

	priv->bDriverIsGoingToUnload = false;
	priv->bdisable_nic = false;

	priv->up = 1;
	priv->rtllib->ieee_up = 1;

	priv->up_first_time = 0;
	priv->bfirst_init = true;
	init_status = priv->ops->initialize_adapter(dev);
	if (!init_status) {
		netdev_err(dev, "%s(): Initialization failed!\n", __func__);
		priv->bfirst_init = false;
		return -1;
	}

	RT_CLEAR_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC);
	priv->bfirst_init = false;

	if (priv->polling_timer_on == 0)
		rtl92e_check_rfctrl_gpio_timer(&priv->gpio_polling_timer);

	if (priv->rtllib->state != RTLLIB_LINKED)
		rtllib_softmac_start_protocol(priv->rtllib, 0);
	rtllib_reset_queue(priv->rtllib);
	_rtl92e_watchdog_timer_cb(&priv->watch_dog_timer);

	if (!netif_queue_stopped(dev))
		netif_start_queue(dev);
	else
		netif_wake_queue(dev);

	priv->bfirst_after_down = false;
	return 0;
}

static int _rtl92e_sta_down(struct net_device *dev, bool shutdownrf)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags = 0;
	u8 RFInProgressTimeOut = 0;

	if (priv->up == 0)
		return -1;

	if (priv->rtllib->rtllib_ips_leave)
		priv->rtllib->rtllib_ips_leave(dev);

	if (priv->rtllib->state == RTLLIB_LINKED)
		rtl92e_leisure_ps_leave(dev);

	priv->bDriverIsGoingToUnload = true;
	priv->up = 0;
	priv->rtllib->ieee_up = 0;
	priv->bfirst_after_down = true;
	if (!netif_queue_stopped(dev))
		netif_stop_queue(dev);

	priv->rtllib->wpa_ie_len = 0;
	kfree(priv->rtllib->wpa_ie);
	priv->rtllib->wpa_ie = NULL;
	rtl92e_cam_reset(dev);
	memset(priv->rtllib->swcamtable, 0, sizeof(struct sw_cam_table) * 32);
	rtl92e_irq_disable(dev);

	del_timer_sync(&priv->watch_dog_timer);
	_rtl92e_cancel_deferred_work(priv);
	cancel_delayed_work(&priv->rtllib->hw_wakeup_wq);

	rtllib_softmac_stop_protocol(priv->rtllib, 0, true);
	spin_lock_irqsave(&priv->rf_ps_lock, flags);
	while (priv->rf_change_in_progress) {
		spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
		if (RFInProgressTimeOut > 100) {
			spin_lock_irqsave(&priv->rf_ps_lock, flags);
			break;
		}
		mdelay(1);
		RFInProgressTimeOut++;
		spin_lock_irqsave(&priv->rf_ps_lock, flags);
	}
	priv->rf_change_in_progress = true;
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	priv->ops->stop_adapter(dev, false);
	spin_lock_irqsave(&priv->rf_ps_lock, flags);
	priv->rf_change_in_progress = false;
	spin_unlock_irqrestore(&priv->rf_ps_lock, flags);
	udelay(100);
	memset(&priv->rtllib->current_network, 0,
	       offsetof(struct rtllib_network, list));

	return 0;
}

static void _rtl92e_init_priv_handler(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->rtllib->softmac_hard_start_xmit	= _rtl92e_hard_start_xmit;
	priv->rtllib->set_chan			= _rtl92e_set_chan;
	priv->rtllib->link_change		= priv->ops->link_change;
	priv->rtllib->softmac_data_hard_start_xmit = _rtl92e_hard_data_xmit;
	priv->rtllib->check_nic_enough_desc	= _rtl92e_check_nic_enough_desc;
	priv->rtllib->handle_assoc_response	= _rtl92e_handle_assoc_response;
	priv->rtllib->handle_beacon		= _rtl92e_handle_beacon;
	priv->rtllib->SetWirelessMode		= rtl92e_set_wireless_mode;
	priv->rtllib->LeisurePSLeave		= rtl92e_leisure_ps_leave;
	priv->rtllib->SetBWModeHandler		= rtl92e_set_bw_mode;
	priv->rf_set_chan			= rtl92e_set_channel;

	priv->rtllib->start_send_beacons = rtl92e_start_beacon;
	priv->rtllib->stop_send_beacons = _rtl92e_stop_beacon;

	priv->rtllib->sta_wake_up = rtl92e_hw_wakeup;
	priv->rtllib->enter_sleep_state = rtl92e_enter_sleep;
	priv->rtllib->ps_is_queue_empty = _rtl92e_is_tx_queue_empty;

	priv->rtllib->GetNmodeSupportBySecCfg = rtl92e_get_nmode_support_by_sec;
	priv->rtllib->GetHalfNmodeSupportByAPsHandler =
						rtl92e_is_halfn_supported_by_ap;

	priv->rtllib->SetHwRegHandler = rtl92e_set_reg;
	priv->rtllib->AllowAllDestAddrHandler = rtl92e_set_monitor_mode;
	priv->rtllib->SetFwCmdHandler = NULL;
	priv->rtllib->InitialGainHandler = rtl92e_init_gain;
	priv->rtllib->rtllib_ips_leave_wq = rtl92e_rtllib_ips_leave_wq;
	priv->rtllib->rtllib_ips_leave = rtl92e_rtllib_ips_leave;

	priv->rtllib->LedControlHandler = NULL;
	priv->rtllib->UpdateBeaconInterruptHandler = NULL;

	priv->rtllib->ScanOperationBackupHandler = rtl92e_scan_op_backup;
}

static void _rtl92e_init_priv_constant(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					&priv->rtllib->PowerSaveControl;

	pPSC->RegMaxLPSAwakeIntvl = 5;
}

static void _rtl92e_init_priv_variable(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 i;

	priv->AcmMethod = eAcmWay2_SW;
	priv->dot11_current_preamble_mode = PREAMBLE_AUTO;
	priv->rtllib->status = 0;
	priv->polling_timer_on = 0;
	priv->up_first_time = 1;
	priv->blinked_ingpio = false;
	priv->bDriverIsGoingToUnload = false;
	priv->being_init_adapter = false;
	priv->initialized_at_probe = false;
	priv->bdisable_nic = false;
	priv->bfirst_init = false;
	priv->txringcount = 64;
	priv->rxbuffersize = 9100;
	priv->rxringcount = MAX_RX_COUNT;
	priv->irq_enabled = 0;
	priv->chan = 1;
	priv->RegChannelPlan = 0xf;
	priv->rtllib->mode = WIRELESS_MODE_AUTO;
	priv->rtllib->iw_mode = IW_MODE_INFRA;
	priv->rtllib->bNetPromiscuousMode = false;
	priv->rtllib->IntelPromiscuousModeInfo.bPromiscuousOn = false;
	priv->rtllib->IntelPromiscuousModeInfo.bFilterSourceStationFrame =
								 false;
	priv->rtllib->ieee_up = 0;
	priv->retry_rts = DEFAULT_RETRY_RTS;
	priv->retry_data = DEFAULT_RETRY_DATA;
	priv->rtllib->rts = DEFAULT_RTS_THRESHOLD;
	priv->rtllib->rate = 110;
	priv->rtllib->short_slot = 1;
	priv->promisc = (dev->flags & IFF_PROMISC) ? 1 : 0;
	priv->bcck_in_ch14 = false;
	priv->bfsync_processing  = false;
	priv->CCKPresentAttentuation = 0;
	priv->rfa_txpowertrackingindex = 0;
	priv->rfc_txpowertrackingindex = 0;
	priv->CckPwEnl = 6;
	priv->ScanDelay = 50;
	priv->ResetProgress = RESET_TYPE_NORESET;
	priv->bForcedSilentReset = false;
	priv->bDisableNormalResetCheck = false;
	priv->force_reset = false;
	memset(priv->rtllib->swcamtable, 0, sizeof(struct sw_cam_table) * 32);

	memset(&priv->InterruptLog, 0, sizeof(struct log_int_8190));
	priv->RxCounter = 0;
	priv->rtllib->wx_set_enc = 0;
	priv->hw_radio_off = false;
	priv->RegRfOff = false;
	priv->isRFOff = false;
	priv->bInPowerSaveMode = false;
	priv->rtllib->rf_off_reason = 0;
	priv->rf_change_in_progress = false;
	priv->bHwRfOffAction = 0;
	priv->SetRFPowerStateInProgress = false;
	priv->rtllib->PowerSaveControl.bInactivePs = true;
	priv->rtllib->PowerSaveControl.bIPSModeBackup = false;
	priv->rtllib->PowerSaveControl.bLeisurePs = true;
	priv->rtllib->PowerSaveControl.bFwCtrlLPS = false;
	priv->rtllib->LPSDelayCnt = 0;
	priv->rtllib->sta_sleep = LPS_IS_WAKE;
	priv->rtllib->rf_power_state = rf_on;

	priv->rtllib->current_network.beacon_interval = DEFAULT_BEACONINTERVAL;
	priv->rtllib->iw_mode = IW_MODE_INFRA;
	priv->rtllib->active_scan = 1;
	priv->rtllib->be_scan_inprogress = false;
	priv->rtllib->modulation = RTLLIB_CCK_MODULATION |
				   RTLLIB_OFDM_MODULATION;
	priv->rtllib->host_encrypt = 1;
	priv->rtllib->host_decrypt = 1;

	priv->rtllib->fts = DEFAULT_FRAG_THRESHOLD;

	priv->card_type = PCI;

	priv->pFirmware = vzalloc(sizeof(struct rt_firmware));
	if (!priv->pFirmware)
		netdev_err(dev,
			   "rtl8192e: Unable to allocate space for firmware\n");

	skb_queue_head_init(&priv->skb_queue);

	for (i = 0; i < MAX_QUEUE_SIZE; i++)
		skb_queue_head_init(&priv->rtllib->skb_waitQ[i]);
	for (i = 0; i < MAX_QUEUE_SIZE; i++)
		skb_queue_head_init(&priv->rtllib->skb_aggQ[i]);
}

static void _rtl92e_init_priv_lock(struct r8192_priv *priv)
{
	spin_lock_init(&priv->tx_lock);
	spin_lock_init(&priv->irq_th_lock);
	spin_lock_init(&priv->rf_ps_lock);
	spin_lock_init(&priv->ps_lock);
	mutex_init(&priv->wx_mutex);
	mutex_init(&priv->rf_mutex);
	mutex_init(&priv->mutex);
}

static void _rtl92e_init_priv_task(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	INIT_WORK_RSL(&priv->reset_wq, (void *)_rtl92e_restart, dev);
	INIT_WORK_RSL(&priv->rtllib->ips_leave_wq, (void *)rtl92e_ips_leave_wq,
		      dev);
	INIT_DELAYED_WORK_RSL(&priv->watch_dog_wq,
			      (void *)_rtl92e_watchdog_wq_cb, dev);
	INIT_DELAYED_WORK_RSL(&priv->txpower_tracking_wq,
			      (void *)rtl92e_dm_txpower_tracking_wq, dev);
	INIT_DELAYED_WORK_RSL(&priv->rfpath_check_wq,
			      (void *)rtl92e_dm_rf_pathcheck_wq, dev);
	INIT_DELAYED_WORK_RSL(&priv->update_beacon_wq,
			      (void *)_rtl92e_update_beacon, dev);
	INIT_WORK_RSL(&priv->qos_activate, (void *)_rtl92e_qos_activate, dev);
	INIT_DELAYED_WORK_RSL(&priv->rtllib->hw_wakeup_wq,
			      (void *)rtl92e_hw_wakeup_wq, dev);
	INIT_DELAYED_WORK_RSL(&priv->rtllib->hw_sleep_wq,
			      (void *)rtl92e_hw_sleep_wq, dev);
	tasklet_setup(&priv->irq_rx_tasklet, _rtl92e_irq_rx_tasklet);
	tasklet_setup(&priv->irq_tx_tasklet, _rtl92e_irq_tx_tasklet);
	tasklet_setup(&priv->irq_prepare_beacon_tasklet,
		      _rtl92e_prepare_beacon);
}

static short _rtl92e_get_channel_map(struct net_device *dev)
{
	int i;

	struct r8192_priv *priv = rtllib_priv(dev);

	if ((priv->rf_chip != RF_8225) && (priv->rf_chip != RF_8256) &&
						(priv->rf_chip != RF_6052)) {
		netdev_err(dev, "%s: unknown rf chip, can't set channel map\n",
			   __func__);
		return -1;
	}

	if (priv->ChannelPlan >= COUNTRY_CODE_MAX) {
		netdev_info(dev,
			    "rtl819x_init:Error channel plan! Set to default.\n");
		priv->ChannelPlan = COUNTRY_CODE_FCC;
	}
	dot11d_init(priv->rtllib);
	dot11d_channel_map(priv->ChannelPlan, priv->rtllib);
	for (i = 1; i <= 11; i++)
		(priv->rtllib->active_channel_map)[i] = 1;
	(priv->rtllib->active_channel_map)[12] = 2;
	(priv->rtllib->active_channel_map)[13] = 2;

	return 0;
}

static short _rtl92e_init(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	memset(&priv->stats, 0, sizeof(struct rt_stats));

	_rtl92e_init_priv_handler(dev);
	_rtl92e_init_priv_constant(dev);
	_rtl92e_init_priv_variable(dev);
	_rtl92e_init_priv_lock(priv);
	_rtl92e_init_priv_task(dev);
	priv->ops->get_eeprom_size(dev);
	priv->ops->init_adapter_variable(dev);
	_rtl92e_get_channel_map(dev);

	rtl92e_dm_init(dev);

	timer_setup(&priv->watch_dog_timer, _rtl92e_watchdog_timer_cb, 0);

	timer_setup(&priv->gpio_polling_timer, rtl92e_check_rfctrl_gpio_timer,
		    0);

	rtl92e_irq_disable(dev);
	if (request_irq(dev->irq, _rtl92e_irq, IRQF_SHARED, dev->name, dev)) {
		netdev_err(dev, "Error allocating IRQ %d", dev->irq);
		return -1;
	}

	priv->irq = dev->irq;

	if (_rtl92e_pci_initdescring(dev) != 0) {
		netdev_err(dev, "Endopoints initialization failed");
		free_irq(dev->irq, dev);
		return -1;
	}

	return 0;
}

/***************************************************************************
 * -------------------------------WATCHDOG STUFF---------------------------
 **************************************************************************/
static short _rtl92e_is_tx_queue_empty(struct net_device *dev)
{
	int i = 0;
	struct r8192_priv *priv = rtllib_priv(dev);

	for (i = 0; i <= MGNT_QUEUE; i++) {
		if ((i == TXCMD_QUEUE) || (i == HCCA_QUEUE))
			continue;
		if (skb_queue_len(&(&priv->tx_ring[i])->queue) > 0) {
			netdev_info(dev, "===>tx queue is not empty:%d, %d\n",
			       i, skb_queue_len(&(&priv->tx_ring[i])->queue));
			return 0;
		}
	}
	return 1;
}

static enum reset_type _rtl92e_tx_check_stuck(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8	QueueID;
	bool	bCheckFwTxCnt = false;
	struct rtl8192_tx_ring  *ring = NULL;
	struct sk_buff *skb = NULL;
	struct cb_desc *tcb_desc = NULL;
	unsigned long flags = 0;

	switch (priv->rtllib->ps) {
	case RTLLIB_PS_DISABLED:
		break;
	case (RTLLIB_PS_MBCAST | RTLLIB_PS_UNICAST):
		break;
	default:
		break;
	}
	spin_lock_irqsave(&priv->irq_th_lock, flags);
	for (QueueID = 0; QueueID < MAX_TX_QUEUE; QueueID++) {
		if (QueueID == TXCMD_QUEUE)
			continue;

		if (QueueID == BEACON_QUEUE)
			continue;

		ring = &priv->tx_ring[QueueID];

		if (skb_queue_len(&ring->queue) == 0) {
			continue;
		} else {
			skb = __skb_peek(&ring->queue);
			tcb_desc = (struct cb_desc *)(skb->cb +
				    MAX_DEV_ADDR_SIZE);
			tcb_desc->nStuckCount++;
			bCheckFwTxCnt = true;
			if (tcb_desc->nStuckCount > 1)
				netdev_info(dev,
					    "%s: QueueID=%d tcb_desc->nStuckCount=%d\n",
					    __func__, QueueID,
					    tcb_desc->nStuckCount);
		}
	}
	spin_unlock_irqrestore(&priv->irq_th_lock, flags);

	if (bCheckFwTxCnt) {
		if (priv->ops->tx_check_stuck_handler(dev))
			return RESET_TYPE_SILENT;
	}

	return RESET_TYPE_NORESET;
}

static enum reset_type _rtl92e_rx_check_stuck(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->ops->rx_check_stuck_handler(dev))
		return RESET_TYPE_SILENT;

	return RESET_TYPE_NORESET;
}

static enum reset_type _rtl92e_if_check_reset(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	enum reset_type TxResetType = RESET_TYPE_NORESET;
	enum reset_type RxResetType = RESET_TYPE_NORESET;
	enum rt_rf_power_state rfState;

	rfState = priv->rtllib->rf_power_state;

	if (rfState == rf_on)
		TxResetType = _rtl92e_tx_check_stuck(dev);

	if (rfState == rf_on &&
	    (priv->rtllib->iw_mode == IW_MODE_INFRA) &&
	    (priv->rtllib->state == RTLLIB_LINKED))
		RxResetType = _rtl92e_rx_check_stuck(dev);

	if (TxResetType == RESET_TYPE_NORMAL ||
	    RxResetType == RESET_TYPE_NORMAL) {
		netdev_info(dev, "%s(): TxResetType is %d, RxResetType is %d\n",
			    __func__, TxResetType, RxResetType);
		return RESET_TYPE_NORMAL;
	} else if (TxResetType == RESET_TYPE_SILENT ||
		   RxResetType == RESET_TYPE_SILENT) {
		netdev_info(dev, "%s(): TxResetType is %d, RxResetType is %d\n",
			    __func__, TxResetType, RxResetType);
		return RESET_TYPE_SILENT;
	} else {
		return RESET_TYPE_NORESET;
	}
}

static void _rtl92e_if_silent_reset(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8	reset_times = 0;
	int reset_status = 0;
	struct rtllib_device *ieee = priv->rtllib;
	unsigned long flag;

	if (priv->ResetProgress == RESET_TYPE_NORESET) {
		priv->ResetProgress = RESET_TYPE_SILENT;

		spin_lock_irqsave(&priv->rf_ps_lock, flag);
		if (priv->rf_change_in_progress) {
			spin_unlock_irqrestore(&priv->rf_ps_lock, flag);
			goto END;
		}
		priv->rf_change_in_progress = true;
		priv->bResetInProgress = true;
		spin_unlock_irqrestore(&priv->rf_ps_lock, flag);

RESET_START:

		mutex_lock(&priv->wx_mutex);

		if (priv->rtllib->state == RTLLIB_LINKED)
			rtl92e_leisure_ps_leave(dev);

		if (priv->up) {
			netdev_info(dev, "%s():the driver is not up.\n",
				    __func__);
			mutex_unlock(&priv->wx_mutex);
			return;
		}
		priv->up = 0;

		mdelay(1000);

		if (!netif_queue_stopped(dev))
			netif_stop_queue(dev);

		rtl92e_irq_disable(dev);
		del_timer_sync(&priv->watch_dog_timer);
		_rtl92e_cancel_deferred_work(priv);
		rtl92e_dm_deinit(dev);
		rtllib_stop_scan_syncro(ieee);

		if (ieee->state == RTLLIB_LINKED) {
			mutex_lock(&ieee->wx_mutex);
			netdev_info(dev, "ieee->state is RTLLIB_LINKED\n");
			rtllib_stop_send_beacons(priv->rtllib);
			del_timer_sync(&ieee->associate_timer);
			cancel_delayed_work(&ieee->associate_retry_wq);
			rtllib_stop_scan(ieee);
			netif_carrier_off(dev);
			mutex_unlock(&ieee->wx_mutex);
		} else {
			netdev_info(dev, "ieee->state is NOT LINKED\n");
			rtllib_softmac_stop_protocol(priv->rtllib, 0, true);
		}

		rtl92e_dm_backup_state(dev);

		mutex_unlock(&priv->wx_mutex);
		reset_status = _rtl92e_up(dev, true);

		if (reset_status == -1) {
			if (reset_times < 3) {
				reset_times++;
				goto RESET_START;
			} else {
				netdev_warn(dev, "%s():	Reset Failed\n",
					    __func__);
			}
		}

		ieee->is_silent_reset = 1;

		spin_lock_irqsave(&priv->rf_ps_lock, flag);
		priv->rf_change_in_progress = false;
		spin_unlock_irqrestore(&priv->rf_ps_lock, flag);

		rtl92e_enable_hw_security_config(dev);

		if (ieee->state == RTLLIB_LINKED && ieee->iw_mode ==
		    IW_MODE_INFRA) {
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel);

			schedule_work(&ieee->associate_complete_wq);

		} else if (ieee->state == RTLLIB_LINKED && ieee->iw_mode ==
			   IW_MODE_ADHOC) {
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel);
			ieee->link_change(ieee->dev);

			notify_wx_assoc_event(ieee);

			rtllib_start_send_beacons(ieee);

			netif_carrier_on(ieee->dev);
		}

		rtl92e_cam_restore(dev);
		rtl92e_dm_restore_state(dev);
END:
		priv->ResetProgress = RESET_TYPE_NORESET;
		priv->reset_count++;

		priv->bForcedSilentReset = false;
		priv->bResetInProgress = false;

		rtl92e_writeb(dev, UFWP, 1);
	}
}

static void _rtl92e_update_rxcounts(struct r8192_priv *priv, u32 *TotalRxBcnNum,
				    u32 *TotalRxDataNum)
{
	u16	SlotIndex;
	u8	i;

	*TotalRxBcnNum = 0;
	*TotalRxDataNum = 0;

	SlotIndex = (priv->rtllib->LinkDetectInfo.SlotIndex++) %
			(priv->rtllib->LinkDetectInfo.SlotNum);
	priv->rtllib->LinkDetectInfo.RxBcnNum[SlotIndex] =
			priv->rtllib->LinkDetectInfo.NumRecvBcnInPeriod;
	priv->rtllib->LinkDetectInfo.RxDataNum[SlotIndex] =
			priv->rtllib->LinkDetectInfo.NumRecvDataInPeriod;
	for (i = 0; i < priv->rtllib->LinkDetectInfo.SlotNum; i++) {
		*TotalRxBcnNum += priv->rtllib->LinkDetectInfo.RxBcnNum[i];
		*TotalRxDataNum += priv->rtllib->LinkDetectInfo.RxDataNum[i];
	}
}

static void _rtl92e_watchdog_wq_cb(void *data)
{
	struct r8192_priv *priv = container_of_dwork_rsl(data,
				  struct r8192_priv, watch_dog_wq);
	struct net_device *dev = priv->rtllib->dev;
	struct rtllib_device *ieee = priv->rtllib;
	enum reset_type ResetType = RESET_TYPE_NORESET;
	static u8 check_reset_cnt;
	unsigned long flags;
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					(&priv->rtllib->PowerSaveControl);
	bool bBusyTraffic = false;
	bool	bHigherBusyTraffic = false;
	bool	bHigherBusyRxTraffic = false;
	bool bEnterPS = false;

	if (!priv->up || priv->hw_radio_off)
		return;

	if (priv->rtllib->state >= RTLLIB_LINKED) {
		if (priv->rtllib->CntAfterLink < 2)
			priv->rtllib->CntAfterLink++;
	} else {
		priv->rtllib->CntAfterLink = 0;
	}

	rtl92e_dm_watchdog(dev);

	if (!rtllib_act_scanning(priv->rtllib, false)) {
		if ((ieee->iw_mode == IW_MODE_INFRA) && (ieee->state ==
		     RTLLIB_NOLINK) &&
		     (ieee->rf_power_state == rf_on) && !ieee->is_set_key &&
		     (!ieee->proto_stoppping) && !ieee->wx_set_enc) {
			if ((ieee->PowerSaveControl.ReturnPoint ==
			     IPS_CALLBACK_NONE) &&
			     (!ieee->bNetPromiscuousMode)) {
				rtl92e_ips_enter(dev);
			}
		}
	}
	if ((ieee->state == RTLLIB_LINKED) && (ieee->iw_mode ==
	     IW_MODE_INFRA) && (!ieee->bNetPromiscuousMode)) {
		if (ieee->LinkDetectInfo.NumRxOkInPeriod > 100 ||
		ieee->LinkDetectInfo.NumTxOkInPeriod > 100)
			bBusyTraffic = true;

		if (ieee->LinkDetectInfo.NumRxOkInPeriod > 4000 ||
		    ieee->LinkDetectInfo.NumTxOkInPeriod > 4000) {
			bHigherBusyTraffic = true;
			if (ieee->LinkDetectInfo.NumRxOkInPeriod > 5000)
				bHigherBusyRxTraffic = true;
			else
				bHigherBusyRxTraffic = false;
		}

		if (((ieee->LinkDetectInfo.NumRxUnicastOkInPeriod +
		    ieee->LinkDetectInfo.NumTxOkInPeriod) > 8) ||
		    (ieee->LinkDetectInfo.NumRxUnicastOkInPeriod > 2))
			bEnterPS = false;
		else
			bEnterPS = true;

		if (ieee->current_network.beacon_interval < 95)
			bEnterPS = false;

		if (bEnterPS)
			rtl92e_leisure_ps_enter(dev);
		else
			rtl92e_leisure_ps_leave(dev);

	} else {
		rtl92e_leisure_ps_leave(dev);
	}

	ieee->LinkDetectInfo.NumRxOkInPeriod = 0;
	ieee->LinkDetectInfo.NumTxOkInPeriod = 0;
	ieee->LinkDetectInfo.NumRxUnicastOkInPeriod = 0;
	ieee->LinkDetectInfo.bBusyTraffic = bBusyTraffic;

	ieee->LinkDetectInfo.bHigherBusyTraffic = bHigherBusyTraffic;
	ieee->LinkDetectInfo.bHigherBusyRxTraffic = bHigherBusyRxTraffic;

	if (ieee->state == RTLLIB_LINKED && ieee->iw_mode == IW_MODE_INFRA) {
		u32	TotalRxBcnNum = 0;
		u32	TotalRxDataNum = 0;

		_rtl92e_update_rxcounts(priv, &TotalRxBcnNum, &TotalRxDataNum);

		if ((TotalRxBcnNum + TotalRxDataNum) == 0)
			priv->check_roaming_cnt++;
		else
			priv->check_roaming_cnt = 0;

		if (priv->check_roaming_cnt > 0) {
			if (ieee->rf_power_state == rf_off)
				netdev_info(dev, "%s(): RF is off\n", __func__);

			netdev_info(dev,
				    "===>%s(): AP is power off, chan:%d, connect another one\n",
				    __func__, priv->chan);

			ieee->state = RTLLIB_ASSOCIATING;

			RemovePeerTS(priv->rtllib,
				     priv->rtllib->current_network.bssid);
			ieee->is_roaming = true;
			ieee->is_set_key = false;
			ieee->link_change(dev);
			if (ieee->LedControlHandler)
				ieee->LedControlHandler(ieee->dev,
							LED_CTL_START_TO_LINK);

			notify_wx_assoc_event(ieee);

			if (!(ieee->rtllib_ap_sec_type(ieee) &
			     (SEC_ALG_CCMP | SEC_ALG_TKIP)))
				schedule_delayed_work(
					&ieee->associate_procedure_wq, 0);

			priv->check_roaming_cnt = 0;
		}
		ieee->LinkDetectInfo.NumRecvBcnInPeriod = 0;
		ieee->LinkDetectInfo.NumRecvDataInPeriod = 0;
	}

	spin_lock_irqsave(&priv->tx_lock, flags);
	if ((check_reset_cnt++ >= 3) && (!ieee->is_roaming) &&
	    (!priv->rf_change_in_progress) && (!pPSC->bSwRfProcessing)) {
		ResetType = _rtl92e_if_check_reset(dev);
		check_reset_cnt = 3;
	}
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	if (!priv->bDisableNormalResetCheck && ResetType == RESET_TYPE_NORMAL) {
		priv->ResetProgress = RESET_TYPE_NORMAL;
		return;
	}

	if (((priv->force_reset) || (!priv->bDisableNormalResetCheck &&
	      ResetType == RESET_TYPE_SILENT)))
		_rtl92e_if_silent_reset(dev);
	priv->force_reset = false;
	priv->bForcedSilentReset = false;
	priv->bResetInProgress = false;
}

static void _rtl92e_watchdog_timer_cb(struct timer_list *t)
{
	struct r8192_priv *priv = from_timer(priv, t, watch_dog_timer);

	schedule_delayed_work(&priv->watch_dog_wq, 0);
	mod_timer(&priv->watch_dog_timer, jiffies +
		  msecs_to_jiffies(RTLLIB_WATCH_DOG_TIME));
}

/****************************************************************************
 * ---------------------------- NIC TX/RX STUFF---------------------------
 ****************************************************************************/
void rtl92e_rx_enable(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->ops->rx_enable(dev);
}

void rtl92e_tx_enable(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->ops->tx_enable(dev);

	rtllib_reset_queue(priv->rtllib);
}

static void _rtl92e_free_rx_ring(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int i, rx_queue_idx;

	for (rx_queue_idx = 0; rx_queue_idx < MAX_RX_QUEUE;
	     rx_queue_idx++) {
		for (i = 0; i < priv->rxringcount; i++) {
			struct sk_buff *skb = priv->rx_buf[rx_queue_idx][i];

			if (!skb)
				continue;

			dma_unmap_single(&priv->pdev->dev,
					 *((dma_addr_t *)skb->cb),
					 priv->rxbuffersize, DMA_FROM_DEVICE);
			kfree_skb(skb);
		}

		dma_free_coherent(&priv->pdev->dev,
				  sizeof(*priv->rx_ring[rx_queue_idx]) * priv->rxringcount,
				  priv->rx_ring[rx_queue_idx],
				  priv->rx_ring_dma[rx_queue_idx]);
		priv->rx_ring[rx_queue_idx] = NULL;
	}
}

static void _rtl92e_free_tx_ring(struct net_device *dev, unsigned int prio)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtl8192_tx_ring *ring = &priv->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb = __skb_dequeue(&ring->queue);

		dma_unmap_single(&priv->pdev->dev, entry->TxBuffAddr,
				 skb->len, DMA_TO_DEVICE);
		kfree_skb(skb);
		ring->idx = (ring->idx + 1) % ring->entries;
	}

	dma_free_coherent(&priv->pdev->dev,
			  sizeof(*ring->desc) * ring->entries, ring->desc,
			  ring->dma);
	ring->desc = NULL;
}

static void _rtl92e_hard_data_xmit(struct sk_buff *skb, struct net_device *dev,
				   int rate)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int ret;
	struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb +
				    MAX_DEV_ADDR_SIZE);
	u8 queue_index = tcb_desc->queue_index;

	if ((priv->rtllib->rf_power_state == rf_off) || !priv->up ||
	     priv->bResetInProgress) {
		kfree_skb(skb);
		return;
	}

	if (queue_index == TXCMD_QUEUE)
		netdev_warn(dev, "%s(): queue index == TXCMD_QUEUE\n",
			    __func__);

	memcpy((unsigned char *)(skb->cb), &dev, sizeof(dev));
	skb_push(skb, priv->rtllib->tx_headroom);
	ret = _rtl92e_tx(dev, skb);

	if (queue_index != MGNT_QUEUE) {
		priv->rtllib->stats.tx_bytes += (skb->len -
						 priv->rtllib->tx_headroom);
		priv->rtllib->stats.tx_packets++;
	}

	if (ret != 0)
		kfree_skb(skb);
}

static int _rtl92e_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int ret;
	struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb +
				    MAX_DEV_ADDR_SIZE);
	u8 queue_index = tcb_desc->queue_index;

	if (queue_index != TXCMD_QUEUE) {
		if ((priv->rtllib->rf_power_state == rf_off) ||
		     !priv->up || priv->bResetInProgress) {
			kfree_skb(skb);
			return 0;
		}
	}

	memcpy((unsigned char *)(skb->cb), &dev, sizeof(dev));
	if (queue_index == TXCMD_QUEUE) {
		_rtl92e_tx_cmd(dev, skb);
		return 0;
	}

	tcb_desc->RATRIndex = 7;
	tcb_desc->bTxDisableRateFallBack = 1;
	tcb_desc->bTxUseDriverAssingedRate = 1;
	tcb_desc->bTxEnableFwCalcDur = 1;
	skb_push(skb, priv->rtllib->tx_headroom);
	ret = _rtl92e_tx(dev, skb);
	if (ret != 0)
		kfree_skb(skb);
	return ret;
}

static void _rtl92e_tx_isr(struct net_device *dev, int prio)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	struct rtl8192_tx_ring *ring = &priv->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb;

		if (prio != BEACON_QUEUE) {
			if (entry->OWN)
				return;
			ring->idx = (ring->idx + 1) % ring->entries;
		}

		skb = __skb_dequeue(&ring->queue);
		dma_unmap_single(&priv->pdev->dev, entry->TxBuffAddr,
				 skb->len, DMA_TO_DEVICE);

		kfree_skb(skb);
	}
	if (prio != BEACON_QUEUE)
		tasklet_schedule(&priv->irq_tx_tasklet);
}

static void _rtl92e_tx_cmd(struct net_device *dev, struct sk_buff *skb)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtl8192_tx_ring *ring;
	struct tx_desc_cmd *entry;
	unsigned int idx;
	struct cb_desc *tcb_desc;
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_th_lock, flags);
	ring = &priv->tx_ring[TXCMD_QUEUE];

	idx = (ring->idx + skb_queue_len(&ring->queue)) % ring->entries;
	entry = (struct tx_desc_cmd *)&ring->desc[idx];

	tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);

	priv->ops->tx_fill_cmd_descriptor(dev, entry, tcb_desc, skb);

	__skb_queue_tail(&ring->queue, skb);
	spin_unlock_irqrestore(&priv->irq_th_lock, flags);
}

static short _rtl92e_tx(struct net_device *dev, struct sk_buff *skb)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtl8192_tx_ring  *ring;
	unsigned long flags;
	struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb +
				    MAX_DEV_ADDR_SIZE);
	struct tx_desc *pdesc = NULL;
	struct rtllib_hdr_1addr *header = NULL;
	u16 fc = 0, type = 0;
	u8 *pda_addr = NULL;
	int   idx;
	u32 fwinfo_size = 0;

	if (priv->bdisable_nic) {
		netdev_warn(dev, "%s: Nic is disabled! Can't tx packet.\n",
			    __func__);
		return skb->len;
	}

	priv->rtllib->bAwakePktSent = true;

	fwinfo_size = sizeof(struct tx_fwinfo_8190pci);

	header = (struct rtllib_hdr_1addr *)(((u8 *)skb->data) + fwinfo_size);
	fc = le16_to_cpu(header->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	pda_addr = header->addr1;

	if (is_broadcast_ether_addr(pda_addr))
		priv->stats.txbytesbroadcast += skb->len - fwinfo_size;
	else if (is_multicast_ether_addr(pda_addr))
		priv->stats.txbytesmulticast += skb->len - fwinfo_size;
	else
		priv->stats.txbytesunicast += skb->len - fwinfo_size;

	spin_lock_irqsave(&priv->irq_th_lock, flags);
	ring = &priv->tx_ring[tcb_desc->queue_index];
	if (tcb_desc->queue_index != BEACON_QUEUE)
		idx = (ring->idx + skb_queue_len(&ring->queue)) % ring->entries;
	else
		idx = 0;

	pdesc = &ring->desc[idx];
	if ((pdesc->OWN == 1) && (tcb_desc->queue_index != BEACON_QUEUE)) {
		netdev_warn(dev,
			    "No more TX desc@%d, ring->idx = %d, idx = %d, skblen = 0x%x queuelen=%d",
			    tcb_desc->queue_index, ring->idx, idx, skb->len,
			    skb_queue_len(&ring->queue));
		spin_unlock_irqrestore(&priv->irq_th_lock, flags);
		return skb->len;
	}

	if (type == RTLLIB_FTYPE_DATA) {
		if (priv->rtllib->LedControlHandler)
			priv->rtllib->LedControlHandler(dev, LED_CTL_TX);
	}
	priv->ops->tx_fill_descriptor(dev, pdesc, tcb_desc, skb);
	__skb_queue_tail(&ring->queue, skb);
	pdesc->OWN = 1;
	spin_unlock_irqrestore(&priv->irq_th_lock, flags);
	netif_trans_update(dev);

	rtl92e_writew(dev, TPPoll, 0x01 << tcb_desc->queue_index);
	return 0;
}

static short _rtl92e_alloc_rx_ring(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rx_desc *entry = NULL;
	int i, rx_queue_idx;

	for (rx_queue_idx = 0; rx_queue_idx < MAX_RX_QUEUE; rx_queue_idx++) {
		priv->rx_ring[rx_queue_idx] = dma_alloc_coherent(&priv->pdev->dev,
								 sizeof(*priv->rx_ring[rx_queue_idx]) * priv->rxringcount,
								 &priv->rx_ring_dma[rx_queue_idx],
								 GFP_ATOMIC);
		if (!priv->rx_ring[rx_queue_idx] ||
		    (unsigned long)priv->rx_ring[rx_queue_idx] & 0xFF) {
			netdev_warn(dev, "Cannot allocate RX ring\n");
			return -ENOMEM;
		}

		priv->rx_idx[rx_queue_idx] = 0;

		for (i = 0; i < priv->rxringcount; i++) {
			struct sk_buff *skb = dev_alloc_skb(priv->rxbuffersize);
			dma_addr_t *mapping;

			entry = &priv->rx_ring[rx_queue_idx][i];
			if (!skb)
				return 0;
			skb->dev = dev;
			priv->rx_buf[rx_queue_idx][i] = skb;
			mapping = (dma_addr_t *)skb->cb;
			*mapping = dma_map_single(&priv->pdev->dev,
						  skb_tail_pointer_rsl(skb),
						  priv->rxbuffersize, DMA_FROM_DEVICE);
			if (dma_mapping_error(&priv->pdev->dev, *mapping)) {
				dev_kfree_skb_any(skb);
				return -1;
			}
			entry->BufferAddress = *mapping;

			entry->Length = priv->rxbuffersize;
			entry->OWN = 1;
		}

		if (entry)
			entry->EOR = 1;
	}
	return 0;
}

static int _rtl92e_alloc_tx_ring(struct net_device *dev, unsigned int prio,
				 unsigned int entries)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct tx_desc *ring;
	dma_addr_t dma;
	int i;

	ring = dma_alloc_coherent(&priv->pdev->dev, sizeof(*ring) * entries,
				  &dma, GFP_ATOMIC);
	if (!ring || (unsigned long)ring & 0xFF) {
		netdev_warn(dev, "Cannot allocate TX ring (prio = %d)\n", prio);
		return -ENOMEM;
	}

	priv->tx_ring[prio].desc = ring;
	priv->tx_ring[prio].dma = dma;
	priv->tx_ring[prio].idx = 0;
	priv->tx_ring[prio].entries = entries;
	skb_queue_head_init(&priv->tx_ring[prio].queue);

	for (i = 0; i < entries; i++)
		ring[i].NextDescAddress =
			(u32)dma + ((i + 1) % entries) *
			sizeof(*ring);

	return 0;
}

static short _rtl92e_pci_initdescring(struct net_device *dev)
{
	u32 ret;
	int i;
	struct r8192_priv *priv = rtllib_priv(dev);

	ret = _rtl92e_alloc_rx_ring(dev);
	if (ret)
		return ret;

	for (i = 0; i < MAX_TX_QUEUE_COUNT; i++) {
		ret = _rtl92e_alloc_tx_ring(dev, i, priv->txringcount);
		if (ret)
			goto err_free_rings;
	}

	return 0;

err_free_rings:
	_rtl92e_free_rx_ring(dev);
	for (i = 0; i < MAX_TX_QUEUE_COUNT; i++)
		if (priv->tx_ring[i].desc)
			_rtl92e_free_tx_ring(dev, i);
	return 1;
}

void rtl92e_reset_desc_ring(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int i, rx_queue_idx;
	unsigned long flags = 0;

	for (rx_queue_idx = 0; rx_queue_idx < MAX_RX_QUEUE; rx_queue_idx++) {
		if (priv->rx_ring[rx_queue_idx]) {
			struct rx_desc *entry = NULL;

			for (i = 0; i < priv->rxringcount; i++) {
				entry = &priv->rx_ring[rx_queue_idx][i];
				entry->OWN = 1;
			}
			priv->rx_idx[rx_queue_idx] = 0;
		}
	}

	spin_lock_irqsave(&priv->irq_th_lock, flags);
	for (i = 0; i < MAX_TX_QUEUE_COUNT; i++) {
		if (priv->tx_ring[i].desc) {
			struct rtl8192_tx_ring *ring = &priv->tx_ring[i];

			while (skb_queue_len(&ring->queue)) {
				struct tx_desc *entry = &ring->desc[ring->idx];
				struct sk_buff *skb =
						 __skb_dequeue(&ring->queue);

				dma_unmap_single(&priv->pdev->dev,
						 entry->TxBuffAddr, skb->len,
						 DMA_TO_DEVICE);
				kfree_skb(skb);
				ring->idx = (ring->idx + 1) % ring->entries;
			}
			ring->idx = 0;
		}
	}
	spin_unlock_irqrestore(&priv->irq_th_lock, flags);
}

void rtl92e_update_rx_pkt_timestamp(struct net_device *dev,
				    struct rtllib_rx_stats *stats)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (stats->bIsAMPDU && !stats->bFirstMPDU)
		stats->mac_time = priv->LastRxDescTSF;
	else
		priv->LastRxDescTSF = stats->mac_time;
}

long rtl92e_translate_to_dbm(struct r8192_priv *priv, u8 signal_strength_index)
{
	long	signal_power;

	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;

	return signal_power;
}

void rtl92e_update_rx_statistics(struct r8192_priv *priv,
				 struct rtllib_rx_stats *pprevious_stats)
{
	int weighting = 0;

	if (priv->stats.recv_signal_power == 0)
		priv->stats.recv_signal_power =
					 pprevious_stats->RecvSignalPower;

	if (pprevious_stats->RecvSignalPower > priv->stats.recv_signal_power)
		weighting = 5;
	else if (pprevious_stats->RecvSignalPower <
		 priv->stats.recv_signal_power)
		weighting = (-5);
	priv->stats.recv_signal_power = (priv->stats.recv_signal_power * 5 +
					pprevious_stats->RecvSignalPower +
					weighting) / 6;
}

u8 rtl92e_rx_db_to_percent(s8 antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
		return	0;
	else if (antpower >= 0)
		return	100;
	else
		return	100 + antpower;

}	/* QueryRxPwrPercentage */

u8 rtl92e_evm_db_to_percent(s8 value)
{
	s8 ret_val = clamp(-value, 0, 33) * 3;

	if (ret_val == 99)
		ret_val = 100;

	return ret_val;
}

void rtl92e_copy_mpdu_stats(struct rtllib_rx_stats *psrc_stats,
			    struct rtllib_rx_stats *ptarget_stats)
{
	ptarget_stats->bIsAMPDU = psrc_stats->bIsAMPDU;
	ptarget_stats->bFirstMPDU = psrc_stats->bFirstMPDU;
}

static void _rtl92e_rx_normal(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_hdr_1addr *rtllib_hdr = NULL;
	bool unicast_packet = false;
	bool bLedBlinking = true;
	u16 fc = 0, type = 0;
	u32 skb_len = 0;
	int rx_queue_idx = RX_MPDU_QUEUE;

	struct rtllib_rx_stats stats = {
		.signal = 0,
		.noise = (u8)-98,
		.rate = 0,
		.freq = RTLLIB_24GHZ_BAND,
	};
	unsigned int count = priv->rxringcount;

	stats.nic_type = NIC_8192E;

	while (count--) {
		struct rx_desc *pdesc = &priv->rx_ring[rx_queue_idx]
					[priv->rx_idx[rx_queue_idx]];
		struct sk_buff *skb = priv->rx_buf[rx_queue_idx]
				      [priv->rx_idx[rx_queue_idx]];
		struct sk_buff *new_skb;

		if (pdesc->OWN)
			return;
		if (!priv->ops->rx_query_status_descriptor(dev, &stats,
		pdesc, skb))
			goto done;
		new_skb = dev_alloc_skb(priv->rxbuffersize);
		/* if allocation of new skb failed - drop current packet
		 * and reuse skb
		 */
		if (unlikely(!new_skb))
			goto done;

		dma_unmap_single(&priv->pdev->dev, *((dma_addr_t *)skb->cb),
				 priv->rxbuffersize, DMA_FROM_DEVICE);

		skb_put(skb, pdesc->Length);
		skb_reserve(skb, stats.RxDrvInfoSize +
			stats.RxBufShift);
		skb_trim(skb, skb->len - 4/*sCrcLng*/);
		rtllib_hdr = (struct rtllib_hdr_1addr *)skb->data;
		if (!is_multicast_ether_addr(rtllib_hdr->addr1)) {
			/* unicast packet */
			unicast_packet = true;
		}
		fc = le16_to_cpu(rtllib_hdr->frame_ctl);
		type = WLAN_FC_GET_TYPE(fc);
		if (type == RTLLIB_FTYPE_MGMT)
			bLedBlinking = false;

		if (bLedBlinking)
			if (priv->rtllib->LedControlHandler)
				priv->rtllib->LedControlHandler(dev,
							LED_CTL_RX);

		if (stats.bCRC) {
			if (type != RTLLIB_FTYPE_MGMT)
				priv->stats.rxdatacrcerr++;
			else
				priv->stats.rxmgmtcrcerr++;
		}

		skb_len = skb->len;

		if (!rtllib_rx(priv->rtllib, skb, &stats)) {
			dev_kfree_skb_any(skb);
		} else {
			priv->stats.rxok++;
			if (unicast_packet)
				priv->stats.rxbytesunicast += skb_len;
		}

		skb = new_skb;
		skb->dev = dev;

		priv->rx_buf[rx_queue_idx][priv->rx_idx[rx_queue_idx]] =
								 skb;
		*((dma_addr_t *)skb->cb) = dma_map_single(&priv->pdev->dev,
							  skb_tail_pointer_rsl(skb),
							  priv->rxbuffersize, DMA_FROM_DEVICE);
		if (dma_mapping_error(&priv->pdev->dev, *((dma_addr_t *)skb->cb))) {
			dev_kfree_skb_any(skb);
			return;
		}
done:
		pdesc->BufferAddress = *((dma_addr_t *)skb->cb);
		pdesc->OWN = 1;
		pdesc->Length = priv->rxbuffersize;
		if (priv->rx_idx[rx_queue_idx] == priv->rxringcount - 1)
			pdesc->EOR = 1;
		priv->rx_idx[rx_queue_idx] = (priv->rx_idx[rx_queue_idx] + 1) %
					      priv->rxringcount;
	}
}

static void _rtl92e_tx_resume(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	struct sk_buff *skb;
	int queue_index;

	for (queue_index = BK_QUEUE;
	     queue_index < MAX_QUEUE_SIZE; queue_index++) {
		while ((!skb_queue_empty(&ieee->skb_waitQ[queue_index])) &&
		(priv->rtllib->check_nic_enough_desc(dev, queue_index) > 0)) {
			skb = skb_dequeue(&ieee->skb_waitQ[queue_index]);
			ieee->softmac_data_hard_start_xmit(skb, dev, 0);
		}
	}
}

static void _rtl92e_irq_tx_tasklet(struct tasklet_struct *t)
{
	struct r8192_priv *priv = from_tasklet(priv, t, irq_tx_tasklet);

	_rtl92e_tx_resume(priv->rtllib->dev);
}

static void _rtl92e_irq_rx_tasklet(struct tasklet_struct *t)
{
	struct r8192_priv *priv = from_tasklet(priv, t, irq_rx_tasklet);

	_rtl92e_rx_normal(priv->rtllib->dev);

	rtl92e_writel(priv->rtllib->dev, INTA_MASK,
		      rtl92e_readl(priv->rtllib->dev, INTA_MASK) | IMR_RDU);
}

/****************************************************************************
 * ---------------------------- NIC START/CLOSE STUFF---------------------------
 ****************************************************************************/
static void _rtl92e_cancel_deferred_work(struct r8192_priv *priv)
{
	cancel_delayed_work_sync(&priv->watch_dog_wq);
	cancel_delayed_work_sync(&priv->update_beacon_wq);
	cancel_delayed_work(&priv->rtllib->hw_sleep_wq);
	cancel_work_sync(&priv->reset_wq);
	cancel_work_sync(&priv->qos_activate);
}

static int _rtl92e_up(struct net_device *dev, bool is_silent_reset)
{
	if (_rtl92e_sta_up(dev, is_silent_reset) == -1)
		return -1;
	return 0;
}

static int _rtl92e_open(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int ret;

	mutex_lock(&priv->wx_mutex);
	ret = _rtl92e_try_up(dev);
	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_try_up(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->up == 1)
		return -1;
	return _rtl92e_up(dev, false);
}

static int _rtl92e_close(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int ret;

	if ((rtllib_act_scanning(priv->rtllib, false)) &&
		!(priv->rtllib->softmac_features & IEEE_SOFTMAC_SCAN)) {
		rtllib_stop_scan(priv->rtllib);
	}

	mutex_lock(&priv->wx_mutex);

	ret = _rtl92e_down(dev, true);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_down(struct net_device *dev, bool shutdownrf)
{
	if (_rtl92e_sta_down(dev, shutdownrf) == -1)
		return -1;

	return 0;
}

void rtl92e_commit(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->up == 0)
		return;
	rtllib_softmac_stop_protocol(priv->rtllib, 0, true);
	rtl92e_irq_disable(dev);
	priv->ops->stop_adapter(dev, true);
	_rtl92e_up(dev, false);
}

static void _rtl92e_restart(void *data)
{
	struct r8192_priv *priv = container_of_work_rsl(data, struct r8192_priv,
				  reset_wq);
	struct net_device *dev = priv->rtllib->dev;

	mutex_lock(&priv->wx_mutex);

	rtl92e_commit(dev);

	mutex_unlock(&priv->wx_mutex);
}

static void _rtl92e_set_multicast(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	short promisc;

	promisc = (dev->flags & IFF_PROMISC) ? 1 : 0;
	priv->promisc = promisc;
}

static int _rtl92e_set_mac_adr(struct net_device *dev, void *mac)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct sockaddr *addr = mac;

	mutex_lock(&priv->wx_mutex);

	eth_hw_addr_set(dev, addr->sa_data);

	schedule_work(&priv->reset_wq);
	mutex_unlock(&priv->wx_mutex);

	return 0;
}

static irqreturn_t _rtl92e_irq(int irq, void *netdev)
{
	struct net_device *dev = netdev;
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned long flags;
	u32 inta;
	u32 intb;

	intb = 0;

	if (priv->irq_enabled == 0)
		goto done;

	spin_lock_irqsave(&priv->irq_th_lock, flags);

	priv->ops->interrupt_recognized(dev, &inta, &intb);
	priv->stats.shints++;

	if (!inta) {
		spin_unlock_irqrestore(&priv->irq_th_lock, flags);
		goto done;
	}

	if (inta == 0xffff) {
		spin_unlock_irqrestore(&priv->irq_th_lock, flags);
		goto done;
	}

	priv->stats.ints++;

	if (!netif_running(dev)) {
		spin_unlock_irqrestore(&priv->irq_th_lock, flags);
		goto done;
	}

	if (inta & IMR_TBDOK)
		priv->stats.txbeaconokint++;

	if (inta & IMR_TBDER)
		priv->stats.txbeaconerr++;

	if (inta  & IMR_MGNTDOK) {
		priv->stats.txmanageokint++;
		_rtl92e_tx_isr(dev, MGNT_QUEUE);
		spin_unlock_irqrestore(&priv->irq_th_lock, flags);
		if (priv->rtllib->ack_tx_to_ieee) {
			if (_rtl92e_is_tx_queue_empty(dev)) {
				priv->rtllib->ack_tx_to_ieee = 0;
				rtllib_ps_tx_ack(priv->rtllib, 1);
			}
		}
		spin_lock_irqsave(&priv->irq_th_lock, flags);
	}

	if (inta & IMR_COMDOK) {
		priv->stats.txcmdpktokint++;
		_rtl92e_tx_isr(dev, TXCMD_QUEUE);
	}

	if (inta & IMR_HIGHDOK)
		_rtl92e_tx_isr(dev, HIGH_QUEUE);

	if (inta & IMR_ROK) {
		priv->stats.rxint++;
		priv->InterruptLog.nIMR_ROK++;
		tasklet_schedule(&priv->irq_rx_tasklet);
	}

	if (inta & IMR_BcnInt)
		tasklet_schedule(&priv->irq_prepare_beacon_tasklet);

	if (inta & IMR_RDU) {
		priv->stats.rxrdu++;
		rtl92e_writel(dev, INTA_MASK,
			      rtl92e_readl(dev, INTA_MASK) & ~IMR_RDU);
		tasklet_schedule(&priv->irq_rx_tasklet);
	}

	if (inta & IMR_RXFOVW) {
		priv->stats.rxoverflow++;
		tasklet_schedule(&priv->irq_rx_tasklet);
	}

	if (inta & IMR_TXFOVW)
		priv->stats.txoverflow++;

	if (inta & IMR_BKDOK) {
		priv->stats.txbkokint++;
		priv->rtllib->LinkDetectInfo.NumTxOkInPeriod++;
		_rtl92e_tx_isr(dev, BK_QUEUE);
	}

	if (inta & IMR_BEDOK) {
		priv->stats.txbeokint++;
		priv->rtllib->LinkDetectInfo.NumTxOkInPeriod++;
		_rtl92e_tx_isr(dev, BE_QUEUE);
	}

	if (inta & IMR_VIDOK) {
		priv->stats.txviokint++;
		priv->rtllib->LinkDetectInfo.NumTxOkInPeriod++;
		_rtl92e_tx_isr(dev, VI_QUEUE);
	}

	if (inta & IMR_VODOK) {
		priv->stats.txvookint++;
		priv->rtllib->LinkDetectInfo.NumTxOkInPeriod++;
		_rtl92e_tx_isr(dev, VO_QUEUE);
	}

	spin_unlock_irqrestore(&priv->irq_th_lock, flags);

done:

	return IRQ_HANDLED;
}

/****************************************************************************
 * ---------------------------- PCI_STUFF---------------------------
 ****************************************************************************/
static const struct net_device_ops rtl8192_netdev_ops = {
	.ndo_open = _rtl92e_open,
	.ndo_stop = _rtl92e_close,
	.ndo_tx_timeout = _rtl92e_tx_timeout,
	.ndo_set_rx_mode = _rtl92e_set_multicast,
	.ndo_set_mac_address = _rtl92e_set_mac_adr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_start_xmit = rtllib_xmit,
};

static int _rtl92e_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	unsigned long ioaddr = 0;
	struct net_device *dev = NULL;
	struct r8192_priv *priv = NULL;
	struct rtl819x_ops *ops = (struct rtl819x_ops *)(id->driver_data);
	unsigned long pmem_start, pmem_len, pmem_flags;
	int err = -ENOMEM;
	u8 revision_id;

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "Failed to enable PCI device");
		return -EIO;
	}

	pci_set_master(pdev);

	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32))) {
			dev_info(&pdev->dev,
				 "Unable to obtain 32bit DMA for consistent allocations\n");
			goto err_pci_disable;
		}
	}
	dev = alloc_rtllib(sizeof(struct r8192_priv));
	if (!dev)
		goto err_pci_disable;

	err = -ENODEV;

	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	priv = rtllib_priv(dev);
	priv->rtllib = (struct rtllib_device *)netdev_priv_rsl(dev);
	priv->pdev = pdev;
	priv->rtllib->pdev = pdev;
	if ((pdev->subsystem_vendor == PCI_VENDOR_ID_DLINK) &&
	    (pdev->subsystem_device == 0x3304))
		priv->rtllib->bSupportRemoteWakeUp = 1;
	else
		priv->rtllib->bSupportRemoteWakeUp = 0;

	pmem_start = pci_resource_start(pdev, 1);
	pmem_len = pci_resource_len(pdev, 1);
	pmem_flags = pci_resource_flags(pdev, 1);

	if (!(pmem_flags & IORESOURCE_MEM)) {
		netdev_err(dev, "region #1 not a MMIO resource, aborting");
		goto err_rel_rtllib;
	}

	dev_info(&pdev->dev, "Memory mapped space start: 0x%08lx\n",
		 pmem_start);
	if (!request_mem_region(pmem_start, pmem_len, DRV_NAME)) {
		netdev_err(dev, "request_mem_region failed!");
		goto err_rel_rtllib;
	}

	ioaddr = (unsigned long)ioremap(pmem_start, pmem_len);
	if (ioaddr == (unsigned long)NULL) {
		netdev_err(dev, "ioremap failed!");
		goto err_rel_mem;
	}

	dev->mem_start = ioaddr;
	dev->mem_end = ioaddr + pci_resource_len(pdev, 0);

	pci_read_config_byte(pdev, 0x08, &revision_id);
	/* If the revisionid is 0x10, the device uses rtl8192se. */
	if (pdev->device == 0x8192 && revision_id == 0x10)
		goto err_unmap;

	priv->ops = ops;

	if (!rtl92e_check_adapter(pdev, dev))
		goto err_unmap;

	dev->irq = pdev->irq;
	priv->irq = 0;

	dev->netdev_ops = &rtl8192_netdev_ops;

	dev->wireless_handlers = &r8192_wx_handlers_def;
	dev->ethtool_ops = &rtl819x_ethtool_ops;

	dev->type = ARPHRD_ETHER;
	dev->watchdog_timeo = HZ * 3;

	if (dev_alloc_name(dev, ifname) < 0)
		dev_alloc_name(dev, ifname);

	if (_rtl92e_init(dev) != 0) {
		netdev_warn(dev, "Initialization failed");
		goto err_free_irq;
	}

	netif_carrier_off(dev);
	netif_stop_queue(dev);

	if (register_netdev(dev))
		goto err_free_irq;

	if (priv->polling_timer_on == 0)
		rtl92e_check_rfctrl_gpio_timer(&priv->gpio_polling_timer);

	return 0;

err_free_irq:
	free_irq(dev->irq, dev);
	priv->irq = 0;
err_unmap:
	iounmap((void __iomem *)ioaddr);
err_rel_mem:
	release_mem_region(pmem_start, pmem_len);
err_rel_rtllib:
	free_rtllib(dev);
err_pci_disable:
	pci_disable_device(pdev);
	return err;
}

static void _rtl92e_pci_disconnect(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct r8192_priv *priv;
	u32 i;

	if (dev) {
		unregister_netdev(dev);

		priv = rtllib_priv(dev);

		del_timer_sync(&priv->gpio_polling_timer);
		cancel_delayed_work_sync(&priv->gpio_change_rf_wq);
		priv->polling_timer_on = 0;
		_rtl92e_down(dev, true);
		rtl92e_dm_deinit(dev);
		vfree(priv->pFirmware);
		priv->pFirmware = NULL;
		_rtl92e_free_rx_ring(dev);
		for (i = 0; i < MAX_TX_QUEUE_COUNT; i++)
			_rtl92e_free_tx_ring(dev, i);

		if (priv->irq) {
			dev_info(&pdev->dev, "Freeing irq %d\n", dev->irq);
			free_irq(dev->irq, dev);
			priv->irq = 0;
		}

		if (dev->mem_start != 0) {
			iounmap((void __iomem *)dev->mem_start);
			release_mem_region(pci_resource_start(pdev, 1),
					pci_resource_len(pdev, 1));
		}

		free_rtllib(dev);
	}

	pci_disable_device(pdev);
}

bool rtl92e_enable_nic(struct net_device *dev)
{
	bool init_status = true;
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					(&priv->rtllib->PowerSaveControl);

	if (!priv->up) {
		netdev_warn(dev, "%s(): Driver is already down!\n", __func__);
		priv->bdisable_nic = false;
		return false;
	}

	priv->bfirst_init = true;
	init_status = priv->ops->initialize_adapter(dev);
	if (!init_status) {
		netdev_warn(dev, "%s(): Initialization failed!\n", __func__);
		priv->bdisable_nic = false;
		return false;
	}
	RT_CLEAR_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC);
	priv->bfirst_init = false;

	rtl92e_irq_enable(dev);
	priv->bdisable_nic = false;
	return init_status;
}

bool rtl92e_disable_nic(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 tmp_state = 0;

	priv->bdisable_nic = true;
	tmp_state = priv->rtllib->state;
	rtllib_softmac_stop_protocol(priv->rtllib, 0, false);
	priv->rtllib->state = tmp_state;
	_rtl92e_cancel_deferred_work(priv);
	rtl92e_irq_disable(dev);

	priv->ops->stop_adapter(dev, false);
	return true;
}

module_pci_driver(rtl8192_pci_driver);

void rtl92e_check_rfctrl_gpio_timer(struct timer_list *t)
{
	struct r8192_priv *priv = from_timer(priv, t, gpio_polling_timer);

	priv->polling_timer_on = 1;

	schedule_delayed_work(&priv->gpio_change_rf_wq, 0);

	mod_timer(&priv->gpio_polling_timer, jiffies +
		  msecs_to_jiffies(RTLLIB_WATCH_DOG_TIME));
}

/***************************************************************************
 * ------------------- module init / exit stubs ----------------
 ***************************************************************************/
MODULE_DESCRIPTION("Linux driver for Realtek RTL819x WiFi cards");
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(RTL8192E_BOOT_IMG_FW);
MODULE_FIRMWARE(RTL8192E_MAIN_IMG_FW);
MODULE_FIRMWARE(RTL8192E_DATA_IMG_FW);

module_param(ifname, charp, 0644);
module_param(hwwep, int, 0644);

MODULE_PARM_DESC(ifname, " Net interface name, wlan%d=default");
MODULE_PARM_DESC(hwwep, " Try to use hardware WEP support(default use hw. set 0 to use software security)");
