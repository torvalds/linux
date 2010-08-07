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

#include <asm/tlbflush.h>
#include <asm/mmu-44x.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_44x.h>
#include "timing.h"

#include "44x_tlb.h"
#include "trace.h"

#ifndef PPC44x_TLBE_SIZE
#define PPC44x_TLBE_SIZE	PPC44x_TLB_4K
#endif

#define PAGE_SIZE_4K (1<<12)
#define PAGE_MASK_4K (~(PAGE_SIZE_4K - 1))

#define PPC44x_TLB_UATTR_MASK \
	(PPC44x_TLB_U0|PPC44x_TLB_U1|PPC44x_TLB_U2|PPC44x_TLB_U3)
#define PPC44x_TLB_USER_PERM_MASK (PPC44x_TLB_UX|PPC44x_TLB_UR|PPC44x_TLB_UW)
#define PPC44x_TLB_SUPER_PERM_MASK (PPC44x_TLB_SX|PPC44x_TLB_SR|PPC44x_TLB_SW)

#ifdef DEBUG
void kvmppc_dump_tlbs(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	struct kvmppc_44x_tlbe *tlbe;
	int i;

	printk("vcpu %d TLB dump:\n", vcpu->vcpu_id);
	printk("| %2s | %3s | %8s | %8s | %8s |\n",
			"nr", "tid", "word0", "word1", "word2");

	for (i = 0; i < ARRAY_SIZE(vcpu_44x->guest_tlb); i++) {
		tlbe = &vcpu_44x->guest_tlb[i];
		if (tlbe->word0 & PPC44x_TLB_VALID)
			printk(" G%2d |  %02X | %08X | %08X | %08X |\n",
			       i, tlbe->tid, tlbe->word0, tlbe->word1,
			       tlbe->word2);
	}
}
#endif

static inline void kvmppc_44x_tlbie(unsigned int index)
{
	/* 0 <= index < 64, so the V bit is clear and we can use the index as
	 * word0. */
	asm volatile(
		"tlbwe %[index], %[index], 0\n"
	:
	: [index] "r"(index)
	);
}

static inline void kvmppc_44x_tlbre(unsigned int index,
                                    struct kvmppc_44x_tlbe *tlbe)
{
	asm volatile(
		"tlbre %[word0], %[index], 0\n"
		"mfspr %[tid], %[sprn_mmucr]\n"
		"andi. %[tid], %[tid], 0xff\n"
		"tlbre %[word1], %[index], 1\n"
		"tlbre %[word2], %[index], 2\n"
		: [word0] "=r"(tlbe->word0),
		  [word1] "=r"(tlbe->word1),
		  [word2] "=r"(tlbe->word2),
		  [tid]   "=r"(tlbe->tid)
		: [index] "r"(index),
		  [sprn_mmucr] "i"(SPRN_MMUCR)
		: "cc"
	);
}

static inline void kvmppc_44x_tlbwe(unsigned int index,
                                    struct kvmppc_44x_tlbe *stlbe)
{
	unsigned long tmp;

	asm volatile(
		"mfspr %[tmp], %[sprn_mmucr]\n"
		"rlwimi %[tmp], %[tid], 0, 0xff\n"
		"mtspr %[sprn_mmucr], %[tmp]\n"
		"tlbwe %[word0], %[index], 0\n"
		"tlbwe %[word1], %[index], 1\n"
		"tlbwe %[word2], %[index], 2\n"
		: [tmp]   "=&r"(tmp)
		: [word0] "r"(stlbe->word0),
		  [word1] "r"(stlbe->word1),
		  [word2] "r"(stlbe->word2),
		  [tid]   "r"(stlbe->tid),
		  [index] "r"(index),
		  [sprn_mmucr] "i"(SPRN_MMUCR)
	);
}

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

/* Load shadow TLB back into hardware. */
void kvmppc_44x_tlb_load(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	int i;

	for (i = 0; i <= tlb_44x_hwater; i++) {
		struct kvmppc_44x_tlbe *stlbe = &vcpu_44x->shadow_tlb[i];

		if (get_tlb_v(stlbe) && get_tlb_ts(stlbe))
			kvmppc_44x_tlbwe(i, stlbe);
	}
}

static void kvmppc_44x_tlbe_set_modified(struct kvmppc_vcpu_44x *vcpu_44x,
                                         unsigned int i)
{
	vcpu_44x->shadow_tlb_mod[i] = 1;
}

/* Save hardware TLB to the vcpu, and invalidate all guest mappings. */
void kvmppc_44x_tlb_put(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	int i;

	for (i = 0; i <= tlb_44x_hwater; i++) {
		struct kvmppc_44x_tlbe *stlbe = &vcpu_44x->shadow_tlb[i];

		if (vcpu_44x->shadow_tlb_mod[i])
			kvmppc_44x_tlbre(i, stlbe);

		if (get_tlb_v(stlbe) && get_tlb_ts(stlbe))
			kvmppc_44x_tlbie(i);
	}
}


/* Search the guest TLB for a matching entry. */
int kvmppc_44x_tlb_index(struct kvm_vcpu *vcpu, gva_t eaddr, unsigned int pid,
                         unsigned int as)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	int i;

	/* XXX Replace loop with fancy data structures. */
	for (i = 0; i < ARRAY_SIZE(vcpu_44x->guest_tlb); i++) {
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

gpa_t kvmppc_mmu_xlate(struct kvm_vcpu *vcpu, unsigned int gtlb_index,
                       gva_t eaddr)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	struct kvmppc_44x_tlbe *gtlbe = &vcpu_44x->guest_tlb[gtlb_index];
	unsigned int pgmask = get_tlb_bytes(gtlbe) - 1;

	return get_tlb_raddr(gtlbe) | (eaddr & pgmask);
}

int kvmppc_mmu_itlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_IS);

	return kvmppc_44x_tlb_index(vcpu, eaddr, vcpu->arch.pid, as);
}

int kvmppc_mmu_dtlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_DS);

	return kvmppc_44x_tlb_index(vcpu, eaddr, vcpu->arch.pid, as);
}

void kvmppc_mmu_itlb_miss(struct kvm_vcpu *vcpu)
{
}

void kvmppc_mmu_dtlb_miss(struct kvm_vcpu *vcpu)
{
}

static void kvmppc_44x_shadow_release(struct kvmppc_vcpu_44x *vcpu_44x,
                                      unsigned int stlb_index)
{
	struct kvmppc_44x_shadow_ref *ref = &vcpu_44x->shadow_refs[stlb_index];

	if (!ref->page)
		return;

	/* Discard from the TLB. */
	/* Note: we could actually invalidate a host mapping, if the host overwrote
	 * this TLB entry since we inserted a guest mapping. */
	kvmppc_44x_tlbie(stlb_index);

	/* Now release the page. */
	if (ref->writeable)
		kvm_release_page_dirty(ref->page);
	else
		kvm_release_page_clean(ref->page);

	ref->page = NULL;

	/* XXX set tlb_44x_index to stlb_index? */

	trace_kvm_stlb_inval(stlb_index);
}

void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	int i;

	for (i = 0; i <= tlb_44x_hwater; i++)
		kvmppc_44x_shadow_release(vcpu_44x, i);
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
void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 gvaddr, gpa_t gpaddr,
                    unsigned int gtlb_index)
{
	struct kvmppc_44x_tlbe stlbe;
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	struct kvmppc_44x_tlbe *gtlbe = &vcpu_44x->guest_tlb[gtlb_index];
	struct kvmppc_44x_shadow_ref *ref;
	struct page *new_page;
	hpa_t hpaddr;
	gfn_t gfn;
	u32 asid = gtlbe->tid;
	u32 flags = gtlbe->word2;
	u32 max_bytes = get_tlb_bytes(gtlbe);
	unsigned int victim;

	/* Select TLB entry to clobber. Indirectly guard against races with the TLB
	 * miss handler by disabling interrupts. */
	local_irq_disable();
	victim = ++tlb_44x_index;
	if (victim > tlb_44x_hwater)
		victim = 0;
	tlb_44x_index = victim;
	local_irq_enable();

	/* Get reference to new page. */
	gfn = gpaddr >> PAGE_SHIFT;
	new_page = gfn_to_page(vcpu->kvm, gfn);
	if (is_error_page(new_page)) {
		printk(KERN_ERR "Couldn't get guest page for gfn %llx!\n",
			(unsigned long long)gfn);
		kvm_release_page_clean(new_page);
		return;
	}
	hpaddr = page_to_phys(new_page);

	/* Invalidate any previous shadow mappings. */
	kvmppc_44x_shadow_release(vcpu_44x, victim);

	/* XXX Make sure (va, size) doesn't overlap any other
	 * entries. 440x6 user manual says the result would be
	 * "undefined." */

	/* XXX what about AS? */

	/* Force TS=1 for all guest mappings. */
	stlbe.word0 = PPC44x_TLB_VALID | PPC44x_TLB_TS;

	if (max_bytes >= PAGE_SIZE) {
		/* Guest mapping is larger than or equal to host page size. We can use
		 * a "native" host mapping. */
		stlbe.word0 |= (gvaddr & PAGE_MASK) | PPC44x_TLBE_SIZE;
	} else {
		/* Guest mapping is smaller than host page size. We must restrict the
		 * size of the mapping to be at most the smaller of the two, but for
		 * simplicity we fall back to a 4K mapping (this is probably what the
		 * guest is using anyways). */
		stlbe.word0 |= (gvaddr & PAGE_MASK_4K) | PPC44x_TLB_4K;

		/* 'hpaddr' is a host page, which is larger than the mapping we're
		 * inserting here. To compensate, we must add the in-page offset to the
		 * sub-page. */
		hpaddr |= gpaddr & (PAGE_MASK ^ PAGE_MASK_4K);
	}

	stlbe.word1 = (hpaddr & 0xfffffc00) | ((hpaddr >> 32) & 0xf);
	stlbe.word2 = kvmppc_44x_tlb_shadow_attrib(flags,
	                                            vcpu->arch.shared->msr & MSR_PR);
	stlbe.tid = !(asid & 0xff);

	/* Keep track of the reference so we can properly release it later. */
	ref = &vcpu_44x->shadow_refs[victim];
	ref->page = new_page;
	ref->gtlb_index = gtlb_index;
	ref->writeable = !!(stlbe.word2 & PPC44x_TLB_UW);
	ref->tid = stlbe.tid;

	/* Insert shadow mapping into hardware TLB. */
	kvmppc_44x_tlbe_set_modified(vcpu_44x, victim);
	kvmppc_44x_tlbwe(victim, &stlbe);
	trace_kvm_stlb_write(victim, stlbe.tid, stlbe.word0, stlbe.word1,
			     stlbe.word2);
}

/* For a particular guest TLB entry, invalidate the corresponding host TLB
 * mappings and release the host pages. */
static void kvmppc_44x_invalidate(struct kvm_vcpu *vcpu,
                                  unsigned int gtlb_index)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	int i;

	for (i = 0; i < ARRAY_SIZE(vcpu_44x->shadow_refs); i++) {
		struct kvmppc_44x_shadow_ref *ref = &vcpu_44x->shadow_refs[i];
		if (ref->gtlb_index == gtlb_index)
			kvmppc_44x_shadow_release(vcpu_44x, i);
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
	for (i = 0; i < ARRAY_SIZE(vcpu_44x->shadow_refs); i++) {
		struct kvmppc_44x_shadow_ref *ref = &vcpu_44x->shadow_refs[i];

		if (ref->tid == 0)
			kvmppc_44x_shadow_release(vcpu_44x, i);
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
	if (get_tlb_ts(tlbe) != !!(vcpu->arch.shared->msr & MSR_IS))
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
	struct kvmppc_44x_tlbe *tlbe;
	unsigned int gtlb_index;

	gtlb_index = kvmppc_get_gpr(vcpu, ra);
	if (gtlb_index >= KVM44x_GUEST_TLB_SIZE) {
		printk("%s: index %d\n", __func__, gtlb_index);
		kvmppc_dump_vcpu(vcpu);
		return EMULATE_FAIL;
	}

	tlbe = &vcpu_44x->guest_tlb[gtlb_index];

	/* Invalidate shadow mappings for the about-to-be-clobbered TLB entry. */
	if (tlbe->word0 & PPC44x_TLB_VALID)
		kvmppc_44x_invalidate(vcpu, gtlb_index);

	switch (ws) {
	case PPC44x_TLB_PAGEID:
		tlbe->tid = get_mmucr_stid(vcpu);
		tlbe->word0 = kvmppc_get_gpr(vcpu, rs);
		break;

	case PPC44x_TLB_XLAT:
		tlbe->word1 = kvmppc_get_gpr(vcpu, rs);
		break;

	case PPC44x_TLB_ATTRIB:
		tlbe->word2 = kvmppc_get_gpr(vcpu, rs);
		break;

	default:
		return EMULATE_FAIL;
	}

	if (tlbe_is_host_safe(vcpu, tlbe)) {
		gva_t eaddr;
		gpa_t gpaddr;
		u32 bytes;

		eaddr = get_tlb_eaddr(tlbe);
		gpaddr = get_tlb_raddr(tlbe);

		/* Use the advertised page size to mask effective and real addrs. */
		bytes = get_tlb_bytes(tlbe);
		eaddr &= ~(bytes - 1);
		gpaddr &= ~(bytes - 1);

		kvmppc_mmu_map(vcpu, eaddr, gpaddr, gtlb_index);
	}

	trace_kvm_gtlb_write(gtlb_index, tlbe->tid, tlbe->word0, tlbe->word1,
			     tlbe->word2);

	kvmppc_set_exit_type(vcpu, EMULATED_TLBWE_EXITS);
	return EMULATE_DONE;
}

int kvmppc_44x_emul_tlbsx(struct kvm_vcpu *vcpu, u8 rt, u8 ra, u8 rb, u8 rc)
{
	u32 ea;
	int gtlb_index;
	unsigned int as = get_mmucr_sts(vcpu);
	unsigned int pid = get_mmucr_stid(vcpu);

	ea = kvmppc_get_gpr(vcpu, rb);
	if (ra)
		ea += kvmppc_get_gpr(vcpu, ra);

	gtlb_index = kvmppc_44x_tlb_index(vcpu, ea, pid, as);
	if (rc) {
		u32 cr = kvmppc_get_cr(vcpu);

		if (gtlb_index < 0)
			kvmppc_set_cr(vcpu, cr & ~0x20000000);
		else
			kvmppc_set_cr(vcpu, cr | 0x20000000);
	}
	kvmppc_set_gpr(vcpu, rt, gtlb_index);

	kvmppc_set_exit_type(vcpu, EMULATED_TLBSX_EXITS);
	return EMULATE_DONE;
}
