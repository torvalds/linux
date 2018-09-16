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
#include <linux/of.h>
#include <linux/io.h>
#include <linux/soc/qcom/qmi.h>
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
	struct workqueue_struct *mwq;
	spinlock_t tx_buf_lock;
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

static struct qmi_elem_info slimbus_select_inst_req_msg_v01_ei[] = {
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

static struct qmi_elem_info slimbus_select_inst_resp_msg_v01_ei[] = {
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

static struct qmi_elem_info slimbus_power_req_msg_v01_ei[] = {
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

static struct qmi_elem_info slimbus_power_resp_msg_v01_ei[] = {
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

static struct qmi_msg_handler qcom_slim_qmi_msg_handlers[] = {
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
		pm_runtime_mark_last_busy(ctrl->dev);
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

	ctrl->dma_rx_channel = dma_request_slave_channel(dev, "rx");
	if (!ctrl->dma_rx_channel) {
		dev_err(dev, "Failed to request dma channels");
		return -EINVAL;
	}

	size = QCOM_SLIM_NGD_DESC_NUM * SLIM_MSGQ_BUF_LEN;
	ctrl->rx_base = dma_alloc_coherent(dev, size, &ctrl->rx_phys_base,
					   GFP_KERNEL);
	if (!ctrl->rx_base) {
		dev_err(dev, "dma_alloc_coherent failed\n");
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

	ctrl->dma_tx_channel = dma_request_slave_channel(dev, "tx");
	if (!ctrl->dma_tx_channel) {
		dev_err(dev, "Failed to request dma channels");
		return -EINVAL;
	}

	size = ((QCOM_SLIM_NGD_DESC_NUM + 1) * SLIM_MSGQ_BUF_LEN);
	ctrl->tx_base = dma_alloc_coherent(dev, size, &ctrl->tx_phys_base,
					   GFP_KERNEL);
	if (!ctrl->tx_base) {
		dev_err(dev, "dma_alloc_coherent failed\n");
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
	u32 stat = readl(base + NGD_INT_STAT);

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

	if (txn->mc & SLIM_MSG_CLK_PAUSE_SEQ_FLG)
		return -EPROTONOSUPPORT;

	if (txn->mt == SLIM_MSG_MT_CORE &&
		(txn->mc >= SLIM_MSG_MC_BEGIN_RECONFIGURATION &&
		 txn->mc <= SLIM_MSG_MC_RECONFIGURE_NOW))
		return 0;

	if (txn->dt == SLIM_MSG_DEST_ENUMADDR)
		return -EPROTONOSUPPORT;

	if (txn->msg->num_bytes > SLIM_MSGQ_BUF_LEN ||
			txn->rl > SLIM_MSGQ_BUF_LEN) {
		dev_err(ctrl->dev, "msg exeeds HW limit\n");
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

	ret = qcom_slim_ngd_tx_msg_post(ctrl, pbuf, txn->rl);
	if (ret)
		return ret;

	timeout = wait_for_completion_timeout(&tx_sent, HZ);
	if (!timeout) {
		dev_err(sctrl->dev, "TX timed out:MC:0x%x,mt:0x%x", txn->mc,
					txn->mt);
		return -ETIMEDOUT;
	}

	if (usr_msg) {
		timeout = wait_for_completion_timeout(&done, HZ);
		if (!timeout) {
			dev_err(sctrl->dev, "TX timed out:MC:0x%x,mt:0x%x",
				txn->mc, txn->mt);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int qcom_slim_ngd_xfer_msg_sync(struct slim_controller *ctrl,
				       struct slim_msg_txn *txn)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int ret, timeout;

	pm_runtime_get_sync(ctrl->dev);

	txn->comp = &done;

	ret = qcom_slim_ngd_xfer_msg(ctrl, txn);
	if (ret)
		return ret;

	timeout = wait_for_completion_timeout(&done, HZ);
	if (!timeout) {
		dev_err(ctrl->dev, "TX timed out:MC:0x%x,mt:0x%x", txn->mc,
				txn->mt);
		return -ETIMEDOUT;
	}
	return 0;
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
			int seg_interval = SLIM_SLOTS_PER_SUPERFRAME/rt->ratem;
			int exp;

			wbuf[txn.msg->num_bytes++] = sdev->laddr;
			wbuf[txn.msg->num_bytes] = rt->bps >> 2 |
						   (port->ch.aux_fmt << 6);

			/* Data channel segment interval not multiple of 3 */
			exp = seg_interval % 3;
			if (exp)
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

	if (ctrl->state == QCOM_SLIM_NGD_CTRL_DOWN)
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
		return 0;
	}

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

static int qcom_slim_ngd_runtime_resume(struct device *dev)
{
	struct qcom_slim_ngd_ctrl *ctrl = dev_get_drvdata(dev);
	int ret = 0;

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
		if (!pm_runtime_enabled(ctrl->dev) ||
				!pm_runtime_suspended(ctrl->dev))
			qcom_slim_ngd_runtime_resume(ctrl->dev);
		else
			pm_runtime_resume(ctrl->dev);
		pm_runtime_mark_last_busy(ctrl->dev);
		pm_runtime_put(ctrl->dev);

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

	qcom_slim_ngd_enable(ctrl, true);

	return 0;
}

static void qcom_slim_ngd_qmi_del_server(struct qmi_handle *hdl,
					 struct qmi_service *service)
{
	struct qcom_slim_ngd_qmi *qmi =
		container_of(hdl, struct qcom_slim_ngd_qmi, svc_event_hdl);

	qmi->svc_info.sq_node = 0;
	qmi->svc_info.sq_port = 0;
}

static struct qmi_ops qcom_slim_ngd_qmi_svc_event_ops = {
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
	},
	{}
};

MODULE_DEVICE_TABLE(of, qcom_slim_ngd_dt_match);

static int of_qcom_slim_ngd_register(struct device *parent,
				     struct qcom_slim_ngd_ctrl *ctrl)
{
	const struct ngd_reg_offset_data *data;
	struct qcom_slim_ngd *ngd;
	struct device_node *node;
	u32 id;

	data = of_match_node(qcom_slim_ngd_dt_match, parent->of_node)->data;

	for_each_available_child_of_node(parent->of_node, node) {
		if (of_property_read_u32(node, "reg", &id))
			continue;

		ngd = kzalloc(sizeof(*ngd), GFP_KERNEL);
		if (!ngd)
			return -ENOMEM;

		ngd->pdev = platform_device_alloc(QCOM_SLIM_NGD_DRV_NAME, id);
		if (!ngd->pdev) {
			kfree(ngd);
			return -ENOMEM;
		}
		ngd->id = id;
		ngd->pdev->dev.parent = parent;
		ngd->pdev->driver_override = QCOM_SLIM_NGD_DRV_NAME;
		ngd->pdev->dev.of_node = node;
		ctrl->ngd = ngd;
		platform_set_drvdata(ngd->pdev, ctrl);

		platform_device_add(ngd->pdev);
		ngd->base = ctrl->base + ngd->id * data->offset +
					(ngd->id - 1) * data->size;
		ctrl->ngd = ngd;

		return 0;
	}

	return -ENODEV;
}

static int qcom_slim_ngd_probe(struct platform_device *pdev)
{
	struct qcom_slim_ngd_ctrl *ctrl = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret;

	ctrl->ctrl.dev = dev;

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
	struct resource *res;
	int ret;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	dev_set_drvdata(dev, ctrl);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctrl->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no slimbus IRQ resource\n");
		return -ENODEV;
	}

	ret = devm_request_irq(dev, res->start, qcom_slim_ngd_interrupt,
			       IRQF_TRIGGER_HIGH, "slim-ngd", ctrl);
	if (ret) {
		dev_err(&pdev->dev, "request IRQ failed\n");
		return ret;
	}

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

	spin_lock_init(&ctrl->tx_buf_lock);
	init_completion(&ctrl->reconf);
	init_completion(&ctrl->qmi.qmi_comp);

	platform_driver_register(&qcom_slim_ngd_driver);
	return of_qcom_slim_ngd_register(dev, ctrl);
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
