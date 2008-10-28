/* include/asm/current.h
 *
 * Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright (C) 2002 Pete Zaitcev (zaitcev@yahoo.com)
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 *
 *  Derived from "include/asm-s390/current.h" by
 *  Martin Schwidefsky (schwidefsky@de.ibm.com)
 *  Derived from "include/asm-i386/current.h"
*/
#ifndef _SPARC_CURRENT_H
#define _SPARC_CURRENT_H

#include <linux/thread_info.h>

#ifdef CONFIG_SPARC64
register struct task_struct *current asm("g4");
#endif

#ifdef CONFIG_SPARC32
/* We might want to consider using %g4 like sparc64 to shave a few cycles.
 *
 * Two stage process (inline + #define) for type-checking.
 * We also obfuscate get_current() to check if anyone used that by mistake.
 */
struct task_struct;
static inline struct task_struct *__get_current(void)
{
	return current_thread_info()->task;
}
#define current __get_current()
#endif

#endif /* !(_SPARC_CURRENT_H) */
