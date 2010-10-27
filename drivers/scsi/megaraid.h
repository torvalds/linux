#ifndef __MEGARAID_H__
#define __MEGARAID_H__

#include <linux/spinlock.h>
#include <linux/mutex.h>

#define MEGARAID_VERSION	\
	"v2.00.4 (Release Date: Thu Feb 9 08:51:30 EST 2006)\n"

/*
 * Driver features - change the values to enable or disable features in the
 * driver.
 */

/*
 * Comand coalescing - This feature allows the driver to be able to combine
 * two or more commands and issue as one command in order to boost I/O
 * performance. Useful if the nature of the I/O is sequential. It is not very
 * useful for random natured I/Os.
 */
#define MEGA_HAVE_COALESCING	0

/*
 * Clustering support - Set this flag if you are planning to use the
 * clustering services provided by the megaraid controllers and planning to
 * setup a cluster
 */
#define MEGA_HAVE_CLUSTERING	1

/*
 * Driver statistics - Set this flag if you are interested in statics about
 * number of I/O completed on each logical drive and how many interrupts
 * generated. If enabled, this information is available through /proc
 * interface and through the private ioctl. Setting this flag has a
 * performance penalty.
 */
#define MEGA_HAVE_STATS		0

/*
 * Enhanced /proc interface - This feature will allow you to have a more
 * detailed /proc interface for megaraid driver. E.g., a real time update of
 * the status of the logical drives, battery status, physical drives etc.
 */
#define MEGA_HAVE_ENH_PROC	1

#define MAX_DEV_TYPE	32

#ifndef PCI_VENDOR_ID_LSI_LOGIC
#define PCI_VENDOR_ID_LSI_LOGIC		0x1000
#endif

#ifndef PCI_VENDOR_ID_AMI
#define PCI_VENDOR_ID_AMI		0x101E
#endif

#ifndef PCI_VENDOR_ID_DELL
#define PCI_VENDOR_ID_DELL		0x1028
#endif

#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL		0x8086
#endif

#ifndef PCI_DEVICE_ID_AMI_MEGARAID
#define PCI_DEVICE_ID_AMI_MEGARAID	0x9010
#endif

#ifndef PCI_DEVICE_ID_AMI_MEGARAID2
#define PCI_DEVICE_ID_AMI_MEGARAID2	0x9060
#endif

#ifndef PCI_DEVICE_ID_AMI_MEGARAID3
#define PCI_DEVICE_ID_AMI_MEGARAID3	0x1960
#endif

#define PCI_DEVICE_ID_DISCOVERY		0x000E
#define PCI_DEVICE_ID_PERC4_DI		0x000F
#define PCI_DEVICE_ID_PERC4_QC_VERDE	0x0407

/* Sub-System Vendor IDs */
#define	AMI_SUBSYS_VID			0x101E
#define DELL_SUBSYS_VID			0x1028
#define	HP_SUBSYS_VID			0x103C
#define LSI_SUBSYS_VID			0x1000
#define INTEL_SUBSYS_VID		0x8086

#define HBA_SIGNATURE	      		0x3344
#define HBA_SIGNATURE_471	  	0xCCCC
#define HBA_SIGNATURE_64BIT		0x0299

#define MBOX_BUSY_WAIT			10	/* wait for up to 10 usec for
						   mailbox to be free */
#define DEFAULT_INITIATOR_ID	7

#define MAX_SGLIST		64	/* max supported in f/w */
#define MIN_SGLIST		26	/* guaranteed to support these many */
#define MAX_COMMANDS		126
#define CMDID_INT_CMDS		MAX_COMMANDS+1	/* make sure CMDID_INT_CMDS
					 	is less than max commands
						supported by any f/w */

#define MAX_CDB_LEN	     	10
#define MAX_EXT_CDB_LEN		16	/* we support cdb length up to 16 */

#define DEF_CMD_PER_LUN		63
#define MAX_CMD_PER_LUN		MAX_COMMANDS
#define MAX_FIRMWARE_STATUS	46
#define MAX_XFER_PER_CMD	(64*1024)
#define MAX_SECTORS_PER_IO	128

#define MAX_LOGICAL_DRIVES_40LD		40
#define FC_MAX_PHYSICAL_DEVICES		256
#define MAX_LOGICAL_DRIVES_8LD		8
#define MAX_CHANNELS			5
#define MAX_TARGET			15
#define MAX_PHYSICAL_DRIVES		MAX_CHANNELS*MAX_TARGET
#define MAX_ROW_SIZE_40LD		32
#define MAX_ROW_SIZE_8LD		8
#define MAX_SPAN_DEPTH			8

#define NVIRT_CHAN		4	/* # of virtual channels to represent
					   up to 60 logical drives */
struct mbox_out {
	/* 0x0 */ u8 cmd;
	/* 0x1 */ u8 cmdid;
	/* 0x2 */ u16 numsectors;
	/* 0x4 */ u32 lba;
	/* 0x8 */ u32 xferaddr;
	/* 0xC */ u8 logdrv;
	/* 0xD */ u8 numsgelements;
	/* 0xE */ u8 resvd;
} __attribute__ ((packed));

struct mbox_in {
	/* 0xF */ volatile u8 busy;
	/* 0x10 */ volatile u8 numstatus;
	/* 0x11 */ volatile u8 status;
	/* 0x12 */ volatile u8 completed[MAX_FIRMWARE_STATUS];
	volatile u8 poll;
	volatile u8 ack;
} __attribute__ ((packed));

typedef struct {
	struct mbox_out	m_out;
	struct mbox_in	m_in;
} __attribute__ ((packed)) mbox_t;

typedef struct {
	u32 xfer_segment_lo;
	u32 xfer_segment_hi;
	mbox_t mbox;
} __attribute__ ((packed)) mbox64_t;


/*
 * Passthru definitions
 */
#define MAX_REQ_SENSE_LEN       0x20

typedef struct {
	u8 timeout:3;		/* 0=6sec/1=60sec/2=10min/3=3hrs */
	u8 ars:1;
	u8 reserved:3;
	u8 islogical:1;
	u8 logdrv;		/* if islogical == 1 */
	u8 channel;		/* if islogical == 0 */
	u8 target;		/* if islogical == 0 */
	u8 queuetag;		/* unused */
	u8 queueaction;		/* unused */
	u8 cdb[MAX_CDB_LEN];
	u8 cdblen;
	u8 reqsenselen;
	u8 reqsensearea[MAX_REQ_SENSE_LEN];
	u8 numsgelements;
	u8 scsistatus;
	u32 dataxferaddr;
	u32 dataxferlen;
} __attribute__ ((packed)) mega_passthru;


/*
 * Extended passthru: support CDB > 10 bytes
 */
typedef struct {
	u8 timeout:3;		/* 0=6sec/1=60sec/2=10min/3=3hrs */
	u8 ars:1;
	u8 rsvd1:1;
	u8 cd_rom:1;
	u8 rsvd2:1;
	u8 islogical:1;
	u8 logdrv;		/* if islogical == 1 */
	u8 channel;		/* if islogical == 0 */
	u8 target;		/* if islogical == 0 */
	u8 queuetag;		/* unused */
	u8 queueaction;		/* unused */
	u8 cdblen;
	u8 rsvd3;
	u8 cdb[MAX_EXT_CDB_LEN];
	u8 numsgelements;
	u8 status;
	u8 reqsenselen;
	u8 reqsensearea[MAX_REQ_SENSE_LEN];
	u8 rsvd4;
	u32 dataxferaddr;
	u32 dataxferlen;
} __attribute__ ((packed)) mega_ext_passthru;

typedef struct {
	u64 address;
	u32 length;
} __attribute__ ((packed)) mega_sgl64;

typedef struct {
	u32 address;
	u32 length;
} __attribute__ ((packed)) mega_sglist;


/* Queued command data */
typedef struct {
	int	idx;
	u32	state;
	struct list_head	list;
	u8	raw_mbox[66];
	u32	dma_type;
	u32	dma_direction;

	Scsi_Cmnd	*cmd;
	dma_addr_t	dma_h_bulkdata;
	dma_addr_t	dma_h_sgdata;

	mega_sglist	*sgl;
	mega_sgl64	*sgl64;
	dma_addr_t	sgl_dma_addr;

	mega_passthru		*pthru;
	dma_addr_t		pthru_dma_addr;
	mega_ext_passthru	*epthru;
	dma_addr_t		epthru_dma_addr;
} scb_t;

/*
 * Flags to follow the scb as it transitions between various stages
 */
#define SCB_FREE	0x0000	/* on the free list */
#define SCB_ACTIVE	0x0001	/* off the free list */
#define SCB_PENDQ	0x0002	/* on the pending queue */
#define SCB_ISSUED	0x0004	/* issued - owner f/w */
#define SCB_ABORT	0x0008	/* Got an abort for this one */
#define SCB_RESET	0x0010	/* Got a reset for this one */

/*
 * Utilities declare this strcture size as 1024 bytes. So more fields can
 * be added in future.
 */
typedef struct {
	u32	data_size; /* current size in bytes (not including resvd) */

	u32	config_signature;
		/* Current value is 0x00282008
		 * 0x28=MAX_LOGICAL_DRIVES,
		 * 0x20=Number of stripes and
		 * 0x08=Number of spans */

	u8	fw_version[16];		/* printable ASCI string */
	u8	bios_version[16];	/* printable ASCI string */
	u8	product_name[80];	/* printable ASCI string */

	u8	max_commands;		/* Max. concurrent commands supported */
	u8	nchannels;		/* Number of SCSI Channels detected */
	u8	fc_loop_present;	/* Number of Fibre Loops detected */
	u8	mem_type;		/* EDO, FPM, SDRAM etc */

	u32	signature;
	u16	dram_size;		/* In terms of MB */
	u16	subsysid;

	u16	subsysvid;
	u8	notify_counters;
	u8	pad1k[889];		/* 135 + 889 resvd = 1024 total size */
} __attribute__ ((packed)) mega_product_info;

struct notify {
	u32 global_counter;	/* Any change increments this counter */

	u8 param_counter;	/* Indicates any params changed  */
	u8 param_id;		/* Param modified - defined below */
	u16 param_val;		/* New val of last param modified */

	u8 write_config_counter;	/* write config occurred */
	u8 write_config_rsvd[3];

	u8 ldrv_op_counter;	/* Indicates ldrv op started/completed */
	u8 ldrv_opid;		/* ldrv num */
	u8 ldrv_opcmd;		/* ldrv operation - defined below */
	u8 ldrv_opstatus;	/* status of the operation */

	u8 ldrv_state_counter;	/* Indicates change of ldrv state */
	u8 ldrv_state_id;		/* ldrv num */
	u8 ldrv_state_new;	/* New state */
	u8 ldrv_state_old;	/* old state */

	u8 pdrv_state_counter;	/* Indicates change of ldrv state */
	u8 pdrv_state_id;		/* pdrv id */
	u8 pdrv_state_new;	/* New state */
	u8 pdrv_state_old;	/* old state */

	u8 pdrv_fmt_counter;	/* Indicates pdrv format started/over */
	u8 pdrv_fmt_id;		/* pdrv id */
	u8 pdrv_fmt_val;		/* format started/over */
	u8 pdrv_fmt_rsvd;

	u8 targ_xfer_counter;	/* Indicates SCSI-2 Xfer rate change */
	u8 targ_xfer_id;	/* pdrv Id  */
	u8 targ_xfer_val;		/* new Xfer params of last pdrv */
	u8 targ_xfer_rsvd;

	u8 fcloop_id_chg_counter;	/* Indicates loopid changed */
	u8 fcloopid_pdrvid;		/* pdrv id */
	u8 fcloop_id0;			/* loopid on fc loop 0 */
	u8 fcloop_id1;			/* loopid on fc loop 1 */

	u8 fcloop_state_counter;	/* Indicates loop state changed */
	u8 fcloop_state0;		/* state of fc loop 0 */
	u8 fcloop_state1;		/* state of fc loop 1 */
	u8 fcloop_state_rsvd;
} __attribute__ ((packed));

#define MAX_NOTIFY_SIZE     0x80
#define CUR_NOTIFY_SIZE     sizeof(struct notify)

typedef struct {
	u32	data_size; /* current size in bytes (not including resvd) */

	struct notify notify;

	u8	notify_rsvd[MAX_NOTIFY_SIZE - CUR_NOTIFY_SIZE];

	u8	rebuild_rate;		/* Rebuild rate (0% - 100%) */
	u8	cache_flush_interval;	/* In terms of Seconds */
	u8	sense_alert;
	u8	drive_insert_count;	/* drive insertion count */

	u8	battery_status;
	u8	num_ldrv;		/* No. of Log Drives configured */
	u8	recon_state[MAX_LOGICAL_DRIVES_40LD / 8];	/* State of
							   reconstruct */
	u16	ldrv_op_status[MAX_LOGICAL_DRIVES_40LD / 8]; /* logdrv
								 Status */

	u32	ldrv_size[MAX_LOGICAL_DRIVES_40LD];/* Size of each log drv */
	u8	ldrv_prop[MAX_LOGICAL_DRIVES_40LD];
	u8	ldrv_state[MAX_LOGICAL_DRIVES_40LD];/* State of log drives */
	u8	pdrv_state[FC_MAX_PHYSICAL_DEVICES];/* State of phys drvs. */
	u16	pdrv_format[FC_MAX_PHYSICAL_DEVICES / 16];

	u8	targ_xfer[80];	/* phys device transfer rate */
	u8	pad1k[263];	/* 761 + 263reserved = 1024 bytes total size */
} __attribute__ ((packed)) mega_inquiry3;


/* Structures */
typedef struct {
	u8	max_commands;	/* Max concurrent commands supported */
	u8	rebuild_rate;	/* Rebuild rate - 0% thru 100% */
	u8	max_targ_per_chan;	/* Max targ per channel */
	u8	nchannels;	/* Number of channels on HBA */
	u8	fw_version[4];	/* Firmware version */
	u16	age_of_flash;	/* Number of times FW has been flashed */
	u8	chip_set_value;	/* Contents of 0xC0000832 */
	u8	dram_size;	/* In MB */
	u8	cache_flush_interval;	/* in seconds */
	u8	bios_version[4];
	u8	board_type;
	u8	sense_alert;
	u8	write_config_count;	/* Increase with every configuration
					   change */
	u8	drive_inserted_count;	/* Increase with every drive inserted
					 */
	u8	inserted_drive;	/* Channel:Id of inserted drive */
	u8	battery_status;	/*
				 * BIT 0: battery module missing
				 * BIT 1: VBAD
				 * BIT 2: temperature high
				 * BIT 3: battery pack missing
				 * BIT 4,5:
				 *   00 - charge complete
				 *   01 - fast charge in progress
				 *   10 - fast charge fail
				 *   11 - undefined
				 * Bit 6: counter > 1000
				 * Bit 7: Undefined
				 */
	u8	dec_fault_bus_info;
} __attribute__ ((packed)) mega_adp_info;


typedef struct {
	u8	num_ldrv;	/* Number of logical drives configured */
	u8	rsvd[3];
	u32	ldrv_size[MAX_LOGICAL_DRIVES_8LD];
	u8	ldrv_prop[MAX_LOGICAL_DRIVES_8LD];
	u8	ldrv_state[MAX_LOGICAL_DRIVES_8LD];
} __attribute__ ((packed)) mega_ldrv_info;

typedef struct {
	u8	pdrv_state[MAX_PHYSICAL_DRIVES];
	u8	rsvd;
} __attribute__ ((packed)) mega_pdrv_info;

/* RAID inquiry: Mailbox command 0x05*/
typedef struct {
	mega_adp_info	adapter_info;
	mega_ldrv_info	logdrv_info;
	mega_pdrv_info	pdrv_info;
} __attribute__ ((packed)) mraid_inquiry;


/* RAID extended inquiry: Mailbox command 0x04*/
typedef struct {
	mraid_inquiry	raid_inq;
	u16	phys_drv_format[MAX_CHANNELS];
	u8	stack_attn;
	u8	modem_status;
	u8	rsvd[2];
} __attribute__ ((packed)) mraid_ext_inquiry;


typedef struct {
	u8	channel;
	u8	target;
}__attribute__ ((packed)) adp_device;

typedef struct {
	u32		start_blk;	/* starting block */
	u32		num_blks;	/* # of blocks */
	adp_device	device[MAX_ROW_SIZE_40LD];
}__attribute__ ((packed)) adp_span_40ld;

typedef struct {
	u32		start_blk;	/* starting block */
	u32		num_blks;	/* # of blocks */
	adp_device	device[MAX_ROW_SIZE_8LD];
}__attribute__ ((packed)) adp_span_8ld;

typedef struct {
	u8	span_depth;	/* Total # of spans */
	u8	level;		/* RAID level */
	u8	read_ahead;	/* read ahead, no read ahead, adaptive read
				   ahead */
	u8	stripe_sz;	/* Encoded stripe size */
	u8	status;		/* Status of the logical drive */
	u8	write_mode;	/* write mode, write_through/write_back */
	u8	direct_io;	/* direct io or through cache */
	u8	row_size;	/* Number of stripes in a row */
} __attribute__ ((packed)) logdrv_param;

typedef struct {
	logdrv_param	lparam;
	adp_span_40ld	span[MAX_SPAN_DEPTH];
}__attribute__ ((packed)) logdrv_40ld;

typedef struct {
	logdrv_param	lparam;
	adp_span_8ld	span[MAX_SPAN_DEPTH];
}__attribute__ ((packed)) logdrv_8ld;

typedef struct {
	u8	type;		/* Type of the device */
	u8	cur_status;	/* current status of the device */
	u8	tag_depth;	/* Level of tagging */
	u8	sync_neg;	/* sync negotiation - ENABLE or DISABLE */
	u32	size;		/* configurable size in terms of 512 byte
				   blocks */
}__attribute__ ((packed)) phys_drv;

typedef struct {
	u8		nlog_drives;		/* number of logical drives */
	u8		resvd[3];
	logdrv_40ld	ldrv[MAX_LOGICAL_DRIVES_40LD];
	phys_drv	pdrv[MAX_PHYSICAL_DRIVES];
}__attribute__ ((packed)) disk_array_40ld;

typedef struct {
	u8		nlog_drives;	/* number of logical drives */
	u8		resvd[3];
	logdrv_8ld	ldrv[MAX_LOGICAL_DRIVES_8LD];
	phys_drv	pdrv[MAX_PHYSICAL_DRIVES];
}__attribute__ ((packed)) disk_array_8ld;


/*
 * User ioctl structure.
 * This structure will be used for Traditional Method ioctl interface
 * commands (0x80),Alternate Buffer Method (0x81) ioctl commands and the
 * Driver ioctls.
 * The Driver ioctl interface handles the commands at the driver level,
 * without being sent to the card.
 */
/* system call imposed limit. Change accordingly */
#define IOCTL_MAX_DATALEN       4096

struct uioctl_t {
	u32 inlen;
	u32 outlen;
	union {
		u8 fca[16];
		struct {
			u8 opcode;
			u8 subopcode;
			u16 adapno;
#if BITS_PER_LONG == 32
			u8 *buffer;
			u8 pad[4];
#endif
#if BITS_PER_LONG == 64
			u8 *buffer;
#endif
			u32 length;
		} __attribute__ ((packed)) fcs;
	} __attribute__ ((packed)) ui;
	u8 mbox[18];		/* 16 bytes + 2 status bytes */
	mega_passthru pthru;
#if BITS_PER_LONG == 32
	char __user *data;		/* buffer <= 4096 for 0x80 commands */
	char pad[4];
#endif
#if BITS_PER_LONG == 64
	char __user *data;
#endif
} __attribute__ ((packed));

/*
 * struct mcontroller is used to pass information about the controllers in the
 * system. Its upto the application how to use the information. We are passing
 * as much info about the cards as possible and useful. Before issuing the
 * call to find information about the cards, the applicaiton needs to issue a
 * ioctl first to find out the number of controllers in the system.
 */
#define MAX_CONTROLLERS 32

struct mcontroller {
	u64 base;
	u8 irq;
	u8 numldrv;
	u8 pcibus;
	u16 pcidev;
	u8 pcifun;
	u16 pciid;
	u16 pcivendor;
	u8 pcislot;
	u32 uid;
};

/*
 * mailbox structure used for internal commands
 */
typedef struct {
	u8	cmd;
	u8	cmdid;
	u8	opcode;
	u8	subopcode;
	u32	lba;
	u32	xferaddr;
	u8	logdrv;
	u8	rsvd[3];
	u8	numstatus;
	u8	status;
} __attribute__ ((packed)) megacmd_t;

/*
 * Defines for Driver IOCTL interface
 */
#define MEGAIOC_MAGIC  	'm'

#define MEGAIOC_QNADAP		'm'	/* Query # of adapters */
#define MEGAIOC_QDRVRVER	'e'	/* Query driver version */
#define MEGAIOC_QADAPINFO   	'g'	/* Query adapter information */
#define MKADAP(adapno)	  	(MEGAIOC_MAGIC << 8 | (adapno) )
#define GETADAP(mkadap)	 	( (mkadap) ^ MEGAIOC_MAGIC << 8 )

/*
 * Definition for the new ioctl interface (NIT)
 */

/*
 * Vendor specific Group-7 commands
 */
#define VENDOR_SPECIFIC_COMMANDS	0xE0
#define MEGA_INTERNAL_CMD		VENDOR_SPECIFIC_COMMANDS + 0x01

/*
 * The ioctl command. No other command shall be used for this interface
 */
#define USCSICMD	VENDOR_SPECIFIC_COMMANDS

/*
 * Data direction flags
 */
#define UIOC_RD		0x00001
#define UIOC_WR		0x00002

/*
 * ioctl opcodes
 */
#define MBOX_CMD	0x00000	/* DCMD or passthru command */
#define GET_DRIVER_VER	0x10000	/* Get driver version */
#define GET_N_ADAP	0x20000	/* Get number of adapters */
#define GET_ADAP_INFO	0x30000	/* Get information about a adapter */
#define GET_CAP		0x40000	/* Get ioctl capabilities */
#define GET_STATS	0x50000	/* Get statistics, including error info */


/*
 * The ioctl structure.
 * MBOX macro converts a nitioctl_t structure to megacmd_t pointer and
 * MBOX_P macro converts a nitioctl_t pointer to megacmd_t pointer.
 */
typedef struct {
	char		signature[8];	/* Must contain "MEGANIT" */
	u32		opcode;		/* opcode for the command */
	u32		adapno;		/* adapter number */
	union {
		u8	__raw_mbox[18];
		void __user *__uaddr; /* xferaddr for non-mbox cmds */
	}__ua;

#define uioc_rmbox	__ua.__raw_mbox
#define MBOX(uioc)	((megacmd_t *)&((uioc).__ua.__raw_mbox[0]))
#define MBOX_P(uioc)	((megacmd_t __user *)&((uioc)->__ua.__raw_mbox[0]))
#define uioc_uaddr	__ua.__uaddr

	u32		xferlen;	/* xferlen for DCMD and non-mbox
					   commands */
	u32		flags;		/* data direction flags */
}nitioctl_t;


/*
 * I/O statistics for some applications like SNMP agent. The caller must
 * provide the number of logical drives for which status should be reported.
 */
typedef struct {
	int	num_ldrv;	/* Number for logical drives for which the
				   status should be reported. */
	u32	nreads[MAX_LOGICAL_DRIVES_40LD];	/* number of reads for
							each logical drive */
	u32	nreadblocks[MAX_LOGICAL_DRIVES_40LD];	/* number of blocks
							read for each logical
							drive */
	u32	nwrites[MAX_LOGICAL_DRIVES_40LD];	/* number of writes
							for each logical
							drive */
	u32	nwriteblocks[MAX_LOGICAL_DRIVES_40LD];	/* number of blocks
							writes for each
							logical drive */
	u32	rd_errors[MAX_LOGICAL_DRIVES_40LD];	/* number of read
							   errors for each
							   logical drive */
	u32	wr_errors[MAX_LOGICAL_DRIVES_40LD];	/* number of write
							   errors for each
							   logical drive */
}megastat_t;


struct private_bios_data {
	u8	geometry:4;	/*
				 * bits 0-3 - BIOS geometry
				 * 0x0001 - 1GB
				 * 0x0010 - 2GB
				 * 0x1000 - 8GB
				 * Others values are invalid
							 */
	u8	unused:4;	/* bits 4-7 are unused */
	u8	boot_drv;	/*
				 * logical drive set as boot drive
				 * 0..7 - for 8LD cards
				 * 0..39 - for 40LD cards
				 */
	u8	rsvd[12];
	u16	cksum;	/* 0-(sum of first 13 bytes of this structure) */
} __attribute__ ((packed));




/*
 * Mailbox and firmware commands and subopcodes used in this driver.
 */

#define MEGA_MBOXCMD_LREAD	0x01
#define MEGA_MBOXCMD_LWRITE	0x02
#define MEGA_MBOXCMD_PASSTHRU	0x03
#define MEGA_MBOXCMD_ADPEXTINQ	0x04
#define MEGA_MBOXCMD_ADAPTERINQ	0x05
#define MEGA_MBOXCMD_LREAD64	0xA7
#define MEGA_MBOXCMD_LWRITE64	0xA8
#define MEGA_MBOXCMD_PASSTHRU64	0xC3
#define MEGA_MBOXCMD_EXTPTHRU	0xE3

#define MAIN_MISC_OPCODE	0xA4	/* f/w misc opcode */
#define GET_MAX_SG_SUPPORT	0x01	/* get max sg len supported by f/w */

#define FC_NEW_CONFIG		0xA1
#define NC_SUBOP_PRODUCT_INFO	0x0E
#define NC_SUBOP_ENQUIRY3	0x0F
#define ENQ3_GET_SOLICITED_FULL	0x02
#define OP_DCMD_READ_CONFIG	0x04
#define NEW_READ_CONFIG_8LD	0x67
#define READ_CONFIG_8LD		0x07
#define FLUSH_ADAPTER		0x0A
#define FLUSH_SYSTEM		0xFE

/*
 * Command for random deletion of logical drives
 */
#define	FC_DEL_LOGDRV		0xA4	/* f/w command */
#define	OP_SUP_DEL_LOGDRV	0x2A	/* is feature supported */
#define OP_GET_LDID_MAP		0x18	/* get ldid and logdrv number map */
#define OP_DEL_LOGDRV		0x1C	/* delete logical drive */

/*
 * BIOS commands
 */
#define IS_BIOS_ENABLED		0x62
#define GET_BIOS		0x01
#define CHNL_CLASS		0xA9
#define GET_CHNL_CLASS		0x00
#define SET_CHNL_CLASS		0x01
#define CH_RAID			0x01
#define CH_SCSI			0x00
#define BIOS_PVT_DATA		0x40
#define GET_BIOS_PVT_DATA	0x00


/*
 * Commands to support clustering
 */
#define MEGA_GET_TARGET_ID	0x7D
#define MEGA_CLUSTER_OP		0x70
#define MEGA_GET_CLUSTER_MODE	0x02
#define MEGA_CLUSTER_CMD	0x6E
#define MEGA_RESERVE_LD		0x01
#define MEGA_RELEASE_LD		0x02
#define MEGA_RESET_RESERVATIONS	0x03
#define MEGA_RESERVATION_STATUS	0x04
#define MEGA_RESERVE_PD		0x05
#define MEGA_RELEASE_PD		0x06


/*
 * Module battery status
 */
#define MEGA_BATT_MODULE_MISSING	0x01
#define MEGA_BATT_LOW_VOLTAGE		0x02
#define MEGA_BATT_TEMP_HIGH		0x04
#define MEGA_BATT_PACK_MISSING		0x08
#define MEGA_BATT_CHARGE_MASK		0x30
#define MEGA_BATT_CHARGE_DONE		0x00
#define MEGA_BATT_CHARGE_INPROG		0x10
#define MEGA_BATT_CHARGE_FAIL		0x20
#define MEGA_BATT_CYCLES_EXCEEDED	0x40

/*
 * Physical drive states.
 */
#define PDRV_UNCNF	0
#define PDRV_ONLINE	3
#define PDRV_FAILED	4
#define PDRV_RBLD	5
#define PDRV_HOTSPARE	6


/*
 * Raid logical drive states.
 */
#define RDRV_OFFLINE	0
#define RDRV_DEGRADED	1
#define RDRV_OPTIMAL	2
#define RDRV_DELETED	3

/*
 * Read, write and cache policies
 */
#define NO_READ_AHEAD		0
#define READ_AHEAD		1
#define ADAP_READ_AHEAD		2
#define WRMODE_WRITE_THRU	0
#define WRMODE_WRITE_BACK	1
#define CACHED_IO		0
#define DIRECT_IO		1


#define SCSI_LIST(scp) ((struct list_head *)(&(scp)->SCp))

/*
 * Each controller's soft state
 */
typedef struct {
	int	this_id;	/* our id, may set to different than 7 if
				   clustering is available */
	u32	flag;

	unsigned long		base;
	void __iomem		*mmio_base;

	/* mbox64 with mbox not aligned on 16-byte boundry */
	mbox64_t	*una_mbox64;
	dma_addr_t	una_mbox64_dma;

	volatile mbox64_t	*mbox64;/* ptr to 64-bit mailbox */
	volatile mbox_t		*mbox;	/* ptr to standard mailbox */
	dma_addr_t		mbox_dma;

	struct pci_dev	*dev;

	struct list_head	free_list;
	struct list_head	pending_list;
	struct list_head	completed_list;

	struct Scsi_Host	*host;

#define MEGA_BUFFER_SIZE (2*1024)
	u8		*mega_buffer;
	dma_addr_t	buf_dma_handle;

	mega_product_info	product_info;

	u8		max_cmds;
	scb_t		*scb_list;

	atomic_t	pend_cmds;	/* maintain a counter for pending
					   commands in firmware */

#if MEGA_HAVE_STATS
	u32	nreads[MAX_LOGICAL_DRIVES_40LD];
	u32	nreadblocks[MAX_LOGICAL_DRIVES_40LD];
	u32	nwrites[MAX_LOGICAL_DRIVES_40LD];
	u32	nwriteblocks[MAX_LOGICAL_DRIVES_40LD];
	u32	rd_errors[MAX_LOGICAL_DRIVES_40LD];
	u32	wr_errors[MAX_LOGICAL_DRIVES_40LD];
#endif

	/* Host adapter parameters */
	u8	numldrv;
	u8	fw_version[7];
	u8	bios_version[7];

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry	*controller_proc_dir_entry;
	struct proc_dir_entry	*proc_read;
	struct proc_dir_entry	*proc_stat;
	struct proc_dir_entry	*proc_mbox;

#if MEGA_HAVE_ENH_PROC
	struct proc_dir_entry	*proc_rr;
	struct proc_dir_entry	*proc_battery;
#define MAX_PROC_CHANNELS	4
	struct proc_dir_entry	*proc_pdrvstat[MAX_PROC_CHANNELS];
	struct proc_dir_entry	*proc_rdrvstat[MAX_PROC_CHANNELS];
#endif

#endif

	int	has_64bit_addr;		/* are we using 64-bit addressing */
	int	support_ext_cdb;
	int	boot_ldrv_enabled;
	int	boot_ldrv;
	int	boot_pdrv_enabled;	/* boot from physical drive */
	int	boot_pdrv_ch;		/* boot physical drive channel */
	int	boot_pdrv_tgt;		/* boot physical drive target */


	int	support_random_del;	/* Do we support random deletion of
					   logdrvs */
	int	read_ldidmap;	/* set after logical drive deltion. The
				   logical drive number must be read from the
				   map */
	atomic_t	quiescent;	/* a stage reached when delete logical
					   drive needs to be done. Stop
					   sending requests to the hba till
					   delete operation is completed */
	spinlock_t	lock;

	u8	logdrv_chan[MAX_CHANNELS+NVIRT_CHAN]; /* logical drive are on
							what channels. */
	int	mega_ch_class;

	u8	sglen;	/* f/w supported scatter-gather list length */

	unsigned char int_cdb[MAX_COMMAND_SIZE];
	scb_t			int_scb;
	struct mutex		int_mtx;	/* To synchronize the internal
						commands */
	struct completion	int_waitq;	/* wait queue for internal
						 cmds */

	int	has_cluster;	/* cluster support on this HBA */
}adapter_t;


struct mega_hbas {
	int is_bios_enabled;
	adapter_t *hostdata_addr;
};


/*
 * For state flag. Do not use LSB(8 bits) which are
 * reserved for storing info about channels.
 */
#define IN_ABORT	0x80000000L
#define IN_RESET	0x40000000L
#define BOARD_MEMMAP	0x20000000L
#define BOARD_IOMAP	0x10000000L
#define BOARD_40LD   	0x08000000L
#define BOARD_64BIT	0x04000000L

#define INTR_VALID			0x40

#define PCI_CONF_AMISIG			0xa0
#define PCI_CONF_AMISIG64		0xa4


#define MEGA_DMA_TYPE_NONE		0xFFFF
#define MEGA_BULK_DATA			0x0001
#define MEGA_SGLIST			0x0002

/*
 * Parameters for the io-mapped controllers
 */

/* I/O Port offsets */
#define CMD_PORT	 	0x00
#define ACK_PORT	 	0x00
#define TOGGLE_PORT		0x01
#define INTR_PORT	  	0x0a

#define MBOX_BUSY_PORT     	0x00
#define MBOX_PORT0	 	0x04
#define MBOX_PORT1	 	0x05
#define MBOX_PORT2	 	0x06
#define MBOX_PORT3	 	0x07
#define ENABLE_MBOX_REGION 	0x0B

/* I/O Port Values */
#define ISSUE_BYTE	 	0x10
#define ACK_BYTE	   	0x08
#define ENABLE_INTR_BYTE   	0xc0
#define DISABLE_INTR_BYTE  	0x00
#define VALID_INTR_BYTE    	0x40
#define MBOX_BUSY_BYTE     	0x10
#define ENABLE_MBOX_BYTE   	0x00


/* Setup some port macros here */
#define issue_command(adapter)	\
		outb_p(ISSUE_BYTE, (adapter)->base + CMD_PORT)

#define irq_state(adapter)	inb_p((adapter)->base + INTR_PORT)

#define set_irq_state(adapter, value)	\
		outb_p((value), (adapter)->base + INTR_PORT)

#define irq_ack(adapter)	\
		outb_p(ACK_BYTE, (adapter)->base + ACK_PORT)

#define irq_enable(adapter)	\
	outb_p(ENABLE_INTR_BYTE, (adapter)->base + TOGGLE_PORT)

#define irq_disable(adapter)	\
	outb_p(DISABLE_INTR_BYTE, (adapter)->base + TOGGLE_PORT)


/*
 * This is our SYSDEP area. All kernel specific detail should be placed here -
 * as much as possible
 */

/*
 * End of SYSDEP area
 */

const char *megaraid_info (struct Scsi_Host *);

static int mega_query_adapter(adapter_t *);
static int issue_scb(adapter_t *, scb_t *);
static int mega_setup_mailbox(adapter_t *);

static int megaraid_queue (Scsi_Cmnd *, void (*)(Scsi_Cmnd *));
static scb_t * mega_build_cmd(adapter_t *, Scsi_Cmnd *, int *);
static void __mega_runpendq(adapter_t *);
static int issue_scb_block(adapter_t *, u_char *);

static irqreturn_t megaraid_isr_memmapped(int, void *);
static irqreturn_t megaraid_isr_iomapped(int, void *);

static void mega_free_scb(adapter_t *, scb_t *);

static int megaraid_abort(Scsi_Cmnd *);
static int megaraid_reset(Scsi_Cmnd *);
static int megaraid_abort_and_reset(adapter_t *, Scsi_Cmnd *, int);
static int megaraid_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int []);

static int mega_build_sglist (adapter_t *adapter, scb_t *scb,
			      u32 *buffer, u32 *length);
static int __mega_busywait_mbox (adapter_t *);
static void mega_rundoneq (adapter_t *);
static void mega_cmd_done(adapter_t *, u8 [], int, int);
static inline void mega_free_sgl (adapter_t *adapter);
static void mega_8_to_40ld (mraid_inquiry *inquiry,
		mega_inquiry3 *enquiry3, mega_product_info *);

static int megadev_open (struct inode *, struct file *);
static int megadev_ioctl (struct file *, unsigned int, unsigned long);
static int mega_m_to_n(void __user *, nitioctl_t *);
static int mega_n_to_m(void __user *, megacmd_t *);

static int mega_init_scb (adapter_t *);

static int mega_is_bios_enabled (adapter_t *);

#ifdef CONFIG_PROC_FS
static int mega_print_inquiry(char *, char *);
static void mega_create_proc_entry(int, struct proc_dir_entry *);
static int proc_read_config(char *, char **, off_t, int, int *, void *);
static int proc_read_stat(char *, char **, off_t, int, int *, void *);
static int proc_read_mbox(char *, char **, off_t, int, int *, void *);
static int proc_rebuild_rate(char *, char **, off_t, int, int *, void *);
static int proc_battery(char *, char **, off_t, int, int *, void *);
static int proc_pdrv_ch0(char *, char **, off_t, int, int *, void *);
static int proc_pdrv_ch1(char *, char **, off_t, int, int *, void *);
static int proc_pdrv_ch2(char *, char **, off_t, int, int *, void *);
static int proc_pdrv_ch3(char *, char **, off_t, int, int *, void *);
static int proc_pdrv(adapter_t *, char *, int);
static int proc_rdrv_10(char *, char **, off_t, int, int *, void *);
static int proc_rdrv_20(char *, char **, off_t, int, int *, void *);
static int proc_rdrv_30(char *, char **, off_t, int, int *, void *);
static int proc_rdrv_40(char *, char **, off_t, int, int *, void *);
static int proc_rdrv(adapter_t *, char *, int, int);

static int mega_adapinq(adapter_t *, dma_addr_t);
static int mega_internal_dev_inquiry(adapter_t *, u8, u8, dma_addr_t);
#endif

static int mega_support_ext_cdb(adapter_t *);
static mega_passthru* mega_prepare_passthru(adapter_t *, scb_t *,
		Scsi_Cmnd *, int, int);
static mega_ext_passthru* mega_prepare_extpassthru(adapter_t *,
		scb_t *, Scsi_Cmnd *, int, int);
static void mega_enum_raid_scsi(adapter_t *);
static void mega_get_boot_drv(adapter_t *);
static int mega_support_random_del(adapter_t *);
static int mega_del_logdrv(adapter_t *, int);
static int mega_do_del_logdrv(adapter_t *, int);
static void mega_get_max_sgl(adapter_t *);
static int mega_internal_command(adapter_t *, megacmd_t *, mega_passthru *);
static void mega_internal_done(Scsi_Cmnd *);
static int mega_support_cluster(adapter_t *);
#endif

/* vi: set ts=8 sw=8 tw=78: */
