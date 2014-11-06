/*
 * PowerNV OPAL definitions.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __OPAL_H
#define __OPAL_H

#ifndef __ASSEMBLY__
/*
 * SG entry
 *
 * WARNING: The current implementation requires each entry
 * to represent a block that is 4k aligned *and* each block
 * size except the last one in the list to be as well.
 */
struct opal_sg_entry {
	__be64 data;
	__be64 length;
};

/* SG list */
struct opal_sg_list {
	__be64 length;
	__be64 next;
	struct opal_sg_entry entry[];
};

/* We calculate number of sg entries based on PAGE_SIZE */
#define SG_ENTRIES_PER_NODE ((PAGE_SIZE - 16) / sizeof(struct opal_sg_entry))

#endif /* __ASSEMBLY__ */

/****** OPAL APIs ******/

/* Return codes */
#define OPAL_SUCCESS 		0
#define OPAL_PARAMETER		-1
#define OPAL_BUSY		-2
#define OPAL_PARTIAL		-3
#define OPAL_CONSTRAINED	-4
#define OPAL_CLOSED		-5
#define OPAL_HARDWARE		-6
#define OPAL_UNSUPPORTED	-7
#define OPAL_PERMISSION		-8
#define OPAL_NO_MEM		-9
#define OPAL_RESOURCE		-10
#define OPAL_INTERNAL_ERROR	-11
#define OPAL_BUSY_EVENT		-12
#define OPAL_HARDWARE_FROZEN	-13
#define OPAL_WRONG_STATE	-14
#define OPAL_ASYNC_COMPLETION	-15

/* API Tokens (in r0) */
#define OPAL_INVALID_CALL			-1
#define OPAL_CONSOLE_WRITE			1
#define OPAL_CONSOLE_READ			2
#define OPAL_RTC_READ				3
#define OPAL_RTC_WRITE				4
#define OPAL_CEC_POWER_DOWN			5
#define OPAL_CEC_REBOOT				6
#define OPAL_READ_NVRAM				7
#define OPAL_WRITE_NVRAM			8
#define OPAL_HANDLE_INTERRUPT			9
#define OPAL_POLL_EVENTS			10
#define OPAL_PCI_SET_HUB_TCE_MEMORY		11
#define OPAL_PCI_SET_PHB_TCE_MEMORY		12
#define OPAL_PCI_CONFIG_READ_BYTE		13
#define OPAL_PCI_CONFIG_READ_HALF_WORD  	14
#define OPAL_PCI_CONFIG_READ_WORD		15
#define OPAL_PCI_CONFIG_WRITE_BYTE		16
#define OPAL_PCI_CONFIG_WRITE_HALF_WORD		17
#define OPAL_PCI_CONFIG_WRITE_WORD		18
#define OPAL_SET_XIVE				19
#define OPAL_GET_XIVE				20
#define OPAL_GET_COMPLETION_TOKEN_STATUS	21 /* obsolete */
#define OPAL_REGISTER_OPAL_EXCEPTION_HANDLER	22
#define OPAL_PCI_EEH_FREEZE_STATUS		23
#define OPAL_PCI_SHPC				24
#define OPAL_CONSOLE_WRITE_BUFFER_SPACE		25
#define OPAL_PCI_EEH_FREEZE_CLEAR		26
#define OPAL_PCI_PHB_MMIO_ENABLE		27
#define OPAL_PCI_SET_PHB_MEM_WINDOW		28
#define OPAL_PCI_MAP_PE_MMIO_WINDOW		29
#define OPAL_PCI_SET_PHB_TABLE_MEMORY		30
#define OPAL_PCI_SET_PE				31
#define OPAL_PCI_SET_PELTV			32
#define OPAL_PCI_SET_MVE			33
#define OPAL_PCI_SET_MVE_ENABLE			34
#define OPAL_PCI_GET_XIVE_REISSUE		35
#define OPAL_PCI_SET_XIVE_REISSUE		36
#define OPAL_PCI_SET_XIVE_PE			37
#define OPAL_GET_XIVE_SOURCE			38
#define OPAL_GET_MSI_32				39
#define OPAL_GET_MSI_64				40
#define OPAL_START_CPU				41
#define OPAL_QUERY_CPU_STATUS			42
#define OPAL_WRITE_OPPANEL			43
#define OPAL_PCI_MAP_PE_DMA_WINDOW		44
#define OPAL_PCI_MAP_PE_DMA_WINDOW_REAL		45
#define OPAL_PCI_RESET				49
#define OPAL_PCI_GET_HUB_DIAG_DATA		50
#define OPAL_PCI_GET_PHB_DIAG_DATA		51
#define OPAL_PCI_FENCE_PHB			52
#define OPAL_PCI_REINIT				53
#define OPAL_PCI_MASK_PE_ERROR			54
#define OPAL_SET_SLOT_LED_STATUS		55
#define OPAL_GET_EPOW_STATUS			56
#define OPAL_SET_SYSTEM_ATTENTION_LED		57
#define OPAL_RESERVED1				58
#define OPAL_RESERVED2				59
#define OPAL_PCI_NEXT_ERROR			60
#define OPAL_PCI_EEH_FREEZE_STATUS2		61
#define OPAL_PCI_POLL				62
#define OPAL_PCI_MSI_EOI			63
#define OPAL_PCI_GET_PHB_DIAG_DATA2		64
#define OPAL_XSCOM_READ				65
#define OPAL_XSCOM_WRITE			66
#define OPAL_LPC_READ				67
#define OPAL_LPC_WRITE				68
#define OPAL_RETURN_CPU				69
#define OPAL_REINIT_CPUS			70
#define OPAL_ELOG_READ				71
#define OPAL_ELOG_WRITE				72
#define OPAL_ELOG_ACK				73
#define OPAL_ELOG_RESEND			74
#define OPAL_ELOG_SIZE				75
#define OPAL_FLASH_VALIDATE			76
#define OPAL_FLASH_MANAGE			77
#define OPAL_FLASH_UPDATE			78
#define OPAL_RESYNC_TIMEBASE			79
#define OPAL_CHECK_TOKEN			80
#define OPAL_DUMP_INIT				81
#define OPAL_DUMP_INFO				82
#define OPAL_DUMP_READ				83
#define OPAL_DUMP_ACK				84
#define OPAL_GET_MSG				85
#define OPAL_CHECK_ASYNC_COMPLETION		86
#define OPAL_SYNC_HOST_REBOOT			87
#define OPAL_SENSOR_READ			88
#define OPAL_GET_PARAM				89
#define OPAL_SET_PARAM				90
#define OPAL_DUMP_RESEND			91
#define OPAL_PCI_SET_PHB_CXL_MODE		93
#define OPAL_DUMP_INFO2				94
#define OPAL_PCI_ERR_INJECT			96
#define OPAL_PCI_EEH_FREEZE_SET			97
#define OPAL_HANDLE_HMI				98
#define OPAL_REGISTER_DUMP_REGION		101
#define OPAL_UNREGISTER_DUMP_REGION		102
#define OPAL_IPMI_SEND				107
#define OPAL_IPMI_RECV				108

#ifndef __ASSEMBLY__

#include <linux/notifier.h>

/* Other enums */
enum OpalVendorApiTokens {
	OPAL_START_VENDOR_API_RANGE = 1000, OPAL_END_VENDOR_API_RANGE = 1999
};

enum OpalFreezeState {
	OPAL_EEH_STOPPED_NOT_FROZEN = 0,
	OPAL_EEH_STOPPED_MMIO_FREEZE = 1,
	OPAL_EEH_STOPPED_DMA_FREEZE = 2,
	OPAL_EEH_STOPPED_MMIO_DMA_FREEZE = 3,
	OPAL_EEH_STOPPED_RESET = 4,
	OPAL_EEH_STOPPED_TEMP_UNAVAIL = 5,
	OPAL_EEH_STOPPED_PERM_UNAVAIL = 6
};

enum OpalEehFreezeActionToken {
	OPAL_EEH_ACTION_CLEAR_FREEZE_MMIO = 1,
	OPAL_EEH_ACTION_CLEAR_FREEZE_DMA = 2,
	OPAL_EEH_ACTION_CLEAR_FREEZE_ALL = 3,

	OPAL_EEH_ACTION_SET_FREEZE_MMIO = 1,
	OPAL_EEH_ACTION_SET_FREEZE_DMA  = 2,
	OPAL_EEH_ACTION_SET_FREEZE_ALL  = 3
};

enum OpalPciStatusToken {
	OPAL_EEH_NO_ERROR	= 0,
	OPAL_EEH_IOC_ERROR	= 1,
	OPAL_EEH_PHB_ERROR	= 2,
	OPAL_EEH_PE_ERROR	= 3,
	OPAL_EEH_PE_MMIO_ERROR	= 4,
	OPAL_EEH_PE_DMA_ERROR	= 5
};

enum OpalPciErrorSeverity {
	OPAL_EEH_SEV_NO_ERROR	= 0,
	OPAL_EEH_SEV_IOC_DEAD	= 1,
	OPAL_EEH_SEV_PHB_DEAD	= 2,
	OPAL_EEH_SEV_PHB_FENCED	= 3,
	OPAL_EEH_SEV_PE_ER	= 4,
	OPAL_EEH_SEV_INF	= 5
};

enum OpalErrinjectType {
	OPAL_ERR_INJECT_TYPE_IOA_BUS_ERR	= 0,
	OPAL_ERR_INJECT_TYPE_IOA_BUS_ERR64	= 1,
};

enum OpalErrinjectFunc {
	/* IOA bus specific errors */
	OPAL_ERR_INJECT_FUNC_IOA_LD_MEM_ADDR	= 0,
	OPAL_ERR_INJECT_FUNC_IOA_LD_MEM_DATA	= 1,
	OPAL_ERR_INJECT_FUNC_IOA_LD_IO_ADDR	= 2,
	OPAL_ERR_INJECT_FUNC_IOA_LD_IO_DATA	= 3,
	OPAL_ERR_INJECT_FUNC_IOA_LD_CFG_ADDR	= 4,
	OPAL_ERR_INJECT_FUNC_IOA_LD_CFG_DATA	= 5,
	OPAL_ERR_INJECT_FUNC_IOA_ST_MEM_ADDR	= 6,
	OPAL_ERR_INJECT_FUNC_IOA_ST_MEM_DATA	= 7,
	OPAL_ERR_INJECT_FUNC_IOA_ST_IO_ADDR	= 8,
	OPAL_ERR_INJECT_FUNC_IOA_ST_IO_DATA	= 9,
	OPAL_ERR_INJECT_FUNC_IOA_ST_CFG_ADDR	= 10,
	OPAL_ERR_INJECT_FUNC_IOA_ST_CFG_DATA	= 11,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_RD_ADDR	= 12,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_RD_DATA	= 13,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_RD_MASTER	= 14,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_RD_TARGET	= 15,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_WR_ADDR	= 16,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_WR_DATA	= 17,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_WR_MASTER	= 18,
	OPAL_ERR_INJECT_FUNC_IOA_DMA_WR_TARGET	= 19,
};

enum OpalShpcAction {
	OPAL_SHPC_GET_LINK_STATE = 0,
	OPAL_SHPC_GET_SLOT_STATE = 1
};

enum OpalShpcLinkState {
	OPAL_SHPC_LINK_DOWN = 0,
	OPAL_SHPC_LINK_UP = 1
};

enum OpalMmioWindowType {
	OPAL_M32_WINDOW_TYPE = 1,
	OPAL_M64_WINDOW_TYPE = 2,
	OPAL_IO_WINDOW_TYPE = 3
};

enum OpalShpcSlotState {
	OPAL_SHPC_DEV_NOT_PRESENT = 0,
	OPAL_SHPC_DEV_PRESENT = 1
};

enum OpalExceptionHandler {
	OPAL_MACHINE_CHECK_HANDLER = 1,
	OPAL_HYPERVISOR_MAINTENANCE_HANDLER = 2,
	OPAL_SOFTPATCH_HANDLER = 3
};

enum OpalPendingState {
	OPAL_EVENT_OPAL_INTERNAL	= 0x1,
	OPAL_EVENT_NVRAM		= 0x2,
	OPAL_EVENT_RTC			= 0x4,
	OPAL_EVENT_CONSOLE_OUTPUT	= 0x8,
	OPAL_EVENT_CONSOLE_INPUT	= 0x10,
	OPAL_EVENT_ERROR_LOG_AVAIL	= 0x20,
	OPAL_EVENT_ERROR_LOG		= 0x40,
	OPAL_EVENT_EPOW			= 0x80,
	OPAL_EVENT_LED_STATUS		= 0x100,
	OPAL_EVENT_PCI_ERROR		= 0x200,
	OPAL_EVENT_DUMP_AVAIL		= 0x400,
	OPAL_EVENT_MSG_PENDING		= 0x800,
};

enum OpalMessageType {
	OPAL_MSG_ASYNC_COMP = 0,	/* params[0] = token, params[1] = rc,
					 * additional params function-specific
					 */
	OPAL_MSG_MEM_ERR,
	OPAL_MSG_EPOW,
	OPAL_MSG_SHUTDOWN,
	OPAL_MSG_HMI_EVT,
	OPAL_MSG_TYPE_MAX,
};

/* Machine check related definitions */
enum OpalMCE_Version {
	OpalMCE_V1 = 1,
};

enum OpalMCE_Severity {
	OpalMCE_SEV_NO_ERROR = 0,
	OpalMCE_SEV_WARNING = 1,
	OpalMCE_SEV_ERROR_SYNC = 2,
	OpalMCE_SEV_FATAL = 3,
};

enum OpalMCE_Disposition {
	OpalMCE_DISPOSITION_RECOVERED = 0,
	OpalMCE_DISPOSITION_NOT_RECOVERED = 1,
};

enum OpalMCE_Initiator {
	OpalMCE_INITIATOR_UNKNOWN = 0,
	OpalMCE_INITIATOR_CPU = 1,
};

enum OpalMCE_ErrorType {
	OpalMCE_ERROR_TYPE_UNKNOWN = 0,
	OpalMCE_ERROR_TYPE_UE = 1,
	OpalMCE_ERROR_TYPE_SLB = 2,
	OpalMCE_ERROR_TYPE_ERAT = 3,
	OpalMCE_ERROR_TYPE_TLB = 4,
};

enum OpalMCE_UeErrorType {
	OpalMCE_UE_ERROR_INDETERMINATE = 0,
	OpalMCE_UE_ERROR_IFETCH = 1,
	OpalMCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH = 2,
	OpalMCE_UE_ERROR_LOAD_STORE = 3,
	OpalMCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE = 4,
};

enum OpalMCE_SlbErrorType {
	OpalMCE_SLB_ERROR_INDETERMINATE = 0,
	OpalMCE_SLB_ERROR_PARITY = 1,
	OpalMCE_SLB_ERROR_MULTIHIT = 2,
};

enum OpalMCE_EratErrorType {
	OpalMCE_ERAT_ERROR_INDETERMINATE = 0,
	OpalMCE_ERAT_ERROR_PARITY = 1,
	OpalMCE_ERAT_ERROR_MULTIHIT = 2,
};

enum OpalMCE_TlbErrorType {
	OpalMCE_TLB_ERROR_INDETERMINATE = 0,
	OpalMCE_TLB_ERROR_PARITY = 1,
	OpalMCE_TLB_ERROR_MULTIHIT = 2,
};

enum OpalThreadStatus {
	OPAL_THREAD_INACTIVE = 0x0,
	OPAL_THREAD_STARTED = 0x1,
	OPAL_THREAD_UNAVAILABLE = 0x2 /* opal-v3 */
};

enum OpalPciBusCompare {
	OpalPciBusAny	= 0,	/* Any bus number match */
	OpalPciBus3Bits	= 2,	/* Match top 3 bits of bus number */
	OpalPciBus4Bits	= 3,	/* Match top 4 bits of bus number */
	OpalPciBus5Bits	= 4,	/* Match top 5 bits of bus number */
	OpalPciBus6Bits	= 5,	/* Match top 6 bits of bus number */
	OpalPciBus7Bits	= 6,	/* Match top 7 bits of bus number */
	OpalPciBusAll	= 7,	/* Match bus number exactly */
};

enum OpalDeviceCompare {
	OPAL_IGNORE_RID_DEVICE_NUMBER = 0,
	OPAL_COMPARE_RID_DEVICE_NUMBER = 1
};

enum OpalFuncCompare {
	OPAL_IGNORE_RID_FUNCTION_NUMBER = 0,
	OPAL_COMPARE_RID_FUNCTION_NUMBER = 1
};

enum OpalPeAction {
	OPAL_UNMAP_PE = 0,
	OPAL_MAP_PE = 1
};

enum OpalPeltvAction {
	OPAL_REMOVE_PE_FROM_DOMAIN = 0,
	OPAL_ADD_PE_TO_DOMAIN = 1
};

enum OpalMveEnableAction {
	OPAL_DISABLE_MVE = 0,
	OPAL_ENABLE_MVE = 1
};

enum OpalM64EnableAction {
	OPAL_DISABLE_M64 = 0,
	OPAL_ENABLE_M64_SPLIT = 1,
	OPAL_ENABLE_M64_NON_SPLIT = 2
};

enum OpalPciResetScope {
	OPAL_RESET_PHB_COMPLETE		= 1,
	OPAL_RESET_PCI_LINK		= 2,
	OPAL_RESET_PHB_ERROR		= 3,
	OPAL_RESET_PCI_HOT		= 4,
	OPAL_RESET_PCI_FUNDAMENTAL	= 5,
	OPAL_RESET_PCI_IODA_TABLE	= 6
};

enum OpalPciReinitScope {
	OPAL_REINIT_PCI_DEV = 1000
};

enum OpalPciResetState {
	OPAL_DEASSERT_RESET = 0,
	OPAL_ASSERT_RESET = 1
};

enum OpalPciMaskAction {
	OPAL_UNMASK_ERROR_TYPE = 0,
	OPAL_MASK_ERROR_TYPE = 1
};

enum OpalSlotLedType {
	OPAL_SLOT_LED_ID_TYPE = 0,
	OPAL_SLOT_LED_FAULT_TYPE = 1
};

enum OpalLedAction {
	OPAL_TURN_OFF_LED = 0,
	OPAL_TURN_ON_LED = 1,
	OPAL_QUERY_LED_STATE_AFTER_BUSY = 2
};

enum OpalEpowStatus {
	OPAL_EPOW_NONE = 0,
	OPAL_EPOW_UPS = 1,
	OPAL_EPOW_OVER_AMBIENT_TEMP = 2,
	OPAL_EPOW_OVER_INTERNAL_TEMP = 3
};

/*
 * Address cycle types for LPC accesses. These also correspond
 * to the content of the first cell of the "reg" property for
 * device nodes on the LPC bus
 */
enum OpalLPCAddressType {
	OPAL_LPC_MEM	= 0,
	OPAL_LPC_IO	= 1,
	OPAL_LPC_FW	= 2,
};

/* System parameter permission */
enum OpalSysparamPerm {
	OPAL_SYSPARAM_READ      = 0x1,
	OPAL_SYSPARAM_WRITE     = 0x2,
	OPAL_SYSPARAM_RW        = (OPAL_SYSPARAM_READ | OPAL_SYSPARAM_WRITE),
};

struct opal_msg {
	__be32 msg_type;
	__be32 reserved;
	__be64 params[8];
};

enum {
	OPAL_IPMI_MSG_FORMAT_VERSION_1 = 1,
};

struct opal_ipmi_msg {
	uint8_t		version;
	uint8_t		netfn;
	uint8_t		cmd;
	uint8_t		data[];
};

struct opal_machine_check_event {
	enum OpalMCE_Version	version:8;	/* 0x00 */
	uint8_t			in_use;		/* 0x01 */
	enum OpalMCE_Severity	severity:8;	/* 0x02 */
	enum OpalMCE_Initiator	initiator:8;	/* 0x03 */
	enum OpalMCE_ErrorType	error_type:8;	/* 0x04 */
	enum OpalMCE_Disposition disposition:8; /* 0x05 */
	uint8_t			reserved_1[2];	/* 0x06 */
	uint64_t		gpr3;		/* 0x08 */
	uint64_t		srr0;		/* 0x10 */
	uint64_t		srr1;		/* 0x18 */
	union {					/* 0x20 */
		struct {
			enum OpalMCE_UeErrorType ue_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		physical_address_provided;
			uint8_t		reserved_1[5];
			uint64_t	effective_address;
			uint64_t	physical_address;
			uint8_t		reserved_2[8];
		} ue_error;

		struct {
			enum OpalMCE_SlbErrorType slb_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} slb_error;

		struct {
			enum OpalMCE_EratErrorType erat_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} erat_error;

		struct {
			enum OpalMCE_TlbErrorType tlb_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} tlb_error;
	} u;
};

/* FSP memory errors handling */
enum OpalMemErr_Version {
	OpalMemErr_V1 = 1,
};

enum OpalMemErrType {
	OPAL_MEM_ERR_TYPE_RESILIENCE	= 0,
	OPAL_MEM_ERR_TYPE_DYN_DALLOC,
	OPAL_MEM_ERR_TYPE_SCRUB,
};

/* Memory Reilience error type */
enum OpalMemErr_ResilErrType {
	OPAL_MEM_RESILIENCE_CE		= 0,
	OPAL_MEM_RESILIENCE_UE,
	OPAL_MEM_RESILIENCE_UE_SCRUB,
};

/* Dynamic Memory Deallocation type */
enum OpalMemErr_DynErrType {
	OPAL_MEM_DYNAMIC_DEALLOC	= 0,
};

/* OpalMemoryErrorData->flags */
#define OPAL_MEM_CORRECTED_ERROR	0x0001
#define OPAL_MEM_THRESHOLD_EXCEEDED	0x0002
#define OPAL_MEM_ACK_REQUIRED		0x8000

struct OpalMemoryErrorData {
	enum OpalMemErr_Version	version:8;	/* 0x00 */
	enum OpalMemErrType	type:8;		/* 0x01 */
	__be16			flags;		/* 0x02 */
	uint8_t			reserved_1[4];	/* 0x04 */

	union {
		/* Memory Resilience corrected/uncorrected error info */
		struct {
			enum OpalMemErr_ResilErrType resil_err_type:8;
			uint8_t		reserved_1[7];
			__be64		physical_address_start;
			__be64		physical_address_end;
		} resilience;
		/* Dynamic memory deallocation error info */
		struct {
			enum OpalMemErr_DynErrType dyn_err_type:8;
			uint8_t		reserved_1[7];
			__be64		physical_address_start;
			__be64		physical_address_end;
		} dyn_dealloc;
	} u;
};

/* HMI interrupt event */
enum OpalHMI_Version {
	OpalHMIEvt_V1 = 1,
};

enum OpalHMI_Severity {
	OpalHMI_SEV_NO_ERROR = 0,
	OpalHMI_SEV_WARNING = 1,
	OpalHMI_SEV_ERROR_SYNC = 2,
	OpalHMI_SEV_FATAL = 3,
};

enum OpalHMI_Disposition {
	OpalHMI_DISPOSITION_RECOVERED = 0,
	OpalHMI_DISPOSITION_NOT_RECOVERED = 1,
};

enum OpalHMI_ErrType {
	OpalHMI_ERROR_MALFUNC_ALERT	= 0,
	OpalHMI_ERROR_PROC_RECOV_DONE,
	OpalHMI_ERROR_PROC_RECOV_DONE_AGAIN,
	OpalHMI_ERROR_PROC_RECOV_MASKED,
	OpalHMI_ERROR_TFAC,
	OpalHMI_ERROR_TFMR_PARITY,
	OpalHMI_ERROR_HA_OVERFLOW_WARN,
	OpalHMI_ERROR_XSCOM_FAIL,
	OpalHMI_ERROR_XSCOM_DONE,
	OpalHMI_ERROR_SCOM_FIR,
	OpalHMI_ERROR_DEBUG_TRIG_FIR,
	OpalHMI_ERROR_HYP_RESOURCE,
};

struct OpalHMIEvent {
	uint8_t		version;	/* 0x00 */
	uint8_t		severity;	/* 0x01 */
	uint8_t		type;		/* 0x02 */
	uint8_t		disposition;	/* 0x03 */
	uint8_t		reserved_1[4];	/* 0x04 */

	__be64		hmer;
	/* TFMR register. Valid only for TFAC and TFMR_PARITY error type. */
	__be64		tfmr;
};

enum {
	OPAL_P7IOC_DIAG_TYPE_NONE	= 0,
	OPAL_P7IOC_DIAG_TYPE_RGC	= 1,
	OPAL_P7IOC_DIAG_TYPE_BI		= 2,
	OPAL_P7IOC_DIAG_TYPE_CI		= 3,
	OPAL_P7IOC_DIAG_TYPE_MISC	= 4,
	OPAL_P7IOC_DIAG_TYPE_I2C	= 5,
	OPAL_P7IOC_DIAG_TYPE_LAST	= 6
};

struct OpalIoP7IOCErrorData {
	__be16 type;

	/* GEM */
	__be64 gemXfir;
	__be64 gemRfir;
	__be64 gemRirqfir;
	__be64 gemMask;
	__be64 gemRwof;

	/* LEM */
	__be64 lemFir;
	__be64 lemErrMask;
	__be64 lemAction0;
	__be64 lemAction1;
	__be64 lemWof;

	union {
		struct OpalIoP7IOCRgcErrorData {
			__be64 rgcStatus;	/* 3E1C10 */
			__be64 rgcLdcp;		/* 3E1C18 */
		}rgc;
		struct OpalIoP7IOCBiErrorData {
			__be64 biLdcp0;		/* 3C0100, 3C0118 */
			__be64 biLdcp1;		/* 3C0108, 3C0120 */
			__be64 biLdcp2;		/* 3C0110, 3C0128 */
			__be64 biFenceStatus;	/* 3C0130, 3C0130 */

			    u8 biDownbound;	/* BI Downbound or Upbound */
		}bi;
		struct OpalIoP7IOCCiErrorData {
			__be64 ciPortStatus;	/* 3Dn008 */
			__be64 ciPortLdcp;	/* 3Dn010 */

			    u8 ciPort;		/* Index of CI port: 0/1 */
		}ci;
	};
};

/**
 * This structure defines the overlay which will be used to store PHB error
 * data upon request.
 */
enum {
	OPAL_PHB_ERROR_DATA_VERSION_1 = 1,
};

enum {
	OPAL_PHB_ERROR_DATA_TYPE_P7IOC = 1,
	OPAL_PHB_ERROR_DATA_TYPE_PHB3 = 2
};

enum {
	OPAL_P7IOC_NUM_PEST_REGS = 128,
	OPAL_PHB3_NUM_PEST_REGS = 256
};

struct OpalIoPhbErrorCommon {
	__be32 version;
	__be32 ioType;
	__be32 len;
};

struct OpalIoP7IOCPhbErrorData {
	struct OpalIoPhbErrorCommon common;

	__be32 brdgCtl;

	// P7IOC utl regs
	__be32 portStatusReg;
	__be32 rootCmplxStatus;
	__be32 busAgentStatus;

	// P7IOC cfg regs
	__be32 deviceStatus;
	__be32 slotStatus;
	__be32 linkStatus;
	__be32 devCmdStatus;
	__be32 devSecStatus;

	// cfg AER regs
	__be32 rootErrorStatus;
	__be32 uncorrErrorStatus;
	__be32 corrErrorStatus;
	__be32 tlpHdr1;
	__be32 tlpHdr2;
	__be32 tlpHdr3;
	__be32 tlpHdr4;
	__be32 sourceId;

	__be32 rsv3;

	// Record data about the call to allocate a buffer.
	__be64 errorClass;
	__be64 correlator;

	//P7IOC MMIO Error Regs
	__be64 p7iocPlssr;                // n120
	__be64 p7iocCsr;                  // n110
	__be64 lemFir;                    // nC00
	__be64 lemErrorMask;              // nC18
	__be64 lemWOF;                    // nC40
	__be64 phbErrorStatus;            // nC80
	__be64 phbFirstErrorStatus;       // nC88
	__be64 phbErrorLog0;              // nCC0
	__be64 phbErrorLog1;              // nCC8
	__be64 mmioErrorStatus;           // nD00
	__be64 mmioFirstErrorStatus;      // nD08
	__be64 mmioErrorLog0;             // nD40
	__be64 mmioErrorLog1;             // nD48
	__be64 dma0ErrorStatus;           // nD80
	__be64 dma0FirstErrorStatus;      // nD88
	__be64 dma0ErrorLog0;             // nDC0
	__be64 dma0ErrorLog1;             // nDC8
	__be64 dma1ErrorStatus;           // nE00
	__be64 dma1FirstErrorStatus;      // nE08
	__be64 dma1ErrorLog0;             // nE40
	__be64 dma1ErrorLog1;             // nE48
	__be64 pestA[OPAL_P7IOC_NUM_PEST_REGS];
	__be64 pestB[OPAL_P7IOC_NUM_PEST_REGS];
};

struct OpalIoPhb3ErrorData {
	struct OpalIoPhbErrorCommon common;

	__be32 brdgCtl;

	/* PHB3 UTL regs */
	__be32 portStatusReg;
	__be32 rootCmplxStatus;
	__be32 busAgentStatus;

	/* PHB3 cfg regs */
	__be32 deviceStatus;
	__be32 slotStatus;
	__be32 linkStatus;
	__be32 devCmdStatus;
	__be32 devSecStatus;

	/* cfg AER regs */
	__be32 rootErrorStatus;
	__be32 uncorrErrorStatus;
	__be32 corrErrorStatus;
	__be32 tlpHdr1;
	__be32 tlpHdr2;
	__be32 tlpHdr3;
	__be32 tlpHdr4;
	__be32 sourceId;

	__be32 rsv3;

	/* Record data about the call to allocate a buffer */
	__be64 errorClass;
	__be64 correlator;

	__be64 nFir;			/* 000 */
	__be64 nFirMask;		/* 003 */
	__be64 nFirWOF;		/* 008 */

	/* PHB3 MMIO Error Regs */
	__be64 phbPlssr;		/* 120 */
	__be64 phbCsr;		/* 110 */
	__be64 lemFir;		/* C00 */
	__be64 lemErrorMask;		/* C18 */
	__be64 lemWOF;		/* C40 */
	__be64 phbErrorStatus;	/* C80 */
	__be64 phbFirstErrorStatus;	/* C88 */
	__be64 phbErrorLog0;		/* CC0 */
	__be64 phbErrorLog1;		/* CC8 */
	__be64 mmioErrorStatus;	/* D00 */
	__be64 mmioFirstErrorStatus;	/* D08 */
	__be64 mmioErrorLog0;		/* D40 */
	__be64 mmioErrorLog1;		/* D48 */
	__be64 dma0ErrorStatus;	/* D80 */
	__be64 dma0FirstErrorStatus;	/* D88 */
	__be64 dma0ErrorLog0;		/* DC0 */
	__be64 dma0ErrorLog1;		/* DC8 */
	__be64 dma1ErrorStatus;	/* E00 */
	__be64 dma1FirstErrorStatus;	/* E08 */
	__be64 dma1ErrorLog0;		/* E40 */
	__be64 dma1ErrorLog1;		/* E48 */
	__be64 pestA[OPAL_PHB3_NUM_PEST_REGS];
	__be64 pestB[OPAL_PHB3_NUM_PEST_REGS];
};

enum {
	OPAL_REINIT_CPUS_HILE_BE	= (1 << 0),
	OPAL_REINIT_CPUS_HILE_LE	= (1 << 1),
};

typedef struct oppanel_line {
	const char * 	line;
	uint64_t 	line_len;
} oppanel_line_t;

/* /sys/firmware/opal */
extern struct kobject *opal_kobj;

/* /ibm,opal */
extern struct device_node *opal_node;

/* API functions */
int64_t opal_invalid_call(void);
int64_t opal_console_write(int64_t term_number, __be64 *length,
			   const uint8_t *buffer);
int64_t opal_console_read(int64_t term_number, __be64 *length,
			  uint8_t *buffer);
int64_t opal_console_write_buffer_space(int64_t term_number,
					__be64 *length);
int64_t opal_rtc_read(__be32 *year_month_day,
		      __be64 *hour_minute_second_millisecond);
int64_t opal_rtc_write(uint32_t year_month_day,
		       uint64_t hour_minute_second_millisecond);
int64_t opal_cec_power_down(uint64_t request);
int64_t opal_cec_reboot(void);
int64_t opal_read_nvram(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_write_nvram(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_handle_interrupt(uint64_t isn, __be64 *outstanding_event_mask);
int64_t opal_poll_events(__be64 *outstanding_event_mask);
int64_t opal_pci_set_hub_tce_memory(uint64_t hub_id, uint64_t tce_mem_addr,
				    uint64_t tce_mem_size);
int64_t opal_pci_set_phb_tce_memory(uint64_t phb_id, uint64_t tce_mem_addr,
				    uint64_t tce_mem_size);
int64_t opal_pci_config_read_byte(uint64_t phb_id, uint64_t bus_dev_func,
				  uint64_t offset, uint8_t *data);
int64_t opal_pci_config_read_half_word(uint64_t phb_id, uint64_t bus_dev_func,
				       uint64_t offset, __be16 *data);
int64_t opal_pci_config_read_word(uint64_t phb_id, uint64_t bus_dev_func,
				  uint64_t offset, __be32 *data);
int64_t opal_pci_config_write_byte(uint64_t phb_id, uint64_t bus_dev_func,
				   uint64_t offset, uint8_t data);
int64_t opal_pci_config_write_half_word(uint64_t phb_id, uint64_t bus_dev_func,
					uint64_t offset, uint16_t data);
int64_t opal_pci_config_write_word(uint64_t phb_id, uint64_t bus_dev_func,
				   uint64_t offset, uint32_t data);
int64_t opal_set_xive(uint32_t isn, uint16_t server, uint8_t priority);
int64_t opal_get_xive(uint32_t isn, __be16 *server, uint8_t *priority);
int64_t opal_register_exception_handler(uint64_t opal_exception,
					uint64_t handler_address,
					uint64_t glue_cache_line);
int64_t opal_pci_eeh_freeze_status(uint64_t phb_id, uint64_t pe_number,
				   uint8_t *freeze_state,
				   __be16 *pci_error_type,
				   __be64 *phb_status);
int64_t opal_pci_eeh_freeze_clear(uint64_t phb_id, uint64_t pe_number,
				  uint64_t eeh_action_token);
int64_t opal_pci_eeh_freeze_set(uint64_t phb_id, uint64_t pe_number,
				uint64_t eeh_action_token);
int64_t opal_pci_err_inject(uint64_t phb_id, uint32_t pe_no, uint32_t type,
			    uint32_t func, uint64_t addr, uint64_t mask);
int64_t opal_pci_shpc(uint64_t phb_id, uint64_t shpc_action, uint8_t *state);



int64_t opal_pci_phb_mmio_enable(uint64_t phb_id, uint16_t window_type,
				 uint16_t window_num, uint16_t enable);
int64_t opal_pci_set_phb_mem_window(uint64_t phb_id, uint16_t window_type,
				    uint16_t window_num,
				    uint64_t starting_real_address,
				    uint64_t starting_pci_address,
				    uint64_t size);
int64_t opal_pci_map_pe_mmio_window(uint64_t phb_id, uint16_t pe_number,
				    uint16_t window_type, uint16_t window_num,
				    uint16_t segment_num);
int64_t opal_pci_set_phb_table_memory(uint64_t phb_id, uint64_t rtt_addr,
				      uint64_t ivt_addr, uint64_t ivt_len,
				      uint64_t reject_array_addr,
				      uint64_t peltv_addr);
int64_t opal_pci_set_pe(uint64_t phb_id, uint64_t pe_number, uint64_t bus_dev_func,
			uint8_t bus_compare, uint8_t dev_compare, uint8_t func_compare,
			uint8_t pe_action);
int64_t opal_pci_set_peltv(uint64_t phb_id, uint32_t parent_pe, uint32_t child_pe,
			   uint8_t state);
int64_t opal_pci_set_mve(uint64_t phb_id, uint32_t mve_number, uint32_t pe_number);
int64_t opal_pci_set_mve_enable(uint64_t phb_id, uint32_t mve_number,
				uint32_t state);
int64_t opal_pci_get_xive_reissue(uint64_t phb_id, uint32_t xive_number,
				  uint8_t *p_bit, uint8_t *q_bit);
int64_t opal_pci_set_xive_reissue(uint64_t phb_id, uint32_t xive_number,
				  uint8_t p_bit, uint8_t q_bit);
int64_t opal_pci_msi_eoi(uint64_t phb_id, uint32_t hw_irq);
int64_t opal_pci_set_xive_pe(uint64_t phb_id, uint32_t pe_number,
			     uint32_t xive_num);
int64_t opal_get_xive_source(uint64_t phb_id, uint32_t xive_num,
			     __be32 *interrupt_source_number);
int64_t opal_get_msi_32(uint64_t phb_id, uint32_t mve_number, uint32_t xive_num,
			uint8_t msi_range, __be32 *msi_address,
			__be32 *message_data);
int64_t opal_get_msi_64(uint64_t phb_id, uint32_t mve_number,
			uint32_t xive_num, uint8_t msi_range,
			__be64 *msi_address, __be32 *message_data);
int64_t opal_start_cpu(uint64_t thread_number, uint64_t start_address);
int64_t opal_query_cpu_status(uint64_t thread_number, uint8_t *thread_status);
int64_t opal_write_oppanel(oppanel_line_t *lines, uint64_t num_lines);
int64_t opal_pci_map_pe_dma_window(uint64_t phb_id, uint16_t pe_number, uint16_t window_id,
				   uint16_t tce_levels, uint64_t tce_table_addr,
				   uint64_t tce_table_size, uint64_t tce_page_size);
int64_t opal_pci_map_pe_dma_window_real(uint64_t phb_id, uint16_t pe_number,
					uint16_t dma_window_number, uint64_t pci_start_addr,
					uint64_t pci_mem_size);
int64_t opal_pci_reset(uint64_t phb_id, uint8_t reset_scope, uint8_t assert_state);

int64_t opal_pci_get_hub_diag_data(uint64_t hub_id, void *diag_buffer,
				   uint64_t diag_buffer_len);
int64_t opal_pci_get_phb_diag_data(uint64_t phb_id, void *diag_buffer,
				   uint64_t diag_buffer_len);
int64_t opal_pci_get_phb_diag_data2(uint64_t phb_id, void *diag_buffer,
				    uint64_t diag_buffer_len);
int64_t opal_pci_fence_phb(uint64_t phb_id);
int64_t opal_pci_reinit(uint64_t phb_id, uint64_t reinit_scope, uint64_t data);
int64_t opal_pci_mask_pe_error(uint64_t phb_id, uint16_t pe_number, uint8_t error_type, uint8_t mask_action);
int64_t opal_set_slot_led_status(uint64_t phb_id, uint64_t slot_id, uint8_t led_type, uint8_t led_action);
int64_t opal_get_epow_status(__be64 *status);
int64_t opal_set_system_attention_led(uint8_t led_action);
int64_t opal_pci_next_error(uint64_t phb_id, __be64 *first_frozen_pe,
			    __be16 *pci_error_type, __be16 *severity);
int64_t opal_pci_poll(uint64_t phb_id);
int64_t opal_return_cpu(void);
int64_t opal_check_token(uint64_t token);
int64_t opal_reinit_cpus(uint64_t flags);

int64_t opal_xscom_read(uint32_t gcid, uint64_t pcb_addr, __be64 *val);
int64_t opal_xscom_write(uint32_t gcid, uint64_t pcb_addr, uint64_t val);

int64_t opal_lpc_write(uint32_t chip_id, enum OpalLPCAddressType addr_type,
		       uint32_t addr, uint32_t data, uint32_t sz);
int64_t opal_lpc_read(uint32_t chip_id, enum OpalLPCAddressType addr_type,
		      uint32_t addr, __be32 *data, uint32_t sz);

int64_t opal_read_elog(uint64_t buffer, uint64_t size, uint64_t log_id);
int64_t opal_get_elog_size(__be64 *log_id, __be64 *size, __be64 *elog_type);
int64_t opal_write_elog(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_send_ack_elog(uint64_t log_id);
void opal_resend_pending_logs(void);

int64_t opal_validate_flash(uint64_t buffer, uint32_t *size, uint32_t *result);
int64_t opal_manage_flash(uint8_t op);
int64_t opal_update_flash(uint64_t blk_list);
int64_t opal_dump_init(uint8_t dump_type);
int64_t opal_dump_info(__be32 *dump_id, __be32 *dump_size);
int64_t opal_dump_info2(__be32 *dump_id, __be32 *dump_size, __be32 *dump_type);
int64_t opal_dump_read(uint32_t dump_id, uint64_t buffer);
int64_t opal_dump_ack(uint32_t dump_id);
int64_t opal_dump_resend_notification(void);

int64_t opal_get_msg(uint64_t buffer, uint64_t size);
int64_t opal_check_completion(uint64_t buffer, uint64_t size, uint64_t token);
int64_t opal_sync_host_reboot(void);
int64_t opal_get_param(uint64_t token, uint32_t param_id, uint64_t buffer,
		uint64_t length);
int64_t opal_set_param(uint64_t token, uint32_t param_id, uint64_t buffer,
		uint64_t length);
int64_t opal_sensor_read(uint32_t sensor_hndl, int token, __be32 *sensor_data);
int64_t opal_handle_hmi(void);
int64_t opal_register_dump_region(uint32_t id, uint64_t start, uint64_t end);
int64_t opal_unregister_dump_region(uint32_t id);
int64_t opal_pci_set_phb_cxl_mode(uint64_t phb_id, uint64_t mode, uint64_t pe_number);
int64_t opal_ipmi_send(uint64_t interface, struct opal_ipmi_msg *msg,
		uint64_t msg_len);
int64_t opal_ipmi_recv(uint64_t interface, struct opal_ipmi_msg *msg,
		uint64_t *msg_len);

/* Internal functions */
extern int early_init_dt_scan_opal(unsigned long node, const char *uname,
				   int depth, void *data);
extern int early_init_dt_scan_recoverable_ranges(unsigned long node,
				 const char *uname, int depth, void *data);

extern int opal_get_chars(uint32_t vtermno, char *buf, int count);
extern int opal_put_chars(uint32_t vtermno, const char *buf, int total_len);

extern void hvc_opal_init_early(void);

extern int opal_notifier_register(struct notifier_block *nb);
extern int opal_notifier_unregister(struct notifier_block *nb);

extern int opal_message_notifier_register(enum OpalMessageType msg_type,
						struct notifier_block *nb);
extern void opal_notifier_enable(void);
extern void opal_notifier_disable(void);
extern void opal_notifier_update_evt(uint64_t evt_mask, uint64_t evt_val);

extern int __opal_async_get_token(void);
extern int opal_async_get_token_interruptible(void);
extern int __opal_async_release_token(int token);
extern int opal_async_release_token(int token);
extern int opal_async_wait_response(uint64_t token, struct opal_msg *msg);
extern int opal_get_sensor_data(u32 sensor_hndl, u32 *sensor_data);

struct rtc_time;
extern int opal_set_rtc_time(struct rtc_time *tm);
extern void opal_get_rtc_time(struct rtc_time *tm);
extern unsigned long opal_get_boot_time(void);
extern void opal_nvram_init(void);
extern void opal_flash_init(void);
extern void opal_flash_term_callback(void);
extern int opal_elog_init(void);
extern void opal_platform_dump_init(void);
extern void opal_sys_param_init(void);
extern void opal_msglog_init(void);

extern int opal_machine_check(struct pt_regs *regs);
extern bool opal_mce_check_early_recovery(struct pt_regs *regs);
extern int opal_hmi_exception_early(struct pt_regs *regs);
extern int opal_handle_hmi_exception(struct pt_regs *regs);

extern void opal_shutdown(void);
extern int opal_resync_timebase(void);

extern void opal_lpc_init(void);

struct opal_sg_list *opal_vmalloc_to_sg_list(void *vmalloc_addr,
					     unsigned long vmalloc_size);
void opal_free_sg_list(struct opal_sg_list *sg);

/*
 * Dump region ID range usable by the OS
 */
#define OPAL_DUMP_REGION_HOST_START		0x80
#define OPAL_DUMP_REGION_LOG_BUF		0x80
#define OPAL_DUMP_REGION_HOST_END		0xFF

#endif /* __ASSEMBLY__ */

#endif /* __OPAL_H */
