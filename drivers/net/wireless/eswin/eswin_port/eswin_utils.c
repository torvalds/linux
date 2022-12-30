/**
 ******************************************************************************
 *
 * @file eswin_utils.c
 *
 *@brief msg and management frame send functions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <net/sock.h>

#include "ecrnx_defs.h"
#include "ecrnx_tx.h"
#include "ecrnx_msg_tx.h"
#ifdef CONFIG_ECRNX_FULLMAC
#include "ecrnx_mesh.h"
#endif
#include "ecrnx_events.h"
#include "ecrnx_compat.h"

#include "eswin_utils.h"

#if 0
#if defined(CONFIG_ECRNX_ESWIN_SDIO)
#include "sdio_host_interface.h"
#elif defined(CONFIG_ECRNX_ESWIN_USB)
#include "usb_host_interface.h"
#endif
#endif

typedef int (*fn_send_t)(void * buff, int len, int flag);


fn_send_t   ecrnx_host_send;


void ecrnx_send_handle_register(void * fn)
{
	ecrnx_host_send = (fn_send_t)fn;
}


int host_send(void *buff, int len, int flag)
{
	//ECRNX_DBG("%s:len:%d,flag %#x \n", __func__, len, flag);
	return ecrnx_host_send(buff, len, flag);
}

void ecrnx_frame_send(struct ecrnx_hw *ecrnx_hw, struct txdesc_api *tx_desc,
                          struct sk_buff *skb, int hw_queue, int user)
{
    u32_l tx_flag;
    //struct ecrnx_txhdr *txhdr = NULL;
    struct ecrnx_sw_txhdr *sw_txhdr = NULL;

    ecrnx_hw->data_tx++;
    //send tx desc
    tx_flag = ((user << FLAG_TXDESC_USER_SHIFT) & FLAG_TXDESC_USER_MASK) | ((hw_queue << FLAG_TXDESC_QUEUE_SHIFT) & FLAG_TXDESC_QUEUE_MASK);
    tx_flag |= TX_FLAG_TX_DESC & FLAG_MSG_TYPE_MASK;
#if 0
	ECRNX_DBG("%s, user %d, queue %d, flag %#x, hostid 0x%08x, skb 0x%08x\n", __func__,
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
		user, hw_queue, tx_flag, tx_desc->host.packet_addr[0], (u32_l)skb);
	tx_desc->host.packet_addr[0] = (u32_l)skb;
#else
		user, hw_queue, tx_flag, tx_desc->host.packet_addr, (u32_l)skb);
	tx_desc->host.packet_addr = (u32_l)skb;
#endif
#endif

#if defined(CONFIG_ECRNX_ESWIN_SDIO)
    uint8_t *data = skb->data;
    sw_txhdr = container_of(tx_desc, struct ecrnx_sw_txhdr, desc);
    data += sw_txhdr->offset;
    data -= ECRNX_TX_TXDESC_API_ALIGN;
    memcpy(data, tx_desc, sizeof(struct txdesc_api));

    host_send((void*)data, ECRNX_TX_TXDESC_API_ALIGN + sw_txhdr->frame_len,  tx_flag);

#elif defined(CONFIG_ECRNX_ESWIN_USB)
    uint8_t *data;
    struct ecrnx_txhdr *txhdr = (struct ecrnx_txhdr *)skb->data;

    sw_txhdr = txhdr->sw_hdr;
    skb_pull(skb, sw_txhdr->offset - ECRNX_TX_TXDESC_API_ALIGN - sizeof(u32_l));
    memcpy(skb->data, (char*)&tx_flag, sizeof(u32_l));
    memcpy((uint8_t *)skb->data + sizeof(u32_l), (char*)tx_desc, sizeof(struct txdesc_api));

    data = skb->data - sizeof(ptr_addr);
    memcpy(data, (char*)&txhdr, sizeof(void *)); //store the ecrnx thhdr to the skb, cfm need use it
    host_send((void *)skb, skb->len, tx_flag);
#endif

#ifdef CONFIG_ECRNX_AMSDUS_TX
    //send amsdu sub frame
    if (sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU) {
        struct ecrnx_amsdu_txhdr *amsdu_txhdr;
        printk("...........amsdu tx\n");
        list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
        printk("...........amsdu tx: %x, %u\n", amsdu_txhdr->send_pos, amsdu_txhdr->map_len);
#if defined(CONFIG_ECRNX_ESWIN_SDIO)
            host_send((void*)amsdu_txhdr->send_pos, amsdu_txhdr->map_len, tx_flag);
#elif defined(CONFIG_ECRNX_ESWIN_USB)
            struct sk_buff *amsdu_skb = dev_alloc_skb(amsdu_txhdr->map_len + sizeof(int));

            memcpy(amsdu_skb->data, &tx_flag, sizeof(int));
            memcpy((char*)amsdu_skb->data + sizeof(int), amsdu_txhdr->send_pos, amsdu_txhdr->map_len);
            amsdu_skb->len = amsdu_txhdr->map_len + sizeof(int);
            host_send((void *)amsdu_skb, amsdu_skb->len, tx_flag);
#endif
        }
    }
#endif
    return;
}

static inline void ecrnx_bcn_param_send(struct mm_bcn_change_req *req)
{
    if (req->bcn_ptr && req->bcn_len) {
        host_send((void*)req->bcn_ptr, req->bcn_len, TX_FLAG_MSG_BEACON_IE);
    }
}

static inline void ecrnx_scanie_param_send(struct scan_start_req *req)
{
    if (req->add_ies && req->add_ie_len) {
        host_send((void*)req->add_ies, req->add_ie_len, TX_FLAG_MSG_SCAN_IE);
    }
}

static inline void ecrnx_scanuie_param_send(struct scanu_start_req *req)
{
    if (req->add_ies && req->add_ie_len) {
        host_send((void*)req->add_ies, req->add_ie_len, TX_FLAG_MSG_SCANU_IE);
    }
}

static inline void ecrnx_apm_param_send(struct apm_start_req *req)
{
    if (req->bcn_addr && req->bcn_len) {
        host_send((void*)req->bcn_addr, req->bcn_len, TX_FLAG_MSG_SOFTAP_IE);
    }
}

void ecrnx_msg_send(struct ecrnx_cmd *cmd, uint16_t len)
{
    void *param = cmd->a2e_msg->param;
    int param_len = cmd->a2e_msg->param_len;

    //cmd->a2e_msg->hostid = (u64_l)cmd;
    memcpy(cmd->a2e_msg->hostid, &cmd, sizeof(struct ecrnx_cmd *));

    switch (cmd->a2e_msg->id) {
        case MM_BCN_CHANGE_REQ:
            if (param_len >= sizeof(struct mm_bcn_change_req)) {
                ecrnx_bcn_param_send((struct mm_bcn_change_req *)param);
            }
            break;
        case SCAN_START_REQ:
            if (param_len >= sizeof(struct scan_start_req)) {
                ecrnx_scanie_param_send((struct scan_start_req *)param);
            }
            break;
        case SCANU_START_REQ:
            if (param_len >= sizeof(struct scanu_start_req)) {
                ecrnx_scanuie_param_send((struct scanu_start_req *)param);
            }
            break;
        case APM_START_REQ:
            if (param_len >= sizeof(struct apm_start_req)) {
                ecrnx_apm_param_send((struct apm_start_req *)param);
            }
            break;
        default:
            break;
    }

    host_send((void*)cmd->a2e_msg, len, TX_FLAG_MSG_E);
}

const struct ecrnx_element *cfg80211_find_ecrnx_elem(u8 eid, const u8 *ies, int len)
{
    const struct ecrnx_element *elem;
    for_each_ecrnx_element(elem, ies, len) {
        if (elem->id == (eid))
            return elem;
    }
    return NULL;
}
