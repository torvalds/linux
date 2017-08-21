#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <asm/debug.h>
#include <asm/fpu_emulator.h>
#include <asm/local.h>

DEFINE_PER_CPU(struct mips_fpu_emulator_stats, fpuemustats);

static int fpuemu_stat_get(void *data, u64 *val)
{
	int cpu;
	unsigned long sum = 0;

	for_each_online_cpu(cpu) {
		struct mips_fpu_emulator_stats *ps;
		local_t *pv;

		ps = &per_cpu(fpuemustats, cpu);
		pv = (void *)ps + (unsigned long)data;
		sum += local_read(pv);
	}
	*val = sum;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_fpuemu_stat, fpuemu_stat_get, NULL, "%llu\n");

/*
 * Used to obtain names for a debugfs instruction counter, given field name
 * in fpuemustats structure. For example, for input "cmp_sueq_d", the output
 * would be "cmp.sueq.d". This is needed since dots are not allowed to be
 * used in structure field names, and are, on the other hand, desired to be
 * used in debugfs item names to be clearly associated to corresponding
 * MIPS FPU instructions.
 */
static void adjust_instruction_counter_name(char *out_name, char *in_name)
{
	int i = 0;

	strcpy(out_name, in_name);
	while (in_name[i] != '\0') {
		if (out_name[i] == '_')
			out_name[i] = '.';
		i++;
	}
}

static int fpuemustats_clear_show(struct seq_file *s, void *unused)
{
	__this_cpu_write((fpuemustats).emulated, 0);
	__this_cpu_write((fpuemustats).loads, 0);
	__this_cpu_write((fpuemustats).stores, 0);
	__this_cpu_write((fpuemustats).branches, 0);
	__this_cpu_write((fpuemustats).cp1ops, 0);
	__this_cpu_write((fpuemustats).cp1xops, 0);
	__this_cpu_write((fpuemustats).errors, 0);
	__this_cpu_write((fpuemustats).ieee754_inexact, 0);
	__this_cpu_write((fpuemustats).ieee754_underflow, 0);
	__this_cpu_write((fpuemustats).ieee754_overflow, 0);
	__this_cpu_write((fpuemustats).ieee754_zerodiv, 0);
	__this_cpu_write((fpuemustats).ieee754_invalidop, 0);
	__this_cpu_write((fpuemustats).ds_emul, 0);

	__this_cpu_write((fpuemustats).abs_s, 0);
	__this_cpu_write((fpuemustats).abs_d, 0);
	__this_cpu_write((fpuemustats).add_s, 0);
	__this_cpu_write((fpuemustats).add_d, 0);
	__this_cpu_write((fpuemustats).bc1eqz, 0);
	__this_cpu_write((fpuemustats).bc1nez, 0);
	__this_cpu_write((fpuemustats).ceil_w_s, 0);
	__this_cpu_write((fpuemustats).ceil_w_d, 0);
	__this_cpu_write((fpuemustats).ceil_l_s, 0);
	__this_cpu_write((fpuemustats).ceil_l_d, 0);
	__this_cpu_write((fpuemustats).class_s, 0);
	__this_cpu_write((fpuemustats).class_d, 0);
	__this_cpu_write((fpuemustats).cmp_af_s, 0);
	__this_cpu_write((fpuemustats).cmp_af_d, 0);
	__this_cpu_write((fpuemustats).cmp_eq_s, 0);
	__this_cpu_write((fpuemustats).cmp_eq_d, 0);
	__this_cpu_write((fpuemustats).cmp_le_s, 0);
	__this_cpu_write((fpuemustats).cmp_le_d, 0);
	__this_cpu_write((fpuemustats).cmp_lt_s, 0);
	__this_cpu_write((fpuemustats).cmp_lt_d, 0);
	__this_cpu_write((fpuemustats).cmp_ne_s, 0);
	__this_cpu_write((fpuemustats).cmp_ne_d, 0);
	__this_cpu_write((fpuemustats).cmp_or_s, 0);
	__this_cpu_write((fpuemustats).cmp_or_d, 0);
	__this_cpu_write((fpuemustats).cmp_ueq_s, 0);
	__this_cpu_write((fpuemustats).cmp_ueq_d, 0);
	__this_cpu_write((fpuemustats).cmp_ule_s, 0);
	__this_cpu_write((fpuemustats).cmp_ule_d, 0);
	__this_cpu_write((fpuemustats).cmp_ult_s, 0);
	__this_cpu_write((fpuemustats).cmp_ult_d, 0);
	__this_cpu_write((fpuemustats).cmp_un_s, 0);
	__this_cpu_write((fpuemustats).cmp_un_d, 0);
	__this_cpu_write((fpuemustats).cmp_une_s, 0);
	__this_cpu_write((fpuemustats).cmp_une_d, 0);
	__this_cpu_write((fpuemustats).cmp_saf_s, 0);
	__this_cpu_write((fpuemustats).cmp_saf_d, 0);
	__this_cpu_write((fpuemustats).cmp_seq_s, 0);
	__this_cpu_write((fpuemustats).cmp_seq_d, 0);
	__this_cpu_write((fpuemustats).cmp_sle_s, 0);
	__this_cpu_write((fpuemustats).cmp_sle_d, 0);
	__this_cpu_write((fpuemustats).cmp_slt_s, 0);
	__this_cpu_write((fpuemustats).cmp_slt_d, 0);
	__this_cpu_write((fpuemustats).cmp_sne_s, 0);
	__this_cpu_write((fpuemustats).cmp_sne_d, 0);
	__this_cpu_write((fpuemustats).cmp_sor_s, 0);
	__this_cpu_write((fpuemustats).cmp_sor_d, 0);
	__this_cpu_write((fpuemustats).cmp_sueq_s, 0);
	__this_cpu_write((fpuemustats).cmp_sueq_d, 0);
	__this_cpu_write((fpuemustats).cmp_sule_s, 0);
	__this_cpu_write((fpuemustats).cmp_sule_d, 0);
	__this_cpu_write((fpuemustats).cmp_sult_s, 0);
	__this_cpu_write((fpuemustats).cmp_sult_d, 0);
	__this_cpu_write((fpuemustats).cmp_sun_s, 0);
	__this_cpu_write((fpuemustats).cmp_sun_d, 0);
	__this_cpu_write((fpuemustats).cmp_sune_s, 0);
	__this_cpu_write((fpuemustats).cmp_sune_d, 0);
	__this_cpu_write((fpuemustats).cvt_d_l, 0);
	__this_cpu_write((fpuemustats).cvt_d_s, 0);
	__this_cpu_write((fpuemustats).cvt_d_w, 0);
	__this_cpu_write((fpuemustats).cvt_l_s, 0);
	__this_cpu_write((fpuemustats).cvt_l_d, 0);
	__this_cpu_write((fpuemustats).cvt_s_d, 0);
	__this_cpu_write((fpuemustats).cvt_s_l, 0);
	__this_cpu_write((fpuemustats).cvt_s_w, 0);
	__this_cpu_write((fpuemustats).cvt_w_s, 0);
	__this_cpu_write((fpuemustats).cvt_w_d, 0);
	__this_cpu_write((fpuemustats).div_s, 0);
	__this_cpu_write((fpuemustats).div_d, 0);
	__this_cpu_write((fpuemustats).floor_w_s, 0);
	__this_cpu_write((fpuemustats).floor_w_d, 0);
	__this_cpu_write((fpuemustats).floor_l_s, 0);
	__this_cpu_write((fpuemustats).floor_l_d, 0);
	__this_cpu_write((fpuemustats).maddf_s, 0);
	__this_cpu_write((fpuemustats).maddf_d, 0);
	__this_cpu_write((fpuemustats).max_s, 0);
	__this_cpu_write((fpuemustats).max_d, 0);
	__this_cpu_write((fpuemustats).maxa_s, 0);
	__this_cpu_write((fpuemustats).maxa_d, 0);
	__this_cpu_write((fpuemustats).min_s, 0);
	__this_cpu_write((fpuemustats).min_d, 0);
	__this_cpu_write((fpuemustats).mina_s, 0);
	__this_cpu_write((fpuemustats).mina_d, 0);
	__this_cpu_write((fpuemustats).mov_s, 0);
	__this_cpu_write((fpuemustats).mov_d, 0);
	__this_cpu_write((fpuemustats).msubf_s, 0);
	__this_cpu_write((fpuemustats).msubf_d, 0);
	__this_cpu_write((fpuemustats).mul_s, 0);
	__this_cpu_write((fpuemustats).mul_d, 0);
	__this_cpu_write((fpuemustats).neg_s, 0);
	__this_cpu_write((fpuemustats).neg_d, 0);
	__this_cpu_write((fpuemustats).recip_s, 0);
	__this_cpu_write((fpuemustats).recip_d, 0);
	__this_cpu_write((fpuemustats).rint_s, 0);
	__this_cpu_write((fpuemustats).rint_d, 0);
	__this_cpu_write((fpuemustats).round_w_s, 0);
	__this_cpu_write((fpuemustats).round_w_d, 0);
	__this_cpu_write((fpuemustats).round_l_s, 0);
	__this_cpu_write((fpuemustats).round_l_d, 0);
	__this_cpu_write((fpuemustats).rsqrt_s, 0);
	__this_cpu_write((fpuemustats).rsqrt_d, 0);
	__this_cpu_write((fpuemustats).sel_s, 0);
	__this_cpu_write((fpuemustats).sel_d, 0);
	__this_cpu_write((fpuemustats).seleqz_s, 0);
	__this_cpu_write((fpuemustats).seleqz_d, 0);
	__this_cpu_write((fpuemustats).selnez_s, 0);
	__this_cpu_write((fpuemustats).selnez_d, 0);
	__this_cpu_write((fpuemustats).sqrt_s, 0);
	__this_cpu_write((fpuemustats).sqrt_d, 0);
	__this_cpu_write((fpuemustats).sub_s, 0);
	__this_cpu_write((fpuemustats).sub_d, 0);
	__this_cpu_write((fpuemustats).trunc_w_s, 0);
	__this_cpu_write((fpuemustats).trunc_w_d, 0);
	__this_cpu_write((fpuemustats).trunc_l_s, 0);
	__this_cpu_write((fpuemustats).trunc_l_d, 0);

	return 0;
}

static int fpuemustats_clear_open(struct inode *inode, struct file *file)
{
	return single_open(file, fpuemustats_clear_show, inode->i_private);
}

static const struct file_operations fpuemustats_clear_fops = {
	.open                   = fpuemustats_clear_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int __init debugfs_fpuemu(void)
{
	struct dentry *fpuemu_debugfs_base_dir;
	struct dentry *fpuemu_debugfs_inst_dir;
	struct dentry *d, *reset_file;

	if (!mips_debugfs_dir)
		return -ENODEV;

	fpuemu_debugfs_base_dir = debugfs_create_dir("fpuemustats",
						     mips_debugfs_dir);
	if (!fpuemu_debugfs_base_dir)
		return -ENOMEM;

	reset_file = debugfs_create_file("fpuemustats_clear", 0444,
					 mips_debugfs_dir, NULL,
					 &fpuemustats_clear_fops);
	if (!reset_file)
		return -ENOMEM;

#define FPU_EMU_STAT_OFFSET(m)						\
	offsetof(struct mips_fpu_emulator_stats, m)

#define FPU_STAT_CREATE(m)						\
do {									\
	d = debugfs_create_file(#m, 0444, fpuemu_debugfs_base_dir,	\
				(void *)FPU_EMU_STAT_OFFSET(m),		\
				&fops_fpuemu_stat);			\
	if (!d)								\
		return -ENOMEM;						\
} while (0)

	FPU_STAT_CREATE(emulated);
	FPU_STAT_CREATE(loads);
	FPU_STAT_CREATE(stores);
	FPU_STAT_CREATE(branches);
	FPU_STAT_CREATE(cp1ops);
	FPU_STAT_CREATE(cp1xops);
	FPU_STAT_CREATE(errors);
	FPU_STAT_CREATE(ieee754_inexact);
	FPU_STAT_CREATE(ieee754_underflow);
	FPU_STAT_CREATE(ieee754_overflow);
	FPU_STAT_CREATE(ieee754_zerodiv);
	FPU_STAT_CREATE(ieee754_invalidop);
	FPU_STAT_CREATE(ds_emul);

	fpuemu_debugfs_inst_dir = debugfs_create_dir("instructions",
						     fpuemu_debugfs_base_dir);
	if (!fpuemu_debugfs_inst_dir)
		return -ENOMEM;

#define FPU_STAT_CREATE_EX(m)						\
do {									\
	char name[32];							\
									\
	adjust_instruction_counter_name(name, #m);			\
									\
	d = debugfs_create_file(name, 0444, fpuemu_debugfs_inst_dir,	\
				(void *)FPU_EMU_STAT_OFFSET(m),		\
				&fops_fpuemu_stat);			\
	if (!d)								\
		return -ENOMEM;						\
} while (0)

	FPU_STAT_CREATE_EX(abs_s);
	FPU_STAT_CREATE_EX(abs_d);
	FPU_STAT_CREATE_EX(add_s);
	FPU_STAT_CREATE_EX(add_d);
	FPU_STAT_CREATE_EX(bc1eqz);
	FPU_STAT_CREATE_EX(bc1nez);
	FPU_STAT_CREATE_EX(ceil_w_s);
	FPU_STAT_CREATE_EX(ceil_w_d);
	FPU_STAT_CREATE_EX(ceil_l_s);
	FPU_STAT_CREATE_EX(ceil_l_d);
	FPU_STAT_CREATE_EX(class_s);
	FPU_STAT_CREATE_EX(class_d);
	FPU_STAT_CREATE_EX(cmp_af_s);
	FPU_STAT_CREATE_EX(cmp_af_d);
	FPU_STAT_CREATE_EX(cmp_eq_s);
	FPU_STAT_CREATE_EX(cmp_eq_d);
	FPU_STAT_CREATE_EX(cmp_le_s);
	FPU_STAT_CREATE_EX(cmp_le_d);
	FPU_STAT_CREATE_EX(cmp_lt_s);
	FPU_STAT_CREATE_EX(cmp_lt_d);
	FPU_STAT_CREATE_EX(cmp_ne_s);
	FPU_STAT_CREATE_EX(cmp_ne_d);
	FPU_STAT_CREATE_EX(cmp_or_s);
	FPU_STAT_CREATE_EX(cmp_or_d);
	FPU_STAT_CREATE_EX(cmp_ueq_s);
	FPU_STAT_CREATE_EX(cmp_ueq_d);
	FPU_STAT_CREATE_EX(cmp_ule_s);
	FPU_STAT_CREATE_EX(cmp_ule_d);
	FPU_STAT_CREATE_EX(cmp_ult_s);
	FPU_STAT_CREATE_EX(cmp_ult_d);
	FPU_STAT_CREATE_EX(cmp_un_s);
	FPU_STAT_CREATE_EX(cmp_un_d);
	FPU_STAT_CREATE_EX(cmp_une_s);
	FPU_STAT_CREATE_EX(cmp_une_d);
	FPU_STAT_CREATE_EX(cmp_saf_s);
	FPU_STAT_CREATE_EX(cmp_saf_d);
	FPU_STAT_CREATE_EX(cmp_seq_s);
	FPU_STAT_CREATE_EX(cmp_seq_d);
	FPU_STAT_CREATE_EX(cmp_sle_s);
	FPU_STAT_CREATE_EX(cmp_sle_d);
	FPU_STAT_CREATE_EX(cmp_slt_s);
	FPU_STAT_CREATE_EX(cmp_slt_d);
	FPU_STAT_CREATE_EX(cmp_sne_s);
	FPU_STAT_CREATE_EX(cmp_sne_d);
	FPU_STAT_CREATE_EX(cmp_sor_s);
	FPU_STAT_CREATE_EX(cmp_sor_d);
	FPU_STAT_CREATE_EX(cmp_sueq_s);
	FPU_STAT_CREATE_EX(cmp_sueq_d);
	FPU_STAT_CREATE_EX(cmp_sule_s);
	FPU_STAT_CREATE_EX(cmp_sule_d);
	FPU_STAT_CREATE_EX(cmp_sult_s);
	FPU_STAT_CREATE_EX(cmp_sult_d);
	FPU_STAT_CREATE_EX(cmp_sun_s);
	FPU_STAT_CREATE_EX(cmp_sun_d);
	FPU_STAT_CREATE_EX(cmp_sune_s);
	FPU_STAT_CREATE_EX(cmp_sune_d);
	FPU_STAT_CREATE_EX(cvt_d_l);
	FPU_STAT_CREATE_EX(cvt_d_s);
	FPU_STAT_CREATE_EX(cvt_d_w);
	FPU_STAT_CREATE_EX(cvt_l_s);
	FPU_STAT_CREATE_EX(cvt_l_d);
	FPU_STAT_CREATE_EX(cvt_s_d);
	FPU_STAT_CREATE_EX(cvt_s_l);
	FPU_STAT_CREATE_EX(cvt_s_w);
	FPU_STAT_CREATE_EX(cvt_w_s);
	FPU_STAT_CREATE_EX(cvt_w_d);
	FPU_STAT_CREATE_EX(div_s);
	FPU_STAT_CREATE_EX(div_d);
	FPU_STAT_CREATE_EX(floor_w_s);
	FPU_STAT_CREATE_EX(floor_w_d);
	FPU_STAT_CREATE_EX(floor_l_s);
	FPU_STAT_CREATE_EX(floor_l_d);
	FPU_STAT_CREATE_EX(maddf_s);
	FPU_STAT_CREATE_EX(maddf_d);
	FPU_STAT_CREATE_EX(max_s);
	FPU_STAT_CREATE_EX(max_d);
	FPU_STAT_CREATE_EX(maxa_s);
	FPU_STAT_CREATE_EX(maxa_d);
	FPU_STAT_CREATE_EX(min_s);
	FPU_STAT_CREATE_EX(min_d);
	FPU_STAT_CREATE_EX(mina_s);
	FPU_STAT_CREATE_EX(mina_d);
	FPU_STAT_CREATE_EX(mov_s);
	FPU_STAT_CREATE_EX(mov_d);
	FPU_STAT_CREATE_EX(msubf_s);
	FPU_STAT_CREATE_EX(msubf_d);
	FPU_STAT_CREATE_EX(mul_s);
	FPU_STAT_CREATE_EX(mul_d);
	FPU_STAT_CREATE_EX(neg_s);
	FPU_STAT_CREATE_EX(neg_d);
	FPU_STAT_CREATE_EX(recip_s);
	FPU_STAT_CREATE_EX(recip_d);
	FPU_STAT_CREATE_EX(rint_s);
	FPU_STAT_CREATE_EX(rint_d);
	FPU_STAT_CREATE_EX(round_w_s);
	FPU_STAT_CREATE_EX(round_w_d);
	FPU_STAT_CREATE_EX(round_l_s);
	FPU_STAT_CREATE_EX(round_l_d);
	FPU_STAT_CREATE_EX(rsqrt_s);
	FPU_STAT_CREATE_EX(rsqrt_d);
	FPU_STAT_CREATE_EX(sel_s);
	FPU_STAT_CREATE_EX(sel_d);
	FPU_STAT_CREATE_EX(seleqz_s);
	FPU_STAT_CREATE_EX(seleqz_d);
	FPU_STAT_CREATE_EX(selnez_s);
	FPU_STAT_CREATE_EX(selnez_d);
	FPU_STAT_CREATE_EX(sqrt_s);
	FPU_STAT_CREATE_EX(sqrt_d);
	FPU_STAT_CREATE_EX(sub_s);
	FPU_STAT_CREATE_EX(sub_d);
	FPU_STAT_CREATE_EX(trunc_w_s);
	FPU_STAT_CREATE_EX(trunc_w_d);
	FPU_STAT_CREATE_EX(trunc_l_s);
	FPU_STAT_CREATE_EX(trunc_l_d);

	return 0;
}
arch_initcall(debugfs_fpuemu);
