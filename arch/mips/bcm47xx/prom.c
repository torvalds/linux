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
#include <linux/memblock.h>
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

static unsigned long lowmem __initdata;

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
	 * less than that amount of ram it remaps the ram more often into the
	 * available space.
	 */

	/* Physical address, without mapping to any kernel segment */
	off = CPHYSADDR((unsigned long)prom_init);

	/* Accessing memory after 128 MiB will cause an exception */
	max = 128 << 20;

	for (mem = 1 << 20; mem < max; mem += 1 << 20) {
		/* Loop condition may be not enough, off may be over 1 MiB */
		if (off + mem >= max) {
			mem = max;
			pr_debug("Assume 128MB RAM\n");
			break;
		}
		if (!memcmp((void *)prom_init, (void *)prom_init + mem, 32))
			break;
	}
	lowmem = mem;

	/* Ignoring the last page when ddr size is 128M. Cached
	 * accesses to last page is causing the processor to prefetch
	 * using address above 128M stepping out of the ddr address
	 * space.
	 */
	if (c->cputype == CPU_74K && (mem == (128  << 20)))
		mem -= 0x1000;
	memblock_add(0, mem);
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

#if defined(CONFIG_BCM47XX_BCMA) && defined(CONFIG_HIGHMEM)

#define EXTVBASE	0xc0000000
#define ENTRYLO(x)	((pte_val(pfn_pte((x) >> _PFN_SHIFT, PAGE_KERNEL_UNCACHED)) >> 6) | 1)

#include <asm/tlbflush.h>

/* Stripped version of tlb_init, with the call to build_tlb_refill_handler
 * dropped. Calling it at this stage causes a hang.
 */
void early_tlb_init(void)
{
	write_c0_pagemask(PM_DEFAULT_MASK);
	write_c0_wired(0);
	temp_tlb_entry = current_cpu_data.tlbsize - 1;
	local_flush_tlb_all();
}

void __init bcm47xx_prom_highmem_init(void)
{
	unsigned long off = (unsigned long)prom_init;
	unsigned long extmem = 0;
	bool highmem_region = false;

	if (WARN_ON(bcm47xx_bus_type != BCM47XX_BUS_TYPE_BCMA))
		return;

	if (bcm47xx_bus.bcma.bus.chipinfo.id == BCMA_CHIP_ID_BCM4706)
		highmem_region = true;

	if (lowmem != 128 << 20 || !highmem_region)
		return;

	early_tlb_init();

	/* Add one temporary TLB entry to map SDRAM Region 2.
	 *      Physical        Virtual
	 *      0x80000000      0xc0000000      (1st: 256MB)
	 *      0x90000000      0xd0000000      (2nd: 256MB)
	 */
	add_temporary_entry(ENTRYLO(0x80000000),
			    ENTRYLO(0x80000000 + (256 << 20)),
			    EXTVBASE, PM_256M);

	off = EXTVBASE + __pa(off);
	for (extmem = 128 << 20; extmem < 512 << 20; extmem <<= 1) {
		if (!memcmp((void *)prom_init, (void *)(off + extmem), 16))
			break;
	}
	extmem -= lowmem;

	early_tlb_init();

	if (!extmem)
		return;

	pr_warn("Found %lu MiB of extra memory, but highmem is unsupported yet!\n",
		extmem >> 20);

	/* TODO: Register extra memory */
}

#endif /* defined(CONFIG_BCM47XX_BCMA) && defined(CONFIG_HIGHMEM) */
