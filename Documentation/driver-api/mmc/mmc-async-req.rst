========================
MMC Asynchronous Request
========================

Rationale
=========

How significant is the cache maintenance overhead?

It depends. Fast eMMC and multiple cache levels with speculative cache
pre-fetch makes the cache overhead relatively significant. If the DMA
preparations for the next request are done in parallel with the current
transfer, the DMA preparation overhead would not affect the MMC performance.

The intention of non-blocking (asynchronous) MMC requests is to minimize the
time between when an MMC request ends and another MMC request begins.

Using mmc_wait_for_req(), the MMC controller is idle while dma_map_sg and
dma_unmap_sg are processing. Using non-blocking MMC requests makes it
possible to prepare the caches for next job in parallel with an active
MMC request.

MMC block driver
================

The mmc_blk_mq_issue_rw_rq() in the MMC block driver is made non-blocking.

The increase in throughput is proportional to the time it takes to
prepare (major part of preparations are dma_map_sg() and dma_unmap_sg())
a request and how fast the memory is. The faster the MMC/SD is the
more significant the prepare request time becomes. Roughly the expected
performance gain is 5% for large writes and 10% on large reads on a L2 cache
platform. In power save mode, when clocks run on a lower frequency, the DMA
preparation may cost even more. As long as these slower preparations are run
in parallel with the transfer performance won't be affected.

MMC core API
============

The preparation of a request is separated from starting the transfer, so
that a host can prepare a request while another one is still in progress:

 * mmc_pre_req() prepares a request before it is started. It may be called
   while another request is running on the host.
 * mmc_start_request() starts a prepared request without waiting for it to
   complete. mmc_wait_for_req() starts a request and waits for it to
   complete as well.
 * mmc_post_req() releases the resources allocated by mmc_pre_req() after
   the request has completed. It may likewise run while another request is
   active.

MMC host extensions
===================

There are two optional members in the mmc_host_ops -- pre_req() and
post_req() -- that the host driver may implement in order to move work
to before and after the actual mmc_host_ops.request() function is called.

In the DMA case pre_req() may do dma_map_sg() and prepare the DMA
descriptor, and post_req() runs the dma_unmap_sg().
