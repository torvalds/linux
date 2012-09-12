/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#define KMSG_COMPONENT "sclp_config"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <asm/smp.h>

#include "sclp.h"

struct conf_mgm_data {
	u8 reserved;
	u8 ev_qualifier;
} __attribute__((packed));

#define EV_QUAL_CPU_CHANGE	1
#define EV_QUAL_CAP_CHANGE	3

static struct work_struct sclp_cpu_capability_work;
static struct work_struct sclp_cpu_change_work;

static void sclp_cpu_capability_notify(struct work_struct *work)
{
	int cpu;
	struct device *dev;

	s390_adjust_jiffies();
	pr_warning("cpu capability changed.\n");
	get_online_cpus();
	for_each_online_cpu(cpu) {
		dev = get_cpu_device(cpu);
		kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	}
	put_online_cpus();
}

static void __ref sclp_cpu_change_notify(struct work_struct *work)
{
	smp_rescan_cpus();
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
	.receive_mask = EVTYP_CONFMGMDATA_MASK,
	.receiver_fn  = sclp_conf_receiver_fn,
};

static int __init sclp_conf_init(void)
{
	INIT_WORK(&sclp_cpu_capability_work, sclp_cpu_capability_notify);
	INIT_WORK(&sclp_cpu_change_work, sclp_cpu_change_notify);
	return sclp_register(&sclp_conf_register);
}

__initcall(sclp_conf_init);
