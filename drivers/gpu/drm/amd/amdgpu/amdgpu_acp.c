/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include <linux/irqdomain.h>
#include <linux/pm_domain.h>
#include <linux/platform_device.h>
#include <sound/designware_i2s.h>
#include <sound/pcm.h>

#include "amdgpu.h"
#include "atom.h"
#include "amdgpu_acp.h"

#include "acp_gfx_if.h"

#define ACP_TILE_ON_MASK                	0x03
#define ACP_TILE_OFF_MASK               	0x02
#define ACP_TILE_ON_RETAIN_REG_MASK     	0x1f
#define ACP_TILE_OFF_RETAIN_REG_MASK    	0x20

#define ACP_TILE_P1_MASK                	0x3e
#define ACP_TILE_P2_MASK                	0x3d
#define ACP_TILE_DSP0_MASK              	0x3b
#define ACP_TILE_DSP1_MASK              	0x37

#define ACP_TILE_DSP2_MASK              	0x2f

#define ACP_DMA_REGS_END			0x146c0
#define ACP_I2S_PLAY_REGS_START			0x14840
#define ACP_I2S_PLAY_REGS_END			0x148b4
#define ACP_I2S_CAP_REGS_START			0x148b8
#define ACP_I2S_CAP_REGS_END			0x1496c

#define ACP_I2S_COMP1_CAP_REG_OFFSET		0xac
#define ACP_I2S_COMP2_CAP_REG_OFFSET		0xa8
#define ACP_I2S_COMP1_PLAY_REG_OFFSET		0x6c
#define ACP_I2S_COMP2_PLAY_REG_OFFSET		0x68
#define ACP_BT_PLAY_REGS_START			0x14970
#define ACP_BT_PLAY_REGS_END			0x14a24
#define ACP_BT_COMP1_REG_OFFSET			0xac
#define ACP_BT_COMP2_REG_OFFSET			0xa8

#define mmACP_PGFSM_RETAIN_REG			0x51c9
#define mmACP_PGFSM_CONFIG_REG			0x51ca
#define mmACP_PGFSM_READ_REG_0			0x51cc

#define mmACP_MEM_SHUT_DOWN_REQ_LO		0x51f8
#define mmACP_MEM_SHUT_DOWN_REQ_HI		0x51f9
#define mmACP_MEM_SHUT_DOWN_STS_LO		0x51fa
#define mmACP_MEM_SHUT_DOWN_STS_HI		0x51fb

#define mmACP_CONTROL				0x5131
#define mmACP_STATUS				0x5133
#define mmACP_SOFT_RESET			0x5134
#define ACP_CONTROL__ClkEn_MASK 		0x1
#define ACP_SOFT_RESET__SoftResetAud_MASK 	0x100
#define ACP_SOFT_RESET__SoftResetAudDone_MASK	0x1000000
#define ACP_CLOCK_EN_TIME_OUT_VALUE		0x000000FF
#define ACP_SOFT_RESET_DONE_TIME_OUT_VALUE	0x000000FF

#define ACP_TIMEOUT_LOOP			0x000000FF
#define ACP_DEVS				4
#define ACP_SRC_ID				162

enum {
	ACP_TILE_P1 = 0,
	ACP_TILE_P2,
	ACP_TILE_DSP0,
	ACP_TILE_DSP1,
	ACP_TILE_DSP2,
};

static int acp_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->acp.parent = adev->dev;

	adev->acp.cgs_device =
		amdgpu_cgs_create_device(adev);
	if (!adev->acp.cgs_device)
		return -EINVAL;

	return 0;
}

static int acp_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->acp.cgs_device)
		amdgpu_cgs_destroy_device(adev->acp.cgs_device);

	return 0;
}

struct acp_pm_domain {
	void *adev;
	struct generic_pm_domain gpd;
};

static int acp_poweroff(struct generic_pm_domain *genpd)
{
	struct acp_pm_domain *apd;
	struct amdgpu_device *adev;

	apd = container_of(genpd, struct acp_pm_domain, gpd);
	if (apd != NULL) {
		adev = apd->adev;
	/* call smu to POWER GATE ACP block
	 * smu will
	 * 1. turn off the acp clock
	 * 2. power off the acp tiles
	 * 3. check and enter ulv state
	 */
		if (adev->powerplay.pp_funcs->set_powergating_by_smu)
			amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_ACP, true);
	}
	return 0;
}

static int acp_poweron(struct generic_pm_domain *genpd)
{
	struct acp_pm_domain *apd;
	struct amdgpu_device *adev;

	apd = container_of(genpd, struct acp_pm_domain, gpd);
	if (apd != NULL) {
		adev = apd->adev;
	/* call smu to UNGATE ACP block
	 * smu will
	 * 1. exit ulv
	 * 2. turn on acp clock
	 * 3. power on acp tiles
	 */
		if (adev->powerplay.pp_funcs->set_powergating_by_smu)
			amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_ACP, false);
	}
	return 0;
}

static struct device *get_mfd_cell_dev(const char *device_name, int r)
{
	char auto_dev_name[25];
	struct device *dev;

	snprintf(auto_dev_name, sizeof(auto_dev_name),
		 "%s.%d.auto", device_name, r);
	dev = bus_find_device_by_name(&platform_bus_type, NULL, auto_dev_name);
	dev_info(dev, "device %s added to pm domain\n", auto_dev_name);

	return dev;
}

/**
 * acp_hw_init - start and test ACP block
 *
 * @adev: amdgpu_device pointer
 *
 */
static int acp_hw_init(void *handle)
{
	int r, i;
	uint64_t acp_base;
	u32 val = 0;
	u32 count = 0;
	struct device *dev;
	struct i2s_platform_data *i2s_pdata;

	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	const struct amdgpu_ip_block *ip_block =
		amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_ACP);

	if (!ip_block)
		return -EINVAL;

	r = amd_acp_hw_init(adev->acp.cgs_device,
			    ip_block->version->major, ip_block->version->minor);
	/* -ENODEV means board uses AZ rather than ACP */
	if (r == -ENODEV) {
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_ACP, true);
		return 0;
	} else if (r) {
		return r;
	}

	if (adev->rmmio_size == 0 || adev->rmmio_size < 0x5289)
		return -EINVAL;

	acp_base = adev->rmmio_base;


	adev->acp.acp_genpd = kzalloc(sizeof(struct acp_pm_domain), GFP_KERNEL);
	if (adev->acp.acp_genpd == NULL)
		return -ENOMEM;

	adev->acp.acp_genpd->gpd.name = "ACP_AUDIO";
	adev->acp.acp_genpd->gpd.power_off = acp_poweroff;
	adev->acp.acp_genpd->gpd.power_on = acp_poweron;


	adev->acp.acp_genpd->adev = adev;

	pm_genpd_init(&adev->acp.acp_genpd->gpd, NULL, false);

	adev->acp.acp_cell = kcalloc(ACP_DEVS, sizeof(struct mfd_cell),
							GFP_KERNEL);

	if (adev->acp.acp_cell == NULL)
		return -ENOMEM;

	adev->acp.acp_res = kcalloc(5, sizeof(struct resource), GFP_KERNEL);
	if (adev->acp.acp_res == NULL) {
		kfree(adev->acp.acp_cell);
		return -ENOMEM;
	}

	i2s_pdata = kcalloc(3, sizeof(struct i2s_platform_data), GFP_KERNEL);
	if (i2s_pdata == NULL) {
		kfree(adev->acp.acp_res);
		kfree(adev->acp.acp_cell);
		return -ENOMEM;
	}

	switch (adev->asic_type) {
	case CHIP_STONEY:
		i2s_pdata[0].quirks = DW_I2S_QUIRK_COMP_REG_OFFSET |
			DW_I2S_QUIRK_16BIT_IDX_OVERRIDE;
		break;
	default:
		i2s_pdata[0].quirks = DW_I2S_QUIRK_COMP_REG_OFFSET;
	}
	i2s_pdata[0].cap = DWC_I2S_PLAY;
	i2s_pdata[0].snd_rates = SNDRV_PCM_RATE_8000_96000;
	i2s_pdata[0].i2s_reg_comp1 = ACP_I2S_COMP1_PLAY_REG_OFFSET;
	i2s_pdata[0].i2s_reg_comp2 = ACP_I2S_COMP2_PLAY_REG_OFFSET;
	switch (adev->asic_type) {
	case CHIP_STONEY:
		i2s_pdata[1].quirks = DW_I2S_QUIRK_COMP_REG_OFFSET |
			DW_I2S_QUIRK_COMP_PARAM1 |
			DW_I2S_QUIRK_16BIT_IDX_OVERRIDE;
		break;
	default:
		i2s_pdata[1].quirks = DW_I2S_QUIRK_COMP_REG_OFFSET |
			DW_I2S_QUIRK_COMP_PARAM1;
	}

	i2s_pdata[1].cap = DWC_I2S_RECORD;
	i2s_pdata[1].snd_rates = SNDRV_PCM_RATE_8000_96000;
	i2s_pdata[1].i2s_reg_comp1 = ACP_I2S_COMP1_CAP_REG_OFFSET;
	i2s_pdata[1].i2s_reg_comp2 = ACP_I2S_COMP2_CAP_REG_OFFSET;

	i2s_pdata[2].quirks = DW_I2S_QUIRK_COMP_REG_OFFSET;
	switch (adev->asic_type) {
	case CHIP_STONEY:
		i2s_pdata[2].quirks |= DW_I2S_QUIRK_16BIT_IDX_OVERRIDE;
		break;
	default:
		break;
	}

	i2s_pdata[2].cap = DWC_I2S_PLAY | DWC_I2S_RECORD;
	i2s_pdata[2].snd_rates = SNDRV_PCM_RATE_8000_96000;
	i2s_pdata[2].i2s_reg_comp1 = ACP_BT_COMP1_REG_OFFSET;
	i2s_pdata[2].i2s_reg_comp2 = ACP_BT_COMP2_REG_OFFSET;

	adev->acp.acp_res[0].name = "acp2x_dma";
	adev->acp.acp_res[0].flags = IORESOURCE_MEM;
	adev->acp.acp_res[0].start = acp_base;
	adev->acp.acp_res[0].end = acp_base + ACP_DMA_REGS_END;

	adev->acp.acp_res[1].name = "acp2x_dw_i2s_play";
	adev->acp.acp_res[1].flags = IORESOURCE_MEM;
	adev->acp.acp_res[1].start = acp_base + ACP_I2S_PLAY_REGS_START;
	adev->acp.acp_res[1].end = acp_base + ACP_I2S_PLAY_REGS_END;

	adev->acp.acp_res[2].name = "acp2x_dw_i2s_cap";
	adev->acp.acp_res[2].flags = IORESOURCE_MEM;
	adev->acp.acp_res[2].start = acp_base + ACP_I2S_CAP_REGS_START;
	adev->acp.acp_res[2].end = acp_base + ACP_I2S_CAP_REGS_END;

	adev->acp.acp_res[3].name = "acp2x_dw_bt_i2s_play_cap";
	adev->acp.acp_res[3].flags = IORESOURCE_MEM;
	adev->acp.acp_res[3].start = acp_base + ACP_BT_PLAY_REGS_START;
	adev->acp.acp_res[3].end = acp_base + ACP_BT_PLAY_REGS_END;

	adev->acp.acp_res[4].name = "acp2x_dma_irq";
	adev->acp.acp_res[4].flags = IORESOURCE_IRQ;
	adev->acp.acp_res[4].start = amdgpu_irq_create_mapping(adev, 162);
	adev->acp.acp_res[4].end = adev->acp.acp_res[4].start;

	adev->acp.acp_cell[0].name = "acp_audio_dma";
	adev->acp.acp_cell[0].num_resources = 5;
	adev->acp.acp_cell[0].resources = &adev->acp.acp_res[0];
	adev->acp.acp_cell[0].platform_data = &adev->asic_type;
	adev->acp.acp_cell[0].pdata_size = sizeof(adev->asic_type);

	adev->acp.acp_cell[1].name = "designware-i2s";
	adev->acp.acp_cell[1].num_resources = 1;
	adev->acp.acp_cell[1].resources = &adev->acp.acp_res[1];
	adev->acp.acp_cell[1].platform_data = &i2s_pdata[0];
	adev->acp.acp_cell[1].pdata_size = sizeof(struct i2s_platform_data);

	adev->acp.acp_cell[2].name = "designware-i2s";
	adev->acp.acp_cell[2].num_resources = 1;
	adev->acp.acp_cell[2].resources = &adev->acp.acp_res[2];
	adev->acp.acp_cell[2].platform_data = &i2s_pdata[1];
	adev->acp.acp_cell[2].pdata_size = sizeof(struct i2s_platform_data);

	adev->acp.acp_cell[3].name = "designware-i2s";
	adev->acp.acp_cell[3].num_resources = 1;
	adev->acp.acp_cell[3].resources = &adev->acp.acp_res[3];
	adev->acp.acp_cell[3].platform_data = &i2s_pdata[2];
	adev->acp.acp_cell[3].pdata_size = sizeof(struct i2s_platform_data);

	r = mfd_add_hotplug_devices(adev->acp.parent, adev->acp.acp_cell,
								ACP_DEVS);
	if (r)
		return r;

	for (i = 0; i < ACP_DEVS ; i++) {
		dev = get_mfd_cell_dev(adev->acp.acp_cell[i].name, i);
		r = pm_genpd_add_device(&adev->acp.acp_genpd->gpd, dev);
		if (r) {
			dev_err(dev, "Failed to add dev to genpd\n");
			return r;
		}
	}


	/* Assert Soft reset of ACP */
	val = cgs_read_register(adev->acp.cgs_device, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	cgs_write_register(adev->acp.cgs_device, mmACP_SOFT_RESET, val);

	count = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (true) {
		val = cgs_read_register(adev->acp.cgs_device, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK))
			break;
		if (--count == 0) {
			dev_err(&adev->pdev->dev, "Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}
	/* Enable clock to ACP and wait until the clock is enabled */
	val = cgs_read_register(adev->acp.cgs_device, mmACP_CONTROL);
	val = val | ACP_CONTROL__ClkEn_MASK;
	cgs_write_register(adev->acp.cgs_device, mmACP_CONTROL, val);

	count = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (true) {
		val = cgs_read_register(adev->acp.cgs_device, mmACP_STATUS);
		if (val & (u32) 0x1)
			break;
		if (--count == 0) {
			dev_err(&adev->pdev->dev, "Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}
	/* Deassert the SOFT RESET flags */
	val = cgs_read_register(adev->acp.cgs_device, mmACP_SOFT_RESET);
	val &= ~ACP_SOFT_RESET__SoftResetAud_MASK;
	cgs_write_register(adev->acp.cgs_device, mmACP_SOFT_RESET, val);
	return 0;
}

/**
 * acp_hw_fini - stop the hardware block
 *
 * @adev: amdgpu_device pointer
 *
 */
static int acp_hw_fini(void *handle)
{
	int i, ret;
	u32 val = 0;
	u32 count = 0;
	struct device *dev;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* return early if no ACP */
	if (!adev->acp.acp_genpd) {
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_ACP, false);
		return 0;
	}

	/* Assert Soft reset of ACP */
	val = cgs_read_register(adev->acp.cgs_device, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	cgs_write_register(adev->acp.cgs_device, mmACP_SOFT_RESET, val);

	count = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (true) {
		val = cgs_read_register(adev->acp.cgs_device, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK))
			break;
		if (--count == 0) {
			dev_err(&adev->pdev->dev, "Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}
	/* Disable ACP clock */
	val = cgs_read_register(adev->acp.cgs_device, mmACP_CONTROL);
	val &= ~ACP_CONTROL__ClkEn_MASK;
	cgs_write_register(adev->acp.cgs_device, mmACP_CONTROL, val);

	count = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (true) {
		val = cgs_read_register(adev->acp.cgs_device, mmACP_STATUS);
		if (val & (u32) 0x1)
			break;
		if (--count == 0) {
			dev_err(&adev->pdev->dev, "Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}

	for (i = 0; i < ACP_DEVS ; i++) {
		dev = get_mfd_cell_dev(adev->acp.acp_cell[i].name, i);
		ret = pm_genpd_remove_device(dev);
		/* If removal fails, dont giveup and try rest */
		if (ret)
			dev_err(dev, "remove dev from genpd failed\n");
	}

	mfd_remove_devices(adev->acp.parent);
	kfree(adev->acp.acp_res);
	kfree(adev->acp.acp_genpd);
	kfree(adev->acp.acp_cell);

	return 0;
}

static int acp_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* power up on suspend */
	if (!adev->acp.acp_cell)
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_ACP, false);
	return 0;
}

static int acp_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* power down again on resume */
	if (!adev->acp.acp_cell)
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_ACP, true);
	return 0;
}

static int acp_early_init(void *handle)
{
	return 0;
}

static bool acp_is_idle(void *handle)
{
	return true;
}

static int acp_wait_for_idle(void *handle)
{
	return 0;
}

static int acp_soft_reset(void *handle)
{
	return 0;
}

static int acp_set_clockgating_state(void *handle,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int acp_set_powergating_state(void *handle,
				     enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = state == AMD_PG_STATE_GATE ? true : false;

	if (adev->powerplay.pp_funcs->set_powergating_by_smu)
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_ACP, enable);

	return 0;
}

static const struct amd_ip_funcs acp_ip_funcs = {
	.name = "acp_ip",
	.early_init = acp_early_init,
	.late_init = NULL,
	.sw_init = acp_sw_init,
	.sw_fini = acp_sw_fini,
	.hw_init = acp_hw_init,
	.hw_fini = acp_hw_fini,
	.suspend = acp_suspend,
	.resume = acp_resume,
	.is_idle = acp_is_idle,
	.wait_for_idle = acp_wait_for_idle,
	.soft_reset = acp_soft_reset,
	.set_clockgating_state = acp_set_clockgating_state,
	.set_powergating_state = acp_set_powergating_state,
};

const struct amdgpu_ip_block_version acp_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_ACP,
	.major = 2,
	.minor = 2,
	.rev = 0,
	.funcs = &acp_ip_funcs,
};
