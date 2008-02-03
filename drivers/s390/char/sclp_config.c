/*
 *  drivers/s390/char/sclp_config.c
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/cpu.h>
#include <linux/sysdev.h>
#include <linux/workqueue.h>
#include "sclp.h"

#define TAG	"sclp_config: "

struct conf_mgm_data {
	u8 reserved;
	u8 ev_qualifier;
} __attribute__((packed));

#define EV_QUAL_CAP_CHANGE	3

static struct work_struct sclp_cpu_capability_work;

static void sclp_cpu_capability_notify(struct work_struct *work)
{
	int cpu;
	struct sys_device *sysdev;

	printk(KERN_WARNING TAG "cpu capability changed.\n");
	get_online_cpus();
	for_each_online_cpu(cpu) {
		sysdev = get_cpu_sysdev(cpu);
		kobject_uevent(&sysdev->kobj, KOBJ_CHANGE);
	}
	put_online_cpus();
}

static void sclp_conf_receiver_fn(struct evbuf_header *evbuf)
{
	struct conf_mgm_data *cdata;

	cdata = (struct conf_mgm_data *)(evbuf + 1);
	if (cdata->ev_qualifier == EV_QUAL_CAP_CHANGE)
		schedule_work(&sclp_cpu_capability_work);
}

static struct sclp_register sclp_conf_register =
{
	.receive_mask = EVTYP_CONFMGMDATA_MASK,
	.receiver_fn  = sclp_conf_receiver_fn,
};

static int __init sclp_conf_init(void)
{
	int rc;

	INIT_WORK(&sclp_cpu_capability_work, sclp_cpu_capability_notify);

	rc = sclp_register(&sclp_conf_register);
	if (rc) {
		printk(KERN_ERR TAG "failed to register (%d).\n", rc);
		return rc;
	}

	if (!(sclp_conf_register.sclp_receive_mask & EVTYP_CONFMGMDATA_MASK)) {
		printk(KERN_WARNING TAG "no configuration management.\n");
		sclp_unregister(&sclp_conf_register);
		rc = -ENOSYS;
	}
	return rc;
}

__initcall(sclp_conf_init);
