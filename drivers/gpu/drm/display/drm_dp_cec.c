// SPDX-License-Identifier: GPL-2.0
/*
 * DisplayPort CEC-Tunneling-over-AUX support
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <media/cec.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>

/*
 * Unfortunately it turns out that we have a chicken-and-egg situation
 * here. Quite a few active (mini-)DP-to-HDMI or USB-C-to-HDMI adapters
 * have a converter chip that supports CEC-Tunneling-over-AUX (usually the
 * Parade PS176), but they do not wire up the CEC pin, thus making CEC
 * useless. Note that MegaChips 2900-based adapters appear to have good
 * support for CEC tunneling. Those adapters that I have tested using
 * this chipset all have the CEC line connected.
 *
 * Sadly there is no way for this driver to know this. What happens is
 * that a /dev/cecX device is created that is isolated and unable to see
 * any of the other CEC devices. Quite literally the CEC wire is cut
 * (or in this case, never connected in the first place).
 *
 * The reason so few adapters support this is that this tunneling protocol
 * was never supported by any OS. So there was no easy way of testing it,
 * and no incentive to correctly wire up the CEC pin.
 *
 * Hopefully by creating this driver it will be easier for vendors to
 * finally fix their adapters and test the CEC functionality.
 *
 * I keep a list of known working adapters here:
 *
 * https://hverkuil.home.xs4all.nl/cec-status.txt
 *
 * Please mail me (hverkuil@xs4all.nl) if you find an adapter that works
 * and is not yet listed there.
 *
 * Note that the current implementation does not support CEC over an MST hub.
 * As far as I can see there is no mechanism defined in the DisplayPort
 * standard to transport CEC interrupts over an MST device. It might be
 * possible to do this through polling, but I have not been able to get that
 * to work.
 */

/**
 * DOC: dp cec helpers
 *
 * These functions take care of supporting the CEC-Tunneling-over-AUX
 * feature of DisplayPort-to-HDMI adapters.
 */

/*
 * When the EDID is unset because the HPD went low, then the CEC DPCD registers
 * typically can no longer be read (true for a DP-to-HDMI adapter since it is
 * powered by the HPD). However, some displays toggle the HPD off and on for a
 * short period for one reason or another, and that would cause the CEC adapter
 * to be removed and added again, even though nothing else changed.
 *
 * This module parameter sets a delay in seconds before the CEC adapter is
 * actually unregistered. Only if the HPD does not return within that time will
 * the CEC adapter be unregistered.
 *
 * If it is set to a value >= NEVER_UNREG_DELAY, then the CEC adapter will never
 * be unregistered for as long as the connector remains registered.
 *
 * If it is set to 0, then the CEC adapter will be unregistered immediately as
 * soon as the HPD disappears.
 *
 * The default is one second to prevent short HPD glitches from unregistering
 * the CEC adapter.
 *
 * Note that for integrated HDMI branch devices that support CEC the DPCD
 * registers remain available even if the HPD goes low since it is not powered
 * by the HPD. In that case the CEC adapter will never be unregistered during
 * the life time of the connector. At least, this is the theory since I do not
 * have hardware with an integrated HDMI branch device that supports CEC.
 */
#define NEVER_UNREG_DELAY 1000
static unsigned int drm_dp_cec_unregister_delay = 1;
module_param(drm_dp_cec_unregister_delay, uint, 0600);
MODULE_PARM_DESC(drm_dp_cec_unregister_delay,
		 "CEC unregister delay in seconds, 0: no delay, >= 1000: never unregister");

static int drm_dp_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	u32 val = enable ? DP_CEC_TUNNELING_ENABLE : 0;
	ssize_t err = 0;

	err = drm_dp_dpcd_writeb(aux, DP_CEC_TUNNELING_CONTROL, val);
	return (enable && err < 0) ? err : 0;
}

static int drm_dp_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	/* Bit 15 (logical address 15) should always be set */
	u16 la_mask = 1 << CEC_LOG_ADDR_BROADCAST;
	u8 mask[2];
	ssize_t err;

	if (addr != CEC_LOG_ADDR_INVALID)
		la_mask |= adap->log_addrs.log_addr_mask | (1 << addr);
	mask[0] = la_mask & 0xff;
	mask[1] = la_mask >> 8;
	err = drm_dp_dpcd_write(aux, DP_CEC_LOGICAL_ADDRESS_MASK, mask, 2);
	return (addr != CEC_LOG_ADDR_INVALID && err < 0) ? err : 0;
}

static int drm_dp_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				    u32 signal_free_time, struct cec_msg *msg)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	unsigned int retries = min(5, attempts - 1);
	ssize_t err;

	err = drm_dp_dpcd_write(aux, DP_CEC_TX_MESSAGE_BUFFER,
				msg->msg, msg->len);
	if (err < 0)
		return err;

	err = drm_dp_dpcd_writeb(aux, DP_CEC_TX_MESSAGE_INFO,
				 (msg->len - 1) | (retries << 4) |
				 DP_CEC_TX_MESSAGE_SEND);
	return err < 0 ? err : 0;
}

static int drm_dp_cec_adap_monitor_all_enable(struct cec_adapter *adap,
					      bool enable)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	ssize_t err;
	u8 val;

	if (!(adap->capabilities & CEC_CAP_MONITOR_ALL))
		return 0;

	err = drm_dp_dpcd_readb(aux, DP_CEC_TUNNELING_CONTROL, &val);
	if (err >= 0) {
		if (enable)
			val |= DP_CEC_SNOOPING_ENABLE;
		else
			val &= ~DP_CEC_SNOOPING_ENABLE;
		err = drm_dp_dpcd_writeb(aux, DP_CEC_TUNNELING_CONTROL, val);
	}
	return (enable && err < 0) ? err : 0;
}

static void drm_dp_cec_adap_status(struct cec_adapter *adap,
				   struct seq_file *file)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	struct drm_dp_desc desc;
	struct drm_dp_dpcd_ident *id = &desc.ident;

	if (drm_dp_read_desc(aux, &desc, true))
		return;
	seq_printf(file, "OUI: %*phD\n",
		   (int)sizeof(id->oui), id->oui);
	seq_printf(file, "ID: %*pE\n",
		   (int)strnlen(id->device_id, sizeof(id->device_id)),
		   id->device_id);
	seq_printf(file, "HW Rev: %d.%d\n", id->hw_rev >> 4, id->hw_rev & 0xf);
	/*
	 * Show this both in decimal and hex: at least one vendor
	 * always reports this in hex.
	 */
	seq_printf(file, "FW/SW Rev: %d.%d (0x%02x.0x%02x)\n",
		   id->sw_major_rev, id->sw_minor_rev,
		   id->sw_major_rev, id->sw_minor_rev);
}

static const struct cec_adap_ops drm_dp_cec_adap_ops = {
	.adap_enable = drm_dp_cec_adap_enable,
	.adap_log_addr = drm_dp_cec_adap_log_addr,
	.adap_transmit = drm_dp_cec_adap_transmit,
	.adap_monitor_all_enable = drm_dp_cec_adap_monitor_all_enable,
	.adap_status = drm_dp_cec_adap_status,
};

static int drm_dp_cec_received(struct drm_dp_aux *aux)
{
	struct cec_adapter *adap = aux->cec.adap;
	struct cec_msg msg;
	u8 rx_msg_info;
	ssize_t err;

	err = drm_dp_dpcd_readb(aux, DP_CEC_RX_MESSAGE_INFO, &rx_msg_info);
	if (err < 0)
		return err;

	if (!(rx_msg_info & DP_CEC_RX_MESSAGE_ENDED))
		return 0;

	msg.len = (rx_msg_info & DP_CEC_RX_MESSAGE_LEN_MASK) + 1;
	err = drm_dp_dpcd_read(aux, DP_CEC_RX_MESSAGE_BUFFER, msg.msg, msg.len);
	if (err < 0)
		return err;

	cec_received_msg(adap, &msg);
	return 0;
}

static void drm_dp_cec_handle_irq(struct drm_dp_aux *aux)
{
	struct cec_adapter *adap = aux->cec.adap;
	u8 flags;

	if (drm_dp_dpcd_readb(aux, DP_CEC_TUNNELING_IRQ_FLAGS, &flags) < 0)
		return;

	if (flags & DP_CEC_RX_MESSAGE_INFO_VALID)
		drm_dp_cec_received(aux);

	if (flags & DP_CEC_TX_MESSAGE_SENT)
		cec_transmit_attempt_done(adap, CEC_TX_STATUS_OK);
	else if (flags & DP_CEC_TX_LINE_ERROR)
		cec_transmit_attempt_done(adap, CEC_TX_STATUS_ERROR |
						CEC_TX_STATUS_MAX_RETRIES);
	else if (flags &
		 (DP_CEC_TX_ADDRESS_NACK_ERROR | DP_CEC_TX_DATA_NACK_ERROR))
		cec_transmit_attempt_done(adap, CEC_TX_STATUS_NACK |
						CEC_TX_STATUS_MAX_RETRIES);
	drm_dp_dpcd_writeb(aux, DP_CEC_TUNNELING_IRQ_FLAGS, flags);
}

/**
 * drm_dp_cec_irq() - handle CEC interrupt, if any
 * @aux: DisplayPort AUX channel
 *
 * Should be called when handling an IRQ_HPD request. If CEC-tunneling-over-AUX
 * is present, then it will check for a CEC_IRQ and handle it accordingly.
 */
void drm_dp_cec_irq(struct drm_dp_aux *aux)
{
	u8 cec_irq;
	int ret;

	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;

	mutex_lock(&aux->cec.lock);
	if (!aux->cec.adap)
		goto unlock;

	ret = drm_dp_dpcd_readb(aux, DP_DEVICE_SERVICE_IRQ_VECTOR_ESI1,
				&cec_irq);
	if (ret < 0 || !(cec_irq & DP_CEC_IRQ))
		goto unlock;

	drm_dp_cec_handle_irq(aux);
	drm_dp_dpcd_writeb(aux, DP_DEVICE_SERVICE_IRQ_VECTOR_ESI1, DP_CEC_IRQ);
unlock:
	mutex_unlock(&aux->cec.lock);
}
EXPORT_SYMBOL(drm_dp_cec_irq);

static bool drm_dp_cec_cap(struct drm_dp_aux *aux, u8 *cec_cap)
{
	u8 cap = 0;

	if (drm_dp_dpcd_readb(aux, DP_CEC_TUNNELING_CAPABILITY, &cap) != 1 ||
	    !(cap & DP_CEC_TUNNELING_CAPABLE))
		return false;
	if (cec_cap)
		*cec_cap = cap;
	return true;
}

/*
 * Called if the HPD was low for more than drm_dp_cec_unregister_delay
 * seconds. This unregisters the CEC adapter.
 */
static void drm_dp_cec_unregister_work(struct work_struct *work)
{
	struct drm_dp_aux *aux = container_of(work, struct drm_dp_aux,
					      cec.unregister_work.work);

	mutex_lock(&aux->cec.lock);
	cec_unregister_adapter(aux->cec.adap);
	aux->cec.adap = NULL;
	mutex_unlock(&aux->cec.lock);
}

/*
 * A new EDID is set. If there is no CEC adapter, then create one. If
 * there was a CEC adapter, then check if the CEC adapter properties
 * were unchanged and just update the CEC physical address. Otherwise
 * unregister the old CEC adapter and create a new one.
 */
void drm_dp_cec_attach(struct drm_dp_aux *aux, u16 source_physical_address)
{
	struct drm_connector *connector = aux->cec.connector;
	u32 cec_caps = CEC_CAP_DEFAULTS | CEC_CAP_NEEDS_HPD |
		       CEC_CAP_CONNECTOR_INFO;
	struct cec_connector_info conn_info;
	unsigned int num_las = 1;
	u8 cap;

	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;

#ifndef CONFIG_MEDIA_CEC_RC
	/*
	 * CEC_CAP_RC is part of CEC_CAP_DEFAULTS, but it is stripped by
	 * cec_allocate_adapter() if CONFIG_MEDIA_CEC_RC is undefined.
	 *
	 * Do this here as well to ensure the tests against cec_caps are
	 * correct.
	 */
	cec_caps &= ~CEC_CAP_RC;
#endif
	cancel_delayed_work_sync(&aux->cec.unregister_work);

	mutex_lock(&aux->cec.lock);
	if (!drm_dp_cec_cap(aux, &cap)) {
		/* CEC is not supported, unregister any existing adapter */
		cec_unregister_adapter(aux->cec.adap);
		aux->cec.adap = NULL;
		goto unlock;
	}

	if (cap & DP_CEC_SNOOPING_CAPABLE)
		cec_caps |= CEC_CAP_MONITOR_ALL;
	if (cap & DP_CEC_MULTIPLE_LA_CAPABLE)
		num_las = CEC_MAX_LOG_ADDRS;

	if (aux->cec.adap) {
		if (aux->cec.adap->capabilities == cec_caps &&
		    aux->cec.adap->available_log_addrs == num_las) {
			/* Unchanged, so just set the phys addr */
			cec_s_phys_addr(aux->cec.adap, source_physical_address, false);
			goto unlock;
		}
		/*
		 * The capabilities changed, so unregister the old
		 * adapter first.
		 */
		cec_unregister_adapter(aux->cec.adap);
	}

	/* Create a new adapter */
	aux->cec.adap = cec_allocate_adapter(&drm_dp_cec_adap_ops,
					     aux, connector->name, cec_caps,
					     num_las);
	if (IS_ERR(aux->cec.adap)) {
		aux->cec.adap = NULL;
		goto unlock;
	}

	cec_fill_conn_info_from_drm(&conn_info, connector);
	cec_s_conn_info(aux->cec.adap, &conn_info);

	if (cec_register_adapter(aux->cec.adap, connector->dev->dev)) {
		cec_delete_adapter(aux->cec.adap);
		aux->cec.adap = NULL;
	} else {
		/*
		 * Update the phys addr for the new CEC adapter. When called
		 * from drm_dp_cec_register_connector() edid == NULL, so in
		 * that case the phys addr is just invalidated.
		 */
		cec_s_phys_addr(aux->cec.adap, source_physical_address, false);
	}
unlock:
	mutex_unlock(&aux->cec.lock);
}
EXPORT_SYMBOL(drm_dp_cec_attach);

/*
 * Note: Prefer calling drm_dp_cec_attach() with
 * connector->display_info.source_physical_address if possible.
 */
void drm_dp_cec_set_edid(struct drm_dp_aux *aux, const struct edid *edid)
{
	u16 pa = CEC_PHYS_ADDR_INVALID;

	if (edid && edid->extensions)
		pa = cec_get_edid_phys_addr((const u8 *)edid,
					    EDID_LENGTH * (edid->extensions + 1), NULL);

	drm_dp_cec_attach(aux, pa);
}
EXPORT_SYMBOL(drm_dp_cec_set_edid);

/*
 * The EDID disappeared (likely because of the HPD going down).
 */
void drm_dp_cec_unset_edid(struct drm_dp_aux *aux)
{
	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;

	cancel_delayed_work_sync(&aux->cec.unregister_work);

	mutex_lock(&aux->cec.lock);
	if (!aux->cec.adap)
		goto unlock;

	cec_phys_addr_invalidate(aux->cec.adap);
	/*
	 * We're done if we want to keep the CEC device
	 * (drm_dp_cec_unregister_delay is >= NEVER_UNREG_DELAY) or if the
	 * DPCD still indicates the CEC capability (expected for an integrated
	 * HDMI branch device).
	 */
	if (drm_dp_cec_unregister_delay < NEVER_UNREG_DELAY &&
	    !drm_dp_cec_cap(aux, NULL)) {
		/*
		 * Unregister the CEC adapter after drm_dp_cec_unregister_delay
		 * seconds. This to debounce short HPD off-and-on cycles from
		 * displays.
		 */
		schedule_delayed_work(&aux->cec.unregister_work,
				      drm_dp_cec_unregister_delay * HZ);
	}
unlock:
	mutex_unlock(&aux->cec.lock);
}
EXPORT_SYMBOL(drm_dp_cec_unset_edid);

/**
 * drm_dp_cec_register_connector() - register a new connector
 * @aux: DisplayPort AUX channel
 * @connector: drm connector
 *
 * A new connector was registered with associated CEC adapter name and
 * CEC adapter parent device. After registering the name and parent
 * drm_dp_cec_set_edid() is called to check if the connector supports
 * CEC and to register a CEC adapter if that is the case.
 */
void drm_dp_cec_register_connector(struct drm_dp_aux *aux,
				   struct drm_connector *connector)
{
	WARN_ON(aux->cec.adap);
	if (WARN_ON(!aux->transfer))
		return;
	aux->cec.connector = connector;
	INIT_DELAYED_WORK(&aux->cec.unregister_work,
			  drm_dp_cec_unregister_work);
}
EXPORT_SYMBOL(drm_dp_cec_register_connector);

/**
 * drm_dp_cec_unregister_connector() - unregister the CEC adapter, if any
 * @aux: DisplayPort AUX channel
 */
void drm_dp_cec_unregister_connector(struct drm_dp_aux *aux)
{
	if (!aux->cec.adap)
		return;
	cancel_delayed_work_sync(&aux->cec.unregister_work);
	cec_unregister_adapter(aux->cec.adap);
	aux->cec.adap = NULL;
}
EXPORT_SYMBOL(drm_dp_cec_unregister_connector);
