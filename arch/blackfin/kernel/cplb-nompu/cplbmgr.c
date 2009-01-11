/*
 * File:         arch/blackfin/kernel/cplb-nompu-c/cplbmgr.c
 * Based on:     arch/blackfin/kernel/cplb-mpu/cplbmgr.c
 * Author:       Michael McTernan <mmcternan@airvana.com>
 *
 * Created:      01Nov2008
 * Description:  CPLB miss handler.
 *
 * Modified:
 *               Copyright 2008 Airvana Inc.
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <asm/blackfin.h>
#include <asm/cplbinit.h>
#include <asm/cplb.h>
#include <asm/mmu_context.h>

/*
 * WARNING
 *
 * This file is compiled with certain -ffixed-reg options.  We have to
 * make sure not to call any functions here that could clobber these
 * registers.
 */

int nr_dcplb_miss[NR_CPUS], nr_icplb_miss[NR_CPUS];
int nr_dcplb_supv_miss[NR_CPUS], nr_icplb_supv_miss[NR_CPUS];
int nr_cplb_flush[NR_CPUS], nr_dcplb_prot[NR_CPUS];

#ifdef CONFIG_EXCPT_IRQ_SYSC_L1
#define MGR_ATTR __attribute__((l1_text))
#else
#define MGR_ATTR
#endif

/*
 * We're in an exception handler.  The normal cli nop nop workaround
 * isn't going to do very much, as the only thing that can interrupt
 * us is an NMI, and the cli isn't going to stop that.
 */
#define NOWA_SSYNC __asm__ __volatile__ ("ssync;")

/* Anomaly handlers provide SSYNCs, so avoid extra if anomaly is present */
#if ANOMALY_05000125

#define bfin_write_DMEM_CONTROL_SSYNC(v)    bfin_write_DMEM_CONTROL(v)
#define bfin_write_IMEM_CONTROL_SSYNC(v)    bfin_write_IMEM_CONTROL(v)

#else

#define bfin_write_DMEM_CONTROL_SSYNC(v) \
    do { NOWA_SSYNC; bfin_write_DMEM_CONTROL(v); NOWA_SSYNC; } while (0)
#define bfin_write_IMEM_CONTROL_SSYNC(v) \
    do { NOWA_SSYNC; bfin_write_IMEM_CONTROL(v); NOWA_SSYNC; } while (0)

#endif

static inline void write_dcplb_data(int cpu, int idx, unsigned long data,
				    unsigned long addr)
{
	unsigned long ctrl = bfin_read_DMEM_CONTROL();
	bfin_write_DMEM_CONTROL_SSYNC(ctrl & ~ENDCPLB);
	bfin_write32(DCPLB_DATA0 + idx * 4, data);
	bfin_write32(DCPLB_ADDR0 + idx * 4, addr);
	bfin_write_DMEM_CONTROL_SSYNC(ctrl);

#ifdef CONFIG_CPLB_INFO
	dcplb_tbl[cpu][idx].addr = addr;
	dcplb_tbl[cpu][idx].data = data;
#endif
}

static inline void write_icplb_data(int cpu, int idx, unsigned long data,
				    unsigned long addr)
{
	unsigned long ctrl = bfin_read_IMEM_CONTROL();

	bfin_write_IMEM_CONTROL_SSYNC(ctrl & ~ENICPLB);
	bfin_write32(ICPLB_DATA0 + idx * 4, data);
	bfin_write32(ICPLB_ADDR0 + idx * 4, addr);
	bfin_write_IMEM_CONTROL_SSYNC(ctrl);

#ifdef CONFIG_CPLB_INFO
	icplb_tbl[cpu][idx].addr = addr;
	icplb_tbl[cpu][idx].data = data;
#endif
}

/*
 * Given the contents of the status register, return the index of the
 * CPLB that caused the fault.
 */
static inline int faulting_cplb_index(int status)
{
	int signbits = __builtin_bfin_norm_fr1x32(status & 0xFFFF);
	return 30 - signbits;
}

/*
 * Given the contents of the status register and the DCPLB_DATA contents,
 * return true if a write access should be permitted.
 */
static inline int write_permitted(int status, unsigned long data)
{
	if (status & FAULT_USERSUPV)
		return !!(data & CPLB_SUPV_WR);
	else
		return !!(data & CPLB_USER_WR);
}

/* Counters to implement round-robin replacement.  */
static int icplb_rr_index[NR_CPUS] PDT_ATTR;
static int dcplb_rr_index[NR_CPUS] PDT_ATTR;

/*
 * Find an ICPLB entry to be evicted and return its index.
 */
static int evict_one_icplb(int cpu)
{
	int i = first_switched_icplb + icplb_rr_index[cpu];
	if (i >= MAX_CPLBS) {
		i -= MAX_CPLBS - first_switched_icplb;
		icplb_rr_index[cpu] -= MAX_CPLBS - first_switched_icplb;
	}
	icplb_rr_index[cpu]++;
	return i;
}

static int evict_one_dcplb(int cpu)
{
	int i = first_switched_dcplb + dcplb_rr_index[cpu];
	if (i >= MAX_CPLBS) {
		i -= MAX_CPLBS - first_switched_dcplb;
		dcplb_rr_index[cpu] -= MAX_CPLBS - first_switched_dcplb;
	}
	dcplb_rr_index[cpu]++;
	return i;
}

MGR_ATTR static int icplb_miss(int cpu)
{
	unsigned long addr = bfin_read_ICPLB_FAULT_ADDR();
	int status = bfin_read_ICPLB_STATUS();
	int idx;
	unsigned long i_data, base, addr1, eaddr;

	nr_icplb_miss[cpu]++;
	if (unlikely(status & FAULT_USERSUPV))
		nr_icplb_supv_miss[cpu]++;

	base = 0;
	for (idx = 0; idx < icplb_nr_bounds; idx++) {
		eaddr = icplb_bounds[idx].eaddr;
		if (addr < eaddr)
			break;
		base = eaddr;
	}
	if (unlikely(idx == icplb_nr_bounds))
		return CPLB_NO_ADDR_MATCH;

	i_data = icplb_bounds[idx].data;
	if (unlikely(i_data == 0))
		return CPLB_NO_ADDR_MATCH;

	addr1 = addr & ~(SIZE_4M - 1);
	addr &= ~(SIZE_1M - 1);
	i_data |= PAGE_SIZE_1MB;
	if (addr1 >= base && (addr1 + SIZE_4M) <= eaddr) {
		/*
		 * This works because
		 * (PAGE_SIZE_4MB & PAGE_SIZE_1MB) == PAGE_SIZE_1MB.
		 */
		i_data |= PAGE_SIZE_4MB;
		addr = addr1;
	}

	/* Pick entry to evict */
	idx = evict_one_icplb(cpu);

	write_icplb_data(cpu, idx, i_data, addr);

	return CPLB_RELOADED;
}

MGR_ATTR static int dcplb_miss(int cpu)
{
	unsigned long addr = bfin_read_DCPLB_FAULT_ADDR();
	int status = bfin_read_DCPLB_STATUS();
	int idx;
	unsigned long d_data, base, addr1, eaddr;

	nr_dcplb_miss[cpu]++;
	if (unlikely(status & FAULT_USERSUPV))
		nr_dcplb_supv_miss[cpu]++;

	base = 0;
	for (idx = 0; idx < dcplb_nr_bounds; idx++) {
		eaddr = dcplb_bounds[idx].eaddr;
		if (addr < eaddr)
			break;
		base = eaddr;
	}
	if (unlikely(idx == dcplb_nr_bounds))
		return CPLB_NO_ADDR_MATCH;

	d_data = dcplb_bounds[idx].data;
	if (unlikely(d_data == 0))
		return CPLB_NO_ADDR_MATCH;

	addr1 = addr & ~(SIZE_4M - 1);
	addr &= ~(SIZE_1M - 1);
	d_data |= PAGE_SIZE_1MB;
	if (addr1 >= base && (addr1 + SIZE_4M) <= eaddr) {
		/*
		 * This works because
		 * (PAGE_SIZE_4MB & PAGE_SIZE_1MB) == PAGE_SIZE_1MB.
		 */
		d_data |= PAGE_SIZE_4MB;
		addr = addr1;
	}

	/* Pick entry to evict */
	idx = evict_one_dcplb(cpu);

	write_dcplb_data(cpu, idx, d_data, addr);

	return CPLB_RELOADED;
}

MGR_ATTR static noinline int dcplb_protection_fault(int cpu)
{
	int status = bfin_read_DCPLB_STATUS();

	nr_dcplb_prot[cpu]++;

	if (likely(status & FAULT_RW)) {
		int idx = faulting_cplb_index(status);
		unsigned long regaddr = DCPLB_DATA0 + idx * 4;
		unsigned long data = bfin_read32(regaddr);

		/* Check if fault is to dirty a clean page */
		if (!(data & CPLB_WT) && !(data & CPLB_DIRTY) &&
		    write_permitted(status, data)) {

			dcplb_tbl[cpu][idx].data = data;
			bfin_write32(regaddr, data);
			return CPLB_RELOADED;
		}
	}

	return CPLB_PROT_VIOL;
}

MGR_ATTR int cplb_hdr(int seqstat, struct pt_regs *regs)
{
	int cause = seqstat & 0x3f;
	unsigned int cpu = smp_processor_id();
	switch (cause) {
	case 0x2C:
		return icplb_miss(cpu);
	case 0x26:
		return dcplb_miss(cpu);
	default:
		if (unlikely(cause == 0x23))
			return dcplb_protection_fault(cpu);

		return CPLB_UNKNOWN_ERR;
	}
}
