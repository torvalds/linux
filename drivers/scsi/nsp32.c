// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NinjaSCSI-32Bi Cardbus, NinjaSCSI-32UDE PCI/CardBus SCSI driver
 * Copyright (C) 2001, 2002, 2003
 *      YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>
 *      GOTO Masanori <gotom@debian.or.jp>, <gotom@debian.org>
 *
 * Revision History:
 *   1.0: Initial Release.
 *   1.1: Add /proc SDTR status.
 *        Remove obsolete error handler nsp32_reset.
 *        Some clean up.
 *   1.2: PowerPC (big endian) support.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>

#include <asm/dma.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>

#include "nsp32.h"


/***********************************************************************
 * Module parameters
 */
static int       trans_mode = 0;	/* default: BIOS */
module_param     (trans_mode, int, 0);
MODULE_PARM_DESC(trans_mode, "transfer mode (0: BIOS(default) 1: Async 2: Ultra20M");
#define ASYNC_MODE    1
#define ULTRA20M_MODE 2

static bool      auto_param = 0;	/* default: ON */
module_param     (auto_param, bool, 0);
MODULE_PARM_DESC(auto_param, "AutoParameter mode (0: ON(default) 1: OFF)");

static bool      disc_priv  = 1;	/* default: OFF */
module_param     (disc_priv, bool, 0);
MODULE_PARM_DESC(disc_priv,  "disconnection privilege mode (0: ON 1: OFF(default))");

MODULE_AUTHOR("YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>, GOTO Masanori <gotom@debian.or.jp>");
MODULE_DESCRIPTION("Workbit NinjaSCSI-32Bi/UDE CardBus/PCI SCSI host bus adapter module");
MODULE_LICENSE("GPL");

static const char *nsp32_release_version = "1.2";


/****************************************************************************
 * Supported hardware
 */
static struct pci_device_id nsp32_pci_table[] = {
	{
		.vendor      = PCI_VENDOR_ID_IODATA,
		.device      = PCI_DEVICE_ID_NINJASCSI_32BI_CBSC_II,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_IODATA,
	},
	{
		.vendor      = PCI_VENDOR_ID_WORKBIT,
		.device      = PCI_DEVICE_ID_NINJASCSI_32BI_KME,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_KME,
	},
	{
		.vendor      = PCI_VENDOR_ID_WORKBIT,
		.device      = PCI_DEVICE_ID_NINJASCSI_32BI_WBT,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_WORKBIT,
	},
	{
		.vendor      = PCI_VENDOR_ID_WORKBIT,
		.device      = PCI_DEVICE_ID_WORKBIT_STANDARD,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_PCI_WORKBIT,
	},
	{
		.vendor      = PCI_VENDOR_ID_WORKBIT,
		.device      = PCI_DEVICE_ID_NINJASCSI_32BI_LOGITEC,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_LOGITEC,
	},
	{
		.vendor      = PCI_VENDOR_ID_WORKBIT,
		.device      = PCI_DEVICE_ID_NINJASCSI_32BIB_LOGITEC,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_PCI_LOGITEC,
	},
	{
		.vendor      = PCI_VENDOR_ID_WORKBIT,
		.device      = PCI_DEVICE_ID_NINJASCSI_32UDE_MELCO,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_PCI_MELCO,
	},
	{
		.vendor      = PCI_VENDOR_ID_WORKBIT,
		.device      = PCI_DEVICE_ID_NINJASCSI_32UDE_MELCO_II,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = MODEL_PCI_MELCO,
	},
	{0,0,},
};
MODULE_DEVICE_TABLE(pci, nsp32_pci_table);

static nsp32_hw_data nsp32_data_base;  /* probe <-> detect glue */


/*
 * Period/AckWidth speed conversion table
 *
 * Note: This period/ackwidth speed table must be in descending order.
 */
static nsp32_sync_table nsp32_sync_table_40M[] = {
     /* {PNo, AW,   SP,   EP, SREQ smpl}  Speed(MB/s) Period AckWidth */
	{0x1,  0, 0x0c, 0x0c, SMPL_40M},  /*  20.0 :  50ns,  25ns */
	{0x2,  0, 0x0d, 0x18, SMPL_40M},  /*  13.3 :  75ns,  25ns */
	{0x3,  1, 0x19, 0x19, SMPL_40M},  /*  10.0 : 100ns,  50ns */
	{0x4,  1, 0x1a, 0x1f, SMPL_20M},  /*   8.0 : 125ns,  50ns */
	{0x5,  2, 0x20, 0x25, SMPL_20M},  /*   6.7 : 150ns,  75ns */
	{0x6,  2, 0x26, 0x31, SMPL_20M},  /*   5.7 : 175ns,  75ns */
	{0x7,  3, 0x32, 0x32, SMPL_20M},  /*   5.0 : 200ns, 100ns */
	{0x8,  3, 0x33, 0x38, SMPL_10M},  /*   4.4 : 225ns, 100ns */
	{0x9,  3, 0x39, 0x3e, SMPL_10M},  /*   4.0 : 250ns, 100ns */
};

static nsp32_sync_table nsp32_sync_table_20M[] = {
	{0x1,  0, 0x19, 0x19, SMPL_40M},  /* 10.0 : 100ns,  50ns */
	{0x2,  0, 0x1a, 0x25, SMPL_20M},  /*  6.7 : 150ns,  50ns */
	{0x3,  1, 0x26, 0x32, SMPL_20M},  /*  5.0 : 200ns, 100ns */
	{0x4,  1, 0x33, 0x3e, SMPL_10M},  /*  4.0 : 250ns, 100ns */
	{0x5,  2, 0x3f, 0x4b, SMPL_10M},  /*  3.3 : 300ns, 150ns */
	{0x6,  2, 0x4c, 0x57, SMPL_10M},  /*  2.8 : 350ns, 150ns */
	{0x7,  3, 0x58, 0x64, SMPL_10M},  /*  2.5 : 400ns, 200ns */
	{0x8,  3, 0x65, 0x70, SMPL_10M},  /*  2.2 : 450ns, 200ns */
	{0x9,  3, 0x71, 0x7d, SMPL_10M},  /*  2.0 : 500ns, 200ns */
};

static nsp32_sync_table nsp32_sync_table_pci[] = {
	{0x1,  0, 0x0c, 0x0f, SMPL_40M},  /* 16.6 :  60ns,  30ns */
	{0x2,  0, 0x10, 0x16, SMPL_40M},  /* 11.1 :  90ns,  30ns */
	{0x3,  1, 0x17, 0x1e, SMPL_20M},  /*  8.3 : 120ns,  60ns */
	{0x4,  1, 0x1f, 0x25, SMPL_20M},  /*  6.7 : 150ns,  60ns */
	{0x5,  2, 0x26, 0x2d, SMPL_20M},  /*  5.6 : 180ns,  90ns */
	{0x6,  2, 0x2e, 0x34, SMPL_10M},  /*  4.8 : 210ns,  90ns */
	{0x7,  3, 0x35, 0x3c, SMPL_10M},  /*  4.2 : 240ns, 120ns */
	{0x8,  3, 0x3d, 0x43, SMPL_10M},  /*  3.7 : 270ns, 120ns */
	{0x9,  3, 0x44, 0x4b, SMPL_10M},  /*  3.3 : 300ns, 120ns */
};

/*
 * function declaration
 */
/* module entry point */
static int         nsp32_probe (struct pci_dev *, const struct pci_device_id *);
static void        nsp32_remove(struct pci_dev *);
static int  __init init_nsp32  (void);
static void __exit exit_nsp32  (void);

/* struct struct scsi_host_template */
static int         nsp32_show_info   (struct seq_file *, struct Scsi_Host *);

static int         nsp32_detect      (struct pci_dev *pdev);
static int         nsp32_queuecommand(struct Scsi_Host *, struct scsi_cmnd *);
static const char *nsp32_info        (struct Scsi_Host *);
static int         nsp32_release     (struct Scsi_Host *);

/* SCSI error handler */
static int         nsp32_eh_abort     (struct scsi_cmnd *);
static int         nsp32_eh_host_reset(struct scsi_cmnd *);

/* generate SCSI message */
static void nsp32_build_identify(struct scsi_cmnd *);
static void nsp32_build_nop     (struct scsi_cmnd *);
static void nsp32_build_reject  (struct scsi_cmnd *);
static void nsp32_build_sdtr    (struct scsi_cmnd *, unsigned char, unsigned char);

/* SCSI message handler */
static int  nsp32_busfree_occur(struct scsi_cmnd *, unsigned short);
static void nsp32_msgout_occur (struct scsi_cmnd *);
static void nsp32_msgin_occur  (struct scsi_cmnd *, unsigned long, unsigned short);

static int  nsp32_setup_sg_table    (struct scsi_cmnd *);
static int  nsp32_selection_autopara(struct scsi_cmnd *);
static int  nsp32_selection_autoscsi(struct scsi_cmnd *);
static void nsp32_scsi_done         (struct scsi_cmnd *);
static int  nsp32_arbitration       (struct scsi_cmnd *, unsigned int);
static int  nsp32_reselection       (struct scsi_cmnd *, unsigned char);
static void nsp32_adjust_busfree    (struct scsi_cmnd *, unsigned int);
static void nsp32_restart_autoscsi  (struct scsi_cmnd *, unsigned short);

/* SCSI SDTR */
static void nsp32_analyze_sdtr       (struct scsi_cmnd *);
static int  nsp32_search_period_entry(nsp32_hw_data *, nsp32_target *, unsigned char);
static void nsp32_set_async          (nsp32_hw_data *, nsp32_target *);
static void nsp32_set_max_sync       (nsp32_hw_data *, nsp32_target *, unsigned char *, unsigned char *);
static void nsp32_set_sync_entry     (nsp32_hw_data *, nsp32_target *, int, unsigned char);

/* SCSI bus status handler */
static void nsp32_wait_req    (nsp32_hw_data *, int);
static void nsp32_wait_sack   (nsp32_hw_data *, int);
static void nsp32_sack_assert (nsp32_hw_data *);
static void nsp32_sack_negate (nsp32_hw_data *);
static void nsp32_do_bus_reset(nsp32_hw_data *);

/* hardware interrupt handler */
static irqreturn_t do_nsp32_isr(int, void *);

/* initialize hardware */
static int  nsp32hw_init(nsp32_hw_data *);

/* EEPROM handler */
static        int  nsp32_getprom_param (nsp32_hw_data *);
static        int  nsp32_getprom_at24  (nsp32_hw_data *);
static        int  nsp32_getprom_c16   (nsp32_hw_data *);
static        void nsp32_prom_start    (nsp32_hw_data *);
static        void nsp32_prom_stop     (nsp32_hw_data *);
static        int  nsp32_prom_read     (nsp32_hw_data *, int);
static        int  nsp32_prom_read_bit (nsp32_hw_data *);
static        void nsp32_prom_write_bit(nsp32_hw_data *, int);
static        void nsp32_prom_set      (nsp32_hw_data *, int, int);
static        int  nsp32_prom_get      (nsp32_hw_data *, int);

/* debug/warning/info message */
static void nsp32_message (const char *, int, char *, char *, ...);
#ifdef NSP32_DEBUG
static void nsp32_dmessage(const char *, int, int,    char *, ...);
#endif

/*
 * max_sectors is currently limited up to 128.
 */
static struct scsi_host_template nsp32_template = {
	.proc_name			= "nsp32",
	.name				= "Workbit NinjaSCSI-32Bi/UDE",
	.show_info			= nsp32_show_info,
	.info				= nsp32_info,
	.queuecommand			= nsp32_queuecommand,
	.can_queue			= 1,
	.sg_tablesize			= NSP32_SG_SIZE,
	.max_sectors			= 128,
	.this_id			= NSP32_HOST_SCSIID,
	.dma_boundary			= PAGE_SIZE - 1,
	.eh_abort_handler		= nsp32_eh_abort,
	.eh_host_reset_handler		= nsp32_eh_host_reset,
/*	.highmem_io			= 1, */
};

#include "nsp32_io.h"

/***********************************************************************
 * debug, error print
 */
#ifndef NSP32_DEBUG
# define NSP32_DEBUG_MASK	      0x000000
# define nsp32_msg(type, args...)     nsp32_message ("", 0, (type), args)
# define nsp32_dbg(mask, args...)     /* */
#else
# define NSP32_DEBUG_MASK	      0xffffff
# define nsp32_msg(type, args...) \
	nsp32_message (__func__, __LINE__, (type), args)
# define nsp32_dbg(mask, args...) \
	nsp32_dmessage(__func__, __LINE__, (mask), args)
#endif

#define NSP32_DEBUG_QUEUECOMMAND	BIT(0)
#define NSP32_DEBUG_REGISTER		BIT(1)
#define NSP32_DEBUG_AUTOSCSI		BIT(2)
#define NSP32_DEBUG_INTR		BIT(3)
#define NSP32_DEBUG_SGLIST		BIT(4)
#define NSP32_DEBUG_BUSFREE		BIT(5)
#define NSP32_DEBUG_CDB_CONTENTS	BIT(6)
#define NSP32_DEBUG_RESELECTION		BIT(7)
#define NSP32_DEBUG_MSGINOCCUR		BIT(8)
#define NSP32_DEBUG_EEPROM		BIT(9)
#define NSP32_DEBUG_MSGOUTOCCUR		BIT(10)
#define NSP32_DEBUG_BUSRESET		BIT(11)
#define NSP32_DEBUG_RESTART		BIT(12)
#define NSP32_DEBUG_SYNC		BIT(13)
#define NSP32_DEBUG_WAIT		BIT(14)
#define NSP32_DEBUG_TARGETFLAG		BIT(15)
#define NSP32_DEBUG_PROC		BIT(16)
#define NSP32_DEBUG_INIT		BIT(17)
#define NSP32_SPECIAL_PRINT_REGISTER	BIT(20)

#define NSP32_DEBUG_BUF_LEN		100

static void nsp32_message(const char *func, int line, char *type, char *fmt, ...)
{
	va_list args;
	char buf[NSP32_DEBUG_BUF_LEN];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

#ifndef NSP32_DEBUG
	printk("%snsp32: %s\n", type, buf);
#else
	printk("%snsp32: %s (%d): %s\n", type, func, line, buf);
#endif
}

#ifdef NSP32_DEBUG
static void nsp32_dmessage(const char *func, int line, int mask, char *fmt, ...)
{
	va_list args;
	char buf[NSP32_DEBUG_BUF_LEN];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (mask & NSP32_DEBUG_MASK) {
		printk("nsp32-debug: 0x%x %s (%d): %s\n", mask, func, line, buf);
	}
}
#endif

#ifdef NSP32_DEBUG
# include "nsp32_debug.c"
#else
# define show_command(arg)   /* */
# define show_busphase(arg)  /* */
# define show_autophase(arg) /* */
#endif

/*
 * IDENTIFY Message
 */
static void nsp32_build_identify(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	int pos             = data->msgout_len;
	int mode            = FALSE;

	/* XXX: Auto DiscPriv detection is progressing... */
	if (disc_priv == 0) {
		/* mode = TRUE; */
	}

	data->msgoutbuf[pos] = IDENTIFY(mode, SCpnt->device->lun); pos++;

	data->msgout_len = pos;
}

/*
 * SDTR Message Routine
 */
static void nsp32_build_sdtr(struct scsi_cmnd    *SCpnt,
			     unsigned char period,
			     unsigned char offset)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	int pos             = data->msgout_len;

	data->msgoutbuf[pos] = EXTENDED_MESSAGE;  pos++;
	data->msgoutbuf[pos] = EXTENDED_SDTR_LEN; pos++;
	data->msgoutbuf[pos] = EXTENDED_SDTR;     pos++;
	data->msgoutbuf[pos] = period;            pos++;
	data->msgoutbuf[pos] = offset;            pos++;

	data->msgout_len = pos;
}

/*
 * No Operation Message
 */
static void nsp32_build_nop(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	int            pos  = data->msgout_len;

	if (pos != 0) {
		nsp32_msg(KERN_WARNING,
			  "Some messages are already contained!");
		return;
	}

	data->msgoutbuf[pos] = NOP; pos++;
	data->msgout_len = pos;
}

/*
 * Reject Message
 */
static void nsp32_build_reject(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	int            pos  = data->msgout_len;

	data->msgoutbuf[pos] = MESSAGE_REJECT; pos++;
	data->msgout_len = pos;
}
	
/*
 * timer
 */
#if 0
static void nsp32_start_timer(struct scsi_cmnd *SCpnt, int time)
{
	unsigned int base = SCpnt->host->io_port;

	nsp32_dbg(NSP32_DEBUG_INTR, "timer=%d", time);

	if (time & (~TIMER_CNT_MASK)) {
		nsp32_dbg(NSP32_DEBUG_INTR, "timer set overflow");
	}

	nsp32_write2(base, TIMER_SET, time & TIMER_CNT_MASK);
}
#endif


/*
 * set SCSI command and other parameter to asic, and start selection phase
 */
static int nsp32_selection_autopara(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data  *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int	base    = SCpnt->device->host->io_port;
	unsigned int	host_id = SCpnt->device->host->this_id;
	unsigned char	target  = scmd_id(SCpnt);
	nsp32_autoparam *param  = data->autoparam;
	unsigned char	phase;
	int		i, ret;
	unsigned int	msgout;
	u16_le	        s;

	nsp32_dbg(NSP32_DEBUG_AUTOSCSI, "in");

	/*
	 * check bus free
	 */
	phase = nsp32_read1(base, SCSI_BUS_MONITOR);
	if (phase != BUSMON_BUS_FREE) {
		nsp32_msg(KERN_WARNING, "bus busy");
		show_busphase(phase & BUSMON_PHASE_MASK);
		SCpnt->result = DID_BUS_BUSY << 16;
		return FALSE;
	}

	/*
	 * message out
	 *
	 * Note: If the range of msgout_len is 1 - 3, fill scsi_msgout.
	 *       over 3 messages needs another routine.
	 */
	if (data->msgout_len == 0) {
		nsp32_msg(KERN_ERR, "SCSI MsgOut without any message!");
		SCpnt->result = DID_ERROR << 16;
		return FALSE;
	} else if (data->msgout_len > 0 && data->msgout_len <= 3) {
		msgout = 0;
		for (i = 0; i < data->msgout_len; i++) {
			/*
			 * the sending order of the message is:
			 *  MCNT 3: MSG#0 -> MSG#1 -> MSG#2
			 *  MCNT 2:          MSG#1 -> MSG#2
			 *  MCNT 1:                   MSG#2    
			 */
			msgout >>= 8;
			msgout |= ((unsigned int)(data->msgoutbuf[i]) << 24);
		}
		msgout |= MV_VALID;	/* MV valid */
		msgout |= (unsigned int)data->msgout_len; /* len */
	} else {
		/* data->msgout_len > 3 */
		msgout = 0;
	}

	// nsp_dbg(NSP32_DEBUG_AUTOSCSI, "sel time out=0x%x\n", nsp32_read2(base, SEL_TIME_OUT));
	// nsp32_write2(base, SEL_TIME_OUT,   SEL_TIMEOUT_TIME);

	/*
	 * setup asic parameter
	 */
	memset(param, 0, sizeof(nsp32_autoparam));

	/* cdb */
	for (i = 0; i < SCpnt->cmd_len; i++) {
		param->cdb[4 * i] = SCpnt->cmnd[i];
	}

	/* outgoing messages */
	param->msgout = cpu_to_le32(msgout);

	/* syncreg, ackwidth, target id, SREQ sampling rate */
	param->syncreg    = data->cur_target->syncreg;
	param->ackwidth   = data->cur_target->ackwidth;
	param->target_id  = BIT(host_id) | BIT(target);
	param->sample_reg = data->cur_target->sample_reg;

	// nsp32_dbg(NSP32_DEBUG_AUTOSCSI, "sample rate=0x%x\n", data->cur_target->sample_reg);

	/* command control */
	param->command_control = cpu_to_le16(CLEAR_CDB_FIFO_POINTER |
					     AUTOSCSI_START         |
					     AUTO_MSGIN_00_OR_04    |
					     AUTO_MSGIN_02          |
					     AUTO_ATN               );


	/* transfer control */
	s = 0;
	switch (data->trans_method) {
	case NSP32_TRANSFER_BUSMASTER:
		s |= BM_START;
		break;
	case NSP32_TRANSFER_MMIO:
		s |= CB_MMIO_MODE;
		break;
	case NSP32_TRANSFER_PIO:
		s |= CB_IO_MODE;
		break;
	default:
		nsp32_msg(KERN_ERR, "unknown trans_method");
		break;
	}
	/*
	 * OR-ed BLIEND_MODE, FIFO intr is decreased, instead of PCI bus waits.
	 * For bus master transfer, it's taken off.
	 */
	s |= (TRANSFER_GO | ALL_COUNTER_CLR);
	param->transfer_control = cpu_to_le16(s);

	/* sg table addr */
	param->sgt_pointer = cpu_to_le32(data->cur_lunt->sglun_paddr);

	/*
	 * transfer parameter to ASIC
	 */
	nsp32_write4(base, SGT_ADR,         data->auto_paddr);
	nsp32_write2(base, COMMAND_CONTROL, CLEAR_CDB_FIFO_POINTER |
		                            AUTO_PARAMETER         );

	/*
	 * Check arbitration
	 */
	ret = nsp32_arbitration(SCpnt, base);

	return ret;
}


/*
 * Selection with AUTO SCSI (without AUTO PARAMETER)
 */
static int nsp32_selection_autoscsi(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data  *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int	base    = SCpnt->device->host->io_port;
	unsigned int	host_id = SCpnt->device->host->this_id;
	unsigned char	target  = scmd_id(SCpnt);
	unsigned char	phase;
	int		status;
	unsigned short	command	= 0;
	unsigned int	msgout  = 0;
	unsigned short	execph;
	int		i;

	nsp32_dbg(NSP32_DEBUG_AUTOSCSI, "in");

	/*
	 * IRQ disable
	 */
	nsp32_write2(base, IRQ_CONTROL, IRQ_CONTROL_ALL_IRQ_MASK);

	/*
	 * check bus line
	 */
	phase = nsp32_read1(base, SCSI_BUS_MONITOR);
	if ((phase & BUSMON_BSY) || (phase & BUSMON_SEL)) {
		nsp32_msg(KERN_WARNING, "bus busy");
		SCpnt->result = DID_BUS_BUSY << 16;
		status = 1;
		goto out;
        }

	/*
	 * clear execph
	 */
	execph = nsp32_read2(base, SCSI_EXECUTE_PHASE);

	/*
	 * clear FIFO counter to set CDBs
	 */
	nsp32_write2(base, COMMAND_CONTROL, CLEAR_CDB_FIFO_POINTER);

	/*
	 * set CDB0 - CDB15
	 */
	for (i = 0; i < SCpnt->cmd_len; i++) {
		nsp32_write1(base, COMMAND_DATA, SCpnt->cmnd[i]);
        }
	nsp32_dbg(NSP32_DEBUG_CDB_CONTENTS, "CDB[0]=[0x%x]", SCpnt->cmnd[0]);

	/*
	 * set SCSIOUT LATCH(initiator)/TARGET(target) (OR-ed) ID
	 */
	nsp32_write1(base, SCSI_OUT_LATCH_TARGET_ID, BIT(host_id) | BIT(target));

	/*
	 * set SCSI MSGOUT REG
	 *
	 * Note: If the range of msgout_len is 1 - 3, fill scsi_msgout.
	 *       over 3 messages needs another routine.
	 */
	if (data->msgout_len == 0) {
		nsp32_msg(KERN_ERR, "SCSI MsgOut without any message!");
		SCpnt->result = DID_ERROR << 16;
		status = 1;
		goto out;
	} else if (data->msgout_len > 0 && data->msgout_len <= 3) {
		msgout = 0;
		for (i = 0; i < data->msgout_len; i++) {
			/*
			 * the sending order of the message is:
			 *  MCNT 3: MSG#0 -> MSG#1 -> MSG#2
			 *  MCNT 2:          MSG#1 -> MSG#2
			 *  MCNT 1:                   MSG#2    
			 */
			msgout >>= 8;
			msgout |= ((unsigned int)(data->msgoutbuf[i]) << 24);
		}
		msgout |= MV_VALID;	/* MV valid */
		msgout |= (unsigned int)data->msgout_len; /* len */
		nsp32_write4(base, SCSI_MSG_OUT, msgout);
	} else {
		/* data->msgout_len > 3 */
		nsp32_write4(base, SCSI_MSG_OUT, 0);
	}

	/*
	 * set selection timeout(= 250ms)
	 */
	nsp32_write2(base, SEL_TIME_OUT,   SEL_TIMEOUT_TIME);

	/*
	 * set SREQ hazard killer sampling rate
	 * 
	 * TODO: sample_rate (BASE+0F) is 0 when internal clock = 40MHz.
	 *      check other internal clock!
	 */
	nsp32_write1(base, SREQ_SMPL_RATE, data->cur_target->sample_reg);

	/*
	 * clear Arbit
	 */
	nsp32_write1(base, SET_ARBIT,      ARBIT_CLEAR);

	/*
	 * set SYNCREG
	 * Don't set BM_START_ADR before setting this register.
	 */
	nsp32_write1(base, SYNC_REG,  data->cur_target->syncreg);

	/*
	 * set ACKWIDTH
	 */
	nsp32_write1(base, ACK_WIDTH, data->cur_target->ackwidth);

	nsp32_dbg(NSP32_DEBUG_AUTOSCSI,
		  "syncreg=0x%x, ackwidth=0x%x, sgtpaddr=0x%x, id=0x%x",
		  nsp32_read1(base, SYNC_REG), nsp32_read1(base, ACK_WIDTH),
		  nsp32_read4(base, SGT_ADR), nsp32_read1(base, SCSI_OUT_LATCH_TARGET_ID));
	nsp32_dbg(NSP32_DEBUG_AUTOSCSI, "msgout_len=%d, msgout=0x%x",
		  data->msgout_len, msgout);

	/*
	 * set SGT ADDR (physical address)
	 */
	nsp32_write4(base, SGT_ADR, data->cur_lunt->sglun_paddr);

	/*
	 * set TRANSFER CONTROL REG
	 */
	command = 0;
	command |= (TRANSFER_GO | ALL_COUNTER_CLR);
	if (data->trans_method & NSP32_TRANSFER_BUSMASTER) {
		if (scsi_bufflen(SCpnt) > 0) {
			command |= BM_START;
		}
	} else if (data->trans_method & NSP32_TRANSFER_MMIO) {
		command |= CB_MMIO_MODE;
	} else if (data->trans_method & NSP32_TRANSFER_PIO) {
		command |= CB_IO_MODE;
	}
	nsp32_write2(base, TRANSFER_CONTROL, command);

	/*
	 * start AUTO SCSI, kick off arbitration
	 */
	command = (CLEAR_CDB_FIFO_POINTER |
		   AUTOSCSI_START         |
		   AUTO_MSGIN_00_OR_04    |
		   AUTO_MSGIN_02          |
		   AUTO_ATN                );
	nsp32_write2(base, COMMAND_CONTROL, command);

	/*
	 * Check arbitration
	 */
	status = nsp32_arbitration(SCpnt, base);

 out:
	/*
	 * IRQ enable
	 */
	nsp32_write2(base, IRQ_CONTROL, 0);

	return status;
}


/*
 * Arbitration Status Check
 *	
 * Note: Arbitration counter is waited during ARBIT_GO is not lifting.
 *	 Using udelay(1) consumes CPU time and system time, but 
 *	 arbitration delay time is defined minimal 2.4us in SCSI
 *	 specification, thus udelay works as coarse grained wait timer.
 */
static int nsp32_arbitration(struct scsi_cmnd *SCpnt, unsigned int base)
{
	unsigned char arbit;
	int	      status = TRUE;
	int	      time   = 0;

	do {
		arbit = nsp32_read1(base, ARBIT_STATUS);
		time++;
	} while ((arbit & (ARBIT_WIN | ARBIT_FAIL)) == 0 &&
		 (time <= ARBIT_TIMEOUT_TIME));

	nsp32_dbg(NSP32_DEBUG_AUTOSCSI,
		  "arbit: 0x%x, delay time: %d", arbit, time);

	if (arbit & ARBIT_WIN) {
		/* Arbitration succeeded */
		SCpnt->result = DID_OK << 16;
		nsp32_index_write1(base, EXT_PORT, LED_ON); /* PCI LED on */
	} else if (arbit & ARBIT_FAIL) {
		/* Arbitration failed */
		SCpnt->result = DID_BUS_BUSY << 16;
		status = FALSE;
	} else {
		/*
		 * unknown error or ARBIT_GO timeout,
		 * something lock up! guess no connection.
		 */
		nsp32_dbg(NSP32_DEBUG_AUTOSCSI, "arbit timeout");
		SCpnt->result = DID_NO_CONNECT << 16;
		status = FALSE;
        }

	/*
	 * clear Arbit
	 */
	nsp32_write1(base, SET_ARBIT, ARBIT_CLEAR);

	return status;
}


/*
 * reselection
 *
 * Note: This reselection routine is called from msgin_occur,
 *	 reselection target id&lun must be already set.
 *	 SCSI-2 says IDENTIFY implies RESTORE_POINTER operation.
 */
static int nsp32_reselection(struct scsi_cmnd *SCpnt, unsigned char newlun)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int   host_id = SCpnt->device->host->this_id;
	unsigned int   base    = SCpnt->device->host->io_port;
	unsigned char  tmpid, newid;

	nsp32_dbg(NSP32_DEBUG_RESELECTION, "enter");

	/*
	 * calculate reselected SCSI ID
	 */
	tmpid = nsp32_read1(base, RESELECT_ID);
	tmpid &= (~BIT(host_id));
	newid = 0;
	while (tmpid) {
		if (tmpid & 1) {
			break;
		}
		tmpid >>= 1;
		newid++;
	}

	/*
	 * If reselected New ID:LUN is not existed
	 * or current nexus is not existed, unexpected
	 * reselection is occurred. Send reject message.
	 */
	if (newid >= ARRAY_SIZE(data->lunt) || newlun >= ARRAY_SIZE(data->lunt[0])) {
		nsp32_msg(KERN_WARNING, "unknown id/lun");
		return FALSE;
	} else if(data->lunt[newid][newlun].SCpnt == NULL) {
		nsp32_msg(KERN_WARNING, "no SCSI command is processing");
		return FALSE;
	}

	data->cur_id    = newid;
	data->cur_lun   = newlun;
	data->cur_target = &(data->target[newid]);
	data->cur_lunt   = &(data->lunt[newid][newlun]);

	/* reset SACK/SavedACK counter (or ALL clear?) */
	nsp32_write4(base, CLR_COUNTER, CLRCOUNTER_ALLMASK);

	return TRUE;
}


/*
 * nsp32_setup_sg_table - build scatter gather list for transfer data
 *			    with bus master.
 *
 * Note: NinjaSCSI-32Bi/UDE bus master can not transfer over 64KB at a time.
 */
static int nsp32_setup_sg_table(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	struct scatterlist *sg;
	nsp32_sgtable *sgt = data->cur_lunt->sglun->sgt;
	int num, i;
	u32_le l;

	if (sgt == NULL) {
		nsp32_dbg(NSP32_DEBUG_SGLIST, "SGT == null");
		return FALSE;
	}

	num = scsi_dma_map(SCpnt);
	if (!num)
		return TRUE;
	else if (num < 0)
		return FALSE;
	else {
		scsi_for_each_sg(SCpnt, sg, num, i) {
			/*
			 * Build nsp32_sglist, substitute sg dma addresses.
			 */
			sgt[i].addr = cpu_to_le32(sg_dma_address(sg));
			sgt[i].len  = cpu_to_le32(sg_dma_len(sg));

			if (le32_to_cpu(sgt[i].len) > 0x10000) {
				nsp32_msg(KERN_ERR,
					"can't transfer over 64KB at a time, size=0x%lx", le32_to_cpu(sgt[i].len));
				return FALSE;
			}
			nsp32_dbg(NSP32_DEBUG_SGLIST,
				  "num 0x%x : addr 0x%lx len 0x%lx",
				  i,
				  le32_to_cpu(sgt[i].addr),
				  le32_to_cpu(sgt[i].len ));
		}

		/* set end mark */
		l = le32_to_cpu(sgt[num-1].len);
		sgt[num-1].len = cpu_to_le32(l | SGTEND);
	}

	return TRUE;
}

static int nsp32_queuecommand_lck(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *))
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	nsp32_target *target;
	nsp32_lunt   *cur_lunt;
	int ret;

	nsp32_dbg(NSP32_DEBUG_QUEUECOMMAND,
		  "enter. target: 0x%x LUN: 0x%llx cmnd: 0x%x cmndlen: 0x%x "
		  "use_sg: 0x%x reqbuf: 0x%lx reqlen: 0x%x",
		  SCpnt->device->id, SCpnt->device->lun, SCpnt->cmnd[0], SCpnt->cmd_len,
		  scsi_sg_count(SCpnt), scsi_sglist(SCpnt), scsi_bufflen(SCpnt));

	if (data->CurrentSC != NULL) {
		nsp32_msg(KERN_ERR, "Currentsc != NULL. Cancel this command request");
		data->CurrentSC = NULL;
		SCpnt->result   = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
	}

	/* check target ID is not same as this initiator ID */
	if (scmd_id(SCpnt) == SCpnt->device->host->this_id) {
		nsp32_dbg(NSP32_DEBUG_QUEUECOMMAND, "target==host???");
		SCpnt->result = DID_BAD_TARGET << 16;
		done(SCpnt);
		return 0;
	}

	/* check target LUN is allowable value */
	if (SCpnt->device->lun >= MAX_LUN) {
		nsp32_dbg(NSP32_DEBUG_QUEUECOMMAND, "no more lun");
		SCpnt->result = DID_BAD_TARGET << 16;
		done(SCpnt);
		return 0;
	}

	show_command(SCpnt);

	SCpnt->scsi_done     = done;
	data->CurrentSC      = SCpnt;
	SCpnt->SCp.Status    = CHECK_CONDITION;
	SCpnt->SCp.Message   = 0;
	scsi_set_resid(SCpnt, scsi_bufflen(SCpnt));

	SCpnt->SCp.ptr		    = (char *)scsi_sglist(SCpnt);
	SCpnt->SCp.this_residual    = scsi_bufflen(SCpnt);
	SCpnt->SCp.buffer	    = NULL;
	SCpnt->SCp.buffers_residual = 0;

	/* initialize data */
	data->msgout_len	= 0;
	data->msgin_len		= 0;
	cur_lunt		= &(data->lunt[SCpnt->device->id][SCpnt->device->lun]);
	cur_lunt->SCpnt		= SCpnt;
	cur_lunt->save_datp	= 0;
	cur_lunt->msgin03	= FALSE;
	data->cur_lunt		= cur_lunt;
	data->cur_id		= SCpnt->device->id;
	data->cur_lun		= SCpnt->device->lun;

	ret = nsp32_setup_sg_table(SCpnt);
	if (ret == FALSE) {
		nsp32_msg(KERN_ERR, "SGT fail");
		SCpnt->result = DID_ERROR << 16;
		nsp32_scsi_done(SCpnt);
		return 0;
	}

	/* Build IDENTIFY */
	nsp32_build_identify(SCpnt);

	/* 
	 * If target is the first time to transfer after the reset
	 * (target don't have SDTR_DONE and SDTR_INITIATOR), sync
	 * message SDTR is needed to do synchronous transfer.
	 */
	target = &data->target[scmd_id(SCpnt)];
	data->cur_target = target;

	if (!(target->sync_flag & (SDTR_DONE | SDTR_INITIATOR | SDTR_TARGET))) {
		unsigned char period, offset;

		if (trans_mode != ASYNC_MODE) {
			nsp32_set_max_sync(data, target, &period, &offset);
			nsp32_build_sdtr(SCpnt, period, offset);
			target->sync_flag |= SDTR_INITIATOR;
		} else {
			nsp32_set_async(data, target);
			target->sync_flag |= SDTR_DONE;
		}

		nsp32_dbg(NSP32_DEBUG_QUEUECOMMAND,
			  "SDTR: entry: %d start_period: 0x%x offset: 0x%x\n",
			  target->limit_entry, period, offset);
	} else if (target->sync_flag & SDTR_INITIATOR) {
		/*
		 * It was negotiating SDTR with target, sending from the
		 * initiator, but there are no chance to remove this flag.
		 * Set async because we don't get proper negotiation.
		 */
		nsp32_set_async(data, target);
		target->sync_flag &= ~SDTR_INITIATOR;
		target->sync_flag |= SDTR_DONE;

		nsp32_dbg(NSP32_DEBUG_QUEUECOMMAND,
			  "SDTR_INITIATOR: fall back to async");
	} else if (target->sync_flag & SDTR_TARGET) {
		/*
		 * It was negotiating SDTR with target, sending from target,
		 * but there are no chance to remove this flag.  Set async
		 * because we don't get proper negotiation.
		 */
		nsp32_set_async(data, target);
		target->sync_flag &= ~SDTR_TARGET;
		target->sync_flag |= SDTR_DONE;

		nsp32_dbg(NSP32_DEBUG_QUEUECOMMAND,
			  "Unknown SDTR from target is reached, fall back to async.");
	}

	nsp32_dbg(NSP32_DEBUG_TARGETFLAG,
		  "target: %d sync_flag: 0x%x syncreg: 0x%x ackwidth: 0x%x",
		  SCpnt->device->id, target->sync_flag, target->syncreg,
		  target->ackwidth);

	/* Selection */
	if (auto_param == 0) {
		ret = nsp32_selection_autopara(SCpnt);
	} else {
		ret = nsp32_selection_autoscsi(SCpnt);
	}

	if (ret != TRUE) {
		nsp32_dbg(NSP32_DEBUG_QUEUECOMMAND, "selection fail");
		nsp32_scsi_done(SCpnt);
	}

	return 0;
}

static DEF_SCSI_QCMD(nsp32_queuecommand)

/* initialize asic */
static int nsp32hw_init(nsp32_hw_data *data)
{
	unsigned int   base = data->BaseAddress;
	unsigned short irq_stat;
	unsigned long  lc_reg;
	unsigned char  power;

	lc_reg = nsp32_index_read4(base, CFG_LATE_CACHE);
	if ((lc_reg & 0xff00) == 0) {
		lc_reg |= (0x20 << 8);
		nsp32_index_write2(base, CFG_LATE_CACHE, lc_reg & 0xffff);
	}

	nsp32_write2(base, IRQ_CONTROL,        IRQ_CONTROL_ALL_IRQ_MASK);
	nsp32_write2(base, TRANSFER_CONTROL,   0);
	nsp32_write4(base, BM_CNT,             0);
	nsp32_write2(base, SCSI_EXECUTE_PHASE, 0);

	do {
		irq_stat = nsp32_read2(base, IRQ_STATUS);
		nsp32_dbg(NSP32_DEBUG_INIT, "irq_stat 0x%x", irq_stat);
	} while (irq_stat & IRQSTATUS_ANY_IRQ);

	/*
	 * Fill FIFO_FULL_SHLD, FIFO_EMPTY_SHLD. Below parameter is
	 *  designated by specification.
	 */
	if ((data->trans_method & NSP32_TRANSFER_PIO) ||
	    (data->trans_method & NSP32_TRANSFER_MMIO)) {
		nsp32_index_write1(base, FIFO_FULL_SHLD_COUNT,  0x40);
		nsp32_index_write1(base, FIFO_EMPTY_SHLD_COUNT, 0x40);
	} else if (data->trans_method & NSP32_TRANSFER_BUSMASTER) {
		nsp32_index_write1(base, FIFO_FULL_SHLD_COUNT,  0x10);
		nsp32_index_write1(base, FIFO_EMPTY_SHLD_COUNT, 0x60);
	} else {
		nsp32_dbg(NSP32_DEBUG_INIT, "unknown transfer mode");
	}

	nsp32_dbg(NSP32_DEBUG_INIT, "full 0x%x emp 0x%x",
		  nsp32_index_read1(base, FIFO_FULL_SHLD_COUNT),
		  nsp32_index_read1(base, FIFO_EMPTY_SHLD_COUNT));

	nsp32_index_write1(base, CLOCK_DIV, data->clock);
	nsp32_index_write1(base, BM_CYCLE,  MEMRD_CMD1 | SGT_AUTO_PARA_MEMED_CMD);
	nsp32_write1(base, PARITY_CONTROL, 0);	/* parity check is disable */

	/*
	 * initialize MISC_WRRD register
	 * 
	 * Note: Designated parameters is obeyed as following:
	 *	MISC_SCSI_DIRECTION_DETECTOR_SELECT: It must be set.
	 *	MISC_MASTER_TERMINATION_SELECT:      It must be set.
	 *	MISC_BMREQ_NEGATE_TIMING_SEL:	     It should be set.
	 *	MISC_AUTOSEL_TIMING_SEL:	     It should be set.
	 *	MISC_BMSTOP_CHANGE2_NONDATA_PHASE:   It should be set.
	 *	MISC_DELAYED_BMSTART:		     It's selected for safety.
	 *
	 * Note: If MISC_BMSTOP_CHANGE2_NONDATA_PHASE is set, then
	 *	we have to set TRANSFERCONTROL_BM_START as 0 and set
	 *	appropriate value before restarting bus master transfer.
	 */
	nsp32_index_write2(base, MISC_WR,
			   (SCSI_DIRECTION_DETECTOR_SELECT |
			    DELAYED_BMSTART                |
			    MASTER_TERMINATION_SELECT      |
			    BMREQ_NEGATE_TIMING_SEL        |
			    AUTOSEL_TIMING_SEL             |
			    BMSTOP_CHANGE2_NONDATA_PHASE));

	nsp32_index_write1(base, TERM_PWR_CONTROL, 0);
	power = nsp32_index_read1(base, TERM_PWR_CONTROL);
	if (!(power & SENSE)) {
		nsp32_msg(KERN_INFO, "term power on");
		nsp32_index_write1(base, TERM_PWR_CONTROL, BPWR);
	}

	nsp32_write2(base, TIMER_SET, TIMER_STOP);
	nsp32_write2(base, TIMER_SET, TIMER_STOP); /* Required 2 times */

	nsp32_write1(base, SYNC_REG,     0);
	nsp32_write1(base, ACK_WIDTH,    0);
	nsp32_write2(base, SEL_TIME_OUT, SEL_TIMEOUT_TIME);

	/*
	 * enable to select designated IRQ (except for
	 * IRQSELECT_SERR, IRQSELECT_PERR, IRQSELECT_BMCNTERR)
	 */
	nsp32_index_write2(base, IRQ_SELECT, IRQSELECT_TIMER_IRQ         |
			                     IRQSELECT_SCSIRESET_IRQ     |
			                     IRQSELECT_FIFO_SHLD_IRQ     |
			                     IRQSELECT_RESELECT_IRQ      |
			                     IRQSELECT_PHASE_CHANGE_IRQ  |
			                     IRQSELECT_AUTO_SCSI_SEQ_IRQ |
			                  //   IRQSELECT_BMCNTERR_IRQ      |
			                     IRQSELECT_TARGET_ABORT_IRQ  |
			                     IRQSELECT_MASTER_ABORT_IRQ );
	nsp32_write2(base, IRQ_CONTROL, 0);

	/* PCI LED off */
	nsp32_index_write1(base, EXT_PORT_DDR, LED_OFF);
	nsp32_index_write1(base, EXT_PORT,     LED_OFF);

	return TRUE;
}


/* interrupt routine */
static irqreturn_t do_nsp32_isr(int irq, void *dev_id)
{
	nsp32_hw_data *data = dev_id;
	unsigned int base = data->BaseAddress;
	struct scsi_cmnd *SCpnt = data->CurrentSC;
	unsigned short auto_stat, irq_stat, trans_stat;
	unsigned char busmon, busphase;
	unsigned long flags;
	int ret;
	int handled = 0;
	struct Scsi_Host *host = data->Host;

	spin_lock_irqsave(host->host_lock, flags);

	/*
	 * IRQ check, then enable IRQ mask
	 */
	irq_stat = nsp32_read2(base, IRQ_STATUS);
	nsp32_dbg(NSP32_DEBUG_INTR, 
		  "enter IRQ: %d, IRQstatus: 0x%x", irq, irq_stat);
	/* is this interrupt comes from Ninja asic? */
	if ((irq_stat & IRQSTATUS_ANY_IRQ) == 0) {
		nsp32_dbg(NSP32_DEBUG_INTR, "shared interrupt: irq other 0x%x", irq_stat);
		goto out2;
	}
	handled = 1;
	nsp32_write2(base, IRQ_CONTROL, IRQ_CONTROL_ALL_IRQ_MASK);

	busmon = nsp32_read1(base, SCSI_BUS_MONITOR);
	busphase = busmon & BUSMON_PHASE_MASK;

	trans_stat = nsp32_read2(base, TRANSFER_STATUS);
	if ((irq_stat == 0xffff) && (trans_stat == 0xffff)) {
		nsp32_msg(KERN_INFO, "card disconnect");
		if (data->CurrentSC != NULL) {
			nsp32_msg(KERN_INFO, "clean up current SCSI command");
			SCpnt->result = DID_BAD_TARGET << 16;
			nsp32_scsi_done(SCpnt);
		}
		goto out;
	}

	/* Timer IRQ */
	if (irq_stat & IRQSTATUS_TIMER_IRQ) {
		nsp32_dbg(NSP32_DEBUG_INTR, "timer stop");
		nsp32_write2(base, TIMER_SET, TIMER_STOP);
		goto out;
	}

	/* SCSI reset */
	if (irq_stat & IRQSTATUS_SCSIRESET_IRQ) {
		nsp32_msg(KERN_INFO, "detected someone do bus reset");
		nsp32_do_bus_reset(data);
		if (SCpnt != NULL) {
			SCpnt->result = DID_RESET << 16;
			nsp32_scsi_done(SCpnt);
		}
		goto out;
	}

	if (SCpnt == NULL) {
		nsp32_msg(KERN_WARNING, "SCpnt==NULL this can't be happened");
		nsp32_msg(KERN_WARNING, "irq_stat=0x%x trans_stat=0x%x", irq_stat, trans_stat);
		goto out;
	}

	/*
	 * AutoSCSI Interrupt.
	 * Note: This interrupt is occurred when AutoSCSI is finished.  Then
	 * check SCSIEXECUTEPHASE, and do appropriate action.  Each phases are
	 * recorded when AutoSCSI sequencer has been processed.
	 */
	if(irq_stat & IRQSTATUS_AUTOSCSI_IRQ) {
		/* getting SCSI executed phase */
		auto_stat = nsp32_read2(base, SCSI_EXECUTE_PHASE);
		nsp32_write2(base, SCSI_EXECUTE_PHASE, 0);

		/* Selection Timeout, go busfree phase. */
		if (auto_stat & SELECTION_TIMEOUT) {
			nsp32_dbg(NSP32_DEBUG_INTR,
				  "selection timeout occurred");

			SCpnt->result = DID_TIME_OUT << 16;
			nsp32_scsi_done(SCpnt);
			goto out;
		}

		if (auto_stat & MSGOUT_PHASE) {
			/*
			 * MsgOut phase was processed.
			 * If MSG_IN_OCCUER is not set, then MsgOut phase is
			 * completed. Thus, msgout_len must reset.  Otherwise,
			 * nothing to do here. If MSG_OUT_OCCUER is occurred,
			 * then we will encounter the condition and check.
			 */
			if (!(auto_stat & MSG_IN_OCCUER) &&
			     (data->msgout_len <= 3)) {
				/*
				 * !MSG_IN_OCCUER && msgout_len <=3
				 *   ---> AutoSCSI with MSGOUTreg is processed.
				 */
				data->msgout_len = 0;
			}

			nsp32_dbg(NSP32_DEBUG_INTR, "MsgOut phase processed");
		}

		if ((auto_stat & DATA_IN_PHASE) &&
		    (scsi_get_resid(SCpnt) > 0) &&
		    ((nsp32_read2(base, FIFO_REST_CNT) & FIFO_REST_MASK) != 0)) {
			printk( "auto+fifo\n");
			//nsp32_pio_read(SCpnt);
		}

		if (auto_stat & (DATA_IN_PHASE | DATA_OUT_PHASE)) {
			/* DATA_IN_PHASE/DATA_OUT_PHASE was processed. */
			nsp32_dbg(NSP32_DEBUG_INTR,
				  "Data in/out phase processed");

			/* read BMCNT, SGT pointer addr */
			nsp32_dbg(NSP32_DEBUG_INTR, "BMCNT=0x%lx", 
				    nsp32_read4(base, BM_CNT));
			nsp32_dbg(NSP32_DEBUG_INTR, "addr=0x%lx", 
				    nsp32_read4(base, SGT_ADR));
			nsp32_dbg(NSP32_DEBUG_INTR, "SACK=0x%lx", 
				    nsp32_read4(base, SACK_CNT));
			nsp32_dbg(NSP32_DEBUG_INTR, "SSACK=0x%lx", 
				    nsp32_read4(base, SAVED_SACK_CNT));

			scsi_set_resid(SCpnt, 0); /* all data transferred! */
		}

		/*
		 * MsgIn Occur
		 */
		if (auto_stat & MSG_IN_OCCUER) {
			nsp32_msgin_occur(SCpnt, irq_stat, auto_stat);
		}

		/*
		 * MsgOut Occur
		 */
		if (auto_stat & MSG_OUT_OCCUER) {
			nsp32_msgout_occur(SCpnt);
		}

		/*
		 * Bus Free Occur
		 */
		if (auto_stat & BUS_FREE_OCCUER) {
			ret = nsp32_busfree_occur(SCpnt, auto_stat);
			if (ret == TRUE) {
				goto out;
			}
		}

		if (auto_stat & STATUS_PHASE) {
			/*
			 * Read CSB and substitute CSB for SCpnt->result
			 * to save status phase stutas byte.
			 * scsi error handler checks host_byte (DID_*:
			 * low level driver to indicate status), then checks 
			 * status_byte (SCSI status byte).
			 */
			SCpnt->result =	(int)nsp32_read1(base, SCSI_CSB_IN);
		}

		if (auto_stat & ILLEGAL_PHASE) {
			/* Illegal phase is detected. SACK is not back. */
			nsp32_msg(KERN_WARNING, 
				  "AUTO SCSI ILLEGAL PHASE OCCUR!!!!");

			/* TODO: currently we don't have any action... bus reset? */

			/*
			 * To send back SACK, assert, wait, and negate.
			 */
			nsp32_sack_assert(data);
			nsp32_wait_req(data, NEGATE);
			nsp32_sack_negate(data);

		}

		if (auto_stat & COMMAND_PHASE) {
			/* nothing to do */
			nsp32_dbg(NSP32_DEBUG_INTR, "Command phase processed");
		}

		if (auto_stat & AUTOSCSI_BUSY) {
			/* AutoSCSI is running */
		}

		show_autophase(auto_stat);
	}

	/* FIFO_SHLD_IRQ */
	if (irq_stat & IRQSTATUS_FIFO_SHLD_IRQ) {
		nsp32_dbg(NSP32_DEBUG_INTR, "FIFO IRQ");

		switch(busphase) {
		case BUSPHASE_DATA_OUT:
			nsp32_dbg(NSP32_DEBUG_INTR, "fifo/write");

			//nsp32_pio_write(SCpnt);

			break;

		case BUSPHASE_DATA_IN:
			nsp32_dbg(NSP32_DEBUG_INTR, "fifo/read");

			//nsp32_pio_read(SCpnt);

			break;

		case BUSPHASE_STATUS:
			nsp32_dbg(NSP32_DEBUG_INTR, "fifo/status");

			SCpnt->SCp.Status = nsp32_read1(base, SCSI_CSB_IN);

			break;
		default:
			nsp32_dbg(NSP32_DEBUG_INTR, "fifo/other phase");
			nsp32_dbg(NSP32_DEBUG_INTR, "irq_stat=0x%x trans_stat=0x%x", irq_stat, trans_stat);
			show_busphase(busphase);
			break;
		}

		goto out;
	}

	/* Phase Change IRQ */
	if (irq_stat & IRQSTATUS_PHASE_CHANGE_IRQ) {
		nsp32_dbg(NSP32_DEBUG_INTR, "phase change IRQ");

		switch(busphase) {
		case BUSPHASE_MESSAGE_IN:
			nsp32_dbg(NSP32_DEBUG_INTR, "phase chg/msg in");
			nsp32_msgin_occur(SCpnt, irq_stat, 0);
			break;
		default:
			nsp32_msg(KERN_WARNING, "phase chg/other phase?");
			nsp32_msg(KERN_WARNING, "irq_stat=0x%x trans_stat=0x%x\n",
				  irq_stat, trans_stat);
			show_busphase(busphase);
			break;
		}
		goto out;
	}

	/* PCI_IRQ */
	if (irq_stat & IRQSTATUS_PCI_IRQ) {
		nsp32_dbg(NSP32_DEBUG_INTR, "PCI IRQ occurred");
		/* Do nothing */
	}

	/* BMCNTERR_IRQ */
	if (irq_stat & IRQSTATUS_BMCNTERR_IRQ) {
		nsp32_msg(KERN_ERR, "Received unexpected BMCNTERR IRQ! ");
		/*
		 * TODO: To be implemented improving bus master
		 * transfer reliability when BMCNTERR is occurred in
		 * AutoSCSI phase described in specification.
		 */
	}

#if 0
	nsp32_dbg(NSP32_DEBUG_INTR,
		  "irq_stat=0x%x trans_stat=0x%x", irq_stat, trans_stat);
	show_busphase(busphase);
#endif

 out:
	/* disable IRQ mask */
	nsp32_write2(base, IRQ_CONTROL, 0);

 out2:
	spin_unlock_irqrestore(host->host_lock, flags);

	nsp32_dbg(NSP32_DEBUG_INTR, "exit");

	return IRQ_RETVAL(handled);
}


static int nsp32_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	unsigned long     flags;
	nsp32_hw_data    *data;
	int               hostno;
	unsigned int      base;
	unsigned char     mode_reg;
	int               id, speed;
	long              model;

	hostno = host->host_no;
	data = (nsp32_hw_data *)host->hostdata;
	base = host->io_port;

	seq_puts(m, "NinjaSCSI-32 status\n\n");
	seq_printf(m, "Driver version:        %s, $Revision: 1.33 $\n", nsp32_release_version);
	seq_printf(m, "SCSI host No.:         %d\n",		hostno);
	seq_printf(m, "IRQ:                   %d\n",		host->irq);
	seq_printf(m, "IO:                    0x%lx-0x%lx\n", host->io_port, host->io_port + host->n_io_port - 1);
	seq_printf(m, "MMIO(virtual address): 0x%lx-0x%lx\n",	host->base, host->base + data->MmioLength - 1);
	seq_printf(m, "sg_tablesize:          %d\n",		host->sg_tablesize);
	seq_printf(m, "Chip revision:         0x%x\n",		(nsp32_read2(base, INDEX_REG) >> 8) & 0xff);

	mode_reg = nsp32_index_read1(base, CHIP_MODE);
	model    = data->pci_devid->driver_data;

#ifdef CONFIG_PM
	seq_printf(m, "Power Management:      %s\n",          (mode_reg & OPTF) ? "yes" : "no");
#endif
	seq_printf(m, "OEM:                   %ld, %s\n",     (mode_reg & (OEM0|OEM1)), nsp32_model[model]);

	spin_lock_irqsave(&(data->Lock), flags);
	seq_printf(m, "CurrentSC:             0x%p\n\n",      data->CurrentSC);
	spin_unlock_irqrestore(&(data->Lock), flags);


	seq_puts(m, "SDTR status\n");
	for (id = 0; id < ARRAY_SIZE(data->target); id++) {

		seq_printf(m, "id %d: ", id);

		if (id == host->this_id) {
			seq_puts(m, "----- NinjaSCSI-32 host adapter\n");
			continue;
		}

		if (data->target[id].sync_flag == SDTR_DONE) {
			if (data->target[id].period == 0            &&
			    data->target[id].offset == ASYNC_OFFSET ) {
				seq_puts(m, "async");
			} else {
				seq_puts(m, " sync");
			}
		} else {
			seq_puts(m, " none");
		}

		if (data->target[id].period != 0) {

			speed = 1000000 / (data->target[id].period * 4);

			seq_printf(m, " transfer %d.%dMB/s, offset %d",
				speed / 1000,
				speed % 1000,
				data->target[id].offset
				);
		}
		seq_putc(m, '\n');
	}
	return 0;
}



/*
 * Reset parameters and call scsi_done for data->cur_lunt.
 * Be careful setting SCpnt->result = DID_* before calling this function.
 */
static void nsp32_scsi_done(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int   base = SCpnt->device->host->io_port;

	scsi_dma_unmap(SCpnt);

	/*
	 * clear TRANSFERCONTROL_BM_START
	 */
	nsp32_write2(base, TRANSFER_CONTROL, 0);
	nsp32_write4(base, BM_CNT,           0);

	/*
	 * call scsi_done
	 */
	(*SCpnt->scsi_done)(SCpnt);

	/*
	 * reset parameters
	 */
	data->cur_lunt->SCpnt = NULL;
	data->cur_lunt        = NULL;
	data->cur_target      = NULL;
	data->CurrentSC      = NULL;
}


/*
 * Bus Free Occur
 *
 * Current Phase is BUSFREE. AutoSCSI is automatically execute BUSFREE phase
 * with ACK reply when below condition is matched:
 *	MsgIn 00: Command Complete.
 *	MsgIn 02: Save Data Pointer.
 *	MsgIn 04: Disconnect.
 * In other case, unexpected BUSFREE is detected.
 */
static int nsp32_busfree_occur(struct scsi_cmnd *SCpnt, unsigned short execph)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int base   = SCpnt->device->host->io_port;

	nsp32_dbg(NSP32_DEBUG_BUSFREE, "enter execph=0x%x", execph);
	show_autophase(execph);

	nsp32_write4(base, BM_CNT,           0);
	nsp32_write2(base, TRANSFER_CONTROL, 0);

	/*
	 * MsgIn 02: Save Data Pointer
	 *
	 * VALID:
	 *   Save Data Pointer is received. Adjust pointer.
	 *   
	 * NO-VALID:
	 *   SCSI-3 says if Save Data Pointer is not received, then we restart
	 *   processing and we can't adjust any SCSI data pointer in next data
	 *   phase.
	 */
	if (execph & MSGIN_02_VALID) {
		nsp32_dbg(NSP32_DEBUG_BUSFREE, "MsgIn02_Valid");

		/*
		 * Check sack_cnt/saved_sack_cnt, then adjust sg table if
		 * needed.
		 */
		if (!(execph & MSGIN_00_VALID) && 
		    ((execph & DATA_IN_PHASE) || (execph & DATA_OUT_PHASE))) {
			unsigned int sacklen, s_sacklen;

			/*
			 * Read SACK count and SAVEDSACK count, then compare.
			 */
			sacklen   = nsp32_read4(base, SACK_CNT      );
			s_sacklen = nsp32_read4(base, SAVED_SACK_CNT);

			/*
			 * If SAVEDSACKCNT == 0, it means SavedDataPointer is
			 * come after data transferring.
			 */
			if (s_sacklen > 0) {
				/*
				 * Comparing between sack and savedsack to
				 * check the condition of AutoMsgIn03.
				 *
				 * If they are same, set msgin03 == TRUE,
				 * COMMANDCONTROL_AUTO_MSGIN_03 is enabled at
				 * reselection.  On the other hand, if they
				 * aren't same, set msgin03 == FALSE, and
				 * COMMANDCONTROL_AUTO_MSGIN_03 is disabled at
				 * reselection.
				 */
				if (sacklen != s_sacklen) {
					data->cur_lunt->msgin03 = FALSE;
				} else {
					data->cur_lunt->msgin03 = TRUE;
				}

				nsp32_adjust_busfree(SCpnt, s_sacklen);
			}
		}

		/* This value has not substitude with valid value yet... */
		//data->cur_lunt->save_datp = data->cur_datp;
	} else {
		/*
		 * no processing.
		 */
	}
	
	if (execph & MSGIN_03_VALID) {
		/* MsgIn03 was valid to be processed. No need processing. */
	}

	/*
	 * target SDTR check
	 */
	if (data->cur_target->sync_flag & SDTR_INITIATOR) {
		/*
		 * SDTR negotiation pulled by the initiator has not
		 * finished yet. Fall back to ASYNC mode.
		 */
		nsp32_set_async(data, data->cur_target);
		data->cur_target->sync_flag &= ~SDTR_INITIATOR;
		data->cur_target->sync_flag |= SDTR_DONE;
	} else if (data->cur_target->sync_flag & SDTR_TARGET) {
		/*
		 * SDTR negotiation pulled by the target has been
		 * negotiating.
		 */
		if (execph & (MSGIN_00_VALID | MSGIN_04_VALID)) {
			/* 
			 * If valid message is received, then
			 * negotiation is succeeded.
			 */
		} else {
			/*
			 * On the contrary, if unexpected bus free is
			 * occurred, then negotiation is failed. Fall
			 * back to ASYNC mode.
			 */
			nsp32_set_async(data, data->cur_target);
		}
		data->cur_target->sync_flag &= ~SDTR_TARGET;
		data->cur_target->sync_flag |= SDTR_DONE;
	}

	/*
	 * It is always ensured by SCSI standard that initiator
	 * switches into Bus Free Phase after
	 * receiving message 00 (Command Complete), 04 (Disconnect).
	 * It's the reason that processing here is valid.
	 */
	if (execph & MSGIN_00_VALID) {
		/* MsgIn 00: Command Complete */
		nsp32_dbg(NSP32_DEBUG_BUSFREE, "command complete");

		SCpnt->SCp.Status  = nsp32_read1(base, SCSI_CSB_IN);
		SCpnt->SCp.Message = 0;
		nsp32_dbg(NSP32_DEBUG_BUSFREE, 
			  "normal end stat=0x%x resid=0x%x\n",
			  SCpnt->SCp.Status, scsi_get_resid(SCpnt));
		SCpnt->result = (DID_OK             << 16) |
			        (SCpnt->SCp.Message <<  8) |
			        (SCpnt->SCp.Status  <<  0);
		nsp32_scsi_done(SCpnt);
		/* All operation is done */
		return TRUE;
	} else if (execph & MSGIN_04_VALID) {
		/* MsgIn 04: Disconnect */
		SCpnt->SCp.Status  = nsp32_read1(base, SCSI_CSB_IN);
		SCpnt->SCp.Message = 4;
		
		nsp32_dbg(NSP32_DEBUG_BUSFREE, "disconnect");
		return TRUE;
	} else {
		/* Unexpected bus free */
		nsp32_msg(KERN_WARNING, "unexpected bus free occurred");

		/* DID_ERROR? */
		//SCpnt->result   = (DID_OK << 16) | (SCpnt->SCp.Message << 8) | (SCpnt->SCp.Status << 0);
		SCpnt->result = DID_ERROR << 16;
		nsp32_scsi_done(SCpnt);
		return TRUE;
	}
	return FALSE;
}


/*
 * nsp32_adjust_busfree - adjusting SG table
 *
 * Note: This driver adjust the SG table using SCSI ACK
 *       counter instead of BMCNT counter!
 */
static void nsp32_adjust_busfree(struct scsi_cmnd *SCpnt, unsigned int s_sacklen)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	int                   old_entry = data->cur_entry;
	int                   new_entry;
	int                   sg_num = data->cur_lunt->sg_num;
	nsp32_sgtable *sgt    = data->cur_lunt->sglun->sgt;
	unsigned int          restlen, sentlen;
	u32_le                len, addr;

	nsp32_dbg(NSP32_DEBUG_SGLIST, "old resid=0x%x", scsi_get_resid(SCpnt));

	/* adjust saved SACK count with 4 byte start address boundary */
	s_sacklen -= le32_to_cpu(sgt[old_entry].addr) & 3;

	/*
	 * calculate new_entry from sack count and each sgt[].len 
	 * calculate the byte which is intent to send
	 */
	sentlen = 0;
	for (new_entry = old_entry; new_entry < sg_num; new_entry++) {
		sentlen += (le32_to_cpu(sgt[new_entry].len) & ~SGTEND);
		if (sentlen > s_sacklen) {
			break;
		}
	}

	/* all sgt is processed */
	if (new_entry == sg_num) {
		goto last;
	}

	if (sentlen == s_sacklen) {
		/* XXX: confirm it's ok or not */
		/* In this case, it's ok because we are at 
		   the head element of the sg. restlen is correctly calculated. */
	}

	/* calculate the rest length for transferring */
	restlen = sentlen - s_sacklen;

	/* update adjusting current SG table entry */
	len  = le32_to_cpu(sgt[new_entry].len);
	addr = le32_to_cpu(sgt[new_entry].addr);
	addr += (len - restlen);
	sgt[new_entry].addr = cpu_to_le32(addr);
	sgt[new_entry].len  = cpu_to_le32(restlen);

	/* set cur_entry with new_entry */
	data->cur_entry = new_entry;
 
	return;

 last:
	if (scsi_get_resid(SCpnt) < sentlen) {
		nsp32_msg(KERN_ERR, "resid underflow");
	}

	scsi_set_resid(SCpnt, scsi_get_resid(SCpnt) - sentlen);
	nsp32_dbg(NSP32_DEBUG_SGLIST, "new resid=0x%x", scsi_get_resid(SCpnt));

	/* update hostdata and lun */

	return;
}


/*
 * It's called MsgOut phase occur.
 * NinjaSCSI-32Bi/UDE automatically processes up to 3 messages in
 * message out phase. It, however, has more than 3 messages,
 * HBA creates the interrupt and we have to process by hand.
 */
static void nsp32_msgout_occur(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int base   = SCpnt->device->host->io_port;
	//unsigned short command;
	long new_sgtp;
	int i;
	
	nsp32_dbg(NSP32_DEBUG_MSGOUTOCCUR,
		  "enter: msgout_len: 0x%x", data->msgout_len);

	/*
	 * If MsgOut phase is occurred without having any
	 * message, then No_Operation is sent (SCSI-2).
	 */
	if (data->msgout_len == 0) {
		nsp32_build_nop(SCpnt);
	}

	/*
	 * Set SGTP ADDR current entry for restarting AUTOSCSI, 
	 * because SGTP is incremented next point.
	 * There is few statement in the specification...
	 */
 	new_sgtp = data->cur_lunt->sglun_paddr + 
		   (data->cur_lunt->cur_entry * sizeof(nsp32_sgtable));

	/*
	 * send messages
	 */
	for (i = 0; i < data->msgout_len; i++) {
		nsp32_dbg(NSP32_DEBUG_MSGOUTOCCUR,
			  "%d : 0x%x", i, data->msgoutbuf[i]);

		/*
		 * Check REQ is asserted.
		 */
		nsp32_wait_req(data, ASSERT);

		if (i == (data->msgout_len - 1)) {
			/*
			 * If the last message, set the AutoSCSI restart
			 * before send back the ack message. AutoSCSI
			 * restart automatically negate ATN signal.
			 */
			//command = (AUTO_MSGIN_00_OR_04 | AUTO_MSGIN_02);
			//nsp32_restart_autoscsi(SCpnt, command);
			nsp32_write2(base, COMMAND_CONTROL,
					 (CLEAR_CDB_FIFO_POINTER |
					  AUTO_COMMAND_PHASE     |
					  AUTOSCSI_RESTART       |
					  AUTO_MSGIN_00_OR_04    |
					  AUTO_MSGIN_02          ));
		}
		/*
		 * Write data with SACK, then wait sack is
		 * automatically negated.
		 */
		nsp32_write1(base, SCSI_DATA_WITH_ACK, data->msgoutbuf[i]);
		nsp32_wait_sack(data, NEGATE);

		nsp32_dbg(NSP32_DEBUG_MSGOUTOCCUR, "bus: 0x%x\n",
			  nsp32_read1(base, SCSI_BUS_MONITOR));
	}

	data->msgout_len = 0;

	nsp32_dbg(NSP32_DEBUG_MSGOUTOCCUR, "exit");
}

/*
 * Restart AutoSCSI
 *
 * Note: Restarting AutoSCSI needs set:
 *		SYNC_REG, ACK_WIDTH, SGT_ADR, TRANSFER_CONTROL
 */
static void nsp32_restart_autoscsi(struct scsi_cmnd *SCpnt, unsigned short command)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int   base = data->BaseAddress;
	unsigned short transfer = 0;

	nsp32_dbg(NSP32_DEBUG_RESTART, "enter");

	if (data->cur_target == NULL || data->cur_lunt == NULL) {
		nsp32_msg(KERN_ERR, "Target or Lun is invalid");
	}

	/*
	 * set SYNC_REG
	 * Don't set BM_START_ADR before setting this register.
	 */
	nsp32_write1(base, SYNC_REG, data->cur_target->syncreg);

	/*
	 * set ACKWIDTH
	 */
	nsp32_write1(base, ACK_WIDTH, data->cur_target->ackwidth);

	/*
	 * set SREQ hazard killer sampling rate
	 */
	nsp32_write1(base, SREQ_SMPL_RATE, data->cur_target->sample_reg);

	/*
	 * set SGT ADDR (physical address)
	 */
	nsp32_write4(base, SGT_ADR, data->cur_lunt->sglun_paddr);

	/*
	 * set TRANSFER CONTROL REG
	 */
	transfer = 0;
	transfer |= (TRANSFER_GO | ALL_COUNTER_CLR);
	if (data->trans_method & NSP32_TRANSFER_BUSMASTER) {
		if (scsi_bufflen(SCpnt) > 0) {
			transfer |= BM_START;
		}
	} else if (data->trans_method & NSP32_TRANSFER_MMIO) {
		transfer |= CB_MMIO_MODE;
	} else if (data->trans_method & NSP32_TRANSFER_PIO) {
		transfer |= CB_IO_MODE;
	}
	nsp32_write2(base, TRANSFER_CONTROL, transfer);

	/*
	 * restart AutoSCSI
	 *
	 * TODO: COMMANDCONTROL_AUTO_COMMAND_PHASE is needed ?
	 */
	command |= (CLEAR_CDB_FIFO_POINTER |
		    AUTO_COMMAND_PHASE     |
		    AUTOSCSI_RESTART       );
	nsp32_write2(base, COMMAND_CONTROL, command);

	nsp32_dbg(NSP32_DEBUG_RESTART, "exit");
}


/*
 * cannot run automatically message in occur
 */
static void nsp32_msgin_occur(struct scsi_cmnd     *SCpnt,
			      unsigned long  irq_status,
			      unsigned short execph)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int   base = SCpnt->device->host->io_port;
	unsigned char  msg;
	unsigned char  msgtype;
	unsigned char  newlun;
	unsigned short command  = 0;
	int            msgclear = TRUE;
	long           new_sgtp;
	int            ret;

	/*
	 * read first message
	 *    Use SCSIDATA_W_ACK instead of SCSIDATAIN, because the procedure
	 *    of Message-In have to be processed before sending back SCSI ACK.
	 */
	msg = nsp32_read1(base, SCSI_DATA_IN);
	data->msginbuf[(unsigned char)data->msgin_len] = msg;
	msgtype = data->msginbuf[0];
	nsp32_dbg(NSP32_DEBUG_MSGINOCCUR,
		  "enter: msglen: 0x%x msgin: 0x%x msgtype: 0x%x",
		  data->msgin_len, msg, msgtype);

	/*
	 * TODO: We need checking whether bus phase is message in?
	 */

	/*
	 * assert SCSI ACK
	 */
	nsp32_sack_assert(data);

	/*
	 * processing IDENTIFY
	 */
	if (msgtype & 0x80) {
		if (!(irq_status & IRQSTATUS_RESELECT_OCCUER)) {
			/* Invalid (non reselect) phase */
			goto reject;
		}

		newlun = msgtype & 0x1f; /* TODO: SPI-3 compliant? */
		ret = nsp32_reselection(SCpnt, newlun);
		if (ret == TRUE) {
			goto restart;
		} else {
			goto reject;
		}
	}
	
	/*
	 * processing messages except for IDENTIFY
	 *
	 * TODO: Messages are all SCSI-2 terminology. SCSI-3 compliance is TODO.
	 */
	switch (msgtype) {
	/*
	 * 1-byte message
	 */
	case COMMAND_COMPLETE:
	case DISCONNECT:
		/*
		 * These messages should not be occurred.
		 * They should be processed on AutoSCSI sequencer.
		 */
		nsp32_msg(KERN_WARNING, 
			   "unexpected message of AutoSCSI MsgIn: 0x%x", msg);
		break;
		
	case RESTORE_POINTERS:
		/*
		 * AutoMsgIn03 is disabled, and HBA gets this message.
		 */

		if ((execph & DATA_IN_PHASE) || (execph & DATA_OUT_PHASE)) {
			unsigned int s_sacklen;

			s_sacklen = nsp32_read4(base, SAVED_SACK_CNT);
			if ((execph & MSGIN_02_VALID) && (s_sacklen > 0)) {
				nsp32_adjust_busfree(SCpnt, s_sacklen);
			} else {
				/* No need to rewrite SGT */
			}
		}
		data->cur_lunt->msgin03 = FALSE;

		/* Update with the new value */

		/* reset SACK/SavedACK counter (or ALL clear?) */
		nsp32_write4(base, CLR_COUNTER, CLRCOUNTER_ALLMASK);

		/*
		 * set new sg pointer
		 */
		new_sgtp = data->cur_lunt->sglun_paddr + 
			(data->cur_lunt->cur_entry * sizeof(nsp32_sgtable));
		nsp32_write4(base, SGT_ADR, new_sgtp);

		break;

	case SAVE_POINTERS:
		/*
		 * These messages should not be occurred.
		 * They should be processed on AutoSCSI sequencer.
		 */
		nsp32_msg (KERN_WARNING, 
			   "unexpected message of AutoSCSI MsgIn: SAVE_POINTERS");
		
		break;
		
	case MESSAGE_REJECT:
		/* If previous message_out is sending SDTR, and get 
		   message_reject from target, SDTR negotiation is failed */
		if (data->cur_target->sync_flag &
				(SDTR_INITIATOR | SDTR_TARGET)) {
			/*
			 * Current target is negotiating SDTR, but it's
			 * failed.  Fall back to async transfer mode, and set
			 * SDTR_DONE.
			 */
			nsp32_set_async(data, data->cur_target);
			data->cur_target->sync_flag &= ~SDTR_INITIATOR;
			data->cur_target->sync_flag |= SDTR_DONE;

		}
		break;

	case LINKED_CMD_COMPLETE:
	case LINKED_FLG_CMD_COMPLETE:
		/* queue tag is not supported currently */
		nsp32_msg (KERN_WARNING, 
			   "unsupported message: 0x%x", msgtype);
		break;

	case INITIATE_RECOVERY:
		/* staring ECA (Extended Contingent Allegiance) state. */
		/* This message is declined in SPI2 or later. */

		goto reject;

	/*
	 * 2-byte message
	 */
	case SIMPLE_QUEUE_TAG:
	case 0x23:
		/*
		 * 0x23: Ignore_Wide_Residue is not declared in scsi.h.
		 * No support is needed.
		 */
		if (data->msgin_len >= 1) {
			goto reject;
		}

		/* current position is 1-byte of 2 byte */
		msgclear = FALSE;

		break;

	/*
	 * extended message
	 */
	case EXTENDED_MESSAGE:
		if (data->msgin_len < 1) {
			/*
			 * Current position does not reach 2-byte
			 * (2-byte is extended message length).
			 */
			msgclear = FALSE;
			break;
		}

		if ((data->msginbuf[1] + 1) > data->msgin_len) {
			/*
			 * Current extended message has msginbuf[1] + 2
			 * (msgin_len starts counting from 0, so buf[1] + 1).
			 * If current message position is not finished,
			 * continue receiving message.
			 */
			msgclear = FALSE;
			break;
		}

		/*
		 * Reach here means regular length of each type of 
		 * extended messages.
		 */
		switch (data->msginbuf[2]) {
		case EXTENDED_MODIFY_DATA_POINTER:
			/* TODO */
			goto reject; /* not implemented yet */
			break;

		case EXTENDED_SDTR:
			/*
			 * Exchange this message between initiator and target.
			 */
			if (data->msgin_len != EXTENDED_SDTR_LEN + 1) {
				/*
				 * received inappropriate message.
				 */
				goto reject;
				break;
			}

			nsp32_analyze_sdtr(SCpnt);

			break;

		case EXTENDED_EXTENDED_IDENTIFY:
			/* SCSI-I only, not supported. */
			goto reject; /* not implemented yet */

			break;

		case EXTENDED_WDTR:
			goto reject; /* not implemented yet */

			break;
			
		default:
			goto reject;
		}
		break;
		
	default:
		goto reject;
	}

 restart:
	if (msgclear == TRUE) {
		data->msgin_len = 0;

		/*
		 * If restarting AutoSCSI, but there are some message to out
		 * (msgout_len > 0), set AutoATN, and set SCSIMSGOUT as 0
		 * (MV_VALID = 0). When commandcontrol is written with
		 * AutoSCSI restart, at the same time MsgOutOccur should be
		 * happened (however, such situation is really possible...?).
		 */
		if (data->msgout_len > 0) {	
			nsp32_write4(base, SCSI_MSG_OUT, 0);
			command |= AUTO_ATN;
		}

		/*
		 * restart AutoSCSI
		 * If it's failed, COMMANDCONTROL_AUTO_COMMAND_PHASE is needed.
		 */
		command |= (AUTO_MSGIN_00_OR_04 | AUTO_MSGIN_02);

		/*
		 * If current msgin03 is TRUE, then flag on.
		 */
		if (data->cur_lunt->msgin03 == TRUE) {
			command |= AUTO_MSGIN_03;
		}
		data->cur_lunt->msgin03 = FALSE;
	} else {
		data->msgin_len++;
	}

	/*
	 * restart AutoSCSI
	 */
	nsp32_restart_autoscsi(SCpnt, command);

	/*
	 * wait SCSI REQ negate for REQ-ACK handshake
	 */
	nsp32_wait_req(data, NEGATE);

	/*
	 * negate SCSI ACK
	 */
	nsp32_sack_negate(data);

	nsp32_dbg(NSP32_DEBUG_MSGINOCCUR, "exit");

	return;

 reject:
	nsp32_msg(KERN_WARNING, 
		  "invalid or unsupported MessageIn, rejected. "
		  "current msg: 0x%x (len: 0x%x), processing msg: 0x%x",
		  msg, data->msgin_len, msgtype);
	nsp32_build_reject(SCpnt);
	data->msgin_len = 0;

	goto restart;
}

/*
 * 
 */
static void nsp32_analyze_sdtr(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data   *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	nsp32_target     *target     = data->cur_target;
	nsp32_sync_table *synct;
	unsigned char     get_period = data->msginbuf[3];
	unsigned char     get_offset = data->msginbuf[4];
	int               entry;
	int               syncnum;

	nsp32_dbg(NSP32_DEBUG_MSGINOCCUR, "enter");

	synct   = data->synct;
	syncnum = data->syncnum;

	/*
	 * If this inititor sent the SDTR message, then target responds SDTR,
	 * initiator SYNCREG, ACKWIDTH from SDTR parameter.
	 * Messages are not appropriate, then send back reject message.
	 * If initiator did not send the SDTR, but target sends SDTR, 
	 * initiator calculator the appropriate parameter and send back SDTR.
	 */	
	if (target->sync_flag & SDTR_INITIATOR) {
		/*
		 * Initiator sent SDTR, the target responds and
		 * send back negotiation SDTR.
		 */
		nsp32_dbg(NSP32_DEBUG_MSGINOCCUR, "target responds SDTR");
	
		target->sync_flag &= ~SDTR_INITIATOR;
		target->sync_flag |= SDTR_DONE;

		/*
		 * offset:
		 */
		if (get_offset > SYNC_OFFSET) {
			/*
			 * Negotiation is failed, the target send back
			 * unexpected offset value.
			 */
			goto reject;
		}
		
		if (get_offset == ASYNC_OFFSET) {
			/*
			 * Negotiation is succeeded, the target want
			 * to fall back into asynchronous transfer mode.
			 */
			goto async;
		}

		/*
		 * period:
		 *    Check whether sync period is too short. If too short,
		 *    fall back to async mode. If it's ok, then investigate
		 *    the received sync period. If sync period is acceptable
		 *    between sync table start_period and end_period, then
		 *    set this I_T nexus as sent offset and period.
		 *    If it's not acceptable, send back reject and fall back
		 *    to async mode.
		 */
		if (get_period < data->synct[0].period_num) {
			/*
			 * Negotiation is failed, the target send back
			 * unexpected period value.
			 */
			goto reject;
		}

		entry = nsp32_search_period_entry(data, target, get_period);

		if (entry < 0) {
			/*
			 * Target want to use long period which is not 
			 * acceptable NinjaSCSI-32Bi/UDE.
			 */
			goto reject;
		}

		/*
		 * Set new sync table and offset in this I_T nexus.
		 */
		nsp32_set_sync_entry(data, target, entry, get_offset);
	} else {
		/* Target send SDTR to initiator. */
		nsp32_dbg(NSP32_DEBUG_MSGINOCCUR, "target send SDTR");
	
		target->sync_flag |= SDTR_INITIATOR;

		/* offset: */
		if (get_offset > SYNC_OFFSET) {
			/* send back as SYNC_OFFSET */
			get_offset = SYNC_OFFSET;
		}

		/* period: */
		if (get_period < data->synct[0].period_num) {
			get_period = data->synct[0].period_num;
		}

		entry = nsp32_search_period_entry(data, target, get_period);

		if (get_offset == ASYNC_OFFSET || entry < 0) {
			nsp32_set_async(data, target);
			nsp32_build_sdtr(SCpnt, 0, ASYNC_OFFSET);
		} else {
			nsp32_set_sync_entry(data, target, entry, get_offset);
			nsp32_build_sdtr(SCpnt, get_period, get_offset);
		}
	}

	target->period = get_period;
	nsp32_dbg(NSP32_DEBUG_MSGINOCCUR, "exit");
	return;

 reject:
	/*
	 * If the current message is unacceptable, send back to the target
	 * with reject message.
	 */
	nsp32_build_reject(SCpnt);

 async:
	nsp32_set_async(data, target);	/* set as ASYNC transfer mode */

	target->period = 0;
	nsp32_dbg(NSP32_DEBUG_MSGINOCCUR, "exit: set async");
	return;
}


/*
 * Search config entry number matched in sync_table from given
 * target and speed period value. If failed to search, return negative value.
 */
static int nsp32_search_period_entry(nsp32_hw_data *data,
				     nsp32_target  *target,
				     unsigned char  period)
{
	int i;

	if (target->limit_entry >= data->syncnum) {
		nsp32_msg(KERN_ERR, "limit_entry exceeds syncnum!");
		target->limit_entry = 0;
	}

	for (i = target->limit_entry; i < data->syncnum; i++) {
		if (period >= data->synct[i].start_period &&
		    period <= data->synct[i].end_period) {
				break;
		}
	}

	/*
	 * Check given period value is over the sync_table value.
	 * If so, return max value.
	 */
	if (i == data->syncnum) {
		i = -1;
	}

	return i;
}


/*
 * target <-> initiator use ASYNC transfer
 */
static void nsp32_set_async(nsp32_hw_data *data, nsp32_target *target)
{
	unsigned char period = data->synct[target->limit_entry].period_num;

	target->offset     = ASYNC_OFFSET;
	target->period     = 0;
	target->syncreg    = TO_SYNCREG(period, ASYNC_OFFSET);
	target->ackwidth   = 0;
	target->sample_reg = 0;

	nsp32_dbg(NSP32_DEBUG_SYNC, "set async");
}


/*
 * target <-> initiator use maximum SYNC transfer
 */
static void nsp32_set_max_sync(nsp32_hw_data *data,
			       nsp32_target  *target,
			       unsigned char *period,
			       unsigned char *offset)
{
	unsigned char period_num, ackwidth;

	period_num = data->synct[target->limit_entry].period_num;
	*period    = data->synct[target->limit_entry].start_period;
	ackwidth   = data->synct[target->limit_entry].ackwidth;
	*offset    = SYNC_OFFSET;

	target->syncreg    = TO_SYNCREG(period_num, *offset);
	target->ackwidth   = ackwidth;
	target->offset     = *offset;
	target->sample_reg = 0;       /* disable SREQ sampling */
}


/*
 * target <-> initiator use entry number speed
 */
static void nsp32_set_sync_entry(nsp32_hw_data *data,
				 nsp32_target  *target,
				 int            entry,
				 unsigned char  offset)
{
	unsigned char period, ackwidth, sample_rate;

	period      = data->synct[entry].period_num;
	ackwidth    = data->synct[entry].ackwidth;
	sample_rate = data->synct[entry].sample_rate;

	target->syncreg    = TO_SYNCREG(period, offset);
	target->ackwidth   = ackwidth;
	target->offset     = offset;
	target->sample_reg = sample_rate | SAMPLING_ENABLE;

	nsp32_dbg(NSP32_DEBUG_SYNC, "set sync");
}


/*
 * It waits until SCSI REQ becomes assertion or negation state.
 *
 * Note: If nsp32_msgin_occur is called, we asserts SCSI ACK. Then
 *     connected target responds SCSI REQ negation.  We have to wait
 *     SCSI REQ becomes negation in order to negate SCSI ACK signal for
 *     REQ-ACK handshake.
 */
static void nsp32_wait_req(nsp32_hw_data *data, int state)
{
	unsigned int  base      = data->BaseAddress;
	int           wait_time = 0;
	unsigned char bus, req_bit;

	if (!((state == ASSERT) || (state == NEGATE))) {
		nsp32_msg(KERN_ERR, "unknown state designation");
	}
	/* REQ is BIT(5) */
	req_bit = (state == ASSERT ? BUSMON_REQ : 0);

	do {
		bus = nsp32_read1(base, SCSI_BUS_MONITOR);
		if ((bus & BUSMON_REQ) == req_bit) {
			nsp32_dbg(NSP32_DEBUG_WAIT, 
				  "wait_time: %d", wait_time);
			return;
		}
		udelay(1);
		wait_time++;
	} while (wait_time < REQSACK_TIMEOUT_TIME);

	nsp32_msg(KERN_WARNING, "wait REQ timeout, req_bit: 0x%x", req_bit);
}

/*
 * It waits until SCSI SACK becomes assertion or negation state.
 */
static void nsp32_wait_sack(nsp32_hw_data *data, int state)
{
	unsigned int  base      = data->BaseAddress;
	int           wait_time = 0;
	unsigned char bus, ack_bit;

	if (!((state == ASSERT) || (state == NEGATE))) {
		nsp32_msg(KERN_ERR, "unknown state designation");
	}
	/* ACK is BIT(4) */
	ack_bit = (state == ASSERT ? BUSMON_ACK : 0);

	do {
		bus = nsp32_read1(base, SCSI_BUS_MONITOR);
		if ((bus & BUSMON_ACK) == ack_bit) {
			nsp32_dbg(NSP32_DEBUG_WAIT,
				  "wait_time: %d", wait_time);
			return;
		}
		udelay(1);
		wait_time++;
	} while (wait_time < REQSACK_TIMEOUT_TIME);

	nsp32_msg(KERN_WARNING, "wait SACK timeout, ack_bit: 0x%x", ack_bit);
}

/*
 * assert SCSI ACK
 *
 * Note: SCSI ACK assertion needs with ACKENB=1, AUTODIRECTION=1.
 */
static void nsp32_sack_assert(nsp32_hw_data *data)
{
	unsigned int  base = data->BaseAddress;
	unsigned char busctrl;

	busctrl  = nsp32_read1(base, SCSI_BUS_CONTROL);
	busctrl	|= (BUSCTL_ACK | AUTODIRECTION | ACKENB);
	nsp32_write1(base, SCSI_BUS_CONTROL, busctrl);
}

/*
 * negate SCSI ACK
 */
static void nsp32_sack_negate(nsp32_hw_data *data)
{
	unsigned int  base = data->BaseAddress;
	unsigned char busctrl;

	busctrl  = nsp32_read1(base, SCSI_BUS_CONTROL);
	busctrl	&= ~BUSCTL_ACK;
	nsp32_write1(base, SCSI_BUS_CONTROL, busctrl);
}



/*
 * Note: n_io_port is defined as 0x7f because I/O register port is
 *	 assigned as:
 *	0x800-0x8ff: memory mapped I/O port
 *	0x900-0xbff: (map same 0x800-0x8ff I/O port image repeatedly)
 *	0xc00-0xfff: CardBus status registers
 */
static int nsp32_detect(struct pci_dev *pdev)
{
	struct Scsi_Host *host;	/* registered host structure */
	struct resource  *res;
	nsp32_hw_data    *data;
	int               ret;
	int               i, j;

	nsp32_dbg(NSP32_DEBUG_REGISTER, "enter");

	/*
	 * register this HBA as SCSI device
	 */
	host = scsi_host_alloc(&nsp32_template, sizeof(nsp32_hw_data));
	if (host == NULL) {
		nsp32_msg (KERN_ERR, "failed to scsi register");
		goto err;
	}

	/*
	 * set nsp32_hw_data
	 */
	data = (nsp32_hw_data *)host->hostdata;

	memcpy(data, &nsp32_data_base, sizeof(nsp32_hw_data));

	host->irq       = data->IrqNumber;
	host->io_port   = data->BaseAddress;
	host->unique_id = data->BaseAddress;
	host->n_io_port	= data->NumAddress;
	host->base      = (unsigned long)data->MmioAddress;

	data->Host      = host;
	spin_lock_init(&(data->Lock));

	data->cur_lunt   = NULL;
	data->cur_target = NULL;

	/*
	 * Bus master transfer mode is supported currently.
	 */
	data->trans_method = NSP32_TRANSFER_BUSMASTER;

	/*
	 * Set clock div, CLOCK_4 (HBA has own external clock, and
	 * dividing * 100ns/4).
	 * Currently CLOCK_4 has only tested, not for CLOCK_2/PCICLK yet.
	 */
	data->clock = CLOCK_4;

	/*
	 * Select appropriate nsp32_sync_table and set I_CLOCKDIV.
	 */
	switch (data->clock) {
	case CLOCK_4:
		/* If data->clock is CLOCK_4, then select 40M sync table. */
		data->synct   = nsp32_sync_table_40M;
		data->syncnum = ARRAY_SIZE(nsp32_sync_table_40M);
		break;
	case CLOCK_2:
		/* If data->clock is CLOCK_2, then select 20M sync table. */
		data->synct   = nsp32_sync_table_20M;
		data->syncnum = ARRAY_SIZE(nsp32_sync_table_20M);
		break;
	case PCICLK:
		/* If data->clock is PCICLK, then select pci sync table. */
		data->synct   = nsp32_sync_table_pci;
		data->syncnum = ARRAY_SIZE(nsp32_sync_table_pci);
		break;
	default:
		nsp32_msg(KERN_WARNING,
			  "Invalid clock div is selected, set CLOCK_4.");
		/* Use default value CLOCK_4 */
		data->clock   = CLOCK_4;
		data->synct   = nsp32_sync_table_40M;
		data->syncnum = ARRAY_SIZE(nsp32_sync_table_40M);
	}

	/*
	 * setup nsp32_lunt
	 */

	/*
	 * setup DMA 
	 */
	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(32)) != 0) {
		nsp32_msg (KERN_ERR, "failed to set PCI DMA mask");
		goto scsi_unregister;
	}

	/*
	 * allocate autoparam DMA resource.
	 */
	data->autoparam = dma_alloc_coherent(&pdev->dev,
			sizeof(nsp32_autoparam), &(data->auto_paddr),
			GFP_KERNEL);
	if (data->autoparam == NULL) {
		nsp32_msg(KERN_ERR, "failed to allocate DMA memory");
		goto scsi_unregister;
	}

	/*
	 * allocate scatter-gather DMA resource.
	 */
	data->sg_list = dma_alloc_coherent(&pdev->dev, NSP32_SG_TABLE_SIZE,
			&data->sg_paddr, GFP_KERNEL);
	if (data->sg_list == NULL) {
		nsp32_msg(KERN_ERR, "failed to allocate DMA memory");
		goto free_autoparam;
	}

	for (i = 0; i < ARRAY_SIZE(data->lunt); i++) {
		for (j = 0; j < ARRAY_SIZE(data->lunt[0]); j++) {
			int offset = i * ARRAY_SIZE(data->lunt[0]) + j;
			nsp32_lunt tmp = {
				.SCpnt       = NULL,
				.save_datp   = 0,
				.msgin03     = FALSE,
				.sg_num      = 0,
				.cur_entry   = 0,
				.sglun       = &(data->sg_list[offset]),
				.sglun_paddr = data->sg_paddr + (offset * sizeof(nsp32_sglun)),
			};

			data->lunt[i][j] = tmp;
		}
	}

	/*
	 * setup target
	 */
	for (i = 0; i < ARRAY_SIZE(data->target); i++) {
		nsp32_target *target = &(data->target[i]);

		target->limit_entry  = 0;
		target->sync_flag    = 0;
		nsp32_set_async(data, target);
	}

	/*
	 * EEPROM check
	 */
	ret = nsp32_getprom_param(data);
	if (ret == FALSE) {
		data->resettime = 3;	/* default 3 */
	}

	/*
	 * setup HBA
	 */
	nsp32hw_init(data);

	snprintf(data->info_str, sizeof(data->info_str),
		 "NinjaSCSI-32Bi/UDE: irq %d, io 0x%lx+0x%x",
		 host->irq, host->io_port, host->n_io_port);

	/*
	 * SCSI bus reset
	 *
	 * Note: It's important to reset SCSI bus in initialization phase.
	 *     NinjaSCSI-32Bi/UDE HBA EEPROM seems to exchange SDTR when
	 *     system is coming up, so SCSI devices connected to HBA is set as
	 *     un-asynchronous mode.  It brings the merit that this HBA is
	 *     ready to start synchronous transfer without any preparation,
	 *     but we are difficult to control transfer speed.  In addition,
	 *     it prevents device transfer speed from effecting EEPROM start-up
	 *     SDTR.  NinjaSCSI-32Bi/UDE has the feature if EEPROM is set as
	 *     Auto Mode, then FAST-10M is selected when SCSI devices are
	 *     connected same or more than 4 devices.  It should be avoided
	 *     depending on this specification. Thus, resetting the SCSI bus
	 *     restores all connected SCSI devices to asynchronous mode, then
	 *     this driver set SDTR safely later, and we can control all SCSI
	 *     device transfer mode.
	 */
	nsp32_do_bus_reset(data);

	ret = request_irq(host->irq, do_nsp32_isr, IRQF_SHARED, "nsp32", data);
	if (ret < 0) {
		nsp32_msg(KERN_ERR, "Unable to allocate IRQ for NinjaSCSI32 "
			  "SCSI PCI controller. Interrupt: %d", host->irq);
		goto free_sg_list;
	}

        /*
         * PCI IO register
         */
	res = request_region(host->io_port, host->n_io_port, "nsp32");
	if (res == NULL) {
		nsp32_msg(KERN_ERR, 
			  "I/O region 0x%lx+0x%lx is already used",
			  data->BaseAddress, data->NumAddress);
		goto free_irq;
        }

	ret = scsi_add_host(host, &pdev->dev);
	if (ret) {
		nsp32_msg(KERN_ERR, "failed to add scsi host");
		goto free_region;
	}
	scsi_scan_host(host);
	pci_set_drvdata(pdev, host);
	return 0;

 free_region:
	release_region(host->io_port, host->n_io_port);

 free_irq:
	free_irq(host->irq, data);

 free_sg_list:
	dma_free_coherent(&pdev->dev, NSP32_SG_TABLE_SIZE,
			    data->sg_list, data->sg_paddr);

 free_autoparam:
	dma_free_coherent(&pdev->dev, sizeof(nsp32_autoparam),
			    data->autoparam, data->auto_paddr);
	
 scsi_unregister:
	scsi_host_put(host);

 err:
	return 1;
}

static int nsp32_release(struct Scsi_Host *host)
{
	nsp32_hw_data *data = (nsp32_hw_data *)host->hostdata;

	if (data->autoparam) {
		dma_free_coherent(&data->Pci->dev, sizeof(nsp32_autoparam),
				    data->autoparam, data->auto_paddr);
	}

	if (data->sg_list) {
		dma_free_coherent(&data->Pci->dev, NSP32_SG_TABLE_SIZE,
				    data->sg_list, data->sg_paddr);
	}

	if (host->irq) {
		free_irq(host->irq, data);
	}

	if (host->io_port && host->n_io_port) {
		release_region(host->io_port, host->n_io_port);
	}

	if (data->MmioAddress) {
		iounmap(data->MmioAddress);
	}

	return 0;
}

static const char *nsp32_info(struct Scsi_Host *shpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)shpnt->hostdata;

	return data->info_str;
}


/****************************************************************************
 * error handler
 */
static int nsp32_eh_abort(struct scsi_cmnd *SCpnt)
{
	nsp32_hw_data *data = (nsp32_hw_data *)SCpnt->device->host->hostdata;
	unsigned int   base = SCpnt->device->host->io_port;

	nsp32_msg(KERN_WARNING, "abort");

	if (data->cur_lunt->SCpnt == NULL) {
		nsp32_dbg(NSP32_DEBUG_BUSRESET, "abort failed");
		return FAILED;
	}

	if (data->cur_target->sync_flag & (SDTR_INITIATOR | SDTR_TARGET)) {
		/* reset SDTR negotiation */
		data->cur_target->sync_flag = 0;
		nsp32_set_async(data, data->cur_target);
	}

	nsp32_write2(base, TRANSFER_CONTROL, 0);
	nsp32_write2(base, BM_CNT,           0);

	SCpnt->result = DID_ABORT << 16;
	nsp32_scsi_done(SCpnt);

	nsp32_dbg(NSP32_DEBUG_BUSRESET, "abort success");
	return SUCCESS;
}

static void nsp32_do_bus_reset(nsp32_hw_data *data)
{
	unsigned int   base = data->BaseAddress;
	unsigned short intrdat;
	int i;

	nsp32_dbg(NSP32_DEBUG_BUSRESET, "in");

	/*
	 * stop all transfer
	 * clear TRANSFERCONTROL_BM_START
	 * clear counter
	 */
	nsp32_write2(base, TRANSFER_CONTROL, 0);
	nsp32_write4(base, BM_CNT,           0);
	nsp32_write4(base, CLR_COUNTER,      CLRCOUNTER_ALLMASK);

	/*
	 * fall back to asynchronous transfer mode
	 * initialize SDTR negotiation flag
	 */
	for (i = 0; i < ARRAY_SIZE(data->target); i++) {
		nsp32_target *target = &data->target[i];

		target->sync_flag = 0;
		nsp32_set_async(data, target);
	}

	/*
	 * reset SCSI bus
	 */
	nsp32_write1(base, SCSI_BUS_CONTROL, BUSCTL_RST);
	mdelay(RESET_HOLD_TIME / 1000);
	nsp32_write1(base, SCSI_BUS_CONTROL, 0);
	for(i = 0; i < 5; i++) {
		intrdat = nsp32_read2(base, IRQ_STATUS); /* dummy read */
		nsp32_dbg(NSP32_DEBUG_BUSRESET, "irq:1: 0x%x", intrdat);
        }

	data->CurrentSC = NULL;
}

static int nsp32_eh_host_reset(struct scsi_cmnd *SCpnt)
{
	struct Scsi_Host *host = SCpnt->device->host;
	unsigned int      base = SCpnt->device->host->io_port;
	nsp32_hw_data    *data = (nsp32_hw_data *)host->hostdata;

	nsp32_msg(KERN_INFO, "Host Reset");	
	nsp32_dbg(NSP32_DEBUG_BUSRESET, "SCpnt=0x%x", SCpnt);

	spin_lock_irq(SCpnt->device->host->host_lock);

	nsp32hw_init(data);
	nsp32_write2(base, IRQ_CONTROL, IRQ_CONTROL_ALL_IRQ_MASK);
	nsp32_do_bus_reset(data);
	nsp32_write2(base, IRQ_CONTROL, 0);

	spin_unlock_irq(SCpnt->device->host->host_lock);
	return SUCCESS;	/* Host reset is succeeded at any time. */
}


/**************************************************************************
 * EEPROM handler
 */

/*
 * getting EEPROM parameter
 */
static int nsp32_getprom_param(nsp32_hw_data *data)
{
	int vendor = data->pci_devid->vendor;
	int device = data->pci_devid->device;
	int ret, val, i;

	/*
	 * EEPROM checking.
	 */
	ret = nsp32_prom_read(data, 0x7e);
	if (ret != 0x55) {
		nsp32_msg(KERN_INFO, "No EEPROM detected: 0x%x", ret);
		return FALSE;
	}
	ret = nsp32_prom_read(data, 0x7f);
	if (ret != 0xaa) {
		nsp32_msg(KERN_INFO, "Invalid number: 0x%x", ret);
		return FALSE;
	}

	/*
	 * check EEPROM type
	 */
	if (vendor == PCI_VENDOR_ID_WORKBIT &&
	    device == PCI_DEVICE_ID_WORKBIT_STANDARD) {
		ret = nsp32_getprom_c16(data);
	} else if (vendor == PCI_VENDOR_ID_WORKBIT &&
		   device == PCI_DEVICE_ID_NINJASCSI_32BIB_LOGITEC) {
		ret = nsp32_getprom_at24(data);
	} else if (vendor == PCI_VENDOR_ID_WORKBIT &&
		   device == PCI_DEVICE_ID_NINJASCSI_32UDE_MELCO ) {
		ret = nsp32_getprom_at24(data);
	} else {
		nsp32_msg(KERN_WARNING, "Unknown EEPROM");
		ret = FALSE;
	}

	/* for debug : SPROM data full checking */
	for (i = 0; i <= 0x1f; i++) {
		val = nsp32_prom_read(data, i);
		nsp32_dbg(NSP32_DEBUG_EEPROM,
			  "rom address 0x%x : 0x%x", i, val);
	}

	return ret;
}


/*
 * AT24C01A (Logitec: LHA-600S), AT24C02 (Melco Buffalo: IFC-USLP) data map:
 *
 *   ROMADDR
 *   0x00 - 0x06 :  Device Synchronous Transfer Period (SCSI ID 0 - 6) 
 *			Value 0x0: ASYNC, 0x0c: Ultra-20M, 0x19: Fast-10M
 *   0x07        :  HBA Synchronous Transfer Period
 *			Value 0: AutoSync, 1: Manual Setting
 *   0x08 - 0x0f :  Not Used? (0x0)
 *   0x10        :  Bus Termination
 * 			Value 0: Auto[ON], 1: ON, 2: OFF
 *   0x11        :  Not Used? (0)
 *   0x12        :  Bus Reset Delay Time (0x03)
 *   0x13        :  Bootable CD Support
 *			Value 0: Disable, 1: Enable
 *   0x14        :  Device Scan
 *			Bit   7  6  5  4  3  2  1  0
 *			      |  <----------------->
 * 			      |    SCSI ID: Value 0: Skip, 1: YES
 *			      |->  Value 0: ALL scan,  Value 1: Manual
 *   0x15 - 0x1b :  Not Used? (0)
 *   0x1c        :  Constant? (0x01) (clock div?)
 *   0x1d - 0x7c :  Not Used (0xff)
 *   0x7d	 :  Not Used? (0xff)
 *   0x7e        :  Constant (0x55), Validity signature
 *   0x7f        :  Constant (0xaa), Validity signature
 */
static int nsp32_getprom_at24(nsp32_hw_data *data)
{
	int           ret, i;
	int           auto_sync;
	nsp32_target *target;
	int           entry;

	/*
	 * Reset time which is designated by EEPROM.
	 *
	 * TODO: Not used yet.
	 */
	data->resettime = nsp32_prom_read(data, 0x12);

	/*
	 * HBA Synchronous Transfer Period
	 *
	 * Note: auto_sync = 0: auto, 1: manual.  Ninja SCSI HBA spec says
	 *	that if auto_sync is 0 (auto), and connected SCSI devices are
	 *	same or lower than 3, then transfer speed is set as ULTRA-20M.
	 *	On the contrary if connected SCSI devices are same or higher
	 *	than 4, then transfer speed is set as FAST-10M.
	 *
	 *	I break this rule. The number of connected SCSI devices are
	 *	only ignored. If auto_sync is 0 (auto), then transfer speed is
	 *	forced as ULTRA-20M.
	 */
	ret = nsp32_prom_read(data, 0x07);
	switch (ret) {
	case 0:
		auto_sync = TRUE;
		break;
	case 1:
		auto_sync = FALSE;
		break;
	default:
		nsp32_msg(KERN_WARNING,
			  "Unsupported Auto Sync mode. Fall back to manual mode.");
		auto_sync = TRUE;
	}

	if (trans_mode == ULTRA20M_MODE) {
		auto_sync = TRUE;
	}

	/*
	 * each device Synchronous Transfer Period
	 */
	for (i = 0; i < NSP32_HOST_SCSIID; i++) {
		target = &data->target[i];
		if (auto_sync == TRUE) {
			target->limit_entry = 0;   /* set as ULTRA20M */
		} else {
			ret   = nsp32_prom_read(data, i);
			entry = nsp32_search_period_entry(data, target, ret);
			if (entry < 0) {
				/* search failed... set maximum speed */
				entry = 0;
			}
			target->limit_entry = entry;
		}
	}

	return TRUE;
}


/*
 * C16 110 (I-O Data: SC-NBD) data map:
 *
 *   ROMADDR
 *   0x00 - 0x06 :  Device Synchronous Transfer Period (SCSI ID 0 - 6) 
 *			Value 0x0: 20MB/S, 0x1: 10MB/S, 0x2: 5MB/S, 0x3: ASYNC
 *   0x07        :  0 (HBA Synchronous Transfer Period: Auto Sync)
 *   0x08 - 0x0f :  Not Used? (0x0)
 *   0x10        :  Transfer Mode
 *			Value 0: PIO, 1: Busmater
 *   0x11        :  Bus Reset Delay Time (0x00-0x20)
 *   0x12        :  Bus Termination
 * 			Value 0: Disable, 1: Enable
 *   0x13 - 0x19 :  Disconnection
 *			Value 0: Disable, 1: Enable
 *   0x1a - 0x7c :  Not Used? (0)
 *   0x7d	 :  Not Used? (0xf8)
 *   0x7e        :  Constant (0x55), Validity signature
 *   0x7f        :  Constant (0xaa), Validity signature
 */
static int nsp32_getprom_c16(nsp32_hw_data *data)
{
	int           ret, i;
	nsp32_target *target;
	int           entry, val;

	/*
	 * Reset time which is designated by EEPROM.
	 *
	 * TODO: Not used yet.
	 */
	data->resettime = nsp32_prom_read(data, 0x11);

	/*
	 * each device Synchronous Transfer Period
	 */
	for (i = 0; i < NSP32_HOST_SCSIID; i++) {
		target = &data->target[i];
		ret = nsp32_prom_read(data, i);
		switch (ret) {
		case 0:		/* 20MB/s */
			val = 0x0c;
			break;
		case 1:		/* 10MB/s */
			val = 0x19;
			break;
		case 2:		/* 5MB/s */
			val = 0x32;
			break;
		case 3:		/* ASYNC */
			val = 0x00;
			break;
		default:	/* default 20MB/s */
			val = 0x0c;
			break;
		}
		entry = nsp32_search_period_entry(data, target, val);
		if (entry < 0 || trans_mode == ULTRA20M_MODE) {
			/* search failed... set maximum speed */
			entry = 0;
		}
		target->limit_entry = entry;
	}

	return TRUE;
}


/*
 * Atmel AT24C01A (drived in 5V) serial EEPROM routines
 */
static int nsp32_prom_read(nsp32_hw_data *data, int romaddr)
{
	int i, val;

	/* start condition */
	nsp32_prom_start(data);

	/* device address */
	nsp32_prom_write_bit(data, 1);	/* 1 */
	nsp32_prom_write_bit(data, 0);	/* 0 */
	nsp32_prom_write_bit(data, 1);	/* 1 */
	nsp32_prom_write_bit(data, 0);	/* 0 */
	nsp32_prom_write_bit(data, 0);	/* A2: 0 (GND) */
	nsp32_prom_write_bit(data, 0);	/* A1: 0 (GND) */
	nsp32_prom_write_bit(data, 0);	/* A0: 0 (GND) */

	/* R/W: W for dummy write */
	nsp32_prom_write_bit(data, 0);

	/* ack */
	nsp32_prom_write_bit(data, 0);

	/* word address */
	for (i = 7; i >= 0; i--) {
		nsp32_prom_write_bit(data, ((romaddr >> i) & 1));
	}

	/* ack */
	nsp32_prom_write_bit(data, 0);

	/* start condition */
	nsp32_prom_start(data);

	/* device address */
	nsp32_prom_write_bit(data, 1);	/* 1 */
	nsp32_prom_write_bit(data, 0);	/* 0 */
	nsp32_prom_write_bit(data, 1);	/* 1 */
	nsp32_prom_write_bit(data, 0);	/* 0 */
	nsp32_prom_write_bit(data, 0);	/* A2: 0 (GND) */
	nsp32_prom_write_bit(data, 0);	/* A1: 0 (GND) */
	nsp32_prom_write_bit(data, 0);	/* A0: 0 (GND) */

	/* R/W: R */
	nsp32_prom_write_bit(data, 1);

	/* ack */
	nsp32_prom_write_bit(data, 0);

	/* data... */
	val = 0;
	for (i = 7; i >= 0; i--) {
		val += (nsp32_prom_read_bit(data) << i);
	}
	
	/* no ack */
	nsp32_prom_write_bit(data, 1);

	/* stop condition */
	nsp32_prom_stop(data);

	return val;
}

static void nsp32_prom_set(nsp32_hw_data *data, int bit, int val)
{
	int base = data->BaseAddress;
	int tmp;

	tmp = nsp32_index_read1(base, SERIAL_ROM_CTL);

	if (val == 0) {
		tmp &= ~bit;
	} else {
		tmp |=  bit;
	}

	nsp32_index_write1(base, SERIAL_ROM_CTL, tmp);

	udelay(10);
}

static int nsp32_prom_get(nsp32_hw_data *data, int bit)
{
	int base = data->BaseAddress;
	int tmp, ret;

	if (bit != SDA) {
		nsp32_msg(KERN_ERR, "return value is not appropriate");
		return 0;
	}


	tmp = nsp32_index_read1(base, SERIAL_ROM_CTL) & bit;

	if (tmp == 0) {
		ret = 0;
	} else {
		ret = 1;
	}

	udelay(10);

	return ret;
}

static void nsp32_prom_start (nsp32_hw_data *data)
{
	/* start condition */
	nsp32_prom_set(data, SCL, 1);
	nsp32_prom_set(data, SDA, 1);
	nsp32_prom_set(data, ENA, 1);	/* output mode */
	nsp32_prom_set(data, SDA, 0);	/* keeping SCL=1 and transiting
					 * SDA 1->0 is start condition */
	nsp32_prom_set(data, SCL, 0);
}

static void nsp32_prom_stop (nsp32_hw_data *data)
{
	/* stop condition */
	nsp32_prom_set(data, SCL, 1);
	nsp32_prom_set(data, SDA, 0);
	nsp32_prom_set(data, ENA, 1);	/* output mode */
	nsp32_prom_set(data, SDA, 1);
	nsp32_prom_set(data, SCL, 0);
}

static void nsp32_prom_write_bit(nsp32_hw_data *data, int val)
{
	/* write */
	nsp32_prom_set(data, SDA, val);
	nsp32_prom_set(data, SCL, 1  );
	nsp32_prom_set(data, SCL, 0  );
}

static int nsp32_prom_read_bit(nsp32_hw_data *data)
{
	int val;

	/* read */
	nsp32_prom_set(data, ENA, 0);	/* input mode */
	nsp32_prom_set(data, SCL, 1);

	val = nsp32_prom_get(data, SDA);

	nsp32_prom_set(data, SCL, 0);
	nsp32_prom_set(data, ENA, 1);	/* output mode */

	return val;
}


/**************************************************************************
 * Power Management
 */
#ifdef CONFIG_PM

/* Device suspended */
static int nsp32_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);

	nsp32_msg(KERN_INFO, "pci-suspend: pdev=0x%p, state=%ld, slot=%s, host=0x%p", pdev, state, pci_name(pdev), host);

	pci_save_state     (pdev);
	pci_disable_device (pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

/* Device woken up */
static int nsp32_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	nsp32_hw_data    *data = (nsp32_hw_data *)host->hostdata;
	unsigned short    reg;

	nsp32_msg(KERN_INFO, "pci-resume: pdev=0x%p, slot=%s, host=0x%p", pdev, pci_name(pdev), host);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake    (pdev, PCI_D0, 0);
	pci_restore_state  (pdev);

	reg = nsp32_read2(data->BaseAddress, INDEX_REG);

	nsp32_msg(KERN_INFO, "io=0x%x reg=0x%x", data->BaseAddress, reg);

	if (reg == 0xffff) {
		nsp32_msg(KERN_INFO, "missing device. abort resume.");
		return 0;
	}

	nsp32hw_init      (data);
	nsp32_do_bus_reset(data);

	nsp32_msg(KERN_INFO, "resume success");

	return 0;
}

#endif

/************************************************************************
 * PCI/Cardbus probe/remove routine
 */
static int nsp32_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	nsp32_hw_data *data = &nsp32_data_base;

	nsp32_dbg(NSP32_DEBUG_REGISTER, "enter");

        ret = pci_enable_device(pdev);
	if (ret) {
		nsp32_msg(KERN_ERR, "failed to enable pci device");
		return ret;
	}

	data->Pci         = pdev;
	data->pci_devid   = id;
	data->IrqNumber   = pdev->irq;
	data->BaseAddress = pci_resource_start(pdev, 0);
	data->NumAddress  = pci_resource_len  (pdev, 0);
	data->MmioAddress = pci_ioremap_bar(pdev, 1);
	data->MmioLength  = pci_resource_len  (pdev, 1);

	pci_set_master(pdev);

	ret = nsp32_detect(pdev);

	nsp32_msg(KERN_INFO, "irq: %i mmio: %p+0x%lx slot: %s model: %s",
		  pdev->irq,
		  data->MmioAddress, data->MmioLength,
		  pci_name(pdev),
		  nsp32_model[id->driver_data]);

	nsp32_dbg(NSP32_DEBUG_REGISTER, "exit %d", ret);

	return ret;
}

static void nsp32_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);

	nsp32_dbg(NSP32_DEBUG_REGISTER, "enter");

        scsi_remove_host(host);

	nsp32_release(host);

	scsi_host_put(host);
}

static struct pci_driver nsp32_driver = {
	.name		= "nsp32",
	.id_table	= nsp32_pci_table,
	.probe		= nsp32_probe,
	.remove		= nsp32_remove,
#ifdef CONFIG_PM
	.suspend	= nsp32_suspend, 
	.resume		= nsp32_resume, 
#endif
};

/*********************************************************************
 * Moule entry point
 */
static int __init init_nsp32(void) {
	nsp32_msg(KERN_INFO, "loading...");
	return pci_register_driver(&nsp32_driver);
}

static void __exit exit_nsp32(void) {
	nsp32_msg(KERN_INFO, "unloading...");
	pci_unregister_driver(&nsp32_driver);
}

module_init(init_nsp32);
module_exit(exit_nsp32);

/* end */
