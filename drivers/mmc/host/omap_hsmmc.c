/*
 * drivers/mmc/host/omap_hsmmc.c
 *
 * Driver for OMAP2430/3430 MMC controller.
 *
 * Copyright (C) 2007 Texas Instruments.
 *
 * Authors:
 *	Syed Mohammed Khasim	<x0khasim@ti.com>
 *	Madhusudhan		<madhu.cr@ti.com>
 *	Mohit Jalori		<mjalori@ti.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <mach/dma.h>
#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/mmc.h>
#include <mach/cpu.h>

/* OMAP HSMMC Host Controller Registers */
#define OMAP_HSMMC_SYSCONFIG	0x0010
#define OMAP_HSMMC_CON		0x002C
#define OMAP_HSMMC_BLK		0x0104
#define OMAP_HSMMC_ARG		0x0108
#define OMAP_HSMMC_CMD		0x010C
#define OMAP_HSMMC_RSP10	0x0110
#define OMAP_HSMMC_RSP32	0x0114
#define OMAP_HSMMC_RSP54	0x0118
#define OMAP_HSMMC_RSP76	0x011C
#define OMAP_HSMMC_DATA		0x0120
#define OMAP_HSMMC_HCTL		0x0128
#define OMAP_HSMMC_SYSCTL	0x012C
#define OMAP_HSMMC_STAT		0x0130
#define OMAP_HSMMC_IE		0x0134
#define OMAP_HSMMC_ISE		0x0138
#define OMAP_HSMMC_CAPA		0x0140

#define VS18			(1 << 26)
#define VS30			(1 << 25)
#define SDVS18			(0x5 << 9)
#define SDVS30			(0x6 << 9)
#define SDVSCLR			0xFFFFF1FF
#define SDVSDET			0x00000400
#define AUTOIDLE		0x1
#define SDBP			(1 << 8)
#define DTO			0xe
#define ICE			0x1
#define ICS			0x2
#define CEN			(1 << 2)
#define CLKD_MASK		0x0000FFC0
#define CLKD_SHIFT		6
#define DTO_MASK		0x000F0000
#define DTO_SHIFT		16
#define INT_EN_MASK		0x307F0033
#define INIT_STREAM		(1 << 1)
#define DP_SELECT		(1 << 21)
#define DDIR			(1 << 4)
#define DMA_EN			0x1
#define MSBS			(1 << 5)
#define BCE			(1 << 1)
#define FOUR_BIT		(1 << 1)
#define CC			0x1
#define TC			0x02
#define OD			0x1
#define ERR			(1 << 15)
#define CMD_TIMEOUT		(1 << 16)
#define DATA_TIMEOUT		(1 << 20)
#define CMD_CRC			(1 << 17)
#define DATA_CRC		(1 << 21)
#define CARD_ERR		(1 << 28)
#define STAT_CLEAR		0xFFFFFFFF
#define INIT_STREAM_CMD		0x00000000
#define DUAL_VOLT_OCR_BIT	7
#define SRC			(1 << 25)
#define SRD			(1 << 26)

/*
 * FIXME: Most likely all the data using these _DEVID defines should come
 * from the platform_data, or implemented in controller and slot specific
 * functions.
 */
#define OMAP_MMC1_DEVID		0
#define OMAP_MMC2_DEVID		1

#define OMAP_MMC_DATADIR_NONE	0
#define OMAP_MMC_DATADIR_READ	1
#define OMAP_MMC_DATADIR_WRITE	2
#define MMC_TIMEOUT_MS		20
#define OMAP_MMC_MASTER_CLOCK	96000000
#define DRIVER_NAME		"mmci-omap-hs"

/*
 * One controller can have multiple slots, like on some omap boards using
 * omap.c controller driver. Luckily this is not currently done on any known
 * omap_hsmmc.c device.
 */
#define mmc_slot(host)		(host->pdata->slots[host->slot_id])

/*
 * MMC Host controller read/write API's
 */
#define OMAP_HSMMC_READ(base, reg)	\
	__raw_readl((base) + OMAP_HSMMC_##reg)

#define OMAP_HSMMC_WRITE(base, reg, val) \
	__raw_writel((val), (base) + OMAP_HSMMC_##reg)

struct mmc_omap_host {
	struct	device		*dev;
	struct	mmc_host	*mmc;
	struct	mmc_request	*mrq;
	struct	mmc_command	*cmd;
	struct	mmc_data	*data;
	struct	clk		*fclk;
	struct	clk		*iclk;
	struct	clk		*dbclk;
	struct	semaphore	sem;
	struct	work_struct	mmc_carddetect_work;
	void	__iomem		*base;
	resource_size_t		mapbase;
	unsigned int		id;
	unsigned int		dma_len;
	unsigned int		dma_dir;
	unsigned char		bus_mode;
	unsigned char		datadir;
	u32			*buffer;
	u32			bytesleft;
	int			suspended;
	int			irq;
	int			carddetect;
	int			use_dma, dma_ch;
	int			initstr;
	int			slot_id;
	int			dbclk_enabled;
	struct	omap_mmc_platform_data	*pdata;
};

/*
 * Stop clock to the card
 */
static void omap_mmc_stop_clock(struct mmc_omap_host *host)
{
	OMAP_HSMMC_WRITE(host->base, SYSCTL,
		OMAP_HSMMC_READ(host->base, SYSCTL) & ~CEN);
	if ((OMAP_HSMMC_READ(host->base, SYSCTL) & CEN) != 0x0)
		dev_dbg(mmc_dev(host->mmc), "MMC Clock is not stoped\n");
}

/*
 * Send init stream sequence to card
 * before sending IDLE command
 */
static void send_init_stream(struct mmc_omap_host *host)
{
	int reg = 0;
	unsigned long timeout;

	disable_irq(host->irq);
	OMAP_HSMMC_WRITE(host->base, CON,
		OMAP_HSMMC_READ(host->base, CON) | INIT_STREAM);
	OMAP_HSMMC_WRITE(host->base, CMD, INIT_STREAM_CMD);

	timeout = jiffies + msecs_to_jiffies(MMC_TIMEOUT_MS);
	while ((reg != CC) && time_before(jiffies, timeout))
		reg = OMAP_HSMMC_READ(host->base, STAT) & CC;

	OMAP_HSMMC_WRITE(host->base, CON,
		OMAP_HSMMC_READ(host->base, CON) & ~INIT_STREAM);
	enable_irq(host->irq);
}

static inline
int mmc_omap_cover_is_closed(struct mmc_omap_host *host)
{
	int r = 1;

	if (host->pdata->slots[host->slot_id].get_cover_state)
		r = host->pdata->slots[host->slot_id].get_cover_state(host->dev,
			host->slot_id);
	return r;
}

static ssize_t
mmc_omap_show_cover_switch(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct mmc_host *mmc = container_of(dev, struct mmc_host, class_dev);
	struct mmc_omap_host *host = mmc_priv(mmc);

	return sprintf(buf, "%s\n", mmc_omap_cover_is_closed(host) ? "closed" :
		       "open");
}

static DEVICE_ATTR(cover_switch, S_IRUGO, mmc_omap_show_cover_switch, NULL);

static ssize_t
mmc_omap_show_slot_name(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct mmc_host *mmc = container_of(dev, struct mmc_host, class_dev);
	struct mmc_omap_host *host = mmc_priv(mmc);
	struct omap_mmc_slot_data slot = host->pdata->slots[host->slot_id];

	return sprintf(buf, "slot:%s\n", slot.name);
}

static DEVICE_ATTR(slot_name, S_IRUGO, mmc_omap_show_slot_name, NULL);

/*
 * Configure the response type and send the cmd.
 */
static void
mmc_omap_start_command(struct mmc_omap_host *host, struct mmc_command *cmd,
	struct mmc_data *data)
{
	int cmdreg = 0, resptype = 0, cmdtype = 0;

	dev_dbg(mmc_dev(host->mmc), "%s: CMD%d, argument 0x%08x\n",
		mmc_hostname(host->mmc), cmd->opcode, cmd->arg);
	host->cmd = cmd;

	/*
	 * Clear status bits and enable interrupts
	 */
	OMAP_HSMMC_WRITE(host->base, STAT, STAT_CLEAR);
	OMAP_HSMMC_WRITE(host->base, ISE, INT_EN_MASK);
	OMAP_HSMMC_WRITE(host->base, IE, INT_EN_MASK);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			resptype = 1;
		else
			resptype = 2;
	}

	/*
	 * Unlike OMAP1 controller, the cmdtype does not seem to be based on
	 * ac, bc, adtc, bcr. Only commands ending an open ended transfer need
	 * a val of 0x3, rest 0x0.
	 */
	if (cmd == host->mrq->stop)
		cmdtype = 0x3;

	cmdreg = (cmd->opcode << 24) | (resptype << 16) | (cmdtype << 22);

	if (data) {
		cmdreg |= DP_SELECT | MSBS | BCE;
		if (data->flags & MMC_DATA_READ)
			cmdreg |= DDIR;
		else
			cmdreg &= ~(DDIR);
	}

	if (host->use_dma)
		cmdreg |= DMA_EN;

	OMAP_HSMMC_WRITE(host->base, ARG, cmd->arg);
	OMAP_HSMMC_WRITE(host->base, CMD, cmdreg);
}

/*
 * Notify the transfer complete to MMC core
 */
static void
mmc_omap_xfer_done(struct mmc_omap_host *host, struct mmc_data *data)
{
	host->data = NULL;

	if (host->use_dma && host->dma_ch != -1)
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->dma_len,
			host->dma_dir);

	host->datadir = OMAP_MMC_DATADIR_NONE;

	if (!data->error)
		data->bytes_xfered += data->blocks * (data->blksz);
	else
		data->bytes_xfered = 0;

	if (!data->stop) {
		host->mrq = NULL;
		mmc_request_done(host->mmc, data->mrq);
		return;
	}
	mmc_omap_start_command(host, data->stop, NULL);
}

/*
 * Notify the core about command completion
 */
static void
mmc_omap_cmd_done(struct mmc_omap_host *host, struct mmc_command *cmd)
{
	host->cmd = NULL;

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* response type 2 */
			cmd->resp[3] = OMAP_HSMMC_READ(host->base, RSP10);
			cmd->resp[2] = OMAP_HSMMC_READ(host->base, RSP32);
			cmd->resp[1] = OMAP_HSMMC_READ(host->base, RSP54);
			cmd->resp[0] = OMAP_HSMMC_READ(host->base, RSP76);
		} else {
			/* response types 1, 1b, 3, 4, 5, 6 */
			cmd->resp[0] = OMAP_HSMMC_READ(host->base, RSP10);
		}
	}
	if (host->data == NULL || cmd->error) {
		host->mrq = NULL;
		mmc_request_done(host->mmc, cmd->mrq);
	}
}

/*
 * DMA clean up for command errors
 */
static void mmc_dma_cleanup(struct mmc_omap_host *host)
{
	host->data->error = -ETIMEDOUT;

	if (host->use_dma && host->dma_ch != -1) {
		dma_unmap_sg(mmc_dev(host->mmc), host->data->sg, host->dma_len,
			host->dma_dir);
		omap_free_dma(host->dma_ch);
		host->dma_ch = -1;
		up(&host->sem);
	}
	host->data = NULL;
	host->datadir = OMAP_MMC_DATADIR_NONE;
}

/*
 * Readable error output
 */
#ifdef CONFIG_MMC_DEBUG
static void mmc_omap_report_irq(struct mmc_omap_host *host, u32 status)
{
	/* --- means reserved bit without definition at documentation */
	static const char *mmc_omap_status_bits[] = {
		"CC", "TC", "BGE", "---", "BWR", "BRR", "---", "---", "CIRQ",
		"OBI", "---", "---", "---", "---", "---", "ERRI", "CTO", "CCRC",
		"CEB", "CIE", "DTO", "DCRC", "DEB", "---", "ACE", "---",
		"---", "---", "---", "CERR", "CERR", "BADA", "---", "---", "---"
	};
	char res[256];
	char *buf = res;
	int len, i;

	len = sprintf(buf, "MMC IRQ 0x%x :", status);
	buf += len;

	for (i = 0; i < ARRAY_SIZE(mmc_omap_status_bits); i++)
		if (status & (1 << i)) {
			len = sprintf(buf, " %s", mmc_omap_status_bits[i]);
			buf += len;
		}

	dev_dbg(mmc_dev(host->mmc), "%s\n", res);
}
#endif  /* CONFIG_MMC_DEBUG */


/*
 * MMC controller IRQ handler
 */
static irqreturn_t mmc_omap_irq(int irq, void *dev_id)
{
	struct mmc_omap_host *host = dev_id;
	struct mmc_data *data;
	int end_cmd = 0, end_trans = 0, status;

	if (host->cmd == NULL && host->data == NULL) {
		OMAP_HSMMC_WRITE(host->base, STAT,
			OMAP_HSMMC_READ(host->base, STAT));
		return IRQ_HANDLED;
	}

	data = host->data;
	status = OMAP_HSMMC_READ(host->base, STAT);
	dev_dbg(mmc_dev(host->mmc), "IRQ Status is %x\n", status);

	if (status & ERR) {
#ifdef CONFIG_MMC_DEBUG
		mmc_omap_report_irq(host, status);
#endif
		if ((status & CMD_TIMEOUT) ||
			(status & CMD_CRC)) {
			if (host->cmd) {
				if (status & CMD_TIMEOUT) {
					OMAP_HSMMC_WRITE(host->base, SYSCTL,
						OMAP_HSMMC_READ(host->base,
								SYSCTL) | SRC);
					while (OMAP_HSMMC_READ(host->base,
							SYSCTL) & SRC)
						;

					host->cmd->error = -ETIMEDOUT;
				} else {
					host->cmd->error = -EILSEQ;
				}
				end_cmd = 1;
			}
			if (host->data)
				mmc_dma_cleanup(host);
		}
		if ((status & DATA_TIMEOUT) ||
			(status & DATA_CRC)) {
			if (host->data) {
				if (status & DATA_TIMEOUT)
					mmc_dma_cleanup(host);
				else
					host->data->error = -EILSEQ;
				OMAP_HSMMC_WRITE(host->base, SYSCTL,
					OMAP_HSMMC_READ(host->base,
							SYSCTL) | SRD);
				while (OMAP_HSMMC_READ(host->base,
						SYSCTL) & SRD)
					;
				end_trans = 1;
			}
		}
		if (status & CARD_ERR) {
			dev_dbg(mmc_dev(host->mmc),
				"Ignoring card err CMD%d\n", host->cmd->opcode);
			if (host->cmd)
				end_cmd = 1;
			if (host->data)
				end_trans = 1;
		}
	}

	OMAP_HSMMC_WRITE(host->base, STAT, status);

	if (end_cmd || (status & CC))
		mmc_omap_cmd_done(host, host->cmd);
	if (end_trans || (status & TC))
		mmc_omap_xfer_done(host, data);

	return IRQ_HANDLED;
}

/*
 * Switch MMC operating voltage
 */
static int omap_mmc_switch_opcond(struct mmc_omap_host *host, int vdd)
{
	u32 reg_val = 0;
	int ret;

	/* Disable the clocks */
	clk_disable(host->fclk);
	clk_disable(host->iclk);
	clk_disable(host->dbclk);

	/* Turn the power off */
	ret = mmc_slot(host).set_power(host->dev, host->slot_id, 0, 0);
	if (ret != 0)
		goto err;

	/* Turn the power ON with given VDD 1.8 or 3.0v */
	ret = mmc_slot(host).set_power(host->dev, host->slot_id, 1, vdd);
	if (ret != 0)
		goto err;

	clk_enable(host->fclk);
	clk_enable(host->iclk);
	clk_enable(host->dbclk);

	OMAP_HSMMC_WRITE(host->base, HCTL,
		OMAP_HSMMC_READ(host->base, HCTL) & SDVSCLR);
	reg_val = OMAP_HSMMC_READ(host->base, HCTL);
	/*
	 * If a MMC dual voltage card is detected, the set_ios fn calls
	 * this fn with VDD bit set for 1.8V. Upon card removal from the
	 * slot, omap_mmc_set_ios sets the VDD back to 3V on MMC_POWER_OFF.
	 *
	 * Only MMC1 supports 3.0V.  MMC2 will not function if SDVS30 is
	 * set in HCTL.
	 */
	if (host->id == OMAP_MMC1_DEVID && (((1 << vdd) == MMC_VDD_32_33) ||
				((1 << vdd) == MMC_VDD_33_34)))
		reg_val |= SDVS30;
	if ((1 << vdd) == MMC_VDD_165_195)
		reg_val |= SDVS18;

	OMAP_HSMMC_WRITE(host->base, HCTL, reg_val);

	OMAP_HSMMC_WRITE(host->base, HCTL,
		OMAP_HSMMC_READ(host->base, HCTL) | SDBP);

	return 0;
err:
	dev_dbg(mmc_dev(host->mmc), "Unable to switch operating voltage\n");
	return ret;
}

/*
 * Work Item to notify the core about card insertion/removal
 */
static void mmc_omap_detect(struct work_struct *work)
{
	struct mmc_omap_host *host = container_of(work, struct mmc_omap_host,
						mmc_carddetect_work);

	sysfs_notify(&host->mmc->class_dev.kobj, NULL, "cover_switch");
	if (host->carddetect) {
		mmc_detect_change(host->mmc, (HZ * 200) / 1000);
	} else {
		OMAP_HSMMC_WRITE(host->base, SYSCTL,
			OMAP_HSMMC_READ(host->base, SYSCTL) | SRD);
		while (OMAP_HSMMC_READ(host->base, SYSCTL) & SRD)
			;

		mmc_detect_change(host->mmc, (HZ * 50) / 1000);
	}
}

/*
 * ISR for handling card insertion and removal
 */
static irqreturn_t omap_mmc_cd_handler(int irq, void *dev_id)
{
	struct mmc_omap_host *host = (struct mmc_omap_host *)dev_id;

	host->carddetect = mmc_slot(host).card_detect(irq);
	schedule_work(&host->mmc_carddetect_work);

	return IRQ_HANDLED;
}

/*
 * DMA call back function
 */
static void mmc_omap_dma_cb(int lch, u16 ch_status, void *data)
{
	struct mmc_omap_host *host = data;

	if (ch_status & OMAP2_DMA_MISALIGNED_ERR_IRQ)
		dev_dbg(mmc_dev(host->mmc), "MISALIGNED_ADRS_ERR\n");

	if (host->dma_ch < 0)
		return;

	omap_free_dma(host->dma_ch);
	host->dma_ch = -1;
	/*
	 * DMA Callback: run in interrupt context.
	 * mutex_unlock will through a kernel warning if used.
	 */
	up(&host->sem);
}

/*
 * Configure dma src and destination parameters
 */
static int mmc_omap_config_dma_param(int sync_dir, struct mmc_omap_host *host,
				struct mmc_data *data)
{
	if (sync_dir == 0) {
		omap_set_dma_dest_params(host->dma_ch, 0,
			OMAP_DMA_AMODE_CONSTANT,
			(host->mapbase + OMAP_HSMMC_DATA), 0, 0);
		omap_set_dma_src_params(host->dma_ch, 0,
			OMAP_DMA_AMODE_POST_INC,
			sg_dma_address(&data->sg[0]), 0, 0);
	} else {
		omap_set_dma_src_params(host->dma_ch, 0,
			OMAP_DMA_AMODE_CONSTANT,
			(host->mapbase + OMAP_HSMMC_DATA), 0, 0);
		omap_set_dma_dest_params(host->dma_ch, 0,
			OMAP_DMA_AMODE_POST_INC,
			sg_dma_address(&data->sg[0]), 0, 0);
	}
	return 0;
}
/*
 * Routine to configure and start DMA for the MMC card
 */
static int
mmc_omap_start_dma_transfer(struct mmc_omap_host *host, struct mmc_request *req)
{
	int sync_dev, sync_dir = 0;
	int dma_ch = 0, ret = 0, err = 1;
	struct mmc_data *data = req->data;

	/*
	 * If for some reason the DMA transfer is still active,
	 * we wait for timeout period and free the dma
	 */
	if (host->dma_ch != -1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(100);
		if (down_trylock(&host->sem)) {
			omap_free_dma(host->dma_ch);
			host->dma_ch = -1;
			up(&host->sem);
			return err;
		}
	} else {
		if (down_trylock(&host->sem))
			return err;
	}

	if (!(data->flags & MMC_DATA_WRITE)) {
		host->dma_dir = DMA_FROM_DEVICE;
		if (host->id == OMAP_MMC1_DEVID)
			sync_dev = OMAP24XX_DMA_MMC1_RX;
		else
			sync_dev = OMAP24XX_DMA_MMC2_RX;
	} else {
		host->dma_dir = DMA_TO_DEVICE;
		if (host->id == OMAP_MMC1_DEVID)
			sync_dev = OMAP24XX_DMA_MMC1_TX;
		else
			sync_dev = OMAP24XX_DMA_MMC2_TX;
	}

	ret = omap_request_dma(sync_dev, "MMC/SD", mmc_omap_dma_cb,
			host, &dma_ch);
	if (ret != 0) {
		dev_dbg(mmc_dev(host->mmc),
			"%s: omap_request_dma() failed with %d\n",
			mmc_hostname(host->mmc), ret);
		return ret;
	}

	host->dma_len = dma_map_sg(mmc_dev(host->mmc), data->sg,
			data->sg_len, host->dma_dir);
	host->dma_ch = dma_ch;

	if (!(data->flags & MMC_DATA_WRITE))
		mmc_omap_config_dma_param(1, host, data);
	else
		mmc_omap_config_dma_param(0, host, data);

	if ((data->blksz % 4) == 0)
		omap_set_dma_transfer_params(dma_ch, OMAP_DMA_DATA_TYPE_S32,
			(data->blksz / 4), data->blocks, OMAP_DMA_SYNC_FRAME,
			sync_dev, sync_dir);
	else
		/* REVISIT: The MMC buffer increments only when MSB is written.
		 * Return error for blksz which is non multiple of four.
		 */
		return -EINVAL;

	omap_start_dma(dma_ch);
	return 0;
}

static void set_data_timeout(struct mmc_omap_host *host,
			     struct mmc_request *req)
{
	unsigned int timeout, cycle_ns;
	uint32_t reg, clkd, dto = 0;

	reg = OMAP_HSMMC_READ(host->base, SYSCTL);
	clkd = (reg & CLKD_MASK) >> CLKD_SHIFT;
	if (clkd == 0)
		clkd = 1;

	cycle_ns = 1000000000 / (clk_get_rate(host->fclk) / clkd);
	timeout = req->data->timeout_ns / cycle_ns;
	timeout += req->data->timeout_clks;
	if (timeout) {
		while ((timeout & 0x80000000) == 0) {
			dto += 1;
			timeout <<= 1;
		}
		dto = 31 - dto;
		timeout <<= 1;
		if (timeout && dto)
			dto += 1;
		if (dto >= 13)
			dto -= 13;
		else
			dto = 0;
		if (dto > 14)
			dto = 14;
	}

	reg &= ~DTO_MASK;
	reg |= dto << DTO_SHIFT;
	OMAP_HSMMC_WRITE(host->base, SYSCTL, reg);
}

/*
 * Configure block length for MMC/SD cards and initiate the transfer.
 */
static int
mmc_omap_prepare_data(struct mmc_omap_host *host, struct mmc_request *req)
{
	int ret;
	host->data = req->data;

	if (req->data == NULL) {
		host->datadir = OMAP_MMC_DATADIR_NONE;
		OMAP_HSMMC_WRITE(host->base, BLK, 0);
		return 0;
	}

	OMAP_HSMMC_WRITE(host->base, BLK, (req->data->blksz)
					| (req->data->blocks << 16));
	set_data_timeout(host, req);

	host->datadir = (req->data->flags & MMC_DATA_WRITE) ?
			OMAP_MMC_DATADIR_WRITE : OMAP_MMC_DATADIR_READ;

	if (host->use_dma) {
		ret = mmc_omap_start_dma_transfer(host, req);
		if (ret != 0) {
			dev_dbg(mmc_dev(host->mmc), "MMC start dma failure\n");
			return ret;
		}
	}
	return 0;
}

/*
 * Request function. for read/write operation
 */
static void omap_mmc_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct mmc_omap_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);
	host->mrq = req;
	mmc_omap_prepare_data(host, req);
	mmc_omap_start_command(host, req->cmd, req->data);
}


/* Routine to configure clock values. Exposed API to core */
static void omap_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmc_omap_host *host = mmc_priv(mmc);
	u16 dsor = 0;
	unsigned long regval;
	unsigned long timeout;

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		mmc_slot(host).set_power(host->dev, host->slot_id, 0, 0);
		/*
		 * Reset bus voltage to 3V if it got set to 1.8V earlier.
		 * REVISIT: If we are able to detect cards after unplugging
		 * a 1.8V card, this code should not be needed.
		 */
		if (!(OMAP_HSMMC_READ(host->base, HCTL) & SDVSDET)) {
			int vdd = fls(host->mmc->ocr_avail) - 1;
			if (omap_mmc_switch_opcond(host, vdd) != 0)
				host->mmc->ios.vdd = vdd;
		}
		break;
	case MMC_POWER_UP:
		mmc_slot(host).set_power(host->dev, host->slot_id, 1, ios->vdd);
		break;
	}

	switch (mmc->ios.bus_width) {
	case MMC_BUS_WIDTH_4:
		OMAP_HSMMC_WRITE(host->base, HCTL,
			OMAP_HSMMC_READ(host->base, HCTL) | FOUR_BIT);
		break;
	case MMC_BUS_WIDTH_1:
		OMAP_HSMMC_WRITE(host->base, HCTL,
			OMAP_HSMMC_READ(host->base, HCTL) & ~FOUR_BIT);
		break;
	}

	if (host->id == OMAP_MMC1_DEVID) {
		/* Only MMC1 can operate at 3V/1.8V */
		if ((OMAP_HSMMC_READ(host->base, HCTL) & SDVSDET) &&
			(ios->vdd == DUAL_VOLT_OCR_BIT)) {
				/*
				 * The mmc_select_voltage fn of the core does
				 * not seem to set the power_mode to
				 * MMC_POWER_UP upon recalculating the voltage.
				 * vdd 1.8v.
				 */
				if (omap_mmc_switch_opcond(host, ios->vdd) != 0)
					dev_dbg(mmc_dev(host->mmc),
						"Switch operation failed\n");
		}
	}

	if (ios->clock) {
		dsor = OMAP_MMC_MASTER_CLOCK / ios->clock;
		if (dsor < 1)
			dsor = 1;

		if (OMAP_MMC_MASTER_CLOCK / dsor > ios->clock)
			dsor++;

		if (dsor > 250)
			dsor = 250;
	}
	omap_mmc_stop_clock(host);
	regval = OMAP_HSMMC_READ(host->base, SYSCTL);
	regval = regval & ~(CLKD_MASK);
	regval = regval | (dsor << 6) | (DTO << 16);
	OMAP_HSMMC_WRITE(host->base, SYSCTL, regval);
	OMAP_HSMMC_WRITE(host->base, SYSCTL,
		OMAP_HSMMC_READ(host->base, SYSCTL) | ICE);

	/* Wait till the ICS bit is set */
	timeout = jiffies + msecs_to_jiffies(MMC_TIMEOUT_MS);
	while ((OMAP_HSMMC_READ(host->base, SYSCTL) & ICS) != 0x2
		&& time_before(jiffies, timeout))
		msleep(1);

	OMAP_HSMMC_WRITE(host->base, SYSCTL,
		OMAP_HSMMC_READ(host->base, SYSCTL) | CEN);

	if (ios->power_mode == MMC_POWER_ON)
		send_init_stream(host);

	if (ios->bus_mode == MMC_BUSMODE_OPENDRAIN)
		OMAP_HSMMC_WRITE(host->base, CON,
				OMAP_HSMMC_READ(host->base, CON) | OD);
}

static int omap_hsmmc_get_cd(struct mmc_host *mmc)
{
	struct mmc_omap_host *host = mmc_priv(mmc);
	struct omap_mmc_platform_data *pdata = host->pdata;

	if (!pdata->slots[0].card_detect)
		return -ENOSYS;
	return pdata->slots[0].card_detect(pdata->slots[0].card_detect_irq);
}

static int omap_hsmmc_get_ro(struct mmc_host *mmc)
{
	struct mmc_omap_host *host = mmc_priv(mmc);
	struct omap_mmc_platform_data *pdata = host->pdata;

	if (!pdata->slots[0].get_ro)
		return -ENOSYS;
	return pdata->slots[0].get_ro(host->dev, 0);
}

static struct mmc_host_ops mmc_omap_ops = {
	.request = omap_mmc_request,
	.set_ios = omap_mmc_set_ios,
	.get_cd = omap_hsmmc_get_cd,
	.get_ro = omap_hsmmc_get_ro,
	/* NYET -- enable_sdio_irq */
};

static int __init omap_mmc_probe(struct platform_device *pdev)
{
	struct omap_mmc_platform_data *pdata = pdev->dev.platform_data;
	struct mmc_host *mmc;
	struct mmc_omap_host *host = NULL;
	struct resource *res;
	int ret = 0, irq;
	u32 hctl, capa;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "Platform Data is missing\n");
		return -ENXIO;
	}

	if (pdata->nr_slots == 0) {
		dev_err(&pdev->dev, "No Slots\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (res == NULL || irq < 0)
		return -ENXIO;

	res = request_mem_region(res->start, res->end - res->start + 1,
							pdev->name);
	if (res == NULL)
		return -EBUSY;

	mmc = mmc_alloc_host(sizeof(struct mmc_omap_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto err;
	}

	host		= mmc_priv(mmc);
	host->mmc	= mmc;
	host->pdata	= pdata;
	host->dev	= &pdev->dev;
	host->use_dma	= 1;
	host->dev->dma_mask = &pdata->dma_mask;
	host->dma_ch	= -1;
	host->irq	= irq;
	host->id	= pdev->id;
	host->slot_id	= 0;
	host->mapbase	= res->start;
	host->base	= ioremap(host->mapbase, SZ_4K);

	platform_set_drvdata(pdev, host);
	INIT_WORK(&host->mmc_carddetect_work, mmc_omap_detect);

	mmc->ops	= &mmc_omap_ops;
	mmc->f_min	= 400000;
	mmc->f_max	= 52000000;

	sema_init(&host->sem, 1);

	host->iclk = clk_get(&pdev->dev, "mmchs_ick");
	if (IS_ERR(host->iclk)) {
		ret = PTR_ERR(host->iclk);
		host->iclk = NULL;
		goto err1;
	}
	host->fclk = clk_get(&pdev->dev, "mmchs_fck");
	if (IS_ERR(host->fclk)) {
		ret = PTR_ERR(host->fclk);
		host->fclk = NULL;
		clk_put(host->iclk);
		goto err1;
	}

	if (clk_enable(host->fclk) != 0) {
		clk_put(host->iclk);
		clk_put(host->fclk);
		goto err1;
	}

	if (clk_enable(host->iclk) != 0) {
		clk_disable(host->fclk);
		clk_put(host->iclk);
		clk_put(host->fclk);
		goto err1;
	}

	host->dbclk = clk_get(&pdev->dev, "mmchsdb_fck");
	/*
	 * MMC can still work without debounce clock.
	 */
	if (IS_ERR(host->dbclk))
		dev_warn(mmc_dev(host->mmc), "Failed to get debounce clock\n");
	else
		if (clk_enable(host->dbclk) != 0)
			dev_dbg(mmc_dev(host->mmc), "Enabling debounce"
							" clk failed\n");
		else
			host->dbclk_enabled = 1;

#ifdef CONFIG_MMC_BLOCK_BOUNCE
	mmc->max_phys_segs = 1;
	mmc->max_hw_segs = 1;
#endif
	mmc->max_blk_size = 512;       /* Block Length at max can be 1024 */
	mmc->max_blk_count = 0xFFFF;    /* No. of Blocks is 16 bits */
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;

	mmc->ocr_avail = mmc_slot(host).ocr_mask;
	mmc->caps |= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;

	if (pdata->slots[host->slot_id].wires >= 4)
		mmc->caps |= MMC_CAP_4_BIT_DATA;

	/* Only MMC1 supports 3.0V */
	if (host->id == OMAP_MMC1_DEVID) {
		hctl = SDVS30;
		capa = VS30 | VS18;
	} else {
		hctl = SDVS18;
		capa = VS18;
	}

	OMAP_HSMMC_WRITE(host->base, HCTL,
			OMAP_HSMMC_READ(host->base, HCTL) | hctl);

	OMAP_HSMMC_WRITE(host->base, CAPA,
			OMAP_HSMMC_READ(host->base, CAPA) | capa);

	/* Set the controller to AUTO IDLE mode */
	OMAP_HSMMC_WRITE(host->base, SYSCONFIG,
			OMAP_HSMMC_READ(host->base, SYSCONFIG) | AUTOIDLE);

	/* Set SD bus power bit */
	OMAP_HSMMC_WRITE(host->base, HCTL,
			OMAP_HSMMC_READ(host->base, HCTL) | SDBP);

	/* Request IRQ for MMC operations */
	ret = request_irq(host->irq, mmc_omap_irq, IRQF_DISABLED,
			mmc_hostname(mmc), host);
	if (ret) {
		dev_dbg(mmc_dev(host->mmc), "Unable to grab HSMMC IRQ\n");
		goto err_irq;
	}

	if (pdata->init != NULL) {
		if (pdata->init(&pdev->dev) != 0) {
			dev_dbg(mmc_dev(host->mmc),
				"Unable to configure MMC IRQs\n");
			goto err_irq_cd_init;
		}
	}

	/* Request IRQ for card detect */
	if ((mmc_slot(host).card_detect_irq) && (mmc_slot(host).card_detect)) {
		ret = request_irq(mmc_slot(host).card_detect_irq,
				  omap_mmc_cd_handler,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
					  | IRQF_DISABLED,
				  mmc_hostname(mmc), host);
		if (ret) {
			dev_dbg(mmc_dev(host->mmc),
				"Unable to grab MMC CD IRQ\n");
			goto err_irq_cd;
		}
	}

	OMAP_HSMMC_WRITE(host->base, ISE, INT_EN_MASK);
	OMAP_HSMMC_WRITE(host->base, IE, INT_EN_MASK);

	mmc_add_host(mmc);

	if (host->pdata->slots[host->slot_id].name != NULL) {
		ret = device_create_file(&mmc->class_dev, &dev_attr_slot_name);
		if (ret < 0)
			goto err_slot_name;
	}
	if (mmc_slot(host).card_detect_irq && mmc_slot(host).card_detect &&
			host->pdata->slots[host->slot_id].get_cover_state) {
		ret = device_create_file(&mmc->class_dev,
					&dev_attr_cover_switch);
		if (ret < 0)
			goto err_cover_switch;
	}

	return 0;

err_cover_switch:
	device_remove_file(&mmc->class_dev, &dev_attr_cover_switch);
err_slot_name:
	mmc_remove_host(mmc);
err_irq_cd:
	free_irq(mmc_slot(host).card_detect_irq, host);
err_irq_cd_init:
	free_irq(host->irq, host);
err_irq:
	clk_disable(host->fclk);
	clk_disable(host->iclk);
	clk_put(host->fclk);
	clk_put(host->iclk);
	if (host->dbclk_enabled) {
		clk_disable(host->dbclk);
		clk_put(host->dbclk);
	}

err1:
	iounmap(host->base);
err:
	dev_dbg(mmc_dev(host->mmc), "Probe Failed\n");
	release_mem_region(res->start, res->end - res->start + 1);
	if (host)
		mmc_free_host(mmc);
	return ret;
}

static int omap_mmc_remove(struct platform_device *pdev)
{
	struct mmc_omap_host *host = platform_get_drvdata(pdev);
	struct resource *res;

	if (host) {
		mmc_remove_host(host->mmc);
		if (host->pdata->cleanup)
			host->pdata->cleanup(&pdev->dev);
		free_irq(host->irq, host);
		if (mmc_slot(host).card_detect_irq)
			free_irq(mmc_slot(host).card_detect_irq, host);
		flush_scheduled_work();

		clk_disable(host->fclk);
		clk_disable(host->iclk);
		clk_put(host->fclk);
		clk_put(host->iclk);
		if (host->dbclk_enabled) {
			clk_disable(host->dbclk);
			clk_put(host->dbclk);
		}

		mmc_free_host(host->mmc);
		iounmap(host->base);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, res->end - res->start + 1);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int omap_mmc_suspend(struct platform_device *pdev, pm_message_t state)
{
	int ret = 0;
	struct mmc_omap_host *host = platform_get_drvdata(pdev);

	if (host && host->suspended)
		return 0;

	if (host) {
		ret = mmc_suspend_host(host->mmc, state);
		if (ret == 0) {
			host->suspended = 1;

			OMAP_HSMMC_WRITE(host->base, ISE, 0);
			OMAP_HSMMC_WRITE(host->base, IE, 0);

			if (host->pdata->suspend) {
				ret = host->pdata->suspend(&pdev->dev,
								host->slot_id);
				if (ret)
					dev_dbg(mmc_dev(host->mmc),
						"Unable to handle MMC board"
						" level suspend\n");
			}

			if (!(OMAP_HSMMC_READ(host->base, HCTL) & SDVSDET)) {
				OMAP_HSMMC_WRITE(host->base, HCTL,
					OMAP_HSMMC_READ(host->base, HCTL)
					& SDVSCLR);
				OMAP_HSMMC_WRITE(host->base, HCTL,
					OMAP_HSMMC_READ(host->base, HCTL)
					| SDVS30);
				OMAP_HSMMC_WRITE(host->base, HCTL,
					OMAP_HSMMC_READ(host->base, HCTL)
					| SDBP);
			}

			clk_disable(host->fclk);
			clk_disable(host->iclk);
			clk_disable(host->dbclk);
		}

	}
	return ret;
}

/* Routine to resume the MMC device */
static int omap_mmc_resume(struct platform_device *pdev)
{
	int ret = 0;
	struct mmc_omap_host *host = platform_get_drvdata(pdev);

	if (host && !host->suspended)
		return 0;

	if (host) {

		ret = clk_enable(host->fclk);
		if (ret)
			goto clk_en_err;

		ret = clk_enable(host->iclk);
		if (ret) {
			clk_disable(host->fclk);
			clk_put(host->fclk);
			goto clk_en_err;
		}

		if (clk_enable(host->dbclk) != 0)
			dev_dbg(mmc_dev(host->mmc),
					"Enabling debounce clk failed\n");

		if (host->pdata->resume) {
			ret = host->pdata->resume(&pdev->dev, host->slot_id);
			if (ret)
				dev_dbg(mmc_dev(host->mmc),
					"Unmask interrupt failed\n");
		}

		/* Notify the core to resume the host */
		ret = mmc_resume_host(host->mmc);
		if (ret == 0)
			host->suspended = 0;
	}

	return ret;

clk_en_err:
	dev_dbg(mmc_dev(host->mmc),
		"Failed to enable MMC clocks during resume\n");
	return ret;
}

#else
#define omap_mmc_suspend	NULL
#define omap_mmc_resume		NULL
#endif

static struct platform_driver omap_mmc_driver = {
	.probe		= omap_mmc_probe,
	.remove		= omap_mmc_remove,
	.suspend	= omap_mmc_suspend,
	.resume		= omap_mmc_resume,
	.driver		= {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init omap_mmc_init(void)
{
	/* Register the MMC driver */
	return platform_driver_register(&omap_mmc_driver);
}

static void __exit omap_mmc_cleanup(void)
{
	/* Unregister MMC driver */
	platform_driver_unregister(&omap_mmc_driver);
}

module_init(omap_mmc_init);
module_exit(omap_mmc_cleanup);

MODULE_DESCRIPTION("OMAP High Speed Multimedia Card driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Texas Instruments Inc");
