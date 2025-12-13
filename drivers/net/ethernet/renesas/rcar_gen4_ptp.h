/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas R-Car Gen4 gPTP device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RCAR_GEN4_PTP_H__
#define __RCAR_GEN4_PTP_H__

#include <linux/ptp_clock_kernel.h>

#define RCAR_GEN4_GPTP_OFFSET_S4	0x00018000

/* driver's definitions */
#define RCAR_GEN4_RXTSTAMP_ENABLED		BIT(0)
#define RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT	BIT(1)
#define RCAR_GEN4_RXTSTAMP_TYPE_ALL		(RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT | BIT(2))
#define RCAR_GEN4_RXTSTAMP_TYPE			RCAR_GEN4_RXTSTAMP_TYPE_ALL

#define RCAR_GEN4_TXTSTAMP_ENABLED		BIT(0)


struct rcar_gen4_ptp_private {
	void __iomem *addr;
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	spinlock_t lock;	/* For multiple registers access */
	u32 tstamp_tx_ctrl;
	u32 tstamp_rx_ctrl;
	s64 default_addend;
	bool initialized;
};

int rcar_gen4_ptp_register(struct rcar_gen4_ptp_private *ptp_priv, u32 rate);
int rcar_gen4_ptp_unregister(struct rcar_gen4_ptp_private *ptp_priv);
struct rcar_gen4_ptp_private *rcar_gen4_ptp_alloc(struct platform_device *pdev);

#endif	/* #ifndef __RCAR_GEN4_PTP_H__ */
