/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_FDIR_H_
#define _TXGBE_FDIR_H_

void txgbe_atr(struct wx_ring *ring, struct wx_tx_buffer *first, u8 ptype);
void txgbe_configure_fdir(struct wx *wx);

#endif /* _TXGBE_FDIR_H_ */
