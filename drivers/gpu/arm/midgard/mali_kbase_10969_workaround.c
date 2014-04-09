/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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



#include <mali_kbase.h>

/* This function is used to solve an HW issue with single iterator GPUs.
 * If a fragment job is soft-stopped on the edge of its bounding box, can happen that the
 * restart index is out of bounds and the rerun causes a tile range fault. If this happens
 * we try to clamp the restart index to a correct value and rerun the job.
 */
/* Mask of X and Y coordinates for the coordinates words in the descriptors*/
#define X_COORDINATE_MASK 0x00000FFF
#define Y_COORDINATE_MASK 0x0FFF0000
/* Max number of words needed from the fragment shader job descriptor */
#define JOB_HEADER_SIZE_IN_WORDS 10
#define JOB_HEADER_SIZE (JOB_HEADER_SIZE_IN_WORDS*sizeof(u32))

/* Word 0: Status Word */
#define JOB_DESC_STATUS_WORD 0
/* Word 1: Restart Index */
#define JOB_DESC_RESTART_INDEX_WORD 1
/* Word 2: Fault address low word */
#define JOB_DESC_FAULT_ADDR_LOW_WORD 2
/* Word 8: Minimum Tile Coordinates */
#define FRAG_JOB_DESC_MIN_TILE_COORD_WORD 8
/* Word 9: Maximum Tile Coordinates */
#define FRAG_JOB_DESC_MAX_TILE_COORD_WORD 9

int kbasep_10969_workaround_clamp_coordinates(kbase_jd_atom *katom)
{
	struct device *dev = katom->kctx->kbdev->dev;
	u32   clamped = 0;
	dev_warn(dev,"Called TILE_RANGE_FAULT workaround clamping function. \n");
	if (katom->core_req & BASE_JD_REQ_FS){
		kbase_va_region * region = kbase_region_tracker_find_region_enclosing_address(katom->kctx, katom->jc );

		if (region){
			phys_addr_t * page_array = kbase_get_phy_pages(region);

			if (page_array){
				u64 page_index = (katom->jc >> PAGE_SHIFT) - region->start_pfn;
				u32 offset = katom->jc & (~PAGE_MASK);
				u32 * page_1 = NULL;
				u32 * page_2 = NULL;
				u32   job_header[JOB_HEADER_SIZE_IN_WORDS];
				void* dst = job_header;

				/* we need the first 10 words of the fragment shader job descriptor. We need to check
				 * that the offset + 10 words is less that the page size otherwise we need to load the next
				 * page. page_size_overflow will be equal to 0 in case the whole descriptor is within the page
				 * >0 otherwise.
				 */
				u32 copy_size = MIN(PAGE_SIZE - offset, JOB_HEADER_SIZE);

				page_1 = kmap_atomic(pfn_to_page(PFN_DOWN(page_array[page_index])));

				/* page_1 is a u32 pointer, offset is expressed in bytes */
				page_1 += offset>>2;
				kbase_sync_to_cpu(page_array[page_index] + offset, page_1, copy_size);
				memcpy(dst, page_1, copy_size);

				/* The data needed overflows page the dimension, need to map the subsequent page */
				if (copy_size < JOB_HEADER_SIZE){
					page_2 = kmap_atomic(pfn_to_page(PFN_DOWN(page_array[page_index + 1])));

					kbase_sync_to_cpu(page_array[page_index + 1], page_2, JOB_HEADER_SIZE - copy_size);
					memcpy(dst + copy_size, page_2, JOB_HEADER_SIZE - copy_size);
				}

				/* We managed to correctly map one or two pages (in case of overflow ) */
				{
					u32 minX,minY,maxX,maxY;
					u32 restartX,restartY;

					/* Get Bounding Box data and restart index from fault address low word*/
					minX     = job_header[FRAG_JOB_DESC_MIN_TILE_COORD_WORD] & X_COORDINATE_MASK;
					minY     = job_header[FRAG_JOB_DESC_MIN_TILE_COORD_WORD] & Y_COORDINATE_MASK;
					maxX     = job_header[FRAG_JOB_DESC_MAX_TILE_COORD_WORD] & X_COORDINATE_MASK;
					maxY     = job_header[FRAG_JOB_DESC_MAX_TILE_COORD_WORD] & Y_COORDINATE_MASK;
					restartX = job_header[JOB_DESC_FAULT_ADDR_LOW_WORD] & X_COORDINATE_MASK;
					restartY = job_header[JOB_DESC_FAULT_ADDR_LOW_WORD] & Y_COORDINATE_MASK;

					dev_warn(dev, "Before Clamping: \n" \
					              "Jobstatus: %08x  \n" \
					              "restartIdx: %08x  \n" \
					              "Fault_addr_low: %08x \n" \
					              "minCoordsX: %08x minCoordsY: %08x \n" \
					              "maxCoordsX: %08x maxCoordsY: %08x \n", 
					              job_header[JOB_DESC_STATUS_WORD],
					              job_header[JOB_DESC_RESTART_INDEX_WORD],
					              job_header[JOB_DESC_FAULT_ADDR_LOW_WORD],
					              minX,minY,
					              maxX,maxY );

					/* Set the restart index to the one which generated the fault*/
					job_header[JOB_DESC_RESTART_INDEX_WORD] = job_header[JOB_DESC_FAULT_ADDR_LOW_WORD];

					if (restartX < minX){
						job_header[JOB_DESC_RESTART_INDEX_WORD] = (minX) | restartY;
						dev_warn(dev,
						         "Clamping restart X index to minimum. %08x clamped to %08x \n",
						         restartX, minX );
						clamped =  1;
					}
					if (restartY < minY){
						job_header[JOB_DESC_RESTART_INDEX_WORD] = (minY) | restartX;
						dev_warn(dev,
						         "Clamping restart Y index to minimum. %08x clamped to %08x \n",
						         restartY, minY );
						clamped =  1;
					}
					if (restartX > maxX){
						job_header[JOB_DESC_RESTART_INDEX_WORD] = (maxX) | restartY;
						dev_warn(dev,
						         "Clamping restart X index to maximum. %08x clamped to %08x \n",
						         restartX, maxX );
						clamped =  1;
					}
					if (restartY > maxY){
						job_header[JOB_DESC_RESTART_INDEX_WORD] = (maxY) | restartX;
						dev_warn(dev,
						         "Clamping restart Y index to maximum. %08x clamped to %08x \n",
						         restartY, maxY );
						clamped =  1;
					}

					if (clamped){
						/* Reset the fault address low word and set the job status to STOPPED */
						job_header[JOB_DESC_FAULT_ADDR_LOW_WORD] = 0x0;
						job_header[JOB_DESC_STATUS_WORD] = BASE_JD_EVENT_STOPPED;
						dev_warn(dev, "After Clamping: \n"                   \
						              "Jobstatus: %08x  \n"                  \
						              "restartIdx: %08x  \n"                 \
						              "Fault_addr_low: %08x \n"              \
						              "minCoordsX: %08x minCoordsY: %08x \n" \
						              "maxCoordsX: %08x maxCoordsY: %08x \n", 
						              job_header[JOB_DESC_STATUS_WORD],
						              job_header[JOB_DESC_RESTART_INDEX_WORD],
						              job_header[JOB_DESC_FAULT_ADDR_LOW_WORD],
						              minX,minY,
						              maxX,maxY );

						/* Flush CPU cache to update memory for future GPU reads*/
						memcpy(page_1, dst, copy_size);
						kbase_sync_to_memory(page_array[page_index] + offset, page_1, copy_size);

						if (copy_size < JOB_HEADER_SIZE){
							 memcpy(page_2, dst + copy_size, JOB_HEADER_SIZE - copy_size);
							 kbase_sync_to_memory(page_array[page_index + 1], page_2, JOB_HEADER_SIZE - copy_size);
						}

					}
				}
				if (copy_size < JOB_HEADER_SIZE) 
					kunmap_atomic(page_2);

				kunmap_atomic(page_1);
			}
		}
	}
	return clamped;
}
