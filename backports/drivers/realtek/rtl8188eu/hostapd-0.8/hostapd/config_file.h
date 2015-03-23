/*
 * hostapd / Configuration file parser
 * Copyright (c) 2003-2009, Jouni Malinen <j@w1.fi>
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

#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

struct hostapd_config * hostapd_config_read(const char *fname);

#endif /* CONFIG_FILE_H */
