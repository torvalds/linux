/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __MTK_BTIF_H_
#define __MTK_BTIF_H_

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/time.h>		/* gettimeofday */
#include <asm-generic/bug.h>

#include "btif_pub.h"
#include "btif_dma_pub.h"
#include "mtk_btif_exp.h"

#define BTIF_PORT_NR 1
#define BTIF_USER_NAME_MAX_LEN 32

/*-------------Register Defination Start ---------------*/
#if (defined(CONFIG_MTK_GMO_RAM_OPTIMIZE) && !defined(CONFIG_MTK_ENG_BUILD))
#define BTIF_RX_BUFFER_SIZE (1024 * 32)
#else
#define BTIF_RX_BUFFER_SIZE (1024 * 64)
#endif
#define BTIF_TX_FIFO_SIZE (1024 * 4)

/*------------Register Defination End ----------------*/

/*------------BTIF Module Clock and Power Control Defination---------------*/
typedef enum _ENUM_BTIF_RX_TYPE_ {
	BTIF_IRQ_CTX = 0,
	BTIF_TASKLET_CTX = BTIF_IRQ_CTX + 1,
	BTIF_THREAD_CTX = BTIF_TASKLET_CTX + 1,
	BTIF_WQ_CTX = BTIF_THREAD_CTX + 1,
	BTIF_RX_TYPE_MAX,
} ENUM_BTIF_RX_TYPE;

typedef enum _ENUM_BTIF_TX_TYPE_ {
	BTIF_TX_USER_CTX = 0,
	BTIF_TX_SINGLE_CTX = BTIF_TX_USER_CTX + 1,
	BTIF_TX_TYPE_MAX,
} ENUM_BTIF_TX_TYPE;

typedef enum _ENUM_BTIF_STATE_ {
	B_S_OFF = 0,
	B_S_SUSPEND = B_S_OFF + 1,
	B_S_DPIDLE = B_S_SUSPEND + 1,
	B_S_ON = B_S_DPIDLE + 1,
	B_S_MAX,
} ENUM_BTIF_STATE;

#define ENABLE_BTIF_RX_DMA 1
#define ENABLE_BTIF_TX_DMA 1

#if ENABLE_BTIF_TX_DMA
#define BTIF_TX_MODE BTIF_MODE_DMA
#else
#define BTIF_TX_MODE BTIF_MODE_PIO
#endif

#if ENABLE_BTIF_RX_DMA
#define BTIF_RX_MODE BTIF_MODE_DMA
#else
#define BTIF_RX_MODE BTIF_MODE_PIO
#endif

#define BTIF_RX_BTM_CTX BTIF_THREAD_CTX/*BTIF_WQ_CTX*//* BTIF_TASKLET_CTX */
/*
 * -- cannot be used because ,
 * mtk_wcn_stp_parser data will call *(stp_if_tx) to send ack,
 * in which context sleepable lock or usleep operation may be used,
 * these operation is not allowed in tasklet, may cause schedule_bug
 */

#define BTIF_TX_CTX BTIF_TX_USER_CTX	/* BTIF_TX_SINGLE_CTX */

#define ENABLE_BTIF_RX_THREAD_RT_SCHED 0
#define MAX_BTIF_RXD_TIME_REC 3

/*Structure Defination*/

/*-----------------BTIF setting--------------*/
typedef struct _mtk_btif_setting_ {
	ENUM_BTIF_MODE tx_mode;	/*BTIF Tx Mode Setting */
	ENUM_BTIF_MODE rx_mode;	/*BTIF Tx Mode Setting */
	ENUM_BTIF_RX_TYPE rx_type;	/*rx handle type */
	ENUM_BTIF_TX_TYPE tx_type;	/*tx type */
} mtk_btif_setting, *p_mtk_btif_setting;
/*---------------------------------------------------------------------------*/

#if 0
/*---------------------------------------------------------------------------*/
typedef struct _mtk_btif_register_ {
	unsigned int iir;	/*Interrupt Identification Register */
	unsigned int lsr;	/*Line Status Register */
	unsigned int fake_lcr;	/*Fake Lcr Regiseter */
	unsigned int fifo_ctrl;	/*FIFO Control Register */
	unsigned int ier;	/*Interrupt Enable Register */
	unsigned int sleep_en;	/*Sleep Enable Register */
	unsigned int rto_counter;	/*Rx Timeout Counter Register */
	unsigned int dma_en;	/*DMA Enalbe Register */
	unsigned int tri_lvl;	/*Tx/Rx Trigger Level Register */
	unsigned int wat_time;	/*Async Wait Time Register */
	unsigned int handshake;	/*New HandShake Mode Register */
	unsigned int sleep_wak;	/*Sleep Wakeup Reigster */
} mtk_btif_register, *p_mtk_btif_register;
/*---------------------------------------------------------------------------*/

#endif

typedef struct _btif_buf_str_ {
	unsigned int size;
	unsigned char *p_buf;
	/*
	 * For Tx: next Tx data pointer to FIFO;
	 * For Rx: next read data pointer from BTIF user
	 */
	unsigned int rd_idx;
	/*
	 * For Tx: next Tx data pointer from BTIF user;
	 * For Rx: next write data(from FIFO) pointer
	 */
	unsigned int wr_idx;
} btif_buf_str, *p_btif_buf_str;

/*---------------------------------------------------------------------------*/
typedef struct _mtk_btif_dma_ {
					/*p_mtk_btif*/ void *p_btif;
					/*BTIF pointer to which DMA belongs */

#if 0
	unsigned int channel;	/*DMA's channel */
#endif

	ENUM_BTIF_DIR dir;	/*DMA's direction: */
	bool enable;		/*DMA enable or disable flag */

	P_MTK_DMA_INFO_STR p_dma_info;	/*DMA's IRQ information */

#if 0
	mtk_dma_register register;	/*DMA's register */
#endif

	spinlock_t iolock;	/*io lock for DMA channel */
	atomic_t entry;		/* entry count */
} mtk_btif_dma, *p_mtk_btif_dma;

#if (defined(CONFIG_MTK_GMO_RAM_OPTIMIZE) && !defined(CONFIG_MTK_ENG_BUILD))
#define BTIF_LOG_ENTRY_NUM 10
#else
#define BTIF_LOG_ENTRY_NUM 30
#endif

#define BTIF_LOG_SZ  1536

typedef void (*MTK_BTIF_RX_NOTIFY) (void);

typedef struct _btif_log_buf_t_ {
	unsigned int len;
	struct timeval timer;
	unsigned char buffer[BTIF_LOG_SZ];
} BTIF_LOG_BUF_T, *P_BTIF_LOG_BUF_T;

typedef struct _btif_log_queue_t_ {
	ENUM_BTIF_DIR dir;
	bool enable;
	bool output_flag;
	unsigned int in;
	unsigned int out;
	unsigned int size;
	spinlock_t lock;
	P_BTIF_LOG_BUF_T p_queue[BTIF_LOG_ENTRY_NUM];
} BTIF_LOG_QUEUE_T, *P_BTIF_LOG_QUEUE_T;

/*---------------------------------------------------------------------------*/
typedef struct _mtk_btif_ {
	unsigned int open_counter;	/*open counter */
	bool enable;		/*BTIF module enable flag */
	bool lpbk_flag;		/*BTIF module enable flag */
#if 0
	unsigned long base;	/* BTIF controller base address */
#endif

	ENUM_BTIF_STATE state;	/*BTIF state mechanism */
	struct mutex state_mtx;	/*lock to BTIF state mechanism's state change */
	struct mutex ops_mtx;	/*lock to BTIF's open and close */

#if 0
	mtk_btif_register register;	/*BTIF registers */
#endif

	ENUM_BTIF_MODE tx_mode;	/* BTIF Tx channel mode */
	ENUM_BTIF_MODE rx_mode;	/* BTIF Rx channel mode */
	struct mutex tx_mtx;	/*lock to BTIF's tx process */
/*rx handling */
	ENUM_BTIF_RX_TYPE btm_type;	/*BTIF Rx bottom half context */
/*tx handling*/
	ENUM_BTIF_TX_TYPE tx_ctx;	/*BTIF tx context */
/* unsigned char rx_buf[BTIF_RX_BUFFER_SIZE]; */
	btif_buf_str btif_buf;
	spinlock_t rx_irq_spinlock;	/*lock for rx irq handling */

/*rx workqueue information*/
	/*lock to BTIF's rx bottom half when kernel thread is used */
	struct mutex rx_mtx;
	struct workqueue_struct *p_rx_wq;
	struct work_struct rx_work;

	struct workqueue_struct *p_tx_wq;
	struct work_struct tx_work;
	struct kfifo *p_tx_fifo;

/*rx tasklet information*/
	struct tasklet_struct rx_tasklet;
	/*lock to BTIF's rx bottom half when tasklet is used */
	spinlock_t rx_tasklet_spinlock;

/*rx thread information*/
	struct task_struct *p_task;
	struct completion rx_comp;

	mtk_btif_setting *setting;	/*BTIF setting */

	p_mtk_btif_dma p_tx_dma;	/*BTIF Tx channel DMA */
	p_mtk_btif_dma p_rx_dma;	/*BTIF Rx channel DMA */

	MTK_WCN_BTIF_RX_CB rx_cb;	/*Rx callback function */
	MTK_BTIF_RX_NOTIFY rx_notify;

	P_MTK_BTIF_INFO_STR p_btif_info;	/*BTIF's information */

/*Log Tx data to buffer*/
	BTIF_LOG_QUEUE_T tx_log;

/*Log Rx data to buffer*/
	BTIF_LOG_QUEUE_T rx_log;

/* struct list_head *p_user_list; */
	struct list_head user_list;
/* get btif dev pointer*/
	void *private_data;
} mtk_btif, *p_mtk_btif;
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
#if 0
/*---------------------------------------------------------------------------*/
typedef struct _mtk_dma_register_ {
	unsigned int int_flag;	/*Tx offset:0x0     Rx offset:0x0 */
	unsigned int int_enable;	/*Tx offset:0x4     Rx offset:0x4 */
	unsigned int dma_enable;	/*Tx offset:0x8     Rx offset:0x8 */
	unsigned int dma_reset;	/*Tx offset:0xc     Rx offset:0xc */
	unsigned int dma_stop;	/*Tx offset:0x10     Rx offset:0x10 */
	unsigned int dma_flush;	/*Tx offset:0x14     Rx offset:0x14 */
	unsigned int vff_addr;	/*Tx offset:0x1c     Rx offset:0x1c */
	unsigned int vff_len;	/*Tx offset:0x24     Rx offset:0x24 */
	unsigned int vff_thr;	/*Tx offset:0x28     Rx offset:0x28 */
	unsigned int vff_wpt;	/*Tx offset:0x2c     Rx offset:0x2c */
	unsigned int vff_rpt;	/*Tx offset:0x30     Rx offset:0x30 */
	unsigned int rx_fc_thr;	/*Tx:No this register     Rx offset:0x34 */
	unsigned int int_buf_size;	/*Tx offset:0x38     Rx offset:0x38 */
	unsigned int vff_valid_size;	/*Tx offset:0x3c     Rx offset:0x3c */
	unsigned int vff_left_size;	/*Tx offset:0x40     Rx offset:0x40 */
	unsigned int debug_status;	/*Tx offset:0x50     Rx offset:0x50 */
} mtk_dma_register, *p_mtk_dma_register;
/*---------------------------------------------------------------------------*/
#endif

/*---------------------------------------------------------------------------*/
typedef struct _mtk_btif_user_ {
	bool enable;		/*register its state */
	struct list_head entry;	/*btif_user's bi-direction list table */
	/*BTIF's user, static allocation */
	char u_name[BTIF_USER_NAME_MAX_LEN];
	unsigned long u_id;
	p_mtk_btif p_btif;
} mtk_btif_user, *p_mtk_btif_user;

/*---------------------------------------------------------------------------*/
#define BBS_PTR(ptr, idx) ((ptr->p_buf) + idx)

#define BBS_SIZE(ptr) ((ptr)->size)
#define BBS_MASK(ptr) (BBS_SIZE(ptr) - 1)
#define BBS_COUNT(ptr) ((ptr)->wr_idx >= (ptr)->rd_idx ? (ptr)->wr_idx - \
(ptr)->rd_idx : BBS_SIZE(ptr) - \
((ptr)->rd_idx - (ptr)->wr_idx))
#define BBS_COUNT_CUR(ptr, wr_idx) (wr_idx >= (ptr)->rd_idx ? wr_idx - \
(ptr)->rd_idx : BBS_SIZE(ptr) - \
((ptr)->rd_idx - wr_idx))

#define BBS_LEFT(ptr) (BBS_SIZE(ptr) - BBS_COUNT(ptr))

#define BBS_AVL_SIZE(ptr) (BBS_SIZE(ptr) - BBS_COUNT(ptr))
#define BBS_FULL(ptr) (BBS_COUNT(ptr) - BBS_SIZE(ptr))
#define BBS_EMPTY(ptr) ((ptr)->wr_idx == (ptr)->rd_idx)
#define BBS_WRITE_MOVE_NEXT(ptr) ((ptr)->wr_idx = \
((ptr)->wr_idx + 1) & BBS_MASK(ptr))
#define BBS_READ_MOVE_NEXT(ptr) ((ptr)->rd_idx = \
((ptr)->rd_idx + 1) & BBS_MASK(ptr))

#define BBS_INIT(ptr) \
{ \
(ptr)->rd_idx = (ptr)->wr_idx = 0; \
(ptr)->size = BTIF_RX_BUFFER_SIZE; \
}


#define BTIF_MUTEX_UNLOCK(x) mutex_unlock(x)

extern mtk_btif g_btif[];

int btif_open(p_mtk_btif p_btif);
int btif_close(p_mtk_btif p_btif);
int btif_send_data(p_mtk_btif p_btif,
		   const unsigned char *p_buf, unsigned int buf_len);
int btif_enter_dpidle(p_mtk_btif p_btif);
int btif_exit_dpidle(p_mtk_btif p_btif);
int btif_rx_cb_reg(p_mtk_btif p_btif, MTK_WCN_BTIF_RX_CB rx_cb);

/*for test purpose*/
int _btif_suspend(p_mtk_btif p_btif);
int _btif_resume(p_mtk_btif p_btif);
int _btif_restore_noirq(p_mtk_btif p_btif);

int btif_lpbk_ctrl(p_mtk_btif p_btif, bool flag);
int btif_log_buf_dmp_in(P_BTIF_LOG_QUEUE_T p_log_que, const char *p_buf,
			int len);
int btif_dump_data(char *p_buf, int len);
int btif_log_buf_dmp_out(P_BTIF_LOG_QUEUE_T p_log_que);
int btif_log_buf_enable(P_BTIF_LOG_QUEUE_T p_log_que);
int btif_log_buf_disable(P_BTIF_LOG_QUEUE_T p_log_que);
int btif_log_output_enable(P_BTIF_LOG_QUEUE_T p_log_que);
int btif_log_output_disable(P_BTIF_LOG_QUEUE_T p_log_que);
int btif_log_buf_reset(P_BTIF_LOG_QUEUE_T p_log_que);
int btif_log_buf_init(p_mtk_btif p_btif);
int btif_dump_reg(p_mtk_btif p_btif);
int btif_rx_notify_reg(p_mtk_btif p_btif, MTK_BTIF_RX_NOTIFY rx_notify);
int btif_raise_wak_signal(p_mtk_btif p_btif);
int btif_clock_ctrl(p_mtk_btif p_btif, int en);
bool btif_parser_wmt_evt(p_mtk_btif p_btif,
				const char *sub_str,
				unsigned int sub_len);
void mtk_btif_read_cpu_sw_rst_debug(void);

#endif /*__MTK_BTIF_H_*/
