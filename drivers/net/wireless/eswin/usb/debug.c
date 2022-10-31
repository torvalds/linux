#include <linux/timer.h>
#include "ecrnx_utils.h"
#include "ecrnx_defs.h"
#include "ecrnx_msg_rx.h"
#include "ecrnx_usb.h"
#include "core.h"
#include "debug.h"
#include "usb.h"
#include "ecrnx_rx.h"


//kernal timer param
static struct timer_list tm;
static int counter = 0;

//rx param
struct sk_buff *skb = NULL;
struct ecrnx_hw *g_ecrnx_hw = NULL;

//tx param
struct ecrnx_vif vif;
struct ecrnx_sta sta;
struct cfg80211_mgmt_tx_params params;

static void test_timer_handler(struct timer_list * lt);

void ecrnx_hw_set(void* init_ecrnx_hw)
{
    g_ecrnx_hw = (struct ecrnx_hw *)init_ecrnx_hw;
}

static int sdio_rx_param_init(void)
{
    struct rxu_stat_mm rxu_state;
    struct rx_hd  rx_head;
    struct ethhdr eth_hd;
    int res, index = 0;
    uint8_t *ptr = NULL;
    uint16_t head_len = sizeof(struct ethhdr);
    ECRNX_DBG("%s entry!!", __func__);
    memset(&rxu_state, 0, sizeof(struct rxu_stat_mm));
    memset(&rx_head, 0, sizeof(struct rx_hd));
    memset(&eth_hd, 0, sizeof(struct ethhdr));

    rxu_state.comm_hd.frm_type = USB_FRM_TYPE_RXDESC;
    //rxu_state.comm_hd.frm_type = USB_FRM_TYPE_MSG;
    if(rxu_state.comm_hd.frm_type == USB_FRM_TYPE_RXDESC)
    {
        head_len += sizeof(struct rxu_stat_mm) + sizeof(struct rx_hd);
    }
    else
    {
        head_len += sizeof(dispatch_hdr_t);
    }

    skb = dev_alloc_skb(FRAME_SIZE + head_len);
    skb_reserve(skb, head_len);
    ptr = skb_put(skb, FRAME_SIZE); //ptr is skb tail
    memset(skb->data, 0x0f, FRAME_SIZE); //payload
    skb_push(skb, sizeof(struct ethhdr));

    for( index = 0; index < ETH_ALEN; index++)
    {
        eth_hd.h_dest[index] = index;
        eth_hd.h_source[index] = index;
    }

    eth_hd.h_proto = ETH_P_80221; //ETHERTYPE_IP;
    memcpy(skb->data, &eth_hd, sizeof(struct ethhdr));

    if(rxu_state.comm_hd.frm_type == USB_FRM_TYPE_RXDESC)
    {
        //data frame, need header, rxu state
        //rx head
        skb_push(skb, sizeof(struct rx_hd));
        rx_head.frmlen = FRAME_SIZE + head_len;
        rx_head.ampdu_stat_info = 0; 
        //...
        memcpy(skb->data , &rx_head, sizeof(struct rx_hd));

        //rxu state
        skb_push(skb, sizeof(struct rxu_stat_mm));
        rxu_state.msdu_mode = 0x01;
        rxu_state.host_id = 0x0001;
        rxu_state.frame_len  = rx_head.frmlen;
        rxu_state.status = RX_STAT_MONITOR;
        //rxu_state.phy_info.info1 = 1 | (1 << 8) | (2450 << 16);
        //rxu_state.phy_info.info2 = 2450 | (2450 << 16);
#ifdef CONFIG_ECRNX_FULLMAC
        //rxu_state.flags = 0;
#endif
        //rxu_state.pattern = ecrnx_rxbuff_pattern;
        memcpy(skb->data, &rxu_state, sizeof(struct rxu_stat_mm));
    }
    else
    {
        //message frame, don't need header
        skb_push(skb, sizeof(dispatch_hdr_t)); //rxu state
        memcpy(skb->data, &rxu_state.comm_hd, sizeof(dispatch_hdr_t));
    }

    for(index = 0; index < head_len; index++)
    {
        ECRNX_DBG("0x%x ", skb->data[index]);
    }

    ECRNX_DBG("%s exit, skb_len:%d, type: %d!!", __func__, skb->len, (skb->data[1] << 8) | skb->data[0]);

    return res;
}

extern int ecrnx_start_mgmt_xmit(struct ecrnx_vif *vif, \
                        struct ecrnx_sta *sta, \
                        struct cfg80211_mgmt_tx_params *params, \
                        bool offchan, \
                        u64 *cookie);

/*
struct cfg80211_mgmt_tx_params {
	struct ieee80211_channel *chan;
	bool offchan;
	unsigned int wait;
	const u8 *buf;
	size_t len;
	bool no_cck;
	bool dont_wait_for_ack;
	int n_csa_offsets;
	const u16 *csa_offsets;
};
*/
static void sdio_tx_param_init(void)
{
    u8 send_buf[FRAME_SIZE] = {0x00, 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09};

    params.len = FRAME_SIZE;
    params.buf = (const u8 *)send_buf;
    params.n_csa_offsets = 10;
    params.csa_offsets = (const u16 *)send_buf;
    params.no_cck = 0;

    vif.ecrnx_hw = g_ecrnx_hw;
}

void sdio_rx_tx_test_schedule(void)
{
    ECRNX_DBG("%s entry!!", __func__);
    tm.function = test_timer_handler;
    tm.expires = jiffies + HZ * 10;
    add_timer(&tm);
    
    sdio_rx_param_init();
    sdio_tx_param_init();
    ECRNX_DBG("%s exit!!", __func__);
}

static void test_timer_handler(struct timer_list * lt)
{
    ECRNX_DBG("%s, counter:%d\n", __FUNCTION__, counter);

    if(counter%2)
    {
        u64 cookie;
        //ecrnx_start_mgmt_xmit(&vif, NULL, &params, false, &cookie);
    }
    else
    {
        ecrnx_rx_callback(g_ecrnx_hw, skb, 1);
    }

    if(lt)
    {
        counter++;
    }

    if(counter < 5)
    {
        //tm.expires = jiffies +1 * HZ / 1000/10;  //100us
        tm.expires = jiffies +1 * HZ;
        add_timer(&tm);
    }
    else
    {
        counter = 0;
        del_timer(&tm);
    }
}

