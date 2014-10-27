/* Internal definitions for asymmetric key type
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

extern struct asymmetric_key_id *asymmetric_key_hex_to_key_id(const char *id);

static inline
const struct asymmetric_key_ids *asymmetric_key_ids(const struct key *key)
{
	return key->type_data.p[1];
}
