/*
 * BRIEF MODULE DESCRIPTION
 *	Galileo EV96100 setup.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * This file was derived from Carsten Langgaard's
 * arch/mips/mips-boards/atlas/atlas_setup.c.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/pci.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/gt64120.h>
#include <asm/galileo-boards/ev96100int.h>


extern char *__init prom_getcmdline(void);

extern void mips_reboot_setup(void);

unsigned char mac_0_1[12];

void __init plat_mem_setup(void)
{
	unsigned int config = read_c0_config();
	unsigned int status = read_c0_status();
	unsigned int info = read_c0_info();
	u32 tmp;

	char *argptr;

	clear_c0_status(ST0_FR);

	if (config & 0x8)
		printk("Secondary cache is enabled\n");
	else
		printk("Secondary cache is disabled\n");

	if (status & (1 << 27))
		printk("User-mode cache ops enabled\n");
	else
		printk("User-mode cache ops disabled\n");

	printk("CP0 info reg: %x\n", (unsigned) info);
	if (info & (1 << 28))
		printk("burst mode Scache RAMS\n");
	else
		printk("pipelined Scache RAMS\n");

	if (info & 0x1)
		printk("Atomic Enable is set\n");

	argptr = prom_getcmdline();
#ifdef CONFIG_SERIAL_CONSOLE
	if (strstr(argptr, "console=") == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif

	mips_reboot_setup();

	set_io_port_base(KSEG1);
	ioport_resource.start = GT_PCI_IO_BASE;
	ioport_resource.end = GT_PCI_IO_BASE + 0x01ffffff;

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
#endif


	/*
	 * Setup GT controller master bit so we can do config cycles
	 */

	/* Clear cause register bits */
	GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
				     GT_INTRCAUSE_TARABORT0_BIT));
	/* Setup address */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (0 << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (0 << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((PCI_COMMAND / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);

	udelay(2);
	tmp = GT_READ(GT_PCI0_CFGDATA_OFS);

	tmp |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR);
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (0 << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (0 << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((PCI_COMMAND / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);
	udelay(2);
	GT_WRITE(GT_PCI0_CFGDATA_OFS, tmp);

	/* Setup address */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (0 << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (0 << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((PCI_COMMAND / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);

	udelay(2);
	tmp = GT_READ(GT_PCI0_CFGDATA_OFS);
}

unsigned short get_gt_devid(void)
{
	u32 gt_devid;

	/* Figure out if this is a gt96100 or gt96100A */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (0 << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (0 << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((PCI_VENDOR_ID / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);

	udelay(4);
	gt_devid = GT_READ(GT_PCI0_CFGDATA_OFS);

	return gt_devid >> 16;
}
