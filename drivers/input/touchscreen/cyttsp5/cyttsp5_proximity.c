/*
 * cyttsp5_proximity.c
 * Parade TrueTouch(TM) Standard Product V5 Proximity Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2013-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 *
 */

#include "cyttsp5_regs.h"

#define CYTTSP5_PROXIMITY_NAME "cyttsp5_proximity"

/* Timeout value in ms. */
#define CYTTSP5_PROXIMITY_REQUEST_EXCLUSIVE_TIMEOUT		1000

#define CYTTSP5_PROXIMITY_ON 0
#define CYTTSP5_PROXIMITY_OFF 1

static inline struct cyttsp5_proximity_data *get_prox_data(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);

	return &cd->pd;
}

static void cyttsp5_report_proximity(struct cyttsp5_proximity_data *pd,
	bool on)
{
	int val = on ? CYTTSP5_PROXIMITY_ON : CYTTSP5_PROXIMITY_OFF;

	input_report_abs(pd->input, ABS_DISTANCE, val);
	input_sync(pd->input);
}

static void cyttsp5_get_touch_axis(struct cyttsp5_proximity_data *pd,
	int *axis, int size, int max, u8 *xy_data, int bofs)
{
	int nbyte;
	int next;

	for (nbyte = 0, *axis = 0, next = 0; nbyte < size; nbyte++) {
		parade_debug(pd->dev, DEBUG_LEVEL_2,
			"%s: *axis=%02X(%d) size=%d max=%08X xy_data=%p xy_data[%d]=%02X(%d) bofs=%d\n",
			__func__, *axis, *axis, size, max, xy_data, next,
			xy_data[next], xy_data[next], bofs);
		*axis = *axis + ((xy_data[next] >> bofs) << (nbyte * 8));
		next++;
	}

	*axis &= max - 1;

	parade_debug(pd->dev, DEBUG_LEVEL_2,
		"%s: *axis=%02X(%d) size=%d max=%08X xy_data=%p xy_data[%d]=%02X(%d)\n",
		__func__, *axis, *axis, size, max, xy_data, next,
		xy_data[next], xy_data[next]);
}

static void cyttsp5_get_touch_hdr(struct cyttsp5_proximity_data *pd,
	struct cyttsp5_touch *touch, u8 *xy_mode)
{
	struct device *dev = pd->dev;
	struct cyttsp5_sysinfo *si = pd->si;
	enum cyttsp5_tch_hdr hdr;

	for (hdr = CY_TCH_TIME; hdr < CY_TCH_NUM_HDR; hdr++) {
		if (!si->tch_hdr[hdr].report)
			continue;
		cyttsp5_get_touch_axis(pd, &touch->hdr[hdr],
			si->tch_hdr[hdr].size,
			si->tch_hdr[hdr].max,
			xy_mode + si->tch_hdr[hdr].ofs,
			si->tch_hdr[hdr].bofs);
		parade_debug(dev, DEBUG_LEVEL_2, "%s: get %s=%04X(%d)\n",
			__func__, cyttsp5_tch_hdr_string[hdr],
			touch->hdr[hdr], touch->hdr[hdr]);
	}
}

static void cyttsp5_get_touch(struct cyttsp5_proximity_data *pd,
	struct cyttsp5_touch *touch, u8 *xy_data)
{
	struct device *dev = pd->dev;
	struct cyttsp5_sysinfo *si = pd->si;
	enum cyttsp5_tch_abs abs;

	for (abs = CY_TCH_X; abs < CY_TCH_NUM_ABS; abs++) {
		if (!si->tch_abs[abs].report)
			continue;
		cyttsp5_get_touch_axis(pd, &touch->abs[abs],
			si->tch_abs[abs].size,
			si->tch_abs[abs].max,
			xy_data + si->tch_abs[abs].ofs,
			si->tch_abs[abs].bofs);
		parade_debug(dev, DEBUG_LEVEL_2, "%s: get %s=%04X(%d)\n",
			__func__, cyttsp5_tch_abs_string[abs],
			touch->abs[abs], touch->abs[abs]);
	}

	parade_debug(dev, DEBUG_LEVEL_2, "%s: x=%04X(%d) y=%04X(%d)\n",
		__func__, touch->abs[CY_TCH_X], touch->abs[CY_TCH_X],
		touch->abs[CY_TCH_Y], touch->abs[CY_TCH_Y]);
}

static void cyttsp5_get_proximity_touch(struct cyttsp5_proximity_data *pd,
		struct cyttsp5_touch *tch, int num_cur_tch)
{
	struct cyttsp5_sysinfo *si = pd->si;
	int i;

	for (i = 0; i < num_cur_tch; i++) {
		cyttsp5_get_touch(pd, tch, si->xy_data +
			(i * si->desc.tch_record_size));

		/* Check for proximity event */
		if (tch->abs[CY_TCH_O] == CY_OBJ_PROXIMITY) {
			if (tch->abs[CY_TCH_E] == CY_EV_TOUCHDOWN)
				cyttsp5_report_proximity(pd, true);
			else if (tch->abs[CY_TCH_E] == CY_EV_LIFTOFF)
				cyttsp5_report_proximity(pd, false);
			break;
		}
	}
}

/* read xy_data for all current touches */
static int cyttsp5_xy_worker(struct cyttsp5_proximity_data *pd)
{
	struct device *dev = pd->dev;
	struct cyttsp5_sysinfo *si = pd->si;
	struct cyttsp5_touch tch;
	u8 num_cur_tch;

	cyttsp5_get_touch_hdr(pd, &tch, si->xy_mode + 3);

	num_cur_tch = tch.hdr[CY_TCH_NUM];
	if (num_cur_tch > si->sensing_conf_data.max_tch) {
		dev_err(dev, "%s: Num touch err detected (n=%d)\n",
			__func__, num_cur_tch);
		num_cur_tch = si->sensing_conf_data.max_tch;
	}

	if (tch.hdr[CY_TCH_LO])
		parade_debug(dev, DEBUG_LEVEL_1, "%s: Large area detected\n",
		__func__);

	/* extract xy_data for all currently reported touches */
	parade_debug(dev, DEBUG_LEVEL_2, "%s: extract data num_cur_rec=%d\n",
		__func__, num_cur_tch);
	if (num_cur_tch)
		cyttsp5_get_proximity_touch(pd, &tch, num_cur_tch);
	else
		cyttsp5_report_proximity(pd, false);

	return 0;
}

static int cyttsp5_proximity_attention(struct device *dev)
{
	struct cyttsp5_proximity_data *pd = get_prox_data(dev);
	int rc = 0;

	if (pd->si->xy_mode[2] != pd->si->desc.tch_report_id)
		return 0;

	mutex_lock(&pd->prox_lock);
	rc = cyttsp5_xy_worker(pd);
	mutex_unlock(&pd->prox_lock);
	if (rc < 0)
		dev_err(dev, "%s: xy_worker error r=%d\n", __func__, rc);

	return rc;
}

static int cyttsp5_startup_attention(struct device *dev)
{
	struct cyttsp5_proximity_data *pd = get_prox_data(dev);

	mutex_lock(&pd->prox_lock);
	cyttsp5_report_proximity(pd, false);
	mutex_unlock(&pd->prox_lock);

	return 0;
}

static int _cyttsp5_set_proximity_via_touchmode_enabled(
		struct cyttsp5_proximity_data *pd, bool enable)
{
	struct device *dev = pd->dev;
	u32 touchmode_enabled;
	int rc;

	rc = cyttsp5_request_nonhid_get_param(dev, 0,
			CY_RAM_ID_TOUCHMODE_ENABLED, &touchmode_enabled);
	if (rc)
		return rc;

	if (enable)
		touchmode_enabled |= 0x80;
	else
		touchmode_enabled &= 0x7F;

	rc = cyttsp5_request_nonhid_set_param(dev, 0,
			CY_RAM_ID_TOUCHMODE_ENABLED, touchmode_enabled,
			CY_RAM_ID_TOUCHMODE_ENABLED_SIZE);

	return rc;
}

static int _cyttsp5_set_proximity_via_proximity_enable(
		struct cyttsp5_proximity_data *pd, bool enable)
{
	struct device *dev = pd->dev;
	u32 proximity_enable;
	int rc;

	rc = cyttsp5_request_nonhid_get_param(dev, 0,
			CY_RAM_ID_PROXIMITY_ENABLE, &proximity_enable);
	if (rc)
		return rc;

	if (enable)
		proximity_enable |= 0x01;
	else
		proximity_enable &= 0xFE;

	rc = cyttsp5_request_nonhid_set_param(dev, 0,
			CY_RAM_ID_PROXIMITY_ENABLE, proximity_enable,
			CY_RAM_ID_PROXIMITY_ENABLE_SIZE);

	return rc;
}

static int _cyttsp5_set_proximity(struct cyttsp5_proximity_data *pd,
		bool enable)
{
	if (!IS_PIP_VER_GE(pd->si, 1, 4))
		return _cyttsp5_set_proximity_via_touchmode_enabled(pd,
				enable);

	return _cyttsp5_set_proximity_via_proximity_enable(pd, enable);
}

static int _cyttsp5_proximity_enable(struct cyttsp5_proximity_data *pd)
{
	struct device *dev = pd->dev;
	int rc = 0;

	pm_runtime_get_sync(dev);

	rc = cyttsp5_request_exclusive(dev,
			CYTTSP5_PROXIMITY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto exit;
	}

	rc = _cyttsp5_set_proximity(pd, true);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request enable proximity scantype r=%d\n",
				__func__, rc);
		goto exit_release;
	}

	parade_debug(dev, DEBUG_LEVEL_2, "%s: setup subscriptions\n", __func__);

	/* set up touch call back */
	_cyttsp5_subscribe_attention(dev, CY_ATTEN_IRQ, CYTTSP5_PROXIMITY_NAME,
		cyttsp5_proximity_attention, CY_MODE_OPERATIONAL);

	/* set up startup call back */
	_cyttsp5_subscribe_attention(dev, CY_ATTEN_STARTUP,
		CYTTSP5_PROXIMITY_NAME, cyttsp5_startup_attention, 0);

exit_release:
	cyttsp5_release_exclusive(dev);
exit:
	return rc;
}

static int _cyttsp5_proximity_disable(struct cyttsp5_proximity_data *pd,
		bool force)
{
	struct device *dev = pd->dev;
	int rc = 0;

	rc = cyttsp5_request_exclusive(dev,
			CYTTSP5_PROXIMITY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto exit;
	}

	rc = _cyttsp5_set_proximity(pd, false);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request disable proximity scan r=%d\n",
				__func__, rc);
		goto exit_release;
	}

exit_release:
	cyttsp5_release_exclusive(dev);

exit:
	if (!rc || force) {
		_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_IRQ,
			CYTTSP5_PROXIMITY_NAME, cyttsp5_proximity_attention,
			CY_MODE_OPERATIONAL);

		_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_PROXIMITY_NAME, cyttsp5_startup_attention, 0);
	}

	pm_runtime_put(dev);

	return rc;
}

static ssize_t cyttsp5_proximity_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_proximity_data *pd = get_prox_data(dev);
	int val = 0;

	mutex_lock(&pd->sysfs_lock);
	val = pd->enable_count;
	mutex_unlock(&pd->sysfs_lock);

	return scnprintf(buf, CY_MAX_PRBUF_SIZE, "%d\n", val);
}

static ssize_t cyttsp5_proximity_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_proximity_data *pd = get_prox_data(dev);
	unsigned long value;
	int rc;

	rc = kstrtoul(buf, 10, &value);
	if (rc < 0 || (value != 0 && value != 1)) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&pd->sysfs_lock);
	if (value) {
		if (pd->enable_count++) {
			parade_debug(dev, DEBUG_LEVEL_2, "%s: '%s' already enabled\n",
				__func__, pd->input->name);
		} else {
			rc = _cyttsp5_proximity_enable(pd);
			if (rc)
				pd->enable_count--;
		}
	} else {
		if (--pd->enable_count) {
			if (pd->enable_count < 0) {
				dev_err(dev, "%s: '%s' unbalanced disable\n",
					__func__, pd->input->name);
				pd->enable_count = 0;
			}
		} else {
			rc = _cyttsp5_proximity_disable(pd, false);
			if (rc)
				pd->enable_count++;
		}
	}
	mutex_unlock(&pd->sysfs_lock);

	if (rc)
		return rc;

	return size;
}

static DEVICE_ATTR(prox_enable, S_IRUSR | S_IWUSR,
		cyttsp5_proximity_enable_show,
		cyttsp5_proximity_enable_store);

static int cyttsp5_setup_input_device_and_sysfs(struct device *dev)
{
	struct cyttsp5_proximity_data *pd = get_prox_data(dev);
	int signal = CY_IGNORE_VALUE;
	int i;
	int rc;

	rc = device_create_file(dev, &dev_attr_prox_enable);
	if (rc) {
		dev_err(dev, "%s: Error, could not create enable\n",
				__func__);
		goto exit;
	}

	parade_debug(dev, DEBUG_LEVEL_2, "%s: Initialize event signals\n",
				__func__);

	__set_bit(EV_ABS, pd->input->evbit);

	/* set event signal capabilities */
	for (i = 0; i < NUM_SIGNALS(pd->pdata->frmwrk); i++) {
		signal = PARAM_SIGNAL(pd->pdata->frmwrk, i);
		if (signal != CY_IGNORE_VALUE) {
			input_set_abs_params(pd->input, signal,
				PARAM_MIN(pd->pdata->frmwrk, i),
				PARAM_MAX(pd->pdata->frmwrk, i),
				PARAM_FUZZ(pd->pdata->frmwrk, i),
				PARAM_FLAT(pd->pdata->frmwrk, i));
		}
	}

	rc = input_register_device(pd->input);
	if (rc) {
		dev_err(dev, "%s: Error, failed register input device r=%d\n",
			__func__, rc);
		goto unregister_enable;
	}

	pd->input_device_registered = true;
	return rc;

unregister_enable:
	device_remove_file(dev, &dev_attr_prox_enable);
exit:
	return rc;
}

static int cyttsp5_setup_input_attention(struct device *dev)
{
	struct cyttsp5_proximity_data *pd = get_prox_data(dev);
	int rc;

	pd->si = _cyttsp5_request_sysinfo(dev);
	if (!pd->si)
		return -EINVAL;

	rc = cyttsp5_setup_input_device_and_sysfs(dev);
	if (!rc)
		rc = _cyttsp5_set_proximity(pd, false);

	_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_STARTUP,
		CYTTSP5_PROXIMITY_NAME, cyttsp5_setup_input_attention, 0);

	return rc;
}

int cyttsp5_proximity_probe(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_proximity_data *pd = &cd->pd;
	struct cyttsp5_platform_data *pdata = dev_get_platdata(dev);
	struct cyttsp5_proximity_platform_data *prox_pdata;
	int rc = 0;

	if (!pdata ||  !pdata->prox_pdata) {
		dev_err(dev, "%s: Missing platform data\n", __func__);
		rc = -ENODEV;
		goto error_no_pdata;
	}
	prox_pdata = pdata->prox_pdata;

	mutex_init(&pd->prox_lock);
	mutex_init(&pd->sysfs_lock);
	pd->dev = dev;
	pd->pdata = prox_pdata;

	/* Create the input device and register it. */
	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Create the input device and register it\n", __func__);
	pd->input = input_allocate_device();
	if (!pd->input) {
		dev_err(dev, "%s: Error, failed to allocate input device\n",
			__func__);
		rc = -ENODEV;
		goto error_alloc_failed;
	}

	if (pd->pdata->inp_dev_name)
		pd->input->name = pd->pdata->inp_dev_name;
	else
		pd->input->name = CYTTSP5_PROXIMITY_NAME;
	scnprintf(pd->phys, sizeof(pd->phys), "%s/input%d", dev_name(dev),
			cd->phys_num++);
	pd->input->phys = pd->phys;
	pd->input->dev.parent = pd->dev;
	input_set_drvdata(pd->input, pd);

	/* get sysinfo */
	pd->si = _cyttsp5_request_sysinfo(dev);

	if (pd->si) {
		rc = cyttsp5_setup_input_device_and_sysfs(dev);
		if (rc)
			goto error_init_input;

		rc = _cyttsp5_set_proximity(pd, false);
	} else {
		dev_err(dev, "%s: Fail get sysinfo pointer from core p=%p\n",
			__func__, pd->si);
		_cyttsp5_subscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_PROXIMITY_NAME, cyttsp5_setup_input_attention,
			0);
	}

	return 0;

error_init_input:
	input_free_device(pd->input);
error_alloc_failed:
error_no_pdata:
	dev_err(dev, "%s failed.\n", __func__);
	return rc;
}

int cyttsp5_proximity_release(struct device *dev)
{
	struct cyttsp5_proximity_data *pd = get_prox_data(dev);

	if (pd->input_device_registered) {
		/* Disable proximity sensing */
		mutex_lock(&pd->sysfs_lock);
		if (pd->enable_count)
			_cyttsp5_proximity_disable(pd, true);
		mutex_unlock(&pd->sysfs_lock);
		device_remove_file(dev, &dev_attr_prox_enable);
		input_unregister_device(pd->input);
	} else {
		input_free_device(pd->input);
		_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_PROXIMITY_NAME, cyttsp5_setup_input_attention,
			0);
	}

	return 0;
}
