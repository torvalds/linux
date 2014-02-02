/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_

#include <basic_types.h>

#define _FAIL		0
#define _SUCCESS	1
#define RTW_RX_HANDLED	2

#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/circ_buf.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>
#include <linux/atomic.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/sem.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>	/*  Necessary because we use the proc fs */
#include <linux/interrupt.h>	/*  for struct tasklet_struct */
#include <linux/ip.h>
#include <linux/kthread.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>

struct	__queue	{
	struct	list_head	queue;
	spinlock_t lock;
};

static inline struct list_head *get_next(struct list_head *list)
{
	return list->next;
}

static inline struct list_head *get_list_head(struct __queue *queue)
{
	return &(queue->queue);
}


#define LIST_CONTAINOR(ptr, type, member) \
	((type *)((char *)(ptr)-(size_t)(&((type *)0)->member)))

static inline int _enter_critical_mutex(struct mutex *pmutex,
					unsigned long *pirqL)
{
	int ret;

	ret = mutex_lock_interruptible(pmutex);
	return ret;
}


static inline void _exit_critical_mutex(struct mutex *pmutex,
					unsigned long *pirqL)
{
		mutex_unlock(pmutex);
}

static inline void rtw_list_delete(struct list_head *plist)
{
	list_del_init(plist);
}

static inline void _init_timer(struct timer_list *ptimer,
			       struct  net_device *nic_hdl,
			       void *pfunc, void *cntx)
{
	ptimer->function = pfunc;
	ptimer->data = (unsigned long)cntx;
	init_timer(ptimer);
}

static inline void _set_timer(struct timer_list *ptimer, u32 delay_time)
{
	mod_timer(ptimer , (jiffies+(delay_time*HZ/1000)));
}

static inline void _cancel_timer(struct timer_list *ptimer, u8 *bcancelled)
{
	del_timer_sync(ptimer);
	*bcancelled = true;/* true ==1; false==0 */
}

#define RTW_TIMER_HDL_ARGS void *FunctionContext
#define RTW_TIMER_HDL_NAME(name) rtw_##name##_timer_hdl
#define RTW_DECLARE_TIMER_HDL(name) \
	void RTW_TIMER_HDL_NAME(name)(RTW_TIMER_HDL_ARGS)

static inline void _init_workitem(struct work_struct *pwork, void *pfunc,
				  void *cntx)
{
	INIT_WORK(pwork, pfunc);
}

static inline void _set_workitem(struct work_struct *pwork)
{
	schedule_work(pwork);
}

static inline void _cancel_workitem_sync(struct work_struct *pwork)
{
	cancel_work_sync(pwork);
}

static inline int rtw_netif_queue_stopped(struct net_device *pnetdev)
{
	return  netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 0)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 1)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 2)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 3));
}

static inline void rtw_netif_wake_queue(struct net_device *pnetdev)
{
	netif_tx_wake_all_queues(pnetdev);
}

static inline void rtw_netif_start_queue(struct net_device *pnetdev)
{
	netif_tx_start_all_queues(pnetdev);
}

static inline void rtw_netif_stop_queue(struct net_device *pnetdev)
{
	netif_tx_stop_all_queues(pnetdev);
}

#ifndef BIT
	#define BIT(x)	(1 << (x))
#endif

#define BIT0	0x00000001
#define BIT1	0x00000002
#define BIT2	0x00000004
#define BIT3	0x00000008
#define BIT4	0x00000010
#define BIT5	0x00000020
#define BIT6	0x00000040
#define BIT7	0x00000080
#define BIT8	0x00000100
#define BIT9	0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000
#define BIT32	0x0100000000
#define BIT33	0x0200000000
#define BIT34	0x0400000000
#define BIT35	0x0800000000
#define BIT36	0x1000000000

extern int RTW_STATUS_CODE(int error_code);

/* flags used for rtw_update_mem_stat() */
enum {
	MEM_STAT_VIR_ALLOC_SUCCESS,
	MEM_STAT_VIR_ALLOC_FAIL,
	MEM_STAT_VIR_FREE,
	MEM_STAT_PHY_ALLOC_SUCCESS,
	MEM_STAT_PHY_ALLOC_FAIL,
	MEM_STAT_PHY_FREE,
	MEM_STAT_TX, /* used to distinguish TX/RX, asigned from caller */
	MEM_STAT_TX_ALLOC_SUCCESS,
	MEM_STAT_TX_ALLOC_FAIL,
	MEM_STAT_TX_FREE,
	MEM_STAT_RX, /* used to distinguish TX/RX, asigned from caller */
	MEM_STAT_RX_ALLOC_SUCCESS,
	MEM_STAT_RX_ALLOC_FAIL,
	MEM_STAT_RX_FREE
};

extern unsigned char MCS_rate_2R[16];
extern unsigned char MCS_rate_1R[16];
extern unsigned char RTW_WPA_OUI[];
extern unsigned char WPA_TKIP_CIPHER[4];
extern unsigned char RSN_TKIP_CIPHER[4];

#define rtw_update_mem_stat(flag, sz) do {} while (0)
u8 *_rtw_vmalloc(u32 sz);
u8 *_rtw_zvmalloc(u32 sz);
void _rtw_vmfree(u8 *pbuf, u32 sz);
u8 *_rtw_zmalloc(u32 sz);
u8 *_rtw_malloc(u32 sz);
void _rtw_mfree(u8 *pbuf, u32 sz);
#define rtw_vmalloc(sz)			_rtw_vmalloc((sz))
#define rtw_zvmalloc(sz)			_rtw_zvmalloc((sz))
#define rtw_vmfree(pbuf, sz)		_rtw_vmfree((pbuf), (sz))
#define rtw_malloc(sz)			_rtw_malloc((sz))
#define rtw_zmalloc(sz)			_rtw_zmalloc((sz))
#define rtw_mfree(pbuf, sz)		_rtw_mfree((pbuf), (sz))

void *rtw_malloc2d(int h, int w, int size);
void rtw_mfree2d(void *pbuf, int h, int w, int size);

void _rtw_memcpy(void *dec, void *sour, u32 sz);
int  _rtw_memcmp(void *dst, void *src, u32 sz);
void _rtw_memset(void *pbuf, int c, u32 sz);

void _rtw_init_listhead(struct list_head *list);
u32  rtw_is_list_empty(struct list_head *phead);
void rtw_list_insert_head(struct list_head *plist, struct list_head *phead);
void rtw_list_insert_tail(struct list_head *plist, struct list_head *phead);
void rtw_list_delete(struct list_head *plist);

u32  _rtw_down_sema(struct semaphore *sema);

void _rtw_init_queue(struct __queue *pqueue);
u32  _rtw_queue_empty(struct __queue *pqueue);
u32  rtw_end_of_queue_search(struct list_head *queue,
			     struct list_head *pelement);

u32  rtw_systime_to_ms(u32 systime);
u32  rtw_ms_to_systime(u32 ms);
s32  rtw_get_passing_time_ms(u32 start);
s32  rtw_get_time_interval_ms(u32 start, u32 end);

void rtw_sleep_schedulable(int ms);

u32  rtw_atoi(u8 *s);

static inline unsigned char _cancel_timer_ex(struct timer_list *ptimer)
{
	return del_timer_sync(ptimer);
}

static inline void thread_enter(char *name)
{
	allow_signal(SIGTERM);
}

static inline void flush_signals_thread(void)
{
	if (signal_pending(current))
		flush_signals(current);
}

static inline int res_to_status(int res)
{
	return res;
}

#define _RND(sz, r) ((((sz)+((r)-1))/(r))*(r))
#define RND4(x)	(((x >> 2) + (((x & 3) == 0) ?  0 : 1)) << 2)

static inline u32 _RND4(u32 sz)
{
	u32	val;

	val = ((sz >> 2) + ((sz & 3) ? 1 : 0)) << 2;
	return val;
}

static inline u32 _RND8(u32 sz)
{
	u32	val;

	val = ((sz >> 3) + ((sz & 7) ? 1 : 0)) << 3;
	return val;
}

static inline u32 _RND128(u32 sz)
{
	u32	val;

	val = ((sz >> 7) + ((sz & 127) ? 1 : 0)) << 7;
	return val;
}

static inline u32 _RND256(u32 sz)
{
	u32	val;

	val = ((sz >> 8) + ((sz & 255) ? 1 : 0)) << 8;
	return val;
}

static inline u32 _RND512(u32 sz)
{
	u32	val;

	val = ((sz >> 9) + ((sz & 511) ? 1 : 0)) << 9;
	return val;
}

static inline u32 bitshift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++)
		if (((bitmask>>i) &  0x1) == 1)
			break;
	return i;
}

/*  limitation of path length */
#define PATH_LENGTH_MAX PATH_MAX

struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv,
						    void *old_priv);
struct net_device *rtw_alloc_etherdev(int sizeof_priv);

#define rtw_netdev_priv(netdev)					\
	(((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv)
void rtw_free_netdev(struct net_device *netdev);

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) adapter->pnetdev->name
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
#define FUNC_ADPT_ARG(adapter) __func__, adapter->pnetdev->name

#define rtw_signal_process(pid, sig) kill_pid(find_vpid((pid)), (sig), 1)

u64 rtw_modular64(u64 x, u64 y);
u64 rtw_division64(u64 x, u64 y);

/* Macros for handling unaligned memory accesses */

#define RTW_GET_BE16(a) ((u16) (((a)[0] << 8) | (a)[1]))
#define RTW_PUT_BE16(a, val)			\
	do {					\
		(a)[0] = ((u16) (val)) >> 8;	\
		(a)[1] = ((u16) (val)) & 0xff;	\
	} while (0)

#define RTW_GET_LE16(a) ((u16) (((a)[1] << 8) | (a)[0]))
#define RTW_PUT_LE16(a, val)			\
	do {					\
		(a)[1] = ((u16) (val)) >> 8;	\
		(a)[0] = ((u16) (val)) & 0xff;	\
	} while (0)

#define RTW_GET_BE24(a) ((((u32) (a)[0]) << 16) | (((u32) (a)[1]) << 8) | \
			 ((u32) (a)[2]))
#define RTW_PUT_BE24(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[2] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define RTW_GET_BE32(a) ((((u32) (a)[0]) << 24) | (((u32) (a)[1]) << 16) | \
			 (((u32) (a)[2]) << 8) | ((u32) (a)[3]))
#define RTW_PUT_BE32(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[3] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define RTW_GET_LE32(a) ((((u32) (a)[3]) << 24) | (((u32) (a)[2]) << 16) | \
			 (((u32) (a)[1]) << 8) | ((u32) (a)[0]))
#define RTW_PUT_LE32(a, val)					\
	do {							\
		(a)[3] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[0] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define RTW_GET_BE64(a) ((((u64) (a)[0]) << 56) | (((u64) (a)[1]) << 48) | \
			 (((u64) (a)[2]) << 40) | (((u64) (a)[3]) << 32) | \
			 (((u64) (a)[4]) << 24) | (((u64) (a)[5]) << 16) | \
			 (((u64) (a)[6]) << 8) | ((u64) (a)[7]))
#define RTW_PUT_BE64(a, val)				\
	do {						\
		(a)[0] = (u8) (((u64) (val)) >> 56);	\
		(a)[1] = (u8) (((u64) (val)) >> 48);	\
		(a)[2] = (u8) (((u64) (val)) >> 40);	\
		(a)[3] = (u8) (((u64) (val)) >> 32);	\
		(a)[4] = (u8) (((u64) (val)) >> 24);	\
		(a)[5] = (u8) (((u64) (val)) >> 16);	\
		(a)[6] = (u8) (((u64) (val)) >> 8);	\
		(a)[7] = (u8) (((u64) (val)) & 0xff);	\
	} while (0)

#define RTW_GET_LE64(a) ((((u64) (a)[7]) << 56) | (((u64) (a)[6]) << 48) | \
			 (((u64) (a)[5]) << 40) | (((u64) (a)[4]) << 32) | \
			 (((u64) (a)[3]) << 24) | (((u64) (a)[2]) << 16) | \
			 (((u64) (a)[1]) << 8) | ((u64) (a)[0]))

void rtw_buf_free(u8 **buf, u32 *buf_len);
void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len);

struct rtw_cbuf {
	u32 write;
	u32 read;
	u32 size;
	void *bufs[0];
};

bool rtw_cbuf_full(struct rtw_cbuf *cbuf);
bool rtw_cbuf_empty(struct rtw_cbuf *cbuf);
bool rtw_cbuf_push(struct rtw_cbuf *cbuf, void *buf);
void *rtw_cbuf_pop(struct rtw_cbuf *cbuf);
struct rtw_cbuf *rtw_cbuf_alloc(u32 size);
int wifirate2_ratetbl_inx(unsigned char rate);

#endif
