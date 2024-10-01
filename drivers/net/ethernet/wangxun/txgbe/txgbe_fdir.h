/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_FDIR_H_
#define _TXGBE_FDIR_H_

void txgbe_atr_compute_perfect_hash(union txgbe_atr_input *input,
				    union txgbe_atr_input *input_mask);
void txgbe_atr(struct wx_ring *ring, struct wx_tx_buffer *first, u8 ptype);
int txgbe_fdir_set_input_mask(struct wx *wx, union txgbe_atr_input *input_mask);
int txgbe_fdir_write_perfect_filter(struct wx *wx,
				    union txgbe_atr_input *input,
				    u16 soft_id, u8 queue);
int txgbe_fdir_erase_perfect_filter(struct wx *wx,
				    union txgbe_atr_input *input,
				    u16 soft_id);
void txgbe_configure_fdir(struct wx *wx);
void txgbe_fdir_filter_exit(struct wx *wx);

#endif /* _TXGBE_FDIR_H_ */
