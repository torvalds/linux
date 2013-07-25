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

/****** Takeover interface ********/

/* PAPR H-Call used to querty the HAL existence and/or instanciate
 * it from within pHyp (tech preview only).
 *
 * This is exclusively used in prom_init.c
 */

#ifndef __ASSEMBLY__

struct opal_takeover_args {
	u64	k_image;		/* r4 */
	u64	k_size;			/* r5 */
	u64	k_entry;		/* r6 */
	u64	k_entry2;		/* r7 */
	u64	hal_addr;		/* r8 */
	u64	rd_image;		/* r9 */
	u64	rd_size;		/* r10 */
	u64	rd_loc;			/* r11 */
};

extern long opal_query_takeover(u64 *hal_size, u64 *hal_align);

extern long opal_do_takeover(struct opal_takeover_args *args);

struct rtas_args;
extern int opal_enter_rtas(struct rtas_args *args,
			   unsigned long data,
			   unsigned long entry);

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

/* API Tokens (in r0) */
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

#ifndef __ASSEMBLY__

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
	OPAL_EEH_ACTION_CLEAR_FREEZE_ALL = 3
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
	OPAL_EVENT_PCI_ERROR		= 0x200
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

enum OpalPciResetAndReinitScope {
	OPAL_PHB_COMPLETE = 1, OPAL_PCI_LINK = 2, OPAL_PHB_ERROR = 3,
	OPAL_PCI_HOT_RESET = 4, OPAL_PCI_FUNDAMENTAL_RESET = 5,
	OPAL_PCI_IODA_TABLE_RESET = 6,
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
	uint16_t type;

	/* GEM */
	uint64_t gemXfir;
	uint64_t gemRfir;
	uint64_t gemRirqfir;
	uint64_t gemMask;
	uint64_t gemRwof;

	/* LEM */
	uint64_t lemFir;
	uint64_t lemErrMask;
	uint64_t lemAction0;
	uint64_t lemAction1;
	uint64_t lemWof;

	union {
		struct OpalIoP7IOCRgcErrorData {
			uint64_t rgcStatus;		/* 3E1C10 */
			uint64_t rgcLdcp;		/* 3E1C18 */
		}rgc;
		struct OpalIoP7IOCBiErrorData {
			uint64_t biLdcp0;		/* 3C0100, 3C0118 */
			uint64_t biLdcp1;		/* 3C0108, 3C0120 */
			uint64_t biLdcp2;		/* 3C0110, 3C0128 */
			uint64_t biFenceStatus;		/* 3C0130, 3C0130 */

			uint8_t  biDownbound;		/* BI Downbound or Upbound */
		}bi;
		struct OpalIoP7IOCCiErrorData {
			uint64_t ciPortStatus;		/* 3Dn008 */
			uint64_t ciPortLdcp;		/* 3Dn010 */

			uint8_t	 ciPort;		/* Index of CI port: 0/1 */
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
};

enum {
	OPAL_P7IOC_NUM_PEST_REGS = 128,
};

struct OpalIoPhbErrorCommon {
	uint32_t version;
	uint32_t ioType;
	uint32_t len;
};

struct OpalIoP7IOCPhbErrorData {
	struct OpalIoPhbErrorCommon common;

	uint32_t brdgCtl;

	// P7IOC utl regs
	uint32_t portStatusReg;
	uint32_t rootCmplxStatus;
	uint32_t busAgentStatus;

	// P7IOC cfg regs
	uint32_t deviceStatus;
	uint32_t slotStatus;
	uint32_t linkStatus;
	uint32_t devCmdStatus;
	uint32_t devSecStatus;

	// cfg AER regs
	uint32_t rootErrorStatus;
	uint32_t uncorrErrorStatus;
	uint32_t corrErrorStatus;
	uint32_t tlpHdr1;
	uint32_t tlpHdr2;
	uint32_t tlpHdr3;
	uint32_t tlpHdr4;
	uint32_t sourceId;

	uint32_t rsv3;

	// Record data about the call to allocate a buffer.
	uint64_t errorClass;
	uint64_t correlator;

	//P7IOC MMIO Error Regs
	uint64_t p7iocPlssr;                // n120
	uint64_t p7iocCsr;                  // n110
	uint64_t lemFir;                    // nC00
	uint64_t lemErrorMask;              // nC18
	uint64_t lemWOF;                    // nC40
	uint64_t phbErrorStatus;            // nC80
	uint64_t phbFirstErrorStatus;       // nC88
	uint64_t phbErrorLog0;              // nCC0
	uint64_t phbErrorLog1;              // nCC8
	uint64_t mmioErrorStatus;           // nD00
	uint64_t mmioFirstErrorStatus;      // nD08
	uint64_t mmioErrorLog0;             // nD40
	uint64_t mmioErrorLog1;             // nD48
	uint64_t dma0ErrorStatus;           // nD80
	uint64_t dma0FirstErrorStatus;      // nD88
	uint64_t dma0ErrorLog0;             // nDC0
	uint64_t dma0ErrorLog1;             // nDC8
	uint64_t dma1ErrorStatus;           // nE00
	uint64_t dma1FirstErrorStatus;      // nE08
	uint64_t dma1ErrorLog0;             // nE40
	uint64_t dma1ErrorLog1;             // nE48
	uint64_t pestA[OPAL_P7IOC_NUM_PEST_REGS];
	uint64_t pestB[OPAL_P7IOC_NUM_PEST_REGS];
};

typedef struct oppanel_line {
	const char * 	line;
	uint64_t 	line_len;
} oppanel_line_t;

/* API functions */
int64_t opal_console_write(int64_t term_number, int64_t *length,
			   const uint8_t *buffer);
int64_t opal_console_read(int64_t term_number, int64_t *length,
			  uint8_t *buffer);
int64_t opal_console_write_buffer_space(int64_t term_number,
					int64_t *length);
int64_t opal_rtc_read(uint32_t *year_month_day,
		      uint64_t *hour_minute_second_millisecond);
int64_t opal_rtc_write(uint32_t year_month_day,
		       uint64_t hour_minute_second_millisecond);
int64_t opal_cec_power_down(uint64_t request);
int64_t opal_cec_reboot(void);
int64_t opal_read_nvram(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_write_nvram(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_handle_interrupt(uint64_t isn, uint64_t *outstanding_event_mask);
int64_t opal_poll_events(uint64_t *outstanding_event_mask);
int64_t opal_pci_set_hub_tce_memory(uint64_t hub_id, uint64_t tce_mem_addr,
				    uint64_t tce_mem_size);
int64_t opal_pci_set_phb_tce_memory(uint64_t phb_id, uint64_t tce_mem_addr,
				    uint64_t tce_mem_size);
int64_t opal_pci_config_read_byte(uint64_t phb_id, uint64_t bus_dev_func,
				  uint64_t offset, uint8_t *data);
int64_t opal_pci_config_read_half_word(uint64_t phb_id, uint64_t bus_dev_func,
				       uint64_t offset, uint16_t *data);
int64_t opal_pci_config_read_word(uint64_t phb_id, uint64_t bus_dev_func,
				  uint64_t offset, uint32_t *data);
int64_t opal_pci_config_write_byte(uint64_t phb_id, uint64_t bus_dev_func,
				   uint64_t offset, uint8_t data);
int64_t opal_pci_config_write_half_word(uint64_t phb_id, uint64_t bus_dev_func,
					uint64_t offset, uint16_t data);
int64_t opal_pci_config_write_word(uint64_t phb_id, uint64_t bus_dev_func,
				   uint64_t offset, uint32_t data);
int64_t opal_set_xive(uint32_t isn, uint16_t server, uint8_t priority);
int64_t opal_get_xive(uint32_t isn, uint16_t *server, uint8_t *priority);
int64_t opal_register_exception_handler(uint64_t opal_exception,
					uint64_t handler_address,
					uint64_t glue_cache_line);
int64_t opal_pci_eeh_freeze_status(uint64_t phb_id, uint64_t pe_number,
				   uint8_t *freeze_state,
				   uint16_t *pci_error_type,
				   uint64_t *phb_status);
int64_t opal_pci_eeh_freeze_clear(uint64_t phb_id, uint64_t pe_number,
				  uint64_t eeh_action_token);
int64_t opal_pci_shpc(uint64_t phb_id, uint64_t shpc_action, uint8_t *state);



int64_t opal_pci_phb_mmio_enable(uint64_t phb_id, uint16_t window_type,
				 uint16_t window_num, uint16_t enable);
int64_t opal_pci_set_phb_mem_window(uint64_t phb_id, uint16_t window_type,
				    uint16_t window_num,
				    uint64_t starting_real_address,
				    uint64_t starting_pci_address,
				    uint16_t segment_size);
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
			     int32_t *interrupt_source_number);
int64_t opal_get_msi_32(uint64_t phb_id, uint32_t mve_number, uint32_t xive_num,
			uint8_t msi_range, uint32_t *msi_address,
			uint32_t *message_data);
int64_t opal_get_msi_64(uint64_t phb_id, uint32_t mve_number,
			uint32_t xive_num, uint8_t msi_range,
			uint64_t *msi_address, uint32_t *message_data);
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
int64_t opal_pci_reinit(uint64_t phb_id, uint8_t reinit_scope);
int64_t opal_pci_mask_pe_error(uint64_t phb_id, uint16_t pe_number, uint8_t error_type, uint8_t mask_action);
int64_t opal_set_slot_led_status(uint64_t phb_id, uint64_t slot_id, uint8_t led_type, uint8_t led_action);
int64_t opal_get_epow_status(uint64_t *status);
int64_t opal_set_system_attention_led(uint8_t led_action);
int64_t opal_pci_next_error(uint64_t phb_id, uint64_t *first_frozen_pe,
			    uint16_t *pci_error_type, uint16_t *severity);
int64_t opal_pci_poll(uint64_t phb_id);

/* Internal functions */
extern int early_init_dt_scan_opal(unsigned long node, const char *uname, int depth, void *data);

extern int opal_get_chars(uint32_t vtermno, char *buf, int count);
extern int opal_put_chars(uint32_t vtermno, const char *buf, int total_len);

extern void hvc_opal_init_early(void);

/* Internal functions */
extern int early_init_dt_scan_opal(unsigned long node, const char *uname,
				   int depth, void *data);

extern int opal_notifier_register(struct notifier_block *nb);
extern void opal_notifier_enable(void);
extern void opal_notifier_disable(void);
extern void opal_notifier_update_evt(uint64_t evt_mask, uint64_t evt_val);

extern int opal_get_chars(uint32_t vtermno, char *buf, int count);
extern int opal_put_chars(uint32_t vtermno, const char *buf, int total_len);

extern void hvc_opal_init_early(void);

struct rtc_time;
extern int opal_set_rtc_time(struct rtc_time *tm);
extern void opal_get_rtc_time(struct rtc_time *tm);
extern unsigned long opal_get_boot_time(void);
extern void opal_nvram_init(void);

extern int opal_machine_check(struct pt_regs *regs);

extern void opal_shutdown(void);

#endif /* __ASSEMBLY__ */

#endif /* __OPAL_H */
