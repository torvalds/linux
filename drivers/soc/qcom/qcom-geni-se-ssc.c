// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/qcom-geni-se-common.h>
#include <linux/qcom_scm.h>
#include <linux/remoteproc/qcom_rproc.h>

#define SCM_LOAD_QUP_FW_ARG	0x7E7E7E7E
#define SCM_AUTH_GSI_QUP_PROC	0x13

#define NUM_LOG_PAGES 2

struct geni_se_ssc_device *geni_se_ssc_dev;

enum ssc_core_clks {
	SSC_CORE_CLK,
	SSC_CORE2X_CLK,
	SSC_NUM_CLKS
};

static ssize_t ssc_qup_state_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, sizeof(int), "%d\n",
				!geni_se_ssc_dev->ssr.is_ssr_down);
}

static DEVICE_ATTR_RO(ssc_qup_state);

struct geni_se_ssc_device *get_se_ssc_dev(void)
{
	return geni_se_ssc_dev;
}
EXPORT_SYMBOL_GPL(get_se_ssc_dev);

/*
 * geni_se_ssc_clk_on() - vote/unvote SSC QUP corex/2x clocks
 * @dev: Pointer to the SSC device structure.
 * @enable: boolean to enable or disable clocks
 *
 * Return: none
 */
void geni_se_ssc_clk_on(struct geni_se_ssc_device *dev, bool enable)
{
	int ret = 0;

	if (enable) {
		if (!atomic_read(&dev->is_ssc_clk_enabled)) {
			/* Enable core and core2x clock */
			ret = clk_bulk_prepare_enable(SSC_NUM_CLKS, dev->ssc_clks);
			if (ret) {
				GENI_SE_ERR(dev->log_ctx, false, NULL,
					    "%s: corex/2x clks enable failed ret:%d\n",
					    __func__, ret);
				return;
			}
			atomic_set(&dev->is_ssc_clk_enabled, 1);
		} else {
			GENI_SE_ERR(dev->log_ctx, false, NULL,
				    "%s: SSC corex/2x clks already enabled\n",
				    __func__);
		}
	} else {
		if (atomic_read(&dev->is_ssc_clk_enabled)) {
			/* Disable core and core2x clk */
			clk_bulk_disable_unprepare(SSC_NUM_CLKS, dev->ssc_clks);
			atomic_set(&dev->is_ssc_clk_enabled, 0);
		} else {
			GENI_SE_ERR(dev->log_ctx, false, NULL,
				    "%s: SSC corex/2x clks already disabled\n",
				    __func__);
		}
	}
}

/*
 * geni_se_ssc_clk_enable() - enable/disable SSC QUP corex/2x clocks
 * @rsc: Pointer to resource associated with the serial engine.
 * @enable: boolean to enable or disable clocks
 *
 * Return: none
 */
void geni_se_ssc_clk_enable(struct geni_se_rsc *rsc, bool enable)
{
	if (!rsc->rsc_ssr.ssr_enable)
		return;

	geni_se_ssc_clk_on(rsc->ssc_dev, enable);
}
EXPORT_SYMBOL_GPL(geni_se_ssc_clk_enable);

/*
 * geni_se_ssc_qup_down() - process the SSR Down notification
 * @dev: Pointer to SSC driver device structure.
 *
 * Return: none
 */
static void geni_se_ssc_qup_down(struct geni_se_ssc_device *dev)
{
	struct geni_se_rsc *rsc = NULL;

	dev->ssr.is_ssr_down = true;
	if (list_empty(&dev->ssr.active_list_head)) {
		GENI_SE_ERR(dev->log_ctx, false, NULL,
			    "%s: No Active usecase\n", __func__);
		return;
	}

	list_for_each_entry(rsc, &dev->ssr.active_list_head,
			    rsc_ssr.active_list) {
		rsc->rsc_ssr.force_suspend(rsc->ctrl_dev);
	}
	geni_se_ssc_clk_on(dev, false);
}

/*
 * geni_se_ssc_qup_up() - process the SSR Up notification
 * @dev: Pointer to SSC driver device structure.
 *
 * Return: none
 */
static void geni_se_ssc_qup_up(struct geni_se_ssc_device *dev)
{
	struct geni_se_rsc *rsc = NULL;
	int ret = 0;

	if (list_empty(&dev->ssr.active_list_head)) {
		GENI_SE_ERR(dev->log_ctx, false, NULL,
			    "%s: No Active usecase\n", __func__);
		return;
	}

	geni_se_ssc_clk_on(dev, true);

	if (!dev->ssr.is_ssr_down) {
		/*
		 * Since bootup of ADSP cause default SSR up,
		 * we need to ignore this very first time.
		 * Later ssr up and ssr down can continue as usual.
		 */
		GENI_SE_ERR(dev->log_ctx, false, NULL,
			    "%s: Ignore SSR up notification process\n",
			    __func__);
		return;
	}

	list_for_each_entry(rsc, &dev->ssr.active_list_head,
			    rsc_ssr.active_list) {
		ret = geni_se_resources_on(rsc->se_rsc);
		if (ret) {
			GENI_SE_ERR(dev->log_ctx, false, NULL,
				    "%s: geni_se_resources_on failed%d\n",
				    __func__, ret);
			return;
		}
	}

	/* Make SCM call, to load the TZ FW */
	ret = qcom_scm_restore_sec_cfg(SCM_AUTH_GSI_QUP_PROC,
				       SCM_LOAD_QUP_FW_ARG);
	if (ret) {
		GENI_SE_ERR(dev->log_ctx, false, NULL,
			    "Unable to load firmware after SSR\n");
		return;
	}

	list_for_each_entry(rsc, &dev->ssr.active_list_head,
			    rsc_ssr.active_list) {
		ret = geni_se_resources_off(rsc->se_rsc);
		if (ret) {
			GENI_SE_ERR(dev->log_ctx, false, NULL,
				    "%s: geni_se_resources_off failed%d\n",
				    __func__, ret);
			return;
		}
	}

	list_for_each_entry(rsc, &dev->ssr.active_list_head,
			    rsc_ssr.active_list) {
		rsc->rsc_ssr.force_resume(rsc->ctrl_dev);
	}

	dev->ssr.is_ssr_down = false;
}

/*
 * geni_se_ssr_notify_block() - process the SSR Down/Up notification
 * @n: Pointer to structure of notifier block
 * @code: notification code value
 * @_cmd: data for notifier block
 *
 * Return: 0 on success
 */
static int geni_se_ssr_notify_block(struct notifier_block *n,
				    unsigned long code, void *_cmd)
{
	struct ssc_qup_nb *ssc_qup_nb = container_of(n, struct ssc_qup_nb, nb);
	struct ssc_qup_ssr *ssr = container_of(ssc_qup_nb, struct ssc_qup_ssr,
					       ssc_qup_nb);
	struct geni_se_ssc_device *dev = container_of(ssr, struct geni_se_ssc_device,
						ssr);

	switch (code) {
	case QCOM_SSR_BEFORE_SHUTDOWN:
		GENI_SE_DBG(dev->log_ctx, false, NULL,
			    "SSR notification before power down\n");
		geni_se_ssc_qup_down(dev);
		break;
	case QCOM_SSR_AFTER_POWERUP:
		geni_se_ssc_qup_up(dev);
		GENI_SE_DBG(dev->log_ctx, false, NULL,
			    "SSR notification after power up\n");
		break;
	default:
		break;
	}

	return 0;
}

/*
 * geni_se_ssc_qup_ssr_reg() - to register with subsystem for
 * the SSR Down/Up notification
 * @dev: Pointer to SSC driver device structure.
 *
 * Return: 0 on success
 */
static int geni_se_ssc_qup_ssr_reg(struct geni_se_ssc_device *dev)
{
	dev->ssr.ssc_qup_nb.nb.notifier_call = geni_se_ssr_notify_block;
	dev->ssr.ssc_qup_nb.next = qcom_register_ssr_notifier(dev->ssr.subsys_name,
							      &dev->ssr.ssc_qup_nb.nb);

	if (IS_ERR_OR_NULL(dev->ssr.ssc_qup_nb.next)) {
		GENI_SE_ERR(dev->log_ctx, false, NULL,
			    "subsys_notif_register_notifier failed %ld\n",
			     PTR_ERR(dev->ssr.ssc_qup_nb.next));
		return PTR_ERR(dev->ssr.ssc_qup_nb.next);
	}

	GENI_SE_DBG(dev->log_ctx, false, NULL, "SSR registration done\n");

	return 0;
}

static int geni_se_ssc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	geni_se_ssc_dev = devm_kzalloc(dev, sizeof(*geni_se_ssc_dev), GFP_KERNEL);
	if (!geni_se_ssc_dev)
		return -ENOMEM;

	geni_se_ssc_dev->dev = dev;
	geni_se_ssc_dev->wrapper_dev = dev->parent;
	if (!geni_se_ssc_dev->wrapper_dev) {
		dev_err(&pdev->dev, "SSC SE Wrapper is NULL, deferring probe\n");
		return -EPROBE_DEFER;
	}

	geni_se_ssc_dev->log_ctx = ipc_log_context_create(NUM_LOG_PAGES,
							  dev_name(geni_se_ssc_dev->dev), 0);
	if (!geni_se_ssc_dev->log_ctx)
		dev_err(dev, "%s Failed to allocate log context\n", __func__);
	else
		GENI_SE_DBG(geni_se_ssc_dev->log_ctx, false, NULL,
			    "IPC log created successfully\n");

	ret = of_property_read_string(geni_se_ssc_dev->dev->of_node,
				      "qcom,subsys-name", &geni_se_ssc_dev->ssr.subsys_name);
	if (!ret) {
		geni_se_ssc_dev->ssc_clks = devm_kcalloc(dev, SSC_NUM_CLKS,
							 sizeof(*geni_se_ssc_dev->ssc_clks),
							 GFP_KERNEL);
		if (!geni_se_ssc_dev->ssc_clks) {
			ret = -ENOMEM;
			dev_err(dev, "%s: Unable to allocate memmory ret:%d\n",
				__func__, ret);
			goto err;
		}
		geni_se_ssc_dev->ssc_clks[SSC_CORE_CLK].id = "corex";
		geni_se_ssc_dev->ssc_clks[SSC_CORE2X_CLK].id = "core2x";
		ret = devm_clk_bulk_get(geni_se_ssc_dev->dev, SSC_NUM_CLKS,
					geni_se_ssc_dev->ssc_clks);
		if (ret) {
			dev_err(dev, "%s: Err getting core/2x clk:%d\n",
				__func__, ret);
			goto err;
		}
		atomic_set(&geni_se_ssc_dev->is_ssc_clk_enabled, 0);

		INIT_LIST_HEAD(&geni_se_ssc_dev->ssr.active_list_head);
		ret = geni_se_ssc_qup_ssr_reg(geni_se_ssc_dev);
		if (ret) {
			dev_err(dev, "Unable to register SSR notification\n");
			goto err;
		}

		ret = sysfs_create_file(&geni_se_ssc_dev->wrapper_dev->kobj,
					&dev_attr_ssc_qup_state.attr);
		if (ret)
			dev_err(dev, "Unable to create sysfs file\n");
	}

	dev_dbg(dev, "%s: SSC QUP Serial Engine probed\n", __func__);
	return 0;
err:
	if (geni_se_ssc_dev->log_ctx)
		ipc_log_context_destroy(geni_se_ssc_dev->log_ctx);

	return ret;
}

static const struct of_device_id geni_se_ssc_dt_match[] = {
	{ .compatible = "qcom,geni-se-ssc-qup",},
	{}
};
MODULE_DEVICE_TABLE(of, geni_se_ssc_dt_match);

static struct platform_driver geni_se_ssc_driver = {
	.driver = {
		.name = "geni_se_ssc_qup",
		.of_match_table = geni_se_ssc_dt_match,
	},
	.probe = geni_se_ssc_probe,
};
module_platform_driver(geni_se_ssc_driver);

MODULE_SOFTDEP("pre: rproc_qcom_common");
MODULE_DESCRIPTION("GENI SSC Serial Engine Driver");
MODULE_LICENSE("GPL");
