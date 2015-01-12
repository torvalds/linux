/*
 * bdc_ep.h - header for the BDC debug functions
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * Author: Ashwini Pahuja
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */
#ifndef __LINUX_BDC_EP_H__
#define __LINUX_BDC_EP_H__

int bdc_init_ep(struct bdc *);
int bdc_ep_disable(struct bdc_ep *);
int bdc_ep_enable(struct bdc_ep *);
void bdc_free_ep(struct bdc *);

#endif /* __LINUX_BDC_EP_H__ */
