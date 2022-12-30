/**
 ******************************************************************************
 *
 * @file sdio.c
 *
 * @brief sdio driver function definitions
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
#include <uapi/linux/sched/types.h>
//#include "debug.h"
#include "ecrnx_defs.h"
#include "ecrnx_sdio.h"
#include "sdio.h"

#include "sdio_host_interface.h"

#define SDIO_DEBUG	1


#define SDIO_ADDR_INFO		        (unsigned int)(0x081)
#define SDIO_ADDR_INFO_ASYNC		(unsigned int)(0x082)
#define SDIO_ADDR_DATA		        (unsigned int)(0x080)
#define NEXT_BUF_SZ_OFFSET			(0)
#define SLAVE_BUF_SZ_OFFSET         (4)
#define SDIO_AVL_NOTIFY_FLAG    (0x5A5A5A5A)

static atomic_t suspend;
int stop_tx= 0;

#ifdef  CONFIG_SHOW_TX_SPEED
unsigned long sdio_tx_last_jiffies;
unsigned long sdio_tx_len_totol;
unsigned long sdio_tx_error_cnt;
#endif

#ifdef  CONFIG_SHOW_RX_SPEED
unsigned long sdio_rx_last_jiffies;
unsigned long sdio_rx_len_totol;
unsigned long sdio_rx_error_cnt;
#endif


struct eswin_sdio * g_sdio;

#if SDIO_DEBUG
static struct eswin_sdio *trS;
static struct dentry *p_debugfs_sdio;

static int debugfs_sdio_read(void *data, u64 *val)
{
	struct eswin_sdio *tr_sdio = trS;

	int pdata[10];

	ECRNX_PRINT("%s\n", __func__);
	ECRNX_PRINT("%s, func: 0x%x!!", __func__, tr_sdio->func);
	ECRNX_PRINT("%s, func: 0x%x!!", __func__, tr_sdio->slave_avl_buf);
	ECRNX_PRINT("%s, data addr: 0x%x, sdio_info: 0x%x, 0x%x !!", __func__, pdata, &tr_sdio->sdio_info, &(tr_sdio->sdio_info));
#if 0
	sdio_claim_host(tr_sdio->func);
    /* replace the sdio_memcpy_fromio with sdio_readsb. in which the op code is 0, the addr will not change during transmittion */
	ret = sdio_readsb(tr_sdio->func, pdata/* &tr_sdio->sdio_info */, SDIO_ADDR_INFO, 4);
	//ret = 0;
	if (ret < 0) {
		ECRNX_PRINT(" debugfs_sdio_read failde, ret: %d\n", ret);
		//print_hex_dump(KERN_DEBUG, "status - 1 ", DUMP_PREFIX_NONE, 16, 1, &priv->sdio_info, 32, false);
		sdio_release_host(tr_sdio->func);
		return ret;
	}
	print_hex_dump(KERN_DEBUG, "status - 1 ", DUMP_PREFIX_NONE, 16, 1, pdata, 16, false);
	sdio_release_host(tr_sdio->func);
#endif
	return 0;
}

static int debugfs_sdio_write(void *data1, u64 val)
{
	int ret = 0;
	//int cmd = (int)val;
	char cmd[16] = {1,2,3,4,5,6,7,8,9,1,2,3,4,5,6,7};
	struct sk_buff * skb;
	char * p;

#if 1	
	struct eswin_sdio *tr_sdio = trS;

	ECRNX_PRINT(" %s, tr_sdio->func: %x\n", __func__, g_sdio->func);

	sdio_claim_host(tr_sdio->func);

    /* replace the sdio_memcpy_xxio with sdio_xxxxsb. in sdio_xxxxsb the op code is 0, the addr will not change during transmittion */
	ret = sdio_writesb(tr_sdio->func, 0x100, &cmd, 16);
	if (ret < 0) {
		ECRNX_PRINT(" debugfs_sdio_write failde, ret: %d\n", ret);
	}

	sdio_release_host(tr_sdio->func);
#else

	skb = dev_alloc_skb(4096);
	memset(skb->data, 0x34, 4096);

	p = (char *)(skb->data);
	for (ret=0; ret<4096; ret++) {
		*p++ = (char)ret;
	}
	
	sdio_host_send(skb->data, 4096, 0x100);
#endif
	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debugfs_sdio,
			debugfs_sdio_read,
			debugfs_sdio_write,
			"%llu\n");

void debugfs_sdio_init(void)
{
	ECRNX_PRINT("%s\n", __func__);
	p_debugfs_sdio = debugfs_create_file("sdiod", 0777, NULL, NULL, &debugfs_sdio);
}

void debug_sdio_rx_callback(struct sk_buff *skb)
{
	ECRNX_PRINT("rx-callback: %d\n", skb->len);
	print_hex_dump(KERN_DEBUG, DBG_PREFIX_SDIO_RX, DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len, false);
	dev_kfree_skb(skb);
}
#endif


static inline u16 sdio_num_slots(struct eswin_sdio *tr_sdio, int dir)
{
	return (tr_sdio->slot[dir].head - tr_sdio->slot[dir].tail);
}

#if 0
static void sdio_credit_skb(struct eswin_sdio *tr_sdio)
{
	struct sk_buff *skb;
	struct hif *hif;
	struct wim *wim;
	struct wim_credit_report *cr;
	u8 *p;
	int i;
	int size = sizeof(*hif) + sizeof(struct wim) + sizeof(*cr);

	if (!once) {
		once = true;
		return;
	}

	skb = dev_alloc_skb(size);

	p = skb->data;
	hif = (void *)p;
	hif->type = HIF_TYPE_WIM;
	hif->subtype = HIF_WIM_SUB_EVENT;
	hif->vifindex = 0;
	hif->len = sizeof(*wim) + sizeof(*cr);

	p += sizeof(*hif);
	wim = (void *)p;
	wim->event = WIM_EVENT_CREDIT_REPORT;

	p += sizeof(*wim);
	cr = (void *)p;
	cr->h.type = WIM_TLV_AC_CREDIT_REPORT;
	cr->h.len = sizeof(struct wim_credit_report_param);

	cr->v.change_index = 0;

	for (i = 0; i < CREDIT_QUEUE_MAX; i++) {
		u8 room = tr_sdio->front[i] - tr_sdio->rear[i];

		room = min(tr_sdio->credit_max[i], room);
		cr->v.ac[i] = tr_sdio->credit_max[i] - room;

#ifdef CONFIG_NRC_HIF_PRINT_FLOW_CONTROL
		ECRNX_PRINT(" credit[%d] %d f:%d, r:%d\n",
				i, cr->v.ac[i], tr_sdio->front[i],
				tr_sdio->rear[i]);
#endif
	}

	skb_put(skb, hif->len+sizeof(*hif));

    if (tr_sdio->tr->rx_callback) {
	    tr_sdio->tr->rx_callback(tr_sdio->tr->umac_priv, skb);
    } else {
        dev_kfree_skb(skb);
    }
}
#endif

static int sdio_update_status(struct eswin_sdio *tr_sdio)
{
	u32 rear;
	int ac, ret;
    u8 * buf = kmalloc(4,GFP_ATOMIC);

	//ECRNX_PRINT("%s\n", __func__);
	sdio_claim_host(tr_sdio->func);
	trS = tr_sdio;
    /* replace the sdio_memcpy_fromio with sdio_readsb. in which the op code is 0, the addr will not change during transmittion */
	ret = sdio_readsb(tr_sdio->func, buf, SDIO_ADDR_INFO_ASYNC, 4);
	if (ret < 0) {
		ECRNX_PRINT(" sdio_update_status, ret: %d\n", ret);
        kfree(buf);
		//print_hex_dump(KERN_DEBUG, "status - 1 ", DUMP_PREFIX_NONE, 16, 1, &priv->sdio_info, 32, false);
		sdio_release_host(tr_sdio->func);
		return ret;
	}

    /*
    if (slave_avl_buf < tr_sdio->curr_tx_size)
    {
        sdio_release_host(tr_sdio->func);
        return -1;
    }
    */
    spin_lock(&tr_sdio->lock);
    tr_sdio->slave_avl_buf = *(unsigned int*)buf;
    spin_unlock(&tr_sdio->lock);
    kfree(buf);
	sdio_release_host(tr_sdio->func);
		
	return 0;
}

static void sdio_poll_status(struct work_struct *work)
{
	struct eswin_sdio *tr_sdio = container_of(to_delayed_work(work), struct eswin_sdio, work);

	if( sdio_update_status(tr_sdio)) {
		schedule_delayed_work(&tr_sdio->work, msecs_to_jiffies(1000));
	} else {
		wake_up_interruptible(&tr_sdio->wait);
	}
}

int continue_transfer = 0;

static struct sk_buff *sdio_rx_skb(struct eswin_sdio *tr_sdio)
{
	struct sk_buff *skb = NULL;
	int ret;
	int recv_len;
    unsigned int slave_avl_buf;
    //unsigned int slave_avl_buf_last;
	//unsigned int real_length;

	if(tr_sdio->next_rx_size)
	{
    	//clear next rx buffer size
    	tr_sdio->next_rx_size = 0;
	}
    else
	{
    	/* Wait until at least one rx slot is non-empty */
    	ret = wait_event_interruptible(tr_sdio->wait, (tr_sdio->recv_len > 1 || kthread_should_stop()));
    	if (ret < 0)
		    goto fail;
	}



	if (kthread_should_stop())
		goto fail;

	recv_len = tr_sdio->recv_len;
	skb = dev_alloc_skb(recv_len);

	sdio_claim_host(tr_sdio->func);

    /* replace the sdio_memcpy_fromio with sdio_readsb. in which the op code is 0, the addr will not change during transmittion */
 	ret = sdio_readsb(tr_sdio->func, skb->data, SDIO_ADDR_DATA, recv_len);
	if (ret){
		print_hex_dump(KERN_DEBUG, DBG_PREFIX_SDIO_RX, DUMP_PREFIX_NONE, 16, 1, skb->data, recv_len, false);
		ECRNX_PRINT("[eswin-err] rx-len: %d, ret %d\n", recv_len, ret);
		stop_tx = 1;
		sdio_release_host(tr_sdio->func);
		goto fail;
	}

    //get next rx size
	tr_sdio->next_rx_size=*(unsigned int *)&skb->data[NEXT_BUF_SZ_OFFSET];
    if(tr_sdio->next_rx_size > 1)
    {
        tr_sdio->recv_len = tr_sdio->next_rx_size;
    }
    else
    {
        tr_sdio->recv_len = 1;
    }
	//get slave avl buf cnt
    slave_avl_buf = *(unsigned int *)&skb->data[SLAVE_BUF_SZ_OFFSET] >> 16;
    spin_lock(&tr_sdio->lock);
    tr_sdio->slave_avl_buf = slave_avl_buf;
    spin_unlock(&tr_sdio->lock);

    skb_put(skb, recv_len);
    skb_queue_tail(&tr_sdio->skb_rx_list,skb);

	sdio_release_host(tr_sdio->func);

    if (slave_avl_buf > (SDIO_PKG_MAX_CNT-1))
    {
        if (atomic_read(&tr_sdio->slave_buf_suspend))
        {
            wake_up_interruptible(&tr_sdio->wait);
        }
    }

	//ECRNX_PRINT("rx-len: %d, skb: 0x%x \n", skb->len, skb);
	return skb;

fail:
	if (skb)
		dev_kfree_skb(skb);
	return NULL;
}


#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#include <uapi/linux/sched/types.h>
#endif
//extern int sched_setscheduler(struct task_struct *, int, const struct sched_param *);

static int sdio_rx_unpack_thread(void *data)
{
	struct eswin_sdio *tr_sdio = (struct eswin_sdio *)data;
	struct eswin *tr = tr_sdio->tr;
	struct sk_buff *skb;
	struct sk_buff *skb_frag;
	struct sdio_rx_head_t * rx_head;
	int ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
		struct sched_param param = { .sched_priority = 1 };
		param.sched_priority = 56;
		sched_setscheduler(get_current(), SCHED_FIFO, &param);
#else
		sched_set_fifo(get_current());
#endif
		ECRNX_PRINT("rx unpack thread entry\n");

	while (!kthread_should_stop())
	{
		ret = wait_event_interruptible(tr_sdio->wait_unpack, skb_peek(&tr_sdio->skb_rx_list) 
			|| kthread_should_stop() );
		if(kthread_should_stop())
			continue;
		if (ret < 0)
		{
			ECRNX_ERR("rx unpack thread error!\n");
			return 0;
		}
		while(NULL != (skb = skb_dequeue(&tr_sdio->skb_rx_list)))
		{
			while (skb->len > 8)//valid data must contain 8 byte head
			{
				rx_head = (struct sdio_rx_head_t *)skb->data;
				if(rx_head->data_len == 0)
					break;
				if (*(unsigned int *)&skb->data[8] == SDIO_AVL_NOTIFY_FLAG && rx_head->data_len == 12)
				{
					skb_pull(skb, ALIGN(rx_head->data_len, 4));
					continue;
				}
				skb_frag = dev_alloc_skb(rx_head->data_len);
				memcpy(skb_frag->data,skb->data,rx_head->data_len);
				skb_put(skb_frag,rx_head->data_len);

				if (skb_frag->len > skb->len)
				{
					ECRNX_ERR("skb len error!!!  frag len:%d skb->len:%d\n",skb_frag->len,skb->len);
					print_hex_dump(KERN_DEBUG, DBG_PREFIX_SDIO_RX, DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len, false);
					BUG_ON(1);
				}
				tr->rx_callback(tr->umac_priv, skb_frag);
				skb_pull(skb,ALIGN(rx_head->data_len, 4));
			}
			dev_kfree_skb(skb);
		}
	}
	ECRNX_PRINT("rx unpack thread exit\n");
	return 0;
}

static int sdio_rx_thread(void *data)
{
	struct eswin_sdio *tr_sdio = (struct eswin_sdio *)data;
	struct eswin *tr = tr_sdio->tr;
	struct sk_buff *skb;
	struct sk_buff *skb_frag;
	struct sdio_rx_head_t * rx_head;
	int is_suspend;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
	struct sched_param param = { .sched_priority = 1 };
	param.sched_priority = 56;
	sched_setscheduler(get_current(), SCHED_FIFO, &param);
#else
	sched_set_fifo(get_current());
#endif
	ECRNX_PRINT("rx thread entry, loopbakc: %d\n", tr->loopback);

	while (!kthread_should_stop())
	{
		skb = sdio_rx_skb(tr_sdio);
		is_suspend = atomic_read(&suspend);
		ECRNX_DBG("rx_cb: 0x%x, skb: 0x%x, is_suspend: 0x%x \n", tr->rx_callback, skb, is_suspend);
		if ((!tr->rx_callback) || (skb && is_suspend)){
			dev_kfree_skb(skb);
		}
		else if (skb){
			wake_up_interruptible(&tr_sdio->wait_unpack);
#if 0
			while (skb->len > 8)//valid data must contain 8 byte head
				{
				//printk("skb len 0:%d",skb->len);
				rx_head = (struct sdio_rx_head_t *)skb->data;
				if(rx_head->data_len == 0)
					break;
				skb_frag = dev_alloc_skb(rx_head->data_len);
				memcpy(skb_frag->data,skb->data,rx_head->data_len);
				skb_put(skb_frag,rx_head->data_len);
				//printk("skb_frag len 1:%d",skb_frag->len);

				if (skb_frag->len > skb->len)
					{
					printk("skb len error!!!  frag len:%d skb->len:%d\n",skb_frag->len,skb->len);
					print_hex_dump(KERN_DEBUG, "eswin-rx: ", DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len, false);
					BUG_ON(1);
					}

				tr->rx_callback(tr->umac_priv, skb_frag);
				skb_pull(skb,ALIGN(rx_head->data_len, 4));
				//printk("skb len 1:%d",skb->len);
				//break;

				}
			dev_kfree_skb(skb);
			//dev_alloc_skb
			//tr->rx_callback(tr->umac_priv, skb);
			//debug_sdio_rx_callback(skb);
#endif
		}
		else {
			ECRNX_ERR("rx-head: %d, rx-tail: %d, rx-cnt: %d\n",
					tr_sdio->slot[RX_SLOT].head, tr_sdio->slot[RX_SLOT].tail, tr_sdio->sdio_info.info_rd);
			break;
		}
	}
	ECRNX_PRINT("rx thread exit\n");

	return 0;
}

#if 0
static int sdio_noop(struct eswin *tr, struct sk_buff *skb)
{
	ECRNX_PRINT("%s exit!!", __func__);
	return 0;
}
#endif

void dump_sdio_buf(struct tx_buff_node *node)
{
	int i = 0;

	ECRNX_PRINT("sdio tx len %d\n", node?node->len:-1);
	/*for (i = 0; i < node->len; ++i) {
		if (i % 16 == 0 && i) {
			printk("\n");
		}
		printk("%02x ", ((unsigned char *)(node->buff))[i]);
	}*/

}

static int sdio_xmit(struct eswin *tr, struct tx_buff_pkg_node *node)
{
	struct eswin_sdio *tr_sdio   = (struct eswin_sdio *)tr->drv_priv;
	struct eswin * tr1 = tr_sdio->tr;
	int ret,flag;
	unsigned int slave_avl_buf = 0;

	//dump_sdio_buf(node);
    spin_lock(&tr_sdio->lock);
    slave_avl_buf = tr_sdio->slave_avl_buf;
    spin_unlock(&tr_sdio->lock);
    if(slave_avl_buf < node->node_cnt && (node->flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_TX_DESC)
    {
        atomic_set(&tr_sdio->slave_buf_suspend, 1);
        ret = wait_event_interruptible(tr_sdio->wait, node->node_cnt < tr_sdio->slave_avl_buf);
        if (ret < 0) 
        {
            ECRNX_PRINT("[transa] sdio_xmit, wait_event_interruptible fail, ret = %d slave_avl_buf=%d\n", ret, tr_sdio->slave_avl_buf);
            return ret;
        }
        cancel_delayed_work_sync(&tr_sdio->work);	
        atomic_set(&tr_sdio->slave_buf_suspend, 0);
    }
	sdio_claim_host(tr_sdio->func);
    /* replace the sdio_memcpy_xxio with sdio_xxxxsb. in sdio_xxxxsb the op code is 0, the addr will not change during transmittion */
	ret = sdio_writesb(tr_sdio->func, node->flag, node->buff, node->len);
	if(ret) {
		//stop_tx = 1;
		ECRNX_ERR("sdio_xmit error, len = %d, ret:%d slave_avl_buf=%d ,node->node_cnt :%d\n",\
			node->len, ret, tr_sdio->slave_avl_buf,node->node_cnt);
		print_hex_dump(KERN_DEBUG, DBG_PREFIX_SDIO_TX, DUMP_PREFIX_NONE, 32, 1,
			       node->buff, 64, false);
	}

    spin_lock(&tr_sdio->lock);
    if ((node->flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_TX_DESC)
    {
        tr_sdio->slave_avl_buf -= node->node_cnt;
    }
    spin_unlock(&tr_sdio->lock);

    sdio_release_host(tr_sdio->func);
    ECRNX_DBG(" sdio_xmit ok, len = %d, flag: 0x%02x!\n", node->len, node->flag);
    return ret;
}

static int sdio_start(struct eswin *tr)
{
	int ret=0;

	struct eswin_sdio *tr_sdio   = (struct eswin_sdio *)tr->drv_priv;

	ECRNX_PRINT("%s\n", __func__);

	/* Start rx thread */
	//if(tr->loopback == 1)
	//	return 0;
	tr_sdio->curr_tx_size = 0;
	ret = sdio_update_status(tr_sdio);
	INIT_DELAYED_WORK(&tr_sdio->work, sdio_poll_status);

	tr_sdio->kthread = kthread_run(sdio_rx_thread, tr_sdio, "sdio-rx");
	tr_sdio->kthread_unpack = kthread_run(sdio_rx_unpack_thread, tr_sdio, "sdio-rx-unpack");

	atomic_set(&suspend, 0);
	atomic_set(&tr_sdio->slave_buf_suspend, 0);
    spin_lock_init(&tr_sdio->lock);

	return ret;
}


static int sdio_suspend(struct eswin *tr)
{
	atomic_set(&suspend, 1);
	return 0;
}

static int sdio_resume(struct eswin *tr)
{
	atomic_set(&suspend, 0);
	return 0;
}


static int sdio_raw_write(struct eswin *tr,	const void *data, const u32 len)
{
	int ret;
	struct eswin_sdio *tr_sdio   = (struct eswin_sdio *)tr->drv_priv;

	ECRNX_PRINT(" %s, entry~", __func__);

	sdio_claim_host(tr_sdio->func);
    /* replace the sdio_memcpy_xxio with sdio_xxxxsb. in sdio_xxxxsb the op code is 0, the addr will not change during transmittion */
	ret = sdio_writesb(tr_sdio->func, SDIO_ADDR_DATA, data,  len);
	sdio_release_host(tr_sdio->func);

	return ret;
}

		
static int sdio_wait_ack(struct eswin *tr)
{
	int data = 0;
    u8 * buf = kmalloc(4,GFP_ATOMIC);
	struct eswin_sdio *tr_sdio   = (struct eswin_sdio *)tr->drv_priv;

	ECRNX_PRINT(" %s, entry~", __func__);

	sdio_claim_host(tr_sdio->func);
    /* replace the sdio_memcpy_xxio with sdio_xxxxsb. in sdio_xxxxsb the op code is 0, the addr will not change during transmittion */
	sdio_readsb(tr_sdio->func, buf, SDIO_ADDR_DATA, 1);
	sdio_release_host(tr_sdio->func);
    data = *(int *)buf;
    kfree(buf);

	return data;
}



static struct sdio_ops eswin_sdio_ops = {
	.start      = sdio_start,
	.xmit       = sdio_xmit,
	.suspend    = sdio_suspend,
	.resume     = sdio_resume,
	.write      = sdio_raw_write,
	.wait_ack	= sdio_wait_ack,
};



static void eswin_sdio_irq_handler(struct sdio_func *func)
{
	struct eswin_sdio *tr_sdio = sdio_get_drvdata(func);
	u32 rear;	
	unsigned char lowbyte, highbyte;
	int ret, ac;

	//printk(" %s, entry~\n", __func__);

	sdio_claim_host(tr_sdio->func);
	lowbyte  = sdio_readb(tr_sdio->func, 0x00, &ret);
	highbyte = sdio_readb(tr_sdio->func, 0x01, &ret);
	tr_sdio->recv_len  = (highbyte << 8) | lowbyte;
	//printk("%s %u, %hhu, %hhu!", __func__, tr_sdio->recv_len, highbyte, lowbyte);

#if 0
	//_info(" eswin_sdio_irq_handler, len: %d\n", tr_sdio->recv_len);
	if(tr_sdio->recv_len == 1) {
	
		ret = sdio_memcpy_fromio(tr_sdio->func, &tr_sdio->sdio_info, SDIO_ADDR_INFO, 0x10 /*priv->recv_len*/);
		if (ret < 0) {
			ECRNX_PRINT(" eswin_sdio_irq_handler, info-ret: %d\n", ret);
			//print_hex_dump(KERN_DEBUG, "status - 2 ", DUMP_PREFIX_NONE, 16, 1, &priv->sdio_info, 32, false);
			sdio_release_host(tr_sdio->func);
			return;
		}
		//ECRNX_PRINT(" get info\n");
		//ECRNX_PRINT(" eswin_sdio_irq_handler, info-wr: %#x, info-rd: %#x\n", priv->sdio_info.info_wr, priv->sdio_info.info_rd);

		tr_sdio->slot[TX_SLOT].head = tr_sdio->sdio_info.info_wr;
		tr_sdio->slot[RX_SLOT].head = tr_sdio->sdio_info.info_rd;


		//if (hdev->nw->loopback)
		//	return;

		if((tr_sdio->sdio_info.credit_vif0 != tr_sdio->credit_vif0) 
			||(tr_sdio->sdio_info.credit_vif1 != tr_sdio->credit_vif1)){
			/* Update VIF0 credit */
			rear = tr_sdio->sdio_info.credit_vif0;
			tr_sdio->credit_vif0 = rear;
			for (ac = 0; ac < 4; ac++)
				tr_sdio->rear[ac] = (rear >> 8*ac) & 0xff;


			/* Update VIF1 credit */
			rear = tr_sdio->sdio_info.credit_vif1;
			tr_sdio->credit_vif1 = rear;
			for (ac = 0; ac < 4; ac++)
				tr_sdio->rear[6+ac] = (rear >> 8*ac) & 0xff;

			//need_credit = 1;
			//sdio_release_host(tr_sdio->func);
			//sdio_credit_skb(tr_sdio);
			//sdio_claim_host(func);
		}
#if 0
	printk("irq: wr: %d, rd: %d, credit:%d/%d, %d/%d, %d/%d, %d/%d\n", 
			priv->sdio_info.info_wr, priv->sdio_info.info_rd,
			priv->rear[3], priv->front[3], 
			priv->rear[2], priv->front[2], 
			priv->rear[1], priv->front[1], 
			priv->rear[0], priv->front[0]);
#endif

#ifdef CONFIG_NRC_HIF_PRINT_FLOW_CONTROL
		//nrc_dbg(NRC_DBG_HIF, "-%s\n", __func__);
#endif

	}
	else
	{
		//ECRNX_PRINT(" get data len: %d\n", priv->recv_len);
	}
#endif
	sdio_release_host(tr_sdio->func);
	wake_up_interruptible(&tr_sdio->wait);
}

static void eswin_sdio_irq2_handler(struct sdio_func *func)
{
	struct eswin_sdio *tr_sdio = g_sdio;
	unsigned char lowbyte, highbyte;
	int ret, recv_len;
	struct sk_buff * skb;

	ECRNX_PRINT(" %s, entry~\n", __func__);
	
	sdio_claim_host(func);
	lowbyte  = sdio_readb(func, 0x00, &ret);
	highbyte = sdio_readb(func, 0x01, &ret);
	sdio_release_host(func);

	recv_len  = (highbyte << 8) | lowbyte;
	skb = dev_alloc_skb(recv_len);
	skb_put(skb, recv_len);

	sdio_claim_host(func);
    /* replace the sdio_memcpy_fromio with sdio_readsb. in which the op code is 0, the addr will not change during transmittion */
 	ret = sdio_readsb(func, skb->data, SDIO_ADDR_DATA, recv_len);
	if (ret){
		ECRNX_ERR("[eswin-err] rx-len: %d\n", skb->len);
	}

	sdio_release_host(func);

	print_hex_dump(KERN_DEBUG, DBG_PREFIX_SDIO_RX, DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len, false);
	dev_kfree_skb(skb);
}

struct device *eswin_sdio_get_dev(void *plat)
{
    struct eswin* tr = (struct eswin*)plat;
    
    return tr->dev;
}


static int eswin_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	int ret;
	struct eswin_sdio * tr_sdio;
	struct eswin* tr;

	ECRNX_PRINT("%s entry, func: %d!!", __func__, func->num);

	if (func->num == 1) {
		tr = eswin_core_create(sizeof(* tr_sdio), &func->dev, &eswin_sdio_ops);
		if(!tr) {
			dev_err(&func->dev, "failed to allocate core\n");
			return -ENOMEM;
		}

		tr_sdio = (struct eswin_sdio *)tr->drv_priv;
		g_sdio = tr_sdio;
		tr_sdio->tr = tr;
		tr_sdio->func = func;
	} else {
		g_sdio->func2 = func;
		
		sdio_claim_host(func);
		sdio_enable_func(func);
		func->max_blksize = ESWIN_SDIO_BLK_SIZE;
		sdio_set_block_size(func, func->max_blksize);
		sdio_claim_irq(func, eswin_sdio_irq2_handler);
		sdio_release_host(func);
		return 0;
	}

	tr_sdio->slot[TX_SLOT].size = 456;
	tr_sdio->slot[RX_SLOT].size = 492;

	tr_sdio->credit_max[0] = CREDIT_AC0;
	tr_sdio->credit_max[1] = CREDIT_AC1;
	tr_sdio->credit_max[2] = CREDIT_AC2;
	tr_sdio->credit_max[3] = CREDIT_AC3;

	tr_sdio->credit_max[6] = CREDIT_AC0;
	tr_sdio->credit_max[7] = CREDIT_AC1;
	tr_sdio->credit_max[8] = CREDIT_AC2;
	tr_sdio->credit_max[9] = CREDIT_AC3;

	init_waitqueue_head(&tr_sdio->wait);
	init_waitqueue_head(&tr_sdio->wait_unpack);

	func->max_blksize = ESWIN_SDIO_BLK_SIZE;

	skb_queue_head_init(&tr_sdio->skb_rx_list);
	//skb_queue_head_init(tr_sdio->skb_rx_unpack_list);

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if(ret) {
		ECRNX_PRINT("failed to enable sdio func\n");
		goto release;
	}
	
	ret = sdio_set_block_size(func, func->max_blksize);
	if(ret) {
		ECRNX_PRINT("failed to set sdio func block size\n");
		goto release;
	}

	ret = sdio_claim_irq(func, eswin_sdio_irq_handler);
	if(ret) {
		ECRNX_PRINT("failed to claim sdio irq\n");
		goto release;
	}

	sdio_release_host(func);
	sdio_set_drvdata(func, tr_sdio);

#ifdef CONFIG_SHOW_TX_SPEED
	sdio_tx_last_jiffies = jiffies;
	sdio_tx_len_totol = 0;
	sdio_tx_error_cnt = 0;
#endif

#ifdef CONFIG_SHOW_RX_SPEED
	sdio_rx_last_jiffies = jiffies;
	sdio_rx_len_totol = 0;
	sdio_rx_error_cnt = 0;
#endif

	
	ret = eswin_core_register(tr);
	if(ret) {
		ECRNX_PRINT("failed to register core\n");

	}

	debugfs_sdio_init();

	ECRNX_PRINT("%s exit!!", __func__);
	return 0;

release:
	sdio_release_host(func);
	return ret;
}

static void eswin_sdio_remove(struct sdio_func *func)
{
    struct eswin_sdio *tr_sdio = sdio_get_drvdata(func);
    struct eswin *tr = tr_sdio->tr;


    ECRNX_PRINT(" %s entry!!\n", __func__);
    debugfs_remove_recursive(p_debugfs_sdio);

    eswin_core_unregister(tr);

    sdio_claim_host(func);
    sdio_release_irq(func);
    sdio_disable_func(func);
    sdio_release_host(func);

	kthread_stop(tr_sdio->kthread);
	wake_up_interruptible(&tr_sdio->wait);
	kthread_stop(tr_sdio->kthread_unpack);
	wake_up_interruptible(&tr_sdio->wait_unpack);

    eswin_core_destroy(tr); 
    ECRNX_PRINT(" %s exit!!\n", __func__);
}


static const struct sdio_device_id eswin_sdio_dev[] =
{
	{ SDIO_DEVICE(ESWIN_SDIO_VENDER, ESWIN_SDIO_DEVICE) },
	{},
};

static struct sdio_driver eswin_sdio_driver =
{
	.name     = "eswin_sdio",
	.id_table = eswin_sdio_dev,
	.probe    = eswin_sdio_probe,
	.remove   = eswin_sdio_remove,
};

static int __init eswin_sdio_init(void)
{
	int ret;
	ECRNX_PRINT(" %s entry!!", __func__);

	ret = sdio_register_driver(&eswin_sdio_driver);
	if (ret)
		ECRNX_PRINT("sdio driver registration failed: %d\n", ret);

	ECRNX_PRINT(" %s exit!!", __func__);
	return ret;
}

static void __exit eswin_sdio_exit(void)
{
	ECRNX_PRINT(" %s entry!!", __func__);
	sdio_unregister_driver(&eswin_sdio_driver);
	ECRNX_PRINT(" %s exit!!", __func__);
}

int ecrnx_sdio_register_drv(void)
{
    return eswin_sdio_init();
}

void ecrnx_sdio_unregister_drv(void)
{
    return eswin_sdio_exit();
}

//module_init(eswin_sdio_init);
//module_exit(eswin_sdio_exit);

//MODULE_AUTHOR("Transa-Semi");
//MODULE_LICENSE("Dual BSD/GPL");
//MODULE_DESCRIPTION("Driver support for Transa-Semi 802.11 WLAN SDIO driver");
//MODULE_SUPPORTED_DEVICE("Transa-Semi 802.11 devices");

