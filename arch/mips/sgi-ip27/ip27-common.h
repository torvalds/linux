/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IP27_COMMON_H
#define __IP27_COMMON_H

extern nasid_t master_nasid;

extern void cpu_node_probe(void);
extern void hub_rt_clock_event_init(void);
extern void hub_rtc_init(nasid_t nasid);
extern void install_cpu_nmi_handler(int slice);
extern void install_ipi(void);
extern void ip27_reboot_setup(void);
extern const struct plat_smp_ops ip27_smp_ops;
extern unsigned long node_getfirstfree(nasid_t nasid);
extern void per_cpu_init(void);
extern void replicate_kernel_text(void);
extern void setup_replication_mask(void);

#endif /* __IP27_COMMON_H */
