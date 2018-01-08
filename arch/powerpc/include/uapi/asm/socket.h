/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _ASM_POWERPC_SOCKET_H
#define _ASM_POWERPC_SOCKET_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define SO_RCVLOWAT	16
#define SO_SNDLOWAT	17
#define SO_RCVTIMEO	18
#define SO_SNDTIMEO	19
#define SO_PASSCRED	20
#define SO_PEERCRED	21

#include <asm-generic/socket.h>

#endif	/* _ASM_POWERPC_SOCKET_H */
