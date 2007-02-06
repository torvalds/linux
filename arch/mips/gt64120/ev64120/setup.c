/*
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
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
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timex.h>
#include <linux/pm.h>

#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/reboot.h>
#include <asm/traps.h>
#include <linux/bootmem.h>

unsigned long gt64120_base = KSEG1ADDR(0x14000000);

/* These functions are used for rebooting or halting the machine*/
extern void galileo_machine_restart(char *command);
extern void galileo_machine_halt(void);
extern void galileo_machine_power_off(void);
/*
 *This structure holds pointers to the pci configuration space accesses
 *and interrupts allocating routine for device over the PCI
 */
extern struct pci_ops galileo_pci_ops;

void __init prom_free_prom_memory(void)
{
}

/*
 * Initializes basic routines and structures pointers, memory size (as
 * given by the bios and saves the command line.
 */

void __init plat_mem_setup(void)
{
	_machine_restart = galileo_machine_restart;
	_machine_halt = galileo_machine_halt;
	pm_power_off = galileo_machine_power_off;

	set_io_port_base(KSEG1);
}

const char *get_system_type(void)
{
	return "Galileo EV64120A";
}

/*
 * Kernel arguments passed by the firmware
 *
 * $a0 - nothing
 * $a1 - holds a pointer to the eprom parameters
 * $a2 - nothing
 */

void __init prom_init(void)
{
	mips_machgroup = MACH_GROUP_GALILEO;
	mips_machtype = MACH_EV64120A;

	add_memory_region(0, 32 << 20, BOOT_MEM_RAM);
}
