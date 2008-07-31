#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bios_ebda.h>

#define BIOS_LOWMEM_KILOBYTES 0x413

/*
 * The BIOS places the EBDA/XBDA at the top of conventional
 * memory, and usually decreases the reported amount of
 * conventional memory (int 0x12) too. This also contains a
 * workaround for Dell systems that neglect to reserve EBDA.
 * The same workaround also avoids a problem with the AMD768MPX
 * chipset: reserve a page before VGA to prevent PCI prefetch
 * into it (errata #56). Usually the page is reserved anyways,
 * unless you have no PS/2 mouse plugged in.
 */
void __init reserve_ebda_region(void)
{
	unsigned int lowmem, ebda_addr;

	/* To determine the position of the EBDA and the */
	/* end of conventional memory, we need to look at */
	/* the BIOS data area. In a paravirtual environment */
	/* that area is absent. We'll just have to assume */
	/* that the paravirt case can handle memory setup */
	/* correctly, without our help. */
	if (paravirt_enabled())
		return;

	/* end of low (conventional) memory */
	lowmem = *(unsigned short *)__va(BIOS_LOWMEM_KILOBYTES);
	lowmem <<= 10;

	/* start of EBDA area */
	ebda_addr = get_bios_ebda();

	/* Fixup: bios puts an EBDA in the top 64K segment */
	/* of conventional memory, but does not adjust lowmem. */
	if ((lowmem - ebda_addr) <= 0x10000)
		lowmem = ebda_addr;

	/* Fixup: bios does not report an EBDA at all. */
	/* Some old Dells seem to need 4k anyhow (bugzilla 2990) */
	if ((ebda_addr == 0) && (lowmem >= 0x9f000))
		lowmem = 0x9f000;

	/* Paranoia: should never happen, but... */
	if ((lowmem == 0) || (lowmem >= 0x100000))
		lowmem = 0x9f000;

	/* reserve all memory between lowmem and the 1MB mark */
	reserve_early_overlap_ok(lowmem, 0x100000, "BIOS reserved");
}
