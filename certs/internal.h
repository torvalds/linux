/* Internal definitions
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

/*
 * system_keyring.c
 */
#ifdef CONFIG_SECONDARY_TRUSTED_KEYRING
extern void __init add_trusted_secondary_key(const char *source,
					     const void *data, size_t len);
#endif
