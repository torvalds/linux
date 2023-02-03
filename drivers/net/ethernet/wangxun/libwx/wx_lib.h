/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WangXun Gigabit PCI Express Linux driver
 * Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd.
 */

#ifndef _WX_LIB_H_
#define _WX_LIB_H_

void wx_reset_interrupt_capability(struct wx *wx);
void wx_clear_interrupt_scheme(struct wx *wx);
int wx_init_interrupt_scheme(struct wx *wx);
irqreturn_t wx_msix_clean_rings(int __always_unused irq, void *data);
void wx_free_irq(struct wx *wx);
int wx_setup_isb_resources(struct wx *wx);
void wx_free_isb_resources(struct wx *wx);
u32 wx_misc_isb(struct wx *wx, enum wx_isb_idx idx);
void wx_configure_vectors(struct wx *wx);

#endif /* _NGBE_LIB_H_ */
