/*
 *    SCLP OCF communication parameters sysfs interface
 *
 *    Copyright IBM Corp. 2011
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#define KMSG_COMPONENT "sclp_ocf"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <asm/ebcdic.h>
#include <asm/sclp.h>

#include "sclp.h"

#define OCF_LENGTH_HMC_NETWORK 8UL
#define OCF_LENGTH_CPC_NAME 8UL

static char hmc_network[OCF_LENGTH_HMC_NETWORK + 1];
static char cpc_name[OCF_LENGTH_CPC_NAME]; /* in EBCDIC */

static DEFINE_SPINLOCK(sclp_ocf_lock);
static struct work_struct sclp_ocf_change_work;

static struct kset *ocf_kset;

static void sclp_ocf_change_notify(struct work_struct *work)
{
	kobject_uevent(&ocf_kset->kobj, KOBJ_CHANGE);
}

/* Handler for OCF event. Look for the CPC image name. */
static void sclp_ocf_handler(struct evbuf_header *evbuf)
{
	struct gds_vector *v;
	struct gds_subvector *sv, *netid, *cpc;
	size_t size;

	/* Find the 0x9f00 block. */
	v = sclp_find_gds_vector(evbuf + 1, (void *) evbuf + evbuf->length,
				 0x9f00);
	if (!v)
		return;
	/* Find the 0x9f22 block inside the 0x9f00 block. */
	v = sclp_find_gds_vector(v + 1, (void *) v + v->length, 0x9f22);
	if (!v)
		return;
	/* Find the 0x81 block inside the 0x9f22 block. */
	sv = sclp_find_gds_subvector(v + 1, (void *) v + v->length, 0x81);
	if (!sv)
		return;
	/* Find the 0x01 block inside the 0x81 block. */
	netid = sclp_find_gds_subvector(sv + 1, (void *) sv + sv->length, 1);
	/* Find the 0x02 block inside the 0x81 block. */
	cpc = sclp_find_gds_subvector(sv + 1, (void *) sv + sv->length, 2);
	/* Copy network name and cpc name. */
	spin_lock(&sclp_ocf_lock);
	if (netid) {
		size = min(OCF_LENGTH_HMC_NETWORK, (size_t) netid->length);
		memcpy(hmc_network, netid + 1, size);
		EBCASC(hmc_network, size);
		hmc_network[size] = 0;
	}
	if (cpc) {
		size = min(OCF_LENGTH_CPC_NAME, (size_t) cpc->length);
		memset(cpc_name, 0, OCF_LENGTH_CPC_NAME);
		memcpy(cpc_name, cpc + 1, size);
	}
	spin_unlock(&sclp_ocf_lock);
	schedule_work(&sclp_ocf_change_work);
}

static struct sclp_register sclp_ocf_event = {
	.receive_mask = EVTYP_OCF_MASK,
	.receiver_fn = sclp_ocf_handler,
};

void sclp_ocf_cpc_name_copy(char *dst)
{
	spin_lock_irq(&sclp_ocf_lock);
	memcpy(dst, cpc_name, OCF_LENGTH_CPC_NAME);
	spin_unlock_irq(&sclp_ocf_lock);
}
EXPORT_SYMBOL(sclp_ocf_cpc_name_copy);

static ssize_t cpc_name_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *page)
{
	char name[OCF_LENGTH_CPC_NAME + 1];

	sclp_ocf_cpc_name_copy(name);
	name[OCF_LENGTH_CPC_NAME] = 0;
	EBCASC(name, OCF_LENGTH_CPC_NAME);
	return snprintf(page, PAGE_SIZE, "%s\n", name);
}

static struct kobj_attribute cpc_name_attr =
	__ATTR(cpc_name, 0444, cpc_name_show, NULL);

static ssize_t hmc_network_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *page)
{
	int rc;

	spin_lock_irq(&sclp_ocf_lock);
	rc = snprintf(page, PAGE_SIZE, "%s\n", hmc_network);
	spin_unlock_irq(&sclp_ocf_lock);
	return rc;
}

static struct kobj_attribute hmc_network_attr =
	__ATTR(hmc_network, 0444, hmc_network_show, NULL);

static struct attribute *ocf_attrs[] = {
	&cpc_name_attr.attr,
	&hmc_network_attr.attr,
	NULL,
};

static struct attribute_group ocf_attr_group = {
	.attrs = ocf_attrs,
};

static int __init ocf_init(void)
{
	int rc;

	INIT_WORK(&sclp_ocf_change_work, sclp_ocf_change_notify);
	ocf_kset = kset_create_and_add("ocf", NULL, firmware_kobj);
	if (!ocf_kset)
		return -ENOMEM;

	rc = sysfs_create_group(&ocf_kset->kobj, &ocf_attr_group);
	if (rc) {
		kset_unregister(ocf_kset);
		return rc;
	}

	return sclp_register(&sclp_ocf_event);
}

device_initcall(ocf_init);
