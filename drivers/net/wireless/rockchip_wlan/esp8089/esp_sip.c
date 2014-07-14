/*
 * Copyright (c) 2009 - 2013 Espressif System.
 */

#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/version.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include <linux/completion.h> 

#include "esp_pub.h"
#include "esp_sip.h"
#include "esp_ctrl.h"
#include "esp_sif.h"
#include "esp_debug.h"
#include "slc_host_register.h"
#include "esp_wmac.h"
#include "esp_utils.h"
#ifdef TEST_MODE
#include "testmode.h"
#endif

#ifdef USE_EXT_GPIO
#include "esp_ext.h"
#endif /* USE_EXT_GPIO */

extern struct completion *gl_bootup_cplx; 
static u32 bcn_counter = 0;
static u32 probe_rsp_counter = 0;

#define TID_TO_AC(_tid) ((_tid)== 0||((_tid)==3)?WME_AC_BE:((_tid)<3)?WME_AC_BK:((_tid)<6)?WME_AC_VI:WME_AC_VO)

#ifdef SIP_DEBUG
#define esp_sip_dbg esp_dbg
struct sip_trace {
        u32 tx_data;
        u32 tx_cmd;
        u32 rx_data;
        u32 rx_evt;
        u32 rx_tx_status;
        u32 tx_out_of_credit;
        u32 tx_one_shot_overflow;
};
static struct sip_trace str;
#define STRACE_TX_DATA_INC() (str.tx_data++)
#define STRACE_TX_CMD_INC()  (str.tx_cmd++)
#define STRACE_RX_DATA_INC() (str.rx_data++)
#define STRACE_RX_EVENT_INC() (str.rx_evt++)
#define STRACE_RX_TXSTATUS_INC() (str.rx_tx_status++)
#define STRACE_TX_OUT_OF_CREDIT_INC() (str.tx_out_of_credit++)
#define STRACE_TX_ONE_SHOT_INC() (str.tx_one_shot_overflow++)

#if 0
static void sip_show_trace(struct esp_sip *sip);
#endif //0000

#define STRACE_SHOW(sip)  sip_show_trace(sip)
#else
#define esp_sip_dbg(...)
#define STRACE_TX_DATA_INC()
#define STRACE_TX_CMD_INC()
#define STRACE_RX_DATA_INC()
#define STRACE_RX_EVENT_INC()
#define STRACE_RX_TXSTATUS_INC()
#define STRACE_TX_OUT_OF_CREDIT_INC()
#define STRACE_TX_ONE_SHOT_INC()
#define STRACE_SHOW(sip)
#endif /* SIP_DEBUG */

#define SIP_STOP_QUEUE_THRESHOLD 48
#define SIP_RESUME_QUEUE_THRESHOLD  12
#ifndef FAST_TX_STATUS
#define SIP_PENDING_STOP_TX_THRESHOLD 6
#define SIP_PENDING_RESUME_TX_THRESHOLD 6
#endif /* !FAST_TX_STATUS */

#define SIP_MIN_DATA_PKT_LEN    (sizeof(struct esp_mac_rx_ctrl) + 24) //24 is min 80211hdr
#define TARGET_RX_SIZE 524

static struct sip_pkt *sip_get_ctrl_buf(struct esp_sip *sip, SIP_BUF_TYPE bftype);

static void sip_reclaim_ctrl_buf(struct esp_sip *sip, struct sip_pkt *pkt, SIP_BUF_TYPE bftype);

static void sip_free_init_ctrl_buf(struct esp_sip *sip);

static void sip_dec_credit(struct esp_sip *sip);


static int sip_pack_pkt(struct esp_sip *sip, struct sk_buff *skb, int *pm_state);

static struct esp_mac_rx_ctrl *sip_parse_normal_mac_ctrl(struct sk_buff *skb, int * pkt_len_enc, int *buf_len, int *pulled_len);

static struct sk_buff * sip_parse_data_rx_info(struct esp_sip *sip, struct sk_buff *skb, int pkt_len_enc, int buf_len, struct esp_mac_rx_ctrl *mac_ctrl, int *pulled_len);

#ifndef RX_SYNC
static inline void sip_rx_pkt_enqueue(struct esp_sip *sip, struct sk_buff *skb);

static inline struct sk_buff * sip_rx_pkt_dequeue(struct esp_sip *sip);
#endif /* RX_SYNC */
#ifndef FAST_TX_STATUS
static void sip_after_tx_status_update(struct esp_sip *sip);
#endif /* !FAST_TX_STATUS */

static void sip_after_write_pkts(struct esp_sip *sip);

//static void show_data_seq(u8 *pkt);

static void sip_update_tx_credits(struct esp_sip *sip, u16 recycled_credits);

//static void sip_trigger_txq_process(struct esp_sip *sip);

#ifndef RX_SYNC
static bool sip_rx_pkt_process(struct esp_sip * sip, struct sk_buff *skb);
#else
static void sip_rx_pkt_process_sync(struct esp_sip *sip, struct sk_buff *skb);
#endif /* RX_SYNC */

#ifdef FAST_TX_STATUS
static void sip_tx_status_report(struct esp_sip *sip, struct sk_buff *skb, struct ieee80211_tx_info* tx_info, bool success);
#endif /* FAST_TX_STATUS */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
static void sip_check_skb_alignment(struct sk_buff *skb);
#endif /* NEW_KERNEL */

#ifdef ESP_RX_COPYBACK_TEST
/* only for rx test */
static u8 *copyback_buf;
static u32 copyback_offset = 0;
#endif /* ESP_RX_COPYBACK_TEST */

#ifdef FPGA_TXDATA
int sip_send_tx_data(struct esp_sip *sip);
#endif/* FPGA_TXDATA */

#ifdef FPGA_LOOPBACK
int sip_send_loopback_cmd_mblk(struct esp_sip *sip);
#endif /* FPGA_LOOPBACK */

#ifdef SIP_DEBUG
#if 0
static void sip_show_trace(struct esp_sip *sip)
{
        esp_sip_dbg(ESP_DBG_TRACE, "\n \t tx_data %u \t tx_cmd %u \t rx_data %u \t rx_evt %u \t rx_tx_status %u \n\n", \
                    str.tx_data, str.tx_cmd, str.rx_data, str.rx_evt, str.rx_tx_status);
}
#endif //0000
#endif //SIP_DEBUG

#if 0
static void show_data_seq(u8 *pkt)
{
        struct ieee80211_hdr * wh = (struct ieee80211_hdr *)pkt;
        u16 seq = 0;

        if (ieee80211_is_data(wh->frame_control)) {
                seq = (le16_to_cpu(wh->seq_ctrl) >> 4);
                esp_sip_dbg(ESP_DBG_TRACE, " ieee80211 seq %u addr1 %pM\n", seq, wh->addr1);
        } else if (ieee80211_is_beacon(wh->frame_control) || ieee80211_is_probe_resp(wh->frame_control))
                esp_sip_dbg(ESP_DBG_TRACE, " ieee80211 probe resp or beacon 0x%04x\n", wh->frame_control);
        else
                esp_sip_dbg(ESP_DBG_TRACE, " ieee80211 other mgmt pkt 0x%04x\n", wh->frame_control);
}
#endif //0000

//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
static bool check_ac_tid(u8 *pkt, u8 ac, u8 tid)
{
        struct ieee80211_hdr * wh = (struct ieee80211_hdr *)pkt;
#ifdef TID_DEBUG
        u16 real_tid = 0;
#endif //TID_DEBUG

        if (ieee80211_is_data_qos(wh->frame_control)) {
#ifdef TID_DEBUG
		real_tid = *ieee80211_get_qos_ctl(wh) & IEEE80211_QOS_CTL_TID_MASK;

                esp_sip_dbg(ESP_SHOW, "ac:%u, tid:%u, tid in pkt:%u\n", ac, tid, real_tid);
                if (tid != real_tid) {
                        esp_sip_dbg(ESP_DBG_ERROR, "111 ac:%u, tid:%u, tid in pkt:%u\n", ac, tid, real_tid);
                }
                if (TID_TO_AC(tid) != ac) {
                        esp_sip_dbg(ESP_DBG_ERROR, "222 ac:%u, tid:%u, tid in pkt:%u\n", ac, tid, real_tid);
                }

                //ASSERT(tid == real_tid);
                //ASSERT(TID_TO_AC(tid) == ac);
#endif /* TID_DEBUG*/
        } else if (ieee80211_is_mgmt(wh->frame_control)) {
#ifdef TID_DEBUG
                esp_sip_dbg(ESP_SHOW, "ac:%u, tid:%u\n", ac, tid);
                if (tid != 7 || ac != WME_AC_VO) {
                        esp_sip_dbg(ESP_DBG_ERROR, "333 ac:%u, tid:%u\n", ac, tid);
                }
                //ASSERT(tid == 7);
                //ASSERT(ac == WME_AC_VO);
#endif /* TID_DEBUG*/
        } else {
                if (ieee80211_is_ctl(wh->frame_control)) {
#ifdef TID_DEBUG
                        esp_sip_dbg(ESP_SHOW, "%s is ctrl pkt fc 0x%04x ac:%u, tid:%u, tid in pkt:%u\n", __func__, wh->frame_control, ac, tid, real_tid);
#endif /* TID_DEBUG*/
                } else {
                        if (tid != 0 || ac != WME_AC_BE) {
                                //show_buf(pkt, 24);
                                esp_sip_dbg(ESP_DBG_LOG, "444 ac:%u, tid:%u \n", ac, tid);
                                if (tid == 7 && ac == WME_AC_VO)
                                        return false;
                        }
                        return true; //hack to modify non-qos null data.

                }
                //ASSERT(tid == 0);
                //ASSERT(ac = WME_AC_BE);
        }

        return false;
}
//#endif /* NEW_KERNEL || KERNEL_35 */

static void sip_update_tx_credits(struct esp_sip *sip, u16 recycled_credits)
{
        esp_sip_dbg(ESP_DBG_TRACE, "%s:before add, credits is %d\n", __func__, atomic_read(&sip->tx_credits));
        atomic_add(recycled_credits, &sip->tx_credits);
        esp_sip_dbg(ESP_DBG_TRACE, "%s:after add %d, credits is %d\n", __func__, recycled_credits, atomic_read(&sip->tx_credits));
}

void sip_trigger_txq_process(struct esp_sip *sip)
{
        if (atomic_read(&sip->tx_credits) <= sip->credit_to_reserve)  //no credits, do nothing
                return;

        if (sip_queue_may_resume(sip)) {
                /* wakeup upper queue only if we have sufficient credits */
                esp_sip_dbg(ESP_DBG_TRACE, "%s wakeup ieee80211 txq \n", __func__);
                atomic_set(&sip->epub->txq_stopped, false);
                ieee80211_wake_queues(sip->epub->hw);
        } else if (atomic_read(&sip->epub->txq_stopped) ) {
                esp_sip_dbg(ESP_DBG_TRACE, "%s can't wake txq, credits: %d \n", __func__, atomic_read(&sip->tx_credits) );
        }

        if (!skb_queue_empty(&sip->epub->txq)) {
                /* try to send out pkt already in sip queue once we have credits */
                esp_sip_dbg(ESP_DBG_TRACE, "%s resume sip txq \n", __func__);
#if !defined(FPGA_LOOPBACK) && !defined(FPGA_TXDATA) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
                ieee80211_queue_work(sip->epub->hw, &sip->epub->tx_work);
#else
                queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
#endif
        }
}

static bool sip_ampdu_occupy_buf(struct esp_sip *sip, struct esp_rx_ampdu_len * ampdu_len)
{
        return (ampdu_len->substate == 0 || esp_wmac_rxsec_error(ampdu_len->substate) || (sip->dump_rpbm_err && ampdu_len->substate == RX_RPBM_ERR));
}

#ifdef RX_SYNC
static void sip_rx_pkt_process_sync(struct esp_sip *sip, struct sk_buff *skb)
#else
static bool sip_rx_pkt_process(struct esp_sip * sip, struct sk_buff *skb)
#endif /* RX_SYNC */
{
#define DO_NOT_COPY false
#define DO_COPY true

	struct sip_hdr * hdr = NULL;
	struct sk_buff * rskb = NULL;
	int remains_len = 0;
	int first_pkt_len = 0;
	u8 *bufptr = NULL;
	int ret = 0;
	bool trigger_rxq = false;
#ifdef RX_SYNC
	bool trigger_txq = false;
#endif/* RX_SYNC */

	if (skb == NULL) {
		esp_sip_dbg(ESP_DBG_ERROR, "%s NULL SKB!!!!!!!! \n", __func__);
#ifdef RX_SYNC
		return;
#else
		return trigger_rxq;
#endif /* RX_SYNC */
	}

	hdr = (struct sip_hdr *)skb->data;
	bufptr = skb->data;


	esp_sip_dbg(ESP_DBG_TRACE, "%s Hcredits 0x%08x, realCredits %d\n", __func__, hdr->h_credits, hdr->h_credits & SIP_CREDITS_MASK);
	if (hdr->h_credits & SIP_CREDITS_MASK) {
		sip_update_tx_credits(sip, hdr->h_credits & SIP_CREDITS_MASK);
#ifdef RX_SYNC
		trigger_txq = true;
#endif/* RX_SYNC */
	}

	hdr->h_credits &= ~SIP_CREDITS_MASK; /* clean credits in sip_hdr, prevent over-add */

	esp_sip_dbg(ESP_DBG_TRACE, "%s credits %d\n", __func__, hdr->h_credits);

	/*
	 * first pkt's length is stored in  recycled_credits first 20 bits
	 * config w3 [31:12]
	 * repair hdr->len of first pkt
	 */
	remains_len = hdr->len;
	first_pkt_len = hdr->h_credits >> 12;
	hdr->len = first_pkt_len;

	esp_dbg(ESP_DBG_TRACE, "%s first_pkt_len %d, whole pkt len %d \n", __func__, first_pkt_len, remains_len);
	if (first_pkt_len > remains_len) {
		printk("first_pkt_len %d, whole pkt len %d\n", first_pkt_len, remains_len);
		show_buf((u8 *)hdr, first_pkt_len);
		ASSERT(0);
	}

	/*
	 * pkts handling, including the first pkt, should alloc new skb for each data pkt.
	 * free the original whole skb after parsing is done.
	 */
	while (remains_len) {
		ASSERT(remains_len >= sizeof(struct sip_hdr));
		hdr = (struct sip_hdr *)bufptr;
		ASSERT(hdr->len > 0);
		if((hdr->len & 3) != 0) {
			show_buf((u8 *)hdr, 512);
		}
		ASSERT((hdr->len & 3) == 0);
		if (unlikely(hdr->seq != sip->rxseq++)) {
			esp_dbg(ESP_DBG_ERROR, "%s seq mismatch! got %u, expect %u\n", __func__, hdr->seq, sip->rxseq-1);
			show_buf(bufptr, 32);
			ASSERT(0);
		}

		if (SIP_HDR_IS_CTRL(hdr)) {
			STRACE_RX_EVENT_INC();
			esp_sip_dbg(ESP_DBG_TRACE, "seq %u \n", hdr->seq);

			ret = sip_parse_events(sip, bufptr);

			skb_pull(skb, hdr->len);

		} else if (SIP_HDR_IS_DATA(hdr)) {
			struct esp_mac_rx_ctrl * mac_ctrl = NULL;
			int pkt_len_enc = 0, buf_len = 0, pulled_len = 0;

			STRACE_RX_DATA_INC();
			esp_sip_dbg(ESP_DBG_TRACE, "seq %u \n", hdr->seq);
			mac_ctrl = sip_parse_normal_mac_ctrl(skb, &pkt_len_enc, &buf_len, &pulled_len);
			rskb = sip_parse_data_rx_info(sip, skb, pkt_len_enc, buf_len, mac_ctrl, &pulled_len);

			if(rskb == NULL)
				goto _move_on;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
			sip_check_skb_alignment(rskb);
#endif /* !NEW_KERNEL */
			if (likely(atomic_read(&sip->epub->wl.off) == 0)) {
#ifndef RX_SENDUP_SYNC
				skb_queue_tail(&sip->epub->rxq, rskb);
				trigger_rxq = true;
#else
#ifdef RX_CHECKSUM_TEST
				esp_rx_checksum_test(rskb);
#endif
				local_bh_disable();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
				ieee80211_rx(sip->epub->hw, rskb);
#else
                //simulate IEEE80211_SKB_RXCB in 2.6.32 
                ieee80211_rx(sip->epub->hw, rskb ,(struct ieee80211_rx_status *)rskb->cb);
#endif
				local_bh_enable();
#endif /* RX_SENDUP_SYNC */
			} else {
				/* still need go thro parsing as skb_pull should invoke */
				kfree_skb(rskb);
			}
		} else if (SIP_HDR_IS_AMPDU(hdr)) {
			struct esp_mac_rx_ctrl * mac_ctrl = NULL;
			struct esp_mac_rx_ctrl new_mac_ctrl;
			struct esp_rx_ampdu_len *ampdu_len;
			int pkt_num;
			int pulled_len = 0;
			static int pkt_dropped = 0;
			static int pkt_total = 0;
			bool have_rxabort = false;
			bool have_goodpkt = false;
			static u8 frame_head[16];
			static u8 frame_buf_ttl = 0;

			ampdu_len = (struct esp_rx_ampdu_len *)(skb->data + hdr->len/sip->rx_blksz * sip->rx_blksz);
			esp_sip_dbg(ESP_DBG_TRACE, "%s rx ampdu total len %u\n", __func__, hdr->len);
			if(skb->data != (u8 *)hdr) {
				printk("%p %p\n", skb->data, hdr);
				show_buf(skb->data, 512);
				show_buf((u8 *)hdr, 512);
			}
			ASSERT(skb->data == (u8 *)hdr);
			mac_ctrl = sip_parse_normal_mac_ctrl(skb, NULL, NULL, &pulled_len);
			memcpy(&new_mac_ctrl, mac_ctrl, sizeof(struct esp_mac_rx_ctrl));
			mac_ctrl = &new_mac_ctrl;
			pkt_num = mac_ctrl->ampdu_cnt;
			esp_sip_dbg(ESP_DBG_TRACE, "%s %d rx ampdu %u pkts, %d pkts dumped, first len %u\n",
					__func__, __LINE__, (unsigned int)((hdr->len % sip->rx_blksz) / sizeof(struct esp_rx_ampdu_len)), pkt_num, (unsigned int)ampdu_len->sublen);

			pkt_total += mac_ctrl->ampdu_cnt;
			//esp_sip_dbg(ESP_DBG_ERROR, "%s ampdu dropped %d/%d\n", __func__, pkt_dropped, pkt_total);
			while (pkt_num > 0) {
				esp_sip_dbg(ESP_DBG_TRACE, "%s %d ampdu sub state %02x,\n", __func__, __LINE__, ampdu_len->substate);

				if (sip_ampdu_occupy_buf(sip, ampdu_len)) { //pkt is dumped

					rskb = sip_parse_data_rx_info(sip, skb, ampdu_len->sublen - FCS_LEN, 0, mac_ctrl, &pulled_len);
					assert(rskb != NULL);

					if (likely(atomic_read(&sip->epub->wl.off) == 0) &&
							(ampdu_len->substate == 0 || ampdu_len->substate == RX_TKIPMIC_ERR ||
							 (sip->sendup_rpbm_pkt && ampdu_len->substate == RX_RPBM_ERR)) &&
							(sip->rxabort_fixed || !have_rxabort) ) 
					{
						if(!have_goodpkt) {
							have_goodpkt = true;
							memcpy(frame_head, rskb->data, 16);
							frame_head[1] &= ~0x80;
							frame_buf_ttl = 3;
						}
#ifndef RX_SENDUP_SYNC
						skb_queue_tail(&sip->epub->rxq, rskb);
						trigger_rxq = true;
#else
#ifdef RX_CHECKSUM_TEST
						esp_rx_checksum_test(rskb);
#endif
						local_bh_disable();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
				ieee80211_rx(sip->epub->hw, rskb);
#else
                //simulate IEEE80211_SKB_RXCB in 2.6.32 
                ieee80211_rx(sip->epub->hw, rskb ,(struct ieee80211_rx_status *)rskb->cb);
#endif
						local_bh_enable();
#endif /* RX_SENDUP_SYNC */

					} else {
						kfree_skb(rskb);
					}
				} else {
					if (ampdu_len->substate == RX_ABORT) {
						u8 * a;
						have_rxabort = true;
						esp_sip_dbg(ESP_DBG_TRACE, "rx abort %d %d\n", frame_buf_ttl, pkt_num);
						if(frame_buf_ttl && !sip->rxabort_fixed) {
							struct esp_rx_ampdu_len * next_good_ampdu_len = ampdu_len + 1;
							a = frame_head;
							esp_sip_dbg(ESP_DBG_TRACE, "frame:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
									a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
							while(!sip_ampdu_occupy_buf(sip, next_good_ampdu_len)) {
								if(next_good_ampdu_len > ampdu_len + pkt_num - 1)
									break;
								next_good_ampdu_len++;

							}
							if(next_good_ampdu_len <= ampdu_len + pkt_num -1) {
								bool b0, b10, b11;
								a = skb->data;
								esp_sip_dbg(ESP_DBG_TRACE, "buf:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
										a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
								b0 = memcmp(frame_head + 4, skb->data + 4, 12) == 0;
								b10 = memcmp(frame_head + 10, skb->data, 6) == 0;
								b11 = memcpy(frame_head + 11, skb->data, 5) == 0;
								esp_sip_dbg(ESP_DBG_TRACE, "com %d %d %d\n", b0, b10, b11);
								if(b0 && !b10 && !b11) {
									have_rxabort = false;
									esp_sip_dbg(ESP_DBG_TRACE, "repair 0\n");
								} else if(!b0 && b10 && !b11) {
									skb_push(skb, 10);
									memcpy(skb->data, frame_head, 10);
									have_rxabort = false;
									pulled_len -= 10;
									esp_sip_dbg(ESP_DBG_TRACE, "repair 10\n");
								} else if(!b0 && !b10 && b11) {
									skb_push(skb, 11);
									memcpy(skb->data, frame_head, 11);
									have_rxabort = false;
									pulled_len -= 11;
									esp_sip_dbg(ESP_DBG_TRACE, "repair 11\n");
								}
							}
						}
					}
					pkt_dropped++;
					esp_sip_dbg(ESP_DBG_LOG, "%s ampdu dropped %d/%d\n", __func__, pkt_dropped, pkt_total);
				}
				pkt_num--;
				ampdu_len++;
			}
			if(frame_buf_ttl)
				frame_buf_ttl--;
			skb_pull(skb, hdr->len - pulled_len);
		} else {
			esp_sip_dbg(ESP_DBG_ERROR, "%s %d unknown type\n", __func__, __LINE__);
		}

_move_on:
		if (hdr->len < remains_len) {
			remains_len -= hdr->len;
		} else {
			break;
		}
		bufptr += hdr->len;
	}

	do {
		ASSERT(skb != rskb);
		kfree_skb(skb);
	} while (0);

#ifdef RX_SYNC
	if (trigger_rxq) {
		queue_work(sip->epub->esp_wkq, &sip->epub->sendup_work);
	}
	if (trigger_txq) {
		sip_trigger_txq_process(sip);
	}
#else
	return trigger_rxq;
#endif /* RX_SYNC */

#undef DO_NOT_COPY
#undef DO_COPY
}

#ifndef RX_SYNC
static void _sip_rxq_process(struct esp_sip *sip)
{
        struct sk_buff *skb = NULL;
        bool sendup = false;

        while ((skb = skb_dequeue(&sip->rxq))) {
                if (sip_rx_pkt_process(sip, skb))
                        sendup = true;
        }
#ifndef RX_SENDUP_SYNC
        if (sendup) {
                queue_work(sip->epub->esp_wkq, &sip->epub->sendup_work);
        }
#endif /* !RX_SENDUP_SYNC */

        /* probably tx_credit is updated, try txq */
        sip_trigger_txq_process(sip);
}

void sip_rxq_process(struct work_struct *work)
{
        struct esp_sip *sip = container_of(work, struct esp_sip, rx_process_work);
        ASSERT(sip != NULL);

	if (unlikely(atomic_read(&sip->state) == SIP_SEND_INIT)) {
		sip_send_chip_init(sip);
		atomic_set(&sip->state, SIP_WAIT_BOOTUP);
		return;
	}

        spin_lock(&sip->rx_lock);
        _sip_rxq_process(sip);
        spin_unlock(&sip->rx_lock);
}

static inline void sip_rx_pkt_enqueue(struct esp_sip *sip, struct sk_buff *skb)
{
        skb_queue_tail(&sip->rxq, skb);
}

static inline struct sk_buff * sip_rx_pkt_dequeue(struct esp_sip *sip) {
        return skb_dequeue(&sip->rxq);
}
#endif /* RX_SYNC */

static u32 sip_rx_count = 0;
void sip_debug_show(struct esp_sip *sip)
{
	esp_sip_dbg(ESP_DBG_ERROR, "txq left %d %d\n", skb_queue_len(&sip->epub->txq), atomic_read(&sip->tx_data_pkt_queued));
	esp_sip_dbg(ESP_DBG_ERROR, "tx queues stop ? %d\n", atomic_read(&sip->epub->txq_stopped));
	esp_sip_dbg(ESP_DBG_ERROR, "txq stop?  %d\n", test_bit(ESP_WL_FLAG_STOP_TXQ, &sip->epub->wl.flags));
	esp_sip_dbg(ESP_DBG_ERROR, "tx credit %d\n", atomic_read(&sip->tx_credits));
	esp_sip_dbg(ESP_DBG_ERROR, "rx collect %d\n", sip_rx_count);
	sip_rx_count = 0;
}

#ifndef LOOKAHEAD
int sip_rx(struct esp_pub *epub)
{
        struct sip_hdr *shdr = NULL;
        struct esp_sip *sip = epub->sip;
        int err = 0;
        struct sk_buff *first_skb = NULL;
        struct sk_buff *next_skb = NULL;
        u8 *rx_buf = NULL;
        u32 rx_blksz;
        u32 first_sz = 4;
        struct sk_buff *rx_skb = NULL;

        esp_sip_dbg(ESP_DBG_LOG, "%s enter\n", __func__);


        /* first read one block out, if we luck enough, that's it
         *
         *  To make design as simple as possible, we allocate skb(s)
         *  separately for each sif read operation to avoid global
         *  read_buf_pointe access.  It coule be optimized late.
         */
        rx_blksz = sif_get_blksz(epub);
        first_skb = __dev_alloc_skb(first_sz, GFP_KERNEL);
        if (first_skb == NULL) {
                esp_sip_dbg(ESP_DBG_ERROR, "%s first no memory \n", __func__);
                goto _err;
        }
        rx_buf = skb_put(first_skb, first_sz);
        esp_sip_dbg(ESP_DBG_LOG, "%s rx_buf ptr %p, first_sz %d\n", __func__, rx_buf, first_sz);

        sif_lock_bus(epub);

#ifdef USE_EXT_GPIO
	do{
		int err2;
		u16 value = 0;
		u16 intr_mask = ext_gpio_get_int_mask_reg();
		if(!intr_mask)
			break;
		err2 = sif_get_gpio_intr(epub, intr_mask, &value);
		if(!err2 && value) {
            		esp_sip_dbg(ESP_DBG_TRACE, "%s intr_mask[0x%04x] value[0x%04x]\n", __func__, intr_mask, value);
			ext_gpio_int_process(value);
		}
	}while(0);
#endif
#ifdef ESP_ACK_INTERRUPT
#ifdef ESP_ACK_LATER
		err = esp_common_read(epub, rx_buf, first_sz, ESP_SIF_NOSYNC, true);
        sif_platform_ack_interrupt(epub);
#else
        sif_platform_ack_interrupt(epub);
		err = esp_common_read(epub, rx_buf, first_sz, ESP_SIF_NOSYNC, true);
#endif /* ESP_ACK_LATER */
#else
        err = esp_common_read(epub, rx_buf, first_sz, ESP_SIF_NOSYNC, true);
#endif //ESP_ACK_INTERRUPT

	sip_rx_count++;
        if (unlikely(err)) {
                esp_dbg(ESP_DBG_ERROR, " %s first read err %d %d\n", __func__, err, sif_get_regs(epub)->config_w0);
                kfree_skb(first_skb);
        	sif_unlock_bus(epub);
                goto _err;
        }

        shdr = (struct sip_hdr *)rx_buf;
	if(SIP_HDR_IS_CTRL(shdr) && (shdr->c_evtid == SIP_EVT_SLEEP)) {
		atomic_set(&sip->epub->ps.state, ESP_PM_ON);
		esp_dbg(ESP_DBG_TRACE, "s\n");
	}

        ASSERT((shdr->len & 3) == 0);
        //ASSERT(shdr->len >= SIP_HDR_LEN && shdr->len <= SIP_PKT_MAX_LEN);
        if (shdr->len > first_sz)  {
                /* larger than one blk, fetch the rest */
                next_skb = __dev_alloc_skb(roundup(shdr->len, rx_blksz) + first_sz, GFP_KERNEL);
                if (unlikely(next_skb == NULL)) {
                        sif_unlock_bus(epub);
                        esp_sip_dbg(ESP_DBG_ERROR, "%s next no memory \n", __func__);
                        kfree_skb(first_skb);
                        goto _err;
                }
                rx_buf = skb_put(next_skb, shdr->len);
                rx_buf += first_sz; /* skip the first block */

        	err = esp_common_read(epub, rx_buf, (shdr->len - first_sz), ESP_SIF_NOSYNC, false);
        	sif_unlock_bus(epub);

                if (unlikely(err)) {
                        esp_sip_dbg(ESP_DBG_ERROR, "%s next read err %d \n", __func__, err);
                        kfree(first_skb);
                        kfree(next_skb);
                        goto _err;
                }
                /* merge two skbs, TBD: could be optimized by skb_linearize*/
                memcpy(next_skb->data, first_skb->data, first_sz);
                esp_dbg(ESP_DBG_TRACE, " %s  next skb\n", __func__);

                rx_skb = next_skb;
                kfree_skb(first_skb);
        } else {
		sif_unlock_bus(epub);
                skb_trim(first_skb, shdr->len);
                esp_dbg(ESP_DBG_TRACE, " %s first_skb only\n", __func__);

                rx_skb = first_skb;
        }
        if (atomic_read(&sip->state) == SIP_STOP) {
		kfree_skb(rx_skb);
                esp_sip_dbg(ESP_DBG_ERROR, "%s when sip stopped\n", __func__);
                return 0;
        }
#ifndef RX_SYNC
        sip_rx_pkt_enqueue(sip, rx_skb);
        queue_work(sip->epub->esp_wkq, &sip->rx_process_work);
#else
        sip_rx_pkt_process_sync(sip, rx_skb);
#endif /* RX_SYNC */

_err:

        return err;
}

#else

int sip_rx(struct esp_pub *epub)
{
        struct sip_hdr *shdr = NULL;
        struct esp_sip *sip = epub->sip;
        int err = 0;
        struct sk_buff *rx_skb = NULL;
        u8 *rx_buf = NULL;
        u32 lookahead = 0;
        struct slc_host_regs *regs = sif_get_regs(epub);
        bool free_skb = false;

        esp_sip_dbg(ESP_DBG_LOG, "%s enter\n", __func__);

        lookahead = regs->config_w0;

        esp_sip_dbg(ESP_DBG_TRACE, "%s lookahead %u \n", __func__, lookahead);
        rx_skb = __dev_alloc_skb(lookahead, GFP_KERNEL);
        if (rx_skb == NULL) {
                esp_sip_dbg(ESP_DBG_ERROR, "%s no memory \n", __func__);
                goto __out;
        }
        rx_buf = skb_put(rx_skb, lookahead);
        err = sif_lldesc_read_raw(epub, rx_buf, lookahead);
        if (unlikely(err)) {
                esp_dbg(ESP_DBG_ERROR, " %s read err %d \n", __func__, err);
                goto __out;
        }

        shdr = (struct sip_hdr *)rx_buf;
        esp_sip_dbg(ESP_DBG_TRACE, "%s len %d totlen %d\n", __func__, shdr->len, (shdr->h_credits >> 12));
        show_buf(rx_buf, 32);
        //ASSERT(shdr->len >= SIP_HDR_LEN && shdr->len <= SIP_PKT_MAX_LEN);

        esp_sip_dbg(ESP_DBG_TRACE, "%s credits %d\n", __func__, shdr->h_credits);
        if (shdr->h_credits & SIP_CREDITS_MASK) {
                sip_update_tx_credits(sip, shdr->h_credits & SIP_CREDITS_MASK);
        }
#if 0
        if (SIP_HDR_IS_CREDIT_RPT(shdr)) {
                esp_sip_dbg(ESP_DBG_TRACE, "%s credit update %d\n", __func__, shdr->h_credits & SIP_CREDITS_MASK);
                if (unlikely(shdr->seq != sip->rxseq++)) {
                        esp_sip_dbg(ESP_DBG_ERROR, "%s seq mismatch! got %u, expect %u\n", __func__, shdr->seq, sip->rxseq-1);
                }

                free_skb = true;
                goto __out;
        }
#endif
        shdr->h_credits &= ~SIP_CREDITS_MASK; /* clean credits in sip_hdr, prevent over-add */

        /* we need some place to store the total length of the pkt chain, the simple way is make it consistent with
         * non-lookahead case
         */
        shdr->h_credits = (shdr->len << 12);
        shdr->len = lookahead;

        sip_rx_pkt_enqueue(sip, rx_skb);
        queue_work(sip->epub->esp_wkq, &sip->rx_process_work);

__out:
        if (free_skb)
                kfree_skb(rx_skb);

        sip_trigger_txq_process(sip);

        return err;
}
#endif /* LOOKAHEAD */

int sip_get_raw_credits(struct esp_sip *sip)
{
#if 1
        unsigned long timeout;
        int err = 0;

        esp_dbg(ESP_DBG_TRACE, "%s entern \n", __func__);

        /* 1s timeout */
        timeout = jiffies + msecs_to_jiffies(1000);

        while (time_before(jiffies, timeout) && !sip->boot_credits) {

		err = esp_common_read_with_addr(sip->epub, SLC_HOST_TOKEN_RDATA, (u8 *)&sip->boot_credits, 4, ESP_SIF_SYNC);

                if (err) {
                        esp_dbg(ESP_DBG_ERROR, "Can't read credits\n");
                        return err;
                }
                sip->boot_credits &= SLC_HOST_TOKEN0_MASK;
#ifdef SIP_DEBUG
                if (sip->boot_credits == 0) {
                        esp_dbg(ESP_DBG_ERROR, "no credit, try again\n");
                        mdelay(50);
                }
#endif /* SIP_DEBUG */
        }

        if (!sip->boot_credits) {
                esp_dbg(ESP_DBG_ERROR, "read credits timeout\n");
                return -1;
        }

        esp_dbg(ESP_DBG_TRACE, "%s got credits: %d\n", __func__, sip->boot_credits);
#endif //0000

        return 0;
}


/* Only cooperate with get_raw_credits */
static void
sip_dec_credit(struct esp_sip *sip)
{
#if 0
        u32 reg = 0;
        int err = 0;

        reg = SLC_HOST_TOKEN0_WR | SLC_HOST_TOKEN0_DEC;
        memcpy(sip->rawbuf, &reg, sizeof(u32));
        err = sif_io_sync(sip->epub, SLC_HOST_INT_CLR, sip->rawbuf, sizeof(u32), SIF_TO_DEVICE | SIF_SYNC | SIF_BYTE_BASIS | SIF_INC_ADDR);

        if (err)
                esp_dbg(ESP_DBG_ERROR, "%s can't clear target token0 \n", __func__);

#endif //0000
        /* SLC 2.0, token reg is read-to-clean, thus no need to access target */
        sip->boot_credits--;
}

int sip_post_init(struct esp_sip *sip, struct sip_evt_bootup2 *bevt)
{
        struct esp_pub *epub;
        int po = 0;

        ASSERT(sip != NULL);

        epub = sip->epub;

        po = get_order(SIP_TX_AGGR_BUF_SIZE);
        sip->tx_aggr_buf = (u8 *)__get_free_pages(GFP_ATOMIC, po);
        if (sip->tx_aggr_buf == NULL) {
                esp_dbg(ESP_DBG_ERROR, "no mem for tx_aggr_buf! \n");
                return -ENOMEM;
        }
#if 0
        po = get_order(SIP_RX_AGGR_BUF_SIZE);
        sip->rx_aggr_buf = (u8 *)__get_free_pages(GFP_ATOMIC, po);

        if (sip->rx_aggr_buf == NULL) {
                po = get_order(SIP_TX_AGGR_BUF_SIZE);
                free_pages((unsigned long)sip->tx_aggr_buf, po);
                esp_dbg(ESP_DBG_ERROR, "no mem for rx_aggr_buf! \n");
                return -ENOMEM;
        }
#endif

        sip->tx_aggr_write_ptr = sip->tx_aggr_buf;

        sip->tx_blksz = bevt->tx_blksz;
        sip->rx_blksz = bevt->rx_blksz;
        sip->credit_to_reserve = bevt->credit_to_reserve;

        sip->dump_rpbm_err = (bevt->options & SIP_DUMP_RPBM_ERR);
        sip->rxabort_fixed = (bevt->options & SIP_RXABORT_FIXED);
        sip->support_bgscan = (bevt->options & SIP_SUPPORT_BGSCAN);

        sip->sendup_rpbm_pkt = sip->dump_rpbm_err && false;

        /* print out MAC addr... */
        memcpy(epub->mac_addr, bevt->mac_addr, ETH_ALEN);
	sip->noise_floor = bevt->noise_floor;

        esp_sip_dbg(ESP_DBG_TRACE, "%s tx_blksz %d rx_blksz %d mac addr %pM\n", __func__, sip->tx_blksz, sip->rx_blksz, epub->mac_addr);

       	return 0;
}

/* write pkts in aggr buf to target memory */
static void sip_write_pkts(struct esp_sip *sip, int pm_state)
{
        int tx_aggr_len = 0;
        struct sip_hdr *first_shdr = NULL;
	int err = 0;

        tx_aggr_len = sip->tx_aggr_write_ptr - sip->tx_aggr_buf;
        if (tx_aggr_len < sizeof(struct sip_hdr)) {
                printk("%s tx_aggr_len %d \n", __func__, tx_aggr_len);
                ASSERT(0);
        }
        //ASSERT(tx_aggr_len >= sizeof(struct sip_hdr));
        ASSERT((tx_aggr_len & 0x3) == 0);

        first_shdr = (struct sip_hdr *)sip->tx_aggr_buf;

        if (atomic_read(&sip->tx_credits) <= SIP_CREDITS_LOW_THRESHOLD) {
                first_shdr->fc[1] |= SIP_HDR_F_NEED_CRDT_RPT;
        }

        ASSERT(tx_aggr_len == sip->tx_tot_len);

        /* still use lock bus instead of sif_lldesc_write_sync since we want to protect several global varibles assignments */
        sif_lock_bus(sip->epub);

        /* add a dummy read in power saving mode, although target may not really in
         * sleep
         */
        //if (atomic_read(&sip->epub->ps.state) == ESP_PM_ON) {
	//	atomic_set(&sip->epub->ps.state, ESP_PM_OFF);
                sif_raw_dummy_read(sip->epub);
        //}
	//esp_dbg(ESP_DBG_ERROR, "%s write len %u\n", __func__, tx_aggr_len);

		err = esp_common_write(sip->epub, sip->tx_aggr_buf, tx_aggr_len, ESP_SIF_NOSYNC);

        sip->tx_aggr_write_ptr = sip->tx_aggr_buf;
        sip->tx_tot_len = 0;

#if 0
        if (pm_state == ESP_PM_TURNING_ON) {
                ASSERT(atomic_read(&sip->epub->ps.state) == ESP_PM_TURNING_ON);
                atomic_set(&sip->epub->ps.state, ESP_PM_ON);
        } else if (pm_state == ESP_PM_TURNING_OFF) {
                ASSERT(atomic_read(&sip->epub->ps.state) == ESP_PM_TURNING_OFF);
                atomic_set(&sip->epub->ps.state, ESP_PM_OFF);
                esp_dbg(ESP_DBG_PS, "----beacon %d, probe rsp %d -------\n", bcn_counter, probe_rsp_counter);
                bcn_counter = 0; probe_rsp_counter = 0;
        } else if (pm_state && atomic_read(&sip->epub->ps.state)) {
                esp_dbg(ESP_DBG_ERROR, "%s wrong pm_state %d ps.state %d \n", __func__, pm_state, atomic_read(&sip->epub->ps.state));
        }
#endif
        sif_unlock_bus(sip->epub);

	if (err)
		esp_sip_dbg(ESP_DBG_ERROR, "func %s err!!!!!!!!!: %d\n", __func__, err);

}

/* setup sip header and tx info, copy pkt into aggr buf */
static int sip_pack_pkt(struct esp_sip *sip, struct sk_buff *skb, int *pm_state)
{
        struct ieee80211_tx_info *itx_info;
        struct sip_hdr *shdr;
        u32 tx_len = 0, offset = 0;
        bool is_data = true;

        itx_info = IEEE80211_SKB_CB(skb);

        if (itx_info->flags == 0xffffffff) {
                shdr = (struct sip_hdr *)skb->data;
                is_data = false;
                tx_len = skb->len;
        } else {
                struct ieee80211_hdr * wh = (struct ieee80211_hdr *)skb->data;
                struct esp_vif *evif = (struct esp_vif *)itx_info->control.vif->drv_priv;
                u8 sta_index;
                struct esp_node *node;	
                /* update sip header */
                shdr = (struct sip_hdr *)sip->tx_aggr_write_ptr;
                
                shdr->fc[0] = 0;
                shdr->fc[1] = 0;

                if ((itx_info->flags & IEEE80211_TX_CTL_AMPDU) && (true || esp_is_ip_pkt(skb)))
                        SIP_HDR_SET_TYPE(shdr->fc[0], SIP_DATA_AMPDU);
                else
                        SIP_HDR_SET_TYPE(shdr->fc[0], SIP_DATA);

		if(evif->epub == NULL){
#ifndef FAST_TX_STATUS
			/* TBD */
#else
			sip_tx_status_report(sip, skb, itx_info, false);
			atomic_dec(&sip->tx_data_pkt_queued);
			return -1;
#endif /* FAST_TX_STATUS */
		}

                /* make room for encrypted pkt */
                if (itx_info->control.hw_key) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
                        shdr->d_enc_flag= itx_info->control.hw_key->alg+1;
#else
                        int alg = esp_cipher2alg(itx_info->control.hw_key->cipher);
                        if (unlikely(alg == -1)) {
#ifndef FAST_TX_STATUS
                                /* TBD */
#else
                                sip_tx_status_report(sip, skb, itx_info, false);
                                atomic_dec(&sip->tx_data_pkt_queued);
                                return -1;
#endif /* FAST_TX_STATUS */
                        } else {
                                shdr->d_enc_flag = alg + 1;
                        }

#endif /* NEW_KERNEL */
                         shdr->d_hw_kid =  itx_info->control.hw_key->hw_key_idx | (evif->index<<7);
                } else {
                        shdr->d_enc_flag=0;
                        shdr->d_hw_kid = (evif->index << 7 | evif->index);
                }

                /* update sip tx info */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
               /* if(itx_info->control.sta == NULL){
                        node = NULL;
                } else {
                        node = esp_get_node_by_addr(sip->epub, itx_info->control.sta->addr);
                }*/
#else
		
                node = esp_get_node_by_addr(sip->epub, wh->addr1);
#endif
                if(node != NULL)
                        sta_index = node->index;
                else
                        sta_index = ESP_PUB_MAX_STA + 1;
                SIP_HDR_SET_IFIDX(shdr->fc[0], evif->index << 3 | sta_index);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
                shdr->d_p2p = itx_info->control.vif->p2p;
                if(evif->index == 1)
                        shdr->d_p2p = 1;
#endif
                shdr->d_ac = skb_get_queue_mapping(skb);
                shdr->d_tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
                wh = (struct ieee80211_hdr *)skb->data;
                if (ieee80211_is_mgmt(wh->frame_control)) {
		/* addba/delba/bar may use different tid/ac */
                        if (ieee80211_is_beacon(wh->frame_control)||shdr->d_ac == WME_AC_VO) {
                                shdr->d_tid = 7;
                        }
                }
//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
                if (check_ac_tid(skb->data, shdr->d_ac, shdr->d_tid)) {
                        shdr->d_ac = WME_AC_BE;
                        shdr->d_tid = 0;
                }
//#endif  /* NEW_KERNEL || KERNEL_35 */


                /* make sure data is start at 4 bytes aligned addr. */
                offset = roundup(sizeof(struct sip_hdr), 4);

#ifdef HOST_RC
                esp_sip_dbg(ESP_DBG_TRACE, "%s offset0 %d \n", __func__, offset);
                memcpy(sip->tx_aggr_write_ptr + offset, (void *)&itx_info->control,
                       sizeof(struct sip_tx_rc));

                offset += roundup(sizeof(struct sip_tx_rc), 4);
                esp_show_tx_rates(&itx_info->control.rates[0]);

#endif /* HOST_RC */

                if (SIP_HDR_IS_AMPDU(shdr)) {
                        memset(sip->tx_aggr_write_ptr + offset, 0, sizeof(struct esp_tx_ampdu_entry));
                        offset += roundup(sizeof(struct esp_tx_ampdu_entry), 4);
                }

                tx_len = offset + skb->len;
                shdr->len = tx_len;  /* actual len */

                esp_sip_dbg(ESP_DBG_TRACE, "%s offset %d skblen %d txlen %d\n", __func__, offset, skb->len, tx_len);

        }

        shdr->seq = sip->txseq++;
        //esp_sip_dbg(ESP_DBG_ERROR, "%s seq %u, %u %u\n", __func__, shdr->seq, SIP_HDR_GET_TYPE(shdr->fc[0]),shdr->c_cmdid);

        /* copy skb to aggr buf */
        memcpy(sip->tx_aggr_write_ptr + offset, skb->data, skb->len);

        if (is_data) {
			spin_lock_bh(&sip->epub->tx_lock);
			sip->txdataseq = shdr->seq;
			spin_unlock_bh(&sip->epub->tx_lock);
#ifndef FAST_TX_STATUS
                /* store seq in driver data, need seq to pick pkt during tx status report */
                *(u32 *)itx_info->driver_data = shdr->seq;
                atomic_inc(&sip->pending_tx_status);
#else
                /* fake a tx_status and report to mac80211 stack to speed up tx, may affect
                 *  1) rate control (now it's all in target, so should be OK)
                 *  2) ps mode, mac80211 want to check ACK of ps/nulldata to see if AP is awake
                 *  3) BAR, mac80211 do BAR by checking ACK
                 */
                /*
                 *  XXX: need to adjust for 11n, e.g. report tx_status according to BA received in target
                 *
                 */
                sip_tx_status_report(sip, skb, itx_info, true);
                atomic_dec(&sip->tx_data_pkt_queued);

#endif /* FAST_TX_STATUS */
                STRACE_TX_DATA_INC();
        } else {
                /* check pm state here */
#if 0
                if (*pm_state == 0) {
                        if (SIP_HDR_IS_PM_TURNING_ON(shdr))
                                *pm_state = ESP_PM_TURNING_ON;
                        else if (SIP_HDR_IS_PM_TURNING_OFF(shdr))
                                *pm_state = ESP_PM_TURNING_OFF;
                } else {
                        if (SIP_HDR_IS_PM_TURNING_ON(shdr) || SIP_HDR_IS_PM_TURNING_OFF(shdr)) {
                                esp_dbg(ESP_DBG_ERROR, "%s two pm states in one bunch - prev: %d; curr %d\n", __func__,
                                        *pm_state, SIP_HDR_IS_PM_TURNING_ON(shdr)?ESP_PM_TURNING_ON:ESP_PM_TURNING_OFF);
                                if (*pm_state == ESP_PM_TURNING_ON && SIP_HDR_IS_PM_TURNING_OFF(shdr)) {
                                        *pm_state = 0;
                                        atomic_set(&sip->epub->ps.state, ESP_PM_OFF);
                                } else if (*pm_state == ESP_PM_TURNING_OFF && SIP_HDR_IS_PM_TURNING_ON(shdr)) {
                                        *pm_state = 0;
                                        atomic_set(&sip->epub->ps.state, ESP_PM_ON);
                                } else {
                                        ASSERT(0);
                                }
                        }
                }
#endif
                /* no need to hold ctrl skb */
                sip_free_ctrl_skbuff(sip, skb);
                STRACE_TX_CMD_INC();
        }

        /* TBD: roundup here or whole aggr-buf */
        tx_len = roundup(tx_len, sip->tx_blksz);

        sip->tx_aggr_write_ptr += tx_len;
        sip->tx_tot_len += tx_len;

        return 0;
}

#ifndef FAST_TX_STATUS
static void
sip_after_tx_status_update(struct esp_sip *sip)
{
        if (atomic_read(&sip->data_tx_stopped) == true && sip_tx_data_may_resume(sip)) {
                atomic_set(&sip->data_tx_stopped, false);
                if (sip_is_tx_mblk_avail(sip) == false) {
                        esp_sip_dbg(ESP_DBG_ERROR, "%s mblk still unavail \n", __func__);
                } else {
                        esp_sip_dbg(ESP_DBG_TRACE, "%s trigger txq \n", __func__);
                        sip_trigger_txq_process(sip);
                }
        } else if (!sip_tx_data_may_resume(sip)) { //JLU: this is redundant
                STRACE_SHOW(sip);
        }
}
#endif /* !FAST_TX_STATUS */

#ifdef HOST_RC
static void sip_set_tx_rate_status(struct sip_rc_status *rcstatus, struct ieee80211_tx_rate *irates)
{
        int i;
        u8 shift = 0;
        u32 cnt = 0;

        for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
                if (rcstatus->rc_map & BIT(i)) {
                        shift = i << 2;
                        cnt = (rcstatus->rc_cnt_store >> shift) & RC_CNT_MASK;
                        irates[i].idx = i;
                        irates[i].count = (u8)cnt;
                } else {
                        irates[i].idx = -1;
                        irates[i].count = 0;
                }
        }

        esp_show_rcstatus(rcstatus);
        esp_show_tx_rates(irates);
}
#endif /* HOST_RC */

#ifndef FAST_TX_STATUS
static void
sip_txdoneq_process(struct esp_sip *sip, struct sip_evt_tx_report *tx_report)
{
        struct sk_buff *skb, *tmp;
        struct esp_pub *epub = sip->epub;
        int matchs = 0;
        struct ieee80211_tx_info *tx_info;
        struct sip_tx_status *tx_status;
        int i;

        esp_sip_dbg(ESP_DBG_LOG, "%s enter, report->pkts %d, pending tx_status %d\n", __func__, tx_report->pkts, atomic_read(&sip->pending_tx_status));

        /* traversal the txdone queue, find out matched skb by seq, hand over
         * to up layer stack
         */
        for (i = 0; i < tx_report->pkts; i++) {
                //esp_sip_dbg(ESP_DBG_TRACE, "%s status %d seq %u\n", __func__, i, tx_report->status[i].sip_seq);
                skb_queue_walk_safe(&epub->txdoneq, skb, tmp) {
                        tx_info = IEEE80211_SKB_CB(skb);

                        //esp_sip_dbg(ESP_DBG_TRACE, "%s skb seq %u\n", __func__, *(u32 *)tx_info->driver_data);
                        if (tx_report->status[i].sip_seq == *(u32 *)tx_info->driver_data) {
                                tx_status = &tx_report->status[i];
                                __skb_unlink(skb, &epub->txdoneq);

                                //fill up ieee80211_tx_info
                                //TBD: lock ??
                                if (tx_status->errno == SIP_TX_ST_OK &&
                                    !(tx_info->flags & IEEE80211_TX_CTL_NO_ACK)) {
                                        tx_info->flags |= IEEE80211_TX_STAT_ACK;
                                }
#ifdef HOST_RC
                                sip_set_tx_rate_status(&tx_report->status[i].rcstatus, &tx_info->status.rates[0]);
                                esp_sip_dbg(ESP_DBG_TRACE, "%s idx0 %d, cnt0 %d, flags0 0x%02x\n", __func__, tx_info->status.rates[0].idx,tx_info->status.rates[0].count, tx_info->status.rates[0].flags);

#else
                                /* manipulate rate status... */
                                tx_info->status.rates[0].idx = 0;
                                tx_info->status.rates[0].count = 1;
                                tx_info->status.rates[0].flags = 0;
                                tx_info->status.rates[1].idx = -1;
#endif /* HOST_RC */

                                ieee80211_tx_status(epub->hw, skb);
                                matchs++;
                                atomic_dec(&sip->pending_tx_status);
                                STRACE_RX_TXSTATUS_INC();
                        }
                }
        }

        if (matchs < tx_report->pkts) {
                esp_sip_dbg(ESP_DBG_ERROR, "%s tx report mismatch! \n", __func__);
        } else {
                //esp_sip_dbg(ESP_DBG_TRACE, "%s tx report %d pkts! \n", __func__, matchs);
        }

        sip_after_tx_status_update(sip);
}
#else
#ifndef FAST_TX_NOWAIT

static void
sip_txdoneq_process(struct esp_sip *sip)
{
        struct esp_pub *epub = sip->epub;
        struct sk_buff *skb;
        while ((skb = skb_dequeue(&epub->txdoneq))) {
                ieee80211_tx_status(epub->hw, skb);
        }
}
#endif
#endif /* !FAST_TX_STATUS */

#ifdef FAST_TX_STATUS
static void sip_tx_status_report(struct esp_sip *sip, struct sk_buff *skb, struct ieee80211_tx_info *tx_info, bool success)
{
        if(!(tx_info->flags & IEEE80211_TX_CTL_AMPDU)) {
                if (likely(success))
                        tx_info->flags |= IEEE80211_TX_STAT_ACK;
                else
                        tx_info->flags &= ~IEEE80211_TX_STAT_ACK;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
                /* manipulate rate status... */
                tx_info->status.rates[0].idx = 11;
                tx_info->status.rates[0].count = 1;
                tx_info->status.rates[0].flags = 0;
                tx_info->status.rates[1].idx = -1;
#else
                tx_info->status.retry_count = 1;
                tx_info->status.excessive_retries = false;
#endif

        } else {
                tx_info->flags |= IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_STAT_ACK;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
                tx_info->status.ampdu_ack_map = 1;
#else
                tx_info->status.ampdu_len = 1;
#endif
                tx_info->status.ampdu_ack_len = 1;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
                /* manipulate rate status... */
                tx_info->status.rates[0].idx = 7;
                tx_info->status.rates[0].count = 1;
                tx_info->status.rates[0].flags = IEEE80211_TX_RC_MCS | IEEE80211_TX_RC_SHORT_GI;
                tx_info->status.rates[1].idx = -1;
#else
                tx_info->status.retry_count = 1;
                tx_info->status.excessive_retries = false;
#endif

        }

        if(tx_info->flags & IEEE80211_TX_STAT_AMPDU)
                esp_sip_dbg(ESP_DBG_TRACE, "%s ampdu status! \n", __func__);

        if (!mod_support_no_txampdu() /*&&
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
                sip->epub->hw->conf.channel_type != NL80211_CHAN_NO_HT
#else
                !(sip->epub->hw->conf.flags&IEEE80211_CONF_SUPPORT_HT_MODE)
#endif*/ //add libing
                ) {
                struct ieee80211_tx_info * tx_info = IEEE80211_SKB_CB(skb);
                struct ieee80211_hdr * wh = (struct ieee80211_hdr *)skb->data;
                if(ieee80211_is_data_qos(wh->frame_control)) {
                        if(!(tx_info->flags & IEEE80211_TX_CTL_AMPDU)) {
                                u8 tidno = ieee80211_get_qos_ctl(wh)[0] & IEEE80211_QOS_CTL_TID_MASK;
                                struct esp_node * node;
                                struct esp_tx_tid *tid;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
 /*                               struct ieee80211_sta *sta;
                                sta = tx_info->control.sta;
                                if(sta == NULL)
                                        goto _exit;
                                node = (struct esp_node *)sta->drv_priv;
                                ASSERT(node != NULL);
                                if(node->sta == NULL) 
                                        goto _exit;*/ //add libing
#else
                                node = esp_get_node_by_addr(sip->epub, wh->addr1);
                                if(node == NULL)
                                        goto _exit;
#endif
                                tid = &node->tid[tidno];
                                spin_lock_bh(&sip->epub->tx_ampdu_lock);
                                //start session
                                ASSERT(tid != NULL);
                                if ((tid->state == ESP_TID_STATE_INIT) && 
						(TID_TO_AC(tidno) != WME_AC_VO) && tid->cnt >= 10) {
                                        tid->state = ESP_TID_STATE_TRIGGER;
                                        esp_sip_dbg(ESP_DBG_ERROR, "start tx ba session,addr:%pM,tid:%u\n", wh->addr1, tidno);
                                        spin_unlock_bh(&sip->epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                                        ieee80211_start_tx_ba_session(sip->epub->hw, wh->addr1, tidno);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32))
                                        ieee80211_start_tx_ba_session(sip->epub->hw, sta->addr, tidno);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 37))
                                        ieee80211_start_tx_ba_session(sta, tidno);
#else
                                       // ieee80211_start_tx_ba_session(sta, tidno, 0);
#endif
                                } else {
					if(tid->state == ESP_TID_STATE_INIT)
						tid->cnt++;
					else
						tid->cnt = 0;
                                        spin_unlock_bh(&sip->epub->tx_ampdu_lock);
                                }
                        }
                }
        }
/*_exit:
#ifndef FAST_TX_NOWAIT 
        skb_queue_tail(&sip->epub->txdoneq, skb);
#else
        ieee80211_tx_status(sip->epub->hw, skb);
#endif*/ //add libing
}
#endif /* FAST_TX_STATUS */

/*
 *  NB: this routine should be locked when calling
 */
void
sip_txq_process(struct esp_pub *epub)
{
        struct sk_buff *skb;
        struct esp_sip *sip = epub->sip;
        u32 pkt_len = 0, tx_len = 0, blknum = 0;
        bool queued_back = false;
        bool out_of_credits = false;
        struct ieee80211_tx_info *itx_info;
        int pm_state = 0;
	
        while ((skb = skb_dequeue(&epub->txq))) {

                /* cmd skb->len does not include sip_hdr too */
                pkt_len = skb->len;
                itx_info = IEEE80211_SKB_CB(skb);
                if (itx_info->flags != 0xffffffff) {
                        pkt_len += roundup(sizeof(struct sip_hdr), 4);
                        if ((itx_info->flags & IEEE80211_TX_CTL_AMPDU) && (true || esp_is_ip_pkt(skb)))
                                pkt_len += roundup(sizeof(struct esp_tx_ampdu_entry), 4);
                }

                /* current design simply requires every sip_hdr must be at the begin of mblk, that definitely
                 * need to be optimized, e.g. calulate remain length in the previous mblk, if it larger than
                 * certain threshold (e.g, whole pkt or > 50% of pkt or 2 x sizeof(struct sip_hdr), append pkt
                 * to the previous mblk.  This might be done in sip_pack_pkt()
                 */
                pkt_len = roundup(pkt_len, sip->tx_blksz);
                blknum = pkt_len / sip->tx_blksz;
                esp_dbg(ESP_DBG_TRACE, "%s skb_len %d pkt_len %d blknum %d\n", __func__, skb->len, pkt_len, blknum);

                if (unlikely(blknum > atomic_read(&sip->tx_credits) - sip->credit_to_reserve)) {
                        esp_dbg(ESP_DBG_TRACE, "%s out of credits!\n", __func__);
                        STRACE_TX_OUT_OF_CREDIT_INC();
                        queued_back = true;
                        out_of_credits = true;
			/*
                        if (epub->hw) {
                                ieee80211_stop_queues(epub->hw);
                                atomic_set(&epub->txq_stopped, true);
                        }
			*/
                        /* we will be back */
                        break;
                }

                tx_len += pkt_len;
                if (tx_len >= SIP_TX_AGGR_BUF_SIZE) {
                        /* do we need to have limitation likemax 8 pkts in a row? */
                        esp_dbg(ESP_DBG_TRACE, "%s too much pkts in one shot!\n", __func__);
                        STRACE_TX_ONE_SHOT_INC();
                        tx_len -= pkt_len;
                        queued_back = true;
                        break;
                }

                if (sip_pack_pkt(sip, skb, &pm_state) != 0) {
                        /* wrong pkt, won't send to target */
                        tx_len -= pkt_len;
                        continue;
                }

                esp_sip_dbg(ESP_DBG_TRACE, "%s:before sub, credits is %d\n", __func__, atomic_read(&sip->tx_credits));
                atomic_sub(blknum, &sip->tx_credits);
                esp_sip_dbg(ESP_DBG_TRACE, "%s:after sub %d,credits remains %d\n", __func__, blknum, atomic_read(&sip->tx_credits));

        }

        if (queued_back) {
                skb_queue_head(&epub->txq, skb);
        }

        if (atomic_read(&sip->state) == SIP_STOP 
#ifdef HOST_RESET_BUG
		|| atomic_read(&epub->wl.off) == 1
#endif
		)
	{
		queued_back = 1;
		tx_len = 0;
                sip_after_write_pkts(sip);
	}

        if (tx_len) {
	
		sip_write_pkts(sip, pm_state);

                sip_after_write_pkts(sip);
        }

        if (queued_back && !out_of_credits) {

                /* skb pending, do async process again */
                //	if (!skb_queue_empty(&epub->txq))
                sip_trigger_txq_process(sip);
        }
}

static void sip_after_write_pkts(struct esp_sip *sip)
{
        //enable txq
#if 0
        if (atomic_read(&sip->epub->txq_stopped) == true && sip_queue_may_resume(sip)) {
                atomic_set(&sip->epub->txq_stopped, false);
                ieee80211_wake_queues(sip->epub->hw);
                esp_sip_dbg(ESP_DBG_TRACE, "%s resume ieee80211 tx \n", __func__);
        }
#endif

#ifndef FAST_TX_NOWAIT
        sip_txdoneq_process(sip);
#endif
        //disable tx_data
#ifndef FAST_TX_STATUS
        if (atomic_read(&sip->data_tx_stopped) == false && sip_tx_data_need_stop(sip)) {
                esp_sip_dbg(ESP_DBG_TRACE, "%s data_tx_stopped \n", __func__);
                atomic_set(&sip->data_tx_stopped, true);
        }
#endif /* FAST_TX_STATUS */
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
/*
 * old mac80211 (2.6.32.x) needs payload is 4 byte aligned, thus we need this hack.
 * TBD: However, the latest mac80211 stack does not need this. we may
 * need to check kernel version here...
 */
static void sip_check_skb_alignment(struct sk_buff *skb)
{
        struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
        int hdrlen;

        hdrlen = ieee80211_hdrlen(hdr->frame_control);

#if 0

        /* TBD */
        if (rx->flags & IEEE80211_RX_AMSDU)
                hdrlen += ETH_HLEN;

#endif

        if (unlikely(((unsigned long)(skb->data + hdrlen)) & 3)) {

                esp_sip_dbg(ESP_DBG_TRACE, "%s adjust skb data postion \n", __func__);
                skb_push(skb, 2);
                memmove(skb->data, skb->data+2, skb->len-2);
                skb_trim(skb, skb->len-2);
        }
}
#endif /* !NEW_KERNEL */

int sip_channel_value_inconsistency(u8 *start, size_t len, unsigned channel)
{
        size_t left = len;
        u8 *pos = start;
        u8 *DS_Param = NULL;
        u8 channel_parsed = 0xff;
        bool found = false;
        u8 ssid[33];

        while(left >=2 && !found) {
                u8 id, elen;
                id = *pos++;
                elen = *pos++;
                left -= 2;

                if(elen > left)
                        break;

                switch (id) {
                case WLAN_EID_SSID:
                        ASSERT(elen < 33);
                        memcpy(ssid, pos, elen);
                        ssid[elen] = 0;
                        esp_sip_dbg(ESP_DBG_TRACE, "ssid:%s\n", ssid);
                        break;
                case WLAN_EID_SUPP_RATES:
                        break;
                case WLAN_EID_FH_PARAMS:
                        break;
                case WLAN_EID_DS_PARAMS:
                        DS_Param = pos;
                        found = true;
                        break;
                default:
                        break;
                }

                left -= elen;
                pos += elen;
        }

        if (DS_Param) {
                channel_parsed = DS_Param[0];
        } else {
                esp_dbg(ESP_DBG_ERROR, "DS_Param not found\n");
                return -1;
        }

        return channel_parsed != channel;
}

/*  parse mac_rx_ctrl and return length */
static int sip_parse_mac_rx_info(struct esp_sip *sip, struct esp_mac_rx_ctrl * mac_ctrl, struct sk_buff *skb)
{
        struct ieee80211_rx_status *rx_status = NULL;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
        rx_status = IEEE80211_SKB_RXCB(skb);
#else
        rx_status = (struct ieee80211_rx_status *)skb->cb;
#endif
        rx_status->freq = esp_ieee2mhz(mac_ctrl->channel);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
        rx_status->signal = mac_ctrl->rssi + sip->noise_floor;  /* snr actually, need to offset noise floor e.g. -85 */
#else
        rx_status->signal = mac_ctrl->rssi;  /* snr actually, need to offset noise floor e.g. -85 */
#endif /* NEW_KERNEL */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
#define ESP_RSSI_MIN_RSSI (-90)
#define ESP_RSSI_MAX_RSSI (-45)
        rx_status->noise = 0;  /* TBD */
        rx_status->qual = (mac_ctrl->rssi - ESP_RSSI_MIN_RSSI)* 100/(ESP_RSSI_MAX_RSSI - ESP_RSSI_MIN_RSSI);
        rx_status->qual = min(rx_status->qual, 100);
        rx_status->qual = max(rx_status->qual, 0);
#undef ESP_RSSI_MAX_RSSI
#undef ESP_RSSI_MIN_RSSI
#endif /* !NEW_KERNEL && KERNEL_35*/
        rx_status->antenna = 0;  /* one antenna for now */
        rx_status->band = IEEE80211_BAND_2GHZ;
        rx_status->flag = RX_FLAG_DECRYPTED | RX_FLAG_MMIC_STRIPPED;
        if (mac_ctrl->sig_mode) {
            // 2.6.27 has RX_FLAG_RADIOTAP in enum mac80211_rx_flags in include/net/mac80211.h
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))                
                rx_status->flag |= RX_FLAG_HT;
                rx_status->rate_idx = mac_ctrl->MCS;
                if(mac_ctrl->SGI)
                        rx_status->flag |= RX_FLAG_SHORT_GI;
#else
                rx_status->rate_idx = esp_wmac_rate2idx(0xc);//ESP_RATE_54
#endif
        } else {
                rx_status->rate_idx = esp_wmac_rate2idx(mac_ctrl->rate);
        }
        if (mac_ctrl->rxend_state == RX_FCS_ERR)
                rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

        /* Mic error frame flag */
        if (mac_ctrl->rxend_state == RX_TKIPMIC_ERR || mac_ctrl->rxend_state == RX_CCMPMIC_ERR){
                if(atomic_read(&sip->epub->wl.tkip_key_set) == 1){
			rx_status->flag|= RX_FLAG_MMIC_ERROR;
			atomic_set(&sip->epub->wl.tkip_key_set, 0);
			printk("mic err\n");
		} else {
			printk("mic err discard\n");
		}
	}

        //esp_dbg(ESP_DBG_LOG, "%s freq: %u; signal: %d;  rate_idx %d; flag: %d \n", __func__, rx_status->freq, rx_status->signal, rx_status->rate_idx, rx_status->flag);

        do {
                struct ieee80211_hdr * wh = (struct ieee80211_hdr *)((u8 *)skb->data);

                if (ieee80211_is_beacon(wh->frame_control) || ieee80211_is_probe_resp(wh->frame_control)) {
                        struct ieee80211_mgmt * mgmt = (struct ieee80211_mgmt *)((u8 *)skb->data);
                        u8 *start = NULL;
                        size_t baselen, len = skb->len;
                        int inconsistency = 0;

                        if (ieee80211_is_beacon(wh->frame_control)) {
                                start = mgmt->u.beacon.variable;
                                baselen = (u8 *) mgmt->u.beacon.variable - (u8 *) mgmt;
                                bcn_counter++;
                        } else if(ieee80211_is_probe_resp(wh->frame_control)) {
                                start = mgmt->u.probe_resp.variable;
                                baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
                                probe_rsp_counter++;
                        }

                        if (baselen > len)
                                return -1;

                        inconsistency = sip_channel_value_inconsistency(start, len-baselen, mac_ctrl->channel);

                        if (inconsistency) {
                                return -1;
                        }
                }

#ifdef KERNEL_IV_WAR
		/* some kernel e.g. 3.0.8 wrongly handles non-encrypted pkt like eapol */
		if (ieee80211_is_data(wh->frame_control)) {
                        if( !ieee80211_has_protected(wh->frame_control)) {
                                esp_sip_dbg(ESP_DBG_TRACE, "%s kiv_war, add iv_stripped flag \n", __func__);
                                rx_status->flag |= RX_FLAG_IV_STRIPPED;
                        } else {
                                if ((atomic_read(&sip->epub->wl.ptk_cnt) == 0 && !(wh->addr1[0] & 0x1)) || 
					(atomic_read(&sip->epub->wl.gtk_cnt) == 0 && (wh->addr1[0] & 0x1))) 
				{
                                        esp_dbg(ESP_DBG_TRACE, "%s ==kiv_war, got bogus enc pkt==\n", __func__);
                                        rx_status->flag |= RX_FLAG_IV_STRIPPED;
                                        //show_buf(skb->data, 32);
                                }

                                esp_sip_dbg(ESP_DBG_TRACE, "%s kiv_war, got enc pkt \n", __func__);
                        }
                }
#endif /* KERNEL_IV_WAR*/
        } while (0);

        return 0;
}

static struct esp_mac_rx_ctrl *sip_parse_normal_mac_ctrl(struct sk_buff *skb, int * pkt_len_enc, int *buf_len, int *pulled_len) 
{
        struct esp_mac_rx_ctrl *mac_ctrl = NULL;
        struct sip_hdr *hdr =(struct sip_hdr *)skb->data;
        int len_in_hdr = hdr->len;

        ASSERT(skb != NULL);
        ASSERT(skb->len > SIP_MIN_DATA_PKT_LEN);

        skb_pull(skb, sizeof(struct sip_hdr));
        *pulled_len += sizeof(struct sip_hdr);
        mac_ctrl = (struct esp_mac_rx_ctrl *)skb->data;
        if(!mac_ctrl->Aggregation) {
                ASSERT(pkt_len_enc != NULL);
                ASSERT(buf_len != NULL);
                *pkt_len_enc = (mac_ctrl->sig_mode?mac_ctrl->HT_length:mac_ctrl->legacy_length) - FCS_LEN;
                *buf_len = len_in_hdr - sizeof(struct sip_hdr) - sizeof(struct esp_mac_rx_ctrl);
        }
        skb_pull(skb, sizeof(struct esp_mac_rx_ctrl));
        *pulled_len += sizeof(struct esp_mac_rx_ctrl);

        return mac_ctrl;
}

/*
 * for one MPDU (including subframe in AMPDU)
 *
 */
static struct sk_buff * sip_parse_data_rx_info(struct esp_sip *sip, struct sk_buff *skb, int pkt_len_enc, int buf_len, struct esp_mac_rx_ctrl *mac_ctrl, int *pulled_len) {
        /*
         *   | mac_rx_ctrl | real_data_payload | ampdu_entries |
         */
        //without enc
        int pkt_len = 0;
        struct sk_buff *rskb = NULL;
        int ret;

        if (mac_ctrl->Aggregation) {
                struct ieee80211_hdr * wh = (struct ieee80211_hdr *)skb->data;
                pkt_len = pkt_len_enc;
                if (ieee80211_has_protected(wh->frame_control))//ampdu, it is CCMP enc
                        pkt_len -= 8;
                buf_len = roundup(pkt_len, 4);
        } else
                pkt_len  = buf_len - 3 + ((pkt_len_enc - 1) & 0x3);
        esp_dbg(ESP_DBG_TRACE, "%s pkt_len %u, pkt_len_enc %u!, delta %d \n", __func__, pkt_len, pkt_len_enc, pkt_len_enc - pkt_len);
        do {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
                rskb = __dev_alloc_skb(pkt_len_enc + 2, GFP_ATOMIC);
#else
                rskb = __dev_alloc_skb(pkt_len_enc, GFP_ATOMIC);
#endif/* NEW_KERNEL */
                if (unlikely(rskb == NULL)) {
                        esp_sip_dbg(ESP_DBG_ERROR, "%s no mem for rskb\n", __func__);
                        return NULL;
                }
                skb_put(rskb, pkt_len_enc);
        } while(0);

        do {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
                do {
                        struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
                        int hdrlen;

                        hdrlen = ieee80211_hdrlen(hdr->frame_control);
                        if (unlikely(((unsigned long)(rskb->data + hdrlen)) & 3)) {
                                skb_put(rskb, 2);
                                skb_pull(rskb, 2);
                        }
                } while(0);
#endif /* < KERNEL_VERSION(2, 6, 39) */
                memcpy(rskb->data, skb->data, pkt_len);
                if (pkt_len_enc > pkt_len) {
                        memset(rskb->data + pkt_len, 0, pkt_len_enc - pkt_len);
                }
                /* strip out current pkt, move to the next one */
                skb_pull(skb, buf_len);
                *pulled_len += buf_len;
        } while (0);

        ret = sip_parse_mac_rx_info(sip, mac_ctrl, rskb);
        if(ret == -1 && !mac_ctrl->Aggregation) {
                kfree_skb(rskb);
                return NULL;
        }

        esp_dbg(ESP_DBG_LOG, "%s after pull headers, skb->len %d rskb->len %d \n", __func__, skb->len, rskb->len);

        return rskb;
}

struct esp_sip * sip_attach(struct esp_pub *epub) 
{
        struct esp_sip *sip = NULL;
        struct sip_pkt *pkt = NULL;
        int i;

        sip = kzalloc(sizeof(struct esp_sip), GFP_KERNEL);
        ASSERT(sip != NULL);

#ifdef ESP_RX_COPYBACK_TEST
        /* alloc 64KB for rx test */
        copyback_buf = kzalloc(0x10000, GFP_KERNEL);
#endif /* ESP_RX_COPYBACK_TEST */

        spin_lock_init(&sip->lock);

        INIT_LIST_HEAD(&sip->free_ctrl_txbuf);
        INIT_LIST_HEAD(&sip->free_ctrl_rxbuf);

        for (i = 0; i < SIP_CTRL_BUF_N; i++) {
                pkt = kzalloc(sizeof(struct sip_pkt), GFP_KERNEL);

                if (!pkt) break;

                pkt->buf_begin = kzalloc(SIP_CTRL_BUF_SZ, GFP_KERNEL);

                if (pkt->buf_begin == NULL) {
                        kfree(pkt);
                        break;
                }

                pkt->buf_len = SIP_CTRL_BUF_SZ;
                pkt->buf = pkt->buf_begin;

                if (i < SIP_CTRL_TXBUF_N) {
                        list_add_tail(&pkt->list, &sip->free_ctrl_txbuf);
                } else {
                        list_add_tail(&pkt->list, &sip->free_ctrl_rxbuf);
                }
        }

        spin_lock_init(&sip->rx_lock);
        skb_queue_head_init(&sip->rxq);
#ifndef RX_SYNC
        INIT_WORK(&sip->rx_process_work, sip_rxq_process);
#endif/* RX_SYNC */

        sip->epub = epub;
	sip->noise_floor = -96;

        atomic_set(&sip->state, SIP_INIT);
	atomic_set(&sip->tx_credits, 0);

        return sip;
}

static void sip_free_init_ctrl_buf(struct esp_sip *sip)
{
        struct sip_pkt *pkt, *tpkt;

        list_for_each_entry_safe(pkt, tpkt,
                                 &sip->free_ctrl_txbuf, list) {
                list_del(&pkt->list);
                kfree(pkt->buf_begin);
                kfree(pkt);
        }

        list_for_each_entry_safe(pkt, tpkt,
                                 &sip->free_ctrl_rxbuf, list) {
                list_del(&pkt->list);
                kfree(pkt->buf_begin);
                kfree(pkt);
        }
}

void sip_detach(struct esp_sip *sip)
{
        int po;

        sip_free_init_ctrl_buf(sip);

        if (atomic_read(&sip->state) == SIP_RUN) {

                sif_disable_target_interrupt(sip->epub);

                atomic_set(&sip->state, SIP_STOP);

                /* disable irq here */
                sif_disable_irq(sip->epub);
#ifndef RX_SYNC
                cancel_work_sync(&sip->rx_process_work);
#endif/* RX_SYNC */

                skb_queue_purge(&sip->rxq);
                cancel_work_sync(&sip->epub->sendup_work);
                skb_queue_purge(&sip->epub->rxq);

#ifdef ESP_NO_MAC80211
                unregister_netdev(sip->epub->net_dev);
                wiphy_unregister(sip->epub->wdev->wiphy);
#else
                if (test_and_clear_bit(ESP_WL_FLAG_HW_REGISTERED, &sip->epub->wl.flags)) {
                        ieee80211_unregister_hw(sip->epub->hw);
                }
#endif

                /* cancel all worker/timer */
                cancel_work_sync(&sip->epub->tx_work);
                skb_queue_purge(&sip->epub->txq);
                skb_queue_purge(&sip->epub->txdoneq);

                po = get_order(SIP_TX_AGGR_BUF_SIZE);
                free_pages((unsigned long)sip->tx_aggr_buf, po);
                sip->tx_aggr_buf = NULL;
#if 0
                po = get_order(SIP_RX_AGGR_BUF_SIZE);
                free_pages((unsigned long)sip->rx_aggr_buf, po);
                sip->rx_aggr_buf = NULL;
#endif
                atomic_set(&sip->state, SIP_INIT);
        } else if (atomic_read(&sip->state) >= SIP_BOOT && atomic_read(&sip->state) <= SIP_WAIT_BOOTUP) {

                sif_disable_target_interrupt(sip->epub);
                atomic_set(&sip->state, SIP_STOP);
                sif_disable_irq(sip->epub);

                if (sip->rawbuf)
                        kfree(sip->rawbuf);

                if (atomic_read(&sip->state) == SIP_SEND_INIT) {
#ifndef RX_SYNC
                        cancel_work_sync(&sip->rx_process_work);
#endif/* RX_SYNC */
                        skb_queue_purge(&sip->rxq);
                        cancel_work_sync(&sip->epub->sendup_work);
                        skb_queue_purge(&sip->epub->rxq);
                }

#ifdef ESP_NO_MAC80211
                unregister_netdev(sip->epub->net_dev);
                wiphy_unregister(sip->epub->wdev->wiphy);
#else
                if (test_and_clear_bit(ESP_WL_FLAG_HW_REGISTERED, &sip->epub->wl.flags)) {
                        ieee80211_unregister_hw(sip->epub->hw);
                }
#endif
                        atomic_set(&sip->state, SIP_INIT);
        } else
                esp_dbg(ESP_DBG_ERROR, "%s wrong state %d\n", __func__, atomic_read(&sip->state));

        kfree(sip);
}

int sip_prepare_boot(struct esp_sip *sip)
{
        if (atomic_read(&sip->state) != SIP_INIT) {
                esp_dbg(ESP_DBG_ERROR, "%s wrong state %d\n", __func__, atomic_read(&sip->state));
                return -1;
        }

        if (sip->rawbuf == NULL) {
                sip->rawbuf = kzalloc(SIP_BOOT_BUF_SIZE, GFP_KERNEL);

                if (sip->rawbuf == NULL)
                        return -ENOMEM;
        }

        atomic_set(&sip->state, SIP_PREPARE_BOOT);
        
	return 0;
}

int sip_write_memory(struct esp_sip *sip, u32 addr, u8 *buf, u16 len)
{
        struct sip_cmd_write_memory *cmd;
        struct sip_hdr *chdr;
        u16 remains, hdrs, bufsize;
        u32 loadaddr;
        u8 *src;
        int err = 0;
        u32 *t = 0;

        ASSERT(sip->rawbuf != NULL);

        memset(sip->rawbuf, 0, SIP_BOOT_BUF_SIZE);

        chdr = (struct sip_hdr *)sip->rawbuf;
        SIP_HDR_SET_TYPE(chdr->fc[0], SIP_CTRL);
        chdr->c_cmdid = SIP_CMD_WRITE_MEMORY;

        remains = len;
        hdrs = sizeof(struct sip_hdr) + sizeof(struct sip_cmd_write_memory);

        while (remains) {
                src = &buf[len - remains];
                loadaddr = addr + (len - remains);

                if (remains < (SIP_BOOT_BUF_SIZE - hdrs)) {
                        /* aligned with 4 bytes */
                        bufsize = roundup(remains, 4);
                        memset(sip->rawbuf + hdrs, 0, bufsize);
                        remains = 0;
                } else {
                        bufsize = SIP_BOOT_BUF_SIZE - hdrs;
                        remains -=  bufsize;
                }

                chdr->len = bufsize + hdrs;
                chdr->seq = sip->txseq++;
                cmd = (struct sip_cmd_write_memory *)(sip->rawbuf + SIP_CTRL_HDR_LEN);
                cmd->len = bufsize;
                cmd->addr = loadaddr;
                memcpy(sip->rawbuf+hdrs, src, bufsize);

                t = (u32 *)sip->rawbuf;
                esp_dbg(ESP_DBG_TRACE, "%s t0: 0x%08x t1: 0x%08x t2:0x%08x loadaddr 0x%08x \n", __func__, t[0], t[1], t[2], loadaddr);

				err = esp_common_write(sip->epub, sip->rawbuf, chdr->len, ESP_SIF_SYNC);

                if (err) {
                        esp_dbg(ESP_DBG_ERROR, "%s send buffer failed\n", __func__);
                        return err;
                }

                // 1ms is enough, in fact on dell-d430, need not delay at all.
                mdelay(1);

                sip_dec_credit(sip);
        }

        return err;
}

int sip_send_cmd(struct esp_sip *sip, int cid, u32 cmdlen, void *cmd)
{
        struct sip_hdr *chdr;
        struct sip_pkt *pkt = NULL;
        int ret = 0;

        pkt = sip_get_ctrl_buf(sip, SIP_TX_CTRL_BUF);

        if (pkt == NULL)
                return -ENOMEM;

        chdr = (struct sip_hdr *)pkt->buf_begin;
        chdr->len = SIP_CTRL_HDR_LEN + cmdlen;
        chdr->seq = sip->txseq++;
        chdr->c_cmdid = cid;
	

	if (cmd) {
		memset(pkt->buf, 0, cmdlen);
		memcpy(pkt->buf, (u8 *)cmd, cmdlen);
	}

        esp_dbg(ESP_DBG_TRACE, "cid %d, len %u, seq %u \n", chdr->c_cmdid, chdr->len, chdr->seq);

        esp_dbg(ESP_DBG_TRACE, "c1 0x%08x   c2 0x%08x\n", *(u32 *)&pkt->buf[0], *(u32 *)&pkt->buf[4]);

		ret = esp_common_write(sip->epub, pkt->buf_begin, chdr->len, ESP_SIF_SYNC);

        if (ret)
                esp_dbg(ESP_DBG_ERROR, "%s send cmd %d failed \n", __func__, cid);

        sip_dec_credit(sip);

        sip_reclaim_ctrl_buf(sip, pkt, SIP_TX_CTRL_BUF);

        /*
         *  Hack here: reset tx/rx seq before target ram code is up...
         */
        if (cid == SIP_CMD_BOOTUP) {
                sip->rxseq = 0;
                sip->txseq = 0;
		sip->txdataseq = 0;
        }

        return ret;
}

struct sk_buff *
sip_alloc_ctrl_skbuf(struct esp_sip *sip, u16 len, u32 cid) {
        struct sip_hdr *si = NULL;
        struct ieee80211_tx_info *ti = NULL;
        struct sk_buff *skb = NULL;

        ASSERT(len <= sip->tx_blksz);

        /* no need to reserve space for net stack */
        skb = __dev_alloc_skb(len, GFP_KERNEL);

        if (skb == NULL) {
                esp_dbg(ESP_DBG_ERROR, "no skb for ctrl !\n");
                return NULL;
        }

        skb->len = len;

        ti = IEEE80211_SKB_CB(skb);
        /* set tx_info flags to 0xffffffff to indicate sip_ctrl pkt */
        ti->flags = 0xffffffff;
        si = (struct sip_hdr *)skb->data;
        memset(si, 0, sizeof(struct sip_hdr));
        SIP_HDR_SET_TYPE(si->fc[0], SIP_CTRL);
        si->len = len;
        si->c_cmdid = cid;

        return skb;
}

void
sip_free_ctrl_skbuff(struct esp_sip *sip, struct sk_buff *skb)
{
        memset(IEEE80211_SKB_CB(skb), 0, sizeof(struct ieee80211_tx_info));
        kfree_skb(skb);
}

static struct sip_pkt *
sip_get_ctrl_buf(struct esp_sip *sip, SIP_BUF_TYPE bftype) {
        struct sip_pkt *pkt = NULL;
        struct list_head *bflist;
        struct sip_hdr *chdr;

        bflist = (bftype == SIP_TX_CTRL_BUF) ? &sip->free_ctrl_txbuf :&sip->free_ctrl_rxbuf;

        spin_lock_bh(&sip->lock);

        if (list_empty(bflist)) {
                spin_unlock_bh(&sip->lock);
                return NULL;
        }

        pkt = list_first_entry(bflist, struct sip_pkt, list);
        list_del(&pkt->list);
        spin_unlock_bh(&sip->lock);

        if (bftype == SIP_TX_CTRL_BUF) {
                chdr = (struct sip_hdr *)pkt->buf_begin;
                SIP_HDR_SET_TYPE(chdr->fc[0], SIP_CTRL);
                pkt->buf = pkt->buf_begin + SIP_CTRL_HDR_LEN;
        } else {
                pkt->buf = pkt->buf_begin;
        }

        return pkt;
}

static void
sip_reclaim_ctrl_buf(struct esp_sip *sip, struct sip_pkt *pkt, SIP_BUF_TYPE bftype)
{
        struct list_head *bflist = NULL;

        if (bftype == SIP_TX_CTRL_BUF)
                bflist = &sip->free_ctrl_txbuf;
        else if (bftype == SIP_RX_CTRL_BUF)
                bflist = &sip->free_ctrl_rxbuf;
        else return;

        pkt->buf = pkt->buf_begin;
        pkt->payload_len = 0;

        spin_lock_bh(&sip->lock);
        list_add_tail(&pkt->list, bflist);
        spin_unlock_bh(&sip->lock);
}

int
sip_poll_bootup_event(struct esp_sip *sip)
{
	int ret = 0;

        esp_dbg(ESP_DBG_TRACE, "polling bootup event... \n");

	if (gl_bootup_cplx)
		ret = wait_for_completion_timeout(gl_bootup_cplx, 2 * HZ);

	esp_dbg(ESP_DBG_TRACE, "******time remain****** = [%d]\n", ret);
	if (ret <= 0) {
		esp_dbg(ESP_DBG_ERROR, "bootup event timeout\n");
		return ret;
	}	

#ifndef FPGA_LOOPBACK
        ret = esp_register_mac80211(sip->epub);
#endif /* !FPGA_LOOPBACK */

#ifdef TEST_MODE
        ret = test_init_netlink(sip);
        if (ret < 0) {
                esp_sip_dbg(ESP_DBG_TRACE, "esp_sdio: failed initializing netlink\n");
		return ret;
	}
#endif
        
	atomic_set(&sip->state, SIP_RUN);
        esp_dbg(ESP_DBG_TRACE, "target booted up\n");

	return ret;
}

int
sip_poll_resetting_event(struct esp_sip *sip)
{
	int ret = 0;

        esp_dbg(ESP_DBG_TRACE, "polling resetting event... \n");

	if (gl_bootup_cplx)
		ret = wait_for_completion_timeout(gl_bootup_cplx, 10 * HZ);

	esp_dbg(ESP_DBG_TRACE, "******time remain****** = [%d]\n", ret);
	if (ret <= 0) {
		esp_dbg(ESP_DBG_ERROR, "resetting event timeout\n");
		return ret;
	}	
      
        esp_dbg(ESP_DBG_TRACE, "target resetting %d %p\n", ret, gl_bootup_cplx);

	return ret;
}


#ifdef FPGA_DEBUG

/* bogus bootup cmd for FPGA debugging */
int
sip_send_bootup(struct esp_sip *sip)
{
        int ret;
        struct sip_cmd_bootup bootcmd;

        esp_dbg(ESP_DBG_LOG, "sending bootup\n");

        bootcmd.boot_addr = 0;
        ret = sip_send_cmd(sip, SIP_CMD_BOOTUP, sizeof(struct sip_cmd_bootup), &bootcmd);

        return ret;
}

#endif /* FPGA_DEBUG */

bool
sip_queue_need_stop(struct esp_sip *sip)
{
        return atomic_read(&sip->tx_data_pkt_queued) >= SIP_STOP_QUEUE_THRESHOLD
		|| (atomic_read(&sip->tx_credits) < 8
		&& atomic_read(&sip->tx_data_pkt_queued) >= SIP_STOP_QUEUE_THRESHOLD / 4 * 3);
}

bool
sip_queue_may_resume(struct esp_sip *sip)
{
	return atomic_read(&sip->epub->txq_stopped)
		&& !test_bit(ESP_WL_FLAG_STOP_TXQ, &sip->epub->wl.flags)
		&& ((atomic_read(&sip->tx_credits) >= 16
		&& atomic_read(&sip->tx_data_pkt_queued) < SIP_RESUME_QUEUE_THRESHOLD * 2)
		|| atomic_read(&sip->tx_data_pkt_queued) < SIP_RESUME_QUEUE_THRESHOLD);
}

#ifndef FAST_TX_STATUS
bool
sip_tx_data_need_stop(struct esp_sip *sip)
{
        return atomic_read(&sip->pending_tx_status) >= SIP_PENDING_STOP_TX_THRESHOLD;
}

bool
sip_tx_data_may_resume(struct esp_sip *sip)
{
        return atomic_read(&sip->pending_tx_status) < SIP_PENDING_RESUME_TX_THRESHOLD;
}
#endif /* FAST_TX_STATUS */

int
sip_cmd_enqueue(struct esp_sip *sip, struct sk_buff *skb)
{
	if (!sip || !sip->epub) {
		esp_dbg(ESP_DBG_ERROR, "func %s, sip->epub->txq is NULL\n", __func__);
		return -1;
	}
	
	if (!skb) {
		esp_dbg(ESP_DBG_ERROR, "func %s, skb is NULL\n", __func__);
		return -2;
	}

        skb_queue_tail(&sip->epub->txq, skb);

#if  !defined(FPGA_LOOPBACK) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
        ieee80211_queue_work(sip->epub->hw, &sip->epub->tx_work);
#else
        queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
#endif
        return 0;
}

void sip_tx_data_pkt_enqueue(struct esp_pub *epub, struct sk_buff *skb)
{
	if(!epub || !epub->sip) {
		if (!epub)
			esp_dbg(ESP_DBG_ERROR, "func %s, epub is NULL\n", __func__);
		else
			esp_dbg(ESP_DBG_ERROR, "func %s, epub->sip is NULL\n", __func__);

		return;
	}
	if (!skb) {
                esp_dbg(ESP_DBG_ERROR, "func %s, skb is NULL\n", __func__);
                return;
        }
        skb_queue_tail(&epub->txq, skb);
        atomic_inc(&epub->sip->tx_data_pkt_queued);
	if(sip_queue_need_stop(epub->sip)){
		if (epub->hw) {
			ieee80211_stop_queues(epub->hw);
			atomic_set(&epub->txq_stopped, true);
		}

	}
}

#ifdef FPGA_TXDATA
int sip_send_tx_data(struct esp_sip *sip)
{
        struct sk_buff *skb = NULL;
        struct sip_cmd_bss_info_update*bsscmd;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_bss_info_update), SIP_CMD_BSS_INFO_UPDATE);
        if (!skb)
                return -1;

        bsscmd = (struct sip_cmd_bss_info_update *)(skb->data + sizeof(struct sip_tx_info));
        bsscmd->isassoc= (assoc==true)? 1: 0;
        memcpy(bsscmd->bssid, bssid, ETH_ALEN);
        STRACE_SHOW(epub->sip);
        return sip_cmd_enqueue(epub->sip, skb);
}
#endif /* FPGA_TXDATA */

#ifdef SIP_DEBUG
void sip_dump_pending_data(struct esp_pub *epub)
{
#if 0
        struct sk_buff *tskb, *tmp;

        skb_queue_walk_safe(&epub->txdoneq, tskb, tmp) {
                show_buf(tskb->data, 32);
        }
#endif //0000
}
#else
void sip_dump_pending_data(struct esp_pub *epub)
{}
#endif /* SIP_DEBUG */

