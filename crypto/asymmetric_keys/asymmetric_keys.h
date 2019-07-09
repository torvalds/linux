/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal definitions for asymmetric key type
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <keys/asymmetric-type.h>

extern struct asymmetric_key_id *asymmetric_key_hex_to_key_id(const char *id);

extern int __asymmetric_key_hex_to_key_id(const char *id,
					  struct asymmetric_key_id *match_id,
					  size_t hexlen);

extern int asymmetric_key_eds_op(struct kernel_pkey_params *params,
				 const void *in, void *out);
