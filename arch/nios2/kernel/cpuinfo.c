/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 *
 * Based on cpuinfo.c from microblaze
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/of.h>
#include <asm/cpuinfo.h>

struct cpuinfo cpuinfo;

#define err_cpu(x) \
	pr_err("ERROR: Nios II " x " different for kernel and DTS\n");

static inline u32 fcpu(struct device_node *cpu, const char *n)
{
	u32 val = 0;

	of_property_read_u32(cpu, n, &val);

	return val;
}

static inline u32 fcpu_has(struct device_node *cpu, const char *n)
{
	return of_get_property(cpu, n, NULL) ? 1 : 0;
}

void __init setup_cpuinfo(void)
{
	struct device_node *cpu;
	const char *str;
	int len;

	cpu = of_find_node_by_type(NULL, "cpu");
	if (!cpu)
		panic("%s: No CPU found in devicetree!\n", __func__);

	cpuinfo.mmu = fcpu_has(cpu, "ALTR,has-mmu");
	if (!cpuinfo.mmu) {
		panic("ERROR: Can't get 'ALTR,has-mmu' from device tree. Only support"
			" Nios II with MMU enabled. Please enable MMU in Nios II "
			"hardware.");
	}

	cpuinfo.cpu_clock_freq = fcpu(cpu, "clock-frequency");

	str = of_get_property(cpu, "ALTR,implementation", &len);
	if (str)
		strlcpy(cpuinfo.cpu_impl, str, sizeof(cpuinfo.cpu_impl));
	else
		strcpy(cpuinfo.cpu_impl, "<unknown>");

	cpuinfo.has_div = fcpu_has(cpu, "ALTR,has-div");
	cpuinfo.has_mul = fcpu_has(cpu, "ALTR,has-mul");
	cpuinfo.has_mulx = fcpu_has(cpu, "ALTR,has-mulx");

#ifdef CONFIG_NIOS2_HW_DIV_SUPPORT
	if (!cpuinfo.has_div)
		err_cpu("DIV");
#endif
#ifdef CONFIG_NIOS2_HW_MUL_SUPPORT
	if (!cpuinfo.has_mul)
		err_cpu("MUL");
#endif
#ifdef CONFIG_NIOS2_HW_MULX_SUPPORT
	if (!cpuinfo.has_mulx)
		err_cpu("MULX");
#endif

	cpuinfo.icache_line_size = fcpu(cpu, "icache-line-size");
	cpuinfo.icache_size = fcpu(cpu, "icache-size");
	cpuinfo.dcache_line_size = fcpu(cpu, "dcache-line-size");
	cpuinfo.dcache_size = fcpu(cpu, "dcache-size");

	cpuinfo.tlb_pid_num_bits = fcpu(cpu, "ALTR,pid-num-bits");
	cpuinfo.tlb_num_ways = fcpu(cpu, "ALTR,tlb-num-ways");
	cpuinfo.tlb_num_ways_log2 = ilog2(cpuinfo.tlb_num_ways);
	cpuinfo.tlb_num_entries = fcpu(cpu, "ALTR,tlb-num-entries");
	cpuinfo.tlb_num_lines = cpuinfo.tlb_num_entries / cpuinfo.tlb_num_ways;
	cpuinfo.tlb_ptr_sz = fcpu(cpu, "ALTR,tlb-ptr-sz");

	cpuinfo.reset_addr = fcpu(cpu, "ALTR,reset-addr");
	cpuinfo.exception_addr = fcpu(cpu, "ALTR,exception-addr");
	cpuinfo.fast_tlb_miss_exc_addr = fcpu(cpu, "ALTR,fast-tlb-miss-addr");
}

#ifdef CONFIG_PROC_FS

/*
 * Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	int count = 0;
	const u32 clockfreq = cpuinfo.cpu_clock_freq;

	count = seq_printf(m,
			"CPU:\t\tNios II/%s\n"
			"MMU:\t\t%s\n"
			"FPU:\t\tnone\n"
			"Clocking:\t%u.%02u MHz\n"
			"BogoMips:\t%lu.%02lu\n"
			"Calibration:\t%lu loops\n",
			cpuinfo.cpu_impl,
			cpuinfo.mmu ? "present" : "none",
			clockfreq / 1000000, (clockfreq / 100000) % 10,
			(loops_per_jiffy * HZ) / 500000,
			((loops_per_jiffy * HZ) / 5000) % 100,
			(loops_per_jiffy * HZ));

	count += seq_printf(m,
			"HW:\n"
			" MUL:\t\t%s\n"
			" MULX:\t\t%s\n"
			" DIV:\t\t%s\n",
			cpuinfo.has_mul ? "yes" : "no",
			cpuinfo.has_mulx ? "yes" : "no",
			cpuinfo.has_div ? "yes" : "no");

	count += seq_printf(m,
			"Icache:\t\t%ukB, line length: %u\n",
			cpuinfo.icache_size >> 10,
			cpuinfo.icache_line_size);

	count += seq_printf(m,
			"Dcache:\t\t%ukB, line length: %u\n",
			cpuinfo.dcache_size >> 10,
			cpuinfo.dcache_line_size);

	count += seq_printf(m,
			"TLB:\t\t%u ways, %u entries, %u PID bits\n",
			cpuinfo.tlb_num_ways,
			cpuinfo.tlb_num_entries,
			cpuinfo.tlb_pid_num_bits);

	return 0;
}

static void *cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	unsigned long i = *pos;

	return i < num_possible_cpus() ? (void *) (i + 1) : NULL;
}

static void *cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return cpuinfo_start(m, pos);
}

static void cpuinfo_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= cpuinfo_start,
	.next	= cpuinfo_next,
	.stop	= cpuinfo_stop,
	.show	= show_cpuinfo
};

#endif /* CONFIG_PROC_FS */
