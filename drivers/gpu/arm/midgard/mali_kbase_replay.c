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



/**
 * @file mali_kbase_replay.c
 * Replay soft job handlers
 */

#include <mali_kbase_config.h>
#include <mali_kbase.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_debug.h>

#define JOB_NOT_STARTED 0

#define JOB_TYPE_MASK      0xfe
#define JOB_TYPE_NULL      (1 << 1)
#define JOB_TYPE_VERTEX    (5 << 1)
#define JOB_TYPE_TILER     (7 << 1)
#define JOB_TYPE_FUSED     (8 << 1)
#define JOB_TYPE_FRAGMENT  (9 << 1)

#define JOB_FLAG_DESC_SIZE           (1 << 0)
#define JOB_FLAG_PERFORM_JOB_BARRIER (1 << 8)

#define JOB_HEADER_32_FBD_OFFSET (31*4)

#define FBD_POINTER_MASK (~0x3f)

#define SFBD_TILER_OFFSET (48*4)

#define MFBD_TILER_FLAGS_OFFSET (15*4)
#define MFBD_TILER_OFFSET       (16*4)

#define FBD_HIERARCHY_WEIGHTS 8
#define FBD_HIERARCHY_MASK_MASK 0x1fff

#define FBD_TYPE 1

#define HIERARCHY_WEIGHTS 13

#define JOB_HEADER_ID_MAX                 0xffff

typedef struct job_head
{
	u32 status;
	u32 not_complete_index;
	u64 fault_addr;
	u16 flags;
	u16 index;
	u16 dependencies[2];
	union
	{
		u64 _64;
		u32 _32;
	} next;
	u32 x[2];
	union
	{
		u64 _64;
		u32 _32;
	} fragment_fbd;
} job_head;

static void dump_job_head(kbase_context *kctx, char *head_str, job_head *job)
{
#ifdef CONFIG_MALI_DEBUG
	struct device *dev = kctx->kbdev->dev;

	KBASE_LOG(2, dev, "%s\n", head_str);
	KBASE_LOG(2, dev, "addr               = %p\n"
					"status             = %x\n"
					"not_complete_index = %x\n"
					"fault_addr         = %llx\n"
					"flags              = %x\n"
					"index              = %x\n"
					"dependencies       = %x,%x\n",
									   job,
								   job->status,
						       job->not_complete_index,
							       job->fault_addr,
								    job->flags,
								    job->index,
							  job->dependencies[0],
							 job->dependencies[1]);

	if (job->flags & JOB_FLAG_DESC_SIZE)
		KBASE_LOG(2, dev, "next               = %llx\n", job->next._64);
	else
		KBASE_LOG(2, dev, "next               = %x\n", job->next._32);
#endif
}


static void *kbasep_map_page(kbase_context *kctx, mali_addr64 gpu_addr,
								u64 *phys_addr)
{
	void *cpu_addr = NULL;
	u64 page_index;
	kbase_va_region *region;
	phys_addr_t *page_array;

	region = kbase_region_tracker_find_region_enclosing_address(kctx,
								     gpu_addr);
	if (!region || (region->flags & KBASE_REG_FREE))
		return NULL;

	page_index = (gpu_addr >> PAGE_SHIFT) - region->start_pfn;
	if (page_index >= kbase_reg_current_backed_size(region))
		return NULL;

	page_array = kbase_get_phy_pages(region);
	if (!page_array)
		return NULL;

	cpu_addr = kmap_atomic(pfn_to_page(PFN_DOWN(page_array[page_index])));
	if (!cpu_addr)
		return NULL;

	if (phys_addr)
		*phys_addr = page_array[page_index];

	return cpu_addr + (gpu_addr & ~PAGE_MASK);
}

static void *kbasep_map_page_sync(kbase_context *kctx, mali_addr64 gpu_addr,
								u64 *phys_addr)
{
	void *cpu_addr = kbasep_map_page(kctx, gpu_addr, phys_addr);

	if (!cpu_addr)
		return NULL;

	kbase_sync_to_cpu(*phys_addr,
				 (void *)((uintptr_t)cpu_addr & PAGE_MASK),
								    PAGE_SIZE);

	return cpu_addr;
}

static void kbasep_unmap_page(void *cpu_addr)
{
	kunmap_atomic((void *)((uintptr_t)cpu_addr & PAGE_MASK));
}

static void kbasep_unmap_page_sync(void *cpu_addr, u64 phys_addr)
{
	kbase_sync_to_memory(phys_addr,
				 (void *)((uintptr_t)cpu_addr & PAGE_MASK),
								    PAGE_SIZE);

	kunmap_atomic((void *)((uintptr_t)cpu_addr & PAGE_MASK));
}

static mali_error kbasep_replay_reset_sfbd(kbase_context *kctx,
					   mali_addr64 fbd_address,
					   mali_addr64 tiler_heap_free,
					   u16 hierarchy_mask,
					   u32 default_weight)
{
	u64 phys_addr;
	struct
	{
		u32 padding_1[1];
		u32 flags;
		u64 padding_2[2];
		u64 heap_free_address;
		u32 padding[8];
		u32 weights[FBD_HIERARCHY_WEIGHTS];
	} *fbd_tiler;
	struct device *dev = kctx->kbdev->dev;

	KBASE_LOG(2, dev, "fbd_address: %llx\n", fbd_address);

	fbd_tiler = kbasep_map_page_sync(kctx, fbd_address + SFBD_TILER_OFFSET,
								   &phys_addr);
	if (!fbd_tiler) {
		dev_err(dev, "kbasep_replay_reset_fbd: failed to map fbd\n");
		return MALI_ERROR_FUNCTION_FAILED;
	}
#ifdef CONFIG_MALI_DEBUG
	KBASE_LOG(2, dev, "FBD tiler:\n"
				"flags = %x\n"
				"heap_free_address = %llx\n",
							      fbd_tiler->flags,
						 fbd_tiler->heap_free_address);
#endif
	if (hierarchy_mask) {
		u32 weights[HIERARCHY_WEIGHTS];
		u16 old_hierarchy_mask = fbd_tiler->flags &
						       FBD_HIERARCHY_MASK_MASK;
		int i, j = 0;

		for (i = 0; i < HIERARCHY_WEIGHTS; i++) {
			if (old_hierarchy_mask & (1 << i)) {
				KBASE_DEBUG_ASSERT(j < FBD_HIERARCHY_WEIGHTS);
				weights[i] = fbd_tiler->weights[j++];
			} else {
				weights[i] = default_weight;
			}
		}


		KBASE_LOG(2, dev,
			      "Old hierarchy mask=%x  New hierarchy mask=%x\n",
					   old_hierarchy_mask, hierarchy_mask);
		for (i = 0; i < HIERARCHY_WEIGHTS; i++)
			KBASE_LOG(2, dev, " Hierarchy weight %02d: %08x\n",
								i, weights[i]);

		j = 0;

		for (i = 0; i < HIERARCHY_WEIGHTS; i++) {
			if (hierarchy_mask & (1 << i)) {
				KBASE_DEBUG_ASSERT(j < FBD_HIERARCHY_WEIGHTS);

				KBASE_LOG(2, dev,
				" Writing hierarchy level %02d (%08x) to %d\n",
							     i, weights[i], j);

				fbd_tiler->weights[j++] = weights[i];
			}
		}

		for (; j < FBD_HIERARCHY_WEIGHTS; j++)
			fbd_tiler->weights[j] = 0;

		fbd_tiler->flags = hierarchy_mask | (1 << 16);
	}

	fbd_tiler->heap_free_address = tiler_heap_free;

	KBASE_LOG(2, dev, "heap_free_address=%llx flags=%x\n",
			       fbd_tiler->heap_free_address, fbd_tiler->flags);

	kbasep_unmap_page_sync(fbd_tiler, phys_addr);

	return MALI_ERROR_NONE;
}

static mali_error kbasep_replay_reset_mfbd(kbase_context *kctx,
					   mali_addr64 fbd_address,
					   mali_addr64 tiler_heap_free,
					   u16 hierarchy_mask,
					   u32 default_weight)
{
	u64 phys_addr, phys_addr_flags;
	struct
	{
		u64 padding_1[2];
		u64 heap_free_address;
		u64 padding_2;
		u32 weights[FBD_HIERARCHY_WEIGHTS];
	} *fbd_tiler;
	u32 *fbd_tiler_flags;
	mali_bool flags_different_page;
	struct device *dev = kctx->kbdev->dev;

	KBASE_LOG(2, dev, "fbd_address: %llx\n", fbd_address);

	fbd_tiler = kbasep_map_page_sync(kctx, fbd_address + MFBD_TILER_OFFSET,
								   &phys_addr);
	if (((fbd_address + MFBD_TILER_OFFSET) & PAGE_MASK) !=
	    ((fbd_address + MFBD_TILER_FLAGS_OFFSET) & PAGE_MASK)) {
		flags_different_page = MALI_TRUE;
		fbd_tiler_flags = kbasep_map_page_sync(kctx,
					 fbd_address + MFBD_TILER_FLAGS_OFFSET,
							     &phys_addr_flags);
	} else {
		flags_different_page = MALI_FALSE;
		fbd_tiler_flags = (u32 *)((uintptr_t)fbd_tiler -
				  MFBD_TILER_OFFSET + MFBD_TILER_FLAGS_OFFSET);
	}

	if (!fbd_tiler || !fbd_tiler_flags) {
		dev_err(dev, "kbasep_replay_reset_fbd: failed to map fbd\n");

		if (fbd_tiler_flags && flags_different_page)
			kbasep_unmap_page_sync(fbd_tiler_flags,
							      phys_addr_flags);
		if (fbd_tiler)
			kbasep_unmap_page_sync(fbd_tiler, phys_addr);

		return MALI_ERROR_FUNCTION_FAILED;
	}
#ifdef CONFIG_MALI_DEBUG
	KBASE_LOG(2, dev, "FBD tiler:\n"
				"heap_free_address = %llx\n",
				 fbd_tiler->heap_free_address);
#endif
	if (hierarchy_mask) {
		u32 weights[HIERARCHY_WEIGHTS];
		u16 old_hierarchy_mask = (*fbd_tiler_flags) &
						       FBD_HIERARCHY_MASK_MASK;
		int i, j = 0;

		for (i = 0; i < HIERARCHY_WEIGHTS; i++) {
			if (old_hierarchy_mask & (1 << i)) {
				KBASE_DEBUG_ASSERT(j < FBD_HIERARCHY_WEIGHTS);
				weights[i] = fbd_tiler->weights[j++];
			}
			else
				weights[i] = default_weight;
		}


		KBASE_LOG(2, dev,
			      "Old hierarchy mask=%x  New hierarchy mask=%x\n",
					   old_hierarchy_mask, hierarchy_mask);
		for (i = 0; i < HIERARCHY_WEIGHTS; i++)
			KBASE_LOG(2, dev, " Hierarchy weight %02d: %08x\n",
								i, weights[i]);

		j = 0;

		for (i = 0; i < HIERARCHY_WEIGHTS; i++) {
			if (hierarchy_mask & (1 << i)) {
				KBASE_DEBUG_ASSERT(j < FBD_HIERARCHY_WEIGHTS);

				KBASE_LOG(2, dev,
				" Writing hierarchy level %02d (%08x) to %d\n",
							     i, weights[i], j);

				fbd_tiler->weights[j++] = weights[i];
			}
		}

		for (; j < FBD_HIERARCHY_WEIGHTS; j++)
			fbd_tiler->weights[j] = 0;

		*fbd_tiler_flags = hierarchy_mask | (1 << 16);
	}

	fbd_tiler->heap_free_address = tiler_heap_free;

	if (flags_different_page)
		kbasep_unmap_page_sync(fbd_tiler_flags, phys_addr_flags);

	kbasep_unmap_page_sync(fbd_tiler, phys_addr);

	return MALI_ERROR_NONE;
}

/**
 * @brief Reset the status of an FBD pointed to by a tiler job
 *
 * This performs two functions :
 * - Set the hierarchy mask
 * - Reset the tiler free heap address
 *
 * @param[in] kctx              Context pointer
 * @param[in] job_header        Address of job header to reset.
 * @param[in] tiler_heap_free   The value to reset Tiler Heap Free to
 * @param[in] hierarchy_mask    The hierarchy mask to use
 * @param[in] default_weight    Default hierarchy weight to write when no other
 *                              weight is given in the FBD
 * @param[in] job_64            MALI_TRUE if this job is using 64-bit
 *                              descriptors
 *
 * @return MALI_ERROR_NONE on success, error code on failure
 */
static mali_error kbasep_replay_reset_tiler_job(kbase_context *kctx,
						mali_addr64 job_header,
						mali_addr64 tiler_heap_free,
						u16 hierarchy_mask,
						u32 default_weight,
						mali_bool job_64)
{
	mali_addr64 fbd_address;

	if (job_64) {
		dev_err(kctx->kbdev->dev,
				      "64-bit job descriptor not supported\n");
		return MALI_ERROR_FUNCTION_FAILED;
	} else {
		u32 *job_ext;	

		job_ext = kbasep_map_page(kctx,
					 job_header + JOB_HEADER_32_FBD_OFFSET,
									 NULL);
		if (!job_ext) {
			dev_err(kctx->kbdev->dev,
			  "kbasep_replay_reset_tiler_job: failed to map jc\n");
			return MALI_ERROR_FUNCTION_FAILED;
		}

		fbd_address = *job_ext;

		kbasep_unmap_page(job_ext);
	}

	if (fbd_address & FBD_TYPE) {
		return kbasep_replay_reset_mfbd(kctx,
						fbd_address & FBD_POINTER_MASK,
						tiler_heap_free,
						hierarchy_mask,
						default_weight);
	} else {
		return kbasep_replay_reset_sfbd(kctx,
						fbd_address & FBD_POINTER_MASK,
						tiler_heap_free,
						hierarchy_mask,
						default_weight);
	}
}

/**
 * @brief Reset the status of a job
 *
 * This performs the following functions :
 *
 * - Reset the Job Status field of each job to NOT_STARTED.
 * - Set the Job Type field of any Vertex Jobs to Null Job.
 * - For any jobs using an FBD, set the Tiler Heap Free field to the value of
 *   the tiler_heap_free parameter, and set the hierarchy level mask to the
 *   hier_mask parameter.
 * - Offset HW dependencies by the hw_job_id_offset parameter
 * - Set the Perform Job Barrier flag if this job is the first in the chain
 * - Read the address of the next job header
 *
 * @param[in] kctx              Context pointer
 * @param[in,out] job_header    Address of job header to reset. Set to address
 *                              of next job header on exit.
 * @param[in] prev_jc           Previous job chain to link to, if this job is
 *                              the last in the chain.
 * @param[in] hw_job_id_offset  Offset for HW job IDs
 * @param[in] tiler_heap_free   The value to reset Tiler Heap Free to
 * @param[in] hierarchy_mask    The hierarchy mask to use
 * @param[in] default_weight    Default hierarchy weight to write when no other
 *                              weight is given in the FBD
 * @param[in] first_in_chain    MALI_TRUE if this job is the first in the chain
 * @param[in] fragment_chain    MALI_TRUE if this job is in the fragment chain
 *
 * @return MALI_ERROR_NONE on success, error code on failure
 */
static mali_error kbasep_replay_reset_job(kbase_context *kctx,
						mali_addr64 *job_header,
						mali_addr64 prev_jc,
						mali_addr64 tiler_heap_free,
						u16 hierarchy_mask,
						u32 default_weight,
						u16 hw_job_id_offset,
						mali_bool first_in_chain,
						mali_bool fragment_chain)
{
	job_head *job;
	u64 phys_addr;
	mali_addr64 new_job_header;
	struct device *dev = kctx->kbdev->dev;

	job = kbasep_map_page_sync(kctx, *job_header, &phys_addr);
	if (!job) {
		dev_err(dev, "kbasep_replay_parse_jc: failed to map jc\n");
		return MALI_ERROR_FUNCTION_FAILED;
	}

	dump_job_head(kctx, "Job header:", job);

	if (job->status == JOB_NOT_STARTED && !fragment_chain) {
		dev_err(dev, "Job already not started\n");
		kbasep_unmap_page_sync(job, phys_addr);
		return MALI_ERROR_FUNCTION_FAILED;
	}
	job->status = JOB_NOT_STARTED;

	if ((job->flags & JOB_TYPE_MASK) == JOB_TYPE_VERTEX)
		job->flags = (job->flags & ~JOB_TYPE_MASK) | JOB_TYPE_NULL;

	if ((job->flags & JOB_TYPE_MASK) == JOB_TYPE_FUSED) {
		dev_err(dev, "Fused jobs can not be replayed\n");
		kbasep_unmap_page_sync(job, phys_addr);
		return MALI_ERROR_FUNCTION_FAILED;
	}

	if (first_in_chain)
		job->flags |= JOB_FLAG_PERFORM_JOB_BARRIER;

	if ((job->dependencies[0] + hw_job_id_offset) > JOB_HEADER_ID_MAX ||
	    (job->dependencies[1] + hw_job_id_offset) > JOB_HEADER_ID_MAX ||
	    (job->index + hw_job_id_offset) > JOB_HEADER_ID_MAX) {
		dev_err(dev, "Job indicies/dependencies out of valid range\n");
		kbasep_unmap_page_sync(job, phys_addr);
		return MALI_ERROR_FUNCTION_FAILED;
	}

	if (job->dependencies[0])
		job->dependencies[0] += hw_job_id_offset;
	if (job->dependencies[1])
		job->dependencies[1] += hw_job_id_offset;

	job->index += hw_job_id_offset;

	if (job->flags & JOB_FLAG_DESC_SIZE) {
		new_job_header = job->next._64;
		if (!job->next._64)
			job->next._64 = prev_jc;
	} else {
		new_job_header = job->next._32;
		if (!job->next._32)
			job->next._32 = prev_jc;
	}
	dump_job_head(kctx, "Updated to:", job);

	if ((job->flags & JOB_TYPE_MASK) == JOB_TYPE_TILER) {
		kbasep_unmap_page_sync(job, phys_addr);
		if (kbasep_replay_reset_tiler_job(kctx, *job_header,
					tiler_heap_free, hierarchy_mask, 
					default_weight,
					job->flags & JOB_FLAG_DESC_SIZE) !=
							MALI_ERROR_NONE)
			return MALI_ERROR_FUNCTION_FAILED;

	} else if ((job->flags & JOB_TYPE_MASK) == JOB_TYPE_FRAGMENT) {
		u64 fbd_address;

		if (job->flags & JOB_FLAG_DESC_SIZE) {
			kbasep_unmap_page_sync(job, phys_addr);
			dev_err(dev, "64-bit job descriptor not supported\n");
			return MALI_ERROR_FUNCTION_FAILED;
		} else {
			fbd_address = (u64)job->fragment_fbd._32;
		}

		kbasep_unmap_page_sync(job, phys_addr);

		if (fbd_address & FBD_TYPE) {
			if (kbasep_replay_reset_mfbd(kctx,
						fbd_address & FBD_POINTER_MASK,
						tiler_heap_free,
						hierarchy_mask,
						default_weight) !=
							       MALI_ERROR_NONE)
				return MALI_ERROR_FUNCTION_FAILED;
		} else {
			if (kbasep_replay_reset_sfbd(kctx,
						fbd_address & FBD_POINTER_MASK,
						tiler_heap_free,
						hierarchy_mask,
						default_weight) !=
							       MALI_ERROR_NONE)
				return MALI_ERROR_FUNCTION_FAILED;
		}
	} else {
		kbasep_unmap_page_sync(job, phys_addr);
	}

	*job_header = new_job_header;

	return MALI_ERROR_NONE;
}

/**
 * @brief Find the highest job ID in a job chain
 *
 * @param[in] kctx        Context pointer
 * @param[in] jc          Job chain start address
 * @param[out] hw_job_id  Highest job ID in chain
 *
 * @return MALI_ERROR_NONE on success, error code on failure
 */
static mali_error kbasep_replay_find_hw_job_id(kbase_context *kctx,
						mali_addr64 jc,
						u16 *hw_job_id)
{
	while (jc) {
		job_head *job;
		u64 phys_addr;

		KBASE_LOG(2, kctx->kbdev->dev,
			"kbasep_replay_find_hw_job_id: parsing jc=%llx\n", jc);

		job = kbasep_map_page_sync(kctx, jc, &phys_addr);
		if (!job) {
			dev_err(kctx->kbdev->dev, "failed to map jc\n");

			return MALI_ERROR_FUNCTION_FAILED;
		}

		if (job->index > *hw_job_id)
			*hw_job_id = job->index;

		if (job->flags & JOB_FLAG_DESC_SIZE)
			jc = job->next._64;
		else
			jc = job->next._32;

		kbasep_unmap_page_sync(job, phys_addr);
	}

	return MALI_ERROR_NONE;
}

/**
 * @brief Reset the status of a number of jobs
 *
 * This function walks the provided job chain, and calls
 * kbasep_replay_reset_job for each job. It also links the job chain to the
 * provided previous job chain.
 *
 * The function will fail if any of the jobs passed already have status of
 * NOT_STARTED.
 *
 * @param[in] kctx              Context pointer
 * @param[in] jc                Job chain to be processed
 * @param[in] prev_jc           Job chain to be added to. May be NULL
 * @param[in] tiler_heap_free   The value to reset Tiler Heap Free to
 * @param[in] hierarchy_mask    The hierarchy mask to use
 * @param[in] default_weight    Default hierarchy weight to write when no other
 *                              weight is given in the FBD
 * @param[in] hw_job_id_offset  Offset for HW job IDs
 * @param[in] fragment_chain    MAIL_TRUE if this chain is the fragment chain
 *
 * @return MALI_ERROR_NONE on success, error code otherwise
 */
static mali_error kbasep_replay_parse_jc(kbase_context *kctx,
						mali_addr64 jc,
						mali_addr64 prev_jc,
						mali_addr64 tiler_heap_free,
						u16 hierarchy_mask,
						u32 default_weight,
						u16 hw_job_id_offset,
						mali_bool fragment_chain)
{
	mali_bool first_in_chain = MALI_TRUE;
	int nr_jobs = 0;

	KBASE_LOG(2, kctx->kbdev->dev,
			      "kbasep_replay_parse_jc: jc=%llx hw_job_id=%x\n",
							 jc, hw_job_id_offset);

	while (jc) {
		KBASE_LOG(2, kctx->kbdev->dev,
				   "kbasep_replay_parse_jc: parsing jc=%llx\n",
									   jc);

		if (kbasep_replay_reset_job(kctx, &jc, prev_jc,
				tiler_heap_free, hierarchy_mask,
				default_weight, hw_job_id_offset,
				first_in_chain, fragment_chain) != 
							     MALI_ERROR_NONE)
			return MALI_ERROR_FUNCTION_FAILED;

		first_in_chain = MALI_FALSE;

		nr_jobs++;
		if (fragment_chain &&
                		nr_jobs >= BASE_JD_REPLAY_F_CHAIN_JOB_LIMIT) {
			dev_err(kctx->kbdev->dev,
				"Exceeded maximum number of jobs in fragment chain\n");
			return MALI_ERROR_FUNCTION_FAILED;
		}
	}

	return MALI_ERROR_NONE;
}

/**
 * @brief Reset the status of a replay job, and set up dependencies
 *
 * This performs the actions to allow the replay job to be re-run following
 * completion of the passed dependency.
 *
 * @param[in] katom     The atom to be reset
 * @param[in] dep_atom  The dependency to be attached to the atom
 */
static void kbasep_replay_reset_softjob(kbase_jd_atom *katom,
						       kbase_jd_atom *dep_atom)
{
	katom->status = KBASE_JD_ATOM_STATE_QUEUED;
	katom->dep_atom[0] = dep_atom;
	list_add_tail(&katom->dep_item[0], &dep_atom->dep_head[0]);
}

/**
 * @brief Allocate an unused katom
 *
 * This will search the provided context for an unused katom, and will mark it
 * as KBASE_JD_ATOM_STATE_QUEUED.
 *
 * If no atoms are available then the function will fail.
 *
 * @param[in] kctx      Context pointer
 * @return An atom ID, or -1 on failure
 */
static int kbasep_allocate_katom(kbase_context *kctx)
{
	kbase_jd_context *jctx = &kctx->jctx;
	int i;

	for (i = BASE_JD_ATOM_COUNT-1; i > 0; i--) {
		if (jctx->atoms[i].status == KBASE_JD_ATOM_STATE_UNUSED) {
			jctx->atoms[i].status = KBASE_JD_ATOM_STATE_QUEUED;
			KBASE_LOG(2, kctx->kbdev->dev,
				  "kbasep_allocate_katom: Allocated atom %d\n",
									    i);
			return i;
		}
	}

	return -1;
}

/**
 * @brief Release a katom
 *
 * This will mark the provided atom as available, and remove any dependencies.
 *
 * For use on error path.
 *
 * @param[in] kctx      Context pointer
 * @param[in] atom_id   ID of atom to release
 */
static void kbasep_release_katom(kbase_context *kctx, int atom_id)
{
	kbase_jd_context *jctx = &kctx->jctx;

	KBASE_LOG(2, kctx->kbdev->dev,
				    "kbasep_release_katom: Released atom %d\n",
								      atom_id);

	while (!list_empty(&jctx->atoms[atom_id].dep_head[0]))
		list_del(jctx->atoms[atom_id].dep_head[0].next);
	while (!list_empty(&jctx->atoms[atom_id].dep_head[1]))
		list_del(jctx->atoms[atom_id].dep_head[1].next);

	jctx->atoms[atom_id].status = KBASE_JD_ATOM_STATE_UNUSED;
}

static void kbasep_replay_create_atom(kbase_context *kctx,
				      base_jd_atom_v2 *atom,
				      int atom_nr,
				      int prio)
{
	atom->nr_extres = 0;
	atom->extres_list.value = NULL;
	atom->device_nr = 0;
	/* Convert priority back from NICE range */
	atom->prio = ((prio << 16) / ((20 << 16) / 128)) - 128;
	atom->atom_number = atom_nr;

	atom->pre_dep[0] = 0;
	atom->pre_dep[1] = 0;

	atom->udata.blob[0] = 0;
	atom->udata.blob[1] = 0;
}

/**
 * @brief Create two atoms for the purpose of replaying jobs
 *
 * Two atoms are allocated and created. The jc pointer is not set at this
 * stage. The second atom has a dependency on the first. The remaining fields
 * are set up as follows :
 *
 * - No external resources. Any required external resources will be held by the
 *   replay atom.
 * - device_nr is set to 0. This is not relevant as
 *   BASE_JD_REQ_SPECIFIC_COHERENT_GROUP should not be set.
 * - Priority is inherited from the replay job.
 *
 * @param[out] t_atom      Atom to use for tiler jobs
 * @param[out] f_atom      Atom to use for fragment jobs
 * @param[in]  prio        Priority of new atom (inherited from replay soft
 *                         job)
 * @return MALI_ERROR_NONE on success, error code on failure
 */
static mali_error kbasep_replay_create_atoms(kbase_context *kctx,
					     base_jd_atom_v2 *t_atom,
					     base_jd_atom_v2 *f_atom,
					     int prio)
{
	int t_atom_nr, f_atom_nr;

	t_atom_nr = kbasep_allocate_katom(kctx);
	if (t_atom_nr < 0) {
		dev_err(kctx->kbdev->dev, "Failed to allocate katom\n");
		return MALI_ERROR_FUNCTION_FAILED;
	}

	f_atom_nr = kbasep_allocate_katom(kctx);
	if (f_atom_nr < 0) {
		dev_err(kctx->kbdev->dev, "Failed to allocate katom\n");
		kbasep_release_katom(kctx, t_atom_nr);
		return MALI_ERROR_FUNCTION_FAILED;
	}

	kbasep_replay_create_atom(kctx, t_atom, t_atom_nr, prio);
	kbasep_replay_create_atom(kctx, f_atom, f_atom_nr, prio);

	f_atom->pre_dep[0] = t_atom_nr;

	return MALI_ERROR_NONE;
}

#ifdef CONFIG_MALI_DEBUG
static void payload_dump(kbase_context *kctx, base_jd_replay_payload *payload)
{
	mali_addr64 next;

	KBASE_LOG(2, kctx->kbdev->dev, "Tiler jc list :\n");
	next = payload->tiler_jc_list;

	while (next) {
		base_jd_replay_jc *jc_struct = kbasep_map_page(kctx, next, NULL);

		if (!jc_struct)
			return;

		KBASE_LOG(2, kctx->kbdev->dev,
					  "* jc_struct=%p jc=%llx next=%llx\n",
								     jc_struct,
								 jc_struct->jc,
							      jc_struct->next);
		next = jc_struct->next;

		kbasep_unmap_page(jc_struct);
	}
}
#endif

/**
 * @brief Parse a base_jd_replay_payload provided by userspace
 *
 * This will read the payload from userspace, and parse the job chains.
 *
 * @param[in] kctx         Context pointer
 * @param[in] replay_atom  Replay soft job atom
 * @param[in] t_atom       Atom to use for tiler jobs
 * @param[in] f_atom       Atom to use for fragment jobs
 * @return  MALI_ERROR_NONE on success, error code on failure
 */
static mali_error kbasep_replay_parse_payload(kbase_context *kctx, 
					      kbase_jd_atom *replay_atom,
					      base_jd_atom_v2 *t_atom,
					      base_jd_atom_v2 *f_atom)
{
	base_jd_replay_payload *payload;
	mali_addr64 next;
	mali_addr64 prev_jc = 0;
	u16 hw_job_id_offset = 0;
	mali_error ret = MALI_ERROR_FUNCTION_FAILED;
	u64 phys_addr;
	struct device *dev = kctx->kbdev->dev;

	KBASE_LOG(2, dev,
			"kbasep_replay_parse_payload: replay_atom->jc = %llx  "
			"sizeof(payload) = %d\n",
					     replay_atom->jc, sizeof(payload));

	kbase_gpu_vm_lock(kctx);

	payload = kbasep_map_page_sync(kctx, replay_atom->jc, &phys_addr);

	if (!payload) {
		kbase_gpu_vm_unlock(kctx);
		dev_err(dev, "kbasep_replay_parse_payload: failed to map payload into kernel space\n");
		return MALI_ERROR_FUNCTION_FAILED;
	}

#ifdef CONFIG_MALI_DEBUG
	KBASE_LOG(2, dev, "kbasep_replay_parse_payload: payload=%p\n", payload);
	KBASE_LOG(2, dev, "Payload structure:\n"
					"tiler_jc_list            = %llx\n"
					"fragment_jc              = %llx\n"
					"tiler_heap_free          = %llx\n"
					"fragment_hierarchy_mask  = %x\n"
					"tiler_hierarchy_mask     = %x\n"
					"hierarchy_default_weight = %x\n"
					"tiler_core_req           = %x\n"
					"fragment_core_req        = %x\n",
							payload->tiler_jc_list,
							  payload->fragment_jc,
						      payload->tiler_heap_free,
					      payload->fragment_hierarchy_mask,
						 payload->tiler_hierarchy_mask,
					     payload->hierarchy_default_weight,
						       payload->tiler_core_req,
						   payload->fragment_core_req);
	payload_dump(kctx, payload);
#endif

	t_atom->core_req = payload->tiler_core_req | BASEP_JD_REQ_EVENT_NEVER;
	f_atom->core_req = payload->fragment_core_req | BASEP_JD_REQ_EVENT_NEVER;

	/* Sanity check core requirements*/
	if ((t_atom->core_req & BASEP_JD_REQ_ATOM_TYPE &
			       ~BASE_JD_REQ_COHERENT_GROUP) != BASE_JD_REQ_T ||
	    (f_atom->core_req & BASEP_JD_REQ_ATOM_TYPE &
			      ~BASE_JD_REQ_COHERENT_GROUP) != BASE_JD_REQ_FS ||
	     t_atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES ||
	     f_atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) {
		dev_err(dev, "Invalid core requirements\n");
		goto out;
	}
	
	/* Process tiler job chains */
	next = payload->tiler_jc_list;
	if (!next) {
		dev_err(dev, "Invalid tiler JC list\n");
		goto out;
	}

	while (next) {
		base_jd_replay_jc *jc_struct = kbasep_map_page(kctx, next, NULL);
		mali_addr64 jc;

		if (!jc_struct) {
			dev_err(dev, "Failed to map jc struct\n");
			goto out;
		}

		jc = jc_struct->jc;
		next = jc_struct->next;
		if (next)
			jc_struct->jc = 0;

		kbasep_unmap_page(jc_struct);

		if (jc) {
			u16 max_hw_job_id = 0;

			if (kbasep_replay_find_hw_job_id(kctx, jc,
					    &max_hw_job_id) != MALI_ERROR_NONE)
				goto out;

			if (kbasep_replay_parse_jc(kctx, jc, prev_jc,
					     payload->tiler_heap_free,
					     payload->tiler_hierarchy_mask,
					     payload->hierarchy_default_weight,
				             hw_job_id_offset, MALI_FALSE) !=
							     MALI_ERROR_NONE) {
				goto out;
			}

			hw_job_id_offset += max_hw_job_id;

			prev_jc = jc;
		}
	}
	t_atom->jc = prev_jc;

	/* Process fragment job chain */
	f_atom->jc = payload->fragment_jc;
	if (kbasep_replay_parse_jc(kctx, payload->fragment_jc, 0,
					 payload->tiler_heap_free,
					 payload->fragment_hierarchy_mask,
					 payload->hierarchy_default_weight, 0,
					       MALI_TRUE) != MALI_ERROR_NONE) {
		goto out;
	}

	if (!t_atom->jc || !f_atom->jc) {
		dev_err(dev, "Invalid payload\n");
		goto out;
	}

	KBASE_LOG(2, dev, "t_atom->jc=%llx f_atom->jc=%llx\n",
						       t_atom->jc, f_atom->jc);
	ret = MALI_ERROR_NONE;

out:	
	kbasep_unmap_page_sync(payload, phys_addr);

	kbase_gpu_vm_unlock(kctx);

	return ret;
}

/**
 * @brief Process a replay job
 *
 * Called from kbase_process_soft_job.
 *
 * On exit, if the job has completed, katom->event_code will have been updated.
 * If the job has not completed, and is replaying jobs, then the atom status
 * will have been reset to KBASE_JD_ATOM_STATE_QUEUED.
 *
 * @param[in] katom  The atom to be processed
 * @return           MALI_REPLAY_STATUS_COMPLETE  if the atom has completed
 *                   MALI_REPLAY_STATUS_REPLAYING if the atom is replaying jobs
 *                   Set MALI_REPLAY_FLAG_JS_RESCHED if 
 *                   kbasep_js_try_schedule_head_ctx required
 */
int kbase_replay_process(kbase_jd_atom *katom)
{
	kbase_context *kctx = katom->kctx;
	kbase_jd_context *jctx = &kctx->jctx;
	mali_bool need_to_try_schedule_context = MALI_FALSE;
	base_jd_atom_v2 t_atom, f_atom;
	kbase_jd_atom *t_katom, *f_katom;
	struct device *dev = kctx->kbdev->dev;

	if (katom->event_code == BASE_JD_EVENT_DONE) {
		KBASE_LOG(2, dev, "Previous job succeeded - not replaying\n");
		return MALI_REPLAY_STATUS_COMPLETE;
	}

	if (jctx->sched_info.ctx.is_dying) {
		KBASE_LOG(2, dev, "Not replaying; context is dying\n");
		return MALI_REPLAY_STATUS_COMPLETE;
	}

	dev_warn(dev, "Replaying jobs retry=%d\n", katom->retry_count);

	katom->retry_count++;
	if (katom->retry_count > BASEP_JD_REPLAY_LIMIT) {
		dev_err(dev, "Replay exceeded limit - failing jobs\n");
		/* katom->event_code is already set to the failure code of the
		   previous job */
		return MALI_REPLAY_STATUS_COMPLETE;
	}

	if (kbasep_replay_create_atoms(kctx, &t_atom, &f_atom,
				       katom->nice_prio) != MALI_ERROR_NONE) {
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		return MALI_REPLAY_STATUS_COMPLETE;
	}

	t_katom = &jctx->atoms[t_atom.atom_number];
	f_katom = &jctx->atoms[f_atom.atom_number];

	if (kbasep_replay_parse_payload(kctx, katom, &t_atom, &f_atom) !=
							     MALI_ERROR_NONE) {
		kbasep_release_katom(kctx, t_atom.atom_number);
		kbasep_release_katom(kctx, f_atom.atom_number);
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		return MALI_REPLAY_STATUS_COMPLETE;
	}

	kbasep_replay_reset_softjob(katom, f_katom);

	need_to_try_schedule_context |= jd_submit_atom(kctx, &t_atom, t_katom);
	if (t_katom->event_code == BASE_JD_EVENT_JOB_INVALID) {
		dev_err(dev, "Replay failed to submit atom\n");
		kbasep_release_katom(kctx, f_atom.atom_number);
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		return MALI_REPLAY_STATUS_COMPLETE;
	}
	need_to_try_schedule_context |= jd_submit_atom(kctx, &f_atom, f_katom);
	if (f_katom->event_code == BASE_JD_EVENT_JOB_INVALID) {
		dev_err(dev, "Replay failed to submit atom\n");
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		return MALI_REPLAY_STATUS_COMPLETE;
	}

	katom->event_code = BASE_JD_EVENT_DONE;

	if (need_to_try_schedule_context)
		return MALI_REPLAY_STATUS_REPLAYING | 
						MALI_REPLAY_FLAG_JS_RESCHED;
	return MALI_REPLAY_STATUS_REPLAYING;
}

