// SPDX-License-Identifier: GPL-2.0-only
/* vendor_hook.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2020 Google LLC
 */

#include <linux/iova.h>
#include <linux/dma-buf.h>

#define CREATE_TRACE_POINTS
#include <trace/hooks/vendor_hooks.h>
#include <linux/tracepoint.h>

#include <trace/hooks/fpsimd.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/cpuidle.h>
#include <trace/hooks/mpam.h>
#include <trace/hooks/wqlockup.h>
#include <trace/hooks/debug.h>
#include <trace/hooks/sysrqcrash.h>
#include <trace/hooks/printk.h>
#include <trace/hooks/epoch.h>
#include <trace/hooks/cpufreq.h>
#include <trace/hooks/preemptirq.h>
#include <trace/hooks/ftrace_dump.h>
#include <trace/hooks/ufshcd.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/sys.h>
#include <trace/hooks/iommu.h>
#include <trace/hooks/net.h>
#include <trace/hooks/pm_domain.h>
#include <trace/hooks/cpuidle_psci.h>
#include <trace/hooks/vmscan.h>
#include <trace/hooks/avc.h>
#include <trace/hooks/creds.h>
#include <trace/hooks/selinux.h>
#include <trace/hooks/syscall_check.h>
#include <trace/hooks/remoteproc.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/fips140.h>
#include <trace/hooks/dmabuf.h>
#include <trace/hooks/gic.h>
#include <trace/hooks/gic_v3.h>
#include <trace/hooks/timer.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/hung_task.h>
#include <trace/hooks/bug.h>
#include <trace/hooks/softlockup.h>
#include <trace/hooks/power.h>
#include <trace/hooks/fault.h>
#include <trace/hooks/traps.h>
#include <trace/hooks/thermal.h>
#include <trace/hooks/audio_usboffload.h>

/*
 * Export tracepoints that act as a bare tracehook (ie: have no trace event
 * associated with them) to allow external modules to probe them.
 */
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_arch_set_freq_scale);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_is_fpsimd_save);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_transaction_init);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_set_priority);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_restore_priority);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_wakeup_ilocked);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_mutex_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_mutex_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rtmutex_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rtmutex_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_read_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_read_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_sched_show_task);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cpu_idle_enter);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cpu_idle_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_mpam_set);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_wq_lockup_pool);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ipi_stop);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_sysrq_crash);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_printk_hotplug);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_show_suspend_epoch_val);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_show_resume_epoch_val);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_freq_table_limits);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cpufreq_resolve_freq);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cpufreq_fast_switch);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cpufreq_target);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_preempt_disable);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_preempt_enable);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_irqs_disable);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_irqs_enable);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_cpu_cgroup_attach);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_cpu_cgroup_online);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ftrace_oops_enter);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ftrace_oops_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ftrace_size_check);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ftrace_format_check);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ftrace_dump_buffer);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_fill_prdt);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_prepare_command);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_update_sysfs);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_send_command);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_compl_command);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cgroup_set_task);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_syscall_prctl_finished);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_send_uic_command);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_send_tm_command);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_check_int_errors);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_update_sdev);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cgroup_attach);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_iommu_setup_dma_ops);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_iommu_alloc_insert_iova);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_iommu_iovad_alloc_iova);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_iommu_iovad_free_iova);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_iommu_iovad_init_alloc_algo);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_iommu_limit_align_shift);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ptype_head);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_allow_domain_state);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cpuidle_psci_enter);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_cpuidle_psci_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_wait_for_work);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_sync_txn_recvd);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_cpufreq_transition);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_set_balance_anon_file_reclaim);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_show_max_freq);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_selinux_avc_insert);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_selinux_avc_node_delete);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_selinux_avc_node_replace);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_selinux_avc_lookup);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_commit_creds);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_exit_creds);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_override_creds);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_revert_creds);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_selinux_is_initialized);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_mmap_file);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_file_open);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_bpf_syscall);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ignore_dmabuf_vmap_bounds);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rproc_recovery);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rproc_recovery_set);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_init);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_wake);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_finished);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_alter_rwsem_list_add);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_alter_futex_plist_add);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_sha256);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_aes_expandkey);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_aes_encrypt);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_aes_decrypt);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_timer_calc_index);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_gic_v3_set_affinity);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_gic_set_affinity);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_gic_v3_affinity_init);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_uninterrupt_tasks);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_uninterrupt_tasks_done);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_report_bug);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_watchdog_timer_softlockup);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_try_to_freeze_todo);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_try_to_freeze_todo_unfrozen);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_die_kernel_fault);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_do_sea);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_do_sp_pc_abort);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_do_undefinstr);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_do_el1_bti);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_do_el1_fpac);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_panic_unhandled);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_arm64_serror_panic);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_disable_thermal_cooling_stats);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_gic_resume);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_audio_usb_offload_connect);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_audio_usb_offload_disconnect);
