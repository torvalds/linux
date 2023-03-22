/**
 * rwnx_ipc_utils.h
 *
 * IPC utility function declarations
 *
 * Copyright (C) RivieraWaves 2012-2019
 */
#ifndef _RWNX_IPC_UTILS_H_
#define _RWNX_IPC_UTILS_H_

#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>

#include "lmac_msg.h"
#include "aicwf_debug.h"

#if 0
#ifdef CONFIG_RWNX_DBG
/*  #define RWNX_DBG(format, arg...) pr_warn(format, ## arg) */
#define RWNX_DBG printk
#else
#define RWNX_DBG(a...) do {} while (0)
#endif
#endif



enum rwnx_dev_flag {
    RWNX_DEV_RESTARTING,
    RWNX_DEV_STACK_RESTARTING,
    RWNX_DEV_STARTED,
};

struct rwnx_hw;
struct rwnx_sta;

/**
 * struct rwnx_ipc_elem - Generic IPC buffer of fixed size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 */
struct rwnx_ipc_elem {
    void *addr;
    dma_addr_t dma_addr;
};

/**
 * struct rwnx_ipc_elem_pool - Generic pool of IPC buffers of fixed size
 *
 * @nb: Number of buffers currenlty allocated in the pool
 * @buf: Array of buffers (size of array is @nb)
 * @pool: DMA pool in which buffers have been allocated
 */
struct rwnx_ipc_elem_pool {
    int nb;
    struct rwnx_ipc_elem *buf;
    struct dma_pool *pool;
};

/**
 * struct rwnx_ipc_elem - Generic IPC buffer of variable size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 * @size: Size, in bytes, of the buffer
 */
struct rwnx_ipc_elem_var {
    void *addr;
    dma_addr_t dma_addr;
    size_t size;
};

/**
 * struct rwnx_ipc_dbgdump_elem - IPC buffer for debug dump
 *
 * @mutex: Mutex to protect access to debug dump
 * @buf: IPC buffer
 */
struct rwnx_ipc_dbgdump_elem {
    struct mutex mutex;
    struct rwnx_ipc_elem_var buf;
};

static const u32 rwnx_rxbuff_pattern = 0xCAFEFADE;

/*
 * Maximum Length of Radiotap header vendor specific data(in bytes)
 */
#define RADIOTAP_HDR_VEND_MAX_LEN   16

/*
 * Maximum Radiotap Header Length without vendor specific data (in bytes)
 */
#define RADIOTAP_HDR_MAX_LEN        80

/*
 * Unsupported HT Frame data length (in bytes)
 */
#define UNSUP_RX_VEC_DATA_LEN       2

/**
 * struct rwnx_ipc_skb_elem - IPC buffer for SKB element
 *
 * @skb: Pointer to the skb buffer allocated
 * @dma_addr: DMA address of the data buffer fo skb
 *
 */
struct rwnx_ipc_skb_elem {
    struct sk_buff *skb;
    dma_addr_t dma_addr;
};

#ifdef CONFIG_RWNX_FULLMAC

/* Maximum number of rx buffer the fw may use at the same time */
#define RWNX_RXBUFF_MAX (64 * NX_REMOTE_STA_MAX)

/**
 * struct rwnx_ipc_rxbuf_elems - IPC buffers for RX
 *
 * @skb: Array of buffer push to FW.
 * @idx: Index of the last pushed skb.(Use to find the next free entry quicker)
 *
 * Note: contrary to softmac version, dma_addr are stored inside skb->cb.
 * (cf &struct rwnx_skb_cb)
 */
struct rwnx_ipc_rxbuf_elems {
    struct sk_buff *skb[RWNX_RXBUFF_MAX];
    int idx;
};

#endif /* CONFIG_RWNX_FULLMAC */
#endif /* _RWNX_IPC_UTILS_H_ */
