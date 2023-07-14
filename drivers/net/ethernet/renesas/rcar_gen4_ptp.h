/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas R-Car Gen4 gPTP device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RCAR_GEN4_PTP_H__
#define __RCAR_GEN4_PTP_H__

#include <linux/ptp_clock_kernel.h>

#define PTPTIVC_INIT			0x19000000	/* 320MHz */
#define RCAR_GEN4_PTP_CLOCK_S4		PTPTIVC_INIT
#define RCAR_GEN4_GPTP_OFFSET_S4	0x00018000

/* for rcar_gen4_ptp_init */
enum rcar_gen4_ptp_reg_layout {
	RCAR_GEN4_PTP_REG_LAYOUT_S4
};

/* driver's definitions */
#define RCAR_GEN4_RXTSTAMP_ENABLED		BIT(0)
#define RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT	BIT(1)
#define RCAR_GEN4_RXTSTAMP_TYPE_ALL		(RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT | BIT(2))
#define RCAR_GEN4_RXTSTAMP_TYPE			RCAR_GEN4_RXTSTAMP_TYPE_ALL

#define RCAR_GEN4_TXTSTAMP_ENABLED		BIT(0)

#define PTPRO				0

enum rcar_gen4_ptp_reg_s4 {
	PTPTMEC		= PTPRO + 0x0010,
	PTPTMDC		= PTPRO + 0x0014,
	PTPTIVC0	= PTPRO + 0x0020,
	PTPTOVC00	= PTPRO + 0x0030,
	PTPTOVC10	= PTPRO + 0x0034,
	PTPTOVC20	= PTPRO + 0x0038,
	PTPGPTPTM00	= PTPRO + 0x0050,
	PTPGPTPTM10	= PTPRO + 0x0054,
	PTPGPTPTM20	= PTPRO + 0x0058,
};

struct rcar_gen4_ptp_reg_offset {
	u16 enable;
	u16 disable;
	u16 increment;
	u16 config_t0;
	u16 config_t1;
	u16 config_t2;
	u16 monitor_t0;
	u16 monitor_t1;
	u16 monitor_t2;
};

struct rcar_gen4_ptp_private {
	void __iomem *addr;
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	const struct rcar_gen4_ptp_reg_offset *offs;
	spinlock_t lock;	/* For multiple registers access */
	u32 tstamp_tx_ctrl;
	u32 tstamp_rx_ctrl;
	s64 default_addend;
	bool initialized;
};

int rcar_gen4_ptp_register(struct rcar_gen4_ptp_private *ptp_priv,
			   enum rcar_gen4_ptp_reg_layout layout, u32 clock);
int rcar_gen4_ptp_unregister(struct rcar_gen4_ptp_private *ptp_priv);
struct rcar_gen4_ptp_private *rcar_gen4_ptp_alloc(struct platform_device *pdev);

#endif	/* #ifndef __RCAR_GEN4_PTP_H__ */
