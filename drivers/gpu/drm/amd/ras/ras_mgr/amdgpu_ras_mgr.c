// SPDX-License-Identifier: MIT
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
#include "amdgpu.h"
#include "amdgpu_reset.h"
#include "amdgpu_xgmi.h"
#include "ras_sys.h"
#include "amdgpu_ras_mgr.h"
#include "amdgpu_ras_cmd.h"
#include "amdgpu_virt_ras_cmd.h"
#include "amdgpu_ras_process.h"
#include "amdgpu_ras_eeprom_i2c.h"
#include "amdgpu_ras_mp1_v13_0.h"
#include "amdgpu_ras_nbio_v7_9.h"

#define MAX_SOCKET_NUM_PER_HIVE		8
#define MAX_AID_NUM_PER_SOCKET		4
#define MAX_XCD_NUM_PER_AID			2

/* typical ECC bad page rate is 1 bad page per 100MB VRAM */
#define TYPICAL_ECC_BAD_PAGE_RATE (100ULL * SZ_1M)

#define COUNT_BAD_PAGE_THRESHOLD(size) (((size) >> 21) << 4)

/* Reserve 8 physical dram row for possible retirement.
 * In worst cases, it will lose 8 * 2MB memory in vram domain
 */
#define RAS_RESERVED_VRAM_SIZE_DEFAULT	(16ULL << 20)


static void ras_mgr_init_event_mgr(struct ras_event_manager *mgr)
{
	struct ras_event_state *event_state;
	int i;

	memset(mgr, 0, sizeof(*mgr));
	atomic64_set(&mgr->seqno, 0);

	for (i = 0; i < ARRAY_SIZE(mgr->event_state); i++) {
		event_state = &mgr->event_state[i];
		event_state->last_seqno = RAS_EVENT_INVALID_ID;
		atomic64_set(&event_state->count, 0);
	}
}

static void amdgpu_ras_mgr_init_event_mgr(struct ras_core_context *ras_core)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct ras_event_manager *event_mgr;
	struct amdgpu_hive_info *hive;

	hive = amdgpu_get_xgmi_hive(adev);
	event_mgr = hive ? &hive->event_mgr : &ras_mgr->ras_event_mgr;

	/* init event manager with node 0 on xgmi system */
	if (!amdgpu_reset_in_recovery(adev)) {
		if (!hive || adev->gmc.xgmi.node_id == 0)
			ras_mgr_init_event_mgr(event_mgr);
	}

	if (hive)
		amdgpu_put_xgmi_hive(hive);
}

static int amdgpu_ras_mgr_init_aca_config(struct amdgpu_device *adev,
		struct ras_core_config *config)
{
	struct ras_aca_config *aca_cfg = &config->aca_cfg;

	aca_cfg->socket_num_per_hive = MAX_SOCKET_NUM_PER_HIVE;
	aca_cfg->aid_num_per_socket = MAX_AID_NUM_PER_SOCKET;
	aca_cfg->xcd_num_per_aid = MAX_XCD_NUM_PER_AID;

	return 0;
}

static int amdgpu_ras_mgr_init_eeprom_config(struct amdgpu_device *adev,
		struct ras_core_config *config)
{
	struct ras_eeprom_config *eeprom_cfg = &config->eeprom_cfg;

	eeprom_cfg->eeprom_sys_fn = &amdgpu_ras_eeprom_i2c_sys_func;
	eeprom_cfg->eeprom_i2c_adapter = adev->pm.ras_eeprom_i2c_bus;
	if (eeprom_cfg->eeprom_i2c_adapter) {
		const struct i2c_adapter_quirks *quirks =
			((struct i2c_adapter *)eeprom_cfg->eeprom_i2c_adapter)->quirks;

		if (quirks) {
			eeprom_cfg->max_i2c_read_len = quirks->max_read_len;
			eeprom_cfg->max_i2c_write_len = quirks->max_write_len;
		}
	}

	/*
	 * amdgpu_bad_page_threshold is used to config
	 * the threshold for the number of bad pages.
	 * -1:  Threshold is set to default value
	 *      Driver will issue a warning message when threshold is reached
	 *      and continue runtime services.
	 * 0:   Disable bad page retirement
	 *      Driver will not retire bad pages
	 *      which is intended for debugging purpose.
	 * -2:  Threshold is determined by a formula
	 *      that assumes 1 bad page per 100M of local memory.
	 *      Driver will continue runtime services when threhold is reached.
	 * 0 < threshold < max number of bad page records in EEPROM,
	 *      A user-defined threshold is set
	 *      Driver will halt runtime services when this custom threshold is reached.
	 */
	if (amdgpu_bad_page_threshold == NONSTOP_OVER_THRESHOLD)
		eeprom_cfg->eeprom_record_threshold_count =
			div64_u64(adev->gmc.mc_vram_size, TYPICAL_ECC_BAD_PAGE_RATE);
	else if (amdgpu_bad_page_threshold == WARN_NONSTOP_OVER_THRESHOLD)
		eeprom_cfg->eeprom_record_threshold_count =
				COUNT_BAD_PAGE_THRESHOLD(RAS_RESERVED_VRAM_SIZE_DEFAULT);
	else
		eeprom_cfg->eeprom_record_threshold_count = amdgpu_bad_page_threshold;

	eeprom_cfg->eeprom_record_threshold_config = amdgpu_bad_page_threshold;

	return 0;
}

static int amdgpu_ras_mgr_init_mp1_config(struct amdgpu_device *adev,
		struct ras_core_config *config)
{
	struct ras_mp1_config *mp1_cfg = &config->mp1_cfg;
	int ret = 0;

	switch (config->mp1_ip_version) {
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
	case IP_VERSION(13, 0, 12):
		mp1_cfg->mp1_sys_fn = &amdgpu_ras_mp1_sys_func_v13_0;
		break;
	default:
		RAS_DEV_ERR(adev,
			"The mp1(0x%x) ras config is not right!\n",
			config->mp1_ip_version);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int amdgpu_ras_mgr_init_nbio_config(struct amdgpu_device *adev,
		struct ras_core_config *config)
{
	struct ras_nbio_config *nbio_cfg = &config->nbio_cfg;
	int ret = 0;

	switch (config->nbio_ip_version) {
	case IP_VERSION(7, 9, 0):
	case IP_VERSION(7, 9, 1):
		nbio_cfg->nbio_sys_fn = &amdgpu_ras_nbio_sys_func_v7_9;
		break;
	default:
		RAS_DEV_ERR(adev,
			"The nbio(0x%x) ras config is not right!\n",
			config->nbio_ip_version);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int amdgpu_ras_mgr_get_ras_psp_system_status(struct ras_core_context *ras_core,
			struct ras_psp_sys_status *status)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct ta_context *context = &adev->psp.ras_context.context;

	status->initialized = context->initialized;
	status->session_id = context->session_id;
	status->psp_cmd_mutex = &adev->psp.mutex;

	return 0;
}

static int amdgpu_ras_mgr_get_ras_ta_init_param(struct ras_core_context *ras_core,
	struct ras_ta_init_param *ras_ta_param)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	uint32_t nps_mode;

	if (amdgpu_ras_is_poison_mode_supported(adev))
		ras_ta_param->poison_mode_en = 1;

	if (!adev->gmc.xgmi.connected_to_cpu && !adev->gmc.is_app_apu)
		ras_ta_param->dgpu_mode = 1;

	ras_ta_param->xcc_mask = adev->gfx.xcc_mask;
	ras_ta_param->channel_dis_num = hweight32(adev->gmc.m_half_use) * 2;

	ras_ta_param->active_umc_mask = adev->umc.active_mask;
	ras_ta_param->vram_type = (uint8_t)adev->gmc.vram_type;

	if (!amdgpu_ras_mgr_get_curr_nps_mode(adev, &nps_mode))
		ras_ta_param->nps_mode = nps_mode;

	return 0;
}

const struct ras_psp_sys_func amdgpu_ras_psp_sys_func = {
	.get_ras_psp_system_status = amdgpu_ras_mgr_get_ras_psp_system_status,
	.get_ras_ta_init_param = amdgpu_ras_mgr_get_ras_ta_init_param,
};

static int amdgpu_ras_mgr_init_psp_config(struct amdgpu_device *adev,
	struct ras_core_config *config)
{
	struct ras_psp_config *psp_cfg = &config->psp_cfg;

	psp_cfg->psp_sys_fn = &amdgpu_ras_psp_sys_func;

	return 0;
}

static int amdgpu_ras_mgr_init_umc_config(struct amdgpu_device *adev,
	struct ras_core_config *config)
{
	struct ras_umc_config *umc_cfg = &config->umc_cfg;

	umc_cfg->umc_vram_type = adev->gmc.vram_type;

	return 0;
}

static struct ras_core_context *amdgpu_ras_mgr_create_ras_core(struct amdgpu_device *adev)
{
	struct ras_core_config init_config;

	memset(&init_config, 0, sizeof(init_config));

	init_config.umc_ip_version = amdgpu_ip_version(adev, UMC_HWIP, 0);
	init_config.mp1_ip_version = amdgpu_ip_version(adev, MP1_HWIP, 0);
	init_config.gfx_ip_version = amdgpu_ip_version(adev, GC_HWIP, 0);
	init_config.nbio_ip_version = amdgpu_ip_version(adev, NBIO_HWIP, 0);
	init_config.psp_ip_version = amdgpu_ip_version(adev, MP1_HWIP, 0);

	if (init_config.umc_ip_version == IP_VERSION(12, 0, 0) ||
	    init_config.umc_ip_version == IP_VERSION(12, 5, 0))
		init_config.aca_ip_version = IP_VERSION(1, 0, 0);

	init_config.sys_fn = &amdgpu_ras_sys_fn;
	init_config.ras_eeprom_supported = true;
	init_config.poison_supported =
		amdgpu_ras_is_poison_mode_supported(adev);

	amdgpu_ras_mgr_init_aca_config(adev, &init_config);
	amdgpu_ras_mgr_init_eeprom_config(adev, &init_config);
	amdgpu_ras_mgr_init_mp1_config(adev, &init_config);
	amdgpu_ras_mgr_init_nbio_config(adev, &init_config);
	amdgpu_ras_mgr_init_psp_config(adev, &init_config);
	amdgpu_ras_mgr_init_umc_config(adev, &init_config);

	return ras_core_create(&init_config);
}

static int amdgpu_ras_mgr_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct amdgpu_ras_mgr *ras_mgr;
	int ret = 0;

	/* Disabled by default */
	con->uniras_enabled = false;

	/* Enabled only in debug mode */
	if (adev->debug_enable_ras_aca) {
		con->uniras_enabled = true;
		RAS_DEV_INFO(adev, "Debug amdgpu uniras!");
	}

	if (!con->uniras_enabled)
		return 0;

	ras_mgr = kzalloc(sizeof(*ras_mgr), GFP_KERNEL);
	if (!ras_mgr)
		return -EINVAL;

	con->ras_mgr = ras_mgr;
	ras_mgr->adev = adev;

	ras_mgr->ras_core = amdgpu_ras_mgr_create_ras_core(adev);
	if (!ras_mgr->ras_core) {
		RAS_DEV_ERR(adev, "Failed to create ras core!\n");
		ret = -EINVAL;
		goto err;
	}

	ras_mgr->ras_core->dev = adev;

	amdgpu_ras_process_init(adev);
	ras_core_sw_init(ras_mgr->ras_core);
	amdgpu_ras_mgr_init_event_mgr(ras_mgr->ras_core);

	if (amdgpu_sriov_vf(adev)) {
		ret = amdgpu_virt_ras_sw_init(adev);
		if (ret) {
			RAS_DEV_ERR(adev,
				"Virt ras sw_init failed! ret:%d\n", ret);
			goto err;
		}
	}

	return 0;

err:
	kfree(ras_mgr);
	return ret;
}

static int amdgpu_ras_mgr_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct amdgpu_ras_mgr *ras_mgr = (struct amdgpu_ras_mgr *)con->ras_mgr;

	if (!con->uniras_enabled)
		return 0;

	if (!ras_mgr)
		return 0;

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_ras_sw_fini(adev);

	amdgpu_ras_process_fini(adev);
	ras_core_sw_fini(ras_mgr->ras_core);
	ras_core_destroy(ras_mgr->ras_core);
	ras_mgr->ras_core = NULL;

	kfree(con->ras_mgr);
	con->ras_mgr = NULL;

	return 0;
}

static int amdgpu_ras_mgr_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	int ret;

	if (!con->uniras_enabled)
		return 0;

	if (!ras_mgr || !ras_mgr->ras_core)
		return -EINVAL;

	if (amdgpu_sriov_vf(adev))
		ret = amdgpu_virt_ras_hw_init(adev);
	else
		ret = ras_core_hw_init(ras_mgr->ras_core);

	if (ret) {
		RAS_DEV_ERR(adev, "Failed to initialize hw_init!, ret:%d\n", ret);
		return ret;
	}

	ras_mgr->ras_is_ready = true;

	amdgpu_enable_uniras(adev, true);

	RAS_DEV_INFO(adev, "AMDGPU RAS Is Ready.\n");
	return 0;
}

static int amdgpu_ras_mgr_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!con->uniras_enabled)
		return 0;

	if (!ras_mgr || !ras_mgr->ras_core)
		return -EINVAL;

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_ras_hw_fini(adev);
	else
		ras_core_hw_fini(ras_mgr->ras_core);

	ras_mgr->ras_is_ready = false;

	return 0;
}

struct amdgpu_ras_mgr *amdgpu_ras_mgr_get_context(struct amdgpu_device *adev)
{
	if (!adev || !adev->psp.ras_context.ras)
		return NULL;

	return (struct amdgpu_ras_mgr *)adev->psp.ras_context.ras->ras_mgr;
}

static const struct amd_ip_funcs __maybe_unused ras_v1_0_ip_funcs = {
	.name = "ras_v1_0",
	.sw_init = amdgpu_ras_mgr_sw_init,
	.sw_fini = amdgpu_ras_mgr_sw_fini,
	.hw_init = amdgpu_ras_mgr_hw_init,
	.hw_fini = amdgpu_ras_mgr_hw_fini,
};

const struct amdgpu_ip_block_version ras_v1_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_RAS,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &ras_v1_0_ip_funcs,
};

int amdgpu_enable_uniras(struct amdgpu_device *adev, bool enable)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!ras_mgr || !ras_mgr->ras_core)
		return -EPERM;

	RAS_DEV_INFO(adev, "Enable amdgpu unified ras!");
	return ras_core_set_status(ras_mgr->ras_core, enable);
}

bool amdgpu_uniras_enabled(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (amdgpu_sriov_vf(adev))
		return amdgpu_virt_ras_remote_uniras_enabled(adev);

	if (!ras_mgr || !ras_mgr->ras_core)
		return false;

	return ras_core_is_enabled(ras_mgr->ras_core);
}

static bool amdgpu_ras_mgr_is_ready(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (ras_mgr && ras_mgr->ras_core && ras_mgr->ras_is_ready &&
	    ras_core_is_ready(ras_mgr->ras_core))
		return true;

	return false;
}

int amdgpu_ras_mgr_handle_fatal_interrupt(struct amdgpu_device *adev, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!amdgpu_ras_mgr_is_ready(adev))
		return -EPERM;

	return ras_core_handle_nbio_irq(ras_mgr->ras_core, data);
}

uint64_t amdgpu_ras_mgr_gen_ras_event_seqno(struct amdgpu_device *adev,
			enum ras_seqno_type seqno_type)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	int ret;
	uint64_t seq_no;

	if (!amdgpu_ras_mgr_is_ready(adev) ||
	    (seqno_type >= RAS_SEQNO_TYPE_COUNT_MAX))
		return 0;

	seq_no = ras_core_gen_seqno(ras_mgr->ras_core, seqno_type);

	if ((seqno_type == RAS_SEQNO_TYPE_DE) ||
	    (seqno_type == RAS_SEQNO_TYPE_POISON_CONSUMPTION)) {
		ret = ras_core_put_seqno(ras_mgr->ras_core, seqno_type, seq_no);
		if (ret)
			RAS_DEV_WARN(adev, "There are too many ras interrupts!");
	}

	return seq_no;
}

int amdgpu_ras_mgr_handle_controller_interrupt(struct amdgpu_device *adev, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct ras_ih_info *ih_info = (struct ras_ih_info *)data;
	uint64_t seq_no = 0;
	int ret = 0;

	if (!amdgpu_ras_mgr_is_ready(adev))
		return -EPERM;

	if (ih_info && (ih_info->block == AMDGPU_RAS_BLOCK__UMC)) {
		if (ras_mgr->ras_core->poison_supported) {
			seq_no = amdgpu_ras_mgr_gen_ras_event_seqno(adev, RAS_SEQNO_TYPE_DE);
			RAS_DEV_INFO(adev,
				"{%llu} RAS poison is created, no user action is needed.\n",
				seq_no);
		}

		ret = amdgpu_ras_process_handle_umc_interrupt(adev, ih_info);
	} else if (ras_mgr->ras_core->poison_supported) {
		ret = amdgpu_ras_process_handle_unexpected_interrupt(adev, ih_info);
	} else {
		RAS_DEV_WARN(adev,
			"No RAS interrupt handler for non-UMC block with poison disabled.\n");
	}

	return ret;
}

int amdgpu_ras_mgr_handle_consumer_interrupt(struct amdgpu_device *adev, void *data)
{
	if (!amdgpu_ras_mgr_is_ready(adev))
		return -EPERM;

	return amdgpu_ras_process_handle_consumption_interrupt(adev, data);
}

int amdgpu_ras_mgr_update_ras_ecc(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!amdgpu_ras_mgr_is_ready(adev))
		return -EPERM;

	return ras_core_update_ecc_info(ras_mgr->ras_core);
}

int amdgpu_ras_mgr_reset_gpu(struct amdgpu_device *adev, uint32_t flags)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!amdgpu_ras_mgr_is_ready(adev))
		return -EPERM;

	con->gpu_reset_flags |= flags;
	return amdgpu_ras_reset_gpu(adev);
}

bool amdgpu_ras_mgr_check_eeprom_safety_watermark(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!amdgpu_ras_mgr_is_ready(adev))
		return false;

	return ras_eeprom_check_safety_watermark(ras_mgr->ras_core);
}

int amdgpu_ras_mgr_get_curr_nps_mode(struct amdgpu_device *adev,
	uint32_t *nps_mode)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	uint32_t mode;

	if (!amdgpu_ras_mgr_is_ready(adev))
		return -EINVAL;

	mode = ras_core_get_curr_nps_mode(ras_mgr->ras_core);
	if (!mode || mode > AMDGPU_NPS8_PARTITION_MODE)
		return -EINVAL;

	*nps_mode = mode;

	return 0;
}

bool amdgpu_ras_mgr_check_retired_addr(struct amdgpu_device *adev,
			uint64_t addr)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!amdgpu_ras_mgr_is_ready(adev))
		return false;

	return ras_umc_check_retired_addr(ras_mgr->ras_core, addr);
}

bool amdgpu_ras_mgr_is_rma(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!ras_mgr || !ras_mgr->ras_core || !ras_mgr->ras_is_ready)
		return false;

	return ras_core_gpu_is_rma(ras_mgr->ras_core);
}

int amdgpu_ras_mgr_handle_ras_cmd(struct amdgpu_device *adev,
			uint32_t cmd_id, void *input, uint32_t input_size,
			void *output, uint32_t out_size)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct ras_cmd_ctx *cmd_ctx;
	uint32_t ctx_buf_size = PAGE_SIZE;
	int ret;

	if (!amdgpu_sriov_vf(adev) && !amdgpu_ras_mgr_is_ready(adev))
		return -EPERM;

	cmd_ctx = kzalloc(ctx_buf_size, GFP_KERNEL);
	if (!cmd_ctx)
		return -ENOMEM;

	cmd_ctx->cmd_id = cmd_id;

	memcpy(cmd_ctx->input_buff_raw, input, input_size);
	cmd_ctx->input_size = input_size;
	cmd_ctx->output_buf_size = ctx_buf_size - sizeof(*cmd_ctx);

	ret = amdgpu_ras_submit_cmd(ras_mgr->ras_core, cmd_ctx);
	if (!ret && !cmd_ctx->cmd_res && output && (out_size == cmd_ctx->output_size))
		memcpy(output, cmd_ctx->output_buff_raw, cmd_ctx->output_size);

	kfree(cmd_ctx);

	return ret;
}

int amdgpu_ras_mgr_pre_reset(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		return amdgpu_virt_ras_pre_reset(adev);

	if (!amdgpu_ras_mgr_is_ready(adev)) {
		RAS_DEV_ERR(adev, "Invalid ras suspend!\n");
		return -EPERM;
	}

	amdgpu_ras_process_pre_reset(adev);
	return 0;
}

int amdgpu_ras_mgr_post_reset(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		return amdgpu_virt_ras_post_reset(adev);

	if (!amdgpu_ras_mgr_is_ready(adev)) {
		RAS_DEV_ERR(adev, "Invalid ras resume!\n");
		return -EPERM;
	}

	amdgpu_ras_process_post_reset(adev);
	return 0;
}

int amdgpu_ras_mgr_lookup_bad_pages_in_a_row(struct amdgpu_device *adev,
		uint64_t addr, uint64_t *nps_page_addr, uint32_t max_page_count)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!amdgpu_ras_mgr_is_ready(adev))
		return -EPERM;

	if (!nps_page_addr || !max_page_count)
		return -EINVAL;

	return ras_core_convert_soc_pa_to_cur_nps_pages(ras_mgr->ras_core,
			addr, nps_page_addr, max_page_count);
}
