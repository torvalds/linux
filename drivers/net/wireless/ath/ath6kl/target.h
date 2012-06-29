/*
 * Copyright (c) 2004-2010 Atheros Communications Inc.
 * Copyright (c) 2011 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef TARGET_H
#define TARGET_H

#define AR6003_BOARD_DATA_SZ		1024
#define AR6003_BOARD_EXT_DATA_SZ	768
#define AR6003_BOARD_EXT_DATA_SZ_V2	1024

#define AR6004_BOARD_DATA_SZ     6144
#define AR6004_BOARD_EXT_DATA_SZ 0

#define RESET_CONTROL_ADDRESS		0x00000000
#define RESET_CONTROL_COLD_RST		0x00000100
#define RESET_CONTROL_MBOX_RST		0x00000004

#define CPU_CLOCK_STANDARD_S		0
#define CPU_CLOCK_STANDARD		0x00000003
#define CPU_CLOCK_ADDRESS		0x00000020

#define CLOCK_CONTROL_ADDRESS		0x00000028
#define CLOCK_CONTROL_LF_CLK32_S	2
#define CLOCK_CONTROL_LF_CLK32		0x00000004

#define SYSTEM_SLEEP_ADDRESS		0x000000c4
#define SYSTEM_SLEEP_DISABLE_S		0
#define SYSTEM_SLEEP_DISABLE		0x00000001

#define LPO_CAL_ADDRESS			0x000000e0
#define LPO_CAL_ENABLE_S		20
#define LPO_CAL_ENABLE			0x00100000

#define GPIO_PIN9_ADDRESS		0x0000004c
#define GPIO_PIN10_ADDRESS		0x00000050
#define GPIO_PIN11_ADDRESS		0x00000054
#define GPIO_PIN12_ADDRESS		0x00000058
#define GPIO_PIN13_ADDRESS		0x0000005c

#define HOST_INT_STATUS_ADDRESS		0x00000400
#define HOST_INT_STATUS_ERROR_S		7
#define HOST_INT_STATUS_ERROR		0x00000080

#define HOST_INT_STATUS_CPU_S		6
#define HOST_INT_STATUS_CPU		0x00000040

#define HOST_INT_STATUS_COUNTER_S	4
#define HOST_INT_STATUS_COUNTER		0x00000010

#define CPU_INT_STATUS_ADDRESS		0x00000401

#define ERROR_INT_STATUS_ADDRESS	0x00000402
#define ERROR_INT_STATUS_WAKEUP_S	2
#define ERROR_INT_STATUS_WAKEUP		0x00000004

#define ERROR_INT_STATUS_RX_UNDERFLOW_S	1
#define ERROR_INT_STATUS_RX_UNDERFLOW	0x00000002

#define ERROR_INT_STATUS_TX_OVERFLOW_S	0
#define ERROR_INT_STATUS_TX_OVERFLOW	0x00000001

#define COUNTER_INT_STATUS_ADDRESS	0x00000403
#define COUNTER_INT_STATUS_COUNTER_S	0
#define COUNTER_INT_STATUS_COUNTER	0x000000ff

#define RX_LOOKAHEAD_VALID_ADDRESS	0x00000405

#define INT_STATUS_ENABLE_ADDRESS	0x00000418
#define INT_STATUS_ENABLE_ERROR_S	7
#define INT_STATUS_ENABLE_ERROR		0x00000080

#define INT_STATUS_ENABLE_CPU_S		6
#define INT_STATUS_ENABLE_CPU		0x00000040

#define INT_STATUS_ENABLE_INT_S		5
#define INT_STATUS_ENABLE_INT		0x00000020
#define INT_STATUS_ENABLE_COUNTER_S	4
#define INT_STATUS_ENABLE_COUNTER	0x00000010

#define INT_STATUS_ENABLE_MBOX_DATA_S	0
#define INT_STATUS_ENABLE_MBOX_DATA	0x0000000f

#define CPU_INT_STATUS_ENABLE_ADDRESS	0x00000419
#define CPU_INT_STATUS_ENABLE_BIT_S	0
#define CPU_INT_STATUS_ENABLE_BIT	0x000000ff

#define ERROR_STATUS_ENABLE_ADDRESS		0x0000041a
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_S	1
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW	0x00000002

#define ERROR_STATUS_ENABLE_TX_OVERFLOW_S	0
#define ERROR_STATUS_ENABLE_TX_OVERFLOW		0x00000001

#define COUNTER_INT_STATUS_ENABLE_ADDRESS	0x0000041b
#define COUNTER_INT_STATUS_ENABLE_BIT_S		0
#define COUNTER_INT_STATUS_ENABLE_BIT		0x000000ff

#define COUNT_ADDRESS			0x00000420

#define COUNT_DEC_ADDRESS		0x00000440

#define WINDOW_DATA_ADDRESS		0x00000474
#define WINDOW_WRITE_ADDR_ADDRESS	0x00000478
#define WINDOW_READ_ADDR_ADDRESS	0x0000047c
#define CPU_DBG_SEL_ADDRESS		0x00000483
#define CPU_DBG_ADDRESS			0x00000484

#define LOCAL_SCRATCH_ADDRESS		0x000000c0
#define ATH6KL_OPTION_SLEEP_DISABLE	0x08

#define RTC_BASE_ADDRESS		0x00004000
#define GPIO_BASE_ADDRESS		0x00014000
#define MBOX_BASE_ADDRESS		0x00018000
#define ANALOG_INTF_BASE_ADDRESS	0x0001c000

/* real name of the register is unknown */
#define ATH6KL_ANALOG_PLL_REGISTER	(ANALOG_INTF_BASE_ADDRESS + 0x284)

#define SM(f, v)	(((v) << f##_S) & f)
#define MS(f, v)	(((v) & f) >> f##_S)

/*
 * xxx_HOST_INTEREST_ADDRESS is the address in Target RAM of the
 * host_interest structure.
 *
 * Host Interest is shared between Host and Target in order to coordinate
 * between the two, and is intended to remain constant (with additions only
 * at the end).
 */
#define ATH6KL_AR6003_HI_START_ADDR           0x00540600
#define ATH6KL_AR6004_HI_START_ADDR           0x00400800

/*
 * These are items that the Host may need to access
 * via BMI or via the Diagnostic Window. The position
 * of items in this structure must remain constant.
 * across firmware revisions!
 *
 * Types for each item must be fixed size across target and host platforms.
 * The structure is used only to calculate offset for each register with
 * HI_ITEM() macro, no values are stored to it.
 *
 * More items may be added at the end.
 */
struct host_interest {
	/*
	 * Pointer to application-defined area, if any.
	 * Set by Target application during startup.
	 */
	u32 hi_app_host_interest;                      /* 0x00 */

	/* Pointer to register dump area, valid after Target crash. */
	u32 hi_failure_state;                          /* 0x04 */

	/* Pointer to debug logging header */
	u32 hi_dbglog_hdr;                             /* 0x08 */

	u32 hi_unused1;                       /* 0x0c */

	/*
	 * General-purpose flag bits, similar to ATH6KL_OPTION_* flags.
	 * Can be used by application rather than by OS.
	 */
	u32 hi_option_flag;                            /* 0x10 */

	/*
	 * Boolean that determines whether or not to
	 * display messages on the serial port.
	 */
	u32 hi_serial_enable;                          /* 0x14 */

	/* Start address of DataSet index, if any */
	u32 hi_dset_list_head;                         /* 0x18 */

	/* Override Target application start address */
	u32 hi_app_start;                              /* 0x1c */

	/* Clock and voltage tuning */
	u32 hi_skip_clock_init;                        /* 0x20 */
	u32 hi_core_clock_setting;                     /* 0x24 */
	u32 hi_cpu_clock_setting;                      /* 0x28 */
	u32 hi_system_sleep_setting;                   /* 0x2c */
	u32 hi_xtal_control_setting;                   /* 0x30 */
	u32 hi_pll_ctrl_setting_24ghz;                 /* 0x34 */
	u32 hi_pll_ctrl_setting_5ghz;                  /* 0x38 */
	u32 hi_ref_voltage_trim_setting;               /* 0x3c */
	u32 hi_clock_info;                             /* 0x40 */

	/*
	 * Flash configuration overrides, used only
	 * when firmware is not executing from flash.
	 * (When using flash, modify the global variables
	 * with equivalent names.)
	 */
	u32 hi_bank0_addr_value;                       /* 0x44 */
	u32 hi_bank0_read_value;                       /* 0x48 */
	u32 hi_bank0_write_value;                      /* 0x4c */
	u32 hi_bank0_config_value;                     /* 0x50 */

	/* Pointer to Board Data  */
	u32 hi_board_data;                             /* 0x54 */
	u32 hi_board_data_initialized;                 /* 0x58 */

	u32 hi_dset_ram_index_tbl;                     /* 0x5c */

	u32 hi_desired_baud_rate;                      /* 0x60 */
	u32 hi_dbglog_config;                          /* 0x64 */
	u32 hi_end_ram_reserve_sz;                     /* 0x68 */
	u32 hi_mbox_io_block_sz;                       /* 0x6c */

	u32 hi_num_bpatch_streams;                     /* 0x70 -- unused */
	u32 hi_mbox_isr_yield_limit;                   /* 0x74 */

	u32 hi_refclk_hz;                              /* 0x78 */
	u32 hi_ext_clk_detected;                       /* 0x7c */
	u32 hi_dbg_uart_txpin;                         /* 0x80 */
	u32 hi_dbg_uart_rxpin;                         /* 0x84 */
	u32 hi_hci_uart_baud;                          /* 0x88 */
	u32 hi_hci_uart_pin_assignments;               /* 0x8C */
	/*
	 * NOTE: byte [0] = tx pin, [1] = rx pin, [2] = rts pin, [3] = cts
	 * pin
	 */
	u32 hi_hci_uart_baud_scale_val;                /* 0x90 */
	u32 hi_hci_uart_baud_step_val;                 /* 0x94 */

	u32 hi_allocram_start;                         /* 0x98 */
	u32 hi_allocram_sz;                            /* 0x9c */
	u32 hi_hci_bridge_flags;                       /* 0xa0 */
	u32 hi_hci_uart_support_pins;                  /* 0xa4 */
	/*
	 * NOTE: byte [0] = RESET pin (bit 7 is polarity),
	 * bytes[1]..bytes[3] are for future use
	 */
	u32 hi_hci_uart_pwr_mgmt_params;               /* 0xa8 */
	/*
	 * 0xa8   - [1]: 0 = UART FC active low, 1 = UART FC active high
	 *      [31:16]: wakeup timeout in ms
	 */

	/* Pointer to extended board data */
	u32 hi_board_ext_data;                /* 0xac */
	u32 hi_board_ext_data_config;         /* 0xb0 */

	/*
	 * Bit [0]  :   valid
	 * Bit[31:16:   size
	 */
	/*
	 * hi_reset_flag is used to do some stuff when target reset.
	 * such as restore app_start after warm reset or
	 * preserve host Interest area, or preserve ROM data, literals etc.
	 */
	u32 hi_reset_flag;                            /* 0xb4 */
	/* indicate hi_reset_flag is valid */
	u32 hi_reset_flag_valid;                      /* 0xb8 */
	u32 hi_hci_uart_pwr_mgmt_params_ext;           /* 0xbc */
	/*
	 * 0xbc - [31:0]: idle timeout in ms
	 */
	/* ACS flags */
	u32 hi_acs_flags;                              /* 0xc0 */
	u32 hi_console_flags;                          /* 0xc4 */
	u32 hi_nvram_state;                            /* 0xc8 */
	u32 hi_option_flag2;                           /* 0xcc */

	/* If non-zero, override values sent to Host in WMI_READY event. */
	u32 hi_sw_version_override;                    /* 0xd0 */
	u32 hi_abi_version_override;                   /* 0xd4 */

	/*
	 * Percentage of high priority RX traffic to total expected RX traffic -
	 * applicable only to ar6004
	 */
	u32 hi_hp_rx_traffic_ratio;                    /* 0xd8 */

	/* test applications flags */
	u32 hi_test_apps_related    ;                  /* 0xdc */
	/* location of test script */
	u32 hi_ota_testscript;                         /* 0xe0 */
	/* location of CAL data */
	u32 hi_cal_data;                               /* 0xe4 */
	/* Number of packet log buffers */
	u32 hi_pktlog_num_buffers;                     /* 0xe8 */

} __packed;

#define HI_ITEM(item)  offsetof(struct host_interest, item)

#define HI_OPTION_MAC_ADDR_METHOD_SHIFT	3

#define HI_OPTION_FW_MODE_IBSS    0x0
#define HI_OPTION_FW_MODE_BSS_STA 0x1
#define HI_OPTION_FW_MODE_AP      0x2

#define HI_OPTION_FW_SUBMODE_NONE      0x0
#define HI_OPTION_FW_SUBMODE_P2PDEV    0x1
#define HI_OPTION_FW_SUBMODE_P2PCLIENT 0x2
#define HI_OPTION_FW_SUBMODE_P2PGO     0x3

#define HI_OPTION_NUM_DEV_SHIFT   0x9

#define HI_OPTION_FW_BRIDGE_SHIFT 0x04

/* Fw Mode/SubMode Mask
|------------------------------------------------------------------------------|
|   SUB   |   SUB   |   SUB   |  SUB    |         |         |         |
| MODE[3] | MODE[2] | MODE[1] | MODE[0] | MODE[3] | MODE[2] | MODE[1] | MODE[0|
|   (2)   |   (2)   |   (2)   |   (2)   |   (2)   |   (2)   |   (2)   |   (2)
|------------------------------------------------------------------------------|
*/
#define HI_OPTION_FW_MODE_BITS	       0x2
#define HI_OPTION_FW_MODE_SHIFT        0xC

#define HI_OPTION_FW_SUBMODE_BITS      0x2
#define HI_OPTION_FW_SUBMODE_SHIFT     0x14

/* Convert a Target virtual address into a Target physical address */
#define AR6003_VTOP(vaddr) ((vaddr) & 0x001fffff)
#define AR6004_VTOP(vaddr) (vaddr)

#define TARG_VTOP(target_type, vaddr) \
	(((target_type) == TARGET_TYPE_AR6003) ? AR6003_VTOP(vaddr) : \
	(((target_type) == TARGET_TYPE_AR6004) ? AR6004_VTOP(vaddr) : 0))

#define ATH6KL_FWLOG_PAYLOAD_SIZE		1500

struct ath6kl_dbglog_buf {
	__le32 next;
	__le32 buffer_addr;
	__le32 bufsize;
	__le32 length;
	__le32 count;
	__le32 free;
} __packed;

struct ath6kl_dbglog_hdr {
	__le32 dbuf_addr;
	__le32 dropped;
} __packed;

#endif
