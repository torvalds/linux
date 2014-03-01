/******************************************************************************

  Copyright(c) 2003 - 2004 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

******************************************************************************

  Few modifications for Realtek's Wi-Fi drivers by
  Andrea Merello <andrea.merello@gmail.com>

  A special thanks goes to Realtek for their support !

******************************************************************************/

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <linux/uaccess.h>
#include <linux/if_vlan.h>

#include "rtllib.h"

/*


802.11 Data Frame


802.11 frame_control for data frames - 2 bytes
     ,-----------------------------------------------------------------------------------------.
bits | 0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  a  |  b  |  c  |  d  |  e   |
     |----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|------|
val  | 0  |  0  |  0  |  1  |  x  |  0  |  0  |  0  |  1  |  0  |  x  |  x  |  x  |  x  |  x   |
     |----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|------|
desc | ^-ver-^  |  ^type-^  |  ^-----subtype-----^  | to  |from |more |retry| pwr |more |wep   |
     |	  |	   | x=0 data,x=1 data+ack | DS  | DS  |frag |     | mgm |data |      |
     '-----------------------------------------------------------------------------------------'
						    /\
						    |
802.11 Data Frame				   |
	   ,--------- 'ctrl' expands to >-----------'
	  |
      ,--'---,-------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  Frame  |  fcs |
      |      | tion | (BSSID) |	 |	 | ence |  data   |      |
      `--------------------------------------------------|	 |------'
Total: 28 non-data bytes				 `----.----'
							      |
       .- 'Frame data' expands to <---------------------------'
       |
       V
      ,---------------------------------------------------.
Bytes |  1   |  1   |    1    |    3     |  2   |  0-2304 |
      |------|------|---------|----------|------|---------|
Desc. | SNAP | SNAP | Control |Eth Tunnel| Type | IP      |
      | DSAP | SSAP |	 |	  |      | Packet  |
      | 0xAA | 0xAA |0x03 (UI)|0x00-00-F8|      |	 |
      `-----------------------------------------|	 |
Total: 8 non-data bytes			 `----.----'
						     |
       .- 'IP Packet' expands, if WEP enabled, to <--'
       |
       V
      ,-----------------------.
Bytes |  4  |   0-2296  |  4  |
      |-----|-----------|-----|
Desc. | IV  | Encrypted | ICV |
      |     | IP Packet |     |
      `-----------------------'
Total: 8 non-data bytes


802.3 Ethernet Data Frame

      ,-----------------------------------------.
Bytes |   6   |   6   |  2   |  Variable |   4  |
      |-------|-------|------|-----------|------|
Desc. | Dest. | Source| Type | IP Packet |  fcs |
      |  MAC  |  MAC  |      |	   |      |
      `-----------------------------------------'
Total: 18 non-data bytes

In the event that fragmentation is required, the incoming payload is split into
N parts of size ieee->fts.  The first fragment contains the SNAP header and the
remaining packets are just data.

If encryption is enabled, each fragment payload size is reduced by enough space
to add the prefix and postfix (IV and ICV totalling 8 bytes in the case of WEP)
So if you have 1500 bytes of payload with ieee->fts set to 500 without
encryption it will take 3 frames.  With WEP it will take 4 frames as the
payload of each frame is reduced to 492 bytes.

* SKB visualization
*
*  ,- skb->data
* |
* |    ETHERNET HEADER	,-<-- PAYLOAD
* |			   |     14 bytes from skb->data
* |  2 bytes for Type --> ,T. |     (sizeof ethhdr)
* |		       | | |
* |,-Dest.--. ,--Src.---. | | |
* |  6 bytes| | 6 bytes | | | |
* v	 | |	 | | | |
* 0	 | v       1 | v | v	   2
* 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
*     ^     | ^	 | ^ |
*     |     | |	 | | |
*     |     | |	 | `T' <---- 2 bytes for Type
*     |     | |	 |
*     |     | '---SNAP--' <-------- 6 bytes for SNAP
*     |     |
*     `-IV--' <-------------------- 4 bytes for IV (WEP)
*
*      SNAP HEADER
*
*/

static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

inline int rtllib_put_snap(u8 *data, u16 h_proto)
{
	struct rtllib_snap_hdr *snap;
	u8 *oui;

	snap = (struct rtllib_snap_hdr *)data;
	snap->dsap = 0xaa;
	snap->ssap = 0xaa;
	snap->ctrl = 0x03;

	if (h_proto == 0x8137 || h_proto == 0x80f3)
		oui = P802_1H_OUI;
	else
		oui = RFC1042_OUI;
	snap->oui[0] = oui[0];
	snap->oui[1] = oui[1];
	snap->oui[2] = oui[2];

	*(u16 *)(data + SNAP_SIZE) = h_proto;

	return SNAP_SIZE + sizeof(u16);
}

int rtllib_encrypt_fragment(struct rtllib_device *ieee, struct sk_buff *frag,
			    int hdr_len)
{
	struct lib80211_crypt_data *crypt = NULL;
	int res;

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];

	if (!(crypt && crypt->ops)) {
		printk(KERN_INFO "=========>%s(), crypt is null\n", __func__);
		return -1;
	}
	/* To encrypt, frame format is:
	 * IV (4 bytes), clear payload (including SNAP), ICV (4 bytes) */

	/* Host-based IEEE 802.11 fragmentation for TX is not yet supported, so
	 * call both MSDU and MPDU encryption functions from here. */
	atomic_inc(&crypt->refcnt);
	res = 0;
	if (crypt->ops->encrypt_msdu)
		res = crypt->ops->encrypt_msdu(frag, hdr_len, crypt->priv);
	if (res == 0 && crypt->ops->encrypt_mpdu)
		res = crypt->ops->encrypt_mpdu(frag, hdr_len, crypt->priv);

	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_INFO "%s: Encryption failed: len=%d.\n",
		       ieee->dev->name, frag->len);
		ieee->ieee_stats.tx_discards++;
		return -1;
	}

	return 0;
}


void rtllib_txb_free(struct rtllib_txb *txb)
{
	if (unlikely(!txb))
		return;
	kfree(txb);
}

static struct rtllib_txb *rtllib_alloc_txb(int nr_frags, int txb_size,
					   gfp_t gfp_mask)
{
	struct rtllib_txb *txb;
	int i;
	txb = kmalloc(sizeof(struct rtllib_txb) + (sizeof(u8 *) * nr_frags),
		      gfp_mask);
	if (!txb)
		return NULL;

	memset(txb, 0, sizeof(struct rtllib_txb));
	txb->nr_frags = nr_frags;
	txb->frag_size = cpu_to_le16(txb_size);

	for (i = 0; i < nr_frags; i++) {
		txb->fragments[i] = dev_alloc_skb(txb_size);
		if (unlikely(!txb->fragments[i])) {
			i--;
			break;
		}
		memset(txb->fragments[i]->cb, 0, sizeof(txb->fragments[i]->cb));
	}
	if (unlikely(i != nr_frags)) {
		while (i >= 0)
			dev_kfree_skb_any(txb->fragments[i--]);
		kfree(txb);
		return NULL;
	}
	return txb;
}

static int rtllib_classify(struct sk_buff *skb, u8 bIsAmsdu)
{
	struct ethhdr *eth;
	struct iphdr *ip;

	eth = (struct ethhdr *)skb->data;
	if (eth->h_proto != htons(ETH_P_IP))
		return 0;

	RTLLIB_DEBUG_DATA(RTLLIB_DL_DATA, skb->data, skb->len);
	ip = ip_hdr(skb);
	switch (ip->tos & 0xfc) {
	case 0x20:
		return 2;
	case 0x40:
		return 1;
	case 0x60:
		return 3;
	case 0x80:
		return 4;
	case 0xa0:
		return 5;
	case 0xc0:
		return 6;
	case 0xe0:
		return 7;
	default:
		return 0;
	}
}

static void rtllib_tx_query_agg_cap(struct rtllib_device *ieee,
				    struct sk_buff *skb,
				    struct cb_desc *tcb_desc)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	struct tx_ts_record *pTxTs = NULL;
	struct rtllib_hdr_1addr *hdr = (struct rtllib_hdr_1addr *)skb->data;

	if (rtllib_act_scanning(ieee, false))
		return;

	if (!pHTInfo->bCurrentHTSupport || !pHTInfo->bEnableHT)
		return;
	if (!IsQoSDataFrame(skb->data))
		return;
	if (is_multicast_ether_addr(hdr->addr1))
		return;

	if (tcb_desc->bdhcp || ieee->CntAfterLink < 2)
		return;

	if (pHTInfo->IOTAction & HT_IOT_ACT_TX_NO_AGGREGATION)
		return;

	if (!ieee->GetNmodeSupportBySecCfg(ieee->dev))
		return;
	if (pHTInfo->bCurrentAMPDUEnable) {
		if (!GetTs(ieee, (struct ts_common_info **)(&pTxTs), hdr->addr1,
		    skb->priority, TX_DIR, true)) {
			printk(KERN_INFO "%s: can't get TS\n", __func__);
			return;
		}
		if (pTxTs->TxAdmittedBARecord.bValid == false) {
			if (ieee->wpa_ie_len && (ieee->pairwise_key_type ==
			    KEY_TYPE_NA)) {
				;
			} else if (tcb_desc->bdhcp == 1) {
				;
			} else if (!pTxTs->bDisable_AddBa) {
				TsStartAddBaProcess(ieee, pTxTs);
			}
			goto FORCED_AGG_SETTING;
		} else if (pTxTs->bUsingBa == false) {
			if (SN_LESS(pTxTs->TxAdmittedBARecord.BaStartSeqCtrl.field.SeqNum,
			   (pTxTs->TxCurSeq+1)%4096))
				pTxTs->bUsingBa = true;
			else
				goto FORCED_AGG_SETTING;
		}
		if (ieee->iw_mode == IW_MODE_INFRA) {
			tcb_desc->bAMPDUEnable = true;
			tcb_desc->ampdu_factor = pHTInfo->CurrentAMPDUFactor;
			tcb_desc->ampdu_density = pHTInfo->CurrentMPDUDensity;
		}
	}
FORCED_AGG_SETTING:
	switch (pHTInfo->ForcedAMPDUMode) {
	case HT_AGG_AUTO:
		break;

	case HT_AGG_FORCE_ENABLE:
		tcb_desc->bAMPDUEnable = true;
		tcb_desc->ampdu_density = pHTInfo->ForcedMPDUDensity;
		tcb_desc->ampdu_factor = pHTInfo->ForcedAMPDUFactor;
		break;

	case HT_AGG_FORCE_DISABLE:
		tcb_desc->bAMPDUEnable = false;
		tcb_desc->ampdu_density = 0;
		tcb_desc->ampdu_factor = 0;
		break;
	}
	return;
}

static void rtllib_qurey_ShortPreambleMode(struct rtllib_device *ieee,
					   struct cb_desc *tcb_desc)
{
	tcb_desc->bUseShortPreamble = false;
	if (tcb_desc->data_rate == 2)
		return;
	else if (ieee->current_network.capability &
		 WLAN_CAPABILITY_SHORT_PREAMBLE)
		tcb_desc->bUseShortPreamble = true;
	return;
}

static void rtllib_query_HTCapShortGI(struct rtllib_device *ieee,
				      struct cb_desc *tcb_desc)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	tcb_desc->bUseShortGI		= false;

	if (!pHTInfo->bCurrentHTSupport || !pHTInfo->bEnableHT)
		return;

	if (pHTInfo->bForcedShortGI) {
		tcb_desc->bUseShortGI = true;
		return;
	}

	if ((pHTInfo->bCurBW40MHz == true) && pHTInfo->bCurShortGI40MHz)
		tcb_desc->bUseShortGI = true;
	else if ((pHTInfo->bCurBW40MHz == false) && pHTInfo->bCurShortGI20MHz)
		tcb_desc->bUseShortGI = true;
}

static void rtllib_query_BandwidthMode(struct rtllib_device *ieee,
				       struct cb_desc *tcb_desc)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	tcb_desc->bPacketBW = false;

	if (!pHTInfo->bCurrentHTSupport || !pHTInfo->bEnableHT)
		return;

	if (tcb_desc->bMulticast || tcb_desc->bBroadcast)
		return;

	if ((tcb_desc->data_rate & 0x80) == 0)
		return;
	if (pHTInfo->bCurBW40MHz && pHTInfo->bCurTxBW40MHz &&
	    !ieee->bandwidth_auto_switch.bforced_tx20Mhz)
		tcb_desc->bPacketBW = true;
	return;
}

static void rtllib_query_protectionmode(struct rtllib_device *ieee,
					struct cb_desc *tcb_desc,
					struct sk_buff *skb)
{
	tcb_desc->bRTSSTBC			= false;
	tcb_desc->bRTSUseShortGI		= false;
	tcb_desc->bCTSEnable			= false;
	tcb_desc->RTSSC				= 0;
	tcb_desc->bRTSBW			= false;

	if (tcb_desc->bBroadcast || tcb_desc->bMulticast)
		return;

	if (is_broadcast_ether_addr(skb->data+16))
		return;

	if (ieee->mode < IEEE_N_24G) {
		if (skb->len > ieee->rts) {
			tcb_desc->bRTSEnable = true;
			tcb_desc->rts_rate = MGN_24M;
		} else if (ieee->current_network.buseprotection) {
			tcb_desc->bRTSEnable = true;
			tcb_desc->bCTSEnable = true;
			tcb_desc->rts_rate = MGN_24M;
		}
		return;
	} else {
		struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
		while (true) {
			if (pHTInfo->IOTAction & HT_IOT_ACT_FORCED_CTS2SELF) {
				tcb_desc->bCTSEnable	= true;
				tcb_desc->rts_rate  =	MGN_24M;
				tcb_desc->bRTSEnable = true;
				break;
			} else if (pHTInfo->IOTAction & (HT_IOT_ACT_FORCED_RTS |
				   HT_IOT_ACT_PURE_N_MODE)) {
				tcb_desc->bRTSEnable = true;
				tcb_desc->rts_rate  =	MGN_24M;
				break;
			}
			if (ieee->current_network.buseprotection) {
				tcb_desc->bRTSEnable = true;
				tcb_desc->bCTSEnable = true;
				tcb_desc->rts_rate = MGN_24M;
				break;
			}
			if (pHTInfo->bCurrentHTSupport  && pHTInfo->bEnableHT) {
				u8 HTOpMode = pHTInfo->CurrentOpMode;
				if ((pHTInfo->bCurBW40MHz && (HTOpMode == 2 ||
				     HTOpMode == 3)) ||
				     (!pHTInfo->bCurBW40MHz && HTOpMode == 3)) {
					tcb_desc->rts_rate = MGN_24M;
					tcb_desc->bRTSEnable = true;
					break;
				}
			}
			if (skb->len > ieee->rts) {
				tcb_desc->rts_rate = MGN_24M;
				tcb_desc->bRTSEnable = true;
				break;
			}
			if (tcb_desc->bAMPDUEnable) {
				tcb_desc->rts_rate = MGN_24M;
				tcb_desc->bRTSEnable = false;
				break;
			}
			goto NO_PROTECTION;
		}
	}
	if (ieee->current_network.capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		tcb_desc->bUseShortPreamble = true;
	if (ieee->iw_mode == IW_MODE_MASTER)
			goto NO_PROTECTION;
	return;
NO_PROTECTION:
	tcb_desc->bRTSEnable	= false;
	tcb_desc->bCTSEnable	= false;
	tcb_desc->rts_rate	= 0;
	tcb_desc->RTSSC		= 0;
	tcb_desc->bRTSBW	= false;
}


static void rtllib_txrate_selectmode(struct rtllib_device *ieee,
				     struct cb_desc *tcb_desc)
{
	if (ieee->bTxDisableRateFallBack)
		tcb_desc->bTxDisableRateFallBack = true;

	if (ieee->bTxUseDriverAssingedRate)
		tcb_desc->bTxUseDriverAssingedRate = true;
	if (!tcb_desc->bTxDisableRateFallBack ||
	    !tcb_desc->bTxUseDriverAssingedRate) {
		if (ieee->iw_mode == IW_MODE_INFRA ||
		    ieee->iw_mode == IW_MODE_ADHOC)
			tcb_desc->RATRIndex = 0;
	}
}

u16 rtllib_query_seqnum(struct rtllib_device *ieee, struct sk_buff *skb,
			u8 *dst)
{
	u16 seqnum = 0;

	if (is_multicast_ether_addr(dst))
		return 0;
	if (IsQoSDataFrame(skb->data)) {
		struct tx_ts_record *pTS = NULL;
		if (!GetTs(ieee, (struct ts_common_info **)(&pTS), dst,
		    skb->priority, TX_DIR, true))
			return 0;
		seqnum = pTS->TxCurSeq;
		pTS->TxCurSeq = (pTS->TxCurSeq+1)%4096;
		return seqnum;
	}
	return 0;
}

static int wme_downgrade_ac(struct sk_buff *skb)
{
	switch (skb->priority) {
	case 6:
	case 7:
		skb->priority = 5; /* VO -> VI */
		return 0;
	case 4:
	case 5:
		skb->priority = 3; /* VI -> BE */
		return 0;
	case 0:
	case 3:
		skb->priority = 1; /* BE -> BK */
		return 0;
	default:
		return -1;
	}
}

int rtllib_xmit_inter(struct sk_buff *skb, struct net_device *dev)
{
	struct rtllib_device *ieee = (struct rtllib_device *)
				     netdev_priv_rsl(dev);
	struct rtllib_txb *txb = NULL;
	struct rtllib_hdr_3addrqos *frag_hdr;
	int i, bytes_per_frag, nr_frags, bytes_last_frag, frag_size;
	unsigned long flags;
	struct net_device_stats *stats = &ieee->stats;
	int ether_type = 0, encrypt;
	int bytes, fc, qos_ctl = 0, hdr_len;
	struct sk_buff *skb_frag;
	struct rtllib_hdr_3addrqos header = { /* Ensure zero initialized */
		.duration_id = 0,
		.seq_ctl = 0,
		.qos_ctl = 0
	};
	u8 dest[ETH_ALEN], src[ETH_ALEN];
	int qos_actived = ieee->current_network.qos_data.active;
	struct lib80211_crypt_data *crypt = NULL;
	struct cb_desc *tcb_desc;
	u8 bIsMulticast = false;
	u8 IsAmsdu = false;
	bool	bdhcp = false;

	spin_lock_irqsave(&ieee->lock, flags);

	/* If there is no driver handler to take the TXB, don't bother
	 * creating it... */
	if ((!ieee->hard_start_xmit && !(ieee->softmac_features &
	   IEEE_SOFTMAC_TX_QUEUE)) ||
	   ((!ieee->softmac_data_hard_start_xmit &&
	   (ieee->softmac_features & IEEE_SOFTMAC_TX_QUEUE)))) {
		printk(KERN_WARNING "%s: No xmit handler.\n",
		       ieee->dev->name);
		goto success;
	}


	if (likely(ieee->raw_tx == 0)) {
		if (unlikely(skb->len < SNAP_SIZE + sizeof(u16))) {
			printk(KERN_WARNING "%s: skb too small (%d).\n",
			ieee->dev->name, skb->len);
			goto success;
		}
		/* Save source and destination addresses */
		memcpy(dest, skb->data, ETH_ALEN);
		memcpy(src, skb->data+ETH_ALEN, ETH_ALEN);

		memset(skb->cb, 0, sizeof(skb->cb));
		ether_type = ntohs(((struct ethhdr *)skb->data)->h_proto);

		if (ieee->iw_mode == IW_MODE_MONITOR) {
			txb = rtllib_alloc_txb(1, skb->len, GFP_ATOMIC);
			if (unlikely(!txb)) {
				printk(KERN_WARNING "%s: Could not allocate "
				       "TXB\n",
				ieee->dev->name);
				goto failed;
			}

			txb->encrypted = 0;
			txb->payload_size = cpu_to_le16(skb->len);
			memcpy(skb_put(txb->fragments[0], skb->len), skb->data,
			       skb->len);

			goto success;
		}

		if (skb->len > 282) {
			if (ETH_P_IP == ether_type) {
				const struct iphdr *ip = (struct iphdr *)
					((u8 *)skb->data+14);
				if (IPPROTO_UDP == ip->protocol) {
					struct udphdr *udp;

					udp = (struct udphdr *)((u8 *)ip +
					      (ip->ihl << 2));
					if (((((u8 *)udp)[1] == 68) &&
					   (((u8 *)udp)[3] == 67)) ||
					   ((((u8 *)udp)[1] == 67) &&
					   (((u8 *)udp)[3] == 68))) {
						bdhcp = true;
						ieee->LPSDelayCnt = 200;
					}
				}
			} else if (ETH_P_ARP == ether_type) {
				printk(KERN_INFO "=================>DHCP "
				       "Protocol start tx ARP pkt!!\n");
				bdhcp = true;
				ieee->LPSDelayCnt =
					 ieee->current_network.tim.tim_count;
			}
		}

		skb->priority = rtllib_classify(skb, IsAmsdu);
		crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
		encrypt = !(ether_type == ETH_P_PAE && ieee->ieee802_1x) &&
			ieee->host_encrypt && crypt && crypt->ops;
		if (!encrypt && ieee->ieee802_1x &&
		    ieee->drop_unencrypted && ether_type != ETH_P_PAE) {
			stats->tx_dropped++;
			goto success;
		}
		if (crypt && !encrypt && ether_type == ETH_P_PAE) {
			struct eapol *eap = (struct eapol *)(skb->data +
				sizeof(struct ethhdr) - SNAP_SIZE -
				sizeof(u16));
			RTLLIB_DEBUG_EAP("TX: IEEE 802.11 EAPOL frame: %s\n",
				eap_get_type(eap->type));
		}

		/* Advance the SKB to the start of the payload */
		skb_pull(skb, sizeof(struct ethhdr));

		/* Determine total amount of storage required for TXB packets */
		bytes = skb->len + SNAP_SIZE + sizeof(u16);

		if (encrypt)
			fc = RTLLIB_FTYPE_DATA | RTLLIB_FCTL_WEP;
		else
			fc = RTLLIB_FTYPE_DATA;

		if (qos_actived)
			fc |= RTLLIB_STYPE_QOS_DATA;
		else
			fc |= RTLLIB_STYPE_DATA;

		if (ieee->iw_mode == IW_MODE_INFRA) {
			fc |= RTLLIB_FCTL_TODS;
			/* To DS: Addr1 = BSSID, Addr2 = SA,
			Addr3 = DA */
			memcpy(&header.addr1, ieee->current_network.bssid,
			       ETH_ALEN);
			memcpy(&header.addr2, &src, ETH_ALEN);
			if (IsAmsdu)
				memcpy(&header.addr3,
				       ieee->current_network.bssid, ETH_ALEN);
			else
				memcpy(&header.addr3, &dest, ETH_ALEN);
		} else if (ieee->iw_mode == IW_MODE_ADHOC) {
			/* not From/To DS: Addr1 = DA, Addr2 = SA,
			Addr3 = BSSID */
			memcpy(&header.addr1, dest, ETH_ALEN);
			memcpy(&header.addr2, src, ETH_ALEN);
			memcpy(&header.addr3, ieee->current_network.bssid,
			       ETH_ALEN);
		}

		bIsMulticast = is_multicast_ether_addr(header.addr1);

		header.frame_ctl = cpu_to_le16(fc);

		/* Determine fragmentation size based on destination (multicast
		* and broadcast are not fragmented) */
		if (bIsMulticast) {
			frag_size = MAX_FRAG_THRESHOLD;
			qos_ctl |= QOS_CTL_NOTCONTAIN_ACK;
		} else {
			frag_size = ieee->fts;
			qos_ctl = 0;
		}

		if (qos_actived) {
			hdr_len = RTLLIB_3ADDR_LEN + 2;

		/* in case we are a client verify acm is not set for this ac */
		while (unlikely(ieee->wmm_acm & (0x01 << skb->priority))) {
			printk(KERN_INFO "skb->priority = %x\n", skb->priority);
			if (wme_downgrade_ac(skb))
				break;
			printk(KERN_INFO "converted skb->priority = %x\n",
			       skb->priority);
		 }
			qos_ctl |= skb->priority;
			header.qos_ctl = cpu_to_le16(qos_ctl & RTLLIB_QOS_TID);
		} else {
			hdr_len = RTLLIB_3ADDR_LEN;
		}
		/* Determine amount of payload per fragment.  Regardless of if
		 * this stack is providing the full 802.11 header, one will
		 * eventually be affixed to this fragment -- so we must account
		 * for it when determining the amount of payload space. */
		bytes_per_frag = frag_size - hdr_len;
		if (ieee->config &
		   (CFG_RTLLIB_COMPUTE_FCS | CFG_RTLLIB_RESERVE_FCS))
			bytes_per_frag -= RTLLIB_FCS_LEN;

		/* Each fragment may need to have room for encrypting
		 * pre/postfix */
		if (encrypt) {
			bytes_per_frag -= crypt->ops->extra_mpdu_prefix_len +
				crypt->ops->extra_mpdu_postfix_len +
				crypt->ops->extra_msdu_prefix_len +
				crypt->ops->extra_msdu_postfix_len;
		}
		/* Number of fragments is the total bytes_per_frag /
		* payload_per_fragment */
		nr_frags = bytes / bytes_per_frag;
		bytes_last_frag = bytes % bytes_per_frag;
		if (bytes_last_frag)
			nr_frags++;
		else
			bytes_last_frag = bytes_per_frag;

		/* When we allocate the TXB we allocate enough space for the
		 * reserve and full fragment bytes (bytes_per_frag doesn't
		 * include prefix, postfix, header, FCS, etc.) */
		txb = rtllib_alloc_txb(nr_frags, frag_size +
				       ieee->tx_headroom, GFP_ATOMIC);
		if (unlikely(!txb)) {
			printk(KERN_WARNING "%s: Could not allocate TXB\n",
			ieee->dev->name);
			goto failed;
		}
		txb->encrypted = encrypt;
		txb->payload_size = cpu_to_le16(bytes);

		if (qos_actived)
			txb->queue_index = UP2AC(skb->priority);
		else
			txb->queue_index = WME_AC_BE;

		for (i = 0; i < nr_frags; i++) {
			skb_frag = txb->fragments[i];
			tcb_desc = (struct cb_desc *)(skb_frag->cb +
				    MAX_DEV_ADDR_SIZE);
			if (qos_actived) {
				skb_frag->priority = skb->priority;
				tcb_desc->queue_index =  UP2AC(skb->priority);
			} else {
				skb_frag->priority = WME_AC_BE;
				tcb_desc->queue_index = WME_AC_BE;
			}
			skb_reserve(skb_frag, ieee->tx_headroom);

			if (encrypt) {
				if (ieee->hwsec_active)
					tcb_desc->bHwSec = 1;
				else
					tcb_desc->bHwSec = 0;
				skb_reserve(skb_frag,
					    crypt->ops->extra_mpdu_prefix_len +
					    crypt->ops->extra_msdu_prefix_len);
			} else {
				tcb_desc->bHwSec = 0;
			}
			frag_hdr = (struct rtllib_hdr_3addrqos *)
				   skb_put(skb_frag, hdr_len);
			memcpy(frag_hdr, &header, hdr_len);

			/* If this is not the last fragment, then add the
			 * MOREFRAGS bit to the frame control */
			if (i != nr_frags - 1) {
				frag_hdr->frame_ctl = cpu_to_le16(
					fc | RTLLIB_FCTL_MOREFRAGS);
				bytes = bytes_per_frag;

			} else {
				/* The last fragment has the remaining length */
				bytes = bytes_last_frag;
			}
			if ((qos_actived) && (!bIsMulticast)) {
				frag_hdr->seq_ctl =
					 cpu_to_le16(rtllib_query_seqnum(ieee, skb_frag,
							     header.addr1));
				frag_hdr->seq_ctl =
					 cpu_to_le16(le16_to_cpu(frag_hdr->seq_ctl)<<4 | i);
			} else {
				frag_hdr->seq_ctl =
					 cpu_to_le16(ieee->seq_ctrl[0]<<4 | i);
			}
			/* Put a SNAP header on the first fragment */
			if (i == 0) {
				rtllib_put_snap(
					skb_put(skb_frag, SNAP_SIZE +
					sizeof(u16)), ether_type);
				bytes -= SNAP_SIZE + sizeof(u16);
			}

			memcpy(skb_put(skb_frag, bytes), skb->data, bytes);

			/* Advance the SKB... */
			skb_pull(skb, bytes);

			/* Encryption routine will move the header forward in
			 * order to insert the IV between the header and the
			 * payload */
			if (encrypt)
				rtllib_encrypt_fragment(ieee, skb_frag,
							hdr_len);
			if (ieee->config &
			   (CFG_RTLLIB_COMPUTE_FCS | CFG_RTLLIB_RESERVE_FCS))
				skb_put(skb_frag, 4);
		}

		if ((qos_actived) && (!bIsMulticast)) {
			if (ieee->seq_ctrl[UP2AC(skb->priority) + 1] == 0xFFF)
				ieee->seq_ctrl[UP2AC(skb->priority) + 1] = 0;
			else
				ieee->seq_ctrl[UP2AC(skb->priority) + 1]++;
		} else {
			if (ieee->seq_ctrl[0] == 0xFFF)
				ieee->seq_ctrl[0] = 0;
			else
					ieee->seq_ctrl[0]++;
		}
	} else {
		if (unlikely(skb->len < sizeof(struct rtllib_hdr_3addr))) {
			printk(KERN_WARNING "%s: skb too small (%d).\n",
			ieee->dev->name, skb->len);
			goto success;
		}

		txb = rtllib_alloc_txb(1, skb->len, GFP_ATOMIC);
		if (!txb) {
			printk(KERN_WARNING "%s: Could not allocate TXB\n",
			ieee->dev->name);
			goto failed;
		}

		txb->encrypted = 0;
		txb->payload_size = cpu_to_le16(skb->len);
		memcpy(skb_put(txb->fragments[0], skb->len), skb->data,
		       skb->len);
	}

 success:
	if (txb) {
		struct cb_desc *tcb_desc = (struct cb_desc *)
				(txb->fragments[0]->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->bTxEnableFwCalcDur = 1;
		tcb_desc->priority = skb->priority;

		if (ether_type == ETH_P_PAE) {
			if (ieee->pHTInfo->IOTAction &
			    HT_IOT_ACT_WA_IOT_Broadcom) {
				tcb_desc->data_rate =
					 MgntQuery_TxRateExcludeCCKRates(ieee);
				tcb_desc->bTxDisableRateFallBack = false;
			} else {
				tcb_desc->data_rate = ieee->basic_rate;
				tcb_desc->bTxDisableRateFallBack = 1;
			}


			tcb_desc->RATRIndex = 7;
			tcb_desc->bTxUseDriverAssingedRate = 1;
		} else {
			if (is_multicast_ether_addr(header.addr1))
				tcb_desc->bMulticast = 1;
			if (is_broadcast_ether_addr(header.addr1))
				tcb_desc->bBroadcast = 1;
			rtllib_txrate_selectmode(ieee, tcb_desc);
			if (tcb_desc->bMulticast ||  tcb_desc->bBroadcast)
				tcb_desc->data_rate = ieee->basic_rate;
			else
				tcb_desc->data_rate = CURRENT_RATE(ieee->mode,
					ieee->rate, ieee->HTCurrentOperaRate);

			if (bdhcp) {
				if (ieee->pHTInfo->IOTAction &
				    HT_IOT_ACT_WA_IOT_Broadcom) {
					tcb_desc->data_rate =
					   MgntQuery_TxRateExcludeCCKRates(ieee);
					tcb_desc->bTxDisableRateFallBack = false;
				} else {
					tcb_desc->data_rate = MGN_1M;
					tcb_desc->bTxDisableRateFallBack = 1;
				}


				tcb_desc->RATRIndex = 7;
				tcb_desc->bTxUseDriverAssingedRate = 1;
				tcb_desc->bdhcp = 1;
			}

			rtllib_qurey_ShortPreambleMode(ieee, tcb_desc);
			rtllib_tx_query_agg_cap(ieee, txb->fragments[0],
						tcb_desc);
			rtllib_query_HTCapShortGI(ieee, tcb_desc);
			rtllib_query_BandwidthMode(ieee, tcb_desc);
			rtllib_query_protectionmode(ieee, tcb_desc,
						    txb->fragments[0]);
		}
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
	dev_kfree_skb_any(skb);
	if (txb) {
		if (ieee->softmac_features & IEEE_SOFTMAC_TX_QUEUE) {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += txb->payload_size;
			rtllib_softmac_xmit(txb, ieee);
		} else {
			if ((*ieee->hard_start_xmit)(txb, dev) == 0) {
				stats->tx_packets++;
				stats->tx_bytes += txb->payload_size;
				return 0;
			}
			rtllib_txb_free(txb);
		}
	}

	return 0;

 failed:
	spin_unlock_irqrestore(&ieee->lock, flags);
	netif_stop_queue(dev);
	stats->tx_errors++;
	return 1;

}
int rtllib_xmit(struct sk_buff *skb, struct net_device *dev)
{
	memset(skb->cb, 0, sizeof(skb->cb));
	return rtllib_xmit_inter(skb, dev);
}
EXPORT_SYMBOL(rtllib_xmit);
