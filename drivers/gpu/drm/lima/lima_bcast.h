/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2018-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_BCAST_H__
#define __LIMA_BCAST_H__

struct lima_ip;

int lima_bcast_init(struct lima_ip *ip);
void lima_bcast_fini(struct lima_ip *ip);

void lima_bcast_enable(struct lima_device *dev, int num_pp);

#endif
