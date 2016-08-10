#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/memblock.h>

#include <asm/setup.h>
#include <asm/bios_ebda.h>

/*
 * This function reserves all conventional PC system BIOS related
 * firmware memory areas (some of which are data, some of which
 * are code), that must not be used by the kernel as available
 * RAM.
 *
 * The BIOS places the EBDA/XBDA at the top of conventional
 * memory, and usually decreases the reported amount of
 * conventional memory (int 0x12) too.
 *
 * This means that as a first approximation on most systems we can
 * guess the reserved BIOS area by looking at the low BIOS RAM size
 * value and assume that everything above that value (up to 1MB) is
 * reserved.
 *
 * But life in firmware country is not that simple:
 *
 * - This code also contains a quirk for Dell systems that neglect
 *   to reserve the EBDA area in the 'RAM size' value ...
 *
 * - The same quirk also avoids a problem with the AMD768MPX
 *   chipset: reserve a page before VGA to prevent PCI prefetch
 *   into it (errata #56). (Usually the page is reserved anyways,
 *   unless you have no PS/2 mouse plugged in.)
 *
 * - Plus paravirt systems don't have a reliable value in the
 *   'BIOS RAM size' pointer we can rely on, so we must quirk
 *   them too.
 *
 * Due to those various problems this function is deliberately
 * very conservative and tries to err on the side of reserving
 * too much, to not risk reserving too little.
 *
 * Losing a small amount of memory in the bottom megabyte is
 * rarely a problem, as long as we have enough memory to install
 * the SMP bootup trampoline which *must* be in this area.
 *
 * Using memory that is in use by the BIOS or by some DMA device
 * the BIOS didn't shut down *is* a big problem to the kernel,
 * obviously.
 */

#define BIOS_RAM_SIZE_KB_PTR	0x413

#define BIOS_START_MIN		0x20000U	/* 128K, less than this is insane */
#define BIOS_START_MAX		0x9f000U	/* 640K, absolute maximum */

void __init reserve_bios_regions(void)
{
	unsigned int bios_start, ebda_start;

	/*
	 * NOTE: In a paravirtual environment the BIOS reserved
	 * area is absent. We'll just have to assume that the
	 * paravirt case can handle memory setup correctly,
	 * without our help.
	 */
	if (!x86_platform.legacy.reserve_bios_regions)
		return;

	/*
	 * BIOS RAM size is encoded in kilobytes, convert it
	 * to bytes to get a first guess at where the BIOS
	 * firmware area starts:
	 */
	bios_start = *(unsigned short *)__va(BIOS_RAM_SIZE_KB_PTR);
	bios_start <<= 10;

	/*
	 * If bios_start is less than 128K, assume it is bogus
	 * and bump it up to 640K.  Similarly, if bios_start is above 640K,
	 * don't trust it.
	 */
	if (bios_start < BIOS_START_MIN || bios_start > BIOS_START_MAX)
		bios_start = BIOS_START_MAX;

	/* Get the start address of the EBDA page: */
	ebda_start = get_bios_ebda();

	/*
	 * If the EBDA start address is sane and is below the BIOS region,
	 * then also reserve everything from the EBDA start address up to
	 * the BIOS region.
	 */
	if (ebda_start >= BIOS_START_MIN && ebda_start < bios_start)
		bios_start = ebda_start;

	/* Reserve all memory between bios_start and the 1MB mark: */
	memblock_reserve(bios_start, 0x100000 - bios_start);
}
