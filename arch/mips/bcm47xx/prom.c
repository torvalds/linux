/*
 *  Copyright (C) 2004 Florian Schirmer <jolt@tuxbox.org>
 *  Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 *  Copyright (C) 2010-2012 Hauke Mehrtens <hauke@hauke-m.de>
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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/ssb/ssb_driver_chipcommon.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/smp.h>
#include <asm/bootinfo.h>
#include <bcm47xx.h>
#include <bcm47xx_board.h>


static char bcm47xx_system_type[20] = "Broadcom BCM47XX";

const char *get_system_type(void)
{
	return bcm47xx_system_type;
}

__init void bcm47xx_set_system_type(u16 chip_id)
{
	snprintf(bcm47xx_system_type, sizeof(bcm47xx_system_type),
		 (chip_id > 0x9999) ? "Broadcom BCM%d" :
				      "Broadcom BCM%04X",
		 chip_id);
}

static __init void prom_init_mem(void)
{
	unsigned long mem;
	unsigned long max;
	unsigned long off;
	struct cpuinfo_mips *c = &current_cpu_data;

	/* Figure out memory size by finding aliases.
	 *
	 * We should theoretically use the mapping from CFE using cfe_enummem().
	 * However as the BCM47XX is mostly used on low-memory systems, we
	 * want to reuse the memory used by CFE (around 4MB). That means cfe_*
	 * functions stop to work at some point during the boot, we should only
	 * call them at the beginning of the boot.
	 *
	 * BCM47XX uses 128MB for addressing the ram, if the system contains
	 * less that that amount of ram it remaps the ram more often into the
	 * available space.
	 * Accessing memory after 128MB will cause an exception.
	 * max contains the biggest possible address supported by the platform.
	 * If the method wants to try something above we assume 128MB ram.
	 */
	off = (unsigned long)prom_init;
	max = off | ((128 << 20) - 1);
	for (mem = (1 << 20); mem < (128 << 20); mem += (1 << 20)) {
		if ((off + mem) > max) {
			mem = (128 << 20);
			printk(KERN_DEBUG "assume 128MB RAM\n");
			break;
		}
		if (!memcmp(prom_init, prom_init + mem, 32))
			break;
	}

	/* Ignoring the last page when ddr size is 128M. Cached
	 * accesses to last page is causing the processor to prefetch
	 * using address above 128M stepping out of the ddr address
	 * space.
	 */
	if (c->cputype == CPU_74K && (mem == (128  << 20)))
		mem -= 0x1000;

	add_memory_region(0, mem, BOOT_MEM_RAM);
}

/*
 * This is the first serial on the chip common core, it is at this position
 * for sb (ssb) and ai (bcma) bus.
 */
#define BCM47XX_SERIAL_ADDR (SSB_ENUM_BASE + SSB_CHIPCO_UART0_DATA)

void __init prom_init(void)
{
	prom_init_mem();
	setup_8250_early_printk_port(CKSEG1ADDR(BCM47XX_SERIAL_ADDR), 0, 0);
}

void __init prom_free_prom_memory(void)
{
}
