#ifndef __PLAT_OMAP_FB_H__
#define __PLAT_OMAP_FB_H__

extern unsigned long omapfb_reserve_sram(unsigned long sram_pstart,
					 unsigned long sram_vstart,
					 unsigned long sram_size,
					 unsigned long pstart_avail,
					 unsigned long size_avail);

#endif /* __PLAT_OMAP_FB_H__ */
