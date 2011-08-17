/*
 * Copyright (c) 2006, 2007, 2008 QLogic Corporation. All rights reserved.
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

#include "ipath_kernel.h"
#include "ipath_verbs.h"
#include "ipath_common.h"

/**
 * ipath_parse_ushort - parse an unsigned short value in an arbitrary base
 * @str: the string containing the number
 * @valp: where to put the result
 *
 * returns the number of bytes consumed, or negative value on error
 */
int ipath_parse_ushort(const char *str, unsigned short *valp)
{
	unsigned long val;
	char *end;
	int ret;

	if (!isdigit(str[0])) {
		ret = -EINVAL;
		goto bail;
	}

	val = simple_strtoul(str, &end, 0);

	if (val > 0xffff) {
		ret = -EINVAL;
		goto bail;
	}

	*valp = val;

	ret = end + 1 - str;
	if (ret == 0)
		ret = -EINVAL;

bail:
	return ret;
}

static ssize_t show_version(struct device_driver *dev, char *buf)
{
	/* The string printed here is already newline-terminated. */
	return scnprintf(buf, PAGE_SIZE, "%s", ib_ipath_version);
}

static ssize_t show_num_units(struct device_driver *dev, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 ipath_count_units(NULL, NULL, NULL));
}

static ssize_t show_status(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	ssize_t ret;

	if (!dd->ipath_statusp) {
		ret = -EINVAL;
		goto bail;
	}

	ret = scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			(unsigned long long) *(dd->ipath_statusp));

bail:
	return ret;
}

static const char *ipath_status_str[] = {
	"Initted",
	"Disabled",
	"Admin_Disabled",
	"", /* This used to be the old "OIB_SMA" status. */
	"", /* This used to be the old "SMA" status. */
	"Present",
	"IB_link_up",
	"IB_configured",
	"NoIBcable",
	"Fatal_Hardware_Error",
	NULL,
};

static ssize_t show_status_str(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int i, any;
	u64 s;
	ssize_t ret;

	if (!dd->ipath_statusp) {
		ret = -EINVAL;
		goto bail;
	}

	s = *(dd->ipath_statusp);
	*buf = '\0';
	for (any = i = 0; s && ipath_status_str[i]; i++) {
		if (s & 1) {
			if (any && strlcat(buf, " ", PAGE_SIZE) >=
			    PAGE_SIZE)
				/* overflow */
				break;
			if (strlcat(buf, ipath_status_str[i],
				    PAGE_SIZE) >= PAGE_SIZE)
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

static ssize_t show_boardversion(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	/* The string printed here is already newline-terminated. */
	return scnprintf(buf, PAGE_SIZE, "%s", dd->ipath_boardversion);
}

static ssize_t show_localbus_info(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	/* The string printed here is already newline-terminated. */
	return scnprintf(buf, PAGE_SIZE, "%s", dd->ipath_lbus_info);
}

static ssize_t show_lmc(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", dd->ipath_lmc);
}

static ssize_t store_lmc(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf,
			 size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	u16 lmc = 0;
	int ret;

	ret = ipath_parse_ushort(buf, &lmc);
	if (ret < 0)
		goto invalid;

	if (lmc > 7) {
		ret = -EINVAL;
		goto invalid;
	}

	ipath_set_lid(dd, dd->ipath_lid, lmc);

	goto bail;
invalid:
	ipath_dev_err(dd, "attempt to set invalid LMC %u\n", lmc);
bail:
	return ret;
}

static ssize_t show_lid(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", dd->ipath_lid);
}

static ssize_t store_lid(struct device *dev,
			 struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	u16 lid = 0;
	int ret;

	ret = ipath_parse_ushort(buf, &lid);
	if (ret < 0)
		goto invalid;

	if (lid == 0 || lid >= IPATH_MULTICAST_LID_BASE) {
		ret = -EINVAL;
		goto invalid;
	}

	ipath_set_lid(dd, lid, dd->ipath_lmc);

	goto bail;
invalid:
	ipath_dev_err(dd, "attempt to set invalid LID 0x%x\n", lid);
bail:
	return ret;
}

static ssize_t show_mlid(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", dd->ipath_mlid);
}

static ssize_t store_mlid(struct device *dev,
			 struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	u16 mlid;
	int ret;

	ret = ipath_parse_ushort(buf, &mlid);
	if (ret < 0 || mlid < IPATH_MULTICAST_LID_BASE)
		goto invalid;

	dd->ipath_mlid = mlid;

	goto bail;
invalid:
	ipath_dev_err(dd, "attempt to set invalid MLID\n");
bail:
	return ret;
}

static ssize_t show_guid(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	u8 *guid;

	guid = (u8 *) & (dd->ipath_guid);

	return scnprintf(buf, PAGE_SIZE,
			 "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			 guid[0], guid[1], guid[2], guid[3],
			 guid[4], guid[5], guid[6], guid[7]);
}

static ssize_t store_guid(struct device *dev,
			 struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	ssize_t ret;
	unsigned short guid[8];
	__be64 new_guid;
	u8 *ng;
	int i;

	if (sscanf(buf, "%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx",
		   &guid[0], &guid[1], &guid[2], &guid[3],
		   &guid[4], &guid[5], &guid[6], &guid[7]) != 8)
		goto invalid;

	ng = (u8 *) &new_guid;

	for (i = 0; i < 8; i++) {
		if (guid[i] > 0xff)
			goto invalid;
		ng[i] = guid[i];
	}

	if (new_guid == 0)
		goto invalid;

	dd->ipath_guid = new_guid;
	dd->ipath_nguid = 1;
	if (dd->verbs_dev)
		dd->verbs_dev->ibdev.node_guid = new_guid;

	ret = strlen(buf);
	goto bail;

invalid:
	ipath_dev_err(dd, "attempt to set invalid GUID\n");
	ret = -EINVAL;

bail:
	return ret;
}

static ssize_t show_nguid(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", dd->ipath_nguid);
}

static ssize_t show_nports(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	/* Return the number of user ports available. */
	return scnprintf(buf, PAGE_SIZE, "%u\n", dd->ipath_cfgports - 1);
}

static ssize_t show_serial(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	buf[sizeof dd->ipath_serial] = '\0';
	memcpy(buf, dd->ipath_serial, sizeof dd->ipath_serial);
	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t show_unit(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", dd->ipath_unit);
}

static ssize_t show_jint_max_packets(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%hu\n", dd->ipath_jint_max_packets);
}

static ssize_t store_jint_max_packets(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	u16 v = 0;
	int ret;

	ret = ipath_parse_ushort(buf, &v);
	if (ret < 0)
		ipath_dev_err(dd, "invalid jint_max_packets.\n");
	else
		dd->ipath_f_config_jint(dd, dd->ipath_jint_idle_ticks, v);

	return ret;
}

static ssize_t show_jint_idle_ticks(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%hu\n", dd->ipath_jint_idle_ticks);
}

static ssize_t store_jint_idle_ticks(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	u16 v = 0;
	int ret;

	ret = ipath_parse_ushort(buf, &v);
	if (ret < 0)
		ipath_dev_err(dd, "invalid jint_idle_ticks.\n");
	else
		dd->ipath_f_config_jint(dd, v, dd->ipath_jint_max_packets);

	return ret;
}

#define DEVICE_COUNTER(name, attr) \
	static ssize_t show_counter_##name(struct device *dev, \
					   struct device_attribute *attr, \
					   char *buf) \
	{ \
		struct ipath_devdata *dd = dev_get_drvdata(dev); \
		return scnprintf(\
			buf, PAGE_SIZE, "%llu\n", (unsigned long long) \
			ipath_snap_cntr( \
				dd, offsetof(struct infinipath_counters, \
					     attr) / sizeof(u64)));	\
	} \
	static DEVICE_ATTR(name, S_IRUGO, show_counter_##name, NULL);

DEVICE_COUNTER(ib_link_downeds, IBLinkDownedCnt);
DEVICE_COUNTER(ib_link_err_recoveries, IBLinkErrRecoveryCnt);
DEVICE_COUNTER(ib_status_changes, IBStatusChangeCnt);
DEVICE_COUNTER(ib_symbol_errs, IBSymbolErrCnt);
DEVICE_COUNTER(lb_flow_stalls, LBFlowStallCnt);
DEVICE_COUNTER(lb_ints, LBIntCnt);
DEVICE_COUNTER(rx_bad_formats, RxBadFormatCnt);
DEVICE_COUNTER(rx_buf_ovfls, RxBufOvflCnt);
DEVICE_COUNTER(rx_data_pkts, RxDataPktCnt);
DEVICE_COUNTER(rx_dropped_pkts, RxDroppedPktCnt);
DEVICE_COUNTER(rx_dwords, RxDwordCnt);
DEVICE_COUNTER(rx_ebps, RxEBPCnt);
DEVICE_COUNTER(rx_flow_ctrl_errs, RxFlowCtrlErrCnt);
DEVICE_COUNTER(rx_flow_pkts, RxFlowPktCnt);
DEVICE_COUNTER(rx_icrc_errs, RxICRCErrCnt);
DEVICE_COUNTER(rx_len_errs, RxLenErrCnt);
DEVICE_COUNTER(rx_link_problems, RxLinkProblemCnt);
DEVICE_COUNTER(rx_lpcrc_errs, RxLPCRCErrCnt);
DEVICE_COUNTER(rx_max_min_len_errs, RxMaxMinLenErrCnt);
DEVICE_COUNTER(rx_p0_hdr_egr_ovfls, RxP0HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p1_hdr_egr_ovfls, RxP1HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p2_hdr_egr_ovfls, RxP2HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p3_hdr_egr_ovfls, RxP3HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p4_hdr_egr_ovfls, RxP4HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p5_hdr_egr_ovfls, RxP5HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p6_hdr_egr_ovfls, RxP6HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p7_hdr_egr_ovfls, RxP7HdrEgrOvflCnt);
DEVICE_COUNTER(rx_p8_hdr_egr_ovfls, RxP8HdrEgrOvflCnt);
DEVICE_COUNTER(rx_pkey_mismatches, RxPKeyMismatchCnt);
DEVICE_COUNTER(rx_tid_full_errs, RxTIDFullErrCnt);
DEVICE_COUNTER(rx_tid_valid_errs, RxTIDValidErrCnt);
DEVICE_COUNTER(rx_vcrc_errs, RxVCRCErrCnt);
DEVICE_COUNTER(tx_data_pkts, TxDataPktCnt);
DEVICE_COUNTER(tx_dropped_pkts, TxDroppedPktCnt);
DEVICE_COUNTER(tx_dwords, TxDwordCnt);
DEVICE_COUNTER(tx_flow_pkts, TxFlowPktCnt);
DEVICE_COUNTER(tx_flow_stalls, TxFlowStallCnt);
DEVICE_COUNTER(tx_len_errs, TxLenErrCnt);
DEVICE_COUNTER(tx_max_min_len_errs, TxMaxMinLenErrCnt);
DEVICE_COUNTER(tx_underruns, TxUnderrunCnt);
DEVICE_COUNTER(tx_unsup_vl_errs, TxUnsupVLErrCnt);

static struct attribute *dev_counter_attributes[] = {
	&dev_attr_ib_link_downeds.attr,
	&dev_attr_ib_link_err_recoveries.attr,
	&dev_attr_ib_status_changes.attr,
	&dev_attr_ib_symbol_errs.attr,
	&dev_attr_lb_flow_stalls.attr,
	&dev_attr_lb_ints.attr,
	&dev_attr_rx_bad_formats.attr,
	&dev_attr_rx_buf_ovfls.attr,
	&dev_attr_rx_data_pkts.attr,
	&dev_attr_rx_dropped_pkts.attr,
	&dev_attr_rx_dwords.attr,
	&dev_attr_rx_ebps.attr,
	&dev_attr_rx_flow_ctrl_errs.attr,
	&dev_attr_rx_flow_pkts.attr,
	&dev_attr_rx_icrc_errs.attr,
	&dev_attr_rx_len_errs.attr,
	&dev_attr_rx_link_problems.attr,
	&dev_attr_rx_lpcrc_errs.attr,
	&dev_attr_rx_max_min_len_errs.attr,
	&dev_attr_rx_p0_hdr_egr_ovfls.attr,
	&dev_attr_rx_p1_hdr_egr_ovfls.attr,
	&dev_attr_rx_p2_hdr_egr_ovfls.attr,
	&dev_attr_rx_p3_hdr_egr_ovfls.attr,
	&dev_attr_rx_p4_hdr_egr_ovfls.attr,
	&dev_attr_rx_p5_hdr_egr_ovfls.attr,
	&dev_attr_rx_p6_hdr_egr_ovfls.attr,
	&dev_attr_rx_p7_hdr_egr_ovfls.attr,
	&dev_attr_rx_p8_hdr_egr_ovfls.attr,
	&dev_attr_rx_pkey_mismatches.attr,
	&dev_attr_rx_tid_full_errs.attr,
	&dev_attr_rx_tid_valid_errs.attr,
	&dev_attr_rx_vcrc_errs.attr,
	&dev_attr_tx_data_pkts.attr,
	&dev_attr_tx_dropped_pkts.attr,
	&dev_attr_tx_dwords.attr,
	&dev_attr_tx_flow_pkts.attr,
	&dev_attr_tx_flow_stalls.attr,
	&dev_attr_tx_len_errs.attr,
	&dev_attr_tx_max_min_len_errs.attr,
	&dev_attr_tx_underruns.attr,
	&dev_attr_tx_unsup_vl_errs.attr,
	NULL
};

static struct attribute_group dev_counter_attr_group = {
	.name = "counters",
	.attrs = dev_counter_attributes
};

static ssize_t store_reset(struct device *dev,
			 struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	if (count < 5 || memcmp(buf, "reset", 5)) {
		ret = -EINVAL;
		goto bail;
	}

	if (dd->ipath_flags & IPATH_DISABLED) {
		/*
		 * post-reset init would re-enable interrupts, etc.
		 * so don't allow reset on disabled devices.  Not
		 * perfect error, but about the best choice.
		 */
		dev_info(dev,"Unit %d is disabled, can't reset\n",
			 dd->ipath_unit);
		ret = -EINVAL;
		goto bail;
	}
	ret = ipath_reset_device(dd->ipath_unit);
bail:
	return ret<0 ? ret : count;
}

static ssize_t store_link_state(struct device *dev,
			 struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, r;
	u16 state;

	ret = ipath_parse_ushort(buf, &state);
	if (ret < 0)
		goto invalid;

	r = ipath_set_linkstate(dd, state);
	if (r < 0) {
		ret = r;
		goto bail;
	}

	goto bail;
invalid:
	ipath_dev_err(dd, "attempt to set invalid link state\n");
bail:
	return ret;
}

static ssize_t show_mtu(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%u\n", dd->ipath_ibmtu);
}

static ssize_t store_mtu(struct device *dev,
			 struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	ssize_t ret;
	u16 mtu = 0;
	int r;

	ret = ipath_parse_ushort(buf, &mtu);
	if (ret < 0)
		goto invalid;

	r = ipath_set_mtu(dd, mtu);
	if (r < 0)
		ret = r;

	goto bail;
invalid:
	ipath_dev_err(dd, "attempt to set invalid MTU\n");
bail:
	return ret;
}

static ssize_t show_enabled(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (dd->ipath_flags & IPATH_DISABLED) ? 0 : 1);
}

static ssize_t store_enabled(struct device *dev,
			 struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	ssize_t ret;
	u16 enable = 0;

	ret = ipath_parse_ushort(buf, &enable);
	if (ret < 0) {
		ipath_dev_err(dd, "attempt to use non-numeric on enable\n");
		goto bail;
	}

	if (enable) {
		if (!(dd->ipath_flags & IPATH_DISABLED))
			goto bail;

		dev_info(dev, "Enabling unit %d\n", dd->ipath_unit);
		/* same as post-reset */
		ret = ipath_init_chip(dd, 1);
		if (ret)
			ipath_dev_err(dd, "Failed to enable unit %d\n",
				      dd->ipath_unit);
		else {
			dd->ipath_flags &= ~IPATH_DISABLED;
			*dd->ipath_statusp &= ~IPATH_STATUS_ADMIN_DISABLED;
		}
	}
	else if (!(dd->ipath_flags & IPATH_DISABLED)) {
		dev_info(dev, "Disabling unit %d\n", dd->ipath_unit);
		ipath_shutdown_device(dd);
		dd->ipath_flags |= IPATH_DISABLED;
		*dd->ipath_statusp |= IPATH_STATUS_ADMIN_DISABLED;
	}

bail:
	return ret;
}

static ssize_t store_rx_pol_inv(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, r;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret < 0)
		goto invalid;

	r = ipath_set_rx_pol_inv(dd, val);
	if (r < 0) {
		ret = r;
		goto bail;
	}

	goto bail;
invalid:
	ipath_dev_err(dd, "attempt to set invalid Rx Polarity invert\n");
bail:
	return ret;
}

static ssize_t store_led_override(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret > 0)
		ipath_set_led_override(dd, val);
	else
		ipath_dev_err(dd, "attempt to set invalid LED override\n");
	return ret;
}

static ssize_t show_logged_errs(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int idx, count;

	/* force consistency with actual EEPROM */
	if (ipath_update_eeprom_log(dd) != 0)
		return -ENXIO;

	count = 0;
	for (idx = 0; idx < IPATH_EEP_LOG_CNT; ++idx) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%d%c",
			dd->ipath_eep_st_errs[idx],
			idx == (IPATH_EEP_LOG_CNT - 1) ? '\n' : ' ');
	}

	return count;
}

/*
 * New sysfs entries to control various IB config. These all turn into
 * accesses via ipath_f_get/set_ib_cfg.
 *
 * Get/Set heartbeat enable. Or of 1=enabled, 2=auto
 */
static ssize_t show_hrtbt_enb(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	ret = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_HRTBT);
	if (ret >= 0)
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return ret;
}

static ssize_t store_hrtbt_enb(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, r;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret >= 0 && val > 3)
		ret = -EINVAL;
	if (ret < 0) {
		ipath_dev_err(dd, "attempt to set invalid Heartbeat enable\n");
		goto bail;
	}

	/*
	 * Set the "intentional" heartbeat enable per either of
	 * "Enable" and "Auto", as these are normally set together.
	 * This bit is consulted when leaving loopback mode,
	 * because entering loopback mode overrides it and automatically
	 * disables heartbeat.
	 */
	r = dd->ipath_f_set_ib_cfg(dd, IPATH_IB_CFG_HRTBT, val);
	if (r < 0)
		ret = r;
	else if (val == IPATH_IB_HRTBT_OFF)
		dd->ipath_flags |= IPATH_NO_HRTBT;
	else
		dd->ipath_flags &= ~IPATH_NO_HRTBT;

bail:
	return ret;
}

/*
 * Get/Set Link-widths enabled. Or of 1=1x, 2=4x (this is human/IB centric,
 * _not_ the particular encoding of any given chip)
 */
static ssize_t show_lwid_enb(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	ret = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_LWID_ENB);
	if (ret >= 0)
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return ret;
}

static ssize_t store_lwid_enb(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, r;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret >= 0 && (val == 0 || val > 3))
		ret = -EINVAL;
	if (ret < 0) {
		ipath_dev_err(dd,
			"attempt to set invalid Link Width (enable)\n");
		goto bail;
	}

	r = dd->ipath_f_set_ib_cfg(dd, IPATH_IB_CFG_LWID_ENB, val);
	if (r < 0)
		ret = r;

bail:
	return ret;
}

/* Get current link width */
static ssize_t show_lwid(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)

{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	ret = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_LWID);
	if (ret >= 0)
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return ret;
}

/*
 * Get/Set Link-speeds enabled. Or of 1=SDR 2=DDR.
 */
static ssize_t show_spd_enb(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	ret = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_SPD_ENB);
	if (ret >= 0)
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return ret;
}

static ssize_t store_spd_enb(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, r;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret >= 0 && (val == 0 || val > (IPATH_IB_SDR | IPATH_IB_DDR)))
		ret = -EINVAL;
	if (ret < 0) {
		ipath_dev_err(dd,
			"attempt to set invalid Link Speed (enable)\n");
		goto bail;
	}

	r = dd->ipath_f_set_ib_cfg(dd, IPATH_IB_CFG_SPD_ENB, val);
	if (r < 0)
		ret = r;

bail:
	return ret;
}

/* Get current link speed */
static ssize_t show_spd(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	ret = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_SPD);
	if (ret >= 0)
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return ret;
}

/*
 * Get/Set RX polarity-invert enable. 0=no, 1=yes.
 */
static ssize_t show_rx_polinv_enb(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	ret = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_RXPOL_ENB);
	if (ret >= 0)
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return ret;
}

static ssize_t store_rx_polinv_enb(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, r;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret >= 0 && val > 1) {
		ipath_dev_err(dd,
			"attempt to set invalid Rx Polarity (enable)\n");
		ret = -EINVAL;
		goto bail;
	}

	r = dd->ipath_f_set_ib_cfg(dd, IPATH_IB_CFG_RXPOL_ENB, val);
	if (r < 0)
		ret = r;

bail:
	return ret;
}

/*
 * Get/Set RX lane-reversal enable. 0=no, 1=yes.
 */
static ssize_t show_lanerev_enb(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;

	ret = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_LREV_ENB);
	if (ret >= 0)
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return ret;
}

static ssize_t store_lanerev_enb(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, r;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret >= 0 && val > 1) {
		ret = -EINVAL;
		ipath_dev_err(dd,
			"attempt to set invalid Lane reversal (enable)\n");
		goto bail;
	}

	r = dd->ipath_f_set_ib_cfg(dd, IPATH_IB_CFG_LREV_ENB, val);
	if (r < 0)
		ret = r;

bail:
	return ret;
}

static DRIVER_ATTR(num_units, S_IRUGO, show_num_units, NULL);
static DRIVER_ATTR(version, S_IRUGO, show_version, NULL);

static struct attribute *driver_attributes[] = {
	&driver_attr_num_units.attr,
	&driver_attr_version.attr,
	NULL
};

static struct attribute_group driver_attr_group = {
	.attrs = driver_attributes
};

static ssize_t store_tempsense(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret, stat;
	u16 val;

	ret = ipath_parse_ushort(buf, &val);
	if (ret <= 0) {
		ipath_dev_err(dd, "attempt to set invalid tempsense config\n");
		goto bail;
	}
	/* If anything but the highest limit, enable T_CRIT_A "interrupt" */
	stat = ipath_tempsense_write(dd, 9, (val == 0x7f7f) ? 0x80 : 0);
	if (stat) {
		ipath_dev_err(dd, "Unable to set tempsense config\n");
		ret = -1;
		goto bail;
	}
	stat = ipath_tempsense_write(dd, 0xB, (u8) (val & 0xFF));
	if (stat) {
		ipath_dev_err(dd, "Unable to set local Tcrit\n");
		ret = -1;
		goto bail;
	}
	stat = ipath_tempsense_write(dd, 0xD, (u8) (val >> 8));
	if (stat) {
		ipath_dev_err(dd, "Unable to set remote Tcrit\n");
		ret = -1;
		goto bail;
	}

bail:
	return ret;
}

/*
 * dump tempsense regs. in decimal, to ease shell-scripts.
 */
static ssize_t show_tempsense(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct ipath_devdata *dd = dev_get_drvdata(dev);
	int ret;
	int idx;
	u8 regvals[8];

	ret = -ENXIO;
	for (idx = 0; idx < 8; ++idx) {
		if (idx == 6)
			continue;
		ret = ipath_tempsense_read(dd, idx);
		if (ret < 0)
			break;
		regvals[idx] = ret;
	}
	if (idx == 8)
		ret = scnprintf(buf, PAGE_SIZE, "%d %d %02X %02X %d %d\n",
			*(signed char *)(regvals),
			*(signed char *)(regvals + 1),
			regvals[2], regvals[3],
			*(signed char *)(regvals + 5),
			*(signed char *)(regvals + 7));
	return ret;
}

const struct attribute_group *ipath_driver_attr_groups[] = {
	&driver_attr_group,
	NULL,
};

static DEVICE_ATTR(guid, S_IWUSR | S_IRUGO, show_guid, store_guid);
static DEVICE_ATTR(lmc, S_IWUSR | S_IRUGO, show_lmc, store_lmc);
static DEVICE_ATTR(lid, S_IWUSR | S_IRUGO, show_lid, store_lid);
static DEVICE_ATTR(link_state, S_IWUSR, NULL, store_link_state);
static DEVICE_ATTR(mlid, S_IWUSR | S_IRUGO, show_mlid, store_mlid);
static DEVICE_ATTR(mtu, S_IWUSR | S_IRUGO, show_mtu, store_mtu);
static DEVICE_ATTR(enabled, S_IWUSR | S_IRUGO, show_enabled, store_enabled);
static DEVICE_ATTR(nguid, S_IRUGO, show_nguid, NULL);
static DEVICE_ATTR(nports, S_IRUGO, show_nports, NULL);
static DEVICE_ATTR(reset, S_IWUSR, NULL, store_reset);
static DEVICE_ATTR(serial, S_IRUGO, show_serial, NULL);
static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);
static DEVICE_ATTR(status_str, S_IRUGO, show_status_str, NULL);
static DEVICE_ATTR(boardversion, S_IRUGO, show_boardversion, NULL);
static DEVICE_ATTR(unit, S_IRUGO, show_unit, NULL);
static DEVICE_ATTR(rx_pol_inv, S_IWUSR, NULL, store_rx_pol_inv);
static DEVICE_ATTR(led_override, S_IWUSR, NULL, store_led_override);
static DEVICE_ATTR(logged_errors, S_IRUGO, show_logged_errs, NULL);
static DEVICE_ATTR(localbus_info, S_IRUGO, show_localbus_info, NULL);
static DEVICE_ATTR(jint_max_packets, S_IWUSR | S_IRUGO,
		   show_jint_max_packets, store_jint_max_packets);
static DEVICE_ATTR(jint_idle_ticks, S_IWUSR | S_IRUGO,
		   show_jint_idle_ticks, store_jint_idle_ticks);
static DEVICE_ATTR(tempsense, S_IWUSR | S_IRUGO,
		   show_tempsense, store_tempsense);

static struct attribute *dev_attributes[] = {
	&dev_attr_guid.attr,
	&dev_attr_lmc.attr,
	&dev_attr_lid.attr,
	&dev_attr_link_state.attr,
	&dev_attr_mlid.attr,
	&dev_attr_mtu.attr,
	&dev_attr_nguid.attr,
	&dev_attr_nports.attr,
	&dev_attr_serial.attr,
	&dev_attr_status.attr,
	&dev_attr_status_str.attr,
	&dev_attr_boardversion.attr,
	&dev_attr_unit.attr,
	&dev_attr_enabled.attr,
	&dev_attr_rx_pol_inv.attr,
	&dev_attr_led_override.attr,
	&dev_attr_logged_errors.attr,
	&dev_attr_tempsense.attr,
	&dev_attr_localbus_info.attr,
	NULL
};

static struct attribute_group dev_attr_group = {
	.attrs = dev_attributes
};

static DEVICE_ATTR(hrtbt_enable, S_IWUSR | S_IRUGO, show_hrtbt_enb,
		   store_hrtbt_enb);
static DEVICE_ATTR(link_width_enable, S_IWUSR | S_IRUGO, show_lwid_enb,
		   store_lwid_enb);
static DEVICE_ATTR(link_width, S_IRUGO, show_lwid, NULL);
static DEVICE_ATTR(link_speed_enable, S_IWUSR | S_IRUGO, show_spd_enb,
		   store_spd_enb);
static DEVICE_ATTR(link_speed, S_IRUGO, show_spd, NULL);
static DEVICE_ATTR(rx_pol_inv_enable, S_IWUSR | S_IRUGO, show_rx_polinv_enb,
		   store_rx_polinv_enb);
static DEVICE_ATTR(rx_lane_rev_enable, S_IWUSR | S_IRUGO, show_lanerev_enb,
		   store_lanerev_enb);

static struct attribute *dev_ibcfg_attributes[] = {
	&dev_attr_hrtbt_enable.attr,
	&dev_attr_link_width_enable.attr,
	&dev_attr_link_width.attr,
	&dev_attr_link_speed_enable.attr,
	&dev_attr_link_speed.attr,
	&dev_attr_rx_pol_inv_enable.attr,
	&dev_attr_rx_lane_rev_enable.attr,
	NULL
};

static struct attribute_group dev_ibcfg_attr_group = {
	.attrs = dev_ibcfg_attributes
};

/**
 * ipath_expose_reset - create a device reset file
 * @dev: the device structure
 *
 * Only expose a file that lets us reset the device after someone
 * enters diag mode.  A device reset is quite likely to crash the
 * machine entirely, so we don't want to normally make it
 * available.
 *
 * Called with ipath_mutex held.
 */
int ipath_expose_reset(struct device *dev)
{
	static int exposed;
	int ret;

	if (!exposed) {
		ret = device_create_file(dev, &dev_attr_reset);
		exposed = 1;
	}
	else
		ret = 0;

	return ret;
}

int ipath_device_create_group(struct device *dev, struct ipath_devdata *dd)
{
	int ret;

	ret = sysfs_create_group(&dev->kobj, &dev_attr_group);
	if (ret)
		goto bail;

	ret = sysfs_create_group(&dev->kobj, &dev_counter_attr_group);
	if (ret)
		goto bail_attrs;

	if (dd->ipath_flags & IPATH_HAS_MULT_IB_SPEED) {
		ret = device_create_file(dev, &dev_attr_jint_idle_ticks);
		if (ret)
			goto bail_counter;
		ret = device_create_file(dev, &dev_attr_jint_max_packets);
		if (ret)
			goto bail_idle;

		ret = sysfs_create_group(&dev->kobj, &dev_ibcfg_attr_group);
		if (ret)
			goto bail_max;
	}

	return 0;

bail_max:
	device_remove_file(dev, &dev_attr_jint_max_packets);
bail_idle:
	device_remove_file(dev, &dev_attr_jint_idle_ticks);
bail_counter:
	sysfs_remove_group(&dev->kobj, &dev_counter_attr_group);
bail_attrs:
	sysfs_remove_group(&dev->kobj, &dev_attr_group);
bail:
	return ret;
}

void ipath_device_remove_group(struct device *dev, struct ipath_devdata *dd)
{
	sysfs_remove_group(&dev->kobj, &dev_counter_attr_group);

	if (dd->ipath_flags & IPATH_HAS_MULT_IB_SPEED) {
		sysfs_remove_group(&dev->kobj, &dev_ibcfg_attr_group);
		device_remove_file(dev, &dev_attr_jint_idle_ticks);
		device_remove_file(dev, &dev_attr_jint_max_packets);
	}

	sysfs_remove_group(&dev->kobj, &dev_attr_group);

	device_remove_file(dev, &dev_attr_reset);
}
