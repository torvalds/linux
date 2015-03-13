#ifndef __BACKPORT_LINUX_LOCKDEP_H
#define __BACKPORT_LINUX_LOCKDEP_H
#include_next <linux/lockdep.h>
#include <linux/version.h>

#ifndef lockdep_assert_held
#define lockdep_assert_held(l)			do { } while (0)
#endif

#endif /* __BACKPORT_LINUX_LOCKDEP_H */
