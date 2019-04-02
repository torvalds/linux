/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *   S/390 de facility
 *
 *    Copyright IBM Corp. 1999, 2000
 */

#ifndef _UAPIDE_H
#define _UAPIDE_H

#include <linux/fs.h>

/* Note:
 * struct __de_entry must be defined outside of #ifdef __KERNEL__ 
 * in order to allow a user program to analyze the 'raw'-view.
 */

struct __de_entry{
        union {
                struct {
                        unsigned long long clock:52;
                        unsigned long long exception:1;
                        unsigned long long level:3;
                        unsigned long long cpuid:8;
                } fields;

                unsigned long long stck;
        } id;
        void* caller;
} __attribute__((packed));


#define __DE_FEATURE_VERSION      2  /* version of de feature */

#endif /* _UAPIDE_H */
