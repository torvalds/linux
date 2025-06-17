/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2023 Collabora ltd. */

#ifndef __PANTHOR_SCHED_H__
#define __PANTHOR_SCHED_H__

struct drm_exec;
struct dma_fence;
struct drm_file;
struct drm_gem_object;
struct drm_sched_job;
struct drm_memory_stats;
struct drm_panthor_group_create;
struct drm_panthor_queue_create;
struct drm_panthor_group_get_state;
struct drm_panthor_queue_submit;
struct panthor_device;
struct panthor_file;
struct panthor_group_pool;
struct panthor_job;

int panthor_group_create(struct panthor_file *pfile,
			 const struct drm_panthor_group_create *group_args,
			 const struct drm_panthor_queue_create *queue_args);
int panthor_group_destroy(struct panthor_file *pfile, u32 group_handle);
int panthor_group_get_state(struct panthor_file *pfile,
			    struct drm_panthor_group_get_state *get_state);

struct drm_sched_job *
panthor_job_create(struct panthor_file *pfile,
		   u16 group_handle,
		   const struct drm_panthor_queue_submit *qsubmit,
		   u64 drm_client_id);
struct drm_sched_job *panthor_job_get(struct drm_sched_job *job);
struct panthor_vm *panthor_job_vm(struct drm_sched_job *sched_job);
void panthor_job_put(struct drm_sched_job *job);
void panthor_job_update_resvs(struct drm_exec *exec, struct drm_sched_job *job);

int panthor_group_pool_create(struct panthor_file *pfile);
void panthor_group_pool_destroy(struct panthor_file *pfile);
void panthor_fdinfo_gather_group_mem_info(struct panthor_file *pfile,
					  struct drm_memory_stats *stats);

int panthor_sched_init(struct panthor_device *ptdev);
void panthor_sched_unplug(struct panthor_device *ptdev);
void panthor_sched_pre_reset(struct panthor_device *ptdev);
void panthor_sched_post_reset(struct panthor_device *ptdev, bool reset_failed);
void panthor_sched_suspend(struct panthor_device *ptdev);
void panthor_sched_resume(struct panthor_device *ptdev);

void panthor_sched_report_mmu_fault(struct panthor_device *ptdev);
void panthor_sched_report_fw_events(struct panthor_device *ptdev, u32 events);

void panthor_fdinfo_gather_group_samples(struct panthor_file *pfile);

#endif
