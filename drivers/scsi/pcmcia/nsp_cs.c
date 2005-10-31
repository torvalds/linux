/*======================================================================

    NinjaSCSI-3 / NinjaSCSI-32Bi PCMCIA SCSI host adapter card driver
      By: YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>

    Ver.2.8   Support 32bit MMIO mode
              Support Synchronous Data Transfer Request (SDTR) mode
    Ver.2.0   Support 32bit PIO mode
    Ver.1.1.2 Fix for scatter list buffer exceeds
    Ver.1.1   Support scatter list
    Ver.0.1   Initial version

    This software may be used and distributed according to the terms of
    the GNU General Public License.

======================================================================*/

/***********************************************************************
    This driver is for these PCcards.

	I-O DATA PCSC-F	 (Workbit NinjaSCSI-3)
			"WBT", "NinjaSCSI-3", "R1.0"
	I-O DATA CBSC-II (Workbit NinjaSCSI-32Bi in 16bit mode)
			"IO DATA", "CBSC16	 ", "1"

***********************************************************************/

/* $Id: nsp_cs.c,v 1.23 2003/08/18 11:09:19 elca Exp $ */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/stat.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <../drivers/scsi/scsi.h>
#include <scsi/scsi_host.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include "nsp_cs.h"

MODULE_AUTHOR("YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>");
MODULE_DESCRIPTION("WorkBit NinjaSCSI-3 / NinjaSCSI-32Bi(16bit) PCMCIA SCSI host adapter module $Revision: 1.23 $");
MODULE_SUPPORTED_DEVICE("sd,sr,sg,st");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#include "nsp_io.h"

/*====================================================================*/
/* Parameters that can be set with 'insmod' */

static int       nsp_burst_mode = BURST_MEM32;
module_param(nsp_burst_mode, int, 0);
MODULE_PARM_DESC(nsp_burst_mode, "Burst transfer mode (0=io8, 1=io32, 2=mem32(default))");

/* Release IO ports after configuration? */
static int       free_ports = 0;
module_param(free_ports, bool, 0);
MODULE_PARM_DESC(free_ports, "Release IO ports after configuration? (default: 0 (=no))");

/* /usr/src/linux/drivers/scsi/hosts.h */
static struct scsi_host_template nsp_driver_template = {
	.proc_name	         = "nsp_cs",
	.proc_info		 = nsp_proc_info,
	.name			 = "WorkBit NinjaSCSI-3/32Bi(16bit)",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
	.detect			 = nsp_detect_old,
	.release		 = nsp_release_old,
#endif
	.info			 = nsp_info,
	.queuecommand		 = nsp_queuecommand,
/*	.eh_abort_handler	 = nsp_eh_abort,*/
	.eh_bus_reset_handler	 = nsp_eh_bus_reset,
	.eh_host_reset_handler	 = nsp_eh_host_reset,
	.can_queue		 = 1,
	.this_id		 = NSP_INITIATOR_ID,
	.sg_tablesize		 = SG_ALL,
	.cmd_per_lun		 = 1,
	.use_clustering		 = DISABLE_CLUSTERING,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,2))
	.use_new_eh_code	 = 1,
#endif
};

static dev_link_t *dev_list = NULL;
static dev_info_t dev_info  = {"nsp_cs"};

static nsp_hw_data nsp_data_base; /* attach <-> detect glue */



/*
 * debug, error print
 */
#ifndef NSP_DEBUG
# define NSP_DEBUG_MASK		0x000000
# define nsp_msg(type, args...) nsp_cs_message("", 0, (type), args)
# define nsp_dbg(mask, args...) /* */
#else
# define NSP_DEBUG_MASK		0xffffff
# define nsp_msg(type, args...) \
	nsp_cs_message (__FUNCTION__, __LINE__, (type), args)
# define nsp_dbg(mask, args...) \
	nsp_cs_dmessage(__FUNCTION__, __LINE__, (mask), args)
#endif

#define NSP_DEBUG_QUEUECOMMAND		BIT(0)
#define NSP_DEBUG_REGISTER		BIT(1)
#define NSP_DEBUG_AUTOSCSI		BIT(2)
#define NSP_DEBUG_INTR			BIT(3)
#define NSP_DEBUG_SGLIST		BIT(4)
#define NSP_DEBUG_BUSFREE		BIT(5)
#define NSP_DEBUG_CDB_CONTENTS		BIT(6)
#define NSP_DEBUG_RESELECTION		BIT(7)
#define NSP_DEBUG_MSGINOCCUR		BIT(8)
#define NSP_DEBUG_EEPROM		BIT(9)
#define NSP_DEBUG_MSGOUTOCCUR		BIT(10)
#define NSP_DEBUG_BUSRESET		BIT(11)
#define NSP_DEBUG_RESTART		BIT(12)
#define NSP_DEBUG_SYNC			BIT(13)
#define NSP_DEBUG_WAIT			BIT(14)
#define NSP_DEBUG_TARGETFLAG		BIT(15)
#define NSP_DEBUG_PROC			BIT(16)
#define NSP_DEBUG_INIT			BIT(17)
#define NSP_DEBUG_DATA_IO      		BIT(18)
#define NSP_SPECIAL_PRINT_REGISTER	BIT(20)

#define NSP_DEBUG_BUF_LEN		150

static void nsp_cs_message(const char *func, int line, char *type, char *fmt, ...)
{
	va_list args;
	char buf[NSP_DEBUG_BUF_LEN];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

#ifndef NSP_DEBUG
	printk("%snsp_cs: %s\n", type, buf);
#else
	printk("%snsp_cs: %s (%d): %s\n", type, func, line, buf);
#endif
}

#ifdef NSP_DEBUG
static void nsp_cs_dmessage(const char *func, int line, int mask, char *fmt, ...)
{
	va_list args;
	char buf[NSP_DEBUG_BUF_LEN];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (mask & NSP_DEBUG_MASK) {
		printk("nsp_cs-debug: 0x%x %s (%d): %s\n", mask, func, line, buf);
	}
}
#endif

/***********************************************************/

/*====================================================
 * Clenaup parameters and call done() functions.
 * You must be set SCpnt->result before call this function.
 */
static void nsp_scsi_done(Scsi_Cmnd *SCpnt)
{
	nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;

	data->CurrentSC = NULL;

	SCpnt->scsi_done(SCpnt);
}

static int nsp_queuecommand(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
#ifdef NSP_DEBUG
	/*unsigned int host_id = SCpnt->device->host->this_id;*/
	/*unsigned int base    = SCpnt->device->host->io_port;*/
	unsigned char target = scmd_id(SCpnt);
#endif
	nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;

	nsp_dbg(NSP_DEBUG_QUEUECOMMAND, "SCpnt=0x%p target=%d lun=%d buff=0x%p bufflen=%d use_sg=%d",
		   SCpnt, target, SCpnt->device->lun, SCpnt->request_buffer, SCpnt->request_bufflen, SCpnt->use_sg);
	//nsp_dbg(NSP_DEBUG_QUEUECOMMAND, "before CurrentSC=0x%p", data->CurrentSC);

	SCpnt->scsi_done	= done;

	if (data->CurrentSC != NULL) {
		nsp_msg(KERN_DEBUG, "CurrentSC!=NULL this can't be happen");
		SCpnt->result   = DID_BAD_TARGET << 16;
		nsp_scsi_done(SCpnt);
		return 0;
	}

#if 0
	/* XXX: pcmcia-cs generates SCSI command with "scsi_info" utility.
	        This makes kernel crash when suspending... */
	if (data->ScsiInfo->stop != 0) {
		nsp_msg(KERN_INFO, "suspending device. reject command.");
		SCpnt->result  = DID_BAD_TARGET << 16;
		nsp_scsi_done(SCpnt);
		return SCSI_MLQUEUE_HOST_BUSY;
	}
#endif

	show_command(SCpnt);

	data->CurrentSC		= SCpnt;

	SCpnt->SCp.Status	= CHECK_CONDITION;
	SCpnt->SCp.Message	= 0;
	SCpnt->SCp.have_data_in = IO_UNKNOWN;
	SCpnt->SCp.sent_command = 0;
	SCpnt->SCp.phase	= PH_UNDETERMINED;
	SCpnt->resid	        = SCpnt->request_bufflen;

	/* setup scratch area
	   SCp.ptr		: buffer pointer
	   SCp.this_residual	: buffer length
	   SCp.buffer		: next buffer
	   SCp.buffers_residual : left buffers in list
	   SCp.phase		: current state of the command */
	if (SCpnt->use_sg) {
		SCpnt->SCp.buffer	    = (struct scatterlist *) SCpnt->request_buffer;
		SCpnt->SCp.ptr		    = BUFFER_ADDR;
		SCpnt->SCp.this_residual    = SCpnt->SCp.buffer->length;
		SCpnt->SCp.buffers_residual = SCpnt->use_sg - 1;
	} else {
		SCpnt->SCp.ptr		    = (char *) SCpnt->request_buffer;
		SCpnt->SCp.this_residual    = SCpnt->request_bufflen;
		SCpnt->SCp.buffer	    = NULL;
		SCpnt->SCp.buffers_residual = 0;
	}

	if (nsphw_start_selection(SCpnt) == FALSE) {
		nsp_dbg(NSP_DEBUG_QUEUECOMMAND, "selection fail");
		SCpnt->result   = DID_BUS_BUSY << 16;
		nsp_scsi_done(SCpnt);
		return 0;
	}


	//nsp_dbg(NSP_DEBUG_QUEUECOMMAND, "out");
#ifdef NSP_DEBUG
	data->CmdId++;
#endif
	return 0;
}

/*
 * setup PIO FIFO transfer mode and enable/disable to data out
 */
static void nsp_setup_fifo(nsp_hw_data *data, int enabled)
{
	unsigned int  base = data->BaseAddress;
	unsigned char transfer_mode_reg;

	//nsp_dbg(NSP_DEBUG_DATA_IO, "enabled=%d", enabled);

	if (enabled != FALSE) {
		transfer_mode_reg = TRANSFER_GO | BRAIND;
	} else {
		transfer_mode_reg = 0;
	}

	transfer_mode_reg |= data->TransferMode;

	nsp_index_write(base, TRANSFERMODE, transfer_mode_reg);
}

static void nsphw_init_sync(nsp_hw_data *data)
{
	sync_data tmp_sync = { .SyncNegotiation = SYNC_NOT_YET,
			       .SyncPeriod      = 0,
			       .SyncOffset      = 0
	};
	int i;

	/* setup sync data */
	for ( i = 0; i < ARRAY_SIZE(data->Sync); i++ ) {
		data->Sync[i] = tmp_sync;
	}
}

/*
 * Initialize Ninja hardware
 */
static int nsphw_init(nsp_hw_data *data)
{
	unsigned int base     = data->BaseAddress;

	nsp_dbg(NSP_DEBUG_INIT, "in base=0x%x", base);

	data->ScsiClockDiv = CLOCK_40M | FAST_20;
	data->CurrentSC    = NULL;
	data->FifoCount    = 0;
	data->TransferMode = MODE_IO8;

	nsphw_init_sync(data);

	/* block all interrupts */
	nsp_write(base,	      IRQCONTROL,   IRQCONTROL_ALLMASK);

	/* setup SCSI interface */
	nsp_write(base,	      IFSELECT,	    IF_IFSEL);

	nsp_index_write(base, SCSIIRQMODE,  0);

	nsp_index_write(base, TRANSFERMODE, MODE_IO8);
	nsp_index_write(base, CLOCKDIV,	    data->ScsiClockDiv);

	nsp_index_write(base, PARITYCTRL,   0);
	nsp_index_write(base, POINTERCLR,   POINTER_CLEAR     |
					    ACK_COUNTER_CLEAR |
					    REQ_COUNTER_CLEAR |
					    HOST_COUNTER_CLEAR);

	/* setup fifo asic */
	nsp_write(base,	      IFSELECT,	    IF_REGSEL);
	nsp_index_write(base, TERMPWRCTRL,  0);
	if ((nsp_index_read(base, OTHERCONTROL) & TPWR_SENSE) == 0) {
		nsp_msg(KERN_INFO, "terminator power on");
		nsp_index_write(base, TERMPWRCTRL, POWER_ON);
	}

	nsp_index_write(base, TIMERCOUNT,   0);
	nsp_index_write(base, TIMERCOUNT,   0); /* requires 2 times!! */

	nsp_index_write(base, SYNCREG,	    0);
	nsp_index_write(base, ACKWIDTH,	    0);

	/* enable interrupts and ack them */
	nsp_index_write(base, SCSIIRQMODE,  SCSI_PHASE_CHANGE_EI |
					    RESELECT_EI		 |
					    SCSI_RESET_IRQ_EI	 );
	nsp_write(base,	      IRQCONTROL,   IRQCONTROL_ALLCLEAR);

	nsp_setup_fifo(data, FALSE);

	return TRUE;
}

/*
 * Start selection phase
 */
static int nsphw_start_selection(Scsi_Cmnd *SCpnt)
{
	unsigned int  host_id	 = SCpnt->device->host->this_id;
	unsigned int  base	 = SCpnt->device->host->io_port;
	unsigned char target	 = scmd_id(SCpnt);
	nsp_hw_data  *data = (nsp_hw_data *)SCpnt->device->host->hostdata;
	int	      time_out;
	unsigned char phase, arbit;

	//nsp_dbg(NSP_DEBUG_RESELECTION, "in");

	phase = nsp_index_read(base, SCSIBUSMON);
	if(phase != BUSMON_BUS_FREE) {
		//nsp_dbg(NSP_DEBUG_RESELECTION, "bus busy");
		return FALSE;
	}

	/* start arbitration */
	//nsp_dbg(NSP_DEBUG_RESELECTION, "start arbit");
	SCpnt->SCp.phase = PH_ARBSTART;
	nsp_index_write(base, SETARBIT, ARBIT_GO);

	time_out = 1000;
	do {
		/* XXX: what a stupid chip! */
		arbit = nsp_index_read(base, ARBITSTATUS);
		//nsp_dbg(NSP_DEBUG_RESELECTION, "arbit=%d, wait_count=%d", arbit, wait_count);
		udelay(1); /* hold 1.2us */
	} while((arbit & (ARBIT_WIN | ARBIT_FAIL)) == 0 &&
		(time_out-- != 0));

	if (!(arbit & ARBIT_WIN)) {
		//nsp_dbg(NSP_DEBUG_RESELECTION, "arbit fail");
		nsp_index_write(base, SETARBIT, ARBIT_FLAG_CLEAR);
		return FALSE;
	}

	/* assert select line */
	//nsp_dbg(NSP_DEBUG_RESELECTION, "assert SEL line");
	SCpnt->SCp.phase = PH_SELSTART;
	udelay(3); /* wait 2.4us */
	nsp_index_write(base, SCSIDATALATCH, BIT(host_id) | BIT(target));
	nsp_index_write(base, SCSIBUSCTRL,   SCSI_SEL | SCSI_BSY                    | SCSI_ATN);
	udelay(2); /* wait >1.2us */
	nsp_index_write(base, SCSIBUSCTRL,   SCSI_SEL | SCSI_BSY | SCSI_DATAOUT_ENB | SCSI_ATN);
	nsp_index_write(base, SETARBIT,	     ARBIT_FLAG_CLEAR);
	/*udelay(1);*/ /* wait >90ns */
	nsp_index_write(base, SCSIBUSCTRL,   SCSI_SEL            | SCSI_DATAOUT_ENB | SCSI_ATN);

	/* check selection timeout */
	nsp_start_timer(SCpnt, 1000/51);
	data->SelectionTimeOut = 1;

	return TRUE;
}

struct nsp_sync_table {
	unsigned int min_period;
	unsigned int max_period;
	unsigned int chip_period;
	unsigned int ack_width;
};

static struct nsp_sync_table nsp_sync_table_40M[] = {
	{0x0c, 0x0c, 0x1, 0},	/* 20MB	  50ns*/
	{0x19, 0x19, 0x3, 1},	/* 10MB	 100ns*/ 
	{0x1a, 0x25, 0x5, 2},	/* 7.5MB 150ns*/ 
	{0x26, 0x32, 0x7, 3},	/* 5MB	 200ns*/
	{   0,    0,   0, 0},
};

static struct nsp_sync_table nsp_sync_table_20M[] = {
	{0x19, 0x19, 0x1, 0},	/* 10MB	 100ns*/ 
	{0x1a, 0x25, 0x2, 0},	/* 7.5MB 150ns*/ 
	{0x26, 0x32, 0x3, 1},	/* 5MB	 200ns*/
	{   0,    0,   0, 0},
};

/*
 * setup synchronous data transfer mode
 */
static int nsp_analyze_sdtr(Scsi_Cmnd *SCpnt)
{
	unsigned char	       target = scmd_id(SCpnt);
//	unsigned char	       lun    = SCpnt->device->lun;
	nsp_hw_data           *data   = (nsp_hw_data *)SCpnt->device->host->hostdata;
	sync_data	      *sync   = &(data->Sync[target]);
	struct nsp_sync_table *sync_table;
	unsigned int	       period, offset;
	int		       i;


	nsp_dbg(NSP_DEBUG_SYNC, "in");

	period = sync->SyncPeriod;
	offset = sync->SyncOffset;

	nsp_dbg(NSP_DEBUG_SYNC, "period=0x%x, offset=0x%x", period, offset);

	if ((data->ScsiClockDiv & (BIT(0)|BIT(1))) == CLOCK_20M) {
		sync_table = nsp_sync_table_20M;
	} else {
		sync_table = nsp_sync_table_40M;
	}

	for ( i = 0; sync_table->max_period != 0; i++, sync_table++) {
		if ( period >= sync_table->min_period &&
		     period <= sync_table->max_period	 ) {
			break;
		}
	}

	if (period != 0 && sync_table->max_period == 0) {
		/*
		 * No proper period/offset found
		 */
		nsp_dbg(NSP_DEBUG_SYNC, "no proper period/offset");

		sync->SyncPeriod      = 0;
		sync->SyncOffset      = 0;
		sync->SyncRegister    = 0;
		sync->AckWidth	      = 0;

		return FALSE;
	}

	sync->SyncRegister    = (sync_table->chip_period << SYNCREG_PERIOD_SHIFT) |
		                (offset & SYNCREG_OFFSET_MASK);
	sync->AckWidth	      = sync_table->ack_width;

	nsp_dbg(NSP_DEBUG_SYNC, "sync_reg=0x%x, ack_width=0x%x", sync->SyncRegister, sync->AckWidth);

	return TRUE;
}


/*
 * start ninja hardware timer
 */
static void nsp_start_timer(Scsi_Cmnd *SCpnt, int time)
{
	unsigned int base = SCpnt->device->host->io_port;
	nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;

	//nsp_dbg(NSP_DEBUG_INTR, "in SCpnt=0x%p, time=%d", SCpnt, time);
	data->TimerCount = time;
	nsp_index_write(base, TIMERCOUNT, time);
}

/*
 * wait for bus phase change
 */
static int nsp_negate_signal(Scsi_Cmnd *SCpnt, unsigned char mask, char *str)
{
	unsigned int  base = SCpnt->device->host->io_port;
	unsigned char reg;
	int	      time_out;

	//nsp_dbg(NSP_DEBUG_INTR, "in");

	time_out = 100;

	do {
		reg = nsp_index_read(base, SCSIBUSMON);
		if (reg == 0xff) {
			break;
		}
	} while ((time_out-- != 0) && (reg & mask) != 0);

	if (time_out == 0) {
		nsp_msg(KERN_DEBUG, " %s signal off timeut", str);
	}

	return 0;
}

/*
 * expect Ninja Irq
 */
static int nsp_expect_signal(Scsi_Cmnd	   *SCpnt,
			     unsigned char  current_phase,
			     unsigned char  mask)
{
	unsigned int  base	 = SCpnt->device->host->io_port;
	int	      time_out;
	unsigned char phase, i_src;

	//nsp_dbg(NSP_DEBUG_INTR, "current_phase=0x%x, mask=0x%x", current_phase, mask);

	time_out = 100;
	do {
		phase = nsp_index_read(base, SCSIBUSMON);
		if (phase == 0xff) {
			//nsp_dbg(NSP_DEBUG_INTR, "ret -1");
			return -1;
		}
		i_src = nsp_read(base, IRQSTATUS);
		if (i_src & IRQSTATUS_SCSI) {
			//nsp_dbg(NSP_DEBUG_INTR, "ret 0 found scsi signal");
			return 0;
		}
		if ((phase & mask) != 0 && (phase & BUSMON_PHASE_MASK) == current_phase) {
			//nsp_dbg(NSP_DEBUG_INTR, "ret 1 phase=0x%x", phase);
			return 1;
		}
	} while(time_out-- != 0);

	//nsp_dbg(NSP_DEBUG_INTR, "timeout");
	return -1;
}

/*
 * transfer SCSI message
 */
static int nsp_xfer(Scsi_Cmnd *SCpnt, int phase)
{
	unsigned int  base = SCpnt->device->host->io_port;
	nsp_hw_data  *data = (nsp_hw_data *)SCpnt->device->host->hostdata;
	char	     *buf  = data->MsgBuffer;
	int	      len  = min(MSGBUF_SIZE, data->MsgLen);
	int	      ptr;
	int	      ret;

	//nsp_dbg(NSP_DEBUG_DATA_IO, "in");
	for (ptr = 0; len > 0; len--, ptr++) {

		ret = nsp_expect_signal(SCpnt, phase, BUSMON_REQ);
		if (ret <= 0) {
			nsp_dbg(NSP_DEBUG_DATA_IO, "xfer quit");
			return 0;
		}

		/* if last byte, negate ATN */
		if (len == 1 && SCpnt->SCp.phase == PH_MSG_OUT) {
			nsp_index_write(base, SCSIBUSCTRL, AUTODIRECTION | ACKENB);
		}

		/* read & write message */
		if (phase & BUSMON_IO) {
			nsp_dbg(NSP_DEBUG_DATA_IO, "read msg");
			buf[ptr] = nsp_index_read(base, SCSIDATAWITHACK);
		} else {
			nsp_dbg(NSP_DEBUG_DATA_IO, "write msg");
			nsp_index_write(base, SCSIDATAWITHACK, buf[ptr]);
		}
		nsp_negate_signal(SCpnt, BUSMON_ACK, "xfer<ack>");

	}
	return len;
}

/*
 * get extra SCSI data from fifo
 */
static int nsp_dataphase_bypass(Scsi_Cmnd *SCpnt)
{
	nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;
	unsigned int count;

	//nsp_dbg(NSP_DEBUG_DATA_IO, "in");

	if (SCpnt->SCp.have_data_in != IO_IN) {
		return 0;
	}

	count = nsp_fifo_count(SCpnt);
	if (data->FifoCount == count) {
		//nsp_dbg(NSP_DEBUG_DATA_IO, "not use bypass quirk");
		return 0;
	}

	/*
	 * XXX: NSP_QUIRK
	 * data phase skip only occures in case of SCSI_LOW_READ
	 */
	nsp_dbg(NSP_DEBUG_DATA_IO, "use bypass quirk");
	SCpnt->SCp.phase = PH_DATA;
	nsp_pio_read(SCpnt);
	nsp_setup_fifo(data, FALSE);

	return 0;
}

/*
 * accept reselection
 */
static int nsp_reselected(Scsi_Cmnd *SCpnt)
{
	unsigned int  base    = SCpnt->device->host->io_port;
	unsigned int  host_id = SCpnt->device->host->this_id;
	//nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;
	unsigned char bus_reg;
	unsigned char id_reg, tmp;
	int target;

	nsp_dbg(NSP_DEBUG_RESELECTION, "in");

	id_reg = nsp_index_read(base, RESELECTID);
	tmp    = id_reg & (~BIT(host_id));
	target = 0;
	while(tmp != 0) {
		if (tmp & BIT(0)) {
			break;
		}
		tmp >>= 1;
		target++;
	}

	if (scmd_id(SCpnt) != target) {
		nsp_msg(KERN_ERR, "XXX: reselect ID must be %d in this implementation.", target);
	}

	nsp_negate_signal(SCpnt, BUSMON_SEL, "reselect<SEL>");

	nsp_nexus(SCpnt);
	bus_reg = nsp_index_read(base, SCSIBUSCTRL) & ~(SCSI_BSY | SCSI_ATN);
	nsp_index_write(base, SCSIBUSCTRL, bus_reg);
	nsp_index_write(base, SCSIBUSCTRL, bus_reg | AUTODIRECTION | ACKENB);

	return TRUE;
}

/*
 * count how many data transferd
 */
static int nsp_fifo_count(Scsi_Cmnd *SCpnt)
{
	unsigned int base = SCpnt->device->host->io_port;
	unsigned int count;
	unsigned int l, m, h, dummy;

	nsp_index_write(base, POINTERCLR, POINTER_CLEAR | ACK_COUNTER);

	l     = nsp_index_read(base, TRANSFERCOUNT);
	m     = nsp_index_read(base, TRANSFERCOUNT);
	h     = nsp_index_read(base, TRANSFERCOUNT);
	dummy = nsp_index_read(base, TRANSFERCOUNT); /* required this! */

	count = (h << 16) | (m << 8) | (l << 0);

	//nsp_dbg(NSP_DEBUG_DATA_IO, "count=0x%x", count);

	return count;
}

/* fifo size */
#define RFIFO_CRIT 64
#define WFIFO_CRIT 64

/*
 * read data in DATA IN phase
 */
static void nsp_pio_read(Scsi_Cmnd *SCpnt)
{
	unsigned int  base      = SCpnt->device->host->io_port;
	unsigned long mmio_base = SCpnt->device->host->base;
	nsp_hw_data  *data      = (nsp_hw_data *)SCpnt->device->host->hostdata;
	long	      time_out;
	int	      ocount, res;
	unsigned char stat, fifo_stat;

	ocount = data->FifoCount;

	nsp_dbg(NSP_DEBUG_DATA_IO, "in SCpnt=0x%p resid=%d ocount=%d ptr=0x%p this_residual=%d buffers=0x%p nbuf=%d",
		SCpnt, SCpnt->resid, ocount, SCpnt->SCp.ptr, SCpnt->SCp.this_residual, SCpnt->SCp.buffer, SCpnt->SCp.buffers_residual);

	time_out = 1000;

	while ((time_out-- != 0) &&
	       (SCpnt->SCp.this_residual > 0 || SCpnt->SCp.buffers_residual > 0 ) ) {

		stat = nsp_index_read(base, SCSIBUSMON);
		stat &= BUSMON_PHASE_MASK;


		res = nsp_fifo_count(SCpnt) - ocount;
		//nsp_dbg(NSP_DEBUG_DATA_IO, "ptr=0x%p this=0x%x ocount=0x%x res=0x%x", SCpnt->SCp.ptr, SCpnt->SCp.this_residual, ocount, res);
		if (res == 0) { /* if some data avilable ? */
			if (stat == BUSPHASE_DATA_IN) { /* phase changed? */
				//nsp_dbg(NSP_DEBUG_DATA_IO, " wait for data this=%d", SCpnt->SCp.this_residual);
				continue;
			} else {
				nsp_dbg(NSP_DEBUG_DATA_IO, "phase changed stat=0x%x", stat);
				break;
			}
		}

		fifo_stat = nsp_read(base, FIFOSTATUS);
		if ((fifo_stat & FIFOSTATUS_FULL_EMPTY) == 0 &&
		    stat                                == BUSPHASE_DATA_IN) {
			continue;
		}

		res = min(res, SCpnt->SCp.this_residual);

		switch (data->TransferMode) {
		case MODE_IO32:
			res &= ~(BIT(1)|BIT(0)); /* align 4 */
			nsp_fifo32_read(base, SCpnt->SCp.ptr, res >> 2);
			break;
		case MODE_IO8:
			nsp_fifo8_read (base, SCpnt->SCp.ptr, res     );
			break;

		case MODE_MEM32:
			res &= ~(BIT(1)|BIT(0)); /* align 4 */
			nsp_mmio_fifo32_read(mmio_base, SCpnt->SCp.ptr, res >> 2);
			break;

		default:
			nsp_dbg(NSP_DEBUG_DATA_IO, "unknown read mode");
			return;
		}

		SCpnt->resid	       	 -= res;
		SCpnt->SCp.ptr		 += res;
		SCpnt->SCp.this_residual -= res;
		ocount			 += res;
		//nsp_dbg(NSP_DEBUG_DATA_IO, "ptr=0x%p this_residual=0x%x ocount=0x%x", SCpnt->SCp.ptr, SCpnt->SCp.this_residual, ocount);

		/* go to next scatter list if available */
		if (SCpnt->SCp.this_residual	== 0 &&
		    SCpnt->SCp.buffers_residual != 0 ) {
			//nsp_dbg(NSP_DEBUG_DATA_IO, "scatterlist next timeout=%d", time_out);
			SCpnt->SCp.buffers_residual--;
			SCpnt->SCp.buffer++;
			SCpnt->SCp.ptr		 = BUFFER_ADDR;
			SCpnt->SCp.this_residual = SCpnt->SCp.buffer->length;
			time_out = 1000;

			//nsp_dbg(NSP_DEBUG_DATA_IO, "page: 0x%p, off: 0x%x", SCpnt->SCp.buffer->page, SCpnt->SCp.buffer->offset);
		}
	}

	data->FifoCount = ocount;

	if (time_out == 0) {
		nsp_msg(KERN_DEBUG, "pio read timeout resid=%d this_residual=%d buffers_residual=%d",
			SCpnt->resid, SCpnt->SCp.this_residual, SCpnt->SCp.buffers_residual);
	}
	nsp_dbg(NSP_DEBUG_DATA_IO, "read ocount=0x%x", ocount);
	nsp_dbg(NSP_DEBUG_DATA_IO, "r cmd=%d resid=0x%x\n", data->CmdId, SCpnt->resid);
}

/*
 * write data in DATA OUT phase
 */
static void nsp_pio_write(Scsi_Cmnd *SCpnt)
{
	unsigned int  base      = SCpnt->device->host->io_port;
	unsigned long mmio_base = SCpnt->device->host->base;
	nsp_hw_data  *data      = (nsp_hw_data *)SCpnt->device->host->hostdata;
	int	      time_out;
	int           ocount, res;
	unsigned char stat;

	ocount	 = data->FifoCount;

	nsp_dbg(NSP_DEBUG_DATA_IO, "in fifocount=%d ptr=0x%p this_residual=%d buffers=0x%p nbuf=%d resid=0x%x",
		data->FifoCount, SCpnt->SCp.ptr, SCpnt->SCp.this_residual, SCpnt->SCp.buffer, SCpnt->SCp.buffers_residual, SCpnt->resid);

	time_out = 1000;

	while ((time_out-- != 0) &&
	       (SCpnt->SCp.this_residual > 0 || SCpnt->SCp.buffers_residual > 0)) {
		stat = nsp_index_read(base, SCSIBUSMON);
		stat &= BUSMON_PHASE_MASK;

		if (stat != BUSPHASE_DATA_OUT) {
			res = ocount - nsp_fifo_count(SCpnt);

			nsp_dbg(NSP_DEBUG_DATA_IO, "phase changed stat=0x%x, res=%d\n", stat, res);
			/* Put back pointer */
			SCpnt->resid	       	 += res;
			SCpnt->SCp.ptr		 -= res;
			SCpnt->SCp.this_residual += res;
			ocount			 -= res;

			break;
		}

		res = ocount - nsp_fifo_count(SCpnt);
		if (res > 0) { /* write all data? */
			nsp_dbg(NSP_DEBUG_DATA_IO, "wait for all data out. ocount=0x%x res=%d", ocount, res);
			continue;
		}

		res = min(SCpnt->SCp.this_residual, WFIFO_CRIT);

		//nsp_dbg(NSP_DEBUG_DATA_IO, "ptr=0x%p this=0x%x res=0x%x", SCpnt->SCp.ptr, SCpnt->SCp.this_residual, res);
		switch (data->TransferMode) {
		case MODE_IO32:
			res &= ~(BIT(1)|BIT(0)); /* align 4 */
			nsp_fifo32_write(base, SCpnt->SCp.ptr, res >> 2);
			break;
		case MODE_IO8:
			nsp_fifo8_write (base, SCpnt->SCp.ptr, res     );
			break;

		case MODE_MEM32:
			res &= ~(BIT(1)|BIT(0)); /* align 4 */
			nsp_mmio_fifo32_write(mmio_base, SCpnt->SCp.ptr, res >> 2);
			break;

		default:
			nsp_dbg(NSP_DEBUG_DATA_IO, "unknown write mode");
			break;
		}

		SCpnt->resid	       	 -= res;
		SCpnt->SCp.ptr		 += res;
		SCpnt->SCp.this_residual -= res;
		ocount			 += res;

		/* go to next scatter list if available */
		if (SCpnt->SCp.this_residual	== 0 &&
		    SCpnt->SCp.buffers_residual != 0 ) {
			//nsp_dbg(NSP_DEBUG_DATA_IO, "scatterlist next");
			SCpnt->SCp.buffers_residual--;
			SCpnt->SCp.buffer++;
			SCpnt->SCp.ptr		 = BUFFER_ADDR;
			SCpnt->SCp.this_residual = SCpnt->SCp.buffer->length;
			time_out = 1000;
		}
	}

	data->FifoCount = ocount;

	if (time_out == 0) {
		nsp_msg(KERN_DEBUG, "pio write timeout resid=0x%x", SCpnt->resid);
	}
	nsp_dbg(NSP_DEBUG_DATA_IO, "write ocount=0x%x", ocount);
	nsp_dbg(NSP_DEBUG_DATA_IO, "w cmd=%d resid=0x%x\n", data->CmdId, SCpnt->resid);
}
#undef RFIFO_CRIT
#undef WFIFO_CRIT

/*
 * setup synchronous/asynchronous data transfer mode
 */
static int nsp_nexus(Scsi_Cmnd *SCpnt)
{
	unsigned int   base   = SCpnt->device->host->io_port;
	unsigned char  target = scmd_id(SCpnt);
//	unsigned char  lun    = SCpnt->device->lun;
	nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;
	sync_data     *sync   = &(data->Sync[target]);

	//nsp_dbg(NSP_DEBUG_DATA_IO, "in SCpnt=0x%p", SCpnt);

	/* setup synch transfer registers */
	nsp_index_write(base, SYNCREG,	sync->SyncRegister);
	nsp_index_write(base, ACKWIDTH, sync->AckWidth);

	if (SCpnt->use_sg    == 0        ||
	    SCpnt->resid % 4 != 0        ||
	    SCpnt->resid     <= PAGE_SIZE ) {
		data->TransferMode = MODE_IO8;
	} else if (nsp_burst_mode == BURST_MEM32) {
		data->TransferMode = MODE_MEM32;
	} else if (nsp_burst_mode == BURST_IO32) {
		data->TransferMode = MODE_IO32;
	} else {
		data->TransferMode = MODE_IO8;
	}

	/* setup pdma fifo */
	nsp_setup_fifo(data, TRUE);

	/* clear ack counter */
 	data->FifoCount = 0;
	nsp_index_write(base, POINTERCLR, POINTER_CLEAR	    |
					  ACK_COUNTER_CLEAR |
					  REQ_COUNTER_CLEAR |
					  HOST_COUNTER_CLEAR);

	return 0;
}

#include "nsp_message.c"
/*
 * interrupt handler
 */
static irqreturn_t nspintr(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int   base;
	unsigned char  irq_status, irq_phase, phase;
	Scsi_Cmnd     *tmpSC;
	unsigned char  target, lun;
	unsigned int  *sync_neg;
	int            i, tmp;
	nsp_hw_data   *data;


	//nsp_dbg(NSP_DEBUG_INTR, "dev_id=0x%p", dev_id);
	//nsp_dbg(NSP_DEBUG_INTR, "host=0x%p", ((scsi_info_t *)dev_id)->host);

	if (                dev_id        != NULL &&
	    ((scsi_info_t *)dev_id)->host != NULL  ) {
		scsi_info_t *info = (scsi_info_t *)dev_id;

		data = (nsp_hw_data *)info->host->hostdata;
	} else {
		nsp_dbg(NSP_DEBUG_INTR, "host data wrong");
		return IRQ_NONE;
	}

	//nsp_dbg(NSP_DEBUG_INTR, "&nsp_data_base=0x%p, dev_id=0x%p", &nsp_data_base, dev_id);

	base = data->BaseAddress;
	//nsp_dbg(NSP_DEBUG_INTR, "base=0x%x", base);

	/*
	 * interrupt check
	 */
	nsp_write(base, IRQCONTROL, IRQCONTROL_IRQDISABLE);
	irq_status = nsp_read(base, IRQSTATUS);
	//nsp_dbg(NSP_DEBUG_INTR, "irq_status=0x%x", irq_status);
	if ((irq_status == 0xff) || ((irq_status & IRQSTATUS_MASK) == 0)) {
		nsp_write(base, IRQCONTROL, 0);
		//nsp_dbg(NSP_DEBUG_INTR, "no irq/shared irq");
		return IRQ_NONE;
	}

	/* XXX: IMPORTANT
	 * Do not read an irq_phase register if no scsi phase interrupt.
	 * Unless, you should lose a scsi phase interrupt.
	 */
	phase = nsp_index_read(base, SCSIBUSMON);
	if((irq_status & IRQSTATUS_SCSI) != 0) {
		irq_phase = nsp_index_read(base, IRQPHASESENCE);
	} else {
		irq_phase = 0;
	}

	//nsp_dbg(NSP_DEBUG_INTR, "irq_phase=0x%x", irq_phase);

	/*
	 * timer interrupt handler (scsi vs timer interrupts)
	 */
	//nsp_dbg(NSP_DEBUG_INTR, "timercount=%d", data->TimerCount);
	if (data->TimerCount != 0) {
		//nsp_dbg(NSP_DEBUG_INTR, "stop timer");
		nsp_index_write(base, TIMERCOUNT, 0);
		nsp_index_write(base, TIMERCOUNT, 0);
		data->TimerCount = 0;
	}

	if ((irq_status & IRQSTATUS_MASK) == IRQSTATUS_TIMER &&
	    data->SelectionTimeOut == 0) {
		//nsp_dbg(NSP_DEBUG_INTR, "timer start");
		nsp_write(base, IRQCONTROL, IRQCONTROL_TIMER_CLEAR);
		return IRQ_HANDLED;
	}

	nsp_write(base, IRQCONTROL, IRQCONTROL_TIMER_CLEAR | IRQCONTROL_FIFO_CLEAR);

	if ((irq_status & IRQSTATUS_SCSI) &&
	    (irq_phase  & SCSI_RESET_IRQ)) {
		nsp_msg(KERN_ERR, "bus reset (power off?)");

		nsphw_init(data);
		nsp_bus_reset(data);

		if(data->CurrentSC != NULL) {
			tmpSC = data->CurrentSC;
			tmpSC->result  = (DID_RESET                   << 16) |
				         ((tmpSC->SCp.Message & 0xff) <<  8) |
				         ((tmpSC->SCp.Status  & 0xff) <<  0);
			nsp_scsi_done(tmpSC);
		}
		return IRQ_HANDLED;
	}

	if (data->CurrentSC == NULL) {
		nsp_msg(KERN_ERR, "CurrentSC==NULL irq_status=0x%x phase=0x%x irq_phase=0x%x this can't be happen. reset everything", irq_status, phase, irq_phase);
		nsphw_init(data);
		nsp_bus_reset(data);
		return IRQ_HANDLED;
	}

	tmpSC    = data->CurrentSC;
	target   = tmpSC->device->id;
	lun      = tmpSC->device->lun;
	sync_neg = &(data->Sync[target].SyncNegotiation);

	/*
	 * parse hardware SCSI irq reasons register
	 */
	if (irq_status & IRQSTATUS_SCSI) {
		if (irq_phase & RESELECT_IRQ) {
			nsp_dbg(NSP_DEBUG_INTR, "reselect");
			nsp_write(base, IRQCONTROL, IRQCONTROL_RESELECT_CLEAR);
			if (nsp_reselected(tmpSC) != FALSE) {
				return IRQ_HANDLED;
			}
		}

		if ((irq_phase & (PHASE_CHANGE_IRQ | LATCHED_BUS_FREE)) == 0) {
			return IRQ_HANDLED;
		}
	}

	//show_phase(tmpSC);

	switch(tmpSC->SCp.phase) {
	case PH_SELSTART:
		// *sync_neg = SYNC_NOT_YET;
		if ((phase & BUSMON_BSY) == 0) {
			//nsp_dbg(NSP_DEBUG_INTR, "selection count=%d", data->SelectionTimeOut);
			if (data->SelectionTimeOut >= NSP_SELTIMEOUT) {
				nsp_dbg(NSP_DEBUG_INTR, "selection time out");
				data->SelectionTimeOut = 0;
				nsp_index_write(base, SCSIBUSCTRL, 0);

				tmpSC->result   = DID_TIME_OUT << 16;
				nsp_scsi_done(tmpSC);

				return IRQ_HANDLED;
			}
			data->SelectionTimeOut += 1;
			nsp_start_timer(tmpSC, 1000/51);
			return IRQ_HANDLED;
		}

		/* attention assert */
		//nsp_dbg(NSP_DEBUG_INTR, "attention assert");
		data->SelectionTimeOut = 0;
		tmpSC->SCp.phase       = PH_SELECTED;
		nsp_index_write(base, SCSIBUSCTRL, SCSI_ATN);
		udelay(1);
		nsp_index_write(base, SCSIBUSCTRL, SCSI_ATN | AUTODIRECTION | ACKENB);
		return IRQ_HANDLED;

		break;

	case PH_RESELECT:
		//nsp_dbg(NSP_DEBUG_INTR, "phase reselect");
		// *sync_neg = SYNC_NOT_YET;
		if ((phase & BUSMON_PHASE_MASK) != BUSPHASE_MESSAGE_IN) {

			tmpSC->result	= DID_ABORT << 16;
			nsp_scsi_done(tmpSC);
			return IRQ_HANDLED;
		}
		/* fall thru */
	default:
		if ((irq_status & (IRQSTATUS_SCSI | IRQSTATUS_FIFO)) == 0) {
			return IRQ_HANDLED;
		}
		break;
	}

	/*
	 * SCSI sequencer
	 */
	//nsp_dbg(NSP_DEBUG_INTR, "start scsi seq");

	/* normal disconnect */
	if (((tmpSC->SCp.phase == PH_MSG_IN) || (tmpSC->SCp.phase == PH_MSG_OUT)) &&
	    (irq_phase & LATCHED_BUS_FREE) != 0 ) {
		nsp_dbg(NSP_DEBUG_INTR, "normal disconnect irq_status=0x%x, phase=0x%x, irq_phase=0x%x", irq_status, phase, irq_phase);

		//*sync_neg       = SYNC_NOT_YET;

		if ((tmpSC->SCp.Message == MSG_COMMAND_COMPLETE)) {     /* all command complete and return status */
			tmpSC->result = (DID_OK		             << 16) |
					((tmpSC->SCp.Message & 0xff) <<  8) |
					((tmpSC->SCp.Status  & 0xff) <<  0);
			nsp_dbg(NSP_DEBUG_INTR, "command complete result=0x%x", tmpSC->result);
			nsp_scsi_done(tmpSC);

			return IRQ_HANDLED;
		}

		return IRQ_HANDLED;
	}


	/* check unexpected bus free state */
	if (phase == 0) {
		nsp_msg(KERN_DEBUG, "unexpected bus free. irq_status=0x%x, phase=0x%x, irq_phase=0x%x", irq_status, phase, irq_phase);

		*sync_neg       = SYNC_NG;
		tmpSC->result   = DID_ERROR << 16;
		nsp_scsi_done(tmpSC);
		return IRQ_HANDLED;
	}

	switch (phase & BUSMON_PHASE_MASK) {
	case BUSPHASE_COMMAND:
		nsp_dbg(NSP_DEBUG_INTR, "BUSPHASE_COMMAND");
		if ((phase & BUSMON_REQ) == 0) {
			nsp_dbg(NSP_DEBUG_INTR, "REQ == 0");
			return IRQ_HANDLED;
		}

		tmpSC->SCp.phase = PH_COMMAND;

		nsp_nexus(tmpSC);

		/* write scsi command */
		nsp_dbg(NSP_DEBUG_INTR, "cmd_len=%d", tmpSC->cmd_len);
		nsp_index_write(base, COMMANDCTRL, CLEAR_COMMAND_POINTER);
		for (i = 0; i < tmpSC->cmd_len; i++) {
			nsp_index_write(base, COMMANDDATA, tmpSC->cmnd[i]);
		}
		nsp_index_write(base, COMMANDCTRL, CLEAR_COMMAND_POINTER | AUTO_COMMAND_GO);
		break;

	case BUSPHASE_DATA_OUT:
		nsp_dbg(NSP_DEBUG_INTR, "BUSPHASE_DATA_OUT");

		tmpSC->SCp.phase        = PH_DATA;
		tmpSC->SCp.have_data_in = IO_OUT;

		nsp_pio_write(tmpSC);

		break;

	case BUSPHASE_DATA_IN:
		nsp_dbg(NSP_DEBUG_INTR, "BUSPHASE_DATA_IN");

		tmpSC->SCp.phase        = PH_DATA;
		tmpSC->SCp.have_data_in = IO_IN;

		nsp_pio_read(tmpSC);

		break;

	case BUSPHASE_STATUS:
		nsp_dataphase_bypass(tmpSC);
		nsp_dbg(NSP_DEBUG_INTR, "BUSPHASE_STATUS");

		tmpSC->SCp.phase = PH_STATUS;

		tmpSC->SCp.Status = nsp_index_read(base, SCSIDATAWITHACK);
		nsp_dbg(NSP_DEBUG_INTR, "message=0x%x status=0x%x", tmpSC->SCp.Message, tmpSC->SCp.Status);

		break;

	case BUSPHASE_MESSAGE_OUT:
		nsp_dbg(NSP_DEBUG_INTR, "BUSPHASE_MESSAGE_OUT");
		if ((phase & BUSMON_REQ) == 0) {
			goto timer_out;
		}

		tmpSC->SCp.phase = PH_MSG_OUT;

		//*sync_neg = SYNC_NOT_YET;

		data->MsgLen = i = 0;
		data->MsgBuffer[i] = IDENTIFY(TRUE, lun); i++;

		if (*sync_neg == SYNC_NOT_YET) {
			data->Sync[target].SyncPeriod = 0;
			data->Sync[target].SyncOffset = 0;

			/**/
			data->MsgBuffer[i] = MSG_EXTENDED; i++;
			data->MsgBuffer[i] = 3;            i++;
			data->MsgBuffer[i] = MSG_EXT_SDTR; i++;
			data->MsgBuffer[i] = 0x0c;         i++;
			data->MsgBuffer[i] = 15;           i++;
			/**/
		}
		data->MsgLen = i;

		nsp_analyze_sdtr(tmpSC);
		show_message(data);
		nsp_message_out(tmpSC);
		break;

	case BUSPHASE_MESSAGE_IN:
		nsp_dataphase_bypass(tmpSC);
		nsp_dbg(NSP_DEBUG_INTR, "BUSPHASE_MESSAGE_IN");
		if ((phase & BUSMON_REQ) == 0) {
			goto timer_out;
		}

		tmpSC->SCp.phase = PH_MSG_IN;
		nsp_message_in(tmpSC);

		/**/
		if (*sync_neg == SYNC_NOT_YET) {
			//nsp_dbg(NSP_DEBUG_INTR, "sync target=%d,lun=%d",target,lun);

			if (data->MsgLen       >= 5            &&
			    data->MsgBuffer[0] == MSG_EXTENDED &&
			    data->MsgBuffer[1] == 3            &&
			    data->MsgBuffer[2] == MSG_EXT_SDTR ) {
				data->Sync[target].SyncPeriod = data->MsgBuffer[3];
				data->Sync[target].SyncOffset = data->MsgBuffer[4];
				//nsp_dbg(NSP_DEBUG_INTR, "sync ok, %d %d", data->MsgBuffer[3], data->MsgBuffer[4]);
				*sync_neg = SYNC_OK;
			} else {
				data->Sync[target].SyncPeriod = 0;
				data->Sync[target].SyncOffset = 0;
				*sync_neg = SYNC_NG;
			}
			nsp_analyze_sdtr(tmpSC);
		}
		/**/

		/* search last messeage byte */
		tmp = -1;
		for (i = 0; i < data->MsgLen; i++) {
			tmp = data->MsgBuffer[i];
			if (data->MsgBuffer[i] == MSG_EXTENDED) {
				i += (1 + data->MsgBuffer[i+1]);
			}
		}
		tmpSC->SCp.Message = tmp;

		nsp_dbg(NSP_DEBUG_INTR, "message=0x%x len=%d", tmpSC->SCp.Message, data->MsgLen);
		show_message(data);

		break;

	case BUSPHASE_SELECT:
	default:
		nsp_dbg(NSP_DEBUG_INTR, "BUSPHASE other");

		break;
	}

	//nsp_dbg(NSP_DEBUG_INTR, "out");
	return IRQ_HANDLED; 	

timer_out:
	nsp_start_timer(tmpSC, 1000/102);
	return IRQ_HANDLED;
}

#ifdef NSP_DEBUG
#include "nsp_debug.c"
#endif	/* NSP_DEBUG */

/*----------------------------------------------------------------*/
/* look for ninja3 card and init if found			  */
/*----------------------------------------------------------------*/
static struct Scsi_Host *nsp_detect(struct scsi_host_template *sht)
{
	struct Scsi_Host *host;	/* registered host structure */
	nsp_hw_data *data_b = &nsp_data_base, *data;

	nsp_dbg(NSP_DEBUG_INIT, "this_id=%d", sht->this_id);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,73))
	host = scsi_host_alloc(&nsp_driver_template, sizeof(nsp_hw_data));
#else
	host = scsi_register(sht, sizeof(nsp_hw_data));
#endif
	if (host == NULL) {
		nsp_dbg(NSP_DEBUG_INIT, "host failed");
		return NULL;
	}

	memcpy(host->hostdata, data_b, sizeof(nsp_hw_data));
	data = (nsp_hw_data *)host->hostdata;
	data->ScsiInfo->host = host;
#ifdef NSP_DEBUG
	data->CmdId = 0;
#endif

	nsp_dbg(NSP_DEBUG_INIT, "irq=%d,%d", data_b->IrqNumber, ((nsp_hw_data *)host->hostdata)->IrqNumber);

	host->unique_id	  = data->BaseAddress;
	host->io_port	  = data->BaseAddress;
	host->n_io_port	  = data->NumAddress;
	host->irq	  = data->IrqNumber;
	host->base        = data->MmioAddress;

	spin_lock_init(&(data->Lock));

	snprintf(data->nspinfo,
		 sizeof(data->nspinfo),
		 "NinjaSCSI-3/32Bi Driver $Revision: 1.23 $ IO:0x%04lx-0x%04lx MMIO(virt addr):0x%04lx IRQ:%02d",
		 host->io_port, host->io_port + host->n_io_port - 1,
		 host->base,
		 host->irq);
	sht->name	  = data->nspinfo;

	nsp_dbg(NSP_DEBUG_INIT, "end");


	return host; /* detect done. */
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
static int nsp_detect_old(struct scsi_host_template *sht)
{
	if (nsp_detect(sht) == NULL) {
		return 0;
	} else {
		//MOD_INC_USE_COUNT;
		return 1;
	}
}


static int nsp_release_old(struct Scsi_Host *shpnt)
{
	//nsp_hw_data *data = (nsp_hw_data *)shpnt->hostdata;

	/* PCMCIA Card Service dose same things below. */
	/* So we do nothing.                           */
	//if (shpnt->irq) {
	//	free_irq(shpnt->irq, data->ScsiInfo);
	//}
	//if (shpnt->io_port) {
	//	release_region(shpnt->io_port, shpnt->n_io_port);
	//}

	//MOD_DEC_USE_COUNT;

	return 0;
}
#endif

/*----------------------------------------------------------------*/
/* return info string						  */
/*----------------------------------------------------------------*/
static const char *nsp_info(struct Scsi_Host *shpnt)
{
	nsp_hw_data *data = (nsp_hw_data *)shpnt->hostdata;

	return data->nspinfo;
}

#undef SPRINTF
#define SPRINTF(args...) \
        do { \
		if(length > (pos - buffer)) { \
			pos += snprintf(pos, length - (pos - buffer) + 1, ## args); \
			nsp_dbg(NSP_DEBUG_PROC, "buffer=0x%p pos=0x%p length=%d %d\n", buffer, pos, length,  length - (pos - buffer));\
		} \
	} while(0)
static int
nsp_proc_info(
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,73))
	struct Scsi_Host *host,
#endif
	char  *buffer,
	char **start,
	off_t  offset,
	int    length,
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,5,73))
	int    hostno,
#endif
	int    inout)
{
	int id;
	char *pos = buffer;
	int thislength;
	int speed;
	unsigned long flags;
	nsp_hw_data *data;
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,5,73))
	struct Scsi_Host *host;
#else
	int hostno;
#endif
	if (inout) {
		return -EINVAL;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,73))
	hostno = host->host_no;
#else
	/* search this HBA host */
	host = scsi_host_hn_get(hostno);
	if (host == NULL) {
		return -ESRCH;
	}
#endif
	data = (nsp_hw_data *)host->hostdata;


	SPRINTF("NinjaSCSI status\n\n");
	SPRINTF("Driver version:        $Revision: 1.23 $\n");
	SPRINTF("SCSI host No.:         %d\n",          hostno);
	SPRINTF("IRQ:                   %d\n",          host->irq);
	SPRINTF("IO:                    0x%lx-0x%lx\n", host->io_port, host->io_port + host->n_io_port - 1);
	SPRINTF("MMIO(virtual address): 0x%lx-0x%lx\n", host->base, host->base + data->MmioLength - 1);
	SPRINTF("sg_tablesize:          %d\n",          host->sg_tablesize);

	SPRINTF("burst transfer mode:   ");
	switch (nsp_burst_mode) {
	case BURST_IO8:
		SPRINTF("io8");
		break;
	case BURST_IO32:
		SPRINTF("io32");
		break;
	case BURST_MEM32:
		SPRINTF("mem32");
		break;
	default:
		SPRINTF("???");
		break;
	}
	SPRINTF("\n");


	spin_lock_irqsave(&(data->Lock), flags);
	SPRINTF("CurrentSC:             0x%p\n\n",      data->CurrentSC);
	spin_unlock_irqrestore(&(data->Lock), flags);

	SPRINTF("SDTR status\n");
	for(id = 0; id < ARRAY_SIZE(data->Sync); id++) {

		SPRINTF("id %d: ", id);

		if (id == host->this_id) {
			SPRINTF("----- NinjaSCSI-3 host adapter\n");
			continue;
		}

		switch(data->Sync[id].SyncNegotiation) {
		case SYNC_OK:
			SPRINTF(" sync");
			break;
		case SYNC_NG:
			SPRINTF("async");
			break;
		case SYNC_NOT_YET:
			SPRINTF(" none");
			break;
		default:
			SPRINTF("?????");
			break;
		}

		if (data->Sync[id].SyncPeriod != 0) {
			speed = 1000000 / (data->Sync[id].SyncPeriod * 4);

			SPRINTF(" transfer %d.%dMB/s, offset %d",
				speed / 1000,
				speed % 1000,
				data->Sync[id].SyncOffset
				);
		}
		SPRINTF("\n");
	}

	thislength = pos - (buffer + offset);

	if(thislength < 0) {
		*start = NULL;
                return 0;
        }


	thislength = min(thislength, length);
	*start = buffer + offset;

	return thislength;
}
#undef SPRINTF

/*---------------------------------------------------------------*/
/* error handler                                                 */
/*---------------------------------------------------------------*/

/*
static int nsp_eh_abort(Scsi_Cmnd *SCpnt)
{
	nsp_dbg(NSP_DEBUG_BUSRESET, "SCpnt=0x%p", SCpnt);

	return nsp_eh_bus_reset(SCpnt);
}*/

static int nsp_bus_reset(nsp_hw_data *data)
{
	unsigned int base = data->BaseAddress;
	int	     i;

	nsp_write(base, IRQCONTROL, IRQCONTROL_ALLMASK);

	nsp_index_write(base, SCSIBUSCTRL, SCSI_RST);
	mdelay(100); /* 100ms */
	nsp_index_write(base, SCSIBUSCTRL, 0);
	for(i = 0; i < 5; i++) {
		nsp_index_read(base, IRQPHASESENCE); /* dummy read */
	}

	nsphw_init_sync(data);

	nsp_write(base, IRQCONTROL, IRQCONTROL_ALLCLEAR);

	return SUCCESS;
}

static int nsp_eh_bus_reset(Scsi_Cmnd *SCpnt)
{
	nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;

	nsp_dbg(NSP_DEBUG_BUSRESET, "SCpnt=0x%p", SCpnt);

	return nsp_bus_reset(data);
}

static int nsp_eh_host_reset(Scsi_Cmnd *SCpnt)
{
	nsp_hw_data *data = (nsp_hw_data *)SCpnt->device->host->hostdata;

	nsp_dbg(NSP_DEBUG_BUSRESET, "in");

	nsphw_init(data);

	return SUCCESS;
}


/**********************************************************************
  PCMCIA functions
**********************************************************************/

/*======================================================================
    nsp_cs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.
======================================================================*/
static dev_link_t *nsp_cs_attach(void)
{
	scsi_info_t  *info;
	client_reg_t  client_reg;
	dev_link_t   *link;
	int	      ret;
	nsp_hw_data  *data = &nsp_data_base;

	nsp_dbg(NSP_DEBUG_INIT, "in");

	/* Create new SCSI device */
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL) { return NULL; }
	memset(info, 0, sizeof(*info));
	link = &info->link;
	link->priv = info;
	data->ScsiInfo = info;

	nsp_dbg(NSP_DEBUG_INIT, "info=0x%p", info);

	/* The io structure describes IO port mapping */
	link->io.NumPorts1	 = 0x10;
	link->io.Attributes1	 = IO_DATA_PATH_WIDTH_AUTO;
	link->io.IOAddrLines	 = 10;	/* not used */

	/* Interrupt setup */
	link->irq.Attributes	 = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
	link->irq.IRQInfo1	 = IRQ_LEVEL_ID;

	/* Interrupt handler */
	link->irq.Handler	 = &nspintr;
	link->irq.Instance       = info;
	link->irq.Attributes     |= (SA_SHIRQ | SA_SAMPLE_RANDOM);

	/* General socket configuration */
	link->conf.Attributes	 = CONF_ENABLE_IRQ;
	link->conf.Vcc		 = 50;
	link->conf.IntType	 = INT_MEMORY_AND_IO;
	link->conf.Present	 = PRESENT_OPTION;


	/* Register with Card Services */
	link->next               = dev_list;
	dev_list                 = link;
	client_reg.dev_info	 = &dev_info;
	client_reg.Version	 = 0x0210;
	client_reg.event_callback_args.client_data = link;
	ret = pcmcia_register_client(&link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		nsp_cs_detach(link);
		return NULL;
	}


	nsp_dbg(NSP_DEBUG_INIT, "link=0x%p", link);
	return link;
} /* nsp_cs_attach */


/*======================================================================
    This deletes a driver "instance".  The device is de-registered
    with Card Services.	 If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.
======================================================================*/
static void nsp_cs_detach(dev_link_t *link)
{
	dev_link_t **linkp;

	nsp_dbg(NSP_DEBUG_INIT, "in, link=0x%p", link);

	/* Locate device structure */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next) {
		if (*linkp == link) {
			break;
		}
	}
	if (*linkp == NULL) {
		return;
	}

	if (link->state & DEV_CONFIG)
		nsp_cs_release(link);

	/* Break the link with Card Services */
	if (link->handle) {
		pcmcia_deregister_client(link->handle);
	}

	/* Unlink device structure, free bits */
	*linkp = link->next;
	kfree(link->priv);
	link->priv = NULL;

} /* nsp_cs_detach */


/*======================================================================
    nsp_cs_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.
======================================================================*/
#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)
/*====================================================================*/
static void nsp_cs_config(dev_link_t *link)
{
	client_handle_t	  handle = link->handle;
	scsi_info_t	 *info	 = link->priv;
	tuple_t		  tuple;
	cisparse_t	  parse;
	int		  last_ret, last_fn;
	unsigned char	  tuple_data[64];
	config_info_t	  conf;
	win_req_t         req;
	memreq_t          map;
	cistpl_cftable_entry_t dflt = { 0 };
	struct Scsi_Host *host;
	nsp_hw_data      *data = &nsp_data_base;
#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,74))
	struct scsi_device	 *dev;
	dev_node_t	**tail, *node;
#endif

	nsp_dbg(NSP_DEBUG_INIT, "in");

	tuple.DesiredTuple    = CISTPL_CONFIG;
	tuple.Attributes      = 0;
	tuple.TupleData	      = tuple_data;
	tuple.TupleDataMax    = sizeof(tuple_data);
	tuple.TupleOffset     = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData,	pcmcia_get_tuple_data(handle, &tuple));
	CS_CHECK(ParseTuple,	pcmcia_parse_tuple(handle, &tuple, &parse));
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present    = parse.config.rmask[0];

	/* Configure card */
	link->state	      |= DEV_CONFIG;

	/* Look up the current Vcc */
	CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(handle, &conf));
	link->conf.Vcc = conf.Vcc;

	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	while (1) {
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);

		if (pcmcia_get_tuple_data(handle, &tuple) != 0 ||
				pcmcia_parse_tuple(handle, &tuple, &parse) != 0)
			goto next_entry;

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT) { dflt = *cfg; }
		if (cfg->index == 0) { goto next_entry; }
		link->conf.ConfigIndex = cfg->index;

		/* Does this card need audio output? */
		if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
			link->conf.Attributes |= CONF_ENABLE_SPKR;
			link->conf.Status = CCSR_AUDIO_ENA;
		}

		/* Use power settings for Vcc and Vpp if present */
		/*  Note that the CIS values need to be rescaled */
		if (cfg->vcc.present & (1<<CISTPL_POWER_VNOM)) {
			if (conf.Vcc != cfg->vcc.param[CISTPL_POWER_VNOM]/10000) {
				goto next_entry;
			}
		} else if (dflt.vcc.present & (1<<CISTPL_POWER_VNOM)) {
			if (conf.Vcc != dflt.vcc.param[CISTPL_POWER_VNOM]/10000) {
				goto next_entry;
			}
		}

		if (cfg->vpp1.present & (1 << CISTPL_POWER_VNOM)) {
			link->conf.Vpp1 = link->conf.Vpp2 =
				cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
		} else if (dflt.vpp1.present & (1 << CISTPL_POWER_VNOM)) {
			link->conf.Vpp1 = link->conf.Vpp2 =
				dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;
		}

		/* Do we need to allocate an interrupt? */
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1) {
			link->conf.Attributes |= CONF_ENABLE_IRQ;
		}

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 = link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
			/* This reserves IO space but doesn't actually enable it */
			if (pcmcia_request_io(link->handle, &link->io) != 0)
				goto next_entry;
		}

		if ((cfg->mem.nwin > 0) || (dflt.mem.nwin > 0)) {
			cistpl_mem_t *mem =
				(cfg->mem.nwin) ? &cfg->mem : &dflt.mem;
			req.Attributes = WIN_DATA_WIDTH_16|WIN_MEMORY_TYPE_CM;
			req.Attributes |= WIN_ENABLE;
			req.Base = mem->win[0].host_addr;
			req.Size = mem->win[0].len;
			if (req.Size < 0x1000) {
				req.Size = 0x1000;
			}
			req.AccessSpeed = 0;
			if (pcmcia_request_window(&link->handle, &req, &link->win) != 0)
				goto next_entry;
			map.Page = 0; map.CardOffset = mem->win[0].card_addr;
			if (pcmcia_map_mem_page(link->win, &map) != 0)
				goto next_entry;

			data->MmioAddress = (unsigned long)ioremap_nocache(req.Base, req.Size);
			data->MmioLength  = req.Size;
		}
		/* If we got this far, we're cool! */
		break;

	next_entry:
		nsp_dbg(NSP_DEBUG_INIT, "next");

		if (link->io.NumPorts1) {
			pcmcia_release_io(link->handle, &link->io);
		}
		CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(handle, &tuple));
	}

	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		CS_CHECK(RequestIRQ, pcmcia_request_irq(link->handle, &link->irq));
	}
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(handle, &link->conf));

	if (free_ports) {
		if (link->io.BasePort1) {
			release_region(link->io.BasePort1, link->io.NumPorts1);
		}
		if (link->io.BasePort2) {
			release_region(link->io.BasePort2, link->io.NumPorts2);
		}
	}

	/* Set port and IRQ */
	data->BaseAddress = link->io.BasePort1;
	data->NumAddress  = link->io.NumPorts1;
	data->IrqNumber   = link->irq.AssignedIRQ;

	nsp_dbg(NSP_DEBUG_INIT, "I/O[0x%x+0x%x] IRQ %d",
		data->BaseAddress, data->NumAddress, data->IrqNumber);

	if(nsphw_init(data) == FALSE) {
		goto cs_failed;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,2))
	host = nsp_detect(&nsp_driver_template);
#else
	scsi_register_host(&nsp_driver_template);
	for (host = scsi_host_get_next(NULL); host != NULL;
	     host = scsi_host_get_next(host)) {
		if (host->hostt == &nsp_driver_template) {
			break;
		}
	}
#endif

	if (host == NULL) {
		nsp_dbg(NSP_DEBUG_INIT, "detect failed");
		goto cs_failed;
	}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,74))
	scsi_add_host (host, NULL);
	scsi_scan_host(host);

	snprintf(info->node.dev_name, sizeof(info->node.dev_name), "scsi%d", host->host_no);
	link->dev  = &info->node;
	info->host = host;

#else
	nsp_dbg(NSP_DEBUG_INIT, "GET_SCSI_INFO");
	tail = &link->dev;
	info->ndev = 0;

	nsp_dbg(NSP_DEBUG_INIT, "host=0x%p", host);

	for (dev = host->host_queue; dev != NULL; dev = dev->next) {
		unsigned long id;
		id = (dev->id & 0x0f) + ((dev->lun & 0x0f) << 4) +
			((dev->channel & 0x0f) << 8) +
			((dev->host->host_no & 0x0f) << 12);
		node = &info->node[info->ndev];
		node->minor = 0;
		switch (dev->type) {
		case TYPE_TAPE:
			node->major = SCSI_TAPE_MAJOR;
			snprintf(node->dev_name, sizeof(node->dev_name), "st#%04lx", id);
			break;
		case TYPE_DISK:
		case TYPE_MOD:
			node->major = SCSI_DISK0_MAJOR;
			snprintf(node->dev_name, sizeof(node->dev_name), "sd#%04lx", id);
			break;
		case TYPE_ROM:
		case TYPE_WORM:
			node->major = SCSI_CDROM_MAJOR;
			snprintf(node->dev_name, sizeof(node->dev_name), "sr#%04lx", id);
			break;
		default:
			node->major = SCSI_GENERIC_MAJOR;
			snprintf(node->dev_name, sizeof(node->dev_name), "sg#%04lx", id);
			break;
		}
		*tail = node; tail = &node->next;
		info->ndev++;
		info->host = dev->host;
	}

	*tail = NULL;
	if (info->ndev == 0) {
		nsp_msg(KERN_INFO, "no SCSI devices found");
	}
	nsp_dbg(NSP_DEBUG_INIT, "host=0x%p", host);
#endif

	/* Finally, report what we've done */
	printk(KERN_INFO "nsp_cs: index 0x%02x: Vcc %d.%d",
	       link->conf.ConfigIndex,
	       link->conf.Vcc/10, link->conf.Vcc%10);
	if (link->conf.Vpp1) {
		printk(", Vpp %d.%d", link->conf.Vpp1/10, link->conf.Vpp1%10);
	}
	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		printk(", irq %d", link->irq.AssignedIRQ);
	}
	if (link->io.NumPorts1) {
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1+link->io.NumPorts1-1);
	}
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2+link->io.NumPorts2-1);
	if (link->win)
		printk(", mem 0x%06lx-0x%06lx", req.Base,
		       req.Base+req.Size-1);
	printk("\n");

	link->state &= ~DEV_CONFIG_PENDING;
	return;

 cs_failed:
	nsp_dbg(NSP_DEBUG_INIT, "config fail");
	cs_error(link->handle, last_fn, last_ret);
	nsp_cs_release(link);

	return;
} /* nsp_cs_config */
#undef CS_CHECK


/*======================================================================
    After a card is removed, nsp_cs_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
======================================================================*/
static void nsp_cs_release(dev_link_t *link)
{
	scsi_info_t *info = link->priv;
	nsp_hw_data *data = NULL;

	if (info->host == NULL) {
		nsp_msg(KERN_DEBUG, "unexpected card release call.");
	} else {
		data = (nsp_hw_data *)info->host->hostdata;
	}

	nsp_dbg(NSP_DEBUG_INIT, "link=0x%p", link);

	/* Unlink the device chain */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,2))
	if (info->host != NULL) {
		scsi_remove_host(info->host);
	}
#else
	scsi_unregister_host(&nsp_driver_template);
#endif
	link->dev = NULL;

	if (link->win) {
		if (data != NULL) {
			iounmap((void *)(data->MmioAddress));
		}
		pcmcia_release_window(link->win);
	}
	pcmcia_release_configuration(link->handle);
	if (link->io.NumPorts1) {
		pcmcia_release_io(link->handle, &link->io);
	}
	if (link->irq.AssignedIRQ) {
		pcmcia_release_irq(link->handle, &link->irq);
	}
	link->state &= ~DEV_CONFIG;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,2))
	if (info->host != NULL) {
		scsi_host_put(info->host);
	}
#endif
} /* nsp_cs_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.  A CARD_REMOVAL event
    also sets some flags to discourage the net drivers from trying
    to talk to the card any more.

    When a CARD_REMOVAL event is received, we immediately set a flag
    to block future accesses to this device.  All the functions that
    actually access the device should check this flag to make sure
    the card is still present.

======================================================================*/
static int nsp_cs_event(event_t		       event,
			int		       priority,
			event_callback_args_t *args)
{
	dev_link_t  *link = args->client_data;
	scsi_info_t *info = link->priv;
	nsp_hw_data *data;

	nsp_dbg(NSP_DEBUG_INIT, "in, event=0x%08x", event);

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		nsp_dbg(NSP_DEBUG_INIT, "event: remove");
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			((scsi_info_t *)link->priv)->stop = 1;
			nsp_cs_release(link);
		}
		break;

	case CS_EVENT_CARD_INSERTION:
		nsp_dbg(NSP_DEBUG_INIT, "event: insert");
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,68))
		info->bus    =  args->bus;
#endif
		nsp_cs_config(link);
		break;

	case CS_EVENT_PM_SUSPEND:
		nsp_dbg(NSP_DEBUG_INIT, "event: suspend");
		link->state |= DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		/* Mark the device as stopped, to block IO until later */
		nsp_dbg(NSP_DEBUG_INIT, "event: reset physical");

		if (info->host != NULL) {
			nsp_msg(KERN_INFO, "clear SDTR status");

			data = (nsp_hw_data *)info->host->hostdata;

			nsphw_init_sync(data);
		}

		info->stop = 1;
		if (link->state & DEV_CONFIG) {
			pcmcia_release_configuration(link->handle);
		}
		break;

	case CS_EVENT_PM_RESUME:
		nsp_dbg(NSP_DEBUG_INIT, "event: resume");
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		nsp_dbg(NSP_DEBUG_INIT, "event: reset");
		if (link->state & DEV_CONFIG) {
			pcmcia_request_configuration(link->handle, &link->conf);
		}
		info->stop = 0;

		if (info->host != NULL) {
			nsp_msg(KERN_INFO, "reset host and bus");

			data = (nsp_hw_data *)info->host->hostdata;

			nsphw_init   (data);
			nsp_bus_reset(data);
		}

		break;

	default:
		nsp_dbg(NSP_DEBUG_INIT, "event: unknown");
		break;
	}
	nsp_dbg(NSP_DEBUG_INIT, "end");
	return 0;
} /* nsp_cs_event */

/*======================================================================*
 *	module entry point
 *====================================================================*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,68))
static struct pcmcia_device_id nsp_cs_ids[] = {
	PCMCIA_DEVICE_PROD_ID123("IO DATA", "CBSC16       ", "1", 0x547e66dc, 0x0d63a3fd, 0x51de003a),
	PCMCIA_DEVICE_PROD_ID123("KME    ", "SCSI-CARD-001", "1", 0x534c02bc, 0x52008408, 0x51de003a),
	PCMCIA_DEVICE_PROD_ID123("KME    ", "SCSI-CARD-002", "1", 0x534c02bc, 0xcb09d5b2, 0x51de003a),
	PCMCIA_DEVICE_PROD_ID123("KME    ", "SCSI-CARD-003", "1", 0x534c02bc, 0xbc0ee524, 0x51de003a),
	PCMCIA_DEVICE_PROD_ID123("KME    ", "SCSI-CARD-004", "1", 0x534c02bc, 0x226a7087, 0x51de003a),
	PCMCIA_DEVICE_PROD_ID123("WBT", "NinjaSCSI-3", "R1.0", 0xc7ba805f, 0xfdc7c97d, 0x6973710e),
	PCMCIA_DEVICE_PROD_ID123("WORKBIT", "UltraNinja-16", "1", 0x28191418, 0xb70f4b09, 0x51de003a),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, nsp_cs_ids);

static struct pcmcia_driver nsp_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "nsp_cs",
	},
	.attach		= nsp_cs_attach,
	.event		= nsp_cs_event,
	.detach		= nsp_cs_detach,
	.id_table	= nsp_cs_ids,
};
#endif

static int __init nsp_cs_init(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,68))
	nsp_msg(KERN_INFO, "loading...");

	return pcmcia_register_driver(&nsp_driver);
#else
	servinfo_t serv;

	nsp_msg(KERN_INFO, "loading...");
	pcmcia_get_card_services_info(&serv);
	if (serv.Revision != CS_RELEASE_CODE) {
		nsp_msg(KERN_DEBUG, "Card Services release does not match!");
		return -EINVAL;
	}
	register_pcmcia_driver(&dev_info, &nsp_cs_attach, &nsp_cs_detach);

	nsp_dbg(NSP_DEBUG_INIT, "out");
	return 0;
#endif
}

static void __exit nsp_cs_exit(void)
{
	nsp_msg(KERN_INFO, "unloading...");

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,68))
	pcmcia_unregister_driver(&nsp_driver);
	BUG_ON(dev_list != NULL);
#else
	unregister_pcmcia_driver(&dev_info);
	/* XXX: this really needs to move into generic code.. */
	while (dev_list != NULL) {
		if (dev_list->state & DEV_CONFIG) {
			nsp_cs_release(dev_list);
		}
		nsp_cs_detach(dev_list);
	}
#endif
}


module_init(nsp_cs_init)
module_exit(nsp_cs_exit)

/* end */
