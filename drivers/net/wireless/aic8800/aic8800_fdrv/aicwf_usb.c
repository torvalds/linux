// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * aicwf_usb.c
 *
 * USB function declarations
 *
 * Copyright (C) AICSemi 2018-2020
 */

#include <linux/usb.h>
#include <linux/kthread.h>
#include "aicwf_txrxif.h"
#include "aicwf_usb.h"
#include "rwnx_tx.h"
#include "rwnx_defs.h"
#include "usb_host.h"
#include "rwnx_platform.h"

#ifdef CONFIG_GPIO_WAKEUP
#ifdef CONFIG_PLATFORM_ROCKCHIP
#include <linux/rfkill-wlan.h>
#endif
static int wakeup_enable;
static u32 hostwake_irq_num;
atomic_t irq_count;
spinlock_t irq_lock;
#endif

#include <linux/semaphore.h>
extern struct semaphore aicwf_deinit_sem;
extern atomic_t aicwf_deinit_atomic;
#define SEM_TIMOUT 2000


#ifdef CONFIG_TXRX_THREAD_PRIO

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#include "linux/sched/types.h"
#else
//#include "linux/sched/rt.h"
//#include "uapi/linux/sched/types.h"
#endif

int bustx_thread_prio = 1;
module_param(bustx_thread_prio, int, 0);
int busrx_thread_prio = 1;
module_param(busrx_thread_prio, int, 0);
#endif

atomic_t rx_urb_cnt;

void aicwf_usb_tx_flowctrl(struct rwnx_hw *rwnx_hw, bool state)
{
    struct rwnx_vif *rwnx_vif;

    list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
        if (!rwnx_vif || !rwnx_vif->ndev || !rwnx_vif->up)
            continue;
        if (state)
            netif_tx_stop_all_queues(rwnx_vif->ndev);//netif_stop_queue(rwnx_vif->ndev);
        else
            netif_tx_wake_all_queues(rwnx_vif->ndev);//netif_wake_queue(rwnx_vif->ndev);
	}
}

static struct aicwf_usb_buf *aicwf_usb_tx_dequeue(struct aic_usb_dev *usb_dev,
    struct list_head *q, int *counter, spinlock_t *qlock)
{
    unsigned long flags;
    struct aicwf_usb_buf *usb_buf;

    spin_lock_irqsave(qlock, flags);
    if (list_empty(q)) {
        usb_buf = NULL;
    } else {
        usb_buf = list_first_entry(q, struct aicwf_usb_buf, list);
        list_del_init(&usb_buf->list);
        if (counter)
            (*counter)--;
    }
    spin_unlock_irqrestore(qlock, flags);
    return usb_buf;
}

static void aicwf_usb_tx_queue(struct aic_usb_dev *usb_dev,
    struct list_head *q, struct aicwf_usb_buf *usb_buf, int *counter,
    spinlock_t *qlock)
{
    unsigned long flags;

    spin_lock_irqsave(qlock, flags);
    list_add_tail(&usb_buf->list, q);
    (*counter)++;
    spin_unlock_irqrestore(qlock, flags);
}

static struct aicwf_usb_buf *aicwf_usb_rx_buf_get(struct aic_usb_dev *usb_dev)
{
    unsigned long flags;
    struct aicwf_usb_buf *usb_buf;

    spin_lock_irqsave(&usb_dev->rx_free_lock, flags);
    if (list_empty(&usb_dev->rx_free_list)) {
        usb_buf = NULL;
    } else {
        usb_buf = list_first_entry(&usb_dev->rx_free_list, struct aicwf_usb_buf, list);
        list_del_init(&usb_buf->list);
    }
    spin_unlock_irqrestore(&usb_dev->rx_free_lock, flags);
    return usb_buf;
}

static void aicwf_usb_rx_buf_put(struct aic_usb_dev *usb_dev, struct aicwf_usb_buf *usb_buf)
{
    unsigned long flags;

    spin_lock_irqsave(&usb_dev->rx_free_lock, flags);
    list_add_tail(&usb_buf->list, &usb_dev->rx_free_list);
    spin_unlock_irqrestore(&usb_dev->rx_free_lock, flags);
}

#ifdef CONFIG_USB_MSG_IN_EP
static struct aicwf_usb_buf *aicwf_usb_msg_rx_buf_get(struct aic_usb_dev *usb_dev)
{
    unsigned long flags;
    struct aicwf_usb_buf *usb_buf;

    spin_lock_irqsave(&usb_dev->msg_rx_free_lock, flags);
    if (list_empty(&usb_dev->msg_rx_free_list)) {
        usb_buf = NULL;
    } else {
        usb_buf = list_first_entry(&usb_dev->msg_rx_free_list, struct aicwf_usb_buf, list);
        list_del_init(&usb_buf->list);
    }
    spin_unlock_irqrestore(&usb_dev->msg_rx_free_lock, flags);
    return usb_buf;
}

static void aicwf_usb_msg_rx_buf_put(struct aic_usb_dev *usb_dev, struct aicwf_usb_buf *usb_buf)
{
    unsigned long flags;

    spin_lock_irqsave(&usb_dev->msg_rx_free_lock, flags);
    list_add_tail(&usb_buf->list, &usb_dev->msg_rx_free_list);
    spin_unlock_irqrestore(&usb_dev->msg_rx_free_lock, flags);
}
#endif

void rwnx_stop_sta_all_queues(struct rwnx_sta *sta, struct rwnx_hw *rwnx_hw)
{
        u8 tid;
         struct rwnx_txq *txq;
         for(tid=0; tid<8; tid++) {
                 txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);
                 netif_stop_subqueue(txq->ndev, txq->ndev_idx);
         }
 }

void rwnx_wake_sta_all_queues(struct rwnx_sta *sta, struct rwnx_hw *rwnx_hw)
{
        u8 tid;
         struct rwnx_txq *txq;
         for(tid=0; tid<8; tid++) {
                 txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);
                 netif_wake_subqueue(txq->ndev, txq->ndev_idx);
         }
 }

static void usb_txc_sta_flowctrl(struct aicwf_usb_buf *usb_buf, struct aic_usb_dev *usb_dev)
{
#ifdef CONFIG_PER_STA_FC
    unsigned long flags;
	struct rwnx_sta *sta;
	struct txdesc_api *hostdesc;
	u8 sta_idx;
	if(usb_buf->cfm)
		hostdesc = (struct txdesc_api *)((u8 *)usb_buf->skb + 4);
	else
		hostdesc = (struct txdesc_api *)((u8 *)usb_buf->skb->data + 4);
	//printk("txcpl: sta %d\n", hostdesc->host.staid);
	sta_idx = hostdesc->host.staid;
	if(sta_idx < NX_REMOTE_STA_MAX && !(hostdesc->host.flags & TXU_CNTRL_MGMT)) {
		struct rwnx_vif *vif = NULL;
		sta = &usb_dev->rwnx_hw->sta_table[sta_idx];
		vif = usb_dev->rwnx_hw->vif_table[sta->vif_idx];
		spin_lock_irqsave(&usb_dev->tx_flow_lock, flags);
		atomic_dec(&usb_dev->rwnx_hw->sta_flowctrl[sta_idx].tx_pending_cnt);
		//printk("sta:%d, pending:%d, flowctrl=%d\n", sta->sta_idx, sta->tx_pending_cnt, sta->flowctrl);
		if(RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP) {
			if(atomic_read(&usb_dev->rwnx_hw->sta_flowctrl[sta_idx].tx_pending_cnt) < AICWF_USB_FC_PERSTA_LOW_WATER &&
							usb_dev->rwnx_hw->sta_flowctrl[sta_idx].flowctrl) {
				//AICWFDBG(LOGDEBUG, "sta 0x%x:0x%x, %d pending %d, wake\n", sta->mac_addr[4], sta->mac_addr[5], sta->sta_idx, atomic_read(&usb_dev->rwnx_hw->sta_flowctrl[sta_idx].tx_pending_cnt));
				if(!usb_dev->tbusy)
					rwnx_wake_sta_all_queues(sta, usb_dev->rwnx_hw);
				usb_dev->rwnx_hw->sta_flowctrl[sta_idx].flowctrl = 0;
			}
		}
		spin_unlock_irqrestore(&usb_dev->tx_flow_lock, flags);
	}
#endif
}

static void aicwf_usb_tx_complete(struct urb *urb)
{
    unsigned long flags;
    struct aicwf_usb_buf *usb_buf = (struct aicwf_usb_buf *) urb->context;
    struct aic_usb_dev *usb_dev = usb_buf->usbdev;
    #ifndef CONFIG_USB_TX_AGGR
    struct sk_buff *skb;
    #endif

	usb_txc_sta_flowctrl(usb_buf, usb_dev);

#ifdef CONFIG_USB_ALIGN_DATA
	if(usb_buf->usb_align_data) {
		kfree(usb_buf->usb_align_data);
	}
#endif
#ifndef CONFIG_USB_TX_AGGR
    if (usb_buf->cfm == false) {
        skb = usb_buf->skb;
        dev_kfree_skb_any(skb);
    }
    #if !defined CONFIG_USB_NO_TRANS_DMA_MAP
    else {
        u8 *buf;
        buf = (u8 *)usb_buf->skb;
        kfree(buf);
    }
    #endif
    usb_buf->skb = NULL;
#else
    AICWFDBG(LOGDEBUG,"tx com %d\n", usb_buf->aggr_cnt);
    usb_buf->aggr_cnt = 0;
#endif//CONFIG_USB_TX_AGGR
    aicwf_usb_tx_queue(usb_dev, &usb_dev->tx_free_list, usb_buf,
                    &usb_dev->tx_free_count, &usb_dev->tx_free_lock);

    spin_lock_irqsave(&usb_dev->tx_flow_lock, flags);
    if (usb_dev->tx_free_count > AICWF_USB_TX_HIGH_WATER) {
        if (usb_dev->tbusy) {
            usb_dev->tbusy = false;
            aicwf_usb_tx_flowctrl(usb_dev->rwnx_hw, false);
        }
    }
    spin_unlock_irqrestore(&usb_dev->tx_flow_lock, flags);
}

void aicwf_usb_rx_submit_all_urb_(struct aic_usb_dev *usb_dev);

#ifdef CONFIG_PREALLOC_RX_SKB
static void aicwf_usb_rx_complete(struct urb *urb)
    {
        struct aicwf_usb_buf *usb_buf = (struct aicwf_usb_buf *) urb->context;
        struct aic_usb_dev *usb_dev = usb_buf->usbdev;
        struct aicwf_rx_priv* rx_priv = usb_dev->rx_priv;
        struct rx_buff *rx_buff = NULL;
        unsigned long flags = 0;
    
        rx_buff = usb_buf->rx_buff;
        usb_buf->rx_buff = NULL;
    
        atomic_dec(&rx_urb_cnt);
        if(atomic_read(&rx_urb_cnt) < 10){
            AICWFDBG(LOGDEBUG, "%s %d \r\n", __func__, atomic_read(&rx_urb_cnt));
            //printk("%s %d \r\n", __func__, atomic_read(&rx_urb_cnt));
        }

        if(!usb_dev->rwnx_hw){
            aicwf_prealloc_rxbuff_free(rx_buff, &rx_priv->rxbuff_lock);
            aicwf_usb_rx_buf_put(usb_dev, usb_buf);
            AICWFDBG(LOGERROR, "usb_dev->rwnx_hw is not ready \r\n");
            return;
        }
    
        if (urb->actual_length > urb->transfer_buffer_length) {
            aicwf_prealloc_rxbuff_free(rx_buff, &rx_priv->rxbuff_lock);
            aicwf_usb_rx_buf_put(usb_dev, usb_buf);
            aicwf_usb_rx_submit_all_urb_(usb_dev);
            return;
        }
    
        if (urb->status != 0 || !urb->actual_length) {
            aicwf_prealloc_rxbuff_free(rx_buff, &rx_priv->rxbuff_lock);
            aicwf_usb_rx_buf_put(usb_dev, usb_buf);
            if(urb->status < 0){
                AICWFDBG(LOGDEBUG, "%s urb->status:%d \r\n", __func__, urb->status);
    
                if(g_rwnx_plat->wait_disconnect_cb == false){
                    g_rwnx_plat->wait_disconnect_cb = true;
                    if(atomic_read(&aicwf_deinit_atomic) > 0){
                        atomic_set(&aicwf_deinit_atomic, 0);
                        down(&aicwf_deinit_sem);
                        AICWFDBG(LOGINFO, "%s need to wait for disconnect callback \r\n", __func__);
                    }else{
                        g_rwnx_plat->wait_disconnect_cb = false;
                    }
                }
    
                return;
            }else{
                //schedule_work(&usb_dev->rx_urb_work);
                aicwf_usb_rx_submit_all_urb_(usb_dev);
                return;
            }
        }
    
    if (usb_dev->state == USB_UP_ST) {
        spin_lock_irqsave(&rx_priv->rxqlock, flags);

        if(!aicwf_rxbuff_enqueue(usb_dev->dev, &rx_priv->rxq, rx_buff)){
            spin_unlock_irqrestore(&rx_priv->rxqlock, flags);
            usb_err("rx_priv->rxq is over flow!!!\n");
            aicwf_prealloc_rxbuff_free(rx_buff, &rx_priv->rxbuff_lock);
            aicwf_usb_rx_buf_put(usb_dev, usb_buf);
            aicwf_usb_rx_submit_all_urb_(usb_dev);
            return;
        }
        spin_unlock_irqrestore(&rx_priv->rxqlock, flags);
        atomic_inc(&rx_priv->rx_cnt);
            
        if(atomic_read(&rx_priv->rx_cnt) == 1){
            complete(&rx_priv->usbdev->bus_if->busrx_trgg);
        }

        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
        aicwf_usb_rx_submit_all_urb_(usb_dev);
    } else {
        aicwf_prealloc_rxbuff_free(rx_buff, &rx_priv->rxbuff_lock);
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
    }
}

#else
static void aicwf_usb_rx_complete(struct urb *urb)
{
    struct aicwf_usb_buf *usb_buf = (struct aicwf_usb_buf *) urb->context;
    struct aic_usb_dev *usb_dev = usb_buf->usbdev;
    struct aicwf_rx_priv* rx_priv = usb_dev->rx_priv;
    struct sk_buff *skb = NULL;
    unsigned long flags = 0;

    skb = usb_buf->skb;
    usb_buf->skb = NULL;

	atomic_dec(&rx_urb_cnt);
	if(atomic_read(&rx_urb_cnt) < 10){
		AICWFDBG(LOGDEBUG, "%s %d \r\n", __func__, atomic_read(&rx_urb_cnt));
		//printk("%s %d \r\n", __func__, atomic_read(&rx_urb_cnt));
	}

	if(!usb_dev->rwnx_hw){
		aicwf_dev_skb_free(skb);
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
		AICWFDBG(LOGERROR, "usb_dev->rwnx_hw is not ready \r\n");
		return;
	}

    if (urb->actual_length > urb->transfer_buffer_length) {
        aicwf_dev_skb_free(skb);
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
        aicwf_usb_rx_submit_all_urb_(usb_dev);
        return;
    }

    if (urb->status != 0 || !urb->actual_length) {
        aicwf_dev_skb_free(skb);
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
		if(urb->status < 0){
			AICWFDBG(LOGDEBUG, "%s urb->status:%d \r\n", __func__, urb->status);

			if(g_rwnx_plat->wait_disconnect_cb == false){
				g_rwnx_plat->wait_disconnect_cb = true;
				if(atomic_read(&aicwf_deinit_atomic) > 0){
					atomic_set(&aicwf_deinit_atomic, 0);
					down(&aicwf_deinit_sem);
					AICWFDBG(LOGINFO, "%s need to wait for disconnect callback \r\n", __func__);
				}else{
					g_rwnx_plat->wait_disconnect_cb = false;
				}
			}

			return;
		}else{
			//schedule_work(&usb_dev->rx_urb_work);
			aicwf_usb_rx_submit_all_urb_(usb_dev);
        	return;
		}
    }
    #ifdef CONFIG_USB_RX_AGGR
    if (urb->actual_length > 1600 * 30) {
        printk("r%d\n", urb->actual_length);
    }
    #endif

    if (usb_dev->state == USB_UP_ST) {

        skb_put(skb, urb->actual_length);

        spin_lock_irqsave(&rx_priv->rxqlock, flags);
        #ifdef CONFIG_USB_RX_AGGR
        skb->len = urb->actual_length;
        #endif
        if(!aicwf_rxframe_enqueue(usb_dev->dev, &rx_priv->rxq, skb)){
            spin_unlock_irqrestore(&rx_priv->rxqlock, flags);
            usb_err("rx_priv->rxq is over flow!!!\n");
            aicwf_dev_skb_free(skb);
            aicwf_usb_rx_buf_put(usb_dev, usb_buf);
            aicwf_usb_rx_submit_all_urb_(usb_dev);
            return;
        }
        spin_unlock_irqrestore(&rx_priv->rxqlock, flags);
        atomic_inc(&rx_priv->rx_cnt);
		
#ifndef CONFIG_RX_TASKLET 
		//if(!rx_priv->rx_thread_working && (atomic_read(&rx_priv->rx_cnt)>0)){
		if(atomic_read(&rx_priv->rx_cnt) == 1){
        	complete(&rx_priv->usbdev->bus_if->busrx_trgg);
		}
#else
        tasklet_schedule(&rx_priv->usbdev->recv_tasklet);
#endif
		
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
        aicwf_usb_rx_submit_all_urb_(usb_dev);
        //schedule_work(&usb_dev->rx_urb_work);
    } else {
        aicwf_dev_skb_free(skb);
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
    }
}
#endif

#ifdef CONFIG_USB_MSG_IN_EP
void aicwf_usb_msg_rx_submit_all_urb_(struct aic_usb_dev *usb_dev);

static void aicwf_usb_msg_rx_complete(struct urb *urb)
{
    struct aicwf_usb_buf *usb_buf = (struct aicwf_usb_buf *) urb->context;
    struct aic_usb_dev *usb_dev = usb_buf->usbdev;
    struct aicwf_rx_priv* rx_priv = usb_dev->rx_priv;
    struct sk_buff *skb = NULL;
    unsigned long flags = 0;

    skb = usb_buf->skb;
    usb_buf->skb = NULL;

    if (urb->actual_length > urb->transfer_buffer_length) {
        aicwf_dev_skb_free(skb);
        aicwf_usb_msg_rx_buf_put(usb_dev, usb_buf);
		aicwf_usb_msg_rx_submit_all_urb_(usb_dev);
        //schedule_work(&usb_dev->msg_rx_urb_work);
        return;
    }

    if (urb->status != 0 || !urb->actual_length) {
        aicwf_dev_skb_free(skb);
        aicwf_usb_msg_rx_buf_put(usb_dev, usb_buf);

		if(urb->status < 0){
			AICWFDBG(LOGDEBUG, "%s urb->status:%d \r\n", __func__, urb->status);
			return;
		}else{
			aicwf_usb_msg_rx_submit_all_urb_(usb_dev);
			//schedule_work(&usb_dev->msg_rx_urb_work);
        	return;
		}
    }

    if (usb_dev->state == USB_UP_ST) {
        skb_put(skb, urb->actual_length);

        spin_lock_irqsave(&rx_priv->msg_rxqlock, flags);
        if(!aicwf_rxframe_enqueue(usb_dev->dev, &rx_priv->msg_rxq, skb)){
            spin_unlock_irqrestore(&rx_priv->msg_rxqlock, flags);
            usb_err("rx_priv->rxq is over flow!!!\n");
            aicwf_dev_skb_free(skb);
            return;
        }
        spin_unlock_irqrestore(&rx_priv->msg_rxqlock, flags);
        atomic_inc(&rx_priv->msg_rx_cnt);
        complete(&rx_priv->usbdev->bus_if->msg_busrx_trgg);
        aicwf_usb_msg_rx_buf_put(usb_dev, usb_buf);
        aicwf_usb_msg_rx_submit_all_urb_(usb_dev);
        //schedule_work(&usb_dev->msg_rx_urb_work);
    } else {
        aicwf_dev_skb_free(skb);
        aicwf_usb_msg_rx_buf_put(usb_dev, usb_buf);
    }
}
#endif

#ifdef CONFIG_PREALLOC_RX_SKB
//extern int aic_rxbuff_size;
static int aicwf_usb_submit_rx_urb(struct aic_usb_dev *usb_dev,
                struct aicwf_usb_buf *usb_buf)
{
    int ret;
    struct rx_buff *rx_buff;

    if (!usb_buf || !usb_dev)
        return -1;

    if (usb_dev->state != USB_UP_ST) {
        usb_err("usb state is not up!\r\n");
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
        return -1;
    }
    rx_buff =  aicwf_prealloc_rxbuff_alloc(&usb_dev->rx_priv->rxbuff_lock);
	if (rx_buff == NULL) {
		AICWFDBG(LOGERROR, "failed to alloc rxbuff\r\n");
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
        return -1;
    }
	rx_buff->len = 0;
	rx_buff->start = rx_buff->data;
	rx_buff->read = rx_buff->start;
	rx_buff->end = rx_buff->data + aicwf_rxbuff_size_get();

    usb_buf->rx_buff = rx_buff;

    usb_fill_bulk_urb(usb_buf->urb,
        usb_dev->udev,
        usb_dev->bulk_in_pipe,
        rx_buff->data, aicwf_rxbuff_size_get(), aicwf_usb_rx_complete, usb_buf);

    usb_buf->usbdev = usb_dev;

    usb_anchor_urb(usb_buf->urb, &usb_dev->rx_submitted);
    ret = usb_submit_urb(usb_buf->urb, GFP_ATOMIC);
    if (ret) {
        usb_err("usb submit rx urb fail:%d\n", ret);
        usb_unanchor_urb(usb_buf->urb);
        aicwf_prealloc_rxbuff_free(rx_buff, &usb_dev->rx_priv->rxbuff_lock);
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);

        msleep(100);
    }else{
    	atomic_inc(&rx_urb_cnt);
	}
    return 0;
}

#else
static int aicwf_usb_submit_rx_urb(struct aic_usb_dev *usb_dev,
                struct aicwf_usb_buf *usb_buf)
{
    struct sk_buff *skb;
    int ret;

    if (!usb_buf || !usb_dev)
        return -1;

    if (usb_dev->state != USB_UP_ST) {
        usb_err("usb state is not up!\n");
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
        return -1;
    }

    skb = __dev_alloc_skb(AICWF_USB_MAX_PKT_SIZE, GFP_ATOMIC/*GFP_KERNEL*/);
    if (!skb) {
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);
        return -1;
    }

    usb_buf->skb = skb;

    usb_fill_bulk_urb(usb_buf->urb,
        usb_dev->udev,
        usb_dev->bulk_in_pipe,
        skb->data, skb_tailroom(skb), aicwf_usb_rx_complete, usb_buf);

    usb_buf->usbdev = usb_dev;

    usb_anchor_urb(usb_buf->urb, &usb_dev->rx_submitted);
    ret = usb_submit_urb(usb_buf->urb, GFP_ATOMIC);
    if (ret) {
        usb_err("usb submit rx urb fail:%d\n", ret);
        usb_unanchor_urb(usb_buf->urb);
        aicwf_dev_skb_free(usb_buf->skb);
        usb_buf->skb = NULL;
        aicwf_usb_rx_buf_put(usb_dev, usb_buf);

        msleep(100);
    }else{
    	atomic_inc(&rx_urb_cnt);
	}
    return 0;
}
#endif

static void aicwf_usb_rx_submit_all_urb(struct aic_usb_dev *usb_dev)
{
    struct aicwf_usb_buf *usb_buf;
//	int i = 0;

    if (usb_dev->state != USB_UP_ST) {
        AICWFDBG(LOGERROR, "bus is not up=%d\n", usb_dev->state);
        return;
    }

    while((usb_buf = aicwf_usb_rx_buf_get(usb_dev)) != NULL) {
        if (aicwf_usb_submit_rx_urb(usb_dev, usb_buf)) {
            AICWFDBG(LOGERROR, "usb rx refill fail\n");
            if (usb_dev->state != USB_UP_ST)
                return;
        }
    }
}

#ifdef CONFIG_USB_MSG_IN_EP
static int aicwf_usb_submit_msg_rx_urb(struct aic_usb_dev *usb_dev,
                struct aicwf_usb_buf *usb_buf)
{
    struct sk_buff *skb;
    int ret;

    if (!usb_buf || !usb_dev)
        return -1;

    if (usb_dev->state != USB_UP_ST) {
        AICWFDBG(LOGERROR, "usb state is not up!\n");
        aicwf_usb_msg_rx_buf_put(usb_dev, usb_buf);
        return -1;
    }

    skb = __dev_alloc_skb(AICWF_USB_MAX_PKT_SIZE, GFP_ATOMIC);
    if (!skb) {
        aicwf_usb_msg_rx_buf_put(usb_dev, usb_buf);
        return -1;
    }

    usb_buf->skb = skb;

    usb_fill_bulk_urb(usb_buf->urb,
        usb_dev->udev,
        usb_dev->msg_in_pipe,
        skb->data, skb_tailroom(skb), aicwf_usb_msg_rx_complete, usb_buf);

    usb_buf->usbdev = usb_dev;

    usb_anchor_urb(usb_buf->urb, &usb_dev->msg_rx_submitted);
    ret = usb_submit_urb(usb_buf->urb, GFP_ATOMIC);
    if (ret) {
        AICWFDBG(LOGERROR, "usb submit msg rx urb fail:%d\n", ret);
        usb_unanchor_urb(usb_buf->urb);
        aicwf_dev_skb_free(usb_buf->skb);
        usb_buf->skb = NULL;
        aicwf_usb_msg_rx_buf_put(usb_dev, usb_buf);

        msleep(100);
    }
    return 0;
}


static void aicwf_usb_msg_rx_submit_all_urb(struct aic_usb_dev *usb_dev)
{
    struct aicwf_usb_buf *usb_buf;

    if (usb_dev->state != USB_UP_ST) {
        AICWFDBG(LOGERROR, "bus is not up=%d\n", usb_dev->state);
        return;
    }

    while((usb_buf = aicwf_usb_msg_rx_buf_get(usb_dev)) != NULL) {
        if (aicwf_usb_submit_msg_rx_urb(usb_dev, usb_buf)) {
            AICWFDBG(LOGERROR, "usb msg rx refill fail\n");
            if (usb_dev->state != USB_UP_ST)
                return;
        }
    }
}
#endif

#ifdef CONFIG_USB_MSG_IN_EP
void aicwf_usb_msg_rx_submit_all_urb_(struct aic_usb_dev *usb_dev){
	aicwf_usb_msg_rx_submit_all_urb(usb_dev);
}
#endif

void aicwf_usb_rx_submit_all_urb_(struct aic_usb_dev *usb_dev){
	aicwf_usb_rx_submit_all_urb(usb_dev);
}


static void aicwf_usb_rx_prepare(struct aic_usb_dev *usb_dev)
{
    aicwf_usb_rx_submit_all_urb(usb_dev);
}

#ifdef CONFIG_USB_MSG_IN_EP
static void aicwf_usb_msg_rx_prepare(struct aic_usb_dev *usb_dev)
{
    aicwf_usb_msg_rx_submit_all_urb(usb_dev);
}
#endif


static void aicwf_usb_tx_prepare(struct aic_usb_dev *usb_dev)
{
    struct aicwf_usb_buf *usb_buf;

    while(!list_empty(&usb_dev->tx_post_list)){
        usb_buf = aicwf_usb_tx_dequeue(usb_dev, &usb_dev->tx_post_list,
            &usb_dev->tx_post_count, &usb_dev->tx_post_lock);
        #ifndef CONFIG_USB_TX_AGGR
        if(usb_buf->skb) {
            dev_kfree_skb(usb_buf->skb);
            usb_buf->skb = NULL;
        }
        #endif
        aicwf_usb_tx_queue(usb_dev, &usb_dev->tx_free_list, usb_buf,
                &usb_dev->tx_free_count, &usb_dev->tx_free_lock);
    }
}

#ifdef CONFIG_USB_TX_AGGR
int aicwf_usb_send_pkt(struct aic_usb_dev *usb_dev, u8 *buf, uint buf_len)
{
    int ret = 0;
    struct aicwf_usb_buf *usb_buf;
    unsigned long flags;
    bool need_cfm = false;

    if (usb_dev->state != USB_UP_ST) {
        AICWFDBG(LOGERROR, "usb state is not up!\n");
        return -EIO;
    }

    usb_buf = aicwf_usb_tx_dequeue(usb_dev, &usb_dev->tx_free_list,
                        &usb_dev->tx_free_count, &usb_dev->tx_free_lock);
    if (!usb_buf) {
        AICWFDBG(LOGERROR, "free:%d, post:%d\n", usb_dev->tx_free_count, usb_dev->tx_post_count);
        ret = -ENOMEM;
        goto flow_ctrl;
    }

    usb_buf->skb = (struct sk_buff *)buf;
    usb_buf->usbdev = usb_dev;
    if (need_cfm)
        usb_buf->cfm = true;
    else
        usb_buf->cfm = false;
    AICWFDBG(LOGERROR, "%s len %d\n", __func__, buf_len);
    print_hex_dump(KERN_ERR, "buf  ", DUMP_PREFIX_NONE, 16, 1, &buf[0], 32, false);
    usb_fill_bulk_urb(usb_buf->urb, usb_dev->udev, usb_dev->bulk_out_pipe,
                buf, buf_len, aicwf_usb_tx_complete, usb_buf);
    usb_buf->urb->transfer_flags |= URB_ZERO_PACKET;

    aicwf_usb_tx_queue(usb_dev, &usb_dev->tx_post_list, usb_buf,
                    &usb_dev->tx_post_count, &usb_dev->tx_post_lock);
    ret = 0;

    flow_ctrl:
    spin_lock_irqsave(&usb_dev->tx_flow_lock, flags);
    if (usb_dev->tx_free_count < AICWF_USB_TX_LOW_WATER) {
        usb_dev->tbusy = true;
        aicwf_usb_tx_flowctrl(usb_dev->rwnx_hw, true);
    }
    spin_unlock_irqrestore(&usb_dev->tx_flow_lock, flags);

    return ret;
}

int aicwf_usb_aggr(struct aicwf_tx_priv *tx_priv, struct sk_buff *pkt)
{
    struct rwnx_txhdr *txhdr = (struct rwnx_txhdr *)pkt->data;
    u8 usb_header[8];
    u8 adjust_str[4] = {0, 0, 0, 0};
    u32 curr_len = 0;
    int allign_len = 0;
    u32 data_len = (pkt->len - sizeof(struct rwnx_txhdr) + sizeof(struct txdesc_api)) + 4;

    usb_header[0] =(data_len & 0xff);
    usb_header[1] =((data_len >> 8)&0x0f);
    usb_header[2] =(data_len & 0xff);
    usb_header[3] =((data_len >> 8)&0x0f);

    usb_header[4] =(data_len & 0xff);
    usb_header[5] =((data_len >> 8)&0x0f);
    usb_header[6] = 0x01; //data
    usb_header[7] = 0; //reserved

    memcpy(tx_priv->tail, (u8 *)&usb_header, sizeof(usb_header));
    tx_priv->tail += sizeof(usb_header);
    //payload
    memcpy(tx_priv->tail, (u8 *)(long)&txhdr->sw_hdr->desc, sizeof(struct txdesc_api));
    tx_priv->tail += sizeof(struct txdesc_api); //hostdesc
    memcpy(tx_priv->tail, (u8 *)((u8 *)txhdr + txhdr->sw_hdr->headroom), pkt->len-txhdr->sw_hdr->headroom);
    tx_priv->tail += (pkt->len - txhdr->sw_hdr->headroom);

    //word alignment
    curr_len = tx_priv->tail - tx_priv->head;
    if (curr_len & (TX_ALIGNMENT - 1)) {
        allign_len = roundup(curr_len, TX_ALIGNMENT)-curr_len;
        memcpy(tx_priv->tail, adjust_str, allign_len);
        tx_priv->tail += allign_len;
    }

    tx_priv->aggr_buf->dev = pkt->dev;

    if(!txhdr->sw_hdr->need_cfm) {
        kmem_cache_free(txhdr->sw_hdr->rwnx_vif->rwnx_hw->sw_txhdr_cache, txhdr->sw_hdr);
        skb_pull(pkt, txhdr->sw_hdr->headroom);
        consume_skb(pkt);
    }

    atomic_inc(&tx_priv->aggr_count);
    return 0;
}

int aicwf_usb_send(struct aicwf_tx_priv *tx_priv)
{
    struct sk_buff *pkt;
    struct sk_buff *tx_buf;
    struct aic_usb_dev *usbdev = tx_priv->usbdev;
    struct aicwf_usb_buf *usb_buf;
    u8* buf;
    int ret = 0;
    int curr_len = 0;

    if (aicwf_is_framequeue_empty(&tx_priv->txq)) {
        ret = -1;
        AICWFDBG(LOGERROR, "no buf to send\n");
        return ret;
    }

    if (usbdev->state != USB_UP_ST) {
        AICWFDBG(LOGERROR, "usb state is not up!\n");
        ret = -ENODEV;
        return ret;
    }
    usb_buf = aicwf_usb_tx_dequeue(usbdev, &usbdev->tx_free_list,
                        &usbdev->tx_free_count, &usbdev->tx_free_lock);
    if (!usb_buf) {
        AICWFDBG(LOGERROR, "free:%d, post:%d\n", usbdev->tx_free_count, usbdev->tx_post_count);
        ret = -ENOMEM;
        return ret;
    }

    usb_buf->aggr_cnt = 0;
    spin_lock_bh(&usbdev->tx_priv->txdlock);
    tx_priv->head = usb_buf->skb->data;
    tx_priv->tail = usb_buf->skb->data;

    while (!aicwf_is_framequeue_empty(&usbdev->tx_priv->txq)) {
        if (usbdev->state != USB_UP_ST) {
            AICWFDBG(LOGERROR, "usb state is not up, break!\n");
            ret = -ENODEV;
            break;
        }

        if (usb_buf->aggr_cnt == 10) {
            break;
        }
        spin_lock_bh(&usbdev->tx_priv->txqlock);
        pkt = aicwf_frame_dequeue(&usbdev->tx_priv->txq);
        if (pkt == NULL) {
            AICWFDBG(LOGERROR, "txq no pkt\n");
            spin_unlock_bh(&usbdev->tx_priv->txqlock);
            ret = -1;
            return ret;
        }
        atomic_dec(&usbdev->tx_priv->tx_pktcnt);
        spin_unlock_bh(&usbdev->tx_priv->txqlock);
        if(tx_priv==NULL || tx_priv->tail==NULL || pkt==NULL) {
            AICWFDBG(LOGERROR, "null error\n");
        }
        aicwf_usb_aggr(tx_priv, pkt);
        usb_buf->aggr_cnt++;
    }

    tx_buf = usb_buf->skb;
    buf = tx_buf->data;

    curr_len = tx_priv->tail - tx_priv->head;

    AICWFDBG(LOGERROR, "%s len %d, cnt %d\n", __func__,curr_len, usb_buf->aggr_cnt);
    tx_buf->len = tx_priv->tail - tx_priv->head;
    spin_unlock_bh(&usbdev->tx_priv->txdlock);
    usb_fill_bulk_urb(usb_buf->urb, usbdev->udev, usbdev->bulk_out_pipe,
                buf, curr_len, aicwf_usb_tx_complete, usb_buf);
    usb_buf->urb->transfer_flags |= URB_ZERO_PACKET;

    aicwf_usb_tx_queue(usbdev, &usbdev->tx_post_list, usb_buf,
                    &usbdev->tx_post_count, &usbdev->tx_post_lock);
/*
    flow_ctrl:
    spin_lock_irqsave(&usbdev->tx_flow_lock, flags);
    if (usbdev->tx_free_count < AICWF_USB_TX_LOW_WATER) {
        usbdev->tbusy = true;
        aicwf_usb_tx_flowctrl(usbdev->rwnx_hw, true);
    }
    spin_unlock_irqrestore(&usbdev->tx_flow_lock, flags);
*/
    return ret;
}

#endif

static void aicwf_usb_tx_process(struct aic_usb_dev *usb_dev)
{
    struct aicwf_usb_buf *usb_buf;
    int ret = 0;
    u8* data = NULL;

#ifdef CONFIG_USB_TX_AGGR
    if (!aicwf_is_framequeue_empty(&usb_dev->tx_priv->txq)) {
        if (aicwf_usb_send(usb_dev->tx_priv)) {
            AICWFDBG(LOGERROR, "%s no buf send\n", __func__);
        }
    }
#endif

    while(!list_empty(&usb_dev->tx_post_list)) {

        if (usb_dev->state != USB_UP_ST) {
            usb_err("usb state is not up!\n");
            return;
        }

        usb_buf = aicwf_usb_tx_dequeue(usb_dev, &usb_dev->tx_post_list,
                        &usb_dev->tx_post_count, &usb_dev->tx_post_lock);
        if(!usb_buf) {
            usb_err("can not get usb_buf from tx_post_list!\n");
            return;
        }
        data = usb_buf->skb->data;

        ret = usb_submit_urb(usb_buf->urb, GFP_ATOMIC);
        if (ret) {
            AICWFDBG(LOGERROR, "aicwf_usb_bus_tx usb_submit_urb FAILED err:%d\n", ret);
            #ifdef CONFIG_USB_TX_AGGR
            aicwf_usb_tx_queue(usb_dev, &usb_dev->tx_post_list, usb_buf,
                    &usb_dev->tx_post_count, &usb_dev->tx_post_lock);
            break;
            #else
            goto fail;
            #endif
        }

        continue;
#ifndef CONFIG_USB_TX_AGGR
fail:
        usb_txc_sta_flowctrl(usb_buf, usb_dev);
        dev_kfree_skb(usb_buf->skb);
        usb_buf->skb = NULL;
        aicwf_usb_tx_queue(usb_dev, &usb_dev->tx_free_list, usb_buf,
                    &usb_dev->tx_free_count, &usb_dev->tx_free_lock);
#endif
    }
}

#ifdef CONFIG_TX_TASKLET
void aicwf_tasklet_tx_process(struct aic_usb_dev *usb_dev){
	aicwf_usb_tx_process(usb_dev);
}
#endif

static inline void aic_thread_wait_stop(void)
{
#if 1// PLATFORM_LINUX
	#if 0
	while (!kthread_should_stop())
		rtw_msleep_os(10);
	#else
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	#endif
#endif
}


int usb_bustx_thread(void *data)
{
    struct aicwf_bus *bus = (struct aicwf_bus *)data;
    struct aic_usb_dev *usbdev = bus->bus_priv.usb;

#ifdef CONFIG_TXRX_THREAD_PRIO
	if (bustx_thread_prio > 0) {
			struct sched_param param;
			param.sched_priority = (bustx_thread_prio < MAX_RT_PRIO)?bustx_thread_prio:(MAX_RT_PRIO-1);
			sched_setscheduler(current, SCHED_FIFO, &param);
	}
#endif
	AICWFDBG(LOGINFO, "%s the policy of current thread is:%d\n", __func__, current->policy);
	AICWFDBG(LOGINFO, "%s the rt_priority of current thread is:%d\n", __func__, current->rt_priority);
	AICWFDBG(LOGINFO, "%s the current pid is:%d\n", __func__, current->pid);


    while (1) {
		#if 0
        if(kthread_should_stop()) {
            usb_err("usb bustx thread stop 2\n");
            break;
        }
		#endif
        if (!wait_for_completion_interruptible(&bus->bustx_trgg)) {
            if(usbdev->bus_if->state == BUS_DOWN_ST){
				AICWFDBG(LOGINFO, "usb bustx thread will to stop\n");
                break;
			}
            #ifdef CONFIG_USB_TX_AGGR
            if ((usbdev->tx_post_count > 0) || !aicwf_is_framequeue_empty(&usbdev->tx_priv->txq))
            #else
            if (usbdev->tx_post_count > 0)
            #endif
                aicwf_usb_tx_process(usbdev);
        }
    }

	aic_thread_wait_stop();
	AICWFDBG(LOGINFO, "usb bustx thread stop\n");

    return 0;
}

int usb_busrx_thread(void *data)
{
    struct aicwf_rx_priv *rx_priv = (struct aicwf_rx_priv *)data;
    struct aicwf_bus *bus_if = rx_priv->usbdev->bus_if;

#ifdef CONFIG_TXRX_THREAD_PRIO
	if (busrx_thread_prio > 0) {
			struct sched_param param;
			param.sched_priority = (busrx_thread_prio < MAX_RT_PRIO)?busrx_thread_prio:(MAX_RT_PRIO-1);
			sched_setscheduler(current, SCHED_FIFO, &param);
	}
#endif
	AICWFDBG(LOGINFO, "%s the policy of current thread is:%d\n", __func__, current->policy);
	AICWFDBG(LOGINFO, "%s the rt_priority of current thread is:%d\n", __func__, current->rt_priority);
	AICWFDBG(LOGINFO, "%s the current pid is:%d\n", __func__, current->pid);

    while (1) {
#if 0
        if(kthread_should_stop()) {
            usb_err("usb busrx thread stop 2\n");
            break;
        }
#endif
		//rx_priv->rx_thread_working = 0;//AIDEN
        if (!wait_for_completion_interruptible(&bus_if->busrx_trgg)) {
            if(bus_if->state == BUS_DOWN_ST){
				AICWFDBG(LOGINFO, "usb busrx thread will to stop\n");
				break;
            }
			//rx_priv->rx_thread_working = 1;//AIDEN
            aicwf_process_rxframes(rx_priv);
        }
    }

	aic_thread_wait_stop();
	AICWFDBG(LOGINFO, "usb busrx thread stop\n");

    return 0;
}

#ifdef CONFIG_USB_MSG_IN_EP
int usb_msg_busrx_thread(void *data)
{
    struct aicwf_rx_priv *rx_priv = (struct aicwf_rx_priv *)data;
    struct aicwf_bus *bus_if = rx_priv->usbdev->bus_if;

#ifdef CONFIG_TXRX_THREAD_PRIO
			if (busrx_thread_prio > 0) {
					struct sched_param param;
					param.sched_priority = (busrx_thread_prio < MAX_RT_PRIO)?busrx_thread_prio:(MAX_RT_PRIO-1);
					sched_setscheduler(current, SCHED_FIFO, &param);
			}
#endif
			AICWFDBG(LOGINFO, "%s the policy of current thread is:%d\n", __func__, current->policy);
			AICWFDBG(LOGINFO, "%s the rt_priority of current thread is:%d\n", __func__, current->rt_priority);
			AICWFDBG(LOGINFO, "%s the current pid is:%d\n", __func__, current->pid);



    while (1) {
        if(kthread_should_stop()) {
            usb_err("usb msg busrx thread stop\n");
            break;
        }
        if (!wait_for_completion_interruptible(&bus_if->msg_busrx_trgg)) {
            if(bus_if->state == BUS_DOWN_ST)
                break;
            aicwf_process_msg_rxframes(rx_priv);
        }
    }

    return 0;
}
#endif


static void aicwf_usb_send_msg_complete(struct urb *urb)
{
    struct aic_usb_dev *usb_dev = (struct aic_usb_dev *) urb->context;

    usb_dev->msg_finished = true;
    if (waitqueue_active(&usb_dev->msg_wait))
        wake_up(&usb_dev->msg_wait);
}

static int aicwf_usb_bus_txmsg(struct device *dev, u8 *buf, u32 len)
{
    int ret = 0;
    struct aicwf_bus *bus_if = dev_get_drvdata(dev);
    struct aic_usb_dev *usb_dev = bus_if->bus_priv.usb;

    if (usb_dev->state != USB_UP_ST)
        return -EIO;

    if (buf == NULL || len == 0 || usb_dev->msg_out_urb == NULL)
        return -EINVAL;

#if 0
    if (test_and_set_bit(0, &usb_dev->msg_busy)) {
        usb_err("In a control frame option, can't tx!\n");
        return -EIO;
    }
#endif

    usb_dev->msg_finished = false;

#ifdef CONFIG_USB_MSG_OUT_EP
    if (usb_dev->msg_out_pipe) {
        usb_fill_bulk_urb(usb_dev->msg_out_urb,
            usb_dev->udev,
            usb_dev->msg_out_pipe,
            buf, len, (usb_complete_t) aicwf_usb_send_msg_complete, usb_dev);
    } else {
        usb_fill_bulk_urb(usb_dev->msg_out_urb,
            usb_dev->udev,
            usb_dev->bulk_out_pipe,
            buf, len, (usb_complete_t) aicwf_usb_send_msg_complete, usb_dev);
    }
#else
    usb_fill_bulk_urb(usb_dev->msg_out_urb,
        usb_dev->udev,
        usb_dev->bulk_out_pipe,
        buf, len, (usb_complete_t) aicwf_usb_send_msg_complete, usb_dev);
#endif
    #if defined CONFIG_USB_NO_TRANS_DMA_MAP
    usb_dev->msg_out_urb->transfer_dma = usb_dev->cmd_dma_trans_addr;
    usb_dev->msg_out_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
    #endif
    usb_dev->msg_out_urb->transfer_flags |= URB_ZERO_PACKET;

    ret = usb_submit_urb(usb_dev->msg_out_urb, GFP_ATOMIC);
    if (ret) {
        usb_err("usb_submit_urb failed %d\n", ret);
        goto exit;
    }

    ret = wait_event_timeout(usb_dev->msg_wait,
        usb_dev->msg_finished, msecs_to_jiffies(CMD_TX_TIMEOUT));
    if (!ret) {
        if (usb_dev->msg_out_urb)
            usb_kill_urb(usb_dev->msg_out_urb);
        usb_err("Txmsg wait timed out\n");
        ret = -EIO;
        goto exit;
    }

    if (usb_dev->msg_finished == false) {
        usb_err("Txmsg timed out\n");
        ret = -ETIMEDOUT;
        goto exit;
    }
exit:
#if 0
    clear_bit(0, &usb_dev->msg_busy);
#endif
    return ret;
}


static void aicwf_usb_free_urb(struct list_head *q, spinlock_t *qlock)
{
    struct aicwf_usb_buf *usb_buf, *tmp;
    unsigned long flags;

    spin_lock_irqsave(qlock, flags);
    list_for_each_entry_safe(usb_buf, tmp, q, list) {
    spin_unlock_irqrestore(qlock, flags);
        if (!usb_buf->urb) {
            usb_err("bad usb_buf\n");
            spin_lock_irqsave(qlock, flags);
            break;
        }
        #ifdef CONFIG_USB_TX_AGGR
        if (usb_buf->skb) {
            dev_kfree_skb(usb_buf->skb);
        }
        #endif
        usb_free_urb(usb_buf->urb);
        #if defined CONFIG_USB_NO_TRANS_DMA_MAP
        // free dma buf if needed
        if (usb_buf->data_buf) {
            #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
            usb_free_coherent(usb_buf->usbdev->udev, DATA_BUF_MAX, usb_buf->data_buf, usb_buf->data_dma_trans_addr);
            #else
            usb_buffer_free(usb_buf->usbdev->udev, DATA_BUF_MAX, usb_buf->data_buf, usb_buf->data_dma_trans_addr);
            #endif
            usb_buf->data_buf = NULL;
            usb_buf->data_dma_trans_addr = 0x0;
        }
        #endif
        list_del_init(&usb_buf->list);
        spin_lock_irqsave(qlock, flags);
    }
    spin_unlock_irqrestore(qlock, flags);
}

static int aicwf_usb_alloc_rx_urb(struct aic_usb_dev *usb_dev)
{
    int i;

	AICWFDBG(LOGINFO, "%s AICWF_USB_RX_URBS:%d \r\n", __func__, AICWF_USB_RX_URBS);
    for (i = 0; i < AICWF_USB_RX_URBS; i++) {
        struct aicwf_usb_buf *usb_buf = &usb_dev->usb_rx_buf[i];

        usb_buf->usbdev = usb_dev;
        usb_buf->urb = usb_alloc_urb(0, GFP_KERNEL);
        if (!usb_buf->urb) {
            usb_err("could not allocate rx data urb\n");
            goto err;
        }
        #if defined CONFIG_USB_NO_TRANS_DMA_MAP
        // dma buf unused
        usb_buf->data_buf = NULL;
        usb_buf->data_dma_trans_addr = 0x0;
        #endif
        list_add_tail(&usb_buf->list, &usb_dev->rx_free_list);
    }
    return 0;

err:
    aicwf_usb_free_urb(&usb_dev->rx_free_list, &usb_dev->rx_free_lock);
    return -ENOMEM;
}

static int aicwf_usb_alloc_tx_urb(struct aic_usb_dev *usb_dev)
{
    int i;

	AICWFDBG(LOGINFO, "%s AICWF_USB_TX_URBS:%d \r\n", __func__, AICWF_USB_TX_URBS);
    for (i = 0; i < AICWF_USB_TX_URBS; i++) {
        struct aicwf_usb_buf *usb_buf = &usb_dev->usb_tx_buf[i];

        usb_buf->usbdev = usb_dev;
        usb_buf->urb = usb_alloc_urb(0, GFP_KERNEL);
        if (!usb_buf->urb) {
            usb_err("could not allocate tx data urb\n");
            goto err;
        }
        #ifdef CONFIG_USB_TX_AGGR
        usb_buf->skb = dev_alloc_skb(MAX_USB_AGGR_TXPKT_LEN);
        #endif
        #if defined CONFIG_USB_NO_TRANS_DMA_MAP
        // alloc dma buf
        #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
        usb_buf->data_buf = usb_alloc_coherent(usb_dev->udev, DATA_BUF_MAX, (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL), &usb_buf->data_dma_trans_addr);
        #else
        usb_buf->data_buf = usb_buffer_alloc(usb_dev->udev, DATA_BUF_MAX, (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL), &usb_buf->data_dma_trans_addr);
        #endif
        if (usb_buf->data_buf == NULL) {
            usb_err("could not allocate tx data dma buf\n");
            goto err;
        }
        #endif
        list_add_tail(&usb_buf->list, &usb_dev->tx_free_list);
        (usb_dev->tx_free_count)++;
    }
    return 0;

err:
    aicwf_usb_free_urb(&usb_dev->tx_free_list, &usb_dev->tx_free_lock);
    return -ENOMEM;
}

#ifdef CONFIG_USB_MSG_IN_EP
static int aicwf_usb_alloc_msg_rx_urb(struct aic_usb_dev *usb_dev)
{
    int i;
    
    AICWFDBG(LOGINFO, "%s AICWF_USB_MSG_RX_URBS:%d \r\n", __func__, AICWF_USB_MSG_RX_URBS);

    for (i = 0; i < AICWF_USB_MSG_RX_URBS; i++) {
        struct aicwf_usb_buf *usb_buf = &usb_dev->usb_msg_rx_buf[i];

        usb_buf->usbdev = usb_dev;
        usb_buf->urb = usb_alloc_urb(0, GFP_KERNEL);
        if (!usb_buf->urb) {
            usb_err("could not allocate rx data urb\n");
            goto err;
        }
        list_add_tail(&usb_buf->list, &usb_dev->msg_rx_free_list);
    }
    return 0;

err:
    aicwf_usb_free_urb(&usb_dev->msg_rx_free_list, &usb_dev->msg_rx_free_lock);
    return -ENOMEM;
}
#endif

static void aicwf_usb_state_change(struct aic_usb_dev *usb_dev, int state)
{
    int old_state;

    if (usb_dev->state == state)
        return;

    old_state = usb_dev->state;
    usb_dev->state = state;

    if (state == USB_DOWN_ST) {
        usb_dev->bus_if->state = BUS_DOWN_ST;
    }
    if (state == USB_UP_ST) {
        usb_dev->bus_if->state = BUS_UP_ST;
    }
}

int align_param = 8;
module_param(align_param, int, 0660);

static void usb_tx_flow_ctrl(struct rwnx_txhdr *txhdr, struct aic_usb_dev *usb_dev, struct rwnx_hw *rwnx_hw)
{
#ifdef CONFIG_PER_STA_FC
	struct rwnx_sta *sta;
	u8 sta_idx;
	unsigned long flags;

	//printk("txdata: sta %d\n", txhdr->sw_hdr->desc.host.staid);
	sta_idx = txhdr->sw_hdr->desc.host.staid;
	if(sta_idx < NX_REMOTE_STA_MAX && !(txhdr->sw_hdr->desc.host.flags & TXU_CNTRL_MGMT)) {
		struct rwnx_vif *vif = NULL;
		sta = &rwnx_hw->sta_table[sta_idx];
		vif = rwnx_hw->vif_table[sta->vif_idx];
		spin_lock_irqsave(&usb_dev->tx_flow_lock, flags);
		atomic_inc(&rwnx_hw->sta_flowctrl[sta_idx].tx_pending_cnt);
		//printk("sta %d pending %d >= 64, flowctrl=%d\n", sta->sta_idx, sta->tx_pending_cnt, sta->flowctrl);
		if(RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP) {
			if((atomic_read(&rwnx_hw->sta_flowctrl[sta_idx].tx_pending_cnt) >= AICWF_USB_FC_PERSTA_HIGH_WATER && rwnx_hw->sta_flowctrl[sta_idx].flowctrl == 0) ||
										rwnx_hw->sta_flowctrl[sta_idx].flowctrl) {
				//AICWFDBG(LOGDEBUG, "sta 0x%x:0x%x, %d pending %d, stop\n", sta->mac_addr[4], sta->mac_addr[5], sta->sta_idx, atomic_read(&rwnx_hw->sta_flowctrl[sta_idx].tx_pending_cnt));
				if(!usb_dev->tbusy)
					rwnx_stop_sta_all_queues(sta, usb_dev->rwnx_hw);
				rwnx_hw->sta_flowctrl[sta_idx].flowctrl = 1;
			}
		}
		spin_unlock_irqrestore(&usb_dev->tx_flow_lock, flags);
	}
#endif
}

#ifdef CONFIG_USB_TX_AGGR
static int aicwf_usb_bus_txdata(struct device *dev, struct sk_buff *pkt)
{
    uint prio;
    int ret = -EBADE;
    struct aicwf_bus *bus_if = dev_get_drvdata(dev);
    struct aic_usb_dev *usbdev = bus_if->bus_priv.usb;

    //printk("%s\n", __func__);
    prio = (pkt->priority & 0x7);
    spin_lock_bh(&usbdev->tx_priv->txqlock);
    if (!aicwf_frame_enq(usbdev->dev, &usbdev->tx_priv->txq, pkt, prio)) {
        aicwf_dev_skb_free(pkt);
        spin_unlock_bh(&usbdev->tx_priv->txqlock);
        return -ENOSR;
    } else {
        ret = 0;
    }

    if (bus_if->state != BUS_UP_ST) {
        usb_err("bus_if stopped\n");
        spin_unlock_bh(&usbdev->tx_priv->txqlock);
        return -1;
    }

    atomic_inc(&usbdev->tx_priv->tx_pktcnt);
    spin_unlock_bh(&usbdev->tx_priv->txqlock);
    complete(&bus_if->bustx_trgg);

    return ret;
}

#else
static int aicwf_usb_bus_txdata(struct device *dev, struct sk_buff *skb)
{
    u8 *buf;
    u16 buf_len = 0;
    u16 adjust_len = 0;
    struct aicwf_usb_buf *usb_buf;
    int ret = 0;
    unsigned long flags;
    struct aicwf_bus *bus_if = dev_get_drvdata(dev);
    struct aic_usb_dev *usb_dev = bus_if->bus_priv.usb;
    struct rwnx_txhdr *txhdr = (struct rwnx_txhdr *)skb->data;
    struct rwnx_hw *rwnx_hw = usb_dev->rwnx_hw;
    u8 usb_header[4];
    u8 adj_buf[4] = {0};
    u16 index = 0;
    bool need_cfm = false;
#ifdef CONFIG_USB_ALIGN_DATA//AIDEN
	int align;
#endif

    if (usb_dev->state != USB_UP_ST) {
        usb_err("usb state is not up!\n");
        kmem_cache_free(rwnx_hw->sw_txhdr_cache, txhdr->sw_hdr);
        dev_kfree_skb_any(skb);
        return -EIO;
    }

    usb_buf = aicwf_usb_tx_dequeue(usb_dev, &usb_dev->tx_free_list,
                        &usb_dev->tx_free_count, &usb_dev->tx_free_lock);
    if (!usb_buf) {
        usb_err("free:%d, post:%d\n", usb_dev->tx_free_count, usb_dev->tx_post_count);
        kmem_cache_free(rwnx_hw->sw_txhdr_cache, txhdr->sw_hdr);
        dev_kfree_skb_any(skb);
        ret = -ENOMEM;
        goto flow_ctrl;
    }

    usb_tx_flow_ctrl(txhdr, usb_dev, rwnx_hw);

    if (txhdr->sw_hdr->need_cfm) {
        need_cfm = true;
        #if defined CONFIG_USB_NO_TRANS_DMA_MAP
        buf = usb_buf->data_buf;
        #else
        buf = kmalloc(skb->len + 1, GFP_ATOMIC/*GFP_KERNEL*/);
        #endif
        index += sizeof(usb_header);
        memcpy(&buf[index], (u8 *)(long)&txhdr->sw_hdr->desc, sizeof(struct txdesc_api));
        index += sizeof(struct txdesc_api);
        memcpy(&buf[index], &skb->data[txhdr->sw_hdr->headroom], skb->len - txhdr->sw_hdr->headroom);
        index += skb->len - txhdr->sw_hdr->headroom;
        buf_len = index;
        if (buf_len & (TX_ALIGNMENT - 1)) {
            adjust_len = roundup(buf_len, TX_ALIGNMENT)-buf_len;
            memcpy(&buf[buf_len], adj_buf, adjust_len);
            buf_len += adjust_len;
        }
        usb_header[0] =((buf_len) & 0xff);
        usb_header[1] =(((buf_len) >> 8)&0x0f);
        usb_header[2] = 0x01; //data
        usb_header[3] = 0; //reserved
        memcpy(&buf[0], usb_header, sizeof(usb_header));
        usb_buf->skb = (struct sk_buff *)buf;
    } else {
        skb_pull(skb, txhdr->sw_hdr->headroom);
        skb_push(skb, sizeof(struct txdesc_api));
        memcpy(&skb->data[0], (u8 *)(long)&txhdr->sw_hdr->desc, sizeof(struct txdesc_api));
        kmem_cache_free(rwnx_hw->sw_txhdr_cache, txhdr->sw_hdr);

        skb_push(skb, sizeof(usb_header));
        usb_header[0] =((skb->len) & 0xff);
        usb_header[1] =(((skb->len) >> 8)&0x0f);
        usb_header[2] = 0x01; //data
        usb_header[3] = 0; //reserved
        memcpy(&skb->data[0], usb_header, sizeof(usb_header));

        #if defined CONFIG_USB_NO_TRANS_DMA_MAP
        buf = usb_buf->data_buf;
        memcpy(&buf[0], skb->data, skb->len);
        #else
        buf = skb->data;
        #endif
        buf_len = skb->len;

        usb_buf->skb = skb;
    }
    usb_buf->usbdev = usb_dev;
    if (need_cfm)
        usb_buf->cfm = true;
    else
        usb_buf->cfm = false;


#ifndef CONFIG_USE_USB_ZERO_PACKET
	if((buf_len % 512) == 0){
		printk("%s send zero package buf_len: %d\r\n", __func__, buf_len);
		if(txhdr->sw_hdr->need_cfm){
			buf[buf_len] = 0x00;
			buf_len = buf_len + 1;
		}else{
			skb_put(skb, 1);
			skb->data[buf_len] = 0x00;
			buf = skb->data;
       		buf_len = skb->len;
		}
	}
#endif

#ifdef CONFIG_USB_ALIGN_DATA
    #if defined CONFIG_USB_NO_TRANS_DMA_MAP
    #error "CONFIG_USB_NO_TRANS_DMA_MAP not supported"
    #endif
	usb_buf->usb_align_data = (u8*)kmalloc(sizeof(u8) * buf_len + align_param, GFP_ATOMIC);

	align = ((unsigned long)(usb_buf->usb_align_data)) & (align_param - 1);
	memcpy(usb_buf->usb_align_data + (align_param - align), buf, buf_len);

    usb_fill_bulk_urb(usb_buf->urb, usb_dev->udev, usb_dev->bulk_out_pipe,
                usb_buf->usb_align_data + (align_param - align), buf_len, aicwf_usb_tx_complete, usb_buf);
#else
	usb_fill_bulk_urb(usb_buf->urb, usb_dev->udev, usb_dev->bulk_out_pipe,
			buf, buf_len, aicwf_usb_tx_complete, usb_buf);
#endif

    #if defined CONFIG_USB_NO_TRANS_DMA_MAP
    usb_buf->urb->transfer_dma = usb_buf->data_dma_trans_addr;
    usb_buf->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
    #endif
#ifdef CONFIG_USE_USB_ZERO_PACKET
    usb_buf->urb->transfer_flags |= URB_ZERO_PACKET;
#endif

    aicwf_usb_tx_queue(usb_dev, &usb_dev->tx_post_list, usb_buf,
                    &usb_dev->tx_post_count, &usb_dev->tx_post_lock);

#ifdef CONFIG_TX_TASKLET
	tasklet_schedule(&usb_dev->xmit_tasklet);
#else
	complete(&bus_if->bustx_trgg);
#endif

    ret = 0;

    flow_ctrl:
    spin_lock_irqsave(&usb_dev->tx_flow_lock, flags);
    if (usb_dev->tx_free_count < AICWF_USB_TX_LOW_WATER) {
		AICWFDBG(LOGDEBUG, "usb_dev->tx_free_count < AICWF_USB_TX_LOW_WATER:%d\r\n",
			usb_dev->tx_free_count);
        usb_dev->tbusy = true;
        aicwf_usb_tx_flowctrl(usb_dev->rwnx_hw, true);
    }
    spin_unlock_irqrestore(&usb_dev->tx_flow_lock, flags);

    return ret;
}
#endif
static int aicwf_usb_bus_start(struct device *dev)
{
    struct aicwf_bus *bus_if = dev_get_drvdata(dev);
    struct aic_usb_dev *usb_dev = bus_if->bus_priv.usb;

    if (usb_dev->state == USB_UP_ST)
        return 0;

    aicwf_usb_state_change(usb_dev, USB_UP_ST);
    aicwf_usb_rx_prepare(usb_dev);
    aicwf_usb_tx_prepare(usb_dev);
#ifdef CONFIG_USB_MSG_IN_EP
	if(usb_dev->chipid != PRODUCT_ID_AIC8801){
		aicwf_usb_msg_rx_prepare(usb_dev);
	}
#endif

    return 0;
}

static void aicwf_usb_cancel_all_urbs_(struct aic_usb_dev *usb_dev)
{
    struct aicwf_usb_buf *usb_buf, *tmp;
    unsigned long flags;

    if (usb_dev->msg_out_urb)
        usb_kill_urb(usb_dev->msg_out_urb);

    spin_lock_irqsave(&usb_dev->tx_post_lock, flags);
    list_for_each_entry_safe(usb_buf, tmp, &usb_dev->tx_post_list, list) {
        spin_unlock_irqrestore(&usb_dev->tx_post_lock, flags);
        if (!usb_buf->urb) {
            usb_err("bad usb_buf\n");
            spin_lock_irqsave(&usb_dev->tx_post_lock, flags);
            break;
        }
        usb_kill_urb(usb_buf->urb);
        #if defined CONFIG_USB_NO_TRANS_DMA_MAP
        // free dma buf if needed
        if (usb_buf->data_buf) {
            #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
            usb_free_coherent(usb_buf->usbdev->udev, DATA_BUF_MAX, usb_buf->data_buf, usb_buf->data_dma_trans_addr);
            #else
            usb_buffer_free(usb_buf->usbdev->udev, DATA_BUF_MAX, usb_buf->data_buf, usb_buf->data_dma_trans_addr);
            #endif
            usb_buf->data_buf = NULL;
            usb_buf->data_dma_trans_addr = 0x0;
        } else {
            usb_err("bad usb dma buf\n");
            spin_lock_irqsave(&usb_dev->tx_post_lock, flags);
            break;
        }
        #endif
        spin_lock_irqsave(&usb_dev->tx_post_lock, flags);
    }
    spin_unlock_irqrestore(&usb_dev->tx_post_lock, flags);

    usb_kill_anchored_urbs(&usb_dev->rx_submitted);
#ifdef CONFIG_USB_MSG_IN_EP
	if(usb_dev->chipid != PRODUCT_ID_AIC8801){
   		usb_kill_anchored_urbs(&usb_dev->msg_rx_submitted);
	}
#endif
}

void aicwf_usb_cancel_all_urbs(struct aic_usb_dev *usb_dev){
	aicwf_usb_cancel_all_urbs_(usb_dev);
}


static void aicwf_usb_bus_stop(struct device *dev)
{
    struct aicwf_bus *bus_if = dev_get_drvdata(dev);
    struct aic_usb_dev *usb_dev = bus_if->bus_priv.usb;

	AICWFDBG(LOGINFO, "%s\r\n", __func__);
    if (usb_dev == NULL)
        return;

    if (usb_dev->state == USB_DOWN_ST)
        return;

    if(g_rwnx_plat->wait_disconnect_cb == true){
            atomic_set(&aicwf_deinit_atomic, 1);
            up(&aicwf_deinit_sem);
    }
    aicwf_usb_state_change(usb_dev, USB_DOWN_ST);
    //aicwf_usb_cancel_all_urbs(usb_dev);//AIDEN
}

static void aicwf_usb_deinit(struct aic_usb_dev *usbdev)
{
    cancel_work_sync(&usbdev->rx_urb_work);
    aicwf_usb_free_urb(&usbdev->rx_free_list, &usbdev->rx_free_lock);
    aicwf_usb_free_urb(&usbdev->tx_free_list, &usbdev->tx_free_lock);
#ifdef CONFIG_USB_MSG_IN_EP
	if(usbdev->chipid != PRODUCT_ID_AIC8801){
		cancel_work_sync(&usbdev->msg_rx_urb_work);
		aicwf_usb_free_urb(&usbdev->msg_rx_free_list, &usbdev->msg_rx_free_lock);
	}
#endif

    usb_free_urb(usbdev->msg_out_urb);
}

static void aicwf_usb_rx_urb_work(struct work_struct *work)
{
    struct aic_usb_dev *usb_dev = container_of(work, struct aic_usb_dev, rx_urb_work);

    aicwf_usb_rx_submit_all_urb(usb_dev);
}

#ifdef CONFIG_USB_MSG_IN_EP
static void aicwf_usb_msg_rx_urb_work(struct work_struct *work)
{
    struct aic_usb_dev *usb_dev = container_of(work, struct aic_usb_dev, msg_rx_urb_work);

    aicwf_usb_msg_rx_submit_all_urb(usb_dev);
}
#endif

static int aicwf_usb_init(struct aic_usb_dev *usb_dev)
{
    int ret = 0;

    usb_dev->tbusy = false;
    usb_dev->state = USB_DOWN_ST;

    init_waitqueue_head(&usb_dev->msg_wait);
    init_usb_anchor(&usb_dev->rx_submitted);
#ifdef CONFIG_USB_MSG_IN_EP
	if(usb_dev->chipid != PRODUCT_ID_AIC8801){
		init_usb_anchor(&usb_dev->msg_rx_submitted);
	}
#endif

    spin_lock_init(&usb_dev->tx_free_lock);
    spin_lock_init(&usb_dev->tx_post_lock);
    spin_lock_init(&usb_dev->rx_free_lock);
    spin_lock_init(&usb_dev->tx_flow_lock);
#ifdef CONFIG_USB_MSG_IN_EP
	if(usb_dev->chipid != PRODUCT_ID_AIC8801){
		spin_lock_init(&usb_dev->msg_rx_free_lock);
	}
#endif

    INIT_LIST_HEAD(&usb_dev->rx_free_list);
    INIT_LIST_HEAD(&usb_dev->tx_free_list);
    INIT_LIST_HEAD(&usb_dev->tx_post_list);
#ifdef CONFIG_USB_MSG_IN_EP
	if(usb_dev->chipid != PRODUCT_ID_AIC8801){
		INIT_LIST_HEAD(&usb_dev->msg_rx_free_list);
	}
#endif

	atomic_set(&rx_urb_cnt, 0);

    usb_dev->tx_free_count = 0;
    usb_dev->tx_post_count = 0;

    ret =  aicwf_usb_alloc_rx_urb(usb_dev);
    if (ret) {
        goto error;
    }
    ret =  aicwf_usb_alloc_tx_urb(usb_dev);
    if (ret) {
        goto error;
    }
#ifdef CONFIG_USB_MSG_IN_EP
	if(usb_dev->chipid != PRODUCT_ID_AIC8801){
		ret =  aicwf_usb_alloc_msg_rx_urb(usb_dev);
		if (ret) {
			goto error;
		}
	}
#endif


    usb_dev->msg_out_urb = usb_alloc_urb(0, GFP_ATOMIC);
    if (!usb_dev->msg_out_urb) {
        usb_err("usb_alloc_urb (msg out) failed\n");
        ret = ENOMEM;
        goto error;
    }

    INIT_WORK(&usb_dev->rx_urb_work, aicwf_usb_rx_urb_work);
#ifdef CONFIG_USB_MSG_IN_EP
	if(usb_dev->chipid != PRODUCT_ID_AIC8801){
		INIT_WORK(&usb_dev->msg_rx_urb_work, aicwf_usb_msg_rx_urb_work);
	}
#endif

    return ret;
    error:
    usb_err("failed!\n");
    aicwf_usb_deinit(usb_dev);
    return ret;
}


static int aicwf_parse_usb(struct aic_usb_dev *usb_dev, struct usb_interface *interface)
{
    struct usb_interface_descriptor *interface_desc;
    struct usb_host_interface *host_interface;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_device *usb = usb_dev->udev;
    int i, endpoints;
    u8 endpoint_num;
    int ret = 0;

    usb_dev->bulk_in_pipe = 0;
    usb_dev->bulk_out_pipe = 0;
#ifdef CONFIG_USB_MSG_OUT_EP
    usb_dev->msg_out_pipe = 0;
#endif
#ifdef CONFIG_USB_MSG_IN_EP
	usb_dev->msg_in_pipe = 0;
#endif

    host_interface = &interface->altsetting[0];
    interface_desc = &host_interface->desc;
    endpoints = interface_desc->bNumEndpoints;
	AICWFDBG(LOGINFO, "%s endpoints = %d\n", __func__, endpoints);

    /* Check device configuration */
    if (usb->descriptor.bNumConfigurations != 1) {
        usb_err("Number of configurations: %d not supported\n",
                        usb->descriptor.bNumConfigurations);
        ret = -ENODEV;
        goto exit;
    }

    /* Check deviceclass */
#ifndef CONFIG_USB_BT
    if (usb->descriptor.bDeviceClass != 0x00) {
        usb_err("DeviceClass %d not supported\n",
            usb->descriptor.bDeviceClass);
        ret = -ENODEV;
        goto exit;
    }
#endif

    /* Check interface number */
#ifdef CONFIG_USB_BT
    if (usb->actconfig->desc.bNumInterfaces != 3) {
#else
    if (usb->actconfig->desc.bNumInterfaces != 1) {
#endif
	   AICWFDBG(LOGERROR, "Number of interfaces: %d not supported\n",
            usb->actconfig->desc.bNumInterfaces);
		if(usb_dev->chipid == PRODUCT_ID_AIC8800DC){
			AICWFDBG(LOGERROR, "AIC8800DC change to AIC8800DW\n");
			usb_dev->chipid = PRODUCT_ID_AIC8800DW;
		}else{
			ret = -ENODEV;
			goto exit;
		}
    }

    if ((interface_desc->bInterfaceClass != USB_CLASS_VENDOR_SPEC) ||
        (interface_desc->bInterfaceSubClass != 0xff) ||
        (interface_desc->bInterfaceProtocol != 0xff)) {
        usb_err("non WLAN interface %d: 0x%x:0x%x:0x%x\n",
            interface_desc->bInterfaceNumber, interface_desc->bInterfaceClass,
            interface_desc->bInterfaceSubClass, interface_desc->bInterfaceProtocol);
        ret = -ENODEV;
        goto exit;
    }

    for (i = 0; i < endpoints; i++) {
        endpoint = &host_interface->endpoint[i].desc;
        endpoint_num = usb_endpoint_num(endpoint);

        if (usb_endpoint_dir_in(endpoint) &&
            usb_endpoint_xfer_bulk(endpoint)) {
            if (!usb_dev->bulk_in_pipe) {
                usb_dev->bulk_in_pipe = usb_rcvbulkpipe(usb, endpoint_num);
            }
#ifdef CONFIG_USB_MSG_IN_EP
            else if (!usb_dev->msg_in_pipe) {
				if(usb_dev->chipid != PRODUCT_ID_AIC8801){
                	usb_dev->msg_in_pipe = usb_rcvbulkpipe(usb, endpoint_num);
				}
            }
#endif
        }

        if (usb_endpoint_dir_out(endpoint) &&
            usb_endpoint_xfer_bulk(endpoint)) {
            if (!usb_dev->bulk_out_pipe)
            {
                usb_dev->bulk_out_pipe = usb_sndbulkpipe(usb, endpoint_num);
            }
#ifdef CONFIG_USB_MSG_OUT_EP
             else if (!usb_dev->msg_out_pipe) {
                usb_dev->msg_out_pipe = usb_sndbulkpipe(usb, endpoint_num);
            }
#endif

        }
    }

    if (usb_dev->bulk_in_pipe == 0) {
        usb_err("No RX (in) Bulk EP found\n");
        ret = -ENODEV;
        goto exit;
    }
    if (usb_dev->bulk_out_pipe == 0) {
        usb_err("No TX (out) Bulk EP found\n");
        ret = -ENODEV;
        goto exit;
    }
#ifdef CONFIG_USB_MSG_OUT_EP
    if (usb_dev->msg_out_pipe == 0) {
        usb_err("No TX Msg (out) Bulk EP found\n");
    }
#endif
#ifdef CONFIG_USB_MSG_IN_EP
		if(usb_dev->chipid != PRODUCT_ID_AIC8801){
			if (usb_dev->msg_in_pipe == 0) {
				usb_err("No RX Msg (in) Bulk EP found\n");
			}
		}
#endif

    if (usb->speed == USB_SPEED_HIGH){
		AICWFDBG(LOGINFO, "Aic high speed USB device detected\n");
    }else{
    	AICWFDBG(LOGINFO, "Aic high speed USB device detected\n");
    }

    exit:
    return ret;
}



static struct aicwf_bus_ops aicwf_usb_bus_ops = {
    .start = aicwf_usb_bus_start,
    .stop = aicwf_usb_bus_stop,
    .txdata = aicwf_usb_bus_txdata,
    .txmsg = aicwf_usb_bus_txmsg,
};


#ifdef CONFIG_GPIO_WAKEUP

static irqreturn_t rwnx_irq_handler(int irq, void *para)
{
	unsigned long irqflags;
    spin_lock_irqsave(&irq_lock, irqflags);
    disable_irq_nosync(hostwake_irq_num);
    //do something
    printk("%s gpio irq trigger\r\n", __func__);
    spin_unlock_irqrestore(&irq_lock, irqflags);
    atomic_dec(&irq_count);
	return IRQ_HANDLED;
}


static int rwnx_register_hostwake_irq(struct device *dev)
{
	int ret = 0;
	uint irq_flags = 0;

	spin_lock_init(&irq_lock);

//Setting hostwake gpio for platform
//For Rockchip
#ifdef CONFIG_PLATFORM_ROCKCHIP
	hostwake_irq_num = rockchip_wifi_get_oob_irq();
	printk("%s hostwake_irq_num:%d \r\n", __func__, hostwake_irq_num);
	irq_flags = (IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE) & IRQF_TRIGGER_MASK;
	printk("%s irq_flags:%d \r\n", __func__, irq_flags);
	wakeup_enable = 1;
#endif //CONFIG_PLATFORM_ROCKCHIP

//For Allwinner
#ifdef CONFIG_PLATFORM_ALLWINNER
		int irq_flags;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
	hostwake_irq_num = sunxi_wlan_get_oob_irq(&irq_flags, &wakeup_enable);
#else
	hostwake_irq_num = sunxi_wlan_get_oob_irq();
	irq_flags = sunxi_wlan_get_oob_irq_flags();
	wakeup_enable = 1;
#endif
#endif //CONFIG_PLATFORM_ALLWINNER


	ret = request_irq(hostwake_irq_num,
				rwnx_irq_handler, IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND,
				"rwnx_irq_handler", NULL);

	enable_irq_wake(hostwake_irq_num);

	return ret;
}

static int rwnx_unregister_hostwake_irq(struct device *dev)
{
	wakeup_enable = 0;

	printk("%s hostwake_irq_num:%d \r\n", __func__, hostwake_irq_num);
	disable_irq_wake(hostwake_irq_num);
	free_irq(hostwake_irq_num, NULL);

	return 0;
}

#endif //CONFIG_GPIO_WAKEUP

static int aicwf_usb_chipmatch(struct aic_usb_dev *usb_dev, u16_l vid, u16_l pid){

	if(pid == USB_PRODUCT_ID_AIC8801){
		usb_dev->chipid = PRODUCT_ID_AIC8801;
		AICWFDBG(LOGINFO, "%s USE AIC8801\r\n", __func__);
		return 0;
	}else if(pid == USB_PRODUCT_ID_AIC8800DC){
		usb_dev->chipid = PRODUCT_ID_AIC8800DC;
		AICWFDBG(LOGINFO, "%s USE AIC8800DC\r\n", __func__);
		return 0;
	}else if(pid == USB_PRODUCT_ID_AIC8800DW){
        usb_dev->chipid = PRODUCT_ID_AIC8800DW;
		AICWFDBG(LOGINFO, "%s USE AIC8800DW\r\n", __func__);
        return 0;
    }else{
		return -1;
	}
}


static int aicwf_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    int ret = 0;
    struct usb_device *usb = interface_to_usbdev(intf);
    struct aicwf_bus *bus_if = NULL;
    struct device *dev = NULL;
    struct aicwf_rx_priv* rx_priv = NULL;
    struct aic_usb_dev *usb_dev = NULL;
    #ifdef CONFIG_USB_TX_AGGR
    struct aicwf_tx_priv *tx_priv = NULL;
    #endif

    usb_dev = kzalloc(sizeof(struct aic_usb_dev), GFP_ATOMIC);
    if (!usb_dev) {
        return -ENOMEM;
    }

    usb_dev->udev = usb;
    usb_dev->dev = &usb->dev;
    usb_set_intfdata(intf, usb_dev);
	
	ret = aicwf_usb_chipmatch(usb_dev, id->idVendor, id->idProduct);
	
	if (ret < 0) {
        AICWFDBG(LOGERROR, "%s pid:0x%04X vid:0x%04X unsupport\n", 
			__func__, id->idVendor, id->idProduct);
        goto out_free_bus;
    }

    ret = aicwf_parse_usb(usb_dev, intf);
    if (ret) {
        AICWFDBG(LOGERROR, "aicwf_parse_usb err %d\n", ret);
        goto out_free;
    }

    ret = aicwf_usb_init(usb_dev);
    if (ret) {
        AICWFDBG(LOGERROR, "aicwf_usb_init err %d\n", ret);
        goto out_free;
    }

    bus_if = kzalloc(sizeof(struct aicwf_bus), GFP_ATOMIC);
    if (!bus_if) {
        ret = -ENOMEM;
        goto out_free_usb;
    }

    dev = usb_dev->dev;
    bus_if->dev = dev;
    usb_dev->bus_if = bus_if;
    bus_if->bus_priv.usb = usb_dev;
    dev_set_drvdata(dev, bus_if);

    bus_if->ops = &aicwf_usb_bus_ops;

    rx_priv = aicwf_rx_init(usb_dev);
    if(!rx_priv) {
       AICWFDBG(LOGERROR, "rx init failed\n");
        ret = -1;
        goto out_free_bus;
    }
    usb_dev->rx_priv = rx_priv;

#ifdef CONFIG_USB_TX_AGGR
    tx_priv = aicwf_tx_init(usb_dev);
    if(!tx_priv) {
        usb_err("tx init fail\n");
        goto out_free_bus;
    }
    usb_dev->tx_priv = tx_priv;
    aicwf_frame_queue_init(&tx_priv->txq, 8, TXQLEN);
    spin_lock_init(&tx_priv->txqlock);
    spin_lock_init(&tx_priv->txdlock);
#endif

    ret = aicwf_bus_init(0, dev);
    if (ret < 0) {
        AICWFDBG(LOGERROR, "aicwf_bus_init err %d\n", ret);
        goto out_free_bus;
    }

    ret = aicwf_bus_start(bus_if);
    if (ret < 0) {
        AICWFDBG(LOGERROR, "aicwf_bus_start err %d\n", ret);
        goto out_free_bus;
    }

    ret = aicwf_rwnx_usb_platform_init(usb_dev);
	if (ret < 0) {
        AICWFDBG(LOGERROR, "aicwf_rwnx_usb_platform_init err %d\n", ret);
        goto out_free_bus;
    }
    aicwf_hostif_ready();

#ifdef CONFIG_GPIO_WAKEUP
	rwnx_register_hostwake_irq(usb_dev->dev);
#endif

    return 0;

out_free_bus:
    aicwf_bus_deinit(dev);
    kfree(bus_if);
out_free_usb:
    aicwf_usb_deinit(usb_dev);
out_free:
    usb_err("failed with errno %d\n", ret);
    kfree(usb_dev);
    usb_set_intfdata(intf, NULL);
    return ret;
}

static void aicwf_usb_disconnect(struct usb_interface *intf)
{
    struct aic_usb_dev *usb_dev =
            (struct aic_usb_dev *) usb_get_intfdata(intf);
        AICWFDBG(LOGINFO, "%s Enter\r\n", __func__);

	if(g_rwnx_plat->wait_disconnect_cb == false){
		atomic_set(&aicwf_deinit_atomic, 0);
		down(&aicwf_deinit_sem);
	}

    if (!usb_dev){
		AICWFDBG(LOGERROR, "%s usb_dev is null \r\n", __func__);
        return;
    }

#if 0
	if(timer_pending(&usb_dev->rwnx_hw->p2p_alive_timer) && usb_dev->rwnx_hw->is_p2p_alive == 1){
		printk("%s del timer rwnx_hw->p2p_alive_timer \r\n", __func__);
		rwnx_del_timer(&usb_dev->rwnx_hw->p2p_alive_timer);
	}
#endif
    aicwf_bus_deinit(usb_dev->dev);
    aicwf_usb_deinit(usb_dev);
    rwnx_cmd_mgr_deinit(&usb_dev->cmd_mgr);

#ifdef CONFIG_GPIO_WAKEUP
	rwnx_unregister_hostwake_irq(usb_dev->dev);
#endif

    if (usb_dev->rx_priv)
        aicwf_rx_deinit(usb_dev->rx_priv);

    kfree(usb_dev->bus_if);
    kfree(usb_dev);
	AICWFDBG(LOGINFO, "%s exit\r\n", __func__);
	up(&aicwf_deinit_sem);
	atomic_set(&aicwf_deinit_atomic, 1);
}

static int aicwf_usb_suspend(struct usb_interface *intf, pm_message_t state)
{
    struct aic_usb_dev *usb_dev =
        (struct aic_usb_dev *) usb_get_intfdata(intf);
#ifdef CONFIG_GPIO_WAKEUP
	struct rwnx_vif *rwnx_vif, *tmp;
	//unsigned long irqflags;
#endif

	printk("%s enter\r\n", __func__);

#ifdef CONFIG_GPIO_WAKEUP
//	spin_lock_irqsave(&irq_lock, irqflags);
//	rwnx_enable_hostwake_irq();
//    spin_unlock_irqrestore(&irq_lock, irqflags);
    atomic_inc(&irq_count);

	list_for_each_entry_safe(rwnx_vif, tmp, &usb_dev->rwnx_hw->vifs, list) {
		if (rwnx_vif->ndev)
			netif_device_detach(rwnx_vif->ndev);
	}
#endif

	aicwf_usb_state_change(usb_dev, USB_SLEEP_ST);
    aicwf_bus_stop(usb_dev->bus_if);


    return 0;
}

static int aicwf_usb_resume(struct usb_interface *intf)
{
    struct aic_usb_dev *usb_dev =
        (struct aic_usb_dev *) usb_get_intfdata(intf);
#ifdef CONFIG_GPIO_WAKEUP
	struct rwnx_vif *rwnx_vif, *tmp;
//	unsigned long irqflags;
#endif
	printk("%s enter\r\n", __func__);

#ifdef CONFIG_GPIO_WAKEUP
//	spin_lock_irqsave(&irq_lock, irqflags);
//	rwnx_disable_hostwake_irq();
//	spin_unlock_irqrestore(&irq_lock, irqflags);
	atomic_dec(&irq_count);

	list_for_each_entry_safe(rwnx_vif, tmp, &usb_dev->rwnx_hw->vifs, list) {
		if (rwnx_vif->ndev)
			netif_device_attach(rwnx_vif->ndev);
	}
#endif

    if (usb_dev->state == USB_UP_ST)
        return 0;

    aicwf_bus_start(usb_dev->bus_if);
    return 0;
}

static int aicwf_usb_reset_resume(struct usb_interface *intf)
{
    return aicwf_usb_resume(intf);
}

static struct usb_device_id aicwf_usb_id_table[] = {
#ifndef CONFIG_USB_BT
    {USB_DEVICE(USB_VENDOR_ID_AIC, USB_PRODUCT_ID_AIC8800)},
#else
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_AIC, USB_PRODUCT_ID_AIC8801, 0xff, 0xff, 0xff)},
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_AIC, USB_PRODUCT_ID_AIC8800DC, 0xff, 0xff, 0xff)},
    {USB_DEVICE(USB_VENDOR_ID_AIC, USB_PRODUCT_ID_AIC8800DW)},
#endif
    {}
};

MODULE_DEVICE_TABLE(usb, aicwf_usb_id_table);

static struct usb_driver aicwf_usbdrvr = {
    .name = KBUILD_MODNAME,
    .probe = aicwf_usb_probe,
    .disconnect = aicwf_usb_disconnect,
    .id_table = aicwf_usb_id_table,
    .suspend = aicwf_usb_suspend,
    .resume = aicwf_usb_resume,
    .reset_resume = aicwf_usb_reset_resume,
#ifdef ANDROID_PLATFORM
    .supports_autosuspend = 1,
#else
    .supports_autosuspend = 0,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
    .disable_hub_initiated_lpm = 1,
#endif
};

void aicwf_usb_register(void)
{
    if (usb_register(&aicwf_usbdrvr) < 0) {
        usb_err("usb_register failed\n");
    }
}

void aicwf_usb_exit(void)
{
	//RWNX_DBG(RWNX_FN_ENTRY_STR);
	AICWFDBG(LOGDEBUG, "%s in_interrupt:%d in_softirq:%d in_atomic:%d\r\n", __func__, (int)in_interrupt(), (int)in_softirq(), (int)in_atomic());
	atomic_set(&aicwf_deinit_atomic, 0);
	if(down_timeout(&aicwf_deinit_sem, msecs_to_jiffies(SEM_TIMOUT)) != 0){
		AICWFDBG(LOGERROR, "%s semaphore waiting timeout\r\n", __func__);
	}

	if(g_rwnx_plat){
		g_rwnx_plat->wait_disconnect_cb = false;
	}
	
	AICWFDBG(LOGINFO, "%s Enter\r\n", __func__);

	if(!g_rwnx_plat || !g_rwnx_plat->enabled){
		AICWFDBG(LOGINFO, "g_rwnx_plat is not ready. waiting for 500ms\r\n");
		mdelay(500);
	}

#if 1
    if(g_rwnx_plat && g_rwnx_plat->enabled){
        rwnx_platform_deinit(g_rwnx_plat->usbdev->rwnx_hw);
    }
#endif

	up(&aicwf_deinit_sem);
	atomic_set(&aicwf_deinit_atomic, 1);

	AICWFDBG(LOGINFO, "%s usb_deregister \r\n", __func__);

    usb_deregister(&aicwf_usbdrvr);
	//mdelay(500);
	if(g_rwnx_plat){
    	kfree(g_rwnx_plat);
	}
	
	AICWFDBG(LOGINFO, "%s exit\r\n", __func__);

}
