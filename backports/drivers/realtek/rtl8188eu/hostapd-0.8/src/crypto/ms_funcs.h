/*
 * WPA Supplicant / shared MSCHAPV2 helper functions / RFC 2433 / RFC 2759
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef MS_FUNCS_H
#define MS_FUNCS_H

int generate_nt_response(const u8 *auth_challenge, const u8 *peer_challenge,
			 const u8 *username, size_t username_len,
			 const u8 *password, size_t password_len,
			 u8 *response);
int generate_nt_response_pwhash(const u8 *auth_challenge,
				const u8 *peer_challenge,
				const u8 *username, size_t username_len,
				const u8 *password_hash,
				u8 *response);
int generate_authenticator_response(const u8 *password, size_t password_len,
				    const u8 *peer_challenge,
				    const u8 *auth_challenge,
				    const u8 *username, size_t username_len,
				    const u8 *nt_response, u8 *response);
int generate_authenticator_response_pwhash(
	const u8 *password_hash,
	const u8 *peer_challenge, const u8 *auth_challenge,
	const u8 *username, size_t username_len,
	const u8 *nt_response, u8 *response);
int nt_challenge_response(const u8 *challenge, const u8 *password,
			  size_t password_len, u8 *response);

void challenge_response(const u8 *challenge, const u8 *password_hash,
			u8 *response);
int nt_password_hash(const u8 *password, size_t password_len,
		     u8 *password_hash);
int hash_nt_password_hash(const u8 *password_hash, u8 *password_hash_hash);
int get_master_key(const u8 *password_hash_hash, const u8 *nt_response,
		   u8 *master_key);
int get_asymetric_start_key(const u8 *master_key, u8 *session_key,
			    size_t session_key_len, int is_send,
			    int is_server);
int __must_check encrypt_pw_block_with_password_hash(
	const u8 *password, size_t password_len,
	const u8 *password_hash, u8 *pw_block);
int __must_check new_password_encrypted_with_old_nt_password_hash(
	const u8 *new_password, size_t new_password_len,
	const u8 *old_password, size_t old_password_len,
	u8 *encrypted_pw_block);
void nt_password_hash_encrypted_with_block(const u8 *password_hash,
					   const u8 *block, u8 *cypher);
int old_nt_password_hash_encrypted_with_new_nt_password_hash(
	const u8 *new_password, size_t new_password_len,
	const u8 *old_password, size_t old_password_len,
	u8 *encrypted_password_hash);

#endif /* MS_FUNCS_H */
