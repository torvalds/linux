// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026, Broadcom Corporation
 */

#include <linux/auxiliary_bus.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/fwctl.h>
#include <linux/bnxt/hsi.h>
#include <linux/bnxt/ulp.h>
#include <uapi/fwctl/fwctl.h>
#include <uapi/fwctl/bnxt.h>

struct bnxtctl_uctx {
	struct fwctl_uctx uctx;
	u32 uctx_caps;
};

struct bnxtctl_dev {
	struct fwctl_device fwctl;
	struct bnxt_aux_priv *aux_priv;
};

DEFINE_FREE(bnxtctl, struct bnxtctl_dev *, if (_T) fwctl_put(&_T->fwctl))

static int bnxtctl_open_uctx(struct fwctl_uctx *uctx)
{
	struct bnxtctl_uctx *bnxtctl_uctx =
		container_of(uctx, struct bnxtctl_uctx, uctx);

	bnxtctl_uctx->uctx_caps = BIT(FWCTL_BNXT_INLINE_COMMANDS) |
				  BIT(FWCTL_BNXT_QUERY_COMMANDS) |
				  BIT(FWCTL_BNXT_SEND_COMMANDS);
	return 0;
}

static void bnxtctl_close_uctx(struct fwctl_uctx *uctx)
{
}

static void *bnxtctl_info(struct fwctl_uctx *uctx, size_t *length)
{
	struct bnxtctl_uctx *bnxtctl_uctx =
		container_of(uctx, struct bnxtctl_uctx, uctx);
	struct fwctl_info_bnxt *info;

	info = kzalloc_obj(*info);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->uctx_caps = bnxtctl_uctx->uctx_caps;

	*length = sizeof(*info);
	return info;
}

/* Caller must hold edev->en_dev_lock */
static bool bnxtctl_validate_rpc(struct bnxt_en_dev *edev,
				 struct bnxt_fw_msg *hwrm_in,
				 enum fwctl_rpc_scope scope)
{
	struct input *req = (struct input *)hwrm_in->msg;

	lockdep_assert_held(&edev->en_dev_lock);
	if (edev->flags & BNXT_EN_FLAG_ULP_STOPPED)
		return false;

	switch (le16_to_cpu(req->req_type)) {
	case HWRM_FUNC_RESET:
	case HWRM_PORT_CLR_STATS:
	case HWRM_FW_RESET:
	case HWRM_FW_SYNC:
	case HWRM_FW_SET_TIME:
	case HWRM_DBG_LOG_BUFFER_FLUSH:
	case HWRM_DBG_ERASE_NVM:
	case HWRM_DBG_CFG:
	case HWRM_NVM_DEFRAG:
	case HWRM_NVM_FACTORY_DEFAULTS:
	case HWRM_NVM_FLUSH:
	case HWRM_NVM_VERIFY_UPDATE:
	case HWRM_NVM_ERASE_DIR_ENTRY:
	case HWRM_NVM_MOD_DIR_ENTRY:
	case HWRM_NVM_FIND_DIR_ENTRY:
		return scope >= FWCTL_RPC_CONFIGURATION;

	case HWRM_VER_GET:
	case HWRM_ERROR_RECOVERY_QCFG:
	case HWRM_FUNC_QCAPS:
	case HWRM_FUNC_QCFG:
	case HWRM_FUNC_QSTATS:
	case HWRM_PORT_PHY_QCFG:
	case HWRM_PORT_MAC_QCFG:
	case HWRM_PORT_PHY_QCAPS:
	case HWRM_PORT_PHY_I2C_READ:
	case HWRM_PORT_PHY_MDIO_READ:
	case HWRM_QUEUE_PRI2COS_QCFG:
	case HWRM_QUEUE_COS2BW_QCFG:
	case HWRM_VNIC_RSS_QCFG:
	case HWRM_QUEUE_GLOBAL_QCFG:
	case HWRM_QUEUE_ADPTV_QOS_RX_FEATURE_QCFG:
	case HWRM_QUEUE_ADPTV_QOS_TX_FEATURE_QCFG:
	case HWRM_QUEUE_QCAPS:
	case HWRM_QUEUE_ADPTV_QOS_RX_TUNING_QCFG:
	case HWRM_QUEUE_ADPTV_QOS_TX_TUNING_QCFG:
	case HWRM_TUNNEL_DST_PORT_QUERY:
	case HWRM_PORT_TX_FIR_QCFG:
	case HWRM_FW_LIVEPATCH_QUERY:
	case HWRM_FW_QSTATUS:
	case HWRM_FW_HEALTH_CHECK:
	case HWRM_FW_GET_TIME:
	case HWRM_PORT_EP_TX_QCFG:
	case HWRM_PORT_QCFG:
	case HWRM_PORT_MAC_QCAPS:
	case HWRM_TEMP_MONITOR_QUERY:
	case HWRM_REG_POWER_QUERY:
	case HWRM_CORE_FREQUENCY_QUERY:
	case HWRM_CFA_REDIRECT_QUERY_TUNNEL_TYPE:
	case HWRM_CFA_ADV_FLOW_MGNT_QCAPS:
	case HWRM_FUNC_RESOURCE_QCAPS:
	case HWRM_FUNC_BACKING_STORE_QCAPS:
	case HWRM_FUNC_BACKING_STORE_QCFG:
	case HWRM_FUNC_QSTATS_EXT:
	case HWRM_FUNC_PTP_PIN_QCFG:
	case HWRM_FUNC_PTP_EXT_QCFG:
	case HWRM_FUNC_BACKING_STORE_QCFG_V2:
	case HWRM_FUNC_BACKING_STORE_QCAPS_V2:
	case HWRM_FUNC_SYNCE_QCFG:
	case HWRM_FUNC_TTX_PACING_RATE_PROF_QUERY:
	case HWRM_PORT_PHY_FDRSTAT:
	case HWRM_DBG_RING_INFO_GET:
	case HWRM_DBG_QCAPS:
	case HWRM_DBG_QCFG:
	case HWRM_DBG_USEQ_FLUSH:
	case HWRM_DBG_USEQ_QCAPS:
	case HWRM_DBG_SIM_CABLE_STATE:
	case HWRM_DBG_TOKEN_QUERY_AUTH_IDS:
	case HWRM_NVM_GET_DEV_INFO:
	case HWRM_NVM_GET_DIR_INFO:
	case HWRM_SELFTEST_QLIST:
		return scope >= FWCTL_RPC_DEBUG_READ_ONLY;

	case HWRM_PORT_PHY_I2C_WRITE:
	case HWRM_PORT_PHY_MDIO_WRITE:
		return scope >= FWCTL_RPC_DEBUG_WRITE;

	default:
		return false;
	}
}

#define BNXTCTL_HWRM_CMD_TIMEOUT_DFLT	500	/* ms */
#define BNXTCTL_HWRM_CMD_TIMEOUT_MEDM	2000	/* ms */
#define BNXTCTL_HWRM_CMD_TIMEOUT_LONG	60000	/* ms */

static unsigned int bnxtctl_get_timeout(struct input *req)
{
	switch (le16_to_cpu(req->req_type)) {
	case HWRM_NVM_DEFRAG:
	case HWRM_NVM_FACTORY_DEFAULTS:
	case HWRM_NVM_FLUSH:
	case HWRM_NVM_VERIFY_UPDATE:
	case HWRM_NVM_ERASE_DIR_ENTRY:
	case HWRM_NVM_MOD_DIR_ENTRY:
		return BNXTCTL_HWRM_CMD_TIMEOUT_LONG;
	case HWRM_FUNC_RESET:
		return BNXTCTL_HWRM_CMD_TIMEOUT_MEDM;
	default:
		return BNXTCTL_HWRM_CMD_TIMEOUT_DFLT;
	}
}

static void *bnxtctl_fw_rpc(struct fwctl_uctx *uctx,
			    enum fwctl_rpc_scope scope,
			    void *in, size_t in_len, size_t *out_len)
{
	struct bnxtctl_dev *bnxtctl =
		container_of(uctx->fwctl, struct bnxtctl_dev, fwctl);
	struct bnxt_en_dev *edev = bnxtctl->aux_priv->edev;
	struct bnxt_fw_msg rpc_in = {0};
	int rc;

	if (in_len < sizeof(struct input) || in_len > HWRM_MAX_REQ_LEN)
		return ERR_PTR(-EINVAL);

	if (*out_len < sizeof(struct output))
		return ERR_PTR(-EINVAL);

	rpc_in.msg = in;
	rpc_in.msg_len = in_len;
	rpc_in.resp = kzalloc(*out_len, GFP_KERNEL);
	if (!rpc_in.resp)
		return ERR_PTR(-ENOMEM);

	rpc_in.resp_max_len = *out_len;
	rpc_in.timeout = bnxtctl_get_timeout(in);

	guard(mutex)(&edev->en_dev_lock);

	if (!bnxtctl_validate_rpc(edev, &rpc_in, scope)) {
		kfree(rpc_in.resp);
		return ERR_PTR(-EPERM);
	}

	rc = bnxt_send_msg(edev, &rpc_in);
	if (rc) {
		struct output *resp = rpc_in.resp;

		/* Copy the response to user always, as it contains
		 * detailed status of the command failure
		 */
		if (!resp->error_code)
			/* bnxt_send_msg() returned much before FW
			 * received the command.
			 */
			resp->error_code = cpu_to_le16(rc);
	}

	return rpc_in.resp;
}

static const struct fwctl_ops bnxtctl_ops = {
	.device_type = FWCTL_DEVICE_TYPE_BNXT,
	.uctx_size = sizeof(struct bnxtctl_uctx),
	.open_uctx = bnxtctl_open_uctx,
	.close_uctx = bnxtctl_close_uctx,
	.info = bnxtctl_info,
	.fw_rpc = bnxtctl_fw_rpc,
};

static int bnxtctl_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id)
{
	struct bnxt_aux_priv *aux_priv =
		container_of(adev, struct bnxt_aux_priv, aux_dev);
	struct bnxtctl_dev *bnxtctl __free(bnxtctl) =
		fwctl_alloc_device(&aux_priv->edev->pdev->dev, &bnxtctl_ops,
				   struct bnxtctl_dev, fwctl);
	int rc;

	if (!bnxtctl)
		return -ENOMEM;

	bnxtctl->aux_priv = aux_priv;

	rc = fwctl_register(&bnxtctl->fwctl);
	if (rc)
		return rc;

	auxiliary_set_drvdata(adev, no_free_ptr(bnxtctl));
	return 0;
}

static void bnxtctl_remove(struct auxiliary_device *adev)
{
	struct bnxtctl_dev *ctldev = auxiliary_get_drvdata(adev);

	fwctl_unregister(&ctldev->fwctl);
	fwctl_put(&ctldev->fwctl);
}

static const struct auxiliary_device_id bnxtctl_id_table[] = {
	{ .name = "bnxt_en.fwctl", },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, bnxtctl_id_table);

static struct auxiliary_driver bnxtctl_driver = {
	.name = "bnxt_fwctl",
	.probe = bnxtctl_probe,
	.remove = bnxtctl_remove,
	.id_table = bnxtctl_id_table,
};

module_auxiliary_driver(bnxtctl_driver);

MODULE_IMPORT_NS("FWCTL");
MODULE_DESCRIPTION("BNXT fwctl driver");
MODULE_AUTHOR("Pavan Chebbi <pavan.chebbi@broadcom.com>");
MODULE_AUTHOR("Andy Gospodarek <gospo@broadcom.com>");
MODULE_LICENSE("GPL");
