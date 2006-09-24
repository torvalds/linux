/*
 * Cryptographic API.
 *
 * Support for VIA PadLock hardware crypto engine.
 *
 * Copyright (c) 2006  Michal Ludvig <michal@logix.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include "padlock.h"

static int __init padlock_init(void)
{
	int success = 0;

	if (crypto_has_cipher("aes-padlock", 0, 0))
		success++;

	if (crypto_has_hash("sha1-padlock", 0, 0))
		success++;

	if (crypto_has_hash("sha256-padlock", 0, 0))
		success++;

	if (!success) {
		printk(KERN_WARNING PFX "No VIA PadLock drivers have been loaded.\n");
		return -ENODEV;
	}

	printk(KERN_NOTICE PFX "%d drivers are available.\n", success);

	return 0;
}

static void __exit padlock_fini(void)
{
}

module_init(padlock_init);
module_exit(padlock_fini);

MODULE_DESCRIPTION("Load all configured PadLock algorithms.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig");

