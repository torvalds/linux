/**
 * aicwf_txrxif.h
 *
 * bus function declarations
 *
 * Copyright (C) AICSemi 2018-2020
 */

#ifndef _AICWF_TXRXIF_H_
#define _AICWF_TXRXIF_H_

#include <linux/skbuff.h>
#include <linux/sched.h>
//#include "aicsdio.h"
#include "aicwf_usb.h"

#define CMD_BUF_MAX                 1536
#define DATA_BUF_MAX                2048
#define TXPKT_BLOCKSIZE             512
#define MAX_AGGR_TXPKT_LEN          (1536*32)
#define CMD_TX_TIMEOUT              5000
#define TX_ALIGNMENT                4

#define RX_HWHRD_LEN                60 //58->60 word allined
#define CCMP_OR_WEP_INFO            8
#define MAX_RXQLEN                  2000
#define RX_ALIGNMENT                4

#define DEBUG_ERROR_LEVEL           0
#define DEBUG_DEBUG_LEVEL           1
#define DEBUG_INFO_LEVEL            2

#define DBG_LEVEL                   DEBUG_DEBUG_LEVEL

#define txrx_err(fmt, ...)          pr_err("txrx_err:<%s,%d>: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define sdio_err(fmt, ...)          pr_err("sdio_err:<%s,%d>: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define usb_err(fmt, ...)           pr_err("usb_err:<%s,%d>: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#if DBG_LEVEL >= DEBUG_DEBUG_LEVEL
#define sdio_dbg(fmt, ...)          printk("aicbt: " fmt, ##__VA_ARGS__)
#define usb_dbg(fmt, ...)           printk("aicbt: " fmt, ##__VA_ARGS__)
#else
#define sdio_dbg(fmt, ...)
#define usb_dbg(fmt, ...)
#endif
#if DBG_LEVEL >= DEBUG_INFO_LEVEL
#define sdio_info(fmt, ...)         printk("aicbt: " fmt, ##__VA_ARGS__)
#define usb_info(fmt, ...)          printk("aicbt: " fmt, ##__VA_ARGS__)
#else
#define sdio_info(fmt, ...)
#define usb_info(fmt, ...)
#endif

enum aicwf_bus_state {
    BUS_DOWN_ST,
    BUS_UP_ST
};

struct aicwf_bus_ops {
    int (*start) (struct device * dev);
    void (*stop) (struct device * dev);
    int (*txdata) (struct device * dev, struct sk_buff * skb);
    int (*txmsg) (struct device * dev, u8 * msg, uint len);
};

struct frame_queue {
    u16              num_prio;
    u16              hi_prio;
    u16              qmax;      /* max number of queued frames */
    u16              qcnt;
    struct sk_buff_head queuelist[8];
};

struct aicwf_bus {
    union {
        struct aic_sdio_dev *sdio;
        struct aic_usb_dev *usb;
    } bus_priv;
    struct device *dev;
    struct aicwf_bus_ops *ops;
    enum aicwf_bus_state state;
    u8 *cmd_buf;
    struct completion bustx_trgg;
    struct completion busrx_trgg;
    struct task_struct *bustx_thread;
    struct task_struct *busrx_thread;
};

struct aicwf_tx_priv {
#ifdef AICWF_SDIO_SUPPORT
    struct aic_sdio_dev *sdiodev;
    int fw_avail_bufcnt;
    //for cmd tx
    u8 *cmd_buf;
    uint cmd_len;
    bool cmd_txstate;
    bool cmd_tx_succ;
    struct semaphore cmd_txsema;
    wait_queue_head_t cmd_txdone_wait;
    //for data tx
    atomic_t tx_pktcnt;

    struct frame_queue txq;
    spinlock_t txqlock;
    struct semaphore txctl_sema;
#endif
#ifdef AICWF_USB_SUPPORT
    struct aic_usb_dev *usbdev;
#endif
    struct sk_buff *aggr_buf;
    atomic_t aggr_count;
    u8 *head;
    u8 *tail;
};


#define MAX_REORD_RXFRAME       250
#define REORDER_UPDATE_TIME     50
#define AICWF_REORDER_WINSIZE   64
#define SN_LESS(a, b)           (((a-b)&0x800)!=0)
#define SN_EQUAL(a, b)          (a == b)

struct reord_ctrl {
    struct aicwf_rx_priv *rx_priv;
    u8 enable;
    u16 ind_sn;
    u8 wsize_b;
    spinlock_t reord_list_lock;
    struct list_head reord_list;
    struct timer_list reord_timer;
    struct work_struct reord_timer_work;
};

struct reord_ctrl_info {
    u8 mac_addr[6];
    struct reord_ctrl preorder_ctrl[8];
    struct list_head list;
};

struct recv_msdu {
     struct sk_buff  *pkt;
     u8  tid;
     u16 seq_num;
     uint len;
     u8 *rx_data;
     //for pending rx reorder list
    struct list_head reord_pending_list;
    //for total frame list, when rxframe from busif, dequeue, when submit frame to net, enqueue
    struct list_head rxframe_list;
    struct reord_ctrl *preorder_ctrl;
};

struct aicwf_rx_priv {
    struct aic_usb_dev *usbdev;
    void *rwnx_vif;
    atomic_t rx_cnt;
    u32 data_len;
    spinlock_t rxqlock;
    struct frame_queue rxq;

    spinlock_t freeq_lock;
    struct list_head rxframes_freequeue;
    struct list_head stas_reord_list;
    spinlock_t stas_reord_lock;
    struct recv_msdu *recv_frames;
};

static inline int aicwf_bus_start(struct aicwf_bus *bus)
{
    return bus->ops->start(bus->dev);
}

static inline void aicwf_bus_stop(struct aicwf_bus *bus)
{
    bus->ops->stop(bus->dev);
}

static inline int aicwf_bus_txdata(struct aicwf_bus *bus, struct sk_buff *skb)
{
    return bus->ops->txdata(bus->dev, skb);
}

static inline int aicwf_bus_txmsg(struct aicwf_bus *bus, u8 *msg, uint len)
{
    return bus->ops->txmsg(bus->dev, msg, len);
}

static inline void aicwf_sched_timeout(u32 millisec)
{
    ulong timeout = 0, expires = 0;
    expires = jiffies + msecs_to_jiffies(millisec);
    timeout = millisec;

    while (timeout) {
        timeout = schedule_timeout(timeout);
        if (time_after(jiffies, expires))
            break;
    }
}

int aicwf_bus_init(uint bus_hdrlen, struct device *dev);
void aicwf_bus_deinit(struct device *dev);
void aicwf_tx_deinit(struct aicwf_tx_priv* tx_priv);
void aicwf_rx_deinit(struct aicwf_rx_priv* rx_priv);
struct aicwf_tx_priv* aicwf_tx_init(void *arg);
struct aicwf_rx_priv* aicwf_rx_init(void *arg);
void aicwf_frame_queue_init(struct frame_queue *pq, int num_prio, int max_len);
void aicwf_frame_queue_flush(struct frame_queue *pq);
bool aicwf_frame_enq(struct device *dev, struct frame_queue *q, struct sk_buff *pkt, int prio);
bool aicwf_rxframe_enqueue(struct device *dev, struct frame_queue *q, struct sk_buff *pkt);
bool aicwf_is_framequeue_empty(struct frame_queue *pq);
void aicwf_frame_tx(void *dev, struct sk_buff *skb);
void aicwf_dev_skb_free(struct sk_buff *skb);
struct sk_buff *aicwf_frame_dequeue(struct frame_queue *pq);
struct sk_buff *aicwf_frame_queue_peek_tail(struct frame_queue *pq, int *prio_out);

#endif /* _AICWF_TXRXIF_H_ */
