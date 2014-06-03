/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LIBCFS_LIBCFS_H__
#define __LIBCFS_LIBCFS_H__

#if !__GNUC__
#define __attribute__(x)
#endif

#include <linux/libcfs/linux/libcfs.h>
#include <linux/gfp.h>

#include "curproc.h"

#ifndef offsetof
# define offsetof(typ,memb) ((long)(long_ptr_t)((char *)&(((typ *)0)->memb)))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof ((a)[0])))
#endif

#if !defined(swap)
#define swap(x,y) do { typeof(x) z = x; x = y; y = z; } while (0)
#endif

#if !defined(container_of)
/* given a pointer @ptr to the field @member embedded into type (usually
 * struct) @type, return pointer to the embedding instance of @type. */
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

static inline int __is_po2(unsigned long long val)
{
	return !(val & (val - 1));
}

#define IS_PO2(val) __is_po2((unsigned long long)(val))

#define LOWEST_BIT_SET(x)       ((x) & ~((x) - 1))

/*
 * Lustre Error Checksum: calculates checksum
 * of Hex number by XORing each bit.
 */
#define LERRCHKSUM(hexnum) (((hexnum) & 0xf) ^ ((hexnum) >> 4 & 0xf) ^ \
			   ((hexnum) >> 8 & 0xf))

#define LUSTRE_SRV_LNET_PID      LUSTRE_LNET_PID

#include <linux/list.h>

/* libcfs tcpip */
int libcfs_ipif_query(char *name, int *up, __u32 *ip, __u32 *mask);
int libcfs_ipif_enumerate(char ***names);
void libcfs_ipif_free_enumeration(char **names, int n);
int libcfs_sock_listen(socket_t **sockp, __u32 ip, int port, int backlog);
int libcfs_sock_accept(socket_t **newsockp, socket_t *sock);
void libcfs_sock_abort_accept(socket_t *sock);
int libcfs_sock_connect(socket_t **sockp, int *fatal,
			__u32 local_ip, int local_port,
			__u32 peer_ip, int peer_port);
int libcfs_sock_setbuf(socket_t *socket, int txbufsize, int rxbufsize);
int libcfs_sock_getbuf(socket_t *socket, int *txbufsize, int *rxbufsize);
int libcfs_sock_getaddr(socket_t *socket, int remote, __u32 *ip, int *port);
int libcfs_sock_write(socket_t *sock, void *buffer, int nob, int timeout);
int libcfs_sock_read(socket_t *sock, void *buffer, int nob, int timeout);
void libcfs_sock_release(socket_t *sock);

/* need both kernel and user-land acceptor */
#define LNET_ACCEPTOR_MIN_RESERVED_PORT    512
#define LNET_ACCEPTOR_MAX_RESERVED_PORT    1023

/*
 * libcfs pseudo device operations
 *
 * It's just draft now.
 */

struct cfs_psdev_file {
	unsigned long   off;
	void	    *private_data;
	unsigned long   reserved1;
	unsigned long   reserved2;
};

struct cfs_psdev_ops {
	int (*p_open)(unsigned long, void *);
	int (*p_close)(unsigned long, void *);
	int (*p_read)(struct cfs_psdev_file *, char *, unsigned long);
	int (*p_write)(struct cfs_psdev_file *, char *, unsigned long);
	int (*p_ioctl)(struct cfs_psdev_file *, unsigned long, void *);
};

/*
 * Drop into debugger, if possible. Implementation is provided by platform.
 */

void cfs_enter_debugger(void);

/*
 * Defined by platform
 */
int unshare_fs_struct(void);
sigset_t cfs_get_blocked_sigs(void);
sigset_t cfs_block_allsigs(void);
sigset_t cfs_block_sigs(unsigned long sigs);
sigset_t cfs_block_sigsinv(unsigned long sigs);
void cfs_restore_sigs(sigset_t);
int cfs_signal_pending(void);
void cfs_clear_sigpending(void);

/*
 * Random number handling
 */

/* returns a random 32-bit integer */
unsigned int cfs_rand(void);
/* seed the generator */
void cfs_srand(unsigned int, unsigned int);
void cfs_get_random_bytes(void *buf, int size);

#include <linux/libcfs/libcfs_debug.h>
#include <linux/libcfs/libcfs_cpu.h>
#include <linux/libcfs/libcfs_private.h>
#include <linux/libcfs/libcfs_ioctl.h>
#include <linux/libcfs/libcfs_prim.h>
#include <linux/libcfs/libcfs_time.h>
#include <linux/libcfs/libcfs_string.h>
#include <linux/libcfs/libcfs_kernelcomm.h>
#include <linux/libcfs/libcfs_workitem.h>
#include <linux/libcfs/libcfs_hash.h>
#include <linux/libcfs/libcfs_heap.h>
#include <linux/libcfs/libcfs_fail.h>
#include <linux/libcfs/params_tree.h>
#include <linux/libcfs/libcfs_crypto.h>

/* container_of depends on "likely" which is defined in libcfs_private.h */
static inline void *__container_of(void *ptr, unsigned long shift)
{
	if (unlikely(IS_ERR(ptr) || ptr == NULL))
		return ptr;
	else
		return (char *)ptr - shift;
}

#define container_of0(ptr, type, member) \
	((type *)__container_of((void *)(ptr), offsetof(type, member)))

#define _LIBCFS_H

#endif /* _LIBCFS_H */
