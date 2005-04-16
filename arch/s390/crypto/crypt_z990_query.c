/*
 * Cryptographic API.
 *
 * Support for z990 cryptographic instructions.
 * Testing module for querying processor crypto capabilities.
 *
 * Copyright (c) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/errno.h>
#include "crypt_z990.h"

static void
query_available_functions(void)
{
	printk(KERN_INFO "#####################\n");
	//query available KM functions
	printk(KERN_INFO "KM_QUERY: %d\n",
			crypt_z990_func_available(KM_QUERY));
	printk(KERN_INFO "KM_DEA: %d\n",
			crypt_z990_func_available(KM_DEA_ENCRYPT));
	printk(KERN_INFO "KM_TDEA_128: %d\n",
			crypt_z990_func_available(KM_TDEA_128_ENCRYPT));
	printk(KERN_INFO "KM_TDEA_192: %d\n",
			crypt_z990_func_available(KM_TDEA_192_ENCRYPT));
	//query available KMC functions
	printk(KERN_INFO "KMC_QUERY: %d\n",
			crypt_z990_func_available(KMC_QUERY));
	printk(KERN_INFO "KMC_DEA: %d\n",
			crypt_z990_func_available(KMC_DEA_ENCRYPT));
	printk(KERN_INFO "KMC_TDEA_128: %d\n",
			crypt_z990_func_available(KMC_TDEA_128_ENCRYPT));
	printk(KERN_INFO "KMC_TDEA_192: %d\n",
			crypt_z990_func_available(KMC_TDEA_192_ENCRYPT));
	//query available KIMD fucntions
	printk(KERN_INFO "KIMD_QUERY: %d\n",
			crypt_z990_func_available(KIMD_QUERY));
	printk(KERN_INFO "KIMD_SHA_1: %d\n",
			crypt_z990_func_available(KIMD_SHA_1));
	//query available KLMD functions
	printk(KERN_INFO "KLMD_QUERY: %d\n",
			crypt_z990_func_available(KLMD_QUERY));
	printk(KERN_INFO "KLMD_SHA_1: %d\n",
			crypt_z990_func_available(KLMD_SHA_1));
	//query available KMAC functions
	printk(KERN_INFO "KMAC_QUERY: %d\n",
			crypt_z990_func_available(KMAC_QUERY));
	printk(KERN_INFO "KMAC_DEA: %d\n",
			crypt_z990_func_available(KMAC_DEA));
	printk(KERN_INFO "KMAC_TDEA_128: %d\n",
			crypt_z990_func_available(KMAC_TDEA_128));
	printk(KERN_INFO "KMAC_TDEA_192: %d\n",
			crypt_z990_func_available(KMAC_TDEA_192));
}

static int
init(void)
{
	struct crypt_z990_query_status status = {
		.high = 0,
		.low = 0
	};

	printk(KERN_INFO "crypt_z990: querying available crypto functions\n");
	crypt_z990_km(KM_QUERY, &status, NULL, NULL, 0);
	printk(KERN_INFO "KM: %016llx %016llx\n",
			(unsigned long long) status.high,
			(unsigned long long) status.low);
	status.high = status.low = 0;
	crypt_z990_kmc(KMC_QUERY, &status, NULL, NULL, 0);
	printk(KERN_INFO "KMC: %016llx %016llx\n",
			(unsigned long long) status.high,
			(unsigned long long) status.low);
	status.high = status.low = 0;
	crypt_z990_kimd(KIMD_QUERY, &status, NULL, 0);
	printk(KERN_INFO "KIMD: %016llx %016llx\n",
			(unsigned long long) status.high,
			(unsigned long long) status.low);
	status.high = status.low = 0;
	crypt_z990_klmd(KLMD_QUERY, &status, NULL, 0);
	printk(KERN_INFO "KLMD: %016llx %016llx\n",
			(unsigned long long) status.high,
			(unsigned long long) status.low);
	status.high = status.low = 0;
	crypt_z990_kmac(KMAC_QUERY, &status, NULL, 0);
	printk(KERN_INFO "KMAC: %016llx %016llx\n",
			(unsigned long long) status.high,
			(unsigned long long) status.low);

	query_available_functions();
	return -1;
}

static void __exit
cleanup(void)
{
}

module_init(init);
module_exit(cleanup);

MODULE_LICENSE("GPL");
