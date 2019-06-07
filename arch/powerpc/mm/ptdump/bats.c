// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018, Christophe Leroy CS S.I.
 * <christophe.leroy@c-s.fr>
 *
 * This dumps the content of BATS
 */

#include <asm/debugfs.h>
#include <asm/pgtable.h>
#include <asm/cpu_has_feature.h>

static char *pp_601(int k, int pp)
{
	if (pp == 0)
		return k ? "NA" : "RWX";
	if (pp == 1)
		return k ? "ROX" : "RWX";
	if (pp == 2)
		return k ? "RWX" : "RWX";
	return k ? "ROX" : "ROX";
}

static void bat_show_601(struct seq_file *m, int idx, u32 lower, u32 upper)
{
	u32 blpi = upper & 0xfffe0000;
	u32 k = (upper >> 2) & 3;
	u32 pp = upper & 3;
	phys_addr_t pbn = PHYS_BAT_ADDR(lower);
	u32 bsm = lower & 0x3ff;
	u32 size = (bsm + 1) << 17;

	seq_printf(m, "%d: ", idx);
	if (!(lower & 0x40)) {
		seq_puts(m, "        -\n");
		return;
	}

	seq_printf(m, "0x%08x-0x%08x ", blpi, blpi + size - 1);
#ifdef CONFIG_PHYS_64BIT
	seq_printf(m, "0x%016llx ", pbn);
#else
	seq_printf(m, "0x%08x ", pbn);
#endif

	seq_printf(m, "Kernel %s User %s", pp_601(k & 2, pp), pp_601(k & 1, pp));

	if (lower & _PAGE_WRITETHRU)
		seq_puts(m, "write through ");
	if (lower & _PAGE_NO_CACHE)
		seq_puts(m, "no cache ");
	if (lower & _PAGE_COHERENT)
		seq_puts(m, "coherent ");
	seq_puts(m, "\n");
}

#define BAT_SHOW_601(_m, _n, _l, _u) bat_show_601(_m, _n, mfspr(_l), mfspr(_u))

static int bats_show_601(struct seq_file *m, void *v)
{
	seq_puts(m, "---[ Block Address Translation ]---\n");

	BAT_SHOW_601(m, 0, SPRN_IBAT0L, SPRN_IBAT0U);
	BAT_SHOW_601(m, 1, SPRN_IBAT1L, SPRN_IBAT1U);
	BAT_SHOW_601(m, 2, SPRN_IBAT2L, SPRN_IBAT2U);
	BAT_SHOW_601(m, 3, SPRN_IBAT3L, SPRN_IBAT3U);

	return 0;
}

static void bat_show_603(struct seq_file *m, int idx, u32 lower, u32 upper, bool is_d)
{
	u32 bepi = upper & 0xfffe0000;
	u32 bl = (upper >> 2) & 0x7ff;
	u32 k = upper & 3;
	phys_addr_t brpn = PHYS_BAT_ADDR(lower);
	u32 size = (bl + 1) << 17;

	seq_printf(m, "%d: ", idx);
	if (k == 0) {
		seq_puts(m, "        -\n");
		return;
	}

	seq_printf(m, "0x%08x-0x%08x ", bepi, bepi + size - 1);
#ifdef CONFIG_PHYS_64BIT
	seq_printf(m, "0x%016llx ", brpn);
#else
	seq_printf(m, "0x%08x ", brpn);
#endif

	if (k == 1)
		seq_puts(m, "User ");
	else if (k == 2)
		seq_puts(m, "Kernel ");
	else
		seq_puts(m, "Kernel/User ");

	if (lower & BPP_RX)
		seq_puts(m, is_d ? "RO " : "EXEC ");
	else if (lower & BPP_RW)
		seq_puts(m, is_d ? "RW " : "EXEC ");
	else
		seq_puts(m, is_d ? "NA " : "NX   ");

	if (lower & _PAGE_WRITETHRU)
		seq_puts(m, "write through ");
	if (lower & _PAGE_NO_CACHE)
		seq_puts(m, "no cache ");
	if (lower & _PAGE_COHERENT)
		seq_puts(m, "coherent ");
	if (lower & _PAGE_GUARDED)
		seq_puts(m, "guarded ");
	seq_puts(m, "\n");
}

#define BAT_SHOW_603(_m, _n, _l, _u, _d) bat_show_603(_m, _n, mfspr(_l), mfspr(_u), _d)

static int bats_show_603(struct seq_file *m, void *v)
{
	seq_puts(m, "---[ Instruction Block Address Translation ]---\n");

	BAT_SHOW_603(m, 0, SPRN_IBAT0L, SPRN_IBAT0U, false);
	BAT_SHOW_603(m, 1, SPRN_IBAT1L, SPRN_IBAT1U, false);
	BAT_SHOW_603(m, 2, SPRN_IBAT2L, SPRN_IBAT2U, false);
	BAT_SHOW_603(m, 3, SPRN_IBAT3L, SPRN_IBAT3U, false);
	if (mmu_has_feature(MMU_FTR_USE_HIGH_BATS)) {
		BAT_SHOW_603(m, 4, SPRN_IBAT4L, SPRN_IBAT4U, false);
		BAT_SHOW_603(m, 5, SPRN_IBAT5L, SPRN_IBAT5U, false);
		BAT_SHOW_603(m, 6, SPRN_IBAT6L, SPRN_IBAT6U, false);
		BAT_SHOW_603(m, 7, SPRN_IBAT7L, SPRN_IBAT7U, false);
	}

	seq_puts(m, "\n---[ Data Block Address Translation ]---\n");

	BAT_SHOW_603(m, 0, SPRN_DBAT0L, SPRN_DBAT0U, true);
	BAT_SHOW_603(m, 1, SPRN_DBAT1L, SPRN_DBAT1U, true);
	BAT_SHOW_603(m, 2, SPRN_DBAT2L, SPRN_DBAT2U, true);
	BAT_SHOW_603(m, 3, SPRN_DBAT3L, SPRN_DBAT3U, true);
	if (mmu_has_feature(MMU_FTR_USE_HIGH_BATS)) {
		BAT_SHOW_603(m, 4, SPRN_DBAT4L, SPRN_DBAT4U, true);
		BAT_SHOW_603(m, 5, SPRN_DBAT5L, SPRN_DBAT5U, true);
		BAT_SHOW_603(m, 6, SPRN_DBAT6L, SPRN_DBAT6U, true);
		BAT_SHOW_603(m, 7, SPRN_DBAT7L, SPRN_DBAT7U, true);
	}

	return 0;
}

static int bats_open(struct inode *inode, struct file *file)
{
	if (cpu_has_feature(CPU_FTR_601))
		return single_open(file, bats_show_601, NULL);

	return single_open(file, bats_show_603, NULL);
}

static const struct file_operations bats_fops = {
	.open		= bats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init bats_init(void)
{
	struct dentry *debugfs_file;

	debugfs_file = debugfs_create_file("block_address_translation", 0400,
					   powerpc_debugfs_root, NULL, &bats_fops);
	return debugfs_file ? 0 : -ENOMEM;
}
device_initcall(bats_init);
