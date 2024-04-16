// SPDX-License-Identifier: GPL-2.0
/*
 *    HMC Drive DVD Module
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 */

#define KMSG_COMPONENT "hmcdrv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>

#include "hmcdrv_ftp.h"
#include "hmcdrv_dev.h"
#include "hmcdrv_cache.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Copyright 2013 IBM Corporation");
MODULE_DESCRIPTION("HMC drive DVD access");

/*
 * module parameter 'cachesize'
 */
static size_t hmcdrv_mod_cachesize = HMCDRV_CACHE_SIZE_DFLT;
module_param_named(cachesize, hmcdrv_mod_cachesize, ulong, S_IRUGO);

/**
 * hmcdrv_mod_init() - module init function
 */
static int __init hmcdrv_mod_init(void)
{
	int rc = hmcdrv_ftp_probe(); /* perform w/o cache */

	if (rc)
		return rc;

	rc = hmcdrv_cache_startup(hmcdrv_mod_cachesize);

	if (rc)
		return rc;

	rc = hmcdrv_dev_init();

	if (rc)
		hmcdrv_cache_shutdown();

	return rc;
}

/**
 * hmcdrv_mod_exit() - module exit function
 */
static void __exit hmcdrv_mod_exit(void)
{
	hmcdrv_dev_exit();
	hmcdrv_cache_shutdown();
}

module_init(hmcdrv_mod_init);
module_exit(hmcdrv_mod_exit);
