// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/slimbus.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/soc/qcom/pdr.h>
#include <net/sock.h>
#include "slimbus.h"

/* NGD (Non-ported Generic Device) registers */
#define	NGD_CFG			0x0
#define	NGD_CFG_ENABLE		BIT(0)
#define	NGD_CFG_RX_MSGQ_EN	BIT(1)
#define	NGD_CFG_TX_MSGQ_EN	BIT(2)
#define	NGD_STATUS		0x4
#define NGD_LADDR		BIT(1)
#define	NGD_RX_MSGQ_CFG		0x8
#define	NGD_INT_EN		0x10
#define	NGD_INT_RECFG_DONE	BIT(24)
#define	NGD_INT_TX_NACKED_2	BIT(25)
#define	NGD_INT_MSG_BUF_CONTE	BIT(26)
#define	NGD_INT_MSG_TX_INVAL	BIT(27)
#define	NGD_INT_IE_VE_CHG	BIT(28)
#define	NGD_INT_DEV_ERR		BIT(29)
#define	NGD_INT_RX_MSG_RCVD	BIT(30)
#define	NGD_INT_TX_MSG_SENT	BIT(31)
#define	NGD_INT_STAT		0x14
#define	NGD_INT_CLR		0x18
#define DEF_NGD_INT_MASK (NGD_INT_TX_NACKED_2 | NGD_INT_MSG_BUF_CONTE | \
				NGD_INT_MSG_TX_INVAL | NGD_INT_IE_VE_CHG | \
				NGD_INT_DEV_ERR | NGD_INT_TX_MSG_SENT | \
				NGD_INT_RX_MSG_RCVD)

/* Slimbus QMI service */
#define SLIMBUS_QMI_SVC_ID	0x0301
#define SLIMBUS_QMI_SVC_V1	1
#define SLIMBUS_QMI_INS_ID	0
#define SLIMBUS_QMI_SELECT_INSTANCE_REQ_V01	0x0020
#define SLIMBUS_QMI_SELECT_INSTANCE_RESP_V01	0x0020
#define SLIMBUS_QMI_POWER_REQ_V01		0x0021
#define SLIMBUS_QMI_POWER_RESP_V01		0x0021
#define SLIMBUS_QMI_CHECK_FRAMER_STATUS_REQ	0x0022
#define SLIMBUS_QMI_CHECK_FRAMER_STATUS_RESP	0x0022
#define SLIMBUS_QMI_POWER_REQ_MAX_MSG_LEN	14
#define SLIMBUS_QMI_POWER_RESP_MAX_MSG_LEN	7
#define SLIMBUS_QMI_SELECT_INSTANCE_REQ_MAX_MSG_LEN	14
#define SLIMBUS_QMI_SELECT_INSTANCE_RESP_MAX_MSG_LEN	7
#define SLIMBUS_QMI_CHECK_FRAMER_STAT_RESP_MAX_MSG_LEN	7
/* QMI response timeout of 500ms */
#define SLIMBUS_QMI_RESP_TOUT	1000

/* User defined commands */
#define SLIM_USR_MC_GENERIC_ACK	0x25
#define SLIM_USR_MC_MASTER_CAPABILITY	0x0
#define SLIM_USR_MC_REPORT_SATELLITE	0x1
#define SLIM_USR_MC_ADDR_QUERY		0xD
#define SLIM_USR_MC_ADDR_REPLY		0xE
#define SLIM_USR_MC_DEFINE_CHAN		0x20
#define SLIM_USR_MC_DEF_ACT_CHAN	0x21
#define SLIM_USR_MC_CHAN_CTRL		0x23
#define SLIM_USR_MC_RECONFIG_NOW	0x24
#define SLIM_USR_MC_REQ_BW		0x28
#define SLIM_USR_MC_CONNECT_SRC		0x2C
#define SLIM_USR_MC_CONNECT_SINK	0x2D
#define SLIM_USR_MC_DISCONNECT_PORT	0x2E
#define SLIM_USR_MC_REPEAT_CHANGE_VALUE	0x0

#define QCOM_SLIM_NGD_AUTOSUSPEND	MSEC_PER_SEC
#define SLIM_RX_MSGQ_TIMEOUT_VAL	0x10000

#define SLIM_LA_MGR	0xFF
#define SLIM_ROOT_FREQ	24576000
#define LADDR_RETRY	5

/* Per spec.max 40 bytes per received message */
#define SLIM_MSGQ_BUF_LEN	40
#define QCOM_SLIM_NGD_DESC_NUM	32

#define SLIM_MSG_ASM_FIRST_WORD(l, mt, mc, dt, ad) \
		((l) | ((mt) << 5) | ((mc) << 8) | ((dt) << 15) | ((ad) << 16))

#define INIT_MX_RETRIES 10
#define DEF_RETRY_MS	10
#define SAT_MAGIC_LSB	0xD9
#define SAT_MAGIC_MSB	0xC5
#define SAT_MSG_VER	0x1
#define SAT_MSG_PROT	0x1
#define to_ngd(d)	container_of(d, struct qcom_slim_ngd, dev)

struct ngd_reg_offset_data {
	u32 offset, size;
};

static const struct ngd_reg_offset_data ngd_v1_5_offset_info = {
	.offset = 0x1000,
	.size = 0x1000,
};

enum qcom_slim_ngd_state {
	QCOM_SLIM_NGD_CTRL_AWAKE,
	QCOM_SLIM_NGD_CTRL_IDLE,
	QCOM_SLIM_NGD_CTRL_ASLEEP,
	QCOM_SLIM_NGD_CTRL_DOWN,
};

struct qcom_slim_ngd_qmi {
	struct qmi_handle qmi;
	struct sockaddr_qrtr svc_info;
	struct qmi_handle svc_event_hdl;
	struct qmi_response_type_v01 resp;
	struct qmi_handle *handle;
	struct completion qmi_comp;
};

struct qcom_slim_ngd_ctrl;
struct qcom_slim_ngd;

struct qcom_slim_ngd_dma_desc {
	struct dma_async_tx_descriptor *desc;
	struct qcom_slim_ngd_ctrl *ctrl;
	struct completion *comp;
	dma_cookie_t cookie;
	dma_addr_t phys;
	void *base;
};

struct qcom_slim_ngd {
	struct platform_device *pdev;
	void __iomem *base;
	int id;
};

struct qcom_slim_ngd_ctrl {
	struct slim_framer framer;
	struct slim_controller ctrl;
	struct qcom_slim_ngd_qmi qmi;
	struct qcom_slim_ngd *ngd;
	struct device *dev;
	void __iomem *base;
	struct dma_chan *dma_rx_channel;
	struct dma_chan	*dma_tx_channel;
	struct qcom_slim_ngd_dma_desc rx_desc[QCOM_SLIM_NGD_DESC_NUM];
	struct qcom_slim_ngd_dma_desc txdesc[QCOM_SLIM_NGD_DESC_NUM];
	struct completion reconf;
	struct work_struct m_work;
	struct work_struct ngd_up_work;
	struct workqueue_struct *mwq;
	struct completion qmi_up;
	spinlock_t tx_buf_lock;
	struct mutex tx_lock;
	struct mutex ssr_lock;
	struct notifier_block nb;
	void *notifier;
	struct pdr_handle *pdr;
	enum qcom_slim_ngd_state state;
	dma_addr_t rx_phys_base;
	dma_addr_t tx_phys_base;
	void *rx_base;
	void *tx_base;
	int tx_tail;
	int tx_head;
	u32 ver;
};

enum slimbus_mode_enum_type_v01 {
	/* To force a 32 bit signed enum. Do not change or use*/
	SLIMBUS_MODE_ENUM_TYPE_MIN_ENUM_VAL_V01 = INT_MIN,
	SLIMBUS_MODE_SATELLITE_V01 = 1,
	SLIMBUS_MODE_MASTER_V01 = 2,
	SLIMBUS_MODE_ENUM_TYPE_MAX_ENUM_VAL_V01 = INT_MAX,
};

enum slimbus_pm_enum_type_v01 {
	/* To force a 32 bit signed enum. Do not change or use*/
	SLIMBUS_PM_ENUM_TYPE_MIN_ENUM_VAL_V01 = INT_MIN,
	SLIMBUS_PM_INACTIVE_V01 = 1,
	SLIMBUS_PM_ACTIVE_V01 = 2,
	SLIMBUS_PM_ENUM_TYPE_MAX_ENUM_VAL_V01 = INT_MAX,
};

enum slimbus_resp_enum_type_v01 {
	SLIMBUS_RESP_ENUM_TYPE_MIN_VAL_V01 = INT_MIN,
	SLIMBUS_RESP_SYNCHRONOUS_V01 = 1,
	SLIMBUS_RESP_ENUM_TYPE_MAX_VAL_V01 = INT_MAX,
};

struct slimbus_select_inst_req_msg_v01 {
	uint32_t instance;
	uint8_t mode_valid;
	enum slimbus_mode_enum_type_v01 mode;
};

struct slimbus_select_inst_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

struct slimbus_power_req_msg_v01 {
	enum slimbus_pm_enum_type_v01 pm_req;
	uint8_t resp_type_valid;
	enum slimbus_resp_enum_type_v01 resp_type;
};

struct slimbus_power_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

static const struct qmi_elem_info slimbus_select_inst_req_msg_v01_ei[] = {
	{
		.data_type  = QMI_UNSIGNED_4_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(uint32_t),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x01,
		.offset     = offsetof(struct slimbus_select_inst_req_msg_v01,
				       instance),
		.ei_array   = NULL,
	},
	{
		.data_type  = QMI_OPT_FLAG,
		.elem_len   = 1,
		.elem_size  = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x10,
		.offset     = offsetof(struct slimbus_select_inst_req_msg_v01,
				       mode_valid),
		.ei_array   = NULL,
	},
	{
		.data_type  = QMI_UNSIGNED_4_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(enum slimbus_mode_enum_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x10,
		.offset     = offsetof(struct slimbus_select_inst_req_msg_v01,
				       mode),
		.ei_array   = NULL,
	},
	{
		.data_type  = QMI_EOTI,
		.elem_len   = 0,
		.elem_size  = 0,
		.array_type = NO_ARRAY,
		.tlv_type   = 0x00,
		.offset     = 0,
		.ei_array   = NULL,
	},
};

static const struct qmi_elem_info slimbus_select_inst_resp_msg_v01_ei[] = {
	{
		.data_type  = QMI_STRUCT,
		.elem_len   = 1,
		.elem_size  = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x02,
		.offset     = offsetof(struct slimbus_select_inst_resp_msg_v01,
				       resp),
		.ei_array   = qmi_response_type_v01_ei,
	},
	{
		.data_type  = QMI_EOTI,
		.elem_len   = 0,
		.elem_size  = 0,
		.array_type = NO_ARRAY,
		.tlv_type   = 0x00,
		.offset     = 0,
		.ei_array   = NULL,
	},
};

static const struct qmi_elem_info slimbus_power_req_msg_v01_ei[] = {
	{
		.data_type  = QMI_UNSIGNED_4_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(enum slimbus_pm_enum_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x01,
		.offset     = offsetof(struct slimbus_power_req_msg_v01,
				       pm_req),
		.ei_array   = NULL,
	},
	{
		.data_type  = QMI_OPT_FLAG,
		.elem_len   = 1,
		.elem_size  = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x10,
		.offset     = offsetof(struct slimbus_power_req_msg_v01,
				       resp_type_valid),
	},
	{
		.data_type  = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len   = 1,
		.elem_size  = sizeof(enum slimbus_resp_enum_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x10,
		.offset     = offsetof(struct slimbus_power_req_msg_v01,
				       resp_type),
	},
	{
		.data_type  = QMI_EOTI,
		.elem_len   = 0,
		.elem_size  = 0,
		.array_type = NO_ARRAY,
		.tlv_type   = 0x00,
		.offset     = 0,
		.ei_array   = NULL,
	},
};

static const struct qmi_elem_info slimbus_power_resp_msg_v01_ei[] = {
	{
		.data_type  = QMI_STRUCT,
		.elem_len   = 1,
		.elem_size  = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x02,
		.offset     = offsetof(struct slimbus_power_resp_msg_v01, resp),
		.ei_array   = qmi_response_type_v01_ei,
	},
	{
		.data_type  = QMI_EOTI,
		.elem_len   = 0,
		.elem_size  = 0,
		.array_type = NO_ARRAY,
		.tlv_type   = 0x00,
		.offset     = 0,
		.ei_array   = NULL,
	},
};

static int qcom_slim_qmi_send_select_inst_req(struct qcom_slim_ngd_ctrl *ctrl,
				struct slimbus_select_inst_req_msg_v01 *req)
{
	struct slimbus_select_inst_resp_msg_v01 resp = { { 0, 0 } };
	struct qmi_txn txn;
	int rc;

	rc = qmi_txn_init(ctrl->qmi.handle, &txn,
				slimbus_select_inst_resp_msg_v01_ei, &resp);
	if (rc < 0) {
		dev_err(ctrl->dev, "QMI TXN init fail: %d\n", rc);
		return rc;
	}

	rc = qmi_send_request(ctrl->qmi.handle, NULL, &txn,
				SLIMBUS_QMI_SELECT_INSTANCE_REQ_V01,
				SLIMBUS_QMI_SELECT_INSTANCE_REQ_MAX_MSG_LEN,
				slimbus_select_inst_req_msg_v01_ei, req);
	if (rc < 0) {
		dev_err(ctrl->dev, "QMI send req fail %d\n", rc);
		qmi_txn_cancel(&txn);
		return rc;
	}

	rc = qmi_txn_wait(&txn, SLIMBUS_QMI_RESP_TOUT);
	if (rc < 0) {
		dev_err(ctrl->dev, "QMI TXN wait fail: %d\n", rc);
		return rc;
	}
	/* Check the response */
	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		dev_err(ctrl->dev, "QMI request failed 0x%x\n",
			resp.resp.result);
		return -EREMOTEIO;
	}

	return 0;
}

static void qcom_slim_qmi_power_resp_cb(struct qmi_handle *handle,
					struct sockaddr_qrtr *sq,
					struct qmi_txn *txn, const void *data)
{
	struct slimbus_power_resp_msg_v01 *resp;

	resp = (struct slimbus_power_resp_msg_v01 *)data;
	if (resp->resp.result != QMI_RESULT_SUCCESS_V01)
		pr_err("QMI power request failed 0x%x\n",
				resp->resp.result);

	complete(&txn->completion);
}

static int qcom_slim_qmi_send_power_request(struct qcom_slim_ngd_ctrl *ctrl,
					struct slimbus_power_req_msg_v01 *req)
{
	struct slimbus_power_resp_msg_v01 resp = { { 0, 0 } };
	struct qmi_txn txn;
	int rc;

	rc = qmi_txn_init(ctrl->qmi.handle, &txn,
				slimbus_power_resp_msg_v01_ei, &resp);

	rc = qmi_send_request(ctrl->qmi.handle, NULL, &txn,
				SLIMBUS_QMI_POWER_REQ_V01,
				SLIMBUS_QMI_POWER_REQ_MAX_MSG_LEN,
				slimbus_power_req_msg_v01_ei, req);
	if (rc < 0) {
		dev_err(ctrl->dev, "QMI send req fail %d\n", rc);
		qmi_txn_cancel(&txn);
		return rc;
	}

	rc = qmi_txn_wait(&txn, SLIMBUS_QMI_RESP_TOUT);
	if (rc < 0) {
		dev_err(ctrl->dev, "QMI TXN wait fail: %d\n", rc);
		return rc;
	}

	/* Check the response */
	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		dev_err(ctrl->dev, "QMI request failed 0x%x\n",
			resp.resp.result);
		return -EREMOTEIO;
	}

	return 0;
}

static const struct qmi_msg_handler qcom_slim_qmi_msg_handlers[] = {
	{
		.type = QMI_RESPONSE,
		.msg_id = SLIMBUS_QMI_POWER_RESP_V01,
		.ei = slimbus_power_resp_msg_v01_ei,
		.decoded_size = sizeof(struct slimbus_power_resp_msg_v01),
		.fn = qcom_slim_qmi_power_resp_cb,
	},
	{}
};

static int qcom_slim_qmi_init(struct qcom_slim_ngd_ctrl *ctrl,
			      bool apps_is_master)
{
	struct slimbus_select_inst_req_msg_v01 req;
	struct qmi_handle *handle;
	int rc;

	handle = devm_kzalloc(ctrl->dev, sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	rc = qmi_handle_init(handle, SLIMBUS_QMI_POWER_REQ_MAX_MSG_LEN,
				NULL, qcom_slim_qmi_msg_handlers);
	if (rc < 0) {
		dev_err(ctrl->dev, "QMI client init failed: %d\n", rc);
		goto qmi_handle_init_failed;
	}

	rc = kernel_connect(handle->sock,
				(struct sockaddr *)&ctrl->qmi.svc_info,
				sizeof(ctrl->qmi.svc_info), 0);
	if (rc < 0) {
		dev_err(ctrl->dev, "Remote Service connect failed: %d\n", rc);
		goto qmi_connect_to_service_failed;
	}

	/* Instance is 0 based */
	req.instance = (ctrl->ngd->id >> 1);
	req.mode_valid = 1;

	/* Mode indicates the role of the ADSP */
	if (apps_is_master)
		req.mode = SLIMBUS_MODE_SATELLITE_V01;
	else
		req.mode = SLIMBUS_MODE_MASTER_V01;

	ctrl->qmi.handle = handle;

	rc = qcom_slim_qmi_send_select_inst_req(ctrl, &req);
	if (rc) {
		dev_err(ctrl->dev, "failed to select h/w instance\n");
		goto qmi_select_instance_failed;
	}

	return 0;

qmi_select_instance_failed:
	ctrl->qmi.handle = NULL;
qmi_connect_to_service_failed:
	qmi_handle_release(handle);
qmi_handle_init_failed:
	devm_kfree(ctrl->dev, handle);
	return rc;
}

static void qcom_slim_qmi_exit(struct qcom_slim_ngd_ctrl *ctrl)
{
	if (!ctrl->qmi.handle)
		return;

	qmi_handle_release(ctrl->qmi.handle);
	devm_kfree(ctrl->dev, ctrl->qmi.handle);
	ctrl->qmi.handle = NULL;
}

static int qcom_slim_qmi_power_request(struct qcom_slim_ngd_ctrl *ctrl,
				       bool active)
{
	struct slimbus_power_req_msg_v01 req;

	if (active)
		req.pm_req = SLIMBUS_PM_ACTIVE_V01;
	else
		req.pm_req = SLIMBUS_PM_INACTIVE_V01;

	req.resp_type_valid = 0;

	return qcom_slim_qmi_send_power_request(ctrl, &req);
}

static u32 *qcom_slim_ngd_tx_msg_get(struct qcom_slim_ngd_ctrl *ctrl, int len,
				     struct completion *comp)
{
	struct qcom_slim_ngd_dma_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&ctrl->tx_buf_lock, flags);

	if ((ctrl->tx_tail + 1) % QCOM_SLIM_NGD_DESC_NUM == ctrl->tx_head) {
		spin_unlock_irqrestore(&ctrl->tx_buf_lock, flags);
		return NULL;
	}
	desc  = &ctrl->txdesc[ctrl->tx_tail];
	desc->base = ctrl->tx_base + ctrl->tx_tail * SLIM_MSGQ_BUF_LEN;
	desc->comp = comp;
	ctrl->tx_tail = (ctrl->tx_tail + 1) % QCOM_SLIM_NGD_DESC_NUM;

	spin_unlock_irqrestore(&ctrl->tx_buf_lock, flags);

	return desc->base;
}

static void qcom_slim_ngd_tx_msg_dma_cb(void *args)
{
	struct qcom_slim_ngd_dma_desc *desc = args;
	struct qcom_slim_ngd_ctrl *ctrl = desc->ctrl;
	unsigned long flags;

	spin_lock_irqsave(&ctrl->tx_buf_lock, flags);

	if (desc->comp) {
		complete(desc->comp);
		desc->comp = NULL;
	}

	ctrl->tx_head = (ctrl->tx_head + 1) % QCOM_SLIM_NGD_DESC_NUM;
	spin_unlock_irqrestore(&ctrl->tx_buf_lock, flags);
}

static int qcom_slim_ngd_tx_msg_post(struct qcom_slim_ngd_ctrl *ctrl,
				     void *buf, int len)
{
	struct qcom_slim_ngd_dma_desc *desc;
	unsigned long flags;
	int index, offset;

	spin_lock_irqsave(&ctrl->tx_buf_lock, flags);
	offset = buf - ctrl->tx_base;
	index = offset/SLIM_MSGQ_BUF_LEN;

	desc = &ctrl->txdesc[index];
	desc->phys = ctrl->tx_phys_base + offset;
	desc->base = ctrl->tx_base + offset;
	desc->ctrl = ctrl;
	len = (len + 3) & 0xfc;

	desc->desc = dmaengine_prep_slave_single(ctrl->dma_tx_channel,
						desc->phys, len,
						DMA_MEM_TO_DEV,
						DMA_PREP_INTERRUPT);
	if (!desc->desc) {
		dev_err(ctrl->dev, "unable to prepare channel\n");
		spin_unlock_irqrestore(&ctrl->tx_buf_lock, flags);
		return -EINVAL;
	}

	desc->desc->callback = qcom_slim_ngd_tx_msg_dma_cb;
	desc->desc->callback_param = desc;
	desc->desc->cookie = dmaengine_submit(desc->desc);
	dma_async_issue_pending(ctrl->dma_tx_channel);
	spin_unlock_irqrestore(&ctrl->tx_buf_lock, flags);

	return 0;
}

static void qcom_slim_ngd_rx(struct qcom_slim_ngd_ctrl *ctrl, u8 *buf)
{
	u8 mc, mt, len;

	mt = SLIM_HEADER_GET_MT(buf[0]);
	len = SLIM_HEADER_GET_RL(buf[0]);
	mc = SLIM_HEADER_GET_MC(buf[1]);

	if (mc == SLIM_USR_MC_MASTER_CAPABILITY &&
		mt == SLIM_MSG_MT_SRC_REFERRED_USER)
		queue_work(ctrl->mwq, &ctrl->m_work);

	if (mc == SLIM_MSG_MC_REPLY_INFORMATION ||
	    mc == SLIM_MSG_MC_REPLY_VALUE || (mc == SLIM_USR_MC_ADDR_REPLY &&
	    mt == SLIM_MSG_MT_SRC_REFERRED_USER) ||
		(mc == SLIM_USR_MC_GENERIC_ACK &&
		 mt == SLIM_MSG_MT_SRC_REFERRED_USER)) {
		slim_msg_response(&ctrl->ctrl, &buf[4], buf[3], len - 4);
		pm_runtime_mark_last_busy(ctrl->ctrl.dev);
	}
}

static void qcom_slim_ngd_rx_msgq_cb(void *args)
{
	struct qcom_slim_ngd_dma_desc *desc = args;
	struct qcom_slim_ngd_ctrl *ctrl = desc->ctrl;

	qcom_slim_ngd_rx(ctrl, (u8 *)desc->base);
	/* Add descriptor back to the queue */
	desc->desc = dmaengine_prep_slave_single(ctrl->dma_rx_channel,
					desc->phys, SLIM_MSGQ_BUF_LEN,
					DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT);
	if (!desc->desc) {
		dev_err(ctrl->dev, "Unable to prepare rx channel\n");
		return;
	}

	desc->desc->callback = qcom_slim_ngd_rx_msgq_cb;
	desc->desc->callback_param = desc;
	desc->desc->cookie = dmaengine_submit(desc->desc);
	dma_async_issue_pending(ctrl->dma_rx_channel);
}

static int qcom_slim_ngd_post_rx_msgq(struct qcom_slim_ngd_ctrl *ctrl)
{
	struct qcom_slim_ngd_dma_desc *desc;
	int i;

	for (i = 0; i < QCOM_SLIM_NGD_DESC_NUM; i++) {
		desc = &ctrl->rx_desc[i];
		desc->phys = ctrl->rx_phys_base + i * SLIM_MSGQ_BUF_LEN;
		desc->ctrl = ctrl;
		desc->base = ctrl->rx_base + i * SLIM_MSGQ_BUF_LEN;
		desc->desc = dmaengine_prep_slave_single(ctrl->dma_rx_channel,
						desc->phys, SLIM_MSGQ_BUF_LEN,
						DMA_DEV_TO_MEM,
						DMA_PREP_INTERRUPT);
		if (!desc->desc) {
			dev_err(ctrl->dev, "Unable to prepare rx channel\n");
			return -EINVAL;
		}

		desc->desc->callback = qcom_slim_ngd_rx_msgq_cb;
		desc->desc->callback_param = desc;
		desc->desc->cookie = dmaengine_submit(desc->desc);
	}
	dma_async_issue_pending(ctrl->dma_rx_channel);

	return 0;
}

static int qcom_slim_ngd_init_rx_msgq(struct qcom_slim_ngd_ctrl *ctrl)
{
	struct device *dev = ctrl->dev;
	int ret, size;

	ctrl->dma_rx_channel = dma_request_chan(dev, "rx");
	if (IS_ERR(ctrl->dma_rx_channel)) {
		dev_err(dev, "Failed to request RX dma channel");
		ret = PTR_ERR(ctrl->dma_rx_channel);
		ctrl->dma_rx_channel = NULL;
		return ret;
	}

	size = QCOM_SLIM_NGD_DESC_NUM * SLIM_MSGQ_BUF_LEN;
	ctrl->rx_base = dma_alloc_coherent(dev, size, &ctrl->rx_phys_base,
					   GFP_KERNEL);
	if (!ctrl->rx_base) {
		ret = -ENOMEM;
		goto rel_rx;
	}

	ret = qcom_slim_ngd_post_rx_msgq(ctrl);
	if (ret) {
		dev_err(dev, "post_rx_msgq() failed 0x%x\n", ret);
		goto rx_post_err;
	}

	return 0;

rx_post_err:
	dma_free_coherent(dev, size, ctrl->rx_base, ctrl->rx_phys_base);
rel_rx:
	dma_release_channel(ctrl->dma_rx_channel);
	return ret;
}

static int qcom_slim_ngd_init_tx_msgq(struct qcom_slim_ngd_ctrl *ctrl)
{
	struct device *dev = ctrl->dev;
	unsigned long flags;
	int ret = 0;
	int size;

	ctrl->dma_tx_channel = dma_request_chan(dev, "tx");
	if (IS_ERR(ctrl->dma_tx_channel)) {
		dev_err(dev, "Failed to request TX dma channel");
		ret = PTR_ERR(ctrl->dma_tx_channel);
		ctrl->dma_tx_channel = NULL;
		return ret;
	}

	size = ((QCOM_SLIM_NGD_DESC_NUM + 1) * SLIM_MSGQ_BUF_LEN);
	ctrl->tx_base = dma_alloc_coherent(dev, size, &ctrl->tx_phys_base,
					   GFP_KERNEL);
	if (!ctrl->tx_base) {
		ret = -EINVAL;
		goto rel_tx;
	}

	spin_lock_irqsave(&ctrl->tx_buf_lock, flags);
	ctrl->tx_tail = 0;
	ctrl->tx_head = 0;
	spin_unlock_irqrestore(&ctrl->tx_buf_lock, flags);

	return 0;
rel_tx:
	dma_release_channel(ctrl->dma_tx_channel);
	return ret;
}

static int qcom_slim_ngd_init_dma(struct qcom_slim_ngd_ctrl *ctrl)
{
	int ret = 0;

	ret = qcom_slim_ngd_init_rx_msgq(ctrl);
	if (ret) {
		dev_err(ctrl->dev, "rx dma init failed\n");
		return ret;
	}

	ret = qcom_slim_ngd_init_tx_msgq(ctrl);
	if (ret)
		dev_err(ctrl->dev, "tx dma init failed\n");

	return ret;
}

static irqreturn_t qcom_slim_ngd_interrupt(int irq, void *d)
{
	struct qcom_slim_ngd_ctrl *ctrl = d;
	void __iomem *base = ctrl->ngd->base;
	u32 stat;

	if (pm_runtime_suspended(ctrl->ctrl.dev)) {
		dev_warn_once(ctrl->dev, "Interrupt received while suspended\n");
		return IRQ_NONE;
	}

	stat = readl(base + NGD_INT_STAT);

	if ((stat & NGD_INT_MSG_BUF_CONTE) ||
		(stat & NGD_INT_MSG_TX_INVAL) || (stat & NGD_INT_DEV_ERR) ||
		(stat & NGD_INT_TX_NACKED_2)) {
		dev_err(ctrl->dev, "Error Interrupt received 0x%x\n", stat);
	}

	writel(stat, base + NGD_INT_CLR);

	return IRQ_HANDLED;
}

static int qcom_slim_ngd_xfer_msg(struct slim_controller *sctrl,
				  struct slim_msg_txn *txn)
{
	struct qcom_slim_ngd_ctrl *ctrl = dev_get_drvdata(sctrl->dev);
	DECLARE_COMPLETION_ONSTACK(tx_sent);
	DECLARE_COMPLETION_ONSTACK(done);
	int ret, timeout, i;
	u8 wbuf[SLIM_MSGQ_BUF_LEN];
	u8 rbuf[SLIM_MSGQ_BUF_LEN];
	u32 *pbuf;
	u8 *puc;
	u8 la = txn->la;
	bool usr_msg = false;

	if (txn->mt == SLIM_MSG_MT_CORE &&
		(txn->mc >= SLIM_MSG_MC_BEGIN_RECONFIGURATION &&
		 txn->mc <= SLIM_MSG_MC_RECONFIGURE_NOW))
		return 0;

	if (txn->dt == SLIM_MSG_DEST_ENUMADDR)
		return -EPROTONOSUPPORT;

	if (txn->msg->num_bytes > SLIM_MSGQ_BUF_LEN ||
			txn->rl > SLIM_MSGQ_BUF_LEN) {
		dev_err(ctrl->dev, "msg exceeds HW limit\n");
		return -EINVAL;
	}

	pbuf = qcom_slim_ngd_tx_msg_get(ctrl, txn->rl, &tx_sent);
	if (!pbuf) {
		dev_err(ctrl->dev, "Message buffer unavailable\n");
		return -ENOMEM;
	}

	if (txn->mt == SLIM_MSG_MT_CORE &&
		(txn->mc == SLIM_MSG_MC_CONNECT_SOURCE ||
		txn->mc == SLIM_MSG_MC_CONNECT_SINK ||
		txn->mc == SLIM_MSG_MC_DISCONNECT_PORT)) {
		txn->mt = SLIM_MSG_MT_DEST_REFERRED_USER;
		switch (txn->mc) {
		case SLIM_MSG_MC_CONNECT_SOURCE:
			txn->mc = SLIM_USR_MC_CONNECT_SRC;
			break;
		case SLIM_MSG_MC_CONNECT_SINK:
			txn->mc = SLIM_USR_MC_CONNECT_SINK;
			break;
		case SLIM_MSG_MC_DISCONNECT_PORT:
			txn->mc = SLIM_USR_MC_DISCONNECT_PORT;
			break;
		default:
			return -EINVAL;
		}

		usr_msg = true;
		i = 0;
		wbuf[i++] = txn->la;
		la = SLIM_LA_MGR;
		wbuf[i++] = txn->msg->wbuf[0];
		if (txn->mc != SLIM_USR_MC_DISCONNECT_PORT)
			wbuf[i++] = txn->msg->wbuf[1];

		txn->comp = &done;
		ret = slim_alloc_txn_tid(sctrl, txn);
		if (ret) {
			dev_err(ctrl->dev, "Unable to allocate TID\n");
			return ret;
		}

		wbuf[i++] = txn->tid;

		txn->msg->num_bytes = i;
		txn->msg->wbuf = wbuf;
		txn->msg->rbuf = rbuf;
		txn->rl = txn->msg->num_bytes + 4;
	}

	/* HW expects length field to be excluded */
	txn->rl--;
	puc = (u8 *)pbuf;
	*pbuf = 0;
	if (txn->dt == SLIM_MSG_DEST_LOGICALADDR) {
		*pbuf = SLIM_MSG_ASM_FIRST_WORD(txn->rl, txn->mt, txn->mc, 0,
				la);
		puc += 3;
	} else {
		*pbuf = SLIM_MSG_ASM_FIRST_WORD(txn->rl, txn->mt, txn->mc, 1,
				la);
		puc += 2;
	}

	if (slim_tid_txn(txn->mt, txn->mc))
		*(puc++) = txn->tid;

	if (slim_ec_txn(txn->mt, txn->mc)) {
		*(puc++) = (txn->ec & 0xFF);
		*(puc++) = (txn->ec >> 8) & 0xFF;
	}

	if (txn->msg && txn->msg->wbuf)
		memcpy(puc, txn->msg->wbuf, txn->msg->num_bytes);

	mutex_lock(&ctrl->tx_lock);
	ret = qcom_slim_ngd_tx_msg_post(ctrl, pbuf, txn->rl);
	if (ret) {
		mutex_unlock(&ctrl->tx_lock);
		return ret;
	}

	timeout = wait_for_completion_timeout(&tx_sent, HZ);
	if (!timeout) {
		dev_err(sctrl->dev, "TX timed out:MC:0x%x,mt:0x%x", txn->mc,
					txn->mt);
		mutex_unlock(&ctrl->tx_lock);
		return -ETIMEDOUT;
	}

	if (usr_msg) {
		timeout = wait_for_completion_timeout(&done, HZ);
		if (!timeout) {
			dev_err(sctrl->dev, "TX timed out:MC:0x%x,mt:0x%x",
				txn->mc, txn->mt);
			mutex_unlock(&ctrl->tx_lock);
			return -ETIMEDOUT;
		}
	}

	mutex_unlock(&ctrl->tx_lock);
	return 0;
}

static int qcom_slim_ngd_xfer_msg_sync(struct slim_controller *ctrl,
				       struct slim_msg_txn *txn)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int ret, timeout;

	ret = pm_runtime_get_sync(ctrl->dev);
	if (ret < 0)
		goto pm_put;

	txn->comp = &done;

	ret = qcom_slim_ngd_xfer_msg(ctrl, txn);
	if (ret)
		goto pm_put;

	timeout = wait_for_completion_timeout(&done, HZ);
	if (!timeout) {
		dev_err(ctrl->dev, "TX timed out:MC:0x%x,mt:0x%x", txn->mc,
				txn->mt);
		ret = -ETIMEDOUT;
		goto pm_put;
	}
	return 0;

pm_put:
	pm_runtime_put(ctrl->dev);

	return ret;
}

static int qcom_slim_calc_coef(struct slim_stream_runtime *rt, int *exp)
{
	struct slim_controller *ctrl = rt->dev->ctrl;
	int coef;

	if (rt->ratem * ctrl->a_framer->superfreq < rt->rate)
		rt->ratem++;

	coef = rt->ratem;
	*exp = 0;

	/*
	 * CRM = Cx(2^E) is the formula we are using.
	 * Here C is the coffecient and E is the exponent.
	 * CRM is the Channel Rate Multiplier.
	 * Coefficeint should be either 1 or 3 and exponenet
	 * should be an integer between 0 to 9, inclusive.
	 */
	while (1) {
		while ((coef & 0x1) != 0x1) {
			coef >>= 1;
			*exp = *exp + 1;
		}

		if (coef <= 3)
			break;

		coef++;
	}

	/*
	 * we rely on the coef value (1 or 3) to set a bit
	 * in the slimbus message packet. This bit is
	 * BIT(5) which is the segment rate coefficient.
	 */
	if (coef == 1) {
		if (*exp > 9)
			return -EIO;
		coef = 0;
	} else {
		if (*exp > 8)
			return -EIO;
		coef = 1;
	}

	return coef;
}

static int qcom_slim_ngd_enable_stream(struct slim_stream_runtime *rt)
{
	struct slim_device *sdev = rt->dev;
	struct slim_controller *ctrl = sdev->ctrl;
	struct slim_val_inf msg =  {0};
	u8 wbuf[SLIM_MSGQ_BUF_LEN];
	u8 rbuf[SLIM_MSGQ_BUF_LEN];
	struct slim_msg_txn txn = {0,};
	int i, ret;

	txn.mt = SLIM_MSG_MT_DEST_REFERRED_USER;
	txn.dt = SLIM_MSG_DEST_LOGICALADDR;
	txn.la = SLIM_LA_MGR;
	txn.ec = 0;
	txn.msg = &msg;
	txn.msg->num_bytes = 0;
	txn.msg->wbuf = wbuf;
	txn.msg->rbuf = rbuf;

	for (i = 0; i < rt->num_ports; i++) {
		struct slim_port *port = &rt->ports[i];

		if (txn.msg->num_bytes == 0) {
			int exp = 0, coef = 0;

			wbuf[txn.msg->num_bytes++] = sdev->laddr;
			wbuf[txn.msg->num_bytes] = rt->bps >> 2 |
						   (port->ch.aux_fmt << 6);

			/* calculate coef dynamically */
			coef = qcom_slim_calc_coef(rt, &exp);
			if (coef < 0) {
				dev_err(&sdev->dev,
				"%s: error calculating coef %d\n", __func__,
									coef);
				return -EIO;
			}

			if (coef)
				wbuf[txn.msg->num_bytes] |= BIT(5);

			txn.msg->num_bytes++;
			wbuf[txn.msg->num_bytes++] = exp << 4 | rt->prot;

			if (rt->prot == SLIM_PROTO_ISO)
				wbuf[txn.msg->num_bytes++] =
						port->ch.prrate |
						SLIM_CHANNEL_CONTENT_FL;
			else
				wbuf[txn.msg->num_bytes++] =  port->ch.prrate;

			ret = slim_alloc_txn_tid(ctrl, &txn);
			if (ret) {
				dev_err(&sdev->dev, "Fail to allocate TID\n");
				return -ENXIO;
			}
			wbuf[txn.msg->num_bytes++] = txn.tid;
		}
		wbuf[txn.msg->num_bytes++] = port->ch.id;
	}

	txn.mc = SLIM_USR_MC_DEF_ACT_CHAN;
	txn.rl = txn.msg->num_bytes + 4;
	ret = qcom_slim_ngd_xfer_msg_sync(ctrl, &txn);
	if (ret) {
		slim_free_txn_tid(ctrl, &txn);
		dev_err(&sdev->dev, "TX timed out:MC:0x%x,mt:0x%x", txn.mc,
				txn.mt);
		return ret;
	}

	txn.mc = SLIM_USR_MC_RECONFIG_NOW;
	txn.msg->num_bytes = 2;
	wbuf[1] = sdev->laddr;
	txn.rl = txn.msg->num_bytes + 4;

	ret = slim_alloc_txn_tid(ctrl, &txn);
	if (ret) {
		dev_err(ctrl->dev, "Fail to allocate TID\n");
		return ret;
	}

	wbuf[0] = txn.tid;
	ret = qcom_slim_ngd_xfer_msg_sync(ctrl, &txn);
	if (ret) {
		slim_free_txn_tid(ctrl, &txn);
		dev_err(&sdev->dev, "TX timed out:MC:0x%x,mt:0x%x", txn.mc,
				txn.mt);
	}

	return ret;
}

static int qcom_slim_ngd_get_laddr(struct slim_controller *ctrl,
				   struct slim_eaddr *ea, u8 *laddr)
{
	struct slim_val_inf msg =  {0};
	u8 failed_ea[6] = {0, 0, 0, 0, 0, 0};
	struct slim_msg_txn txn;
	u8 wbuf[10] = {0};
	u8 rbuf[10] = {0};
	int ret;

	txn.mt = SLIM_MSG_MT_DEST_REFERRED_USER;
	txn.dt = SLIM_MSG_DEST_LOGICALADDR;
	txn.la = SLIM_LA_MGR;
	txn.ec = 0;

	txn.mc = SLIM_USR_MC_ADDR_QUERY;
	txn.rl = 11;
	txn.msg = &msg;
	txn.msg->num_bytes = 7;
	txn.msg->wbuf = wbuf;
	txn.msg->rbuf = rbuf;

	ret = slim_alloc_txn_tid(ctrl, &txn);
	if (ret < 0)
		return ret;

	wbuf[0] = (u8)txn.tid;
	memcpy(&wbuf[1], ea, sizeof(*ea));

	ret = qcom_slim_ngd_xfer_msg_sync(ctrl, &txn);
	if (ret) {
		slim_free_txn_tid(ctrl, &txn);
		return ret;
	}

	if (!memcmp(rbuf, failed_ea, 6))
		return -ENXIO;

	*laddr = rbuf[6];

	return ret;
}

static int qcom_slim_ngd_exit_dma(struct qcom_slim_ngd_ctrl *ctrl)
{
	if (ctrl->dma_rx_channel) {
		dmaengine_terminate_sync(ctrl->dma_rx_channel);
		dma_release_channel(ctrl->dma_rx_channel);
	}

	if (ctrl->dma_tx_channel) {
		dmaengine_terminate_sync(ctrl->dma_tx_channel);
		dma_release_channel(ctrl->dma_tx_channel);
	}

	ctrl->dma_tx_channel = ctrl->dma_rx_channel = NULL;

	return 0;
}

static void qcom_slim_ngd_setup(struct qcom_slim_ngd_ctrl *ctrl)
{
	u32 cfg = readl_relaxed(ctrl->ngd->base);

	if (ctrl->state == QCOM_SLIM_NGD_CTRL_DOWN ||
		ctrl->state == QCOM_SLIM_NGD_CTRL_ASLEEP)
		qcom_slim_ngd_init_dma(ctrl);

	/* By default enable message queues */
	cfg |= NGD_CFG_RX_MSGQ_EN;
	cfg |= NGD_CFG_TX_MSGQ_EN;

	/* Enable NGD if it's not already enabled*/
	if (!(cfg & NGD_CFG_ENABLE))
		cfg |= NGD_CFG_ENABLE;

	writel_relaxed(cfg, ctrl->ngd->base);
}

static int qcom_slim_ngd_power_up(struct qcom_slim_ngd_ctrl *ctrl)
{
	enum qcom_slim_ngd_state cur_state = ctrl->state;
	struct qcom_slim_ngd *ngd = ctrl->ngd;
	u32 laddr, rx_msgq;
	int timeout, ret = 0;

	if (ctrl->state == QCOM_SLIM_NGD_CTRL_DOWN) {
		timeout = wait_for_completion_timeout(&ctrl->qmi.qmi_comp, HZ);
		if (!timeout)
			return -EREMOTEIO;
	}

	if (ctrl->state == QCOM_SLIM_NGD_CTRL_ASLEEP ||
		ctrl->state == QCOM_SLIM_NGD_CTRL_DOWN) {
		ret = qcom_slim_qmi_power_request(ctrl, true);
		if (ret) {
			dev_err(ctrl->dev, "SLIM QMI power request failed:%d\n",
					ret);
			return ret;
		}
	}

	ctrl->ver = readl_relaxed(ctrl->base);
	/* Version info in 16 MSbits */
	ctrl->ver >>= 16;

	laddr = readl_relaxed(ngd->base + NGD_STATUS);
	if (laddr & NGD_LADDR) {
		/*
		 * external MDM restart case where ADSP itself was active framer
		 * For example, modem restarted when playback was active
		 */
		if (cur_state == QCOM_SLIM_NGD_CTRL_AWAKE) {
			dev_info(ctrl->dev, "Subsys restart: ADSP active framer\n");
			return 0;
		}
		qcom_slim_ngd_setup(ctrl);
		return 0;
	}

	/*
	 * Reinitialize only when registers are not retained or when enumeration
	 * is lost for ngd.
	 */
	reinit_completion(&ctrl->reconf);

	writel_relaxed(DEF_NGD_INT_MASK, ngd->base + NGD_INT_EN);
	rx_msgq = readl_relaxed(ngd->base + NGD_RX_MSGQ_CFG);

	writel_relaxed(rx_msgq|SLIM_RX_MSGQ_TIMEOUT_VAL,
				ngd->base + NGD_RX_MSGQ_CFG);
	qcom_slim_ngd_setup(ctrl);

	timeout = wait_for_completion_timeout(&ctrl->reconf, HZ);
	if (!timeout) {
		dev_err(ctrl->dev, "capability exchange timed-out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void qcom_slim_ngd_notify_slaves(struct qcom_slim_ngd_ctrl *ctrl)
{
	struct slim_device *sbdev;
	struct device_node *node;

	for_each_child_of_node(ctrl->ngd->pdev->dev.of_node, node) {
		sbdev = of_slim_get_device(&ctrl->ctrl, node);
		if (!sbdev)
			continue;

		if (slim_get_logical_addr(sbdev))
			dev_err(ctrl->dev, "Failed to get logical address\n");
	}
}

static void qcom_slim_ngd_master_worker(struct work_struct *work)
{
	struct qcom_slim_ngd_ctrl *ctrl;
	struct slim_msg_txn txn;
	struct slim_val_inf msg = {0};
	int retries = 0;
	u8 wbuf[8];
	int ret = 0;

	ctrl = container_of(work, struct qcom_slim_ngd_ctrl, m_work);
	txn.dt = SLIM_MSG_DEST_LOGICALADDR;
	txn.ec = 0;
	txn.mc = SLIM_USR_MC_REPORT_SATELLITE;
	txn.mt = SLIM_MSG_MT_SRC_REFERRED_USER;
	txn.la = SLIM_LA_MGR;
	wbuf[0] = SAT_MAGIC_LSB;
	wbuf[1] = SAT_MAGIC_MSB;
	wbuf[2] = SAT_MSG_VER;
	wbuf[3] = SAT_MSG_PROT;
	txn.msg = &msg;
	txn.msg->wbuf = wbuf;
	txn.msg->num_bytes = 4;
	txn.rl = 8;

	dev_info(ctrl->dev, "SLIM SAT: Rcvd master capability\n");

capability_retry:
	ret = qcom_slim_ngd_xfer_msg(&ctrl->ctrl, &txn);
	if (!ret) {
		if (ctrl->state >= QCOM_SLIM_NGD_CTRL_ASLEEP)
			complete(&ctrl->reconf);
		else
			dev_err(ctrl->dev, "unexpected state:%d\n",
						ctrl->state);

		if (ctrl->state == QCOM_SLIM_NGD_CTRL_DOWN)
			qcom_slim_ngd_notify_slaves(ctrl);

	} else if (ret == -EIO) {
		dev_err(ctrl->dev, "capability message NACKed, retrying\n");
		if (retries < INIT_MX_RETRIES) {
			msleep(DEF_RETRY_MS);
			retries++;
			goto capability_retry;
		}
	} else {
		dev_err(ctrl->dev, "SLIM: capability TX failed:%d\n", ret);
	}
}

static int qcom_slim_ngd_update_device_status(struct device *dev, void *null)
{
	slim_report_absent(to_slim_device(dev));

	return 0;
}

static int qcom_slim_ngd_runtime_resume(struct device *dev)
{
	struct qcom_slim_ngd_ctrl *ctrl = dev_get_drvdata(dev);
	int ret = 0;

	if (!ctrl->qmi.handle)
		return 0;

	if (ctrl->state >= QCOM_SLIM_NGD_CTRL_ASLEEP)
		ret = qcom_slim_ngd_power_up(ctrl);
	if (ret) {
		/* Did SSR cause this power up failure */
		if (ctrl->state != QCOM_SLIM_NGD_CTRL_DOWN)
			ctrl->state = QCOM_SLIM_NGD_CTRL_ASLEEP;
		else
			dev_err(ctrl->dev, "HW wakeup attempt during SSR\n");
	} else {
		ctrl->state = QCOM_SLIM_NGD_CTRL_AWAKE;
	}

	return 0;
}

static int qcom_slim_ngd_enable(struct qcom_slim_ngd_ctrl *ctrl, bool enable)
{
	if (enable) {
		int ret = qcom_slim_qmi_init(ctrl, false);

		if (ret) {
			dev_err(ctrl->dev, "qmi init fail, ret:%d, state:%d\n",
				ret, ctrl->state);
			return ret;
		}
		/* controller state should be in sync with framework state */
		complete(&ctrl->qmi.qmi_comp);
		if (!pm_runtime_enabled(ctrl->ctrl.dev) ||
			 !pm_runtime_suspended(ctrl->ctrl.dev))
			qcom_slim_ngd_runtime_resume(ctrl->ctrl.dev);
		else
			pm_runtime_resume(ctrl->ctrl.dev);

		pm_runtime_mark_last_busy(ctrl->ctrl.dev);
		pm_runtime_put(ctrl->ctrl.dev);

		ret = slim_register_controller(&ctrl->ctrl);
		if (ret) {
			dev_err(ctrl->dev, "error adding slim controller\n");
			return ret;
		}

		dev_info(ctrl->dev, "SLIM controller Registered\n");
	} else {
		qcom_slim_qmi_exit(ctrl);
		slim_unregister_controller(&ctrl->ctrl);
	}

	return 0;
}

static int qcom_slim_ngd_qmi_new_server(struct qmi_handle *hdl,
					struct qmi_service *service)
{
	struct qcom_slim_ngd_qmi *qmi =
		container_of(hdl, struct qcom_slim_ngd_qmi, svc_event_hdl);
	struct qcom_slim_ngd_ctrl *ctrl =
		container_of(qmi, struct qcom_slim_ngd_ctrl, qmi);

	qmi->svc_info.sq_family = AF_QIPCRTR;
	qmi->svc_info.sq_node = service->node;
	qmi->svc_info.sq_port = service->port;

	complete(&ctrl->qmi_up);

	return 0;
}

static void qcom_slim_ngd_qmi_del_server(struct qmi_handle *hdl,
					 struct qmi_service *service)
{
	struct qcom_slim_ngd_qmi *qmi =
		container_of(hdl, struct qcom_slim_ngd_qmi, svc_event_hdl);
	struct qcom_slim_ngd_ctrl *ctrl =
		container_of(qmi, struct qcom_slim_ngd_ctrl, qmi);

	reinit_completion(&ctrl->qmi_up);
	qmi->svc_info.sq_node = 0;
	qmi->svc_info.sq_port = 0;
}

static const struct qmi_ops qcom_slim_ngd_qmi_svc_event_ops = {
	.new_server = qcom_slim_ngd_qmi_new_server,
	.del_server = qcom_slim_ngd_qmi_del_server,
};

static int qcom_slim_ngd_qmi_svc_event_init(struct qcom_slim_ngd_ctrl *ctrl)
{
	struct qcom_slim_ngd_qmi *qmi = &ctrl->qmi;
	int ret;

	ret = qmi_handle_init(&qmi->svc_event_hdl, 0,
				&qcom_slim_ngd_qmi_svc_event_ops, NULL);
	if (ret < 0) {
		dev_err(ctrl->dev, "qmi_handle_init failed: %d\n", ret);
		return ret;
	}

	ret = qmi_add_lookup(&qmi->svc_event_hdl, SLIMBUS_QMI_SVC_ID,
			SLIMBUS_QMI_SVC_V1, SLIMBUS_QMI_INS_ID);
	if (ret < 0) {
		dev_err(ctrl->dev, "qmi_add_lookup failed: %d\n", ret);
		qmi_handle_release(&qmi->svc_event_hdl);
	}
	return ret;
}

static void qcom_slim_ngd_qmi_svc_event_deinit(struct qcom_slim_ngd_qmi *qmi)
{
	qmi_handle_release(&qmi->svc_event_hdl);
}

static struct platform_driver qcom_slim_ngd_driver;
#define QCOM_SLIM_NGD_DRV_NAME	"qcom,slim-ngd"

static const struct of_device_id qcom_slim_ngd_dt_match[] = {
	{
		.compatible = "qcom,slim-ngd-v1.5.0",
		.data = &ngd_v1_5_offset_info,
	},{
		.compatible = "qcom,slim-ngd-v2.1.0",
		.data = &ngd_v1_5_offset_info,
	},
	{}
};

MODULE_DEVICE_TABLE(of, qcom_slim_ngd_dt_match);

static void qcom_slim_ngd_down(struct qcom_slim_ngd_ctrl *ctrl)
{
	mutex_lock(&ctrl->ssr_lock);
	device_for_each_child(ctrl->ctrl.dev, NULL,
			      qcom_slim_ngd_update_device_status);
	qcom_slim_ngd_enable(ctrl, false);
	mutex_unlock(&ctrl->ssr_lock);
}

static void qcom_slim_ngd_up_worker(struct work_struct *work)
{
	struct qcom_slim_ngd_ctrl *ctrl;

	ctrl = container_of(work, struct qcom_slim_ngd_ctrl, ngd_up_work);

	/* Make sure qmi service is up before continuing */
	if (!wait_for_completion_interruptible_timeout(&ctrl->qmi_up,
						       msecs_to_jiffies(MSEC_PER_SEC))) {
		dev_err(ctrl->dev, "QMI wait timeout\n");
		return;
	}

	mutex_lock(&ctrl->ssr_lock);
	qcom_slim_ngd_enable(ctrl, true);
	mutex_unlock(&ctrl->ssr_lock);
}

static int qcom_slim_ngd_ssr_pdr_notify(struct qcom_slim_ngd_ctrl *ctrl,
					unsigned long action)
{
	switch (action) {
	case QCOM_SSR_BEFORE_SHUTDOWN:
	case SERVREG_SERVICE_STATE_DOWN:
		/* Make sure the last dma xfer is finished */
		mutex_lock(&ctrl->tx_lock);
		if (ctrl->state != QCOM_SLIM_NGD_CTRL_DOWN) {
			pm_runtime_get_noresume(ctrl->ctrl.dev);
			ctrl->state = QCOM_SLIM_NGD_CTRL_DOWN;
			qcom_slim_ngd_down(ctrl);
			qcom_slim_ngd_exit_dma(ctrl);
		}
		mutex_unlock(&ctrl->tx_lock);
		break;
	case QCOM_SSR_AFTER_POWERUP:
	case SERVREG_SERVICE_STATE_UP:
		schedule_work(&ctrl->ngd_up_work);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int qcom_slim_ngd_ssr_notify(struct notifier_block *nb,
				    unsigned long action,
				    void *data)
{
	struct qcom_slim_ngd_ctrl *ctrl = container_of(nb,
					       struct qcom_slim_ngd_ctrl, nb);

	return qcom_slim_ngd_ssr_pdr_notify(ctrl, action);
}

static void slim_pd_status(int state, char *svc_path, void *priv)
{
	struct qcom_slim_ngd_ctrl *ctrl = (struct qcom_slim_ngd_ctrl *)priv;

	qcom_slim_ngd_ssr_pdr_notify(ctrl, state);
}
static int of_qcom_slim_ngd_register(struct device *parent,
				     struct qcom_slim_ngd_ctrl *ctrl)
{
	const struct ngd_reg_offset_data *data;
	struct qcom_slim_ngd *ngd;
	const struct of_device_id *match;
	struct device_node *node;
	u32 id;
	int ret;

	match = of_match_node(qcom_slim_ngd_dt_match, parent->of_node);
	data = match->data;
	for_each_available_child_of_node(parent->of_node, node) {
		if (of_property_read_u32(node, "reg", &id))
			continue;

		ngd = kzalloc(sizeof(*ngd), GFP_KERNEL);
		if (!ngd) {
			of_node_put(node);
			return -ENOMEM;
		}

		ngd->pdev = platform_device_alloc(QCOM_SLIM_NGD_DRV_NAME, id);
		if (!ngd->pdev) {
			kfree(ngd);
			of_node_put(node);
			return -ENOMEM;
		}
		ngd->id = id;
		ngd->pdev->dev.parent = parent;

		ret = driver_set_override(&ngd->pdev->dev,
					  &ngd->pdev->driver_override,
					  QCOM_SLIM_NGD_DRV_NAME,
					  strlen(QCOM_SLIM_NGD_DRV_NAME));
		if (ret) {
			platform_device_put(ngd->pdev);
			kfree(ngd);
			of_node_put(node);
			return ret;
		}
		ngd->pdev->dev.of_node = node;
		ctrl->ngd = ngd;

		ret = platform_device_add(ngd->pdev);
		if (ret) {
			platform_device_put(ngd->pdev);
			kfree(ngd);
			of_node_put(node);
			return ret;
		}
		ngd->base = ctrl->base + ngd->id * data->offset +
					(ngd->id - 1) * data->size;

		return 0;
	}

	return -ENODEV;
}

static int qcom_slim_ngd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_slim_ngd_ctrl *ctrl = dev_get_drvdata(dev->parent);
	int ret;

	ctrl->ctrl.dev = dev;

	platform_set_drvdata(pdev, ctrl);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, QCOM_SLIM_NGD_AUTOSUSPEND);
	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_noresume(dev);
	ret = qcom_slim_ngd_qmi_svc_event_init(ctrl);
	if (ret) {
		dev_err(&pdev->dev, "QMI service registration failed:%d", ret);
		return ret;
	}

	INIT_WORK(&ctrl->m_work, qcom_slim_ngd_master_worker);
	INIT_WORK(&ctrl->ngd_up_work, qcom_slim_ngd_up_worker);
	ctrl->mwq = create_singlethread_workqueue("ngd_master");
	if (!ctrl->mwq) {
		dev_err(&pdev->dev, "Failed to start master worker\n");
		ret = -ENOMEM;
		goto wq_err;
	}

	return 0;
wq_err:
	qcom_slim_ngd_qmi_svc_event_deinit(&ctrl->qmi);
	if (ctrl->mwq)
		destroy_workqueue(ctrl->mwq);

	return ret;
}

static int qcom_slim_ngd_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_slim_ngd_ctrl *ctrl;
	int ret;
	struct pdr_service *pds;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	dev_set_drvdata(dev, ctrl);

	ctrl->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(dev, ret, qcom_slim_ngd_interrupt,
			       IRQF_TRIGGER_HIGH, "slim-ngd", ctrl);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "request IRQ failed\n");

	ctrl->nb.notifier_call = qcom_slim_ngd_ssr_notify;
	ctrl->notifier = qcom_register_ssr_notifier("lpass", &ctrl->nb);
	if (IS_ERR(ctrl->notifier))
		return PTR_ERR(ctrl->notifier);

	ctrl->dev = dev;
	ctrl->framer.rootfreq = SLIM_ROOT_FREQ >> 3;
	ctrl->framer.superfreq =
		ctrl->framer.rootfreq / SLIM_CL_PER_SUPERFRAME_DIV8;

	ctrl->ctrl.a_framer = &ctrl->framer;
	ctrl->ctrl.clkgear = SLIM_MAX_CLK_GEAR;
	ctrl->ctrl.get_laddr = qcom_slim_ngd_get_laddr;
	ctrl->ctrl.enable_stream = qcom_slim_ngd_enable_stream;
	ctrl->ctrl.xfer_msg = qcom_slim_ngd_xfer_msg;
	ctrl->ctrl.wakeup = NULL;
	ctrl->state = QCOM_SLIM_NGD_CTRL_DOWN;

	mutex_init(&ctrl->tx_lock);
	mutex_init(&ctrl->ssr_lock);
	spin_lock_init(&ctrl->tx_buf_lock);
	init_completion(&ctrl->reconf);
	init_completion(&ctrl->qmi.qmi_comp);
	init_completion(&ctrl->qmi_up);

	ctrl->pdr = pdr_handle_alloc(slim_pd_status, ctrl);
	if (IS_ERR(ctrl->pdr)) {
		ret = dev_err_probe(dev, PTR_ERR(ctrl->pdr),
				    "Failed to init PDR handle\n");
		goto err_pdr_alloc;
	}

	pds = pdr_add_lookup(ctrl->pdr, "avs/audio", "msm/adsp/audio_pd");
	if (IS_ERR(pds) && PTR_ERR(pds) != -EALREADY) {
		ret = dev_err_probe(dev, PTR_ERR(pds), "pdr add lookup failed\n");
		goto err_pdr_lookup;
	}

	platform_driver_register(&qcom_slim_ngd_driver);
	return of_qcom_slim_ngd_register(dev, ctrl);

err_pdr_alloc:
	qcom_unregister_ssr_notifier(ctrl->notifier, &ctrl->nb);

err_pdr_lookup:
	pdr_handle_release(ctrl->pdr);

	return ret;
}

static int qcom_slim_ngd_ctrl_remove(struct platform_device *pdev)
{
	platform_driver_unregister(&qcom_slim_ngd_driver);

	return 0;
}

static int qcom_slim_ngd_remove(struct platform_device *pdev)
{
	struct qcom_slim_ngd_ctrl *ctrl = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	pdr_handle_release(ctrl->pdr);
	qcom_unregister_ssr_notifier(ctrl->notifier, &ctrl->nb);
	qcom_slim_ngd_enable(ctrl, false);
	qcom_slim_ngd_exit_dma(ctrl);
	qcom_slim_ngd_qmi_svc_event_deinit(&ctrl->qmi);
	if (ctrl->mwq)
		destroy_workqueue(ctrl->mwq);

	kfree(ctrl->ngd);
	ctrl->ngd = NULL;
	return 0;
}

static int __maybe_unused qcom_slim_ngd_runtime_idle(struct device *dev)
{
	struct qcom_slim_ngd_ctrl *ctrl = dev_get_drvdata(dev);

	if (ctrl->state == QCOM_SLIM_NGD_CTRL_AWAKE)
		ctrl->state = QCOM_SLIM_NGD_CTRL_IDLE;
	pm_request_autosuspend(dev);
	return -EAGAIN;
}

static int __maybe_unused qcom_slim_ngd_runtime_suspend(struct device *dev)
{
	struct qcom_slim_ngd_ctrl *ctrl = dev_get_drvdata(dev);
	int ret = 0;

	qcom_slim_ngd_exit_dma(ctrl);
	if (!ctrl->qmi.handle)
		return 0;

	ret = qcom_slim_qmi_power_request(ctrl, false);
	if (ret && ret != -EBUSY)
		dev_info(ctrl->dev, "slim resource not idle:%d\n", ret);
	if (!ret || ret == -ETIMEDOUT)
		ctrl->state = QCOM_SLIM_NGD_CTRL_ASLEEP;

	return ret;
}

static const struct dev_pm_ops qcom_slim_ngd_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(
		qcom_slim_ngd_runtime_suspend,
		qcom_slim_ngd_runtime_resume,
		qcom_slim_ngd_runtime_idle
	)
};

static struct platform_driver qcom_slim_ngd_ctrl_driver = {
	.probe = qcom_slim_ngd_ctrl_probe,
	.remove = qcom_slim_ngd_ctrl_remove,
	.driver	= {
		.name = "qcom,slim-ngd-ctrl",
		.of_match_table = qcom_slim_ngd_dt_match,
	},
};

static struct platform_driver qcom_slim_ngd_driver = {
	.probe = qcom_slim_ngd_probe,
	.remove = qcom_slim_ngd_remove,
	.driver	= {
		.name = QCOM_SLIM_NGD_DRV_NAME,
		.pm = &qcom_slim_ngd_dev_pm_ops,
	},
};

module_platform_driver(qcom_slim_ngd_ctrl_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm SLIMBus NGD controller");
