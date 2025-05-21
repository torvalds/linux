/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_AML_H_
#define _TXGBE_AML_H_

void txgbe_gpio_init_aml(struct wx *wx);
irqreturn_t txgbe_gpio_irq_handler_aml(int irq, void *data);
int txgbe_test_hostif(struct wx *wx);
int txgbe_set_phy_link(struct wx *wx);
int txgbe_identify_sfp(struct wx *wx);
void txgbe_setup_link(struct wx *wx);
int txgbe_phylink_init_aml(struct txgbe *txgbe);

#endif /* _TXGBE_AML_H_ */
