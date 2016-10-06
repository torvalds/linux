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
 */

#ifndef __DGNC_TTY_H
#define __DGNC_TTY_H

#include "dgnc_driver.h"

int	dgnc_tty_register(struct dgnc_board *brd);
void dgnc_tty_unregister(struct dgnc_board *brd);

int	dgnc_tty_preinit(void);
int     dgnc_tty_init(struct dgnc_board *);

void	dgnc_tty_post_uninit(void);
void	dgnc_cleanup_tty(struct dgnc_board *);

void	dgnc_input(struct channel_t *ch);
void	dgnc_carrier(struct channel_t *ch);
void	dgnc_wakeup_writes(struct channel_t *ch);
void	dgnc_check_queue_flow_control(struct channel_t *ch);

#endif
