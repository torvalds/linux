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
#include "amdgpu_imu.h"
#include "soc15.h"
#include "amdgpu_ras.h"
#include "amdgpu_ring_mux.h"

/* GFX current status */
#define AMDGPU_GFX_NORMAL_MODE			0x00000000L
#define AMDGPU_GFX_SAFE_MODE			0x00000001L
#define AMDGPU_GFX_PG_DISABLED_MODE		0x00000002L
#define AMDGPU_GFX_CG_DISABLED_MODE		0x00000004L
#define AMDGPU_GFX_LBPW_DISABLED_MODE		0x00000008L

#define AMDGPU_MAX_GC_INSTANCES		8
#define AMDGPU_MAX_QUEUES		128

#define AMDGPU_MAX_GFX_QUEUES AMDGPU_MAX_QUEUES
#define AMDGPU_MAX_COMPUTE_QUEUES AMDGPU_MAX_QUEUES

enum amdgpu_gfx_pipe_priority {
	AMDGPU_GFX_PIPE_PRIO_NORMAL = AMDGPU_RING_PRIO_1,
	AMDGPU_GFX_PIPE_PRIO_HIGH = AMDGPU_RING_PRIO_2
};

#define AMDGPU_GFX_QUEUE_PRIORITY_MINIMUM  0
#define AMDGPU_GFX_QUEUE_PRIORITY_MAXIMUM  15

enum amdgpu_gfx_partition {
	AMDGPU_SPX_PARTITION_MODE = 0,
	AMDGPU_DPX_PARTITION_MODE = 1,
	AMDGPU_TPX_PARTITION_MODE = 2,
	AMDGPU_QPX_PARTITION_MODE = 3,
	AMDGPU_CPX_PARTITION_MODE = 4,
	AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE = -1,
	/* Automatically choose the right mode */
	AMDGPU_AUTO_COMPUTE_PARTITION_MODE = -2,
};

#define NUM_XCC(x) hweight16(x)

enum amdgpu_gfx_ras_mem_id_type {
	AMDGPU_GFX_CP_MEM = 0,
	AMDGPU_GFX_GCEA_MEM,
	AMDGPU_GFX_GC_CANE_MEM,
	AMDGPU_GFX_GCUTCL2_MEM,
	AMDGPU_GFX_GDS_MEM,
	AMDGPU_GFX_LDS_MEM,
	AMDGPU_GFX_RLC_MEM,
	AMDGPU_GFX_SP_MEM,
	AMDGPU_GFX_SPI_MEM,
	AMDGPU_GFX_SQC_MEM,
	AMDGPU_GFX_SQ_MEM,
	AMDGPU_GFX_TA_MEM,
	AMDGPU_GFX_TCC_MEM,
	AMDGPU_GFX_TCA_MEM,
	AMDGPU_GFX_TCI_MEM,
	AMDGPU_GFX_TCP_MEM,
	AMDGPU_GFX_TD_MEM,
	AMDGPU_GFX_TCX_MEM,
	AMDGPU_GFX_ATC_L2_MEM,
	AMDGPU_GFX_UTCL2_MEM,
	AMDGPU_GFX_VML2_MEM,
	AMDGPU_GFX_VML2_WALKER_MEM,
	AMDGPU_GFX_MEM_TYPE_NUM
};

struct amdgpu_mec {
	struct amdgpu_bo	*hpd_eop_obj;
	u64			hpd_eop_gpu_addr;
	struct amdgpu_bo	*mec_fw_obj;
	u64			mec_fw_gpu_addr;
	struct amdgpu_bo	*mec_fw_data_obj;
	u64			mec_fw_data_gpu_addr;

	u32 num_mec;
	u32 num_pipe_per_mec;
	u32 num_queue_per_pipe;
	void			*mqd_backup[AMDGPU_MAX_COMPUTE_RINGS * AMDGPU_MAX_GC_INSTANCES];
};

struct amdgpu_mec_bitmap {
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
	void			*mqd_backup;
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
	uint8_t num_pkrs;
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
	/* Whether texture coordinate truncation is conformant. */
	bool ta_cntl2_truncate_coord_mode;
	uint64_t tcc_disabled_mask;
	uint32_t gc_num_tcp_per_sa;
	uint32_t gc_num_sdp_interface;
	uint32_t gc_num_tcps;
	uint32_t gc_num_tcp_per_wpg;
	uint32_t gc_tcp_l1_size;
	uint32_t gc_num_sqc_per_wgp;
	uint32_t gc_l1_instruction_cache_size_per_sqc;
	uint32_t gc_l1_data_cache_size_per_sqc;
	uint32_t gc_gl1c_per_sa;
	uint32_t gc_gl1c_size_per_instance;
	uint32_t gc_gl2c_per_gpu;
	uint32_t gc_tcp_size_per_cu;
	uint32_t gc_num_cu_per_sqc;
	uint32_t gc_tcc_size;
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
	uint32_t bitmap[AMDGPU_MAX_GC_INSTANCES][4][4];
};

struct amdgpu_gfx_ras {
	struct amdgpu_ras_block_object  ras_block;
	void (*enable_watchdog_timer)(struct amdgpu_device *adev);
	int (*rlc_gc_fed_irq)(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry);
	int (*poison_consumption_handler)(struct amdgpu_device *adev,
						struct amdgpu_iv_entry *entry);
};

struct amdgpu_gfx_shadow_info {
	u32 shadow_size;
	u32 shadow_alignment;
	u32 csa_size;
	u32 csa_alignment;
};

struct amdgpu_gfx_funcs {
	/* get the gpu clock counter */
	uint64_t (*get_gpu_clock_counter)(struct amdgpu_device *adev);
	void (*select_se_sh)(struct amdgpu_device *adev, u32 se_num,
			     u32 sh_num, u32 instance, int xcc_id);
	void (*read_wave_data)(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd,
			       uint32_t wave, uint32_t *dst, int *no_fields);
	void (*read_wave_vgprs)(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd,
				uint32_t wave, uint32_t thread, uint32_t start,
				uint32_t size, uint32_t *dst);
	void (*read_wave_sgprs)(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd,
				uint32_t wave, uint32_t start, uint32_t size,
				uint32_t *dst);
	void (*select_me_pipe_q)(struct amdgpu_device *adev, u32 me, u32 pipe,
				 u32 queue, u32 vmid, u32 xcc_id);
	void (*init_spm_golden)(struct amdgpu_device *adev);
	void (*update_perfmon_mgcg)(struct amdgpu_device *adev, bool enable);
	int (*get_gfx_shadow_info)(struct amdgpu_device *adev,
				   struct amdgpu_gfx_shadow_info *shadow_info);
	enum amdgpu_gfx_partition
			(*query_partition_mode)(struct amdgpu_device *adev);
	int (*switch_partition_mode)(struct amdgpu_device *adev,
				     int num_xccs_per_xcp);
	int (*ih_node_to_logical_xcc)(struct amdgpu_device *adev, int ih_node);
};

struct sq_work {
	struct work_struct	work;
	unsigned ih_data;
};

struct amdgpu_pfp {
	struct amdgpu_bo		*pfp_fw_obj;
	uint64_t			pfp_fw_gpu_addr;
	uint32_t			*pfp_fw_ptr;

	struct amdgpu_bo		*pfp_fw_data_obj;
	uint64_t			pfp_fw_data_gpu_addr;
	uint32_t			*pfp_fw_data_ptr;
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

	struct amdgpu_bo		*me_fw_data_obj;
	uint64_t			me_fw_data_gpu_addr;
	uint32_t			*me_fw_data_ptr;

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
	struct amdgpu_mec_bitmap	mec_bitmap[AMDGPU_MAX_GC_INSTANCES];
	struct amdgpu_kiq		kiq[AMDGPU_MAX_GC_INSTANCES];
	struct amdgpu_imu		imu;
	bool				rs64_enable; /* firmware format */
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
	const struct firmware		*imu_fw; /* IMU firmware */
	uint32_t			imu_fw_version;
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
	uint32_t			rlcp_ucode_version;
	uint32_t			rlcp_ucode_feature_version;
	uint32_t			rlcv_ucode_version;
	uint32_t			rlcv_ucode_feature_version;
	uint32_t			mec_feature_version;
	uint32_t			mec2_feature_version;
	bool				mec_fw_write_wait;
	bool				me_fw_write_wait;
	bool				cp_fw_write_wait;
	struct amdgpu_ring		gfx_ring[AMDGPU_MAX_GFX_RINGS];
	unsigned			num_gfx_rings;
	struct amdgpu_ring		compute_ring[AMDGPU_MAX_COMPUTE_RINGS * AMDGPU_MAX_GC_INSTANCES];
	unsigned			num_compute_rings;
	struct amdgpu_irq_src		eop_irq;
	struct amdgpu_irq_src		priv_reg_irq;
	struct amdgpu_irq_src		priv_inst_irq;
	struct amdgpu_irq_src		cp_ecc_error_irq;
	struct amdgpu_irq_src		sq_irq;
	struct amdgpu_irq_src		rlc_gc_fed_irq;
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
	bool                            gfx_off_state;      /* true: enabled, false: disabled */
	struct mutex                    gfx_off_mutex;      /* mutex to change gfxoff state */
	uint32_t                        gfx_off_req_count;  /* default 1, enable gfx off: dec 1, disable gfx off: add 1 */
	struct delayed_work             gfx_off_delay_work; /* async work to set gfx block off */
	uint32_t                        gfx_off_residency;  /* last logged residency */
	uint64_t                        gfx_off_entrycount; /* count of times GPU has get into GFXOFF state */

	/* pipe reservation */
	struct mutex			pipe_reserve_mutex;
	DECLARE_BITMAP			(pipe_reserve_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	/*ras */
	struct ras_common_if		*ras_if;
	struct amdgpu_gfx_ras		*ras;

	bool				is_poweron;

	struct amdgpu_ring		sw_gfx_ring[AMDGPU_MAX_SW_GFX_RINGS];
	struct amdgpu_ring_mux          muxer;

	bool				cp_gfx_shadow; /* for gfx11 */

	uint16_t 			xcc_mask;
	uint32_t			num_xcc_per_xcp;
	struct mutex			partition_mutex;
	bool				mcbp; /* mid command buffer preemption */

	/* IP reg dump */
	uint32_t			*ip_dump_core;
	uint32_t			*ip_dump_cp_queues;
	uint32_t			*ip_dump_gfx_queues;
};

struct amdgpu_gfx_ras_reg_entry {
	struct amdgpu_ras_err_status_reg_entry reg_entry;
	enum amdgpu_gfx_ras_mem_id_type mem_id_type;
	uint32_t se_num;
};

struct amdgpu_gfx_ras_mem_id_entry {
	const struct amdgpu_ras_memory_id_entry *mem_id_ent;
	uint32_t size;
};

#define AMDGPU_GFX_MEMID_ENT(x) {(x), ARRAY_SIZE(x)},

#define amdgpu_gfx_get_gpu_clock_counter(adev) (adev)->gfx.funcs->get_gpu_clock_counter((adev))
#define amdgpu_gfx_select_se_sh(adev, se, sh, instance, xcc_id) ((adev)->gfx.funcs->select_se_sh((adev), (se), (sh), (instance), (xcc_id)))
#define amdgpu_gfx_select_me_pipe_q(adev, me, pipe, q, vmid, xcc_id) ((adev)->gfx.funcs->select_me_pipe_q((adev), (me), (pipe), (q), (vmid), (xcc_id)))
#define amdgpu_gfx_init_spm_golden(adev) (adev)->gfx.funcs->init_spm_golden((adev))
#define amdgpu_gfx_get_gfx_shadow_info(adev, si) ((adev)->gfx.funcs->get_gfx_shadow_info((adev), (si)))

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

void amdgpu_gfx_parse_disable_cu(unsigned *mask, unsigned max_se,
				 unsigned max_sh);

int amdgpu_gfx_kiq_init_ring(struct amdgpu_device *adev, int xcc_id);

void amdgpu_gfx_kiq_free_ring(struct amdgpu_ring *ring);

void amdgpu_gfx_kiq_fini(struct amdgpu_device *adev, int xcc_id);
int amdgpu_gfx_kiq_init(struct amdgpu_device *adev,
			unsigned hpd_size, int xcc_id);

int amdgpu_gfx_mqd_sw_init(struct amdgpu_device *adev,
			   unsigned mqd_size, int xcc_id);
void amdgpu_gfx_mqd_sw_fini(struct amdgpu_device *adev, int xcc_id);
int amdgpu_gfx_disable_kcq(struct amdgpu_device *adev, int xcc_id);
int amdgpu_gfx_enable_kcq(struct amdgpu_device *adev, int xcc_id);
int amdgpu_gfx_disable_kgq(struct amdgpu_device *adev, int xcc_id);
int amdgpu_gfx_enable_kgq(struct amdgpu_device *adev, int xcc_id);

void amdgpu_gfx_compute_queue_acquire(struct amdgpu_device *adev);
void amdgpu_gfx_graphics_queue_acquire(struct amdgpu_device *adev);

int amdgpu_gfx_mec_queue_to_bit(struct amdgpu_device *adev, int mec,
				int pipe, int queue);
void amdgpu_queue_mask_bit_to_mec_queue(struct amdgpu_device *adev, int bit,
				 int *mec, int *pipe, int *queue);
bool amdgpu_gfx_is_mec_queue_enabled(struct amdgpu_device *adev, int xcc_id,
				     int mec, int pipe, int queue);
bool amdgpu_gfx_is_high_priority_compute_queue(struct amdgpu_device *adev,
					       struct amdgpu_ring *ring);
bool amdgpu_gfx_is_high_priority_graphics_queue(struct amdgpu_device *adev,
						struct amdgpu_ring *ring);
int amdgpu_gfx_me_queue_to_bit(struct amdgpu_device *adev, int me,
			       int pipe, int queue);
void amdgpu_gfx_bit_to_me_queue(struct amdgpu_device *adev, int bit,
				int *me, int *pipe, int *queue);
bool amdgpu_gfx_is_me_queue_enabled(struct amdgpu_device *adev, int me,
				    int pipe, int queue);
void amdgpu_gfx_off_ctrl(struct amdgpu_device *adev, bool enable);
int amdgpu_get_gfx_off_status(struct amdgpu_device *adev, uint32_t *value);
int amdgpu_gfx_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block);
void amdgpu_gfx_ras_fini(struct amdgpu_device *adev);
int amdgpu_get_gfx_off_entrycount(struct amdgpu_device *adev, u64 *value);
int amdgpu_get_gfx_off_residency(struct amdgpu_device *adev, u32 *residency);
int amdgpu_set_gfx_off_residency(struct amdgpu_device *adev, bool value);
int amdgpu_gfx_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry);
int amdgpu_gfx_cp_ecc_error_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry);
uint32_t amdgpu_kiq_rreg(struct amdgpu_device *adev, uint32_t reg, uint32_t xcc_id);
void amdgpu_kiq_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v, uint32_t xcc_id);
int amdgpu_gfx_get_num_kcq(struct amdgpu_device *adev);
void amdgpu_gfx_cp_init_microcode(struct amdgpu_device *adev, uint32_t ucode_id);

int amdgpu_gfx_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_gfx_poison_consumption_handler(struct amdgpu_device *adev,
						struct amdgpu_iv_entry *entry);

bool amdgpu_gfx_is_master_xcc(struct amdgpu_device *adev, int xcc_id);
int amdgpu_gfx_sysfs_init(struct amdgpu_device *adev);
void amdgpu_gfx_sysfs_fini(struct amdgpu_device *adev);
void amdgpu_gfx_ras_error_func(struct amdgpu_device *adev,
		void *ras_error_status,
		void (*func)(struct amdgpu_device *adev, void *ras_error_status,
				int xcc_id));

static inline const char *amdgpu_gfx_compute_mode_desc(int mode)
{
	switch (mode) {
	case AMDGPU_SPX_PARTITION_MODE:
		return "SPX";
	case AMDGPU_DPX_PARTITION_MODE:
		return "DPX";
	case AMDGPU_TPX_PARTITION_MODE:
		return "TPX";
	case AMDGPU_QPX_PARTITION_MODE:
		return "QPX";
	case AMDGPU_CPX_PARTITION_MODE:
		return "CPX";
	default:
		return "UNKNOWN";
	}
}

#endif
