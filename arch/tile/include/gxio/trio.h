/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/*
 *
 * An API for allocating, configuring, and manipulating TRIO hardware
 * resources
 */

/*
 *
 * The TILE-Gx TRIO shim provides connections to external devices via
 * PCIe or other transaction IO standards.  The gxio_trio_ API,
 * declared in <gxio/trio.h>, allows applications to allocate and
 * configure TRIO IO resources like DMA command rings, memory map
 * windows, and device interrupts.  The following sections introduce
 * the various components of the API.  We strongly recommend reading
 * the TRIO section of the IO Device Guide (UG404) before working with
 * this API.
 *
 * @section trio__ingress TRIO Ingress Hardware Resources
 *
 * The TRIO ingress hardware is responsible for examining incoming
 * PCIe or StreamIO packets and choosing a processing mechanism based
 * on the packets' bus address.  The gxio_trio_ API can be used to
 * configure different handlers for different ranges of bus address
 * space.  The user can configure "mapped memory" and "scatter queue"
 * regions to match incoming packets within 4kB-aligned ranges of bus
 * addresses.  Each range specifies a different set of mapping
 * parameters to be applied when handling the ingress packet.  The
 * following sections describe how to work with MapMem and scatter
 * queue regions.
 *
 * @subsection trio__mapmem TRIO MapMem Regions
 *
 * TRIO mapped memory (or MapMem) regions allow the user to map
 * incoming read and write requests directly to the application's
 * memory space.  MapMem regions are allocated via
 * gxio_trio_alloc_memory_maps().  Given an integer MapMem number,
 * applications can use gxio_trio_init_memory_map() to specify the
 * range of bus addresses that will match the region and the range of
 * virtual addresses to which those packets will be applied.
 *
 * As with many other gxio APIs, the programmer must be sure to
 * register memory pages that will be used with MapMem regions.  Pages
 * can be registered with TRIO by allocating an ASID (address space
 * identifier) and then using gxio_trio_register_page() to register up to
 * 16 pages with the hardware.  The initialization functions for
 * resources that require registered memory (MapMem, scatter queues,
 * push DMA, and pull DMA) then take an 'asid' parameter in order to
 * configure which set of registered pages is used by each resource.
 *
 * @subsection trio__scatter_queue TRIO Scatter Queues
 *
 * The TRIO shim's scatter queue regions allow users to dynamically
 * map buffers from a large address space into a small range of bus
 * addresses.  This is particularly helpful for PCIe endpoint devices,
 * where the host generally limits the size of BARs to tens of
 * megabytes.
 *
 * Each scatter queue consists of a memory map region, a queue of
 * tile-side buffer VAs to be mapped to that region, and a bus-mapped
 * "doorbell" register that the remote endpoint can write to trigger a
 * dequeue of the current buffer VA, thus swapping in a new buffer.
 * The VAs pushed onto a scatter queue must be 4kB aligned, so
 * applications may need to use higher-level protocols to inform
 * remote entities that they should apply some additional, sub-4kB
 * offset when reading or writing the scatter queue region.  For more
 * information, see the IO Device Guide (UG404).
 *
 * @section trio__egress TRIO Egress Hardware Resources
 *
 * The TRIO shim supports two mechanisms for egress packet generation:
 * programmed IO (PIO) and push/pull DMA.  PIO allows applications to
 * create MMIO mappings for PCIe or StreamIO address space, such that
 * the application can generate word-sized read or write transactions
 * by issuing load or store instructions.  Push and pull DMA are tuned
 * for larger transactions; they use specialized hardware engines to
 * transfer large blocks of data at line rate.
 *
 * @subsection trio__pio TRIO Programmed IO
 *
 * Programmed IO allows applications to create MMIO mappings for PCIe
 * or StreamIO address space.  The hardware PIO regions support access
 * to PCIe configuration, IO, and memory space, but the gxio_trio API
 * only supports memory space accesses.  PIO regions are allocated
 * with gxio_trio_alloc_pio_regions() and initialized via
 * gxio_trio_init_pio_region().  Once a region is bound to a range of
 * bus address via the initialization function, the application can
 * use gxio_trio_map_pio_region() to create MMIO mappings from its VA
 * space onto the range of bus addresses supported by the PIO region.
 *
 * @subsection trio_dma TRIO Push and Pull DMA
 *
 * The TRIO push and pull DMA engines allow users to copy blocks of
 * data between application memory and the bus.  Push DMA generates
 * write packets that copy from application memory to the bus and pull
 * DMA generates read packets that copy from the bus into application
 * memory.  The DMA engines are managed via an API that is very
 * similar to the mPIPE eDMA interface.  For a detailed explanation of
 * the eDMA queue API, see @ref gxio_mpipe_wrappers.
 *
 * Push and pull DMA queues are allocated via
 * gxio_trio_alloc_push_dma_ring() / gxio_trio_alloc_pull_dma_ring().
 * Once allocated, users generally use a ::gxio_trio_dma_queue_t
 * object to manage the queue, providing easy wrappers for reserving
 * command slots in the DMA command ring, filling those slots, and
 * waiting for commands to complete.  DMA queues can be initialized
 * via gxio_trio_init_push_dma_queue() or
 * gxio_trio_init_pull_dma_queue().
 *
 * See @ref trio/push_dma/app.c for an example of how to use push DMA.
 *
 * @section trio_shortcomings Plans for Future API Revisions
 *
 * The simulation framework is incomplete.  Future features include:
 *
 * - Support for reset and deallocation of resources.
 *
 * - Support for pull DMA.
 *
 * - Support for interrupt regions and user-space interrupt delivery.
 *
 * - Support for getting BAR mappings and reserving regions of BAR
 *   address space.
 */
#ifndef _GXIO_TRIO_H_
#define _GXIO_TRIO_H_

#include <linux/types.h>

#include <gxio/common.h>
#include <gxio/dma_queue.h>

#include <arch/trio_constants.h>
#include <arch/trio.h>
#include <arch/trio_pcie_intfc.h>
#include <arch/trio_pcie_rc.h>
#include <arch/trio_shm.h>
#include <hv/drv_trio_intf.h>
#include <hv/iorpc.h>

/* A context object used to manage TRIO hardware resources. */
typedef struct {

	/* File descriptor for calling up to Linux (and thus the HV). */
	int fd;

	/* The VA at which the MAC MMIO registers are mapped. */
	char *mmio_base_mac;

	/* The VA at which the PIO config space are mapped for each PCIe MAC.
	   Gx36 has max 3 PCIe MACs per TRIO shim. */
	char *mmio_base_pio_cfg[TILEGX_TRIO_PCIES];

#ifdef USE_SHARED_PCIE_CONFIG_REGION
	/* Index of the shared PIO region for PCI config access. */
	int pio_cfg_index;
#else
	/* Index of the PIO region for PCI config access per MAC. */
	int pio_cfg_index[TILEGX_TRIO_PCIES];
#endif

	/*  The VA at which the push DMA MMIO registers are mapped. */
	char *mmio_push_dma[TRIO_NUM_PUSH_DMA_RINGS];

	/*  The VA at which the pull DMA MMIO registers are mapped. */
	char *mmio_pull_dma[TRIO_NUM_PUSH_DMA_RINGS];

	/* Application space ID. */
	unsigned int asid;

} gxio_trio_context_t;

/* Command descriptor for push or pull DMA. */
typedef TRIO_DMA_DESC_t gxio_trio_dma_desc_t;

/* A convenient, thread-safe interface to an eDMA ring. */
typedef struct {

	/* State object for tracking head and tail pointers. */
	__gxio_dma_queue_t dma_queue;

	/* The ring entries. */
	gxio_trio_dma_desc_t *dma_descs;

	/* The number of entries minus one. */
	unsigned long mask_num_entries;

	/* The log2() of the number of entries. */
	unsigned int log2_num_entries;

} gxio_trio_dma_queue_t;

/* Initialize a TRIO context.
 *
 * This function allocates a TRIO "service domain" and maps the MMIO
 * registers into the the caller's VA space.
 *
 * @param trio_index Which TRIO shim; Gx36 must pass 0.
 * @param context Context object to be initialized.
 */
extern int gxio_trio_init(gxio_trio_context_t *context,
			  unsigned int trio_index);

/* This indicates that an ASID hasn't been allocated. */
#define GXIO_ASID_NULL -1

/* Ordering modes for map memory regions and scatter queue regions. */
typedef enum gxio_trio_order_mode_e {
	/* Writes are not ordered.  Reads always wait for previous writes. */
	GXIO_TRIO_ORDER_MODE_UNORDERED =
		TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_UNORDERED,
	/* Both writes and reads wait for previous transactions to complete. */
	GXIO_TRIO_ORDER_MODE_STRICT =
		TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_STRICT,
	/* Writes are ordered unless the incoming packet has the
	   relaxed-ordering attributes set. */
	GXIO_TRIO_ORDER_MODE_OBEY_PACKET =
		TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_REL_ORD
} gxio_trio_order_mode_t;

/* Initialize a memory mapping region.
 *
 * @param context An initialized TRIO context.
 * @param map A Memory map region allocated by gxio_trio_alloc_memory_map().
 * @param target_mem VA of backing memory, should be registered via
 *   gxio_trio_register_page() and aligned to 4kB.
 * @param target_size Length of the memory mapping, must be a multiple
 * of 4kB.
 * @param asid ASID to be used for Tile-side address translation.
 * @param mac MAC number.
 * @param bus_address Bus address at which the mapping starts.
 * @param order_mode Memory ordering mode for this mapping.
 * @return Zero on success, else ::GXIO_TRIO_ERR_BAD_MEMORY_MAP,
 * GXIO_TRIO_ERR_BAD_ASID, or ::GXIO_TRIO_ERR_BAD_BUS_RANGE.
 */
extern int gxio_trio_init_memory_map(gxio_trio_context_t *context,
				     unsigned int map, void *target_mem,
				     size_t target_size, unsigned int asid,
				     unsigned int mac, uint64_t bus_address,
				     gxio_trio_order_mode_t order_mode);

/* Flags that can be passed to resource allocation functions. */
enum gxio_trio_alloc_flags_e {
	GXIO_TRIO_ALLOC_FIXED = HV_TRIO_ALLOC_FIXED,
};

/* Flags that can be passed to memory registration functions. */
enum gxio_trio_mem_flags_e {
	/* Do not fill L3 when writing, and invalidate lines upon egress. */
	GXIO_TRIO_MEM_FLAG_NT_HINT = IORPC_MEM_BUFFER_FLAG_NT_HINT,

	/* L3 cache fills should only populate IO cache ways. */
	GXIO_TRIO_MEM_FLAG_IO_PIN = IORPC_MEM_BUFFER_FLAG_IO_PIN,
};

/* Flag indicating a request generator uses a special traffic
    class. */
#define GXIO_TRIO_FLAG_TRAFFIC_CLASS(N) HV_TRIO_FLAG_TC(N)

/* Flag indicating a request generator uses a virtual function
    number. */
#define GXIO_TRIO_FLAG_VFUNC(N) HV_TRIO_FLAG_VFUNC(N)

/*****************************************************************
 *                       Memory Registration                      *
 ******************************************************************/

/* Allocate Application Space Identifiers (ASIDs).  Each ASID can
 * register up to 16 page translations.  ASIDs are used by memory map
 * regions, scatter queues, and DMA queues to translate application
 * VAs into memory system PAs.
 *
 * @param context An initialized TRIO context.
 * @param count Number of ASIDs required.
 * @param first Index of first ASID if ::GXIO_TRIO_ALLOC_FIXED flag
 *   is set, otherwise ignored.
 * @param flags Flag bits, including bits from ::gxio_trio_alloc_flags_e.
 * @return Index of first ASID, or ::GXIO_TRIO_ERR_NO_ASID if allocation
 *   failed.
 */
extern int gxio_trio_alloc_asids(gxio_trio_context_t *context,
				 unsigned int count, unsigned int first,
				 unsigned int flags);

#endif /* ! _GXIO_TRIO_H_ */
