/*
 * hostapd / UNIX domain socket -based control interface
 * Copyright (c) 2004, Jouni Malinen <j@w1.fi>
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

#ifndef CTRL_IFACE_H
#define CTRL_IFACE_H

#ifndef CONFIG_NO_CTRL_IFACE
int hostapd_ctrl_iface_init(struct hostapd_data *hapd);
void hostapd_ctrl_iface_deinit(struct hostapd_data *hapd);
#else /* CONFIG_NO_CTRL_IFACE */
static inline int hostapd_ctrl_iface_init(struct hostapd_data *hapd)
{
	return 0;
}

static inline void hostapd_ctrl_iface_deinit(struct hostapd_data *hapd)
{
}
#endif /* CONFIG_NO_CTRL_IFACE */

#endif /* CTRL_IFACE_H */
