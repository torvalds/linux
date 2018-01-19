/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *   S/390 debug facility
 *
 *    Copyright IBM Corp. 1999, 2000
 */

#ifndef _UAPIDEBUG_H
#define _UAPIDEBUG_H

#include <linux/fs.h>

/* Note:
 * struct __debug_entry must be defined outside of #ifdef __KERNEL__ 
 * in order to allow a user program to analyze the 'raw'-view.
 */

struct __debug_entry{
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


#define __DEBUG_FEATURE_VERSION      2  /* version of debug feature */

#endif /* _UAPIDEBUG_H */
