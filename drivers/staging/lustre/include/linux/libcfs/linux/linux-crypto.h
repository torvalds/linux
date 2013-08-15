 /*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see http://www.gnu.org/licenses
 *
 * Please  visit http://www.xyratex.com/contact if you need additional
 * information or have any questions.
 *
 * GPL HEADER END
 */

/*
 * Copyright 2012 Xyratex Technology Limited
 */

/**
 * Linux crypto hash specific functions.
 */

/**
 * Functions for start/stop shash CRC32 algorithm.
 */
int cfs_crypto_crc32_register(void);
void cfs_crypto_crc32_unregister(void);

/**
 * Functions for start/stop shash adler32 algorithm.
 */
int cfs_crypto_adler32_register(void);
void cfs_crypto_adler32_unregister(void);

/**
 * Functions for start/stop shash crc32 pclmulqdq
 */
int cfs_crypto_crc32_pclmul_register(void);
void cfs_crypto_crc32_pclmul_unregister(void);
