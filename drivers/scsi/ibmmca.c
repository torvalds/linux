/*
 Low Level Linux Driver for the IBM Microchannel SCSI Subsystem for
 Linux Kernel >= 2.4.0.
 Copyright (c) 1995 Strom Systems, Inc. under the terms of the GNU
 General Public License. Written by Martin Kolinek, December 1995.
 Further development by: Chris Beauregard, Klaus Kudielka, Michael Lang
 See the file Documentation/scsi/ibmmca.txt for a detailed description
 of this driver, the commandline arguments and the history of its
 development.
 See the WWW-page: http://www.uni-mainz.de/~langm000/linux.html for latest
 updates, info and ADF-files for adapters supported by this driver.

 Alan Cox <alan@redhat.com>
 Updated for Linux 2.5.45 to use the new error handler, cleaned up the
 lock macros and did a few unavoidable locking tweaks, plus one locking
 fix in the irq and completion path.
 
 */

#include <linux/config.h>
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,45)
#error "This driver works only with kernel 2.5.45 or higher!"
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mca.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/mca-legacy.h>

#include <asm/system.h>
#include <asm/io.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "ibmmca.h"

/* current version of this driver-source: */
#define IBMMCA_SCSI_DRIVER_VERSION "4.0b-ac"

/* driver configuration */
#define IM_MAX_HOSTS     8	/* maximum number of host adapters */
#define IM_RESET_DELAY	60	/* seconds allowed for a reset */

/* driver debugging - #undef all for normal operation */
/* if defined: count interrupts and ignore this special one: */
#undef	IM_DEBUG_TIMEOUT	//50
#define TIMEOUT_PUN	0
#define TIMEOUT_LUN	0
/* verbose interrupt: */
#undef IM_DEBUG_INT
/* verbose queuecommand: */
#undef IM_DEBUG_CMD
/* verbose queucommand for specific SCSI-device type: */
#undef IM_DEBUG_CMD_SPEC_DEV
/* verbose device probing */
#undef IM_DEBUG_PROBE

/* device type that shall be displayed on syslog (only during debugging): */
#define IM_DEBUG_CMD_DEVICE	TYPE_TAPE

/* relative addresses of hardware registers on a subsystem */
#define IM_CMD_REG(hi)	(hosts[(hi)]->io_port)	/*Command Interface, (4 bytes long) */
#define IM_ATTN_REG(hi)	(hosts[(hi)]->io_port+4)	/*Attention (1 byte) */
#define IM_CTR_REG(hi)	(hosts[(hi)]->io_port+5)	/*Basic Control (1 byte) */
#define IM_INTR_REG(hi)	(hosts[(hi)]->io_port+6)	/*Interrupt Status (1 byte, r/o) */
#define IM_STAT_REG(hi)	(hosts[(hi)]->io_port+7)	/*Basic Status (1 byte, read only) */

/* basic I/O-port of first adapter */
#define IM_IO_PORT	0x3540
/* maximum number of hosts that can be found */
#define IM_N_IO_PORT	8

/*requests going into the upper nibble of the Attention register */
/*note: the lower nibble specifies the device(0-14), or subsystem(15) */
#define IM_IMM_CMD	0x10	/*immediate command */
#define IM_SCB		0x30	/*Subsystem Control Block command */
#define IM_LONG_SCB	0x40	/*long Subsystem Control Block command */
#define IM_EOI		0xe0	/*end-of-interrupt request */

/*values for bits 7,1,0 of Basic Control reg. (bits 6-2 reserved) */
#define IM_HW_RESET	0x80	/*hardware reset */
#define IM_ENABLE_DMA	0x02	/*enable subsystem's busmaster DMA */
#define IM_ENABLE_INTR	0x01	/*enable interrupts to the system */

/*to interpret the upper nibble of Interrupt Status register */
/*note: the lower nibble specifies the device(0-14), or subsystem(15) */
#define IM_SCB_CMD_COMPLETED			0x10
#define IM_SCB_CMD_COMPLETED_WITH_RETRIES	0x50
#define IM_LOOP_SCATTER_BUFFER_FULL		0x60
#define IM_ADAPTER_HW_FAILURE			0x70
#define IM_IMMEDIATE_CMD_COMPLETED		0xa0
#define IM_CMD_COMPLETED_WITH_FAILURE		0xc0
#define IM_CMD_ERROR				0xe0
#define IM_SOFTWARE_SEQUENCING_ERROR		0xf0

/*to interpret bits 3-0 of Basic Status register (bits 7-4 reserved) */
#define IM_CMD_REG_FULL		0x08
#define IM_CMD_REG_EMPTY	0x04
#define IM_INTR_REQUEST		0x02
#define IM_BUSY			0x01

/*immediate commands (word written into low 2 bytes of command reg) */
#define IM_RESET_IMM_CMD	0x0400
#define IM_FEATURE_CTR_IMM_CMD	0x040c
#define IM_DMA_PACING_IMM_CMD	0x040d
#define IM_ASSIGN_IMM_CMD	0x040e
#define IM_ABORT_IMM_CMD	0x040f
#define IM_FORMAT_PREP_IMM_CMD	0x0417

/*SCB (Subsystem Control Block) structure */
struct im_scb {
	unsigned short command;	/*command word (read, etc.) */
	unsigned short enable;	/*enable word, modifies cmd */
	union {
		unsigned long log_blk_adr;	/*block address on SCSI device */
		unsigned char scsi_cmd_length;	/*6,10,12, for other scsi cmd */
	} u1;
	unsigned long sys_buf_adr;	/*physical system memory adr */
	unsigned long sys_buf_length;	/*size of sys mem buffer */
	unsigned long tsb_adr;	/*Termination Status Block adr */
	unsigned long scb_chain_adr;	/*optional SCB chain address */
	union {
		struct {
			unsigned short count;	/*block count, on SCSI device */
			unsigned short length;	/*block length, on SCSI device */
		} blk;
		unsigned char scsi_command[12];	/*other scsi command */
	} u2;
};

/*structure scatter-gather element (for list of system memory areas) */
struct im_sge {
	void *address;
	unsigned long byte_length;
};

/*structure returned by a get_pos_info command: */
struct im_pos_info {
	unsigned short pos_id;	/* adapter id */
	unsigned char pos_3a;	/* pos 3 (if pos 6 = 0) */
	unsigned char pos_2;	/* pos 2 */
	unsigned char int_level;	/* interrupt level IRQ 11 or 14 */
	unsigned char pos_4a;	/* pos 4 (if pos 6 = 0) */
	unsigned short connector_size;	/* MCA connector size: 16 or 32 Bit */
	unsigned char num_luns;	/* number of supported luns per device */
	unsigned char num_puns;	/* number of supported puns */
	unsigned char pacing_factor;	/* pacing factor */
	unsigned char num_ldns;	/* number of ldns available */
	unsigned char eoi_off;	/* time EOI and interrupt inactive */
	unsigned char max_busy;	/* time between reset and busy on */
	unsigned short cache_stat;	/* ldn cachestat. Bit=1 = not cached */
	unsigned short retry_stat;	/* retry status of ldns. Bit=1=disabled */
	unsigned char pos_4b;	/* pos 4 (if pos 6 = 1) */
	unsigned char pos_3b;	/* pos 3 (if pos 6 = 1) */
	unsigned char pos_6;	/* pos 6 */
	unsigned char pos_5;	/* pos 5 */
	unsigned short max_overlap;	/* maximum overlapping requests */
	unsigned short num_bus;	/* number of SCSI-busses */
};

/*values for SCB command word */
#define IM_NO_SYNCHRONOUS      0x0040	/*flag for any command */
#define IM_NO_DISCONNECT       0x0080	/*flag for any command */
#define IM_READ_DATA_CMD       0x1c01
#define IM_WRITE_DATA_CMD      0x1c02
#define IM_READ_VERIFY_CMD     0x1c03
#define IM_WRITE_VERIFY_CMD    0x1c04
#define IM_REQUEST_SENSE_CMD   0x1c08
#define IM_READ_CAPACITY_CMD   0x1c09
#define IM_DEVICE_INQUIRY_CMD  0x1c0b
#define IM_READ_LOGICAL_CMD    0x1c2a
#define IM_OTHER_SCSI_CMD_CMD  0x241f

/* unused, but supported, SCB commands */
#define IM_GET_COMMAND_COMPLETE_STATUS_CMD   0x1c07	/* command status */
#define IM_GET_POS_INFO_CMD                  0x1c0a	/* returns neat stuff */
#define IM_READ_PREFETCH_CMD                 0x1c31	/* caching controller only */
#define IM_FOMAT_UNIT_CMD                    0x1c16	/* format unit */
#define IM_REASSIGN_BLOCK_CMD                0x1c18	/* in case of error */

/*values to set bits in the enable word of SCB */
#define IM_READ_CONTROL              0x8000
#define IM_REPORT_TSB_ONLY_ON_ERROR  0x4000
#define IM_RETRY_ENABLE              0x2000
#define IM_POINTER_TO_LIST           0x1000
#define IM_SUPRESS_EXCEPTION_SHORT   0x0400
#define IM_BYPASS_BUFFER             0x0200
#define IM_CHAIN_ON_NO_ERROR         0x0001

/*TSB (Termination Status Block) structure */
struct im_tsb {
	unsigned short end_status;
	unsigned short reserved1;
	unsigned long residual_byte_count;
	unsigned long sg_list_element_adr;
	unsigned short status_length;
	unsigned char dev_status;
	unsigned char cmd_status;
	unsigned char dev_error;
	unsigned char cmd_error;
	unsigned short reserved2;
	unsigned short reserved3;
	unsigned short low_of_last_scb_adr;
	unsigned short high_of_last_scb_adr;
};

/*subsystem uses interrupt request level 14 */
#define IM_IRQ     14
/*SCSI-2 F/W may evade to interrupt 11 */
#define IM_IRQ_FW  11

/* Model 95 has an additional alphanumeric display, which can be used
   to display SCSI-activities. 8595 models do not have any disk led, which
   makes this feature quite useful.
   The regular PS/2 disk led is turned on/off by bits 6,7 of system
   control port. */

/* LED display-port (actually, last LED on display) */
#define MOD95_LED_PORT	   0x108
/* system-control-register of PS/2s with diskindicator */
#define PS2_SYS_CTR        0x92
/* activity displaying methods */
#define LED_DISP           1
#define LED_ADISP          2
#define LED_ACTIVITY       4
/* failed intr */
#define CMD_FAIL           255

/* The SCSI-ID(!) of the accessed SCSI-device is shown on PS/2-95 machines' LED
   displays. ldn is no longer displayed here, because the ldn mapping is now 
   done dynamically and the ldn <-> pun,lun maps can be looked-up at boottime 
   or during uptime in /proc/scsi/ibmmca/<host_no> in case of trouble, 
   interest, debugging or just for having fun. The left number gives the
   host-adapter number and the right shows the accessed SCSI-ID. */

/* display_mode is set by the ibmmcascsi= command line arg */
static int display_mode = 0;
/* set default adapter timeout */
static unsigned int adapter_timeout = 45;
/* for probing on feature-command: */
static unsigned int global_command_error_excuse = 0;
/* global setting by command line for adapter_speed */
static int global_adapter_speed = 0;	/* full speed by default */

/* Panel / LED on, do it right for F/W addressin, too. adisplay will
 * just ignore ids>7, as the panel has only 7 digits available */
#define PS2_DISK_LED_ON(ad,id) { if (display_mode & LED_DISP) { if (id>9) \
    outw((ad+48)|((id+55)<<8), MOD95_LED_PORT ); else \
    outw((ad+48)|((id+48)<<8), MOD95_LED_PORT ); } else \
    if (display_mode & LED_ADISP) { if (id<7) outb((char)(id+48),MOD95_LED_PORT+1+id); \
    outb((char)(ad+48), MOD95_LED_PORT); } \
    if ((display_mode & LED_ACTIVITY)||(!display_mode)) \
    outb(inb(PS2_SYS_CTR) | 0xc0, PS2_SYS_CTR); }

/* Panel / LED off */
/* bug fixed, Dec 15, 1997, where | was replaced by & here */
#define PS2_DISK_LED_OFF() { if (display_mode & LED_DISP) \
    outw(0x2020, MOD95_LED_PORT ); else if (display_mode & LED_ADISP) { \
    outl(0x20202020,MOD95_LED_PORT); outl(0x20202020,MOD95_LED_PORT+4); } \
    if ((display_mode & LED_ACTIVITY)||(!display_mode)) \
    outb(inb(PS2_SYS_CTR) & 0x3f, PS2_SYS_CTR); }

/*list of supported subsystems */
struct subsys_list_struct {
	unsigned short mca_id;
	char *description;
};

/* types of different supported hardware that goes to hostdata special */
#define IBM_SCSI2_FW     0
#define IBM_7568_WCACHE  1
#define IBM_EXP_UNIT     2
#define IBM_SCSI_WCACHE  3
#define IBM_SCSI         4

/* other special flags for hostdata structure */
#define FORCED_DETECTION         100
#define INTEGRATED_SCSI          101

/* List of possible IBM-SCSI-adapters */
static struct subsys_list_struct subsys_list[] = {
	{0x8efc, "IBM SCSI-2 F/W Adapter"},	/* special = 0 */
	{0x8efd, "IBM 7568 Industrial Computer SCSI Adapter w/Cache"},	/* special = 1 */
	{0x8ef8, "IBM Expansion Unit SCSI Controller"},	/* special = 2 */
	{0x8eff, "IBM SCSI Adapter w/Cache"},	/* special = 3 */
	{0x8efe, "IBM SCSI Adapter"},	/* special = 4 */
};

/* Max number of logical devices (can be up from 0 to 14).  15 is the address
of the adapter itself. */
#define MAX_LOG_DEV  15

/*local data for a logical device */
struct logical_device {
	struct im_scb scb;	/* SCSI-subsystem-control-block structure */
	struct im_tsb tsb;	/* SCSI command complete status block structure */
	struct im_sge sge[16];	/* scatter gather list structure */
	unsigned char buf[256];	/* SCSI command return data buffer */
	Scsi_Cmnd *cmd;		/* SCSI-command that is currently in progress */
	int device_type;	/* type of the SCSI-device. See include/scsi/scsi.h
				   for interpretation of the possible values */
	int block_length;	/* blocksize of a particular logical SCSI-device */
	int cache_flag;		/* 1 if this is uncached, 0 if cache is present for ldn */
	int retry_flag;		/* 1 if adapter retry is disabled, 0 if enabled */
};

/* statistics of the driver during operations (for proc_info) */
struct Driver_Statistics {
	/* SCSI statistics on the adapter */
	int ldn_access[MAX_LOG_DEV + 1];	/* total accesses on a ldn */
	int ldn_read_access[MAX_LOG_DEV + 1];	/* total read-access on a ldn */
	int ldn_write_access[MAX_LOG_DEV + 1];	/* total write-access on a ldn */
	int ldn_inquiry_access[MAX_LOG_DEV + 1];	/* total inquiries on a ldn */
	int ldn_modeselect_access[MAX_LOG_DEV + 1];	/* total mode selects on ldn */
	int scbs;		/* short SCBs queued */
	int long_scbs;		/* long SCBs queued */
	int total_accesses;	/* total accesses on all ldns */
	int total_interrupts;	/* total interrupts (should be
				   same as total_accesses) */
	int total_errors;	/* command completed with error */
	/* dynamical assignment statistics */
	int total_scsi_devices;	/* number of physical pun,lun */
	int dyn_flag;		/* flag showing dynamical mode */
	int dynamical_assignments;	/* number of remappings of ldns */
	int ldn_assignments[MAX_LOG_DEV + 1];	/* number of remappings of each
						   ldn */
};

/* data structure for each host adapter */
struct ibmmca_hostdata {
	/* array of logical devices: */
	struct logical_device _ld[MAX_LOG_DEV + 1];
	/* array to convert (pun, lun) into logical device number: */
	unsigned char _get_ldn[16][8];
	/*array that contains the information about the physical SCSI-devices
	   attached to this host adapter: */
	unsigned char _get_scsi[16][8];
	/* used only when checking logical devices: */
	int _local_checking_phase_flag;
	/* report received interrupt: */
	int _got_interrupt;
	/* report termination-status of SCSI-command: */
	int _stat_result;
	/* reset status (used only when doing reset): */
	int _reset_status;
	/* code of the last SCSI command (needed for panic info): */
	int _last_scsi_command[MAX_LOG_DEV + 1];
	/* identifier of the last SCSI-command type */
	int _last_scsi_type[MAX_LOG_DEV + 1];
	/* last blockcount */
	int _last_scsi_blockcount[MAX_LOG_DEV + 1];
	/* last locgical block address */
	unsigned long _last_scsi_logical_block[MAX_LOG_DEV + 1];
	/* Counter that points on the next reassignable ldn for dynamical
	   remapping. The default value is 7, that is the first reassignable
	   number in the list at boottime: */
	int _next_ldn;
	/* Statistics-structure for this IBM-SCSI-host: */
	struct Driver_Statistics _IBM_DS;
	/* This hostadapters pos-registers pos2 until pos6 */
	unsigned int _pos[8];
	/* assign a special variable, that contains dedicated info about the
	   adaptertype */
	int _special;
	/* connector size on the MCA bus */
	int _connector_size;
	/* synchronous SCSI transfer rate bitpattern */
	int _adapter_speed;
};

/* macros to access host data structure */
#define subsystem_pun(hi) (hosts[(hi)]->this_id)
#define subsystem_maxid(hi) (hosts[(hi)]->max_id)
#define ld(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_ld)
#define get_ldn(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_get_ldn)
#define get_scsi(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_get_scsi)
#define local_checking_phase_flag(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_local_checking_phase_flag)
#define got_interrupt(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_got_interrupt)
#define stat_result(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_stat_result)
#define reset_status(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_reset_status)
#define last_scsi_command(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_last_scsi_command)
#define last_scsi_type(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_last_scsi_type)
#define last_scsi_blockcount(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_last_scsi_blockcount)
#define last_scsi_logical_block(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_last_scsi_logical_block)
#define last_scsi_type(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_last_scsi_type)
#define next_ldn(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_next_ldn)
#define IBM_DS(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_IBM_DS)
#define special(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_special)
#define subsystem_connector_size(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_connector_size)
#define adapter_speed(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_adapter_speed)
#define pos2(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_pos[2])
#define pos3(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_pos[3])
#define pos4(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_pos[4])
#define pos5(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_pos[5])
#define pos6(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_pos[6])

/* Define a arbitrary number as subsystem-marker-type. This number is, as
   described in the ANSI-SCSI-standard, not occupied by other device-types. */
#define TYPE_IBM_SCSI_ADAPTER   0x2F

/* Define 0xFF for no device type, because this type is not defined within
   the ANSI-SCSI-standard, therefore, it can be used and should not cause any
   harm. */
#define TYPE_NO_DEVICE          0xFF

/* define medium-changer. If this is not defined previously, e.g. Linux
   2.0.x, define this type here. */
#ifndef TYPE_MEDIUM_CHANGER
#define TYPE_MEDIUM_CHANGER     0x08
#endif

/* define possible operations for the immediate_assign command */
#define SET_LDN        0
#define REMOVE_LDN     1

/* ldn which is used to probe the SCSI devices */
#define PROBE_LDN      0

/* reset status flag contents */
#define IM_RESET_NOT_IN_PROGRESS         0
#define IM_RESET_IN_PROGRESS             1
#define IM_RESET_FINISHED_OK             2
#define IM_RESET_FINISHED_FAIL           3
#define IM_RESET_NOT_IN_PROGRESS_NO_INT  4
#define IM_RESET_FINISHED_OK_NO_INT      5

/* define undefined SCSI-command */
#define NO_SCSI                  0xffff

/*-----------------------------------------------------------------------*/

/* if this is nonzero, ibmmcascsi option has been passed to the kernel */
static int io_port[IM_MAX_HOSTS] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int scsi_id[IM_MAX_HOSTS] = { 7, 7, 7, 7, 7, 7, 7, 7 };

/* fill module-parameters only, when this define is present.
   (that is kernel version 2.1.x) */
#if defined(MODULE)
static char *boot_options = NULL;
module_param(boot_options, charp, 0);
module_param_array(io_port, int, NULL, 0);
module_param_array(scsi_id, int, NULL, 0);

#if 0 /* FIXME: No longer exist? --RR */
MODULE_PARM(display, "1i");
MODULE_PARM(adisplay, "1i");
MODULE_PARM(normal, "1i");
MODULE_PARM(ansi, "1i");
#endif

MODULE_LICENSE("GPL");
#endif
/*counter of concurrent disk read/writes, to turn on/off disk led */
static int disk_rw_in_progress = 0;

/* host information */
static int found = 0;
static struct Scsi_Host *hosts[IM_MAX_HOSTS + 1] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static unsigned int pos[8];	/* whole pos register-line for diagnosis */
/* Taking into account the additions, made by ZP Gu.
 * This selects now the preset value from the configfile and
 * offers the 'normal' commandline option to be accepted */
#ifdef CONFIG_IBMMCA_SCSI_ORDER_STANDARD
static char ibm_ansi_order = 1;
#else
static char ibm_ansi_order = 0;
#endif

static void issue_cmd(int, unsigned long, unsigned char);
static void internal_done(Scsi_Cmnd * cmd);
static void check_devices(int, int);
static int immediate_assign(int, unsigned int, unsigned int, unsigned int, unsigned int);
static int immediate_feature(int, unsigned int, unsigned int);
#ifdef CONFIG_IBMMCA_SCSI_DEV_RESET
static int immediate_reset(int, unsigned int);
#endif
static int device_inquiry(int, int);
static int read_capacity(int, int);
static int get_pos_info(int);
static char *ti_p(int);
static char *ti_l(int);
static char *ibmrate(unsigned int, int);
static int probe_display(int);
static int probe_bus_mode(int);
static int device_exists(int, int, int *, int *);
static struct Scsi_Host *ibmmca_register(Scsi_Host_Template *, int, int, int, char *);
static int option_setup(char *);
/* local functions needed for proc_info */
static int ldn_access_load(int, int);
static int ldn_access_total_read_write(int);

static irqreturn_t interrupt_handler(int irq, void *dev_id,
					struct pt_regs *regs)
{
	int host_index, ihost_index;
	unsigned int intr_reg;
	unsigned int cmd_result;
	unsigned int ldn;
	Scsi_Cmnd *cmd;
	int lastSCSI;
	struct Scsi_Host *dev = dev_id;

	spin_lock(dev->host_lock);
	    /* search for one adapter-response on shared interrupt */
	    for (host_index = 0; hosts[host_index] && !(inb(IM_STAT_REG(host_index)) & IM_INTR_REQUEST); host_index++);
	/* return if some other device on this IRQ caused the interrupt */
	if (!hosts[host_index]) {
		spin_unlock(dev->host_lock);
		return IRQ_NONE;
	}

	/* the reset-function already did all the job, even ints got
	   renabled on the subsystem, so just return */
	if ((reset_status(host_index) == IM_RESET_NOT_IN_PROGRESS_NO_INT) || (reset_status(host_index) == IM_RESET_FINISHED_OK_NO_INT)) {
		reset_status(host_index) = IM_RESET_NOT_IN_PROGRESS;
		spin_unlock(dev->host_lock);
		return IRQ_HANDLED;
	}

	/*must wait for attention reg not busy, then send EOI to subsystem */
	while (1) {
		if (!(inb(IM_STAT_REG(host_index)) & IM_BUSY))
			break;
		cpu_relax();
	}
	ihost_index = host_index;
	/*get command result and logical device */
	intr_reg = (unsigned char) (inb(IM_INTR_REG(ihost_index)));
	cmd_result = intr_reg & 0xf0;
	ldn = intr_reg & 0x0f;
	/* get the last_scsi_command here */
	lastSCSI = last_scsi_command(ihost_index)[ldn];
	outb(IM_EOI | ldn, IM_ATTN_REG(ihost_index));
	
	/*these should never happen (hw fails, or a local programming bug) */
	if (!global_command_error_excuse) {
		switch (cmd_result) {
			/* Prevent from Ooopsing on error to show the real reason */
		case IM_ADAPTER_HW_FAILURE:
		case IM_SOFTWARE_SEQUENCING_ERROR:
		case IM_CMD_ERROR:
			printk(KERN_ERR "IBM MCA SCSI: Fatal Subsystem ERROR!\n");
			printk(KERN_ERR "              Last cmd=0x%x, ena=%x, len=", lastSCSI, ld(ihost_index)[ldn].scb.enable);
			if (ld(ihost_index)[ldn].cmd)
				printk("%ld/%ld,", (long) (ld(ihost_index)[ldn].cmd->request_bufflen), (long) (ld(ihost_index)[ldn].scb.sys_buf_length));
			else
				printk("none,");
			if (ld(ihost_index)[ldn].cmd)
				printk("Blocksize=%d", ld(ihost_index)[ldn].scb.u2.blk.length);
			else
				printk("Blocksize=none");
			printk(", host=0x%x, ldn=0x%x\n", ihost_index, ldn);
			if (ld(ihost_index)[ldn].cmd) {
				printk(KERN_ERR "Blockcount=%d/%d\n", last_scsi_blockcount(ihost_index)[ldn], ld(ihost_index)[ldn].scb.u2.blk.count);
				printk(KERN_ERR "Logical block=%lx/%lx\n", last_scsi_logical_block(ihost_index)[ldn], ld(ihost_index)[ldn].scb.u1.log_blk_adr);
			}
			printk(KERN_ERR "Reason given: %s\n", (cmd_result == IM_ADAPTER_HW_FAILURE) ? "HARDWARE FAILURE" : (cmd_result == IM_SOFTWARE_SEQUENCING_ERROR) ? "SOFTWARE SEQUENCING ERROR" : (cmd_result == IM_CMD_ERROR) ? "COMMAND ERROR" : "UNKNOWN");
			/* if errors appear, enter this section to give detailed info */
			printk(KERN_ERR "IBM MCA SCSI: Subsystem Error-Status follows:\n");
			printk(KERN_ERR "              Command Type................: %x\n", last_scsi_type(ihost_index)[ldn]);
			printk(KERN_ERR "              Attention Register..........: %x\n", inb(IM_ATTN_REG(ihost_index)));
			printk(KERN_ERR "              Basic Control Register......: %x\n", inb(IM_CTR_REG(ihost_index)));
			printk(KERN_ERR "              Interrupt Status Register...: %x\n", intr_reg);
			printk(KERN_ERR "              Basic Status Register.......: %x\n", inb(IM_STAT_REG(ihost_index)));
			if ((last_scsi_type(ihost_index)[ldn] == IM_SCB) || (last_scsi_type(ihost_index)[ldn] == IM_LONG_SCB)) {
				printk(KERN_ERR "              SCB-Command.................: %x\n", ld(ihost_index)[ldn].scb.command);
				printk(KERN_ERR "              SCB-Enable..................: %x\n", ld(ihost_index)[ldn].scb.enable);
				printk(KERN_ERR "              SCB-logical block address...: %lx\n", ld(ihost_index)[ldn].scb.u1.log_blk_adr);
				printk(KERN_ERR "              SCB-system buffer address...: %lx\n", ld(ihost_index)[ldn].scb.sys_buf_adr);
				printk(KERN_ERR "              SCB-system buffer length....: %lx\n", ld(ihost_index)[ldn].scb.sys_buf_length);
				printk(KERN_ERR "              SCB-tsb address.............: %lx\n", ld(ihost_index)[ldn].scb.tsb_adr);
				printk(KERN_ERR "              SCB-Chain address...........: %lx\n", ld(ihost_index)[ldn].scb.scb_chain_adr);
				printk(KERN_ERR "              SCB-block count.............: %x\n", ld(ihost_index)[ldn].scb.u2.blk.count);
				printk(KERN_ERR "              SCB-block length............: %x\n", ld(ihost_index)[ldn].scb.u2.blk.length);
			}
			printk(KERN_ERR "              Send this report to the maintainer.\n");
			panic("IBM MCA SCSI: Fatal error message from the subsystem (0x%X,0x%X)!\n", lastSCSI, cmd_result);
			break;
		}
	} else {
		/* The command error handling is made silent, but we tell the
		 * calling function, that there is a reported error from the
		 * adapter. */
		switch (cmd_result) {
		case IM_ADAPTER_HW_FAILURE:
		case IM_SOFTWARE_SEQUENCING_ERROR:
		case IM_CMD_ERROR:
			global_command_error_excuse = CMD_FAIL;
			break;
		default:
			global_command_error_excuse = 0;
			break;
		}
	}
	/* if no panic appeared, increase the interrupt-counter */
	IBM_DS(ihost_index).total_interrupts++;
	/*only for local checking phase */
	if (local_checking_phase_flag(ihost_index)) {
		stat_result(ihost_index) = cmd_result;
		got_interrupt(ihost_index) = 1;
		reset_status(ihost_index) = IM_RESET_FINISHED_OK;
		last_scsi_command(ihost_index)[ldn] = NO_SCSI;
		spin_unlock(dev->host_lock);
		return IRQ_HANDLED;
	}
	/* handling of commands coming from upper level of scsi driver */
	if (last_scsi_type(ihost_index)[ldn] == IM_IMM_CMD) {
		/* verify ldn, and may handle rare reset immediate command */
		if ((reset_status(ihost_index) == IM_RESET_IN_PROGRESS) && (last_scsi_command(ihost_index)[ldn] == IM_RESET_IMM_CMD)) {
			if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE) {
				disk_rw_in_progress = 0;
				PS2_DISK_LED_OFF();
				reset_status(ihost_index) = IM_RESET_FINISHED_FAIL;
			} else {
				/*reset disk led counter, turn off disk led */
				disk_rw_in_progress = 0;
				PS2_DISK_LED_OFF();
				reset_status(ihost_index) = IM_RESET_FINISHED_OK;
			}
			stat_result(ihost_index) = cmd_result;
			last_scsi_command(ihost_index)[ldn] = NO_SCSI;
			last_scsi_type(ihost_index)[ldn] = 0;
			spin_unlock(dev->host_lock);
			return IRQ_HANDLED;
		} else if (last_scsi_command(ihost_index)[ldn] == IM_ABORT_IMM_CMD) {
			/* react on SCSI abort command */
#ifdef IM_DEBUG_PROBE
			printk("IBM MCA SCSI: Interrupt from SCSI-abort.\n");
#endif
			disk_rw_in_progress = 0;
			PS2_DISK_LED_OFF();
			cmd = ld(ihost_index)[ldn].cmd;
			ld(ihost_index)[ldn].cmd = NULL;
			if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
				cmd->result = DID_NO_CONNECT << 16;
			else
				cmd->result = DID_ABORT << 16;
			stat_result(ihost_index) = cmd_result;
			last_scsi_command(ihost_index)[ldn] = NO_SCSI;
			last_scsi_type(ihost_index)[ldn] = 0;
			if (cmd->scsi_done)
				(cmd->scsi_done) (cmd);	/* should be the internal_done */
			spin_unlock(dev->host_lock);
			return IRQ_HANDLED;
		} else {
			disk_rw_in_progress = 0;
			PS2_DISK_LED_OFF();
			reset_status(ihost_index) = IM_RESET_FINISHED_OK;
			stat_result(ihost_index) = cmd_result;
			last_scsi_command(ihost_index)[ldn] = NO_SCSI;
			spin_unlock(dev->host_lock);
			return IRQ_HANDLED;
		}
	}
	last_scsi_command(ihost_index)[ldn] = NO_SCSI;
	last_scsi_type(ihost_index)[ldn] = 0;
	cmd = ld(ihost_index)[ldn].cmd;
	ld(ihost_index)[ldn].cmd = NULL;
#ifdef IM_DEBUG_TIMEOUT
	if (cmd) {
		if ((cmd->target == TIMEOUT_PUN) && (cmd->device->lun == TIMEOUT_LUN)) {
			printk("IBM MCA SCSI: Ignoring interrupt from pun=%x, lun=%x.\n", cmd->target, cmd->device->lun);
			return IRQ_HANDLED;
		}
	}
#endif
	/*if no command structure, just return, else clear cmd */
	if (!cmd)
	{
		spin_unlock(dev->host_lock);
		return IRQ_HANDLED;
	}

#ifdef IM_DEBUG_INT
	printk("cmd=%02x ireg=%02x ds=%02x cs=%02x de=%02x ce=%02x\n", cmd->cmnd[0], intr_reg, ld(ihost_index)[ldn].tsb.dev_status, ld(ihost_index)[ldn].tsb.cmd_status, ld(ihost_index)[ldn].tsb.dev_error, ld(ihost_index)[ldn].tsb.cmd_error);
#endif
	/*if this is end of media read/write, may turn off PS/2 disk led */
	if ((ld(ihost_index)[ldn].device_type != TYPE_NO_LUN) && (ld(ihost_index)[ldn].device_type != TYPE_NO_DEVICE)) {
		/* only access this, if there was a valid device addressed */
		if (--disk_rw_in_progress == 0)
			PS2_DISK_LED_OFF();
	}

	/* IBM describes the status-mask to be 0x1e, but this is not conform
	 * with SCSI-definition, I suppose, the reason for it is that IBM
	 * adapters do not support CMD_TERMINATED, TASK_SET_FULL and
	 * ACA_ACTIVE as returning statusbyte information. (ML) */
	if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE) {
		cmd->result = (unsigned char) (ld(ihost_index)[ldn].tsb.dev_status & 0x1e);
		IBM_DS(ihost_index).total_errors++;
	} else
		cmd->result = 0;
	/* write device status into cmd->result, and call done function */
	if (lastSCSI == NO_SCSI) {	/* unexpected interrupt :-( */
		cmd->result |= DID_BAD_INTR << 16;
		printk("IBM MCA SCSI: WARNING - Interrupt from non-pending SCSI-command!\n");
	} else			/* things went right :-) */
		cmd->result |= DID_OK << 16;
	if (cmd->scsi_done)
		(cmd->scsi_done) (cmd);
	spin_unlock(dev->host_lock);
	return IRQ_HANDLED;
}

static void issue_cmd(int host_index, unsigned long cmd_reg, unsigned char attn_reg)
{
	unsigned long flags;
	/* must wait for attention reg not busy */
	while (1) {
		spin_lock_irqsave(hosts[host_index]->host_lock, flags);
		if (!(inb(IM_STAT_REG(host_index)) & IM_BUSY))
			break;
		spin_unlock_irqrestore(hosts[host_index]->host_lock, flags);
	}
	/* write registers and enable system interrupts */
	outl(cmd_reg, IM_CMD_REG(host_index));
	outb(attn_reg, IM_ATTN_REG(host_index));
	spin_unlock_irqrestore(hosts[host_index]->host_lock, flags);
}

static void internal_done(Scsi_Cmnd * cmd)
{
	cmd->SCp.Status++;
	return;
}

/* SCSI-SCB-command for device_inquiry */
static int device_inquiry(int host_index, int ldn)
{
	int retr;
	struct im_scb *scb;
	struct im_tsb *tsb;
	unsigned char *buf;

	scb = &(ld(host_index)[ldn].scb);
	tsb = &(ld(host_index)[ldn].tsb);
	buf = (unsigned char *) (&(ld(host_index)[ldn].buf));
	ld(host_index)[ldn].tsb.dev_status = 0;	/* prepare statusblock */
	for (retr = 0; retr < 3; retr++) {
		/* fill scb with inquiry command */
		scb->command = IM_DEVICE_INQUIRY_CMD | IM_NO_DISCONNECT;
		scb->enable = IM_REPORT_TSB_ONLY_ON_ERROR | IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT | IM_RETRY_ENABLE | IM_BYPASS_BUFFER;
		last_scsi_command(host_index)[ldn] = IM_DEVICE_INQUIRY_CMD;
		last_scsi_type(host_index)[ldn] = IM_SCB;
		scb->sys_buf_adr = isa_virt_to_bus(buf);
		scb->sys_buf_length = 255;	/* maximum bufferlength gives max info */
		scb->tsb_adr = isa_virt_to_bus(tsb);
		/* issue scb to passed ldn, and busy wait for interrupt */
		got_interrupt(host_index) = 0;
		issue_cmd(host_index, isa_virt_to_bus(scb), IM_SCB | ldn);
		while (!got_interrupt(host_index))
			barrier();

		/*if command succesful, break */
		if ((stat_result(host_index) == IM_SCB_CMD_COMPLETED) || (stat_result(host_index) == IM_SCB_CMD_COMPLETED_WITH_RETRIES))
			return 1;
	}
	/*if all three retries failed, return "no device at this ldn" */
	if (retr >= 3)
		return 0;
	else
		return 1;
}

static int read_capacity(int host_index, int ldn)
{
	int retr;
	struct im_scb *scb;
	struct im_tsb *tsb;
	unsigned char *buf;

	scb = &(ld(host_index)[ldn].scb);
	tsb = &(ld(host_index)[ldn].tsb);
	buf = (unsigned char *) (&(ld(host_index)[ldn].buf));
	ld(host_index)[ldn].tsb.dev_status = 0;
	for (retr = 0; retr < 3; retr++) {
		/*fill scb with read capacity command */
		scb->command = IM_READ_CAPACITY_CMD;
		scb->enable = IM_REPORT_TSB_ONLY_ON_ERROR | IM_READ_CONTROL | IM_RETRY_ENABLE | IM_BYPASS_BUFFER;
		last_scsi_command(host_index)[ldn] = IM_READ_CAPACITY_CMD;
		last_scsi_type(host_index)[ldn] = IM_SCB;
		scb->sys_buf_adr = isa_virt_to_bus(buf);
		scb->sys_buf_length = 8;
		scb->tsb_adr = isa_virt_to_bus(tsb);
		/*issue scb to passed ldn, and busy wait for interrupt */
		got_interrupt(host_index) = 0;
		issue_cmd(host_index, isa_virt_to_bus(scb), IM_SCB | ldn);
		while (!got_interrupt(host_index))
			barrier();

		/*if got capacity, get block length and return one device found */
		if ((stat_result(host_index) == IM_SCB_CMD_COMPLETED) || (stat_result(host_index) == IM_SCB_CMD_COMPLETED_WITH_RETRIES))
			return 1;
	}
	/*if all three retries failed, return "no device at this ldn" */
	if (retr >= 3)
		return 0;
	else
		return 1;
}

static int get_pos_info(int host_index)
{
	int retr;
	struct im_scb *scb;
	struct im_tsb *tsb;
	unsigned char *buf;

	scb = &(ld(host_index)[MAX_LOG_DEV].scb);
	tsb = &(ld(host_index)[MAX_LOG_DEV].tsb);
	buf = (unsigned char *) (&(ld(host_index)[MAX_LOG_DEV].buf));
	ld(host_index)[MAX_LOG_DEV].tsb.dev_status = 0;
	for (retr = 0; retr < 3; retr++) {
		/*fill scb with get_pos_info command */
		scb->command = IM_GET_POS_INFO_CMD;
		scb->enable = IM_READ_CONTROL | IM_REPORT_TSB_ONLY_ON_ERROR | IM_RETRY_ENABLE | IM_BYPASS_BUFFER;
		last_scsi_command(host_index)[MAX_LOG_DEV] = IM_GET_POS_INFO_CMD;
		last_scsi_type(host_index)[MAX_LOG_DEV] = IM_SCB;
		scb->sys_buf_adr = isa_virt_to_bus(buf);
		if (special(host_index) == IBM_SCSI2_FW)
			scb->sys_buf_length = 256;	/* get all info from F/W adapter */
		else
			scb->sys_buf_length = 18;	/* get exactly 18 bytes for other SCSI */
		scb->tsb_adr = isa_virt_to_bus(tsb);
		/*issue scb to ldn=15, and busy wait for interrupt */
		got_interrupt(host_index) = 0;
		issue_cmd(host_index, isa_virt_to_bus(scb), IM_SCB | MAX_LOG_DEV);
		
		/* FIXME: timeout */
		while (!got_interrupt(host_index))
			barrier();

		/*if got POS-stuff, get block length and return one device found */
		if ((stat_result(host_index) == IM_SCB_CMD_COMPLETED) || (stat_result(host_index) == IM_SCB_CMD_COMPLETED_WITH_RETRIES))
			return 1;
	}
	/* if all three retries failed, return "no device at this ldn" */
	if (retr >= 3)
		return 0;
	else
		return 1;
}

/* SCSI-immediate-command for assign. This functions maps/unmaps specific
 ldn-numbers on SCSI (PUN,LUN). It is needed for presetting of the
 subsystem and for dynamical remapping od ldns. */
static int immediate_assign(int host_index, unsigned int pun, unsigned int lun, unsigned int ldn, unsigned int operation)
{
	int retr;
	unsigned long imm_cmd;

	for (retr = 0; retr < 3; retr++) {
		/* select mutation level of the SCSI-adapter */
		switch (special(host_index)) {
		case IBM_SCSI2_FW:
			imm_cmd = (unsigned long) (IM_ASSIGN_IMM_CMD);
			imm_cmd |= (unsigned long) ((lun & 7) << 24);
			imm_cmd |= (unsigned long) ((operation & 1) << 23);
			imm_cmd |= (unsigned long) ((pun & 7) << 20) | ((pun & 8) << 24);
			imm_cmd |= (unsigned long) ((ldn & 15) << 16);
			break;
		default:
			imm_cmd = inl(IM_CMD_REG(host_index));
			imm_cmd &= (unsigned long) (0xF8000000);	/* keep reserved bits */
			imm_cmd |= (unsigned long) (IM_ASSIGN_IMM_CMD);
			imm_cmd |= (unsigned long) ((lun & 7) << 24);
			imm_cmd |= (unsigned long) ((operation & 1) << 23);
			imm_cmd |= (unsigned long) ((pun & 7) << 20);
			imm_cmd |= (unsigned long) ((ldn & 15) << 16);
			break;
		}
		last_scsi_command(host_index)[MAX_LOG_DEV] = IM_ASSIGN_IMM_CMD;
		last_scsi_type(host_index)[MAX_LOG_DEV] = IM_IMM_CMD;
		got_interrupt(host_index) = 0;
		issue_cmd(host_index, (unsigned long) (imm_cmd), IM_IMM_CMD | MAX_LOG_DEV);
		while (!got_interrupt(host_index))
			barrier();

		/*if command succesful, break */
		if (stat_result(host_index) == IM_IMMEDIATE_CMD_COMPLETED)
			return 1;
	}
	if (retr >= 3)
		return 0;
	else
		return 1;
}

static int immediate_feature(int host_index, unsigned int speed, unsigned int timeout)
{
	int retr;
	unsigned long imm_cmd;

	for (retr = 0; retr < 3; retr++) {
		/* select mutation level of the SCSI-adapter */
		imm_cmd = IM_FEATURE_CTR_IMM_CMD;
		imm_cmd |= (unsigned long) ((speed & 0x7) << 29);
		imm_cmd |= (unsigned long) ((timeout & 0x1fff) << 16);
		last_scsi_command(host_index)[MAX_LOG_DEV] = IM_FEATURE_CTR_IMM_CMD;
		last_scsi_type(host_index)[MAX_LOG_DEV] = IM_IMM_CMD;
		got_interrupt(host_index) = 0;
		/* we need to run into command errors in order to probe for the
		 * right speed! */
		global_command_error_excuse = 1;
		issue_cmd(host_index, (unsigned long) (imm_cmd), IM_IMM_CMD | MAX_LOG_DEV);
		
		/* FIXME: timeout */
		while (!got_interrupt(host_index))
			barrier();
		if (global_command_error_excuse == CMD_FAIL) {
			global_command_error_excuse = 0;
			return 2;
		} else
			global_command_error_excuse = 0;
		/*if command succesful, break */
		if (stat_result(host_index) == IM_IMMEDIATE_CMD_COMPLETED)
			return 1;
	}
	if (retr >= 3)
		return 0;
	else
		return 1;
}

#ifdef CONFIG_IBMMCA_SCSI_DEV_RESET
static int immediate_reset(int host_index, unsigned int ldn)
{
	int retries;
	int ticks;
	unsigned long imm_command;

	for (retries = 0; retries < 3; retries++) {
		imm_command = inl(IM_CMD_REG(host_index));
		imm_command &= (unsigned long) (0xFFFF0000);	/* keep reserved bits */
		imm_command |= (unsigned long) (IM_RESET_IMM_CMD);
		last_scsi_command(host_index)[ldn] = IM_RESET_IMM_CMD;
		last_scsi_type(host_index)[ldn] = IM_IMM_CMD;
		got_interrupt(host_index) = 0;
		reset_status(host_index) = IM_RESET_IN_PROGRESS;
		issue_cmd(host_index, (unsigned long) (imm_command), IM_IMM_CMD | ldn);
		ticks = IM_RESET_DELAY * HZ;
		while (reset_status(host_index) == IM_RESET_IN_PROGRESS && --ticks) {
			udelay((1 + 999 / HZ) * 1000);
			barrier();
		}
		/* if reset did not complete, just complain */
		if (!ticks) {
			printk(KERN_ERR "IBM MCA SCSI: reset did not complete within %d seconds.\n", IM_RESET_DELAY);
			reset_status(host_index) = IM_RESET_FINISHED_OK;
			/* did not work, finish */
			return 1;
		}
		/*if command succesful, break */
		if (stat_result(host_index) == IM_IMMEDIATE_CMD_COMPLETED)
			return 1;
	}
	if (retries >= 3)
		return 0;
	else
		return 1;
}
#endif

/* type-interpreter for physical device numbers */
static char *ti_p(int dev)
{
	switch (dev) {
	case TYPE_IBM_SCSI_ADAPTER:
		return ("A");
	case TYPE_DISK:
		return ("D");
	case TYPE_TAPE:
		return ("T");
	case TYPE_PROCESSOR:
		return ("P");
	case TYPE_WORM:
		return ("W");
	case TYPE_ROM:
		return ("R");
	case TYPE_SCANNER:
		return ("S");
	case TYPE_MOD:
		return ("M");
	case TYPE_MEDIUM_CHANGER:
		return ("C");
	case TYPE_NO_LUN:
		return ("+");	/* show NO_LUN */
	}
	return ("-");		/* TYPE_NO_DEVICE and others */
}

/* interpreter for logical device numbers (ldn) */
static char *ti_l(int val)
{
	const char hex[16] = "0123456789abcdef";
	static char answer[2];

	answer[1] = (char) (0x0);
	if (val <= MAX_LOG_DEV)
		answer[0] = hex[val];
	else
		answer[0] = '-';
	return (char *) &answer;
}

/* transfers bitpattern of the feature command to values in MHz */
static char *ibmrate(unsigned int speed, int i)
{
	switch (speed) {
	case 0:
		return i ? "5.00" : "10.00";
	case 1:
		return i ? "4.00" : "8.00";
	case 2:
		return i ? "3.33" : "6.66";
	case 3:
		return i ? "2.86" : "5.00";
	case 4:
		return i ? "2.50" : "4.00";
	case 5:
		return i ? "2.22" : "3.10";
	case 6:
		return i ? "2.00" : "2.50";
	case 7:
		return i ? "1.82" : "2.00";
	}
	return "---";
}

static int probe_display(int what)
{
	static int rotator = 0;
	const char rotor[] = "|/-\\";

	if (!(display_mode & LED_DISP))
		return 0;
	if (!what) {
		outl(0x20202020, MOD95_LED_PORT);
		outl(0x20202020, MOD95_LED_PORT + 4);
	} else {
		outb('S', MOD95_LED_PORT + 7);
		outb('C', MOD95_LED_PORT + 6);
		outb('S', MOD95_LED_PORT + 5);
		outb('I', MOD95_LED_PORT + 4);
		outb('i', MOD95_LED_PORT + 3);
		outb('n', MOD95_LED_PORT + 2);
		outb('i', MOD95_LED_PORT + 1);
		outb((char) (rotor[rotator]), MOD95_LED_PORT);
		rotator++;
		if (rotator > 3)
			rotator = 0;
	}
	return 0;
}

static int probe_bus_mode(int host_index)
{
	struct im_pos_info *info;
	int num_bus = 0;
	int ldn;

	info = (struct im_pos_info *) (&(ld(host_index)[MAX_LOG_DEV].buf));
	if (get_pos_info(host_index)) {
		if (info->connector_size & 0xf000)
			subsystem_connector_size(host_index) = 16;
		else
			subsystem_connector_size(host_index) = 32;
		num_bus |= (info->pos_4b & 8) >> 3;
		for (ldn = 0; ldn <= MAX_LOG_DEV; ldn++) {
			if ((special(host_index) == IBM_SCSI_WCACHE) || (special(host_index) == IBM_7568_WCACHE)) {
				if (!((info->cache_stat >> ldn) & 1))
					ld(host_index)[ldn].cache_flag = 0;
			}
			if (!((info->retry_stat >> ldn) & 1))
				ld(host_index)[ldn].retry_flag = 0;
		}
#ifdef IM_DEBUG_PROBE
		printk("IBM MCA SCSI: SCSI-Cache bits: ");
		for (ldn = 0; ldn <= MAX_LOG_DEV; ldn++) {
			printk("%d", ld(host_index)[ldn].cache_flag);
		}
		printk("\nIBM MCA SCSI: SCSI-Retry bits: ");
		for (ldn = 0; ldn <= MAX_LOG_DEV; ldn++) {
			printk("%d", ld(host_index)[ldn].retry_flag);
		}
		printk("\n");
#endif
	}
	return num_bus;
}

/* probing scsi devices */
static void check_devices(int host_index, int adaptertype)
{
	int id, lun, ldn, ticks;
	int count_devices;	/* local counter for connected device */
	int max_pun;
	int num_bus;
	int speedrun;		/* local adapter_speed check variable */

	/* assign default values to certain variables */
	ticks = 0;
	count_devices = 0;
	IBM_DS(host_index).dyn_flag = 0;	/* normally no need for dynamical ldn management */
	IBM_DS(host_index).total_errors = 0;	/* set errorcounter to 0 */
	next_ldn(host_index) = 7;	/* next ldn to be assigned is 7, because 0-6 is 'hardwired' */

	/* initialize the very important driver-informational arrays/structs */
	memset(ld(host_index), 0, sizeof(ld(host_index)));
	for (ldn = 0; ldn <= MAX_LOG_DEV; ldn++) {
		last_scsi_command(host_index)[ldn] = NO_SCSI;	/* emptify last SCSI-command storage */
		last_scsi_type(host_index)[ldn] = 0;
		ld(host_index)[ldn].cache_flag = 1;
		ld(host_index)[ldn].retry_flag = 1;
	}
	memset(get_ldn(host_index), TYPE_NO_DEVICE, sizeof(get_ldn(host_index)));	/* this is essential ! */
	memset(get_scsi(host_index), TYPE_NO_DEVICE, sizeof(get_scsi(host_index)));	/* this is essential ! */
	for (lun = 0; lun < 8; lun++) {
		/* mark the adapter at its pun on all luns */
		get_scsi(host_index)[subsystem_pun(host_index)][lun] = TYPE_IBM_SCSI_ADAPTER;
		get_ldn(host_index)[subsystem_pun(host_index)][lun] = MAX_LOG_DEV;	/* make sure, the subsystem
											   ldn is active for all
											   luns. */
	}
	probe_display(0);	/* Supercool display usage during SCSI-probing. */
	/* This makes sense, when booting without any */
	/* monitor connected on model XX95. */

	/* STEP 1: */
	adapter_speed(host_index) = global_adapter_speed;
	speedrun = adapter_speed(host_index);
	while (immediate_feature(host_index, speedrun, adapter_timeout) == 2) {
		probe_display(1);
		if (speedrun == 7)
			panic("IBM MCA SCSI: Cannot set Synchronous-Transfer-Rate!\n");
		speedrun++;
		if (speedrun > 7)
			speedrun = 7;
	}
	adapter_speed(host_index) = speedrun;
	/* Get detailed information about the current adapter, necessary for
	 * device operations: */
	num_bus = probe_bus_mode(host_index);

	/* num_bus contains only valid data for the F/W adapter! */
	if (adaptertype == IBM_SCSI2_FW) {	/* F/W SCSI adapter: */
		/* F/W adapter PUN-space extension evaluation: */
		if (num_bus) {
			printk(KERN_INFO "IBM MCA SCSI: Separate bus mode (wide-addressing enabled)\n");
			subsystem_maxid(host_index) = 16;
		} else {
			printk(KERN_INFO "IBM MCA SCSI: Combined bus mode (wide-addressing disabled)\n");
			subsystem_maxid(host_index) = 8;
		}
		printk(KERN_INFO "IBM MCA SCSI: Sync.-Rate (F/W: 20, Int.: 10, Ext.: %s) MBytes/s\n", ibmrate(speedrun, adaptertype));
	} else			/* all other IBM SCSI adapters: */
		printk(KERN_INFO "IBM MCA SCSI: Synchronous-SCSI-Transfer-Rate: %s MBytes/s\n", ibmrate(speedrun, adaptertype));

	/* assign correct PUN device space */
	max_pun = subsystem_maxid(host_index);

#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Current SCSI-host index: %d\n", host_index);
	printk("IBM MCA SCSI: Removing default logical SCSI-device mapping.");
#else
	printk(KERN_INFO "IBM MCA SCSI: Dev. Order: %s, Mapping (takes <2min): ", (ibm_ansi_order) ? "ANSI" : "New");
#endif
	for (ldn = 0; ldn < MAX_LOG_DEV; ldn++) {
		probe_display(1);
#ifdef IM_DEBUG_PROBE
		printk(".");
#endif
		immediate_assign(host_index, 0, 0, ldn, REMOVE_LDN);	/* remove ldn (wherever) */
	}
	lun = 0;		/* default lun is 0 */
#ifndef IM_DEBUG_PROBE
	printk("cleared,");
#endif
	/* STEP 2: */
#ifdef IM_DEBUG_PROBE
	printk("\nIBM MCA SCSI: Scanning SCSI-devices.");
#endif
	for (id = 0; id < max_pun; id++)
#ifdef CONFIG_SCSI_MULTI_LUN
		for (lun = 0; lun < 8; lun++)
#endif
		{
			probe_display(1);
#ifdef IM_DEBUG_PROBE
			printk(".");
#endif
			if (id != subsystem_pun(host_index)) {
				/* if pun is not the adapter: */
				/* set ldn=0 to pun,lun */
				immediate_assign(host_index, id, lun, PROBE_LDN, SET_LDN);
				if (device_inquiry(host_index, PROBE_LDN)) {	/* probe device */
					get_scsi(host_index)[id][lun] = (unsigned char) (ld(host_index)[PROBE_LDN].buf[0]);
					/* entry, even for NO_LUN */
					if (ld(host_index)[PROBE_LDN].buf[0] != TYPE_NO_LUN)
						count_devices++;	/* a existing device is found */
				}
				/* remove ldn */
				immediate_assign(host_index, id, lun, PROBE_LDN, REMOVE_LDN);
			}
		}
#ifndef IM_DEBUG_PROBE
	printk("scanned,");
#endif
	/* STEP 3: */
#ifdef IM_DEBUG_PROBE
	printk("\nIBM MCA SCSI: Mapping SCSI-devices.");
#endif
	ldn = 0;
	lun = 0;
#ifdef CONFIG_SCSI_MULTI_LUN
	for (lun = 0; lun < 8 && ldn < MAX_LOG_DEV; lun++)
#endif
		for (id = 0; id < max_pun && ldn < MAX_LOG_DEV; id++) {
			probe_display(1);
#ifdef IM_DEBUG_PROBE
			printk(".");
#endif
			if (id != subsystem_pun(host_index)) {
				if (get_scsi(host_index)[id][lun] != TYPE_NO_LUN && get_scsi(host_index)[id][lun] != TYPE_NO_DEVICE) {
					/* Only map if accepted type. Always enter for
					   lun == 0 to get no gaps into ldn-mapping for ldn<7. */
					immediate_assign(host_index, id, lun, ldn, SET_LDN);
					get_ldn(host_index)[id][lun] = ldn;	/* map ldn */
					if (device_exists(host_index, ldn, &ld(host_index)[ldn].block_length, &ld(host_index)[ldn].device_type)) {
#ifdef CONFIG_IBMMCA_SCSI_DEV_RESET
						printk("resetting device at ldn=%x ... ", ldn);
						immediate_reset(host_index, ldn);
#endif
						ldn++;
					} else {
						/* device vanished, probably because we don't know how to
						 * handle it or because it has problems */
						if (lun > 0) {
							/* remove mapping */
							get_ldn(host_index)[id][lun] = TYPE_NO_DEVICE;
							immediate_assign(host_index, 0, 0, ldn, REMOVE_LDN);
						} else
							ldn++;
					}
				} else if (lun == 0) {
					/* map lun == 0, even if no device exists */
					immediate_assign(host_index, id, lun, ldn, SET_LDN);
					get_ldn(host_index)[id][lun] = ldn;	/* map ldn */
					ldn++;
				}
			}
		}
	/* STEP 4: */

	/* map remaining ldns to non-existing devices */
	for (lun = 1; lun < 8 && ldn < MAX_LOG_DEV; lun++)
		for (id = 0; id < max_pun && ldn < MAX_LOG_DEV; id++) {
			if (get_scsi(host_index)[id][lun] == TYPE_NO_LUN || get_scsi(host_index)[id][lun] == TYPE_NO_DEVICE) {
				probe_display(1);
				/* Map remaining ldns only to NON-existing pun,lun
				   combinations to make sure an inquiry will fail.
				   For MULTI_LUN, it is needed to avoid adapter autonome
				   SCSI-remapping. */
				immediate_assign(host_index, id, lun, ldn, SET_LDN);
				get_ldn(host_index)[id][lun] = ldn;
				ldn++;
			}
		}
#ifndef IM_DEBUG_PROBE
	printk("mapped.");
#endif
	printk("\n");
#ifdef IM_DEBUG_PROBE
	if (ibm_ansi_order)
		printk("IBM MCA SCSI: Device order: IBM/ANSI (pun=7 is first).\n");
	else
		printk("IBM MCA SCSI: Device order: New Industry Standard (pun=0 is first).\n");
#endif

#ifdef IM_DEBUG_PROBE
	/* Show the physical and logical mapping during boot. */
	printk("IBM MCA SCSI: Determined SCSI-device-mapping:\n");
	printk("    Physical SCSI-Device Map               Logical SCSI-Device Map\n");
	printk("ID\\LUN  0  1  2  3  4  5  6  7       ID\\LUN  0  1  2  3  4  5  6  7\n");
	for (id = 0; id < max_pun; id++) {
		printk("%2d     ", id);
		for (lun = 0; lun < 8; lun++)
			printk("%2s ", ti_p(get_scsi(host_index)[id][lun]));
		printk("      %2d     ", id);
		for (lun = 0; lun < 8; lun++)
			printk("%2s ", ti_l(get_ldn(host_index)[id][lun]));
		printk("\n");
	}
#endif

	/* assign total number of found SCSI-devices to the statistics struct */
	IBM_DS(host_index).total_scsi_devices = count_devices;

	/* decide for output in /proc-filesystem, if the configuration of
	   SCSI-devices makes dynamical reassignment of devices necessary */
	if (count_devices >= MAX_LOG_DEV)
		IBM_DS(host_index).dyn_flag = 1;	/* dynamical assignment is necessary */
	else
		IBM_DS(host_index).dyn_flag = 0;	/* dynamical assignment is not necessary */

	/* If no SCSI-devices are assigned, return 1 in order to cause message. */
	if (ldn == 0)
		printk("IBM MCA SCSI: Warning: No SCSI-devices found/assigned!\n");

	/* reset the counters for statistics on the current adapter */
	IBM_DS(host_index).scbs = 0;
	IBM_DS(host_index).long_scbs = 0;
	IBM_DS(host_index).total_accesses = 0;
	IBM_DS(host_index).total_interrupts = 0;
	IBM_DS(host_index).dynamical_assignments = 0;
	memset(IBM_DS(host_index).ldn_access, 0x0, sizeof(IBM_DS(host_index).ldn_access));
	memset(IBM_DS(host_index).ldn_read_access, 0x0, sizeof(IBM_DS(host_index).ldn_read_access));
	memset(IBM_DS(host_index).ldn_write_access, 0x0, sizeof(IBM_DS(host_index).ldn_write_access));
	memset(IBM_DS(host_index).ldn_inquiry_access, 0x0, sizeof(IBM_DS(host_index).ldn_inquiry_access));
	memset(IBM_DS(host_index).ldn_modeselect_access, 0x0, sizeof(IBM_DS(host_index).ldn_modeselect_access));
	memset(IBM_DS(host_index).ldn_assignments, 0x0, sizeof(IBM_DS(host_index).ldn_assignments));
	probe_display(0);
	return;
}

static int device_exists(int host_index, int ldn, int *block_length, int *device_type)
{
	unsigned char *buf;
	/* if no valid device found, return immediately with 0 */
	if (!(device_inquiry(host_index, ldn)))
		return 0;
	buf = (unsigned char *) (&(ld(host_index)[ldn].buf));
	if (*buf == TYPE_ROM) {
		*device_type = TYPE_ROM;
		*block_length = 2048;	/* (standard blocksize for yellow-/red-book) */
		return 1;
	}
	if (*buf == TYPE_WORM) {
		*device_type = TYPE_WORM;
		*block_length = 2048;
		return 1;
	}
	if (*buf == TYPE_DISK) {
		*device_type = TYPE_DISK;
		if (read_capacity(host_index, ldn)) {
			*block_length = *(buf + 7) + (*(buf + 6) << 8) + (*(buf + 5) << 16) + (*(buf + 4) << 24);
			return 1;
		} else
			return 0;
	}
	if (*buf == TYPE_MOD) {
		*device_type = TYPE_MOD;
		if (read_capacity(host_index, ldn)) {
			*block_length = *(buf + 7) + (*(buf + 6) << 8) + (*(buf + 5) << 16) + (*(buf + 4) << 24);
			return 1;
		} else
			return 0;
	}
	if (*buf == TYPE_TAPE) {
		*device_type = TYPE_TAPE;
		*block_length = 0;	/* not in use (setting by mt and mtst in op.) */
		return 1;
	}
	if (*buf == TYPE_PROCESSOR) {
		*device_type = TYPE_PROCESSOR;
		*block_length = 0;	/* they set their stuff on drivers */
		return 1;
	}
	if (*buf == TYPE_SCANNER) {
		*device_type = TYPE_SCANNER;
		*block_length = 0;	/* they set their stuff on drivers */
		return 1;
	}
	if (*buf == TYPE_MEDIUM_CHANGER) {
		*device_type = TYPE_MEDIUM_CHANGER;
		*block_length = 0;	/* One never knows, what to expect on a medium
					   changer device. */
		return 1;
	}
	return 0;
}

static void internal_ibmmca_scsi_setup(char *str, int *ints)
{
	int i, j, io_base, id_base;
	char *token;

	io_base = 0;
	id_base = 0;
	if (str) {
		j = 0;
		while ((token = strsep(&str, ",")) != NULL) {
			if (!strcmp(token, "activity"))
				display_mode |= LED_ACTIVITY;
			if (!strcmp(token, "display"))
				display_mode |= LED_DISP;
			if (!strcmp(token, "adisplay"))
				display_mode |= LED_ADISP;
			if (!strcmp(token, "normal"))
				ibm_ansi_order = 0;
			if (!strcmp(token, "ansi"))
				ibm_ansi_order = 1;
			if (!strcmp(token, "fast"))
				global_adapter_speed = 0;
			if (!strcmp(token, "medium"))
				global_adapter_speed = 4;
			if (!strcmp(token, "slow"))
				global_adapter_speed = 7;
			if ((*token == '-') || (isdigit(*token))) {
				if (!(j % 2) && (io_base < IM_MAX_HOSTS))
					io_port[io_base++] = simple_strtoul(token, NULL, 0);
				if ((j % 2) && (id_base < IM_MAX_HOSTS))
					scsi_id[id_base++] = simple_strtoul(token, NULL, 0);
				j++;
			}
		}
	} else if (ints) {
		for (i = 0; i < IM_MAX_HOSTS && 2 * i + 2 < ints[0]; i++) {
			io_port[i] = ints[2 * i + 2];
			scsi_id[i] = ints[2 * i + 2];
		}
	}
	return;
}

static int ibmmca_getinfo(char *buf, int slot, void *dev_id)
{
	struct Scsi_Host *shpnt;
	int len, speciale, connectore, k;
	unsigned int pos[8];
	unsigned long flags;
	struct Scsi_Host *dev = dev_id;

	spin_lock_irqsave(dev->host_lock, flags);
	
	shpnt = dev;		/* assign host-structure to local pointer */
	len = 0;		/* set filled text-buffer index to 0 */
	/* get the _special contents of the hostdata structure */
	speciale = ((struct ibmmca_hostdata *) shpnt->hostdata)->_special;
	connectore = ((struct ibmmca_hostdata *) shpnt->hostdata)->_connector_size;
	for (k = 2; k < 4; k++)
		pos[k] = ((struct ibmmca_hostdata *) shpnt->hostdata)->_pos[k];
	if (speciale == FORCED_DETECTION) {	/* forced detection */
		len += sprintf(buf + len,
			       "Adapter category: forced detected\n" "***************************************\n" "***  Forced detected SCSI Adapter   ***\n" "***  No chip-information available  ***\n" "***************************************\n");
	} else if (speciale == INTEGRATED_SCSI) {
		/* if the integrated subsystem has been found automatically: */
		len += sprintf(buf + len,
			       "Adapter category: integrated\n" "Chip revision level: %d\n" "Chip status: %s\n" "8 kByte NVRAM status: %s\n", ((pos[2] & 0xf0) >> 4), (pos[2] & 1) ? "enabled" : "disabled", (pos[2] & 2) ? "locked" : "accessible");
	} else if ((speciale >= 0) && (speciale < (sizeof(subsys_list) / sizeof(struct subsys_list_struct)))) {
		/* if the subsystem is a slot adapter */
		len += sprintf(buf + len, "Adapter category: slot-card\n" "ROM Segment Address: ");
		if ((pos[2] & 0xf0) == 0xf0)
			len += sprintf(buf + len, "off\n");
		else
			len += sprintf(buf + len, "0x%x\n", ((pos[2] & 0xf0) << 13) + 0xc0000);
		len += sprintf(buf + len, "Chip status: %s\n", (pos[2] & 1) ? "enabled" : "disabled");
		len += sprintf(buf + len, "Adapter I/O Offset: 0x%x\n", ((pos[2] & 0x0e) << 2));
	} else {
		len += sprintf(buf + len, "Adapter category: unknown\n");
	}
	/* common subsystem information to write to the slotn file */
	len += sprintf(buf + len, "Subsystem PUN: %d\n", shpnt->this_id);
	len += sprintf(buf + len, "I/O base address range: 0x%x-0x%x\n", (unsigned int) (shpnt->io_port), (unsigned int) (shpnt->io_port + 7));
	len += sprintf(buf + len, "MCA-slot size: %d bits", connectore);
	/* Now make sure, the bufferlength is devidable by 4 to avoid
	 * paging problems of the buffer. */
	while (len % sizeof(int) != (sizeof(int) - 1))
		len += sprintf(buf + len, " ");
	len += sprintf(buf + len, "\n");
	
	spin_unlock_irqrestore(shpnt->host_lock, flags);
	
	return len;
}

int ibmmca_detect(Scsi_Host_Template * scsi_template)
{
	struct Scsi_Host *shpnt;
	int port, id, i, j, k, list_size, slot;
	int devices_on_irq_11 = 0;
	int devices_on_irq_14 = 0;
	int IRQ14_registered = 0;
	int IRQ11_registered = 0;

	found = 0;		/* make absolutely sure, that found is set to 0 */

	/* First of all, print the version number of the driver. This is
	 * important to allow better user bugreports in case of already
	 * having problems with the MCA_bus probing. */
	printk(KERN_INFO "IBM MCA SCSI: Version %s\n", IBMMCA_SCSI_DRIVER_VERSION);
	/* if this is not MCA machine, return "nothing found" */
	if (!MCA_bus) {
		printk(KERN_INFO "IBM MCA SCSI:  No Microchannel-bus present --> Aborting.\n" "      	     This machine does not have any IBM MCA-bus\n" "    	     or the MCA-Kernel-support is not enabled!\n");
		return 0;
	}

#ifdef MODULE
	/* If the driver is run as module, read from conf.modules or cmd-line */
	if (boot_options)
		option_setup(boot_options);
#endif

	/* get interrupt request level */
	if (request_irq(IM_IRQ, interrupt_handler, SA_SHIRQ, "ibmmcascsi", hosts)) {
		printk(KERN_ERR "IBM MCA SCSI: Unable to get shared IRQ %d.\n", IM_IRQ);
		return 0;
	} else
		IRQ14_registered++;

	/* if ibmmcascsi setup option was passed to kernel, return "found" */
	for (i = 0; i < IM_MAX_HOSTS; i++)
		if (io_port[i] > 0 && scsi_id[i] >= 0 && scsi_id[i] < 8) {
			printk("IBM MCA SCSI: forced detected SCSI Adapter, io=0x%x, scsi id=%d.\n", io_port[i], scsi_id[i]);
			if ((shpnt = ibmmca_register(scsi_template, io_port[i], scsi_id[i], FORCED_DETECTION, "forced detected SCSI Adapter"))) {
				for (k = 2; k < 7; k++)
					((struct ibmmca_hostdata *) shpnt->hostdata)->_pos[k] = 0;
				((struct ibmmca_hostdata *) shpnt->hostdata)->_special = FORCED_DETECTION;
				mca_set_adapter_name(MCA_INTEGSCSI, "forced detected SCSI Adapter");
				mca_set_adapter_procfn(MCA_INTEGSCSI, (MCA_ProcFn) ibmmca_getinfo, shpnt);
				mca_mark_as_used(MCA_INTEGSCSI);
				devices_on_irq_14++;
			}
		}
	if (found)
		return found;

	/* The POS2-register of all PS/2 model SCSI-subsystems has the following
	 * interpretation of bits:
	 *                             Bit 7 - 4 : Chip Revision ID (Release)
	 *                             Bit 3 - 2 : Reserved
	 *                             Bit 1     : 8k NVRAM Disabled
	 *                             Bit 0     : Chip Enable (EN-Signal)
	 * The POS3-register is interpreted as follows:
	 *                             Bit 7 - 5 : SCSI ID
	 *                             Bit 4     : Reserved = 0
	 *                             Bit 3 - 0 : Reserved = 0
	 * (taken from "IBM, PS/2 Hardware Interface Technical Reference, Common
	 * Interfaces (1991)").
	 * In short words, this means, that IBM PS/2 machines only support
	 * 1 single subsystem by default. The slot-adapters must have another
	 * configuration on pos2. Here, one has to assume the following
	 * things for POS2-register:
	 *                             Bit 7 - 4 : Chip Revision ID (Release)
	 *                             Bit 3 - 1 : port offset factor
	 *                             Bit 0     : Chip Enable (EN-Signal)
	 * As I found a patch here, setting the IO-registers to 0x3540 forced,
	 * as there was a 0x05 in POS2 on a model 56, I assume, that the
	 * port 0x3540 must be fix for integrated SCSI-controllers.
	 * Ok, this discovery leads to the following implementation: (M.Lang) */

	/* first look for the IBM SCSI integrated subsystem on the motherboard */
	for (j = 0; j < 8; j++)	/* read the pos-information */
		pos[j] = mca_read_stored_pos(MCA_INTEGSCSI, j);
	/* pos2 = pos3 = 0xff if there is no integrated SCSI-subsystem present, but
	 * if we ignore the settings of all surrounding pos registers, it is not
	 * completely sufficient to only check pos2 and pos3. */
	/* Therefore, now the following if statement is used to
	 * make sure, we see a real integrated onboard SCSI-interface and no
	 * internal system information, which gets mapped to some pos registers
	 * on models 95xx. */
	if ((!pos[0] && !pos[1] && pos[2] > 0 && pos[3] > 0 && !pos[4] && !pos[5] && !pos[6] && !pos[7]) || (pos[0] == 0xff && pos[1] == 0xff && pos[2] < 0xff && pos[3] < 0xff && pos[4] == 0xff && pos[5] == 0xff && pos[6] == 0xff && pos[7] == 0xff)) {
		if ((pos[2] & 1) == 1)	/* is the subsystem chip enabled ? */
			port = IM_IO_PORT;
		else {		/* if disabled, no IRQs will be generated, as the chip won't
				 * listen to the incoming commands and will do really nothing,
				 * except for listening to the pos-register settings. If this
				 * happens, I need to hugely think about it, as one has to
				 * write something to the MCA-Bus pos register in order to
				 * enable the chip. Normally, IBM-SCSI won't pass the POST,
				 * when the chip is disabled (see IBM tech. ref.). */
			port = IM_IO_PORT;	/* anyway, set the portnumber and warn */
			printk("IBM MCA SCSI: WARNING - Your SCSI-subsystem is disabled!\n" "              SCSI-operations may not work.\n");
		}
		id = (pos[3] & 0xe0) >> 5;	/* this is correct and represents the PUN */
		/* give detailed information on the subsystem. This helps me
		 * additionally during debugging and analyzing bug-reports. */
		printk(KERN_INFO "IBM MCA SCSI: IBM Integrated SCSI Controller ffound, io=0x%x, scsi id=%d,\n", port, id);
		printk(KERN_INFO "              chip rev.=%d, 8K NVRAM=%s, subsystem=%s\n", ((pos[2] & 0xf0) >> 4), (pos[2] & 2) ? "locked" : "accessible", (pos[2] & 1) ? "enabled." : "disabled.");

		/* register the found integrated SCSI-subsystem */
		if ((shpnt = ibmmca_register(scsi_template, port, id, INTEGRATED_SCSI, "IBM Integrated SCSI Controller"))) 
		{
			for (k = 2; k < 7; k++)
				((struct ibmmca_hostdata *) shpnt->hostdata)->_pos[k] = pos[k];
			((struct ibmmca_hostdata *) shpnt->hostdata)->_special = INTEGRATED_SCSI;
			mca_set_adapter_name(MCA_INTEGSCSI, "IBM Integrated SCSI Controller");
			mca_set_adapter_procfn(MCA_INTEGSCSI, (MCA_ProcFn) ibmmca_getinfo, shpnt);
			mca_mark_as_used(MCA_INTEGSCSI);
			devices_on_irq_14++;
		}
	}

	/* now look for other adapters in MCA slots, */
	/* determine the number of known IBM-SCSI-subsystem types */
	/* see the pos[2] dependence to get the adapter port-offset. */
	list_size = sizeof(subsys_list) / sizeof(struct subsys_list_struct);
	for (i = 0; i < list_size; i++) {
		/* scan each slot for a fitting adapter id */
		slot = 0;	/* start at slot 0 */
		while ((slot = mca_find_adapter(subsys_list[i].mca_id, slot))
		       != MCA_NOTFOUND) {	/* scan through all slots */
			for (j = 0; j < 8; j++)	/* read the pos-information */
				pos[j] = mca_read_stored_pos(slot, j);
			if ((pos[2] & 1) == 1)
				/* is the subsystem chip enabled ? */
				/* (explanations see above) */
				port = IM_IO_PORT + ((pos[2] & 0x0e) << 2);
			else {
				/* anyway, set the portnumber and warn */
				port = IM_IO_PORT + ((pos[2] & 0x0e) << 2);
				printk(KERN_WARNING "IBM MCA SCSI: WARNING - Your SCSI-subsystem is disabled!\n");
				printk(KERN_WARNING "              SCSI-operations may not work.\n");
			}
			if ((i == IBM_SCSI2_FW) && (pos[6] != 0)) {
				printk(KERN_ERR "IBM MCA SCSI: ERROR - Wrong POS(6)-register setting!\n");
				printk(KERN_ERR "              Impossible to determine adapter PUN!\n");
				printk(KERN_ERR "              Guessing adapter PUN = 7.\n");
				id = 7;
			} else {
				id = (pos[3] & 0xe0) >> 5;	/* get subsystem PUN */
				if (i == IBM_SCSI2_FW) {
					id |= (pos[3] & 0x10) >> 1;	/* get subsystem PUN high-bit
									 * for F/W adapters */
				}
			}
			if ((i == IBM_SCSI2_FW) && (pos[4] & 0x01) && (pos[6] == 0)) {
				/* IRQ11 is used by SCSI-2 F/W Adapter/A */
				printk(KERN_DEBUG "IBM MCA SCSI: SCSI-2 F/W adapter needs IRQ 11.\n");
				/* get interrupt request level */
				if (request_irq(IM_IRQ_FW, interrupt_handler, SA_SHIRQ, "ibmmcascsi", hosts)) {
					printk(KERN_ERR "IBM MCA SCSI: Unable to get shared IRQ %d.\n", IM_IRQ_FW);
				} else
					IRQ11_registered++;
			}
			printk(KERN_INFO "IBM MCA SCSI: %s found in slot %d, io=0x%x, scsi id=%d,\n", subsys_list[i].description, slot + 1, port, id);
			if ((pos[2] & 0xf0) == 0xf0)
				printk(KERN_DEBUG"              ROM Addr.=off,");
			else
				printk(KERN_DEBUG "              ROM Addr.=0x%x,", ((pos[2] & 0xf0) << 13) + 0xc0000);
			printk(KERN_DEBUG " port-offset=0x%x, subsystem=%s\n", ((pos[2] & 0x0e) << 2), (pos[2] & 1) ? "enabled." : "disabled.");

			/* register the hostadapter */
			if ((shpnt = ibmmca_register(scsi_template, port, id, i, subsys_list[i].description))) {
				for (k = 2; k < 8; k++)
					((struct ibmmca_hostdata *) shpnt->hostdata)->_pos[k] = pos[k];
				((struct ibmmca_hostdata *) shpnt->hostdata)->_special = i;
				mca_set_adapter_name(slot, subsys_list[i].description);
				mca_set_adapter_procfn(slot, (MCA_ProcFn) ibmmca_getinfo, shpnt);
				mca_mark_as_used(slot);
				if ((i == IBM_SCSI2_FW) && (pos[4] & 0x01) && (pos[6] == 0))
					devices_on_irq_11++;
				else
					devices_on_irq_14++;
			}
			slot++;	/* advance to next slot */
		}		/* advance to next adapter id in the list of IBM-SCSI-subsystems */
	}

	/* now check for SCSI-adapters, mapped to the integrated SCSI
	 * area. E.g. a W/Cache in MCA-slot 9(!). Do the check correct here,
	 * as this is a known effect on some models 95xx. */
	list_size = sizeof(subsys_list) / sizeof(struct subsys_list_struct);
	for (i = 0; i < list_size; i++) {
		/* scan each slot for a fitting adapter id */
		slot = mca_find_adapter(subsys_list[i].mca_id, MCA_INTEGSCSI);
		if (slot != MCA_NOTFOUND) {	/* scan through all slots */
			for (j = 0; j < 8; j++)	/* read the pos-information */
				pos[j] = mca_read_stored_pos(slot, j);
			if ((pos[2] & 1) == 1) {	/* is the subsystem chip enabled ? */
				/* (explanations see above) */
				port = IM_IO_PORT + ((pos[2] & 0x0e) << 2);
			} else {	/* anyway, set the portnumber and warn */
				port = IM_IO_PORT + ((pos[2] & 0x0e) << 2);
				printk(KERN_WARNING "IBM MCA SCSI: WARNING - Your SCSI-subsystem is disabled!\n");
				printk(KERN_WARNING "              SCSI-operations may not work.\n");
			}
			if ((i == IBM_SCSI2_FW) && (pos[6] != 0)) {
				printk(KERN_ERR "IBM MCA SCSI: ERROR - Wrong POS(6)-register setting!\n");
				printk(KERN_ERR  "              Impossible to determine adapter PUN!\n");
				printk(KERN_ERR "              Guessing adapter PUN = 7.\n");
				id = 7;
			} else {
				id = (pos[3] & 0xe0) >> 5;	/* get subsystem PUN */
				if (i == IBM_SCSI2_FW)
					id |= (pos[3] & 0x10) >> 1;	/* get subsystem PUN high-bit
									 * for F/W adapters */
			}
			if ((i == IBM_SCSI2_FW) && (pos[4] & 0x01) && (pos[6] == 0)) {
				/* IRQ11 is used by SCSI-2 F/W Adapter/A */
				printk(KERN_DEBUG  "IBM MCA SCSI: SCSI-2 F/W adapter needs IRQ 11.\n");
				/* get interrupt request level */
				if (request_irq(IM_IRQ_FW, interrupt_handler, SA_SHIRQ, "ibmmcascsi", hosts))
					printk(KERN_ERR "IBM MCA SCSI: Unable to get shared IRQ %d.\n", IM_IRQ_FW);
				else
					IRQ11_registered++;
			}
			printk(KERN_INFO "IBM MCA SCSI: %s found in slot %d, io=0x%x, scsi id=%d,\n", subsys_list[i].description, slot + 1, port, id);
			if ((pos[2] & 0xf0) == 0xf0)
				printk(KERN_DEBUG "              ROM Addr.=off,");
			else
				printk(KERN_DEBUG "              ROM Addr.=0x%x,", ((pos[2] & 0xf0) << 13) + 0xc0000);
			printk(KERN_DEBUG " port-offset=0x%x, subsystem=%s\n", ((pos[2] & 0x0e) << 2), (pos[2] & 1) ? "enabled." : "disabled.");

			/* register the hostadapter */
			if ((shpnt = ibmmca_register(scsi_template, port, id, i, subsys_list[i].description))) {
				for (k = 2; k < 7; k++)
					((struct ibmmca_hostdata *) shpnt->hostdata)->_pos[k] = pos[k];
				((struct ibmmca_hostdata *) shpnt->hostdata)->_special = i;
				mca_set_adapter_name(slot, subsys_list[i].description);
				mca_set_adapter_procfn(slot, (MCA_ProcFn) ibmmca_getinfo, shpnt);
				mca_mark_as_used(slot);
				if ((i == IBM_SCSI2_FW) && (pos[4] & 0x01) && (pos[6] == 0))
					devices_on_irq_11++;
				else
					devices_on_irq_14++;
			}
			slot++;	/* advance to next slot */
		}		/* advance to next adapter id in the list of IBM-SCSI-subsystems */
	}
	if (IRQ11_registered && !devices_on_irq_11)
		free_irq(IM_IRQ_FW, hosts);	/* no devices on IRQ 11 */
	if (IRQ14_registered && !devices_on_irq_14)
		free_irq(IM_IRQ, hosts);	/* no devices on IRQ 14 */
	if (!devices_on_irq_11 && !devices_on_irq_14)
		printk(KERN_WARNING "IBM MCA SCSI: No IBM SCSI-subsystem adapter attached.\n");
	return found;		/* return the number of found SCSI hosts. Should be 1 or 0. */
}

static struct Scsi_Host *ibmmca_register(Scsi_Host_Template * scsi_template, int port, int id, int adaptertype, char *hostname)
{
	struct Scsi_Host *shpnt;
	int i, j;
	unsigned int ctrl;

	/* check I/O region */
	if (!request_region(port, IM_N_IO_PORT, hostname)) {
		printk(KERN_ERR "IBM MCA SCSI: Unable to get I/O region 0x%x-0x%x (%d ports).\n", port, port + IM_N_IO_PORT - 1, IM_N_IO_PORT);
		return NULL;
	}

	/* register host */
	shpnt = scsi_register(scsi_template, sizeof(struct ibmmca_hostdata));
	if (!shpnt) {
		printk(KERN_ERR "IBM MCA SCSI: Unable to register host.\n");
		release_region(port, IM_N_IO_PORT);
		return NULL;
	}

	/* request I/O region */
	hosts[found] = shpnt;	/* add new found hostadapter to the list */
	special(found) = adaptertype;	/* important assignment or else crash! */
	subsystem_connector_size(found) = 0;	/* preset slot-size */
	shpnt->irq = IM_IRQ;	/* assign necessary stuff for the adapter */
	shpnt->io_port = port;
	shpnt->n_io_port = IM_N_IO_PORT;
	shpnt->this_id = id;
	shpnt->max_id = 8;	/* 8 PUNs are default */
	/* now, the SCSI-subsystem is connected to Linux */

	ctrl = (unsigned int) (inb(IM_CTR_REG(found)));	/* get control-register status */
#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Control Register contents: %x, status: %x\n", ctrl, inb(IM_STAT_REG(found)));
	printk("IBM MCA SCSI: This adapters' POS-registers: ");
	for (i = 0; i < 8; i++)
		printk("%x ", pos[i]);
	printk("\n");
#endif
	reset_status(found) = IM_RESET_NOT_IN_PROGRESS;

	for (i = 0; i < 16; i++)	/* reset the tables */
		for (j = 0; j < 8; j++)
			get_ldn(found)[i][j] = MAX_LOG_DEV;

	/* check which logical devices exist */
	/* after this line, local interrupting is possible: */
	local_checking_phase_flag(found) = 1;
	check_devices(found, adaptertype);	/* call by value, using the global variable hosts */
	local_checking_phase_flag(found) = 0;
	found++;		/* now increase index to be prepared for next found subsystem */
	/* an ibm mca subsystem has been detected */
	return shpnt;
}

static int ibmmca_release(struct Scsi_Host *shpnt)
{
	release_region(shpnt->io_port, shpnt->n_io_port);
	if (!(--found))
		free_irq(shpnt->irq, hosts);
	return 0;
}

/* The following routine is the SCSI command queue for the midlevel driver */
static int ibmmca_queuecommand(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
	unsigned int ldn;
	unsigned int scsi_cmd;
	struct im_scb *scb;
	struct Scsi_Host *shpnt;
	int current_ldn;
	int id, lun;
	int target;
	int host_index;
	int max_pun;
	int i;
	struct scatterlist *sl;

	shpnt = cmd->device->host;
	/* search for the right hostadapter */
	for (host_index = 0; hosts[host_index] && hosts[host_index]->host_no != shpnt->host_no; host_index++);

	if (!hosts[host_index]) {	/* invalid hostadapter descriptor address */
		cmd->result = DID_NO_CONNECT << 16;
		if (done)
			done(cmd);
		return 0;
	}
	max_pun = subsystem_maxid(host_index);
	if (ibm_ansi_order) {
		target = max_pun - 1 - cmd->device->id;
		if ((target <= subsystem_pun(host_index)) && (cmd->device->id <= subsystem_pun(host_index)))
			target--;
		else if ((target >= subsystem_pun(host_index)) && (cmd->device->id >= subsystem_pun(host_index)))
			target++;
	} else
		target = cmd->device->id;

	/* if (target,lun) is NO LUN or not existing at all, return error */
	if ((get_scsi(host_index)[target][cmd->device->lun] == TYPE_NO_LUN) || (get_scsi(host_index)[target][cmd->device->lun] == TYPE_NO_DEVICE)) {
		cmd->result = DID_NO_CONNECT << 16;
		if (done)
			done(cmd);
		return 0;
	}

	/*if (target,lun) unassigned, do further checks... */
	ldn = get_ldn(host_index)[target][cmd->device->lun];
	if (ldn >= MAX_LOG_DEV) {	/* on invalid ldn do special stuff */
		if (ldn > MAX_LOG_DEV) {	/* dynamical remapping if ldn unassigned */
			current_ldn = next_ldn(host_index);	/* stop-value for one circle */
			while (ld(host_index)[next_ldn(host_index)].cmd) {	/* search for a occupied, but not in */
				/* command-processing ldn. */
				next_ldn(host_index)++;
				if (next_ldn(host_index) >= MAX_LOG_DEV)
					next_ldn(host_index) = 7;
				if (current_ldn == next_ldn(host_index)) {	/* One circle done ? */
					/* no non-processing ldn found */
					scmd_printk(KERN_WARNING, cmd,
	"IBM MCA SCSI: Cannot assign SCSI-device dynamically!\n"
	"              On ldn 7-14 SCSI-commands everywhere in progress.\n"
	"              Reporting DID_NO_CONNECT for device.\n");
					cmd->result = DID_NO_CONNECT << 16;	/* return no connect */
					if (done)
						done(cmd);
					return 0;
				}
			}

			/* unmap non-processing ldn */
			for (id = 0; id < max_pun; id++)
				for (lun = 0; lun < 8; lun++) {
					if (get_ldn(host_index)[id][lun] == next_ldn(host_index)) {
						get_ldn(host_index)[id][lun] = TYPE_NO_DEVICE;
						get_scsi(host_index)[id][lun] = TYPE_NO_DEVICE;
						/* unmap entry */
					}
				}
			/* set reduced interrupt_handler-mode for checking */
			local_checking_phase_flag(host_index) = 1;
			/* map found ldn to pun,lun */
			get_ldn(host_index)[target][cmd->device->lun] = next_ldn(host_index);
			/* change ldn to the right value, that is now next_ldn */
			ldn = next_ldn(host_index);
			/* unassign all ldns (pun,lun,ldn does not matter for remove) */
			immediate_assign(host_index, 0, 0, 0, REMOVE_LDN);
			/* set only LDN for remapped device */
			immediate_assign(host_index, target, cmd->device->lun, ldn, SET_LDN);
			/* get device information for ld[ldn] */
			if (device_exists(host_index, ldn, &ld(host_index)[ldn].block_length, &ld(host_index)[ldn].device_type)) {
				ld(host_index)[ldn].cmd = NULL;	/* To prevent panic set 0, because
								   devices that were not assigned,
								   should have nothing in progress. */
				get_scsi(host_index)[target][cmd->device->lun] = ld(host_index)[ldn].device_type;
				/* increase assignment counters for statistics in /proc */
				IBM_DS(host_index).dynamical_assignments++;
				IBM_DS(host_index).ldn_assignments[ldn]++;
			} else
				/* panic here, because a device, found at boottime has
				   vanished */
				panic("IBM MCA SCSI: ldn=0x%x, SCSI-device on (%d,%d) vanished!\n", ldn, target, cmd->device->lun);
			/* unassign again all ldns (pun,lun,ldn does not matter for remove) */
			immediate_assign(host_index, 0, 0, 0, REMOVE_LDN);
			/* remap all ldns, as written in the pun/lun table */
			lun = 0;
#ifdef CONFIG_SCSI_MULTI_LUN
			for (lun = 0; lun < 8; lun++)
#endif
				for (id = 0; id < max_pun; id++) {
					if (get_ldn(host_index)[id][lun] <= MAX_LOG_DEV)
						immediate_assign(host_index, id, lun, get_ldn(host_index)[id][lun], SET_LDN);
				}
			/* set back to normal interrupt_handling */
			local_checking_phase_flag(host_index) = 0;
#ifdef IM_DEBUG_PROBE
			/* Information on syslog terminal */
			printk("IBM MCA SCSI: ldn=0x%x dynamically reassigned to (%d,%d).\n", ldn, target, cmd->device->lun);
#endif
			/* increase next_ldn for next dynamical assignment */
			next_ldn(host_index)++;
			if (next_ldn(host_index) >= MAX_LOG_DEV)
				next_ldn(host_index) = 7;
		} else {	/* wall against Linux accesses to the subsystem adapter */
			cmd->result = DID_BAD_TARGET << 16;
			if (done)
				done(cmd);
			return 0;
		}
	}

	/*verify there is no command already in progress for this log dev */
	if (ld(host_index)[ldn].cmd)
		panic("IBM MCA SCSI: cmd already in progress for this ldn.\n");

	/*save done in cmd, and save cmd for the interrupt handler */
	cmd->scsi_done = done;
	ld(host_index)[ldn].cmd = cmd;

	/*fill scb information independent of the scsi command */
	scb = &(ld(host_index)[ldn].scb);
	ld(host_index)[ldn].tsb.dev_status = 0;
	scb->enable = IM_REPORT_TSB_ONLY_ON_ERROR | IM_RETRY_ENABLE;
	scb->tsb_adr = isa_virt_to_bus(&(ld(host_index)[ldn].tsb));
	scsi_cmd = cmd->cmnd[0];

	if (cmd->use_sg) {
		i = cmd->use_sg;
		sl = (struct scatterlist *) (cmd->request_buffer);
		if (i > 16)
			panic("IBM MCA SCSI: scatter-gather list too long.\n");
		while (--i >= 0) {
			ld(host_index)[ldn].sge[i].address = (void *) (isa_page_to_bus(sl[i].page) + sl[i].offset);
			ld(host_index)[ldn].sge[i].byte_length = sl[i].length;
		}
		scb->enable |= IM_POINTER_TO_LIST;
		scb->sys_buf_adr = isa_virt_to_bus(&(ld(host_index)[ldn].sge[0]));
		scb->sys_buf_length = cmd->use_sg * sizeof(struct im_sge);
	} else {
		scb->sys_buf_adr = isa_virt_to_bus(cmd->request_buffer);
		/* recent Linux midlevel SCSI places 1024 byte for inquiry
		 * command. Far too much for old PS/2 hardware. */
		switch (scsi_cmd) {
			/* avoid command errors by setting bufferlengths to
			 * ANSI-standard. Beware of forcing it to 255,
			 * this could SEGV the kernel!!! */
		case INQUIRY:
		case REQUEST_SENSE:
		case MODE_SENSE:
		case MODE_SELECT:
			if (cmd->request_bufflen > 255)
				scb->sys_buf_length = 255;
			else
				scb->sys_buf_length = cmd->request_bufflen;
			break;
		case TEST_UNIT_READY:
			scb->sys_buf_length = 0;
			break;
		default:
			scb->sys_buf_length = cmd->request_bufflen;
			break;
		}
	}
	/*fill scb information dependent on scsi command */

#ifdef IM_DEBUG_CMD
	printk("issue scsi cmd=%02x to ldn=%d\n", scsi_cmd, ldn);
#endif

	/* for specific device-type debugging: */
#ifdef IM_DEBUG_CMD_SPEC_DEV
	if (ld(host_index)[ldn].device_type == IM_DEBUG_CMD_DEVICE)
		printk("(SCSI-device-type=0x%x) issue scsi cmd=%02x to ldn=%d\n", ld(host_index)[ldn].device_type, scsi_cmd, ldn);
#endif

	/* for possible panics store current command */
	last_scsi_command(host_index)[ldn] = scsi_cmd;
	last_scsi_type(host_index)[ldn] = IM_SCB;
	/* update statistical info */
	IBM_DS(host_index).total_accesses++;
	IBM_DS(host_index).ldn_access[ldn]++;

	switch (scsi_cmd) {
	case READ_6:
	case WRITE_6:
	case READ_10:
	case WRITE_10:
	case READ_12:
	case WRITE_12:
		/* Distinguish between disk and other devices. Only disks (that are the
		   most frequently accessed devices) should be supported by the
		   IBM-SCSI-Subsystem commands. */
		switch (ld(host_index)[ldn].device_type) {
		case TYPE_DISK:	/* for harddisks enter here ... */
		case TYPE_MOD:	/* ... try it also for MO-drives (send flames as */
			/*     you like, if this won't work.) */
			if (scsi_cmd == READ_6 || scsi_cmd == READ_10 || scsi_cmd == READ_12) {
				/* read command preparations */
				scb->enable |= IM_READ_CONTROL;
				IBM_DS(host_index).ldn_read_access[ldn]++;	/* increase READ-access on ldn stat. */
				scb->command = IM_READ_DATA_CMD | IM_NO_DISCONNECT;
			} else {	/* write command preparations */
				IBM_DS(host_index).ldn_write_access[ldn]++;	/* increase write-count on ldn stat. */
				scb->command = IM_WRITE_DATA_CMD | IM_NO_DISCONNECT;
			}
			if (scsi_cmd == READ_6 || scsi_cmd == WRITE_6) {
				scb->u1.log_blk_adr = (((unsigned) cmd->cmnd[3]) << 0) | (((unsigned) cmd->cmnd[2]) << 8) | ((((unsigned) cmd->cmnd[1]) & 0x1f) << 16);
				scb->u2.blk.count = (unsigned) cmd->cmnd[4];
			} else {
				scb->u1.log_blk_adr = (((unsigned) cmd->cmnd[5]) << 0) | (((unsigned) cmd->cmnd[4]) << 8) | (((unsigned) cmd->cmnd[3]) << 16) | (((unsigned) cmd->cmnd[2]) << 24);
				scb->u2.blk.count = (((unsigned) cmd->cmnd[8]) << 0) | (((unsigned) cmd->cmnd[7]) << 8);
			}
			last_scsi_logical_block(host_index)[ldn] = scb->u1.log_blk_adr;
			last_scsi_blockcount(host_index)[ldn] = scb->u2.blk.count;
			scb->u2.blk.length = ld(host_index)[ldn].block_length;
			break;
			/* for other devices, enter here. Other types are not known by
			   Linux! TYPE_NO_LUN is forbidden as valid device. */
		case TYPE_ROM:
		case TYPE_TAPE:
		case TYPE_PROCESSOR:
		case TYPE_WORM:
		case TYPE_SCANNER:
		case TYPE_MEDIUM_CHANGER:
			/* If there is a sequential-device, IBM recommends to use
			   IM_OTHER_SCSI_CMD_CMD instead of subsystem READ/WRITE.
			   This includes CD-ROM devices, too, due to the partial sequential
			   read capabilities. */
			scb->command = IM_OTHER_SCSI_CMD_CMD;
			if (scsi_cmd == READ_6 || scsi_cmd == READ_10 || scsi_cmd == READ_12)
				/* enable READ */
				scb->enable |= IM_READ_CONTROL;
			scb->enable |= IM_BYPASS_BUFFER;
			scb->u1.scsi_cmd_length = cmd->cmd_len;
			memcpy(scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
			last_scsi_type(host_index)[ldn] = IM_LONG_SCB;
			/* Read/write on this non-disk devices is also displayworthy,
			   so flash-up the LED/display. */
			break;
		}
		break;
	case INQUIRY:
		IBM_DS(host_index).ldn_inquiry_access[ldn]++;
		scb->command = IM_DEVICE_INQUIRY_CMD;
		scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT | IM_BYPASS_BUFFER;
		scb->u1.log_blk_adr = 0;
		break;
	case TEST_UNIT_READY:
		scb->command = IM_OTHER_SCSI_CMD_CMD;
		scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT | IM_BYPASS_BUFFER;
		scb->u1.log_blk_adr = 0;
		scb->u1.scsi_cmd_length = 6;
		memcpy(scb->u2.scsi_command, cmd->cmnd, 6);
		last_scsi_type(host_index)[ldn] = IM_LONG_SCB;
		break;
	case READ_CAPACITY:
		/* the length of system memory buffer must be exactly 8 bytes */
		scb->command = IM_READ_CAPACITY_CMD;
		scb->enable |= IM_READ_CONTROL | IM_BYPASS_BUFFER;
		if (scb->sys_buf_length > 8)
			scb->sys_buf_length = 8;
		break;
		/* Commands that need read-only-mode (system <- device): */
	case REQUEST_SENSE:
		scb->command = IM_REQUEST_SENSE_CMD;
		scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT | IM_BYPASS_BUFFER;
		break;
		/* Commands that need write-only-mode (system -> device): */
	case MODE_SELECT:
	case MODE_SELECT_10:
		IBM_DS(host_index).ldn_modeselect_access[ldn]++;
		scb->command = IM_OTHER_SCSI_CMD_CMD;
		scb->enable |= IM_SUPRESS_EXCEPTION_SHORT | IM_BYPASS_BUFFER;	/*Select needs WRITE-enabled */
		scb->u1.scsi_cmd_length = cmd->cmd_len;
		memcpy(scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
		last_scsi_type(host_index)[ldn] = IM_LONG_SCB;
		break;
		/* For other commands, read-only is useful. Most other commands are
		   running without an input-data-block. */
	default:
		scb->command = IM_OTHER_SCSI_CMD_CMD;
		scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT | IM_BYPASS_BUFFER;
		scb->u1.scsi_cmd_length = cmd->cmd_len;
		memcpy(scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
		last_scsi_type(host_index)[ldn] = IM_LONG_SCB;
		break;
	}
	/*issue scb command, and return */
	if (++disk_rw_in_progress == 1)
		PS2_DISK_LED_ON(shpnt->host_no, target);

	if (last_scsi_type(host_index)[ldn] == IM_LONG_SCB) {
		issue_cmd(host_index, isa_virt_to_bus(scb), IM_LONG_SCB | ldn);
		IBM_DS(host_index).long_scbs++;
	} else {
		issue_cmd(host_index, isa_virt_to_bus(scb), IM_SCB | ldn);
		IBM_DS(host_index).scbs++;
	}
	return 0;
}

static int __ibmmca_abort(Scsi_Cmnd * cmd)
{
	/* Abort does not work, as the adapter never generates an interrupt on
	 * whatever situation is simulated, even when really pending commands
	 * are running on the adapters' hardware ! */

	struct Scsi_Host *shpnt;
	unsigned int ldn;
	void (*saved_done) (Scsi_Cmnd *);
	int target;
	int host_index;
	int max_pun;
	unsigned long imm_command;

#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Abort subroutine called...\n");
#endif

	shpnt = cmd->device->host;
	/* search for the right hostadapter */
	for (host_index = 0; hosts[host_index] && hosts[host_index]->host_no != shpnt->host_no; host_index++);

	if (!hosts[host_index]) {	/* invalid hostadapter descriptor address */
		cmd->result = DID_NO_CONNECT << 16;
		if (cmd->scsi_done)
			(cmd->scsi_done) (cmd);
		shpnt = cmd->device->host;
#ifdef IM_DEBUG_PROBE
		printk(KERN_DEBUG "IBM MCA SCSI: Abort adapter selection failed!\n");
#endif
		return SUCCESS;
	}
	max_pun = subsystem_maxid(host_index);
	if (ibm_ansi_order) {
		target = max_pun - 1 - cmd->device->id;
		if ((target <= subsystem_pun(host_index)) && (cmd->device->id <= subsystem_pun(host_index)))
			target--;
		else if ((target >= subsystem_pun(host_index)) && (cmd->device->id >= subsystem_pun(host_index)))
			target++;
	} else
		target = cmd->device->id;

	/* get logical device number, and disable system interrupts */
	printk(KERN_WARNING "IBM MCA SCSI: Sending abort to device pun=%d, lun=%d.\n", target, cmd->device->lun);
	ldn = get_ldn(host_index)[target][cmd->device->lun];

	/*if cmd for this ldn has already finished, no need to abort */
	if (!ld(host_index)[ldn].cmd) {
		    return SUCCESS;
	}

	/* Clear ld.cmd, save done function, install internal done,
	 * send abort immediate command (this enables sys. interrupts),
	 * and wait until the interrupt arrives.
	 */
	saved_done = cmd->scsi_done;
	cmd->scsi_done = internal_done;
	cmd->SCp.Status = 0;
	last_scsi_command(host_index)[ldn] = IM_ABORT_IMM_CMD;
	last_scsi_type(host_index)[ldn] = IM_IMM_CMD;
	imm_command = inl(IM_CMD_REG(host_index));
	imm_command &= (unsigned long) (0xffff0000);	/* mask reserved stuff */
	imm_command |= (unsigned long) (IM_ABORT_IMM_CMD);
	/* must wait for attention reg not busy */
	/* FIXME - timeout, politeness */
	while (1) {
		if (!(inb(IM_STAT_REG(host_index)) & IM_BUSY))
			break;
	}
	/* write registers and enable system interrupts */
	outl(imm_command, IM_CMD_REG(host_index));
	outb(IM_IMM_CMD | ldn, IM_ATTN_REG(host_index));
#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Abort queued to adapter...\n");
#endif
	spin_unlock_irq(shpnt->host_lock);
	while (!cmd->SCp.Status)
		yield();
	spin_lock_irq(shpnt->host_lock);
	cmd->scsi_done = saved_done;
#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Abort returned with adapter response...\n");
#endif

	/*if abort went well, call saved done, then return success or error */
	if (cmd->result == (DID_ABORT << 16)) 
	{
		cmd->result |= DID_ABORT << 16;
		if (cmd->scsi_done)
			(cmd->scsi_done) (cmd);
		ld(host_index)[ldn].cmd = NULL;
#ifdef IM_DEBUG_PROBE
		printk("IBM MCA SCSI: Abort finished with success.\n");
#endif
		return SUCCESS;
	} else {
		cmd->result |= DID_NO_CONNECT << 16;
		if (cmd->scsi_done)
			(cmd->scsi_done) (cmd);
		ld(host_index)[ldn].cmd = NULL;
#ifdef IM_DEBUG_PROBE
		printk("IBM MCA SCSI: Abort failed.\n");
#endif
		return FAILED;
	}
}

static int ibmmca_abort(Scsi_Cmnd * cmd)
{
	struct Scsi_Host *shpnt = cmd->device->host;
	int rc;

	spin_lock_irq(shpnt->host_lock);
	rc = __ibmmca_abort(cmd);
	spin_unlock_irq(shpnt->host_lock);

	return rc;
}

static int __ibmmca_host_reset(Scsi_Cmnd * cmd)
{
	struct Scsi_Host *shpnt;
	Scsi_Cmnd *cmd_aid;
	int ticks, i;
	int host_index;
	unsigned long imm_command;

	if (cmd == NULL)
		BUG();

	ticks = IM_RESET_DELAY * HZ;
	shpnt = cmd->device->host;
	/* search for the right hostadapter */
	for (host_index = 0; hosts[host_index] && hosts[host_index]->host_no != shpnt->host_no; host_index++);

	if (!hosts[host_index])	/* invalid hostadapter descriptor address */
		return FAILED;

	if (local_checking_phase_flag(host_index)) {
		printk(KERN_WARNING "IBM MCA SCSI: unable to reset while checking devices.\n");
		return FAILED;
	}

	/* issue reset immediate command to subsystem, and wait for interrupt */
	printk("IBM MCA SCSI: resetting all devices.\n");
	reset_status(host_index) = IM_RESET_IN_PROGRESS;
	last_scsi_command(host_index)[0xf] = IM_RESET_IMM_CMD;
	last_scsi_type(host_index)[0xf] = IM_IMM_CMD;
	imm_command = inl(IM_CMD_REG(host_index));
	imm_command &= (unsigned long) (0xffff0000);	/* mask reserved stuff */
	imm_command |= (unsigned long) (IM_RESET_IMM_CMD);
	/* must wait for attention reg not busy */
	while (1) {
		if (!(inb(IM_STAT_REG(host_index)) & IM_BUSY))
			break;
		spin_unlock_irq(shpnt->host_lock);
		yield();
		spin_lock_irq(shpnt->host_lock);
	}
	/*write registers and enable system interrupts */
	outl(imm_command, IM_CMD_REG(host_index));
	outb(IM_IMM_CMD | 0xf, IM_ATTN_REG(host_index));
	/* wait for interrupt finished or intr_stat register to be set, as the
	 * interrupt will not be executed, while we are in here! */
	 
	/* FIXME: This is really really icky we so want a sleeping version of this ! */
	while (reset_status(host_index) == IM_RESET_IN_PROGRESS && --ticks && ((inb(IM_INTR_REG(host_index)) & 0x8f) != 0x8f)) {
		udelay((1 + 999 / HZ) * 1000);
		barrier();
	}
	/* if reset did not complete, just return an error */
	if (!ticks) {
		printk(KERN_ERR "IBM MCA SCSI: reset did not complete within %d seconds.\n", IM_RESET_DELAY);
		reset_status(host_index) = IM_RESET_FINISHED_FAIL;
		return FAILED;
	}

	if ((inb(IM_INTR_REG(host_index)) & 0x8f) == 0x8f) {
		/* analysis done by this routine and not by the intr-routine */
		if (inb(IM_INTR_REG(host_index)) == 0xaf)
			reset_status(host_index) = IM_RESET_FINISHED_OK_NO_INT;
		else if (inb(IM_INTR_REG(host_index)) == 0xcf)
			reset_status(host_index) = IM_RESET_FINISHED_FAIL;
		else		/* failed, 4get it */
			reset_status(host_index) = IM_RESET_NOT_IN_PROGRESS_NO_INT;
		outb(IM_EOI | 0xf, IM_ATTN_REG(host_index));
	}

	/* if reset failed, just return an error */
	if (reset_status(host_index) == IM_RESET_FINISHED_FAIL) {
		printk(KERN_ERR "IBM MCA SCSI: reset failed.\n");
		return FAILED;
	}

	/* so reset finished ok - call outstanding done's, and return success */
	printk(KERN_INFO "IBM MCA SCSI: Reset successfully completed.\n");
	for (i = 0; i < MAX_LOG_DEV; i++) {
		cmd_aid = ld(host_index)[i].cmd;
		if (cmd_aid && cmd_aid->scsi_done) {
			ld(host_index)[i].cmd = NULL;
			cmd_aid->result = DID_RESET << 16;
		}
	}
	return SUCCESS;
}

static int ibmmca_host_reset(Scsi_Cmnd * cmd)
{
	struct Scsi_Host *shpnt = cmd->device->host;
	int rc;

	spin_lock_irq(shpnt->host_lock);
	rc = __ibmmca_host_reset(cmd);
	spin_unlock_irq(shpnt->host_lock);

	return rc;
}

static int ibmmca_biosparam(struct scsi_device *sdev, struct block_device *bdev, sector_t capacity, int *info)
{
	int size = capacity;
	info[0] = 64;
	info[1] = 32;
	info[2] = size / (info[0] * info[1]);
	if (info[2] >= 1024) {
		info[0] = 128;
		info[1] = 63;
		info[2] = size / (info[0] * info[1]);
		if (info[2] >= 1024) {
			info[0] = 255;
			info[1] = 63;
			info[2] = size / (info[0] * info[1]);
			if (info[2] >= 1024)
				info[2] = 1023;
		}
	}
	return 0;
}

/* calculate percentage of total accesses on a ldn */
static int ldn_access_load(int host_index, int ldn)
{
	if (IBM_DS(host_index).total_accesses == 0)
		return (0);
	if (IBM_DS(host_index).ldn_access[ldn] == 0)
		return (0);
	return (IBM_DS(host_index).ldn_access[ldn] * 100) / IBM_DS(host_index).total_accesses;
}

/* calculate total amount of r/w-accesses */
static int ldn_access_total_read_write(int host_index)
{
	int a;
	int i;

	a = 0;
	for (i = 0; i <= MAX_LOG_DEV; i++)
		a += IBM_DS(host_index).ldn_read_access[i] + IBM_DS(host_index).ldn_write_access[i];
	return (a);
}

static int ldn_access_total_inquiry(int host_index)
{
	int a;
	int i;

	a = 0;
	for (i = 0; i <= MAX_LOG_DEV; i++)
		a += IBM_DS(host_index).ldn_inquiry_access[i];
	return (a);
}

static int ldn_access_total_modeselect(int host_index)
{
	int a;
	int i;

	a = 0;
	for (i = 0; i <= MAX_LOG_DEV; i++)
		a += IBM_DS(host_index).ldn_modeselect_access[i];
	return (a);
}

/* routine to display info in the proc-fs-structure (a deluxe feature) */
static int ibmmca_proc_info(struct Scsi_Host *shpnt, char *buffer, char **start, off_t offset, int length, int inout)
{
	int len = 0;
	int i, id, lun, host_index;
	unsigned long flags;
	int max_pun;

	for (i = 0; hosts[i] && hosts[i] != shpnt; i++);
	
	spin_lock_irqsave(hosts[i]->host_lock, flags);	/* Check it */
	host_index = i;
	if (!shpnt) {
		len += sprintf(buffer + len, "\nIBM MCA SCSI: Can't find adapter for host number %d\n",
				shpnt->host_no);
		return len;
	}
	max_pun = subsystem_maxid(host_index);

	len += sprintf(buffer + len, "\n             IBM-SCSI-Subsystem-Linux-Driver, Version %s\n\n\n", IBMMCA_SCSI_DRIVER_VERSION);
	len += sprintf(buffer + len, " SCSI Access-Statistics:\n");
	len += sprintf(buffer + len, "               Device Scanning Order....: %s\n", (ibm_ansi_order) ? "IBM/ANSI" : "New Industry Standard");
#ifdef CONFIG_SCSI_MULTI_LUN
	len += sprintf(buffer + len, "               Multiple LUN probing.....: Yes\n");
#else
	len += sprintf(buffer + len, "               Multiple LUN probing.....: No\n");
#endif
	len += sprintf(buffer + len, "               This Hostnumber..........: %d\n", shpnt->host_no);
	len += sprintf(buffer + len, "               Base I/O-Port............: 0x%x\n", (unsigned int) (IM_CMD_REG(host_index)));
	len += sprintf(buffer + len, "               (Shared) IRQ.............: %d\n", IM_IRQ);
	len += sprintf(buffer + len, "               Total Interrupts.........: %d\n", IBM_DS(host_index).total_interrupts);
	len += sprintf(buffer + len, "               Total SCSI Accesses......: %d\n", IBM_DS(host_index).total_accesses);
	len += sprintf(buffer + len, "               Total short SCBs.........: %d\n", IBM_DS(host_index).scbs);
	len += sprintf(buffer + len, "               Total long SCBs..........: %d\n", IBM_DS(host_index).long_scbs);
	len += sprintf(buffer + len, "                 Total SCSI READ/WRITE..: %d\n", ldn_access_total_read_write(host_index));
	len += sprintf(buffer + len, "                 Total SCSI Inquiries...: %d\n", ldn_access_total_inquiry(host_index));
	len += sprintf(buffer + len, "                 Total SCSI Modeselects.: %d\n", ldn_access_total_modeselect(host_index));
	len += sprintf(buffer + len, "                 Total SCSI other cmds..: %d\n", IBM_DS(host_index).total_accesses - ldn_access_total_read_write(host_index)
		       - ldn_access_total_modeselect(host_index)
		       - ldn_access_total_inquiry(host_index));
	len += sprintf(buffer + len, "               Total SCSI command fails.: %d\n\n", IBM_DS(host_index).total_errors);
	len += sprintf(buffer + len, " Logical-Device-Number (LDN) Access-Statistics:\n");
	len += sprintf(buffer + len, "         LDN | Accesses [%%] |   READ    |   WRITE   | ASSIGNMENTS\n");
	len += sprintf(buffer + len, "        -----|--------------|-----------|-----------|--------------\n");
	for (i = 0; i <= MAX_LOG_DEV; i++)
		len += sprintf(buffer + len, "         %2X  |    %3d       |  %8d |  %8d | %8d\n", i, ldn_access_load(host_index, i), IBM_DS(host_index).ldn_read_access[i], IBM_DS(host_index).ldn_write_access[i], IBM_DS(host_index).ldn_assignments[i]);
	len += sprintf(buffer + len, "        -----------------------------------------------------------\n\n");
	len += sprintf(buffer + len, " Dynamical-LDN-Assignment-Statistics:\n");
	len += sprintf(buffer + len, "               Number of physical SCSI-devices..: %d (+ Adapter)\n", IBM_DS(host_index).total_scsi_devices);
	len += sprintf(buffer + len, "               Dynamical Assignment necessary...: %s\n", IBM_DS(host_index).dyn_flag ? "Yes" : "No ");
	len += sprintf(buffer + len, "               Next LDN to be assigned..........: 0x%x\n", next_ldn(host_index));
	len += sprintf(buffer + len, "               Dynamical assignments done yet...: %d\n", IBM_DS(host_index).dynamical_assignments);
	len += sprintf(buffer + len, "\n Current SCSI-Device-Mapping:\n");
	len += sprintf(buffer + len, "        Physical SCSI-Device Map               Logical SCSI-Device Map\n");
	len += sprintf(buffer + len, "    ID\\LUN  0  1  2  3  4  5  6  7       ID\\LUN  0  1  2  3  4  5  6  7\n");
	for (id = 0; id < max_pun; id++) {
		len += sprintf(buffer + len, "    %2d     ", id);
		for (lun = 0; lun < 8; lun++)
			len += sprintf(buffer + len, "%2s ", ti_p(get_scsi(host_index)[id][lun]));
		len += sprintf(buffer + len, "      %2d     ", id);
		for (lun = 0; lun < 8; lun++)
			len += sprintf(buffer + len, "%2s ", ti_l(get_ldn(host_index)[id][lun]));
		len += sprintf(buffer + len, "\n");
	}

	len += sprintf(buffer + len, "(A = IBM-Subsystem, D = Harddisk, T = Tapedrive, P = Processor, W = WORM,\n");
	len += sprintf(buffer + len, " R = CD-ROM, S = Scanner, M = MO-Drive, C = Medium-Changer, + = unprovided LUN,\n");
	len += sprintf(buffer + len, " - = nothing found, nothing assigned or unprobed LUN)\n\n");

	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	spin_unlock_irqrestore(shpnt->host_lock, flags);
	return len;
}

static int option_setup(char *str)
{
	int ints[IM_MAX_HOSTS];
	char *cur = str;
	int i = 1;

	while (cur && isdigit(*cur) && i <= IM_MAX_HOSTS) {
		ints[i++] = simple_strtoul(cur, NULL, 0);
		if ((cur = strchr(cur, ',')) != NULL)
			cur++;
	}
	ints[0] = i - 1;
	internal_ibmmca_scsi_setup(cur, ints);
	return 0;
}

__setup("ibmmcascsi=", option_setup);

static Scsi_Host_Template driver_template = {
          .proc_name      = "ibmmca",
	  .proc_info	  = ibmmca_proc_info,
          .name           = "IBM SCSI-Subsystem",
          .detect         = ibmmca_detect,
          .release        = ibmmca_release,
          .queuecommand   = ibmmca_queuecommand,
	  .eh_abort_handler = ibmmca_abort,
	  .eh_host_reset_handler = ibmmca_host_reset,
          .bios_param     = ibmmca_biosparam,
          .can_queue      = 16,
          .this_id        = 7,
          .sg_tablesize   = 16,
          .cmd_per_lun    = 1,
          .use_clustering = ENABLE_CLUSTERING,
};
#include "scsi_module.c"
