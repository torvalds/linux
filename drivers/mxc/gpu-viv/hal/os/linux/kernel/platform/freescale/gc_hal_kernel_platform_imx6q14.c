/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel_platform.h"
#include "gc_hal_kernel_device.h"
#include "gc_hal_driver.h"
#include <linux/slab.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#endif

#if USE_PLATFORM_DRIVER
#   include <linux/platform_device.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#include <mach/viv_gpu.h>
#else
#include <linux/pm_runtime.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
#include <mach/busfreq.h>
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 29)
#include <linux/busfreq-imx6.h>
#include <linux/reset.h>
#else
#include <linux/busfreq-imx.h>
#include <linux/reset.h>
#endif
#endif

#include <linux/clk.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#include <mach/hardware.h>
#endif
#include <linux/pm_runtime.h>

#include <linux/regulator/consumer.h>

#ifdef CONFIG_DEVICE_THERMAL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include <linux/device_cooling.h>
#define REG_THERMAL_NOTIFIER(a) register_devfreq_cooling_notifier(a);
#define UNREG_THERMAL_NOTIFIER(a) unregister_devfreq_cooling_notifier(a);
#else
extern int register_thermal_notifier(struct notifier_block *nb);
extern int unregister_thermal_notifier(struct notifier_block *nb);
#define REG_THERMAL_NOTIFIER(a) register_thermal_notifier(a);
#define UNREG_THERMAL_NOTIFIER(a) unregister_thermal_notifier(a);
#endif
#endif

#ifndef gcdFSL_CONTIGUOUS_SIZE
#define gcdFSL_CONTIGUOUS_SIZE (4 << 20)
#endif

static int initgpu3DMinClock = 1;
module_param(initgpu3DMinClock, int, 0644);

struct platform_device *pdevice;

#ifdef CONFIG_GPU_LOW_MEMORY_KILLER
#    include <linux/kernel.h>
#    include <linux/mm.h>
#    include <linux/oom.h>
#    include <linux/sched.h>
#    include <linux/profile.h>

struct task_struct *lowmem_deathpending;

static int
task_notify_func(struct notifier_block *self, unsigned long val, void *data);

static struct notifier_block task_nb = {
	.notifier_call	= task_notify_func,
};

static int
task_notify_func(struct notifier_block *self, unsigned long val, void *data)
{
	struct task_struct *task = data;

	if (task == lowmem_deathpending)
		lowmem_deathpending = NULL;

	return NOTIFY_DONE;
}

extern struct task_struct *lowmem_deathpending;
static unsigned long lowmem_deathpending_timeout;

static int force_contiguous_lowmem_shrink(IN gckKERNEL Kernel)
{
	struct task_struct *p;
	struct task_struct *selected = NULL;
	int tasksize;
        int ret = -1;
	int min_adj = 0;
	int selected_tasksize = 0;
	int selected_oom_adj;
	/*
	 * If we already have a death outstanding, then
	 * bail out right away; indicating to vmscan
	 * that we have nothing further to offer on
	 * this pass.
	 *
	 */
	if (lowmem_deathpending &&
	    time_before_eq(jiffies, lowmem_deathpending_timeout))
		return 0;
	selected_oom_adj = min_adj;

       rcu_read_lock();
	for_each_process(p) {
		struct mm_struct *mm;
		struct signal_struct *sig;
                gcuDATABASE_INFO info;
		int oom_adj;

		task_lock(p);
		mm = p->mm;
		sig = p->signal;
		if (!mm || !sig) {
			task_unlock(p);
			continue;
		}
		oom_adj = sig->oom_score_adj;
		if (oom_adj < min_adj) {
			task_unlock(p);
			continue;
		}

		tasksize = 0;
		task_unlock(p);
               rcu_read_unlock();

		if (gckKERNEL_QueryProcessDB(Kernel, p->pid, gcvFALSE, gcvDB_VIDEO_MEMORY, &info) == gcvSTATUS_OK){
			tasksize += info.counters.bytes / PAGE_SIZE;
		}
		if (gckKERNEL_QueryProcessDB(Kernel, p->pid, gcvFALSE, gcvDB_CONTIGUOUS, &info) == gcvSTATUS_OK){
			tasksize += info.counters.bytes / PAGE_SIZE;
		}

               rcu_read_lock();

		if (tasksize <= 0)
			continue;

		gckOS_Print("<gpu> pid %d (%s), adj %d, size %d \n", p->pid, p->comm, oom_adj, tasksize);

		if (selected) {
			if (oom_adj < selected_oom_adj)
				continue;
			if (oom_adj == selected_oom_adj &&
			    tasksize <= selected_tasksize)
				continue;
		}
		selected = p;
		selected_tasksize = tasksize;
		selected_oom_adj = oom_adj;
	}
    if (selected && selected_oom_adj > 0) {
		gckOS_Print("<gpu> send sigkill to %d (%s), adj %d, size %d\n",
			     selected->pid, selected->comm,
			     selected_oom_adj, selected_tasksize);
		lowmem_deathpending = selected;
		lowmem_deathpending_timeout = jiffies + HZ;
		force_sig(SIGKILL, selected);
		ret = 0;
	}
       rcu_read_unlock();
	return ret;
}

extern gckKERNEL
_GetValidKernel(
  gckGALDEVICE Device
  );

gceSTATUS
_ShrinkMemory(
    IN gckPLATFORM Platform
    )
{
    struct platform_device *pdev;
    gckGALDEVICE galDevice;
    gckKERNEL kernel;
    gceSTATUS status = gcvSTATUS_OK;

    pdev = Platform->device;

    galDevice = platform_get_drvdata(pdev);

    kernel = _GetValidKernel(galDevice);

    if (kernel != gcvNULL)
    {
        if (force_contiguous_lowmem_shrink(kernel) != 0)
            status = gcvSTATUS_OUT_OF_MEMORY;
    }
    else
    {
        gcmkPRINT("%s(%d) can't find kernel! ", __FUNCTION__, __LINE__);
    }

    return status;
}
#endif

#if gcdENABLE_FSCALE_VAL_ADJUST && defined(CONFIG_DEVICE_THERMAL)
static int thermal_hot_pm_notify(struct notifier_block *nb, unsigned long event,
       void *dummy)
{
    static gctUINT orgFscale, minFscale, maxFscale;
    static gctBOOL bAlreadyTooHot = gcvFALSE;
    gckHARDWARE hardware;
    gckGALDEVICE galDevice;

    galDevice = platform_get_drvdata(pdevice);
    if (!galDevice)
    {
        /* GPU is not ready, so it is meaningless to change GPU freq. */
        return NOTIFY_OK;
    }

    if (!galDevice->kernels[gcvCORE_MAJOR])
    {
        return NOTIFY_OK;
    }

    hardware = galDevice->kernels[gcvCORE_MAJOR]->hardware;

    if (!hardware)
    {
        return NOTIFY_OK;
    }

    if (event && !bAlreadyTooHot) {
        gckHARDWARE_GetFscaleValue(hardware,&orgFscale,&minFscale, &maxFscale);
        gckHARDWARE_SetFscaleValue(hardware, minFscale);
        bAlreadyTooHot = gcvTRUE;
        gckOS_Print("System is too hot. GPU3D will work at %d/64 clock.\n", minFscale);
    } else if (!event && bAlreadyTooHot) {
        gckHARDWARE_SetFscaleValue(hardware, orgFscale);
        gckOS_Print("Hot alarm is canceled. GPU3D clock will return to %d/64\n", orgFscale);
        bAlreadyTooHot = gcvFALSE;
    }
    return NOTIFY_OK;
}

static struct notifier_block thermal_hot_pm_notifier = {
    .notifier_call = thermal_hot_pm_notify,
    };

static ssize_t show_gpu3DMinClock(struct device_driver *dev, char *buf)
{
    gctUINT currentf,minf,maxf;
    gckGALDEVICE galDevice;

    galDevice = platform_get_drvdata(pdevice);
    if(galDevice->kernels[gcvCORE_MAJOR])
    {
         gckHARDWARE_GetFscaleValue(galDevice->kernels[gcvCORE_MAJOR]->hardware,
            &currentf, &minf, &maxf);
    }
    snprintf(buf, PAGE_SIZE, "%d\n", minf);
    return strlen(buf);
}

static ssize_t update_gpu3DMinClock(struct device_driver *dev, const char *buf, size_t count)
{

    gctINT fields;
    gctUINT MinFscaleValue;
    gckGALDEVICE galDevice;

    galDevice = platform_get_drvdata(pdevice);
    if(galDevice->kernels[gcvCORE_MAJOR])
    {
         fields = sscanf(buf, "%d", &MinFscaleValue);
         if (fields < 1)
             return -EINVAL;

         gckHARDWARE_SetMinFscaleValue(galDevice->kernels[gcvCORE_MAJOR]->hardware,MinFscaleValue);
    }

    return count;
}

static DRIVER_ATTR(gpu3DMinClock, S_IRUGO | S_IWUSR, show_gpu3DMinClock, update_gpu3DMinClock);
#endif




#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
static const struct of_device_id mxs_gpu_dt_ids[] = {
    { .compatible = "fsl,imx6q-gpu", },
    {/* sentinel */}
};
MODULE_DEVICE_TABLE(of, mxs_gpu_dt_ids);
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
struct contiguous_mem_pool {
    struct dma_attrs attrs;
    dma_addr_t phys;
    void *virt;
    size_t size;
};
#endif

struct imx_priv {
    /* Clock management.*/
    struct clk         *clk_3d_core;
    struct clk         *clk_3d_shader;
    struct clk         *clk_3d_axi;
    struct clk         *clk_2d_core;
    struct clk         *clk_2d_axi;
    struct clk         *clk_vg_axi;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    /*Power management.*/
    struct regulator      *gpu_regulator;
#endif
#endif
       /*Run time pm*/
       struct device           *pmdev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    struct contiguous_mem_pool *pool;
    struct reset_control *rstc[gcdMAX_GPU_COUNT];
#endif
};

static struct imx_priv imxPriv;

gceSTATUS
gckPLATFORM_AdjustParam(
    IN gckPLATFORM Platform,
    OUT gcsMODULE_PARAMETERS *Args
    )
{
     struct resource* res;
     struct platform_device* pdev = Platform->device;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
       struct device_node *dn =pdev->dev.of_node;
       const u32 *prop;
#else
       struct viv_gpu_platform_data *pdata;
#endif

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phys_baseaddr");
    if (res)
        Args->baseAddress = res->start;

    res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "irq_3d");
    if (res)
        Args->irqLine = res->start;

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iobase_3d");
    if (res)
    {
        Args->registerMemBase = res->start;
        Args->registerMemSize = res->end - res->start + 1;
    }

    res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "irq_2d");
    if (res)
        Args->irqLine2D = res->start;

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iobase_2d");
    if (res)
    {
        Args->registerMemBase2D = res->start;
        Args->registerMemSize2D = res->end - res->start + 1;
    }

    res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "irq_vg");
    if (res)
        Args->irqLineVG = res->start;

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iobase_vg");
    if (res)
    {
        Args->registerMemBaseVG = res->start;
        Args->registerMemSizeVG = res->end - res->start + 1;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "contiguous_mem");
    if (res)
    {
        if( Args->contiguousBase == 0 )
           Args->contiguousBase = res->start;
        if( Args->contiguousSize == ~0U )
           Args->contiguousSize = res->end - res->start + 1;
    }
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
       Args->contiguousBase = 0;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
       prop = of_get_property(dn, "contiguousbase", NULL);
       if(prop)
               Args->contiguousBase = *prop;
       of_property_read_u32(dn,"contiguoussize", (u32 *)&contiguousSize);
#else
    pdata = pdev->dev.platform_data;
    if (pdata) {
        Args->contiguousBase = pdata->reserved_mem_base;
       Args->contiguousSize = pdata->reserved_mem_size;
     }
#endif
    if (Args->contiguousSize == ~0U)
    {
       gckOS_Print("Warning: No contiguous memory is reserverd for gpu.!\n ");
       gckOS_Print("Warning: Will use default value(%d) for the reserved memory!\n ",gcdFSL_CONTIGUOUS_SIZE);
       Args->contiguousSize = gcdFSL_CONTIGUOUS_SIZE;
    }

    Args->gpu3DMinClock = initgpu3DMinClock;

    if(Args->physSize == 0)
    {
        Args->physSize = 0x80000000;
    }

    return gcvSTATUS_OK;
}

gceSTATUS
_AllocPriv(
    IN gckPLATFORM Platform
    )
{
    Platform->priv = &imxPriv;

#ifdef CONFIG_GPU_LOW_MEMORY_KILLER
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
     task_free_register(&task_nb);
#else
    task_handoff_register(&task_nb);
#endif
#endif

    return gcvSTATUS_OK;
}

gceSTATUS
_FreePriv(
    IN gckPLATFORM Platform
    )
{
#ifdef CONFIG_GPU_LOW_MEMORY_KILLER
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
     task_free_unregister(&task_nb);
#else
    task_handoff_unregister(&task_nb);
#endif
#endif

    return gcvSTATUS_OK;
}

gceSTATUS
_SetClock(
    IN gckPLATFORM Platform,
    IN gceCORE GPU,
    IN gctBOOL Enable
    );

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static void imx6sx_optimize_qosc_for_GPU(IN gckPLATFORM Platform)
{
	struct device_node *np;
	void __iomem *src_base;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6sx-qosc");
	if (!np)
		return;

	src_base = of_iomap(np, 0);
	WARN_ON(!src_base);
    	_SetClock(Platform, gcvCORE_MAJOR, gcvTRUE);
	writel_relaxed(0, src_base); /* Disable clkgate & soft_rst */
	writel_relaxed(0, src_base+0x60); /* Enable all masters */
	writel_relaxed(0, src_base+0x1400); /* Disable clkgate & soft_rst for gpu */
	writel_relaxed(0x0f000222, src_base+0x1400+0xd0); /* Set Write QoS 2 for gpu */
	writel_relaxed(0x0f000822, src_base+0x1400+0xe0); /* Set Read QoS 8 for gpu */
    	_SetClock(Platform, gcvCORE_MAJOR, gcvFALSE);
	return;
}
#endif

gceSTATUS
_GetPower(
    IN gckPLATFORM Platform
    )
{
    struct device* pdev = &Platform->device->dev;
    struct imx_priv *priv = Platform->priv;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    struct reset_control *rstc;
#endif

#ifdef CONFIG_PM
    /*Init runtime pm for gpu*/
    pm_runtime_enable(pdev);
    priv->pmdev = pdev;
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    rstc = devm_reset_control_get(pdev, "gpu3d");
    priv->rstc[gcvCORE_MAJOR] = IS_ERR(rstc) ? NULL : rstc;
    rstc = devm_reset_control_get(pdev, "gpu2d");
    priv->rstc[gcvCORE_2D] = IS_ERR(rstc) ? NULL : rstc;
    rstc = devm_reset_control_get(pdev, "gpuvg");
    priv->rstc[gcvCORE_VG] = IS_ERR(rstc) ? NULL : rstc;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
    /*get gpu regulator*/
    priv->gpu_regulator = regulator_get(pdev, "cpu_vddgpu");
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    priv->gpu_regulator = devm_regulator_get(pdev, "pu");
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    if (IS_ERR(priv->gpu_regulator)) {
       gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
               "%s(%d): Failed to get gpu regulator \n",
               __FUNCTION__, __LINE__);
       return gcvSTATUS_NOT_FOUND;
    }
#endif
#endif

    /*Initialize the clock structure*/
    priv->clk_3d_core = clk_get(pdev, "gpu3d_clk");
    if (!IS_ERR(priv->clk_3d_core)) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
        if (cpu_is_mx6q()) {
               priv->clk_3d_shader = clk_get(pdev, "gpu3d_shader_clk");
               if (IS_ERR(priv->clk_3d_shader)) {
                   clk_put(priv->clk_3d_core);
                   priv->clk_3d_core = NULL;
                   priv->clk_3d_shader = NULL;
                   gckOS_Print("galcore: clk_get gpu3d_shader_clk failed, disable 3d!\n");
               }
             }
#else
               priv->clk_3d_axi = clk_get(pdev, "gpu3d_axi_clk");
               priv->clk_3d_shader = clk_get(pdev, "gpu3d_shader_clk");
               if (IS_ERR(priv->clk_3d_shader)) {
                   clk_put(priv->clk_3d_core);
                   priv->clk_3d_core = NULL;
                   priv->clk_3d_shader = NULL;
                   gckOS_Print("galcore: clk_get gpu3d_shader_clk failed, disable 3d!\n");
               }
#endif
    } else {
        priv->clk_3d_core = NULL;
        gckOS_Print("galcore: clk_get gpu3d_clk failed, disable 3d!\n");
    }

    priv->clk_2d_core = clk_get(pdev, "gpu2d_clk");
    if (IS_ERR(priv->clk_2d_core)) {
        priv->clk_2d_core = NULL;
        gckOS_Print("galcore: clk_get 2d core clock failed, disable 2d/vg!\n");
    } else {
        priv->clk_2d_axi = clk_get(pdev, "gpu2d_axi_clk");
        if (IS_ERR(priv->clk_2d_axi)) {
            priv->clk_2d_axi = NULL;
            gckOS_Print("galcore: clk_get 2d axi clock failed, disable 2d\n");
        }

        priv->clk_vg_axi = clk_get(pdev, "openvg_axi_clk");
        if (IS_ERR(priv->clk_vg_axi)) {
               priv->clk_vg_axi = NULL;
               gckOS_Print("galcore: clk_get vg clock failed, disable vg!\n");
        }
    }


#if gcdENABLE_FSCALE_VAL_ADJUST
    pdevice = Platform->device;
    REG_THERMAL_NOTIFIER(&thermal_hot_pm_notifier);
    {
        int ret = 0;
        ret = driver_create_file(pdevice->dev.driver, &driver_attr_gpu3DMinClock);
        if(ret)
            dev_err(&pdevice->dev, "create gpu3DMinClock attr failed (%d)\n", ret);
    }
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    imx6sx_optimize_qosc_for_GPU(Platform);
#endif
    return gcvSTATUS_OK;
}

gceSTATUS
_PutPower(
    IN gckPLATFORM Platform
    )
{
    struct imx_priv *priv = Platform->priv;

    /*Disable clock*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    if (priv->clk_3d_axi) {
       clk_put(priv->clk_3d_axi);
       priv->clk_3d_axi = NULL;
    }
#endif
    if (priv->clk_3d_core) {
       clk_put(priv->clk_3d_core);
       priv->clk_3d_core = NULL;
    }
    if (priv->clk_3d_shader) {
       clk_put(priv->clk_3d_shader);
       priv->clk_3d_shader = NULL;
    }
    if (priv->clk_2d_core) {
       clk_put(priv->clk_2d_core);
       priv->clk_2d_core = NULL;
    }
    if (priv->clk_2d_axi) {
       clk_put(priv->clk_2d_axi);
       priv->clk_2d_axi = NULL;
    }
    if (priv->clk_vg_axi) {
       clk_put(priv->clk_vg_axi);
       priv->clk_vg_axi = NULL;
    }

#ifdef CONFIG_PM
    if(priv->pmdev)
        pm_runtime_disable(priv->pmdev);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
    if (priv->gpu_regulator) {
       regulator_put(priv->gpu_regulator);
       priv->gpu_regulator = NULL;
    }
#endif

#if gcdENABLE_FSCALE_VAL_ADJUST
    UNREG_THERMAL_NOTIFIER(&thermal_hot_pm_notifier);

    driver_remove_file(pdevice->dev.driver, &driver_attr_gpu3DMinClock);
#endif

    return gcvSTATUS_OK;
}

gceSTATUS
_SetPower(
    IN gckPLATFORM Platform,
    IN gceCORE GPU,
    IN gctBOOL Enable
    )
{
    struct imx_priv* priv = Platform->priv;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    int ret;
#endif
#endif

    if (Enable)
    {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
        if(!IS_ERR(priv->gpu_regulator)) {
            ret = regulator_enable(priv->gpu_regulator);
            if (ret != 0)
                gckOS_Print("%s(%d): fail to enable pu regulator %d!\n",
                    __FUNCTION__, __LINE__, ret);
        }
#else
        imx_gpc_power_up_pu(true);
#endif
#endif

#ifdef CONFIG_PM
		pm_runtime_get_sync(priv->pmdev);
#endif
	}
    else
    {
#ifdef CONFIG_PM
        pm_runtime_put_sync(priv->pmdev);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
        if(!IS_ERR(priv->gpu_regulator))
            regulator_disable(priv->gpu_regulator);
#else
        imx_gpc_power_up_pu(false);
#endif
#endif

    }

    return gcvSTATUS_OK;
}

gceSTATUS
_SetClock(
    IN gckPLATFORM Platform,
    IN gceCORE GPU,
    IN gctBOOL Enable
    )
{
    struct imx_priv* priv = Platform->priv;
    struct clk *clk_3dcore = priv->clk_3d_core;
    struct clk *clk_3dshader = priv->clk_3d_shader;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    struct clk *clk_3d_axi = priv->clk_3d_axi;
#endif
    struct clk *clk_2dcore = priv->clk_2d_core;
    struct clk *clk_2d_axi = priv->clk_2d_axi;
    struct clk *clk_vg_axi = priv->clk_vg_axi;


#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
    if (Enable) {
        switch (GPU) {
        case gcvCORE_MAJOR:
            clk_enable(clk_3dcore);
            if (cpu_is_mx6q())
                clk_enable(clk_3dshader);
            break;
        case gcvCORE_2D:
            clk_enable(clk_2dcore);
            clk_enable(clk_2d_axi);
            break;
        case gcvCORE_VG:
            clk_enable(clk_2dcore);
            clk_enable(clk_vg_axi);
            break;
        default:
            break;
        }
    } else {
        switch (GPU) {
        case gcvCORE_MAJOR:
            if (cpu_is_mx6q())
                clk_disable(clk_3dshader);
            clk_disable(clk_3dcore);
            break;
       case gcvCORE_2D:
            clk_disable(clk_2dcore);
            clk_disable(clk_2d_axi);
            break;
        case gcvCORE_VG:
            clk_disable(clk_2dcore);
            clk_disable(clk_vg_axi);
            break;
        default:
            break;
        }
    }
#else
    if (Enable) {
        switch (GPU) {
        case gcvCORE_MAJOR:
            clk_prepare(clk_3dcore);
            clk_enable(clk_3dcore);
            clk_prepare(clk_3dshader);
            clk_enable(clk_3dshader);
            clk_prepare(clk_3d_axi);
            clk_enable(clk_3d_axi);
            break;
        case gcvCORE_2D:
            clk_prepare(clk_2dcore);
            clk_enable(clk_2dcore);
            clk_prepare(clk_2d_axi);
            clk_enable(clk_2d_axi);
            break;
        case gcvCORE_VG:
            clk_prepare(clk_2dcore);
            clk_enable(clk_2dcore);
            clk_prepare(clk_vg_axi);
            clk_enable(clk_vg_axi);
            break;
        default:
            break;
        }
    } else {
        switch (GPU) {
        case gcvCORE_MAJOR:
            clk_disable(clk_3dshader);
            clk_unprepare(clk_3dshader);
            clk_disable(clk_3dcore);
            clk_unprepare(clk_3dcore);
            clk_disable(clk_3d_axi);
            clk_unprepare(clk_3d_axi);
            break;
       case gcvCORE_2D:
            clk_disable(clk_2dcore);
            clk_unprepare(clk_2dcore);
            clk_disable(clk_2d_axi);
            clk_unprepare(clk_2d_axi);
            break;
        case gcvCORE_VG:
            clk_disable(clk_2dcore);
            clk_unprepare(clk_2dcore);
            clk_disable(clk_vg_axi);
            clk_unprepare(clk_vg_axi);
            break;
        default:
            break;
        }
    }
#endif

    return gcvSTATUS_OK;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
#ifdef CONFIG_PM
static int gpu_runtime_suspend(struct device *dev)
{
    release_bus_freq(BUS_FREQ_HIGH);
    return 0;
}

static int gpu_runtime_resume(struct device *dev)
{
    request_bus_freq(BUS_FREQ_HIGH);
    return 0;
}

static struct dev_pm_ops gpu_pm_ops;
#endif
#endif

gceSTATUS
_AdjustDriver(
    IN gckPLATFORM Platform
    )
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    struct platform_driver * driver = Platform->driver;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    driver->driver.of_match_table = mxs_gpu_dt_ids;
#endif

    /* Override PM callbacks to add runtime PM callbacks. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    /* Fill local structure with original value. */
    memcpy(&gpu_pm_ops, driver->driver.pm, sizeof(struct dev_pm_ops));

    /* Add runtime PM callback. */
#ifdef CONFIG_PM
    gpu_pm_ops.runtime_suspend = gpu_runtime_suspend;
    gpu_pm_ops.runtime_resume = gpu_runtime_resume;
    gpu_pm_ops.runtime_idle = NULL;
#endif

    /* Replace callbacks. */
    driver->driver.pm = &gpu_pm_ops;
#endif
    return gcvSTATUS_OK;
}

gceSTATUS
_Reset(
    IN gckPLATFORM Platform,
    gceCORE GPU
    )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#define SRC_SCR_OFFSET 0
#define BP_SRC_SCR_GPU3D_RST 1
#define BP_SRC_SCR_GPU2D_RST 4
    void __iomem *src_base = IO_ADDRESS(SRC_BASE_ADDR);
    gctUINT32 bit_offset,val;

    if(GPU == gcvCORE_MAJOR) {
        bit_offset = BP_SRC_SCR_GPU3D_RST;
    } else if((GPU == gcvCORE_VG)
            ||(GPU == gcvCORE_2D)) {
        bit_offset = BP_SRC_SCR_GPU2D_RST;
    } else {
        return gcvSTATUS_INVALID_CONFIG;
    }
    val = __raw_readl(src_base + SRC_SCR_OFFSET);
    val &= ~(1 << (bit_offset));
    val |= (1 << (bit_offset));
    __raw_writel(val, src_base + SRC_SCR_OFFSET);

    while ((__raw_readl(src_base + SRC_SCR_OFFSET) &
                (1 << (bit_offset))) != 0) {
    }

    return gcvSTATUS_NOT_SUPPORTED;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    struct imx_priv* priv = Platform->priv;
    struct reset_control *rstc = priv->rstc[GPU];
    if (rstc)
        reset_control_reset(rstc);
#else
    imx_src_reset_gpu((int)GPU);
#endif
    return gcvSTATUS_OK;
}

gcmkPLATFROM_Name

gcsPLATFORM_OPERATIONS platformOperations = {
    .adjustParam  = gckPLATFORM_AdjustParam,
    .allocPriv    = _AllocPriv,
    .freePriv     = _FreePriv,
    .getPower     = _GetPower,
    .putPower     = _PutPower,
    .setPower     = _SetPower,
    .setClock     = _SetClock,
    .adjustDriver = _AdjustDriver,
    .reset        = _Reset,
#ifdef CONFIG_GPU_LOW_MEMORY_KILLER
    .shrinkMemory = _ShrinkMemory,
#endif
    .name          = _Name,
};

void
gckPLATFORM_QueryOperations(
    IN gcsPLATFORM_OPERATIONS ** Operations
    )
{
     *Operations = &platformOperations;
}

