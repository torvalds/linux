/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 */

#ifndef _DGNC_TTY_H
#define _DGNC_TTY_H

#include "dgnc_driver.h"

int	dgnc_tty_register(struct dgnc_board *brd);
void dgnc_tty_unregister(struct dgnc_board *brd);

int     dgnc_tty_init(struct dgnc_board *brd);

void	dgnc_cleanup_tty(struct dgnc_board *brd);

void	dgnc_input(struct channel_t *ch);
void	dgnc_carrier(struct channel_t *ch);
void	dgnc_wakeup_writes(struct channel_t *ch);
void	dgnc_check_queue_flow_control(struct channel_t *ch);

#endif	/* _DGNC_TTY_H */
