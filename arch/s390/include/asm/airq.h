/*
 *    Copyright IBM Corp. 2002, 2007
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Arnd Bergmann <arndb@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef _ASM_S390_AIRQ_H
#define _ASM_S390_AIRQ_H

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

#endif /* _ASM_S390_AIRQ_H */
