/*
 * Diffie-Hellman group 5 operations
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
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

#ifndef DH_GROUP5_H
#define DH_GROUP5_H

void * dh5_init(struct wpabuf **priv, struct wpabuf **publ);
struct wpabuf * dh5_derive_shared(void *ctx, const struct wpabuf *peer_public,
				  const struct wpabuf *own_private);
void dh5_free(void *ctx);

#endif /* DH_GROUP5_H */
