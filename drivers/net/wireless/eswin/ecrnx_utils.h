/**
 * ecrnx_ipc_utils.h
 *
 * IPC utility function declarations
 *
 * Copyright (C) ESWIN 2015-2020
 */
#ifndef _ECRNX_IPC_UTILS_H_
#define _ECRNX_IPC_UTILS_H_

#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>

#include "lmac_msg.h"

enum ecrnx_dev_flag {
    ECRNX_DEV_RESTARTING,
    ECRNX_DEV_STACK_RESTARTING,
    ECRNX_DEV_STARTED,
    ECRNX_DEV_ADDING_STA,
};

struct ecrnx_hw;
struct ecrnx_sta;

/**
 * struct ecrnx_ipc_elem - Generic IPC buffer of fixed size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 */
struct ecrnx_ipc_elem {
    void *addr;
    dma_addr_t dma_addr;
};

/**
 * struct ecrnx_ipc_elem_pool - Generic pool of IPC buffers of fixed size
 *
 * @nb: Number of buffers currenlty allocated in the pool
 * @buf: Array of buffers (size of array is @nb)
 * @pool: DMA pool in which buffers have been allocated
 */
struct ecrnx_ipc_elem_pool {
    int nb;
    struct ecrnx_ipc_elem *buf;
    struct dma_pool *pool;
};

/**
 * struct ecrnx_ipc_elem - Generic IPC buffer of variable size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 * @size: Size, in bytes, of the buffer
 */
struct ecrnx_ipc_elem_var {
    void *addr;
    dma_addr_t dma_addr;
    size_t size;
};

/**
 * struct ecrnx_ipc_dbgdump_elem - IPC buffer for debug dump
 *
 * @mutex: Mutex to protect access to debug dump
 * @buf: IPC buffer
 */
struct ecrnx_ipc_dbgdump_elem {
    struct mutex mutex;
    struct ecrnx_ipc_elem_var buf;
};

//static const u32 ecrnx_rxbuff_pattern = 0xCAFEFADE;
static const u32 ecrnx_rxbuff_pattern = 0xAAAAAA00;


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
 * struct ecrnx_ipc_skb_elem - IPC buffer for SKB element
 *
 * @skb: Pointer to the skb buffer allocated
 * @dma_addr: DMA address of the data buffer fo skb
 *
 */
struct ecrnx_ipc_skb_elem {
    struct sk_buff *skb;
    dma_addr_t dma_addr;
};

#ifdef CONFIG_ECRNX_FULLMAC

/* Maximum number of rx buffer the fw may use at the same time */
#define ECRNX_RXBUFF_MAX (64 * NX_REMOTE_STA_MAX)

/**
 * struct ecrnx_ipc_rxbuf_elems - IPC buffers for RX
 *
 * @skb: Array of buffer push to FW.
 * @idx: Index of the last pushed skb.(Use to find the next free entry quicker)
 *
 * Note: contrary to softmac version, dma_addr are stored inside skb->cb.
 * (cf &struct ecrnx_skb_cb)
 */
struct ecrnx_ipc_rxbuf_elems {
    struct sk_buff *skb[ECRNX_RXBUFF_MAX];
    int idx;
};

/**
 * struct ecrnx_skb_cb - Control Buffer structure for RX buffer
 *
 * @dma_addr: DMA address of the data buffer
 * @pattern: Known pattern (used to check pointer on skb)
 * @idx: Index in &struct ecrnx_hw.rxbuff_table that contains address of this
 * buffer
 */
struct ecrnx_skb_cb {
    dma_addr_t dma_addr;
    uint32_t pattern;
    uint32_t idx;
};

#define ECRNX_RXBUFF_DMA_ADDR_SET(skbuff, addr)          \
    ((struct ecrnx_skb_cb *)(skbuff->cb))->dma_addr = addr
#define ECRNX_RXBUFF_DMA_ADDR_GET(skbuff)                \
    ((struct ecrnx_skb_cb *)(skbuff->cb))->dma_addr

#define ECRNX_RXBUFF_PATTERN_SET(skbuff, pat)                \
    ((struct ecrnx_skb_cb *)(skbuff->cb))->pattern = pat
#define ECRNX_RXBUFF_PATTERN_GET(skbuff)         \
    ((struct ecrnx_skb_cb *)(skbuff->cb))->pattern

#define ECRNX_RXBUFF_IDX_SET(skbuff, val)                \
    ((struct ecrnx_skb_cb *)(skbuff->cb))->idx = val
#define ECRNX_RXBUFF_IDX_GET(skbuff)             \
    ((struct ecrnx_skb_cb *)(skbuff->cb))->idx

#define ECRNX_RXBUFF_VALID_IDX(idx) ((idx) < ECRNX_RXBUFF_MAX)

/* Used to ensure that hostid set to fw is never 0 */
#define ECRNX_RXBUFF_IDX_TO_HOSTID(idx) ((idx) + 1)
#define ECRNX_RXBUFF_HOSTID_TO_IDX(hostid) ((hostid) - 1)

#endif /* CONFIG_ECRNX_FULLMAC */


#ifdef CONFIG_ECRNX_SOFTMAC
int ecrnx_ipc_rxbuf_elem_allocs(struct ecrnx_hw *ecrnx_hw,
                               struct ecrnx_ipc_skb_elem *elem);
void ecrnx_ipc_rxbuf_elem_repush(struct ecrnx_hw *ecrnx_hw,
                                struct ecrnx_ipc_skb_elem *elem);
#else
int ecrnx_ipc_rxbuf_elem_allocs(struct ecrnx_hw *ecrnx_hw);
void ecrnx_ipc_rxbuf_elem_pull(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb);
void ecrnx_ipc_rxbuf_elem_sync(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb,
                              int len);
void ecrnx_ipc_rxdesc_elem_repush(struct ecrnx_hw *ecrnx_hw,
                                 struct ecrnx_ipc_elem *elem);
void ecrnx_ipc_rxbuf_elem_repush(struct ecrnx_hw *ecrnx_hw,
                                struct sk_buff *skb);
#endif /* CONFIG_ECRNX_SOFTMAC */

void ecrnx_printf(const char *fmt, ...);
void ecrnx_ipc_msg_push(struct ecrnx_hw *ecrnx_hw, void *msg_buf, uint16_t len);
void ecrnx_ipc_txdesc_push(struct ecrnx_hw *ecrnx_hw, void *tx_desc,
                          void *hostid, int hw_queue, int user);
void *ecrnx_ipc_fw_trace_desc_get(struct ecrnx_hw *ecrnx_hw);
int ecrnx_ipc_rxbuf_init(struct ecrnx_hw *ecrnx_hw, uint32_t rx_bufsz);
int ecrnx_ipc_init(struct ecrnx_hw *ecrnx_hw, u8 *shared_ram);
void ecrnx_ipc_deinit(struct ecrnx_hw *ecrnx_hw);
void ecrnx_ipc_start(struct ecrnx_hw *ecrnx_hw);
void ecrnx_ipc_stop(struct ecrnx_hw *ecrnx_hw);
void ecrnx_ipc_tx_drain(struct ecrnx_hw *ecrnx_hw);
bool ecrnx_ipc_tx_pending(struct ecrnx_hw *ecrnx_hw);

struct ipc_host_env_tag;
int ecrnx_ipc_elem_var_allocs(struct ecrnx_hw *ecrnx_hw,
                             struct ecrnx_ipc_elem_var *elem, size_t elem_size,
                             enum dma_data_direction dir,
                             void *buf, const void *init,
                             void (*push)(struct ipc_host_env_tag *, uint32_t));
void ecrnx_ipc_elem_var_deallocs(struct ecrnx_hw *ecrnx_hw,
                                struct ecrnx_ipc_elem_var *elem);
int ecrnx_ipc_unsup_rx_vec_elem_allocs(struct ecrnx_hw *ecrnx_hw,
                                      struct ecrnx_ipc_skb_elem *elem);

void ecrnx_error_ind(struct ecrnx_hw *ecrnx_hw);
void ecrnx_umh_done(struct ecrnx_hw *ecrnx_hw);

void ecrnx_ipc_sta_buffer_init(struct ecrnx_hw *ecrnx_hw, int sta_idx);
void ecrnx_ipc_sta_buffer(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta, int tid, int size);

#endif /* _ECRNX_IPC_UTILS_H_ */
