/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 */

#define SWSMU_CODE_LAYER_L1

#include <linux/firmware.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_internal.h"
#include "atom.h"
#include "arcturus_ppt.h"
#include "navi10_ppt.h"
#include "sienna_cichlid_ppt.h"
#include "renoir_ppt.h"
#include "amd_pcie.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

size_t smu_sys_get_pp_feature_mask(struct smu_context *smu, char *buf)
{
	size_t size = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	size = smu_get_pp_feature_mask(smu, buf);

	mutex_unlock(&smu->mutex);

	return size;
}

int smu_sys_set_pp_feature_mask(struct smu_context *smu, uint64_t new_mask)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	ret = smu_set_pp_feature_mask(smu, new_mask);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_status_gfxoff(struct amdgpu_device *adev, uint32_t *value)
{
	int ret = 0;
	struct smu_context *smu = &adev->smu;

	if (is_support_sw_smu(adev) && smu->ppt_funcs->get_gfx_off_status)
		*value = smu_get_gfx_off_status(smu);
	else
		ret = -EINVAL;

	return ret;
}

int smu_set_soft_freq_range(struct smu_context *smu,
			    enum smu_clk_type clk_type,
			    uint32_t min,
			    uint32_t max)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_soft_freq_limited_range)
		ret = smu->ppt_funcs->set_soft_freq_limited_range(smu,
								  clk_type,
								  min,
								  max);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_dpm_freq_range(struct smu_context *smu,
			   enum smu_clk_type clk_type,
			   uint32_t *min,
			   uint32_t *max)
{
	int ret = 0;

	if (!min && !max)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_dpm_ultimate_freq)
		ret = smu->ppt_funcs->get_dpm_ultimate_freq(smu,
							    clk_type,
							    min,
							    max);

	mutex_unlock(&smu->mutex);

	return ret;
}

static int smu_dpm_set_vcn_enable_locked(struct smu_context *smu,
					 bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (!smu->ppt_funcs->dpm_set_vcn_enable)
		return 0;

	if (atomic_read(&power_gate->vcn_gated) ^ enable)
		return 0;

	ret = smu->ppt_funcs->dpm_set_vcn_enable(smu, enable);
	if (!ret)
		atomic_set(&power_gate->vcn_gated, !enable);

	return ret;
}

static int smu_dpm_set_vcn_enable(struct smu_context *smu,
				  bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	mutex_lock(&power_gate->vcn_gate_lock);

	ret = smu_dpm_set_vcn_enable_locked(smu, enable);

	mutex_unlock(&power_gate->vcn_gate_lock);

	return ret;
}

static int smu_dpm_set_jpeg_enable_locked(struct smu_context *smu,
					  bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (!smu->ppt_funcs->dpm_set_jpeg_enable)
		return 0;

	if (atomic_read(&power_gate->jpeg_gated) ^ enable)
		return 0;

	ret = smu->ppt_funcs->dpm_set_jpeg_enable(smu, enable);
	if (!ret)
		atomic_set(&power_gate->jpeg_gated, !enable);

	return ret;
}

static int smu_dpm_set_jpeg_enable(struct smu_context *smu,
				   bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	mutex_lock(&power_gate->jpeg_gate_lock);

	ret = smu_dpm_set_jpeg_enable_locked(smu, enable);

	mutex_unlock(&power_gate->jpeg_gate_lock);

	return ret;
}

/**
 * smu_dpm_set_power_gate - power gate/ungate the specific IP block
 *
 * @smu:        smu_context pointer
 * @block_type: the IP block to power gate/ungate
 * @gate:       to power gate if true, ungate otherwise
 *
 * This API uses no smu->mutex lock protection due to:
 * 1. It is either called by other IP block(gfx/sdma/vcn/uvd/vce).
 *    This is guarded to be race condition free by the caller.
 * 2. Or get called on user setting request of power_dpm_force_performance_level.
 *    Under this case, the smu->mutex lock protection is already enforced on
 *    the parent API smu_force_performance_level of the call path.
 */
int smu_dpm_set_power_gate(struct smu_context *smu, uint32_t block_type,
			   bool gate)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	switch (block_type) {
	/*
	 * Some legacy code of amdgpu_vcn.c and vcn_v2*.c still uses
	 * AMD_IP_BLOCK_TYPE_UVD for VCN. So, here both of them are kept.
	 */
	case AMD_IP_BLOCK_TYPE_UVD:
	case AMD_IP_BLOCK_TYPE_VCN:
		ret = smu_dpm_set_vcn_enable(smu, !gate);
		if (ret)
			dev_err(smu->adev->dev, "Failed to power %s VCN!\n",
				gate ? "gate" : "ungate");
		break;
	case AMD_IP_BLOCK_TYPE_GFX:
		ret = smu_gfx_off_control(smu, gate);
		if (ret)
			dev_err(smu->adev->dev, "Failed to %s gfxoff!\n",
				gate ? "enable" : "disable");
		break;
	case AMD_IP_BLOCK_TYPE_SDMA:
		ret = smu_powergate_sdma(smu, gate);
		if (ret)
			dev_err(smu->adev->dev, "Failed to power %s SDMA!\n",
				gate ? "gate" : "ungate");
		break;
	case AMD_IP_BLOCK_TYPE_JPEG:
		ret = smu_dpm_set_jpeg_enable(smu, !gate);
		if (ret)
			dev_err(smu->adev->dev, "Failed to power %s JPEG!\n",
				gate ? "gate" : "ungate");
		break;
	default:
		dev_err(smu->adev->dev, "Unsupported block type!\n");
		return -EINVAL;
	}

	return ret;
}

int smu_get_power_num_states(struct smu_context *smu,
			     struct pp_states_info *state_info)
{
	if (!state_info)
		return -EINVAL;

	/* not support power state */
	memset(state_info, 0, sizeof(struct pp_states_info));
	state_info->nums = 1;
	state_info->states[0] = POWER_STATE_TYPE_DEFAULT;

	return 0;
}

bool is_support_sw_smu(struct amdgpu_device *adev)
{
	if (adev->asic_type >= CHIP_ARCTURUS)
		return true;

	return false;
}

int smu_sys_get_pp_table(struct smu_context *smu, void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint32_t powerplay_table_size;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!smu_table->power_play_table && !smu_table->hardcode_pptable)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	if (smu_table->hardcode_pptable)
		*table = smu_table->hardcode_pptable;
	else
		*table = smu_table->power_play_table;

	powerplay_table_size = smu_table->power_play_table_size;

	mutex_unlock(&smu->mutex);

	return powerplay_table_size;
}

int smu_sys_set_pp_table(struct smu_context *smu,  void *buf, size_t size)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	ATOM_COMMON_TABLE_HEADER *header = (ATOM_COMMON_TABLE_HEADER *)buf;
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (header->usStructureSize != size) {
		dev_err(smu->adev->dev, "pp table size not matched !\n");
		return -EIO;
	}

	mutex_lock(&smu->mutex);
	if (!smu_table->hardcode_pptable)
		smu_table->hardcode_pptable = kzalloc(size, GFP_KERNEL);
	if (!smu_table->hardcode_pptable) {
		ret = -ENOMEM;
		goto failed;
	}

	memcpy(smu_table->hardcode_pptable, buf, size);
	smu_table->power_play_table = smu_table->hardcode_pptable;
	smu_table->power_play_table_size = size;

	/*
	 * Special hw_fini action(for Navi1x, the DPMs disablement will be
	 * skipped) may be needed for custom pptable uploading.
	 */
	smu->uploading_custom_pp_table = true;

	ret = smu_reset(smu);
	if (ret)
		dev_info(smu->adev->dev, "smu reset failed, ret = %d\n", ret);

	smu->uploading_custom_pp_table = false;

failed:
	mutex_unlock(&smu->mutex);
	return ret;
}

static int smu_get_driver_allowed_feature_mask(struct smu_context *smu)
{
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;
	uint32_t allowed_feature_mask[SMU_FEATURE_MAX/32];

	bitmap_zero(feature->allowed, SMU_FEATURE_MAX);

	ret = smu_get_allowed_feature_mask(smu, allowed_feature_mask,
					     SMU_FEATURE_MAX/32);
	if (ret)
		return ret;

	bitmap_or(feature->allowed, feature->allowed,
		      (unsigned long *)allowed_feature_mask,
		      feature->feature_num);

	return ret;
}

static int smu_set_funcs(struct amdgpu_device *adev)
{
	struct smu_context *smu = &adev->smu;

	if (adev->pm.pp_feature & PP_OVERDRIVE_MASK)
		smu->od_enabled = true;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		navi10_set_ppt_funcs(smu);
		break;
	case CHIP_ARCTURUS:
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		arcturus_set_ppt_funcs(smu);
		/* OD is not supported on Arcturus */
		smu->od_enabled =false;
		break;
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
		sienna_cichlid_set_ppt_funcs(smu);
		break;
	case CHIP_RENOIR:
		renoir_set_ppt_funcs(smu);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int smu_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	smu->adev = adev;
	smu->pm_enabled = !!amdgpu_dpm;
	smu->is_apu = false;
	mutex_init(&smu->mutex);

	return smu_set_funcs(adev);
}

static int smu_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int vcn_gate, jpeg_gate;
	int ret = 0;

	if (!smu->ppt_funcs->set_default_dpm_table)
		return 0;

	mutex_lock(&power_gate->vcn_gate_lock);
	mutex_lock(&power_gate->jpeg_gate_lock);

	vcn_gate = atomic_read(&power_gate->vcn_gated);
	jpeg_gate = atomic_read(&power_gate->jpeg_gated);

	ret = smu_dpm_set_vcn_enable_locked(smu, true);
	if (ret)
		goto err0_out;

	ret = smu_dpm_set_jpeg_enable_locked(smu, true);
	if (ret)
		goto err1_out;

	ret = smu->ppt_funcs->set_default_dpm_table(smu);
	if (ret)
		dev_err(smu->adev->dev,
			"Failed to setup default dpm clock tables!\n");

	smu_dpm_set_jpeg_enable_locked(smu, !jpeg_gate);
err1_out:
	smu_dpm_set_vcn_enable_locked(smu, !vcn_gate);
err0_out:
	mutex_unlock(&power_gate->jpeg_gate_lock);
	mutex_unlock(&power_gate->vcn_gate_lock);

	return ret;
}

static int smu_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret = 0;

	if (!smu->pm_enabled)
		return 0;

	ret = smu_post_init(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to post smu init!\n");
		return ret;
	}

	ret = smu_set_default_od_settings(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to setup default OD settings!\n");
		return ret;
	}

	/*
	 * Set initialized values (get from vbios) to dpm tables context such as
	 * gfxclk, memclk, dcefclk, and etc. And enable the DPM feature for each
	 * type of clks.
	 */
	ret = smu_set_default_dpm_table(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to setup default dpm clock tables!\n");
		return ret;
	}

	ret = smu_populate_umd_state_clk(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to populate UMD state clocks!\n");
		return ret;
	}

	ret = smu_get_asic_power_limits(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to get asic power limits!\n");
		return ret;
	}

	smu_get_unique_id(smu);

	smu_get_fan_parameters(smu);

	smu_handle_task(&adev->smu,
			smu->smu_dpm.dpm_level,
			AMD_PP_TASK_COMPLETE_INIT,
			false);

	return 0;
}

static int smu_init_fb_allocations(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	struct smu_table *driver_table = &(smu_table->driver_table);
	uint32_t max_table_size = 0;
	int ret, i;

	/* VRAM allocation for tool table */
	if (tables[SMU_TABLE_PMSTATUSLOG].size) {
		ret = amdgpu_bo_create_kernel(adev,
					      tables[SMU_TABLE_PMSTATUSLOG].size,
					      tables[SMU_TABLE_PMSTATUSLOG].align,
					      tables[SMU_TABLE_PMSTATUSLOG].domain,
					      &tables[SMU_TABLE_PMSTATUSLOG].bo,
					      &tables[SMU_TABLE_PMSTATUSLOG].mc_address,
					      &tables[SMU_TABLE_PMSTATUSLOG].cpu_addr);
		if (ret) {
			dev_err(adev->dev, "VRAM allocation for tool table failed!\n");
			return ret;
		}
	}

	/* VRAM allocation for driver table */
	for (i = 0; i < SMU_TABLE_COUNT; i++) {
		if (tables[i].size == 0)
			continue;

		if (i == SMU_TABLE_PMSTATUSLOG)
			continue;

		if (max_table_size < tables[i].size)
			max_table_size = tables[i].size;
	}

	driver_table->size = max_table_size;
	driver_table->align = PAGE_SIZE;
	driver_table->domain = AMDGPU_GEM_DOMAIN_VRAM;

	ret = amdgpu_bo_create_kernel(adev,
				      driver_table->size,
				      driver_table->align,
				      driver_table->domain,
				      &driver_table->bo,
				      &driver_table->mc_address,
				      &driver_table->cpu_addr);
	if (ret) {
		dev_err(adev->dev, "VRAM allocation for driver table failed!\n");
		if (tables[SMU_TABLE_PMSTATUSLOG].mc_address)
			amdgpu_bo_free_kernel(&tables[SMU_TABLE_PMSTATUSLOG].bo,
					      &tables[SMU_TABLE_PMSTATUSLOG].mc_address,
					      &tables[SMU_TABLE_PMSTATUSLOG].cpu_addr);
	}

	return ret;
}

static int smu_fini_fb_allocations(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	struct smu_table *driver_table = &(smu_table->driver_table);

	if (tables[SMU_TABLE_PMSTATUSLOG].mc_address)
		amdgpu_bo_free_kernel(&tables[SMU_TABLE_PMSTATUSLOG].bo,
				      &tables[SMU_TABLE_PMSTATUSLOG].mc_address,
				      &tables[SMU_TABLE_PMSTATUSLOG].cpu_addr);

	amdgpu_bo_free_kernel(&driver_table->bo,
			      &driver_table->mc_address,
			      &driver_table->cpu_addr);

	return 0;
}

/**
 * smu_alloc_memory_pool - allocate memory pool in the system memory
 *
 * @smu: amdgpu_device pointer
 *
 * This memory pool will be used for SMC use and msg SetSystemVirtualDramAddr
 * and DramLogSetDramAddr can notify it changed.
 *
 * Returns 0 on success, error on failure.
 */
static int smu_alloc_memory_pool(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *memory_pool = &smu_table->memory_pool;
	uint64_t pool_size = smu->pool_size;
	int ret = 0;

	if (pool_size == SMU_MEMORY_POOL_SIZE_ZERO)
		return ret;

	memory_pool->size = pool_size;
	memory_pool->align = PAGE_SIZE;
	memory_pool->domain = AMDGPU_GEM_DOMAIN_GTT;

	switch (pool_size) {
	case SMU_MEMORY_POOL_SIZE_256_MB:
	case SMU_MEMORY_POOL_SIZE_512_MB:
	case SMU_MEMORY_POOL_SIZE_1_GB:
	case SMU_MEMORY_POOL_SIZE_2_GB:
		ret = amdgpu_bo_create_kernel(adev,
					      memory_pool->size,
					      memory_pool->align,
					      memory_pool->domain,
					      &memory_pool->bo,
					      &memory_pool->mc_address,
					      &memory_pool->cpu_addr);
		if (ret)
			dev_err(adev->dev, "VRAM allocation for dramlog failed!\n");
		break;
	default:
		break;
	}

	return ret;
}

static int smu_free_memory_pool(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *memory_pool = &smu_table->memory_pool;

	if (memory_pool->size == SMU_MEMORY_POOL_SIZE_ZERO)
		return 0;

	amdgpu_bo_free_kernel(&memory_pool->bo,
			      &memory_pool->mc_address,
			      &memory_pool->cpu_addr);

	memset(memory_pool, 0, sizeof(struct smu_table));

	return 0;
}

static int smu_alloc_dummy_read_table(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *dummy_read_1_table =
			&smu_table->dummy_read_1_table;
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	dummy_read_1_table->size = 0x40000;
	dummy_read_1_table->align = PAGE_SIZE;
	dummy_read_1_table->domain = AMDGPU_GEM_DOMAIN_VRAM;

	ret = amdgpu_bo_create_kernel(adev,
				      dummy_read_1_table->size,
				      dummy_read_1_table->align,
				      dummy_read_1_table->domain,
				      &dummy_read_1_table->bo,
				      &dummy_read_1_table->mc_address,
				      &dummy_read_1_table->cpu_addr);
	if (ret)
		dev_err(adev->dev, "VRAM allocation for dummy read table failed!\n");

	return ret;
}

static void smu_free_dummy_read_table(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *dummy_read_1_table =
			&smu_table->dummy_read_1_table;


	amdgpu_bo_free_kernel(&dummy_read_1_table->bo,
			      &dummy_read_1_table->mc_address,
			      &dummy_read_1_table->cpu_addr);

	memset(dummy_read_1_table, 0, sizeof(struct smu_table));
}

static int smu_smc_table_sw_init(struct smu_context *smu)
{
	int ret;

	/**
	 * Create smu_table structure, and init smc tables such as
	 * TABLE_PPTABLE, TABLE_WATERMARKS, TABLE_SMU_METRICS, and etc.
	 */
	ret = smu_init_smc_tables(smu);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to init smc tables!\n");
		return ret;
	}

	/**
	 * Create smu_power_context structure, and allocate smu_dpm_context and
	 * context size to fill the smu_power_context data.
	 */
	ret = smu_init_power(smu);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to init smu_init_power!\n");
		return ret;
	}

	/*
	 * allocate vram bos to store smc table contents.
	 */
	ret = smu_init_fb_allocations(smu);
	if (ret)
		return ret;

	ret = smu_alloc_memory_pool(smu);
	if (ret)
		return ret;

	ret = smu_alloc_dummy_read_table(smu);
	if (ret)
		return ret;

	ret = smu_i2c_init(smu, &smu->adev->pm.smu_i2c);
	if (ret)
		return ret;

	return 0;
}

static int smu_smc_table_sw_fini(struct smu_context *smu)
{
	int ret;

	smu_i2c_fini(smu, &smu->adev->pm.smu_i2c);

	smu_free_dummy_read_table(smu);

	ret = smu_free_memory_pool(smu);
	if (ret)
		return ret;

	ret = smu_fini_fb_allocations(smu);
	if (ret)
		return ret;

	ret = smu_fini_power(smu);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to init smu_fini_power!\n");
		return ret;
	}

	ret = smu_fini_smc_tables(smu);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to smu_fini_smc_tables!\n");
		return ret;
	}

	return 0;
}

static void smu_throttling_logging_work_fn(struct work_struct *work)
{
	struct smu_context *smu = container_of(work, struct smu_context,
					       throttling_logging_work);

	smu_log_thermal_throttling(smu);
}

static int smu_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	smu->pool_size = adev->pm.smu_prv_buffer_size;
	smu->smu_feature.feature_num = SMU_FEATURE_MAX;
	mutex_init(&smu->smu_feature.mutex);
	bitmap_zero(smu->smu_feature.supported, SMU_FEATURE_MAX);
	bitmap_zero(smu->smu_feature.enabled, SMU_FEATURE_MAX);
	bitmap_zero(smu->smu_feature.allowed, SMU_FEATURE_MAX);

	mutex_init(&smu->smu_baco.mutex);
	smu->smu_baco.state = SMU_BACO_STATE_EXIT;
	smu->smu_baco.platform_support = false;

	mutex_init(&smu->sensor_lock);
	mutex_init(&smu->metrics_lock);
	mutex_init(&smu->message_lock);

	INIT_WORK(&smu->throttling_logging_work, smu_throttling_logging_work_fn);
	atomic64_set(&smu->throttle_int_counter, 0);
	smu->watermarks_bitmap = 0;
	smu->power_profile_mode = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;
	smu->default_power_profile_mode = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;

	atomic_set(&smu->smu_power.power_gate.vcn_gated, 1);
	atomic_set(&smu->smu_power.power_gate.jpeg_gated, 1);
	mutex_init(&smu->smu_power.power_gate.vcn_gate_lock);
	mutex_init(&smu->smu_power.power_gate.jpeg_gate_lock);

	smu->workload_mask = 1 << smu->workload_prority[PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT];
	smu->workload_prority[PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT] = 0;
	smu->workload_prority[PP_SMC_POWER_PROFILE_FULLSCREEN3D] = 1;
	smu->workload_prority[PP_SMC_POWER_PROFILE_POWERSAVING] = 2;
	smu->workload_prority[PP_SMC_POWER_PROFILE_VIDEO] = 3;
	smu->workload_prority[PP_SMC_POWER_PROFILE_VR] = 4;
	smu->workload_prority[PP_SMC_POWER_PROFILE_COMPUTE] = 5;
	smu->workload_prority[PP_SMC_POWER_PROFILE_CUSTOM] = 6;

	smu->workload_setting[0] = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;
	smu->workload_setting[1] = PP_SMC_POWER_PROFILE_FULLSCREEN3D;
	smu->workload_setting[2] = PP_SMC_POWER_PROFILE_POWERSAVING;
	smu->workload_setting[3] = PP_SMC_POWER_PROFILE_VIDEO;
	smu->workload_setting[4] = PP_SMC_POWER_PROFILE_VR;
	smu->workload_setting[5] = PP_SMC_POWER_PROFILE_COMPUTE;
	smu->workload_setting[6] = PP_SMC_POWER_PROFILE_CUSTOM;
	smu->display_config = &adev->pm.pm_display_cfg;

	smu->smu_dpm.dpm_level = AMD_DPM_FORCED_LEVEL_AUTO;
	smu->smu_dpm.requested_dpm_level = AMD_DPM_FORCED_LEVEL_AUTO;

	if (!amdgpu_sriov_vf(adev)) {
		ret = smu_init_microcode(smu);
		if (ret) {
			dev_err(adev->dev, "Failed to load smu firmware!\n");
			return ret;
		}
	}

	ret = smu_smc_table_sw_init(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to sw init smc table!\n");
		return ret;
	}

	ret = smu_register_irq_handler(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to register smc irq handler!\n");
		return ret;
	}

	return 0;
}

static int smu_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	ret = smu_smc_table_sw_fini(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to sw fini smc table!\n");
		return ret;
	}

	smu_fini_microcode(smu);

	return 0;
}

static int smu_get_thermal_temperature_range(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_temperature_range *range =
				&smu->thermal_range;
	int ret = 0;

	if (!smu->ppt_funcs->get_thermal_temperature_range)
		return 0;

	ret = smu->ppt_funcs->get_thermal_temperature_range(smu, range);
	if (ret)
		return ret;

	adev->pm.dpm.thermal.min_temp = range->min;
	adev->pm.dpm.thermal.max_temp = range->max;
	adev->pm.dpm.thermal.max_edge_emergency_temp = range->edge_emergency_max;
	adev->pm.dpm.thermal.min_hotspot_temp = range->hotspot_min;
	adev->pm.dpm.thermal.max_hotspot_crit_temp = range->hotspot_crit_max;
	adev->pm.dpm.thermal.max_hotspot_emergency_temp = range->hotspot_emergency_max;
	adev->pm.dpm.thermal.min_mem_temp = range->mem_min;
	adev->pm.dpm.thermal.max_mem_crit_temp = range->mem_crit_max;
	adev->pm.dpm.thermal.max_mem_emergency_temp = range->mem_emergency_max;

	return ret;
}

static int smu_smc_hw_setup(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t pcie_gen = 0, pcie_width = 0;
	int ret;

	if (adev->in_suspend && smu_is_dpm_running(smu)) {
		dev_info(adev->dev, "dpm has been enabled\n");
		return 0;
	}

	ret = smu_init_display_count(smu, 0);
	if (ret) {
		dev_info(adev->dev, "Failed to pre-set display count as 0!\n");
		return ret;
	}

	ret = smu_set_driver_table_location(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to SetDriverDramAddr!\n");
		return ret;
	}

	/*
	 * Set PMSTATUSLOG table bo address with SetToolsDramAddr MSG for tools.
	 */
	ret = smu_set_tool_table_location(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to SetToolsDramAddr!\n");
		return ret;
	}

	/*
	 * Use msg SetSystemVirtualDramAddr and DramLogSetDramAddr can notify
	 * pool location.
	 */
	ret = smu_notify_memory_pool_location(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to SetDramLogDramAddr!\n");
		return ret;
	}

	/* smu_dump_pptable(smu); */
	/*
	 * Copy pptable bo in the vram to smc with SMU MSGs such as
	 * SetDriverDramAddr and TransferTableDram2Smu.
	 */
	ret = smu_write_pptable(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to transfer pptable to SMC!\n");
		return ret;
	}

	/* issue Run*Btc msg */
	ret = smu_run_btc(smu);
	if (ret)
		return ret;

	ret = smu_feature_set_allowed_mask(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to set driver allowed features mask!\n");
		return ret;
	}

	ret = smu_system_features_control(smu, true);
	if (ret) {
		dev_err(adev->dev, "Failed to enable requested dpm features!\n");
		return ret;
	}

	if (!smu_is_dpm_running(smu))
		dev_info(adev->dev, "dpm has been disabled\n");

	if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN4)
		pcie_gen = 3;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
		pcie_gen = 2;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2)
		pcie_gen = 1;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1)
		pcie_gen = 0;

	/* Bit 31:16: LCLK DPM level. 0 is DPM0, and 1 is DPM1
	 * Bit 15:8:  PCIE GEN, 0 to 3 corresponds to GEN1 to GEN4
	 * Bit 7:0:   PCIE lane width, 1 to 7 corresponds is x1 to x32
	 */
	if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X16)
		pcie_width = 6;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X12)
		pcie_width = 5;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X8)
		pcie_width = 4;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X4)
		pcie_width = 3;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X2)
		pcie_width = 2;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X1)
		pcie_width = 1;
	ret = smu_update_pcie_parameters(smu, pcie_gen, pcie_width);
	if (ret) {
		dev_err(adev->dev, "Attempt to override pcie params failed!\n");
		return ret;
	}

	ret = smu_get_thermal_temperature_range(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to get thermal temperature ranges!\n");
		return ret;
	}

	ret = smu_enable_thermal_alert(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to enable thermal alert!\n");
		return ret;
	}

	ret = smu_notify_display_change(smu);
	if (ret)
		return ret;

	/*
	 * Set min deep sleep dce fclk with bootup value from vbios via
	 * SetMinDeepSleepDcefclk MSG.
	 */
	ret = smu_set_min_dcef_deep_sleep(smu,
					  smu->smu_table.boot_values.dcefclk / 100);
	if (ret)
		return ret;

	return ret;
}

static int smu_start_smc_engine(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		if (adev->asic_type < CHIP_NAVI10) {
			if (smu->ppt_funcs->load_microcode) {
				ret = smu->ppt_funcs->load_microcode(smu);
				if (ret)
					return ret;
			}
		}
	}

	if (smu->ppt_funcs->check_fw_status) {
		ret = smu->ppt_funcs->check_fw_status(smu);
		if (ret) {
			dev_err(adev->dev, "SMC is not ready\n");
			return ret;
		}
	}

	/*
	 * Send msg GetDriverIfVersion to check if the return value is equal
	 * with DRIVER_IF_VERSION of smc header.
	 */
	ret = smu_check_fw_version(smu);
	if (ret)
		return ret;

	return ret;
}

static int smu_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (amdgpu_sriov_vf(adev) && !amdgpu_sriov_is_pp_one_vf(adev)) {
		smu->pm_enabled = false;
		return 0;
	}

	ret = smu_start_smc_engine(smu);
	if (ret) {
		dev_err(adev->dev, "SMC engine is not correctly up!\n");
		return ret;
	}

	if (smu->is_apu) {
		smu_powergate_sdma(&adev->smu, false);
		smu_dpm_set_vcn_enable(smu, true);
		smu_dpm_set_jpeg_enable(smu, true);
		smu_set_gfx_cgpg(&adev->smu, true);
	}

	if (!smu->pm_enabled)
		return 0;

	/* get boot_values from vbios to set revision, gfxclk, and etc. */
	ret = smu_get_vbios_bootup_values(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to get VBIOS boot clock values!\n");
		return ret;
	}

	ret = smu_setup_pptable(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to setup pptable!\n");
		return ret;
	}

	ret = smu_get_driver_allowed_feature_mask(smu);
	if (ret)
		return ret;

	ret = smu_smc_hw_setup(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to setup smc hw!\n");
		return ret;
	}

	/*
	 * Move maximum sustainable clock retrieving here considering
	 * 1. It is not needed on resume(from S3).
	 * 2. DAL settings come between .hw_init and .late_init of SMU.
	 *    And DAL needs to know the maximum sustainable clocks. Thus
	 *    it cannot be put in .late_init().
	 */
	ret = smu_init_max_sustainable_clocks(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to init max sustainable clocks!\n");
		return ret;
	}

	adev->pm.dpm_enabled = true;

	dev_info(adev->dev, "SMU is initialized successfully!\n");

	return 0;
}

static int smu_disable_dpms(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;
	bool use_baco = !smu->is_apu &&
		((amdgpu_in_reset(adev) &&
		  (amdgpu_asic_reset_method(adev) == AMD_RESET_METHOD_BACO)) ||
		 ((adev->in_runpm || adev->in_hibernate) && amdgpu_asic_supports_baco(adev)));

	/*
	 * For custom pptable uploading, skip the DPM features
	 * disable process on Navi1x ASICs.
	 *   - As the gfx related features are under control of
	 *     RLC on those ASICs. RLC reinitialization will be
	 *     needed to reenable them. That will cost much more
	 *     efforts.
	 *
	 *   - SMU firmware can handle the DPM reenablement
	 *     properly.
	 */
	if (smu->uploading_custom_pp_table &&
	    (adev->asic_type >= CHIP_NAVI10) &&
	    (adev->asic_type <= CHIP_NAVY_FLOUNDER))
		return 0;

	/*
	 * For Sienna_Cichlid, PMFW will handle the features disablement properly
	 * on BACO in. Driver involvement is unnecessary.
	 */
	if ((adev->asic_type == CHIP_SIENNA_CICHLID) &&
	     use_baco)
		return 0;

	/*
	 * For gpu reset, runpm and hibernation through BACO,
	 * BACO feature has to be kept enabled.
	 */
	if (use_baco && smu_feature_is_enabled(smu, SMU_FEATURE_BACO_BIT)) {
		ret = smu_disable_all_features_with_exception(smu,
							      SMU_FEATURE_BACO_BIT);
		if (ret)
			dev_err(adev->dev, "Failed to disable smu features except BACO.\n");
	} else {
		ret = smu_system_features_control(smu, false);
		if (ret)
			dev_err(adev->dev, "Failed to disable smu features.\n");
	}

	if (adev->asic_type >= CHIP_NAVI10 &&
	    adev->gfx.rlc.funcs->stop)
		adev->gfx.rlc.funcs->stop(adev);

	return ret;
}

static int smu_smc_hw_cleanup(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	cancel_work_sync(&smu->throttling_logging_work);

	ret = smu_disable_thermal_alert(smu);
	if (ret) {
		dev_err(adev->dev, "Fail to disable thermal alert!\n");
		return ret;
	}

	ret = smu_disable_dpms(smu);
	if (ret) {
		dev_err(adev->dev, "Fail to disable dpm features!\n");
		return ret;
	}

	return 0;
}

static int smu_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (amdgpu_sriov_vf(adev)&& !amdgpu_sriov_is_pp_one_vf(adev))
		return 0;

	if (smu->is_apu) {
		smu_powergate_sdma(&adev->smu, true);
		smu_dpm_set_vcn_enable(smu, false);
		smu_dpm_set_jpeg_enable(smu, false);
	}

	if (!smu->pm_enabled)
		return 0;

	adev->pm.dpm_enabled = false;

	return smu_smc_hw_cleanup(smu);
}

int smu_reset(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;

	amdgpu_gfx_off_ctrl(smu->adev, false);

	ret = smu_hw_fini(adev);
	if (ret)
		return ret;

	ret = smu_hw_init(adev);
	if (ret)
		return ret;

	ret = smu_late_init(adev);
	if (ret)
		return ret;

	amdgpu_gfx_off_ctrl(smu->adev, true);

	return 0;
}

static int smu_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	if (amdgpu_sriov_vf(adev)&& !amdgpu_sriov_is_pp_one_vf(adev))
		return 0;

	if (!smu->pm_enabled)
		return 0;

	adev->pm.dpm_enabled = false;

	ret = smu_smc_hw_cleanup(smu);
	if (ret)
		return ret;

	smu->watermarks_bitmap &= ~(WATERMARKS_LOADED);

	if (smu->is_apu)
		smu_set_gfx_cgpg(&adev->smu, false);

	return 0;
}

static int smu_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (amdgpu_sriov_vf(adev)&& !amdgpu_sriov_is_pp_one_vf(adev))
		return 0;

	if (!smu->pm_enabled)
		return 0;

	dev_info(adev->dev, "SMU is resuming...\n");

	ret = smu_start_smc_engine(smu);
	if (ret) {
		dev_err(adev->dev, "SMC engine is not correctly up!\n");
		return ret;
	}

	ret = smu_smc_hw_setup(smu);
	if (ret) {
		dev_err(adev->dev, "Failed to setup smc hw!\n");
		return ret;
	}

	if (smu->is_apu)
		smu_set_gfx_cgpg(&adev->smu, true);

	smu->disable_uclk_switch = 0;

	adev->pm.dpm_enabled = true;

	dev_info(adev->dev, "SMU is resumed successfully!\n");

	return 0;
}

int smu_display_configuration_change(struct smu_context *smu,
				     const struct amd_pp_display_configuration *display_config)
{
	int index = 0;
	int num_of_active_display = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!display_config)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	smu_set_min_dcef_deep_sleep(smu,
				    display_config->min_dcef_deep_sleep_set_clk / 100);

	for (index = 0; index < display_config->num_path_including_non_display; index++) {
		if (display_config->displays[index].controller_id != 0)
			num_of_active_display++;
	}

	smu_set_active_display_count(smu, num_of_active_display);

	smu_store_cc6_data(smu, display_config->cpu_pstate_separation_time,
			   display_config->cpu_cc6_disable,
			   display_config->cpu_pstate_disable,
			   display_config->nb_pstate_switch_disable);

	mutex_unlock(&smu->mutex);

	return 0;
}

static int smu_get_clock_info(struct smu_context *smu,
			      struct smu_clock_info *clk_info,
			      enum smu_perf_level_designation designation)
{
	int ret;
	struct smu_performance_level level = {0};

	if (!clk_info)
		return -EINVAL;

	ret = smu_get_perf_level(smu, PERF_LEVEL_ACTIVITY, &level);
	if (ret)
		return -EINVAL;

	clk_info->min_mem_clk = level.memory_clock;
	clk_info->min_eng_clk = level.core_clock;
	clk_info->min_bus_bandwidth = level.non_local_mem_freq * level.non_local_mem_width;

	ret = smu_get_perf_level(smu, designation, &level);
	if (ret)
		return -EINVAL;

	clk_info->min_mem_clk = level.memory_clock;
	clk_info->min_eng_clk = level.core_clock;
	clk_info->min_bus_bandwidth = level.non_local_mem_freq * level.non_local_mem_width;

	return 0;
}

int smu_get_current_clocks(struct smu_context *smu,
			   struct amd_pp_clock_info *clocks)
{
	struct amd_pp_simple_clock_info simple_clocks = {0};
	struct smu_clock_info hw_clocks;
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	smu_get_dal_power_level(smu, &simple_clocks);

	if (smu->support_power_containment)
		ret = smu_get_clock_info(smu, &hw_clocks,
					 PERF_LEVEL_POWER_CONTAINMENT);
	else
		ret = smu_get_clock_info(smu, &hw_clocks, PERF_LEVEL_ACTIVITY);

	if (ret) {
		dev_err(smu->adev->dev, "Error in smu_get_clock_info\n");
		goto failed;
	}

	clocks->min_engine_clock = hw_clocks.min_eng_clk;
	clocks->max_engine_clock = hw_clocks.max_eng_clk;
	clocks->min_memory_clock = hw_clocks.min_mem_clk;
	clocks->max_memory_clock = hw_clocks.max_mem_clk;
	clocks->min_bus_bandwidth = hw_clocks.min_bus_bandwidth;
	clocks->max_bus_bandwidth = hw_clocks.max_bus_bandwidth;
	clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
	clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;

        if (simple_clocks.level == 0)
                clocks->max_clocks_state = PP_DAL_POWERLEVEL_7;
        else
                clocks->max_clocks_state = simple_clocks.level;

        if (!smu_get_current_shallow_sleep_clocks(smu, &hw_clocks)) {
                clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
                clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;
        }

failed:
	mutex_unlock(&smu->mutex);
	return ret;
}

static int smu_set_clockgating_state(void *handle,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int smu_set_powergating_state(void *handle,
				     enum amd_powergating_state state)
{
	return 0;
}

static int smu_enable_umd_pstate(void *handle,
		      enum amd_dpm_forced_level *level)
{
	uint32_t profile_mode_mask = AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_PEAK;

	struct smu_context *smu = (struct smu_context*)(handle);
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!smu->is_apu && !smu_dpm_ctx->dpm_context)
		return -EINVAL;

	if (!(smu_dpm_ctx->dpm_level & profile_mode_mask)) {
		/* enter umd pstate, save current level, disable gfx cg*/
		if (*level & profile_mode_mask) {
			smu_dpm_ctx->saved_dpm_level = smu_dpm_ctx->dpm_level;
			smu_dpm_ctx->enable_umd_pstate = true;
			amdgpu_device_ip_set_powergating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_PG_STATE_UNGATE);
			amdgpu_device_ip_set_clockgating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_CG_STATE_UNGATE);
			smu_gfx_ulv_control(smu, false);
			smu_deep_sleep_control(smu, false);
		}
	} else {
		/* exit umd pstate, restore level, enable gfx cg*/
		if (!(*level & profile_mode_mask)) {
			if (*level == AMD_DPM_FORCED_LEVEL_PROFILE_EXIT)
				*level = smu_dpm_ctx->saved_dpm_level;
			smu_dpm_ctx->enable_umd_pstate = false;
			smu_deep_sleep_control(smu, true);
			smu_gfx_ulv_control(smu, true);
			amdgpu_device_ip_set_clockgating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_CG_STATE_GATE);
			amdgpu_device_ip_set_powergating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_PG_STATE_GATE);
		}
	}

	return 0;
}

static int smu_adjust_power_state_dynamic(struct smu_context *smu,
				   enum amd_dpm_forced_level level,
				   bool skip_display_settings)
{
	int ret = 0;
	int index = 0;
	long workload;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!skip_display_settings) {
		ret = smu_display_config_changed(smu);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to change display config!");
			return ret;
		}
	}

	ret = smu_apply_clocks_adjust_rules(smu);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to apply clocks adjust rules!");
		return ret;
	}

	if (!skip_display_settings) {
		ret = smu_notify_smc_display_config(smu);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to notify smc display config!");
			return ret;
		}
	}

	if (smu_dpm_ctx->dpm_level != level) {
		ret = smu_asic_set_performance_level(smu, level);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to set performance level!");
			return ret;
		}

		/* update the saved copy */
		smu_dpm_ctx->dpm_level = level;
	}

	if (smu_dpm_ctx->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		index = fls(smu->workload_mask);
		index = index > 0 && index <= WORKLOAD_POLICY_MAX ? index - 1 : 0;
		workload = smu->workload_setting[index];

		if (smu->power_profile_mode != workload)
			smu_set_power_profile_mode(smu, &workload, 0, false);
	}

	return ret;
}

int smu_handle_task(struct smu_context *smu,
		    enum amd_dpm_forced_level level,
		    enum amd_pp_task task_id,
		    bool lock_needed)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (lock_needed)
		mutex_lock(&smu->mutex);

	switch (task_id) {
	case AMD_PP_TASK_DISPLAY_CONFIG_CHANGE:
		ret = smu_pre_display_config_changed(smu);
		if (ret)
			goto out;
		ret = smu_set_cpu_power_state(smu);
		if (ret)
			goto out;
		ret = smu_adjust_power_state_dynamic(smu, level, false);
		break;
	case AMD_PP_TASK_COMPLETE_INIT:
	case AMD_PP_TASK_READJUST_POWER_STATE:
		ret = smu_adjust_power_state_dynamic(smu, level, true);
		break;
	default:
		break;
	}

out:
	if (lock_needed)
		mutex_unlock(&smu->mutex);

	return ret;
}

int smu_switch_power_profile(struct smu_context *smu,
			     enum PP_SMC_POWER_PROFILE type,
			     bool en)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	long workload;
	uint32_t index;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!(type < PP_SMC_POWER_PROFILE_CUSTOM))
		return -EINVAL;

	mutex_lock(&smu->mutex);

	if (!en) {
		smu->workload_mask &= ~(1 << smu->workload_prority[type]);
		index = fls(smu->workload_mask);
		index = index > 0 && index <= WORKLOAD_POLICY_MAX ? index - 1 : 0;
		workload = smu->workload_setting[index];
	} else {
		smu->workload_mask |= (1 << smu->workload_prority[type]);
		index = fls(smu->workload_mask);
		index = index <= WORKLOAD_POLICY_MAX ? index - 1 : 0;
		workload = smu->workload_setting[index];
	}

	if (smu_dpm_ctx->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		smu_set_power_profile_mode(smu, &workload, 0, false);

	mutex_unlock(&smu->mutex);

	return 0;
}

enum amd_dpm_forced_level smu_get_performance_level(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	enum amd_dpm_forced_level level;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!smu->is_apu && !smu_dpm_ctx->dpm_context)
		return -EINVAL;

	mutex_lock(&(smu->mutex));
	level = smu_dpm_ctx->dpm_level;
	mutex_unlock(&(smu->mutex));

	return level;
}

int smu_force_performance_level(struct smu_context *smu, enum amd_dpm_forced_level level)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!smu->is_apu && !smu_dpm_ctx->dpm_context)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	ret = smu_enable_umd_pstate(smu, &level);
	if (ret) {
		mutex_unlock(&smu->mutex);
		return ret;
	}

	ret = smu_handle_task(smu, level,
			      AMD_PP_TASK_READJUST_POWER_STATE,
			      false);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_display_count(struct smu_context *smu, uint32_t count)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);
	ret = smu_init_display_count(smu, count);
	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_force_clk_levels(struct smu_context *smu,
			 enum smu_clk_type clk_type,
			 uint32_t mask)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (smu_dpm_ctx->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		dev_dbg(smu->adev->dev, "force clock level is for dpm manual mode only.\n");
		return -EINVAL;
	}

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs && smu->ppt_funcs->force_clk_levels)
		ret = smu->ppt_funcs->force_clk_levels(smu, clk_type, mask);

	mutex_unlock(&smu->mutex);

	return ret;
}

/*
 * On system suspending or resetting, the dpm_enabled
 * flag will be cleared. So that those SMU services which
 * are not supported will be gated.
 * However, the mp1 state setting should still be granted
 * even if the dpm_enabled cleared.
 */
int smu_set_mp1_state(struct smu_context *smu,
		      enum pp_mp1_state mp1_state)
{
	uint16_t msg;
	int ret;

	if (!smu->pm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	switch (mp1_state) {
	case PP_MP1_STATE_SHUTDOWN:
		msg = SMU_MSG_PrepareMp1ForShutdown;
		break;
	case PP_MP1_STATE_UNLOAD:
		msg = SMU_MSG_PrepareMp1ForUnload;
		break;
	case PP_MP1_STATE_RESET:
		msg = SMU_MSG_PrepareMp1ForReset;
		break;
	case PP_MP1_STATE_NONE:
	default:
		mutex_unlock(&smu->mutex);
		return 0;
	}

	ret = smu_send_smc_msg(smu, msg, NULL);
	/* some asics may not support those messages */
	if (ret == -EINVAL)
		ret = 0;
	if (ret)
		dev_err(smu->adev->dev, "[PrepareMp1] Failed!\n");

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_df_cstate(struct smu_context *smu,
		      enum pp_df_cstate state)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!smu->ppt_funcs || !smu->ppt_funcs->set_df_cstate)
		return 0;

	mutex_lock(&smu->mutex);

	ret = smu->ppt_funcs->set_df_cstate(smu, state);
	if (ret)
		dev_err(smu->adev->dev, "[SetDfCstate] failed!\n");

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_allow_xgmi_power_down(struct smu_context *smu, bool en)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!smu->ppt_funcs || !smu->ppt_funcs->allow_xgmi_power_down)
		return 0;

	mutex_lock(&smu->mutex);

	ret = smu->ppt_funcs->allow_xgmi_power_down(smu, en);
	if (ret)
		dev_err(smu->adev->dev, "[AllowXgmiPowerDown] failed!\n");

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_write_watermarks_table(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	ret = smu_set_watermarks_table(smu, NULL);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_watermarks_for_clock_ranges(struct smu_context *smu,
		struct pp_smu_wm_range_sets *clock_ranges)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (smu->disable_watermark)
		return 0;

	mutex_lock(&smu->mutex);

	ret = smu_set_watermarks_table(smu, clock_ranges);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_ac_dc(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	/* controlled by firmware */
	if (smu->dc_controlled_by_gpio)
		return 0;

	mutex_lock(&smu->mutex);
	ret = smu_set_power_source(smu,
				   smu->adev->pm.ac_power ? SMU_POWER_SOURCE_AC :
				   SMU_POWER_SOURCE_DC);
	if (ret)
		dev_err(smu->adev->dev, "Failed to switch to %s mode!\n",
		       smu->adev->pm.ac_power ? "AC" : "DC");
	mutex_unlock(&smu->mutex);

	return ret;
}

const struct amd_ip_funcs smu_ip_funcs = {
	.name = "smu",
	.early_init = smu_early_init,
	.late_init = smu_late_init,
	.sw_init = smu_sw_init,
	.sw_fini = smu_sw_fini,
	.hw_init = smu_hw_init,
	.hw_fini = smu_hw_fini,
	.suspend = smu_suspend,
	.resume = smu_resume,
	.is_idle = NULL,
	.check_soft_reset = NULL,
	.wait_for_idle = NULL,
	.soft_reset = NULL,
	.set_clockgating_state = smu_set_clockgating_state,
	.set_powergating_state = smu_set_powergating_state,
	.enable_umd_pstate = smu_enable_umd_pstate,
};

const struct amdgpu_ip_block_version smu_v11_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 11,
	.minor = 0,
	.rev = 0,
	.funcs = &smu_ip_funcs,
};

const struct amdgpu_ip_block_version smu_v12_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 12,
	.minor = 0,
	.rev = 0,
	.funcs = &smu_ip_funcs,
};

int smu_load_microcode(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->load_microcode)
		ret = smu->ppt_funcs->load_microcode(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_check_fw_status(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->check_fw_status)
		ret = smu->ppt_funcs->check_fw_status(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_gfx_cgpg(struct smu_context *smu, bool enabled)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_gfx_cgpg)
		ret = smu->ppt_funcs->set_gfx_cgpg(smu, enabled);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_fan_speed_rpm(struct smu_context *smu, uint32_t speed)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_fan_speed_rpm)
		ret = smu->ppt_funcs->set_fan_speed_rpm(smu, speed);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_power_limit(struct smu_context *smu,
			uint32_t *limit,
			bool max_setting)
{
	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	*limit = (max_setting ? smu->max_power_limit : smu->current_power_limit);

	mutex_unlock(&smu->mutex);

	return 0;
}

int smu_set_power_limit(struct smu_context *smu, uint32_t limit)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (limit > smu->max_power_limit) {
		dev_err(smu->adev->dev,
			"New power limit (%d) is over the max allowed %d\n",
			limit, smu->max_power_limit);
		goto out;
	}

	if (!limit)
		limit = smu->current_power_limit;

	if (smu->ppt_funcs->set_power_limit)
		ret = smu->ppt_funcs->set_power_limit(smu, limit);

out:
	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_print_clk_levels(struct smu_context *smu, enum smu_clk_type clk_type, char *buf)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->print_clk_levels)
		ret = smu->ppt_funcs->print_clk_levels(smu, clk_type, buf);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_od_percentage(struct smu_context *smu, enum smu_clk_type type)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_od_percentage)
		ret = smu->ppt_funcs->get_od_percentage(smu, type);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_od_percentage(struct smu_context *smu, enum smu_clk_type type, uint32_t value)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_od_percentage)
		ret = smu->ppt_funcs->set_od_percentage(smu, type, value);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_od_edit_dpm_table(struct smu_context *smu,
			  enum PP_OD_DPM_TABLE_COMMAND type,
			  long *input, uint32_t size)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->od_edit_dpm_table) {
		ret = smu->ppt_funcs->od_edit_dpm_table(smu, type, input, size);
		if (!ret && (type == PP_OD_COMMIT_DPM_TABLE))
			ret = smu_handle_task(smu,
					      smu->smu_dpm.dpm_level,
					      AMD_PP_TASK_READJUST_POWER_STATE,
					      false);
	}

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_read_sensor(struct smu_context *smu,
		    enum amd_pp_sensors sensor,
		    void *data, uint32_t *size)
{
	struct smu_umd_pstate_table *pstate_table =
				&smu->pstate_table;
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!data || !size)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->read_sensor)
		if (!smu->ppt_funcs->read_sensor(smu, sensor, data, size))
			goto unlock;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_SCLK:
		*((uint32_t *)data) = pstate_table->gfxclk_pstate.standard * 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_MCLK:
		*((uint32_t *)data) = pstate_table->uclk_pstate.standard * 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK:
		ret = smu_feature_get_enabled_mask(smu, (uint32_t *)data, 2);
		*size = 8;
		break;
	case AMDGPU_PP_SENSOR_UVD_POWER:
		*(uint32_t *)data = smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UVD_BIT) ? 1 : 0;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VCE_POWER:
		*(uint32_t *)data = smu_feature_is_enabled(smu, SMU_FEATURE_DPM_VCE_BIT) ? 1 : 0;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VCN_POWER_STATE:
		*(uint32_t *)data = atomic_read(&smu->smu_power.power_gate.vcn_gated) ? 0: 1;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MIN_FAN_RPM:
		*(uint32_t *)data = 0;
		*size = 4;
		break;
	default:
		*size = 0;
		ret = -EOPNOTSUPP;
		break;
	}

unlock:
	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_power_profile_mode)
		ret = smu->ppt_funcs->get_power_profile_mode(smu, buf);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_power_profile_mode(struct smu_context *smu,
			       long *param,
			       uint32_t param_size,
			       bool lock_needed)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (lock_needed)
		mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_power_profile_mode)
		ret = smu->ppt_funcs->set_power_profile_mode(smu, param, param_size);

	if (lock_needed)
		mutex_unlock(&smu->mutex);

	return ret;
}


int smu_get_fan_control_mode(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_fan_control_mode)
		ret = smu->ppt_funcs->get_fan_control_mode(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_fan_control_mode(struct smu_context *smu, int value)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_fan_control_mode)
		ret = smu->ppt_funcs->set_fan_control_mode(smu, value);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_fan_speed_percent(struct smu_context *smu, uint32_t *speed)
{
	int ret = 0;
	uint32_t percent;
	uint32_t current_rpm;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_fan_speed_rpm) {
		ret = smu->ppt_funcs->get_fan_speed_rpm(smu, &current_rpm);
		if (!ret) {
			percent = current_rpm * 100 / smu->fan_max_rpm;
			*speed = percent > 100 ? 100 : percent;
		}
	}

	mutex_unlock(&smu->mutex);


	return ret;
}

int smu_set_fan_speed_percent(struct smu_context *smu, uint32_t speed)
{
	int ret = 0;
	uint32_t rpm;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_fan_speed_rpm) {
		if (speed > 100)
			speed = 100;
		rpm = speed * smu->fan_max_rpm / 100;
		ret = smu->ppt_funcs->set_fan_speed_rpm(smu, rpm);
	}

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_fan_speed_rpm(struct smu_context *smu, uint32_t *speed)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_fan_speed_rpm)
		ret = smu->ppt_funcs->get_fan_speed_rpm(smu, speed);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_deep_sleep_dcefclk(struct smu_context *smu, int clk)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	ret = smu_set_min_dcef_deep_sleep(smu, clk);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_clock_by_type(struct smu_context *smu,
			  enum amd_pp_clock_type type,
			  struct amd_pp_clocks *clocks)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_clock_by_type)
		ret = smu->ppt_funcs->get_clock_by_type(smu, type, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_max_high_clocks(struct smu_context *smu,
			    struct amd_pp_simple_clock_info *clocks)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_max_high_clocks)
		ret = smu->ppt_funcs->get_max_high_clocks(smu, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_clock_by_type_with_latency(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       struct pp_clock_levels_with_latency *clocks)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_clock_by_type_with_latency)
		ret = smu->ppt_funcs->get_clock_by_type_with_latency(smu, clk_type, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_clock_by_type_with_voltage(struct smu_context *smu,
				       enum amd_pp_clock_type type,
				       struct pp_clock_levels_with_voltage *clocks)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_clock_by_type_with_voltage)
		ret = smu->ppt_funcs->get_clock_by_type_with_voltage(smu, type, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}


int smu_display_clock_voltage_request(struct smu_context *smu,
				      struct pp_display_clock_request *clock_req)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->display_clock_voltage_request)
		ret = smu->ppt_funcs->display_clock_voltage_request(smu, clock_req);

	mutex_unlock(&smu->mutex);

	return ret;
}


int smu_display_disable_memory_clock_switch(struct smu_context *smu, bool disable_memory_clock_switch)
{
	int ret = -EINVAL;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->display_disable_memory_clock_switch)
		ret = smu->ppt_funcs->display_disable_memory_clock_switch(smu, disable_memory_clock_switch);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_notify_smu_enable_pwe(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->notify_smu_enable_pwe)
		ret = smu->ppt_funcs->notify_smu_enable_pwe(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_xgmi_pstate(struct smu_context *smu,
			uint32_t pstate)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_xgmi_pstate)
		ret = smu->ppt_funcs->set_xgmi_pstate(smu, pstate);

	mutex_unlock(&smu->mutex);

	if(ret)
		dev_err(smu->adev->dev, "Failed to set XGMI pstate!\n");

	return ret;
}

int smu_set_azalia_d3_pme(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_azalia_d3_pme)
		ret = smu->ppt_funcs->set_azalia_d3_pme(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

/*
 * On system suspending or resetting, the dpm_enabled
 * flag will be cleared. So that those SMU services which
 * are not supported will be gated.
 *
 * However, the baco/mode1 reset should still be granted
 * as they are still supported and necessary.
 */
bool smu_baco_is_support(struct smu_context *smu)
{
	bool ret = false;

	if (!smu->pm_enabled)
		return false;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs && smu->ppt_funcs->baco_is_support)
		ret = smu->ppt_funcs->baco_is_support(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_baco_get_state(struct smu_context *smu, enum smu_baco_state *state)
{
	if (smu->ppt_funcs->baco_get_state)
		return -EINVAL;

	mutex_lock(&smu->mutex);
	*state = smu->ppt_funcs->baco_get_state(smu);
	mutex_unlock(&smu->mutex);

	return 0;
}

int smu_baco_enter(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->baco_enter)
		ret = smu->ppt_funcs->baco_enter(smu);

	mutex_unlock(&smu->mutex);

	if (ret)
		dev_err(smu->adev->dev, "Failed to enter BACO state!\n");

	return ret;
}

int smu_baco_exit(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->baco_exit)
		ret = smu->ppt_funcs->baco_exit(smu);

	mutex_unlock(&smu->mutex);

	if (ret)
		dev_err(smu->adev->dev, "Failed to exit BACO state!\n");

	return ret;
}

bool smu_mode1_reset_is_support(struct smu_context *smu)
{
	bool ret = false;

	if (!smu->pm_enabled)
		return false;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs && smu->ppt_funcs->mode1_reset_is_support)
		ret = smu->ppt_funcs->mode1_reset_is_support(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_mode1_reset(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->mode1_reset)
		ret = smu->ppt_funcs->mode1_reset(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_mode2_reset(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->mode2_reset)
		ret = smu->ppt_funcs->mode2_reset(smu);

	mutex_unlock(&smu->mutex);

	if (ret)
		dev_err(smu->adev->dev, "Mode2 reset failed!\n");

	return ret;
}

int smu_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
					 struct pp_smu_nv_clock_table *max_clocks)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_max_sustainable_clocks_by_dc)
		ret = smu->ppt_funcs->get_max_sustainable_clocks_by_dc(smu, max_clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_uclk_dpm_states(struct smu_context *smu,
			    unsigned int *clock_values_in_khz,
			    unsigned int *num_states)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_uclk_dpm_states)
		ret = smu->ppt_funcs->get_uclk_dpm_states(smu, clock_values_in_khz, num_states);

	mutex_unlock(&smu->mutex);

	return ret;
}

enum amd_pm_state_type smu_get_current_power_state(struct smu_context *smu)
{
	enum amd_pm_state_type pm_state = POWER_STATE_TYPE_DEFAULT;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_current_power_state)
		pm_state = smu->ppt_funcs->get_current_power_state(smu);

	mutex_unlock(&smu->mutex);

	return pm_state;
}

int smu_get_dpm_clock_table(struct smu_context *smu,
			    struct dpm_clocks *clock_table)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_dpm_clock_table)
		ret = smu->ppt_funcs->get_dpm_clock_table(smu, clock_table);

	mutex_unlock(&smu->mutex);

	return ret;
}

ssize_t smu_sys_get_gpu_metrics(struct smu_context *smu,
				void **table)
{
	ssize_t size;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	if (!smu->ppt_funcs->get_gpu_metrics)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	size = smu->ppt_funcs->get_gpu_metrics(smu, table);

	mutex_unlock(&smu->mutex);

	return size;
}

int smu_enable_mgpu_fan_boost(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled || !smu->adev->pm.dpm_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->enable_mgpu_fan_boost)
		ret = smu->ppt_funcs->enable_mgpu_fan_boost(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}
