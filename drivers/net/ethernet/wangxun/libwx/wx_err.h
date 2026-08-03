/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WangXun Gigabit PCI Express Linux driver
 * Copyright (c) 2015 - 2026 Beijing WangXun Technology Co., Ltd.
 */

#ifndef _WX_ERR_H_
#define _WX_ERR_H_

extern const struct pci_error_handlers wx_err_handler;

void wx_check_err_subtask(struct wx *wx);
int wx_init_err_task(struct wx *wx);
void wx_check_hang_subtask(struct wx *wx);
void wx_tx_timeout(struct net_device *netdev, unsigned int txqueue);
void wx_handle_tx_hang(struct wx_ring *tx_ring, unsigned int next);

#endif /* _WX_ERR_H_ */
