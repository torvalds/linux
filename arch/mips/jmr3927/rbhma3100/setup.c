/***********************************************************************
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * Based on arch/mips/ddb5xxx/ddb5477/setup.c
 *
 *     Setup file for JMR3927.
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
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
 ***********************************************************************
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/param.h>	/* for HZ */
#include <linux/delay.h>
#include <linux/pm.h>
#ifdef CONFIG_SERIAL_TXX9
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#endif

#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/bcache.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/gdb-stub.h>
#include <asm/jmr3927/jmr3927.h>
#include <asm/mipsregs.h>
#include <asm/traps.h>

extern void puts(unsigned char *cp);

/* Tick Timer divider */
#define JMR3927_TIMER_CCD	0	/* 1/2 */
#define JMR3927_TIMER_CLK	(JMR3927_IMCLK / (2 << JMR3927_TIMER_CCD))

unsigned char led_state = 0xf;

struct {
    struct resource ram0;
    struct resource ram1;
    struct resource pcimem;
    struct resource iob;
    struct resource ioc;
    struct resource pciio;
    struct resource jmy1394;
    struct resource rom1;
    struct resource rom0;
    struct resource sio0;
    struct resource sio1;
} jmr3927_resources = {
	{
		.start	= 0,
		.end	= 0x01FFFFFF,
		.name	= "RAM0",
		.flags = IORESOURCE_MEM
	}, {
		.start	= 0x02000000,
		.end	= 0x03FFFFFF,
		.name	= "RAM1",
		.flags = IORESOURCE_MEM
	}, {
		.start	= 0x08000000,
		.end	= 0x07FFFFFF,
		.name	= "PCIMEM",
		.flags = IORESOURCE_MEM
	}, {
		.start	= 0x10000000,
		.end	= 0x13FFFFFF,
		.name	= "IOB"
	}, {
		.start	= 0x14000000,
		.end	= 0x14FFFFFF,
		.name	= "IOC"
	}, {
		.start	= 0x15000000,
		.end	= 0x15FFFFFF,
		.name	= "PCIIO"
	}, {
		.start	= 0x1D000000,
		.end	= 0x1D3FFFFF,
		.name	= "JMY1394"
	}, {
		.start	= 0x1E000000,
		.end	= 0x1E3FFFFF,
		.name	= "ROM1"
	}, {
		.start	= 0x1FC00000,
		.end	= 0x1FFFFFFF,
		.name	= "ROM0"
	}, {
		.start	= 0xFFFEF300,
		.end	= 0xFFFEF3FF,
		.name	= "SIO0"
	}, {
		.start	= 0xFFFEF400,
		.end	= 0xFFFEF4FF,
		.name	= "SIO1"
	},
};

/* don't enable - see errata */
int jmr3927_ccfg_toeon = 0;

static inline void do_reset(void)
{
#ifdef CONFIG_TC35815
	extern void tc35815_killall(void);
	tc35815_killall();
#endif
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

static cycle_t jmr3927_hpt_read(void)
{
	/* We assume this function is called xtime_lock held. */
	return jiffies * (JMR3927_TIMER_CLK / HZ) + jmr3927_tmrptr->trr;
}

#define USE_RTC_DS1742
#ifdef USE_RTC_DS1742
extern void rtc_ds1742_init(unsigned long base);
#endif
static void __init jmr3927_time_init(void)
{
	clocksource_mips.read = jmr3927_hpt_read;
	mips_hpt_frequency = JMR3927_TIMER_CLK;
#ifdef USE_RTC_DS1742
	if (jmr3927_have_nvram()) {
	        rtc_ds1742_init(JMR3927_IOC_NVRAMB_ADDR);
	}
#endif
}

void __init plat_timer_setup(struct irqaction *irq)
{
	jmr3927_tmrptr->cpra = JMR3927_TIMER_CLK / HZ;
	jmr3927_tmrptr->itmr = TXx927_TMTITMR_TIIE | TXx927_TMTITMR_TZCE;
	jmr3927_tmrptr->ccdr = JMR3927_TIMER_CCD;
	jmr3927_tmrptr->tcr =
		TXx927_TMTCR_TCE | TXx927_TMTCR_CCDE | TXx927_TMTCR_TMODE_ITVL;

	setup_irq(JMR3927_IRQ_TICK, irq);
}

#define USECS_PER_JIFFY (1000000/HZ)

//#undef DO_WRITE_THROUGH
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

	board_time_init = jmr3927_time_init;

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

	{
		unsigned int conf;
		conf = read_c0_conf();
	}

#if 1
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
#endif

	/* initialize board */
	jmr3927_board_init();

	argptr = prom_getcmdline();

	if ((argptr = strstr(argptr, "toeon")) != NULL) {
			jmr3927_ccfg_toeon = 1;
	}
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
			req.membase = (char *)TX3927_SIO_REG(i);
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

#ifdef CONFIG_PCI
unsigned long mips_pci_io_base;
unsigned long mips_pci_io_size;
unsigned long mips_pci_mem_base;
unsigned long mips_pci_mem_size;
/* for legacy I/O, PCI I/O PCI Bus address must be 0 */
unsigned long mips_pci_io_pciaddr = 0;
#endif

static void __init jmr3927_board_init(void)
{
	char *argptr;

#ifdef CONFIG_PCI
	mips_pci_io_base = JMR3927_PCIIO;
	mips_pci_io_size = JMR3927_PCIIO_SIZE;
	mips_pci_mem_base = JMR3927_PCIMEM;
	mips_pci_mem_size = JMR3927_PCIMEM_SIZE;
#endif

	tx3927_setup();

	if (jmr3927_have_isac()) {

#ifdef CONFIG_FB_E1355
		argptr = prom_getcmdline();
		if ((argptr = strstr(argptr, "video=")) == NULL) {
			argptr = prom_getcmdline();
			strcat(argptr, " video=e1355fb:crt16h");
		}
#endif

#ifdef CONFIG_BLK_DEV_IDE
		/* overrides PCI-IDE */
#endif
	}

	/* SIO0 DTR on */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_DTR_ADDR);

	jmr3927_led_set(0);


	if (jmr3927_have_isac())
		jmr3927_io_led_set(0);
	printk("JMR-TX3927 (Rev %d) --- IOC(Rev %d) DIPSW:%d,%d,%d,%d\n",
	       jmr3927_ioc_reg_in(JMR3927_IOC_BREV_ADDR) & JMR3927_REV_MASK,
	       jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR) & JMR3927_REV_MASK,
	       jmr3927_dipsw1(), jmr3927_dipsw2(),
	       jmr3927_dipsw3(), jmr3927_dipsw4());
	if (jmr3927_have_isac())
		printk("JMI-3927IO2 --- ISAC(Rev %d) DIPSW:%01x\n",
		       jmr3927_isac_reg_in(JMR3927_ISAC_REV_ADDR) & JMR3927_REV_MASK,
		       jmr3927_io_dipsw());
}

void __init tx3927_setup(void)
{
	int i;

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

	/* IRC */
	/* disable interrupt control */
	tx3927_ircptr->cer = 0;
	/* mask all IRC interrupts */
	tx3927_ircptr->imr = 0;
	for (i = 0; i < TX3927_NUM_IR / 2; i++) {
		tx3927_ircptr->ilr[i] = 0;
	}
	/* setup IRC interrupt mode (Low Active) */
	for (i = 0; i < TX3927_NUM_IR / 8; i++) {
		tx3927_ircptr->cr[i] = 0;
	}

	/* TMR */
	/* disable all timers */
	for (i = 0; i < TX3927_NR_TMR; i++) {
		tx3927_tmrptr(i)->tcr = TXx927_TMTCR_CRE;
		tx3927_tmrptr(i)->tisr = 0;
		tx3927_tmrptr(i)->cpra = 0xffffffff;
		tx3927_tmrptr(i)->itmr = 0;
		tx3927_tmrptr(i)->ccdr = 0;
		tx3927_tmrptr(i)->pgmr = 0;
	}

	/* DMA */
	tx3927_dmaptr->mcr = 0;
	for (i = 0; i < sizeof(tx3927_dmaptr->ch) / sizeof(tx3927_dmaptr->ch[0]); i++) {
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
#ifndef JMR3927_INIT_INDIRECT_PCI
		/* Enable Direct mapping Address Space Decoder */
		tx3927_pcicptr->lbc |= TX3927_PCIC_LBC_ILMDE | TX3927_PCIC_LBC_ILIDE;
#endif

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
#if 1
		/* Target Control (per errata) */
		tx3927_pcicptr->tc = TX3927_PCIC_TC_OF8E | TX3927_PCIC_TC_IF8E;
#endif

		/* Enable Bus Arbiter */
#if 0
		tx3927_pcicptr->req_trace = 0x73737373;
#endif
		tx3927_pcicptr->pbapmc = TX3927_PCIC_PBAPMC_PBAEN;

		tx3927_pcicptr->pcicmd = PCI_COMMAND_MASTER |
			PCI_COMMAND_MEMORY |
#if 1
			PCI_COMMAND_IO |
#endif
			PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	}
#endif /* CONFIG_PCI */

	/* PIO */
	/* PIO[15:12] connected to LEDs */
	tx3927_pioptr->dir = 0x0000f000;
	tx3927_pioptr->maskcpu = 0;
	tx3927_pioptr->maskext = 0;
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
