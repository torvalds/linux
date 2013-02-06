/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ARCH_ARM_MACH_MSM_SMSM_H_
#define _ARCH_ARM_MACH_MSM_SMSM_H_

#include <linux/notifier.h>
#if defined(CONFIG_MSM_N_WAY_SMSM)
enum {
	SMSM_APPS_STATE,
	SMSM_MODEM_STATE,
	SMSM_Q6_STATE,
	SMSM_APPS_DEM,
	SMSM_WCNSS_STATE = SMSM_APPS_DEM,
	SMSM_MODEM_DEM,
	SMSM_DSPS_STATE = SMSM_MODEM_DEM,
	SMSM_Q6_DEM,
	SMSM_POWER_MASTER_DEM,
	SMSM_TIME_MASTER_DEM,
};
extern uint32_t SMSM_NUM_ENTRIES;
#else
enum {
	SMSM_APPS_STATE = 1,
	SMSM_MODEM_STATE = 3,
	SMSM_NUM_ENTRIES,
};
#endif

enum {
	SMSM_APPS,
	SMSM_MODEM,
	SMSM_Q6,
	SMSM_WCNSS,
	SMSM_DSPS,
};
extern uint32_t SMSM_NUM_HOSTS;

#define SMSM_INIT              0x00000001
#define SMSM_OSENTERED         0x00000002
#define SMSM_SMDWAIT           0x00000004
#define SMSM_SMDINIT           0x00000008
#define SMSM_RPCWAIT           0x00000010
#define SMSM_RPCINIT           0x00000020
#define SMSM_RESET             0x00000040
#define SMSM_RSA               0x00000080
#define SMSM_RUN               0x00000100
#define SMSM_PWRC              0x00000200
#define SMSM_TIMEWAIT          0x00000400
#define SMSM_TIMEINIT          0x00000800
#define SMSM_PWRC_EARLY_EXIT   0x00001000
#define SMSM_WFPI              0x00002000
#define SMSM_SLEEP             0x00004000
#define SMSM_SLEEPEXIT         0x00008000
#define SMSM_OEMSBL_RELEASE    0x00010000
#define SMSM_APPS_REBOOT       0x00020000
#define SMSM_SYSTEM_POWER_DOWN 0x00040000
#define SMSM_SYSTEM_REBOOT     0x00080000
#define SMSM_SYSTEM_DOWNLOAD   0x00100000
#define SMSM_PWRC_SUSPEND      0x00200000
#define SMSM_APPS_SHUTDOWN     0x00400000
#define SMSM_SMD_LOOPBACK      0x00800000
#define SMSM_RUN_QUIET         0x01000000
#define SMSM_MODEM_WAIT        0x02000000
#define SMSM_MODEM_BREAK       0x04000000
#define SMSM_MODEM_CONTINUE    0x08000000
#define SMSM_SYSTEM_REBOOT_USR 0x20000000
#define SMSM_SYSTEM_PWRDWN_USR 0x40000000
#define SMSM_UNKNOWN           0x80000000

#define SMSM_WKUP_REASON_RPC	0x00000001
#define SMSM_WKUP_REASON_INT	0x00000002
#define SMSM_WKUP_REASON_GPIO	0x00000004
#define SMSM_WKUP_REASON_TIMER	0x00000008
#define SMSM_WKUP_REASON_ALARM	0x00000010
#define SMSM_WKUP_REASON_RESET	0x00000020
#define SMSM_A2_FORCE_SHUTDOWN 0x00002000
#define SMSM_A2_RESET_BAM      0x00004000

#define SMSM_VENDOR             0x00020000

#define SMSM_A2_POWER_CONTROL  0x00000002
#define SMSM_A2_POWER_CONTROL_ACK  0x00000800

#define SMSM_WLAN_TX_RINGS_EMPTY 0x00000200
#define SMSM_WLAN_TX_ENABLE	0x00000400


void *smem_alloc(unsigned id, unsigned size);
void *smem_alloc2(unsigned id, unsigned size_in);
void *smem_get_entry(unsigned id, unsigned *size);
int smsm_change_state(uint32_t smsm_entry,
		      uint32_t clear_mask, uint32_t set_mask);

/*
 * Changes the global interrupt mask.  The set and clear masks are re-applied
 * every time the global interrupt mask is updated for callback registration
 * and de-registration.
 *
 * The clear mask is applied first, so if a bit is set to 1 in both the clear
 * mask and the set mask, the result will be that the interrupt is set.
 *
 * @smsm_entry  SMSM entry to change
 * @clear_mask  1 = clear bit, 0 = no-op
 * @set_mask    1 = set bit, 0 = no-op
 *
 * @returns 0 for success, < 0 for error
 */
int smsm_change_intr_mask(uint32_t smsm_entry,
			  uint32_t clear_mask, uint32_t set_mask);
int smsm_get_intr_mask(uint32_t smsm_entry, uint32_t *intr_mask);
uint32_t smsm_get_state(uint32_t smsm_entry);
int smsm_state_cb_register(uint32_t smsm_entry, uint32_t mask,
	void (*notify)(void *, uint32_t old_state, uint32_t new_state),
	void *data);
int smsm_state_cb_deregister(uint32_t smsm_entry, uint32_t mask,
	void (*notify)(void *, uint32_t, uint32_t), void *data);
int smsm_driver_state_notifier_register(struct notifier_block *nb);
int smsm_driver_state_notifier_unregister(struct notifier_block *nb);
void smsm_print_sleep_info(uint32_t sleep_delay, uint32_t sleep_limit,
	uint32_t irq_mask, uint32_t wakeup_reason, uint32_t pending_irqs);
void smsm_reset_modem(unsigned mode);
void smsm_reset_modem_cont(void);
void smd_sleep_exit(void);

#define SMEM_NUM_SMD_STREAM_CHANNELS        64
#define SMEM_NUM_SMD_BLOCK_CHANNELS         64

enum {
	/* fixed items */
	SMEM_PROC_COMM = 0,
	SMEM_HEAP_INFO,
	SMEM_ALLOCATION_TABLE,
	SMEM_VERSION_INFO,
	SMEM_HW_RESET_DETECT,
	SMEM_AARM_WARM_BOOT,
	SMEM_DIAG_ERR_MESSAGE,
	SMEM_SPINLOCK_ARRAY,
	SMEM_MEMORY_BARRIER_LOCATION,
	SMEM_FIXED_ITEM_LAST = SMEM_MEMORY_BARRIER_LOCATION,

	/* dynamic items */
	SMEM_AARM_PARTITION_TABLE,
	SMEM_AARM_BAD_BLOCK_TABLE,
	SMEM_RESERVE_BAD_BLOCKS,
	SMEM_WM_UUID,
	SMEM_CHANNEL_ALLOC_TBL,
	SMEM_SMD_BASE_ID,
	SMEM_SMEM_LOG_IDX = SMEM_SMD_BASE_ID + SMEM_NUM_SMD_STREAM_CHANNELS,
	SMEM_SMEM_LOG_EVENTS,
	SMEM_SMEM_STATIC_LOG_IDX,
	SMEM_SMEM_STATIC_LOG_EVENTS,
	SMEM_SMEM_SLOW_CLOCK_SYNC,
	SMEM_SMEM_SLOW_CLOCK_VALUE,
	SMEM_BIO_LED_BUF,
	SMEM_SMSM_SHARED_STATE,
	SMEM_SMSM_INT_INFO,
	SMEM_SMSM_SLEEP_DELAY,
	SMEM_SMSM_LIMIT_SLEEP,
	SMEM_SLEEP_POWER_COLLAPSE_DISABLED,
	SMEM_KEYPAD_KEYS_PRESSED,
	SMEM_KEYPAD_STATE_UPDATED,
	SMEM_KEYPAD_STATE_IDX,
	SMEM_GPIO_INT,
	SMEM_MDDI_LCD_IDX,
	SMEM_MDDI_HOST_DRIVER_STATE,
	SMEM_MDDI_LCD_DISP_STATE,
	SMEM_LCD_CUR_PANEL,
	SMEM_MARM_BOOT_SEGMENT_INFO,
	SMEM_AARM_BOOT_SEGMENT_INFO,
	SMEM_SLEEP_STATIC,
	SMEM_SCORPION_FREQUENCY,
	SMEM_SMD_PROFILES,
	SMEM_TSSC_BUSY,
	SMEM_HS_SUSPEND_FILTER_INFO,
	SMEM_BATT_INFO,
	SMEM_APPS_BOOT_MODE,
	SMEM_VERSION_FIRST,
	SMEM_VERSION_SMD = SMEM_VERSION_FIRST,
	SMEM_VERSION_LAST = SMEM_VERSION_FIRST + 24,
	SMEM_OSS_RRCASN1_BUF1,
	SMEM_OSS_RRCASN1_BUF2,
	SMEM_ID_VENDOR0,
	SMEM_ID_VENDOR1,
	SMEM_ID_VENDOR2,
	SMEM_HW_SW_BUILD_ID,
	SMEM_SMD_BLOCK_PORT_BASE_ID,
	SMEM_SMD_BLOCK_PORT_PROC0_HEAP = SMEM_SMD_BLOCK_PORT_BASE_ID +
						SMEM_NUM_SMD_BLOCK_CHANNELS,
	SMEM_SMD_BLOCK_PORT_PROC1_HEAP = SMEM_SMD_BLOCK_PORT_PROC0_HEAP +
						SMEM_NUM_SMD_BLOCK_CHANNELS,
	SMEM_I2C_MUTEX = SMEM_SMD_BLOCK_PORT_PROC1_HEAP +
						SMEM_NUM_SMD_BLOCK_CHANNELS,
	SMEM_SCLK_CONVERSION,
	SMEM_SMD_SMSM_INTR_MUX,
	SMEM_SMSM_CPU_INTR_MASK,
	SMEM_APPS_DEM_SLAVE_DATA,
	SMEM_QDSP6_DEM_SLAVE_DATA,
	SMEM_CLKREGIM_BSP,
	SMEM_CLKREGIM_SOURCES,
	SMEM_SMD_FIFO_BASE_ID,
	SMEM_USABLE_RAM_PARTITION_TABLE = SMEM_SMD_FIFO_BASE_ID +
						SMEM_NUM_SMD_STREAM_CHANNELS,
	SMEM_POWER_ON_STATUS_INFO,
	SMEM_DAL_AREA,
	SMEM_SMEM_LOG_POWER_IDX,
	SMEM_SMEM_LOG_POWER_WRAP,
	SMEM_SMEM_LOG_POWER_EVENTS,
	SMEM_ERR_CRASH_LOG,
	SMEM_ERR_F3_TRACE_LOG,
	SMEM_SMD_BRIDGE_ALLOC_TABLE,
	SMEM_SMDLITE_TABLE,
	SMEM_SD_IMG_UPGRADE_STATUS,
	SMEM_SEFS_INFO,
	SMEM_RESET_LOG,
	SMEM_RESET_LOG_SYMBOLS,
	SMEM_MODEM_SW_BUILD_ID,
	SMEM_SMEM_LOG_MPROC_WRAP,
	SMEM_BOOT_INFO_FOR_APPS,
	SMEM_SMSM_SIZE_INFO,
	SMEM_SMD_LOOPBACK_REGISTER,
	SMEM_SSR_REASON_MSS0,
	SMEM_SSR_REASON_WCNSS0,
	SMEM_SSR_REASON_LPASS0,
	SMEM_SSR_REASON_DSPS0,
	SMEM_SSR_REASON_VCODEC0,
	SMEM_MEM_LAST = SMEM_SSR_REASON_VCODEC0,
	SMEM_NUM_ITEMS,
};

enum {
	SMEM_APPS_Q6_SMSM = 3,
	SMEM_Q6_APPS_SMSM = 5,
	SMSM_NUM_INTR_MUX = 8,
};

int smsm_check_for_modem_crash(void);
void *smem_find(unsigned id, unsigned size);
void *smem_get_entry(unsigned id, unsigned *size);

#endif
