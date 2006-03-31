/*
 * QLogic ISP2x00 SCSI-FCP
 * Written by Erik H. Moe, ehm@cris.com
 * Copyright 1995, Erik H. Moe
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

/* Renamed and updated to 1.3.x by Michael Griffith <grif@cs.ucr.edu> */

/* This is a version of the isp1020 driver which was modified by
 * Chris Loveland <cwl@iol.unh.edu> to support the isp2100 and isp2200
 *
 * Big endian support and dynamic DMA mapping added
 * by Jakub Jelinek <jakub@redhat.com>.
 *
 * Conversion to final pci64 DMA interfaces
 * by David S. Miller <davem@redhat.com>.
 */

/*
 * $Date: 1995/09/22 02:23:15 $
 * $Revision: 0.5 $
 *
 * $Log: isp1020.c,v $
 * Revision 0.5  1995/09/22  02:23:15  root
 * do auto request sense
 *
 * Revision 0.4  1995/08/07  04:44:33  root
 * supply firmware with driver.
 * numerous bug fixes/general cleanup of code.
 *
 * Revision 0.3  1995/07/16  16:15:39  root
 * added reset/abort code.
 *
 * Revision 0.2  1995/06/29  03:14:19  root
 * fixed biosparam.
 * added queue protocol.
 *
 * Revision 0.1  1995/06/25  01:55:45  root
 * Initial release.
 *
 */

#include <linux/blkdev.h>
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
#include <linux/dma-mapping.h>
#include <linux/jiffies.h>
#include <asm/io.h>
#include <asm/irq.h>
#include "scsi.h"
#include <scsi/scsi_host.h>

#define pci64_dma_hi32(a) ((u32) (0xffffffff & (((u64)(a))>>32)))
#define pci64_dma_lo32(a) ((u32) (0xffffffff & (((u64)(a)))))
#define pci64_dma_build(hi,lo) \
	((dma_addr_t)(((u64)(lo))|(((u64)(hi))<<32)))

/*
 * With the qlogic interface, every queue slot can hold a SCSI
 * command with up to 2 scatter/gather entries.  If we need more
 * than 2 entries, continuation entries can be used that hold
 * another 5 entries each.  Unlike for other drivers, this means
 * that the maximum number of scatter/gather entries we can
 * support at any given time is a function of the number of queue
 * slots available.  That is, host->can_queue and host->sg_tablesize
 * are dynamic and _not_ independent.  This all works fine because
 * requests are queued serially and the scatter/gather limit is
 * determined for each queue request anew.
 */

#define DATASEGS_PER_COMMAND 2
#define DATASEGS_PER_CONT 5

#define QLOGICFC_REQ_QUEUE_LEN 255     /* must be power of two - 1 */
#define QLOGICFC_MAX_SG(ql)	(DATASEGS_PER_COMMAND + (((ql) > 0) ? DATASEGS_PER_CONT*((ql) - 1) : 0))
#define QLOGICFC_CMD_PER_LUN    8

/* Configuration section **************************************************** */

/* Set the following macro to 1 to reload the ISP2x00's firmware.  This is
   version 1.17.30 of the isp2100's firmware and version 2.00.40 of the 
   isp2200's firmware. 
*/

#define USE_NVRAM_DEFAULTS      1

#define ISP2x00_PORTDB          1

/* Set the following to 1 to include fabric support, fabric support is 
 * currently not as well tested as the other aspects of the driver */

#define ISP2x00_FABRIC          1

/*  Macros used for debugging */
#define DEBUG_ISP2x00		0
#define DEBUG_ISP2x00_INT	0
#define DEBUG_ISP2x00_INTR	0
#define DEBUG_ISP2x00_SETUP	0
#define DEBUG_ISP2x00_FABRIC    0
#define TRACE_ISP 		0 


#define DEFAULT_LOOP_COUNT	1000000000

#define ISP_TIMEOUT (2*HZ)
/* End Configuration section ************************************************ */

#include <linux/module.h>

#if TRACE_ISP

#define TRACE_BUF_LEN	(32*1024)

struct {
	u_long next;
	struct {
		u_long time;
		u_int index;
		u_int addr;
		u_char *name;
	} buf[TRACE_BUF_LEN];
} trace;

#define TRACE(w, i, a)						\
{								\
	unsigned long flags;					\
								\
	save_flags(flags);					\
	cli();							\
	trace.buf[trace.next].name  = (w);			\
	trace.buf[trace.next].time  = jiffies;			\
	trace.buf[trace.next].index = (i);			\
	trace.buf[trace.next].addr  = (long) (a);		\
	trace.next = (trace.next + 1) & (TRACE_BUF_LEN - 1);	\
	restore_flags(flags);					\
}

#else
#define TRACE(w, i, a)
#endif

#if DEBUG_ISP2x00_FABRIC
#define DEBUG_FABRIC(x)	x
#else
#define DEBUG_FABRIC(x)
#endif				/* DEBUG_ISP2x00_FABRIC */


#if DEBUG_ISP2x00
#define ENTER(x)	printk("isp2x00 : entering %s()\n", x);
#define LEAVE(x)	printk("isp2x00 : leaving %s()\n", x);
#define DEBUG(x)	x
#else
#define ENTER(x)
#define LEAVE(x)
#define DEBUG(x)
#endif				/* DEBUG_ISP2x00 */

#if DEBUG_ISP2x00_INTR
#define ENTER_INTR(x)	printk("isp2x00 : entering %s()\n", x);
#define LEAVE_INTR(x)	printk("isp2x00 : leaving %s()\n", x);
#define DEBUG_INTR(x)	x
#else
#define ENTER_INTR(x)
#define LEAVE_INTR(x)
#define DEBUG_INTR(x)
#endif				/* DEBUG ISP2x00_INTR */


#define ISP2100_REV_ID1	       1
#define ISP2100_REV_ID3        3
#define ISP2200_REV_ID5        5

/* host configuration and control registers */
#define HOST_HCCR	0xc0	/* host command and control */

/* pci bus interface registers */
#define FLASH_BIOS_ADDR	0x00
#define FLASH_BIOS_DATA	0x02
#define ISP_CTRL_STATUS	0x06	/* configuration register #1 */
#define PCI_INTER_CTL	0x08	/* pci interrupt control */
#define PCI_INTER_STS	0x0a	/* pci interrupt status */
#define PCI_SEMAPHORE	0x0c	/* pci semaphore */
#define PCI_NVRAM	0x0e	/* pci nvram interface */

/* mailbox registers */
#define MBOX0		0x10	/* mailbox 0 */
#define MBOX1		0x12	/* mailbox 1 */
#define MBOX2		0x14	/* mailbox 2 */
#define MBOX3		0x16	/* mailbox 3 */
#define MBOX4		0x18	/* mailbox 4 */
#define MBOX5		0x1a	/* mailbox 5 */
#define MBOX6		0x1c	/* mailbox 6 */
#define MBOX7		0x1e	/* mailbox 7 */

/* mailbox command complete status codes */
#define MBOX_COMMAND_COMPLETE		0x4000
#define INVALID_COMMAND			0x4001
#define HOST_INTERFACE_ERROR		0x4002
#define TEST_FAILED			0x4003
#define COMMAND_ERROR			0x4005
#define COMMAND_PARAM_ERROR		0x4006
#define PORT_ID_USED                    0x4007
#define LOOP_ID_USED                    0x4008
#define ALL_IDS_USED                    0x4009

/* async event status codes */
#define RESET_DETECTED  		0x8001
#define SYSTEM_ERROR			0x8002
#define REQUEST_TRANSFER_ERROR		0x8003
#define RESPONSE_TRANSFER_ERROR		0x8004
#define REQUEST_QUEUE_WAKEUP		0x8005
#define LIP_OCCURRED                     0x8010
#define LOOP_UP                         0x8011
#define LOOP_DOWN                       0x8012
#define LIP_RECEIVED                    0x8013
#define PORT_DB_CHANGED                 0x8014
#define CHANGE_NOTIFICATION             0x8015
#define SCSI_COMMAND_COMPLETE           0x8020
#define POINT_TO_POINT_UP               0x8030
#define CONNECTION_MODE                 0x8036

struct Entry_header {
	u_char entry_type;
	u_char entry_cnt;
	u_char sys_def_1;
	u_char flags;
};

/* entry header type commands */
#define ENTRY_COMMAND		0x19
#define ENTRY_CONTINUATION	0x0a

#define ENTRY_STATUS		0x03
#define ENTRY_MARKER		0x04


/* entry header flag definitions */
#define EFLAG_BUSY		2
#define EFLAG_BAD_HEADER	4
#define EFLAG_BAD_PAYLOAD	8

struct dataseg {
	u_int d_base;
	u_int d_base_hi;
	u_int d_count;
};

struct Command_Entry {
	struct Entry_header hdr;
	u_int handle;
	u_char target_lun;
	u_char target_id;
	u_short expanded_lun;
	u_short control_flags;
	u_short rsvd2;
	u_short time_out;
	u_short segment_cnt;
	u_char cdb[16];
	u_int total_byte_cnt;
	struct dataseg dataseg[DATASEGS_PER_COMMAND];
};

/* command entry control flag definitions */
#define CFLAG_NODISC		0x01
#define CFLAG_HEAD_TAG		0x02
#define CFLAG_ORDERED_TAG	0x04
#define CFLAG_SIMPLE_TAG	0x08
#define CFLAG_TAR_RTN		0x10
#define CFLAG_READ		0x20
#define CFLAG_WRITE		0x40

struct Continuation_Entry {
	struct Entry_header hdr;
	struct dataseg dataseg[DATASEGS_PER_CONT];
};

struct Marker_Entry {
	struct Entry_header hdr;
	u_int reserved;
	u_char target_lun;
	u_char target_id;
	u_char modifier;
	u_char expanded_lun;
	u_char rsvds[52];
};

/* marker entry modifier definitions */
#define SYNC_DEVICE	0
#define SYNC_TARGET	1
#define SYNC_ALL	2

struct Status_Entry {
	struct Entry_header hdr;
	u_int handle;
	u_short scsi_status;
	u_short completion_status;
	u_short state_flags;
	u_short status_flags;
	u_short res_info_len;
	u_short req_sense_len;
	u_int residual;
	u_char res_info[8];
	u_char req_sense_data[32];
};

/* status entry completion status definitions */
#define CS_COMPLETE			0x0000
#define CS_DMA_ERROR			0x0002
#define CS_RESET_OCCURRED		0x0004
#define CS_ABORTED			0x0005
#define CS_TIMEOUT			0x0006
#define CS_DATA_OVERRUN			0x0007
#define CS_DATA_UNDERRUN		0x0015
#define CS_QUEUE_FULL			0x001c
#define CS_PORT_UNAVAILABLE             0x0028
#define CS_PORT_LOGGED_OUT              0x0029
#define CS_PORT_CONFIG_CHANGED		0x002a

/* status entry state flag definitions */
#define SF_SENT_CDB			0x0400
#define SF_TRANSFERRED_DATA		0x0800
#define SF_GOT_STATUS			0x1000

/* status entry status flag definitions */
#define STF_BUS_RESET			0x0008
#define STF_DEVICE_RESET		0x0010
#define STF_ABORTED			0x0020
#define STF_TIMEOUT			0x0040

/* interrupt control commands */
#define ISP_EN_INT			0x8000
#define ISP_EN_RISC			0x0008

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
#define MBOX_LOAD_RISC_RAM              0x0009
#define MBOX_DUMP_RISC_RAM              0x000a
#define MBOX_CHECK_FIRMWARE		0x000e
#define MBOX_INIT_REQ_QUEUE		0x0010
#define MBOX_INIT_RES_QUEUE		0x0011
#define MBOX_EXECUTE_IOCB		0x0012
#define MBOX_WAKE_UP			0x0013
#define MBOX_STOP_FIRMWARE		0x0014
#define MBOX_ABORT_IOCB			0x0015
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
#define MBOX_GET_RETRY_COUNT		0x0022
#define MBOX_GET_TARGET_PARAMS		0x0028
#define MBOX_GET_DEV_QUEUE_PARAMS	0x0029
#define MBOX_SET_RETRY_COUNT		0x0032
#define MBOX_SET_TARGET_PARAMS		0x0038
#define MBOX_SET_DEV_QUEUE_PARAMS	0x0039
#define MBOX_EXECUTE_IOCB64             0x0054
#define MBOX_INIT_FIRMWARE              0x0060
#define MBOX_GET_INIT_CB                0x0061
#define MBOX_INIT_LIP			0x0062
#define MBOX_GET_POS_MAP                0x0063
#define MBOX_GET_PORT_DB                0x0064
#define MBOX_CLEAR_ACA                  0x0065
#define MBOX_TARGET_RESET               0x0066
#define MBOX_CLEAR_TASK_SET             0x0067
#define MBOX_ABORT_TASK_SET             0x0068
#define MBOX_GET_FIRMWARE_STATE         0x0069
#define MBOX_GET_PORT_NAME              0x006a
#define MBOX_SEND_SNS                   0x006e
#define MBOX_PORT_LOGIN                 0x006f
#define MBOX_SEND_CHANGE_REQUEST        0x0070
#define MBOX_PORT_LOGOUT                0x0071

/*
 *	Firmware if needed (note this is a hack, it belongs in a separate
 *	module.
 */
 
#ifdef CONFIG_SCSI_QLOGIC_FC_FIRMWARE
#include "qlogicfc_asm.c"
#else
static unsigned short risc_code_addr01 = 0x1000 ;
#endif

/* Each element in mbox_param is an 8 bit bitmap where each bit indicates
   if that mbox should be copied as input.  For example 0x2 would mean
   only copy mbox1. */

static const u_char mbox_param[] =
{
	0x01,			/* MBOX_NO_OP */
	0x1f,			/* MBOX_LOAD_RAM */
	0x03,			/* MBOX_EXEC_FIRMWARE */
	0x1f,			/* MBOX_DUMP_RAM */
	0x07,			/* MBOX_WRITE_RAM_WORD */
	0x03,			/* MBOX_READ_RAM_WORD */
	0xff,			/* MBOX_MAILBOX_REG_TEST */
	0x03,			/* MBOX_VERIFY_CHECKSUM */
	0x01,			/* MBOX_ABOUT_FIRMWARE */
	0xff,			/* MBOX_LOAD_RISC_RAM */
	0xff,			/* MBOX_DUMP_RISC_RAM */
	0x00,			/* 0x000b */
	0x00,			/* 0x000c */
	0x00,			/* 0x000d */
	0x01,			/* MBOX_CHECK_FIRMWARE */
	0x00,			/* 0x000f */
	0x1f,			/* MBOX_INIT_REQ_QUEUE */
	0x2f,			/* MBOX_INIT_RES_QUEUE */
	0x0f,			/* MBOX_EXECUTE_IOCB */
	0x03,			/* MBOX_WAKE_UP */
	0x01,			/* MBOX_STOP_FIRMWARE */
	0x0f,			/* MBOX_ABORT_IOCB */
	0x03,			/* MBOX_ABORT_DEVICE */
	0x07,			/* MBOX_ABORT_TARGET */
	0x03,			/* MBOX_BUS_RESET */
	0x03,			/* MBOX_STOP_QUEUE */
	0x03,			/* MBOX_START_QUEUE */
	0x03,			/* MBOX_SINGLE_STEP_QUEUE */
	0x03,			/* MBOX_ABORT_QUEUE */
	0x03,			/* MBOX_GET_DEV_QUEUE_STATUS */
	0x00,			/* 0x001e */
	0x01,			/* MBOX_GET_FIRMWARE_STATUS */
	0x01,			/* MBOX_GET_INIT_SCSI_ID */
	0x00,			/* 0x0021 */
	0x01,			/* MBOX_GET_RETRY_COUNT */
	0x00,			/* 0x0023 */
	0x00,			/* 0x0024 */
	0x00,			/* 0x0025 */
	0x00,			/* 0x0026 */
	0x00,			/* 0x0027 */
	0x03,			/* MBOX_GET_TARGET_PARAMS */
	0x03,			/* MBOX_GET_DEV_QUEUE_PARAMS */
	0x00,			/* 0x002a */
	0x00,			/* 0x002b */
	0x00,			/* 0x002c */
	0x00,			/* 0x002d */
	0x00,			/* 0x002e */
	0x00,			/* 0x002f */
	0x00,			/* 0x0030 */
	0x00,			/* 0x0031 */
	0x07,			/* MBOX_SET_RETRY_COUNT */
	0x00,			/* 0x0033 */
	0x00,			/* 0x0034 */
	0x00,			/* 0x0035 */
	0x00,			/* 0x0036 */
	0x00,			/* 0x0037 */
	0x0f,			/* MBOX_SET_TARGET_PARAMS */
	0x0f,			/* MBOX_SET_DEV_QUEUE_PARAMS */
	0x00,			/* 0x003a */
	0x00,			/* 0x003b */
	0x00,			/* 0x003c */
	0x00,			/* 0x003d */
	0x00,			/* 0x003e */
	0x00,			/* 0x003f */
	0x00,			/* 0x0040 */
	0x00,			/* 0x0041 */
	0x00,			/* 0x0042 */
	0x00,			/* 0x0043 */
	0x00,			/* 0x0044 */
	0x00,			/* 0x0045 */
	0x00,			/* 0x0046 */
	0x00,			/* 0x0047 */
	0x00,			/* 0x0048 */
	0x00,			/* 0x0049 */
	0x00,			/* 0x004a */
	0x00,			/* 0x004b */
	0x00,			/* 0x004c */
	0x00,			/* 0x004d */
	0x00,			/* 0x004e */
	0x00,			/* 0x004f */
	0x00,			/* 0x0050 */
	0x00,			/* 0x0051 */
	0x00,			/* 0x0052 */
	0x00,			/* 0x0053 */
	0xcf,			/* MBOX_EXECUTE_IOCB64 */
	0x00,			/* 0x0055 */
	0x00,			/* 0x0056 */
	0x00,			/* 0x0057 */
	0x00,			/* 0x0058 */
	0x00,			/* 0x0059 */
	0x00,			/* 0x005a */
	0x00,			/* 0x005b */
	0x00,			/* 0x005c */
	0x00,			/* 0x005d */
	0x00,			/* 0x005e */
	0x00,			/* 0x005f */
	0xff,			/* MBOX_INIT_FIRMWARE */
	0xcd,			/* MBOX_GET_INIT_CB */
	0x01,			/* MBOX_INIT_LIP */
	0xcd,			/* MBOX_GET_POS_MAP */
	0xcf,			/* MBOX_GET_PORT_DB */
	0x03,			/* MBOX_CLEAR_ACA */
	0x03,			/* MBOX_TARGET_RESET */
	0x03,			/* MBOX_CLEAR_TASK_SET */
	0x03,			/* MBOX_ABORT_TASK_SET */
	0x01,			/* MBOX_GET_FIRMWARE_STATE */
	0x03,			/* MBOX_GET_PORT_NAME */
	0x00,			/* 0x006b */
	0x00,			/* 0x006c */
	0x00,			/* 0x006d */
	0xcf,			/* MBOX_SEND_SNS */
	0x0f,			/* MBOX_PORT_LOGIN */
	0x03,			/* MBOX_SEND_CHANGE_REQUEST */
	0x03,			/* MBOX_PORT_LOGOUT */
};

#define MAX_MBOX_COMMAND	(sizeof(mbox_param)/sizeof(u_short))


struct id_name_map {
	u64 wwn;
	u_char loop_id;
};

struct sns_cb {
	u_short len;
	u_short res1;
	u_int response_low;
	u_int response_high;
	u_short sub_len;
	u_short res2;
	u_char data[44];
};

/* address of instance of this struct is passed to adapter to initialize things
 */
struct init_cb {
	u_char version;
	u_char reseverd1[1];
	u_short firm_opts;
	u_short max_frame_len;
	u_short max_iocb;
	u_short exec_throttle;
	u_char retry_cnt;
	u_char retry_delay;
	u_short node_name[4];
	u_short hard_addr;
	u_char reserved2[10];
	u_short req_queue_out;
	u_short res_queue_in;
	u_short req_queue_len;
	u_short res_queue_len;
	u_int req_queue_addr_lo;
	u_int req_queue_addr_high;
	u_int res_queue_addr_lo;
	u_int res_queue_addr_high;
        /* the rest of this structure only applies to the isp2200 */
        u_short lun_enables;
        u_char cmd_resource_cnt;
        u_char notify_resource_cnt;
        u_short timeout;
        u_short reserved3;
        u_short add_firm_opts;
        u_char res_accum_timer;
        u_char irq_delay_timer;
        u_short special_options;
        u_short reserved4[13];
};

/*
 * The result queue can be quite a bit smaller since continuation entries
 * do not show up there:
 */
#define RES_QUEUE_LEN		((QLOGICFC_REQ_QUEUE_LEN + 1) / 8 - 1)
#define QUEUE_ENTRY_LEN		64

#if ISP2x00_FABRIC
#define QLOGICFC_MAX_ID    0xff
#else
#define QLOGICFC_MAX_ID    0x7d
#endif

#define QLOGICFC_MAX_LUN	128
#define QLOGICFC_MAX_LOOP_ID	0x7d

/* the following connection options only apply to the 2200.  i have only
 * had success with LOOP_ONLY and P2P_ONLY.
 */

#define LOOP_ONLY              0
#define P2P_ONLY               1
#define LOOP_PREFERED          2
#define P2P_PREFERED           3

#define CONNECTION_PREFERENCE  LOOP_ONLY

/* adapter_state values */
#define AS_FIRMWARE_DEAD      -1
#define AS_LOOP_DOWN           0
#define AS_LOOP_GOOD           1
#define AS_REDO_FABRIC_PORTDB  2
#define AS_REDO_LOOP_PORTDB    4

#define RES_SIZE	((RES_QUEUE_LEN + 1)*QUEUE_ENTRY_LEN)
#define REQ_SIZE	((QLOGICFC_REQ_QUEUE_LEN + 1)*QUEUE_ENTRY_LEN)

struct isp2x00_hostdata {
	u_char revision;
	struct pci_dev *pci_dev;
	/* result and request queues (shared with isp2x00): */
	u_int req_in_ptr;	/* index of next request slot */
	u_int res_out_ptr;	/* index of next result slot */

	/* this is here so the queues are nicely aligned */
	long send_marker;	/* do we need to send a marker? */

	char * res;
	char * req;
	struct init_cb control_block;
	int adapter_state;
	unsigned long int tag_ages[QLOGICFC_MAX_ID + 1];
	Scsi_Cmnd *handle_ptrs[QLOGICFC_REQ_QUEUE_LEN + 1];
	unsigned long handle_serials[QLOGICFC_REQ_QUEUE_LEN + 1];
	struct id_name_map port_db[QLOGICFC_MAX_ID + 1];
	u_char mbox_done;
	u64 wwn;
	u_int port_id;
	u_char queued;
	u_char host_id;
        struct timer_list explore_timer;
	struct id_name_map tempmap[QLOGICFC_MAX_ID + 1];
};


/* queue length's _must_ be power of two: */
#define QUEUE_DEPTH(in, out, ql)	((in - out) & (ql))
#define REQ_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, 		     \
						    QLOGICFC_REQ_QUEUE_LEN)
#define RES_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, RES_QUEUE_LEN)

static void isp2x00_enable_irqs(struct Scsi_Host *);
static void isp2x00_disable_irqs(struct Scsi_Host *);
static int isp2x00_init(struct Scsi_Host *);
static int isp2x00_reset_hardware(struct Scsi_Host *);
static int isp2x00_mbox_command(struct Scsi_Host *, u_short[]);
static int isp2x00_return_status(Scsi_Cmnd *, struct Status_Entry *);
static void isp2x00_intr_handler(int, void *, struct pt_regs *);
static irqreturn_t do_isp2x00_intr_handler(int, void *, struct pt_regs *);
static int isp2x00_make_portdb(struct Scsi_Host *);

#if ISP2x00_FABRIC
static int isp2x00_init_fabric(struct Scsi_Host *, struct id_name_map *, int);
#endif

#if USE_NVRAM_DEFAULTS
static int isp2x00_get_nvram_defaults(struct Scsi_Host *, struct init_cb *);
static u_short isp2x00_read_nvram_word(struct Scsi_Host *, u_short);
#endif

#if DEBUG_ISP2x00
static void isp2x00_print_scsi_cmd(Scsi_Cmnd *);
#endif

#if DEBUG_ISP2x00_INTR
static void isp2x00_print_status_entry(struct Status_Entry *);
#endif

static inline void isp2x00_enable_irqs(struct Scsi_Host *host)
{
	outw(ISP_EN_INT | ISP_EN_RISC, host->io_port + PCI_INTER_CTL);
}


static inline void isp2x00_disable_irqs(struct Scsi_Host *host)
{
	outw(0x0, host->io_port + PCI_INTER_CTL);
}


static int isp2x00_detect(struct scsi_host_template * tmpt)
{
	int hosts = 0;
	unsigned long wait_time;
	struct Scsi_Host *host = NULL;
	struct isp2x00_hostdata *hostdata;
	struct pci_dev *pdev;
	unsigned short device_ids[2];
	dma_addr_t busaddr;
	int i;


	ENTER("isp2x00_detect");

       	device_ids[0] = PCI_DEVICE_ID_QLOGIC_ISP2100;
	device_ids[1] = PCI_DEVICE_ID_QLOGIC_ISP2200;

	tmpt->proc_name = "isp2x00";

	for (i=0; i<2; i++){
		pdev = NULL;
	        while ((pdev = pci_find_device(PCI_VENDOR_ID_QLOGIC, device_ids[i], pdev))) {
			if (pci_enable_device(pdev))
				continue;

			/* Try to configure DMA attributes. */
			if (pci_set_dma_mask(pdev, DMA_64BIT_MASK) &&
			    pci_set_dma_mask(pdev, DMA_32BIT_MASK))
					continue;

		        host = scsi_register(tmpt, sizeof(struct isp2x00_hostdata));
			if (!host) {
			        printk("qlogicfc%d : could not register host.\n", hosts);
				continue;
			}
			host->max_id = QLOGICFC_MAX_ID + 1;
			host->max_lun = QLOGICFC_MAX_LUN;
			hostdata = (struct isp2x00_hostdata *) host->hostdata;

			memset(hostdata, 0, sizeof(struct isp2x00_hostdata));
			hostdata->pci_dev = pdev;
			hostdata->res = pci_alloc_consistent(pdev, RES_SIZE + REQ_SIZE, &busaddr);

			if (!hostdata->res){
			        printk("qlogicfc%d : could not allocate memory for request and response queue.\n", hosts);
			        scsi_unregister(host);
				continue;
			}
			hostdata->req = hostdata->res + (RES_QUEUE_LEN + 1)*QUEUE_ENTRY_LEN;
			hostdata->queued = 0;
			/* set up the control block */
			hostdata->control_block.version = 0x1;
			hostdata->control_block.firm_opts = cpu_to_le16(0x800e);
			hostdata->control_block.max_frame_len = cpu_to_le16(2048);
			hostdata->control_block.max_iocb = cpu_to_le16(QLOGICFC_REQ_QUEUE_LEN);
			hostdata->control_block.exec_throttle = cpu_to_le16(QLOGICFC_CMD_PER_LUN);
			hostdata->control_block.retry_delay = 5;
			hostdata->control_block.retry_cnt = 1;
			hostdata->control_block.node_name[0] = cpu_to_le16(0x0020);
			hostdata->control_block.node_name[1] = cpu_to_le16(0xE000);
			hostdata->control_block.node_name[2] = cpu_to_le16(0x008B);
			hostdata->control_block.node_name[3] = cpu_to_le16(0x0000);
			hostdata->control_block.hard_addr = cpu_to_le16(0x0003);
			hostdata->control_block.req_queue_len = cpu_to_le16(QLOGICFC_REQ_QUEUE_LEN + 1);
			hostdata->control_block.res_queue_len = cpu_to_le16(RES_QUEUE_LEN + 1);
			hostdata->control_block.res_queue_addr_lo = cpu_to_le32(pci64_dma_lo32(busaddr));
			hostdata->control_block.res_queue_addr_high = cpu_to_le32(pci64_dma_hi32(busaddr));
			hostdata->control_block.req_queue_addr_lo = cpu_to_le32(pci64_dma_lo32(busaddr + RES_SIZE));
			hostdata->control_block.req_queue_addr_high = cpu_to_le32(pci64_dma_hi32(busaddr + RES_SIZE));


			hostdata->control_block.add_firm_opts |= cpu_to_le16(CONNECTION_PREFERENCE<<4);
			hostdata->adapter_state = AS_LOOP_DOWN;
			hostdata->explore_timer.data = 1;
			hostdata->host_id = hosts;

			if (isp2x00_init(host) || isp2x00_reset_hardware(host)) {
				pci_free_consistent (pdev, RES_SIZE + REQ_SIZE, hostdata->res, busaddr);
			        scsi_unregister(host);
				continue;
			}
			host->this_id = 0;

			if (request_irq(host->irq, do_isp2x00_intr_handler, SA_INTERRUPT | SA_SHIRQ, "qlogicfc", host)) {
			        printk("qlogicfc%d : interrupt %d already in use\n",
				       hostdata->host_id, host->irq);
				pci_free_consistent (pdev, RES_SIZE + REQ_SIZE, hostdata->res, busaddr);
				scsi_unregister(host);
				continue;
			}
			if (!request_region(host->io_port, 0xff, "qlogicfc")) {
			        printk("qlogicfc%d : i/o region 0x%lx-0x%lx already "
				       "in use\n",
				       hostdata->host_id, host->io_port, host->io_port + 0xff);
				free_irq(host->irq, host);
				pci_free_consistent (pdev, RES_SIZE + REQ_SIZE, hostdata->res, busaddr);
				scsi_unregister(host);
				continue;
			}

			outw(0x0, host->io_port + PCI_SEMAPHORE);
			outw(HCCR_CLEAR_RISC_INTR, host->io_port + HOST_HCCR);
			isp2x00_enable_irqs(host);
			/* wait for the loop to come up */
			for (wait_time = jiffies + 10 * HZ; time_before(jiffies, wait_time) && hostdata->adapter_state == AS_LOOP_DOWN;) {
			        barrier();
				cpu_relax();
			}
			if (hostdata->adapter_state == AS_LOOP_DOWN) {
			        printk("qlogicfc%d : link is not up\n", hostdata->host_id);
			}
			hosts++;
			hostdata->explore_timer.data = 0;
		}
	}


	/* this busy loop should not be needed but the isp2x00 seems to need 
	   some time before recognizing it is attached to a fabric */

#if ISP2x00_FABRIC
	if (hosts) {
		for (wait_time = jiffies + 5 * HZ; time_before(jiffies, wait_time);) {
			barrier();
			cpu_relax();
		}
	}
#endif

	LEAVE("isp2x00_detect");

	return hosts;
}


static int isp2x00_make_portdb(struct Scsi_Host *host)
{

	short param[8];
	int i, j;
	struct isp2x00_hostdata *hostdata;

	isp2x00_disable_irqs(host);

	hostdata = (struct isp2x00_hostdata *) host->hostdata;
	memset(hostdata->tempmap, 0, sizeof(hostdata->tempmap));

#if ISP2x00_FABRIC
	for (i = 0x81; i < QLOGICFC_MAX_ID; i++) {
		param[0] = MBOX_PORT_LOGOUT;
		param[1] = i << 8;
		param[2] = 0;
		param[3] = 0;

		isp2x00_mbox_command(host, param);

		if (param[0] != MBOX_COMMAND_COMPLETE) {

			DEBUG_FABRIC(printk("qlogicfc%d : logout failed %x  %x\n", hostdata->host_id, i, param[0]));
		}
	}
#endif


	param[0] = MBOX_GET_INIT_SCSI_ID;

	isp2x00_mbox_command(host, param);

	if (param[0] == MBOX_COMMAND_COMPLETE) {
		hostdata->port_id = ((u_int) param[3]) << 16;
		hostdata->port_id |= param[2];
		hostdata->tempmap[0].loop_id = param[1];
		hostdata->tempmap[0].wwn = hostdata->wwn;
	}
	else {
	        printk("qlogicfc%d : error getting scsi id.\n", hostdata->host_id);
	}

        for (i = 0; i <=QLOGICFC_MAX_ID; i++)
                hostdata->tempmap[i].loop_id = hostdata->tempmap[0].loop_id;
   
        for (i = 0, j = 1; i <= QLOGICFC_MAX_LOOP_ID; i++) {
                param[0] = MBOX_GET_PORT_NAME;
		param[1] = (i << 8) & 0xff00;

		isp2x00_mbox_command(host, param);

		if (param[0] == MBOX_COMMAND_COMPLETE) {
			hostdata->tempmap[j].loop_id = i;
			hostdata->tempmap[j].wwn = ((u64) (param[2] & 0xff)) << 56;
			hostdata->tempmap[j].wwn |= ((u64) ((param[2] >> 8) & 0xff)) << 48;
			hostdata->tempmap[j].wwn |= ((u64) (param[3] & 0xff)) << 40;
			hostdata->tempmap[j].wwn |= ((u64) ((param[3] >> 8) & 0xff)) << 32;
			hostdata->tempmap[j].wwn |= ((u64) (param[6] & 0xff)) << 24;
			hostdata->tempmap[j].wwn |= ((u64) ((param[6] >> 8) & 0xff)) << 16;
			hostdata->tempmap[j].wwn |= ((u64) (param[7] & 0xff)) << 8;
			hostdata->tempmap[j].wwn |= ((u64) ((param[7] >> 8) & 0xff));

			j++;

		}
	}


#if ISP2x00_FABRIC
	isp2x00_init_fabric(host, hostdata->tempmap, j);
#endif

	for (i = 0; i <= QLOGICFC_MAX_ID; i++) {
		if (hostdata->tempmap[i].wwn != hostdata->port_db[i].wwn) {
			for (j = 0; j <= QLOGICFC_MAX_ID; j++) {
				if (hostdata->tempmap[j].wwn == hostdata->port_db[i].wwn) {
					hostdata->port_db[i].loop_id = hostdata->tempmap[j].loop_id;
					break;
				}
			}
			if (j == QLOGICFC_MAX_ID + 1)
				hostdata->port_db[i].loop_id = hostdata->tempmap[0].loop_id;

			for (j = 0; j <= QLOGICFC_MAX_ID; j++) {
				if (hostdata->port_db[j].wwn == hostdata->tempmap[i].wwn || !hostdata->port_db[j].wwn) {
					break;
				}
			}
			if (j == QLOGICFC_MAX_ID + 1)
				printk("qlogicfc%d : Too many scsi devices, no more room in port map.\n", hostdata->host_id);
			if (!hostdata->port_db[j].wwn) {
				hostdata->port_db[j].loop_id = hostdata->tempmap[i].loop_id;
				hostdata->port_db[j].wwn = hostdata->tempmap[i].wwn;
			}
		} else
			hostdata->port_db[i].loop_id = hostdata->tempmap[i].loop_id;

	}

	isp2x00_enable_irqs(host);

	return 0;
}


#if ISP2x00_FABRIC

#define FABRIC_PORT          0x7e
#define FABRIC_CONTROLLER    0x7f
#define FABRIC_SNS           0x80

int isp2x00_init_fabric(struct Scsi_Host *host, struct id_name_map *port_db, int cur_scsi_id)
{

	u_short param[8];
	u64 wwn;
	int done = 0;
	u_short loop_id = 0x81;
	u_short scsi_id = cur_scsi_id;
	u_int port_id;
	struct sns_cb *req;
	u_char *sns_response;
	dma_addr_t busaddr;
	struct isp2x00_hostdata *hostdata;

	hostdata = (struct isp2x00_hostdata *) host->hostdata;
	
	DEBUG_FABRIC(printk("qlogicfc%d : Checking for a fabric.\n", hostdata->host_id));
	param[0] = MBOX_GET_PORT_NAME;
	param[1] = (u16)FABRIC_PORT << 8;

	isp2x00_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		DEBUG_FABRIC(printk("qlogicfc%d : fabric check result %x\n", hostdata->host_id, param[0]));
		return 0;
	}
	printk("qlogicfc%d : Fabric found.\n", hostdata->host_id);

	req = (struct sns_cb *)pci_alloc_consistent(hostdata->pci_dev, sizeof(*req) + 608, &busaddr);
	
	if (!req){
		printk("qlogicfc%d : Could not allocate DMA resources for fabric initialization\n", hostdata->host_id);
		return 0;
	}
	sns_response = (u_char *)(req + 1);

	if (hostdata->adapter_state & AS_REDO_LOOP_PORTDB){
	        memset(req, 0, sizeof(*req));
	
		req->len = cpu_to_le16(8);
		req->response_low = cpu_to_le32(pci64_dma_lo32(busaddr + sizeof(*req)));
		req->response_high = cpu_to_le32(pci64_dma_hi32(busaddr + sizeof(*req)));
		req->sub_len = cpu_to_le16(22);
		req->data[0] = 0x17;
		req->data[1] = 0x02;
		req->data[8] = (u_char) (hostdata->port_id & 0xff);
		req->data[9] = (u_char) (hostdata->port_id >> 8 & 0xff);
		req->data[10] = (u_char) (hostdata->port_id >> 16 & 0xff);
		req->data[13] = 0x01;
		param[0] = MBOX_SEND_SNS;
		param[1] = 30;
		param[2] = pci64_dma_lo32(busaddr) >> 16;
		param[3] = pci64_dma_lo32(busaddr);
		param[6] = pci64_dma_hi32(busaddr) >> 16;
		param[7] = pci64_dma_hi32(busaddr);

		isp2x00_mbox_command(host, param);
	
		if (param[0] != MBOX_COMMAND_COMPLETE)
		        printk("qlogicfc%d : error sending RFC-4\n", hostdata->host_id);
	}

	port_id = hostdata->port_id;
	while (!done) {
		memset(req, 0, sizeof(*req));

		req->len = cpu_to_le16(304);
		req->response_low = cpu_to_le32(pci64_dma_lo32(busaddr + sizeof(*req)));
		req->response_high = cpu_to_le32(pci64_dma_hi32(busaddr + sizeof(*req)));
		req->sub_len = cpu_to_le16(6);
		req->data[0] = 0x00;
		req->data[1] = 0x01;
		req->data[8] = (u_char) (port_id & 0xff);
		req->data[9] = (u_char) (port_id >> 8 & 0xff);
		req->data[10] = (u_char) (port_id >> 16 & 0xff);

		param[0] = MBOX_SEND_SNS;
		param[1] = 14;
		param[2] = pci64_dma_lo32(busaddr) >> 16;
		param[3] = pci64_dma_lo32(busaddr);
		param[6] = pci64_dma_hi32(busaddr) >> 16;
		param[7] = pci64_dma_hi32(busaddr);

		isp2x00_mbox_command(host, param);

		if (param[0] == MBOX_COMMAND_COMPLETE) {
			DEBUG_FABRIC(printk("qlogicfc%d : found node %02x%02x%02x%02x%02x%02x%02x%02x ", hostdata->host_id, sns_response[20], sns_response[21], sns_response[22], sns_response[23], sns_response[24], sns_response[25], sns_response[26], sns_response[27]));
			DEBUG_FABRIC(printk("  port id: %02x%02x%02x\n", sns_response[17], sns_response[18], sns_response[19]));
			port_id = ((u_int) sns_response[17]) << 16;
			port_id |= ((u_int) sns_response[18]) << 8;
			port_id |= ((u_int) sns_response[19]);
			wwn = ((u64) sns_response[20]) << 56;
			wwn |= ((u64) sns_response[21]) << 48;
			wwn |= ((u64) sns_response[22]) << 40;
			wwn |= ((u64) sns_response[23]) << 32;
			wwn |= ((u64) sns_response[24]) << 24;
			wwn |= ((u64) sns_response[25]) << 16;
			wwn |= ((u64) sns_response[26]) << 8;
			wwn |= ((u64) sns_response[27]);
			if (hostdata->port_id >> 8 != port_id >> 8) {
				DEBUG_FABRIC(printk("qlogicfc%d : adding a fabric port: %x\n", hostdata->host_id, port_id));
				param[0] = MBOX_PORT_LOGIN;
				param[1] = loop_id << 8;
				param[2] = (u_short) (port_id >> 16);
				param[3] = (u_short) (port_id);

				isp2x00_mbox_command(host, param);

				if (param[0] == MBOX_COMMAND_COMPLETE) {
					port_db[scsi_id].wwn = wwn;
					port_db[scsi_id].loop_id = loop_id;
					loop_id++;
					scsi_id++;
				} else {
					printk("qlogicfc%d : Error performing port login %x\n", hostdata->host_id, param[0]);
					DEBUG_FABRIC(printk("qlogicfc%d : loop_id: %x\n", hostdata->host_id, loop_id));
					param[0] = MBOX_PORT_LOGOUT;
					param[1] = loop_id << 8;
					param[2] = 0;
					param[3] = 0;

					isp2x00_mbox_command(host, param);
					
				}

			}
			if (hostdata->port_id == port_id)
				done = 1;
		} else {
			printk("qlogicfc%d : Get All Next failed %x.\n", hostdata->host_id, param[0]);
			pci_free_consistent(hostdata->pci_dev, sizeof(*req) + 608, req, busaddr);
			return 0;
		}
	}

	pci_free_consistent(hostdata->pci_dev, sizeof(*req) + 608, req, busaddr);
	return 1;
}

#endif				/* ISP2x00_FABRIC */


static int isp2x00_release(struct Scsi_Host *host)
{
	struct isp2x00_hostdata *hostdata;
	dma_addr_t busaddr;

	ENTER("isp2x00_release");

	hostdata = (struct isp2x00_hostdata *) host->hostdata;

	outw(0x0, host->io_port + PCI_INTER_CTL);
	free_irq(host->irq, host);

	release_region(host->io_port, 0xff);

	busaddr = pci64_dma_build(le32_to_cpu(hostdata->control_block.res_queue_addr_high),
				  le32_to_cpu(hostdata->control_block.res_queue_addr_lo));
	pci_free_consistent(hostdata->pci_dev, RES_SIZE + REQ_SIZE, hostdata->res, busaddr);

	LEAVE("isp2x00_release");

	return 0;
}


static const char *isp2x00_info(struct Scsi_Host *host)
{
	static char buf[80];
	struct isp2x00_hostdata *hostdata;
	ENTER("isp2x00_info");

	hostdata = (struct isp2x00_hostdata *) host->hostdata;
	sprintf(buf,
		"QLogic ISP%04x SCSI on PCI bus %02x device %02x irq %d base 0x%lx",
		hostdata->pci_dev->device, hostdata->pci_dev->bus->number, hostdata->pci_dev->devfn, host->irq,
		host->io_port);


	LEAVE("isp2x00_info");

	return buf;
}


/*
 * The middle SCSI layer ensures that queuecommand never gets invoked
 * concurrently with itself or the interrupt handler (though the
 * interrupt handler may call this routine as part of
 * request-completion handling).
 */
static int isp2x00_queuecommand(Scsi_Cmnd * Cmnd, void (*done) (Scsi_Cmnd *))
{
	int i, sg_count, n, num_free;
	u_int in_ptr, out_ptr;
	struct dataseg *ds;
	struct scatterlist *sg;
	struct Command_Entry *cmd;
	struct Continuation_Entry *cont;
	struct Scsi_Host *host;
	struct isp2x00_hostdata *hostdata;

	ENTER("isp2x00_queuecommand");

	host = Cmnd->device->host;
	hostdata = (struct isp2x00_hostdata *) host->hostdata;
	Cmnd->scsi_done = done;

	DEBUG(isp2x00_print_scsi_cmd(Cmnd));

	if (hostdata->adapter_state & AS_REDO_FABRIC_PORTDB || hostdata->adapter_state & AS_REDO_LOOP_PORTDB) {
		isp2x00_make_portdb(host);
		hostdata->adapter_state = AS_LOOP_GOOD;
		printk("qlogicfc%d : Port Database\n", hostdata->host_id);
		for (i = 0; hostdata->port_db[i].wwn != 0; i++) {
			printk("wwn: %08x%08x  scsi_id: %x  loop_id: ", (u_int) (hostdata->port_db[i].wwn >> 32), (u_int) hostdata->port_db[i].wwn, i);
			if (hostdata->port_db[i].loop_id != hostdata->port_db[0].loop_id || i == 0)
			        printk("%x", hostdata->port_db[i].loop_id);
			else
			        printk("Not Available");
			printk("\n");
		}
	}
	if (hostdata->adapter_state == AS_FIRMWARE_DEAD) {
		printk("qlogicfc%d : The firmware is dead, just return.\n", hostdata->host_id);
		host->max_id = 0;
		return 0;
	}

	out_ptr = inw(host->io_port + MBOX4);
	in_ptr = hostdata->req_in_ptr;

	DEBUG(printk("qlogicfc%d : request queue depth %d\n", hostdata->host_id,
		     REQ_QUEUE_DEPTH(in_ptr, out_ptr)));

	cmd = (struct Command_Entry *) &hostdata->req[in_ptr*QUEUE_ENTRY_LEN];
	in_ptr = (in_ptr + 1) & QLOGICFC_REQ_QUEUE_LEN;
	if (in_ptr == out_ptr) {
		DEBUG(printk("qlogicfc%d : request queue overflow\n", hostdata->host_id));
		return 1;
	}
	if (hostdata->send_marker) {
		struct Marker_Entry *marker;

		TRACE("queue marker", in_ptr, 0);

		DEBUG(printk("qlogicfc%d : adding marker entry\n", hostdata->host_id));
		marker = (struct Marker_Entry *) cmd;
		memset(marker, 0, sizeof(struct Marker_Entry));

		marker->hdr.entry_type = ENTRY_MARKER;
		marker->hdr.entry_cnt = 1;
		marker->modifier = SYNC_ALL;

		hostdata->send_marker = 0;

		if (((in_ptr + 1) & QLOGICFC_REQ_QUEUE_LEN) == out_ptr) {
			outw(in_ptr, host->io_port + MBOX4);
			hostdata->req_in_ptr = in_ptr;
			DEBUG(printk("qlogicfc%d : request queue overflow\n", hostdata->host_id));
			return 1;
		}
		cmd = (struct Command_Entry *) &hostdata->req[in_ptr*QUEUE_ENTRY_LEN];
		in_ptr = (in_ptr + 1) & QLOGICFC_REQ_QUEUE_LEN;
	}
	TRACE("queue command", in_ptr, Cmnd);

	memset(cmd, 0, sizeof(struct Command_Entry));

	/* find a free handle mapping slot */
	for (i = in_ptr; i != (in_ptr - 1) && hostdata->handle_ptrs[i]; i = ((i + 1) % (QLOGICFC_REQ_QUEUE_LEN + 1)));

	if (!hostdata->handle_ptrs[i]) {
		cmd->handle = cpu_to_le32(i);
		hostdata->handle_ptrs[i] = Cmnd;
		hostdata->handle_serials[i] = Cmnd->serial_number;
	} else {
		printk("qlogicfc%d : no handle slots, this should not happen.\n", hostdata->host_id);
		printk("hostdata->queued is %x, in_ptr: %x\n", hostdata->queued, in_ptr);
		for (i = 0; i <= QLOGICFC_REQ_QUEUE_LEN; i++){
			if (!hostdata->handle_ptrs[i]){
				printk("slot %d has %p\n", i, hostdata->handle_ptrs[i]);
			}
		}
		return 1;
	}

	cmd->hdr.entry_type = ENTRY_COMMAND;
	cmd->hdr.entry_cnt = 1;
	cmd->target_lun = Cmnd->device->lun;
	cmd->expanded_lun = cpu_to_le16(Cmnd->device->lun);
#if ISP2x00_PORTDB
	cmd->target_id = hostdata->port_db[Cmnd->device->id].loop_id;
#else
	cmd->target_id = Cmnd->target;
#endif
	cmd->total_byte_cnt = cpu_to_le32(Cmnd->request_bufflen);
	cmd->time_out = 0;
	memcpy(cmd->cdb, Cmnd->cmnd, Cmnd->cmd_len);

	if (Cmnd->use_sg) {
		sg = (struct scatterlist *) Cmnd->request_buffer;
		sg_count = pci_map_sg(hostdata->pci_dev, sg, Cmnd->use_sg, Cmnd->sc_data_direction);
		cmd->segment_cnt = cpu_to_le16(sg_count);
		ds = cmd->dataseg;
		/* fill in first two sg entries: */
		n = sg_count;
		if (n > DATASEGS_PER_COMMAND)
			n = DATASEGS_PER_COMMAND;

		for (i = 0; i < n; i++) {
			ds[i].d_base = cpu_to_le32(pci64_dma_lo32(sg_dma_address(sg)));
			ds[i].d_base_hi = cpu_to_le32(pci64_dma_hi32(sg_dma_address(sg)));
			ds[i].d_count = cpu_to_le32(sg_dma_len(sg));
			++sg;
		}
		sg_count -= DATASEGS_PER_COMMAND;

		while (sg_count > 0) {
			++cmd->hdr.entry_cnt;
			cont = (struct Continuation_Entry *)
			    &hostdata->req[in_ptr*QUEUE_ENTRY_LEN];
			memset(cont, 0, sizeof(struct Continuation_Entry));
			in_ptr = (in_ptr + 1) & QLOGICFC_REQ_QUEUE_LEN;
			if (in_ptr == out_ptr) {
				DEBUG(printk("qlogicfc%d : unexpected request queue overflow\n", hostdata->host_id));
				return 1;
			}
			TRACE("queue continuation", in_ptr, 0);
			cont->hdr.entry_type = ENTRY_CONTINUATION;
			ds = cont->dataseg;
			n = sg_count;
			if (n > DATASEGS_PER_CONT)
				n = DATASEGS_PER_CONT;
			for (i = 0; i < n; ++i) {
				ds[i].d_base = cpu_to_le32(pci64_dma_lo32(sg_dma_address(sg)));
				ds[i].d_base_hi = cpu_to_le32(pci64_dma_hi32(sg_dma_address(sg)));
				ds[i].d_count = cpu_to_le32(sg_dma_len(sg));
				++sg;
			}
			sg_count -= n;
		}
	} else if (Cmnd->request_bufflen && Cmnd->sc_data_direction != PCI_DMA_NONE) {
		struct page *page = virt_to_page(Cmnd->request_buffer);
		unsigned long offset = offset_in_page(Cmnd->request_buffer);
		dma_addr_t busaddr = pci_map_page(hostdata->pci_dev,
						  page, offset,
						  Cmnd->request_bufflen,
						  Cmnd->sc_data_direction);
		Cmnd->SCp.dma_handle = busaddr;

		cmd->dataseg[0].d_base = cpu_to_le32(pci64_dma_lo32(busaddr));
		cmd->dataseg[0].d_base_hi = cpu_to_le32(pci64_dma_hi32(busaddr));
		cmd->dataseg[0].d_count = cpu_to_le32(Cmnd->request_bufflen);
		cmd->segment_cnt = cpu_to_le16(1);
	} else {
		cmd->dataseg[0].d_base = 0;
		cmd->dataseg[0].d_base_hi = 0;
		cmd->segment_cnt = cpu_to_le16(1); /* Shouldn't this be 0? */
	}

	if (Cmnd->sc_data_direction == DMA_TO_DEVICE)
		cmd->control_flags = cpu_to_le16(CFLAG_WRITE);
	else 
		cmd->control_flags = cpu_to_le16(CFLAG_READ);

	if (Cmnd->device->tagged_supported) {
		if (time_after(jiffies, hostdata->tag_ages[Cmnd->device->id] + (2 * ISP_TIMEOUT))) {
			cmd->control_flags |= cpu_to_le16(CFLAG_ORDERED_TAG);
			hostdata->tag_ages[Cmnd->device->id] = jiffies;
		} else
			switch (Cmnd->tag) {
			case HEAD_OF_QUEUE_TAG:
				cmd->control_flags |= cpu_to_le16(CFLAG_HEAD_TAG);
				break;
			case ORDERED_QUEUE_TAG:
				cmd->control_flags |= cpu_to_le16(CFLAG_ORDERED_TAG);
				break;
			default:
				cmd->control_flags |= cpu_to_le16(CFLAG_SIMPLE_TAG);
				break;
		}
	}
	/*
	 * TEST_UNIT_READY commands from scsi_scan will fail due to "overlapped
	 * commands attempted" unless we setup at least a simple queue (midlayer 
	 * will embelish this once it can do an INQUIRY command to the device)
	 */
	else
		cmd->control_flags |= cpu_to_le16(CFLAG_SIMPLE_TAG);
	outw(in_ptr, host->io_port + MBOX4);
	hostdata->req_in_ptr = in_ptr;

	hostdata->queued++;

	num_free = QLOGICFC_REQ_QUEUE_LEN - REQ_QUEUE_DEPTH(in_ptr, out_ptr);
	num_free = (num_free > 2) ? num_free - 2 : 0;
       host->can_queue = host->host_busy + num_free;
	if (host->can_queue > QLOGICFC_REQ_QUEUE_LEN)
		host->can_queue = QLOGICFC_REQ_QUEUE_LEN;
	host->sg_tablesize = QLOGICFC_MAX_SG(num_free);

	LEAVE("isp2x00_queuecommand");

	return 0;
}


/* we have received an event, such as a lip or an RSCN, which may mean that
 * our port database is incorrect so the port database must be recreated.
 */
static void redo_port_db(unsigned long arg)
{

        struct Scsi_Host * host = (struct Scsi_Host *) arg;
	struct isp2x00_hostdata * hostdata;
	unsigned long flags;
	int i;

	hostdata = (struct isp2x00_hostdata *) host->hostdata;
	hostdata->explore_timer.data = 0;
	del_timer(&hostdata->explore_timer);

	spin_lock_irqsave(host->host_lock, flags);

	if (hostdata->adapter_state & AS_REDO_FABRIC_PORTDB || hostdata->adapter_state & AS_REDO_LOOP_PORTDB) {
		isp2x00_make_portdb(host);
		printk("qlogicfc%d : Port Database\n", hostdata->host_id);
		for (i = 0; hostdata->port_db[i].wwn != 0; i++) {
			printk("wwn: %08x%08x  scsi_id: %x  loop_id: ", (u_int) (hostdata->port_db[i].wwn >> 32), (u_int) hostdata->port_db[i].wwn, i);
			if (hostdata->port_db[i].loop_id != hostdata->port_db[0].loop_id || i == 0)
			        printk("%x", hostdata->port_db[i].loop_id);
			else
			        printk("Not Available");
			printk("\n");
		}
		
	        for (i = 0; i < QLOGICFC_REQ_QUEUE_LEN; i++){ 
		        if (hostdata->handle_ptrs[i] && (hostdata->port_db[hostdata->handle_ptrs[i]->device->id].loop_id > QLOGICFC_MAX_LOOP_ID || hostdata->adapter_state & AS_REDO_LOOP_PORTDB)){
                                if (hostdata->port_db[hostdata->handle_ptrs[i]->device->id].loop_id != hostdata->port_db[0].loop_id){
					Scsi_Cmnd *Cmnd = hostdata->handle_ptrs[i];

					 if (Cmnd->use_sg)
						 pci_unmap_sg(hostdata->pci_dev,
							      (struct scatterlist *)Cmnd->buffer,
							      Cmnd->use_sg,
							      Cmnd->sc_data_direction);
					 else if (Cmnd->request_bufflen &&
						  Cmnd->sc_data_direction != PCI_DMA_NONE) {
						 pci_unmap_page(hostdata->pci_dev,
								Cmnd->SCp.dma_handle,
								Cmnd->request_bufflen,
								Cmnd->sc_data_direction);
					 }

					 hostdata->handle_ptrs[i]->result = DID_SOFT_ERROR << 16;

					 if (hostdata->handle_ptrs[i]->scsi_done){
					   (*hostdata->handle_ptrs[i]->scsi_done) (hostdata->handle_ptrs[i]);
					 }
					 else printk("qlogicfc%d : done is null?\n", hostdata->host_id);
					 hostdata->handle_ptrs[i] = NULL;
					 hostdata->handle_serials[i] = 0;
				}
			}
		}
		
		hostdata->adapter_state = AS_LOOP_GOOD;
	}

	spin_unlock_irqrestore(host->host_lock, flags);

}

#define ASYNC_EVENT_INTERRUPT	0x01

irqreturn_t do_isp2x00_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct Scsi_Host *host = dev_id;
	unsigned long flags;

	spin_lock_irqsave(host->host_lock, flags);
	isp2x00_intr_handler(irq, dev_id, regs);
	spin_unlock_irqrestore(host->host_lock, flags);

	return IRQ_HANDLED;
}

void isp2x00_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	Scsi_Cmnd *Cmnd;
	struct Status_Entry *sts;
	struct Scsi_Host *host = dev_id;
	struct isp2x00_hostdata *hostdata;
	u_int in_ptr, out_ptr, handle, num_free;
	u_short status;

	ENTER_INTR("isp2x00_intr_handler");

	hostdata = (struct isp2x00_hostdata *) host->hostdata;

	DEBUG_INTR(printk("qlogicfc%d : interrupt on line %d\n", hostdata->host_id, irq));

	if (!(inw(host->io_port + PCI_INTER_STS) & 0x08)) {
		/* spurious interrupts can happen legally */
		DEBUG_INTR(printk("qlogicfc%d : got spurious interrupt\n", hostdata->host_id));
		return;
	}
	in_ptr = inw(host->io_port + MBOX5);
	out_ptr = hostdata->res_out_ptr;

	if ((inw(host->io_port + PCI_SEMAPHORE) & ASYNC_EVENT_INTERRUPT)) {
		status = inw(host->io_port + MBOX0);

		DEBUG_INTR(printk("qlogicfc%d : mbox completion status: %x\n",
				  hostdata->host_id, status));

		switch (status) {
		case LOOP_UP:
		case POINT_TO_POINT_UP:
		        printk("qlogicfc%d : Link is Up\n", hostdata->host_id);
			hostdata->adapter_state = AS_REDO_FABRIC_PORTDB | AS_REDO_LOOP_PORTDB;
			break;
		case LOOP_DOWN:
		        printk("qlogicfc%d : Link is Down\n", hostdata->host_id);
			hostdata->adapter_state = AS_LOOP_DOWN;
			break;
		case CONNECTION_MODE:
		        printk("received CONNECTION_MODE irq %x\n", inw(host->io_port + MBOX1));
			break;
		case CHANGE_NOTIFICATION:
		        printk("qlogicfc%d : RSCN Received\n", hostdata->host_id);
			if (hostdata->adapter_state == AS_LOOP_GOOD)
				hostdata->adapter_state = AS_REDO_FABRIC_PORTDB;
			break;		        
		case LIP_OCCURRED:
		case LIP_RECEIVED:
		        printk("qlogicfc%d : Loop Reinitialized\n", hostdata->host_id);
			if (hostdata->adapter_state == AS_LOOP_GOOD)
				hostdata->adapter_state = AS_REDO_LOOP_PORTDB;
			break;
		case SYSTEM_ERROR:
			printk("qlogicfc%d : The firmware just choked.\n", hostdata->host_id);
			hostdata->adapter_state = AS_FIRMWARE_DEAD;
			break;
		case SCSI_COMMAND_COMPLETE:
			handle = inw(host->io_port + MBOX1) | (inw(host->io_port + MBOX2) << 16);
			Cmnd = hostdata->handle_ptrs[handle];
			hostdata->handle_ptrs[handle] = NULL;
			hostdata->handle_serials[handle] = 0;
			hostdata->queued--;
			if (Cmnd != NULL) {
				if (Cmnd->use_sg)
					pci_unmap_sg(hostdata->pci_dev,
						     (struct scatterlist *)Cmnd->buffer,
						     Cmnd->use_sg,
						     Cmnd->sc_data_direction);
				else if (Cmnd->request_bufflen &&
					 Cmnd->sc_data_direction != PCI_DMA_NONE)
					pci_unmap_page(hostdata->pci_dev,
						       Cmnd->SCp.dma_handle,
						       Cmnd->request_bufflen,
						       Cmnd->sc_data_direction);
				Cmnd->result = 0x0;
				(*Cmnd->scsi_done) (Cmnd);
			} else
				printk("qlogicfc%d.c : got a null value out of handle_ptrs, this sucks\n", hostdata->host_id);
			break;
		case MBOX_COMMAND_COMPLETE:
		case INVALID_COMMAND:
		case HOST_INTERFACE_ERROR:
		case TEST_FAILED:
		case COMMAND_ERROR:
		case COMMAND_PARAM_ERROR:
		case PORT_ID_USED:
		case LOOP_ID_USED:
		case ALL_IDS_USED:
			hostdata->mbox_done = 1;
			outw(HCCR_CLEAR_RISC_INTR, host->io_port + HOST_HCCR);
			return;
		default:
			printk("qlogicfc%d : got an unknown status? %x\n", hostdata->host_id, status);
		}
		if ((hostdata->adapter_state & AS_REDO_LOOP_PORTDB || hostdata->adapter_state & AS_REDO_FABRIC_PORTDB) && hostdata->explore_timer.data == 0){
                        hostdata->explore_timer.function = redo_port_db;
			hostdata->explore_timer.data = (unsigned long)host;
			hostdata->explore_timer.expires = jiffies + (HZ/4);
			init_timer(&hostdata->explore_timer);
			add_timer(&hostdata->explore_timer);
		}
		outw(0x0, host->io_port + PCI_SEMAPHORE);
	} else {
		DEBUG_INTR(printk("qlogicfc%d : response queue update\n", hostdata->host_id));
		DEBUG_INTR(printk("qlogicfc%d : response queue depth %d\n", hostdata->host_id, RES_QUEUE_DEPTH(in_ptr, out_ptr)));

		while (out_ptr != in_ptr) {
			unsigned le_hand;
			sts = (struct Status_Entry *) &hostdata->res[out_ptr*QUEUE_ENTRY_LEN];
			out_ptr = (out_ptr + 1) & RES_QUEUE_LEN;
                 
			TRACE("done", out_ptr, Cmnd);
			DEBUG_INTR(isp2x00_print_status_entry(sts));
			le_hand = le32_to_cpu(sts->handle);
			if (sts->hdr.entry_type == ENTRY_STATUS && (Cmnd = hostdata->handle_ptrs[le_hand])) {
				Cmnd->result = isp2x00_return_status(Cmnd, sts);
				hostdata->queued--;

				if (Cmnd->use_sg)
					pci_unmap_sg(hostdata->pci_dev,
						     (struct scatterlist *)Cmnd->buffer, Cmnd->use_sg,
						     Cmnd->sc_data_direction);
				else if (Cmnd->request_bufflen && Cmnd->sc_data_direction != PCI_DMA_NONE)
					pci_unmap_page(hostdata->pci_dev,
						       Cmnd->SCp.dma_handle,
						       Cmnd->request_bufflen,
						       Cmnd->sc_data_direction);

				/* 
				 * if any of the following are true we do not
				 * call scsi_done.  if the status is CS_ABORTED
				 * we don't have to call done because the upper
				 * level should already know its aborted.
				 */
				if (hostdata->handle_serials[le_hand] != Cmnd->serial_number 
				    || le16_to_cpu(sts->completion_status) == CS_ABORTED){
					hostdata->handle_serials[le_hand] = 0;
					hostdata->handle_ptrs[le_hand] = NULL;
					outw(out_ptr, host->io_port + MBOX5);
					continue;
				}
				/*
				 * if we get back an error indicating the port
				 * is not there or if the link is down and 
				 * this is a device that used to be there 
				 * allow the command to timeout.
				 * the device may well be back in a couple of
				 * seconds.
				 */
				if ((hostdata->adapter_state == AS_LOOP_DOWN || sts->completion_status == cpu_to_le16(CS_PORT_UNAVAILABLE) || sts->completion_status == cpu_to_le16(CS_PORT_LOGGED_OUT) || sts->completion_status == cpu_to_le16(CS_PORT_CONFIG_CHANGED)) && hostdata->port_db[Cmnd->device->id].wwn){
					outw(out_ptr, host->io_port + MBOX5);
					continue;
				}
			} else {
				outw(out_ptr, host->io_port + MBOX5);
				continue;
			}

			hostdata->handle_ptrs[le_hand] = NULL;

			if (sts->completion_status == cpu_to_le16(CS_RESET_OCCURRED)
			    || (sts->status_flags & cpu_to_le16(STF_BUS_RESET)))
				hostdata->send_marker = 1;

			if (le16_to_cpu(sts->scsi_status) & 0x0200)
				memcpy(Cmnd->sense_buffer, sts->req_sense_data,
				       sizeof(Cmnd->sense_buffer));

			outw(out_ptr, host->io_port + MBOX5);

			if (Cmnd->scsi_done != NULL) {
				(*Cmnd->scsi_done) (Cmnd);
			} else
				printk("qlogicfc%d : Ouch, scsi done is NULL\n", hostdata->host_id);
		}
		hostdata->res_out_ptr = out_ptr;
	}


	out_ptr = inw(host->io_port + MBOX4);
	in_ptr = hostdata->req_in_ptr;

	num_free = QLOGICFC_REQ_QUEUE_LEN - REQ_QUEUE_DEPTH(in_ptr, out_ptr);
	num_free = (num_free > 2) ? num_free - 2 : 0;
       host->can_queue = host->host_busy + num_free;
	if (host->can_queue > QLOGICFC_REQ_QUEUE_LEN)
		host->can_queue = QLOGICFC_REQ_QUEUE_LEN;
	host->sg_tablesize = QLOGICFC_MAX_SG(num_free);

	outw(HCCR_CLEAR_RISC_INTR, host->io_port + HOST_HCCR);
	LEAVE_INTR("isp2x00_intr_handler");
}


static int isp2x00_return_status(Scsi_Cmnd *Cmnd, struct Status_Entry *sts)
{
	int host_status = DID_ERROR;
#if DEBUG_ISP2x00_INTR
	static char *reason[] =
	{
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
#endif				/* DEBUG_ISP2x00_INTR */

	ENTER("isp2x00_return_status");

	DEBUG(printk("qlogicfc : completion status = 0x%04x\n",
		     le16_to_cpu(sts->completion_status)));

	switch (le16_to_cpu(sts->completion_status)) {
	case CS_COMPLETE:
		host_status = DID_OK;
		break;
	case CS_DMA_ERROR:
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
		host_status = DID_ERROR;
		break;
	case CS_DATA_UNDERRUN:
	        if (Cmnd->underflow <= (Cmnd->request_bufflen - le32_to_cpu(sts->residual)))
		        host_status = DID_OK;
		else
		        host_status = DID_ERROR;
		break;
	case CS_PORT_UNAVAILABLE:
	case CS_PORT_LOGGED_OUT:
	case CS_PORT_CONFIG_CHANGED:
		host_status = DID_BAD_TARGET;
		break;
	case CS_QUEUE_FULL:
		host_status = DID_ERROR;
		break;
	default:
		printk("qlogicfc : unknown completion status 0x%04x\n",
		       le16_to_cpu(sts->completion_status));
		host_status = DID_ERROR;
		break;
	}

	DEBUG_INTR(printk("qlogicfc : host status (%s) scsi status %x\n",
			  reason[host_status], le16_to_cpu(sts->scsi_status)));

	LEAVE("isp2x00_return_status");

	return (le16_to_cpu(sts->scsi_status) & STATUS_MASK) | (host_status << 16);
}


static int isp2x00_abort(Scsi_Cmnd * Cmnd)
{
	u_short param[8];
	int i;
	struct Scsi_Host *host;
	struct isp2x00_hostdata *hostdata;
	int return_status = SUCCESS;

	ENTER("isp2x00_abort");

	host = Cmnd->device->host;
	hostdata = (struct isp2x00_hostdata *) host->hostdata;

	for (i = 0; i < QLOGICFC_REQ_QUEUE_LEN; i++)
		if (hostdata->handle_ptrs[i] == Cmnd)
			break;

	if (i == QLOGICFC_REQ_QUEUE_LEN){
		return SUCCESS;
	}

	isp2x00_disable_irqs(host);

	param[0] = MBOX_ABORT_IOCB;
#if ISP2x00_PORTDB
	param[1] = (((u_short) hostdata->port_db[Cmnd->device->id].loop_id) << 8) | Cmnd->device->lun;
#else
	param[1] = (((u_short) Cmnd->target) << 8) | Cmnd->lun;
#endif
	param[2] = i & 0xffff;
	param[3] = i >> 16;

	isp2x00_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicfc%d : scsi abort failure: %x\n", hostdata->host_id, param[0]);
		if (param[0] == 0x4005)
			Cmnd->result = DID_ERROR << 16;
		if (param[0] == 0x4006)
			Cmnd->result = DID_BAD_TARGET << 16;
		return_status = FAILED;
	}

	if (return_status != SUCCESS){
		param[0] = MBOX_GET_FIRMWARE_STATE;
		isp2x00_mbox_command(host, param);
		printk("qlogicfc%d : abort failed\n", hostdata->host_id);
		printk("qlogicfc%d : firmware status is %x %x\n", hostdata->host_id, param[0], param[1]);
	}

	isp2x00_enable_irqs(host);

	LEAVE("isp2x00_abort");

	return return_status;
}


static int isp2x00_biosparam(struct scsi_device *sdev, struct block_device *n,
		sector_t capacity, int ip[])
{
	int size = capacity;

	ENTER("isp2x00_biosparam");

	ip[0] = 64;
	ip[1] = 32;
	ip[2] = size >> 11;
	if (ip[2] > 1024) {
		ip[0] = 255;
		ip[1] = 63;
		ip[2] = size / (ip[0] * ip[1]);
	}
	LEAVE("isp2x00_biosparam");

	return 0;
}

static int isp2x00_reset_hardware(struct Scsi_Host *host)
{
	u_short param[8];
	struct isp2x00_hostdata *hostdata;
	int loop_count;
	dma_addr_t busaddr;

	ENTER("isp2x00_reset_hardware");

	hostdata = (struct isp2x00_hostdata *) host->hostdata;

	/*
	 *	This cannot be right - PCI writes are posted
	 *	(apparently this is hardware design flaw not software ?)
	 */
	 
	outw(0x01, host->io_port + ISP_CTRL_STATUS);
	udelay(100);
	outw(HCCR_RESET, host->io_port + HOST_HCCR);
	udelay(100);
	outw(HCCR_RELEASE, host->io_port + HOST_HCCR);
	outw(HCCR_BIOS_DISABLE, host->io_port + HOST_HCCR);

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && inw(host->io_port + HOST_HCCR) == RISC_BUSY) {
		barrier();
		cpu_relax();
	}
	if (!loop_count)
		printk("qlogicfc%d : reset_hardware loop timeout\n", hostdata->host_id);



#if DEBUG_ISP2x00
	printk("qlogicfc%d : mbox 0 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX0));
	printk("qlogicfc%d : mbox 1 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX1));
	printk("qlogicfc%d : mbox 2 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX2));
	printk("qlogicfc%d : mbox 3 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX3));
	printk("qlogicfc%d : mbox 4 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX4));
	printk("qlogicfc%d : mbox 5 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX5));
	printk("qlogicfc%d : mbox 6 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX6));
	printk("qlogicfc%d : mbox 7 0x%04x \n", hostdata->host_id,  inw(host->io_port + MBOX7));
#endif				/* DEBUG_ISP2x00 */

	DEBUG(printk("qlogicfc%d : verifying checksum\n", hostdata->host_id));

#if defined(CONFIG_SCSI_QLOGIC_FC_FIRMWARE)
	{
		int i;
		unsigned short * risc_code = NULL;
		unsigned short risc_code_len = 0;
		if (hostdata->pci_dev->device == PCI_DEVICE_ID_QLOGIC_ISP2100){
		        risc_code = risc_code2100;
			risc_code_len = risc_code_length2100;
		}
		else if (hostdata->pci_dev->device == PCI_DEVICE_ID_QLOGIC_ISP2200){
		        risc_code = risc_code2200;
			risc_code_len = risc_code_length2200;
		}

		for (i = 0; i < risc_code_len; i++) {
			param[0] = MBOX_WRITE_RAM_WORD;
			param[1] = risc_code_addr01 + i;
			param[2] = risc_code[i];

			isp2x00_mbox_command(host, param);

			if (param[0] != MBOX_COMMAND_COMPLETE) {
				printk("qlogicfc%d : firmware load failure\n", hostdata->host_id);
				return 1;
			}
		}
	}
#endif				/* RELOAD_FIRMWARE */

	param[0] = MBOX_VERIFY_CHECKSUM;
	param[1] = risc_code_addr01;

	isp2x00_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicfc%d : ram checksum failure\n", hostdata->host_id);
		return 1;
	}
	DEBUG(printk("qlogicfc%d : executing firmware\n", hostdata->host_id));

	param[0] = MBOX_EXEC_FIRMWARE;
	param[1] = risc_code_addr01;

	isp2x00_mbox_command(host, param);

	param[0] = MBOX_ABOUT_FIRMWARE;

	isp2x00_mbox_command(host, param);

	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicfc%d : about firmware failure\n", hostdata->host_id);
		return 1;
	}
	DEBUG(printk("qlogicfc%d : firmware major revision %d\n", hostdata->host_id,  param[1]));
	DEBUG(printk("qlogicfc%d : firmware minor revision %d\n", hostdata->host_id,  param[2]));

#ifdef USE_NVRAM_DEFAULTS

	if (isp2x00_get_nvram_defaults(host, &hostdata->control_block) != 0) {
		printk("qlogicfc%d : Could not read from NVRAM\n", hostdata->host_id);
	}
#endif

	hostdata->wwn = (u64) (cpu_to_le16(hostdata->control_block.node_name[0])) << 56;
	hostdata->wwn |= (u64) (cpu_to_le16(hostdata->control_block.node_name[0]) & 0xff00) << 48;
	hostdata->wwn |= (u64) (cpu_to_le16(hostdata->control_block.node_name[1]) & 0xff00) << 24;
	hostdata->wwn |= (u64) (cpu_to_le16(hostdata->control_block.node_name[1]) & 0x00ff) << 48;
	hostdata->wwn |= (u64) (cpu_to_le16(hostdata->control_block.node_name[2]) & 0x00ff) << 24;
	hostdata->wwn |= (u64) (cpu_to_le16(hostdata->control_block.node_name[2]) & 0xff00) << 8;
	hostdata->wwn |= (u64) (cpu_to_le16(hostdata->control_block.node_name[3]) & 0x00ff) << 8;
	hostdata->wwn |= (u64) (cpu_to_le16(hostdata->control_block.node_name[3]) & 0xff00) >> 8;

	/* FIXME: If the DMA transfer goes one way only, this should use
	 *        PCI_DMA_TODEVICE and below as well.
	 */
	busaddr = pci_map_page(hostdata->pci_dev,
			       virt_to_page(&hostdata->control_block),
			       offset_in_page(&hostdata->control_block),
			       sizeof(hostdata->control_block),
			       PCI_DMA_BIDIRECTIONAL);

	param[0] = MBOX_INIT_FIRMWARE;
	param[2] = (u_short) (pci64_dma_lo32(busaddr) >> 16);
	param[3] = (u_short) (pci64_dma_lo32(busaddr) & 0xffff);
	param[4] = 0;
	param[5] = 0;
	param[6] = (u_short) (pci64_dma_hi32(busaddr) >> 16);
	param[7] = (u_short) (pci64_dma_hi32(busaddr) & 0xffff);
	isp2x00_mbox_command(host, param);
	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicfc%d.c: Ouch 0x%04x\n", hostdata->host_id,  param[0]);
		pci_unmap_page(hostdata->pci_dev, busaddr,
			       sizeof(hostdata->control_block),
			       PCI_DMA_BIDIRECTIONAL);
		return 1;
	}
	param[0] = MBOX_GET_FIRMWARE_STATE;
	isp2x00_mbox_command(host, param);
	if (param[0] != MBOX_COMMAND_COMPLETE) {
		printk("qlogicfc%d.c: 0x%04x\n", hostdata->host_id,  param[0]);
		pci_unmap_page(hostdata->pci_dev, busaddr,
			       sizeof(hostdata->control_block),
			       PCI_DMA_BIDIRECTIONAL);
		return 1;
	}

	pci_unmap_page(hostdata->pci_dev, busaddr,
		       sizeof(hostdata->control_block),
		       PCI_DMA_BIDIRECTIONAL);
	LEAVE("isp2x00_reset_hardware");

	return 0;
}

#ifdef USE_NVRAM_DEFAULTS

static int isp2x00_get_nvram_defaults(struct Scsi_Host *host, struct init_cb *control_block)
{

	u_short value;
	if (isp2x00_read_nvram_word(host, 0) != 0x5349)
		return 1;

	value = isp2x00_read_nvram_word(host, 8);
	control_block->node_name[0] = cpu_to_le16(isp2x00_read_nvram_word(host, 9));
	control_block->node_name[1] = cpu_to_le16(isp2x00_read_nvram_word(host, 10));
	control_block->node_name[2] = cpu_to_le16(isp2x00_read_nvram_word(host, 11));
	control_block->node_name[3] = cpu_to_le16(isp2x00_read_nvram_word(host, 12));
	control_block->hard_addr = cpu_to_le16(isp2x00_read_nvram_word(host, 13));

	return 0;

}

#endif

static int isp2x00_init(struct Scsi_Host *sh)
{
	u_long io_base;
	struct isp2x00_hostdata *hostdata;
	u_char revision;
	u_int irq;
	u_short command;
	struct pci_dev *pdev;


	ENTER("isp2x00_init");

	hostdata = (struct isp2x00_hostdata *) sh->hostdata;
	pdev = hostdata->pci_dev;

	if (pci_read_config_word(pdev, PCI_COMMAND, &command)
	  || pci_read_config_byte(pdev, PCI_CLASS_REVISION, &revision)) {
		printk("qlogicfc%d : error reading PCI configuration\n", hostdata->host_id);
		return 1;
	}
	io_base = pci_resource_start(pdev, 0);
	irq = pdev->irq;


	if (pdev->vendor != PCI_VENDOR_ID_QLOGIC) {
		printk("qlogicfc%d : 0x%04x is not QLogic vendor ID\n", hostdata->host_id, 
		       pdev->vendor);
		return 1;
	}
	if (pdev->device != PCI_DEVICE_ID_QLOGIC_ISP2100 && pdev->device != PCI_DEVICE_ID_QLOGIC_ISP2200) {
		printk("qlogicfc%d : 0x%04x does not match ISP2100 or ISP2200 device id\n", hostdata->host_id, 
		       pdev->device);
		return 1;
	}
	if (!(command & PCI_COMMAND_IO) ||
	    !(pdev->resource[0].flags & IORESOURCE_IO)) {
		printk("qlogicfc%d : i/o mapping is disabled\n", hostdata->host_id);
		return 1;
	}

	pci_set_master(pdev);
	if (revision != ISP2100_REV_ID1 && revision != ISP2100_REV_ID3 && revision != ISP2200_REV_ID5)
		printk("qlogicfc%d : new isp2x00 revision ID (%d)\n", hostdata->host_id,  revision);


	hostdata->revision = revision;

	sh->irq = irq;
	sh->io_port = io_base;

	LEAVE("isp2x00_init");

	return 0;
}

#if USE_NVRAM_DEFAULTS

#define NVRAM_DELAY() udelay(10)	/* 10 microsecond delay */


u_short isp2x00_read_nvram_word(struct Scsi_Host * host, u_short byte)
{
	int i;
	u_short value, output, input;

	outw(0x2, host->io_port + PCI_NVRAM);
	NVRAM_DELAY();
	outw(0x3, host->io_port + PCI_NVRAM);
	NVRAM_DELAY();

	byte &= 0xff;
	byte |= 0x0600;
	for (i = 10; i >= 0; i--) {
		output = ((byte >> i) & 0x1) ? 0x4 : 0x0;
		outw(output | 0x2, host->io_port + PCI_NVRAM);
		NVRAM_DELAY();
		outw(output | 0x3, host->io_port + PCI_NVRAM);
		NVRAM_DELAY();
		outw(output | 0x2, host->io_port + PCI_NVRAM);
		NVRAM_DELAY();
	}

	for (i = 0xf, value = 0; i >= 0; i--) {
		value <<= 1;
		outw(0x3, host->io_port + PCI_NVRAM);
		NVRAM_DELAY();
		input = inw(host->io_port + PCI_NVRAM);
		NVRAM_DELAY();
		outw(0x2, host->io_port + PCI_NVRAM);
		NVRAM_DELAY();
		if (input & 0x8)
			value |= 1;
	}

	outw(0x0, host->io_port + PCI_NVRAM);
	NVRAM_DELAY();

	return value;
}


#endif				/* USE_NVRAM_DEFAULTS */



/*
 * currently, this is only called during initialization or abort/reset,
 * at which times interrupts are disabled, so polling is OK, I guess...
 */
static int isp2x00_mbox_command(struct Scsi_Host *host, u_short param[])
{
	int loop_count;
	struct isp2x00_hostdata *hostdata = (struct isp2x00_hostdata *) host->hostdata;

	if (mbox_param[param[0]] == 0 || hostdata->adapter_state == AS_FIRMWARE_DEAD)
		return 1;

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && inw(host->io_port + HOST_HCCR) & 0x0080) {
		barrier();
		cpu_relax();
	}
	if (!loop_count) {
		printk("qlogicfc%d : mbox_command loop timeout #1\n", hostdata->host_id);
		param[0] = 0x4006;
		hostdata->adapter_state = AS_FIRMWARE_DEAD;
		return 1;
	}
	hostdata->mbox_done = 0;

	if (mbox_param[param[0]] == 0)
		printk("qlogicfc%d : invalid mbox command\n", hostdata->host_id);

	if (mbox_param[param[0]] & 0x80)
		outw(param[7], host->io_port + MBOX7);
	if (mbox_param[param[0]] & 0x40)
		outw(param[6], host->io_port + MBOX6);
	if (mbox_param[param[0]] & 0x20)
		outw(param[5], host->io_port + MBOX5);
	if (mbox_param[param[0]] & 0x10)
		outw(param[4], host->io_port + MBOX4);
	if (mbox_param[param[0]] & 0x08)
		outw(param[3], host->io_port + MBOX3);
	if (mbox_param[param[0]] & 0x04)
		outw(param[2], host->io_port + MBOX2);
	if (mbox_param[param[0]] & 0x02)
		outw(param[1], host->io_port + MBOX1);
	if (mbox_param[param[0]] & 0x01)
		outw(param[0], host->io_port + MBOX0);


	outw(HCCR_SET_HOST_INTR, host->io_port + HOST_HCCR);

	while (1) {
		loop_count = DEFAULT_LOOP_COUNT;
		while (--loop_count && !(inw(host->io_port + PCI_INTER_STS) & 0x08)) { 
			barrier();
			cpu_relax();
		}

		if (!loop_count) {
			hostdata->adapter_state = AS_FIRMWARE_DEAD;
			printk("qlogicfc%d : mbox_command loop timeout #2\n", hostdata->host_id);
			break;
		}
		isp2x00_intr_handler(host->irq, host, NULL);

		if (hostdata->mbox_done == 1)
			break;

	}

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && inw(host->io_port + MBOX0) == 0x04) {
		barrier();
		cpu_relax();
	}
	if (!loop_count)
		printk("qlogicfc%d : mbox_command loop timeout #3\n", hostdata->host_id);

	param[7] = inw(host->io_port + MBOX7);
	param[6] = inw(host->io_port + MBOX6);
	param[5] = inw(host->io_port + MBOX5);
	param[4] = inw(host->io_port + MBOX4);
	param[3] = inw(host->io_port + MBOX3);
	param[2] = inw(host->io_port + MBOX2);
	param[1] = inw(host->io_port + MBOX1);
	param[0] = inw(host->io_port + MBOX0);


	outw(0x0, host->io_port + PCI_SEMAPHORE);

	if (inw(host->io_port + HOST_HCCR) & 0x0080) {
		hostdata->adapter_state = AS_FIRMWARE_DEAD;
		printk("qlogicfc%d : mbox op is still pending\n", hostdata->host_id);
	}
	return 0;
}

#if DEBUG_ISP2x00_INTR

void isp2x00_print_status_entry(struct Status_Entry *status)
{
	printk("qlogicfc : entry count = 0x%02x, type = 0x%02x, flags = 0x%02x\n", 
	status->hdr.entry_cnt, status->hdr.entry_type, status->hdr.flags);
	printk("qlogicfc : scsi status = 0x%04x, completion status = 0x%04x\n",
	       le16_to_cpu(status->scsi_status), le16_to_cpu(status->completion_status));
	printk("qlogicfc : state flags = 0x%04x, status flags = 0x%04x\n", 
	       le16_to_cpu(status->state_flags), le16_to_cpu(status->status_flags));
	printk("qlogicfc : response info length = 0x%04x, request sense length = 0x%04x\n",
	       le16_to_cpu(status->res_info_len), le16_to_cpu(status->req_sense_len));
	printk("qlogicfc : residual transfer length = 0x%08x, response = 0x%02x\n", le32_to_cpu(status->residual), status->res_info[3]);

}

#endif                         /* DEBUG_ISP2x00_INTR */


#if DEBUG_ISP2x00

void isp2x00_print_scsi_cmd(Scsi_Cmnd * cmd)
{
	int i;

	printk("qlogicfc : target = 0x%02x, lun = 0x%02x, cmd_len = 0x%02x\n", 
	       cmd->target, cmd->lun, cmd->cmd_len);
	printk("qlogicfc : command = ");
	for (i = 0; i < cmd->cmd_len; i++)
		printk("0x%02x ", cmd->cmnd[i]);
	printk("\n");
}

#endif				/* DEBUG_ISP2x00 */

MODULE_LICENSE("GPL");

static struct scsi_host_template driver_template = {
        .detect                 = isp2x00_detect,
        .release                = isp2x00_release,
        .info                   = isp2x00_info,
        .queuecommand           = isp2x00_queuecommand,
        .eh_abort_handler       = isp2x00_abort,
        .bios_param             = isp2x00_biosparam,
        .can_queue              = QLOGICFC_REQ_QUEUE_LEN,
        .this_id                = -1,
        .sg_tablesize           = QLOGICFC_MAX_SG(QLOGICFC_REQ_QUEUE_LEN),
	.cmd_per_lun		= QLOGICFC_CMD_PER_LUN,
        .use_clustering         = ENABLE_CLUSTERING,
};
#include "scsi_module.c"
