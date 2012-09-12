/*
 *    Copyright IBM Corp. 2002, 2007
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Arnd Bergmann <arndb@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef _ASM_S390_AIRQ_H
#define _ASM_S390_AIRQ_H

typedef void (*adapter_int_handler_t)(void *, void *);

void *s390_register_adapter_interrupt(adapter_int_handler_t, void *, u8);
void s390_unregister_adapter_interrupt(void *, u8);

#endif /* _ASM_S390_AIRQ_H */
