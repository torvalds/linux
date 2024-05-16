// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2012 Thomas Langer <thomas.langer@lantiq.com>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <lantiq_soc.h>

#define DRV_NAME		"sflash-falcon"

#define FALCON_SPI_XFER_BEGIN	(1 << 0)
#define FALCON_SPI_XFER_END	(1 << 1)

/* Bus Read Configuration Register0 */
#define BUSRCON0		0x00000010
/* Bus Write Configuration Register0 */
#define BUSWCON0		0x00000018
/* Serial Flash Configuration Register */
#define SFCON			0x00000080
/* Serial Flash Time Register */
#define SFTIME			0x00000084
/* Serial Flash Status Register */
#define SFSTAT			0x00000088
/* Serial Flash Command Register */
#define SFCMD			0x0000008C
/* Serial Flash Address Register */
#define SFADDR			0x00000090
/* Serial Flash Data Register */
#define SFDATA			0x00000094
/* Serial Flash I/O Control Register */
#define SFIO			0x00000098
/* EBU Clock Control Register */
#define EBUCC			0x000000C4

/* Dummy Phase Length */
#define SFCMD_DUMLEN_OFFSET	16
#define SFCMD_DUMLEN_MASK	0x000F0000
/* Chip Select */
#define SFCMD_CS_OFFSET		24
#define SFCMD_CS_MASK		0x07000000
/* field offset */
#define SFCMD_ALEN_OFFSET	20
#define SFCMD_ALEN_MASK		0x00700000
/* SCK Rise-edge Position */
#define SFTIME_SCKR_POS_OFFSET	8
#define SFTIME_SCKR_POS_MASK	0x00000F00
/* SCK Period */
#define SFTIME_SCK_PER_OFFSET	0
#define SFTIME_SCK_PER_MASK	0x0000000F
/* SCK Fall-edge Position */
#define SFTIME_SCKF_POS_OFFSET	12
#define SFTIME_SCKF_POS_MASK	0x0000F000
/* Device Size */
#define SFCON_DEV_SIZE_A23_0	0x03000000
#define SFCON_DEV_SIZE_MASK	0x0F000000
/* Read Data Position */
#define SFTIME_RD_POS_MASK	0x000F0000
/* Data Output */
#define SFIO_UNUSED_WD_MASK	0x0000000F
/* Command Opcode mask */
#define SFCMD_OPC_MASK		0x000000FF
/* dlen bytes of data to write */
#define SFCMD_DIR_WRITE		0x00000100
/* Data Length offset */
#define SFCMD_DLEN_OFFSET	9
/* Command Error */
#define SFSTAT_CMD_ERR		0x20000000
/* Access Command Pending */
#define SFSTAT_CMD_PEND		0x00400000
/* Frequency set to 100MHz. */
#define EBUCC_EBUDIV_SELF100	0x00000001
/* Serial Flash */
#define BUSRCON0_AGEN_SERIAL_FLASH	0xF0000000
/* 8-bit multiplexed */
#define BUSRCON0_PORTW_8_BIT_MUX	0x00000000
/* Serial Flash */
#define BUSWCON0_AGEN_SERIAL_FLASH	0xF0000000
/* Chip Select after opcode */
#define SFCMD_KEEP_CS_KEEP_SELECTED	0x00008000

#define CLOCK_100M	100000000
#define CLOCK_50M	50000000

struct falcon_sflash {
	u32 sfcmd; /* for caching of opcode, direction, ... */
	struct spi_controller *host;
};

int falcon_sflash_xfer(struct spi_device *spi, struct spi_transfer *t,
		unsigned long flags)
{
	struct device *dev = &spi->dev;
	struct falcon_sflash *priv = spi_controller_get_devdata(spi->controller);
	const u8 *txp = t->tx_buf;
	u8 *rxp = t->rx_buf;
	unsigned int bytelen = ((8 * t->len + 7) / 8);
	unsigned int len, alen, dumlen;
	u32 val;
	enum {
		state_init,
		state_command_prepare,
		state_write,
		state_read,
		state_disable_cs,
		state_end
	} state = state_init;

	do {
		switch (state) {
		case state_init: /* detect phase of upper layer sequence */
		{
			/* initial write ? */
			if (flags & FALCON_SPI_XFER_BEGIN) {
				if (!txp) {
					dev_err(dev,
						"BEGIN without tx data!\n");
					return -ENODATA;
				}
				/*
				 * Prepare the parts of the sfcmd register,
				 * which should not change during a sequence!
				 * Only exception are the length fields,
				 * especially alen and dumlen.
				 */

				priv->sfcmd = ((spi_get_chipselect(spi, 0)
						<< SFCMD_CS_OFFSET)
					       & SFCMD_CS_MASK);
				priv->sfcmd |= SFCMD_KEEP_CS_KEEP_SELECTED;
				priv->sfcmd |= *txp;
				txp++;
				bytelen--;
				if (bytelen) {
					/*
					 * more data:
					 * maybe address and/or dummy
					 */
					state = state_command_prepare;
					break;
				} else {
					dev_dbg(dev, "write cmd %02X\n",
						priv->sfcmd & SFCMD_OPC_MASK);
				}
			}
			/* continued write ? */
			if (txp && bytelen) {
				state = state_write;
				break;
			}
			/* read data? */
			if (rxp && bytelen) {
				state = state_read;
				break;
			}
			/* end of sequence? */
			if (flags & FALCON_SPI_XFER_END)
				state = state_disable_cs;
			else
				state = state_end;
			break;
		}
		/* collect tx data for address and dummy phase */
		case state_command_prepare:
		{
			/* txp is valid, already checked */
			val = 0;
			alen = 0;
			dumlen = 0;
			while (bytelen > 0) {
				if (alen < 3) {
					val = (val << 8) | (*txp++);
					alen++;
				} else if ((dumlen < 15) && (*txp == 0)) {
					/*
					 * assume dummy bytes are set to 0
					 * from upper layer
					 */
					dumlen++;
					txp++;
				} else {
					break;
				}
				bytelen--;
			}
			priv->sfcmd &= ~(SFCMD_ALEN_MASK | SFCMD_DUMLEN_MASK);
			priv->sfcmd |= (alen << SFCMD_ALEN_OFFSET) |
					 (dumlen << SFCMD_DUMLEN_OFFSET);
			if (alen > 0)
				ltq_ebu_w32(val, SFADDR);

			dev_dbg(dev, "wr %02X, alen=%d (addr=%06X) dlen=%d\n",
				priv->sfcmd & SFCMD_OPC_MASK,
				alen, val, dumlen);

			if (bytelen > 0) {
				/* continue with write */
				state = state_write;
			} else if (flags & FALCON_SPI_XFER_END) {
				/* end of sequence? */
				state = state_disable_cs;
			} else {
				/*
				 * go to end and expect another
				 * call (read or write)
				 */
				state = state_end;
			}
			break;
		}
		case state_write:
		{
			/* txp still valid */
			priv->sfcmd |= SFCMD_DIR_WRITE;
			len = 0;
			val = 0;
			do {
				if (bytelen--)
					val |= (*txp++) << (8 * len++);
				if ((flags & FALCON_SPI_XFER_END)
				    && (bytelen == 0)) {
					priv->sfcmd &=
						~SFCMD_KEEP_CS_KEEP_SELECTED;
				}
				if ((len == 4) || (bytelen == 0)) {
					ltq_ebu_w32(val, SFDATA);
					ltq_ebu_w32(priv->sfcmd
						| (len<<SFCMD_DLEN_OFFSET),
						SFCMD);
					len = 0;
					val = 0;
					priv->sfcmd &= ~(SFCMD_ALEN_MASK
							 | SFCMD_DUMLEN_MASK);
				}
			} while (bytelen);
			state = state_end;
			break;
		}
		case state_read:
		{
			/* read data */
			priv->sfcmd &= ~SFCMD_DIR_WRITE;
			do {
				if ((flags & FALCON_SPI_XFER_END)
				    && (bytelen <= 4)) {
					priv->sfcmd &=
						~SFCMD_KEEP_CS_KEEP_SELECTED;
				}
				len = (bytelen > 4) ? 4 : bytelen;
				bytelen -= len;
				ltq_ebu_w32(priv->sfcmd
					| (len << SFCMD_DLEN_OFFSET), SFCMD);
				priv->sfcmd &= ~(SFCMD_ALEN_MASK
						 | SFCMD_DUMLEN_MASK);
				do {
					val = ltq_ebu_r32(SFSTAT);
					if (val & SFSTAT_CMD_ERR) {
						/* reset error status */
						dev_err(dev, "SFSTAT: CMD_ERR");
						dev_err(dev, " (%x)\n", val);
						ltq_ebu_w32(SFSTAT_CMD_ERR,
							SFSTAT);
						return -EBADE;
					}
				} while (val & SFSTAT_CMD_PEND);
				val = ltq_ebu_r32(SFDATA);
				do {
					*rxp = (val & 0xFF);
					rxp++;
					val >>= 8;
					len--;
				} while (len);
			} while (bytelen);
			state = state_end;
			break;
		}
		case state_disable_cs:
		{
			priv->sfcmd &= ~SFCMD_KEEP_CS_KEEP_SELECTED;
			ltq_ebu_w32(priv->sfcmd | (0 << SFCMD_DLEN_OFFSET),
				SFCMD);
			val = ltq_ebu_r32(SFSTAT);
			if (val & SFSTAT_CMD_ERR) {
				/* reset error status */
				dev_err(dev, "SFSTAT: CMD_ERR (%x)\n", val);
				ltq_ebu_w32(SFSTAT_CMD_ERR, SFSTAT);
				return -EBADE;
			}
			state = state_end;
			break;
		}
		case state_end:
			break;
		}
	} while (state != state_end);

	return 0;
}

static int falcon_sflash_setup(struct spi_device *spi)
{
	unsigned int i;
	unsigned long flags;

	spin_lock_irqsave(&ebu_lock, flags);

	if (spi->max_speed_hz >= CLOCK_100M) {
		/* set EBU clock to 100 MHz */
		ltq_sys1_w32_mask(0, EBUCC_EBUDIV_SELF100, EBUCC);
		i = 1; /* divider */
	} else {
		/* set EBU clock to 50 MHz */
		ltq_sys1_w32_mask(EBUCC_EBUDIV_SELF100, 0, EBUCC);

		/* search for suitable divider */
		for (i = 1; i < 7; i++) {
			if (CLOCK_50M / i <= spi->max_speed_hz)
				break;
		}
	}

	/* setup period of serial clock */
	ltq_ebu_w32_mask(SFTIME_SCKF_POS_MASK
		     | SFTIME_SCKR_POS_MASK
		     | SFTIME_SCK_PER_MASK,
		     (i << SFTIME_SCKR_POS_OFFSET)
		     | (i << (SFTIME_SCK_PER_OFFSET + 1)),
		     SFTIME);

	/*
	 * set some bits of unused_wd, to not trigger HOLD/WP
	 * signals on non QUAD flashes
	 */
	ltq_ebu_w32((SFIO_UNUSED_WD_MASK & (0x8 | 0x4)), SFIO);

	ltq_ebu_w32(BUSRCON0_AGEN_SERIAL_FLASH | BUSRCON0_PORTW_8_BIT_MUX,
			BUSRCON0);
	ltq_ebu_w32(BUSWCON0_AGEN_SERIAL_FLASH, BUSWCON0);
	/* set address wrap around to maximum for 24-bit addresses */
	ltq_ebu_w32_mask(SFCON_DEV_SIZE_MASK, SFCON_DEV_SIZE_A23_0, SFCON);

	spin_unlock_irqrestore(&ebu_lock, flags);

	return 0;
}

static int falcon_sflash_xfer_one(struct spi_controller *host,
					struct spi_message *m)
{
	struct falcon_sflash *priv = spi_controller_get_devdata(host);
	struct spi_transfer *t;
	unsigned long spi_flags;
	unsigned long flags;
	int ret = 0;

	priv->sfcmd = 0;
	m->actual_length = 0;

	spi_flags = FALCON_SPI_XFER_BEGIN;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (list_is_last(&t->transfer_list, &m->transfers))
			spi_flags |= FALCON_SPI_XFER_END;

		spin_lock_irqsave(&ebu_lock, flags);
		ret = falcon_sflash_xfer(m->spi, t, spi_flags);
		spin_unlock_irqrestore(&ebu_lock, flags);

		if (ret)
			break;

		m->actual_length += t->len;

		WARN_ON(t->delay.value || t->cs_change);
		spi_flags = 0;
	}

	m->status = ret;
	spi_finalize_current_message(host);

	return 0;
}

static int falcon_sflash_probe(struct platform_device *pdev)
{
	struct falcon_sflash *priv;
	struct spi_controller *host;
	int ret;

	host = spi_alloc_host(&pdev->dev, sizeof(*priv));
	if (!host)
		return -ENOMEM;

	priv = spi_controller_get_devdata(host);
	priv->host = host;

	host->mode_bits = SPI_MODE_3;
	host->flags = SPI_CONTROLLER_HALF_DUPLEX;
	host->setup = falcon_sflash_setup;
	host->transfer_one_message = falcon_sflash_xfer_one;
	host->dev.of_node = pdev->dev.of_node;

	ret = devm_spi_register_controller(&pdev->dev, host);
	if (ret)
		spi_controller_put(host);
	return ret;
}

static const struct of_device_id falcon_sflash_match[] = {
	{ .compatible = "lantiq,sflash-falcon" },
	{},
};
MODULE_DEVICE_TABLE(of, falcon_sflash_match);

static struct platform_driver falcon_sflash_driver = {
	.probe	= falcon_sflash_probe,
	.driver = {
		.name	= DRV_NAME,
		.of_match_table = falcon_sflash_match,
	}
};

module_platform_driver(falcon_sflash_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lantiq Falcon SPI/SFLASH controller driver");
