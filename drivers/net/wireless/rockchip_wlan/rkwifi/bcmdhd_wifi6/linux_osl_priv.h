/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Private header file for Linux OS Independent Layer
 *
 * Copyright (C) 1999-2019, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: linux_osl_priv.h 794159 2018-12-12 07:41:14Z $
 */

#ifndef _LINUX_OSL_PRIV_H_
#define _LINUX_OSL_PRIV_H_

#define OS_HANDLE_MAGIC		0x1234abcd	/* Magic # to recognize osh */
#define BCM_MEM_FILENAME_LEN	24		/* Mem. filename length */

/* dependancy check */
#if !defined(BCMPCIE) && defined(DHD_USE_STATIC_CTRLBUF)
#error "DHD_USE_STATIC_CTRLBUF suppored PCIE target only"
#endif /* !BCMPCIE && DHD_USE_STATIC_CTRLBUF */

#ifdef CONFIG_DHD_USE_STATIC_BUF
#ifdef DHD_USE_STATIC_CTRLBUF
#define DHD_SKB_1PAGE_BUFSIZE	(PAGE_SIZE*1)
#define DHD_SKB_2PAGE_BUFSIZE	(PAGE_SIZE*2)
#define DHD_SKB_4PAGE_BUFSIZE	(PAGE_SIZE*4)

#define PREALLOC_FREE_MAGIC	0xFEDC
#define PREALLOC_USED_MAGIC	0xFCDE
#else
#define DHD_SKB_HDRSIZE		336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)
#endif /* DHD_USE_STATIC_CTRLBUF */

#define STATIC_BUF_MAX_NUM	16
#define STATIC_BUF_SIZE	(PAGE_SIZE*2)
#define STATIC_BUF_TOTAL_LEN	(STATIC_BUF_MAX_NUM * STATIC_BUF_SIZE)

typedef struct bcm_static_buf {
	spinlock_t static_lock;
	unsigned char *buf_ptr;
	unsigned char buf_use[STATIC_BUF_MAX_NUM];
} bcm_static_buf_t;

extern bcm_static_buf_t *bcm_static_buf;

#ifdef DHD_USE_STATIC_CTRLBUF
#define STATIC_PKT_4PAGE_NUM	0
#define DHD_SKB_MAX_BUFSIZE	DHD_SKB_2PAGE_BUFSIZE
#elif defined(ENHANCED_STATIC_BUF)
#define STATIC_PKT_4PAGE_NUM	1
#define DHD_SKB_MAX_BUFSIZE	DHD_SKB_4PAGE_BUFSIZE
#else
#define STATIC_PKT_4PAGE_NUM	0
#define DHD_SKB_MAX_BUFSIZE	DHD_SKB_2PAGE_BUFSIZE
#endif /* DHD_USE_STATIC_CTRLBUF */

#ifdef DHD_USE_STATIC_CTRLBUF
#define STATIC_PKT_1PAGE_NUM	0
#define STATIC_PKT_2PAGE_NUM	128
#else
#define STATIC_PKT_1PAGE_NUM	8
#define STATIC_PKT_2PAGE_NUM	8
#endif /* DHD_USE_STATIC_CTRLBUF */

#define STATIC_PKT_1_2PAGE_NUM	\
	((STATIC_PKT_1PAGE_NUM) + (STATIC_PKT_2PAGE_NUM))
#define STATIC_PKT_MAX_NUM	\
	((STATIC_PKT_1_2PAGE_NUM) + (STATIC_PKT_4PAGE_NUM))

typedef struct bcm_static_pkt {
#ifdef DHD_USE_STATIC_CTRLBUF
	struct sk_buff *skb_8k[STATIC_PKT_2PAGE_NUM];
	unsigned char pkt_invalid[STATIC_PKT_2PAGE_NUM];
	spinlock_t osl_pkt_lock;
	uint32 last_allocated_index;
#else
	struct sk_buff *skb_4k[STATIC_PKT_1PAGE_NUM];
	struct sk_buff *skb_8k[STATIC_PKT_2PAGE_NUM];
#ifdef ENHANCED_STATIC_BUF
	struct sk_buff *skb_16k;
#endif /* ENHANCED_STATIC_BUF */
	struct semaphore osl_pkt_sem;
#endif /* DHD_USE_STATIC_CTRLBUF */
	unsigned char pkt_use[STATIC_PKT_MAX_NUM];
} bcm_static_pkt_t;

extern bcm_static_pkt_t *bcm_static_skb;
#endif /* CONFIG_DHD_USE_STATIC_BUF */

typedef struct bcm_mem_link {
	struct bcm_mem_link *prev;
	struct bcm_mem_link *next;
	uint	size;
	int	line;
	void 	*osh;
	char	file[BCM_MEM_FILENAME_LEN];
} bcm_mem_link_t;

struct osl_cmn_info {
	atomic_t malloced;
	atomic_t pktalloced;    /* Number of allocated packet buffers */
	spinlock_t dbgmem_lock;
	bcm_mem_link_t *dbgmem_list;
	bcm_mem_link_t *dbgvmem_list;
	spinlock_t pktalloc_lock;
	atomic_t refcount; /* Number of references to this shared structure. */
};
typedef struct osl_cmn_info osl_cmn_t;

#if defined(BCM_BACKPLANE_TIMEOUT)
typedef uint32 (*bpt_cb_fn)(void *ctx, void *addr);
#endif	/* BCM_BACKPLANE_TIMEOUT */

struct osl_info {
	osl_pubinfo_t pub;
	uint32  flags;		/* If specific cases to be handled in the OSL */
	uint magic;
	void *pdev;
	uint failed;
	uint bustype;
	osl_cmn_t *cmn; /* Common OSL related data shred between two OSH's */

	void *bus_handle;
#ifdef	BCM_SECURE_DMA
#ifdef NOT_YET
	struct sec_mem_elem *sec_list_512;
	struct sec_mem_elem *sec_list_base_512;
	struct sec_mem_elem *sec_list_2048;
	struct sec_mem_elem *sec_list_base_2048;
#endif /* NOT_YET */
	struct sec_mem_elem *sec_list_4096;
	struct sec_mem_elem *sec_list_base_4096;
	phys_addr_t  contig_base;
	void *contig_base_va;
	phys_addr_t  contig_base_alloc;
	void *contig_base_alloc_va;
	phys_addr_t contig_base_alloc_coherent;
	void *contig_base_alloc_coherent_va;
	void *contig_base_coherent_va;
	void *contig_delta_va_pa;
	struct {
		phys_addr_t pa;
		void *va;
		bool avail;
	} sec_cma_coherent[SEC_CMA_COHERENT_MAX];
	int stb_ext_params;
#endif /* BCM_SECURE_DMA */
#if defined(BCM_BACKPLANE_TIMEOUT)
	bpt_cb_fn bpt_cb;
	void *sih;
#endif	/* BCM_BACKPLANE_TIMEOUT */
#ifdef USE_DMA_LOCK
	spinlock_t dma_lock;
	bool dma_lock_bh;
#endif /* USE_DMA_LOCK */
#ifdef DHD_MAP_LOGGING
	void *dhd_map_log;
	void *dhd_unmap_log;
#endif /* DHD_MAP_LOGGING */
};

#endif /* _LINUX_OSL_PRIV_H_ */
