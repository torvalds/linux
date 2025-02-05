// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for HiSilicon PCIe tune and trace device
 *
 * Copyright (c) 2022 HiSilicon Technologies Co., Ltd.
 * Author: Yicong Yang <yangyicong@hisilicon.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cpuhotplug.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/vmalloc.h>

#include "hisi_ptt.h"

/* Dynamic CPU hotplug state used by PTT */
static enum cpuhp_state hisi_ptt_pmu_online;

static bool hisi_ptt_wait_tuning_finish(struct hisi_ptt *hisi_ptt)
{
	u32 val;

	return !readl_poll_timeout(hisi_ptt->iobase + HISI_PTT_TUNING_INT_STAT,
				   val, !(val & HISI_PTT_TUNING_INT_STAT_MASK),
				   HISI_PTT_WAIT_POLL_INTERVAL_US,
				   HISI_PTT_WAIT_TUNE_TIMEOUT_US);
}

static ssize_t hisi_ptt_tune_attr_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct hisi_ptt *hisi_ptt = to_hisi_ptt(dev_get_drvdata(dev));
	struct dev_ext_attribute *ext_attr;
	struct hisi_ptt_tune_desc *desc;
	u32 reg;
	u16 val;

	ext_attr = container_of(attr, struct dev_ext_attribute, attr);
	desc = ext_attr->var;

	mutex_lock(&hisi_ptt->tune_lock);

	reg = readl(hisi_ptt->iobase + HISI_PTT_TUNING_CTRL);
	reg &= ~(HISI_PTT_TUNING_CTRL_CODE | HISI_PTT_TUNING_CTRL_SUB);
	reg |= FIELD_PREP(HISI_PTT_TUNING_CTRL_CODE | HISI_PTT_TUNING_CTRL_SUB,
			  desc->event_code);
	writel(reg, hisi_ptt->iobase + HISI_PTT_TUNING_CTRL);

	/* Write all 1 to indicates it's the read process */
	writel(~0U, hisi_ptt->iobase + HISI_PTT_TUNING_DATA);

	if (!hisi_ptt_wait_tuning_finish(hisi_ptt)) {
		mutex_unlock(&hisi_ptt->tune_lock);
		return -ETIMEDOUT;
	}

	reg = readl(hisi_ptt->iobase + HISI_PTT_TUNING_DATA);
	reg &= HISI_PTT_TUNING_DATA_VAL_MASK;
	val = FIELD_GET(HISI_PTT_TUNING_DATA_VAL_MASK, reg);

	mutex_unlock(&hisi_ptt->tune_lock);
	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t hisi_ptt_tune_attr_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct hisi_ptt *hisi_ptt = to_hisi_ptt(dev_get_drvdata(dev));
	struct dev_ext_attribute *ext_attr;
	struct hisi_ptt_tune_desc *desc;
	u32 reg;
	u16 val;

	ext_attr = container_of(attr, struct dev_ext_attribute, attr);
	desc = ext_attr->var;

	if (kstrtou16(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&hisi_ptt->tune_lock);

	reg = readl(hisi_ptt->iobase + HISI_PTT_TUNING_CTRL);
	reg &= ~(HISI_PTT_TUNING_CTRL_CODE | HISI_PTT_TUNING_CTRL_SUB);
	reg |= FIELD_PREP(HISI_PTT_TUNING_CTRL_CODE | HISI_PTT_TUNING_CTRL_SUB,
			  desc->event_code);
	writel(reg, hisi_ptt->iobase + HISI_PTT_TUNING_CTRL);
	writel(FIELD_PREP(HISI_PTT_TUNING_DATA_VAL_MASK, val),
	       hisi_ptt->iobase + HISI_PTT_TUNING_DATA);

	if (!hisi_ptt_wait_tuning_finish(hisi_ptt)) {
		mutex_unlock(&hisi_ptt->tune_lock);
		return -ETIMEDOUT;
	}

	mutex_unlock(&hisi_ptt->tune_lock);
	return count;
}

#define HISI_PTT_TUNE_ATTR(_name, _val, _show, _store)			\
	static struct hisi_ptt_tune_desc _name##_desc = {		\
		.name = #_name,						\
		.event_code = (_val),					\
	};								\
	static struct dev_ext_attribute hisi_ptt_##_name##_attr = {	\
		.attr	= __ATTR(_name, 0600, _show, _store),		\
		.var	= &_name##_desc,				\
	}

#define HISI_PTT_TUNE_ATTR_COMMON(_name, _val)		\
	HISI_PTT_TUNE_ATTR(_name, _val,			\
			   hisi_ptt_tune_attr_show,	\
			   hisi_ptt_tune_attr_store)

/*
 * The value of the tuning event are composed of two parts: main event code
 * in BIT[0,15] and subevent code in BIT[16,23]. For example, qox_tx_cpl is
 * a subevent of 'Tx path QoS control' which for tuning the weight of Tx
 * completion TLPs. See hisi_ptt.rst documentation for more information.
 */
#define HISI_PTT_TUNE_QOS_TX_CPL		(0x4 | (3 << 16))
#define HISI_PTT_TUNE_QOS_TX_NP			(0x4 | (4 << 16))
#define HISI_PTT_TUNE_QOS_TX_P			(0x4 | (5 << 16))
#define HISI_PTT_TUNE_RX_ALLOC_BUF_LEVEL	(0x5 | (6 << 16))
#define HISI_PTT_TUNE_TX_ALLOC_BUF_LEVEL	(0x5 | (7 << 16))

HISI_PTT_TUNE_ATTR_COMMON(qos_tx_cpl, HISI_PTT_TUNE_QOS_TX_CPL);
HISI_PTT_TUNE_ATTR_COMMON(qos_tx_np, HISI_PTT_TUNE_QOS_TX_NP);
HISI_PTT_TUNE_ATTR_COMMON(qos_tx_p, HISI_PTT_TUNE_QOS_TX_P);
HISI_PTT_TUNE_ATTR_COMMON(rx_alloc_buf_level, HISI_PTT_TUNE_RX_ALLOC_BUF_LEVEL);
HISI_PTT_TUNE_ATTR_COMMON(tx_alloc_buf_level, HISI_PTT_TUNE_TX_ALLOC_BUF_LEVEL);

static struct attribute *hisi_ptt_tune_attrs[] = {
	&hisi_ptt_qos_tx_cpl_attr.attr.attr,
	&hisi_ptt_qos_tx_np_attr.attr.attr,
	&hisi_ptt_qos_tx_p_attr.attr.attr,
	&hisi_ptt_rx_alloc_buf_level_attr.attr.attr,
	&hisi_ptt_tx_alloc_buf_level_attr.attr.attr,
	NULL,
};

static struct attribute_group hisi_ptt_tune_group = {
	.name	= "tune",
	.attrs	= hisi_ptt_tune_attrs,
};

static u16 hisi_ptt_get_filter_val(u16 devid, bool is_port)
{
	if (is_port)
		return BIT(HISI_PCIE_CORE_PORT_ID(devid & 0xff));

	return devid;
}

static bool hisi_ptt_wait_trace_hw_idle(struct hisi_ptt *hisi_ptt)
{
	u32 val;

	return !readl_poll_timeout_atomic(hisi_ptt->iobase + HISI_PTT_TRACE_STS,
					  val, val & HISI_PTT_TRACE_IDLE,
					  HISI_PTT_WAIT_POLL_INTERVAL_US,
					  HISI_PTT_WAIT_TRACE_TIMEOUT_US);
}

static void hisi_ptt_wait_dma_reset_done(struct hisi_ptt *hisi_ptt)
{
	u32 val;

	readl_poll_timeout_atomic(hisi_ptt->iobase + HISI_PTT_TRACE_WR_STS,
				  val, !val, HISI_PTT_RESET_POLL_INTERVAL_US,
				  HISI_PTT_RESET_TIMEOUT_US);
}

static void hisi_ptt_trace_end(struct hisi_ptt *hisi_ptt)
{
	writel(0, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);

	/* Mask the interrupt on the end */
	writel(HISI_PTT_TRACE_INT_MASK_ALL, hisi_ptt->iobase + HISI_PTT_TRACE_INT_MASK);

	hisi_ptt->trace_ctrl.started = false;
}

static int hisi_ptt_trace_start(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	u32 val;
	int i;

	/* Check device idle before start trace */
	if (!hisi_ptt_wait_trace_hw_idle(hisi_ptt)) {
		pci_err(hisi_ptt->pdev, "Failed to start trace, the device is still busy\n");
		return -EBUSY;
	}

	ctrl->started = true;

	/* Reset the DMA before start tracing */
	val = readl(hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);
	val |= HISI_PTT_TRACE_CTRL_RST;
	writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);

	hisi_ptt_wait_dma_reset_done(hisi_ptt);

	val = readl(hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);
	val &= ~HISI_PTT_TRACE_CTRL_RST;
	writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);

	/* Reset the index of current buffer */
	hisi_ptt->trace_ctrl.buf_index = 0;

	/* Zero the trace buffers */
	for (i = 0; i < HISI_PTT_TRACE_BUF_CNT; i++)
		memset(ctrl->trace_buf[i].addr, 0, HISI_PTT_TRACE_BUF_SIZE);

	/* Clear the interrupt status */
	writel(HISI_PTT_TRACE_INT_STAT_MASK, hisi_ptt->iobase + HISI_PTT_TRACE_INT_STAT);
	writel(0, hisi_ptt->iobase + HISI_PTT_TRACE_INT_MASK);

	/* Set the trace control register */
	val = FIELD_PREP(HISI_PTT_TRACE_CTRL_TYPE_SEL, ctrl->type);
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_RXTX_SEL, ctrl->direction);
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_DATA_FORMAT, ctrl->format);
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_TARGET_SEL, hisi_ptt->trace_ctrl.filter);
	if (!hisi_ptt->trace_ctrl.is_port)
		val |= HISI_PTT_TRACE_CTRL_FILTER_MODE;

	/* Start the Trace */
	val |= HISI_PTT_TRACE_CTRL_EN;
	writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);

	return 0;
}

static int hisi_ptt_update_aux(struct hisi_ptt *hisi_ptt, int index, bool stop)
{
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	struct perf_output_handle *handle = &ctrl->handle;
	struct perf_event *event = handle->event;
	struct hisi_ptt_pmu_buf *buf;
	size_t size;
	void *addr;

	buf = perf_get_aux(handle);
	if (!buf || !handle->size)
		return -EINVAL;

	addr = ctrl->trace_buf[ctrl->buf_index].addr;

	/*
	 * If we're going to stop, read the size of already traced data from
	 * HISI_PTT_TRACE_WR_STS. Otherwise we're coming from the interrupt,
	 * the data size is always HISI_PTT_TRACE_BUF_SIZE.
	 */
	if (stop) {
		u32 reg;

		reg = readl(hisi_ptt->iobase + HISI_PTT_TRACE_WR_STS);
		size = FIELD_GET(HISI_PTT_TRACE_WR_STS_WRITE, reg);
	} else {
		size = HISI_PTT_TRACE_BUF_SIZE;
	}

	memcpy(buf->base + buf->pos, addr, size);
	buf->pos += size;

	/*
	 * Always commit the data to the AUX buffer in time to make sure
	 * userspace got enough time to consume the data.
	 *
	 * If we're not going to stop, apply a new one and check whether
	 * there's enough room for the next trace.
	 */
	perf_aux_output_end(handle, size);
	if (!stop) {
		buf = perf_aux_output_begin(handle, event);
		if (!buf)
			return -EINVAL;

		buf->pos = handle->head % buf->length;
		if (buf->length - buf->pos < HISI_PTT_TRACE_BUF_SIZE) {
			perf_aux_output_end(handle, 0);
			return -EINVAL;
		}
	}

	return 0;
}

static irqreturn_t hisi_ptt_isr(int irq, void *context)
{
	struct hisi_ptt *hisi_ptt = context;
	u32 status, buf_idx;

	status = readl(hisi_ptt->iobase + HISI_PTT_TRACE_INT_STAT);
	if (!(status & HISI_PTT_TRACE_INT_STAT_MASK))
		return IRQ_NONE;

	buf_idx = ffs(status) - 1;

	/* Clear the interrupt status of buffer @buf_idx */
	writel(status, hisi_ptt->iobase + HISI_PTT_TRACE_INT_STAT);

	/*
	 * Update the AUX buffer and cache the current buffer index,
	 * as we need to know this and save the data when the trace
	 * is ended out of the interrupt handler. End the trace
	 * if the updating fails.
	 */
	if (hisi_ptt_update_aux(hisi_ptt, buf_idx, false))
		hisi_ptt_trace_end(hisi_ptt);
	else
		hisi_ptt->trace_ctrl.buf_index = (buf_idx + 1) % HISI_PTT_TRACE_BUF_CNT;

	return IRQ_HANDLED;
}

static void hisi_ptt_irq_free_vectors(void *pdev)
{
	pci_free_irq_vectors(pdev);
}

static int hisi_ptt_register_irq(struct hisi_ptt *hisi_ptt)
{
	struct pci_dev *pdev = hisi_ptt->pdev;
	int ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0) {
		pci_err(pdev, "failed to allocate irq vector, ret = %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&pdev->dev, hisi_ptt_irq_free_vectors, pdev);
	if (ret < 0)
		return ret;

	hisi_ptt->trace_irq = pci_irq_vector(pdev, HISI_PTT_TRACE_DMA_IRQ);
	ret = devm_request_irq(&pdev->dev, hisi_ptt->trace_irq, hisi_ptt_isr,
				IRQF_NOBALANCING | IRQF_NO_THREAD, DRV_NAME,
				hisi_ptt);
	if (ret) {
		pci_err(pdev, "failed to request irq %d, ret = %d\n",
			hisi_ptt->trace_irq, ret);
		return ret;
	}

	return 0;
}

static void hisi_ptt_del_free_filter(struct hisi_ptt *hisi_ptt,
				      struct hisi_ptt_filter_desc *filter)
{
	if (filter->is_port)
		hisi_ptt->port_mask &= ~hisi_ptt_get_filter_val(filter->devid, true);

	list_del(&filter->list);
	kfree(filter->name);
	kfree(filter);
}

static struct hisi_ptt_filter_desc *
hisi_ptt_alloc_add_filter(struct hisi_ptt *hisi_ptt, u16 devid, bool is_port)
{
	struct hisi_ptt_filter_desc *filter;
	u8 devfn = devid & 0xff;
	char *filter_name;

	filter_name = kasprintf(GFP_KERNEL, "%04x:%02x:%02x.%d", pci_domain_nr(hisi_ptt->pdev->bus),
				 PCI_BUS_NUM(devid), PCI_SLOT(devfn), PCI_FUNC(devfn));
	if (!filter_name) {
		pci_err(hisi_ptt->pdev, "failed to allocate name for filter %04x:%02x:%02x.%d\n",
			pci_domain_nr(hisi_ptt->pdev->bus), PCI_BUS_NUM(devid),
			PCI_SLOT(devfn), PCI_FUNC(devfn));
		return NULL;
	}

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter) {
		pci_err(hisi_ptt->pdev, "failed to add filter for %s\n",
			filter_name);
		kfree(filter_name);
		return NULL;
	}

	filter->name = filter_name;
	filter->is_port = is_port;
	filter->devid = devid;

	if (filter->is_port) {
		list_add_tail(&filter->list, &hisi_ptt->port_filters);

		/* Update the available port mask */
		hisi_ptt->port_mask |= hisi_ptt_get_filter_val(filter->devid, true);
	} else {
		list_add_tail(&filter->list, &hisi_ptt->req_filters);
	}

	return filter;
}

static ssize_t hisi_ptt_filter_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct hisi_ptt_filter_desc *filter;
	unsigned long filter_val;

	filter = container_of(attr, struct hisi_ptt_filter_desc, attr);
	filter_val = hisi_ptt_get_filter_val(filter->devid, filter->is_port) |
		     (filter->is_port ? HISI_PTT_PMU_FILTER_IS_PORT : 0);

	return sysfs_emit(buf, "0x%05lx\n", filter_val);
}

static int hisi_ptt_create_rp_filter_attr(struct hisi_ptt *hisi_ptt,
					  struct hisi_ptt_filter_desc *filter)
{
	struct kobject *kobj = &hisi_ptt->hisi_ptt_pmu.dev->kobj;

	sysfs_attr_init(&filter->attr.attr);
	filter->attr.attr.name = filter->name;
	filter->attr.attr.mode = 0400; /* DEVICE_ATTR_ADMIN_RO */
	filter->attr.show = hisi_ptt_filter_show;

	return sysfs_add_file_to_group(kobj, &filter->attr.attr,
				       HISI_PTT_RP_FILTERS_GRP_NAME);
}

static void hisi_ptt_remove_rp_filter_attr(struct hisi_ptt *hisi_ptt,
					  struct hisi_ptt_filter_desc *filter)
{
	struct kobject *kobj = &hisi_ptt->hisi_ptt_pmu.dev->kobj;

	sysfs_remove_file_from_group(kobj, &filter->attr.attr,
				     HISI_PTT_RP_FILTERS_GRP_NAME);
}

static int hisi_ptt_create_req_filter_attr(struct hisi_ptt *hisi_ptt,
					   struct hisi_ptt_filter_desc *filter)
{
	struct kobject *kobj = &hisi_ptt->hisi_ptt_pmu.dev->kobj;

	sysfs_attr_init(&filter->attr.attr);
	filter->attr.attr.name = filter->name;
	filter->attr.attr.mode = 0400; /* DEVICE_ATTR_ADMIN_RO */
	filter->attr.show = hisi_ptt_filter_show;

	return sysfs_add_file_to_group(kobj, &filter->attr.attr,
				       HISI_PTT_REQ_FILTERS_GRP_NAME);
}

static void hisi_ptt_remove_req_filter_attr(struct hisi_ptt *hisi_ptt,
					   struct hisi_ptt_filter_desc *filter)
{
	struct kobject *kobj = &hisi_ptt->hisi_ptt_pmu.dev->kobj;

	sysfs_remove_file_from_group(kobj, &filter->attr.attr,
				     HISI_PTT_REQ_FILTERS_GRP_NAME);
}

static int hisi_ptt_create_filter_attr(struct hisi_ptt *hisi_ptt,
				       struct hisi_ptt_filter_desc *filter)
{
	int ret;

	if (filter->is_port)
		ret = hisi_ptt_create_rp_filter_attr(hisi_ptt, filter);
	else
		ret = hisi_ptt_create_req_filter_attr(hisi_ptt, filter);

	if (ret)
		pci_err(hisi_ptt->pdev, "failed to create sysfs attribute for filter %s\n",
			filter->name);

	return ret;
}

static void hisi_ptt_remove_filter_attr(struct hisi_ptt *hisi_ptt,
					struct hisi_ptt_filter_desc *filter)
{
	if (filter->is_port)
		hisi_ptt_remove_rp_filter_attr(hisi_ptt, filter);
	else
		hisi_ptt_remove_req_filter_attr(hisi_ptt, filter);
}

static void hisi_ptt_remove_all_filter_attributes(void *data)
{
	struct hisi_ptt_filter_desc *filter;
	struct hisi_ptt *hisi_ptt = data;

	mutex_lock(&hisi_ptt->filter_lock);

	list_for_each_entry(filter, &hisi_ptt->req_filters, list)
		hisi_ptt_remove_filter_attr(hisi_ptt, filter);

	list_for_each_entry(filter, &hisi_ptt->port_filters, list)
		hisi_ptt_remove_filter_attr(hisi_ptt, filter);

	hisi_ptt->sysfs_inited = false;
	mutex_unlock(&hisi_ptt->filter_lock);
}

static int hisi_ptt_init_filter_attributes(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_filter_desc *filter;
	int ret;

	mutex_lock(&hisi_ptt->filter_lock);

	/*
	 * Register the reset callback in the first stage. In reset we traverse
	 * the filters list to remove the sysfs attributes so the callback can
	 * be called safely even without below filter attributes creation.
	 */
	ret = devm_add_action(&hisi_ptt->pdev->dev,
			      hisi_ptt_remove_all_filter_attributes,
			      hisi_ptt);
	if (ret)
		goto out;

	list_for_each_entry(filter, &hisi_ptt->port_filters, list) {
		ret = hisi_ptt_create_filter_attr(hisi_ptt, filter);
		if (ret)
			goto out;
	}

	list_for_each_entry(filter, &hisi_ptt->req_filters, list) {
		ret = hisi_ptt_create_filter_attr(hisi_ptt, filter);
		if (ret)
			goto out;
	}

	hisi_ptt->sysfs_inited = true;
out:
	mutex_unlock(&hisi_ptt->filter_lock);
	return ret;
}

static void hisi_ptt_update_filters(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct hisi_ptt_filter_update_info info;
	struct hisi_ptt_filter_desc *filter;
	struct hisi_ptt *hisi_ptt;

	hisi_ptt = container_of(delayed_work, struct hisi_ptt, work);

	if (!mutex_trylock(&hisi_ptt->filter_lock)) {
		schedule_delayed_work(&hisi_ptt->work, HISI_PTT_WORK_DELAY_MS);
		return;
	}

	while (kfifo_get(&hisi_ptt->filter_update_kfifo, &info)) {
		if (info.is_add) {
			/*
			 * Notify the users if failed to add this filter, others
			 * still work and available. See the comments in
			 * hisi_ptt_init_filters().
			 */
			filter = hisi_ptt_alloc_add_filter(hisi_ptt, info.devid, info.is_port);
			if (!filter)
				continue;

			/*
			 * If filters' sysfs entries hasn't been initialized,
			 * then we're still at probe stage. Add the filters to
			 * the list and later hisi_ptt_init_filter_attributes()
			 * will create sysfs attributes for all the filters.
			 */
			if (hisi_ptt->sysfs_inited &&
			    hisi_ptt_create_filter_attr(hisi_ptt, filter)) {
				hisi_ptt_del_free_filter(hisi_ptt, filter);
				continue;
			}
		} else {
			struct hisi_ptt_filter_desc *tmp;
			struct list_head *target_list;

			target_list = info.is_port ? &hisi_ptt->port_filters :
				      &hisi_ptt->req_filters;

			list_for_each_entry_safe(filter, tmp, target_list, list)
				if (filter->devid == info.devid) {
					if (hisi_ptt->sysfs_inited)
						hisi_ptt_remove_filter_attr(hisi_ptt, filter);

					hisi_ptt_del_free_filter(hisi_ptt, filter);
					break;
				}
		}
	}

	mutex_unlock(&hisi_ptt->filter_lock);
}

/*
 * A PCI bus notifier is used here for dynamically updating the filter
 * list.
 */
static int hisi_ptt_notifier_call(struct notifier_block *nb, unsigned long action,
				  void *data)
{
	struct hisi_ptt *hisi_ptt = container_of(nb, struct hisi_ptt, hisi_ptt_nb);
	struct hisi_ptt_filter_update_info info;
	struct pci_dev *pdev, *root_port;
	struct device *dev = data;
	u32 port_devid;

	pdev = to_pci_dev(dev);
	root_port = pcie_find_root_port(pdev);
	if (!root_port)
		return 0;

	port_devid = pci_dev_id(root_port);
	if (port_devid < hisi_ptt->lower_bdf ||
	    port_devid > hisi_ptt->upper_bdf)
		return 0;

	info.is_port = pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT;
	info.devid = pci_dev_id(pdev);

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		info.is_add = true;
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		info.is_add = false;
		break;
	default:
		return 0;
	}

	/*
	 * The FIFO size is 16 which is sufficient for almost all the cases,
	 * since each PCIe core will have most 8 Root Ports (typically only
	 * 1~4 Root Ports). On failure log the failed filter and let user
	 * handle it.
	 */
	if (kfifo_in_spinlocked(&hisi_ptt->filter_update_kfifo, &info, 1,
				&hisi_ptt->filter_update_lock))
		schedule_delayed_work(&hisi_ptt->work, 0);
	else
		pci_warn(hisi_ptt->pdev,
			 "filter update fifo overflow for target %s\n",
			 pci_name(pdev));

	return 0;
}

static int hisi_ptt_init_filters(struct pci_dev *pdev, void *data)
{
	struct pci_dev *root_port = pcie_find_root_port(pdev);
	struct hisi_ptt_filter_desc *filter;
	struct hisi_ptt *hisi_ptt = data;
	u32 port_devid;

	if (!root_port)
		return 0;

	port_devid = pci_dev_id(root_port);
	if (port_devid < hisi_ptt->lower_bdf ||
	    port_devid > hisi_ptt->upper_bdf)
		return 0;

	/*
	 * We won't fail the probe if filter allocation failed here. The filters
	 * should be partial initialized and users would know which filter fails
	 * through the log. Other functions of PTT device are still available.
	 */
	filter = hisi_ptt_alloc_add_filter(hisi_ptt, pci_dev_id(pdev),
					    pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT);
	if (!filter)
		return -ENOMEM;

	return 0;
}

static void hisi_ptt_release_filters(void *data)
{
	struct hisi_ptt_filter_desc *filter, *tmp;
	struct hisi_ptt *hisi_ptt = data;

	list_for_each_entry_safe(filter, tmp, &hisi_ptt->req_filters, list)
		hisi_ptt_del_free_filter(hisi_ptt, filter);

	list_for_each_entry_safe(filter, tmp, &hisi_ptt->port_filters, list)
		hisi_ptt_del_free_filter(hisi_ptt, filter);
}

static int hisi_ptt_config_trace_buf(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	struct device *dev = &hisi_ptt->pdev->dev;
	int i;

	ctrl->trace_buf = devm_kcalloc(dev, HISI_PTT_TRACE_BUF_CNT,
				       sizeof(*ctrl->trace_buf), GFP_KERNEL);
	if (!ctrl->trace_buf)
		return -ENOMEM;

	for (i = 0; i < HISI_PTT_TRACE_BUF_CNT; ++i) {
		ctrl->trace_buf[i].addr = dmam_alloc_coherent(dev, HISI_PTT_TRACE_BUF_SIZE,
							     &ctrl->trace_buf[i].dma,
							     GFP_KERNEL);
		if (!ctrl->trace_buf[i].addr)
			return -ENOMEM;
	}

	/* Configure the trace DMA buffer */
	for (i = 0; i < HISI_PTT_TRACE_BUF_CNT; i++) {
		writel(lower_32_bits(ctrl->trace_buf[i].dma),
		       hisi_ptt->iobase + HISI_PTT_TRACE_ADDR_BASE_LO_0 +
		       i * HISI_PTT_TRACE_ADDR_STRIDE);
		writel(upper_32_bits(ctrl->trace_buf[i].dma),
		       hisi_ptt->iobase + HISI_PTT_TRACE_ADDR_BASE_HI_0 +
		       i * HISI_PTT_TRACE_ADDR_STRIDE);
	}
	writel(HISI_PTT_TRACE_BUF_SIZE, hisi_ptt->iobase + HISI_PTT_TRACE_ADDR_SIZE);

	return 0;
}

static int hisi_ptt_init_ctrls(struct hisi_ptt *hisi_ptt)
{
	struct pci_dev *pdev = hisi_ptt->pdev;
	struct pci_bus *bus;
	int ret;
	u32 reg;

	INIT_DELAYED_WORK(&hisi_ptt->work, hisi_ptt_update_filters);
	INIT_KFIFO(hisi_ptt->filter_update_kfifo);
	spin_lock_init(&hisi_ptt->filter_update_lock);

	INIT_LIST_HEAD(&hisi_ptt->port_filters);
	INIT_LIST_HEAD(&hisi_ptt->req_filters);
	mutex_init(&hisi_ptt->filter_lock);

	ret = hisi_ptt_config_trace_buf(hisi_ptt);
	if (ret)
		return ret;

	/*
	 * The device range register provides the information about the root
	 * ports which the RCiEP can control and trace. The RCiEP and the root
	 * ports which it supports are on the same PCIe core, with same domain
	 * number but maybe different bus number. The device range register
	 * will tell us which root ports we can support, Bit[31:16] indicates
	 * the upper BDF numbers of the root port, while Bit[15:0] indicates
	 * the lower.
	 */
	reg = readl(hisi_ptt->iobase + HISI_PTT_DEVICE_RANGE);
	hisi_ptt->upper_bdf = FIELD_GET(HISI_PTT_DEVICE_RANGE_UPPER, reg);
	hisi_ptt->lower_bdf = FIELD_GET(HISI_PTT_DEVICE_RANGE_LOWER, reg);

	bus = pci_find_bus(pci_domain_nr(pdev->bus), PCI_BUS_NUM(hisi_ptt->upper_bdf));
	if (bus)
		pci_walk_bus(bus, hisi_ptt_init_filters, hisi_ptt);

	ret = devm_add_action_or_reset(&pdev->dev, hisi_ptt_release_filters, hisi_ptt);
	if (ret)
		return ret;

	hisi_ptt->trace_ctrl.on_cpu = -1;
	return 0;
}

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct hisi_ptt *hisi_ptt = to_hisi_ptt(dev_get_drvdata(dev));
	const cpumask_t *cpumask = cpumask_of_node(dev_to_node(&hisi_ptt->pdev->dev));

	return cpumap_print_to_pagebuf(true, buf, cpumask);
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *hisi_ptt_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static const struct attribute_group hisi_ptt_cpumask_attr_group = {
	.attrs = hisi_ptt_cpumask_attrs,
};

/*
 * Bit 19 indicates the filter type, 1 for Root Port filter and 0 for Requester
 * filter. Bit[15:0] indicates the filter value, for Root Port filter it's
 * a bit mask of desired ports and for Requester filter it's the Requester ID
 * of the desired PCIe function. Bit[18:16] is reserved for extension.
 *
 * See hisi_ptt.rst documentation for detailed information.
 */
PMU_FORMAT_ATTR(filter,		"config:0-19");
PMU_FORMAT_ATTR(direction,	"config:20-23");
PMU_FORMAT_ATTR(type,		"config:24-31");
PMU_FORMAT_ATTR(format,		"config:32-35");

static struct attribute *hisi_ptt_pmu_format_attrs[] = {
	&format_attr_filter.attr,
	&format_attr_direction.attr,
	&format_attr_type.attr,
	&format_attr_format.attr,
	NULL
};

static struct attribute_group hisi_ptt_pmu_format_group = {
	.name = "format",
	.attrs = hisi_ptt_pmu_format_attrs,
};

static ssize_t hisi_ptt_filter_multiselect_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct dev_ext_attribute *ext_attr;

	ext_attr = container_of(attr, struct dev_ext_attribute, attr);
	return sysfs_emit(buf, "%s\n", (char *)ext_attr->var);
}

static struct dev_ext_attribute root_port_filters_multiselect = {
	.attr = {
		.attr = { .name = "multiselect", .mode = 0400 },
		.show = hisi_ptt_filter_multiselect_show,
	},
	.var = "1",
};

static struct attribute *hisi_ptt_pmu_root_ports_attrs[] = {
	&root_port_filters_multiselect.attr.attr,
	NULL
};

static struct attribute_group hisi_ptt_pmu_root_ports_group = {
	.name = HISI_PTT_RP_FILTERS_GRP_NAME,
	.attrs = hisi_ptt_pmu_root_ports_attrs,
};

static struct dev_ext_attribute requester_filters_multiselect = {
	.attr = {
		.attr = { .name = "multiselect", .mode = 0400 },
		.show = hisi_ptt_filter_multiselect_show,
	},
	.var = "0",
};

static struct attribute *hisi_ptt_pmu_requesters_attrs[] = {
	&requester_filters_multiselect.attr.attr,
	NULL
};

static struct attribute_group hisi_ptt_pmu_requesters_group = {
	.name = HISI_PTT_REQ_FILTERS_GRP_NAME,
	.attrs = hisi_ptt_pmu_requesters_attrs,
};

static const struct attribute_group *hisi_ptt_pmu_groups[] = {
	&hisi_ptt_cpumask_attr_group,
	&hisi_ptt_pmu_format_group,
	&hisi_ptt_tune_group,
	&hisi_ptt_pmu_root_ports_group,
	&hisi_ptt_pmu_requesters_group,
	NULL
};

static int hisi_ptt_trace_valid_direction(u32 val)
{
	/*
	 * The direction values have different effects according to the data
	 * format (specified in the parentheses). TLP set A/B means different
	 * set of TLP types. See hisi_ptt.rst documentation for more details.
	 */
	static const u32 hisi_ptt_trace_available_direction[] = {
		0,	/* inbound(4DW) or reserved(8DW) */
		1,	/* outbound(4DW) */
		2,	/* {in, out}bound(4DW) or inbound(8DW), TLP set A */
		3,	/* {in, out}bound(4DW) or inbound(8DW), TLP set B */
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(hisi_ptt_trace_available_direction); i++) {
		if (val == hisi_ptt_trace_available_direction[i])
			return 0;
	}

	return -EINVAL;
}

static int hisi_ptt_trace_valid_type(u32 val)
{
	/* Different types can be set simultaneously */
	static const u32 hisi_ptt_trace_available_type[] = {
		1,	/* posted_request */
		2,	/* non-posted_request */
		4,	/* completion */
	};
	int i;

	if (!val)
		return -EINVAL;

	/*
	 * Walk the available list and clear the valid bits of
	 * the config. If there is any resident bit after the
	 * walk then the config is invalid.
	 */
	for (i = 0; i < ARRAY_SIZE(hisi_ptt_trace_available_type); i++)
		val &= ~hisi_ptt_trace_available_type[i];

	if (val)
		return -EINVAL;

	return 0;
}

static int hisi_ptt_trace_valid_format(u32 val)
{
	static const u32 hisi_ptt_trace_availble_format[] = {
		0,	/* 4DW */
		1,	/* 8DW */
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(hisi_ptt_trace_availble_format); i++) {
		if (val == hisi_ptt_trace_availble_format[i])
			return 0;
	}

	return -EINVAL;
}

static int hisi_ptt_trace_valid_filter(struct hisi_ptt *hisi_ptt, u64 config)
{
	unsigned long val, port_mask = hisi_ptt->port_mask;
	struct hisi_ptt_filter_desc *filter;
	int ret = 0;

	hisi_ptt->trace_ctrl.is_port = FIELD_GET(HISI_PTT_PMU_FILTER_IS_PORT, config);
	val = FIELD_GET(HISI_PTT_PMU_FILTER_VAL_MASK, config);

	/*
	 * Port filters are defined as bit mask. For port filters, check
	 * the bits in the @val are within the range of hisi_ptt->port_mask
	 * and whether it's empty or not, otherwise user has specified
	 * some unsupported root ports.
	 *
	 * For Requester ID filters, walk the available filter list to see
	 * whether we have one matched.
	 */
	mutex_lock(&hisi_ptt->filter_lock);
	if (!hisi_ptt->trace_ctrl.is_port) {
		list_for_each_entry(filter, &hisi_ptt->req_filters, list) {
			if (val == hisi_ptt_get_filter_val(filter->devid, filter->is_port))
				goto out;
		}
	} else if (bitmap_subset(&val, &port_mask, BITS_PER_LONG)) {
		goto out;
	}

	ret = -EINVAL;
out:
	mutex_unlock(&hisi_ptt->filter_lock);
	return ret;
}

static void hisi_ptt_pmu_init_configs(struct hisi_ptt *hisi_ptt, struct perf_event *event)
{
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	u32 val;

	val = FIELD_GET(HISI_PTT_PMU_FILTER_VAL_MASK, event->attr.config);
	hisi_ptt->trace_ctrl.filter = val;

	val = FIELD_GET(HISI_PTT_PMU_DIRECTION_MASK, event->attr.config);
	ctrl->direction = val;

	val = FIELD_GET(HISI_PTT_PMU_TYPE_MASK, event->attr.config);
	ctrl->type = val;

	val = FIELD_GET(HISI_PTT_PMU_FORMAT_MASK, event->attr.config);
	ctrl->format = val;
}

static int hisi_ptt_pmu_event_init(struct perf_event *event)
{
	struct hisi_ptt *hisi_ptt = to_hisi_ptt(event->pmu);
	int ret;
	u32 val;

	if (event->attr.type != hisi_ptt->hisi_ptt_pmu.type)
		return -ENOENT;

	if (event->cpu < 0) {
		dev_dbg(event->pmu->dev, "Per-task mode not supported\n");
		return -EOPNOTSUPP;
	}

	if (event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	ret = hisi_ptt_trace_valid_filter(hisi_ptt, event->attr.config);
	if (ret < 0)
		return ret;

	val = FIELD_GET(HISI_PTT_PMU_DIRECTION_MASK, event->attr.config);
	ret = hisi_ptt_trace_valid_direction(val);
	if (ret < 0)
		return ret;

	val = FIELD_GET(HISI_PTT_PMU_TYPE_MASK, event->attr.config);
	ret = hisi_ptt_trace_valid_type(val);
	if (ret < 0)
		return ret;

	val = FIELD_GET(HISI_PTT_PMU_FORMAT_MASK, event->attr.config);
	return hisi_ptt_trace_valid_format(val);
}

static void *hisi_ptt_pmu_setup_aux(struct perf_event *event, void **pages,
				    int nr_pages, bool overwrite)
{
	struct hisi_ptt_pmu_buf *buf;
	struct page **pagelist;
	int i;

	if (overwrite) {
		dev_warn(event->pmu->dev, "Overwrite mode is not supported\n");
		return NULL;
	}

	/* If the pages size less than buffers, we cannot start trace */
	if (nr_pages < HISI_PTT_TRACE_TOTAL_BUF_SIZE / PAGE_SIZE)
		return NULL;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	pagelist = kcalloc(nr_pages, sizeof(*pagelist), GFP_KERNEL);
	if (!pagelist)
		goto err;

	for (i = 0; i < nr_pages; i++)
		pagelist[i] = virt_to_page(pages[i]);

	buf->base = vmap(pagelist, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!buf->base) {
		kfree(pagelist);
		goto err;
	}

	buf->nr_pages = nr_pages;
	buf->length = nr_pages * PAGE_SIZE;
	buf->pos = 0;

	kfree(pagelist);
	return buf;
err:
	kfree(buf);
	return NULL;
}

static void hisi_ptt_pmu_free_aux(void *aux)
{
	struct hisi_ptt_pmu_buf *buf = aux;

	vunmap(buf->base);
	kfree(buf);
}

static void hisi_ptt_pmu_start(struct perf_event *event, int flags)
{
	struct hisi_ptt *hisi_ptt = to_hisi_ptt(event->pmu);
	struct perf_output_handle *handle = &hisi_ptt->trace_ctrl.handle;
	struct hw_perf_event *hwc = &event->hw;
	struct device *dev = event->pmu->dev;
	struct hisi_ptt_pmu_buf *buf;
	int cpu = event->cpu;
	int ret;

	hwc->state = 0;

	/* Serialize the perf process if user specified several CPUs */
	spin_lock(&hisi_ptt->pmu_lock);
	if (hisi_ptt->trace_ctrl.started) {
		dev_dbg(dev, "trace has already started\n");
		goto stop;
	}

	/*
	 * Handle the interrupt on the same cpu which starts the trace to avoid
	 * context mismatch. Otherwise we'll trigger the WARN from the perf
	 * core in event_function_local(). If CPU passed is offline we'll fail
	 * here, just log it since we can do nothing here.
	 */
	ret = irq_set_affinity(hisi_ptt->trace_irq, cpumask_of(cpu));
	if (ret)
		dev_warn(dev, "failed to set the affinity of trace interrupt\n");

	hisi_ptt->trace_ctrl.on_cpu = cpu;

	buf = perf_aux_output_begin(handle, event);
	if (!buf) {
		dev_dbg(dev, "aux output begin failed\n");
		goto stop;
	}

	buf->pos = handle->head % buf->length;

	hisi_ptt_pmu_init_configs(hisi_ptt, event);

	ret = hisi_ptt_trace_start(hisi_ptt);
	if (ret) {
		dev_dbg(dev, "trace start failed, ret = %d\n", ret);
		perf_aux_output_end(handle, 0);
		goto stop;
	}

	spin_unlock(&hisi_ptt->pmu_lock);
	return;
stop:
	event->hw.state |= PERF_HES_STOPPED;
	spin_unlock(&hisi_ptt->pmu_lock);
}

static void hisi_ptt_pmu_stop(struct perf_event *event, int flags)
{
	struct hisi_ptt *hisi_ptt = to_hisi_ptt(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->state & PERF_HES_STOPPED)
		return;

	spin_lock(&hisi_ptt->pmu_lock);
	if (hisi_ptt->trace_ctrl.started) {
		hisi_ptt_trace_end(hisi_ptt);

		if (!hisi_ptt_wait_trace_hw_idle(hisi_ptt))
			dev_warn(event->pmu->dev, "Device is still busy\n");

		hisi_ptt_update_aux(hisi_ptt, hisi_ptt->trace_ctrl.buf_index, true);
	}
	spin_unlock(&hisi_ptt->pmu_lock);

	hwc->state |= PERF_HES_STOPPED;
	perf_event_update_userpage(event);
	hwc->state |= PERF_HES_UPTODATE;
}

static int hisi_ptt_pmu_add(struct perf_event *event, int flags)
{
	struct hisi_ptt *hisi_ptt = to_hisi_ptt(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int cpu = event->cpu;

	/* Only allow the cpus on the device's node to add the event */
	if (!cpumask_test_cpu(cpu, cpumask_of_node(dev_to_node(&hisi_ptt->pdev->dev))))
		return 0;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START) {
		hisi_ptt_pmu_start(event, PERF_EF_RELOAD);
		if (hwc->state & PERF_HES_STOPPED)
			return -EINVAL;
	}

	return 0;
}

static void hisi_ptt_pmu_del(struct perf_event *event, int flags)
{
	hisi_ptt_pmu_stop(event, PERF_EF_UPDATE);
}

static void hisi_ptt_pmu_read(struct perf_event *event)
{
}

static void hisi_ptt_remove_cpuhp_instance(void *hotplug_node)
{
	cpuhp_state_remove_instance_nocalls(hisi_ptt_pmu_online, hotplug_node);
}

static void hisi_ptt_unregister_pmu(void *pmu)
{
	perf_pmu_unregister(pmu);
}

static int hisi_ptt_register_pmu(struct hisi_ptt *hisi_ptt)
{
	u16 core_id, sicl_id;
	char *pmu_name;
	u32 reg;
	int ret;

	ret = cpuhp_state_add_instance_nocalls(hisi_ptt_pmu_online,
					       &hisi_ptt->hotplug_node);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&hisi_ptt->pdev->dev,
				       hisi_ptt_remove_cpuhp_instance,
				       &hisi_ptt->hotplug_node);
	if (ret)
		return ret;

	mutex_init(&hisi_ptt->tune_lock);
	spin_lock_init(&hisi_ptt->pmu_lock);

	hisi_ptt->hisi_ptt_pmu = (struct pmu) {
		.module		= THIS_MODULE,
		.parent         = &hisi_ptt->pdev->dev,
		.capabilities	= PERF_PMU_CAP_EXCLUSIVE | PERF_PMU_CAP_NO_EXCLUDE,
		.task_ctx_nr	= perf_sw_context,
		.attr_groups	= hisi_ptt_pmu_groups,
		.event_init	= hisi_ptt_pmu_event_init,
		.setup_aux	= hisi_ptt_pmu_setup_aux,
		.free_aux	= hisi_ptt_pmu_free_aux,
		.start		= hisi_ptt_pmu_start,
		.stop		= hisi_ptt_pmu_stop,
		.add		= hisi_ptt_pmu_add,
		.del		= hisi_ptt_pmu_del,
		.read		= hisi_ptt_pmu_read,
	};

	reg = readl(hisi_ptt->iobase + HISI_PTT_LOCATION);
	core_id = FIELD_GET(HISI_PTT_CORE_ID, reg);
	sicl_id = FIELD_GET(HISI_PTT_SICL_ID, reg);

	pmu_name = devm_kasprintf(&hisi_ptt->pdev->dev, GFP_KERNEL, "hisi_ptt%u_%u",
				  sicl_id, core_id);
	if (!pmu_name)
		return -ENOMEM;

	ret = perf_pmu_register(&hisi_ptt->hisi_ptt_pmu, pmu_name, -1);
	if (ret)
		return ret;

	return devm_add_action_or_reset(&hisi_ptt->pdev->dev,
					hisi_ptt_unregister_pmu,
					&hisi_ptt->hisi_ptt_pmu);
}

static void hisi_ptt_unregister_filter_update_notifier(void *data)
{
	struct hisi_ptt *hisi_ptt = data;

	bus_unregister_notifier(&pci_bus_type, &hisi_ptt->hisi_ptt_nb);

	/* Cancel any work that has been queued */
	cancel_delayed_work_sync(&hisi_ptt->work);
}

/* Register the bus notifier for dynamically updating the filter list */
static int hisi_ptt_register_filter_update_notifier(struct hisi_ptt *hisi_ptt)
{
	int ret;

	hisi_ptt->hisi_ptt_nb.notifier_call = hisi_ptt_notifier_call;
	ret = bus_register_notifier(&pci_bus_type, &hisi_ptt->hisi_ptt_nb);
	if (ret)
		return ret;

	return devm_add_action_or_reset(&hisi_ptt->pdev->dev,
					hisi_ptt_unregister_filter_update_notifier,
					hisi_ptt);
}

/*
 * The DMA of PTT trace can only use direct mappings due to some
 * hardware restriction. Check whether there is no IOMMU or the
 * policy of the IOMMU domain is passthrough, otherwise the trace
 * cannot work.
 *
 * The PTT device is supposed to behind an ARM SMMUv3, which
 * should have passthrough the device by a quirk.
 */
static int hisi_ptt_check_iommu_mapping(struct pci_dev *pdev)
{
	struct iommu_domain *iommu_domain;

	iommu_domain = iommu_get_domain_for_dev(&pdev->dev);
	if (!iommu_domain || iommu_domain->type == IOMMU_DOMAIN_IDENTITY)
		return 0;

	return -EOPNOTSUPP;
}

static int hisi_ptt_probe(struct pci_dev *pdev,
			  const struct pci_device_id *id)
{
	struct hisi_ptt *hisi_ptt;
	int ret;

	ret = hisi_ptt_check_iommu_mapping(pdev);
	if (ret) {
		pci_err(pdev, "requires direct DMA mappings\n");
		return ret;
	}

	hisi_ptt = devm_kzalloc(&pdev->dev, sizeof(*hisi_ptt), GFP_KERNEL);
	if (!hisi_ptt)
		return -ENOMEM;

	hisi_ptt->pdev = pdev;
	pci_set_drvdata(pdev, hisi_ptt);

	ret = pcim_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "failed to enable device, ret = %d\n", ret);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(2), DRV_NAME);
	if (ret) {
		pci_err(pdev, "failed to remap io memory, ret = %d\n", ret);
		return ret;
	}

	hisi_ptt->iobase = pcim_iomap_table(pdev)[2];

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		pci_err(pdev, "failed to set 64 bit dma mask, ret = %d\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	ret = hisi_ptt_register_irq(hisi_ptt);
	if (ret)
		return ret;

	ret = hisi_ptt_init_ctrls(hisi_ptt);
	if (ret) {
		pci_err(pdev, "failed to init controls, ret = %d\n", ret);
		return ret;
	}

	ret = hisi_ptt_register_filter_update_notifier(hisi_ptt);
	if (ret)
		pci_warn(pdev, "failed to register filter update notifier, ret = %d", ret);

	ret = hisi_ptt_register_pmu(hisi_ptt);
	if (ret) {
		pci_err(pdev, "failed to register PMU device, ret = %d", ret);
		return ret;
	}

	ret = hisi_ptt_init_filter_attributes(hisi_ptt);
	if (ret) {
		pci_err(pdev, "failed to init sysfs filter attributes, ret = %d", ret);
		return ret;
	}

	return 0;
}

static const struct pci_device_id hisi_ptt_id_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, 0xa12e) },
	{ }
};
MODULE_DEVICE_TABLE(pci, hisi_ptt_id_tbl);

static struct pci_driver hisi_ptt_driver = {
	.name = DRV_NAME,
	.id_table = hisi_ptt_id_tbl,
	.probe = hisi_ptt_probe,
};

static int hisi_ptt_cpu_teardown(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_ptt *hisi_ptt;
	struct device *dev;
	int target, src;

	hisi_ptt = hlist_entry_safe(node, struct hisi_ptt, hotplug_node);
	src = hisi_ptt->trace_ctrl.on_cpu;
	dev = hisi_ptt->hisi_ptt_pmu.dev;

	if (!hisi_ptt->trace_ctrl.started || src != cpu)
		return 0;

	target = cpumask_any_but(cpumask_of_node(dev_to_node(&hisi_ptt->pdev->dev)), cpu);
	if (target >= nr_cpu_ids) {
		dev_err(dev, "no available cpu for perf context migration\n");
		return 0;
	}

	perf_pmu_migrate_context(&hisi_ptt->hisi_ptt_pmu, src, target);

	/*
	 * Also make sure the interrupt bind to the migrated CPU as well. Warn
	 * the user on failure here.
	 */
	if (irq_set_affinity(hisi_ptt->trace_irq, cpumask_of(target)))
		dev_warn(dev, "failed to set the affinity of trace interrupt\n");

	hisi_ptt->trace_ctrl.on_cpu = target;
	return 0;
}

static int __init hisi_ptt_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, DRV_NAME, NULL,
				      hisi_ptt_cpu_teardown);
	if (ret < 0)
		return ret;
	hisi_ptt_pmu_online = ret;

	ret = pci_register_driver(&hisi_ptt_driver);
	if (ret)
		cpuhp_remove_multi_state(hisi_ptt_pmu_online);

	return ret;
}
module_init(hisi_ptt_init);

static void __exit hisi_ptt_exit(void)
{
	pci_unregister_driver(&hisi_ptt_driver);
	cpuhp_remove_multi_state(hisi_ptt_pmu_online);
}
module_exit(hisi_ptt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yicong Yang <yangyicong@hisilicon.com>");
MODULE_DESCRIPTION("Driver for HiSilicon PCIe tune and trace device");
