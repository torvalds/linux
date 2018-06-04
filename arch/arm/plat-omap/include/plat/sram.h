/* SPDX-License-Identifier: GPL-2.0 */
int omap_sram_init(void);

void omap_map_sram(unsigned long start, unsigned long size,
			unsigned long skip, int cached);
void omap_sram_reset(void);

extern void *omap_sram_push(void *funcp, unsigned long size);
