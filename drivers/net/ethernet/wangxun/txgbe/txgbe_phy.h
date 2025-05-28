/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_PHY_H_
#define _TXGBE_PHY_H_

irqreturn_t txgbe_link_irq_handler(int irq, void *data);
int txgbe_init_phy(struct txgbe *txgbe);
void txgbe_remove_phy(struct txgbe *txgbe);

#endif /* _TXGBE_PHY_H_ */
