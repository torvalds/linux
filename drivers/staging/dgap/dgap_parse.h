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

#ifndef _DGAP_PARSE_H
#define _DGAP_PARSE_H

#include "dgap_driver.h"

extern int dgap_parsefile(char **in, int Remove);
extern struct cnode *dgap_find_config(int type, int bus, int slot);
extern uint dgap_config_get_number_of_ports(struct board_t *bd);
extern char *dgap_create_config_string(struct board_t *bd, char *string);
extern char *dgap_get_config_letters(struct board_t *bd, char *string);
extern uint dgap_config_get_useintr(struct board_t *bd);
extern uint dgap_config_get_altpin(struct board_t *bd);

#endif
