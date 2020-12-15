// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include "lima_device.h"
#include "lima_gp.h"
#include "lima_pp.h"
#include "lima_mmu.h"
#include "lima_pmu.h"
#include "lima_l2_cache.h"
#include "lima_dlbu.h"
#include "lima_bcast.h"
#include "lima_vm.h"

struct lima_ip_desc {
	char *name;
	char *irq_name;
	bool must_have[lima_gpu_num];
	int offset[lima_gpu_num];

	int (*init)(struct lima_ip *ip);
	void (*fini)(struct lima_ip *ip);
	int (*resume)(struct lima_ip *ip);
	void (*suspend)(struct lima_ip *ip);
};

#define LIMA_IP_DESC(ipname, mst0, mst1, off0, off1, func, irq) \
	[lima_ip_##ipname] = { \
		.name = #ipname, \
		.irq_name = irq, \
		.must_have = { \
			[lima_gpu_mali400] = mst0, \
			[lima_gpu_mali450] = mst1, \
		}, \
		.offset = { \
			[lima_gpu_mali400] = off0, \
			[lima_gpu_mali450] = off1, \
		}, \
		.init = lima_##func##_init, \
		.fini = lima_##func##_fini, \
		.resume = lima_##func##_resume, \
		.suspend = lima_##func##_suspend, \
	}

static struct lima_ip_desc lima_ip_desc[lima_ip_num] = {
	LIMA_IP_DESC(pmu,         false, false, 0x02000, 0x02000, pmu,      "pmu"),
	LIMA_IP_DESC(l2_cache0,   true,  true,  0x01000, 0x10000, l2_cache, NULL),
	LIMA_IP_DESC(l2_cache1,   false, true,  -1,      0x01000, l2_cache, NULL),
	LIMA_IP_DESC(l2_cache2,   false, false, -1,      0x11000, l2_cache, NULL),
	LIMA_IP_DESC(gp,          true,  true,  0x00000, 0x00000, gp,       "gp"),
	LIMA_IP_DESC(pp0,         true,  true,  0x08000, 0x08000, pp,       "pp0"),
	LIMA_IP_DESC(pp1,         false, false, 0x0A000, 0x0A000, pp,       "pp1"),
	LIMA_IP_DESC(pp2,         false, false, 0x0C000, 0x0C000, pp,       "pp2"),
	LIMA_IP_DESC(pp3,         false, false, 0x0E000, 0x0E000, pp,       "pp3"),
	LIMA_IP_DESC(pp4,         false, false, -1,      0x28000, pp,       "pp4"),
	LIMA_IP_DESC(pp5,         false, false, -1,      0x2A000, pp,       "pp5"),
	LIMA_IP_DESC(pp6,         false, false, -1,      0x2C000, pp,       "pp6"),
	LIMA_IP_DESC(pp7,         false, false, -1,      0x2E000, pp,       "pp7"),
	LIMA_IP_DESC(gpmmu,       true,  true,  0x03000, 0x03000, mmu,      "gpmmu"),
	LIMA_IP_DESC(ppmmu0,      true,  true,  0x04000, 0x04000, mmu,      "ppmmu0"),
	LIMA_IP_DESC(ppmmu1,      false, false, 0x05000, 0x05000, mmu,      "ppmmu1"),
	LIMA_IP_DESC(ppmmu2,      false, false, 0x06000, 0x06000, mmu,      "ppmmu2"),
	LIMA_IP_DESC(ppmmu3,      false, false, 0x07000, 0x07000, mmu,      "ppmmu3"),
	LIMA_IP_DESC(ppmmu4,      false, false, -1,      0x1C000, mmu,      "ppmmu4"),
	LIMA_IP_DESC(ppmmu5,      false, false, -1,      0x1D000, mmu,      "ppmmu5"),
	LIMA_IP_DESC(ppmmu6,      false, false, -1,      0x1E000, mmu,      "ppmmu6"),
	LIMA_IP_DESC(ppmmu7,      false, false, -1,      0x1F000, mmu,      "ppmmu7"),
	LIMA_IP_DESC(dlbu,        false, true,  -1,      0x14000, dlbu,     NULL),
	LIMA_IP_DESC(bcast,       false, true,  -1,      0x13000, bcast,    NULL),
	LIMA_IP_DESC(pp_bcast,    false, true,  -1,      0x16000, pp_bcast, "pp"),
	LIMA_IP_DESC(ppmmu_bcast, false, true,  -1,      0x15000, mmu,      NULL),
};

const char *lima_ip_name(struct lima_ip *ip)
{
	return lima_ip_desc[ip->id].name;
}

static int lima_clk_enable(struct lima_device *dev)
{
	int err;

	err = clk_prepare_enable(dev->clk_bus);
	if (err)
		return err;

	err = clk_prepare_enable(dev->clk_gpu);
	if (err)
		goto error_out0;

	if (dev->reset) {
		err = reset_control_deassert(dev->reset);
		if (err) {
			dev_err(dev->dev,
				"reset controller deassert failed %d\n", err);
			goto error_out1;
		}
	}

	return 0;

error_out1:
	clk_disable_unprepare(dev->clk_gpu);
error_out0:
	clk_disable_unprepare(dev->clk_bus);
	return err;
}

static void lima_clk_disable(struct lima_device *dev)
{
	if (dev->reset)
		reset_control_assert(dev->reset);
	clk_disable_unprepare(dev->clk_gpu);
	clk_disable_unprepare(dev->clk_bus);
}

static int lima_clk_init(struct lima_device *dev)
{
	int err;

	dev->clk_bus = devm_clk_get(dev->dev, "bus");
	if (IS_ERR(dev->clk_bus)) {
		err = PTR_ERR(dev->clk_bus);
		if (err != -EPROBE_DEFER)
			dev_err(dev->dev, "get bus clk failed %d\n", err);
		dev->clk_bus = NULL;
		return err;
	}

	dev->clk_gpu = devm_clk_get(dev->dev, "core");
	if (IS_ERR(dev->clk_gpu)) {
		err = PTR_ERR(dev->clk_gpu);
		if (err != -EPROBE_DEFER)
			dev_err(dev->dev, "get core clk failed %d\n", err);
		dev->clk_gpu = NULL;
		return err;
	}

	dev->reset = devm_reset_control_array_get_optional_shared(dev->dev);
	if (IS_ERR(dev->reset)) {
		err = PTR_ERR(dev->reset);
		if (err != -EPROBE_DEFER)
			dev_err(dev->dev, "get reset controller failed %d\n",
				err);
		dev->reset = NULL;
		return err;
	}

	return lima_clk_enable(dev);
}

static void lima_clk_fini(struct lima_device *dev)
{
	lima_clk_disable(dev);
}

static int lima_regulator_enable(struct lima_device *dev)
{
	int ret;

	if (!dev->regulator)
		return 0;

	ret = regulator_enable(dev->regulator);
	if (ret < 0) {
		dev_err(dev->dev, "failed to enable regulator: %d\n", ret);
		return ret;
	}

	return 0;
}

static void lima_regulator_disable(struct lima_device *dev)
{
	if (dev->regulator)
		regulator_disable(dev->regulator);
}

static int lima_regulator_init(struct lima_device *dev)
{
	int ret;

	dev->regulator = devm_regulator_get_optional(dev->dev, "mali");
	if (IS_ERR(dev->regulator)) {
		ret = PTR_ERR(dev->regulator);
		dev->regulator = NULL;
		if (ret == -ENODEV)
			return 0;
		if (ret != -EPROBE_DEFER)
			dev_err(dev->dev, "failed to get regulator: %d\n", ret);
		return ret;
	}

	return lima_regulator_enable(dev);
}

static void lima_regulator_fini(struct lima_device *dev)
{
	lima_regulator_disable(dev);
}

static int lima_init_ip(struct lima_device *dev, int index)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct lima_ip_desc *desc = lima_ip_desc + index;
	struct lima_ip *ip = dev->ip + index;
	const char *irq_name = desc->irq_name;
	int offset = desc->offset[dev->id];
	bool must = desc->must_have[dev->id];
	int err;

	if (offset < 0)
		return 0;

	ip->dev = dev;
	ip->id = index;
	ip->iomem = dev->iomem + offset;
	if (irq_name) {
		err = must ? platform_get_irq_byname(pdev, irq_name) :
			     platform_get_irq_byname_optional(pdev, irq_name);
		if (err < 0)
			goto out;
		ip->irq = err;
	}

	err = desc->init(ip);
	if (!err) {
		ip->present = true;
		return 0;
	}

out:
	return must ? err : 0;
}

static void lima_fini_ip(struct lima_device *ldev, int index)
{
	struct lima_ip_desc *desc = lima_ip_desc + index;
	struct lima_ip *ip = ldev->ip + index;

	if (ip->present)
		desc->fini(ip);
}

static int lima_resume_ip(struct lima_device *ldev, int index)
{
	struct lima_ip_desc *desc = lima_ip_desc + index;
	struct lima_ip *ip = ldev->ip + index;
	int ret = 0;

	if (ip->present)
		ret = desc->resume(ip);

	return ret;
}

static void lima_suspend_ip(struct lima_device *ldev, int index)
{
	struct lima_ip_desc *desc = lima_ip_desc + index;
	struct lima_ip *ip = ldev->ip + index;

	if (ip->present)
		desc->suspend(ip);
}

static int lima_init_gp_pipe(struct lima_device *dev)
{
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_gp;
	int err;

	pipe->ldev = dev;

	err = lima_sched_pipe_init(pipe, "gp");
	if (err)
		return err;

	pipe->l2_cache[pipe->num_l2_cache++] = dev->ip + lima_ip_l2_cache0;
	pipe->mmu[pipe->num_mmu++] = dev->ip + lima_ip_gpmmu;
	pipe->processor[pipe->num_processor++] = dev->ip + lima_ip_gp;

	err = lima_gp_pipe_init(dev);
	if (err) {
		lima_sched_pipe_fini(pipe);
		return err;
	}

	return 0;
}

static void lima_fini_gp_pipe(struct lima_device *dev)
{
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_gp;

	lima_gp_pipe_fini(dev);
	lima_sched_pipe_fini(pipe);
}

static int lima_init_pp_pipe(struct lima_device *dev)
{
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;
	int err, i;

	pipe->ldev = dev;

	err = lima_sched_pipe_init(pipe, "pp");
	if (err)
		return err;

	for (i = 0; i < LIMA_SCHED_PIPE_MAX_PROCESSOR; i++) {
		struct lima_ip *pp = dev->ip + lima_ip_pp0 + i;
		struct lima_ip *ppmmu = dev->ip + lima_ip_ppmmu0 + i;
		struct lima_ip *l2_cache;

		if (dev->id == lima_gpu_mali400)
			l2_cache = dev->ip + lima_ip_l2_cache0;
		else
			l2_cache = dev->ip + lima_ip_l2_cache1 + (i >> 2);

		if (pp->present && ppmmu->present && l2_cache->present) {
			pipe->mmu[pipe->num_mmu++] = ppmmu;
			pipe->processor[pipe->num_processor++] = pp;
			if (!pipe->l2_cache[i >> 2])
				pipe->l2_cache[pipe->num_l2_cache++] = l2_cache;
		}
	}

	if (dev->ip[lima_ip_bcast].present) {
		pipe->bcast_processor = dev->ip + lima_ip_pp_bcast;
		pipe->bcast_mmu = dev->ip + lima_ip_ppmmu_bcast;
	}

	err = lima_pp_pipe_init(dev);
	if (err) {
		lima_sched_pipe_fini(pipe);
		return err;
	}

	return 0;
}

static void lima_fini_pp_pipe(struct lima_device *dev)
{
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;

	lima_pp_pipe_fini(dev);
	lima_sched_pipe_fini(pipe);
}

int lima_device_init(struct lima_device *ldev)
{
	struct platform_device *pdev = to_platform_device(ldev->dev);
	int err, i;

	dma_set_coherent_mask(ldev->dev, DMA_BIT_MASK(32));

	err = lima_clk_init(ldev);
	if (err)
		return err;

	err = lima_regulator_init(ldev);
	if (err)
		goto err_out0;

	ldev->empty_vm = lima_vm_create(ldev);
	if (!ldev->empty_vm) {
		err = -ENOMEM;
		goto err_out1;
	}

	ldev->va_start = 0;
	if (ldev->id == lima_gpu_mali450) {
		ldev->va_end = LIMA_VA_RESERVE_START;
		ldev->dlbu_cpu = dma_alloc_wc(
			ldev->dev, LIMA_PAGE_SIZE,
			&ldev->dlbu_dma, GFP_KERNEL | __GFP_NOWARN);
		if (!ldev->dlbu_cpu) {
			err = -ENOMEM;
			goto err_out2;
		}
	} else
		ldev->va_end = LIMA_VA_RESERVE_END;

	ldev->iomem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ldev->iomem)) {
		dev_err(ldev->dev, "fail to ioremap iomem\n");
		err = PTR_ERR(ldev->iomem);
		goto err_out3;
	}

	for (i = 0; i < lima_ip_num; i++) {
		err = lima_init_ip(ldev, i);
		if (err)
			goto err_out4;
	}

	err = lima_init_gp_pipe(ldev);
	if (err)
		goto err_out4;

	err = lima_init_pp_pipe(ldev);
	if (err)
		goto err_out5;

	ldev->dump.magic = LIMA_DUMP_MAGIC;
	ldev->dump.version_major = LIMA_DUMP_MAJOR;
	ldev->dump.version_minor = LIMA_DUMP_MINOR;
	INIT_LIST_HEAD(&ldev->error_task_list);
	mutex_init(&ldev->error_task_list_lock);

	dev_info(ldev->dev, "bus rate = %lu\n", clk_get_rate(ldev->clk_bus));
	dev_info(ldev->dev, "mod rate = %lu", clk_get_rate(ldev->clk_gpu));

	return 0;

err_out5:
	lima_fini_gp_pipe(ldev);
err_out4:
	while (--i >= 0)
		lima_fini_ip(ldev, i);
err_out3:
	if (ldev->dlbu_cpu)
		dma_free_wc(ldev->dev, LIMA_PAGE_SIZE,
			    ldev->dlbu_cpu, ldev->dlbu_dma);
err_out2:
	lima_vm_put(ldev->empty_vm);
err_out1:
	lima_regulator_fini(ldev);
err_out0:
	lima_clk_fini(ldev);
	return err;
}

void lima_device_fini(struct lima_device *ldev)
{
	int i;
	struct lima_sched_error_task *et, *tmp;

	list_for_each_entry_safe(et, tmp, &ldev->error_task_list, list) {
		list_del(&et->list);
		kvfree(et);
	}
	mutex_destroy(&ldev->error_task_list_lock);

	lima_fini_pp_pipe(ldev);
	lima_fini_gp_pipe(ldev);

	for (i = lima_ip_num - 1; i >= 0; i--)
		lima_fini_ip(ldev, i);

	if (ldev->dlbu_cpu)
		dma_free_wc(ldev->dev, LIMA_PAGE_SIZE,
			    ldev->dlbu_cpu, ldev->dlbu_dma);

	lima_vm_put(ldev->empty_vm);

	lima_regulator_fini(ldev);

	lima_clk_fini(ldev);
}

int lima_device_resume(struct device *dev)
{
	struct lima_device *ldev = dev_get_drvdata(dev);
	int i, err;

	err = lima_clk_enable(ldev);
	if (err) {
		dev_err(dev, "resume clk fail %d\n", err);
		return err;
	}

	err = lima_regulator_enable(ldev);
	if (err) {
		dev_err(dev, "resume regulator fail %d\n", err);
		goto err_out0;
	}

	for (i = 0; i < lima_ip_num; i++) {
		err = lima_resume_ip(ldev, i);
		if (err) {
			dev_err(dev, "resume ip %d fail\n", i);
			goto err_out1;
		}
	}

	err = lima_devfreq_resume(&ldev->devfreq);
	if (err) {
		dev_err(dev, "devfreq resume fail\n");
		goto err_out1;
	}

	return 0;

err_out1:
	while (--i >= 0)
		lima_suspend_ip(ldev, i);
	lima_regulator_disable(ldev);
err_out0:
	lima_clk_disable(ldev);
	return err;
}

int lima_device_suspend(struct device *dev)
{
	struct lima_device *ldev = dev_get_drvdata(dev);
	int i, err;

	/* check any task running */
	for (i = 0; i < lima_pipe_num; i++) {
		if (atomic_read(&ldev->pipe[i].base.hw_rq_count))
			return -EBUSY;
	}

	err = lima_devfreq_suspend(&ldev->devfreq);
	if (err) {
		dev_err(dev, "devfreq suspend fail\n");
		return err;
	}

	for (i = lima_ip_num - 1; i >= 0; i--)
		lima_suspend_ip(ldev, i);

	lima_regulator_disable(ldev);

	lima_clk_disable(ldev);

	return 0;
}
