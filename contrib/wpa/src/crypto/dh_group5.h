/*
 * Diffie-Hellman group 5 operations
 * Copyright (c) 2009, 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DH_GROUP5_H
#define DH_GROUP5_H

void * dh5_init(struct wpabuf **priv, struct wpabuf **publ);
void * dh5_init_fixed(const struct wpabuf *priv, const struct wpabuf *publ);
struct wpabuf * dh5_derive_shared(void *ctx, const struct wpabuf *peer_public,
				  const struct wpabuf *own_private);
void dh5_free(void *ctx);

#endif /* DH_GROUP5_H */
