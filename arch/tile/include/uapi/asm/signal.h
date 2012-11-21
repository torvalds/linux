/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _UAPI_ASM_TILE_SIGNAL_H
#define _UAPI_ASM_TILE_SIGNAL_H

/* Do not notify a ptracer when this signal is handled. */
#define SA_NOPTRACE 0x02000000u

/* Used in earlier Tilera releases, so keeping for binary compatibility. */
#define SA_RESTORER 0x04000000u

#include <asm-generic/signal.h>


#endif /* _UAPI_ASM_TILE_SIGNAL_H */
