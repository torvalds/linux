/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_L2_CACHE_H__
#define __LIMA_L2_CACHE_H__

struct lima_ip;

int lima_l2_cache_init(struct lima_ip *ip);
void lima_l2_cache_fini(struct lima_ip *ip);

int lima_l2_cache_flush(struct lima_ip *ip);

#endif
