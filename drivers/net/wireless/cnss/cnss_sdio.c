// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "cnss_sdio:%s:%d:: " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/io.h>
#if IS_ENABLED(CONFIG_MSM_SUBSYSTEM_RESTART)
#include <soc/qcom/subsystem_restart.h>
#include <soc/qcom/subsystem_notif.h>
#endif
#include <soc/qcom/qcom_ramdump.h>
#include <soc/qcom/memory_dump.h>
#include <net/cnss.h>
#include "cnss_common.h"
#include <linux/pm_qos.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/panic_notifier.h>
#include <linux/remoteproc.h>

#define WLAN_VREG_NAME		"vdd-wlan"
#define WLAN_VREG_DSRC_NAME	"vdd-wlan-dsrc"
#define WLAN_VREG_IO_NAME	"vdd-wlan-io"
#define WLAN_VREG_XTAL_NAME	"vdd-wlan-xtal"
#define WLAN_GPIO_CAPTSF_NAME	"qcom,cap-tsf-gpio"

#define WLAN_VREG_IO_MAX	1800000
#define WLAN_VREG_IO_MIN	1800000
#define WLAN_VREG_XTAL_MAX	3465000
#define WLAN_VREG_XTAL_MIN	1620000
#define WLAN_VREG_XTAL_TYP	1800000
#define POWER_ON_DELAY		4

/* Values for Dynamic Ramdump Collection*/
#define CNSS_DUMP_FORMAT_VER	0x11
#define CNSS_DUMP_MAGIC_VER_V2	0x42445953
#define CNSS_DUMP_NAME		"CNSS_WLAN_SDIO"
#define CNSS_PINCTRL_SLEEP_STATE	"sleep"
#define CNSS_PINCTRL_ACTIVE_STATE	"active"

struct cnss_sdio_regulator {
	struct regulator *wlan_io;
	struct regulator *wlan_xtal;
	struct regulator *wlan_vreg;
	struct regulator *wlan_vreg_dsrc;
};

struct cnss_sdio_info {
	struct cnss_sdio_wlan_driver *wdrv;
	struct sdio_func *func;
	struct mmc_card *card;
	struct mmc_host *host;
	struct device *dev;
	const struct sdio_device_id *id;
	bool skip_wlan_en_toggle;
	bool cnss_hw_state;
	struct cnss_cap_tsf_info cap_tsf_info;
};

#if IS_ENABLED(CONFIG_MSM_SUBSYSTEM_RESTART)
struct cnss_ssr_info {
	struct subsys_device *subsys;
	struct subsys_desc subsysdesc;
	void *subsys_handle;
	struct ramdump_device *ramdump_dev;
	unsigned long ramdump_size;
	void *ramdump_addr;
	phys_addr_t ramdump_phys;
	struct msm_dump_data dump_data;
	bool ramdump_dynamic;
	char subsys_name[10];
};
#endif

struct cnss_ramdump_info {
	void *ramdump_dev;
	unsigned long ramdump_size;
	void *ramdump_va;
	phys_addr_t ramdump_pa;
	struct msm_dump_data dump_data;
	char subsys_name[10];
	bool ramdump_dynamic;
};

struct cnss_wlan_pinctrl_info {
	bool is_antenna_shared;
	struct pinctrl *pinctrl;
	struct pinctrl_state *sleep;
	struct pinctrl_state *active;
};

struct cnss_sdio_bus_bandwidth {
	struct msm_bus_scale_pdata *bus_scale_table;
	u32 bus_client;
	int current_bandwidth_vote;
};

static struct cnss_sdio_data {
	struct cnss_sdio_regulator regulator;
	struct platform_device *pdev;
	struct cnss_sdio_info cnss_sdio_info;
#if IS_ENABLED(CONFIG_MSM_SUBSYSTEM_RESTART)
	struct cnss_ssr_info ssr_info;
#endif
	struct pm_qos_request qos_request;
	struct cnss_wlan_pinctrl_info pinctrl_info;
	struct cnss_sdio_bus_bandwidth bus_bandwidth;
	struct cnss_dev_platform_ops platform_ops;
	struct notifier_block panic_nb;
	struct cnss_ramdump_info ramdump_info;
	struct rproc *rproc;
} *cnss_pdata;

#define WLAN_RECOVERY_DELAY 1
/* cnss sdio subsytem device name, required property */
#define CNSS_SUBSYS_NAME_KEY "subsys-name"

/* SDIO manufacturer ID and Codes */
#define MANUFACTURER_ID_AR6320_BASE	   0x500
#define MANUFACTURER_ID_QCA9377_BASE	   0x700
#define MANUFACTURER_ID_QCA9379_BASE	   0x800
#define MANUFACTURER_CODE		   0x271

static const struct sdio_device_id ar6k_id_table[] = {
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x0))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x1))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x2))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x3))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x4))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x5))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x6))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x7))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x8))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0x9))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0xA))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0xB))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0xC))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0xD))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0xE))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6320_BASE | 0xF))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x0))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x1))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x2))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x3))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x4))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x5))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x6))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x7))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x8))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0x9))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0xA))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0xB))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0xC))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0xD))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0xE))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9377_BASE | 0xF))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x0))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x1))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x2))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x3))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x4))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x5))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x6))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x7))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x8))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0x9))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0xA))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0xB))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0xC))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0xD))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0xE))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_QCA9379_BASE | 0xF))},
	{},
};
MODULE_DEVICE_TABLE(sdio, ar6k_id_table);

void cnss_sdio_request_pm_qos_type(int latency_type, u32 qos_val)
{
	if (!cnss_pdata)
		return;

	pr_debug("PM QoS value: %d\n", qos_val);
	cpu_latency_qos_add_request(&cnss_pdata->qos_request, qos_val);
}
EXPORT_SYMBOL_GPL(cnss_sdio_request_pm_qos_type);

int cnss_sdio_request_bus_bandwidth(int bandwidth)
{
	int ret = 0;
	struct cnss_sdio_bus_bandwidth *bus_bandwidth;

	if (!cnss_pdata)
		return -ENODEV;

	bus_bandwidth = &cnss_pdata->bus_bandwidth;
	if (!bus_bandwidth->bus_client)
		return -EINVAL;
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	switch (bandwidth) {
	case CNSS_BUS_WIDTH_NONE:
	case CNSS_BUS_WIDTH_LOW:
	case CNSS_BUS_WIDTH_MEDIUM:
	case CNSS_BUS_WIDTH_HIGH:
		ret = legacy_bus_client_update_request(bus_bandwidth->bus_client,
						       bandwidth);
		if (!ret) {
			bus_bandwidth->current_bandwidth_vote = bandwidth;
		} else {
			pr_debug("could not set bus bandwidth %d, ret = %d\n",
				 bandwidth, ret);
		}
		break;
	default:
		pr_debug("Invalid request %d\n", bandwidth);
		ret = -EINVAL;
	}
#endif
	return ret;
}

void cnss_sdio_request_pm_qos(u32 qos_val)
{
	if (!cnss_pdata)
		return;

	pr_debug("PM QoS value: %d\n", qos_val);
	cpu_latency_qos_add_request(&cnss_pdata->qos_request, qos_val);
}
EXPORT_SYMBOL_GPL(cnss_sdio_request_pm_qos);

void cnss_sdio_remove_pm_qos(void)
{
	if (!cnss_pdata)
		return;
	cpu_latency_qos_remove_request(&cnss_pdata->qos_request);
	pr_debug("PM QoS removed\n");
}
EXPORT_SYMBOL_GPL(cnss_sdio_remove_pm_qos);

static int cnss_put_hw_resources(struct device *dev)
{
	int ret = -EINVAL;
	struct cnss_sdio_info *info;
	struct mmc_host *host;

	if (!cnss_pdata)
		return ret;

	info = &cnss_pdata->cnss_sdio_info;

	if (info->skip_wlan_en_toggle) {
		pr_debug("HW doesn't support wlan toggling\n");
		return 0;
	}

	if (!info->cnss_hw_state) {
		pr_debug("HW resources are already released\n");
		return 0;
	}

	host = info->host;

	if (!host) {
		pr_err("MMC host is invalid\n");
		return ret;
	}
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	ret = mmc_power_save_host(host);
	if (ret) {
		pr_err("Failed to Power Save Host err:%d\n",
		       ret);
		return ret;
	}
#endif

	if (cnss_pdata->regulator.wlan_vreg)
		regulator_disable(cnss_pdata->regulator.wlan_vreg);
	else
		pr_debug("wlan_vreg regulator is invalid\n");

	info->cnss_hw_state = false;

	return 0;
}

static int cnss_get_hw_resources(struct device *dev)
{
	int ret = 0;
	struct mmc_host *host;
	struct cnss_sdio_info *info;

	if (!cnss_pdata)
		return ret;

	info = &cnss_pdata->cnss_sdio_info;

	if (info->skip_wlan_en_toggle) {
		pr_debug("HW doesn't support wlan toggling\n");
		return 0;
	}

	if (info->cnss_hw_state) {
		pr_debug("HW resources are already active\n");
		return 0;
	}

	host = info->host;

	if (!host) {
		pr_err("MMC Host is Invalid; Enumeration Failed\n");
		return ret;
	}

	if (cnss_pdata->regulator.wlan_vreg) {
		ret = regulator_enable(cnss_pdata->regulator.wlan_vreg);
		if (ret) {
			pr_err("Failed to enable wlan vreg\n");
			return ret;
		}
	} else {
		pr_debug("wlan_vreg regulator is invalid\n");
	}
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	ret = mmc_power_restore_host(host);
	if (ret) {
		pr_err("Failed to restore host power ret:%d\n",
		       ret);
		if (cnss_pdata->regulator.wlan_vreg)
			regulator_disable(cnss_pdata->regulator.wlan_vreg);
		return ret;
	}
#endif
	info->cnss_hw_state = true;
	return ret;
}

#if IS_ENABLED(CONFIG_MSM_SUBSYSTEM_RESTART)
static int cnss_sdio_shutdown(const struct subsys_desc *subsys, bool force_stop)
{
	struct cnss_sdio_info *cnss_info;
	struct cnss_sdio_wlan_driver *wdrv;
	int ret = 0;

	if (!cnss_pdata)
		return -ENODEV;
	cnss_info = &cnss_pdata->cnss_sdio_info;
	wdrv = cnss_info->wdrv;
	if (!wdrv)
		return 0;
	if (!wdrv->shutdown)
		return 0;
	wdrv->shutdown(cnss_info->func);
	ret = cnss_put_hw_resources(cnss_info->dev);
	if (ret)
		pr_err("Failed to put hw resources\n");
	return ret;
}

static int cnss_sdio_powerup(const struct subsys_desc *subsys)
{
	struct cnss_sdio_info *cnss_info;
	struct cnss_sdio_wlan_driver *wdrv;
	int ret = 0;

	if (!cnss_pdata)
		return -ENODEV;
	cnss_info = &cnss_pdata->cnss_sdio_info;
	wdrv = cnss_info->wdrv;
	if (!wdrv)
		return 0;
	if (!wdrv->reinit)
		return 0;
	ret = cnss_get_hw_resources(cnss_info->dev);
	if (ret) {
		pr_err("Failed to power up HW\n");
		return ret;
	}
	ret = wdrv->reinit(cnss_info->func, cnss_info->id);
	if (ret)
		pr_err("wlan reinit error=%d\n", ret);
	return ret;
}

static void cnss_sdio_crash_shutdown(const struct subsys_desc *subsys)
{
	struct cnss_sdio_info *cnss_info;
	struct cnss_sdio_wlan_driver *wdrv;

	if (!cnss_pdata)
		return;
	cnss_info = &cnss_pdata->cnss_sdio_info;
	wdrv = cnss_info->wdrv;
	if (wdrv && wdrv->crash_shutdown)
		wdrv->crash_shutdown(cnss_info->func);
}

static int cnss_sdio_ramdump(int enable, const struct subsys_desc *subsys)
{
	struct cnss_ssr_info *ssr_info;
	struct ramdump_segment segment;
	int ret;

	if (!cnss_pdata)
		return -ENODEV;

	if (!cnss_pdata->ssr_info.ramdump_size)
		return -ENOENT;

	if (!enable)
		return 0;
	ssr_info = &cnss_pdata->ssr_info;
	memset(&segment, 0, sizeof(segment));
	segment.v_address = ssr_info->ramdump_addr;
	segment.size = ssr_info->ramdump_size;
	ret = do_ramdump(ssr_info->ramdump_dev, &segment, 1);
	if (ret)
		pr_err("do_ramdump failed error=%d\n", ret);
	return ret;
}

static int cnss_subsys_init(void)
{
	struct cnss_ssr_info *ssr_info;
	int ret = 0;

	if (!cnss_pdata)
		return -ENODEV;

	ssr_info = &cnss_pdata->ssr_info;
	ssr_info->subsysdesc.name = ssr_info->subsys_name;
	ssr_info->subsysdesc.owner = THIS_MODULE;
	ssr_info->subsysdesc.shutdown = cnss_sdio_shutdown;
	ssr_info->subsysdesc.powerup = cnss_sdio_powerup;
	ssr_info->subsysdesc.ramdump = cnss_sdio_ramdump;
	ssr_info->subsysdesc.crash_shutdown = cnss_sdio_crash_shutdown;
	ssr_info->subsysdesc.dev = &cnss_pdata->pdev->dev;
	ssr_info->subsys = subsys_register(&ssr_info->subsysdesc);
	if (IS_ERR(ssr_info->subsys)) {
		ret = PTR_ERR(ssr_info->subsys);
		ssr_info->subsys = NULL;
		dev_err(&cnss_pdata->pdev->dev, "Failed to subsys_register error=%d\n",
			ret);
		goto err_subsys_reg;
	}
	ssr_info->subsys_handle = subsystem_get(ssr_info->subsysdesc.name);
	if (IS_ERR(ssr_info->subsys_handle)) {
		ret = PTR_ERR(ssr_info->subsys_handle);
		ssr_info->subsys_handle = NULL;
		dev_err(&cnss_pdata->pdev->dev, "Failed to subsystem_get error=%d\n",
			ret);
		goto err_subsys_get;
	}
	return 0;
err_subsys_get:
	subsys_unregister(ssr_info->subsys);
	ssr_info->subsys = NULL;
err_subsys_reg:
	return ret;
}

static void cnss_subsys_exit(void)
{
	struct cnss_ssr_info *ssr_info;

	if (!cnss_pdata)
		return;

	ssr_info = &cnss_pdata->ssr_info;
	if (ssr_info->subsys_handle)
		subsystem_put(ssr_info->subsys_handle);
	ssr_info->subsys_handle = NULL;
	if (ssr_info->subsys)
		subsys_unregister(ssr_info->subsys);
	ssr_info->subsys = NULL;
}

static void cnss_ramdump_cleanup(void)
{
	struct cnss_ssr_info *ssr_info;
	struct device *dev;

	if (!cnss_pdata)
		return;

	dev = &cnss_pdata->pdev->dev;
	ssr_info = &cnss_pdata->ssr_info;
	if (ssr_info->ramdump_addr) {
		if (ssr_info->ramdump_dynamic)
			dma_free_coherent(dev, ssr_info->ramdump_size,
					  ssr_info->ramdump_addr,
					  ssr_info->ramdump_phys);
		else
			iounmap(ssr_info->ramdump_addr);
	}

	ssr_info->ramdump_addr = NULL;
	if (ssr_info->ramdump_dev)
		destroy_ramdump_device(ssr_info->ramdump_dev);
	ssr_info->ramdump_dev = NULL;
}

void *cnss_sdio_get_virt_ramdump_mem(unsigned long *size)
{
	if (!cnss_pdata || !cnss_pdata->pdev)
		return NULL;

	*size = cnss_pdata->ssr_info.ramdump_size;

	return cnss_pdata->ssr_info.ramdump_addr;
}

void cnss_sdio_device_self_recovery(void)
{
	cnss_sdio_shutdown(NULL, false);
	msleep(WLAN_RECOVERY_DELAY);
	cnss_sdio_powerup(NULL);
}

void cnss_sdio_device_crashed(void)
{
	struct cnss_ssr_info *ssr_info;

	if (!cnss_pdata)
		return;
	ssr_info = &cnss_pdata->ssr_info;
	if (ssr_info->subsys) {
		subsys_set_crash_status(ssr_info->subsys, true);
		subsystem_restart_dev(ssr_info->subsys);
	}
}

static void cnss_sdio_recovery_work_handler(struct work_struct *recovery)
{
	cnss_sdio_device_self_recovery();
}

DECLARE_WORK(cnss_sdio_recovery_work, cnss_sdio_recovery_work_handler);

void cnss_sdio_schedule_recovery_work(void)
{
	schedule_work(&cnss_sdio_recovery_work);
}

/**
 * cnss_get_restart_level() - cnss get restart level API
 *
 * Wlan sdio function driver uses this API to get the current
 * subsystem restart level.
 *
 * Return: CNSS_RESET_SOC - "SYSTEM", restart system
 *         CNSS_RESET_SUBSYS_COUPLED - "RELATED",restart subsystem
 */
int cnss_get_restart_level(void)
{
	struct cnss_ssr_info *ssr_info;
	int level = 0;

	if (!cnss_pdata)
		return CNSS_RESET_SOC;
	ssr_info = &cnss_pdata->ssr_info;
	if (!ssr_info->subsys)
		return CNSS_RESET_SOC;

	level = subsys_get_restart_level(ssr_info->subsys);

	switch (level) {
	case RESET_SOC:
		return CNSS_RESET_SOC;
	case RESET_SUBSYS_COUPLED:
		return CNSS_RESET_SUBSYS_COUPLED;
	default:
		return CNSS_RESET_SOC;
	}
}
EXPORT_SYMBOL_GPL(cnss_get_restart_level);

#else

/**
 * cnss_get_restart_level() - cnss get restart level API
 *
 * Wlan sdio function driver uses this API to get the current
 * subsystem restart level.
 *
 * Return: CNSS_RESET_SOC - "SYSTEM", restart system
 *		 CNSS_RESET_SUBSYS_COUPLED - "RELATED",restart subsystem
 */

int cnss_get_restart_level(void)
{
	if (!cnss_pdata)
		return CNSS_RESET_SOC;
	if (cnss_pdata->rproc->recovery_disabled)
		return CNSS_RESET_SOC;
	else
		return CNSS_RESET_SUBSYS_COUPLED;
}
EXPORT_SYMBOL_GPL(cnss_get_restart_level);

static int cnss_sdio_shutdown(void)
{
	struct cnss_sdio_info *cnss_info;
	struct cnss_sdio_wlan_driver *wdrv;
	int ret = 0;

	if (!cnss_pdata)
		return -ENODEV;
	cnss_info = &cnss_pdata->cnss_sdio_info;
	wdrv = cnss_info->wdrv;
	if (!wdrv)
		return 0;
	if (!wdrv->shutdown)
		return 0;
	wdrv->shutdown(cnss_info->func);
	ret = cnss_put_hw_resources(cnss_info->dev);
	if (ret)
		pr_err("Failed to put hw resources\n");
	return ret;
}

static int cnss_sdio_powerup(void)
{
	struct cnss_sdio_info *cnss_info;
	struct cnss_sdio_wlan_driver *wdrv;
	int ret = 0;

	if (!cnss_pdata)
		return -ENODEV;
	cnss_info = &cnss_pdata->cnss_sdio_info;
	wdrv = cnss_info->wdrv;
	if (!wdrv)
		return 0;
	if (!wdrv->reinit)
		return 0;
	ret = cnss_get_hw_resources(cnss_info->dev);
	if (ret) {
		pr_err("Failed to power up HW\n");
		return ret;
	}
	ret = wdrv->reinit(cnss_info->func, cnss_info->id);
	if (ret)
		pr_err("wlan reinit error=%d\n", ret);
	return ret;
}

void cnss_sdio_device_self_recovery(void)
{
	cnss_sdio_shutdown();
	msleep(WLAN_RECOVERY_DELAY);
	cnss_sdio_powerup();
}

static void cnss_sdio_recovery_work_handler(struct work_struct *recovery)
{
	cnss_sdio_device_self_recovery();
}

DECLARE_WORK(cnss_sdio_recovery_work, cnss_sdio_recovery_work_handler);

void cnss_sdio_schedule_recovery_work(void)
{
	schedule_work(&cnss_sdio_recovery_work);
}

void cnss_sdio_device_crashed(void)
{
	if (!cnss_pdata)
		return;
	if (cnss_pdata->rproc->recovery_disabled)
		panic("subsys-restart: Resetting the SoC wlan crashed\n");
	cnss_sdio_schedule_recovery_work();
}

static void cnss_sdio_crash_shutdown(void)
{
	struct cnss_sdio_info *cnss_info;
	struct cnss_sdio_wlan_driver *wdrv;

	if (!cnss_pdata)
		return;

	cnss_info = &cnss_pdata->cnss_sdio_info;
	wdrv = cnss_info->wdrv;
	if (wdrv && wdrv->crash_shutdown)
		wdrv->crash_shutdown(cnss_info->func);
}

static int cnss_panic_handler(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	cnss_sdio_crash_shutdown();
	return NOTIFY_DONE;
}

static int cnss_sdio_up(struct rproc *rproc)
{
	return 0;
}

static int cnss_sdio_down(struct rproc *rproc)
{
	return 0;
}

int cnss_subsys_init(void)
{
	int ret;
	const char *name;
	struct device *dev;

	static const struct rproc_ops cnss_ops = {
		.start = cnss_sdio_up,
		.stop  = cnss_sdio_down,
	};

	const struct rproc_ops *ops = &cnss_ops;

	if (!cnss_pdata)
		return -ENODEV;

	dev = &cnss_pdata->pdev->dev;
	ret = of_property_read_string(dev->of_node, CNSS_SUBSYS_NAME_KEY,
				      &name);
	if (ret) {
		pr_err("cnss missing DT key '%s'\n", CNSS_SUBSYS_NAME_KEY);
		ret = -ENODEV;
	}

	cnss_pdata->panic_nb.notifier_call = cnss_panic_handler;
	ret = atomic_notifier_chain_register(&panic_notifier_list,
					     &cnss_pdata->panic_nb);
	if (ret) {
		pr_err("Failed to register panic handler\n");
		return -EINVAL;
	}
	cnss_pdata->rproc = rproc_alloc(&cnss_pdata->pdev->dev, name, ops, NULL, 0);
	if (!cnss_pdata->rproc) {
		pr_err("Failed to allocate rproc\n");
		return -ENOMEM;
	}
	cnss_pdata->rproc->recovery_disabled = true;
	ret = rproc_add(cnss_pdata->rproc);
	if (ret) {
		pr_info("rproc_add failed: %d\n", ret);
		goto free_rproc;
	}

	return 0;
free_rproc:
	rproc_free(cnss_pdata->rproc);
	return 0;
}

void cnss_subsys_exit(void)
{
	int ret;

	ret = atomic_notifier_chain_unregister(&panic_notifier_list,
					       &cnss_pdata->panic_nb);
	if (ret)
		pr_err("Failed to unregister panic handler\n");
	rproc_free(cnss_pdata->rproc);
}

static int cnss_configure_dump_table(struct cnss_ramdump_info *ramdump_info)
{
	struct msm_dump_entry dump_entry;
	int ret;

	ramdump_info->dump_data.addr = ramdump_info->ramdump_pa;
	ramdump_info->dump_data.len = ramdump_info->ramdump_size;
	ramdump_info->dump_data.version = CNSS_DUMP_FORMAT_VER;
	ramdump_info->dump_data.magic = CNSS_DUMP_MAGIC_VER_V2;
	strscpy(ramdump_info->dump_data.name, CNSS_DUMP_NAME,
		sizeof(ramdump_info->dump_data.name));

	dump_entry.id = MSM_DUMP_DATA_CNSS_WLAN;
	dump_entry.addr = virt_to_phys(&ramdump_info->dump_data);

	ret = msm_dump_data_register(MSM_DUMP_TABLE_APPS, &dump_entry);
	if (ret)
		pr_err("Dump table setup failed: %d\n", ret);

	return ret;
}

static int cnss_configure_ramdump(void)
{
	struct cnss_ramdump_info *ramdump_info;
	int ret = 0;
	struct resource *res;
	const char *name;
	u32 ramdump_size = 0;
	struct device *dev;

	if (!cnss_pdata)
		return -ENODEV;

	dev = &cnss_pdata->pdev->dev;
	ramdump_info = &cnss_pdata->ramdump_info;
	ret = of_property_read_string(dev->of_node, CNSS_SUBSYS_NAME_KEY,
				      &name);
	if (ret) {
		pr_err("cnss missing DT key '%s'\n", CNSS_SUBSYS_NAME_KEY);
		ret = -ENODEV;
		goto err_subsys_name_query;
	}
	strscpy(ramdump_info->subsys_name, name, sizeof(ramdump_info->subsys_name));
	if (of_property_read_u32(dev->of_node, "qcom,wlan-ramdump-dynamic",
				 &ramdump_size) == 0) {
		ramdump_info->ramdump_va  =
			dma_alloc_coherent(dev, ramdump_size,
					   &ramdump_info->ramdump_pa,
					   GFP_KERNEL);
		if (ramdump_info->ramdump_va)
			ramdump_info->ramdump_size = ramdump_size;
		ramdump_info->ramdump_dynamic = true;
	} else {
		res = platform_get_resource_byname(cnss_pdata->pdev,
						   IORESOURCE_MEM, "ramdump");
		if (res) {
			ramdump_info->ramdump_pa  = res->start;
			ramdump_size = resource_size(res);
			ramdump_info->ramdump_va = ioremap(ramdump_info->ramdump_pa,
							   ramdump_size);
			if (ramdump_info->ramdump_va)
				ramdump_info->ramdump_size = ramdump_size;
			ramdump_info->ramdump_dynamic = false;
		}
	}

	pr_info("ramdump addr: %p, phys: %pa subsys:'%s'\n",
		ramdump_info->ramdump_va, &ramdump_info->ramdump_pa,
		ramdump_info->subsys_name);

	if (ramdump_info->ramdump_size == 0) {
		pr_info("CNSS ramdump will not be collected\n");
		return 0;
	}

	if (ramdump_info->ramdump_dynamic) {
		ret = cnss_configure_dump_table(ramdump_info);
		if (ret)
			goto err_configure_dump_table;
	}

	ramdump_info->ramdump_dev = qcom_create_ramdump_device(ramdump_info->subsys_name,
							       dev);
	if (!ramdump_info->ramdump_dev) {
		ret = -ENOMEM;
		pr_err("ramdump dev create failed: error=%d\n", ret);
		goto err_configure_dump_table;
	}

	return 0;

err_configure_dump_table:
	if (ramdump_info->ramdump_dynamic)
		dma_free_coherent(dev, ramdump_info->ramdump_size,
				  ramdump_info->ramdump_va,
				  ramdump_info->ramdump_pa);
	else
		iounmap(ramdump_info->ramdump_va);

	ramdump_info->ramdump_va = NULL;
	ramdump_info->ramdump_size = 0;
err_subsys_name_query:
	return ret;
}

static void cnss_ramdump_cleanup(void)
{
	struct device *dev;
	struct cnss_ramdump_info *ramdump_info;

	if (!cnss_pdata)
		return;

	dev = &cnss_pdata->pdev->dev;
	ramdump_info = &cnss_pdata->ramdump_info;
	if (ramdump_info->ramdump_va) {
		if (ramdump_info->ramdump_dynamic)
			dma_free_coherent(dev, ramdump_info->ramdump_size,
					  ramdump_info->ramdump_va,
					  ramdump_info->ramdump_pa);
		else
			iounmap(ramdump_info->ramdump_va);
	}

	ramdump_info->ramdump_va = NULL;
	if (ramdump_info->ramdump_dev)
		qcom_destroy_ramdump_device(ramdump_info->ramdump_dev);
	ramdump_info->ramdump_dev = NULL;
}

void *cnss_sdio_get_virt_ramdump_mem(unsigned long *size)
{
	if (!cnss_pdata || !cnss_pdata->pdev)
		return NULL;

	*size = cnss_pdata->ramdump_info.ramdump_size;

	return cnss_pdata->ramdump_info.ramdump_va;
}
#endif

static inline int cnss_get_tsf_cap_irq(struct device *dev)
{
	int irq = -EINVAL;
	int gpio;

	if (!dev)
		return -ENODEV;

	gpio = of_get_named_gpio(dev->of_node, WLAN_GPIO_CAPTSF_NAME, 0);
	if (gpio >= 0)
		irq = gpio_to_irq(gpio);

	return irq;
}

static int cnss_sdio_register_tsf_captured_handler(irq_handler_t handler,
						   void *ctx)
{
	struct cnss_cap_tsf_info *tsf_info;

	if (!cnss_pdata)
		return -ENODEV;

	tsf_info = &cnss_pdata->cnss_sdio_info.cap_tsf_info;
	if (tsf_info->irq_num < 0)
		return -EOPNOTSUPP;

	tsf_info->irq_handler = handler;
	tsf_info->context = ctx;
	return 0;
}

static int cnss_sdio_unregister_tsf_captured_handler(void *ctx)
{
	struct cnss_cap_tsf_info *tsf_info;

	if (!cnss_pdata)
		return -ENODEV;

	tsf_info = &cnss_pdata->cnss_sdio_info.cap_tsf_info;
	if (tsf_info->irq_num < 0)
		return -EOPNOTSUPP;

	if (ctx == tsf_info->context) {
		tsf_info->irq_handler = NULL;
		tsf_info->context = NULL;
	}
	return 0;
}

static irqreturn_t cnss_sdio_tsf_captured_handler(int irq, void *ctx)
{
	struct cnss_cap_tsf_info *tsf_info;

	if (!cnss_pdata)
		return IRQ_HANDLED;

	tsf_info = &cnss_pdata->cnss_sdio_info.cap_tsf_info;
	if (tsf_info->irq_num < 0 || tsf_info->irq_num != irq ||
	    !tsf_info->irq_handler || !tsf_info->context)
		return IRQ_HANDLED;

	return tsf_info->irq_handler(irq, tsf_info->context);
}

static void cnss_sdio_tsf_init(struct device *dev,
			       struct cnss_cap_tsf_info *tsf_info)
{
	int ret, irq;

	tsf_info->irq_num = -EINVAL;
	tsf_info->irq_handler = NULL;
	tsf_info->context = NULL;

	irq = cnss_get_tsf_cap_irq(dev);
	if (irq < 0) {
		dev_err(dev, "%s: fail to get irq: %d\n", __func__, irq);
		return;
	}

	ret = request_irq(irq, cnss_sdio_tsf_captured_handler,
			  IRQF_SHARED | IRQF_TRIGGER_RISING, dev_name(dev),
			  (void *)tsf_info);
	dev_err(dev, "%s: request irq[%d] for dev: %s, result: %d\n",
		__func__, irq, dev_name(dev), ret);
	if (!ret)
		tsf_info->irq_num = irq;
}

static void cnss_sdio_tsf_deinit(struct cnss_cap_tsf_info *tsf_info)
{
	int irq = tsf_info->irq_num;

	if (irq < 0)
		return;

	free_irq(irq, (void *)tsf_info);

	tsf_info->irq_num = -EINVAL;
	tsf_info->irq_handler = NULL;
	tsf_info->context = NULL;
}

static void cnss_sdio_set_platform_ops(struct device *dev)
{
	struct cnss_dev_platform_ops *pf_ops = &cnss_pdata->platform_ops;

	pf_ops->power_up = cnss_sdio_power_up;
	pf_ops->power_down = cnss_sdio_power_down;
	pf_ops->device_crashed = cnss_sdio_device_crashed;

	pf_ops->get_virt_ramdump_mem = cnss_sdio_get_virt_ramdump_mem;
	pf_ops->device_self_recovery = cnss_sdio_device_self_recovery;
	pf_ops->schedule_recovery_work = cnss_sdio_schedule_recovery_work;
	pf_ops->get_wlan_mac_address = cnss_sdio_get_wlan_mac_address;
	pf_ops->set_wlan_mac_address = cnss_sdio_set_wlan_mac_address;
	pf_ops->request_bus_bandwidth = cnss_sdio_request_bus_bandwidth;
	pf_ops->register_tsf_captured_handler =
		cnss_sdio_register_tsf_captured_handler;
	pf_ops->unregister_tsf_captured_handler =
		cnss_sdio_unregister_tsf_captured_handler;
	dev->platform_data = pf_ops;
}

static int cnss_sdio_wlan_inserted(struct sdio_func *func,
				   const struct sdio_device_id *id)
{
	struct cnss_sdio_info *info;

	if (!cnss_pdata)
		return -ENODEV;

	info = &cnss_pdata->cnss_sdio_info;

	info->func = func;
	info->card = func->card;
	info->host = func->card->host;
	info->id = id;
	info->dev = &func->dev;
	cnss_sdio_set_platform_ops(info->dev);

	cnss_put_hw_resources(cnss_pdata->cnss_sdio_info.dev);

	pr_info("SDIO Device is Probed\n");
	return 0;
}

static void cnss_sdio_wlan_removed(struct sdio_func *func)
{
	struct cnss_sdio_info *info;

	if (!cnss_pdata)
		return;

	info = &cnss_pdata->cnss_sdio_info;

	info->host = NULL;
	info->card = NULL;
	info->func = NULL;
	info->id = NULL;
}

#if defined(CONFIG_PM)
static int cnss_sdio_wlan_suspend(struct device *dev)
{
	struct cnss_sdio_wlan_driver *wdrv;
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	struct cnss_sdio_bus_bandwidth *bus_bandwidth;
#endif
	struct sdio_func *func;

	int error = 0;

	if (!cnss_pdata)
		return -ENODEV;

#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	bus_bandwidth = &cnss_pdata->bus_bandwidth;
	if (bus_bandwidth->bus_client) {
		legacy_bus_client_update_request(bus_bandwidth->bus_client,
						 CNSS_BUS_WIDTH_NONE);
	}
#endif
	func = cnss_pdata->cnss_sdio_info.func;
	wdrv = cnss_pdata->cnss_sdio_info.wdrv;
	if (!wdrv) {
		/* This can happen when no wlan driver loaded (no register to
		 * platform driver).
		 */
		sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
		pr_debug("wlan driver not registered\n");
		return 0;
	}
	if (wdrv->suspend) {
		error = wdrv->suspend(dev);
		if (error)
			pr_err("wlan suspend failed error=%d\n", error);
	}

	return error;
}

static int cnss_sdio_wlan_resume(struct device *dev)
{
	struct cnss_sdio_wlan_driver *wdrv;
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	struct cnss_sdio_bus_bandwidth *bus_bandwidth;
#endif
	int error = 0;

	if (!cnss_pdata)
		return -ENODEV;

#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	bus_bandwidth = &cnss_pdata->bus_bandwidth;
	if (bus_bandwidth->bus_client) {
		legacy_bus_client_update_request(bus_bandwidth->bus_client,
						 bus_bandwidth->current_bandwidth_vote);
	}
#endif

	wdrv = cnss_pdata->cnss_sdio_info.wdrv;
	if (!wdrv) {
		/* This can happen when no wlan driver loaded (no register to
		 * platform driver).
		 */
		pr_debug("wlan driver not registered\n");
		return 0;
	}
	if (wdrv->resume) {
		error = wdrv->resume(dev);
		if (error)
			pr_err("wlan resume failed error=%d\n", error);
	}
	return error;
}
#endif

#if defined(CONFIG_PM)
static const struct dev_pm_ops cnss_ar6k_device_pm_ops = {
	.suspend = cnss_sdio_wlan_suspend,
	.resume = cnss_sdio_wlan_resume,
};
#endif /* CONFIG_PM */

static struct sdio_driver cnss_ar6k_driver = {
	.name = "cnss_ar6k_wlan",
	.id_table = ar6k_id_table,
	.probe = cnss_sdio_wlan_inserted,
	.remove = cnss_sdio_wlan_removed,
#if defined(CONFIG_PM)
	.drv = {
		.pm = &cnss_ar6k_device_pm_ops,
	}
#endif
};

static int cnss_set_pinctrl_state(struct cnss_sdio_data *pdata, bool state)
{
	struct cnss_wlan_pinctrl_info *info = &pdata->pinctrl_info;

	if (!info->is_antenna_shared)
		return 0;

	if (!info->pinctrl)
		return -EIO;

	return state ? pinctrl_select_state(info->pinctrl, info->active) :
		pinctrl_select_state(info->pinctrl, info->sleep);
}

int cnss_sdio_configure_spdt(bool state)
{
	if (!cnss_pdata)
		return -ENODEV;

	return cnss_set_pinctrl_state(cnss_pdata, state);
}
EXPORT_SYMBOL_GPL(cnss_sdio_configure_spdt);

/**
 * cnss_sdio_wlan_register_driver() - cnss wlan register API
 * @driver: sdio wlan driver interface from wlan driver.
 *
 * wlan sdio function driver uses this API to register callback
 * functions to cnss_sido platform driver. The callback will
 * be invoked by corresponding wrapper function of this cnss
 * platform driver.
 */
int cnss_sdio_wlan_register_driver(struct cnss_sdio_wlan_driver *driver)
{
	struct cnss_sdio_info *cnss_info;
	struct device *dev;
	int error = -EINVAL;

	if (!cnss_pdata)
		return -ENODEV;

	cnss_info = &cnss_pdata->cnss_sdio_info;
	dev = cnss_info->dev;

	if (cnss_info->wdrv) {
		pr_debug("wdrv already existed\n");
		return error;
	}

	if (!driver)
		return error;

	error = cnss_get_hw_resources(dev);
	if (error) {
		pr_err("Failed to restore power err:%d\n", error);
		return error;
	}

	error = cnss_set_pinctrl_state(cnss_pdata, PINCTRL_ACTIVE);
	if (error) {
		pr_err("Fail to set pinctrl to active state\n");
		cnss_put_hw_resources(dev);
		goto put_hw;
	}

	/* The HW resources are released in unregister logic if probe fails */
	error = driver->probe ? driver->probe(cnss_info->func,
						  cnss_info->id) : error;
	if (error) {
		pr_err("wlan probe failed error=%d\n", error);
		/**
		 * Check memory leak in skb pre-alloc memory pool
		 * Reset the skb memory pool
		 */
		goto pinctrl_sleep;
	}

	cnss_info->wdrv = driver;

	return error;

pinctrl_sleep:
	cnss_set_pinctrl_state(cnss_pdata, PINCTRL_SLEEP);
put_hw:
	return error;
}
EXPORT_SYMBOL_GPL(cnss_sdio_wlan_register_driver);

/**
 * cnss_sdio_wlan_unregister_driver() - cnss wlan unregister API
 * @driver: sdio wlan driver interface from wlan driver.
 *
 * wlan sdio function driver uses this API to detach it from cnss_sido
 * platform driver.
 */
void
cnss_sdio_wlan_unregister_driver(struct cnss_sdio_wlan_driver *driver)
{
	struct cnss_sdio_info *cnss_info;
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	struct cnss_sdio_bus_bandwidth *bus_bandwidth;
#endif

	if (!cnss_pdata)
		return;

#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	bus_bandwidth = &cnss_pdata->bus_bandwidth;
	if (bus_bandwidth->bus_client) {
		legacy_bus_client_update_request(bus_bandwidth->bus_client,
						 CNSS_BUS_WIDTH_NONE);
	}
#endif

	cnss_info = &cnss_pdata->cnss_sdio_info;
	if (!cnss_info->wdrv) {
		pr_err("driver not registered\n");
		return;
	}

	if (!driver)
		return;

	if (!driver->remove)
		return;

	driver->remove(cnss_info->func);

	cnss_info->wdrv = NULL;
	cnss_set_pinctrl_state(cnss_pdata, PINCTRL_SLEEP);
	cnss_put_hw_resources(cnss_info->dev);
}
EXPORT_SYMBOL_GPL(cnss_sdio_wlan_unregister_driver);

/**
 * cnss_wlan_query_oob_status() - cnss wlan query oob status API
 *
 * Wlan sdio function driver uses this API to check whether oob is
 * supported in platform driver.
 *
 * Return: 0 means oob is supported, others means unsupported.
 */
int cnss_wlan_query_oob_status(void)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_wlan_query_oob_status);

/**
 * cnss_wlan_register_oob_irq_handler() - cnss wlan register oob callback API
 * @handler: oob callback function pointer which registered to platform driver.
 * @pm_oob : parameter which registered to platform driver.
 *
 * Wlan sdio function driver uses this API to register oob callback
 * function to platform driver.
 *
 * Return: 0 means register successfully, others means failure.
 */
int cnss_wlan_register_oob_irq_handler(oob_irq_handler_t handler, void *pm_oob)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_wlan_register_oob_irq_handler);

/**
 * cnss_wlan_unregister_oob_irq_handler() - unregister oob callback API
 * @pm_oob: parameter which unregistered from platform driver.
 *
 * Wlan sdio function driver uses this API to unregister oob callback
 * function from platform driver.
 *
 * Return: 0 means unregister successfully, others means failure.
 */
int cnss_wlan_unregister_oob_irq_handler(void *pm_oob)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_wlan_unregister_oob_irq_handler);

static void cnss_sdio_reset_platform_ops(void)
{
	struct cnss_dev_platform_ops *pf_ops = &cnss_pdata->platform_ops;
	struct cnss_sdio_info *sdio_info = &cnss_pdata->cnss_sdio_info;

	memset(pf_ops, 0, sizeof(struct cnss_dev_platform_ops));
	if (sdio_info->dev)
		sdio_info->dev->platform_data = NULL;
}

static int cnss_sdio_wlan_init(void)
{
	int error = 0;

	error = sdio_register_driver(&cnss_ar6k_driver);
	if (error) {
		cnss_sdio_reset_platform_ops();
		pr_err("registered fail error=%d\n", error);
	} else {
		pr_debug("registered success\n");
	}
	return error;
}

static void cnss_sdio_wlan_exit(void)
{
	if (!cnss_pdata)
		return;

	cnss_sdio_reset_platform_ops();
	sdio_unregister_driver(&cnss_ar6k_driver);
}

static void cnss_sdio_deinit_bus_bandwidth(void)
{
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	struct cnss_sdio_bus_bandwidth *bus_bandwidth;

	bus_bandwidth = &cnss_pdata->bus_bandwidth;
	if (bus_bandwidth->bus_client) {
		legacy_bus_client_update_request(bus_bandwidth->bus_client,
						 CNSS_BUS_WIDTH_NONE);
		legacy_bus_unregister_client(bus_bandwidth->bus_client);
	}
#endif
}

static int cnss_sdio_configure_wlan_enable_regulator(void)
{
	int error;
	struct device *dev = &cnss_pdata->pdev->dev;

	if (of_get_property(cnss_pdata->pdev->dev.of_node,
			    WLAN_VREG_NAME "-supply", NULL)) {
		cnss_pdata->regulator.wlan_vreg =
			 regulator_get(&cnss_pdata->pdev->dev, WLAN_VREG_NAME);
		if (IS_ERR(cnss_pdata->regulator.wlan_vreg)) {
			error = PTR_ERR(cnss_pdata->regulator.wlan_vreg);
			dev_err(dev, "VDD-VREG get failed error=%d\n", error);
			return error;
		}

		error = regulator_enable(cnss_pdata->regulator.wlan_vreg);
		if (error) {
			dev_err(dev, "VDD-VREG enable failed error=%d\n",
				error);
			goto err_vdd_vreg_regulator;
		}
	}

	return 0;

err_vdd_vreg_regulator:
	regulator_put(cnss_pdata->regulator.wlan_vreg);

	return error;
}

static int cnss_sdio_configure_wlan_enable_dsrc_regulator(void)
{
	int error;
	struct device *dev = &cnss_pdata->pdev->dev;

	if (of_get_property(cnss_pdata->pdev->dev.of_node,
			    WLAN_VREG_DSRC_NAME "-supply", NULL)) {
		cnss_pdata->regulator.wlan_vreg_dsrc =
			regulator_get(&cnss_pdata->pdev->dev, WLAN_VREG_DSRC_NAME);
		if (IS_ERR(cnss_pdata->regulator.wlan_vreg_dsrc)) {
			error = PTR_ERR(cnss_pdata->regulator.wlan_vreg_dsrc);
			dev_err(dev, "VDD-VREG-DSRC get failed error=%d\n",
				error);
			return error;
		}

		error = regulator_enable(cnss_pdata->regulator.wlan_vreg_dsrc);
		if (error) {
			dev_err(dev, "VDD-VREG-DSRC enable failed error=%d\n",
				error);
			goto err_vdd_vreg_dsrc_regulator;
		}
	}

	return 0;

err_vdd_vreg_dsrc_regulator:
	regulator_put(cnss_pdata->regulator.wlan_vreg_dsrc);

	return error;
}

static int cnss_sdio_configure_regulator(void)
{
	int error;
	struct device *dev = &cnss_pdata->pdev->dev;
	u32 vdd_xtal_min;
	u32 vdd_xtal_max;

	if (of_get_property(cnss_pdata->pdev->dev.of_node,
			    WLAN_VREG_IO_NAME "-supply", NULL)) {
		cnss_pdata->regulator.wlan_io =
			   regulator_get(&cnss_pdata->pdev->dev, WLAN_VREG_IO_NAME);
		if (IS_ERR(cnss_pdata->regulator.wlan_io)) {
			error = PTR_ERR(cnss_pdata->regulator.wlan_io);
			dev_err(dev, "VDD-IO get failed error=%d\n", error);
			return error;
		}

		error = regulator_set_voltage(cnss_pdata->regulator.wlan_io,
					      WLAN_VREG_IO_MIN,
					      WLAN_VREG_IO_MAX);
		if (error) {
			dev_err(dev, "VDD-IO set failed error=%d\n", error);
			goto err_vdd_io_regulator;
		} else {
			error = regulator_enable(cnss_pdata->regulator.wlan_io);
			if (error) {
				dev_err(dev, "VDD-IO enable failed error=%d\n",
					error);
				goto err_vdd_io_regulator;
			}
		}
	}

	if (of_get_property(cnss_pdata->pdev->dev.of_node,
			    WLAN_VREG_XTAL_NAME "-supply", NULL)) {
		cnss_pdata->regulator.wlan_xtal =
			 regulator_get(&cnss_pdata->pdev->dev, WLAN_VREG_XTAL_NAME);
		if (IS_ERR(cnss_pdata->regulator.wlan_xtal)) {
			error = PTR_ERR(cnss_pdata->regulator.wlan_xtal);
			dev_err(dev, "VDD-XTAL get failed error=%d\n", error);
			goto err_vdd_xtal_regulator;
		}

		if (!of_property_read_u32(cnss_pdata->pdev->dev.of_node,
					  WLAN_VREG_XTAL_NAME "-min",
					  &vdd_xtal_min)) {
			if (vdd_xtal_min < WLAN_VREG_XTAL_MIN ||
			    vdd_xtal_min > WLAN_VREG_XTAL_MAX)
				vdd_xtal_min = WLAN_VREG_XTAL_TYP;
		} else {
			vdd_xtal_min = WLAN_VREG_XTAL_TYP;
		}

		if (!of_property_read_u32(cnss_pdata->pdev->dev.of_node,
					  WLAN_VREG_XTAL_NAME "-max",
					  &vdd_xtal_max)) {
			if (vdd_xtal_max < WLAN_VREG_XTAL_MIN ||
			    vdd_xtal_max > WLAN_VREG_XTAL_MAX)
				vdd_xtal_max = WLAN_VREG_XTAL_TYP;
		} else {
			vdd_xtal_max = WLAN_VREG_XTAL_TYP;
		}

		if (vdd_xtal_min > vdd_xtal_max)
			vdd_xtal_min = vdd_xtal_max;

		error = regulator_set_voltage(cnss_pdata->regulator.wlan_xtal,
					      vdd_xtal_min, vdd_xtal_max);
		if (error) {
			dev_err(dev, "VDD-XTAL set failed error=%d\n", error);
			goto err_vdd_xtal_regulator;
		} else {
			error =
				  regulator_enable(cnss_pdata->regulator.wlan_xtal);
			if (error) {
				dev_err(dev, "VDD-XTAL enable failed err=%d\n",
					error);
				goto err_vdd_xtal_regulator;
			}
		}
	}

	return 0;

err_vdd_xtal_regulator:
	regulator_put(cnss_pdata->regulator.wlan_xtal);
err_vdd_io_regulator:
	regulator_put(cnss_pdata->regulator.wlan_io);
	return error;
}

static void cnss_sdio_release_resource(void)
{
	if (cnss_pdata->regulator.wlan_xtal)
		regulator_put(cnss_pdata->regulator.wlan_xtal);
	if (cnss_pdata->regulator.wlan_vreg)
		regulator_put(cnss_pdata->regulator.wlan_vreg);
	if (cnss_pdata->regulator.wlan_io)
		regulator_put(cnss_pdata->regulator.wlan_io);
	if (cnss_pdata->regulator.wlan_vreg_dsrc)
		regulator_put(cnss_pdata->regulator.wlan_vreg_dsrc);
}

static int cnss_sdio_pinctrl_init(struct cnss_sdio_data *pdata,
				  struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct cnss_wlan_pinctrl_info *info = &pdata->pinctrl_info;

	if (!of_find_property(dev->of_node, "qcom,is-antenna-shared", NULL))
		return 0;

	info->is_antenna_shared = true;
	info->pinctrl = devm_pinctrl_get(dev);
	if ((IS_ERR_OR_NULL(info->pinctrl))) {
		dev_err(dev, "%s: Failed to get pinctrl\n", __func__);
		return PTR_ERR(info->pinctrl);
	}

	info->sleep = pinctrl_lookup_state(info->pinctrl,
					   CNSS_PINCTRL_SLEEP_STATE);
	if (IS_ERR_OR_NULL(info->sleep)) {
		dev_err(dev, "%s: Fail to get sleep state for pin\n", __func__);
		ret = PTR_ERR(info->sleep);
		goto release_pinctrl;
	}

	info->active = pinctrl_lookup_state(info->pinctrl,
					    CNSS_PINCTRL_ACTIVE_STATE);
	if (IS_ERR_OR_NULL(info->active)) {
		dev_err(dev, "%s: Fail to get active state for pin\n",
			__func__);
		ret = PTR_ERR(info->active);
		goto release_pinctrl;
	}

	ret = cnss_set_pinctrl_state(pdata, PINCTRL_SLEEP);

	if (ret) {
		dev_err(dev, "%s: Fail to set pin in sleep state\n", __func__);
		goto release_pinctrl;
	}

	return ret;

release_pinctrl:
	devm_pinctrl_put(info->pinctrl);
	info->is_antenna_shared = false;
	return ret;
}

static int cnss_sdio_init_bus_bandwidth(void)
{
	int ret = 0;
#ifdef CNSS_COMPLIE_ISSUE_FIX_LATER_IFNEEDED
	struct cnss_sdio_bus_bandwidth *bus_bandwidth;
	struct device *dev = &cnss_pdata->pdev->dev;

	bus_bandwidth = &cnss_pdata->bus_bandwidth;
	bus_bandwidth->bus_scale_table = msm_bus_cl_get_pdata(cnss_pdata->pdev);
	if (!bus_bandwidth->bus_scale_table) {
		dev_err(dev, "Failed to get the bus scale platform data\n");
		ret = -EINVAL;
	}

	bus_bandwidth->bus_client =
		  legacy_bus_register_client(bus_bandwidth->bus_scale_table);
	if (!bus_bandwidth->bus_client) {
		dev_err(dev, "Failed to register with bus_scale client\n");
		ret = -EINVAL;
	}
#endif
	return ret;
}

static int cnss_sdio_probe(struct platform_device *pdev)
{
	int error;
	struct device *dev = &pdev->dev;
	struct cnss_sdio_info *info;

	if (pdev->dev.of_node) {
		cnss_pdata = devm_kzalloc(&pdev->dev,
					  sizeof(*cnss_pdata), GFP_KERNEL);
		if (!cnss_pdata)
			return -ENOMEM;
	} else {
		cnss_pdata = pdev->dev.platform_data;
	}

	if (!cnss_pdata)
		return -EINVAL;

	cnss_pdata->pdev = pdev;
	info = &cnss_pdata->cnss_sdio_info;

	error = cnss_sdio_pinctrl_init(cnss_pdata, pdev);
	if (error) {
		dev_err(&pdev->dev, "Fail to configure pinctrl err:%d\n",
			error);
		return error;
	}

	error = cnss_sdio_configure_regulator();
	if (error) {
		dev_err(&pdev->dev, "Failed to configure voltage regulator error=%d\n",
			error);
		return error;
	}

	if (of_get_property(cnss_pdata->pdev->dev.of_node,
			    WLAN_VREG_NAME "-supply", NULL)) {
		error = cnss_sdio_configure_wlan_enable_regulator();
		if (error) {
			dev_err(&pdev->dev,
				"Failed to enable wlan enable regulator error=%d\n",
				error);
			goto err_wlan_enable_regulator;
		}
	}

	if (of_get_property(cnss_pdata->pdev->dev.of_node,
			    WLAN_VREG_DSRC_NAME "-supply", NULL)) {
		error = cnss_sdio_configure_wlan_enable_dsrc_regulator();
		if (error) {
			dev_err(&pdev->dev,
				"Failed to enable wlan dsrc enable regulator\n");
			goto err_wlan_dsrc_enable_regulator;
		}
	}

	info->skip_wlan_en_toggle = of_property_read_bool(dev->of_node,
							  "qcom,skip-wlan-en-toggle");
	info->cnss_hw_state = true;

	cnss_sdio_tsf_init(dev, &info->cap_tsf_info);

	error = cnss_sdio_wlan_init();
	if (error) {
		dev_err(&pdev->dev, "cnss wlan init failed error=%d\n", error);
		goto err_wlan_dsrc_enable_regulator;
	}

	error = cnss_configure_ramdump();
	if (error) {
		dev_err(&pdev->dev, "Failed to configure ramdump error=%d\n",
			error);
		goto err_ramdump_create;
	}

	error = cnss_subsys_init();
	if (error) {
		dev_err(&pdev->dev, "Failed to cnss_subsys_init error=%d\n",
			error);
		goto err_subsys_init;
	}

	if (of_property_read_bool(pdev->dev.of_node,
				  "qcom,cnss-enable-bus-bandwidth")) {
		error = cnss_sdio_init_bus_bandwidth();
		if (error) {
			dev_err(&pdev->dev, "Failed to init bus bandwidth\n");
			goto err_bus_bandwidth_init;
		}
	}
	dev_info(&pdev->dev, "CNSS SDIO Driver registered\n");
	return 0;

err_bus_bandwidth_init:
	cnss_subsys_exit();
err_subsys_init:
	cnss_ramdump_cleanup();
err_ramdump_create:
	cnss_sdio_wlan_exit();
err_wlan_dsrc_enable_regulator:
	info->cnss_hw_state = false;
	regulator_put(cnss_pdata->regulator.wlan_vreg_dsrc);
err_wlan_enable_regulator:
	regulator_put(cnss_pdata->regulator.wlan_xtal);
	regulator_put(cnss_pdata->regulator.wlan_io);
	cnss_pdata = NULL;
	return error;
}

static int cnss_sdio_remove(struct platform_device *pdev)
{
	struct cnss_sdio_info *info;
	struct cnss_cap_tsf_info *tsf_info;

	if (!cnss_pdata)
		return -ENODEV;

	info = &cnss_pdata->cnss_sdio_info;
	tsf_info = &info->cap_tsf_info;

	cnss_sdio_tsf_deinit(tsf_info);
	cnss_sdio_deinit_bus_bandwidth();
	cnss_sdio_wlan_exit();
	cnss_subsys_exit();
	cnss_ramdump_cleanup();
	cnss_put_hw_resources(info->dev);
	cnss_sdio_release_resource();
	cnss_pdata = NULL;
	return 0;
}

int cnss_sdio_set_wlan_mac_address(const u8 *in, u32 len)
{
	return 0;
}

u8 *cnss_sdio_get_wlan_mac_address(u32 *num)
{
	*num = 0;
	return NULL;
}

int cnss_sdio_power_down(struct device *dev)
{
	return 0;
}

int cnss_sdio_power_up(struct device *dev)
{
	return 0;
}

static const struct of_device_id cnss_sdio_dt_match[] = {
	{.compatible = "qcom,cnss_sdio"},
	{}
};
MODULE_DEVICE_TABLE(of, cnss_sdio_dt_match);

static struct platform_driver cnss_sdio_driver = {
	.probe  = cnss_sdio_probe,
	.remove = cnss_sdio_remove,
	.driver = {
		.name = "cnss_sdio",
		.of_match_table = cnss_sdio_dt_match,
	},
};

static int __init cnss_sdio_init(void)
{
	return platform_driver_register(&cnss_sdio_driver);
}

static void __exit cnss_sdio_exit(void)
{
	platform_driver_unregister(&cnss_sdio_driver);
}

module_init(cnss_sdio_init);
module_exit(cnss_sdio_exit);

MODULE_LICENSE("GPL");
