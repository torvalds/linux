/**
 * ecrnx_utils.c
 *
 * IPC utility function definitions
 *
 * Copyright (C) ESWIN 2015-2020
 */
#include "ecrnx_utils.h"
#include "ecrnx_defs.h"
#include "ecrnx_rx.h"
#include "ecrnx_tx.h"
#include "ecrnx_msg_rx.h"
#include "ecrnx_debugfs.h"
#include "ecrnx_prof.h"
#include "ipc_host.h"

#ifdef CONFIG_ECRNX_ESWIN_SDIO
#include "eswin_utils.h"
#include "ecrnx_sdio.h"
#include "sdio.h"
#elif defined(CONFIG_ECRNX_ESWIN_USB)
#include "eswin_utils.h"
#include "ecrnx_usb.h"
#include "usb.h"
#endif


/**
 * ecrnx_ipc_elem_pool_allocs() - Allocate and push to fw a pool of buffer.
 *
 * @ecrnx_hw: Main driver structure
 * @pool: Pool to allocate
 * @nb: Size of the pool to allocate
 * @elem_size: SIze of one pool element
 * @pool_name: Name of the pool
 * @push: Function to push one pool element to fw
 *
 * This function will allocate an array to store the list of element addresses,
 * a dma pool and @nb element in the dma pool.
 * Each element is set with '0' and then push to fw using the @push function.
 * It assumes that pointer inside @ipc parameter are set to NULL at start.
 *
 * Return: 0 on success and <0 upon error. If error is returned any allocated
 * memory is NOT freed and ecrnx_ipc_elem_pool_deallocs() must be called.
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_ipc_elem_pool_allocs(struct ecrnx_hw *ecrnx_hw,
                                     struct ecrnx_ipc_elem_pool *pool,
                                     int nb, size_t elem_size, char *pool_name,
                                     int (*push)(struct ipc_host_env_tag *,
                                                 void *, uint32_t))
{
    struct ecrnx_ipc_elem *buf;
    int i;

    pool->nb = 0;

    /* allocate buf array */
    pool->buf = kmalloc(nb * sizeof(struct ecrnx_ipc_elem), GFP_KERNEL);
    if (!pool->buf) {
        dev_err(ecrnx_hw->dev, "Allocation of buffer array for %s failed\n",
                pool_name);
        return -ENOMEM;
    }

    /* allocate dma pool */
    pool->pool = dma_pool_create(pool_name, ecrnx_hw->dev, elem_size,
                                 cache_line_size(), 0);
    if (!pool->pool) {
        dev_err(ecrnx_hw->dev, "Allocation of dma pool %s failed\n",
                pool_name);
        return -ENOMEM;
    }

    for (i = 0, buf = pool->buf; i < nb; buf++, i++) {

        /* allocate an elem */
        buf->addr = dma_pool_alloc(pool->pool, GFP_KERNEL, &buf->dma_addr);
        if (!buf->addr) {
            dev_err(ecrnx_hw->dev, "Allocation of block %d/%d in %s failed\n",
                    (i + 1), nb, pool_name);
            return -ENOMEM;
        }
        pool->nb++;

        /* reset the element */
        memset(buf->addr, 0, elem_size);

        /* push it to FW */
        push(ecrnx_hw->ipc_env, buf, (uint32_t)buf->dma_addr);
    }

    return 0;
}
#endif
/**
 * ecrnx_ipc_elem_pool_deallocs() - Free all memory allocated for a pool
 *
 * @pool: Pool to free
 *
 * Must be call once after ecrnx_ipc_elem_pool_allocs(), even if it returned
 * an error
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_ipc_elem_pool_deallocs(struct ecrnx_ipc_elem_pool *pool)
{
    struct ecrnx_ipc_elem *buf;
    int i;

    for (i = 0, buf = pool->buf; i < pool->nb ; buf++, i++) {
        dma_pool_free(pool->pool, buf->addr, buf->dma_addr);
    }
    pool->nb = 0;

    if (pool->pool)
        dma_pool_destroy(pool->pool);
    pool->pool = NULL;

    if (pool->buf)
        kfree(pool->buf);
    pool->buf = NULL;
}
#endif
/**
 * ecrnx_ipc_elem_var_allocs - Alloc a single ipc buffer and push it to fw
 *
 * @ecrnx_hw: Main driver structure
 * @elem: Element to allocate
 * @elem_size: Size of the element to allcoate
 * @dir: DMA direction
 * @buf: If not NULL, used this buffer instead of allocating a new one. It must
 * be @elem_size long and be allocated by kmalloc as kfree will be called.
 * @init: Pointer to initial data to write in buffer before DMA sync. Needed
 * only if direction is DMA_TO_DEVICE. If set it is assume that its size is
 * @elem_size.
 * @push: Function to push the element to fw. May be set to NULL.
 *
 * It allocates a buffer (or use the one provided with @buf), initializes it if
 * @init is set, map buffer for DMA transfer, initializes @elem and push buffer
 * to FW if @push is seet.
 *
 * Return: 0 on success and <0 upon error. If error is returned any allocated
 * memory has been freed (including @buf if set).
 */
int ecrnx_ipc_elem_var_allocs(struct ecrnx_hw *ecrnx_hw,
                             struct ecrnx_ipc_elem_var *elem, size_t elem_size,
                             enum dma_data_direction dir,
                             void *buf, const void *init,
                             void (*push)(struct ipc_host_env_tag *, uint32_t))
{
    if (buf) {
        elem->addr = buf;
    } else {
        elem->addr = kmalloc(elem_size, GFP_KERNEL);
        if (!elem->addr) {
            dev_err(ecrnx_hw->dev, "Allocation of ipc buffer failed\n");
            return -ENOMEM;
        }
    }
    elem->size = elem_size;

    if ((dir == DMA_TO_DEVICE) && init) {
        memcpy(elem->addr, init, elem_size);
    }

#ifdef CONFIG_ECRNX_ESWIN
    elem->dma_addr = (ptr_addr)elem->addr;
#else
    elem->dma_addr = dma_map_single(ecrnx_hw->dev, elem->addr, elem_size, dir);
    if (dma_mapping_error(ecrnx_hw->dev, elem->dma_addr)) {
        dev_err(ecrnx_hw->dev, "DMA mapping failed\n");
        kfree(elem->addr);
        elem->addr = NULL;
        return -EIO;
    }

    if (push)
        push(ecrnx_hw->ipc_env, elem->dma_addr);
#endif
    return 0;
}

/**
 * ecrnx_ipc_elem_var_deallocs() - Free memory allocated for a single ipc buffer
 *
 * @ecrnx_hw: Main driver structure
 * @elem: Element to free
 */
void ecrnx_ipc_elem_var_deallocs(struct ecrnx_hw *ecrnx_hw,
                                struct ecrnx_ipc_elem_var *elem)
{
    if (!elem->addr)
        return;
#ifndef CONFIG_ECRNX_ESWIN
    dma_unmap_single(ecrnx_hw->dev, elem->dma_addr, elem->size, DMA_TO_DEVICE);
#endif
    kfree(elem->addr);
    elem->addr = NULL;
}

/**
 * ecrnx_ipc_skb_elem_allocs() - Allocate and push a skb buffer for the FW
 *
 * @ecrnx_hw: Main driver data
 * @elem: Pointer to the skb elem that will contain the address of the buffer
 */
int ecrnx_ipc_skb_elem_allocs(struct ecrnx_hw *ecrnx_hw,
                                 struct ecrnx_ipc_skb_elem *elem, size_t skb_size,
                                 enum dma_data_direction dir,
                                 int (*push)(struct ipc_host_env_tag *,
                                             void *, uint32_t))
{
    elem->skb = dev_alloc_skb(skb_size);
    if (unlikely(!elem->skb)) {
        dev_err(ecrnx_hw->dev, "Allocation of ipc skb failed\n");
        return -ENOMEM;
    }

    elem->dma_addr = dma_map_single(ecrnx_hw->dev, elem->skb->data, skb_size, dir);
    if (unlikely(dma_mapping_error(ecrnx_hw->dev, elem->dma_addr))) {
        dev_err(ecrnx_hw->dev, "DMA mapping failed\n");
        dev_kfree_skb(elem->skb);
        elem->skb = NULL;
        return -EIO;
    }

    if (push){
        push(ecrnx_hw->ipc_env, elem, elem->dma_addr);
    }
    return 0;
}

/**
 * ecrnx_ipc_skb_elem_deallocs() - Free a skb buffer allocated for the FW
 *
 * @ecrnx_hw: Main driver data
 * @elem: Pointer to the skb elem that contains the address of the buffer
 * @skb_size: size of the skb buffer data
 * @dir: DMA direction
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_ipc_skb_elem_deallocs(struct ecrnx_hw *ecrnx_hw,
                                       struct ecrnx_ipc_skb_elem *elem,
                                       size_t skb_size, enum dma_data_direction dir) 
{
    if (elem->skb) {
        dma_unmap_single(ecrnx_hw->dev, elem->dma_addr, skb_size, dir);
        dev_kfree_skb(elem->skb);
        elem->skb = NULL;
    }
}
#endif
/**
 * ecrnx_ipc_unsup_rx_vec_elem_allocs() - Allocate and push an unsupported
 *                                       RX vector buffer for the FW
 *
 * @ecrnx_hw: Main driver data
 * @elem: Pointer to the skb elem that will contain the address of the buffer
 */
#ifndef CONFIG_ECRNX_ESWIN
int ecrnx_ipc_unsup_rx_vec_elem_allocs(struct ecrnx_hw *ecrnx_hw,
                                      struct ecrnx_ipc_skb_elem *elem)
{
    struct rx_vector_desc *rxdesc;

    if (ecrnx_ipc_skb_elem_allocs(ecrnx_hw, elem,
            ecrnx_hw->ipc_env->unsuprxvec_bufsz, DMA_FROM_DEVICE, NULL))
        return -ENOMEM;

    rxdesc = (struct rx_vector_desc *) elem->skb->data;
    rxdesc->pattern = 0;
    dma_sync_single_for_device(ecrnx_hw->dev,
                        elem->dma_addr + offsetof(struct rx_vector_desc, pattern),
                        sizeof(rxdesc->pattern), DMA_BIDIRECTIONAL);

    ipc_host_unsup_rx_vec_buf_push(ecrnx_hw->ipc_env, elem, (u32) elem->dma_addr);

    return 0;
}
#endif

/**
 * ecrnx_ipc_rxbuf_elems_deallocs() - Free all unsupported rx vector buffer
 *                                   allocated for the FW
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_ipc_unsup_rx_vec_elems_deallocs(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_ipc_skb_elem *elem;
    int i, nb = ecrnx_hw->ipc_env->unsuprxvec_bufnb;

    if (!ecrnx_hw->e2aunsuprxvec_elems)
        return;

    for (i = 0, elem = ecrnx_hw->e2aunsuprxvec_elems; i < nb; i++, elem++) {
        ecrnx_ipc_skb_elem_deallocs(ecrnx_hw, elem, ecrnx_hw->ipc_env->unsuprxvec_bufsz, DMA_FROM_DEVICE);
    }

    kfree(ecrnx_hw->e2aunsuprxvec_elems);
    ecrnx_hw->e2aunsuprxvec_elems = NULL;
}
#endif

/**
* ecrnx_ipc_unsup_rx_vec_elems_allocs() - Allocate and push all unsupported RX
*                                        vector buffer for the FW
*
* @ecrnx_hw: Main driver data
*/
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_ipc_unsup_rx_vec_elems_allocs(struct ecrnx_hw *ecrnx_hw)
{
   struct ecrnx_ipc_skb_elem *elem;
   int i, nb = ecrnx_hw->ipc_env->unsuprxvec_bufnb;

   ecrnx_hw->e2aunsuprxvec_elems = kzalloc(nb * sizeof(struct ecrnx_ipc_skb_elem),
                                   GFP_KERNEL);
   if (!ecrnx_hw->e2aunsuprxvec_elems) {
       dev_err(ecrnx_hw->dev, "Failed to allocate unsuprxvec_elems\n");
       return -ENOMEM;
   }

   for (i = 0, elem = ecrnx_hw->e2aunsuprxvec_elems; i < nb; i++, elem++)
   {
       if (ecrnx_ipc_unsup_rx_vec_elem_allocs(ecrnx_hw, elem)) {
           dev_err(ecrnx_hw->dev, "Failed to allocate unsuprxvec buf %d/%d\n",
                   i + 1, nb);
           return -ENOMEM;
       }
   }
   return 0;
}
#endif

#ifdef CONFIG_ECRNX_SOFTMAC
/**
 * ecrnx_ipc_rxbuf_elem_allocs() - Allocate and push a rx buffer for the FW
 *
 * @ecrnx_hw: Main driver data
 * @elem: Pointer to the skb elem that will contain the address of the buffer
 */
#ifndef CONFIG_ECRNX_ESWIN
int ecrnx_ipc_rxbuf_elem_allocs(struct ecrnx_hw *ecrnx_hw,
                               struct ecrnx_ipc_skb_elem *elem)
{
    struct hw_rxhdr *hw_rxhdr;

    if (ecrnx_ipc_skb_elem_allocs(ecrnx_hw, elem,
            ecrnx_hw->ipc_env->rx_bufsz, DMA_FROM_DEVICE, NULL))
        return -ENOMEM;

    hw_rxhdr = (struct hw_rxhdr *) elem->skb->data;
    hw_rxhdr->pattern = 0;
    dma_sync_single_for_device(ecrnx_hw->dev,
            elem->dma_addr + offsetof(struct hw_rxhdr, pattern),
            sizeof(hw_rxhdr->pattern), DMA_BIDIRECTIONAL);

    ipc_host_rxbuf_push(ecrnx_hw->ipc_env, elem, (u32) elem->dma_addr);

    return 0;
}
#endif

/**
 * ecrnx_ipc_rxbuf_elem_repush() - Reset and repush an already allocated RX buffer
 *
 * @ecrnx_hw: Main driver data
 * @elem: Pointer to the skb elem that contains the address of the buffer
 */
#ifndef CONFIG_ECRNX_ESWIN
void ecrnx_ipc_rxbuf_elem_repush(struct ecrnx_hw *ecrnx_hw,
                                struct ecrnx_ipc_skb_elem *elem)
{
    struct sk_buff *skb = elem->skb;
    int pattern_offset = sizeof(struct hw_rxhdr);

    ((struct hw_rxhdr *)skb->data)->pattern = 0;
    dma_sync_single_for_device(ecrnx_hw->dev, elem->dma_addr,
                               pattern_offset, DMA_BIDIRECTIONAL);
    ipc_host_rxbuf_push(ecrnx_hw->ipc_env, elem, (u32)elem->dma_addr);
}
#endif

/**
 * ecrnx_ipc_rxbuf_elems_allocs() - Allocate and push all RX buffer for the FW
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_ipc_rxbuf_elems_allocs(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_ipc_skb_elem *elem;
    int i, nb = ecrnx_hw->ipc_env->rx_bufnb;

    ecrnx_hw->rxbuf_elems = kzalloc(nb * sizeof(struct ecrnx_ipc_skb_elem),
                                GFP_KERNEL);
    if (!ecrnx_hw->rxbuf_elems) {
        dev_err(ecrnx_hw->dev, "Failed to allocate rx_elems\n");
        return -ENOMEM;
    }

    for (i = 0, elem = ecrnx_hw->rxbuf_elems; i < nb; i++, elem++) {
        if (ecrnx_ipc_rxbuf_elem_allocs(ecrnx_hw, elem)) {
            dev_err(ecrnx_hw->dev, "Failed to allocate rx buf %d/%d\n",
                    i + 1, nb);
            return -ENOMEM;
        }
    }

    return 0;
}
#endif

/**
 * ecrnx_ipc_rxbuf_elems_deallocs() - Free all RX buffer allocated for the FW
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_ipc_rxbuf_elems_deallocs(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_ipc_skb_elem *elem;
    int i, nb = ecrnx_hw->ipc_env->rx_bufnb;

    if (!ecrnx_hw->rxbuf_elems)
        return;

    for (i = 0, elem = ecrnx_hw->rxbuf_elems; i < nb; i++, elem++) {
        ecrnx_ipc_skb_elem_deallocs(ecrnx_hw, elem, ecrnx_hw->ipc_env->rx_bufsz, DMA_FROM_DEVICE);
    }

    kfree(ecrnx_hw->rxbuf_elems);
    ecrnx_hw->rxbuf_elems = NULL;   
}
#endif 

#else /* ! CONFIG_ECRNX_SOFTMAC */

/**
 * ecrnx_ipc_rxdesc_elem_repush() - Repush a rxdesc to FW
 *
 * @ecrnx_hw: Main driver data
 * @elem: Rx desc to repush
 *
 * Once rx buffer has been received, the rxdesc used by FW to upload this
 * buffer can be re-used for another rx buffer.
 */
#ifndef CONFIG_ECRNX_ESWIN
void ecrnx_ipc_rxdesc_elem_repush(struct ecrnx_hw *ecrnx_hw,
                                 struct ecrnx_ipc_elem *elem)
{
    struct rxdesc_tag *rxdesc = elem->addr;
    rxdesc->status = 0;
    dma_sync_single_for_device(ecrnx_hw->dev, elem->dma_addr,
                               sizeof(struct rxdesc_tag), DMA_BIDIRECTIONAL);
    ipc_host_rxdesc_push(ecrnx_hw->ipc_env, elem, (u32)elem->dma_addr);
}
#endif
/**
 * ecrnx_ipc_rxbuf_elem_allocs() - Allocate and push a RX buffer for the FW
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
int ecrnx_ipc_rxbuf_elem_allocs(struct ecrnx_hw *ecrnx_hw)
{
    struct sk_buff *skb;
    struct hw_rxhdr *hw_rxhdr;
    dma_addr_t dma_addr;
    int size = ecrnx_hw->ipc_env->rx_bufsz;
    int nb, idx;

    skb = dev_alloc_skb(size);
    if (unlikely(!skb)) {
        dev_err(ecrnx_hw->dev, "Failed to allocate rx buffer\n");
        return -ENOMEM;
    }

    dma_addr = dma_map_single(ecrnx_hw->dev, skb->data, size, DMA_FROM_DEVICE);

    if (unlikely(dma_mapping_error(ecrnx_hw->dev, dma_addr))) {
        dev_err(ecrnx_hw->dev, "Failed to map rx buffer\n");
        goto err_skb;
    }

    hw_rxhdr = (struct hw_rxhdr *)skb->data;
    hw_rxhdr->pattern = 0;
    dma_sync_single_for_device(ecrnx_hw->dev,
                               dma_addr + offsetof(struct hw_rxhdr, pattern),
                               sizeof(hw_rxhdr->pattern), DMA_BIDIRECTIONAL);

    /* Find first free slot */
    nb = 0;
    idx = ecrnx_hw->rxbuf_elems.idx;
    while (ecrnx_hw->rxbuf_elems.skb[idx] && nb < ECRNX_RXBUFF_MAX) {
        idx = ( idx + 1 ) % ECRNX_RXBUFF_MAX;
        nb++;
    }

    if (WARN((nb == ECRNX_RXBUFF_MAX), "No more free space for rxbuff")) {
        goto err_dma;
    }

    ecrnx_hw->rxbuf_elems.skb[idx] = skb;

    /* Save info in skb control buffer  */
    ECRNX_RXBUFF_DMA_ADDR_SET(skb, dma_addr);
    ECRNX_RXBUFF_PATTERN_SET(skb, ecrnx_rxbuff_pattern);
    ECRNX_RXBUFF_IDX_SET(skb, idx);

    /* Push buffer to FW */
    ipc_host_rxbuf_push(ecrnx_hw->ipc_env, ECRNX_RXBUFF_IDX_TO_HOSTID(idx),
                        dma_addr);

    /* Save idx so that on next push the free slot will be found quicker */
    ecrnx_hw->rxbuf_elems.idx = ( idx + 1 ) % ECRNX_RXBUFF_MAX;

    return 0;

  err_dma:
    dma_unmap_single(ecrnx_hw->dev, dma_addr, size, DMA_FROM_DEVICE);
  err_skb:
    dev_kfree_skb(skb);
    return -ENOMEM;

    return 0;
}
#endif

/**
 * ecrnx_ipc_rxbuf_elem_repush() - Repush a rxbuf to FW
 *
 * @ecrnx_hw: Main driver data
 * @skb: Skb to repush
 *
 * In case a skb is not forwarded to upper layer it can be re-used.
 * It is assumed that @skb has been verified before calling this function and
 * that it is a valid rx buffer
 * (i.e. skb == ecrnx_hw->rxbuf_elems.skb[ECRNX_RXBUFF_IDX_GET(skb)])
 */
#ifndef CONFIG_ECRNX_ESWIN
void ecrnx_ipc_rxbuf_elem_repush(struct ecrnx_hw *ecrnx_hw,
                                struct sk_buff *skb)
{
    dma_addr_t dma_addr;
    struct hw_rxhdr *hw_rxhdr = (struct hw_rxhdr *)skb->data;
    int idx;

    /* reset pattern */
    hw_rxhdr->pattern = 0;
    dma_addr = ECRNX_RXBUFF_DMA_ADDR_GET(skb);
    dma_sync_single_for_device(ecrnx_hw->dev,
                               dma_addr + offsetof(struct hw_rxhdr, pattern),
                               sizeof(hw_rxhdr->pattern), DMA_BIDIRECTIONAL);

    /* re-push buffer to FW */
    idx = ECRNX_RXBUFF_IDX_GET(skb);
 
   ipc_host_rxbuf_push(ecrnx_hw->ipc_env, ECRNX_RXBUFF_IDX_TO_HOSTID(idx),
                       dma_addr);
}
#endif

/**
 * ecrnx_ipc_rxbuf_elems_allocs() - Allocate and push all RX buffer for the FW
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_ipc_rxbuf_elems_allocs(struct ecrnx_hw *ecrnx_hw)
{
    //int i, nb = ecrnx_hw->ipc_env->rx_bufnb;
    int i, nb = 0;

    for (i = 0; i < ECRNX_RXBUFF_MAX; i++) {
        ecrnx_hw->rxbuf_elems.skb[i] = NULL;
    }
    ecrnx_hw->rxbuf_elems.idx = 0;

    for (i = 0; i < nb; i++) {
        if (ecrnx_ipc_rxbuf_elem_allocs(ecrnx_hw)) {
            dev_err(ecrnx_hw->dev, "Failed to allocate rx buf %d/%d\n",
                    i + 1, nb);
            return -ENOMEM;
        }
    }
    return 0;
}
#endif

/**
 * ecrnx_ipc_rxbuf_elems_deallocs() - Free all RX buffer allocated for the FW
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_ipc_rxbuf_elems_deallocs(struct ecrnx_hw *ecrnx_hw)
{
    struct sk_buff *skb;
    int i;

    for (i = 0; i < ECRNX_RXBUFF_MAX; i++) {
        if (ecrnx_hw->rxbuf_elems.skb[i]) {
            skb = ecrnx_hw->rxbuf_elems.skb[i];
            dma_unmap_single(ecrnx_hw->dev, ECRNX_RXBUFF_DMA_ADDR_GET(skb),
                             ecrnx_hw->ipc_env->rx_bufsz, DMA_FROM_DEVICE);
            dev_kfree_skb(skb);
            ecrnx_hw->rxbuf_elems.skb[i] = NULL;
        }
    }   
}
#endif

/**
 * ecrnx_ipc_rxbuf_elem_pull() - Extract a skb from local table
 *
 * @ecrnx_hw: Main driver data
 * @skb: SKb to extract for table
 *
 * After checking that skb is actually a pointer of local table, extract it
 * from the table.
 * When buffer is removed, DMA mapping is remove which has the effect to
 * synchronize the buffer for the cpu.
 * To be called before passing skb to upper layer.
 */
#ifndef CONFIG_ECRNX_ESWIN
void ecrnx_ipc_rxbuf_elem_pull(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb)
{
    unsigned int idx = ECRNX_RXBUFF_IDX_GET(skb);

    if (ECRNX_RXBUFF_VALID_IDX(idx) && (ecrnx_hw->rxbuf_elems.skb[idx] == skb)) {
        dma_addr_t dma_addr = ECRNX_RXBUFF_DMA_ADDR_GET(skb);
        ecrnx_hw->rxbuf_elems.skb[idx] = NULL;
        dma_unmap_single(ecrnx_hw->dev, dma_addr,
                         ecrnx_hw->ipc_env->rx_bufsz, DMA_FROM_DEVICE);
    } else {
        WARN(1, "Incorrect rxbuff idx skb=%p table[%u]=%p", skb, idx,
             idx < ECRNX_RXBUFF_MAX ? ecrnx_hw->rxbuf_elems.skb[idx] : NULL);
    }

    /* Reset the pattern and idx */
    ECRNX_RXBUFF_PATTERN_SET(skb, 0);
    ECRNX_RXBUFF_IDX_SET(skb, ECRNX_RXBUFF_MAX);
}
#endif

/**
 * ecrnx_ipc_rxbuf_elem_sync() - Sync part of a RX buffer
 *
 * @ecrnx_hw: Main driver data
 * @skb: SKb to sync
 * @len: Len to sync
 *
 * After checking that skb is actually a pointer of local table, sync @p len
 * bytes of the buffer for CPU. Buffer is not removed from the table
 */
#ifndef CONFIG_ECRNX_ESWIN
void ecrnx_ipc_rxbuf_elem_sync(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb,
                              int len)
{
    unsigned int idx = ECRNX_RXBUFF_IDX_GET(skb);

    if (ECRNX_RXBUFF_VALID_IDX(idx) && (ecrnx_hw->rxbuf_elems.skb[idx] == skb)) {
        dma_addr_t dma_addr = ECRNX_RXBUFF_DMA_ADDR_GET(skb);
        dma_sync_single_for_cpu(ecrnx_hw->dev, dma_addr, len, DMA_FROM_DEVICE);
    } else {
        WARN(1, "Incorrect rxbuff idx skb=%p table[%u]=%p", skb, idx,
             idx < ECRNX_RXBUFF_MAX ? ecrnx_hw->rxbuf_elems.skb[idx] : NULL);
    }
}
#endif
#endif /* ! CONFIG_ECRNX_SOFTMAC */

/**
 * ecrnx_elems_deallocs() - Deallocate IPC storage elements.
 * @ecrnx_hw: Main driver data
 *
 * This function deallocates all the elements required for communications with
 * LMAC, such as Rx Data elements, MSGs elements, ...
 * This function should be called in correspondence with the allocation function.
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_elems_deallocs(struct ecrnx_hw *ecrnx_hw)
{
    ecrnx_ipc_rxbuf_elems_deallocs(ecrnx_hw);
    ecrnx_ipc_unsup_rx_vec_elems_deallocs(ecrnx_hw);
#ifdef CONFIG_ECRNX_FULLMAC
    ecrnx_ipc_elem_pool_deallocs(&ecrnx_hw->e2arxdesc_pool);
#endif
    ecrnx_ipc_elem_pool_deallocs(&ecrnx_hw->e2amsgs_pool);
    ecrnx_ipc_elem_pool_deallocs(&ecrnx_hw->dbgmsgs_pool);
    ecrnx_ipc_elem_pool_deallocs(&ecrnx_hw->e2aradars_pool);
    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &ecrnx_hw->pattern_elem);
    ecrnx_ipc_elem_var_deallocs(ecrnx_hw, &ecrnx_hw->dbgdump_elem.buf);   
}
#endif

/**
 * ecrnx_elems_allocs() - Allocate IPC storage elements.
 * @ecrnx_hw: Main driver data
 *
 * This function allocates all the elements required for communications with
 * LMAC, such as Rx Data elements, MSGs elements, ...
 * This function should be called in correspondence with the deallocation function.
 */
static int ecrnx_elems_allocs(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

#ifndef CONFIG_ECRNX_ESWIN
    if (dma_set_coherent_mask(ecrnx_hw->dev, DMA_BIT_MASK(32)))
        goto err_alloc;
    if (ecrnx_ipc_elem_pool_allocs(ecrnx_hw, &ecrnx_hw->e2amsgs_pool,
                                  ecrnx_hw->ipc_env->ipc_e2amsg_bufnb,
                                  ecrnx_hw->ipc_env->ipc_e2amsg_bufsz,
                                  "ecrnx_ipc_e2amsgs_pool",
                                  ipc_host_msgbuf_push))
        goto err_alloc;

    if (ecrnx_ipc_elem_pool_allocs(ecrnx_hw, &ecrnx_hw->dbgmsgs_pool,
                                  ecrnx_hw->ipc_env->ipc_dbg_bufnb,
                                  ecrnx_hw->ipc_env->ipc_dbg_bufsz,
                                  "ecrnx_ipc_dbgmsgs_pool",
                                  ipc_host_dbgbuf_push))
        goto err_alloc;

    if (ecrnx_ipc_elem_pool_allocs(ecrnx_hw, &ecrnx_hw->e2aradars_pool,
                                  ecrnx_hw->ipc_env->radar_bufnb,
                                  ecrnx_hw->ipc_env->radar_bufsz,
                                  "ecrnx_ipc_e2aradars_pool",
                                  ipc_host_radarbuf_push))
        goto err_alloc;

    if (ecrnx_ipc_unsup_rx_vec_elems_allocs(ecrnx_hw))
    #error 1111
        goto err_alloc;

    if (ecrnx_ipc_elem_var_allocs(ecrnx_hw, &ecrnx_hw->pattern_elem,
                                 sizeof(u32), DMA_TO_DEVICE,
                                 NULL, &ecrnx_rxbuff_pattern,
                                 ipc_host_patt_addr_push))
        goto err_alloc;

    if (ecrnx_ipc_elem_var_allocs(ecrnx_hw, &ecrnx_hw->dbgdump_elem.buf,
                                 sizeof(struct dbg_debug_dump_tag),
                                 DMA_FROM_DEVICE, NULL, NULL,
                                 ipc_host_dbginfobuf_push))
        goto err_alloc;

    /*
     * Note that the RX buffers are no longer allocated here as their size depends on the
     * FW configuration, which is not available at that time.
     * They will be allocated when checking the parameter compatibility between the driver
     * and the underlying components (i.e. during the ecrnx_handle_dynparams() execution)
     */

#ifdef CONFIG_ECRNX_FULLMAC
    if (ecrnx_ipc_elem_pool_allocs(ecrnx_hw, &ecrnx_hw->e2arxdesc_pool,
                                  ecrnx_hw->ipc_env->rxdesc_nb,
                                  sizeof(struct rxdesc_tag),
                                  "ecrnx_ipc_e2arxdesc_pool",
                                  ipc_host_rxdesc_push))
        goto err_alloc;

#endif /* CONFIG_ECRNX_FULLMAC */

    return 0;

err_alloc:
    ecrnx_elems_deallocs(ecrnx_hw);
    return -ENOMEM;
#else
    return 0;
#endif
}

/**
 * ecrnx_ipc_msg_push() - Push a msg to IPC queue
 *
 * @ecrnx_hw: Main driver data
 * @msg_buf: Pointer to message
 * @len: Size, in bytes, of message
 */
void ecrnx_ipc_msg_push(struct ecrnx_hw *ecrnx_hw, void *msg_buf, uint16_t len)
{
    ecrnx_hw->msg_tx++;
    ipc_host_msg_push(ecrnx_hw->ipc_env, msg_buf, len);
}

/**
 * ecrnx_ipc_txdesc_push() - Push a txdesc to FW
 *
 * @ecrnx_hw: Main driver data
 * @tx_desc: Pointer on &struct txdesc_api to push to FW
 * @hostid: Pointer save in ipc env to retrieve tx buffer upon confirmation.
 * @hw_queue: Hw queue to push txdesc to
 * @user: User position to push the txdesc to. It must be set to 0 if  MU-MIMMO
 * is not used.
 */
void ecrnx_ipc_txdesc_push(struct ecrnx_hw *ecrnx_hw, void *tx_desc,
                          void *hostid, int hw_queue, int user)
{

#if !defined(CONFIG_ECRNX_ESWIN_SDIO) && !defined(CONFIG_ECRNX_ESWIN_USB)
    volatile struct txdesc_host *txdesc_host;
    u32 *src, *dst;
    int i;

    txdesc_host = ipc_host_txdesc_get(ecrnx_hw->ipc_env, hw_queue, user);
    BUG_ON(!txdesc_host);

    dst = (typeof(dst))&txdesc_host->api;
    src = (typeof(src))tx_desc;
    for (i = 0; i < sizeof(txdesc_host->api) / sizeof(*src); i++)
        *dst++ = *src++;

    wmb(); /* vs desc */

	ipc_host_txdesc_push(ecrnx_hw->ipc_env, hw_queue, user, hostid);
#else
    ecrnx_frame_send(ecrnx_hw, tx_desc, hostid, hw_queue, user);
#endif
}

/**
 * ecrnx_ipc_fw_trace_desc_get() - Return pointer to the start of trace
 * description in IPC environment
 *
 * @ecrnx_hw: Main driver data
 */
void *ecrnx_ipc_fw_trace_desc_get(struct ecrnx_hw *ecrnx_hw)
{
#ifndef CONFIG_ECRNX_ESWIN
    return (void *)&(ecrnx_hw->ipc_env->shared->trace_pattern);
#else
    return NULL;
#endif
}

#ifndef CONFIG_ECRNX_ESWIN
/**
 * ecrnx_ipc_sta_buffer_init - Initialize counter of bufferred data for a given sta
 *
 * @ecrnx_hw: Main driver data
 * @sta_idx: Index of the station to initialize
 */
void ecrnx_ipc_sta_buffer_init(struct ecrnx_hw *ecrnx_hw, int sta_idx)
{
    int i;
    volatile u32_l *buffered;

    if (sta_idx >= NX_REMOTE_STA_MAX)
        return;

    buffered = ecrnx_hw->ipc_env->shared->buffered[sta_idx];

    for (i = 0; i < TID_MAX; i++) {
        *buffered++ = 0;
    }
}
#endif
/**
 * ecrnx_ipc_sta_buffer - Update counter of bufferred data for a given sta
 *
 * @ecrnx_hw: Main driver data
 * @sta: Managed station
 * @tid: TID on which data has been added or removed
 * @size: Size of data to add (or remove if < 0) to STA buffer.
 */
void ecrnx_ipc_sta_buffer(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta, int tid, int size)
{
#ifndef CONFIG_ECRNX_ESWIN
    u32_l *buffered;

    if (!sta)
        return;

    if ((sta->sta_idx >= NX_REMOTE_STA_MAX) || (tid >= TID_MAX))
        return;

    buffered = &ecrnx_hw->ipc_env->shared->buffered[sta->sta_idx][tid];

    if (size < 0) {
        size = -size;
        if (*buffered < size)
            *buffered = 0;
        else
            *buffered -= size;
    } else {
        // no test on overflow
        *buffered += size;
    }
#endif
}

/**
 * ecrnx_msgind() - IRQ handler callback for %IPC_IRQ_E2A_MSG
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to IPC elem from e2amsgs_pool
 */
static u8 ecrnx_msgind(void *pthis, void *hostid)
{
    struct ecrnx_hw *ecrnx_hw = pthis;
	u8 ret = 0;
#ifndef CONFIG_ECRNX_ESWIN
    struct ecrnx_ipc_elem *elem = hostid;
    struct ipc_e2a_msg *msg = elem->addr;

    REG_SW_SET_PROFILING(ecrnx_hw, SW_PROF_MSGIND);

    /* Look for pattern which means that this hostbuf has been used for a MSG */
    if (msg->pattern != IPC_MSGE2A_VALID_PATTERN) {
        ret = -1;
        goto msg_no_push;
    }
#else
    struct ipc_e2a_msg *msg = NULL;

    ecrnx_hw->msg_rx++;
    ECRNX_DBG("%s enter 0x%x, 0x%x!!\n", __func__, pthis, hostid);
    if(!pthis || !hostid){
        ECRNX_ERR(" %s input param error!! \n", __func__);
        return ret;
    }
    msg = hostid;
#endif
    /* Relay further actions to the msg parser */
    ecrnx_rx_handle_msg(ecrnx_hw, msg);
    
#ifndef CONFIG_ECRNX_ESWIN
    /* Reset the msg element and re-use it */
    msg->pattern = 0;
    wmb();

    /* Push back the buffer to the LMAC */
    ipc_host_msgbuf_push(ecrnx_hw->ipc_env, elem, elem->dma_addr);

msg_no_push:
    REG_SW_CLEAR_PROFILING(ecrnx_hw, SW_PROF_MSGIND);
#endif
    ECRNX_DBG("%s exit!!", __func__);
    return ret;
}

/**
 * ecrnx_msgackind() - IRQ handler callback for %IPC_IRQ_E2A_MSG_ACK
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to command acknoledged
 */
static u8 ecrnx_msgackind(void *pthis, void *hostid)
{
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw *)pthis;

    ecrnx_hw->msg_tx_done++;
    ecrnx_hw->cmd_mgr.llind(&ecrnx_hw->cmd_mgr, (struct ecrnx_cmd *)hostid);
    return -1;
}

/**
 * ecrnx_radarind() - IRQ handler callback for %IPC_IRQ_E2A_RADAR
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to IPC elem from e2aradars_pool
 */
static u8 ecrnx_radarind(void *pthis, void *hostid)
{
#ifdef CONFIG_ECRNX_RADAR
    struct ecrnx_hw *ecrnx_hw = pthis;
    struct ecrnx_ipc_elem *elem = hostid;
    struct radar_pulse_array_desc *pulses = elem->addr;
    u8 ret = 0;
    int i;

    /* Look for pulse count meaning that this hostbuf contains RADAR pulses */
    if (pulses->cnt == 0) {
        ret = -1;
        goto radar_no_push;
    }

    if (ecrnx_radar_detection_is_enable(&ecrnx_hw->radar, pulses->idx)) {
        /* Save the received pulses only if radar detection is enabled */
        for (i = 0; i < pulses->cnt; i++) {
            struct ecrnx_radar_pulses *p = &ecrnx_hw->radar.pulses[pulses->idx];

            p->buffer[p->index] = pulses->pulse[i];
            p->index = (p->index + 1) % ECRNX_RADAR_PULSE_MAX;
            if (p->count < ECRNX_RADAR_PULSE_MAX)
                p->count++;
        }

        /* Defer pulse processing in separate work */
        if (! work_pending(&ecrnx_hw->radar.detection_work))
            schedule_work(&ecrnx_hw->radar.detection_work);
    }

    /* Reset the radar element and re-use it */
    pulses->cnt = 0;
#ifndef CONFIG_ECRNX_ESWIN
    wmb();

    /* Push back the buffer to the LMAC */
    ipc_host_radarbuf_push(ecrnx_hw->ipc_env, elem, (u32)elem->dma_addr);
#endif
radar_no_push:
    return ret;
#else
    return -1;
#endif
}

/**
 * ecrnx_prim_tbtt_ind() - IRQ handler callback for %IPC_IRQ_E2A_TBTT_PRIM
 *
 * @pthis: Pointer to main driver data
 */
static void ecrnx_prim_tbtt_ind(void *pthis)
{
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw *)pthis;
    ecrnx_tx_bcns(ecrnx_hw);
#endif /* CONFIG_ECRNX_SOFTMAC */
}

/**
 * ecrnx_sec_tbtt_ind() - IRQ handler callback for %IPC_IRQ_E2A_TBTT_SEC
 *
 * @pthis: Pointer to main driver data
 */
static void ecrnx_sec_tbtt_ind(void *pthis)
{
}

/**
 * ecrnx_dbgind() - IRQ handler callback for %IPC_IRQ_E2A_DBG
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to IPC elem from dbgmsgs_pool
 */
 
#ifdef CONFIG_ECRNX_ESWIN_USB
 extern void usb_dbg_printf(void * data, int len);
#endif
static u8 ecrnx_dbgind(void *pthis, void *hostid)
{
    u8 ret = 0;
#ifndef CONFIG_ECRNX_ESWIN
    struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw *)pthis;
    struct ecrnx_ipc_elem *elem = hostid;
    struct ipc_dbg_msg *dbg_msg = elem->addr;

    REG_SW_SET_PROFILING(ecrnx_hw, SW_PROF_DBGIND);

    /* Look for pattern which means that this hostbuf has been used for a MSG */
    if (dbg_msg->pattern != IPC_DBG_VALID_PATTERN) {
        ret = -1;
        goto dbg_no_push;
    }

    /* Display the string */
    //printk("%s %s", (char *)FW_STR, (char *)dbg_msg->string);

    /* Reset the msg element and re-use it */
    dbg_msg->pattern = 0;
    wmb();

    /* Push back the buffer to the LMAC */
    ipc_host_dbgbuf_push(ecrnx_hw->ipc_env, elem, (u32)elem->dma_addr);

dbg_no_push:
    REG_SW_CLEAR_PROFILING(ecrnx_hw, SW_PROF_DBGIND);

#else
    struct sk_buff *skb = (struct sk_buff *)hostid;
#ifdef CONFIG_ECRNX_ESWIN_USB
    usb_dbg_printf(skb->data, skb->len);
#else
    uint8_t string[IPC_DBG_PARAM_SIZE] = {0}; 
    if(skb->len < IPC_DBG_PARAM_SIZE)
    {
        memcpy(string, skb->data, skb->len);
    }
    else
    {
        printk("waring: string buff no enough \n");
        memcpy(string, skb->data, IPC_DBG_PARAM_SIZE-1);
    }
    ECRNX_PRINT("%s %s", (char *)FW_STR, (char *)string);
#endif
#endif

    return ret;
}

/**
 * ecrnx_ipc_rxbuf_init() - Allocate and initialize RX buffers.
 *
 * @ecrnx_hw: Main driver data
 * @rx_bufsz: Size of the buffer to be allocated
 *
 * This function updates the RX buffer size according to the parameter and allocates the
 * RX buffers
 */
#ifndef CONFIG_ECRNX_ESWIN
int ecrnx_ipc_rxbuf_init(struct ecrnx_hw *ecrnx_hw, uint32_t rx_bufsz)
{
    ecrnx_hw->ipc_env->rx_bufsz = rx_bufsz;
    return(ecrnx_ipc_rxbuf_elems_allocs(ecrnx_hw));
}
#endif
/**
 * ecrnx_ipc_init() - Initialize IPC interface.
 *
 * @ecrnx_hw: Main driver data
 * @shared_ram: Pointer to shared memory that contains IPC shared struct
 *
 * This function initializes IPC interface by registering callbacks, setting
 * shared memory area and calling IPC Init function.
 * It should be called only once during driver's lifetime.
 */
int ecrnx_ipc_init(struct ecrnx_hw *ecrnx_hw, u8 *shared_ram)
{
    struct ipc_host_cb_tag cb;
    int res;
    ECRNX_DBG("%s entry!!", __func__);
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* initialize the API interface */
    cb.recv_data_ind   = ecrnx_rxdataind;
    cb.recv_radar_ind  = ecrnx_radarind;
    cb.recv_msg_ind    = ecrnx_msgind;
    cb.recv_msgack_ind = ecrnx_msgackind;
    cb.recv_dbg_ind    = ecrnx_dbgind;
    cb.send_data_cfm   = ecrnx_txdatacfm;
    cb.handle_data_cfm   = ecrnx_handle_tx_datacfm;

    cb.prim_tbtt_ind   = ecrnx_prim_tbtt_ind;
    cb.sec_tbtt_ind    = ecrnx_sec_tbtt_ind;
    cb.recv_unsup_rx_vec_ind = ecrnx_unsup_rx_vec_ind;

    /* set the IPC environment */
    ecrnx_hw->ipc_env = (struct ipc_host_env_tag *)
                       kzalloc(sizeof(struct ipc_host_env_tag), GFP_KERNEL);

    if (!ecrnx_hw->ipc_env)
        return -ENOMEM;

    /* call the initialization of the IPC */
    ipc_host_init(ecrnx_hw->ipc_env, &cb,
                  (struct ipc_shared_env_tag *)shared_ram, ecrnx_hw);

    ecrnx_cmd_mgr_init(&ecrnx_hw->cmd_mgr);

    ecrnx_rx_reord_init(ecrnx_hw);
#ifdef CONFIG_ECRNX_ESWIN_SDIO
    ecrnx_sdio_init(ecrnx_hw);
#elif defined(CONFIG_ECRNX_ESWIN_USB)
    ecrnx_usb_init(ecrnx_hw);
#endif

    res = ecrnx_elems_allocs(ecrnx_hw);
    if (res) {

        kfree(ecrnx_hw->ipc_env);
        ecrnx_hw->ipc_env = NULL;
    }
    ECRNX_DBG("%s exit!!", __func__);
    return res;
}

/**
 * ecrnx_ipc_deinit() - Release IPC interface
 *
 * @ecrnx_hw: Main driver data
 */
void ecrnx_ipc_deinit(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    ecrnx_ipc_tx_drain(ecrnx_hw);
    ecrnx_cmd_mgr_deinit(&ecrnx_hw->cmd_mgr);
#ifndef CONFIG_ECRNX_ESWIN
    ecrnx_elems_deallocs(ecrnx_hw);
#endif

    ecrnx_rx_reord_deinit(ecrnx_hw);
#ifdef CONFIG_ECRNX_ESWIN_SDIO
    if (ecrnx_hw->ipc_env->shared) {
        kfree(ecrnx_hw->ipc_env->shared);
        ecrnx_hw->ipc_env->shared = NULL;
    }
    ecrnx_sdio_deinit(ecrnx_hw);
#elif defined(CONFIG_ECRNX_ESWIN_USB)
    ecrnx_usb_deinit(ecrnx_hw);
#endif

    if (ecrnx_hw->ipc_env) {
        kfree(ecrnx_hw->ipc_env);
        ecrnx_hw->ipc_env = NULL;
    }
}

/**
 * ecrnx_ipc_start() - Start IPC interface
 *
 * @ecrnx_hw: Main driver data
 */
void ecrnx_ipc_start(struct ecrnx_hw *ecrnx_hw)
{
    ipc_host_enable_irq(ecrnx_hw->ipc_env, IPC_IRQ_E2A_ALL);
}

/**
 * ecrnx_ipc_stop() - Stop IPC interface
 *
 * @ecrnx_hw: Main driver data
 */
void ecrnx_ipc_stop(struct ecrnx_hw *ecrnx_hw)
{
    ipc_host_disable_irq(ecrnx_hw->ipc_env, IPC_IRQ_E2A_ALL);
}

/**
 * ecrnx_ipc_tx_drain() - Flush IPC TX buffers
 *
 * @ecrnx_hw: Main driver data
 *
 * This assumes LMAC is still (tx wise) and there's no TX race until LMAC is up
 * tx wise.
 * This also lets both IPC sides remain in sync before resetting the LMAC,
 * e.g with ecrnx_send_reset.
 */
void ecrnx_ipc_tx_drain(struct ecrnx_hw *ecrnx_hw)
{
    int i, j;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if (!ecrnx_hw->ipc_env) {
        printk(KERN_CRIT "%s: bypassing (restart must have failed)\n", __func__);
        return;
    }

    for (i = 0; i < ECRNX_HWQ_NB; i++) {
        for (j = 0; j < nx_txuser_cnt[i]; j++) {
            struct sk_buff *skb;
            while ((skb = (struct sk_buff *)ipc_host_tx_flush(ecrnx_hw->ipc_env, i, j))) {
                struct ecrnx_sw_txhdr *sw_txhdr =
                    ((struct ecrnx_txhdr *)skb->data)->sw_hdr;
#ifndef CONFIG_ECRNX_ESWIN        
	#ifdef CONFIG_ECRNX_AMSDUS_TX
                if (sw_txhdr->desc.host.packet_cnt > 1) {
                    struct ecrnx_amsdu_txhdr *amsdu_txhdr;
                    list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
                        dma_unmap_single(ecrnx_hw->dev, amsdu_txhdr->dma_addr,
                                         amsdu_txhdr->map_len, DMA_TO_DEVICE);
                        dev_kfree_skb_any(amsdu_txhdr->skb);
                    }
                }
	#endif
                kmem_cache_free(ecrnx_hw->sw_txhdr_cache, sw_txhdr);
                dma_unmap_single(ecrnx_hw->dev, sw_txhdr->dma_addr,
                                 sw_txhdr->map_len, DMA_TO_DEVICE);
#endif
                skb_pull(skb, sw_txhdr->headroom);
#ifdef CONFIG_ECRNX_SOFTMAC
                ieee80211_free_txskb(ecrnx_hw->hw, skb);
#else
                dev_kfree_skb_any(skb);
#endif /* CONFIG_ECRNX_SOFTMAC */
            }
        }
    }
}

/**
 * ecrnx_ipc_tx_pending() - Check if TX pframes are pending at FW level
 *
 * @ecrnx_hw: Main driver data
 */
bool ecrnx_ipc_tx_pending(struct ecrnx_hw *ecrnx_hw)
{
    return ipc_host_tx_frames_pending(ecrnx_hw->ipc_env);
}

/**
 * ecrnx_error_ind() - %DBG_ERROR_IND message callback
 *
 * @ecrnx_hw: Main driver data
 *
 * This function triggers the UMH script call that will indicate to the user
 * space the error that occurred and stored the debug dump. Once the UMH script
 * is executed, the ecrnx_umh_done() function has to be called.
 */
void ecrnx_error_ind(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_ipc_elem_var *elem = &ecrnx_hw->dbgdump_elem.buf;
    struct dbg_debug_dump_tag *dump = elem->addr;

    dma_sync_single_for_device(ecrnx_hw->dev, elem->dma_addr, elem->size,
                               DMA_FROM_DEVICE);
    dev_err(ecrnx_hw->dev, "(type %d): dump received\n",
            dump->dbg_info.error_type);

#ifdef CONFIG_ECRNX_DEBUGFS
    ecrnx_hw->debugfs.trace_prst = true;
    ecrnx_trigger_um_helper(&ecrnx_hw->debugfs);
#endif
}

/**
 * ecrnx_umh_done() - Indicate User Mode helper finished
 *
 * @ecrnx_hw: Main driver data
 *
 */
void ecrnx_umh_done(struct ecrnx_hw *ecrnx_hw)
{
    if (!test_bit(ECRNX_DEV_STARTED, &ecrnx_hw->flags))
        return;

    /* this assumes error_ind won't trigger before ipc_host_dbginfobuf_push
       is called and so does not irq protect (TODO) against error_ind */
#ifdef CONFIG_ECRNX_DEBUGFS
    ecrnx_hw->debugfs.trace_prst = false;
#ifndef CONFIG_ECRNX_ESWIN
    ipc_host_dbginfobuf_push(ecrnx_hw->ipc_env, ecrnx_hw->dbgdump_elem.buf.dma_addr);
#endif
#endif
}
