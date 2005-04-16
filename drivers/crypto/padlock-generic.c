/* 
 * Cryptographic API.
 *
 * Support for VIA PadLock hardware crypto engine.
 *
 * Copyright (c) 2004  Michal Ludvig <michal@logix.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <asm/byteorder.h>
#include "padlock.h"

static int __init
padlock_init(void)
{
	int ret = -ENOSYS;
	
	if (!cpu_has_xcrypt) {
		printk(KERN_ERR PFX "VIA PadLock not detected.\n");
		return -ENODEV;
	}

	if (!cpu_has_xcrypt_enabled) {
		printk(KERN_ERR PFX "VIA PadLock detected, but not enabled. Hmm, strange...\n");
		return -ENODEV;
	}

#ifdef CONFIG_CRYPTO_DEV_PADLOCK_AES
	if ((ret = padlock_init_aes())) {
		printk(KERN_ERR PFX "VIA PadLock AES initialization failed.\n");
		return ret;
	}
#endif

	if (ret == -ENOSYS)
		printk(KERN_ERR PFX "Hmm, VIA PadLock was compiled without any algorithm.\n");

	return ret;
}

static void __exit
padlock_fini(void)
{
#ifdef CONFIG_CRYPTO_DEV_PADLOCK_AES
	padlock_fini_aes();
#endif
}

module_init(padlock_init);
module_exit(padlock_fini);

MODULE_DESCRIPTION("VIA PadLock crypto engine support.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Michal Ludvig");
