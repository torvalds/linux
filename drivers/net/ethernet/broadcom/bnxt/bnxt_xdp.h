/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_XDP_H
#define BNXT_XDP_H

void bnxt_tx_int_xdp(struct bnxt *bp, struct bnxt_napi *bnapi, int nr_pkts);
bool bnxt_rx_xdp(struct bnxt *bp, struct bnxt_rx_ring_info *rxr, u16 cons,
		 struct page *page, u8 **data_ptr, unsigned int *len,
		 u8 *event);
int bnxt_xdp(struct net_device *dev, struct netdev_xdp *xdp);

#endif
