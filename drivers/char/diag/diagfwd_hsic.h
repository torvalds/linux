/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
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

#ifndef DIAGFWD_HSIC_H
#define DIAGFWD_HSIC_H

#include <mach/diag_bridge.h>
#define N_MDM_WRITE	1 /* Upgrade to 2 with ping pong buffer */
#define N_MDM_READ	1

enum {
	WRITE_TO_USB = 0,
	WRITE_TO_SD
};

void diagfwd_hsic_init(void);
void diagfwd_hsic_exit(void);
int diagfwd_connect_hsic(unsigned int);
int diagfwd_disconnect_hsic(void);

#endif
