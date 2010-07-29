/*
 *  sdricoh_cs.c - driver for Ricoh Secure Digital Card Readers that can be
 *     found on some Ricoh RL5c476 II cardbus bridge
 *
 *  Copyright (C) 2006 - 2008 Sascha Sommer <saschasommer@freenet.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
#define DEBUG
#define VERBOSE_DEBUG
*/
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/scatterlist.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <linux/io.h>

#include <linux/mmc/host.h>

#define DRIVER_NAME "sdricoh_cs"

static unsigned int switchlocked;

/* i/o region */
#define SDRICOH_PCI_REGION 0
#define SDRICOH_PCI_REGION_SIZE 0x1000

/* registers */
#define R104_VERSION     0x104
#define R200_CMD         0x200
#define R204_CMD_ARG     0x204
#define R208_DATAIO      0x208
#define R20C_RESP        0x20c
#define R21C_STATUS      0x21c
#define R2E0_INIT        0x2e0
#define R2E4_STATUS_RESP 0x2e4
#define R2F0_RESET       0x2f0
#define R224_MODE        0x224
#define R226_BLOCKSIZE   0x226
#define R228_POWER       0x228
#define R230_DATA        0x230

/* flags for the R21C_STATUS register */
#define STATUS_CMD_FINISHED      0x00000001
#define STATUS_TRANSFER_FINISHED 0x00000004
#define STATUS_CARD_INSERTED     0x00000020
#define STATUS_CARD_LOCKED       0x00000080
#define STATUS_CMD_TIMEOUT       0x00400000
#define STATUS_READY_TO_READ     0x01000000
#define STATUS_READY_TO_WRITE    0x02000000
#define STATUS_BUSY              0x40000000

/* timeouts */
#define INIT_TIMEOUT      100
#define CMD_TIMEOUT       100000
#define TRANSFER_TIMEOUT  100000
#define BUSY_TIMEOUT      32767

/* list of supported pcmcia devices */
static struct pcmcia_device_id pcmcia_ids[] = {
	/* vendor and device strings followed by their crc32 hashes */
	PCMCIA_DEVICE_PROD_ID12("RICOH", "Bay1Controller", 0xd9f522ed,
				0xc3901202),
	PCMCIA_DEVICE_PROD_ID12("RICOH", "Bay Controller", 0xd9f522ed,
				0xace80909),
	PCMCIA_DEVICE_NULL,
};

MODULE_DEVICE_TABLE(pcmcia, pcmcia_ids);

/* mmc privdata */
struct sdricoh_host {
	struct device *dev;
	struct mmc_host *mmc;	/* MMC structure */
	unsigned char __iomem *iobase;
	struct pci_dev *pci_dev;
	int app_cmd;
};

/***************** register i/o helper functions *****************************/

static inline unsigned int sdricoh_readl(struct sdricoh_host *host,
					 unsigned int reg)
{
	unsigned int value = readl(host->iobase + reg);
	dev_vdbg(host->dev, "rl %x 0x%x\n", reg, value);
	return value;
}

static inline void sdricoh_writel(struct sdricoh_host *host, unsigned int reg,
				  unsigned int value)
{
	writel(value, host->iobase + reg);
	dev_vdbg(host->dev, "wl %x 0x%x\n", reg, value);

}

static inline unsigned int sdricoh_readw(struct sdricoh_host *host,
					 unsigned int reg)
{
	unsigned int value = readw(host->iobase + reg);
	dev_vdbg(host->dev, "rb %x 0x%x\n", reg, value);
	return value;
}

static inline void sdricoh_writew(struct sdricoh_host *host, unsigned int reg,
					 unsigned short value)
{
	writew(value, host->iobase + reg);
	dev_vdbg(host->dev, "ww %x 0x%x\n", reg, value);
}

static inline unsigned int sdricoh_readb(struct sdricoh_host *host,
					 unsigned int reg)
{
	unsigned int value = readb(host->iobase + reg);
	dev_vdbg(host->dev, "rb %x 0x%x\n", reg, value);
	return value;
}

static int sdricoh_query_status(struct sdricoh_host *host, unsigned int wanted,
				unsigned int timeout){
	unsigned int loop;
	unsigned int status = 0;
	struct device *dev = host->dev;
	for (loop = 0; loop < timeout; loop++) {
		status = sdricoh_readl(host, R21C_STATUS);
		sdricoh_writel(host, R2E4_STATUS_RESP, status);
		if (status & wanted)
			break;
	}

	if (loop == timeout) {
		dev_err(dev, "query_status: timeout waiting for %x\n", wanted);
		return -ETIMEDOUT;
	}

	/* do not do this check in the loop as some commands fail otherwise */
	if (status & 0x7F0000) {
		dev_err(dev, "waiting for status bit %x failed\n", wanted);
		return -EINVAL;
	}
	return 0;

}

static int sdricoh_mmc_cmd(struct sdricoh_host *host, unsigned char opcode,
			   unsigned int arg)
{
	unsigned int status;
	int result = 0;
	unsigned int loop = 0;
	/* reset status reg? */
	sdricoh_writel(host, R21C_STATUS, 0x18);
	/* fill parameters */
	sdricoh_writel(host, R204_CMD_ARG, arg);
	sdricoh_writel(host, R200_CMD, (0x10000 << 8) | opcode);
	/* wait for command completion */
	if (opcode) {
		for (loop = 0; loop < CMD_TIMEOUT; loop++) {
			status = sdricoh_readl(host, R21C_STATUS);
			sdricoh_writel(host, R2E4_STATUS_RESP, status);
			if (status  & STATUS_CMD_FINISHED)
				break;
		}
		/* don't check for timeout in the loop it is not always
		   reset correctly
		*/
		if (loop == CMD_TIMEOUT || status & STATUS_CMD_TIMEOUT)
			result = -ETIMEDOUT;

	}

	return result;

}

static int sdricoh_reset(struct sdricoh_host *host)
{
	dev_dbg(host->dev, "reset\n");
	sdricoh_writel(host, R2F0_RESET, 0x10001);
	sdricoh_writel(host, R2E0_INIT, 0x10000);
	if (sdricoh_readl(host, R2E0_INIT) != 0x10000)
		return -EIO;
	sdricoh_writel(host, R2E0_INIT, 0x10007);

	sdricoh_writel(host, R224_MODE, 0x2000000);
	sdricoh_writel(host, R228_POWER, 0xe0);


	/* status register ? */
	sdricoh_writel(host, R21C_STATUS, 0x18);

	return 0;
}

static int sdricoh_blockio(struct sdricoh_host *host, int read,
				u8 *buf, int len)
{
	int size;
	u32 data = 0;
	/* wait until the data is available */
	if (read) {
		if (sdricoh_query_status(host, STATUS_READY_TO_READ,
						TRANSFER_TIMEOUT))
			return -ETIMEDOUT;
		sdricoh_writel(host, R21C_STATUS, 0x18);
		/* read data */
		while (len) {
			data = sdricoh_readl(host, R230_DATA);
			size = min(len, 4);
			len -= size;
			while (size) {
				*buf = data & 0xFF;
				buf++;
				data >>= 8;
				size--;
			}
		}
	} else {
		if (sdricoh_query_status(host, STATUS_READY_TO_WRITE,
						TRANSFER_TIMEOUT))
			return -ETIMEDOUT;
		sdricoh_writel(host, R21C_STATUS, 0x18);
		/* write data */
		while (len) {
			size = min(len, 4);
			len -= size;
			while (size) {
				data >>= 8;
				data |= (u32)*buf << 24;
				buf++;
				size--;
			}
			sdricoh_writel(host, R230_DATA, data);
		}
	}

	if (len)
		return -EIO;

	return 0;
}

static void sdricoh_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdricoh_host *host = mmc_priv(mmc);
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = cmd->data;
	struct device *dev = host->dev;
	unsigned char opcode = cmd->opcode;
	int i;

	dev_dbg(dev, "=============================\n");
	dev_dbg(dev, "sdricoh_request opcode=%i\n", opcode);

	sdricoh_writel(host, R21C_STATUS, 0x18);

	/* MMC_APP_CMDs need some special handling */
	if (host->app_cmd) {
		opcode |= 64;
		host->app_cmd = 0;
	} else if (opcode == 55)
		host->app_cmd = 1;

	/* read/write commands seem to require this */
	if (data) {
		sdricoh_writew(host, R226_BLOCKSIZE, data->blksz);
		sdricoh_writel(host, R208_DATAIO, 0);
	}

	cmd->error = sdricoh_mmc_cmd(host, opcode, cmd->arg);

	/* read response buffer */
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* CRC is stripped so we need to do some shifting. */
			for (i = 0; i < 4; i++) {
				cmd->resp[i] =
				    sdricoh_readl(host,
						  R20C_RESP + (3 - i) * 4) << 8;
				if (i != 3)
					cmd->resp[i] |=
					    sdricoh_readb(host, R20C_RESP +
							  (3 - i) * 4 - 1);
			}
		} else
			cmd->resp[0] = sdricoh_readl(host, R20C_RESP);
	}

	/* transfer data */
	if (data && cmd->error == 0) {
		dev_dbg(dev, "transfer: blksz %i blocks %i sg_len %i "
			"sg length %i\n", data->blksz, data->blocks,
			data->sg_len, data->sg->length);

		/* enter data reading mode */
		sdricoh_writel(host, R21C_STATUS, 0x837f031e);
		for (i = 0; i < data->blocks; i++) {
			size_t len = data->blksz;
			u8 *buf;
			struct page *page;
			int result;
			page = sg_page(data->sg);

			buf = kmap(page) + data->sg->offset + (len * i);
			result =
				sdricoh_blockio(host,
					data->flags & MMC_DATA_READ, buf, len);
			kunmap(page);
			flush_dcache_page(page);
			if (result) {
				dev_err(dev, "sdricoh_request: cmd %i "
					"block transfer failed\n", cmd->opcode);
				cmd->error = result;
				break;
			} else
				data->bytes_xfered += len;
		}

		sdricoh_writel(host, R208_DATAIO, 1);

		if (sdricoh_query_status(host, STATUS_TRANSFER_FINISHED,
					TRANSFER_TIMEOUT)) {
			dev_err(dev, "sdricoh_request: transfer end error\n");
			cmd->error = -EINVAL;
		}
	}
	/* FIXME check busy flag */

	mmc_request_done(mmc, mrq);
	dev_dbg(dev, "=============================\n");
}

static void sdricoh_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdricoh_host *host = mmc_priv(mmc);
	dev_dbg(host->dev, "set_ios\n");

	if (ios->power_mode == MMC_POWER_ON) {
		sdricoh_writel(host, R228_POWER, 0xc0e0);

		if (ios->bus_width == MMC_BUS_WIDTH_4) {
			sdricoh_writel(host, R224_MODE, 0x2000300);
			sdricoh_writel(host, R228_POWER, 0x40e0);
		} else {
			sdricoh_writel(host, R224_MODE, 0x2000340);
		}

	} else if (ios->power_mode == MMC_POWER_UP) {
		sdricoh_writel(host, R224_MODE, 0x2000320);
		sdricoh_writel(host, R228_POWER, 0xe0);
	}
}

static int sdricoh_get_ro(struct mmc_host *mmc)
{
	struct sdricoh_host *host = mmc_priv(mmc);
	unsigned int status;

	status = sdricoh_readl(host, R21C_STATUS);
	sdricoh_writel(host, R2E4_STATUS_RESP, status);

	/* some notebooks seem to have the locked flag switched */
	if (switchlocked)
		return !(status & STATUS_CARD_LOCKED);

	return (status & STATUS_CARD_LOCKED);
}

static struct mmc_host_ops sdricoh_ops = {
	.request = sdricoh_request,
	.set_ios = sdricoh_set_ios,
	.get_ro = sdricoh_get_ro,
};

/* initialize the control and register it to the mmc framework */
static int sdricoh_init_mmc(struct pci_dev *pci_dev,
			    struct pcmcia_device *pcmcia_dev)
{
	int result = 0;
	void __iomem *iobase = NULL;
	struct mmc_host *mmc = NULL;
	struct sdricoh_host *host = NULL;
	struct device *dev = &pcmcia_dev->dev;
	/* map iomem */
	if (pci_resource_len(pci_dev, SDRICOH_PCI_REGION) !=
	    SDRICOH_PCI_REGION_SIZE) {
		dev_dbg(dev, "unexpected pci resource len\n");
		return -ENODEV;
	}
	iobase =
	    pci_iomap(pci_dev, SDRICOH_PCI_REGION, SDRICOH_PCI_REGION_SIZE);
	if (!iobase) {
		dev_err(dev, "unable to map iobase\n");
		return -ENODEV;
	}
	/* check version? */
	if (readl(iobase + R104_VERSION) != 0x4000) {
		dev_dbg(dev, "no supported mmc controller found\n");
		result = -ENODEV;
		goto err;
	}
	/* allocate privdata */
	mmc = pcmcia_dev->priv =
	    mmc_alloc_host(sizeof(struct sdricoh_host), &pcmcia_dev->dev);
	if (!mmc) {
		dev_err(dev, "mmc_alloc_host failed\n");
		result = -ENOMEM;
		goto err;
	}
	host = mmc_priv(mmc);

	host->iobase = iobase;
	host->dev = dev;
	host->pci_dev = pci_dev;

	mmc->ops = &sdricoh_ops;

	/* FIXME: frequency and voltage handling is done by the controller
	 */
	mmc->f_min = 450000;
	mmc->f_max = 24000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps |= MMC_CAP_4_BIT_DATA;

	mmc->max_seg_size = 1024 * 512;
	mmc->max_blk_size = 512;

	/* reset the controler */
	if (sdricoh_reset(host)) {
		dev_dbg(dev, "could not reset\n");
		result = -EIO;
		goto err;

	}

	result = mmc_add_host(mmc);

	if (!result) {
		dev_dbg(dev, "mmc host registered\n");
		return 0;
	}

err:
	if (iobase)
		pci_iounmap(pci_dev, iobase);
	if (mmc)
		mmc_free_host(mmc);

	return result;
}

/* search for supported mmc controllers */
static int sdricoh_pcmcia_probe(struct pcmcia_device *pcmcia_dev)
{
	struct pci_dev *pci_dev = NULL;

	dev_info(&pcmcia_dev->dev, "Searching MMC controller for pcmcia device"
		" %s %s ...\n", pcmcia_dev->prod_id[0], pcmcia_dev->prod_id[1]);

	/* search pci cardbus bridge that contains the mmc controler */
	/* the io region is already claimed by yenta_socket... */
	while ((pci_dev =
		pci_get_device(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C476,
			       pci_dev))) {
		/* try to init the device */
		if (!sdricoh_init_mmc(pci_dev, pcmcia_dev)) {
			dev_info(&pcmcia_dev->dev, "MMC controller found\n");
			return 0;
		}

	}
	dev_err(&pcmcia_dev->dev, "No MMC controller was found.\n");
	return -ENODEV;
}

static void sdricoh_pcmcia_detach(struct pcmcia_device *link)
{
	struct mmc_host *mmc = link->priv;

	dev_dbg(&link->dev, "detach\n");

	/* remove mmc host */
	if (mmc) {
		struct sdricoh_host *host = mmc_priv(mmc);
		mmc_remove_host(mmc);
		pci_iounmap(host->pci_dev, host->iobase);
		pci_dev_put(host->pci_dev);
		mmc_free_host(mmc);
	}
	pcmcia_disable_device(link);

}

#ifdef CONFIG_PM
static int sdricoh_pcmcia_suspend(struct pcmcia_device *link)
{
	struct mmc_host *mmc = link->priv;
	dev_dbg(&link->dev, "suspend\n");
	mmc_suspend_host(mmc);
	return 0;
}

static int sdricoh_pcmcia_resume(struct pcmcia_device *link)
{
	struct mmc_host *mmc = link->priv;
	dev_dbg(&link->dev, "resume\n");
	sdricoh_reset(mmc_priv(mmc));
	mmc_resume_host(mmc);
	return 0;
}
#else
#define sdricoh_pcmcia_suspend NULL
#define sdricoh_pcmcia_resume NULL
#endif

static struct pcmcia_driver sdricoh_driver = {
	.drv = {
		.name = DRIVER_NAME,
		},
	.probe = sdricoh_pcmcia_probe,
	.remove = sdricoh_pcmcia_detach,
	.id_table = pcmcia_ids,
	.suspend = sdricoh_pcmcia_suspend,
	.resume = sdricoh_pcmcia_resume,
};

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init sdricoh_drv_init(void)
{
	return pcmcia_register_driver(&sdricoh_driver);
}

static void __exit sdricoh_drv_exit(void)
{
	pcmcia_unregister_driver(&sdricoh_driver);
}

module_init(sdricoh_drv_init);
module_exit(sdricoh_drv_exit);

module_param(switchlocked, uint, 0444);

MODULE_AUTHOR("Sascha Sommer <saschasommer@freenet.de>");
MODULE_DESCRIPTION("Ricoh PCMCIA Secure Digital Interface driver");
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(switchlocked, "Switch the cards locked status."
		"Use this when unlocked cards are shown readonly (default 0)");
