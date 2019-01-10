/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifndef _KBASE_GATOR_API_H_
#define _KBASE_GATOR_API_H_

/**
 * @brief This file describes the API used by Gator to fetch hardware counters.
 */

/* This define is used by the gator kernel module compile to select which DDK
 * API calling convention to use. If not defined (legacy DDK) gator assumes
 * version 1. The version to DDK release mapping is:
 *     Version 1 API: DDK versions r1px, r2px
 *     Version 2 API: DDK versions r3px, r4px
 *     Version 3 API: DDK version r5p0 and newer
 *
 * API Usage
 * =========
 *
 * 1] Call kbase_gator_hwcnt_init_names() to return the list of short counter
 * names for the GPU present in this device.
 *
 * 2] Create a kbase_gator_hwcnt_info structure and set the counter enables for
 * the counters you want enabled. The enables can all be set for simplicity in
 * most use cases, but disabling some will let you minimize bandwidth impact.
 *
 * 3] Call kbase_gator_hwcnt_init() using the above structure, to create a
 * counter context. On successful return the DDK will have populated the
 * structure with a variety of useful information.
 *
 * 4] Call kbase_gator_hwcnt_dump_irq() to queue a non-blocking request for a
 * counter dump. If this returns a non-zero value the request has been queued,
 * otherwise the driver has been unable to do so (typically because of another
 * user of the instrumentation exists concurrently).
 *
 * 5] Call kbase_gator_hwcnt_dump_complete() to test whether the  previously
 * requested dump has been succesful. If this returns non-zero the counter dump
 * has resolved, but the value of *success must also be tested as the dump
 * may have not been successful. If it returns zero the counter dump was
 * abandoned due to the device being busy (typically because of another
 * user of the instrumentation exists concurrently).
 *
 * 6] Process the counters stored in the buffer pointed to by ...
 *
 *        kbase_gator_hwcnt_info->kernel_dump_buffer
 *
 *    In pseudo code you can find all of the counters via this approach:
 *
 *
 *        hwcnt_info # pointer to kbase_gator_hwcnt_info structure
 *        hwcnt_name # pointer to name list
 *
 *        u32 * hwcnt_data = (u32*)hwcnt_info->kernel_dump_buffer
 *
 *        # Iterate over each 64-counter block in this GPU configuration
 *        for( i = 0; i < hwcnt_info->nr_hwc_blocks; i++) {
 *            hwc_type type = hwcnt_info->hwc_layout[i];
 *
 *            # Skip reserved type blocks - they contain no counters at all
 *            if( type == RESERVED_BLOCK ) {
 *                continue;
 *            }
 *
 *            size_t name_offset = type * 64;
 *            size_t data_offset = i * 64;
 *
 *            # Iterate over the names of the counters in this block type
 *            for( j = 0; j < 64; j++) {
 *                const char * name = hwcnt_name[name_offset+j];
 *
 *                # Skip empty name strings - there is no counter here
 *                if( name[0] == '\0' ) {
 *                    continue;
 *                }
 *
 *                u32 data = hwcnt_data[data_offset+j];
 *
 *                printk( "COUNTER: %s DATA: %u\n", name, data );
 *            }
 *        }
 *
 *
 *     Note that in most implementations you typically want to either SUM or
 *     AVERAGE multiple instances of the same counter if, for example, you have
 *     multiple shader cores or multiple L2 caches. The most sensible view for
 *     analysis is to AVERAGE shader core counters, but SUM L2 cache and MMU
 *     counters.
 *
 * 7] Goto 4, repeating until you want to stop collecting counters.
 *
 * 8] Release the dump resources by calling kbase_gator_hwcnt_term().
 *
 * 9] Release the name table resources by calling
 *    kbase_gator_hwcnt_term_names(). This function must only be called if
 *    init_names() returned a non-NULL value.
 **/

#define MALI_DDK_GATOR_API_VERSION 3

enum hwc_type {
	JM_BLOCK = 0,
	TILER_BLOCK,
	SHADER_BLOCK,
	MMU_L2_BLOCK,
	RESERVED_BLOCK
};

struct kbase_gator_hwcnt_info {
	/* Passed from Gator to kbase */

	/* the bitmask of enabled hardware counters for each counter block */
	uint16_t bitmask[4];

	/* Passed from kbase to Gator */

	/* ptr to counter dump memory */
	void *kernel_dump_buffer;

	/* size of counter dump memory */
	uint32_t size;

	/* the ID of the Mali device */
	uint32_t gpu_id;

	/* the number of shader cores in the GPU */
	uint32_t nr_cores;

	/* the number of core groups */
	uint32_t nr_core_groups;

	/* the memory layout of the performance counters */
	enum hwc_type *hwc_layout;

	/* the total number of hardware couter blocks */
	uint32_t nr_hwc_blocks;
};

/**
 * @brief Opaque block of Mali data which Gator needs to return to the API later.
 */
struct kbase_gator_hwcnt_handles;

/**
 * @brief Initialize the resources Gator needs for performance profiling.
 *
 * @param in_out_info   A pointer to a structure containing the enabled counters passed from Gator and all the Mali
 *                      specific information that will be returned to Gator. On entry Gator must have populated the
 *                      'bitmask' field with the counters it wishes to enable for each class of counter block.
 *                      Each entry in the array corresponds to a single counter class based on the "hwc_type"
 *                      enumeration, and each bit corresponds to an enable for 4 sequential counters (LSB enables
 *                      the first 4 counters in the block, and so on). See the GPU counter array as returned by
 *                      kbase_gator_hwcnt_get_names() for the index values of each counter for the curernt GPU.
 *
 * @return              Pointer to an opaque handle block on success, NULL on error.
 */
extern struct kbase_gator_hwcnt_handles *kbase_gator_hwcnt_init(struct kbase_gator_hwcnt_info *in_out_info);

/**
 * @brief Free all resources once Gator has finished using performance counters.
 *
 * @param in_out_info       A pointer to a structure containing the enabled counters passed from Gator and all the
 *                          Mali specific information that will be returned to Gator.
 * @param opaque_handles    A wrapper structure for kbase structures.
 */
extern void kbase_gator_hwcnt_term(struct kbase_gator_hwcnt_info *in_out_info, struct kbase_gator_hwcnt_handles *opaque_handles);

/**
 * @brief Poll whether a counter dump is successful.
 *
 * @param opaque_handles    A wrapper structure for kbase structures.
 * @param[out] success      Non-zero on success, zero on failure.
 *
 * @return                  Zero if the dump is still pending, non-zero if the dump has completed. Note that a
 *                          completed dump may not have dumped succesfully, so the caller must test for both
 *                          a completed and successful dump before processing counters.
 */
extern uint32_t kbase_gator_instr_hwcnt_dump_complete(struct kbase_gator_hwcnt_handles *opaque_handles, uint32_t * const success);

/**
 * @brief Request the generation of a new counter dump.
 *
 * @param opaque_handles    A wrapper structure for kbase structures.
 *
 * @return                  Zero if the hardware device is busy and cannot handle the request, non-zero otherwise.
 */
extern uint32_t kbase_gator_instr_hwcnt_dump_irq(struct kbase_gator_hwcnt_handles *opaque_handles);

/**
 * @brief This function is used to fetch the names table based on the Mali device in use.
 *
 * @param[out] total_counters The total number of counters short names in the Mali devices' list.
 *
 * @return                    Pointer to an array of strings of length *total_counters.
 */
extern const char * const *kbase_gator_hwcnt_init_names(uint32_t *total_counters);

/**
 * @brief This function is used to terminate the use of the names table.
 *
 * This function must only be called if the initial call to kbase_gator_hwcnt_init_names returned a non-NULL value.
 */
extern void kbase_gator_hwcnt_term_names(void);

#endif
