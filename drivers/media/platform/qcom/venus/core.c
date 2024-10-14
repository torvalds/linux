// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/devcoredump.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>

#include "core.h"
#include "firmware.h"
#include "pm_helpers.h"
#include "hfi_venus_io.h"

static void venus_coredump(struct venus_core *core)
{
	struct device *dev;
	phys_addr_t mem_phys;
	size_t mem_size;
	void *mem_va;
	void *data;

	dev = core->dev;
	mem_phys = core->fw.mem_phys;
	mem_size = core->fw.mem_size;

	mem_va = memremap(mem_phys, mem_size, MEMREMAP_WC);
	if (!mem_va)
		return;

	data = vmalloc(mem_size);
	if (!data) {
		memunmap(mem_va);
		return;
	}

	memcpy(data, mem_va, mem_size);
	memunmap(mem_va);
	dev_coredumpv(dev, data, mem_size, GFP_KERNEL);
}

static void venus_event_notify(struct venus_core *core, u32 event)
{
	struct venus_inst *inst;

	switch (event) {
	case EVT_SYS_WATCHDOG_TIMEOUT:
	case EVT_SYS_ERROR:
		break;
	default:
		return;
	}

	mutex_lock(&core->lock);
	set_bit(0, &core->sys_error);
	set_bit(0, &core->dump_core);
	list_for_each_entry(inst, &core->instances, list)
		inst->ops->event_notify(inst, EVT_SESSION_ERROR, NULL);
	mutex_unlock(&core->lock);

	disable_irq_nosync(core->irq);
	schedule_delayed_work(&core->work, msecs_to_jiffies(10));
}

static const struct hfi_core_ops venus_core_ops = {
	.event_notify = venus_event_notify,
};

#define RPM_WAIT_FOR_IDLE_MAX_ATTEMPTS 10

static void venus_sys_error_handler(struct work_struct *work)
{
	struct venus_core *core =
			container_of(work, struct venus_core, work.work);
	int ret, i, max_attempts = RPM_WAIT_FOR_IDLE_MAX_ATTEMPTS;
	const char *err_msg = "";
	bool failed = false;

	ret = pm_runtime_get_sync(core->dev);
	if (ret < 0) {
		err_msg = "resume runtime PM";
		max_attempts = 0;
		failed = true;
	}

	core->ops->core_deinit(core);
	core->state = CORE_UNINIT;

	for (i = 0; i < max_attempts; i++) {
		if (!pm_runtime_active(core->dev_dec) && !pm_runtime_active(core->dev_enc))
			break;
		msleep(10);
	}

	mutex_lock(&core->lock);

	venus_shutdown(core);

	if (test_bit(0, &core->dump_core)) {
		venus_coredump(core);
		clear_bit(0, &core->dump_core);
	}

	pm_runtime_put_sync(core->dev);

	for (i = 0; i < max_attempts; i++) {
		if (!core->pmdomains ||
		    !pm_runtime_active(core->pmdomains->pd_devs[0]))
			break;
		usleep_range(1000, 1500);
	}

	hfi_reinit(core);

	ret = pm_runtime_get_sync(core->dev);
	if (ret < 0) {
		err_msg = "resume runtime PM";
		failed = true;
	}

	ret = venus_boot(core);
	if (ret && !failed) {
		err_msg = "boot Venus";
		failed = true;
	}

	ret = hfi_core_resume(core, true);
	if (ret && !failed) {
		err_msg = "resume HFI";
		failed = true;
	}

	enable_irq(core->irq);

	mutex_unlock(&core->lock);

	ret = hfi_core_init(core);
	if (ret && !failed) {
		err_msg = "init HFI";
		failed = true;
	}

	pm_runtime_put_sync(core->dev);

	if (failed) {
		disable_irq_nosync(core->irq);
		dev_warn_ratelimited(core->dev,
				     "System error has occurred, recovery failed to %s\n",
				     err_msg);
		schedule_delayed_work(&core->work, msecs_to_jiffies(10));
		return;
	}

	dev_warn(core->dev, "system error has occurred (recovered)\n");

	mutex_lock(&core->lock);
	clear_bit(0, &core->sys_error);
	wake_up_all(&core->sys_err_done);
	mutex_unlock(&core->lock);
}

static u32 to_v4l2_codec_type(u32 codec)
{
	switch (codec) {
	case HFI_VIDEO_CODEC_H264:
		return V4L2_PIX_FMT_H264;
	case HFI_VIDEO_CODEC_H263:
		return V4L2_PIX_FMT_H263;
	case HFI_VIDEO_CODEC_MPEG1:
		return V4L2_PIX_FMT_MPEG1;
	case HFI_VIDEO_CODEC_MPEG2:
		return V4L2_PIX_FMT_MPEG2;
	case HFI_VIDEO_CODEC_MPEG4:
		return V4L2_PIX_FMT_MPEG4;
	case HFI_VIDEO_CODEC_VC1:
		return V4L2_PIX_FMT_VC1_ANNEX_G;
	case HFI_VIDEO_CODEC_VP8:
		return V4L2_PIX_FMT_VP8;
	case HFI_VIDEO_CODEC_VP9:
		return V4L2_PIX_FMT_VP9;
	case HFI_VIDEO_CODEC_DIVX:
	case HFI_VIDEO_CODEC_DIVX_311:
		return V4L2_PIX_FMT_XVID;
	default:
		return 0;
	}
}

static int venus_enumerate_codecs(struct venus_core *core, u32 type)
{
	const struct hfi_inst_ops dummy_ops = {};
	struct venus_inst *inst;
	u32 codec, codecs;
	unsigned int i;
	int ret;

	if (core->res->hfi_version != HFI_VERSION_1XX)
		return 0;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	mutex_init(&inst->lock);
	inst->core = core;
	inst->session_type = type;
	if (type == VIDC_SESSION_TYPE_DEC)
		codecs = core->dec_codecs;
	else
		codecs = core->enc_codecs;

	ret = hfi_session_create(inst, &dummy_ops);
	if (ret)
		goto err;

	for (i = 0; i < MAX_CODEC_NUM; i++) {
		codec = (1UL << i) & codecs;
		if (!codec)
			continue;

		ret = hfi_session_init(inst, to_v4l2_codec_type(codec));
		if (ret)
			goto done;

		ret = hfi_session_deinit(inst);
		if (ret)
			goto done;
	}

done:
	hfi_session_destroy(inst);
err:
	mutex_destroy(&inst->lock);
	kfree(inst);

	return ret;
}

static void venus_assign_register_offsets(struct venus_core *core)
{
	if (IS_IRIS2(core) || IS_IRIS2_1(core)) {
		core->vbif_base = core->base + VBIF_BASE;
		core->cpu_base = core->base + CPU_BASE_V6;
		core->cpu_cs_base = core->base + CPU_CS_BASE_V6;
		core->cpu_ic_base = core->base + CPU_IC_BASE_V6;
		core->wrapper_base = core->base + WRAPPER_BASE_V6;
		core->wrapper_tz_base = core->base + WRAPPER_TZ_BASE_V6;
		core->aon_base = core->base + AON_BASE_V6;
	} else {
		core->vbif_base = core->base + VBIF_BASE;
		core->cpu_base = core->base + CPU_BASE;
		core->cpu_cs_base = core->base + CPU_CS_BASE;
		core->cpu_ic_base = core->base + CPU_IC_BASE;
		core->wrapper_base = core->base + WRAPPER_BASE;
		core->wrapper_tz_base = NULL;
		core->aon_base = NULL;
	}
}

static irqreturn_t venus_isr_thread(int irq, void *dev_id)
{
	struct venus_core *core = dev_id;
	irqreturn_t ret;

	ret = hfi_isr_thread(irq, dev_id);

	if (ret == IRQ_HANDLED && venus_fault_inject_ssr())
		hfi_core_trigger_ssr(core, HFI_TEST_SSR_SW_ERR_FATAL);

	return ret;
}

static int venus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct venus_core *core;
	int ret;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->dev = dev;

	core->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->base))
		return PTR_ERR(core->base);

	core->video_path = devm_of_icc_get(dev, "video-mem");
	if (IS_ERR(core->video_path))
		return PTR_ERR(core->video_path);

	core->cpucfg_path = devm_of_icc_get(dev, "cpu-cfg");
	if (IS_ERR(core->cpucfg_path))
		return PTR_ERR(core->cpucfg_path);

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0)
		return core->irq;

	core->res = of_device_get_match_data(dev);
	if (!core->res)
		return -ENODEV;

	mutex_init(&core->pm_lock);

	core->pm_ops = venus_pm_get(core->res->hfi_version);
	if (!core->pm_ops)
		return -ENODEV;

	if (core->pm_ops->core_get) {
		ret = core->pm_ops->core_get(core);
		if (ret)
			return ret;
	}

	ret = dma_set_mask_and_coherent(dev, core->res->dma_mask);
	if (ret)
		goto err_core_put;

	dma_set_max_seg_size(dev, UINT_MAX);

	INIT_LIST_HEAD(&core->instances);
	mutex_init(&core->lock);
	INIT_DELAYED_WORK(&core->work, venus_sys_error_handler);
	init_waitqueue_head(&core->sys_err_done);

	ret = devm_request_threaded_irq(dev, core->irq, hfi_isr, venus_isr_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"venus", core);
	if (ret)
		goto err_core_put;

	ret = hfi_create(core, &venus_core_ops);
	if (ret)
		goto err_core_put;

	venus_assign_register_offsets(core);

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret)
		goto err_core_deinit;

	platform_set_drvdata(pdev, core);

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_runtime_disable;

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret)
		goto err_runtime_disable;

	ret = venus_firmware_init(core);
	if (ret)
		goto err_of_depopulate;

	ret = venus_boot(core);
	if (ret)
		goto err_firmware_deinit;

	ret = hfi_core_resume(core, true);
	if (ret)
		goto err_venus_shutdown;

	ret = hfi_core_init(core);
	if (ret)
		goto err_venus_shutdown;

	ret = venus_enumerate_codecs(core, VIDC_SESSION_TYPE_DEC);
	if (ret)
		goto err_venus_shutdown;

	ret = venus_enumerate_codecs(core, VIDC_SESSION_TYPE_ENC);
	if (ret)
		goto err_venus_shutdown;

	ret = pm_runtime_put_sync(dev);
	if (ret) {
		pm_runtime_get_noresume(dev);
		goto err_dev_unregister;
	}

	venus_dbgfs_init(core);

	return 0;

err_dev_unregister:
	v4l2_device_unregister(&core->v4l2_dev);
err_venus_shutdown:
	venus_shutdown(core);
err_firmware_deinit:
	venus_firmware_deinit(core);
err_of_depopulate:
	of_platform_depopulate(dev);
err_runtime_disable:
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_disable(dev);
	hfi_destroy(core);
err_core_deinit:
	hfi_core_deinit(core, false);
err_core_put:
	if (core->pm_ops->core_put)
		core->pm_ops->core_put(core);
	return ret;
}

static void venus_remove(struct platform_device *pdev)
{
	struct venus_core *core = platform_get_drvdata(pdev);
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	struct device *dev = core->dev;
	int ret;

	cancel_delayed_work_sync(&core->work);
	ret = pm_runtime_get_sync(dev);
	WARN_ON(ret < 0);

	ret = hfi_core_deinit(core, true);
	WARN_ON(ret);

	venus_shutdown(core);
	of_platform_depopulate(dev);

	venus_firmware_deinit(core);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	if (pm_ops->core_put)
		pm_ops->core_put(core);

	v4l2_device_unregister(&core->v4l2_dev);

	hfi_destroy(core);

	mutex_destroy(&core->pm_lock);
	mutex_destroy(&core->lock);
	venus_dbgfs_deinit(core);
}

static void venus_core_shutdown(struct platform_device *pdev)
{
	struct venus_core *core = platform_get_drvdata(pdev);

	pm_runtime_get_sync(core->dev);
	venus_shutdown(core);
	venus_firmware_deinit(core);
	pm_runtime_put_sync(core->dev);
}

static __maybe_unused int venus_runtime_suspend(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret;

	ret = hfi_core_suspend(core);
	if (ret)
		return ret;

	if (pm_ops->core_power) {
		ret = pm_ops->core_power(core, POWER_OFF);
		if (ret)
			return ret;
	}

	ret = icc_set_bw(core->cpucfg_path, 0, 0);
	if (ret)
		goto err_cpucfg_path;

	ret = icc_set_bw(core->video_path, 0, 0);
	if (ret)
		goto err_video_path;

	return ret;

err_video_path:
	icc_set_bw(core->cpucfg_path, kbps_to_icc(1000), 0);
err_cpucfg_path:
	if (pm_ops->core_power)
		pm_ops->core_power(core, POWER_ON);

	return ret;
}

static __maybe_unused int venus_runtime_resume(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret;

	ret = icc_set_bw(core->video_path, kbps_to_icc(20000), 0);
	if (ret)
		return ret;

	ret = icc_set_bw(core->cpucfg_path, kbps_to_icc(1000), 0);
	if (ret)
		return ret;

	if (pm_ops->core_power) {
		ret = pm_ops->core_power(core, POWER_ON);
		if (ret)
			return ret;
	}

	return hfi_core_resume(core, false);
}

static const struct dev_pm_ops venus_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(venus_runtime_suspend, venus_runtime_resume, NULL)
};

static const struct freq_tbl msm8916_freq_table[] = {
	{ 352800, 228570000 },	/* 1920x1088 @ 30 + 1280x720 @ 30 */
	{ 244800, 160000000 },	/* 1920x1088 @ 30 */
	{ 108000, 100000000 },	/* 1280x720 @ 30 */
};

static const struct reg_val msm8916_reg_preset[] = {
	{ 0xe0020, 0x05555556 },
	{ 0xe0024, 0x05555556 },
	{ 0x80124, 0x00000003 },
};

static const struct venus_resources msm8916_res = {
	.freq_tbl = msm8916_freq_table,
	.freq_tbl_size = ARRAY_SIZE(msm8916_freq_table),
	.reg_tbl = msm8916_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(msm8916_reg_preset),
	.clks = { "core", "iface", "bus", },
	.clks_num = 3,
	.max_load = 352800, /* 720p@30 + 1080p@30 */
	.hfi_version = HFI_VERSION_1XX,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xddc00000 - 1,
	.fwname = "qcom/venus-1.8/venus.mbn",
};

static const struct freq_tbl msm8996_freq_table[] = {
	{ 1944000, 520000000 },	/* 4k UHD @ 60 (decode only) */
	{  972000, 520000000 },	/* 4k UHD @ 30 */
	{  489600, 346666667 },	/* 1080p @ 60 */
	{  244800, 150000000 },	/* 1080p @ 30 */
	{  108000,  75000000 },	/* 720p @ 30 */
};

static const struct reg_val msm8996_reg_preset[] = {
	{ 0x80010, 0xffffffff },
	{ 0x80018, 0x00001556 },
	{ 0x8001C, 0x00001556 },
};

static const struct venus_resources msm8996_res = {
	.freq_tbl = msm8996_freq_table,
	.freq_tbl_size = ARRAY_SIZE(msm8996_freq_table),
	.reg_tbl = msm8996_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(msm8996_reg_preset),
	.clks = {"core", "iface", "bus", "mbus" },
	.clks_num = 4,
	.vcodec0_clks = { "core" },
	.vcodec1_clks = { "core" },
	.vcodec_clks_num = 1,
	.max_load = 2563200,
	.hfi_version = HFI_VERSION_3XX,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xddc00000 - 1,
	.fwname = "qcom/venus-4.2/venus.mbn",
};

static const struct freq_tbl msm8998_freq_table[] = {
	{ 1944000, 465000000 },	/* 4k UHD @ 60 (decode only) */
	{  972000, 465000000 },	/* 4k UHD @ 30 */
	{  489600, 360000000 },	/* 1080p @ 60 */
	{  244800, 186000000 },	/* 1080p @ 30 */
	{  108000, 100000000 },	/* 720p @ 30 */
};

static const struct reg_val msm8998_reg_preset[] = {
	{ 0x80124, 0x00000003 },
	{ 0x80550, 0x01111111 },
	{ 0x80560, 0x01111111 },
	{ 0x80568, 0x01111111 },
	{ 0x80570, 0x01111111 },
	{ 0x80580, 0x01111111 },
	{ 0x80588, 0x01111111 },
	{ 0xe2010, 0x00000000 },
};

static const struct venus_resources msm8998_res = {
	.freq_tbl = msm8998_freq_table,
	.freq_tbl_size = ARRAY_SIZE(msm8998_freq_table),
	.reg_tbl = msm8998_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(msm8998_reg_preset),
	.clks = { "core", "iface", "bus", "mbus" },
	.clks_num = 4,
	.vcodec0_clks = { "core" },
	.vcodec1_clks = { "core" },
	.vcodec_clks_num = 1,
	.max_load = 2563200,
	.hfi_version = HFI_VERSION_3XX,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xddc00000 - 1,
	.fwname = "qcom/venus-4.4/venus.mbn",
};

static const struct freq_tbl sdm660_freq_table[] = {
	{ 979200, 518400000 },
	{ 489600, 441600000 },
	{ 432000, 404000000 },
	{ 244800, 320000000 },
	{ 216000, 269330000 },
	{ 108000, 133330000 },
};

static const struct reg_val sdm660_reg_preset[] = {
	{ 0x80010, 0x001f001f },
	{ 0x80018, 0x00000156 },
	{ 0x8001c, 0x00000156 },
};

static const struct bw_tbl sdm660_bw_table_enc[] = {
	{  979200,  1044000, 0, 2446336, 0 },	/* 4k UHD @ 30 */
	{  864000,   887000, 0, 2108416, 0 },	/* 720p @ 240 */
	{  489600,   666000, 0, 1207296, 0 },	/* 1080p @ 60 */
	{  432000,   578000, 0, 1058816, 0 },	/* 720p @ 120 */
	{  244800,   346000, 0,  616448, 0 },	/* 1080p @ 30 */
	{  216000,   293000, 0,  534528, 0 },	/* 720p @ 60 */
	{  108000,   151000, 0,  271360, 0 },	/* 720p @ 30 */
};

static const struct bw_tbl sdm660_bw_table_dec[] = {
	{  979200,  2365000, 0, 1892000, 0 },	/* 4k UHD @ 30 */
	{  864000,  1978000, 0, 1554000, 0 },	/* 720p @ 240 */
	{  489600,  1133000, 0,  895000, 0 },	/* 1080p @ 60 */
	{  432000,   994000, 0,  781000, 0 },	/* 720p @ 120 */
	{  244800,   580000, 0,  460000, 0 },	/* 1080p @ 30 */
	{  216000,   501000, 0,  301000, 0 },	/* 720p @ 60 */
	{  108000,   255000, 0,  202000, 0 },	/* 720p @ 30 */
};

static const struct venus_resources sdm660_res = {
	.freq_tbl = sdm660_freq_table,
	.freq_tbl_size = ARRAY_SIZE(sdm660_freq_table),
	.reg_tbl = sdm660_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(sdm660_reg_preset),
	.bw_tbl_enc = sdm660_bw_table_enc,
	.bw_tbl_enc_size = ARRAY_SIZE(sdm660_bw_table_enc),
	.bw_tbl_dec = sdm660_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sdm660_bw_table_dec),
	.clks = {"core", "iface", "bus", "bus_throttle" },
	.clks_num = 4,
	.vcodec0_clks = { "vcodec0_core" },
	.vcodec1_clks = { "vcodec0_core" },
	.vcodec_clks_num = 1,
	.vcodec_num = 1,
	.max_load = 1036800,
	.hfi_version = HFI_VERSION_3XX,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.cp_start = 0,
	.cp_size = 0x79000000,
	.cp_nonpixel_start = 0x1000000,
	.cp_nonpixel_size = 0x28000000,
	.dma_mask = 0xd9000000 - 1,
	.fwname = "qcom/venus-4.4/venus.mdt",
};

static const struct freq_tbl sdm845_freq_table[] = {
	{ 3110400, 533000000 },	/* 4096x2160@90 */
	{ 2073600, 444000000 },	/* 4096x2160@60 */
	{ 1944000, 404000000 },	/* 3840x2160@60 */
	{  972000, 330000000 },	/* 3840x2160@30 */
	{  489600, 200000000 },	/* 1920x1080@60 */
	{  244800, 100000000 },	/* 1920x1080@30 */
};

static const struct bw_tbl sdm845_bw_table_enc[] = {
	{ 1944000, 1612000, 0, 2416000, 0 },	/* 3840x2160@60 */
	{  972000,  951000, 0, 1434000, 0 },	/* 3840x2160@30 */
	{  489600,  723000, 0,  973000, 0 },	/* 1920x1080@60 */
	{  244800,  370000, 0,	495000, 0 },	/* 1920x1080@30 */
};

static const struct bw_tbl sdm845_bw_table_dec[] = {
	{ 2073600, 3929000, 0, 5551000, 0 },	/* 4096x2160@60 */
	{ 1036800, 1987000, 0, 2797000, 0 },	/* 4096x2160@30 */
	{  489600, 1040000, 0, 1298000, 0 },	/* 1920x1080@60 */
	{  244800,  530000, 0,  659000, 0 },	/* 1920x1080@30 */
};

static const struct venus_resources sdm845_res = {
	.freq_tbl = sdm845_freq_table,
	.freq_tbl_size = ARRAY_SIZE(sdm845_freq_table),
	.bw_tbl_enc = sdm845_bw_table_enc,
	.bw_tbl_enc_size = ARRAY_SIZE(sdm845_bw_table_enc),
	.bw_tbl_dec = sdm845_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sdm845_bw_table_dec),
	.clks = {"core", "iface", "bus" },
	.clks_num = 3,
	.vcodec0_clks = { "core", "bus" },
	.vcodec1_clks = { "core", "bus" },
	.vcodec_clks_num = 2,
	.max_load = 3110400,	/* 4096x2160@90 */
	.hfi_version = HFI_VERSION_4XX,
	.vpu_version = VPU_VERSION_AR50,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/venus-5.2/venus.mbn",
};

static const struct venus_resources sdm845_res_v2 = {
	.freq_tbl = sdm845_freq_table,
	.freq_tbl_size = ARRAY_SIZE(sdm845_freq_table),
	.bw_tbl_enc = sdm845_bw_table_enc,
	.bw_tbl_enc_size = ARRAY_SIZE(sdm845_bw_table_enc),
	.bw_tbl_dec = sdm845_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sdm845_bw_table_dec),
	.clks = {"core", "iface", "bus" },
	.clks_num = 3,
	.vcodec0_clks = { "vcodec0_core", "vcodec0_bus" },
	.vcodec1_clks = { "vcodec1_core", "vcodec1_bus" },
	.vcodec_clks_num = 2,
	.vcodec_pmdomains = (const char *[]) { "venus", "vcodec0", "vcodec1" },
	.vcodec_pmdomains_num = 3,
	.opp_pmdomain = (const char *[]) { "cx", NULL },
	.vcodec_num = 2,
	.max_load = 3110400,	/* 4096x2160@90 */
	.hfi_version = HFI_VERSION_4XX,
	.vpu_version = VPU_VERSION_AR50,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xe0000000 - 1,
	.cp_start = 0,
	.cp_size = 0x70800000,
	.cp_nonpixel_start = 0x1000000,
	.cp_nonpixel_size = 0x24800000,
	.fwname = "qcom/venus-5.2/venus.mbn",
};

static const struct freq_tbl sc7180_freq_table[] = {
	{  0, 500000000 },
	{  0, 434000000 },
	{  0, 340000000 },
	{  0, 270000000 },
	{  0, 150000000 },
};

static const struct bw_tbl sc7180_bw_table_enc[] = {
	{  972000,  750000, 0, 0, 0 },	/* 3840x2160@30 */
	{  489600,  451000, 0, 0, 0 },	/* 1920x1080@60 */
	{  244800,  234000, 0, 0, 0 },	/* 1920x1080@30 */
};

static const struct bw_tbl sc7180_bw_table_dec[] = {
	{ 1036800, 1386000, 0, 1875000, 0 },	/* 4096x2160@30 */
	{  489600,  865000, 0, 1146000, 0 },	/* 1920x1080@60 */
	{  244800,  530000, 0,  583000, 0 },	/* 1920x1080@30 */
};

static const struct venus_resources sc7180_res = {
	.freq_tbl = sc7180_freq_table,
	.freq_tbl_size = ARRAY_SIZE(sc7180_freq_table),
	.bw_tbl_enc = sc7180_bw_table_enc,
	.bw_tbl_enc_size = ARRAY_SIZE(sc7180_bw_table_enc),
	.bw_tbl_dec = sc7180_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sc7180_bw_table_dec),
	.clks = {"core", "iface", "bus" },
	.clks_num = 3,
	.vcodec0_clks = { "vcodec0_core", "vcodec0_bus" },
	.vcodec_clks_num = 2,
	.vcodec_pmdomains = (const char *[]) { "venus", "vcodec0" },
	.vcodec_pmdomains_num = 2,
	.opp_pmdomain = (const char *[]) { "cx", NULL },
	.vcodec_num = 1,
	.hfi_version = HFI_VERSION_4XX,
	.vpu_version = VPU_VERSION_AR50,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xe0000000 - 1,
	.cp_start = 0,
	.cp_size = 0x70800000,
	.cp_nonpixel_start = 0x1000000,
	.cp_nonpixel_size = 0x24800000,
	.fwname = "qcom/venus-5.4/venus.mbn",
};

static const struct freq_tbl sm8250_freq_table[] = {
	{ 0, 444000000 },
	{ 0, 366000000 },
	{ 0, 338000000 },
	{ 0, 240000000 },
};

static const struct bw_tbl sm8250_bw_table_enc[] = {
	{ 1944000, 1954000, 0, 3711000, 0 },	/* 3840x2160@60 */
	{  972000,  996000, 0, 1905000, 0 },	/* 3840x2160@30 */
	{  489600,  645000, 0,  977000, 0 },	/* 1920x1080@60 */
	{  244800,  332000, 0,	498000, 0 },	/* 1920x1080@30 */
};

static const struct bw_tbl sm8250_bw_table_dec[] = {
	{ 2073600, 2403000, 0, 4113000, 0 },	/* 4096x2160@60 */
	{ 1036800, 1224000, 0, 2079000, 0 },	/* 4096x2160@30 */
	{  489600,  812000, 0,  998000, 0 },	/* 1920x1080@60 */
	{  244800,  416000, 0,  509000, 0 },	/* 1920x1080@30 */
};

static const struct reg_val sm8250_reg_preset[] = {
	{ 0xb0088, 0 },
};

static const struct venus_resources sm8250_res = {
	.freq_tbl = sm8250_freq_table,
	.freq_tbl_size = ARRAY_SIZE(sm8250_freq_table),
	.reg_tbl = sm8250_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(sm8250_reg_preset),
	.bw_tbl_enc = sm8250_bw_table_enc,
	.bw_tbl_enc_size = ARRAY_SIZE(sm8250_bw_table_enc),
	.bw_tbl_dec = sm8250_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8250_bw_table_dec),
	.clks = {"core", "iface"},
	.clks_num = 2,
	.resets = { "bus", "core" },
	.resets_num = 2,
	.vcodec0_clks = { "vcodec0_core" },
	.vcodec_clks_num = 1,
	.vcodec_pmdomains = (const char *[]) { "venus", "vcodec0" },
	.vcodec_pmdomains_num = 2,
	.opp_pmdomain = (const char *[]) { "mx", NULL },
	.vcodec_num = 1,
	.max_load = 7833600,
	.hfi_version = HFI_VERSION_6XX,
	.vpu_version = VPU_VERSION_IRIS2,
	.num_vpp_pipes = 4,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu-1.0/venus.mbn",
};

static const struct freq_tbl sc7280_freq_table[] = {
	{ 0, 460000000 },
	{ 0, 424000000 },
	{ 0, 335000000 },
	{ 0, 240000000 },
	{ 0, 133333333 },
};

static const struct bw_tbl sc7280_bw_table_enc[] = {
	{ 1944000, 1896000, 0, 3657000, 0 },	/* 3840x2160@60 */
	{  972000,  968000, 0, 1848000, 0 },	/* 3840x2160@30 */
	{  489600,  618000, 0,  941000, 0 },	/* 1920x1080@60 */
	{  244800,  318000, 0,	480000, 0 },	/* 1920x1080@30 */
};

static const struct bw_tbl sc7280_bw_table_dec[] = {
	{ 2073600, 2128000, 0, 3831000, 0 },	/* 4096x2160@60 */
	{ 1036800, 1085000, 0, 1937000, 0 },	/* 4096x2160@30 */
	{  489600,  779000, 0,  998000, 0 },	/* 1920x1080@60 */
	{  244800,  400000, 0,  509000, 0 },	/* 1920x1080@30 */
};

static const struct reg_val sm7280_reg_preset[] = {
	{ 0xb0088, 0 },
};

static const struct hfi_ubwc_config sc7280_ubwc_config = {
	0, 0, {1, 1, 1, 0, 0, 0}, 8, 32, 14, 0, 0, {0, 0}
};

static const struct venus_resources sc7280_res = {
	.freq_tbl = sc7280_freq_table,
	.freq_tbl_size = ARRAY_SIZE(sc7280_freq_table),
	.reg_tbl = sm7280_reg_preset,
	.reg_tbl_size = ARRAY_SIZE(sm7280_reg_preset),
	.bw_tbl_enc = sc7280_bw_table_enc,
	.bw_tbl_enc_size = ARRAY_SIZE(sc7280_bw_table_enc),
	.bw_tbl_dec = sc7280_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sc7280_bw_table_dec),
	.ubwc_conf = &sc7280_ubwc_config,
	.clks = {"core", "bus", "iface"},
	.clks_num = 3,
	.vcodec0_clks = {"vcodec_core", "vcodec_bus"},
	.vcodec_clks_num = 2,
	.vcodec_pmdomains = (const char *[]) { "venus", "vcodec0" },
	.vcodec_pmdomains_num = 2,
	.opp_pmdomain = (const char *[]) { "cx", NULL },
	.vcodec_num = 1,
	.hfi_version = HFI_VERSION_6XX,
	.vpu_version = VPU_VERSION_IRIS2_1,
	.num_vpp_pipes = 1,
	.vmem_id = VIDC_RESOURCE_NONE,
	.vmem_size = 0,
	.vmem_addr = 0,
	.dma_mask = 0xe0000000 - 1,
	.cp_start = 0,
	.cp_size = 0x25800000,
	.cp_nonpixel_start = 0x1000000,
	.cp_nonpixel_size = 0x24800000,
	.fwname = "qcom/vpu-2.0/venus.mbn",
};

static const struct of_device_id venus_dt_match[] = {
	{ .compatible = "qcom,msm8916-venus", .data = &msm8916_res, },
	{ .compatible = "qcom,msm8996-venus", .data = &msm8996_res, },
	{ .compatible = "qcom,msm8998-venus", .data = &msm8998_res, },
	{ .compatible = "qcom,sdm660-venus", .data = &sdm660_res, },
	{ .compatible = "qcom,sdm845-venus", .data = &sdm845_res, },
	{ .compatible = "qcom,sdm845-venus-v2", .data = &sdm845_res_v2, },
	{ .compatible = "qcom,sc7180-venus", .data = &sc7180_res, },
	{ .compatible = "qcom,sc7280-venus", .data = &sc7280_res, },
	{ .compatible = "qcom,sm8250-venus", .data = &sm8250_res, },
	{ }
};
MODULE_DEVICE_TABLE(of, venus_dt_match);

static struct platform_driver qcom_venus_driver = {
	.probe = venus_probe,
	.remove_new = venus_remove,
	.driver = {
		.name = "qcom-venus",
		.of_match_table = venus_dt_match,
		.pm = &venus_pm_ops,
	},
	.shutdown = venus_core_shutdown,
};
module_platform_driver(qcom_venus_driver);

MODULE_ALIAS("platform:qcom-venus");
MODULE_DESCRIPTION("Qualcomm Venus video encoder and decoder driver");
MODULE_LICENSE("GPL v2");
