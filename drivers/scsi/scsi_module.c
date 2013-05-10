/*
 * Copyright (C) 2003 Christoph Hellwig.
 *	Released under GPL v2.
 *
 * Support for old-style host templates.
 *
 * NOTE:  Do not use this for new drivers ever.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <scsi/scsi_host.h>


static int __init init_this_scsi_driver(void)
{
	struct scsi_host_template *sht = &driver_template;
	struct Scsi_Host *shost;
	struct list_head *l;
	int error;

	if (!sht->release) {
		printk(KERN_ERR
		    "scsi HBA driver %s didn't set a release method.\n",
		    sht->name);
		return -EINVAL;
	}

	sht->module = THIS_MODULE;
	INIT_LIST_HEAD(&sht->legacy_hosts);

	sht->detect(sht);
	if (list_empty(&sht->legacy_hosts))
		return -ENODEV;

	list_for_each_entry(shost, &sht->legacy_hosts, sht_legacy_list) {
		error = scsi_add_host(shost, NULL);
		if (error)
			goto fail;
		scsi_scan_host(shost);
	}
	return 0;
 fail:
	l = &shost->sht_legacy_list;
	while ((l = l->prev) != &sht->legacy_hosts)
		scsi_remove_host(list_entry(l, struct Scsi_Host, sht_legacy_list));
	return error;
}

static void __exit exit_this_scsi_driver(void)
{
	struct scsi_host_template *sht = &driver_template;
	struct Scsi_Host *shost, *s;

	list_for_each_entry(shost, &sht->legacy_hosts, sht_legacy_list)
		scsi_remove_host(shost);
	list_for_each_entry_safe(shost, s, &sht->legacy_hosts, sht_legacy_list)
		sht->release(shost);

	if (list_empty(&sht->legacy_hosts))
		return;

	printk(KERN_WARNING "%s did not call scsi_unregister\n", sht->name);
	dump_stack();

	list_for_each_entry_safe(shost, s, &sht->legacy_hosts, sht_legacy_list)
		scsi_unregister(shost);
}

module_init(init_this_scsi_driver);
module_exit(exit_this_scsi_driver);
