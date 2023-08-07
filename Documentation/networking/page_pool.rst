.. SPDX-License-Identifier: GPL-2.0

=============
Page Pool API
=============

.. kernel-doc:: include/net/page_pool/helpers.h
   :doc: page_pool allocator

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

.. kernel-doc:: net/core/page_pool.c
   :identifiers: page_pool_create

.. kernel-doc:: include/net/page_pool/types.h
   :identifiers: struct page_pool_params

.. kernel-doc:: include/net/page_pool/helpers.h
   :identifiers: page_pool_put_page page_pool_put_full_page
		 page_pool_recycle_direct page_pool_dev_alloc_pages
		 page_pool_get_dma_addr page_pool_get_dma_dir

.. kernel-doc:: net/core/page_pool.c
   :identifiers: page_pool_put_page_bulk page_pool_get_stats

DMA sync
--------
Driver is always responsible for syncing the pages for the CPU.
Drivers may choose to take care of syncing for the device as well
or set the ``PP_FLAG_DMA_SYNC_DEV`` flag to request that pages
allocated from the page pool are already synced for the device.

If ``PP_FLAG_DMA_SYNC_DEV`` is set, the driver must inform the core what portion
of the buffer has to be synced. This allows the core to avoid syncing the entire
page when the drivers knows that the device only accessed a portion of the page.

Most drivers will reserve headroom in front of the frame. This part
of the buffer is not touched by the device, so to avoid syncing
it drivers can set the ``offset`` field in struct page_pool_params
appropriately.

For pages recycled on the XDP xmit and skb paths the page pool will
use the ``max_len`` member of struct page_pool_params to decide how
much of the page needs to be synced (starting at ``offset``).
When directly freeing pages in the driver (page_pool_put_page())
the ``dma_sync_size`` argument specifies how much of the buffer needs
to be synced.

If in doubt set ``offset`` to 0, ``max_len`` to ``PAGE_SIZE`` and
pass -1 as ``dma_sync_size``. That combination of arguments is always
correct.

Note that the syncing parameters are for the entire page.
This is important to remember when using fragments (``PP_FLAG_PAGE_FRAG``),
where allocated buffers may be smaller than a full page.
Unless the driver author really understands page pool internals
it's recommended to always use ``offset = 0``, ``max_len = PAGE_SIZE``
with fragmented page pools.

Stats API and structures
------------------------
If the kernel is configured with ``CONFIG_PAGE_POOL_STATS=y``, the API
page_pool_get_stats() and structures described below are available.
It takes a  pointer to a ``struct page_pool`` and a pointer to a struct
page_pool_stats allocated by the caller.

The API will fill in the provided struct page_pool_stats with
statistics about the page_pool.

.. kernel-doc:: include/net/page_pool/types.h
   :identifiers: struct page_pool_recycle_stats
		 struct page_pool_alloc_stats
		 struct page_pool_stats

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
    pp_params.napi = napi; /* only if locking is tied to NAPI */
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
            skb_mark_for_recycle(skb);
            new_page = page_pool_dev_alloc_pages(page_pool);
        }
    }

Stats
-----

.. code-block:: c

	#ifdef CONFIG_PAGE_POOL_STATS
	/* retrieve stats */
	struct page_pool_stats stats = { 0 };
	if (page_pool_get_stats(page_pool, &stats)) {
		/* perhaps the driver reports statistics with ethool */
		ethtool_print_allocation_stats(&stats.alloc_stats);
		ethtool_print_recycle_stats(&stats.recycle_stats);
	}
	#endif

Driver unload
-------------

.. code-block:: c

    /* Driver unload */
    page_pool_put_full_page(page_pool, page, false);
    xdp_rxq_info_unreg(&xdp_rxq);
