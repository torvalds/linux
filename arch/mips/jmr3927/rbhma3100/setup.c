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
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#ifdef CONFIG_SERIAL_TXX9
#include <linux/serial_core.h>
#endif

#include <asm/txx9tmr.h>
#include <asm/txx9pio.h>
#include <asm/reboot.h>
#include <asm/jmr3927/jmr3927.h>
#include <asm/mipsregs.h>

extern void puts(const char *cp);

/* don't enable - see errata */
static int jmr3927_ccfg_toeon;

static inline void do_reset(void)
{
#if 1	/* Resetting PCI bus */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);
	jmr3927_ioc_reg_out(JMR3927_IOC_RESET_PCI, JMR3927_IOC_RESET_ADDR);
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_RESET_ADDR);	/* flush WB */
	mdelay(1);
	jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);
#endif
	jmr3927_ioc_reg_out(JMR3927_IOC_RESET_CPU, JMR3927_IOC_RESET_ADDR);
}

static void jmr3927_machine_restart(char *command)
{
	local_irq_disable();
	puts("Rebooting...");
	do_reset();
}

static void jmr3927_machine_halt(void)
{
	puts("JMR-TX3927 halted.\n");
	while (1);
}

static void jmr3927_machine_power_off(void)
{
	puts("JMR-TX3927 halted. Please turn off the power.\n");
	while (1);
}

void __init plat_time_init(void)
{
	txx9_clockevent_init(TX3927_TMR_REG(0),
			     TXX9_IRQ_BASE + JMR3927_IRQ_IRC_TMR(0),
			     JMR3927_IMCLK);
	txx9_clocksource_init(TX3927_TMR_REG(1), JMR3927_IMCLK);
}

#define DO_WRITE_THROUGH
#define DO_ENABLE_CACHE

extern char * __init prom_getcmdline(void);
static void jmr3927_board_init(void);
extern struct resource pci_io_resource;
extern struct resource pci_mem_resource;

void __init plat_mem_setup(void)
{
	char *argptr;

	set_io_port_base(JMR3927_PORT_BASE + JMR3927_PCIIO);

	_machine_restart = jmr3927_machine_restart;
	_machine_halt = jmr3927_machine_halt;
	pm_power_off = jmr3927_machine_power_off;

	/*
	 * IO/MEM resources.
	 */
	ioport_resource.start = pci_io_resource.start;
	ioport_resource.end = pci_io_resource.end;
	iomem_resource.start = 0;
	iomem_resource.end = 0xffffffff;

	/* Reboot on panic */
	panic_timeout = 180;

	/* cache setup */
	{
		unsigned int conf;
#ifdef DO_ENABLE_CACHE
		int mips_ic_disable = 0, mips_dc_disable = 0;
#else
		int mips_ic_disable = 1, mips_dc_disable = 1;
#endif
#ifdef DO_WRITE_THROUGH
		int mips_config_cwfon = 0;
		int mips_config_wbon = 0;
#else
		int mips_config_cwfon = 1;
		int mips_config_wbon = 1;
#endif

		conf = read_c0_conf();
		conf &= ~(TX39_CONF_ICE | TX39_CONF_DCE | TX39_CONF_WBON | TX39_CONF_CWFON);
		conf |= mips_ic_disable ? 0 : TX39_CONF_ICE;
		conf |= mips_dc_disable ? 0 : TX39_CONF_DCE;
		conf |= mips_config_wbon ? TX39_CONF_WBON : 0;
		conf |= mips_config_cwfon ? TX39_CONF_CWFON : 0;

		write_c0_conf(conf);
		write_c0_cache(0);
	}

	/* initialize board */
	jmr3927_board_init();

	argptr = prom_getcmdline();

	if ((argptr = strstr(argptr, "toeon")) != NULL)
		jmr3927_ccfg_toeon = 1;
	argptr = prom_getcmdline();
	if ((argptr = strstr(argptr, "ip=")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " ip=bootp");
	}

#ifdef CONFIG_SERIAL_TXX9
	{
		extern int early_serial_txx9_setup(struct uart_port *port);
		int i;
		struct uart_port req;
		for(i = 0; i < 2; i++) {
			memset(&req, 0, sizeof(req));
			req.line = i;
			req.iotype = UPIO_MEM;
			req.membase = (unsigned char __iomem *)TX3927_SIO_REG(i);
			req.mapbase = TX3927_SIO_REG(i);
			req.irq = i == 0 ?
				JMR3927_IRQ_IRC_SIO0 : JMR3927_IRQ_IRC_SIO1;
			if (i == 0)
				req.flags |= UPF_BUGGY_UART /*HAVE_CTS_LINE*/;
			req.uartclk = JMR3927_IMCLK;
			early_serial_txx9_setup(&req);
		}
	}
#ifdef CONFIG_SERIAL_TXX9_CONSOLE
	argptr = prom_getcmdline();
	if ((argptr = strstr(argptr, "console=")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS1,115200");
	}
#endif
#endif
}

static void tx3927_setup(void);

static void __init jmr3927_board_init(void)
{
	tx3927_setup();

	/* SIO0 DTR on */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_DTR_ADDR);

	jmr3927_led_set(0);

	printk("JMR-TX3927 (Rev %d) --- IOC(Rev %d) DIPSW:%d,%d,%d,%d\n",
	       jmr3927_ioc_reg_in(JMR3927_IOC_BREV_ADDR) & JMR3927_REV_MASK,
	       jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR) & JMR3927_REV_MASK,
	       jmr3927_dipsw1(), jmr3927_dipsw2(),
	       jmr3927_dipsw3(), jmr3927_dipsw4());
}

static void __init tx3927_setup(void)
{
	int i;
#ifdef CONFIG_PCI
	unsigned long mips_pci_io_base = JMR3927_PCIIO;
	unsigned long mips_pci_io_size = JMR3927_PCIIO_SIZE;
	unsigned long mips_pci_mem_base = JMR3927_PCIMEM;
	unsigned long mips_pci_mem_size = JMR3927_PCIMEM_SIZE;
	/* for legacy I/O, PCI I/O PCI Bus address must be 0 */
	unsigned long mips_pci_io_pciaddr = 0;
#endif

	/* SDRAMC are configured by PROM */

	/* ROMC */
	tx3927_romcptr->cr[1] = JMR3927_ROMCE1 | 0x00030048;
	tx3927_romcptr->cr[2] = JMR3927_ROMCE2 | 0x000064c8;
	tx3927_romcptr->cr[3] = JMR3927_ROMCE3 | 0x0003f698;
	tx3927_romcptr->cr[5] = JMR3927_ROMCE5 | 0x0000f218;

	/* CCFG */
	/* enable Timeout BusError */
	if (jmr3927_ccfg_toeon)
		tx3927_ccfgptr->ccfg |= TX3927_CCFG_TOE;

	/* clear BusErrorOnWrite flag */
	tx3927_ccfgptr->ccfg &= ~TX3927_CCFG_BEOW;
	/* Disable PCI snoop */
	tx3927_ccfgptr->ccfg &= ~TX3927_CCFG_PSNP;
	/* do reset on watchdog */
	tx3927_ccfgptr->ccfg |= TX3927_CCFG_WR;

#ifdef DO_WRITE_THROUGH
	/* Enable PCI SNOOP - with write through only */
	tx3927_ccfgptr->ccfg |= TX3927_CCFG_PSNP;
#endif

	/* Pin selection */
	tx3927_ccfgptr->pcfg &= ~TX3927_PCFG_SELALL;
	tx3927_ccfgptr->pcfg |=
		TX3927_PCFG_SELSIOC(0) | TX3927_PCFG_SELSIO_ALL |
		(TX3927_PCFG_SELDMA_ALL & ~TX3927_PCFG_SELDMA(1));

	printk("TX3927 -- CRIR:%08lx CCFG:%08lx PCFG:%08lx\n",
	       tx3927_ccfgptr->crir,
	       tx3927_ccfgptr->ccfg, tx3927_ccfgptr->pcfg);

	/* TMR */
	for (i = 0; i < TX3927_NR_TMR; i++)
		txx9_tmr_init(TX3927_TMR_REG(i));

	/* DMA */
	tx3927_dmaptr->mcr = 0;
	for (i = 0; i < ARRAY_SIZE(tx3927_dmaptr->ch); i++) {
		/* reset channel */
		tx3927_dmaptr->ch[i].ccr = TX3927_DMA_CCR_CHRST;
		tx3927_dmaptr->ch[i].ccr = 0;
	}
	/* enable DMA */
#ifdef __BIG_ENDIAN
	tx3927_dmaptr->mcr = TX3927_DMA_MCR_MSTEN;
#else
	tx3927_dmaptr->mcr = TX3927_DMA_MCR_MSTEN | TX3927_DMA_MCR_LE;
#endif

#ifdef CONFIG_PCI
	/* PCIC */
	printk("TX3927 PCIC -- DID:%04x VID:%04x RID:%02x Arbiter:",
	       tx3927_pcicptr->did, tx3927_pcicptr->vid,
	       tx3927_pcicptr->rid);
	if (!(tx3927_ccfgptr->ccfg & TX3927_CCFG_PCIXARB)) {
		printk("External\n");
		/* XXX */
	} else {
		printk("Internal\n");

		/* Reset PCI Bus */
		jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);
		udelay(100);
		jmr3927_ioc_reg_out(JMR3927_IOC_RESET_PCI,
				    JMR3927_IOC_RESET_ADDR);
		udelay(100);
		jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);


		/* Disable External PCI Config. Access */
		tx3927_pcicptr->lbc = TX3927_PCIC_LBC_EPCAD;
#ifdef __BIG_ENDIAN
		tx3927_pcicptr->lbc |= TX3927_PCIC_LBC_IBSE |
			TX3927_PCIC_LBC_TIBSE |
			TX3927_PCIC_LBC_TMFBSE | TX3927_PCIC_LBC_MSDSE;
#endif
		/* LB->PCI mappings */
		tx3927_pcicptr->iomas = ~(mips_pci_io_size - 1);
		tx3927_pcicptr->ilbioma = mips_pci_io_base;
		tx3927_pcicptr->ipbioma = mips_pci_io_pciaddr;
		tx3927_pcicptr->mmas = ~(mips_pci_mem_size - 1);
		tx3927_pcicptr->ilbmma = mips_pci_mem_base;
		tx3927_pcicptr->ipbmma = mips_pci_mem_base;
		/* PCI->LB mappings */
		tx3927_pcicptr->iobas = 0xffffffff;
		tx3927_pcicptr->ioba = 0;
		tx3927_pcicptr->tlbioma = 0;
		tx3927_pcicptr->mbas = ~(mips_pci_mem_size - 1);
		tx3927_pcicptr->mba = 0;
		tx3927_pcicptr->tlbmma = 0;
		/* Enable Direct mapping Address Space Decoder */
		tx3927_pcicptr->lbc |= TX3927_PCIC_LBC_ILMDE | TX3927_PCIC_LBC_ILIDE;

		/* Clear All Local Bus Status */
		tx3927_pcicptr->lbstat = TX3927_PCIC_LBIM_ALL;
		/* Enable All Local Bus Interrupts */
		tx3927_pcicptr->lbim = TX3927_PCIC_LBIM_ALL;
		/* Clear All PCI Status Error */
		tx3927_pcicptr->pcistat = TX3927_PCIC_PCISTATIM_ALL;
		/* Enable All PCI Status Error Interrupts */
		tx3927_pcicptr->pcistatim = TX3927_PCIC_PCISTATIM_ALL;

		/* PCIC Int => IRC IRQ10 */
		tx3927_pcicptr->il = TX3927_IR_PCI;
		/* Target Control (per errata) */
		tx3927_pcicptr->tc = TX3927_PCIC_TC_OF8E | TX3927_PCIC_TC_IF8E;

		/* Enable Bus Arbiter */
		tx3927_pcicptr->pbapmc = TX3927_PCIC_PBAPMC_PBAEN;

		tx3927_pcicptr->pcicmd = PCI_COMMAND_MASTER |
			PCI_COMMAND_MEMORY |
			PCI_COMMAND_IO |
			PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	}
#endif /* CONFIG_PCI */

	/* PIO */
	/* PIO[15:12] connected to LEDs */
	__raw_writel(0x0000f000, &tx3927_pioptr->dir);
	__raw_writel(0, &tx3927_pioptr->maskcpu);
	__raw_writel(0, &tx3927_pioptr->maskext);
	txx9_gpio_init(TX3927_PIO_REG, 0, 16);
	gpio_request(11, "dipsw1");
	gpio_request(10, "dipsw2");
	{
		unsigned int conf;

	conf = read_c0_conf();
               if (!(conf & TX39_CONF_ICE))
                       printk("TX3927 I-Cache disabled.\n");
               if (!(conf & TX39_CONF_DCE))
                       printk("TX3927 D-Cache disabled.\n");
               else if (!(conf & TX39_CONF_WBON))
                       printk("TX3927 D-Cache WriteThrough.\n");
               else if (!(conf & TX39_CONF_CWFON))
                       printk("TX3927 D-Cache WriteBack.\n");
               else
                       printk("TX3927 D-Cache WriteBack (CWF) .\n");
	}
}

/* This trick makes rtc-ds1742 driver usable as is. */
unsigned long __swizzle_addr_b(unsigned long port)
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
EXPORT_SYMBOL(__swizzle_addr_b);

static int __init jmr3927_rtc_init(void)
{
	static struct resource __initdata res = {
		.start	= JMR3927_IOC_NVRAMB_ADDR - IO_BASE,
		.end	= JMR3927_IOC_NVRAMB_ADDR - IO_BASE + 0x800 - 1,
		.flags	= IORESOURCE_MEM,
	};
	struct platform_device *dev;
	dev = platform_device_register_simple("rtc-ds1742", -1, &res, 1);
	return IS_ERR(dev) ? PTR_ERR(dev) : 0;
}
device_initcall(jmr3927_rtc_init);

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

static int __init jmr3927_wdt_init(void)
{
	return txx9_wdt_init(TX3927_TMR_REG(2));
}
device_initcall(jmr3927_wdt_init);

/* Minimum CLK support */

struct clk *clk_get(struct device *dev, const char *id)
{
	if (!strcmp(id, "imbus_clk"))
		return (struct clk *)JMR3927_IMCLK;
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return (unsigned long)clk;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);
