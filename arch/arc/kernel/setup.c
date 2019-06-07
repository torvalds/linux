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
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/of_fdt.h>
#include <linux/of.h>
#include <linux/cache.h>
#include <uapi/linux/mount.h>
#include <asm/sections.h>
#include <asm/arcregs.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/unwind.h>
#include <asm/mach_desc.h>
#include <asm/smp.h>

#define FIX_PTR(x)  __asm__ __volatile__(";" : "+r"(x))

unsigned int intr_to_DE_cnt;

/* Part of U-boot ABI: see head.S */
int __initdata uboot_tag;
int __initdata uboot_magic;
char __initdata *uboot_arg;

const struct machine_desc *machine_desc;

struct task_struct *_current_task[NR_CPUS];	/* For stack switching */

struct cpuinfo_arc cpuinfo_arc700[NR_CPUS];

static const struct id_to_str arc_legacy_rel[] = {
	/* ID.ARCVER,	Release */
#ifdef CONFIG_ISA_ARCOMPACT
	{ 0x34, 	"R4.10"},
	{ 0x35, 	"R4.11"},
#else
	{ 0x51, 	"R2.0" },
	{ 0x52, 	"R2.1" },
	{ 0x53,		"R3.0" },
#endif
	{ 0x00,		NULL   }
};

static const struct id_to_str arc_cpu_rel[] = {
	/* UARCH.MAJOR,	Release */
	{  0,		"R3.10a"},
	{  1,		"R3.50a"},
	{  0xFF,	NULL   }
};

static void read_decode_ccm_bcr(struct cpuinfo_arc *cpu)
{
	if (is_isa_arcompact()) {
		struct bcr_iccm_arcompact iccm;
		struct bcr_dccm_arcompact dccm;

		READ_BCR(ARC_REG_ICCM_BUILD, iccm);
		if (iccm.ver) {
			cpu->iccm.sz = 4096 << iccm.sz;	/* 8K to 512K */
			cpu->iccm.base_addr = iccm.base << 16;
		}

		READ_BCR(ARC_REG_DCCM_BUILD, dccm);
		if (dccm.ver) {
			unsigned long base;
			cpu->dccm.sz = 2048 << dccm.sz;	/* 2K to 256K */

			base = read_aux_reg(ARC_REG_DCCM_BASE_BUILD);
			cpu->dccm.base_addr = base & ~0xF;
		}
	} else {
		struct bcr_iccm_arcv2 iccm;
		struct bcr_dccm_arcv2 dccm;
		unsigned long region;

		READ_BCR(ARC_REG_ICCM_BUILD, iccm);
		if (iccm.ver) {
			cpu->iccm.sz = 256 << iccm.sz00;	/* 512B to 16M */
			if (iccm.sz00 == 0xF && iccm.sz01 > 0)
				cpu->iccm.sz <<= iccm.sz01;

			region = read_aux_reg(ARC_REG_AUX_ICCM);
			cpu->iccm.base_addr = region & 0xF0000000;
		}

		READ_BCR(ARC_REG_DCCM_BUILD, dccm);
		if (dccm.ver) {
			cpu->dccm.sz = 256 << dccm.sz0;
			if (dccm.sz0 == 0xF && dccm.sz1 > 0)
				cpu->dccm.sz <<= dccm.sz1;

			region = read_aux_reg(ARC_REG_AUX_DCCM);
			cpu->dccm.base_addr = region & 0xF0000000;
		}
	}
}

static void decode_arc_core(struct cpuinfo_arc *cpu)
{
	struct bcr_uarch_build_arcv2 uarch;
	const struct id_to_str *tbl;

	/*
	 * Up until (including) the first core4 release (0x54) things were
	 * simple: AUX IDENTITY.ARCVER was sufficient to identify arc family
	 * and release: 0x50 to 0x53 was HS38, 0x54 was HS48 (dual issue)
	 */

	if (cpu->core.family < 0x54) { /* includes arc700 */

		for (tbl = &arc_legacy_rel[0]; tbl->id != 0; tbl++) {
			if (cpu->core.family == tbl->id) {
				cpu->release = tbl->str;
				break;
			}
		}

		if (is_isa_arcompact())
			cpu->name = "ARC700";
		else if (tbl->str)
			cpu->name = "HS38";
		else
			cpu->name = cpu->release = "Unknown";

		return;
	}

	/*
	 * However the subsequent HS release (same 0x54) allow HS38 or HS48
	 * configurations and encode this info in a different BCR.
	 * The BCR was introduced in 0x54 so can't be read unconditionally.
	 */

	READ_BCR(ARC_REG_MICRO_ARCH_BCR, uarch);

	if (uarch.prod == 4) {
		cpu->name = "HS48";
		cpu->extn.dual = 1;

	} else {
		cpu->name = "HS38";
	}

	for (tbl = &arc_cpu_rel[0]; tbl->id != 0xFF; tbl++) {
		if (uarch.maj == tbl->id) {
			cpu->release = tbl->str;
			break;
		}
	}
}

static void read_arc_build_cfg_regs(void)
{
	struct bcr_timer timer;
	struct bcr_generic bcr;
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[smp_processor_id()];
	struct bcr_isa_arcv2 isa;
	struct bcr_actionpoint ap;

	FIX_PTR(cpu);

	READ_BCR(AUX_IDENTITY, cpu->core);
	decode_arc_core(cpu);

	READ_BCR(ARC_REG_TIMERS_BCR, timer);
	cpu->extn.timer0 = timer.t0;
	cpu->extn.timer1 = timer.t1;
	cpu->extn.rtc = timer.rtc;

	cpu->vec_base = read_aux_reg(AUX_INTR_VEC_BASE);

	READ_BCR(ARC_REG_MUL_BCR, cpu->extn_mpy);

	/* Read CCM BCRs for boot reporting even if not enabled in Kconfig */
	read_decode_ccm_bcr(cpu);

	read_decode_mmu_bcr();
	read_decode_cache_bcr();

	if (is_isa_arcompact()) {
		struct bcr_fp_arcompact sp, dp;
		struct bcr_bpu_arcompact bpu;

		READ_BCR(ARC_REG_FP_BCR, sp);
		READ_BCR(ARC_REG_DPFP_BCR, dp);
		cpu->extn.fpu_sp = sp.ver ? 1 : 0;
		cpu->extn.fpu_dp = dp.ver ? 1 : 0;

		READ_BCR(ARC_REG_BPU_BCR, bpu);
		cpu->bpu.ver = bpu.ver;
		cpu->bpu.full = bpu.fam ? 1 : 0;
		if (bpu.ent) {
			cpu->bpu.num_cache = 256 << (bpu.ent - 1);
			cpu->bpu.num_pred = 256 << (bpu.ent - 1);
		}
	} else {
		struct bcr_fp_arcv2 spdp;
		struct bcr_bpu_arcv2 bpu;

		READ_BCR(ARC_REG_FP_V2_BCR, spdp);
		cpu->extn.fpu_sp = spdp.sp ? 1 : 0;
		cpu->extn.fpu_dp = spdp.dp ? 1 : 0;

		READ_BCR(ARC_REG_BPU_BCR, bpu);
		cpu->bpu.ver = bpu.ver;
		cpu->bpu.full = bpu.ft;
		cpu->bpu.num_cache = 256 << bpu.bce;
		cpu->bpu.num_pred = 2048 << bpu.pte;
		cpu->bpu.ret_stk = 4 << bpu.rse;

		/* if dual issue hardware, is it enabled ? */
		if (cpu->extn.dual) {
			unsigned int exec_ctrl;

			READ_BCR(AUX_EXEC_CTRL, exec_ctrl);
			cpu->extn.dual_enb = !(exec_ctrl & 1);
		}
	}

	READ_BCR(ARC_REG_AP_BCR, ap);
	if (ap.ver) {
		cpu->extn.ap_num = 2 << ap.num;
		cpu->extn.ap_full = !ap.min;
	}

	READ_BCR(ARC_REG_SMART_BCR, bcr);
	cpu->extn.smart = bcr.ver ? 1 : 0;

	READ_BCR(ARC_REG_RTT_BCR, bcr);
	cpu->extn.rtt = bcr.ver ? 1 : 0;

	READ_BCR(ARC_REG_ISA_CFG_BCR, isa);

	/* some hacks for lack of feature BCR info in old ARC700 cores */
	if (is_isa_arcompact()) {
		if (!isa.ver)	/* ISA BCR absent, use Kconfig info */
			cpu->isa.atomic = IS_ENABLED(CONFIG_ARC_HAS_LLSC);
		else {
			/* ARC700_BUILD only has 2 bits of isa info */
			struct bcr_generic bcr = *(struct bcr_generic *)&isa;
			cpu->isa.atomic = bcr.info & 1;
		}

		cpu->isa.be = IS_ENABLED(CONFIG_CPU_BIG_ENDIAN);

		 /* there's no direct way to distinguish 750 vs. 770 */
		if (unlikely(cpu->core.family < 0x34 || cpu->mmu.ver < 3))
			cpu->name = "ARC750";
	} else {
		cpu->isa = isa;
	}
}

static char *arc_cpu_mumbojumbo(int cpu_id, char *buf, int len)
{
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[cpu_id];
	struct bcr_identity *core = &cpu->core;
	char mpy_opt[16];
	int n = 0;

	FIX_PTR(cpu);

	n += scnprintf(buf + n, len - n,
		       "\nIDENTITY\t: ARCVER [%#02x] ARCNUM [%#02x] CHIPID [%#4x]\n",
		       core->family, core->cpu_id, core->chip_id);

	n += scnprintf(buf + n, len - n, "processor [%d]\t: %s %s (%s ISA) %s%s%s\n",
		       cpu_id, cpu->name, cpu->release,
		       is_isa_arcompact() ? "ARCompact" : "ARCv2",
		       IS_AVAIL1(cpu->isa.be, "[Big-Endian]"),
		       IS_AVAIL3(cpu->extn.dual, cpu->extn.dual_enb, " Dual-Issue "));

	n += scnprintf(buf + n, len - n, "Timers\t\t: %s%s%s%s%s%s\nISA Extn\t: ",
		       IS_AVAIL1(cpu->extn.timer0, "Timer0 "),
		       IS_AVAIL1(cpu->extn.timer1, "Timer1 "),
		       IS_AVAIL2(cpu->extn.rtc, "RTC [UP 64-bit] ", CONFIG_ARC_TIMERS_64BIT),
		       IS_AVAIL2(cpu->extn.gfrc, "GFRC [SMP 64-bit] ", CONFIG_ARC_TIMERS_64BIT));

	if (cpu->extn_mpy.ver) {
		if (is_isa_arcompact()) {
			scnprintf(mpy_opt, 16, "mpy");
		} else {

			int opt = 2;	/* stock MPY/MPYH */

			if (cpu->extn_mpy.dsp)	/* OPT 7-9 */
				opt = cpu->extn_mpy.dsp + 6;

			scnprintf(mpy_opt, 16, "mpy[opt %d] ", opt);
		}
	}

	n += scnprintf(buf + n, len - n, "%s%s%s%s%s%s%s%s\n",
		       IS_AVAIL2(cpu->isa.atomic, "atomic ", CONFIG_ARC_HAS_LLSC),
		       IS_AVAIL2(cpu->isa.ldd, "ll64 ", CONFIG_ARC_HAS_LL64),
		       IS_AVAIL2(cpu->isa.unalign, "unalign ", CONFIG_ARC_USE_UNALIGNED_MEM_ACCESS),
		       IS_AVAIL1(cpu->extn_mpy.ver, mpy_opt),
		       IS_AVAIL1(cpu->isa.div_rem, "div_rem "));

	if (cpu->bpu.ver) {
		n += scnprintf(buf + n, len - n,
			      "BPU\t\t: %s%s match, cache:%d, Predict Table:%d Return stk: %d",
			      IS_AVAIL1(cpu->bpu.full, "full"),
			      IS_AVAIL1(!cpu->bpu.full, "partial"),
			      cpu->bpu.num_cache, cpu->bpu.num_pred, cpu->bpu.ret_stk);

		if (is_isa_arcv2()) {
			struct bcr_lpb lpb;

			READ_BCR(ARC_REG_LPB_BUILD, lpb);
			if (lpb.ver) {
				unsigned int ctl;
				ctl = read_aux_reg(ARC_REG_LPB_CTRL);

				n += scnprintf(buf + n, len - n, " Loop Buffer:%d %s",
					       lpb.entries,
					       IS_DISABLED_RUN(!ctl));
			}
		}
		n += scnprintf(buf + n, len - n, "\n");
	}

	return buf;
}

static char *arc_extn_mumbojumbo(int cpu_id, char *buf, int len)
{
	int n = 0;
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[cpu_id];

	FIX_PTR(cpu);

	n += scnprintf(buf + n, len - n, "Vector Table\t: %#x\n", cpu->vec_base);

	if (cpu->extn.fpu_sp || cpu->extn.fpu_dp)
		n += scnprintf(buf + n, len - n, "FPU\t\t: %s%s\n",
			       IS_AVAIL1(cpu->extn.fpu_sp, "SP "),
			       IS_AVAIL1(cpu->extn.fpu_dp, "DP "));

	if (cpu->extn.ap_num | cpu->extn.smart | cpu->extn.rtt) {
		n += scnprintf(buf + n, len - n, "DEBUG\t\t: %s%s",
			       IS_AVAIL1(cpu->extn.smart, "smaRT "),
			       IS_AVAIL1(cpu->extn.rtt, "RTT "));
		if (cpu->extn.ap_num) {
			n += scnprintf(buf + n, len - n, "ActionPoint %d/%s",
				       cpu->extn.ap_num,
				       cpu->extn.ap_full ? "full":"min");
		}
		n += scnprintf(buf + n, len - n, "\n");
	}

	if (cpu->dccm.sz || cpu->iccm.sz)
		n += scnprintf(buf + n, len - n, "Extn [CCM]\t: DCCM @ %x, %d KB / ICCM: @ %x, %d KB\n",
			       cpu->dccm.base_addr, TO_KB(cpu->dccm.sz),
			       cpu->iccm.base_addr, TO_KB(cpu->iccm.sz));

	if (is_isa_arcv2()) {

		/* Error Protection: ECC/Parity */
		struct bcr_erp erp;
		READ_BCR(ARC_REG_ERP_BUILD, erp);

		if (erp.ver) {
			struct  ctl_erp ctl;
			READ_BCR(ARC_REG_ERP_CTRL, ctl);

			/* inverted bits: 0 means enabled */
			n += scnprintf(buf + n, len - n, "Extn [ECC]\t: %s%s%s%s%s%s\n",
				IS_AVAIL3(erp.ic,  !ctl.dpi, "IC "),
				IS_AVAIL3(erp.dc,  !ctl.dpd, "DC "),
				IS_AVAIL3(erp.mmu, !ctl.mpd, "MMU "));
		}
	}

	return buf;
}

static void arc_chk_core_config(void)
{
	struct cpuinfo_arc *cpu = &cpuinfo_arc700[smp_processor_id()];
	int saved = 0, present = 0;
	char *opt_nm = NULL;

	if (!cpu->extn.timer0)
		panic("Timer0 is not present!\n");

	if (!cpu->extn.timer1)
		panic("Timer1 is not present!\n");

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

	/*
	 * FP hardware/software config sanity
	 * -If hardware present, kernel needs to save/restore FPU state
	 * -If not, it will crash trying to save/restore the non-existant regs
	 */

	if (is_isa_arcompact()) {
		opt_nm = "CONFIG_ARC_FPU_SAVE_RESTORE";
		saved = IS_ENABLED(CONFIG_ARC_FPU_SAVE_RESTORE);

		/* only DPDP checked since SP has no arch visible regs */
		present = cpu->extn.fpu_dp;
	} else {
		opt_nm = "CONFIG_ARC_HAS_ACCL_REGS";
		saved = IS_ENABLED(CONFIG_ARC_HAS_ACCL_REGS);

		/* Accumulator Low:High pair (r58:59) present if DSP MPY or FPU */
		present = cpu->extn_mpy.dsp | cpu->extn.fpu_sp | cpu->extn.fpu_dp;
	}

	if (present && !saved)
		pr_warn("Enable %s for working apps\n", opt_nm);
	else if (!present && saved)
		panic("Disable %s, hardware NOT present\n", opt_nm);
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

	pr_info("%s", arc_cpu_mumbojumbo(cpu_id, str, sizeof(str)));

	arc_mmu_init();
	arc_cache_init();

	pr_info("%s", arc_extn_mumbojumbo(cpu_id, str, sizeof(str)));
	pr_info("%s", arc_platform_smp_cpuinfo());

	arc_chk_core_config();
}

static inline bool uboot_arg_invalid(unsigned long addr)
{
	/*
	 * Check that it is a untranslated address (although MMU is not enabled
	 * yet, it being a high address ensures this is not by fluke)
	 */
	if (addr < PAGE_OFFSET)
		return true;

	/* Check that address doesn't clobber resident kernel image */
	return addr >= (unsigned long)_stext && addr <= (unsigned long)_end;
}

#define IGNORE_ARGS		"Ignore U-boot args: "

/* uboot_tag values for U-boot - kernel ABI revision 0; see head.S */
#define UBOOT_TAG_NONE		0
#define UBOOT_TAG_CMDLINE	1
#define UBOOT_TAG_DTB		2
/* We always pass 0 as magic from U-boot */
#define UBOOT_MAGIC_VALUE	0

void __init handle_uboot_args(void)
{
	bool use_embedded_dtb = true;
	bool append_cmdline = false;

	/* check that we know this tag */
	if (uboot_tag != UBOOT_TAG_NONE &&
	    uboot_tag != UBOOT_TAG_CMDLINE &&
	    uboot_tag != UBOOT_TAG_DTB) {
		pr_warn(IGNORE_ARGS "invalid uboot tag: '%08x'\n", uboot_tag);
		goto ignore_uboot_args;
	}

	if (uboot_magic != UBOOT_MAGIC_VALUE) {
		pr_warn(IGNORE_ARGS "non zero uboot magic\n");
		goto ignore_uboot_args;
	}

	if (uboot_tag != UBOOT_TAG_NONE &&
            uboot_arg_invalid((unsigned long)uboot_arg)) {
		pr_warn(IGNORE_ARGS "invalid uboot arg: '%px'\n", uboot_arg);
		goto ignore_uboot_args;
	}

	/* see if U-boot passed an external Device Tree blob */
	if (uboot_tag == UBOOT_TAG_DTB) {
		machine_desc = setup_machine_fdt((void *)uboot_arg);

		/* external Device Tree blob is invalid - use embedded one */
		use_embedded_dtb = !machine_desc;
	}

	if (uboot_tag == UBOOT_TAG_CMDLINE)
		append_cmdline = true;

ignore_uboot_args:

	if (use_embedded_dtb) {
		machine_desc = setup_machine_fdt(__dtb_start);
		if (!machine_desc)
			panic("Embedded DT invalid\n");
	}

	/*
	 * NOTE: @boot_command_line is populated by setup_machine_fdt() so this
	 * append processing can only happen after.
	 */
	if (append_cmdline) {
		/* Ensure a whitespace between the 2 cmdlines */
		strlcat(boot_command_line, " ", COMMAND_LINE_SIZE);
		strlcat(boot_command_line, uboot_arg, COMMAND_LINE_SIZE);
	}
}

void __init setup_arch(char **cmdline_p)
{
	handle_uboot_args();

	/* Save unparsed command line copy for /proc/cmdline */
	*cmdline_p = boot_command_line;

	/* To force early parsing of things like mem=xxx */
	parse_early_param();

	/* Platform/board specific: e.g. early console registration */
	if (machine_desc->init_early)
		machine_desc->init_early();

	smp_init_cpus();

	setup_processor();
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
}

/*
 * Called from start_kernel() - boot CPU only
 */
void __init time_init(void)
{
	of_clk_init(NULL);
	timer_probe();
}

static int __init customize_machine(void)
{
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
	struct device *cpu_dev = get_cpu_device(cpu_id);
	struct clk *cpu_clk;
	unsigned long freq = 0;

	if (!cpu_online(cpu_id)) {
		seq_printf(m, "processor [%d]\t: Offline\n", cpu_id);
		goto done;
	}

	str = (char *)__get_free_page(GFP_KERNEL);
	if (!str)
		goto done;

	seq_printf(m, arc_cpu_mumbojumbo(cpu_id, str, PAGE_SIZE));

	cpu_clk = clk_get(cpu_dev, NULL);
	if (IS_ERR(cpu_clk)) {
		seq_printf(m, "CPU speed \t: Cannot get clock for processor [%d]\n",
			   cpu_id);
	} else {
		freq = clk_get_rate(cpu_clk);
	}
	if (freq)
		seq_printf(m, "CPU speed\t: %lu.%02lu Mhz\n",
			   freq / 1000000, (freq / 10000) % 100);

	seq_printf(m, "Bogo MIPS\t: %lu.%02lu\n",
		   loops_per_jiffy / (500000 / HZ),
		   (loops_per_jiffy / (5000 / HZ)) % 100);

	seq_printf(m, arc_mmu_mumbojumbo(cpu_id, str, PAGE_SIZE));
	seq_printf(m, arc_cache_mumbojumbo(cpu_id, str, PAGE_SIZE));
	seq_printf(m, arc_extn_mumbojumbo(cpu_id, str, PAGE_SIZE));
	seq_printf(m, arc_platform_smp_cpuinfo());

	free_page((unsigned long)str);
done:
	seq_printf(m, "\n");

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
	return *pos < nr_cpu_ids ? cpu_to_ptr(*pos) : NULL;
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
