/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#include "msm_gpu.h"
#include "msm_gem.h"
#include "msm_mmu.h"


/*
 * Power Management:
 */

#ifdef CONFIG_MSM_BUS_SCALING
#include <mach/board.h>
static void bs_init(struct msm_gpu *gpu)
{
	if (gpu->bus_scale_table) {
		gpu->bsc = msm_bus_scale_register_client(gpu->bus_scale_table);
		DBG("bus scale client: %08x", gpu->bsc);
	}
}

static void bs_fini(struct msm_gpu *gpu)
{
	if (gpu->bsc) {
		msm_bus_scale_unregister_client(gpu->bsc);
		gpu->bsc = 0;
	}
}

static void bs_set(struct msm_gpu *gpu, int idx)
{
	if (gpu->bsc) {
		DBG("set bus scaling: %d", idx);
		msm_bus_scale_client_update_request(gpu->bsc, idx);
	}
}
#else
static void bs_init(struct msm_gpu *gpu) {}
static void bs_fini(struct msm_gpu *gpu) {}
static void bs_set(struct msm_gpu *gpu, int idx) {}
#endif

static int enable_pwrrail(struct msm_gpu *gpu)
{
	struct drm_device *dev = gpu->dev;
	int ret = 0;

	if (gpu->gpu_reg) {
		ret = regulator_enable(gpu->gpu_reg);
		if (ret) {
			dev_err(dev->dev, "failed to enable 'gpu_reg': %d\n", ret);
			return ret;
		}
	}

	if (gpu->gpu_cx) {
		ret = regulator_enable(gpu->gpu_cx);
		if (ret) {
			dev_err(dev->dev, "failed to enable 'gpu_cx': %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int disable_pwrrail(struct msm_gpu *gpu)
{
	if (gpu->gpu_cx)
		regulator_disable(gpu->gpu_cx);
	if (gpu->gpu_reg)
		regulator_disable(gpu->gpu_reg);
	return 0;
}

static int enable_clk(struct msm_gpu *gpu)
{
	struct clk *rate_clk = NULL;
	int i;

	/* NOTE: kgsl_pwrctrl_clk() ignores grp_clks[0].. */
	for (i = ARRAY_SIZE(gpu->grp_clks) - 1; i > 0; i--) {
		if (gpu->grp_clks[i]) {
			clk_prepare(gpu->grp_clks[i]);
			rate_clk = gpu->grp_clks[i];
		}
	}

	if (rate_clk && gpu->fast_rate)
		clk_set_rate(rate_clk, gpu->fast_rate);

	for (i = ARRAY_SIZE(gpu->grp_clks) - 1; i > 0; i--)
		if (gpu->grp_clks[i])
			clk_enable(gpu->grp_clks[i]);

	return 0;
}

static int disable_clk(struct msm_gpu *gpu)
{
	struct clk *rate_clk = NULL;
	int i;

	/* NOTE: kgsl_pwrctrl_clk() ignores grp_clks[0].. */
	for (i = ARRAY_SIZE(gpu->grp_clks) - 1; i > 0; i--) {
		if (gpu->grp_clks[i]) {
			clk_disable(gpu->grp_clks[i]);
			rate_clk = gpu->grp_clks[i];
		}
	}

	if (rate_clk && gpu->slow_rate)
		clk_set_rate(rate_clk, gpu->slow_rate);

	for (i = ARRAY_SIZE(gpu->grp_clks) - 1; i > 0; i--)
		if (gpu->grp_clks[i])
			clk_unprepare(gpu->grp_clks[i]);

	return 0;
}

static int enable_axi(struct msm_gpu *gpu)
{
	if (gpu->ebi1_clk)
		clk_prepare_enable(gpu->ebi1_clk);
	if (gpu->bus_freq)
		bs_set(gpu, gpu->bus_freq);
	return 0;
}

static int disable_axi(struct msm_gpu *gpu)
{
	if (gpu->ebi1_clk)
		clk_disable_unprepare(gpu->ebi1_clk);
	if (gpu->bus_freq)
		bs_set(gpu, 0);
	return 0;
}

int msm_gpu_pm_resume(struct msm_gpu *gpu)
{
	int ret;

	DBG("%s", gpu->name);

	ret = enable_pwrrail(gpu);
	if (ret)
		return ret;

	ret = enable_clk(gpu);
	if (ret)
		return ret;

	ret = enable_axi(gpu);
	if (ret)
		return ret;

	return 0;
}

int msm_gpu_pm_suspend(struct msm_gpu *gpu)
{
	int ret;

	DBG("%s", gpu->name);

	ret = disable_axi(gpu);
	if (ret)
		return ret;

	ret = disable_clk(gpu);
	if (ret)
		return ret;

	ret = disable_pwrrail(gpu);
	if (ret)
		return ret;

	return 0;
}

/*
 * Hangcheck detection for locked gpu:
 */

static void recover_worker(struct work_struct *work)
{
	struct msm_gpu *gpu = container_of(work, struct msm_gpu, recover_work);
	struct drm_device *dev = gpu->dev;

	dev_err(dev->dev, "%s: hangcheck recover!\n", gpu->name);

	mutex_lock(&dev->struct_mutex);
	gpu->funcs->recover(gpu);
	mutex_unlock(&dev->struct_mutex);

	msm_gpu_retire(gpu);
}

static void hangcheck_timer_reset(struct msm_gpu *gpu)
{
	DBG("%s", gpu->name);
	mod_timer(&gpu->hangcheck_timer,
			round_jiffies_up(jiffies + DRM_MSM_HANGCHECK_JIFFIES));
}

static void hangcheck_handler(unsigned long data)
{
	struct msm_gpu *gpu = (struct msm_gpu *)data;
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	uint32_t fence = gpu->funcs->last_fence(gpu);

	if (fence != gpu->hangcheck_fence) {
		/* some progress has been made.. ya! */
		gpu->hangcheck_fence = fence;
	} else if (fence < gpu->submitted_fence) {
		/* no progress and not done.. hung! */
		gpu->hangcheck_fence = fence;
		dev_err(dev->dev, "%s: hangcheck detected gpu lockup!\n",
				gpu->name);
		dev_err(dev->dev, "%s:     completed fence: %u\n",
				gpu->name, fence);
		dev_err(dev->dev, "%s:     submitted fence: %u\n",
				gpu->name, gpu->submitted_fence);
		queue_work(priv->wq, &gpu->recover_work);
	}

	/* if still more pending work, reset the hangcheck timer: */
	if (gpu->submitted_fence > gpu->hangcheck_fence)
		hangcheck_timer_reset(gpu);

	/* workaround for missing irq: */
	queue_work(priv->wq, &gpu->retire_work);
}

/*
 * Cmdstream submission/retirement:
 */

static void retire_worker(struct work_struct *work)
{
	struct msm_gpu *gpu = container_of(work, struct msm_gpu, retire_work);
	struct drm_device *dev = gpu->dev;
	uint32_t fence = gpu->funcs->last_fence(gpu);

	msm_update_fence(gpu->dev, fence);

	mutex_lock(&dev->struct_mutex);

	while (!list_empty(&gpu->active_list)) {
		struct msm_gem_object *obj;

		obj = list_first_entry(&gpu->active_list,
				struct msm_gem_object, mm_list);

		if ((obj->read_fence <= fence) &&
				(obj->write_fence <= fence)) {
			/* move to inactive: */
			msm_gem_move_to_inactive(&obj->base);
			msm_gem_put_iova(&obj->base, gpu->id);
			drm_gem_object_unreference(&obj->base);
		} else {
			break;
		}
	}

	mutex_unlock(&dev->struct_mutex);
}

/* call from irq handler to schedule work to retire bo's */
void msm_gpu_retire(struct msm_gpu *gpu)
{
	struct msm_drm_private *priv = gpu->dev->dev_private;
	queue_work(priv->wq, &gpu->retire_work);
}

/* add bo's to gpu's ring, and kick gpu: */
int msm_gpu_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx)
{
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	int i, ret;

	submit->fence = ++priv->next_fence;

	gpu->submitted_fence = submit->fence;

	ret = gpu->funcs->submit(gpu, submit, ctx);
	priv->lastctx = ctx;

	for (i = 0; i < submit->nr_bos; i++) {
		struct msm_gem_object *msm_obj = submit->bos[i].obj;

		/* can't happen yet.. but when we add 2d support we'll have
		 * to deal w/ cross-ring synchronization:
		 */
		WARN_ON(is_active(msm_obj) && (msm_obj->gpu != gpu));

		if (!is_active(msm_obj)) {
			uint32_t iova;

			/* ring takes a reference to the bo and iova: */
			drm_gem_object_reference(&msm_obj->base);
			msm_gem_get_iova_locked(&msm_obj->base,
					submit->gpu->id, &iova);
		}

		if (submit->bos[i].flags & MSM_SUBMIT_BO_READ)
			msm_gem_move_to_active(&msm_obj->base, gpu, false, submit->fence);

		if (submit->bos[i].flags & MSM_SUBMIT_BO_WRITE)
			msm_gem_move_to_active(&msm_obj->base, gpu, true, submit->fence);
	}
	hangcheck_timer_reset(gpu);

	return ret;
}

/*
 * Init/Cleanup:
 */

static irqreturn_t irq_handler(int irq, void *data)
{
	struct msm_gpu *gpu = data;
	return gpu->funcs->irq(gpu);
}

static const char *clk_names[] = {
		"src_clk", "core_clk", "iface_clk", "mem_clk", "mem_iface_clk",
};

int msm_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct msm_gpu *gpu, const struct msm_gpu_funcs *funcs,
		const char *name, const char *ioname, const char *irqname, int ringsz)
{
	struct iommu_domain *iommu;
	int i, ret;

	gpu->dev = drm;
	gpu->funcs = funcs;
	gpu->name = name;

	INIT_LIST_HEAD(&gpu->active_list);
	INIT_WORK(&gpu->retire_work, retire_worker);
	INIT_WORK(&gpu->recover_work, recover_worker);

	setup_timer(&gpu->hangcheck_timer, hangcheck_handler,
			(unsigned long)gpu);

	BUG_ON(ARRAY_SIZE(clk_names) != ARRAY_SIZE(gpu->grp_clks));

	/* Map registers: */
	gpu->mmio = msm_ioremap(pdev, ioname, name);
	if (IS_ERR(gpu->mmio)) {
		ret = PTR_ERR(gpu->mmio);
		goto fail;
	}

	/* Get Interrupt: */
	gpu->irq = platform_get_irq_byname(pdev, irqname);
	if (gpu->irq < 0) {
		ret = gpu->irq;
		dev_err(drm->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, gpu->irq, irq_handler,
			IRQF_TRIGGER_HIGH, gpu->name, gpu);
	if (ret) {
		dev_err(drm->dev, "failed to request IRQ%u: %d\n", gpu->irq, ret);
		goto fail;
	}

	/* Acquire clocks: */
	for (i = 0; i < ARRAY_SIZE(clk_names); i++) {
		gpu->grp_clks[i] = devm_clk_get(&pdev->dev, clk_names[i]);
		DBG("grp_clks[%s]: %p", clk_names[i], gpu->grp_clks[i]);
		if (IS_ERR(gpu->grp_clks[i]))
			gpu->grp_clks[i] = NULL;
	}

	gpu->ebi1_clk = devm_clk_get(&pdev->dev, "bus_clk");
	DBG("ebi1_clk: %p", gpu->ebi1_clk);
	if (IS_ERR(gpu->ebi1_clk))
		gpu->ebi1_clk = NULL;

	/* Acquire regulators: */
	gpu->gpu_reg = devm_regulator_get(&pdev->dev, "vdd");
	DBG("gpu_reg: %p", gpu->gpu_reg);
	if (IS_ERR(gpu->gpu_reg))
		gpu->gpu_reg = NULL;

	gpu->gpu_cx = devm_regulator_get(&pdev->dev, "vddcx");
	DBG("gpu_cx: %p", gpu->gpu_cx);
	if (IS_ERR(gpu->gpu_cx))
		gpu->gpu_cx = NULL;

	/* Setup IOMMU.. eventually we will (I think) do this once per context
	 * and have separate page tables per context.  For now, to keep things
	 * simple and to get something working, just use a single address space:
	 */
	iommu = iommu_domain_alloc(&platform_bus_type);
	if (iommu) {
		dev_info(drm->dev, "%s: using IOMMU\n", name);
		gpu->mmu = msm_iommu_new(drm, iommu);
	} else {
		dev_info(drm->dev, "%s: no IOMMU, fallback to VRAM carveout!\n", name);
	}
	gpu->id = msm_register_mmu(drm, gpu->mmu);

	/* Create ringbuffer: */
	gpu->rb = msm_ringbuffer_new(gpu, ringsz);
	if (IS_ERR(gpu->rb)) {
		ret = PTR_ERR(gpu->rb);
		gpu->rb = NULL;
		dev_err(drm->dev, "could not create ringbuffer: %d\n", ret);
		goto fail;
	}

	ret = msm_gem_get_iova_locked(gpu->rb->bo, gpu->id, &gpu->rb_iova);
	if (ret) {
		gpu->rb_iova = 0;
		dev_err(drm->dev, "could not map ringbuffer: %d\n", ret);
		goto fail;
	}

	bs_init(gpu);

	return 0;

fail:
	return ret;
}

void msm_gpu_cleanup(struct msm_gpu *gpu)
{
	DBG("%s", gpu->name);

	WARN_ON(!list_empty(&gpu->active_list));

	bs_fini(gpu);

	if (gpu->rb) {
		if (gpu->rb_iova)
			msm_gem_put_iova(gpu->rb->bo, gpu->id);
		msm_ringbuffer_destroy(gpu->rb);
	}

	if (gpu->mmu)
		gpu->mmu->funcs->destroy(gpu->mmu);
}
