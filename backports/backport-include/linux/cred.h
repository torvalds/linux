#ifndef __BACKPORT_LINUX_CRED_H
#define __BACKPORT_LINUX_CRED_H
#include_next <linux/cred.h>
#include <linux/version.h>

#ifndef current_user_ns
#define current_user_ns()	(current->nsproxy->user_ns)
#endif

#endif /* __BACKPORT_LINUX_CRED_H */
