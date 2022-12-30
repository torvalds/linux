/**
 ******************************************************************************
 *
 * @file core.c
 *
 * @brief sdio core function definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
#include <linux/firmware.h>
#include <linux/kthread.h>
#include "core.h"
#include "fw.h"
#include <uapi/linux/sched/types.h>
//#include "debug.h"
#include "ecrnx_platform.h"
#include "sdio.h"
#include "ecrnx_rx.h"
#include "sdio_host_interface.h"
#include "eswin_utils.h"

bool loopback;
module_param(loopback, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(loopback, "HIF loopback");

int power_save;
module_param(power_save, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(power_save, "Power Save(0: disable, 1:enable)");

int disable_cqm = 0;
module_param(disable_cqm, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(disable_cqm, "Disable CQM (0: disable, 1:enable)");


int listen_interval = 0;
module_param(listen_interval, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(listen_interval, "Listen Interval");

int bss_max_idle = 0;
module_param(bss_max_idle, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(bss_max_idle, "BSS Max Idle");


bool dl_fw;
module_param(dl_fw, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(dl_fw, "download firmware");


#ifdef CONFIG_ECRNX_WIFO_CAIL
bool amt_mode;
module_param(amt_mode, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(amt_mode, "calibrate mode");
#endif

bool set_gain;
module_param(set_gain, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(set_gain, "set gain delta");

char *fw_name;
struct eswin *pEswin;

module_param(fw_name, charp, S_IRUGO);
MODULE_PARM_DESC(fw_name, "Firmware file name");

#if 0
static void eswin_fw_ready(struct sk_buff *skb, struct eswin * tr)
{
	struct ieee80211_hw *hw = tr->hw;
	struct wim_ready *ready;
	struct wim *wim = (struct wim *)skb->data;

	ECRNX_PRINT(" %s entry!!", __func__);
	ready = (struct wim_ready *) (wim + 1);

	ECRNX_PRINT(" %s  -- version: 0x%x", __func__, ready->v.version);
	ECRNX_PRINT(" %s  -- rx_head_size: %d", __func__, ready->v.rx_head_size);
	ECRNX_PRINT(" %s  -- tx_head_size: %d", __func__, ready->v.tx_head_size);
	ECRNX_PRINT(" %s  -- buffer_size: %d", __func__, ready->v.buffer_size);

	tr->fwinfo.ready = 2;
	tr->fwinfo.version = ready->v.version;
	tr->fwinfo.rx_head_size = ready->v.rx_head_size;
	tr->fwinfo.tx_head_size = ready->v.tx_head_size;
	tr->fwinfo.payload_align = ready->v.payload_align;
	tr->fwinfo.buffer_size = ready->v.buffer_size;

	ECRNX_PRINT(" %s  -- cap_mask: 0x%llx", __func__, ready->v.cap.cap);
	ECRNX_PRINT(" %s  -- cap_li: %d, %d", __func__, ready->v.cap.listen_interval, listen_interval);
	ECRNX_PRINT(" %s  -- cap_idle: %d, %d", __func__, ready->v.cap.bss_max_idle, bss_max_idle);

	tr->cap.cap_mask = ready->v.cap.cap;
	tr->cap.listen_interval = ready->v.cap.listen_interval;
	tr->cap.bss_max_idle = ready->v.cap.bss_max_idle;

	if (listen_interval) {
		hw->max_listen_interval = listen_interval;
		tr->cap.listen_interval = listen_interval;
	} 

	if (bss_max_idle) {
		tr->cap.bss_max_idle = bss_max_idle;
	} 

	dev_kfree_skb(skb);
	ECRNX_PRINT(" %s exit!!", __func__);
}
#endif
static unsigned int sdio_tx_packets = 0;
static struct timer_list sdio_tx_timer = {0};

#define SDIO_TX_TIMER_TIMEOUT_US          (200)

void sdio_tx_queue_init(struct tx_buff_queue * queue)
{
	queue->head = NULL;
	queue->tail = NULL;
	queue->count = 0;
	spin_lock_init(&queue->lock);
}

void sdio_tx_queue_push(struct tx_buff_queue * queue, struct tx_buff_node *node)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	if (queue->head) {
		queue->tail->next = node;
	} else {
		queue->head = node;
	}

	queue->tail = node;;
	queue->tail->next = NULL;
	queue->count++;

	spin_unlock_irqrestore(&queue->lock, flags);

	//ECRNX_PRINT(" queue push count: %d\n", queue->count);
	//ECRNX_PRINT(" queue push head: %#x\n", queue->head);
}

struct tx_buff_node *sdio_tx_queue_pop(struct tx_buff_queue *queue)
{
	unsigned long flags;
	struct tx_buff_node *res = NULL;

	//ECRNX_PRINT(" queue pop count: %d\n", queue->count);
	//ECRNX_PRINT(" queue pop head: %#x\n", queue->head);

	spin_lock_irqsave(&queue->lock, flags);

	if (queue->count) {
		res = queue->head;
		queue->head = res->next;
		res->next = NULL;
		queue->count--;
	}

	spin_unlock_irqrestore(&queue->lock, flags);
	return res;
}

struct tx_buff_node *sdio_tx_queue_peek(struct tx_buff_queue *queue)
{
	unsigned long flags;
	struct tx_buff_node *res = NULL;

	spin_lock_irqsave(&queue->lock, flags);

	if (queue->count) {
		res = queue->head;
	}

	spin_unlock_irqrestore(&queue->lock, flags);
	return res;
}

struct tx_buff_node * sdio_tx_node_alloc(struct eswin *tr)
{
	struct tx_buff_node * res;
	unsigned long flags;

	spin_lock_irqsave(&tr->tx_lock,flags);
	res = tr->tx_node_head;
	if(res == NULL)
	{
		spin_unlock_irqrestore(&tr->tx_lock, flags);
		return NULL;
	}
	tr->tx_node_head = tr->tx_node_head->next;
	res->next = NULL;
	tr->tx_node_num--;
	spin_unlock_irqrestore(&tr->tx_lock, flags);

	return res;
}

void sdio_tx_node_free(struct eswin *tr, struct tx_buff_node * node)
{
	unsigned long flags;
	spin_lock_irqsave(&tr->tx_lock,flags);
	kfree(node->buff);
    node->buff = NULL;
	node->next = tr->tx_node_head;
	tr->tx_node_head = node;
	tr->tx_node_num++;
	spin_unlock_irqrestore(&tr->tx_lock, flags);
}

void sdio_tx_pkg_queue_init(struct tx_buff_pkg_queue * queue)
{
	queue->head = NULL;
	queue->tail = NULL;
	queue->count = 0;
	spin_lock_init(&queue->lock);
}

void sdio_tx_pkg_queue_push(struct tx_buff_pkg_queue * queue, struct tx_buff_pkg_node *node)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	if (queue->head) {
		queue->tail->next = node;
	} else {
		queue->head = node;
	}

	queue->tail = node;;
	queue->tail->next = NULL;
	queue->count++;

	spin_unlock_irqrestore(&queue->lock, flags);
}

struct tx_buff_pkg_node *sdio_tx_pkg_queue_pop(struct tx_buff_pkg_queue *queue)
{
	unsigned long flags;
	struct tx_buff_pkg_node *res = NULL;

	spin_lock_irqsave(&queue->lock, flags);

	if (queue->count) {
		res = queue->head;
		queue->head = res->next;
		res->next = NULL;
		queue->count--;
	}

	spin_unlock_irqrestore(&queue->lock, flags);
	return res;
}

struct tx_buff_pkg_node * sdio_tx_pkg_node_alloc(struct eswin *tr)
{
	struct tx_buff_pkg_node * res;
	unsigned long flags;

	spin_lock_irqsave(&tr->tx_pkg_lock,flags);
	res = tr->tx_pkg_node_head;
	if(res == NULL)
	{
		spin_unlock_irqrestore(&tr->tx_pkg_lock, flags);
		return NULL;
	}
	tr->tx_pkg_node_head = tr->tx_pkg_node_head->next;
	res->next = NULL;
	tr->tx_pkg_node_num--;
	spin_unlock_irqrestore(&tr->tx_pkg_lock, flags);

	return res;
}

void sdio_tx_pkg_node_free(struct eswin *tr, struct tx_buff_pkg_node * node)
{
	unsigned long flags;
	int i;
	spin_lock_irqsave(&tr->tx_pkg_lock,flags);
	if((node->flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_TX_DESC)
	{
		kfree(node->buff);
	}
	node->buff = NULL;
	node->next = tr->tx_pkg_node_head;
	tr->tx_pkg_node_head = node;
	tr->tx_pkg_node_num++;
	/*
	if (node->node_cnt > 1)
	{
		printk("%s,count :%d\n",__func__,node->node_cnt);
	}
	*/

	for (i = 0; i < node->node_cnt; ++i)
	{
		sdio_tx_node_free(tr, node->tx_node[i]);
	}
	spin_unlock_irqrestore(&tr->tx_pkg_lock, flags);
}


void eswin_sdio_register_rx_cb(struct eswin *tr, sdio_rx_cb_t cb)
{
    tr->rx_callback = cb;
}

extern int ecrnx_data_cfm_callback(void *priv, void *host_id);
extern int ecrnx_msg_cfm_callback(void *priv, void *host_id);
static void eswin_core_register_work(struct work_struct *work)
{
	//struct sk_buff *skb_resp;
	int ret;
	struct eswin *tr = container_of(work, struct eswin, register_work.work);

	ECRNX_PRINT(" %s entry, dl_fw = %d!!", __func__, dl_fw);

	if (dl_fw  && eswin_fw_file_chech(tr)) {
		eswin_fw_file_download(tr);
        release_firmware(tr->fw);
		dl_fw = false;
		schedule_delayed_work(&tr->register_work, msecs_to_jiffies(1000));
		return;
	}

#ifdef CONFIG_ECRNX_WIFO_CAIL
	ECRNX_PRINT(" %s entry, amt_mode = %d!!", __func__, amt_mode);
#endif

	tr->rx_callback = ecrnx_rx_callback;
	tr->data_cfm_callback = ecrnx_data_cfm_callback;
	tr->msg_cfm_callback = ecrnx_msg_cfm_callback;

	ret = ecrnx_platform_init(tr, &tr->umac_priv);
	set_bit(ESWIN_FLAG_CORE_REGISTERED, &tr->dev_flags);
    ECRNX_DBG("%s exit!!", __func__);

    return;
}

int eswin_core_register(struct eswin *tr)
{
	ECRNX_PRINT("%s entry!!", __func__);
	tr->ops->start(tr);

	//schedule_delayed_work(&tr->register_work, msecs_to_jiffies(10));
	schedule_delayed_work(&tr->register_work, msecs_to_jiffies(1));
	ECRNX_PRINT("%s exit!!", __func__);
	return 0;
}

void eswin_core_unregister(struct eswin *tr)
{
	struct eswin_sdio *tr_sdio   = (struct eswin_sdio *)tr->drv_priv;
	ECRNX_PRINT("%s entry!!", __func__);

	cancel_delayed_work(&tr->register_work);

	if (!test_bit(ESWIN_FLAG_CORE_REGISTERED, &tr->dev_flags))
		return;

    ecrnx_platform_deinit(tr->umac_priv);
}

static int eswin_sdio_tx_thread(void *data)
{
	struct eswin *tr = (struct eswin *)data;
	struct tx_buff_pkg_node *node;
	int i, ret = 0, cb_per = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
		struct sched_param param = { .sched_priority = 1 };
		param.sched_priority = 56;
		sched_setscheduler(get_current(), SCHED_FIFO, &param);
#else
		sched_set_fifo(get_current());
#endif
		ECRNX_PRINT("sdio pkg thread entry\n");

	while (!kthread_should_stop())
	{
		ret = wait_event_interruptible(tr->wait_tx, tr->tx_pkg_queue.count != 0 || kthread_should_stop());
		if (ret < 0)
		{
			ECRNX_ERR("sdio pkg thread error!\n");
			return 0;
		}
		if(kthread_should_stop())
		{
			continue;
		}
		while (tr->tx_pkg_queue.count != 0)
		{
			node = sdio_tx_pkg_queue_pop(&tr->tx_pkg_queue);
			if (!node) {
				cb_per = 0;
				wake_up_interruptible(&tr->wait_cb);
			    break;
			}
			
			if (tr->ops->xmit) {
				ret = tr->ops->xmit(tr, node);
				WARN_ON(ret < 0);
				if((node->flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_TX_DESC || (node->flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_MSG_E)
				{
					sdio_tx_pkg_queue_push(&tr->tx_c_queue,node);
				}
				else
				{
					sdio_tx_pkg_node_free(tr, node);
				}
				cb_per++;
				//if (cb_per % 4 == 0)
				{
					cb_per = 0;
					wake_up_interruptible(&tr->wait_cb);
				}
			} else {
				ECRNX_ERR(" eswin_sdio_work, ops->xmit is null\n");
			}
		}
	}
	ECRNX_PRINT("sdio tx thread exit\n");
	return 0;
}

static int eswin_sdio_callback_thread(void *data)
{
	struct eswin *tr = (struct eswin *)data;
	struct tx_buff_pkg_node *node;
	int i, ret = 0;
	struct txdesc_api *tx_desc;
	ptr_addr host_id;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
		struct sched_param param = { .sched_priority = 1 };
		param.sched_priority = 56;
		sched_setscheduler(get_current(), SCHED_FIFO, &param);
#else
		sched_set_fifo(get_current());
#endif
		ECRNX_PRINT("sdio callback thread entry\n");

	while (!kthread_should_stop())
	{
		ret = wait_event_interruptible(tr->wait_cb, tr->tx_c_queue.count != 0 || kthread_should_stop());
		if (ret < 0)
		{
			ECRNX_ERR("sdio callback thread error!\n");
			return 0;
		}
		if(kthread_should_stop())
		{
			continue;
		}
		while (tr->tx_c_queue.count != 0)
		{
			node = sdio_tx_pkg_queue_pop(&tr->tx_c_queue);
			if((node->flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_TX_DESC && (tr->data_cfm_callback))
			{
				for (i = 0; i < node->node_cnt; ++i)
				{
					tx_desc = (struct txdesc_api *)node->tx_node[i]->buff;
					if (tx_desc->host.flags & TXU_CNTRL_MGMT)
					{
						continue;
					}
					memcpy(&host_id, tx_desc->host.packet_addr, sizeof(ptr_addr));
					tr->data_cfm_callback(tr->umac_priv, (void*)host_id);
				}
			}
			//else if((node->flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_MSG_E && (tr->msg_cfm_callback))
			//{
			//	for (i = 0; i < node->node_cnt; ++i)
			//	{
			//		struct ecrnx_cmd_a2emsg *msg = (struct ecrnx_cmd_a2emsg *)node->tx_node[i]->buff;
			//		tr->msg_cfm_callback(tr->umac_priv, msg->hostid);
			//	}
			//}
			sdio_tx_pkg_node_free(tr, node);
		}
	}
	ECRNX_PRINT("rx callback thread exit\n");
	return 0;
}

static int eswin_sdio_tx_pkg_thread(void *data)
{
	struct eswin *tr = (struct eswin *)data;
	struct tx_buff_node *node;
	struct txdesc_api *tx_desc;
	struct tx_buff_pkg_node * pkg_node = NULL;
	struct tx_buff_pkg_head tx_pkg_head;
	unsigned int offset = 0;
	int i, ret, pkg_cnt = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
		struct sched_param param = { .sched_priority = 1 };
		param.sched_priority = 56;
		sched_setscheduler(get_current(), SCHED_FIFO, &param);
#else
		sched_set_fifo(get_current());
#endif
		ECRNX_PRINT("sdio tx pkg thread entry\n");

	while (!kthread_should_stop())
	{
		ret = wait_event_interruptible(tr->wait_pkg, tr->tx_queue.count != 0 || kthread_should_stop());
		if (ret < 0)
		{
			ECRNX_ERR("sdio tx pkg thread error!\n");
			return 0;
		}
		if(kthread_should_stop())
		{
			continue;
		}
		while (tr->tx_queue.count != 0)
		{
			pkg_cnt = 0;
			offset = 0;
			memset(&tx_pkg_head,0,sizeof(tx_pkg_head));
			pkg_node = sdio_tx_pkg_node_alloc(tr);
			memset(pkg_node,0,sizeof(struct tx_buff_pkg_node));
			if (!pkg_node) {
			    ECRNX_PRINT(" sdio pkg failed, no node!!\n");
			    break;
			}
			node = sdio_tx_queue_peek(&tr->tx_queue);
			pkg_node->flag = node->flag;

			if((node->flag & FLAG_MSG_TYPE_MASK) != TX_FLAG_TX_DESC)
			{
				node = sdio_tx_queue_pop(&tr->tx_queue);
				pkg_node->buff = node->buff;
				pkg_node->len = node->len;
				pkg_node->tx_node[pkg_cnt] = node;
				pkg_cnt++;
			}
			else
			{
				pkg_node->buff = (void *)kzalloc(ALIGN(SDIO_PKG_MAX_DATA*SDIO_PKG_MAX_CNT + sizeof(tx_pkg_head), 512), GFP_ATOMIC);
				if(!pkg_node->buff){
			        ECRNX_PRINT("pkg_node buff malloc error! \n");
				}
				pkg_node->len = sizeof(tx_pkg_head);

				while (tr->tx_queue.count)
				{
					offset = pkg_node->len;
					node = sdio_tx_queue_peek(&tr->tx_queue);

					if (((node->flag & FLAG_MSG_TYPE_MASK) != TX_FLAG_TX_DESC) || (pkg_cnt > (SDIO_PKG_MAX_CNT-1)))
					{
						break;
					}
					//ECRNX_DBG("tx count 2 %d,node %x",tr->tx_queue.count,node);
					node = sdio_tx_queue_pop(&tr->tx_queue);
					if(ALIGN(node->len, SDIO_PKG_PAD_GRN) < (SDIO_PKG_DIV_MSZ+1))
					{
						pkg_node->len += ALIGN(node->len, SDIO_PKG_PAD_GRN);
						pkg_node->flag |= (ALIGN(node->len, SDIO_PKG_PAD_GRN)/SDIO_PKG_PAD_GRN) << (8+SDIO_PKG_BIT_SHIFT*pkg_cnt);
					}
					else
					{
						pkg_node->len += SDIO_PKG_MAX_DATA;
						pkg_node->flag |= ((1 << SDIO_PKG_BIT_SHIFT) - 1) << (8+SDIO_PKG_BIT_SHIFT*pkg_cnt);
					}
					memcpy(pkg_node->buff + offset, node->buff, node->len);
					pkg_node->tx_node[pkg_cnt] = node;
					tx_pkg_head.len[pkg_cnt] = node->len;
					pkg_cnt++;
				}
				pkg_node->len = ALIGN(pkg_node->len, 512);
				memcpy(pkg_node->buff, &tx_pkg_head, sizeof(tx_pkg_head));
			}
			pkg_node->node_cnt = pkg_cnt;
			sdio_tx_pkg_queue_push(&tr->tx_pkg_queue, pkg_node);
			wake_up_interruptible(&tr->wait_tx);
		}
	}
	ECRNX_PRINT("tx pkg thread exit\n");
	return 0;
}

void eswin_sdio_ops_init(struct eswin * tr, const struct sdio_ops * ops)
{
	int i;

	tr->ops = ops;
	
	sdio_tx_queue_init(&tr->tx_queue);
	sdio_tx_pkg_queue_init(&tr->tx_c_queue);
	sdio_tx_pkg_queue_init(&tr->tx_pkg_queue);

	for (i=1; i<ESWIN_TX_NODE_CNT; i++) {
		tr->tx_node[i-1].next = &tr->tx_node[i];
	}

	tr->tx_node[i-1].next = NULL;
	tr->tx_node_head = &tr->tx_node[0];
	tr->tx_node_num = ESWIN_TX_NODE_CNT;
	spin_lock_init(&tr->tx_lock);

	for (i=1; i<ESWIN_TX_NODE_CNT; i++) {
		tr->tx_pkg_node[i-1].next = &tr->tx_pkg_node[i];
	}

	tr->tx_pkg_node[i-1].next = NULL;
	tr->tx_pkg_node_head = &tr->tx_pkg_node[0];
	tr->tx_pkg_node_num = ESWIN_TX_NODE_CNT;
	spin_lock_init(&tr->tx_pkg_lock);
}

int tx_desc_count = 0;
int sdio_host_send(void *buff, int len, int flag)
{
    struct eswin * tr= pEswin;
    struct tx_buff_node * node = sdio_tx_node_alloc(tr);

    ECRNX_DBG("%s enter, data len :%d ", __func__, len);

    if (!node) {
        ECRNX_PRINT(" sdio send failed, no node!!\n");
        return -1;
    }

    node->buff = (struct lmac_msg *)kzalloc(len, GFP_ATOMIC);
    if(!node->buff){
        ECRNX_PRINT("buff malloc error! \n");
    }

    memcpy(node->buff, buff, len);
    node->len  = len;
    node->flag = flag & 0xFF;

    if ((len > 512) && (len%512) && ((node->flag & FLAG_MSG_TYPE_MASK) != TX_FLAG_TX_DESC)) {
        node->flag |= (len%512)<<8;
    }
	else
	{
	}

    sdio_tx_queue_push(&tr->tx_queue, node);

	if((node->flag & FLAG_MSG_TYPE_MASK) != TX_FLAG_TX_DESC)
	{
		tx_desc_count = 0;
		//queue_work(tr->workqueue_pkg,&tr->work_pkg);
		wake_up_interruptible(&tr->wait_pkg);
	}
	else
	{
		tx_desc_count++;
		if(tx_desc_count%SDIO_PKG_MAX_CNT == 0)
		{
			//queue_work(tr->workqueue_pkg,&tr->work_pkg);
			wake_up_interruptible(&tr->wait_pkg);
		}
		else
		{
			mod_timer(&sdio_tx_timer, jiffies + usecs_to_jiffies(SDIO_TX_TIMER_TIMEOUT_US));
		}
	}
    return 0;
}

void sdio_tx_timer_handle(struct timer_list *time)
{
	struct eswin * tr = pEswin;
	if (tx_desc_count)
	{
		tx_desc_count = 0;
		wake_up_interruptible(&tr->wait_pkg);
	}
}

extern void ecrnx_send_handle_register(void * fn);

struct eswin * eswin_core_create(size_t priv_size, struct device *dev,
				const struct sdio_ops * ops)
{
	struct eswin * tr;

	tr = (struct eswin *)kzalloc(sizeof(struct eswin) + priv_size, GFP_KERNEL);
	if(!tr) {
		return NULL;
	}

	pEswin = tr;

	tr->dev = dev;
	tr->loopback = loopback;
	//tr->loopback = 1;
	eswin_sdio_ops_init(tr, ops);
	ecrnx_send_handle_register(sdio_host_send);

	//init_completion(&tr->wim_responded);
	init_waitqueue_head(&tr->wait_pkg);
	init_waitqueue_head(&tr->wait_tx);
	init_waitqueue_head(&tr->wait_cb);
	
	tr->kthread_pkg = kthread_run(eswin_sdio_tx_pkg_thread, tr, "sdio-tx-pkg");
	tr->kthread_tx = kthread_run(eswin_sdio_tx_thread, tr, "sdio-tx");
	tr->kthread_cb = kthread_run(eswin_sdio_callback_thread, tr, "sdio-tx-callback");

	INIT_DELAYED_WORK(&tr->register_work, eswin_core_register_work);
	timer_setup(&sdio_tx_timer, sdio_tx_timer_handle, 0);

	tr->state = ESWIN_STATE_INIT;

	//eswin_init_debugfs(tr);

	ECRNX_PRINT(" %s exit!!", __func__);
	return tr;

err_free_mac:
	eswin_core_destroy(tr);
	return NULL;
}

void eswin_core_destroy(struct eswin *tr)
{
    unsigned long flags;
    int i;

    ECRNX_PRINT("%s entry!!", __func__);
    tr->state = ESWIN_STATE_CLOSEED;


    //flush_workqueue(tr->workqueue);
    //destroy_workqueue(tr->workqueue);
    //tr->workqueue = NULL;

    ECRNX_PRINT("%s node_num %d\n", __func__, tr->tx_node_num);
    spin_lock_irqsave(&tr->tx_lock,flags);
    for (i=0; i<64; i++) 
    {
        if (tr->tx_node[i].buff)
        {
            kfree(tr->tx_node[i].buff);
        }
    }
    spin_unlock_irqrestore(&tr->tx_lock, flags);

    spin_lock_irqsave(&tr->tx_pkg_lock,flags);
    for (i=0; i<64; i++)
    {
        if (tr->tx_pkg_node[i].buff)
        {
            kfree(tr->tx_pkg_node[i].buff);
        }
    }
    spin_unlock_irqrestore(&tr->tx_pkg_lock, flags);

	kthread_stop(tr->kthread_pkg);
	wake_up_interruptible(&tr->wait_pkg);
	kthread_stop(tr->kthread_cb);
	wake_up_interruptible(&tr->wait_cb);
	kthread_stop(tr->kthread_tx);
	wake_up_interruptible(&tr->wait_tx);

    kfree(tr);
    tr = NULL;
    //TODO:
    //eswin_mac_destroy(tr);
    ECRNX_PRINT("%s exit!!", __func__);
}


//MODULE_AUTHOR("Transa-Semi");
//MODULE_LICENSE("Dual BSD/GPL");
//MODULE_DESCRIPTION("Core module for Transa-Semi 802.11 WLAN SDIO driver");
