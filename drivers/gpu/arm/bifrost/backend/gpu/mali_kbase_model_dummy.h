/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2015, 2017-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/*
 * Dummy Model interface
 */

#ifndef _KBASE_MODEL_DUMMY_H_
#define _KBASE_MODEL_DUMMY_H_

#include <uapi/gpu/arm/bifrost/backend/gpu/mali_kbase_model_dummy.h>

#define model_error_log(module, ...) pr_err(__VA_ARGS__)

#define NUM_SLOTS 4		/*number of job slots */

/*Errors Mask Codes*/
/* each bit of errors_mask is associated to a specific error:
 * NON FAULT STATUS CODES: only the following are implemented since the others
 * represent normal working statuses
 */
#define KBASE_JOB_INTERRUPTED         (1<<0)
#define KBASE_JOB_STOPPED             (1<<1)
#define KBASE_JOB_TERMINATED          (1<<2)

/* JOB EXCEPTIONS: */
#define KBASE_JOB_CONFIG_FAULT        (1<<3)
#define KBASE_JOB_POWER_FAULT         (1<<4)
#define KBASE_JOB_READ_FAULT          (1<<5)
#define KBASE_JOB_WRITE_FAULT         (1<<6)
#define KBASE_JOB_AFFINITY_FAULT      (1<<7)
#define KBASE_JOB_BUS_FAULT           (1<<8)
#define KBASE_INSTR_INVALID_PC        (1<<9)
#define KBASE_INSTR_INVALID_ENC       (1<<10)
#define KBASE_INSTR_TYPE_MISMATCH     (1<<11)
#define KBASE_INSTR_OPERAND_FAULT     (1<<12)
#define KBASE_INSTR_TLS_FAULT         (1<<13)
#define KBASE_INSTR_BARRIER_FAULT     (1<<14)
#define KBASE_INSTR_ALIGN_FAULT       (1<<15)
#define KBASE_DATA_INVALID_FAULT      (1<<16)
#define KBASE_TILE_RANGE_FAULT        (1<<17)
#define KBASE_ADDR_RANGE_FAULT        (1<<18)
#define KBASE_OUT_OF_MEMORY           (1<<19)
#define KBASE_UNKNOWN                 (1<<20)

/* GPU EXCEPTIONS:*/
#define KBASE_DELAYED_BUS_FAULT       (1<<21)
#define KBASE_SHAREABILITY_FAULT      (1<<22)

/* MMU EXCEPTIONS:*/
#define KBASE_TRANSLATION_FAULT       (1<<23)
#define KBASE_PERMISSION_FAULT        (1<<24)
#define KBASE_TRANSTAB_BUS_FAULT      (1<<25)
#define KBASE_ACCESS_FLAG             (1<<26)

/* generic useful bitmasks */
#define IS_A_JOB_ERROR ((KBASE_UNKNOWN << 1) - KBASE_JOB_INTERRUPTED)
#define IS_A_MMU_ERROR ((KBASE_ACCESS_FLAG << 1) - KBASE_TRANSLATION_FAULT)
#define IS_A_GPU_ERROR (KBASE_DELAYED_BUS_FAULT|KBASE_SHAREABILITY_FAULT)

/* number of possible MMU address spaces */
#define NUM_MMU_AS 16 /* total number of MMU address spaces as in
		       * MMU_IRQ_RAWSTAT register
		       */

/* Forward declaration */
struct kbase_device;

/*
 * the function below is used to trigger the simulation of a faulty
 * HW condition for a specific job chain atom
 */

struct kbase_error_params {
	u64 jc;
	u32 errors_mask;
	u32 mmu_table_level;
	u16 faulty_mmu_as;
	u16 padding[3];
};

enum kbase_model_control_command {
	/* Disable/Enable job completion in the dummy model */
	KBASE_MC_DISABLE_JOBS
};

/* struct to control dummy model behavior */
struct kbase_model_control_params {
	s32 command;
	s32 value;
};

/* struct to track faulty atoms */
struct kbase_error_atom {
	struct kbase_error_params params;
	struct kbase_error_atom *next;
};

/*struct to track the system error state*/
struct error_status_t {
	spinlock_t access_lock;

	u32 errors_mask;
	u32 mmu_table_level;
	int faulty_mmu_as;

	u64 current_jc;
	int current_job_slot;

	u32 job_irq_rawstat;
	u32 job_irq_status;
	u32 js_status[NUM_SLOTS];

	u32 mmu_irq_mask;
	u32 mmu_irq_rawstat;

	u32 gpu_error_irq;
	u32 gpu_fault_status;

	u32 as_faultstatus[NUM_MMU_AS];
	u32 as_command[NUM_MMU_AS];
	u64 as_transtab[NUM_MMU_AS];
};

/**
 * struct gpu_model_prfcnt_en - Performance counter enable masks
 * @fe: Enable mask for front-end block
 * @tiler: Enable mask for tiler block
 * @l2: Enable mask for L2/Memory system blocks
 * @shader: Enable mask for shader core blocks
 */
struct gpu_model_prfcnt_en {
	u32 fe;
	u32 tiler;
	u32 l2;
	u32 shader;
};

void *midgard_model_create(const void *config);
void midgard_model_destroy(void *h);
u8 midgard_model_write_reg(void *h, u32 addr, u32 value);
u8 midgard_model_read_reg(void *h, u32 addr,
							u32 * const value);
void midgard_set_error(int job_slot);
int job_atom_inject_error(struct kbase_error_params *params);
int gpu_model_control(void *h,
				struct kbase_model_control_params *params);

/**
 * gpu_model_set_dummy_prfcnt_user_sample() - Set performance counter values
 * @data: Userspace pointer to array of counter values
 * @size: Size of counter value array
 *
 * Counter values set by this function will be used for one sample dump only
 * after which counters will be cleared back to zero.
 *
 * Return: 0 on success, else error code.
 */
int gpu_model_set_dummy_prfcnt_user_sample(u32 __user *data, u32 size);

/**
 * gpu_model_set_dummy_prfcnt_kernel_sample() - Set performance counter values
 * @data: Pointer to array of counter values
 * @size: Size of counter value array
 *
 * Counter values set by this function will be used for one sample dump only
 * after which counters will be cleared back to zero.
 */
void gpu_model_set_dummy_prfcnt_kernel_sample(u64 *data, u32 size);

void gpu_model_get_dummy_prfcnt_cores(struct kbase_device *kbdev,
		u64 *l2_present, u64 *shader_present);
void gpu_model_set_dummy_prfcnt_cores(struct kbase_device *kbdev,
		u64 l2_present, u64 shader_present);

/* Clear the counter values array maintained by the dummy model */
void gpu_model_clear_prfcnt_values(void);

#if MALI_USE_CSF
/**
 * gpu_model_prfcnt_dump_request() - Request performance counter sample dump.
 * @sample_buf:  Pointer to KBASE_DUMMY_MODEL_MAX_VALUES_PER_SAMPLE sized array
 *               in which to store dumped performance counter values.
 * @enable_maps: Physical enable maps for performance counter blocks.
 */
void gpu_model_prfcnt_dump_request(uint32_t *sample_buf, struct gpu_model_prfcnt_en enable_maps);

/**
 * gpu_model_glb_request_job_irq() - Trigger job interrupt with global request
 *                                   flag set.
 * @model: Model pointer returned by midgard_model_create().
 */
void gpu_model_glb_request_job_irq(void *model);
#endif /* MALI_USE_CSF */

enum gpu_dummy_irq {
	GPU_DUMMY_JOB_IRQ,
	GPU_DUMMY_GPU_IRQ,
	GPU_DUMMY_MMU_IRQ
};

void gpu_device_raise_irq(void *model,
						enum gpu_dummy_irq irq);
void gpu_device_set_data(void *model, void *data);
void *gpu_device_get_data(void *model);

extern struct error_status_t hw_error_status;

#endif
