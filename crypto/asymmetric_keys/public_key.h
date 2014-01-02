/* Public key algorithm internals
 *
 * See Documentation/crypto/asymmetric-keys.txt
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <crypto/public_key.h>

extern struct asymmetric_key_subtype public_key_subtype;

/*
 * Public key algorithm definition.
 */
struct public_key_algorithm {
	const char	*name;
	u8		n_pub_mpi;	/* Number of MPIs in public key */
	u8		n_sec_mpi;	/* Number of MPIs in secret key */
	u8		n_sig_mpi;	/* Number of MPIs in a signature */
	int (*verify_signature)(const struct public_key *key,
				const struct public_key_signature *sig);
};

extern const struct public_key_algorithm RSA_public_key_algorithm;

/*
 * public_key.c
 */
extern int public_key_verify_signature(const struct public_key *pk,
				       const struct public_key_signature *sig);
