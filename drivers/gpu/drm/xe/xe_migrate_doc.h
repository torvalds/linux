/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_MIGRATE_DOC_H_
#define _XE_MIGRATE_DOC_H_

/**
 * DOC: Migrate Layer
 *
 * The XE migrate layer is used generate jobs which can copy memory (eviction),
 * clear memory, or program tables (binds). This layer exists in every GT, has
 * a migrate engine, and uses a special VM for all generated jobs.
 *
 * Special VM details
 * ==================
 *
 * The special VM is configured with a page structure where we can dynamically
 * map BOs which need to be copied and cleared, dynamically map other VM's page
 * table BOs for updates, and identity map the entire device's VRAM with 1 GB
 * pages.
 *
 * Currently the page structure consists of 32 physical pages with 16 being
 * reserved for BO mapping during copies and clear, 1 reserved for kernel binds,
 * several pages are needed to setup the identity mappings (exact number based
 * on how many bits of address space the device has), and the rest are reserved
 * user bind operations.
 *
 * TODO: Diagram of layout
 *
 * Bind jobs
 * =========
 *
 * A bind job consist of two batches and runs either on the migrate engine
 * (kernel binds) or the bind engine passed in (user binds). In both cases the
 * VM of the engine is the migrate VM.
 *
 * The first batch is used to update the migration VM page structure to point to
 * the bind VM page table BOs which need to be updated. A physical page is
 * required for this. If it is a user bind, the page is allocated from pool of
 * pages reserved user bind operations with drm_suballoc managing this pool. If
 * it is a kernel bind, the page reserved for kernel binds is used.
 *
 * The first batch is only required for devices without VRAM as when the device
 * has VRAM the bind VM page table BOs are in VRAM and the identity mapping can
 * be used.
 *
 * The second batch is used to program page table updated in the bind VM. Why
 * not just one batch? Well the TLBs need to be invalidated between these two
 * batches and that only can be done from the ring.
 *
 * When the bind job complete, the page allocated is returned the pool of pages
 * reserved for user bind operations if a user bind. No need do this for kernel
 * binds as the reserved kernel page is serially used by each job.
 *
 * Copy / clear jobs
 * =================
 *
 * A copy or clear job consist of two batches and runs on the migrate engine.
 *
 * Like binds, the first batch is used update the migration VM page structure.
 * In copy jobs, we need to map the source and destination of the BO into page
 * the structure. In clear jobs, we just need to add 1 mapping of BO into the
 * page structure. We use the 16 reserved pages in migration VM for mappings,
 * this gives us a maximum copy size of 16 MB and maximum clear size of 32 MB.
 *
 * The second batch is used do either do the copy or clear. Again similar to
 * binds, two batches are required as the TLBs need to be invalidated from the
 * ring between the batches.
 *
 * More than one job will be generated if the BO is larger than maximum copy /
 * clear size.
 *
 * Future work
 * ===========
 *
 * Update copy and clear code to use identity mapped VRAM.
 *
 * Can we rework the use of the pages async binds to use all the entries in each
 * page?
 *
 * Using large pages for sysmem mappings.
 *
 * Is it possible to identity map the sysmem? We should explore this.
 */

#endif
