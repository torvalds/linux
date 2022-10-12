/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */
#include <linux/kobject.h>
#include <linux/proc_fs.h>
#include <linux/sysfs.h>
#include <osl.h>
#include <dhd_dbg.h>
#include <dhd_linux_priv.h>
#ifdef DHD_ADPS_BAM_EXPORT
#include <wl_bam.h>
#endif // endif

#ifndef BCMDHDX
#define SYSFS_DIR_BCMDHD "bcmdhd"
#define SYSFS_DIR_WIFI "wifi0"
#define PROCFS_DIR_TRACE "dhd_trace"
#define PROCFS_DIR_ENCOUNTERS "dhd_ecounters"
#define PROCFS_DIR_RTT "dhd_rtt"
#else
#define SYSFS_DIR_BCMDHD "bcmdhdx"
#define SYSFS_DIR_WIFI "wifi1"
#define PROCFS_DIR_WIFI "dhdx_trace"
#define PROCFS_DIR_ENCOUNTERS "dhdx_ecounters"
#define PROCFS_DIR_RTT "dhdx_rtt"
#endif /* BCMDHDX */

#ifdef SHOW_LOGTRACE
extern dhd_pub_t* g_dhd_pub;
static int dhd_ring_proc_open(struct inode *inode, struct file *file);
ssize_t dhd_ring_proc_read(struct file *file, char *buffer, size_t tt, loff_t *loff);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops dhd_ring_proc_fops = {
	.proc_open = dhd_ring_proc_open,
	.proc_read = dhd_ring_proc_read,
	.proc_release = single_release,
};
#else
static const struct file_operations dhd_ring_proc_fops = {
	.open = dhd_ring_proc_open,
	.read = dhd_ring_proc_read,
	.release = single_release,
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0) */

static int
dhd_ring_proc_open(struct inode *inode, struct file *file)
{
	int ret = BCME_ERROR;
	if (inode) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
		ret = single_open(file, 0, PDE_DATA(inode));
#else
		/* This feature is not supported for lower kernel versions */
		ret = single_open(file, 0, NULL);
#endif // endif
	} else {
		DHD_ERROR(("%s: inode is NULL\n", __FUNCTION__));
	}
	return ret;
}

ssize_t
dhd_ring_proc_read(struct file *file, char __user *buffer, size_t tt, loff_t *loff)
{
	trace_buf_info_t *trace_buf_info;
	int ret = BCME_ERROR;
	dhd_dbg_ring_t *ring = (dhd_dbg_ring_t *)((struct seq_file *)(file->private_data))->private;

	if (ring == NULL) {
		DHD_ERROR(("%s: ring is NULL\n", __FUNCTION__));
		return ret;
	}

	ASSERT(g_dhd_pub);

	trace_buf_info = (trace_buf_info_t *)MALLOCZ(g_dhd_pub->osh, sizeof(trace_buf_info_t));
	if (trace_buf_info) {
		dhd_dbg_read_ring_into_trace_buf(ring, trace_buf_info);
		if (copy_to_user(buffer, (void*)trace_buf_info->buf, MIN(trace_buf_info->size, tt)))
		{
			ret = -EFAULT;
			goto exit;
		}
		if (trace_buf_info->availability == BUF_NOT_AVAILABLE)
			ret = BUF_NOT_AVAILABLE;
		else
			ret = trace_buf_info->size;
	} else
		DHD_ERROR(("Memory allocation Failed\n"));

exit:
	if (trace_buf_info) {
		MFREE(g_dhd_pub->osh, trace_buf_info, sizeof(trace_buf_info_t));
	}
	return ret;
}

void
dhd_dbg_ring_proc_create(dhd_pub_t *dhdp)
{
#ifdef DEBUGABILITY
	dhd_dbg_ring_t *dbg_verbose_ring = NULL;

	dbg_verbose_ring = dhd_dbg_get_ring_from_ring_id(dhdp, FW_VERBOSE_RING_ID);
	if (dbg_verbose_ring) {
		if (!proc_create_data(PROCFS_DIR_TRACE, S_IRUSR, NULL, &dhd_ring_proc_fops,
			dbg_verbose_ring)) {
			DHD_ERROR(("Failed to create /proc/dhd_trace procfs interface\n"));
		} else {
			DHD_ERROR(("Created /proc/dhd_trace procfs interface\n"));
		}
	} else {
		DHD_ERROR(("dbg_verbose_ring is NULL, /proc/dhd_trace not created\n"));
	}
#endif /* DEBUGABILITY */

#ifdef EWP_ECNTRS_LOGGING
	if (!proc_create_data(PROCFS_DIR_ENCOUNTERS, S_IRUSR, NULL, &dhd_ring_proc_fops,
		dhdp->ecntr_dbg_ring)) {
		DHD_ERROR(("Failed to create /proc/dhd_ecounters procfs interface\n"));
	} else {
		DHD_ERROR(("Created /proc/dhd_ecounters procfs interface\n"));
	}
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
	if (!proc_create_data(PROCFS_DIR_RTT, S_IRUSR, NULL, &dhd_ring_proc_fops,
		dhdp->rtt_dbg_ring)) {
		DHD_ERROR(("Failed to create /proc/dhd_rtt procfs interface\n"));
	} else {
		DHD_ERROR(("Created /proc/dhd_rtt procfs interface\n"));
	}
#endif /* EWP_RTT_LOGGING */
}

void
dhd_dbg_ring_proc_destroy(dhd_pub_t *dhdp)
{
#ifdef DEBUGABILITY
	remove_proc_entry(PROCFS_DIR_TRACE, NULL);
#endif /* DEBUGABILITY */

#ifdef EWP_ECNTRS_LOGGING
	remove_proc_entry(PROCFS_DIR_ENCOUNTERS, NULL);
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
	remove_proc_entry(PROCFS_DIR_RTT, NULL);
#endif /* EWP_RTT_LOGGING */

}
#endif /* SHOW_LOGTRACE */

/* ----------------------------------------------------------------------------
 * Infrastructure code for sysfs interface support for DHD
 *
 * What is sysfs interface?
 * https://www.kernel.org/doc/Documentation/filesystems/sysfs.txt
 *
 * Why sysfs interface?
 * This is the Linux standard way of changing/configuring Run Time parameters
 * for a driver. We can use this interface to control "linux" specific driver
 * parameters.
 *
 * -----------------------------------------------------------------------------
 */

#if defined(DHD_TRACE_WAKE_LOCK)
extern atomic_t trace_wklock_onoff;

/* Function to show the history buffer */
static ssize_t
show_wklock_trace(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	dhd_info_t *dhd = (dhd_info_t *)dev;

	buf[ret] = '\n';
	buf[ret+1] = 0;

	dhd_wk_lock_stats_dump(&dhd->pub);
	return ret+1;
}

/* Function to enable/disable wakelock trace */
static ssize_t
wklock_trace_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;
	BCM_REFERENCE(dhd);

	onoff = bcm_strtoul(buf, NULL, 10);
	if (onoff != 0 && onoff != 1) {
		return -EINVAL;
	}

	atomic_set(&trace_wklock_onoff, onoff);
	if (atomic_read(&trace_wklock_onoff)) {
		printk("ENABLE WAKLOCK TRACE\n");
	} else {
		printk("DISABLE WAKELOCK TRACE\n");
	}

	return (ssize_t)(onoff+1);
}
#endif /* DHD_TRACE_WAKE_LOCK */

#if defined(DHD_LB_TXP)
static ssize_t
show_lbtxp(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;

	onoff = atomic_read(&dhd->lb_txp_active);
	ret = scnprintf(buf, PAGE_SIZE - 1, "%lu \n",
		onoff);
	return ret;
}

static ssize_t
lbtxp_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;
	int i;

	onoff = bcm_strtoul(buf, NULL, 10);

	sscanf(buf, "%lu", &onoff);
	if (onoff != 0 && onoff != 1) {
		return -EINVAL;
	}
	atomic_set(&dhd->lb_txp_active, onoff);

	/* Since the scheme is changed clear the counters */
	for (i = 0; i < NR_CPUS; i++) {
		DHD_LB_STATS_CLR(dhd->txp_percpu_run_cnt[i]);
		DHD_LB_STATS_CLR(dhd->tx_start_percpu_run_cnt[i]);
	}

	return count;
}

#endif /* DHD_LB_TXP */

#if defined(DHD_LB_RXP)
static ssize_t
show_lbrxp(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;

	onoff = atomic_read(&dhd->lb_rxp_active);
	ret = scnprintf(buf, PAGE_SIZE - 1, "%lu \n",
		onoff);
	return ret;
}

static ssize_t
lbrxp_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;
	int i, j;

	onoff = bcm_strtoul(buf, NULL, 10);

	sscanf(buf, "%lu", &onoff);
	if (onoff != 0 && onoff != 1) {
		return -EINVAL;
	}
	atomic_set(&dhd->lb_rxp_active, onoff);

	/* Since the scheme is changed clear the counters */
	for (i = 0; i < NR_CPUS; i++) {
		DHD_LB_STATS_CLR(dhd->napi_percpu_run_cnt[i]);
		for (j = 0; j < HIST_BIN_SIZE; j++) {
			DHD_LB_STATS_CLR(dhd->napi_rx_hist[j][i]);
		}
	}

	return count;
}
#endif /* DHD_LB_RXP */

#ifdef DHD_LOG_DUMP
extern int logdump_periodic_flush;
extern int logdump_ecntr_enable;
static ssize_t
show_logdump_periodic_flush(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	unsigned long val;

	val = logdump_periodic_flush;
	ret = scnprintf(buf, PAGE_SIZE - 1, "%lu \n", val);
	return ret;
}

static ssize_t
logdump_periodic_flush_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long val;

	val = bcm_strtoul(buf, NULL, 10);

	sscanf(buf, "%lu", &val);
	if (val != 0 && val != 1) {
		 return -EINVAL;
	}
	logdump_periodic_flush = val;
	return count;
}

static ssize_t
show_logdump_ecntr(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	unsigned long val;

	val = logdump_ecntr_enable;
	ret = scnprintf(buf, PAGE_SIZE - 1, "%lu \n", val);
	return ret;
}

static ssize_t
logdump_ecntr_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long val;

	val = bcm_strtoul(buf, NULL, 10);

	sscanf(buf, "%lu", &val);
	if (val != 0 && val != 1) {
		 return -EINVAL;
	}
	logdump_ecntr_enable = val;
	return count;
}

#endif /* DHD_LOG_DUMP */

extern uint enable_ecounter;
static ssize_t
show_enable_ecounter(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	unsigned long onoff;

	onoff = enable_ecounter;
	ret = scnprintf(buf, PAGE_SIZE - 1, "%lu \n",
		onoff);
	return ret;
}

static ssize_t
ecounter_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;
	dhd_pub_t *dhdp;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return count;
	}
	dhdp = &dhd->pub;
	if (!FW_SUPPORTED(dhdp, ecounters)) {
		DHD_ERROR(("%s: ecounters not supported by FW\n", __FUNCTION__));
		return count;
	}

	onoff = bcm_strtoul(buf, NULL, 10);

	sscanf(buf, "%lu", &onoff);
	if (onoff != 0 && onoff != 1) {
		return -EINVAL;
	}

	if (enable_ecounter == onoff) {
		DHD_ERROR(("%s: ecounters already %d\n", __FUNCTION__, enable_ecounter));
		return count;
	}

	enable_ecounter = onoff;
	dhd_ecounter_configure(dhdp, enable_ecounter);

	return count;
}

/*
 * Generic Attribute Structure for DHD.
 * If we have to add a new sysfs entry under /sys/bcm-dhd/, we have
 * to instantiate an object of type dhd_attr,  populate it with
 * the required show/store functions (ex:- dhd_attr_cpumask_primary)
 * and add the object to default_attrs[] array, that gets registered
 * to the kobject of dhd (named bcm-dhd).
 */

struct dhd_attr {
	struct attribute attr;
	ssize_t(*show)(struct dhd_info *, char *);
	ssize_t(*store)(struct dhd_info *, const char *, size_t count);
};

#if defined(DHD_TRACE_WAKE_LOCK)
static struct dhd_attr dhd_attr_wklock =
	__ATTR(wklock_trace, 0660, show_wklock_trace, wklock_trace_onoff);
#endif /* defined(DHD_TRACE_WAKE_LOCK */

#if defined(DHD_LB_TXP)
static struct dhd_attr dhd_attr_lbtxp =
	__ATTR(lbtxp, 0660, show_lbtxp, lbtxp_onoff);
#endif /* DHD_LB_TXP */

#if defined(DHD_LB_RXP)
static struct dhd_attr dhd_attr_lbrxp =
	__ATTR(lbrxp, 0660, show_lbrxp, lbrxp_onoff);
#endif /* DHD_LB_RXP */

#ifdef DHD_LOG_DUMP
static struct dhd_attr dhd_attr_logdump_periodic_flush =
     __ATTR(logdump_periodic_flush, 0660, show_logdump_periodic_flush,
		logdump_periodic_flush_onoff);
static struct dhd_attr dhd_attr_logdump_ecntr =
	__ATTR(logdump_ecntr_enable, 0660, show_logdump_ecntr,
		logdump_ecntr_onoff);
#endif /* DHD_LOG_DUMP */

static struct dhd_attr dhd_attr_ecounters =
	__ATTR(ecounters, 0660, show_enable_ecounter, ecounter_onoff);

/* Attribute object that gets registered with "bcm-dhd" kobject tree */
static struct attribute *default_attrs[] = {
#if defined(DHD_TRACE_WAKE_LOCK)
	&dhd_attr_wklock.attr,
#endif // endif
#if defined(DHD_LB_TXP)
	&dhd_attr_lbtxp.attr,
#endif /* DHD_LB_TXP */
#if defined(DHD_LB_RXP)
	&dhd_attr_lbrxp.attr,
#endif /* DHD_LB_RXP */
#ifdef DHD_LOG_DUMP
	&dhd_attr_logdump_periodic_flush.attr,
	&dhd_attr_logdump_ecntr.attr,
#endif // endif
	&dhd_attr_ecounters.attr,
	NULL
};

#define to_dhd(k) container_of(k, struct dhd_info, dhd_kobj)
#define to_attr(a) container_of(a, struct dhd_attr, attr)

/*
 * bcm-dhd kobject show function, the "attr" attribute specifices to which
 * node under "bcm-dhd" the show function is called.
 */
static ssize_t dhd_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	dhd_info_t *dhd = to_dhd(kobj);
	struct dhd_attr *d_attr = to_attr(attr);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
	int ret;

	if (d_attr->show)
		ret = d_attr->show(dhd, buf);
	else
		ret = -EIO;

	return ret;
}

/*
 * bcm-dhd kobject show function, the "attr" attribute specifices to which
 * node under "bcm-dhd" the store function is called.
 */
static ssize_t dhd_store(struct kobject *kobj, struct attribute *attr,
	const char *buf, size_t count)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	dhd_info_t *dhd = to_dhd(kobj);
	struct dhd_attr *d_attr = to_attr(attr);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
	int ret;

	if (d_attr->store)
		ret = d_attr->store(dhd, buf, count);
	else
		ret = -EIO;

	return ret;

}

static struct sysfs_ops dhd_sysfs_ops = {
	.show = dhd_show,
	.store = dhd_store,
};

static struct kobj_type dhd_ktype = {
	.sysfs_ops = &dhd_sysfs_ops,
	.default_attrs = default_attrs,
};

#ifdef DHD_MAC_ADDR_EXPORT
struct ether_addr sysfs_mac_addr;
static ssize_t
show_mac_addr(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE - 1, MACF,
		(uint32)sysfs_mac_addr.octet[0], (uint32)sysfs_mac_addr.octet[1],
		(uint32)sysfs_mac_addr.octet[2], (uint32)sysfs_mac_addr.octet[3],
		(uint32)sysfs_mac_addr.octet[4], (uint32)sysfs_mac_addr.octet[5]);

	return ret;
}

static ssize_t
set_mac_addr(struct dhd_info *dev, const char *buf, size_t count)
{
	if (!bcm_ether_atoe(buf, &sysfs_mac_addr)) {
		DHD_ERROR(("Invalid Mac Address \n"));
		return -EINVAL;
	}

	DHD_ERROR(("Mac Address set with "MACDBG"\n", MAC2STRDBG(&sysfs_mac_addr)));

	return count;
}

static struct dhd_attr dhd_attr_cntl_macaddr =
	__ATTR(mac_addr, 0660, show_mac_addr, set_mac_addr);
#endif /* DHD_MAC_ADDR_EXPORT */

#ifdef DHD_FW_COREDUMP

#ifdef CUSTOMER_HW4_DEBUG
#define MEMDUMPINFO PLATFORM_PATH".memdump.info"
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
#define MEMDUMPINFO "/data/misc/wifi/.memdump.info"
#elif defined(OEM_ANDROID) && (defined(BOARD_PANDA) || defined(__ARM_ARCH_7A__))
#define MEMDUMPINFO "/data/misc/wifi/.memdump.info"
#elif defined(OEM_ANDROID) && defined(DHD_FW_COREDUMP)
#define MEMDUMPINFO_LIVE "/installmedia/.memdump.info"
#define MEMDUMPINFO_INST "/data/.memdump.info"
#define MEMDUMPINFO MEMDUMPINFO_LIVE
#else /* FC19 and Others */
#define MEMDUMPINFO "/root/.memdump.info"
#endif /* CUSTOMER_HW4_DEBUG */

uint32
get_mem_val_from_file(void)
{
	struct file *fp = NULL;
	uint32 mem_val = DUMP_MEMFILE_MAX;
	char *p_mem_val = NULL;
	char *filepath = MEMDUMPINFO;
	int ret = 0;

	/* Read memdump info from the file */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
#if defined(CONFIG_X86) && defined(OEM_ANDROID) && defined(DHD_FW_COREDUMP)
		/* Check if it is Live Brix Image */
		if (strcmp(filepath, MEMDUMPINFO_LIVE) != 0) {
			goto done;
		}
		/* Try if it is Installed Brix Image */
		filepath = MEMDUMPINFO_INST;
		DHD_ERROR(("%s: Try File [%s]\n", __FUNCTION__, filepath));
		fp = filp_open(filepath, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
			goto done;
		}
#else /* Non Brix Android platform */
		goto done;
#endif /* CONFIG_X86 && OEM_ANDROID */
	}

	/* Handle success case */
	ret = compat_kernel_read(fp, 0, (char *)&mem_val, sizeof(uint32));
	if (ret < 0) {
		DHD_ERROR(("%s: File read error, ret=%d\n", __FUNCTION__, ret));
		filp_close(fp, NULL);
		goto done;
	}

	p_mem_val = (char*)&mem_val;
	p_mem_val[sizeof(uint32) - 1] = '\0';
	mem_val = bcm_atoi(p_mem_val);

	filp_close(fp, NULL);

done:
	return mem_val;
}

void dhd_get_memdump_info(dhd_pub_t *dhd)
{
#ifndef DHD_EXPORT_CNTL_FILE
	uint32 mem_val = DUMP_MEMFILE_MAX;

	mem_val = get_mem_val_from_file();
	if (mem_val != DUMP_MEMFILE_MAX)
		dhd->memdump_enabled = mem_val;
#ifdef DHD_INIT_DEFAULT_MEMDUMP
	if (mem_val == 0 || mem_val == DUMP_MEMFILE_MAX)
		mem_val = DUMP_MEMFILE_BUGON;
#endif /* DHD_INIT_DEFAULT_MEMDUMP */
#else
#ifdef DHD_INIT_DEFAULT_MEMDUMP
	if (dhd->memdump_enabled == 0 || dhd->memdump_enabled == DUMP_MEMFILE_MAX)
		dhd->memdump_enabled = DUMP_MEMFILE_BUGON;
#endif /* DHD_INIT_DEFAULT_MEMDUMP */
#endif /* !DHD_EXPORT_CNTL_FILE */
#ifdef BCMQT
	/* In QT environment collecting memdump on FW TRAP, IOVAR timeouts,
	 * is taking more time and makes system unresponsive so disabling it.
	 * if needed memdump can be collected through 'dhd upload' command.
	*/
	dhd->memdump_enabled = DUMP_DISABLED;
#endif // endif
	DHD_ERROR(("%s: MEMDUMP ENABLED = %d\n", __FUNCTION__, dhd->memdump_enabled));
}

#ifdef DHD_EXPORT_CNTL_FILE
static ssize_t
show_memdump_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	dhd_pub_t *dhdp;

	if (!dev) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return ret;
	}

	dhdp = &dev->pub;
	ret = scnprintf(buf, PAGE_SIZE -1, "%u\n", dhdp->memdump_enabled);
	return ret;
}

static ssize_t
set_memdump_info(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long memval;
	dhd_pub_t *dhdp;

	if (!dev) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return count;
	}
	dhdp = &dev->pub;

	memval = bcm_strtoul(buf, NULL, 10);
	sscanf(buf, "%lu", &memval);

	dhdp->memdump_enabled = (uint32)memval;

	DHD_ERROR(("%s: MEMDUMP ENABLED = %iu\n", __FUNCTION__, dhdp->memdump_enabled));
	return count;
}

static struct dhd_attr dhd_attr_cntl_memdump =
	__ATTR(memdump, 0660, show_memdump_info, set_memdump_info);
#endif /* DHD_EXPORT_CNTL_FILE */
#endif /* DHD_FW_COREDUMP */

#ifdef BCMASSERT_LOG
#ifdef CUSTOMER_HW4_DEBUG
#define ASSERTINFO PLATFORM_PATH".assert.info"
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
#define ASSERTINFO "/data/misc/wifi/.assert.info"
#elif defined(OEM_ANDROID)
#define ASSERTINFO "/installmedia/.assert.info"
#else
#define ASSERTINFO "/root/.assert.info"
#endif /* CUSTOMER_HW4_DEBUG */
int
get_assert_val_from_file(void)
{
	struct file *fp = NULL;
	char *filepath = ASSERTINFO;
	char *p_mem_val = NULL;
	int mem_val = -1;

	/*
	 * Read assert info from the file
	 * 0: Trigger Kernel crash by panic()
	 * 1: Print out the logs and don't trigger Kernel panic. (default)
	 * 2: Trigger Kernel crash by BUG()
	 * File doesn't exist: Keep default value (1).
	 */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
	} else {
		int ret = compat_kernel_read(fp, 0, (char *)&mem_val, sizeof(uint32));
		if (ret < 0) {
			DHD_ERROR(("%s: File read error, ret=%d\n", __FUNCTION__, ret));
		} else {
			p_mem_val = (char *)&mem_val;
			p_mem_val[sizeof(uint32) - 1] = '\0';
			mem_val = bcm_atoi(p_mem_val);
			DHD_ERROR(("%s: ASSERT ENABLED = %d\n", __FUNCTION__, mem_val));
		}
		filp_close(fp, NULL);
	}

	return mem_val;
}

void dhd_get_assert_info(dhd_pub_t *dhd)
{
#ifndef DHD_EXPORT_CNTL_FILE
	int mem_val = -1;

	mem_val = get_assert_val_from_file();
	if (mem_val != -1)
		g_assert_type = mem_val;
#endif /* !DHD_EXPORT_CNTL_FILE */
}

#ifdef DHD_EXPORT_CNTL_FILE
static ssize_t
show_assert_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	if (!dev) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return ret;
	}

	ret = scnprintf(buf, PAGE_SIZE -1, "%d\n", g_assert_type);
	return ret;

}

static ssize_t
set_assert_info(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long assert_val;

	assert_val = bcm_strtoul(buf, NULL, 10);
	sscanf(buf, "%lu", &assert_val);

	g_assert_type = (uint32)assert_val;

	DHD_ERROR(("%s: ASSERT ENABLED = %lu\n", __FUNCTION__, assert_val));
	return count;

}

static struct dhd_attr dhd_attr_cntl_assert =
	__ATTR(assert, 0660, show_assert_info, set_assert_info);
#endif /* DHD_EXPORT_CNTL_FILE */
#endif /* BCMASSERT_LOG */

#ifdef DHD_EXPORT_CNTL_FILE
#if defined(WRITE_WLANINFO)
static ssize_t
show_wifiver_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE -1, "%s", version_info);
	return ret;
}

static ssize_t
set_wifiver_info(struct dhd_info *dev, const char *buf, size_t count)
{
	DHD_ERROR(("Do not set version info\n"));
	return -EINVAL;
}

static struct dhd_attr dhd_attr_cntl_wifiver =
	__ATTR(wifiver, 0660, show_wifiver_info, set_wifiver_info);
#endif /* WRITE_WLANINFO */

#if defined(USE_CID_CHECK)
char cidinfostr[MAX_VNAME_LEN];

static ssize_t
show_cid_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE -1, "%s", cidinfostr);
	return ret;
}

static ssize_t
set_cid_info(struct dhd_info *dev, const char *buf, size_t count)
{
	int len = strlen(buf) + 1;
	int maxstrsz;
	maxstrsz = MAX_VNAME_LEN;

	scnprintf(cidinfostr, ((len > maxstrsz) ? maxstrsz : len), "%s", buf);
	DHD_INFO(("%s : CID info string\n", cidinfostr));
	return count;
}

static struct dhd_attr dhd_attr_cntl_cidinfo =
	__ATTR(cid, 0660, show_cid_info, set_cid_info);
#endif /* USE_CID_CHECK */

#if defined(GEN_SOFTAP_INFO_FILE)
char softapinfostr[SOFTAP_INFO_BUF_SZ];
static ssize_t
show_softap_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE -1, "%s", softapinfostr);
	return ret;
}

static ssize_t
set_softap_info(struct dhd_info *dev, const char *buf, size_t count)
{
	DHD_ERROR(("Do not set sofap related info\n"));
	return -EINVAL;
}

static struct dhd_attr dhd_attr_cntl_softapinfo =
	__ATTR(softap, 0660, show_softap_info, set_softap_info);
#endif /* GEN_SOFTAP_INFO_FILE */

#if defined(MIMO_ANT_SETTING)
unsigned long antsel;

static ssize_t
show_ant_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE -1, "%lu\n", antsel);
	return ret;
}

static ssize_t
set_ant_info(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long ant_val;

	ant_val = bcm_strtoul(buf, NULL, 10);
	sscanf(buf, "%lu", &ant_val);

	/*
	 * Check value
	 * 0 - Not set, handle same as file not exist
	 */
	if (ant_val > 3) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %lu \n",
			__FUNCTION__, ant_val));
		return -EINVAL;
	}

	antsel = ant_val;
	DHD_ERROR(("[WIFI_SEC] %s: Set Antinfo val = %lu \n", __FUNCTION__, antsel));
	return count;
}

static struct dhd_attr dhd_attr_cntl_antinfo =
	__ATTR(ant, 0660, show_ant_info, set_ant_info);
#endif /* MIMO_ANT_SETTING */

#ifdef DHD_PM_CONTROL_FROM_FILE
extern bool g_pm_control;
extern uint32 pmmode_val;
static ssize_t
show_pm_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	if (!g_pm_control) {
		ret = scnprintf(buf, PAGE_SIZE -1, "PM mode is not set\n");
	} else {
		ret = scnprintf(buf, PAGE_SIZE -1, "%u\n", pmmode_val);
	}
	return ret;
}

static ssize_t
set_pm_info(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long pm_val;

	pm_val = bcm_strtoul(buf, NULL, 10);
	sscanf(buf, "%lu", &pm_val);

	if (pm_val > 2) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %lu \n",
			__FUNCTION__, pm_val));
		return -EINVAL;
	}

	if (!pm_val) {
		g_pm_control = TRUE;
	} else {
		g_pm_control = FALSE;
	}

	pmmode_val = (uint32)pm_val;
	DHD_ERROR(("[WIFI_SEC] %s: Set pminfo val = %u\n", __FUNCTION__, pmmode_val));
	return count;
}

static struct dhd_attr dhd_attr_cntl_pminfo =
	__ATTR(pm, 0660, show_pm_info, set_pm_info);
#endif /* DHD_PM_CONTROL_FROM_FILE */

#ifdef LOGTRACE_FROM_FILE
unsigned long logtrace_val = 1;

static ssize_t
show_logtrace_info(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE -1, "%lu\n", logtrace_val);
	return ret;
}

static ssize_t
set_logtrace_info(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long onoff;

	onoff = bcm_strtoul(buf, NULL, 10);
	sscanf(buf, "%lu", &onoff);

	if (onoff > 2) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %lu \n",
			__FUNCTION__, onoff));
		return -EINVAL;
	}

	logtrace_val = onoff;
	DHD_ERROR(("[WIFI_SEC] %s: LOGTRACE On/Off from sysfs = %lu\n",
		__FUNCTION__, logtrace_val));
	return count;
}

static struct dhd_attr dhd_attr_cntl_logtraceinfo =
	__ATTR(logtrace, 0660, show_logtrace_info, set_logtrace_info);
#endif /* LOGTRACE_FROM_FILE */

#ifdef  USE_WFA_CERT_CONF
#ifdef BCMSDIO
uint32 bus_txglom = VALUENOTSET;

static ssize_t
show_bustxglom(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	if (bus_txglom == VALUENOTSET) {
		ret = scnprintf(buf, PAGE_SIZE - 1, "%s\n", "bustxglom not set from sysfs");
	} else {
		ret = scnprintf(buf, PAGE_SIZE -1, "%u\n", bus_txglom);
	}
	return ret;
}

static ssize_t
set_bustxglom(struct dhd_info *dev, const char *buf, size_t count)
{
	uint32 onoff;

	onoff = (uint32)bcm_atoi(buf);
	sscanf(buf, "%u", &onoff);

	if (onoff > 2) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %u \n",
			__FUNCTION__, onoff));
		return -EINVAL;
	}

	bus_txglom = onoff;
	DHD_ERROR(("[WIFI_SEC] %s: BUS TXGLOM On/Off from sysfs = %u\n",
			__FUNCTION__, bus_txglom));
	return count;
}

static struct dhd_attr dhd_attr_cntl_bustxglom =
	__ATTR(bustxglom, 0660, show_bustxglom, set_bustxglom);
#endif /* BCMSDIO */

#if defined(ROAM_ENABLE) || defined(DISABLE_BUILTIN_ROAM)
uint32 roam_off = VALUENOTSET;

static ssize_t
show_roamoff(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	if (roam_off == VALUENOTSET) {
		ret = scnprintf(buf, PAGE_SIZE -1, "%s\n", "roam_off not set from sysfs");
	} else {
		ret = scnprintf(buf, PAGE_SIZE -1, "%u\n", roam_off);
	}
	return ret;
}

static ssize_t
set_roamoff(struct dhd_info *dev, const char *buf, size_t count)
{
	uint32 onoff;

	onoff = bcm_atoi(buf);
	sscanf(buf, "%u", &onoff);

	if (onoff > 2) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %u \n",
			__FUNCTION__, onoff));
		return -EINVAL;
	}

	roam_off = onoff;
	DHD_ERROR(("[WIFI_SEC] %s: ROAM On/Off from sysfs = %u\n",
		__FUNCTION__, roam_off));
	return count;
}

static struct dhd_attr dhd_attr_cntl_roamoff =
	__ATTR(roamoff, 0660, show_roamoff, set_roamoff);
#endif /* ROAM_ENABLE || DISABLE_BUILTIN_ROAM */

#ifdef USE_WL_FRAMEBURST
uint32 frameburst = VALUENOTSET;

static ssize_t
show_frameburst(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	if (frameburst == VALUENOTSET) {
		ret = scnprintf(buf, PAGE_SIZE -1, "%s\n", "frameburst not set from sysfs");
	} else {
		ret = scnprintf(buf, PAGE_SIZE -1, "%u\n", frameburst);
	}
	return ret;
}

static ssize_t
set_frameburst(struct dhd_info *dev, const char *buf, size_t count)
{
	uint32 onoff;

	onoff = bcm_atoi(buf);
	sscanf(buf, "%u", &onoff);

	if (onoff > 2) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %u \n",
			__FUNCTION__, onoff));
		return -EINVAL;
	}

	frameburst = onoff;
	DHD_ERROR(("[WIFI_SEC] %s: FRAMEBURST On/Off from sysfs = %u\n",
		__FUNCTION__, frameburst));
	return count;
}

static struct dhd_attr dhd_attr_cntl_frameburst =
	__ATTR(frameburst, 0660, show_frameburst, set_frameburst);
#endif /* USE_WL_FRAMEBURST */

#ifdef USE_WL_TXBF
uint32 txbf = VALUENOTSET;

static ssize_t
show_txbf(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	if (txbf == VALUENOTSET) {
		ret = scnprintf(buf, PAGE_SIZE -1, "%s\n", "txbf not set from sysfs");
	} else {
		ret = scnprintf(buf, PAGE_SIZE -1, "%u\n", txbf);
	}
	return ret;
}

static ssize_t
set_txbf(struct dhd_info *dev, const char *buf, size_t count)
{
	uint32 onoff;

	onoff = bcm_atoi(buf);
	sscanf(buf, "%u", &onoff);

	if (onoff > 2) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %u \n",
			__FUNCTION__, onoff));
		return -EINVAL;
	}

	txbf = onoff;
	DHD_ERROR(("[WIFI_SEC] %s: FRAMEBURST On/Off from sysfs = %u\n",
		__FUNCTION__, txbf));
	return count;
}

static struct dhd_attr dhd_attr_cntl_txbf =
	__ATTR(txbf, 0660, show_txbf, set_txbf);
#endif /* USE_WL_TXBF */

#ifdef PROP_TXSTATUS
uint32 proptx = VALUENOTSET;

static ssize_t
show_proptx(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	if (proptx == VALUENOTSET) {
		ret = scnprintf(buf, PAGE_SIZE -1, "%s\n", "proptx not set from sysfs");
	} else {
		ret = scnprintf(buf, PAGE_SIZE -1, "%u\n", proptx);
	}
	return ret;
}

static ssize_t
set_proptx(struct dhd_info *dev, const char *buf, size_t count)
{
	uint32 onoff;

	onoff = bcm_strtoul(buf, NULL, 10);
	sscanf(buf, "%u", &onoff);

	if (onoff > 2) {
		DHD_ERROR(("[WIFI_SEC] %s: Set Invalid value %u \n",
			__FUNCTION__, onoff));
		return -EINVAL;
	}

	proptx = onoff;
	DHD_ERROR(("[WIFI_SEC] %s: FRAMEBURST On/Off from sysfs = %u\n",
		__FUNCTION__, txbf));
	return count;
}

static struct dhd_attr dhd_attr_cntl_proptx =
	__ATTR(proptx, 0660, show_proptx, set_proptx);

#endif /* PROP_TXSTATUS */
#endif /* USE_WFA_CERT_CONF */
#endif /* DHD_EXPORT_CNTL_FILE */

#if defined(DHD_ADPS_BAM_EXPORT) && defined(WL_BAM)
#define BAD_AP_MAC_ADDR_ELEMENT_NUM	6
wl_bad_ap_mngr_t *g_bad_ap_mngr = NULL;

static ssize_t
show_adps_bam_list(struct dhd_info *dev, char *buf)
{
	int offset = 0;
	ssize_t ret = 0;

	wl_bad_ap_info_t *bad_ap;
	wl_bad_ap_info_entry_t *entry;

	if (g_bad_ap_mngr == NULL)
		return ret;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	list_for_each_entry(entry, &g_bad_ap_mngr->list, list) {
		bad_ap = &entry->bad_ap;

		ret = scnprintf(buf + offset, PAGE_SIZE - 1, MACF"\n",
			bad_ap->bssid.octet[0], bad_ap->bssid.octet[1],
			bad_ap->bssid.octet[2], bad_ap->bssid.octet[3],
			bad_ap->bssid.octet[4], bad_ap->bssid.octet[5]);

		offset += ret;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

	return offset;
}

static ssize_t
store_adps_bam_list(struct dhd_info *dev, const char *buf, size_t count)
{
	int ret;
	size_t len;
	int offset;
	char tmp[128];
	wl_bad_ap_info_t bad_ap;

	if (g_bad_ap_mngr == NULL)
		return count;

	len = count;
	offset = 0;
	do {
		ret = sscanf(buf + offset, MACF"\n",
			(uint32 *)&bad_ap.bssid.octet[0], (uint32 *)&bad_ap.bssid.octet[1],
			(uint32 *)&bad_ap.bssid.octet[2], (uint32 *)&bad_ap.bssid.octet[3],
			(uint32 *)&bad_ap.bssid.octet[4], (uint32 *)&bad_ap.bssid.octet[5]);
		if (ret != BAD_AP_MAC_ADDR_ELEMENT_NUM) {
			DHD_ERROR(("%s - fail to parse bad ap data\n", __FUNCTION__));
			return -EINVAL;
		}

		ret = wl_bad_ap_mngr_add(g_bad_ap_mngr, &bad_ap);
		if (ret < 0)
			return ret;

		ret = snprintf(tmp, ARRAYSIZE(tmp), MACF"\n",
			bad_ap.bssid.octet[0], bad_ap.bssid.octet[1],
			bad_ap.bssid.octet[2], bad_ap.bssid.octet[3],
			bad_ap.bssid.octet[4], bad_ap.bssid.octet[5]);
		if (ret < 0) {
			DHD_ERROR(("%s - fail to get bad ap data length(%d)\n", __FUNCTION__, ret));
			return ret;
		}

		len -= ret;
		offset += ret;
	} while (len > 0);

	return count;
}

static struct dhd_attr dhd_attr_adps_bam =
	__ATTR(bad_ap_list, 0660, show_adps_bam_list, store_adps_bam_list);
#endif	/* DHD_ADPS_BAM_EXPORT && WL_BAM */

#ifdef DHD_SEND_HANG_PRIVCMD_ERRORS
uint32 report_hang_privcmd_err = 1;

static ssize_t
show_hang_privcmd_err(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE - 1, "%u\n", report_hang_privcmd_err);
	return ret;
}

static ssize_t
set_hang_privcmd_err(struct dhd_info *dev, const char *buf, size_t count)
{
	uint32 val;

	val = bcm_atoi(buf);
	sscanf(buf, "%u", &val);

	report_hang_privcmd_err = val ? 1 : 0;
	DHD_INFO(("%s: Set report HANG for private cmd error: %d\n",
		__FUNCTION__, report_hang_privcmd_err));
	return count;
}

static struct dhd_attr dhd_attr_hang_privcmd_err =
	__ATTR(hang_privcmd_err, 0660, show_hang_privcmd_err, set_hang_privcmd_err);
#endif /* DHD_SEND_HANG_PRIVCMD_ERRORS */

#if defined(DISABLE_HE_ENAB) || defined(CUSTOM_CONTROL_HE_ENAB)
uint8 control_he_enab = 1;
#endif /* DISABLE_HE_ENAB || CUSTOM_CONTROL_HE_ENAB */

#if defined(CUSTOM_CONTROL_HE_ENAB)
static ssize_t
show_control_he_enab(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;

	ret = scnprintf(buf, PAGE_SIZE - 1, "%d\n", control_he_enab);
	return ret;
}

static ssize_t
set_control_he_enab(struct dhd_info *dev, const char *buf, size_t count)
{
	uint32 val;

	val = bcm_atoi(buf);

	control_he_enab = val ? 1 : 0;
	DHD_ERROR(("%s: Set control he enab: %d\n", __FUNCTION__, control_he_enab));
	return count;
}

static struct dhd_attr dhd_attr_control_he_enab=
__ATTR(control_he_enab, 0660, show_control_he_enab, set_control_he_enab);
#endif /* CUSTOM_CONTROL_HE_ENAB */
/* Attribute object that gets registered with "wifi" kobject tree */
static struct attribute *control_file_attrs[] = {
#ifdef DHD_MAC_ADDR_EXPORT
	&dhd_attr_cntl_macaddr.attr,
#endif /* DHD_MAC_ADDR_EXPORT */
#ifdef DHD_EXPORT_CNTL_FILE
#ifdef DHD_FW_COREDUMP
	&dhd_attr_cntl_memdump.attr,
#endif /* DHD_FW_COREDUMP */
#ifdef BCMASSERT_LOG
	&dhd_attr_cntl_assert.attr,
#endif /* BCMASSERT_LOG */
#ifdef WRITE_WLANINFO
	&dhd_attr_cntl_wifiver.attr,
#endif /* WRITE_WLANINFO */
#ifdef USE_CID_CHECK
	&dhd_attr_cntl_cidinfo.attr,
#endif /* USE_CID_CHECK */
#ifdef GEN_SOFTAP_INFO_FILE
	&dhd_attr_cntl_softapinfo.attr,
#endif /* GEN_SOFTAP_INFO_FILE */
#ifdef MIMO_ANT_SETTING
	&dhd_attr_cntl_antinfo.attr,
#endif /* MIMO_ANT_SETTING */
#ifdef DHD_PM_CONTROL_FROM_FILE
	&dhd_attr_cntl_pminfo.attr,
#endif /* DHD_PM_CONTROL_FROM_FILE */
#ifdef LOGTRACE_FROM_FILE
	&dhd_attr_cntl_logtraceinfo.attr,
#endif /* LOGTRACE_FROM_FILE */
#ifdef USE_WFA_CERT_CONF
#ifdef BCMSDIO
	&dhd_attr_cntl_bustxglom.attr,
#endif /* BCMSDIO */
	&dhd_attr_cntl_roamoff.attr,
#ifdef USE_WL_FRAMEBURST
	&dhd_attr_cntl_frameburst.attr,
#endif /* USE_WL_FRAMEBURST */
#ifdef USE_WL_TXBF
	&dhd_attr_cntl_txbf.attr,
#endif /* USE_WL_TXBF */
#ifdef PROP_TXSTATUS
	&dhd_attr_cntl_proptx.attr,
#endif /* PROP_TXSTATUS */
#endif /* USE_WFA_CERT_CONF */
#endif /* DHD_EXPORT_CNTL_FILE */
#ifdef DHD_ADPS_BAM_EXPORT
	&dhd_attr_adps_bam.attr,
#endif	/* DHD_ADPS_BAM_EXPORT */
#ifdef DHD_SEND_HANG_PRIVCMD_ERRORS
	&dhd_attr_hang_privcmd_err.attr,
#endif /* DHD_SEND_HANG_PRIVCMD_ERRORS */
#if defined(CUSTOM_CONTROL_HE_ENAB)
	&dhd_attr_control_he_enab.attr,
#endif /* CUSTOM_CONTROL_HE_ENAB */
	NULL
};

#define to_cntl_dhd(k) container_of(k, struct dhd_info, dhd_conf_file_kobj)

/*
 * wifi kobject show function, the "attr" attribute specifices to which
 * node under "sys/wifi" the show function is called.
 */
static ssize_t dhd_cntl_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	dhd_info_t *dhd = to_cntl_dhd(kobj);
	struct dhd_attr *d_attr = to_attr(attr);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
	int ret;

	if (d_attr->show)
		ret = d_attr->show(dhd, buf);
	else
		ret = -EIO;

	return ret;
}

/*
 * wifi kobject show function, the "attr" attribute specifices to which
 * node under "sys/wifi" the store function is called.
 */
static ssize_t dhd_cntl_store(struct kobject *kobj, struct attribute *attr,
	const char *buf, size_t count)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	dhd_info_t *dhd = to_cntl_dhd(kobj);
	struct dhd_attr *d_attr = to_attr(attr);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
	int ret;

	if (d_attr->store)
		ret = d_attr->store(dhd, buf, count);
	else
		ret = -EIO;

	return ret;

}

static struct sysfs_ops dhd_sysfs_cntl_ops = {
	.show = dhd_cntl_show,
	.store = dhd_cntl_store,
};

static struct kobj_type dhd_cntl_file_ktype = {
	.sysfs_ops = &dhd_sysfs_cntl_ops,
	.default_attrs = control_file_attrs,
};

/* Create a kobject and attach to sysfs interface */
int dhd_sysfs_init(dhd_info_t *dhd)
{
	int ret = -1;

	if (dhd == NULL) {
		DHD_ERROR(("%s(): dhd is NULL \r\n", __FUNCTION__));
		return ret;
	}

	/* Initialize the kobject */
	ret = kobject_init_and_add(&dhd->dhd_kobj, &dhd_ktype, NULL, SYSFS_DIR_BCMDHD);
	if (ret) {
		kobject_put(&dhd->dhd_kobj);
		DHD_ERROR(("%s(): Unable to allocate kobject \r\n", __FUNCTION__));
		return ret;
	}
	ret = kobject_init_and_add(&dhd->dhd_conf_file_kobj,
			&dhd_cntl_file_ktype, NULL, SYSFS_DIR_WIFI);
	if (ret) {
		kobject_put(&dhd->dhd_conf_file_kobj);
		DHD_ERROR(("%s(): Unable to allocate kobject \r\n", __FUNCTION__));
		return ret;
	}

	/*
	 * We are always responsible for sending the uevent that the kobject
	 * was added to the system.
	 */
	kobject_uevent(&dhd->dhd_kobj, KOBJ_ADD);
	kobject_uevent(&dhd->dhd_conf_file_kobj, KOBJ_ADD);

	return ret;
}

/* Done with the kobject and detach the sysfs interface */
void dhd_sysfs_exit(dhd_info_t *dhd)
{
	if (dhd == NULL) {
		DHD_ERROR(("%s(): dhd is NULL \r\n", __FUNCTION__));
		return;
	}

	/* Releae the kobject */
	if (dhd->dhd_kobj.state_initialized)
		kobject_put(&dhd->dhd_kobj);
	if (dhd->dhd_conf_file_kobj.state_initialized)
		kobject_put(&dhd->dhd_conf_file_kobj);
}
