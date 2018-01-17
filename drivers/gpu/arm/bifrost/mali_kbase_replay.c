/*
 *
 * (C) COPYRIGHT 2014-2017 ARM Limited. All rights reserved.
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

#include <linux/dma-mapping.h>
#include <mali_kbase_config.h>
#include <mali_kbase.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_mem_linux.h>

#define JOB_NOT_STARTED 0
#define JOB_TYPE_NULL      (1)
#define JOB_TYPE_VERTEX    (5)
#define JOB_TYPE_TILER     (7)
#define JOB_TYPE_FUSED     (8)
#define JOB_TYPE_FRAGMENT  (9)

#define JOB_HEADER_32_FBD_OFFSET (31*4)
#define JOB_HEADER_64_FBD_OFFSET (44*4)

#define FBD_POINTER_MASK (~0x3f)

#define SFBD_TILER_OFFSET (48*4)

#define MFBD_TILER_OFFSET       (14*4)

#define FBD_HIERARCHY_WEIGHTS 8
#define FBD_HIERARCHY_MASK_MASK 0x1fff

#define FBD_TYPE 1

#define HIERARCHY_WEIGHTS 13

#define JOB_HEADER_ID_MAX                 0xffff

#define JOB_SOURCE_ID(status)		(((status) >> 16) & 0xFFFF)
#define JOB_POLYGON_LIST		(0x03)

struct fragment_job {
	struct job_descriptor_header header;

	u32 x[2];
	union {
		u64 _64;
		u32 _32;
	} fragment_fbd;
};

static void dump_job_head(struct kbase_context *kctx, char *head_str,
		struct job_descriptor_header *job)
{
#ifdef CONFIG_MALI_DEBUG
	dev_dbg(kctx->kbdev->dev, "%s\n", head_str);
	dev_dbg(kctx->kbdev->dev,
			"addr                  = %p\n"
			"exception_status      = %x (Source ID: 0x%x Access: 0x%x Exception: 0x%x)\n"
			"first_incomplete_task = %x\n"
			"fault_pointer         = %llx\n"
			"job_descriptor_size   = %x\n"
			"job_type              = %x\n"
			"job_barrier           = %x\n"
			"_reserved_01          = %x\n"
			"_reserved_02          = %x\n"
			"_reserved_03          = %x\n"
			"_reserved_04/05       = %x,%x\n"
			"job_index             = %x\n"
			"dependencies          = %x,%x\n",
			job, job->exception_status,
			JOB_SOURCE_ID(job->exception_status),
			(job->exception_status >> 8) & 0x3,
			job->exception_status  & 0xFF,
			job->first_incomplete_task,
			job->fault_pointer, job->job_descriptor_size,
			job->job_type, job->job_barrier, job->_reserved_01,
			job->_reserved_02, job->_reserved_03,
			job->_reserved_04, job->_reserved_05,
			job->job_index,
			job->job_dependency_index_1,
			job->job_dependency_index_2);

	if (job->job_descriptor_size)
		dev_dbg(kctx->kbdev->dev, "next               = %llx\n",
				job->next_job._64);
	else
		dev_dbg(kctx->kbdev->dev, "next               = %x\n",
				job->next_job._32);
#endif
}

static int kbasep_replay_reset_sfbd(struct kbase_context *kctx,
		u64 fbd_address, u64 tiler_heap_free,
		u16 hierarchy_mask, u32 default_weight)
{
	struct {
		u32 padding_1[1];
		u32 flags;
		u64 padding_2[2];
		u64 heap_free_address;
		u32 padding[8];
		u32 weights[FBD_HIERARCHY_WEIGHTS];
	} *fbd_tiler;
	struct kbase_vmap_struct map;

	dev_dbg(kctx->kbdev->dev, "fbd_address: %llx\n", fbd_address);

	fbd_tiler = kbase_vmap(kctx, fbd_address + SFBD_TILER_OFFSET,
			sizeof(*fbd_tiler), &map);
	if (!fbd_tiler) {
		dev_err(kctx->kbdev->dev, "kbasep_replay_reset_fbd: failed to map fbd\n");
		return -EINVAL;
	}

#ifdef CONFIG_MALI_DEBUG
	dev_dbg(kctx->kbdev->dev,
		"FBD tiler:\n"
		"flags = %x\n"
		"heap_free_address = %llx\n",
		fbd_tiler->flags, fbd_tiler->heap_free_address);
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


		dev_dbg(kctx->kbdev->dev, "Old hierarchy mask=%x  New hierarchy mask=%x\n",
				old_hierarchy_mask, hierarchy_mask);

		for (i = 0; i < HIERARCHY_WEIGHTS; i++)
			dev_dbg(kctx->kbdev->dev, " Hierarchy weight %02d: %08x\n",
					i, weights[i]);

		j = 0;

		for (i = 0; i < HIERARCHY_WEIGHTS; i++) {
			if (hierarchy_mask & (1 << i)) {
				KBASE_DEBUG_ASSERT(j < FBD_HIERARCHY_WEIGHTS);

				dev_dbg(kctx->kbdev->dev, " Writing hierarchy level %02d (%08x) to %d\n",
						i, weights[i], j);

				fbd_tiler->weights[j++] = weights[i];
			}
		}

		for (; j < FBD_HIERARCHY_WEIGHTS; j++)
			fbd_tiler->weights[j] = 0;

		fbd_tiler->flags = hierarchy_mask | (1 << 16);
	}

	fbd_tiler->heap_free_address = tiler_heap_free;

	dev_dbg(kctx->kbdev->dev, "heap_free_address=%llx flags=%x\n",
			fbd_tiler->heap_free_address, fbd_tiler->flags);

	kbase_vunmap(kctx, &map);

	return 0;
}

static int kbasep_replay_reset_mfbd(struct kbase_context *kctx,
		u64 fbd_address, u64 tiler_heap_free,
		u16 hierarchy_mask, u32 default_weight)
{
	struct kbase_vmap_struct map;
	struct {
		u32 padding_0;
		u32 flags;
		u64 padding_1[2];
		u64 heap_free_address;
		u64 padding_2;
		u32 weights[FBD_HIERARCHY_WEIGHTS];
	} *fbd_tiler;

	dev_dbg(kctx->kbdev->dev, "fbd_address: %llx\n", fbd_address);

	fbd_tiler = kbase_vmap(kctx, fbd_address + MFBD_TILER_OFFSET,
			sizeof(*fbd_tiler), &map);
	if (!fbd_tiler) {
		dev_err(kctx->kbdev->dev,
			       "kbasep_replay_reset_fbd: failed to map fbd\n");
		return -EINVAL;
	}

#ifdef CONFIG_MALI_DEBUG
	dev_dbg(kctx->kbdev->dev, "FBD tiler:\n"
			"flags = %x\n"
			"heap_free_address = %llx\n",
			fbd_tiler->flags,
			fbd_tiler->heap_free_address);
#endif
	if (hierarchy_mask) {
		u32 weights[HIERARCHY_WEIGHTS];
		u16 old_hierarchy_mask = (fbd_tiler->flags) &
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


		dev_dbg(kctx->kbdev->dev, "Old hierarchy mask=%x  New hierarchy mask=%x\n",
				old_hierarchy_mask, hierarchy_mask);

		for (i = 0; i < HIERARCHY_WEIGHTS; i++)
			dev_dbg(kctx->kbdev->dev, " Hierarchy weight %02d: %08x\n",
					i, weights[i]);

		j = 0;

		for (i = 0; i < HIERARCHY_WEIGHTS; i++) {
			if (hierarchy_mask & (1 << i)) {
				KBASE_DEBUG_ASSERT(j < FBD_HIERARCHY_WEIGHTS);

				dev_dbg(kctx->kbdev->dev,
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

	kbase_vunmap(kctx, &map);

	return 0;
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
 * @param[in] job_64            true if this job is using 64-bit
 *                              descriptors
 *
 * @return 0 on success, error code on failure
 */
static int kbasep_replay_reset_tiler_job(struct kbase_context *kctx,
		u64 job_header,	u64 tiler_heap_free,
		u16 hierarchy_mask, u32 default_weight,	bool job_64)
{
	struct kbase_vmap_struct map;
	u64 fbd_address;

	if (job_64) {
		u64 *job_ext;

		job_ext = kbase_vmap(kctx,
				job_header + JOB_HEADER_64_FBD_OFFSET,
				sizeof(*job_ext), &map);

		if (!job_ext) {
			dev_err(kctx->kbdev->dev, "kbasep_replay_reset_tiler_job: failed to map jc\n");
			return -EINVAL;
		}

		fbd_address = *job_ext;

		kbase_vunmap(kctx, &map);
	} else {
		u32 *job_ext;

		job_ext = kbase_vmap(kctx,
				job_header + JOB_HEADER_32_FBD_OFFSET,
				sizeof(*job_ext), &map);

		if (!job_ext) {
			dev_err(kctx->kbdev->dev, "kbasep_replay_reset_tiler_job: failed to map jc\n");
			return -EINVAL;
		}

		fbd_address = *job_ext;

		kbase_vunmap(kctx, &map);
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
 * @param[in] first_in_chain    true if this job is the first in the chain
 * @param[in] fragment_chain    true if this job is in the fragment chain
 *
 * @return 0 on success, error code on failure
 */
static int kbasep_replay_reset_job(struct kbase_context *kctx,
		u64 *job_header, u64 prev_jc,
		u64 tiler_heap_free, u16 hierarchy_mask,
		u32 default_weight, u16 hw_job_id_offset,
		bool first_in_chain, bool fragment_chain)
{
	struct fragment_job *frag_job;
	struct job_descriptor_header *job;
	u64 new_job_header;
	struct kbase_vmap_struct map;

	frag_job = kbase_vmap(kctx, *job_header, sizeof(*frag_job), &map);
	if (!frag_job) {
		dev_err(kctx->kbdev->dev,
				 "kbasep_replay_parse_jc: failed to map jc\n");
		return -EINVAL;
	}
	job = &frag_job->header;

	dump_job_head(kctx, "Job header:", job);

	if (job->exception_status == JOB_NOT_STARTED && !fragment_chain) {
		dev_err(kctx->kbdev->dev, "Job already not started\n");
		goto out_unmap;
	}
	job->exception_status = JOB_NOT_STARTED;

	if (job->job_type == JOB_TYPE_VERTEX)
		job->job_type = JOB_TYPE_NULL;

	if (job->job_type == JOB_TYPE_FUSED) {
		dev_err(kctx->kbdev->dev, "Fused jobs can not be replayed\n");
		goto out_unmap;
	}

	if (first_in_chain)
		job->job_barrier = 1;

	if ((job->job_dependency_index_1 + hw_job_id_offset) >
			JOB_HEADER_ID_MAX ||
	    (job->job_dependency_index_2 + hw_job_id_offset) >
			JOB_HEADER_ID_MAX ||
	    (job->job_index + hw_job_id_offset) > JOB_HEADER_ID_MAX) {
		dev_err(kctx->kbdev->dev,
			     "Job indicies/dependencies out of valid range\n");
		goto out_unmap;
	}

	if (job->job_dependency_index_1)
		job->job_dependency_index_1 += hw_job_id_offset;
	if (job->job_dependency_index_2)
		job->job_dependency_index_2 += hw_job_id_offset;

	job->job_index += hw_job_id_offset;

	if (job->job_descriptor_size) {
		new_job_header = job->next_job._64;
		if (!job->next_job._64)
			job->next_job._64 = prev_jc;
	} else {
		new_job_header = job->next_job._32;
		if (!job->next_job._32)
			job->next_job._32 = prev_jc;
	}
	dump_job_head(kctx, "Updated to:", job);

	if (job->job_type == JOB_TYPE_TILER) {
		bool job_64 = job->job_descriptor_size != 0;

		if (kbasep_replay_reset_tiler_job(kctx, *job_header,
				tiler_heap_free, hierarchy_mask,
				default_weight, job_64) != 0)
			goto out_unmap;

	} else if (job->job_type == JOB_TYPE_FRAGMENT) {
		u64 fbd_address;

		if (job->job_descriptor_size)
			fbd_address = frag_job->fragment_fbd._64;
		else
			fbd_address = (u64)frag_job->fragment_fbd._32;

		if (fbd_address & FBD_TYPE) {
			if (kbasep_replay_reset_mfbd(kctx,
					fbd_address & FBD_POINTER_MASK,
					tiler_heap_free,
					hierarchy_mask,
					default_weight) != 0)
				goto out_unmap;
		} else {
			if (kbasep_replay_reset_sfbd(kctx,
					fbd_address & FBD_POINTER_MASK,
					tiler_heap_free,
					hierarchy_mask,
					default_weight) != 0)
				goto out_unmap;
		}
	}

	kbase_vunmap(kctx, &map);

	*job_header = new_job_header;

	return 0;

out_unmap:
	kbase_vunmap(kctx, &map);
	return -EINVAL;
}

/**
 * @brief Find the highest job ID in a job chain
 *
 * @param[in] kctx        Context pointer
 * @param[in] jc          Job chain start address
 * @param[out] hw_job_id  Highest job ID in chain
 *
 * @return 0 on success, error code on failure
 */
static int kbasep_replay_find_hw_job_id(struct kbase_context *kctx,
		u64 jc,	u16 *hw_job_id)
{
	while (jc) {
		struct job_descriptor_header *job;
		struct kbase_vmap_struct map;

		dev_dbg(kctx->kbdev->dev,
			"kbasep_replay_find_hw_job_id: parsing jc=%llx\n", jc);

		job = kbase_vmap(kctx, jc, sizeof(*job), &map);
		if (!job) {
			dev_err(kctx->kbdev->dev, "failed to map jc\n");

			return -EINVAL;
		}

		if (job->job_index > *hw_job_id)
			*hw_job_id = job->job_index;

		if (job->job_descriptor_size)
			jc = job->next_job._64;
		else
			jc = job->next_job._32;

		kbase_vunmap(kctx, &map);
	}

	return 0;
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
 * @param[in] fragment_chain    true if this chain is the fragment chain
 *
 * @return 0 on success, error code otherwise
 */
static int kbasep_replay_parse_jc(struct kbase_context *kctx,
		u64 jc,	u64 prev_jc,
		u64 tiler_heap_free, u16 hierarchy_mask,
		u32 default_weight, u16 hw_job_id_offset,
		bool fragment_chain)
{
	bool first_in_chain = true;
	int nr_jobs = 0;

	dev_dbg(kctx->kbdev->dev, "kbasep_replay_parse_jc: jc=%llx hw_job_id=%x\n",
			jc, hw_job_id_offset);

	while (jc) {
		dev_dbg(kctx->kbdev->dev, "kbasep_replay_parse_jc: parsing jc=%llx\n", jc);

		if (kbasep_replay_reset_job(kctx, &jc, prev_jc,
				tiler_heap_free, hierarchy_mask,
				default_weight, hw_job_id_offset,
				first_in_chain, fragment_chain) != 0)
			return -EINVAL;

		first_in_chain = false;

		nr_jobs++;
		if (fragment_chain &&
		    nr_jobs >= BASE_JD_REPLAY_F_CHAIN_JOB_LIMIT) {
			dev_err(kctx->kbdev->dev,
				"Exceeded maximum number of jobs in fragment chain\n");
			return -EINVAL;
		}
	}

	return 0;
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
static void kbasep_replay_reset_softjob(struct kbase_jd_atom *katom,
		struct kbase_jd_atom *dep_atom)
{
	katom->status = KBASE_JD_ATOM_STATE_QUEUED;
	kbase_jd_katom_dep_set(&katom->dep[0], dep_atom, BASE_JD_DEP_TYPE_DATA);
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
static int kbasep_allocate_katom(struct kbase_context *kctx)
{
	struct kbase_jd_context *jctx = &kctx->jctx;
	int i;

	for (i = BASE_JD_ATOM_COUNT-1; i > 0; i--) {
		if (jctx->atoms[i].status == KBASE_JD_ATOM_STATE_UNUSED) {
			jctx->atoms[i].status = KBASE_JD_ATOM_STATE_QUEUED;
			dev_dbg(kctx->kbdev->dev,
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
static void kbasep_release_katom(struct kbase_context *kctx, int atom_id)
{
	struct kbase_jd_context *jctx = &kctx->jctx;

	dev_dbg(kctx->kbdev->dev, "kbasep_release_katom: Released atom %d\n",
			atom_id);

	while (!list_empty(&jctx->atoms[atom_id].dep_head[0]))
		list_del(jctx->atoms[atom_id].dep_head[0].next);

	while (!list_empty(&jctx->atoms[atom_id].dep_head[1]))
		list_del(jctx->atoms[atom_id].dep_head[1].next);

	jctx->atoms[atom_id].status = KBASE_JD_ATOM_STATE_UNUSED;
}

static void kbasep_replay_create_atom(struct kbase_context *kctx,
				      struct base_jd_atom_v2 *atom,
				      int atom_nr,
				      base_jd_prio prio)
{
	atom->nr_extres = 0;
	atom->extres_list = 0;
	atom->device_nr = 0;
	atom->prio = prio;
	atom->atom_number = atom_nr;

	base_jd_atom_dep_set(&atom->pre_dep[0], 0 , BASE_JD_DEP_TYPE_INVALID);
	base_jd_atom_dep_set(&atom->pre_dep[1], 0 , BASE_JD_DEP_TYPE_INVALID);

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
 * @return 0 on success, error code on failure
 */
static int kbasep_replay_create_atoms(struct kbase_context *kctx,
		struct base_jd_atom_v2 *t_atom,
		struct base_jd_atom_v2 *f_atom,
		base_jd_prio prio)
{
	int t_atom_nr, f_atom_nr;

	t_atom_nr = kbasep_allocate_katom(kctx);
	if (t_atom_nr < 0) {
		dev_err(kctx->kbdev->dev, "Failed to allocate katom\n");
		return -EINVAL;
	}

	f_atom_nr = kbasep_allocate_katom(kctx);
	if (f_atom_nr < 0) {
		dev_err(kctx->kbdev->dev, "Failed to allocate katom\n");
		kbasep_release_katom(kctx, t_atom_nr);
		return -EINVAL;
	}

	kbasep_replay_create_atom(kctx, t_atom, t_atom_nr, prio);
	kbasep_replay_create_atom(kctx, f_atom, f_atom_nr, prio);

	base_jd_atom_dep_set(&f_atom->pre_dep[0], t_atom_nr , BASE_JD_DEP_TYPE_DATA);

	return 0;
}

#ifdef CONFIG_MALI_DEBUG
static void payload_dump(struct kbase_context *kctx, base_jd_replay_payload *payload)
{
	u64 next;

	dev_dbg(kctx->kbdev->dev, "Tiler jc list :\n");
	next = payload->tiler_jc_list;

	while (next) {
		struct kbase_vmap_struct map;
		base_jd_replay_jc *jc_struct;

		jc_struct = kbase_vmap(kctx, next, sizeof(*jc_struct), &map);

		if (!jc_struct)
			return;

		dev_dbg(kctx->kbdev->dev, "* jc_struct=%p jc=%llx next=%llx\n",
				jc_struct, jc_struct->jc, jc_struct->next);

		next = jc_struct->next;

		kbase_vunmap(kctx, &map);
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
 * @return 0 on success, error code on failure
 */
static int kbasep_replay_parse_payload(struct kbase_context *kctx,
					      struct kbase_jd_atom *replay_atom,
					      struct base_jd_atom_v2 *t_atom,
					      struct base_jd_atom_v2 *f_atom)
{
	base_jd_replay_payload *payload = NULL;
	u64 next;
	u64 prev_jc = 0;
	u16 hw_job_id_offset = 0;
	int ret = -EINVAL;
	struct kbase_vmap_struct map;

	dev_dbg(kctx->kbdev->dev, "kbasep_replay_parse_payload: replay_atom->jc = %llx sizeof(payload) = %zu\n",
			replay_atom->jc, sizeof(payload));

	payload = kbase_vmap(kctx, replay_atom->jc, sizeof(*payload), &map);
	if (!payload) {
		dev_err(kctx->kbdev->dev, "kbasep_replay_parse_payload: failed to map payload into kernel space\n");
		return -EINVAL;
	}

#ifdef BASE_LEGACY_UK10_2_SUPPORT
	if (KBASE_API_VERSION(10, 3) > replay_atom->kctx->api_version) {
		base_jd_replay_payload_uk10_2 *payload_uk10_2;
		u16 tiler_core_req;
		u16 fragment_core_req;

		payload_uk10_2 = (base_jd_replay_payload_uk10_2 *) payload;
		memcpy(&tiler_core_req, &payload_uk10_2->tiler_core_req,
				sizeof(tiler_core_req));
		memcpy(&fragment_core_req, &payload_uk10_2->fragment_core_req,
				sizeof(fragment_core_req));
		payload->tiler_core_req = (u32)(tiler_core_req & 0x7fff);
		payload->fragment_core_req = (u32)(fragment_core_req & 0x7fff);
	}
#endif /* BASE_LEGACY_UK10_2_SUPPORT */

#ifdef CONFIG_MALI_DEBUG
	dev_dbg(kctx->kbdev->dev, "kbasep_replay_parse_payload: payload=%p\n", payload);
	dev_dbg(kctx->kbdev->dev, "Payload structure:\n"
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
	if ((t_atom->core_req & BASE_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_T ||
	    (f_atom->core_req & BASE_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_FS ||
	     t_atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES ||
	     f_atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) {

		int t_atom_type = t_atom->core_req & BASE_JD_REQ_ATOM_TYPE & ~BASE_JD_REQ_COHERENT_GROUP;
		int f_atom_type = f_atom->core_req & BASE_JD_REQ_ATOM_TYPE & ~BASE_JD_REQ_COHERENT_GROUP & ~BASE_JD_REQ_FS_AFBC;
		int t_has_ex_res = t_atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES;
		int f_has_ex_res = f_atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES;

		if (t_atom_type != BASE_JD_REQ_T) {
			dev_err(kctx->kbdev->dev, "Invalid core requirement: Tiler atom not a tiler job. Was: 0x%x\n Expected: 0x%x",
			    t_atom_type, BASE_JD_REQ_T);
		}
		if (f_atom_type != BASE_JD_REQ_FS) {
			dev_err(kctx->kbdev->dev, "Invalid core requirement: Fragment shader atom not a fragment shader. Was 0x%x Expected: 0x%x\n",
			    f_atom_type, BASE_JD_REQ_FS);
		}
		if (t_has_ex_res) {
			dev_err(kctx->kbdev->dev, "Invalid core requirement: Tiler atom has external resources.\n");
		}
		if (f_has_ex_res) {
			dev_err(kctx->kbdev->dev, "Invalid core requirement: Fragment shader atom has external resources.\n");
		}

		goto out;
	}

	/* Process tiler job chains */
	next = payload->tiler_jc_list;
	if (!next) {
		dev_err(kctx->kbdev->dev, "Invalid tiler JC list\n");
		goto out;
	}

	while (next) {
		base_jd_replay_jc *jc_struct;
		struct kbase_vmap_struct jc_map;
		u64 jc;

		jc_struct = kbase_vmap(kctx, next, sizeof(*jc_struct), &jc_map);

		if (!jc_struct) {
			dev_err(kctx->kbdev->dev, "Failed to map jc struct\n");
			goto out;
		}

		jc = jc_struct->jc;
		next = jc_struct->next;
		if (next)
			jc_struct->jc = 0;

		kbase_vunmap(kctx, &jc_map);

		if (jc) {
			u16 max_hw_job_id = 0;

			if (kbasep_replay_find_hw_job_id(kctx, jc,
					&max_hw_job_id) != 0)
				goto out;

			if (kbasep_replay_parse_jc(kctx, jc, prev_jc,
					payload->tiler_heap_free,
					payload->tiler_hierarchy_mask,
					payload->hierarchy_default_weight,
					hw_job_id_offset, false) != 0) {
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
			true) != 0) {
		goto out;
	}

	if (!t_atom->jc || !f_atom->jc) {
		dev_err(kctx->kbdev->dev, "Invalid payload\n");
		goto out;
	}

	dev_dbg(kctx->kbdev->dev, "t_atom->jc=%llx f_atom->jc=%llx\n",
			t_atom->jc, f_atom->jc);
	ret = 0;

out:
	kbase_vunmap(kctx, &map);

	return ret;
}

static void kbase_replay_process_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom;
	struct kbase_context *kctx;
	struct kbase_jd_context *jctx;
	bool need_to_try_schedule_context = false;

	struct base_jd_atom_v2 t_atom, f_atom;
	struct kbase_jd_atom *t_katom, *f_katom;
	base_jd_prio atom_prio;

	katom = container_of(data, struct kbase_jd_atom, work);
	kctx = katom->kctx;
	jctx = &kctx->jctx;

	mutex_lock(&jctx->lock);

	atom_prio = kbasep_js_sched_prio_to_atom_prio(katom->sched_priority);

	if (kbasep_replay_create_atoms(
			kctx, &t_atom, &f_atom, atom_prio) != 0) {
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		goto out;
	}

	t_katom = &jctx->atoms[t_atom.atom_number];
	f_katom = &jctx->atoms[f_atom.atom_number];

	if (kbasep_replay_parse_payload(kctx, katom, &t_atom, &f_atom) != 0) {
		kbasep_release_katom(kctx, t_atom.atom_number);
		kbasep_release_katom(kctx, f_atom.atom_number);
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		goto out;
	}

	kbasep_replay_reset_softjob(katom, f_katom);

	need_to_try_schedule_context |= jd_submit_atom(kctx, &t_atom, t_katom);
	if (t_katom->event_code == BASE_JD_EVENT_JOB_INVALID) {
		dev_err(kctx->kbdev->dev, "Replay failed to submit atom\n");
		kbasep_release_katom(kctx, f_atom.atom_number);
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		goto out;
	}
	need_to_try_schedule_context |= jd_submit_atom(kctx, &f_atom, f_katom);
	if (f_katom->event_code == BASE_JD_EVENT_JOB_INVALID) {
		dev_err(kctx->kbdev->dev, "Replay failed to submit atom\n");
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		goto out;
	}

	katom->event_code = BASE_JD_EVENT_DONE;

out:
	if (katom->event_code != BASE_JD_EVENT_DONE) {
		kbase_disjoint_state_down(kctx->kbdev);

		need_to_try_schedule_context |= jd_done_nolock(katom, NULL);
	}

	if (need_to_try_schedule_context)
		kbase_js_sched_all(kctx->kbdev);

	mutex_unlock(&jctx->lock);
}

/**
 * @brief Check job replay fault
 *
 * This will read the job payload, checks fault type and source, then decides
 * whether replay is required.
 *
 * @param[in] katom       The atom to be processed
 * @return  true (success) if replay required or false on failure.
 */
static bool kbase_replay_fault_check(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct device *dev = kctx->kbdev->dev;
	base_jd_replay_payload *payload;
	u64 job_header;
	u64 job_loop_detect;
	struct job_descriptor_header *job;
	struct kbase_vmap_struct job_map;
	struct kbase_vmap_struct map;
	bool err = false;

	/* Replay job if fault is of type BASE_JD_EVENT_JOB_WRITE_FAULT or
	 * if force_replay is enabled.
	 */
	if (BASE_JD_EVENT_TERMINATED == katom->event_code) {
		return false;
	} else if (BASE_JD_EVENT_JOB_WRITE_FAULT == katom->event_code) {
		return true;
	} else if (BASE_JD_EVENT_FORCE_REPLAY == katom->event_code) {
		katom->event_code = BASE_JD_EVENT_DATA_INVALID_FAULT;
		return true;
	} else if (BASE_JD_EVENT_DATA_INVALID_FAULT != katom->event_code) {
		/* No replay for faults of type other than
		 * BASE_JD_EVENT_DATA_INVALID_FAULT.
		 */
		return false;
	}

	/* Job fault is BASE_JD_EVENT_DATA_INVALID_FAULT, now scan fragment jc
	 * to find out whether the source of exception is POLYGON_LIST. Replay
	 * is required if the source of fault is POLYGON_LIST.
	 */
	payload = kbase_vmap(kctx, katom->jc, sizeof(*payload), &map);
	if (!payload) {
		dev_err(dev, "kbase_replay_fault_check: failed to map payload.\n");
		return false;
	}

#ifdef CONFIG_MALI_DEBUG
	dev_dbg(dev, "kbase_replay_fault_check: payload=%p\n", payload);
	dev_dbg(dev, "\nPayload structure:\n"
		     "fragment_jc              = 0x%llx\n"
		     "fragment_hierarchy_mask  = 0x%x\n"
		     "fragment_core_req        = 0x%x\n",
		     payload->fragment_jc,
		     payload->fragment_hierarchy_mask,
		     payload->fragment_core_req);
#endif
	/* Process fragment job chain */
	job_header      = (u64) payload->fragment_jc;
	job_loop_detect = job_header;
	while (job_header) {
		job = kbase_vmap(kctx, job_header, sizeof(*job), &job_map);
		if (!job) {
			dev_err(dev, "failed to map jc\n");
			/* unmap payload*/
			kbase_vunmap(kctx, &map);
			return false;
		}


		dump_job_head(kctx, "\njob_head structure:\n", job);

		/* Replay only when the polygon list reader caused the
		 * DATA_INVALID_FAULT */
		if ((BASE_JD_EVENT_DATA_INVALID_FAULT == katom->event_code) &&
		   (JOB_POLYGON_LIST == JOB_SOURCE_ID(job->exception_status))) {
			err = true;
			kbase_vunmap(kctx, &job_map);
			break;
		}

		/* Move on to next fragment job in the list */
		if (job->job_descriptor_size)
			job_header = job->next_job._64;
		else
			job_header = job->next_job._32;

		kbase_vunmap(kctx, &job_map);

		/* Job chain loop detected */
		if (job_header == job_loop_detect)
			break;
	}

	/* unmap payload*/
	kbase_vunmap(kctx, &map);

	return err;
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
 * @return           false if the atom has completed
 *                   true if the atom is replaying jobs
 */
bool kbase_replay_process(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;

	/* Don't replay this atom if these issues are not present in the
	 * hardware */
	if (!kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_11020) &&
			!kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_11024)) {
		dev_dbg(kbdev->dev, "Hardware does not need replay workaround");

		/* Signal failure to userspace */
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;

		return false;
	}

	if (katom->event_code == BASE_JD_EVENT_DONE) {
		dev_dbg(kbdev->dev, "Previous job succeeded - not replaying\n");

		if (katom->retry_count)
			kbase_disjoint_state_down(kbdev);

		return false;
	}

	if (kbase_ctx_flag(kctx, KCTX_DYING)) {
		dev_dbg(kbdev->dev, "Not replaying; context is dying\n");

		if (katom->retry_count)
			kbase_disjoint_state_down(kbdev);

		return false;
	}

	/* Check job exception type and source before replaying. */
	if (!kbase_replay_fault_check(katom)) {
		dev_dbg(kbdev->dev,
			"Replay cancelled on event %x\n", katom->event_code);
		/* katom->event_code is already set to the failure code of the
		 * previous job.
		 */
		return false;
	}

	dev_warn(kbdev->dev, "Replaying jobs retry=%d\n",
			katom->retry_count);

	katom->retry_count++;

	if (katom->retry_count > BASEP_JD_REPLAY_LIMIT) {
		dev_err(kbdev->dev, "Replay exceeded limit - failing jobs\n");

		kbase_disjoint_state_down(kbdev);

		/* katom->event_code is already set to the failure code of the
		   previous job */
		return false;
	}

	/* only enter the disjoint state once for the whole time while the replay is ongoing */
	if (katom->retry_count == 1)
		kbase_disjoint_state_up(kbdev);

	INIT_WORK(&katom->work, kbase_replay_process_worker);
	queue_work(kctx->event_workq, &katom->work);

	return true;
}
