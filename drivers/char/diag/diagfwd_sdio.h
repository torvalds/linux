/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DIAGFWD_SDIO_H
#define DIAGFWD_SDIO_H

#include <mach/sdio_al.h>
#define N_MDM_WRITE	1 /* Upgrade to 2 with ping pong buffer */
#define N_MDM_READ	1

void diagfwd_sdio_init(void);
void diagfwd_sdio_exit(void);
int diagfwd_connect_sdio(void);
int diagfwd_disconnect_sdio(void);
int diagfwd_read_complete_sdio(void);
int diagfwd_write_complete_sdio(void);

#endif
