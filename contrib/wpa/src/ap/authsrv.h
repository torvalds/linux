/*
 * Authentication server setup
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef AUTHSRV_H
#define AUTHSRV_H

int authsrv_init(struct hostapd_data *hapd);
void authsrv_deinit(struct hostapd_data *hapd);

#endif /* AUTHSRV_H */
