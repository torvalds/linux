/* SPDX-License-Identifier: GPL-2.0 */
/* to be used by qlogicfas and qlogic_cs */
#ifndef __QLOGICFAS408_H
#define __QLOGICFAS408_H

/*----------------------------------------------------------------*/
/* Configuration */

/* Set the following to max out the speed of the PIO PseudoDMA transfers,
   again, 0 tends to be slower, but more stable.  */

#define QL_TURBO_PDMA 1

/* This should be 1 to enable parity detection */

#define QL_ENABLE_PARITY 1

/* This will reset all devices when the driver is initialized (during bootup).
   The other linux drivers don't do this, but the DOS drivers do, and after
   using DOS or some kind of crash or lockup this will bring things back
   without requiring a cold boot.  It does take some time to recover from a
   reset, so it is slower, and I have seen timeouts so that devices weren't
   recognized when this was set. */

#define QL_RESET_AT_START 0

/* crystal frequency in megahertz (for offset 5 and 9)
   Please set this for your card.  Most Qlogic cards are 40 Mhz.  The
   Control Concepts ISA (not VLB) is 24 Mhz */

#define XTALFREQ	40

/**********/
/* DANGER! modify these at your own risk */
/* SLOWCABLE can usually be reset to zero if you have a clean setup and
   proper termination.  The rest are for synchronous transfers and other
   advanced features if your device can transfer faster than 5Mb/sec.
   If you are really curious, email me for a quick howto until I have
   something official */
/**********/

/*****/
/* config register 1 (offset 8) options */
/* This needs to be set to 1 if your cabling is long or noisy */
#define SLOWCABLE 1

/*****/
/* offset 0xc */
/* This will set fast (10Mhz) synchronous timing when set to 1
   For this to have an effect, FASTCLK must also be 1 */
#define FASTSCSI 0

/* This when set to 1 will set a faster sync transfer rate */
#define FASTCLK 0	/*(XTALFREQ>25?1:0)*/

/*****/
/* offset 6 */
/* This is the sync transfer divisor, XTALFREQ/X will be the maximum
   achievable data rate (assuming the rest of the system is capable
   and set properly) */
#define SYNCXFRPD 5	/*(XTALFREQ/5)*/

/*****/
/* offset 7 */
/* This is the count of how many synchronous transfers can take place
	i.e. how many reqs can occur before an ack is given.
	The maximum value for this is 15, the upper bits can modify
	REQ/ACK assertion and deassertion during synchronous transfers
	If this is 0, the bus will only transfer asynchronously */
#define SYNCOFFST 0
/* for the curious, bits 7&6 control the deassertion delay in 1/2 cycles
	of the 40Mhz clock. If FASTCLK is 1, specifying 01 (1/2) will
	cause the deassertion to be early by 1/2 clock.  Bits 5&4 control
	the assertion delay, also in 1/2 clocks (FASTCLK is ignored here). */

/*----------------------------------------------------------------*/

struct qlogicfas408_priv {
	int qbase;		/* Port */
	int qinitid;		/* initiator ID */
	int qabort;		/* Flag to cause an abort */
	int qlirq;		/* IRQ being used */
	int int_type;		/* type of irq, 2 for ISA board, 0 for PCMCIA */
	char qinfo[80];		/* description */
	struct scsi_cmnd *qlcmd;	/* current command being processed */
	struct Scsi_Host *shost;	/* pointer back to host */
	struct qlogicfas408_priv *next; /* next private struct */
};

/* The qlogic card uses two register maps - These macros select which one */
#define REG0 ( outb( inb( qbase + 0xd ) & 0x7f , qbase + 0xd ), outb( 4 , qbase + 0xd ))
#define REG1 ( outb( inb( qbase + 0xd ) | 0x80 , qbase + 0xd ), outb( 0xb4 | int_type, qbase + 0xd ))

/* following is watchdog timeout in microseconds */
#define WATCHDOG 5000000

/*----------------------------------------------------------------*/
/* the following will set the monitor border color (useful to find
   where something crashed or gets stuck at and as a simple profiler) */

#define rtrc(i) {}

#define get_priv_by_cmd(x) (struct qlogicfas408_priv *)&((x)->device->host->hostdata[0])
#define get_priv_by_host(x) (struct qlogicfas408_priv *)&((x)->hostdata[0])

irqreturn_t qlogicfas408_ihandl(int irq, void *dev_id);
int qlogicfas408_queuecommand(struct Scsi_Host *h, struct scsi_cmnd * cmd);
int qlogicfas408_biosparam(struct scsi_device * disk,
			   struct gendisk *unused,
			   sector_t capacity, int ip[]);
int qlogicfas408_abort(struct scsi_cmnd * cmd);
extern int qlogicfas408_host_reset(struct scsi_cmnd *cmd);
const char *qlogicfas408_info(struct Scsi_Host *host);
int qlogicfas408_get_chip_type(int qbase, int int_type);
void qlogicfas408_setup(int qbase, int id, int int_type);
int qlogicfas408_detect(int qbase, int int_type);
void qlogicfas408_disable_ints(struct qlogicfas408_priv *priv);
#endif	/* __QLOGICFAS408_H */

