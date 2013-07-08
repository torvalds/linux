/*
 * Rockchip MMC Interface driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _RKMMC_H_
#define _RKMMC_H_

#include <linux/bitops.h>

#if 0
#define mmc_dbg(host, format, arg...)            \
	        dev_printk(KERN_DEBUG , host->dev , format , ## arg)
#else
#define mmc_dbg(host, format, arg...)
#endif

#define mmc_info(host, format, arg...)            \
	        dev_printk(KERN_INFO , host->dev , format , ## arg)
#define mmc_err(host, format, arg...)            \
	        dev_printk(KERN_ERR , host->dev , format , ## arg)

#define mmc_writel(host, reg, val)	writel_relaxed(val, host->regs + MMC_##reg)
#define mmc_readl(host, reg)		readl_relaxed(host->regs + MMC_##reg)


#define	MMC_CTRL	0x00
#define MMC_PWREN	0X04
#define MMC_CLKDIV	0x08
#define MMC_CLKSRC	0x0c
#define MMC_CLKENA	0x10
#define MMC_TMOUT	0x14
#define MMC_CTYPE	0x18
#define MMC_BLKSIZ	0x1c
#define MMC_BYTCNT	0x20
#define MMC_INTMASK	0x24
#define MMC_CMDARG	0x28
#define MMC_CMD	0x2c
#define MMC_RESP0	0x30
#define MMC_RESP1	0X34
#define MMC_RESP2	0x38
#define MMC_RESP3	0x3c
#define MMC_MINTSTS	0x40
#define MMC_RINTSTS	0x44
#define MMC_STATUS	0x48
#define MMC_FIFOTH	0x4c
#define MMC_CDETECT	0x50
#define MMC_WRTPRT	0x54
#define MMC_TCBCNT	0x5c
#define MMC_TBBCNT	0x60
#define MMC_DEBNCE	0x64
#define MMC_USRID	0x68
#define MMC_VERID	0x6c
#define MMC_UHS_REG	0X74
#define MMC_RST_N	0x78

#define MMC_FIFO_BASE	0x200
#define MMC_DATA	MMC_FIFO_BASE
/* Control register defines */
#define MMC_CTRL_ABORT_READ_DATA	BIT(8)
#define MMC_CTRL_SEND_IRQ_RESPONSE	BIT(7)
#define MMC_CTRL_READ_WAIT		BIT(6)
#define MMC_CTRL_DMA_ENABLE		BIT(5)
#define MMC_CTRL_INT_ENABLE		BIT(4)
#define MMC_CTRL_DMA_RESET		BIT(2)
#define MMC_CTRL_FIFO_RESET		BIT(1)
#define MMC_CTRL_RESET			BIT(0)
/* Hardware reset register defines */
#define MMC_CARD_RESET			BIT(0)
/* Power enable register defines */
#define MMC_PWREN_ON			BIT(0)
/* Clock Enable register defines */
#define MMC_CLKEN_LOW_PWR             	BIT(16)
#define MMC_CLKEN_ENABLE              	BIT(0)
/* time-out register defines */
#define MMC_TMOUT_DATA(n)             	_SBF(8, (n))
#define MMC_TMOUT_DATA_MSK            	0xFFFFFF00
#define MMC_TMOUT_RESP(n)             	((n) & 0xFF)
#define MMC_TMOUT_RESP_MSK            	0xFF
/* card-type register defines */
#define MMC_CTYPE_8BIT                	BIT(16)
#define MMC_CTYPE_4BIT                	BIT(0)
#define MMC_CTYPE_1BIT                	0
/* Interrupt status & mask register defines */
#define MMC_INT_SDIO                  	BIT(16)
#define MMC_INT_EBE                   	BIT(15)
#define MMC_INT_ACD                   	BIT(14)
#define MMC_INT_SBE                   	BIT(13)
#define MMC_INT_HLE                   	BIT(12)
#define MMC_INT_FRUN                  	BIT(11)
#define MMC_INT_HTO                   	BIT(10)
#define MMC_INT_DTO                   	BIT(9)
#define MMC_INT_RTO                   	BIT(8)
#define MMC_INT_DCRC                  	BIT(7)
#define MMC_INT_RCRC                  	BIT(6)
#define MMC_INT_RXDR                  	BIT(5)
#define MMC_INT_TXDR                  	BIT(4)
#define MMC_INT_DATA_OVER             	BIT(3)
#define MMC_INT_CMD_DONE              	BIT(2)
#define MMC_INT_RESP_ERR              	BIT(1)
#define MMC_INT_CD                    	BIT(0)
#define MMC_INT_ERROR                 	0xbfc2
/* Command register defines */
#define MMC_CMD_START                 	BIT(31)
#define MMC_USE_HOLD_REG		BIT(29)
#define MMC_CMD_CCS_EXP               	BIT(23)
#define MMC_CMD_CEATA_RD              	BIT(22)
#define MMC_CMD_UPD_CLK               	BIT(21)
#define MMC_CMD_INIT                  	BIT(15)
#define MMC_CMD_STOP                  	BIT(14)
#define MMC_CMD_PRV_DAT_WAIT          	BIT(13)
#define MMC_CMD_SEND_STOP             	BIT(12)
#define MMC_CMD_STRM_MODE             	BIT(11)
#define MMC_CMD_DAT_WR                	BIT(10)
#define MMC_CMD_DAT_EXP               	BIT(9)
#define MMC_CMD_RESP_CRC              	BIT(8)
#define MMC_CMD_RESP_LONG		BIT(7)
#define MMC_CMD_RESP_EXP		BIT(6)
#define MMC_CMD_INDX(n)		((n) & 0x1F)
/* Status register defines */
#define MMC_GET_FCNT(x)		(((x)>>17) & 0x1FF)
#define MMC_MC_BUSY			BIT(10)
#define MMC_DATA_BUSY			BIT(9)
/* FIFO threshold register defines */
#define FIFO_DETH			256

/* UHS-1 register defines */
#define MMC_UHS_DDR_MODE		BIT(16)
#define MMC_UHS_VOLT_18			BIT(0)


/* Common flag combinations */
#define MMC_DATA_ERROR_FLAGS (MMC_INT_DTO | MMC_INT_DCRC | \
		                                 MMC_INT_HTO | MMC_INT_SBE  | \
		                                 MMC_INT_EBE)
#define MMC_CMD_ERROR_FLAGS  (MMC_INT_RTO | MMC_INT_RCRC | \
		                                 MMC_INT_RESP_ERR)
#define MMC_ERROR_FLAGS      (MMC_DATA_ERROR_FLAGS | \
		                                 MMC_CMD_ERROR_FLAGS  | MMC_INT_HLE)

#define	MMC_DMA_THRESHOLD    	(16)

#define MMC_BUS_CLOCK		96000000
enum rk_mmc_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_SENDING_DATA,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
	STATE_DATA_ERROR,
};

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_XFER_COMPLETE,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
	EVENT_XFER_ERROR,
};
struct mmc_data;

struct rk_mmc{
	struct device 		*dev;

	struct tasklet_struct   tasklet;

	spinlock_t              lock;
	void __iomem            *regs;
	int 			irq;

	struct scatterlist      *sg;
	unsigned int            pio_offset;
	
	struct mmc_command 	stop;
	int			stop_ex;

	struct mmc_host 	*mmc;
	struct mmc_request      *mrq;
	struct mmc_command      *cmd;
	struct mmc_data         *data;

	int			use_dma;
	u32			dma_xfer_size;
	dma_addr_t              sg_dma;
	unsigned long		dma_addr;
	struct rk_mmc_dma_ops	*ops;

	u32                     cmd_status;
	u32                     data_status;
	u32                     stop_cmdr;
	u32			ctype;

	u32			shutdown;
#define MMC_RECV_DATA	0
#define MMC_SEND_DATA	1
	u32                     dir_status;

	u32			curr_clock;
	u32			bus_hz;
	struct clk              *clk;

	enum rk_mmc_state	state;
	unsigned long           pending_events;
	unsigned long           completed_events;

	u32			bus_test;
#define MMC_NEED_INIT		1
	unsigned long		flags;
};

struct rk_mmc_dma_ops {
	int (*init)(struct rk_mmc *host);
	int (*start)(struct rk_mmc *host);
	int (*stop)(struct rk_mmc *host);
	void (*exit)(struct rk_mmc *host);
};
#endif
