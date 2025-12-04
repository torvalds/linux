/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_MBX_H
#define _RNPGBE_MBX_H

#include "rnpgbe.h"

#define MUCSE_MBX_FW2PF_CNT       0
#define MUCSE_MBX_PF2FW_CNT       4
#define MUCSE_MBX_FWPF_SHM        8
#define MUCSE_MBX_PF2FW_CTRL(mbx) ((mbx)->pf2fw_mbx_ctrl)
#define MUCSE_MBX_FWPF_MASK(mbx)  ((mbx)->fwpf_mbx_mask)
#define MUCSE_MBX_REQ             BIT(0) /* Request a req to mailbox */
#define MUCSE_MBX_PFU             BIT(3) /* PF owns the mailbox buffer */

int mucse_write_and_wait_ack_mbx(struct mucse_hw *hw, u32 *msg, u16 size);
void mucse_init_mbx_params_pf(struct mucse_hw *hw);
int mucse_poll_and_read_mbx(struct mucse_hw *hw, u32 *msg, u16 size);
#endif /* _RNPGBE_MBX_H */
