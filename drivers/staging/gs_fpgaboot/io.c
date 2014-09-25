/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/firmware.h>
#include <linux/io.h>

#include "io.h"

#ifdef CONFIG_B4860G100
static struct gpiobus gbus;
#endif /* CONFIG_B4860G100 */

static inline void byte0_out(unsigned char data);
static inline void byte1_out(unsigned char data);
static inline void xl_cclk_b(int32_t i);


/* Assert and Deassert CCLK */
void xl_shift_cclk(int count)
{
	int i;

	for (i = 0; i < count; i++) {
		xl_cclk_b(1);
		xl_cclk_b(0);
	}
}

int xl_supported_prog_bus_width(enum wbus bus_bytes)
{
	switch (bus_bytes) {
	case bus_1byte:
		break;
	case bus_2byte:
		break;
	default:
		pr_err("unsupported program bus width %d\n",
				bus_bytes);
		return 0;
	}

	return 1;
}

/* Serialize byte and clock each bit on target's DIN and CCLK pins */
void xl_shift_bytes_out(enum wbus bus_byte, unsigned char *pdata)
{
	/*
	 * supports 1 and 2 bytes programming mode
	 */
	if (likely(bus_byte == bus_2byte))
		byte0_out(pdata[0]);

	byte1_out(pdata[1]);
	xl_shift_cclk(1);
}

/*
 * generic bit swap for xilinx SYSTEMMAP FPGA programming
 */
static inline unsigned char bitswap(unsigned char s)
{
	unsigned char d;

	d = (((s&0x80)>>7) | ((s&0x40)>>5) | ((s&0x20)>>3) | ((s&0x10)>>1) |
		((s&0x08)<<1) | ((s&0x04)<<3) | ((s&0x02)<<5) | ((s&0x01)<<7));
	return d;
}

#ifdef CONFIG_B4860G100
/*
 * ======================================================================
 * board specific configuration
 */

static inline void mpc85xx_gpio_set_dir(
			int32_t port,
			uint32_t mask,
			uint32_t dir)
{
	dir |= (in_be32(gbus.r[port]+GPDIR) & ~mask);
	out_be32(gbus.r[port]+GPDIR, dir);
}

static inline void mpc85xx_gpio_set(int32_t port, uint32_t mask, uint32_t val)
{
	/* First mask off the unwanted parts of "dir" and "val" */
	val &= mask;

	/* Now read in the values we're supposed to preserve */
	val |= (in_be32(gbus.r[port]+GPDAT) & ~mask);

	out_be32(gbus.r[port]+GPDAT, val);
}

static inline uint32_t mpc85xx_gpio_get(int32_t port, uint32_t mask)
{
	/* Read the requested values */
	return in_be32(gbus.r[port]+GPDAT) & mask;
}

static inline void mpc85xx_gpio_set_low(int32_t port, uint32_t gpios)
{
	mpc85xx_gpio_set(port, gpios, 0x00000000);
}

static inline void mpc85xx_gpio_set_high(int32_t port, uint32_t gpios)
{
	mpc85xx_gpio_set(port, gpios, 0xFFFFFFFF);
}

static inline void gpio_set_value(int32_t port, uint32_t gpio, uint32_t value)
{
	int32_t g;

	g = 31 - gpio;
	if (value)
		mpc85xx_gpio_set_high(port, 1U << g);
	else
		mpc85xx_gpio_set_low(port, 1U << g);
}

static inline int gpio_get_value(int32_t port, uint32_t gpio)
{
	int32_t g;

	g = 31 - gpio;
	return !!mpc85xx_gpio_get(port, 1U << g);
}

static inline void xl_cclk_b(int32_t i)
{
	gpio_set_value(XL_CCLK_PORT, XL_CCLK_PIN, i);
}

void xl_program_b(int32_t i)
{
	gpio_set_value(XL_PROGN_PORT, XL_PROGN_PIN, i);
}

void xl_rdwr_b(int32_t i)
{
	gpio_set_value(XL_RDWRN_PORT, XL_RDWRN_PIN, i);
}

void xl_csi_b(int32_t i)
{
	gpio_set_value(XL_CSIN_PORT, XL_CSIN_PIN, i);
}

int xl_get_init_b(void)
{
	return gpio_get_value(XL_INITN_PORT, XL_INITN_PIN);
}

int xl_get_done_b(void)
{
	return gpio_get_value(XL_DONE_PORT, XL_DONE_PIN);
}


/* G100 specific bit swap and remmap (to gpio pins) for byte 0 */
static inline uint32_t bit_remap_byte0(uint32_t s)
{
	uint32_t d;

	d = (((s&0x80)>>7) | ((s&0x40)>>5) | ((s&0x20)>>3) | ((s&0x10)>>1) |
		((s&0x08)<<1) | ((s&0x04)<<3) | ((s&0x02)<<6) | ((s&0x01)<<9));
	return d;
}

/*
 * G100 specific MSB, in this order [byte0 | byte1], out
 */
static inline void byte0_out(unsigned char data)
{
	uint32_t swap32;

	swap32 =  bit_remap_byte0((uint32_t) data) << 8;

	mpc85xx_gpio_set(0, 0x0002BF00, (uint32_t) swap32);
}

/*
 * G100 specific LSB, in this order [byte0 | byte1], out
 */
static inline void byte1_out(unsigned char data)
{
	mpc85xx_gpio_set(0, 0x000000FF, (uint32_t) bitswap(data));
}

/*
 * configurable per device type for different I/O config
 */
int xl_init_io(void)
{
	struct device_node *np;
	const u32 *p_reg;
	int reg, cnt;

	cnt = 0;
	memset(&gbus, 0, sizeof(struct gpiobus));
	for_each_compatible_node(np, NULL, "fsl,qoriq-gpio") {
		p_reg = of_get_property(np, "reg", NULL);
		if (p_reg == NULL)
			break;
		reg = (int) *p_reg;
		gbus.r[cnt] = of_iomap(np, 0);

		if (!gbus.r[cnt]) {
			pr_err("not findding gpio cell-index %d\n", cnt);
			return -ENODEV;
		}
		cnt++;
	}
	mpc85xx_gpio_set_dir(0, 0x0002BFFF, 0x0002BFFF);
	mpc85xx_gpio_set_dir(1, 0x00240060, 0x00240060);

	gbus.ngpio = cnt;

	return 0;
}


#else	/* placeholder for boards with different config */

void xl_program_b(int32_t i)
{
	return;
}

void xl_rdwr_b(int32_t i)
{
	return;
}

void xl_csi_b(int32_t i)
{
	return;
}

int xl_get_init_b(void)
{
	return -1;
}

int xl_get_done_b(void)
{
	return -1;
}

static inline void byte0_out(unsigned char data)
{
	return;
}

static inline void byte1_out(unsigned char data)
{
	return;
}

static inline void xl_cclk_b(int32_t i)
{
	return;
}

/*
 * configurable per device type for different I/O config
 */
int xl_init_io(void)
{
	return -1;
}

#endif /* CONFIG_B4860G100 */
