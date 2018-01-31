/*
 * Driver for Rockchip TSP  Controller
 *
 * Copyright (C) 2012-2016 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _RK_TSP_H
#define _RK_TSP_H

#include <linux/types.h>

#define _SBF(s, v)		((v) << (s))

#define TSP_GCFG		0x0000
#define TSOUT_ON		BIT(3)
#define PVR_ON			BIT(2)
#define PTI1_ON			BIT(1)
#define PTI0_ON			BIT(0)

#define PVR_CTRL		0x0004
#define PVR_BURST_INCR4		_SBF(4, 0x00)
#define PVR_BURST_INCR8		_SBF(4, 0x01)
#define PVR_BURST_INCR16	_SBF(4, 0x02)
#define PVR_SOURCE_NO_PID_FILTER_PTI0	_SBF(2, 0x00)
#define PVR_SOURCE_PID_FILTER_PTI0	_SBF(2, 0x01)
#define PVR_SOURCE_NO_PID_FILTER_PTI1	_SBF(2, 0x02)
#define PVR_SOURCE_PID_FILTER_PTI1	_SBF(2, 0x03)
#define PVR_STOP		BIT(1)
#define PVR_START		BIT(0)

#define PVR_LEN			0x0008
#define PVR_ADDR		0x000c
#define PVR_INT_STS		0x0010
#define PVR_INT_ENA		0x0014
#define TSOUT_CTRL		0x0018

#define PVR_TOP_ADDR		0x001c
#define PVR_WRITE_ADDR		0x0020

#define PTI0_CTRL		0x0100
#define TS_ERROR_NOT_OUTPUT		_SBF(19, 0x00)
#define TS_ERROR_SET_INDICATOR		_SBF(19, 0x01)
#define TS_ERROR_NOT_CARE		_SBF(19, 0x02)
#define TS_CLK_PHASE_SEL		BIT(18)
#define DEMUX_BURST_INCR4		_SBF(16, 0x00)
#define DEMUX_BURST_INCR8		_SBF(16, 0x01)
#define DEMUX_BURST_INCR16		_SBF(16, 0x02)
#define SYNC_BYPASS			BIT(15)
#define CW_BYTEORDER			BIT(14)
#define CM_ON				BIT(13)
#define SERIAL_SYNC_VALID_MODE		_SBF(11, 0x00)
#define PARALLEL_SYNC_VALID_MODE	_SBF(11, 0x01)
#define PARALLEL_SYNC_BURST_MODE	_SBF(11, 0x02)
#define PARALLEL_NOSYNC_VALID_MODE	_SBF(11, 0x03)
#define TSI_BIT_ORDER		BIT(10)
/* 0: memory, 1: hsadc */
#define TSI_SEL			BIT(9)
#define OUT_BYTESWAP		BIT(8)
#define IN_BYTESWAP		BIT(7)
#define SOFT_CLEAR		BIT(0)

#define PTI0_LLP_CFG		0x0104
#define PTI0_LLP_BASE		0x0108
#define PTI0_LLP_WRITE		0x010c
#define PTI0_LLP_READ		0x0110
#define PTI0_PID_STS0		0x0114
#define PTI0_PID_STS1		0x0118
#define PTI0_PID_STS2		0x011c
#define PTI0_PID_STS3		0x0120
#define PTI0_PID_INT_ENA0	0x0124
#define PTI0_PID_INT_ENA1	0x0128
#define PTI0_PID_INT_ENA2	0x012c
#define PTI0_PID_INT_ENA3	0x0130
#define PTI0_PCR_INT_STS	0x0134
#define PTI0_PCR_INT_ENA	0x0138
#define PTI0_PCR0_CTRL		0x013c
#define PTI0_PCR0_H		0x015c
#define PTI0_PCR0_L		0x0160
#define PTI0_DMA_STS		0x019c
#define PTI0_DMA_ENA		0x01a0
#define PTI0_DATA_FLAG0		0x01a4
#define PTI0_DATA_FLAG1		0x01a8
#define PTI0_LIST_FLAG		0x01ac
#define PTI0_DST_STS0		0x01b0
#define PTI0_DST_STS1		0x01b4
#define PTI0_DST_ENA0		0x01b8
#define PTI0_DST_ENA1		0x01bc
#define PTI0_ECW0_H		0x0200
#define PTI0_ECW0_L		0x0204
#define PTI0_OCW0_H		0x0208
#define PTI0_OCW0_L		0x020c
#define PTI0_PID0_CTRL		0x0300
#define PTI0_PID0_BASE		0x0400
#define PTI0_PID0_TOP		0x0404
#define PTI0_PID0_WRITE		0x0408
#define PTI0_PID0_READ		0x040c
#define PTI0_LIST0_BASE		0x0800
#define PTI0_LIST0_TOP		0x0804
#define PTI0_LIST0_WRITE	0x0808
#define PTI0_LIST0_READ		0x080c
#define PTI0_PID0_CFG		0x0900
#define PTI0_PID0_FILT_0	0x0904
#define PTI0_PID0_FILT_1	0x0908
#define PTI0_PID0_FILT_2	0x090c
#define PTI0_PID0_FILT_3	0x0910

#define MMU_DTE_ADDR		0x08800
#define MMU_STATUS		0x08804
#define MMU_COMMAND		0x08808
#define MMU_PAGE_FAULT_ADDR	0x0880c
#define MMU_ZAP_ONE_LINE	0x08810
#define MMU_INT_RAWSTAT		0x08814
#define MMU_INT_CLEAR		0x08818
#define MMU_INT_MASK		0x0881c
#define MMU_INT_STATUS		0x08820
#define MMU_AUTO_GATING		0x08824
#define MMU_MISS_CNT		0x08828
#define MMU_BURST_CNT		0x0882c

#define GRF_REG_FIELD(reg, lsb, msb)    ((reg << 16) | (lsb << 8) | (msb))

enum soc_type {
	RK312X,
	RK3228,
	RK3328,
};

enum grf_fields {
	TSP_IO_GROUP_SEL,
	TSP_VALID,
	TSP_FAIL,
	TSP_CLK,
	TSP_SYNCM0,
	TSP_D0,
	TSP_D1,
	TSP_D2,
	TSP_D3,
	TSP_D4,
	TSP_D5M0,
	TSP_D6M0,
	TSP_D7M0,
	TSP_SYNCM1,
	TSP_D5M1,
	TSP_D6M1,
	TSP_D7M1,
	MAX_FIELDS,
};

enum tsp_filter_type {
	TSP_SECTION_FILTER = 1,
	TSP_PES_FILTER = 2,
	TSP_ES_FILTER = 4,
	TSP_TS_FILTER = 8,
};

struct rockchip_tsp_plat_data {
	const u32 *grf_reg_fields;
	enum soc_type soc_type;
};

struct tsp_ctx {
	int pid;
	int index;
	uint filter_type; /*bit 0~3: section, pes, es, ts*/
	u8 *base;
	u8 *top;
	u8 *write;
	u8 *read;
	u8 *buf;
	u32 buf_len;
	dma_addr_t dma_buf;
	u8 filter_byte[TSP_DMX_FILTER_SIZE];
	u8 filter_mask[TSP_DMX_FILTER_SIZE];
	void (*get_data_callback)(const u8 *buf, size_t count, u16 pid);
	struct list_head pid_list;
};

struct tsp_dev {
	struct device *dev;
	void __iomem *ioaddr;
	struct regmap *grf;
	struct clk *tsp_clk;
	struct clk *tsp_aclk;
	struct clk *tsp_hclk;
	int tsp_irq;
	int serial_parallel_mode;
	struct list_head pid_list;
	/* lock for list operate */
	spinlock_t list_lock;

	int is_open;

	/* timer */
	struct timer_list timer;

	/* ts workque */
	struct work_struct ts_work;
	struct workqueue_struct *ts_queue;
	/* mutex for feed buf to dvb-core */
	struct mutex ts_mutex;

	/* section workque */
	struct work_struct sec_work;
	struct workqueue_struct *sec_queue;

	int tsp_start_descram;

	struct rockchip_tsp_channel_info channel_info[64];
	unsigned long channel_release_timeout[64];
	const struct rockchip_tsp_plat_data *pdata;
};

#define TSP_CMD_IO_OPEN          _IO('o', 120)
#define TSP_CMD_IO_CLOSE         _IO('o', 121)
#define TSP_CMD_SET_PCR_PID      _IOW('o', 122, ca_descr_t)
#define TSP_CMD_GET_PCR_VAL      _IOR('o', 123, ca_descr_t)
#define TSP_CMD_SET_DESCAM_PID   _IOW('o', 124, ca_descr_t)
#define TSP_CMD_SET_LIVE_STATUS  _IOW('o', 125, ca_descr_t)
#define TSP_CMD_RESET_REGS       _IOW('o', 126, ca_descr_t)

#endif
