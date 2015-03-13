#ifndef __BACKPORT_ASM_GENERIC_BUG_H
#define __BACKPORT_ASM_GENERIC_BUG_H
#include_next <asm-generic/bug.h>

#ifndef __WARN
#define __WARN(foo) dump_stack()
#endif

#ifndef WARN_ONCE
#define WARN_ONCE(condition, format...) ({                      \
	static int __warned;                                    \
	int __ret_warn_once = !!(condition);                    \
								\
	if (unlikely(__ret_warn_once))                          \
		if (WARN(!__warned, format))                    \
			__warned = 1;                           \
	unlikely(__ret_warn_once);                              \
})
#endif

#ifndef __WARN_printf
/*
 * To port this properly we'd have to port warn_slowpath_null(),
 * which I'm lazy to do so just do a regular print for now. If you
 * want to port this read kernel/panic.c
 */
#define __WARN_printf(arg...)   do { printk(arg); __WARN(); } while (0)
#endif

#ifndef WARN
#define WARN(condition, format...) ({					\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		__WARN_printf(format);					\
	unlikely(__ret_warn_on);					\
})
#endif

#endif /* __BACKPORT_ASM_GENERIC_BUG_H */
