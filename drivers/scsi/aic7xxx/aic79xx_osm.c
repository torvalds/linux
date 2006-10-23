/*
 * Adaptec AIC79xx device driver for Linux.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic79xx_osm.c#171 $
 *
 * --------------------------------------------------------------------------
 * Copyright (c) 1994-2000 Justin T. Gibbs.
 * Copyright (c) 1997-1999 Doug Ledford
 * Copyright (c) 2000-2003 Adaptec Inc.
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
 */

#include "aic79xx_osm.h"
#include "aic79xx_inline.h"
#include <scsi/scsicam.h>

static struct scsi_transport_template *ahd_linux_transport_template = NULL;

#include <linux/init.h>		/* __setup */
#include <linux/mm.h>		/* For fetching system memory size */
#include <linux/blkdev.h>		/* For block_size() */
#include <linux/delay.h>	/* For ssleep/msleep */
#include <linux/device.h>

/*
 * Bucket size for counting good commands in between bad ones.
 */
#define AHD_LINUX_ERR_THRESH	1000

/*
 * Set this to the delay in seconds after SCSI bus reset.
 * Note, we honor this only for the initial bus reset.
 * The scsi error recovery code performs its own bus settle
 * delay handling for error recovery actions.
 */
#ifdef CONFIG_AIC79XX_RESET_DELAY_MS
#define AIC79XX_RESET_DELAY CONFIG_AIC79XX_RESET_DELAY_MS
#else
#define AIC79XX_RESET_DELAY 5000
#endif

/*
 * To change the default number of tagged transactions allowed per-device,
 * add a line to the lilo.conf file like:
 * append="aic79xx=verbose,tag_info:{{32,32,32,32},{32,32,32,32}}"
 * which will result in the first four devices on the first two
 * controllers being set to a tagged queue depth of 32.
 *
 * The tag_commands is an array of 16 to allow for wide and twin adapters.
 * Twin adapters will use indexes 0-7 for channel 0, and indexes 8-15
 * for channel 1.
 */
typedef struct {
	uint16_t tag_commands[16];	/* Allow for wide/twin adapters. */
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
 * the devices on the first probed aic79xx adapter.
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
adapter_tag_info_t aic79xx_tag_info[] =
{
	{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
	{{4, 64, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 4, 4, 4}},
	{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
	{{0, 16, 4, 0, 16, 4, 4, 4, 127, 4, 4, 4, 4, 4, 4, 4}}
};
*/

#ifdef CONFIG_AIC79XX_CMDS_PER_DEVICE
#define AIC79XX_CMDS_PER_DEVICE CONFIG_AIC79XX_CMDS_PER_DEVICE
#else
#define AIC79XX_CMDS_PER_DEVICE AHD_MAX_QUEUE
#endif

#define AIC79XX_CONFIGED_TAG_COMMANDS {					\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE,		\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE,		\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE,		\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE,		\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE,		\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE,		\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE,		\
	AIC79XX_CMDS_PER_DEVICE, AIC79XX_CMDS_PER_DEVICE		\
}

/*
 * By default, use the number of commands specified by
 * the users kernel configuration.
 */
static adapter_tag_info_t aic79xx_tag_info[] =
{
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS},
	{AIC79XX_CONFIGED_TAG_COMMANDS}
};

/*
 * The I/O cell on the chip is very configurable in respect to its analog
 * characteristics.  Set the defaults here; they can be overriden with
 * the proper insmod parameters.
 */
struct ahd_linux_iocell_opts
{
	uint8_t	precomp;
	uint8_t	slewrate;
	uint8_t amplitude;
};
#define AIC79XX_DEFAULT_PRECOMP		0xFF
#define AIC79XX_DEFAULT_SLEWRATE	0xFF
#define AIC79XX_DEFAULT_AMPLITUDE	0xFF
#define AIC79XX_DEFAULT_IOOPTS			\
{						\
	AIC79XX_DEFAULT_PRECOMP,		\
	AIC79XX_DEFAULT_SLEWRATE,		\
	AIC79XX_DEFAULT_AMPLITUDE		\
}
#define AIC79XX_PRECOMP_INDEX	0
#define AIC79XX_SLEWRATE_INDEX	1
#define AIC79XX_AMPLITUDE_INDEX	2
static struct ahd_linux_iocell_opts aic79xx_iocell_info[] =
{
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS,
	AIC79XX_DEFAULT_IOOPTS
};

/*
 * There should be a specific return value for this in scsi.h, but
 * it seems that most drivers ignore it.
 */
#define DID_UNDERFLOW   DID_ERROR

void
ahd_print_path(struct ahd_softc *ahd, struct scb *scb)
{
	printk("(scsi%d:%c:%d:%d): ",
	       ahd->platform_data->host->host_no,
	       scb != NULL ? SCB_GET_CHANNEL(ahd, scb) : 'X',
	       scb != NULL ? SCB_GET_TARGET(ahd, scb) : -1,
	       scb != NULL ? SCB_GET_LUN(scb) : -1);
}

/*
 * XXX - these options apply unilaterally to _all_ adapters
 *       cards in the system.  This should be fixed.  Exceptions to this
 *       rule are noted in the comments.
 */

/*
 * Skip the scsi bus reset.  Non 0 make us skip the reset at startup.  This
 * has no effect on any later resets that might occur due to things like
 * SCSI bus timeouts.
 */
static uint32_t aic79xx_no_reset;

/*
 * Should we force EXTENDED translation on a controller.
 *     0 == Use whatever is in the SEEPROM or default to off
 *     1 == Use whatever is in the SEEPROM or default to on
 */
static uint32_t aic79xx_extended;

/*
 * PCI bus parity checking of the Adaptec controllers.  This is somewhat
 * dubious at best.  To my knowledge, this option has never actually
 * solved a PCI parity problem, but on certain machines with broken PCI
 * chipset configurations, it can generate tons of false error messages.
 * It's included in the driver for completeness.
 *   0	   = Shut off PCI parity check
 *   non-0 = Enable PCI parity check
 *
 * NOTE: you can't actually pass -1 on the lilo prompt.  So, to set this
 * variable to -1 you would actually want to simply pass the variable
 * name without a number.  That will invert the 0 which will result in
 * -1.
 */
static uint32_t aic79xx_pci_parity = ~0;

/*
 * There are lots of broken chipsets in the world.  Some of them will
 * violate the PCI spec when we issue byte sized memory writes to our
 * controller.  I/O mapped register access, if allowed by the given
 * platform, will work in almost all cases.
 */
uint32_t aic79xx_allow_memio = ~0;

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
static uint32_t aic79xx_seltime;

/*
 * Certain devices do not perform any aging on commands.  Should the
 * device be saturated by commands in one portion of the disk, it is
 * possible for transactions on far away sectors to never be serviced.
 * To handle these devices, we can periodically send an ordered tag to
 * force all outstanding transactions to be serviced prior to a new
 * transaction.
 */
static uint32_t aic79xx_periodic_otag;

/* Some storage boxes are using an LSI chip which has a bug making it
 * impossible to use aic79xx Rev B chip in 320 speeds.  The following
 * storage boxes have been reported to be buggy:
 * EonStor 3U 16-Bay: U16U-G3A3
 * EonStor 2U 12-Bay: U12U-G3A3
 * SentinelRAID: 2500F R5 / R6
 * SentinelRAID: 2500F R1
 * SentinelRAID: 2500F/1500F
 * SentinelRAID: 150F
 * 
 * To get around this LSI bug, you can set your board to 160 mode
 * or you can enable the SLOWCRC bit.
 */
uint32_t aic79xx_slowcrc;

/*
 * Module information and settable options.
 */
static char *aic79xx = NULL;

MODULE_AUTHOR("Maintainer: Justin T. Gibbs <gibbs@scsiguy.com>");
MODULE_DESCRIPTION("Adaptec Aic790X U320 SCSI Host Bus Adapter driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(AIC79XX_DRIVER_VERSION);
module_param(aic79xx, charp, 0444);
MODULE_PARM_DESC(aic79xx,
"period-delimited options string:\n"
"	verbose			Enable verbose/diagnostic logging\n"
"	allow_memio		Allow device registers to be memory mapped\n"
"	debug			Bitmask of debug values to enable\n"
"	no_reset		Supress initial bus resets\n"
"	extended		Enable extended geometry on all controllers\n"
"	periodic_otag		Send an ordered tagged transaction\n"
"				periodically to prevent tag starvation.\n"
"				This may be required by some older disk\n"
"				or drives/RAID arrays.\n"
"	tag_info:<tag_str>	Set per-target tag depth\n"
"	global_tag_depth:<int>	Global tag depth for all targets on all buses\n"
"	slewrate:<slewrate_list>Set the signal slew rate (0-15).\n"
"	precomp:<pcomp_list>	Set the signal precompensation (0-7).\n"
"	amplitude:<int>		Set the signal amplitude (0-7).\n"
"	seltime:<int>		Selection Timeout:\n"
"				(0/256ms,1/128ms,2/64ms,3/32ms)\n"
"	slowcrc			Turn on the SLOWCRC bit (Rev B only)\n"		 
"\n"
"	Sample /etc/modprobe.conf line:\n"
"		Enable verbose logging\n"
"		Set tag depth on Controller 2/Target 2 to 10 tags\n"
"		Shorten the selection timeout to 128ms\n"
"\n"
"	options aic79xx 'aic79xx=verbose.tag_info:{{}.{}.{..10}}.seltime:1'\n"
);

static void ahd_linux_handle_scsi_status(struct ahd_softc *,
					 struct scsi_device *,
					 struct scb *);
static void ahd_linux_queue_cmd_complete(struct ahd_softc *ahd,
					 struct scsi_cmnd *cmd);
static int ahd_linux_queue_abort_cmd(struct scsi_cmnd *cmd);
static void ahd_linux_initialize_scsi_bus(struct ahd_softc *ahd);
static u_int ahd_linux_user_tagdepth(struct ahd_softc *ahd,
				     struct ahd_devinfo *devinfo);
static void ahd_linux_device_queue_depth(struct scsi_device *);
static int ahd_linux_run_command(struct ahd_softc*,
				 struct ahd_linux_device *,
				 struct scsi_cmnd *);
static void ahd_linux_setup_tag_info_global(char *p);
static int  aic79xx_setup(char *c);

static int ahd_linux_unit;


/****************************** Inlines ***************************************/
static __inline void ahd_linux_unmap_scb(struct ahd_softc*, struct scb*);

static __inline void
ahd_linux_unmap_scb(struct ahd_softc *ahd, struct scb *scb)
{
	struct scsi_cmnd *cmd;
	int direction;

	cmd = scb->io_ctx;
	direction = cmd->sc_data_direction;
	ahd_sync_sglist(ahd, scb, BUS_DMASYNC_POSTWRITE);
	if (cmd->use_sg != 0) {
		struct scatterlist *sg;

		sg = (struct scatterlist *)cmd->request_buffer;
		pci_unmap_sg(ahd->dev_softc, sg, cmd->use_sg, direction);
	} else if (cmd->request_bufflen != 0) {
		pci_unmap_single(ahd->dev_softc,
				 scb->platform_data->buf_busaddr,
				 cmd->request_bufflen, direction);
	}
}

/******************************** Macros **************************************/
#define BUILD_SCSIID(ahd, cmd)						\
	(((scmd_id(cmd) << TID_SHIFT) & TID) | (ahd)->our_id)

/*
 * Return a string describing the driver.
 */
static const char *
ahd_linux_info(struct Scsi_Host *host)
{
	static char buffer[512];
	char	ahd_info[256];
	char   *bp;
	struct ahd_softc *ahd;

	bp = &buffer[0];
	ahd = *(struct ahd_softc **)host->hostdata;
	memset(bp, 0, sizeof(buffer));
	strcpy(bp, "Adaptec AIC79XX PCI-X SCSI HBA DRIVER, Rev ");
	strcat(bp, AIC79XX_DRIVER_VERSION);
	strcat(bp, "\n");
	strcat(bp, "        <");
	strcat(bp, ahd->description);
	strcat(bp, ">\n");
	strcat(bp, "        ");
	ahd_controller_info(ahd, ahd_info);
	strcat(bp, ahd_info);
	strcat(bp, "\n");

	return (bp);
}

/*
 * Queue an SCB to the controller.
 */
static int
ahd_linux_queue(struct scsi_cmnd * cmd, void (*scsi_done) (struct scsi_cmnd *))
{
	struct	 ahd_softc *ahd;
	struct	 ahd_linux_device *dev = scsi_transport_device_data(cmd->device);
	int rtn = SCSI_MLQUEUE_HOST_BUSY;

	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;

	cmd->scsi_done = scsi_done;
	cmd->result = CAM_REQ_INPROG << 16;
	rtn = ahd_linux_run_command(ahd, dev, cmd);

	return rtn;
}

static inline struct scsi_target **
ahd_linux_target_in_softc(struct scsi_target *starget)
{
	struct	ahd_softc *ahd =
		*((struct ahd_softc **)dev_to_shost(&starget->dev)->hostdata);
	unsigned int target_offset;

	target_offset = starget->id;
	if (starget->channel != 0)
		target_offset += 8;

	return &ahd->platform_data->starget[target_offset];
}

static int
ahd_linux_target_alloc(struct scsi_target *starget)
{
	struct	ahd_softc *ahd =
		*((struct ahd_softc **)dev_to_shost(&starget->dev)->hostdata);
	struct seeprom_config *sc = ahd->seep_config;
	unsigned long flags;
	struct scsi_target **ahd_targp = ahd_linux_target_in_softc(starget);
	struct ahd_devinfo devinfo;
	struct ahd_initiator_tinfo *tinfo;
	struct ahd_tmode_tstate *tstate;
	char channel = starget->channel + 'A';

	ahd_lock(ahd, &flags);

	BUG_ON(*ahd_targp != NULL);

	*ahd_targp = starget;

	if (sc) {
		int flags = sc->device_flags[starget->id];

		tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
					    starget->id, &tstate);

		if ((flags  & CFPACKETIZED) == 0) {
			/* Do not negotiate packetized transfers */
			spi_rd_strm(starget) = 0;
			spi_pcomp_en(starget) = 0;
			spi_rti(starget) = 0;
			spi_wr_flow(starget) = 0;
			spi_hold_mcs(starget) = 0;
		} else {
			if ((ahd->features & AHD_RTI) == 0)
				spi_rti(starget) = 0;
		}

		if ((flags & CFQAS) == 0)
			spi_qas(starget) = 0;

		/* Transinfo values have been set to BIOS settings */
		spi_max_width(starget) = (flags & CFWIDEB) ? 1 : 0;
		spi_min_period(starget) = tinfo->user.period;
		spi_max_offset(starget) = tinfo->user.offset;
	}

	tinfo = ahd_fetch_transinfo(ahd, channel, ahd->our_id,
				    starget->id, &tstate);
	ahd_compile_devinfo(&devinfo, ahd->our_id, starget->id,
			    CAM_LUN_WILDCARD, channel,
			    ROLE_INITIATOR);
	ahd_set_syncrate(ahd, &devinfo, 0, 0, 0,
			 AHD_TRANS_GOAL, /*paused*/FALSE);
	ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
		      AHD_TRANS_GOAL, /*paused*/FALSE);
	ahd_unlock(ahd, &flags);

	return 0;
}

static void
ahd_linux_target_destroy(struct scsi_target *starget)
{
	struct scsi_target **ahd_targp = ahd_linux_target_in_softc(starget);

	*ahd_targp = NULL;
}

static int
ahd_linux_slave_alloc(struct scsi_device *sdev)
{
	struct	ahd_softc *ahd =
		*((struct ahd_softc **)sdev->host->hostdata);
	struct ahd_linux_device *dev;

	if (bootverbose)
		printf("%s: Slave Alloc %d\n", ahd_name(ahd), sdev->id);

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
	
	return (0);
}

static int
ahd_linux_slave_configure(struct scsi_device *sdev)
{
	struct	ahd_softc *ahd;

	ahd = *((struct ahd_softc **)sdev->host->hostdata);
	if (bootverbose)
		sdev_printk(KERN_INFO, sdev, "Slave Configure\n");

	ahd_linux_device_queue_depth(sdev);

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
ahd_linux_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		    sector_t capacity, int geom[])
{
	uint8_t *bh;
	int	 heads;
	int	 sectors;
	int	 cylinders;
	int	 ret;
	int	 extended;
	struct	 ahd_softc *ahd;

	ahd = *((struct ahd_softc **)sdev->host->hostdata);

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

	if (aic79xx_extended != 0)
		extended = 1;
	else
		extended = (ahd->flags & AHD_EXTENDED_TRANS_A) != 0;
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
ahd_linux_abort(struct scsi_cmnd *cmd)
{
	int error;
	
	error = ahd_linux_queue_abort_cmd(cmd);

	return error;
}

/*
 * Attempt to send a target reset message to the device that timed out.
 */
static int
ahd_linux_dev_reset(struct scsi_cmnd *cmd)
{
	struct ahd_softc *ahd;
	struct ahd_linux_device *dev;
	struct scb *reset_scb;
	u_int  cdb_byte;
	int    retval = SUCCESS;
	int    paused;
	int    wait;
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	unsigned long flags;
	DECLARE_COMPLETION_ONSTACK(done);

	reset_scb = NULL;
	paused = FALSE;
	wait = FALSE;
	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;

	scmd_printk(KERN_INFO, cmd,
		    "Attempting to queue a TARGET RESET message:");

	printf("CDB:");
	for (cdb_byte = 0; cdb_byte < cmd->cmd_len; cdb_byte++)
		printf(" 0x%x", cmd->cmnd[cdb_byte]);
	printf("\n");

	/*
	 * Determine if we currently own this command.
	 */
	dev = scsi_transport_device_data(cmd->device);

	if (dev == NULL) {
		/*
		 * No target device for this command exists,
		 * so we must not still own the command.
		 */
		scmd_printk(KERN_INFO, cmd, "Is not an active device\n");
		return SUCCESS;
	}

	/*
	 * Generate us a new SCB
	 */
	reset_scb = ahd_get_scb(ahd, AHD_NEVER_COL_IDX);
	if (!reset_scb) {
		scmd_printk(KERN_INFO, cmd, "No SCB available\n");
		return FAILED;
	}

	tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
				    cmd->device->id, &tstate);
	reset_scb->io_ctx = cmd;
	reset_scb->platform_data->dev = dev;
	reset_scb->sg_count = 0;
	ahd_set_residual(reset_scb, 0);
	ahd_set_sense_residual(reset_scb, 0);
	reset_scb->platform_data->xfer_len = 0;
	reset_scb->hscb->control = 0;
	reset_scb->hscb->scsiid = BUILD_SCSIID(ahd,cmd);
	reset_scb->hscb->lun = cmd->device->lun;
	reset_scb->hscb->cdb_len = 0;
	reset_scb->hscb->task_management = SIU_TASKMGMT_LUN_RESET;
	reset_scb->flags |= SCB_DEVICE_RESET|SCB_RECOVERY_SCB|SCB_ACTIVE;
	if ((tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {
		reset_scb->flags |= SCB_PACKETIZED;
	} else {
		reset_scb->hscb->control |= MK_MESSAGE;
	}
	dev->openings--;
	dev->active++;
	dev->commands_issued++;

	ahd_lock(ahd, &flags);

	LIST_INSERT_HEAD(&ahd->pending_scbs, reset_scb, pending_links);
	ahd_queue_scb(ahd, reset_scb);

	ahd->platform_data->eh_done = &done;
	ahd_unlock(ahd, &flags);

	printf("%s: Device reset code sleeping\n", ahd_name(ahd));
	if (!wait_for_completion_timeout(&done, 5 * HZ)) {
		ahd_lock(ahd, &flags);
		ahd->platform_data->eh_done = NULL;
		ahd_unlock(ahd, &flags);
		printf("%s: Device reset timer expired (active %d)\n",
		       ahd_name(ahd), dev->active);
		retval = FAILED;
	}
	printf("%s: Device reset returning 0x%x\n", ahd_name(ahd), retval);

	return (retval);
}

/*
 * Reset the SCSI bus.
 */
static int
ahd_linux_bus_reset(struct scsi_cmnd *cmd)
{
	struct ahd_softc *ahd;
	int    found;
	unsigned long flags;

	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;
#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_RECOVERY) != 0)
		printf("%s: Bus reset called for cmd %p\n",
		       ahd_name(ahd), cmd);
#endif
	ahd_lock(ahd, &flags);

	found = ahd_reset_channel(ahd, scmd_channel(cmd) + 'A',
				  /*initiate reset*/TRUE);
	ahd_unlock(ahd, &flags);

	if (bootverbose)
		printf("%s: SCSI bus reset delivered. "
		       "%d SCBs aborted.\n", ahd_name(ahd), found);

	return (SUCCESS);
}

struct scsi_host_template aic79xx_driver_template = {
	.module			= THIS_MODULE,
	.name			= "aic79xx",
	.proc_name		= "aic79xx",
	.proc_info		= ahd_linux_proc_info,
	.info			= ahd_linux_info,
	.queuecommand		= ahd_linux_queue,
	.eh_abort_handler	= ahd_linux_abort,
	.eh_device_reset_handler = ahd_linux_dev_reset,
	.eh_bus_reset_handler	= ahd_linux_bus_reset,
#if defined(__i386__)
	.bios_param		= ahd_linux_biosparam,
#endif
	.can_queue		= AHD_MAX_QUEUE,
	.this_id		= -1,
	.max_sectors		= 8192,
	.cmd_per_lun		= 2,
	.use_clustering		= ENABLE_CLUSTERING,
	.slave_alloc		= ahd_linux_slave_alloc,
	.slave_configure	= ahd_linux_slave_configure,
	.target_alloc		= ahd_linux_target_alloc,
	.target_destroy		= ahd_linux_target_destroy,
};

/******************************** Bus DMA *************************************/
int
ahd_dma_tag_create(struct ahd_softc *ahd, bus_dma_tag_t parent,
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
ahd_dma_tag_destroy(struct ahd_softc *ahd, bus_dma_tag_t dmat)
{
	free(dmat, M_DEVBUF);
}

int
ahd_dmamem_alloc(struct ahd_softc *ahd, bus_dma_tag_t dmat, void** vaddr,
		 int flags, bus_dmamap_t *mapp)
{
	*vaddr = pci_alloc_consistent(ahd->dev_softc,
				      dmat->maxsize, mapp);
	if (*vaddr == NULL)
		return (ENOMEM);
	return(0);
}

void
ahd_dmamem_free(struct ahd_softc *ahd, bus_dma_tag_t dmat,
		void* vaddr, bus_dmamap_t map)
{
	pci_free_consistent(ahd->dev_softc, dmat->maxsize,
			    vaddr, map);
}

int
ahd_dmamap_load(struct ahd_softc *ahd, bus_dma_tag_t dmat, bus_dmamap_t map,
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
ahd_dmamap_destroy(struct ahd_softc *ahd, bus_dma_tag_t dmat, bus_dmamap_t map)
{
}

int
ahd_dmamap_unload(struct ahd_softc *ahd, bus_dma_tag_t dmat, bus_dmamap_t map)
{
	/* Nothing to do */
	return (0);
}

/********************* Platform Dependent Functions ***************************/
static void
ahd_linux_setup_iocell_info(u_long index, int instance, int targ, int32_t value)
{

	if ((instance >= 0)
	 && (instance < ARRAY_SIZE(aic79xx_iocell_info))) {
		uint8_t *iocell_info;

		iocell_info = (uint8_t*)&aic79xx_iocell_info[instance];
		iocell_info[index] = value & 0xFFFF;
		if (bootverbose)
			printf("iocell[%d:%ld] = %d\n", instance, index, value);
	}
}

static void
ahd_linux_setup_tag_info_global(char *p)
{
	int tags, i, j;

	tags = simple_strtoul(p + 1, NULL, 0) & 0xff;
	printf("Setting Global Tags= %d\n", tags);

	for (i = 0; i < ARRAY_SIZE(aic79xx_tag_info); i++) {
		for (j = 0; j < AHD_NUM_TARGETS; j++) {
			aic79xx_tag_info[i].tag_commands[j] = tags;
		}
	}
}

static void
ahd_linux_setup_tag_info(u_long arg, int instance, int targ, int32_t value)
{

	if ((instance >= 0) && (targ >= 0)
	 && (instance < ARRAY_SIZE(aic79xx_tag_info))
	 && (targ < AHD_NUM_TARGETS)) {
		aic79xx_tag_info[instance].tag_commands[targ] = value & 0x1FF;
		if (bootverbose)
			printf("tag_info[%d:%d] = %d\n", instance, targ, value);
	}
}

static char *
ahd_parse_brace_option(char *opt_name, char *opt_arg, char *end, int depth,
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
 * ie. aic79xx=stpwlev:1,extended
 */
static int
aic79xx_setup(char *s)
{
	int	i, n;
	char   *p;
	char   *end;

	static struct {
		const char *name;
		uint32_t *flag;
	} options[] = {
		{ "extended", &aic79xx_extended },
		{ "no_reset", &aic79xx_no_reset },
		{ "verbose", &aic79xx_verbose },
		{ "allow_memio", &aic79xx_allow_memio},
#ifdef AHD_DEBUG
		{ "debug", &ahd_debug },
#endif
		{ "periodic_otag", &aic79xx_periodic_otag },
		{ "pci_parity", &aic79xx_pci_parity },
		{ "seltime", &aic79xx_seltime },
		{ "tag_info", NULL },
		{ "global_tag_depth", NULL},
		{ "slewrate", NULL },
		{ "precomp", NULL },
		{ "amplitude", NULL },
		{ "slowcrc", &aic79xx_slowcrc },
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
			ahd_linux_setup_tag_info_global(p + n);
		} else if (strncmp(p, "tag_info", n) == 0) {
			s = ahd_parse_brace_option("tag_info", p + n, end,
			    2, ahd_linux_setup_tag_info, 0);
		} else if (strncmp(p, "slewrate", n) == 0) {
			s = ahd_parse_brace_option("slewrate",
			    p + n, end, 1, ahd_linux_setup_iocell_info,
			    AIC79XX_SLEWRATE_INDEX);
		} else if (strncmp(p, "precomp", n) == 0) {
			s = ahd_parse_brace_option("precomp",
			    p + n, end, 1, ahd_linux_setup_iocell_info,
			    AIC79XX_PRECOMP_INDEX);
		} else if (strncmp(p, "amplitude", n) == 0) {
			s = ahd_parse_brace_option("amplitude",
			    p + n, end, 1, ahd_linux_setup_iocell_info,
			    AIC79XX_AMPLITUDE_INDEX);
		} else if (p[n] == ':') {
			*(options[i].flag) = simple_strtoul(p + n + 1, NULL, 0);
		} else if (!strncmp(p, "verbose", n)) {
			*(options[i].flag) = 1;
		} else {
			*(options[i].flag) ^= 0xFFFFFFFF;
		}
	}
	return 1;
}

__setup("aic79xx=", aic79xx_setup);

uint32_t aic79xx_verbose;

int
ahd_linux_register_host(struct ahd_softc *ahd, struct scsi_host_template *template)
{
	char	buf[80];
	struct	Scsi_Host *host;
	char	*new_name;
	u_long	s;
	int	retval;

	template->name = ahd->description;
	host = scsi_host_alloc(template, sizeof(struct ahd_softc *));
	if (host == NULL)
		return (ENOMEM);

	*((struct ahd_softc **)host->hostdata) = ahd;
	ahd->platform_data->host = host;
	host->can_queue = AHD_MAX_QUEUE;
	host->cmd_per_lun = 2;
	host->sg_tablesize = AHD_NSEG;
	host->this_id = ahd->our_id;
	host->irq = ahd->platform_data->irq;
	host->max_id = (ahd->features & AHD_WIDE) ? 16 : 8;
	host->max_lun = AHD_NUM_LUNS;
	host->max_channel = 0;
	host->sg_tablesize = AHD_NSEG;
	ahd_lock(ahd, &s);
	ahd_set_unit(ahd, ahd_linux_unit++);
	ahd_unlock(ahd, &s);
	sprintf(buf, "scsi%d", host->host_no);
	new_name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (new_name != NULL) {
		strcpy(new_name, buf);
		ahd_set_name(ahd, new_name);
	}
	host->unique_id = ahd->unit;
	ahd_linux_initialize_scsi_bus(ahd);
	ahd_intr_enable(ahd, TRUE);

	host->transportt = ahd_linux_transport_template;

	retval = scsi_add_host(host, &ahd->dev_softc->dev);
	if (retval) {
		printk(KERN_WARNING "aic79xx: scsi_add_host failed\n");
		scsi_host_put(host);
		return retval;
	}

	scsi_scan_host(host);
	return 0;
}

uint64_t
ahd_linux_get_memsize(void)
{
	struct sysinfo si;

	si_meminfo(&si);
	return ((uint64_t)si.totalram << PAGE_SHIFT);
}

/*
 * Place the SCSI bus into a known state by either resetting it,
 * or forcing transfer negotiations on the next command to any
 * target.
 */
static void
ahd_linux_initialize_scsi_bus(struct ahd_softc *ahd)
{
	u_int target_id;
	u_int numtarg;
	unsigned long s;

	target_id = 0;
	numtarg = 0;

	if (aic79xx_no_reset != 0)
		ahd->flags &= ~AHD_RESET_BUS_A;

	if ((ahd->flags & AHD_RESET_BUS_A) != 0)
		ahd_reset_channel(ahd, 'A', /*initiate_reset*/TRUE);
	else
		numtarg = (ahd->features & AHD_WIDE) ? 16 : 8;

	ahd_lock(ahd, &s);

	/*
	 * Force negotiation to async for all targets that
	 * will not see an initial bus reset.
	 */
	for (; target_id < numtarg; target_id++) {
		struct ahd_devinfo devinfo;
		struct ahd_initiator_tinfo *tinfo;
		struct ahd_tmode_tstate *tstate;

		tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
					    target_id, &tstate);
		ahd_compile_devinfo(&devinfo, ahd->our_id, target_id,
				    CAM_LUN_WILDCARD, 'A', ROLE_INITIATOR);
		ahd_update_neg_request(ahd, &devinfo, tstate,
				       tinfo, AHD_NEG_ALWAYS);
	}
	ahd_unlock(ahd, &s);
	/* Give the bus some time to recover */
	if ((ahd->flags & AHD_RESET_BUS_A) != 0) {
		ahd_freeze_simq(ahd);
		msleep(AIC79XX_RESET_DELAY);
		ahd_release_simq(ahd);
	}
}

int
ahd_platform_alloc(struct ahd_softc *ahd, void *platform_arg)
{
	ahd->platform_data =
	    malloc(sizeof(struct ahd_platform_data), M_DEVBUF, M_NOWAIT);
	if (ahd->platform_data == NULL)
		return (ENOMEM);
	memset(ahd->platform_data, 0, sizeof(struct ahd_platform_data));
	ahd->platform_data->irq = AHD_LINUX_NOIRQ;
	ahd_lockinit(ahd);
	ahd->seltime = (aic79xx_seltime & 0x3) << 4;
	return (0);
}

void
ahd_platform_free(struct ahd_softc *ahd)
{
	struct scsi_target *starget;
	int i;

	if (ahd->platform_data != NULL) {
		/* destroy all of the device and target objects */
		for (i = 0; i < AHD_NUM_TARGETS; i++) {
			starget = ahd->platform_data->starget[i];
			if (starget != NULL) {
				ahd->platform_data->starget[i] = NULL;
			}
		}

		if (ahd->platform_data->irq != AHD_LINUX_NOIRQ)
			free_irq(ahd->platform_data->irq, ahd);
		if (ahd->tags[0] == BUS_SPACE_PIO
		 && ahd->bshs[0].ioport != 0)
			release_region(ahd->bshs[0].ioport, 256);
		if (ahd->tags[1] == BUS_SPACE_PIO
		 && ahd->bshs[1].ioport != 0)
			release_region(ahd->bshs[1].ioport, 256);
		if (ahd->tags[0] == BUS_SPACE_MEMIO
		 && ahd->bshs[0].maddr != NULL) {
			iounmap(ahd->bshs[0].maddr);
			release_mem_region(ahd->platform_data->mem_busaddr,
					   0x1000);
		}
		if (ahd->platform_data->host)
			scsi_host_put(ahd->platform_data->host);

		free(ahd->platform_data, M_DEVBUF);
	}
}

void
ahd_platform_init(struct ahd_softc *ahd)
{
	/*
	 * Lookup and commit any modified IO Cell options.
	 */
	if (ahd->unit < ARRAY_SIZE(aic79xx_iocell_info)) {
		struct ahd_linux_iocell_opts *iocell_opts;

		iocell_opts = &aic79xx_iocell_info[ahd->unit];
		if (iocell_opts->precomp != AIC79XX_DEFAULT_PRECOMP)
			AHD_SET_PRECOMP(ahd, iocell_opts->precomp);
		if (iocell_opts->slewrate != AIC79XX_DEFAULT_SLEWRATE)
			AHD_SET_SLEWRATE(ahd, iocell_opts->slewrate);
		if (iocell_opts->amplitude != AIC79XX_DEFAULT_AMPLITUDE)
			AHD_SET_AMPLITUDE(ahd, iocell_opts->amplitude);
	}

}

void
ahd_platform_freeze_devq(struct ahd_softc *ahd, struct scb *scb)
{
	ahd_platform_abort_scbs(ahd, SCB_GET_TARGET(ahd, scb),
				SCB_GET_CHANNEL(ahd, scb),
				SCB_GET_LUN(scb), SCB_LIST_NULL,
				ROLE_UNKNOWN, CAM_REQUEUE_REQ);
}

void
ahd_platform_set_tags(struct ahd_softc *ahd, struct scsi_device *sdev,
		      struct ahd_devinfo *devinfo, ahd_queue_alg alg)
{
	struct ahd_linux_device *dev;
	int was_queuing;
	int now_queuing;

	if (sdev == NULL)
		return;

	dev = scsi_transport_device_data(sdev);

	if (dev == NULL)
		return;
	was_queuing = dev->flags & (AHD_DEV_Q_BASIC|AHD_DEV_Q_TAGGED);
	switch (alg) {
	default:
	case AHD_QUEUE_NONE:
		now_queuing = 0;
		break; 
	case AHD_QUEUE_BASIC:
		now_queuing = AHD_DEV_Q_BASIC;
		break;
	case AHD_QUEUE_TAGGED:
		now_queuing = AHD_DEV_Q_TAGGED;
		break;
	}
	if ((dev->flags & AHD_DEV_FREEZE_TIL_EMPTY) == 0
	 && (was_queuing != now_queuing)
	 && (dev->active != 0)) {
		dev->flags |= AHD_DEV_FREEZE_TIL_EMPTY;
		dev->qfrozen++;
	}

	dev->flags &= ~(AHD_DEV_Q_BASIC|AHD_DEV_Q_TAGGED|AHD_DEV_PERIODIC_OTAG);
	if (now_queuing) {
		u_int usertags;

		usertags = ahd_linux_user_tagdepth(ahd, devinfo);
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
		} else if (alg == AHD_QUEUE_TAGGED) {
			dev->flags |= AHD_DEV_Q_TAGGED;
			if (aic79xx_periodic_otag != 0)
				dev->flags |= AHD_DEV_PERIODIC_OTAG;
		} else
			dev->flags |= AHD_DEV_Q_BASIC;
	} else {
		/* We can only have one opening. */
		dev->maxtags = 0;
		dev->openings =  1 - dev->active;
	}

	switch ((dev->flags & (AHD_DEV_Q_BASIC|AHD_DEV_Q_TAGGED))) {
	case AHD_DEV_Q_BASIC:
		scsi_set_tag_type(sdev, MSG_SIMPLE_TASK);
		scsi_activate_tcq(sdev, dev->openings + dev->active);
		break;
	case AHD_DEV_Q_TAGGED:
		scsi_set_tag_type(sdev, MSG_ORDERED_TASK);
		scsi_activate_tcq(sdev, dev->openings + dev->active);
		break;
	default:
		/*
		 * We allow the OS to queue 2 untagged transactions to
		 * us at any time even though we can only execute them
		 * serially on the controller/device.  This should
		 * remove some latency.
		 */
		scsi_deactivate_tcq(sdev, 1);
		break;
	}
}

int
ahd_platform_abort_scbs(struct ahd_softc *ahd, int target, char channel,
			int lun, u_int tag, role_t role, uint32_t status)
{
	return 0;
}

static u_int
ahd_linux_user_tagdepth(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	static int warned_user;
	u_int tags;

	tags = 0;
	if ((ahd->user_discenable & devinfo->target_mask) != 0) {
		if (ahd->unit >= ARRAY_SIZE(aic79xx_tag_info)) {

			if (warned_user == 0) {
				printf(KERN_WARNING
"aic79xx: WARNING: Insufficient tag_info instances\n"
"aic79xx: for installed controllers.  Using defaults\n"
"aic79xx: Please update the aic79xx_tag_info array in\n"
"aic79xx: the aic79xx_osm.c source file.\n");
				warned_user++;
			}
			tags = AHD_MAX_QUEUE;
		} else {
			adapter_tag_info_t *tag_info;

			tag_info = &aic79xx_tag_info[ahd->unit];
			tags = tag_info->tag_commands[devinfo->target_offset];
			if (tags > AHD_MAX_QUEUE)
				tags = AHD_MAX_QUEUE;
		}
	}
	return (tags);
}

/*
 * Determines the queue depth for a given device.
 */
static void
ahd_linux_device_queue_depth(struct scsi_device *sdev)
{
	struct	ahd_devinfo devinfo;
	u_int	tags;
	struct ahd_softc *ahd = *((struct ahd_softc **)sdev->host->hostdata);

	ahd_compile_devinfo(&devinfo,
			    ahd->our_id,
			    sdev->sdev_target->id, sdev->lun,
			    sdev->sdev_target->channel == 0 ? 'A' : 'B',
			    ROLE_INITIATOR);
	tags = ahd_linux_user_tagdepth(ahd, &devinfo);
	if (tags != 0 && sdev->tagged_supported != 0) {

		ahd_platform_set_tags(ahd, sdev, &devinfo, AHD_QUEUE_TAGGED);
		ahd_send_async(ahd, devinfo.channel, devinfo.target,
			       devinfo.lun, AC_TRANSFER_NEG);
		ahd_print_devinfo(ahd, &devinfo);
		printf("Tagged Queuing enabled.  Depth %d\n", tags);
	} else {
		ahd_platform_set_tags(ahd, sdev, &devinfo, AHD_QUEUE_NONE);
		ahd_send_async(ahd, devinfo.channel, devinfo.target,
			       devinfo.lun, AC_TRANSFER_NEG);
	}
}

static int
ahd_linux_run_command(struct ahd_softc *ahd, struct ahd_linux_device *dev,
		      struct scsi_cmnd *cmd)
{
	struct	 scb *scb;
	struct	 hardware_scb *hscb;
	struct	 ahd_initiator_tinfo *tinfo;
	struct	 ahd_tmode_tstate *tstate;
	u_int	 col_idx;
	uint16_t mask;
	unsigned long flags;

	ahd_lock(ahd, &flags);

	/*
	 * Get an scb to use.
	 */
	tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
				    cmd->device->id, &tstate);
	if ((dev->flags & (AHD_DEV_Q_TAGGED|AHD_DEV_Q_BASIC)) == 0
	 || (tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {
		col_idx = AHD_NEVER_COL_IDX;
	} else {
		col_idx = AHD_BUILD_COL_IDX(cmd->device->id,
					    cmd->device->lun);
	}
	if ((scb = ahd_get_scb(ahd, col_idx)) == NULL) {
		ahd->flags |= AHD_RESOURCE_SHORTAGE;
		ahd_unlock(ahd, &flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	scb->io_ctx = cmd;
	scb->platform_data->dev = dev;
	hscb = scb->hscb;
	cmd->host_scribble = (char *)scb;

	/*
	 * Fill out basics of the HSCB.
	 */
	hscb->control = 0;
	hscb->scsiid = BUILD_SCSIID(ahd, cmd);
	hscb->lun = cmd->device->lun;
	scb->hscb->task_management = 0;
	mask = SCB_GET_TARGET_MASK(ahd, scb);

	if ((ahd->user_discenable & mask) != 0)
		hscb->control |= DISCENB;

	if ((tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ) != 0)
		scb->flags |= SCB_PACKETIZED;

	if ((tstate->auto_negotiate & mask) != 0) {
		scb->flags |= SCB_AUTO_NEGOTIATE;
		scb->hscb->control |= MK_MESSAGE;
	}

	if ((dev->flags & (AHD_DEV_Q_TAGGED|AHD_DEV_Q_BASIC)) != 0) {
		int	msg_bytes;
		uint8_t tag_msgs[2];

		msg_bytes = scsi_populate_tag_msg(cmd, tag_msgs);
		if (msg_bytes && tag_msgs[0] != MSG_SIMPLE_TASK) {
			hscb->control |= tag_msgs[0];
			if (tag_msgs[0] == MSG_ORDERED_TASK)
				dev->commands_since_idle_or_otag = 0;
		} else
		if (dev->commands_since_idle_or_otag == AHD_OTAG_THRESH
		 && (dev->flags & AHD_DEV_Q_TAGGED) != 0) {
			hscb->control |= MSG_ORDERED_TASK;
			dev->commands_since_idle_or_otag = 0;
		} else {
			hscb->control |= MSG_SIMPLE_TASK;
		}
	}

	hscb->cdb_len = cmd->cmd_len;
	memcpy(hscb->shared_data.idata.cdb, cmd->cmnd, hscb->cdb_len);

	scb->platform_data->xfer_len = 0;
	ahd_set_residual(scb, 0);
	ahd_set_sense_residual(scb, 0);
	scb->sg_count = 0;
	if (cmd->use_sg != 0) {
		void	*sg;
		struct	 scatterlist *cur_seg;
		u_int	 nseg;
		int	 dir;

		cur_seg = (struct scatterlist *)cmd->request_buffer;
		dir = cmd->sc_data_direction;
		nseg = pci_map_sg(ahd->dev_softc, cur_seg,
				  cmd->use_sg, dir);
		scb->platform_data->xfer_len = 0;
		for (sg = scb->sg_list; nseg > 0; nseg--, cur_seg++) {
			dma_addr_t addr;
			bus_size_t len;

			addr = sg_dma_address(cur_seg);
			len = sg_dma_len(cur_seg);
			scb->platform_data->xfer_len += len;
			sg = ahd_sg_setup(ahd, scb, sg, addr, len,
					  /*last*/nseg == 1);
		}
	} else if (cmd->request_bufflen != 0) {
		void *sg;
		dma_addr_t addr;
		int dir;

		sg = scb->sg_list;
		dir = cmd->sc_data_direction;
		addr = pci_map_single(ahd->dev_softc,
				      cmd->request_buffer,
				      cmd->request_bufflen, dir);
		scb->platform_data->xfer_len = cmd->request_bufflen;
		scb->platform_data->buf_busaddr = addr;
		sg = ahd_sg_setup(ahd, scb, sg, addr,
				  cmd->request_bufflen, /*last*/TRUE);
	}

	LIST_INSERT_HEAD(&ahd->pending_scbs, scb, pending_links);
	dev->openings--;
	dev->active++;
	dev->commands_issued++;

	if ((dev->flags & AHD_DEV_PERIODIC_OTAG) != 0)
		dev->commands_since_idle_or_otag++;
	scb->flags |= SCB_ACTIVE;
	ahd_queue_scb(ahd, scb);

	ahd_unlock(ahd, &flags);

	return 0;
}

/*
 * SCSI controller interrupt handler.
 */
irqreturn_t
ahd_linux_isr(int irq, void *dev_id)
{
	struct	ahd_softc *ahd;
	u_long	flags;
	int	ours;

	ahd = (struct ahd_softc *) dev_id;
	ahd_lock(ahd, &flags); 
	ours = ahd_intr(ahd);
	ahd_unlock(ahd, &flags);
	return IRQ_RETVAL(ours);
}

void
ahd_send_async(struct ahd_softc *ahd, char channel,
	       u_int target, u_int lun, ac_code code)
{
	switch (code) {
	case AC_TRANSFER_NEG:
	{
		char	buf[80];
		struct  scsi_target *starget;
		struct	info_str info;
		struct	ahd_initiator_tinfo *tinfo;
		struct	ahd_tmode_tstate *tstate;
		unsigned int target_ppr_options;

		BUG_ON(target == CAM_TARGET_WILDCARD);

		info.buffer = buf;
		info.length = sizeof(buf);
		info.offset = 0;
		info.pos = 0;
		tinfo = ahd_fetch_transinfo(ahd, channel, ahd->our_id,
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
		starget = ahd->platform_data->starget[target];
		if (starget == NULL)
			break;

		target_ppr_options =
			(spi_dt(starget) ? MSG_EXT_PPR_DT_REQ : 0)
			+ (spi_qas(starget) ? MSG_EXT_PPR_QAS_REQ : 0)
			+ (spi_iu(starget) ?  MSG_EXT_PPR_IU_REQ : 0)
			+ (spi_rd_strm(starget) ? MSG_EXT_PPR_RD_STRM : 0)
			+ (spi_pcomp_en(starget) ? MSG_EXT_PPR_PCOMP_EN : 0)
			+ (spi_rti(starget) ? MSG_EXT_PPR_RTI : 0)
			+ (spi_wr_flow(starget) ? MSG_EXT_PPR_WR_FLOW : 0)
			+ (spi_hold_mcs(starget) ? MSG_EXT_PPR_HOLD_MCS : 0);

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
		spi_rd_strm(starget) = tinfo->curr.ppr_options & MSG_EXT_PPR_RD_STRM ? 1 : 0;
		spi_pcomp_en(starget) =  tinfo->curr.ppr_options & MSG_EXT_PPR_PCOMP_EN ? 1 : 0;
		spi_rti(starget) =  tinfo->curr.ppr_options &  MSG_EXT_PPR_RTI ? 1 : 0;
		spi_wr_flow(starget) = tinfo->curr.ppr_options & MSG_EXT_PPR_WR_FLOW ? 1 : 0;
		spi_hold_mcs(starget) = tinfo->curr.ppr_options & MSG_EXT_PPR_HOLD_MCS ? 1 : 0;
		spi_display_xfer_agreement(starget);
		break;
	}
        case AC_SENT_BDR:
	{
		WARN_ON(lun != CAM_LUN_WILDCARD);
		scsi_report_device_reset(ahd->platform_data->host,
					 channel - 'A', target);
		break;
	}
        case AC_BUS_RESET:
		if (ahd->platform_data->host != NULL) {
			scsi_report_bus_reset(ahd->platform_data->host,
					      channel - 'A');
		}
                break;
        default:
                panic("ahd_send_async: Unexpected async event");
        }
}

/*
 * Calls the higher level scsi done function and frees the scb.
 */
void
ahd_done(struct ahd_softc *ahd, struct scb *scb)
{
	struct scsi_cmnd *cmd;
	struct	  ahd_linux_device *dev;

	if ((scb->flags & SCB_ACTIVE) == 0) {
		printf("SCB %d done'd twice\n", SCB_GET_TAG(scb));
		ahd_dump_card_state(ahd);
		panic("Stopping for safety");
	}
	LIST_REMOVE(scb, pending_links);
	cmd = scb->io_ctx;
	dev = scb->platform_data->dev;
	dev->active--;
	dev->openings++;
	if ((cmd->result & (CAM_DEV_QFRZN << 16)) != 0) {
		cmd->result &= ~(CAM_DEV_QFRZN << 16);
		dev->qfrozen--;
	}
	ahd_linux_unmap_scb(ahd, scb);

	/*
	 * Guard against stale sense data.
	 * The Linux mid-layer assumes that sense
	 * was retrieved anytime the first byte of
	 * the sense buffer looks "sane".
	 */
	cmd->sense_buffer[0] = 0;
	if (ahd_get_transaction_status(scb) == CAM_REQ_INPROG) {
		uint32_t amount_xferred;

		amount_xferred =
		    ahd_get_transfer_length(scb) - ahd_get_residual(scb);
		if ((scb->flags & SCB_TRANSMISSION_ERROR) != 0) {
#ifdef AHD_DEBUG
			if ((ahd_debug & AHD_SHOW_MISC) != 0) {
				ahd_print_path(ahd, scb);
				printf("Set CAM_UNCOR_PARITY\n");
			}
#endif
			ahd_set_transaction_status(scb, CAM_UNCOR_PARITY);
#ifdef AHD_REPORT_UNDERFLOWS
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

			ahd_print_path(ahd, scb);
			printf("CDB:");
			for (i = 0; i < scb->io_ctx->cmd_len; i++)
				printf(" 0x%x", scb->io_ctx->cmnd[i]);
			printf("\n");
			ahd_print_path(ahd, scb);
			printf("Saw underflow (%ld of %ld bytes). "
			       "Treated as error\n",
				ahd_get_residual(scb),
				ahd_get_transfer_length(scb));
			ahd_set_transaction_status(scb, CAM_DATA_RUN_ERR);
#endif
		} else {
			ahd_set_transaction_status(scb, CAM_REQ_CMP);
		}
	} else if (ahd_get_transaction_status(scb) == CAM_SCSI_STATUS_ERROR) {
		ahd_linux_handle_scsi_status(ahd, cmd->device, scb);
	}

	if (dev->openings == 1
	 && ahd_get_transaction_status(scb) == CAM_REQ_CMP
	 && ahd_get_scsi_status(scb) != SCSI_STATUS_QUEUE_FULL)
		dev->tag_success_count++;
	/*
	 * Some devices deal with temporary internal resource
	 * shortages by returning queue full.  When the queue
	 * full occurrs, we throttle back.  Slowly try to get
	 * back to our previous queue depth.
	 */
	if ((dev->openings + dev->active) < dev->maxtags
	 && dev->tag_success_count > AHD_TAG_SUCCESS_INTERVAL) {
		dev->tag_success_count = 0;
		dev->openings++;
	}

	if (dev->active == 0)
		dev->commands_since_idle_or_otag = 0;

	if ((scb->flags & SCB_RECOVERY_SCB) != 0) {
		printf("Recovery SCB completes\n");
		if (ahd_get_transaction_status(scb) == CAM_BDR_SENT
		 || ahd_get_transaction_status(scb) == CAM_REQ_ABORTED)
			ahd_set_transaction_status(scb, CAM_CMD_TIMEOUT);

		if (ahd->platform_data->eh_done)
			complete(ahd->platform_data->eh_done);
	}

	ahd_free_scb(ahd, scb);
	ahd_linux_queue_cmd_complete(ahd, cmd);
}

static void
ahd_linux_handle_scsi_status(struct ahd_softc *ahd,
			     struct scsi_device *sdev, struct scb *scb)
{
	struct	ahd_devinfo devinfo;
	struct ahd_linux_device *dev = scsi_transport_device_data(sdev);

	ahd_compile_devinfo(&devinfo,
			    ahd->our_id,
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
	switch (ahd_get_scsi_status(scb)) {
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
		if ((scb->flags & (SCB_SENSE|SCB_PKT_SENSE)) != 0) {
			struct scsi_status_iu_header *siu;
			u_int sense_size;
			u_int sense_offset;

			if (scb->flags & SCB_SENSE) {
				sense_size = min(sizeof(struct scsi_sense_data)
					       - ahd_get_sense_residual(scb),
						 (u_long)sizeof(cmd->sense_buffer));
				sense_offset = 0;
			} else {
				/*
				 * Copy only the sense data into the provided
				 * buffer.
				 */
				siu = (struct scsi_status_iu_header *)
				    scb->sense_data;
				sense_size = min_t(size_t,
						scsi_4btoul(siu->sense_length),
						sizeof(cmd->sense_buffer));
				sense_offset = SIU_SENSE_OFFSET(siu);
			}

			memset(cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));
			memcpy(cmd->sense_buffer,
			       ahd_get_sense_buf(ahd, scb)
			       + sense_offset, sense_size);
			cmd->result |= (DRIVER_SENSE << 24);

#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_SENSE) {
				int i;

				printf("Copied %d bytes of sense data at %d:",
				       sense_size, sense_offset);
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
#ifdef AHD_DEBUG
			if ((ahd_debug & AHD_SHOW_QFULL) != 0) {
				ahd_print_path(ahd, scb);
				printf("Dropping tag count to %d\n",
				       dev->active);
			}
#endif
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
				 == AHD_LOCK_TAGS_COUNT) {
					dev->maxtags = dev->active;
					ahd_print_path(ahd, scb);
					printf("Locking max tag count at %d\n",
					       dev->active);
				}
			} else {
				dev->tags_on_last_queuefull = dev->active;
				dev->last_queuefull_same_count = 0;
			}
			ahd_set_transaction_status(scb, CAM_REQUEUE_REQ);
			ahd_set_scsi_status(scb, SCSI_STATUS_OK);
			ahd_platform_set_tags(ahd, sdev, &devinfo,
				     (dev->flags & AHD_DEV_Q_BASIC)
				   ? AHD_QUEUE_BASIC : AHD_QUEUE_TAGGED);
			break;
		}
		/*
		 * Drop down to a single opening, and treat this
		 * as if the target returned BUSY SCSI status.
		 */
		dev->openings = 1;
		ahd_platform_set_tags(ahd, sdev, &devinfo,
			     (dev->flags & AHD_DEV_Q_BASIC)
			   ? AHD_QUEUE_BASIC : AHD_QUEUE_TAGGED);
		ahd_set_scsi_status(scb, SCSI_STATUS_BUSY);
	}
}

static void
ahd_linux_queue_cmd_complete(struct ahd_softc *ahd, struct scsi_cmnd *cmd)
{
	int status;
	int new_status = DID_OK;
	int do_fallback = 0;
	int scsi_status;

	/*
	 * Map CAM error codes into Linux Error codes.  We
	 * avoid the conversion so that the DV code has the
	 * full error information available when making
	 * state change decisions.
	 */

	status = ahd_cmd_get_transaction_status(cmd);
	switch (status) {
	case CAM_REQ_INPROG:
	case CAM_REQ_CMP:
		new_status = DID_OK;
		break;
	case CAM_AUTOSENSE_FAIL:
		new_status = DID_ERROR;
		/* Fallthrough */
	case CAM_SCSI_STATUS_ERROR:
		scsi_status = ahd_cmd_get_scsi_status(cmd);

		switch(scsi_status) {
		case SCSI_STATUS_CMD_TERMINATED:
		case SCSI_STATUS_CHECK_COND:
			if ((cmd->result >> 24) != DRIVER_SENSE) {
				do_fallback = 1;
			} else {
				struct scsi_sense_data *sense;
				
				sense = (struct scsi_sense_data *)
					&cmd->sense_buffer;
				if (sense->extra_len >= 5 &&
				    (sense->add_sense_code == 0x47
				     || sense->add_sense_code == 0x48))
					do_fallback = 1;
			}
			break;
		default:
			break;
		}
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
		do_fallback = 1;
		break;
	case CAM_CMD_TIMEOUT:
		new_status = DID_TIME_OUT;
		do_fallback = 1;
		break;
	case CAM_REQ_CMP_ERR:
	case CAM_UNEXP_BUSFREE:
	case CAM_DATA_RUN_ERR:
		new_status = DID_ERROR;
		do_fallback = 1;
		break;
	case CAM_UA_ABORT:
	case CAM_NO_HBA:
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

	if (do_fallback) {
		printf("%s: device overrun (status %x) on %d:%d:%d\n",
		       ahd_name(ahd), status, cmd->device->channel,
		       cmd->device->id, cmd->device->lun);
	}

	ahd_cmd_set_transaction_status(cmd, new_status);

	cmd->scsi_done(cmd);
}

void
ahd_freeze_simq(struct ahd_softc *ahd)
{
	scsi_block_requests(ahd->platform_data->host);
}

void
ahd_release_simq(struct ahd_softc *ahd)
{
	scsi_unblock_requests(ahd->platform_data->host);
}

static int
ahd_linux_queue_abort_cmd(struct scsi_cmnd *cmd)
{
	struct ahd_softc *ahd;
	struct ahd_linux_device *dev;
	struct scb *pending_scb;
	u_int  saved_scbptr;
	u_int  active_scbptr;
	u_int  last_phase;
	u_int  saved_scsiid;
	u_int  cdb_byte;
	int    retval;
	int    was_paused;
	int    paused;
	int    wait;
	int    disconnected;
	ahd_mode_state saved_modes;
	unsigned long flags;

	pending_scb = NULL;
	paused = FALSE;
	wait = FALSE;
	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;

	scmd_printk(KERN_INFO, cmd,
		    "Attempting to queue an ABORT message:");

	printf("CDB:");
	for (cdb_byte = 0; cdb_byte < cmd->cmd_len; cdb_byte++)
		printf(" 0x%x", cmd->cmnd[cdb_byte]);
	printf("\n");

	ahd_lock(ahd, &flags);

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
		scmd_printk(KERN_INFO, cmd, "Is not an active device\n");
		retval = SUCCESS;
		goto no_cmd;
	}

	/*
	 * See if we can find a matching cmd in the pending list.
	 */
	LIST_FOREACH(pending_scb, &ahd->pending_scbs, pending_links) {
		if (pending_scb->io_ctx == cmd)
			break;
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
	 * behind our back.  Also make sure that we
	 * didn't "just" miss an interrupt that would
	 * affect this cmd.
	 */
	was_paused = ahd_is_paused(ahd);
	ahd_pause_and_flushwork(ahd);
	paused = TRUE;

	if ((pending_scb->flags & SCB_ACTIVE) == 0) {
		scmd_printk(KERN_INFO, cmd, "Command already completed\n");
		goto no_cmd;
	}

	printf("%s: At time of recovery, card was %spaused\n",
	       ahd_name(ahd), was_paused ? "" : "not ");
	ahd_dump_card_state(ahd);

	disconnected = TRUE;
	if (ahd_search_qinfifo(ahd, cmd->device->id, 
			       cmd->device->channel + 'A',
			       cmd->device->lun, 
			       pending_scb->hscb->tag,
			       ROLE_INITIATOR, CAM_REQ_ABORTED,
			       SEARCH_COMPLETE) > 0) {
		printf("%s:%d:%d:%d: Cmd aborted from QINFIFO\n",
		       ahd_name(ahd), cmd->device->channel, 
		       cmd->device->id, cmd->device->lun);
		retval = SUCCESS;
		goto done;
	}

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	last_phase = ahd_inb(ahd, LASTPHASE);
	saved_scbptr = ahd_get_scbptr(ahd);
	active_scbptr = saved_scbptr;
	if (disconnected && (ahd_inb(ahd, SEQ_FLAGS) & NOT_IDENTIFIED) == 0) {
		struct scb *bus_scb;

		bus_scb = ahd_lookup_scb(ahd, active_scbptr);
		if (bus_scb == pending_scb)
			disconnected = FALSE;
	}

	/*
	 * At this point, pending_scb is the scb associated with the
	 * passed in command.  That command is currently active on the
	 * bus or is in the disconnected state.
	 */
	saved_scsiid = ahd_inb(ahd, SAVED_SCSIID);
	if (last_phase != P_BUSFREE
	    && SCB_GET_TAG(pending_scb) == active_scbptr) {

		/*
		 * We're active on the bus, so assert ATN
		 * and hope that the target responds.
		 */
		pending_scb = ahd_lookup_scb(ahd, active_scbptr);
		pending_scb->flags |= SCB_RECOVERY_SCB|SCB_ABORT;
		ahd_outb(ahd, MSG_OUT, HOST_MSG);
		ahd_outb(ahd, SCSISIGO, last_phase|ATNO);
		scmd_printk(KERN_INFO, cmd, "Device is active, asserting ATN\n");
		wait = TRUE;
	} else if (disconnected) {

		/*
		 * Actually re-queue this SCB in an attempt
		 * to select the device before it reconnects.
		 */
		pending_scb->flags |= SCB_RECOVERY_SCB|SCB_ABORT;
		ahd_set_scbptr(ahd, SCB_GET_TAG(pending_scb));
		pending_scb->hscb->cdb_len = 0;
		pending_scb->hscb->task_attribute = 0;
		pending_scb->hscb->task_management = SIU_TASKMGMT_ABORT_TASK;

		if ((pending_scb->flags & SCB_PACKETIZED) != 0) {
			/*
			 * Mark the SCB has having an outstanding
			 * task management function.  Should the command
			 * complete normally before the task management
			 * function can be sent, the host will be notified
			 * to abort our requeued SCB.
			 */
			ahd_outb(ahd, SCB_TASK_MANAGEMENT,
				 pending_scb->hscb->task_management);
		} else {
			/*
			 * If non-packetized, set the MK_MESSAGE control
			 * bit indicating that we desire to send a message.
			 * We also set the disconnected flag since there is
			 * no guarantee that our SCB control byte matches
			 * the version on the card.  We don't want the
			 * sequencer to abort the command thinking an
			 * unsolicited reselection occurred.
			 */
			pending_scb->hscb->control |= MK_MESSAGE|DISCONNECTED;

			/*
			 * The sequencer will never re-reference the
			 * in-core SCB.  To make sure we are notified
			 * during reslection, set the MK_MESSAGE flag in
			 * the card's copy of the SCB.
			 */
			ahd_outb(ahd, SCB_CONTROL,
				 ahd_inb(ahd, SCB_CONTROL)|MK_MESSAGE);
		}

		/*
		 * Clear out any entries in the QINFIFO first
		 * so we are the next SCB for this target
		 * to run.
		 */
		ahd_search_qinfifo(ahd, cmd->device->id,
				   cmd->device->channel + 'A', cmd->device->lun,
				   SCB_LIST_NULL, ROLE_INITIATOR,
				   CAM_REQUEUE_REQ, SEARCH_COMPLETE);
		ahd_qinfifo_requeue_tail(ahd, pending_scb);
		ahd_set_scbptr(ahd, saved_scbptr);
		ahd_print_path(ahd, pending_scb);
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
		ahd_unpause(ahd);
	if (wait) {
		DECLARE_COMPLETION_ONSTACK(done);

		ahd->platform_data->eh_done = &done;
		ahd_unlock(ahd, &flags);

		printf("%s: Recovery code sleeping\n", ahd_name(ahd));
		if (!wait_for_completion_timeout(&done, 5 * HZ)) {
			ahd_lock(ahd, &flags);
			ahd->platform_data->eh_done = NULL;
			ahd_unlock(ahd, &flags);
			printf("%s: Timer Expired (active %d)\n",
			       ahd_name(ahd), dev->active);
			retval = FAILED;
		}
		printf("Recovery code awake\n");
	} else
		ahd_unlock(ahd, &flags);

	if (retval != SUCCESS)
		printf("%s: Command abort returning 0x%x\n",
		       ahd_name(ahd), retval);

	return retval;
}

static void ahd_linux_set_width(struct scsi_target *starget, int width)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_devinfo devinfo;
	unsigned long flags;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_lock(ahd, &flags);
	ahd_set_width(ahd, &devinfo, width, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_period(struct scsi_target *starget, int period)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options;
	unsigned int dt;
	unsigned long flags;
	unsigned long offset = tinfo->goal.offset;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: set period to %d\n", ahd_name(ahd), period);
#endif
	if (offset == 0)
		offset = MAX_OFFSET;

	if (period < 8)
		period = 8;
	if (period < 10) {
		ppr_options |= MSG_EXT_PPR_DT_REQ;
		if (period == 8)
			ppr_options |= MSG_EXT_PPR_IU_REQ;
	}

	dt = ppr_options & MSG_EXT_PPR_DT_REQ;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);

	/* all PPR requests apart from QAS require wide transfers */
	if (ppr_options & ~MSG_EXT_PPR_QAS_REQ) {
		if (spi_width(starget) == 0)
			ppr_options &= MSG_EXT_PPR_QAS_REQ;
	}

	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_offset(struct scsi_target *starget, int offset)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = 0;
	unsigned int period = 0;
	unsigned int dt = ppr_options & MSG_EXT_PPR_DT_REQ;
	unsigned long flags;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: set offset to %d\n", ahd_name(ahd), offset);
#endif

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	if (offset != 0) {
		period = tinfo->goal.period;
		ppr_options = tinfo->goal.ppr_options;
		ahd_find_syncrate(ahd, &period, &ppr_options, 
				  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);
	}

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, offset, ppr_options,
			 AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_dt(struct scsi_target *starget, int dt)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_DT_REQ;
	unsigned int period = tinfo->goal.period;
	unsigned int width = tinfo->goal.width;
	unsigned long flags;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: %s DT\n", ahd_name(ahd), 
		       dt ? "enabling" : "disabling");
#endif
	if (dt) {
		ppr_options |= MSG_EXT_PPR_DT_REQ;
		if (!width)
			ahd_linux_set_width(starget, 1);
	} else {
		if (period <= 9)
			period = 10; /* If resetting DT, period must be >= 25ns */
		/* IU is invalid without DT set */
		ppr_options &= ~MSG_EXT_PPR_IU_REQ;
	}
	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_qas(struct scsi_target *starget, int qas)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_QAS_REQ;
	unsigned int period = tinfo->goal.period;
	unsigned int dt;
	unsigned long flags;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: %s QAS\n", ahd_name(ahd), 
		       qas ? "enabling" : "disabling");
#endif

	if (qas) {
		ppr_options |= MSG_EXT_PPR_QAS_REQ; 
	}

	dt = ppr_options & MSG_EXT_PPR_DT_REQ;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_iu(struct scsi_target *starget, int iu)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_IU_REQ;
	unsigned int period = tinfo->goal.period;
	unsigned int dt;
	unsigned long flags;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: %s IU\n", ahd_name(ahd),
		       iu ? "enabling" : "disabling");
#endif

	if (iu) {
		ppr_options |= MSG_EXT_PPR_IU_REQ;
		ppr_options |= MSG_EXT_PPR_DT_REQ; /* IU requires DT */
	}

	dt = ppr_options & MSG_EXT_PPR_DT_REQ;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_rd_strm(struct scsi_target *starget, int rdstrm)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_RD_STRM;
	unsigned int period = tinfo->goal.period;
	unsigned int dt = ppr_options & MSG_EXT_PPR_DT_REQ;
	unsigned long flags;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: %s Read Streaming\n", ahd_name(ahd), 
		       rdstrm  ? "enabling" : "disabling");
#endif

	if (rdstrm)
		ppr_options |= MSG_EXT_PPR_RD_STRM;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_wr_flow(struct scsi_target *starget, int wrflow)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_WR_FLOW;
	unsigned int period = tinfo->goal.period;
	unsigned int dt = ppr_options & MSG_EXT_PPR_DT_REQ;
	unsigned long flags;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: %s Write Flow Control\n", ahd_name(ahd),
		       wrflow ? "enabling" : "disabling");
#endif

	if (wrflow)
		ppr_options |= MSG_EXT_PPR_WR_FLOW;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_rti(struct scsi_target *starget, int rti)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_RTI;
	unsigned int period = tinfo->goal.period;
	unsigned int dt = ppr_options & MSG_EXT_PPR_DT_REQ;
	unsigned long flags;

	if ((ahd->features & AHD_RTI) == 0) {
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_DV) != 0)
			printf("%s: RTI not available\n", ahd_name(ahd));
#endif
		return;
	}

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: %s RTI\n", ahd_name(ahd),
		       rti ? "enabling" : "disabling");
#endif

	if (rti)
		ppr_options |= MSG_EXT_PPR_RTI;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_pcomp_en(struct scsi_target *starget, int pcomp)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_PCOMP_EN;
	unsigned int period = tinfo->goal.period;
	unsigned int dt = ppr_options & MSG_EXT_PPR_DT_REQ;
	unsigned long flags;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_DV) != 0)
		printf("%s: %s Precompensation\n", ahd_name(ahd), 
		       pcomp ? "Enable" : "Disable");
#endif

	if (pcomp) {
		uint8_t precomp;

		if (ahd->unit < ARRAY_SIZE(aic79xx_iocell_info)) {
			struct ahd_linux_iocell_opts *iocell_opts;

			iocell_opts = &aic79xx_iocell_info[ahd->unit];
			precomp = iocell_opts->precomp;
		} else {
			precomp = AIC79XX_DEFAULT_PRECOMP;
		}
		ppr_options |= MSG_EXT_PPR_PCOMP_EN;
		AHD_SET_PRECOMP(ahd, precomp);
	} else {
		AHD_SET_PRECOMP(ahd, 0);
	}

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}

static void ahd_linux_set_hold_mcs(struct scsi_target *starget, int hold)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ahd_softc *ahd = *((struct ahd_softc **)shost->hostdata);
	struct ahd_tmode_tstate *tstate;
	struct ahd_initiator_tinfo *tinfo 
		= ahd_fetch_transinfo(ahd,
				      starget->channel + 'A',
				      shost->this_id, starget->id, &tstate);
	struct ahd_devinfo devinfo;
	unsigned int ppr_options = tinfo->goal.ppr_options
		& ~MSG_EXT_PPR_HOLD_MCS;
	unsigned int period = tinfo->goal.period;
	unsigned int dt = ppr_options & MSG_EXT_PPR_DT_REQ;
	unsigned long flags;

	if (hold)
		ppr_options |= MSG_EXT_PPR_HOLD_MCS;

	ahd_compile_devinfo(&devinfo, shost->this_id, starget->id, 0,
			    starget->channel + 'A', ROLE_INITIATOR);
	ahd_find_syncrate(ahd, &period, &ppr_options,
			  dt ? AHD_SYNCRATE_MAX : AHD_SYNCRATE_ULTRA2);

	ahd_lock(ahd, &flags);
	ahd_set_syncrate(ahd, &devinfo, period, tinfo->goal.offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	ahd_unlock(ahd, &flags);
}



static struct spi_function_template ahd_linux_transport_functions = {
	.set_offset	= ahd_linux_set_offset,
	.show_offset	= 1,
	.set_period	= ahd_linux_set_period,
	.show_period	= 1,
	.set_width	= ahd_linux_set_width,
	.show_width	= 1,
	.set_dt		= ahd_linux_set_dt,
	.show_dt	= 1,
	.set_iu		= ahd_linux_set_iu,
	.show_iu	= 1,
	.set_qas	= ahd_linux_set_qas,
	.show_qas	= 1,
	.set_rd_strm	= ahd_linux_set_rd_strm,
	.show_rd_strm	= 1,
	.set_wr_flow	= ahd_linux_set_wr_flow,
	.show_wr_flow	= 1,
	.set_rti	= ahd_linux_set_rti,
	.show_rti	= 1,
	.set_pcomp_en	= ahd_linux_set_pcomp_en,
	.show_pcomp_en	= 1,
	.set_hold_mcs	= ahd_linux_set_hold_mcs,
	.show_hold_mcs	= 1,
};

static int __init
ahd_linux_init(void)
{
	int	error = 0;

	/*
	 * If we've been passed any parameters, process them now.
	 */
	if (aic79xx)
		aic79xx_setup(aic79xx);

	ahd_linux_transport_template =
		spi_attach_transport(&ahd_linux_transport_functions);
	if (!ahd_linux_transport_template)
		return -ENODEV;

	scsi_transport_reserve_device(ahd_linux_transport_template,
				      sizeof(struct ahd_linux_device));

	error = ahd_linux_pci_init();
	if (error)
		spi_release_transport(ahd_linux_transport_template);
	return error;
}

static void __exit
ahd_linux_exit(void)
{
	ahd_linux_pci_exit();
	spi_release_transport(ahd_linux_transport_template);
}

module_init(ahd_linux_init);
module_exit(ahd_linux_exit);
