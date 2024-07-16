/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * bdc_ep.h - header for the BDC debug functions
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * Author: Ashwini Pahuja
 */
#ifndef __LINUX_BDC_EP_H__
#define __LINUX_BDC_EP_H__

int bdc_init_ep(struct bdc *bdc);
int bdc_ep_disable(struct bdc_ep *ep);
int bdc_ep_enable(struct bdc_ep *ep);
void bdc_free_ep(struct bdc *bdc);

#endif /* __LINUX_BDC_EP_H__ */
