/*
 * Adaptec AIC7xxx device driver for Linux.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic7xxx_osm.c#235 $
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Sources include the Adaptec 1740 driver (aha1740.c), the Ultrastor 24F
 * driver (ultrastor.c), various Linux kernel source, the Adaptec EISA
 * config file (!adp7771.cfg), the Adaptec AHA-2740A Series User's Guide,
 * the Linux Kernel Hacker's Guide, Writing a SCSI Device Driver for Linux,
 * the Adaptec 1542 driver (aha1542.c), the Adaptec EISA overlay file
 * (adp7770.ovl), the Adaptec AHA-2740 Series Technical Reference Manual,
 * the Adaptec AIC-7770 Data Book, the ANSI SCSI specification, the
 * ANSI SCSI-2 specification (draft 10c), ...
 *
 * --------------------------------------------------------------------------
 *
 *  Modifications by Daniel M. Eischen (deischen@iworks.InterWorks.org):
 *
 *  Substantially modified to include support for wide and twin bus
 *  adapters, DMAing of SCBs, tagged queueing, IRQ sharing, bug fixes,
 *  SCB paging, and other rework of the code.
 *
 * --------------------------------------------------------------------------
 * Copyright (c) 1994-2000 Justin T. Gibbs.
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 *---------------------------------------------------------------------------
 *
 *  Thanks also go to (in alphabetical order) the following:
 *
 *    Rory Bolt     - Sequencer bug fixes
 *    Jay Estabrook - Initial DEC Alpha support
 *    Doug Ledford  - Much needed abort/reset bug fixes
 *    Kai Makisara  - DMAing of SCBs
 *
 *  A Boot time option was also added for not resetting the scsi bus.
 *
 *    Form:  aic7xxx=extended
 *           aic7xxx=no_reset
 *           aic7xxx=verbose
 *
 *  Daniel M. Eischen, deischen@iworks.InterWorks.org, 1/23/97
 *
 *  Id: aic7xxx.c,v 4.1 1997/06/12 08:23:42 deang Exp
 */

/*
 * Further driver modifications made by Doug Ledford <dledford@redhat.com>
 *
 * Copyright (c) 1997-1999 Doug Ledford
 *
 * These changes are released under the same licensing terms as the FreeBSD
 * driver written by Justin Gibbs.  Please see his Copyright notice above
 * for the exact terms and conditions covering my changes as well as the
 * warranty statement.
 *
 * Modifications made to the aic7xxx.c,v 4.1 driver from Dan Eischen include
 * but are not limited to:
 *
 *  1: Import of the latest FreeBSD sequencer code for this driver
 *  2: Modification of kernel code to accommodate different sequencer semantics
 *  3: Extensive changes throughout kernel portion of driver to improve
 *     abort/reset processing and error hanndling
 *  4: Other work contributed by various people on the Internet
 *  5: Changes to printk information and verbosity selection code
 *  6: General reliability related changes, especially in IRQ management
 *  7: Modifications to the default probe/attach order for supported cards
 *  8: SMP friendliness has been improved
 *
 */

#include "aic7xxx_osm.h"
#include "aic7xxx_inline.h"
#include <scsi/scsicam.h>

static struct scsi_transport_template *ahc_linux_transport_template = NULL;

#include <linux/init.h>		/* __setup */
#include <linux/mm.h>		/* For fetching system memory size */
#include <linux/blkdev.h>		/* For block_size() */
#include <linux/delay.h>	/* For ssleep/msleep */


/*
 * Set this to the delay in seconds after SCSI bus reset.
 * Note, we honor this only for the initial bus reset.
 * The scsi error recovery code performs its own bus settle
 * delay handling for error recovery actions.
 */
#ifdef CONFIG_AIC7XXX_RESET_DELAY_MS
#define AIC7XXX_RESET_DELAY CONFIG_AIC7XXX_RESET_DELAY_MS
#else
#define AIC7XXX_RESET_DELAY 5000
#endif

/*
 * Control collection of SCSI transfer statistics for the /proc filesystem.
 *
 * NOTE: Do NOT enable this when running on kernels version 1.2.x and below.
 * NOTE: This does affect performance since it has to maintain statistics.
 */
#ifdef CONFIG_AIC7XXX_PROC_STATS
#define AIC7XXX_PROC_STATS
#endif

/*
 * To change the default number of tagged transactions allowed per-device,
 * add a line to the lilo.conf file like:
 * append="aic7xxx=verbose,tag_info:{{32,32,32,32},{32,32,32,32}}"
 * which will result in the first four devices on the first two
 * controllers being set to a tagged queue depth of 32.
 *
 * The tag_commands is an array of 16 to allow for wide and twin adapters.
 * Twin adapters will use indexes 0-7 for channel 0, and indexes 8-15
 * for channel 1.
 */
typedef struct {
	uint8_t tag_commands[16];	/* Allow for wide/twin adapters. */
} adapter_tag_info_t;

/*
 * Modify this as you see fit for your system.
 *
 * 0			tagged queuing disabled
 * 1 <= n <= 253	n == max tags ever dispatched.
 *
 * The driver will throttle the number of commands dispatched to a
 * device if it returns queue full.  For devices with a fixed maximum
 * queue depth, the driver will eventually determine this depth and
 * lock it in (a console message is printed to indicate that a lock
 * has occurred).  On some devices, queue full is returned for a temporary
 * resource shortage.  These devices will return queue full at varying
 * depths.  The driver will throttle back when the queue fulls occur and
 * attempt to slowly increase the depth over time as the device recovers
 * from the resource shortage.
 *
 * In this example, the first line will disable tagged queueing for all
 * the devices on the first probed aic7xxx adapter.
 *
 * The second line enables tagged queueing with 4 commands/LUN for IDs
 * (0, 2-11, 13-15), disables tagged queueing for ID 12, and tells the
 * driver to attempt to use up to 64 tags for ID 1.
 *
 * The third line is the same as the first line.
 *
 * The fourth line disables tagged queueing for devices 0 and 3.  It
 * enables tagged queueing for the other IDs, with 16 commands/LUN
 * for IDs 1 and 4, 127 commands/LUN for ID 8, and 4 commands/LUN for
 * IDs 2, 5-7, and 9-15.
 */

/*
 * NOTE: The below structure is for reference only, the actual structure
 *       to modify in order to change things is just below this comment block.
adapter_tag_info_t aic7xxx_tag_info[] =
{
	{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
	{{4, 64, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 4, 4, 4}},
	{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
	{{0, 16, 4, 0, 16, 4, 4, 4, 127, 4, 4, 4, 4, 4, 4, 4}}
};
*/

#ifdef CONFIG_AIC7XXX_CMDS_PER_DEVICE
#define AIC7XXX_CMDS_PER_DEVICE CONFIG_AIC7XXX_CMDS_PER_DEVICE
#else
#define AIC7XXX_CMDS_PER_DEVICE AHC_MAX_QUEUE
#endif

#define AIC7XXX_CONFIGED_TAG_COMMANDS {					\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE,		\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE,		\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE,		\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE,		\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE,		\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE,		\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE,		\
	AIC7XXX_CMDS_PER_DEVICE, AIC7XXX_CMDS_PER_DEVICE		\
}

/*
 * By default, use the number of commands specified by
 * the users kernel configuration.
 */
static adapter_tag_info_t aic7xxx_tag_info[] =
{
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS},
	{AIC7XXX_CONFIGED_TAG_COMMANDS}
};

/*
 * There should be a specific return value for this in scsi.h, but
 * it seems that most drivers ignore it.
 */
#define DID_UNDERFLOW   DID_ERROR

void
ahc_print_path(struct ahc_softc *ahc, struct scb *scb)
{
	printk("(scsi%d:%c:%d:%d): ",
	       ahc->platform_data->host->host_no,
	       scb != NULL ? SCB_GET_CHANNEL(ahc, scb) : 'X',
	       scb != NULL ? SCB_GET_TARGET(ahc, scb) : -1,
	       scb != NULL ? SCB_GET_LUN(scb) : -1);
}

/*
 * XXX - these options apply unilaterally to _all_ 274x/284x/294x
 *       cards in the system.  This should be fixed.  Exceptions to this
 *       rule are noted in the comments.
 */

/*
 * Skip the scsi bus reset.  Non 0 make us skip the reset at startup.  This
 * has no effect on any later resets that might occur due to things like
 * SCSI bus timeouts.
 */
static uint32_t aic7xxx_no_reset;

/*
 * Should we force EXTENDED translation on a controller.
 *     0 == Use whatever is in the SEEPROM or default to off
 *     1 == Use whatever is in the SEEPROM or default to on
 */
static uint32_t aic7xxx_extended;

/*
 * PCI bus parity checking of the Adaptec controllers.  This is somewhat
 * dubious at best.  To my knowledge, this option has never actually
 * solved a PCI parity problem, but on certain machines with broken PCI
 * chipset configurations where stray PCI transactions with bad parity are
 * the norm rather than the exception, the error messages can be overwelming.
 * It's included in the driver for completeness.
 *   0	   = Shut off PCI parity check
 *   non-0 = reverse polarity pci parity checking
 */
static uint32_t aic7xxx_pci_parity = ~0;

/*
 * There are lots of broken chipsets in the world.  Some of them will
 * violate the PCI spec when we issue byte sized memory writes to our
 * controller.  I/O mapped register access, if allowed by the given
 * platform, will work in almost all cases.
 */
uint32_t aic7xxx_allow_memio = ~0;

/*
 * So that we can set how long each device is given as a selection timeout.
 * The table of values goes like this:
 *   0 - 256ms
 *   1 - 128ms
 *   2 - 64ms
 *   3 - 32ms
 * We default to 256ms because some older devices need a longer time
 * to respond to initial selection.
 */
static uint32_t aic7xxx_seltime;

/*
 * Certain devices do not perform any aging on commands.  Should the
 * device be saturated by commands in one portion of the disk, it is
 * possible for transactions on far away sectors to never be serviced.
 * To handle these devices, we can periodically send an ordered tag to
 * force all outstanding transactions to be serviced prior to a new
 * transaction.
 */
static uint32_t aic7xxx_periodic_otag;

/*
 * Module information and settable options.
 */
static char *aic7xxx = NULL;

MODULE_AUTHOR("Maintainer: Hannes Reinecke <hare@suse.de>");
MODULE_DESCRIPTION("Adaptec AIC77XX/78XX SCSI Host Bus Adapter driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(AIC7XXX_DRIVER_VERSION);
module_param(aic7xxx, charp, 0444);
MODULE_PARM_DESC(aic7xxx,
"period-delimited options string:\n"
"	verbose			Enable verbose/diagnostic logging\n"
"	allow_memio		Allow device registers to be memory mapped\n"
"	debug			Bitmask of debug values to enable\n"
"	no_probe		Toggle EISA/VLB controller probing\n"
"	probe_eisa_vl		Toggle EISA/VLB controller probing\n"
"	no_reset		Supress initial bus resets\n"
"	extended		Enable extended geometry on all controllers\n"
"	periodic_otag		Send an ordered tagged transaction\n"
"				periodically to prevent tag starvation.\n"
"				This may be required by some older disk\n"
"				drives or RAID arrays.\n"
"	tag_info:<tag_str>	Set per-target tag depth\n"
"	global_tag_depth:<int>	Global tag depth for every target\n"
"				on every bus\n"
"	seltime:<int>		Selection Timeout\n"
"				(0/256ms,1/128ms,2/64ms,3/32ms)\n"
"\n"
"	Sample /etc/modprobe.conf line:\n"
"		Toggle EISA/VLB probing\n"
"		Set tag depth on Controller 1/Target 1 to 10 tags\n"
"		Shorten the selection timeout to 128ms\n"
"\n"
"	options aic7xxx 'aic7xxx=probe_eisa_vl.tag_info:{{}.{.10}}.seltime:1'\n"
);

static void ahc_linux_handle_scsi_status(struct ahc_softc *,
					 struct scsi_device *,
					 struct scb *);
static void ahc_linux_queue_cmd_complete(struct ahc_softc *ahc,
					 struct scsi_cmnd *cmd);
static void ahc_linux_freeze_simq(struct ahc_softc *ahc);
static void ahc_linux_release_simq(struct ahc_softc *ahc);
static int  ahc_linux_queue_recovery_cmd(struct scsi_cmnd *cmd, scb_flag flag);
static void ahc_linux_initialize_scsi_bus(struct ahc_softc *ahc);
static u_int ahc_linux_user_tagdepth(struct ahc_softc *ahc,
				     struct ahc_devinfo *devinfo);
static void ahc_linux_device_queue_depth(struct scsi_device *);
static int ahc_linux_run_command(struct ahc_softc*,
				 struct ahc_linux_device *,
				 struct scsi_cmnd *);
static void ahc_linux_setup_tag_info_global(char *p);
static int  aic7xxx_setup(char *s);

static int ahc_linux_unit;


/********************************* Inlines ************************************/
static __inline void ahc_linux_unmap_scb(struct ahc_softc*, struct scb*);

static __inline int ahc_linux_map_seg(struct ahc_softc *ahc, struct scb *scb,
		 		      struct ahc_dma_seg *sg,
				      dma_addr_t addr, bus_size_t len);

static __inline void
ahc_linux_unmap_scb(struct ahc_softc *ahc, struct scb *scb)
{
	struct scsi_cmnd *cmd;

	cmd = scb->io_ctx;
	ahc_sync_sglist(ahc, scb, BUS_DMASYNC_POSTWRITE);

	scsi_dma_unmap(cmd);
}

static __inline int
ahc_linux_map_seg(struct ahc_softc *ahc, struct scb *scb,
		  struct ahc_dma_seg *sg, dma_addr_t addr, bus_size_t len)
{
	int	 consumed;

	if ((scb->sg_count + 1) > AHC_NSEG)
		panic("Too few segs for dma mapping.  "
		      "Increase AHC_NSEG\n");

	consumed = 1;
	sg->addr = ahc_htole32(addr & 0xFFFFFFFF);
	scb->platform_data->xfer_len += len;

	if (sizeof(dma_addr_t) > 4
	 && (ahc->flags & AHC_39BIT_ADDRESSING) != 0)
		len |= (addr >> 8) & AHC_SG_HIGH_ADDR_MASK;

	sg->len = ahc_htole32(len);
	return (consumed);
}

/*
 * Return a string describing the driver.
 */
static const char *
ahc_linux_info(struct Scsi_Host *host)
{
	static char buffer[512];
	char	ahc_info[256];
	char   *bp;
	struct ahc_softc *ahc;

	bp = &buffer[0];
	ahc = *(struct ahc_softc **)host->hostdata;
	memset(bp, 0, sizeof(buffer));
	strcpy(bp, "Adaptec AIC7XXX EISA/VLB/PCI SCSI HBA DRIVER, Rev ");
	strcat(bp, AIC7XXX_DRIVER_VERSION);
	strcat(bp, "\n");
	strcat(bp, "        <");
	strcat(bp, ahc->description);
	strcat(bp, ">\n");
	strcat(bp, "        ");
	ahc_controller_info(ahc, ahc_info);
	strcat(bp, ahc_info);
	strcat(bp, "\n");

	return (bp);
}

/*
 * Queue an SCB to the controller.
 */
static int
ahc_linux_queue(struct scsi_cmnd * cmd, void (*scsi_done) (struct scsi_cmnd *))
{
	struct	 ahc_softc *ahc;
	struct	 ahc_linux_device *dev = scsi_transport_device_data(cmd->device);
	int rtn = SCSI_MLQUEUE_HOST_BUSY;
	unsigned long flags;

	ahc = *(struct ahc_softc **)cmd->device->host->hostdata;

	ahc_lock(ahc, &flags);
	if (ahc->platform_data->qfrozen == 0) {
		cmd->scsi_done = scsi_done;
		cmd->result = CAM_REQ_INPROG << 16;
		rtn = ahc_linux_run_command(ahc, dev, cmd);
	}
	ahc_unlock(ahc, &flags);

	return rtn;
}

static inline struct scsi_target **
ahc_linux_target_in_softc(struct scsi_target *starget)
{
	struct	ahc_softc *ahc =
		*((struct ahc_softc **)dev_to_shost(&starget->dev)->hostdata);
	unsigned int target_offset;

	target_offset = starget->id;
	if (starget->channel != 0)
		target_offset += 8;

	return &ahc->platform_data->starget[target_offset];
}

static int
ahc_linux_target_alloc(struct scsi_target *starget)
{
	struct	ahc_softc *ahc =
		*((struct ahc_softc **)dev_to_shost(&starget->dev)->hostdata);
	struct seeprom_config *sc = ahc->seep_config;
	unsigned long flags;
	struct scsi_target **ahc_targp = ahc_linux_target_in_softc(starget);
	unsigned short scsirate;
	struct ahc_devinfo devinfo;
	struct ahc_initiator_tinfo *tinfo;
	struct ahc_tmode_tstate *tstate;
	char channel = starget->channel + 'A';
	unsigned int our_id = ahc->our_id;
	unsigned int target_offset;

	target_offset = starget->id;
	if (starget->channel != 0)
		target_offset += 8;
	  
	if (starget->channel)
		our_id = ahc->our_id_b;

	ahc_lock(ahc, &flags);

	BUG_ON(*ahc_targp != NULL);

	*ahc_targp = starget;

	if (sc) {
		int maxsync = AHC_SYNCRATE_DT;
		int ultra = 0;
		int flags = sc->device_flags[target_offset];

		if (ahc->flags & AHC_NEWEEPROM_FMT) {
		    if (flags & CFSYNCHISULTRA)
			ultra = 1;
		} else if (flags & CFULTRAEN)
			ultra = 1;
		/* AIC nutcase; 10MHz appears as ultra = 1, CFXFER = 0x04
		 * change it to ultra=0, CFXFER = 0 */
		if(ultra && (flags & CFXFER) == 0x04) {
			ultra = 0;
			flags &= ~CFXFER;
		}
	    
		if ((ahc->features & AHC_ULTRA2) != 0) {
			scsirate = (flags & CFXFER) | (ultra ? 0x8 : 0);
		} else {
			scsirate = (flags & CFXFER) << 4;
			maxsync = ultra ? AHC_SYNCRATE_ULTRA : 
				AHC_SYNCRATE_FAST;
		}
		spi_max_width(starget) = (flags & CFWIDEB) ? 1 : 0;
		if (!(flags & CFSYNCH))
			spi_max_offset(starget) = 0;
		spi_min_period(starget) = 
			ahc_find_period(ahc, scsirate, maxsync);

		tinfo = ahc_fetch_transinfo(ahc, channel, ahc->our_id,
					    starget->id, &tstate);
	}
	ahc_compile_devinfo(&devinfo, our_id, starget->id,
			    CAM_LUN_WILDCARD, channel,
			    ROLE_INITIATOR);
	ahc_set_syncrate(ahc, &devinfo, NULL, 0, 0, 0,
			 AHC_TRANS_GOAL, /*paused*/FALSE);
	ahc_set_width(ahc, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
		      AHC_TRANS_GOAL, /*paused*/FALSE);
	ahc_unlock(ahc, &flags);

	return 0;
}

static void
ahc_linux_target_destroy(struct scsi_target *starget)
{
	struct scsi_target **ahc_targp = ahc_linux_target_in_softc(starget);

	*ahc_targp = NULL;
}

static int
ahc_linux_slave_alloc(struct scsi_device *sdev)
{
	struct	ahc_softc *ahc =
		*((struct ahc_softc **)sdev->host->hostdata);
	struct scsi_target *starget = sdev->sdev_target;
	struct ahc_linux_device *dev;

	if (bootverbose)
		printf("%s: Slave Alloc %d\n", ahc_name(ahc), sdev->id);

	dev = scsi_transport_device_data(sdev);
	memset(dev, 0, sizeof(*dev));

	/*
	 * We start out life using untagged
	 * transactions of which we allow one.
	 */
	dev->openings = 1;

	/*
	 * Set maxtags to 0.  This will be changed if we
	 * later determine that we are dealing with
	 * a tagged queuing capable device.
	 */
	dev->maxtags = 0;
	
	spi_period(starget) = 0;

	return 0;
}

static int
ahc_linux_slave_configure(struct scsi_device *sdev)
{
	struct	ahc_softc *ahc;

	ahc = *((struct ahc_softc **)sdev->host->hostdata);

	if (bootverbose)
		sdev_printk(KERN_INFO, sdev, "Slave Configure\n");

	ahc_linux_device_queue_depth(sdev);

	/* Initial Domain Validation */
	if (!spi_initial_dv(sdev->sdev_target))
		spi_dv_device(sdev);

	return 0;
}

#if defined(__i386__)
/*
 * Return the disk geometry for the given SCSI device.
 */
static int
ahc_linux_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		    sector_t capacity, int geom[])
{
	uint8_t *bh;
	int	 heads;
	int	 sectors;
	int	 cylinders;
	int	 ret;
	int	 extended;
	struct	 ahc_softc *ahc;
	u_int	 channel;

	ahc = *((struct ahc_softc **)sdev->host->hostdata);
	channel = sdev_channel(sdev);

	bh = scsi_bios_ptable(bdev);
	if (bh) {
		ret = scsi_partsize(bh, capacity,
				    &geom[2], &geom[0], &geom[1]);
		kfree(bh);
		if (ret != -1)
			return (ret);
	}
	heads = 64;
	sectors = 32;
	cylinders = aic_sector_div(capacity, heads, sectors);

	if (aic7xxx_extended != 0)
		extended = 1;
	else if (channel == 0)
		extended = (ahc->flags & AHC_EXTENDED_TRANS_A) != 0;
	else
		extended = (ahc->flags & AHC_EXTENDED_TRANS_B) != 0;
	if (extended && cylinders >= 1024) {
		heads = 255;
		sectors = 63;
		cylinders = aic_sector_div(capacity, heads, sectors);
	}
	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;
	return (0);
}
#endif

/*
 * Abort the current SCSI command(s).
 */
static int
ahc_linux_abort(struct scsi_cmnd *cmd)
{
	int error;

	error = ahc_linux_queue_recovery_cmd(cmd, SCB_ABORT);
	if (error != 0)
		printf("aic7xxx_abort returns 0x%x\n", error);
	return (error);
}

/*
 * Attempt to send a target reset message to the device that timed out.
 */
static int
ahc_linux_dev_reset(struct scsi_cmnd *cmd)
{
	int error;

	error = ahc_linux_queue_recovery_cmd(cmd, SCB_DEVICE_RESET);
	if (error != 0)
		printf("aic7xxx_dev_reset returns 0x%x\n", error);
	return (error);
}

/*
 * Reset the SCSI bus.
 */
static int
ahc_linux_bus_reset(struct scsi_cmnd *cmd)
{
	struct ahc_softc *ahc;
	int    found;
	unsigned long flags;

	ahc = *(struct ahc_softc **)cmd->device->host->hostdata;

	ahc_lock(ahc, &flags);
	found = ahc_reset_channel(ahc, scmd_channel(cmd) + 'A',
				  /*initiate reset*/TRUE);
	ahc_unlock(ahc, &flags);

	if (bootverbose)
		printf("%s: SCSI bus reset delivered. "
		       "%d SCBs aborted.\n", ahc_name(ahc), found);

	return SUCCESS;
}

struct scsi_host_template aic7xxx_driver_template = {
	.module			= THIS_MODULE,
	.name			= "aic7xxx",
	.proc_name		= "aic7xxx",
	.proc_info		= ahc_linux_proc_info,
	.info			= ahc_linux_info,
	.queuecommand		= ahc_linux_queue,
	.eh_abort_handler	= ahc_linux_abort,
	.eh_device_reset_handler = ahc_linux_dev_reset,
	.eh_bus_reset_handler	= ahc_linux_bus_reset,
#if defined(__i386__)
	.bios_param		= ahc_linux_biosparam,
#endif
	.can_queue		= AHC_MAX_QUEUE,
	.this_id		= -1,
	.max_sectors		= 8192,
	.cmd_per_lun		= 2,
	.use_clustering		= ENABLE_CLUSTERING,
	.use_sg_chaining	= ENABLE_SG_CHAINING,
	.slave_alloc		= ahc_linux_slave_alloc,
	.slave_configure	= ahc_linux_slave_configure,
	.target_alloc		= ahc_linux_target_alloc,
	.target_destroy		= ahc_linux_target_destroy,
};

/**************************** Tasklet Handler *********************************/

/******************************** Macros **************************************/
#define BUILD_SCSIID(ahc, cmd)						    \
	((((cmd)->device->id << TID_SHIFT) & TID)			    \
	| (((cmd)->device->channel == 0) ? (ahc)->our_id : (ahc)->our_id_b) \
	| (((cmd)->device->channel == 0) ? 0 : TWIN_CHNLB))

/******************************** Bus DMA *************************************/
int
ahc_dma_tag_create(struct ahc_softc *ahc, bus_dma_tag_t parent,
		   bus_size_t alignment, bus_size_t boundary,
		   dma_addr_t lowaddr, dma_addr_t highaddr,
		   bus_dma_filter_t *filter, void *filterarg,
		   bus_size_t maxsize, int nsegments,
		   bus_size_t maxsegsz, int flags, bus_dma_tag_t *ret_tag)
{
	bus_dma_tag_t dmat;

	dmat = malloc(sizeof(*dmat), M_DEVBUF, M_NOWAIT);
	if (dmat == NULL)
		return (ENOMEM);

	/*
	 * Linux is very simplistic about DMA memory.  For now don't
	 * maintain all specification information.  Once Linux supplies
	 * better facilities for doing these operations, or the
	 * needs of this particular driver change, we might need to do
	 * more here.
	 */
	dmat->alignment = alignment;
	dmat->boundary = boundary;
	dmat->maxsize = maxsize;
	*ret_tag = dmat;
	return (0);
}

void
ahc_dma_tag_destroy(struct ahc_softc *ahc, bus_dma_tag_t dmat)
{
	free(dmat, M_DEVBUF);
}

int
ahc_dmamem_alloc(struct ahc_softc *ahc, bus_dma_tag_t dmat, void** vaddr,
		 int flags, bus_dmamap_t *mapp)
{
	*vaddr = pci_alloc_consistent(ahc->dev_softc,
				      dmat->maxsize, mapp);
	if (*vaddr == NULL)
		return ENOMEM;
	return 0;
}

void
ahc_dmamem_free(struct ahc_softc *ahc, bus_dma_tag_t dmat,
		void* vaddr, bus_dmamap_t map)
{
	pci_free_consistent(ahc->dev_softc, dmat->maxsize,
			    vaddr, map);
}

int
ahc_dmamap_load(struct ahc_softc *ahc, bus_dma_tag_t dmat, bus_dmamap_t map,
		void *buf, bus_size_t buflen, bus_dmamap_callback_t *cb,
		void *cb_arg, int flags)
{
	/*
	 * Assume for now that this will only be used during
	 * initialization and not for per-transaction buffer mapping.
	 */
	bus_dma_segment_t stack_sg;

	stack_sg.ds_addr = map;
	stack_sg.ds_len = dmat->maxsize;
	cb(cb_arg, &stack_sg, /*nseg*/1, /*error*/0);
	return (0);
}

void
ahc_dmamap_destroy(struct ahc_softc *ahc, bus_dma_tag_t dmat, bus_dmamap_t map)
{
}

int
ahc_dmamap_unload(struct ahc_softc *ahc, bus_dma_tag_t dmat, bus_dmamap_t map)
{
	/* Nothing to do */
	return (0);
}

static void
ahc_linux_setup_tag_info_global(char *p)
{
	int tags, i, j;

	tags = simple_strtoul(p + 1, NULL, 0) & 0xff;
	printf("Setting Global Tags= %d\n", tags);

	for (i = 0; i < ARRAY_SIZE(aic7xxx_tag_info); i++) {
		for (j = 0; j < AHC_NUM_TARGETS; j++) {
			aic7xxx_tag_info[i].tag_commands[j] = tags;
		}
	}
}

static void
ahc_linux_setup_tag_info(u_long arg, int instance, int targ, int32_t value)
{

	if ((instance >= 0) && (targ >= 0)
	 && (instance < ARRAY_SIZE(aic7xxx_tag_info))
	 && (targ < AHC_NUM_TARGETS)) {
		aic7xxx_tag_info[instance].tag_commands[targ] = value & 0xff;
		if (bootverbose)
			printf("tag_info[%d:%d] = %d\n", instance, targ, value);
	}
}

static char *
ahc_parse_brace_option(char *opt_name, char *opt_arg, char *end, int depth,
		       void (*callback)(u_long, int, int, int32_t),
		       u_long callback_arg)
{
	char	*tok_end;
	char	*tok_end2;
	int      i;
	int      instance;
	int	 targ;
	int	 done;
	char	 tok_list[] = {'.', ',', '{', '}', '\0'};

	/* All options use a ':' name/arg separator */
	if (*opt_arg != ':')
		return (opt_arg);
	opt_arg++;
	instance = -1;
	targ = -1;
	done = FALSE;
	/*
	 * Restore separator that may be in
	 * the middle of our option argument.
	 */
	tok_end = strchr(opt_arg, '\0');
	if (tok_end < end)
		*tok_end = ',';
	while (!done) {
		switch (*opt_arg) {
		case '{':
			if (instance == -1) {
				instance = 0;
			} else {
				if (depth > 1) {
					if (targ == -1)
						targ = 0;
				} else {
					printf("Malformed Option %s\n",
					       opt_name);
					done = TRUE;
				}
			}
			opt_arg++;
			break;
		case '}':
			if (targ != -1)
				targ = -1;
			else if (instance != -1)
				instance = -1;
			opt_arg++;
			break;
		case ',':
		case '.':
			if (instance == -1)
				done = TRUE;
			else if (targ >= 0)
				targ++;
			else if (instance >= 0)
				instance++;
			opt_arg++;
			break;
		case '\0':
			done = TRUE;
			break;
		default:
			tok_end = end;
			for (i = 0; tok_list[i]; i++) {
				tok_end2 = strchr(opt_arg, tok_list[i]);
				if ((tok_end2) && (tok_end2 < tok_end))
					tok_end = tok_end2;
			}
			callback(callback_arg, instance, targ,
				 simple_strtol(opt_arg, NULL, 0));
			opt_arg = tok_end;
			break;
		}
	}
	return (opt_arg);
}

/*
 * Handle Linux boot parameters. This routine allows for assigning a value
 * to a parameter with a ':' between the parameter and the value.
 * ie. aic7xxx=stpwlev:1,extended
 */
static int
aic7xxx_setup(char *s)
{
	int	i, n;
	char   *p;
	char   *end;

	static struct {
		const char *name;
		uint32_t *flag;
	} options[] = {
		{ "extended", &aic7xxx_extended },
		{ "no_reset", &aic7xxx_no_reset },
		{ "verbose", &aic7xxx_verbose },
		{ "allow_memio", &aic7xxx_allow_memio},
#ifdef AHC_DEBUG
		{ "debug", &ahc_debug },
#endif
		{ "periodic_otag", &aic7xxx_periodic_otag },
		{ "pci_parity", &aic7xxx_pci_parity },
		{ "seltime", &aic7xxx_seltime },
		{ "tag_info", NULL },
		{ "global_tag_depth", NULL },
		{ "dv", NULL }
	};

	end = strchr(s, '\0');

	/*
	 * XXX ia64 gcc isn't smart enough to know that ARRAY_SIZE
	 * will never be 0 in this case.
	 */
	n = 0;

	while ((p = strsep(&s, ",.")) != NULL) {
		if (*p == '\0')
			continue;
		for (i = 0; i < ARRAY_SIZE(options); i++) {

			n = strlen(options[i].name);
			if (strncmp(options[i].name, p, n) == 0)
				break;
		}
		if (i == ARRAY_SIZE(options))
			continue;

		if (strncmp(p, "global_tag_depth", n) == 0) {
			ahc_linux_setup_tag_info_global(p + n);
		} else if (strncmp(p, "tag_info", n) == 0) {
			s = ahc_parse_brace_option("tag_info", p + n, end,
			    2, ahc_linux_setup_tag_info, 0);
		} else if (p[n] == ':') {
			*(options[i].flag) = simple_strtoul(p + n + 1, NULL, 0);
		} else if (strncmp(p, "verbose", n) == 0) {
			*(options[i].flag) = 1;
		} else {
			*(options[i].flag) ^= 0xFFFFFFFF;
		}
	}
	return 1;
}

__setup("aic7xxx=", aic7xxx_setup);

uint32_t aic7xxx_verbose;

int
ahc_linux_register_host(struct ahc_softc *ahc, struct scsi_host_template *template)
{
	char	buf[80];
	struct	Scsi_Host *host;
	char	*new_name;
	u_long	s;
	int	retval;

	template->name = ahc->description;
	host = scsi_host_alloc(template, sizeof(struct ahc_softc *));
	if (host == NULL)
		return (ENOMEM);

	*((struct ahc_softc **)host->hostdata) = ahc;
	ahc->platform_data->host = host;
	host->can_queue = AHC_MAX_QUEUE;
	host->cmd_per_lun = 2;
	/* XXX No way to communicate the ID for multiple channels */
	host->this_id = ahc->our_id;
	host->irq = ahc->platform_data->irq;
	host->max_id = (ahc->features & AHC_WIDE) ? 16 : 8;
	host->max_lun = AHC_NUM_LUNS;
	host->max_channel = (ahc->features & AHC_TWIN) ? 1 : 0;
	host->sg_tablesize = AHC_NSEG;
	ahc_lock(ahc, &s);
	ahc_set_unit(ahc, ahc_linux_unit++);
	ahc_unlock(ahc, &s);
	sprintf(buf, "scsi%d", host->host_no);
	new_name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (new_name != NULL) {
		strcpy(new_name, buf);
		ahc_set_name(ahc, new_name);
	}
	host->unique_id = ahc->unit;
	ahc_linux_initialize_scsi_bus(ahc);
	ahc_intr_enable(ahc, TRUE);

	host->transportt = ahc_linux_transport_template;

	retval = scsi_add_host(host,
			(ahc->dev_softc ? &ahc->dev_softc->dev : NULL));
	if (retval) {
		printk(KERN_WARNING "aic7xxx: scsi_add_host failed\n");
		scsi_host_put(host);
		return retval;
	}

	scsi_scan_host(host);
	return 0;
}

/*
 * Place the SCSI bus into a known state by either resetting it,
 * or forcing transfer negotiations on the next command to any
 * target.
 */
void
ahc_linux_initialize_scsi_bus(struct ahc_softc *ahc)
{
	int i;
	int numtarg;
	unsigned long s;

	i = 0;
	numtarg = 0;

	ahc_lock(ahc, &s);

	if (aic7xxx_no_reset != 0)
		ahc->flags &= ~(AHC_RESET_BUS_A|AHC_RESET_BUS_B);

	if ((ahc->flags & AHC_RESET_BUS_A) != 0)
		ahc_reset_channel(ahc, 'A', /*initiate_reset*/TRUE);
	else
		numtarg = (ahc->features & AHC_WIDE) ? 16 : 8;

	if ((ahc->features & AHC_TWIN) != 0) {

		if ((ahc->flags & AHC_RESET_BUS_B) != 0) {
			ahc_reset_channel(ahc, 'B', /*initiate_reset*/TRUE);
		} else {
			if (numtarg == 0)
				i = 8;
			numtarg += 8;
		}
	}

	/*
	 * Force negotiation to async for all targets that
	 * will not see an initial bus reset.
	 */
	for (; i < numtarg; i++) {
		struct ahc_devinfo devinfo;
		struct ahc_initiator_tinfo *tinfo;
		struct ahc_tmode_tstate *tstate;
		u_int our_id;
		u_int target_id;
		char channel;

		channel = 'A';
		our_id = ahc->our_id;
		target_id = i;
		if (i > 7 && (ahc->features & AHC_TWIN) != 0) {
			channel = 'B';
			our_id = ahc->our_id_b;
			target_id = i % 8;
		}
		tinfo = ahc_fetch_transinfo(ahc, channel, our_id,
					    target_id, &tstate);
		ahc_compile_devinfo(&devinfo, our_id, target_id,
				    CAM_LUN_WILDCARD, channel, ROLE_INITIATOR);
		ahc_update_neg_request(ahc, &devinfo, tstate,
				       tinfo, AHC_NEG_ALWAYS);
	}
	ahc_unlock(ahc, &s);
	/* Give the bus some time to recover */
	if ((ahc->flags & (AHC_RESET_BUS_A|AHC_RESET_BUS_B)) != 0) {
		ahc_linux_freeze_simq(ahc);
		msleep(AIC7XXX_RESET_DELAY);
		ahc_linux_release_simq(ahc);
	}
}

int
ahc_platform_alloc(struct ahc_softc *ahc, void *platform_arg)
{

	ahc->platform_data =
	    malloc(sizeof(struct ahc_platform_data), M_DEVBUF, M_NOWAIT);
	if (ahc->platform_data == NULL)
		return (ENOMEM);
	memset(ahc->platform_data, 0, sizeof(struct ahc_platform_data));
	ahc->platform_data->irq = AHC_LINUX_NOIRQ;
	ahc_lockinit(ahc);
	ahc->seltime = (aic7xxx_seltime & 0x3) << 4;
	ahc->seltime_b = (aic7xxx_seltime & 0x3) << 4;
	if (aic7xxx_pci_parity == 0)
		ahc->flags |= AHC_DISABLE_PCI_PERR;

	return (0);
}

void
ahc_platform_free(struct ahc_softc *ahc)
{
	struct scsi_target *starget;
	int i;

	if (ahc->platform_data != NULL) {
		/* destroy all of the device and target objects */
		for (i = 0; i < AHC_NUM_TARGETS; i++) {
			starget = ahc->platform_data->starget[i];
			if (starget != NULL) {
				ahc->platform_data->starget[i] = NULL;
 			}
 		}

		if (ahc->platform_data->irq != AHC_LINUX_NOIRQ)
			free_irq(ahc->platform_data->irq, ahc);
		if (ahc->tag == BUS_SPACE_PIO
		 && ahc->bsh.ioport != 0)
			release_region(ahc->bsh.ioport, 256);
		if (ahc->tag == BUS_SPACE_MEMIO
		 && ahc->bsh.maddr != NULL) {
			iounmap(ahc->bsh.maddr);
			release_mem_region(ahc->platform_data->mem_busaddr,
					   0x1000);
		}

		if (ahc->platform_data->host)
			scsi_host_put(ahc->platform_data->host);

		free(ahc->platform_data, M_DEVBUF);
	}
}

void
ahc_platform_freeze_devq(struct ahc_softc *ahc, struct scb *scb)
{
	ahc_platform_abort_scbs(ahc, SCB_GET_TARGET(ahc, scb),
				SCB_GET_CHANNEL(ahc, scb),
				SCB_GET_LUN(scb), SCB_LIST_NULL,
				ROLE_UNKNOWN, CAM_REQUEUE_REQ);
}

void
ahc_platform_set_tags(struct ahc_softc *ahc, struct scsi_device *sdev,
		      struct ahc_devinfo *devinfo, ahc_queue_alg alg)
{
	struct ahc_linux_device *dev;
	int was_queuing;
	int now_queuing;

	if (sdev == NULL)
		return;
	dev = scsi_transport_device_data(sdev);

	was_queuing = dev->flags & (AHC_DEV_Q_BASIC|AHC_DEV_Q_TAGGED);
	switch (alg) {
	default:
	case AHC_QUEUE_NONE:
		now_queuing = 0;
		break; 
	case AHC_QUEUE_BASIC:
		now_queuing = AHC_DEV_Q_BASIC;
		break;
	case AHC_QUEUE_TAGGED:
		now_queuing = AHC_DEV_Q_TAGGED;
		break;
	}
	if ((dev->flags & AHC_DEV_FREEZE_TIL_EMPTY) == 0
	 && (was_queuing != now_queuing)
	 && (dev->active != 0)) {
		dev->flags |= AHC_DEV_FREEZE_TIL_EMPTY;
		dev->qfrozen++;
	}

	dev->flags &= ~(AHC_DEV_Q_BASIC|AHC_DEV_Q_TAGGED|AHC_DEV_PERIODIC_OTAG);
	if (now_queuing) {
		u_int usertags;

		usertags = ahc_linux_user_tagdepth(ahc, devinfo);
		if (!was_queuing) {
			/*
			 * Start out agressively and allow our
			 * dynamic queue depth algorithm to take
			 * care of the rest.
			 */
			dev->maxtags = usertags;
			dev->openings = dev->maxtags - dev->active;
		}
		if (dev->maxtags == 0) {
			/*
			 * Queueing is disabled by the user.
			 */
			dev->openings = 1;
		} else if (alg == AHC_QUEUE_TAGGED) {
			dev->flags |= AHC_DEV_Q_TAGGED;
			if (aic7xxx_periodic_otag != 0)
				dev->flags |= AHC_DEV_PERIODIC_OTAG;
		} else
			dev->flags |= AHC_DEV_Q_BASIC;
	} else {
		/* We can only have one opening. */
		dev->maxtags = 0;
		dev->openings =  1 - dev->active;
	}
	switch ((dev->flags & (AHC_DEV_Q_BASIC|AHC_DEV_Q_TAGGED))) {
	case AHC_DEV_Q_BASIC:
		scsi_set_tag_type(sdev, MSG_SIMPLE_TAG);
		scsi_activate_tcq(sdev, dev->openings + dev->active);
		break;
	case AHC_DEV_Q_TAGGED:
		scsi_set_tag_type(sdev, MSG_ORDERED_TAG);
		scsi_activate_tcq(sdev, dev->openings + dev->active);
		break;
	default:
		/*
		 * We allow the OS to queue 2 untagged transactions to
		 * us at any time even though we can only execute them
		 * serially on the controller/device.  This should
		 * remove some latency.
		 */
		scsi_deactivate_tcq(sdev, 2);
		break;
	}
}

int
ahc_platform_abort_scbs(struct ahc_softc *ahc, int target, char channel,
			int lun, u_int tag, role_t role, uint32_t status)
{
	return 0;
}

static u_int
ahc_linux_user_tagdepth(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	static int warned_user;
	u_int tags;

	tags = 0;
	if ((ahc->user_discenable & devinfo->target_mask) != 0) {
		if (ahc->unit >= ARRAY_SIZE(aic7xxx_tag_info)) {
			if (warned_user == 0) {

				printf(KERN_WARNING
"aic7xxx: WARNING: Insufficient tag_info instances\n"
"aic7xxx: for installed controllers. Using defaults\n"
"aic7xxx: Please update the aic7xxx_tag_info array in\n"
"aic7xxx: the aic7xxx_osm..c source file.\n");
				warned_user++;
			}
			tags = AHC_MAX_QUEUE;
		} else {
			adapter_tag_info_t *tag_info;

			tag_info = &aic7xxx_tag_info[ahc->unit];
			tags = tag_info->tag_commands[devinfo->target_offset];
			if (tags > AHC_MAX_QUEUE)
				tags = AHC_MAX_QUEUE;
		}
	}
	return (tags);
}

/*
 * Determines the queue depth for a given device.
 */
static void
ahc_linux_device_queue_depth(struct scsi_device *sdev)
{
	struct	ahc_devinfo devinfo;
	u_int	tags;
	struct ahc_softc *ahc = *((struct ahc_softc **)sdev->host->hostdata);

	ahc_compile_devinfo(&devinfo,
			    sdev->sdev_target->channel == 0
			  ? ahc->our_id : ahc->our_id_b,
			    sdev->sdev_target->id, sdev->lun,
			    sdev->sdev_target->channel == 0 ? 'A' : 'B',
			    ROLE_INITIATOR);
	tags = ahc_linux_user_tagdepth(ahc, &devinfo);
	if (tags != 0 && sdev->tagged_supported != 0) {

		ahc_platform_set_tags(ahc, sdev, &devinfo, AHC_QUEUE_TAGGED);
		ahc_send_async(ahc, devinfo.channel, devinfo.target,
			       devinfo.lun, AC_TRANSFER_NEG);
		ahc_print_devinfo(ahc, &devinfo);
		printf("Tagged Queuing enabled.  Depth %d\n", tags);
	} else {
		ahc_platform_set_tags(ahc, sdev, &devinfo, AHC_QUEUE_NONE);
		ahc_send_async(ahc, devinfo.channel, devinfo.target,
			       devinfo.lun, AC_TRANSFER_NEG);
	}
}

static int
ahc_linux_run_command(struct ahc_softc *ahc, struct ahc_linux_device *dev,
		      struct scsi_cmnd *cmd)
{
	struct	 scb *scb;
	struct	 hardware_scb *hscb;
	struct	 ahc_initiator_tinfo *tinfo;
	struct	 ahc_tmode_tstate *tstate;
	uint16_t mask;
	struct scb_tailq *untagged_q = NULL;
	int nseg;

	/*
	 * Schedule us to run later.  The only reason we are not
	 * running is because the whole controller Q is frozen.
	 */
	if (ahc->platform_data->qfrozen != 0)
		return SCSI_MLQUEUE_HOST_BUSY;

	/*
	 * We only allow one untagged transaction
	 * per target in the initiator role unless
	 * we are storing a full busy target *lun*
	 * table in SCB space.
	 */
	if (!blk_rq_tagged(cmd->request)
	    && (ahc->features & AHC_SCB_BTT) == 0) {
		int target_offset;

		target_offset = cmd->device->id + cmd->device->channel * 8;
		untagged_q = &(ahc->untagged_queues[target_offset]);
		if (!TAILQ_EMPTY(untagged_q))
			/* if we're already executing an untagged command
			 * we're busy to another */
			return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	/*
	 * Get an scb to use.
	 */
	scb = ahc_get_scb(ahc);
	if (!scb)
		return SCSI_MLQUEUE_HOST_BUSY;

	scb->io_ctx = cmd;
	scb->platform_data->dev = dev;
	hscb = scb->hscb;
	cmd->host_scribble = (char *)scb;

	/*
	 * Fill out basics of the HSCB.
	 */
	hscb->control = 0;
	hscb->scsiid = BUILD_SCSIID(ahc, cmd);
	hscb->lun = cmd->device->lun;
	mask = SCB_GET_TARGET_MASK(ahc, scb);
	tinfo = ahc_fetch_transinfo(ahc, SCB_GET_CHANNEL(ahc, scb),
				    SCB_GET_OUR_ID(scb),
				    SCB_GET_TARGET(ahc, scb), &tstate);
	hscb->scsirate = tinfo->scsirate;
	hscb->scsioffset = tinfo->curr.offset;
	if ((tstate->ultraenb & mask) != 0)
		hscb->control |= ULTRAENB;
	
	if ((ahc->user_discenable & mask) != 0)
		hscb->control |= DISCENB;
	
	if ((tstate->auto_negotiate & mask) != 0) {
		scb->flags |= SCB_AUTO_NEGOTIATE;
		scb->hscb->control |= MK_MESSAGE;
	}

	if ((dev->flags & (AHC_DEV_Q_TAGGED|AHC_DEV_Q_BASIC)) != 0) {
		int	msg_bytes;
		uint8_t tag_msgs[2];
		
		msg_bytes = scsi_populate_tag_msg(cmd, tag_msgs);
		if (msg_bytes && tag_msgs[0] != MSG_SIMPLE_TASK) {
			hscb->control |= tag_msgs[0];
			if (tag_msgs[0] == MSG_ORDERED_TASK)
				dev->commands_since_idle_or_otag = 0;
		} else if (dev->commands_since_idle_or_otag == AHC_OTAG_THRESH
				&& (dev->flags & AHC_DEV_Q_TAGGED) != 0) {
			hscb->control |= MSG_ORDERED_TASK;
			dev->commands_since_idle_or_otag = 0;
		} else {
			hscb->control |= MSG_SIMPLE_TASK;
		}
	}

	hscb->cdb_len = cmd->cmd_len;
	if (hscb->cdb_len <= 12) {
		memcpy(hscb->shared_data.cdb, cmd->cmnd, hscb->cdb_len);
	} else {
		memcpy(hscb->cdb32, cmd->cmnd, hscb->cdb_len);
		scb->flags |= SCB_CDB32_PTR;
	}

	scb->platform_data->xfer_len = 0;
	ahc_set_residual(scb, 0);
	ahc_set_sense_residual(scb, 0);
	scb->sg_count = 0;

	nseg = scsi_dma_map(cmd);
	BUG_ON(nseg < 0);
	if (nseg > 0) {
		struct	ahc_dma_seg *sg;
		struct	scatterlist *cur_seg;
		int i;

		/* Copy the segments into the SG list. */
		sg = scb->sg_list;
		/*
		 * The sg_count may be larger than nseg if
		 * a transfer crosses a 32bit page.
		 */
		scsi_for_each_sg(cmd, cur_seg, nseg, i) {
			dma_addr_t addr;
			bus_size_t len;
			int consumed;

			addr = sg_dma_address(cur_seg);
			len = sg_dma_len(cur_seg);
			consumed = ahc_linux_map_seg(ahc, scb,
						     sg, addr, len);
			sg += consumed;
			scb->sg_count += consumed;
		}
		sg--;
		sg->len |= ahc_htole32(AHC_DMA_LAST_SEG);

		/*
		 * Reset the sg list pointer.
		 */
		scb->hscb->sgptr =
			ahc_htole32(scb->sg_list_phys | SG_FULL_RESID);
		
		/*
		 * Copy the first SG into the "current"
		 * data pointer area.
		 */
		scb->hscb->dataptr = scb->sg_list->addr;
		scb->hscb->datacnt = scb->sg_list->len;
	} else {
		scb->hscb->sgptr = ahc_htole32(SG_LIST_NULL);
		scb->hscb->dataptr = 0;
		scb->hscb->datacnt = 0;
		scb->sg_count = 0;
	}

	LIST_INSERT_HEAD(&ahc->pending_scbs, scb, pending_links);
	dev->openings--;
	dev->active++;
	dev->commands_issued++;
	if ((dev->flags & AHC_DEV_PERIODIC_OTAG) != 0)
		dev->commands_since_idle_or_otag++;
	
	scb->flags |= SCB_ACTIVE;
	if (untagged_q) {
		TAILQ_INSERT_TAIL(untagged_q, scb, links.tqe);
		scb->flags |= SCB_UNTAGGEDQ;
	}
	ahc_queue_scb(ahc, scb);
	return 0;
}

/*
 * SCSI controller interrupt handler.
 */
irqreturn_t
ahc_linux_isr(int irq, void *dev_id)
{
	struct	ahc_softc *ahc;
	u_long	flags;
	int	ours;

	ahc = (struct ahc_softc *) dev_id;
	ahc_lock(ahc, &flags); 
	ours = ahc_intr(ahc);
	ahc_unlock(ahc, &flags);
	return IRQ_RETVAL(ours);
}

void
ahc_platform_flushwork(struct ahc_softc *ahc)
{

}

void
ahc_send_async(struct ahc_softc *ahc, char channel,
	       u_int target, u_int lun, ac_code code)
{
	switch (code) {
	case AC_TRANSFER_NEG:
	{
		char	buf[80];
		struct	scsi_target *starget;
		struct	ahc_linux_target *targ;
		struct	info_str info;
		struct	ahc_initiator_tinfo *tinfo;
		struct	ahc_tmode_tstate *tstate;
		int	target_offset;
		unsigned int target_ppr_options;

		BUG_ON(target == CAM_TARGET_WILDCARD);

		info.buffer = buf;
		info.length = sizeof(buf);
		info.offset = 0;
		info.pos = 0;
		tinfo = ahc_fetch_transinfo(ahc, channel,
						channel == 'A' ? ahc->our_id
							       : ahc->our_id_b,
						target, &tstate);

		/*
		 * Don't bother reporting results while
		 * negotiations are still pending.
		 */
		if (tinfo->curr.period != tinfo->goal.period
		 || tinfo->curr.width != tinfo->goal.width
		 || tinfo->curr.offset != tinfo->goal.offset
		 || tinfo->curr.ppr_options != tinfo->goal.ppr_options)
			if (bootverbose == 0)
				break;

		/*
		 * Don't bother reporting results that
		 * are identical to those last reported.
		 */
		target_offset = target;
		if (channel == 'B')
			target_offset += 8;
		starget = ahc->platform_data->starget[target_offset];
		if (starget == NULL)
			break;
		targ = scsi_transport_target_data(starget);

		target_ppr_options =
			(spi_dt(starget) ? MSG_EXT_PPR_DT_REQ : 0)
			+ (spi_qas(starget) ? MSG_EXT_PPR_QAS_REQ : 0)
			+ (spi_iu(starget) ?  MSG_EXT_PPR_IU_REQ : 0);

		if (tinfo->curr.period == spi_period(starget)
		    && tinfo->curr.width == spi_width(starget)
		    && tinfo->curr.offset == spi_offset(starget)
		 && tinfo->curr.ppr_options == target_ppr_options)
			if (bootverbose == 0)
				break;

		spi_period(starget) = tinfo->curr.period;
		spi_width(starget) = tinfo->curr.width;
		spi_offset(starget) = tinfo->curr.offset;
		spi_dt(starget) = tinfo->curr.ppr_options & MSG_EXT_PPR_DT_REQ ? 1 : 0;
		spi_qas(starget) = tinfo->curr.ppr_options & MSG_EXT_PPR_QAS_REQ ? 1 : 0;
		spi_iu(starget) = tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ ? 1 : 0;
		spi_display_xfer_agreement(starget);
		break;
	}
        case AC_SENT_BDR:
	{
		WARN_ON(lun != CAM_LUN_WILDCARD);
		scsi_report_device_reset(ahc->platform_data->host,
					 channel - 'A', target);
		break;
	}
        case AC_BUS_RESET:
		if (ahc->platform_data->host != NULL) {
			scsi_report_bus_reset(ahc->platform_data->host,
					      channel - 'A');
		}
                break;
        default:
                panic("ahc_send_async: Unexpected async event");
        }
}

/*
 * Calls the higher level scsi done function and frees the scb.
 */
void
ahc_done(struct ahc_softc *ahc, struct scb *scb)
{
	struct scsi_cmnd *cmd;
	struct	   ahc_linux_device *dev;

	LIST_REMOVE(scb, pending_links);
	if ((scb->flags & SCB_UNTAGGEDQ) != 0) {
		struct scb_tailq *untagged_q;
		int target_offset;

		target_offset = SCB_GET_TARGET_OFFSET(ahc, scb);
		untagged_q = &(ahc->untagged_queues[target_offset]);
		TAILQ_REMOVE(untagged_q, scb, links.tqe);
		BUG_ON(!TAILQ_EMPTY(untagged_q));
	}

	if ((scb->flags & SCB_ACTIVE) == 0) {
		printf("SCB %d done'd twice\n", scb->hscb->tag);
		ahc_dump_card_state(ahc);
		panic("Stopping for safety");
	}
	cmd = scb->io_ctx;
	dev = scb->platform_data->dev;
	dev->active--;
	dev->openings++;
	if ((cmd->result & (CAM_DEV_QFRZN << 16)) != 0) {
		cmd->result &= ~(CAM_DEV_QFRZN << 16);
		dev->qfrozen--;
	}
	ahc_linux_unmap_scb(ahc, scb);

	/*
	 * Guard against stale sense data.
	 * The Linux mid-layer assumes that sense
	 * was retrieved anytime the first byte of
	 * the sense buffer looks "sane".
	 */
	cmd->sense_buffer[0] = 0;
	if (ahc_get_transaction_status(scb) == CAM_REQ_INPROG) {
		uint32_t amount_xferred;

		amount_xferred =
		    ahc_get_transfer_length(scb) - ahc_get_residual(scb);
		if ((scb->flags & SCB_TRANSMISSION_ERROR) != 0) {
#ifdef AHC_DEBUG
			if ((ahc_debug & AHC_SHOW_MISC) != 0) {
				ahc_print_path(ahc, scb);
				printf("Set CAM_UNCOR_PARITY\n");
			}
#endif
			ahc_set_transaction_status(scb, CAM_UNCOR_PARITY);
#ifdef AHC_REPORT_UNDERFLOWS
		/*
		 * This code is disabled by default as some
		 * clients of the SCSI system do not properly
		 * initialize the underflow parameter.  This
		 * results in spurious termination of commands
		 * that complete as expected (e.g. underflow is
		 * allowed as command can return variable amounts
		 * of data.
		 */
		} else if (amount_xferred < scb->io_ctx->underflow) {
			u_int i;

			ahc_print_path(ahc, scb);
			printf("CDB:");
			for (i = 0; i < scb->io_ctx->cmd_len; i++)
				printf(" 0x%x", scb->io_ctx->cmnd[i]);
			printf("\n");
			ahc_print_path(ahc, scb);
			printf("Saw underflow (%ld of %ld bytes). "
			       "Treated as error\n",
				ahc_get_residual(scb),
				ahc_get_transfer_length(scb));
			ahc_set_transaction_status(scb, CAM_DATA_RUN_ERR);
#endif
		} else {
			ahc_set_transaction_status(scb, CAM_REQ_CMP);
		}
	} else if (ahc_get_transaction_status(scb) == CAM_SCSI_STATUS_ERROR) {
		ahc_linux_handle_scsi_status(ahc, cmd->device, scb);
	}

	if (dev->openings == 1
	 && ahc_get_transaction_status(scb) == CAM_REQ_CMP
	 && ahc_get_scsi_status(scb) != SCSI_STATUS_QUEUE_FULL)
		dev->tag_success_count++;
	/*
	 * Some devices deal with temporary internal resource
	 * shortages by returning queue full.  When the queue
	 * full occurrs, we throttle back.  Slowly try to get
	 * back to our previous queue depth.
	 */
	if ((dev->openings + dev->active) < dev->maxtags
	 && dev->tag_success_count > AHC_TAG_SUCCESS_INTERVAL) {
		dev->tag_success_count = 0;
		dev->openings++;
	}

	if (dev->active == 0)
		dev->commands_since_idle_or_otag = 0;

	if ((scb->flags & SCB_RECOVERY_SCB) != 0) {
		printf("Recovery SCB completes\n");
		if (ahc_get_transaction_status(scb) == CAM_BDR_SENT
		 || ahc_get_transaction_status(scb) == CAM_REQ_ABORTED)
			ahc_set_transaction_status(scb, CAM_CMD_TIMEOUT);

		if (ahc->platform_data->eh_done)
			complete(ahc->platform_data->eh_done);
	}

	ahc_free_scb(ahc, scb);
	ahc_linux_queue_cmd_complete(ahc, cmd);
}

static void
ahc_linux_handle_scsi_status(struct ahc_softc *ahc,
			     struct scsi_device *sdev, struct scb *scb)
{
	struct	ahc_devinfo devinfo;
	struct ahc_linux_device *dev = scsi_transport_device_data(sdev);

	ahc_compile_devinfo(&devinfo,
			    ahc->our_id,
			    sdev->sdev_target->id, sdev->lun,
			    sdev->sdev_target->channel == 0 ? 'A' : 'B',
			    ROLE_INITIATOR);
	
	/*
	 * We don't currently trust the mid-layer to
	 * properly deal with queue full or busy.  So,
	 * when one occurs, we tell the mid-layer to
	 * unconditionally requeue the command to us
	 * so that we can retry it ourselves.  We also
	 * implement our own throttling mechanism so
	 * we don't clobber the device with too many
	 * commands.
	 */
	switch (ahc_get_scsi_status(scb)) {
	default:
		break;
	case SCSI_STATUS_CHECK_COND:
	case SCSI_STATUS_CMD_TERMINATED:
	{
		struct scsi_cmnd *cmd;

		/*
		 * Copy sense information to the OS's cmd
		 * structure if it is available.
		 */
		cmd = scb->io_ctx;
		if (scb->flags & SCB_SENSE) {
			u_int sense_size;

			sense_size = min(sizeof(struct scsi_sense_data)
				       - ahc_get_sense_residual(scb),
					 (u_long)sizeof(cmd->sense_buffer));
			memcpy(cmd->sense_buffer,
			       ahc_get_sense_buf(ahc, scb), sense_size);
			if (sense_size < sizeof(cmd->sense_buffer))
				memset(&cmd->sense_buffer[sense_size], 0,
				       sizeof(cmd->sense_buffer) - sense_size);
			cmd->result |= (DRIVER_SENSE << 24);
#ifdef AHC_DEBUG
			if (ahc_debug & AHC_SHOW_SENSE) {
				int i;

				printf("Copied %d bytes of sense data:",
				       sense_size);
				for (i = 0; i < sense_size; i++) {
					if ((i & 0xF) == 0)
						printf("\n");
					printf("0x%x ", cmd->sense_buffer[i]);
				}
				printf("\n");
			}
#endif
		}
		break;
	}
	case SCSI_STATUS_QUEUE_FULL:
	{
		/*
		 * By the time the core driver has returned this
		 * command, all other commands that were queued
		 * to us but not the device have been returned.
		 * This ensures that dev->active is equal to
		 * the number of commands actually queued to
		 * the device.
		 */
		dev->tag_success_count = 0;
		if (dev->active != 0) {
			/*
			 * Drop our opening count to the number
			 * of commands currently outstanding.
			 */
			dev->openings = 0;
/*
			ahc_print_path(ahc, scb);
			printf("Dropping tag count to %d\n", dev->active);
 */
			if (dev->active == dev->tags_on_last_queuefull) {

				dev->last_queuefull_same_count++;
				/*
				 * If we repeatedly see a queue full
				 * at the same queue depth, this
				 * device has a fixed number of tag
				 * slots.  Lock in this tag depth
				 * so we stop seeing queue fulls from
				 * this device.
				 */
				if (dev->last_queuefull_same_count
				 == AHC_LOCK_TAGS_COUNT) {
					dev->maxtags = dev->active;
					ahc_print_path(ahc, scb);
					printf("Locking max tag count at %d\n",
					       dev->active);
				}
			} else {
				dev->tags_on_last_queuefull = dev->active;
				dev->last_queuefull_same_count = 0;
			}
			ahc_set_transaction_status(scb, CAM_REQUEUE_REQ);
			ahc_set_scsi_status(scb, SCSI_STATUS_OK);
			ahc_platform_set_tags(ahc, sdev, &devinfo,
				     (dev->flags & AHC_DEV_Q_BASIC)
				   ? AHC_QUEUE_BASIC : AHC_QUEUE_TAGGED);
			break;
		}
		/*
		 * Drop down to a single opening, and treat this
		 * as if the target returned BUSY SCSI status.
		 */
		dev->openings = 1;
		ahc_set_scsi_status(scb, SCSI_STATUS_BUSY);
		ahc_platform_set_tags(ahc, sdev, &devinfo,
			     (dev->flags & AHC_DEV_Q_BASIC)
			   ? AHC_QUEUE_BASIC : AHC_QUEUE_TAGGED);
		break;
	}
	}
}

static void
ahc_linux_queue_cmd_complete(struct ahc_softc *ahc, struct scsi_cmnd *cmd)
{
	/*
	 * Map CAM error codes into Linux Error codes.  We
	 * avoid the conversion so that the DV code has the
	 * full error information available when making
	 * state change decisions.
	 */
	{
		u_int new_status;

		switch (ahc_cmd_get_transaction_status(cmd)) {
		case CAM_REQ_INPROG:
		case CAM_REQ_CMP:
		case CAM_SCSI_STATUS_ERROR:
			new_status = DID_OK;
			break;
		case CAM_REQ_ABORTED:
			new_status = DID_ABORT;
			break;
		case CAM_BUSY:
			new_status = DID_BUS_BUSY;
			break;
		case CAM_REQ_INVALID:
		case CAM_PATH_INVALID:
			new_status = DID_BAD_TARGET;
			break;
		case CAM_SEL_TIMEOUT:
			new_status = DID_NO_CONNECT;
			break;
		case CAM_SCSI_BUS_RESET:
		case CAM_BDR_SENT:
			new_status = DID_RESET;
			break;
		case CAM_UNCOR_PARITY:
			new_status = DID_PARITY;
			break;
		case CAM_CMD_TIMEOUT:
			new_status = DID_TIME_OUT;
			break;
		case CAM_UA_ABORT:
		case CAM_REQ_CMP_ERR:
		case CAM_AUTOSENSE_FAIL:
		case CAM_NO_HBA:
		case CAM_DATA_RUN_ERR:
		case CAM_UNEXP_BUSFREE:
		case CAM_SEQUENCE_FAIL:
		case CAM_CCB_LEN_ERR:
		case CAM_PROVIDE_FAIL:
		case CAM_REQ_TERMIO:
		case CAM_UNREC_HBA_ERROR:
		case CAM_REQ_TOO_BIG:
			new_status = DID_ERROR;
			break;
		case CAM_REQUEUE_REQ:
			new_status = DID_REQUEUE;
			break;
		default:
			/* We should never get here */
			new_status = DID_ERROR;
			break;
		}

		ahc_cmd_set_transaction_status(cmd, new_status);
	}

	cmd->scsi_done(cmd);
}

static void
ahc_linux_freeze_simq(struct ahc_softc *ahc)
{
	unsigned long s;

	ahc_lock(ahc, &s);
	ahc->platform_data->qfrozen++;
	if (ahc->platform_data->qfrozen == 1) {
		scsi_block_requests(ahc->platform_data->host);

		/* XXX What about Twin channels? */
		ahc_platform_abort_scbs(ahc, CAM_TARGET_WILDCARD, ALL_CHANNELS,
					CAM_LUN_WILDCARD, SCB_LIST_NULL,
					ROLE_INITIATOR, CAM_REQUEUE_REQ);
	}
	ahc_unlock(ahc, &s);
}

static void
ahc_linux_release_simq(struct ahc_softc *ahc)
{
	u_long s;
	int    unblock_reqs;

	unblock_reqs = 0;
	ahc_lock(ahc, &s);
	if (ahc->platform_data->qfrozen > 0)
		ahc->platform_data->qfrozen--;
	if (ahc->platform_data->qfrozen == 0)
		unblock_reqs = 1;
	ahc_unlock(ahc, &s);
	/*
	 * There is still a race here.  The mid-layer
	 * should keep its own freeze count and use
	 * a bottom half handler to run the queues
	 * so we can unblock with our own lock held.
	 */
	if (unblock_reqs)
		scsi_unblock_requests(ahc->platform_data->host);
}

static int
ahc_linux_queue_recovery_cmd(struct scsi_cmnd *cmd, scb_flag flag)
{
	struct ahc_softc *ahc;
	struct ahc_linux_device *dev;
	struct scb *pending_scb;
	u_int  saved_scbptr;
	u_int  active_scb_index;
	u_int  last_phase;
	u_int  saved_scsiid;
	u_int  cdb_byte;
	int    retval;
	int    was_paused;
	int    paused;
	int    wait;
	int    disconnected;
	unsigned long flags;

	pending_scb = NULL;
	paused = FALSE;
	wait = FALSE;
	ahc = *(struct ahc_softc **)cmd->device->host->hostdata;

	scmd_printk(KERN_INFO, cmd, "Attempting to queue a%s message\n",
	       flag == SCB_ABORT ? "n ABORT" : " TARGET RESET");

	printf("CDB:");
	for (cdb_byte = 0; cdb_byte < cmd->cmd_len; cdb_byte++)
		printf(" 0x%x", cmd->cmnd[cdb_byte]);
	printf("\n");

	ahc_lock(ahc, &flags);

	/*
	 * First determine if we currently own this command.
	 * Start by searching the device queue.  If not found
	 * there, check the pending_scb list.  If not found
	 * at all, and the system wanted us to just abort the
	 * command, return success.
	 */
	dev = scsi_transport_device_data(cmd->device);

	if (dev == NULL) {
		/*
		 * No target device for this command exists,
		 * so we must not still own the command.
		 */
		printf("%s:%d:%d:%d: Is not an active device\n",
		       ahc_name(ahc), cmd->device->channel, cmd->device->id,
		       cmd->device->lun);
		retval = SUCCESS;
		goto no_cmd;
	}

	if ((dev->flags & (AHC_DEV_Q_BASIC|AHC_DEV_Q_TAGGED)) == 0
	 && ahc_search_untagged_queues(ahc, cmd, cmd->device->id,
				       cmd->device->channel + 'A',
				       cmd->device->lun,
				       CAM_REQ_ABORTED, SEARCH_COMPLETE) != 0) {
		printf("%s:%d:%d:%d: Command found on untagged queue\n",
		       ahc_name(ahc), cmd->device->channel, cmd->device->id,
		       cmd->device->lun);
		retval = SUCCESS;
		goto done;
	}

	/*
	 * See if we can find a matching cmd in the pending list.
	 */
	LIST_FOREACH(pending_scb, &ahc->pending_scbs, pending_links) {
		if (pending_scb->io_ctx == cmd)
			break;
	}

	if (pending_scb == NULL && flag == SCB_DEVICE_RESET) {

		/* Any SCB for this device will do for a target reset */
		LIST_FOREACH(pending_scb, &ahc->pending_scbs, pending_links) {
		  	if (ahc_match_scb(ahc, pending_scb, scmd_id(cmd),
					  scmd_channel(cmd) + 'A',
					  CAM_LUN_WILDCARD,
					  SCB_LIST_NULL, ROLE_INITIATOR))
				break;
		}
	}

	if (pending_scb == NULL) {
		scmd_printk(KERN_INFO, cmd, "Command not found\n");
		goto no_cmd;
	}

	if ((pending_scb->flags & SCB_RECOVERY_SCB) != 0) {
		/*
		 * We can't queue two recovery actions using the same SCB
		 */
		retval = FAILED;
		goto  done;
	}

	/*
	 * Ensure that the card doesn't do anything
	 * behind our back and that we didn't "just" miss
	 * an interrupt that would affect this cmd.
	 */
	was_paused = ahc_is_paused(ahc);
	ahc_pause_and_flushwork(ahc);
	paused = TRUE;

	if ((pending_scb->flags & SCB_ACTIVE) == 0) {
		scmd_printk(KERN_INFO, cmd, "Command already completed\n");
		goto no_cmd;
	}

	printf("%s: At time of recovery, card was %spaused\n",
	       ahc_name(ahc), was_paused ? "" : "not ");
	ahc_dump_card_state(ahc);

	disconnected = TRUE;
	if (flag == SCB_ABORT) {
		if (ahc_search_qinfifo(ahc, cmd->device->id,
				       cmd->device->channel + 'A',
				       cmd->device->lun,
				       pending_scb->hscb->tag,
				       ROLE_INITIATOR, CAM_REQ_ABORTED,
				       SEARCH_COMPLETE) > 0) {
			printf("%s:%d:%d:%d: Cmd aborted from QINFIFO\n",
			       ahc_name(ahc), cmd->device->channel,
					cmd->device->id, cmd->device->lun);
			retval = SUCCESS;
			goto done;
		}
	} else if (ahc_search_qinfifo(ahc, cmd->device->id,
				      cmd->device->channel + 'A',
				      cmd->device->lun, pending_scb->hscb->tag,
				      ROLE_INITIATOR, /*status*/0,
				      SEARCH_COUNT) > 0) {
		disconnected = FALSE;
	}

	if (disconnected && (ahc_inb(ahc, SEQ_FLAGS) & NOT_IDENTIFIED) == 0) {
		struct scb *bus_scb;

		bus_scb = ahc_lookup_scb(ahc, ahc_inb(ahc, SCB_TAG));
		if (bus_scb == pending_scb)
			disconnected = FALSE;
		else if (flag != SCB_ABORT
		      && ahc_inb(ahc, SAVED_SCSIID) == pending_scb->hscb->scsiid
		      && ahc_inb(ahc, SAVED_LUN) == SCB_GET_LUN(pending_scb))
			disconnected = FALSE;
	}

	/*
	 * At this point, pending_scb is the scb associated with the
	 * passed in command.  That command is currently active on the
	 * bus, is in the disconnected state, or we're hoping to find
	 * a command for the same target active on the bus to abuse to
	 * send a BDR.  Queue the appropriate message based on which of
	 * these states we are in.
	 */
	last_phase = ahc_inb(ahc, LASTPHASE);
	saved_scbptr = ahc_inb(ahc, SCBPTR);
	active_scb_index = ahc_inb(ahc, SCB_TAG);
	saved_scsiid = ahc_inb(ahc, SAVED_SCSIID);
	if (last_phase != P_BUSFREE
	 && (pending_scb->hscb->tag == active_scb_index
	  || (flag == SCB_DEVICE_RESET
	   && SCSIID_TARGET(ahc, saved_scsiid) == scmd_id(cmd)))) {

		/*
		 * We're active on the bus, so assert ATN
		 * and hope that the target responds.
		 */
		pending_scb = ahc_lookup_scb(ahc, active_scb_index);
		pending_scb->flags |= SCB_RECOVERY_SCB|flag;
		ahc_outb(ahc, MSG_OUT, HOST_MSG);
		ahc_outb(ahc, SCSISIGO, last_phase|ATNO);
		scmd_printk(KERN_INFO, cmd, "Device is active, asserting ATN\n");
		wait = TRUE;
	} else if (disconnected) {

		/*
		 * Actually re-queue this SCB in an attempt
		 * to select the device before it reconnects.
		 * In either case (selection or reselection),
		 * we will now issue the approprate message
		 * to the timed-out device.
		 *
		 * Set the MK_MESSAGE control bit indicating
		 * that we desire to send a message.  We
		 * also set the disconnected flag since
		 * in the paging case there is no guarantee
		 * that our SCB control byte matches the
		 * version on the card.  We don't want the
		 * sequencer to abort the command thinking
		 * an unsolicited reselection occurred.
		 */
		pending_scb->hscb->control |= MK_MESSAGE|DISCONNECTED;
		pending_scb->flags |= SCB_RECOVERY_SCB|flag;

		/*
		 * Remove any cached copy of this SCB in the
		 * disconnected list in preparation for the
		 * queuing of our abort SCB.  We use the
		 * same element in the SCB, SCB_NEXT, for
		 * both the qinfifo and the disconnected list.
		 */
		ahc_search_disc_list(ahc, cmd->device->id,
				     cmd->device->channel + 'A',
				     cmd->device->lun, pending_scb->hscb->tag,
				     /*stop_on_first*/TRUE,
				     /*remove*/TRUE,
				     /*save_state*/FALSE);

		/*
		 * In the non-paging case, the sequencer will
		 * never re-reference the in-core SCB.
		 * To make sure we are notified during
		 * reslection, set the MK_MESSAGE flag in
		 * the card's copy of the SCB.
		 */
		if ((ahc->flags & AHC_PAGESCBS) == 0) {
			ahc_outb(ahc, SCBPTR, pending_scb->hscb->tag);
			ahc_outb(ahc, SCB_CONTROL,
				 ahc_inb(ahc, SCB_CONTROL)|MK_MESSAGE);
		}

		/*
		 * Clear out any entries in the QINFIFO first
		 * so we are the next SCB for this target
		 * to run.
		 */
		ahc_search_qinfifo(ahc, cmd->device->id,
				   cmd->device->channel + 'A',
				   cmd->device->lun, SCB_LIST_NULL,
				   ROLE_INITIATOR, CAM_REQUEUE_REQ,
				   SEARCH_COMPLETE);
		ahc_qinfifo_requeue_tail(ahc, pending_scb);
		ahc_outb(ahc, SCBPTR, saved_scbptr);
		ahc_print_path(ahc, pending_scb);
		printf("Device is disconnected, re-queuing SCB\n");
		wait = TRUE;
	} else {
		scmd_printk(KERN_INFO, cmd, "Unable to deliver message\n");
		retval = FAILED;
		goto done;
	}

no_cmd:
	/*
	 * Our assumption is that if we don't have the command, no
	 * recovery action was required, so we return success.  Again,
	 * the semantics of the mid-layer recovery engine are not
	 * well defined, so this may change in time.
	 */
	retval = SUCCESS;
done:
	if (paused)
		ahc_unpause(ahc);
	if (wait) {
		DECLARE_COMPLETION_ONSTACK(done);

		ahc->platform_data->eh_done = &done;
		ahc_unlock(ahc, &flags);

		printf("Recovery code sleeping\n");
		if (!wait_for_completion_timeout(&done, 5 * HZ)) {
			ahc_lock(ahc, &flags);
			ahc->platform_data->eh_done = NULL;
			ahc_unlock(ahc, &flags);

			printf("Timer Expired\n");
			retval = FAILED;
		}
		printf("Recovery code awake\n");
	} else
		ahc_unlock(ahc, &flags);
	return (retval);
}

void
ahc_platform_dump_card_state(struct ahc_softc *ahc)
{
}

static void ahc_linux_set_width(struct scsi_target *starget, int width)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahc_softc *ahc = *((struct ahc_softc **)shost->hostdata);
	struct ahc_devinfo devinfo;
	unsigned long flags;

	ahc_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahc_lock(ahc, &flags);
	ahc_set_width(ahc, &devinfo, width, AHC_TRANS_GOAL, FALSE);
	ahc_unlock(ahc, &flags);
}

static void ahc_linux_set_period(struct scsi_target *starget, int period)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahc_softc *ahc = *((struct ahc_softc **)shost->hostdata);
	struct ahc_tmode_tstate *tstate;
	struct ahc_initiator_tinfo *tinfo 
		= ahc_fetch_transinfo(ahc,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahc_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options;
	unsigned long flags;
	unsigned long offset = tinfo->goal.offset;
	struct ahc_syncrate *syncrate;

	if (offset == 0)
		offset = MAX_OFFSET;

	if (period < 9)
		period = 9;	/* 12.5ns is our minimum */
	if (period == 9) {
		if (spi_max_width(starget))
			ppr_options |= MSG_EXT_PPR_DT_REQ;
		else
			/* need wide for DT and need DT for 12.5 ns */
			period = 10;
	}

	ahc_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);

	/* all PPR requests apart from QAS require wide transfers */
	if (ppr_options & ~MSG_EXT_PPR_QAS_REQ) {
		if (spi_width(starget) == 0)
			ppr_options &= MSG_EXT_PPR_QAS_REQ;
	}

	syncrate = ahc_find_syncrate(ahc, &period, &ppr_options, AHC_SYNCRATE_DT);
	ahc_lock(ahc, &flags);
	ahc_set_syncrate(ahc, &devinfo, syncrate, period, offset,
			 ppr_options, AHC_TRANS_GOAL, FALSE);
	ahc_unlock(ahc, &flags);
}

static void ahc_linux_set_offset(struct scsi_target *starget, int offset)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahc_softc *ahc = *((struct ahc_softc **)shost->hostdata);
	struct ahc_tmode_tstate *tstate;
	struct ahc_initiator_tinfo *tinfo 
		= ahc_fetch_transinfo(ahc,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahc_devinfo devinfo;
	unsigned int ppr_options = 0;
	unsigned int period = 0;
	unsigned long flags;
	struct ahc_syncrate *syncrate = NULL;

	ahc_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	if (offset != 0) {
		syncrate = ahc_find_syncrate(ahc, &period, &ppr_options, AHC_SYNCRATE_DT);
		period = tinfo->goal.period;
		ppr_options = tinfo->goal.ppr_options;
	}
	ahc_lock(ahc, &flags);
	ahc_set_syncrate(ahc, &devinfo, syncrate, period, offset,
			 ppr_options, AHC_TRANS_GOAL, FALSE);
	ahc_unlock(ahc, &flags);
}

static void ahc_linux_set_dt(struct scsi_target *starget, int dt)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahc_softc *ahc = *((struct ahc_softc **)shost->hostdata);
	struct ahc_tmode_tstate *tstate;
	struct ahc_initiator_tinfo *tinfo 
		= ahc_fetch_transinfo(ahc,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahc_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_DT_REQ;
	unsigned int period = tinfo->goal.period;
	unsigned int width = tinfo->goal.width;
	unsigned long flags;
	struct ahc_syncrate *syncrate;

	if (dt && spi_max_width(starget)) {
		ppr_options |= MSG_EXT_PPR_DT_REQ;
		if (!width)
			ahc_linux_set_width(starget, 1);
	} else if (period == 9)
		period = 10;	/* if resetting DT, period must be >= 25ns */

	ahc_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	syncrate = ahc_find_syncrate(ahc, &period, &ppr_options,AHC_SYNCRATE_DT);
	ahc_lock(ahc, &flags);
	ahc_set_syncrate(ahc, &devinfo, syncrate, period, tinfo->goal.offset,
			 ppr_options, AHC_TRANS_GOAL, FALSE);
	ahc_unlock(ahc, &flags);
}

#if 0
/* FIXME: This code claims to support IU and QAS.  However, the actual
 * sequencer code and aic7xxx_core have no support for these parameters and
 * will get into a bad state if they're negotiated.  Do not enable this
 * unless you know what you're doing */
static void ahc_linux_set_qas(struct scsi_target *starget, int qas)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahc_softc *ahc = *((struct ahc_softc **)shost->hostdata);
	struct ahc_tmode_tstate *tstate;
	struct ahc_initiator_tinfo *tinfo 
		= ahc_fetch_transinfo(ahc,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahc_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_QAS_REQ;
	unsigned int period = tinfo->goal.period;
	unsigned long flags;
	struct ahc_syncrate *syncrate;

	if (qas)
		ppr_options |= MSG_EXT_PPR_QAS_REQ;

	ahc_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	syncrate = ahc_find_syncrate(ahc, &period, &ppr_options, AHC_SYNCRATE_DT);
	ahc_lock(ahc, &flags);
	ahc_set_syncrate(ahc, &devinfo, syncrate, period, tinfo->goal.offset,
			 ppr_options, AHC_TRANS_GOAL, FALSE);
	ahc_unlock(ahc, &flags);
}

static void ahc_linux_set_iu(struct scsi_target *starget, int iu)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahc_softc *ahc = *((struct ahc_softc **)shost->hostdata);
	struct ahc_tmode_tstate *tstate;
	struct ahc_initiator_tinfo *tinfo 
		= ahc_fetch_transinfo(ahc,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahc_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_IU_REQ;
	unsigned int period = tinfo->goal.period;
	unsigned long flags;
	struct ahc_syncrate *syncrate;

	if (iu)
		ppr_options |= MSG_EXT_PPR_IU_REQ;

	ahc_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	syncrate = ahc_find_syncrate(ahc, &period, &ppr_options, AHC_SYNCRATE_DT);
	ahc_lock(ahc, &flags);
	ahc_set_syncrate(ahc, &devinfo, syncrate, period, tinfo->goal.offset,
			 ppr_options, AHC_TRANS_GOAL, FALSE);
	ahc_unlock(ahc, &flags);
}
#endif

static void ahc_linux_get_signalling(struct Scsi_Host *shost)
{
	struct ahc_softc *ahc = *(struct ahc_softc **)shost->hostdata;
	unsigned long flags;
	u8 mode;

	if (!(ahc->features & AHC_ULTRA2)) {
		/* non-LVD chipset, may not have SBLKCTL reg */
		spi_signalling(shost) = 
			ahc->features & AHC_HVD ?
			SPI_SIGNAL_HVD :
			SPI_SIGNAL_SE;
		return;
	}

	ahc_lock(ahc, &flags);
	ahc_pause(ahc);
	mode = ahc_inb(ahc, SBLKCTL);
	ahc_unpause(ahc);
	ahc_unlock(ahc, &flags);

	if (mode & ENAB40)
		spi_signalling(shost) = SPI_SIGNAL_LVD;
	else if (mode & ENAB20)
		spi_signalling(shost) = SPI_SIGNAL_SE;
	else
		spi_signalling(shost) = SPI_SIGNAL_UNKNOWN;
}

static struct spi_function_template ahc_linux_transport_functions = {
	.set_offset	= ahc_linux_set_offset,
	.show_offset	= 1,
	.set_period	= ahc_linux_set_period,
	.show_period	= 1,
	.set_width	= ahc_linux_set_width,
	.show_width	= 1,
	.set_dt		= ahc_linux_set_dt,
	.show_dt	= 1,
#if 0
	.set_iu		= ahc_linux_set_iu,
	.show_iu	= 1,
	.set_qas	= ahc_linux_set_qas,
	.show_qas	= 1,
#endif
	.get_signalling	= ahc_linux_get_signalling,
};



static int __init
ahc_linux_init(void)
{
	/*
	 * If we've been passed any parameters, process them now.
	 */
	if (aic7xxx)
		aic7xxx_setup(aic7xxx);

	ahc_linux_transport_template =
		spi_attach_transport(&ahc_linux_transport_functions);
	if (!ahc_linux_transport_template)
		return -ENODEV;

	scsi_transport_reserve_device(ahc_linux_transport_template,
				      sizeof(struct ahc_linux_device));

	ahc_linux_pci_init();
	ahc_linux_eisa_init();
	return 0;
}

static void
ahc_linux_exit(void)
{
	ahc_linux_pci_exit();
	ahc_linux_eisa_exit();
	spi_release_transport(ahc_linux_transport_template);
}

module_init(ahc_linux_init);
module_exit(ahc_linux_exit);
