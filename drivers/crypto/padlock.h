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

/* Control word. */
union cword {
	uint32_t cword[4];
	struct {
		int rounds:4;
		int algo:3;
		int keygen:1;
		int interm:1;
		int encdec:1;
		int ksize:2;
	} b;
};

#define PFX	"padlock: "

#ifdef CONFIG_CRYPTO_DEV_PADLOCK_AES
int padlock_init_aes(void);
void padlock_fini_aes(void);
#endif

#endif	/* _CRYPTO_PADLOCK_H */
