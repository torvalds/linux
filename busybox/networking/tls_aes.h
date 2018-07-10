/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * Selected few declarations for AES.
 */

void aes_cbc_encrypt(const void *key, int klen, void *iv, const void *data, size_t len, void *dst);
void aes_cbc_decrypt(const void *key, int klen, void *iv, const void *data, size_t len, void *dst);
