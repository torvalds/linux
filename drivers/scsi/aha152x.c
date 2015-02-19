/* aha152x.c -- Adaptec AHA-152x driver
 * Author: Jürgen E. Fischer, fischer@norbit.de
 * Copyright 1993-2004 Jürgen E. Fischer
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
 *
 *
 * $Id: aha152x.c,v 2.7 2004/01/24 11:42:59 fischer Exp $
 *
 * $Log: aha152x.c,v $
 * Revision 2.7  2004/01/24 11:42:59  fischer
 * - gather code that is not used by PCMCIA at the end
 * - move request_region for !PCMCIA case to detection
 * - migration to new scsi host api (remove legacy code)
 * - free host scribble before scsi_done
 * - fix error handling
 * - one isapnp device added to id_table
 *
 * Revision 2.6  2003/10/30 20:52:47  fischer
 * - interfaces changes for kernel 2.6
 * - aha152x_probe_one introduced for pcmcia stub
 * - fixed pnpdev handling
 * - instead of allocation a new one, reuse command for request sense after check condition and reset
 * - fixes race in is_complete
 *
 * Revision 2.5  2002/04/14 11:24:53  fischer
 * - isapnp support
 * - abort fixed
 * - 2.5 support
 *
 * Revision 2.4  2000/12/16 12:53:56  fischer
 * - allow REQUEST SENSE to be queued
 * - handle shared PCI interrupts
 *
 * Revision 2.3  2000/11/04 16:40:26  fischer
 * - handle data overruns
 * - extend timeout for data phases
 *
 * Revision 2.2  2000/08/08 19:54:53  fischer
 * - minor changes
 *
 * Revision 2.1  2000/05/17 16:23:17  fischer
 * - signature update
 * - fix for data out w/o scatter gather
 *
 * Revision 2.0  1999/12/25 15:07:32  fischer
 * - interrupt routine completly reworked
 * - basic support for new eh code
 *
 * Revision 1.21  1999/11/10 23:46:36  fischer
 * - default to synchronous operation
 * - synchronous negotiation fixed
 * - added timeout to loops
 * - debugging output can be controlled through procfs
 *
 * Revision 1.20  1999/11/07 18:37:31  fischer
 * - synchronous operation works
 * - resid support for sg driver
 *
 * Revision 1.19  1999/11/02 22:39:59  fischer
 * - moved leading comments to README.aha152x
 * - new additional module parameters
 * - updates for 2.3
 * - support for the Tripace TC1550 controller
 * - interrupt handling changed
 *
 * Revision 1.18  1996/09/07 20:10:40  fischer
 * - fixed can_queue handling (multiple outstanding commands working again)
 *
 * Revision 1.17  1996/08/17 16:05:14  fischer
 * - biosparam improved
 * - interrupt verification
 * - updated documentation
 * - cleanups
 *
 * Revision 1.16  1996/06/09 00:04:56  root
 * - added configuration symbols for insmod (aha152x/aha152x1)
 *
 * Revision 1.15  1996/04/30 14:52:06  fischer
 * - proc info fixed
 * - support for extended translation for >1GB disks
 *
 * Revision 1.14  1996/01/17  15:11:20  fischer
 * - fixed lockup in MESSAGE IN phase after reconnection
 *
 * Revision 1.13  1996/01/09  02:15:53  fischer
 * - some cleanups
 * - moved request_irq behind controller initialization
 *   (to avoid spurious interrupts)
 *
 * Revision 1.12  1995/12/16  12:26:07  fischer
 * - barrier()s added
 * - configurable RESET delay added
 *
 * Revision 1.11  1995/12/06  21:18:35  fischer
 * - some minor updates
 *
 * Revision 1.10  1995/07/22  19:18:45  fischer
 * - support for 2 controllers
 * - started synchronous data transfers (not working yet)
 *
 * Revision 1.9  1995/03/18  09:20:24  root
 * - patches for PCMCIA and modules
 *
 * Revision 1.8  1995/01/21  22:07:19  root
 * - snarf_region => request_region
 * - aha152x_intr interface change
 *
 * Revision 1.7  1995/01/02  23:19:36  root
 * - updated COMMAND_SIZE to cmd_len
 * - changed sti() to restore_flags()
 * - fixed some #ifdef which generated warnings
 *
 * Revision 1.6  1994/11/24  20:35:27  root
 * - problem with odd number of bytes in fifo fixed
 *
 * Revision 1.5  1994/10/30  14:39:56  root
 * - abort code fixed
 * - debugging improved
 *
 * Revision 1.4  1994/09/12  11:33:01  root
 * - irqaction to request_irq
 * - abortion updated
 *
 * Revision 1.3  1994/08/04  13:53:05  root
 * - updates for mid-level-driver changes
 * - accept unexpected BUSFREE phase as error condition
 * - parity check now configurable
 *
 * Revision 1.2  1994/07/03  12:56:36  root
 * - cleaned up debugging code
 * - more tweaking on reset delays
 * - updated abort/reset code (pretty untested...)
 *
 * Revision 1.1  1994/05/28  21:18:49  root
 * - update for mid-level interface change (abort-reset)
 * - delays after resets adjusted for some slow devices
 *
 * Revision 1.0  1994/03/25  12:52:00  root
 * - Fixed "more data than expected" problem
 * - added new BIOS signatures
 *
 * Revision 0.102  1994/01/31  20:44:12  root
 * - minor changes in insw/outsw handling
 *
 * Revision 0.101  1993/12/13  01:16:27  root
 * - fixed STATUS phase (non-GOOD stati were dropped sometimes;
 *   fixes problems with CD-ROM sector size detection & media change)
 *
 * Revision 0.100  1993/12/10  16:58:47  root
 * - fix for unsuccessful selections in case of non-continuous id assignments
 *   on the scsi bus.
 *
 * Revision 0.99  1993/10/24  16:19:59  root
 * - fixed DATA IN (rare read errors gone)
 *
 * Revision 0.98  1993/10/17  12:54:44  root
 * - fixed some recent fixes (shame on me)
 * - moved initialization of scratch area to aha152x_queue
 *
 * Revision 0.97  1993/10/09  18:53:53  root
 * - DATA IN fixed. Rarely left data in the fifo.
 *
 * Revision 0.96  1993/10/03  00:53:59  root
 * - minor changes on DATA IN
 *
 * Revision 0.95  1993/09/24  10:36:01  root
 * - change handling of MSGI after reselection
 * - fixed sti/cli
 * - minor changes
 *
 * Revision 0.94  1993/09/18  14:08:22  root
 * - fixed bug in multiple outstanding command code
 * - changed detection
 * - support for kernel command line configuration
 * - reset corrected
 * - changed message handling
 *
 * Revision 0.93  1993/09/15  20:41:19  root
 * - fixed bugs with multiple outstanding commands
 *
 * Revision 0.92  1993/09/13  02:46:33  root
 * - multiple outstanding commands work (no problems with IBM drive)
 *
 * Revision 0.91  1993/09/12  20:51:46  root
 * added multiple outstanding commands
 * (some problem with this $%&? IBM device remain)
 *
 * Revision 0.9  1993/09/12  11:11:22  root
 * - corrected auto-configuration
 * - changed the auto-configuration (added some '#define's)
 * - added support for dis-/reconnection
 *
 * Revision 0.8  1993/09/06  23:09:39  root
 * - added support for the drive activity light
 * - minor changes
 *
 * Revision 0.7  1993/09/05  14:30:15  root
 * - improved phase detection
 * - now using the new snarf_region code of 0.99pl13
 *
 * Revision 0.6  1993/09/02  11:01:38  root
 * first public release; added some signatures and biosparam()
 *
 * Revision 0.5  1993/08/30  10:23:30  root
 * fixed timing problems with my IBM drive
 *
 * Revision 0.4  1993/08/29  14:06:52  root
 * fixed some problems with timeouts due incomplete commands
 *
 * Revision 0.3  1993/08/28  15:55:03  root
 * writing data works too.  mounted and worked on a dos partition
 *
 * Revision 0.2  1993/08/27  22:42:07  root
 * reading data works.  Mounted a msdos partition.
 *
 * Revision 0.1  1993/08/25  13:38:30  root
 * first "damn thing doesn't work" version
 *
 * Revision 0.0  1993/08/14  19:54:25  root
 * empty function bodies; detect() works.
 *
 *
 **************************************************************************

 see Documentation/scsi/aha152x.txt for configuration details

 **************************************************************************/

#include <linux/module.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/isapnp.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <scsi/scsicam.h>

#include "scsi.h"
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_spi.h>
#include <scsi/scsi_eh.h>
#include "aha152x.h"

static LIST_HEAD(aha152x_host_list);


/* DEFINES */

/* For PCMCIA cards, always use AUTOCONF */
#if defined(PCMCIA) || defined(MODULE)
#if !defined(AUTOCONF)
#define AUTOCONF
#endif
#endif

#if !defined(AUTOCONF) && !defined(SETUP0)
#error define AUTOCONF or SETUP0
#endif

#define	DO_LOCK(flags)		spin_lock_irqsave(&QLOCK,flags)
#define	DO_UNLOCK(flags)	spin_unlock_irqrestore(&QLOCK,flags)

#define LEAD		"(scsi%d:%d:%d) "
#define INFO_LEAD	KERN_INFO	LEAD
#define CMDINFO(cmd) \
			(cmd) ? ((cmd)->device->host->host_no) : -1, \
                        (cmd) ? ((cmd)->device->id & 0x0f) : -1, \
			(cmd) ? ((u8)(cmd)->device->lun & 0x07) : -1

static inline void
CMD_INC_RESID(struct scsi_cmnd *cmd, int inc)
{
	scsi_set_resid(cmd, scsi_get_resid(cmd) + inc);
}

#define DELAY_DEFAULT 1000

#if defined(PCMCIA)
#define IRQ_MIN 0
#define IRQ_MAX 16
#else
#define IRQ_MIN 9
#if defined(__PPC)
#define IRQ_MAX (nr_irqs-1)
#else
#define IRQ_MAX 12
#endif
#endif

enum {
	not_issued	= 0x0001,	/* command not yet issued */
	selecting	= 0x0002,	/* target is being selected */
	identified	= 0x0004,	/* IDENTIFY was sent */
	disconnected	= 0x0008,	/* target disconnected */
	completed	= 0x0010,	/* target sent COMMAND COMPLETE */
	aborted		= 0x0020,	/* ABORT was sent */
	resetted	= 0x0040,	/* BUS DEVICE RESET was sent */
	spiordy		= 0x0080,	/* waiting for SPIORDY to raise */
	syncneg		= 0x0100,	/* synchronous negotiation in progress */
	aborting	= 0x0200,	/* ABORT is pending */
	resetting	= 0x0400,	/* BUS DEVICE RESET is pending */
	check_condition = 0x0800,	/* requesting sense after CHECK CONDITION */
};

MODULE_AUTHOR("Jürgen Fischer");
MODULE_DESCRIPTION(AHA152X_REVID);
MODULE_LICENSE("GPL");

#if !defined(PCMCIA)
#if defined(MODULE)
static int io[] = {0, 0};
module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io,"base io address of controller");

static int irq[] = {0, 0};
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq,"interrupt for controller");

static int scsiid[] = {7, 7};
module_param_array(scsiid, int, NULL, 0);
MODULE_PARM_DESC(scsiid,"scsi id of controller");

static int reconnect[] = {1, 1};
module_param_array(reconnect, int, NULL, 0);
MODULE_PARM_DESC(reconnect,"allow targets to disconnect");

static int parity[] = {1, 1};
module_param_array(parity, int, NULL, 0);
MODULE_PARM_DESC(parity,"use scsi parity");

static int sync[] = {1, 1};
module_param_array(sync, int, NULL, 0);
MODULE_PARM_DESC(sync,"use synchronous transfers");

static int delay[] = {DELAY_DEFAULT, DELAY_DEFAULT};
module_param_array(delay, int, NULL, 0);
MODULE_PARM_DESC(delay,"scsi reset delay");

static int exttrans[] = {0, 0};
module_param_array(exttrans, int, NULL, 0);
MODULE_PARM_DESC(exttrans,"use extended translation");

static int aha152x[] = {0, 11, 7, 1, 1, 0, DELAY_DEFAULT, 0};
module_param_array(aha152x, int, NULL, 0);
MODULE_PARM_DESC(aha152x, "parameters for first controller");

static int aha152x1[] = {0, 11, 7, 1, 1, 0, DELAY_DEFAULT, 0};
module_param_array(aha152x1, int, NULL, 0);
MODULE_PARM_DESC(aha152x1, "parameters for second controller");
#endif /* MODULE */

#ifdef __ISAPNP__
static struct isapnp_device_id id_table[] = {
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1502), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1505), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1510), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1515), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1520), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x2015), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1522), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x2215), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1530), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x3015), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x1532), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x3215), 0 },
	{ ISAPNP_ANY_ID, ISAPNP_ANY_ID,	ISAPNP_VENDOR('A', 'D', 'P'), ISAPNP_FUNCTION(0x6360), 0 },
	{ ISAPNP_DEVICE_SINGLE_END, }
};
MODULE_DEVICE_TABLE(isapnp, id_table);
#endif /* ISAPNP */

#endif /* !PCMCIA */

static struct scsi_host_template aha152x_driver_template;

/*
 * internal states of the host
 *
 */
enum aha152x_state {
	idle=0,
	unknown,
	seldo,
	seldi,
	selto,
	busfree,
	msgo,
	cmd,
	msgi,
	status,
	datai,
	datao,
	parerr,
	rsti,
	maxstate
};

/*
 * current state information of the host
 *
 */
struct aha152x_hostdata {
	Scsi_Cmnd *issue_SC;
		/* pending commands to issue */

	Scsi_Cmnd *current_SC;
		/* current command on the bus */

	Scsi_Cmnd *disconnected_SC;
		/* commands that disconnected */

	Scsi_Cmnd *done_SC;
		/* command that was completed */

	spinlock_t lock;
		/* host lock */

#if defined(AHA152X_STAT)
	int	      total_commands;
	int	      disconnections;
	int	      busfree_without_any_action;
	int	      busfree_without_old_command;
	int	      busfree_without_new_command;
	int	      busfree_without_done_command;
	int	      busfree_with_check_condition;
	int	      count[maxstate];
	int	      count_trans[maxstate];
	unsigned long time[maxstate];
#endif

	int commands;		/* current number of commands */

	int reconnect;		/* disconnection allowed */
	int parity;		/* parity checking enabled */
	int synchronous;	/* synchronous transferes enabled */
	int delay;		/* reset out delay */
	int ext_trans;		/* extended translation enabled */

	int swint;		/* software-interrupt was fired during detect() */
	int service;		/* bh needs to be run */
	int in_intr;		/* bh is running */

	/* current state,
	   previous state,
	   last state different from current state */
	enum aha152x_state state, prevstate, laststate;

	int target;
		/* reconnecting target */

	unsigned char syncrate[8];
		/* current synchronous transfer agreements */

	unsigned char syncneg[8];
		/* 0: no negotiation;
		 * 1: negotiation in progress;
		 * 2: negotiation completed
		 */

	int cmd_i;
		/* number of sent bytes of current command */

	int msgi_len;
		/* number of received message bytes */
	unsigned char msgi[256];
		/* received message bytes */

	int msgo_i, msgo_len;
		/* number of sent bytes and length of current messages */
	unsigned char msgo[256];
		/* pending messages */

	int data_len;
		/* number of sent/received bytes in dataphase */

	unsigned long io_port0;
	unsigned long io_port1;

#ifdef __ISAPNP__
	struct pnp_dev *pnpdev;
#endif
	struct list_head host_list;
};


/*
 * host specific command extension
 *
 */
struct aha152x_scdata {
	Scsi_Cmnd *next;	/* next sc in queue */
	struct completion *done;/* semaphore to block on */
	struct scsi_eh_save ses;
};

/* access macros for hostdata */

#define HOSTDATA(shpnt)		((struct aha152x_hostdata *) &shpnt->hostdata)

#define HOSTNO			((shpnt)->host_no)

#define CURRENT_SC		(HOSTDATA(shpnt)->current_SC)
#define DONE_SC			(HOSTDATA(shpnt)->done_SC)
#define ISSUE_SC		(HOSTDATA(shpnt)->issue_SC)
#define DISCONNECTED_SC		(HOSTDATA(shpnt)->disconnected_SC)
#define QLOCK			(HOSTDATA(shpnt)->lock)
#define QLOCKER			(HOSTDATA(shpnt)->locker)
#define QLOCKERL		(HOSTDATA(shpnt)->lockerl)

#define STATE			(HOSTDATA(shpnt)->state)
#define PREVSTATE		(HOSTDATA(shpnt)->prevstate)
#define LASTSTATE		(HOSTDATA(shpnt)->laststate)

#define RECONN_TARGET		(HOSTDATA(shpnt)->target)

#define CMD_I			(HOSTDATA(shpnt)->cmd_i)

#define MSGO(i)			(HOSTDATA(shpnt)->msgo[i])
#define MSGO_I			(HOSTDATA(shpnt)->msgo_i)
#define MSGOLEN			(HOSTDATA(shpnt)->msgo_len)
#define ADDMSGO(x)		(MSGOLEN<256 ? (void)(MSGO(MSGOLEN++)=x) : aha152x_error(shpnt,"MSGO overflow"))

#define MSGI(i)			(HOSTDATA(shpnt)->msgi[i])
#define MSGILEN			(HOSTDATA(shpnt)->msgi_len)
#define ADDMSGI(x)		(MSGILEN<256 ? (void)(MSGI(MSGILEN++)=x) : aha152x_error(shpnt,"MSGI overflow"))

#define DATA_LEN		(HOSTDATA(shpnt)->data_len)

#define SYNCRATE		(HOSTDATA(shpnt)->syncrate[CURRENT_SC->device->id])
#define SYNCNEG			(HOSTDATA(shpnt)->syncneg[CURRENT_SC->device->id])

#define DELAY			(HOSTDATA(shpnt)->delay)
#define EXT_TRANS		(HOSTDATA(shpnt)->ext_trans)
#define TC1550			(HOSTDATA(shpnt)->tc1550)
#define RECONNECT		(HOSTDATA(shpnt)->reconnect)
#define PARITY			(HOSTDATA(shpnt)->parity)
#define SYNCHRONOUS		(HOSTDATA(shpnt)->synchronous)

#define HOSTIOPORT0		(HOSTDATA(shpnt)->io_port0)
#define HOSTIOPORT1		(HOSTDATA(shpnt)->io_port1)

#define SCDATA(SCpnt)		((struct aha152x_scdata *) (SCpnt)->host_scribble)
#define SCNEXT(SCpnt)		SCDATA(SCpnt)->next
#define SCSEM(SCpnt)		SCDATA(SCpnt)->done

#define SG_ADDRESS(buffer)	((char *) sg_virt((buffer)))

/* state handling */
static void seldi_run(struct Scsi_Host *shpnt);
static void seldo_run(struct Scsi_Host *shpnt);
static void selto_run(struct Scsi_Host *shpnt);
static void busfree_run(struct Scsi_Host *shpnt);

static void msgo_init(struct Scsi_Host *shpnt);
static void msgo_run(struct Scsi_Host *shpnt);
static void msgo_end(struct Scsi_Host *shpnt);

static void cmd_init(struct Scsi_Host *shpnt);
static void cmd_run(struct Scsi_Host *shpnt);
static void cmd_end(struct Scsi_Host *shpnt);

static void datai_init(struct Scsi_Host *shpnt);
static void datai_run(struct Scsi_Host *shpnt);
static void datai_end(struct Scsi_Host *shpnt);

static void datao_init(struct Scsi_Host *shpnt);
static void datao_run(struct Scsi_Host *shpnt);
static void datao_end(struct Scsi_Host *shpnt);

static void status_run(struct Scsi_Host *shpnt);

static void msgi_run(struct Scsi_Host *shpnt);
static void msgi_end(struct Scsi_Host *shpnt);

static void parerr_run(struct Scsi_Host *shpnt);
static void rsti_run(struct Scsi_Host *shpnt);

static void is_complete(struct Scsi_Host *shpnt);

/*
 * driver states
 *
 */
static struct {
	char		*name;
	void		(*init)(struct Scsi_Host *);
	void		(*run)(struct Scsi_Host *);
	void		(*end)(struct Scsi_Host *);
	int		spio;
} states[] = {
	{ "idle",	NULL,		NULL,		NULL,		0},
	{ "unknown",	NULL,		NULL,		NULL,		0},
	{ "seldo",	NULL,		seldo_run,	NULL,		0},
	{ "seldi",	NULL,		seldi_run,	NULL,		0},
	{ "selto",	NULL,		selto_run,	NULL,		0},
	{ "busfree",	NULL,		busfree_run,	NULL,		0},
	{ "msgo",	msgo_init,	msgo_run,	msgo_end,	1},
	{ "cmd",	cmd_init,	cmd_run,	cmd_end,	1},
	{ "msgi",	NULL,		msgi_run,	msgi_end,	1},
	{ "status",	NULL,		status_run,	NULL,		1},
	{ "datai",	datai_init,	datai_run,	datai_end,	0},
	{ "datao",	datao_init,	datao_run,	datao_end,	0},
	{ "parerr",	NULL,		parerr_run,	NULL,		0},
	{ "rsti",	NULL,		rsti_run,	NULL,		0},
};

/* setup & interrupt */
static irqreturn_t intr(int irq, void *dev_id);
static void reset_ports(struct Scsi_Host *shpnt);
static void aha152x_error(struct Scsi_Host *shpnt, char *msg);
static void done(struct Scsi_Host *shpnt, int error);

/* diagnostics */
static void show_command(Scsi_Cmnd * ptr);
static void show_queues(struct Scsi_Host *shpnt);
static void disp_enintr(struct Scsi_Host *shpnt);


/*
 *  queue services:
 *
 */
static inline void append_SC(Scsi_Cmnd **SC, Scsi_Cmnd *new_SC)
{
	Scsi_Cmnd *end;

	SCNEXT(new_SC) = NULL;
	if (!*SC)
		*SC = new_SC;
	else {
		for (end = *SC; SCNEXT(end); end = SCNEXT(end))
			;
		SCNEXT(end) = new_SC;
	}
}

static inline Scsi_Cmnd *remove_first_SC(Scsi_Cmnd ** SC)
{
	Scsi_Cmnd *ptr;

	ptr = *SC;
	if (ptr) {
		*SC = SCNEXT(*SC);
		SCNEXT(ptr)=NULL;
	}
	return ptr;
}

static inline Scsi_Cmnd *remove_lun_SC(Scsi_Cmnd ** SC, int target, int lun)
{
	Scsi_Cmnd *ptr, *prev;

	for (ptr = *SC, prev = NULL;
	     ptr && ((ptr->device->id != target) || (ptr->device->lun != lun));
	     prev = ptr, ptr = SCNEXT(ptr))
	     ;

	if (ptr) {
		if (prev)
			SCNEXT(prev) = SCNEXT(ptr);
		else
			*SC = SCNEXT(ptr);

		SCNEXT(ptr)=NULL;
	}

	return ptr;
}

static inline Scsi_Cmnd *remove_SC(Scsi_Cmnd **SC, Scsi_Cmnd *SCp)
{
	Scsi_Cmnd *ptr, *prev;

	for (ptr = *SC, prev = NULL;
	     ptr && SCp!=ptr;
	     prev = ptr, ptr = SCNEXT(ptr))
	     ;

	if (ptr) {
		if (prev)
			SCNEXT(prev) = SCNEXT(ptr);
		else
			*SC = SCNEXT(ptr);

		SCNEXT(ptr)=NULL;
	}

	return ptr;
}

static irqreturn_t swintr(int irqno, void *dev_id)
{
	struct Scsi_Host *shpnt = dev_id;

	HOSTDATA(shpnt)->swint++;

	SETPORT(DMACNTRL0, INTEN);
	return IRQ_HANDLED;
}

struct Scsi_Host *aha152x_probe_one(struct aha152x_setup *setup)
{
	struct Scsi_Host *shpnt;

	shpnt = scsi_host_alloc(&aha152x_driver_template, sizeof(struct aha152x_hostdata));
	if (!shpnt) {
		printk(KERN_ERR "aha152x: scsi_host_alloc failed\n");
		return NULL;
	}

	memset(HOSTDATA(shpnt), 0, sizeof *HOSTDATA(shpnt));
	INIT_LIST_HEAD(&HOSTDATA(shpnt)->host_list);

	/* need to have host registered before triggering any interrupt */
	list_add_tail(&HOSTDATA(shpnt)->host_list, &aha152x_host_list);

	shpnt->io_port   = setup->io_port;
	shpnt->n_io_port = IO_RANGE;
	shpnt->irq       = setup->irq;

	if (!setup->tc1550) {
		HOSTIOPORT0 = setup->io_port;
		HOSTIOPORT1 = setup->io_port;
	} else {
		HOSTIOPORT0 = setup->io_port+0x10;
		HOSTIOPORT1 = setup->io_port-0x10;
	}

	spin_lock_init(&QLOCK);
	RECONNECT   = setup->reconnect;
	SYNCHRONOUS = setup->synchronous;
	PARITY      = setup->parity;
	DELAY       = setup->delay;
	EXT_TRANS   = setup->ext_trans;

	SETPORT(SCSIID, setup->scsiid << 4);
	shpnt->this_id = setup->scsiid;

	if (setup->reconnect)
		shpnt->can_queue = AHA152X_MAXQUEUE;

	/* RESET OUT */
	printk("aha152x: resetting bus...\n");
	SETPORT(SCSISEQ, SCSIRSTO);
	mdelay(256);
	SETPORT(SCSISEQ, 0);
	mdelay(DELAY);

	reset_ports(shpnt);

	printk(KERN_INFO
	       "aha152x%d%s: "
	       "vital data: rev=%x, "
	       "io=0x%03lx (0x%03lx/0x%03lx), "
	       "irq=%d, "
	       "scsiid=%d, "
	       "reconnect=%s, "
	       "parity=%s, "
	       "synchronous=%s, "
	       "delay=%d, "
	       "extended translation=%s\n",
	       shpnt->host_no, setup->tc1550 ? " (tc1550 mode)" : "",
	       GETPORT(REV) & 0x7,
	       shpnt->io_port, HOSTIOPORT0, HOSTIOPORT1,
	       shpnt->irq,
	       shpnt->this_id,
	       RECONNECT ? "enabled" : "disabled",
	       PARITY ? "enabled" : "disabled",
	       SYNCHRONOUS ? "enabled" : "disabled",
	       DELAY,
	       EXT_TRANS ? "enabled" : "disabled");

	/* not expecting any interrupts */
	SETPORT(SIMODE0, 0);
	SETPORT(SIMODE1, 0);

	if (request_irq(shpnt->irq, swintr, IRQF_SHARED, "aha152x", shpnt)) {
		printk(KERN_ERR "aha152x%d: irq %d busy.\n", shpnt->host_no, shpnt->irq);
		goto out_host_put;
	}

	HOSTDATA(shpnt)->swint = 0;

	printk(KERN_INFO "aha152x%d: trying software interrupt, ", shpnt->host_no);

	mb();
	SETPORT(DMACNTRL0, SWINT|INTEN);
	mdelay(1000);
	free_irq(shpnt->irq, shpnt);

	if (!HOSTDATA(shpnt)->swint) {
		if (TESTHI(DMASTAT, INTSTAT)) {
			printk("lost.\n");
		} else {
			printk("failed.\n");
		}

		SETPORT(DMACNTRL0, INTEN);

		printk(KERN_ERR "aha152x%d: irq %d possibly wrong.  "
				"Please verify.\n", shpnt->host_no, shpnt->irq);
		goto out_host_put;
	}
	printk("ok.\n");


	/* clear interrupts */
	SETPORT(SSTAT0, 0x7f);
	SETPORT(SSTAT1, 0xef);

	if (request_irq(shpnt->irq, intr, IRQF_SHARED, "aha152x", shpnt)) {
		printk(KERN_ERR "aha152x%d: failed to reassign irq %d.\n", shpnt->host_no, shpnt->irq);
		goto out_host_put;
	}

	if( scsi_add_host(shpnt, NULL) ) {
		free_irq(shpnt->irq, shpnt);
		printk(KERN_ERR "aha152x%d: failed to add host.\n", shpnt->host_no);
		goto out_host_put;
	}

	scsi_scan_host(shpnt);

	return shpnt;

out_host_put:
	list_del(&HOSTDATA(shpnt)->host_list);
	scsi_host_put(shpnt);

	return NULL;
}

void aha152x_release(struct Scsi_Host *shpnt)
{
	if (!shpnt)
		return;

	scsi_remove_host(shpnt);
	if (shpnt->irq)
		free_irq(shpnt->irq, shpnt);

#if !defined(PCMCIA)
	if (shpnt->io_port)
		release_region(shpnt->io_port, IO_RANGE);
#endif

#ifdef __ISAPNP__
	if (HOSTDATA(shpnt)->pnpdev)
		pnp_device_detach(HOSTDATA(shpnt)->pnpdev);
#endif

	list_del(&HOSTDATA(shpnt)->host_list);
	scsi_host_put(shpnt);
}


/*
 * setup controller to generate interrupts depending
 * on current state (lock has to be acquired)
 *
 */
static int setup_expected_interrupts(struct Scsi_Host *shpnt)
{
	if(CURRENT_SC) {
		CURRENT_SC->SCp.phase |= 1 << 16;

		if(CURRENT_SC->SCp.phase & selecting) {
			SETPORT(SSTAT1, SELTO);
			SETPORT(SIMODE0, ENSELDO | (DISCONNECTED_SC ? ENSELDI : 0));
			SETPORT(SIMODE1, ENSELTIMO);
		} else {
			SETPORT(SIMODE0, (CURRENT_SC->SCp.phase & spiordy) ? ENSPIORDY : 0);
			SETPORT(SIMODE1, ENPHASEMIS | ENSCSIRST | ENSCSIPERR | ENBUSFREE);
		}
	} else if(STATE==seldi) {
		SETPORT(SIMODE0, 0);
		SETPORT(SIMODE1, ENPHASEMIS | ENSCSIRST | ENSCSIPERR | ENBUSFREE);
	} else {
		SETPORT(SIMODE0, DISCONNECTED_SC ? ENSELDI : 0);
		SETPORT(SIMODE1, ENSCSIRST | ( (ISSUE_SC||DONE_SC) ? ENBUSFREE : 0));
	}

	if(!HOSTDATA(shpnt)->in_intr)
		SETBITS(DMACNTRL0, INTEN);

	return TESTHI(DMASTAT, INTSTAT);
}


/*
 *  Queue a command and setup interrupts for a free bus.
 */
static int aha152x_internal_queue(Scsi_Cmnd *SCpnt, struct completion *complete,
		int phase, void (*done)(Scsi_Cmnd *))
{
	struct Scsi_Host *shpnt = SCpnt->device->host;
	unsigned long flags;

	SCpnt->scsi_done	= done;
	SCpnt->SCp.phase	= not_issued | phase;
	SCpnt->SCp.Status	= 0x1; /* Ilegal status by SCSI standard */
	SCpnt->SCp.Message	= 0;
	SCpnt->SCp.have_data_in	= 0;
	SCpnt->SCp.sent_command	= 0;

	if(SCpnt->SCp.phase & (resetting|check_condition)) {
		if (!SCpnt->host_scribble || SCSEM(SCpnt) || SCNEXT(SCpnt)) {
			scmd_printk(KERN_ERR, SCpnt, "cannot reuse command\n");
			return FAILED;
		}
	} else {
		SCpnt->host_scribble = kmalloc(sizeof(struct aha152x_scdata), GFP_ATOMIC);
		if(!SCpnt->host_scribble) {
			scmd_printk(KERN_ERR, SCpnt, "allocation failed\n");
			return FAILED;
		}
	}

	SCNEXT(SCpnt)		= NULL;
	SCSEM(SCpnt)		= complete;

	/* setup scratch area
	   SCp.ptr              : buffer pointer
	   SCp.this_residual    : buffer length
	   SCp.buffer           : next buffer
	   SCp.buffers_residual : left buffers in list
	   SCp.phase            : current state of the command */

	if ((phase & resetting) || !scsi_sglist(SCpnt)) {
		SCpnt->SCp.ptr           = NULL;
		SCpnt->SCp.this_residual = 0;
		scsi_set_resid(SCpnt, 0);
		SCpnt->SCp.buffer           = NULL;
		SCpnt->SCp.buffers_residual = 0;
	} else {
		scsi_set_resid(SCpnt, scsi_bufflen(SCpnt));
		SCpnt->SCp.buffer           = scsi_sglist(SCpnt);
		SCpnt->SCp.ptr              = SG_ADDRESS(SCpnt->SCp.buffer);
		SCpnt->SCp.this_residual    = SCpnt->SCp.buffer->length;
		SCpnt->SCp.buffers_residual = scsi_sg_count(SCpnt) - 1;
	}

	DO_LOCK(flags);

#if defined(AHA152X_STAT)
	HOSTDATA(shpnt)->total_commands++;
#endif

	/* Turn led on, when this is the first command. */
	HOSTDATA(shpnt)->commands++;
	if (HOSTDATA(shpnt)->commands==1)
		SETPORT(PORTA, 1);

	append_SC(&ISSUE_SC, SCpnt);

	if(!HOSTDATA(shpnt)->in_intr)
		setup_expected_interrupts(shpnt);

	DO_UNLOCK(flags);

	return 0;
}

/*
 *  queue a command
 *
 */
static int aha152x_queue_lck(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
	return aha152x_internal_queue(SCpnt, NULL, 0, done);
}

static DEF_SCSI_QCMD(aha152x_queue)


/*
 *
 */
static void reset_done(Scsi_Cmnd *SCpnt)
{
	if(SCSEM(SCpnt)) {
		complete(SCSEM(SCpnt));
	} else {
		printk(KERN_ERR "aha152x: reset_done w/o completion\n");
	}
}

/*
 *  Abort a command
 *
 */
static int aha152x_abort(Scsi_Cmnd *SCpnt)
{
	struct Scsi_Host *shpnt = SCpnt->device->host;
	Scsi_Cmnd *ptr;
	unsigned long flags;

	DO_LOCK(flags);

	ptr=remove_SC(&ISSUE_SC, SCpnt);

	if(ptr) {
		HOSTDATA(shpnt)->commands--;
		if (!HOSTDATA(shpnt)->commands)
			SETPORT(PORTA, 0);
		DO_UNLOCK(flags);

		kfree(SCpnt->host_scribble);
		SCpnt->host_scribble=NULL;

		return SUCCESS;
	}

	DO_UNLOCK(flags);

	/*
	 * FIXME:
	 * for current command: queue ABORT for message out and raise ATN
	 * for disconnected command: pseudo SC with ABORT message or ABORT on reselection?
	 *
	 */

	scmd_printk(KERN_ERR, SCpnt,
		    "cannot abort running or disconnected command\n");

	return FAILED;
}

/*
 * Reset a device
 *
 */
static int aha152x_device_reset(Scsi_Cmnd * SCpnt)
{
	struct Scsi_Host *shpnt = SCpnt->device->host;
	DECLARE_COMPLETION(done);
	int ret, issued, disconnected;
	unsigned char old_cmd_len = SCpnt->cmd_len;
	unsigned long flags;
	unsigned long timeleft;

	if(CURRENT_SC==SCpnt) {
		scmd_printk(KERN_ERR, SCpnt, "cannot reset current device\n");
		return FAILED;
	}

	DO_LOCK(flags);
	issued       = remove_SC(&ISSUE_SC, SCpnt) == NULL;
	disconnected = issued && remove_SC(&DISCONNECTED_SC, SCpnt);
	DO_UNLOCK(flags);

	SCpnt->cmd_len         = 0;

	aha152x_internal_queue(SCpnt, &done, resetting, reset_done);

	timeleft = wait_for_completion_timeout(&done, 100*HZ);
	if (!timeleft) {
		/* remove command from issue queue */
		DO_LOCK(flags);
		remove_SC(&ISSUE_SC, SCpnt);
		DO_UNLOCK(flags);
	}

	SCpnt->cmd_len         = old_cmd_len;

	DO_LOCK(flags);

	if(SCpnt->SCp.phase & resetted) {
		HOSTDATA(shpnt)->commands--;
		if (!HOSTDATA(shpnt)->commands)
			SETPORT(PORTA, 0);
		kfree(SCpnt->host_scribble);
		SCpnt->host_scribble=NULL;

		ret = SUCCESS;
	} else {
		/* requeue */
		if(!issued) {
			append_SC(&ISSUE_SC, SCpnt);
		} else if(disconnected) {
			append_SC(&DISCONNECTED_SC, SCpnt);
		}

		ret = FAILED;
	}

	DO_UNLOCK(flags);
	return ret;
}

static void free_hard_reset_SCs(struct Scsi_Host *shpnt, Scsi_Cmnd **SCs)
{
	Scsi_Cmnd *ptr;

	ptr=*SCs;
	while(ptr) {
		Scsi_Cmnd *next;

		if(SCDATA(ptr)) {
			next = SCNEXT(ptr);
		} else {
			scmd_printk(KERN_DEBUG, ptr,
				    "queue corrupted at %p\n", ptr);
			next = NULL;
		}

		if (!ptr->device->soft_reset) {
			remove_SC(SCs, ptr);
			HOSTDATA(shpnt)->commands--;
			kfree(ptr->host_scribble);
			ptr->host_scribble=NULL;
		}

		ptr = next;
	}
}

/*
 * Reset the bus
 *
 */
static int aha152x_bus_reset_host(struct Scsi_Host *shpnt)
{
	unsigned long flags;

	DO_LOCK(flags);

	free_hard_reset_SCs(shpnt, &ISSUE_SC);
	free_hard_reset_SCs(shpnt, &DISCONNECTED_SC);

	SETPORT(SCSISEQ, SCSIRSTO);
	mdelay(256);
	SETPORT(SCSISEQ, 0);
	mdelay(DELAY);

	setup_expected_interrupts(shpnt);
	if(HOSTDATA(shpnt)->commands==0)
		SETPORT(PORTA, 0);

	DO_UNLOCK(flags);

	return SUCCESS;
}

/*
 * Reset the bus
 *
 */
static int aha152x_bus_reset(Scsi_Cmnd *SCpnt)
{
	return aha152x_bus_reset_host(SCpnt->device->host);
}

/*
 *  Restore default values to the AIC-6260 registers and reset the fifos
 *
 */
static void reset_ports(struct Scsi_Host *shpnt)
{
	unsigned long flags;

	/* disable interrupts */
	SETPORT(DMACNTRL0, RSTFIFO);

	SETPORT(SCSISEQ, 0);

	SETPORT(SXFRCTL1, 0);
	SETPORT(SCSISIG, 0);
	SETRATE(0);

	/* clear all interrupt conditions */
	SETPORT(SSTAT0, 0x7f);
	SETPORT(SSTAT1, 0xef);

	SETPORT(SSTAT4, SYNCERR | FWERR | FRERR);

	SETPORT(DMACNTRL0, 0);
	SETPORT(DMACNTRL1, 0);

	SETPORT(BRSTCNTRL, 0xf1);

	/* clear SCSI fifos and transfer count */
	SETPORT(SXFRCTL0, CH1|CLRCH1|CLRSTCNT);
	SETPORT(SXFRCTL0, CH1);

	DO_LOCK(flags);
	setup_expected_interrupts(shpnt);
	DO_UNLOCK(flags);
}

/*
 * Reset the host (bus and controller)
 *
 */
int aha152x_host_reset_host(struct Scsi_Host *shpnt)
{
	aha152x_bus_reset_host(shpnt);
	reset_ports(shpnt);

	return SUCCESS;
}

/*
 * Reset the host (bus and controller)
 *
 */
static int aha152x_host_reset(Scsi_Cmnd *SCpnt)
{
	return aha152x_host_reset_host(SCpnt->device->host);
}

/*
 * Return the "logical geometry"
 *
 */
static int aha152x_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		sector_t capacity, int *info_array)
{
	struct Scsi_Host *shpnt = sdev->host;

	/* try default translation */
	info_array[0] = 64;
	info_array[1] = 32;
	info_array[2] = (unsigned long)capacity / (64 * 32);

	/* for disks >1GB do some guessing */
	if (info_array[2] >= 1024) {
		int info[3];

		/* try to figure out the geometry from the partition table */
		if (scsicam_bios_param(bdev, capacity, info) < 0 ||
		    !((info[0] == 64 && info[1] == 32) || (info[0] == 255 && info[1] == 63))) {
			if (EXT_TRANS) {
				printk(KERN_NOTICE
				       "aha152x: unable to verify geometry for disk with >1GB.\n"
				       "         using extended translation.\n");
				info_array[0] = 255;
				info_array[1] = 63;
				info_array[2] = (unsigned long)capacity / (255 * 63);
			} else {
				printk(KERN_NOTICE
				       "aha152x: unable to verify geometry for disk with >1GB.\n"
				       "         Using default translation. Please verify yourself.\n"
				       "         Perhaps you need to enable extended translation in the driver.\n"
				       "         See Documentation/scsi/aha152x.txt for details.\n");
			}
		} else {
			info_array[0] = info[0];
			info_array[1] = info[1];
			info_array[2] = info[2];

			if (info[0] == 255 && !EXT_TRANS) {
				printk(KERN_NOTICE
				       "aha152x: current partition table is using extended translation.\n"
				       "         using it also, although it's not explicitly enabled.\n");
			}
		}
	}

	return 0;
}

/*
 *  Internal done function
 *
 */
static void done(struct Scsi_Host *shpnt, int error)
{
	if (CURRENT_SC) {
		if(DONE_SC)
			scmd_printk(KERN_ERR, CURRENT_SC,
				    "there's already a completed command %p "
				    "- will cause abort\n", DONE_SC);

		DONE_SC = CURRENT_SC;
		CURRENT_SC = NULL;
		DONE_SC->result = error;
	} else
		printk(KERN_ERR "aha152x: done() called outside of command\n");
}

static struct work_struct aha152x_tq;

/*
 * Run service completions on the card with interrupts enabled.
 *
 */
static void run(struct work_struct *work)
{
	struct aha152x_hostdata *hd;

	list_for_each_entry(hd, &aha152x_host_list, host_list) {
		struct Scsi_Host *shost = container_of((void *)hd, struct Scsi_Host, hostdata);

		is_complete(shost);
	}
}

/*
 * Interrupt handler
 *
 */
static irqreturn_t intr(int irqno, void *dev_id)
{
	struct Scsi_Host *shpnt = dev_id;
	unsigned long flags;
	unsigned char rev, dmacntrl0;

	/*
	 * Read a couple of registers that are known to not be all 1's. If
	 * we read all 1's (-1), that means that either:
	 *
	 * a. The host adapter chip has gone bad, and we cannot control it,
	 *	OR
	 * b. The host adapter is a PCMCIA card that has been ejected
	 *
	 * In either case, we cannot do anything with the host adapter at
	 * this point in time. So just ignore the interrupt and return.
	 * In the latter case, the interrupt might actually be meant for
	 * someone else sharing this IRQ, and that driver will handle it.
	 */
	rev = GETPORT(REV);
	dmacntrl0 = GETPORT(DMACNTRL0);
	if ((rev == 0xFF) && (dmacntrl0 == 0xFF))
		return IRQ_NONE;

	if( TESTLO(DMASTAT, INTSTAT) )
		return IRQ_NONE;

	/* no more interrupts from the controller, while we're busy.
	   INTEN is restored by the BH handler */
	CLRBITS(DMACNTRL0, INTEN);

	DO_LOCK(flags);
	if( HOSTDATA(shpnt)->service==0 ) {
		HOSTDATA(shpnt)->service=1;

		/* Poke the BH handler */
		INIT_WORK(&aha152x_tq, run);
		schedule_work(&aha152x_tq);
	}
	DO_UNLOCK(flags);

	return IRQ_HANDLED;
}

/*
 * busfree phase
 * - handle completition/disconnection/error of current command
 * - start selection for next command (if any)
 */
static void busfree_run(struct Scsi_Host *shpnt)
{
	unsigned long flags;
#if defined(AHA152X_STAT)
	int action=0;
#endif

	SETPORT(SXFRCTL0, CH1|CLRCH1|CLRSTCNT);
	SETPORT(SXFRCTL0, CH1);

	SETPORT(SSTAT1, CLRBUSFREE);

	if(CURRENT_SC) {
#if defined(AHA152X_STAT)
		action++;
#endif
		CURRENT_SC->SCp.phase &= ~syncneg;

		if(CURRENT_SC->SCp.phase & completed) {
			/* target sent COMMAND COMPLETE */
			done(shpnt, (CURRENT_SC->SCp.Status & 0xff) | ((CURRENT_SC->SCp.Message & 0xff) << 8) | (DID_OK << 16));

		} else if(CURRENT_SC->SCp.phase & aborted) {
			done(shpnt, (CURRENT_SC->SCp.Status & 0xff) | ((CURRENT_SC->SCp.Message & 0xff) << 8) | (DID_ABORT << 16));

		} else if(CURRENT_SC->SCp.phase & resetted) {
			done(shpnt, (CURRENT_SC->SCp.Status & 0xff) | ((CURRENT_SC->SCp.Message & 0xff) << 8) | (DID_RESET << 16));

		} else if(CURRENT_SC->SCp.phase & disconnected) {
			/* target sent DISCONNECT */
#if defined(AHA152X_STAT)
			HOSTDATA(shpnt)->disconnections++;
#endif
			append_SC(&DISCONNECTED_SC, CURRENT_SC);
			CURRENT_SC->SCp.phase |= 1 << 16;
			CURRENT_SC = NULL;

		} else {
			done(shpnt, DID_ERROR << 16);
		}
#if defined(AHA152X_STAT)
	} else {
		HOSTDATA(shpnt)->busfree_without_old_command++;
#endif
	}

	DO_LOCK(flags);

	if(DONE_SC) {
#if defined(AHA152X_STAT)
		action++;
#endif

		if(DONE_SC->SCp.phase & check_condition) {
			struct scsi_cmnd *cmd = HOSTDATA(shpnt)->done_SC;
			struct aha152x_scdata *sc = SCDATA(cmd);

			scsi_eh_restore_cmnd(cmd, &sc->ses);

			cmd->SCp.Status = SAM_STAT_CHECK_CONDITION;

			HOSTDATA(shpnt)->commands--;
			if (!HOSTDATA(shpnt)->commands)
				SETPORT(PORTA, 0);	/* turn led off */
		} else if(DONE_SC->SCp.Status==SAM_STAT_CHECK_CONDITION) {
#if defined(AHA152X_STAT)
			HOSTDATA(shpnt)->busfree_with_check_condition++;
#endif

			if(!(DONE_SC->SCp.phase & not_issued)) {
				struct aha152x_scdata *sc;
				Scsi_Cmnd *ptr = DONE_SC;
				DONE_SC=NULL;

				sc = SCDATA(ptr);
				/* It was allocated in aha152x_internal_queue? */
				BUG_ON(!sc);
				scsi_eh_prep_cmnd(ptr, &sc->ses, NULL, 0, ~0);

				DO_UNLOCK(flags);
				aha152x_internal_queue(ptr, NULL, check_condition, ptr->scsi_done);
				DO_LOCK(flags);
			}
		}

		if(DONE_SC && DONE_SC->scsi_done) {
			Scsi_Cmnd *ptr = DONE_SC;
			DONE_SC=NULL;

			/* turn led off, when no commands are in the driver */
			HOSTDATA(shpnt)->commands--;
			if (!HOSTDATA(shpnt)->commands)
				SETPORT(PORTA, 0);	/* turn led off */

			if(ptr->scsi_done != reset_done) {
				kfree(ptr->host_scribble);
				ptr->host_scribble=NULL;
			}

			DO_UNLOCK(flags);
			ptr->scsi_done(ptr);
			DO_LOCK(flags);
		}

		DONE_SC=NULL;
#if defined(AHA152X_STAT)
	} else {
		HOSTDATA(shpnt)->busfree_without_done_command++;
#endif
	}

	if(ISSUE_SC)
		CURRENT_SC = remove_first_SC(&ISSUE_SC);

	DO_UNLOCK(flags);

	if(CURRENT_SC) {
#if defined(AHA152X_STAT)
		action++;
#endif
		CURRENT_SC->SCp.phase |= selecting;

		/* clear selection timeout */
		SETPORT(SSTAT1, SELTO);

		SETPORT(SCSIID, (shpnt->this_id << OID_) | CURRENT_SC->device->id);
		SETPORT(SXFRCTL1, (PARITY ? ENSPCHK : 0 ) | ENSTIMER);
		SETPORT(SCSISEQ, ENSELO | ENAUTOATNO | (DISCONNECTED_SC ? ENRESELI : 0));
	} else {
#if defined(AHA152X_STAT)
		HOSTDATA(shpnt)->busfree_without_new_command++;
#endif
		SETPORT(SCSISEQ, DISCONNECTED_SC ? ENRESELI : 0);
	}

#if defined(AHA152X_STAT)
	if(!action)
		HOSTDATA(shpnt)->busfree_without_any_action++;
#endif
}

/*
 * Selection done (OUT)
 * - queue IDENTIFY message and SDTR to selected target for message out
 *   (ATN asserted automagically via ENAUTOATNO in busfree())
 */
static void seldo_run(struct Scsi_Host *shpnt)
{
	SETPORT(SCSISIG, 0);
	SETPORT(SSTAT1, CLRBUSFREE);
	SETPORT(SSTAT1, CLRPHASECHG);

	CURRENT_SC->SCp.phase &= ~(selecting|not_issued);

	SETPORT(SCSISEQ, 0);

	if (TESTLO(SSTAT0, SELDO)) {
		scmd_printk(KERN_ERR, CURRENT_SC,
			    "aha152x: passing bus free condition\n");
		done(shpnt, DID_NO_CONNECT << 16);
		return;
	}

	SETPORT(SSTAT0, CLRSELDO);

	ADDMSGO(IDENTIFY(RECONNECT, CURRENT_SC->device->lun));

	if (CURRENT_SC->SCp.phase & aborting) {
		ADDMSGO(ABORT);
	} else if (CURRENT_SC->SCp.phase & resetting) {
		ADDMSGO(BUS_DEVICE_RESET);
	} else if (SYNCNEG==0 && SYNCHRONOUS) {
		CURRENT_SC->SCp.phase |= syncneg;
		MSGOLEN += spi_populate_sync_msg(&MSGO(MSGOLEN), 50, 8);
		SYNCNEG=1;		/* negotiation in progress */
	}

	SETRATE(SYNCRATE);
}

/*
 * Selection timeout
 * - return command to mid-level with failure cause
 *
 */
static void selto_run(struct Scsi_Host *shpnt)
{
	SETPORT(SCSISEQ, 0);
	SETPORT(SSTAT1, CLRSELTIMO);

	if (!CURRENT_SC)
		return;

	CURRENT_SC->SCp.phase &= ~selecting;

	if (CURRENT_SC->SCp.phase & aborted)
		done(shpnt, DID_ABORT << 16);
	else if (TESTLO(SSTAT0, SELINGO))
		done(shpnt, DID_BUS_BUSY << 16);
	else
		/* ARBITRATION won, but SELECTION failed */
		done(shpnt, DID_NO_CONNECT << 16);
}

/*
 * Selection in done
 * - put current command back to issue queue
 *   (reconnection of a disconnected nexus instead
 *    of successful selection out)
 *
 */
static void seldi_run(struct Scsi_Host *shpnt)
{
	int selid;
	int target;
	unsigned long flags;

	SETPORT(SCSISIG, 0);
	SETPORT(SSTAT0, CLRSELDI);
	SETPORT(SSTAT1, CLRBUSFREE);
	SETPORT(SSTAT1, CLRPHASECHG);

	if(CURRENT_SC) {
		if(!(CURRENT_SC->SCp.phase & not_issued))
			scmd_printk(KERN_ERR, CURRENT_SC,
				    "command should not have been issued yet\n");

		DO_LOCK(flags);
		append_SC(&ISSUE_SC, CURRENT_SC);
		DO_UNLOCK(flags);

		CURRENT_SC = NULL;
	}

	if (!DISCONNECTED_SC)
		return;

	RECONN_TARGET=-1;

	selid = GETPORT(SELID) & ~(1 << shpnt->this_id);

	if (selid==0) {
		shost_printk(KERN_INFO, shpnt,
			     "target id unknown (%02x)\n", selid);
		return;
	}

	for(target=7; !(selid & (1 << target)); target--)
		;

	if(selid & ~(1 << target)) {
		shost_printk(KERN_INFO, shpnt,
			     "multiple targets reconnected (%02x)\n", selid);
	}


	SETPORT(SCSIID, (shpnt->this_id << OID_) | target);
	SETPORT(SCSISEQ, 0);

	SETRATE(HOSTDATA(shpnt)->syncrate[target]);

	RECONN_TARGET=target;
}

/*
 * message in phase
 * - handle initial message after reconnection to identify
 *   reconnecting nexus
 * - queue command on DISCONNECTED_SC on DISCONNECT message
 * - set completed flag on COMMAND COMPLETE
 *   (other completition code moved to busfree_run)
 * - handle response to SDTR
 * - clear synchronous transfer agreements on BUS RESET
 *
 * FIXME: what about SAVE POINTERS, RESTORE POINTERS?
 *
 */
static void msgi_run(struct Scsi_Host *shpnt)
{
	for(;;) {
		int sstat1 = GETPORT(SSTAT1);

		if(sstat1 & (PHASECHG|PHASEMIS|BUSFREE) || !(sstat1 & REQINIT))
			return;

		if (TESTLO(SSTAT0, SPIORDY))
			return;

		ADDMSGI(GETPORT(SCSIDAT));

		if(!CURRENT_SC) {
			if(LASTSTATE!=seldi) {
				shost_printk(KERN_ERR, shpnt,
					     "message in w/o current command"
					     " not after reselection\n");
			}

			/*
			 * Handle reselection
			 */
			if(!(MSGI(0) & IDENTIFY_BASE)) {
				shost_printk(KERN_ERR, shpnt,
					     "target didn't identify after reselection\n");
				continue;
			}

			CURRENT_SC = remove_lun_SC(&DISCONNECTED_SC, RECONN_TARGET, MSGI(0) & 0x3f);

			if (!CURRENT_SC) {
				show_queues(shpnt);
				shost_printk(KERN_ERR, shpnt,
					     "no disconnected command"
					     " for target %d/%d\n",
					     RECONN_TARGET, MSGI(0) & 0x3f);
				continue;
			}

			CURRENT_SC->SCp.Message = MSGI(0);
			CURRENT_SC->SCp.phase &= ~disconnected;

			MSGILEN=0;

			/* next message if any */
			continue;
		}

		CURRENT_SC->SCp.Message = MSGI(0);

		switch (MSGI(0)) {
		case DISCONNECT:
			if (!RECONNECT)
				scmd_printk(KERN_WARNING, CURRENT_SC,
					    "target was not allowed to disconnect\n");

			CURRENT_SC->SCp.phase |= disconnected;
			break;

		case COMMAND_COMPLETE:
			CURRENT_SC->SCp.phase |= completed;
			break;

		case MESSAGE_REJECT:
			if (SYNCNEG==1) {
				scmd_printk(KERN_INFO, CURRENT_SC,
					    "Synchronous Data Transfer Request"
					    " was rejected\n");
				SYNCNEG=2;	/* negotiation completed */
			} else
				scmd_printk(KERN_INFO, CURRENT_SC,
					    "inbound message (MESSAGE REJECT)\n");
			break;

		case SAVE_POINTERS:
			break;

		case RESTORE_POINTERS:
			break;

		case EXTENDED_MESSAGE:
			if(MSGILEN<2 || MSGILEN<MSGI(1)+2) {
				/* not yet completed */
				continue;
			}

			switch (MSGI(2)) {
			case EXTENDED_SDTR:
				{
					long ticks;

					if (MSGI(1) != 3) {
						scmd_printk(KERN_ERR, CURRENT_SC,
							    "SDTR message length!=3\n");
						break;
					}

					if (!HOSTDATA(shpnt)->synchronous)
						break;

					printk(INFO_LEAD, CMDINFO(CURRENT_SC));
					spi_print_msg(&MSGI(0));
					printk("\n");

					ticks = (MSGI(3) * 4 + 49) / 50;

					if (syncneg) {
						/* negotiation in progress */
						if (ticks > 9 || MSGI(4) < 1 || MSGI(4) > 8) {
							ADDMSGO(MESSAGE_REJECT);
							scmd_printk(KERN_INFO,
								    CURRENT_SC,
								    "received Synchronous Data Transfer Request invalid - rejected\n");
							break;
						}

						SYNCRATE |= ((ticks - 2) << 4) + MSGI(4);
					} else if (ticks <= 9 && MSGI(4) >= 1) {
						ADDMSGO(EXTENDED_MESSAGE);
						ADDMSGO(3);
						ADDMSGO(EXTENDED_SDTR);
						if (ticks < 4) {
							ticks = 4;
							ADDMSGO(50);
						} else
							ADDMSGO(MSGI(3));

						if (MSGI(4) > 8)
							MSGI(4) = 8;

						ADDMSGO(MSGI(4));

						SYNCRATE |= ((ticks - 2) << 4) + MSGI(4);
					} else {
						/* requested SDTR is too slow, do it asynchronously */
						scmd_printk(KERN_INFO,
							    CURRENT_SC,
							    "Synchronous Data Transfer Request too slow - Rejecting\n");
						ADDMSGO(MESSAGE_REJECT);
					}

					/* negotiation completed */
					SYNCNEG=2;
					SETRATE(SYNCRATE);
				}
				break;

			case BUS_DEVICE_RESET:
				{
					int i;

					for(i=0; i<8; i++) {
						HOSTDATA(shpnt)->syncrate[i]=0;
						HOSTDATA(shpnt)->syncneg[i]=0;
					}

				}
				break;

			case EXTENDED_MODIFY_DATA_POINTER:
			case EXTENDED_EXTENDED_IDENTIFY:
			case EXTENDED_WDTR:
			default:
				ADDMSGO(MESSAGE_REJECT);
				break;
			}
			break;
		}

		MSGILEN=0;
	}
}

static void msgi_end(struct Scsi_Host *shpnt)
{
	if(MSGILEN>0)
		scmd_printk(KERN_WARNING, CURRENT_SC,
			    "target left before message completed (%d)\n",
			    MSGILEN);

	if (MSGOLEN > 0 && !(GETPORT(SSTAT1) & BUSFREE))
		SETPORT(SCSISIG, P_MSGI | SIG_ATNO);
}

/*
 * message out phase
 *
 */
static void msgo_init(struct Scsi_Host *shpnt)
{
	if(MSGOLEN==0) {
		if((CURRENT_SC->SCp.phase & syncneg) && SYNCNEG==2 && SYNCRATE==0) {
			ADDMSGO(IDENTIFY(RECONNECT, CURRENT_SC->device->lun));
		} else {
			scmd_printk(KERN_INFO, CURRENT_SC,
				    "unexpected MESSAGE OUT phase; rejecting\n");
			ADDMSGO(MESSAGE_REJECT);
		}
	}

}

/*
 * message out phase
 *
 */
static void msgo_run(struct Scsi_Host *shpnt)
{
	while(MSGO_I<MSGOLEN) {
		if (TESTLO(SSTAT0, SPIORDY))
			return;

		if (MSGO_I==MSGOLEN-1) {
			/* Leave MESSAGE OUT after transfer */
			SETPORT(SSTAT1, CLRATNO);
		}


		if (MSGO(MSGO_I) & IDENTIFY_BASE)
			CURRENT_SC->SCp.phase |= identified;

		if (MSGO(MSGO_I)==ABORT)
			CURRENT_SC->SCp.phase |= aborted;

		if (MSGO(MSGO_I)==BUS_DEVICE_RESET)
			CURRENT_SC->SCp.phase |= resetted;

		SETPORT(SCSIDAT, MSGO(MSGO_I++));
	}
}

static void msgo_end(struct Scsi_Host *shpnt)
{
	if(MSGO_I<MSGOLEN) {
		scmd_printk(KERN_ERR, CURRENT_SC,
			    "message sent incompletely (%d/%d)\n",
			    MSGO_I, MSGOLEN);
		if(SYNCNEG==1) {
			scmd_printk(KERN_INFO, CURRENT_SC,
				    "Synchronous Data Transfer Request was rejected\n");
			SYNCNEG=2;
		}
	}

	MSGO_I  = 0;
	MSGOLEN = 0;
}

/*
 * command phase
 *
 */
static void cmd_init(struct Scsi_Host *shpnt)
{
	if (CURRENT_SC->SCp.sent_command) {
		scmd_printk(KERN_ERR, CURRENT_SC,
			    "command already sent\n");
		done(shpnt, DID_ERROR << 16);
		return;
	}

	CMD_I=0;
}

/*
 * command phase
 *
 */
static void cmd_run(struct Scsi_Host *shpnt)
{
	while(CMD_I<CURRENT_SC->cmd_len) {
		if (TESTLO(SSTAT0, SPIORDY))
			return;

		SETPORT(SCSIDAT, CURRENT_SC->cmnd[CMD_I++]);
	}
}

static void cmd_end(struct Scsi_Host *shpnt)
{
	if(CMD_I<CURRENT_SC->cmd_len)
		scmd_printk(KERN_ERR, CURRENT_SC,
			    "command sent incompletely (%d/%d)\n",
			    CMD_I, CURRENT_SC->cmd_len);
	else
		CURRENT_SC->SCp.sent_command++;
}

/*
 * status phase
 *
 */
static void status_run(struct Scsi_Host *shpnt)
{
	if (TESTLO(SSTAT0, SPIORDY))
		return;

	CURRENT_SC->SCp.Status = GETPORT(SCSIDAT);

}

/*
 * data in phase
 *
 */
static void datai_init(struct Scsi_Host *shpnt)
{
	SETPORT(DMACNTRL0, RSTFIFO);
	SETPORT(DMACNTRL0, RSTFIFO|ENDMA);

	SETPORT(SXFRCTL0, CH1|CLRSTCNT);
	SETPORT(SXFRCTL0, CH1|SCSIEN|DMAEN);

	SETPORT(SIMODE0, 0);
	SETPORT(SIMODE1, ENSCSIPERR | ENSCSIRST | ENPHASEMIS | ENBUSFREE);

	DATA_LEN=0;
}

static void datai_run(struct Scsi_Host *shpnt)
{
	unsigned long the_time;
	int fifodata, data_count;

	/*
	 * loop while the phase persists or the fifos are not empty
	 *
	 */
	while(TESTLO(DMASTAT, INTSTAT) || TESTLO(DMASTAT, DFIFOEMP) || TESTLO(SSTAT2, SEMPTY)) {
		/* FIXME: maybe this should be done by setting up
		 * STCNT to trigger ENSWRAP interrupt, instead of
		 * polling for DFIFOFULL
		 */
		the_time=jiffies + 100*HZ;
		while(TESTLO(DMASTAT, DFIFOFULL|INTSTAT) && time_before(jiffies,the_time))
			barrier();

		if(TESTLO(DMASTAT, DFIFOFULL|INTSTAT)) {
			scmd_printk(KERN_ERR, CURRENT_SC, "datai timeout\n");
			break;
		}

		if(TESTHI(DMASTAT, DFIFOFULL)) {
			fifodata = 128;
		} else {
			the_time=jiffies + 100*HZ;
			while(TESTLO(SSTAT2, SEMPTY) && time_before(jiffies,the_time))
				barrier();

			if(TESTLO(SSTAT2, SEMPTY)) {
				scmd_printk(KERN_ERR, CURRENT_SC,
					    "datai sempty timeout");
				break;
			}

			fifodata = GETPORT(FIFOSTAT);
		}

		if(CURRENT_SC->SCp.this_residual>0) {
			while(fifodata>0 && CURRENT_SC->SCp.this_residual>0) {
				data_count = fifodata > CURRENT_SC->SCp.this_residual ?
						CURRENT_SC->SCp.this_residual :
						fifodata;
				fifodata -= data_count;

				if (data_count & 1) {
					SETPORT(DMACNTRL0, ENDMA|_8BIT);
					*CURRENT_SC->SCp.ptr++ = GETPORT(DATAPORT);
					CURRENT_SC->SCp.this_residual--;
					DATA_LEN++;
					SETPORT(DMACNTRL0, ENDMA);
				}

				if (data_count > 1) {
					data_count >>= 1;
					insw(DATAPORT, CURRENT_SC->SCp.ptr, data_count);
					CURRENT_SC->SCp.ptr += 2 * data_count;
					CURRENT_SC->SCp.this_residual -= 2 * data_count;
					DATA_LEN += 2 * data_count;
				}

				if (CURRENT_SC->SCp.this_residual == 0 &&
				    CURRENT_SC->SCp.buffers_residual > 0) {
					/* advance to next buffer */
					CURRENT_SC->SCp.buffers_residual--;
					CURRENT_SC->SCp.buffer++;
					CURRENT_SC->SCp.ptr           = SG_ADDRESS(CURRENT_SC->SCp.buffer);
					CURRENT_SC->SCp.this_residual = CURRENT_SC->SCp.buffer->length;
				}
			}
		} else if (fifodata > 0) {
			scmd_printk(KERN_ERR, CURRENT_SC,
				    "no buffers left for %d(%d) bytes"
				    " (data overrun!?)\n",
				    fifodata, GETPORT(FIFOSTAT));
			SETPORT(DMACNTRL0, ENDMA|_8BIT);
			while(fifodata>0) {
				int data;
				data=GETPORT(DATAPORT);
				fifodata--;
				DATA_LEN++;
			}
			SETPORT(DMACNTRL0, ENDMA|_8BIT);
		}
	}

	if(TESTLO(DMASTAT, INTSTAT) ||
	   TESTLO(DMASTAT, DFIFOEMP) ||
	   TESTLO(SSTAT2, SEMPTY) ||
	   GETPORT(FIFOSTAT)>0) {
		/*
		 * something went wrong, if there's something left in the fifos
		 * or the phase didn't change
		 */
		scmd_printk(KERN_ERR, CURRENT_SC,
			    "fifos should be empty and phase should have changed\n");
	}

	if(DATA_LEN!=GETSTCNT()) {
		scmd_printk(KERN_ERR, CURRENT_SC,
			    "manual transfer count differs from automatic "
			    "(count=%d;stcnt=%d;diff=%d;fifostat=%d)",
			    DATA_LEN, GETSTCNT(), GETSTCNT()-DATA_LEN,
			    GETPORT(FIFOSTAT));
		mdelay(10000);
	}
}

static void datai_end(struct Scsi_Host *shpnt)
{
	CMD_INC_RESID(CURRENT_SC, -GETSTCNT());

	SETPORT(SXFRCTL0, CH1|CLRSTCNT);
	SETPORT(DMACNTRL0, 0);
}

/*
 * data out phase
 *
 */
static void datao_init(struct Scsi_Host *shpnt)
{
	SETPORT(DMACNTRL0, WRITE_READ | RSTFIFO);
	SETPORT(DMACNTRL0, WRITE_READ | ENDMA);

	SETPORT(SXFRCTL0, CH1|CLRSTCNT);
	SETPORT(SXFRCTL0, CH1|SCSIEN|DMAEN);

	SETPORT(SIMODE0, 0);
	SETPORT(SIMODE1, ENSCSIPERR | ENSCSIRST | ENPHASEMIS | ENBUSFREE );

	DATA_LEN = scsi_get_resid(CURRENT_SC);
}

static void datao_run(struct Scsi_Host *shpnt)
{
	unsigned long the_time;
	int data_count;

	/* until phase changes or all data sent */
	while(TESTLO(DMASTAT, INTSTAT) && CURRENT_SC->SCp.this_residual>0) {
		data_count = 128;
		if(data_count > CURRENT_SC->SCp.this_residual)
			data_count=CURRENT_SC->SCp.this_residual;

		if(TESTLO(DMASTAT, DFIFOEMP)) {
			scmd_printk(KERN_ERR, CURRENT_SC,
				    "datao fifo not empty (%d)",
				    GETPORT(FIFOSTAT));
			break;
		}

		if(data_count & 1) {
			SETPORT(DMACNTRL0,WRITE_READ|ENDMA|_8BIT);
			SETPORT(DATAPORT, *CURRENT_SC->SCp.ptr++);
			CURRENT_SC->SCp.this_residual--;
			CMD_INC_RESID(CURRENT_SC, -1);
			SETPORT(DMACNTRL0,WRITE_READ|ENDMA);
		}

		if(data_count > 1) {
			data_count >>= 1;
			outsw(DATAPORT, CURRENT_SC->SCp.ptr, data_count);
			CURRENT_SC->SCp.ptr           += 2 * data_count;
			CURRENT_SC->SCp.this_residual -= 2 * data_count;
			CMD_INC_RESID(CURRENT_SC, -2 * data_count);
		}

		if(CURRENT_SC->SCp.this_residual==0 && CURRENT_SC->SCp.buffers_residual>0) {
			/* advance to next buffer */
			CURRENT_SC->SCp.buffers_residual--;
			CURRENT_SC->SCp.buffer++;
			CURRENT_SC->SCp.ptr           = SG_ADDRESS(CURRENT_SC->SCp.buffer);
			CURRENT_SC->SCp.this_residual = CURRENT_SC->SCp.buffer->length;
		}

		the_time=jiffies + 100*HZ;
		while(TESTLO(DMASTAT, DFIFOEMP|INTSTAT) && time_before(jiffies,the_time))
			barrier();

		if(TESTLO(DMASTAT, DFIFOEMP|INTSTAT)) {
			scmd_printk(KERN_ERR, CURRENT_SC, "dataout timeout\n");
			break;
		}
	}
}

static void datao_end(struct Scsi_Host *shpnt)
{
	if(TESTLO(DMASTAT, DFIFOEMP)) {
		int data_count = (DATA_LEN - scsi_get_resid(CURRENT_SC)) -
			GETSTCNT();

		CMD_INC_RESID(CURRENT_SC, data_count);

		data_count -= CURRENT_SC->SCp.ptr -
			SG_ADDRESS(CURRENT_SC->SCp.buffer);
		while(data_count>0) {
			CURRENT_SC->SCp.buffer--;
			CURRENT_SC->SCp.buffers_residual++;
			data_count -= CURRENT_SC->SCp.buffer->length;
		}
		CURRENT_SC->SCp.ptr = SG_ADDRESS(CURRENT_SC->SCp.buffer) -
			data_count;
		CURRENT_SC->SCp.this_residual = CURRENT_SC->SCp.buffer->length +
			data_count;
	}

	SETPORT(SXFRCTL0, CH1|CLRCH1|CLRSTCNT);
	SETPORT(SXFRCTL0, CH1);

	SETPORT(DMACNTRL0, 0);
}

/*
 * figure out what state we're in
 *
 */
static int update_state(struct Scsi_Host *shpnt)
{
	int dataphase=0;
	unsigned int stat0 = GETPORT(SSTAT0);
	unsigned int stat1 = GETPORT(SSTAT1);

	PREVSTATE = STATE;
	STATE=unknown;

	if(stat1 & SCSIRSTI) {
		STATE=rsti;
		SETPORT(SCSISEQ,0);
		SETPORT(SSTAT1,SCSIRSTI);
	} else if (stat0 & SELDI && PREVSTATE == busfree) {
		STATE=seldi;
	} else if(stat0 & SELDO && CURRENT_SC && (CURRENT_SC->SCp.phase & selecting)) {
		STATE=seldo;
	} else if(stat1 & SELTO) {
		STATE=selto;
	} else if(stat1 & BUSFREE) {
		STATE=busfree;
		SETPORT(SSTAT1,BUSFREE);
	} else if(stat1 & SCSIPERR) {
		STATE=parerr;
		SETPORT(SSTAT1,SCSIPERR);
	} else if(stat1 & REQINIT) {
		switch(GETPORT(SCSISIG) & P_MASK) {
		case P_MSGI:	STATE=msgi;	break;
		case P_MSGO:	STATE=msgo;	break;
		case P_DATAO:	STATE=datao;	break;
		case P_DATAI:	STATE=datai;	break;
		case P_STATUS:	STATE=status;	break;
		case P_CMD:	STATE=cmd;	break;
		}
		dataphase=1;
	}

	if((stat0 & SELDI) && STATE!=seldi && !dataphase) {
		scmd_printk(KERN_INFO, CURRENT_SC, "reselection missed?");
	}

	if(STATE!=PREVSTATE) {
		LASTSTATE=PREVSTATE;
	}

	return dataphase;
}

/*
 * handle parity error
 *
 * FIXME: in which phase?
 *
 */
static void parerr_run(struct Scsi_Host *shpnt)
{
	scmd_printk(KERN_ERR, CURRENT_SC, "parity error\n");
	done(shpnt, DID_PARITY << 16);
}

/*
 * handle reset in
 *
 */
static void rsti_run(struct Scsi_Host *shpnt)
{
	Scsi_Cmnd *ptr;

	shost_printk(KERN_NOTICE, shpnt, "scsi reset in\n");

	ptr=DISCONNECTED_SC;
	while(ptr) {
		Scsi_Cmnd *next = SCNEXT(ptr);

		if (!ptr->device->soft_reset) {
			remove_SC(&DISCONNECTED_SC, ptr);

			kfree(ptr->host_scribble);
			ptr->host_scribble=NULL;

			ptr->result =  DID_RESET << 16;
			ptr->scsi_done(ptr);
		}

		ptr = next;
	}

	if(CURRENT_SC && !CURRENT_SC->device->soft_reset)
		done(shpnt, DID_RESET << 16 );
}


/*
 * bottom-half handler
 *
 */
static void is_complete(struct Scsi_Host *shpnt)
{
	int dataphase;
	unsigned long flags;
	int pending;

	if(!shpnt)
		return;

	DO_LOCK(flags);

	if( HOSTDATA(shpnt)->service==0 )  {
		DO_UNLOCK(flags);
		return;
	}

	HOSTDATA(shpnt)->service = 0;

	if(HOSTDATA(shpnt)->in_intr) {
		DO_UNLOCK(flags);
		/* aha152x_error never returns.. */
		aha152x_error(shpnt, "bottom-half already running!?");
	}
	HOSTDATA(shpnt)->in_intr++;

	/*
	 * loop while there are interrupt conditions pending
	 *
	 */
	do {
		unsigned long start = jiffies;
		DO_UNLOCK(flags);

		dataphase=update_state(shpnt);

		/*
		 * end previous state
		 *
		 */
		if(PREVSTATE!=STATE && states[PREVSTATE].end)
			states[PREVSTATE].end(shpnt);

		/*
		 * disable SPIO mode if previous phase used it
		 * and this one doesn't
		 *
		 */
		if(states[PREVSTATE].spio && !states[STATE].spio) {
			SETPORT(SXFRCTL0, CH1);
			SETPORT(DMACNTRL0, 0);
			if(CURRENT_SC)
				CURRENT_SC->SCp.phase &= ~spiordy;
		}

		/*
		 * accept current dataphase phase
		 *
		 */
		if(dataphase) {
			SETPORT(SSTAT0, REQINIT);
			SETPORT(SCSISIG, GETPORT(SCSISIG) & P_MASK);
			SETPORT(SSTAT1, PHASECHG);
		}

		/*
		 * enable SPIO mode if previous didn't use it
		 * and this one does
		 *
		 */
		if(!states[PREVSTATE].spio && states[STATE].spio) {
			SETPORT(DMACNTRL0, 0);
			SETPORT(SXFRCTL0, CH1|SPIOEN);
			if(CURRENT_SC)
				CURRENT_SC->SCp.phase |= spiordy;
		}

		/*
		 * initialize for new state
		 *
		 */
		if(PREVSTATE!=STATE && states[STATE].init)
			states[STATE].init(shpnt);

		/*
		 * handle current state
		 *
		 */
		if(states[STATE].run)
			states[STATE].run(shpnt);
		else
			scmd_printk(KERN_ERR, CURRENT_SC,
				    "unexpected state (%x)\n", STATE);

		/*
		 * setup controller to interrupt on
		 * the next expected condition and
		 * loop if it's already there
		 *
		 */
		DO_LOCK(flags);
		pending=setup_expected_interrupts(shpnt);
#if defined(AHA152X_STAT)
		HOSTDATA(shpnt)->count[STATE]++;
		if(PREVSTATE!=STATE)
			HOSTDATA(shpnt)->count_trans[STATE]++;
		HOSTDATA(shpnt)->time[STATE] += jiffies-start;
#endif

	} while(pending);

	/*
	 * enable interrupts and leave bottom-half
	 *
	 */
	HOSTDATA(shpnt)->in_intr--;
	SETBITS(DMACNTRL0, INTEN);
	DO_UNLOCK(flags);
}


/*
 * Dump the current driver status and panic
 */
static void aha152x_error(struct Scsi_Host *shpnt, char *msg)
{
	shost_printk(KERN_EMERG, shpnt, "%s\n", msg);
	show_queues(shpnt);
	panic("aha152x panic\n");
}

/*
 * display enabled interrupts
 */
static void disp_enintr(struct Scsi_Host *shpnt)
{
	int s0, s1;

	s0 = GETPORT(SIMODE0);
	s1 = GETPORT(SIMODE1);

	shost_printk(KERN_DEBUG, shpnt,
		     "enabled interrupts (%s%s%s%s%s%s%s%s%s%s%s%s%s%s)\n",
		     (s0 & ENSELDO) ? "ENSELDO " : "",
		     (s0 & ENSELDI) ? "ENSELDI " : "",
		     (s0 & ENSELINGO) ? "ENSELINGO " : "",
		     (s0 & ENSWRAP) ? "ENSWRAP " : "",
		     (s0 & ENSDONE) ? "ENSDONE " : "",
		     (s0 & ENSPIORDY) ? "ENSPIORDY " : "",
		     (s0 & ENDMADONE) ? "ENDMADONE " : "",
		     (s1 & ENSELTIMO) ? "ENSELTIMO " : "",
		     (s1 & ENATNTARG) ? "ENATNTARG " : "",
		     (s1 & ENPHASEMIS) ? "ENPHASEMIS " : "",
		     (s1 & ENBUSFREE) ? "ENBUSFREE " : "",
		     (s1 & ENSCSIPERR) ? "ENSCSIPERR " : "",
		     (s1 & ENPHASECHG) ? "ENPHASECHG " : "",
		     (s1 & ENREQINIT) ? "ENREQINIT " : "");
}

/*
 * Show the command data of a command
 */
static void show_command(Scsi_Cmnd *ptr)
{
	scsi_print_command(ptr);
	scmd_printk(KERN_DEBUG, ptr,
		    "request_bufflen=%d; resid=%d; "
		    "phase |%s%s%s%s%s%s%s%s%s; next=0x%p",
		    scsi_bufflen(ptr), scsi_get_resid(ptr),
		    (ptr->SCp.phase & not_issued) ? "not issued|" : "",
		    (ptr->SCp.phase & selecting) ? "selecting|" : "",
		    (ptr->SCp.phase & identified) ? "identified|" : "",
		    (ptr->SCp.phase & disconnected) ? "disconnected|" : "",
		    (ptr->SCp.phase & completed) ? "completed|" : "",
		    (ptr->SCp.phase & spiordy) ? "spiordy|" : "",
		    (ptr->SCp.phase & syncneg) ? "syncneg|" : "",
		    (ptr->SCp.phase & aborted) ? "aborted|" : "",
		    (ptr->SCp.phase & resetted) ? "resetted|" : "",
		    (SCDATA(ptr)) ? SCNEXT(ptr) : NULL);
}

/*
 * Dump the queued data
 */
static void show_queues(struct Scsi_Host *shpnt)
{
	Scsi_Cmnd *ptr;
	unsigned long flags;

	DO_LOCK(flags);
	printk(KERN_DEBUG "\nqueue status:\nissue_SC:\n");
	for (ptr = ISSUE_SC; ptr; ptr = SCNEXT(ptr))
		show_command(ptr);
	DO_UNLOCK(flags);

	printk(KERN_DEBUG "current_SC:\n");
	if (CURRENT_SC)
		show_command(CURRENT_SC);
	else
		printk(KERN_DEBUG "none\n");

	printk(KERN_DEBUG "disconnected_SC:\n");
	for (ptr = DISCONNECTED_SC; ptr; ptr = SCDATA(ptr) ? SCNEXT(ptr) : NULL)
		show_command(ptr);

	disp_enintr(shpnt);
}

static void get_command(struct seq_file *m, Scsi_Cmnd * ptr)
{
	int i;

	seq_printf(m, "%p: target=%d; lun=%d; cmnd=( ",
		ptr, ptr->device->id, (u8)ptr->device->lun);

	for (i = 0; i < COMMAND_SIZE(ptr->cmnd[0]); i++)
		seq_printf(m, "0x%02x ", ptr->cmnd[i]);

	seq_printf(m, "); resid=%d; residual=%d; buffers=%d; phase |",
		scsi_get_resid(ptr), ptr->SCp.this_residual,
		ptr->SCp.buffers_residual);

	if (ptr->SCp.phase & not_issued)
		seq_puts(m, "not issued|");
	if (ptr->SCp.phase & selecting)
		seq_puts(m, "selecting|");
	if (ptr->SCp.phase & disconnected)
		seq_puts(m, "disconnected|");
	if (ptr->SCp.phase & aborted)
		seq_puts(m, "aborted|");
	if (ptr->SCp.phase & identified)
		seq_puts(m, "identified|");
	if (ptr->SCp.phase & completed)
		seq_puts(m, "completed|");
	if (ptr->SCp.phase & spiordy)
		seq_puts(m, "spiordy|");
	if (ptr->SCp.phase & syncneg)
		seq_puts(m, "syncneg|");
	seq_printf(m, "; next=0x%p\n", SCNEXT(ptr));
}

static void get_ports(struct seq_file *m, struct Scsi_Host *shpnt)
{
	int s;

	seq_printf(m, "\n%s: %s(%s) ", CURRENT_SC ? "on bus" : "waiting", states[STATE].name, states[PREVSTATE].name);

	s = GETPORT(SCSISEQ);
	seq_puts(m, "SCSISEQ( ");
	if (s & TEMODEO)
		seq_puts(m, "TARGET MODE ");
	if (s & ENSELO)
		seq_puts(m, "SELO ");
	if (s & ENSELI)
		seq_puts(m, "SELI ");
	if (s & ENRESELI)
		seq_puts(m, "RESELI ");
	if (s & ENAUTOATNO)
		seq_puts(m, "AUTOATNO ");
	if (s & ENAUTOATNI)
		seq_puts(m, "AUTOATNI ");
	if (s & ENAUTOATNP)
		seq_puts(m, "AUTOATNP ");
	if (s & SCSIRSTO)
		seq_puts(m, "SCSIRSTO ");
	seq_puts(m, ");");

	seq_puts(m, " SCSISIG(");
	s = GETPORT(SCSISIG);
	switch (s & P_MASK) {
	case P_DATAO:
		seq_puts(m, "DATA OUT");
		break;
	case P_DATAI:
		seq_puts(m, "DATA IN");
		break;
	case P_CMD:
		seq_puts(m, "COMMAND");
		break;
	case P_STATUS:
		seq_puts(m, "STATUS");
		break;
	case P_MSGO:
		seq_puts(m, "MESSAGE OUT");
		break;
	case P_MSGI:
		seq_puts(m, "MESSAGE IN");
		break;
	default:
		seq_puts(m, "*invalid*");
		break;
	}

	seq_puts(m, "); ");

	seq_printf(m, "INTSTAT (%s); ", TESTHI(DMASTAT, INTSTAT) ? "hi" : "lo");

	seq_puts(m, "SSTAT( ");
	s = GETPORT(SSTAT0);
	if (s & TARGET)
		seq_puts(m, "TARGET ");
	if (s & SELDO)
		seq_puts(m, "SELDO ");
	if (s & SELDI)
		seq_puts(m, "SELDI ");
	if (s & SELINGO)
		seq_puts(m, "SELINGO ");
	if (s & SWRAP)
		seq_puts(m, "SWRAP ");
	if (s & SDONE)
		seq_puts(m, "SDONE ");
	if (s & SPIORDY)
		seq_puts(m, "SPIORDY ");
	if (s & DMADONE)
		seq_puts(m, "DMADONE ");

	s = GETPORT(SSTAT1);
	if (s & SELTO)
		seq_puts(m, "SELTO ");
	if (s & ATNTARG)
		seq_puts(m, "ATNTARG ");
	if (s & SCSIRSTI)
		seq_puts(m, "SCSIRSTI ");
	if (s & PHASEMIS)
		seq_puts(m, "PHASEMIS ");
	if (s & BUSFREE)
		seq_puts(m, "BUSFREE ");
	if (s & SCSIPERR)
		seq_puts(m, "SCSIPERR ");
	if (s & PHASECHG)
		seq_puts(m, "PHASECHG ");
	if (s & REQINIT)
		seq_puts(m, "REQINIT ");
	seq_puts(m, "); ");


	seq_puts(m, "SSTAT( ");

	s = GETPORT(SSTAT0) & GETPORT(SIMODE0);

	if (s & TARGET)
		seq_puts(m, "TARGET ");
	if (s & SELDO)
		seq_puts(m, "SELDO ");
	if (s & SELDI)
		seq_puts(m, "SELDI ");
	if (s & SELINGO)
		seq_puts(m, "SELINGO ");
	if (s & SWRAP)
		seq_puts(m, "SWRAP ");
	if (s & SDONE)
		seq_puts(m, "SDONE ");
	if (s & SPIORDY)
		seq_puts(m, "SPIORDY ");
	if (s & DMADONE)
		seq_puts(m, "DMADONE ");

	s = GETPORT(SSTAT1) & GETPORT(SIMODE1);

	if (s & SELTO)
		seq_puts(m, "SELTO ");
	if (s & ATNTARG)
		seq_puts(m, "ATNTARG ");
	if (s & SCSIRSTI)
		seq_puts(m, "SCSIRSTI ");
	if (s & PHASEMIS)
		seq_puts(m, "PHASEMIS ");
	if (s & BUSFREE)
		seq_puts(m, "BUSFREE ");
	if (s & SCSIPERR)
		seq_puts(m, "SCSIPERR ");
	if (s & PHASECHG)
		seq_puts(m, "PHASECHG ");
	if (s & REQINIT)
		seq_puts(m, "REQINIT ");
	seq_puts(m, "); ");

	seq_puts(m, "SXFRCTL0( ");

	s = GETPORT(SXFRCTL0);
	if (s & SCSIEN)
		seq_puts(m, "SCSIEN ");
	if (s & DMAEN)
		seq_puts(m, "DMAEN ");
	if (s & CH1)
		seq_puts(m, "CH1 ");
	if (s & CLRSTCNT)
		seq_puts(m, "CLRSTCNT ");
	if (s & SPIOEN)
		seq_puts(m, "SPIOEN ");
	if (s & CLRCH1)
		seq_puts(m, "CLRCH1 ");
	seq_puts(m, "); ");

	seq_puts(m, "SIGNAL( ");

	s = GETPORT(SCSISIG);
	if (s & SIG_ATNI)
		seq_puts(m, "ATNI ");
	if (s & SIG_SELI)
		seq_puts(m, "SELI ");
	if (s & SIG_BSYI)
		seq_puts(m, "BSYI ");
	if (s & SIG_REQI)
		seq_puts(m, "REQI ");
	if (s & SIG_ACKI)
		seq_puts(m, "ACKI ");
	seq_puts(m, "); ");

	seq_printf(m, "SELID(%02x), ", GETPORT(SELID));

	seq_printf(m, "STCNT(%d), ", GETSTCNT());

	seq_puts(m, "SSTAT2( ");

	s = GETPORT(SSTAT2);
	if (s & SOFFSET)
		seq_puts(m, "SOFFSET ");
	if (s & SEMPTY)
		seq_puts(m, "SEMPTY ");
	if (s & SFULL)
		seq_puts(m, "SFULL ");
	seq_printf(m, "); SFCNT (%d); ", s & (SFULL | SFCNT));

	s = GETPORT(SSTAT3);
	seq_printf(m, "SCSICNT (%d), OFFCNT(%d), ", (s & 0xf0) >> 4, s & 0x0f);

	seq_puts(m, "SSTAT4( ");
	s = GETPORT(SSTAT4);
	if (s & SYNCERR)
		seq_puts(m, "SYNCERR ");
	if (s & FWERR)
		seq_puts(m, "FWERR ");
	if (s & FRERR)
		seq_puts(m, "FRERR ");
	seq_puts(m, "); ");

	seq_puts(m, "DMACNTRL0( ");
	s = GETPORT(DMACNTRL0);
	seq_printf(m, "%s ", s & _8BIT ? "8BIT" : "16BIT");
	seq_printf(m, "%s ", s & DMA ? "DMA" : "PIO");
	seq_printf(m, "%s ", s & WRITE_READ ? "WRITE" : "READ");
	if (s & ENDMA)
		seq_puts(m, "ENDMA ");
	if (s & INTEN)
		seq_puts(m, "INTEN ");
	if (s & RSTFIFO)
		seq_puts(m, "RSTFIFO ");
	if (s & SWINT)
		seq_puts(m, "SWINT ");
	seq_puts(m, "); ");

	seq_puts(m, "DMASTAT( ");
	s = GETPORT(DMASTAT);
	if (s & ATDONE)
		seq_puts(m, "ATDONE ");
	if (s & WORDRDY)
		seq_puts(m, "WORDRDY ");
	if (s & DFIFOFULL)
		seq_puts(m, "DFIFOFULL ");
	if (s & DFIFOEMP)
		seq_puts(m, "DFIFOEMP ");
	seq_puts(m, ")\n");

	seq_puts(m, "enabled interrupts( ");

	s = GETPORT(SIMODE0);
	if (s & ENSELDO)
		seq_puts(m, "ENSELDO ");
	if (s & ENSELDI)
		seq_puts(m, "ENSELDI ");
	if (s & ENSELINGO)
		seq_puts(m, "ENSELINGO ");
	if (s & ENSWRAP)
		seq_puts(m, "ENSWRAP ");
	if (s & ENSDONE)
		seq_puts(m, "ENSDONE ");
	if (s & ENSPIORDY)
		seq_puts(m, "ENSPIORDY ");
	if (s & ENDMADONE)
		seq_puts(m, "ENDMADONE ");

	s = GETPORT(SIMODE1);
	if (s & ENSELTIMO)
		seq_puts(m, "ENSELTIMO ");
	if (s & ENATNTARG)
		seq_puts(m, "ENATNTARG ");
	if (s & ENPHASEMIS)
		seq_puts(m, "ENPHASEMIS ");
	if (s & ENBUSFREE)
		seq_puts(m, "ENBUSFREE ");
	if (s & ENSCSIPERR)
		seq_puts(m, "ENSCSIPERR ");
	if (s & ENPHASECHG)
		seq_puts(m, "ENPHASECHG ");
	if (s & ENREQINIT)
		seq_puts(m, "ENREQINIT ");
	seq_puts(m, ")\n");
}

static int aha152x_set_info(struct Scsi_Host *shpnt, char *buffer, int length)
{
	if(!shpnt || !buffer || length<8 || strncmp("aha152x ", buffer, 8)!=0)
		return -EINVAL;

#if defined(AHA152X_STAT)
	if(length>13 && strncmp("reset", buffer+8, 5)==0) {
		int i;

		HOSTDATA(shpnt)->total_commands=0;
		HOSTDATA(shpnt)->disconnections=0;
		HOSTDATA(shpnt)->busfree_without_any_action=0;
		HOSTDATA(shpnt)->busfree_without_old_command=0;
		HOSTDATA(shpnt)->busfree_without_new_command=0;
		HOSTDATA(shpnt)->busfree_without_done_command=0;
		HOSTDATA(shpnt)->busfree_with_check_condition=0;
		for (i = idle; i<maxstate; i++) {
			HOSTDATA(shpnt)->count[i]=0;
			HOSTDATA(shpnt)->count_trans[i]=0;
			HOSTDATA(shpnt)->time[i]=0;
		}

		shost_printk(KERN_INFO, shpnt, "aha152x: stats reset.\n");

	} else
#endif
	{
		return -EINVAL;
	}


	return length;
}

static int aha152x_show_info(struct seq_file *m, struct Scsi_Host *shpnt)
{
	int i;
	Scsi_Cmnd *ptr;
	unsigned long flags;

	seq_puts(m, AHA152X_REVID "\n");

	seq_printf(m, "ioports 0x%04lx to 0x%04lx\n",
		shpnt->io_port, shpnt->io_port + shpnt->n_io_port - 1);
	seq_printf(m, "interrupt 0x%02x\n", shpnt->irq);
	seq_printf(m, "disconnection/reconnection %s\n",
		RECONNECT ? "enabled" : "disabled");
	seq_printf(m, "parity checking %s\n",
		PARITY ? "enabled" : "disabled");
	seq_printf(m, "synchronous transfers %s\n",
		SYNCHRONOUS ? "enabled" : "disabled");
	seq_printf(m, "%d commands currently queued\n", HOSTDATA(shpnt)->commands);

	if(SYNCHRONOUS) {
		seq_puts(m, "synchronously operating targets (tick=50 ns):\n");
		for (i = 0; i < 8; i++)
			if (HOSTDATA(shpnt)->syncrate[i] & 0x7f)
				seq_printf(m, "target %d: period %dT/%dns; req/ack offset %d\n",
					i,
					(((HOSTDATA(shpnt)->syncrate[i] & 0x70) >> 4) + 2),
					(((HOSTDATA(shpnt)->syncrate[i] & 0x70) >> 4) + 2) * 50,
				    HOSTDATA(shpnt)->syncrate[i] & 0x0f);
	}
	seq_puts(m, "\nqueue status:\n");
	DO_LOCK(flags);
	if (ISSUE_SC) {
		seq_puts(m, "not yet issued commands:\n");
		for (ptr = ISSUE_SC; ptr; ptr = SCNEXT(ptr))
			get_command(m, ptr);
	} else
		seq_puts(m, "no not yet issued commands\n");
	DO_UNLOCK(flags);

	if (CURRENT_SC) {
		seq_puts(m, "current command:\n");
		get_command(m, CURRENT_SC);
	} else
		seq_puts(m, "no current command\n");

	if (DISCONNECTED_SC) {
		seq_puts(m, "disconnected commands:\n");
		for (ptr = DISCONNECTED_SC; ptr; ptr = SCNEXT(ptr))
			get_command(m, ptr);
	} else
		seq_puts(m, "no disconnected commands\n");

	get_ports(m, shpnt);

#if defined(AHA152X_STAT)
	seq_printf(m, "statistics:\n"
		"total commands:               %d\n"
		"disconnections:               %d\n"
		"busfree with check condition: %d\n"
		"busfree without old command:  %d\n"
		"busfree without new command:  %d\n"
		"busfree without done command: %d\n"
		"busfree without any action:   %d\n"
		"state      "
		"transitions  "
		"count        "
		"time\n",
		HOSTDATA(shpnt)->total_commands,
		HOSTDATA(shpnt)->disconnections,
		HOSTDATA(shpnt)->busfree_with_check_condition,
		HOSTDATA(shpnt)->busfree_without_old_command,
		HOSTDATA(shpnt)->busfree_without_new_command,
		HOSTDATA(shpnt)->busfree_without_done_command,
		HOSTDATA(shpnt)->busfree_without_any_action);
	for(i=0; i<maxstate; i++) {
		seq_printf(m, "%-10s %-12d %-12d %-12ld\n",
			states[i].name,
			HOSTDATA(shpnt)->count_trans[i],
			HOSTDATA(shpnt)->count[i],
			HOSTDATA(shpnt)->time[i]);
	}
#endif
	return 0;
}

static int aha152x_adjust_queue(struct scsi_device *device)
{
	blk_queue_bounce_limit(device->request_queue, BLK_BOUNCE_HIGH);
	return 0;
}

static struct scsi_host_template aha152x_driver_template = {
	.module				= THIS_MODULE,
	.name				= AHA152X_REVID,
	.proc_name			= "aha152x",
	.show_info			= aha152x_show_info,
	.write_info			= aha152x_set_info,
	.queuecommand			= aha152x_queue,
	.eh_abort_handler		= aha152x_abort,
	.eh_device_reset_handler	= aha152x_device_reset,
	.eh_bus_reset_handler		= aha152x_bus_reset,
	.eh_host_reset_handler		= aha152x_host_reset,
	.bios_param			= aha152x_biosparam,
	.can_queue			= 1,
	.this_id			= 7,
	.sg_tablesize			= SG_ALL,
	.cmd_per_lun			= 1,
	.use_clustering			= DISABLE_CLUSTERING,
	.slave_alloc			= aha152x_adjust_queue,
};

#if !defined(PCMCIA)
static int setup_count;
static struct aha152x_setup setup[2];

/* possible i/o addresses for the AIC-6260; default first */
static unsigned short ports[] = { 0x340, 0x140 };

#if !defined(SKIP_BIOSTEST)
/* possible locations for the Adaptec BIOS; defaults first */
static unsigned int addresses[] =
{
	0xdc000,		/* default first */
	0xc8000,
	0xcc000,
	0xd0000,
	0xd4000,
	0xd8000,
	0xe0000,
	0xeb800,		/* VTech Platinum SMP */
	0xf0000,
};

/* signatures for various AIC-6[23]60 based controllers.
   The point in detecting signatures is to avoid useless and maybe
   harmful probes on ports. I'm not sure that all listed boards pass
   auto-configuration. For those which fail the BIOS signature is
   obsolete, because user intervention to supply the configuration is
   needed anyway.  May be an information whether or not the BIOS supports
   extended translation could be also useful here. */
static struct signature {
	unsigned char *signature;
	int sig_offset;
	int sig_length;
} signatures[] =
{
	{ "Adaptec AHA-1520 BIOS",	0x102e, 21 },
		/* Adaptec 152x */
	{ "Adaptec AHA-1520B",		0x000b, 17 },
		/* Adaptec 152x rev B */
	{ "Adaptec AHA-1520B",		0x0026, 17 },
		/* Iomega Jaz Jet ISA (AIC6370Q) */
	{ "Adaptec ASW-B626 BIOS",	0x1029, 21 },
		/* on-board controller */
	{ "Adaptec BIOS: ASW-B626",	0x000f, 22 },
		/* on-board controller */
	{ "Adaptec ASW-B626 S2",	0x2e6c, 19 },
		/* on-board controller */
	{ "Adaptec BIOS:AIC-6360",	0x000c, 21 },
		/* on-board controller */
	{ "ScsiPro SP-360 BIOS",	0x2873, 19 },
		/* ScsiPro-Controller  */
	{ "GA-400 LOCAL BUS SCSI BIOS", 0x102e, 26 },
		/* Gigabyte Local-Bus-SCSI */
	{ "Adaptec BIOS:AVA-282X",	0x000c, 21 },
		/* Adaptec 282x */
	{ "Adaptec IBM Dock II SCSI",   0x2edd, 24 },
		/* IBM Thinkpad Dock II */
	{ "Adaptec BIOS:AHA-1532P",     0x001c, 22 },
		/* IBM Thinkpad Dock II SCSI */
	{ "DTC3520A Host Adapter BIOS", 0x318a, 26 },
		/* DTC 3520A ISA SCSI */
};
#endif /* !SKIP_BIOSTEST */

/*
 * Test, if port_base is valid.
 *
 */
static int aha152x_porttest(int io_port)
{
	int i;

	SETPORT(io_port + O_DMACNTRL1, 0);	/* reset stack pointer */
	for (i = 0; i < 16; i++)
		SETPORT(io_port + O_STACK, i);

	SETPORT(io_port + O_DMACNTRL1, 0);	/* reset stack pointer */
	for (i = 0; i < 16 && GETPORT(io_port + O_STACK) == i; i++)
		;

	return (i == 16);
}

static int tc1550_porttest(int io_port)
{
	int i;

	SETPORT(io_port + O_TC_DMACNTRL1, 0);	/* reset stack pointer */
	for (i = 0; i < 16; i++)
		SETPORT(io_port + O_STACK, i);

	SETPORT(io_port + O_TC_DMACNTRL1, 0);	/* reset stack pointer */
	for (i = 0; i < 16 && GETPORT(io_port + O_TC_STACK) == i; i++)
		;

	return (i == 16);
}


static int checksetup(struct aha152x_setup *setup)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(ports) && (setup->io_port != ports[i]); i++)
		;

	if (i == ARRAY_SIZE(ports))
		return 0;

	if (!request_region(setup->io_port, IO_RANGE, "aha152x")) {
		printk(KERN_ERR "aha152x: io port 0x%x busy.\n", setup->io_port);
		return 0;
	}

	if( aha152x_porttest(setup->io_port) ) {
		setup->tc1550=0;
	} else if( tc1550_porttest(setup->io_port) ) {
		setup->tc1550=1;
	} else {
		release_region(setup->io_port, IO_RANGE);
		return 0;
	}

	release_region(setup->io_port, IO_RANGE);

	if ((setup->irq < IRQ_MIN) || (setup->irq > IRQ_MAX))
		return 0;

	if ((setup->scsiid < 0) || (setup->scsiid > 7))
		return 0;

	if ((setup->reconnect < 0) || (setup->reconnect > 1))
		return 0;

	if ((setup->parity < 0) || (setup->parity > 1))
		return 0;

	if ((setup->synchronous < 0) || (setup->synchronous > 1))
		return 0;

	if ((setup->ext_trans < 0) || (setup->ext_trans > 1))
		return 0;


	return 1;
}


static int __init aha152x_init(void)
{
	int i, j, ok;
#if defined(AUTOCONF)
	aha152x_config conf;
#endif
#ifdef __ISAPNP__
	struct pnp_dev *dev=NULL, *pnpdev[2] = {NULL, NULL};
#endif

	if ( setup_count ) {
		printk(KERN_INFO "aha152x: processing commandline: ");

		for (i = 0; i<setup_count; i++) {
			if (!checksetup(&setup[i])) {
				printk(KERN_ERR "\naha152x: %s\n", setup[i].conf);
				printk(KERN_ERR "aha152x: invalid line\n");
			}
		}
		printk("ok\n");
	}

#if defined(SETUP0)
	if (setup_count < ARRAY_SIZE(setup)) {
		struct aha152x_setup override = SETUP0;

		if (setup_count == 0 || (override.io_port != setup[0].io_port)) {
			if (!checksetup(&override)) {
				printk(KERN_ERR "\naha152x: invalid override SETUP0={0x%x,%d,%d,%d,%d,%d,%d,%d}\n",
				       override.io_port,
				       override.irq,
				       override.scsiid,
				       override.reconnect,
				       override.parity,
				       override.synchronous,
				       override.delay,
				       override.ext_trans);
			} else
				setup[setup_count++] = override;
		}
	}
#endif

#if defined(SETUP1)
	if (setup_count < ARRAY_SIZE(setup)) {
		struct aha152x_setup override = SETUP1;

		if (setup_count == 0 || (override.io_port != setup[0].io_port)) {
			if (!checksetup(&override)) {
				printk(KERN_ERR "\naha152x: invalid override SETUP1={0x%x,%d,%d,%d,%d,%d,%d,%d}\n",
				       override.io_port,
				       override.irq,
				       override.scsiid,
				       override.reconnect,
				       override.parity,
				       override.synchronous,
				       override.delay,
				       override.ext_trans);
			} else
				setup[setup_count++] = override;
		}
	}
#endif

#if defined(MODULE)
	if (setup_count<ARRAY_SIZE(setup) && (aha152x[0]!=0 || io[0]!=0 || irq[0]!=0)) {
		if(aha152x[0]!=0) {
			setup[setup_count].conf        = "";
			setup[setup_count].io_port     = aha152x[0];
			setup[setup_count].irq         = aha152x[1];
			setup[setup_count].scsiid      = aha152x[2];
			setup[setup_count].reconnect   = aha152x[3];
			setup[setup_count].parity      = aha152x[4];
			setup[setup_count].synchronous = aha152x[5];
			setup[setup_count].delay       = aha152x[6];
			setup[setup_count].ext_trans   = aha152x[7];
		} else if (io[0] != 0 || irq[0] != 0) {
			if(io[0]!=0)  setup[setup_count].io_port = io[0];
			if(irq[0]!=0) setup[setup_count].irq     = irq[0];

			setup[setup_count].scsiid      = scsiid[0];
			setup[setup_count].reconnect   = reconnect[0];
			setup[setup_count].parity      = parity[0];
			setup[setup_count].synchronous = sync[0];
			setup[setup_count].delay       = delay[0];
			setup[setup_count].ext_trans   = exttrans[0];
		}

		if (checksetup(&setup[setup_count]))
			setup_count++;
		else
			printk(KERN_ERR "aha152x: invalid module params io=0x%x, irq=%d,scsiid=%d,reconnect=%d,parity=%d,sync=%d,delay=%d,exttrans=%d\n",
			       setup[setup_count].io_port,
			       setup[setup_count].irq,
			       setup[setup_count].scsiid,
			       setup[setup_count].reconnect,
			       setup[setup_count].parity,
			       setup[setup_count].synchronous,
			       setup[setup_count].delay,
			       setup[setup_count].ext_trans);
	}

	if (setup_count<ARRAY_SIZE(setup) && (aha152x1[0]!=0 || io[1]!=0 || irq[1]!=0)) {
		if(aha152x1[0]!=0) {
			setup[setup_count].conf        = "";
			setup[setup_count].io_port     = aha152x1[0];
			setup[setup_count].irq         = aha152x1[1];
			setup[setup_count].scsiid      = aha152x1[2];
			setup[setup_count].reconnect   = aha152x1[3];
			setup[setup_count].parity      = aha152x1[4];
			setup[setup_count].synchronous = aha152x1[5];
			setup[setup_count].delay       = aha152x1[6];
			setup[setup_count].ext_trans   = aha152x1[7];
		} else if (io[1] != 0 || irq[1] != 0) {
			if(io[1]!=0)  setup[setup_count].io_port = io[1];
			if(irq[1]!=0) setup[setup_count].irq     = irq[1];

			setup[setup_count].scsiid      = scsiid[1];
			setup[setup_count].reconnect   = reconnect[1];
			setup[setup_count].parity      = parity[1];
			setup[setup_count].synchronous = sync[1];
			setup[setup_count].delay       = delay[1];
			setup[setup_count].ext_trans   = exttrans[1];
		}
		if (checksetup(&setup[setup_count]))
			setup_count++;
		else
			printk(KERN_ERR "aha152x: invalid module params io=0x%x, irq=%d,scsiid=%d,reconnect=%d,parity=%d,sync=%d,delay=%d,exttrans=%d\n",
			       setup[setup_count].io_port,
			       setup[setup_count].irq,
			       setup[setup_count].scsiid,
			       setup[setup_count].reconnect,
			       setup[setup_count].parity,
			       setup[setup_count].synchronous,
			       setup[setup_count].delay,
			       setup[setup_count].ext_trans);
	}
#endif

#ifdef __ISAPNP__
	for(i=0; setup_count<ARRAY_SIZE(setup) && id_table[i].vendor; i++) {
		while ( setup_count<ARRAY_SIZE(setup) &&
			(dev=pnp_find_dev(NULL, id_table[i].vendor, id_table[i].function, dev)) ) {
			if (pnp_device_attach(dev) < 0)
				continue;

			if (pnp_activate_dev(dev) < 0) {
				pnp_device_detach(dev);
				continue;
			}

			if (!pnp_port_valid(dev, 0)) {
				pnp_device_detach(dev);
				continue;
			}

			if (setup_count==1 && pnp_port_start(dev, 0)==setup[0].io_port) {
				pnp_device_detach(dev);
				continue;
			}

			setup[setup_count].io_port     = pnp_port_start(dev, 0);
			setup[setup_count].irq         = pnp_irq(dev, 0);
			setup[setup_count].scsiid      = 7;
			setup[setup_count].reconnect   = 1;
			setup[setup_count].parity      = 1;
			setup[setup_count].synchronous = 1;
			setup[setup_count].delay       = DELAY_DEFAULT;
			setup[setup_count].ext_trans   = 0;
#if defined(__ISAPNP__)
			pnpdev[setup_count]            = dev;
#endif
			printk (KERN_INFO
				"aha152x: found ISAPnP adapter at io=0x%03x, irq=%d\n",
				setup[setup_count].io_port, setup[setup_count].irq);
			setup_count++;
		}
	}
#endif

#if defined(AUTOCONF)
	if (setup_count<ARRAY_SIZE(setup)) {
#if !defined(SKIP_BIOSTEST)
		ok = 0;
		for (i = 0; i < ARRAY_SIZE(addresses) && !ok; i++) {
			void __iomem *p = ioremap(addresses[i], 0x4000);
			if (!p)
				continue;
			for (j = 0; j<ARRAY_SIZE(signatures) && !ok; j++)
				ok = check_signature(p + signatures[j].sig_offset,
								signatures[j].signature, signatures[j].sig_length);
			iounmap(p);
		}
		if (!ok && setup_count == 0)
			return -ENODEV;

		printk(KERN_INFO "aha152x: BIOS test: passed, ");
#else
		printk(KERN_INFO "aha152x: ");
#endif				/* !SKIP_BIOSTEST */

		ok = 0;
		for (i = 0; i < ARRAY_SIZE(ports) && setup_count < 2; i++) {
			if ((setup_count == 1) && (setup[0].io_port == ports[i]))
				continue;

			if (!request_region(ports[i], IO_RANGE, "aha152x")) {
				printk(KERN_ERR "aha152x: io port 0x%x busy.\n", ports[i]);
				continue;
			}

			if (aha152x_porttest(ports[i])) {
				setup[setup_count].tc1550  = 0;

				conf.cf_port =
				    (GETPORT(ports[i] + O_PORTA) << 8) + GETPORT(ports[i] + O_PORTB);
			} else if (tc1550_porttest(ports[i])) {
				setup[setup_count].tc1550  = 1;

				conf.cf_port =
				    (GETPORT(ports[i] + O_TC_PORTA) << 8) + GETPORT(ports[i] + O_TC_PORTB);
			} else {
				release_region(ports[i], IO_RANGE);
				continue;
			}

			release_region(ports[i], IO_RANGE);

			ok++;
			setup[setup_count].io_port = ports[i];
			setup[setup_count].irq = IRQ_MIN + conf.cf_irq;
			setup[setup_count].scsiid = conf.cf_id;
			setup[setup_count].reconnect = conf.cf_tardisc;
			setup[setup_count].parity = !conf.cf_parity;
			setup[setup_count].synchronous = conf.cf_syncneg;
			setup[setup_count].delay = DELAY_DEFAULT;
			setup[setup_count].ext_trans = 0;
			setup_count++;

		}

		if (ok)
			printk("auto configuration: ok, ");
	}
#endif

	printk("%d controller(s) configured\n", setup_count);

	for (i=0; i<setup_count; i++) {
		if ( request_region(setup[i].io_port, IO_RANGE, "aha152x") ) {
			struct Scsi_Host *shpnt = aha152x_probe_one(&setup[i]);

			if( !shpnt ) {
				release_region(setup[i].io_port, IO_RANGE);
#if defined(__ISAPNP__)
			} else if( pnpdev[i] ) {
				HOSTDATA(shpnt)->pnpdev=pnpdev[i];
				pnpdev[i]=NULL;
#endif
			}
		} else {
			printk(KERN_ERR "aha152x: io port 0x%x busy.\n", setup[i].io_port);
		}

#if defined(__ISAPNP__)
		if( pnpdev[i] )
			pnp_device_detach(pnpdev[i]);
#endif
	}

	return 0;
}

static void __exit aha152x_exit(void)
{
	struct aha152x_hostdata *hd, *tmp;

	list_for_each_entry_safe(hd, tmp, &aha152x_host_list, host_list) {
		struct Scsi_Host *shost = container_of((void *)hd, struct Scsi_Host, hostdata);

		aha152x_release(shost);
	}
}

module_init(aha152x_init);
module_exit(aha152x_exit);

#if !defined(MODULE)
static int __init aha152x_setup(char *str)
{
	int ints[10];

	get_options(str, ARRAY_SIZE(ints), ints);

	if(setup_count>=ARRAY_SIZE(setup)) {
		printk(KERN_ERR "aha152x: you can only configure up to two controllers\n");
		return 1;
	}

	setup[setup_count].conf        = str;
	setup[setup_count].io_port     = ints[0] >= 1 ? ints[1] : 0x340;
	setup[setup_count].irq         = ints[0] >= 2 ? ints[2] : 11;
	setup[setup_count].scsiid      = ints[0] >= 3 ? ints[3] : 7;
	setup[setup_count].reconnect   = ints[0] >= 4 ? ints[4] : 1;
	setup[setup_count].parity      = ints[0] >= 5 ? ints[5] : 1;
	setup[setup_count].synchronous = ints[0] >= 6 ? ints[6] : 1;
	setup[setup_count].delay       = ints[0] >= 7 ? ints[7] : DELAY_DEFAULT;
	setup[setup_count].ext_trans   = ints[0] >= 8 ? ints[8] : 0;
	if (ints[0] > 8) {                                                /*}*/
		printk(KERN_NOTICE "aha152x: usage: aha152x=<IOBASE>[,<IRQ>[,<SCSI ID>"
		       "[,<RECONNECT>[,<PARITY>[,<SYNCHRONOUS>[,<DELAY>[,<EXT_TRANS>]]]]]]]\n");
	} else {
		setup_count++;
		return 0;
	}

	return 1;
}
__setup("aha152x=", aha152x_setup);
#endif

#endif /* !PCMCIA */
