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

#ifndef __DGAP_TTY_H
#define __DGAP_TTY_H

#include "dgap_driver.h"

int	dgap_tty_register(struct board_t *brd);

int	dgap_tty_preinit(void);
int     dgap_tty_init(struct board_t *);

void	dgap_tty_post_uninit(void);
void	dgap_tty_uninit(struct board_t *);

void	dgap_carrier(struct channel_t *ch);
void	dgap_input(struct channel_t *ch);


#endif
