// SPDX-License-Identifier: GPL-2.0+
/*
 * icom.h
 *
 * Copyright (C) 2001 Michael Anderson, IBM Corporation
 *
 * Serial device driver include file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/serial_core.h>

#define BAUD_TABLE_LIMIT	((sizeof(icom_acfg_baud)/sizeof(int)) - 1)
static int icom_acfg_baud[] = {
	300,
	600,
	900,
	1200,
	1800,
	2400,
	3600,
	4800,
	7200,
	9600,
	14400,
	19200,
	28800,
	38400,
	57600,
	76800,
	115200,
	153600,
	230400,
	307200,
	460800,
};

struct icom_regs {
	u32 control;		/* Adapter Control Register     */
	u32 interrupt;		/* Adapter Interrupt Register   */
	u32 int_mask;		/* Adapter Interrupt Mask Reg   */
	u32 int_pri;		/* Adapter Interrupt Priority r */
	u32 int_reg_b;		/* Adapter non-masked Interrupt */
	u32 resvd01;
	u32 resvd02;
	u32 resvd03;
	u32 control_2;		/* Adapter Control Register 2   */
	u32 interrupt_2;	/* Adapter Interrupt Register 2 */
	u32 int_mask_2;		/* Adapter Interrupt Mask 2     */
	u32 int_pri_2;		/* Adapter Interrupt Prior 2    */
	u32 int_reg_2b;		/* Adapter non-masked 2         */
};

struct func_dram {
	u32 reserved[108];	/* 0-1B0   reserved by personality code */
	u32 RcvStatusAddr;	/* 1B0-1B3 Status Address for Next rcv */
	u8 RcvStnAddr;		/* 1B4     Receive Station Addr */
	u8 IdleState;		/* 1B5     Idle State */
	u8 IdleMonitor;		/* 1B6     Idle Monitor */
	u8 FlagFillIdleTimer;	/* 1B7     Flag Fill Idle Timer */
	u32 XmitStatusAddr;	/* 1B8-1BB Transmit Status Address */
	u8 StartXmitCmd;	/* 1BC     Start Xmit Command */
	u8 HDLCConfigReg;	/* 1BD     Reserved */
	u8 CauseCode;		/* 1BE     Cause code for fatal error */
	u8 xchar;		/* 1BF     High priority send */
	u32 reserved3;		/* 1C0-1C3 Reserved */
	u8 PrevCmdReg;		/* 1C4     Reserved */
	u8 CmdReg;		/* 1C5     Command Register */
	u8 async_config2;	/* 1C6     Async Config Byte 2 */
	u8 async_config3;	/* 1C7     Async Config Byte 3 */
	u8 dce_resvd[20];	/* 1C8-1DB DCE Rsvd           */
	u8 dce_resvd21;		/* 1DC     DCE Rsvd (21st byte */
	u8 misc_flags;		/* 1DD     misc flags         */
#define V2_HARDWARE     0x40
#define ICOM_HDW_ACTIVE 0x01
	u8 call_length;		/* 1DE     Phone #/CFI buff ln */
	u8 call_length2;	/* 1DF     Upper byte (unused) */
	u32 call_addr;		/* 1E0-1E3 Phn #/CFI buff addr */
	u16 timer_value;	/* 1E4-1E5 general timer value */
	u8 timer_command;	/* 1E6     general timer cmd  */
	u8 dce_command;		/* 1E7     dce command reg    */
	u8 dce_cmd_status;	/* 1E8     dce command stat   */
	u8 x21_r1_ioff;		/* 1E9     dce ready counter  */
	u8 x21_r0_ioff;		/* 1EA     dce not ready ctr  */
	u8 x21_ralt_ioff;	/* 1EB     dce CNR counter    */
	u8 x21_r1_ion;		/* 1EC     dce ready I on ctr */
	u8 rsvd_ier;		/* 1ED     Rsvd for IER (if ne */
	u8 ier;			/* 1EE     Interrupt Enable   */
	u8 isr;			/* 1EF     Input Signal Reg   */
	u8 osr;			/* 1F0     Output Signal Reg  */
	u8 reset;		/* 1F1     Reset/Reload Reg   */
	u8 disable;		/* 1F2     Disable Reg        */
	u8 sync;		/* 1F3     Sync Reg           */
	u8 error_stat;		/* 1F4     Error Status       */
	u8 cable_id;		/* 1F5     Cable ID           */
	u8 cs_length;		/* 1F6     CS Load Length     */
	u8 mac_length;		/* 1F7     Mac Load Length    */
	u32 cs_load_addr;	/* 1F8-1FB Call Load PCI Addr */
	u32 mac_load_addr;	/* 1FC-1FF Mac Load PCI Addr  */
};

/*
 * adapter defines and structures
 */
#define ICOM_CONTROL_START_A         0x00000008
#define ICOM_CONTROL_STOP_A          0x00000004
#define ICOM_CONTROL_START_B         0x00000002
#define ICOM_CONTROL_STOP_B          0x00000001
#define ICOM_CONTROL_START_C         0x00000008
#define ICOM_CONTROL_STOP_C          0x00000004
#define ICOM_CONTROL_START_D         0x00000002
#define ICOM_CONTROL_STOP_D          0x00000001
#define ICOM_IRAM_OFFSET             0x1000
#define ICOM_IRAM_SIZE               0x0C00
#define ICOM_DCE_IRAM_OFFSET         0x0A00
#define ICOM_CABLE_ID_VALID          0x01
#define ICOM_CABLE_ID_MASK           0xF0
#define ICOM_DISABLE                 0x80
#define CMD_XMIT_RCV_ENABLE          0xC0
#define CMD_XMIT_ENABLE              0x40
#define CMD_RCV_DISABLE              0x00
#define CMD_RCV_ENABLE               0x80
#define CMD_RESTART                  0x01
#define CMD_HOLD_XMIT                0x02
#define CMD_SND_BREAK                0x04
#define RS232_CABLE                  0x06
#define V24_CABLE                    0x0E
#define V35_CABLE                    0x0C
#define V36_CABLE                    0x02
#define NO_CABLE                     0x00
#define START_DOWNLOAD               0x80
#define ICOM_INT_MASK_PRC_A          0x00003FFF
#define ICOM_INT_MASK_PRC_B          0x3FFF0000
#define ICOM_INT_MASK_PRC_C          0x00003FFF
#define ICOM_INT_MASK_PRC_D          0x3FFF0000
#define INT_RCV_COMPLETED            0x1000
#define INT_XMIT_COMPLETED           0x2000
#define INT_IDLE_DETECT              0x0800
#define INT_RCV_DISABLED             0x0400
#define INT_XMIT_DISABLED            0x0200
#define INT_RCV_XMIT_SHUTDOWN        0x0100
#define INT_FATAL_ERROR              0x0080
#define INT_CABLE_PULL               0x0020
#define INT_SIGNAL_CHANGE            0x0010
#define HDLC_PPP_PURE_ASYNC          0x02
#define HDLC_FF_FILL                 0x00
#define HDLC_HDW_FLOW                0x01
#define START_XMIT                   0x80
#define ICOM_ACFG_DRIVE1             0x20
#define ICOM_ACFG_NO_PARITY          0x00
#define ICOM_ACFG_PARITY_ENAB        0x02
#define ICOM_ACFG_PARITY_ODD         0x01
#define ICOM_ACFG_8BPC               0x00
#define ICOM_ACFG_7BPC               0x04
#define ICOM_ACFG_6BPC               0x08
#define ICOM_ACFG_5BPC               0x0C
#define ICOM_ACFG_1STOP_BIT          0x00
#define ICOM_ACFG_2STOP_BIT          0x10
#define ICOM_DTR                     0x80
#define ICOM_RTS                     0x40
#define ICOM_RI                      0x08
#define ICOM_DSR                     0x80
#define ICOM_DCD                     0x20
#define ICOM_CTS                     0x40

#define NUM_XBUFFS 1
#define NUM_RBUFFS 2
#define RCV_BUFF_SZ 0x0200
#define XMIT_BUFF_SZ 0x1000
struct statusArea {
    /**********************************************/
	/* Transmit Status Area                       */
    /**********************************************/
	struct xmit_status_area{
		u32 leNext;	/* Next entry in Little Endian on Adapter */
		u32 leNextASD;
		u32 leBuffer;	/* Buffer for entry in LE for Adapter */
		u16 leLengthASD;
		u16 leOffsetASD;
		u16 leLength;	/* Length of data in segment */
		u16 flags;
#define SA_FLAGS_DONE           0x0080	/* Done with Segment */
#define SA_FLAGS_CONTINUED      0x8000	/* More Segments */
#define SA_FLAGS_IDLE           0x4000	/* Mark IDLE after frm */
#define SA_FLAGS_READY_TO_XMIT  0x0800
#define SA_FLAGS_STAT_MASK      0x007F
	} xmit[NUM_XBUFFS];

    /**********************************************/
	/* Receive Status Area                        */
    /**********************************************/
	struct {
		u32 leNext;	/* Next entry in Little Endian on Adapter */
		u32 leNextASD;
		u32 leBuffer;	/* Buffer for entry in LE for Adapter */
		u16 WorkingLength;	/* size of segment */
		u16 reserv01;
		u16 leLength;	/* Length of data in segment */
		u16 flags;
#define SA_FL_RCV_DONE           0x0010	/* Data ready */
#define SA_FLAGS_OVERRUN         0x0040
#define SA_FLAGS_PARITY_ERROR    0x0080
#define SA_FLAGS_FRAME_ERROR     0x0001
#define SA_FLAGS_FRAME_TRUNC     0x0002
#define SA_FLAGS_BREAK_DET       0x0004	/* set conditionally by device driver, not hardware */
#define SA_FLAGS_RCV_MASK        0xFFE6
	} rcv[NUM_RBUFFS];
};

struct icom_adapter;


#define ICOM_MAJOR       243
#define ICOM_MINOR_START 0

struct icom_port {
	struct uart_port uart_port;
	u8 imbed_modem;
#define ICOM_UNKNOWN		1
#define ICOM_RVX		2
#define ICOM_IMBED_MODEM	3
	unsigned char cable_id;
	unsigned char read_status_mask;
	unsigned char ignore_status_mask;
	void __iomem * int_reg;
	struct icom_regs __iomem *global_reg;
	struct func_dram __iomem *dram;
	int port;
	struct statusArea *statStg;
	dma_addr_t statStg_pci;
	u32 *xmitRestart;
	dma_addr_t xmitRestart_pci;
	unsigned char *xmit_buf;
	dma_addr_t xmit_buf_pci;
	unsigned char *recv_buf;
	dma_addr_t recv_buf_pci;
	int next_rcv;
	int put_length;
	int status;
#define ICOM_PORT_ACTIVE	1	/* Port exists. */
#define ICOM_PORT_OFF		0	/* Port does not exist. */
	int load_in_progress;
	struct icom_adapter *adapter;
};

struct icom_adapter {
	void __iomem * base_addr;
	unsigned long base_addr_pci;
	struct pci_dev *pci_dev;
	struct icom_port port_info[4];
	int index;
	int version;
#define ADAPTER_V1	0x0001
#define ADAPTER_V2	0x0002
	u32 subsystem_id;
#define FOUR_PORT_MODEL				0x0252
#define V2_TWO_PORTS_RVX			0x021A
#define V2_ONE_PORT_RVX_ONE_PORT_IMBED_MDM	0x0251
	int numb_ports;
	struct list_head icom_adapter_entry;
	struct kref kref;
};

/* prototype */
extern void iCom_sercons_init(void);

struct lookup_proc_table {
	u32	__iomem *global_control_reg;
	unsigned long	processor_id;
};

struct lookup_int_table {
	u32	__iomem *global_int_mask;
	unsigned long	processor_id;
};
