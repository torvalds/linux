// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/root_dev.h>
#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/cpu.h>
#include <linux/of_clk.h>
#include <linux/of_fdt.h>
#include <linux/of.h>
#include <linux/cache.h>
#include <uapi/linux/mount.h>
#include <asm/sections.h>
#include <asm/arcregs.h>
#include <asm/asserts.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/unwind.h>
#include <asm/mach_desc.h>
#include <asm/smp.h>
#include <asm/dsp-impl.h>
#include <soc/arc/mcip.h>

#define FIX_PTR(x)  __asm__ __volatile__(";" : "+r"(x))

unsigned int intr_to_DE_cnt;

/* Part of U-boot ABI: see head.S */
int __initdata uboot_tag;
int __initdata uboot_magic;
char __initdata *uboot_arg;

const struct machine_desc *machine_desc;

struct task_struct *_current_task[NR_CPUS];	/* For stack switching */

struct cpuinfo_arc {
	int arcver;
	unsigned int t0:1, t1:1;
	struct {
		unsigned long base;
		unsigned int sz;
	} iccm, dccm;
};

#ifdef CONFIG_ISA_ARCV2

static const struct id_to_str arc_hs_rel[] = {
	/* ID.ARCVER,	Release */
	{ 0x51, 	"R2.0" },
	{ 0x52, 	"R2.1" },
	{ 0x53,		"R3.0" },
};

static const struct id_to_str arc_hs_ver54_rel[] = {
	/* UARCH.MAJOR,	Release */
	{  0,		"R3.10a"},
	{  1,		"R3.50a"},
	{  2,		"R3.60a"},
	{  3,		"R4.00a"},
	{  0xFF,	NULL   }
};
#endif

static int
arcompact_mumbojumbo(int c, struct cpuinfo_arc *info, char *buf, int len)
{
	int n = 0;
#ifdef CONFIG_ISA_ARCOMPACT
	char *cpu_nm, *isa_nm = "ARCompact";
	struct bcr_fp_arcompact fpu_sp, fpu_dp;
	int atomic = 0, be, present;
	int bpu_full, bpu_cache, bpu_pred;
	struct bcr_bpu_arcompact bpu;
	struct bcr_iccm_arcompact iccm;
	struct bcr_dccm_arcompact dccm;
	struct bcr_generic isa;

	READ_BCR(ARC_REG_ISA_CFG_BCR, isa);

	if (!isa.ver)	/* ISA BCR absent, use Kconfig info */
		atomic = IS_ENABLED(CONFIG_ARC_HAS_LLSC);
	else {
		/* ARC700_BUILD only has 2 bits of isa info */
		atomic = isa.info & 1;
	}

	be = IS_ENABLED(CONFIG_CPU_BIG_ENDIAN);

	if (info->arcver < 0x34)
		cpu_nm = "ARC750";
	else
		cpu_nm = "ARC770";

	n += scnprintf(buf + n, len - n, "processor [%d]\t: %s (%s ISA) %s%s%s\n",
		       c, cpu_nm, isa_nm,
		       IS_AVAIL2(atomic, "atomic ", CONFIG_ARC_HAS_LLSC),
		       IS_AVAIL1(be, "[Big-Endian]"));

	READ_BCR(ARC_REG_FP_BCR, fpu_sp);
	READ_BCR(ARC_REG_DPFP_BCR, fpu_dp);

	if (fpu_sp.ver | fpu_dp.ver)
		n += scnprintf(buf + n, len - n, "FPU\t\t: %s%s\n",
			       IS_AVAIL1(fpu_sp.ver, "SP "),
			       IS_AVAIL1(fpu_dp.ver, "DP "));

	READ_BCR(ARC_REG_BPU_BCR, bpu);
	bpu_full = bpu.fam ? 1 : 0;
	bpu_cache = 256 << (bpu.ent - 1);
	bpu_pred = 256 << (bpu.ent - 1);

	n += scnprintf(buf + n, len - n,
			"BPU\t\t: %s%s match, cache:%d, Predict Table:%d\n",
			IS_AVAIL1(bpu_full, "full"),
			IS_AVAIL1(!bpu_full, "partial"),
			bpu_cache, bpu_pred);

	READ_BCR(ARC_REG_ICCM_BUILD, iccm);
	if (iccm.ver) {
		info->iccm.sz = 4096 << iccm.sz;	/* 8K to 512K */
		info->iccm.base = iccm.base << 16;
	}

	READ_BCR(ARC_REG_DCCM_BUILD, dccm);
	if (dccm.ver) {
		unsigned long base;
		info->dccm.sz = 2048 << dccm.sz;	/* 2K to 256K */

		base = read_aux_reg(ARC_REG_DCCM_BASE_BUILD);
		info->dccm.base = base & ~0xF;
	}

	/* ARCompact ISA specific sanity checks */
	present = fpu_dp.ver;	/* SP has no arch visible regs */
	CHK_OPT_STRICT(CONFIG_ARC_FPU_SAVE_RESTORE, present);
#endif
	return n;

}

static int arcv2_mumbojumbo(int c, struct cpuinfo_arc *info, char *buf, int len)
{
	int n = 0;
#ifdef CONFIG_ISA_ARCV2
	const char *release = "", *cpu_nm = "HS38", *isa_nm = "ARCv2";
	int dual_issue = 0, dual_enb = 0, mpy_opt, present;
	int bpu_full, bpu_cache, bpu_pred, bpu_ret_stk;
	char mpy_nm[16], lpb_nm[32];
	struct bcr_isa_arcv2 isa;
	struct bcr_mpy mpy;
	struct bcr_fp_arcv2 fpu;
	struct bcr_bpu_arcv2 bpu;
	struct bcr_lpb lpb;
	struct bcr_iccm_arcv2 iccm;
	struct bcr_dccm_arcv2 dccm;
	struct bcr_erp erp;

	/*
	 * Initial HS cores bumped AUX IDENTITY.ARCVER for each release until
	 * ARCVER 0x54 which introduced AUX MICRO_ARCH_BUILD and subsequent
	 * releases only update it.
	 */

	if (info->arcver > 0x50 && info->arcver <= 0x53) {
		release = arc_hs_rel[info->arcver - 0x51].str;
	} else {
		const struct id_to_str *tbl;
		struct bcr_uarch_build uarch;

		READ_BCR(ARC_REG_MICRO_ARCH_BCR, uarch);

		for (tbl = &arc_hs_ver54_rel[0]; tbl->id != 0xFF; tbl++) {
			if (uarch.maj == tbl->id) {
				release = tbl->str;
				break;
			}
		}
		if (uarch.prod == 4) {
			unsigned int exec_ctrl;

			cpu_nm = "HS48";
			dual_issue = 1;
			/* if dual issue hardware, is it enabled ? */
			READ_BCR(AUX_EXEC_CTRL, exec_ctrl);
			dual_enb = !(exec_ctrl & 1);
		}
	}

	READ_BCR(ARC_REG_ISA_CFG_BCR, isa);

	n += scnprintf(buf + n, len - n, "processor [%d]\t: %s %s (%s ISA) %s%s%s\n",
		       c, cpu_nm, release, isa_nm,
		       IS_AVAIL1(isa.be, "[Big-Endian]"),
		       IS_AVAIL3(dual_issue, dual_enb, " Dual-Issue "));

	READ_BCR(ARC_REG_MPY_BCR, mpy);
	mpy_opt = 2;	/* stock MPY/MPYH */
	if (mpy.dsp)	/* OPT 7-9 */
		mpy_opt = mpy.dsp + 6;

	scnprintf(mpy_nm, 16, "mpy[opt %d] ", mpy_opt);

	READ_BCR(ARC_REG_FP_V2_BCR, fpu);

	n += scnprintf(buf + n, len - n, "ISA Extn\t: %s%s%s%s%s%s%s%s%s%s%s\n",
		       IS_AVAIL2(isa.atomic, "atomic ", CONFIG_ARC_HAS_LLSC),
		       IS_AVAIL2(isa.ldd, "ll64 ", CONFIG_ARC_HAS_LL64),
		       IS_AVAIL2(isa.unalign, "unalign ", CONFIG_ARC_USE_UNALIGNED_MEM_ACCESS),
		       IS_AVAIL1(mpy.ver, mpy_nm),
		       IS_AVAIL1(isa.div_rem, "div_rem "),
		       IS_AVAIL1((fpu.sp | fpu.dp), "  FPU:"),
		       IS_AVAIL1(fpu.sp, " sp"),
		       IS_AVAIL1(fpu.dp, " dp"));

	READ_BCR(ARC_REG_BPU_BCR, bpu);
	bpu_full = bpu.ft;
	bpu_cache = 256 << bpu.bce;
	bpu_pred = 2048 << bpu.pte;
	bpu_ret_stk = 4 << bpu.rse;

	READ_BCR(ARC_REG_LPB_BUILD, lpb);
	if (lpb.ver) {
		unsigned int ctl;
		ctl = read_aux_reg(ARC_REG_LPB_CTRL);

		scnprintf(lpb_nm, sizeof(lpb_nm), " Loop Buffer:%d %s",
			  lpb.entries, IS_DISABLED_RUN(!ctl));
	}

	n += scnprintf(buf + n, len - n,
			"BPU\t\t: %s%s match, cache:%d, Predict Table:%d Return stk: %d%s\n",
			IS_AVAIL1(bpu_full, "full"),
			IS_AVAIL1(!bpu_full, "partial"),
			bpu_cache, bpu_pred, bpu_ret_stk,
			lpb_nm);

	READ_BCR(ARC_REG_ICCM_BUILD, iccm);
	if (iccm.ver) {
		unsigned long base;
		info->iccm.sz = 256 << iccm.sz00;	/* 512B to 16M */
		if (iccm.sz00 == 0xF && iccm.sz01 > 0)
			info->iccm.sz <<= iccm.sz01;
		base = read_aux_reg(ARC_REG_AUX_ICCM);
		info->iccm.base = base & 0xF0000000;
	}

	READ_BCR(ARC_REG_DCCM_BUILD, dccm);
	if (dccm.ver) {
		unsigned long base;
		info->dccm.sz = 256 << dccm.sz0;
		if (dccm.sz0 == 0xF && dccm.sz1 > 0)
			info->dccm.sz <<= dccm.sz1;
		base = read_aux_reg(ARC_REG_AUX_DCCM);
		info->dccm.base = base & 0xF0000000;
	}

	/* Error Protection: ECC/Parity */
	READ_BCR(ARC_REG_ERP_BUILD, erp);
	if (erp.ver) {
		struct ctl_erp ctl;
		READ_BCR(ARC_REG_ERP_CTRL, ctl);
		/* inverted bits: 0 means enabled */
		n += scnprintf(buf + n, len - n, "Extn [ECC]\t: %s%s%s%s%s%s\n",
				IS_AVAIL3(erp.ic,  !ctl.dpi, "IC "),
				IS_AVAIL3(erp.dc,  !ctl.dpd, "DC "),
				IS_AVAIL3(erp.mmu, !ctl.mpd, "MMU "));
	}

	/* ARCv2 ISA specific sanity checks */
	present = fpu.sp | fpu.dp | mpy.dsp;	/* DSP and/or FPU */
	CHK_OPT_STRICT(CONFIG_ARC_HAS_ACCL_REGS, present);

	dsp_config_check();
#endif
	return n;
}

static char *arc_cpu_mumbojumbo(int c, struct cpuinfo_arc *info, char *buf, int len)
{
	struct bcr_identity ident;
	struct bcr_timer timer;
	struct bcr_generic bcr;
	struct mcip_bcr mp;
	struct bcr_actionpoint ap;
	unsigned long vec_base;
	int ap_num, ap_full, smart, rtt, n;

	memset(info, 0, sizeof(struct cpuinfo_arc));

	READ_BCR(AUX_IDENTITY, ident);
	info->arcver = ident.family;

	n = scnprintf(buf, len,
		       "\nIDENTITY\t: ARCVER [%#02x] ARCNUM [%#02x] CHIPID [%#4x]\n",
		       ident.family, ident.cpu_id, ident.chip_id);

	if (is_isa_arcompact()) {
		n += arcompact_mumbojumbo(c, info, buf + n, len - n);
	} else if (is_isa_arcv2()){
		n += arcv2_mumbojumbo(c, info, buf + n, len - n);
	}

	n += arc_mmu_mumbojumbo(c, buf + n, len - n);
	n += arc_cache_mumbojumbo(c, buf + n, len - n);

	READ_BCR(ARC_REG_TIMERS_BCR, timer);
	info->t0 = timer.t0;
	info->t1 = timer.t1;

	READ_BCR(ARC_REG_MCIP_BCR, mp);
	vec_base = read_aux_reg(AUX_INTR_VEC_BASE);

	n += scnprintf(buf + n, len - n,
		       "Timers\t\t: %s%s%s%s%s%s\nVector Table\t: %#lx\n",
		       IS_AVAIL1(timer.t0, "Timer0 "),
		       IS_AVAIL1(timer.t1, "Timer1 "),
		       IS_AVAIL2(timer.rtc, "RTC [UP 64-bit] ", CONFIG_ARC_TIMERS_64BIT),
		       IS_AVAIL2(mp.gfrc, "GFRC [SMP 64-bit] ", CONFIG_ARC_TIMERS_64BIT),
		       vec_base);

	READ_BCR(ARC_REG_AP_BCR, ap);
	if (ap.ver) {
		ap_num = 2 << ap.num;
		ap_full = !ap.min;
	}

	READ_BCR(ARC_REG_SMART_BCR, bcr);
	smart = bcr.ver ? 1 : 0;

	READ_BCR(ARC_REG_RTT_BCR, bcr);
	rtt = bcr.ver ? 1 : 0;

	if (ap.ver | smart | rtt) {
		n += scnprintf(buf + n, len - n, "DEBUG\t\t: %s%s",
			       IS_AVAIL1(smart, "smaRT "),
			       IS_AVAIL1(rtt, "RTT "));
		if (ap.ver) {
			n += scnprintf(buf + n, len - n, "ActionPoint %d/%s",
				       ap_num,
				       ap_full ? "full":"min");
		}
		n += scnprintf(buf + n, len - n, "\n");
	}

	if (info->dccm.sz || info->iccm.sz)
		n += scnprintf(buf + n, len - n,
			       "Extn [CCM]\t: DCCM @ %lx, %d KB / ICCM: @ %lx, %d KB\n",
			       info->dccm.base, TO_KB(info->dccm.sz),
			       info->iccm.base, TO_KB(info->iccm.sz));

	return buf;
}

void chk_opt_strict(char *opt_name, bool hw_exists, bool opt_ena)
{
	if (hw_exists && !opt_ena)
		pr_warn(" ! Enable %s for working apps\n", opt_name);
	else if (!hw_exists && opt_ena)
		panic("Disable %s, hardware NOT present\n", opt_name);
}

void chk_opt_weak(char *opt_name, bool hw_exists, bool opt_ena)
{
	if (!hw_exists && opt_ena)
		panic("Disable %s, hardware NOT present\n", opt_name);
}

/*
 * ISA agnostic sanity checks
 */
static void arc_chk_core_config(struct cpuinfo_arc *info)
{
	if (!info->t0)
		panic("Timer0 is not present!\n");

	if (!info->t1)
		panic("Timer1 is not present!\n");

#ifdef CONFIG_ARC_HAS_DCCM
	/*
	 * DCCM can be arbit placed in hardware.
	 * Make sure it's placement/sz matches what Linux is built with
	 */
	if ((unsigned int)__arc_dccm_base != info->dccm.base)
		panic("Linux built with incorrect DCCM Base address\n");

	if (CONFIG_ARC_DCCM_SZ * SZ_1K != info->dccm.sz)
		panic("Linux built with incorrect DCCM Size\n");
#endif

#ifdef CONFIG_ARC_HAS_ICCM
	if (CONFIG_ARC_ICCM_SZ * SZ_1K != info->iccm.sz)
		panic("Linux built with incorrect ICCM Size\n");
#endif
}

/*
 * Initialize and setup the processor core
 * This is called by all the CPUs thus should not do special case stuff
 *    such as only for boot CPU etc
 */

void setup_processor(void)
{
	struct cpuinfo_arc info;
	int c = smp_processor_id();
	char str[512];

	pr_info("%s", arc_cpu_mumbojumbo(c, &info, str, sizeof(str)));
	pr_info("%s", arc_platform_smp_cpuinfo());

	arc_chk_core_config(&info);

	arc_init_IRQ();
	arc_mmu_init();
	arc_cache_init();

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
	struct cpuinfo_arc info;
	struct clk *cpu_clk;
	unsigned long freq = 0;

	if (!cpu_online(cpu_id)) {
		seq_printf(m, "processor [%d]\t: Offline\n", cpu_id);
		goto done;
	}

	str = (char *)__get_free_page(GFP_KERNEL);
	if (!str)
		goto done;

	seq_printf(m, arc_cpu_mumbojumbo(cpu_id, &info, str, PAGE_SIZE));

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
