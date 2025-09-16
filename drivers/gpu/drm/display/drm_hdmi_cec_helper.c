// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2024 Linaro Ltd
 */

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_managed.h>
#include <drm/display/drm_hdmi_cec_helper.h>

#include <linux/export.h>
#include <linux/mutex.h>

#include <media/cec.h>

struct drm_connector_hdmi_cec_data {
	struct cec_adapter *adapter;
	const struct drm_connector_hdmi_cec_funcs *funcs;
};

static int drm_connector_hdmi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct drm_connector *connector = cec_get_drvdata(adap);
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	return data->funcs->enable(connector, enable);
}

static int drm_connector_hdmi_cec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct drm_connector *connector = cec_get_drvdata(adap);
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	return data->funcs->log_addr(connector, logical_addr);
}

static int drm_connector_hdmi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
						u32 signal_free_time, struct cec_msg *msg)
{
	struct drm_connector *connector = cec_get_drvdata(adap);
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	return data->funcs->transmit(connector, attempts, signal_free_time, msg);
}

static const struct cec_adap_ops drm_connector_hdmi_cec_adap_ops = {
	.adap_enable = drm_connector_hdmi_cec_adap_enable,
	.adap_log_addr = drm_connector_hdmi_cec_adap_log_addr,
	.adap_transmit = drm_connector_hdmi_cec_adap_transmit,
};

static void drm_connector_hdmi_cec_adapter_phys_addr_invalidate(struct drm_connector *connector)
{
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	cec_phys_addr_invalidate(data->adapter);
}

static void drm_connector_hdmi_cec_adapter_phys_addr_set(struct drm_connector *connector,
							 u16 addr)
{
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	cec_s_phys_addr(data->adapter, addr, false);
}

static void drm_connector_hdmi_cec_adapter_unregister(struct drm_device *dev, void *res)
{
	struct drm_connector *connector = res;
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	cec_unregister_adapter(data->adapter);

	if (data->funcs->uninit)
		data->funcs->uninit(connector);

	kfree(data);
	connector->cec.data = NULL;
}

static struct drm_connector_cec_funcs drm_connector_hdmi_cec_adapter_funcs = {
	.phys_addr_invalidate = drm_connector_hdmi_cec_adapter_phys_addr_invalidate,
	.phys_addr_set = drm_connector_hdmi_cec_adapter_phys_addr_set,
};

int drmm_connector_hdmi_cec_register(struct drm_connector *connector,
				     const struct drm_connector_hdmi_cec_funcs *funcs,
				     const char *name,
				     u8 available_las,
				     struct device *dev)
{
	struct drm_connector_hdmi_cec_data *data;
	struct cec_connector_info conn_info;
	struct cec_adapter *cec_adap;
	int ret;

	if (!funcs->init || !funcs->enable || !funcs->log_addr || !funcs->transmit)
		return -EINVAL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->funcs = funcs;

	cec_adap = cec_allocate_adapter(&drm_connector_hdmi_cec_adap_ops, connector, name,
					CEC_CAP_DEFAULTS | CEC_CAP_CONNECTOR_INFO,
					available_las ? : CEC_MAX_LOG_ADDRS);
	ret = PTR_ERR_OR_ZERO(cec_adap);
	if (ret < 0)
		goto err_free;

	cec_fill_conn_info_from_drm(&conn_info, connector);
	cec_s_conn_info(cec_adap, &conn_info);

	data->adapter = cec_adap;

	mutex_lock(&connector->cec.mutex);

	connector->cec.data = data;
	connector->cec.funcs = &drm_connector_hdmi_cec_adapter_funcs;

	ret = funcs->init(connector);
	if (ret < 0)
		goto err_delete_adapter;

	/*
	 * NOTE: the CEC adapter will be unregistered by drmm cleanup from
	 * drm_managed_release(), which is called from drm_dev_release()
	 * during device unbind.
	 *
	 * However, the CEC framework cleans up the CEC adapter only when the
	 * last user has closed its file descriptor, so we don't need to handle
	 * it in DRM.
	 *
	 * Before that CEC framework makes sure that even if the userspace
	 * still holds CEC device open, all calls will be shortcut via
	 * cec_is_registered(), making sure that there is no access to the
	 * freed memory.
	 */
	ret = cec_register_adapter(cec_adap, dev);
	if (ret < 0)
		goto err_delete_adapter;

	mutex_unlock(&connector->cec.mutex);

	return drmm_add_action_or_reset(connector->dev,
					drm_connector_hdmi_cec_adapter_unregister,
					connector);

err_delete_adapter:
	cec_delete_adapter(cec_adap);

	connector->cec.data = NULL;

	mutex_unlock(&connector->cec.mutex);

err_free:
	kfree(data);

	return ret;
}
EXPORT_SYMBOL(drmm_connector_hdmi_cec_register);

void drm_connector_hdmi_cec_received_msg(struct drm_connector *connector,
					 struct cec_msg *msg)
{
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	cec_received_msg(data->adapter, msg);
}
EXPORT_SYMBOL(drm_connector_hdmi_cec_received_msg);

void drm_connector_hdmi_cec_transmit_attempt_done(struct drm_connector *connector,
						  u8 status)
{
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	cec_transmit_attempt_done(data->adapter, status);
}
EXPORT_SYMBOL(drm_connector_hdmi_cec_transmit_attempt_done);

void drm_connector_hdmi_cec_transmit_done(struct drm_connector *connector,
					  u8 status,
					  u8 arb_lost_cnt, u8 nack_cnt,
					  u8 low_drive_cnt, u8 error_cnt)
{
	struct drm_connector_hdmi_cec_data *data = connector->cec.data;

	cec_transmit_done(data->adapter, status,
			  arb_lost_cnt, nack_cnt, low_drive_cnt, error_cnt);
}
EXPORT_SYMBOL(drm_connector_hdmi_cec_transmit_done);
