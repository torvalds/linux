.. SPDX-License-Identifier: GPL-2.0

=============
Page Pool API
=============

The page_pool allocator is optimized for the XDP mode that uses one frame
per-page, but it can fallback on the regular page allocator APIs.

Basic use involves replacing alloc_pages() calls with the
page_pool_alloc_pages() call.  Drivers should use page_pool_dev_alloc_pages()
replacing dev_alloc_pages().

API keeps track of inflight pages, in order to let API user know
when it is safe to free a page_pool object.  Thus, API users
must run page_pool_release_page() when a page is leaving the page_pool or
call page_pool_put_page() where appropriate in order to maintain correct
accounting.

API user must call page_pool_put_page() once on a page, as it
will either recycle the page, or in case of refcnt > 1, it will
release the DMA mapping and inflight state accounting.

Architecture overview
=====================

.. code-block:: none

    +------------------+
    |       Driver     |
    +------------------+
            ^
            |
            |
            |
            v
    +--------------------------------------------+
    |                request memory              |
    +--------------------------------------------+
        ^                                  ^
        |                                  |
        | Pool empty                       | Pool has entries
        |                                  |
        v                                  v
    +-----------------------+     +------------------------+
    | alloc (and map) pages |     |  get page from cache   |
    +-----------------------+     +------------------------+
                                    ^                    ^
                                    |                    |
                                    | cache available    | No entries, refill
                                    |                    | from ptr-ring
                                    |                    |
                                    v                    v
                          +-----------------+     +------------------+
                          |   Fast cache    |     |  ptr-ring cache  |
                          +-----------------+     +------------------+

API interface
=============
The number of pools created **must** match the number of hardware queues
unless hardware restrictions make that impossible. This would otherwise beat the
purpose of page pool, which is allocate pages fast from cache without locking.
This lockless guarantee naturally comes from running under a NAPI softirq.
The protection doesn't strictly have to be NAPI, any guarantee that allocating
a page will cause no race conditions is enough.

* page_pool_create(): Create a pool.
    * flags:      PP_FLAG_DMA_MAP, PP_FLAG_DMA_SYNC_DEV
    * order:      2^order pages on allocation
    * pool_size:  size of the ptr_ring
    * nid:        preferred NUMA node for allocation
    * dev:        struct device. Used on DMA operations
    * dma_dir:    DMA direction
    * max_len:    max DMA sync memory size
    * offset:     DMA address offset

* page_pool_put_page(): The outcome of this depends on the page refcnt. If the
  driver bumps the refcnt > 1 this will unmap the page. If the page refcnt is 1
  the allocator owns the page and will try to recycle it in one of the pool
  caches. If PP_FLAG_DMA_SYNC_DEV is set, the page will be synced for_device
  using dma_sync_single_range_for_device().

* page_pool_put_full_page(): Similar to page_pool_put_page(), but will DMA sync
  for the entire memory area configured in area pool->max_len.

* page_pool_recycle_direct(): Similar to page_pool_put_full_page() but caller
  must guarantee safe context (e.g NAPI), since it will recycle the page
  directly into the pool fast cache.

* page_pool_release_page(): Unmap the page (if mapped) and account for it on
  inflight counters.

* page_pool_dev_alloc_pages(): Get a page from the page allocator or page_pool
  caches.

* page_pool_get_dma_addr(): Retrieve the stored DMA address.

* page_pool_get_dma_dir(): Retrieve the stored DMA direction.

Coding examples
===============

Registration
------------

.. code-block:: c

    /* Page pool registration */
    struct page_pool_params pp_params = { 0 };
    struct xdp_rxq_info xdp_rxq;
    int err;

    pp_params.order = 0;
    /* internal DMA mapping in page_pool */
    pp_params.flags = PP_FLAG_DMA_MAP;
    pp_params.pool_size = DESC_NUM;
    pp_params.nid = NUMA_NO_NODE;
    pp_params.dev = priv->dev;
    pp_params.dma_dir = xdp_prog ? DMA_BIDIRECTIONAL : DMA_FROM_DEVICE;
    page_pool = page_pool_create(&pp_params);

    err = xdp_rxq_info_reg(&xdp_rxq, ndev, 0);
    if (err)
        goto err_out;

    err = xdp_rxq_info_reg_mem_model(&xdp_rxq, MEM_TYPE_PAGE_POOL, page_pool);
    if (err)
        goto err_out;

NAPI poller
-----------


.. code-block:: c

    /* NAPI Rx poller */
    enum dma_data_direction dma_dir;

    dma_dir = page_pool_get_dma_dir(dring->page_pool);
    while (done < budget) {
        if (some error)
            page_pool_recycle_direct(page_pool, page);
        if (packet_is_xdp) {
            if XDP_DROP:
                page_pool_recycle_direct(page_pool, page);
        } else (packet_is_skb) {
            page_pool_release_page(page_pool, page);
            new_page = page_pool_dev_alloc_pages(page_pool);
        }
    }

Driver unload
-------------

.. code-block:: c

    /* Driver unload */
    page_pool_put_full_page(page_pool, page, false);
    xdp_rxq_info_unreg(&xdp_rxq);
