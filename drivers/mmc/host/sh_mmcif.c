/*
 * MMCIF eMMC driver.
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 * Yusuke Goda <yusuke.goda.sx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *
 * TODO
 *  1. DMA
 *  2. Power management
 *  3. Handle MMC errors better
 *
 */

#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/sh_mmcif.h>

#define DRIVER_NAME	"sh_mmcif"
#define DRIVER_VERSION	"2010-04-28"

/* CE_CMD_SET */
#define CMD_MASK		0x3f000000
#define CMD_SET_RTYP_NO		((0 << 23) | (0 << 22))
#define CMD_SET_RTYP_6B		((0 << 23) | (1 << 22)) /* R1/R1b/R3/R4/R5 */
#define CMD_SET_RTYP_17B	((1 << 23) | (0 << 22)) /* R2 */
#define CMD_SET_RBSY		(1 << 21) /* R1b */
#define CMD_SET_CCSEN		(1 << 20)
#define CMD_SET_WDAT		(1 << 19) /* 1: on data, 0: no data */
#define CMD_SET_DWEN		(1 << 18) /* 1: write, 0: read */
#define CMD_SET_CMLTE		(1 << 17) /* 1: multi block trans, 0: single */
#define CMD_SET_CMD12EN		(1 << 16) /* 1: CMD12 auto issue */
#define CMD_SET_RIDXC_INDEX	((0 << 15) | (0 << 14)) /* index check */
#define CMD_SET_RIDXC_BITS	((0 << 15) | (1 << 14)) /* check bits check */
#define CMD_SET_RIDXC_NO	((1 << 15) | (0 << 14)) /* no check */
#define CMD_SET_CRC7C		((0 << 13) | (0 << 12)) /* CRC7 check*/
#define CMD_SET_CRC7C_BITS	((0 << 13) | (1 << 12)) /* check bits check*/
#define CMD_SET_CRC7C_INTERNAL	((1 << 13) | (0 << 12)) /* internal CRC7 check*/
#define CMD_SET_CRC16C		(1 << 10) /* 0: CRC16 check*/
#define CMD_SET_CRCSTE		(1 << 8) /* 1: not receive CRC status */
#define CMD_SET_TBIT		(1 << 7) /* 1: tran mission bit "Low" */
#define CMD_SET_OPDM		(1 << 6) /* 1: open/drain */
#define CMD_SET_CCSH		(1 << 5)
#define CMD_SET_DATW_1		((0 << 1) | (0 << 0)) /* 1bit */
#define CMD_SET_DATW_4		((0 << 1) | (1 << 0)) /* 4bit */
#define CMD_SET_DATW_8		((1 << 1) | (0 << 0)) /* 8bit */

/* CE_CMD_CTRL */
#define CMD_CTRL_BREAK		(1 << 0)

/* CE_BLOCK_SET */
#define BLOCK_SIZE_MASK		0x0000ffff

/* CE_CLK_CTRL */
#define CLK_ENABLE		(1 << 24) /* 1: output mmc clock */
#define CLK_CLEAR		((1 << 19) | (1 << 18) | (1 << 17) | (1 << 16))
#define CLK_SUP_PCLK		((1 << 19) | (1 << 18) | (1 << 17) | (1 << 16))
#define SRSPTO_256		((1 << 13) | (0 << 12)) /* resp timeout */
#define SRBSYTO_29		((1 << 11) | (1 << 10) |	\
				 (1 << 9) | (1 << 8)) /* resp busy timeout */
#define SRWDTO_29		((1 << 7) | (1 << 6) |		\
				 (1 << 5) | (1 << 4)) /* read/write timeout */
#define SCCSTO_29		((1 << 3) | (1 << 2) |		\
				 (1 << 1) | (1 << 0)) /* ccs timeout */

/* CE_BUF_ACC */
#define BUF_ACC_DMAWEN		(1 << 25)
#define BUF_ACC_DMAREN		(1 << 24)
#define BUF_ACC_BUSW_32		(0 << 17)
#define BUF_ACC_BUSW_16		(1 << 17)
#define BUF_ACC_ATYP		(1 << 16)

/* CE_INT */
#define INT_CCSDE		(1 << 29)
#define INT_CMD12DRE		(1 << 26)
#define INT_CMD12RBE		(1 << 25)
#define INT_CMD12CRE		(1 << 24)
#define INT_DTRANE		(1 << 23)
#define INT_BUFRE		(1 << 22)
#define INT_BUFWEN		(1 << 21)
#define INT_BUFREN		(1 << 20)
#define INT_CCSRCV		(1 << 19)
#define INT_RBSYE		(1 << 17)
#define INT_CRSPE		(1 << 16)
#define INT_CMDVIO		(1 << 15)
#define INT_BUFVIO		(1 << 14)
#define INT_WDATERR		(1 << 11)
#define INT_RDATERR		(1 << 10)
#define INT_RIDXERR		(1 << 9)
#define INT_RSPERR		(1 << 8)
#define INT_CCSTO		(1 << 5)
#define INT_CRCSTO		(1 << 4)
#define INT_WDATTO		(1 << 3)
#define INT_RDATTO		(1 << 2)
#define INT_RBSYTO		(1 << 1)
#define INT_RSPTO		(1 << 0)
#define INT_ERR_STS		(INT_CMDVIO | INT_BUFVIO | INT_WDATERR |  \
				 INT_RDATERR | INT_RIDXERR | INT_RSPERR | \
				 INT_CCSTO | INT_CRCSTO | INT_WDATTO |	  \
				 INT_RDATTO | INT_RBSYTO | INT_RSPTO)

/* CE_INT_MASK */
#define MASK_ALL		0x00000000
#define MASK_MCCSDE		(1 << 29)
#define MASK_MCMD12DRE		(1 << 26)
#define MASK_MCMD12RBE		(1 << 25)
#define MASK_MCMD12CRE		(1 << 24)
#define MASK_MDTRANE		(1 << 23)
#define MASK_MBUFRE		(1 << 22)
#define MASK_MBUFWEN		(1 << 21)
#define MASK_MBUFREN		(1 << 20)
#define MASK_MCCSRCV		(1 << 19)
#define MASK_MRBSYE		(1 << 17)
#define MASK_MCRSPE		(1 << 16)
#define MASK_MCMDVIO		(1 << 15)
#define MASK_MBUFVIO		(1 << 14)
#define MASK_MWDATERR		(1 << 11)
#define MASK_MRDATERR		(1 << 10)
#define MASK_MRIDXERR		(1 << 9)
#define MASK_MRSPERR		(1 << 8)
#define MASK_MCCSTO		(1 << 5)
#define MASK_MCRCSTO		(1 << 4)
#define MASK_MWDATTO		(1 << 3)
#define MASK_MRDATTO		(1 << 2)
#define MASK_MRBSYTO		(1 << 1)
#define MASK_MRSPTO		(1 << 0)

/* CE_HOST_STS1 */
#define STS1_CMDSEQ		(1 << 31)

/* CE_HOST_STS2 */
#define STS2_CRCSTE		(1 << 31)
#define STS2_CRC16E		(1 << 30)
#define STS2_AC12CRCE		(1 << 29)
#define STS2_RSPCRC7E		(1 << 28)
#define STS2_CRCSTEBE		(1 << 27)
#define STS2_RDATEBE		(1 << 26)
#define STS2_AC12REBE		(1 << 25)
#define STS2_RSPEBE		(1 << 24)
#define STS2_AC12IDXE		(1 << 23)
#define STS2_RSPIDXE		(1 << 22)
#define STS2_CCSTO		(1 << 15)
#define STS2_RDATTO		(1 << 14)
#define STS2_DATBSYTO		(1 << 13)
#define STS2_CRCSTTO		(1 << 12)
#define STS2_AC12BSYTO		(1 << 11)
#define STS2_RSPBSYTO		(1 << 10)
#define STS2_AC12RSPTO		(1 << 9)
#define STS2_RSPTO		(1 << 8)
#define STS2_CRC_ERR		(STS2_CRCSTE | STS2_CRC16E |		\
				 STS2_AC12CRCE | STS2_RSPCRC7E | STS2_CRCSTEBE)
#define STS2_TIMEOUT_ERR	(STS2_CCSTO | STS2_RDATTO |		\
				 STS2_DATBSYTO | STS2_CRCSTTO |		\
				 STS2_AC12BSYTO | STS2_RSPBSYTO |	\
				 STS2_AC12RSPTO | STS2_RSPTO)

/* CE_VERSION */
#define SOFT_RST_ON		(1 << 31)
#define SOFT_RST_OFF		(0 << 31)

#define CLKDEV_EMMC_DATA	52000000 /* 52MHz */
#define CLKDEV_MMC_DATA		20000000 /* 20MHz */
#define CLKDEV_INIT		400000   /* 400 KHz */

struct sh_mmcif_host {
	struct mmc_host *mmc;
	struct mmc_data *data;
	struct mmc_command *cmd;
	struct platform_device *pd;
	struct clk *hclk;
	unsigned int clk;
	int bus_width;
	u16 wait_int;
	u16 sd_error;
	long timeout;
	void __iomem *addr;
	wait_queue_head_t intr_wait;
};


static inline void sh_mmcif_bitset(struct sh_mmcif_host *host,
					unsigned int reg, u32 val)
{
	writel(val | readl(host->addr + reg), host->addr + reg);
}

static inline void sh_mmcif_bitclr(struct sh_mmcif_host *host,
					unsigned int reg, u32 val)
{
	writel(~val & readl(host->addr + reg), host->addr + reg);
}


static void sh_mmcif_clock_control(struct sh_mmcif_host *host, unsigned int clk)
{
	struct sh_mmcif_plat_data *p = host->pd->dev.platform_data;

	sh_mmcif_bitclr(host, MMCIF_CE_CLK_CTRL, CLK_ENABLE);
	sh_mmcif_bitclr(host, MMCIF_CE_CLK_CTRL, CLK_CLEAR);

	if (!clk)
		return;
	if (p->sup_pclk && clk == host->clk)
		sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_SUP_PCLK);
	else
		sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_CLEAR &
			(ilog2(__rounddown_pow_of_two(host->clk / clk)) << 16));

	sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_ENABLE);
}

static void sh_mmcif_sync_reset(struct sh_mmcif_host *host)
{
	u32 tmp;

	tmp = 0x010f0000 & sh_mmcif_readl(host->addr, MMCIF_CE_CLK_CTRL);

	sh_mmcif_writel(host->addr, MMCIF_CE_VERSION, SOFT_RST_ON);
	sh_mmcif_writel(host->addr, MMCIF_CE_VERSION, SOFT_RST_OFF);
	sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, tmp |
		SRSPTO_256 | SRBSYTO_29 | SRWDTO_29 | SCCSTO_29);
	/* byte swap on */
	sh_mmcif_bitset(host, MMCIF_CE_BUF_ACC, BUF_ACC_ATYP);
}

static int sh_mmcif_error_manage(struct sh_mmcif_host *host)
{
	u32 state1, state2;
	int ret, timeout = 10000000;

	host->sd_error = 0;
	host->wait_int = 0;

	state1 = sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS1);
	state2 = sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS2);
	pr_debug("%s: ERR HOST_STS1 = %08x\n", DRIVER_NAME, state1);
	pr_debug("%s: ERR HOST_STS2 = %08x\n", DRIVER_NAME, state2);

	if (state1 & STS1_CMDSEQ) {
		sh_mmcif_bitset(host, MMCIF_CE_CMD_CTRL, CMD_CTRL_BREAK);
		sh_mmcif_bitset(host, MMCIF_CE_CMD_CTRL, ~CMD_CTRL_BREAK);
		while (1) {
			timeout--;
			if (timeout < 0) {
				pr_err(DRIVER_NAME": Forceed end of " \
					"command sequence timeout err\n");
				return -EIO;
			}
			if (!(sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS1)
								& STS1_CMDSEQ))
				break;
			mdelay(1);
		}
		sh_mmcif_sync_reset(host);
		pr_debug(DRIVER_NAME": Forced end of command sequence\n");
		return -EIO;
	}

	if (state2 & STS2_CRC_ERR) {
		pr_debug(DRIVER_NAME": Happened CRC error\n");
		ret = -EIO;
	} else if (state2 & STS2_TIMEOUT_ERR) {
		pr_debug(DRIVER_NAME": Happened Timeout error\n");
		ret = -ETIMEDOUT;
	} else {
		pr_debug(DRIVER_NAME": Happened End/Index error\n");
		ret = -EIO;
	}
	return ret;
}

static int sh_mmcif_single_read(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, *p = sg_virt(data->sg);

	host->wait_int = 0;

	/* buf read enable */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
	time = wait_event_interruptible_timeout(host->intr_wait,
			host->wait_int == 1 ||
			host->sd_error == 1, host->timeout);
	if (host->wait_int != 1 && (time == 0 || host->sd_error != 0))
		return sh_mmcif_error_manage(host);

	host->wait_int = 0;
	blocksize = (BLOCK_SIZE_MASK &
			sh_mmcif_readl(host->addr, MMCIF_CE_BLOCK_SET)) + 3;
	for (i = 0; i < blocksize / 4; i++)
		*p++ = sh_mmcif_readl(host->addr, MMCIF_CE_DATA);

	/* buffer read end */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFRE);
	time = wait_event_interruptible_timeout(host->intr_wait,
			host->wait_int == 1 ||
			host->sd_error == 1, host->timeout);
	if (host->wait_int != 1 && (time == 0 || host->sd_error != 0))
		return sh_mmcif_error_manage(host);

	host->wait_int = 0;
	return 0;
}

static int sh_mmcif_multi_read(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, j, sec, *p;

	blocksize = BLOCK_SIZE_MASK & sh_mmcif_readl(host->addr,
						     MMCIF_CE_BLOCK_SET);
	for (j = 0; j < data->sg_len; j++) {
		p = sg_virt(data->sg);
		host->wait_int = 0;
		for (sec = 0; sec < data->sg->length / blocksize; sec++) {
			sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
			/* buf read enable */
			time = wait_event_interruptible_timeout(host->intr_wait,
				host->wait_int == 1 ||
				host->sd_error == 1, host->timeout);

			if (host->wait_int != 1 &&
			    (time == 0 || host->sd_error != 0))
				return sh_mmcif_error_manage(host);

			host->wait_int = 0;
			for (i = 0; i < blocksize / 4; i++)
				*p++ = sh_mmcif_readl(host->addr,
						      MMCIF_CE_DATA);
		}
		if (j < data->sg_len - 1)
			data->sg++;
	}
	return 0;
}

static int sh_mmcif_single_write(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, *p = sg_virt(data->sg);

	host->wait_int = 0;
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);

	/* buf write enable */
	time = wait_event_interruptible_timeout(host->intr_wait,
			host->wait_int == 1 ||
			host->sd_error == 1, host->timeout);
	if (host->wait_int != 1 && (time == 0 || host->sd_error != 0))
		return sh_mmcif_error_manage(host);

	host->wait_int = 0;
	blocksize = (BLOCK_SIZE_MASK &
			sh_mmcif_readl(host->addr, MMCIF_CE_BLOCK_SET)) + 3;
	for (i = 0; i < blocksize / 4; i++)
		sh_mmcif_writel(host->addr, MMCIF_CE_DATA, *p++);

	/* buffer write end */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MDTRANE);

	time = wait_event_interruptible_timeout(host->intr_wait,
			host->wait_int == 1 ||
			host->sd_error == 1, host->timeout);
	if (host->wait_int != 1 && (time == 0 || host->sd_error != 0))
		return sh_mmcif_error_manage(host);

	host->wait_int = 0;
	return 0;
}

static int sh_mmcif_multi_write(struct sh_mmcif_host *host,
						struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 i, sec, j, blocksize, *p;

	blocksize = BLOCK_SIZE_MASK & sh_mmcif_readl(host->addr,
						     MMCIF_CE_BLOCK_SET);

	for (j = 0; j < data->sg_len; j++) {
		p = sg_virt(data->sg);
		host->wait_int = 0;
		for (sec = 0; sec < data->sg->length / blocksize; sec++) {
			sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);
			/* buf write enable*/
			time = wait_event_interruptible_timeout(host->intr_wait,
				host->wait_int == 1 ||
				host->sd_error == 1, host->timeout);

			if (host->wait_int != 1 &&
			    (time == 0 || host->sd_error != 0))
				return sh_mmcif_error_manage(host);

			host->wait_int = 0;
			for (i = 0; i < blocksize / 4; i++)
				sh_mmcif_writel(host->addr,
						MMCIF_CE_DATA, *p++);
		}
		if (j < data->sg_len - 1)
			data->sg++;
	}
	return 0;
}

static void sh_mmcif_get_response(struct sh_mmcif_host *host,
						struct mmc_command *cmd)
{
	if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP3);
		cmd->resp[1] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP2);
		cmd->resp[2] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP1);
		cmd->resp[3] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP0);
	} else
		cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP0);
}

static void sh_mmcif_get_cmd12response(struct sh_mmcif_host *host,
						struct mmc_command *cmd)
{
	cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP_CMD12);
}

static u32 sh_mmcif_set_cmd(struct sh_mmcif_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd, u32 opc)
{
	u32 tmp = 0;

	/* Response Type check */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		tmp |= CMD_SET_RTYP_NO;
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
	case MMC_RSP_R3:
		tmp |= CMD_SET_RTYP_6B;
		break;
	case MMC_RSP_R2:
		tmp |= CMD_SET_RTYP_17B;
		break;
	default:
		pr_err(DRIVER_NAME": Not support type response.\n");
		break;
	}
	switch (opc) {
	/* RBSY */
	case MMC_SWITCH:
	case MMC_STOP_TRANSMISSION:
	case MMC_SET_WRITE_PROT:
	case MMC_CLR_WRITE_PROT:
	case MMC_ERASE:
	case MMC_GEN_CMD:
		tmp |= CMD_SET_RBSY;
		break;
	}
	/* WDAT / DATW */
	if (host->data) {
		tmp |= CMD_SET_WDAT;
		switch (host->bus_width) {
		case MMC_BUS_WIDTH_1:
			tmp |= CMD_SET_DATW_1;
			break;
		case MMC_BUS_WIDTH_4:
			tmp |= CMD_SET_DATW_4;
			break;
		case MMC_BUS_WIDTH_8:
			tmp |= CMD_SET_DATW_8;
			break;
		default:
			pr_err(DRIVER_NAME": Not support bus width.\n");
			break;
		}
	}
	/* DWEN */
	if (opc == MMC_WRITE_BLOCK || opc == MMC_WRITE_MULTIPLE_BLOCK)
		tmp |= CMD_SET_DWEN;
	/* CMLTE/CMD12EN */
	if (opc == MMC_READ_MULTIPLE_BLOCK || opc == MMC_WRITE_MULTIPLE_BLOCK) {
		tmp |= CMD_SET_CMLTE | CMD_SET_CMD12EN;
		sh_mmcif_bitset(host, MMCIF_CE_BLOCK_SET,
					mrq->data->blocks << 16);
	}
	/* RIDXC[1:0] check bits */
	if (opc == MMC_SEND_OP_COND || opc == MMC_ALL_SEND_CID ||
	    opc == MMC_SEND_CSD || opc == MMC_SEND_CID)
		tmp |= CMD_SET_RIDXC_BITS;
	/* RCRC7C[1:0] check bits */
	if (opc == MMC_SEND_OP_COND)
		tmp |= CMD_SET_CRC7C_BITS;
	/* RCRC7C[1:0] internal CRC7 */
	if (opc == MMC_ALL_SEND_CID ||
		opc == MMC_SEND_CSD || opc == MMC_SEND_CID)
		tmp |= CMD_SET_CRC7C_INTERNAL;

	return opc = ((opc << 24) | tmp);
}

static u32 sh_mmcif_data_trans(struct sh_mmcif_host *host,
				struct mmc_request *mrq, u32 opc)
{
	u32 ret;

	switch (opc) {
	case MMC_READ_MULTIPLE_BLOCK:
		ret = sh_mmcif_multi_read(host, mrq);
		break;
	case MMC_WRITE_MULTIPLE_BLOCK:
		ret = sh_mmcif_multi_write(host, mrq);
		break;
	case MMC_WRITE_BLOCK:
		ret = sh_mmcif_single_write(host, mrq);
		break;
	case MMC_READ_SINGLE_BLOCK:
	case MMC_SEND_EXT_CSD:
		ret = sh_mmcif_single_read(host, mrq);
		break;
	default:
		pr_err(DRIVER_NAME": NOT SUPPORT CMD = d'%08d\n", opc);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static void sh_mmcif_start_cmd(struct sh_mmcif_host *host,
			struct mmc_request *mrq, struct mmc_command *cmd)
{
	long time;
	int ret = 0, mask = 0;
	u32 opc = cmd->opcode;

	host->cmd = cmd;

	switch (opc) {
	/* respons busy check */
	case MMC_SWITCH:
	case MMC_STOP_TRANSMISSION:
	case MMC_SET_WRITE_PROT:
	case MMC_CLR_WRITE_PROT:
	case MMC_ERASE:
	case MMC_GEN_CMD:
		mask = MASK_MRBSYE;
		break;
	default:
		mask = MASK_MCRSPE;
		break;
	}
	mask |=	MASK_MCMDVIO | MASK_MBUFVIO | MASK_MWDATERR |
		MASK_MRDATERR | MASK_MRIDXERR | MASK_MRSPERR |
		MASK_MCCSTO | MASK_MCRCSTO | MASK_MWDATTO |
		MASK_MRDATTO | MASK_MRBSYTO | MASK_MRSPTO;

	if (host->data) {
		sh_mmcif_writel(host->addr, MMCIF_CE_BLOCK_SET, 0);
		sh_mmcif_writel(host->addr, MMCIF_CE_BLOCK_SET,
				mrq->data->blksz);
	}
	opc = sh_mmcif_set_cmd(host, mrq, cmd, opc);

	sh_mmcif_writel(host->addr, MMCIF_CE_INT, 0xD80430C0);
	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, mask);
	/* set arg */
	sh_mmcif_writel(host->addr, MMCIF_CE_ARG, cmd->arg);
	host->wait_int = 0;
	/* set cmd */
	sh_mmcif_writel(host->addr, MMCIF_CE_CMD_SET, opc);

	time = wait_event_interruptible_timeout(host->intr_wait,
		host->wait_int == 1 || host->sd_error == 1, host->timeout);
	if (host->wait_int != 1 && time == 0) {
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}
	if (host->sd_error) {
		switch (cmd->opcode) {
		case MMC_ALL_SEND_CID:
		case MMC_SELECT_CARD:
		case MMC_APP_CMD:
			cmd->error = -ETIMEDOUT;
			break;
		default:
			pr_debug("%s: Cmd(d'%d) err\n",
					DRIVER_NAME, cmd->opcode);
			cmd->error = sh_mmcif_error_manage(host);
			break;
		}
		host->sd_error = 0;
		host->wait_int = 0;
		return;
	}
	if (!(cmd->flags & MMC_RSP_PRESENT)) {
		cmd->error = ret;
		host->wait_int = 0;
		return;
	}
	if (host->wait_int == 1) {
		sh_mmcif_get_response(host, cmd);
		host->wait_int = 0;
	}
	if (host->data) {
		ret = sh_mmcif_data_trans(host, mrq, cmd->opcode);
		if (ret < 0)
			mrq->data->bytes_xfered = 0;
		else
			mrq->data->bytes_xfered =
				mrq->data->blocks * mrq->data->blksz;
	}
	cmd->error = ret;
}

static void sh_mmcif_stop_cmd(struct sh_mmcif_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	long time;

	if (mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK)
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MCMD12DRE);
	else if (mrq->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MCMD12RBE);
	else {
		pr_err(DRIVER_NAME": not support stop cmd\n");
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}

	time = wait_event_interruptible_timeout(host->intr_wait,
			host->wait_int == 1 ||
			host->sd_error == 1, host->timeout);
	if (host->wait_int != 1 && (time == 0 || host->sd_error != 0)) {
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}
	sh_mmcif_get_cmd12response(host, cmd);
	host->wait_int = 0;
	cmd->error = 0;
}

static void sh_mmcif_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);

	switch (mrq->cmd->opcode) {
	/* MMCIF does not support SD/SDIO command */
	case SD_IO_SEND_OP_COND:
	case MMC_APP_CMD:
		mrq->cmd->error = -ETIMEDOUT;
		mmc_request_done(mmc, mrq);
		return;
	case MMC_SEND_EXT_CSD: /* = SD_SEND_IF_COND (8) */
		if (!mrq->data) {
			/* send_if_cond cmd (not support) */
			mrq->cmd->error = -ETIMEDOUT;
			mmc_request_done(mmc, mrq);
			return;
		}
		break;
	default:
		break;
	}
	host->data = mrq->data;
	sh_mmcif_start_cmd(host, mrq, mrq->cmd);
	host->data = NULL;

	if (mrq->cmd->error != 0) {
		mmc_request_done(mmc, mrq);
		return;
	}
	if (mrq->stop)
		sh_mmcif_stop_cmd(host, mrq, mrq->stop);
	mmc_request_done(mmc, mrq);
}

static void sh_mmcif_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);
	struct sh_mmcif_plat_data *p = host->pd->dev.platform_data;

	if (ios->power_mode == MMC_POWER_OFF) {
		/* clock stop */
		sh_mmcif_clock_control(host, 0);
		if (p->down_pwr)
			p->down_pwr(host->pd);
		return;
	} else if (ios->power_mode == MMC_POWER_UP) {
		if (p->set_pwr)
			p->set_pwr(host->pd, ios->power_mode);
	}

	if (ios->clock)
		sh_mmcif_clock_control(host, ios->clock);

	host->bus_width = ios->bus_width;
}

static int sh_mmcif_get_cd(struct mmc_host *mmc)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);
	struct sh_mmcif_plat_data *p = host->pd->dev.platform_data;

	if (!p->get_cd)
		return -ENOSYS;
	else
		return p->get_cd(host->pd);
}

static struct mmc_host_ops sh_mmcif_ops = {
	.request	= sh_mmcif_request,
	.set_ios	= sh_mmcif_set_ios,
	.get_cd		= sh_mmcif_get_cd,
};

static void sh_mmcif_detect(struct mmc_host *mmc)
{
	mmc_detect_change(mmc, 0);
}

static irqreturn_t sh_mmcif_intr(int irq, void *dev_id)
{
	struct sh_mmcif_host *host = dev_id;
	u32 state = 0;
	int err = 0;

	state = sh_mmcif_readl(host->addr, MMCIF_CE_INT);

	if (state & INT_RBSYE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
				~(INT_RBSYE | INT_CRSPE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MRBSYE);
	} else if (state & INT_CRSPE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_CRSPE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCRSPE);
	} else if (state & INT_BUFREN) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFREN);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
	} else if (state & INT_BUFWEN) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFWEN);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);
	} else if (state & INT_CMD12DRE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
			~(INT_CMD12DRE | INT_CMD12RBE |
			  INT_CMD12CRE | INT_BUFRE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCMD12DRE);
	} else if (state & INT_BUFRE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFRE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFRE);
	} else if (state & INT_DTRANE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_DTRANE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MDTRANE);
	} else if (state & INT_CMD12RBE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
				~(INT_CMD12RBE | INT_CMD12CRE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCMD12RBE);
	} else if (state & INT_ERR_STS) {
		/* err interrupts */
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~state);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, state);
		err = 1;
	} else {
		pr_debug("%s: Not support int\n", DRIVER_NAME);
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~state);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, state);
		err = 1;
	}
	if (err) {
		host->sd_error = 1;
		pr_debug("%s: int err state = %08x\n", DRIVER_NAME, state);
	}
	host->wait_int = 1;
	wake_up(&host->intr_wait);

	return IRQ_HANDLED;
}

static int __devinit sh_mmcif_probe(struct platform_device *pdev)
{
	int ret = 0, irq[2];
	struct mmc_host *mmc;
	struct sh_mmcif_host *host = NULL;
	struct sh_mmcif_plat_data *pd = NULL;
	struct resource *res;
	void __iomem *reg;
	char clk_name[8];

	irq[0] = platform_get_irq(pdev, 0);
	irq[1] = platform_get_irq(pdev, 1);
	if (irq[0] < 0 || irq[1] < 0) {
		pr_err(DRIVER_NAME": Get irq error\n");
		return -ENXIO;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "platform_get_resource error.\n");
		return -ENXIO;
	}
	reg = ioremap(res->start, resource_size(res));
	if (!reg) {
		dev_err(&pdev->dev, "ioremap error.\n");
		return -ENOMEM;
	}
	pd = (struct sh_mmcif_plat_data *)(pdev->dev.platform_data);
	if (!pd) {
		dev_err(&pdev->dev, "sh_mmcif plat data error.\n");
		ret = -ENXIO;
		goto clean_up;
	}
	mmc = mmc_alloc_host(sizeof(struct sh_mmcif_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto clean_up;
	}
	host		= mmc_priv(mmc);
	host->mmc	= mmc;
	host->addr	= reg;
	host->timeout	= 1000;

	snprintf(clk_name, sizeof(clk_name), "mmc%d", pdev->id);
	host->hclk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(host->hclk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(host->hclk);
		goto clean_up1;
	}
	clk_enable(host->hclk);
	host->clk = clk_get_rate(host->hclk);
	host->pd = pdev;

	init_waitqueue_head(&host->intr_wait);

	mmc->ops = &sh_mmcif_ops;
	mmc->f_max = host->clk;
	/* close to 400KHz */
	if (mmc->f_max < 51200000)
		mmc->f_min = mmc->f_max / 128;
	else if (mmc->f_max < 102400000)
		mmc->f_min = mmc->f_max / 256;
	else
		mmc->f_min = mmc->f_max / 512;
	if (pd->ocr)
		mmc->ocr_avail = pd->ocr;
	mmc->caps = MMC_CAP_MMC_HIGHSPEED;
	if (pd->caps)
		mmc->caps |= pd->caps;
	mmc->max_segs = 128;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = 65535;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;

	sh_mmcif_sync_reset(host);
	platform_set_drvdata(pdev, host);
	mmc_add_host(mmc);

	ret = request_irq(irq[0], sh_mmcif_intr, 0, "sh_mmc:error", host);
	if (ret) {
		pr_err(DRIVER_NAME": request_irq error (sh_mmc:error)\n");
		goto clean_up2;
	}
	ret = request_irq(irq[1], sh_mmcif_intr, 0, "sh_mmc:int", host);
	if (ret) {
		free_irq(irq[0], host);
		pr_err(DRIVER_NAME": request_irq error (sh_mmc:int)\n");
		goto clean_up2;
	}

	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, MASK_ALL);
	sh_mmcif_detect(host->mmc);

	pr_info("%s: driver version %s\n", DRIVER_NAME, DRIVER_VERSION);
	pr_debug("%s: chip ver H'%04x\n", DRIVER_NAME,
		sh_mmcif_readl(host->addr, MMCIF_CE_VERSION) & 0x0000ffff);
	return ret;

clean_up2:
	clk_disable(host->hclk);
clean_up1:
	mmc_free_host(mmc);
clean_up:
	if (reg)
		iounmap(reg);
	return ret;
}

static int __devexit sh_mmcif_remove(struct platform_device *pdev)
{
	struct sh_mmcif_host *host = platform_get_drvdata(pdev);
	int irq[2];

	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, MASK_ALL);

	irq[0] = platform_get_irq(pdev, 0);
	irq[1] = platform_get_irq(pdev, 1);

	if (host->addr)
		iounmap(host->addr);

	platform_set_drvdata(pdev, NULL);
	mmc_remove_host(host->mmc);

	free_irq(irq[0], host);
	free_irq(irq[1], host);

	clk_disable(host->hclk);
	mmc_free_host(host->mmc);

	return 0;
}

static struct platform_driver sh_mmcif_driver = {
	.probe		= sh_mmcif_probe,
	.remove		= sh_mmcif_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __init sh_mmcif_init(void)
{
	return platform_driver_register(&sh_mmcif_driver);
}

static void __exit sh_mmcif_exit(void)
{
	platform_driver_unregister(&sh_mmcif_driver);
}

module_init(sh_mmcif_init);
module_exit(sh_mmcif_exit);


MODULE_DESCRIPTION("SuperH on-chip MMC/eMMC interface driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS(DRIVER_NAME);
MODULE_AUTHOR("Yusuke Goda <yusuke.goda.sx@renesas.com>");
