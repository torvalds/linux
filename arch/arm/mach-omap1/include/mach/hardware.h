/*
 * arch/arm/mach-omap1/include/mach/hardware.h
 */

#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

#ifndef __ASSEMBLER__
/*
 * NOTE: Please use ioremap + __raw_read/write where possible instead of these
 */
extern u8 omap_readb(u32 pa);
extern u16 omap_readw(u32 pa);
extern u32 omap_readl(u32 pa);
extern void omap_writeb(u8 v, u32 pa);
extern void omap_writew(u16 v, u32 pa);
extern void omap_writel(u32 v, u32 pa);

#include <plat/tc.h>

/* Almost all documentation for chip and board memory maps assumes
 * BM is clear.  Most devel boards have a switch to control booting
 * from NOR flash (using external chipselect 3) rather than mask ROM,
 * which uses BM to interchange the physical CS0 and CS3 addresses.
 */
static inline u32 omap_cs0m_phys(void)
{
	return (omap_readl(EMIFS_CONFIG) & OMAP_EMIFS_CONFIG_BM)
			?  OMAP_CS3_PHYS : 0;
}

static inline u32 omap_cs3_phys(void)
{
	return (omap_readl(EMIFS_CONFIG) & OMAP_EMIFS_CONFIG_BM)
			? 0 : OMAP_CS3_PHYS;
}

#endif
#endif

#include <plat/hardware.h>
