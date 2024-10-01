// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007
 */

#define KMSG_COMPONENT "sclp_config"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <asm/smp.h>

#include "sclp.h"

struct conf_mgm_data {
	u8 reserved;
	u8 ev_qualifier;
} __attribute__((packed));

#define OFB_DATA_MAX 64

struct sclp_ofb_evbuf {
	struct evbuf_header header;
	struct conf_mgm_data cm_data;
	char ev_data[OFB_DATA_MAX];
} __packed;

struct sclp_ofb_sccb {
	struct sccb_header header;
	struct sclp_ofb_evbuf ofb_evbuf;
} __packed;

#define EV_QUAL_CPU_CHANGE	1
#define EV_QUAL_CAP_CHANGE	3
#define EV_QUAL_OPEN4BUSINESS	5

static struct work_struct sclp_cpu_capability_work;
static struct work_struct sclp_cpu_change_work;

static void sclp_cpu_capability_notify(struct work_struct *work)
{
	int cpu;
	struct device *dev;

	s390_update_cpu_mhz();
	pr_info("CPU capability may have changed\n");
	cpus_read_lock();
	for_each_online_cpu(cpu) {
		dev = get_cpu_device(cpu);
		kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	}
	cpus_read_unlock();
}

static void __ref sclp_cpu_change_notify(struct work_struct *work)
{
	lock_device_hotplug();
	smp_rescan_cpus(false);
	unlock_device_hotplug();
}

static void sclp_conf_receiver_fn(struct evbuf_header *evbuf)
{
	struct conf_mgm_data *cdata;

	cdata = (struct conf_mgm_data *)(evbuf + 1);
	switch (cdata->ev_qualifier) {
	case EV_QUAL_CPU_CHANGE:
		schedule_work(&sclp_cpu_change_work);
		break;
	case EV_QUAL_CAP_CHANGE:
		schedule_work(&sclp_cpu_capability_work);
		break;
	}
}

static struct sclp_register sclp_conf_register =
{
#ifdef CONFIG_SCLP_OFB
	.send_mask    = EVTYP_CONFMGMDATA_MASK,
#endif
	.receive_mask = EVTYP_CONFMGMDATA_MASK,
	.receiver_fn  = sclp_conf_receiver_fn,
};

#ifdef CONFIG_SCLP_OFB
static int sclp_ofb_send_req(char *ev_data, size_t len)
{
	static DEFINE_MUTEX(send_mutex);
	struct sclp_ofb_sccb *sccb;
	int rc, response;

	if (len > OFB_DATA_MAX)
		return -EINVAL;
	sccb = (struct sclp_ofb_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	/* Setup SCCB for Control-Program Identification */
	sccb->header.length = sizeof(struct sclp_ofb_sccb);
	sccb->ofb_evbuf.header.length = sizeof(struct sclp_ofb_evbuf);
	sccb->ofb_evbuf.header.type = EVTYP_CONFMGMDATA;
	sccb->ofb_evbuf.cm_data.ev_qualifier = EV_QUAL_OPEN4BUSINESS;
	memcpy(sccb->ofb_evbuf.ev_data, ev_data, len);

	if (!(sclp_conf_register.sclp_receive_mask & EVTYP_CONFMGMDATA_MASK))
		pr_warn("SCLP receiver did not register to receive "
			"Configuration Management Data Events.\n");

	mutex_lock(&send_mutex);
	rc = sclp_sync_request(SCLP_CMDW_WRITE_EVENT_DATA, sccb);
	mutex_unlock(&send_mutex);
	if (rc)
		goto out;
	response = sccb->header.response_code;
	if (response != 0x0020) {
		pr_err("Open for Business request failed with response code "
		       "0x%04x\n", response);
		rc = -EIO;
	}
out:
	free_page((unsigned long)sccb);
	return rc;
}

static ssize_t sysfs_ofb_data_write(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *bin_attr,
				    char *buf, loff_t off, size_t count)
{
	int rc;

	rc = sclp_ofb_send_req(buf, count);
	return rc ?: count;
}

static const struct bin_attribute ofb_bin_attr = {
	.attr = {
		.name = "event_data",
		.mode = S_IWUSR,
	},
	.write = sysfs_ofb_data_write,
};
#endif

static int __init sclp_ofb_setup(void)
{
#ifdef CONFIG_SCLP_OFB
	struct kset *ofb_kset;
	int rc;

	ofb_kset = kset_create_and_add("ofb", NULL, firmware_kobj);
	if (!ofb_kset)
		return -ENOMEM;
	rc = sysfs_create_bin_file(&ofb_kset->kobj, &ofb_bin_attr);
	if (rc) {
		kset_unregister(ofb_kset);
		return rc;
	}
#endif
	return 0;
}

static int __init sclp_conf_init(void)
{
	int rc;

	INIT_WORK(&sclp_cpu_capability_work, sclp_cpu_capability_notify);
	INIT_WORK(&sclp_cpu_change_work, sclp_cpu_change_notify);
	rc = sclp_register(&sclp_conf_register);
	if (rc)
		return rc;
	return sclp_ofb_setup();
}

__initcall(sclp_conf_init);
