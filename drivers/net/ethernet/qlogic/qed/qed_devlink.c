// SPDX-License-Identifier: GPL-2.0-or-later
/* Marvell/Qlogic FastLinQ NIC driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include <linux/kernel.h>
#include <linux/qed/qed_if.h>
#include <linux/vmalloc.h>
#include "qed.h"
#include "qed_devlink.h"

enum qed_devlink_param_id {
	QED_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	QED_DEVLINK_PARAM_ID_IWARP_CMT,
};

struct qed_fw_fatal_ctx {
	enum qed_hw_err_type err_type;
};

int qed_report_fatal_error(struct devlink *devlink, enum qed_hw_err_type err_type)
{
	struct qed_devlink *qdl = devlink_priv(devlink);
	struct qed_fw_fatal_ctx fw_fatal_ctx = {
		.err_type = err_type,
	};

	if (qdl->fw_reporter)
		devlink_health_report(qdl->fw_reporter,
				      "Fatal error occurred", &fw_fatal_ctx);

	return 0;
}

static int
qed_fw_fatal_reporter_dump(struct devlink_health_reporter *reporter,
			   struct devlink_fmsg *fmsg, void *priv_ctx,
			   struct netlink_ext_ack *extack)
{
	struct qed_devlink *qdl = devlink_health_reporter_priv(reporter);
	struct qed_fw_fatal_ctx *fw_fatal_ctx = priv_ctx;
	struct qed_dev *cdev = qdl->cdev;
	u32 dbg_data_buf_size;
	u8 *p_dbg_data_buf;
	int err;

	/* Having context means that was a dump request after fatal,
	 * so we enable extra debugging while gathering the dump,
	 * just in case
	 */
	cdev->print_dbg_data = fw_fatal_ctx ? true : false;

	dbg_data_buf_size = qed_dbg_all_data_size(cdev);
	p_dbg_data_buf = vzalloc(dbg_data_buf_size);
	if (!p_dbg_data_buf) {
		DP_NOTICE(cdev,
			  "Failed to allocate memory for a debug data buffer\n");
		return -ENOMEM;
	}

	err = qed_dbg_all_data(cdev, p_dbg_data_buf);
	if (err) {
		DP_NOTICE(cdev, "Failed to obtain debug data\n");
		vfree(p_dbg_data_buf);
		return err;
	}

	err = devlink_fmsg_binary_pair_put(fmsg, "dump_data",
					   p_dbg_data_buf, dbg_data_buf_size);

	vfree(p_dbg_data_buf);

	return err;
}

static int
qed_fw_fatal_reporter_recover(struct devlink_health_reporter *reporter,
			      void *priv_ctx,
			      struct netlink_ext_ack *extack)
{
	struct qed_devlink *qdl = devlink_health_reporter_priv(reporter);
	struct qed_dev *cdev = qdl->cdev;

	qed_recovery_process(cdev);

	return 0;
}

static const struct devlink_health_reporter_ops qed_fw_fatal_reporter_ops = {
		.name = "fw_fatal",
		.recover = qed_fw_fatal_reporter_recover,
		.dump = qed_fw_fatal_reporter_dump,
};

#define QED_REPORTER_FW_GRACEFUL_PERIOD 1200000

void qed_fw_reporters_create(struct devlink *devlink)
{
	struct qed_devlink *dl = devlink_priv(devlink);

	dl->fw_reporter = devlink_health_reporter_create(devlink, &qed_fw_fatal_reporter_ops,
							 QED_REPORTER_FW_GRACEFUL_PERIOD, dl);
	if (IS_ERR(dl->fw_reporter)) {
		DP_NOTICE(dl->cdev, "Failed to create fw reporter, err = %ld\n",
			  PTR_ERR(dl->fw_reporter));
		dl->fw_reporter = NULL;
	}
}

void qed_fw_reporters_destroy(struct devlink *devlink)
{
	struct qed_devlink *dl = devlink_priv(devlink);
	struct devlink_health_reporter *rep;

	rep = dl->fw_reporter;

	if (!IS_ERR_OR_NULL(rep))
		devlink_health_reporter_destroy(rep);
}

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

static int qed_devlink_info_get(struct devlink *devlink,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct qed_devlink *qed_dl = devlink_priv(devlink);
	struct qed_dev *cdev = qed_dl->cdev;
	struct qed_dev_info *dev_info;
	char buf[100];
	int err;

	dev_info = &cdev->common_dev_info;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	memcpy(buf, cdev->hwfns[0].hw_info.part_num, sizeof(cdev->hwfns[0].hw_info.part_num));
	buf[sizeof(cdev->hwfns[0].hw_info.part_num)] = 0;

	if (buf[0]) {
		err = devlink_info_board_serial_number_put(req, buf);
		if (err)
			return err;
	}

	snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
		 GET_MFW_FIELD(dev_info->mfw_rev, QED_MFW_VERSION_3),
		 GET_MFW_FIELD(dev_info->mfw_rev, QED_MFW_VERSION_2),
		 GET_MFW_FIELD(dev_info->mfw_rev, QED_MFW_VERSION_1),
		 GET_MFW_FIELD(dev_info->mfw_rev, QED_MFW_VERSION_0));

	err = devlink_info_version_stored_put(req,
					      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, buf);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
		 dev_info->fw_major,
		 dev_info->fw_minor,
		 dev_info->fw_rev,
		 dev_info->fw_eng);

	return devlink_info_version_running_put(req,
						DEVLINK_INFO_VERSION_GENERIC_FW_APP, buf);
}

static const struct devlink_ops qed_dl_ops = {
	.info_get = qed_devlink_info_get,
};

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

	qed_fw_reporters_create(dl);

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

	qed_fw_reporters_destroy(devlink);

	devlink_params_unregister(devlink, qed_devlink_params,
				  ARRAY_SIZE(qed_devlink_params));

	devlink_unregister(devlink);
	devlink_free(devlink);
}
