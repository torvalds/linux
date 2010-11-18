#ifndef _S390_EXTINT_H
#define _S390_EXTINT_H

/*
 *  include/asm-s390/s390_ext.h
 *
 *  S390 version
 *    Copyright IBM Corp. 1999,2007
 *    Author(s): Holger Smolinski (Holger.Smolinski@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/types.h>

typedef void (*ext_int_handler_t)(unsigned int, unsigned int, unsigned long);

typedef struct ext_int_info_t {
	struct ext_int_info_t *next;
	ext_int_handler_t handler;
	__u16 code;
} ext_int_info_t;

extern ext_int_info_t *ext_int_hash[];

int register_external_interrupt(__u16 code, ext_int_handler_t handler);
int register_early_external_interrupt(__u16 code, ext_int_handler_t handler,
				      ext_int_info_t *info);
int unregister_external_interrupt(__u16 code, ext_int_handler_t handler);
int unregister_early_external_interrupt(__u16 code, ext_int_handler_t handler,
					ext_int_info_t *info);

#endif
