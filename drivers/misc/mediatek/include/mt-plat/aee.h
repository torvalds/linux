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

#if !defined(__AEE_H__)
#define __AEE_H__

#include <linux/kernel.h>
#include <linux/sched.h>

#define AEE_MODULE_NAME_LENGTH 64
#define AEE_PROCESS_NAME_LENGTH 256
#define AEE_BACKTRACE_LENGTH 3072

typedef enum {
	AE_DEFECT_FATAL,
	AE_DEFECT_EXCEPTION,
	AE_DEFECT_WARNING,
	AE_DEFECT_REMINDING,
	AE_DEFECT_ATTR_END
} AE_DEFECT_ATTR;

typedef enum {
	AE_KE = 0,		/* Fatal Exception */
	AE_HWT,
	AE_REBOOT,
	AE_NE,
	AE_JE,
	AE_SWT,
	AE_EE,
	AE_EXP_ERR_END,
	AE_ANR,			/* Error or Warning or Defect */
	AE_RESMON,
	AE_MODEM_WARNING,
	AE_WTF,
	AE_WRN_ERR_END,
	AE_MANUAL,		/* Manual Raise */
	AE_EXP_CLASS_END,

	AE_KERNEL_PROBLEM_REPORT = 1000,
	AE_SYSTEM_JAVA_DEFECT,
	AE_SYSTEM_NATIVE_DEFECT,
	AE_MANUAL_MRDUMP_KEY,
} AE_EXP_CLASS;			/* General Program Exception Class */

typedef enum {
	AEE_REBOOT_MODE_NORMAL = 0,
	AEE_REBOOT_MODE_KERNEL_OOPS,
	AEE_REBOOT_MODE_KERNEL_PANIC,
	AEE_REBOOT_MODE_NESTED_EXCEPTION,
	AEE_REBOOT_MODE_WDT,
	AEE_REBOOT_MODE_MANUAL_KDUMP,
} AEE_REBOOT_MODE;

#define AEE_SZ_SYMBOL_L 140
#define AEE_SZ_SYMBOL_S 80
struct aee_bt_frame {
	__u64 pc;
	__u64 lr;
	__u32 pad[5];
	char pc_symbol[AEE_SZ_SYMBOL_S];	/* Now we use different symbol length for PC &LR */
	char lr_symbol[AEE_SZ_SYMBOL_L];
};

/* aee_process_info struct should strictly small than ipanic_buffer, now 4KB */
struct aee_process_info {
	char process_path[AEE_PROCESS_NAME_LENGTH];
	char backtrace[AEE_BACKTRACE_LENGTH];
	struct aee_bt_frame ke_frame;
};

struct aee_process_bt {
	__u32 pid;
	__u32 nr_entries;
	struct aee_bt_frame *entries;
};


struct aee_thread_reg {
	pid_t tid;
	struct pt_regs regs;
};

struct aee_user_thread_stack {
	pid_t tid;
	int StackLength;
	unsigned char *Userthread_Stack; /*8k stack ,define to char only for match 64bit/32bit*/
};

struct aee_user_thread_maps {
	pid_t tid;
	int Userthread_mapsLength;
	unsigned char *Userthread_maps; /*8k stack ,define to char only for match 64bit/32bit*/
};



struct aee_oops {
	struct list_head list;
	AE_DEFECT_ATTR attr;
	AE_EXP_CLASS clazz;

	char module[AEE_MODULE_NAME_LENGTH];
	/* consist with struct aee_process_info */
	char process_path[AEE_PROCESS_NAME_LENGTH];
	char backtrace[AEE_BACKTRACE_LENGTH];
	struct aee_bt_frame ke_frame;

	char *detail;
	int detail_len;

	char *console;
	int console_len;

	char *android_main;
	int android_main_len;
	char *android_radio;
	int android_radio_len;
	char *android_system;
	int android_system_len;

	char *userspace_info;
	int userspace_info_len;

	char *mmprofile;
	int mmprofile_len;

	char *mini_rdump;
	int mini_rdump_len;


	struct aee_user_thread_stack userthread_stack;
	struct aee_thread_reg userthread_reg;
	struct aee_user_thread_maps userthread_maps;

	int dump_option;
};

struct aee_kernel_api {
	void (*kernel_reportAPI)(const AE_DEFECT_ATTR attr, const int db_opt, const char *module,
		     const char *msg);
	void (*md_exception)(const char *assert_type, const int *log, int log_size, const int *phy,
		     int phy_size, const char *detail, const int db_opt);
	void (*md32_exception)(const char *assert_type, const int *log, int log_size,
		     const int *phy, int phy_size, const char *detail, const int db_opt);
	void (*combo_exception)(const char *assert_type, const int *log, int log_size,
		     const int *phy, int phy_size, const char *detail, const int db_opt);
	void (*scp_exception)(const char *assert_type, const int *log, int log_size,
		     const int *phy, int phy_size, const char *detail, const int db_opt);
};

void aee_sram_printk(const char *fmt, ...);
int aee_nested_printf(const char *fmt, ...);
void aee_wdt_irq_info(void);
void aee_wdt_fiq_info(void *arg, void *regs, void *svc_sp);
void aee_trigger_kdb(void);
struct aee_oops *aee_oops_create(AE_DEFECT_ATTR attr, AE_EXP_CLASS clazz, const char *module);
void aee_oops_set_backtrace(struct aee_oops *oops, const char *backtrace);
void aee_oops_set_process_path(struct aee_oops *oops, const char *process_path);
void aee_oops_free(struct aee_oops *oops);
/* powerkey press,modules use bits */
#define AE_WDT_Powerkey_DEVICE_PATH		"/dev/kick_powerkey"
#define WDT_SETBY_DEFAULT		(0)
#define WDT_SETBY_Backlight		(1<<0)
#define WDT_SETBY_Display			(1<<1)
#define WDT_SETBY_SF				(1<<2)
#define WDT_SETBY_PM				(1<<3)
#define WDT_SETBY_WMS_DISABLE_PWK_MONITOR	(0xAEEAEE00)
#define WDT_SETBY_WMS_ENABLE_PWK_MONITOR	(0xAEEAEE01)
#define WDT_PWK_HANG_FORCE_HWT				(0xAEE0FFFF)

/* QHQ RT Monitor */
#define AEEIOCTL_RT_MON_Kick _IOR('p', 0x0A, int)
#define AE_WDT_DEVICE_PATH      "/dev/RT_Monitor"
/* QHQ RT Monitor    end */

/* DB dump option bits, set relative bit to 1 to include related file in db */
#define DB_OPT_DEFAULT                  (0)
#define DB_OPT_FTRACE                   (1<<0)
#define DB_OPT_PRINTK_TOO_MUCH          (1<<1)
#define DB_OPT_NE_JBT_TRACES            (1<<2)
#define DB_OPT_SWT_JBT_TRACES           (1<<3)
#define DB_OPT_VM_TRACES                (1<<4)
#define DB_OPT_DUMPSYS_ACTIVITY         (1<<5)
#define DB_OPT_DUMPSYS_WINDOW           (1<<6)
#define DB_OPT_DUMPSYS_GFXINFO          (1<<7)
#define DB_OPT_DUMPSYS_SURFACEFLINGER   (1<<8)
#define DB_OPT_DISPLAY_HANG_DUMP        (1<<9)
#define DB_OPT_LOW_MEMORY_KILLER        (1<<10)
#define DB_OPT_PROC_MEM                 (1<<11)
#define DB_OPT_FS_IO_LOG                (1<<12)
#define DB_OPT_PROCESS_COREDUMP         (1<<13)
#define DB_OPT_VM_HPROF                 (1<<14)
#define DB_OPT_PROCMEM                  (1<<15)
#define DB_OPT_DUMPSYS_INPUT            (1<<16)
#define DB_OPT_MMPROFILE_BUFFER         (1<<17)
#define DB_OPT_BINDER_INFO              (1<<18)
#define DB_OPT_WCN_ISSUE_INFO           (1<<19)
#define DB_OPT_DUMMY_DUMP               (1<<20)
#define DB_OPT_PID_MEMORY_INFO          (1<<21)
#define DB_OPT_VM_OOME_HPROF            (1<<22)
#define DB_OPT_PID_SMAPS                (1<<23)
#define DB_OPT_PROC_CMDQ_INFO           (1<<24)
#define DB_OPT_PROC_USKTRK              (1<<25)
#define DB_OPT_SF_RTT_DUMP              (1<<26)
#define DB_OPT_PAGETYPE_INFO            (1<<27)
#define DB_OPT_DUMPSYS_PROCSTATS        (1<<28)
#define DB_OPT_DUMP_DISPLAY             (1<<29)
#define DB_OPT_NATIVE_BACKTRACE		(1<<30)
#define DB_OPT_AARCH64			(1<<31)

#define aee_kernel_exception(module, msg...)	\
	aee_kernel_exception_api(__FILE__, __LINE__, DB_OPT_DEFAULT, module, msg)
#define aee_kernel_warning(module, msg...)	\
	aee_kernel_warning_api(__FILE__, __LINE__, DB_OPT_DEFAULT, module, msg)
#define aee_kernel_reminding(module, msg...)	\
	aee_kernel_reminding_api(__FILE__, __LINE__, DB_OPT_DEFAULT, module, msg)
#define aee_kernel_dal_show(msg)	\
	aee_kernel_dal_api(__FILE__, __LINE__, msg)

#define aed_md_exception(log, log_size, phy, phy_size, detail)	\
	aed_md_exception_api(log, log_size, phy, phy_size, detail, DB_OPT_DEFAULT)
#define aed_md32_exception(log, log_size, phy, phy_size, detail)	\
	aed_md32_exception_api(log, log_size, phy, phy_size, detail, DB_OPT_DEFAULT)
#define aed_scp_exception(log, log_size, phy, phy_size, detail)	\
	aed_scp_exception_api(log, log_size, phy, phy_size, detail, DB_OPT_DEFAULT)
#define aed_combo_exception(log, log_size, phy, phy_size, detail)	\
	aed_combo_exception_api(log, log_size, phy, phy_size, detail, DB_OPT_DEFAULT)

void aee_kernel_exception_api(const char *file, const int line, const int db_opt,
			      const char *module, const char *msg, ...);
void aee_kernel_warning_api(const char *file, const int line, const int db_opt, const char *module,
			    const char *msg, ...);
void aee_kernel_reminding_api(const char *file, const int line, const int db_opt,
			      const char *module, const char *msg, ...);
void aee_kernel_dal_api(const char *file, const int line, const char *msg);

void aed_md_exception_api(const int *log, int log_size, const int *phy, int phy_size,
			  const char *detail, const int db_opt);
void aed_md32_exception_api(const int *log, int log_size, const int *phy, int phy_size,
			    const char *detail, const int db_opt);
void aed_scp_exception_api(const int *log, int log_size, const int *phy, int phy_size,
			    const char *detail, const int db_opt);
void aed_combo_exception_api(const int *log, int log_size, const int *phy, int phy_size,
			     const char *detail, const int db_opt);

void aee_kernel_wdt_kick_Powkey_api(const char *module, int msg);
int aee_kernel_wdt_kick_api(int kinterval);
void aee_powerkey_notify_press(unsigned long pressed);
int aee_kernel_Powerkey_is_press(void);

void ipanic_recursive_ke(struct pt_regs *regs, struct pt_regs *excp_regs, int cpu);

/* QHQ RT Monitor */
void aee_kernel_RT_Monitor_api(int lParam);
/* QHQ RT Monitor    end */
void mt_fiq_printf(const char *fmt, ...);
void aee_register_api(struct aee_kernel_api *aee_api);
int aee_in_nested_panic(void);
void aee_stop_nested_panic(struct pt_regs *regs);
void aee_wdt_dump_info(void);
void aee_wdt_printf(const char *fmt, ...);

void aee_fiq_ipi_cpu_stop(void *arg, void *regs, void *svc_sp);

#if defined(CONFIG_MTK_AEE_DRAM_CONSOLE)
void aee_dram_console_reserve_memory(void);
#else
static inline void aee_dram_console_reserve_memory(void)
{
}
#endif

extern void *aee_excp_regs;	/* To store latest exception, in case of stack corruption */
#endif				/* __AEE_H__ */
