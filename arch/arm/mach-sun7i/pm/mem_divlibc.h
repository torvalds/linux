#ifndef _MEM_MISC_H
#define _MEM_MISC_H

#include "pm_types.h" 

void __div0(void);
__u32 raw_lib_udiv(__u32 dividend, __u32 divisior);
extern void __aeabi_idiv(void);
extern void __aeabi_idivmod(void);
extern void __aeabi_uidiv(void);
extern void __aeabi_uidivmod(void);

#endif /*_MEM_MISC_H*/