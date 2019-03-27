/*
 * Copyright (c) 2005-2014 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#if !defined(CMA_H)
#define CMA_H

#include <config.h>

#include <stdlib.h>
#include <errno.h>
#include <infiniband/endian.h>
#include <semaphore.h>
#include <stdatomic.h>

#include <rdma/rdma_cma.h>
#include <infiniband/ib.h>

#define PFX "librdmacm: "

/*
 * Fast synchronization for low contention locking.
 */
typedef struct {
	sem_t sem;
	_Atomic(int) cnt;
} fastlock_t;
static inline void fastlock_init(fastlock_t *lock)
{
	sem_init(&lock->sem, 0, 0);
	atomic_store(&lock->cnt, 0);
}
static inline void fastlock_destroy(fastlock_t *lock)
{
	sem_destroy(&lock->sem);
}
static inline void fastlock_acquire(fastlock_t *lock)
{
	if (atomic_fetch_add(&lock->cnt, 1) > 0)
		sem_wait(&lock->sem);
}
static inline void fastlock_release(fastlock_t *lock)
{
	if (atomic_fetch_sub(&lock->cnt, 1) > 1)
		sem_post(&lock->sem);
}

__be16 ucma_get_port(struct sockaddr *addr);
int ucma_addrlen(struct sockaddr *addr);
void ucma_set_sid(enum rdma_port_space ps, struct sockaddr *addr,
		  struct sockaddr_ib *sib);
int ucma_max_qpsize(struct rdma_cm_id *id);
int ucma_complete(struct rdma_cm_id *id);
int ucma_shutdown(struct rdma_cm_id *id);

static inline int ERR(int err)
{
	errno = err;
	return -1;
}

int ucma_init(void);
extern int af_ib_support;

#define RAI_ROUTEONLY		0x01000000

void ucma_ib_init(void);
void ucma_ib_cleanup(void);
void ucma_ib_resolve(struct rdma_addrinfo **rai,
		     const struct rdma_addrinfo *hints);

struct ib_connect_hdr {
	uint8_t  cma_version;
	uint8_t  ip_version; /* IP version: 7:4 */
	uint16_t port;
	uint32_t src_addr[4];
	uint32_t dst_addr[4];
#define cma_src_ip4 src_addr[3]
#define cma_src_ip6 src_addr[0]
#define cma_dst_ip4 dst_addr[3]
#define cma_dst_ip6 dst_addr[0]
};

#endif /* CMA_H */
