/*
 * Copyright (c) 2012 Espressif System.
 */

#include "linux/types.h"
#include "linux/kernel.h"
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include <linux/skbuff.h>

#include <net/tcp.h>
#include <linux/ip.h>
#include <asm/checksum.h>

#include "esp_pub.h"
#include "esp_utils.h"
#include "esp_wmac.h"
#include "esp_debug.h"

/*
 * Convert IEEE channel number to MHz frequency.
 */
u32
esp_ieee2mhz(u8 chan)
{
        if (chan == 14)
                return 2484;

        if (chan < 14)
                return 2407 + chan*5;
        else
                return 2512 + ((chan-15)*20);
}
enum {
        ESP_RATE_1_LONG = 0x0,
        ESP_RATE_2_LONG = 0x1,
        ESP_RATE_2_SHORT = 0x5,
        ESP_RATE_5_SHORT = 0x6,
        ESP_RATE_5_LONG = 0x2,
        ESP_RATE_11_SHORT = 0x7,
        ESP_RATE_11_LONG = 0x3,
        ESP_RATE_6 = 0xb,
        ESP_RATE_9 = 0xf,
        ESP_RATE_12 = 0xa,
        ESP_RATE_18 = 0xe,
        ESP_RATE_24 = 0x9,
        ESP_RATE_36 = 0xd,
        ESP_RATE_48 = 0x8,
        ESP_RATE_54 = 0xc,
        /*        ESP_RATE_MCS0 =0x10,
                ESP_RATE_MCS1 =0x11,
                ESP_RATE_MCS2 =0x12,
                ESP_RATE_MCS3 =0x13,
                ESP_RATE_MCS4 =0x14,
                ESP_RATE_MCS5 =0x15,
                ESP_RATE_MCS6 =0x16,
                ESP_RATE_MCS7 =0x17,
        */
};

static u8 esp_rate_table[20] = {
        ESP_RATE_1_LONG,
        ESP_RATE_2_SHORT,
        ESP_RATE_5_SHORT,
        ESP_RATE_11_SHORT,
        ESP_RATE_6,
        ESP_RATE_9,
        ESP_RATE_12,
        ESP_RATE_18,
        ESP_RATE_24,
        ESP_RATE_36,
        ESP_RATE_48,
        ESP_RATE_54,
        /*        ESP_RATE_MCS0,
                ESP_RATE_MCS1,
                ESP_RATE_MCS2,
                ESP_RATE_MCS3,
                ESP_RATE_MCS4,
                ESP_RATE_MCS5,
                ESP_RATE_MCS6,
                ESP_RATE_MCS7,
        */
};

s8 esp_wmac_rate2idx(u8 rate)
{
        int i;

        for (i = 0; i < 20; i++) {
                if (rate == esp_rate_table[i])
                        return i;
        }

        if (rate == ESP_RATE_2_LONG)
                return 1;
        if (rate == ESP_RATE_5_LONG)
                return 2;
        if (rate == ESP_RATE_11_LONG)
                return 3;

        esp_dbg(ESP_DBG_ERROR,"%s unknown rate 0x%02x \n", __func__, rate);

        return 0;
}

bool esp_wmac_rxsec_error(u8 error)
{
        return (error >= RX_SECOV_ERR && error <= RX_SECFIFO_TIMEOUT) || (error >= RX_WEPICV_ERR && error <= RX_WAPIMIC_ERR);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
int esp_cipher2alg(int cipher)
{
        if (cipher == WLAN_CIPHER_SUITE_TKIP)
                return ALG_TKIP;

        if (cipher == WLAN_CIPHER_SUITE_CCMP)
                return ALG_CCMP;

        if (cipher == WLAN_CIPHER_SUITE_WEP40 || cipher == WLAN_CIPHER_SUITE_WEP104)
                return ALG_WEP;
	
	if (cipher == WLAN_CIPHER_SUITE_AES_CMAC)
			return ALG_AES_CMAC;

        //printk("%s wrong cipher 0x%x!\n",__func__,cipher);

        return -1;
}
#endif /* NEW_KERNEL */

#ifdef RX_CHECKSUM_TEST
atomic_t g_iv_len;
void esp_rx_checksum_test(struct sk_buff *skb)
{
	static u32 ip_err = 0;
	static u32 tcp_err = 0;
	struct ieee80211_hdr *pwh = (struct ieee80211_hdr *)skb->data;
	int hdrlen = ieee80211_hdrlen(pwh->frame_control);
	
	if(ieee80211_has_protected(pwh->frame_control))
		hdrlen += atomic_read(&g_iv_len);

	if (ieee80211_is_data(pwh->frame_control)) {
		struct llc_snap_hdr * llc = (struct llc_snap_hdr *)(skb->data + hdrlen);
		if (ntohs(llc->eth_type) == ETH_P_IP) {
			int llclen = sizeof(struct llc_snap_hdr);
			struct iphdr *iph = (struct iphdr *)(skb->data + hdrlen + llclen);
			__sum16 csum_bak = iph->check;

			iph->check = 0;
			iph->check = ip_fast_csum(iph, iph->ihl);
			if (iph->check != csum_bak) {
				esp_dbg(ESP_DBG_ERROR, "total ip checksum error %d\n", ++ip_err);
			}
			iph->check = csum_bak;

			if (iph->protocol == 0x06) {
				struct tcphdr *tcph = (struct tcphdr *)(skb->data + hdrlen + llclen + iph->ihl * 4);
				int datalen = skb->len - (hdrlen + llclen + iph->ihl * 4);
				csum_bak = tcph->check;

				tcph->check = 0;
				tcph->check = tcp_v4_check(datalen, iph->saddr, iph->daddr, csum_partial((char *)tcph, datalen, 0));
				if (tcph->check != csum_bak)
				{
					esp_dbg(ESP_DBG_ERROR, "total tcp checksum error %d\n", ++tcp_err);
				}
				tcph->check = csum_bak;
			}
        }
	}
}

#endif

#ifdef GEN_ERR_CHECKSUM

void esp_gen_err_checksum(struct sk_buff *skb)
{
        static u32 tx_seq = 0;
    	if ((tx_seq++ % 16) == 0)
    	{
    		struct ieee80211_hdr * hdr = (struct ieee80211_hdr *)skb->data;
    		int hdrlen = ieee80211_hdrlen(hdr->frame_control);
	
		if(ieee80211_has_protected(pwh->frame_control))
                	hdrlen += IEEE80211_SKB_CB(skb)->control.hw_key->iv_len;

    		struct llc_snap_hdr * llc = (struct llc_snap_hdr *)(skb->data + hdrlen);
    		if (ntohs(llc->eth_type) == ETH_P_IP) {
    			int llclen = sizeof(struct llc_snap_hdr);
    			struct iphdr *iph = (struct iphdr *)(skb->data + hdrlen + llclen);

    			iph->check = ~iph->check;

    			if (iph->protocol == 0x06) {
    				struct tcphdr *tcph = (struct tcphdr *)(skb->data + hdrlen + llclen + iph->ihl * 4);
    				tcph->check = ~tcph->check;
    			}
    		}
    	}
}
#endif

bool esp_is_ip_pkt(struct sk_buff *skb)
{
                struct ieee80211_hdr * hdr = (struct ieee80211_hdr *)skb->data;
                int hdrlen;
                struct llc_snap_hdr * llc;
		
		if (!ieee80211_is_data(hdr->frame_control))
			return false;
		
		hdrlen = ieee80211_hdrlen(hdr->frame_control);
		if(ieee80211_has_protected(hdr->frame_control))
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 27))
                	hdrlen += IEEE80211_SKB_CB(skb)->control.hw_key->iv_len;
#else
                	hdrlen += IEEE80211_SKB_CB(skb)->control.iv_len;
#endif
#ifdef RX_CHECKSUM_TEST
		atomic_set(&g_iv_len, IEEE80211_SKB_CB(skb)->control.hw_key->iv_len);
#endif
		if(skb->len < hdrlen + sizeof(struct llc_snap_hdr))
			return false;
		llc = (struct llc_snap_hdr *)(skb->data + hdrlen);
                if (ntohs(llc->eth_type) != ETH_P_IP)
			return false;
		else
			return true;
}
