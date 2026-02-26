// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, STMicroelectronics - All Rights Reserved
 */

#include <linux/bus/stm32_firewall.h>
#include <linux/bus/stm32_firewall_device.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/tee_drv.h>
#include <linux/types.h>

enum stm32_dbg_profile {
	PERIPHERAL_DBG_PROFILE	= 0,
	HDP_DBG_PROFILE		= 1,
};

enum stm32_dbg_pta_command {
	/*
	 * PTA_CMD_GRANT_DBG_ACCESS - Verify the debug configuration against the given debug profile
	 * and grant access or not
	 *
	 * [in]     value[0].a  Debug profile to grant access to.
	 */
	PTA_CMD_GRANT_DBG_ACCESS,
};

/**
 * struct stm32_dbg_bus - OP-TEE based STM32 debug bus private data
 * @dev: STM32 debug bus device.
 * @ctx: OP-TEE context handler.
 */
struct stm32_dbg_bus {
	struct device *dev;
	struct tee_context *ctx;
};

/* Expect at most 1 instance of this driver */
static struct stm32_dbg_bus *stm32_dbg_bus_priv;

static int stm32_dbg_pta_open_session(u32 *id)
{
	struct tee_client_device *dbg_bus_dev = to_tee_client_device(stm32_dbg_bus_priv->dev);
	struct tee_ioctl_open_session_arg sess_arg;
	int ret;

	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &dbg_bus_dev->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;

	ret = tee_client_open_session(stm32_dbg_bus_priv->ctx, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret) {
		dev_err(stm32_dbg_bus_priv->dev, "Failed opening tee session, err: %#x\n",
			sess_arg.ret);
		return -EOPNOTSUPP;
	}

	*id = sess_arg.session;

	return 0;
}

static void stm32_dbg_pta_close_session(u32 id)
{
	tee_client_close_session(stm32_dbg_bus_priv->ctx, id);
}

static int stm32_dbg_bus_grant_access(struct stm32_firewall_controller *ctrl, u32 dbg_profile)
{
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[1] = {0};
	u32 session_id;
	int ret;

	if (dbg_profile != PERIPHERAL_DBG_PROFILE && dbg_profile != HDP_DBG_PROFILE)
		return -EOPNOTSUPP;

	ret = stm32_dbg_pta_open_session(&session_id);
	if (ret)
		return ret;

	inv_arg.func = PTA_CMD_GRANT_DBG_ACCESS;
	inv_arg.session = session_id;
	inv_arg.num_params = 1;
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = dbg_profile;

	ret = tee_client_invoke_func(stm32_dbg_bus_priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_dbg(stm32_dbg_bus_priv->dev,
			"When invoking function, err %x, TEE returns: %x\n", ret, inv_arg.ret);
		if (!ret)
			ret = -EACCES;
	}

	stm32_dbg_pta_close_session(session_id);

	return ret;
}

/* Implement mandatory release_access ops even if it does nothing*/
static void stm32_dbg_bus_release_access(struct stm32_firewall_controller *ctrl, u32 dbg_profile)
{
}

static int stm32_dbg_bus_plat_probe(struct platform_device *pdev)
{
	struct stm32_firewall_controller *dbg_controller;
	int ret;

	/* Defer if OP-TEE service is not yet available */
	if (!stm32_dbg_bus_priv)
		return -EPROBE_DEFER;

	dbg_controller = devm_kzalloc(&pdev->dev, sizeof(*dbg_controller), GFP_KERNEL);
	if (!dbg_controller)
		return dev_err_probe(&pdev->dev, -ENOMEM, "Couldn't allocate debug controller\n");

	dbg_controller->dev = &pdev->dev;
	dbg_controller->mmio = NULL;
	dbg_controller->name = dev_driver_string(dbg_controller->dev);
	dbg_controller->type = STM32_PERIPHERAL_FIREWALL;
	dbg_controller->grant_access = stm32_dbg_bus_grant_access;
	dbg_controller->release_access = stm32_dbg_bus_release_access;

	ret = stm32_firewall_controller_register(dbg_controller);
	if (ret) {
		dev_err(dbg_controller->dev, "Couldn't register as a firewall controller: %d", ret);
		return ret;
	}

	ret = stm32_firewall_populate_bus(dbg_controller);
	if (ret) {
		dev_err(dbg_controller->dev, "Couldn't populate debug bus: %d", ret);
		stm32_firewall_controller_unregister(dbg_controller);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret) {
		dev_err(dbg_controller->dev, "Couldn't populate the node: %d", ret);
		stm32_firewall_controller_unregister(dbg_controller);
		return ret;
	}

	return 0;
}

static const struct of_device_id stm32_dbg_bus_of_match[] = {
	{ .compatible = "st,stm32mp131-dbg-bus", },
	{ .compatible = "st,stm32mp151-dbg-bus", },
	{ },
};
MODULE_DEVICE_TABLE(of, stm32_dbg_bus_of_match);

static struct platform_driver stm32_dbg_bus_driver = {
	.probe = stm32_dbg_bus_plat_probe,
	.driver = {
		.name = "stm32-dbg-bus",
		.of_match_table = stm32_dbg_bus_of_match,
	},
};

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	return (ver->impl_id == TEE_IMPL_ID_OPTEE);
}

static void stm32_dbg_bus_remove(struct tee_client_device *tee_dev)
{
	tee_client_close_context(stm32_dbg_bus_priv->ctx);
	stm32_dbg_bus_priv = NULL;

	of_platform_depopulate(&tee_dev->dev);
}

static int stm32_dbg_bus_probe(struct tee_client_device *tee_dev)
{
	struct device *dev = &tee_dev->dev;
	struct stm32_dbg_bus *priv;
	int ret = 0;

	if (stm32_dbg_bus_priv)
		return dev_err_probe(dev, -EBUSY,
				     "A STM32 debug bus device is already initialized\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Open context with TEE driver */
	priv->ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR_OR_NULL(priv->ctx))
		return dev_err_probe(dev, PTR_ERR_OR_ZERO(priv->ctx), "Cannot open TEE context\n");

	stm32_dbg_bus_priv = priv;
	stm32_dbg_bus_priv->dev = dev;

	return ret;
}

static const struct tee_client_device_id optee_dbg_bus_id_table[] = {
	{UUID_INIT(0xdd05bc8b, 0x9f3b, 0x49f0,
		   0xb6, 0x49, 0x01, 0xaa, 0x10, 0xc1, 0xc2, 0x10)},
	{}
};

static struct tee_client_driver stm32_optee_dbg_bus_driver = {
	.id_table = optee_dbg_bus_id_table,
	.probe = stm32_dbg_bus_probe,
	.remove = stm32_dbg_bus_remove,
	.driver = {
		.name = "optee_dbg_bus",
	},
};

static void __exit stm32_optee_dbg_bus_driver_exit(void)
{
	platform_driver_unregister(&stm32_dbg_bus_driver);
	tee_client_driver_unregister(&stm32_optee_dbg_bus_driver);
}
module_exit(stm32_optee_dbg_bus_driver_exit);

static int __init stm32_optee_dbg_bus_driver_init(void)
{
	int err;

	err = tee_client_driver_register(&stm32_optee_dbg_bus_driver);
	if (err)
		return err;

	err = platform_driver_register(&stm32_dbg_bus_driver);
	if (err)
		tee_client_driver_unregister(&stm32_optee_dbg_bus_driver);

	return err;
}
module_init(stm32_optee_dbg_bus_driver_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gatien Chevallier <gatien.chevallier@foss.st.com>");
MODULE_DESCRIPTION("OP-TEE based STM32 debug access bus driver");
