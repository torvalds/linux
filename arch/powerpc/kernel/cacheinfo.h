/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PPC_CACHEINFO_H
#define _PPC_CACHEINFO_H

/* These are just hooks for sysfs.c to use. */
extern void cacheinfo_cpu_online(unsigned int cpu_id);
extern void cacheinfo_cpu_offline(unsigned int cpu_id);

#endif /* _PPC_CACHEINFO_H */
