/* Copyright 2008 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "dpaa_sys.h"

#include <soc/fsl/bman.h>

/* Portal processing (interrupt) sources */
#define BM_PIRQ_RCRI	0x00000002	/* RCR Ring (below threshold) */

/* Revision info (for errata and feature handling) */
#define BMAN_REV10 0x0100
#define BMAN_REV20 0x0200
#define BMAN_REV21 0x0201
extern u16 bman_ip_rev;	/* 0 if uninitialised, otherwise BMAN_REVx */

extern struct gen_pool *bm_bpalloc;

struct bm_portal_config {
	/*
	 * Corenet portal addresses;
	 * [0]==cache-enabled, [1]==cache-inhibited.
	 */
	void __iomem *addr_virt[2];
	/* Allow these to be joined in lists */
	struct list_head list;
	struct device *dev;
	/* User-visible portal configuration settings */
	/* portal is affined to this cpu */
	int cpu;
	/* portal interrupt line */
	int irq;
};

struct bman_portal *bman_create_affine_portal(
			const struct bm_portal_config *config);
/*
 * The below bman_p_***() variant might be called in a situation that the cpu
 * which the portal affine to is not online yet.
 * @bman_portal specifies which portal the API will use.
 */
int bman_p_irqsource_add(struct bman_portal *p, u32 bits);

/*
 * Used by all portal interrupt registers except 'inhibit'
 * This mask contains all the "irqsource" bits visible to API users
 */
#define BM_PIRQ_VISIBLE	BM_PIRQ_RCRI

const struct bm_portal_config *
bman_get_bm_portal_config(const struct bman_portal *portal);
