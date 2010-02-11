/*
 * Blackfin CPLB initialization
 *
 * Copyright 2008-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>

#include <asm/blackfin.h>
#include <asm/cplb.h>
#include <asm/cplbinit.h>
#include <asm/mem_map.h>

#if ANOMALY_05000263
# error the MPU will not function safely while Anomaly 05000263 applies
#endif

struct cplb_entry icplb_tbl[NR_CPUS][MAX_CPLBS];
struct cplb_entry dcplb_tbl[NR_CPUS][MAX_CPLBS];

int first_switched_icplb, first_switched_dcplb;
int first_mask_dcplb;

void __init generate_cplb_tables_cpu(unsigned int cpu)
{
	int i_d, i_i;
	unsigned long addr;
	unsigned long d_data, i_data;
	unsigned long d_cache = 0, i_cache = 0;

	printk(KERN_INFO "MPU: setting up cplb tables with memory protection\n");

#ifdef CONFIG_BFIN_EXTMEM_ICACHEABLE
	i_cache = CPLB_L1_CHBL | ANOMALY_05000158_WORKAROUND;
#endif

#ifdef CONFIG_BFIN_EXTMEM_DCACHEABLE
	d_cache = CPLB_L1_CHBL;
#ifdef CONFIG_BFIN_EXTMEM_WRITETHROUGH
	d_cache |= CPLB_L1_AOW | CPLB_WT;
#endif
#endif

	i_d = i_i = 0;

	/* Set up the zero page.  */
	dcplb_tbl[cpu][i_d].addr = 0;
	dcplb_tbl[cpu][i_d++].data = SDRAM_OOPS | PAGE_SIZE_1KB;

	icplb_tbl[cpu][i_i].addr = 0;
	icplb_tbl[cpu][i_i++].data = CPLB_VALID | i_cache | CPLB_USER_RD | PAGE_SIZE_1KB;

	/* Cover kernel memory with 4M pages.  */
	addr = 0;
	d_data = d_cache | CPLB_SUPV_WR | CPLB_VALID | PAGE_SIZE_4MB | CPLB_DIRTY;
	i_data = i_cache | CPLB_VALID | CPLB_PORTPRIO | PAGE_SIZE_4MB;

	for (; addr < memory_start; addr += 4 * 1024 * 1024) {
		dcplb_tbl[cpu][i_d].addr = addr;
		dcplb_tbl[cpu][i_d++].data = d_data;
		icplb_tbl[cpu][i_i].addr = addr;
		icplb_tbl[cpu][i_i++].data = i_data | (addr == 0 ? CPLB_USER_RD : 0);
	}

	/* Cover L1 memory.  One 4M area for code and data each is enough.  */
#if L1_DATA_A_LENGTH > 0 || L1_DATA_B_LENGTH > 0
	dcplb_tbl[cpu][i_d].addr = get_l1_data_a_start_cpu(cpu);
	dcplb_tbl[cpu][i_d++].data = L1_DMEMORY | PAGE_SIZE_4MB;
#endif
#if L1_CODE_LENGTH > 0
	icplb_tbl[cpu][i_i].addr = get_l1_code_start_cpu(cpu);
	icplb_tbl[cpu][i_i++].data = L1_IMEMORY | PAGE_SIZE_4MB;
#endif

	/* Cover L2 memory */
#if L2_LENGTH > 0
	dcplb_tbl[cpu][i_d].addr = L2_START;
	dcplb_tbl[cpu][i_d++].data = L2_DMEMORY;
	icplb_tbl[cpu][i_i].addr = L2_START;
	icplb_tbl[cpu][i_i++].data = L2_IMEMORY;
#endif

	first_mask_dcplb = i_d;
	first_switched_dcplb = i_d + (1 << page_mask_order);
	first_switched_icplb = i_i;

	while (i_d < MAX_CPLBS)
		dcplb_tbl[cpu][i_d++].data = 0;
	while (i_i < MAX_CPLBS)
		icplb_tbl[cpu][i_i++].data = 0;
}

void __init generate_cplb_tables_all(void)
{
}
