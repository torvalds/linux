/*
 * drivers/media/video/exynos/fimc-is-mc2/fimc-is-interface.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * The header file related to camera
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_INTERFACE_H
#define FIMC_IS_INTERFACE_H

#include "fimc-is-metadata.h"
#include "fimc-is-framemgr.h"
#include "fimc-is-video.h"

/*#define TRACE_WORK*/
/* cam_ctrl : 1
   shot :     2 */
#define TRACE_WORK_ID_CAMCTRL	0x1
#define TRACE_WORK_ID_SHOT	0x2
#define TRACE_WORK_ID_GENERAL	0x4
#define TRACE_WORK_ID_SCC	0x8
#define TRACE_WORK_ID_DIS	0x10
#define TRACE_WORK_ID_SCP	0x20
#define TRACE_WORK_ID_META	0x40
#define TRACE_WORK_ID_MASK	0xFF

#define MAX_NBLOCKING_COUNT	3
#define MAX_WORK_COUNT		10

#define TRY_TIMEOUT_COUNT	3
#define TRY_RECV_AWARE_COUNT	100

#define LOWBIT_OF(num)	(num >= 32 ? 0 : (u32)1<<num)
#define HIGHBIT_OF(num)	(num >= 32 ? (u32)1<<(num-32) : 0)

enum fimc_is_interface_state {
	IS_IF_STATE_OPEN,
	IS_IF_STATE_START,
	IS_IF_STATE_BUSY,
	IS_IF_STATE_SENSOR_OPENED,
	IS_IF_STATE_SENSOR_CLOSED,
};

enum interrupt_map {
	INTR_GENERAL		= 0,
	INTR_ISP_FDONE		= 1,
	INTR_SCC_FDONE		= 2,
	INTR_DIS_FDONE		= 3,
	INTR_SCP_FDONE		= 4,
	/* 5 is ISP YUV DONE */
	INTR_META_DONE		= 6,
	INTR_SHOT_DONE		= 7,
	INTR_MAX_MAP
};

enum streaming_state {
	IS_IF_STREAMING_INIT,
	IS_IF_STREAMING_OFF,
	IS_IF_STREAMING_ON
};

enum processing_state {
	IS_IF_PROCESSING_INIT,
	IS_IF_PROCESSING_OFF,
	IS_IF_PROCESSING_ON
};

enum pdown_ready_state {
	IS_IF_POWER_DOWN_READY,
	IS_IF_POWER_DOWN_NREADY
};

struct fimc_is_msg {
	u32	id;
	u32	command;
	u32	instance;
	u32	group;
	u32	parameter1;
	u32	parameter2;
	u32	parameter3;
	u32	parameter4;
};

struct fimc_is_work {
	struct list_head		list;
	struct fimc_is_msg		msg;
	u32				fcount;
	struct fimc_is_frame		*frame;
};

struct fimc_is_work_list {
	u32				id;
	struct fimc_is_work		work[MAX_WORK_COUNT];
	spinlock_t			slock_free;
	spinlock_t			slock_request;
	struct list_head		work_free_head;
	u32				work_free_cnt;
	struct list_head		work_request_head;
	u32				work_request_cnt;
	wait_queue_head_t		wait_queue;
};

struct fimc_is_interface {
	void __iomem			*regs;
	struct is_common_reg __iomem	*com_regs;
	unsigned long			state;
	spinlock_t			process_barrier;
	struct mutex			request_barrier;

	atomic_t			lock_pid;
	wait_queue_head_t		lock_wait_queue;
	wait_queue_head_t		init_wait_queue;
	wait_queue_head_t		idle_wait_queue;
	struct fimc_is_msg		reply;

	struct workqueue_struct		*workqueue;
	struct work_struct		work_wq[INTR_MAX_MAP];
	struct fimc_is_work_list	work_list[INTR_MAX_MAP];

	/* sensor streaming flag */
	enum streaming_state		streaming[FIMC_IS_MAX_NODES];
	/* firmware processing flag */
	enum processing_state		processing[FIMC_IS_MAX_NODES];
	/* frrmware power down ready flag */
	enum pdown_ready_state		pdown_ready;

	struct fimc_is_framemgr		*framemgr;

	struct fimc_is_work_list	nblk_cam_ctrl;
	spinlock_t			shot_check_lock;
	atomic_t			shot_check[FIMC_IS_MAX_NODES];
	atomic_t			shot_timeout[FIMC_IS_MAX_NODES];
	struct timer_list		timer;

	struct camera2_uctl		isp_peri_ctl;
	void				*core;
};

int fimc_is_interface_probe(struct fimc_is_interface *this,
	u32 regs,
	u32 irq,
	void *core_data);
int fimc_is_interface_open(struct fimc_is_interface *this);
int fimc_is_interface_close(struct fimc_is_interface *this);
void fimc_is_interface_lock(struct fimc_is_interface *this);
void fimc_is_interface_unlock(struct fimc_is_interface *this);

/*for debugging*/
int print_fre_work_list(struct fimc_is_work_list *this);
int print_req_work_list(struct fimc_is_work_list *this);

int fimc_is_hw_print(struct fimc_is_interface *this);
int fimc_is_hw_enum(struct fimc_is_interface *this);
int fimc_is_hw_open(struct fimc_is_interface *this,
	u32 instance, u32 module, u32 info, u32 group, u32 flag,
	u32 *mwidth, u32 *mheight);
int fimc_is_hw_saddr(struct fimc_is_interface *interface,
	u32 instance, u32 *setfile_addr);
int fimc_is_hw_setfile(struct fimc_is_interface *interface,
	u32 instance);
int fimc_is_hw_process_on(struct fimc_is_interface *this,
	u32 instance, u32 group);
int fimc_is_hw_process_off(struct fimc_is_interface *this,
	u32 instance, u32 group, u32 mode);
int fimc_is_hw_stream_on(struct fimc_is_interface *interface,
	u32 instance);
int fimc_is_hw_stream_off(struct fimc_is_interface *interface,
	u32 instance);
int fimc_is_hw_s_param(struct fimc_is_interface *interface,
	u32 instance, u32 indexes, u32 lindex, u32 hindex);
int fimc_is_hw_a_param(struct fimc_is_interface *this,
	u32 instance, u32 group, u32 sub_mode);
int fimc_is_hw_g_capability(struct fimc_is_interface *this,
	u32 instance, u32 address);
int fimc_is_hw_cfg_mem(struct fimc_is_interface *interface,
	u32 instance, u32 address, u32 size);
int fimc_is_hw_power_down(struct fimc_is_interface *interface,
	u32 instance);
int fimc_is_hw_i2c_lock(struct fimc_is_interface *interface,
	u32 instance, int clk, bool lock);

int fimc_is_hw_shot_nblk(struct fimc_is_interface *this,
	u32 instance, u32 group, u32 bayer, u32 shot, u32 fcount, u32 rcount);
int fimc_is_hw_s_camctrl_nblk(struct fimc_is_interface *this,
	u32 instance, u32 address, u32 fcount);

#endif
