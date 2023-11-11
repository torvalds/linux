/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Huang Rui
 *
 */
#ifndef __AMDGPU_PSP_H__
#define __AMDGPU_PSP_H__

#include "amdgpu.h"
#include "psp_gfx_if.h"
#include "ta_xgmi_if.h"
#include "ta_ras_if.h"
#include "ta_rap_if.h"
#include "ta_secureDisplay_if.h"

#define PSP_FENCE_BUFFER_SIZE	0x1000
#define PSP_CMD_BUFFER_SIZE	0x1000
#define PSP_1_MEG		0x100000
#define PSP_TMR_SIZE(adev)	((adev)->asic_type == CHIP_ALDEBARAN ? 0x800000 : 0x400000)
#define PSP_TMR_ALIGNMENT	0x100000
#define PSP_FW_NAME_LEN		0x24

enum psp_shared_mem_size {
	PSP_ASD_SHARED_MEM_SIZE				= 0x0,
	PSP_XGMI_SHARED_MEM_SIZE			= 0x4000,
	PSP_RAS_SHARED_MEM_SIZE				= 0x4000,
	PSP_HDCP_SHARED_MEM_SIZE			= 0x4000,
	PSP_DTM_SHARED_MEM_SIZE				= 0x4000,
	PSP_RAP_SHARED_MEM_SIZE				= 0x4000,
	PSP_SECUREDISPLAY_SHARED_MEM_SIZE	= 0x4000,
};

enum ta_type_id {
	TA_TYPE_XGMI = 1,
	TA_TYPE_RAS,
	TA_TYPE_HDCP,
	TA_TYPE_DTM,
	TA_TYPE_RAP,
	TA_TYPE_SECUREDISPLAY,

	TA_TYPE_MAX_INDEX,
};

struct psp_context;
struct psp_xgmi_node_info;
struct psp_xgmi_topology_info;
struct psp_bin_desc;

enum psp_bootloader_cmd {
	PSP_BL__LOAD_SYSDRV		= 0x10000,
	PSP_BL__LOAD_SOSDRV		= 0x20000,
	PSP_BL__LOAD_KEY_DATABASE	= 0x80000,
	PSP_BL__LOAD_SOCDRV             = 0xB0000,
	PSP_BL__LOAD_DBGDRV             = 0xC0000,
	PSP_BL__LOAD_INTFDRV		= 0xD0000,
	PSP_BL__LOAD_RASDRV		    = 0xE0000,
	PSP_BL__DRAM_LONG_TRAIN		= 0x100000,
	PSP_BL__DRAM_SHORT_TRAIN	= 0x200000,
	PSP_BL__LOAD_TOS_SPL_TABLE	= 0x10000000,
};

enum psp_ring_type
{
	PSP_RING_TYPE__INVALID = 0,
	/*
	 * These values map to the way the PSP kernel identifies the
	 * rings.
	 */
	PSP_RING_TYPE__UM = 1, /* User mode ring (formerly called RBI) */
	PSP_RING_TYPE__KM = 2  /* Kernel mode ring (formerly called GPCOM) */
};

struct psp_ring
{
	enum psp_ring_type		ring_type;
	struct psp_gfx_rb_frame		*ring_mem;
	uint64_t			ring_mem_mc_addr;
	void				*ring_mem_handle;
	uint32_t			ring_size;
	uint32_t			ring_wptr;
};

/* More registers may will be supported */
enum psp_reg_prog_id {
	PSP_REG_IH_RB_CNTL        = 0,  /* register IH_RB_CNTL */
	PSP_REG_IH_RB_CNTL_RING1  = 1,  /* register IH_RB_CNTL_RING1 */
	PSP_REG_IH_RB_CNTL_RING2  = 2,  /* register IH_RB_CNTL_RING2 */
	PSP_REG_LAST
};

struct psp_funcs
{
	int (*init_microcode)(struct psp_context *psp);
	int (*bootloader_load_kdb)(struct psp_context *psp);
	int (*bootloader_load_spl)(struct psp_context *psp);
	int (*bootloader_load_sysdrv)(struct psp_context *psp);
	int (*bootloader_load_soc_drv)(struct psp_context *psp);
	int (*bootloader_load_intf_drv)(struct psp_context *psp);
	int (*bootloader_load_dbg_drv)(struct psp_context *psp);
	int (*bootloader_load_ras_drv)(struct psp_context *psp);
	int (*bootloader_load_sos)(struct psp_context *psp);
	int (*ring_init)(struct psp_context *psp, enum psp_ring_type ring_type);
	int (*ring_create)(struct psp_context *psp,
			   enum psp_ring_type ring_type);
	int (*ring_stop)(struct psp_context *psp,
			    enum psp_ring_type ring_type);
	int (*ring_destroy)(struct psp_context *psp,
			    enum psp_ring_type ring_type);
	bool (*smu_reload_quirk)(struct psp_context *psp);
	int (*mode1_reset)(struct psp_context *psp);
	int (*mem_training)(struct psp_context *psp, uint32_t ops);
	uint32_t (*ring_get_wptr)(struct psp_context *psp);
	void (*ring_set_wptr)(struct psp_context *psp, uint32_t value);
	int (*load_usbc_pd_fw)(struct psp_context *psp, uint64_t fw_pri_mc_addr);
	int (*read_usbc_pd_fw)(struct psp_context *psp, uint32_t *fw_ver);
	int (*update_spirom)(struct psp_context *psp, uint64_t fw_pri_mc_addr);
	int (*vbflash_stat)(struct psp_context *psp);
};

#define AMDGPU_XGMI_MAX_CONNECTED_NODES		64
struct psp_xgmi_node_info {
	uint64_t				node_id;
	uint8_t					num_hops;
	uint8_t					is_sharing_enabled;
	enum ta_xgmi_assigned_sdma_engine	sdma_engine;
	uint8_t					num_links;
};

struct psp_xgmi_topology_info {
	uint32_t			num_nodes;
	struct psp_xgmi_node_info	nodes[AMDGPU_XGMI_MAX_CONNECTED_NODES];
};

struct psp_bin_desc {
	uint32_t fw_version;
	uint32_t feature_version;
	uint32_t size_bytes;
	uint8_t *start_addr;
};

struct ta_mem_context {
	struct amdgpu_bo		*shared_bo;
	uint64_t		shared_mc_addr;
	void			*shared_buf;
	enum psp_shared_mem_size	shared_mem_size;
};

struct ta_context {
	bool			initialized;
	uint32_t		session_id;
	uint32_t		resp_status;
	struct ta_mem_context	mem_context;
	struct psp_bin_desc		bin_desc;
	enum psp_gfx_cmd_id		ta_load_type;
	enum ta_type_id		ta_type;
};

struct ta_cp_context {
	struct ta_context		context;
	struct mutex			mutex;
};

struct psp_xgmi_context {
	struct ta_context		context;
	struct psp_xgmi_topology_info	top_info;
	bool				supports_extended_data;
};

struct psp_ras_context {
	struct ta_context		context;
	struct amdgpu_ras		*ras;
};

#define MEM_TRAIN_SYSTEM_SIGNATURE		0x54534942
#define GDDR6_MEM_TRAINING_DATA_SIZE_IN_BYTES	0x1000
#define GDDR6_MEM_TRAINING_OFFSET		0x8000
/*Define the VRAM size that will be encroached by BIST training.*/
#define GDDR6_MEM_TRAINING_ENCROACHED_SIZE	0x2000000

enum psp_memory_training_init_flag {
	PSP_MEM_TRAIN_NOT_SUPPORT	= 0x0,
	PSP_MEM_TRAIN_SUPPORT		= 0x1,
	PSP_MEM_TRAIN_INIT_FAILED	= 0x2,
	PSP_MEM_TRAIN_RESERVE_SUCCESS	= 0x4,
	PSP_MEM_TRAIN_INIT_SUCCESS	= 0x8,
};

enum psp_memory_training_ops {
	PSP_MEM_TRAIN_SEND_LONG_MSG	= 0x1,
	PSP_MEM_TRAIN_SAVE		= 0x2,
	PSP_MEM_TRAIN_RESTORE		= 0x4,
	PSP_MEM_TRAIN_SEND_SHORT_MSG	= 0x8,
	PSP_MEM_TRAIN_COLD_BOOT		= PSP_MEM_TRAIN_SEND_LONG_MSG,
	PSP_MEM_TRAIN_RESUME		= PSP_MEM_TRAIN_SEND_SHORT_MSG,
};

struct psp_memory_training_context {
	/*training data size*/
	u64 train_data_size;
	/*
	 * sys_cache
	 * cpu virtual address
	 * system memory buffer that used to store the training data.
	 */
	void *sys_cache;

	/*vram offset of the p2c training data*/
	u64 p2c_train_data_offset;

	/*vram offset of the c2p training data*/
	u64 c2p_train_data_offset;
	struct amdgpu_bo *c2p_bo;

	enum psp_memory_training_init_flag init;
	u32 training_cnt;
	bool enable_mem_training;
};

/** PSP runtime DB **/
#define PSP_RUNTIME_DB_SIZE_IN_BYTES		0x10000
#define PSP_RUNTIME_DB_OFFSET			0x100000
#define PSP_RUNTIME_DB_COOKIE_ID		0x0ed5
#define PSP_RUNTIME_DB_VER_1			0x0100
#define PSP_RUNTIME_DB_DIAG_ENTRY_MAX_COUNT	0x40

enum psp_runtime_entry_type {
	PSP_RUNTIME_ENTRY_TYPE_INVALID		= 0x0,
	PSP_RUNTIME_ENTRY_TYPE_TEST		= 0x1,
	PSP_RUNTIME_ENTRY_TYPE_MGPU_COMMON	= 0x2,  /* Common mGPU runtime data */
	PSP_RUNTIME_ENTRY_TYPE_MGPU_WAFL	= 0x3,  /* WAFL runtime data */
	PSP_RUNTIME_ENTRY_TYPE_MGPU_XGMI	= 0x4,  /* XGMI runtime data */
	PSP_RUNTIME_ENTRY_TYPE_BOOT_CONFIG	= 0x5,  /* Boot Config runtime data */
	PSP_RUNTIME_ENTRY_TYPE_PPTABLE_ERR_STATUS = 0x6, /* SCPM validation data */
};

/* PSP runtime DB header */
struct psp_runtime_data_header {
	/* determine the existence of runtime db */
	uint16_t cookie;
	/* version of runtime db */
	uint16_t version;
};

/* PSP runtime DB entry */
struct psp_runtime_entry {
	/* type of runtime db entry */
	uint32_t entry_type;
	/* offset of entry in bytes */
	uint16_t offset;
	/* size of entry in bytes */
	uint16_t size;
};

/* PSP runtime DB directory */
struct psp_runtime_data_directory {
	/* number of valid entries */
	uint16_t			entry_count;
	/* db entries*/
	struct psp_runtime_entry	entry_list[PSP_RUNTIME_DB_DIAG_ENTRY_MAX_COUNT];
};

/* PSP runtime DB boot config feature bitmask */
enum psp_runtime_boot_cfg_feature {
	BOOT_CFG_FEATURE_GECC                       = 0x1,
	BOOT_CFG_FEATURE_TWO_STAGE_DRAM_TRAINING    = 0x2,
};

/* PSP run time DB SCPM authentication defines */
enum psp_runtime_scpm_authentication {
	SCPM_DISABLE                     = 0x0,
	SCPM_ENABLE                      = 0x1,
	SCPM_ENABLE_WITH_SCPM_ERR        = 0x2,
};

/* PSP runtime DB boot config entry */
struct psp_runtime_boot_cfg_entry {
	uint32_t boot_cfg_bitmask;
	uint32_t reserved;
};

/* PSP runtime DB SCPM entry */
struct psp_runtime_scpm_entry {
	enum psp_runtime_scpm_authentication scpm_status;
};

struct psp_context
{
	struct amdgpu_device            *adev;
	struct psp_ring                 km_ring;
	struct psp_gfx_cmd_resp		*cmd;

	const struct psp_funcs		*funcs;

	/* firmware buffer */
	struct amdgpu_bo		*fw_pri_bo;
	uint64_t			fw_pri_mc_addr;
	void				*fw_pri_buf;

	/* sos firmware */
	const struct firmware		*sos_fw;
	struct psp_bin_desc		sys;
	struct psp_bin_desc		sos;
	struct psp_bin_desc		toc;
	struct psp_bin_desc		kdb;
	struct psp_bin_desc		spl;
	struct psp_bin_desc		rl;
	struct psp_bin_desc		soc_drv;
	struct psp_bin_desc		intf_drv;
	struct psp_bin_desc		dbg_drv;
	struct psp_bin_desc		ras_drv;

	/* tmr buffer */
	struct amdgpu_bo		*tmr_bo;
	uint64_t			tmr_mc_addr;

	/* asd firmware */
	const struct firmware	*asd_fw;

	/* toc firmware */
	const struct firmware		*toc_fw;

	/* cap firmware */
	const struct firmware		*cap_fw;

	/* fence buffer */
	struct amdgpu_bo		*fence_buf_bo;
	uint64_t			fence_buf_mc_addr;
	void				*fence_buf;

	/* cmd buffer */
	struct amdgpu_bo		*cmd_buf_bo;
	uint64_t			cmd_buf_mc_addr;
	struct psp_gfx_cmd_resp		*cmd_buf_mem;

	/* fence value associated with cmd buffer */
	atomic_t			fence_value;
	/* flag to mark whether gfx fw autoload is supported or not */
	bool				autoload_supported;
	/* flag to mark whether df cstate management centralized to PMFW */
	bool				pmfw_centralized_cstate_management;

	/* xgmi ta firmware and buffer */
	const struct firmware		*ta_fw;
	uint32_t			ta_fw_version;

	uint32_t			cap_fw_version;
	uint32_t			cap_feature_version;
	uint32_t			cap_ucode_size;

	struct ta_context		asd_context;
	struct psp_xgmi_context		xgmi_context;
	struct psp_ras_context		ras_context;
	struct ta_cp_context		hdcp_context;
	struct ta_cp_context		dtm_context;
	struct ta_cp_context		rap_context;
	struct ta_cp_context		securedisplay_context;
	struct mutex			mutex;
	struct psp_memory_training_context mem_train_ctx;

	uint32_t			boot_cfg_bitmask;

	char *vbflash_tmp_buf;
	size_t vbflash_image_size;
	bool vbflash_done;
};

struct amdgpu_psp_funcs {
	bool (*check_fw_loading_status)(struct amdgpu_device *adev,
					enum AMDGPU_UCODE_ID);
};


#define psp_ring_init(psp, type) (psp)->funcs->ring_init((psp), (type))
#define psp_ring_create(psp, type) (psp)->funcs->ring_create((psp), (type))
#define psp_ring_stop(psp, type) (psp)->funcs->ring_stop((psp), (type))
#define psp_ring_destroy(psp, type) ((psp)->funcs->ring_destroy((psp), (type)))
#define psp_init_microcode(psp) \
		((psp)->funcs->init_microcode ? (psp)->funcs->init_microcode((psp)) : 0)
#define psp_bootloader_load_kdb(psp) \
		((psp)->funcs->bootloader_load_kdb ? (psp)->funcs->bootloader_load_kdb((psp)) : 0)
#define psp_bootloader_load_spl(psp) \
		((psp)->funcs->bootloader_load_spl ? (psp)->funcs->bootloader_load_spl((psp)) : 0)
#define psp_bootloader_load_sysdrv(psp) \
		((psp)->funcs->bootloader_load_sysdrv ? (psp)->funcs->bootloader_load_sysdrv((psp)) : 0)
#define psp_bootloader_load_soc_drv(psp) \
		((psp)->funcs->bootloader_load_soc_drv ? (psp)->funcs->bootloader_load_soc_drv((psp)) : 0)
#define psp_bootloader_load_intf_drv(psp) \
		((psp)->funcs->bootloader_load_intf_drv ? (psp)->funcs->bootloader_load_intf_drv((psp)) : 0)
#define psp_bootloader_load_dbg_drv(psp) \
		((psp)->funcs->bootloader_load_dbg_drv ? (psp)->funcs->bootloader_load_dbg_drv((psp)) : 0)
#define psp_bootloader_load_ras_drv(psp) \
		((psp)->funcs->bootloader_load_ras_drv ? \
		(psp)->funcs->bootloader_load_ras_drv((psp)) : 0)
#define psp_bootloader_load_sos(psp) \
		((psp)->funcs->bootloader_load_sos ? (psp)->funcs->bootloader_load_sos((psp)) : 0)
#define psp_smu_reload_quirk(psp) \
		((psp)->funcs->smu_reload_quirk ? (psp)->funcs->smu_reload_quirk((psp)) : false)
#define psp_mode1_reset(psp) \
		((psp)->funcs->mode1_reset ? (psp)->funcs->mode1_reset((psp)) : false)
#define psp_mem_training(psp, ops) \
	((psp)->funcs->mem_training ? (psp)->funcs->mem_training((psp), (ops)) : 0)

#define psp_ring_get_wptr(psp) (psp)->funcs->ring_get_wptr((psp))
#define psp_ring_set_wptr(psp, value) (psp)->funcs->ring_set_wptr((psp), (value))

#define psp_load_usbc_pd_fw(psp, fw_pri_mc_addr) \
	((psp)->funcs->load_usbc_pd_fw ? \
	(psp)->funcs->load_usbc_pd_fw((psp), (fw_pri_mc_addr)) : -EINVAL)

#define psp_read_usbc_pd_fw(psp, fw_ver) \
	((psp)->funcs->read_usbc_pd_fw ? \
	(psp)->funcs->read_usbc_pd_fw((psp), fw_ver) : -EINVAL)

#define psp_update_spirom(psp, fw_pri_mc_addr) \
	((psp)->funcs->update_spirom ? \
	(psp)->funcs->update_spirom((psp), fw_pri_mc_addr) : -EINVAL)

#define psp_vbflash_status(psp) \
	((psp)->funcs->vbflash_stat ? \
	(psp)->funcs->vbflash_stat((psp)) : -EINVAL)

extern const struct amd_ip_funcs psp_ip_funcs;

extern const struct amdgpu_ip_block_version psp_v3_1_ip_block;
extern const struct amdgpu_ip_block_version psp_v10_0_ip_block;
extern const struct amdgpu_ip_block_version psp_v11_0_ip_block;
extern const struct amdgpu_ip_block_version psp_v11_0_8_ip_block;
extern const struct amdgpu_ip_block_version psp_v12_0_ip_block;
extern const struct amdgpu_ip_block_version psp_v13_0_ip_block;
extern const struct amdgpu_ip_block_version psp_v13_0_4_ip_block;

extern int psp_wait_for(struct psp_context *psp, uint32_t reg_index,
			uint32_t field_val, uint32_t mask, bool check_changed);

int psp_gpu_reset(struct amdgpu_device *adev);
int psp_update_vcn_sram(struct amdgpu_device *adev, int inst_idx,
			uint64_t cmd_gpu_addr, int cmd_size);

int psp_ta_init_shared_buf(struct psp_context *psp,
				  struct ta_mem_context *mem_ctx);
void psp_ta_free_shared_buf(struct ta_mem_context *mem_ctx);
int psp_ta_unload(struct psp_context *psp, struct ta_context *context);
int psp_ta_load(struct psp_context *psp, struct ta_context *context);
int psp_ta_invoke(struct psp_context *psp,
			uint32_t ta_cmd_id,
			struct ta_context *context);
int psp_ta_invoke_indirect(struct psp_context *psp,
		  uint32_t ta_cmd_id,
		  struct ta_context *context);

int psp_xgmi_initialize(struct psp_context *psp, bool set_extended_data, bool load_ta);
int psp_xgmi_terminate(struct psp_context *psp);
int psp_xgmi_invoke(struct psp_context *psp, uint32_t ta_cmd_id);
int psp_xgmi_get_hive_id(struct psp_context *psp, uint64_t *hive_id);
int psp_xgmi_get_node_id(struct psp_context *psp, uint64_t *node_id);
int psp_xgmi_get_topology_info(struct psp_context *psp,
			       int number_devices,
			       struct psp_xgmi_topology_info *topology,
			       bool get_extended_data);
int psp_xgmi_set_topology_info(struct psp_context *psp,
			       int number_devices,
			       struct psp_xgmi_topology_info *topology);

int psp_ras_invoke(struct psp_context *psp, uint32_t ta_cmd_id);
int psp_ras_enable_features(struct psp_context *psp,
		union ta_ras_cmd_input *info, bool enable);
int psp_ras_trigger_error(struct psp_context *psp,
			  struct ta_ras_trigger_error_input *info);
int psp_ras_terminate(struct psp_context *psp);

int psp_hdcp_invoke(struct psp_context *psp, uint32_t ta_cmd_id);
int psp_dtm_invoke(struct psp_context *psp, uint32_t ta_cmd_id);
int psp_rap_invoke(struct psp_context *psp, uint32_t ta_cmd_id, enum ta_rap_status *status);
int psp_securedisplay_invoke(struct psp_context *psp, uint32_t ta_cmd_id);

int psp_rlc_autoload_start(struct psp_context *psp);

int psp_reg_program(struct psp_context *psp, enum psp_reg_prog_id reg,
		uint32_t value);
int psp_ring_cmd_submit(struct psp_context *psp,
			uint64_t cmd_buf_mc_addr,
			uint64_t fence_mc_addr,
			int index);
int psp_init_asd_microcode(struct psp_context *psp,
			   const char *chip_name);
int psp_init_toc_microcode(struct psp_context *psp,
			   const char *chip_name);
int psp_init_sos_microcode(struct psp_context *psp,
			   const char *chip_name);
int psp_init_ta_microcode(struct psp_context *psp,
			  const char *chip_name);
int psp_init_cap_microcode(struct psp_context *psp,
			  const char *chip_name);
int psp_get_fw_attestation_records_addr(struct psp_context *psp,
					uint64_t *output_ptr);

int psp_load_fw_list(struct psp_context *psp,
		     struct amdgpu_firmware_info **ucode_list, int ucode_count);
void psp_copy_fw(struct psp_context *psp, uint8_t *start_addr, uint32_t bin_size);

int is_psp_fw_valid(struct psp_bin_desc bin);

int amdgpu_psp_sysfs_init(struct amdgpu_device *adev);
void amdgpu_psp_sysfs_fini(struct amdgpu_device *adev);
#endif
