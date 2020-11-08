/*
 * cyttsp5_debug.c
 * Parade TrueTouch(TM) Standard Product V5 Debug Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2012-2015 Cypress Semiconductor
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

#define CYTTSP5_DEBUG_NAME "cyttsp5_debug"

struct cyttsp5_debug_data {
	struct device *dev;
	struct cyttsp5_sysinfo *si;
	uint32_t interrupt_count;
	uint32_t formated_output;
	struct mutex sysfs_lock;
	u8 pr_buf[CY_MAX_PRBUF_SIZE];
};

static struct cyttsp5_core_commands *cmd;

static struct cyttsp5_module debug_module;

static inline struct cyttsp5_debug_data *cyttsp5_get_debug_data(
		struct device *dev)
{
	return cyttsp5_get_module_data(dev, &debug_module);
}

/*
 * This function provide output of combined xy_mode and xy_data.
 * Required by TTHE.
 */
static void cyttsp5_pr_buf_op_mode(struct cyttsp5_debug_data *dd, u8 *pr_buf,
		struct cyttsp5_sysinfo *si, u8 cur_touch)
{
	const char fmt[] = "%02X ";
	int max = (CY_MAX_PRBUF_SIZE - 1) - sizeof(CY_PR_TRUNCATED);
	u8 report_id = si->xy_mode[2];
	int header_size = 0;
	int report_size = 0;
	int total_size = 0;
	int i, k;

	if (report_id == si->desc.tch_report_id) {
		header_size = si->desc.tch_header_size;
		report_size = cur_touch * si->desc.tch_record_size;
	} else if (report_id == si->desc.btn_report_id) {
		header_size = BTN_INPUT_HEADER_SIZE;
		report_size = BTN_REPORT_SIZE;
	}
	total_size = header_size + report_size;

	pr_buf[0] = 0;
	for (i = k = 0; i < header_size && i < max; i++, k += 3)
		scnprintf(pr_buf + k, CY_MAX_PRBUF_SIZE, fmt, si->xy_mode[i]);

	for (i = 0; i < report_size && i < max; i++, k += 3)
		scnprintf(pr_buf + k, CY_MAX_PRBUF_SIZE, fmt, si->xy_data[i]);

	pr_info("%s=%s%s\n", "cyttsp5_OpModeData", pr_buf,
			total_size <= max ? "" : CY_PR_TRUNCATED);
}

static void cyttsp5_debug_print(struct device *dev, u8 *pr_buf, u8 *sptr,
		int size, const char *data_name)
{
	int i, j;
	int elem_size = sizeof("XX ") - 1;
	int max = (CY_MAX_PRBUF_SIZE - 1) / elem_size;
	int limit = size < max ? size : max;

	if (limit < 0)
		limit = 0;

	pr_buf[0] = 0;
	for (i = j = 0; i < limit; i++, j += elem_size)
		scnprintf(pr_buf + j, CY_MAX_PRBUF_SIZE - j, "%02X ", sptr[i]);

	if (size)
		pr_info("%s[0..%d]=%s%s\n", data_name, size - 1, pr_buf,
			size <= max ? "" : CY_PR_TRUNCATED);
	else
		pr_info("%s[]\n", data_name);
}

static void cyttsp5_debug_formated(struct device *dev, u8 *pr_buf,
		struct cyttsp5_sysinfo *si, u8 num_cur_tch)
{
	u8 report_id = si->xy_mode[2];
	int header_size = 0;
	int report_size = 0;
	u8 data_name[] = "touch[99]";
	int max_print_length = 20;
	int i;

	if (report_id == si->desc.tch_report_id) {
		header_size = si->desc.tch_header_size;
		report_size = num_cur_tch * si->desc.tch_record_size;
	} else if (report_id == si->desc.btn_report_id) {
		header_size = BTN_INPUT_HEADER_SIZE;
		report_size = BTN_REPORT_SIZE;
	}

	/* xy_mode */
	cyttsp5_debug_print(dev, pr_buf, si->xy_mode, header_size, "xy_mode");

	/* xy_data */
	if (report_size > max_print_length) {
		pr_info("xy_data[0..%d]:\n", report_size);
		for (i = 0; i < report_size - max_print_length;
				i += max_print_length) {
			cyttsp5_debug_print(dev, pr_buf, si->xy_data + i,
					max_print_length, " ");
		}
		if (report_size - i)
			cyttsp5_debug_print(dev, pr_buf, si->xy_data + i,
					report_size - i, " ");
	} else {
		cyttsp5_debug_print(dev, pr_buf, si->xy_data, report_size,
				"xy_data");
	}

	/* touches */
	if (report_id == si->desc.tch_report_id) {
		for (i = 0; i < num_cur_tch; i++) {
			scnprintf(data_name, sizeof(data_name) - 1,
					"touch[%u]", i);
			cyttsp5_debug_print(dev, pr_buf,
				si->xy_data + (i * si->desc.tch_record_size),
				si->desc.tch_record_size, data_name);
		}
	}

	/* buttons */
	if (report_id == si->desc.btn_report_id)
		cyttsp5_debug_print(dev, pr_buf, si->xy_data, report_size,
				"button");
}

/* read xy_data for all touches for debug */
static int cyttsp5_xy_worker(struct cyttsp5_debug_data *dd)
{
	struct device *dev = dd->dev;
	struct cyttsp5_sysinfo *si = dd->si;
	u8 report_reg = si->xy_mode[TOUCH_COUNT_BYTE_OFFSET];
	u8 num_cur_tch = GET_NUM_TOUCHES(report_reg);
	uint32_t formated_output;

	mutex_lock(&dd->sysfs_lock);
	dd->interrupt_count++;
	formated_output = dd->formated_output;
	mutex_unlock(&dd->sysfs_lock);

	/* Interrupt */
	pr_info("Interrupt(%u)\n", dd->interrupt_count);

	if (formated_output)
		cyttsp5_debug_formated(dev, dd->pr_buf, si, num_cur_tch);
	else
		/* print data for TTHE */
		cyttsp5_pr_buf_op_mode(dd, dd->pr_buf, si, num_cur_tch);

	pr_info("\n");

	return 0;
}

static int cyttsp5_debug_attention(struct device *dev)
{
	struct cyttsp5_debug_data *dd = cyttsp5_get_debug_data(dev);
	struct cyttsp5_sysinfo *si = dd->si;
	u8 report_id = si->xy_mode[2];
	int rc = 0;

	if (report_id != si->desc.tch_report_id
			&& report_id != si->desc.btn_report_id)
		return 0;

	/* core handles handshake */
	rc = cyttsp5_xy_worker(dd);
	if (rc < 0)
		dev_err(dev, "%s: xy_worker error r=%d\n", __func__, rc);

	return rc;
}

static ssize_t cyttsp5_interrupt_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_debug_data *dd = cyttsp5_get_debug_data(dev);
	int val;

	mutex_lock(&dd->sysfs_lock);
	val = dd->interrupt_count;
	mutex_unlock(&dd->sysfs_lock);

	return scnprintf(buf, CY_MAX_PRBUF_SIZE, "Interrupt Count: %d\n", val);
}

static ssize_t cyttsp5_interrupt_count_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_debug_data *dd = cyttsp5_get_debug_data(dev);

	mutex_lock(&dd->sysfs_lock);
	dd->interrupt_count = 0;
	mutex_unlock(&dd->sysfs_lock);
	return size;
}

static DEVICE_ATTR(int_count, S_IRUSR | S_IWUSR,
	cyttsp5_interrupt_count_show, cyttsp5_interrupt_count_store);

static ssize_t cyttsp5_formated_output_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_debug_data *dd = cyttsp5_get_debug_data(dev);
	int val;

	mutex_lock(&dd->sysfs_lock);
	val = dd->formated_output;
	mutex_unlock(&dd->sysfs_lock);

	return scnprintf(buf, CY_MAX_PRBUF_SIZE,
			"Formated debug output: %x\n", val);
}

static ssize_t cyttsp5_formated_output_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_debug_data *dd = cyttsp5_get_debug_data(dev);
	unsigned long value;
	int rc;

	rc = kstrtoul(buf, 10, &value);
	if (rc < 0) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		return size;
	}

	/* Expecting only 0 or 1 */
	if (value != 0 && value != 1) {
		dev_err(dev, "%s: Invalid value %lu\n", __func__, value);
		return size;
	}

	mutex_lock(&dd->sysfs_lock);
	dd->formated_output = value;
	mutex_unlock(&dd->sysfs_lock);
	return size;
}

static DEVICE_ATTR(formated_output, S_IRUSR | S_IWUSR,
	cyttsp5_formated_output_show, cyttsp5_formated_output_store);

static int cyttsp5_debug_probe(struct device *dev, void **data)
{
	struct cyttsp5_debug_data *dd;
	int rc;

	/* get context and debug print buffers */
	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (!dd) {
		rc = -ENOMEM;
		goto cyttsp5_debug_probe_alloc_failed;
	}

	rc = device_create_file(dev, &dev_attr_int_count);
	if (rc) {
		dev_err(dev, "%s: Error, could not create int_count\n",
				__func__);
		goto cyttsp5_debug_probe_create_int_count_failed;
	}

	rc = device_create_file(dev, &dev_attr_formated_output);
	if (rc) {
		dev_err(dev, "%s: Error, could not create formated_output\n",
				__func__);
		goto cyttsp5_debug_probe_create_formated_failed;
	}

	mutex_init(&dd->sysfs_lock);
	dd->dev = dev;
	*data = dd;

	dd->si = cmd->request_sysinfo(dev);
	if (!dd->si) {
		dev_err(dev, "%s: Fail get sysinfo pointer from core\n",
				__func__);
		rc = -ENODEV;
		goto cyttsp5_debug_probe_sysinfo_failed;
	}

	rc = cmd->subscribe_attention(dev, CY_ATTEN_IRQ, CYTTSP5_DEBUG_NAME,
		cyttsp5_debug_attention, CY_MODE_OPERATIONAL);
	if (rc < 0) {
		dev_err(dev, "%s: Error, could not subscribe attention cb\n",
				__func__);
		goto cyttsp5_debug_probe_subscribe_failed;
	}

	return 0;

cyttsp5_debug_probe_subscribe_failed:
cyttsp5_debug_probe_sysinfo_failed:
	device_remove_file(dev, &dev_attr_formated_output);
cyttsp5_debug_probe_create_formated_failed:
	device_remove_file(dev, &dev_attr_int_count);
cyttsp5_debug_probe_create_int_count_failed:
	kfree(dd);
cyttsp5_debug_probe_alloc_failed:
	dev_err(dev, "%s failed.\n", __func__);
	return rc;
}

static void cyttsp5_debug_release(struct device *dev, void *data)
{
	struct cyttsp5_debug_data *dd = data;
	int rc;

	rc = cmd->unsubscribe_attention(dev, CY_ATTEN_IRQ, CYTTSP5_DEBUG_NAME,
		cyttsp5_debug_attention, CY_MODE_OPERATIONAL);
	if (rc < 0) {
		dev_err(dev, "%s: Error, could not un-subscribe attention\n",
				__func__);
		goto cyttsp5_debug_release_exit;
	}

cyttsp5_debug_release_exit:
	device_remove_file(dev, &dev_attr_formated_output);
	device_remove_file(dev, &dev_attr_int_count);
	kfree(dd);
}

static struct cyttsp5_module debug_module = {
	.name = CYTTSP5_DEBUG_NAME,
	.probe = cyttsp5_debug_probe,
	.release = cyttsp5_debug_release,
};

static int __init cyttsp5_debug_init(void)
{
	int rc;

	cmd = cyttsp5_get_commands();
	if (!cmd)
		return -EINVAL;

	rc = cyttsp5_register_module(&debug_module);
	if (rc < 0) {
		pr_err("%s: Error, failed registering module\n",
			__func__);
			return rc;
	}

	pr_info("%s: Parade TTSP Debug Driver (Built %s) rc=%d\n",
		 __func__, CY_DRIVER_VERSION, rc);
	return 0;
}
module_init(cyttsp5_debug_init);

static void __exit cyttsp5_debug_exit(void)
{
	cyttsp5_unregister_module(&debug_module);
}
module_exit(cyttsp5_debug_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade TrueTouch(R) Standard Product Debug Driver");
MODULE_AUTHOR("Parade Technologies <ttdrivers@paradetech.com>");
