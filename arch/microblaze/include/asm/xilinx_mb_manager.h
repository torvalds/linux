/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Xilinx, Inc.
 */
#ifndef _XILINX_MB_MANAGER_H
#define _XILINX_MB_MANAGER_H

#include <linux/of_address.h>

/*
 * When the break vector gets asserted because of error injection, the break
 * signal must be blocked before exiting from the break handler, Below api
 * updates the manager address and control register and error counter callback
 * arguments, which will be used by the break handler to block the break and
 * call the callback function.
 */
void xmb_manager_register(uintptr_t phys_baseaddr, u32 cr_val,
			  void (*callback)(void *data),
			  void *priv, void (*reset_callback)(void *data));

#endif /* _XILINX_MB_MANAGER_H */
