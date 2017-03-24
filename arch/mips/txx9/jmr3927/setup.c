/*
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <asm/reboot.h>
#include <asm/txx9pio.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/pci.h>
#include <asm/txx9/jmr3927.h>
#include <asm/mipsregs.h>

static void jmr3927_machine_restart(char *command)
{
	local_irq_disable();
#if 1	/* Resetting PCI bus */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);
	jmr3927_ioc_reg_out(JMR3927_IOC_RESET_PCI, JMR3927_IOC_RESET_ADDR);
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_RESET_ADDR);	/* flush WB */
	mdelay(1);
	jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);
#endif
	jmr3927_ioc_reg_out(JMR3927_IOC_RESET_CPU, JMR3927_IOC_RESET_ADDR);
	/* fallback */
	(*_machine_halt)();
}

static void __init jmr3927_time_init(void)
{
	tx3927_time_init(0, 1);
}

#define DO_WRITE_THROUGH

static void jmr3927_board_init(void);

static void __init jmr3927_mem_setup(void)
{
	set_io_port_base(JMR3927_PORT_BASE + JMR3927_PCIIO);

	_machine_restart = jmr3927_machine_restart;

	/* cache setup */
	{
		unsigned int conf;
#ifdef DO_WRITE_THROUGH
		int mips_config_cwfon = 0;
		int mips_config_wbon = 0;
#else
		int mips_config_cwfon = 1;
		int mips_config_wbon = 1;
#endif

		conf = read_c0_conf();
		conf &= ~(TX39_CONF_WBON | TX39_CONF_CWFON);
		conf |= mips_config_wbon ? TX39_CONF_WBON : 0;
		conf |= mips_config_cwfon ? TX39_CONF_CWFON : 0;

		write_c0_conf(conf);
		write_c0_cache(0);
	}

	/* initialize board */
	jmr3927_board_init();

	tx3927_sio_init(0, 1 << 1); /* ch1: noCTS */
}

static void __init jmr3927_pci_setup(void)
{
#ifdef CONFIG_PCI
	int extarb = !(tx3927_ccfgptr->ccfg & TX3927_CCFG_PCIXARB);
	struct pci_controller *c;

	c = txx9_alloc_pci_controller(&txx9_primary_pcic,
				      JMR3927_PCIMEM, JMR3927_PCIMEM_SIZE,
				      JMR3927_PCIIO, JMR3927_PCIIO_SIZE);
	register_pci_controller(c);
	if (!extarb) {
		/* Reset PCI Bus */
		jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);
		udelay(100);
		jmr3927_ioc_reg_out(JMR3927_IOC_RESET_PCI,
				    JMR3927_IOC_RESET_ADDR);
		udelay(100);
		jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);
	}
	tx3927_pcic_setup(c, JMR3927_SDRAM_SIZE, extarb);
	tx3927_setup_pcierr_irq();
#endif /* CONFIG_PCI */
}

static void __init jmr3927_board_init(void)
{
	txx9_cpu_clock = JMR3927_CORECLK;
	/* SDRAMC are configured by PROM */

	/* ROMC */
	tx3927_romcptr->cr[1] = JMR3927_ROMCE1 | 0x00030048;
	tx3927_romcptr->cr[2] = JMR3927_ROMCE2 | 0x000064c8;
	tx3927_romcptr->cr[3] = JMR3927_ROMCE3 | 0x0003f698;
	tx3927_romcptr->cr[5] = JMR3927_ROMCE5 | 0x0000f218;

	/* Pin selection */
	tx3927_ccfgptr->pcfg &= ~TX3927_PCFG_SELALL;
	tx3927_ccfgptr->pcfg |=
		TX3927_PCFG_SELSIOC(0) | TX3927_PCFG_SELSIO_ALL |
		(TX3927_PCFG_SELDMA_ALL & ~TX3927_PCFG_SELDMA(1));

	tx3927_setup();

	/* PIO[15:12] connected to LEDs */
	__raw_writel(0x0000f000, &tx3927_pioptr->dir);

	jmr3927_pci_setup();

	/* SIO0 DTR on */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_DTR_ADDR);

	jmr3927_led_set(0);

	pr_info("JMR-TX3927 (Rev %d) --- IOC(Rev %d) DIPSW:%d,%d,%d,%d\n",
		jmr3927_ioc_reg_in(JMR3927_IOC_BREV_ADDR) & JMR3927_REV_MASK,
		jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR) & JMR3927_REV_MASK,
		jmr3927_dipsw1(), jmr3927_dipsw2(),
		jmr3927_dipsw3(), jmr3927_dipsw4());
}

/* This trick makes rtc-ds1742 driver usable as is. */
static unsigned long jmr3927_swizzle_addr_b(unsigned long port)
{
	if ((port & 0xffff0000) != JMR3927_IOC_NVRAMB_ADDR)
		return port;
	port = (port & 0xffff0000) | (port & 0x7fff << 1);
#ifdef __BIG_ENDIAN
	return port;
#else
	return port | 1;
#endif
}

static void __init jmr3927_rtc_init(void)
{
	static struct resource __initdata res = {
		.start	= JMR3927_IOC_NVRAMB_ADDR - IO_BASE,
		.end	= JMR3927_IOC_NVRAMB_ADDR - IO_BASE + 0x800 - 1,
		.flags	= IORESOURCE_MEM,
	};
	platform_device_register_simple("rtc-ds1742", -1, &res, 1);
}

static void __init jmr3927_mtd_init(void)
{
	int i;

	for (i = 0; i < 2; i++)
		tx3927_mtd_init(i);
}

static void __init jmr3927_device_init(void)
{
	unsigned long iocled_base = JMR3927_IOC_LED_ADDR - IO_BASE;
#ifdef __LITTLE_ENDIAN
	iocled_base |= 1;
#endif
	__swizzle_addr_b = jmr3927_swizzle_addr_b;
	jmr3927_rtc_init();
	tx3927_wdt_init();
	jmr3927_mtd_init();
	txx9_iocled_init(iocled_base, -1, 8, 1, "green", NULL);
}

static void __init jmr3927_arch_init(void)
{
	txx9_gpio_init(TX3927_PIO_REG, 0, 16);

	gpio_request(11, "dipsw1");
	gpio_request(10, "dipsw2");
}

struct txx9_board_vec jmr3927_vec __initdata = {
	.system = "Toshiba JMR_TX3927",
	.prom_init = jmr3927_prom_init,
	.mem_setup = jmr3927_mem_setup,
	.irq_setup = jmr3927_irq_setup,
	.time_init = jmr3927_time_init,
	.device_init = jmr3927_device_init,
	.arch_init = jmr3927_arch_init,
#ifdef CONFIG_PCI
	.pci_map_irq = jmr3927_pci_map_irq,
#endif
};
