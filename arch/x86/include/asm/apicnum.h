#ifndef _ASM_X86_APICNUM_H
#define _ASM_X86_APICNUM_H

/* define MAX_IO_APICS */
#ifdef CONFIG_X86_32
# define MAX_IO_APICS 64
#else
# define MAX_IO_APICS 128
# define MAX_LOCAL_APIC 32768
#endif

#endif /* _ASM_X86_APICNUM_H */
