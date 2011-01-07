/*
 *    Copyright IBM Corp. 1999,2010
 *    Author(s): Holger Smolinski <Holger.Smolinski@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 */

#ifndef _S390_EXTINT_H
#define _S390_EXTINT_H

#include <linux/types.h>

typedef void (*ext_int_handler_t)(unsigned int, unsigned int, unsigned long);

int register_external_interrupt(__u16 code, ext_int_handler_t handler);
int unregister_external_interrupt(__u16 code, ext_int_handler_t handler);

#endif /* _S390_EXTINT_H */
