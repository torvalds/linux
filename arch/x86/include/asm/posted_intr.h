/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_POSTED_INTR_H
#define _X86_POSTED_INTR_H

#define POSTED_INTR_ON  0
#define POSTED_INTR_SN  1

#define PID_TABLE_ENTRY_VALID 1

/* Posted-Interrupt Descriptor */
struct pi_desc {
	union {
		u32 pir[8];     /* Posted interrupt requested */
		u64 pir64[4];
	};
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
	return test_and_set_bit(vector, (unsigned long *)pi_desc->pir);
}

static inline bool pi_is_pir_empty(struct pi_desc *pi_desc)
{
	return bitmap_empty((unsigned long *)pi_desc->pir, NR_VECTORS);
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

/* Non-atomic helpers */
static inline void __pi_set_sn(struct pi_desc *pi_desc)
{
	pi_desc->notifications |= BIT(POSTED_INTR_SN);
}

static inline void __pi_clear_sn(struct pi_desc *pi_desc)
{
	pi_desc->notifications &= ~BIT(POSTED_INTR_SN);
}

#endif /* _X86_POSTED_INTR_H */
