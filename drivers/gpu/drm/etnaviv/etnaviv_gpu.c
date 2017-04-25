/*
 * Copyright (C) 2015 Etnaviv Project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/component.h>
#include <linux/dma-fence.h>
#include <linux/moduleparam.h>
#include <linux/of_device.h>

#include "etnaviv_cmdbuf.h"
#include "etnaviv_dump.h"
#include "etnaviv_gpu.h"
#include "etnaviv_gem.h"
#include "etnaviv_mmu.h"
#include "common.xml.h"
#include "state.xml.h"
#include "state_hi.xml.h"
#include "cmdstream.xml.h"

static const struct platform_device_id gpu_ids[] = {
	{ .name = "etnaviv-gpu,2d" },
	{ },
};

static bool etnaviv_dump_core = true;
module_param_named(dump_core, etnaviv_dump_core, bool, 0600);

/*
 * Driver functions:
 */

int etnaviv_gpu_get_param(struct etnaviv_gpu *gpu, u32 param, u64 *value)
{
	switch (param) {
	case ETNAVIV_PARAM_GPU_MODEL:
		*value = gpu->identity.model;
		break;

	case ETNAVIV_PARAM_GPU_REVISION:
		*value = gpu->identity.revision;
		break;

	case ETNAVIV_PARAM_GPU_FEATURES_0:
		*value = gpu->identity.features;
		break;

	case ETNAVIV_PARAM_GPU_FEATURES_1:
		*value = gpu->identity.minor_features0;
		break;

	case ETNAVIV_PARAM_GPU_FEATURES_2:
		*value = gpu->identity.minor_features1;
		break;

	case ETNAVIV_PARAM_GPU_FEATURES_3:
		*value = gpu->identity.minor_features2;
		break;

	case ETNAVIV_PARAM_GPU_FEATURES_4:
		*value = gpu->identity.minor_features3;
		break;

	case ETNAVIV_PARAM_GPU_FEATURES_5:
		*value = gpu->identity.minor_features4;
		break;

	case ETNAVIV_PARAM_GPU_FEATURES_6:
		*value = gpu->identity.minor_features5;
		break;

	case ETNAVIV_PARAM_GPU_STREAM_COUNT:
		*value = gpu->identity.stream_count;
		break;

	case ETNAVIV_PARAM_GPU_REGISTER_MAX:
		*value = gpu->identity.register_max;
		break;

	case ETNAVIV_PARAM_GPU_THREAD_COUNT:
		*value = gpu->identity.thread_count;
		break;

	case ETNAVIV_PARAM_GPU_VERTEX_CACHE_SIZE:
		*value = gpu->identity.vertex_cache_size;
		break;

	case ETNAVIV_PARAM_GPU_SHADER_CORE_COUNT:
		*value = gpu->identity.shader_core_count;
		break;

	case ETNAVIV_PARAM_GPU_PIXEL_PIPES:
		*value = gpu->identity.pixel_pipes;
		break;

	case ETNAVIV_PARAM_GPU_VERTEX_OUTPUT_BUFFER_SIZE:
		*value = gpu->identity.vertex_output_buffer_size;
		break;

	case ETNAVIV_PARAM_GPU_BUFFER_SIZE:
		*value = gpu->identity.buffer_size;
		break;

	case ETNAVIV_PARAM_GPU_INSTRUCTION_COUNT:
		*value = gpu->identity.instruction_count;
		break;

	case ETNAVIV_PARAM_GPU_NUM_CONSTANTS:
		*value = gpu->identity.num_constants;
		break;

	case ETNAVIV_PARAM_GPU_NUM_VARYINGS:
		*value = gpu->identity.varyings_count;
		break;

	default:
		DBG("%s: invalid param: %u", dev_name(gpu->dev), param);
		return -EINVAL;
	}

	return 0;
}


#define etnaviv_is_model_rev(gpu, mod, rev) \
	((gpu)->identity.model == chipModel_##mod && \
	 (gpu)->identity.revision == rev)
#define etnaviv_field(val, field) \
	(((val) & field##__MASK) >> field##__SHIFT)

static void etnaviv_hw_specs(struct etnaviv_gpu *gpu)
{
	if (gpu->identity.minor_features0 &
	    chipMinorFeatures0_MORE_MINOR_FEATURES) {
		u32 specs[4];
		unsigned int streams;

		specs[0] = gpu_read(gpu, VIVS_HI_CHIP_SPECS);
		specs[1] = gpu_read(gpu, VIVS_HI_CHIP_SPECS_2);
		specs[2] = gpu_read(gpu, VIVS_HI_CHIP_SPECS_3);
		specs[3] = gpu_read(gpu, VIVS_HI_CHIP_SPECS_4);

		gpu->identity.stream_count = etnaviv_field(specs[0],
					VIVS_HI_CHIP_SPECS_STREAM_COUNT);
		gpu->identity.register_max = etnaviv_field(specs[0],
					VIVS_HI_CHIP_SPECS_REGISTER_MAX);
		gpu->identity.thread_count = etnaviv_field(specs[0],
					VIVS_HI_CHIP_SPECS_THREAD_COUNT);
		gpu->identity.vertex_cache_size = etnaviv_field(specs[0],
					VIVS_HI_CHIP_SPECS_VERTEX_CACHE_SIZE);
		gpu->identity.shader_core_count = etnaviv_field(specs[0],
					VIVS_HI_CHIP_SPECS_SHADER_CORE_COUNT);
		gpu->identity.pixel_pipes = etnaviv_field(specs[0],
					VIVS_HI_CHIP_SPECS_PIXEL_PIPES);
		gpu->identity.vertex_output_buffer_size =
			etnaviv_field(specs[0],
				VIVS_HI_CHIP_SPECS_VERTEX_OUTPUT_BUFFER_SIZE);

		gpu->identity.buffer_size = etnaviv_field(specs[1],
					VIVS_HI_CHIP_SPECS_2_BUFFER_SIZE);
		gpu->identity.instruction_count = etnaviv_field(specs[1],
					VIVS_HI_CHIP_SPECS_2_INSTRUCTION_COUNT);
		gpu->identity.num_constants = etnaviv_field(specs[1],
					VIVS_HI_CHIP_SPECS_2_NUM_CONSTANTS);

		gpu->identity.varyings_count = etnaviv_field(specs[2],
					VIVS_HI_CHIP_SPECS_3_VARYINGS_COUNT);

		/* This overrides the value from older register if non-zero */
		streams = etnaviv_field(specs[3],
					VIVS_HI_CHIP_SPECS_4_STREAM_COUNT);
		if (streams)
			gpu->identity.stream_count = streams;
	}

	/* Fill in the stream count if not specified */
	if (gpu->identity.stream_count == 0) {
		if (gpu->identity.model >= 0x1000)
			gpu->identity.stream_count = 4;
		else
			gpu->identity.stream_count = 1;
	}

	/* Convert the register max value */
	if (gpu->identity.register_max)
		gpu->identity.register_max = 1 << gpu->identity.register_max;
	else if (gpu->identity.model == chipModel_GC400)
		gpu->identity.register_max = 32;
	else
		gpu->identity.register_max = 64;

	/* Convert thread count */
	if (gpu->identity.thread_count)
		gpu->identity.thread_count = 1 << gpu->identity.thread_count;
	else if (gpu->identity.model == chipModel_GC400)
		gpu->identity.thread_count = 64;
	else if (gpu->identity.model == chipModel_GC500 ||
		 gpu->identity.model == chipModel_GC530)
		gpu->identity.thread_count = 128;
	else
		gpu->identity.thread_count = 256;

	if (gpu->identity.vertex_cache_size == 0)
		gpu->identity.vertex_cache_size = 8;

	if (gpu->identity.shader_core_count == 0) {
		if (gpu->identity.model >= 0x1000)
			gpu->identity.shader_core_count = 2;
		else
			gpu->identity.shader_core_count = 1;
	}

	if (gpu->identity.pixel_pipes == 0)
		gpu->identity.pixel_pipes = 1;

	/* Convert virtex buffer size */
	if (gpu->identity.vertex_output_buffer_size) {
		gpu->identity.vertex_output_buffer_size =
			1 << gpu->identity.vertex_output_buffer_size;
	} else if (gpu->identity.model == chipModel_GC400) {
		if (gpu->identity.revision < 0x4000)
			gpu->identity.vertex_output_buffer_size = 512;
		else if (gpu->identity.revision < 0x4200)
			gpu->identity.vertex_output_buffer_size = 256;
		else
			gpu->identity.vertex_output_buffer_size = 128;
	} else {
		gpu->identity.vertex_output_buffer_size = 512;
	}

	switch (gpu->identity.instruction_count) {
	case 0:
		if (etnaviv_is_model_rev(gpu, GC2000, 0x5108) ||
		    gpu->identity.model == chipModel_GC880)
			gpu->identity.instruction_count = 512;
		else
			gpu->identity.instruction_count = 256;
		break;

	case 1:
		gpu->identity.instruction_count = 1024;
		break;

	case 2:
		gpu->identity.instruction_count = 2048;
		break;

	default:
		gpu->identity.instruction_count = 256;
		break;
	}

	if (gpu->identity.num_constants == 0)
		gpu->identity.num_constants = 168;

	if (gpu->identity.varyings_count == 0) {
		if (gpu->identity.minor_features1 & chipMinorFeatures1_HALTI0)
			gpu->identity.varyings_count = 12;
		else
			gpu->identity.varyings_count = 8;
	}

	/*
	 * For some cores, two varyings are consumed for position, so the
	 * maximum varying count needs to be reduced by one.
	 */
	if (etnaviv_is_model_rev(gpu, GC5000, 0x5434) ||
	    etnaviv_is_model_rev(gpu, GC4000, 0x5222) ||
	    etnaviv_is_model_rev(gpu, GC4000, 0x5245) ||
	    etnaviv_is_model_rev(gpu, GC4000, 0x5208) ||
	    etnaviv_is_model_rev(gpu, GC3000, 0x5435) ||
	    etnaviv_is_model_rev(gpu, GC2200, 0x5244) ||
	    etnaviv_is_model_rev(gpu, GC2100, 0x5108) ||
	    etnaviv_is_model_rev(gpu, GC2000, 0x5108) ||
	    etnaviv_is_model_rev(gpu, GC1500, 0x5246) ||
	    etnaviv_is_model_rev(gpu, GC880, 0x5107) ||
	    etnaviv_is_model_rev(gpu, GC880, 0x5106))
		gpu->identity.varyings_count -= 1;
}

static void etnaviv_hw_identify(struct etnaviv_gpu *gpu)
{
	u32 chipIdentity;

	chipIdentity = gpu_read(gpu, VIVS_HI_CHIP_IDENTITY);

	/* Special case for older graphic cores. */
	if (etnaviv_field(chipIdentity, VIVS_HI_CHIP_IDENTITY_FAMILY) == 0x01) {
		gpu->identity.model    = chipModel_GC500;
		gpu->identity.revision = etnaviv_field(chipIdentity,
					 VIVS_HI_CHIP_IDENTITY_REVISION);
	} else {

		gpu->identity.model = gpu_read(gpu, VIVS_HI_CHIP_MODEL);
		gpu->identity.revision = gpu_read(gpu, VIVS_HI_CHIP_REV);

		/*
		 * !!!! HACK ALERT !!!!
		 * Because people change device IDs without letting software
		 * know about it - here is the hack to make it all look the
		 * same.  Only for GC400 family.
		 */
		if ((gpu->identity.model & 0xff00) == 0x0400 &&
		    gpu->identity.model != chipModel_GC420) {
			gpu->identity.model = gpu->identity.model & 0x0400;
		}

		/* Another special case */
		if (etnaviv_is_model_rev(gpu, GC300, 0x2201)) {
			u32 chipDate = gpu_read(gpu, VIVS_HI_CHIP_DATE);
			u32 chipTime = gpu_read(gpu, VIVS_HI_CHIP_TIME);

			if (chipDate == 0x20080814 && chipTime == 0x12051100) {
				/*
				 * This IP has an ECO; put the correct
				 * revision in it.
				 */
				gpu->identity.revision = 0x1051;
			}
		}

		/*
		 * NXP likes to call the GPU on the i.MX6QP GC2000+, but in
		 * reality it's just a re-branded GC3000. We can identify this
		 * core by the upper half of the revision register being all 1.
		 * Fix model/rev here, so all other places can refer to this
		 * core by its real identity.
		 */
		if (etnaviv_is_model_rev(gpu, GC2000, 0xffff5450)) {
			gpu->identity.model = chipModel_GC3000;
			gpu->identity.revision &= 0xffff;
		}
	}

	dev_info(gpu->dev, "model: GC%x, revision: %x\n",
		 gpu->identity.model, gpu->identity.revision);

	gpu->identity.features = gpu_read(gpu, VIVS_HI_CHIP_FEATURE);

	/* Disable fast clear on GC700. */
	if (gpu->identity.model == chipModel_GC700)
		gpu->identity.features &= ~chipFeatures_FAST_CLEAR;

	if ((gpu->identity.model == chipModel_GC500 &&
	     gpu->identity.revision < 2) ||
	    (gpu->identity.model == chipModel_GC300 &&
	     gpu->identity.revision < 0x2000)) {

		/*
		 * GC500 rev 1.x and GC300 rev < 2.0 doesn't have these
		 * registers.
		 */
		gpu->identity.minor_features0 = 0;
		gpu->identity.minor_features1 = 0;
		gpu->identity.minor_features2 = 0;
		gpu->identity.minor_features3 = 0;
		gpu->identity.minor_features4 = 0;
		gpu->identity.minor_features5 = 0;
	} else
		gpu->identity.minor_features0 =
				gpu_read(gpu, VIVS_HI_CHIP_MINOR_FEATURE_0);

	if (gpu->identity.minor_features0 &
	    chipMinorFeatures0_MORE_MINOR_FEATURES) {
		gpu->identity.minor_features1 =
				gpu_read(gpu, VIVS_HI_CHIP_MINOR_FEATURE_1);
		gpu->identity.minor_features2 =
				gpu_read(gpu, VIVS_HI_CHIP_MINOR_FEATURE_2);
		gpu->identity.minor_features3 =
				gpu_read(gpu, VIVS_HI_CHIP_MINOR_FEATURE_3);
		gpu->identity.minor_features4 =
				gpu_read(gpu, VIVS_HI_CHIP_MINOR_FEATURE_4);
		gpu->identity.minor_features5 =
				gpu_read(gpu, VIVS_HI_CHIP_MINOR_FEATURE_5);
	}

	/* GC600 idle register reports zero bits where modules aren't present */
	if (gpu->identity.model == chipModel_GC600) {
		gpu->idle_mask = VIVS_HI_IDLE_STATE_TX |
				 VIVS_HI_IDLE_STATE_RA |
				 VIVS_HI_IDLE_STATE_SE |
				 VIVS_HI_IDLE_STATE_PA |
				 VIVS_HI_IDLE_STATE_SH |
				 VIVS_HI_IDLE_STATE_PE |
				 VIVS_HI_IDLE_STATE_DE |
				 VIVS_HI_IDLE_STATE_FE;
	} else {
		gpu->idle_mask = ~VIVS_HI_IDLE_STATE_AXI_LP;
	}

	etnaviv_hw_specs(gpu);
}

static void etnaviv_gpu_load_clock(struct etnaviv_gpu *gpu, u32 clock)
{
	gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, clock |
		  VIVS_HI_CLOCK_CONTROL_FSCALE_CMD_LOAD);
	gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, clock);
}

static int etnaviv_hw_reset(struct etnaviv_gpu *gpu)
{
	u32 control, idle;
	unsigned long timeout;
	bool failed = true;

	/* TODO
	 *
	 * - clock gating
	 * - puls eater
	 * - what about VG?
	 */

	/* We hope that the GPU resets in under one second */
	timeout = jiffies + msecs_to_jiffies(1000);

	while (time_is_after_jiffies(timeout)) {
		control = VIVS_HI_CLOCK_CONTROL_DISABLE_DEBUG_REGISTERS |
			  VIVS_HI_CLOCK_CONTROL_FSCALE_VAL(0x40);

		/* enable clock */
		etnaviv_gpu_load_clock(gpu, control);

		/* Wait for stable clock.  Vivante's code waited for 1ms */
		usleep_range(1000, 10000);

		/* isolate the GPU. */
		control |= VIVS_HI_CLOCK_CONTROL_ISOLATE_GPU;
		gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, control);

		/* set soft reset. */
		control |= VIVS_HI_CLOCK_CONTROL_SOFT_RESET;
		gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, control);

		/* wait for reset. */
		msleep(1);

		/* reset soft reset bit. */
		control &= ~VIVS_HI_CLOCK_CONTROL_SOFT_RESET;
		gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, control);

		/* reset GPU isolation. */
		control &= ~VIVS_HI_CLOCK_CONTROL_ISOLATE_GPU;
		gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, control);

		/* read idle register. */
		idle = gpu_read(gpu, VIVS_HI_IDLE_STATE);

		/* try reseting again if FE it not idle */
		if ((idle & VIVS_HI_IDLE_STATE_FE) == 0) {
			dev_dbg(gpu->dev, "FE is not idle\n");
			continue;
		}

		/* read reset register. */
		control = gpu_read(gpu, VIVS_HI_CLOCK_CONTROL);

		/* is the GPU idle? */
		if (((control & VIVS_HI_CLOCK_CONTROL_IDLE_3D) == 0) ||
		    ((control & VIVS_HI_CLOCK_CONTROL_IDLE_2D) == 0)) {
			dev_dbg(gpu->dev, "GPU is not idle\n");
			continue;
		}

		failed = false;
		break;
	}

	if (failed) {
		idle = gpu_read(gpu, VIVS_HI_IDLE_STATE);
		control = gpu_read(gpu, VIVS_HI_CLOCK_CONTROL);

		dev_err(gpu->dev, "GPU failed to reset: FE %sidle, 3D %sidle, 2D %sidle\n",
			idle & VIVS_HI_IDLE_STATE_FE ? "" : "not ",
			control & VIVS_HI_CLOCK_CONTROL_IDLE_3D ? "" : "not ",
			control & VIVS_HI_CLOCK_CONTROL_IDLE_2D ? "" : "not ");

		return -EBUSY;
	}

	/* We rely on the GPU running, so program the clock */
	control = VIVS_HI_CLOCK_CONTROL_DISABLE_DEBUG_REGISTERS |
		  VIVS_HI_CLOCK_CONTROL_FSCALE_VAL(0x40);

	/* enable clock */
	etnaviv_gpu_load_clock(gpu, control);

	return 0;
}

static void etnaviv_gpu_enable_mlcg(struct etnaviv_gpu *gpu)
{
	u32 pmc, ppc;

	/* enable clock gating */
	ppc = gpu_read(gpu, VIVS_PM_POWER_CONTROLS);
	ppc |= VIVS_PM_POWER_CONTROLS_ENABLE_MODULE_CLOCK_GATING;

	/* Disable stall module clock gating for 4.3.0.1 and 4.3.0.2 revs */
	if (gpu->identity.revision == 0x4301 ||
	    gpu->identity.revision == 0x4302)
		ppc |= VIVS_PM_POWER_CONTROLS_DISABLE_STALL_MODULE_CLOCK_GATING;

	gpu_write(gpu, VIVS_PM_POWER_CONTROLS, ppc);

	pmc = gpu_read(gpu, VIVS_PM_MODULE_CONTROLS);

	/* Disable PA clock gating for GC400+ except for GC420 */
	if (gpu->identity.model >= chipModel_GC400 &&
	    gpu->identity.model != chipModel_GC420)
		pmc |= VIVS_PM_MODULE_CONTROLS_DISABLE_MODULE_CLOCK_GATING_PA;

	/*
	 * Disable PE clock gating on revs < 5.0.0.0 when HZ is
	 * present without a bug fix.
	 */
	if (gpu->identity.revision < 0x5000 &&
	    gpu->identity.minor_features0 & chipMinorFeatures0_HZ &&
	    !(gpu->identity.minor_features1 &
	      chipMinorFeatures1_DISABLE_PE_GATING))
		pmc |= VIVS_PM_MODULE_CONTROLS_DISABLE_MODULE_CLOCK_GATING_PE;

	if (gpu->identity.revision < 0x5422)
		pmc |= BIT(15); /* Unknown bit */

	pmc |= VIVS_PM_MODULE_CONTROLS_DISABLE_MODULE_CLOCK_GATING_RA_HZ;
	pmc |= VIVS_PM_MODULE_CONTROLS_DISABLE_MODULE_CLOCK_GATING_RA_EZ;

	gpu_write(gpu, VIVS_PM_MODULE_CONTROLS, pmc);
}

void etnaviv_gpu_start_fe(struct etnaviv_gpu *gpu, u32 address, u16 prefetch)
{
	gpu_write(gpu, VIVS_FE_COMMAND_ADDRESS, address);
	gpu_write(gpu, VIVS_FE_COMMAND_CONTROL,
		  VIVS_FE_COMMAND_CONTROL_ENABLE |
		  VIVS_FE_COMMAND_CONTROL_PREFETCH(prefetch));
}

static void etnaviv_gpu_setup_pulse_eater(struct etnaviv_gpu *gpu)
{
	/*
	 * Base value for VIVS_PM_PULSE_EATER register on models where it
	 * cannot be read, extracted from vivante kernel driver.
	 */
	u32 pulse_eater = 0x01590880;

	if (etnaviv_is_model_rev(gpu, GC4000, 0x5208) ||
	    etnaviv_is_model_rev(gpu, GC4000, 0x5222)) {
		pulse_eater |= BIT(23);

	}

	if (etnaviv_is_model_rev(gpu, GC1000, 0x5039) ||
	    etnaviv_is_model_rev(gpu, GC1000, 0x5040)) {
		pulse_eater &= ~BIT(16);
		pulse_eater |= BIT(17);
	}

	if ((gpu->identity.revision > 0x5420) &&
	    (gpu->identity.features & chipFeatures_PIPE_3D))
	{
		/* Performance fix: disable internal DFS */
		pulse_eater = gpu_read(gpu, VIVS_PM_PULSE_EATER);
		pulse_eater |= BIT(18);
	}

	gpu_write(gpu, VIVS_PM_PULSE_EATER, pulse_eater);
}

static void etnaviv_gpu_hw_init(struct etnaviv_gpu *gpu)
{
	u16 prefetch;

	if ((etnaviv_is_model_rev(gpu, GC320, 0x5007) ||
	     etnaviv_is_model_rev(gpu, GC320, 0x5220)) &&
	    gpu_read(gpu, VIVS_HI_CHIP_TIME) != 0x2062400) {
		u32 mc_memory_debug;

		mc_memory_debug = gpu_read(gpu, VIVS_MC_DEBUG_MEMORY) & ~0xff;

		if (gpu->identity.revision == 0x5007)
			mc_memory_debug |= 0x0c;
		else
			mc_memory_debug |= 0x08;

		gpu_write(gpu, VIVS_MC_DEBUG_MEMORY, mc_memory_debug);
	}

	/* enable module-level clock gating */
	etnaviv_gpu_enable_mlcg(gpu);

	/*
	 * Update GPU AXI cache atttribute to "cacheable, no allocate".
	 * This is necessary to prevent the iMX6 SoC locking up.
	 */
	gpu_write(gpu, VIVS_HI_AXI_CONFIG,
		  VIVS_HI_AXI_CONFIG_AWCACHE(2) |
		  VIVS_HI_AXI_CONFIG_ARCACHE(2));

	/* GC2000 rev 5108 needs a special bus config */
	if (etnaviv_is_model_rev(gpu, GC2000, 0x5108)) {
		u32 bus_config = gpu_read(gpu, VIVS_MC_BUS_CONFIG);
		bus_config &= ~(VIVS_MC_BUS_CONFIG_FE_BUS_CONFIG__MASK |
				VIVS_MC_BUS_CONFIG_TX_BUS_CONFIG__MASK);
		bus_config |= VIVS_MC_BUS_CONFIG_FE_BUS_CONFIG(1) |
			      VIVS_MC_BUS_CONFIG_TX_BUS_CONFIG(0);
		gpu_write(gpu, VIVS_MC_BUS_CONFIG, bus_config);
	}

	/* setup the pulse eater */
	etnaviv_gpu_setup_pulse_eater(gpu);

	/* setup the MMU */
	etnaviv_iommu_restore(gpu);

	/* Start command processor */
	prefetch = etnaviv_buffer_init(gpu);

	gpu_write(gpu, VIVS_HI_INTR_ENBL, ~0U);
	etnaviv_gpu_start_fe(gpu, etnaviv_cmdbuf_get_va(gpu->buffer),
			     prefetch);
}

int etnaviv_gpu_init(struct etnaviv_gpu *gpu)
{
	int ret, i;

	ret = pm_runtime_get_sync(gpu->dev);
	if (ret < 0) {
		dev_err(gpu->dev, "Failed to enable GPU power domain\n");
		return ret;
	}

	etnaviv_hw_identify(gpu);

	if (gpu->identity.model == 0) {
		dev_err(gpu->dev, "Unknown GPU model\n");
		ret = -ENXIO;
		goto fail;
	}

	/* Exclude VG cores with FE2.0 */
	if (gpu->identity.features & chipFeatures_PIPE_VG &&
	    gpu->identity.features & chipFeatures_FE20) {
		dev_info(gpu->dev, "Ignoring GPU with VG and FE2.0\n");
		ret = -ENXIO;
		goto fail;
	}

	/*
	 * Set the GPU linear window to be at the end of the DMA window, where
	 * the CMA area is likely to reside. This ensures that we are able to
	 * map the command buffers while having the linear window overlap as
	 * much RAM as possible, so we can optimize mappings for other buffers.
	 *
	 * For 3D cores only do this if MC2.0 is present, as with MC1.0 it leads
	 * to different views of the memory on the individual engines.
	 */
	if (!(gpu->identity.features & chipFeatures_PIPE_3D) ||
	    (gpu->identity.minor_features0 & chipMinorFeatures0_MC20)) {
		u32 dma_mask = (u32)dma_get_required_mask(gpu->dev);
		if (dma_mask < PHYS_OFFSET + SZ_2G)
			gpu->memory_base = PHYS_OFFSET;
		else
			gpu->memory_base = dma_mask - SZ_2G + 1;
	} else if (PHYS_OFFSET >= SZ_2G) {
		dev_info(gpu->dev, "Need to move linear window on MC1.0, disabling TS\n");
		gpu->memory_base = PHYS_OFFSET;
		gpu->identity.features &= ~chipFeatures_FAST_CLEAR;
	}

	ret = etnaviv_hw_reset(gpu);
	if (ret) {
		dev_err(gpu->dev, "GPU reset failed\n");
		goto fail;
	}

	gpu->mmu = etnaviv_iommu_new(gpu);
	if (IS_ERR(gpu->mmu)) {
		dev_err(gpu->dev, "Failed to instantiate GPU IOMMU\n");
		ret = PTR_ERR(gpu->mmu);
		goto fail;
	}

	gpu->cmdbuf_suballoc = etnaviv_cmdbuf_suballoc_new(gpu);
	if (IS_ERR(gpu->cmdbuf_suballoc)) {
		dev_err(gpu->dev, "Failed to create cmdbuf suballocator\n");
		ret = PTR_ERR(gpu->cmdbuf_suballoc);
		goto fail;
	}

	/* Create buffer: */
	gpu->buffer = etnaviv_cmdbuf_new(gpu->cmdbuf_suballoc, PAGE_SIZE, 0);
	if (!gpu->buffer) {
		ret = -ENOMEM;
		dev_err(gpu->dev, "could not create command buffer\n");
		goto destroy_iommu;
	}

	if (gpu->mmu->version == ETNAVIV_IOMMU_V1 &&
	    etnaviv_cmdbuf_get_va(gpu->buffer) > 0x80000000) {
		ret = -EINVAL;
		dev_err(gpu->dev,
			"command buffer outside valid memory window\n");
		goto free_buffer;
	}

	/* Setup event management */
	spin_lock_init(&gpu->event_spinlock);
	init_completion(&gpu->event_free);
	for (i = 0; i < ARRAY_SIZE(gpu->event); i++) {
		gpu->event[i].used = false;
		complete(&gpu->event_free);
	}

	/* Now program the hardware */
	mutex_lock(&gpu->lock);
	etnaviv_gpu_hw_init(gpu);
	gpu->exec_state = -1;
	mutex_unlock(&gpu->lock);

	pm_runtime_mark_last_busy(gpu->dev);
	pm_runtime_put_autosuspend(gpu->dev);

	return 0;

free_buffer:
	etnaviv_cmdbuf_free(gpu->buffer);
	gpu->buffer = NULL;
destroy_iommu:
	etnaviv_iommu_destroy(gpu->mmu);
	gpu->mmu = NULL;
fail:
	pm_runtime_mark_last_busy(gpu->dev);
	pm_runtime_put_autosuspend(gpu->dev);

	return ret;
}

#ifdef CONFIG_DEBUG_FS
struct dma_debug {
	u32 address[2];
	u32 state[2];
};

static void verify_dma(struct etnaviv_gpu *gpu, struct dma_debug *debug)
{
	u32 i;

	debug->address[0] = gpu_read(gpu, VIVS_FE_DMA_ADDRESS);
	debug->state[0]   = gpu_read(gpu, VIVS_FE_DMA_DEBUG_STATE);

	for (i = 0; i < 500; i++) {
		debug->address[1] = gpu_read(gpu, VIVS_FE_DMA_ADDRESS);
		debug->state[1]   = gpu_read(gpu, VIVS_FE_DMA_DEBUG_STATE);

		if (debug->address[0] != debug->address[1])
			break;

		if (debug->state[0] != debug->state[1])
			break;
	}
}

int etnaviv_gpu_debugfs(struct etnaviv_gpu *gpu, struct seq_file *m)
{
	struct dma_debug debug;
	u32 dma_lo, dma_hi, axi, idle;
	int ret;

	seq_printf(m, "%s Status:\n", dev_name(gpu->dev));

	ret = pm_runtime_get_sync(gpu->dev);
	if (ret < 0)
		return ret;

	dma_lo = gpu_read(gpu, VIVS_FE_DMA_LOW);
	dma_hi = gpu_read(gpu, VIVS_FE_DMA_HIGH);
	axi = gpu_read(gpu, VIVS_HI_AXI_STATUS);
	idle = gpu_read(gpu, VIVS_HI_IDLE_STATE);

	verify_dma(gpu, &debug);

	seq_puts(m, "\tfeatures\n");
	seq_printf(m, "\t minor_features0: 0x%08x\n",
		   gpu->identity.minor_features0);
	seq_printf(m, "\t minor_features1: 0x%08x\n",
		   gpu->identity.minor_features1);
	seq_printf(m, "\t minor_features2: 0x%08x\n",
		   gpu->identity.minor_features2);
	seq_printf(m, "\t minor_features3: 0x%08x\n",
		   gpu->identity.minor_features3);
	seq_printf(m, "\t minor_features4: 0x%08x\n",
		   gpu->identity.minor_features4);
	seq_printf(m, "\t minor_features5: 0x%08x\n",
		   gpu->identity.minor_features5);

	seq_puts(m, "\tspecs\n");
	seq_printf(m, "\t stream_count:  %d\n",
			gpu->identity.stream_count);
	seq_printf(m, "\t register_max: %d\n",
			gpu->identity.register_max);
	seq_printf(m, "\t thread_count: %d\n",
			gpu->identity.thread_count);
	seq_printf(m, "\t vertex_cache_size: %d\n",
			gpu->identity.vertex_cache_size);
	seq_printf(m, "\t shader_core_count: %d\n",
			gpu->identity.shader_core_count);
	seq_printf(m, "\t pixel_pipes: %d\n",
			gpu->identity.pixel_pipes);
	seq_printf(m, "\t vertex_output_buffer_size: %d\n",
			gpu->identity.vertex_output_buffer_size);
	seq_printf(m, "\t buffer_size: %d\n",
			gpu->identity.buffer_size);
	seq_printf(m, "\t instruction_count: %d\n",
			gpu->identity.instruction_count);
	seq_printf(m, "\t num_constants: %d\n",
			gpu->identity.num_constants);
	seq_printf(m, "\t varyings_count: %d\n",
			gpu->identity.varyings_count);

	seq_printf(m, "\taxi: 0x%08x\n", axi);
	seq_printf(m, "\tidle: 0x%08x\n", idle);
	idle |= ~gpu->idle_mask & ~VIVS_HI_IDLE_STATE_AXI_LP;
	if ((idle & VIVS_HI_IDLE_STATE_FE) == 0)
		seq_puts(m, "\t FE is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_DE) == 0)
		seq_puts(m, "\t DE is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_PE) == 0)
		seq_puts(m, "\t PE is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_SH) == 0)
		seq_puts(m, "\t SH is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_PA) == 0)
		seq_puts(m, "\t PA is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_SE) == 0)
		seq_puts(m, "\t SE is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_RA) == 0)
		seq_puts(m, "\t RA is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_TX) == 0)
		seq_puts(m, "\t TX is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_VG) == 0)
		seq_puts(m, "\t VG is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_IM) == 0)
		seq_puts(m, "\t IM is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_FP) == 0)
		seq_puts(m, "\t FP is not idle\n");
	if ((idle & VIVS_HI_IDLE_STATE_TS) == 0)
		seq_puts(m, "\t TS is not idle\n");
	if (idle & VIVS_HI_IDLE_STATE_AXI_LP)
		seq_puts(m, "\t AXI low power mode\n");

	if (gpu->identity.features & chipFeatures_DEBUG_MODE) {
		u32 read0 = gpu_read(gpu, VIVS_MC_DEBUG_READ0);
		u32 read1 = gpu_read(gpu, VIVS_MC_DEBUG_READ1);
		u32 write = gpu_read(gpu, VIVS_MC_DEBUG_WRITE);

		seq_puts(m, "\tMC\n");
		seq_printf(m, "\t read0: 0x%08x\n", read0);
		seq_printf(m, "\t read1: 0x%08x\n", read1);
		seq_printf(m, "\t write: 0x%08x\n", write);
	}

	seq_puts(m, "\tDMA ");

	if (debug.address[0] == debug.address[1] &&
	    debug.state[0] == debug.state[1]) {
		seq_puts(m, "seems to be stuck\n");
	} else if (debug.address[0] == debug.address[1]) {
		seq_puts(m, "address is constant\n");
	} else {
		seq_puts(m, "is running\n");
	}

	seq_printf(m, "\t address 0: 0x%08x\n", debug.address[0]);
	seq_printf(m, "\t address 1: 0x%08x\n", debug.address[1]);
	seq_printf(m, "\t state 0: 0x%08x\n", debug.state[0]);
	seq_printf(m, "\t state 1: 0x%08x\n", debug.state[1]);
	seq_printf(m, "\t last fetch 64 bit word: 0x%08x 0x%08x\n",
		   dma_lo, dma_hi);

	ret = 0;

	pm_runtime_mark_last_busy(gpu->dev);
	pm_runtime_put_autosuspend(gpu->dev);

	return ret;
}
#endif

/*
 * Hangcheck detection for locked gpu:
 */
static void recover_worker(struct work_struct *work)
{
	struct etnaviv_gpu *gpu = container_of(work, struct etnaviv_gpu,
					       recover_work);
	unsigned long flags;
	unsigned int i;

	dev_err(gpu->dev, "hangcheck recover!\n");

	if (pm_runtime_get_sync(gpu->dev) < 0)
		return;

	mutex_lock(&gpu->lock);

	/* Only catch the first event, or when manually re-armed */
	if (etnaviv_dump_core) {
		etnaviv_core_dump(gpu);
		etnaviv_dump_core = false;
	}

	etnaviv_hw_reset(gpu);

	/* complete all events, the GPU won't do it after the reset */
	spin_lock_irqsave(&gpu->event_spinlock, flags);
	for (i = 0; i < ARRAY_SIZE(gpu->event); i++) {
		if (!gpu->event[i].used)
			continue;
		dma_fence_signal(gpu->event[i].fence);
		gpu->event[i].fence = NULL;
		gpu->event[i].used = false;
		complete(&gpu->event_free);
	}
	spin_unlock_irqrestore(&gpu->event_spinlock, flags);
	gpu->completed_fence = gpu->active_fence;

	etnaviv_gpu_hw_init(gpu);
	gpu->lastctx = NULL;
	gpu->exec_state = -1;

	mutex_unlock(&gpu->lock);
	pm_runtime_mark_last_busy(gpu->dev);
	pm_runtime_put_autosuspend(gpu->dev);

	/* Retire the buffer objects in a work */
	etnaviv_queue_work(gpu->drm, &gpu->retire_work);
}

static void hangcheck_timer_reset(struct etnaviv_gpu *gpu)
{
	DBG("%s", dev_name(gpu->dev));
	mod_timer(&gpu->hangcheck_timer,
		  round_jiffies_up(jiffies + DRM_ETNAVIV_HANGCHECK_JIFFIES));
}

static void hangcheck_handler(unsigned long data)
{
	struct etnaviv_gpu *gpu = (struct etnaviv_gpu *)data;
	u32 fence = gpu->completed_fence;
	bool progress = false;

	if (fence != gpu->hangcheck_fence) {
		gpu->hangcheck_fence = fence;
		progress = true;
	}

	if (!progress) {
		u32 dma_addr = gpu_read(gpu, VIVS_FE_DMA_ADDRESS);
		int change = dma_addr - gpu->hangcheck_dma_addr;

		if (change < 0 || change > 16) {
			gpu->hangcheck_dma_addr = dma_addr;
			progress = true;
		}
	}

	if (!progress && fence_after(gpu->active_fence, fence)) {
		dev_err(gpu->dev, "hangcheck detected gpu lockup!\n");
		dev_err(gpu->dev, "     completed fence: %u\n", fence);
		dev_err(gpu->dev, "     active fence: %u\n",
			gpu->active_fence);
		etnaviv_queue_work(gpu->drm, &gpu->recover_work);
	}

	/* if still more pending work, reset the hangcheck timer: */
	if (fence_after(gpu->active_fence, gpu->hangcheck_fence))
		hangcheck_timer_reset(gpu);
}

static void hangcheck_disable(struct etnaviv_gpu *gpu)
{
	del_timer_sync(&gpu->hangcheck_timer);
	cancel_work_sync(&gpu->recover_work);
}

/* fence object management */
struct etnaviv_fence {
	struct etnaviv_gpu *gpu;
	struct dma_fence base;
};

static inline struct etnaviv_fence *to_etnaviv_fence(struct dma_fence *fence)
{
	return container_of(fence, struct etnaviv_fence, base);
}

static const char *etnaviv_fence_get_driver_name(struct dma_fence *fence)
{
	return "etnaviv";
}

static const char *etnaviv_fence_get_timeline_name(struct dma_fence *fence)
{
	struct etnaviv_fence *f = to_etnaviv_fence(fence);

	return dev_name(f->gpu->dev);
}

static bool etnaviv_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static bool etnaviv_fence_signaled(struct dma_fence *fence)
{
	struct etnaviv_fence *f = to_etnaviv_fence(fence);

	return fence_completed(f->gpu, f->base.seqno);
}

static void etnaviv_fence_release(struct dma_fence *fence)
{
	struct etnaviv_fence *f = to_etnaviv_fence(fence);

	kfree_rcu(f, base.rcu);
}

static const struct dma_fence_ops etnaviv_fence_ops = {
	.get_driver_name = etnaviv_fence_get_driver_name,
	.get_timeline_name = etnaviv_fence_get_timeline_name,
	.enable_signaling = etnaviv_fence_enable_signaling,
	.signaled = etnaviv_fence_signaled,
	.wait = dma_fence_default_wait,
	.release = etnaviv_fence_release,
};

static struct dma_fence *etnaviv_gpu_fence_alloc(struct etnaviv_gpu *gpu)
{
	struct etnaviv_fence *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return NULL;

	f->gpu = gpu;

	dma_fence_init(&f->base, &etnaviv_fence_ops, &gpu->fence_spinlock,
		       gpu->fence_context, ++gpu->next_fence);

	return &f->base;
}

int etnaviv_gpu_fence_sync_obj(struct etnaviv_gem_object *etnaviv_obj,
	unsigned int context, bool exclusive)
{
	struct reservation_object *robj = etnaviv_obj->resv;
	struct reservation_object_list *fobj;
	struct dma_fence *fence;
	int i, ret;

	if (!exclusive) {
		ret = reservation_object_reserve_shared(robj);
		if (ret)
			return ret;
	}

	/*
	 * If we have any shared fences, then the exclusive fence
	 * should be ignored as it will already have been signalled.
	 */
	fobj = reservation_object_get_list(robj);
	if (!fobj || fobj->shared_count == 0) {
		/* Wait on any existing exclusive fence which isn't our own */
		fence = reservation_object_get_excl(robj);
		if (fence && fence->context != context) {
			ret = dma_fence_wait(fence, true);
			if (ret)
				return ret;
		}
	}

	if (!exclusive || !fobj)
		return 0;

	for (i = 0; i < fobj->shared_count; i++) {
		fence = rcu_dereference_protected(fobj->shared[i],
						reservation_object_held(robj));
		if (fence->context != context) {
			ret = dma_fence_wait(fence, true);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/*
 * event management:
 */

static unsigned int event_alloc(struct etnaviv_gpu *gpu)
{
	unsigned long ret, flags;
	unsigned int i, event = ~0U;

	ret = wait_for_completion_timeout(&gpu->event_free,
					  msecs_to_jiffies(10 * 10000));
	if (!ret)
		dev_err(gpu->dev, "wait_for_completion_timeout failed");

	spin_lock_irqsave(&gpu->event_spinlock, flags);

	/* find first free event */
	for (i = 0; i < ARRAY_SIZE(gpu->event); i++) {
		if (gpu->event[i].used == false) {
			gpu->event[i].used = true;
			event = i;
			break;
		}
	}

	spin_unlock_irqrestore(&gpu->event_spinlock, flags);

	return event;
}

static void event_free(struct etnaviv_gpu *gpu, unsigned int event)
{
	unsigned long flags;

	spin_lock_irqsave(&gpu->event_spinlock, flags);

	if (gpu->event[event].used == false) {
		dev_warn(gpu->dev, "event %u is already marked as free",
			 event);
		spin_unlock_irqrestore(&gpu->event_spinlock, flags);
	} else {
		gpu->event[event].used = false;
		spin_unlock_irqrestore(&gpu->event_spinlock, flags);

		complete(&gpu->event_free);
	}
}

/*
 * Cmdstream submission/retirement:
 */

static void retire_worker(struct work_struct *work)
{
	struct etnaviv_gpu *gpu = container_of(work, struct etnaviv_gpu,
					       retire_work);
	u32 fence = gpu->completed_fence;
	struct etnaviv_cmdbuf *cmdbuf, *tmp;
	unsigned int i;

	mutex_lock(&gpu->lock);
	list_for_each_entry_safe(cmdbuf, tmp, &gpu->active_cmd_list, node) {
		if (!dma_fence_is_signaled(cmdbuf->fence))
			break;

		list_del(&cmdbuf->node);
		dma_fence_put(cmdbuf->fence);

		for (i = 0; i < cmdbuf->nr_bos; i++) {
			struct etnaviv_vram_mapping *mapping = cmdbuf->bo_map[i];
			struct etnaviv_gem_object *etnaviv_obj = mapping->object;

			atomic_dec(&etnaviv_obj->gpu_active);
			/* drop the refcount taken in etnaviv_gpu_submit */
			etnaviv_gem_mapping_unreference(mapping);
		}

		etnaviv_cmdbuf_free(cmdbuf);
		/*
		 * We need to balance the runtime PM count caused by
		 * each submission.  Upon submission, we increment
		 * the runtime PM counter, and allocate one event.
		 * So here, we put the runtime PM count for each
		 * completed event.
		 */
		pm_runtime_put_autosuspend(gpu->dev);
	}

	gpu->retired_fence = fence;

	mutex_unlock(&gpu->lock);

	wake_up_all(&gpu->fence_event);
}

int etnaviv_gpu_wait_fence_interruptible(struct etnaviv_gpu *gpu,
	u32 fence, struct timespec *timeout)
{
	int ret;

	if (fence_after(fence, gpu->next_fence)) {
		DRM_ERROR("waiting on invalid fence: %u (of %u)\n",
				fence, gpu->next_fence);
		return -EINVAL;
	}

	if (!timeout) {
		/* No timeout was requested: just test for completion */
		ret = fence_completed(gpu, fence) ? 0 : -EBUSY;
	} else {
		unsigned long remaining = etnaviv_timeout_to_jiffies(timeout);

		ret = wait_event_interruptible_timeout(gpu->fence_event,
						fence_completed(gpu, fence),
						remaining);
		if (ret == 0) {
			DBG("timeout waiting for fence: %u (retired: %u completed: %u)",
				fence, gpu->retired_fence,
				gpu->completed_fence);
			ret = -ETIMEDOUT;
		} else if (ret != -ERESTARTSYS) {
			ret = 0;
		}
	}

	return ret;
}

/*
 * Wait for an object to become inactive.  This, on it's own, is not race
 * free: the object is moved by the retire worker off the active list, and
 * then the iova is put.  Moreover, the object could be re-submitted just
 * after we notice that it's become inactive.
 *
 * Although the retirement happens under the gpu lock, we don't want to hold
 * that lock in this function while waiting.
 */
int etnaviv_gpu_wait_obj_inactive(struct etnaviv_gpu *gpu,
	struct etnaviv_gem_object *etnaviv_obj, struct timespec *timeout)
{
	unsigned long remaining;
	long ret;

	if (!timeout)
		return !is_active(etnaviv_obj) ? 0 : -EBUSY;

	remaining = etnaviv_timeout_to_jiffies(timeout);

	ret = wait_event_interruptible_timeout(gpu->fence_event,
					       !is_active(etnaviv_obj),
					       remaining);
	if (ret > 0) {
		struct etnaviv_drm_private *priv = gpu->drm->dev_private;

		/* Synchronise with the retire worker */
		flush_workqueue(priv->wq);
		return 0;
	} else if (ret == -ERESTARTSYS) {
		return -ERESTARTSYS;
	} else {
		return -ETIMEDOUT;
	}
}

int etnaviv_gpu_pm_get_sync(struct etnaviv_gpu *gpu)
{
	return pm_runtime_get_sync(gpu->dev);
}

void etnaviv_gpu_pm_put(struct etnaviv_gpu *gpu)
{
	pm_runtime_mark_last_busy(gpu->dev);
	pm_runtime_put_autosuspend(gpu->dev);
}

/* add bo's to gpu's ring, and kick gpu: */
int etnaviv_gpu_submit(struct etnaviv_gpu *gpu,
	struct etnaviv_gem_submit *submit, struct etnaviv_cmdbuf *cmdbuf)
{
	struct dma_fence *fence;
	unsigned int event, i;
	int ret;

	ret = etnaviv_gpu_pm_get_sync(gpu);
	if (ret < 0)
		return ret;

	/*
	 * TODO
	 *
	 * - flush
	 * - data endian
	 * - prefetch
	 *
	 */

	event = event_alloc(gpu);
	if (unlikely(event == ~0U)) {
		DRM_ERROR("no free event\n");
		ret = -EBUSY;
		goto out_pm_put;
	}

	fence = etnaviv_gpu_fence_alloc(gpu);
	if (!fence) {
		event_free(gpu, event);
		ret = -ENOMEM;
		goto out_pm_put;
	}

	mutex_lock(&gpu->lock);

	gpu->event[event].fence = fence;
	submit->fence = fence->seqno;
	gpu->active_fence = submit->fence;

	if (gpu->lastctx != cmdbuf->ctx) {
		gpu->mmu->need_flush = true;
		gpu->switch_context = true;
		gpu->lastctx = cmdbuf->ctx;
	}

	etnaviv_buffer_queue(gpu, event, cmdbuf);

	cmdbuf->fence = fence;
	list_add_tail(&cmdbuf->node, &gpu->active_cmd_list);

	/* We're committed to adding this command buffer, hold a PM reference */
	pm_runtime_get_noresume(gpu->dev);

	for (i = 0; i < submit->nr_bos; i++) {
		struct etnaviv_gem_object *etnaviv_obj = submit->bos[i].obj;

		/* Each cmdbuf takes a refcount on the mapping */
		etnaviv_gem_mapping_reference(submit->bos[i].mapping);
		cmdbuf->bo_map[i] = submit->bos[i].mapping;
		atomic_inc(&etnaviv_obj->gpu_active);

		if (submit->bos[i].flags & ETNA_SUBMIT_BO_WRITE)
			reservation_object_add_excl_fence(etnaviv_obj->resv,
							  fence);
		else
			reservation_object_add_shared_fence(etnaviv_obj->resv,
							    fence);
	}
	cmdbuf->nr_bos = submit->nr_bos;
	hangcheck_timer_reset(gpu);
	ret = 0;

	mutex_unlock(&gpu->lock);

out_pm_put:
	etnaviv_gpu_pm_put(gpu);

	return ret;
}

/*
 * Init/Cleanup:
 */
static irqreturn_t irq_handler(int irq, void *data)
{
	struct etnaviv_gpu *gpu = data;
	irqreturn_t ret = IRQ_NONE;

	u32 intr = gpu_read(gpu, VIVS_HI_INTR_ACKNOWLEDGE);

	if (intr != 0) {
		int event;

		pm_runtime_mark_last_busy(gpu->dev);

		dev_dbg(gpu->dev, "intr 0x%08x\n", intr);

		if (intr & VIVS_HI_INTR_ACKNOWLEDGE_AXI_BUS_ERROR) {
			dev_err(gpu->dev, "AXI bus error\n");
			intr &= ~VIVS_HI_INTR_ACKNOWLEDGE_AXI_BUS_ERROR;
		}

		if (intr & VIVS_HI_INTR_ACKNOWLEDGE_MMU_EXCEPTION) {
			int i;

			dev_err_ratelimited(gpu->dev,
				"MMU fault status 0x%08x\n",
				gpu_read(gpu, VIVS_MMUv2_STATUS));
			for (i = 0; i < 4; i++) {
				dev_err_ratelimited(gpu->dev,
					"MMU %d fault addr 0x%08x\n",
					i, gpu_read(gpu,
					VIVS_MMUv2_EXCEPTION_ADDR(i)));
			}
			intr &= ~VIVS_HI_INTR_ACKNOWLEDGE_MMU_EXCEPTION;
		}

		while ((event = ffs(intr)) != 0) {
			struct dma_fence *fence;

			event -= 1;

			intr &= ~(1 << event);

			dev_dbg(gpu->dev, "event %u\n", event);

			fence = gpu->event[event].fence;
			gpu->event[event].fence = NULL;
			dma_fence_signal(fence);

			/*
			 * Events can be processed out of order.  Eg,
			 * - allocate and queue event 0
			 * - allocate event 1
			 * - event 0 completes, we process it
			 * - allocate and queue event 0
			 * - event 1 and event 0 complete
			 * we can end up processing event 0 first, then 1.
			 */
			if (fence_after(fence->seqno, gpu->completed_fence))
				gpu->completed_fence = fence->seqno;

			event_free(gpu, event);
		}

		/* Retire the buffer objects in a work */
		etnaviv_queue_work(gpu->drm, &gpu->retire_work);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static int etnaviv_gpu_clk_enable(struct etnaviv_gpu *gpu)
{
	int ret;

	if (gpu->clk_bus) {
		ret = clk_prepare_enable(gpu->clk_bus);
		if (ret)
			return ret;
	}

	if (gpu->clk_core) {
		ret = clk_prepare_enable(gpu->clk_core);
		if (ret)
			goto disable_clk_bus;
	}

	if (gpu->clk_shader) {
		ret = clk_prepare_enable(gpu->clk_shader);
		if (ret)
			goto disable_clk_core;
	}

	return 0;

disable_clk_core:
	if (gpu->clk_core)
		clk_disable_unprepare(gpu->clk_core);
disable_clk_bus:
	if (gpu->clk_bus)
		clk_disable_unprepare(gpu->clk_bus);

	return ret;
}

static int etnaviv_gpu_clk_disable(struct etnaviv_gpu *gpu)
{
	if (gpu->clk_shader)
		clk_disable_unprepare(gpu->clk_shader);
	if (gpu->clk_core)
		clk_disable_unprepare(gpu->clk_core);
	if (gpu->clk_bus)
		clk_disable_unprepare(gpu->clk_bus);

	return 0;
}

int etnaviv_gpu_wait_idle(struct etnaviv_gpu *gpu, unsigned int timeout_ms)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		u32 idle = gpu_read(gpu, VIVS_HI_IDLE_STATE);

		if ((idle & gpu->idle_mask) == gpu->idle_mask)
			return 0;

		if (time_is_before_jiffies(timeout)) {
			dev_warn(gpu->dev,
				 "timed out waiting for idle: idle=0x%x\n",
				 idle);
			return -ETIMEDOUT;
		}

		udelay(5);
	} while (1);
}

static int etnaviv_gpu_hw_suspend(struct etnaviv_gpu *gpu)
{
	if (gpu->buffer) {
		/* Replace the last WAIT with END */
		etnaviv_buffer_end(gpu);

		/*
		 * We know that only the FE is busy here, this should
		 * happen quickly (as the WAIT is only 200 cycles).  If
		 * we fail, just warn and continue.
		 */
		etnaviv_gpu_wait_idle(gpu, 100);
	}

	return etnaviv_gpu_clk_disable(gpu);
}

#ifdef CONFIG_PM
static int etnaviv_gpu_hw_resume(struct etnaviv_gpu *gpu)
{
	u32 clock;
	int ret;

	ret = mutex_lock_killable(&gpu->lock);
	if (ret)
		return ret;

	clock = VIVS_HI_CLOCK_CONTROL_DISABLE_DEBUG_REGISTERS |
		VIVS_HI_CLOCK_CONTROL_FSCALE_VAL(0x40);

	etnaviv_gpu_load_clock(gpu, clock);
	etnaviv_gpu_hw_init(gpu);

	gpu->switch_context = true;
	gpu->exec_state = -1;

	mutex_unlock(&gpu->lock);

	return 0;
}
#endif

static int etnaviv_gpu_bind(struct device *dev, struct device *master,
	void *data)
{
	struct drm_device *drm = data;
	struct etnaviv_drm_private *priv = drm->dev_private;
	struct etnaviv_gpu *gpu = dev_get_drvdata(dev);
	int ret;

#ifdef CONFIG_PM
	ret = pm_runtime_get_sync(gpu->dev);
#else
	ret = etnaviv_gpu_clk_enable(gpu);
#endif
	if (ret < 0)
		return ret;

	gpu->drm = drm;
	gpu->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&gpu->fence_spinlock);

	INIT_LIST_HEAD(&gpu->active_cmd_list);
	INIT_WORK(&gpu->retire_work, retire_worker);
	INIT_WORK(&gpu->recover_work, recover_worker);
	init_waitqueue_head(&gpu->fence_event);

	setup_deferrable_timer(&gpu->hangcheck_timer, hangcheck_handler,
			       (unsigned long)gpu);

	priv->gpu[priv->num_gpus++] = gpu;

	pm_runtime_mark_last_busy(gpu->dev);
	pm_runtime_put_autosuspend(gpu->dev);

	return 0;
}

static void etnaviv_gpu_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct etnaviv_gpu *gpu = dev_get_drvdata(dev);

	DBG("%s", dev_name(gpu->dev));

	hangcheck_disable(gpu);

#ifdef CONFIG_PM
	pm_runtime_get_sync(gpu->dev);
	pm_runtime_put_sync_suspend(gpu->dev);
#else
	etnaviv_gpu_hw_suspend(gpu);
#endif

	if (gpu->buffer) {
		etnaviv_cmdbuf_free(gpu->buffer);
		gpu->buffer = NULL;
	}

	if (gpu->cmdbuf_suballoc) {
		etnaviv_cmdbuf_suballoc_destroy(gpu->cmdbuf_suballoc);
		gpu->cmdbuf_suballoc = NULL;
	}

	if (gpu->mmu) {
		etnaviv_iommu_destroy(gpu->mmu);
		gpu->mmu = NULL;
	}

	gpu->drm = NULL;
}

static const struct component_ops gpu_ops = {
	.bind = etnaviv_gpu_bind,
	.unbind = etnaviv_gpu_unbind,
};

static const struct of_device_id etnaviv_gpu_match[] = {
	{
		.compatible = "vivante,gc"
	},
	{ /* sentinel */ }
};

static int etnaviv_gpu_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct etnaviv_gpu *gpu;
	int err;

	gpu = devm_kzalloc(dev, sizeof(*gpu), GFP_KERNEL);
	if (!gpu)
		return -ENOMEM;

	gpu->dev = &pdev->dev;
	mutex_init(&gpu->lock);

	/* Map registers: */
	gpu->mmio = etnaviv_ioremap(pdev, NULL, dev_name(gpu->dev));
	if (IS_ERR(gpu->mmio))
		return PTR_ERR(gpu->mmio);

	/* Get Interrupt: */
	gpu->irq = platform_get_irq(pdev, 0);
	if (gpu->irq < 0) {
		dev_err(dev, "failed to get irq: %d\n", gpu->irq);
		return gpu->irq;
	}

	err = devm_request_irq(&pdev->dev, gpu->irq, irq_handler, 0,
			       dev_name(gpu->dev), gpu);
	if (err) {
		dev_err(dev, "failed to request IRQ%u: %d\n", gpu->irq, err);
		return err;
	}

	/* Get Clocks: */
	gpu->clk_bus = devm_clk_get(&pdev->dev, "bus");
	DBG("clk_bus: %p", gpu->clk_bus);
	if (IS_ERR(gpu->clk_bus))
		gpu->clk_bus = NULL;

	gpu->clk_core = devm_clk_get(&pdev->dev, "core");
	DBG("clk_core: %p", gpu->clk_core);
	if (IS_ERR(gpu->clk_core))
		gpu->clk_core = NULL;

	gpu->clk_shader = devm_clk_get(&pdev->dev, "shader");
	DBG("clk_shader: %p", gpu->clk_shader);
	if (IS_ERR(gpu->clk_shader))
		gpu->clk_shader = NULL;

	/* TODO: figure out max mapped size */
	dev_set_drvdata(dev, gpu);

	/*
	 * We treat the device as initially suspended.  The runtime PM
	 * autosuspend delay is rather arbitary: no measurements have
	 * yet been performed to determine an appropriate value.
	 */
	pm_runtime_use_autosuspend(gpu->dev);
	pm_runtime_set_autosuspend_delay(gpu->dev, 200);
	pm_runtime_enable(gpu->dev);

	err = component_add(&pdev->dev, &gpu_ops);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register component: %d\n", err);
		return err;
	}

	return 0;
}

static int etnaviv_gpu_platform_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &gpu_ops);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

#ifdef CONFIG_PM
static int etnaviv_gpu_rpm_suspend(struct device *dev)
{
	struct etnaviv_gpu *gpu = dev_get_drvdata(dev);
	u32 idle, mask;

	/* If we have outstanding fences, we're not idle */
	if (gpu->completed_fence != gpu->active_fence)
		return -EBUSY;

	/* Check whether the hardware (except FE) is idle */
	mask = gpu->idle_mask & ~VIVS_HI_IDLE_STATE_FE;
	idle = gpu_read(gpu, VIVS_HI_IDLE_STATE) & mask;
	if (idle != mask)
		return -EBUSY;

	return etnaviv_gpu_hw_suspend(gpu);
}

static int etnaviv_gpu_rpm_resume(struct device *dev)
{
	struct etnaviv_gpu *gpu = dev_get_drvdata(dev);
	int ret;

	ret = etnaviv_gpu_clk_enable(gpu);
	if (ret)
		return ret;

	/* Re-initialise the basic hardware state */
	if (gpu->drm && gpu->buffer) {
		ret = etnaviv_gpu_hw_resume(gpu);
		if (ret) {
			etnaviv_gpu_clk_disable(gpu);
			return ret;
		}
	}

	return 0;
}
#endif

static const struct dev_pm_ops etnaviv_gpu_pm_ops = {
	SET_RUNTIME_PM_OPS(etnaviv_gpu_rpm_suspend, etnaviv_gpu_rpm_resume,
			   NULL)
};

struct platform_driver etnaviv_gpu_driver = {
	.driver = {
		.name = "etnaviv-gpu",
		.owner = THIS_MODULE,
		.pm = &etnaviv_gpu_pm_ops,
		.of_match_table = etnaviv_gpu_match,
	},
	.probe = etnaviv_gpu_platform_probe,
	.remove = etnaviv_gpu_platform_remove,
	.id_table = gpu_ids,
};
