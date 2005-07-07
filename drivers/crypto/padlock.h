/*
 * Driver for VIA PadLock
 *
 * Copyright (c) 2004 Michal Ludvig <michal@logix.cz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_PADLOCK_H
#define _CRYPTO_PADLOCK_H

#define PADLOCK_ALIGNMENT 16

/* Control word. */
struct cword {
	int __attribute__ ((__packed__))
		rounds:4,
		algo:3,
		keygen:1,
		interm:1,
		encdec:1,
		ksize:2;
} __attribute__ ((__aligned__(PADLOCK_ALIGNMENT)));

#define PFX	"padlock: "

#ifdef CONFIG_CRYPTO_DEV_PADLOCK_AES
int padlock_init_aes(void);
void padlock_fini_aes(void);
#endif

#endif	/* _CRYPTO_PADLOCK_H */
