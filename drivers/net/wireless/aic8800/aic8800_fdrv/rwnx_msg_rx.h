/**
 ****************************************************************************************
 *
 * @file rwnx_msg_rx.h
 *
 * @brief RX function declarations
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ****************************************************************************************
 */

#ifndef _RWNX_MSG_RX_H_
#define _RWNX_MSG_RX_H_

void rwnx_rx_handle_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
void rwnx_rx_handle_print(struct rwnx_hw *rwnx_hw, u8 *msg, u32 len);

#endif /* _RWNX_MSG_RX_H_ */
