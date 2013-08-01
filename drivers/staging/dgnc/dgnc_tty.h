/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 */

#ifndef __DGNC_TTY_H
#define __DGNC_TTY_H

#include "dgnc_driver.h"

int	dgnc_tty_register(struct board_t *brd);

int	dgnc_tty_preinit(void);
int     dgnc_tty_init(struct board_t *);

void	dgnc_tty_post_uninit(void);
void	dgnc_tty_uninit(struct board_t *);

void	dgnc_input(struct channel_t *ch);
void	dgnc_carrier(struct channel_t *ch);
void	dgnc_wakeup_writes(struct channel_t *ch);
void	dgnc_check_queue_flow_control(struct channel_t *ch);

void	dgnc_sniff_nowait_nolock(struct channel_t *ch, uchar *text, uchar *buf, int nbuf);

#endif
