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

#include "pp_debug.h"
#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "soc15_common.h"
#include "smu_v11_0.h"
#include "atom.h"

static int smu_set_funcs(struct amdgpu_device *adev)
{
	struct smu_context *smu = &adev->smu;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		smu_v11_0_set_smu_funcs(smu);
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
	int ret;

	ret = smu_set_funcs(adev);
	if (ret)
		return ret;

	smu->adev = adev;
	mutex_init(&smu->mutex);

	return 0;
}

int smu_get_atom_data_table(struct smu_context *smu, uint32_t table,
			    uint16_t *size, uint8_t *frev, uint8_t *crev,
			    uint8_t **addr)
{
	struct amdgpu_device *adev = smu->adev;
	uint16_t data_start;

	if (!amdgpu_atom_parse_data_header(adev->mode_info.atom_context, table,
					   size, frev, crev, &data_start))
		return -EINVAL;

	*addr = (uint8_t *)adev->mode_info.atom_context->bios + data_start;

	return 0;
}

static int smu_initialize_pptable(struct smu_context *smu)
{
	/* TODO */
	return 0;
}

static int smu_smc_table_sw_init(struct smu_context *smu)
{
	int ret;

	ret = smu_initialize_pptable(smu);
	if (ret) {
		pr_err("Failed to init smu_initialize_pptable!\n");
		return ret;
	}

	/**
	 * Create smu_table structure, and init smc tables such as
	 * TABLE_PPTABLE, TABLE_WATERMARKS, TABLE_SMU_METRICS, and etc.
	 */
	ret = smu_init_smc_tables(smu);
	if (ret) {
		pr_err("Failed to init smc tables!\n");
		return ret;
	}

	/**
	 * Create smu_power_context structure, and allocate smu_dpm_context and
	 * context size to fill the smu_power_context data.
	 */
	ret = smu_init_power(smu);
	if (ret) {
		pr_err("Failed to init smu_init_power!\n");
		return ret;
	}

	return 0;
}

static int smu_smc_table_sw_fini(struct smu_context *smu)
{
	int ret;

	ret = smu_fini_smc_tables(smu);
	if (ret) {
		pr_err("Failed to smu_fini_smc_tables!\n");
		return ret;
	}

	return 0;
}

static int smu_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	smu->pool_size = adev->pm.smu_prv_buffer_size;

	ret = smu_init_microcode(smu);
	if (ret) {
		pr_err("Failed to load smu firmware!\n");
		return ret;
	}

	ret = smu_smc_table_sw_init(smu);
	if (ret) {
		pr_err("Failed to sw init smc table!\n");
		return ret;
	}

	return 0;
}

static int smu_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	ret = smu_smc_table_sw_fini(smu);
	if (ret) {
		pr_err("Failed to sw fini smc table!\n");
		return ret;
	}

	ret = smu_fini_power(smu);
	if (ret) {
		pr_err("Failed to init smu_fini_power!\n");
		return ret;
	}

	return 0;
}

static int smu_init_fb_allocations(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	uint32_t table_count = smu_table->table_count;
	uint32_t i = 0;
	int32_t ret = 0;

	if (table_count <= 0)
		return -EINVAL;

	for (i = 0 ; i < table_count; i++) {
		if (tables[i].size == 0)
			continue;
		ret = amdgpu_bo_create_kernel(adev,
					      tables[i].size,
					      tables[i].align,
					      tables[i].domain,
					      &tables[i].bo,
					      &tables[i].mc_address,
					      &tables[i].cpu_addr);
		if (ret)
			goto failed;
	}

	return 0;
failed:
	for (; i > 0; i--) {
		if (tables[i].size == 0)
			continue;
		amdgpu_bo_free_kernel(&tables[i].bo,
				      &tables[i].mc_address,
				      &tables[i].cpu_addr);

	}
	return ret;
}

static int smu_fini_fb_allocations(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	uint32_t table_count = smu_table->table_count;
	uint32_t i = 0;

	if (table_count == 0 || tables == NULL)
		return -EINVAL;

	for (i = 0 ; i < table_count; i++) {
		if (tables[i].size == 0)
			continue;
		amdgpu_bo_free_kernel(&tables[i].bo,
				      &tables[i].mc_address,
				      &tables[i].cpu_addr);
	}

	return 0;
}
static int smu_smc_table_hw_init(struct smu_context *smu)
{
	int ret;

	ret = smu_read_pptable_from_vbios(smu);
	if (ret)
		return ret;

	/* get boot_values from vbios to set revision, gfxclk, and etc. */
	ret = smu_get_vbios_bootup_values(smu);
	if (ret)
		return ret;

	ret = smu_get_clk_info_from_vbios(smu);
	if (ret)
		return ret;

	/*
	 * check if the format_revision in vbios is up to pptable header
	 * version, and the structure size is not 0.
	 */
	ret = smu_get_clk_info_from_vbios(smu);
	if (ret)
		return ret;

	ret = smu_check_pptable(smu);
	if (ret)
		return ret;

	/*
	 * allocate vram bos to store smc table contents.
	 */
	ret = smu_init_fb_allocations(smu);
	if (ret)
		return ret;

	/*
	 * Parse pptable format and fill PPTable_t smc_pptable to
	 * smu_table_context structure. And read the smc_dpm_table from vbios,
	 * then fill it into smc_pptable.
	 */
	ret = smu_parse_pptable(smu);
	if (ret)
		return ret;

	/*
	 * Set initialized values (get from vbios) to dpm tables context such as
	 * gfxclk, memclk, dcefclk, and etc. And enable the DPM feature for each
	 * type of clks.
	 */
	ret = smu_populate_smc_pptable(smu);
	if (ret)
		return ret;

	/*
	 * Send msg GetDriverIfVersion to check if the return value is equal
	 * with DRIVER_IF_VERSION of smc header.
	 */
	ret = smu_check_fw_version(smu);
	if (ret)
		return ret;

	/*
	 * Copy pptable bo in the vram to smc with SMU MSGs such as
	 * SetDriverDramAddr and TransferTableDram2Smu.
	 */
	ret = smu_write_pptable(smu);
	if (ret)
		return ret;

	/*
	 * Set min deep sleep dce fclk with bootup value from vbios via
	 * SetMinDeepSleepDcefclk MSG.
	 */
	ret = smu_set_min_dcef_deep_sleep(smu);
	if (ret)
		return ret;

	/*
	 * Set PMSTATUSLOG table bo address with SetToolsDramAddr MSG for tools.
	 */
	ret = smu_set_tool_table_location(smu);

	return ret;
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
	int ret = 0;

	if (memory_pool->size == SMU_MEMORY_POOL_SIZE_ZERO)
		return ret;

	amdgpu_bo_free_kernel(&memory_pool->bo,
			      &memory_pool->mc_address,
			      &memory_pool->cpu_addr);

	memset(memory_pool, 0, sizeof(struct smu_table));

	return ret;
}
static int smu_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		ret = smu_load_microcode(smu);
		if (ret)
			return ret;
	}

	ret = smu_check_fw_status(smu);
	if (ret) {
		pr_err("SMC firmware status is not correct\n");
		return ret;
	}

	mutex_lock(&smu->mutex);

	ret = smu_smc_table_hw_init(smu);
	if (ret)
		goto failed;

	ret = smu_alloc_memory_pool(smu);
	if (ret)
		goto failed;

	/*
	 * Use msg SetSystemVirtualDramAddr and DramLogSetDramAddr can notify
	 * pool location.
	 */
	ret = smu_notify_memory_pool_location(smu);
	if (ret)
		goto failed;

	mutex_unlock(&smu->mutex);

	pr_info("SMU is initialized successfully!\n");

	return 0;

failed:
	mutex_unlock(&smu->mutex);
	return ret;
}

static int smu_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret = 0;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	ret = smu_fini_fb_allocations(smu);
	if (ret)
		return ret;

	ret = smu_free_memory_pool(smu);
	if (ret)
		return ret;

	return 0;
}

static int smu_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	return 0;
}

static int smu_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	pr_info("SMU is resuming...\n");

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		ret = smu_load_microcode(smu);
		if (ret)
			return ret;
	}

	ret = smu_check_fw_status(smu);
	if (ret) {
		pr_err("SMC firmware status is not correct\n");
		return ret;
	}

	mutex_lock(&smu->mutex);

	ret = smu_set_tool_table_location(smu);
	if (ret)
		goto failed;

	ret = smu_write_pptable(smu);
	if (ret)
		goto failed;

	ret = smu_write_watermarks_table(smu);
	if (ret)
		goto failed;

	ret = smu_set_last_dcef_min_deep_sleep_clk(smu);
	if (ret)
		goto failed;

	ret = smu_system_features_control(smu, true);
	if (ret)
		goto failed;

	mutex_unlock(&smu->mutex);

	pr_info("SMU is resumed successfully!\n");

	return 0;
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

const struct amd_ip_funcs smu_ip_funcs = {
	.name = "smu",
	.early_init = smu_early_init,
	.late_init = NULL,
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
};

const struct amdgpu_ip_block_version smu_v11_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 11,
	.minor = 0,
	.rev = 0,
	.funcs = &smu_ip_funcs,
};
