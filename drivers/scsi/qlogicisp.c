/*
 * QLogic ISP1020 Intelligent SCSI Processor Driver (PCI)
 * Written by Erik H. Moe, ehm@cris.com
 * Copyright 1995, Erik H. Moe
 * Copyright 1996, 1997  Michael A. Griffith <grif@acm.org>
 * Copyright 2000, Jayson C. Vantuyl <vantuyl@csc.smsu.edu>
 *             and Bryon W. Roche    <bryon@csc.smsu.edu>
 *
 * 64-bit addressing added by Kanoj Sarcar <kanoj@sgi.com>
 * 			   and Leo Dagum    <dagum@sgi.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/blkdev.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/unistd.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include "scsi.h"
#include <scsi/scsi_host.h>

/*
 * With the qlogic interface, every queue slot can hold a SCSI
 * command with up to 4 scatter/gather entries.  If we need more
 * than 4 entries, continuation entries can be used that hold
 * another 7 entries each.  Unlike for other drivers, this means
 * that the maximum number of scatter/gather entries we can
 * support at any given time is a function of the number of queue
 * slots available.  That is, host->can_queue and host->sg_tablesize
 * are dynamic and _not_ independent.  This all works fine because
 * requests are queued serially and the scatter/gather limit is
 * determined for each queue request anew.
 */
#define QLOGICISP_REQ_QUEUE_LEN	63	/* must be power of two - 1 */
#define QLOGICISP_MAX_SG(ql)	(4 + ((ql) > 0) ? 7*((ql) - 1) : 0)

/* Configuration section *****************************************************/

/* Set the following macro to 1 to reload the ISP1020's firmware.  This is
   the latest firmware provided by QLogic.  This may be an earlier/later
   revision than supplied by your board. */

#define RELOAD_FIRMWARE		1

/* Set the following macro to 1 to reload the ISP1020's defaults from nvram.
   If you are not sure of your settings, leave this alone, the driver will
   use a set of 'safe' defaults */

#define USE_NVRAM_DEFAULTS	0

/*  Macros used for debugging */

#define DEBUG_ISP1020		0
#define DEBUG_ISP1020_INTR	0
#define DEBUG_ISP1020_SETUP	0
#define TRACE_ISP		0

#define DEFAULT_LOOP_COUNT	1000000

/* End Configuration section *************************************************/

#include <linux/module.h>

#if TRACE_ISP

# define TRACE_BUF_LEN	(32*1024)

struct {
	u_long		next;
	struct {
		u_long		time;
		u_int		index;
		u_int		addr;
		u_char *	name;
	} buf[TRACE_BUF_LEN];
} trace;

#define TRACE(w, i, a)						\
{								\
	unsigned long flags;					\
								\
	trace.buf[trace.next].name  = (w);			\
	trace.buf[trace.next].time  = jiffies;			\
	trace.buf[trace.next].index = (i);			\
	trace.buf[trace.next].addr  = (long) (a);		\
	trace.next = (trace.next + 1) & (TRACE_BUF_LEN - 1);	\
}

#else
# define TRACE(w, i, a)
#endif

#if DEBUG_ISP1020
#define ENTER(x)	printk("isp1020 : entering %s()\n", x);
#define LEAVE(x)	printk("isp1020 : leaving %s()\n", x);
#define DEBUG(x)	x
#else
#define ENTER(x)
#define LEAVE(x)
#define DEBUG(x)
#endif /* DEBUG_ISP1020 */

#if DEBUG_ISP1020_INTR
#define ENTER_INTR(x)	printk("isp1020 : entering %s()\n", x);
#define LEAVE_INTR(x)	printk("isp1020 : leaving %s()\n", x);
#define DEBUG_INTR(x)	x
#else
#define ENTER_INTR(x)
#define LEAVE_INTR(x)
#define DEBUG_INTR(x)
#endif /* DEBUG ISP1020_INTR */

#define ISP1020_REV_ID	1

#define MAX_TARGETS	16
#define MAX_LUNS	8

/* host configuration and control registers */
#define HOST_HCCR	0xc0	/* host command and control */

/* pci bus interface registers */
#define PCI_ID_LOW	0x00	/* vendor id */
#define PCI_ID_HIGH	0x02	/* device id */
#define ISP_CFG0	0x04	/* configuration register #0 */
#define  ISP_CFG0_HWMSK  0x000f	/* Hardware revision mask */
#define  ISP_CFG0_1020	 0x0001 /* ISP1020 */
#define  ISP_CFG0_1020A	 0x0002 /* ISP1020A */
#define  ISP_CFG0_1040	 0x0003 /* ISP1040 */
#define  ISP_CFG0_1040A	 0x0004 /* ISP1040A */
#define  ISP_CFG0_1040B	 0x0005 /* ISP1040B */
#define  ISP_CFG0_1040C	 0x0006 /* ISP1040C */
#define ISP_CFG1	0x06	/* configuration register #1 */
#define  ISP_CFG1_F128	 0x0040	/* 128-byte FIFO threshold */
#define  ISP_CFG1_F64	 0x0030	/* 128-byte FIFO threshold */
#define  ISP_CFG1_F32	 0x0020	/* 128-byte FIFO threshold */
#define  ISP_CFG1_F16	 0x0010	/* 128-byte FIFO threshold */
#define  ISP_CFG1_BENAB	 0x0004	/* Global Bus burst enable */
#define  ISP_CFG1_SXP	 0x0001	/* SXP register select */
#define PCI_INTF_CTL	0x08	/* pci interface control */
#define PCI_INTF_STS	0x0a	/* pci interface status */
#define PCI_SEMAPHORE	0x0c	/* pci semaphore */
#define PCI_NVRAM	0x0e	/* pci nvram interface */
#define CDMA_CONF	0x20	/* Command DMA Config */
#define DDMA_CONF	0x40	/* Data DMA Config */
#define  DMA_CONF_SENAB	 0x0008	/* SXP to DMA Data enable */
#define  DMA_CONF_RIRQ	 0x0004	/* RISC interrupt enable */
#define  DMA_CONF_BENAB	 0x0002	/* Bus burst enable */
#define  DMA_CONF_DIR	 0x0001	/* DMA direction (0=fifo->host 1=host->fifo) */

/* mailbox registers */
#define MBOX0		0x70	/* mailbox 0 */
#define MBOX1		0x72	/* mailbox 1 */
#define MBOX2		0x74	/* mailbox 2 */
#define MBOX3		0x76	/* mailbox 3 */
#define MBOX4		0x78	/* mailbox 4 */
#define MBOX5		0x7a	/* mailbox 5 */
#define MBOX6           0x7c    /* mailbox 6 */
#define MBOX7           0x7e    /* mailbox 7 */

/* mailbox command complete status codes */
#define MBOX_COMMAND_COMPLETE		0x4000
#define INVALID_COMMAND			0x4001
#define HOST_INTERFACE_ERROR		0x4002
#define TEST_FAILED			0x4003
#define COMMAND_ERROR			0x4005
#define COMMAND_PARAM_ERROR		0x4006

/* async event status codes */
#define ASYNC_SCSI_BUS_RESET		0x8001
#define SYSTEM_ERROR			0x8002
#define REQUEST_TRANSFER_ERROR		0x8003
#define RESPONSE_TRANSFER_ERROR		0x8004
#define REQUEST_QUEUE_WAKEUP		0x8005
#define EXECUTION_TIMEOUT_RESET		0x8006

#ifdef CONFIG_QL_ISP_A64
#define IOCB_SEGS                       2
#define CONTINUATION_SEGS               5
#define MAX_CONTINUATION_ENTRIES        254
#else
#define IOCB_SEGS                       4
#define CONTINUATION_SEGS               7
#endif /* CONFIG_QL_ISP_A64 */

struct Entry_header {
	u_char	entry_type;
	u_char	entry_cnt;
	u_char	sys_def_1;
	u_char	flags;
};

/* entry header type commands */
#ifdef CONFIG_QL_ISP_A64
#define ENTRY_COMMAND           9
#define ENTRY_CONTINUATION      0xa
#else
#define ENTRY_COMMAND		1
#define ENTRY_CONTINUATION	2
#endif /* CONFIG_QL_ISP_A64 */

#define ENTRY_STATUS		3
#define ENTRY_MARKER		4
#define ENTRY_EXTENDED_COMMAND	5

/* entry header flag definitions */
#define EFLAG_CONTINUATION	1
#define EFLAG_BUSY		2
#define EFLAG_BAD_HEADER	4
#define EFLAG_BAD_PAYLOAD	8

struct dataseg {
	u_int			d_base;
#ifdef CONFIG_QL_ISP_A64
	u_int                   d_base_hi;
#endif
	u_int			d_count;
};

struct Command_Entry {
	struct Entry_header	hdr;
	u_int			handle;
	u_char			target_lun;
	u_char			target_id;
	u_short			cdb_length;
	u_short			control_flags;
	u_short			rsvd;
	u_short			time_out;
	u_short			segment_cnt;
	u_char			cdb[12];
#ifdef CONFIG_QL_ISP_A64
	u_int                   rsvd1;
	u_int                   rsvd2;
#endif
	struct dataseg		dataseg[IOCB_SEGS];
};

/* command entry control flag definitions */
#define CFLAG_NODISC		0x01
#define CFLAG_HEAD_TAG		0x02
#define CFLAG_ORDERED_TAG	0x04
#define CFLAG_SIMPLE_TAG	0x08
#define CFLAG_TAR_RTN		0x10
#define CFLAG_READ		0x20
#define CFLAG_WRITE		0x40

struct Ext_Command_Entry {
	struct Entry_header	hdr;
	u_int			handle;
	u_char			target_lun;
	u_char			target_id;
	u_short			cdb_length;
	u_short			control_flags;
	u_short			rsvd;
	u_short			time_out;
	u_short			segment_cnt;
	u_char			cdb[44];
};

struct Continuation_Entry {
	struct Entry_header	hdr;
#ifndef CONFIG_QL_ISP_A64
	u_int			reserved;
#endif
	struct dataseg		dataseg[CONTINUATION_SEGS];
};

struct Marker_Entry {
	struct Entry_header	hdr;
	u_int			reserved;
	u_char			target_lun;
	u_char			target_id;
	u_char			modifier;
	u_char			rsvd;
	u_char			rsvds[52];
};

/* marker entry modifier definitions */
#define SYNC_DEVICE	0
#define SYNC_TARGET	1
#define SYNC_ALL	2

struct Status_Entry {
	struct Entry_header	hdr;
	u_int			handle;
	u_short			scsi_status;
	u_short			completion_status;
	u_short			state_flags;
	u_short			status_flags;
	u_short			time;
	u_short			req_sense_len;
	u_int			residual;
	u_char			rsvd[8];
	u_char			req_sense_data[32];
};

/* status entry completion status definitions */
#define CS_COMPLETE			0x0000
#define CS_INCOMPLETE			0x0001
#define CS_DMA_ERROR			0x0002
#define CS_TRANSPORT_ERROR		0x0003
#define CS_RESET_OCCURRED		0x0004
#define CS_ABORTED			0x0005
#define CS_TIMEOUT			0x0006
#define CS_DATA_OVERRUN			0x0007
#define CS_COMMAND_OVERRUN		0x0008
#define CS_STATUS_OVERRUN		0x0009
#define CS_BAD_MESSAGE			0x000a
#define CS_NO_MESSAGE_OUT		0x000b
#define CS_EXT_ID_FAILED		0x000c
#define CS_IDE_MSG_FAILED		0x000d
#define CS_ABORT_MSG_FAILED		0x000e
#define CS_REJECT_MSG_FAILED		0x000f
#define CS_NOP_MSG_FAILED		0x0010
#define CS_PARITY_ERROR_MSG_FAILED	0x0011
#define CS_DEVICE_RESET_MSG_FAILED	0x0012
#define CS_ID_MSG_FAILED		0x0013
#define CS_UNEXP_BUS_FREE		0x0014
#define CS_DATA_UNDERRUN		0x0015

/* status entry state flag definitions */
#define SF_GOT_BUS			0x0100
#define SF_GOT_TARGET			0x0200
#define SF_SENT_CDB			0x0400
#define SF_TRANSFERRED_DATA		0x0800
#define SF_GOT_STATUS			0x1000
#define SF_GOT_SENSE			0x2000

/* status entry status flag definitions */
#define STF_DISCONNECT			0x0001
#define STF_SYNCHRONOUS			0x0002
#define STF_PARITY_ERROR		0x0004
#define STF_BUS_RESET			0x0008
#define STF_DEVICE_RESET		0x0010
#define STF_ABORTED			0x0020
#define STF_TIMEOUT			0x0040
#define STF_NEGOTIATION			0x0080

/* interface control commands */
#define ISP_RESET			0x0001
#define ISP_EN_INT			0x0002
#define ISP_EN_RISC			0x0004

/* host control commands */
#define HCCR_NOP			0x0000
#define HCCR_RESET			0x1000
#define HCCR_PAUSE			0x2000
#define HCCR_RELEASE			0x3000
#define HCCR_SINGLE_STEP		0x4000
#define HCCR_SET_HOST_INTR		0x5000
#define HCCR_CLEAR_HOST_INTR		0x6000
#define HCCR_CLEAR_RISC_INTR		0x7000
#define HCCR_BP_ENABLE			0x8000
#define HCCR_BIOS_DISABLE		0x9000
#define HCCR_TEST_MODE			0xf000

#define RISC_BUSY			0x0004

/* mailbox commands */
#define MBOX_NO_OP			0x0000
#define MBOX_LOAD_RAM			0x0001
#define MBOX_EXEC_FIRMWARE		0x0002
#define MBOX_DUMP_RAM			0x0003
#define MBOX_WRITE_RAM_WORD		0x0004
#define MBOX_READ_RAM_WORD		0x0005
#define MBOX_MAILBOX_REG_TEST		0x0006
#define MBOX_VERIFY_CHECKSUM		0x0007
#define MBOX_ABOUT_FIRMWARE		0x0008
#define MBOX_CHECK_FIRMWARE		0x000e
#define MBOX_INIT_REQ_QUEUE		0x0010
#define MBOX_INIT_RES_QUEUE		0x0011
#define MBOX_EXECUTE_IOCB		0x0012
#define MBOX_WAKE_UP			0x0013
#define MBOX_STOP_FIRMWARE		0x0014
#define MBOX_ABORT			0x0015
#define MBOX_ABORT_DEVICE		0x0016
#define MBOX_ABORT_TARGET		0x0017
#define MBOX_BUS_RESET			0x0018
#define MBOX_STOP_QUEUE			0x0019
#define MBOX_START_QUEUE		0x001a
#define MBOX_SINGLE_STEP_QUEUE		0x001b
#define MBOX_ABORT_QUEUE		0x001c
#define MBOX_GET_DEV_QUEUE_STATUS	0x001d
#define MBOX_GET_FIRMWARE_STATUS	0x001f
#define MBOX_GET_INIT_SCSI_ID		0x0020
#define MBOX_GET_SELECT_TIMEOUT		0x0021
#define MBOX_GET_RETRY_COUNT		0x0022
#define MBOX_GET_TAG_AGE_LIMIT		0x0023
#define MBOX_GET_CLOCK_RATE		0x0024
#define MBOX_GET_ACT_NEG_STATE		0x0025
#define MBOX_GET_ASYNC_DATA_SETUP_TIME	0x0026
#define MBOX_GET_PCI_PARAMS		0x0027
#define MBOX_GET_TARGET_PARAMS		0x0028
#define MBOX_GET_DEV_QUEUE_PARAMS	0x0029
#define MBOX_SET_INIT_SCSI_ID		0x0030
#define MBOX_SET_SELECT_TIMEOUT		0x0031
#define MBOX_SET_RETRY_COUNT		0x0032
#define MBOX_SET_TAG_AGE_LIMIT		0x0033
#define MBOX_SET_CLOCK_RATE		0x0034
#define MBOX_SET_ACTIVE_NEG_STATE	0x0035
#define MBOX_SET_ASYNC_DATA_SETUP_TIME	0x0036
#define MBOX_SET_PCI_CONTROL_PARAMS	0x0037
#define MBOX_SET_TARGET_PARAMS		0x0038
#define MBOX_SET_DEV_QUEUE_PARAMS	0x0039
#define MBOX_RETURN_BIOS_BLOCK_ADDR	0x0040
#define MBOX_WRITE_FOUR_RAM_WORDS	0x0041
#define MBOX_EXEC_BIOS_IOCB		0x0042

#ifdef CONFIG_QL_ISP_A64
#define MBOX_CMD_INIT_REQUEST_QUEUE_64      0x0052
#define MBOX_CMD_INIT_RESPONSE_QUEUE_64     0x0053
#endif /* CONFIG_QL_ISP_A64 */

#include "qlogicisp_asm.c"

#define PACKB(a, b)			(((a)<<4)|(b))

static const u_char mbox_param[] = {
	PACKB(1, 1),	/* MBOX_NO_OP */
	PACKB(5, 5),	/* MBOX_LOAD_RAM */
	PACKB(2, 0),	/* MBOX_EXEC_FIRMWARE */
	PACKB(5, 5),	/* MBOX_DUMP_RAM */
	PACKB(3, 3),	/* MBOX_WRITE_RAM_WORD */
	PACKB(2, 3),	/* MBOX_READ_RAM_WORD */
	PACKB(6, 6),	/* MBOX_MAILBOX_REG_TEST */
	PACKB(2, 3),	/* MBOX_VERIFY_CHECKSUM	*/
	PACKB(1, 3),	/* MBOX_ABOUT_FIRMWARE */
	PACKB(0, 0),	/* 0x0009 */
	PACKB(0, 0),	/* 0x000a */
	PACKB(0, 0),	/* 0x000b */
	PACKB(0, 0),	/* 0x000c */
	PACKB(0, 0),	/* 0x000d */
	PACKB(1, 2),	/* MBOX_CHECK_FIRMWARE */
	PACKB(0, 0),	/* 0x000f */
	PACKB(5, 5),	/* MBOX_INIT_REQ_QUEUE */
	PACKB(6, 6),	/* MBOX_INIT_RES_QUEUE */
	PACKB(4, 4),	/* MBOX_EXECUTE_IOCB */
	PACKB(2, 2),	/* MBOX_WAKE_UP	*/
	PACKB(1, 6),	/* MBOX_STOP_FIRMWARE */
	PACKB(4, 4),	/* MBOX_ABORT */
	PACKB(2, 2),	/* MBOX_ABORT_DEVICE */
	PACKB(3, 3),	/* MBOX_ABORT_TARGET */
	PACKB(2, 2),	/* MBOX_BUS_RESET */
	PACKB(2, 3),	/* MBOX_STOP_QUEUE */
	PACKB(2, 3),	/* MBOX_START_QUEUE */
	PACKB(2, 3),	/* MBOX_SINGLE_STEP_QUEUE */
	PACKB(2, 3),	/* MBOX_ABORT_QUEUE */
	PACKB(2, 4),	/* MBOX_GET_DEV_QUEUE_STATUS */
	PACKB(0, 0),	/* 0x001e */
	PACKB(1, 3),	/* MBOX_GET_FIRMWARE_STATUS */
	PACKB(1, 2),	/* MBOX_GET_INIT_SCSI_ID */
	PACKB(1, 2),	/* MBOX_GET_SELECT_TIMEOUT */
	PACKB(1, 3),	/* MBOX_GET_RETRY_COUNT	*/
	PACKB(1, 2),	/* MBOX_GET_TAG_AGE_LIMIT */
	PACKB(1, 2),	/* MBOX_GET_CLOCK_RATE */
	PACKB(1, 2),	/* MBOX_GET_ACT_NEG_STATE */
	PACKB(1, 2),	/* MBOX_GET_ASYNC_DATA_SETUP_TIME */
	PACKB(1, 3),	/* MBOX_GET_PCI_PARAMS */
	PACKB(2, 4),	/* MBOX_GET_TARGET_PARAMS */
	PACKB(2, 4),	/* MBOX_GET_DEV_QUEUE_PARAMS */
	PACKB(0, 0),	/* 0x002a */
	PACKB(0, 0),	/* 0x002b */
	PACKB(0, 0),	/* 0x002c */
	PACKB(0, 0),	/* 0x002d */
	PACKB(0, 0),	/* 0x002e */
	PACKB(0, 0),	/* 0x002f */
	PACKB(2, 2),	/* MBOX_SET_INIT_SCSI_ID */
	PACKB(2, 2),	/* MBOX_SET_SELECT_TIMEOUT */
	PACKB(3, 3),	/* MBOX_SET_RETRY_COUNT	*/
	PACKB(2, 2),	/* MBOX_SET_TAG_AGE_LIMIT */
	PACKB(2, 2),	/* MBOX_SET_CLOCK_RATE */
	PACKB(2, 2),	/* MBOX_SET_ACTIVE_NEG_STATE */
	PACKB(2, 2),	/* MBOX_SET_ASYNC_DATA_SETUP_TIME */
	PACKB(3, 3),	/* MBOX_SET_PCI_CONTROL_PARAMS */
	PACKB(4, 4),	/* MBOX_SET_TARGET_PARAMS */
	PACKB(4, 4),	/* MBOX_SET_DEV_QUEUE_PARAMS */
	PACKB(0, 0),	/* 0x003a */
	PACKB(0, 0),	/* 0x003b */
	PACKB(0, 0),	/* 0x003c */
	PACKB(0, 0),	/* 0x003d */
	PACKB(0, 0),	/* 0x003e */
	PACKB(0, 0),	/* 0x003f */
	PACKB(1, 2),	/* MBOX_RETURN_BIOS_BLOCK_ADDR */
	PACKB(6, 1),	/* MBOX_WRITE_FOUR_RAM_WORDS */
	PACKB(2, 3)	/* MBOX_EXEC_BIOS_IOCB */
#ifdef CONFIG_QL_ISP_A64
	,PACKB(0, 0),	/* 0x0043 */
	PACKB(0, 0),	/* 0x0044 */
	PACKB(0, 0),	/* 0x0045 */
	PACKB(0, 0),	/* 0x0046 */
	PACKB(0, 0),	/* 0x0047 */
	PACKB(0, 0),	/* 0x0048 */
	PACKB(0, 0),	/* 0x0049 */
	PACKB(0, 0),	/* 0x004a */
	PACKB(0, 0),	/* 0x004b */
	PACKB(0, 0),	/* 0x004c */
	PACKB(0, 0),	/* 0x004d */
	PACKB(0, 0),	/* 0x004e */
	PACKB(0, 0),	/* 0x004f */
	PACKB(0, 0),	/* 0x0050 */
	PACKB(0, 0),	/* 0x0051 */
	PACKB(8, 8),	/* MBOX_CMD_INIT_REQUEST_QUEUE_64 (0x0052) */
	PACKB(8, 8)	/* MBOX_CMD_INIT_RESPONSE_QUEUE_64 (0x0053) */
#endif /* CONFIG_QL_ISP_A64 */
};

#define MAX_MBOX_COMMAND	(sizeof(mbox_param)/sizeof(u_short))

struct host_param {
	u_short		fifo_threshold;
	u_short		host_adapter_enable;
	u_short		initiator_scsi_id;
	u_short		bus_reset_delay;
	u_short		retry_count;
	u_short		retry_delay;
	u_short		async_data_setup_time;
	u_short		req_ack_active_negation;
	u_short		data_line_active_negation;
	u_short		data_dma_burst_enable;
	u_short		command_dma_burst_enable;
	u_short		tag_aging;
	u_short		selection_timeout;
	u_short		max_queue_depth;
};

/*
 * Device Flags:
 *
 * Bit  Name
 * ---------
 *  7   Disconnect Privilege
 *  6   Parity Checking
 *  5   Wide Data Transfers
 *  4   Synchronous Data Transfers
 *  3   Tagged Queuing
 *  2   Automatic Request Sense
 *  1   Stop Queue on Check Condition
 *  0   Renegotiate on Error
 */

struct dev_param {
	u_short		device_flags;
	u_short		execution_throttle;
	u_short		synchronous_period;
	u_short		synchronous_offset;
	u_short		device_enable;
	u_short		reserved; /* pad */
};

/*
 * The result queue can be quite a bit smaller since continuation entries
 * do not show up there:
 */
#define RES_QUEUE_LEN		((QLOGICISP_REQ_QUEUE_LEN + 1) / 8 - 1)
#define QUEUE_ENTRY_LEN		64
#define QSIZE(entries)  (((entries) + 1) * QUEUE_ENTRY_LEN)

struct isp_queue_entry {
	char __opaque[QUEUE_ENTRY_LEN];
};

struct isp1020_hostdata {
	void __iomem *memaddr;
	u_char	revision;
	struct	host_param host_param;
	struct	dev_param dev_param[MAX_TARGETS];
	struct	pci_dev *pci_dev;
	
	struct isp_queue_entry *res_cpu; /* CPU-side address of response queue. */
	struct isp_queue_entry *req_cpu; /* CPU-size address of request queue. */

	/* result and request queues (shared with isp1020): */
	u_int	req_in_ptr;		/* index of next request slot */
	u_int	res_out_ptr;		/* index of next result slot */

	/* this is here so the queues are nicely aligned */
	long	send_marker;		/* do we need to send a marker? */

	/* The cmd->handle has a fixed size, and is only 32-bits.  We
	 * need to take care to handle 64-bit systems correctly thus what
	 * we actually place in cmd->handle is an index to the following
	 * table.  Kudos to Matt Jacob for the technique.  -DaveM
	 */
	Scsi_Cmnd *cmd_slots[QLOGICISP_REQ_QUEUE_LEN + 1];

	dma_addr_t res_dma;	/* PCI side view of response queue */
	dma_addr_t req_dma;	/* PCI side view of request queue */
};

/* queue length's _must_ be power of two: */
#define QUEUE_DEPTH(in, out, ql)	((in - out) & (ql))
#define REQ_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, 		     \
						    QLOGICISP_REQ_QUEUE_LEN)
#define RES_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, RES_QUEUE_LEN)

static void	isp1020_enable_irqs(struct Scsi_Host *);
static void	isp1020_disable_irqs(struct Scsi_Host *);
static int	isp1020_init(struct Scsi_Host *);
static int	isp1020_reset_hardware(struct Scsi_Host *);
static int	isp1020_set_defaults(struct Scsi_Host *);
static int	isp1020_load_parameters(struct Scsi_Host *);
static int	isp1020_mbox_command(struct Scsi_Host *, u_short []); 
static int	isp1020_return_status(struct Status_Entry *);
static void	isp1020_intr_handler(int, void *, struct pt_regs *);
static irqreturn_t do_isp1020_intr_handler(int, void *, struct pt_regs *);

#if USE_NVRAM_DEFAULTS
static int	isp1020_get_defaults(struct Scsi_Host *);
static int	isp1020_verify_nvram(struct Scsi_Host *);
static u_short	isp1020_read_nvram_word(struct Scsi_Host *, u_short);
#endif

#if DEBUG_ISP1020
static void	isp1020_print_scsi_cmd(Scsi_Cmnd *);
#endif
#if DEBUG_ISP1020_INTR
static void	isp1020_print_status_entry(struct Status_Entry *);
#endif

/* memaddr should be used to determine if memmapped port i/o is being used
 * non-null memaddr == mmap'd
 * JV 7-Jan-2000
 */
static inline u_short isp_inw(struct Scsi_Host *host, long offset)
{
	struct isp1020_hostdata *h = (struct isp1020_hostdata *)host->hostdata;
	if (h->memaddr)
		return readw(h->memaddr + offset);
	else
		return inw(host->io_port + offset);
}

static inline void isp_outw(u_short val, struct Scsi_Host *host, long offset)
{
	struct isp1020_hostdata *h = (struct isp1020_hostdata *)host->hostdata;
	if (h->memaddr)
		writew(val, h->memaddr + offset);
	else
		outw(val, host->io_port + offset);
}

static inline void isp1020_enable_irqs(struct Scsi_Host *host)
{
	isp_outw(ISP_EN_INT|ISP_EN_RISC, host, PCI_INTF_CTL);
}


static inline void isp1020_disable_irqs(struct Scsi_Host *host)
{
	isp_outw(0x0, host, PCI_INTF_CTL);
}


static int isp1020_detect(Scsi_Host_Template *tmpt)
{
	int hosts = 0;
	struct Scsi_Host *host;
	struct isp1020_hostdata *hostdata;
	struct pci_dev *pdev = NULL;

	ENTER("isp1020_detect");

	tmpt->proc_name = "isp1020";

	while ((pdev = pci_find_device(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP1020, pdev)))
	{
		if (pci_enable_device(pdev))
			continue;

		host = scsi_register(tmpt, sizeof(struct isp1020_hostdata));
		if (!host)
			continue;

		hostdata = (struct isp1020_hostdata *) host->hostdata;

		memset(hostdata, 0, sizeof(struct isp1020_hostdata));

		hostdata->pci_dev = pdev;

		if (isp1020_init(host))
			goto fail_and_unregister;

		if (isp1020_reset_hardware(host)
#if USE_NVRAM_DEFAULTS
		    || isp1020_get_defaults(host)
#else
		    || isp1020_set_defaults(host)
#endif /* USE_NVRAM_DEFAULTS */
		    || isp1020_load_parameters(host)) {
			goto fail_uninit;
		}

		host->this_id = hostdata->host_param.initiator_scsi_id;
		host->max_sectors = 64;

		if (request_irq(host->irq, do_isp1020_intr_handler, SA_INTERRUPT | SA_SHIRQ,
				"qlogicisp", host))
		{
			printk("qlogicisp : interrupt %d already in use\n",
			       host->irq);
			goto fail_uninit;
		}

		isp_outw(0x0, host, PCI_SEMAPHORE);
		isp_outw(HCCR_CLEAR_RISC_INTR, host, HOST_HCCR);
		isp1020_enable_irqs(host);

		hosts++;
		continue;

	fail_uninit:
		iounmap(hostdata->memaddr);
		release_region(host->io_port, 0xff);
	fail_and_unregister:
		if (hostdata->res_cpu)
			pci_free_consistent(hostdata->pci_dev,
					    QSIZE(RES_QUEUE_LEN),
					    hostdata->res_cpu,
					    hostdata->res_dma);
		if (hostdata->req_cpu)
			pci_free_consistent(hostdata->pci_dev,
					    QSIZE(QLOGICISP_REQ_QUEUE_LEN),
					    hostdata->req_cpu,
					    hostdata->req_dma);
		scsi_unregister(host);
	}

	LEAVE("isp1020_detect");

	return hosts;
}


static int isp1020_release(struct Scsi_Host *host)
{
	struct isp1020_hostdata *hostdata;

	ENTER("isp1020_release");

	hostdata = (struct isp1020_hostdata *) host->hostdata;

	isp_outw(0x0, host, PCI_INTF_CTL);
	free_irq(host->irq, host);

	iounmap(hostdata->memaddr);

	release_region(host->io_port, 0xff);

	LEAVE("isp1020_release");

	return 0;
}


static const char *isp1020_info(struct Scsi_Host *host)
{
	static char buf[80];
	struct isp1020_hostdata *hostdata;

	ENTER("isp1020_info");

	hostdata = (struct isp1020_hostdata *) host->hostdata;
	sprintf(buf,
		"QLogic ISP1020 SCSI on PCI bus %02x device %02x irq %d %s base 0x%lx",
		hostdata->pci_dev->bus->number, hostdata->pci_dev->devfn, host->irq,
		(hostdata->memaddr ? "MEM" : "I/O"),
		(hostdata->memaddr ? (unsigned long)hostdata->memaddr : host->io_port));

	LEAVE("isp1020_info");

	return buf;
}


/*
 * The middle SCSI layer ensures that queuecommand never gets invoked
 * concurrently with itself or the interrupt handler (though the
 * interrupt handler may call this routine as part of
 * request-completion handling).
 */
static int isp1020_queuecommand(Scsi_Cmnd *Cmnd, void (*done)(Scsi_Cmnd *))
{
	int i, n, num_free;
	u_int in_ptr, out_ptr;
	struct dataseg * ds;
	struct scatterlist *sg;
	struct Command_Entry *cmd;
	struct Continuation_Entry *cont;
	struct Scsi_Host *host;
	struct isp1020_hostdata *hostdata;
	dma_addr_t	dma_addr;

	ENTER("isp1020_queuecommand");

	host = Cmnd->device->host;
	hostdata = (struct isp1020_hostdata *) host->hostdata;
	Cmnd->scsi_done = done;

	DEBUG(isp1020_print_scsi_cmd(Cmnd));

	out_ptr = isp_inw(host, + MBOX4);
	in_ptr  = hostdata->req_in_ptr;

	DEBUG(printk("qlogicisp : request queue depth %d\n",
		     REQ_QUEUE_DEPTH(in_ptr, out_ptr)));

	cmd = (struct Command_Entry *) &hostdata->req_cpu[in_ptr];
	in_ptr = (in_ptr + 1) & QLOGICISP_REQ_QUEUE_LEN;
	if (in_ptr == out_ptr) {
		printk("qlogicisp : request queue overflow\n");
		return 1;
	}

	if (hostdata->send_marker) {
		struct Marker_Entry *marker;

		TRACE("queue marker", in_ptr, 0);

		DEBUG(printk("qlogicisp : adding marker entry\n"));
		marker = (struct Marker_Entry *) cmd;
		memset(marker, 0, sizeof(struct Marker_Entry));

		marker->hdr.entry_type = ENTRY_MARKER;
		marker->hdr.entry_cnt = 1;
		marker->modifier = SYNC_ALL;

		hostdata->send_marker = 0;

		if (((in_ptr + 1) & QLOGICISP_REQ_QUEUE_LEN) == out_ptr) {
			isp_outw(in_ptr, host, MBOX4);
			hostdata->req_in_ptr = in_ptr;
			printk("qlogicisp : request queue overflow\n");
			return 1;
		}
		cmd = (struct Command_Entry *) &hostdata->req_cpu[in_ptr];
		in_ptr = (in_ptr + 1) & QLOGICISP_REQ_QUEUE_LEN;
	}

	TRACE("queue command", in_ptr, Cmnd);

	memset(cmd, 0, sizeof(struct Command_Entry));

	cmd->hdr.entry_type = ENTRY_COMMAND;
	cmd->hdr.entry_cnt = 1;

	cmd->target_lun = Cmnd->device->lun;
	cmd->target_id = Cmnd->device->id;
	cmd->cdb_length = cpu_to_le16(Cmnd->cmd_len);
	cmd->control_flags = cpu_to_le16(CFLAG_READ | CFLAG_WRITE);
	cmd->time_out = cpu_to_le16(30);

	memcpy(cmd->cdb, Cmnd->cmnd, Cmnd->cmd_len);

	if (Cmnd->use_sg) {
		int sg_count;

		sg = (struct scatterlist *) Cmnd->request_buffer;
		ds = cmd->dataseg;

		sg_count = pci_map_sg(hostdata->pci_dev, sg, Cmnd->use_sg,
				      Cmnd->sc_data_direction);

		cmd->segment_cnt = cpu_to_le16(sg_count);

		/* fill in first four sg entries: */
		n = sg_count;
		if (n > IOCB_SEGS)
			n = IOCB_SEGS;
		for (i = 0; i < n; i++) {
			dma_addr = sg_dma_address(sg);
			ds[i].d_base  = cpu_to_le32((u32) dma_addr);
#ifdef CONFIG_QL_ISP_A64
			ds[i].d_base_hi = cpu_to_le32((u32) (dma_addr>>32));
#endif /* CONFIG_QL_ISP_A64 */
			ds[i].d_count = cpu_to_le32(sg_dma_len(sg));
			++sg;
		}
		sg_count -= IOCB_SEGS;

		while (sg_count > 0) {
			++cmd->hdr.entry_cnt;
			cont = (struct Continuation_Entry *)
				&hostdata->req_cpu[in_ptr];
			in_ptr = (in_ptr + 1) & QLOGICISP_REQ_QUEUE_LEN;
			if (in_ptr == out_ptr) {
				printk("isp1020: unexpected request queue "
				       "overflow\n");
				return 1;
			}
			TRACE("queue continuation", in_ptr, 0);
			cont->hdr.entry_type = ENTRY_CONTINUATION;
			cont->hdr.entry_cnt  = 0;
			cont->hdr.sys_def_1  = 0;
			cont->hdr.flags      = 0;
#ifndef CONFIG_QL_ISP_A64
			cont->reserved = 0;
#endif
			ds = cont->dataseg;
			n = sg_count;
			if (n > CONTINUATION_SEGS)
				n = CONTINUATION_SEGS;
			for (i = 0; i < n; ++i) {
				dma_addr = sg_dma_address(sg);
				ds[i].d_base = cpu_to_le32((u32) dma_addr);
#ifdef CONFIG_QL_ISP_A64
				ds[i].d_base_hi = cpu_to_le32((u32)(dma_addr>>32));
#endif /* CONFIG_QL_ISP_A64 */
				ds[i].d_count = cpu_to_le32(sg_dma_len(sg));
				++sg;
			}
			sg_count -= n;
		}
	} else if (Cmnd->request_bufflen) {
		/*Cmnd->SCp.ptr = (char *)(unsigned long)*/
		dma_addr = pci_map_single(hostdata->pci_dev,
				       Cmnd->request_buffer,
				       Cmnd->request_bufflen,
				       Cmnd->sc_data_direction);
		Cmnd->SCp.ptr = (char *)(unsigned long) dma_addr;

		cmd->dataseg[0].d_base =
			cpu_to_le32((u32) dma_addr);
#ifdef CONFIG_QL_ISP_A64
		cmd->dataseg[0].d_base_hi =
			cpu_to_le32((u32) (dma_addr>>32));
#endif /* CONFIG_QL_ISP_A64 */
		cmd->dataseg[0].d_count =
			cpu_to_le32((u32)Cmnd->request_bufflen);
		cmd->segment_cnt = cpu_to_le16(1);
	} else {
		cmd->dataseg[0].d_base = 0;
#ifdef CONFIG_QL_ISP_A64
		cmd->dataseg[0].d_base_hi = 0;
#endif /* CONFIG_QL_ISP_A64 */
		cmd->dataseg[0].d_count = 0;
		cmd->segment_cnt = cpu_to_le16(1); /* Shouldn't this be 0? */
	}

	/* Committed, record Scsi_Cmd so we can find it later. */
	cmd->handle = in_ptr;
	hostdata->cmd_slots[in_ptr] = Cmnd;

	isp_outw(in_ptr, host, MBOX4);
	hostdata->req_in_ptr = in_ptr;

	num_free = QLOGICISP_REQ_QUEUE_LEN - REQ_QUEUE_DEPTH(in_ptr, out_ptr);
	host->can_queue = host->host_busy + num_free;
	host->sg_tablesize = QLOGICISP_MAX_SG(num_free);

	LEAVE("isp1020_queuecommand");

	return 0;
}


#define ASYNC_EVENT_INTERRUPT	0x01

irqreturn_t do_isp1020_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct Scsi_Host *host = dev_id;
	unsigned long flags;

	spin_lock_irqsave(host->host_lock, flags);
	isp1020_intr_handler(irq, dev_id, regs);
	spin_unlock_irqrestore(host->host_lock, flags);

	return IRQ_HANDLED;
}

void isp1020_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	Scsi_Cmnd *Cmnd;
	struct Status_Entry *sts;
	struct Scsi_Host *host = dev_id;
	struct isp1020_hostdata *hostdata;
	u_int in_ptr, out_ptr;
	u_short status;

	ENTER_INTR("isp1020_intr_handler");

	hostdata = (struct isp1020_hostdata *) host->hostdata;

	DEBUG_INTR(printk("qlogicisp : interrupt on line %d\n", irq));

	if (!(isp_inw(host, PCI_INTF_STS) & 0x04)) {
		/* spurious interrupts can happen legally */
		DEBUG_INTR(printk("qlogicisp: got spurious interrupt\n"));
		return;
	}
	in_ptr = isp_inw(host, MBOX5);
	isp_outw(HCCR_CLEAR_RISC_INTR, host, HOST_HCCR);

	if ((isp_inw(host, PCI_SEMAPHORE) & ASYNC_EVENT_INTERRUPT)) {
		status = isp_inw(host, MBOX0);

		DEBUG_INTR(printk("qlogicisp : mbox completion status: %x\n",
				  status));

		switch (status) {
		      case ASYNC_SCSI_BUS_RESET:
		      case EXECUTION_TIMEOUT_RESET:
			hostdata->send_marker = 1;
			break;
		      case INVALID_COMMAND:
		      case HOST_INTERFACE_ERROR:
		      case COMMAND_ERROR:
		      case COMMAND_PARAM_ERROR:
			printk("qlogicisp : bad mailbox return status\n");
			break;
		}
		isp_outw(0x0, host, PCI_SEMAPHORE);
	}
	out_ptr = hostdata->res_out_ptr;

	DEBUG_INTR(printk("qlogicisp : response queue update\n"));
	DEBUG_INTR(printk("qlogicisp : response queue depth %d\n",
			  QUEUE_DEPTH(in_ptr, out_ptr, RES_QUEUE_LEN)));

	while (out_ptr != in_ptr) {
		u_int cmd_slot;

		sts = (struct Status_Entry *) &hostdata->res_cpu[out_ptr];
		out_ptr = (out_ptr + 1) & RES_QUEUE_LEN;

		cmd_slot = sts->handle;
		Cmnd = hostdata->cmd_slots[cmd_slot];
		hostdata->cmd_slots[cmd_slot] = NULL;

		TRACE("done", out_ptr, Cmnd);

		if (le16_to_cpu(sts->completion_status) == CS_RESET_OCCURRED
		    || le16_to_cpu(sts->completion_status) == CS_ABORTED
		    || (le16_to_cpu(sts->status_flags) & STF_BUS_RESET))
			hostdata->send_marker = 1;

		if (le16_to_cpu(sts->state_flags) & SF_GOT_SENSE)
			memcpy(Cmnd->sense_buffer, sts->req_sense_data,
			       sizeof(Cmnd->sense_buffer));

		DEBUG_INTR(isp1020_print_status_entry(sts));

		if (sts->hdr.entry_type == ENTRY_STATUS)
			Cmnd->result = isp1020_return_status(sts);
		else
			Cmnd->result = DID_ERROR << 16;

		if (Cmnd->use_sg)
			pci_unmap_sg(hostdata->pci_dev,
				     (struct scatterlist *)Cmnd->buffer,
				     Cmnd->use_sg,
				     Cmnd->sc_data_direction);
		else if (Cmnd->request_bufflen)
			pci_unmap_single(hostdata->pci_dev,
#ifdef CONFIG_QL_ISP_A64
					 (dma_addr_t)((long)Cmnd->SCp.ptr),
#else
					 (u32)((long)Cmnd->SCp.ptr),
#endif
					 Cmnd->request_bufflen,
					 Cmnd->sc_data_direction);

		isp_outw(out_ptr, host, MBOX5);
		(*Cmnd->scsi_done)(Cmnd);
	}
	hostdata->res_out_ptr = out_ptr;

	LEAVE_INTR("isp1020_intr_handler");
}


static int isp1020_return_status(struct Status_Entry *sts)
{
	int host_status = DID_ERROR;
#if DEBUG_ISP1020_INTR
	static char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif /* DEBUG_ISP1020_INTR */

	ENTER("isp1020_return_status");

	DEBUG(printk("qlogicisp : completion status = 0x%04x\n",
		     le16_to_cpu(sts->completion_status)));

	switch(le16_to_cpu(sts->completion_status)) {
	      case CS_COMPLETE:
		host_status = DID_OK;
		break;
	      case CS_INCOMPLETE:
		if (!(le16_to_cpu(sts->state_flags) & SF_GOT_BUS))
			host_status = DID_NO_CONNECT;
		else if (!(le16_to_cpu(sts->state_flags) & SF_GOT_TARGET))
			host_status = DID_BAD_TARGET;
		else if (!(le16_to_cpu(sts->state_flags) & SF_SENT_CDB))
			host_status = DID_ERROR;
		else if (!(le16_to_cpu(sts->state_flags) & SF_TRANSFERRED_DATA))
			host_status = DID_ERROR;
		else if (!(le16_to_cpu(sts->state_flags) & SF_GOT_STATUS))
			host_status = DID_ERROR;
		else if (!(le16_to_cpu(sts->state_flags) & SF_GOT_SENSE))
			host_status = DID_ERROR;
		break;
	      case CS_DMA_ERROR:
	      case CS_TRANSPORT_ERROR:
		host_status = DID_ERROR;
		break;
	      case CS_RESET_OCCURRED:
		host_status = DID_RESET;
		break;
	      case CS_ABORTED:
		host_status = DID_ABORT;
		break;
	      case CS_TIMEOUT:
		host_status = DID_TIME_OUT;
		break;
	      case CS_DATA_OVERRUN:
	      case CS_COMMAND_OVERRUN:
	      case CS_STATUS_OVERRUN:
	      case CS_BAD_MESSAGE:
	      case CS_NO_MESSAGE_OUT:
	      case CS_EXT_ID_FAILED:
	      case CS_IDE_MSG_FAILED:
	      case CS_ABORT_MSG_FAILED:
	      case CS_NOP_MSG_FAILED:
	      case CS_PARITY_ERROR_MSG_FAILED:
	      case CS_DEVICE_RESET_MSG_FAILED:
	      case CS_ID_MSG_FAILED:
	      case CS_UNEXP_BUS_FREE:
		host_status = DID_ERROR;
		break;
	      case CS_DATA_UNDERRUN:
		host_status = DID_OK;
		break;
	      default:
		printk("qlogicisp : unknown completion status 0x%04x\n",
		       le16_to_cpu(sts->completion_status));
		host_status = DID_ERROR;
		break;
	}

	DEBUG_INTR(printk("qlogicisp : host status (%s) scsi status %x\n",
			  reason[host_status], le16_to_cpu(sts->scsi_status)));

	LEAVE("isp1020_return_status");

	return (le16_to_cpu(sts->scsi_status) & STATUS_MASK) | (host_status << 16);
}


static int isp1020_biosparam(struct scsi_device *sdev, struct block_device *n,
		sector_t capacity, int ip[])
{
	int size = capacity;

	ENTER("isp1020_biosparam");

	ip[0] = 64;
	ip[1] = 32;
	ip[2] = size >> 11;
	if (ip[2] > 1024) {
		ip[0] = 255;
		ip[1] = 63;
		ip[2] = size / (ip[0] * ip[1]);
#if 0
		if (ip[2] > 1023)
			ip[2] = 1023;
#endif			
	}

	LEAVE("isp1020_biosparam");

	return 0;
}


static int isp1020_reset_hardware(struct Scsi_Host *host)
{
	u_short param[6];
	int loop_count;

	ENTER("isp1020_reset_hardware");

	isp_outw(ISP_RESET, host, PCI_INTF_CTL);
	udelay(100);
	isp_outw(HCCR_RESET, host, HOST_HCCR);
	udelay(100);
	isp_outw(HCCR_RELEASE, host, HOST_HCCR);
	isp_outw(HCCR_BIOS_DISABLE, host, HOST_HCCR);

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && isp_inw(host, HOST_HCCR) == RISC_BUSY) {
		barrier();
		cpu_relax();
	}
	if (!loop_count)
		printk("qlogicisp: reset_hardware loop timeout\n");

	isp_outw(0, host, ISP_CFG1);

#if DEBUG_ISP1020
	printk("qlogicisp : mbox 0 0x%04x \n", isp_inw(host, MBOX0));
	printk("qlogicisp : mbox 1 0x%04x \n", isp_inw(host, MBOX1));
	printk("qlogicisp : mbox 2 0x%04x \n", isp_inw(host, MBOX2));
	printk("qlogicisp : mbox 3 0x%04x \n", isp_inw(host, MBOX3));
	printk("qlogicisp : mbox 4 0x%04x \n", isp_inw(host, MBOX4));
	printk("qlogicisp : mbox 5 0x%04x \n", isp_inw(host, MBOX5));
#endif /* DEBUG_ISP1020 */

	param[0] = MBOX_NO_OP;
	isp1020_mbox_command(host, param);
	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : NOP test failed\n");
		return 1;
	}

	DEBUG(printk("qlogicisp : loading risc ram\n"));

#if RELOAD_FIRMWARE
	for (loop_count = 0; loop_count < risc_code_length01; loop_count++) {
		param[0] = MBOX_WRITE_RAM_WORD;
		param[1] = risc_code_addr01 + loop_count;
		param[2] = risc_code01[loop_count];
		isp1020_mbox_command(host, param);
		if (param[0] != MBOX_COMMAND_COMPLETE) {
			printk("qlogicisp : firmware load failure at %d\n",
			    loop_count);
			return 1;
		}
	}
#endif /* RELOAD_FIRMWARE */

	DEBUG(printk("qlogicisp : verifying checksum\n"));

	param[0] = MBOX_VERIFY_CHECKSUM;
	param[1] = risc_code_addr01;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : ram checksum failure\n");
		return 1;
	}

	DEBUG(printk("qlogicisp : executing firmware\n"));

	param[0] = MBOX_EXEC_FIRMWARE;
	param[1] = risc_code_addr01;

	isp1020_mbox_command(host, param);

	param[0] = MBOX_ABOUT_FIRMWARE;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : about firmware failure\n");
		return 1;
	}

	DEBUG(printk("qlogicisp : firmware major revision %d\n", param[1]));
	DEBUG(printk("qlogicisp : firmware minor revision %d\n", param[2]));

	LEAVE("isp1020_reset_hardware");

	return 0;
}


static int isp1020_init(struct Scsi_Host *sh)
{
	u_long io_base, mem_base, io_flags, mem_flags;
	struct isp1020_hostdata *hostdata;
	u_char revision;
	u_int irq;
	u_short command;
	struct pci_dev *pdev;

	ENTER("isp1020_init");

	hostdata = (struct isp1020_hostdata *) sh->hostdata;
	pdev = hostdata->pci_dev;

	if (pci_read_config_word(pdev, PCI_COMMAND, &command)
	    || pci_read_config_byte(pdev, PCI_CLASS_REVISION, &revision))
	{
		printk("qlogicisp : error reading PCI configuration\n");
		return 1;
	}

	io_base = pci_resource_start(pdev, 0);
	mem_base = pci_resource_start(pdev, 1);
	io_flags = pci_resource_flags(pdev, 0);
	mem_flags = pci_resource_flags(pdev, 1);
	irq = pdev->irq;

	if (pdev->vendor != PCI_VENDOR_ID_QLOGIC) {
		printk("qlogicisp : 0x%04x is not QLogic vendor ID\n",
		       pdev->vendor);
		return 1;
	}

	if (pdev->device != PCI_DEVICE_ID_QLOGIC_ISP1020) {
		printk("qlogicisp : 0x%04x does not match ISP1020 device id\n",
		       pdev->device);
		return 1;
	}

#ifdef __alpha__
	/* Force ALPHA to use bus I/O and not bus MEM.
	   This is to avoid having to use HAE_MEM registers,
	   which is broken on some platforms and with SMP.  */
	command &= ~PCI_COMMAND_MEMORY; 
#endif

	sh->io_port = io_base;

	if (!request_region(sh->io_port, 0xff, "qlogicisp")) {
		printk("qlogicisp : i/o region 0x%lx-0x%lx already "
		       "in use\n",
		       sh->io_port, sh->io_port + 0xff);
		return 1;
	}

 	if ((command & PCI_COMMAND_MEMORY) &&
 	    ((mem_flags & 1) == 0)) {
 		hostdata->memaddr = ioremap(mem_base, PAGE_SIZE);
		if (!hostdata->memaddr) {
 			printk("qlogicisp : i/o remapping failed.\n");
			goto out_release;
		}
 	} else {
		if (command & PCI_COMMAND_IO && (io_flags & 3) != 1) {
			printk("qlogicisp : i/o mapping is disabled\n");
			goto out_release;
 		}
 		hostdata->memaddr = NULL; /* zero to signify no i/o mapping */
 		mem_base = 0;
	}

	if (revision != ISP1020_REV_ID)
		printk("qlogicisp : new isp1020 revision ID (%d)\n", revision);

	if (isp_inw(sh,  PCI_ID_LOW) != PCI_VENDOR_ID_QLOGIC
	    || isp_inw(sh, PCI_ID_HIGH) != PCI_DEVICE_ID_QLOGIC_ISP1020)
	{
		printk("qlogicisp : can't decode %s address space 0x%lx\n",
		       (io_base ? "I/O" : "MEM"),
		       (io_base ? io_base : mem_base));
		goto out_unmap;
	}

	hostdata->revision = revision;

	sh->irq = irq;
	sh->max_id = MAX_TARGETS;
	sh->max_lun = MAX_LUNS;

	hostdata->res_cpu = pci_alloc_consistent(hostdata->pci_dev,
						 QSIZE(RES_QUEUE_LEN),
						 &hostdata->res_dma);
	if (hostdata->res_cpu == NULL) {
		printk("qlogicisp : can't allocate response queue\n");
		goto out_unmap;
	}

	hostdata->req_cpu = pci_alloc_consistent(hostdata->pci_dev,
						 QSIZE(QLOGICISP_REQ_QUEUE_LEN),
						 &hostdata->req_dma);
	if (hostdata->req_cpu == NULL) {
		pci_free_consistent(hostdata->pci_dev,
				    QSIZE(RES_QUEUE_LEN),
				    hostdata->res_cpu,
				    hostdata->res_dma);
		printk("qlogicisp : can't allocate request queue\n");
		goto out_unmap;
	}

	pci_set_master(pdev);

	LEAVE("isp1020_init");

	return 0;

out_unmap:
	iounmap(hostdata->memaddr);
out_release:
	release_region(sh->io_port, 0xff);
	return 1;
}


#if USE_NVRAM_DEFAULTS

static int isp1020_get_defaults(struct Scsi_Host *host)
{
	int i;
	u_short value;
	struct isp1020_hostdata *hostdata =
		(struct isp1020_hostdata *) host->hostdata;

	ENTER("isp1020_get_defaults");

	if (!isp1020_verify_nvram(host)) {
		printk("qlogicisp : nvram checksum failure\n");
		printk("qlogicisp : attempting to use default parameters\n");
		return isp1020_set_defaults(host);
	}

	value = isp1020_read_nvram_word(host, 2);
	hostdata->host_param.fifo_threshold = (value >> 8) & 0x03;
	hostdata->host_param.host_adapter_enable = (value >> 11) & 0x01;
	hostdata->host_param.initiator_scsi_id = (value >> 12) & 0x0f;

	value = isp1020_read_nvram_word(host, 3);
	hostdata->host_param.bus_reset_delay = value & 0xff;
	hostdata->host_param.retry_count = value >> 8;

	value = isp1020_read_nvram_word(host, 4);
	hostdata->host_param.retry_delay = value & 0xff;
	hostdata->host_param.async_data_setup_time = (value >> 8) & 0x0f;
	hostdata->host_param.req_ack_active_negation = (value >> 12) & 0x01;
	hostdata->host_param.data_line_active_negation = (value >> 13) & 0x01;
	hostdata->host_param.data_dma_burst_enable = (value >> 14) & 0x01;
	hostdata->host_param.command_dma_burst_enable = (value >> 15);

	value = isp1020_read_nvram_word(host, 5);
	hostdata->host_param.tag_aging = value & 0xff;

	value = isp1020_read_nvram_word(host, 6);
	hostdata->host_param.selection_timeout = value & 0xffff;

	value = isp1020_read_nvram_word(host, 7);
	hostdata->host_param.max_queue_depth = value & 0xffff;

#if DEBUG_ISP1020_SETUP
	printk("qlogicisp : fifo threshold=%d\n",
	       hostdata->host_param.fifo_threshold);
	printk("qlogicisp : initiator scsi id=%d\n",
	       hostdata->host_param.initiator_scsi_id);
	printk("qlogicisp : bus reset delay=%d\n",
	       hostdata->host_param.bus_reset_delay);
	printk("qlogicisp : retry count=%d\n",
	       hostdata->host_param.retry_count);
	printk("qlogicisp : retry delay=%d\n",
	       hostdata->host_param.retry_delay);
	printk("qlogicisp : async data setup time=%d\n",
	       hostdata->host_param.async_data_setup_time);
	printk("qlogicisp : req/ack active negation=%d\n",
	       hostdata->host_param.req_ack_active_negation);
	printk("qlogicisp : data line active negation=%d\n",
	       hostdata->host_param.data_line_active_negation);
	printk("qlogicisp : data DMA burst enable=%d\n",
	       hostdata->host_param.data_dma_burst_enable);
	printk("qlogicisp : command DMA burst enable=%d\n",
	       hostdata->host_param.command_dma_burst_enable);
	printk("qlogicisp : tag age limit=%d\n",
	       hostdata->host_param.tag_aging);
	printk("qlogicisp : selection timeout limit=%d\n",
	       hostdata->host_param.selection_timeout);
	printk("qlogicisp : max queue depth=%d\n",
	       hostdata->host_param.max_queue_depth);
#endif /* DEBUG_ISP1020_SETUP */

	for (i = 0; i < MAX_TARGETS; i++) {

		value = isp1020_read_nvram_word(host, 14 + i * 3);
		hostdata->dev_param[i].device_flags = value & 0xff;
		hostdata->dev_param[i].execution_throttle = value >> 8;

		value = isp1020_read_nvram_word(host, 15 + i * 3);
		hostdata->dev_param[i].synchronous_period = value & 0xff;
		hostdata->dev_param[i].synchronous_offset = (value >> 8) & 0x0f;
		hostdata->dev_param[i].device_enable = (value >> 12) & 0x01;

#if DEBUG_ISP1020_SETUP
		printk("qlogicisp : target 0x%02x\n", i);
		printk("qlogicisp :     device flags=0x%02x\n",
		       hostdata->dev_param[i].device_flags);
		printk("qlogicisp :     execution throttle=%d\n",
		       hostdata->dev_param[i].execution_throttle);
		printk("qlogicisp :     synchronous period=%d\n",
		       hostdata->dev_param[i].synchronous_period);
		printk("qlogicisp :     synchronous offset=%d\n",
		       hostdata->dev_param[i].synchronous_offset);
		printk("qlogicisp :     device enable=%d\n",
		       hostdata->dev_param[i].device_enable);
#endif /* DEBUG_ISP1020_SETUP */
	}

	LEAVE("isp1020_get_defaults");

	return 0;
}


#define ISP1020_NVRAM_LEN	0x40
#define ISP1020_NVRAM_SIG1	0x5349
#define ISP1020_NVRAM_SIG2	0x2050

static int isp1020_verify_nvram(struct Scsi_Host *host)
{
	int	i;
	u_short value;
	u_char checksum = 0;

	for (i = 0; i < ISP1020_NVRAM_LEN; i++) {
		value = isp1020_read_nvram_word(host, i);

		switch (i) {
		      case 0:
			if (value != ISP1020_NVRAM_SIG1) return 0;
			break;
		      case 1:
			if (value != ISP1020_NVRAM_SIG2) return 0;
			break;
		      case 2:
			if ((value & 0xff) != 0x02) return 0;
			break;
		}
		checksum += value & 0xff;
		checksum += value >> 8;
	}

	return (checksum == 0);
}

#define NVRAM_DELAY() udelay(2) /* 2 microsecond delay */


u_short isp1020_read_nvram_word(struct Scsi_Host *host, u_short byte)
{
	int i;
	u_short value, output, input;

	byte &= 0x3f; byte |= 0x0180;

	for (i = 8; i >= 0; i--) {
		output = ((byte >> i) & 0x1) ? 0x4 : 0x0;
		isp_outw(output | 0x2, host, PCI_NVRAM); NVRAM_DELAY();
		isp_outw(output | 0x3, host, PCI_NVRAM); NVRAM_DELAY();
		isp_outw(output | 0x2, host, PCI_NVRAM); NVRAM_DELAY();
	}

	for (i = 0xf, value = 0; i >= 0; i--) {
		value <<= 1;
		isp_outw(0x3, host, PCI_NVRAM); NVRAM_DELAY();
		input = isp_inw(host, PCI_NVRAM); NVRAM_DELAY();
		isp_outw(0x2, host, PCI_NVRAM); NVRAM_DELAY();
		if (input & 0x8) value |= 1;
	}

	isp_outw(0x0, host, PCI_NVRAM); NVRAM_DELAY();

	return value;
}

#endif /* USE_NVRAM_DEFAULTS */


static int isp1020_set_defaults(struct Scsi_Host *host)
{
	struct isp1020_hostdata *hostdata =
		(struct isp1020_hostdata *) host->hostdata;
	int i;

	ENTER("isp1020_set_defaults");

	hostdata->host_param.fifo_threshold = 2;
	hostdata->host_param.host_adapter_enable = 1;
	hostdata->host_param.initiator_scsi_id = 7;
	hostdata->host_param.bus_reset_delay = 3;
	hostdata->host_param.retry_count = 0;
	hostdata->host_param.retry_delay = 1;
	hostdata->host_param.async_data_setup_time = 6;
	hostdata->host_param.req_ack_active_negation = 1;
	hostdata->host_param.data_line_active_negation = 1;
	hostdata->host_param.data_dma_burst_enable = 1;
	hostdata->host_param.command_dma_burst_enable = 1;
	hostdata->host_param.tag_aging = 8;
	hostdata->host_param.selection_timeout = 250;
	hostdata->host_param.max_queue_depth = 256;

	for (i = 0; i < MAX_TARGETS; i++) {
		hostdata->dev_param[i].device_flags = 0xfd;
		hostdata->dev_param[i].execution_throttle = 16;
		hostdata->dev_param[i].synchronous_period = 25;
		hostdata->dev_param[i].synchronous_offset = 12;
		hostdata->dev_param[i].device_enable = 1;
	}

	LEAVE("isp1020_set_defaults");

	return 0;
}


static int isp1020_load_parameters(struct Scsi_Host *host)
{
	int i, k;
#ifdef CONFIG_QL_ISP_A64
	u_long queue_addr;
	u_short param[8];
#else
	u_int queue_addr;
	u_short param[6];
#endif
	u_short isp_cfg1, hwrev;
	struct isp1020_hostdata *hostdata =
		(struct isp1020_hostdata *) host->hostdata;

	ENTER("isp1020_load_parameters");

	hwrev = isp_inw(host, ISP_CFG0) & ISP_CFG0_HWMSK;
	isp_cfg1 = ISP_CFG1_F64 | ISP_CFG1_BENAB;
	if (hwrev == ISP_CFG0_1040A) {
		/* Busted fifo, says mjacob. */
		isp_cfg1 &= ISP_CFG1_BENAB;
	}

	isp_outw(isp_inw(host, ISP_CFG1) | isp_cfg1, host, ISP_CFG1);
	isp_outw(isp_inw(host, CDMA_CONF) | DMA_CONF_BENAB, host, CDMA_CONF);
	isp_outw(isp_inw(host, DDMA_CONF) | DMA_CONF_BENAB, host, DDMA_CONF);

	param[0] = MBOX_SET_INIT_SCSI_ID;
	param[1] = hostdata->host_param.initiator_scsi_id;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set initiator id failure\n");
		return 1;
	}

	param[0] = MBOX_SET_RETRY_COUNT;
	param[1] = hostdata->host_param.retry_count;
	param[2] = hostdata->host_param.retry_delay;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set retry count failure\n");
		return 1;
	}

	param[0] = MBOX_SET_ASYNC_DATA_SETUP_TIME;
	param[1] = hostdata->host_param.async_data_setup_time;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : async data setup time failure\n");
		return 1;
	}

	param[0] = MBOX_SET_ACTIVE_NEG_STATE;
	param[1] = (hostdata->host_param.req_ack_active_negation << 4)
		| (hostdata->host_param.data_line_active_negation << 5);

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set active negation state failure\n");
		return 1;
	}

	param[0] = MBOX_SET_PCI_CONTROL_PARAMS;
	param[1] = hostdata->host_param.data_dma_burst_enable << 1;
	param[2] = hostdata->host_param.command_dma_burst_enable << 1;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set pci control parameter failure\n");
		return 1;
	}

	param[0] = MBOX_SET_TAG_AGE_LIMIT;
	param[1] = hostdata->host_param.tag_aging;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set tag age limit failure\n");
		return 1;
	}

	param[0] = MBOX_SET_SELECT_TIMEOUT;
	param[1] = hostdata->host_param.selection_timeout;

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set selection timeout failure\n");
		return 1;
	}

	for (i = 0; i < MAX_TARGETS; i++) {

		if (!hostdata->dev_param[i].device_enable)
			continue;

		param[0] = MBOX_SET_TARGET_PARAMS;
		param[1] = i << 8;
		param[2] = hostdata->dev_param[i].device_flags << 8;
		param[3] = (hostdata->dev_param[i].synchronous_offset << 8)
			| hostdata->dev_param[i].synchronous_period;

		isp1020_mbox_command(host, param);

		if (param[0] != MBOX_COMMAND_COMPLETE) {
			printk("qlogicisp : set target parameter failure\n");
			return 1;
		}

		for (k = 0; k < MAX_LUNS; k++) {

			param[0] = MBOX_SET_DEV_QUEUE_PARAMS;
			param[1] = (i << 8) | k;
			param[2] = hostdata->host_param.max_queue_depth;
			param[3] = hostdata->dev_param[i].execution_throttle;

			isp1020_mbox_command(host, param);

			if (param[0] != MBOX_COMMAND_COMPLETE) {
				printk("qlogicisp : set device queue "
				       "parameter failure\n");
				return 1;
			}
		}
	}

	queue_addr = hostdata->res_dma;
#ifdef CONFIG_QL_ISP_A64
	param[0] = MBOX_CMD_INIT_RESPONSE_QUEUE_64;
#else
	param[0] = MBOX_INIT_RES_QUEUE;
#endif
	param[1] = RES_QUEUE_LEN + 1;
	param[2] = (u_short) (queue_addr >> 16);
	param[3] = (u_short) (queue_addr & 0xffff);
	param[4] = 0;
	param[5] = 0;
#ifdef CONFIG_QL_ISP_A64
	param[6] = (u_short) (queue_addr >> 48);
	param[7] = (u_short) (queue_addr >> 32);
#endif

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set response queue failure\n");
		return 1;
	}

	queue_addr = hostdata->req_dma;
#ifdef CONFIG_QL_ISP_A64
	param[0] = MBOX_CMD_INIT_REQUEST_QUEUE_64;
#else
	param[0] = MBOX_INIT_REQ_QUEUE;
#endif
	param[1] = QLOGICISP_REQ_QUEUE_LEN + 1;
	param[2] = (u_short) (queue_addr >> 16);
	param[3] = (u_short) (queue_addr & 0xffff);
	param[4] = 0;

#ifdef CONFIG_QL_ISP_A64
	param[5] = 0;
	param[6] = (u_short) (queue_addr >> 48);
	param[7] = (u_short) (queue_addr >> 32);
#endif

	isp1020_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicisp : set request queue failure\n");
		return 1;
	}

	LEAVE("isp1020_load_parameters");

	return 0;
}


/*
 * currently, this is only called during initialization or abort/reset,
 * at which times interrupts are disabled, so polling is OK, I guess...
 */
static int isp1020_mbox_command(struct Scsi_Host *host, u_short param[])
{
	int loop_count;

	if (mbox_param[param[0]] == 0)
		return 1;

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && isp_inw(host, HOST_HCCR) & 0x0080) {
		barrier();
		cpu_relax();
	}
	if (!loop_count)
		printk("qlogicisp: mbox_command loop timeout #1\n");

	switch(mbox_param[param[0]] >> 4) {
	      case 8: isp_outw(param[7], host, MBOX7);
	      case 7: isp_outw(param[6], host, MBOX6);
	      case 6: isp_outw(param[5], host, MBOX5);
	      case 5: isp_outw(param[4], host, MBOX4);
	      case 4: isp_outw(param[3], host, MBOX3);
	      case 3: isp_outw(param[2], host, MBOX2);
	      case 2: isp_outw(param[1], host, MBOX1);
	      case 1: isp_outw(param[0], host, MBOX0);
	}

	isp_outw(0x0, host, PCI_SEMAPHORE);
	isp_outw(HCCR_CLEAR_RISC_INTR, host, HOST_HCCR);
	isp_outw(HCCR_SET_HOST_INTR, host, HOST_HCCR);

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && !(isp_inw(host, PCI_INTF_STS) & 0x04)) {
		barrier();
		cpu_relax();
	}
	if (!loop_count)
		printk("qlogicisp: mbox_command loop timeout #2\n");

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && isp_inw(host, MBOX0) == 0x04) {
		barrier();
		cpu_relax();
	}
	if (!loop_count)
		printk("qlogicisp: mbox_command loop timeout #3\n");

	switch(mbox_param[param[0]] & 0xf) {
	      case 8: param[7] = isp_inw(host, MBOX7);
	      case 7: param[6] = isp_inw(host, MBOX6);
	      case 6: param[5] = isp_inw(host, MBOX5);
	      case 5: param[4] = isp_inw(host, MBOX4);
	      case 4: param[3] = isp_inw(host, MBOX3);
	      case 3: param[2] = isp_inw(host, MBOX2);
	      case 2: param[1] = isp_inw(host, MBOX1);
	      case 1: param[0] = isp_inw(host, MBOX0);
	}

	isp_outw(0x0, host, PCI_SEMAPHORE);
	isp_outw(HCCR_CLEAR_RISC_INTR, host, HOST_HCCR);

	return 0;
}


#if DEBUG_ISP1020_INTR

void isp1020_print_status_entry(struct Status_Entry *status)
{
	int i;

	printk("qlogicisp : entry count = 0x%02x, type = 0x%02x, flags = 0x%02x\n",
	       status->hdr.entry_cnt, status->hdr.entry_type, status->hdr.flags);
	printk("qlogicisp : scsi status = 0x%04x, completion status = 0x%04x\n",
	       le16_to_cpu(status->scsi_status), le16_to_cpu(status->completion_status));
	printk("qlogicisp : state flags = 0x%04x, status flags = 0x%04x\n",
	       le16_to_cpu(status->state_flags), le16_to_cpu(status->status_flags));
	printk("qlogicisp : time = 0x%04x, request sense length = 0x%04x\n",
	       le16_to_cpu(status->time), le16_to_cpu(status->req_sense_len));
	printk("qlogicisp : residual transfer length = 0x%08x\n",
	       le32_to_cpu(status->residual));

	for (i = 0; i < le16_to_cpu(status->req_sense_len); i++)
		printk("qlogicisp : sense data = 0x%02x\n", status->req_sense_data[i]);
}

#endif /* DEBUG_ISP1020_INTR */


#if DEBUG_ISP1020

void isp1020_print_scsi_cmd(Scsi_Cmnd *cmd)
{
	int i;

	printk("qlogicisp : target = 0x%02x, lun = 0x%02x, cmd_len = 0x%02x\n",
	       cmd->target, cmd->lun, cmd->cmd_len);
	printk("qlogicisp : command = ");
	for (i = 0; i < cmd->cmd_len; i++)
		printk("0x%02x ", cmd->cmnd[i]);
	printk("\n");
}

#endif /* DEBUG_ISP1020 */

MODULE_LICENSE("GPL");

static Scsi_Host_Template driver_template = {
	.detect			= isp1020_detect,
	.release		= isp1020_release,
	.info			= isp1020_info,	
	.queuecommand		= isp1020_queuecommand,
	.bios_param		= isp1020_biosparam,
	.can_queue		= QLOGICISP_REQ_QUEUE_LEN,
	.this_id		= -1,
	.sg_tablesize		= QLOGICISP_MAX_SG(QLOGICISP_REQ_QUEUE_LEN),
	.cmd_per_lun		= 1,
	.use_clustering		= DISABLE_CLUSTERING,
};
#include "scsi_module.c"
