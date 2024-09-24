/*
 *
 * (C) COPYRIGHT 2018-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef _ETHOSN_DMA_H_
#define _ETHOSN_DMA_H_

#include <linux/dma-mapping.h>
#include <linux/types.h>

#define ETHOSN_REGION_MASK DMA_BIT_MASK(REGION_SHIFT)

#define ETHOSN_PROT_READ (1 << 0)
#define ETHOSN_PROT_WRITE (1 << 1)

struct device;
struct vm_area_struct;

/**
 * Stream type
 */
enum  ethosn_stream_type {
	ETHOSN_STREAM_FIRMWARE            = 0,
	ETHOSN_STREAM_WORKING_DATA        = 1,
	ETHOSN_STREAM_COMMAND_STREAM      = 2,
	ETHOSN_STREAM_DEBUG               = 3,
	ETHOSN_STREAM_PLE_CODE            = 4,
	ETHOSN_STREAM_WEIGHT_DATA         = 5,
	ETHOSN_STREAM_IO_BUFFER           = 6,
	ETHOSN_STREAM_INTERMEDIATE_BUFFER = 7,
	ETHOSN_STREAM_RESERVED            = 8,
	ETHOSN_STREAM_INVALID             = 9,
};

/**
 * Allocator identifier
 */
enum  ethosn_alloc_type {
	ETHOSN_ALLOCATOR_MAIN     = 0,
	ETHOSN_ALLOCATOR_ASSET    = 1,
	ETHOSN_ALLOCATOR_CARVEOUT = 2,
	ETHOSN_ALLOCATOR_INVALID  = 3,
};

/*
 * Carries the information about the DMA allocation.
 * Also, iova_addr is used to set the buffer table for inferences.
 * stream_type is used to select the appropriate sub-allocator from the
 * main or asset allocator for additional dma operations such as mapping,
 * unmapping, and syncing for the device and cpu.
 * imported is set if it represents a shared memory.
 */
struct ethosn_dma_info {
	size_t                  size;
	void                    *cpu_addr;
	dma_addr_t              iova_addr;
	enum ethosn_stream_type stream_type;
	bool                    imported;
};

/**
 * struct ethosn_dma_sub_allocator - Contains allocator operations for DMA
 * memory
 * @ops                Allocator operations
 * @smmu_stream_id     SMMU stream id for the SMMU case, 0 in carveout
 * @dev                Device bound to the sub allocator
 */
struct ethosn_dma_sub_allocator {
	const struct ethosn_dma_allocator_ops *ops;
	u32                                   smmu_stream_id;
	struct device                         *dev;
};

/**
 * struct ethosn_dma_allocator - Contains allocator type and device
 * @type     Stream type of this allocator
 * @alloc_id For asset allocators, this identifies which allocator this is
 * @dev      Device bound to the allocator
 * @kref     Reference counter
 * @pid      PID to enforce one allocator per process limit
 */
struct ethosn_dma_allocator {
	enum ethosn_alloc_type type;
	uint32_t               alloc_id;
	struct device          *dev;
	struct kref            kref;
	pid_t                  pid;
	__u8                   is_protected;
};

/**
 * struct ethosn_dma_prot_range - A range of addresses along with
 *                            memory protection flags (read/write).
 */
struct ethosn_dma_prot_range {
	size_t start;
	size_t end;
	int    prot;
};

/**
 * Types of allocators
 */
struct ethosn_main_allocator {
	struct ethosn_dma_sub_allocator *firmware;
	struct ethosn_dma_sub_allocator *working_data;
	struct ethosn_dma_allocator     allocator;
};

struct ethosn_asset_allocator {
	struct ethosn_dma_sub_allocator *command_stream;
	struct ethosn_dma_sub_allocator *weight_data;
	struct ethosn_dma_sub_allocator *io_buffer;
	struct ethosn_dma_sub_allocator *intermediate_buffer;
	struct ethosn_dma_allocator     allocator;
};

struct ethosn_carveout_allocator {
	struct ethosn_dma_sub_allocator *carveout;
	struct ethosn_dma_allocator     allocator;
};

/**
 * struct ethosn_dma_allocator_ops - Allocator operations for DMA memory
 * @destroy:           Deinitialize the DMA memory allocator and free private
 *                     resources
 * @alloc:             Allocate DMA memory
 * @free               Free DMA memory allocated with alloc
 * @map                Map virtual addresses
 * @from_protected     Use protected memory as DMA memory
 * @import             Attach and map the buffer into the device memory space
 * @release            Release and unmap the buffer from the device memory space
 * @unmap              Unmap virtual addresses
 * @sync_for_device    Transfer ownership of the memory buffer to the Ethos-N by
 *                     flushing the CPU cache
 * @sync_for_cpu       Transfer ownership of the memory buffer to the CPU by
 *                     invalidating the CPU cache
 * @mmap               Memory map the buffer into userspace
 * @get_addr_base      Get address base
 * @get_addr_size      Get address size
 */
struct ethosn_dma_allocator_ops {
	void                   (*destroy)(struct ethosn_dma_sub_allocator *
					  allocator);

	struct ethosn_dma_info *(*alloc)(struct ethosn_dma_sub_allocator *
					 allocator,
					 size_t size,
					 gfp_t gfp);
	int (*map)(struct ethosn_dma_sub_allocator *allocator,
		   struct ethosn_dma_info *dma_info,
		   struct ethosn_dma_prot_range *prot_ranges,
		   size_t num_prot_ranges);
	struct ethosn_dma_info *(*from_protected)(struct
						  ethosn_dma_sub_allocator *
						  allocator,
						  phys_addr_t start_addr,
						  size_t size);
	struct ethosn_dma_info *(*import)(struct ethosn_dma_sub_allocator *
					  allocator,
					  int fd,
					  size_t size);
	void (*release)(struct ethosn_dma_sub_allocator *
			allocator,
			struct ethosn_dma_info **
			dma_info);
	void (*unmap)(struct ethosn_dma_sub_allocator *allocator,
		      struct ethosn_dma_info *dma_info);
	void (*free)(struct ethosn_dma_sub_allocator *allocator,
		     struct ethosn_dma_info **dma_info);
	void (*sync_for_device)(struct ethosn_dma_sub_allocator *
				allocator,
				struct ethosn_dma_info *
				dma_info);
	void (*sync_for_cpu)(struct ethosn_dma_sub_allocator *
			     allocator,
			     struct ethosn_dma_info *dma_info);
	int  (*mmap)(struct ethosn_dma_sub_allocator *allocator,
		     struct vm_area_struct *const vma,
		     const struct ethosn_dma_info *const
		     dma_info);
	dma_addr_t      (*get_addr_base)(struct ethosn_dma_sub_allocator *
					 allocator,
					 enum ethosn_stream_type stream_type);
	resource_size_t (*get_addr_size)(struct ethosn_dma_sub_allocator *
					 allocator,
					 enum ethosn_stream_type stream_type);
};

/**
 * ethosn_dma_top_allocator_create() - Initializes a container for DMA memory
 * allocators
 * @dev: Device to create the top-level allocator against
 * @type: Top-level allocator type to create, determines what sub-allocators it
 * can store
 *
 * Return:
 *  Pointer to ethosn_dma_allocator struct representing the DMA memory allocator
 *  Or NULL or negative error code on failure
 */
struct ethosn_dma_allocator *ethosn_dma_top_allocator_create(struct device *dev,
							     enum
							     ethosn_alloc_type
							     type);

/**
 * ethosn_dma_top_allocator_destroy() - Destroy the allocator container.
 * Does not destroy the DMA allocators which must be destroyed beforehand
 * @dev: Device that the top-level allocator was created with
 * @top_allocator: Pointer to the location of the top-level allocator to
 * destroy
 *
 * Return:
 *   0 or negative error code on failure
 */
int ethosn_dma_top_allocator_destroy(struct device *dev,
				     struct ethosn_dma_allocator
				     **top_allocator);

/**
 * ethosn_dma_sub_allocator_create() - Initializes a DMA memory allocator
 * @dev: Device to create the DMA memory allocator against
 * @top_allocator: Pointer to the top-level allocator to make the sub-allocator
 * under
 * @stream_type: Stream type to select the sub-allocator to create
 * @addr_base: Base address for the stream
 * @speculative_page_addr: Optional address to page for speculative accesses
 * @is_smmu_available: Is SMMU available in this device.
 *
 * If speculative_page_addr is 0x0, the sub allocator will provide a page for
 * the speculative accesses.
 *
 * Return:
 *  0 or negative error code on failure
 */
int ethosn_dma_sub_allocator_create(struct device *dev,
				    struct ethosn_dma_allocator *top_allocator,
				    enum ethosn_stream_type stream_type,
				    dma_addr_t addr_base,
				    phys_addr_t speculative_page_addr,
				    bool is_smmu_available);

/**
 * ethosn_dma_sub_allocator_destroy() - Destroy the sub-allocator and free all
 * internal resources.
 * @top_allocator: Pointer to top-level allocator containing the sub-allocator
 * to be destroyed
 * @stream_type: Stream type to select the sub-allocator to destroy
 */
void ethosn_dma_sub_allocator_destroy(
	struct ethosn_dma_allocator *top_allocator,
	enum ethosn_stream_type stream_type);

/**
 * ethosn_dma_alloc_and_map() - Allocate and map DMA memory
 * @allocator: Top-level allocator for sub-allocators
 * @size: bytes of memory
 * @prot: read/write protection
 * @stream_type: Stream type to select the sub-allocator to use
 * @gfp: GFP flags
 * @debug_tag: (optional) string to identify the allocation. This will be
 *   printed to the console if debug prints are enabled.
 *
 * Return:
 *  Pointer to ethosn_dma_info struct representing the allocation
 *  Or NULL or negative error code on failure
 */
struct ethosn_dma_info *ethosn_dma_alloc_and_map(
	struct ethosn_dma_allocator *allocator,
	size_t size,
	int prot,
	enum ethosn_stream_type stream_type,
	gfp_t gfp,
	const char *debug_tag);

/**
 * ethosn_dma_alloc() - Allocate DMA memory without mapping
 * @top_allocator: Top-level allocator for sub-allocators
 * @size: bytes of memory
 * @stream_type: Stream type to select the sub-allocator to use
 * @gfp: GFP flags
 * @debug_tag: (optional) string to identify the allocation. This will be
 *   printed to the console if debug prints are enabled.
 *
 * Return:
 *  Pointer to ethosn_dma_info struct representing the allocation
 *  Or NULL or negative error code on failure
 */
struct ethosn_dma_info *ethosn_dma_alloc(
	struct ethosn_dma_allocator *top_allocator,
	size_t size,
	enum ethosn_stream_type stream_type,
	gfp_t gfp,
	const char *debug_tag);

/**
 * ethosn_dma_firmware_from_protected() - Setup DMA memory for NPU firmware in
 *                                        protected memory
 * @top_allocator: Top-level allocator for sub-allocators
 * @start_addr: Physical start address of the memory holding the NPU firmware
 * @size: bytes of memory
 *
 * DMA memory setup with this function will always be assigned to
 * ETHOSN_STREAM_FIRMWARE.
 *
 * Return:
 *  Pointer to ethosn_dma_info struct representing the protected memory
 *  Or NULL or negative error code on failure
 */
struct ethosn_dma_info *ethosn_dma_firmware_from_protected(
	struct ethosn_dma_allocator *top_allocator,
	phys_addr_t start_addr,
	size_t size);

/**
 * ethosn_dma_map() - Map DMA memory
 * @top_allocator: Top-level allocator for sub-allocators
 * @dma_info: Pointer to ethosn_dma_info struct representing the allocation
 * @prot: read/write protection
 *
 * Return:
 *  0 or negative on failure
 */
int ethosn_dma_map(struct ethosn_dma_allocator *top_allocator,
		   struct ethosn_dma_info *dma_info,
		   int prot);

/**
 * ethosn_dma_map_with_prot_ranges() - Same as ethosn_dma_map,
 *      except it supports mapping with different protection
 *      flags (read/write) for different regions of the memory
 *      being mapped.
 *
 * @top_allocator: Top-level allocator for sub-allocators
 * @dma_info: Pointer to ethosn_dma_info struct representing the allocation
 * @prot_ranges: Array specifying which read/write protection to use for which
 *               ranges of addresses within the allocation.
 * @num_prot_ranges: Length of the `prot_ranges` array.
 *
 * Return:
 *  0 or negative on failure
 */
int ethosn_dma_map_with_prot_ranges(struct ethosn_dma_allocator *top_allocator,
				    struct ethosn_dma_info *dma_info,
				    struct ethosn_dma_prot_range *prot_ranges,
				    size_t num_prot_ranges);

/**
 * ethosn_dma_unmap() - Unmap DMA memory
 * @top_allocator: Top-level allocator for sub-allocators
 * @dma_info: Allocation information
 */
void ethosn_dma_unmap(struct ethosn_dma_allocator *top_allocator,
		      struct ethosn_dma_info *dma_info);

/**
 * ethosn_dma_unmap_and_release() - Unmap and Release allocated DMA
 * @top_allocator: Top-level allocator for sub-allocators
 * @dma_info: Allocation information
 */
void ethosn_dma_unmap_and_release(struct ethosn_dma_allocator *top_allocator,
				  struct ethosn_dma_info **dma_info);

/**
 * ethosn_dma_get_addr_base() - Get base address of a given stream
 * @top_allocator: Top-level allocator for sub-allocators
 * @stream_type: Stream type to select the sub-allocator to use
 *
 * Return:
 *  Base address or zero on failure
 */
dma_addr_t ethosn_dma_get_addr_base(struct ethosn_dma_allocator *top_allocator,
				    enum ethosn_stream_type stream_type);

/**
 * ethosn_dma_get_addr_size() - Get address space size of a given stream
 * @top_allocator: Top-level allocator for sub-allocators
 * @stream_type: Stream type to select the sub-allocator to use
 *
 * Return:
 *  Size of address space or zero on failure
 */
resource_size_t ethosn_dma_get_addr_size(
	struct ethosn_dma_allocator *top_allocator,
	enum ethosn_stream_type stream_type);

/**
 * ethosn_dma_mmap() - Do MMAP of DMA allocated memory
 * @top_allocator: Top-level allocator for sub-allocators
 * @vma: memory area
 * @dma_info: DMA allocation information
 *
 * Return:
 * * 0 - Success
 * * Negative error code
 */
int ethosn_dma_mmap(struct ethosn_dma_allocator *top_allocator,
		    struct vm_area_struct *const vma,
		    const struct ethosn_dma_info *const dma_info);

/**
 * ethosn_dma_sync_for_device() - Transfer ownership of the memory buffer to
 * the device. Flushes the CPU cache.
 * @top_allocator: Top-level allocator for sub-allocators
 * @dma_info: DMA allocation information
 */
void ethosn_dma_sync_for_device(struct ethosn_dma_allocator *top_allocator,
				struct ethosn_dma_info *dma_info);

/**
 * ethosn_dma_sync_for_cpu() - Transfer ownership of the memory buffer to
 * the cpu. Invalidates the CPU cache.
 * @top_allocator: Top-level allocator for sub-allocators
 * @dma_info: DMA allocation information
 */
void ethosn_dma_sync_for_cpu(struct ethosn_dma_allocator *top_allocator,
			     struct ethosn_dma_info *dma_info);

/**
 * ethosn_dma_import() - Import shared DMA buffer
 * @top_allocator: Top-level allocator for sub-allocators
 * @fd: file descriptor for the shared DMA buffer
 * @size: size of the shared buffer
 * @stream_type: Stream type to select the sub-allocator to use
 *
 * Return:
 * * Pointer to DMA allocation information on success
 * * NULL or negative error code on failure
 */
struct ethosn_dma_info *ethosn_dma_import(
	struct ethosn_dma_allocator *top_allocator,
	int fd,
	size_t size,
	enum ethosn_stream_type stream_type);

/**
 * ethosn_dma_release() - Release a DMA buffer. If the buffer was allocated
 * it'll be freed, else it'll be released.
 * @top_allocator: Top-level allocator for sub-allocators
 * @dma_info: Allocation information
 */
void ethosn_dma_release(struct ethosn_dma_allocator *top_allocator,
			struct ethosn_dma_info **dma_info);

/**
 * ethosn_get_sub_allocator
 * @top_allocator: Top-level allocator for sub-allocators
 * @stream_type: Stream type to select the sub-allocator to get
 *
 * Return:
 * * Pointer to sub-allocator
 * * NULL on error or when sub-allocator doesn't exist
 */
struct ethosn_dma_sub_allocator *ethosn_get_sub_allocator(
	struct ethosn_dma_allocator *top_allocator,
	enum ethosn_stream_type stream_type);

#endif /* _ETHOSN_DMA_H_ */
