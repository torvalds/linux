/***************************************************************************
 * Copyright (c) 2005-2009, Broadcom Corporation.
 *
 *  Name: crystalhd_misc . h
 *
 *  Description:
 *		BCM70012 Linux driver general purpose routines.
 *		Includes reg/mem read and write routines.
 *
 *  HISTORY:
 *
 **********************************************************************
 * This file is part of the crystalhd device driver.
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#ifndef _CRYSTALHD_MISC_H_
#define _CRYSTALHD_MISC_H_

#include "crystalhd.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ioctl.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include "bc_dts_glob_lnx.h"

/* Global log level variable defined in crystal_misc.c file */
extern uint32_t g_linklog_level;

/* Global element pool for all Queue management.
 * TX: Active = BC_TX_LIST_CNT, Free = BC_TX_LIST_CNT.
 * RX: Free = BC_RX_LIST_CNT, Active = 2
 * FW-CMD: 4
 */
#define	BC_LINK_ELEM_POOL_SZ	((BC_TX_LIST_CNT * 2) + BC_RX_LIST_CNT + 2 + 4)

/* Driver's IODATA pool count */
#define	CHD_IODATA_POOL_SZ    (BC_IOCTL_DATA_POOL_SIZE * BC_LINK_MAX_OPENS)

/* Scatter Gather memory pool size for Tx and Rx */
#define BC_LINK_SG_POOL_SZ    (BC_TX_LIST_CNT + BC_RX_LIST_CNT)

enum crystalhd_dio_sig {
	crystalhd_dio_inv = 0,
	crystalhd_dio_locked,
	crystalhd_dio_sg_mapped,
};

struct crystalhd_dio_user_info {
	void			*xfr_buff;
	uint32_t		xfr_len;
	uint32_t		uv_offset;
	bool			dir_tx;

	uint32_t		uv_sg_ix;
	uint32_t		uv_sg_off;
	int			comp_sts;
	int			ev_sts;
	uint32_t		y_done_sz;
	uint32_t		uv_done_sz;
	uint32_t		comp_flags;
	bool			b422mode;
};

struct crystalhd_dio_req {
	uint32_t			sig;
	uint32_t			max_pages;
	struct page			**pages;
	struct scatterlist		*sg;
	int				sg_cnt;
	int				page_cnt;
	int				direction;
	struct crystalhd_dio_user_info	uinfo;
	void				*fb_va;
	uint32_t			fb_size;
	dma_addr_t			fb_pa;
	struct crystalhd_dio_req	*next;
};

#define BC_LINK_DIOQ_SIG	(0x09223280)

struct crystalhd_elem {
	struct crystalhd_elem	*flink;
	struct crystalhd_elem	*blink;
	void			*data;
	uint32_t		tag;
};

typedef void (*crystalhd_data_free_cb)(void *context, void *data);

struct crystalhd_dioq {
	uint32_t		sig;
	struct crystalhd_adp	*adp;
	struct crystalhd_elem		*head;
	struct crystalhd_elem		*tail;
	uint32_t		count;
	spinlock_t		lock;
	wait_queue_head_t	event;
	crystalhd_data_free_cb	data_rel_cb;
	void			*cb_context;
};

typedef void (*hw_comp_callback)(struct crystalhd_dio_req *,
				 wait_queue_head_t *event, enum BC_STATUS sts);

/*========= Decoder (7412) register access routines.================= */
uint32_t bc_dec_reg_rd(struct crystalhd_adp *, uint32_t);
void bc_dec_reg_wr(struct crystalhd_adp *, uint32_t, uint32_t);

/*========= Link (70012) register access routines.. =================*/
uint32_t crystalhd_reg_rd(struct crystalhd_adp *, uint32_t);
void crystalhd_reg_wr(struct crystalhd_adp *, uint32_t, uint32_t);

/*========= Decoder (7412) memory access routines..=================*/
enum BC_STATUS crystalhd_mem_rd(struct crystalhd_adp *,
			 uint32_t, uint32_t, uint32_t *);
enum BC_STATUS crystalhd_mem_wr(struct crystalhd_adp *,
			 uint32_t, uint32_t, uint32_t *);

/*==========Link (70012) PCIe Config access routines.================*/
enum BC_STATUS crystalhd_pci_cfg_rd(struct crystalhd_adp *,
			 uint32_t, uint32_t, uint32_t *);
enum BC_STATUS crystalhd_pci_cfg_wr(struct crystalhd_adp *,
			 uint32_t, uint32_t, uint32_t);

/*========= Linux Kernel Interface routines. ======================= */
void *bc_kern_dma_alloc(struct crystalhd_adp *, uint32_t, dma_addr_t *);
void bc_kern_dma_free(struct crystalhd_adp *, uint32_t,
		      void *, dma_addr_t);
#define crystalhd_create_event(_ev)	init_waitqueue_head(_ev)
#define crystalhd_set_event(_ev)		wake_up_interruptible(_ev)
#define crystalhd_wait_on_event(ev, condition, timeout, ret, nosig)	\
do {									\
	DECLARE_WAITQUEUE(entry, current);				\
	unsigned long end = jiffies + ((timeout * HZ) / 1000);		\
		ret = 0;						\
	add_wait_queue(ev, &entry);					\
	for (;;) {							\
		__set_current_state(TASK_INTERRUPTIBLE);		\
		if (condition) {					\
			break;						\
		}							\
		if (time_after_eq(jiffies, end)) {			\
			ret = -EBUSY;					\
			break;						\
		}							\
		schedule_timeout((HZ / 100 > 1) ? HZ / 100 : 1);	\
		if (!nosig && signal_pending(current)) {		\
			ret = -EINTR;					\
			break;						\
		}							\
	}								\
	__set_current_state(TASK_RUNNING);				\
	remove_wait_queue(ev, &entry);					\
} while (0)

/*================ Direct IO mapping routines ==================*/
extern int crystalhd_create_dio_pool(struct crystalhd_adp *, uint32_t);
extern void crystalhd_destroy_dio_pool(struct crystalhd_adp *);
extern enum BC_STATUS crystalhd_map_dio(struct crystalhd_adp *, void *,
		 uint32_t, uint32_t, bool, bool, struct crystalhd_dio_req**);

extern enum BC_STATUS crystalhd_unmap_dio(struct crystalhd_adp *,
					 struct crystalhd_dio_req*);
#define crystalhd_get_sgle_paddr(_dio, _ix) (cpu_to_le64(sg_dma_address(&_dio->sg[_ix])))
#define crystalhd_get_sgle_len(_dio, _ix) (cpu_to_le32(sg_dma_len(&_dio->sg[_ix])))

/*================ General Purpose Queues ==================*/
extern enum BC_STATUS crystalhd_create_dioq(struct crystalhd_adp *,
		 struct crystalhd_dioq **, crystalhd_data_free_cb , void *);
extern void crystalhd_delete_dioq(struct crystalhd_adp *,
		 struct crystalhd_dioq *);
extern enum BC_STATUS crystalhd_dioq_add(struct crystalhd_dioq *ioq,
		 void *data, bool wake, uint32_t tag);
extern void *crystalhd_dioq_fetch(struct crystalhd_dioq *ioq);
extern void *crystalhd_dioq_find_and_fetch(struct crystalhd_dioq *ioq,
		 uint32_t tag);
extern void *crystalhd_dioq_fetch_wait(struct crystalhd_dioq *ioq,
		 uint32_t to_secs, uint32_t *sig_pend);

#define crystalhd_dioq_count(_ioq)	((_ioq) ? _ioq->count : 0)

extern int crystalhd_create_elem_pool(struct crystalhd_adp *, uint32_t);
extern void crystalhd_delete_elem_pool(struct crystalhd_adp *);


/*================ Debug routines/macros .. ================================*/
extern void crystalhd_show_buffer(uint32_t off, uint8_t *buff,
		 uint32_t dwcount);

enum _chd_log_levels {
	BCMLOG_ERROR		= 0x80000000,	/* Don't disable this option */
	BCMLOG_DATA		= 0x40000000,	/* Data, enable by default */
	BCMLOG_SPINLOCK		= 0x20000000,	/* Spcial case for Spin locks*/

	/* Following are allowed only in debug mode */
	BCMLOG_INFO		= 0x00000001,	/* Generic informational */
	BCMLOG_DBG		= 0x00000002,	/* First level Debug info */
	BCMLOG_SSTEP		= 0x00000004,	/* Stepping information */
};


#define BCMLOG(trace, fmt, args...)	\
do {					\
	if (g_linklog_level & trace)	\
		printk(fmt, ##args);	\
} while (0)


#define BCMLOG_ERR(fmt, args...)				\
do {								\
	if (g_linklog_level & BCMLOG_ERROR)			\
		printk(KERN_ERR "*ERR*:%s:%d: "fmt,		\
				__FILE__, __LINE__, ##args);	\
} while (0)

#endif
