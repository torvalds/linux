/*
 * Authentication server setup
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
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

#ifndef AUTHSRV_H
#define AUTHSRV_H

int authsrv_init(struct hostapd_data *hapd);
void authsrv_deinit(struct hostapd_data *hapd);

#endif /* AUTHSRV_H */
