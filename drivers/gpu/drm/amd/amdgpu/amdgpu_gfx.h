/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __AMDGPU_GFX_H__
#define __AMDGPU_GFX_H__

/*
 * GFX stuff
 */
#include "clearstate_defs.h"
#include "amdgpu_ring.h"
#include "amdgpu_rlc.h"

/* GFX current status */
#define AMDGPU_GFX_NORMAL_MODE			0x00000000L
#define AMDGPU_GFX_SAFE_MODE			0x00000001L
#define AMDGPU_GFX_PG_DISABLED_MODE		0x00000002L
#define AMDGPU_GFX_CG_DISABLED_MODE		0x00000004L
#define AMDGPU_GFX_LBPW_DISABLED_MODE		0x00000008L

#define AMDGPU_MAX_GFX_QUEUES KGD_MAX_QUEUES
#define AMDGPU_MAX_COMPUTE_QUEUES KGD_MAX_QUEUES

enum gfx_pipe_priority {
	AMDGPU_GFX_PIPE_PRIO_NORMAL = 1,
	AMDGPU_GFX_PIPE_PRIO_HIGH,
	AMDGPU_GFX_PIPE_PRIO_MAX
};

#define AMDGPU_GFX_QUEUE_PRIORITY_MINIMUM  0
#define AMDGPU_GFX_QUEUE_PRIORITY_MAXIMUM  15

struct amdgpu_mec {
	struct amdgpu_bo	*hpd_eop_obj;
	u64			hpd_eop_gpu_addr;
	struct amdgpu_bo	*mec_fw_obj;
	u64			mec_fw_gpu_addr;
	u32 num_mec;
	u32 num_pipe_per_mec;
	u32 num_queue_per_pipe;
	void			*mqd_backup[AMDGPU_MAX_COMPUTE_RINGS + 1];

	/* These are the resources for which amdgpu takes ownership */
	DECLARE_BITMAP(queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);
};

enum amdgpu_unmap_queues_action {
	PREEMPT_QUEUES = 0,
	RESET_QUEUES,
	DISABLE_PROCESS_QUEUES,
	PREEMPT_QUEUES_NO_UNMAP,
};

struct kiq_pm4_funcs {
	/* Support ASIC-specific kiq pm4 packets*/
	void (*kiq_set_resources)(struct amdgpu_ring *kiq_ring,
					uint64_t queue_mask);
	void (*kiq_map_queues)(struct amdgpu_ring *kiq_ring,
					struct amdgpu_ring *ring);
	void (*kiq_unmap_queues)(struct amdgpu_ring *kiq_ring,
				 struct amdgpu_ring *ring,
				 enum amdgpu_unmap_queues_action action,
				 u64 gpu_addr, u64 seq);
	void (*kiq_query_status)(struct amdgpu_ring *kiq_ring,
					struct amdgpu_ring *ring,
					u64 addr,
					u64 seq);
	void (*kiq_invalidate_tlbs)(struct amdgpu_ring *kiq_ring,
				uint16_t pasid, uint32_t flush_type,
				bool all_hub);
	/* Packet sizes */
	int set_resources_size;
	int map_queues_size;
	int unmap_queues_size;
	int query_status_size;
	int invalidate_tlbs_size;
};

struct amdgpu_kiq {
	u64			eop_gpu_addr;
	struct amdgpu_bo	*eop_obj;
	spinlock_t              ring_lock;
	struct amdgpu_ring	ring;
	struct amdgpu_irq_src	irq;
	const struct kiq_pm4_funcs *pmf;
};

/*
 * GPU scratch registers structures, functions & helpers
 */
struct amdgpu_scratch {
	unsigned		num_reg;
	uint32_t                reg_base;
	uint32_t		free_mask;
};

/*
 * GFX configurations
 */
#define AMDGPU_GFX_MAX_SE 4
#define AMDGPU_GFX_MAX_SH_PER_SE 2

struct amdgpu_rb_config {
	uint32_t rb_backend_disable;
	uint32_t user_rb_backend_disable;
	uint32_t raster_config;
	uint32_t raster_config_1;
};

struct gb_addr_config {
	uint16_t pipe_interleave_size;
	uint8_t num_pipes;
	uint8_t max_compress_frags;
	uint8_t num_banks;
	uint8_t num_se;
	uint8_t num_rb_per_se;
};

struct amdgpu_gfx_config {
	unsigned max_shader_engines;
	unsigned max_tile_pipes;
	unsigned max_cu_per_sh;
	unsigned max_sh_per_se;
	unsigned max_backends_per_se;
	unsigned max_texture_channel_caches;
	unsigned max_gprs;
	unsigned max_gs_threads;
	unsigned max_hw_contexts;
	unsigned sc_prim_fifo_size_frontend;
	unsigned sc_prim_fifo_size_backend;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;

	unsigned num_tile_pipes;
	unsigned backend_enable_mask;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;
	unsigned mc_arb_ramcfg;
	unsigned num_banks;
	unsigned num_ranks;
	unsigned gb_addr_config;
	unsigned num_rbs;
	unsigned gs_vgt_table_depth;
	unsigned gs_prim_buffer_depth;

	uint32_t tile_mode_array[32];
	uint32_t macrotile_mode_array[16];

	struct gb_addr_config gb_addr_config_fields;
	struct amdgpu_rb_config rb_config[AMDGPU_GFX_MAX_SE][AMDGPU_GFX_MAX_SH_PER_SE];

	/* gfx configure feature */
	uint32_t double_offchip_lds_buf;
	/* cached value of DB_DEBUG2 */
	uint32_t db_debug2;
	/* gfx10 specific config */
	uint32_t num_sc_per_sh;
	uint32_t num_packer_per_sc;
	uint32_t pa_sc_tile_steering_override;
	uint64_t tcc_disabled_mask;
};

struct amdgpu_cu_info {
	uint32_t simd_per_cu;
	uint32_t max_waves_per_simd;
	uint32_t wave_front_size;
	uint32_t max_scratch_slots_per_cu;
	uint32_t lds_size;

	/* total active CU number */
	uint32_t number;
	uint32_t ao_cu_mask;
	uint32_t ao_cu_bitmap[4][4];
	uint32_t bitmap[4][4];
};

struct amdgpu_gfx_funcs {
	/* get the gpu clock counter */
	uint64_t (*get_gpu_clock_counter)(struct amdgpu_device *adev);
	void (*select_se_sh)(struct amdgpu_device *adev, u32 se_num,
			     u32 sh_num, u32 instance);
	void (*read_wave_data)(struct amdgpu_device *adev, uint32_t simd,
			       uint32_t wave, uint32_t *dst, int *no_fields);
	void (*read_wave_vgprs)(struct amdgpu_device *adev, uint32_t simd,
				uint32_t wave, uint32_t thread, uint32_t start,
				uint32_t size, uint32_t *dst);
	void (*read_wave_sgprs)(struct amdgpu_device *adev, uint32_t simd,
				uint32_t wave, uint32_t start, uint32_t size,
				uint32_t *dst);
	void (*select_me_pipe_q)(struct amdgpu_device *adev, u32 me, u32 pipe,
				 u32 queue, u32 vmid);
	int (*ras_error_inject)(struct amdgpu_device *adev, void *inject_if);
	int (*query_ras_error_count) (struct amdgpu_device *adev, void *ras_error_status);
	void (*reset_ras_error_count) (struct amdgpu_device *adev);
};

struct sq_work {
	struct work_struct	work;
	unsigned ih_data;
};

struct amdgpu_pfp {
	struct amdgpu_bo		*pfp_fw_obj;
	uint64_t			pfp_fw_gpu_addr;
	uint32_t			*pfp_fw_ptr;
};

struct amdgpu_ce {
	struct amdgpu_bo		*ce_fw_obj;
	uint64_t			ce_fw_gpu_addr;
	uint32_t			*ce_fw_ptr;
};

struct amdgpu_me {
	struct amdgpu_bo		*me_fw_obj;
	uint64_t			me_fw_gpu_addr;
	uint32_t			*me_fw_ptr;
	uint32_t			num_me;
	uint32_t			num_pipe_per_me;
	uint32_t			num_queue_per_pipe;
	void				*mqd_backup[AMDGPU_MAX_GFX_RINGS];

	/* These are the resources for which amdgpu takes ownership */
	DECLARE_BITMAP(queue_bitmap, AMDGPU_MAX_GFX_QUEUES);
};

struct amdgpu_gfx {
	struct mutex			gpu_clock_mutex;
	struct amdgpu_gfx_config	config;
	struct amdgpu_rlc		rlc;
	struct amdgpu_pfp		pfp;
	struct amdgpu_ce		ce;
	struct amdgpu_me		me;
	struct amdgpu_mec		mec;
	struct amdgpu_kiq		kiq;
	struct amdgpu_scratch		scratch;
	const struct firmware		*me_fw;	/* ME firmware */
	uint32_t			me_fw_version;
	const struct firmware		*pfp_fw; /* PFP firmware */
	uint32_t			pfp_fw_version;
	const struct firmware		*ce_fw;	/* CE firmware */
	uint32_t			ce_fw_version;
	const struct firmware		*rlc_fw; /* RLC firmware */
	uint32_t			rlc_fw_version;
	const struct firmware		*mec_fw; /* MEC firmware */
	uint32_t			mec_fw_version;
	const struct firmware		*mec2_fw; /* MEC2 firmware */
	uint32_t			mec2_fw_version;
	uint32_t			me_feature_version;
	uint32_t			ce_feature_version;
	uint32_t			pfp_feature_version;
	uint32_t			rlc_feature_version;
	uint32_t			rlc_srlc_fw_version;
	uint32_t			rlc_srlc_feature_version;
	uint32_t			rlc_srlg_fw_version;
	uint32_t			rlc_srlg_feature_version;
	uint32_t			rlc_srls_fw_version;
	uint32_t			rlc_srls_feature_version;
	uint32_t			mec_feature_version;
	uint32_t			mec2_feature_version;
	bool				mec_fw_write_wait;
	bool				me_fw_write_wait;
	bool				cp_fw_write_wait;
	struct amdgpu_ring		gfx_ring[AMDGPU_MAX_GFX_RINGS];
	unsigned			num_gfx_rings;
	struct amdgpu_ring		compute_ring[AMDGPU_MAX_COMPUTE_RINGS];
	unsigned			num_compute_rings;
	struct amdgpu_irq_src		eop_irq;
	struct amdgpu_irq_src		priv_reg_irq;
	struct amdgpu_irq_src		priv_inst_irq;
	struct amdgpu_irq_src		cp_ecc_error_irq;
	struct amdgpu_irq_src		sq_irq;
	struct sq_work			sq_work;

	/* gfx status */
	uint32_t			gfx_current_status;
	/* ce ram size*/
	unsigned			ce_ram_size;
	struct amdgpu_cu_info		cu_info;
	const struct amdgpu_gfx_funcs	*funcs;

	/* reset mask */
	uint32_t                        grbm_soft_reset;
	uint32_t                        srbm_soft_reset;

	/* gfx off */
	bool                            gfx_off_state; /* true: enabled, false: disabled */
	struct mutex                    gfx_off_mutex;
	uint32_t                        gfx_off_req_count; /* default 1, enable gfx off: dec 1, disable gfx off: add 1 */
	struct delayed_work             gfx_off_delay_work;

	/* pipe reservation */
	struct mutex			pipe_reserve_mutex;
	DECLARE_BITMAP			(pipe_reserve_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	/*ras */
	struct ras_common_if		*ras_if;
};

#define amdgpu_gfx_get_gpu_clock_counter(adev) (adev)->gfx.funcs->get_gpu_clock_counter((adev))
#define amdgpu_gfx_select_se_sh(adev, se, sh, instance) (adev)->gfx.funcs->select_se_sh((adev), (se), (sh), (instance))
#define amdgpu_gfx_select_me_pipe_q(adev, me, pipe, q, vmid) (adev)->gfx.funcs->select_me_pipe_q((adev), (me), (pipe), (q), (vmid))

/**
 * amdgpu_gfx_create_bitmask - create a bitmask
 *
 * @bit_width: length of the mask
 *
 * create a variable length bit mask.
 * Returns the bitmask.
 */
static inline u32 amdgpu_gfx_create_bitmask(u32 bit_width)
{
	return (u32)((1ULL << bit_width) - 1);
}

int amdgpu_gfx_scratch_get(struct amdgpu_device *adev, uint32_t *reg);
void amdgpu_gfx_scratch_free(struct amdgpu_device *adev, uint32_t reg);

void amdgpu_gfx_parse_disable_cu(unsigned *mask, unsigned max_se,
				 unsigned max_sh);

int amdgpu_gfx_kiq_init_ring(struct amdgpu_device *adev,
			     struct amdgpu_ring *ring,
			     struct amdgpu_irq_src *irq);

void amdgpu_gfx_kiq_free_ring(struct amdgpu_ring *ring);

void amdgpu_gfx_kiq_fini(struct amdgpu_device *adev);
int amdgpu_gfx_kiq_init(struct amdgpu_device *adev,
			unsigned hpd_size);

int amdgpu_gfx_mqd_sw_init(struct amdgpu_device *adev,
			   unsigned mqd_size);
void amdgpu_gfx_mqd_sw_fini(struct amdgpu_device *adev);
int amdgpu_gfx_disable_kcq(struct amdgpu_device *adev);
int amdgpu_gfx_enable_kcq(struct amdgpu_device *adev);

void amdgpu_gfx_compute_queue_acquire(struct amdgpu_device *adev);
void amdgpu_gfx_graphics_queue_acquire(struct amdgpu_device *adev);

int amdgpu_gfx_mec_queue_to_bit(struct amdgpu_device *adev, int mec,
				int pipe, int queue);
void amdgpu_queue_mask_bit_to_mec_queue(struct amdgpu_device *adev, int bit,
				 int *mec, int *pipe, int *queue);
bool amdgpu_gfx_is_mec_queue_enabled(struct amdgpu_device *adev, int mec,
				     int pipe, int queue);
bool amdgpu_gfx_is_high_priority_compute_queue(struct amdgpu_device *adev,
					       int queue);
int amdgpu_gfx_me_queue_to_bit(struct amdgpu_device *adev, int me,
			       int pipe, int queue);
void amdgpu_gfx_bit_to_me_queue(struct amdgpu_device *adev, int bit,
				int *me, int *pipe, int *queue);
bool amdgpu_gfx_is_me_queue_enabled(struct amdgpu_device *adev, int me,
				    int pipe, int queue);
void amdgpu_gfx_off_ctrl(struct amdgpu_device *adev, bool enable);
int amdgpu_gfx_ras_late_init(struct amdgpu_device *adev);
void amdgpu_gfx_ras_fini(struct amdgpu_device *adev);
int amdgpu_gfx_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry);
int amdgpu_gfx_cp_ecc_error_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry);
uint32_t amdgpu_kiq_rreg(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_kiq_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v);
#endif
