/*
 * Toshiba rbtx4927 specific setup
 *
 * Author: MontaVista Software, Inc.
 *	   source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 * Copyright (C) 1996, 97, 2001, 04  Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2002 MontaVista Software Inc.
 * Author: Michael Pruznick, michael_pruznick@mvista.com
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * Copyright (C) 2004 MontaVista Software Inc.
 * Author: Manish Lachwani, mlachwani@mvista.com
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/pci.h>
#include <asm/txx9/rbtx4927.h>
#include <asm/txx9/tx4938.h>	/* for TX4937 */

#ifdef CONFIG_PCI
static void __init tx4927_pci_setup(void)
{
	int extarb = !(__raw_readq(&tx4927_ccfgptr->ccfg) & TX4927_CCFG_PCIARB);
	struct pci_controller *c = &txx9_primary_pcic;

	register_pci_controller(c);

	if (__raw_readq(&tx4927_ccfgptr->ccfg) & TX4927_CCFG_PCI66)
		txx9_pci_option =
			(txx9_pci_option & ~TXX9_PCI_OPT_CLK_MASK) |
			TXX9_PCI_OPT_CLK_66; /* already configured */

	/* Reset PCI Bus */
	writeb(1, rbtx4927_pcireset_addr);
	/* Reset PCIC */
	txx9_set64(&tx4927_ccfgptr->clkctr, TX4927_CLKCTR_PCIRST);
	if ((txx9_pci_option & TXX9_PCI_OPT_CLK_MASK) ==
	    TXX9_PCI_OPT_CLK_66)
		tx4927_pciclk66_setup();
	mdelay(10);
	/* clear PCIC reset */
	txx9_clear64(&tx4927_ccfgptr->clkctr, TX4927_CLKCTR_PCIRST);
	writeb(0, rbtx4927_pcireset_addr);
	iob();

	tx4927_report_pciclk();
	tx4927_pcic_setup(tx4927_pcicptr, c, extarb);
	if ((txx9_pci_option & TXX9_PCI_OPT_CLK_MASK) ==
	    TXX9_PCI_OPT_CLK_AUTO &&
	    txx9_pci66_check(c, 0, 0)) {
		/* Reset PCI Bus */
		writeb(1, rbtx4927_pcireset_addr);
		/* Reset PCIC */
		txx9_set64(&tx4927_ccfgptr->clkctr, TX4927_CLKCTR_PCIRST);
		tx4927_pciclk66_setup();
		mdelay(10);
		/* clear PCIC reset */
		txx9_clear64(&tx4927_ccfgptr->clkctr, TX4927_CLKCTR_PCIRST);
		writeb(0, rbtx4927_pcireset_addr);
		iob();
		/* Reinitialize PCIC */
		tx4927_report_pciclk();
		tx4927_pcic_setup(tx4927_pcicptr, c, extarb);
	}
	tx4927_setup_pcierr_irq();
}

static void __init tx4937_pci_setup(void)
{
	int extarb = !(__raw_readq(&tx4938_ccfgptr->ccfg) & TX4938_CCFG_PCIARB);
	struct pci_controller *c = &txx9_primary_pcic;

	register_pci_controller(c);

	if (__raw_readq(&tx4938_ccfgptr->ccfg) & TX4938_CCFG_PCI66)
		txx9_pci_option =
			(txx9_pci_option & ~TXX9_PCI_OPT_CLK_MASK) |
			TXX9_PCI_OPT_CLK_66; /* already configured */

	/* Reset PCI Bus */
	writeb(1, rbtx4927_pcireset_addr);
	/* Reset PCIC */
	txx9_set64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
	if ((txx9_pci_option & TXX9_PCI_OPT_CLK_MASK) ==
	    TXX9_PCI_OPT_CLK_66)
		tx4938_pciclk66_setup();
	mdelay(10);
	/* clear PCIC reset */
	txx9_clear64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
	writeb(0, rbtx4927_pcireset_addr);
	iob();

	tx4938_report_pciclk();
	tx4927_pcic_setup(tx4938_pcicptr, c, extarb);
	if ((txx9_pci_option & TXX9_PCI_OPT_CLK_MASK) ==
	    TXX9_PCI_OPT_CLK_AUTO &&
	    txx9_pci66_check(c, 0, 0)) {
		/* Reset PCI Bus */
		writeb(1, rbtx4927_pcireset_addr);
		/* Reset PCIC */
		txx9_set64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
		tx4938_pciclk66_setup();
		mdelay(10);
		/* clear PCIC reset */
		txx9_clear64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIRST);
		writeb(0, rbtx4927_pcireset_addr);
		iob();
		/* Reinitialize PCIC */
		tx4938_report_pciclk();
		tx4927_pcic_setup(tx4938_pcicptr, c, extarb);
	}
	tx4938_setup_pcierr_irq();
}

static void __init rbtx4927_arch_init(void)
{
	tx4927_pci_setup();
}

static void __init rbtx4937_arch_init(void)
{
	tx4937_pci_setup();
}
#else
#define rbtx4927_arch_init NULL
#define rbtx4937_arch_init NULL
#endif /* CONFIG_PCI */

static void toshiba_rbtx4927_restart(char *command)
{
	/* enable the s/w reset register */
	writeb(1, rbtx4927_softresetlock_addr);

	/* wait for enable to be seen */
	while (!(readb(rbtx4927_softresetlock_addr) & 1))
		;

	/* do a s/w reset */
	writeb(1, rbtx4927_softreset_addr);

	/* fallback */
	(*_machine_halt)();
}

static void __init rbtx4927_clock_init(void);
static void __init rbtx4937_clock_init(void);

static void __init rbtx4927_mem_setup(void)
{
	if (TX4927_REV_PCODE() == 0x4927) {
		rbtx4927_clock_init();
		tx4927_setup();
	} else {
		rbtx4937_clock_init();
		tx4938_setup();
	}

	_machine_restart = toshiba_rbtx4927_restart;

#ifdef CONFIG_PCI
	txx9_alloc_pci_controller(&txx9_primary_pcic,
				  RBTX4927_PCIMEM, RBTX4927_PCIMEM_SIZE,
				  RBTX4927_PCIIO, RBTX4927_PCIIO_SIZE);
	txx9_board_pcibios_setup = tx4927_pcibios_setup;
#else
	set_io_port_base(KSEG1 + RBTX4927_ISA_IO_OFFSET);
#endif

	/* TX4927-SIO DTR on (PIO[15]) */
	gpio_request(15, "sio-dtr");
	gpio_direction_output(15, 1);

	tx4927_sio_init(0, 0);
}

static void __init rbtx4927_clock_init(void)
{
	/*
	 * ASSUMPTION: PCIDIVMODE is configured for PCI 33MHz or 66MHz.
	 *
	 * For TX4927:
	 * PCIDIVMODE[12:11]'s initial value is given by S9[4:3] (ON:0, OFF:1).
	 * CPU 166MHz: PCI 66MHz : PCIDIVMODE: 00 (1/2.5)
	 * CPU 200MHz: PCI 66MHz : PCIDIVMODE: 01 (1/3)
	 * CPU 166MHz: PCI 33MHz : PCIDIVMODE: 10 (1/5)
	 * CPU 200MHz: PCI 33MHz : PCIDIVMODE: 11 (1/6)
	 * i.e. S9[3]: ON (83MHz), OFF (100MHz)
	 */
	switch ((unsigned long)__raw_readq(&tx4927_ccfgptr->ccfg) &
		TX4927_CCFG_PCIDIVMODE_MASK) {
	case TX4927_CCFG_PCIDIVMODE_2_5:
	case TX4927_CCFG_PCIDIVMODE_5:
		txx9_cpu_clock = 166666666;	/* 166MHz */
		break;
	default:
		txx9_cpu_clock = 200000000;	/* 200MHz */
	}
}

static void __init rbtx4937_clock_init(void)
{
	/*
	 * ASSUMPTION: PCIDIVMODE is configured for PCI 33MHz or 66MHz.
	 *
	 * For TX4937:
	 * PCIDIVMODE[12:11]'s initial value is given by S1[5:4] (ON:0, OFF:1)
	 * PCIDIVMODE[10] is 0.
	 * CPU 266MHz: PCI 33MHz : PCIDIVMODE: 000 (1/8)
	 * CPU 266MHz: PCI 66MHz : PCIDIVMODE: 001 (1/4)
	 * CPU 300MHz: PCI 33MHz : PCIDIVMODE: 010 (1/9)
	 * CPU 300MHz: PCI 66MHz : PCIDIVMODE: 011 (1/4.5)
	 * CPU 333MHz: PCI 33MHz : PCIDIVMODE: 100 (1/10)
	 * CPU 333MHz: PCI 66MHz : PCIDIVMODE: 101 (1/5)
	 */
	switch ((unsigned long)__raw_readq(&tx4938_ccfgptr->ccfg) &
		TX4938_CCFG_PCIDIVMODE_MASK) {
	case TX4938_CCFG_PCIDIVMODE_8:
	case TX4938_CCFG_PCIDIVMODE_4:
		txx9_cpu_clock = 266666666;	/* 266MHz */
		break;
	case TX4938_CCFG_PCIDIVMODE_9:
	case TX4938_CCFG_PCIDIVMODE_4_5:
		txx9_cpu_clock = 300000000;	/* 300MHz */
		break;
	default:
		txx9_cpu_clock = 333333333;	/* 333MHz */
	}
}

static void __init rbtx4927_time_init(void)
{
	tx4927_time_init(0);
}

static void __init toshiba_rbtx4927_rtc_init(void)
{
	struct resource res = {
		.start	= RBTX4927_BRAMRTC_BASE - IO_BASE,
		.end	= RBTX4927_BRAMRTC_BASE - IO_BASE + 0x800 - 1,
		.flags	= IORESOURCE_MEM,
	};
	platform_device_register_simple("rtc-ds1742", -1, &res, 1);
}

static void __init rbtx4927_ne_init(void)
{
	struct resource res[] = {
		{
			.start	= RBTX4927_RTL_8019_BASE,
			.end	= RBTX4927_RTL_8019_BASE + 0x20 - 1,
			.flags	= IORESOURCE_IO,
		}, {
			.start	= RBTX4927_RTL_8019_IRQ,
			.flags	= IORESOURCE_IRQ,
		}
	};
	platform_device_register_simple("ne", -1, res, ARRAY_SIZE(res));
}

static void __init rbtx4927_mtd_init(void)
{
	int i;

	for (i = 0; i < 2; i++)
		tx4927_mtd_init(i);
}

static void __init rbtx4927_gpioled_init(void)
{
	static struct gpio_led leds[] = {
		{ .name = "gpioled:green:0", .gpio = 0, .active_low = 1, },
		{ .name = "gpioled:green:1", .gpio = 1, .active_low = 1, },
	};
	static struct gpio_led_platform_data pdata = {
		.num_leds = ARRAY_SIZE(leds),
		.leds = leds,
	};
	struct platform_device *pdev = platform_device_alloc("leds-gpio", 0);

	if (!pdev)
		return;
	pdev->dev.platform_data = &pdata;
	if (platform_device_add(pdev))
		platform_device_put(pdev);
}

static void __init rbtx4927_device_init(void)
{
	toshiba_rbtx4927_rtc_init();
	rbtx4927_ne_init();
	tx4927_wdt_init();
	rbtx4927_mtd_init();
	if (TX4927_REV_PCODE() == 0x4927) {
		tx4927_dmac_init(2);
		tx4927_aclc_init(0, 1);
	} else {
		tx4938_dmac_init(0, 2);
		tx4938_aclc_init();
	}
	platform_device_register_simple("txx9aclc-generic", -1, NULL, 0);
	txx9_iocled_init(RBTX4927_LED_ADDR - IO_BASE, -1, 3, 1, "green", NULL);
	rbtx4927_gpioled_init();
}

struct txx9_board_vec rbtx4927_vec __initdata = {
	.system = "Toshiba RBTX4927",
	.prom_init = rbtx4927_prom_init,
	.mem_setup = rbtx4927_mem_setup,
	.irq_setup = rbtx4927_irq_setup,
	.time_init = rbtx4927_time_init,
	.device_init = rbtx4927_device_init,
	.arch_init = rbtx4927_arch_init,
#ifdef CONFIG_PCI
	.pci_map_irq = rbtx4927_pci_map_irq,
#endif
};
struct txx9_board_vec rbtx4937_vec __initdata = {
	.system = "Toshiba RBTX4937",
	.prom_init = rbtx4927_prom_init,
	.mem_setup = rbtx4927_mem_setup,
	.irq_setup = rbtx4927_irq_setup,
	.time_init = rbtx4927_time_init,
	.device_init = rbtx4927_device_init,
	.arch_init = rbtx4937_arch_init,
#ifdef CONFIG_PCI
	.pci_map_irq = rbtx4927_pci_map_irq,
#endif
};
