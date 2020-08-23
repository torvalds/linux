// SPDX-License-Identifier: GPL-2.0-or-later
/* Marvell/Qlogic FastLinQ NIC driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include <linux/kernel.h>
#include <linux/qed/qed_if.h>
#include "qed.h"
#include "qed_devlink.h"

enum qed_devlink_param_id {
	QED_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	QED_DEVLINK_PARAM_ID_IWARP_CMT,
};

static int qed_dl_param_get(struct devlink *dl, u32 id,
			    struct devlink_param_gset_ctx *ctx)
{
	struct qed_devlink *qed_dl = devlink_priv(dl);
	struct qed_dev *cdev;

	cdev = qed_dl->cdev;
	ctx->val.vbool = cdev->iwarp_cmt;

	return 0;
}

static int qed_dl_param_set(struct devlink *dl, u32 id,
			    struct devlink_param_gset_ctx *ctx)
{
	struct qed_devlink *qed_dl = devlink_priv(dl);
	struct qed_dev *cdev;

	cdev = qed_dl->cdev;
	cdev->iwarp_cmt = ctx->val.vbool;

	return 0;
}

static const struct devlink_param qed_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(QED_DEVLINK_PARAM_ID_IWARP_CMT,
			     "iwarp_cmt", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     qed_dl_param_get, qed_dl_param_set, NULL),
};

static const struct devlink_ops qed_dl_ops;

struct devlink *qed_devlink_register(struct qed_dev *cdev)
{
	union devlink_param_value value;
	struct qed_devlink *qdevlink;
	struct devlink *dl;
	int rc;

	dl = devlink_alloc(&qed_dl_ops, sizeof(struct qed_devlink));
	if (!dl)
		return ERR_PTR(-ENOMEM);

	qdevlink = devlink_priv(dl);
	qdevlink->cdev = cdev;

	rc = devlink_register(dl, &cdev->pdev->dev);
	if (rc)
		goto err_free;

	rc = devlink_params_register(dl, qed_devlink_params,
				     ARRAY_SIZE(qed_devlink_params));
	if (rc)
		goto err_unregister;

	value.vbool = false;
	devlink_param_driverinit_value_set(dl,
					   QED_DEVLINK_PARAM_ID_IWARP_CMT,
					   value);

	devlink_params_publish(dl);
	cdev->iwarp_cmt = false;

	return dl;

err_unregister:
	devlink_unregister(dl);

err_free:
	devlink_free(dl);

	return ERR_PTR(rc);
}

void qed_devlink_unregister(struct devlink *devlink)
{
	if (!devlink)
		return;

	devlink_params_unregister(devlink, qed_devlink_params,
				  ARRAY_SIZE(qed_devlink_params));

	devlink_unregister(devlink);
	devlink_free(devlink);
}
