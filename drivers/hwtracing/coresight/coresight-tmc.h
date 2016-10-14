/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CORESIGHT_TMC_H
#define _CORESIGHT_TMC_H

#include <linux/miscdevice.h>

#define TMC_RSZ			0x004
#define TMC_STS			0x00c
#define TMC_RRD			0x010
#define TMC_RRP			0x014
#define TMC_RWP			0x018
#define TMC_TRG			0x01c
#define TMC_CTL			0x020
#define TMC_RWD			0x024
#define TMC_MODE		0x028
#define TMC_LBUFLEVEL		0x02c
#define TMC_CBUFLEVEL		0x030
#define TMC_BUFWM		0x034
#define TMC_RRPHI		0x038
#define TMC_RWPHI		0x03c
#define TMC_AXICTL		0x110
#define TMC_DBALO		0x118
#define TMC_DBAHI		0x11c
#define TMC_FFSR		0x300
#define TMC_FFCR		0x304
#define TMC_PSCR		0x308
#define TMC_ITMISCOP0		0xee0
#define TMC_ITTRFLIN		0xee8
#define TMC_ITATBDATA0		0xeec
#define TMC_ITATBCTR2		0xef0
#define TMC_ITATBCTR1		0xef4
#define TMC_ITATBCTR0		0xef8

/* register description */
/* TMC_CTL - 0x020 */
#define TMC_CTL_CAPT_EN		BIT(0)
/* TMC_STS - 0x00C */
#define TMC_STS_TMCREADY_BIT	2
#define TMC_STS_FULL		BIT(0)
#define TMC_STS_TRIGGERED	BIT(1)
/* TMC_AXICTL - 0x110 */
#define TMC_AXICTL_PROT_CTL_B0	BIT(0)
#define TMC_AXICTL_PROT_CTL_B1	BIT(1)
#define TMC_AXICTL_SCT_GAT_MODE	BIT(7)
#define TMC_AXICTL_WR_BURST_16	0xF00
/* TMC_FFCR - 0x304 */
#define TMC_FFCR_FLUSHMAN_BIT	6
#define TMC_FFCR_EN_FMT		BIT(0)
#define TMC_FFCR_EN_TI		BIT(1)
#define TMC_FFCR_FON_FLIN	BIT(4)
#define TMC_FFCR_FON_TRIG_EVT	BIT(5)
#define TMC_FFCR_TRIGON_TRIGIN	BIT(8)
#define TMC_FFCR_STOP_ON_FLUSH	BIT(12)


enum tmc_config_type {
	TMC_CONFIG_TYPE_ETB,
	TMC_CONFIG_TYPE_ETR,
	TMC_CONFIG_TYPE_ETF,
};

enum tmc_mode {
	TMC_MODE_CIRCULAR_BUFFER,
	TMC_MODE_SOFTWARE_FIFO,
	TMC_MODE_HARDWARE_FIFO,
};

enum tmc_mem_intf_width {
	TMC_MEM_INTF_WIDTH_32BITS	= 1,
	TMC_MEM_INTF_WIDTH_64BITS	= 2,
	TMC_MEM_INTF_WIDTH_128BITS	= 4,
	TMC_MEM_INTF_WIDTH_256BITS	= 8,
};

/**
 * struct tmc_drvdata - specifics associated to an TMC component
 * @base:	memory mapped base address for this component.
 * @dev:	the device entity associated to this component.
 * @csdev:	component vitals needed by the framework.
 * @miscdev:	specifics to handle "/dev/xyz.tmc" entry.
 * @spinlock:	only one at a time pls.
 * @buf:	area of memory where trace data get sent.
 * @paddr:	DMA start location in RAM.
 * @vaddr:	virtual representation of @paddr.
 * @size:	@buf size.
 * @mode:	how this TMC is being used.
 * @config_type: TMC variant, must be of type @tmc_config_type.
 * @memwidth:	width of the memory interface databus, in bytes.
 * @trigger_cntr: amount of words to store after a trigger.
 */
struct tmc_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	struct miscdevice	miscdev;
	spinlock_t		spinlock;
	bool			reading;
	char			*buf;
	dma_addr_t		paddr;
	void __iomem		*vaddr;
	u32			size;
	local_t			mode;
	enum tmc_config_type	config_type;
	enum tmc_mem_intf_width	memwidth;
	u32			trigger_cntr;
};

/* Generic functions */
void tmc_wait_for_tmcready(struct tmc_drvdata *drvdata);
void tmc_flush_and_stop(struct tmc_drvdata *drvdata);
void tmc_enable_hw(struct tmc_drvdata *drvdata);
void tmc_disable_hw(struct tmc_drvdata *drvdata);

/* ETB/ETF functions */
int tmc_read_prepare_etb(struct tmc_drvdata *drvdata);
int tmc_read_unprepare_etb(struct tmc_drvdata *drvdata);
extern const struct coresight_ops tmc_etb_cs_ops;
extern const struct coresight_ops tmc_etf_cs_ops;

/* ETR functions */
int tmc_read_prepare_etr(struct tmc_drvdata *drvdata);
int tmc_read_unprepare_etr(struct tmc_drvdata *drvdata);
extern const struct coresight_ops tmc_etr_cs_ops;
#endif
