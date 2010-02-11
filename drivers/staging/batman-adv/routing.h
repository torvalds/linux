/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
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

#include "types.h"

extern wait_queue_head_t thread_wait;
extern atomic_t exit_cond;

int originator_init(void);
void free_orig_node(void *data);
void originator_free(void);
void slide_own_bcast_window(struct batman_if *batman_if);
void batman_data_ready(struct sock *sk, int len);
void purge_orig(struct work_struct *work);
int packet_recv_thread(void *data);
void receive_bat_packet(struct ethhdr *ethhdr, struct batman_packet *batman_packet, unsigned char *hna_buff, int hna_buff_len, struct batman_if *if_incoming);
