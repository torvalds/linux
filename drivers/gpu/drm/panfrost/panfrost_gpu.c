// SPDX-License-Identifier: GPL-2.0
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd., Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "panfrost_device.h"
#include "panfrost_features.h"
#include "panfrost_issues.h"
#include "panfrost_gpu.h"
#include "panfrost_perfcnt.h"
#include "panfrost_regs.h"

static irqreturn_t panfrost_gpu_irq_handler(int irq, void *data)
{
	struct panfrost_device *pfdev = data;
	u32 state = gpu_read(pfdev, GPU_INT_STAT);
	u32 fault_status = gpu_read(pfdev, GPU_FAULT_STATUS);

	if (!state)
		return IRQ_NONE;

	if (state & GPU_IRQ_MASK_ERROR) {
		u64 address = (u64) gpu_read(pfdev, GPU_FAULT_ADDRESS_HI) << 32;
		address |= gpu_read(pfdev, GPU_FAULT_ADDRESS_LO);

		dev_warn(pfdev->dev, "GPU Fault 0x%08x (%s) at 0x%016llx\n",
			 fault_status, panfrost_exception_name(fault_status & 0xFF),
			 address);

		if (state & GPU_IRQ_MULTIPLE_FAULT)
			dev_warn(pfdev->dev, "There were multiple GPU faults - some have not been reported\n");

		gpu_write(pfdev, GPU_INT_MASK, 0);
	}

	if (state & GPU_IRQ_PERFCNT_SAMPLE_COMPLETED)
		panfrost_perfcnt_sample_done(pfdev);

	if (state & GPU_IRQ_CLEAN_CACHES_COMPLETED)
		panfrost_perfcnt_clean_cache_done(pfdev);

	gpu_write(pfdev, GPU_INT_CLEAR, state);

	return IRQ_HANDLED;
}

int panfrost_gpu_soft_reset(struct panfrost_device *pfdev)
{
	int ret;
	u32 val;

	gpu_write(pfdev, GPU_INT_MASK, 0);
	gpu_write(pfdev, GPU_INT_CLEAR, GPU_IRQ_RESET_COMPLETED);
	gpu_write(pfdev, GPU_CMD, GPU_CMD_SOFT_RESET);

	ret = readl_relaxed_poll_timeout(pfdev->iomem + GPU_INT_RAWSTAT,
		val, val & GPU_IRQ_RESET_COMPLETED, 100, 10000);

	if (ret) {
		dev_err(pfdev->dev, "gpu soft reset timed out\n");
		return ret;
	}

	gpu_write(pfdev, GPU_INT_CLEAR, GPU_IRQ_MASK_ALL);
	gpu_write(pfdev, GPU_INT_MASK, GPU_IRQ_MASK_ALL);

	return 0;
}

void panfrost_gpu_amlogic_quirk(struct panfrost_device *pfdev)
{
	/*
	 * The Amlogic integrated Mali-T820, Mali-G31 & Mali-G52 needs
	 * these undocumented bits in GPU_PWR_OVERRIDE1 to be set in order
	 * to operate correctly.
	 */
	gpu_write(pfdev, GPU_PWR_KEY, GPU_PWR_KEY_UNLOCK);
	gpu_write(pfdev, GPU_PWR_OVERRIDE1, 0xfff | (0x20 << 16));
}

static void panfrost_gpu_init_quirks(struct panfrost_device *pfdev)
{
	u32 quirks = 0;

	if (panfrost_has_hw_issue(pfdev, HW_ISSUE_8443) ||
	    panfrost_has_hw_issue(pfdev, HW_ISSUE_11035))
		quirks |= SC_LS_PAUSEBUFFER_DISABLE;

	if (panfrost_has_hw_issue(pfdev, HW_ISSUE_10327))
		quirks |= SC_SDC_DISABLE_OQ_DISCARD;

	if (panfrost_has_hw_issue(pfdev, HW_ISSUE_10797))
		quirks |= SC_ENABLE_TEXGRD_FLAGS;

	if (!panfrost_has_hw_issue(pfdev, GPUCORE_1619)) {
		if (panfrost_model_cmp(pfdev, 0x750) < 0) /* T60x, T62x, T72x */
			quirks |= SC_LS_ATTR_CHECK_DISABLE;
		else if (panfrost_model_cmp(pfdev, 0x880) <= 0) /* T76x, T8xx */
			quirks |= SC_LS_ALLOW_ATTR_TYPES;
	}

	if (panfrost_has_hw_feature(pfdev, HW_FEATURE_TLS_HASHING))
		quirks |= SC_TLS_HASH_ENABLE;

	if (quirks)
		gpu_write(pfdev, GPU_SHADER_CONFIG, quirks);


	quirks = gpu_read(pfdev, GPU_TILER_CONFIG);

	/* Set tiler clock gate override if required */
	if (panfrost_has_hw_issue(pfdev, HW_ISSUE_T76X_3953))
		quirks |= TC_CLOCK_GATE_OVERRIDE;

	gpu_write(pfdev, GPU_TILER_CONFIG, quirks);


	quirks = gpu_read(pfdev, GPU_L2_MMU_CONFIG);

	/* Limit read & write ID width for AXI */
	if (panfrost_has_hw_feature(pfdev, HW_FEATURE_3BIT_EXT_RW_L2_MMU_CONFIG))
		quirks &= ~(L2_MMU_CONFIG_3BIT_LIMIT_EXTERNAL_READS |
			    L2_MMU_CONFIG_3BIT_LIMIT_EXTERNAL_WRITES);
	else
		quirks &= ~(L2_MMU_CONFIG_LIMIT_EXTERNAL_READS |
			    L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES);

	gpu_write(pfdev, GPU_L2_MMU_CONFIG, quirks);

	quirks = 0;
	if ((panfrost_model_eq(pfdev, 0x860) || panfrost_model_eq(pfdev, 0x880)) &&
	    pfdev->features.revision >= 0x2000)
		quirks |= JM_MAX_JOB_THROTTLE_LIMIT << JM_JOB_THROTTLE_LIMIT_SHIFT;
	else if (panfrost_model_eq(pfdev, 0x6000) &&
		 pfdev->features.coherency_features == COHERENCY_ACE)
		quirks |= (COHERENCY_ACE_LITE | COHERENCY_ACE) <<
			   JM_FORCE_COHERENCY_FEATURES_SHIFT;

	if (quirks)
		gpu_write(pfdev, GPU_JM_CONFIG, quirks);

	/* Here goes platform specific quirks */
	if (pfdev->comp->vendor_quirk)
		pfdev->comp->vendor_quirk(pfdev);
}

#define MAX_HW_REVS 6

struct panfrost_model {
	const char *name;
	u32 id;
	u32 id_mask;
	u64 features;
	u64 issues;
	struct {
		u32 revision;
		u64 issues;
	} revs[MAX_HW_REVS];
};

#define GPU_MODEL(_name, _id, ...) \
{\
	.name = __stringify(_name),				\
	.id = _id,						\
	.features = hw_features_##_name,			\
	.issues = hw_issues_##_name,				\
	.revs = { __VA_ARGS__ },				\
}

#define GPU_REV_EXT(name, _rev, _p, _s, stat) \
{\
	.revision = (_rev) << 12 | (_p) << 4 | (_s),		\
	.issues = hw_issues_##name##_r##_rev##p##_p##stat,	\
}
#define GPU_REV(name, r, p) GPU_REV_EXT(name, r, p, 0, )

static const struct panfrost_model gpu_models[] = {
	/* T60x has an oddball version */
	GPU_MODEL(t600, 0x600,
		GPU_REV_EXT(t600, 0, 0, 1, _15dev0)),
	GPU_MODEL(t620, 0x620,
		GPU_REV(t620, 0, 1), GPU_REV(t620, 1, 0)),
	GPU_MODEL(t720, 0x720),
	GPU_MODEL(t760, 0x750,
		GPU_REV(t760, 0, 0), GPU_REV(t760, 0, 1),
		GPU_REV_EXT(t760, 0, 1, 0, _50rel0),
		GPU_REV(t760, 0, 2), GPU_REV(t760, 0, 3)),
	GPU_MODEL(t820, 0x820),
	GPU_MODEL(t830, 0x830),
	GPU_MODEL(t860, 0x860),
	GPU_MODEL(t880, 0x880),

	GPU_MODEL(g71, 0x6000,
		GPU_REV_EXT(g71, 0, 0, 1, _05dev0)),
	GPU_MODEL(g72, 0x6001),
	GPU_MODEL(g51, 0x7000),
	GPU_MODEL(g76, 0x7001),
	GPU_MODEL(g52, 0x7002),
	GPU_MODEL(g31, 0x7003,
		GPU_REV(g31, 1, 0)),
};

static void panfrost_gpu_init_features(struct panfrost_device *pfdev)
{
	u32 gpu_id, num_js, major, minor, status, rev;
	const char *name = "unknown";
	u64 hw_feat = 0;
	u64 hw_issues = hw_issues_all;
	const struct panfrost_model *model;
	int i;

	pfdev->features.l2_features = gpu_read(pfdev, GPU_L2_FEATURES);
	pfdev->features.core_features = gpu_read(pfdev, GPU_CORE_FEATURES);
	pfdev->features.tiler_features = gpu_read(pfdev, GPU_TILER_FEATURES);
	pfdev->features.mem_features = gpu_read(pfdev, GPU_MEM_FEATURES);
	pfdev->features.mmu_features = gpu_read(pfdev, GPU_MMU_FEATURES);
	pfdev->features.thread_features = gpu_read(pfdev, GPU_THREAD_FEATURES);
	pfdev->features.max_threads = gpu_read(pfdev, GPU_THREAD_MAX_THREADS);
	pfdev->features.thread_max_workgroup_sz = gpu_read(pfdev, GPU_THREAD_MAX_WORKGROUP_SIZE);
	pfdev->features.thread_max_barrier_sz = gpu_read(pfdev, GPU_THREAD_MAX_BARRIER_SIZE);
	pfdev->features.coherency_features = gpu_read(pfdev, GPU_COHERENCY_FEATURES);
	pfdev->features.afbc_features = gpu_read(pfdev, GPU_AFBC_FEATURES);
	for (i = 0; i < 4; i++)
		pfdev->features.texture_features[i] = gpu_read(pfdev, GPU_TEXTURE_FEATURES(i));

	pfdev->features.as_present = gpu_read(pfdev, GPU_AS_PRESENT);

	pfdev->features.js_present = gpu_read(pfdev, GPU_JS_PRESENT);
	num_js = hweight32(pfdev->features.js_present);
	for (i = 0; i < num_js; i++)
		pfdev->features.js_features[i] = gpu_read(pfdev, GPU_JS_FEATURES(i));

	pfdev->features.shader_present = gpu_read(pfdev, GPU_SHADER_PRESENT_LO);
	pfdev->features.shader_present |= (u64)gpu_read(pfdev, GPU_SHADER_PRESENT_HI) << 32;

	pfdev->features.tiler_present = gpu_read(pfdev, GPU_TILER_PRESENT_LO);
	pfdev->features.tiler_present |= (u64)gpu_read(pfdev, GPU_TILER_PRESENT_HI) << 32;

	pfdev->features.l2_present = gpu_read(pfdev, GPU_L2_PRESENT_LO);
	pfdev->features.l2_present |= (u64)gpu_read(pfdev, GPU_L2_PRESENT_HI) << 32;
	pfdev->features.nr_core_groups = hweight64(pfdev->features.l2_present);

	pfdev->features.stack_present = gpu_read(pfdev, GPU_STACK_PRESENT_LO);
	pfdev->features.stack_present |= (u64)gpu_read(pfdev, GPU_STACK_PRESENT_HI) << 32;

	pfdev->features.thread_tls_alloc = gpu_read(pfdev, GPU_THREAD_TLS_ALLOC);

	gpu_id = gpu_read(pfdev, GPU_ID);
	pfdev->features.revision = gpu_id & 0xffff;
	pfdev->features.id = gpu_id >> 16;

	/* The T60x has an oddball ID value. Fix it up to the standard Midgard
	 * format so we (and userspace) don't have to special case it.
	 */
	if (pfdev->features.id == 0x6956)
		pfdev->features.id = 0x0600;

	major = (pfdev->features.revision >> 12) & 0xf;
	minor = (pfdev->features.revision >> 4) & 0xff;
	status = pfdev->features.revision & 0xf;
	rev = pfdev->features.revision;

	gpu_id = pfdev->features.id;

	for (model = gpu_models; model->name; model++) {
		int best = -1;

		if (!panfrost_model_eq(pfdev, model->id))
			continue;

		name = model->name;
		hw_feat = model->features;
		hw_issues |= model->issues;
		for (i = 0; i < MAX_HW_REVS; i++) {
			if (model->revs[i].revision == rev) {
				best = i;
				break;
			} else if (model->revs[i].revision == (rev & ~0xf))
				best = i;
		}

		if (best >= 0)
			hw_issues |= model->revs[best].issues;

		break;
	}

	bitmap_from_u64(pfdev->features.hw_features, hw_feat);
	bitmap_from_u64(pfdev->features.hw_issues, hw_issues);

	dev_info(pfdev->dev, "mali-%s id 0x%x major 0x%x minor 0x%x status 0x%x",
		 name, gpu_id, major, minor, status);
	dev_info(pfdev->dev, "features: %64pb, issues: %64pb",
		 pfdev->features.hw_features,
		 pfdev->features.hw_issues);

	dev_info(pfdev->dev, "Features: L2:0x%08x Shader:0x%08x Tiler:0x%08x Mem:0x%0x MMU:0x%08x AS:0x%x JS:0x%x",
		 pfdev->features.l2_features,
		 pfdev->features.core_features,
		 pfdev->features.tiler_features,
		 pfdev->features.mem_features,
		 pfdev->features.mmu_features,
		 pfdev->features.as_present,
		 pfdev->features.js_present);

	dev_info(pfdev->dev, "shader_present=0x%0llx l2_present=0x%0llx",
		 pfdev->features.shader_present, pfdev->features.l2_present);
}

void panfrost_gpu_power_on(struct panfrost_device *pfdev)
{
	int ret;
	u32 val;
	u64 core_mask = U64_MAX;

	panfrost_gpu_init_quirks(pfdev);

	if (pfdev->features.l2_present != 1) {
		/*
		 * Only support one core group now.
		 * ~(l2_present - 1) unsets all bits in l2_present except
		 * the bottom bit. (l2_present - 2) has all the bits in
		 * the first core group set. AND them together to generate
		 * a mask of cores in the first core group.
		 */
		core_mask = ~(pfdev->features.l2_present - 1) &
			     (pfdev->features.l2_present - 2);
		dev_info_once(pfdev->dev, "using only 1st core group (%lu cores from %lu)\n",
			      hweight64(core_mask),
			      hweight64(pfdev->features.shader_present));
	}
	gpu_write(pfdev, L2_PWRON_LO, pfdev->features.l2_present & core_mask);
	ret = readl_relaxed_poll_timeout(pfdev->iomem + L2_READY_LO,
		val, val == (pfdev->features.l2_present & core_mask),
		100, 20000);
	if (ret)
		dev_err(pfdev->dev, "error powering up gpu L2");

	gpu_write(pfdev, SHADER_PWRON_LO,
		  pfdev->features.shader_present & core_mask);
	ret = readl_relaxed_poll_timeout(pfdev->iomem + SHADER_READY_LO,
		val, val == (pfdev->features.shader_present & core_mask),
		100, 20000);
	if (ret)
		dev_err(pfdev->dev, "error powering up gpu shader");

	gpu_write(pfdev, TILER_PWRON_LO, pfdev->features.tiler_present);
	ret = readl_relaxed_poll_timeout(pfdev->iomem + TILER_READY_LO,
		val, val == pfdev->features.tiler_present, 100, 1000);
	if (ret)
		dev_err(pfdev->dev, "error powering up gpu tiler");
}

void panfrost_gpu_power_off(struct panfrost_device *pfdev)
{
	gpu_write(pfdev, TILER_PWROFF_LO, 0);
	gpu_write(pfdev, SHADER_PWROFF_LO, 0);
	gpu_write(pfdev, L2_PWROFF_LO, 0);
}

int panfrost_gpu_init(struct panfrost_device *pfdev)
{
	int err, irq;

	err = panfrost_gpu_soft_reset(pfdev);
	if (err)
		return err;

	panfrost_gpu_init_features(pfdev);

	err = dma_set_mask_and_coherent(pfdev->dev,
		DMA_BIT_MASK(FIELD_GET(0xff00, pfdev->features.mmu_features)));
	if (err)
		return err;

	dma_set_max_seg_size(pfdev->dev, UINT_MAX);

	irq = platform_get_irq_byname(to_platform_device(pfdev->dev), "gpu");
	if (irq <= 0)
		return -ENODEV;

	err = devm_request_irq(pfdev->dev, irq, panfrost_gpu_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME "-gpu", pfdev);
	if (err) {
		dev_err(pfdev->dev, "failed to request gpu irq");
		return err;
	}

	panfrost_gpu_power_on(pfdev);

	return 0;
}

void panfrost_gpu_fini(struct panfrost_device *pfdev)
{
	panfrost_gpu_power_off(pfdev);
}

u32 panfrost_gpu_get_latest_flush_id(struct panfrost_device *pfdev)
{
	u32 flush_id;

	if (panfrost_has_hw_feature(pfdev, HW_FEATURE_FLUSH_REDUCTION)) {
		/* Flush reduction only makes sense when the GPU is kept powered on between jobs */
		if (pm_runtime_get_if_in_use(pfdev->dev)) {
			flush_id = gpu_read(pfdev, GPU_LATEST_FLUSH_ID);
			pm_runtime_put(pfdev->dev);
			return flush_id;
		}
	}

	return 0;
}
