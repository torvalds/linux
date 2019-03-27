/*
 * CHAP-MD5 (RFC 1994)
 * Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef CHAP_H
#define CHAP_H

#define CHAP_MD5_LEN 16

int chap_md5(u8 id, const u8 *secret, size_t secret_len, const u8 *challenge,
	     size_t challenge_len, u8 *response);

#endif /* CHAP_H */
