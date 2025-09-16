/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_POSTED_INTR_H
#define _X86_POSTED_INTR_H

#include <asm/cmpxchg.h>
#include <asm/rwonce.h>
#include <asm/irq_vectors.h>

#include <linux/bitmap.h>

#define POSTED_INTR_ON  0
#define POSTED_INTR_SN  1

#define PID_TABLE_ENTRY_VALID 1

#define NR_PIR_VECTORS	256
#define NR_PIR_WORDS	(NR_PIR_VECTORS / BITS_PER_LONG)

/* Posted-Interrupt Descriptor */
struct pi_desc {
	unsigned long pir[NR_PIR_WORDS];     /* Posted interrupt requested */
	union {
		struct {
			u16	notifications; /* Suppress and outstanding bits */
			u8	nv;
			u8	rsvd_2;
			u32	ndst;
		};
		u64 control;
	};
	u32 rsvd[6];
} __aligned(64);

/*
 * De-multiplexing posted interrupts is on the performance path, the code
 * below is written to optimize the cache performance based on the following
 * considerations:
 * 1.Posted interrupt descriptor (PID) fits in a cache line that is frequently
 *   accessed by both CPU and IOMMU.
 * 2.During software processing of posted interrupts, the CPU needs to do
 *   natural width read and xchg for checking and clearing posted interrupt
 *   request (PIR), a 256 bit field within the PID.
 * 3.On the other side, the IOMMU does atomic swaps of the entire PID cache
 *   line when posting interrupts and setting control bits.
 * 4.The CPU can access the cache line a magnitude faster than the IOMMU.
 * 5.Each time the IOMMU does interrupt posting to the PIR will evict the PID
 *   cache line. The cache line states after each operation are as follows,
 *   assuming a 64-bit kernel:
 *   CPU		IOMMU			PID Cache line state
 *   ---------------------------------------------------------------
 *...read64					exclusive
 *...lock xchg64				modified
 *...			post/atomic swap	invalid
 *...-------------------------------------------------------------
 *
 * To reduce L1 data cache miss, it is important to avoid contention with
 * IOMMU's interrupt posting/atomic swap. Therefore, a copy of PIR is used
 * when processing posted interrupts in software, e.g. to dispatch interrupt
 * handlers for posted MSIs, or to move interrupts from the PIR to the vIRR
 * in KVM.
 *
 * In addition, the code is trying to keep the cache line state consistent
 * as much as possible. e.g. when making a copy and clearing the PIR
 * (assuming non-zero PIR bits are present in the entire PIR), it does:
 *		read, read, read, read, xchg, xchg, xchg, xchg
 * instead of:
 *		read, xchg, read, xchg, read, xchg, read, xchg
 */
static __always_inline bool pi_harvest_pir(unsigned long *pir,
					   unsigned long *pir_vals)
{
	unsigned long pending = 0;
	int i;

	for (i = 0; i < NR_PIR_WORDS; i++) {
		pir_vals[i] = READ_ONCE(pir[i]);
		pending |= pir_vals[i];
	}

	if (!pending)
		return false;

	for (i = 0; i < NR_PIR_WORDS; i++) {
		if (!pir_vals[i])
			continue;

		pir_vals[i] = arch_xchg(&pir[i], 0);
	}

	return true;
}

static inline bool pi_test_and_set_on(struct pi_desc *pi_desc)
{
	return test_and_set_bit(POSTED_INTR_ON, (unsigned long *)&pi_desc->control);
}

static inline bool pi_test_and_clear_on(struct pi_desc *pi_desc)
{
	return test_and_clear_bit(POSTED_INTR_ON, (unsigned long *)&pi_desc->control);
}

static inline bool pi_test_and_clear_sn(struct pi_desc *pi_desc)
{
	return test_and_clear_bit(POSTED_INTR_SN, (unsigned long *)&pi_desc->control);
}

static inline bool pi_test_and_set_pir(int vector, struct pi_desc *pi_desc)
{
	return test_and_set_bit(vector, pi_desc->pir);
}

static inline bool pi_is_pir_empty(struct pi_desc *pi_desc)
{
	return bitmap_empty(pi_desc->pir, NR_VECTORS);
}

static inline void pi_set_sn(struct pi_desc *pi_desc)
{
	set_bit(POSTED_INTR_SN, (unsigned long *)&pi_desc->control);
}

static inline void pi_set_on(struct pi_desc *pi_desc)
{
	set_bit(POSTED_INTR_ON, (unsigned long *)&pi_desc->control);
}

static inline void pi_clear_on(struct pi_desc *pi_desc)
{
	clear_bit(POSTED_INTR_ON, (unsigned long *)&pi_desc->control);
}

static inline void pi_clear_sn(struct pi_desc *pi_desc)
{
	clear_bit(POSTED_INTR_SN, (unsigned long *)&pi_desc->control);
}

static inline bool pi_test_on(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_ON, (unsigned long *)&pi_desc->control);
}

static inline bool pi_test_sn(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_SN, (unsigned long *)&pi_desc->control);
}

static inline bool pi_test_pir(int vector, struct pi_desc *pi_desc)
{
	return test_bit(vector, (unsigned long *)pi_desc->pir);
}

/* Non-atomic helpers */
static inline void __pi_set_sn(struct pi_desc *pi_desc)
{
	pi_desc->notifications |= BIT(POSTED_INTR_SN);
}

static inline void __pi_clear_sn(struct pi_desc *pi_desc)
{
	pi_desc->notifications &= ~BIT(POSTED_INTR_SN);
}

#ifdef CONFIG_X86_POSTED_MSI
/*
 * Not all external vectors are subject to interrupt remapping, e.g. IOMMU's
 * own interrupts. Here we do not distinguish them since those vector bits in
 * PIR will always be zero.
 */
static inline bool pi_pending_this_cpu(unsigned int vector)
{
	struct pi_desc *pid = this_cpu_ptr(&posted_msi_pi_desc);

	if (WARN_ON_ONCE(vector > NR_VECTORS || vector < FIRST_EXTERNAL_VECTOR))
		return false;

	return test_bit(vector, pid->pir);
}

extern void intel_posted_msi_init(void);
#else
static inline bool pi_pending_this_cpu(unsigned int vector) { return false; }

static inline void intel_posted_msi_init(void) {};
#endif /* X86_POSTED_MSI */

#endif /* _X86_POSTED_INTR_H */
