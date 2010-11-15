/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_ORIGINATOR_H_
#define _NET_BATMAN_ADV_ORIGINATOR_H_

int originator_init(struct bat_priv *bat_priv);
void originator_free(struct bat_priv *bat_priv);
void purge_orig_ref(struct bat_priv *bat_priv);
struct orig_node *get_orig_node(struct bat_priv *bat_priv, uint8_t *addr);
struct neigh_node *
create_neighbor(struct orig_node *orig_node, struct orig_node *orig_neigh_node,
		uint8_t *neigh, struct batman_if *if_incoming);
int orig_seq_print_text(struct seq_file *seq, void *offset);
int orig_hash_add_if(struct batman_if *batman_if, int max_if_num);
int orig_hash_del_if(struct batman_if *batman_if, int max_if_num);

#endif /* _NET_BATMAN_ADV_ORIGINATOR_H_ */
