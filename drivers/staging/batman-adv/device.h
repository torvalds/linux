/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
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

void bat_device_init(void);
int bat_device_setup(void);
void bat_device_destroy(void);
int bat_device_open(struct inode *inode, struct file *file);
int bat_device_release(struct inode *inode, struct file *file);
ssize_t bat_device_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos);
ssize_t bat_device_write(struct file *file, const char __user *buff,
			 size_t len, loff_t *off);
unsigned int bat_device_poll(struct file *file, poll_table *wait);
void bat_device_add_packet(struct device_client *device_client,
			   struct icmp_packet *icmp_packet);
void bat_device_receive_packet(struct icmp_packet *icmp_packet);
