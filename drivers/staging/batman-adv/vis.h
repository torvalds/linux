/*
 * Copyright (C) 2008-2010 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich, Marek Lindner
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

#define VIS_TIMEOUT		200000

struct vis_info {
	unsigned long       first_seen;
	struct list_head    recv_list;
			    /* list of server-neighbors we received a vis-packet
			     * from.  we should not reply to them. */
	struct list_head send_list;
	struct kref refcount;
	/* this packet might be part of the vis send queue. */
	struct vis_packet packet;
	/* vis_info may follow here*/
} __attribute__((packed));

struct vis_info_entry {
	uint8_t  src[ETH_ALEN];
	uint8_t  dest[ETH_ALEN];
	uint8_t  quality;	/* quality = 0 means HNA */
} __attribute__((packed));

struct recvlist_node {
	struct list_head list;
	uint8_t mac[ETH_ALEN];
};

extern struct hashtable_t *vis_hash;
extern spinlock_t vis_hash_lock;

ssize_t vis_fill_buffer_text(struct net_device *net_dev, char *buff,
			      size_t count, loff_t off);
void receive_server_sync_packet(struct bat_priv *bat_priv,
				struct vis_packet *vis_packet,
				int vis_info_len);
void receive_client_update_packet(struct bat_priv *bat_priv,
				  struct vis_packet *vis_packet,
				  int vis_info_len);
int vis_init(void);
void vis_quit(void);
