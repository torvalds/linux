// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence MHDP8546 DP bridge driver.
 *
 * Copyright (C) 2020 Cadence Design Systems, Inc.
 *
 */

#include <linux/io.h>
#include <linux/iopoll.h>

#include <linux/unaligned.h>

#include <drm/display/drm_hdcp_helper.h>

#include "cdns-mhdp8546-hdcp.h"

static int cdns_mhdp_secure_mailbox_read(struct cdns_mhdp_device *mhdp)
{
	int ret, empty;

	WARN_ON(!mutex_is_locked(&mhdp->mbox_mutex));

	ret = readx_poll_timeout(readl, mhdp->sapb_regs + CDNS_MAILBOX_EMPTY,
				 empty, !empty, MAILBOX_RETRY_US,
				 MAILBOX_TIMEOUT_US);
	if (ret < 0)
		return ret;

	return readl(mhdp->sapb_regs + CDNS_MAILBOX_RX_DATA) & 0xff;
}

static int cdns_mhdp_secure_mailbox_write(struct cdns_mhdp_device *mhdp,
					  u8 val)
{
	int ret, full;

	WARN_ON(!mutex_is_locked(&mhdp->mbox_mutex));

	ret = readx_poll_timeout(readl, mhdp->sapb_regs + CDNS_MAILBOX_FULL,
				 full, !full, MAILBOX_RETRY_US,
				 MAILBOX_TIMEOUT_US);
	if (ret < 0)
		return ret;

	writel(val, mhdp->sapb_regs + CDNS_MAILBOX_TX_DATA);

	return 0;
}

static int cdns_mhdp_secure_mailbox_recv_header(struct cdns_mhdp_device *mhdp,
						u8 module_id,
						u8 opcode,
						u16 req_size)
{
	u32 mbox_size, i;
	u8 header[4];
	int ret;

	/* read the header of the message */
	for (i = 0; i < sizeof(header); i++) {
		ret = cdns_mhdp_secure_mailbox_read(mhdp);
		if (ret < 0)
			return ret;

		header[i] = ret;
	}

	mbox_size = get_unaligned_be16(header + 2);

	if (opcode != header[0] || module_id != header[1] ||
	    (opcode != HDCP_TRAN_IS_REC_ID_VALID && req_size != mbox_size)) {
		for (i = 0; i < mbox_size; i++)
			if (cdns_mhdp_secure_mailbox_read(mhdp) < 0)
				break;
		return -EINVAL;
	}

	return 0;
}

static int cdns_mhdp_secure_mailbox_recv_data(struct cdns_mhdp_device *mhdp,
					      u8 *buff, u16 buff_size)
{
	int ret;
	u32 i;

	for (i = 0; i < buff_size; i++) {
		ret = cdns_mhdp_secure_mailbox_read(mhdp);
		if (ret < 0)
			return ret;

		buff[i] = ret;
	}

	return 0;
}

static int cdns_mhdp_secure_mailbox_send(struct cdns_mhdp_device *mhdp,
					 u8 module_id,
					 u8 opcode,
					 u16 size,
					 u8 *message)
{
	u8 header[4];
	int ret;
	u32 i;

	header[0] = opcode;
	header[1] = module_id;
	put_unaligned_be16(size, header + 2);

	for (i = 0; i < sizeof(header); i++) {
		ret = cdns_mhdp_secure_mailbox_write(mhdp, header[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < size; i++) {
		ret = cdns_mhdp_secure_mailbox_write(mhdp, message[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int cdns_mhdp_hdcp_get_status(struct cdns_mhdp_device *mhdp,
				     u16 *hdcp_port_status)
{
	u8 hdcp_status[HDCP_STATUS_SIZE];
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_TX,
					    HDCP_TRAN_STATUS_CHANGE, 0, NULL);
	if (ret)
		goto err_get_hdcp_status;

	ret = cdns_mhdp_secure_mailbox_recv_header(mhdp, MB_MODULE_ID_HDCP_TX,
						   HDCP_TRAN_STATUS_CHANGE,
						   sizeof(hdcp_status));
	if (ret)
		goto err_get_hdcp_status;

	ret = cdns_mhdp_secure_mailbox_recv_data(mhdp, hdcp_status,
						 sizeof(hdcp_status));
	if (ret)
		goto err_get_hdcp_status;

	*hdcp_port_status = ((u16)(hdcp_status[0] << 8) | hdcp_status[1]);

err_get_hdcp_status:
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static u8 cdns_mhdp_hdcp_handle_status(struct cdns_mhdp_device *mhdp,
				       u16 status)
{
	u8 err = GET_HDCP_PORT_STS_LAST_ERR(status);

	if (err)
		dev_dbg(mhdp->dev, "HDCP Error = %d", err);

	return err;
}

static int cdns_mhdp_hdcp_rx_id_valid_response(struct cdns_mhdp_device *mhdp,
					       u8 valid)
{
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_TX,
					    HDCP_TRAN_RESPOND_RECEIVER_ID_VALID,
					    1, &valid);
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static int cdns_mhdp_hdcp_rx_id_valid(struct cdns_mhdp_device *mhdp,
				      u8 *recv_num, u8 *hdcp_rx_id)
{
	u8 rec_id_hdr[2];
	u8 status;
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_TX,
					    HDCP_TRAN_IS_REC_ID_VALID, 0, NULL);
	if (ret)
		goto err_rx_id_valid;

	ret = cdns_mhdp_secure_mailbox_recv_header(mhdp, MB_MODULE_ID_HDCP_TX,
						   HDCP_TRAN_IS_REC_ID_VALID,
						   sizeof(status));
	if (ret)
		goto err_rx_id_valid;

	ret = cdns_mhdp_secure_mailbox_recv_data(mhdp, rec_id_hdr, 2);
	if (ret)
		goto err_rx_id_valid;

	*recv_num = rec_id_hdr[0];

	ret = cdns_mhdp_secure_mailbox_recv_data(mhdp, hdcp_rx_id, 5 * *recv_num);

err_rx_id_valid:
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static int cdns_mhdp_hdcp_km_stored_resp(struct cdns_mhdp_device *mhdp,
					 u32 size, u8 *km)
{
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_TX,
					    HDCP2X_TX_RESPOND_KM, size, km);
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static int cdns_mhdp_hdcp_tx_is_km_stored(struct cdns_mhdp_device *mhdp,
					  u8 *resp, u32 size)
{
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_TX,
					    HDCP2X_TX_IS_KM_STORED, 0, NULL);
	if (ret)
		goto err_is_km_stored;

	ret = cdns_mhdp_secure_mailbox_recv_header(mhdp, MB_MODULE_ID_HDCP_TX,
						   HDCP2X_TX_IS_KM_STORED,
						   size);
	if (ret)
		goto err_is_km_stored;

	ret = cdns_mhdp_secure_mailbox_recv_data(mhdp, resp, size);
err_is_km_stored:
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static int cdns_mhdp_hdcp_tx_config(struct cdns_mhdp_device *mhdp,
				    u8 hdcp_cfg)
{
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_TX,
					    HDCP_TRAN_CONFIGURATION, 1, &hdcp_cfg);
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static int cdns_mhdp_hdcp_set_config(struct cdns_mhdp_device *mhdp,
				     u8 hdcp_config, bool enable)
{
	u16 hdcp_port_status;
	u32 ret_event;
	u8 hdcp_cfg;
	int ret;

	hdcp_cfg = hdcp_config | (enable ? 0x04 : 0) |
		   (HDCP_CONTENT_TYPE_0 << 3);
	cdns_mhdp_hdcp_tx_config(mhdp, hdcp_cfg);
	ret_event = cdns_mhdp_wait_for_sw_event(mhdp, CDNS_HDCP_TX_STATUS);
	if (!ret_event)
		return -1;

	ret = cdns_mhdp_hdcp_get_status(mhdp, &hdcp_port_status);
	if (ret || cdns_mhdp_hdcp_handle_status(mhdp, hdcp_port_status))
		return -1;

	return 0;
}

static int cdns_mhdp_hdcp_auth_check(struct cdns_mhdp_device *mhdp)
{
	u16 hdcp_port_status;
	u32 ret_event;
	int ret;

	ret_event = cdns_mhdp_wait_for_sw_event(mhdp, CDNS_HDCP_TX_STATUS);
	if (!ret_event)
		return -1;

	ret = cdns_mhdp_hdcp_get_status(mhdp, &hdcp_port_status);
	if (ret || cdns_mhdp_hdcp_handle_status(mhdp, hdcp_port_status))
		return -1;

	if (hdcp_port_status & 1) {
		dev_dbg(mhdp->dev, "Authentication completed successfully!\n");
		return 0;
	}

	dev_dbg(mhdp->dev, "Authentication failed\n");

	return -1;
}

static int cdns_mhdp_hdcp_check_receviers(struct cdns_mhdp_device *mhdp)
{
	u8 hdcp_rec_id[HDCP_MAX_RECEIVERS][HDCP_RECEIVER_ID_SIZE_BYTES];
	u8 hdcp_num_rec;
	u32 ret_event;

	ret_event = cdns_mhdp_wait_for_sw_event(mhdp,
						CDNS_HDCP_TX_IS_RCVR_ID_VALID);
	if (!ret_event)
		return -1;

	hdcp_num_rec = 0;
	memset(&hdcp_rec_id, 0, sizeof(hdcp_rec_id));
	cdns_mhdp_hdcp_rx_id_valid(mhdp, &hdcp_num_rec, (u8 *)hdcp_rec_id);
	cdns_mhdp_hdcp_rx_id_valid_response(mhdp, 1);

	return 0;
}

static int cdns_mhdp_hdcp_auth_22(struct cdns_mhdp_device *mhdp)
{
	u8 resp[HDCP_STATUS_SIZE];
	u16 hdcp_port_status;
	u32 ret_event;
	int ret;

	dev_dbg(mhdp->dev, "HDCP: Start 2.2 Authentication\n");
	ret_event = cdns_mhdp_wait_for_sw_event(mhdp,
						CDNS_HDCP2_TX_IS_KM_STORED);
	if (!ret_event)
		return -1;

	if (ret_event & CDNS_HDCP_TX_STATUS) {
		mhdp->sw_events &= ~CDNS_HDCP_TX_STATUS;
		ret = cdns_mhdp_hdcp_get_status(mhdp, &hdcp_port_status);
		if (ret || cdns_mhdp_hdcp_handle_status(mhdp, hdcp_port_status))
			return -1;
	}

	cdns_mhdp_hdcp_tx_is_km_stored(mhdp, resp, sizeof(resp));
	cdns_mhdp_hdcp_km_stored_resp(mhdp, 0, NULL);

	if (cdns_mhdp_hdcp_check_receviers(mhdp))
		return -1;

	return 0;
}

static inline int cdns_mhdp_hdcp_auth_14(struct cdns_mhdp_device *mhdp)
{
	dev_dbg(mhdp->dev, "HDCP: Starting 1.4 Authentication\n");
	return cdns_mhdp_hdcp_check_receviers(mhdp);
}

static int cdns_mhdp_hdcp_auth(struct cdns_mhdp_device *mhdp,
			       u8 hdcp_config)
{
	int ret;

	ret = cdns_mhdp_hdcp_set_config(mhdp, hdcp_config, true);
	if (ret)
		goto auth_failed;

	if (hdcp_config == HDCP_TX_1)
		ret = cdns_mhdp_hdcp_auth_14(mhdp);
	else
		ret = cdns_mhdp_hdcp_auth_22(mhdp);

	if (ret)
		goto auth_failed;

	ret = cdns_mhdp_hdcp_auth_check(mhdp);
	if (ret)
		ret = cdns_mhdp_hdcp_auth_check(mhdp);

auth_failed:
	return ret;
}

static int _cdns_mhdp_hdcp_disable(struct cdns_mhdp_device *mhdp)
{
	int ret;

	dev_dbg(mhdp->dev, "[%s:%d] HDCP is being disabled...\n",
		mhdp->connector.name, mhdp->connector.base.id);

	ret = cdns_mhdp_hdcp_set_config(mhdp, 0, false);

	return ret;
}

static int _cdns_mhdp_hdcp_enable(struct cdns_mhdp_device *mhdp, u8 content_type)
{
	int ret = -EINVAL;
	int tries = 3;
	u32 i;

	for (i = 0; i < tries; i++) {
		if (content_type == DRM_MODE_HDCP_CONTENT_TYPE0 ||
		    content_type == DRM_MODE_HDCP_CONTENT_TYPE1) {
			ret = cdns_mhdp_hdcp_auth(mhdp, HDCP_TX_2);
			if (!ret)
				return 0;
			_cdns_mhdp_hdcp_disable(mhdp);
		}

		if (content_type == DRM_MODE_HDCP_CONTENT_TYPE0) {
			ret = cdns_mhdp_hdcp_auth(mhdp, HDCP_TX_1);
			if (!ret)
				return 0;
			_cdns_mhdp_hdcp_disable(mhdp);
		}
	}

	dev_err(mhdp->dev, "HDCP authentication failed (%d tries/%d)\n",
		tries, ret);

	return ret;
}

static int cdns_mhdp_hdcp_check_link(struct cdns_mhdp_device *mhdp)
{
	u16 hdcp_port_status;
	int ret = 0;

	mutex_lock(&mhdp->hdcp.mutex);
	if (mhdp->hdcp.value == DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
		goto out;

	ret = cdns_mhdp_hdcp_get_status(mhdp, &hdcp_port_status);
	if (!ret && hdcp_port_status & HDCP_PORT_STS_AUTH)
		goto out;

	dev_err(mhdp->dev,
		"[%s:%d] HDCP link failed, retrying authentication\n",
		mhdp->connector.name, mhdp->connector.base.id);

	ret = _cdns_mhdp_hdcp_disable(mhdp);
	if (ret) {
		mhdp->hdcp.value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&mhdp->hdcp.prop_work);
		goto out;
	}

	ret = _cdns_mhdp_hdcp_enable(mhdp, mhdp->hdcp.hdcp_content_type);
	if (ret) {
		mhdp->hdcp.value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&mhdp->hdcp.prop_work);
	}
out:
	mutex_unlock(&mhdp->hdcp.mutex);
	return ret;
}

static void cdns_mhdp_hdcp_check_work(struct work_struct *work)
{
	struct delayed_work *d_work = to_delayed_work(work);
	struct cdns_mhdp_hdcp *hdcp = container_of(d_work,
						   struct cdns_mhdp_hdcp,
						   check_work);
	struct cdns_mhdp_device *mhdp = container_of(hdcp,
						     struct cdns_mhdp_device,
						     hdcp);

	if (!cdns_mhdp_hdcp_check_link(mhdp))
		schedule_delayed_work(&hdcp->check_work,
				      DRM_HDCP_CHECK_PERIOD_MS);
}

static void cdns_mhdp_hdcp_prop_work(struct work_struct *work)
{
	struct cdns_mhdp_hdcp *hdcp = container_of(work,
						   struct cdns_mhdp_hdcp,
						   prop_work);
	struct cdns_mhdp_device *mhdp = container_of(hdcp,
						     struct cdns_mhdp_device,
						     hdcp);
	struct drm_device *dev = mhdp->connector.dev;
	struct drm_connector_state *state;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	mutex_lock(&mhdp->hdcp.mutex);
	if (mhdp->hdcp.value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		state = mhdp->connector.state;
		state->content_protection = mhdp->hdcp.value;
	}
	mutex_unlock(&mhdp->hdcp.mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
}

int cdns_mhdp_hdcp_set_lc(struct cdns_mhdp_device *mhdp, u8 *val)
{
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_GENERAL,
					    HDCP_GENERAL_SET_LC_128,
					    16, val);
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

int
cdns_mhdp_hdcp_set_public_key_param(struct cdns_mhdp_device *mhdp,
				    struct cdns_hdcp_tx_public_key_param *val)
{
	int ret;

	mutex_lock(&mhdp->mbox_mutex);
	ret = cdns_mhdp_secure_mailbox_send(mhdp, MB_MODULE_ID_HDCP_TX,
					    HDCP2X_TX_SET_PUBLIC_KEY_PARAMS,
					    sizeof(*val), (u8 *)val);
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

int cdns_mhdp_hdcp_enable(struct cdns_mhdp_device *mhdp, u8 content_type)
{
	int ret;

	mutex_lock(&mhdp->hdcp.mutex);
	ret = _cdns_mhdp_hdcp_enable(mhdp, content_type);
	if (ret)
		goto out;

	mhdp->hdcp.hdcp_content_type = content_type;
	mhdp->hdcp.value = DRM_MODE_CONTENT_PROTECTION_ENABLED;
	schedule_work(&mhdp->hdcp.prop_work);
	schedule_delayed_work(&mhdp->hdcp.check_work,
			      DRM_HDCP_CHECK_PERIOD_MS);
out:
	mutex_unlock(&mhdp->hdcp.mutex);
	return ret;
}

int cdns_mhdp_hdcp_disable(struct cdns_mhdp_device *mhdp)
{
	int ret = 0;

	mutex_lock(&mhdp->hdcp.mutex);
	if (mhdp->hdcp.value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		mhdp->hdcp.value = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
		schedule_work(&mhdp->hdcp.prop_work);
		ret = _cdns_mhdp_hdcp_disable(mhdp);
	}
	mutex_unlock(&mhdp->hdcp.mutex);
	cancel_delayed_work_sync(&mhdp->hdcp.check_work);

	return ret;
}

void cdns_mhdp_hdcp_init(struct cdns_mhdp_device *mhdp)
{
	INIT_DELAYED_WORK(&mhdp->hdcp.check_work, cdns_mhdp_hdcp_check_work);
	INIT_WORK(&mhdp->hdcp.prop_work, cdns_mhdp_hdcp_prop_work);
	mutex_init(&mhdp->hdcp.mutex);
}
