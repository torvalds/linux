/*
 * Copyright 2008 Pavel Machek <pavel@ucw.cz>
 *
 * Distribute under GPLv2.
 *
 * The original driver was written by:
 *     Jeff Lee <YY_Lee@issc.com.tw>
 *
 * and was adapted to the 2.6 kernel by:
 *     Costantino Leandro (Rxart Desktop) <le_costantino@pixartargentina.com.ar>
 */
#include <net/mac80211.h>
#include <linux/usb.h>
#include <linux/module.h>

#include "core.h"
#include "mds_f.h"
#include "mto.h"
#include "wbhal.h"
#include "wb35reg_f.h"
#include "wb35tx_f.h"
#include "wb35rx_f.h"

MODULE_DESCRIPTION("IS89C35 802.11bg WLAN USB Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

static const struct usb_device_id wb35_table[] = {
	{ USB_DEVICE(0x0416, 0x0035) },
	{ USB_DEVICE(0x18E8, 0x6201) },
	{ USB_DEVICE(0x18E8, 0x6206) },
	{ USB_DEVICE(0x18E8, 0x6217) },
	{ USB_DEVICE(0x18E8, 0x6230) },
	{ USB_DEVICE(0x18E8, 0x6233) },
	{ USB_DEVICE(0x1131, 0x2035) },
	{ 0, }
};

MODULE_DEVICE_TABLE(usb, wb35_table);

static struct ieee80211_rate wbsoft_rates[] = {
	{ .bitrate = 10, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
};

static struct ieee80211_channel wbsoft_channels[] = {
	{ .center_freq = 2412 },
};

static struct ieee80211_supported_band wbsoft_band_2GHz = {
	.channels	= wbsoft_channels,
	.n_channels	= ARRAY_SIZE(wbsoft_channels),
	.bitrates	= wbsoft_rates,
	.n_bitrates	= ARRAY_SIZE(wbsoft_rates),
};

static void hal_set_beacon_period(struct hw_data *pHwData, u16 beacon_period)
{
	u32 tmp;

	if (pHwData->SurpriseRemove)
		return;

	pHwData->BeaconPeriod = beacon_period;
	tmp = pHwData->BeaconPeriod << 16;
	tmp |= pHwData->ProbeDelay;
	Wb35Reg_Write(pHwData, 0x0848, tmp);
}

static int wbsoft_add_interface(struct ieee80211_hw *dev,
				struct ieee80211_vif *vif)
{
	struct wbsoft_priv *priv = dev->priv;

	hal_set_beacon_period(&priv->sHwData, vif->bss_conf.beacon_int);

	return 0;
}

static void wbsoft_remove_interface(struct ieee80211_hw *dev,
				    struct ieee80211_vif *vif)
{
	printk("wbsoft_remove interface called\n");
}

static void wbsoft_stop(struct ieee80211_hw *hw)
{
	printk(KERN_INFO "%s called\n", __func__);
}

static int wbsoft_get_stats(struct ieee80211_hw *hw,
			    struct ieee80211_low_level_stats *stats)
{
	printk(KERN_INFO "%s called\n", __func__);
	return 0;
}

static u64 wbsoft_prepare_multicast(struct ieee80211_hw *hw,
				    struct netdev_hw_addr_list *mc_list)
{
	return netdev_hw_addr_list_count(mc_list);
}

static void wbsoft_configure_filter(struct ieee80211_hw *dev,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	unsigned int new_flags;

	new_flags = 0;

	if (*total_flags & FIF_PROMISC_IN_BSS)
		new_flags |= FIF_PROMISC_IN_BSS;
	else if ((*total_flags & FIF_ALLMULTI) || (multicast > 32))
		new_flags |= FIF_ALLMULTI;

	dev->flags &= ~IEEE80211_HW_RX_INCLUDES_FCS;

	*total_flags = new_flags;
}

static void wbsoft_tx(struct ieee80211_hw *dev,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct wbsoft_priv *priv = dev->priv;

	if (priv->sMlmeFrame.IsInUsed != PACKET_FREE_TO_USE) {
		priv->sMlmeFrame.wNumTxMMPDUDiscarded++;
		kfree_skb(skb);
		return;
	}

	priv->sMlmeFrame.IsInUsed = PACKET_COME_FROM_MLME;

	priv->sMlmeFrame.pMMPDU		= skb->data;
	priv->sMlmeFrame.DataType	= FRAME_TYPE_802_11_MANAGEMENT;
	priv->sMlmeFrame.len		= skb->len;
	priv->sMlmeFrame.wNumTxMMPDU++;

	/*
	 * H/W will enter power save by set the register. S/W don't send null
	 * frame with PWRMgt bit enbled to enter power save now.
	 */

	Mds_Tx(priv);
}

static int wbsoft_start(struct ieee80211_hw *dev)
{
	struct wbsoft_priv *priv = dev->priv;

	priv->enabled = true;

	return 0;
}

static void hal_set_radio_mode(struct hw_data *pHwData, unsigned char radio_off)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove)
		return;

	if (radio_off) {	/* disable Baseband receive off */
		pHwData->CurrentRadioSw = 1;	/* off */
		reg->M24_MacControl &= 0xffffffbf;
	} else {
		pHwData->CurrentRadioSw = 0;	/* on */
		reg->M24_MacControl |= 0x00000040;
	}
	Wb35Reg_Write(pHwData, 0x0824, reg->M24_MacControl);
}

static void hal_set_current_channel_ex(struct hw_data *pHwData, struct chan_info channel)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove)
		return;

	printk("Going to channel: %d/%d\n", channel.band, channel.ChanNo);

	RFSynthesizer_SwitchingChannel(pHwData, channel); /* Switch channel */
	pHwData->Channel = channel.ChanNo;
	pHwData->band = channel.band;
	pr_debug("Set channel is %d, band =%d\n", pHwData->Channel, pHwData->band);
	reg->M28_MacControl &= ~0xff;	/* Clean channel information field */
	reg->M28_MacControl |= channel.ChanNo;
	Wb35Reg_WriteWithCallbackValue(pHwData, 0x0828, reg->M28_MacControl,
				       (s8 *) &channel,
				       sizeof(struct chan_info));
}

static void hal_set_current_channel(struct hw_data *pHwData, struct chan_info channel)
{
	hal_set_current_channel_ex(pHwData, channel);
}

static void hal_set_accept_broadcast(struct hw_data *pHwData, u8 enable)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove)
		return;

	reg->M00_MacControl &= ~0x02000000;	/* The HW value */

	if (enable)
		reg->M00_MacControl |= 0x02000000;	/* The HW value */

	Wb35Reg_Write(pHwData, 0x0800, reg->M00_MacControl);
}

/* For wep key error detection, we need to accept broadcast packets to be received temporary. */
static void hal_set_accept_promiscuous(struct hw_data *pHwData, u8 enable)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove)
		return;

	if (enable) {
		reg->M00_MacControl |= 0x00400000;
		Wb35Reg_Write(pHwData, 0x0800, reg->M00_MacControl);
	} else {
		reg->M00_MacControl &= ~0x00400000;
		Wb35Reg_Write(pHwData, 0x0800, reg->M00_MacControl);
	}
}

static void hal_set_accept_multicast(struct hw_data *pHwData, u8 enable)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove)
		return;

	reg->M00_MacControl &= ~0x01000000;	/* The HW value */
	if (enable)
		reg->M00_MacControl |= 0x01000000;	/* The HW value */
	Wb35Reg_Write(pHwData, 0x0800, reg->M00_MacControl);
}

static void hal_set_accept_beacon(struct hw_data *pHwData, u8 enable)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove)
		return;

	if (!enable)	/* Due to SME and MLME are not suitable for 35 */
		return;

	reg->M00_MacControl &= ~0x04000000;	/* The HW value */
	if (enable)
		reg->M00_MacControl |= 0x04000000;	/* The HW value */

	Wb35Reg_Write(pHwData, 0x0800, reg->M00_MacControl);
}

static int wbsoft_config(struct ieee80211_hw *dev, u32 changed)
{
	struct wbsoft_priv *priv = dev->priv;
	struct chan_info ch;

	printk("wbsoft_config called\n");

	/* Should use channel_num, or something, as that is already pre-translated */
	ch.band = 1;
	ch.ChanNo = 1;

	hal_set_current_channel(&priv->sHwData, ch);
	hal_set_accept_broadcast(&priv->sHwData, 1);
	hal_set_accept_promiscuous(&priv->sHwData, 1);
	hal_set_accept_multicast(&priv->sHwData, 1);
	hal_set_accept_beacon(&priv->sHwData, 1);
	hal_set_radio_mode(&priv->sHwData, 0);

	return 0;
}

static u64 wbsoft_get_tsf(struct ieee80211_hw *dev, struct ieee80211_vif *vif)
{
	printk("wbsoft_get_tsf called\n");
	return 0;
}

static const struct ieee80211_ops wbsoft_ops = {
	.tx			= wbsoft_tx,
	.start			= wbsoft_start,
	.stop			= wbsoft_stop,
	.add_interface		= wbsoft_add_interface,
	.remove_interface	= wbsoft_remove_interface,
	.config			= wbsoft_config,
	.prepare_multicast	= wbsoft_prepare_multicast,
	.configure_filter	= wbsoft_configure_filter,
	.get_stats		= wbsoft_get_stats,
	.get_tsf		= wbsoft_get_tsf,
};

static void hal_set_ethernet_address(struct hw_data *pHwData, u8 *current_address)
{
	u32 ltmp[2];

	if (pHwData->SurpriseRemove)
		return;

	memcpy(pHwData->CurrentMacAddress, current_address, ETH_ALEN);

	ltmp[0] = cpu_to_le32(*(u32 *) pHwData->CurrentMacAddress);
	ltmp[1] = cpu_to_le32(*(u32 *) (pHwData->CurrentMacAddress + 4)) & 0xffff;

	Wb35Reg_BurstWrite(pHwData, 0x03e8, ltmp, 2, AUTO_INCREMENT);
}

static void hal_get_permanent_address(struct hw_data *pHwData, u8 *pethernet_address)
{
	if (pHwData->SurpriseRemove)
		return;

	memcpy(pethernet_address, pHwData->PermanentMacAddress, 6);
}

static void hal_stop(struct hw_data *pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;

	pHwData->Wb35Rx.rx_halt = 1;
	Wb35Rx_stop(pHwData);

	pHwData->Wb35Tx.tx_halt = 1;
	Wb35Tx_stop(pHwData);

	reg->D00_DmaControl &= ~0xc0000000;	/* Tx Off, Rx Off */
	Wb35Reg_Write(pHwData, 0x0400, reg->D00_DmaControl);
}

static unsigned char hal_idle(struct hw_data *pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (!pHwData->SurpriseRemove && reg->EP0vm_state != VM_STOP)
		return false;

	return true;
}

u8 hal_get_antenna_number(struct hw_data *pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;

	if ((reg->BB2C & BIT(11)) == 0)
		return 0;
	else
		return 1;
}

/* 0 : radio on; 1: radio off */
static u8 hal_get_hw_radio_off(struct hw_data *pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove)
		return 1;

	/* read the bit16 of register U1B0 */
	Wb35Reg_Read(pHwData, 0x3b0, &reg->U1B0);
	if ((reg->U1B0 & 0x00010000)) {
		pHwData->CurrentRadioHw = 1;
		return 1;
	} else {
		pHwData->CurrentRadioHw = 0;
		return 0;
	}
}

static u8 LED_GRAY[20] = {
	0, 3, 4, 6, 8, 10, 11, 12, 13, 14, 15, 14, 13, 12, 11, 10, 8, 6, 4, 2
};

static u8 LED_GRAY2[30] = {
	7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 15, 14, 13, 12, 11, 10, 9, 8
};

static void hal_led_control(unsigned long data)
{
	struct wbsoft_priv *adapter = (struct wbsoft_priv *)data;
	struct hw_data *pHwData = &adapter->sHwData;
	struct wb35_reg *reg = &pHwData->reg;
	u32 LEDSet = (pHwData->SoftwareSet & HAL_LED_SET_MASK) >> HAL_LED_SET_SHIFT;
	u32 TimeInterval = 500, ltmp, ltmp2;
	ltmp = 0;

	if (pHwData->SurpriseRemove)
		return;

	if (pHwData->LED_control) {
		ltmp2 = pHwData->LED_control & 0xff;
		if (ltmp2 == 5)	{ /* 5 is WPS mode */
			TimeInterval = 100;
			ltmp2 = (pHwData->LED_control >> 8) & 0xff;
			switch (ltmp2) {
			case 1:	/* [0.2 On][0.1 Off]... */
				pHwData->LED_Blinking %= 3;
				ltmp = 0x1010;	/* Led 1 & 0 Green and Red */
				if (pHwData->LED_Blinking == 2)	/* Turn off */
					ltmp = 0;
				break;
			case 2:	/* [0.1 On][0.1 Off]... */
				pHwData->LED_Blinking %= 2;
				ltmp = 0x0010;	/* Led 0 red color */
				if (pHwData->LED_Blinking) /* Turn off */
					ltmp = 0;
				break;
			case 3:	/* [0.1 On][0.1 Off][0.1 On][0.1 Off][0.1 On][0.1 Off][0.1 On][0.1 Off][0.1 On][0.1 Off][0.5 Off]... */
				pHwData->LED_Blinking %= 15;
				ltmp = 0x0010;	/* Led 0 red color */
				if ((pHwData->LED_Blinking >= 9) || (pHwData->LED_Blinking % 2)) /* Turn off 0.6 sec */
					ltmp = 0;
				break;
			case 4:	/* [300 On][ off ] */
				ltmp = 0x1000;	/* Led 1 Green color */
				if (pHwData->LED_Blinking >= 3000)
					ltmp = 0; /* led maybe on after 300sec * 32bit counter overlap. */
				break;
			}
			pHwData->LED_Blinking++;

			reg->U1BC_LEDConfigure = ltmp;
			if (LEDSet != 7) { /* Only 111 mode has 2 LEDs on PCB. */
				reg->U1BC_LEDConfigure |= (ltmp & 0xff) << 8; /* Copy LED result to each LED control register */
				reg->U1BC_LEDConfigure |= (ltmp & 0xff00) >> 8;
			}
			Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);
		}
	} else if (pHwData->CurrentRadioSw || pHwData->CurrentRadioHw) { /* If radio off */
		if (reg->U1BC_LEDConfigure & 0x1010) {
			reg->U1BC_LEDConfigure &= ~0x1010;
			Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);
		}
	} else {
		switch (LEDSet) {
		case 4:	/* [100] Only 1 Led be placed on PCB and use pin 21 of IC. Use LED_0 for showing */
			if (!pHwData->LED_LinkOn) { /* Blink only if not Link On */
				/* Blinking if scanning is on progress */
				if (pHwData->LED_Scanning) {
					if (pHwData->LED_Blinking == 0) {
						reg->U1BC_LEDConfigure |= 0x10;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 On */
						pHwData->LED_Blinking = 1;
						TimeInterval = 300;
					} else {
						reg->U1BC_LEDConfigure &= ~0x10;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 Off */
						pHwData->LED_Blinking = 0;
						TimeInterval = 300;
					}
				} else {
					/* Turn Off LED_0 */
					if (reg->U1BC_LEDConfigure & 0x10) {
						reg->U1BC_LEDConfigure &= ~0x10;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 Off */
					}
				}
			} else {
				/* Turn On LED_0 */
				if ((reg->U1BC_LEDConfigure & 0x10) == 0) {
					reg->U1BC_LEDConfigure |= 0x10;
					Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 Off */
				}
			}
			break;
		case 6:	/* [110] Only 1 Led be placed on PCB and use pin 21 of IC. Use LED_0 for showing */
			if (!pHwData->LED_LinkOn) { /* Blink only if not Link On */
				/* Blinking if scanning is on progress */
				if (pHwData->LED_Scanning) {
					if (pHwData->LED_Blinking == 0) {
						reg->U1BC_LEDConfigure &= ~0xf;
						reg->U1BC_LEDConfigure |= 0x10;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 On */
						pHwData->LED_Blinking = 1;
						TimeInterval = 300;
					} else {
						reg->U1BC_LEDConfigure &= ~0x1f;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 Off */
						pHwData->LED_Blinking = 0;
						TimeInterval = 300;
					}
				} else {
					/* Gray blinking if in disconnect state and not scanning */
					ltmp = reg->U1BC_LEDConfigure;
					reg->U1BC_LEDConfigure &= ~0x1f;
					if (LED_GRAY2[(pHwData->LED_Blinking % 30)]) {
						reg->U1BC_LEDConfigure |= 0x10;
						reg->U1BC_LEDConfigure |=
						    LED_GRAY2[(pHwData->LED_Blinking % 30)];
					}
					pHwData->LED_Blinking++;
					if (reg->U1BC_LEDConfigure != ltmp)
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 Off */
					TimeInterval = 100;
				}
			} else {
				/* Turn On LED_0 */
				if ((reg->U1BC_LEDConfigure & 0x10) == 0) {
					reg->U1BC_LEDConfigure |= 0x10;
					Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_0 Off */
				}
			}
			break;
		case 5:	/* [101] Only 1 Led be placed on PCB and use LED_1 for showing */
			if (!pHwData->LED_LinkOn) { /* Blink only if not Link On */
				/* Blinking if scanning is on progress */
				if (pHwData->LED_Scanning) {
					if (pHwData->LED_Blinking == 0) {
						reg->U1BC_LEDConfigure |= 0x1000;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_1 On */
						pHwData->LED_Blinking = 1;
						TimeInterval = 300;
					} else {
						reg->U1BC_LEDConfigure &= ~0x1000;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_1 Off */
						pHwData->LED_Blinking = 0;
						TimeInterval = 300;
					}
				} else {
					/* Turn Off LED_1 */
					if (reg->U1BC_LEDConfigure & 0x1000) {
						reg->U1BC_LEDConfigure &= ~0x1000;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_1 Off */
					}
				}
			} else {
				/* Is transmitting/receiving ?? */
				if ((adapter->RxByteCount !=
				     pHwData->RxByteCountLast)
				    || (adapter->TxByteCount !=
					pHwData->TxByteCountLast)) {
					if ((reg->U1BC_LEDConfigure & 0x3000) !=
					    0x3000) {
						reg->U1BC_LEDConfigure |= 0x3000;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_1 On */
					}
					/* Update variable */
					pHwData->RxByteCountLast =
					    adapter->RxByteCount;
					pHwData->TxByteCountLast =
					    adapter->TxByteCount;
					TimeInterval = 200;
				} else {
					/* Turn On LED_1 and blinking if transmitting/receiving */
					if ((reg->U1BC_LEDConfigure & 0x3000) !=
					    0x1000) {
						reg->U1BC_LEDConfigure &=
						    ~0x3000;
						reg->U1BC_LEDConfigure |=
						    0x1000;
						Wb35Reg_Write(pHwData, 0x03bc, reg->U1BC_LEDConfigure);	/* LED_1 On */
					}
				}
			}
			break;
		default: /* Default setting. 2 LED be placed on PCB. LED_0: Link On LED_1 Active */
			if ((reg->U1BC_LEDConfigure & 0x3000) != 0x3000) {
				reg->U1BC_LEDConfigure |= 0x3000;	/* LED_1 is always on and event enable */
				Wb35Reg_Write(pHwData, 0x03bc,
					      reg->U1BC_LEDConfigure);
			}

			if (pHwData->LED_Blinking) {
				/* Gray blinking */
				reg->U1BC_LEDConfigure &= ~0x0f;
				reg->U1BC_LEDConfigure |= 0x10;
				reg->U1BC_LEDConfigure |=
				    LED_GRAY[(pHwData->LED_Blinking - 1) % 20];
				Wb35Reg_Write(pHwData, 0x03bc,
					      reg->U1BC_LEDConfigure);

				pHwData->LED_Blinking += 2;
				if (pHwData->LED_Blinking < 40)
					TimeInterval = 100;
				else {
					pHwData->LED_Blinking = 0; /* Stop blinking */
					reg->U1BC_LEDConfigure &= ~0x0f;
					Wb35Reg_Write(pHwData, 0x03bc,
						      reg->U1BC_LEDConfigure);
				}
				break;
			}

			if (pHwData->LED_LinkOn) {
				if (!(reg->U1BC_LEDConfigure & 0x10)) { /* Check the LED_0 */
					/* Try to turn ON LED_0 after gray blinking */
					reg->U1BC_LEDConfigure |= 0x10;
					pHwData->LED_Blinking = 1; /* Start blinking */
					TimeInterval = 50;
				}
			} else {
				if (reg->U1BC_LEDConfigure & 0x10) { /* Check the LED_0 */
					reg->U1BC_LEDConfigure &= ~0x10;
					Wb35Reg_Write(pHwData, 0x03bc,
						      reg->U1BC_LEDConfigure);
				}
			}
			break;
		}
	}

	pHwData->time_count += TimeInterval;
	Wb35Tx_CurrentTime(adapter, pHwData->time_count);
	pHwData->LEDTimer.expires = jiffies + msecs_to_jiffies(TimeInterval);
	add_timer(&pHwData->LEDTimer);
}

static int hal_init_hardware(struct ieee80211_hw *hw)
{
	struct wbsoft_priv *priv = hw->priv;
	struct hw_data *pHwData = &priv->sHwData;
	u16 SoftwareSet;

	pHwData->MaxReceiveLifeTime = DEFAULT_MSDU_LIFE_TIME;
	pHwData->FragmentThreshold = DEFAULT_FRAGMENT_THRESHOLD;

	if (!Wb35Reg_initial(pHwData))
		goto error_reg_destroy;

	if (!Wb35Tx_initial(pHwData))
		goto error_tx_destroy;

	if (!Wb35Rx_initial(pHwData))
		goto error_rx_destroy;

	init_timer(&pHwData->LEDTimer);
	pHwData->LEDTimer.function = hal_led_control;
	pHwData->LEDTimer.data = (unsigned long)priv;
	pHwData->LEDTimer.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&pHwData->LEDTimer);

	SoftwareSet = hal_software_set(pHwData);

	Wb35Rx_start(hw);
	Wb35Tx_EP2VM_start(priv);

	return 0;

error_rx_destroy:
	Wb35Rx_destroy(pHwData);
error_tx_destroy:
	Wb35Tx_destroy(pHwData);
error_reg_destroy:
	Wb35Reg_destroy(pHwData);

	pHwData->SurpriseRemove = 1;
	return -EINVAL;
}

static int wb35_hw_init(struct ieee80211_hw *hw)
{
	struct wbsoft_priv *priv = hw->priv;
	struct hw_data *pHwData = &priv->sHwData;
	u8 EEPROM_region;
	u8 HwRadioOff;
	u8 *pMacAddr2;
	u8 *pMacAddr;
	int err;

	pHwData->phy_type = RF_DECIDE_BY_INF;

	priv->Mds.TxRTSThreshold		= DEFAULT_RTSThreshold;
	priv->Mds.TxFragmentThreshold		= DEFAULT_FRAGMENT_THRESHOLD;

	priv->sLocalPara.region_INF		= REGION_AUTO;
	priv->sLocalPara.TxRateMode		= RATE_AUTO;
	priv->sLocalPara.bMacOperationMode	= MODE_802_11_BG;
	priv->sLocalPara.MTUsize		= MAX_ETHERNET_PACKET_SIZE;
	priv->sLocalPara.bPreambleMode		= AUTO_MODE;
	priv->sLocalPara.bWepKeyError		= false;
	priv->sLocalPara.bToSelfPacketReceived	= false;
	priv->sLocalPara.WepKeyDetectTimerCount	= 2 * 100; /* 2 seconds */

	priv->sLocalPara.RadioOffStatus.boSwRadioOff = false;

	err = hal_init_hardware(hw);
	if (err)
		goto error;

	EEPROM_region = hal_get_region_from_EEPROM(pHwData);
	if (EEPROM_region != REGION_AUTO)
		priv->sLocalPara.region = EEPROM_region;
	else {
		if (priv->sLocalPara.region_INF != REGION_AUTO)
			priv->sLocalPara.region = priv->sLocalPara.region_INF;
		else
			priv->sLocalPara.region = REGION_USA;	/* default setting */
	}

	Mds_initial(priv);

	/*
	 * If no user-defined address in the registry, use the address
	 * "burned" on the NIC instead.
	 */
	pMacAddr = priv->sLocalPara.ThisMacAddress;
	pMacAddr2 = priv->sLocalPara.PermanentAddress;

	/* Reading ethernet address from EEPROM */
	hal_get_permanent_address(pHwData, priv->sLocalPara.PermanentAddress);
	if (memcmp(pMacAddr, "\x00\x00\x00\x00\x00\x00", MAC_ADDR_LENGTH) == 0)
		memcpy(pMacAddr, pMacAddr2, MAC_ADDR_LENGTH);
	else {
		/* Set the user define MAC address */
		hal_set_ethernet_address(pHwData,
					 priv->sLocalPara.ThisMacAddress);
	}

	priv->sLocalPara.bAntennaNo = hal_get_antenna_number(pHwData);
	pr_debug("Driver init, antenna no = %d\n", priv->sLocalPara.bAntennaNo);
	hal_get_hw_radio_off(pHwData);

	/* Waiting for HAL setting OK */
	while (!hal_idle(pHwData))
		msleep(10);

	MTO_Init(priv);

	HwRadioOff = hal_get_hw_radio_off(pHwData);
	priv->sLocalPara.RadioOffStatus.boHwRadioOff = !!HwRadioOff;

	hal_set_radio_mode(pHwData,
			   (unsigned char)(priv->sLocalPara.RadioOffStatus.
					   boSwRadioOff
					   || priv->sLocalPara.RadioOffStatus.
					   boHwRadioOff));

	/* Notify hal that the driver is ready now. */
	hal_driver_init_OK(pHwData) = 1;

error:
	return err;
}

static int wb35_probe(struct usb_interface *intf,
		      const struct usb_device_id *id_table)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface *interface;
	struct ieee80211_hw *dev;
	struct wbsoft_priv *priv;
	int err;
	u32 ltmp;

	usb_get_dev(udev);

	/* Check the device if it already be opened */
	err = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			     0x01,
			     USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			     0x0, 0x400, &ltmp, 4, HZ * 100);
	if (err < 0)
		goto error;

	/* Is already initialized? */
	ltmp = cpu_to_le32(ltmp);
	if (ltmp) {
		err = -EBUSY;
		goto error;
	}

	dev = ieee80211_alloc_hw(sizeof(*priv), &wbsoft_ops);
	if (!dev) {
		err = -ENOMEM;
		goto error;
	}

	priv = dev->priv;

	priv->sHwData.udev = udev;

	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;

	if (endpoint[2].wMaxPacketSize == 512)
		printk("[w35und] Working on USB 2.0\n");

	err = wb35_hw_init(dev);
	if (err)
		goto error_free_hw;

	SET_IEEE80211_DEV(dev, &udev->dev);
	{
		struct hw_data *pHwData = &priv->sHwData;
		unsigned char dev_addr[MAX_ADDR_LEN];
		hal_get_permanent_address(pHwData, dev_addr);
		SET_IEEE80211_PERM_ADDR(dev, dev_addr);
	}

	dev->extra_tx_headroom = 12;	/* FIXME */
	dev->flags = IEEE80211_HW_SIGNAL_UNSPEC;
	dev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	dev->channel_change_time = 1000;
	dev->max_signal = 100;
	dev->queues = 1;

	dev->wiphy->bands[IEEE80211_BAND_2GHZ] = &wbsoft_band_2GHz;

	err = ieee80211_register_hw(dev);
	if (err)
		goto error_free_hw;

	usb_set_intfdata(intf, dev);

	return 0;

error_free_hw:
	ieee80211_free_hw(dev);
error:
	usb_put_dev(udev);
	return err;
}

static void hal_halt(struct hw_data *pHwData)
{
	del_timer_sync(&pHwData->LEDTimer);
	/* XXX: Wait for Timer DPC exit. */
	msleep(100);
	Wb35Rx_destroy(pHwData);
	Wb35Tx_destroy(pHwData);
	Wb35Reg_destroy(pHwData);
}

static void wb35_hw_halt(struct wbsoft_priv *adapter)
{
	/* Turn off Rx and Tx hardware ability */
	hal_stop(&adapter->sHwData);
	pr_debug("[w35und] Hal_stop O.K.\n");
	/* Waiting Irp completed */
	msleep(100);

	hal_halt(&adapter->sHwData);
}

static void wb35_disconnect(struct usb_interface *intf)
{
	struct ieee80211_hw *hw = usb_get_intfdata(intf);
	struct wbsoft_priv *priv = hw->priv;

	wb35_hw_halt(priv);

	ieee80211_stop_queues(hw);
	ieee80211_unregister_hw(hw);
	ieee80211_free_hw(hw);

	usb_set_intfdata(intf, NULL);
	usb_put_dev(interface_to_usbdev(intf));
}

static struct usb_driver wb35_driver = {
	.name		= "w35und",
	.id_table	= wb35_table,
	.probe		= wb35_probe,
	.disconnect	= wb35_disconnect,
};

module_usb_driver(wb35_driver);
