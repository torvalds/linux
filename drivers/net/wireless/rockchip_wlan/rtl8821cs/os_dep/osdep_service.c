/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/


#define _OSDEP_SERVICE_C_

#include <drv_types.h>

#define RT_TAG	'1178'

#ifdef DBG_MEMORY_LEAK
#ifdef PLATFORM_LINUX
atomic_t _malloc_cnt = ATOMIC_INIT(0);
atomic_t _malloc_size = ATOMIC_INIT(0);
#endif
#endif /* DBG_MEMORY_LEAK */


#if defined(PLATFORM_LINUX)
/*
* Translate the OS dependent @param error_code to OS independent RTW_STATUS_CODE
* @return: one of RTW_STATUS_CODE
*/
inline int RTW_STATUS_CODE(int error_code)
{
	if (error_code >= 0)
		return _SUCCESS;

	switch (error_code) {
	/* case -ETIMEDOUT: */
	/*	return RTW_STATUS_TIMEDOUT; */
	default:
		return _FAIL;
	}
}
#else
inline int RTW_STATUS_CODE(int error_code)
{
	return error_code;
}
#endif

u32 rtw_atoi(u8 *s)
{

	int num = 0, flag = 0;
	int i;
	for (i = 0; i <= strlen(s); i++) {
		if (s[i] >= '0' && s[i] <= '9')
			num = num * 10 + s[i] - '0';
		else if (s[0] == '-' && i == 0)
			flag = 1;
		else
			break;
	}

	if (flag == 1)
		num = num * -1;

	return num;

}

inline void *_rtw_vmalloc(u32 sz)
{
	void *pbuf;
#ifdef PLATFORM_LINUX
	pbuf = vmalloc(sz);
#endif
#ifdef PLATFORM_FREEBSD
	pbuf = malloc(sz, M_DEVBUF, M_NOWAIT);
#endif

#ifdef PLATFORM_WINDOWS
	NdisAllocateMemoryWithTag(&pbuf, sz, RT_TAG);
#endif

#ifdef DBG_MEMORY_LEAK
#ifdef PLATFORM_LINUX
	if (pbuf != NULL) {
		atomic_inc(&_malloc_cnt);
		atomic_add(sz, &_malloc_size);
	}
#endif
#endif /* DBG_MEMORY_LEAK */

	return pbuf;
}

inline void *_rtw_zvmalloc(u32 sz)
{
	void *pbuf;
#ifdef PLATFORM_LINUX
	pbuf = _rtw_vmalloc(sz);
	if (pbuf != NULL)
		memset(pbuf, 0, sz);
#endif
#ifdef PLATFORM_FREEBSD
	pbuf = malloc(sz, M_DEVBUF, M_ZERO | M_NOWAIT);
#endif
#ifdef PLATFORM_WINDOWS
	NdisAllocateMemoryWithTag(&pbuf, sz, RT_TAG);
	if (pbuf != NULL)
		NdisFillMemory(pbuf, sz, 0);
#endif

	return pbuf;
}

inline void _rtw_vmfree(void *pbuf, u32 sz)
{
#ifdef PLATFORM_LINUX
	vfree(pbuf);
#endif
#ifdef PLATFORM_FREEBSD
	free(pbuf, M_DEVBUF);
#endif
#ifdef PLATFORM_WINDOWS
	NdisFreeMemory(pbuf, sz, 0);
#endif

#ifdef DBG_MEMORY_LEAK
#ifdef PLATFORM_LINUX
	atomic_dec(&_malloc_cnt);
	atomic_sub(sz, &_malloc_size);
#endif
#endif /* DBG_MEMORY_LEAK */
}

void *_rtw_malloc(u32 sz)
{
	void *pbuf = NULL;

#ifdef PLATFORM_LINUX
#ifdef RTK_DMP_PLATFORM
	if (sz > 0x4000)
		pbuf = dvr_malloc(sz);
	else
#endif
		pbuf = kmalloc(sz, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);

#endif
#ifdef PLATFORM_FREEBSD
	pbuf = malloc(sz, M_DEVBUF, M_NOWAIT);
#endif
#ifdef PLATFORM_WINDOWS

	NdisAllocateMemoryWithTag(&pbuf, sz, RT_TAG);

#endif

#ifdef DBG_MEMORY_LEAK
#ifdef PLATFORM_LINUX
	if (pbuf != NULL) {
		atomic_inc(&_malloc_cnt);
		atomic_add(sz, &_malloc_size);
	}
#endif
#endif /* DBG_MEMORY_LEAK */

	return pbuf;

}


void *_rtw_zmalloc(u32 sz)
{
#ifdef PLATFORM_FREEBSD
	return malloc(sz, M_DEVBUF, M_ZERO | M_NOWAIT);
#else /* PLATFORM_FREEBSD */
	void *pbuf = _rtw_malloc(sz);

	if (pbuf != NULL) {

#ifdef PLATFORM_LINUX
		memset(pbuf, 0, sz);
#endif

#ifdef PLATFORM_WINDOWS
		NdisFillMemory(pbuf, sz, 0);
#endif

	}

	return pbuf;
#endif /* PLATFORM_FREEBSD */
}

void _rtw_mfree(void *pbuf, u32 sz)
{

#ifdef PLATFORM_LINUX
#ifdef RTK_DMP_PLATFORM
	if (sz > 0x4000)
		dvr_free(pbuf);
	else
#endif
		kfree(pbuf);

#endif
#ifdef PLATFORM_FREEBSD
	free(pbuf, M_DEVBUF);
#endif
#ifdef PLATFORM_WINDOWS

	NdisFreeMemory(pbuf, sz, 0);

#endif

#ifdef DBG_MEMORY_LEAK
#ifdef PLATFORM_LINUX
	atomic_dec(&_malloc_cnt);
	atomic_sub(sz, &_malloc_size);
#endif
#endif /* DBG_MEMORY_LEAK */

}

#ifdef PLATFORM_FREEBSD
/* review again */
struct sk_buff *dev_alloc_skb(unsigned int size)
{
	struct sk_buff *skb = NULL;
	u8 *data = NULL;

	/* skb = _rtw_zmalloc(sizeof(struct sk_buff)); */ /* for skb->len, etc. */
	skb = _rtw_malloc(sizeof(struct sk_buff));
	if (!skb)
		goto out;
	data = _rtw_malloc(size);
	if (!data)
		goto nodata;

	skb->head = (unsigned char *)data;
	skb->data = (unsigned char *)data;
	skb->tail = (unsigned char *)data;
	skb->end = (unsigned char *)data + size;
	skb->len = 0;
	/* printf("%s()-%d: skb=%p, skb->head = %p\n", __FUNCTION__, __LINE__, skb, skb->head); */

out:
	return skb;
nodata:
	_rtw_mfree(skb, sizeof(struct sk_buff));
	skb = NULL;
	goto out;

}

void dev_kfree_skb_any(struct sk_buff *skb)
{
	/* printf("%s()-%d: skb->head = %p\n", __FUNCTION__, __LINE__, skb->head); */
	if (skb->head)
		_rtw_mfree(skb->head, 0);
	/* printf("%s()-%d: skb = %p\n", __FUNCTION__, __LINE__, skb); */
	if (skb)
		_rtw_mfree(skb, 0);
}
struct sk_buff *skb_clone(const struct sk_buff *skb)
{
	return NULL;
}

#endif /* PLATFORM_FREEBSD */

inline struct sk_buff *_rtw_skb_alloc(u32 sz)
{
#ifdef PLATFORM_LINUX
	return __dev_alloc_skb(sz, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
	return dev_alloc_skb(sz);
#endif /* PLATFORM_FREEBSD */
}

inline void _rtw_skb_free(struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}

inline struct sk_buff *_rtw_skb_copy(const struct sk_buff *skb)
{
#ifdef PLATFORM_LINUX
	return skb_copy(skb, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
	return NULL;
#endif /* PLATFORM_FREEBSD */
}

inline struct sk_buff *_rtw_skb_clone(struct sk_buff *skb)
{
#ifdef PLATFORM_LINUX
	return skb_clone(skb, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
	return skb_clone(skb);
#endif /* PLATFORM_FREEBSD */
}
inline struct sk_buff *_rtw_pskb_copy(struct sk_buff *skb)
{
#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
	return pskb_copy(skb, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
#else
	return skb_clone(skb, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
#endif
#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
	return NULL;
#endif /* PLATFORM_FREEBSD */
}

inline int _rtw_netif_rx(_nic_hdl ndev, struct sk_buff *skb)
{
#if defined(PLATFORM_LINUX)
	skb->dev = ndev;
	return netif_rx(skb);
#elif defined(PLATFORM_FREEBSD)
	return (*ndev->if_input)(ndev, skb);
#else
	rtw_warn_on(1);
	return -1;
#endif
}

#ifdef CONFIG_RTW_NAPI
inline int _rtw_netif_receive_skb(_nic_hdl ndev, struct sk_buff *skb)
{
#if defined(PLATFORM_LINUX)
	skb->dev = ndev;
	return netif_receive_skb(skb);
#else
	rtw_warn_on(1);
	return -1;
#endif
}

#ifdef CONFIG_RTW_GRO
inline gro_result_t _rtw_napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
#if defined(PLATFORM_LINUX)
	return napi_gro_receive(napi, skb);
#else
	rtw_warn_on(1);
	return -1;
#endif
}
#endif /* CONFIG_RTW_GRO */
#endif /* CONFIG_RTW_NAPI */

void _rtw_skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(list)) != NULL)
		_rtw_skb_free(skb);
}

#ifdef CONFIG_USB_HCI
inline void *_rtw_usb_buffer_alloc(struct usb_device *dev, size_t size, dma_addr_t *dma)
{
#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	return usb_alloc_coherent(dev, size, (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL), dma);
#else
	return usb_buffer_alloc(dev, size, (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL), dma);
#endif
#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
	return malloc(size, M_USBDEV, M_NOWAIT | M_ZERO);
#endif /* PLATFORM_FREEBSD */
}
inline void _rtw_usb_buffer_free(struct usb_device *dev, size_t size, void *addr, dma_addr_t dma)
{
#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	usb_free_coherent(dev, size, addr, dma);
#else
	usb_buffer_free(dev, size, addr, dma);
#endif
#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
	free(addr, M_USBDEV);
#endif /* PLATFORM_FREEBSD */
}
#endif /* CONFIG_USB_HCI */

#if defined(DBG_MEM_ALLOC)

struct rtw_mem_stat {
	ATOMIC_T alloc; /* the memory bytes we allocate currently */
	ATOMIC_T peak; /* the peak memory bytes we allocate */
	ATOMIC_T alloc_cnt; /* the alloc count for alloc currently */
	ATOMIC_T alloc_err_cnt; /* the error times we fail to allocate memory */
};

struct rtw_mem_stat rtw_mem_type_stat[mstat_tf_idx(MSTAT_TYPE_MAX)];
#ifdef RTW_MEM_FUNC_STAT
struct rtw_mem_stat rtw_mem_func_stat[mstat_ff_idx(MSTAT_FUNC_MAX)];
#endif

char *MSTAT_TYPE_str[] = {
	"VIR",
	"PHY",
	"SKB",
	"USB",
};

#ifdef RTW_MEM_FUNC_STAT
char *MSTAT_FUNC_str[] = {
	"UNSP",
	"IO",
	"TXIO",
	"RXIO",
	"TX",
	"RX",
};
#endif

void rtw_mstat_dump(void *sel)
{
	int i;
	int value_t[4][mstat_tf_idx(MSTAT_TYPE_MAX)];
#ifdef RTW_MEM_FUNC_STAT
	int value_f[4][mstat_ff_idx(MSTAT_FUNC_MAX)];
#endif

	for (i = 0; i < mstat_tf_idx(MSTAT_TYPE_MAX); i++) {
		value_t[0][i] = ATOMIC_READ(&(rtw_mem_type_stat[i].alloc));
		value_t[1][i] = ATOMIC_READ(&(rtw_mem_type_stat[i].peak));
		value_t[2][i] = ATOMIC_READ(&(rtw_mem_type_stat[i].alloc_cnt));
		value_t[3][i] = ATOMIC_READ(&(rtw_mem_type_stat[i].alloc_err_cnt));
	}

#ifdef RTW_MEM_FUNC_STAT
	for (i = 0; i < mstat_ff_idx(MSTAT_FUNC_MAX); i++) {
		value_f[0][i] = ATOMIC_READ(&(rtw_mem_func_stat[i].alloc));
		value_f[1][i] = ATOMIC_READ(&(rtw_mem_func_stat[i].peak));
		value_f[2][i] = ATOMIC_READ(&(rtw_mem_func_stat[i].alloc_cnt));
		value_f[3][i] = ATOMIC_READ(&(rtw_mem_func_stat[i].alloc_err_cnt));
	}
#endif

	RTW_PRINT_SEL(sel, "===================== MSTAT =====================\n");
	RTW_PRINT_SEL(sel, "%4s %10s %10s %10s %10s\n", "TAG", "alloc", "peak", "aloc_cnt", "err_cnt");
	RTW_PRINT_SEL(sel, "-------------------------------------------------\n");
	for (i = 0; i < mstat_tf_idx(MSTAT_TYPE_MAX); i++)
		RTW_PRINT_SEL(sel, "%4s %10d %10d %10d %10d\n", MSTAT_TYPE_str[i], value_t[0][i], value_t[1][i], value_t[2][i], value_t[3][i]);
#ifdef RTW_MEM_FUNC_STAT
	RTW_PRINT_SEL(sel, "-------------------------------------------------\n");
	for (i = 0; i < mstat_ff_idx(MSTAT_FUNC_MAX); i++)
		RTW_PRINT_SEL(sel, "%4s %10d %10d %10d %10d\n", MSTAT_FUNC_str[i], value_f[0][i], value_f[1][i], value_f[2][i], value_f[3][i]);
#endif
}

void rtw_mstat_update(const enum mstat_f flags, const MSTAT_STATUS status, u32 sz)
{
	static systime update_time = 0;
	int peak, alloc;
	int i;

	/* initialization */
	if (!update_time) {
		for (i = 0; i < mstat_tf_idx(MSTAT_TYPE_MAX); i++) {
			ATOMIC_SET(&(rtw_mem_type_stat[i].alloc), 0);
			ATOMIC_SET(&(rtw_mem_type_stat[i].peak), 0);
			ATOMIC_SET(&(rtw_mem_type_stat[i].alloc_cnt), 0);
			ATOMIC_SET(&(rtw_mem_type_stat[i].alloc_err_cnt), 0);
		}
		#ifdef RTW_MEM_FUNC_STAT
		for (i = 0; i < mstat_ff_idx(MSTAT_FUNC_MAX); i++) {
			ATOMIC_SET(&(rtw_mem_func_stat[i].alloc), 0);
			ATOMIC_SET(&(rtw_mem_func_stat[i].peak), 0);
			ATOMIC_SET(&(rtw_mem_func_stat[i].alloc_cnt), 0);
			ATOMIC_SET(&(rtw_mem_func_stat[i].alloc_err_cnt), 0);
		}
		#endif
	}

	switch (status) {
	case MSTAT_ALLOC_SUCCESS:
		ATOMIC_INC(&(rtw_mem_type_stat[mstat_tf_idx(flags)].alloc_cnt));
		alloc = ATOMIC_ADD_RETURN(&(rtw_mem_type_stat[mstat_tf_idx(flags)].alloc), sz);
		peak = ATOMIC_READ(&(rtw_mem_type_stat[mstat_tf_idx(flags)].peak));
		if (peak < alloc)
			ATOMIC_SET(&(rtw_mem_type_stat[mstat_tf_idx(flags)].peak), alloc);

		#ifdef RTW_MEM_FUNC_STAT
		ATOMIC_INC(&(rtw_mem_func_stat[mstat_ff_idx(flags)].alloc_cnt));
		alloc = ATOMIC_ADD_RETURN(&(rtw_mem_func_stat[mstat_ff_idx(flags)].alloc), sz);
		peak = ATOMIC_READ(&(rtw_mem_func_stat[mstat_ff_idx(flags)].peak));
		if (peak < alloc)
			ATOMIC_SET(&(rtw_mem_func_stat[mstat_ff_idx(flags)].peak), alloc);
		#endif
		break;

	case MSTAT_ALLOC_FAIL:
		ATOMIC_INC(&(rtw_mem_type_stat[mstat_tf_idx(flags)].alloc_err_cnt));
		#ifdef RTW_MEM_FUNC_STAT
		ATOMIC_INC(&(rtw_mem_func_stat[mstat_ff_idx(flags)].alloc_err_cnt));
		#endif
		break;

	case MSTAT_FREE:
		ATOMIC_DEC(&(rtw_mem_type_stat[mstat_tf_idx(flags)].alloc_cnt));
		ATOMIC_SUB(&(rtw_mem_type_stat[mstat_tf_idx(flags)].alloc), sz);
		#ifdef RTW_MEM_FUNC_STAT
		ATOMIC_DEC(&(rtw_mem_func_stat[mstat_ff_idx(flags)].alloc_cnt));
		ATOMIC_SUB(&(rtw_mem_func_stat[mstat_ff_idx(flags)].alloc), sz);
		#endif
		break;
	};

	/* if (rtw_get_passing_time_ms(update_time) > 5000) { */
	/*	rtw_mstat_dump(RTW_DBGDUMP); */
	update_time = rtw_get_current_time();
	/* } */
}

#ifndef SIZE_MAX
	#define SIZE_MAX (~(size_t)0)
#endif

struct mstat_sniff_rule {
	enum mstat_f flags;
	size_t lb;
	size_t hb;
};

struct mstat_sniff_rule mstat_sniff_rules[] = {
	{MSTAT_TYPE_PHY, 4097, SIZE_MAX},
};

int mstat_sniff_rule_num = sizeof(mstat_sniff_rules) / sizeof(struct mstat_sniff_rule);

bool match_mstat_sniff_rules(const enum mstat_f flags, const size_t size)
{
	int i;
	for (i = 0; i < mstat_sniff_rule_num; i++) {
		if (mstat_sniff_rules[i].flags == flags
			&& mstat_sniff_rules[i].lb <= size
			&& mstat_sniff_rules[i].hb >= size)
			return _TRUE;
	}

	return _FALSE;
}

inline void *dbg_rtw_vmalloc(u32 sz, const enum mstat_f flags, const char *func, const int line)
{
	void *p;

	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%d)\n", func, line, __FUNCTION__, (sz));

	p = _rtw_vmalloc((sz));

	rtw_mstat_update(
		flags
		, p ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, sz
	);

	return p;
}

inline void *dbg_rtw_zvmalloc(u32 sz, const enum mstat_f flags, const char *func, const int line)
{
	void *p;

	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%d)\n", func, line, __FUNCTION__, (sz));

	p = _rtw_zvmalloc((sz));

	rtw_mstat_update(
		flags
		, p ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, sz
	);

	return p;
}

inline void dbg_rtw_vmfree(void *pbuf, u32 sz, const enum mstat_f flags, const char *func, const int line)
{

	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%d)\n", func, line, __FUNCTION__, (sz));

	_rtw_vmfree((pbuf), (sz));

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, sz
	);
}

inline void *dbg_rtw_malloc(u32 sz, const enum mstat_f flags, const char *func, const int line)
{
	void *p;

	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%d)\n", func, line, __FUNCTION__, (sz));

	p = _rtw_malloc((sz));

	rtw_mstat_update(
		flags
		, p ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, sz
	);

	return p;
}

inline void *dbg_rtw_zmalloc(u32 sz, const enum mstat_f flags, const char *func, const int line)
{
	void *p;

	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%d)\n", func, line, __FUNCTION__, (sz));

	p = _rtw_zmalloc((sz));

	rtw_mstat_update(
		flags
		, p ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, sz
	);

	return p;
}

inline void dbg_rtw_mfree(void *pbuf, u32 sz, const enum mstat_f flags, const char *func, const int line)
{
	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%d)\n", func, line, __FUNCTION__, (sz));

	_rtw_mfree((pbuf), (sz));

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, sz
	);
}

inline struct sk_buff *dbg_rtw_skb_alloc(unsigned int size, const enum mstat_f flags, const char *func, int line)
{
	struct sk_buff *skb;
	unsigned int truesize = 0;

	skb = _rtw_skb_alloc(size);

	if (skb)
		truesize = skb->truesize;

	if (!skb || truesize < size || match_mstat_sniff_rules(flags, truesize))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%d), skb:%p, truesize=%u\n", func, line, __FUNCTION__, size, skb, truesize);

	rtw_mstat_update(
		flags
		, skb ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, truesize
	);

	return skb;
}

inline void dbg_rtw_skb_free(struct sk_buff *skb, const enum mstat_f flags, const char *func, int line)
{
	unsigned int truesize = skb->truesize;

	if (match_mstat_sniff_rules(flags, truesize))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s, truesize=%u\n", func, line, __FUNCTION__, truesize);

	_rtw_skb_free(skb);

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, truesize
	);
}

inline struct sk_buff *dbg_rtw_skb_copy(const struct sk_buff *skb, const enum mstat_f flags, const char *func, const int line)
{
	struct sk_buff *skb_cp;
	unsigned int truesize = skb->truesize;
	unsigned int cp_truesize = 0;

	skb_cp = _rtw_skb_copy(skb);
	if (skb_cp)
		cp_truesize = skb_cp->truesize;

	if (!skb_cp || cp_truesize < truesize || match_mstat_sniff_rules(flags, cp_truesize))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%u), skb_cp:%p, cp_truesize=%u\n", func, line, __FUNCTION__, truesize, skb_cp, cp_truesize);

	rtw_mstat_update(
		flags
		, skb_cp ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, cp_truesize
	);

	return skb_cp;
}

inline struct sk_buff *dbg_rtw_skb_clone(struct sk_buff *skb, const enum mstat_f flags, const char *func, const int line)
{
	struct sk_buff *skb_cl;
	unsigned int truesize = skb->truesize;
	unsigned int cl_truesize = 0;

	skb_cl = _rtw_skb_clone(skb);
	if (skb_cl)
		cl_truesize = skb_cl->truesize;

	if (!skb_cl || cl_truesize < truesize || match_mstat_sniff_rules(flags, cl_truesize))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%u), skb_cl:%p, cl_truesize=%u\n", func, line, __FUNCTION__, truesize, skb_cl, cl_truesize);

	rtw_mstat_update(
		flags
		, skb_cl ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, cl_truesize
	);

	return skb_cl;
}

inline int dbg_rtw_netif_rx(_nic_hdl ndev, struct sk_buff *skb, const enum mstat_f flags, const char *func, int line)
{
	int ret;
	unsigned int truesize = skb->truesize;

	if (match_mstat_sniff_rules(flags, truesize))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s, truesize=%u\n", func, line, __FUNCTION__, truesize);

	ret = _rtw_netif_rx(ndev, skb);

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, truesize
	);

	return ret;
}

#ifdef CONFIG_RTW_NAPI
inline int dbg_rtw_netif_receive_skb(_nic_hdl ndev, struct sk_buff *skb, const enum mstat_f flags, const char *func, int line)
{
	int ret;
	unsigned int truesize = skb->truesize;

	if (match_mstat_sniff_rules(flags, truesize))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s, truesize=%u\n", func, line, __FUNCTION__, truesize);

	ret = _rtw_netif_receive_skb(ndev, skb);

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, truesize
	);

	return ret;
}

#ifdef CONFIG_RTW_GRO
inline gro_result_t dbg_rtw_napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb, const enum mstat_f flags, const char *func, int line)
{
	int ret;
	unsigned int truesize = skb->truesize;

	if (match_mstat_sniff_rules(flags, truesize))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s, truesize=%u\n", func, line, __FUNCTION__, truesize);

	ret = _rtw_napi_gro_receive(napi, skb);

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, truesize
	);

	return ret;
}
#endif /* CONFIG_RTW_GRO */
#endif /* CONFIG_RTW_NAPI */

inline void dbg_rtw_skb_queue_purge(struct sk_buff_head *list, enum mstat_f flags, const char *func, int line)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(list)) != NULL)
		dbg_rtw_skb_free(skb, flags, func, line);
}

#ifdef CONFIG_USB_HCI
inline void *dbg_rtw_usb_buffer_alloc(struct usb_device *dev, size_t size, dma_addr_t *dma, const enum mstat_f flags, const char *func, int line)
{
	void *p;

	if (match_mstat_sniff_rules(flags, size))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%zu)\n", func, line, __FUNCTION__, size);

	p = _rtw_usb_buffer_alloc(dev, size, dma);

	rtw_mstat_update(
		flags
		, p ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, size
	);

	return p;
}

inline void dbg_rtw_usb_buffer_free(struct usb_device *dev, size_t size, void *addr, dma_addr_t dma, const enum mstat_f flags, const char *func, int line)
{

	if (match_mstat_sniff_rules(flags, size))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%zu)\n", func, line, __FUNCTION__, size);

	_rtw_usb_buffer_free(dev, size, addr, dma);

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, size
	);
}
#endif /* CONFIG_USB_HCI */

#endif /* defined(DBG_MEM_ALLOC) */

void *rtw_malloc2d(int h, int w, size_t size)
{
	int j;

	void **a = (void **) rtw_zmalloc(h * sizeof(void *) + h * w * size);
	if (a == NULL) {
		RTW_INFO("%s: alloc memory fail!\n", __FUNCTION__);
		return NULL;
	}

	for (j = 0; j < h; j++)
		a[j] = ((char *)(a + h)) + j * w * size;

	return a;
}

void rtw_mfree2d(void *pbuf, int h, int w, int size)
{
	rtw_mfree((u8 *)pbuf, h * sizeof(void *) + w * h * size);
}

inline void rtw_os_pkt_free(_pkt *pkt)
{
#if defined(PLATFORM_LINUX)
	rtw_skb_free(pkt);
#elif defined(PLATFORM_FREEBSD)
	m_freem(pkt);
#else
	#error "TBD\n"
#endif
}

inline _pkt *rtw_os_pkt_copy(_pkt *pkt)
{
#if defined(PLATFORM_LINUX)
	return rtw_skb_copy(pkt);
#elif defined(PLATFORM_FREEBSD)
	return m_dup(pkt, M_NOWAIT);
#else
	#error "TBD\n"
#endif
}

inline void *rtw_os_pkt_data(_pkt *pkt)
{
#if defined(PLATFORM_LINUX)
	return pkt->data;
#elif defined(PLATFORM_FREEBSD)
	return pkt->m_data;
#else
	#error "TBD\n"
#endif
}

inline u32 rtw_os_pkt_len(_pkt *pkt)
{
#if defined(PLATFORM_LINUX)
	return pkt->len;
#elif defined(PLATFORM_FREEBSD)
	return pkt->m_pkthdr.len;
#else
	#error "TBD\n"
#endif
}

void _rtw_memcpy(void *dst, const void *src, u32 sz)
{

#if defined(PLATFORM_LINUX) || defined (PLATFORM_FREEBSD)

	memcpy(dst, src, sz);

#endif

#ifdef PLATFORM_WINDOWS

	NdisMoveMemory(dst, src, sz);

#endif

}

inline void _rtw_memmove(void *dst, const void *src, u32 sz)
{
#if defined(PLATFORM_LINUX)
	memmove(dst, src, sz);
#else
	#error "TBD\n"
#endif
}

int	_rtw_memcmp(const void *dst, const void *src, u32 sz)
{

#if defined(PLATFORM_LINUX) || defined (PLATFORM_FREEBSD)
	/* under Linux/GNU/GLibc, the return value of memcmp for two same mem. chunk is 0 */

	if (!(memcmp(dst, src, sz)))
		return _TRUE;
	else
		return _FALSE;
#endif


#ifdef PLATFORM_WINDOWS
	/* under Windows, the return value of NdisEqualMemory for two same mem. chunk is 1 */

	if (NdisEqualMemory(dst, src, sz))
		return _TRUE;
	else
		return _FALSE;

#endif



}

void _rtw_memset(void *pbuf, int c, u32 sz)
{

#if defined(PLATFORM_LINUX) || defined (PLATFORM_FREEBSD)

	memset(pbuf, c, sz);

#endif

#ifdef PLATFORM_WINDOWS
#if 0
	NdisZeroMemory(pbuf, sz);
	if (c != 0)
		memset(pbuf, c, sz);
#else
	NdisFillMemory(pbuf, sz, c);
#endif
#endif

}

#ifdef PLATFORM_FREEBSD
static inline void __list_add(_list *pnew, _list *pprev, _list *pnext)
{
	pnext->prev = pnew;
	pnew->next = pnext;
	pnew->prev = pprev;
	pprev->next = pnew;
}
#endif /* PLATFORM_FREEBSD */


void _rtw_init_listhead(_list *list)
{

#ifdef PLATFORM_LINUX

	INIT_LIST_HEAD(list);

#endif

#ifdef PLATFORM_FREEBSD
	list->next = list;
	list->prev = list;
#endif
#ifdef PLATFORM_WINDOWS

	NdisInitializeListHead(list);

#endif

}


/*
For the following list_xxx operations,
caller must guarantee the atomic context.
Otherwise, there will be racing condition.
*/
u32	rtw_is_list_empty(_list *phead)
{

#ifdef PLATFORM_LINUX

	if (list_empty(phead))
		return _TRUE;
	else
		return _FALSE;

#endif
#ifdef PLATFORM_FREEBSD

	if (phead->next == phead)
		return _TRUE;
	else
		return _FALSE;

#endif


#ifdef PLATFORM_WINDOWS

	if (IsListEmpty(phead))
		return _TRUE;
	else
		return _FALSE;

#endif


}

void rtw_list_insert_head(_list *plist, _list *phead)
{

#ifdef PLATFORM_LINUX
	list_add(plist, phead);
#endif

#ifdef PLATFORM_FREEBSD
	__list_add(plist, phead, phead->next);
#endif

#ifdef PLATFORM_WINDOWS
	InsertHeadList(phead, plist);
#endif
}

void rtw_list_insert_tail(_list *plist, _list *phead)
{

#ifdef PLATFORM_LINUX

	list_add_tail(plist, phead);

#endif
#ifdef PLATFORM_FREEBSD

	__list_add(plist, phead->prev, phead);

#endif
#ifdef PLATFORM_WINDOWS

	InsertTailList(phead, plist);

#endif

}

inline void rtw_list_splice(_list *list, _list *head)
{
#ifdef PLATFORM_LINUX
	list_splice(list, head);
#else
	#error "TBD\n"
#endif
}

inline void rtw_list_splice_init(_list *list, _list *head)
{
#ifdef PLATFORM_LINUX
	list_splice_init(list, head);
#else
	#error "TBD\n"
#endif
}

inline void rtw_list_splice_tail(_list *list, _list *head)
{
#ifdef PLATFORM_LINUX
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 27))
	if (!list_empty(list))
		__list_splice(list, head);
	#else
	list_splice_tail(list, head);
	#endif
#else
	#error "TBD\n"
#endif
}

inline void rtw_hlist_head_init(rtw_hlist_head *h)
{
#ifdef PLATFORM_LINUX
	INIT_HLIST_HEAD(h);
#else
	#error "TBD\n"
#endif
}

inline void rtw_hlist_add_head(rtw_hlist_node *n, rtw_hlist_head *h)
{
#ifdef PLATFORM_LINUX
	hlist_add_head(n, h);
#else
	#error "TBD\n"
#endif
}

inline void rtw_hlist_del(rtw_hlist_node *n)
{
#ifdef PLATFORM_LINUX
	hlist_del(n);
#else
	#error "TBD\n"
#endif
}

inline void rtw_hlist_add_head_rcu(rtw_hlist_node *n, rtw_hlist_head *h)
{
#ifdef PLATFORM_LINUX
	hlist_add_head_rcu(n, h);
#else
	#error "TBD\n"
#endif
}

inline void rtw_hlist_del_rcu(rtw_hlist_node *n)
{
#ifdef PLATFORM_LINUX
	hlist_del_rcu(n);
#else
	#error "TBD\n"
#endif
}

void rtw_init_timer(_timer *ptimer, void *padapter, void *pfunc, void *ctx)
{
	_adapter *adapter = (_adapter *)padapter;

#ifdef PLATFORM_LINUX
	_init_timer(ptimer, adapter->pnetdev, pfunc, ctx);
#endif
#ifdef PLATFORM_FREEBSD
	_init_timer(ptimer, adapter->pifp, pfunc, ctx);
#endif
#ifdef PLATFORM_WINDOWS
	_init_timer(ptimer, adapter->hndis_adapter, pfunc, ctx);
#endif
}

/*

Caller must check if the list is empty before calling rtw_list_delete

*/


void _rtw_init_sema(_sema	*sema, int init_val)
{

#ifdef PLATFORM_LINUX

	sema_init(sema, init_val);

#endif
#ifdef PLATFORM_FREEBSD
	sema_init(sema, init_val, "rtw_drv");
#endif
#ifdef PLATFORM_OS_XP

	KeInitializeSemaphore(sema, init_val,  SEMA_UPBND); /* count=0; */

#endif

#ifdef PLATFORM_OS_CE
	if (*sema == NULL)
		*sema = CreateSemaphore(NULL, init_val, SEMA_UPBND, NULL);
#endif

}

void _rtw_free_sema(_sema	*sema)
{
#ifdef PLATFORM_FREEBSD
	sema_destroy(sema);
#endif
#ifdef PLATFORM_OS_CE
	CloseHandle(*sema);
#endif

}

void _rtw_up_sema(_sema	*sema)
{

#ifdef PLATFORM_LINUX

	up(sema);

#endif
#ifdef PLATFORM_FREEBSD
	sema_post(sema);
#endif
#ifdef PLATFORM_OS_XP

	KeReleaseSemaphore(sema, IO_NETWORK_INCREMENT, 1,  FALSE);

#endif

#ifdef PLATFORM_OS_CE
	ReleaseSemaphore(*sema,  1,  NULL);
#endif
}

u32 _rtw_down_sema(_sema *sema)
{

#ifdef PLATFORM_LINUX

	if (down_interruptible(sema))
		return _FAIL;
	else
		return _SUCCESS;

#endif
#ifdef PLATFORM_FREEBSD
	sema_wait(sema);
	return  _SUCCESS;
#endif
#ifdef PLATFORM_OS_XP

	if (STATUS_SUCCESS == KeWaitForSingleObject(sema, Executive, KernelMode, TRUE, NULL))
		return  _SUCCESS;
	else
		return _FAIL;
#endif

#ifdef PLATFORM_OS_CE
	if (WAIT_OBJECT_0 == WaitForSingleObject(*sema, INFINITE))
		return _SUCCESS;
	else
		return _FAIL;
#endif
}

inline void thread_exit(_completion *comp)
{
#ifdef PLATFORM_LINUX
	complete_and_exit(comp, 0);
#endif

#ifdef PLATFORM_FREEBSD
	printf("%s", "RTKTHREAD_exit");
#endif

#ifdef PLATFORM_OS_CE
	ExitThread(STATUS_SUCCESS);
#endif

#ifdef PLATFORM_OS_XP
	PsTerminateSystemThread(STATUS_SUCCESS);
#endif
}

inline void _rtw_init_completion(_completion *comp)
{
#ifdef PLATFORM_LINUX
	init_completion(comp);
#endif
}
inline void _rtw_wait_for_comp_timeout(_completion *comp)
{
#ifdef PLATFORM_LINUX
	wait_for_completion_timeout(comp, msecs_to_jiffies(3000));
#endif
}
inline void _rtw_wait_for_comp(_completion *comp)
{
#ifdef PLATFORM_LINUX
	wait_for_completion(comp);
#endif
}

void	_rtw_mutex_init(_mutex *pmutex)
{
#ifdef PLATFORM_LINUX

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	mutex_init(pmutex);
#else
	init_MUTEX(pmutex);
#endif

#endif
#ifdef PLATFORM_FREEBSD
	mtx_init(pmutex, "", NULL, MTX_DEF | MTX_RECURSE);
#endif
#ifdef PLATFORM_OS_XP

	KeInitializeMutex(pmutex, 0);

#endif

#ifdef PLATFORM_OS_CE
	*pmutex =  CreateMutex(NULL, _FALSE, NULL);
#endif
}

void	_rtw_mutex_free(_mutex *pmutex);
void	_rtw_mutex_free(_mutex *pmutex)
{
#ifdef PLATFORM_LINUX

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	mutex_destroy(pmutex);
#else
#endif

#ifdef PLATFORM_FREEBSD
	sema_destroy(pmutex);
#endif

#endif

#ifdef PLATFORM_OS_XP

#endif

#ifdef PLATFORM_OS_CE

#endif
}

void	_rtw_spinlock_init(_lock *plock)
{

#ifdef PLATFORM_LINUX

	spin_lock_init(plock);

#endif
#ifdef PLATFORM_FREEBSD
	mtx_init(plock, "", NULL, MTX_DEF | MTX_RECURSE);
#endif
#ifdef PLATFORM_WINDOWS

	NdisAllocateSpinLock(plock);

#endif

}

void	_rtw_spinlock_free(_lock *plock)
{
#ifdef PLATFORM_FREEBSD
	mtx_destroy(plock);
#endif

#ifdef PLATFORM_WINDOWS

	NdisFreeSpinLock(plock);

#endif

}
#ifdef PLATFORM_FREEBSD
extern PADAPTER prtw_lock;

void rtw_mtx_lock(_lock *plock)
{
	if (prtw_lock)
		mtx_lock(&prtw_lock->glock);
	else
		printf("%s prtw_lock==NULL", __FUNCTION__);
}
void rtw_mtx_unlock(_lock *plock)
{
	if (prtw_lock)
		mtx_unlock(&prtw_lock->glock);
	else
		printf("%s prtw_lock==NULL", __FUNCTION__);

}
#endif /* PLATFORM_FREEBSD */


void	_rtw_spinlock(_lock	*plock)
{

#ifdef PLATFORM_LINUX

	spin_lock(plock);

#endif
#ifdef PLATFORM_FREEBSD
	mtx_lock(plock);
#endif
#ifdef PLATFORM_WINDOWS

	NdisAcquireSpinLock(plock);

#endif

}

void	_rtw_spinunlock(_lock *plock)
{

#ifdef PLATFORM_LINUX

	spin_unlock(plock);

#endif
#ifdef PLATFORM_FREEBSD
	mtx_unlock(plock);
#endif
#ifdef PLATFORM_WINDOWS

	NdisReleaseSpinLock(plock);

#endif
}


void	_rtw_spinlock_ex(_lock	*plock)
{

#ifdef PLATFORM_LINUX

	spin_lock(plock);

#endif
#ifdef PLATFORM_FREEBSD
	mtx_lock(plock);
#endif
#ifdef PLATFORM_WINDOWS

	NdisDprAcquireSpinLock(plock);

#endif

}

void	_rtw_spinunlock_ex(_lock *plock)
{

#ifdef PLATFORM_LINUX

	spin_unlock(plock);

#endif
#ifdef PLATFORM_FREEBSD
	mtx_unlock(plock);
#endif
#ifdef PLATFORM_WINDOWS

	NdisDprReleaseSpinLock(plock);

#endif
}



void _rtw_init_queue(_queue *pqueue)
{
	_rtw_init_listhead(&(pqueue->queue));
	_rtw_spinlock_init(&(pqueue->lock));
}

void _rtw_deinit_queue(_queue *pqueue)
{
	_rtw_spinlock_free(&(pqueue->lock));
}

u32	  _rtw_queue_empty(_queue	*pqueue)
{
	return rtw_is_list_empty(&(pqueue->queue));
}


u32 rtw_end_of_queue_search(_list *head, _list *plist)
{
	if (head == plist)
		return _TRUE;
	else
		return _FALSE;
}


systime _rtw_get_current_time(void)
{

#ifdef PLATFORM_LINUX
	return jiffies;
#endif
#ifdef PLATFORM_FREEBSD
	struct timeval tvp;
	getmicrotime(&tvp);
	return tvp.tv_sec;
#endif
#ifdef PLATFORM_WINDOWS
	LARGE_INTEGER	SystemTime;
	NdisGetCurrentSystemTime(&SystemTime);
	return SystemTime.LowPart;/* count of 100-nanosecond intervals */
#endif
}

inline u32 _rtw_systime_to_ms(systime stime)
{
#ifdef PLATFORM_LINUX
	return jiffies_to_msecs(stime);
#endif
#ifdef PLATFORM_FREEBSD
	return stime * 1000;
#endif
#ifdef PLATFORM_WINDOWS
	return stime / 10000 ;
#endif
}

inline systime _rtw_ms_to_systime(u32 ms)
{
#ifdef PLATFORM_LINUX
	return msecs_to_jiffies(ms);
#endif
#ifdef PLATFORM_FREEBSD
	return ms / 1000;
#endif
#ifdef PLATFORM_WINDOWS
	return ms * 10000 ;
#endif
}

inline systime _rtw_us_to_systime(u32 us)
{
#ifdef PLATFORM_LINUX
	return usecs_to_jiffies(us);
#else
	#error "TBD\n"
#endif
}

/* the input parameter start use the same unit as returned by rtw_get_current_time */
inline s32 _rtw_get_passing_time_ms(systime start)
{
	return _rtw_systime_to_ms(_rtw_get_current_time() - start);
}

inline s32 _rtw_get_remaining_time_ms(systime end)
{
	return _rtw_systime_to_ms(end - _rtw_get_current_time());
}

inline s32 _rtw_get_time_interval_ms(systime start, systime end)
{
	return _rtw_systime_to_ms(end - start);
}

inline bool _rtw_time_after(systime a, systime b)
{
#ifdef PLATFORM_LINUX
	return time_after(a, b);
#else
	#error "TBD\n"
#endif
}

void rtw_sleep_schedulable(int ms)
{

#ifdef PLATFORM_LINUX

	u32 delta;

	delta = (ms * HZ) / 1000; /* (ms) */
	if (delta == 0) {
		delta = 1;/* 1 ms */
	}
	set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(delta);
	return;

#endif
#ifdef PLATFORM_FREEBSD
	DELAY(ms * 1000);
	return ;
#endif

#ifdef PLATFORM_WINDOWS

	NdisMSleep(ms * 1000); /* (us)*1000=(ms) */

#endif

}


void rtw_msleep_os(int ms)
{

#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
	if (ms < 20) {
		unsigned long us = ms * 1000UL;
		usleep_range(us, us + 1000UL);
	} else
#endif
		msleep((unsigned int)ms);

#endif
#ifdef PLATFORM_FREEBSD
	/* Delay for delay microseconds */
	DELAY(ms * 1000);
	return ;
#endif
#ifdef PLATFORM_WINDOWS

	NdisMSleep(ms * 1000); /* (us)*1000=(ms) */

#endif


}
void rtw_usleep_os(int us)
{
#ifdef PLATFORM_LINUX

	/* msleep((unsigned int)us); */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
	usleep_range(us, us + 1);
#else
	if (1 < (us / 1000))
		msleep(1);
	else
		msleep((us / 1000) + 1);
#endif
#endif

#ifdef PLATFORM_FREEBSD
	/* Delay for delay microseconds */
	DELAY(us);

	return ;
#endif
#ifdef PLATFORM_WINDOWS

	NdisMSleep(us); /* (us) */

#endif


}


#ifdef DBG_DELAY_OS
void _rtw_mdelay_os(int ms, const char *func, const int line)
{
#if 0
	if (ms > 10)
		RTW_INFO("%s:%d %s(%d)\n", func, line, __FUNCTION__, ms);
	rtw_msleep_os(ms);
	return;
#endif


	RTW_INFO("%s:%d %s(%d)\n", func, line, __FUNCTION__, ms);

#if defined(PLATFORM_LINUX)

	mdelay((unsigned long)ms);

#elif defined(PLATFORM_WINDOWS)

	NdisStallExecution(ms * 1000); /* (us)*1000=(ms) */

#endif


}
void _rtw_udelay_os(int us, const char *func, const int line)
{

#if 0
	if (us > 1000) {
		RTW_INFO("%s:%d %s(%d)\n", func, line, __FUNCTION__, us);
		rtw_usleep_os(us);
		return;
	}
#endif


	RTW_INFO("%s:%d %s(%d)\n", func, line, __FUNCTION__, us);


#if defined(PLATFORM_LINUX)

	udelay((unsigned long)us);

#elif defined(PLATFORM_WINDOWS)

	NdisStallExecution(us); /* (us) */

#endif

}
#else
void rtw_mdelay_os(int ms)
{

#ifdef PLATFORM_LINUX

	mdelay((unsigned long)ms);

#endif
#ifdef PLATFORM_FREEBSD
	DELAY(ms * 1000);
	return ;
#endif
#ifdef PLATFORM_WINDOWS

	NdisStallExecution(ms * 1000); /* (us)*1000=(ms) */

#endif


}
void rtw_udelay_os(int us)
{

#ifdef PLATFORM_LINUX

	udelay((unsigned long)us);

#endif
#ifdef PLATFORM_FREEBSD
	/* Delay for delay microseconds */
	DELAY(us);
	return ;
#endif
#ifdef PLATFORM_WINDOWS

	NdisStallExecution(us); /* (us) */

#endif

}
#endif

void rtw_yield_os(void)
{
#ifdef PLATFORM_LINUX
	yield();
#endif
#ifdef PLATFORM_FREEBSD
	yield();
#endif
#ifdef PLATFORM_WINDOWS
	SwitchToThread();
#endif
}

bool rtw_macaddr_is_larger(const u8 *a, const u8 *b)
{
	u32 va, vb;

	va = be32_to_cpu(*((u32 *)a));
	vb = be32_to_cpu(*((u32 *)b));
	if (va > vb)
		return 1;
	else if (va < vb)
		return 0;

	return be16_to_cpu(*((u16 *)(a + 4))) > be16_to_cpu(*((u16 *)(b + 4)));
}

#define RTW_SUSPEND_LOCK_NAME "rtw_wifi"
#define RTW_SUSPEND_TRAFFIC_LOCK_NAME "rtw_wifi_traffic"
#define RTW_SUSPEND_RESUME_LOCK_NAME "rtw_wifi_resume"
#ifdef CONFIG_WAKELOCK
static struct wake_lock rtw_suspend_lock;
static struct wake_lock rtw_suspend_traffic_lock;
static struct wake_lock rtw_suspend_resume_lock;
#elif defined(CONFIG_ANDROID_POWER)
static android_suspend_lock_t rtw_suspend_lock = {
	.name = RTW_SUSPEND_LOCK_NAME
};
static android_suspend_lock_t rtw_suspend_traffic_lock = {
	.name = RTW_SUSPEND_TRAFFIC_LOCK_NAME
};
static android_suspend_lock_t rtw_suspend_resume_lock = {
	.name = RTW_SUSPEND_RESUME_LOCK_NAME
};
#endif

inline void rtw_suspend_lock_init(void)
{
#ifdef CONFIG_WAKELOCK
	wake_lock_init(&rtw_suspend_lock, WAKE_LOCK_SUSPEND, RTW_SUSPEND_LOCK_NAME);
	wake_lock_init(&rtw_suspend_traffic_lock, WAKE_LOCK_SUSPEND, RTW_SUSPEND_TRAFFIC_LOCK_NAME);
	wake_lock_init(&rtw_suspend_resume_lock, WAKE_LOCK_SUSPEND, RTW_SUSPEND_RESUME_LOCK_NAME);
#elif defined(CONFIG_ANDROID_POWER)
	android_init_suspend_lock(&rtw_suspend_lock);
	android_init_suspend_lock(&rtw_suspend_traffic_lock);
	android_init_suspend_lock(&rtw_suspend_resume_lock);
#endif
}

inline void rtw_suspend_lock_uninit(void)
{
#ifdef CONFIG_WAKELOCK
	wake_lock_destroy(&rtw_suspend_lock);
	wake_lock_destroy(&rtw_suspend_traffic_lock);
	wake_lock_destroy(&rtw_suspend_resume_lock);
#elif defined(CONFIG_ANDROID_POWER)
	android_uninit_suspend_lock(&rtw_suspend_lock);
	android_uninit_suspend_lock(&rtw_suspend_traffic_lock);
	android_uninit_suspend_lock(&rtw_suspend_resume_lock);
#endif
}

inline void rtw_lock_suspend(void)
{
#ifdef CONFIG_WAKELOCK
	wake_lock(&rtw_suspend_lock);
#elif defined(CONFIG_ANDROID_POWER)
	android_lock_suspend(&rtw_suspend_lock);
#endif

#if  defined(CONFIG_WAKELOCK) || defined(CONFIG_ANDROID_POWER)
	/* RTW_INFO("####%s: suspend_lock_count:%d####\n", __FUNCTION__, rtw_suspend_lock.stat.count); */
#endif
}

inline void rtw_unlock_suspend(void)
{
#ifdef CONFIG_WAKELOCK
	wake_unlock(&rtw_suspend_lock);
#elif defined(CONFIG_ANDROID_POWER)
	android_unlock_suspend(&rtw_suspend_lock);
#endif

#if  defined(CONFIG_WAKELOCK) || defined(CONFIG_ANDROID_POWER)
	/* RTW_INFO("####%s: suspend_lock_count:%d####\n", __FUNCTION__, rtw_suspend_lock.stat.count); */
#endif
}

inline void rtw_resume_lock_suspend(void)
{
#ifdef CONFIG_WAKELOCK
	wake_lock(&rtw_suspend_resume_lock);
#elif defined(CONFIG_ANDROID_POWER)
	android_lock_suspend(&rtw_suspend_resume_lock);
#endif

#if  defined(CONFIG_WAKELOCK) || defined(CONFIG_ANDROID_POWER)
	/* RTW_INFO("####%s: suspend_lock_count:%d####\n", __FUNCTION__, rtw_suspend_lock.stat.count); */
#endif
}

inline void rtw_resume_unlock_suspend(void)
{
#ifdef CONFIG_WAKELOCK
	wake_unlock(&rtw_suspend_resume_lock);
#elif defined(CONFIG_ANDROID_POWER)
	android_unlock_suspend(&rtw_suspend_resume_lock);
#endif

#if  defined(CONFIG_WAKELOCK) || defined(CONFIG_ANDROID_POWER)
	/* RTW_INFO("####%s: suspend_lock_count:%d####\n", __FUNCTION__, rtw_suspend_lock.stat.count); */
#endif
}

inline void rtw_lock_suspend_timeout(u32 timeout_ms)
{
#ifdef CONFIG_WAKELOCK
	wake_lock_timeout(&rtw_suspend_lock, rtw_ms_to_systime(timeout_ms));
#elif defined(CONFIG_ANDROID_POWER)
	android_lock_suspend_auto_expire(&rtw_suspend_lock, rtw_ms_to_systime(timeout_ms));
#endif
}


inline void rtw_lock_traffic_suspend_timeout(u32 timeout_ms)
{
#ifdef CONFIG_WAKELOCK
	wake_lock_timeout(&rtw_suspend_traffic_lock, rtw_ms_to_systime(timeout_ms));
#elif defined(CONFIG_ANDROID_POWER)
	android_lock_suspend_auto_expire(&rtw_suspend_traffic_lock, rtw_ms_to_systime(timeout_ms));
#endif
	/* RTW_INFO("traffic lock timeout:%d\n", timeout_ms); */
}

inline void rtw_set_bit(int nr, unsigned long *addr)
{
#ifdef PLATFORM_LINUX
	set_bit(nr, addr);
#else
	#error "TBD\n";
#endif
}

inline void rtw_clear_bit(int nr, unsigned long *addr)
{
#ifdef PLATFORM_LINUX
	clear_bit(nr, addr);
#else
	#error "TBD\n";
#endif
}

inline int rtw_test_and_clear_bit(int nr, unsigned long *addr)
{
#ifdef PLATFORM_LINUX
	return test_and_clear_bit(nr, addr);
#else
	#error "TBD\n";
#endif
}

inline void ATOMIC_SET(ATOMIC_T *v, int i)
{
#ifdef PLATFORM_LINUX
	atomic_set(v, i);
#elif defined(PLATFORM_WINDOWS)
	*v = i; /* other choice???? */
#elif defined(PLATFORM_FREEBSD)
	atomic_set_int(v, i);
#endif
}

inline int ATOMIC_READ(ATOMIC_T *v)
{
#ifdef PLATFORM_LINUX
	return atomic_read(v);
#elif defined(PLATFORM_WINDOWS)
	return *v; /* other choice???? */
#elif defined(PLATFORM_FREEBSD)
	return atomic_load_acq_32(v);
#endif
}

inline void ATOMIC_ADD(ATOMIC_T *v, int i)
{
#ifdef PLATFORM_LINUX
	atomic_add(i, v);
#elif defined(PLATFORM_WINDOWS)
	InterlockedAdd(v, i);
#elif defined(PLATFORM_FREEBSD)
	atomic_add_int(v, i);
#endif
}
inline void ATOMIC_SUB(ATOMIC_T *v, int i)
{
#ifdef PLATFORM_LINUX
	atomic_sub(i, v);
#elif defined(PLATFORM_WINDOWS)
	InterlockedAdd(v, -i);
#elif defined(PLATFORM_FREEBSD)
	atomic_subtract_int(v, i);
#endif
}

inline void ATOMIC_INC(ATOMIC_T *v)
{
#ifdef PLATFORM_LINUX
	atomic_inc(v);
#elif defined(PLATFORM_WINDOWS)
	InterlockedIncrement(v);
#elif defined(PLATFORM_FREEBSD)
	atomic_add_int(v, 1);
#endif
}

inline void ATOMIC_DEC(ATOMIC_T *v)
{
#ifdef PLATFORM_LINUX
	atomic_dec(v);
#elif defined(PLATFORM_WINDOWS)
	InterlockedDecrement(v);
#elif defined(PLATFORM_FREEBSD)
	atomic_subtract_int(v, 1);
#endif
}

inline int ATOMIC_ADD_RETURN(ATOMIC_T *v, int i)
{
#ifdef PLATFORM_LINUX
	return atomic_add_return(i, v);
#elif defined(PLATFORM_WINDOWS)
	return InterlockedAdd(v, i);
#elif defined(PLATFORM_FREEBSD)
	atomic_add_int(v, i);
	return atomic_load_acq_32(v);
#endif
}

inline int ATOMIC_SUB_RETURN(ATOMIC_T *v, int i)
{
#ifdef PLATFORM_LINUX
	return atomic_sub_return(i, v);
#elif defined(PLATFORM_WINDOWS)
	return InterlockedAdd(v, -i);
#elif defined(PLATFORM_FREEBSD)
	atomic_subtract_int(v, i);
	return atomic_load_acq_32(v);
#endif
}

inline int ATOMIC_INC_RETURN(ATOMIC_T *v)
{
#ifdef PLATFORM_LINUX
	return atomic_inc_return(v);
#elif defined(PLATFORM_WINDOWS)
	return InterlockedIncrement(v);
#elif defined(PLATFORM_FREEBSD)
	atomic_add_int(v, 1);
	return atomic_load_acq_32(v);
#endif
}

inline int ATOMIC_DEC_RETURN(ATOMIC_T *v)
{
#ifdef PLATFORM_LINUX
	return atomic_dec_return(v);
#elif defined(PLATFORM_WINDOWS)
	return InterlockedDecrement(v);
#elif defined(PLATFORM_FREEBSD)
	atomic_subtract_int(v, 1);
	return atomic_load_acq_32(v);
#endif
}

inline bool ATOMIC_INC_UNLESS(ATOMIC_T *v, int u)
{
#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 15))
	return atomic_add_unless(v, 1, u);
#else
	/* only make sure not exceed after this function */
	if (ATOMIC_INC_RETURN(v) > u) {
		ATOMIC_DEC(v);
		return 0;
	}
	return 1;
#endif
#else
	#error "TBD\n"
#endif
}

#ifdef PLATFORM_LINUX
/*
* Open a file with the specific @param path, @param flag, @param mode
* @param fpp the pointer of struct file pointer to get struct file pointer while file opening is success
* @param path the path of the file to open
* @param flag file operation flags, please refer to linux document
* @param mode please refer to linux document
* @return Linux specific error code
*/
static int openFile(struct file **fpp, const char *path, int flag, int mode)
{
	struct file *fp;

	fp = filp_open(path, flag, mode);
	if (IS_ERR(fp)) {
		*fpp = NULL;
		return PTR_ERR(fp);
	} else {
		*fpp = fp;
		return 0;
	}
}

/*
* Close the file with the specific @param fp
* @param fp the pointer of struct file to close
* @return always 0
*/
static int closeFile(struct file *fp)
{
	filp_close(fp, NULL);
	return 0;
}

static int readFile(struct file *fp, char *buf, int len)
{
	int rlen = 0, sum = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	if (!(fp->f_mode & FMODE_CAN_READ))
#else
	if (!fp->f_op || !fp->f_op->read)
#endif
		return -EPERM;

	while (sum < len) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
		rlen = kernel_read(fp, buf + sum, len - sum, &fp->f_pos);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
		rlen = __vfs_read(fp, buf + sum, len - sum, &fp->f_pos);
#else
		rlen = fp->f_op->read(fp, buf + sum, len - sum, &fp->f_pos);
#endif
		if (rlen > 0)
			sum += rlen;
		else if (0 != rlen)
			return rlen;
		else
			break;
	}

	return  sum;

}

static int writeFile(struct file *fp, char *buf, int len)
{
	int wlen = 0, sum = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	if (!(fp->f_mode & FMODE_CAN_WRITE))
#else
	if (!fp->f_op || !fp->f_op->write)
#endif
		return -EPERM;

	while (sum < len) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
		wlen = kernel_write(fp, buf + sum, len - sum, &fp->f_pos);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
		wlen = __vfs_write(fp, buf + sum, len - sum, &fp->f_pos);
#else
		wlen = fp->f_op->write(fp, buf + sum, len - sum, &fp->f_pos);
#endif
		if (wlen > 0)
			sum += wlen;
		else if (0 != wlen)
			return wlen;
		else
			break;
	}

	return sum;

}

/*
* Test if the specifi @param pathname is a direct and readable
* If readable, @param sz is not used
* @param pathname the name of the path to test
* @return Linux specific error code
*/
static int isDirReadable(const char *pathname, u32 *sz)
{
	struct path path;
	int error = 0;

	return kern_path(pathname, LOOKUP_FOLLOW, &path);
}

/*
* Test if the specifi @param path is a file and readable
* If readable, @param sz is got
* @param path the path of the file to test
* @return Linux specific error code
*/
static int isFileReadable(const char *path, u32 *sz)
{
	struct file *fp;
	int ret = 0;
	mm_segment_t oldfs;
	char buf;

	fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp))
		ret = PTR_ERR(fp);
	else {
		oldfs = get_fs();
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))
		set_fs(KERNEL_DS);
		#else
		set_fs(get_ds());
		#endif

		if (1 != readFile(fp, &buf, 1))
			ret = PTR_ERR(fp);

		if (ret == 0 && sz) {
			#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
			*sz = i_size_read(fp->f_path.dentry->d_inode);
			#else
			*sz = i_size_read(fp->f_dentry->d_inode);
			#endif
		}

		set_fs(oldfs);
		filp_close(fp, NULL);
	}
	return ret;
}

/*
* Open the file with @param path and retrive the file content into memory starting from @param buf for @param sz at most
* @param path the path of the file to open and read
* @param buf the starting address of the buffer to store file content
* @param sz how many bytes to read at most
* @return the byte we've read, or Linux specific error code
*/
static int retriveFromFile(const char *path, u8 *buf, u32 sz)
{
	int ret = -1;
	mm_segment_t oldfs;
	struct file *fp;

	if (path && buf) {
		ret = openFile(&fp, path, O_RDONLY, 0);
		if (0 == ret) {
			RTW_INFO("%s openFile path:%s fp=%p\n", __FUNCTION__, path , fp);

			oldfs = get_fs();
			#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))
			set_fs(KERNEL_DS);
			#else
			set_fs(get_ds());
			#endif
			ret = readFile(fp, buf, sz);
			set_fs(oldfs);
			closeFile(fp);

			RTW_INFO("%s readFile, ret:%d\n", __FUNCTION__, ret);

		} else
			RTW_INFO("%s openFile path:%s Fail, ret:%d\n", __FUNCTION__, path, ret);
	} else {
		RTW_INFO("%s NULL pointer\n", __FUNCTION__);
		ret =  -EINVAL;
	}
	return ret;
}

/*
* Open the file with @param path and wirte @param sz byte of data starting from @param buf into the file
* @param path the path of the file to open and write
* @param buf the starting address of the data to write into file
* @param sz how many bytes to write at most
* @return the byte we've written, or Linux specific error code
*/
static int storeToFile(const char *path, u8 *buf, u32 sz)
{
	int ret = 0;
	mm_segment_t oldfs;
	struct file *fp;

	if (path && buf) {
		ret = openFile(&fp, path, O_CREAT | O_WRONLY, 0666);
		if (0 == ret) {
			RTW_INFO("%s openFile path:%s fp=%p\n", __FUNCTION__, path , fp);

			oldfs = get_fs();
			#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))
			set_fs(KERNEL_DS);
			#else
			set_fs(get_ds());
			#endif
			ret = writeFile(fp, buf, sz);
			set_fs(oldfs);
			closeFile(fp);

			RTW_INFO("%s writeFile, ret:%d\n", __FUNCTION__, ret);

		} else
			RTW_INFO("%s openFile path:%s Fail, ret:%d\n", __FUNCTION__, path, ret);
	} else {
		RTW_INFO("%s NULL pointer\n", __FUNCTION__);
		ret =  -EINVAL;
	}
	return ret;
}
#endif /* PLATFORM_LINUX */

/*
* Test if the specifi @param path is a direct and readable
* @param path the path of the direct to test
* @return _TRUE or _FALSE
*/
int rtw_is_dir_readable(const char *path)
{
#ifdef PLATFORM_LINUX
	if (isDirReadable(path, NULL) == 0)
		return _TRUE;
	else
		return _FALSE;
#else
	/* Todo... */
	return _FALSE;
#endif
}

/*
* Test if the specifi @param path is a file and readable
* @param path the path of the file to test
* @return _TRUE or _FALSE
*/
int rtw_is_file_readable(const char *path)
{
#ifdef PLATFORM_LINUX
	if (isFileReadable(path, NULL) == 0)
		return _TRUE;
	else
		return _FALSE;
#else
	/* Todo... */
	return _FALSE;
#endif
}

/*
* Test if the specifi @param path is a file and readable.
* If readable, @param sz is got
* @param path the path of the file to test
* @return _TRUE or _FALSE
*/
int rtw_is_file_readable_with_size(const char *path, u32 *sz)
{
#ifdef PLATFORM_LINUX
	if (isFileReadable(path, sz) == 0)
		return _TRUE;
	else
		return _FALSE;
#else
	/* Todo... */
	return _FALSE;
#endif
}

/*
* Test if the specifi @param path is a readable file with valid size.
* If readable, @param sz is got
* @param path the path of the file to test
* @return _TRUE or _FALSE
*/
int rtw_readable_file_sz_chk(const char *path, u32 sz)
{
	u32 fsz;

	if (rtw_is_file_readable_with_size(path, &fsz) == _FALSE)
		return _FALSE;

	if (fsz > sz)
		return _FALSE;
	
	return _TRUE;
}

/*
* Open the file with @param path and retrive the file content into memory starting from @param buf for @param sz at most
* @param path the path of the file to open and read
* @param buf the starting address of the buffer to store file content
* @param sz how many bytes to read at most
* @return the byte we've read
*/
int rtw_retrieve_from_file(const char *path, u8 *buf, u32 sz)
{
#ifdef PLATFORM_LINUX
	int ret = retriveFromFile(path, buf, sz);
	return ret >= 0 ? ret : 0;
#else
	/* Todo... */
	return 0;
#endif
}

/*
* Open the file with @param path and wirte @param sz byte of data starting from @param buf into the file
* @param path the path of the file to open and write
* @param buf the starting address of the data to write into file
* @param sz how many bytes to write at most
* @return the byte we've written
*/
int rtw_store_to_file(const char *path, u8 *buf, u32 sz)
{
#ifdef PLATFORM_LINUX
	int ret = storeToFile(path, buf, sz);
	return ret >= 0 ? ret : 0;
#else
	/* Todo... */
	return 0;
#endif
}

#ifdef PLATFORM_LINUX
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv)
{
	struct net_device *pnetdev;
	struct rtw_netdev_priv_indicator *pnpi;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
#else
	pnetdev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
#endif
	if (!pnetdev)
		goto RETURN;

	pnpi = netdev_priv(pnetdev);
	pnpi->priv = old_priv;
	pnpi->sizeof_priv = sizeof_priv;

RETURN:
	return pnetdev;
}

struct net_device *rtw_alloc_etherdev(int sizeof_priv)
{
	struct net_device *pnetdev;
	struct rtw_netdev_priv_indicator *pnpi;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
#else
	pnetdev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
#endif
	if (!pnetdev)
		goto RETURN;

	pnpi = netdev_priv(pnetdev);

	pnpi->priv = rtw_zvmalloc(sizeof_priv);
	if (!pnpi->priv) {
		free_netdev(pnetdev);
		pnetdev = NULL;
		goto RETURN;
	}

	pnpi->sizeof_priv = sizeof_priv;
RETURN:
	return pnetdev;
}

void rtw_free_netdev(struct net_device *netdev)
{
	struct rtw_netdev_priv_indicator *pnpi;

	if (!netdev)
		goto RETURN;

	pnpi = netdev_priv(netdev);

	if (!pnpi->priv)
		goto RETURN;

	free_netdev(netdev);

RETURN:
	return;
}

int rtw_change_ifname(_adapter *padapter, const char *ifname)
{
	struct dvobj_priv *dvobj;
	struct net_device *pnetdev;
	struct net_device *cur_pnetdev;
	struct rereg_nd_name_data *rereg_priv;
	int ret;
	u8 rtnl_lock_needed;

	if (!padapter)
		goto error;

	dvobj = adapter_to_dvobj(padapter);
	cur_pnetdev = padapter->pnetdev;
	rereg_priv = &padapter->rereg_nd_name_priv;

	/* free the old_pnetdev */
	if (rereg_priv->old_pnetdev) {
		free_netdev(rereg_priv->old_pnetdev);
		rereg_priv->old_pnetdev = NULL;
	}

	rtnl_lock_needed = rtw_rtnl_lock_needed(dvobj);

	if (rtnl_lock_needed)
		unregister_netdev(cur_pnetdev);
	else
		unregister_netdevice(cur_pnetdev);

	rereg_priv->old_pnetdev = cur_pnetdev;

	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)  {
		ret = -1;
		goto error;
	}

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(adapter_to_dvobj(padapter)));

	rtw_init_netdev_name(pnetdev, ifname);

	_rtw_memcpy(pnetdev->dev_addr, adapter_mac_addr(padapter), ETH_ALEN);

	if (rtnl_lock_needed)
		ret = register_netdev(pnetdev);
	else
		ret = register_netdevice(pnetdev);

	if (ret != 0) {
		goto error;
	}

	return 0;

error:

	return -1;

}
#endif

#ifdef PLATFORM_FREEBSD
/*
 * Copy a buffer from userspace and write into kernel address
 * space.
 *
 * This emulation just calls the FreeBSD copyin function (to
 * copy data from user space buffer into a kernel space buffer)
 * and is designed to be used with the above io_write_wrapper.
 *
 * This function should return the number of bytes not copied.
 * I.e. success results in a zero value.
 * Negative error values are not returned.
 */
unsigned long
copy_from_user(void *to, const void *from, unsigned long n)
{
	if (copyin(from, to, n) != 0) {
		/* Any errors will be treated as a failure
		   to copy any of the requested bytes */
		return n;
	}

	return 0;
}

unsigned long
copy_to_user(void *to, const void *from, unsigned long n)
{
	if (copyout(from, to, n) != 0) {
		/* Any errors will be treated as a failure
		   to copy any of the requested bytes */
		return n;
	}

	return 0;
}


/*
 * The usb_register and usb_deregister functions are used to register
 * usb drivers with the usb subsystem. In this compatibility layer
 * emulation a list of drivers (struct usb_driver) is maintained
 * and is used for probing/attaching etc.
 *
 * usb_register and usb_deregister simply call these functions.
 */
int
usb_register(struct usb_driver *driver)
{
	rtw_usb_linux_register(driver);
	return 0;
}


int
usb_deregister(struct usb_driver *driver)
{
	rtw_usb_linux_deregister(driver);
	return 0;
}

void module_init_exit_wrapper(void *arg)
{
	int (*func)(void) = arg;
	func();
	return;
}

#endif /* PLATFORM_FREEBSD */

#ifdef CONFIG_PLATFORM_SPRD
	#ifdef do_div
		#undef do_div
	#endif
	#include <asm-generic/div64.h>
#endif

u64 rtw_modular64(u64 x, u64 y)
{
#ifdef PLATFORM_LINUX
	return do_div(x, y);
#elif defined(PLATFORM_WINDOWS)
	return x % y;
#elif defined(PLATFORM_FREEBSD)
	return x % y;
#endif
}

u64 rtw_division64(u64 x, u64 y)
{
#ifdef PLATFORM_LINUX
	do_div(x, y);
	return x;
#elif defined(PLATFORM_WINDOWS)
	return x / y;
#elif defined(PLATFORM_FREEBSD)
	return x / y;
#endif
}

inline u32 rtw_random32(void)
{
#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	return prandom_u32();
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18))
	u32 random_int;
	get_random_bytes(&random_int , 4);
	return random_int;
#else
	return random32();
#endif
#elif defined(PLATFORM_WINDOWS)
#error "to be implemented\n"
#elif defined(PLATFORM_FREEBSD)
#error "to be implemented\n"
#endif
}

void rtw_buf_free(u8 **buf, u32 *buf_len)
{
	u32 ori_len;

	if (!buf || !buf_len)
		return;

	ori_len = *buf_len;

	if (*buf) {
		u32 tmp_buf_len = *buf_len;
		*buf_len = 0;
		rtw_mfree(*buf, tmp_buf_len);
		*buf = NULL;
	}
}

void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len)
{
	u32 ori_len = 0, dup_len = 0;
	u8 *ori = NULL;
	u8 *dup = NULL;

	if (!buf || !buf_len)
		return;

	if (!src || !src_len)
		goto keep_ori;

	/* duplicate src */
	dup = rtw_malloc(src_len);
	if (dup) {
		dup_len = src_len;
		_rtw_memcpy(dup, src, dup_len);
	}

keep_ori:
	ori = *buf;
	ori_len = *buf_len;

	/* replace buf with dup */
	*buf_len = 0;
	*buf = dup;
	*buf_len = dup_len;

	/* free ori */
	if (ori && ori_len > 0)
		rtw_mfree(ori, ori_len);
}


/**
 * rtw_cbuf_full - test if cbuf is full
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: _TRUE if cbuf is full
 */
inline bool rtw_cbuf_full(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read - 1) ? _TRUE : _FALSE;
}

/**
 * rtw_cbuf_empty - test if cbuf is empty
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: _TRUE if cbuf is empty
 */
inline bool rtw_cbuf_empty(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read) ? _TRUE : _FALSE;
}

/**
 * rtw_cbuf_push - push a pointer into cbuf
 * @cbuf: pointer of struct rtw_cbuf
 * @buf: pointer to push in
 *
 * Lock free operation, be careful of the use scheme
 * Returns: _TRUE push success
 */
bool rtw_cbuf_push(struct rtw_cbuf *cbuf, void *buf)
{
	if (rtw_cbuf_full(cbuf))
		return _FAIL;

	if (0)
		RTW_INFO("%s on %u\n", __func__, cbuf->write);
	cbuf->bufs[cbuf->write] = buf;
	cbuf->write = (cbuf->write + 1) % cbuf->size;

	return _SUCCESS;
}

/**
 * rtw_cbuf_pop - pop a pointer from cbuf
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Lock free operation, be careful of the use scheme
 * Returns: pointer popped out
 */
void *rtw_cbuf_pop(struct rtw_cbuf *cbuf)
{
	void *buf;
	if (rtw_cbuf_empty(cbuf))
		return NULL;

	if (0)
		RTW_INFO("%s on %u\n", __func__, cbuf->read);
	buf = cbuf->bufs[cbuf->read];
	cbuf->read = (cbuf->read + 1) % cbuf->size;

	return buf;
}

/**
 * rtw_cbuf_alloc - allocte a rtw_cbuf with given size and do initialization
 * @size: size of pointer
 *
 * Returns: pointer of srtuct rtw_cbuf, NULL for allocation failure
 */
struct rtw_cbuf *rtw_cbuf_alloc(u32 size)
{
	struct rtw_cbuf *cbuf;

	cbuf = (struct rtw_cbuf *)rtw_malloc(sizeof(*cbuf) + sizeof(void *) * size);

	if (cbuf) {
		cbuf->write = cbuf->read = 0;
		cbuf->size = size;
	}

	return cbuf;
}

/**
 * rtw_cbuf_free - free the given rtw_cbuf
 * @cbuf: pointer of struct rtw_cbuf to free
 */
void rtw_cbuf_free(struct rtw_cbuf *cbuf)
{
	rtw_mfree((u8 *)cbuf, sizeof(*cbuf) + sizeof(void *) * cbuf->size);
}

/**
 * map_readN - read a range of map data
 * @map: map to read
 * @offset: start address to read
 * @len: length to read
 * @buf: pointer of buffer to store data read
 *
 * Returns: _SUCCESS or _FAIL
 */
int map_readN(const struct map_t *map, u16 offset, u16 len, u8 *buf)
{
	const struct map_seg_t *seg;
	int ret = _FAIL;
	int i;

	if (len == 0) {
		rtw_warn_on(1);
		goto exit;
	}

	if (offset + len > map->len) {
		rtw_warn_on(1);
		goto exit;
	}

	_rtw_memset(buf, map->init_value, len);

	for (i = 0; i < map->seg_num; i++) {
		u8 *c_dst, *c_src;
		u16 c_len;

		seg = map->segs + i;
		if (seg->sa + seg->len <= offset || seg->sa >= offset + len)
			continue;

		if (seg->sa >= offset) {
			c_dst = buf + (seg->sa - offset);
			c_src = seg->c;
			if (seg->sa + seg->len <= offset + len)
				c_len = seg->len;
			else
				c_len = offset + len - seg->sa;
		} else {
			c_dst = buf;
			c_src = seg->c + (offset - seg->sa);
			if (seg->sa + seg->len >= offset + len)
				c_len = len;
			else
				c_len = seg->sa + seg->len - offset;
		}
			
		_rtw_memcpy(c_dst, c_src, c_len);
	}

exit:
	return ret;
}

/**
 * map_read8 - read 1 byte of map data
 * @map: map to read
 * @offset: address to read
 *
 * Returns: value of data of specified offset. map.init_value if offset is out of range
 */
u8 map_read8(const struct map_t *map, u16 offset)
{
	const struct map_seg_t *seg;
	u8 val = map->init_value;
	int i;

	if (offset + 1 > map->len) {
		rtw_warn_on(1);
		goto exit;
	}

	for (i = 0; i < map->seg_num; i++) {
		seg = map->segs + i;
		if (seg->sa + seg->len <= offset || seg->sa >= offset + 1)
			continue;

		val = *(seg->c + offset - seg->sa);
		break;
	}

exit:
	return val;
}

int rtw_blacklist_add(_queue *blist, const u8 *addr, u32 timeout_ms)
{
	struct blacklist_ent *ent;
	_list *list, *head;
	u8 exist = _FALSE, timeout = _FALSE;

	enter_critical_bh(&blist->lock);

	head = &blist->queue;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		ent = LIST_CONTAINOR(list, struct blacklist_ent, list);
		list = get_next(list);

		if (_rtw_memcmp(ent->addr, addr, ETH_ALEN) == _TRUE) {
			exist = _TRUE;
			if (rtw_time_after(rtw_get_current_time(), ent->exp_time))
				timeout = _TRUE;
			ent->exp_time = rtw_get_current_time()
				+ rtw_ms_to_systime(timeout_ms);
			break;
		}

		if (rtw_time_after(rtw_get_current_time(), ent->exp_time)) {
			rtw_list_delete(&ent->list);
			rtw_mfree(ent, sizeof(struct blacklist_ent));
		}
	}

	if (exist == _FALSE) {
		ent = rtw_malloc(sizeof(struct blacklist_ent));
		if (ent) {
			_rtw_memcpy(ent->addr, addr, ETH_ALEN);
			ent->exp_time = rtw_get_current_time()
				+ rtw_ms_to_systime(timeout_ms);
			rtw_list_insert_tail(&ent->list, head);
		}
	}

	exit_critical_bh(&blist->lock);

	return (exist == _TRUE && timeout == _FALSE) ? RTW_ALREADY : (ent ? _SUCCESS : _FAIL);
}

int rtw_blacklist_del(_queue *blist, const u8 *addr)
{
	struct blacklist_ent *ent = NULL;
	_list *list, *head;
	u8 exist = _FALSE;

	enter_critical_bh(&blist->lock);
	head = &blist->queue;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		ent = LIST_CONTAINOR(list, struct blacklist_ent, list);
		list = get_next(list);

		if (_rtw_memcmp(ent->addr, addr, ETH_ALEN) == _TRUE) {
			rtw_list_delete(&ent->list);
			rtw_mfree(ent, sizeof(struct blacklist_ent));
			exist = _TRUE;
			break;
		}

		if (rtw_time_after(rtw_get_current_time(), ent->exp_time)) {
			rtw_list_delete(&ent->list);
			rtw_mfree(ent, sizeof(struct blacklist_ent));
		}
	}

	exit_critical_bh(&blist->lock);

	return exist == _TRUE ? _SUCCESS : RTW_ALREADY;
}

int rtw_blacklist_search(_queue *blist, const u8 *addr)
{
	struct blacklist_ent *ent = NULL;
	_list *list, *head;
	u8 exist = _FALSE;

	enter_critical_bh(&blist->lock);
	head = &blist->queue;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		ent = LIST_CONTAINOR(list, struct blacklist_ent, list);
		list = get_next(list);

		if (_rtw_memcmp(ent->addr, addr, ETH_ALEN) == _TRUE) {
			if (rtw_time_after(rtw_get_current_time(), ent->exp_time)) {
				rtw_list_delete(&ent->list);
				rtw_mfree(ent, sizeof(struct blacklist_ent));
			} else
				exist = _TRUE;
			break;
		}

		if (rtw_time_after(rtw_get_current_time(), ent->exp_time)) {
			rtw_list_delete(&ent->list);
			rtw_mfree(ent, sizeof(struct blacklist_ent));
		}
	}

	exit_critical_bh(&blist->lock);

	return exist;
}

void rtw_blacklist_flush(_queue *blist)
{
	struct blacklist_ent *ent;
	_list *list, *head;
	_list tmp;

	_rtw_init_listhead(&tmp);

	enter_critical_bh(&blist->lock);
	rtw_list_splice_init(&blist->queue, &tmp);
	exit_critical_bh(&blist->lock);

	head = &tmp;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		ent = LIST_CONTAINOR(list, struct blacklist_ent, list);
		list = get_next(list);
		rtw_list_delete(&ent->list);
		rtw_mfree(ent, sizeof(struct blacklist_ent));
	}
}

void dump_blacklist(void *sel, _queue *blist, const char *title)
{
	struct blacklist_ent *ent = NULL;
	_list *list, *head;

	enter_critical_bh(&blist->lock);
	head = &blist->queue;
	list = get_next(head);

	if (rtw_end_of_queue_search(head, list) == _FALSE) {
		if (title)
			RTW_PRINT_SEL(sel, "%s:\n", title);
	
		while (rtw_end_of_queue_search(head, list) == _FALSE) {
			ent = LIST_CONTAINOR(list, struct blacklist_ent, list);
			list = get_next(list);

			if (rtw_time_after(rtw_get_current_time(), ent->exp_time))
				RTW_PRINT_SEL(sel, MAC_FMT" expired\n", MAC_ARG(ent->addr));
			else
				RTW_PRINT_SEL(sel, MAC_FMT" %u\n", MAC_ARG(ent->addr)
					, rtw_get_remaining_time_ms(ent->exp_time));
		}

	}
	exit_critical_bh(&blist->lock);
}

/**
* is_null -
*
* Return	TRUE if c is null character
*		FALSE otherwise.
*/
inline BOOLEAN is_null(char c)
{
	if (c == '\0')
		return _TRUE;
	else
		return _FALSE;
}

inline BOOLEAN is_all_null(char *c, int len)
{
	for (; len > 0; len--)
		if (c[len - 1] != '\0')
			return _FALSE;

	return _TRUE;
}

/**
* is_eol -
*
* Return	TRUE if c is represent for EOL (end of line)
*		FALSE otherwise.
*/
inline BOOLEAN is_eol(char c)
{
	if (c == '\r' || c == '\n')
		return _TRUE;
	else
		return _FALSE;
}

/**
* is_space -
*
* Return	TRUE if c is represent for space
*		FALSE otherwise.
*/
inline BOOLEAN is_space(char c)
{
	if (c == ' ' || c == '\t')
		return _TRUE;
	else
		return _FALSE;
}

/**
* IsHexDigit -
*
* Return	TRUE if chTmp is represent for hex digit
*		FALSE otherwise.
*/
inline BOOLEAN IsHexDigit(char chTmp)
{
	if ((chTmp >= '0' && chTmp <= '9') ||
		(chTmp >= 'a' && chTmp <= 'f') ||
		(chTmp >= 'A' && chTmp <= 'F'))
		return _TRUE;
	else
		return _FALSE;
}

/**
* is_alpha -
*
* Return	TRUE if chTmp is represent for alphabet
*		FALSE otherwise.
*/
inline BOOLEAN is_alpha(char chTmp)
{
	if ((chTmp >= 'a' && chTmp <= 'z') ||
		(chTmp >= 'A' && chTmp <= 'Z'))
		return _TRUE;
	else
		return _FALSE;
}

inline char alpha_to_upper(char c)
{
	if ((c >= 'a' && c <= 'z'))
		c = 'A' + (c - 'a');
	return c;
}

int hex2num_i(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int hex2byte_i(const char *hex)
{
	int a, b;
	a = hex2num_i(*hex++);
	if (a < 0)
		return -1;
	b = hex2num_i(*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}

int hexstr2bin(const char *hex, u8 *buf, size_t len)
{
	size_t i;
	int a;
	const char *ipos = hex;
	u8 *opos = buf;

	for (i = 0; i < len; i++) {
		a = hex2byte_i(ipos);
		if (a < 0)
			return -1;
		*opos++ = a;
		ipos += 2;
	}
	return 0;
}

