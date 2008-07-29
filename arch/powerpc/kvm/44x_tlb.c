/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <asm/mmu-44x.h>
#include <asm/kvm_ppc.h>

#include "44x_tlb.h"

#define PPC44x_TLB_USER_PERM_MASK (PPC44x_TLB_UX|PPC44x_TLB_UR|PPC44x_TLB_UW)
#define PPC44x_TLB_SUPER_PERM_MASK (PPC44x_TLB_SX|PPC44x_TLB_SR|PPC44x_TLB_SW)

static unsigned int kvmppc_tlb_44x_pos;

static u32 kvmppc_44x_tlb_shadow_attrib(u32 attrib, int usermode)
{
	/* Mask off reserved bits. */
	attrib &= PPC44x_TLB_PERM_MASK|PPC44x_TLB_ATTR_MASK;

	if (!usermode) {
		/* Guest is in supervisor mode, so we need to translate guest
		 * supervisor permissions into user permissions. */
		attrib &= ~PPC44x_TLB_USER_PERM_MASK;
		attrib |= (attrib & PPC44x_TLB_SUPER_PERM_MASK) << 3;
	}

	/* Make sure host can always access this memory. */
	attrib |= PPC44x_TLB_SX|PPC44x_TLB_SR|PPC44x_TLB_SW;

	return attrib;
}

/* Search the guest TLB for a matching entry. */
int kvmppc_44x_tlb_index(struct kvm_vcpu *vcpu, gva_t eaddr, unsigned int pid,
                         unsigned int as)
{
	int i;

	/* XXX Replace loop with fancy data structures. */
	for (i = 0; i < PPC44x_TLB_SIZE; i++) {
		struct tlbe *tlbe = &vcpu->arch.guest_tlb[i];
		unsigned int tid;

		if (eaddr < get_tlb_eaddr(tlbe))
			continue;

		if (eaddr > get_tlb_end(tlbe))
			continue;

		tid = get_tlb_tid(tlbe);
		if (tid && (tid != pid))
			continue;

		if (!get_tlb_v(tlbe))
			continue;

		if (get_tlb_ts(tlbe) != as)
			continue;

		return i;
	}

	return -1;
}

struct tlbe *kvmppc_44x_itlb_search(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.msr & MSR_IS);
	unsigned int index;

	index = kvmppc_44x_tlb_index(vcpu, eaddr, vcpu->arch.pid, as);
	if (index == -1)
		return NULL;
	return &vcpu->arch.guest_tlb[index];
}

struct tlbe *kvmppc_44x_dtlb_search(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.msr & MSR_DS);
	unsigned int index;

	index = kvmppc_44x_tlb_index(vcpu, eaddr, vcpu->arch.pid, as);
	if (index == -1)
		return NULL;
	return &vcpu->arch.guest_tlb[index];
}

static int kvmppc_44x_tlbe_is_writable(struct tlbe *tlbe)
{
	return tlbe->word2 & (PPC44x_TLB_SW|PPC44x_TLB_UW);
}

/* Must be called with mmap_sem locked for writing. */
static void kvmppc_44x_shadow_release(struct kvm_vcpu *vcpu,
                                      unsigned int index)
{
	struct tlbe *stlbe = &vcpu->arch.shadow_tlb[index];
	struct page *page = vcpu->arch.shadow_pages[index];

	if (get_tlb_v(stlbe)) {
		if (kvmppc_44x_tlbe_is_writable(stlbe))
			kvm_release_page_dirty(page);
		else
			kvm_release_page_clean(page);
	}
}

/* Caller must ensure that the specified guest TLB entry is safe to insert into
 * the shadow TLB. */
void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 gvaddr, gfn_t gfn, u64 asid,
                    u32 flags)
{
	struct page *new_page;
	struct tlbe *stlbe;
	hpa_t hpaddr;
	unsigned int victim;

	/* Future optimization: don't overwrite the TLB entry containing the
	 * current PC (or stack?). */
	victim = kvmppc_tlb_44x_pos++;
	if (kvmppc_tlb_44x_pos > tlb_44x_hwater)
		kvmppc_tlb_44x_pos = 0;
	stlbe = &vcpu->arch.shadow_tlb[victim];

	/* Get reference to new page. */
	down_read(&current->mm->mmap_sem);
	new_page = gfn_to_page(vcpu->kvm, gfn);
	if (is_error_page(new_page)) {
		printk(KERN_ERR "Couldn't get guest page for gfn %lx!\n", gfn);
		kvm_release_page_clean(new_page);
		up_read(&current->mm->mmap_sem);
		return;
	}
	hpaddr = page_to_phys(new_page);

	/* Drop reference to old page. */
	kvmppc_44x_shadow_release(vcpu, victim);
	up_read(&current->mm->mmap_sem);

	vcpu->arch.shadow_pages[victim] = new_page;

	/* XXX Make sure (va, size) doesn't overlap any other
	 * entries. 440x6 user manual says the result would be
	 * "undefined." */

	/* XXX what about AS? */

	stlbe->tid = asid & 0xff;

	/* Force TS=1 for all guest mappings. */
	/* For now we hardcode 4KB mappings, but it will be important to
	 * use host large pages in the future. */
	stlbe->word0 = (gvaddr & PAGE_MASK) | PPC44x_TLB_VALID | PPC44x_TLB_TS
	               | PPC44x_TLB_4K;

	stlbe->word1 = (hpaddr & 0xfffffc00) | ((hpaddr >> 32) & 0xf);
	stlbe->word2 = kvmppc_44x_tlb_shadow_attrib(flags,
	                                            vcpu->arch.msr & MSR_PR);
}

void kvmppc_mmu_invalidate(struct kvm_vcpu *vcpu, gva_t eaddr,
                           gva_t eend, u32 asid)
{
	unsigned int pid = asid & 0xff;
	int i;

	/* XXX Replace loop with fancy data structures. */
	down_write(&current->mm->mmap_sem);
	for (i = 0; i <= tlb_44x_hwater; i++) {
		struct tlbe *stlbe = &vcpu->arch.shadow_tlb[i];
		unsigned int tid;

		if (!get_tlb_v(stlbe))
			continue;

		if (eend < get_tlb_eaddr(stlbe))
			continue;

		if (eaddr > get_tlb_end(stlbe))
			continue;

		tid = get_tlb_tid(stlbe);
		if (tid && (tid != pid))
			continue;

		kvmppc_44x_shadow_release(vcpu, i);
		stlbe->word0 = 0;
	}
	up_write(&current->mm->mmap_sem);
}

/* Invalidate all mappings, so that when they fault back in they will get the
 * proper permission bits. */
void kvmppc_mmu_priv_switch(struct kvm_vcpu *vcpu, int usermode)
{
	int i;

	/* XXX Replace loop with fancy data structures. */
	down_write(&current->mm->mmap_sem);
	for (i = 0; i <= tlb_44x_hwater; i++) {
		kvmppc_44x_shadow_release(vcpu, i);
		vcpu->arch.shadow_tlb[i].word0 = 0;
	}
	up_write(&current->mm->mmap_sem);
}
