/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NETLOGIC_MULTI_NODE_H_
#define _NETLOGIC_MULTI_NODE_H_

#ifndef CONFIG_NLM_MULTINODE
#define NLM_NR_NODES		1
#else
#if defined(CONFIG_NLM_MULTINODE_2)
#define NLM_NR_NODES		2
#elif defined(CONFIG_NLM_MULTINODE_4)
#define NLM_NR_NODES		4
#else
#define NLM_NR_NODES		1
#endif
#endif

#define NLM_THREADS_PER_CORE	4
#ifdef CONFIG_CPU_XLR
#define nlm_cores_per_node()	8
#else
extern unsigned int xlp_cores_per_node;
#define nlm_cores_per_node()	xlp_cores_per_node
#endif

#define nlm_threads_per_node()	(nlm_cores_per_node() * NLM_THREADS_PER_CORE)
#define nlm_cpuid_to_node(c)	((c) / nlm_threads_per_node())

struct nlm_soc_info {
	unsigned long	coremask;	/* cores enabled on the soc */
	unsigned long	ebase;		/* not used now */
	uint64_t	irqmask;	/* EIMR for the node */
	uint64_t	sysbase;	/* only for XLP - sys block base */
	uint64_t	picbase;	/* PIC block base */
	spinlock_t	piclock;	/* lock for PIC access */
	cpumask_t	cpumask;	/* logical cpu mask for node */
	unsigned int	socbus;
};

extern struct nlm_soc_info nlm_nodes[NLM_NR_NODES];
#define nlm_get_node(i)		(&nlm_nodes[i])
#define nlm_node_present(n)	((n) >= 0 && (n) < NLM_NR_NODES && \
					nlm_get_node(n)->coremask != 0)
#ifdef CONFIG_CPU_XLR
#define nlm_current_node()	(&nlm_nodes[0])
#else
#define nlm_current_node()	(&nlm_nodes[nlm_nodeid()])
#endif
void nlm_node_init(int node);

#endif
