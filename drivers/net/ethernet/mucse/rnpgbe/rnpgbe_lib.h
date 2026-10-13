/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_LIB_H
#define _RNPGBE_LIB_H

#include <linux/bits.h>

struct mucse;
struct mucse_hw;

#define RING_OFFSET(n)            (0x1000 + 0x100 * (n))
#define RNPGBE_DMA_INT_MASK       0x24
#define TX_INT_MASK               BIT(1)
#define RX_INT_MASK               BIT(0)
#define INT_VALID                 (BIT(16) | BIT(17))
#define RNPGBE_DMA_INT_TRIG       0x2c /* lost-interrupt recovery trigger */
/* |  31:24   | .... |    15:8   |    7:0    | */
/* |  pfvfnum |      | tx vector | rx vector | */
#define RING_VECTOR(n)            (0x04 * (n))

#define mucse_for_each_ring(pos, head)\
	for (typeof((head).ring) __pos = (head).ring;\
	     __pos ? ({ pos = __pos; 1; }) : 0;\
	     __pos = __pos->next)

int rnpgbe_init_interrupt_scheme(struct mucse *mucse);
void rnpgbe_clear_interrupt_scheme(struct mucse *mucse);
int rnpgbe_request_mbx_irq(struct mucse *mucse);
void rnpgbe_free_mbx_irq(struct mucse *mucse);
int rnpgbe_request_irq(struct mucse *mucse);
void rnpgbe_free_irq(struct mucse *mucse);
void rnpgbe_down(struct mucse *mucse);
void rnpgbe_up_complete(struct mucse *mucse);
#endif
