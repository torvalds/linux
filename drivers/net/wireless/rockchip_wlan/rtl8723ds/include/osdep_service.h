/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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


#define _FAIL		0
#define _SUCCESS	1
#define RTW_RX_HANDLED 2
/* #define RTW_STATUS_TIMEDOUT -110 */

#undef _TRUE
#define _TRUE		1

#undef _FALSE
#define _FALSE		0


#ifdef PLATFORM_FREEBSD
	#include <osdep_service_bsd.h>
#endif

#ifdef PLATFORM_LINUX
	#include <osdep_service_linux.h>
#endif

#ifdef PLATFORM_OS_XP
	#include <osdep_service_xp.h>
#endif

#ifdef PLATFORM_OS_CE
	#include <osdep_service_ce.h>
#endif

#define RTW_TIMER_HDL_NAME(name) rtw_##name##_timer_hdl
#define RTW_DECLARE_TIMER_HDL(name) void RTW_TIMER_HDL_NAME(name)(RTW_TIMER_HDL_ARGS)

/* #include <rtw_byteorder.h> */

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

#ifndef RTK_DMP_PLATFORM
	#define CONFIG_USE_VMALLOC
#endif

/* flags used for rtw_mstat_update() */
enum mstat_f {
	/* type: 0x00ff */
	MSTAT_TYPE_VIR = 0x00,
	MSTAT_TYPE_PHY = 0x01,
	MSTAT_TYPE_SKB = 0x02,
	MSTAT_TYPE_USB = 0x03,
	MSTAT_TYPE_MAX = 0x04,

	/* func: 0xff00 */
	MSTAT_FUNC_UNSPECIFIED = 0x00 << 8,
	MSTAT_FUNC_IO = 0x01 << 8,
	MSTAT_FUNC_TX_IO = 0x02 << 8,
	MSTAT_FUNC_RX_IO = 0x03 << 8,
	MSTAT_FUNC_TX = 0x04 << 8,
	MSTAT_FUNC_RX = 0x05 << 8,
	MSTAT_FUNC_CFG_VENDOR = 0x06 << 8,
	MSTAT_FUNC_MAX = 0x07 << 8,
};

#define mstat_tf_idx(flags) ((flags) & 0xff)
#define mstat_ff_idx(flags) (((flags) & 0xff00) >> 8)

typedef enum mstat_status {
	MSTAT_ALLOC_SUCCESS = 0,
	MSTAT_ALLOC_FAIL,
	MSTAT_FREE
} MSTAT_STATUS;

#ifdef DBG_MEM_ALLOC
void rtw_mstat_update(const enum mstat_f flags, const MSTAT_STATUS status, u32 sz);
void rtw_mstat_dump(void *sel);
u8 *dbg_rtw_vmalloc(u32 sz, const enum mstat_f flags, const char *func, const int line);
u8 *dbg_rtw_zvmalloc(u32 sz, const enum mstat_f flags, const char *func, const int line);
void dbg_rtw_vmfree(u8 *pbuf, const enum mstat_f flags, u32 sz, const char *func, const int line);
u8 *dbg_rtw_malloc(u32 sz, const enum mstat_f flags, const char *func, const int line);
u8 *dbg_rtw_zmalloc(u32 sz, const enum mstat_f flags, const char *func, const int line);
void dbg_rtw_mfree(u8 *pbuf, const enum mstat_f flags, u32 sz, const char *func, const int line);

struct sk_buff *dbg_rtw_skb_alloc(unsigned int size, const enum mstat_f flags, const char *func, const int line);
void dbg_rtw_skb_free(struct sk_buff *skb, const enum mstat_f flags, const char *func, const int line);
struct sk_buff *dbg_rtw_skb_copy(const struct sk_buff *skb, const enum mstat_f flags, const char *func, const int line);
struct sk_buff *dbg_rtw_skb_clone(struct sk_buff *skb, const enum mstat_f flags, const char *func, const int line);
int dbg_rtw_netif_rx(_nic_hdl ndev, struct sk_buff *skb, const enum mstat_f flags, const char *func, int line);
#ifdef CONFIG_RTW_NAPI
int dbg_rtw_netif_receive_skb(_nic_hdl ndev, struct sk_buff *skb, const enum mstat_f flags, const char *func, int line);
#ifdef CONFIG_RTW_GRO
gro_result_t dbg_rtw_napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb, const enum mstat_f flags, const char *func, int line);
#endif
#endif /* CONFIG_RTW_NAPI */
void dbg_rtw_skb_queue_purge(struct sk_buff_head *list, enum mstat_f flags, const char *func, int line);
#ifdef CONFIG_USB_HCI
void *dbg_rtw_usb_buffer_alloc(struct usb_device *dev, size_t size, dma_addr_t *dma, const enum mstat_f flags, const char *func, const int line);
void dbg_rtw_usb_buffer_free(struct usb_device *dev, size_t size, void *addr, dma_addr_t dma, const enum mstat_f flags, const char *func, const int line);
#endif /* CONFIG_USB_HCI */

#ifdef CONFIG_USE_VMALLOC
#define rtw_vmalloc(sz)			dbg_rtw_vmalloc((sz), MSTAT_TYPE_VIR, __FUNCTION__, __LINE__)
#define rtw_zvmalloc(sz)			dbg_rtw_zvmalloc((sz), MSTAT_TYPE_VIR, __FUNCTION__, __LINE__)
#define rtw_vmfree(pbuf, sz)		dbg_rtw_vmfree((pbuf), (sz), MSTAT_TYPE_VIR, __FUNCTION__, __LINE__)
#define rtw_vmalloc_f(sz, mstat_f)			dbg_rtw_vmalloc((sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_VIR, __FUNCTION__, __LINE__)
#define rtw_zvmalloc_f(sz, mstat_f)		dbg_rtw_zvmalloc((sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_VIR, __FUNCTION__, __LINE__)
#define rtw_vmfree_f(pbuf, sz, mstat_f)	dbg_rtw_vmfree((pbuf), (sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_VIR, __FUNCTION__, __LINE__)
#else /* CONFIG_USE_VMALLOC */
#define rtw_vmalloc(sz)			dbg_rtw_malloc((sz), MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_zvmalloc(sz)			dbg_rtw_zmalloc((sz), MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_vmfree(pbuf, sz)		dbg_rtw_mfree((pbuf), (sz), MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_vmalloc_f(sz, mstat_f)			dbg_rtw_malloc((sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_zvmalloc_f(sz, mstat_f)		dbg_rtw_zmalloc((sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_vmfree_f(pbuf, sz, mstat_f)	dbg_rtw_mfree((pbuf), (sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#endif /* CONFIG_USE_VMALLOC */
#define rtw_malloc(sz)			dbg_rtw_malloc((sz), MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_zmalloc(sz)			dbg_rtw_zmalloc((sz), MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_mfree(pbuf, sz)		dbg_rtw_mfree((pbuf), (sz), MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_malloc_f(sz, mstat_f)			dbg_rtw_malloc((sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_zmalloc_f(sz, mstat_f)			dbg_rtw_zmalloc((sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_mfree_f(pbuf, sz, mstat_f)		dbg_rtw_mfree((pbuf), (sz), ((mstat_f) & 0xff00) | MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)

#define rtw_skb_alloc(size)	dbg_rtw_skb_alloc((size), MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_skb_free(skb)	dbg_rtw_skb_free((skb), MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_skb_alloc_f(size, mstat_f)	dbg_rtw_skb_alloc((size), ((mstat_f) & 0xff00) | MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_skb_free_f(skb, mstat_f)	dbg_rtw_skb_free((skb), ((mstat_f) & 0xff00) | MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_skb_copy(skb)	dbg_rtw_skb_copy((skb), MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_skb_clone(skb)	dbg_rtw_skb_clone((skb), MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_skb_copy_f(skb, mstat_f)	dbg_rtw_skb_copy((skb), ((mstat_f) & 0xff00) | MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_skb_clone_f(skb, mstat_f)	dbg_rtw_skb_clone((skb), ((mstat_f) & 0xff00) | MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#define rtw_netif_rx(ndev, skb)	dbg_rtw_netif_rx(ndev, skb, MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#ifdef CONFIG_RTW_NAPI
#define rtw_netif_receive_skb(ndev, skb) dbg_rtw_netif_receive_skb(ndev, skb, MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#ifdef CONFIG_RTW_GRO
#define rtw_napi_gro_receive(napi, skb) dbg_rtw_napi_gro_receive(napi, skb, MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#endif
#endif /* CONFIG_RTW_NAPI */
#define rtw_skb_queue_purge(sk_buff_head) dbg_rtw_skb_queue_purge(sk_buff_head, MSTAT_TYPE_SKB, __FUNCTION__, __LINE__)
#ifdef CONFIG_USB_HCI
#define rtw_usb_buffer_alloc(dev, size, dma)		dbg_rtw_usb_buffer_alloc((dev), (size), (dma), MSTAT_TYPE_USB, __FUNCTION__, __LINE__)
#define rtw_usb_buffer_free(dev, size, addr, dma)	dbg_rtw_usb_buffer_free((dev), (size), (addr), (dma), MSTAT_TYPE_USB, __FUNCTION__, __LINE__)
#define rtw_usb_buffer_alloc_f(dev, size, dma, mstat_f)			dbg_rtw_usb_buffer_alloc((dev), (size), (dma), ((mstat_f) & 0xff00) | MSTAT_TYPE_USB, __FUNCTION__, __LINE__)
#define rtw_usb_buffer_free_f(dev, size, addr, dma, mstat_f)	dbg_rtw_usb_buffer_free((dev), (size), (addr), (dma), ((mstat_f) & 0xff00) | MSTAT_TYPE_USB, __FUNCTION__, __LINE__)
#endif /* CONFIG_USB_HCI */

#else /* DBG_MEM_ALLOC */
#define rtw_mstat_update(flag, status, sz) do {} while (0)
#define rtw_mstat_dump(sel) do {} while (0)
u8 *_rtw_vmalloc(u32 sz);
u8 *_rtw_zvmalloc(u32 sz);
void	_rtw_vmfree(u8 *pbuf, u32 sz);
u8 *_rtw_zmalloc(u32 sz);
u8 *_rtw_malloc(u32 sz);
void	_rtw_mfree(u8 *pbuf, u32 sz);

struct sk_buff *_rtw_skb_alloc(u32 sz);
void _rtw_skb_free(struct sk_buff *skb);
struct sk_buff *_rtw_skb_copy(const struct sk_buff *skb);
struct sk_buff *_rtw_skb_clone(struct sk_buff *skb);
int _rtw_netif_rx(_nic_hdl ndev, struct sk_buff *skb);
#ifdef CONFIG_RTW_NAPI
int _rtw_netif_receive_skb(_nic_hdl ndev, struct sk_buff *skb);
#ifdef CONFIG_RTW_GRO
gro_result_t _rtw_napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb);
#endif
#endif /* CONFIG_RTW_NAPI */
void _rtw_skb_queue_purge(struct sk_buff_head *list);

#ifdef CONFIG_USB_HCI
void *_rtw_usb_buffer_alloc(struct usb_device *dev, size_t size, dma_addr_t *dma);
void _rtw_usb_buffer_free(struct usb_device *dev, size_t size, void *addr, dma_addr_t dma);
#endif /* CONFIG_USB_HCI */

#ifdef CONFIG_USE_VMALLOC
#define rtw_vmalloc(sz)			_rtw_vmalloc((sz))
#define rtw_zvmalloc(sz)			_rtw_zvmalloc((sz))
#define rtw_vmfree(pbuf, sz)		_rtw_vmfree((pbuf), (sz))
#define rtw_vmalloc_f(sz, mstat_f)			_rtw_vmalloc((sz))
#define rtw_zvmalloc_f(sz, mstat_f)		_rtw_zvmalloc((sz))
#define rtw_vmfree_f(pbuf, sz, mstat_f)	_rtw_vmfree((pbuf), (sz))
#else /* CONFIG_USE_VMALLOC */
#define rtw_vmalloc(sz)			_rtw_malloc((sz))
#define rtw_zvmalloc(sz)			_rtw_zmalloc((sz))
#define rtw_vmfree(pbuf, sz)		_rtw_mfree((pbuf), (sz))
#define rtw_vmalloc_f(sz, mstat_f)			_rtw_malloc((sz))
#define rtw_zvmalloc_f(sz, mstat_f)		_rtw_zmalloc((sz))
#define rtw_vmfree_f(pbuf, sz, mstat_f)	_rtw_mfree((pbuf), (sz))
#endif /* CONFIG_USE_VMALLOC */
#define rtw_malloc(sz)			_rtw_malloc((sz))
#define rtw_zmalloc(sz)			_rtw_zmalloc((sz))
#define rtw_mfree(pbuf, sz)		_rtw_mfree((pbuf), (sz))
#define rtw_malloc_f(sz, mstat_f)			_rtw_malloc((sz))
#define rtw_zmalloc_f(sz, mstat_f)			_rtw_zmalloc((sz))
#define rtw_mfree_f(pbuf, sz, mstat_f)		_rtw_mfree((pbuf), (sz))

#define rtw_skb_alloc(size) _rtw_skb_alloc((size))
#define rtw_skb_free(skb) _rtw_skb_free((skb))
#define rtw_skb_alloc_f(size, mstat_f)	_rtw_skb_alloc((size))
#define rtw_skb_free_f(skb, mstat_f)	_rtw_skb_free((skb))
#define rtw_skb_copy(skb)	_rtw_skb_copy((skb))
#define rtw_skb_clone(skb)	_rtw_skb_clone((skb))
#define rtw_skb_copy_f(skb, mstat_f)	_rtw_skb_copy((skb))
#define rtw_skb_clone_f(skb, mstat_f)	_rtw_skb_clone((skb))
#define rtw_netif_rx(ndev, skb) _rtw_netif_rx(ndev, skb)
#ifdef CONFIG_RTW_NAPI
#define rtw_netif_receive_skb(ndev, skb) _rtw_netif_receive_skb(ndev, skb)
#ifdef CONFIG_RTW_GRO
#define rtw_napi_gro_receive(napi, skb) _rtw_napi_gro_receive(napi, skb)
#endif
#endif /* CONFIG_RTW_NAPI */
#define rtw_skb_queue_purge(sk_buff_head) _rtw_skb_queue_purge(sk_buff_head)
#ifdef CONFIG_USB_HCI
#define rtw_usb_buffer_alloc(dev, size, dma) _rtw_usb_buffer_alloc((dev), (size), (dma))
#define rtw_usb_buffer_free(dev, size, addr, dma) _rtw_usb_buffer_free((dev), (size), (addr), (dma))
#define rtw_usb_buffer_alloc_f(dev, size, dma, mstat_f) _rtw_usb_buffer_alloc((dev), (size), (dma))
#define rtw_usb_buffer_free_f(dev, size, addr, dma, mstat_f) _rtw_usb_buffer_free((dev), (size), (addr), (dma))
#endif /* CONFIG_USB_HCI */
#endif /* DBG_MEM_ALLOC */

extern void	*rtw_malloc2d(int h, int w, size_t size);
extern void	rtw_mfree2d(void *pbuf, int h, int w, int size);

extern void	_rtw_memcpy(void *dec, const void *sour, u32 sz);
extern void _rtw_memmove(void *dst, const void *src, u32 sz);
extern int	_rtw_memcmp(const void *dst, const void *src, u32 sz);
extern void	_rtw_memset(void *pbuf, int c, u32 sz);

extern void	_rtw_init_listhead(_list *list);
extern u32	rtw_is_list_empty(_list *phead);
extern void	rtw_list_insert_head(_list *plist, _list *phead);
extern void	rtw_list_insert_tail(_list *plist, _list *phead);
#ifndef PLATFORM_FREEBSD
extern void	rtw_list_delete(_list *plist);
#endif /* PLATFORM_FREEBSD */

extern void	_rtw_init_sema(_sema *sema, int init_val);
extern void	_rtw_free_sema(_sema	*sema);
extern void	_rtw_up_sema(_sema	*sema);
extern u32	_rtw_down_sema(_sema *sema);
extern void	_rtw_mutex_init(_mutex *pmutex);
extern void	_rtw_mutex_free(_mutex *pmutex);
#ifndef PLATFORM_FREEBSD
extern void	_rtw_spinlock_init(_lock *plock);
#endif /* PLATFORM_FREEBSD */
extern void	_rtw_spinlock_free(_lock *plock);
extern void	_rtw_spinlock(_lock	*plock);
extern void	_rtw_spinunlock(_lock	*plock);
extern void	_rtw_spinlock_ex(_lock	*plock);
extern void	_rtw_spinunlock_ex(_lock	*plock);

extern void	_rtw_init_queue(_queue *pqueue);
extern void _rtw_deinit_queue(_queue *pqueue);
extern u32	_rtw_queue_empty(_queue	*pqueue);
extern u32	rtw_end_of_queue_search(_list *queue, _list *pelement);

extern u32	rtw_get_current_time(void);
extern u32	rtw_systime_to_ms(u32 systime);
extern u32	rtw_ms_to_systime(u32 ms);
extern s32	rtw_get_passing_time_ms(u32 start);
extern s32	rtw_get_time_interval_ms(u32 start, u32 end);

extern void	rtw_sleep_schedulable(int ms);

extern void	rtw_msleep_os(int ms);
extern void	rtw_usleep_os(int us);

extern u32	rtw_atoi(u8 *s);

#ifdef DBG_DELAY_OS
#define rtw_mdelay_os(ms) _rtw_mdelay_os((ms), __FUNCTION__, __LINE__)
#define rtw_udelay_os(ms) _rtw_udelay_os((ms), __FUNCTION__, __LINE__)
extern void _rtw_mdelay_os(int ms, const char *func, const int line);
extern void _rtw_udelay_os(int us, const char *func, const int line);
#else
extern void	rtw_mdelay_os(int ms);
extern void	rtw_udelay_os(int us);
#endif

extern void rtw_yield_os(void);


extern void rtw_init_timer(_timer *ptimer, void *padapter, void *pfunc);


__inline static unsigned char _cancel_timer_ex(_timer *ptimer)
{
#ifdef PLATFORM_LINUX
	return del_timer_sync(ptimer);
#endif
#ifdef PLATFORM_FREEBSD
	_cancel_timer(ptimer, 0);
	return 0;
#endif
#ifdef PLATFORM_WINDOWS
	u8 bcancelled;

	_cancel_timer(ptimer, &bcancelled);

	return bcancelled;
#endif
}

static __inline void thread_enter(char *name)
{
#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
	daemonize("%s", name);
#endif
	allow_signal(SIGTERM);
#endif
#ifdef PLATFORM_FREEBSD
	printf("%s", "RTKTHREAD_enter");
#endif
}

__inline static void flush_signals_thread(void)
{
#ifdef PLATFORM_LINUX
	if (signal_pending(current))
		flush_signals(current);
#endif
}

__inline static _OS_STATUS res_to_status(sint res)
{

#if defined(PLATFORM_LINUX) || defined (PLATFORM_MPIXEL) || defined (PLATFORM_FREEBSD)
	return res;
#endif

#ifdef PLATFORM_WINDOWS

	if (res == _SUCCESS)
		return NDIS_STATUS_SUCCESS;
	else
		return NDIS_STATUS_FAILURE;

#endif

}

__inline static void rtw_dump_stack(void)
{
#ifdef PLATFORM_LINUX
	dump_stack();
#endif
}

#ifdef PLATFORM_LINUX
#define rtw_warn_on(condition) WARN_ON(condition)
#else
#define rtw_warn_on(condition) do {} while (0)
#endif

__inline static int rtw_bug_check(void *parg1, void *parg2, void *parg3, void *parg4)
{
	int ret = _TRUE;

#ifdef PLATFORM_WINDOWS
	if (((uint)parg1) <= 0x7fffffff ||
	    ((uint)parg2) <= 0x7fffffff ||
	    ((uint)parg3) <= 0x7fffffff ||
	    ((uint)parg4) <= 0x7fffffff) {
		ret = _FALSE;
		KeBugCheckEx(0x87110000, (ULONG_PTR)parg1, (ULONG_PTR)parg2, (ULONG_PTR)parg3, (ULONG_PTR)parg4);
	}
#endif

	return ret;

}

#define _RND(sz, r) ((((sz)+((r)-1))/(r))*(r))
#define RND4(x)	(((x >> 2) + (((x & 3) == 0) ? 0 : 1)) << 2)

__inline static u32 _RND4(u32 sz)
{

	u32	val;

	val = ((sz >> 2) + ((sz & 3) ? 1 : 0)) << 2;

	return val;

}

__inline static u32 _RND8(u32 sz)
{

	u32	val;

	val = ((sz >> 3) + ((sz & 7) ? 1 : 0)) << 3;

	return val;

}

__inline static u32 _RND128(u32 sz)
{

	u32	val;

	val = ((sz >> 7) + ((sz & 127) ? 1 : 0)) << 7;

	return val;

}

__inline static u32 _RND256(u32 sz)
{

	u32	val;

	val = ((sz >> 8) + ((sz & 255) ? 1 : 0)) << 8;

	return val;

}

__inline static u32 _RND512(u32 sz)
{

	u32	val;

	val = ((sz >> 9) + ((sz & 511) ? 1 : 0)) << 9;

	return val;

}

__inline static u32 bitshift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++)
		if (((bitmask >> i) &  0x1) == 1)
			break;

	return i;
}

static inline int largest_bit(u32 bitmask)
{
	int i;

	for (i = 31; i >= 0; i--)
		if (bitmask & BIT(i))
			break;

	return i;
}

#define rtw_min(a, b) ((a > b) ? b : a)
#define rtw_is_range_a_in_b(hi_a, lo_a, hi_b, lo_b) (((hi_a) <= (hi_b)) && ((lo_a) >= (lo_b)))
#define rtw_is_range_overlap(hi_a, lo_a, hi_b, lo_b) (((hi_a) > (lo_b)) && ((lo_a) < (hi_b)))

#ifndef MAC_FMT
#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef MAC_ARG
#define MAC_ARG(x) ((u8 *)(x))[0], ((u8 *)(x))[1], ((u8 *)(x))[2], ((u8 *)(x))[3], ((u8 *)(x))[4], ((u8 *)(x))[5]
#endif


extern void rtw_suspend_lock_init(void);
extern void rtw_suspend_lock_uninit(void);
extern void rtw_lock_suspend(void);
extern void rtw_unlock_suspend(void);
extern void rtw_lock_suspend_timeout(u32 timeout_ms);
extern void rtw_lock_ext_suspend_timeout(u32 timeout_ms);
extern void rtw_lock_rx_suspend_timeout(u32 timeout_ms);
extern void rtw_lock_traffic_suspend_timeout(u32 timeout_ms);
extern void rtw_lock_resume_scan_timeout(u32 timeout_ms);
extern void rtw_resume_lock_suspend(void);
extern void rtw_resume_unlock_suspend(void);
#ifdef CONFIG_AP_WOWLAN
extern void rtw_softap_lock_suspend(void);
extern void rtw_softap_unlock_suspend(void);
#endif

extern void ATOMIC_SET(ATOMIC_T *v, int i);
extern int ATOMIC_READ(ATOMIC_T *v);
extern void ATOMIC_ADD(ATOMIC_T *v, int i);
extern void ATOMIC_SUB(ATOMIC_T *v, int i);
extern void ATOMIC_INC(ATOMIC_T *v);
extern void ATOMIC_DEC(ATOMIC_T *v);
extern int ATOMIC_ADD_RETURN(ATOMIC_T *v, int i);
extern int ATOMIC_SUB_RETURN(ATOMIC_T *v, int i);
extern int ATOMIC_INC_RETURN(ATOMIC_T *v);
extern int ATOMIC_DEC_RETURN(ATOMIC_T *v);

/* File operation APIs, just for linux now */
extern int rtw_is_file_readable(const char *path);
extern int rtw_is_file_readable_with_size(const char *path, u32 *sz);
extern int rtw_retrieve_from_file(const char *path, u8 *buf, u32 sz);
extern int rtw_store_to_file(const char *path, u8 *buf, u32 sz);


#ifndef PLATFORM_FREEBSD
extern void rtw_free_netdev(struct net_device *netdev);
#endif /* PLATFORM_FREEBSD */


extern u64 rtw_modular64(u64 x, u64 y);
extern u64 rtw_division64(u64 x, u64 y);
extern u32 rtw_random32(void);

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
void rtw_cbuf_free(struct rtw_cbuf *cbuf);

struct map_seg_t {
	u16 sa;
	u16 len;
	u8 *c;
};

struct map_t {
	u16 len;
	u16 seg_num;
	u8 init_value;
	struct map_seg_t *segs;
};

#define MAPSEG_ARRAY_ENT(_sa, _len, _c, arg...) \
	{ .sa = _sa, .len = _len, .c = (u8[_len]){ _c, ##arg}, }

#define MAPSEG_PTR_ENT(_sa, _len, _p) \
	{ .sa = _sa, .len = _len, .c = _p, }

#define MAP_ENT(_len, _seg_num, _init_v, _seg, arg...) \
	{ .len = _len, .seg_num = _seg_num, .init_value = _init_v, .segs = (struct map_seg_t[_seg_num]){ _seg, ##arg}, }

int map_readN(const struct map_t *map, u16 offset, u16 len, u8 *buf);
u8 map_read8(const struct map_t *map, u16 offset);

/* String handler */

BOOLEAN is_null(char c);
BOOLEAN is_eol(char c);
BOOLEAN is_space(char c);
BOOLEAN IsHexDigit(char chTmp);
BOOLEAN is_alpha(char chTmp);
char alpha_to_upper(char c);

/*
 * Write formatted output to sized buffer
 */
#ifdef PLATFORM_LINUX
#define rtw_sprintf(buf, size, format, arg...)	snprintf(buf, size, format, ##arg)
#else /* !PLATFORM_LINUX */
#error "NOT DEFINE \"rtw_sprintf\"!!"
#endif /* !PLATFORM_LINUX */

#endif
