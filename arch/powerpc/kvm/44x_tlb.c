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
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <asm/mmu-44x.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_44x.h>

#include "44x_tlb.h"

#ifndef PPC44x_TLBE_SIZE
#define PPC44x_TLBE_SIZE	PPC44x_TLB_4K
#endif

#define PAGE_SIZE_4K (1<<12)
#define PAGE_MASK_4K (~(PAGE_SIZE_4K - 1))

#define PPC44x_TLB_UATTR_MASK \
	(PPC44x_TLB_U0|PPC44x_TLB_U1|PPC44x_TLB_U2|PPC44x_TLB_U3)
#define PPC44x_TLB_USER_PERM_MASK (PPC44x_TLB_UX|PPC44x_TLB_UR|PPC44x_TLB_UW)
#define PPC44x_TLB_SUPER_PERM_MASK (PPC44x_TLB_SX|PPC44x_TLB_SR|PPC44x_TLB_SW)

static unsigned int kvmppc_tlb_44x_pos;

#ifdef DEBUG
void kvmppc_dump_tlbs(struct kvm_vcpu *vcpu)
{
	struct kvmppc_44x_tlbe *tlbe;
	int i;

	printk("vcpu %d TLB dump:\n", vcpu->vcpu_id);
	printk("| %2s | %3s | %8s | %8s | %8s |\n",
			"nr", "tid", "word0", "word1", "word2");

	for (i = 0; i < PPC44x_TLB_SIZE; i++) {
		tlbe = &vcpu_44x->guest_tlb[i];
		if (tlbe->word0 & PPC44x_TLB_VALID)
			printk(" G%2d |  %02X | %08X | %08X | %08X |\n",
			       i, tlbe->tid, tlbe->word0, tlbe->word1,
			       tlbe->word2);
	}

	for (i = 0; i < PPC44x_TLB_SIZE; i++) {
		tlbe = &vcpu_44x->shadow_tlb[i];
		if (tlbe->word0 & PPC44x_TLB_VALID)
			printk(" S%2d | %02X | %08X | %08X | %08X |\n",
			       i, tlbe->tid, tlbe->word0, tlbe->word1,
			       tlbe->word2);
	}
}
#endif

static u32 kvmppc_44x_tlb_shadow_attrib(u32 attrib, int usermode)
{
	/* We only care about the guest's permission and user bits. */
	attrib &= PPC44x_TLB_PERM_MASK|PPC44x_TLB_UATTR_MASK;

	if (!usermode) {
		/* Guest is in supervisor mode, so we need to translate guest
		 * supervisor permissions into user permissions. */
		attrib &= ~PPC44x_TLB_USER_PERM_MASK;
		attrib |= (attrib & PPC44x_TLB_SUPER_PERM_MASK) << 3;
	}

	/* Make sure host can always access this memory. */
	attrib |= PPC44x_TLB_SX|PPC44x_TLB_SR|PPC44x_TLB_SW;

	/* WIMGE = 0b00100 */
	attrib |= PPC44x_TLB_M;

	return attrib;
}

/* Search the guest TLB for a matching entry. */
int kvmppc_44x_tlb_index(struct kvm_vcpu *vcpu, gva_t eaddr, unsigned int pid,
                         unsigned int as)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	int i;

	/* XXX Replace loop with fancy data structures. */
	for (i = 0; i < PPC44x_TLB_SIZE; i++) {
		struct kvmppc_44x_tlbe *tlbe = &vcpu_44x->guest_tlb[i];
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

struct kvmppc_44x_tlbe *kvmppc_44x_itlb_search(struct kvm_vcpu *vcpu,
                                               gva_t eaddr)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	unsigned int as = !!(vcpu->arch.msr & MSR_IS);
	unsigned int index;

	index = kvmppc_44x_tlb_index(vcpu, eaddr, vcpu->arch.pid, as);
	if (index == -1)
		return NULL;
	return &vcpu_44x->guest_tlb[index];
}

struct kvmppc_44x_tlbe *kvmppc_44x_dtlb_search(struct kvm_vcpu *vcpu,
                                               gva_t eaddr)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	unsigned int as = !!(vcpu->arch.msr & MSR_DS);
	unsigned int index;

	index = kvmppc_44x_tlb_index(vcpu, eaddr, vcpu->arch.pid, as);
	if (index == -1)
		return NULL;
	return &vcpu_44x->guest_tlb[index];
}

static int kvmppc_44x_tlbe_is_writable(struct kvmppc_44x_tlbe *tlbe)
{
	return tlbe->word2 & (PPC44x_TLB_SW|PPC44x_TLB_UW);
}

static void kvmppc_44x_shadow_release(struct kvm_vcpu *vcpu,
                                      unsigned int index)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	struct kvmppc_44x_tlbe *stlbe = &vcpu_44x->shadow_tlb[index];
	struct page *page = vcpu_44x->shadow_pages[index];

	if (get_tlb_v(stlbe)) {
		if (kvmppc_44x_tlbe_is_writable(stlbe))
			kvm_release_page_dirty(page);
		else
			kvm_release_page_clean(page);
	}
}

void kvmppc_core_destroy_mmu(struct kvm_vcpu *vcpu)
{
	int i;

	for (i = 0; i <= tlb_44x_hwater; i++)
		kvmppc_44x_shadow_release(vcpu, i);
}

void kvmppc_tlbe_set_modified(struct kvm_vcpu *vcpu, unsigned int i)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);

	vcpu_44x->shadow_tlb_mod[i] = 1;
}

/**
 * kvmppc_mmu_map -- create a host mapping for guest memory
 *
 * If the guest wanted a larger page than the host supports, only the first
 * host page is mapped here and the rest are demand faulted.
 *
 * If the guest wanted a smaller page than the host page size, we map only the
 * guest-size page (i.e. not a full host page mapping).
 *
 * Caller must ensure that the specified guest TLB entry is safe to insert into
 * the shadow TLB.
 */
void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 gvaddr, gpa_t gpaddr, u64 asid,
                    u32 flags, u32 max_bytes)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	struct page *new_page;
	struct kvmppc_44x_tlbe *stlbe;
	hpa_t hpaddr;
	gfn_t gfn;
	unsigned int victim;

	/* Future optimization: don't overwrite the TLB entry containing the
	 * current PC (or stack?). */
	victim = kvmppc_tlb_44x_pos++;
	if (kvmppc_tlb_44x_pos > tlb_44x_hwater)
		kvmppc_tlb_44x_pos = 0;
	stlbe = &vcpu_44x->shadow_tlb[victim];

	/* Get reference to new page. */
	gfn = gpaddr >> PAGE_SHIFT;
	new_page = gfn_to_page(vcpu->kvm, gfn);
	if (is_error_page(new_page)) {
		printk(KERN_ERR "Couldn't get guest page for gfn %lx!\n", gfn);
		kvm_release_page_clean(new_page);
		return;
	}
	hpaddr = page_to_phys(new_page);

	/* Drop reference to old page. */
	kvmppc_44x_shadow_release(vcpu, victim);

	vcpu_44x->shadow_pages[victim] = new_page;

	/* XXX Make sure (va, size) doesn't overlap any other
	 * entries. 440x6 user manual says the result would be
	 * "undefined." */

	/* XXX what about AS? */

	stlbe->tid = !(asid & 0xff);

	/* Force TS=1 for all guest mappings. */
	stlbe->word0 = PPC44x_TLB_VALID | PPC44x_TLB_TS;

	if (max_bytes >= PAGE_SIZE) {
		/* Guest mapping is larger than or equal to host page size. We can use
		 * a "native" host mapping. */
		stlbe->word0 |= (gvaddr & PAGE_MASK) | PPC44x_TLBE_SIZE;
	} else {
		/* Guest mapping is smaller than host page size. We must restrict the
		 * size of the mapping to be at most the smaller of the two, but for
		 * simplicity we fall back to a 4K mapping (this is probably what the
		 * guest is using anyways). */
		stlbe->word0 |= (gvaddr & PAGE_MASK_4K) | PPC44x_TLB_4K;

		/* 'hpaddr' is a host page, which is larger than the mapping we're
		 * inserting here. To compensate, we must add the in-page offset to the
		 * sub-page. */
		hpaddr |= gpaddr & (PAGE_MASK ^ PAGE_MASK_4K);
	}

	stlbe->word1 = (hpaddr & 0xfffffc00) | ((hpaddr >> 32) & 0xf);
	stlbe->word2 = kvmppc_44x_tlb_shadow_attrib(flags,
	                                            vcpu->arch.msr & MSR_PR);
	kvmppc_tlbe_set_modified(vcpu, victim);

	KVMTRACE_5D(STLB_WRITE, vcpu, victim,
			stlbe->tid, stlbe->word0, stlbe->word1, stlbe->word2,
			handler);
}

static void kvmppc_mmu_invalidate(struct kvm_vcpu *vcpu, gva_t eaddr,
                                  gva_t eend, u32 asid)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	unsigned int pid = !(asid & 0xff);
	int i;

	/* XXX Replace loop with fancy data structures. */
	for (i = 0; i <= tlb_44x_hwater; i++) {
		struct kvmppc_44x_tlbe *stlbe = &vcpu_44x->shadow_tlb[i];
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
		kvmppc_tlbe_set_modified(vcpu, i);
		KVMTRACE_5D(STLB_INVAL, vcpu, i,
				stlbe->tid, stlbe->word0, stlbe->word1,
				stlbe->word2, handler);
	}
}

void kvmppc_mmu_priv_switch(struct kvm_vcpu *vcpu, int usermode)
{
	vcpu->arch.shadow_pid = !usermode;
}

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 new_pid)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	int i;

	if (unlikely(vcpu->arch.pid == new_pid))
		return;

	vcpu->arch.pid = new_pid;

	/* Guest userspace runs with TID=0 mappings and PID=0, to make sure it
	 * can't access guest kernel mappings (TID=1). When we switch to a new
	 * guest PID, which will also use host PID=0, we must discard the old guest
	 * userspace mappings. */
	for (i = 0; i < ARRAY_SIZE(vcpu_44x->shadow_tlb); i++) {
		struct kvmppc_44x_tlbe *stlbe = &vcpu_44x->shadow_tlb[i];

		if (get_tlb_tid(stlbe) == 0) {
			kvmppc_44x_shadow_release(vcpu, i);
			stlbe->word0 = 0;
			kvmppc_tlbe_set_modified(vcpu, i);
		}
	}
}

static int tlbe_is_host_safe(const struct kvm_vcpu *vcpu,
                             const struct kvmppc_44x_tlbe *tlbe)
{
	gpa_t gpa;

	if (!get_tlb_v(tlbe))
		return 0;

	/* Does it match current guest AS? */
	/* XXX what about IS != DS? */
	if (get_tlb_ts(tlbe) != !!(vcpu->arch.msr & MSR_IS))
		return 0;

	gpa = get_tlb_raddr(tlbe);
	if (!gfn_to_memslot(vcpu->kvm, gpa >> PAGE_SHIFT))
		/* Mapping is not for RAM. */
		return 0;

	return 1;
}

int kvmppc_44x_emul_tlbwe(struct kvm_vcpu *vcpu, u8 ra, u8 rs, u8 ws)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	gva_t eaddr;
	u64 asid;
	struct kvmppc_44x_tlbe *tlbe;
	unsigned int index;

	index = vcpu->arch.gpr[ra];
	if (index > PPC44x_TLB_SIZE) {
		printk("%s: index %d\n", __func__, index);
		kvmppc_dump_vcpu(vcpu);
		return EMULATE_FAIL;
	}

	tlbe = &vcpu_44x->guest_tlb[index];

	/* Invalidate shadow mappings for the about-to-be-clobbered TLBE. */
	if (tlbe->word0 & PPC44x_TLB_VALID) {
		eaddr = get_tlb_eaddr(tlbe);
		asid = (tlbe->word0 & PPC44x_TLB_TS) | tlbe->tid;
		kvmppc_mmu_invalidate(vcpu, eaddr, get_tlb_end(tlbe), asid);
	}

	switch (ws) {
	case PPC44x_TLB_PAGEID:
		tlbe->tid = get_mmucr_stid(vcpu);
		tlbe->word0 = vcpu->arch.gpr[rs];
		break;

	case PPC44x_TLB_XLAT:
		tlbe->word1 = vcpu->arch.gpr[rs];
		break;

	case PPC44x_TLB_ATTRIB:
		tlbe->word2 = vcpu->arch.gpr[rs];
		break;

	default:
		return EMULATE_FAIL;
	}

	if (tlbe_is_host_safe(vcpu, tlbe)) {
		gpa_t gpaddr;
		u32 flags;
		u32 bytes;

		eaddr = get_tlb_eaddr(tlbe);
		gpaddr = get_tlb_raddr(tlbe);

		/* Use the advertised page size to mask effective and real addrs. */
		bytes = get_tlb_bytes(tlbe);
		eaddr &= ~(bytes - 1);
		gpaddr &= ~(bytes - 1);

		asid = (tlbe->word0 & PPC44x_TLB_TS) | tlbe->tid;
		flags = tlbe->word2 & 0xffff;

		kvmppc_mmu_map(vcpu, eaddr, gpaddr, asid, flags, bytes);
	}

	KVMTRACE_5D(GTLB_WRITE, vcpu, index,
	            tlbe->tid, tlbe->word0, tlbe->word1, tlbe->word2,
	            handler);

	return EMULATE_DONE;
}

int kvmppc_44x_emul_tlbsx(struct kvm_vcpu *vcpu, u8 rt, u8 ra, u8 rb, u8 rc)
{
	u32 ea;
	int index;
	unsigned int as = get_mmucr_sts(vcpu);
	unsigned int pid = get_mmucr_stid(vcpu);

	ea = vcpu->arch.gpr[rb];
	if (ra)
		ea += vcpu->arch.gpr[ra];

	index = kvmppc_44x_tlb_index(vcpu, ea, pid, as);
	if (rc) {
		if (index < 0)
			vcpu->arch.cr &= ~0x20000000;
		else
			vcpu->arch.cr |= 0x20000000;
	}
	vcpu->arch.gpr[rt] = index;

	return EMULATE_DONE;
}
