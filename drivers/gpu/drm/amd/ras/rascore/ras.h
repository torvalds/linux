/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef __RAS_H__
#define __RAS_H__
#include "ras_sys.h"
#include "ras_umc.h"
#include "ras_aca.h"
#include "ras_eeprom.h"
#include "ras_core_status.h"
#include "ras_process.h"
#include "ras_gfx.h"
#include "ras_cmd.h"
#include "ras_nbio.h"
#include "ras_mp1.h"
#include "ras_psp.h"
#include "ras_log_ring.h"

#define RAS_HW_ERR		"[Hardware Error]: "

#define RAS_GPU_PAGE_SHIFT  12
#define RAS_ADDR_TO_PFN(addr) ((addr) >> RAS_GPU_PAGE_SHIFT)
#define RAS_PFN_TO_ADDR(pfn) ((pfn) << RAS_GPU_PAGE_SHIFT)

#define RAS_CORE_RESET_GPU 0x10000

#define GPU_RESET_CAUSE_POISON  (RAS_CORE_RESET_GPU | 0x0001)
#define GPU_RESET_CAUSE_FATAL   (RAS_CORE_RESET_GPU | 0x0002)
#define GPU_RESET_CAUSE_RMA     (RAS_CORE_RESET_GPU | 0x0004)

enum ras_block_id {
	RAS_BLOCK_ID__UMC = 0,
	RAS_BLOCK_ID__SDMA,
	RAS_BLOCK_ID__GFX,
	RAS_BLOCK_ID__MMHUB,
	RAS_BLOCK_ID__ATHUB,
	RAS_BLOCK_ID__PCIE_BIF,
	RAS_BLOCK_ID__HDP,
	RAS_BLOCK_ID__XGMI_WAFL,
	RAS_BLOCK_ID__DF,
	RAS_BLOCK_ID__SMN,
	RAS_BLOCK_ID__SEM,
	RAS_BLOCK_ID__MP0,
	RAS_BLOCK_ID__MP1,
	RAS_BLOCK_ID__FUSE,
	RAS_BLOCK_ID__MCA,
	RAS_BLOCK_ID__VCN,
	RAS_BLOCK_ID__JPEG,
	RAS_BLOCK_ID__IH,
	RAS_BLOCK_ID__MPIO,

	RAS_BLOCK_ID__LAST
};

enum ras_ecc_err_type {
	RAS_ECC_ERR__NONE                = 0,
	RAS_ECC_ERR__PARITY              = 1,
	RAS_ECC_ERR__SINGLE_CORRECTABLE  = 2,
	RAS_ECC_ERR__MULTI_UNCORRECTABLE = 4,
	RAS_ECC_ERR__POISON              = 8,
};

enum ras_err_type {
	RAS_ERR_TYPE__UE = 0,
	RAS_ERR_TYPE__CE,
	RAS_ERR_TYPE__DE,
	RAS_ERR_TYPE__LAST
};

enum ras_seqno_type {
	RAS_SEQNO_TYPE_INVALID = 0,
	RAS_SEQNO_TYPE_UE,
	RAS_SEQNO_TYPE_CE,
	RAS_SEQNO_TYPE_DE,
	RAS_SEQNO_TYPE_POISON_CONSUMPTION,
	RAS_SEQNO_TYPE_COUNT_MAX,
};

enum ras_seqno_fifo {
	SEQNO_FIFO_INVALID = 0,
	SEQNO_FIFO_POISON_CREATION,
	SEQNO_FIFO_POISON_CONSUMPTION,
	SEQNO_FIFO_COUNT_MAX
};

enum ras_notify_event {
	RAS_EVENT_ID__NONE,
	RAS_EVENT_ID__BAD_PAGE_DETECTED,
	RAS_EVENT_ID__POISON_CONSUMPTION,
	RAS_EVENT_ID__RESERVE_BAD_PAGE,
	RAS_EVENT_ID__DEVICE_RMA,
	RAS_EVENT_ID__UPDATE_BAD_PAGE_NUM,
	RAS_EVENT_ID__UPDATE_BAD_CHANNEL_BITMAP,
	RAS_EVENT_ID__FATAL_ERROR_DETECTED,
	RAS_EVENT_ID__RESET_GPU,
	RAS_EVENT_ID__RESET_VF,
};

enum ras_gpu_status {
	RAS_GPU_STATUS__NOT_READY = 0,
	RAS_GPU_STATUS__READY = 0x1,
	RAS_GPU_STATUS__IN_RESET = 0x2,
	RAS_GPU_STATUS__IS_RMA = 0x4,
	RAS_GPU_STATUS__IS_VF = 0x8,
};

struct ras_core_context;
struct ras_bank_ecc;
struct ras_umc;
struct ras_aca;
struct ras_process;
struct ras_nbio;
struct ras_log_ring;
struct ras_psp;

struct ras_mp1_sys_func {
	int (*mp1_get_valid_bank_count)(struct ras_core_context *ras_core,
			u32 msg, u32 *count);
	int (*mp1_dump_valid_bank)(struct ras_core_context *ras_core,
			u32 msg, u32 idx, u32 reg_idx, u64 *val);
};

struct ras_eeprom_sys_func {
	int (*eeprom_i2c_xfer)(struct ras_core_context *ras_core,
			u32 eeprom_addr, u8 *eeprom_buf, u32 buf_size, bool read);
	int (*update_eeprom_i2c_config)(struct ras_core_context *ras_core);
};

struct ras_nbio_sys_func {
	int (*set_ras_controller_irq_state)(struct ras_core_context *ras_core,
			bool state);
	int (*set_ras_err_event_athub_irq_state)(struct ras_core_context *ras_core,
			bool state);
};

struct ras_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	long tm_year;
};

struct device_system_info {
	uint32_t device_id;
	uint32_t vendor_id;
	uint32_t socket_id;
};

enum gpu_mem_type {
	GPU_MEM_TYPE_DEFAULT,
	GPU_MEM_TYPE_RAS_PSP_RING,
	GPU_MEM_TYPE_RAS_PSP_CMD,
	GPU_MEM_TYPE_RAS_PSP_FENCE,
	GPU_MEM_TYPE_RAS_TA_FW,
	GPU_MEM_TYPE_RAS_TA_CMD,
};

struct ras_psp_sys_func {
	int (*get_ras_psp_system_status)(struct ras_core_context *ras_core,
		struct ras_psp_sys_status *status);
	int (*get_ras_ta_init_param)(struct ras_core_context *ras_core,
		struct ras_ta_init_param *ras_ta_param);
};

struct ras_sys_func {
	int (*gpu_reset_lock)(struct ras_core_context *ras_core,
			bool down, bool try);
	int (*check_gpu_status)(struct ras_core_context *ras_core,
			uint32_t *status);
	int (*gen_seqno)(struct ras_core_context *ras_core,
			enum ras_seqno_type seqno_type, uint64_t *seqno);
	int (*async_handle_ras_event)(struct ras_core_context *ras_core, void *data);
	int (*ras_notifier)(struct ras_core_context *ras_core,
		    enum ras_notify_event event_id, void *data);
	u64 (*get_utc_second_timestamp)(struct ras_core_context *ras_core);
	int (*get_device_system_info)(struct ras_core_context *ras_core,
			struct device_system_info *dev_info);
	bool (*detect_ras_interrupt)(struct ras_core_context *ras_core);
	int (*get_gpu_mem)(struct ras_core_context *ras_core,
		enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem);
	int (*put_gpu_mem)(struct ras_core_context *ras_core,
		enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem);
};

struct ras_ecc_count {
	uint64_t new_ce_count;
	uint64_t total_ce_count;
	uint64_t new_ue_count;
	uint64_t total_ue_count;
	uint64_t new_de_count;
	uint64_t total_de_count;
};

struct ras_bank_ecc {
	uint32_t nps;
	uint64_t seq_no;
	uint64_t status;
	uint64_t ipid;
	uint64_t addr;
};

struct ras_bank_ecc_node {
	struct list_head node;
	struct ras_bank_ecc ecc;
};

struct ras_aca_config {
	u32 socket_num_per_hive;
	u32 aid_num_per_socket;
	u32 xcd_num_per_aid;
};

struct ras_mp1_config {
	const struct ras_mp1_sys_func *mp1_sys_fn;
};

struct ras_nbio_config {
	const struct ras_nbio_sys_func *nbio_sys_fn;
};

struct ras_psp_config {
	const struct ras_psp_sys_func *psp_sys_fn;
};

struct ras_umc_config {
	uint32_t umc_vram_type;
};

struct ras_eeprom_config {
	const struct ras_eeprom_sys_func *eeprom_sys_fn;
	int eeprom_record_threshold_config;
	uint32_t eeprom_record_threshold_count;
	void *eeprom_i2c_adapter;
	u32 eeprom_i2c_addr;
	u32 eeprom_i2c_port;
	u16 max_i2c_read_len;
	u16 max_i2c_write_len;
};

struct ras_core_config {
	u32 aca_ip_version;
	u32 umc_ip_version;
	u32 mp1_ip_version;
	u32 gfx_ip_version;
	u32 nbio_ip_version;
	u32 psp_ip_version;

	bool poison_supported;
	bool ras_eeprom_supported;
	const struct ras_sys_func *sys_fn;

	struct ras_aca_config aca_cfg;
	struct ras_mp1_config mp1_cfg;
	struct ras_nbio_config nbio_cfg;
	struct ras_psp_config psp_cfg;
	struct ras_eeprom_config eeprom_cfg;
	struct ras_umc_config umc_cfg;
};

struct ras_core_context {
	void *dev;
	struct ras_core_config *config;
	u32 socket_num_per_hive;
	u32 aid_num_per_socket;
	u32 xcd_num_per_aid;
	int max_ue_banks_per_query;
	int max_ce_banks_per_query;
	struct ras_aca ras_aca;

	bool ras_eeprom_supported;
	struct ras_eeprom_control ras_eeprom;

	struct ras_psp ras_psp;
	struct ras_umc ras_umc;
	struct ras_nbio ras_nbio;
	struct ras_gfx ras_gfx;
	struct ras_mp1 ras_mp1;
	struct ras_process ras_proc;
	struct ras_cmd_mgr ras_cmd;
	struct ras_log_ring ras_log_ring;

	const struct ras_sys_func *sys_fn;

	/* is poison mode supported */
	bool poison_supported;

	bool is_rma;
	bool is_initialized;

	struct kfifo de_seqno_fifo;
	struct kfifo consumption_seqno_fifo;
	spinlock_t seqno_lock;

	bool ras_core_enabled;
};

struct ras_core_context *ras_core_create(struct ras_core_config *init_config);
void ras_core_destroy(struct ras_core_context *ras_core);
int ras_core_sw_init(struct ras_core_context *ras_core);
int ras_core_sw_fini(struct ras_core_context *ras_core);
int ras_core_hw_init(struct ras_core_context *ras_core);
int ras_core_hw_fini(struct ras_core_context *ras_core);
bool ras_core_is_ready(struct ras_core_context *ras_core);
uint64_t ras_core_gen_seqno(struct ras_core_context *ras_core,
			enum ras_seqno_type seqno_type);
uint64_t ras_core_get_seqno(struct ras_core_context *ras_core,
			enum ras_seqno_type seqno_type, bool pop);

int ras_core_put_seqno(struct ras_core_context *ras_core,
		enum ras_seqno_type seqno_type, uint64_t seqno);

int ras_core_update_ecc_info(struct ras_core_context *ras_core);
int ras_core_query_block_ecc_data(struct ras_core_context *ras_core,
		enum ras_block_id block, struct ras_ecc_count *ecc_count);

bool ras_core_gpu_in_reset(struct ras_core_context *ras_core);
bool ras_core_gpu_is_rma(struct ras_core_context *ras_core);
bool ras_core_gpu_is_vf(struct ras_core_context *ras_core);
bool ras_core_handle_nbio_irq(struct ras_core_context *ras_core, void *data);
int ras_core_handle_fatal_error(struct ras_core_context *ras_core);

uint32_t ras_core_get_curr_nps_mode(struct ras_core_context *ras_core);
const char *ras_core_get_ras_block_name(enum ras_block_id block_id);
int ras_core_convert_timestamp_to_time(struct ras_core_context *ras_core,
			uint64_t timestamp, struct ras_time *tm);

int ras_core_set_status(struct ras_core_context *ras_core, bool enable);
bool ras_core_is_enabled(struct ras_core_context *ras_core);
uint64_t ras_core_get_utc_second_timestamp(struct ras_core_context *ras_core);
int ras_core_translate_soc_pa_and_bank(struct ras_core_context *ras_core,
	uint64_t *soc_pa, struct umc_bank_addr *bank_addr, bool bank_to_pa);
bool ras_core_ras_interrupt_detected(struct ras_core_context *ras_core);
int ras_core_get_gpu_mem(struct ras_core_context *ras_core,
		enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem);
int ras_core_put_gpu_mem(struct ras_core_context *ras_core,
		enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem);
bool ras_core_check_safety_watermark(struct ras_core_context *ras_core);
int ras_core_down_trylock_gpu_reset_lock(struct ras_core_context *ras_core);
void ras_core_down_gpu_reset_lock(struct ras_core_context *ras_core);
void ras_core_up_gpu_reset_lock(struct ras_core_context *ras_core);
int ras_core_event_notify(struct ras_core_context *ras_core,
		enum ras_notify_event event_id, void *data);
int ras_core_get_device_system_info(struct ras_core_context *ras_core,
		struct device_system_info *dev_info);
#endif
