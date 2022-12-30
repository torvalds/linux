/**
 ******************************************************************************
 *
 * @file sdio.h
 *
 * @brief sdio driver definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef __SDIO_H
#define __SDIO_H

#include "ecrnx_defs.h"
#include "core.h"

#define	ESWIN_SDIO_VENDER	0x0296
#define	ESWIN_SDIO_DEVICE	0x5347

#define	ESWIN_SDIO_BLK_SIZE 	512

#define TX_SLOT 0
#define RX_SLOT 1

#define CREDIT_QUEUE_MAX (12)


#define TCN  (3*2)
#define TCNE (0)

#define CREDIT_AC0	4//(TCN*4+TCNE)	/* BK */
#define CREDIT_AC1	30//(TCN*3+TCNE)	/* BE */
#define CREDIT_AC2	4//(TCN*2+TCNE)	/* VI */
#define CREDIT_AC3	4//(TCN*1+TCNE)	/* VO */

struct sdio_sys_reg {
	u8 wakeup;	/* 0x0 */
	u8 status;	/* 0x1 */
	u16 chip_id;	/* 0x2-0x3 */
	u32 modem_id;	/* 0x4-0x7 */
	u32 sw_id;	/* 0x8-0xb */
	u32 board_id;	/* 0xc-0xf */
} __packed;

struct sdio_status_reg {
	struct {
		u8 mode;
		u8 enable;
		u8 latched_status;
		u8 status;
	} eirq;
	u8 txq_status[6];
	u8 rxq_status[6];
	u32 msg[4];

#define EIRQ_IO_ENABLE	(1<<2)
#define EIRQ_EDGE	(1<<1)
#define EIRQ_ACTIVE_LO	(1<<0)

#define EIRQ_DEV_SLEEP	(1<<3)
#define EIRQ_DEV_READY	(1<<2)
#define EIRQ_RXQ	(1<<1)
#define EIRQ_TXQ	(1<<0)

#define TXQ_ERROR	(1<<7)
#define TXQ_SLOT_COUNT	(0x7F)
#define RXQ_SLOT_COUNT	(0x7F)

} __packed;

struct sdio_rx_head_t {
	unsigned int	  next_rx_len;
	unsigned short	  data_len;
	unsigned short	  avl_len;
};

struct sdio_data_t {
	unsigned int	  credit_vif0;
	unsigned int	  credit_vif1;
	unsigned int	  info_wr;
	unsigned int	  info_rd;
};

struct eswin_sdio {
	struct eswin * tr;
	struct sdio_func   *func;
	struct sdio_func   *func2;

	/* work, kthread, ... */
	struct delayed_work work;
	struct task_struct *kthread;
	wait_queue_head_t wait; /* wait queue */

	struct task_struct *kthread_unpack;
	wait_queue_head_t wait_unpack;

	struct {
		struct sdio_sys_reg    sys;
		struct sdio_status_reg status;
	} hw;

	spinlock_t lock;
	struct {
		unsigned int head;
		unsigned int tail;
		unsigned int size;
		unsigned int count;
	} slot[2];
	/* VIF0(AC0~AC3), BCN, CONC, VIF1(AC0~AC3), padding*/
	u8 front[CREDIT_QUEUE_MAX];
	u8 rear[CREDIT_QUEUE_MAX];
	u8 credit_max[CREDIT_QUEUE_MAX];

/*
	unsigned long loopback_prev_cnt;
	unsigned long loopback_total_cnt;
	unsigned long loopback_last_jiffies;
	unsigned long loopback_read_usec;
	unsigned long loopback_write_usec;
	unsigned long loopback_measure_cnt;
*/
	//struct eswin_sdio_ops_t *ops;
	unsigned int	          recv_len;
	unsigned int	          recv_num;
//	struct dentry *debugfs;

	unsigned int	  credit_vif0;
	unsigned int	  credit_vif1;
	
	struct sdio_data_t sdio_info;
	unsigned int	  slave_avl_buf;
    atomic_t          slave_buf_suspend;
	unsigned int	  curr_tx_size;
	unsigned int	  next_rx_size;
//	struct sk_buff  *skb_tx_last;

	struct sk_buff_head skb_rx_list;
	//struct sk_buff_head *skb_rx_unpack_list;

};

struct sdio_ops {
	int (*start)(struct eswin *tr);
	int (*xmit)(struct eswin *tr, struct tx_buff_pkg_node * node);
	int (*suspend)(struct eswin *tr);
	int (*resume)(struct eswin *tr);
	int (*write)(struct eswin *tr, const void* data, const u32 len);
	int (*wait_ack)(struct eswin *tr);
};

extern int ecrnx_sdio_register_drv(void);
extern void ecrnx_sdio_unregister_drv(void);

#endif /* __SDIO_H */
