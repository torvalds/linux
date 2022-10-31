/**
 ******************************************************************************
 *
 * @file ecrnx_usb.c
 *
 * @brief ECRNX usb init and management function
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include <linux/module.h>
#include <linux/kthread.h>

#include "core.h"
//#include "debug.h"
#include "ecrnx_utils.h"
#include "ecrnx_cmds.h"
#include "ecrnx_defs.h"
#include "ecrnx_msg_rx.h"
#include "ipc_host.h"
#include "ipc_shared.h"
#include "ecrnx_events.h"
#include "ecrnx_usb.h"
#ifdef CONFIG_TEST_ESWIN_USB
#include "debug.h"
#endif
#ifdef CONFIG_ECRNX_WIFO_CAIL
#include "ecrnx_amt.h"
#endif


#define USB_ADDR_DATA		        (unsigned int)(0x200)

#ifdef CONFIG_ECRNX_SOFTMAC
#define FW_STR  "lmac"
#elif defined CONFIG_ECRNX_FULLMAC
#define FW_STR  "fmac"
#endif

#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
#include "ecrnx_debugfs_func.h"
#endif

extern const int nx_txdesc_cnt_msk[];

struct vendor_radiotap_hdr {
    u8 oui[3];
    u8 subns;
    u16 len;
    u8 data[];
};

#ifdef CONFIG_WEXT_PRIV
extern void priv_copy_data_wakeup(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb);
#endif

/**
 * @brief: ipc_host_rxdesc_handler: Handle the reception of a Rx Descriptor
 *  Called from general IRQ handler when status %IPC_IRQ_E2A_RXDESC is set
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int usb_host_rxdesc_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
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
    //ECRNX_DBG("%s exit!!", __func__);
	return ret;
}

/**
 * @brief: usb_host_radar_handler  Handle the reception of radar events
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int usb_host_radar_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
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
 * @brief: usb_host_unsup_rx_vec_handler  Handle the reception of unsupported rx vector
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int usb_host_unsup_rx_vec_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    return env->cb.recv_unsup_rx_vec_ind(env->pthis, skb);
}

/**
 * @brief: usb_host_msg_handler  Handler for firmware message
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int usb_host_msg_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    // LMAC has triggered an IT saying that a message has been sent to upper layer.
    // Then we first need to check the validity of the current msg buf, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    return env->cb.recv_msg_ind(env->pthis, skb->data);
}

/**
 * @brief: usb_host_msgack_handler  Handle the reception of message acknowledgement
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int usb_host_msgack_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    ptr_addr hostid = *(ptr_addr *)skb->data;

    ASSERT_ERR(hostid);

    env->msga2e_hostid = NULL;
    env->cb.recv_msgack_ind(env->pthis, (void*)hostid);

    return 0;
}

/**
 * @brief: usb_host_dbg_handler  Handle the reception of Debug event
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
static int usb_host_dbg_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    // LMAC has triggered an IT saying that a DBG message has been sent to upper layer.
    // Then we first need to check the validity of the current buffer, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    return env->cb.recv_dbg_ind(env->pthis, skb);
}

/**
 * @brief: usb_host_tx_cfm_handler  Handle the reception of TX confirmation
 * @param {env} pointer to the usb Host environment
 * @param {queue_idx} index of the hardware on which the confirmation has been received
 * @param {user_pos} index of the user position
 * @return: none
 */
static int usb_host_tx_cfm_handler(struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    ptr_addr host_id;
    struct sk_buff *skb_cfm;
    struct ecrnx_txhdr *txhdr;

#ifdef CONFIG_ECRNX_FULLMAC
    struct tx_cfm_tag *cfm;

    cfm = (struct tx_cfm_tag *)skb->data;
    //host_id = cfm->hostid;
    memcpy((uint8_t *)&host_id, (uint8_t *)cfm->hostid, sizeof(ptr_addr));
    if (host_id == 0) {
        return 0;
    }

    ECRNX_DBG("%s:hostid(tx_skb):0x%08x, rx_skb: 0x%x \n", __func__, host_id, skb);
    skb_cfm = (struct sk_buff *)host_id;
    txhdr = (struct ecrnx_txhdr *)(*((ptr_addr*)skb_cfm->data - 1));
    memcpy(&txhdr->hw_hdr.cfm, cfm, sizeof(*cfm));
#elif defined CONFIG_ECRNX_SOFTMAC
    //TODO:
#endif

    return env->cb.send_data_cfm(env->pthis, (void*)txhdr);
}

#ifdef CONFIG_ECRNX_WIFO_CAIL
/**
 * @brief: usb_host_amt_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {skb} received skb data
 * @return: none
 */
void usb_host_amt_rx_handler(uint32_t frm_type, struct sk_buff *skb)
{
    int need_free = 0;

    ECRNX_DBG("%s enter, frame type: %d!!", __func__, frm_type);
    if(!skb)
    {
        ECRNX_ERR("usb_host_amt_rx_handler input param error!! \n!");
        return;
    }

    if (frm_type != USB_FRM_TYPE_RXDESC)
    {
        skb_pull(skb, SKB_DATA_COM_HD_OFFSET); //delete the frame common header
    }

    ECRNX_DBG("skb:0x%08x, skb_len:%d, frame type: %d!!", skb->data,skb->len, frm_type);

    switch (frm_type)
    {
        case USB_FRM_TYPE_IWPRIV:
        {
        	/*printk("vif_start:%d, vif_monitor:%d \n", ecrnx_hw->vif_started, ecrnx_hw->monitor_vif);
			print_hex_dump(KERN_INFO, "iwpriv-cfm:", DUMP_PREFIX_ADDRESS, 32, 1,
		        skb->data, skb->len, false);*/
            amt_vif.rxlen = skb->len;
			memset(amt_vif.rxdata, 0, ECRNX_RXSIZE);
            memcpy(amt_vif.rxdata, skb->data, skb->len);
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
    	ECRNX_DBG("skb free: 0x%x !! \n", skb);
        dev_kfree_skb(skb);
    }
    ECRNX_DBG("%s exit!!", __func__);
    return;
}
#endif


/**
 * @brief: usb_host_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
void usb_host_rx_handler(uint32_t frm_type, struct ipc_host_env_tag *env, struct sk_buff *skb)
{
    int ret = 1;

    //ECRNX_DBG("%s enter, frame type: %d!!", __func__, frm_type);
    if(!env || !skb)
    {
        ECRNX_ERR("usb_host_rx_handler input param error!! \n!");
        return;
    }

    ((struct ecrnx_hw *)env->pthis)->usb_rx++;

    if (frm_type != USB_FRM_TYPE_RXDESC)
    {
        skb_pull(skb, SKB_DATA_COM_HD_OFFSET); //delete the frame common header
    }

    //ECRNX_DBG("skb:0x%08x, skb_len:%d, frame type: %d!!", skb->data,skb->len, frm_type);

    switch (frm_type)
    {
        case USB_FRM_TYPE_RXDESC:
        {
            // handle the RX descriptor reception
			usb_host_rxdesc_handler(env, skb); //just for current only one endpoint test
            break;
        }

        case USB_FRM_TYPE_MSG_ACK:
        {
            if(1 == skb->len)
            {
                ECRNX_PRINT("MSG_ACK len: 1");
                break;
            }
            ret = usb_host_msgack_handler(env, skb);
            break;
        }

        case USB_FRM_TYPE_MSG:
        {
            ret = usb_host_msg_handler(env, skb);
            break;
        }

        case USB_FRM_TYPE_TXCFM:
        {
            if(!env->pthis)
            {
                ECRNX_ERR("env->pthis ptr error!! \n!");
                break;
            }

            /* add the spinlock which was missed during porting.
               when skb->len more than 24 and skb contans more than one data cfm.
               data cfm structure length is 24 byte.
            */
            spin_lock_bh(&((struct ecrnx_hw *)env->pthis)->tx_lock);
            while(skb->len > sizeof(struct tx_cfm_tag))
            {
                ret = usb_host_tx_cfm_handler(env, skb);
                skb_pull(skb, sizeof(struct tx_cfm_tag));
            }
            ret = usb_host_tx_cfm_handler(env, skb);
            spin_unlock_bh(&((struct ecrnx_hw *)env->pthis)->tx_lock);
            break;
        }

        case USB_FRM_TYPE_UNSUP_RX_VEC:
        {
            // handle the unsupported rx vector reception
            ret = usb_host_unsup_rx_vec_handler(env, skb);
            break;
        }

        case USB_FRM_TYPE_RADAR:
        {
            // handle the radar event reception
            ret = usb_host_radar_handler(env, skb);
            break;
        }

        case USB_FRM_TYPE_TBTT_SEC:
        {
            env->cb.sec_tbtt_ind(env->pthis);
            break;
        }

        case USB_FRM_TYPE_TBTT_PRIM:
        {
            env->cb.prim_tbtt_ind(env->pthis);
            break;
        }

        case USB_FRM_TYPE_DBG:
        {
            ECRNX_DBG("--%s:USB_FRM_TYPE_DBG, len:%d, slave:%s \n", __func__, skb->len, skb->data);
            ret = usb_host_dbg_handler(env, skb);
            break;
        }
        case USB_FRM_TYPE_IWPRIV:
        {
#ifdef CONFIG_WEXT_PRIV
#if 0
            struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw *)env->pthis;
            struct ecrnx_vif* ecrnx_vif = ecrnx_hw->vif_table[0];

            printk("vif_start:%d, vif_monitor:%d \n", ecrnx_hw->vif_started, ecrnx_hw->monitor_vif);
            print_hex_dump(KERN_INFO, "iwpriv-cfm:", DUMP_PREFIX_ADDRESS, 32, 1,
		        skb->data, skb->len, false);
            ecrnx_vif->rxlen = skb->len;
            memcpy(ecrnx_vif->rxdata, skb->data, skb->len);
            ecrnx_vif->rxdatas = 1;
            wake_up(&ecrnx_vif->rxdataq);
#else
            priv_copy_data_wakeup((struct ecrnx_hw *)env->pthis, skb);
#endif

#endif
			ret = 0;
            break;
        }

#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
        case USB_FRM_DEBUG_FS:
        {
            uint32_t debugfs_type = ((uint32_t*)skb->data)[0];
            debugfs_resp.debugfs_type = debugfs_type;


            if((debugfs_type != SLAVE_LOG_LEVEL) && \
            (debugfs_type < SLAVE_DEBUGFS_MAX)){

                debugfs_resp.rxlen = skb->len-4;
                memcpy(debugfs_resp.rxdata, skb->data+4, debugfs_resp.rxlen);

                ECRNX_DBG("%s - wake_up()\n", __func__);
                debugfs_resp.rxdatas = 1;
                wake_up(&debugfs_resp.rxdataq);
            }

            break;
        }
#endif
        default:
        	ret = 0;
            break;
    }

    if (!ret && skb) { // free the skb
    	ECRNX_DBG("skb free: 0x%x, ret: %d!! \n", skb, ret);
        dev_kfree_skb(skb);
    }
    //ECRNX_DBG("%s exit!!", __func__);
    return;
}

/**
 * @brief: ecrnx_usb_init  Initialize usb interface.
 * @param {ecrnx_hw} Main driver data
 * @return: u8
 */
u8 ecrnx_usb_init(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG("%s entry!!", __func__);
    // Save the pointer to the register base
    ecrnx_hw->ipc_env->pthis = (void*)ecrnx_hw;

    ECRNX_DBG("%s exit!!", __func__);
    return 0;
}

/**
 * @brief: ecrnx_usb_deinit  DeInitialize usb interface.
 * @param {ecrnx_hw} Main driver data
 * @return: none
 */
void ecrnx_usb_deinit(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    memset(ecrnx_hw, 0, sizeof(struct ecrnx_hw));
}
