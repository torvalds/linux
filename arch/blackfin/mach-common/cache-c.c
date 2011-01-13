/*
 * Blackfin cache control code (simpler control-style functions)
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <asm/blackfin.h>
#include <asm/cplbinit.h>

/* Invalidate the Entire Data cache by
 * clearing DMC[1:0] bits
 */
void blackfin_invalidate_entire_dcache(void)
{
	u32 dmem = bfin_read_DMEM_CONTROL();
	bfin_write_DMEM_CONTROL(dmem & ~0xc);
	SSYNC();
	bfin_write_DMEM_CONTROL(dmem);
	SSYNC();
}

/* Invalidate the Entire Instruction cache by
 * clearing IMC bit
 */
void blackfin_invalidate_entire_icache(void)
{
	u32 imem = bfin_read_IMEM_CONTROL();
	bfin_write_IMEM_CONTROL(imem & ~0x4);
	SSYNC();
	bfin_write_IMEM_CONTROL(imem);
	SSYNC();
}

#if defined(CONFIG_BFIN_ICACHE) || defined(CONFIG_BFIN_DCACHE)

static void
bfin_cache_init(struct cplb_entry *cplb_tbl, unsigned long cplb_addr,
                unsigned long cplb_data, unsigned long mem_control,
                unsigned long mem_mask)
{
	int i;

	for (i = 0; i < MAX_CPLBS; i++) {
		bfin_write32(cplb_addr + i * 4, cplb_tbl[i].addr);
		bfin_write32(cplb_data + i * 4, cplb_tbl[i].data);
	}

	_enable_cplb(mem_control, mem_mask);
}

#ifdef CONFIG_BFIN_ICACHE
void __cpuinit bfin_icache_init(struct cplb_entry *icplb_tbl)
{
	bfin_cache_init(icplb_tbl, ICPLB_ADDR0, ICPLB_DATA0, IMEM_CONTROL,
		(IMC | ENICPLB));
}
#endif

#ifdef CONFIG_BFIN_DCACHE
void __cpuinit bfin_dcache_init(struct cplb_entry *dcplb_tbl)
{
	/*
	 *  Anomaly notes:
	 *  05000287 - We implement workaround #2 - Change the DMEM_CONTROL
	 *  register, so that the port preferences for DAG0 and DAG1 are set
	 *  to port B
	 */
	bfin_cache_init(dcplb_tbl, DCPLB_ADDR0, DCPLB_DATA0, DMEM_CONTROL,
		(DMEM_CNTR | PORT_PREF0 | (ANOMALY_05000287 ? PORT_PREF1 : 0)));
}
#endif

#endif
