/* $OpenBSD: mac.h,v 1.10 2016/07/08 03:44:42 djm Exp $ */
/*
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SSHMAC_H
#define SSHMAC_H

#include <sys/types.h>

struct sshmac {
	char	*name;
	int	enabled;
	u_int	mac_len;
	u_char	*key;
	u_int	key_len;
	int	type;
	int	etm;		/* Encrypt-then-MAC */
	struct ssh_hmac_ctx	*hmac_ctx;
	struct umac_ctx		*umac_ctx;
};

int	 mac_valid(const char *);
char	*mac_alg_list(char);
int	 mac_setup(struct sshmac *, char *);
int	 mac_init(struct sshmac *);
int	 mac_compute(struct sshmac *, u_int32_t, const u_char *, int,
    u_char *, size_t);
int	 mac_check(struct sshmac *, u_int32_t, const u_char *, size_t,
    const u_char *, size_t);
void	 mac_clear(struct sshmac *);

#endif /* SSHMAC_H */
