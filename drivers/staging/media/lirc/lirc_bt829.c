/*
 * Remote control driver for the TV-card based on bt829
 *
 *  by Leonid Froenchenko <lfroen@galileo.co.il>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include <media/lirc_dev.h>

static int poll_main(void);
static int atir_init_start(void);

static void write_index(unsigned char index, unsigned int value);
static unsigned int read_index(unsigned char index);

static void do_i2c_start(void);
static void do_i2c_stop(void);

static void seems_wr_byte(unsigned char al);
static unsigned char seems_rd_byte(void);

static unsigned int read_index(unsigned char al);
static void write_index(unsigned char ah, unsigned int edx);

static void cycle_delay(int cycle);

static void do_set_bits(unsigned char bl);
static unsigned char do_get_bits(void);

#define DATA_PCI_OFF 0x7FFC00
#define WAIT_CYCLE   20

#define DRIVER_NAME "lirc_bt829"

static bool debug;
#define dprintk(fmt, args...)						 \
	do {								 \
		if (debug)						 \
			printk(KERN_DEBUG DRIVER_NAME ": "fmt, ## args); \
	} while (0)

static int atir_minor;
static phys_addr_t pci_addr_phys;
static unsigned char *pci_addr_lin;

static struct lirc_driver atir_driver;

static struct pci_dev *do_pci_probe(void)
{
	struct pci_dev *my_dev;
	my_dev = pci_get_device(PCI_VENDOR_ID_ATI,
				PCI_DEVICE_ID_ATI_264VT, NULL);
	if (my_dev) {
		pr_err("Using device: %s\n", pci_name(my_dev));
		pci_addr_phys = 0;
		if (my_dev->resource[0].flags & IORESOURCE_MEM) {
			pci_addr_phys = my_dev->resource[0].start;
			pr_info("memory at %pa\n", &pci_addr_phys);
		}
		if (pci_addr_phys == 0) {
			pr_err("no memory resource ?\n");
			pci_dev_put(my_dev);
			return NULL;
		}
	} else {
		pr_err("pci_probe failed\n");
		return NULL;
	}
	return my_dev;
}

static int atir_add_to_buf(void *data, struct lirc_buffer *buf)
{
	unsigned char key;
	int status;
	status = poll_main();
	key = (status >> 8) & 0xFF;
	if (status & 0xFF) {
		dprintk("reading key %02X\n", key);
		lirc_buffer_write(buf, &key);
		return 0;
	}
	return -ENODATA;
}

static int atir_set_use_inc(void *data)
{
	dprintk("driver is opened\n");
	return 0;
}

static void atir_set_use_dec(void *data)
{
	dprintk("driver is closed\n");
}

int init_module(void)
{
	struct pci_dev *pdev;
	int rc;

	pdev = do_pci_probe();
	if (pdev == NULL)
		return -ENODEV;

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_put_dev;

	if (!atir_init_start()) {
		rc = -ENODEV;
		goto err_disable;
	}

	strcpy(atir_driver.name, "ATIR");
	atir_driver.minor       = -1;
	atir_driver.code_length = 8;
	atir_driver.sample_rate = 10;
	atir_driver.data        = NULL;
	atir_driver.add_to_buf  = atir_add_to_buf;
	atir_driver.set_use_inc = atir_set_use_inc;
	atir_driver.set_use_dec = atir_set_use_dec;
	atir_driver.dev         = &pdev->dev;
	atir_driver.owner       = THIS_MODULE;

	atir_minor = lirc_register_driver(&atir_driver);
	if (atir_minor < 0) {
		pr_err("failed to register driver!\n");
		rc = atir_minor;
		goto err_unmap;
	}
	dprintk("driver is registered on minor %d\n", atir_minor);

	return 0;

err_unmap:
	iounmap(pci_addr_lin);
err_disable:
	pci_disable_device(pdev);
err_put_dev:
	pci_dev_put(pdev);
	return rc;
}


void cleanup_module(void)
{
	struct pci_dev *pdev = to_pci_dev(atir_driver.dev);

	lirc_unregister_driver(atir_minor);
	iounmap(pci_addr_lin);
	pci_disable_device(pdev);
	pci_dev_put(pdev);
}


static int atir_init_start(void)
{
	pci_addr_lin = ioremap(pci_addr_phys + DATA_PCI_OFF, 0x400);
	if (!pci_addr_lin) {
		pr_info("pci mem must be mapped\n");
		return 0;
	}
	return 1;
}

static void cycle_delay(int cycle)
{
	udelay(WAIT_CYCLE*cycle);
}


static int poll_main(void)
{
	unsigned char status_high, status_low;

	do_i2c_start();

	seems_wr_byte(0xAA);
	seems_wr_byte(0x01);

	do_i2c_start();

	seems_wr_byte(0xAB);

	status_low = seems_rd_byte();
	status_high = seems_rd_byte();

	do_i2c_stop();

	return (status_high << 8) | status_low;
}

static void do_i2c_start(void)
{
	do_set_bits(3);
	cycle_delay(4);

	do_set_bits(1);
	cycle_delay(7);

	do_set_bits(0);
	cycle_delay(2);
}

static void do_i2c_stop(void)
{
	unsigned char bits;
	bits =  do_get_bits() & 0xFD;
	do_set_bits(bits);
	cycle_delay(1);

	bits |= 1;
	do_set_bits(bits);
	cycle_delay(2);

	bits |= 2;
	do_set_bits(bits);
	bits = 3;
	do_set_bits(bits);
	cycle_delay(2);
}

static void seems_wr_byte(unsigned char value)
{
	int i;
	unsigned char reg;

	reg = do_get_bits();
	for (i = 0; i < 8; i++) {
		if (value & 0x80)
			reg |= 0x02;
		else
			reg &= 0xFD;

		do_set_bits(reg);
		cycle_delay(1);

		reg |= 1;
		do_set_bits(reg);
		cycle_delay(1);

		reg &= 0xFE;
		do_set_bits(reg);
		cycle_delay(1);
		value <<= 1;
	}
	cycle_delay(2);

	reg |= 2;
	do_set_bits(reg);

	reg |= 1;
	do_set_bits(reg);

	cycle_delay(1);
	do_get_bits();

	reg &= 0xFE;
	do_set_bits(reg);
	cycle_delay(3);
}

static unsigned char seems_rd_byte(void)
{
	int i;
	int rd_byte;
	unsigned char bits_2, bits_1;

	bits_1 = do_get_bits() | 2;
	do_set_bits(bits_1);

	rd_byte = 0;
	for (i = 0; i < 8; i++) {
		bits_1 &= 0xFE;
		do_set_bits(bits_1);
		cycle_delay(2);

		bits_1 |= 1;
		do_set_bits(bits_1);
		cycle_delay(1);

		bits_2 = do_get_bits();
		if (bits_2 & 2)
			rd_byte |= 1;

		rd_byte <<= 1;
	}

	bits_1 = 0;
	if (bits_2 == 0)
		bits_1 |= 2;

	do_set_bits(bits_1);
	cycle_delay(2);

	bits_1 |= 1;
	do_set_bits(bits_1);
	cycle_delay(3);

	bits_1 &= 0xFE;
	do_set_bits(bits_1);
	cycle_delay(2);

	rd_byte >>= 1;
	rd_byte &= 0xFF;
	return rd_byte;
}

static void do_set_bits(unsigned char new_bits)
{
	int reg_val;
	reg_val = read_index(0x34);
	if (new_bits & 2) {
		reg_val &= 0xFFFFFFDF;
		reg_val |= 1;
	} else {
		reg_val &= 0xFFFFFFFE;
		reg_val |= 0x20;
	}
	reg_val |= 0x10;
	write_index(0x34, reg_val);

	reg_val = read_index(0x31);
	if (new_bits & 1)
		reg_val |= 0x1000000;
	else
		reg_val &= 0xFEFFFFFF;

	reg_val |= 0x8000000;
	write_index(0x31, reg_val);
}

static unsigned char do_get_bits(void)
{
	unsigned char bits;
	int reg_val;

	reg_val = read_index(0x34);
	reg_val |= 0x10;
	reg_val &= 0xFFFFFFDF;
	write_index(0x34, reg_val);

	reg_val = read_index(0x34);
	bits = 0;
	if (reg_val & 8)
		bits |= 2;
	else
		bits &= 0xFD;

	reg_val = read_index(0x31);
	if (reg_val & 0x1000000)
		bits |= 1;
	else
		bits &= 0xFE;

	return bits;
}

static unsigned int read_index(unsigned char index)
{
	unsigned char *addr;
	unsigned int value;
	/*  addr = pci_addr_lin + DATA_PCI_OFF + ((index & 0xFF) << 2); */
	addr = pci_addr_lin + ((index & 0xFF) << 2);
	value = readl(addr);
	return value;
}

static void write_index(unsigned char index, unsigned int reg_val)
{
	unsigned char *addr;
	addr = pci_addr_lin + ((index & 0xFF) << 2);
	writel(reg_val, addr);
}

MODULE_AUTHOR("Froenchenko Leonid");
MODULE_DESCRIPTION("IR remote driver for bt829 based TV cards");
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
