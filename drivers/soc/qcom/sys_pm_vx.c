// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/soc/qcom/qcom_aoss.h>
#include <soc/qcom/qcom_stats.h>

#define MAX_QMP_MSG_SIZE	96
#define MODE_AOSS		0xaa
#define MODE_CXPC		0xcc
#define MODE_DDR		0xdd
#define MODE_STR(m)		(m == MODE_CXPC ? "CXPC" :	\
				(m == MODE_AOSS ? "AOSS" :	\
				(m == MODE_DDR  ? "DDR"  : "")))

#define VX_MODE_MASK_TYPE		0xFF
#define VX_MODE_MASK_LOGSIZE		0xFF
#define VX_MODE_SHIFT_LOGSIZE		8
#define VX_FLAG_MASK_DUR		0xFFFF
#define VX_FLAG_MASK_TS			0xFF
#define VX_FLAG_SHIFT_TS		16
#define VX_FLAG_MASK_FLUSH_THRESH	0xFF
#define VX_FLAG_SHIFT_FLUSH_THRESH	24

#define DEFAULT_DEBUG_TIME (10 * 1000)
#define DEFAULT_TIMER (5000)
#define MAX_DRV_NAMES	26

#define read_word(base, itr) ({					\
		u32 v;						\
		v = le32_to_cpu(readl_relaxed(base + itr));	\
		pr_debug("Addr:%p val:%#x\n", base + itr, v);	\
		itr += sizeof(u32);				\
		/* Barrier to enssure sequential read */	\
		smp_rmb();					\
		v;						\
		})

struct vx_header {
	struct {
		u16 unused;
		u8 logsize;
		u8 type;
	} mode;
	struct {
		u8 flush_threshold;
		u8 ts_shift;
		u16 dur_ms;
	} flags;
};

struct vx_data {
	u32 ts;
	u32 *drv_vx;
};

struct vx_log {
	struct vx_header header;
	struct vx_data *data;
	int loglines;
};

struct vx_platform_data {
	void __iomem *base;
	struct dentry *vx_dir;
	size_t n_cxpc_drv;
	size_t n_aoss_drv;
	const char **cxpc_drvs;
	const char **aoss_drvs;
	struct mutex lock;
	struct qmp *qmp;
	ktime_t suspend_time;
	ktime_t resume_time;
	bool debug_enable;
	u32 detect_time_ms;
	u32 timer_ms;
	bool monitor_enable;
	bool debug_dump_enable;
};

static struct vx_platform_data *g_pd;

enum {
	CXPC_DRV_NAME,
	AOSS_DRV_NAME,
};

static const char * const drv_names_kalama[][MAX_DRV_NAMES] = {
	[CXPC_DRV_NAME] = {"TZ", "HYP", "HLOS", "L3", "SECPROC", "AUDIO", "AOP", "DEBUG",
			"GPU", "DISPLAY", "COMPUTE_DSP", "TME_SW", "TME_HW", "MDM SW",
			"MDM HW", "WLAN RF", "WLAN BB", "CAM_IFE0", "CAM_IFE1", "CAM_IFE2",
			"DDR AUX", "ARC CPRF", ""},
	[AOSS_DRV_NAME] = {"APPS", "SP", "AUDIO", "AOP", "DEBUG", "GPU", "DISPLAY", "COMPUTE",
			"TME", "MODEM", "WLAN BB", "CAM", ""},
};

static const char * const drv_names_pineapple[][MAX_DRV_NAMES] = {
	[CXPC_DRV_NAME] = {"TZ", "L3", "HLOS", "HYP", "SECPROC", "AUDIO", "AOP", "DEBUG",
			"GPU", "DISPLAY", "COMPUTE_DSP", "TME_HW", "TME_SW", "MDM SW",
			"MDM HW", "MDM Q6 CESTA", "WLAN RF", "WLAN BB", "CAM_IFE0 CESTA",
			"CAM_IFE1", "CAM_IFE2", "PCI0 CESTA", "PCI1 CESTA",
			"DDR AUX", "ARC CPRF", ""},
	[AOSS_DRV_NAME] = {"APPS", "SP", "AUDIO", "AOP", "DEBUG", "GPU", "DISPLAY", "COMPUTE",
			"TME", "MODEM", "WLAN BB", "CAM", "PCIE", ""},
};

static ssize_t debug_time_ms_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->detect_time_ms);
}

static ssize_t debug_time_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	if (val <= 0) {
		pr_err("debug time ms should be greater than zero\n");
		return -EINVAL;
	}

	mutex_lock(&pd->lock);
	pd->detect_time_ms = val;
	mutex_unlock(&pd->lock);

	return count;
}
static DEVICE_ATTR_RW(debug_time_ms);

static ssize_t set_timer_ms_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->timer_ms);
}

static ssize_t set_timer_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	if (val <= 0) {
		pr_err("timer ms should be greater than zero\n");
		return -EINVAL;
	}

	mutex_lock(&pd->lock);
	pd->timer_ms = val;
	mutex_unlock(&pd->lock);

	return count;
}
static DEVICE_ATTR_RW(set_timer_ms);

static ssize_t debug_enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->debug_enable);
}

static ssize_t debug_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	if ((val < 0) || (val > 1)) {
		pr_err("input error\n");
		return -EINVAL;
	}

	mutex_lock(&pd->lock);
	pd->debug_enable = val;
	mutex_unlock(&pd->lock);

	subsystem_sleep_debug_enable(pd->debug_enable);

	return count;
}
static DEVICE_ATTR_RW(debug_enable);

static void trigger_dump(struct vx_platform_data *pd)
{
	int ret = 0;
	char buf[MAX_QMP_MSG_SIZE] = {};

	if (!pd->debug_dump_enable)
		return;

	mutex_lock(&pd->lock);
	scnprintf(buf, sizeof(buf),
			"{class: misc_debug, res: do_crash_dump, tmr: %d}", pd->timer_ms);

	ret = qmp_send(pd->qmp, buf, sizeof(buf));
	if (ret)
		pr_err("Error sending qmp message: %d\n", ret);

	mutex_unlock(&pd->lock);
}

static void sys_pm_vx_send_msg(struct vx_platform_data *pd, bool enable)
{
	int ret = 0;
	char buf[MAX_QMP_MSG_SIZE] = {};

	mutex_lock(&pd->lock);
	if (enable)
		scnprintf(buf, sizeof(buf),
			"{class: lpm_mon, type: cxpc, dur: 1000, flush: 5, ts_adj: 1}");
	else
		scnprintf(buf, sizeof(buf),
			"{class: lpm_mon, type: cxpc, dur: 1000, flush: 1, log_once: 1}");

	ret = qmp_send(pd->qmp, buf, sizeof(buf));
	if (ret)
		pr_err("Error sending qmp message: %d\n", ret);

	mutex_unlock(&pd->lock);
}

static const char **vx_get_drvs_info(u8 type, struct vx_platform_data *pd,
				      size_t *ndrvs)
{
	const char **drvs;

	switch (type) {
	case MODE_CXPC:
	case MODE_DDR:
		*ndrvs = pd->n_cxpc_drv;
		drvs = pd->cxpc_drvs;
		break;
	case MODE_AOSS:
		*ndrvs = pd->n_aoss_drv;
		drvs = pd->aoss_drvs;
		break;
	default:
		return NULL;
	}

	return drvs;
}

static int read_vx_data(struct vx_platform_data *pd, struct vx_log *log)
{
	void __iomem *base = pd->base;
	struct vx_header *hdr = &log->header;
	struct vx_data *data;
	u32 *vx, val, itr = 0;
	int i, j, k;
	size_t n_drv;
	const char **drvs;

	val = read_word(base, itr);
	if (!val)
		return -ENOENT;

	hdr->mode.type = val & VX_MODE_MASK_TYPE;
	hdr->mode.logsize = (val >> VX_MODE_SHIFT_LOGSIZE) &
				    VX_MODE_MASK_LOGSIZE;
	drvs = vx_get_drvs_info(hdr->mode.type, pd, &n_drv);
	if (!drvs || !n_drv)
		return -ENODEV;

	val = read_word(base, itr);
	if (!val)
		return -ENOENT;

	hdr->flags.dur_ms = val & VX_FLAG_MASK_DUR;
	hdr->flags.ts_shift = (val >> VX_FLAG_SHIFT_TS) & VX_FLAG_MASK_TS;
	hdr->flags.flush_threshold = (val >> VX_FLAG_SHIFT_FLUSH_THRESH) &
					     VX_FLAG_MASK_FLUSH_THRESH;

	data = kcalloc(hdr->mode.logsize, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < hdr->mode.logsize; i++) {
		data[i].ts = read_word(base, itr);
		if (!data[i].ts)
			break;
		data[i].ts <<= hdr->flags.ts_shift;
		vx = kcalloc(ALIGN(n_drv, 4), sizeof(*vx), GFP_KERNEL);
		if (!vx)
			goto no_mem;

		for (j = 0; j < n_drv;) {
			val = read_word(base, itr);
			for (k = 0; k < 4; k++)
				vx[j++] = val >> (8 * k) & 0xFF;
		}
		data[i].drv_vx = vx;
	}

	log->data = data;
	log->loglines = i;

	return 0;
no_mem:
	for (j = 0; j < i; j++)
		kfree(data[j].drv_vx);
	kfree(data);

	return -ENOMEM;
}

static void vx_check_drv(struct vx_platform_data *pd)
{
	struct vx_log log;
	int i, j, ret;

	ret = read_vx_data(pd, &log);
	if (ret) {
		pr_err("fail to read vx data\n");
		return;
	}

	for (i = 0; i < pd->n_cxpc_drv; i++) {
		for (j = 0; j < log.loglines; j++) {
			if (log.data[j].drv_vx[i] == 0)
				break;
			if (j == log.loglines - 1) {
				pr_warn("DRV: %s has blocked power collapse\n", pd->cxpc_drvs[i]);
				trigger_dump(pd);
			}
		}
	}

	for (i = 0; i < log.loglines; i++)
		kfree(log.data[i].drv_vx);
	kfree(log.data);
}

static void show_vx_data(struct vx_platform_data *pd, struct vx_log *log,
			 struct seq_file *seq)
{
	int i, j;
	struct vx_header *hdr = &log->header;
	struct vx_data *data;
	u32 prev;
	bool from_exit = false;
	const char **drvs;
	size_t ndrv;

	drvs = vx_get_drvs_info(hdr->mode.type, pd, &ndrv);
	if (!drvs || !ndrv)
		return;

	seq_printf(seq, "Mode           : %s\n"
			"Duration (ms)  : %u\n"
			"Time Shift     : %u\n"
			"Flush Threshold: %u\n"
			"Max Log Entries: %u\n",
			MODE_STR(hdr->mode.type),
			hdr->flags.dur_ms,
			hdr->flags.ts_shift,
			hdr->flags.flush_threshold,
			hdr->mode.logsize);

	seq_puts(seq, "Timestamp|");

	for (i = 0; i < ndrv; i++)
		seq_printf(seq, "%*s|", 8, drvs[i]);
	seq_puts(seq, "\n");

	for (i = 0; i < log->loglines; i++) {
		data = &log->data[i];
		seq_printf(seq, "%*x|", 9, data->ts);
		/* An all-zero line indicates we entered LPM */
		for (j = 0, prev = data->drv_vx[0]; j < ndrv; j++)
			prev |= data->drv_vx[j];
		if (!prev) {
			if (!from_exit) {
				seq_printf(seq, "%s Enter\n", MODE_STR(hdr->mode.type));
				from_exit = true;
			} else {
				seq_printf(seq, "%s Exit\n", MODE_STR(hdr->mode.type));
				from_exit = false;
			}
			continue;
		}
		for (j = 0; j < ndrv; j++)
			seq_printf(seq, "%*u|", 8, data->drv_vx[j]);
		seq_puts(seq, "\n");
	}
}

static int vx_show(struct seq_file *seq, void *data)
{
	struct vx_platform_data *pd = seq->private;
	struct vx_log log;
	int ret;
	int i;

	/*
	 * Read the data into memory to allow for
	 * post processing of data and present it
	 * cleanly.
	 */
	ret = read_vx_data(pd, &log);
	if (ret)
		return ret;

	show_vx_data(pd, &log, seq);

	for (i = 0; i < log.loglines; i++)
		kfree(log.data[i].drv_vx);
	kfree(log.data);

	return 0;
}

static int open_vx(struct inode *inode, struct file *file)
{
	return single_open(file, vx_show, inode->i_private);
}

static const struct file_operations sys_pm_vx_fops = {
	.open = open_vx,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int trigger_dump_enable_get(void *data, u64 *val)
{
	struct vx_platform_data *pd = data;

	mutex_lock(&pd->lock);
	*val = (u64)pd->debug_dump_enable;
	mutex_unlock(&pd->lock);

	return 0;
}

static int trigger_dump_enable_set(void *data, u64 val)
{
	struct vx_platform_data *pd = data;

	mutex_lock(&pd->lock);
	pd->debug_dump_enable = (bool)val;
	mutex_unlock(&pd->lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(trigger_dump_enable_fops,
			trigger_dump_enable_get,
			trigger_dump_enable_set,
			"%lld\n");

void trigger_dump_enable(void)
{
	mutex_lock(&g_pd->lock);
	g_pd->debug_dump_enable = true;
	mutex_unlock(&g_pd->lock);
}
EXPORT_SYMBOL(trigger_dump_enable);

void trigger_dump_disable(void)
{
	mutex_lock(&g_pd->lock);
	g_pd->debug_dump_enable = false;
	mutex_unlock(&g_pd->lock);
}
EXPORT_SYMBOL(trigger_dump_disable);

static void vx_create_debug_nodes(struct dentry *root, struct vx_platform_data *pd)
{
	debugfs_create_file("sys_pm_violators", 0400, root,
				 pd, &sys_pm_vx_fops);

	debugfs_create_file("trigger_dump", 0600, root,
				 pd, &trigger_dump_enable_fops);
}

static const struct of_device_id drv_match_table[] = {
	{ .compatible = "qcom,sys-pm-kalama",
	  .data = drv_names_kalama },
	{ .compatible = "qcom,sys-pm-pineapple",
	  .data = drv_names_pineapple },
	{ }
};

static int vx_probe(struct platform_device *pdev)
{
	const struct of_device_id *match_id;
	struct vx_platform_data *pd;
	const char **drvs;
	int i, ret;

	g_pd = pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->base = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR_OR_NULL(pd->base))
		return PTR_ERR(pd->base);

	match_id = of_match_node(drv_match_table, pdev->dev.of_node);
	if (!match_id)
		return -ENODEV;

	drvs = (const char **)match_id->data;
	for (i = 0; ; i++) {
		const char *name = (const char *)drvs[i];

		if (!name[0])
			break;
	}
	pd->n_cxpc_drv = i;
	pd->cxpc_drvs = drvs;

	for (i = 0; ; i++) {
		const char *name = (const char *)drvs[MAX_DRV_NAMES + i];

		if (!name[0])
			break;
	}
	pd->n_aoss_drv = i;
	pd->aoss_drvs = &drvs[MAX_DRV_NAMES];

	pd->vx_dir = debugfs_create_dir("sys_pm_vx", NULL);
	if (!pd->vx_dir)
		return -EINVAL;

	vx_create_debug_nodes(pd->vx_dir, pd);

	ret = device_create_file(&pdev->dev, &dev_attr_debug_time_ms);
	if (ret) {
		dev_err(&pdev->dev, "failed: create sys pm vx sysfs debug time entry\n");
		goto fail_create_debug_time;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_debug_enable);
	if (ret) {
		dev_err(&pdev->dev, "failed: create sys pm vx sysfs debug enable entry\n");
		goto fail_create_debug_enable;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_set_timer_ms);
	if (ret) {
		dev_err(&pdev->dev, "failed: create sys pm vx sysfs set timer_ms entry\n");
		goto fail_create_set_timer;
	}

	pd->qmp = qmp_get(&pdev->dev);
	if (IS_ERR(pd->qmp)) {
		ret = PTR_ERR(pd->qmp);
		goto fail_get_qmp;
	}

	mutex_init(&pd->lock);
	pd->detect_time_ms = DEFAULT_DEBUG_TIME;
	pd->timer_ms = DEFAULT_TIMER;
	pd->debug_enable = false;
	pd->monitor_enable = false;
	pd->debug_dump_enable = false;

	platform_set_drvdata(pdev, pd);

	return 0;

fail_get_qmp:
	device_remove_file(&pdev->dev, &dev_attr_set_timer_ms);
fail_create_set_timer:
	device_remove_file(&pdev->dev, &dev_attr_debug_enable);
fail_create_debug_enable:
	device_remove_file(&pdev->dev, &dev_attr_debug_time_ms);
fail_create_debug_time:
	debugfs_remove_recursive(pd->vx_dir);
	return ret;
}

static int vx_remove(struct platform_device *pdev)
{
	struct vx_platform_data *pd = platform_get_drvdata(pdev);

	debugfs_remove_recursive(pd->vx_dir);
	device_remove_file(&pdev->dev, &dev_attr_debug_time_ms);
	device_remove_file(&pdev->dev, &dev_attr_debug_enable);
	device_remove_file(&pdev->dev, &dev_attr_set_timer_ms);
	qmp_put(pd->qmp);
	subsystem_sleep_debug_enable(false);

	return 0;
}

static int vx_suspend(struct device *dev)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);

	if (!pd->debug_enable)
		return 0;

	pd->suspend_time = ktime_get_boottime();
	if (pd->monitor_enable)
		sys_pm_vx_send_msg(pd, pd->monitor_enable);

	return 0;
}

static int vx_resume(struct device *dev)
{
	struct vx_platform_data *pd = dev_get_drvdata(dev);
	ktime_t time_delta_ms;
	bool system_slept;
	bool subsystem_slept;

	if (!pd->debug_enable)
		return 0;

	pd->resume_time = ktime_get_boottime();
	time_delta_ms = ktime_ms_delta(pd->resume_time, pd->suspend_time);
	if (time_delta_ms <= pd->detect_time_ms)
		return 0;

	system_slept = has_system_slept();
	if (system_slept)
		goto exit;

	subsystem_slept = has_subsystem_slept();
	if (!subsystem_slept)
		goto exit;

	/* if monitor was set last time check DRVs blocking system sleep */
	if (pd->monitor_enable)
		vx_check_drv(pd);
	else
		pd->monitor_enable = true;

	return 0;

exit:
	if (pd->monitor_enable)
		sys_pm_vx_send_msg(pd, false);
	pd->monitor_enable = false;

	return 0;
}

static const struct of_device_id vx_table[] = {
	{ .compatible = "qcom,sys-pm-violators" },
	{ }
};

static const struct dev_pm_ops vx_pm_ops = {
	.suspend = vx_suspend,
	.resume = vx_resume,
};

static struct platform_driver vx_driver = {
	.probe = vx_probe,
	.remove = vx_remove,
	.driver = {
		.name = "sys-pm-violators",
		.of_match_table = vx_table,
		.pm = &vx_pm_ops,
	},
};
module_platform_driver(vx_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) System PM Violators driver");
MODULE_ALIAS("platform:sys_pm_vx");
