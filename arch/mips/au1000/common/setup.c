/*
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * Updates to 2.6, Pete Popov, Embedded Alley Solutions, Inc.
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
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/time.h>

extern char * prom_getcmdline(void);
extern void __init board_setup(void);
extern void au1000_restart(char *);
extern void au1000_halt(void);
extern void au1000_power_off(void);
extern void au1x_time_init(void);
extern void au1x_timer_setup(struct irqaction *irq);
extern void au1xxx_time_init(void);
extern void set_cpuspec(void);

void __init plat_mem_setup(void)
{
	struct	cpu_spec *sp;
	char *argptr;
	unsigned long prid, cpupll, bclk = 1;

	set_cpuspec();
	sp = cur_cpu_spec[0];

	board_setup();  /* board specific setup */

	prid = read_c0_prid();
	cpupll = (au_readl(0xB1900060) & 0x3F) * 12;
	printk("(PRId %08lx) @ %ldMHZ\n", prid, cpupll);

	bclk = sp->cpu_bclk;
	if (bclk)
	{
		/* Enable BCLK switching */
		bclk = au_readl(0xB190003C);
		au_writel(bclk | 0x60, 0xB190003C);
		printk("BCLK switching enabled!\n");
	}

	if (sp->cpu_od) {
		/* Various early Au1000 Errata corrected by this */
		set_c0_config(1<<19); /* Set Config[OD] */
	}
	else {
		/* Clear to obtain best system bus performance */
		clear_c0_config(1<<19); /* Clear Config[OD] */
	}

	argptr = prom_getcmdline();

#ifdef CONFIG_SERIAL_8250_CONSOLE
	if ((argptr = strstr(argptr, "console=")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif

#ifdef CONFIG_FB_AU1100
    if ((argptr = strstr(argptr, "video=")) == NULL) {
        argptr = prom_getcmdline();
        /* default panel */
        /*strcat(argptr, " video=au1100fb:panel:Sharp_320x240_16");*/
    }
#endif


#if defined(CONFIG_SOUND_AU1X00) && !defined(CONFIG_SOC_AU1000)
	/* au1000 does not support vra, au1500 and au1100 do */
	strcat(argptr, " au1000_audio=vra");
	argptr = prom_getcmdline();
#endif
	_machine_restart = au1000_restart;
	_machine_halt = au1000_halt;
	pm_power_off = au1000_power_off;
	board_time_init = au1xxx_time_init;

	/* IO/MEM resources. */
	set_io_port_base(0);
	ioport_resource.start = IOPORT_RESOURCE_START;
	ioport_resource.end = IOPORT_RESOURCE_END;
	iomem_resource.start = IOMEM_RESOURCE_START;
	iomem_resource.end = IOMEM_RESOURCE_END;

	while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_E0S);
	au_writel(SYS_CNTRL_E0 | SYS_CNTRL_EN0, SYS_COUNTER_CNTRL);
	au_sync();
	while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_T0S);
	au_writel(0, SYS_TOYTRIM);
}

#if defined(CONFIG_64BIT_PHYS_ADDR)
/* This routine should be valid for all Au1x based boards */
phys_t __fixup_bigphys_addr(phys_t phys_addr, phys_t size)
{
	/* Don't fixup 36 bit addresses */
	if ((phys_addr >> 32) != 0)
		return phys_addr;

#ifdef CONFIG_PCI
	{
		u32 start, end;

		start = (u32)Au1500_PCI_MEM_START;
		end = (u32)Au1500_PCI_MEM_END;
		/* check for pci memory window */
		if ((phys_addr >= start) && ((phys_addr + size) < end))
			return (phys_t)
			       ((phys_addr - start) + Au1500_PCI_MEM_START);
	}
#endif

	/* All Au1x SOCs have a pcmcia controller */
	/* We setup our 32 bit pseudo addresses to be equal to the
	 * 36 bit addr >> 4, to make it easier to check the address
	 * and fix it.
	 * The Au1x socket 0 phys attribute address is 0xF 4000 0000.
	 * The pseudo address we use is 0xF400 0000. Any address over
	 * 0xF400 0000 is a pcmcia pseudo address.
	 */
	if ((phys_addr >= 0xF4000000) && (phys_addr < 0xFFFFFFFF)) {
		return (phys_t)(phys_addr << 4);
	}

	/* default nop */
	return phys_addr;
}
EXPORT_SYMBOL(__fixup_bigphys_addr);
#endif
