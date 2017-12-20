/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __MTK_RAM_CONSOLE_H__
#define __MTK_RAM_CONSOLE_H__

#include <linux/console.h>
#include <linux/pstore.h>

typedef enum {
	AEE_FIQ_STEP_FIQ_ISR_BASE = 1,
	AEE_FIQ_STEP_WDT_FIQ_INFO = 4,
	AEE_FIQ_STEP_WDT_FIQ_STACK,
	AEE_FIQ_STEP_WDT_FIQ_LOOP,
	AEE_FIQ_STEP_WDT_FIQ_DONE,
	AEE_FIQ_STEP_WDT_IRQ_INFO = 8,
	AEE_FIQ_STEP_WDT_IRQ_KICK,
	AEE_FIQ_STEP_WDT_IRQ_SMP_STOP,
	AEE_FIQ_STEP_WDT_IRQ_TIME,
	AEE_FIQ_STEP_WDT_IRQ_STACK,
	AEE_FIQ_STEP_WDT_IRQ_GIC,
	AEE_FIQ_STEP_WDT_IRQ_LOCALTIMER,
	AEE_FIQ_STEP_WDT_IRQ_IDLE,
	AEE_FIQ_STEP_WDT_IRQ_SCHED,
	AEE_FIQ_STEP_WDT_IRQ_DONE,
	AEE_FIQ_STEP_KE_WDT_INFO = 20,
	AEE_FIQ_STEP_KE_WDT_PERCPU,
	AEE_FIQ_STEP_KE_WDT_LOG,
	AEE_FIQ_STEP_KE_SCHED_DEBUG,
	AEE_FIQ_STEP_KE_EINT_DEBUG,
	AEE_FIQ_STEP_KE_WDT_DONE,
	AEE_FIQ_STEP_KE_IPANIC_DIE = 32,
	AEE_FIQ_STEP_KE_IPANIC_START,
	AEE_FIQ_STEP_KE_IPANIC_OOP_HEADER,
	AEE_FIQ_STEP_KE_IPANIC_DETAIL,
	AEE_FIQ_STEP_KE_IPANIC_CONSOLE,
	AEE_FIQ_STEP_KE_IPANIC_USERSPACE,
	AEE_FIQ_STEP_KE_IPANIC_ANDROID,
	AEE_FIQ_STEP_KE_IPANIC_MMPROFILE,
	AEE_FIQ_STEP_KE_IPANIC_HEADER,
	AEE_FIQ_STEP_KE_IPANIC_DONE,
	AEE_FIQ_STEP_KE_NESTED_PANIC = 64,
} AEE_FIQ_STEP_NUM;

#ifdef CONFIG_MTK_RAM_CONSOLE
extern int aee_rr_curr_fiq_step(void);
extern void aee_rr_rec_fiq_step(u8 i);
extern void aee_rr_rec_reboot_mode(u8 mode);
extern void aee_rr_rec_kdump_params(void *params);
extern void aee_rr_rec_last_irq_enter(int cpu, int irq, u64 j);
extern void aee_rr_rec_last_irq_exit(int cpu, int irq, u64 j);
extern void aee_rr_rec_last_sched_jiffies(int cpu, u64 j, const char *comm);
extern void aee_sram_fiq_log(const char *msg);
extern void ram_console_write(struct console *console, const char *s, unsigned int count);
extern void aee_sram_fiq_save_bin(const char *buffer, size_t len);
extern void aee_rr_rec_hotplug_footprint(int cpu, u8 fp);
extern void aee_rr_rec_hotplug_cpu_event(u8 val);
extern void aee_rr_rec_hotplug_cb_index(u8 val);
extern void aee_rr_rec_hotplug_cb_fp(unsigned long val);
#ifdef CONFIG_MTK_EMMC_SUPPORT
extern void last_kmsg_store_to_emmc(void);
#endif

#else
static inline void aee_rr_rec_hotplug_footprint(int cpu, u8 fp)
{
}
static inline void aee_rr_rec_hotplug_cpu_event(u8 val)
{
}
static inline void aee_rr_rec_hotplug_cb_index(u8 val)
{
}
static inline void aee_rr_rec_hotplug_cb_fp(unsigned long val)
{
}
static inline int aee_rr_curr_fiq_step(void)
{
	return 0;
}

static inline void aee_rr_rec_fiq_step(u8 i)
{
}

static inline unsigned int aee_rr_curr_exp_type(void)
{
	return 0;
}

static inline void aee_rr_rec_exp_type(unsigned int type)
{
}

static inline void aee_rr_rec_reboot_mode(u8 mode)
{
}

static inline void aee_rr_rec_kdump_params(void *params)
{
}

static inline void aee_rr_rec_last_irq_enter(int cpu, int irq, u64 j)
{
}

static inline void aee_rr_rec_last_irq_exit(int cpu, int irq, u64 j)
{
}

static inline void aee_rr_rec_last_sched_jiffies(int cpu, u64 j, const char *comm)
{
}

static inline void aee_sram_fiq_log(const char *msg)
{
}

static inline void ram_console_write(struct console *console, const char *s, unsigned int count)
{
}

static inline void aee_sram_fiq_save_bin(unsigned char *buffer, size_t len)
{
}

#ifdef CONFIG_MTK_EMMC_SUPPORT
static inline void last_kmsg_store_to_emmc(void)
{
}
#endif

#endif /* CONFIG_MTK_RAM_CONSOLE */

#ifdef CONFIG_MTK_AEE_IPANIC
extern int ipanic_kmsg_write(unsigned int part, const char *buf, size_t size);
extern int ipanic_kmsg_get_next(int *count, u64 *id, enum pstore_type_id *type, struct timespec *time,
				  char **buf, struct pstore_info *psi);
#else
static inline int ipanic_kmsg_write(unsigned int part, const char *buf, size_t size)
{
	return 0;
}

static inline int ipanic_kmsg_get_next(int *count, u64 *id, enum pstore_type_id *type, struct timespec *time,
				  char **buf, struct pstore_info *psi)
{
	return 0;
}
#endif /* CONFIG_MTK_AEE_IPANIC */

#endif
