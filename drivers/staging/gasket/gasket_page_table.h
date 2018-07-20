/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Gasket Page Table functionality. This file describes the address
 * translation/paging functionality supported by the Gasket driver framework.
 * As much as possible, internal details are hidden to simplify use -
 * all calls are thread-safe (protected by an internal mutex) except where
 * indicated otherwise.
 *
 * Copyright (C) 2018 Google, Inc.
 */

#ifndef __GASKET_ADDR_TRNSL_H__
#define __GASKET_ADDR_TRNSL_H__

#include <linux/pci.h>
#include <linux/types.h>

#include "gasket_constants.h"
#include "gasket_core.h"

/*
 * Structure used for managing address translation on a device. All details are
 * internal to the implementation.
 */
struct gasket_page_table;

/*
 * Allocate and init address translation data.
 * @ppage_table: Pointer to Gasket page table pointer. Set by this call.
 * @att_base_reg: [Mapped] pointer to the first entry in the device's address
 *                translation table.
 * @extended_offset_reg: [Mapped] pointer to the device's register containing
 *                       the starting index of the extended translation table.
 * @extended_bit_location: The index of the bit indicating whether an address
 *                         is extended.
 * @total_entries: The total number of entries in the device's address
 *                 translation table.
 * @device: Device structure for the underlying device. Only used for logging.
 * @pci_dev: PCI system descriptor for the underlying device.
 * @bool has_dma_ops: Whether the page table uses arch specific dma_ops or
 * whether the driver will supply its own.
 *
 * Description: Allocates and initializes data to track address translation -
 * simple and extended page table metadata. Initially, the page table is
 * partitioned such that all addresses are "simple" (single-level lookup).
 * gasket_partition_page_table can be called to change this paritioning.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int gasket_page_table_init(
	struct gasket_page_table **ppg_tbl,
	const struct gasket_bar_data *bar_data,
	const struct gasket_page_table_config *page_table_config,
	struct device *device, struct pci_dev *pci_dev, bool dma_ops);

/*
 * Deallocate and cleanup page table data.
 * @page_table: Gasket page table pointer.
 *
 * Description: The inverse of gasket_init; frees page_table and its contained
 *              elements.
 *
 *	        Because this call destroys the page table, it cannot be
 *	        thread-safe (mutex-protected)!
 */
void gasket_page_table_cleanup(struct gasket_page_table *page_table);

/*
 * Sets the size of the simple page table.
 * @page_table: Gasket page table pointer.
 * @num_simple_entries: Desired size of the simple page table (in entries).
 *
 * Description: gasket_partition_page_table checks to see if the simple page
 *              size can be changed (i.e., if there are no active extended
 *              mappings in the new simple size range), and, if so,
 *              sets the new simple and extended page table sizes.
 *
 *              Returns 0 if successful, or non-zero if the page table entries
 *              are not free.
 */
int gasket_page_table_partition(
	struct gasket_page_table *page_table, uint num_simple_entries);

/*
 * Get and map [host] user space pages into device memory.
 * @page_table: Gasket page table pointer.
 * @host_addr: Starting host virtual memory address of the pages.
 * @dev_addr: Starting device address of the pages.
 * @num_pages: Number of [4kB] pages to map.
 *
 * Description: Maps the "num_pages" pages of host memory pointed to by
 *              host_addr to the address "dev_addr" in device memory.
 *
 *              The caller is responsible for checking the addresses ranges.
 *
 *              Returns 0 if successful or a non-zero error number otherwise.
 *              If there is an error, no pages are mapped.
 */
int gasket_page_table_map(struct gasket_page_table *page_table, ulong host_addr,
			  ulong dev_addr, uint num_pages);

/*
 * Un-map host pages from device memory.
 * @page_table: Gasket page table pointer.
 * @dev_addr: Starting device address of the pages to unmap.
 * @num_pages: The number of [4kB] pages to unmap.
 *
 * Description: The inverse of gasket_map_pages. Unmaps pages from the device.
 */
void gasket_page_table_unmap(
	struct gasket_page_table *page_table, ulong dev_addr, uint num_pages);

/*
 * Unmap ALL host pages from device memory.
 * @page_table: Gasket page table pointer.
 */
void gasket_page_table_unmap_all(struct gasket_page_table *page_table);

/*
 * Unmap all host pages from device memory and reset the table to fully simple
 * addressing.
 * @page_table: Gasket page table pointer.
 */
void gasket_page_table_reset(struct gasket_page_table *page_table);

/*
 * Reclaims unused page table memory.
 * @page_table: Gasket page table pointer.
 *
 * Description: Examines the page table and frees any currently-unused
 *              allocations. Called internally on gasket_cleanup().
 */
void gasket_page_table_garbage_collect(struct gasket_page_table *page_table);

/*
 * Retrieve the backing page for a device address.
 * @page_table: Gasket page table pointer.
 * @dev_addr: Gasket device address.
 * @ppage: Pointer to a page pointer for the returned page.
 * @poffset: Pointer to an unsigned long for the returned offset.
 *
 * Description: Interprets the address and looks up the corresponding page
 *              in the page table and the offset in that page.  (We need an
 *              offset because the host page may be larger than the Gasket chip
 *              page it contains.)
 *
 *              Returns 0 if successful, -1 for an error.  The page pointer
 *              and offset are returned through the pointers, if successful.
 */
int gasket_page_table_lookup_page(
	struct gasket_page_table *page_table, ulong dev_addr,
	struct page **page, ulong *poffset);

/*
 * Checks validity for input addrs and size.
 * @page_table: Gasket page table pointer.
 * @host_addr: Host address to check.
 * @dev_addr: Gasket device address.
 * @bytes: Size of the range to check (in bytes).
 *
 * Description: This call performs a number of checks to verify that the ranges
 * specified by both addresses and the size are valid for mapping pages into
 * device memory.
 *
 * Returns true if the mapping is bad, false otherwise.
 */
bool gasket_page_table_are_addrs_bad(
	struct gasket_page_table *page_table, ulong host_addr, ulong dev_addr,
	ulong bytes);

/*
 * Checks validity for input dev addr and size.
 * @page_table: Gasket page table pointer.
 * @dev_addr: Gasket device address.
 * @bytes: Size of the range to check (in bytes).
 *
 * Description: This call performs a number of checks to verify that the range
 * specified by the device address and the size is valid for mapping pages into
 * device memory.
 *
 * Returns true if the address is bad, false otherwise.
 */
bool gasket_page_table_is_dev_addr_bad(
	struct gasket_page_table *page_table, ulong dev_addr, ulong bytes);

/*
 * Gets maximum size for the given page table.
 * @page_table: Gasket page table pointer.
 */
uint gasket_page_table_max_size(struct gasket_page_table *page_table);

/*
 * Gets the total number of entries in the arg.
 * @page_table: Gasket page table pointer.
 */
uint gasket_page_table_num_entries(struct gasket_page_table *page_table);

/*
 * Gets the number of simple entries.
 * @page_table: Gasket page table pointer.
 */
uint gasket_page_table_num_simple_entries(struct gasket_page_table *page_table);

/*
 * Gets the number of actively pinned pages.
 * @page_table: Gasket page table pointer.
 */
uint gasket_page_table_num_active_pages(struct gasket_page_table *page_table);

/*
 * Get status of page table managed by @page_table.
 * @page_table: Gasket page table pointer.
 */
int gasket_page_table_system_status(struct gasket_page_table *page_table);

/*
 * Allocate a block of coherent memory.
 * @gasket_dev: Gasket Device.
 * @size: Size of the memory block.
 * @dma_address: Dma address allocated by the kernel.
 * @index: Index of the gasket_page_table within this Gasket device
 *
 * Description: Allocate a contiguous coherent memory block, DMA'ble
 * by this device.
 */
int gasket_alloc_coherent_memory(struct gasket_dev *gasket_dev, uint64_t size,
				 dma_addr_t *dma_address, uint64_t index);
/* Release a block of contiguous coherent memory, in use by a device. */
int gasket_free_coherent_memory(struct gasket_dev *gasket_dev, uint64_t size,
				dma_addr_t dma_address, uint64_t index);

/* Release all coherent memory. */
void gasket_free_coherent_memory_all(struct gasket_dev *gasket_dev,
				     uint64_t index);

/*
 * Records the host_addr to coherent dma memory mapping.
 * @gasket_dev: Gasket Device.
 * @size: Size of the virtual address range to map.
 * @dma_address: Dma address within the coherent memory range.
 * @vma: Virtual address we wish to map to coherent memory.
 *
 * Description: For each page in the virtual address range, record the
 * coherent page mapping.
 *
 * Does not perform validity checking.
 */
int gasket_set_user_virt(struct gasket_dev *gasket_dev, uint64_t size,
			 dma_addr_t dma_address, ulong vma);

#endif
