/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2002, 2007
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Arnd Bergmann <arndb@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef _ASM_S390_AIRQ_H
#define _ASM_S390_AIRQ_H

#include <linux/bit_spinlock.h>

struct airq_struct {
	struct hlist_node list;		/* Handler queueing. */
	void (*handler)(struct airq_struct *);	/* Thin-interrupt handler */
	u8 *lsi_ptr;			/* Local-Summary-Indicator pointer */
	u8 lsi_mask;			/* Local-Summary-Indicator mask */
	u8 isc;				/* Interrupt-subclass */
	u8 flags;
};

#define AIRQ_PTR_ALLOCATED	0x01

int register_adapter_interrupt(struct airq_struct *airq);
void unregister_adapter_interrupt(struct airq_struct *airq);

/* Adapter interrupt bit vector */
struct airq_iv {
	unsigned long *vector;	/* Adapter interrupt bit vector */
	unsigned long *avail;	/* Allocation bit mask for the bit vector */
	unsigned long *bitlock;	/* Lock bit mask for the bit vector */
	unsigned long *ptr;	/* Pointer associated with each bit */
	unsigned int *data;	/* 32 bit value associated with each bit */
	unsigned long bits;	/* Number of bits in the vector */
	unsigned long end;	/* Number of highest allocated bit + 1 */
	spinlock_t lock;	/* Lock to protect alloc & free */
};

#define AIRQ_IV_ALLOC	1	/* Use an allocation bit mask */
#define AIRQ_IV_BITLOCK	2	/* Allocate the lock bit mask */
#define AIRQ_IV_PTR	4	/* Allocate the ptr array */
#define AIRQ_IV_DATA	8	/* Allocate the data array */

struct airq_iv *airq_iv_create(unsigned long bits, unsigned long flags);
void airq_iv_release(struct airq_iv *iv);
unsigned long airq_iv_alloc(struct airq_iv *iv, unsigned long num);
void airq_iv_free(struct airq_iv *iv, unsigned long bit, unsigned long num);
unsigned long airq_iv_scan(struct airq_iv *iv, unsigned long start,
			   unsigned long end);

static inline unsigned long airq_iv_alloc_bit(struct airq_iv *iv)
{
	return airq_iv_alloc(iv, 1);
}

static inline void airq_iv_free_bit(struct airq_iv *iv, unsigned long bit)
{
	airq_iv_free(iv, bit, 1);
}

static inline unsigned long airq_iv_end(struct airq_iv *iv)
{
	return iv->end;
}

static inline void airq_iv_lock(struct airq_iv *iv, unsigned long bit)
{
	const unsigned long be_to_le = BITS_PER_LONG - 1;
	bit_spin_lock(bit ^ be_to_le, iv->bitlock);
}

static inline void airq_iv_unlock(struct airq_iv *iv, unsigned long bit)
{
	const unsigned long be_to_le = BITS_PER_LONG - 1;
	bit_spin_unlock(bit ^ be_to_le, iv->bitlock);
}

static inline void airq_iv_set_data(struct airq_iv *iv, unsigned long bit,
				    unsigned int data)
{
	iv->data[bit] = data;
}

static inline unsigned int airq_iv_get_data(struct airq_iv *iv,
					    unsigned long bit)
{
	return iv->data[bit];
}

static inline void airq_iv_set_ptr(struct airq_iv *iv, unsigned long bit,
				   unsigned long ptr)
{
	iv->ptr[bit] = ptr;
}

static inline unsigned long airq_iv_get_ptr(struct airq_iv *iv,
					    unsigned long bit)
{
	return iv->ptr[bit];
}

#endif /* _ASM_S390_AIRQ_H */
