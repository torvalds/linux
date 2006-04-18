#ifndef _SYSDEP_TLS_H
#define _SYSDEP_TLS_H

# ifndef __KERNEL__

/* Change name to avoid conflicts with the original one from <asm/ldt.h>, which
 * may be named user_desc (but in 2.4 and in header matching its API was named
 * modify_ldt_ldt_s). */

typedef struct um_dup_user_desc {
	unsigned int  entry_number;
	unsigned int  base_addr;
	unsigned int  limit;
	unsigned int  seg_32bit:1;
	unsigned int  contents:2;
	unsigned int  read_exec_only:1;
	unsigned int  limit_in_pages:1;
	unsigned int  seg_not_present:1;
	unsigned int  useable:1;
	unsigned int  lm:1;
} user_desc_t;

# else /* __KERNEL__ */

#  include <asm/ldt.h>
typedef struct user_desc user_desc_t;

# endif /* __KERNEL__ */
#endif /* _SYSDEP_TLS_H */
