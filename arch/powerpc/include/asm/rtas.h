/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _POWERPC_RTAS_H
#define _POWERPC_RTAS_H
#ifdef __KERNEL__

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <asm/page.h>
#include <asm/rtas-types.h>
#include <linux/time.h>
#include <linux/cpumask.h>

/*
 * Definitions for talking to the RTAS on CHRP machines.
 *
 * Copyright (C) 2001 Peter Bergner
 * Copyright (C) 2001 PPC 64 Team, IBM Corp
 */

enum rtas_function_index {
	RTAS_FNIDX__CHECK_EXCEPTION,
	RTAS_FNIDX__DISPLAY_CHARACTER,
	RTAS_FNIDX__EVENT_SCAN,
	RTAS_FNIDX__FREEZE_TIME_BASE,
	RTAS_FNIDX__GET_POWER_LEVEL,
	RTAS_FNIDX__GET_SENSOR_STATE,
	RTAS_FNIDX__GET_TERM_CHAR,
	RTAS_FNIDX__GET_TIME_OF_DAY,
	RTAS_FNIDX__IBM_ACTIVATE_FIRMWARE,
	RTAS_FNIDX__IBM_CBE_START_PTCAL,
	RTAS_FNIDX__IBM_CBE_STOP_PTCAL,
	RTAS_FNIDX__IBM_CHANGE_MSI,
	RTAS_FNIDX__IBM_CLOSE_ERRINJCT,
	RTAS_FNIDX__IBM_CONFIGURE_BRIDGE,
	RTAS_FNIDX__IBM_CONFIGURE_CONNECTOR,
	RTAS_FNIDX__IBM_CONFIGURE_KERNEL_DUMP,
	RTAS_FNIDX__IBM_CONFIGURE_PE,
	RTAS_FNIDX__IBM_CREATE_PE_DMA_WINDOW,
	RTAS_FNIDX__IBM_DISPLAY_MESSAGE,
	RTAS_FNIDX__IBM_ERRINJCT,
	RTAS_FNIDX__IBM_EXTI2C,
	RTAS_FNIDX__IBM_GET_CONFIG_ADDR_INFO,
	RTAS_FNIDX__IBM_GET_CONFIG_ADDR_INFO2,
	RTAS_FNIDX__IBM_GET_DYNAMIC_SENSOR_STATE,
	RTAS_FNIDX__IBM_GET_INDICES,
	RTAS_FNIDX__IBM_GET_RIO_TOPOLOGY,
	RTAS_FNIDX__IBM_GET_SYSTEM_PARAMETER,
	RTAS_FNIDX__IBM_GET_VPD,
	RTAS_FNIDX__IBM_GET_XIVE,
	RTAS_FNIDX__IBM_INT_OFF,
	RTAS_FNIDX__IBM_INT_ON,
	RTAS_FNIDX__IBM_IO_QUIESCE_ACK,
	RTAS_FNIDX__IBM_LPAR_PERFTOOLS,
	RTAS_FNIDX__IBM_MANAGE_FLASH_IMAGE,
	RTAS_FNIDX__IBM_MANAGE_STORAGE_PRESERVATION,
	RTAS_FNIDX__IBM_NMI_INTERLOCK,
	RTAS_FNIDX__IBM_NMI_REGISTER,
	RTAS_FNIDX__IBM_OPEN_ERRINJCT,
	RTAS_FNIDX__IBM_OPEN_SRIOV_ALLOW_UNFREEZE,
	RTAS_FNIDX__IBM_OPEN_SRIOV_MAP_PE_NUMBER,
	RTAS_FNIDX__IBM_OS_TERM,
	RTAS_FNIDX__IBM_PARTNER_CONTROL,
	RTAS_FNIDX__IBM_PHYSICAL_ATTESTATION,
	RTAS_FNIDX__IBM_PLATFORM_DUMP,
	RTAS_FNIDX__IBM_POWER_OFF_UPS,
	RTAS_FNIDX__IBM_QUERY_INTERRUPT_SOURCE_NUMBER,
	RTAS_FNIDX__IBM_QUERY_PE_DMA_WINDOW,
	RTAS_FNIDX__IBM_READ_PCI_CONFIG,
	RTAS_FNIDX__IBM_READ_SLOT_RESET_STATE,
	RTAS_FNIDX__IBM_READ_SLOT_RESET_STATE2,
	RTAS_FNIDX__IBM_REMOVE_PE_DMA_WINDOW,
	RTAS_FNIDX__IBM_RESET_PE_DMA_WINDOWS,
	RTAS_FNIDX__IBM_SCAN_LOG_DUMP,
	RTAS_FNIDX__IBM_SET_DYNAMIC_INDICATOR,
	RTAS_FNIDX__IBM_SET_EEH_OPTION,
	RTAS_FNIDX__IBM_SET_SLOT_RESET,
	RTAS_FNIDX__IBM_SET_SYSTEM_PARAMETER,
	RTAS_FNIDX__IBM_SET_XIVE,
	RTAS_FNIDX__IBM_SLOT_ERROR_DETAIL,
	RTAS_FNIDX__IBM_SUSPEND_ME,
	RTAS_FNIDX__IBM_TUNE_DMA_PARMS,
	RTAS_FNIDX__IBM_UPDATE_FLASH_64_AND_REBOOT,
	RTAS_FNIDX__IBM_UPDATE_NODES,
	RTAS_FNIDX__IBM_UPDATE_PROPERTIES,
	RTAS_FNIDX__IBM_VALIDATE_FLASH_IMAGE,
	RTAS_FNIDX__IBM_WRITE_PCI_CONFIG,
	RTAS_FNIDX__NVRAM_FETCH,
	RTAS_FNIDX__NVRAM_STORE,
	RTAS_FNIDX__POWER_OFF,
	RTAS_FNIDX__PUT_TERM_CHAR,
	RTAS_FNIDX__QUERY_CPU_STOPPED_STATE,
	RTAS_FNIDX__READ_PCI_CONFIG,
	RTAS_FNIDX__RTAS_LAST_ERROR,
	RTAS_FNIDX__SET_INDICATOR,
	RTAS_FNIDX__SET_POWER_LEVEL,
	RTAS_FNIDX__SET_TIME_FOR_POWER_ON,
	RTAS_FNIDX__SET_TIME_OF_DAY,
	RTAS_FNIDX__START_CPU,
	RTAS_FNIDX__STOP_SELF,
	RTAS_FNIDX__SYSTEM_REBOOT,
	RTAS_FNIDX__THAW_TIME_BASE,
	RTAS_FNIDX__WRITE_PCI_CONFIG,
};

/*
 * Opaque handle for client code to refer to RTAS functions. All valid
 * function handles are build-time constants prefixed with RTAS_FN_.
 */
typedef struct {
	const enum rtas_function_index index;
} rtas_fn_handle_t;


#define rtas_fn_handle(x_) ((const rtas_fn_handle_t) { .index = x_, })

#define RTAS_FN_CHECK_EXCEPTION                   rtas_fn_handle(RTAS_FNIDX__CHECK_EXCEPTION)
#define RTAS_FN_DISPLAY_CHARACTER                 rtas_fn_handle(RTAS_FNIDX__DISPLAY_CHARACTER)
#define RTAS_FN_EVENT_SCAN                        rtas_fn_handle(RTAS_FNIDX__EVENT_SCAN)
#define RTAS_FN_FREEZE_TIME_BASE                  rtas_fn_handle(RTAS_FNIDX__FREEZE_TIME_BASE)
#define RTAS_FN_GET_POWER_LEVEL                   rtas_fn_handle(RTAS_FNIDX__GET_POWER_LEVEL)
#define RTAS_FN_GET_SENSOR_STATE                  rtas_fn_handle(RTAS_FNIDX__GET_SENSOR_STATE)
#define RTAS_FN_GET_TERM_CHAR                     rtas_fn_handle(RTAS_FNIDX__GET_TERM_CHAR)
#define RTAS_FN_GET_TIME_OF_DAY                   rtas_fn_handle(RTAS_FNIDX__GET_TIME_OF_DAY)
#define RTAS_FN_IBM_ACTIVATE_FIRMWARE             rtas_fn_handle(RTAS_FNIDX__IBM_ACTIVATE_FIRMWARE)
#define RTAS_FN_IBM_CBE_START_PTCAL               rtas_fn_handle(RTAS_FNIDX__IBM_CBE_START_PTCAL)
#define RTAS_FN_IBM_CBE_STOP_PTCAL                rtas_fn_handle(RTAS_FNIDX__IBM_CBE_STOP_PTCAL)
#define RTAS_FN_IBM_CHANGE_MSI                    rtas_fn_handle(RTAS_FNIDX__IBM_CHANGE_MSI)
#define RTAS_FN_IBM_CLOSE_ERRINJCT                rtas_fn_handle(RTAS_FNIDX__IBM_CLOSE_ERRINJCT)
#define RTAS_FN_IBM_CONFIGURE_BRIDGE              rtas_fn_handle(RTAS_FNIDX__IBM_CONFIGURE_BRIDGE)
#define RTAS_FN_IBM_CONFIGURE_CONNECTOR           rtas_fn_handle(RTAS_FNIDX__IBM_CONFIGURE_CONNECTOR)
#define RTAS_FN_IBM_CONFIGURE_KERNEL_DUMP         rtas_fn_handle(RTAS_FNIDX__IBM_CONFIGURE_KERNEL_DUMP)
#define RTAS_FN_IBM_CONFIGURE_PE                  rtas_fn_handle(RTAS_FNIDX__IBM_CONFIGURE_PE)
#define RTAS_FN_IBM_CREATE_PE_DMA_WINDOW          rtas_fn_handle(RTAS_FNIDX__IBM_CREATE_PE_DMA_WINDOW)
#define RTAS_FN_IBM_DISPLAY_MESSAGE               rtas_fn_handle(RTAS_FNIDX__IBM_DISPLAY_MESSAGE)
#define RTAS_FN_IBM_ERRINJCT                      rtas_fn_handle(RTAS_FNIDX__IBM_ERRINJCT)
#define RTAS_FN_IBM_EXTI2C                        rtas_fn_handle(RTAS_FNIDX__IBM_EXTI2C)
#define RTAS_FN_IBM_GET_CONFIG_ADDR_INFO          rtas_fn_handle(RTAS_FNIDX__IBM_GET_CONFIG_ADDR_INFO)
#define RTAS_FN_IBM_GET_CONFIG_ADDR_INFO2         rtas_fn_handle(RTAS_FNIDX__IBM_GET_CONFIG_ADDR_INFO2)
#define RTAS_FN_IBM_GET_DYNAMIC_SENSOR_STATE      rtas_fn_handle(RTAS_FNIDX__IBM_GET_DYNAMIC_SENSOR_STATE)
#define RTAS_FN_IBM_GET_INDICES                   rtas_fn_handle(RTAS_FNIDX__IBM_GET_INDICES)
#define RTAS_FN_IBM_GET_RIO_TOPOLOGY              rtas_fn_handle(RTAS_FNIDX__IBM_GET_RIO_TOPOLOGY)
#define RTAS_FN_IBM_GET_SYSTEM_PARAMETER          rtas_fn_handle(RTAS_FNIDX__IBM_GET_SYSTEM_PARAMETER)
#define RTAS_FN_IBM_GET_VPD                       rtas_fn_handle(RTAS_FNIDX__IBM_GET_VPD)
#define RTAS_FN_IBM_GET_XIVE                      rtas_fn_handle(RTAS_FNIDX__IBM_GET_XIVE)
#define RTAS_FN_IBM_INT_OFF                       rtas_fn_handle(RTAS_FNIDX__IBM_INT_OFF)
#define RTAS_FN_IBM_INT_ON                        rtas_fn_handle(RTAS_FNIDX__IBM_INT_ON)
#define RTAS_FN_IBM_IO_QUIESCE_ACK                rtas_fn_handle(RTAS_FNIDX__IBM_IO_QUIESCE_ACK)
#define RTAS_FN_IBM_LPAR_PERFTOOLS                rtas_fn_handle(RTAS_FNIDX__IBM_LPAR_PERFTOOLS)
#define RTAS_FN_IBM_MANAGE_FLASH_IMAGE            rtas_fn_handle(RTAS_FNIDX__IBM_MANAGE_FLASH_IMAGE)
#define RTAS_FN_IBM_MANAGE_STORAGE_PRESERVATION   rtas_fn_handle(RTAS_FNIDX__IBM_MANAGE_STORAGE_PRESERVATION)
#define RTAS_FN_IBM_NMI_INTERLOCK                 rtas_fn_handle(RTAS_FNIDX__IBM_NMI_INTERLOCK)
#define RTAS_FN_IBM_NMI_REGISTER                  rtas_fn_handle(RTAS_FNIDX__IBM_NMI_REGISTER)
#define RTAS_FN_IBM_OPEN_ERRINJCT                 rtas_fn_handle(RTAS_FNIDX__IBM_OPEN_ERRINJCT)
#define RTAS_FN_IBM_OPEN_SRIOV_ALLOW_UNFREEZE     rtas_fn_handle(RTAS_FNIDX__IBM_OPEN_SRIOV_ALLOW_UNFREEZE)
#define RTAS_FN_IBM_OPEN_SRIOV_MAP_PE_NUMBER      rtas_fn_handle(RTAS_FNIDX__IBM_OPEN_SRIOV_MAP_PE_NUMBER)
#define RTAS_FN_IBM_OS_TERM                       rtas_fn_handle(RTAS_FNIDX__IBM_OS_TERM)
#define RTAS_FN_IBM_PARTNER_CONTROL               rtas_fn_handle(RTAS_FNIDX__IBM_PARTNER_CONTROL)
#define RTAS_FN_IBM_PHYSICAL_ATTESTATION          rtas_fn_handle(RTAS_FNIDX__IBM_PHYSICAL_ATTESTATION)
#define RTAS_FN_IBM_PLATFORM_DUMP                 rtas_fn_handle(RTAS_FNIDX__IBM_PLATFORM_DUMP)
#define RTAS_FN_IBM_POWER_OFF_UPS                 rtas_fn_handle(RTAS_FNIDX__IBM_POWER_OFF_UPS)
#define RTAS_FN_IBM_QUERY_INTERRUPT_SOURCE_NUMBER rtas_fn_handle(RTAS_FNIDX__IBM_QUERY_INTERRUPT_SOURCE_NUMBER)
#define RTAS_FN_IBM_QUERY_PE_DMA_WINDOW           rtas_fn_handle(RTAS_FNIDX__IBM_QUERY_PE_DMA_WINDOW)
#define RTAS_FN_IBM_READ_PCI_CONFIG               rtas_fn_handle(RTAS_FNIDX__IBM_READ_PCI_CONFIG)
#define RTAS_FN_IBM_READ_SLOT_RESET_STATE         rtas_fn_handle(RTAS_FNIDX__IBM_READ_SLOT_RESET_STATE)
#define RTAS_FN_IBM_READ_SLOT_RESET_STATE2        rtas_fn_handle(RTAS_FNIDX__IBM_READ_SLOT_RESET_STATE2)
#define RTAS_FN_IBM_REMOVE_PE_DMA_WINDOW          rtas_fn_handle(RTAS_FNIDX__IBM_REMOVE_PE_DMA_WINDOW)
#define RTAS_FN_IBM_RESET_PE_DMA_WINDOWS          rtas_fn_handle(RTAS_FNIDX__IBM_RESET_PE_DMA_WINDOWS)
#define RTAS_FN_IBM_SCAN_LOG_DUMP                 rtas_fn_handle(RTAS_FNIDX__IBM_SCAN_LOG_DUMP)
#define RTAS_FN_IBM_SET_DYNAMIC_INDICATOR         rtas_fn_handle(RTAS_FNIDX__IBM_SET_DYNAMIC_INDICATOR)
#define RTAS_FN_IBM_SET_EEH_OPTION                rtas_fn_handle(RTAS_FNIDX__IBM_SET_EEH_OPTION)
#define RTAS_FN_IBM_SET_SLOT_RESET                rtas_fn_handle(RTAS_FNIDX__IBM_SET_SLOT_RESET)
#define RTAS_FN_IBM_SET_SYSTEM_PARAMETER          rtas_fn_handle(RTAS_FNIDX__IBM_SET_SYSTEM_PARAMETER)
#define RTAS_FN_IBM_SET_XIVE                      rtas_fn_handle(RTAS_FNIDX__IBM_SET_XIVE)
#define RTAS_FN_IBM_SLOT_ERROR_DETAIL             rtas_fn_handle(RTAS_FNIDX__IBM_SLOT_ERROR_DETAIL)
#define RTAS_FN_IBM_SUSPEND_ME                    rtas_fn_handle(RTAS_FNIDX__IBM_SUSPEND_ME)
#define RTAS_FN_IBM_TUNE_DMA_PARMS                rtas_fn_handle(RTAS_FNIDX__IBM_TUNE_DMA_PARMS)
#define RTAS_FN_IBM_UPDATE_FLASH_64_AND_REBOOT    rtas_fn_handle(RTAS_FNIDX__IBM_UPDATE_FLASH_64_AND_REBOOT)
#define RTAS_FN_IBM_UPDATE_NODES                  rtas_fn_handle(RTAS_FNIDX__IBM_UPDATE_NODES)
#define RTAS_FN_IBM_UPDATE_PROPERTIES             rtas_fn_handle(RTAS_FNIDX__IBM_UPDATE_PROPERTIES)
#define RTAS_FN_IBM_VALIDATE_FLASH_IMAGE          rtas_fn_handle(RTAS_FNIDX__IBM_VALIDATE_FLASH_IMAGE)
#define RTAS_FN_IBM_WRITE_PCI_CONFIG              rtas_fn_handle(RTAS_FNIDX__IBM_WRITE_PCI_CONFIG)
#define RTAS_FN_NVRAM_FETCH                       rtas_fn_handle(RTAS_FNIDX__NVRAM_FETCH)
#define RTAS_FN_NVRAM_STORE                       rtas_fn_handle(RTAS_FNIDX__NVRAM_STORE)
#define RTAS_FN_POWER_OFF                         rtas_fn_handle(RTAS_FNIDX__POWER_OFF)
#define RTAS_FN_PUT_TERM_CHAR                     rtas_fn_handle(RTAS_FNIDX__PUT_TERM_CHAR)
#define RTAS_FN_QUERY_CPU_STOPPED_STATE           rtas_fn_handle(RTAS_FNIDX__QUERY_CPU_STOPPED_STATE)
#define RTAS_FN_READ_PCI_CONFIG                   rtas_fn_handle(RTAS_FNIDX__READ_PCI_CONFIG)
#define RTAS_FN_RTAS_LAST_ERROR                   rtas_fn_handle(RTAS_FNIDX__RTAS_LAST_ERROR)
#define RTAS_FN_SET_INDICATOR                     rtas_fn_handle(RTAS_FNIDX__SET_INDICATOR)
#define RTAS_FN_SET_POWER_LEVEL                   rtas_fn_handle(RTAS_FNIDX__SET_POWER_LEVEL)
#define RTAS_FN_SET_TIME_FOR_POWER_ON             rtas_fn_handle(RTAS_FNIDX__SET_TIME_FOR_POWER_ON)
#define RTAS_FN_SET_TIME_OF_DAY                   rtas_fn_handle(RTAS_FNIDX__SET_TIME_OF_DAY)
#define RTAS_FN_START_CPU                         rtas_fn_handle(RTAS_FNIDX__START_CPU)
#define RTAS_FN_STOP_SELF                         rtas_fn_handle(RTAS_FNIDX__STOP_SELF)
#define RTAS_FN_SYSTEM_REBOOT                     rtas_fn_handle(RTAS_FNIDX__SYSTEM_REBOOT)
#define RTAS_FN_THAW_TIME_BASE                    rtas_fn_handle(RTAS_FNIDX__THAW_TIME_BASE)
#define RTAS_FN_WRITE_PCI_CONFIG                  rtas_fn_handle(RTAS_FNIDX__WRITE_PCI_CONFIG)

#define RTAS_UNKNOWN_SERVICE (-1)
#define RTAS_INSTANTIATE_MAX (1ULL<<30) /* Don't instantiate rtas at/above this value */

/* Memory set aside for sys_rtas to use with calls that need a work area. */
#define RTAS_USER_REGION_SIZE (64 * 1024)

/*
 * Common RTAS function return values, derived from the table "RTAS
 * Status Word Values" in PAPR+ v2.13 7.2.8: "Return Codes". If a
 * function can return a value in this table then generally it has the
 * meaning listed here. More extended commentary in the documentation
 * for rtas_call().
 *
 * RTAS functions may use negative and positive numbers not in this
 * set for function-specific error and success conditions,
 * respectively.
 */
#define RTAS_SUCCESS                     0 /* Success. */
#define RTAS_HARDWARE_ERROR             -1 /* Hardware or other unspecified error. */
#define RTAS_BUSY                       -2 /* Retry immediately. */
#define RTAS_INVALID_PARAMETER          -3 /* Invalid indicator/domain/sensor etc. */
#define RTAS_UNEXPECTED_STATE_CHANGE    -7 /* Seems limited to EEH and slot reset. */
#define RTAS_EXTENDED_DELAY_MIN       9900 /* Retry after delaying for ~1ms. */
#define RTAS_EXTENDED_DELAY_MAX       9905 /* Retry after delaying for ~100s. */
#define RTAS_ML_ISOLATION_ERROR      -9000 /* Multi-level isolation error. */

/* statuses specific to ibm,suspend-me */
#define RTAS_SUSPEND_ABORTED     9000 /* Suspension aborted */
#define RTAS_NOT_SUSPENDABLE    -9004 /* Partition not suspendable */
#define RTAS_THREADS_ACTIVE     -9005 /* Multiple processor threads active */
#define RTAS_OUTSTANDING_COPROC -9006 /* Outstanding coprocessor operations */

/* RTAS event classes */
#define RTAS_INTERNAL_ERROR		0x80000000 /* set bit 0 */
#define RTAS_EPOW_WARNING		0x40000000 /* set bit 1 */
#define RTAS_HOTPLUG_EVENTS		0x10000000 /* set bit 3 */
#define RTAS_IO_EVENTS			0x08000000 /* set bit 4 */
#define RTAS_EVENT_SCAN_ALL_EVENTS	0xffffffff

/* RTAS event severity */
#define RTAS_SEVERITY_FATAL		0x5
#define RTAS_SEVERITY_ERROR		0x4
#define RTAS_SEVERITY_ERROR_SYNC	0x3
#define RTAS_SEVERITY_WARNING		0x2
#define RTAS_SEVERITY_EVENT		0x1
#define RTAS_SEVERITY_NO_ERROR		0x0

/* RTAS event disposition */
#define RTAS_DISP_FULLY_RECOVERED	0x0
#define RTAS_DISP_LIMITED_RECOVERY	0x1
#define RTAS_DISP_NOT_RECOVERED		0x2

/* RTAS event initiator */
#define RTAS_INITIATOR_UNKNOWN		0x0
#define RTAS_INITIATOR_CPU		0x1
#define RTAS_INITIATOR_PCI		0x2
#define RTAS_INITIATOR_ISA		0x3
#define RTAS_INITIATOR_MEMORY		0x4
#define RTAS_INITIATOR_POWERMGM		0x5

/* RTAS event target */
#define RTAS_TARGET_UNKNOWN		0x0
#define RTAS_TARGET_CPU			0x1
#define RTAS_TARGET_PCI			0x2
#define RTAS_TARGET_ISA			0x3
#define RTAS_TARGET_MEMORY		0x4
#define RTAS_TARGET_POWERMGM		0x5

/* RTAS event type */
#define RTAS_TYPE_RETRY			0x01
#define RTAS_TYPE_TCE_ERR		0x02
#define RTAS_TYPE_INTERN_DEV_FAIL	0x03
#define RTAS_TYPE_TIMEOUT		0x04
#define RTAS_TYPE_DATA_PARITY		0x05
#define RTAS_TYPE_ADDR_PARITY		0x06
#define RTAS_TYPE_CACHE_PARITY		0x07
#define RTAS_TYPE_ADDR_INVALID		0x08
#define RTAS_TYPE_ECC_UNCORR		0x09
#define RTAS_TYPE_ECC_CORR		0x0a
#define RTAS_TYPE_EPOW			0x40
#define RTAS_TYPE_PLATFORM		0xE0
#define RTAS_TYPE_IO			0xE1
#define RTAS_TYPE_INFO			0xE2
#define RTAS_TYPE_DEALLOC		0xE3
#define RTAS_TYPE_DUMP			0xE4
#define RTAS_TYPE_HOTPLUG		0xE5
/* I don't add PowerMGM events right now, this is a different topic */
#define RTAS_TYPE_PMGM_POWER_SW_ON	0x60
#define RTAS_TYPE_PMGM_POWER_SW_OFF	0x61
#define RTAS_TYPE_PMGM_LID_OPEN		0x62
#define RTAS_TYPE_PMGM_LID_CLOSE	0x63
#define RTAS_TYPE_PMGM_SLEEP_BTN	0x64
#define RTAS_TYPE_PMGM_WAKE_BTN		0x65
#define RTAS_TYPE_PMGM_BATTERY_WARN	0x66
#define RTAS_TYPE_PMGM_BATTERY_CRIT	0x67
#define RTAS_TYPE_PMGM_SWITCH_TO_BAT	0x68
#define RTAS_TYPE_PMGM_SWITCH_TO_AC	0x69
#define RTAS_TYPE_PMGM_KBD_OR_MOUSE	0x6a
#define RTAS_TYPE_PMGM_ENCLOS_OPEN	0x6b
#define RTAS_TYPE_PMGM_ENCLOS_CLOSED	0x6c
#define RTAS_TYPE_PMGM_RING_INDICATE	0x6d
#define RTAS_TYPE_PMGM_LAN_ATTENTION	0x6e
#define RTAS_TYPE_PMGM_TIME_ALARM	0x6f
#define RTAS_TYPE_PMGM_CONFIG_CHANGE	0x70
#define RTAS_TYPE_PMGM_SERVICE_PROC	0x71
/* Platform Resource Reassignment Notification */
#define RTAS_TYPE_PRRN			0xA0

/* RTAS check-exception vector offset */
#define RTAS_VECTOR_EXTERNAL_INTERRUPT	0x500

static inline uint8_t rtas_error_severity(const struct rtas_error_log *elog)
{
	return (elog->byte1 & 0xE0) >> 5;
}

static inline uint8_t rtas_error_disposition(const struct rtas_error_log *elog)
{
	return (elog->byte1 & 0x18) >> 3;
}

static inline
void rtas_set_disposition_recovered(struct rtas_error_log *elog)
{
	elog->byte1 &= ~0x18;
	elog->byte1 |= (RTAS_DISP_FULLY_RECOVERED << 3);
}

static inline uint8_t rtas_error_extended(const struct rtas_error_log *elog)
{
	return (elog->byte1 & 0x04) >> 2;
}

static inline uint8_t rtas_error_initiator(const struct rtas_error_log *elog)
{
	return (elog->byte2 & 0xf0) >> 4;
}

#define rtas_error_type(x)	((x)->byte3)

static inline
uint32_t rtas_error_extended_log_length(const struct rtas_error_log *elog)
{
	return be32_to_cpu(elog->extended_log_length);
}

#define RTAS_V6EXT_LOG_FORMAT_EVENT_LOG	14

#define RTAS_V6EXT_COMPANY_ID_IBM	(('I' << 24) | ('B' << 16) | ('M' << 8))

static
inline uint8_t rtas_ext_event_log_format(struct rtas_ext_event_log_v6 *ext_log)
{
	return ext_log->byte2 & 0x0F;
}

static
inline uint32_t rtas_ext_event_company_id(struct rtas_ext_event_log_v6 *ext_log)
{
	return be32_to_cpu(ext_log->company_id);
}

/* pSeries event log format */

/* Two bytes ASCII section IDs */
#define PSERIES_ELOG_SECT_ID_PRIV_HDR		(('P' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_USER_HDR		(('U' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_PRIMARY_SRC	(('P' << 8) | 'S')
#define PSERIES_ELOG_SECT_ID_EXTENDED_UH	(('E' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_FAILING_MTMS	(('M' << 8) | 'T')
#define PSERIES_ELOG_SECT_ID_SECONDARY_SRC	(('S' << 8) | 'S')
#define PSERIES_ELOG_SECT_ID_DUMP_LOCATOR	(('D' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_FW_ERROR		(('S' << 8) | 'W')
#define PSERIES_ELOG_SECT_ID_IMPACT_PART_ID	(('L' << 8) | 'P')
#define PSERIES_ELOG_SECT_ID_LOGIC_RESOURCE_ID	(('L' << 8) | 'R')
#define PSERIES_ELOG_SECT_ID_HMC_ID		(('H' << 8) | 'M')
#define PSERIES_ELOG_SECT_ID_EPOW		(('E' << 8) | 'P')
#define PSERIES_ELOG_SECT_ID_IO_EVENT		(('I' << 8) | 'E')
#define PSERIES_ELOG_SECT_ID_MANUFACT_INFO	(('M' << 8) | 'I')
#define PSERIES_ELOG_SECT_ID_CALL_HOME		(('C' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_USER_DEF		(('U' << 8) | 'D')
#define PSERIES_ELOG_SECT_ID_HOTPLUG		(('H' << 8) | 'P')
#define PSERIES_ELOG_SECT_ID_MCE		(('M' << 8) | 'C')

static
inline uint16_t pseries_errorlog_id(struct pseries_errorlog *sect)
{
	return be16_to_cpu(sect->id);
}

static
inline uint16_t pseries_errorlog_length(struct pseries_errorlog *sect)
{
	return be16_to_cpu(sect->length);
}

#define PSERIES_HP_ELOG_RESOURCE_CPU	1
#define PSERIES_HP_ELOG_RESOURCE_MEM	2
#define PSERIES_HP_ELOG_RESOURCE_SLOT	3
#define PSERIES_HP_ELOG_RESOURCE_PHB	4
#define PSERIES_HP_ELOG_RESOURCE_PMEM   6

#define PSERIES_HP_ELOG_ACTION_ADD	1
#define PSERIES_HP_ELOG_ACTION_REMOVE	2

#define PSERIES_HP_ELOG_ID_DRC_NAME	1
#define PSERIES_HP_ELOG_ID_DRC_INDEX	2
#define PSERIES_HP_ELOG_ID_DRC_COUNT	3
#define PSERIES_HP_ELOG_ID_DRC_IC	4

struct pseries_errorlog *get_pseries_errorlog(struct rtas_error_log *log,
					      uint16_t section_id);

/*
 * This can be set by the rtas_flash module so that it can get called
 * as the absolutely last thing before the kernel terminates.
 */
extern void (*rtas_flash_term_hook)(int);

extern struct rtas_t rtas;

s32 rtas_function_token(const rtas_fn_handle_t handle);
static inline bool rtas_function_implemented(const rtas_fn_handle_t handle)
{
	return rtas_function_token(handle) != RTAS_UNKNOWN_SERVICE;
}
int rtas_token(const char *service);
int rtas_call(int token, int nargs, int nret, int *outputs, ...);
void rtas_call_unlocked(struct rtas_args *args, int token, int nargs,
			int nret, ...);
void __noreturn rtas_restart(char *cmd);
void rtas_power_off(void);
void __noreturn rtas_halt(void);
void rtas_os_term(char *str);
void rtas_activate_firmware(void);
int rtas_get_sensor(int sensor, int index, int *state);
int rtas_get_sensor_fast(int sensor, int index, int *state);
int rtas_get_power_level(int powerdomain, int *level);
int rtas_set_power_level(int powerdomain, int level, int *setlevel);
bool rtas_indicator_present(int token, int *maxindex);
int rtas_set_indicator(int indicator, int index, int new_value);
int rtas_set_indicator_fast(int indicator, int index, int new_value);
void rtas_progress(char *s, unsigned short hex);
int rtas_ibm_suspend_me(int *fw_status);
int rtas_error_rc(int rtas_rc);

struct rtc_time;
time64_t rtas_get_boot_time(void);
void rtas_get_rtc_time(struct rtc_time *rtc_time);
int rtas_set_rtc_time(struct rtc_time *rtc_time);

unsigned int rtas_busy_delay_time(int status);
bool rtas_busy_delay(int status);

int early_init_dt_scan_rtas(unsigned long node, const char *uname, int depth, void *data);

void pSeries_log_error(char *buf, unsigned int err_type, int fatal);

#ifdef CONFIG_PPC_PSERIES
extern time64_t last_rtas_event;
int clobbering_unread_rtas_event(void);
int rtas_syscall_dispatch_ibm_suspend_me(u64 handle);
#else
static inline int clobbering_unread_rtas_event(void) { return 0; }
static inline int rtas_syscall_dispatch_ibm_suspend_me(u64 handle)
{
	return -EINVAL;
}
#endif

#ifdef CONFIG_PPC_RTAS_DAEMON
void rtas_cancel_event_scan(void);
#else
static inline void rtas_cancel_event_scan(void) { }
#endif

/* Error types logged.  */
#define ERR_FLAG_ALREADY_LOGGED	0x0
#define ERR_FLAG_BOOT		0x1	/* log was pulled from NVRAM on boot */
#define ERR_TYPE_RTAS_LOG	0x2	/* from rtas event-scan */
#define ERR_TYPE_KERNEL_PANIC	0x4	/* from die()/panic() */
#define ERR_TYPE_KERNEL_PANIC_GZ 0x8	/* ditto, compressed */

/* All the types and not flags */
#define ERR_TYPE_MASK \
	(ERR_TYPE_RTAS_LOG | ERR_TYPE_KERNEL_PANIC | ERR_TYPE_KERNEL_PANIC_GZ)

#define RTAS_DEBUG KERN_DEBUG "RTAS: "

#define RTAS_ERROR_LOG_MAX 2048

/*
 * Return the firmware-specified size of the error log buffer
 *  for all rtas calls that require an error buffer argument.
 *  This includes 'check-exception' and 'rtas-last-error'.
 */
int rtas_get_error_log_max(void);

/* Event Scan Parameters */
#define EVENT_SCAN_ALL_EVENTS	0xf0000000
#define SURVEILLANCE_TOKEN	9000
#define LOG_NUMBER		64		/* must be a power of two */
#define LOG_NUMBER_MASK		(LOG_NUMBER-1)

/* Some RTAS ops require a data buffer and that buffer must be < 4G.
 * Rather than having a memory allocator, just use this buffer
 * (get the lock first), make the RTAS call.  Copy the data instead
 * of holding the buffer for long.
 */

#define RTAS_DATA_BUF_SIZE 4096
extern spinlock_t rtas_data_buf_lock;
extern char rtas_data_buf[RTAS_DATA_BUF_SIZE];

/* RMO buffer reserved for user-space RTAS use */
extern unsigned long rtas_rmo_buf;

extern struct mutex rtas_ibm_get_vpd_lock;

#define GLOBAL_INTERRUPT_QUEUE 9005

/**
 * rtas_config_addr - Format a busno, devfn and reg for RTAS.
 * @busno: The bus number.
 * @devfn: The device and function number as encoded by PCI_DEVFN().
 * @reg: The register number.
 *
 * This function encodes the given busno, devfn and register number as
 * required for RTAS calls that take a "config_addr" parameter.
 * See PAPR requirement 7.3.4-1 for more info.
 */
static inline u32 rtas_config_addr(int busno, int devfn, int reg)
{
	return ((reg & 0xf00) << 20) | ((busno & 0xff) << 16) |
			(devfn << 8) | (reg & 0xff);
}

void rtas_give_timebase(void);
void rtas_take_timebase(void);

#ifdef CONFIG_PPC_RTAS
static inline int page_is_rtas_user_buf(unsigned long pfn)
{
	unsigned long paddr = (pfn << PAGE_SHIFT);
	if (paddr >= rtas_rmo_buf && paddr < (rtas_rmo_buf + RTAS_USER_REGION_SIZE))
		return 1;
	return 0;
}

/* Not the best place to put pSeries_coalesce_init, will be fixed when we
 * move some of the rtas suspend-me stuff to pseries */
void pSeries_coalesce_init(void);
void rtas_initialize(void);
#else
static inline int page_is_rtas_user_buf(unsigned long pfn) { return 0;}
static inline void pSeries_coalesce_init(void) { }
static inline void rtas_initialize(void) { }
#endif

#ifdef CONFIG_HV_PERF_CTRS
void read_24x7_sys_info(void);
#else
static inline void read_24x7_sys_info(void) { }
#endif

#endif /* __KERNEL__ */
#endif /* _POWERPC_RTAS_H */
