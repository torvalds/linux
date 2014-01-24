/*
 * SH RSPI driver
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 *
 * Based on spi-sh.c:
 * Copyright (C) 2011 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/sh_dma.h>
#include <linux/spi/spi.h>
#include <linux/spi/rspi.h>

#define RSPI_SPCR		0x00	/* Control Register */
#define RSPI_SSLP		0x01	/* Slave Select Polarity Register */
#define RSPI_SPPCR		0x02	/* Pin Control Register */
#define RSPI_SPSR		0x03	/* Status Register */
#define RSPI_SPDR		0x04	/* Data Register */
#define RSPI_SPSCR		0x08	/* Sequence Control Register */
#define RSPI_SPSSR		0x09	/* Sequence Status Register */
#define RSPI_SPBR		0x0a	/* Bit Rate Register */
#define RSPI_SPDCR		0x0b	/* Data Control Register */
#define RSPI_SPCKD		0x0c	/* Clock Delay Register */
#define RSPI_SSLND		0x0d	/* Slave Select Negation Delay Register */
#define RSPI_SPND		0x0e	/* Next-Access Delay Register */
#define RSPI_SPCR2		0x0f	/* Control Register 2 */
#define RSPI_SPCMD0		0x10	/* Command Register 0 */
#define RSPI_SPCMD1		0x12	/* Command Register 1 */
#define RSPI_SPCMD2		0x14	/* Command Register 2 */
#define RSPI_SPCMD3		0x16	/* Command Register 3 */
#define RSPI_SPCMD4		0x18	/* Command Register 4 */
#define RSPI_SPCMD5		0x1a	/* Command Register 5 */
#define RSPI_SPCMD6		0x1c	/* Command Register 6 */
#define RSPI_SPCMD7		0x1e	/* Command Register 7 */
#define RSPI_SPBFCR		0x20	/* Buffer Control Register */
#define RSPI_SPBFDR		0x22	/* Buffer Data Count Setting Register */

/*qspi only */
#define QSPI_SPBFCR		0x18	/* Buffer Control Register */
#define QSPI_SPBDCR		0x1a	/* Buffer Data Count Register */
#define QSPI_SPBMUL0		0x1c	/* Transfer Data Length Multiplier Setting Register 0 */
#define QSPI_SPBMUL1		0x20	/* Transfer Data Length Multiplier Setting Register 1 */
#define QSPI_SPBMUL2		0x24	/* Transfer Data Length Multiplier Setting Register 2 */
#define QSPI_SPBMUL3		0x28	/* Transfer Data Length Multiplier Setting Register 3 */

/* SPCR - Control Register */
#define SPCR_SPRIE		0x80	/* Receive Interrupt Enable */
#define SPCR_SPE		0x40	/* Function Enable */
#define SPCR_SPTIE		0x20	/* Transmit Interrupt Enable */
#define SPCR_SPEIE		0x10	/* Error Interrupt Enable */
#define SPCR_MSTR		0x08	/* Master/Slave Mode Select */
#define SPCR_MODFEN		0x04	/* Mode Fault Error Detection Enable */
/* RSPI on SH only */
#define SPCR_TXMD		0x02	/* TX Only Mode (vs. Full Duplex) */
#define SPCR_SPMS		0x01	/* 3-wire Mode (vs. 4-wire) */
/* QSPI on R-Car M2 only */
#define SPCR_WSWAP		0x02	/* Word Swap of read-data for DMAC */
#define SPCR_BSWAP		0x01	/* Byte Swap of read-data for DMAC */

/* SSLP - Slave Select Polarity Register */
#define SSLP_SSL1P		0x02	/* SSL1 Signal Polarity Setting */
#define SSLP_SSL0P		0x01	/* SSL0 Signal Polarity Setting */

/* SPPCR - Pin Control Register */
#define SPPCR_MOIFE		0x20	/* MOSI Idle Value Fixing Enable */
#define SPPCR_MOIFV		0x10	/* MOSI Idle Fixed Value */
#define SPPCR_SPOM		0x04
#define SPPCR_SPLP2		0x02	/* Loopback Mode 2 (non-inverting) */
#define SPPCR_SPLP		0x01	/* Loopback Mode (inverting) */

#define SPPCR_IO3FV		0x04	/* Single-/Dual-SPI Mode IO3 Output Fixed Value */
#define SPPCR_IO2FV		0x04	/* Single-/Dual-SPI Mode IO2 Output Fixed Value */

/* SPSR - Status Register */
#define SPSR_SPRF		0x80	/* Receive Buffer Full Flag */
#define SPSR_TEND		0x40	/* Transmit End */
#define SPSR_SPTEF		0x20	/* Transmit Buffer Empty Flag */
#define SPSR_PERF		0x08	/* Parity Error Flag */
#define SPSR_MODF		0x04	/* Mode Fault Error Flag */
#define SPSR_IDLNF		0x02	/* RSPI Idle Flag */
#define SPSR_OVRF		0x01	/* Overrun Error Flag */

/* SPSCR - Sequence Control Register */
#define SPSCR_SPSLN_MASK	0x07	/* Sequence Length Specification */

/* SPSSR - Sequence Status Register */
#define SPSSR_SPECM_MASK	0x70	/* Command Error Mask */
#define SPSSR_SPCP_MASK		0x07	/* Command Pointer Mask */

/* SPDCR - Data Control Register */
#define SPDCR_TXDMY		0x80	/* Dummy Data Transmission Enable */
#define SPDCR_SPLW1		0x40	/* Access Width Specification (RZ) */
#define SPDCR_SPLW0		0x20	/* Access Width Specification (RZ) */
#define SPDCR_SPLLWORD		(SPDCR_SPLW1 | SPDCR_SPLW0)
#define SPDCR_SPLWORD		SPDCR_SPLW1
#define SPDCR_SPLBYTE		SPDCR_SPLW0
#define SPDCR_SPLW		0x20	/* Access Width Specification (SH) */
#define SPDCR_SPRDTD		0x10	/* Receive Transmit Data Select */
#define SPDCR_SLSEL1		0x08
#define SPDCR_SLSEL0		0x04
#define SPDCR_SLSEL_MASK	0x0c	/* SSL1 Output Select */
#define SPDCR_SPFC1		0x02
#define SPDCR_SPFC0		0x01
#define SPDCR_SPFC_MASK		0x03	/* Frame Count Setting (1-4) */

/* SPCKD - Clock Delay Register */
#define SPCKD_SCKDL_MASK	0x07	/* Clock Delay Setting (1-8) */

/* SSLND - Slave Select Negation Delay Register */
#define SSLND_SLNDL_MASK	0x07	/* SSL Negation Delay Setting (1-8) */

/* SPND - Next-Access Delay Register */
#define SPND_SPNDL_MASK		0x07	/* Next-Access Delay Setting (1-8) */

/* SPCR2 - Control Register 2 */
#define SPCR2_PTE		0x08	/* Parity Self-Test Enable */
#define SPCR2_SPIE		0x04	/* Idle Interrupt Enable */
#define SPCR2_SPOE		0x02	/* Odd Parity Enable (vs. Even) */
#define SPCR2_SPPE		0x01	/* Parity Enable */

/* SPCMDn - Command Registers */
#define SPCMD_SCKDEN		0x8000	/* Clock Delay Setting Enable */
#define SPCMD_SLNDEN		0x4000	/* SSL Negation Delay Setting Enable */
#define SPCMD_SPNDEN		0x2000	/* Next-Access Delay Enable */
#define SPCMD_LSBF		0x1000	/* LSB First */
#define SPCMD_SPB_MASK		0x0f00	/* Data Length Setting */
#define SPCMD_SPB_8_TO_16(bit)	(((bit - 1) << 8) & SPCMD_SPB_MASK)
#define SPCMD_SPB_8BIT		0x0000	/* qspi only */
#define SPCMD_SPB_16BIT		0x0100
#define SPCMD_SPB_20BIT		0x0000
#define SPCMD_SPB_24BIT		0x0100
#define SPCMD_SPB_32BIT		0x0200
#define SPCMD_SSLKP		0x0080	/* SSL Signal Level Keeping */
#define SPCMD_SPIMOD_MASK	0x0060	/* SPI Operating Mode (QSPI only) */
#define SPCMD_SPIMOD1		0x0040
#define SPCMD_SPIMOD0		0x0020
#define SPCMD_SPIMOD_SINGLE	0
#define SPCMD_SPIMOD_DUAL	SPCMD_SPIMOD0
#define SPCMD_SPIMOD_QUAD	SPCMD_SPIMOD1
#define SPCMD_SPRW		0x0010	/* SPI Read/Write Access (Dual/Quad) */
#define SPCMD_SSLA_MASK		0x0030	/* SSL Assert Signal Setting (RSPI) */
#define SPCMD_BRDV_MASK		0x000c	/* Bit Rate Division Setting */
#define SPCMD_CPOL		0x0002	/* Clock Polarity Setting */
#define SPCMD_CPHA		0x0001	/* Clock Phase Setting */

/* SPBFCR - Buffer Control Register */
#define SPBFCR_TXRST		0x80	/* Transmit Buffer Data Reset (qspi only) */
#define SPBFCR_RXRST		0x40	/* Receive Buffer Data Reset (qspi only) */
#define SPBFCR_TXTRG_MASK	0x30	/* Transmit Buffer Data Triggering Number */
#define SPBFCR_RXTRG_MASK	0x07	/* Receive Buffer Data Triggering Number */

#define DUMMY_DATA		0x00

struct rspi_data {
	void __iomem *addr;
	u32 max_speed_hz;
	struct spi_master *master;
	wait_queue_head_t wait;
	struct clk *clk;
	u8 spsr;
	u16 spcmd;
	const struct spi_ops *ops;

	/* for dmaengine */
	struct dma_chan *chan_tx;
	struct dma_chan *chan_rx;
	int irq;

	unsigned dma_width_16bit:1;
	unsigned dma_callbacked:1;
	unsigned byte_access:1;
};

static void rspi_write8(const struct rspi_data *rspi, u8 data, u16 offset)
{
	iowrite8(data, rspi->addr + offset);
}

static void rspi_write16(const struct rspi_data *rspi, u16 data, u16 offset)
{
	iowrite16(data, rspi->addr + offset);
}

static void rspi_write32(const struct rspi_data *rspi, u32 data, u16 offset)
{
	iowrite32(data, rspi->addr + offset);
}

static u8 rspi_read8(const struct rspi_data *rspi, u16 offset)
{
	return ioread8(rspi->addr + offset);
}

static u16 rspi_read16(const struct rspi_data *rspi, u16 offset)
{
	return ioread16(rspi->addr + offset);
}

static void rspi_write_data(const struct rspi_data *rspi, u16 data)
{
	if (rspi->byte_access)
		rspi_write8(rspi, data, RSPI_SPDR);
	else /* 16 bit */
		rspi_write16(rspi, data, RSPI_SPDR);
}

static u16 rspi_read_data(const struct rspi_data *rspi)
{
	if (rspi->byte_access)
		return rspi_read8(rspi, RSPI_SPDR);
	else /* 16 bit */
		return rspi_read16(rspi, RSPI_SPDR);
}

/* optional functions */
struct spi_ops {
	int (*set_config_register)(struct rspi_data *rspi, int access_size);
	int (*send_pio)(struct rspi_data *rspi, struct spi_transfer *t);
	int (*receive_pio)(struct rspi_data *rspi, struct spi_transfer *t);
};

/*
 * functions for RSPI
 */
static int rspi_set_config_register(struct rspi_data *rspi, int access_size)
{
	int spbr;

	/* Sets output mode(CMOS) and MOSI signal(from previous transfer) */
	rspi_write8(rspi, 0x00, RSPI_SPPCR);

	/* Sets transfer bit rate */
	spbr = clk_get_rate(rspi->clk) / (2 * rspi->max_speed_hz) - 1;
	rspi_write8(rspi, clamp(spbr, 0, 255), RSPI_SPBR);

	/* Disable dummy transmission, set 16-bit word access, 1 frame */
	rspi_write8(rspi, 0, RSPI_SPDCR);
	rspi->byte_access = 0;

	/* Sets RSPCK, SSL, next-access delay value */
	rspi_write8(rspi, 0x00, RSPI_SPCKD);
	rspi_write8(rspi, 0x00, RSPI_SSLND);
	rspi_write8(rspi, 0x00, RSPI_SPND);

	/* Sets parity, interrupt mask */
	rspi_write8(rspi, 0x00, RSPI_SPCR2);

	/* Sets SPCMD */
	rspi_write16(rspi, SPCMD_SPB_8_TO_16(access_size) | rspi->spcmd,
		     RSPI_SPCMD0);

	/* Sets RSPI mode */
	rspi_write8(rspi, SPCR_MSTR, RSPI_SPCR);

	return 0;
}

/*
 * functions for QSPI
 */
static int qspi_set_config_register(struct rspi_data *rspi, int access_size)
{
	u16 spcmd;
	int spbr;

	/* Sets output mode(CMOS) and MOSI signal(from previous transfer) */
	rspi_write8(rspi, 0x00, RSPI_SPPCR);

	/* Sets transfer bit rate */
	spbr = clk_get_rate(rspi->clk) / (2 * rspi->max_speed_hz);
	rspi_write8(rspi, clamp(spbr, 0, 255), RSPI_SPBR);

	/* Disable dummy transmission, set byte access */
	rspi_write8(rspi, 0, RSPI_SPDCR);
	rspi->byte_access = 1;

	/* Sets RSPCK, SSL, next-access delay value */
	rspi_write8(rspi, 0x00, RSPI_SPCKD);
	rspi_write8(rspi, 0x00, RSPI_SSLND);
	rspi_write8(rspi, 0x00, RSPI_SPND);

	/* Data Length Setting */
	if (access_size == 8)
		spcmd = SPCMD_SPB_8BIT;
	else if (access_size == 16)
		spcmd = SPCMD_SPB_16BIT;
	else
		spcmd = SPCMD_SPB_32BIT;

	spcmd |= SPCMD_SCKDEN | SPCMD_SLNDEN | rspi->spcmd | SPCMD_SPNDEN;

	/* Resets transfer data length */
	rspi_write32(rspi, 0, QSPI_SPBMUL0);

	/* Resets transmit and receive buffer */
	rspi_write8(rspi, SPBFCR_TXRST | SPBFCR_RXRST, QSPI_SPBFCR);
	/* Sets buffer to allow normal operation */
	rspi_write8(rspi, 0x00, QSPI_SPBFCR);

	/* Sets SPCMD */
	rspi_write16(rspi, spcmd, RSPI_SPCMD0);

	/* Enables SPI function in a master mode */
	rspi_write8(rspi, SPCR_SPE | SPCR_MSTR, RSPI_SPCR);

	return 0;
}

#define set_config_register(spi, n) spi->ops->set_config_register(spi, n)

static void rspi_enable_irq(const struct rspi_data *rspi, u8 enable)
{
	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) | enable, RSPI_SPCR);
}

static void rspi_disable_irq(const struct rspi_data *rspi, u8 disable)
{
	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) & ~disable, RSPI_SPCR);
}

static int rspi_wait_for_interrupt(struct rspi_data *rspi, u8 wait_mask,
				   u8 enable_bit)
{
	int ret;

	rspi->spsr = rspi_read8(rspi, RSPI_SPSR);
	rspi_enable_irq(rspi, enable_bit);
	ret = wait_event_timeout(rspi->wait, rspi->spsr & wait_mask, HZ);
	if (ret == 0 && !(rspi->spsr & wait_mask))
		return -ETIMEDOUT;

	return 0;
}

static int rspi_send_pio(struct rspi_data *rspi, struct spi_transfer *t)
{
	int remain = t->len;
	const u8 *data = t->tx_buf;
	while (remain > 0) {
		rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) | SPCR_TXMD,
			    RSPI_SPCR);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: tx empty timeout\n", __func__);
			return -ETIMEDOUT;
		}

		rspi_write_data(rspi, *data);
		data++;
		remain--;
	}

	/* Waiting for the last transmission */
	rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE);

	return 0;
}

static int qspi_send_pio(struct rspi_data *rspi, struct spi_transfer *t)
{
	int remain = t->len;
	const u8 *data = t->tx_buf;

	rspi_write8(rspi, SPBFCR_TXRST, QSPI_SPBFCR);
	rspi_write8(rspi, 0x00, QSPI_SPBFCR);

	while (remain > 0) {

		if (rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: tx empty timeout\n", __func__);
			return -ETIMEDOUT;
		}
		rspi_write_data(rspi, *data++);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPRF, SPCR_SPRIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: receive timeout\n", __func__);
			return -ETIMEDOUT;
		}
		rspi_read_data(rspi);

		remain--;
	}

	/* Waiting for the last transmission */
	rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE);

	return 0;
}

#define send_pio(spi, t) spi->ops->send_pio(spi, t)

static void rspi_dma_complete(void *arg)
{
	struct rspi_data *rspi = arg;

	rspi->dma_callbacked = 1;
	wake_up_interruptible(&rspi->wait);
}

static int rspi_dma_map_sg(struct scatterlist *sg, const void *buf,
			   unsigned len, struct dma_chan *chan,
			   enum dma_transfer_direction dir)
{
	sg_init_table(sg, 1);
	sg_set_buf(sg, buf, len);
	sg_dma_len(sg) = len;
	return dma_map_sg(chan->device->dev, sg, 1, dir);
}

static void rspi_dma_unmap_sg(struct scatterlist *sg, struct dma_chan *chan,
			      enum dma_transfer_direction dir)
{
	dma_unmap_sg(chan->device->dev, sg, 1, dir);
}

static void rspi_memory_to_8bit(void *buf, const void *data, unsigned len)
{
	u16 *dst = buf;
	const u8 *src = data;

	while (len) {
		*dst++ = (u16)(*src++);
		len--;
	}
}

static void rspi_memory_from_8bit(void *buf, const void *data, unsigned len)
{
	u8 *dst = buf;
	const u16 *src = data;

	while (len) {
		*dst++ = (u8)*src++;
		len--;
	}
}

static int rspi_send_dma(struct rspi_data *rspi, struct spi_transfer *t)
{
	struct scatterlist sg;
	const void *buf = NULL;
	struct dma_async_tx_descriptor *desc;
	unsigned len;
	int ret = 0;

	if (rspi->dma_width_16bit) {
		void *tmp;
		/*
		 * If DMAC bus width is 16-bit, the driver allocates a dummy
		 * buffer. And, the driver converts original data into the
		 * DMAC data as the following format:
		 *  original data: 1st byte, 2nd byte ...
		 *  DMAC data:     1st byte, dummy, 2nd byte, dummy ...
		 */
		len = t->len * 2;
		tmp = kmalloc(len, GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;
		rspi_memory_to_8bit(tmp, t->tx_buf, t->len);
		buf = tmp;
	} else {
		len = t->len;
		buf = t->tx_buf;
	}

	if (!rspi_dma_map_sg(&sg, buf, len, rspi->chan_tx, DMA_TO_DEVICE)) {
		ret = -EFAULT;
		goto end_nomap;
	}
	desc = dmaengine_prep_slave_sg(rspi->chan_tx, &sg, 1, DMA_TO_DEVICE,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -EIO;
		goto end;
	}

	/*
	 * DMAC needs SPTIE, but if SPTIE is set, this IRQ routine will be
	 * called. So, this driver disables the IRQ while DMA transfer.
	 */
	disable_irq(rspi->irq);

	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) | SPCR_TXMD, RSPI_SPCR);
	rspi_enable_irq(rspi, SPCR_SPTIE);
	rspi->dma_callbacked = 0;

	desc->callback = rspi_dma_complete;
	desc->callback_param = rspi;
	dmaengine_submit(desc);
	dma_async_issue_pending(rspi->chan_tx);

	ret = wait_event_interruptible_timeout(rspi->wait,
					       rspi->dma_callbacked, HZ);
	if (ret > 0 && rspi->dma_callbacked)
		ret = 0;
	else if (!ret)
		ret = -ETIMEDOUT;
	rspi_disable_irq(rspi, SPCR_SPTIE);

	enable_irq(rspi->irq);

end:
	rspi_dma_unmap_sg(&sg, rspi->chan_tx, DMA_TO_DEVICE);
end_nomap:
	if (rspi->dma_width_16bit)
		kfree(buf);

	return ret;
}

static void rspi_receive_init(const struct rspi_data *rspi)
{
	u8 spsr;

	spsr = rspi_read8(rspi, RSPI_SPSR);
	if (spsr & SPSR_SPRF)
		rspi_read_data(rspi);	/* dummy read */
	if (spsr & SPSR_OVRF)
		rspi_write8(rspi, rspi_read8(rspi, RSPI_SPSR) & ~SPSR_OVRF,
			    RSPI_SPSR);
}

static int rspi_receive_pio(struct rspi_data *rspi, struct spi_transfer *t)
{
	int remain = t->len;
	u8 *data;

	rspi_receive_init(rspi);

	data = t->rx_buf;
	while (remain > 0) {
		rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) & ~SPCR_TXMD,
			    RSPI_SPCR);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: tx empty timeout\n", __func__);
			return -ETIMEDOUT;
		}
		/* dummy write for generate clock */
		rspi_write_data(rspi, DUMMY_DATA);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPRF, SPCR_SPRIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: receive timeout\n", __func__);
			return -ETIMEDOUT;
		}
		*data = rspi_read_data(rspi);

		data++;
		remain--;
	}

	return 0;
}

static void qspi_receive_init(const struct rspi_data *rspi)
{
	u8 spsr;

	spsr = rspi_read8(rspi, RSPI_SPSR);
	if (spsr & SPSR_SPRF)
		rspi_read_data(rspi);   /* dummy read */
	rspi_write8(rspi, SPBFCR_TXRST | SPBFCR_RXRST, QSPI_SPBFCR);
	rspi_write8(rspi, 0x00, QSPI_SPBFCR);
}

static int qspi_receive_pio(struct rspi_data *rspi, struct spi_transfer *t)
{
	int remain = t->len;
	u8 *data;

	qspi_receive_init(rspi);

	data = t->rx_buf;
	while (remain > 0) {

		if (rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: tx empty timeout\n", __func__);
			return -ETIMEDOUT;
		}
		/* dummy write for generate clock */
		rspi_write_data(rspi, DUMMY_DATA);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPRF, SPCR_SPRIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: receive timeout\n", __func__);
			return -ETIMEDOUT;
		}
		*data++ = rspi_read_data(rspi);
		remain--;
	}

	return 0;
}

#define receive_pio(spi, t) spi->ops->receive_pio(spi, t)

static int rspi_receive_dma(struct rspi_data *rspi, struct spi_transfer *t)
{
	struct scatterlist sg, sg_dummy;
	void *dummy = NULL, *rx_buf = NULL;
	struct dma_async_tx_descriptor *desc, *desc_dummy;
	unsigned len;
	int ret = 0;

	if (rspi->dma_width_16bit) {
		/*
		 * If DMAC bus width is 16-bit, the driver allocates a dummy
		 * buffer. And, finally the driver converts the DMAC data into
		 * actual data as the following format:
		 *  DMAC data:   1st byte, dummy, 2nd byte, dummy ...
		 *  actual data: 1st byte, 2nd byte ...
		 */
		len = t->len * 2;
		rx_buf = kmalloc(len, GFP_KERNEL);
		if (!rx_buf)
			return -ENOMEM;
	 } else {
		len = t->len;
		rx_buf = t->rx_buf;
	}

	/* prepare dummy transfer to generate SPI clocks */
	dummy = kzalloc(len, GFP_KERNEL);
	if (!dummy) {
		ret = -ENOMEM;
		goto end_nomap;
	}
	if (!rspi_dma_map_sg(&sg_dummy, dummy, len, rspi->chan_tx,
			     DMA_TO_DEVICE)) {
		ret = -EFAULT;
		goto end_nomap;
	}
	desc_dummy = dmaengine_prep_slave_sg(rspi->chan_tx, &sg_dummy, 1,
			DMA_TO_DEVICE, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_dummy) {
		ret = -EIO;
		goto end_dummy_mapped;
	}

	/* prepare receive transfer */
	if (!rspi_dma_map_sg(&sg, rx_buf, len, rspi->chan_rx,
			     DMA_FROM_DEVICE)) {
		ret = -EFAULT;
		goto end_dummy_mapped;

	}
	desc = dmaengine_prep_slave_sg(rspi->chan_rx, &sg, 1, DMA_FROM_DEVICE,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -EIO;
		goto end;
	}

	rspi_receive_init(rspi);

	/*
	 * DMAC needs SPTIE, but if SPTIE is set, this IRQ routine will be
	 * called. So, this driver disables the IRQ while DMA transfer.
	 */
	disable_irq(rspi->irq);

	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) & ~SPCR_TXMD, RSPI_SPCR);
	rspi_enable_irq(rspi, SPCR_SPTIE | SPCR_SPRIE);
	rspi->dma_callbacked = 0;

	desc->callback = rspi_dma_complete;
	desc->callback_param = rspi;
	dmaengine_submit(desc);
	dma_async_issue_pending(rspi->chan_rx);

	desc_dummy->callback = NULL;	/* No callback */
	dmaengine_submit(desc_dummy);
	dma_async_issue_pending(rspi->chan_tx);

	ret = wait_event_interruptible_timeout(rspi->wait,
					       rspi->dma_callbacked, HZ);
	if (ret > 0 && rspi->dma_callbacked)
		ret = 0;
	else if (!ret)
		ret = -ETIMEDOUT;
	rspi_disable_irq(rspi, SPCR_SPTIE | SPCR_SPRIE);

	enable_irq(rspi->irq);

end:
	rspi_dma_unmap_sg(&sg, rspi->chan_rx, DMA_FROM_DEVICE);
end_dummy_mapped:
	rspi_dma_unmap_sg(&sg_dummy, rspi->chan_tx, DMA_TO_DEVICE);
end_nomap:
	if (rspi->dma_width_16bit) {
		if (!ret)
			rspi_memory_from_8bit(t->rx_buf, rx_buf, t->len);
		kfree(rx_buf);
	}
	kfree(dummy);

	return ret;
}

static int rspi_is_dma(const struct rspi_data *rspi, struct spi_transfer *t)
{
	if (t->tx_buf && rspi->chan_tx)
		return 1;
	/* If the module receives data by DMAC, it also needs TX DMAC */
	if (t->rx_buf && rspi->chan_tx && rspi->chan_rx)
		return 1;

	return 0;
}

static int rspi_transfer_one(struct spi_master *master, struct spi_device *spi,
			     struct spi_transfer *xfer)
{
	struct rspi_data *rspi = spi_master_get_devdata(master);
	int ret = 0;

	if (xfer->tx_buf) {
		if (rspi_is_dma(rspi, xfer))
			ret = rspi_send_dma(rspi, xfer);
		else
			ret = send_pio(rspi, xfer);
		if (ret < 0)
			return ret;
	}
	if (xfer->rx_buf) {
		if (rspi_is_dma(rspi, xfer))
			ret = rspi_receive_dma(rspi, xfer);
		else
			ret = receive_pio(rspi, xfer);
	}
	return ret;
}

static int rspi_setup(struct spi_device *spi)
{
	struct rspi_data *rspi = spi_master_get_devdata(spi->master);

	rspi->max_speed_hz = spi->max_speed_hz;

	rspi->spcmd = SPCMD_SSLKP;
	if (spi->mode & SPI_CPOL)
		rspi->spcmd |= SPCMD_CPOL;
	if (spi->mode & SPI_CPHA)
		rspi->spcmd |= SPCMD_CPHA;

	set_config_register(rspi, 8);

	return 0;
}

static void rspi_cleanup(struct spi_device *spi)
{
}

static int rspi_prepare_message(struct spi_master *master,
				struct spi_message *message)
{
	struct rspi_data *rspi = spi_master_get_devdata(master);

	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) | SPCR_SPE, RSPI_SPCR);
	return 0;
}

static int rspi_unprepare_message(struct spi_master *master,
				  struct spi_message *message)
{
	struct rspi_data *rspi = spi_master_get_devdata(master);

	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) & ~SPCR_SPE, RSPI_SPCR);
	return 0;
}

static irqreturn_t rspi_irq(int irq, void *_sr)
{
	struct rspi_data *rspi = _sr;
	u8 spsr;
	irqreturn_t ret = IRQ_NONE;
	u8 disable_irq = 0;

	rspi->spsr = spsr = rspi_read8(rspi, RSPI_SPSR);
	if (spsr & SPSR_SPRF)
		disable_irq |= SPCR_SPRIE;
	if (spsr & SPSR_SPTEF)
		disable_irq |= SPCR_SPTIE;

	if (disable_irq) {
		ret = IRQ_HANDLED;
		rspi_disable_irq(rspi, disable_irq);
		wake_up(&rspi->wait);
	}

	return ret;
}

static int rspi_request_dma(struct rspi_data *rspi,
				      struct platform_device *pdev)
{
	const struct rspi_plat_data *rspi_pd = dev_get_platdata(&pdev->dev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dma_cap_mask_t mask;
	struct dma_slave_config cfg;
	int ret;

	if (!res || !rspi_pd)
		return 0;	/* The driver assumes no error. */

	rspi->dma_width_16bit = rspi_pd->dma_width_16bit;

	/* If the module receives data by DMAC, it also needs TX DMAC */
	if (rspi_pd->dma_rx_id && rspi_pd->dma_tx_id) {
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);
		rspi->chan_rx = dma_request_channel(mask, shdma_chan_filter,
						    (void *)rspi_pd->dma_rx_id);
		if (rspi->chan_rx) {
			cfg.slave_id = rspi_pd->dma_rx_id;
			cfg.direction = DMA_DEV_TO_MEM;
			cfg.dst_addr = 0;
			cfg.src_addr = res->start + RSPI_SPDR;
			ret = dmaengine_slave_config(rspi->chan_rx, &cfg);
			if (!ret)
				dev_info(&pdev->dev, "Use DMA when rx.\n");
			else
				return ret;
		}
	}
	if (rspi_pd->dma_tx_id) {
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);
		rspi->chan_tx = dma_request_channel(mask, shdma_chan_filter,
						    (void *)rspi_pd->dma_tx_id);
		if (rspi->chan_tx) {
			cfg.slave_id = rspi_pd->dma_tx_id;
			cfg.direction = DMA_MEM_TO_DEV;
			cfg.dst_addr = res->start + RSPI_SPDR;
			cfg.src_addr = 0;
			ret = dmaengine_slave_config(rspi->chan_tx, &cfg);
			if (!ret)
				dev_info(&pdev->dev, "Use DMA when tx\n");
			else
				return ret;
		}
	}

	return 0;
}

static void rspi_release_dma(struct rspi_data *rspi)
{
	if (rspi->chan_tx)
		dma_release_channel(rspi->chan_tx);
	if (rspi->chan_rx)
		dma_release_channel(rspi->chan_rx);
}

static int rspi_remove(struct platform_device *pdev)
{
	struct rspi_data *rspi = platform_get_drvdata(pdev);

	rspi_release_dma(rspi);
	clk_disable(rspi->clk);

	return 0;
}

static int rspi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct spi_master *master;
	struct rspi_data *rspi;
	int ret, irq;
	char clk_name[16];
	const struct rspi_plat_data *rspi_pd = dev_get_platdata(&pdev->dev);
	const struct spi_ops *ops;
	const struct platform_device_id *id_entry = pdev->id_entry;

	ops = (struct spi_ops *)id_entry->driver_data;
	/* ops parameter check */
	if (!ops->set_config_register) {
		dev_err(&pdev->dev, "there is no set_config_register\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq error\n");
		return -ENODEV;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct rspi_data));
	if (master == NULL) {
		dev_err(&pdev->dev, "spi_alloc_master error.\n");
		return -ENOMEM;
	}

	rspi = spi_master_get_devdata(master);
	platform_set_drvdata(pdev, rspi);
	rspi->ops = ops;
	rspi->master = master;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rspi->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rspi->addr)) {
		ret = PTR_ERR(rspi->addr);
		goto error1;
	}

	snprintf(clk_name, sizeof(clk_name), "%s%d", id_entry->name, pdev->id);
	rspi->clk = devm_clk_get(&pdev->dev, clk_name);
	if (IS_ERR(rspi->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(rspi->clk);
		goto error1;
	}
	clk_enable(rspi->clk);

	init_waitqueue_head(&rspi->wait);

	if (rspi_pd && rspi_pd->num_chipselect)
		master->num_chipselect = rspi_pd->num_chipselect;
	else
		master->num_chipselect = 2; /* default */

	master->bus_num = pdev->id;
	master->setup = rspi_setup;
	master->transfer_one = rspi_transfer_one;
	master->cleanup = rspi_cleanup;
	master->prepare_message = rspi_prepare_message;
	master->unprepare_message = rspi_unprepare_message;
	master->mode_bits = SPI_CPHA | SPI_CPOL;

	ret = devm_request_irq(&pdev->dev, irq, rspi_irq, 0,
			       dev_name(&pdev->dev), rspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq error\n");
		goto error2;
	}

	rspi->irq = irq;
	ret = rspi_request_dma(rspi, pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "rspi_request_dma failed.\n");
		goto error3;
	}

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_master error.\n");
		goto error3;
	}

	dev_info(&pdev->dev, "probed\n");

	return 0;

error3:
	rspi_release_dma(rspi);
error2:
	clk_disable(rspi->clk);
error1:
	spi_master_put(master);

	return ret;
}

static struct spi_ops rspi_ops = {
	.set_config_register =		rspi_set_config_register,
	.send_pio =			rspi_send_pio,
	.receive_pio =			rspi_receive_pio,
};

static struct spi_ops qspi_ops = {
	.set_config_register =		qspi_set_config_register,
	.send_pio =			qspi_send_pio,
	.receive_pio =			qspi_receive_pio,
};

static struct platform_device_id spi_driver_ids[] = {
	{ "rspi",	(kernel_ulong_t)&rspi_ops },
	{ "qspi",	(kernel_ulong_t)&qspi_ops },
	{},
};

MODULE_DEVICE_TABLE(platform, spi_driver_ids);

static struct platform_driver rspi_driver = {
	.probe =	rspi_probe,
	.remove =	rspi_remove,
	.id_table =	spi_driver_ids,
	.driver		= {
		.name = "renesas_spi",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(rspi_driver);

MODULE_DESCRIPTION("Renesas RSPI bus driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_ALIAS("platform:rspi");
