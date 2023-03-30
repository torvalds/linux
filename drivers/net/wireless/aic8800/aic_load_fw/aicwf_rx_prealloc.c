// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include "aicwf_rx_prealloc.h"
#include "aicwf_debug.h"

#ifdef CONFIG_PREALLOC_RX_SKB
struct aicwf_rx_buff_list aic_rx_buff_list;

int aic_rxbuff_num_max = 100;

int aic_rxbuff_size = (4 * 512);

int rx_buff_list_ava = 0;

module_param(rx_buff_list_ava, int, 0660);

int aicwf_rxbuff_size_get(void)
{
    return aic_rxbuff_size;
}

struct rx_buff *aicwf_prealloc_rxbuff_alloc(spinlock_t *lock) 
{
    unsigned long flags;
    struct rx_buff *rxbuff = NULL;

    spin_lock_irqsave(lock, flags);
    rx_buff_list_ava = atomic_read(&aic_rx_buff_list.rxbuff_list_len);
    if(rx_buff_list_ava < 10){
        AICWFDBG(LOGERROR, "%s WARNING rxbuff is running out %d\r\n", __func__,
            rx_buff_list_ava);
    }

    if (list_empty(&aic_rx_buff_list.rxbuff_list)) {
        spin_unlock_irqrestore(lock, flags);
        AICWFDBG(LOGERROR, "%s %d, rxbuff list is empty\r\n", __func__, __LINE__);
        return NULL;
    } else {
		rxbuff = list_first_entry(&aic_rx_buff_list.rxbuff_list,
					   struct rx_buff, queue);
		list_del_init(&rxbuff->queue);
        atomic_dec(&aic_rx_buff_list.rxbuff_list_len);
	}
    spin_unlock_irqrestore(lock, flags);
    //printk("len:%d\n", aic_rx_buff_list.rxbuff_list_len);
    memset(rxbuff->data, 0, aic_rxbuff_size);
    rxbuff->len = 0;
    rxbuff->start = NULL;
    rxbuff->read = NULL;
    rxbuff->end = NULL;

    return rxbuff;
}

void aicwf_prealloc_rxbuff_free(struct rx_buff *rxbuff, spinlock_t *lock)
{
    unsigned long flags;

    spin_lock_irqsave(lock, flags);
	list_add_tail(&rxbuff->queue, &aic_rx_buff_list.rxbuff_list);
	atomic_inc(&aic_rx_buff_list.rxbuff_list_len);
    rx_buff_list_ava = atomic_read(&aic_rx_buff_list.rxbuff_list_len);
    spin_unlock_irqrestore(lock, flags);
}

int aicwf_prealloc_init()
{
    struct rx_buff *rxbuff;
    int i = 0;

    AICWFDBG(LOGINFO, "%s enter\n", __func__);
    INIT_LIST_HEAD(&aic_rx_buff_list.rxbuff_list);
    
	for (i = 0 ; i < aic_rxbuff_num_max ; i++) {
        rxbuff = kzalloc(sizeof(struct rx_buff), GFP_KERNEL);
        if (rxbuff) {
            rxbuff->data = kzalloc(aic_rxbuff_size, GFP_KERNEL);
            if (rxbuff->data == NULL) {
                AICWFDBG(LOGERROR, "failed to alloc rxbuff data\n");
                kfree(rxbuff);
                continue;
            }
            rxbuff->len = 0;
            rxbuff->start = NULL;
            rxbuff->read = NULL;
            rxbuff->end = NULL;
            list_add_tail(&rxbuff->queue, &aic_rx_buff_list.rxbuff_list);
            atomic_inc(&aic_rx_buff_list.rxbuff_list_len);
            rx_buff_list_ava = atomic_read(&aic_rx_buff_list.rxbuff_list_len);
        }
    }

	AICWFDBG(LOGINFO, "pre alloc rxbuff list len: %d\n", (int)atomic_read(&aic_rx_buff_list.rxbuff_list_len));
    return 0;
}

void aicwf_prealloc_exit()
{
    struct rx_buff *rxbuff;
    struct rx_buff *pos;
    
    AICWFDBG(LOGINFO, "%s enter\n", __func__);

	AICWFDBG(LOGINFO, "free pre alloc rxbuff list %d\n", (int)atomic_read(&aic_rx_buff_list.rxbuff_list_len));
    list_for_each_entry_safe(rxbuff, pos, &aic_rx_buff_list.rxbuff_list, queue) {
        list_del_init(&rxbuff->queue);
        kfree(rxbuff->data);
        kfree(rxbuff);
    }
}

EXPORT_SYMBOL(aicwf_rxbuff_size_get);
EXPORT_SYMBOL(aicwf_prealloc_rxbuff_alloc);
EXPORT_SYMBOL(aicwf_prealloc_rxbuff_free);

#endif

