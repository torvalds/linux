/*
 * pSeries_lpar.c
 * Copyright (C) 2001 Todd Inglett, IBM Corporation
 *
 * pSeries LPAR support.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Enables debugging of low-level hash table routines - careful! */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/console.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/mmu_context.h>
#include <asm/iommu.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/prom.h>
#include <asm/cputable.h>
#include <asm/udbg.h>
#include <asm/smp.h>

#include "plpar_wrappers.h"
#include "pseries.h"


/* in hvCall.S */
EXPORT_SYMBOL(plpar_hcall);
EXPORT_SYMBOL(plpar_hcall9);
EXPORT_SYMBOL(plpar_hcall_norets);

extern void pSeries_find_serial_port(void);


static int vtermno;	/* virtual terminal# for udbg  */

#define __ALIGNED__ __attribute__((__aligned__(sizeof(long))))
static void udbg_hvsi_putc(char c)
{
	/* packet's seqno isn't used anyways */
	uint8_t packet[] __ALIGNED__ = { 0xff, 5, 0, 0, c };
	int rc;

	if (c == '\n')
		udbg_hvsi_putc('\r');

	do {
		rc = plpar_put_term_char(vtermno, sizeof(packet), packet);
	} while (rc == H_BUSY);
}

static long hvsi_udbg_buf_len;
static uint8_t hvsi_udbg_buf[256];

static int udbg_hvsi_getc_poll(void)
{
	unsigned char ch;
	int rc, i;

	if (hvsi_udbg_buf_len == 0) {
		rc = plpar_get_term_char(vtermno, &hvsi_udbg_buf_len, hvsi_udbg_buf);
		if (rc != H_SUCCESS || hvsi_udbg_buf[0] != 0xff) {
			/* bad read or non-data packet */
			hvsi_udbg_buf_len = 0;
		} else {
			/* remove the packet header */
			for (i = 4; i < hvsi_udbg_buf_len; i++)
				hvsi_udbg_buf[i-4] = hvsi_udbg_buf[i];
			hvsi_udbg_buf_len -= 4;
		}
	}

	if (hvsi_udbg_buf_len <= 0 || hvsi_udbg_buf_len > 256) {
		/* no data ready */
		hvsi_udbg_buf_len = 0;
		return -1;
	}

	ch = hvsi_udbg_buf[0];
	/* shift remaining data down */
	for (i = 1; i < hvsi_udbg_buf_len; i++) {
		hvsi_udbg_buf[i-1] = hvsi_udbg_buf[i];
	}
	hvsi_udbg_buf_len--;

	return ch;
}

static int udbg_hvsi_getc(void)
{
	int ch;
	for (;;) {
		ch = udbg_hvsi_getc_poll();
		if (ch == -1) {
			/* This shouldn't be needed...but... */
			volatile unsigned long delay;
			for (delay=0; delay < 2000000; delay++)
				;
		} else {
			return ch;
		}
	}
}

static void udbg_putcLP(char c)
{
	char buf[16];
	unsigned long rc;

	if (c == '\n')
		udbg_putcLP('\r');

	buf[0] = c;
	do {
		rc = plpar_put_term_char(vtermno, 1, buf);
	} while(rc == H_BUSY);
}

/* Buffered chars getc */
static long inbuflen;
static long inbuf[2];	/* must be 2 longs */

static int udbg_getc_pollLP(void)
{
	/* The interface is tricky because it may return up to 16 chars.
	 * We save them statically for future calls to udbg_getc().
	 */
	char ch, *buf = (char *)inbuf;
	int i;
	long rc;
	if (inbuflen == 0) {
		/* get some more chars. */
		inbuflen = 0;
		rc = plpar_get_term_char(vtermno, &inbuflen, buf);
		if (rc != H_SUCCESS)
			inbuflen = 0;	/* otherwise inbuflen is garbage */
	}
	if (inbuflen <= 0 || inbuflen > 16) {
		/* Catch error case as well as other oddities (corruption) */
		inbuflen = 0;
		return -1;
	}
	ch = buf[0];
	for (i = 1; i < inbuflen; i++)	/* shuffle them down. */
		buf[i-1] = buf[i];
	inbuflen--;
	return ch;
}

static int udbg_getcLP(void)
{
	int ch;
	for (;;) {
		ch = udbg_getc_pollLP();
		if (ch == -1) {
			/* This shouldn't be needed...but... */
			volatile unsigned long delay;
			for (delay=0; delay < 2000000; delay++)
				;
		} else {
			return ch;
		}
	}
}

/* call this from early_init() for a working debug console on
 * vterm capable LPAR machines
 */
void __init udbg_init_debug_lpar(void)
{
	vtermno = 0;
	udbg_putc = udbg_putcLP;
	udbg_getc = udbg_getcLP;
	udbg_getc_poll = udbg_getc_pollLP;

	register_early_udbg_console();
}

/* returns 0 if couldn't find or use /chosen/stdout as console */
void __init find_udbg_vterm(void)
{
	struct device_node *stdout_node;
	const u32 *termno;
	const char *name;

	/* find the boot console from /chosen/stdout */
	if (!of_chosen)
		return;
	name = of_get_property(of_chosen, "linux,stdout-path", NULL);
	if (name == NULL)
		return;
	stdout_node = of_find_node_by_path(name);
	if (!stdout_node)
		return;
	name = of_get_property(stdout_node, "name", NULL);
	if (!name) {
		printk(KERN_WARNING "stdout node missing 'name' property!\n");
		goto out;
	}

	/* Check if it's a virtual terminal */
	if (strncmp(name, "vty", 3) != 0)
		goto out;
	termno = of_get_property(stdout_node, "reg", NULL);
	if (termno == NULL)
		goto out;
	vtermno = termno[0];

	if (of_device_is_compatible(stdout_node, "hvterm1")) {
		udbg_putc = udbg_putcLP;
		udbg_getc = udbg_getcLP;
		udbg_getc_poll = udbg_getc_pollLP;
		add_preferred_console("hvc", termno[0] & 0xff, NULL);
	} else if (of_device_is_compatible(stdout_node, "hvterm-protocol")) {
		vtermno = termno[0];
		udbg_putc = udbg_hvsi_putc;
		udbg_getc = udbg_hvsi_getc;
		udbg_getc_poll = udbg_hvsi_getc_poll;
		add_preferred_console("hvsi", termno[0] & 0xff, NULL);
	}
out:
	of_node_put(stdout_node);
}

void vpa_init(int cpu)
{
	int hwcpu = get_hard_smp_processor_id(cpu);
	unsigned long addr;
	long ret;

	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		lppaca[cpu].vmxregs_in_use = 1;

	addr = __pa(&lppaca[cpu]);
	ret = register_vpa(hwcpu, addr);

	if (ret) {
		printk(KERN_ERR "WARNING: vpa_init: VPA registration for "
				"cpu %d (hw %d) of area %lx returns %ld\n",
				cpu, hwcpu, addr, ret);
		return;
	}
	/*
	 * PAPR says this feature is SLB-Buffer but firmware never
	 * reports that.  All SPLPAR support SLB shadow buffer.
	 */
	addr = __pa(&slb_shadow[cpu]);
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		ret = register_slb_shadow(hwcpu, addr);
		if (ret)
			printk(KERN_ERR
			       "WARNING: vpa_init: SLB shadow buffer "
			       "registration for cpu %d (hw %d) of area %lx "
			       "returns %ld\n", cpu, hwcpu, addr, ret);
	}
}

static long pSeries_lpar_hpte_insert(unsigned long hpte_group,
 			      unsigned long va, unsigned long pa,
 			      unsigned long rflags, unsigned long vflags,
			      int psize, int ssize)
{
	unsigned long lpar_rc;
	unsigned long flags;
	unsigned long slot;
	unsigned long hpte_v, hpte_r;

	if (!(vflags & HPTE_V_BOLTED))
		pr_devel("hpte_insert(group=%lx, va=%016lx, pa=%016lx, "
			 "rflags=%lx, vflags=%lx, psize=%d)\n",
			 hpte_group, va, pa, rflags, vflags, psize);

	hpte_v = hpte_encode_v(va, psize, ssize) | vflags | HPTE_V_VALID;
	hpte_r = hpte_encode_r(pa, psize) | rflags;

	if (!(vflags & HPTE_V_BOLTED))
		pr_devel(" hpte_v=%016lx, hpte_r=%016lx\n", hpte_v, hpte_r);

	/* Now fill in the actual HPTE */
	/* Set CEC cookie to 0         */
	/* Zero page = 0               */
	/* I-cache Invalidate = 0      */
	/* I-cache synchronize = 0     */
	/* Exact = 0                   */
	flags = 0;

	/* Make pHyp happy */
	if ((rflags & _PAGE_NO_CACHE) & !(rflags & _PAGE_WRITETHRU))
		hpte_r &= ~_PAGE_COHERENT;

	lpar_rc = plpar_pte_enter(flags, hpte_group, hpte_v, hpte_r, &slot);
	if (unlikely(lpar_rc == H_PTEG_FULL)) {
		if (!(vflags & HPTE_V_BOLTED))
			pr_devel(" full\n");
		return -1;
	}

	/*
	 * Since we try and ioremap PHBs we don't own, the pte insert
	 * will fail. However we must catch the failure in hash_page
	 * or we will loop forever, so return -2 in this case.
	 */
	if (unlikely(lpar_rc != H_SUCCESS)) {
		if (!(vflags & HPTE_V_BOLTED))
			pr_devel(" lpar err %lu\n", lpar_rc);
		return -2;
	}
	if (!(vflags & HPTE_V_BOLTED))
		pr_devel(" -> slot: %lu\n", slot & 7);

	/* Because of iSeries, we have to pass down the secondary
	 * bucket bit here as well
	 */
	return (slot & 7) | (!!(vflags & HPTE_V_SECONDARY) << 3);
}

static DEFINE_SPINLOCK(pSeries_lpar_tlbie_lock);

static long pSeries_lpar_hpte_remove(unsigned long hpte_group)
{
	unsigned long slot_offset;
	unsigned long lpar_rc;
	int i;
	unsigned long dummy1, dummy2;

	/* pick a random slot to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {

		/* don't remove a bolted entry */
		lpar_rc = plpar_pte_remove(H_ANDCOND, hpte_group + slot_offset,
					   (0x1UL << 4), &dummy1, &dummy2);
		if (lpar_rc == H_SUCCESS)
			return i;
		BUG_ON(lpar_rc != H_NOT_FOUND);

		slot_offset++;
		slot_offset &= 0x7;
	}

	return -1;
}

static void pSeries_lpar_hptab_clear(void)
{
	unsigned long size_bytes = 1UL << ppc64_pft_size;
	unsigned long hpte_count = size_bytes >> 4;
	unsigned long dummy1, dummy2, dword0;
	long lpar_rc;
	int i;

	/* TODO: Use bulk call */
	for (i = 0; i < hpte_count; i++) {
		/* dont remove HPTEs with VRMA mappings */
		lpar_rc = plpar_pte_remove_raw(H_ANDCOND, i, HPTE_V_1TB_SEG,
						&dummy1, &dummy2);
		if (lpar_rc == H_NOT_FOUND) {
			lpar_rc = plpar_pte_read_raw(0, i, &dword0, &dummy1);
			if (!lpar_rc && ((dword0 & HPTE_V_VRMA_MASK)
				!= HPTE_V_VRMA_MASK))
				/* Can be hpte for 1TB Seg. So remove it */
				plpar_pte_remove_raw(0, i, 0, &dummy1, &dummy2);
		}
	}
}

/*
 * This computes the AVPN and B fields of the first dword of a HPTE,
 * for use when we want to match an existing PTE.  The bottom 7 bits
 * of the returned value are zero.
 */
static inline unsigned long hpte_encode_avpn(unsigned long va, int psize,
					     int ssize)
{
	unsigned long v;

	v = (va >> 23) & ~(mmu_psize_defs[psize].avpnm);
	v <<= HPTE_V_AVPN_SHIFT;
	v |= ((unsigned long) ssize) << HPTE_V_SSIZE_SHIFT;
	return v;
}

/*
 * NOTE: for updatepp ops we are fortunate that the linux "newpp" bits and
 * the low 3 bits of flags happen to line up.  So no transform is needed.
 * We can probably optimize here and assume the high bits of newpp are
 * already zero.  For now I am paranoid.
 */
static long pSeries_lpar_hpte_updatepp(unsigned long slot,
				       unsigned long newpp,
				       unsigned long va,
				       int psize, int ssize, int local)
{
	unsigned long lpar_rc;
	unsigned long flags = (newpp & 7) | H_AVPN;
	unsigned long want_v;

	want_v = hpte_encode_avpn(va, psize, ssize);

	pr_devel("    update: avpnv=%016lx, hash=%016lx, f=%lx, psize: %d ...",
		 want_v, slot, flags, psize);

	lpar_rc = plpar_pte_protect(flags, slot, want_v);

	if (lpar_rc == H_NOT_FOUND) {
		pr_devel("not found !\n");
		return -1;
	}

	pr_devel("ok\n");

	BUG_ON(lpar_rc != H_SUCCESS);

	return 0;
}

static unsigned long pSeries_lpar_hpte_getword0(unsigned long slot)
{
	unsigned long dword0;
	unsigned long lpar_rc;
	unsigned long dummy_word1;
	unsigned long flags;

	/* Read 1 pte at a time                        */
	/* Do not need RPN to logical page translation */
	/* No cross CEC PFT access                     */
	flags = 0;

	lpar_rc = plpar_pte_read(flags, slot, &dword0, &dummy_word1);

	BUG_ON(lpar_rc != H_SUCCESS);

	return dword0;
}

static long pSeries_lpar_hpte_find(unsigned long va, int psize, int ssize)
{
	unsigned long hash;
	unsigned long i;
	long slot;
	unsigned long want_v, hpte_v;

	hash = hpt_hash(va, mmu_psize_defs[psize].shift, ssize);
	want_v = hpte_encode_avpn(va, psize, ssize);

	/* Bolted entries are always in the primary group */
	slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	for (i = 0; i < HPTES_PER_GROUP; i++) {
		hpte_v = pSeries_lpar_hpte_getword0(slot);

		if (HPTE_V_COMPARE(hpte_v, want_v) && (hpte_v & HPTE_V_VALID))
			/* HPTE matches */
			return slot;
		++slot;
	}

	return -1;
} 

static void pSeries_lpar_hpte_updateboltedpp(unsigned long newpp,
					     unsigned long ea,
					     int psize, int ssize)
{
	unsigned long lpar_rc, slot, vsid, va, flags;

	vsid = get_kernel_vsid(ea, ssize);
	va = hpt_va(ea, vsid, ssize);

	slot = pSeries_lpar_hpte_find(va, psize, ssize);
	BUG_ON(slot == -1);

	flags = newpp & 7;
	lpar_rc = plpar_pte_protect(flags, slot, 0);

	BUG_ON(lpar_rc != H_SUCCESS);
}

static void pSeries_lpar_hpte_invalidate(unsigned long slot, unsigned long va,
					 int psize, int ssize, int local)
{
	unsigned long want_v;
	unsigned long lpar_rc;
	unsigned long dummy1, dummy2;

	pr_devel("    inval : slot=%lx, va=%016lx, psize: %d, local: %d\n",
		 slot, va, psize, local);

	want_v = hpte_encode_avpn(va, psize, ssize);
	lpar_rc = plpar_pte_remove(H_AVPN, slot, want_v, &dummy1, &dummy2);
	if (lpar_rc == H_NOT_FOUND)
		return;

	BUG_ON(lpar_rc != H_SUCCESS);
}

static void pSeries_lpar_hpte_removebolted(unsigned long ea,
					   int psize, int ssize)
{
	unsigned long slot, vsid, va;

	vsid = get_kernel_vsid(ea, ssize);
	va = hpt_va(ea, vsid, ssize);

	slot = pSeries_lpar_hpte_find(va, psize, ssize);
	BUG_ON(slot == -1);

	pSeries_lpar_hpte_invalidate(slot, va, psize, ssize, 0);
}

/* Flag bits for H_BULK_REMOVE */
#define HBR_REQUEST	0x4000000000000000UL
#define HBR_RESPONSE	0x8000000000000000UL
#define HBR_END		0xc000000000000000UL
#define HBR_AVPN	0x0200000000000000UL
#define HBR_ANDCOND	0x0100000000000000UL

/*
 * Take a spinlock around flushes to avoid bouncing the hypervisor tlbie
 * lock.
 */
static void pSeries_lpar_flush_hash_range(unsigned long number, int local)
{
	unsigned long i, pix, rc;
	unsigned long flags = 0;
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);
	int lock_tlbie = !cpu_has_feature(CPU_FTR_LOCKLESS_TLBIE);
	unsigned long param[9];
	unsigned long va;
	unsigned long hash, index, shift, hidx, slot;
	real_pte_t pte;
	int psize, ssize;

	if (lock_tlbie)
		spin_lock_irqsave(&pSeries_lpar_tlbie_lock, flags);

	psize = batch->psize;
	ssize = batch->ssize;
	pix = 0;
	for (i = 0; i < number; i++) {
		va = batch->vaddr[i];
		pte = batch->pte[i];
		pte_iterate_hashed_subpages(pte, psize, va, index, shift) {
			hash = hpt_hash(va, shift, ssize);
			hidx = __rpte_to_hidx(pte, index);
			if (hidx & _PTEIDX_SECONDARY)
				hash = ~hash;
			slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
			slot += hidx & _PTEIDX_GROUP_IX;
			if (!firmware_has_feature(FW_FEATURE_BULK_REMOVE)) {
				pSeries_lpar_hpte_invalidate(slot, va, psize,
							     ssize, local);
			} else {
				param[pix] = HBR_REQUEST | HBR_AVPN | slot;
				param[pix+1] = hpte_encode_avpn(va, psize,
								ssize);
				pix += 2;
				if (pix == 8) {
					rc = plpar_hcall9(H_BULK_REMOVE, param,
						param[0], param[1], param[2],
						param[3], param[4], param[5],
						param[6], param[7]);
					BUG_ON(rc != H_SUCCESS);
					pix = 0;
				}
			}
		} pte_iterate_hashed_end();
	}
	if (pix) {
		param[pix] = HBR_END;
		rc = plpar_hcall9(H_BULK_REMOVE, param, param[0], param[1],
				  param[2], param[3], param[4], param[5],
				  param[6], param[7]);
		BUG_ON(rc != H_SUCCESS);
	}

	if (lock_tlbie)
		spin_unlock_irqrestore(&pSeries_lpar_tlbie_lock, flags);
}

void __init hpte_init_lpar(void)
{
	ppc_md.hpte_invalidate	= pSeries_lpar_hpte_invalidate;
	ppc_md.hpte_updatepp	= pSeries_lpar_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = pSeries_lpar_hpte_updateboltedpp;
	ppc_md.hpte_insert	= pSeries_lpar_hpte_insert;
	ppc_md.hpte_remove	= pSeries_lpar_hpte_remove;
	ppc_md.hpte_removebolted = pSeries_lpar_hpte_removebolted;
	ppc_md.flush_hash_range	= pSeries_lpar_flush_hash_range;
	ppc_md.hpte_clear_all   = pSeries_lpar_hptab_clear;
}

#ifdef CONFIG_PPC_SMLPAR
#define CMO_FREE_HINT_DEFAULT 1
static int cmo_free_hint_flag = CMO_FREE_HINT_DEFAULT;

static int __init cmo_free_hint(char *str)
{
	char *parm;
	parm = strstrip(str);

	if (strcasecmp(parm, "no") == 0 || strcasecmp(parm, "off") == 0) {
		printk(KERN_INFO "cmo_free_hint: CMO free page hinting is not active.\n");
		cmo_free_hint_flag = 0;
		return 1;
	}

	cmo_free_hint_flag = 1;
	printk(KERN_INFO "cmo_free_hint: CMO free page hinting is active.\n");

	if (strcasecmp(parm, "yes") == 0 || strcasecmp(parm, "on") == 0)
		return 1;

	return 0;
}

__setup("cmo_free_hint=", cmo_free_hint);

static void pSeries_set_page_state(struct page *page, int order,
				   unsigned long state)
{
	int i, j;
	unsigned long cmo_page_sz, addr;

	cmo_page_sz = cmo_get_page_size();
	addr = __pa((unsigned long)page_address(page));

	for (i = 0; i < (1 << order); i++, addr += PAGE_SIZE) {
		for (j = 0; j < PAGE_SIZE; j += cmo_page_sz)
			plpar_hcall_norets(H_PAGE_INIT, state, addr + j, 0);
	}
}

void arch_free_page(struct page *page, int order)
{
	if (!cmo_free_hint_flag || !firmware_has_feature(FW_FEATURE_CMO))
		return;

	pSeries_set_page_state(page, order, H_PAGE_SET_UNUSED);
}
EXPORT_SYMBOL(arch_free_page);

#endif
