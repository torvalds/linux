/*
 * Connection data.
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009 - 2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MC_KAPI_CONNECTION_H_
#define _MC_KAPI_CONNECTION_H_

#include <linux/semaphore.h>
#include <linux/mutex.h>

#include <stddef.h>
#include <stdbool.h>

struct connection {
	/* Netlink socket */
	struct sock		*socket_descriptor;
	/* Random? magic to match requests/answers */
	uint32_t		sequence_magic;

	struct nlmsghdr		*data_msg;
	/* How much connection data is left */
	uint32_t		data_len;
	/* Start pointer of remaining data */
	void			*data_start;
	struct sk_buff		*skb;

	/* Data protection lock */
	struct mutex		data_lock;
	/* Data protection semaphore */
	struct semaphore	data_available_sem;

	/* PID address used for local connection */
	pid_t			self_pid;
	/* Remote PID for connection */
	pid_t			peer_pid;

	/* The list param for using the kernel lists */
	struct list_head	list;
};

struct connection *connection_new(void);
struct connection *connection_create(int socket_descriptor, pid_t dest);
void connection_cleanup(struct connection *conn);
bool connection_connect(struct connection *conn, pid_t dest);
size_t connection_read_datablock(struct connection *conn, void *buffer,
					uint32_t len);
size_t connection_read_data(struct connection *conn, void *buffer,
				   uint32_t len, int32_t timeout);
size_t connection_write_data(struct connection *conn, void *buffer,
				    uint32_t len);
int connection_process(struct connection *conn, struct sk_buff *skb);

#endif /* _MC_KAPI_CONNECTION_H_ */
