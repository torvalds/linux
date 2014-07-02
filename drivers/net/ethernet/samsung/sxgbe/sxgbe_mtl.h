/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SXGBE_MTL_H__
#define __SXGBE_MTL_H__

#define SXGBE_MTL_OPMODE_ESTMASK	0x3
#define SXGBE_MTL_OPMODE_RAAMASK	0x1
#define SXGBE_MTL_FCMASK		0x7
#define SXGBE_MTL_TX_FIFO_DIV		256
#define SXGBE_MTL_RX_FIFO_DIV		256

#define SXGBE_MTL_RXQ_OP_FEP		BIT(4)
#define SXGBE_MTL_RXQ_OP_FUP		BIT(3)
#define SXGBE_MTL_ENABLE_FC		0x80

#define ETS_WRR				0xFFFFFF9F
#define ETS_RST				0xFFFFFF9F
#define ETS_WFQ				0x00000020
#define ETS_DWRR			0x00000040
#define RAA_SP				0xFFFFFFFB
#define RAA_WSP				0x00000004

#define RX_QUEUE_DYNAMIC		0x80808080
#define RX_FC_ACTIVE			8
#define RX_FC_DEACTIVE			13

enum ttc_control {
	MTL_CONTROL_TTC_64 = 0x00000000,
	MTL_CONTROL_TTC_96 = 0x00000020,
	MTL_CONTROL_TTC_128 = 0x00000030,
	MTL_CONTROL_TTC_192 = 0x00000040,
	MTL_CONTROL_TTC_256 = 0x00000050,
	MTL_CONTROL_TTC_384 = 0x00000060,
	MTL_CONTROL_TTC_512 = 0x00000070,
};

enum rtc_control {
	MTL_CONTROL_RTC_64 = 0x00000000,
	MTL_CONTROL_RTC_96 = 0x00000002,
	MTL_CONTROL_RTC_128 = 0x00000003,
};

enum flow_control_th {
	MTL_FC_FULL_1K = 0x00000000,
	MTL_FC_FULL_2K = 0x00000001,
	MTL_FC_FULL_4K = 0x00000002,
	MTL_FC_FULL_5K = 0x00000003,
	MTL_FC_FULL_6K = 0x00000004,
	MTL_FC_FULL_8K = 0x00000005,
	MTL_FC_FULL_16K = 0x00000006,
	MTL_FC_FULL_24K = 0x00000007,
};

struct sxgbe_mtl_ops {
	void (*mtl_init)(void __iomem *ioaddr, unsigned int etsalg,
			 unsigned int raa);

	void (*mtl_set_txfifosize)(void __iomem *ioaddr, int queue_num,
				   int mtl_fifo);

	void (*mtl_set_rxfifosize)(void __iomem *ioaddr, int queue_num,
				   int queue_fifo);

	void (*mtl_enable_txqueue)(void __iomem *ioaddr, int queue_num);

	void (*mtl_disable_txqueue)(void __iomem *ioaddr, int queue_num);

	void (*set_tx_mtl_mode)(void __iomem *ioaddr, int queue_num,
				int tx_mode);

	void (*set_rx_mtl_mode)(void __iomem *ioaddr, int queue_num,
				int rx_mode);

	void (*mtl_dynamic_dma_rxqueue)(void __iomem *ioaddr);

	void (*mtl_fc_active)(void __iomem *ioaddr, int queue_num,
			      int threshold);

	void (*mtl_fc_deactive)(void __iomem *ioaddr, int queue_num,
				int threshold);

	void (*mtl_fc_enable)(void __iomem *ioaddr, int queue_num);

	void (*mtl_fep_enable)(void __iomem *ioaddr, int queue_num);

	void (*mtl_fep_disable)(void __iomem *ioaddr, int queue_num);

	void (*mtl_fup_enable)(void __iomem *ioaddr, int queue_num);

	void (*mtl_fup_disable)(void __iomem *ioaddr, int queue_num);
};

const struct sxgbe_mtl_ops *sxgbe_get_mtl_ops(void);

#endif /* __SXGBE_MTL_H__ */
