// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Etnaviv Project
 * Copyright (C) 2017 Zodiac Inflight Innovations
 */

#include "common.xml.h"
#include "etnaviv_gpu.h"
#include "etnaviv_perfmon.h"
#include "state_hi.xml.h"

struct etnaviv_pm_domain;

struct etnaviv_pm_signal {
	char name[64];
	u32 data;

	u32 (*sample)(struct etnaviv_gpu *gpu,
		      const struct etnaviv_pm_domain *domain,
		      const struct etnaviv_pm_signal *signal);
};

struct etnaviv_pm_domain {
	char name[64];

	/* profile register */
	u32 profile_read;
	u32 profile_config;

	u8 nr_signals;
	const struct etnaviv_pm_signal *signal;
};

struct etnaviv_pm_domain_meta {
	unsigned int feature;
	const struct etnaviv_pm_domain *domains;
	u32 nr_domains;
};

static u32 perf_reg_read(struct etnaviv_gpu *gpu,
	const struct etnaviv_pm_domain *domain,
	const struct etnaviv_pm_signal *signal)
{
	gpu_write(gpu, domain->profile_config, signal->data);

	return gpu_read(gpu, domain->profile_read);
}

static u32 pipe_perf_reg_read(struct etnaviv_gpu *gpu,
	const struct etnaviv_pm_domain *domain,
	const struct etnaviv_pm_signal *signal)
{
	u32 clock = gpu_read(gpu, VIVS_HI_CLOCK_CONTROL);
	u32 value = 0;
	unsigned i;

	for (i = 0; i < gpu->identity.pixel_pipes; i++) {
		clock &= ~(VIVS_HI_CLOCK_CONTROL_DEBUG_PIXEL_PIPE__MASK);
		clock |= VIVS_HI_CLOCK_CONTROL_DEBUG_PIXEL_PIPE(i);
		gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, clock);
		value += perf_reg_read(gpu, domain, signal);
	}

	/* switch back to pixel pipe 0 to prevent GPU hang */
	clock &= ~(VIVS_HI_CLOCK_CONTROL_DEBUG_PIXEL_PIPE__MASK);
	clock |= VIVS_HI_CLOCK_CONTROL_DEBUG_PIXEL_PIPE(0);
	gpu_write(gpu, VIVS_HI_CLOCK_CONTROL, clock);

	return value;
}

static u32 hi_total_cycle_read(struct etnaviv_gpu *gpu,
	const struct etnaviv_pm_domain *domain,
	const struct etnaviv_pm_signal *signal)
{
	u32 reg = VIVS_HI_PROFILE_TOTAL_CYCLES;

	if (gpu->identity.model == chipModel_GC880 ||
		gpu->identity.model == chipModel_GC2000 ||
		gpu->identity.model == chipModel_GC2100)
		reg = VIVS_MC_PROFILE_CYCLE_COUNTER;

	return gpu_read(gpu, reg);
}

static u32 hi_total_idle_cycle_read(struct etnaviv_gpu *gpu,
	const struct etnaviv_pm_domain *domain,
	const struct etnaviv_pm_signal *signal)
{
	u32 reg = VIVS_HI_PROFILE_IDLE_CYCLES;

	if (gpu->identity.model == chipModel_GC880 ||
		gpu->identity.model == chipModel_GC2000 ||
		gpu->identity.model == chipModel_GC2100)
		reg = VIVS_HI_PROFILE_TOTAL_CYCLES;

	return gpu_read(gpu, reg);
}

static const struct etnaviv_pm_domain doms_3d[] = {
	{
		.name = "HI",
		.profile_read = VIVS_MC_PROFILE_HI_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG2,
		.nr_signals = 5,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"TOTAL_CYCLES",
				0,
				&hi_total_cycle_read
			},
			{
				"IDLE_CYCLES",
				0,
				&hi_total_idle_cycle_read
			},
			{
				"AXI_CYCLES_READ_REQUEST_STALLED",
				VIVS_MC_PROFILE_CONFIG2_HI_AXI_CYCLES_READ_REQUEST_STALLED,
				&perf_reg_read
			},
			{
				"AXI_CYCLES_WRITE_REQUEST_STALLED",
				VIVS_MC_PROFILE_CONFIG2_HI_AXI_CYCLES_WRITE_REQUEST_STALLED,
				&perf_reg_read
			},
			{
				"AXI_CYCLES_WRITE_DATA_STALLED",
				VIVS_MC_PROFILE_CONFIG2_HI_AXI_CYCLES_WRITE_DATA_STALLED,
				&perf_reg_read
			}
		}
	},
	{
		.name = "PE",
		.profile_read = VIVS_MC_PROFILE_PE_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG0,
		.nr_signals = 4,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"PIXEL_COUNT_KILLED_BY_COLOR_PIPE",
				VIVS_MC_PROFILE_CONFIG0_PE_PIXEL_COUNT_KILLED_BY_COLOR_PIPE,
				&pipe_perf_reg_read
			},
			{
				"PIXEL_COUNT_KILLED_BY_DEPTH_PIPE",
				VIVS_MC_PROFILE_CONFIG0_PE_PIXEL_COUNT_KILLED_BY_DEPTH_PIPE,
				&pipe_perf_reg_read
			},
			{
				"PIXEL_COUNT_DRAWN_BY_COLOR_PIPE",
				VIVS_MC_PROFILE_CONFIG0_PE_PIXEL_COUNT_DRAWN_BY_COLOR_PIPE,
				&pipe_perf_reg_read
			},
			{
				"PIXEL_COUNT_DRAWN_BY_DEPTH_PIPE",
				VIVS_MC_PROFILE_CONFIG0_PE_PIXEL_COUNT_DRAWN_BY_DEPTH_PIPE,
				&pipe_perf_reg_read
			}
		}
	},
	{
		.name = "SH",
		.profile_read = VIVS_MC_PROFILE_SH_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG0,
		.nr_signals = 9,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"SHADER_CYCLES",
				VIVS_MC_PROFILE_CONFIG0_SH_SHADER_CYCLES,
				&perf_reg_read
			},
			{
				"PS_INST_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_PS_INST_COUNTER,
				&perf_reg_read
			},
			{
				"RENDERED_PIXEL_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_RENDERED_PIXEL_COUNTER,
				&perf_reg_read
			},
			{
				"VS_INST_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_VS_INST_COUNTER,
				&pipe_perf_reg_read
			},
			{
				"RENDERED_VERTICE_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_RENDERED_VERTICE_COUNTER,
				&pipe_perf_reg_read
			},
			{
				"VTX_BRANCH_INST_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_VTX_BRANCH_INST_COUNTER,
				&pipe_perf_reg_read
			},
			{
				"VTX_TEXLD_INST_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_VTX_TEXLD_INST_COUNTER,
				&pipe_perf_reg_read
			},
			{
				"PXL_BRANCH_INST_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_PXL_BRANCH_INST_COUNTER,
				&pipe_perf_reg_read
			},
			{
				"PXL_TEXLD_INST_COUNTER",
				VIVS_MC_PROFILE_CONFIG0_SH_PXL_TEXLD_INST_COUNTER,
				&pipe_perf_reg_read
			}
		}
	},
	{
		.name = "PA",
		.profile_read = VIVS_MC_PROFILE_PA_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG1,
		.nr_signals = 6,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"INPUT_VTX_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_PA_INPUT_VTX_COUNTER,
				&perf_reg_read
			},
			{
				"INPUT_PRIM_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_PA_INPUT_PRIM_COUNTER,
				&perf_reg_read
			},
			{
				"OUTPUT_PRIM_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_PA_OUTPUT_PRIM_COUNTER,
				&perf_reg_read
			},
			{
				"DEPTH_CLIPPED_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_PA_DEPTH_CLIPPED_COUNTER,
				&pipe_perf_reg_read
			},
			{
				"TRIVIAL_REJECTED_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_PA_TRIVIAL_REJECTED_COUNTER,
				&pipe_perf_reg_read
			},
			{
				"CULLED_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_PA_CULLED_COUNTER,
				&pipe_perf_reg_read
			}
		}
	},
	{
		.name = "SE",
		.profile_read = VIVS_MC_PROFILE_SE_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG1,
		.nr_signals = 2,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"CULLED_TRIANGLE_COUNT",
				VIVS_MC_PROFILE_CONFIG1_SE_CULLED_TRIANGLE_COUNT,
				&perf_reg_read
			},
			{
				"CULLED_LINES_COUNT",
				VIVS_MC_PROFILE_CONFIG1_SE_CULLED_LINES_COUNT,
				&perf_reg_read
			}
		}
	},
	{
		.name = "RA",
		.profile_read = VIVS_MC_PROFILE_RA_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG1,
		.nr_signals = 7,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"VALID_PIXEL_COUNT",
				VIVS_MC_PROFILE_CONFIG1_RA_VALID_PIXEL_COUNT,
				&perf_reg_read
			},
			{
				"TOTAL_QUAD_COUNT",
				VIVS_MC_PROFILE_CONFIG1_RA_TOTAL_QUAD_COUNT,
				&perf_reg_read
			},
			{
				"VALID_QUAD_COUNT_AFTER_EARLY_Z",
				VIVS_MC_PROFILE_CONFIG1_RA_VALID_QUAD_COUNT_AFTER_EARLY_Z,
				&perf_reg_read
			},
			{
				"TOTAL_PRIMITIVE_COUNT",
				VIVS_MC_PROFILE_CONFIG1_RA_TOTAL_PRIMITIVE_COUNT,
				&perf_reg_read
			},
			{
				"PIPE_CACHE_MISS_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_RA_PIPE_CACHE_MISS_COUNTER,
				&perf_reg_read
			},
			{
				"PREFETCH_CACHE_MISS_COUNTER",
				VIVS_MC_PROFILE_CONFIG1_RA_PREFETCH_CACHE_MISS_COUNTER,
				&perf_reg_read
			},
			{
				"CULLED_QUAD_COUNT",
				VIVS_MC_PROFILE_CONFIG1_RA_CULLED_QUAD_COUNT,
				&perf_reg_read
			}
		}
	},
	{
		.name = "TX",
		.profile_read = VIVS_MC_PROFILE_TX_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG1,
		.nr_signals = 9,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"TOTAL_BILINEAR_REQUESTS",
				VIVS_MC_PROFILE_CONFIG1_TX_TOTAL_BILINEAR_REQUESTS,
				&perf_reg_read
			},
			{
				"TOTAL_TRILINEAR_REQUESTS",
				VIVS_MC_PROFILE_CONFIG1_TX_TOTAL_TRILINEAR_REQUESTS,
				&perf_reg_read
			},
			{
				"TOTAL_DISCARDED_TEXTURE_REQUESTS",
				VIVS_MC_PROFILE_CONFIG1_TX_TOTAL_DISCARDED_TEXTURE_REQUESTS,
				&perf_reg_read
			},
			{
				"TOTAL_TEXTURE_REQUESTS",
				VIVS_MC_PROFILE_CONFIG1_TX_TOTAL_TEXTURE_REQUESTS,
				&perf_reg_read
			},
			{
				"MEM_READ_COUNT",
				VIVS_MC_PROFILE_CONFIG1_TX_MEM_READ_COUNT,
				&perf_reg_read
			},
			{
				"MEM_READ_IN_8B_COUNT",
				VIVS_MC_PROFILE_CONFIG1_TX_MEM_READ_IN_8B_COUNT,
				&perf_reg_read
			},
			{
				"CACHE_MISS_COUNT",
				VIVS_MC_PROFILE_CONFIG1_TX_CACHE_MISS_COUNT,
				&perf_reg_read
			},
			{
				"CACHE_HIT_TEXEL_COUNT",
				VIVS_MC_PROFILE_CONFIG1_TX_CACHE_HIT_TEXEL_COUNT,
				&perf_reg_read
			},
			{
				"CACHE_MISS_TEXEL_COUNT",
				VIVS_MC_PROFILE_CONFIG1_TX_CACHE_MISS_TEXEL_COUNT,
				&perf_reg_read
			}
		}
	},
	{
		.name = "MC",
		.profile_read = VIVS_MC_PROFILE_MC_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG2,
		.nr_signals = 3,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"TOTAL_READ_REQ_8B_FROM_PIPELINE",
				VIVS_MC_PROFILE_CONFIG2_MC_TOTAL_READ_REQ_8B_FROM_PIPELINE,
				&perf_reg_read
			},
			{
				"TOTAL_READ_REQ_8B_FROM_IP",
				VIVS_MC_PROFILE_CONFIG2_MC_TOTAL_READ_REQ_8B_FROM_IP,
				&perf_reg_read
			},
			{
				"TOTAL_WRITE_REQ_8B_FROM_PIPELINE",
				VIVS_MC_PROFILE_CONFIG2_MC_TOTAL_WRITE_REQ_8B_FROM_PIPELINE,
				&perf_reg_read
			}
		}
	}
};

static const struct etnaviv_pm_domain doms_2d[] = {
	{
		.name = "PE",
		.profile_read = VIVS_MC_PROFILE_PE_READ,
		.profile_config = VIVS_MC_PROFILE_CONFIG0,
		.nr_signals = 1,
		.signal = (const struct etnaviv_pm_signal[]) {
			{
				"PIXELS_RENDERED_2D",
				VIVS_MC_PROFILE_CONFIG0_PE_PIXELS_RENDERED_2D,
				&pipe_perf_reg_read
			}
		}
	}
};

static const struct etnaviv_pm_domain doms_vg[] = {
};

static const struct etnaviv_pm_domain_meta doms_meta[] = {
	{
		.feature = chipFeatures_PIPE_3D,
		.nr_domains = ARRAY_SIZE(doms_3d),
		.domains = &doms_3d[0]
	},
	{
		.feature = chipFeatures_PIPE_2D,
		.nr_domains = ARRAY_SIZE(doms_2d),
		.domains = &doms_2d[0]
	},
	{
		.feature = chipFeatures_PIPE_VG,
		.nr_domains = ARRAY_SIZE(doms_vg),
		.domains = &doms_vg[0]
	}
};

static unsigned int num_pm_domains(const struct etnaviv_gpu *gpu)
{
	unsigned int num = 0, i;

	for (i = 0; i < ARRAY_SIZE(doms_meta); i++) {
		const struct etnaviv_pm_domain_meta *meta = &doms_meta[i];

		if (gpu->identity.features & meta->feature)
			num += meta->nr_domains;
	}

	return num;
}

static const struct etnaviv_pm_domain *pm_domain(const struct etnaviv_gpu *gpu,
	unsigned int index)
{
	const struct etnaviv_pm_domain *domain = NULL;
	unsigned int offset = 0, i;

	for (i = 0; i < ARRAY_SIZE(doms_meta); i++) {
		const struct etnaviv_pm_domain_meta *meta = &doms_meta[i];

		if (!(gpu->identity.features & meta->feature))
			continue;

		if (index - offset >= meta->nr_domains) {
			offset += meta->nr_domains;
			continue;
		}

		domain = meta->domains + (index - offset);
	}

	return domain;
}

int etnaviv_pm_query_dom(struct etnaviv_gpu *gpu,
	struct drm_etnaviv_pm_domain *domain)
{
	const unsigned int nr_domains = num_pm_domains(gpu);
	const struct etnaviv_pm_domain *dom;

	if (domain->iter >= nr_domains)
		return -EINVAL;

	dom = pm_domain(gpu, domain->iter);
	if (!dom)
		return -EINVAL;

	domain->id = domain->iter;
	domain->nr_signals = dom->nr_signals;
	strncpy(domain->name, dom->name, sizeof(domain->name));

	domain->iter++;
	if (domain->iter == nr_domains)
		domain->iter = 0xff;

	return 0;
}

int etnaviv_pm_query_sig(struct etnaviv_gpu *gpu,
	struct drm_etnaviv_pm_signal *signal)
{
	const unsigned int nr_domains = num_pm_domains(gpu);
	const struct etnaviv_pm_domain *dom;
	const struct etnaviv_pm_signal *sig;

	if (signal->domain >= nr_domains)
		return -EINVAL;

	dom = pm_domain(gpu, signal->domain);
	if (!dom)
		return -EINVAL;

	if (signal->iter >= dom->nr_signals)
		return -EINVAL;

	sig = &dom->signal[signal->iter];

	signal->id = signal->iter;
	strncpy(signal->name, sig->name, sizeof(signal->name));

	signal->iter++;
	if (signal->iter == dom->nr_signals)
		signal->iter = 0xffff;

	return 0;
}

int etnaviv_pm_req_validate(const struct drm_etnaviv_gem_submit_pmr *r,
	u32 exec_state)
{
	const struct etnaviv_pm_domain_meta *meta = &doms_meta[exec_state];
	const struct etnaviv_pm_domain *dom;

	if (r->domain >= meta->nr_domains)
		return -EINVAL;

	dom = meta->domains + r->domain;

	if (r->signal >= dom->nr_signals)
		return -EINVAL;

	return 0;
}

void etnaviv_perfmon_process(struct etnaviv_gpu *gpu,
	const struct etnaviv_perfmon_request *pmr, u32 exec_state)
{
	const struct etnaviv_pm_domain_meta *meta = &doms_meta[exec_state];
	const struct etnaviv_pm_domain *dom;
	const struct etnaviv_pm_signal *sig;
	u32 *bo = pmr->bo_vma;
	u32 val;

	dom = meta->domains + pmr->domain;
	sig = &dom->signal[pmr->signal];
	val = sig->sample(gpu, dom, sig);

	*(bo + pmr->offset) = val;
}
