/*
 *    Support for adapter interruptions
 *
 *    Copyright IBM Corp. 1999, 2007
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Arnd Bergmann <arndb@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/slab.h>

#include <asm/airq.h>
#include <asm/isc.h>

#include "cio.h"
#include "cio_debug.h"
#include "ioasm.h"

static DEFINE_SPINLOCK(airq_lists_lock);
static struct hlist_head airq_lists[MAX_ISC+1];

/**
 * register_adapter_interrupt() - register adapter interrupt handler
 * @airq: pointer to adapter interrupt descriptor
 *
 * Returns 0 on success, or -EINVAL.
 */
int register_adapter_interrupt(struct airq_struct *airq)
{
	char dbf_txt[32];

	if (!airq->handler || airq->isc > MAX_ISC)
		return -EINVAL;
	if (!airq->lsi_ptr) {
		airq->lsi_ptr = kzalloc(1, GFP_KERNEL);
		if (!airq->lsi_ptr)
			return -ENOMEM;
		airq->flags |= AIRQ_PTR_ALLOCATED;
	}
	if (!airq->lsi_mask)
		airq->lsi_mask = 0xff;
	snprintf(dbf_txt, sizeof(dbf_txt), "rairq:%p", airq);
	CIO_TRACE_EVENT(4, dbf_txt);
	isc_register(airq->isc);
	spin_lock(&airq_lists_lock);
	hlist_add_head_rcu(&airq->list, &airq_lists[airq->isc]);
	spin_unlock(&airq_lists_lock);
	return 0;
}
EXPORT_SYMBOL(register_adapter_interrupt);

/**
 * unregister_adapter_interrupt - unregister adapter interrupt handler
 * @airq: pointer to adapter interrupt descriptor
 */
void unregister_adapter_interrupt(struct airq_struct *airq)
{
	char dbf_txt[32];

	if (hlist_unhashed(&airq->list))
		return;
	snprintf(dbf_txt, sizeof(dbf_txt), "urairq:%p", airq);
	CIO_TRACE_EVENT(4, dbf_txt);
	spin_lock(&airq_lists_lock);
	hlist_del_rcu(&airq->list);
	spin_unlock(&airq_lists_lock);
	synchronize_rcu();
	isc_unregister(airq->isc);
	if (airq->flags & AIRQ_PTR_ALLOCATED) {
		kfree(airq->lsi_ptr);
		airq->lsi_ptr = NULL;
		airq->flags &= ~AIRQ_PTR_ALLOCATED;
	}
}
EXPORT_SYMBOL(unregister_adapter_interrupt);

static irqreturn_t do_airq_interrupt(int irq, void *dummy)
{
	struct tpi_info *tpi_info;
	struct airq_struct *airq;
	struct hlist_head *head;

	__this_cpu_write(s390_idle.nohz_delay, 1);
	tpi_info = (struct tpi_info *) &get_irq_regs()->int_code;
	head = &airq_lists[tpi_info->isc];
	rcu_read_lock();
	hlist_for_each_entry_rcu(airq, head, list)
		if ((*airq->lsi_ptr & airq->lsi_mask) != 0)
			airq->handler(airq);
	rcu_read_unlock();

	return IRQ_HANDLED;
}

static struct irqaction airq_interrupt = {
	.name	 = "AIO",
	.handler = do_airq_interrupt,
};

void __init init_airq_interrupts(void)
{
	irq_set_chip_and_handler(THIN_INTERRUPT,
				 &dummy_irq_chip, handle_percpu_irq);
	setup_irq(THIN_INTERRUPT, &airq_interrupt);
}

/**
 * airq_iv_create - create an interrupt vector
 * @bits: number of bits in the interrupt vector
 * @flags: allocation flags
 *
 * Returns a pointer to an interrupt vector structure
 */
struct airq_iv *airq_iv_create(unsigned long bits, unsigned long flags)
{
	struct airq_iv *iv;
	unsigned long size;

	iv = kzalloc(sizeof(*iv), GFP_KERNEL);
	if (!iv)
		goto out;
	iv->bits = bits;
	size = BITS_TO_LONGS(bits) * sizeof(unsigned long);
	iv->vector = kzalloc(size, GFP_KERNEL);
	if (!iv->vector)
		goto out_free;
	if (flags & AIRQ_IV_ALLOC) {
		iv->avail = kmalloc(size, GFP_KERNEL);
		if (!iv->avail)
			goto out_free;
		memset(iv->avail, 0xff, size);
		iv->end = 0;
	} else
		iv->end = bits;
	if (flags & AIRQ_IV_BITLOCK) {
		iv->bitlock = kzalloc(size, GFP_KERNEL);
		if (!iv->bitlock)
			goto out_free;
	}
	if (flags & AIRQ_IV_PTR) {
		size = bits * sizeof(unsigned long);
		iv->ptr = kzalloc(size, GFP_KERNEL);
		if (!iv->ptr)
			goto out_free;
	}
	if (flags & AIRQ_IV_DATA) {
		size = bits * sizeof(unsigned int);
		iv->data = kzalloc(size, GFP_KERNEL);
		if (!iv->data)
			goto out_free;
	}
	spin_lock_init(&iv->lock);
	return iv;

out_free:
	kfree(iv->ptr);
	kfree(iv->bitlock);
	kfree(iv->avail);
	kfree(iv->vector);
	kfree(iv);
out:
	return NULL;
}
EXPORT_SYMBOL(airq_iv_create);

/**
 * airq_iv_release - release an interrupt vector
 * @iv: pointer to interrupt vector structure
 */
void airq_iv_release(struct airq_iv *iv)
{
	kfree(iv->data);
	kfree(iv->ptr);
	kfree(iv->bitlock);
	kfree(iv->vector);
	kfree(iv->avail);
	kfree(iv);
}
EXPORT_SYMBOL(airq_iv_release);

/**
 * airq_iv_alloc - allocate irq bits from an interrupt vector
 * @iv: pointer to an interrupt vector structure
 * @num: number of consecutive irq bits to allocate
 *
 * Returns the bit number of the first irq in the allocated block of irqs,
 * or -1UL if no bit is available or the AIRQ_IV_ALLOC flag has not been
 * specified
 */
unsigned long airq_iv_alloc(struct airq_iv *iv, unsigned long num)
{
	unsigned long bit, i, flags;

	if (!iv->avail || num == 0)
		return -1UL;
	spin_lock_irqsave(&iv->lock, flags);
	bit = find_first_bit_inv(iv->avail, iv->bits);
	while (bit + num <= iv->bits) {
		for (i = 1; i < num; i++)
			if (!test_bit_inv(bit + i, iv->avail))
				break;
		if (i >= num) {
			/* Found a suitable block of irqs */
			for (i = 0; i < num; i++)
				clear_bit_inv(bit + i, iv->avail);
			if (bit + num >= iv->end)
				iv->end = bit + num + 1;
			break;
		}
		bit = find_next_bit_inv(iv->avail, iv->bits, bit + i + 1);
	}
	if (bit + num > iv->bits)
		bit = -1UL;
	spin_unlock_irqrestore(&iv->lock, flags);
	return bit;
}
EXPORT_SYMBOL(airq_iv_alloc);

/**
 * airq_iv_free - free irq bits of an interrupt vector
 * @iv: pointer to interrupt vector structure
 * @bit: number of the first irq bit to free
 * @num: number of consecutive irq bits to free
 */
void airq_iv_free(struct airq_iv *iv, unsigned long bit, unsigned long num)
{
	unsigned long i, flags;

	if (!iv->avail || num == 0)
		return;
	spin_lock_irqsave(&iv->lock, flags);
	for (i = 0; i < num; i++) {
		/* Clear (possibly left over) interrupt bit */
		clear_bit_inv(bit + i, iv->vector);
		/* Make the bit positions available again */
		set_bit_inv(bit + i, iv->avail);
	}
	if (bit + num >= iv->end) {
		/* Find new end of bit-field */
		while (iv->end > 0 && !test_bit_inv(iv->end - 1, iv->avail))
			iv->end--;
	}
	spin_unlock_irqrestore(&iv->lock, flags);
}
EXPORT_SYMBOL(airq_iv_free);

/**
 * airq_iv_scan - scan interrupt vector for non-zero bits
 * @iv: pointer to interrupt vector structure
 * @start: bit number to start the search
 * @end: bit number to end the search
 *
 * Returns the bit number of the next non-zero interrupt bit, or
 * -1UL if the scan completed without finding any more any non-zero bits.
 */
unsigned long airq_iv_scan(struct airq_iv *iv, unsigned long start,
			   unsigned long end)
{
	unsigned long bit;

	/* Find non-zero bit starting from 'ivs->next'. */
	bit = find_next_bit_inv(iv->vector, end, start);
	if (bit >= end)
		return -1UL;
	clear_bit_inv(bit, iv->vector);
	return bit;
}
EXPORT_SYMBOL(airq_iv_scan);
