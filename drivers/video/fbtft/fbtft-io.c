#include <linux/export.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#ifdef CONFIG_ARCH_BCM2708
#include <mach/platform.h>
#endif
#include "fbtft.h"

int fbtft_write_spi(struct fbtft_par *par, void *buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf = buf,
		.len = len,
	};
	struct spi_message m;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	if (!par->spi) {
		dev_err(par->info->device,
			"%s: par->spi is unexpectedly NULL\n", __func__);
		return -1;
	}

	spi_message_init(&m);
	if (par->txbuf.dma && buf == par->txbuf.buf) {
		t.tx_dma = par->txbuf.dma;
		m.is_dma_mapped = 1;
	}
	spi_message_add_tail(&t, &m);
	return spi_sync(par->spi, &m);
}
EXPORT_SYMBOL(fbtft_write_spi);

/**
 * fbtft_write_spi_emulate_9() - write SPI emulating 9-bit
 * @par: Driver data
 * @buf: Buffer to write
 * @len: Length of buffer (must be divisible by 8)
 *
 * When 9-bit SPI is not available, this function can be used to emulate that.
 * par->extra must hold a transformation buffer used for transfer.
 */
int fbtft_write_spi_emulate_9(struct fbtft_par *par, void *buf, size_t len)
{
	u16 *src = buf;
	u8 *dst = par->extra;
	size_t size = len / 2;
	size_t added = 0;
	int bits, i, j;
	u64 val, dc, tmp;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	if (!par->extra) {
		dev_err(par->info->device, "%s: error: par->extra is NULL\n",
			__func__);
		return -EINVAL;
	}
	if ((len % 8) != 0) {
		dev_err(par->info->device,
			"%s: error: len=%d must be divisible by 8\n",
			__func__, len);
		return -EINVAL;
	}

	for (i = 0; i < size; i += 8) {
		tmp = 0;
		bits = 63;
		for (j = 0; j < 7; j++) {
			dc = (*src & 0x0100) ? 1 : 0;
			val = *src & 0x00FF;
			tmp |= dc << bits;
			bits -= 8;
			tmp |= val << bits--;
			src++;
		}
		tmp |= ((*src & 0x0100) ? 1 : 0);
		*(u64 *)dst = cpu_to_be64(tmp);
		dst += 8;
		*dst++ = (u8)(*src++ & 0x00FF);
		added++;
	}

	return spi_write(par->spi, par->extra, size + added);
}
EXPORT_SYMBOL(fbtft_write_spi_emulate_9);

int fbtft_read_spi(struct fbtft_par *par, void *buf, size_t len)
{
	int ret;
	u8 txbuf[32] = { 0, };
	struct spi_transfer	t = {
			.speed_hz = 2000000,
			.rx_buf		= buf,
			.len		= len,
		};
	struct spi_message	m;

	if (!par->spi) {
		dev_err(par->info->device,
			"%s: par->spi is unexpectedly NULL\n", __func__);
		return -ENODEV;
	}

	if (par->startbyte) {
		if (len > 32) {
			dev_err(par->info->device,
				"%s: len=%d can't be larger than 32 when using 'startbyte'\n",
				__func__, len);
			return -EINVAL;
		}
		txbuf[0] = par->startbyte | 0x3;
		t.tx_buf = txbuf;
		fbtft_par_dbg_hex(DEBUG_READ, par, par->info->device, u8,
			txbuf, len, "%s(len=%d) txbuf => ", __func__, len);
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(par->spi, &m);
	fbtft_par_dbg_hex(DEBUG_READ, par, par->info->device, u8, buf, len,
		"%s(len=%d) buf <= ", __func__, len);

	return ret;
}
EXPORT_SYMBOL(fbtft_read_spi);


#ifdef CONFIG_ARCH_BCM2708

/*
 *  Raspberry Pi
 *  -  writing directly to the registers is 40-50% faster than
 *     optimized use of gpiolib
 */

#define GPIOSET(no, ishigh)           \
do {                                  \
	if (ishigh)                   \
		set |= (1 << (no));   \
	else                          \
		reset |= (1 << (no)); \
} while (0)

int fbtft_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len)
{
	unsigned int set = 0;
	unsigned int reset = 0;
	u8 data;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	while (len--) {
		data = *(u8 *) buf;
		buf++;

		/* Set data */
		GPIOSET(par->gpio.db[0], (data&0x01));
		GPIOSET(par->gpio.db[1], (data&0x02));
		GPIOSET(par->gpio.db[2], (data&0x04));
		GPIOSET(par->gpio.db[3], (data&0x08));
		GPIOSET(par->gpio.db[4], (data&0x10));
		GPIOSET(par->gpio.db[5], (data&0x20));
		GPIOSET(par->gpio.db[6], (data&0x40));
		GPIOSET(par->gpio.db[7], (data&0x80));
		writel(set, __io_address(GPIO_BASE+0x1C));
		writel(reset, __io_address(GPIO_BASE+0x28));

		/* Pulse /WR low */
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x28));
		writel(0,  __io_address(GPIO_BASE+0x28)); /* used as a delay */
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x1C));

		set = 0;
		reset = 0;
	}

	return 0;
}
EXPORT_SYMBOL(fbtft_write_gpio8_wr);

int fbtft_write_gpio16_wr(struct fbtft_par *par, void *buf, size_t len)
{
	unsigned int set = 0;
	unsigned int reset = 0;
	u16 data;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	while (len) {
		len -= 2;
		data = *(u16 *) buf;
		buf += 2;

		/* Start writing by pulling down /WR */
		gpio_set_value(par->gpio.wr, 0);

		/* Set data */
		GPIOSET(par->gpio.db[0],  (data&0x0001));
		GPIOSET(par->gpio.db[1],  (data&0x0002));
		GPIOSET(par->gpio.db[2],  (data&0x0004));
		GPIOSET(par->gpio.db[3],  (data&0x0008));
		GPIOSET(par->gpio.db[4],  (data&0x0010));
		GPIOSET(par->gpio.db[5],  (data&0x0020));
		GPIOSET(par->gpio.db[6],  (data&0x0040));
		GPIOSET(par->gpio.db[7],  (data&0x0080));

		GPIOSET(par->gpio.db[8],  (data&0x0100));
		GPIOSET(par->gpio.db[9],  (data&0x0200));
		GPIOSET(par->gpio.db[10], (data&0x0400));
		GPIOSET(par->gpio.db[11], (data&0x0800));
		GPIOSET(par->gpio.db[12], (data&0x1000));
		GPIOSET(par->gpio.db[13], (data&0x2000));
		GPIOSET(par->gpio.db[14], (data&0x4000));
		GPIOSET(par->gpio.db[15], (data&0x8000));

		writel(set, __io_address(GPIO_BASE+0x1C));
		writel(reset, __io_address(GPIO_BASE+0x28));

		/* Pullup /WR */
		gpio_set_value(par->gpio.wr, 1);

		set = 0;
		reset = 0;
	}

	return 0;
}
EXPORT_SYMBOL(fbtft_write_gpio16_wr);

int fbtft_write_gpio16_wr_latched(struct fbtft_par *par, void *buf, size_t len)
{
	unsigned int set = 0;
	unsigned int reset = 0;
	u16 data;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	while (len) {
		len -= 2;
		data = *(u16 *) buf;
		buf += 2;

		/* Start writing by pulling down /WR */
		gpio_set_value(par->gpio.wr, 0);

		/* Low byte */
		GPIOSET(par->gpio.db[0],  (data&0x0001));
		GPIOSET(par->gpio.db[1],  (data&0x0002));
		GPIOSET(par->gpio.db[2],  (data&0x0004));
		GPIOSET(par->gpio.db[3],  (data&0x0008));
		GPIOSET(par->gpio.db[4],  (data&0x0010));
		GPIOSET(par->gpio.db[5],  (data&0x0020));
		GPIOSET(par->gpio.db[6],  (data&0x0040));
		GPIOSET(par->gpio.db[7],  (data&0x0080));
		writel(set, __io_address(GPIO_BASE+0x1C));
		writel(reset, __io_address(GPIO_BASE+0x28));

		/* Pulse 'latch' high */
		gpio_set_value(par->gpio.latch, 1);
		gpio_set_value(par->gpio.latch, 0);

		/* High byte */
		GPIOSET(par->gpio.db[0], (data&0x0100));
		GPIOSET(par->gpio.db[1], (data&0x0200));
		GPIOSET(par->gpio.db[2], (data&0x0400));
		GPIOSET(par->gpio.db[3], (data&0x0800));
		GPIOSET(par->gpio.db[4], (data&0x1000));
		GPIOSET(par->gpio.db[5], (data&0x2000));
		GPIOSET(par->gpio.db[6], (data&0x4000));
		GPIOSET(par->gpio.db[7], (data&0x8000));
		writel(set, __io_address(GPIO_BASE+0x1C));
		writel(reset, __io_address(GPIO_BASE+0x28));

		/* Pullup /WR */
		gpio_set_value(par->gpio.wr, 1);

		set = 0;
		reset = 0;
	}

	return 0;
}
EXPORT_SYMBOL(fbtft_write_gpio16_wr_latched);

#undef GPIOSET

#else

/*
 * Optimized use of gpiolib is twice as fast as no optimization
 * only one driver can use the optimized version at a time
 */

#if defined(CONFIG_MACH_MESON8B_ODROIDC)

union	regx_bitfield {
	unsigned int	wvalue;
	struct {
		unsigned int	unused0:5;
		unsigned int	bit2:1;	/* GPX.5 */
		unsigned int	bit4:1;	/* GPX.6 */
		unsigned int	bit1:1;	/* GPX.7 */
		unsigned int	bit6:1;	/* GPX.8 */
		unsigned int	bit5:1;	/* GPX.9 */
		unsigned int	bit3:1;	/* GPX.10 */
		unsigned int	unused2:7;
		unsigned int	bit0:1;	/* GPX.18 */
		unsigned int	unused3:1;
		unsigned int	bit7:1;	/* GPX.20 */
		unsigned int	unused4:11;
	} bits;
};

union	regy_bitfield {
	unsigned int	wvalue;
	struct {
		unsigned int	unused0:8;
		unsigned int	wr:1;	/* GPY.8 */
		unsigned int	unused1:23;
	} bits;
};

int fbtft_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len)
{
	u8 	data;
	union	regx_bitfield	dbusx;
	union	regy_bitfield	dbusy;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	dbusx.wvalue = ioread32(par->regrd_gpiox);
	dbusy.wvalue = ioread32(par->regrd_gpioy);

	while (len--) {
		data = *(u8 *) buf;

		/* Start writing by pulling down /WR */
		dbusy.bits.wr = 0;
		iowrite32(dbusy.wvalue, par->regwr_gpioy);

		dbusx.bits.bit0 = (data & 0x01) ? 1 : 0;
		dbusx.bits.bit1 = (data & 0x02) ? 1 : 0;
		dbusx.bits.bit2 = (data & 0x04) ? 1 : 0;
		dbusx.bits.bit3 = (data & 0x08) ? 1 : 0;
		dbusx.bits.bit4 = (data & 0x10) ? 1 : 0;
		dbusx.bits.bit5 = (data & 0x20) ? 1 : 0;
		dbusx.bits.bit6 = (data & 0x40) ? 1 : 0;
		dbusx.bits.bit7 = (data & 0x80) ? 1 : 0;
		iowrite32(dbusx.wvalue, par->regwr_gpiox);

		dbusy.bits.wr = 1;
		iowrite32(dbusy.wvalue, par->regwr_gpioy);
		buf++;
	}

	return 0;
}

#else

int fbtft_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len)
{
	u8 data;
	int i;
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
	static u8 prev_data;
#endif

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	while (len--) {
		data = *(u8 *) buf;

		/* Start writing by pulling down /WR */
		gpio_set_value(par->gpio.wr, 0);

		/* Set data */
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		if (data == prev_data) {
			gpio_set_value(par->gpio.wr, 0); /* used as delay */
		} else {
			for (i = 0; i < 8; i++) {
				if ((data & 1) != (prev_data & 1))
					gpio_set_value(par->gpio.db[i],
								(data & 1));
				data >>= 1;
				prev_data >>= 1;
			}
		}
#else
		for (i = 0; i < 8; i++) {
			gpio_set_value(par->gpio.db[i], (data & 1));
			data >>= 1;
		}
#endif

		/* Pullup /WR */
		gpio_set_value(par->gpio.wr, 1);

#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		prev_data = *(u8 *) buf;
#endif
		buf++;
	}

	return 0;
}

#endif	/* #if defined(CONFIG_MACH_MESON8B_ODROIDC) */

EXPORT_SYMBOL(fbtft_write_gpio8_wr);

int fbtft_write_gpio16_wr(struct fbtft_par *par, void *buf, size_t len)
{
	u16 data;
	int i;
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
	static u16 prev_data;
#endif

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	while (len) {
		data = *(u16 *) buf;

		/* Start writing by pulling down /WR */
		gpio_set_value(par->gpio.wr, 0);

		/* Set data */
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		if (data == prev_data) {
			gpio_set_value(par->gpio.wr, 0); /* used as delay */
		} else {
			for (i = 0; i < 16; i++) {
				if ((data & 1) != (prev_data & 1))
					gpio_set_value(par->gpio.db[i],
								(data & 1));
				data >>= 1;
				prev_data >>= 1;
			}
		}
#else
		for (i = 0; i < 16; i++) {
			gpio_set_value(par->gpio.db[i], (data & 1));
			data >>= 1;
		}
#endif

		/* Pullup /WR */
		gpio_set_value(par->gpio.wr, 1);

#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		prev_data = *(u16 *) buf;
#endif
		buf += 2;
		len -= 2;
	}

	return 0;
}
EXPORT_SYMBOL(fbtft_write_gpio16_wr);

int fbtft_write_gpio16_wr_latched(struct fbtft_par *par, void *buf, size_t len)
{
	dev_err(par->info->device, "%s: function not implemented\n", __func__);
	return -1;
}
EXPORT_SYMBOL(fbtft_write_gpio16_wr_latched);

#endif /* CONFIG_ARCH_BCM2708 */
