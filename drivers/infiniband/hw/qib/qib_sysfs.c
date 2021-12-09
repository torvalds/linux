/*
 * Copyright (c) 2012 Intel Corporation.  All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/ctype.h>
#include <rdma/ib_sysfs.h>

#include "qib.h"
#include "qib_mad.h"

static struct qib_pportdata *qib_get_pportdata_kobj(struct kobject *kobj)
{
	u32 port_num;
	struct ib_device *ibdev = ib_port_sysfs_get_ibdev_kobj(kobj, &port_num);
	struct qib_devdata *dd = dd_from_ibdev(ibdev);

	return &dd->pport[port_num - 1];
}

/*
 * Get/Set heartbeat enable. OR of 1=enabled, 2=auto
 */
static ssize_t hrtbt_enable_show(struct ib_device *ibdev, u32 port_num,
				 struct ib_port_attribute *attr, char *buf)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];

	return sysfs_emit(buf, "%d\n", dd->f_get_ib_cfg(ppd, QIB_IB_CFG_HRTBT));
}

static ssize_t hrtbt_enable_store(struct ib_device *ibdev, u32 port_num,
				  struct ib_port_attribute *attr,
				  const char *buf, size_t count)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];
	int ret;
	u16 val;

	ret = kstrtou16(buf, 0, &val);
	if (ret) {
		qib_dev_err(dd, "attempt to set invalid Heartbeat enable\n");
		return ret;
	}

	/*
	 * Set the "intentional" heartbeat enable per either of
	 * "Enable" and "Auto", as these are normally set together.
	 * This bit is consulted when leaving loopback mode,
	 * because entering loopback mode overrides it and automatically
	 * disables heartbeat.
	 */
	ret = dd->f_set_ib_cfg(ppd, QIB_IB_CFG_HRTBT, val);
	return ret < 0 ? ret : count;
}
static IB_PORT_ATTR_RW(hrtbt_enable);

static ssize_t loopback_store(struct ib_device *ibdev, u32 port_num,
			      struct ib_port_attribute *attr, const char *buf,
			      size_t count)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];
	int ret = count, r;

	r = dd->f_set_ib_loopback(ppd, buf);
	if (r < 0)
		ret = r;

	return ret;
}
static IB_PORT_ATTR_WO(loopback);

static ssize_t led_override_store(struct ib_device *ibdev, u32 port_num,
				  struct ib_port_attribute *attr,
				  const char *buf, size_t count)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];
	int ret;
	u16 val;

	ret = kstrtou16(buf, 0, &val);
	if (ret) {
		qib_dev_err(dd, "attempt to set invalid LED override\n");
		return ret;
	}

	qib_set_led_override(ppd, val);
	return count;
}
static IB_PORT_ATTR_WO(led_override);

static ssize_t status_show(struct ib_device *ibdev, u32 port_num,
			   struct ib_port_attribute *attr, char *buf)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];

	if (!ppd->statusp)
		return -EINVAL;

	return sysfs_emit(buf, "0x%llx\n", (unsigned long long)*(ppd->statusp));
}
static IB_PORT_ATTR_RO(status);

/*
 * For userland compatibility, these offsets must remain fixed.
 * They are strings for QIB_STATUS_*
 */
static const char * const qib_status_str[] = {
	"Initted",
	"",
	"",
	"",
	"",
	"Present",
	"IB_link_up",
	"IB_configured",
	"",
	"Fatal_Hardware_Error",
	NULL,
};

static ssize_t status_str_show(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_attribute *attr, char *buf)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];
	int i, any;
	u64 s;
	ssize_t ret;

	if (!ppd->statusp) {
		ret = -EINVAL;
		goto bail;
	}

	s = *(ppd->statusp);
	*buf = '\0';
	for (any = i = 0; s && qib_status_str[i]; i++) {
		if (s & 1) {
			/* if overflow */
			if (any && strlcat(buf, " ", PAGE_SIZE) >= PAGE_SIZE)
				break;
			if (strlcat(buf, qib_status_str[i], PAGE_SIZE) >=
					PAGE_SIZE)
				break;
			any = 1;
		}
		s >>= 1;
	}
	if (any)
		strlcat(buf, "\n", PAGE_SIZE);

	ret = strlen(buf);

bail:
	return ret;
}
static IB_PORT_ATTR_RO(status_str);

/* end of per-port functions */

static struct attribute *port_linkcontrol_attributes[] = {
	&ib_port_attr_loopback.attr,
	&ib_port_attr_led_override.attr,
	&ib_port_attr_hrtbt_enable.attr,
	&ib_port_attr_status.attr,
	&ib_port_attr_status_str.attr,
	NULL
};

static const struct attribute_group port_linkcontrol_group = {
	.name = "linkcontrol",
	.attrs = port_linkcontrol_attributes,
};

/*
 * Start of per-port congestion control structures and support code
 */

/*
 * Congestion control table size followed by table entries
 */
static ssize_t cc_table_bin_read(struct file *filp, struct kobject *kobj,
				 struct bin_attribute *bin_attr, char *buf,
				 loff_t pos, size_t count)
{
	struct qib_pportdata *ppd = qib_get_pportdata_kobj(kobj);
	int ret;

	if (!qib_cc_table_size || !ppd->ccti_entries_shadow)
		return -EINVAL;

	ret = ppd->total_cct_entry * sizeof(struct ib_cc_table_entry_shadow)
		 + sizeof(__be16);

	if (pos > ret)
		return -EINVAL;

	if (count > ret - pos)
		count = ret - pos;

	if (!count)
		return count;

	spin_lock(&ppd->cc_shadow_lock);
	memcpy(buf, ppd->ccti_entries_shadow, count);
	spin_unlock(&ppd->cc_shadow_lock);

	return count;
}
static BIN_ATTR_RO(cc_table_bin, PAGE_SIZE);

/*
 * Congestion settings: port control, control map and an array of 16
 * entries for the congestion entries - increase, timer, event log
 * trigger threshold and the minimum injection rate delay.
 */
static ssize_t cc_setting_bin_read(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *bin_attr, char *buf,
				   loff_t pos, size_t count)
{
	struct qib_pportdata *ppd = qib_get_pportdata_kobj(kobj);
	int ret;

	if (!qib_cc_table_size || !ppd->congestion_entries_shadow)
		return -EINVAL;

	ret = sizeof(struct ib_cc_congestion_setting_attr_shadow);

	if (pos > ret)
		return -EINVAL;
	if (count > ret - pos)
		count = ret - pos;

	if (!count)
		return count;

	spin_lock(&ppd->cc_shadow_lock);
	memcpy(buf, ppd->congestion_entries_shadow, count);
	spin_unlock(&ppd->cc_shadow_lock);

	return count;
}
static BIN_ATTR_RO(cc_setting_bin, PAGE_SIZE);

static struct bin_attribute *port_ccmgta_attributes[] = {
	&bin_attr_cc_setting_bin,
	&bin_attr_cc_table_bin,
	NULL,
};

static umode_t qib_ccmgta_is_bin_visible(struct kobject *kobj,
				 struct bin_attribute *attr, int n)
{
	struct qib_pportdata *ppd = qib_get_pportdata_kobj(kobj);

	if (!qib_cc_table_size || !ppd->congestion_entries_shadow)
		return 0;
	return attr->attr.mode;
}

static const struct attribute_group port_ccmgta_attribute_group = {
	.name = "CCMgtA",
	.is_bin_visible = qib_ccmgta_is_bin_visible,
	.bin_attrs = port_ccmgta_attributes,
};

/* Start sl2vl */

struct qib_sl2vl_attr {
	struct ib_port_attribute attr;
	int sl;
};

static ssize_t sl2vl_attr_show(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_attribute *attr, char *buf)
{
	struct qib_sl2vl_attr *sattr =
		container_of(attr, struct qib_sl2vl_attr, attr);
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return sysfs_emit(buf, "%u\n", qibp->sl_to_vl[sattr->sl]);
}

#define QIB_SL2VL_ATTR(N)                                                      \
	static struct qib_sl2vl_attr qib_sl2vl_attr_##N = {                    \
		.attr = __ATTR(N, 0444, sl2vl_attr_show, NULL),                \
		.sl = N,                                                       \
	}

QIB_SL2VL_ATTR(0);
QIB_SL2VL_ATTR(1);
QIB_SL2VL_ATTR(2);
QIB_SL2VL_ATTR(3);
QIB_SL2VL_ATTR(4);
QIB_SL2VL_ATTR(5);
QIB_SL2VL_ATTR(6);
QIB_SL2VL_ATTR(7);
QIB_SL2VL_ATTR(8);
QIB_SL2VL_ATTR(9);
QIB_SL2VL_ATTR(10);
QIB_SL2VL_ATTR(11);
QIB_SL2VL_ATTR(12);
QIB_SL2VL_ATTR(13);
QIB_SL2VL_ATTR(14);
QIB_SL2VL_ATTR(15);

static struct attribute *port_sl2vl_attributes[] = {
	&qib_sl2vl_attr_0.attr.attr,
	&qib_sl2vl_attr_1.attr.attr,
	&qib_sl2vl_attr_2.attr.attr,
	&qib_sl2vl_attr_3.attr.attr,
	&qib_sl2vl_attr_4.attr.attr,
	&qib_sl2vl_attr_5.attr.attr,
	&qib_sl2vl_attr_6.attr.attr,
	&qib_sl2vl_attr_7.attr.attr,
	&qib_sl2vl_attr_8.attr.attr,
	&qib_sl2vl_attr_9.attr.attr,
	&qib_sl2vl_attr_10.attr.attr,
	&qib_sl2vl_attr_11.attr.attr,
	&qib_sl2vl_attr_12.attr.attr,
	&qib_sl2vl_attr_13.attr.attr,
	&qib_sl2vl_attr_14.attr.attr,
	&qib_sl2vl_attr_15.attr.attr,
	NULL
};

static const struct attribute_group port_sl2vl_group = {
	.name = "sl2vl",
	.attrs = port_sl2vl_attributes,
};

/* End sl2vl */

/* Start diag_counters */

struct qib_diagc_attr {
	struct ib_port_attribute attr;
	size_t counter;
};

static ssize_t diagc_attr_show(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_attribute *attr, char *buf)
{
	struct qib_diagc_attr *dattr =
		container_of(attr, struct qib_diagc_attr, attr);
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return sysfs_emit(buf, "%llu\n", *((u64 *)qibp + dattr->counter));
}

static ssize_t diagc_attr_store(struct ib_device *ibdev, u32 port_num,
				struct ib_port_attribute *attr, const char *buf,
				size_t count)
{
	struct qib_diagc_attr *dattr =
		container_of(attr, struct qib_diagc_attr, attr);
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;
	u64 val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;
	*((u64 *)qibp + dattr->counter) = val;
	return count;
}

#define QIB_DIAGC_ATTR(N)                                                      \
	static_assert(__same_type(((struct qib_ibport *)0)->rvp.n_##N, u64));  \
	static struct qib_diagc_attr qib_diagc_attr_##N = {                    \
		.attr = __ATTR(N, 0664, diagc_attr_show, diagc_attr_store),    \
		.counter =                                                     \
			offsetof(struct qib_ibport, rvp.n_##N) / sizeof(u64)   \
	}

QIB_DIAGC_ATTR(rc_resends);
QIB_DIAGC_ATTR(seq_naks);
QIB_DIAGC_ATTR(rdma_seq);
QIB_DIAGC_ATTR(rnr_naks);
QIB_DIAGC_ATTR(other_naks);
QIB_DIAGC_ATTR(rc_timeouts);
QIB_DIAGC_ATTR(loop_pkts);
QIB_DIAGC_ATTR(pkt_drops);
QIB_DIAGC_ATTR(dmawait);
QIB_DIAGC_ATTR(unaligned);
QIB_DIAGC_ATTR(rc_dupreq);
QIB_DIAGC_ATTR(rc_seqnak);
QIB_DIAGC_ATTR(rc_crwaits);

static u64 get_all_cpu_total(u64 __percpu *cntr)
{
	int cpu;
	u64 counter = 0;

	for_each_possible_cpu(cpu)
		counter += *per_cpu_ptr(cntr, cpu);
	return counter;
}

static ssize_t qib_store_per_cpu(struct qib_devdata *dd, const char *buf,
				 size_t count, u64 *zero, u64 cur)
{
	u32 val;
	int ret;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;
	if (val != 0) {
		qib_dev_err(dd, "Per CPU cntrs can only be zeroed");
		return count;
	}
	*zero = cur;
	return count;
}

static ssize_t rc_acks_show(struct ib_device *ibdev, u32 port_num,
			    struct ib_port_attribute *attr, char *buf)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return sysfs_emit(buf, "%llu\n",
			  get_all_cpu_total(qibp->rvp.rc_acks) -
				  qibp->rvp.z_rc_acks);
}

static ssize_t rc_acks_store(struct ib_device *ibdev, u32 port_num,
			     struct ib_port_attribute *attr, const char *buf,
			     size_t count)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return qib_store_per_cpu(dd, buf, count, &qibp->rvp.z_rc_acks,
				 get_all_cpu_total(qibp->rvp.rc_acks));
}
static IB_PORT_ATTR_RW(rc_acks);

static ssize_t rc_qacks_show(struct ib_device *ibdev, u32 port_num,
			     struct ib_port_attribute *attr, char *buf)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return sysfs_emit(buf, "%llu\n",
			  get_all_cpu_total(qibp->rvp.rc_qacks) -
				  qibp->rvp.z_rc_qacks);
}

static ssize_t rc_qacks_store(struct ib_device *ibdev, u32 port_num,
			      struct ib_port_attribute *attr, const char *buf,
			      size_t count)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return qib_store_per_cpu(dd, buf, count, &qibp->rvp.z_rc_qacks,
				 get_all_cpu_total(qibp->rvp.rc_qacks));
}
static IB_PORT_ATTR_RW(rc_qacks);

static ssize_t rc_delayed_comp_show(struct ib_device *ibdev, u32 port_num,
				    struct ib_port_attribute *attr, char *buf)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return sysfs_emit(buf, "%llu\n",
			 get_all_cpu_total(qibp->rvp.rc_delayed_comp) -
				 qibp->rvp.z_rc_delayed_comp);
}

static ssize_t rc_delayed_comp_store(struct ib_device *ibdev, u32 port_num,
				     struct ib_port_attribute *attr,
				     const char *buf, size_t count)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *qibp = &dd->pport[port_num - 1].ibport_data;

	return qib_store_per_cpu(dd, buf, count, &qibp->rvp.z_rc_delayed_comp,
				 get_all_cpu_total(qibp->rvp.rc_delayed_comp));
}
static IB_PORT_ATTR_RW(rc_delayed_comp);

static struct attribute *port_diagc_attributes[] = {
	&qib_diagc_attr_rc_resends.attr.attr,
	&qib_diagc_attr_seq_naks.attr.attr,
	&qib_diagc_attr_rdma_seq.attr.attr,
	&qib_diagc_attr_rnr_naks.attr.attr,
	&qib_diagc_attr_other_naks.attr.attr,
	&qib_diagc_attr_rc_timeouts.attr.attr,
	&qib_diagc_attr_loop_pkts.attr.attr,
	&qib_diagc_attr_pkt_drops.attr.attr,
	&qib_diagc_attr_dmawait.attr.attr,
	&qib_diagc_attr_unaligned.attr.attr,
	&qib_diagc_attr_rc_dupreq.attr.attr,
	&qib_diagc_attr_rc_seqnak.attr.attr,
	&qib_diagc_attr_rc_crwaits.attr.attr,
	&ib_port_attr_rc_acks.attr,
	&ib_port_attr_rc_qacks.attr,
	&ib_port_attr_rc_delayed_comp.attr,
	NULL
};

static const struct attribute_group port_diagc_group = {
	.name = "linkcontrol",
	.attrs = port_diagc_attributes,
};

/* End diag_counters */

const struct attribute_group *qib_attr_port_groups[] = {
	&port_linkcontrol_group,
	&port_ccmgta_attribute_group,
	&port_sl2vl_group,
	&port_diagc_group,
	NULL,
};

/* end of per-port file structures and support code */

/*
 * Start of per-unit (or driver, in some cases, but replicated
 * per unit) functions (these get a device *)
 */
static ssize_t hw_rev_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);

	return sysfs_emit(buf, "%x\n", dd_from_dev(dev)->minrev);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t hca_type_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);

	if (!dd->boardname)
		return -EINVAL;
	return sysfs_emit(buf, "%s\n", dd->boardname);
}
static DEVICE_ATTR_RO(hca_type);
static DEVICE_ATTR(board_id, 0444, hca_type_show, NULL);

static ssize_t version_show(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	/* The string printed here is already newline-terminated. */
	return sysfs_emit(buf, "%s", (char *)ib_qib_version);
}
static DEVICE_ATTR_RO(version);

static ssize_t boardversion_show(struct device *device,
				 struct device_attribute *attr, char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);

	/* The string printed here is already newline-terminated. */
	return sysfs_emit(buf, "%s", dd->boardversion);
}
static DEVICE_ATTR_RO(boardversion);

static ssize_t localbus_info_show(struct device *device,
				  struct device_attribute *attr, char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);

	/* The string printed here is already newline-terminated. */
	return sysfs_emit(buf, "%s", dd->lbus_info);
}
static DEVICE_ATTR_RO(localbus_info);

static ssize_t nctxts_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);

	/* Return the number of user ports (contexts) available. */
	/* The calculation below deals with a special case where
	 * cfgctxts is set to 1 on a single-port board. */
	return sysfs_emit(buf, "%u\n",
			  (dd->first_user_ctxt > dd->cfgctxts) ?
				  0 :
				  (dd->cfgctxts - dd->first_user_ctxt));
}
static DEVICE_ATTR_RO(nctxts);

static ssize_t nfreectxts_show(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);

	/* Return the number of free user ports (contexts) available. */
	return sysfs_emit(buf, "%u\n", dd->freectxts);
}
static DEVICE_ATTR_RO(nfreectxts);

static ssize_t serial_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);
	const u8 *end = memchr(dd->serial, 0, ARRAY_SIZE(dd->serial));
	int size = end ? end - dd->serial : ARRAY_SIZE(dd->serial);

	return sysfs_emit(buf, ".%*s\n", size, dd->serial);
}
static DEVICE_ATTR_RO(serial);

static ssize_t chip_reset_store(struct device *device,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);
	int ret;

	if (count < 5 || memcmp(buf, "reset", 5) || !dd->diag_client) {
		ret = -EINVAL;
		goto bail;
	}

	ret = qib_reset_device(dd->unit);
bail:
	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_WO(chip_reset);

/*
 * Dump tempsense regs. in decimal, to ease shell-scripts.
 */
static ssize_t tempsense_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct qib_ibdev *dev =
		rdma_device_to_drv_device(device, struct qib_ibdev, rdi.ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);
	int i;
	u8 regvals[8];

	for (i = 0; i < 8; i++) {
		int ret;

		if (i == 6)
			continue;
		ret = dd->f_tempsense_rd(dd, i);
		if (ret < 0)
			return ret;	/* return error on bad read */
		regvals[i] = ret;
	}
	return sysfs_emit(buf, "%d %d %02X %02X %d %d\n",
			  (signed char)regvals[0],
			  (signed char)regvals[1],
			  regvals[2],
			  regvals[3],
			  (signed char)regvals[5],
			  (signed char)regvals[7]);
}
static DEVICE_ATTR_RO(tempsense);

/*
 * end of per-unit (or driver, in some cases, but replicated
 * per unit) functions
 */

/* start of per-unit file structures and support code */
static struct attribute *qib_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	&dev_attr_board_id.attr,
	&dev_attr_version.attr,
	&dev_attr_nctxts.attr,
	&dev_attr_nfreectxts.attr,
	&dev_attr_serial.attr,
	&dev_attr_boardversion.attr,
	&dev_attr_tempsense.attr,
	&dev_attr_localbus_info.attr,
	&dev_attr_chip_reset.attr,
	NULL,
};

const struct attribute_group qib_attr_group = {
	.attrs = qib_attributes,
};
