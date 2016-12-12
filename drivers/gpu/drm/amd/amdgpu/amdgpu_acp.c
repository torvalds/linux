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

#define ACP_TILE_ON_MASK                0x03
#define ACP_TILE_OFF_MASK               0x02
#define ACP_TILE_ON_RETAIN_REG_MASK     0x1f
#define ACP_TILE_OFF_RETAIN_REG_MASK    0x20

#define ACP_TILE_P1_MASK                0x3e
#define ACP_TILE_P2_MASK                0x3d
#define ACP_TILE_DSP0_MASK              0x3b
#define ACP_TILE_DSP1_MASK              0x37

#define ACP_TILE_DSP2_MASK              0x2f

#define ACP_DMA_REGS_END		0x146c0
#define ACP_I2S_PLAY_REGS_START		0x14840
#define ACP_I2S_PLAY_REGS_END		0x148b4
#define ACP_I2S_CAP_REGS_START		0x148b8
#define ACP_I2S_CAP_REGS_END		0x1496c

#define ACP_I2S_COMP1_CAP_REG_OFFSET	0xac
#define ACP_I2S_COMP2_CAP_REG_OFFSET	0xa8
#define ACP_I2S_COMP1_PLAY_REG_OFFSET	0x6c
#define ACP_I2S_COMP2_PLAY_REG_OFFSET	0x68

#define mmACP_PGFSM_RETAIN_REG		0x51c9
#define mmACP_PGFSM_CONFIG_REG		0x51ca
#define mmACP_PGFSM_READ_REG_0		0x51cc

#define mmACP_MEM_SHUT_DOWN_REQ_LO	0x51f8
#define mmACP_MEM_SHUT_DOWN_REQ_HI	0x51f9
#define mmACP_MEM_SHUT_DOWN_STS_LO	0x51fa
#define mmACP_MEM_SHUT_DOWN_STS_HI	0x51fb

#define ACP_TIMEOUT_LOOP		0x000000FF
#define ACP_DEVS			3
#define ACP_SRC_ID			162

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

/* power off a tile/block within ACP */
static int acp_suspend_tile(void *cgs_dev, int tile)
{
	u32 val = 0;
	u32 count = 0;

	if ((tile  < ACP_TILE_P1) || (tile > ACP_TILE_DSP2)) {
		pr_err("Invalid ACP tile : %d to suspend\n", tile);
		return -1;
	}

	val = cgs_read_register(cgs_dev, mmACP_PGFSM_READ_REG_0 + tile);
	val &= ACP_TILE_ON_MASK;

	if (val == 0x0) {
		val = cgs_read_register(cgs_dev, mmACP_PGFSM_RETAIN_REG);
		val = val | (1 << tile);
		cgs_write_register(cgs_dev, mmACP_PGFSM_RETAIN_REG, val);
		cgs_write_register(cgs_dev, mmACP_PGFSM_CONFIG_REG,
					0x500 + tile);

		count = ACP_TIMEOUT_LOOP;
		while (true) {
			val = cgs_read_register(cgs_dev, mmACP_PGFSM_READ_REG_0
								+ tile);
			val = val & ACP_TILE_ON_MASK;
			if (val == ACP_TILE_OFF_MASK)
				break;
			if (--count == 0) {
				pr_err("Timeout reading ACP PGFSM status\n");
				return -ETIMEDOUT;
			}
			udelay(100);
		}

		val = cgs_read_register(cgs_dev, mmACP_PGFSM_RETAIN_REG);

		val |= ACP_TILE_OFF_RETAIN_REG_MASK;
		cgs_write_register(cgs_dev, mmACP_PGFSM_RETAIN_REG, val);
	}
	return 0;
}

/* power on a tile/block within ACP */
static int acp_resume_tile(void *cgs_dev, int tile)
{
	u32 val = 0;
	u32 count = 0;

	if ((tile  < ACP_TILE_P1) || (tile > ACP_TILE_DSP2)) {
		pr_err("Invalid ACP tile to resume\n");
		return -1;
	}

	val = cgs_read_register(cgs_dev, mmACP_PGFSM_READ_REG_0 + tile);
	val = val & ACP_TILE_ON_MASK;

	if (val != 0x0) {
		cgs_write_register(cgs_dev, mmACP_PGFSM_CONFIG_REG,
					0x600 + tile);
		count = ACP_TIMEOUT_LOOP;
		while (true) {
			val = cgs_read_register(cgs_dev, mmACP_PGFSM_READ_REG_0
							+ tile);
			val = val & ACP_TILE_ON_MASK;
			if (val == 0x0)
				break;
			if (--count == 0) {
				pr_err("Timeout reading ACP PGFSM status\n");
				return -ETIMEDOUT;
			}
			udelay(100);
		}
		val = cgs_read_register(cgs_dev, mmACP_PGFSM_RETAIN_REG);
		if (tile == ACP_TILE_P1)
			val = val & (ACP_TILE_P1_MASK);
		else if (tile == ACP_TILE_P2)
			val = val & (ACP_TILE_P2_MASK);

		cgs_write_register(cgs_dev, mmACP_PGFSM_RETAIN_REG, val);
	}
	return 0;
}

struct acp_pm_domain {
	void *cgs_dev;
	struct generic_pm_domain gpd;
};

static int acp_poweroff(struct generic_pm_domain *genpd)
{
	int i, ret;
	struct acp_pm_domain *apd;

	apd = container_of(genpd, struct acp_pm_domain, gpd);
	if (apd != NULL) {
		/* Donot return abruptly if any of power tile fails to suspend.
		 * Log it and continue powering off other tile
		 */
		for (i = 4; i >= 0 ; i--) {
			ret = acp_suspend_tile(apd->cgs_dev, ACP_TILE_P1 + i);
			if (ret)
				pr_err("ACP tile %d tile suspend failed\n", i);
		}
	}
	return 0;
}

static int acp_poweron(struct generic_pm_domain *genpd)
{
	int i, ret;
	struct acp_pm_domain *apd;

	apd = container_of(genpd, struct acp_pm_domain, gpd);
	if (apd != NULL) {
		for (i = 0; i < 2; i++) {
			ret = acp_resume_tile(apd->cgs_dev, ACP_TILE_P1 + i);
			if (ret) {
				pr_err("ACP tile %d resume failed\n", i);
				break;
			}
		}

		/* Disable DSPs which are not going to be used */
		for (i = 0; i < 3; i++) {
			ret = acp_suspend_tile(apd->cgs_dev, ACP_TILE_DSP0 + i);
			/* Continue suspending other DSP, even if one fails */
			if (ret)
				pr_err("ACP DSP %d suspend failed\n", i);
		}
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
	struct device *dev;
	struct i2s_platform_data *i2s_pdata;

	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	const struct amdgpu_ip_block_version *ip_version =
		amdgpu_get_ip_block(adev, AMD_IP_BLOCK_TYPE_ACP);

	if (!ip_version)
		return -EINVAL;

	r = amd_acp_hw_init(adev->acp.cgs_device,
			    ip_version->major, ip_version->minor);
	/* -ENODEV means board uses AZ rather than ACP */
	if (r == -ENODEV)
		return 0;
	else if (r)
		return r;

	r = cgs_get_pci_resource(adev->acp.cgs_device, CGS_RESOURCE_TYPE_MMIO,
			0x5289, 0, &acp_base);
	if (r == -ENODEV)
		return 0;
	else if (r)
		return r;

	adev->acp.acp_genpd = kzalloc(sizeof(struct acp_pm_domain), GFP_KERNEL);
	if (adev->acp.acp_genpd == NULL)
		return -ENOMEM;

	adev->acp.acp_genpd->gpd.name = "ACP_AUDIO";
	adev->acp.acp_genpd->gpd.power_off = acp_poweroff;
	adev->acp.acp_genpd->gpd.power_on = acp_poweron;


	adev->acp.acp_genpd->cgs_dev = adev->acp.cgs_device;

	pm_genpd_init(&adev->acp.acp_genpd->gpd, NULL, false);

	adev->acp.acp_cell = kzalloc(sizeof(struct mfd_cell) * ACP_DEVS,
							GFP_KERNEL);

	if (adev->acp.acp_cell == NULL)
		return -ENOMEM;

	adev->acp.acp_res = kzalloc(sizeof(struct resource) * 4, GFP_KERNEL);

	if (adev->acp.acp_res == NULL) {
		kfree(adev->acp.acp_cell);
		return -ENOMEM;
	}

	i2s_pdata = kzalloc(sizeof(struct i2s_platform_data) * 2, GFP_KERNEL);
	if (i2s_pdata == NULL) {
		kfree(adev->acp.acp_res);
		kfree(adev->acp.acp_cell);
		return -ENOMEM;
	}

	i2s_pdata[0].quirks = DW_I2S_QUIRK_COMP_REG_OFFSET;
	i2s_pdata[0].cap = DWC_I2S_PLAY;
	i2s_pdata[0].snd_rates = SNDRV_PCM_RATE_8000_96000;
	i2s_pdata[0].i2s_reg_comp1 = ACP_I2S_COMP1_PLAY_REG_OFFSET;
	i2s_pdata[0].i2s_reg_comp2 = ACP_I2S_COMP2_PLAY_REG_OFFSET;

	i2s_pdata[1].quirks = DW_I2S_QUIRK_COMP_REG_OFFSET |
				DW_I2S_QUIRK_COMP_PARAM1;
	i2s_pdata[1].cap = DWC_I2S_RECORD;
	i2s_pdata[1].snd_rates = SNDRV_PCM_RATE_8000_96000;
	i2s_pdata[1].i2s_reg_comp1 = ACP_I2S_COMP1_CAP_REG_OFFSET;
	i2s_pdata[1].i2s_reg_comp2 = ACP_I2S_COMP2_CAP_REG_OFFSET;

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

	adev->acp.acp_res[3].name = "acp2x_dma_irq";
	adev->acp.acp_res[3].flags = IORESOURCE_IRQ;
	adev->acp.acp_res[3].start = amdgpu_irq_create_mapping(adev, 162);
	adev->acp.acp_res[3].end = adev->acp.acp_res[3].start;

	adev->acp.acp_cell[0].name = "acp_audio_dma";
	adev->acp.acp_cell[0].num_resources = 4;
	adev->acp.acp_cell[0].resources = &adev->acp.acp_res[0];

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
	struct device *dev;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* return early if no ACP */
	if (!adev->acp.acp_genpd)
		return 0;

	for (i = 0; i < ACP_DEVS ; i++) {
		dev = get_mfd_cell_dev(adev->acp.acp_cell[i].name, i);
		ret = pm_genpd_remove_device(&adev->acp.acp_genpd->gpd, dev);
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
	return 0;
}

static int acp_resume(void *handle)
{
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
	return 0;
}

const struct amd_ip_funcs acp_ip_funcs = {
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
