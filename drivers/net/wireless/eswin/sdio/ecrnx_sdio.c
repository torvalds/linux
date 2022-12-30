/**
 ******************************************************************************
 *
 * @file ecrnx_sdio.c
 *
 * @brief ECRNX sdio init and management function
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include "core.h"
//#include "debug.h"
#include "ecrnx_utils.h"
#include "ecrnx_cmds.h"
#include "ecrnx_defs.h"
#include "ecrnx_msg_rx.h"
#include "ipc_host.h"
#include "ipc_shared.h"
#include "ecrnx_events.h"
#include "ecrnx_sdio.h"
#ifdef CONFIG_TEST_ESWIN_SDIO
#include "debug.h"
#endif
#ifdef CONFIG_ECRNX_WIFO_CAIL
#include "ecrnx_amt.h"
#endif


#define SDIO_ADDR_DATA		        (unsigned int)(0x200)

#ifdef CONFIG_ECRNX_SOFTMAC
#define FW_STR  "lmac"
#elif defined CONFIG_ECRNX_FULLMAC
#define FW_STR  "fmac"
#endif

sdio_rx_buf_t sdio_rx_buff;

extern const int nx_txdesc_cnt_msk[];

struct vendor_radiotap_hdr {
    u8 oui[3];
    u8 subns;
    u16 len;
    u8 data[];
};

/**
 * @brief: sdio_rx_buf_init: Initialization the sdio rx buffer
 * @param {void} void 
 * @return: none
 */
static void sdio_rx_buf_init(void)
{
    uint8_t i = 0;
    ECRNX_DBG("%s entry!!", __func__);
    memset(&sdio_rx_buff, 0, sizeof(sdio_rx_buf_t));

    for(i = 0; i < SDIO_RXBUF_CNT; i++)
    {
        sdio_rx_buff.ecrnx_sdio_rx_skb[i].skb = dev_alloc_skb(SDIO_RXBUF_SIZE);
        skb_put(sdio_rx_buff.ecrnx_sdio_rx_skb[i].skb, SDIO_RXBUF_SIZE);
    }
#ifdef CONFIG_TEST_ESWIN_SDIO
    sdio_rx_tx_test_schedule();
#endif
    ECRNX_DBG("%s exit!!", __func__);
}

/**
 * @brief: sdio_rx_buf_deinit: Deinitialization the sdio rx buffer
 * @param {void} void 
 * @return: none
 */
static void sdio_rx_buf_deinit(void)
{
    uint8_t i = 0;

    memset(&sdio_rx_buff, 0, sizeof(sdio_rx_buf_t));

    for(i = 0; i < SDIO_RXBUF_CNT; i++)
    {
        if(sdio_rx_buff.ecrnx_sdio_rx_skb[i].skb)
        {
            dev_kfree_skb(sdio_rx_buff.ecrnx_sdio_rx_skb[i].skb);
        }
    }
}

/**
 * @brief: sdio_rx_buf_push: push a skb element to sdio rx buffer
 * @param {skb}  skb data need to push
 * @param {recv_len}  skb data length
 * @return: sk_buff
 */
struct sk_buff * sdio_rx_buf_push(struct sk_buff *skb, uint16_t recv_len)
{
    uint8_t index = 0;

    ECRNX_DBG("%s enter, skb: %d, skb_data:%d, skb_len:%d", __func__, skb, skb->data, recv_len);
    if(atomic_read(&sdio_rx_buff.suspend))
    {
        return NULL;
    }

    if(sdio_rx_buff.sdio_host_rx_buff_used >= SDIO_RXBUF_CNT)
    {
        ECRNX_PRINT("no enough space \n");
        return NULL;
    }

    if((!skb) || (!recv_len) || (recv_len > SDIO_RXBUF_SIZE))
    {
        ECRNX_PRINT("rx data is error \n");
        return NULL;
    }

    atomic_set(&sdio_rx_buff.suspend, 1);

    do
    {
        if(!sdio_rx_buff.ecrnx_sdio_rx_skb[index].flag)  //find a index to store the data
        {
            break;
        }
        index++;
    }while(index < SDIO_RXBUF_CNT);

    sdio_rx_buff.sdio_host_rx_buff_used++;
    sdio_rx_buff.ecrnx_sdio_rx_skb[index].flag = true;
    sdio_rx_buff.ecrnx_sdio_rx_skb[index].data_len = recv_len;
    memcpy(sdio_rx_buff.ecrnx_sdio_rx_skb[index].skb->data, skb->data, recv_len);
    atomic_set(&sdio_rx_buff.suspend, 0);

    return sdio_rx_buff.ecrnx_sdio_rx_skb[index].skb;
}

/**
 * @brief: sdio_rx_buf_pop: pop a skb element from sdio rx buffer
 * @param {void} 
 * @return: sk_buff
 */
struct sk_buff* sdio_rx_buf_pop(void)
{
    uint8_t index = 0;
    struct sk_buff *skb= NULL;

    if(atomic_read(&sdio_rx_buff.suspend))
    {
    	ECRNX_PRINT("sdio suspend! \n");
        return NULL;
    }

    if(sdio_rx_buff.sdio_host_rx_buff_used <= 0)
    {
        ECRNX_PRINT("no data in memory \n");
        return NULL;
    }

    atomic_set(&sdio_rx_buff.suspend, 1);

    do
    {
        if(sdio_rx_buff.ecrnx_sdio_rx_skb[index].flag)  //find a index to pop the data
        {
            break;
        }
        index++;
    }while(index < SDIO_RXBUF_CNT);

    if(index != SDIO_RXBUF_CNT)
    {
        skb = sdio_rx_buff.ecrnx_sdio_rx_skb[index].skb;
        sdio_rx_buff.ecrnx_sdio_rx_skb[index].flag = false;
        sdio_rx_buff.sdio_host_rx_buff_used = (sdio_rx_buff.sdio_host_rx_buff_used > 0)? sdio_rx_buff.sdio_host_rx_buff_used-1: 0;
    }
    atomic_set(&sdio_rx_buff.suspend, 0);

    return skb;
}

/**
 * @brief: ipc_host_rxdesc_handler: Handle the reception of a Rx Descriptor
 *  Called from general IRQ handler when status %IPC_IRQ_E2A_RXDESC is set
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int sdio_host_rxdesc_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
	u8 ret = 0;
    // LMAC has triggered an IT saying that a reception has occurred.
    // Then we first need to check the validity of the current hostbuf, and the validity
    // of the next hostbufs too, because it is likely that several hostbufs have been
    // filled within the time needed for this irq handling
#ifdef CONFIG_ECRNX_FULLMAC
    // call the external function to indicate that a RX descriptor is received
    ret = env->cb.recv_data_ind(env->pthis, skb);
#else
    // call the external function to indicate that a RX packet is received
    ret = env->cb.recv_data_ind(env->pthis, skb);
#endif //(CONFIG_ECRNX_FULLMAC)
    ECRNX_DBG("%s exit!!", __func__);
	return ret;
}

/**
 * @brief: sdio_host_radar_handler  Handle the reception of radar events
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int sdio_host_radar_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    int ret = 0;
    
#ifdef CONFIG_ECRNX_RADAR
    // LMAC has triggered an IT saying that a radar event has been sent to upper layer.
    // Then we first need to check the validity of the current msg buf, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    spin_lock(&((struct ecrnx_hw *)env->pthis)->radar.lock);
    ret = env->cb.recv_radar_ind(env->pthis, skb);
    spin_unlock(&((struct ecrnx_hw *)env->pthis)->radar.lock);
#endif /* CONFIG_ECRNX_RADAR */
    return ret;
}

/**
 * @brief: sdio_host_unsup_rx_vec_handler  Handle the reception of unsupported rx vector
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int sdio_host_unsup_rx_vec_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    return env->cb.recv_unsup_rx_vec_ind(env->pthis, skb);
}

/**
 * @brief: sdio_host_msg_handler  Handler for firmware message
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int sdio_host_msg_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    // LMAC has triggered an IT saying that a message has been sent to upper layer.
    // Then we first need to check the validity of the current msg buf, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    return env->cb.recv_msg_ind(env->pthis, skb->data);
}

/**
 * @brief: sdio_host_msgack_handler  Handle the reception of message acknowledgement
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int sdio_host_msgack_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    uint64_t hostid = *(uint64_t *)skb->data;

    ASSERT_ERR(hostid);

    env->msga2e_hostid = NULL;
    env->cb.recv_msgack_ind(env->pthis, hostid);

    return 0;
}

/**
 * @brief: sdio_host_dbg_handler  Handle the reception of Debug event
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int sdio_host_dbg_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    // LMAC has triggered an IT saying that a DBG message has been sent to upper layer.
    // Then we first need to check the validity of the current buffer, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    return env->cb.recv_dbg_ind(env->pthis, skb);
}

/**
 * @brief: sdio_host_tx_cfm_handler  Handle the reception of TX confirmation
 * @param {env} pointer to the sdio Host environment
 * @param {queue_idx} index of the hardware on which the confirmation has been received
 * @param {user_pos} index of the user position
 * @return: none
 */
static int sdio_host_tx_cfm_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    ptr_addr host_id;
    struct sk_buff *skb_cfm;
    struct ecrnx_txhdr *txhdr;

    ECRNX_DBG("%s, skb: 0x%08x \n", __func__, skb);
#ifdef CONFIG_ECRNX_FULLMAC
    struct tx_cfm_tag *cfm;

    cfm = (struct tx_cfm_tag *)skb->data;
    memcpy((uint8_t *)&host_id, (uint8_t *)cfm->hostid, sizeof(ptr_addr));
    ECRNX_DBG("--%s--hostid 0x%08x, skb: 0x%x \n", __func__, host_id, skb);
    if (host_id == 0) {
        return 0;
    }

    skb_cfm = (struct sk_buff *)host_id;
    txhdr = (struct ecrnx_txhdr *)skb_cfm->data;
    memcpy(&txhdr->hw_hdr.cfm, cfm, sizeof(*cfm));
#elif defined CONFIG_ECRNX_SOFTMAC
    //TODO:
#endif

    return env->cb.send_data_cfm(env->pthis, host_id);
    //return 0;
}

#ifdef CONFIG_ECRNX_WIFO_CAIL
/**
 * @brief: sdio_host_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {skb} received skb data
 * @return: none
**/
void sdio_host_amt_rx_handler(uint32_t frm_type, struct sk_buff *skb)
{
    int need_free = 0;

    ECRNX_PRINT("%s enter, frame type: %d, len %d.!!", __func__, frm_type, skb->len);
    if (frm_type != SDIO_FRM_TYPE_RXDESC)
    {
        skb_pull(skb, SKB_DATA_COM_HD_OFFSET); //delete the frame common header
    }

    switch (frm_type)
    {
		case SDIO_FRM_TYPE_IWPRIV:
        {
            /*print_hex_dump(KERN_INFO, "iwpriv-cfm:", DUMP_PREFIX_ADDRESS, 32, 1,
		        skb->data, skb->len, false);*/
            amt_vif.rxlen = skb->len;
			memset(amt_vif.rxdata, 0, ECRNX_RXSIZE);
            memcpy(amt_vif.rxdata, skb->data, skb->len > ECRNX_RXSIZE ? ECRNX_RXSIZE : skb->len);
            amt_vif.rxdatas = 1;
            wake_up(&amt_vif.rxdataq);
			need_free = 1;
            break;
        }

        default:
			need_free = 1;
            break;
    }

    if (need_free && skb) { // free the skb
		ECRNX_DBG("skb free: 0x%x, \n", skb);
        dev_kfree_skb(skb);
    }
    ECRNX_DBG("%s exit!!", __func__);
    return;
}
#endif

/**
 * @brief: sdio_host_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
void sdio_host_rx_handler(uint32_t frm_type, struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    int ret = 1;

    ECRNX_DBG("%s enter, frame type: %d!!", __func__, frm_type);
    if (frm_type != SDIO_FRM_TYPE_RXDESC)
    {
        skb_pull(skb, SKB_DATA_COM_HD_OFFSET); //delete the frame common header
    }

    switch (frm_type)
    {
        case SDIO_FRM_TYPE_RXDESC:
        {
            sdio_host_rxdesc_handler(env, skb);
            break;
        }

        case SDIO_FRM_TYPE_MSG_ACK:
        {
            ret = sdio_host_msgack_handler(env, skb);
            break;
        }

        case SDIO_FRM_TYPE_MSG:
        {
            ret = sdio_host_msg_handler(env, skb);
            break;
        }

        case SDIO_FRM_TYPE_TXCFM:
        {
            /* add the spinlock which was missed during porting. */
            spin_lock_bh(&((struct ecrnx_hw *)env->pthis)->tx_lock);
            while(skb->len > sizeof(struct tx_cfm_tag))
            {
                ret = sdio_host_tx_cfm_handler(env, skb);
                skb_pull(skb, sizeof(struct tx_cfm_tag));
            }
            ret = sdio_host_tx_cfm_handler(env, skb);
            spin_unlock_bh(&((struct ecrnx_hw *)env->pthis)->tx_lock);
            break;
        }

        case SDIO_FRM_TYPE_UNSUP_RX_VEC:
        {
            // handle the unsupported rx vector reception
            ret = sdio_host_unsup_rx_vec_handler(env, skb);
            break;
        }

        case SDIO_FRM_TYPE_RADAR:
        {
            // handle the radar event reception
            ret = sdio_host_radar_handler(env, skb);
            break;
        }

        case SDIO_FRM_TYPE_TBTT_SEC:
        {
            env->cb.sec_tbtt_ind(env->pthis);
            break;
        }

        case SDIO_FRM_TYPE_TBTT_PRIM:
        {
            env->cb.prim_tbtt_ind(env->pthis);
            break;
        }

        case SDIO_FRM_TYPE_DBG:
        {
            ret = sdio_host_dbg_handler(env, skb);
            break;
        }

        case SDIO_FRM_TYPE_UPDATE:
        {
            ret = 0;
            break;
        }
        default:
        	ret = 0;
            break;
    }

    if (!ret && skb) { // free the skb
    	ECRNX_DBG("skb free: 0x%x, ret: %d!! \n", skb, ret);
        dev_kfree_skb(skb);
    }
    ECRNX_DBG("%s exit!!", __func__);
    return;
}

/**
 * @brief: rcv_skb_convert  convert the sdio received skb to the ecrnx handled skb.
 * @param {src_rxu_state} received rxu state in skb
 * @param {src_rxu_state} received rx header in skb
 * @param {src_rxu_state} handled hw rxhdr in skb
 * @param {src_rxu_state} handled rx desc tag  in skb
 * @return: u8
 */
u8 rcv_skb_convert(struct rxu_stat_mm* src_rxu_state, \
                struct rx_hd* src_rx_hd, \
                struct sk_buff *skb, \
                struct hw_rxhdr* dst_hw_rxhdr,\
                struct rxdesc_tag* dst_rxdesc_tag)
{
    ECRNX_DBG("%s enter!!", __func__);
#if 0
    uint16_t index = 0;

    if (!skb || !skb->data){
        ECRNX_PRINT("RX data invalid \n");
        return -1;
    }

    //copy the rxu state and rx header, repush process need to used them
    memcpy(src_rxu_state, skb->data, sizeof(struct rxu_stat_mm));
    memcpy(src_rx_hd, skb->data + sizeof(struct rxu_stat_mm), sizeof(struct rx_hd));

    /*copy the hw vector and rxu state */
    memcpy(dst_hw_rxhdr, skb->data + sizeof(struct rxu_stat_mm), sizeof(struct rx_hd));
    memcpy(&dst_hw_rxhdr->phy_info, \
        skb->data + SKB_DATA_TAG_OFFSET, \
        sizeof(struct rxu_stat_mm) - SKB_DATA_TAG_OFFSET);

    memcpy(dst_rxdesc_tag, skb->data + SKB_DATA_COM_HD_OFFSET + 2, sizeof(struct rxdesc_tag)); //two byte msdu_mode
    /*recove the hdr to skb data*/
    skb_pull(skb, SKB_DATA_HD_OFFSET); //delete the frame type and amsdu flag
    skb_push(skb, sizeof(struct hw_rxhdr));
    memcpy(skb->data, dst_hw_rxhdr, sizeof(struct hw_rxhdr)); //put hw header to skb
    skb_push(skb, sizeof(struct rxdesc_tag));
    memcpy(skb->data, dst_rxdesc_tag, sizeof(struct rxdesc_tag)); //put rx desc tag to skb

    for(index = 0; index < sizeof(struct hw_rxhdr) + sizeof(struct rxdesc_tag); index++)
    {
        ECRNX_DBG("0x%x ", skb->data[index]);
    }
#endif
    ECRNX_DBG("%s exit!!", __func__);
    return 0;
}

/**
 * @brief: ecrnx_sdio_init  Initialize sdio interface.
 * @param {ecrnx_hw} Main driver data
 * @return: u8
 */
u8 ecrnx_sdio_init(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG("%s entry!!", __func__);
    // Save the pointer to the register base
    ecrnx_hw->ipc_env->pthis = (void*)ecrnx_hw;
    
#ifdef CONFIG_TEST_ESWIN_SDIO
    ecrnx_hw_set((void*)ecrnx_hw);
#endif
    ECRNX_DBG("%s exit!!", __func__);
    return 0;
}

/**
 * @brief: ecrnx_sdio_deinit  DeInitialize sdio interface.
 * @param {ecrnx_hw} Main driver data
 * @return: none
 */
void ecrnx_sdio_deinit(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    sdio_rx_buf_deinit();
    memset(ecrnx_hw, 0, sizeof(struct ecrnx_hw));
    memset(&sdio_rx_buff, 0, sizeof(sdio_rx_buf_t));
}
