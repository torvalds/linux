// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009,2010       One Laptop per Child
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <asm/olpc.h>

/* TODO: this eventually belongs in linux/vx855.h */
#define NR_VX855_GPI    14
#define NR_VX855_GPO    13
#define NR_VX855_GPIO   15

#define VX855_GPI(n)    (n)
#define VX855_GPO(n)    (NR_VX855_GPI + (n))
#define VX855_GPIO(n)   (NR_VX855_GPI + NR_VX855_GPO + (n))

#include "olpc_dcon.h"

/* Hardware setup on the XO 1.5:
 *	DCONLOAD connects to VX855_GPIO1 (not SMBCK2)
 *	DCONBLANK connects to VX855_GPIO8 (not SSPICLK)  unused in driver
 *	DCONSTAT0 connects to VX855_GPI10 (not SSPISDI)
 *	DCONSTAT1 connects to VX855_GPI11 (not nSSPISS)
 *	DCONIRQ connects to VX855_GPIO12
 *	DCONSMBDATA connects to VX855 graphics CRTSPD
 *	DCONSMBCLK connects to VX855 graphics CRTSPCLK
 */

#define VX855_GENL_PURPOSE_OUTPUT 0x44c /* PMIO_Rx4c-4f */
#define VX855_GPI_STATUS_CHG 0x450  /* PMIO_Rx50 */
#define VX855_GPI_SCI_SMI 0x452  /* PMIO_Rx52 */
#define BIT_GPIO12 0x40

#define PREFIX "OLPC DCON:"

enum dcon_gpios {
	OLPC_DCON_STAT0,
	OLPC_DCON_STAT1,
	OLPC_DCON_LOAD,
};

struct gpiod_lookup_table gpios_table = {
	.dev_id = NULL,
	.table = {
		GPIO_LOOKUP("VX855 South Bridge", VX855_GPIO(1), "dcon_load",
			    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("VX855 South Bridge", VX855_GPI(10), "dcon_stat0",
			    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("VX855 South Bridge", VX855_GPI(11), "dcon_stat1",
			    GPIO_ACTIVE_LOW),
		{ },
	},
};

static const struct dcon_gpio gpios_asis[] = {
	[OLPC_DCON_STAT0] = { .name = "dcon_stat0", .flags = GPIOD_ASIS },
	[OLPC_DCON_STAT1] = { .name = "dcon_stat1", .flags = GPIOD_ASIS },
	[OLPC_DCON_LOAD] = { .name = "dcon_load", .flags = GPIOD_ASIS },
};

static struct gpio_desc *gpios[3];

static void dcon_clear_irq(void)
{
	/* irq status will appear in PMIO_Rx50[6] (RW1C) on gpio12 */
	outb(BIT_GPIO12, VX855_GPI_STATUS_CHG);
}

static int dcon_was_irq(void)
{
	u8 tmp;

	/* irq status will appear in PMIO_Rx50[6] on gpio12 */
	tmp = inb(VX855_GPI_STATUS_CHG);

	return !!(tmp & BIT_GPIO12);
}

static int dcon_init_xo_1_5(struct dcon_priv *dcon)
{
	unsigned int irq;
	const struct dcon_gpio *pin = &gpios_asis[0];
	int i;
	int ret;

	/* Add GPIO look up table */
	gpios_table.dev_id = dev_name(&dcon->client->dev);
	gpiod_add_lookup_table(&gpios_table);

	/* Get GPIO descriptor */
	for (i = 0; i < ARRAY_SIZE(gpios_asis); i++) {
		gpios[i] = devm_gpiod_get(&dcon->client->dev, pin[i].name,
					  pin[i].flags);
		if (IS_ERR(gpios[i])) {
			ret = PTR_ERR(gpios[i]);
			pr_err("failed to request %s GPIO: %d\n", pin[i].name,
			       ret);
			return ret;
		}
	}

	dcon_clear_irq();

	/* set   PMIO_Rx52[6] to enable SCI/SMI on gpio12 */
	outb(inb(VX855_GPI_SCI_SMI) | BIT_GPIO12, VX855_GPI_SCI_SMI);

	/* Determine the current state of DCONLOAD, likely set by firmware */
	/* GPIO1 */
	dcon->curr_src = (inl(VX855_GENL_PURPOSE_OUTPUT) & 0x1000) ?
			DCON_SOURCE_CPU : DCON_SOURCE_DCON;
	dcon->pending_src = dcon->curr_src;

	/* we're sharing the IRQ with ACPI */
	irq = acpi_gbl_FADT.sci_interrupt;
	if (request_irq(irq, &dcon_interrupt, IRQF_SHARED, "DCON", dcon)) {
		pr_err("DCON (IRQ%d) allocation failed\n", irq);
		return 1;
	}

	return 0;
}

static void set_i2c_line(int sda, int scl)
{
	unsigned char tmp;
	unsigned int port = 0x26;

	/* FIXME: This directly accesses the CRT GPIO controller !!! */
	outb(port, 0x3c4);
	tmp = inb(0x3c5);

	if (scl)
		tmp |= 0x20;
	else
		tmp &= ~0x20;

	if (sda)
		tmp |= 0x10;
	else
		tmp &= ~0x10;

	tmp |= 0x01;

	outb(port, 0x3c4);
	outb(tmp, 0x3c5);
}

static void dcon_wiggle_xo_1_5(void)
{
	int x;

	/*
	 * According to HiMax, when powering the DCON up we should hold
	 * SMB_DATA high for 8 SMB_CLK cycles.  This will force the DCON
	 * state machine to reset to a (sane) initial state.  Mitch Bradley
	 * did some testing and discovered that holding for 16 SMB_CLK cycles
	 * worked a lot more reliably, so that's what we do here.
	 */
	set_i2c_line(1, 1);

	for (x = 0; x < 16; x++) {
		udelay(5);
		set_i2c_line(1, 0);
		udelay(5);
		set_i2c_line(1, 1);
	}
	udelay(5);

	/* set   PMIO_Rx52[6] to enable SCI/SMI on gpio12 */
	outb(inb(VX855_GPI_SCI_SMI) | BIT_GPIO12, VX855_GPI_SCI_SMI);
}

static void dcon_set_dconload_xo_1_5(int val)
{
	gpiod_set_value(gpios[OLPC_DCON_LOAD], val);
}

static int dcon_read_status_xo_1_5(u8 *status)
{
	if (!dcon_was_irq())
		return -1;

	/* i believe this is the same as "inb(0x44b) & 3" */
	*status = gpiod_get_value(gpios[OLPC_DCON_STAT0]);
	*status |= gpiod_get_value(gpios[OLPC_DCON_STAT1]) << 1;

	dcon_clear_irq();

	return 0;
}

struct dcon_platform_data dcon_pdata_xo_1_5 = {
	.init = dcon_init_xo_1_5,
	.bus_stabilize_wiggle = dcon_wiggle_xo_1_5,
	.set_dconload = dcon_set_dconload_xo_1_5,
	.read_status = dcon_read_status_xo_1_5,
};
