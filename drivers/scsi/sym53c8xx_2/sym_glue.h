/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SYM_GLUE_H
#define SYM_GLUE_H

#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/types.h>

#include <asm/io.h>
#ifdef __sparc__
#  include <asm/irq.h>
#endif

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_spi.h>
#include <scsi/scsi_host.h>

#include "sym53c8xx.h"
#include "sym_defs.h"
#include "sym_misc.h"

/*
 * Configuration addendum for Linux.
 */
#define	SYM_CONF_TIMER_INTERVAL		((HZ+1)/2)

#undef SYM_OPT_HANDLE_DEVICE_QUEUEING
#define SYM_OPT_LIMIT_COMMAND_REORDERING

/*
 *  Print a message with severity.
 */
#define printf_emerg(args...)	printk(KERN_EMERG args)
#define	printf_alert(args...)	printk(KERN_ALERT args)
#define	printf_crit(args...)	printk(KERN_CRIT args)
#define	printf_err(args...)	printk(KERN_ERR	args)
#define	printf_warning(args...)	printk(KERN_WARNING args)
#define	printf_notice(args...)	printk(KERN_NOTICE args)
#define	printf_info(args...)	printk(KERN_INFO args)
#define	printf_debug(args...)	printk(KERN_DEBUG args)
#define	printf(args...)		printk(args)

/*
 *  A 'read barrier' flushes any data that have been prefetched 
 *  by the processor due to out of order execution. Such a barrier 
 *  must notably be inserted prior to looking at data that have 
 *  been DMAed, assuming that program does memory READs in proper 
 *  order and that the device ensured proper ordering of WRITEs.
 *
 *  A 'write barrier' prevents any previous WRITEs to pass further 
 *  WRITEs. Such barriers must be inserted each time another agent 
 *  relies on ordering of WRITEs.
 *
 *  Note that, due to posting of PCI memory writes, we also must 
 *  insert dummy PCI read transactions when some ordering involving 
 *  both directions over the PCI does matter. PCI transactions are 
 *  fully ordered in each direction.
 */

#define MEMORY_READ_BARRIER()	rmb()
#define MEMORY_WRITE_BARRIER()	wmb()

/*
 *  IO functions definition for big/little endian CPU support.
 *  For now, PCI chips are only supported in little endian addressing mode, 
 */

#ifdef	__BIG_ENDIAN

#define	readw_l2b	readw
#define	readl_l2b	readl
#define	writew_b2l	writew
#define	writel_b2l	writel

#else	/* little endian */

#define	readw_raw	readw
#define	readl_raw	readl
#define	writew_raw	writew
#define	writel_raw	writel

#endif /* endian */

#ifdef	SYM_CONF_CHIP_BIG_ENDIAN
#error	"Chips in BIG ENDIAN addressing mode are not (yet) supported"
#endif

/*
 *  If the CPU and the chip use same endian-ness addressing,
 *  no byte reordering is needed for script patching.
 *  Macro cpu_to_scr() is to be used for script patching.
 *  Macro scr_to_cpu() is to be used for getting a DWORD 
 *  from the script.
 */

#define cpu_to_scr(dw)	cpu_to_le32(dw)
#define scr_to_cpu(dw)	le32_to_cpu(dw)

/*
 *  These ones are used as return code from 
 *  error recovery handlers under Linux.
 */
#define SCSI_SUCCESS	SUCCESS
#define SCSI_FAILED	FAILED

/*
 *  System specific target data structure.
 *  None for now, under Linux.
 */
/* #define SYM_HAVE_STCB */

/*
 *  System specific lun data structure.
 */
#define SYM_HAVE_SLCB
struct sym_slcb {
	u_short	reqtags;	/* Number of tags requested by user */
	u_short scdev_depth;	/* Queue depth set in select_queue_depth() */
};

/*
 *  System specific command data structure.
 *  Not needed under Linux.
 */
/* struct sym_sccb */

/*
 *  System specific host data structure.
 */
struct sym_shcb {
	/*
	 *  Chip and controller indentification.
	 */
	int		unit;
	char		inst_name[16];
	char		chip_name[8];
	struct pci_dev	*device;

	struct Scsi_Host *host;

	void __iomem *	ioaddr;		/* MMIO kernel io address	*/
	void __iomem *	ramaddr;	/* RAM  kernel io address	*/
	u_short		io_ws;		/* IO window size		*/
	int		irq;		/* IRQ number			*/

	struct timer_list timer;	/* Timer handler link header	*/
	u_long		lasttime;
	u_long		settle_time;	/* Resetting the SCSI BUS	*/
	u_char		settle_time_valid;
};

/*
 *  Return the name of the controller.
 */
#define sym_name(np) (np)->s.inst_name

struct sym_nvram;

/*
 * The IO macros require a struct called 's' and are abused in sym_nvram.c
 */
struct sym_device {
	struct pci_dev *pdev;
	unsigned long mmio_base;
	unsigned long ram_base;
	struct {
		void __iomem *ioaddr;
		void __iomem *ramaddr;
	} s;
	struct sym_chip chip;
	struct sym_nvram *nvram;
	u_short device_id;
	u_char host_id;
};

/*
 *  Driver host data structure.
 */
struct host_data {
	struct sym_hcb *ncb;
};

static inline struct sym_hcb * sym_get_hcb(struct Scsi_Host *host)
{
	return ((struct host_data *)host->hostdata)->ncb;
}

#include "sym_fw.h"
#include "sym_hipd.h"

/*
 *  Set the status field of a CAM CCB.
 */
static __inline void 
sym_set_cam_status(struct scsi_cmnd *cmd, int status)
{
	cmd->result &= ~(0xff  << 16);
	cmd->result |= (status << 16);
}

/*
 *  Get the status field of a CAM CCB.
 */
static __inline int 
sym_get_cam_status(struct scsi_cmnd *cmd)
{
	return host_byte(cmd->result);
}

/*
 *  Build CAM result for a successful IO and for a failed IO.
 */
static __inline void sym_set_cam_result_ok(struct sym_ccb *cp, struct scsi_cmnd *cmd, int resid)
{
	scsi_set_resid(cmd, resid);
	cmd->result = (((DID_OK) << 16) + ((cp->ssss_status) & 0x7f));
}
void sym_set_cam_result_error(struct sym_hcb *np, struct sym_ccb *cp, int resid);

void sym_xpt_done(struct sym_hcb *np, struct scsi_cmnd *ccb);
#define sym_print_addr(cmd, arg...) dev_info(&cmd->device->sdev_gendev , ## arg)
void sym_xpt_async_bus_reset(struct sym_hcb *np);
void sym_xpt_async_sent_bdr(struct sym_hcb *np, int target);
int  sym_setup_data_and_start (struct sym_hcb *np, struct scsi_cmnd *csio, struct sym_ccb *cp);
void sym_log_bus_error(struct sym_hcb *np);

#endif /* SYM_GLUE_H */
