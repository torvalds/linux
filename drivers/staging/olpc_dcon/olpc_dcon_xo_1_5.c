/*
 * Copyright (c) 2009,2010       One Laptop per Child
 *
 * This program is free software.  You can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <linux/acpi.h>

/* Hardware setup on the XO 1.5:
 * 	DCONLOAD connects to
 *		VX855_GPO12 (not nCR_PWOFF)   (rev A)
 *		VX855_GPIO1 (not SMBCK2)      (rev B)
 * 	DCONBLANK connects to VX855_GPIO8 (not SSPICLK)  unused in driver
 *	DCONSTAT0 connects to VX855_GPI10 (not SSPISDI)
 *	DCONSTAT1 connects to VX855_GPI11 (not nSSPISS)
 *	DCONIRQ connects to VX855_GPIO12 (on B3.  on B2, it goes to
 *		SMBALRT, which doesn't work.)
 *	DCONSMBDATA connects to VX855 graphics CRTSPD
 *	DCONSMBCLK connects to VX855 graphics CRTSPCLK
 */

#define TEST_B2 0   // define to test B3 paths on a modded B2 board

#define VX855_GENL_PURPOSE_OUTPUT 0x44c // PMIO_Rx4c-4f
#define VX855_GPI_STATUS_CHG 0x450  // PMIO_Rx50
#define VX855_GPI_SCI_SMI 0x452  // PMIO_Rx52
#define BIT_GPIO12 0x40

#define PREFIX "OLPC DCON:"

/*
  there is no support here for DCONIRQ on 1.5 boards earlier than
  B3.  the issue is that the DCONIRQ signal on earlier boards is
  routed to SMBALRT, which turns out to to be a level sensitive
  interrupt.  the DCONIRQ signal is far too short (11usec) to
  be detected reliably in that case.  including support for
  DCONIRQ functions no better than none at all.
*/

static struct dcon_platform_data dcon_pdata_xo_1_5;

static void dcon_clear_irq(void)
{
	if (TEST_B2 || olpc_board_at_least(olpc_board(BOARD_XO_1_5_B3))) {
		// irq status will appear in PMIO_Rx50[6] (RW1C) on gpio12
		outb(BIT_GPIO12, VX855_GPI_STATUS_CHG);
	}
}

static int dcon_was_irq(void)
{
	u_int8_t tmp;

	if (TEST_B2 || olpc_board_at_least(olpc_board(BOARD_XO_1_5_B3))) {
		// irq status will appear in PMIO_Rx50[6] on gpio12
		tmp = inb(VX855_GPI_STATUS_CHG);
		return !!(tmp & BIT_GPIO12);
	}

	return 0;
}

static int dcon_init_xo_1_5(void)
{
	unsigned int irq;
	u_int8_t tmp;
	struct pci_dev *pdev;
	
	
	pdev = pci_get_device(PCI_VENDOR_ID_VIA,
			      PCI_DEVICE_ID_VIA_VX855, NULL);
	if (!pdev) {
		printk(KERN_ERR "cannot find VX855 PCI ID\n");
		return 1;
	}

	if (olpc_board_at_least(olpc_board(BOARD_XO_1_5_B1))) {
		pci_read_config_byte(pdev, 0x95, &tmp);
		pci_write_config_byte(pdev, 0x95, tmp|0x0c);
	} else {
		/* Set GPO12 to GPO mode, not nCR_PWOFF */
		pci_read_config_byte(pdev, 0x9b, &tmp);
		pci_write_config_byte(pdev, 0x9b, tmp|0x01);
	}

	/* Set GPIO8 to GPIO mode, not SSPICLK */
	pci_read_config_byte(pdev, 0xe3, &tmp);
	pci_write_config_byte(pdev, 0xe3, tmp | 0x04);

	/* Set GPI10/GPI11 to GPI mode, not SSPISDI/SSPISS */
	pci_read_config_byte(pdev, 0xe4, &tmp);
	pci_write_config_byte(pdev, 0xe4, tmp|0x08);

	if (TEST_B2 || olpc_board_at_least(olpc_board(BOARD_XO_1_5_B3))) {
		// clear PMU_RxE1[6] to select SCI on GPIO12
		// clear PMU_RxE0[6] to choose falling edge
		pci_read_config_byte(pdev, 0xe1, &tmp);
		pci_write_config_byte(pdev, 0xe1, tmp & ~BIT_GPIO12);
		pci_read_config_byte(pdev, 0xe0, &tmp);
		pci_write_config_byte(pdev, 0xe0, tmp & ~BIT_GPIO12);

		dcon_clear_irq();

		// set   PMIO_Rx52[6] to enable SCI/SMI on gpio12
		outb(inb(VX855_GPI_SCI_SMI)|BIT_GPIO12, VX855_GPI_SCI_SMI);

	}

	/* Determine the current state of DCONLOAD, likely set by firmware */
	if (olpc_board_at_least(olpc_board(BOARD_XO_1_5_B1))) {
		// GPIO1
		dcon_source = (inl(VX855_GENL_PURPOSE_OUTPUT) & 0x1000) ?
			DCON_SOURCE_CPU : DCON_SOURCE_DCON;
	} else {
		// GPO12
		dcon_source = (inl(VX855_GENL_PURPOSE_OUTPUT) & 0x04000000) ?
			DCON_SOURCE_CPU : DCON_SOURCE_DCON;
	}
	dcon_pending = dcon_source;

	pci_dev_put(pdev);

	/* we're sharing the IRQ with ACPI */
	irq = acpi_gbl_FADT.sci_interrupt;
	if (request_irq(irq, &dcon_interrupt, IRQF_SHARED, "DCON", &dcon_driver)) {
		printk(KERN_ERR PREFIX "DCON (IRQ%d) allocation failed\n", irq);
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

	if (TEST_B2 || olpc_board_at_least(olpc_board(BOARD_XO_1_5_B3))) {
		// set   PMIO_Rx52[6] to enable SCI/SMI on gpio12
		outb(inb(VX855_GPI_SCI_SMI)|BIT_GPIO12, VX855_GPI_SCI_SMI);
	}
}

static void dcon_set_dconload_xo_1_5(int val)
{
	if (olpc_board_at_least(olpc_board(BOARD_XO_1_5_B1))) {
		gpio_set_value(VX855_GPIO(1), val);
	} else {
		gpio_set_value(VX855_GPO(12), val);
	}
}

static int dcon_read_status_xo_1_5(void) 
{
	int status;
	
	if (!dcon_was_irq())
		return -1;

	// i believe this is the same as "inb(0x44b) & 3"
	status = gpio_get_value(VX855_GPI(10));
	status |= gpio_get_value(VX855_GPI(11)) << 1; 

	dcon_clear_irq();

	return status;
}

static struct dcon_platform_data dcon_pdata_xo_1_5 = {
	.init = dcon_init_xo_1_5,
	.bus_stabilize_wiggle = dcon_wiggle_xo_1_5,
	.set_dconload = dcon_set_dconload_xo_1_5,
	.read_status = dcon_read_status_xo_1_5,
};
