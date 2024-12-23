// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>
#include <linux/thermal.h>
#include <linux/soc/qcom/llcc-qcom.h>
#include <linux/soc/qcom/cdsprm_cxlimit.h>
#include <linux/devfreq.h>
#include <linux/interconnect.h>
#include <linux/qcom_scm.h>
#include "npu_common.h"
#include "npu_hw.h"

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
#define CLASS_NAME              "npu"
#define DRIVER_NAME             "msm_npu"

#define MBOX_OP_TIMEOUTMS 1000

#define ICC_NPU_CDSPMEM          "icc-npu-cdspmem"
#define ICC_CPU_IMEMCFG          "icc-cpu-imemcfg"
/* -------------------------------------------------------------------------
 * File Scope Prototypes
 * -------------------------------------------------------------------------
 */
static int npu_enable_regulators(struct npu_device *npu_dev);
static void npu_disable_regulators(struct npu_device *npu_dev);
static int npu_enable_clocks(struct npu_device *npu_dev, bool post_pil);
static void npu_disable_clocks(struct npu_device *npu_dev, bool post_pil);
static int npu_enable_core_clocks(struct npu_device *npu_dev);
static void npu_disable_core_clocks(struct npu_device *npu_dev);
static uint32_t npu_calc_power_level(struct npu_device *npu_dev);
static ssize_t caps_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t pwr_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t pwr_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);
static ssize_t perf_mode_override_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t perf_mode_override_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);
static ssize_t dcvs_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t dcvs_mode_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);
static ssize_t fw_unload_delay_ms_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t fw_unload_delay_ms_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);
static ssize_t fw_state_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t fw_state_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);
static void npu_suspend_devbw(struct npu_device *npu_dev);
static void npu_resume_devbw(struct npu_device *npu_dev);
static bool npu_is_post_clock(const char *clk_name);
static bool npu_is_exclude_rate_clock(const char *clk_name);
static int npu_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state);
static int npu_get_cur_state(struct thermal_cooling_device *cdev,
				unsigned long *state);
static int npu_set_cur_state(struct thermal_cooling_device *cdev,
				unsigned long state);
static int npu_open(struct inode *inode, struct file *file);
static int npu_close(struct inode *inode, struct file *file);
static int npu_get_info(struct npu_client *client, unsigned long arg);
static int npu_map_buf(struct npu_client *client, unsigned long arg);
static int npu_unmap_buf(struct npu_client *client,
	unsigned long arg);
static int npu_load_network(struct npu_client *client,
	unsigned long arg);
static int npu_load_network_v2(struct npu_client *client,
	unsigned long arg);
static int npu_unload_network(struct npu_client *client,
	unsigned long arg);
static int npu_exec_network(struct npu_client *client,
	unsigned long arg);
static int npu_exec_network_v2(struct npu_client *client,
	unsigned long arg);
static int npu_set_fw_state(struct npu_client *client, uint32_t enable);
static int npu_set_property(struct npu_client *client,
	unsigned long arg);
static int npu_get_property(struct npu_client *client,
	unsigned long arg);
static long npu_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg);
static int npu_parse_dt_clock(struct npu_device *npu_dev);
static int npu_parse_dt_regulator(struct npu_device *npu_dev);
static int npu_of_parse_pwrlevels(struct npu_device *npu_dev,
		struct device_node *node);
static int npu_pwrctrl_init(struct npu_device *npu_dev);
static int npu_probe(struct platform_device *pdev);
static int npu_remove(struct platform_device *pdev);
static int npu_suspend(struct platform_device *dev, pm_message_t state);
static int npu_resume(struct platform_device *dev);
static int __init npu_init(void);
static void __exit npu_exit(void);
static int npu_set_power_level(struct npu_device *npu_dev, bool notify_cxlimit);
static uint32_t npu_notify_cdsprm_cxlimit_corner(struct npu_device *npu_dev,
	uint32_t pwr_lvl);
static void npu_icc_init(struct npu_device *npu_dev);
static void npu_icc_deinit(struct npu_device *npu_dev);
static void npu_disable_icc_bw(struct npu_device *npu_dev);
static void npu_enable_icc_bw(struct npu_device *npu_dev);
/* -------------------------------------------------------------------------
 * File Scope Variables
 * -------------------------------------------------------------------------
 */
static const char * const npu_post_clocks[] = {
	"npu_cpc_clk",
	"npu_cpc_timer_clk"
};

static const char * const npu_exclude_rate_clocks[] = {
	"qdss_clk",
	"at_clk",
	"trig_clk",
	"sleep_clk",
	"conf_noc_ahb_clk",
	"comp_noc_axi_clk",
	"npu_core_cti_clk",
	"npu_core_apb_clk",
	"npu_core_atb_clk",
	"npu_cpc_timer_clk",
	"qtimer_core_clk",
	"bwmon_clk",
	"bto_core_clk"
};


static struct npu_reg npu_saved_bw_registers[] = {
	{ BWMON2_SAMPLING_WINDOW, 0, false },
	{ BWMON2_BYTE_COUNT_THRESHOLD_HIGH, 0, false },
	{ BWMON2_BYTE_COUNT_THRESHOLD_MEDIUM, 0, false },
	{ BWMON2_BYTE_COUNT_THRESHOLD_LOW, 0, false },
	{ BWMON2_ZONE_ACTIONS, 0, false },
	{ BWMON2_ZONE_COUNT_THRESHOLD, 0, false },
};

static const struct npu_irq npu_irq_info[NPU_MAX_IRQ] = {
	{"ipc_irq", 0, IRQF_TRIGGER_HIGH},
	{"error_irq", 0, IRQF_TRIGGER_RISING | IRQF_ONESHOT},
	{"wdg_bite_irq", 0, IRQF_TRIGGER_RISING | IRQF_ONESHOT},
};

static struct npu_device *g_npu_dev;

/* -------------------------------------------------------------------------
 * Entry Points for Probe
 * -------------------------------------------------------------------------
 */
/* Sys FS */
static DEVICE_ATTR_RO(caps);
static DEVICE_ATTR_RW(pwr);
static DEVICE_ATTR_RW(perf_mode_override);
static DEVICE_ATTR_RW(dcvs_mode);
static DEVICE_ATTR_RW(fw_unload_delay_ms);
static DEVICE_ATTR_RW(fw_state);

static struct attribute *npu_fs_attrs[] = {
	&dev_attr_caps.attr,
	&dev_attr_pwr.attr,
	&dev_attr_perf_mode_override.attr,
	&dev_attr_dcvs_mode.attr,
	&dev_attr_fw_state.attr,
	&dev_attr_fw_unload_delay_ms.attr,
	NULL
};

static struct attribute_group npu_fs_attr_group = {
	.attrs = npu_fs_attrs
};

static const struct of_device_id npu_dt_match[] = {
	{ .compatible = "qcom,msm-npu",},
	{}
};

static struct platform_driver npu_driver = {
	.probe = npu_probe,
	.remove = npu_remove,
#if defined(CONFIG_PM)
	.suspend = npu_suspend,
	.resume = npu_resume,
#endif
	.driver = {
		.name = "msm_npu",
		.of_match_table = npu_dt_match,
		.pm = NULL,
	},
};

static const struct file_operations npu_fops = {
	.owner = THIS_MODULE,
	.open = npu_open,
	.release = npu_close,
	.unlocked_ioctl = npu_ioctl,
#ifdef CONFIG_COMPAT
	 .compat_ioctl = npu_ioctl,
#endif
};

static const struct thermal_cooling_device_ops npu_cooling_ops = {
	.get_max_state = npu_get_max_state,
	.get_cur_state = npu_get_cur_state,
	.set_cur_state = npu_set_cur_state,
};

/* -------------------------------------------------------------------------
 * SysFS - Capabilities
 * -------------------------------------------------------------------------
 */
static ssize_t caps_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t ret = 0;
	struct npu_device *npu_dev = dev_get_drvdata(dev);

	if (!npu_enable_core_power(npu_dev)) {
		if (scnprintf(buf, PAGE_SIZE, "hw_version :0x%X",
			REGR(npu_dev, NPU_HW_VERSION)) < 0)
			ret = -EINVAL;
		npu_disable_core_power(npu_dev);
	} else
		ret = -EPERM;

	return ret;
}

/* -------------------------------------------------------------------------
 * SysFS - Power State
 * -------------------------------------------------------------------------
 */
static ssize_t pwr_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			(pwr->pwr_vote_num > 0) ? "on" : "off");
}

static ssize_t pwr_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	bool pwr_on = false;

	if (strtobool(buf, &pwr_on) < 0)
		return -EINVAL;

	if (pwr_on) {
		if (npu_enable_core_power(npu_dev))
			return -EPERM;
	} else {
		npu_disable_core_power(npu_dev);
	}

	return count;
}

/* -------------------------------------------------------------------------
 * SysFS - perf_mode_override
 * -------------------------------------------------------------------------
 */
static ssize_t perf_mode_override_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pwr->perf_mode_override);
}

static ssize_t perf_mode_override_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct npu_client client;
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	uint32_t val;
	int rc;

	rc = kstrtou32(buf, 10, &val);
	if (rc) {
		pr_err("Invalid input for perf mode setting\n");
		return -EINVAL;
	}

	val = min(val, npu_dev->pwrctrl.num_pwrlevels);
	npu_dev->pwrctrl.perf_mode_override = val;
	pr_info("setting uc_pwrlevel_override to %d\n", val);

	client.npu_dev = npu_dev;
	npu_host_set_perf_mode(&client, 0, val);

	return count;
}

static ssize_t dcvs_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pwr->dcvs_mode);
}

static ssize_t dcvs_mode_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct msm_npu_property prop;
	uint32_t val;
	int ret = 0;

	ret = kstrtou32(buf, 10, &val);
	if (ret) {
		pr_err("Invalid input for dcvs mode setting\n");
		return -EINVAL;
	}

	val = min(val, (uint32_t)(npu_dev->pwrctrl.num_pwrlevels - 1));
	pr_debug("sysfs: setting dcvs_mode to %d\n", val);

	prop.prop_id = MSM_NPU_PROP_ID_DCVS_MODE;
	prop.num_of_params = 1;
	prop.network_hdl = 0;
	prop.prop_param[0] = val;

	ret = npu_host_set_fw_property(npu_dev, &prop);
	if (ret) {
		pr_err("npu_host_set_fw_property failed %d\n", ret);
		return ret;
	}

	npu_dev->pwrctrl.dcvs_mode = val;

	return count;
}

/* -------------------------------------------------------------------------
 * SysFS - Delayed FW unload
 * -------------------------------------------------------------------------
 */
static ssize_t fw_unload_delay_ms_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
		npu_dev->host_ctx.fw_unload_delay_ms);
}

static ssize_t fw_unload_delay_ms_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	uint32_t val;
	int rc;

	rc = kstrtou32(buf, 10, &val);
	if (rc) {
		pr_err("Invalid input for fw unload delay setting\n");
		return -EINVAL;
	}

	npu_dev->host_ctx.fw_unload_delay_ms = val;
	pr_debug("setting fw_unload_delay_ms to %d\n", val);

	return count;
}

/* -------------------------------------------------------------------------
 * SysFS - firmware state
 * -------------------------------------------------------------------------
 */
static ssize_t fw_state_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			(npu_dev->host_ctx.fw_state == FW_ENABLED) ?
			"on" : "off");
}

static ssize_t fw_state_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct npu_client client;
	bool enable = false;
	int rc;

	if (strtobool(buf, &enable) < 0)
		return -EINVAL;

	client.npu_dev = npu_dev;
	rc = npu_set_fw_state(&client, enable ? 1 : 0);

	if (rc) {
		pr_err("%s fw failed\n", enable ? "enable" : "disable");
		return rc;
	}

	return count;
}

/* -------------------------------------------------------------------------
 * Power Related
 * -------------------------------------------------------------------------
 */
static enum npu_power_level cdsprm_corner_to_npu_power_level(
	enum cdsprm_npu_corner corner)
{
	enum npu_power_level pwr_lvl = NPU_PWRLEVEL_TURBO_L1;

	switch (corner) {
	case CDSPRM_NPU_CLK_OFF:
		pwr_lvl = NPU_PWRLEVEL_OFF;
		break;
	case CDSPRM_NPU_MIN_SVS:
		pwr_lvl = NPU_PWRLEVEL_MINSVS;
		break;
	case CDSPRM_NPU_LOW_SVS:
		pwr_lvl = NPU_PWRLEVEL_LOWSVS;
		break;
	case CDSPRM_NPU_SVS:
		pwr_lvl = NPU_PWRLEVEL_SVS;
		break;
	case CDSPRM_NPU_SVS_L1:
		pwr_lvl = NPU_PWRLEVEL_SVS_L1;
		break;
	case CDSPRM_NPU_NOM:
		pwr_lvl = NPU_PWRLEVEL_NOM;
		break;
	case CDSPRM_NPU_NOM_L1:
		pwr_lvl = NPU_PWRLEVEL_NOM_L1;
		break;
	case CDSPRM_NPU_TURBO:
		pwr_lvl = NPU_PWRLEVEL_TURBO;
		break;
	case CDSPRM_NPU_TURBO_L1:
	default:
		pwr_lvl = NPU_PWRLEVEL_TURBO_L1;
		break;
	}

	return pwr_lvl;
}

static enum cdsprm_npu_corner npu_power_level_to_cdsprm_corner(
	enum npu_power_level pwr_lvl)
{
	enum cdsprm_npu_corner corner = CDSPRM_NPU_MIN_SVS;

	switch (pwr_lvl) {
	case NPU_PWRLEVEL_OFF:
		corner = CDSPRM_NPU_CLK_OFF;
		break;
	case NPU_PWRLEVEL_MINSVS:
		corner = CDSPRM_NPU_MIN_SVS;
		break;
	case NPU_PWRLEVEL_LOWSVS:
		corner = CDSPRM_NPU_LOW_SVS;
		break;
	case NPU_PWRLEVEL_SVS:
		corner = CDSPRM_NPU_SVS;
		break;
	case NPU_PWRLEVEL_SVS_L1:
		corner = CDSPRM_NPU_SVS_L1;
		break;
	case NPU_PWRLEVEL_NOM:
		corner = CDSPRM_NPU_NOM;
		break;
	case NPU_PWRLEVEL_NOM_L1:
		corner = CDSPRM_NPU_NOM_L1;
		break;
	case NPU_PWRLEVEL_TURBO:
		corner = CDSPRM_NPU_TURBO;
		break;
	case NPU_PWRLEVEL_TURBO_L1:
	default:
		corner = CDSPRM_NPU_TURBO_L1;
		break;
	}

	return corner;
}

static int npu_set_cdsprm_corner_limit(enum cdsprm_npu_corner corner)
{
	struct npu_pwrctrl *pwr;
	enum npu_power_level pwr_lvl;

	if (!g_npu_dev)
		return 0;

	pwr = &g_npu_dev->pwrctrl;
	pwr_lvl = cdsprm_corner_to_npu_power_level(corner);
	pwr->cdsprm_pwrlevel = pwr_lvl;
	pr_debug("power level from cdsp %d\n", pwr_lvl);

	return npu_set_power_level(g_npu_dev, false);
}

static const struct cdsprm_npu_limit_cbs cdsprm_npu_limit_cbs = {
	.set_corner_limit = npu_set_cdsprm_corner_limit,
};

int npu_notify_cdsprm_cxlimit_activity(struct npu_device *npu_dev, bool enable)
{
	if (!npu_dev->cxlimit_registered)
		return 0;

	pr_debug("notify cxlimit %s activity\n", enable ? "enable" : "disable");

	return cdsprm_cxlimit_npu_activity_notify(enable ? 1 : 0);
	return 0;
}

static uint32_t npu_notify_cdsprm_cxlimit_corner(
	struct npu_device *npu_dev, uint32_t pwr_lvl)
{
	uint32_t corner, pwr_lvl_to_set;

	if (!npu_dev->cxlimit_registered)
		return pwr_lvl;

	corner = npu_power_level_to_cdsprm_corner(pwr_lvl);
	corner = cdsprm_cxlimit_npu_corner_notify(corner);
	pwr_lvl_to_set = cdsprm_corner_to_npu_power_level(corner);
	pr_debug("Notify cdsprm %d:%d\n", pwr_lvl,
			pwr_lvl_to_set);

	return pwr_lvl_to_set;
}

static int npu_cdsprm_cxlimit_init(struct npu_device *npu_dev)
{
	bool enabled;
	int ret = 0;

	enabled = of_property_read_bool(npu_dev->pdev->dev.of_node,
		"qcom,npu-cxlimit-enable");
	pr_debug("qcom,npu-xclimit-enable is %s\n", enabled ? "true" : "false");

	npu_dev->cxlimit_registered = false;
	if (enabled) {
		ret = cdsprm_cxlimit_npu_limit_register(&cdsprm_npu_limit_cbs);
		ret = 0;
		if (ret) {
			pr_err("register cxlimit npu limit failed\n");
		} else {
			pr_debug("register cxlimit npu limit succeeds\n");
			npu_dev->cxlimit_registered = true;
		}
	}

	return ret;
}

static int npu_cdsprm_cxlimit_deinit(struct npu_device *npu_dev)
{
	int ret = 0;

	if (npu_dev->cxlimit_registered) {
		ret = cdsprm_cxlimit_npu_limit_deregister();
		ret = 0;
		if (ret)
			pr_err("deregister cxlimit npu limit failed\n");
		npu_dev->cxlimit_registered = false;
	}

	return ret;
}

int npu_enable_core_power(struct npu_device *npu_dev)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	int ret = 0;

	mutex_lock(&npu_dev->dev_lock);
	if (!pwr->pwr_vote_num) {
		npu_enable_icc_bw(npu_dev);
		ret = npu_enable_regulators(npu_dev);
		if (ret) {
			npu_disable_icc_bw(npu_dev);
			goto fail;
		}


		ret = npu_enable_core_clocks(npu_dev);
		if (ret) {
			npu_disable_regulators(npu_dev);
			pwr->pwr_vote_num = 0;
			npu_disable_icc_bw(npu_dev);
			goto fail;
		}
		npu_resume_devbw(npu_dev);
	}
	pwr->pwr_vote_num++;
fail:
	mutex_unlock(&npu_dev->dev_lock);

	return ret;
}

void npu_disable_core_power(struct npu_device *npu_dev)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	mutex_lock(&npu_dev->dev_lock);
	if (!pwr->pwr_vote_num) {
		mutex_unlock(&npu_dev->dev_lock);
		return;
	}

	pwr->pwr_vote_num--;
	if (!pwr->pwr_vote_num) {
		npu_suspend_devbw(npu_dev);
		npu_disable_core_clocks(npu_dev);
		npu_disable_regulators(npu_dev);
		npu_disable_icc_bw(npu_dev);
		pwr->active_pwrlevel = pwr->default_pwrlevel;
		pwr->uc_pwrlevel = pwr->max_pwrlevel;
		pwr->cdsprm_pwrlevel = pwr->max_pwrlevel;
		pwr->cur_dcvs_activity = pwr->num_pwrlevels;
		pr_debug("setting back to power level=%d\n",
			pwr->active_pwrlevel);
	}
	mutex_unlock(&npu_dev->dev_lock);
}

static int npu_enable_core_clocks(struct npu_device *npu_dev)
{
	return npu_enable_clocks(npu_dev, false);
}

static void npu_disable_core_clocks(struct npu_device *npu_dev)
{
	return npu_disable_clocks(npu_dev, false);
}

int npu_enable_post_pil_clocks(struct npu_device *npu_dev)
{
	return npu_enable_clocks(npu_dev, true);
}

void npu_disable_post_pil_clocks(struct npu_device *npu_dev)
{
	npu_disable_clocks(npu_dev, true);
}

static uint32_t npu_power_level_from_index(struct npu_device *npu_dev,
	uint32_t index)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	if (index >= pwr->num_pwrlevels)
		index = pwr->num_pwrlevels - 1;

	return pwr->pwrlevels[index].pwr_level;
}

static uint32_t npu_power_level_to_index(struct npu_device *npu_dev,
	uint32_t pwr_lvl)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	int i;

	for (i = 0; i < pwr->num_pwrlevels; i++) {
		if (pwr->pwrlevels[i].pwr_level > pwr_lvl)
			break;
	}


	return i == 0 ? 0 : i - 1;
}

static uint32_t npu_calc_power_level(struct npu_device *npu_dev)
{
	uint32_t ret_level;
	uint32_t therm_pwr_level = npu_dev->thermalctrl.pwr_level;
	uint32_t active_pwr_level = npu_dev->pwrctrl.active_pwrlevel;
	uint32_t uc_pwr_level = npu_dev->pwrctrl.uc_pwrlevel;

	/*
	 * pick the lowese power level between thermal power and usecase power
	 * settings
	 */
	ret_level = min(therm_pwr_level, uc_pwr_level);
	pr_debug("%s therm=%d active=%d uc=%d set level=%d\n",
		__func__, therm_pwr_level, active_pwr_level, uc_pwr_level,
		ret_level);

	return ret_level;
}

static int npu_set_power_level(struct npu_device *npu_dev, bool notify_cxlimit)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	struct npu_pwrlevel *pwrlevel;
	int i, ret = 0;
	uint32_t pwr_level_to_set, pwr_level_to_cdsprm, pwr_level_idx;

	/* get power level to set */
	pwr_level_to_set = npu_calc_power_level(npu_dev);
	pwr_level_to_cdsprm = pwr_level_to_set;

	if (!pwr->pwr_vote_num) {
		pr_debug("power is not enabled during set request\n");
		pwr->active_pwrlevel = min(pwr_level_to_set,
			npu_dev->pwrctrl.cdsprm_pwrlevel);
		return 0;
	}

	/* notify cxlimit to get allowed power level */
	if ((pwr_level_to_set > pwr->active_pwrlevel) && notify_cxlimit)
		pwr_level_to_set = npu_notify_cdsprm_cxlimit_corner(
					npu_dev, pwr_level_to_cdsprm);

	pwr_level_to_set = min(pwr_level_to_set,
		npu_dev->pwrctrl.cdsprm_pwrlevel);

	/* if the same as current, dont do anything */
	if (pwr_level_to_set == pwr->active_pwrlevel) {
		pr_debug("power level %d doesn't change\n", pwr_level_to_set);
		return 0;
	}

	pr_debug("setting power level to [%d]\n", pwr_level_to_set);
	pwr_level_idx = npu_power_level_to_index(npu_dev, pwr_level_to_set);
	pwrlevel = &npu_dev->pwrctrl.pwrlevels[pwr_level_idx];

	for (i = 0; i < npu_dev->core_clk_num; i++) {
		if (npu_is_exclude_rate_clock(
			npu_dev->core_clks[i].clk_name))
			continue;

		if (npu_dev->host_ctx.fw_state == FW_DISABLED) {
			if (npu_is_post_clock(
				npu_dev->core_clks[i].clk_name))
				continue;
		}

		pr_debug("requested rate of clock [%s] to [%ld]\n",
			npu_dev->core_clks[i].clk_name, pwrlevel->clk_freq[i]);

		ret = clk_set_rate(npu_dev->core_clks[i].clk,
			pwrlevel->clk_freq[i]);
		if (ret) {
			pr_debug("clk_set_rate %s to %ld failed with %d\n",
				npu_dev->core_clks[i].clk_name,
				pwrlevel->clk_freq[i], ret);
			break;
		}
	}

	if ((pwr_level_to_cdsprm < pwr->active_pwrlevel) && notify_cxlimit) {
		npu_notify_cdsprm_cxlimit_corner(npu_dev,
			pwr_level_to_cdsprm);
		pr_debug("Notify cdsprm(post) %d\n", pwr_level_to_cdsprm);
	}

	pwr->active_pwrlevel = pwr_level_to_set;
	return ret;
}

int npu_set_uc_power_level(struct npu_device *npu_dev,
	uint32_t perf_mode)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	uint32_t uc_pwrlevel_to_set;

	uc_pwrlevel_to_set = npu_power_level_from_index(npu_dev,
		perf_mode - 1);

	if (uc_pwrlevel_to_set > pwr->max_pwrlevel)
		uc_pwrlevel_to_set = pwr->max_pwrlevel;

	pwr->uc_pwrlevel = uc_pwrlevel_to_set;
	return npu_set_power_level(npu_dev, true);
}

/* -------------------------------------------------------------------------
 * Bandwidth Related
 * -------------------------------------------------------------------------
 */
static void npu_save_bw_registers(struct npu_device *npu_dev)
{
	int i;

	if (!npu_dev->bwmon_io.base)
		return;

	for (i = 0; i < ARRAY_SIZE(npu_saved_bw_registers); i++) {
		npu_saved_bw_registers[i].val = npu_bwmon_reg_read(npu_dev,
			npu_saved_bw_registers[i].off);
		npu_saved_bw_registers[i].valid = true;
	}
}

static void npu_restore_bw_registers(struct npu_device *npu_dev)
{
	int i;

	if (!npu_dev->bwmon_io.base)
		return;

	for (i = 0; i < ARRAY_SIZE(npu_saved_bw_registers); i++) {
		if (npu_saved_bw_registers[i].valid) {
			npu_bwmon_reg_write(npu_dev,
				npu_saved_bw_registers[i].off,
				npu_saved_bw_registers[i].val);
			npu_saved_bw_registers[i].valid = false;
		}
	}
}

static void npu_suspend_devbw(struct npu_device *npu_dev)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	int ret;

	if (pwr->bwmon_enabled && pwr->devbw) {
		pwr->bwmon_enabled = 0;
		//TODO
		//ret = devfreq_suspend_icc(pwr->devbw);
		ret = 0;
		if (ret)
			pr_err("devfreq_suspend_devbw failed rc:%d\n",
				ret);
		npu_save_bw_registers(npu_dev);
	}
}

static void npu_resume_devbw(struct npu_device *npu_dev)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	int ret;

	if (!pwr->bwmon_enabled && pwr->devbw) {
		pwr->bwmon_enabled = 1;
		npu_restore_bw_registers(npu_dev);
		//TODO
		//ret = devfreq_resume_icc(pwr->devbw);
		ret = 0;
		if (ret)
			pr_err("devfreq_resume_devbw failed rc:%d\n", ret);
	}
}

/* -------------------------------------------------------------------------
 * Clocks Related
 * -------------------------------------------------------------------------
 */
static bool npu_is_post_clock(const char *clk_name)
{
	int ret = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(npu_post_clocks); i++) {
		if (!strcmp(clk_name, npu_post_clocks[i])) {
			ret = true;
			break;
		}
	}
	return ret;
}

static bool npu_is_exclude_rate_clock(const char *clk_name)
{
	int ret = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(npu_exclude_rate_clocks); i++) {
		if (!strcmp(clk_name, npu_exclude_rate_clocks[i])) {
			ret = true;
			break;
		}
	}
	return ret;
}

static int npu_enable_clocks(struct npu_device *npu_dev, bool post_pil)
{
	int i, rc = 0;
	struct npu_clk *core_clks = npu_dev->core_clks;
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	struct npu_pwrlevel *pwrlevel;
	uint32_t pwrlevel_to_set, pwrlevel_idx;

	pwrlevel_to_set = pwr->active_pwrlevel;
	if (!post_pil) {
		pwrlevel_to_set = npu_notify_cdsprm_cxlimit_corner(
			npu_dev, pwrlevel_to_set);
		pr_debug("Notify cdsprm %d\n", pwrlevel_to_set);
		pwr->active_pwrlevel = pwrlevel_to_set;
	}

	pwrlevel_idx = npu_power_level_to_index(npu_dev, pwrlevel_to_set);
	pwrlevel = &pwr->pwrlevels[pwrlevel_idx];
	for (i = 0; i < npu_dev->core_clk_num; i++) {
		if (post_pil) {
			if (!npu_is_post_clock(core_clks[i].clk_name))
				continue;
		} else {
			if (npu_is_post_clock(core_clks[i].clk_name))
				continue;
		}

		pr_debug("enabling clock %s\n", core_clks[i].clk_name);

		rc = clk_prepare_enable(core_clks[i].clk);
		if (rc) {
			pr_err("%s enable failed\n",
				core_clks[i].clk_name);
			break;
		}

		if (npu_is_exclude_rate_clock(core_clks[i].clk_name))
			continue;

		pr_debug("setting rate of clock %s to %ld\n",
			core_clks[i].clk_name, pwrlevel->clk_freq[i]);

		rc = clk_set_rate(core_clks[i].clk,
			pwrlevel->clk_freq[i]);
		/* not fatal error, keep using previous clk rate */
		if (rc) {
			pr_err("clk_set_rate %s to %ld failed\n",
				core_clks[i].clk_name,
				pwrlevel->clk_freq[i]);
			rc = 0;
		}
	}

	if (rc) {
		for (i--; i >= 0; i--) {
			if (post_pil) {
				if (!npu_is_post_clock(core_clks[i].clk_name))
					continue;
			} else {
				if (npu_is_post_clock(core_clks[i].clk_name))
					continue;
			}
			pr_debug("disabling clock %s\n", core_clks[i].clk_name);
			clk_disable_unprepare(core_clks[i].clk);
		}
	}

	return rc;
}

static void npu_disable_clocks(struct npu_device *npu_dev, bool post_pil)
{
	int i = 0;
	struct npu_clk *core_clks = npu_dev->core_clks;

	if (!post_pil) {
		npu_notify_cdsprm_cxlimit_corner(npu_dev, NPU_PWRLEVEL_OFF);
		pr_debug("Notify cdsprm clock off\n");
	}

	for (i = npu_dev->core_clk_num - 1; i >= 0 ; i--) {
		if (post_pil) {
			if (!npu_is_post_clock(core_clks[i].clk_name))
				continue;
		} else {
			if (npu_is_post_clock(core_clks[i].clk_name))
				continue;
		}

		pr_debug("disabling clock %s\n", core_clks[i].clk_name);
		clk_disable_unprepare(core_clks[i].clk);
	}
}

/* -------------------------------------------------------------------------
 * Thermal Functions
 * -------------------------------------------------------------------------
 */
static int npu_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct npu_device *npu_dev = cdev->devdata;
	struct npu_thermalctrl *thermalctrl = &npu_dev->thermalctrl;

	pr_debug("enter %s thermal max state=%lu\n", __func__,
		thermalctrl->max_state);

	*state = thermalctrl->max_state;

	return 0;
}

static int npu_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct npu_device *npu_dev = cdev->devdata;
	struct npu_thermalctrl *thermal = &npu_dev->thermalctrl;

	pr_debug("enter %s thermal current state=%lu\n", __func__,
		thermal->current_state);

	*state = thermal->current_state;

	return 0;
}

static int
npu_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct npu_device *npu_dev = cdev->devdata;
	struct npu_thermalctrl *thermal = &npu_dev->thermalctrl;

	pr_debug("enter %s request state=%lu\n", __func__, state);
	if (state > thermal->max_state)
		return -EINVAL;

	thermal->current_state = state;
	thermal->pwr_level =  npu_power_level_from_index(npu_dev,
		thermal->max_state - state);

	return npu_set_power_level(npu_dev, true);
}

/* -------------------------------------------------------------------------
 * Regulator Related
 * -------------------------------------------------------------------------
 */
static int npu_enable_regulators(struct npu_device *npu_dev)
{
	int i = 0;
	int rc = 0;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct npu_regulator *regulators = npu_dev->regulators;

	if (!host_ctx->power_vote_num) {
		for (i = 0; i < npu_dev->regulator_num; i++) {
			rc = regulator_enable(regulators[i].regulator);
			if (rc < 0) {
				pr_err("%s enable failed\n",
					regulators[i].regulator_name);
				break;
			}
			pr_debug("regulator %s enabled\n",
				regulators[i].regulator_name);
		}
	}
	host_ctx->power_vote_num++;
	return rc;
}

static void npu_disable_regulators(struct npu_device *npu_dev)
{
	int i = 0;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct npu_regulator *regulators = npu_dev->regulators;

	if (host_ctx->power_vote_num > 0) {
		for (i = 0; i < npu_dev->regulator_num; i++) {
			regulator_disable(regulators[i].regulator);
			pr_debug("regulator %s disabled\n",
				regulators[i].regulator_name);
		}
		host_ctx->power_vote_num--;
	}
}

/* -------------------------------------------------------------------------
 * Interrupt Related
 * -------------------------------------------------------------------------
 */
int npu_enable_irq(struct npu_device *npu_dev)
{
	int i;

	/* clear pending irq state */
	REGW(npu_dev, NPU_MASTERn_IPC_IRQ_OUT(0), 0x0);
	REGW(npu_dev, NPU_MASTERn_ERROR_IRQ_CLEAR(0), NPU_ERROR_IRQ_MASK);
	REGW(npu_dev, NPU_MASTERn_ERROR_IRQ_ENABLE(0), NPU_ERROR_IRQ_MASK);
	REGW(npu_dev, NPU_MASTERn_ERROR_IRQ_OWNER(0), NPU_ERROR_IRQ_MASK);
	REGW(npu_dev, NPU_MASTERn_WDOG_IRQ_OWNER(0), NPU_WDOG_IRQ_MASK);

	for (i = 0; i < NPU_MAX_IRQ; i++) {
		if (npu_dev->irq[i].irq != 0) {
			enable_irq(npu_dev->irq[i].irq);
			pr_debug("enable irq %d\n", npu_dev->irq[i].irq);
		}
	}

	return 0;
}

void npu_disable_irq(struct npu_device *npu_dev)
{
	int i;

	for (i = 0; i < NPU_MAX_IRQ; i++) {
		if (npu_dev->irq[i].irq != 0) {
			disable_irq(npu_dev->irq[i].irq);
			pr_debug("disable irq %d\n", npu_dev->irq[i].irq);
		}
	}

	REGW(npu_dev, NPU_MASTERn_ERROR_IRQ_ENABLE(0), 0);
	/* clear pending irq state */
	REGW(npu_dev, NPU_MASTERn_IPC_IRQ_OUT(0), 0x0);
	REGW(npu_dev, NPU_MASTERn_ERROR_IRQ_CLEAR(0), NPU_ERROR_IRQ_MASK);
}

/* -------------------------------------------------------------------------
 * System Cache
 * -------------------------------------------------------------------------
 */
int npu_enable_sys_cache(struct npu_device *npu_dev)
{
	int rc = 0;
	uint32_t reg_val = 0;

	if (!npu_dev->host_ctx.sys_cache_disable) {
		npu_dev->sys_cache = llcc_slice_getd(LLCC_NPU);
		if (IS_ERR_OR_NULL(npu_dev->sys_cache)) {
			pr_warn("unable to init sys cache\n");
			npu_dev->sys_cache = NULL;
			npu_dev->host_ctx.sys_cache_disable = true;
			return 0;
		}

		/* set npu side regs - program SCID */
		reg_val = NPU_CACHE_ATTR_IDn___POR | SYS_CACHE_SCID;

		REGW(npu_dev, NPU_CACHE_ATTR_IDn(0), reg_val);
		REGW(npu_dev, NPU_CACHE_ATTR_IDn(1), reg_val);
		REGW(npu_dev, NPU_CACHE_ATTR_IDn(2), reg_val);
		REGW(npu_dev, NPU_CACHE_ATTR_IDn(3), reg_val);
		REGW(npu_dev, NPU_CACHE_ATTR_IDn(4), reg_val);

		pr_debug("prior to activate sys cache\n");
		rc = llcc_slice_activate(npu_dev->sys_cache);
		if (rc) {
			pr_warn("failed to activate sys cache\n");
			llcc_slice_putd(npu_dev->sys_cache);
			npu_dev->sys_cache = NULL;
			return 0;
		}

		pr_debug("sys cache activated\n");
	}

	return rc;
}

void npu_disable_sys_cache(struct npu_device *npu_dev)
{
	int rc = 0;

	if (!npu_dev->host_ctx.sys_cache_disable) {
		if (npu_dev->sys_cache) {
			rc = llcc_slice_deactivate(npu_dev->sys_cache);
			if (rc) {
				pr_err("failed to deactivate sys cache\n");
				return;
			}
			pr_debug("sys cache deactivated\n");
			llcc_slice_putd(npu_dev->sys_cache);
			npu_dev->sys_cache = NULL;
		}
	}
}

/* -------------------------------------------------------------------------
 * Open/Close
 * -------------------------------------------------------------------------
 */
static int npu_open(struct inode *inode, struct file *file)
{
	struct npu_device *npu_dev = container_of(inode->i_cdev,
		struct npu_device, cdev);
	struct npu_client *client;

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->npu_dev = npu_dev;
	mutex_init(&client->list_lock);
	INIT_LIST_HEAD(&(client->mapped_buffer_list));
	file->private_data = client;

	return 0;
}

static int npu_close(struct inode *inode, struct file *file)
{
	struct npu_client *client = file->private_data;

	npu_host_cleanup_networks(client);
	mutex_destroy(&client->list_lock);
	kfree(client);
	return 0;
}

/* -------------------------------------------------------------------------
 * IOCTL Implementations
 * -------------------------------------------------------------------------
 */
static int npu_get_info(struct npu_client *client, unsigned long arg)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct msm_npu_get_info_ioctl req;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ret = copy_from_user(&req, argp, sizeof(req));

	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	ret = npu_host_get_info(npu_dev, &req);

	if (ret) {
		pr_err("npu_host_get_info failed\n");
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));

	if (ret) {
		pr_err("fail to copy to user\n");
		return -EFAULT;
	}
	return 0;
}

static int npu_map_buf(struct npu_client *client, unsigned long arg)
{
	struct msm_npu_map_buf_ioctl req;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ret = copy_from_user(&req, argp, sizeof(req));

	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	ret = npu_host_map_buf(client, &req);

	if (ret) {
		pr_err("npu_host_map_buf failed\n");
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));

	if (ret) {
		pr_err("fail to copy to user\n");
		return -EFAULT;
	}
	return 0;
}

static int npu_unmap_buf(struct npu_client *client, unsigned long arg)
{
	struct msm_npu_unmap_buf_ioctl req;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ret = copy_from_user(&req, argp, sizeof(req));

	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	ret = npu_host_unmap_buf(client, &req);

	if (ret) {
		pr_err("npu_host_unmap_buf failed\n");
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));

	if (ret) {
		pr_err("fail to copy to user\n");
		return -EFAULT;
	}
	return 0;
}

static int npu_load_network(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_load_network_ioctl req;
	struct msm_npu_unload_network_ioctl unload_req;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ret = copy_from_user(&req, argp, sizeof(req));

	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	pr_debug("network load with perf request %d\n", req.perf_mode);

	ret = npu_host_load_network(client, &req);
	if (ret) {
		pr_err("npu_host_load_network failed %d\n", ret);
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));
	if (ret) {
		pr_err("fail to copy to user\n");
		ret = -EFAULT;
		unload_req.network_hdl = req.network_hdl;
		npu_host_unload_network(client, &unload_req);
	}
	return ret;
}

static int npu_load_network_v2(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_load_network_ioctl_v2 req;
	struct msm_npu_unload_network_ioctl unload_req;
	void __user *argp = (void __user *)arg;
	struct msm_npu_patch_info_v2 *patch_info = NULL;
	int ret;

	ret = copy_from_user(&req, argp, sizeof(req));
	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	if (req.patch_info_num > NPU_MAX_PATCH_NUM) {
		pr_err("Invalid patch info num %d[max:%d]\n",
			req.patch_info_num, NPU_MAX_PATCH_NUM);
		return -EINVAL;
	}

	if (req.patch_info_num) {
		patch_info = kmalloc_array(req.patch_info_num,
			sizeof(*patch_info), GFP_KERNEL);
		if (!patch_info)
			return -ENOMEM;

		ret = copy_from_user(patch_info,
			(void __user *)req.patch_info,
			req.patch_info_num * sizeof(*patch_info));
		if (ret) {
			pr_err("fail to copy patch info\n");
			kfree(patch_info);
			return -EFAULT;
		}
	}

	pr_debug("network load with perf request %d\n", req.perf_mode);

	ret = npu_host_load_network_v2(client, &req, patch_info);

	kfree(patch_info);
	if (ret) {
		pr_err("npu_host_load_network_v2 failed %d\n", ret);
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));
	if (ret) {
		pr_err("fail to copy to user\n");
		ret = -EFAULT;
		unload_req.network_hdl = req.network_hdl;
		npu_host_unload_network(client, &unload_req);
	}

	return ret;
}

static int npu_unload_network(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_unload_network_ioctl req;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ret = copy_from_user(&req, argp, sizeof(req));

	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	ret = npu_host_unload_network(client, &req);

	if (ret) {
		pr_err("npu_host_unload_network failed %d\n", ret);
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));

	if (ret) {
		pr_err("fail to copy to user\n");
		return -EFAULT;
	}
	return 0;
}

static int npu_exec_network(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_exec_network_ioctl req;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ret = copy_from_user(&req, argp, sizeof(req));

	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	if ((req.input_layer_num > MSM_NPU_MAX_INPUT_LAYER_NUM) ||
		(req.output_layer_num > MSM_NPU_MAX_OUTPUT_LAYER_NUM)) {
		pr_err("Invalid input/out layer num %d[max:%d] %d[max:%d]\n",
			req.input_layer_num, MSM_NPU_MAX_INPUT_LAYER_NUM,
			req.output_layer_num, MSM_NPU_MAX_OUTPUT_LAYER_NUM);
		return -EINVAL;
	}

	if (!req.patching_required) {
		pr_err("Only support patched network\n");
		return -EINVAL;
	}

	ret = npu_host_exec_network(client, &req);

	if (ret) {
		pr_err("npu_host_exec_network failed %d\n", ret);
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));

	if (ret) {
		pr_err("fail to copy to user\n");
		return -EFAULT;
	}
	return 0;
}

static int npu_exec_network_v2(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_exec_network_ioctl_v2 req;
	void __user *argp = (void __user *)arg;
	struct msm_npu_patch_buf_info *patch_buf_info = NULL;
	int ret;

	ret = copy_from_user(&req, argp, sizeof(req));
	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	if ((req.patch_buf_info_num > NPU_MAX_PATCH_NUM) ||
		(req.patch_buf_info_num == 0)) {
		pr_err("Invalid patch buf info num %d[max:%d]\n",
			req.patch_buf_info_num, NPU_MAX_PATCH_NUM);
		return -EINVAL;
	}

	if (req.stats_buf_size > NPU_MAX_STATS_BUF_SIZE) {
		pr_err("Invalid stats buffer size %d max %d\n",
			req.stats_buf_size, NPU_MAX_STATS_BUF_SIZE);
		return -EINVAL;
	}

	if (req.patch_buf_info_num) {
		patch_buf_info = kmalloc_array(req.patch_buf_info_num,
			sizeof(*patch_buf_info), GFP_KERNEL);
		if (!patch_buf_info)
			return -ENOMEM;

		ret = copy_from_user(patch_buf_info,
			(void __user *)req.patch_buf_info,
			req.patch_buf_info_num * sizeof(*patch_buf_info));
		if (ret) {
			pr_err("fail to copy patch buf info\n");
			kfree(patch_buf_info);
			return -EFAULT;
		}
	}

	ret = npu_host_exec_network_v2(client, &req, patch_buf_info);

	kfree(patch_buf_info);
	if (ret) {
		pr_err("npu_host_exec_network_v2 failed %d\n", ret);
		return ret;
	}

	ret = copy_to_user(argp, &req, sizeof(req));
	if (ret) {
		pr_err("fail to copy to user\n");
		ret = -EFAULT;
	}

	return ret;
}

static int npu_set_fw_state(struct npu_client *client, uint32_t enable)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	int rc = 0;

	if (host_ctx->network_num > 0) {
		return -EINVAL;
	}

	if (enable) {
		pr_debug("enable fw\n");
		rc = fw_init(npu_dev);
		if (rc) {
			pr_err("enable fw failed\n");
		} else {
			host_ctx->npu_init_cnt++;
			pr_debug("npu_init_cnt %d\n",
				host_ctx->npu_init_cnt);
			/* set npu to lowest power level */
			if (npu_set_uc_power_level(npu_dev, 1))
				pr_warn("Failed to set uc power level\n");
		}
	} else if (host_ctx->npu_init_cnt > 0) {
		pr_debug("disable fw\n");
		fw_deinit(npu_dev, false, true);
		host_ctx->npu_init_cnt--;
		pr_debug("npu_init_cnt %d\n", host_ctx->npu_init_cnt);
	} else {
		pr_err("can't disable fw %d\n", host_ctx->npu_init_cnt);
	}

	return rc;
}

static int npu_set_property(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_property prop;
	void __user *argp = (void __user *)arg;
	int ret = -EINVAL;

	ret = copy_from_user(&prop, argp, sizeof(prop));
	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	switch (prop.prop_id) {
	case MSM_NPU_PROP_ID_FW_STATE:
		ret = npu_set_fw_state(client,
			(uint32_t)prop.prop_param[0]);
		break;
	case MSM_NPU_PROP_ID_PERF_MODE:
		ret = npu_host_set_perf_mode(client,
			(uint32_t)prop.network_hdl,
			(uint32_t)prop.prop_param[0]);
		break;
	default:
		ret = npu_host_set_fw_property(client->npu_dev, &prop);
		if (ret)
			pr_err("npu_host_set_fw_property failed\n");
		break;
	}

	return ret;
}

static int npu_get_property(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_property prop;
	void __user *argp = (void __user *)arg;
	int ret = -EINVAL;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	ret = copy_from_user(&prop, argp, sizeof(prop));
	if (ret) {
		pr_err("fail to copy from user\n");
		return -EFAULT;
	}

	switch (prop.prop_id) {
	case MSM_NPU_PROP_ID_FW_STATE:
		prop.prop_param[0] = host_ctx->fw_state;
		break;
	case MSM_NPU_PROP_ID_PERF_MODE:
		prop.prop_param[0] = npu_host_get_perf_mode(client,
			(uint32_t)prop.network_hdl);
		break;
	case MSM_NPU_PROP_ID_PERF_MODE_MAX:
		prop.prop_param[0] = npu_dev->pwrctrl.num_pwrlevels;
		break;
	case MSM_NPU_PROP_ID_DRV_VERSION:
		prop.prop_param[0] = 0;
		break;
	case MSM_NPU_PROP_ID_HARDWARE_VERSION:
		prop.prop_param[0] = npu_dev->hw_version;
		break;
	default:
		ret = npu_host_get_fw_property(client->npu_dev, &prop);
		if (ret) {
			pr_err("npu_host_set_fw_property failed\n");
			return ret;
		}
		break;
	}

	ret = copy_to_user(argp, &prop, sizeof(prop));
	if (ret) {
		pr_err("fail to copy to user\n");
		return -EFAULT;
	}

	return ret;
}

static long npu_ioctl(struct file *file, unsigned int cmd,
						 unsigned long arg)
{
	int ret = -ENOIOCTLCMD;
	struct npu_client *client = file->private_data;

	switch (cmd) {
	case MSM_NPU_GET_INFO:
		ret = npu_get_info(client, arg);
		break;
	case MSM_NPU_MAP_BUF:
		ret = npu_map_buf(client, arg);
		break;
	case MSM_NPU_UNMAP_BUF:
		ret = npu_unmap_buf(client, arg);
		break;
	case MSM_NPU_LOAD_NETWORK:
		ret = npu_load_network(client, arg);
		break;
	case MSM_NPU_LOAD_NETWORK_V2:
		ret = npu_load_network_v2(client, arg);
		break;
	case MSM_NPU_UNLOAD_NETWORK:
		ret = npu_unload_network(client, arg);
		break;
	case MSM_NPU_EXEC_NETWORK:
		ret = npu_exec_network(client, arg);
		break;
	case MSM_NPU_EXEC_NETWORK_V2:
		ret = npu_exec_network_v2(client, arg);
		break;
	case MSM_NPU_SET_PROP:
		ret = npu_set_property(client, arg);
		break;
	case MSM_NPU_GET_PROP:
		ret = npu_get_property(client, arg);
		break;
	default:
		pr_err("unexpected IOCTL %x\n", cmd);
	}

	return ret;
}

/* -------------------------------------------------------------------------
 * Device Tree Parsing
 * -------------------------------------------------------------------------
 */
static int npu_parse_dt_clock(struct npu_device *npu_dev)
{
	int rc = 0;
	uint32_t i;
	const char *clock_name;
	int num_clk;
	struct npu_clk *core_clks = npu_dev->core_clks;
	struct platform_device *pdev = npu_dev->pdev;

	num_clk = of_property_count_strings(pdev->dev.of_node,
			"clock-names");
	if (num_clk <= 0) {
		pr_err("clocks are not defined\n");
		rc = -EINVAL;
		goto clk_err;
	} else if (num_clk > NUM_MAX_CLK_NUM) {
		pr_err("number of clocks %d exceeds limit\n", num_clk);
		rc = -EINVAL;
		goto clk_err;
	}

	npu_dev->core_clk_num = num_clk;
	for (i = 0; i < num_clk; i++) {
		of_property_read_string_index(pdev->dev.of_node, "clock-names",
							i, &clock_name);
		strscpy(core_clks[i].clk_name, clock_name,
			sizeof(core_clks[i].clk_name));
		core_clks[i].clk = devm_clk_get(&pdev->dev, clock_name);
		if (IS_ERR(core_clks[i].clk)) {
			pr_err("unable to get clk: %s\n", clock_name);
			rc = -EINVAL;
			break;
		}
	}

clk_err:
	return rc;
}

static int npu_parse_dt_regulator(struct npu_device *npu_dev)
{
	int rc = 0;
	uint32_t i;
	const char *name;
	int num;
	struct npu_regulator *regulators = npu_dev->regulators;
	struct platform_device *pdev = npu_dev->pdev;

	num = of_property_count_strings(pdev->dev.of_node,
			"qcom,proxy-reg-names");
	if (num <= 0) {
		rc = -EINVAL;
		pr_err("regulator not defined\n");
		goto regulator_err;
	}
	if (num > NPU_MAX_REGULATOR_NUM) {
		rc = -EINVAL;
		pr_err("regulator number %d is over the limit %d\n", num,
			NPU_MAX_REGULATOR_NUM);
		num = NPU_MAX_REGULATOR_NUM;
	}

	npu_dev->regulator_num = num;
	for (i = 0; i < num; i++) {
		of_property_read_string_index(pdev->dev.of_node,
			"qcom,proxy-reg-names", i, &name);
		strscpy(regulators[i].regulator_name, name,
				sizeof(regulators[i].regulator_name));
		regulators[i].regulator = devm_regulator_get(&pdev->dev, name);
		if (IS_ERR(regulators[i].regulator)) {
			pr_err("unable to get regulator: %s\n", name);
			rc = -EINVAL;
			break;
		}
	}

regulator_err:
	return rc;
}

static int npu_of_parse_pwrlevels(struct npu_device *npu_dev,
		struct device_node *node)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	struct device_node *child;
	uint32_t init_level_index = 0, init_power_level;
	uint32_t fmax, fmax_pwrlvl;

	pwr->num_pwrlevels = 0;
	pwr->min_pwrlevel = NPU_PWRLEVEL_TURBO_L1;
	pwr->max_pwrlevel = NPU_PWRLEVEL_MINSVS;

	for_each_available_child_of_node(node, child) {
		uint32_t i = 0;
		uint32_t index;
		uint32_t pwr_level;
		uint32_t clk_array_values[NUM_MAX_CLK_NUM];
		uint32_t clk_rate;
		struct npu_pwrlevel *level;

		if (of_property_read_u32(child, "reg", &index)) {
			pr_err("Can't find reg property\n");
			return -EINVAL;
		}

		if (of_property_read_u32(child, "vreg", &pwr_level)) {
			pr_err("Can't find vreg property\n");
			return -EINVAL;
		}

		if (index >= NPU_MAX_PWRLEVELS) {
			pr_err("pwrlevel index %d is out of range\n",
				index);
			continue;
		}

		if (index >= pwr->num_pwrlevels)
			pwr->num_pwrlevels = index + 1;

		if (of_property_read_u32_array(child, "clk-freq",
			clk_array_values, npu_dev->core_clk_num)) {
			pr_err("pwrlevel index %d read clk-freq failed %d\n",
				index, npu_dev->core_clk_num);
			return -EINVAL;
		}

		level = &pwr->pwrlevels[index];
		level->pwr_level = pwr_level;
		if (pwr->min_pwrlevel > pwr_level)
			pwr->min_pwrlevel = pwr_level;
		if (pwr->max_pwrlevel < pwr_level)
			pwr->max_pwrlevel = pwr_level;

		for (i = 0; i < npu_dev->core_clk_num; i++) {
			if (npu_is_exclude_rate_clock(
				npu_dev->core_clks[i].clk_name))
				continue;

			clk_rate = clk_round_rate(npu_dev->core_clks[i].clk,
				clk_array_values[i]);
			pr_debug("clk %s rate [%u]:[%u]\n",
				npu_dev->core_clks[i].clk_name,
				clk_array_values[i], clk_rate);
			level->clk_freq[i] = clk_rate;
		}
	}

	/* Read FMAX info if available */
	if (npu_dev->qfprom_io.base) {
		fmax = (npu_qfprom_reg_read(npu_dev,
			QFPROM_FMAX_REG_OFFSET) & QFPROM_FMAX_BITS_MASK) >>
			QFPROM_FMAX_BITS_SHIFT;
		pr_debug("fmax %x\n", fmax);

		switch (fmax) {
		case 1:
		case 2:
			fmax_pwrlvl = NPU_PWRLEVEL_NOM;
			break;
		case 3:
			fmax_pwrlvl = NPU_PWRLEVEL_SVS_L1;
			break;
		default:
			fmax_pwrlvl = pwr->max_pwrlevel;
			break;
		}

		if (fmax_pwrlvl < pwr->max_pwrlevel)
			pwr->max_pwrlevel = fmax_pwrlvl;
	}

	of_property_read_u32(node, "initial-pwrlevel", &init_level_index);
	pr_debug("initial-pwrlevel %d\n", init_level_index);

	if (init_level_index >= pwr->num_pwrlevels)
		init_level_index = pwr->num_pwrlevels - 1;

	init_power_level = npu_power_level_from_index(npu_dev,
		init_level_index);
	if (init_power_level > pwr->max_pwrlevel) {
		init_power_level = pwr->max_pwrlevel;
		pr_debug("Adjust init power level to %d\n", init_power_level);
	}

	pr_debug("init power level %d max %d min %d\n", init_power_level,
		pwr->max_pwrlevel, pwr->min_pwrlevel);
	pwr->active_pwrlevel = pwr->default_pwrlevel = init_power_level;
	pwr->uc_pwrlevel = pwr->max_pwrlevel;
	pwr->perf_mode_override = 0;
	pwr->cdsprm_pwrlevel = pwr->max_pwrlevel;
	pwr->cur_dcvs_activity = pwr->num_pwrlevels;

	return 0;
}

static int npu_pwrctrl_init(struct npu_device *npu_dev)
{
	struct platform_device *pdev = npu_dev->pdev;
	struct device_node *node;
	int ret = 0;
	struct platform_device *p2dev;
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	/* Power levels */
	node = of_find_node_by_name(pdev->dev.of_node, "qcom,npu-pwrlevels");

	if (!node) {
		pr_err("unable to find 'qcom,npu-pwrlevels'\n");
		return -EINVAL;
	}

	ret = npu_of_parse_pwrlevels(npu_dev, node);
	if (ret)
		return ret;

	/* Parse Bandwidth */
	node = of_parse_phandle(pdev->dev.of_node,
				"qcom,npubw-dev", 0);

	if (node) {
		/* Set to 1 initially - we assume bwmon is on */
		pwr->bwmon_enabled = 1;
		p2dev = of_find_device_by_node(node);
		if (p2dev) {
			pwr->devbw = &p2dev->dev;
		} else {
			pr_err("parser power level failed\n");
			ret = -EINVAL;
			return ret;
		}
	} else {
		pr_warn("bwdev is not defined in dts\n");
		pwr->devbw = NULL;
	}

	return ret;
}

static int npu_thermalctrl_init(struct npu_device *npu_dev)
{
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	struct npu_thermalctrl *thermalctrl = &npu_dev->thermalctrl;

	thermalctrl->max_state = pwr->num_pwrlevels - 1;
	thermalctrl->current_state = 0;
	return 0;
}

static int npu_irq_init(struct npu_device *npu_dev)
{
	unsigned long irq_type;
	int ret = 0, i;

	memcpy(npu_dev->irq, npu_irq_info, sizeof(npu_irq_info));
	for (i = 0; i < NPU_MAX_IRQ; i++) {
		irq_type = npu_irq_info[i].irq_type;
		npu_dev->irq[i].irq = platform_get_irq_byname(
			npu_dev->pdev, npu_dev->irq[i].name);
		if (npu_dev->irq[i].irq < 0) {
			pr_err("get_irq for %s failed\n\n",
				npu_dev->irq[i].name);
			ret = -EINVAL;
			break;
		}

		pr_debug("irq %s: %d\n", npu_dev->irq[i].name,
			npu_dev->irq[i].irq);
		irq_set_status_flags(npu_dev->irq[i].irq,
						IRQ_NOAUTOEN);
		ret = devm_request_irq(&npu_dev->pdev->dev,
				npu_dev->irq[i].irq, npu_intr_hdler,
				irq_type, npu_dev->irq[i].name,
				npu_dev);
		if (ret) {
			pr_err("devm_request_irq(%s:%d) failed\n",
				npu_dev->irq[i].name,
				npu_dev->irq[i].irq);
			break;
		}
	}

	return ret;
}

static int npu_mbox_init(struct npu_device *npu_dev)
{
	struct platform_device *pdev = npu_dev->pdev;
	struct npu_mbox *mbox_aop = &npu_dev->mbox_aop;

	if (of_find_property(pdev->dev.of_node, "mboxes", NULL)) {
		mbox_aop->client.dev = &pdev->dev;
		mbox_aop->client.tx_block = true;
		mbox_aop->client.tx_tout = MBOX_OP_TIMEOUTMS;
		mbox_aop->client.knows_txdone = false;

		mbox_aop->chan = mbox_request_channel(&mbox_aop->client, 0);
		if (IS_ERR(mbox_aop->chan)) {
			pr_warn("aop mailbox is not available\n");
			mbox_aop->chan = NULL;
		}
	}
	return 0;
}

static void npu_mbox_deinit(struct npu_device *npu_dev)
{
	if (npu_dev->mbox_aop.chan) {
		mbox_free_channel(npu_dev->mbox_aop.chan);
		npu_dev->mbox_aop.chan = NULL;
	}
}

static int npu_hw_info_init(struct npu_device *npu_dev)
{
	int rc = 0;

	rc = npu_enable_core_power(npu_dev);
	if (rc) {
		pr_err("Failed to enable power\n");
		return rc;
	}

	npu_dev->hw_version = REGR(npu_dev, NPU_HW_VERSION);
	pr_debug("NPU_HW_VERSION 0x%x\n", npu_dev->hw_version);
	npu_disable_core_power(npu_dev);

	return rc;
}

static void npu_icc_init(struct npu_device *npu_dev)
{
	struct platform_device *pdev = npu_dev->pdev;

	npu_dev->icc_npu_cdspmem = of_icc_get(&pdev->dev, ICC_NPU_CDSPMEM);
	if (IS_ERR_OR_NULL(npu_dev->icc_npu_cdspmem)) {
		dev_err(&pdev->dev, "(%ld): failed getting %s path\n",
			PTR_ERR(npu_dev->icc_npu_cdspmem), ICC_NPU_CDSPMEM);
		npu_dev->icc_npu_cdspmem = NULL;
	} else {
		pr_info("get interconnects between npu and cdsp_mem successfully\n");
	}
	npu_dev->icc_cpu_imemcfg = of_icc_get(&pdev->dev, ICC_CPU_IMEMCFG);
	if (IS_ERR_OR_NULL(npu_dev->icc_cpu_imemcfg)) {
		dev_err(&pdev->dev, "(%ld): failed getting %s path\n",
			PTR_ERR(npu_dev->icc_cpu_imemcfg), ICC_CPU_IMEMCFG);
		npu_dev->icc_cpu_imemcfg = NULL;
	} else {
		pr_info("get interconnects between cpu and imemcfg successfully\n");
	}
}

static void npu_icc_deinit(struct npu_device *npu_dev)
{
	if (npu_dev->icc_npu_cdspmem) {
		icc_put(npu_dev->icc_npu_cdspmem);
		npu_dev->icc_npu_cdspmem = NULL;
	}
	if (npu_dev->icc_cpu_imemcfg) {
		icc_put(npu_dev->icc_cpu_imemcfg);
		npu_dev->icc_cpu_imemcfg = NULL;
	}
}

static void npu_enable_icc_bw(struct npu_device *npu_dev)
{
	int ret = 0;

	if (npu_dev->icc_npu_cdspmem) {
		ret = icc_set_bw(npu_dev->icc_npu_cdspmem, 100, 100);
		if (ret)
			pr_err("set interconnects npu-cdspmem bw failed, ret: (%d)\n", ret);
	} else {
		pr_err("icc_path icc_npu_cdspmem is nullptr\n");
	}

	if (npu_dev->icc_cpu_imemcfg) {
		ret = icc_set_bw(npu_dev->icc_cpu_imemcfg, 100, 100);
		if (ret)
			pr_err("set interconnects cpu-imemcfg bw failed, ret: (%d)\n", ret);
	} else {
		pr_err("icc_path icc_cpu_imemcfg is nullptr\n");
	}
}

static void npu_disable_icc_bw(struct npu_device *npu_dev)
{
	icc_set_bw(npu_dev->icc_npu_cdspmem, 0, 0);
	icc_set_bw(npu_dev->icc_cpu_imemcfg, 0, 0);
}
static int npu_alloc_memory_region(struct npu_device *npu_dev)
{
	struct device *dev = &npu_dev->pdev->dev;
	struct device_node *node;
	struct resource r;
	int ret;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node) {
		pr_err("no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	of_node_put(node);
	if (ret)
		return ret;

	npu_dev->fw_io.mem_phys = npu_dev->fw_io.mem_reloc = r.start;
	npu_dev->fw_io.mem_size = resource_size(&r);
	npu_dev->fw_io.mem_region = devm_ioremap_wc(dev, npu_dev->fw_io.mem_phys,
			npu_dev->fw_io.mem_size);
	if (!npu_dev->fw_io.mem_region) {
		pr_err("unable to map memory region: %pa+%zx\n",
			&r.start, npu_dev->fw_io.mem_size);
		return -EBUSY;
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Probe/Remove
 * -------------------------------------------------------------------------
 */
static int npu_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct resource *res = NULL;
	struct npu_device *npu_dev = NULL;
	struct thermal_cooling_device *tcdev = NULL;

	if (!qcom_scm_is_available()) {
		pr_err("qcom scm is not available, npu probe defer\n");
		return -EPROBE_DEFER;
	}
	npu_dev = devm_kzalloc(&pdev->dev,
		sizeof(struct npu_device), GFP_KERNEL);
	if (!npu_dev)
		return -EFAULT;
	npu_dev->pdev = pdev;
	mutex_init(&npu_dev->dev_lock);

	platform_set_drvdata(pdev, npu_dev);
	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "core");
	if (!res) {
		pr_err("unable to get core resource\n");
		rc = -ENODEV;
		goto error_get_dev_num;
	}
	npu_dev->core_io.size = resource_size(res);
	npu_dev->core_io.base = devm_ioremap(&pdev->dev, res->start,
					npu_dev->core_io.size);
	if (unlikely(!npu_dev->core_io.base)) {
		pr_err("unable to map core\n");
		rc = -ENOMEM;
		goto error_get_dev_num;
	}
	pr_debug("core phy address=0x%llx virt=%pK\n",
		res->start, npu_dev->core_io.base);

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "tcm");
	if (!res) {
		pr_err("unable to get tcm resource\n");
		rc = -ENODEV;
		goto error_get_dev_num;
	}
	npu_dev->tcm_io.size = resource_size(res);
	npu_dev->tcm_io.base = devm_ioremap(&pdev->dev, res->start,
					npu_dev->tcm_io.size);
	if (unlikely(!npu_dev->tcm_io.base)) {
		pr_err("unable to map tcm\n");
		rc = -ENOMEM;
		goto error_get_dev_num;
	}
	pr_debug("core phy address=0x%llx virt=%pK\n",
		res->start, npu_dev->tcm_io.base);

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "bwmon");
	if (!res) {
		pr_err("unable to get bwmon resource\n");
		rc = -ENODEV;
		goto error_get_dev_num;
	}
	npu_dev->bwmon_io.size = resource_size(res);
	npu_dev->bwmon_io.base = devm_ioremap(&pdev->dev, res->start,
					npu_dev->bwmon_io.size);
	if (unlikely(!npu_dev->bwmon_io.base)) {
		pr_err("unable to map bwmon\n");
		rc = -ENOMEM;
		goto error_get_dev_num;
	}
	pr_debug("bwmon phy address=0x%llx virt=%pK\n",
		res->start, npu_dev->bwmon_io.base);

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "qfprom_physical");
	if (!res) {
		pr_info("unable to get qfprom_physical resource\n");
	} else {
		npu_dev->qfprom_io.size = resource_size(res);
		npu_dev->qfprom_io.base = devm_ioremap(&pdev->dev, res->start,
					npu_dev->qfprom_io.size);
		if (unlikely(!npu_dev->qfprom_io.base)) {
			pr_err("unable to map qfprom_physical\n");
			rc = -ENOMEM;
			goto error_get_dev_num;
		}
		pr_debug("qfprom_physical phy address=0x%llx virt=%pK\n",
			res->start, npu_dev->qfprom_io.base);
	}
	if (npu_alloc_memory_region(npu_dev)) {
		rc = -ENOMEM;
		goto error_get_dev_num;
	}
	rc = npu_parse_dt_regulator(npu_dev);
	if (rc)
		goto error_get_dev_num;

	rc = npu_parse_dt_clock(npu_dev);
	if (rc)
		goto error_get_dev_num;

	//init interconnets path if exists in devicetree file
	npu_icc_init(npu_dev);

	rc = npu_hw_info_init(npu_dev);
	if (rc)
		goto error_get_dev_num;

	rc = npu_pwrctrl_init(npu_dev);
	if (rc)
		goto error_get_dev_num;

	rc = npu_thermalctrl_init(npu_dev);
	if (rc)
		goto error_get_dev_num;

	rc = npu_irq_init(npu_dev);
	if (rc)
		goto error_get_dev_num;

	rc = npu_mbox_init(npu_dev);
	if (rc)
		goto error_get_dev_num;

	/* character device might be optional */
	rc = alloc_chrdev_region(&npu_dev->dev_num, 0, 1, DRIVER_NAME);
	if (rc < 0) {
		pr_err("alloc_chrdev_region failed: %d\n", rc);
		goto error_get_dev_num;
	}

	npu_dev->class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(npu_dev->class)) {
		rc = PTR_ERR(npu_dev->class);
		pr_err("class_create failed: %d\n", rc);
		goto error_class_create;
	}

	npu_dev->device = device_create(npu_dev->class, NULL,
		npu_dev->dev_num, NULL, DRIVER_NAME);
	if (IS_ERR(npu_dev->device)) {
		rc = PTR_ERR(npu_dev->device);
		pr_err("device_create failed: %d\n", rc);
		goto error_class_device_create;
	}

	cdev_init(&npu_dev->cdev, &npu_fops);
	rc = cdev_add(&npu_dev->cdev,
			MKDEV(MAJOR(npu_dev->dev_num), 0), 1);
	if (rc < 0) {
		pr_err("cdev_add failed %d\n", rc);
		goto error_cdev_add;
	}
	dev_set_drvdata(npu_dev->device, npu_dev);
	pr_debug("drvdata %pK %pK\n", dev_get_drvdata(&pdev->dev),
		dev_get_drvdata(npu_dev->device));
	rc = sysfs_create_group(&npu_dev->device->kobj, &npu_fs_attr_group);
	if (rc) {
		pr_err("unable to register npu sysfs nodes\n");
		goto error_res_init;
	}

	if (IS_ENABLED(CONFIG_THERMAL)) {
		tcdev = thermal_of_cooling_device_register(pdev->dev.of_node,
							  "npu", npu_dev,
							  &npu_cooling_ops);
		if (IS_ERR(tcdev)) {
			dev_err(&pdev->dev,
				"npu: failed to register npu as cooling device\n");
			rc = PTR_ERR(tcdev);
			goto error_driver_init;
		}
		npu_dev->tcdev = tcdev;
	}

	rc = npu_cdsprm_cxlimit_init(npu_dev);
	if (rc)
		goto error_driver_init;
#ifdef CONFIG_DEBUG_FS
	npu_debugfs_init(npu_dev);
#endif
	rc = npu_host_init(npu_dev);
	if (rc) {
		pr_err("unable to init host\n");
		goto error_driver_init;
	}

	g_npu_dev = npu_dev;

	return rc;
error_driver_init:
	npu_cdsprm_cxlimit_deinit(npu_dev);
	if (npu_dev->tcdev)
		thermal_cooling_device_unregister(npu_dev->tcdev);
	sysfs_remove_group(&npu_dev->device->kobj, &npu_fs_attr_group);
error_res_init:
	cdev_del(&npu_dev->cdev);
error_cdev_add:
	device_destroy(npu_dev->class, npu_dev->dev_num);
error_class_device_create:
	class_destroy(npu_dev->class);
error_class_create:
	unregister_chrdev_region(npu_dev->dev_num, 1);
	npu_mbox_deinit(npu_dev);
error_get_dev_num:
	npu_icc_deinit(npu_dev);
	return rc;
}

static int npu_remove(struct platform_device *pdev)
{
	struct npu_device *npu_dev;

	npu_dev = platform_get_drvdata(pdev);
	npu_host_deinit(npu_dev);
	npu_debugfs_deinit(npu_dev);
	npu_cdsprm_cxlimit_deinit(npu_dev);
	if (npu_dev->tcdev)
		thermal_cooling_device_unregister(npu_dev->tcdev);
	sysfs_remove_group(&npu_dev->device->kobj, &npu_fs_attr_group);
	cdev_del(&npu_dev->cdev);
	device_destroy(npu_dev->class, npu_dev->dev_num);
	class_destroy(npu_dev->class);
	unregister_chrdev_region(npu_dev->dev_num, 1);
	platform_set_drvdata(pdev, NULL);
	npu_mbox_deinit(npu_dev);
	npu_icc_deinit(npu_dev);
	g_npu_dev = NULL;

	return 0;
}

/* -------------------------------------------------------------------------
 * Suspend/Resume
 * -------------------------------------------------------------------------
 */
#if defined(CONFIG_PM)
static int npu_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int npu_resume(struct platform_device *dev)
{
	return 0;
}
#endif

/* -------------------------------------------------------------------------
 * Module Entry Points
 * -------------------------------------------------------------------------
 */
static int __init npu_init(void)
{
	int rc;

	rc = platform_driver_register(&npu_driver);
	if (rc)
		pr_err("register failed %d\n", rc);
	return rc;
}

static void __exit npu_exit(void)
{
	platform_driver_unregister(&npu_driver);
}

module_init(npu_init);
module_exit(npu_exit);

MODULE_DEVICE_TABLE(of, npu_dt_match);
MODULE_DESCRIPTION("MSM NPU driver");
MODULE_LICENSE("GPL");
MODULE_INFO(intree, "Y");
