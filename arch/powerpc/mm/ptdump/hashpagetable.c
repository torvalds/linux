// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016, Rashmica Gupta, IBM Corp.
 *
 * This traverses the kernel virtual memory and dumps the pages that are in
 * the hash pagetable, along with their flags to
 * /sys/kernel/debug/kernel_hash_pagetable.
 *
 * If radix is enabled then there is no hash page table and so no debugfs file
 * is generated.
 */
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/const.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/plpar_wrappers.h>
#include <linux/memblock.h>
#include <asm/firmware.h>

struct pg_state {
	struct seq_file *seq;
	const struct addr_marker *marker;
	unsigned long start_address;
	unsigned int level;
	u64 current_flags;
};

struct addr_marker {
	unsigned long start_address;
	const char *name;
};

static struct addr_marker address_markers[] = {
	{ 0,	"Start of kernel VM" },
	{ 0,	"vmalloc() Area" },
	{ 0,	"vmalloc() End" },
	{ 0,	"isa I/O start" },
	{ 0,	"isa I/O end" },
	{ 0,	"phb I/O start" },
	{ 0,	"phb I/O end" },
	{ 0,	"I/O remap start" },
	{ 0,	"I/O remap end" },
	{ 0,	"vmemmap start" },
	{ -1,	NULL },
};

struct flag_info {
	u64		mask;
	u64		val;
	const char	*set;
	const char	*clear;
	bool		is_val;
	int		shift;
};

static const struct flag_info v_flag_array[] = {
	{
		.mask   = SLB_VSID_B,
		.val    = SLB_VSID_B_256M,
		.set    = "ssize: 256M",
		.clear  = "ssize: 1T  ",
	}, {
		.mask	= HPTE_V_SECONDARY,
		.val	= HPTE_V_SECONDARY,
		.set	= "secondary",
		.clear	= "primary  ",
	}, {
		.mask	= HPTE_V_VALID,
		.val	= HPTE_V_VALID,
		.set	= "valid  ",
		.clear	= "invalid",
	}, {
		.mask	= HPTE_V_BOLTED,
		.val	= HPTE_V_BOLTED,
		.set	= "bolted",
		.clear	= "",
	}
};

static const struct flag_info r_flag_array[] = {
	{
		.mask	= HPTE_R_PP0 | HPTE_R_PP,
		.val	= PP_RWXX,
		.set	= "prot:RW--",
	}, {
		.mask	= HPTE_R_PP0 | HPTE_R_PP,
		.val	= PP_RWRX,
		.set	= "prot:RWR-",
	}, {
		.mask	= HPTE_R_PP0 | HPTE_R_PP,
		.val	= PP_RWRW,
		.set	= "prot:RWRW",
	}, {
		.mask	= HPTE_R_PP0 | HPTE_R_PP,
		.val	= PP_RXRX,
		.set	= "prot:R-R-",
	}, {
		.mask	= HPTE_R_PP0 | HPTE_R_PP,
		.val	= PP_RXXX,
		.set	= "prot:R---",
	}, {
		.mask	= HPTE_R_KEY_HI | HPTE_R_KEY_LO,
		.val	= HPTE_R_KEY_HI | HPTE_R_KEY_LO,
		.set	= "key",
		.clear	= "",
		.is_val = true,
	}, {
		.mask	= HPTE_R_R,
		.val	= HPTE_R_R,
		.set	= "ref",
		.clear	= "   ",
	}, {
		.mask	= HPTE_R_C,
		.val	= HPTE_R_C,
		.set	= "changed",
		.clear	= "       ",
	}, {
		.mask	= HPTE_R_N,
		.val	= HPTE_R_N,
		.set	= "no execute",
	}, {
		.mask	= HPTE_R_WIMG,
		.val	= HPTE_R_W,
		.set	= "writethru",
	}, {
		.mask	= HPTE_R_WIMG,
		.val	= HPTE_R_I,
		.set	= "no cache",
	}, {
		.mask	= HPTE_R_WIMG,
		.val	= HPTE_R_G,
		.set	= "guarded",
	}
};

static int calculate_pagesize(struct pg_state *st, int ps, char s[])
{
	static const char units[] = "BKMGTPE";
	const char *unit = units;

	while (ps > 9 && unit[1]) {
		ps -= 10;
		unit++;
	}
	seq_printf(st->seq, "  %s_ps: %i%c\t", s, 1<<ps, *unit);
	return ps;
}

static void dump_flag_info(struct pg_state *st, const struct flag_info
		*flag, u64 pte, int num)
{
	unsigned int i;

	for (i = 0; i < num; i++, flag++) {
		const char *s = NULL;
		u64 val;

		/* flag not defined so don't check it */
		if (flag->mask == 0)
			continue;
		/* Some 'flags' are actually values */
		if (flag->is_val) {
			val = pte & flag->val;
			if (flag->shift)
				val = val >> flag->shift;
			seq_printf(st->seq, "  %s:%llx", flag->set, val);
		} else {
			if ((pte & flag->mask) == flag->val)
				s = flag->set;
			else
				s = flag->clear;
			if (s)
				seq_printf(st->seq, "  %s", s);
		}
	}
}

static void dump_hpte_info(struct pg_state *st, unsigned long ea, u64 v, u64 r,
		unsigned long rpn, int bps, int aps, unsigned long lp)
{
	int aps_index;

	while (ea >= st->marker[1].start_address) {
		st->marker++;
		seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	}
	seq_printf(st->seq, "0x%lx:\t", ea);
	seq_printf(st->seq, "AVPN:%llx\t", HPTE_V_AVPN_VAL(v));
	dump_flag_info(st, v_flag_array, v, ARRAY_SIZE(v_flag_array));
	seq_printf(st->seq, "  rpn: %lx\t", rpn);
	dump_flag_info(st, r_flag_array, r, ARRAY_SIZE(r_flag_array));

	calculate_pagesize(st, bps, "base");
	aps_index = calculate_pagesize(st, aps, "actual");
	if (aps_index != 2)
		seq_printf(st->seq, "LP enc: %lx", lp);
	seq_putc(st->seq, '\n');
}


static int native_find(unsigned long ea, int psize, bool primary, u64 *v, u64
		*r)
{
	struct hash_pte *hptep;
	unsigned long hash, vsid, vpn, hpte_group, want_v, hpte_v;
	int i, ssize = mmu_kernel_ssize;
	unsigned long shift = mmu_psize_defs[psize].shift;

	/* calculate hash */
	vsid = get_kernel_vsid(ea, ssize);
	vpn  = hpt_vpn(ea, vsid, ssize);
	hash = hpt_hash(vpn, shift, ssize);
	want_v = hpte_encode_avpn(vpn, psize, ssize);

	/* to check in the secondary hash table, we invert the hash */
	if (!primary)
		hash = ~hash;
	hpte_group = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	for (i = 0; i < HPTES_PER_GROUP; i++) {
		hptep = htab_address + hpte_group;
		hpte_v = be64_to_cpu(hptep->v);

		if (HPTE_V_COMPARE(hpte_v, want_v) && (hpte_v & HPTE_V_VALID)) {
			/* HPTE matches */
			*v = be64_to_cpu(hptep->v);
			*r = be64_to_cpu(hptep->r);
			return 0;
		}
		++hpte_group;
	}
	return -1;
}

static int pseries_find(unsigned long ea, int psize, bool primary, u64 *v, u64 *r)
{
	struct hash_pte ptes[4];
	unsigned long vsid, vpn, hash, hpte_group, want_v;
	int i, j, ssize = mmu_kernel_ssize;
	long lpar_rc = 0;
	unsigned long shift = mmu_psize_defs[psize].shift;

	/* calculate hash */
	vsid = get_kernel_vsid(ea, ssize);
	vpn  = hpt_vpn(ea, vsid, ssize);
	hash = hpt_hash(vpn, shift, ssize);
	want_v = hpte_encode_avpn(vpn, psize, ssize);

	/* to check in the secondary hash table, we invert the hash */
	if (!primary)
		hash = ~hash;
	hpte_group = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	/* see if we can find an entry in the hpte with this hash */
	for (i = 0; i < HPTES_PER_GROUP; i += 4, hpte_group += 4) {
		lpar_rc = plpar_pte_read_4(0, hpte_group, (void *)ptes);

		if (lpar_rc != H_SUCCESS)
			continue;
		for (j = 0; j < 4; j++) {
			if (HPTE_V_COMPARE(ptes[j].v, want_v) &&
					(ptes[j].v & HPTE_V_VALID)) {
				/* HPTE matches */
				*v = ptes[j].v;
				*r = ptes[j].r;
				return 0;
			}
		}
	}
	return -1;
}

static void decode_r(int bps, unsigned long r, unsigned long *rpn, int *aps,
		unsigned long *lp_bits)
{
	struct mmu_psize_def entry;
	unsigned long arpn, mask, lp;
	int penc = -2, idx = 0, shift;

	/*.
	 * The LP field has 8 bits. Depending on the actual page size, some of
	 * these bits are concatenated with the APRN to get the RPN. The rest
	 * of the bits in the LP field is the LP value and is an encoding for
	 * the base page size and the actual page size.
	 *
	 *  -	find the mmu entry for our base page size
	 *  -	go through all page encodings and use the associated mask to
	 *	find an encoding that matches our encoding in the LP field.
	 */
	arpn = (r & HPTE_R_RPN) >> HPTE_R_RPN_SHIFT;
	lp = arpn & 0xff;

	entry = mmu_psize_defs[bps];
	while (idx < MMU_PAGE_COUNT) {
		penc = entry.penc[idx];
		if ((penc != -1) && (mmu_psize_defs[idx].shift)) {
			shift = mmu_psize_defs[idx].shift -  HPTE_R_RPN_SHIFT;
			mask = (0x1 << (shift)) - 1;
			if ((lp & mask) == penc) {
				*aps = mmu_psize_to_shift(idx);
				*lp_bits = lp & mask;
				*rpn = arpn >> shift;
				return;
			}
		}
		idx++;
	}
}

static int base_hpte_find(unsigned long ea, int psize, bool primary, u64 *v,
			  u64 *r)
{
	if (IS_ENABLED(CONFIG_PPC_PSERIES) && firmware_has_feature(FW_FEATURE_LPAR))
		return pseries_find(ea, psize, primary, v, r);

	return native_find(ea, psize, primary, v, r);
}

static unsigned long hpte_find(struct pg_state *st, unsigned long ea, int psize)
{
	unsigned long slot;
	u64 v  = 0, r = 0;
	unsigned long rpn, lp_bits;
	int base_psize = 0, actual_psize = 0;

	if (ea < PAGE_OFFSET)
		return -1;

	/* Look in primary table */
	slot = base_hpte_find(ea, psize, true, &v, &r);

	/* Look in secondary table */
	if (slot == -1)
		slot = base_hpte_find(ea, psize, false, &v, &r);

	/* No entry found */
	if (slot == -1)
		return -1;

	/*
	 * We found an entry in the hash page table:
	 *  - check that this has the same base page
	 *  - find the actual page size
	 *  - find the RPN
	 */
	base_psize = mmu_psize_to_shift(psize);

	if ((v & HPTE_V_LARGE) == HPTE_V_LARGE) {
		decode_r(psize, r, &rpn, &actual_psize, &lp_bits);
	} else {
		/* 4K actual page size */
		actual_psize = 12;
		rpn = (r & HPTE_R_RPN) >> HPTE_R_RPN_SHIFT;
		/* In this case there are no LP bits */
		lp_bits = -1;
	}
	/*
	 * We didn't find a matching encoding, so the PTE we found isn't for
	 * this address.
	 */
	if (actual_psize == -1)
		return -1;

	dump_hpte_info(st, ea, v, r, rpn, base_psize, actual_psize, lp_bits);
	return 0;
}

static void walk_pte(struct pg_state *st, pmd_t *pmd, unsigned long start)
{
	pte_t *pte = pte_offset_kernel(pmd, 0);
	unsigned long addr, pteval, psize;
	int i, status;

	for (i = 0; i < PTRS_PER_PTE; i++, pte++) {
		addr = start + i * PAGE_SIZE;
		pteval = pte_val(*pte);

		if (addr < VMALLOC_END)
			psize = mmu_vmalloc_psize;
		else
			psize = mmu_io_psize;

		/* check for secret 4K mappings */
		if (IS_ENABLED(CONFIG_PPC_64K_PAGES) &&
		    ((pteval & H_PAGE_COMBO) == H_PAGE_COMBO ||
		     (pteval & H_PAGE_4K_PFN) == H_PAGE_4K_PFN))
			psize = mmu_io_psize;

		/* check for hashpte */
		status = hpte_find(st, addr, psize);

		if (((pteval & H_PAGE_HASHPTE) != H_PAGE_HASHPTE)
				&& (status != -1)) {
		/* found a hpte that is not in the linux page tables */
			seq_printf(st->seq, "page probably bolted before linux"
				" pagetables were set: addr:%lx, pteval:%lx\n",
				addr, pteval);
		}
	}
}

static void walk_pmd(struct pg_state *st, pud_t *pud, unsigned long start)
{
	pmd_t *pmd = pmd_offset(pud, 0);
	unsigned long addr;
	unsigned int i;

	for (i = 0; i < PTRS_PER_PMD; i++, pmd++) {
		addr = start + i * PMD_SIZE;
		if (!pmd_none(*pmd))
			/* pmd exists */
			walk_pte(st, pmd, addr);
	}
}

static void walk_pud(struct pg_state *st, p4d_t *p4d, unsigned long start)
{
	pud_t *pud = pud_offset(p4d, 0);
	unsigned long addr;
	unsigned int i;

	for (i = 0; i < PTRS_PER_PUD; i++, pud++) {
		addr = start + i * PUD_SIZE;
		if (!pud_none(*pud))
			/* pud exists */
			walk_pmd(st, pud, addr);
	}
}

static void walk_p4d(struct pg_state *st, pgd_t *pgd, unsigned long start)
{
	p4d_t *p4d = p4d_offset(pgd, 0);
	unsigned long addr;
	unsigned int i;

	for (i = 0; i < PTRS_PER_P4D; i++, p4d++) {
		addr = start + i * P4D_SIZE;
		if (!p4d_none(*p4d))
			/* p4d exists */
			walk_pud(st, p4d, addr);
	}
}

static void walk_pagetables(struct pg_state *st)
{
	pgd_t *pgd = pgd_offset_k(0UL);
	unsigned int i;
	unsigned long addr;

	/*
	 * Traverse the linux pagetable structure and dump pages that are in
	 * the hash pagetable.
	 */
	for (i = 0; i < PTRS_PER_PGD; i++, pgd++) {
		addr = KERN_VIRT_START + i * PGDIR_SIZE;
		if (!pgd_none(*pgd))
			/* pgd exists */
			walk_p4d(st, pgd, addr);
	}
}


static void walk_linearmapping(struct pg_state *st)
{
	unsigned long addr;

	/*
	 * Traverse the linear mapping section of virtual memory and dump pages
	 * that are in the hash pagetable.
	 */
	unsigned long psize = 1 << mmu_psize_defs[mmu_linear_psize].shift;

	for (addr = PAGE_OFFSET; addr < PAGE_OFFSET +
			memblock_end_of_DRAM(); addr += psize)
		hpte_find(st, addr, mmu_linear_psize);
}

static void walk_vmemmap(struct pg_state *st)
{
	struct vmemmap_backing *ptr = vmemmap_list;

	if (!IS_ENABLED(CONFIG_SPARSEMEM_VMEMMAP))
		return;
	/*
	 * Traverse the vmemmaped memory and dump pages that are in the hash
	 * pagetable.
	 */
	while (ptr->list) {
		hpte_find(st, ptr->virt_addr, mmu_vmemmap_psize);
		ptr = ptr->list;
	}
	seq_puts(st->seq, "---[ vmemmap end ]---\n");
}

static void populate_markers(void)
{
	address_markers[0].start_address = PAGE_OFFSET;
	address_markers[1].start_address = VMALLOC_START;
	address_markers[2].start_address = VMALLOC_END;
	address_markers[3].start_address = ISA_IO_BASE;
	address_markers[4].start_address = ISA_IO_END;
	address_markers[5].start_address = PHB_IO_BASE;
	address_markers[6].start_address = PHB_IO_END;
	address_markers[7].start_address = IOREMAP_BASE;
	address_markers[8].start_address = IOREMAP_END;
	address_markers[9].start_address =  H_VMEMMAP_START;
}

static int ptdump_show(struct seq_file *m, void *v)
{
	struct pg_state st = {
		.seq = m,
		.start_address = PAGE_OFFSET,
		.marker = address_markers,
	};
	/*
	 * Traverse the 0xc, 0xd and 0xf areas of the kernel virtual memory and
	 * dump pages that are in the hash pagetable.
	 */
	walk_linearmapping(&st);
	walk_pagetables(&st);
	walk_vmemmap(&st);
	return 0;
}

static int ptdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ptdump_show, NULL);
}

static const struct file_operations ptdump_fops = {
	.open		= ptdump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ptdump_init(void)
{
	if (!radix_enabled()) {
		populate_markers();
		debugfs_create_file("kernel_hash_pagetable", 0400, NULL, NULL,
				    &ptdump_fops);
	}
	return 0;
}
device_initcall(ptdump_init);
