/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip lan969x Switch driver
 *
 * Copyright (c) 2024 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __LAN969X_H__
#define __LAN969X_H__

#include "../sparx5/sparx5_main.h"
#include "../sparx5/sparx5_regs.h"

/* lan969x.c */
extern const struct sparx5_match_data lan969x_desc;

/* lan969x_regs.c */
extern const unsigned int lan969x_tsize[TSIZE_LAST];
extern const unsigned int lan969x_raddr[RADDR_LAST];
extern const unsigned int lan969x_rcnt[RCNT_LAST];
extern const unsigned int lan969x_gaddr[GADDR_LAST];
extern const unsigned int lan969x_gcnt[GCNT_LAST];
extern const unsigned int lan969x_gsize[GSIZE_LAST];
extern const unsigned int lan969x_fpos[FPOS_LAST];
extern const unsigned int lan969x_fsize[FSIZE_LAST];

#endif
