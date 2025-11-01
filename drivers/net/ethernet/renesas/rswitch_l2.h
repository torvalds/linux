/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#ifndef __RSWITCH_L2_H__
#define __RSWITCH_L2_H__

void rswitch_update_l2_offload(struct rswitch_private *priv);

int rswitch_register_notifiers(void);
void rswitch_unregister_notifiers(void);

#endif	/* #ifndef __RSWITCH_L2_H__ */
