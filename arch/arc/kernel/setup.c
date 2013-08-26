/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/of_fdt.h>
#include <linux/cache.h>
#include <asm/sections.h>
#include <asm/arcregs.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/unwind.h>
#include <asm/clk.h>
#include <asm/mach_desc.h>

#define FIX_PTR(x)  __asm__ __volatile__(";" : "+r"(x))

int running_on_hw = 1;	/* vs. on ISS */

char __initdata command_line[COMMAND_LINE_SIZE];
struct machine_desc *machine_desc;

struct task_struct *_current_task[NR_CPUS];	/* For stack switching */

struct cpuinfo_arc cpuinfo_arc700[NR_CPUS];


void read_arc_build_cfg_regs(void)
{
	struct bcr_perip uncached_space;
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[smp_processor_id()];
	FIX_PTR(cpu);

	READ_BCR(AUX_IDENTITY, cpu->core);

	cpu->timers = read_aux_reg(ARC_REG_TIMERS_BCR);
	cpu->vec_base = read_aux_reg(AUX_INTR_VEC_BASE);

	READ_BCR(ARC_REG_D_UNCACH_BCR, uncached_space);
	cpu->uncached_base = uncached_space.start << 24;

	cpu->extn.mul = read_aux_reg(ARC_REG_MUL_BCR);
	cpu->extn.swap = read_aux_reg(ARC_REG_SWAP_BCR);
	cpu->extn.norm = read_aux_reg(ARC_REG_NORM_BCR);
	cpu->extn.minmax = read_aux_reg(ARC_REG_MIXMAX_BCR);
	cpu->extn.barrel = read_aux_reg(ARC_REG_BARREL_BCR);
	READ_BCR(ARC_REG_MAC_BCR, cpu->extn_mac_mul);

	cpu->extn.ext_arith = read_aux_reg(ARC_REG_EXTARITH_BCR);
	cpu->extn.crc = read_aux_reg(ARC_REG_CRC_BCR);

	/* Note that we read the CCM BCRs independent of kernel config
	 * This is to catch the cases where user doesn't know that
	 * CCMs are present in hardware build
	 */
	{
		struct bcr_iccm iccm;
		struct bcr_dccm dccm;
		struct bcr_dccm_base dccm_base;
		unsigned int bcr_32bit_val;

		bcr_32bit_val = read_aux_reg(ARC_REG_ICCM_BCR);
		if (bcr_32bit_val) {
			iccm = *((struct bcr_iccm *)&bcr_32bit_val);
			cpu->iccm.base_addr = iccm.base << 16;
			cpu->iccm.sz = 0x2000 << (iccm.sz - 1);
		}

		bcr_32bit_val = read_aux_reg(ARC_REG_DCCM_BCR);
		if (bcr_32bit_val) {
			dccm = *((struct bcr_dccm *)&bcr_32bit_val);
			cpu->dccm.sz = 0x800 << (dccm.sz);

			READ_BCR(ARC_REG_DCCMBASE_BCR, dccm_base);
			cpu->dccm.base_addr = dccm_base.addr << 8;
		}
	}

	READ_BCR(ARC_REG_XY_MEM_BCR, cpu->extn_xymem);

	read_decode_mmu_bcr();
	read_decode_cache_bcr();

	READ_BCR(ARC_REG_FP_BCR, cpu->fp);
	READ_BCR(ARC_REG_DPFP_BCR, cpu->dpfp);
}

static const struct cpuinfo_data arc_cpu_tbl[] = {
	{ {0x10, "ARCTangent A5"}, 0x1F},
	{ {0x20, "ARC 600"      }, 0x2F},
	{ {0x30, "ARC 700"      }, 0x33},
	{ {0x34, "ARC 700 R4.10"}, 0x34},
	{ {0x00, NULL		} }
};

char *arc_cpu_mumbojumbo(int cpu_id, char *buf, int len)
{
	int n = 0;
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[cpu_id];
	struct bcr_identity *core = &cpu->core;
	const struct cpuinfo_data *tbl;
	int be = 0;
#ifdef CONFIG_CPU_BIG_ENDIAN
	be = 1;
#endif
	FIX_PTR(cpu);

	n += scnprintf(buf + n, len - n,
		       "\nARC IDENTITY\t: Family [%#02x]"
		       " Cpu-id [%#02x] Chip-id [%#4x]\n",
		       core->family, core->cpu_id,
		       core->chip_id);

	for (tbl = &arc_cpu_tbl[0]; tbl->info.id != 0; tbl++) {
		if ((core->family >= tbl->info.id) &&
		    (core->family <= tbl->up_range)) {
			n += scnprintf(buf + n, len - n,
				       "processor\t: %s %s\n",
				       tbl->info.str,
				       be ? "[Big Endian]" : "");
			break;
		}
	}

	if (tbl->info.id == 0)
		n += scnprintf(buf + n, len - n, "UNKNOWN ARC Processor\n");

	n += scnprintf(buf + n, len - n, "CPU speed\t: %u.%02u Mhz\n",
		       (unsigned int)(arc_get_core_freq() / 1000000),
		       (unsigned int)(arc_get_core_freq() / 10000) % 100);

	n += scnprintf(buf + n, len - n, "Timers\t\t: %s %s\n",
		       (cpu->timers & 0x200) ? "TIMER1" : "",
		       (cpu->timers & 0x100) ? "TIMER0" : "");

	n += scnprintf(buf + n, len - n, "Vect Tbl Base\t: %#x\n",
		       cpu->vec_base);

	n += scnprintf(buf + n, len - n, "UNCACHED Base\t: %#x\n",
		       cpu->uncached_base);

	return buf;
}

static const struct id_to_str mul_type_nm[] = {
	{ 0x0, "N/A"},
	{ 0x1, "32x32 (spl Result Reg)" },
	{ 0x2, "32x32 (ANY Result Reg)" }
};

static const struct id_to_str mac_mul_nm[] = {
	{0x0, "N/A"},
	{0x1, "N/A"},
	{0x2, "Dual 16 x 16"},
	{0x3, "N/A"},
	{0x4, "32x16"},
	{0x5, "N/A"},
	{0x6, "Dual 16x16 and 32x16"}
};

char *arc_extn_mumbojumbo(int cpu_id, char *buf, int len)
{
	int n = 0;
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[cpu_id];

	FIX_PTR(cpu);
#define IS_AVAIL1(var, str)	((var) ? str : "")
#define IS_AVAIL2(var, str)	((var == 0x2) ? str : "")
#define IS_USED(cfg)		(IS_ENABLED(cfg) ? "(in-use)" : "(not used)")

	n += scnprintf(buf + n, len - n,
		       "Extn [700-Base]\t: %s %s %s %s %s %s\n",
		       IS_AVAIL2(cpu->extn.norm, "norm,"),
		       IS_AVAIL2(cpu->extn.barrel, "barrel-shift,"),
		       IS_AVAIL1(cpu->extn.swap, "swap,"),
		       IS_AVAIL2(cpu->extn.minmax, "minmax,"),
		       IS_AVAIL1(cpu->extn.crc, "crc,"),
		       IS_AVAIL2(cpu->extn.ext_arith, "ext-arith"));

	n += scnprintf(buf + n, len - n, "Extn [700-MPY]\t: %s",
		       mul_type_nm[cpu->extn.mul].str);

	n += scnprintf(buf + n, len - n, "   MAC MPY: %s\n",
		       mac_mul_nm[cpu->extn_mac_mul.type].str);

	if (cpu->core.family == 0x34) {
		n += scnprintf(buf + n, len - n,
		"Extn [700-4.10]\t: LLOCK/SCOND %s, SWAPE %s, RTSC %s\n",
			       IS_USED(CONFIG_ARC_HAS_LLSC),
			       IS_USED(CONFIG_ARC_HAS_SWAPE),
			       IS_USED(CONFIG_ARC_HAS_RTSC));
	}

	n += scnprintf(buf + n, len - n, "Extn [CCM]\t: %s",
		       !(cpu->dccm.sz || cpu->iccm.sz) ? "N/A" : "");

	if (cpu->dccm.sz)
		n += scnprintf(buf + n, len - n, "DCCM: @ %x, %d KB ",
			       cpu->dccm.base_addr, TO_KB(cpu->dccm.sz));

	if (cpu->iccm.sz)
		n += scnprintf(buf + n, len - n, "ICCM: @ %x, %d KB",
			       cpu->iccm.base_addr, TO_KB(cpu->iccm.sz));

	n += scnprintf(buf + n, len - n, "\nExtn [FPU]\t: %s",
		       !(cpu->fp.ver || cpu->dpfp.ver) ? "N/A" : "");

	if (cpu->fp.ver)
		n += scnprintf(buf + n, len - n, "SP [v%d] %s",
			       cpu->fp.ver, cpu->fp.fast ? "(fast)" : "");

	if (cpu->dpfp.ver)
		n += scnprintf(buf + n, len - n, "DP [v%d] %s",
			       cpu->dpfp.ver, cpu->dpfp.fast ? "(fast)" : "");

	n += scnprintf(buf + n, len - n, "\n");

	n += scnprintf(buf + n, len - n,
		       "OS ABI [v3]\t: no-legacy-syscalls\n");

	return buf;
}

void arc_chk_ccms(void)
{
#if defined(CONFIG_ARC_HAS_DCCM) || defined(CONFIG_ARC_HAS_ICCM)
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[smp_processor_id()];

#ifdef CONFIG_ARC_HAS_DCCM
	/*
	 * DCCM can be arbit placed in hardware.
	 * Make sure it's placement/sz matches what Linux is built with
	 */
	if ((unsigned int)__arc_dccm_base != cpu->dccm.base_addr)
		panic("Linux built with incorrect DCCM Base address\n");

	if (CONFIG_ARC_DCCM_SZ != cpu->dccm.sz)
		panic("Linux built with incorrect DCCM Size\n");
#endif

#ifdef CONFIG_ARC_HAS_ICCM
	if (CONFIG_ARC_ICCM_SZ != cpu->iccm.sz)
		panic("Linux built with incorrect ICCM Size\n");
#endif
#endif
}

/*
 * Ensure that FP hardware and kernel config match
 * -If hardware contains DPFP, kernel needs to save/restore FPU state
 *  across context switches
 * -If hardware lacks DPFP, but kernel configured to save FPU state then
 *  kernel trying to access non-existant DPFP regs will crash
 *
 * We only check for Dbl precision Floating Point, because only DPFP
 * hardware has dedicated regs which need to be saved/restored on ctx-sw
 * (Single Precision uses core regs), thus kernel is kind of oblivious to it
 */
void arc_chk_fpu(void)
{
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[smp_processor_id()];

	if (cpu->dpfp.ver) {
#ifndef CONFIG_ARC_FPU_SAVE_RESTORE
		pr_warn("DPFP support broken in this kernel...\n");
#endif
	} else {
#ifdef CONFIG_ARC_FPU_SAVE_RESTORE
		panic("H/w lacks DPFP support, apps won't work\n");
#endif
	}
}

/*
 * Initialize and setup the processor core
 * This is called by all the CPUs thus should not do special case stuff
 *    such as only for boot CPU etc
 */

void setup_processor(void)
{
	char str[512];
	int cpu_id = smp_processor_id();

	read_arc_build_cfg_regs();
	arc_init_IRQ();

	printk(arc_cpu_mumbojumbo(cpu_id, str, sizeof(str)));

	arc_mmu_init();
	arc_cache_init();
	arc_chk_ccms();

	printk(arc_extn_mumbojumbo(cpu_id, str, sizeof(str)));

#ifdef CONFIG_SMP
	printk(arc_platform_smp_cpuinfo());
#endif

	arc_chk_fpu();
}

void __init setup_arch(char **cmdline_p)
{
	/* This also populates @boot_command_line from /bootargs */
	machine_desc = setup_machine_fdt(__dtb_start);
	if (!machine_desc)
		panic("Embedded DT invalid\n");

	/* Append any u-boot provided cmdline */
#ifdef CONFIG_CMDLINE_UBOOT
	/* Add a whitespace seperator between the 2 cmdlines */
	strlcat(boot_command_line, " ", COMMAND_LINE_SIZE);
	strlcat(boot_command_line, command_line, COMMAND_LINE_SIZE);
#endif

	/* Save unparsed command line copy for /proc/cmdline */
	*cmdline_p = boot_command_line;

	/* To force early parsing of things like mem=xxx */
	parse_early_param();

	/* Platform/board specific: e.g. early console registration */
	if (machine_desc->init_early)
		machine_desc->init_early();

	setup_processor();

#ifdef CONFIG_SMP
	smp_init_cpus();
#endif

	setup_arch_memory();

	/* copy flat DT out of .init and then unflatten it */
	unflatten_and_copy_device_tree();

	/* Can be issue if someone passes cmd line arg "ro"
	 * But that is unlikely so keeping it as it is
	 */
	root_mountflags &= ~MS_RDONLY;

#if defined(CONFIG_VT) && defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif

	arc_unwind_init();
	arc_unwind_setup();
}

static int __init customize_machine(void)
{
	/* Add platform devices */
	if (machine_desc->init_machine)
		machine_desc->init_machine();

	return 0;
}
arch_initcall(customize_machine);

static int __init init_late_machine(void)
{
	if (machine_desc->init_late)
		machine_desc->init_late();

	return 0;
}
late_initcall(init_late_machine);
/*
 *  Get CPU information for use by the procfs.
 */

#define cpu_to_ptr(c)	((void *)(0xFFFF0000 | (unsigned int)(c)))
#define ptr_to_cpu(p)	(~0xFFFF0000UL & (unsigned int)(p))

static int show_cpuinfo(struct seq_file *m, void *v)
{
	char *str;
	int cpu_id = ptr_to_cpu(v);

	str = (char *)__get_free_page(GFP_TEMPORARY);
	if (!str)
		goto done;

	seq_printf(m, arc_cpu_mumbojumbo(cpu_id, str, PAGE_SIZE));

	seq_printf(m, "Bogo MIPS : \t%lu.%02lu\n",
		   loops_per_jiffy / (500000 / HZ),
		   (loops_per_jiffy / (5000 / HZ)) % 100);

	seq_printf(m, arc_mmu_mumbojumbo(cpu_id, str, PAGE_SIZE));

	seq_printf(m, arc_cache_mumbojumbo(cpu_id, str, PAGE_SIZE));

	seq_printf(m, arc_extn_mumbojumbo(cpu_id, str, PAGE_SIZE));

#ifdef CONFIG_SMP
	seq_printf(m, arc_platform_smp_cpuinfo());
#endif

	free_page((unsigned long)str);
done:
	seq_printf(m, "\n\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	/*
	 * Callback returns cpu-id to iterator for show routine, NULL to stop.
	 * However since NULL is also a valid cpu-id (0), we use a round-about
	 * way to pass it w/o having to kmalloc/free a 2 byte string.
	 * Encode cpu-id as 0xFFcccc, which is decoded by show routine.
	 */
	return *pos < num_possible_cpus() ? cpu_to_ptr(*pos) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo
};

static DEFINE_PER_CPU(struct cpu, cpu_topology);

static int __init topology_init(void)
{
	int cpu;

	for_each_present_cpu(cpu)
	    register_cpu(&per_cpu(cpu_topology, cpu), cpu);

	return 0;
}

subsys_initcall(topology_init);
