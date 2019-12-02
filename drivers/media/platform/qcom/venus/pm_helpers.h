/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Linaro Ltd. */
#ifndef __VENUS_PM_HELPERS_H__
#define __VENUS_PM_HELPERS_H__

struct device;

#define POWER_ON	1
#define POWER_OFF	0

struct venus_pm_ops {
	int (*core_get)(struct device *dev);
	void (*core_put)(struct device *dev);
	int (*core_power)(struct device *dev, int on);

	int (*vdec_get)(struct device *dev);
	void (*vdec_put)(struct device *dev);
	int (*vdec_power)(struct device *dev, int on);

	int (*venc_get)(struct device *dev);
	void (*venc_put)(struct device *dev);
	int (*venc_power)(struct device *dev, int on);

	int (*coreid_power)(struct venus_inst *inst, int on);

	int (*load_scale)(struct venus_inst *inst);
};

const struct venus_pm_ops *venus_pm_get(enum hfi_version version);

static inline int venus_pm_load_scale(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;

	if (!core->pm_ops || !core->pm_ops->load_scale)
		return 0;

	return core->pm_ops->load_scale(inst);
}

static inline int venus_pm_acquire_core(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret = 0;

	if (pm_ops && pm_ops->coreid_power)
		ret = pm_ops->coreid_power(inst, POWER_ON);

	return ret;
}

static inline int venus_pm_release_core(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret = 0;

	if (pm_ops && pm_ops->coreid_power)
		ret = pm_ops->coreid_power(inst, POWER_OFF);

	return ret;
}

#endif
