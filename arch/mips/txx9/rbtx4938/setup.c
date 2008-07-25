/*
 * Setup pointers to hardware-dependent routines.
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/reboot.h>
#include <asm/io.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/pci.h>
#include <asm/txx9/rbtx4938.h>
#include <linux/spi/spi.h>
#include <asm/txx9/spi.h>
#include <asm/txx9pio.h>

static void rbtx4938_machine_halt(void)
{
        printk(KERN_NOTICE "System Halted\n");
	local_irq_disable();

	while (1)
		__asm__(".set\tmips3\n\t"
			"wait\n\t"
			".set\tmips0");
}

static void rbtx4938_machine_power_off(void)
{
        rbtx4938_machine_halt();
        /* no return */
}

static void rbtx4938_machine_restart(char *command)
{
	local_irq_disable();

	printk("Rebooting...");
	writeb(1, rbtx4938_softresetlock_addr);
	writeb(1, rbtx4938_sfvol_addr);
	writeb(1, rbtx4938_softreset_addr);
	while(1)
		;
}

static void __init rbtx4938_pci_setup(void)
{
#ifdef CONFIG_PCI
	int extarb = !(__raw_readq(&tx4938_ccfgptr->ccfg) & TX4938_CCFG_PCIARB);
	struct pci_controller *c = &txx9_primary_pcic;

	register_pci_controller(c);

	if (__raw_readq(&tx4938_ccfgptr->ccfg) & TX4938_CCFG_PCI66)
		txx9_pci_option =
			(txx9_pci_option & ~TXX9_PCI_OPT_CLK_MASK) |
			TXX9_PCI_OPT_CLK_66; /* already configured */

	/* Reset PCI Bus */
	writeb(0, rbtx4938_pcireset_addr);
	/* Reset PCIC */
	txx9_set64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
	if ((txx9_pci_option & TXX9_PCI_OPT_CLK_MASK) ==
	    TXX9_PCI_OPT_CLK_66)
		tx4938_pciclk66_setup();
	mdelay(10);
	/* clear PCIC reset */
	txx9_clear64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
	writeb(1, rbtx4938_pcireset_addr);
	iob();

	tx4938_report_pciclk();
	tx4927_pcic_setup(tx4938_pcicptr, c, extarb);
	if ((txx9_pci_option & TXX9_PCI_OPT_CLK_MASK) ==
	    TXX9_PCI_OPT_CLK_AUTO &&
	    txx9_pci66_check(c, 0, 0)) {
		/* Reset PCI Bus */
		writeb(0, rbtx4938_pcireset_addr);
		/* Reset PCIC */
		txx9_set64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
		tx4938_pciclk66_setup();
		mdelay(10);
		/* clear PCIC reset */
		txx9_clear64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
		writeb(1, rbtx4938_pcireset_addr);
		iob();
		/* Reinitialize PCIC */
		tx4938_report_pciclk();
		tx4927_pcic_setup(tx4938_pcicptr, c, extarb);
	}

	if (__raw_readq(&tx4938_ccfgptr->pcfg) &
	    (TX4938_PCFG_ETH0_SEL|TX4938_PCFG_ETH1_SEL)) {
		/* Reset PCIC1 */
		txx9_set64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIC1RST);
		/* PCI1DMD==0 => PCI1CLK==GBUSCLK/2 => PCI66 */
		if (!(__raw_readq(&tx4938_ccfgptr->ccfg)
		      & TX4938_CCFG_PCI1DMD))
			tx4938_ccfg_set(TX4938_CCFG_PCI1_66);
		mdelay(10);
		/* clear PCIC1 reset */
		txx9_clear64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIC1RST);
		tx4938_report_pci1clk();

		/* mem:64K(max), io:64K(max) (enough for ETH0,ETH1) */
		c = txx9_alloc_pci_controller(NULL, 0, 0x10000, 0, 0x10000);
		register_pci_controller(c);
		tx4927_pcic_setup(tx4938_pcic1ptr, c, 0);
	}
	tx4938_setup_pcierr_irq();
#endif /* CONFIG_PCI */
}

/* SPI support */

/* chip select for SPI devices */
#define	SEEPROM1_CS	7	/* PIO7 */
#define	SEEPROM2_CS	0	/* IOC */
#define	SEEPROM3_CS	1	/* IOC */
#define	SRTC_CS	2	/* IOC */

static int __init rbtx4938_ethaddr_init(void)
{
#ifdef CONFIG_PCI
	unsigned char dat[17];
	unsigned char sum;
	int i;

	/* 0-3: "MAC\0", 4-9:eth0, 10-15:eth1, 16:sum */
	if (spi_eeprom_read(SEEPROM1_CS, 0, dat, sizeof(dat))) {
		printk(KERN_ERR "seeprom: read error.\n");
		return -ENODEV;
	} else {
		if (strcmp(dat, "MAC") != 0)
			printk(KERN_WARNING "seeprom: bad signature.\n");
		for (i = 0, sum = 0; i < sizeof(dat); i++)
			sum += dat[i];
		if (sum)
			printk(KERN_WARNING "seeprom: bad checksum.\n");
	}
	for (i = 0; i < 2; i++) {
		unsigned int id =
			TXX9_IRQ_BASE + (i ? TX4938_IR_ETH1 : TX4938_IR_ETH0);
		struct platform_device *pdev;
		if (!(__raw_readq(&tx4938_ccfgptr->pcfg) &
		      (i ? TX4938_PCFG_ETH1_SEL : TX4938_PCFG_ETH0_SEL)))
			continue;
		pdev = platform_device_alloc("tc35815-mac", id);
		if (!pdev ||
		    platform_device_add_data(pdev, &dat[4 + 6 * i], 6) ||
		    platform_device_add(pdev))
			platform_device_put(pdev);
	}
#endif /* CONFIG_PCI */
	return 0;
}

static void __init rbtx4938_spi_setup(void)
{
	/* set SPI_SEL */
	txx9_set64(&tx4938_ccfgptr->pcfg, TX4938_PCFG_SPI_SEL);
}

static struct resource rbtx4938_fpga_resource;

static void __init rbtx4938_time_init(void)
{
	tx4938_time_init(0);
}

static void __init rbtx4938_mem_setup(void)
{
	unsigned long long pcfg;
	char *argptr;

	if (txx9_master_clock == 0)
		txx9_master_clock = 25000000; /* 25MHz */

	tx4938_setup();

#ifdef CONFIG_PCI
	txx9_alloc_pci_controller(&txx9_primary_pcic, 0, 0, 0, 0);
	txx9_board_pcibios_setup = tx4927_pcibios_setup;
#else
	set_io_port_base(RBTX4938_ETHER_BASE);
#endif

	tx4938_setup_serial();
#ifdef CONFIG_SERIAL_TXX9_CONSOLE
        argptr = prom_getcmdline();
        if (strstr(argptr, "console=") == NULL) {
                strcat(argptr, " console=ttyS0,38400");
        }
#endif

#ifdef CONFIG_TOSHIBA_RBTX4938_MPLEX_PIO58_61
	printk("PIOSEL: disabling both ata and nand selection\n");
	local_irq_disable();
	txx9_clear64(&tx4938_ccfgptr->pcfg,
		     TX4938_PCFG_NDF_SEL | TX4938_PCFG_ATA_SEL);
#endif

#ifdef CONFIG_TOSHIBA_RBTX4938_MPLEX_NAND
	printk("PIOSEL: enabling nand selection\n");
	txx9_set64(&tx4938_ccfgptr->pcfg, TX4938_PCFG_NDF_SEL);
	txx9_clear64(&tx4938_ccfgptr->pcfg, TX4938_PCFG_ATA_SEL);
#endif

#ifdef CONFIG_TOSHIBA_RBTX4938_MPLEX_ATA
	printk("PIOSEL: enabling ata selection\n");
	txx9_set64(&tx4938_ccfgptr->pcfg, TX4938_PCFG_ATA_SEL);
	txx9_clear64(&tx4938_ccfgptr->pcfg, TX4938_PCFG_NDF_SEL);
#endif

#ifdef CONFIG_IP_PNP
	argptr = prom_getcmdline();
	if (strstr(argptr, "ip=") == NULL) {
		strcat(argptr, " ip=any");
	}
#endif


#ifdef CONFIG_FB
	{
		conswitchp = &dummy_con;
	}
#endif

	rbtx4938_spi_setup();
	pcfg = ____raw_readq(&tx4938_ccfgptr->pcfg);	/* updated */
	/* fixup piosel */
	if ((pcfg & (TX4938_PCFG_ATA_SEL | TX4938_PCFG_NDF_SEL)) ==
	    TX4938_PCFG_ATA_SEL)
		writeb((readb(rbtx4938_piosel_addr) & 0x03) | 0x04,
		       rbtx4938_piosel_addr);
	else if ((pcfg & (TX4938_PCFG_ATA_SEL | TX4938_PCFG_NDF_SEL)) ==
		 TX4938_PCFG_NDF_SEL)
		writeb((readb(rbtx4938_piosel_addr) & 0x03) | 0x08,
		       rbtx4938_piosel_addr);
	else
		writeb(readb(rbtx4938_piosel_addr) & ~(0x08 | 0x04),
		       rbtx4938_piosel_addr);

	rbtx4938_fpga_resource.name = "FPGA Registers";
	rbtx4938_fpga_resource.start = CPHYSADDR(RBTX4938_FPGA_REG_ADDR);
	rbtx4938_fpga_resource.end = CPHYSADDR(RBTX4938_FPGA_REG_ADDR) + 0xffff;
	rbtx4938_fpga_resource.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&txx9_ce_res[2], &rbtx4938_fpga_resource))
		printk("request resource for fpga failed\n");

	_machine_restart = rbtx4938_machine_restart;
	_machine_halt = rbtx4938_machine_halt;
	pm_power_off = rbtx4938_machine_power_off;

	writeb(0xff, rbtx4938_led_addr);
	printk(KERN_INFO "RBTX4938 --- FPGA(Rev %02x) DIPSW:%02x,%02x\n",
	       readb(rbtx4938_fpga_rev_addr),
	       readb(rbtx4938_dipsw_addr), readb(rbtx4938_bdipsw_addr));
}

static int __init rbtx4938_ne_init(void)
{
	struct resource res[] = {
		{
			.start	= RBTX4938_RTL_8019_BASE,
			.end	= RBTX4938_RTL_8019_BASE + 0x20 - 1,
			.flags	= IORESOURCE_IO,
		}, {
			.start	= RBTX4938_RTL_8019_IRQ,
			.flags	= IORESOURCE_IRQ,
		}
	};
	struct platform_device *dev =
		platform_device_register_simple("ne", -1,
						res, ARRAY_SIZE(res));
	return IS_ERR(dev) ? PTR_ERR(dev) : 0;
}

static DEFINE_SPINLOCK(rbtx4938_spi_gpio_lock);

static void rbtx4938_spi_gpio_set(struct gpio_chip *chip, unsigned int offset,
				  int value)
{
	u8 val;
	unsigned long flags;
	spin_lock_irqsave(&rbtx4938_spi_gpio_lock, flags);
	val = readb(rbtx4938_spics_addr);
	if (value)
		val |= 1 << offset;
	else
		val &= ~(1 << offset);
	writeb(val, rbtx4938_spics_addr);
	mmiowb();
	spin_unlock_irqrestore(&rbtx4938_spi_gpio_lock, flags);
}

static int rbtx4938_spi_gpio_dir_out(struct gpio_chip *chip,
				     unsigned int offset, int value)
{
	rbtx4938_spi_gpio_set(chip, offset, value);
	return 0;
}

static struct gpio_chip rbtx4938_spi_gpio_chip = {
	.set = rbtx4938_spi_gpio_set,
	.direction_output = rbtx4938_spi_gpio_dir_out,
	.label = "RBTX4938-SPICS",
	.base = 16,
	.ngpio = 3,
};

/* SPI support */

static void __init txx9_spi_init(unsigned long base, int irq)
{
	struct resource res[] = {
		{
			.start	= base,
			.end	= base + 0x20 - 1,
			.flags	= IORESOURCE_MEM,
		}, {
			.start	= irq,
			.flags	= IORESOURCE_IRQ,
		},
	};
	platform_device_register_simple("spi_txx9", 0,
					res, ARRAY_SIZE(res));
}

static int __init rbtx4938_spi_init(void)
{
	struct spi_board_info srtc_info = {
		.modalias = "rtc-rs5c348",
		.max_speed_hz = 1000000, /* 1.0Mbps @ Vdd 2.0V */
		.bus_num = 0,
		.chip_select = 16 + SRTC_CS,
		/* Mode 1 (High-Active, Shift-Then-Sample), High Avtive CS  */
		.mode = SPI_MODE_1 | SPI_CS_HIGH,
	};
	spi_register_board_info(&srtc_info, 1);
	spi_eeprom_register(SEEPROM1_CS);
	spi_eeprom_register(16 + SEEPROM2_CS);
	spi_eeprom_register(16 + SEEPROM3_CS);
	gpio_request(16 + SRTC_CS, "rtc-rs5c348");
	gpio_direction_output(16 + SRTC_CS, 0);
	gpio_request(SEEPROM1_CS, "seeprom1");
	gpio_direction_output(SEEPROM1_CS, 1);
	gpio_request(16 + SEEPROM2_CS, "seeprom2");
	gpio_direction_output(16 + SEEPROM2_CS, 1);
	gpio_request(16 + SEEPROM3_CS, "seeprom3");
	gpio_direction_output(16 + SEEPROM3_CS, 1);
	txx9_spi_init(TX4938_SPI_REG & 0xfffffffffULL, RBTX4938_IRQ_IRC_SPI);
	return 0;
}

static void __init rbtx4938_arch_init(void)
{
	gpiochip_add(&rbtx4938_spi_gpio_chip);
	rbtx4938_pci_setup();
	rbtx4938_spi_init();
}

/* Watchdog support */

static int __init txx9_wdt_init(unsigned long base)
{
	struct resource res = {
		.start	= base,
		.end	= base + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	};
	struct platform_device *dev =
		platform_device_register_simple("txx9wdt", -1, &res, 1);
	return IS_ERR(dev) ? PTR_ERR(dev) : 0;
}

static int __init rbtx4938_wdt_init(void)
{
	return txx9_wdt_init(TX4938_TMR_REG(2) & 0xfffffffffULL);
}

static void __init rbtx4938_device_init(void)
{
	rbtx4938_ethaddr_init();
	rbtx4938_ne_init();
	rbtx4938_wdt_init();
}

struct txx9_board_vec rbtx4938_vec __initdata = {
	.system = "Toshiba RBTX4938",
	.prom_init = rbtx4938_prom_init,
	.mem_setup = rbtx4938_mem_setup,
	.irq_setup = rbtx4938_irq_setup,
	.time_init = rbtx4938_time_init,
	.device_init = rbtx4938_device_init,
	.arch_init = rbtx4938_arch_init,
#ifdef CONFIG_PCI
	.pci_map_irq = rbtx4938_pci_map_irq,
#endif
};
