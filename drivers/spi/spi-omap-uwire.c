/*
 * MicroWire interface driver for OMAP
 *
 * Copyright 2003 MontaVista Software Inc. <source@mvista.com>
 *
 * Ported to 2.6 OMAP uwire interface.
 * Copyright (C) 2004 Texas Instruments.
 *
 * Generalization patches by Juha Yrjola <juha.yrjola@nokia.com>
 *
 * Copyright (C) 2005 David Brownell (ported to 2.6 SPI interface)
 * Copyright (C) 2006 Nokia
 *
 * Many updates by Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/module.h>
#include <linux/io.h>

#include <asm/mach-types.h>
#include <linux/soc/ti/omap1-io.h>
#include <linux/soc/ti/omap1-soc.h>
#include <linux/soc/ti/omap1-mux.h>

/* FIXME address is now a platform device resource,
 * and irqs should show there too...
 */
#define UWIRE_BASE_PHYS		0xFFFB3000

/* uWire Registers: */
#define UWIRE_IO_SIZE 0x20
#define UWIRE_TDR     0x00
#define UWIRE_RDR     0x00
#define UWIRE_CSR     0x01
#define UWIRE_SR1     0x02
#define UWIRE_SR2     0x03
#define UWIRE_SR3     0x04
#define UWIRE_SR4     0x05
#define UWIRE_SR5     0x06

/* CSR bits */
#define	RDRB	(1 << 15)
#define	CSRB	(1 << 14)
#define	START	(1 << 13)
#define	CS_CMD	(1 << 12)

/* SR1 or SR2 bits */
#define UWIRE_READ_FALLING_EDGE		0x0001
#define UWIRE_READ_RISING_EDGE		0x0000
#define UWIRE_WRITE_FALLING_EDGE	0x0000
#define UWIRE_WRITE_RISING_EDGE		0x0002
#define UWIRE_CS_ACTIVE_LOW		0x0000
#define UWIRE_CS_ACTIVE_HIGH		0x0004
#define UWIRE_FREQ_DIV_2		0x0000
#define UWIRE_FREQ_DIV_4		0x0008
#define UWIRE_FREQ_DIV_8		0x0010
#define UWIRE_CHK_READY			0x0020
#define UWIRE_CLK_INVERTED		0x0040


struct uwire_spi {
	struct spi_bitbang	bitbang;
	struct clk		*ck;
};

struct uwire_state {
	unsigned	div1_idx;
};

/* REVISIT compile time constant for idx_shift? */
/*
 * Or, put it in a structure which is used throughout the driver;
 * that avoids having to issue two loads for each bit of static data.
 */
static unsigned int uwire_idx_shift = 2;
static void __iomem *uwire_base;

static inline void uwire_write_reg(int idx, u16 val)
{
	__raw_writew(val, uwire_base + (idx << uwire_idx_shift));
}

static inline u16 uwire_read_reg(int idx)
{
	return __raw_readw(uwire_base + (idx << uwire_idx_shift));
}

static inline void omap_uwire_configure_mode(u8 cs, unsigned long flags)
{
	u16	w, val = 0;
	int	shift, reg;

	if (flags & UWIRE_CLK_INVERTED)
		val ^= 0x03;
	val = flags & 0x3f;
	if (cs & 1)
		shift = 6;
	else
		shift = 0;
	if (cs <= 1)
		reg = UWIRE_SR1;
	else
		reg = UWIRE_SR2;

	w = uwire_read_reg(reg);
	w &= ~(0x3f << shift);
	w |= val << shift;
	uwire_write_reg(reg, w);
}

static int wait_uwire_csr_flag(u16 mask, u16 val, int might_not_catch)
{
	u16 w;
	int c = 0;
	unsigned long max_jiffies = jiffies + HZ;

	for (;;) {
		w = uwire_read_reg(UWIRE_CSR);
		if ((w & mask) == val)
			break;
		if (time_after(jiffies, max_jiffies)) {
			printk(KERN_ERR "%s: timeout. reg=%#06x "
					"mask=%#06x val=%#06x\n",
			       __func__, w, mask, val);
			return -1;
		}
		c++;
		if (might_not_catch && c > 64)
			break;
	}
	return 0;
}

static void uwire_set_clk1_div(int div1_idx)
{
	u16 w;

	w = uwire_read_reg(UWIRE_SR3);
	w &= ~(0x03 << 1);
	w |= div1_idx << 1;
	uwire_write_reg(UWIRE_SR3, w);
}

static void uwire_chipselect(struct spi_device *spi, int value)
{
	struct	uwire_state *ust = spi->controller_state;
	u16	w;
	int	old_cs;


	BUG_ON(wait_uwire_csr_flag(CSRB, 0, 0));

	w = uwire_read_reg(UWIRE_CSR);
	old_cs = (w >> 10) & 0x03;
	if (value == BITBANG_CS_INACTIVE || old_cs != spi_get_chipselect(spi, 0)) {
		/* Deselect this CS, or the previous CS */
		w &= ~CS_CMD;
		uwire_write_reg(UWIRE_CSR, w);
	}
	/* activate specfied chipselect */
	if (value == BITBANG_CS_ACTIVE) {
		uwire_set_clk1_div(ust->div1_idx);
		/* invert clock? */
		if (spi->mode & SPI_CPOL)
			uwire_write_reg(UWIRE_SR4, 1);
		else
			uwire_write_reg(UWIRE_SR4, 0);

		w = spi_get_chipselect(spi, 0) << 10;
		w |= CS_CMD;
		uwire_write_reg(UWIRE_CSR, w);
	}
}

static int uwire_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	unsigned	len = t->len;
	unsigned	bits = t->bits_per_word;
	unsigned	bytes;
	u16		val, w;
	int		status = 0;

	if (!t->tx_buf && !t->rx_buf)
		return 0;

	w = spi_get_chipselect(spi, 0) << 10;
	w |= CS_CMD;

	if (t->tx_buf) {
		const u8	*buf = t->tx_buf;

		/* NOTE:  DMA could be used for TX transfers */

		/* write one or two bytes at a time */
		while (len >= 1) {
			/* tx bit 15 is first sent; we byteswap multibyte words
			 * (msb-first) on the way out from memory.
			 */
			val = *buf++;
			if (bits > 8) {
				bytes = 2;
				val |= *buf++ << 8;
			} else
				bytes = 1;
			val <<= 16 - bits;

#ifdef	VERBOSE
			pr_debug("%s: write-%d =%04x\n",
					dev_name(&spi->dev), bits, val);
#endif
			if (wait_uwire_csr_flag(CSRB, 0, 0))
				goto eio;

			uwire_write_reg(UWIRE_TDR, val);

			/* start write */
			val = START | w | (bits << 5);

			uwire_write_reg(UWIRE_CSR, val);
			len -= bytes;

			/* Wait till write actually starts.
			 * This is needed with MPU clock 60+ MHz.
			 * REVISIT: we may not have time to catch it...
			 */
			if (wait_uwire_csr_flag(CSRB, CSRB, 1))
				goto eio;

			status += bytes;
		}

		/* REVISIT:  save this for later to get more i/o overlap */
		if (wait_uwire_csr_flag(CSRB, 0, 0))
			goto eio;

	} else if (t->rx_buf) {
		u8		*buf = t->rx_buf;

		/* read one or two bytes at a time */
		while (len) {
			if (bits > 8) {
				bytes = 2;
			} else
				bytes = 1;

			/* start read */
			val = START | w | (bits << 0);
			uwire_write_reg(UWIRE_CSR, val);
			len -= bytes;

			/* Wait till read actually starts */
			(void) wait_uwire_csr_flag(CSRB, CSRB, 1);

			if (wait_uwire_csr_flag(RDRB | CSRB,
						RDRB, 0))
				goto eio;

			/* rx bit 0 is last received; multibyte words will
			 * be properly byteswapped on the way to memory.
			 */
			val = uwire_read_reg(UWIRE_RDR);
			val &= (1 << bits) - 1;
			*buf++ = (u8) val;
			if (bytes == 2)
				*buf++ = val >> 8;
			status += bytes;
#ifdef	VERBOSE
			pr_debug("%s: read-%d =%04x\n",
					dev_name(&spi->dev), bits, val);
#endif

		}
	}
	return status;
eio:
	return -EIO;
}

static int uwire_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct uwire_state	*ust = spi->controller_state;
	struct uwire_spi	*uwire;
	unsigned		flags = 0;
	unsigned		hz;
	unsigned long		rate;
	int			div1_idx;
	int			div1;
	int			div2;
	int			status;

	uwire = spi_controller_get_devdata(spi->controller);

	/* mode 0..3, clock inverted separately;
	 * standard nCS signaling;
	 * don't treat DI=high as "not ready"
	 */
	if (spi->mode & SPI_CS_HIGH)
		flags |= UWIRE_CS_ACTIVE_HIGH;

	if (spi->mode & SPI_CPOL)
		flags |= UWIRE_CLK_INVERTED;

	switch (spi->mode & SPI_MODE_X_MASK) {
	case SPI_MODE_0:
	case SPI_MODE_3:
		flags |= UWIRE_WRITE_FALLING_EDGE | UWIRE_READ_RISING_EDGE;
		break;
	case SPI_MODE_1:
	case SPI_MODE_2:
		flags |= UWIRE_WRITE_RISING_EDGE | UWIRE_READ_FALLING_EDGE;
		break;
	}

	/* assume it's already enabled */
	rate = clk_get_rate(uwire->ck);

	if (t != NULL)
		hz = t->speed_hz;
	else
		hz = spi->max_speed_hz;

	if (!hz) {
		pr_debug("%s: zero speed?\n", dev_name(&spi->dev));
		status = -EINVAL;
		goto done;
	}

	/* F_INT = mpu_xor_clk / DIV1 */
	for (div1_idx = 0; div1_idx < 4; div1_idx++) {
		switch (div1_idx) {
		case 0:
			div1 = 2;
			break;
		case 1:
			div1 = 4;
			break;
		case 2:
			div1 = 7;
			break;
		default:
		case 3:
			div1 = 10;
			break;
		}
		div2 = (rate / div1 + hz - 1) / hz;
		if (div2 <= 8)
			break;
	}
	if (div1_idx == 4) {
		pr_debug("%s: lowest clock %ld, need %d\n",
			dev_name(&spi->dev), rate / 10 / 8, hz);
		status = -EDOM;
		goto done;
	}

	/* we have to cache this and reset in uwire_chipselect as this is a
	 * global parameter and another uwire device can change it under
	 * us */
	ust->div1_idx = div1_idx;
	uwire_set_clk1_div(div1_idx);

	rate /= div1;

	switch (div2) {
	case 0:
	case 1:
	case 2:
		flags |= UWIRE_FREQ_DIV_2;
		rate /= 2;
		break;
	case 3:
	case 4:
		flags |= UWIRE_FREQ_DIV_4;
		rate /= 4;
		break;
	case 5:
	case 6:
	case 7:
	case 8:
		flags |= UWIRE_FREQ_DIV_8;
		rate /= 8;
		break;
	}
	omap_uwire_configure_mode(spi_get_chipselect(spi, 0), flags);
	pr_debug("%s: uwire flags %02x, armxor %lu KHz, SCK %lu KHz\n",
			__func__, flags,
			clk_get_rate(uwire->ck) / 1000,
			rate / 1000);
	status = 0;
done:
	return status;
}

static int uwire_setup(struct spi_device *spi)
{
	struct uwire_state *ust = spi->controller_state;
	bool initial_setup = false;
	int status;

	if (ust == NULL) {
		ust = kzalloc(sizeof(*ust), GFP_KERNEL);
		if (ust == NULL)
			return -ENOMEM;
		spi->controller_state = ust;
		initial_setup = true;
	}

	status = uwire_setup_transfer(spi, NULL);
	if (status && initial_setup)
		kfree(ust);

	return status;
}

static void uwire_cleanup(struct spi_device *spi)
{
	kfree(spi->controller_state);
}

static void uwire_off(struct uwire_spi *uwire)
{
	uwire_write_reg(UWIRE_SR3, 0);
	clk_disable_unprepare(uwire->ck);
	spi_controller_put(uwire->bitbang.master);
}

static int uwire_probe(struct platform_device *pdev)
{
	struct spi_controller	*host;
	struct uwire_spi	*uwire;
	int			status;

	host = spi_alloc_host(&pdev->dev, sizeof(*uwire));
	if (!host)
		return -ENODEV;

	uwire = spi_controller_get_devdata(host);

	uwire_base = devm_ioremap(&pdev->dev, UWIRE_BASE_PHYS, UWIRE_IO_SIZE);
	if (!uwire_base) {
		dev_dbg(&pdev->dev, "can't ioremap UWIRE\n");
		spi_controller_put(host);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, uwire);

	uwire->ck = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(uwire->ck)) {
		status = PTR_ERR(uwire->ck);
		dev_dbg(&pdev->dev, "no functional clock?\n");
		spi_controller_put(host);
		return status;
	}
	clk_prepare_enable(uwire->ck);

	uwire_write_reg(UWIRE_SR3, 1);

	/* the spi->mode bits understood by this driver: */
	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	host->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 16);
	host->flags = SPI_CONTROLLER_HALF_DUPLEX;

	host->bus_num = 2;	/* "official" */
	host->num_chipselect = 4;
	host->setup = uwire_setup;
	host->cleanup = uwire_cleanup;

	uwire->bitbang.master = host;
	uwire->bitbang.chipselect = uwire_chipselect;
	uwire->bitbang.setup_transfer = uwire_setup_transfer;
	uwire->bitbang.txrx_bufs = uwire_txrx;

	status = spi_bitbang_start(&uwire->bitbang);
	if (status < 0) {
		uwire_off(uwire);
	}
	return status;
}

static void uwire_remove(struct platform_device *pdev)
{
	struct uwire_spi	*uwire = platform_get_drvdata(pdev);

	// FIXME remove all child devices, somewhere ...

	spi_bitbang_stop(&uwire->bitbang);
	uwire_off(uwire);
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:omap_uwire");

static struct platform_driver uwire_driver = {
	.driver = {
		.name		= "omap_uwire",
	},
	.probe = uwire_probe,
	.remove_new = uwire_remove,
	// suspend ... unuse ck
	// resume ... use ck
};

static int __init omap_uwire_init(void)
{
	return platform_driver_register(&uwire_driver);
}

static void __exit omap_uwire_exit(void)
{
	platform_driver_unregister(&uwire_driver);
}

subsys_initcall(omap_uwire_init);
module_exit(omap_uwire_exit);

MODULE_LICENSE("GPL");

