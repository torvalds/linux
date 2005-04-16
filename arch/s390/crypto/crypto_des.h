/*
 * Cryptographic API.
 *
 * Function for checking keys for the DES and Tripple DES Encryption
 * algorithms.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef __CRYPTO_DES_H__
#define __CRYPTO_DES_H__

extern int crypto_des_check_key(const u8*, unsigned int, u32*);

#endif //__CRYPTO_DES_H__
