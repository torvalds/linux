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

/*
 * Include aiclib.c as part of our
 * "module dependencies are hard" work around.
 */
#include "aiclib.c"

#include <linux/init.h>		/* __setup */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include "sd.h"			/* For geometry detection */
#endif

#include <linux/mm.h>		/* For fetching system memory size */
#include <linux/delay.h>	/* For ssleep/msleep */

/*
 * Lock protecting manipulation of the ahd softc list.
 */
spinlock_t ahd_list_spinlock;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
/* For dynamic sglist size calculation. */
u_int ahd_linux_nseg;
#endif

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
 * By default, read streaming is disabled.  In theory,
 * read streaming should enhance performance, but early
 * U320 drive firmware actually performs slower with
 * read streaming enabled.
 */
#ifdef CONFIG_AIC79XX_ENABLE_RD_STRM
#define AIC79XX_CONFIGED_RD_STRM 0xFFFF
#else
#define AIC79XX_CONFIGED_RD_STRM 0
#endif

static uint16_t aic79xx_rd_strm_info[] =
{
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM,
	AIC79XX_CONFIGED_RD_STRM
};

/*
 * DV option:
 *
 * positive value = DV Enabled
 * zero		  = DV Disabled
 * negative value = DV Default for adapter type/seeprom
 */
#ifdef CONFIG_AIC79XX_DV_SETTING
#define AIC79XX_CONFIGED_DV CONFIG_AIC79XX_DV_SETTING
#else
#define AIC79XX_CONFIGED_DV -1
#endif

static int8_t aic79xx_dv_settings[] =
{
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV,
	AIC79XX_CONFIGED_DV
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
 * Certain PCI motherboards will scan PCI devices from highest to lowest,
 * others scan from lowest to highest, and they tend to do all kinds of
 * strange things when they come into contact with PCI bridge chips.  The
 * net result of all this is that the PCI card that is actually used to boot
 * the machine is very hard to detect.  Most motherboards go from lowest
 * PCI slot number to highest, and the first SCSI controller found is the
 * one you boot from.  The only exceptions to this are when a controller
 * has its BIOS disabled.  So, we by default sort all of our SCSI controllers
 * from lowest PCI slot number to highest PCI slot number.  We also force
 * all controllers with their BIOS disabled to the end of the list.  This
 * works on *almost* all computers.  Where it doesn't work, we have this
 * option.  Setting this option to non-0 will reverse the order of the sort
 * to highest first, then lowest, but will still leave cards with their BIOS
 * disabled at the very end.  That should fix everyone up unless there are
 * really strange cirumstances.
 */
static uint32_t aic79xx_reverse_scan;

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
 * aic79xx_detect() has been run, so register all device arrivals
 * immediately with the system rather than deferring to the sorted
 * attachment performed by aic79xx_detect().
 */
int aic79xx_detect_complete;

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
uint32_t aic79xx_periodic_otag;

/*
 * Module information and settable options.
 */
static char *aic79xx = NULL;

MODULE_AUTHOR("Maintainer: Justin T. Gibbs <gibbs@scsiguy.com>");
MODULE_DESCRIPTION("Adaptec Aic790X U320 SCSI Host Bus Adapter driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(AIC79XX_DRIVER_VERSION);
module_param(aic79xx, charp, 0);
MODULE_PARM_DESC(aic79xx,
"period delimited, options string.\n"
"	verbose			Enable verbose/diagnostic logging\n"
"	allow_memio		Allow device registers to be memory mapped\n"
"	debug			Bitmask of debug values to enable\n"
"	no_reset		Supress initial bus resets\n"
"	extended		Enable extended geometry on all controllers\n"
"	periodic_otag		Send an ordered tagged transaction\n"
"				periodically to prevent tag starvation.\n"
"				This may be required by some older disk\n"
"				or drives/RAID arrays.\n"
"	reverse_scan		Sort PCI devices highest Bus/Slot to lowest\n"
"	tag_info:<tag_str>	Set per-target tag depth\n"
"	global_tag_depth:<int>	Global tag depth for all targets on all buses\n"
"	rd_strm:<rd_strm_masks> Set per-target read streaming setting.\n"
"	dv:<dv_settings>	Set per-controller Domain Validation Setting.\n"
"	slewrate:<slewrate_list>Set the signal slew rate (0-15).\n"
"	precomp:<pcomp_list>	Set the signal precompensation (0-7).\n"
"	amplitude:<int>		Set the signal amplitude (0-7).\n"
"	seltime:<int>		Selection Timeout:\n"
"				(0/256ms,1/128ms,2/64ms,3/32ms)\n"
"\n"
"	Sample /etc/modprobe.conf line:\n"
"		Enable verbose logging\n"
"		Set tag depth on Controller 2/Target 2 to 10 tags\n"
"		Shorten the selection timeout to 128ms\n"
"\n"
"	options aic79xx 'aic79xx=verbose.tag_info:{{}.{}.{..10}}.seltime:1'\n"
"\n"
"	Sample /etc/modprobe.conf line:\n"
"		Change Read Streaming for Controller's 2 and 3\n"
"\n"
"	options aic79xx 'aic79xx=rd_strm:{..0xFFF0.0xC0F0}'");

static void ahd_linux_handle_scsi_status(struct ahd_softc *,
					 struct ahd_linux_device *,
					 struct scb *);
static void ahd_linux_queue_cmd_complete(struct ahd_softc *ahd,
					 Scsi_Cmnd *cmd);
static void ahd_linux_filter_inquiry(struct ahd_softc *ahd,
				     struct ahd_devinfo *devinfo);
static void ahd_linux_dev_timed_unfreeze(u_long arg);
static void ahd_linux_sem_timeout(u_long arg);
static void ahd_linux_initialize_scsi_bus(struct ahd_softc *ahd);
static void ahd_linux_size_nseg(void);
static void ahd_linux_thread_run_complete_queue(struct ahd_softc *ahd);
static void ahd_linux_start_dv(struct ahd_softc *ahd);
static void ahd_linux_dv_timeout(struct scsi_cmnd *cmd);
static int  ahd_linux_dv_thread(void *data);
static void ahd_linux_kill_dv_thread(struct ahd_softc *ahd);
static void ahd_linux_dv_target(struct ahd_softc *ahd, u_int target);
static void ahd_linux_dv_transition(struct ahd_softc *ahd,
				    struct scsi_cmnd *cmd,
				    struct ahd_devinfo *devinfo,
				    struct ahd_linux_target *targ);
static void ahd_linux_dv_fill_cmd(struct ahd_softc *ahd,
				  struct scsi_cmnd *cmd,
				  struct ahd_devinfo *devinfo);
static void ahd_linux_dv_inq(struct ahd_softc *ahd,
			     struct scsi_cmnd *cmd,
			     struct ahd_devinfo *devinfo,
			     struct ahd_linux_target *targ,
			     u_int request_length);
static void ahd_linux_dv_tur(struct ahd_softc *ahd,
			     struct scsi_cmnd *cmd,
			     struct ahd_devinfo *devinfo);
static void ahd_linux_dv_rebd(struct ahd_softc *ahd,
			      struct scsi_cmnd *cmd,
			      struct ahd_devinfo *devinfo,
			      struct ahd_linux_target *targ);
static void ahd_linux_dv_web(struct ahd_softc *ahd,
			     struct scsi_cmnd *cmd,
			     struct ahd_devinfo *devinfo,
			     struct ahd_linux_target *targ);
static void ahd_linux_dv_reb(struct ahd_softc *ahd,
			     struct scsi_cmnd *cmd,
			     struct ahd_devinfo *devinfo,
			     struct ahd_linux_target *targ);
static void ahd_linux_dv_su(struct ahd_softc *ahd,
			    struct scsi_cmnd *cmd,
			    struct ahd_devinfo *devinfo,
			    struct ahd_linux_target *targ);
static int ahd_linux_fallback(struct ahd_softc *ahd,
			      struct ahd_devinfo *devinfo);
static __inline int ahd_linux_dv_fallback(struct ahd_softc *ahd,
					  struct ahd_devinfo *devinfo);
static void ahd_linux_dv_complete(Scsi_Cmnd *cmd);
static void ahd_linux_generate_dv_pattern(struct ahd_linux_target *targ);
static u_int ahd_linux_user_tagdepth(struct ahd_softc *ahd,
				     struct ahd_devinfo *devinfo);
static u_int ahd_linux_user_dv_setting(struct ahd_softc *ahd);
static void ahd_linux_setup_user_rd_strm_settings(struct ahd_softc *ahd);
static void ahd_linux_device_queue_depth(struct ahd_softc *ahd,
					 struct ahd_linux_device *dev);
static struct ahd_linux_target*	ahd_linux_alloc_target(struct ahd_softc*,
						       u_int, u_int);
static void			ahd_linux_free_target(struct ahd_softc*,
						      struct ahd_linux_target*);
static struct ahd_linux_device*	ahd_linux_alloc_device(struct ahd_softc*,
						       struct ahd_linux_target*,
						       u_int);
static void			ahd_linux_free_device(struct ahd_softc*,
						      struct ahd_linux_device*);
static void ahd_linux_run_device_queue(struct ahd_softc*,
				       struct ahd_linux_device*);
static void ahd_linux_setup_tag_info_global(char *p);
static aic_option_callback_t ahd_linux_setup_tag_info;
static aic_option_callback_t ahd_linux_setup_rd_strm_info;
static aic_option_callback_t ahd_linux_setup_dv;
static aic_option_callback_t ahd_linux_setup_iocell_info;
static int ahd_linux_next_unit(void);
static void ahd_runq_tasklet(unsigned long data);
static int aic79xx_setup(char *c);

/****************************** Inlines ***************************************/
static __inline void ahd_schedule_completeq(struct ahd_softc *ahd);
static __inline void ahd_schedule_runq(struct ahd_softc *ahd);
static __inline void ahd_setup_runq_tasklet(struct ahd_softc *ahd);
static __inline void ahd_teardown_runq_tasklet(struct ahd_softc *ahd);
static __inline struct ahd_linux_device*
		     ahd_linux_get_device(struct ahd_softc *ahd, u_int channel,
					  u_int target, u_int lun, int alloc);
static struct ahd_cmd *ahd_linux_run_complete_queue(struct ahd_softc *ahd);
static __inline void ahd_linux_check_device_queue(struct ahd_softc *ahd,
						  struct ahd_linux_device *dev);
static __inline struct ahd_linux_device *
		     ahd_linux_next_device_to_run(struct ahd_softc *ahd);
static __inline void ahd_linux_run_device_queues(struct ahd_softc *ahd);
static __inline void ahd_linux_unmap_scb(struct ahd_softc*, struct scb*);

static __inline void
ahd_schedule_completeq(struct ahd_softc *ahd)
{
	if ((ahd->platform_data->flags & AHD_RUN_CMPLT_Q_TIMER) == 0) {
		ahd->platform_data->flags |= AHD_RUN_CMPLT_Q_TIMER;
		ahd->platform_data->completeq_timer.expires = jiffies;
		add_timer(&ahd->platform_data->completeq_timer);
	}
}

/*
 * Must be called with our lock held.
 */
static __inline void
ahd_schedule_runq(struct ahd_softc *ahd)
{
	tasklet_schedule(&ahd->platform_data->runq_tasklet);
}

static __inline
void ahd_setup_runq_tasklet(struct ahd_softc *ahd)
{
	tasklet_init(&ahd->platform_data->runq_tasklet, ahd_runq_tasklet,
		     (unsigned long)ahd);
}

static __inline void
ahd_teardown_runq_tasklet(struct ahd_softc *ahd)
{
	tasklet_kill(&ahd->platform_data->runq_tasklet);
}

static __inline struct ahd_linux_device*
ahd_linux_get_device(struct ahd_softc *ahd, u_int channel, u_int target,
		     u_int lun, int alloc)
{
	struct ahd_linux_target *targ;
	struct ahd_linux_device *dev;
	u_int target_offset;

	target_offset = target;
	if (channel != 0)
		target_offset += 8;
	targ = ahd->platform_data->targets[target_offset];
	if (targ == NULL) {
		if (alloc != 0) {
			targ = ahd_linux_alloc_target(ahd, channel, target);
			if (targ == NULL)
				return (NULL);
		} else
			return (NULL);
	}
	dev = targ->devices[lun];
	if (dev == NULL && alloc != 0)
		dev = ahd_linux_alloc_device(ahd, targ, lun);
	return (dev);
}

#define AHD_LINUX_MAX_RETURNED_ERRORS 4
static struct ahd_cmd *
ahd_linux_run_complete_queue(struct ahd_softc *ahd)
{	
	struct	ahd_cmd *acmd;
	u_long	done_flags;
	int	with_errors;

	with_errors = 0;
	ahd_done_lock(ahd, &done_flags);
	while ((acmd = TAILQ_FIRST(&ahd->platform_data->completeq)) != NULL) {
		Scsi_Cmnd *cmd;

		if (with_errors > AHD_LINUX_MAX_RETURNED_ERRORS) {
			/*
			 * Linux uses stack recursion to requeue
			 * commands that need to be retried.  Avoid
			 * blowing out the stack by "spoon feeding"
			 * commands that completed with error back
			 * the operating system in case they are going
			 * to be retried. "ick"
			 */
			ahd_schedule_completeq(ahd);
			break;
		}
		TAILQ_REMOVE(&ahd->platform_data->completeq,
			     acmd, acmd_links.tqe);
		cmd = &acmd_scsi_cmd(acmd);
		cmd->host_scribble = NULL;
		if (ahd_cmd_get_transaction_status(cmd) != DID_OK
		 || (cmd->result & 0xFF) != SCSI_STATUS_OK)
			with_errors++;

		cmd->scsi_done(cmd);
	}
	ahd_done_unlock(ahd, &done_flags);
	return (acmd);
}

static __inline void
ahd_linux_check_device_queue(struct ahd_softc *ahd,
			     struct ahd_linux_device *dev)
{
	if ((dev->flags & AHD_DEV_FREEZE_TIL_EMPTY) != 0
	 && dev->active == 0) {
		dev->flags &= ~AHD_DEV_FREEZE_TIL_EMPTY;
		dev->qfrozen--;
	}

	if (TAILQ_FIRST(&dev->busyq) == NULL
	 || dev->openings == 0 || dev->qfrozen != 0)
		return;

	ahd_linux_run_device_queue(ahd, dev);
}

static __inline struct ahd_linux_device *
ahd_linux_next_device_to_run(struct ahd_softc *ahd)
{
	
	if ((ahd->flags & AHD_RESOURCE_SHORTAGE) != 0
	 || (ahd->platform_data->qfrozen != 0
	  && AHD_DV_SIMQ_FROZEN(ahd) == 0))
		return (NULL);
	return (TAILQ_FIRST(&ahd->platform_data->device_runq));
}

static __inline void
ahd_linux_run_device_queues(struct ahd_softc *ahd)
{
	struct ahd_linux_device *dev;

	while ((dev = ahd_linux_next_device_to_run(ahd)) != NULL) {
		TAILQ_REMOVE(&ahd->platform_data->device_runq, dev, links);
		dev->flags &= ~AHD_DEV_ON_RUN_LIST;
		ahd_linux_check_device_queue(ahd, dev);
	}
}

static __inline void
ahd_linux_unmap_scb(struct ahd_softc *ahd, struct scb *scb)
{
	Scsi_Cmnd *cmd;
	int direction;

	cmd = scb->io_ctx;
	direction = scsi_to_pci_dma_dir(cmd->sc_data_direction);
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
	((((cmd)->device->id << TID_SHIFT) & TID) | (ahd)->our_id)

/************************  Host template entry points *************************/
static int	   ahd_linux_detect(Scsi_Host_Template *);
static const char *ahd_linux_info(struct Scsi_Host *);
static int	   ahd_linux_queue(Scsi_Cmnd *, void (*)(Scsi_Cmnd *));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static int	   ahd_linux_slave_alloc(Scsi_Device *);
static int	   ahd_linux_slave_configure(Scsi_Device *);
static void	   ahd_linux_slave_destroy(Scsi_Device *);
#if defined(__i386__)
static int	   ahd_linux_biosparam(struct scsi_device*,
				       struct block_device*, sector_t, int[]);
#endif
#else
static int	   ahd_linux_release(struct Scsi_Host *);
static void	   ahd_linux_select_queue_depth(struct Scsi_Host *host,
						Scsi_Device *scsi_devs);
#if defined(__i386__)
static int	   ahd_linux_biosparam(Disk *, kdev_t, int[]);
#endif
#endif
static int	   ahd_linux_bus_reset(Scsi_Cmnd *);
static int	   ahd_linux_dev_reset(Scsi_Cmnd *);
static int	   ahd_linux_abort(Scsi_Cmnd *);

/*
 * Calculate a safe value for AHD_NSEG (as expressed through ahd_linux_nseg).
 *
 * In pre-2.5.X...
 * The midlayer allocates an S/G array dynamically when a command is issued
 * using SCSI malloc.  This array, which is in an OS dependent format that
 * must later be copied to our private S/G list, is sized to house just the
 * number of segments needed for the current transfer.  Since the code that
 * sizes the SCSI malloc pool does not take into consideration fragmentation
 * of the pool, executing transactions numbering just a fraction of our
 * concurrent transaction limit with SG list lengths aproaching AHC_NSEG will
 * quickly depleat the SCSI malloc pool of usable space.  Unfortunately, the
 * mid-layer does not properly handle this scsi malloc failures for the S/G
 * array and the result can be a lockup of the I/O subsystem.  We try to size
 * our S/G list so that it satisfies our drivers allocation requirements in
 * addition to avoiding fragmentation of the SCSI malloc pool.
 */
static void
ahd_linux_size_nseg(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	u_int cur_size;
	u_int best_size;

	/*
	 * The SCSI allocator rounds to the nearest 512 bytes
	 * an cannot allocate across a page boundary.  Our algorithm
	 * is to start at 1K of scsi malloc space per-command and
	 * loop through all factors of the PAGE_SIZE and pick the best.
	 */
	best_size = 0;
	for (cur_size = 1024; cur_size <= PAGE_SIZE; cur_size *= 2) {
		u_int nseg;

		nseg = cur_size / sizeof(struct scatterlist);
		if (nseg < AHD_LINUX_MIN_NSEG)
			continue;

		if (best_size == 0) {
			best_size = cur_size;
			ahd_linux_nseg = nseg;
		} else {
			u_int best_rem;
			u_int cur_rem;

			/*
			 * Compare the traits of the current "best_size"
			 * with the current size to determine if the
			 * current size is a better size.
			 */
			best_rem = best_size % sizeof(struct scatterlist);
			cur_rem = cur_size % sizeof(struct scatterlist);
			if (cur_rem < best_rem) {
				best_size = cur_size;
				ahd_linux_nseg = nseg;
			}
		}
	}
#endif
}

/*
 * Try to detect an Adaptec 79XX controller.
 */
static int
ahd_linux_detect(Scsi_Host_Template *template)
{
	struct	ahd_softc *ahd;
	int     found;
	int	error = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	/*
	 * It is a bug that the upper layer takes
	 * this lock just prior to calling us.
	 */
	spin_unlock_irq(&io_request_lock);
#endif

	/*
	 * Sanity checking of Linux SCSI data structures so
	 * that some of our hacks^H^H^H^H^Hassumptions aren't
	 * violated.
	 */
	if (offsetof(struct ahd_cmd_internal, end)
	  > offsetof(struct scsi_cmnd, host_scribble)) {
		printf("ahd_linux_detect: SCSI data structures changed.\n");
		printf("ahd_linux_detect: Unable to attach\n");
		return (0);
	}
	/*
	 * Determine an appropriate size for our Scatter Gatther lists.
	 */
	ahd_linux_size_nseg();
#ifdef MODULE
	/*
	 * If we've been passed any parameters, process them now.
	 */
	if (aic79xx)
		aic79xx_setup(aic79xx);
#endif

	template->proc_name = "aic79xx";

	/*
	 * Initialize our softc list lock prior to
	 * probing for any adapters.
	 */
	ahd_list_lockinit();

#ifdef CONFIG_PCI
	error = ahd_linux_pci_init();
	if (error)
		return error;
#endif

	/*
	 * Register with the SCSI layer all
	 * controllers we've found.
	 */
	found = 0;
	TAILQ_FOREACH(ahd, &ahd_tailq, links) {

		if (ahd_linux_register_host(ahd, template) == 0)
			found++;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	spin_lock_irq(&io_request_lock);
#endif
	aic79xx_detect_complete++;
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
/*
 * Free the passed in Scsi_Host memory structures prior to unloading the
 * module.
 */
static int
ahd_linux_release(struct Scsi_Host * host)
{
	struct ahd_softc *ahd;
	u_long l;

	ahd_list_lock(&l);
	if (host != NULL) {

		/*
		 * We should be able to just perform
		 * the free directly, but check our
		 * list for extra sanity.
		 */
		ahd = ahd_find_softc(*(struct ahd_softc **)host->hostdata);
		if (ahd != NULL) {
			u_long s;

			ahd_lock(ahd, &s);
			ahd_intr_enable(ahd, FALSE);
			ahd_unlock(ahd, &s);
			ahd_free(ahd);
		}
	}
	ahd_list_unlock(&l);
	return (0);
}
#endif

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
ahd_linux_queue(Scsi_Cmnd * cmd, void (*scsi_done) (Scsi_Cmnd *))
{
	struct	 ahd_softc *ahd;
	struct	 ahd_linux_device *dev;
	u_long	 flags;

	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;

	/*
	 * Save the callback on completion function.
	 */
	cmd->scsi_done = scsi_done;

	ahd_midlayer_entrypoint_lock(ahd, &flags);

	/*
	 * Close the race of a command that was in the process of
	 * being queued to us just as our simq was frozen.  Let
	 * DV commands through so long as we are only frozen to
	 * perform DV.
	 */
	if (ahd->platform_data->qfrozen != 0
	 && AHD_DV_CMD(cmd) == 0) {

		ahd_cmd_set_transaction_status(cmd, CAM_REQUEUE_REQ);
		ahd_linux_queue_cmd_complete(ahd, cmd);
		ahd_schedule_completeq(ahd);
		ahd_midlayer_entrypoint_unlock(ahd, &flags);
		return (0);
	}
	dev = ahd_linux_get_device(ahd, cmd->device->channel,
				   cmd->device->id, cmd->device->lun,
				   /*alloc*/TRUE);
	if (dev == NULL) {
		ahd_cmd_set_transaction_status(cmd, CAM_RESRC_UNAVAIL);
		ahd_linux_queue_cmd_complete(ahd, cmd);
		ahd_schedule_completeq(ahd);
		ahd_midlayer_entrypoint_unlock(ahd, &flags);
		printf("%s: aic79xx_linux_queue - Unable to allocate device!\n",
		       ahd_name(ahd));
		return (0);
	}
	if (cmd->cmd_len > MAX_CDB_LEN)
		return (-EINVAL);
	cmd->result = CAM_REQ_INPROG << 16;
	TAILQ_INSERT_TAIL(&dev->busyq, (struct ahd_cmd *)cmd, acmd_links.tqe);
	if ((dev->flags & AHD_DEV_ON_RUN_LIST) == 0) {
		TAILQ_INSERT_TAIL(&ahd->platform_data->device_runq, dev, links);
		dev->flags |= AHD_DEV_ON_RUN_LIST;
		ahd_linux_run_device_queues(ahd);
	}
	ahd_midlayer_entrypoint_unlock(ahd, &flags);
	return (0);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static int
ahd_linux_slave_alloc(Scsi_Device *device)
{
	struct	ahd_softc *ahd;

	ahd = *((struct ahd_softc **)device->host->hostdata);
	if (bootverbose)
		printf("%s: Slave Alloc %d\n", ahd_name(ahd), device->id);
	return (0);
}

static int
ahd_linux_slave_configure(Scsi_Device *device)
{
	struct	ahd_softc *ahd;
	struct	ahd_linux_device *dev;
	u_long	flags;

	ahd = *((struct ahd_softc **)device->host->hostdata);
	if (bootverbose)
		printf("%s: Slave Configure %d\n", ahd_name(ahd), device->id);
	ahd_midlayer_entrypoint_lock(ahd, &flags);
	/*
	 * Since Linux has attached to the device, configure
	 * it so we don't free and allocate the device
	 * structure on every command.
	 */
	dev = ahd_linux_get_device(ahd, device->channel,
				   device->id, device->lun,
				   /*alloc*/TRUE);
	if (dev != NULL) {
		dev->flags &= ~AHD_DEV_UNCONFIGURED;
		dev->flags |= AHD_DEV_SLAVE_CONFIGURED;
		dev->scsi_device = device;
		ahd_linux_device_queue_depth(ahd, dev);
	}
	ahd_midlayer_entrypoint_unlock(ahd, &flags);
	return (0);
}

static void
ahd_linux_slave_destroy(Scsi_Device *device)
{
	struct	ahd_softc *ahd;
	struct	ahd_linux_device *dev;
	u_long	flags;

	ahd = *((struct ahd_softc **)device->host->hostdata);
	if (bootverbose)
		printf("%s: Slave Destroy %d\n", ahd_name(ahd), device->id);
	ahd_midlayer_entrypoint_lock(ahd, &flags);
	dev = ahd_linux_get_device(ahd, device->channel,
				   device->id, device->lun,
					   /*alloc*/FALSE);

	/*
	 * Filter out "silly" deletions of real devices by only
	 * deleting devices that have had slave_configure()
	 * called on them.  All other devices that have not
	 * been configured will automatically be deleted by
	 * the refcounting process.
	 */
	if (dev != NULL
	 && (dev->flags & AHD_DEV_SLAVE_CONFIGURED) != 0) {
		dev->flags |= AHD_DEV_UNCONFIGURED;
		if (TAILQ_EMPTY(&dev->busyq)
		 && dev->active == 0
		 && (dev->flags & AHD_DEV_TIMER_ACTIVE) == 0)
			ahd_linux_free_device(ahd, dev);
	}
	ahd_midlayer_entrypoint_unlock(ahd, &flags);
}
#else
/*
 * Sets the queue depth for each SCSI device hanging
 * off the input host adapter.
 */
static void
ahd_linux_select_queue_depth(struct Scsi_Host * host,
			     Scsi_Device * scsi_devs)
{
	Scsi_Device *device;
	Scsi_Device *ldev;
	struct	ahd_softc *ahd;
	u_long	flags;

	ahd = *((struct ahd_softc **)host->hostdata);
	ahd_lock(ahd, &flags);
	for (device = scsi_devs; device != NULL; device = device->next) {

		/*
		 * Watch out for duplicate devices.  This works around
		 * some quirks in how the SCSI scanning code does its
		 * device management.
		 */
		for (ldev = scsi_devs; ldev != device; ldev = ldev->next) {
			if (ldev->host == device->host
			 && ldev->channel == device->channel
			 && ldev->id == device->id
			 && ldev->lun == device->lun)
				break;
		}
		/* Skip duplicate. */
		if (ldev != device)
			continue;

		if (device->host == host) {
			struct	 ahd_linux_device *dev;

			/*
			 * Since Linux has attached to the device, configure
			 * it so we don't free and allocate the device
			 * structure on every command.
			 */
			dev = ahd_linux_get_device(ahd, device->channel,
						   device->id, device->lun,
						   /*alloc*/TRUE);
			if (dev != NULL) {
				dev->flags &= ~AHD_DEV_UNCONFIGURED;
				dev->scsi_device = device;
				ahd_linux_device_queue_depth(ahd, dev);
				device->queue_depth = dev->openings
						    + dev->active;
				if ((dev->flags & (AHD_DEV_Q_BASIC
						| AHD_DEV_Q_TAGGED)) == 0) {
					/*
					 * We allow the OS to queue 2 untagged
					 * transactions to us at any time even
					 * though we can only execute them
					 * serially on the controller/device.
					 * This should remove some latency.
					 */
					device->queue_depth = 2;
				}
			}
		}
	}
	ahd_unlock(ahd, &flags);
}
#endif

#if defined(__i386__)
/*
 * Return the disk geometry for the given SCSI device.
 */
static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
ahd_linux_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		    sector_t capacity, int geom[])
{
	uint8_t *bh;
#else
ahd_linux_biosparam(Disk *disk, kdev_t dev, int geom[])
{
	struct	scsi_device *sdev = disk->device;
	u_long	capacity = disk->capacity;
	struct	buffer_head *bh;
#endif
	int	 heads;
	int	 sectors;
	int	 cylinders;
	int	 ret;
	int	 extended;
	struct	 ahd_softc *ahd;

	ahd = *((struct ahd_softc **)sdev->host->hostdata);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	bh = scsi_bios_ptable(bdev);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,17)
	bh = bread(MKDEV(MAJOR(dev), MINOR(dev) & ~0xf), 0, block_size(dev));
#else
	bh = bread(MKDEV(MAJOR(dev), MINOR(dev) & ~0xf), 0, 1024);
#endif

	if (bh) {
		ret = scsi_partsize(bh, capacity,
				    &geom[2], &geom[0], &geom[1]);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		kfree(bh);
#else
		brelse(bh);
#endif
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
ahd_linux_abort(Scsi_Cmnd *cmd)
{
	struct ahd_softc *ahd;
	struct ahd_cmd *acmd;
	struct ahd_cmd *list_acmd;
	struct ahd_linux_device *dev;
	struct scb *pending_scb;
	u_long s;
	u_int  saved_scbptr;
	u_int  active_scbptr;
	u_int  last_phase;
	u_int  cdb_byte;
	int    retval;
	int    was_paused;
	int    paused;
	int    wait;
	int    disconnected;
	ahd_mode_state saved_modes;

	pending_scb = NULL;
	paused = FALSE;
	wait = FALSE;
	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;
	acmd = (struct ahd_cmd *)cmd;

	printf("%s:%d:%d:%d: Attempting to abort cmd %p:",
	       ahd_name(ahd), cmd->device->channel, cmd->device->id,
	       cmd->device->lun, cmd);
	for (cdb_byte = 0; cdb_byte < cmd->cmd_len; cdb_byte++)
		printf(" 0x%x", cmd->cmnd[cdb_byte]);
	printf("\n");

	/*
	 * In all versions of Linux, we have to work around
	 * a major flaw in how the mid-layer is locked down
	 * if we are to sleep successfully in our error handler
	 * while allowing our interrupt handler to run.  Since
	 * the midlayer acquires either the io_request_lock or
	 * our lock prior to calling us, we must use the
	 * spin_unlock_irq() method for unlocking our lock.
	 * This will force interrupts to be enabled on the
	 * current CPU.  Since the EH thread should not have
	 * been running with CPU interrupts disabled other than
	 * by acquiring either the io_request_lock or our own
	 * lock, this *should* be safe.
	 */
	ahd_midlayer_entrypoint_lock(ahd, &s);

	/*
	 * First determine if we currently own this command.
	 * Start by searching the device queue.  If not found
	 * there, check the pending_scb list.  If not found
	 * at all, and the system wanted us to just abort the
	 * command, return success.
	 */
	dev = ahd_linux_get_device(ahd, cmd->device->channel,
				   cmd->device->id, cmd->device->lun,
				   /*alloc*/FALSE);

	if (dev == NULL) {
		/*
		 * No target device for this command exists,
		 * so we must not still own the command.
		 */
		printf("%s:%d:%d:%d: Is not an active device\n",
		       ahd_name(ahd), cmd->device->channel, cmd->device->id,
		       cmd->device->lun);
		retval = SUCCESS;
		goto no_cmd;
	}

	TAILQ_FOREACH(list_acmd, &dev->busyq, acmd_links.tqe) {
		if (list_acmd == acmd)
			break;
	}

	if (list_acmd != NULL) {
		printf("%s:%d:%d:%d: Command found on device queue\n",
		       ahd_name(ahd), cmd->device->channel, cmd->device->id,
		       cmd->device->lun);
		TAILQ_REMOVE(&dev->busyq, list_acmd, acmd_links.tqe);
		cmd->result = DID_ABORT << 16;
		ahd_linux_queue_cmd_complete(ahd, cmd);
		retval = SUCCESS;
		goto done;
	}

	/*
	 * See if we can find a matching cmd in the pending list.
	 */
	LIST_FOREACH(pending_scb, &ahd->pending_scbs, pending_links) {
		if (pending_scb->io_ctx == cmd)
			break;
	}

	if (pending_scb == NULL) {
		printf("%s:%d:%d:%d: Command not found\n",
		       ahd_name(ahd), cmd->device->channel, cmd->device->id,
		       cmd->device->lun);
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
		printf("%s:%d:%d:%d: Command already completed\n",
		       ahd_name(ahd), cmd->device->channel, cmd->device->id,
		       cmd->device->lun);
		goto no_cmd;
	}

	printf("%s: At time of recovery, card was %spaused\n",
	       ahd_name(ahd), was_paused ? "" : "not ");
	ahd_dump_card_state(ahd);

	disconnected = TRUE;
	if (ahd_search_qinfifo(ahd, cmd->device->id, cmd->device->channel + 'A',
			       cmd->device->lun, SCB_GET_TAG(pending_scb),
			       ROLE_INITIATOR, CAM_REQ_ABORTED,
			       SEARCH_COMPLETE) > 0) {
		printf("%s:%d:%d:%d: Cmd aborted from QINFIFO\n",
		       ahd_name(ahd), cmd->device->channel, cmd->device->id,
				cmd->device->lun);
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
		printf("%s:%d:%d:%d: Device is active, asserting ATN\n",
		       ahd_name(ahd), cmd->device->channel,
		       cmd->device->id, cmd->device->lun);
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
		printf("%s:%d:%d:%d: Unable to deliver message\n",
		       ahd_name(ahd), cmd->device->channel,
		       cmd->device->id, cmd->device->lun);
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
		struct timer_list timer;
		int ret;

		pending_scb->platform_data->flags |= AHD_SCB_UP_EH_SEM;
		spin_unlock_irq(&ahd->platform_data->spin_lock);
		init_timer(&timer);
		timer.data = (u_long)pending_scb;
		timer.expires = jiffies + (5 * HZ);
		timer.function = ahd_linux_sem_timeout;
		add_timer(&timer);
		printf("Recovery code sleeping\n");
		down(&ahd->platform_data->eh_sem);
		printf("Recovery code awake\n");
        	ret = del_timer_sync(&timer);
		if (ret == 0) {
			printf("Timer Expired\n");
			retval = FAILED;
		}
		spin_lock_irq(&ahd->platform_data->spin_lock);
	}
	ahd_schedule_runq(ahd);
	ahd_linux_run_complete_queue(ahd);
	ahd_midlayer_entrypoint_unlock(ahd, &s);
	return (retval);
}


static void
ahd_linux_dev_reset_complete(Scsi_Cmnd *cmd)
{
	free(cmd, M_DEVBUF);
}

/*
 * Attempt to send a target reset message to the device that timed out.
 */
static int
ahd_linux_dev_reset(Scsi_Cmnd *cmd)
{
	struct	ahd_softc *ahd;
	struct	scsi_cmnd *recovery_cmd;
	struct	ahd_linux_device *dev;
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	struct	scb *scb;
	struct	hardware_scb *hscb;
	u_long	s;
	struct	timer_list timer;
	int	retval;

	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;
	recovery_cmd = malloc(sizeof(struct scsi_cmnd), M_DEVBUF, M_WAITOK);
	if (!recovery_cmd)
		return (FAILED);
	memset(recovery_cmd, 0, sizeof(struct scsi_cmnd));
	recovery_cmd->device = cmd->device;
	recovery_cmd->scsi_done = ahd_linux_dev_reset_complete;
#if AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_RECOVERY) != 0)
		printf("%s:%d:%d:%d: Device reset called for cmd %p\n",
		       ahd_name(ahd), cmd->device->channel, cmd->device->id,
		       cmd->device->lun, cmd);
#endif
	ahd_midlayer_entrypoint_lock(ahd, &s);

	dev = ahd_linux_get_device(ahd, cmd->device->channel, cmd->device->id,
				   cmd->device->lun, /*alloc*/FALSE);
	if (dev == NULL) {
		ahd_midlayer_entrypoint_unlock(ahd, &s);
		kfree(recovery_cmd);
		return (FAILED);
	}
	if ((scb = ahd_get_scb(ahd, AHD_NEVER_COL_IDX)) == NULL) {
		ahd_midlayer_entrypoint_unlock(ahd, &s);
		kfree(recovery_cmd);
		return (FAILED);
	}
	tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
				    cmd->device->id, &tstate);
	recovery_cmd->result = CAM_REQ_INPROG << 16;
	recovery_cmd->host_scribble = (char *)scb;
	scb->io_ctx = recovery_cmd;
	scb->platform_data->dev = dev;
	scb->sg_count = 0;
	ahd_set_residual(scb, 0);
	ahd_set_sense_residual(scb, 0);
	hscb = scb->hscb;
	hscb->control = 0;
	hscb->scsiid = BUILD_SCSIID(ahd, cmd);
	hscb->lun = cmd->device->lun;
	hscb->cdb_len = 0;
	hscb->task_management = SIU_TASKMGMT_LUN_RESET;
	scb->flags |= SCB_DEVICE_RESET|SCB_RECOVERY_SCB|SCB_ACTIVE;
	if ((tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {
		scb->flags |= SCB_PACKETIZED;
	} else {
		hscb->control |= MK_MESSAGE;
	}
	dev->openings--;
	dev->active++;
	dev->commands_issued++;
	LIST_INSERT_HEAD(&ahd->pending_scbs, scb, pending_links);
	ahd_queue_scb(ahd, scb);

	scb->platform_data->flags |= AHD_SCB_UP_EH_SEM;
	spin_unlock_irq(&ahd->platform_data->spin_lock);
	init_timer(&timer);
	timer.data = (u_long)scb;
	timer.expires = jiffies + (5 * HZ);
	timer.function = ahd_linux_sem_timeout;
	add_timer(&timer);
	printf("Recovery code sleeping\n");
	down(&ahd->platform_data->eh_sem);
	printf("Recovery code awake\n");
	retval = SUCCESS;
	if (del_timer_sync(&timer) == 0) {
		printf("Timer Expired\n");
		retval = FAILED;
	}
	spin_lock_irq(&ahd->platform_data->spin_lock);
	ahd_schedule_runq(ahd);
	ahd_linux_run_complete_queue(ahd);
	ahd_midlayer_entrypoint_unlock(ahd, &s);
	printf("%s: Device reset returning 0x%x\n", ahd_name(ahd), retval);
	return (retval);
}

/*
 * Reset the SCSI bus.
 */
static int
ahd_linux_bus_reset(Scsi_Cmnd *cmd)
{
	struct ahd_softc *ahd;
	u_long s;
	int    found;

	ahd = *(struct ahd_softc **)cmd->device->host->hostdata;
#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_RECOVERY) != 0)
		printf("%s: Bus reset called for cmd %p\n",
		       ahd_name(ahd), cmd);
#endif
	ahd_midlayer_entrypoint_lock(ahd, &s);
	found = ahd_reset_channel(ahd, cmd->device->channel + 'A',
				  /*initiate reset*/TRUE);
	ahd_linux_run_complete_queue(ahd);
	ahd_midlayer_entrypoint_unlock(ahd, &s);

	if (bootverbose)
		printf("%s: SCSI bus reset delivered. "
		       "%d SCBs aborted.\n", ahd_name(ahd), found);

	return (SUCCESS);
}

Scsi_Host_Template aic79xx_driver_template = {
	.module			= THIS_MODULE,
	.name			= "aic79xx",
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
	.cmd_per_lun		= 2,
	.use_clustering		= ENABLE_CLUSTERING,
	.slave_alloc		= ahd_linux_slave_alloc,
	.slave_configure	= ahd_linux_slave_configure,
	.slave_destroy		= ahd_linux_slave_destroy,
};

/**************************** Tasklet Handler *********************************/

/*
 * In 2.4.X and above, this routine is called from a tasklet,
 * so we must re-acquire our lock prior to executing this code.
 * In all prior kernels, ahd_schedule_runq() calls this routine
 * directly and ahd_schedule_runq() is called with our lock held.
 */
static void
ahd_runq_tasklet(unsigned long data)
{
	struct ahd_softc* ahd;
	struct ahd_linux_device *dev;
	u_long flags;

	ahd = (struct ahd_softc *)data;
	ahd_lock(ahd, &flags);
	while ((dev = ahd_linux_next_device_to_run(ahd)) != NULL) {
	
		TAILQ_REMOVE(&ahd->platform_data->device_runq, dev, links);
		dev->flags &= ~AHD_DEV_ON_RUN_LIST;
		ahd_linux_check_device_queue(ahd, dev);
		/* Yeild to our interrupt handler */
		ahd_unlock(ahd, &flags);
		ahd_lock(ahd, &flags);
	}
	ahd_unlock(ahd, &flags);
}

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
	bus_dmamap_t map;

	map = malloc(sizeof(*map), M_DEVBUF, M_NOWAIT);
	if (map == NULL)
		return (ENOMEM);
	/*
	 * Although we can dma data above 4GB, our
	 * "consistent" memory is below 4GB for
	 * space efficiency reasons (only need a 4byte
	 * address).  For this reason, we have to reset
	 * our dma mask when doing allocations.
	 */
	if (ahd->dev_softc != NULL)
		if (pci_set_dma_mask(ahd->dev_softc, 0xFFFFFFFF)) {
			printk(KERN_WARNING "aic79xx: No suitable DMA available.\n");
			kfree(map);
			return (ENODEV);
		}
	*vaddr = pci_alloc_consistent(ahd->dev_softc,
				      dmat->maxsize, &map->bus_addr);
	if (ahd->dev_softc != NULL)
		if (pci_set_dma_mask(ahd->dev_softc,
				     ahd->platform_data->hw_dma_mask)) {
			printk(KERN_WARNING "aic79xx: No suitable DMA available.\n");
			kfree(map);
			return (ENODEV);
		}
	if (*vaddr == NULL)
		return (ENOMEM);
	*mapp = map;
	return(0);
}

void
ahd_dmamem_free(struct ahd_softc *ahd, bus_dma_tag_t dmat,
		void* vaddr, bus_dmamap_t map)
{
	pci_free_consistent(ahd->dev_softc, dmat->maxsize,
			    vaddr, map->bus_addr);
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

	stack_sg.ds_addr = map->bus_addr;
	stack_sg.ds_len = dmat->maxsize;
	cb(cb_arg, &stack_sg, /*nseg*/1, /*error*/0);
	return (0);
}

void
ahd_dmamap_destroy(struct ahd_softc *ahd, bus_dma_tag_t dmat, bus_dmamap_t map)
{
	/*
	 * The map may is NULL in our < 2.3.X implementation.
	 */
	if (map != NULL)
		free(map, M_DEVBUF);
}

int
ahd_dmamap_unload(struct ahd_softc *ahd, bus_dma_tag_t dmat, bus_dmamap_t map)
{
	/* Nothing to do */
	return (0);
}

/********************* Platform Dependent Functions ***************************/
/*
 * Compare "left hand" softc with "right hand" softc, returning:
 * < 0 - lahd has a lower priority than rahd
 *   0 - Softcs are equal
 * > 0 - lahd has a higher priority than rahd
 */
int
ahd_softc_comp(struct ahd_softc *lahd, struct ahd_softc *rahd)
{
	int	value;

	/*
	 * Under Linux, cards are ordered as follows:
	 *	1) PCI devices that are marked as the boot controller.
	 *	2) PCI devices with BIOS enabled sorted by bus/slot/func.
	 *	3) All remaining PCI devices sorted by bus/slot/func.
	 */
#if 0
	value = (lahd->flags & AHD_BOOT_CHANNEL)
	      - (rahd->flags & AHD_BOOT_CHANNEL);
	if (value != 0)
		/* Controllers set for boot have a *higher* priority */
		return (value);
#endif

	value = (lahd->flags & AHD_BIOS_ENABLED)
	      - (rahd->flags & AHD_BIOS_ENABLED);
	if (value != 0)
		/* Controllers with BIOS enabled have a *higher* priority */
		return (value);

	/* Still equal.  Sort by bus/slot/func. */
	if (aic79xx_reverse_scan != 0)
		value = ahd_get_pci_bus(lahd->dev_softc)
		      - ahd_get_pci_bus(rahd->dev_softc);
	else
		value = ahd_get_pci_bus(rahd->dev_softc)
		      - ahd_get_pci_bus(lahd->dev_softc);
	if (value != 0)
		return (value);
	if (aic79xx_reverse_scan != 0)
		value = ahd_get_pci_slot(lahd->dev_softc)
		      - ahd_get_pci_slot(rahd->dev_softc);
	else
		value = ahd_get_pci_slot(rahd->dev_softc)
		      - ahd_get_pci_slot(lahd->dev_softc);
	if (value != 0)
		return (value);

	value = rahd->channel - lahd->channel;
	return (value);
}

static void
ahd_linux_setup_tag_info(u_long arg, int instance, int targ, int32_t value)
{

	if ((instance >= 0) && (targ >= 0)
	 && (instance < NUM_ELEMENTS(aic79xx_tag_info))
	 && (targ < AHD_NUM_TARGETS)) {
		aic79xx_tag_info[instance].tag_commands[targ] = value & 0x1FF;
		if (bootverbose)
			printf("tag_info[%d:%d] = %d\n", instance, targ, value);
	}
}

static void
ahd_linux_setup_rd_strm_info(u_long arg, int instance, int targ, int32_t value)
{
	if ((instance >= 0)
	 && (instance < NUM_ELEMENTS(aic79xx_rd_strm_info))) {
		aic79xx_rd_strm_info[instance] = value & 0xFFFF;
		if (bootverbose)
			printf("rd_strm[%d] = 0x%x\n", instance, value);
	}
}

static void
ahd_linux_setup_dv(u_long arg, int instance, int targ, int32_t value)
{
	if ((instance >= 0)
	 && (instance < NUM_ELEMENTS(aic79xx_dv_settings))) {
		aic79xx_dv_settings[instance] = value;
		if (bootverbose)
			printf("dv[%d] = %d\n", instance, value);
	}
}

static void
ahd_linux_setup_iocell_info(u_long index, int instance, int targ, int32_t value)
{

	if ((instance >= 0)
	 && (instance < NUM_ELEMENTS(aic79xx_iocell_info))) {
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

	for (i = 0; i < NUM_ELEMENTS(aic79xx_tag_info); i++) {
		for (j = 0; j < AHD_NUM_TARGETS; j++) {
			aic79xx_tag_info[i].tag_commands[j] = tags;
		}
	}
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
		{ "reverse_scan", &aic79xx_reverse_scan },
		{ "periodic_otag", &aic79xx_periodic_otag },
		{ "pci_parity", &aic79xx_pci_parity },
		{ "seltime", &aic79xx_seltime },
		{ "tag_info", NULL },
		{ "global_tag_depth", NULL},
		{ "rd_strm", NULL },
		{ "dv", NULL },
		{ "slewrate", NULL },
		{ "precomp", NULL },
		{ "amplitude", NULL },
	};

	end = strchr(s, '\0');

	/*
	 * XXX ia64 gcc isn't smart enough to know that NUM_ELEMENTS
	 * will never be 0 in this case.
	 */      
	n = 0;  

	while ((p = strsep(&s, ",.")) != NULL) {
		if (*p == '\0')
			continue;
		for (i = 0; i < NUM_ELEMENTS(options); i++) {

			n = strlen(options[i].name);
			if (strncmp(options[i].name, p, n) == 0)
				break;
		}
		if (i == NUM_ELEMENTS(options))
			continue;

		if (strncmp(p, "global_tag_depth", n) == 0) {
			ahd_linux_setup_tag_info_global(p + n);
		} else if (strncmp(p, "tag_info", n) == 0) {
			s = aic_parse_brace_option("tag_info", p + n, end,
			    2, ahd_linux_setup_tag_info, 0);
		} else if (strncmp(p, "rd_strm", n) == 0) {
			s = aic_parse_brace_option("rd_strm", p + n, end,
			    1, ahd_linux_setup_rd_strm_info, 0);
		} else if (strncmp(p, "dv", n) == 0) {
			s = aic_parse_brace_option("dv", p + n, end, 1,
			    ahd_linux_setup_dv, 0);
		} else if (strncmp(p, "slewrate", n) == 0) {
			s = aic_parse_brace_option("slewrate",
			    p + n, end, 1, ahd_linux_setup_iocell_info,
			    AIC79XX_SLEWRATE_INDEX);
		} else if (strncmp(p, "precomp", n) == 0) {
			s = aic_parse_brace_option("precomp",
			    p + n, end, 1, ahd_linux_setup_iocell_info,
			    AIC79XX_PRECOMP_INDEX);
		} else if (strncmp(p, "amplitude", n) == 0) {
			s = aic_parse_brace_option("amplitude",
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
ahd_linux_register_host(struct ahd_softc *ahd, Scsi_Host_Template *template)
{
	char	buf[80];
	struct	Scsi_Host *host;
	char	*new_name;
	u_long	s;
	u_long	target;

	template->name = ahd->description;
	host = scsi_host_alloc(template, sizeof(struct ahd_softc *));
	if (host == NULL)
		return (ENOMEM);

	*((struct ahd_softc **)host->hostdata) = ahd;
	ahd_lock(ahd, &s);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	scsi_assign_lock(host, &ahd->platform_data->spin_lock);
#elif AHD_SCSI_HAS_HOST_LOCK != 0
	host->lock = &ahd->platform_data->spin_lock;
#endif
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
	ahd_set_unit(ahd, ahd_linux_next_unit());
	sprintf(buf, "scsi%d", host->host_no);
	new_name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (new_name != NULL) {
		strcpy(new_name, buf);
		ahd_set_name(ahd, new_name);
	}
	host->unique_id = ahd->unit;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	scsi_set_pci_device(host, ahd->dev_softc);
#endif
	ahd_linux_setup_user_rd_strm_settings(ahd);
	ahd_linux_initialize_scsi_bus(ahd);
	ahd_unlock(ahd, &s);
	ahd->platform_data->dv_pid = kernel_thread(ahd_linux_dv_thread, ahd, 0);
	ahd_lock(ahd, &s);
	if (ahd->platform_data->dv_pid < 0) {
		printf("%s: Failed to create DV thread, error= %d\n",
		       ahd_name(ahd), ahd->platform_data->dv_pid);
		return (-ahd->platform_data->dv_pid);
	}
	/*
	 * Initially allocate *all* of our linux target objects
	 * so that the DV thread will scan them all in parallel
	 * just after driver initialization.  Any device that
	 * does not exist will have its target object destroyed
	 * by the selection timeout handler.  In the case of a
	 * device that appears after the initial DV scan, async
	 * negotiation will occur for the first command, and DV
	 * will comence should that first command be successful.
	 */
	for (target = 0; target < host->max_id; target++) {

		/*
		 * Skip our own ID.  Some Compaq/HP storage devices
		 * have enclosure management devices that respond to
		 * single bit selection (i.e. selecting ourselves).
		 * It is expected that either an external application
		 * or a modified kernel will be used to probe this
		 * ID if it is appropriate.  To accommodate these
		 * installations, ahc_linux_alloc_target() will allocate
		 * for our ID if asked to do so.
		 */
		if (target == ahd->our_id) 
			continue;

		ahd_linux_alloc_target(ahd, 0, target);
	}
	ahd_intr_enable(ahd, TRUE);
	ahd_linux_start_dv(ahd);
	ahd_unlock(ahd, &s);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	scsi_add_host(host, &ahd->dev_softc->dev); /* XXX handle failure */
	scsi_scan_host(host);
#endif
	return (0);
}

uint64_t
ahd_linux_get_memsize(void)
{
	struct sysinfo si;

	si_meminfo(&si);
	return ((uint64_t)si.totalram << PAGE_SHIFT);
}

/*
 * Find the smallest available unit number to use
 * for a new device.  We don't just use a static
 * count to handle the "repeated hot-(un)plug"
 * scenario.
 */
static int
ahd_linux_next_unit(void)
{
	struct ahd_softc *ahd;
	int unit;

	unit = 0;
retry:
	TAILQ_FOREACH(ahd, &ahd_tailq, links) {
		if (ahd->unit == unit) {
			unit++;
			goto retry;
		}
	}
	return (unit);
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

	target_id = 0;
	numtarg = 0;

	if (aic79xx_no_reset != 0)
		ahd->flags &= ~AHD_RESET_BUS_A;

	if ((ahd->flags & AHD_RESET_BUS_A) != 0)
		ahd_reset_channel(ahd, 'A', /*initiate_reset*/TRUE);
	else
		numtarg = (ahd->features & AHD_WIDE) ? 16 : 8;

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
	/* Give the bus some time to recover */
	if ((ahd->flags & AHD_RESET_BUS_A) != 0) {
		ahd_freeze_simq(ahd);
		init_timer(&ahd->platform_data->reset_timer);
		ahd->platform_data->reset_timer.data = (u_long)ahd;
		ahd->platform_data->reset_timer.expires =
		    jiffies + (AIC79XX_RESET_DELAY * HZ)/1000;
		ahd->platform_data->reset_timer.function =
		    (ahd_linux_callback_t *)ahd_release_simq;
		add_timer(&ahd->platform_data->reset_timer);
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
	TAILQ_INIT(&ahd->platform_data->completeq);
	TAILQ_INIT(&ahd->platform_data->device_runq);
	ahd->platform_data->irq = AHD_LINUX_NOIRQ;
	ahd->platform_data->hw_dma_mask = 0xFFFFFFFF;
	ahd_lockinit(ahd);
	ahd_done_lockinit(ahd);
	init_timer(&ahd->platform_data->completeq_timer);
	ahd->platform_data->completeq_timer.data = (u_long)ahd;
	ahd->platform_data->completeq_timer.function =
	    (ahd_linux_callback_t *)ahd_linux_thread_run_complete_queue;
	init_MUTEX_LOCKED(&ahd->platform_data->eh_sem);
	init_MUTEX_LOCKED(&ahd->platform_data->dv_sem);
	init_MUTEX_LOCKED(&ahd->platform_data->dv_cmd_sem);
	ahd_setup_runq_tasklet(ahd);
	ahd->seltime = (aic79xx_seltime & 0x3) << 4;
	return (0);
}

void
ahd_platform_free(struct ahd_softc *ahd)
{
	struct ahd_linux_target *targ;
	struct ahd_linux_device *dev;
	int i, j;

	if (ahd->platform_data != NULL) {
		del_timer_sync(&ahd->platform_data->completeq_timer);
		ahd_linux_kill_dv_thread(ahd);
		ahd_teardown_runq_tasklet(ahd);
		if (ahd->platform_data->host != NULL) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
			scsi_remove_host(ahd->platform_data->host);
#endif
			scsi_host_put(ahd->platform_data->host);
		}

		/* destroy all of the device and target objects */
		for (i = 0; i < AHD_NUM_TARGETS; i++) {
			targ = ahd->platform_data->targets[i];
			if (targ != NULL) {
				/* Keep target around through the loop. */
				targ->refcount++;
				for (j = 0; j < AHD_NUM_LUNS; j++) {

					if (targ->devices[j] == NULL)
						continue;
					dev = targ->devices[j];
					ahd_linux_free_device(ahd, dev);
				}
				/*
				 * Forcibly free the target now that
				 * all devices are gone.
				 */
				ahd_linux_free_target(ahd, targ);
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    		/*
		 * In 2.4 we detach from the scsi midlayer before the PCI
		 * layer invokes our remove callback.  No per-instance
		 * detach is provided, so we must reach inside the PCI
		 * subsystem's internals and detach our driver manually.
		 */
		if (ahd->dev_softc != NULL)
			ahd->dev_softc->driver = NULL;
#endif
		free(ahd->platform_data, M_DEVBUF);
	}
}

void
ahd_platform_init(struct ahd_softc *ahd)
{
	/*
	 * Lookup and commit any modified IO Cell options.
	 */
	if (ahd->unit < NUM_ELEMENTS(aic79xx_iocell_info)) {
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
ahd_platform_set_tags(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		      ahd_queue_alg alg)
{
	struct ahd_linux_device *dev;
	int was_queuing;
	int now_queuing;

	dev = ahd_linux_get_device(ahd, devinfo->channel - 'A',
				   devinfo->target,
				   devinfo->lun, /*alloc*/FALSE);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	if (dev->scsi_device != NULL) {
		switch ((dev->flags & (AHD_DEV_Q_BASIC|AHD_DEV_Q_TAGGED))) {
		case AHD_DEV_Q_BASIC:
			scsi_adjust_queue_depth(dev->scsi_device,
						MSG_SIMPLE_TASK,
						dev->openings + dev->active);
			break;
		case AHD_DEV_Q_TAGGED:
			scsi_adjust_queue_depth(dev->scsi_device,
						MSG_ORDERED_TASK,
						dev->openings + dev->active);
			break;
		default:
			/*
			 * We allow the OS to queue 2 untagged transactions to
			 * us at any time even though we can only execute them
			 * serially on the controller/device.  This should
			 * remove some latency.
			 */
			scsi_adjust_queue_depth(dev->scsi_device,
						/*NON-TAGGED*/0,
						/*queue depth*/2);
			break;
		}
	}
#endif
}

int
ahd_platform_abort_scbs(struct ahd_softc *ahd, int target, char channel,
			int lun, u_int tag, role_t role, uint32_t status)
{
	int targ;
	int maxtarg;
	int maxlun;
	int clun;
	int count;

	if (tag != SCB_LIST_NULL)
		return (0);

	targ = 0;
	if (target != CAM_TARGET_WILDCARD) {
		targ = target;
		maxtarg = targ + 1;
	} else {
		maxtarg = (ahd->features & AHD_WIDE) ? 16 : 8;
	}
	clun = 0;
	if (lun != CAM_LUN_WILDCARD) {
		clun = lun;
		maxlun = clun + 1;
	} else {
		maxlun = AHD_NUM_LUNS;
	}

	count = 0;
	for (; targ < maxtarg; targ++) {

		for (; clun < maxlun; clun++) {
			struct ahd_linux_device *dev;
			struct ahd_busyq *busyq;
			struct ahd_cmd *acmd;

			dev = ahd_linux_get_device(ahd, /*chan*/0, targ,
						   clun, /*alloc*/FALSE);
			if (dev == NULL)
				continue;

			busyq = &dev->busyq;
			while ((acmd = TAILQ_FIRST(busyq)) != NULL) {
				Scsi_Cmnd *cmd;

				cmd = &acmd_scsi_cmd(acmd);
				TAILQ_REMOVE(busyq, acmd,
					     acmd_links.tqe);
				count++;
				cmd->result = status << 16;
				ahd_linux_queue_cmd_complete(ahd, cmd);
			}
		}
	}

	return (count);
}

static void
ahd_linux_thread_run_complete_queue(struct ahd_softc *ahd)
{
	u_long flags;

	ahd_lock(ahd, &flags);
	del_timer(&ahd->platform_data->completeq_timer);
	ahd->platform_data->flags &= ~AHD_RUN_CMPLT_Q_TIMER;
	ahd_linux_run_complete_queue(ahd);
	ahd_unlock(ahd, &flags);
}

static void
ahd_linux_start_dv(struct ahd_softc *ahd)
{

	/*
	 * Freeze the simq and signal ahd_linux_queue to not let any
	 * more commands through
	 */
	if ((ahd->platform_data->flags & AHD_DV_ACTIVE) == 0) {
#ifdef AHD_DEBUG
		if (ahd_debug & AHD_SHOW_DV)
			printf("%s: Starting DV\n", ahd_name(ahd));
#endif

		ahd->platform_data->flags |= AHD_DV_ACTIVE;
		ahd_freeze_simq(ahd);

		/* Wake up the DV kthread */
		up(&ahd->platform_data->dv_sem);
	}
}

static int
ahd_linux_dv_thread(void *data)
{
	struct	ahd_softc *ahd;
	int	target;
	u_long	s;

	ahd = (struct ahd_softc *)data;

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV)
		printf("In DV Thread\n");
#endif

	/*
	 * Complete thread creation.
	 */
	lock_kernel();
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,60)
	/*
	 * Don't care about any signals.
	 */
	siginitsetinv(&current->blocked, 0);

	daemonize();
	sprintf(current->comm, "ahd_dv_%d", ahd->unit);
#else
	daemonize("ahd_dv_%d", ahd->unit);
	current->flags |= PF_FREEZE;
#endif
	unlock_kernel();

	while (1) {
		/*
		 * Use down_interruptible() rather than down() to
		 * avoid inclusion in the load average.
		 */
		down_interruptible(&ahd->platform_data->dv_sem);

		/* Check to see if we've been signaled to exit */
		ahd_lock(ahd, &s);
		if ((ahd->platform_data->flags & AHD_DV_SHUTDOWN) != 0) {
			ahd_unlock(ahd, &s);
			break;
		}
		ahd_unlock(ahd, &s);

#ifdef AHD_DEBUG
		if (ahd_debug & AHD_SHOW_DV)
			printf("%s: Beginning Domain Validation\n",
			       ahd_name(ahd));
#endif

		/*
		 * Wait for any pending commands to drain before proceeding.
		 */
		ahd_lock(ahd, &s);
		while (LIST_FIRST(&ahd->pending_scbs) != NULL) {
			ahd->platform_data->flags |= AHD_DV_WAIT_SIMQ_EMPTY;
			ahd_unlock(ahd, &s);
			down_interruptible(&ahd->platform_data->dv_sem);
			ahd_lock(ahd, &s);
		}

		/*
		 * Wait for the SIMQ to be released so that DV is the
		 * only reason the queue is frozen.
		 */
		while (AHD_DV_SIMQ_FROZEN(ahd) == 0) {
			ahd->platform_data->flags |= AHD_DV_WAIT_SIMQ_RELEASE;
			ahd_unlock(ahd, &s);
			down_interruptible(&ahd->platform_data->dv_sem);
			ahd_lock(ahd, &s);
		}
		ahd_unlock(ahd, &s);

		for (target = 0; target < AHD_NUM_TARGETS; target++)
			ahd_linux_dv_target(ahd, target);

		ahd_lock(ahd, &s);
		ahd->platform_data->flags &= ~AHD_DV_ACTIVE;
		ahd_unlock(ahd, &s);

		/*
		 * Release the SIMQ so that normal commands are
		 * allowed to continue on the bus.
		 */
		ahd_release_simq(ahd);
	}
	up(&ahd->platform_data->eh_sem);
	return (0);
}

static void
ahd_linux_kill_dv_thread(struct ahd_softc *ahd)
{
	u_long s;

	ahd_lock(ahd, &s);
	if (ahd->platform_data->dv_pid != 0) {
		ahd->platform_data->flags |= AHD_DV_SHUTDOWN;
		ahd_unlock(ahd, &s);
		up(&ahd->platform_data->dv_sem);

		/*
		 * Use the eh_sem as an indicator that the
		 * dv thread is exiting.  Note that the dv
		 * thread must still return after performing
		 * the up on our semaphore before it has
		 * completely exited this module.  Unfortunately,
		 * there seems to be no easy way to wait for the
		 * exit of a thread for which you are not the
		 * parent (dv threads are parented by init).
		 * Cross your fingers...
		 */
		down(&ahd->platform_data->eh_sem);

		/*
		 * Mark the dv thread as already dead.  This
		 * avoids attempting to kill it a second time.
		 * This is necessary because we must kill the
		 * DV thread before calling ahd_free() in the
		 * module shutdown case to avoid bogus locking
		 * in the SCSI mid-layer, but we ahd_free() is
		 * called without killing the DV thread in the
		 * instance detach case, so ahd_platform_free()
		 * calls us again to verify that the DV thread
		 * is dead.
		 */
		ahd->platform_data->dv_pid = 0;
	} else {
		ahd_unlock(ahd, &s);
	}
}

#define AHD_LINUX_DV_INQ_SHORT_LEN	36
#define AHD_LINUX_DV_INQ_LEN		256
#define AHD_LINUX_DV_TIMEOUT		(HZ / 4)

#define AHD_SET_DV_STATE(ahd, targ, newstate) \
	ahd_set_dv_state(ahd, targ, newstate, __LINE__)

static __inline void
ahd_set_dv_state(struct ahd_softc *ahd, struct ahd_linux_target *targ,
		 ahd_dv_state newstate, u_int line)
{
	ahd_dv_state oldstate;

	oldstate = targ->dv_state;
#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV)
		printf("%s:%d: Going from state %d to state %d\n",
		       ahd_name(ahd), line, oldstate, newstate);
#endif

	if (oldstate == newstate)
		targ->dv_state_retry++;
	else
		targ->dv_state_retry = 0;
	targ->dv_state = newstate;
}

static void
ahd_linux_dv_target(struct ahd_softc *ahd, u_int target_offset)
{
	struct	 ahd_devinfo devinfo;
	struct	 ahd_linux_target *targ;
	struct	 scsi_cmnd *cmd;
	struct	 scsi_device *scsi_dev;
	struct	 scsi_sense_data *sense;
	uint8_t *buffer;
	u_long	 s;
	u_int	 timeout;
	int	 echo_size;

	sense = NULL;
	buffer = NULL;
	echo_size = 0;
	ahd_lock(ahd, &s);
	targ = ahd->platform_data->targets[target_offset];
	if (targ == NULL || (targ->flags & AHD_DV_REQUIRED) == 0) {
		ahd_unlock(ahd, &s);
		return;
	}
	ahd_compile_devinfo(&devinfo, ahd->our_id, targ->target, /*lun*/0,
			    targ->channel + 'A', ROLE_INITIATOR);
#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, &devinfo);
		printf("Performing DV\n");
	}
#endif

	ahd_unlock(ahd, &s);

	cmd = malloc(sizeof(struct scsi_cmnd), M_DEVBUF, M_WAITOK);
	scsi_dev = malloc(sizeof(struct scsi_device), M_DEVBUF, M_WAITOK);
	scsi_dev->host = ahd->platform_data->host;
	scsi_dev->id = devinfo.target;
	scsi_dev->lun = devinfo.lun;
	scsi_dev->channel = devinfo.channel - 'A';
	ahd->platform_data->dv_scsi_dev = scsi_dev;

	AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_INQ_SHORT_ASYNC);

	while (targ->dv_state != AHD_DV_STATE_EXIT) {
		timeout = AHD_LINUX_DV_TIMEOUT;
		switch (targ->dv_state) {
		case AHD_DV_STATE_INQ_SHORT_ASYNC:
		case AHD_DV_STATE_INQ_ASYNC:
		case AHD_DV_STATE_INQ_ASYNC_VERIFY:
			/*
			 * Set things to async narrow to reduce the
			 * chance that the INQ will fail.
			 */
			ahd_lock(ahd, &s);
			ahd_set_syncrate(ahd, &devinfo, 0, 0, 0,
					 AHD_TRANS_GOAL, /*paused*/FALSE);
			ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
				      AHD_TRANS_GOAL, /*paused*/FALSE);
			ahd_unlock(ahd, &s);
			timeout = 10 * HZ;
			targ->flags &= ~AHD_INQ_VALID;
			/* FALLTHROUGH */
		case AHD_DV_STATE_INQ_VERIFY:
		{
			u_int inq_len;

			if (targ->dv_state == AHD_DV_STATE_INQ_SHORT_ASYNC)
				inq_len = AHD_LINUX_DV_INQ_SHORT_LEN;
			else
				inq_len = targ->inq_data->additional_length + 5;
			ahd_linux_dv_inq(ahd, cmd, &devinfo, targ, inq_len);
			break;
		}
		case AHD_DV_STATE_TUR:
		case AHD_DV_STATE_BUSY:
			timeout = 5 * HZ;
			ahd_linux_dv_tur(ahd, cmd, &devinfo);
			break;
		case AHD_DV_STATE_REBD:
			ahd_linux_dv_rebd(ahd, cmd, &devinfo, targ);
			break;
		case AHD_DV_STATE_WEB:
			ahd_linux_dv_web(ahd, cmd, &devinfo, targ);
			break;

		case AHD_DV_STATE_REB:
			ahd_linux_dv_reb(ahd, cmd, &devinfo, targ);
			break;

		case AHD_DV_STATE_SU:
			ahd_linux_dv_su(ahd, cmd, &devinfo, targ);
			timeout = 50 * HZ;
			break;

		default:
			ahd_print_devinfo(ahd, &devinfo);
			printf("Unknown DV state %d\n", targ->dv_state);
			goto out;
		}

		/* Queue the command and wait for it to complete */
		/* Abuse eh_timeout in the scsi_cmnd struct for our purposes */
		init_timer(&cmd->eh_timeout);
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
			/*
			 * All of the printfs during negotiation
			 * really slow down the negotiation.
			 * Add a bit of time just to be safe.
			 */
			timeout += HZ;
#endif
		scsi_add_timer(cmd, timeout, ahd_linux_dv_timeout);
		/*
		 * In 2.5.X, it is assumed that all calls from the
		 * "midlayer" (which we are emulating) will have the
		 * ahd host lock held.  For other kernels, the
		 * io_request_lock must be held.
		 */
#if AHD_SCSI_HAS_HOST_LOCK != 0
		ahd_lock(ahd, &s);
#else
		spin_lock_irqsave(&io_request_lock, s);
#endif
		ahd_linux_queue(cmd, ahd_linux_dv_complete);
#if AHD_SCSI_HAS_HOST_LOCK != 0
		ahd_unlock(ahd, &s);
#else
		spin_unlock_irqrestore(&io_request_lock, s);
#endif
		down_interruptible(&ahd->platform_data->dv_cmd_sem);
		/*
		 * Wait for the SIMQ to be released so that DV is the
		 * only reason the queue is frozen.
		 */
		ahd_lock(ahd, &s);
		while (AHD_DV_SIMQ_FROZEN(ahd) == 0) {
			ahd->platform_data->flags |= AHD_DV_WAIT_SIMQ_RELEASE;
			ahd_unlock(ahd, &s);
			down_interruptible(&ahd->platform_data->dv_sem);
			ahd_lock(ahd, &s);
		}
		ahd_unlock(ahd, &s);

		ahd_linux_dv_transition(ahd, cmd, &devinfo, targ);
	}

out:
	if ((targ->flags & AHD_INQ_VALID) != 0
	 && ahd_linux_get_device(ahd, devinfo.channel - 'A',
				 devinfo.target, devinfo.lun,
				 /*alloc*/FALSE) == NULL) {
		/*
		 * The DV state machine failed to configure this device.  
		 * This is normal if DV is disabled.  Since we have inquiry
		 * data, filter it and use the "optimistic" negotiation
		 * parameters found in the inquiry string.
		 */
		ahd_linux_filter_inquiry(ahd, &devinfo);
		if ((targ->flags & (AHD_BASIC_DV|AHD_ENHANCED_DV)) != 0) {
			ahd_print_devinfo(ahd, &devinfo);
			printf("DV failed to configure device.  "
			       "Please file a bug report against "
			       "this driver.\n");
		}
	}

	if (cmd != NULL)
		free(cmd, M_DEVBUF);

	if (ahd->platform_data->dv_scsi_dev != NULL) {
		free(ahd->platform_data->dv_scsi_dev, M_DEVBUF);
		ahd->platform_data->dv_scsi_dev = NULL;
	}

	ahd_lock(ahd, &s);
	if (targ->dv_buffer != NULL) {
		free(targ->dv_buffer, M_DEVBUF);
		targ->dv_buffer = NULL;
	}
	if (targ->dv_buffer1 != NULL) {
		free(targ->dv_buffer1, M_DEVBUF);
		targ->dv_buffer1 = NULL;
	}
	targ->flags &= ~AHD_DV_REQUIRED;
	if (targ->refcount == 0)
		ahd_linux_free_target(ahd, targ);
	ahd_unlock(ahd, &s);
}

static __inline int
ahd_linux_dv_fallback(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	u_long s;
	int retval;

	ahd_lock(ahd, &s);
	retval = ahd_linux_fallback(ahd, devinfo);
	ahd_unlock(ahd, &s);

	return (retval);
}

static void
ahd_linux_dv_transition(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
			struct ahd_devinfo *devinfo,
			struct ahd_linux_target *targ)
{
	u_int32_t status;

	status = aic_error_action(cmd, targ->inq_data,
				  ahd_cmd_get_transaction_status(cmd),
				  ahd_cmd_get_scsi_status(cmd));

	
#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Entering ahd_linux_dv_transition, state= %d, "
		       "status= 0x%x, cmd->result= 0x%x\n", targ->dv_state,
		       status, cmd->result);
	}
#endif

	switch (targ->dv_state) {
	case AHD_DV_STATE_INQ_SHORT_ASYNC:
	case AHD_DV_STATE_INQ_ASYNC:
		switch (status & SS_MASK) {
		case SS_NOP:
		{
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state+1);
			break;
		}
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		case SS_TUR:
		case SS_RETRY:
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ)
				targ->dv_state_retry--;
			if ((status & SS_ERRMASK) == EBUSY)
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_BUSY);
			if (targ->dv_state_retry < 10)
				break;
			/* FALLTHROUGH */
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				ahd_print_devinfo(ahd, devinfo);
				printf("Failed DV inquiry, skipping\n");
			}
#endif
			break;
		}
		break;
	case AHD_DV_STATE_INQ_ASYNC_VERIFY:
		switch (status & SS_MASK) {
		case SS_NOP:
		{
			u_int xportflags;
			u_int spi3data;

			if (memcmp(targ->inq_data, targ->dv_buffer,
				   AHD_LINUX_DV_INQ_LEN) != 0) {
				/*
				 * Inquiry data must have changed.
				 * Try from the top again.
				 */
				AHD_SET_DV_STATE(ahd, targ,
						 AHD_DV_STATE_INQ_SHORT_ASYNC);
				break;
			}

			AHD_SET_DV_STATE(ahd, targ, targ->dv_state+1);
			targ->flags |= AHD_INQ_VALID;
			if (ahd_linux_user_dv_setting(ahd) == 0)
				break;

			xportflags = targ->inq_data->flags;
			if ((xportflags & (SID_Sync|SID_WBus16)) == 0)
				break;

			spi3data = targ->inq_data->spi3data;
			switch (spi3data & SID_SPI_CLOCK_DT_ST) {
			default:
			case SID_SPI_CLOCK_ST:
				/* Assume only basic DV is supported. */
				targ->flags |= AHD_BASIC_DV;
				break;
			case SID_SPI_CLOCK_DT:
			case SID_SPI_CLOCK_DT_ST:
				targ->flags |= AHD_ENHANCED_DV;
				break;
			}
			break;
		}
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		case SS_TUR:
		case SS_RETRY:
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ)
				targ->dv_state_retry--;

			if ((status & SS_ERRMASK) == EBUSY)
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_BUSY);
			if (targ->dv_state_retry < 10)
				break;
			/* FALLTHROUGH */
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				ahd_print_devinfo(ahd, devinfo);
				printf("Failed DV inquiry, skipping\n");
			}
#endif
			break;
		}
		break;
	case AHD_DV_STATE_INQ_VERIFY:
		switch (status & SS_MASK) {
		case SS_NOP:
		{

			if (memcmp(targ->inq_data, targ->dv_buffer,
				   AHD_LINUX_DV_INQ_LEN) == 0) {
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
				break;
			}

#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				int i;

				ahd_print_devinfo(ahd, devinfo);
				printf("Inquiry buffer mismatch:");
				for (i = 0; i < AHD_LINUX_DV_INQ_LEN; i++) {
					if ((i & 0xF) == 0)
						printf("\n        ");
					printf("0x%x:0x0%x ",
					       ((uint8_t *)targ->inq_data)[i], 
					       targ->dv_buffer[i]);
				}
				printf("\n");
			}
#endif

			if (ahd_linux_dv_fallback(ahd, devinfo) != 0) {
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
				break;
			}
			/*
			 * Do not count "falling back"
			 * against our retries.
			 */
			targ->dv_state_retry = 0;
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			break;
		}
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		case SS_TUR:
		case SS_RETRY:
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ) {
				targ->dv_state_retry--;
			} else if ((status & SSQ_FALLBACK) != 0) {
				if (ahd_linux_dv_fallback(ahd, devinfo) != 0) {
					AHD_SET_DV_STATE(ahd, targ,
							 AHD_DV_STATE_EXIT);
					break;
				}
				/*
				 * Do not count "falling back"
				 * against our retries.
				 */
				targ->dv_state_retry = 0;
			} else if ((status & SS_ERRMASK) == EBUSY)
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_BUSY);
			if (targ->dv_state_retry < 10)
				break;
			/* FALLTHROUGH */
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				ahd_print_devinfo(ahd, devinfo);
				printf("Failed DV inquiry, skipping\n");
			}
#endif
			break;
		}
		break;

	case AHD_DV_STATE_TUR:
		switch (status & SS_MASK) {
		case SS_NOP:
			if ((targ->flags & AHD_BASIC_DV) != 0) {
				ahd_linux_filter_inquiry(ahd, devinfo);
				AHD_SET_DV_STATE(ahd, targ,
						 AHD_DV_STATE_INQ_VERIFY);
			} else if ((targ->flags & AHD_ENHANCED_DV) != 0) {
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_REBD);
			} else {
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			}
			break;
		case SS_RETRY:
		case SS_TUR:
			if ((status & SS_ERRMASK) == EBUSY) {
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_BUSY);
				break;
			}
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ) {
				targ->dv_state_retry--;
			} else if ((status & SSQ_FALLBACK) != 0) {
				if (ahd_linux_dv_fallback(ahd, devinfo) != 0) {
					AHD_SET_DV_STATE(ahd, targ,
							 AHD_DV_STATE_EXIT);
					break;
				}
				/*
				 * Do not count "falling back"
				 * against our retries.
				 */
				targ->dv_state_retry = 0;
			}
			if (targ->dv_state_retry >= 10) {
#ifdef AHD_DEBUG
				if (ahd_debug & AHD_SHOW_DV) {
					ahd_print_devinfo(ahd, devinfo);
					printf("DV TUR reties exhausted\n");
				}
#endif
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
				break;
			}
			if (status & SSQ_DELAY)
				ssleep(1);

			break;
		case SS_START:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_SU);
			break;
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			break;
		}
		break;

	case AHD_DV_STATE_REBD:
		switch (status & SS_MASK) {
		case SS_NOP:
		{
			uint32_t echo_size;

			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_WEB);
			echo_size = scsi_3btoul(&targ->dv_buffer[1]);
			echo_size &= 0x1FFF;
#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				ahd_print_devinfo(ahd, devinfo);
				printf("Echo buffer size= %d\n", echo_size);
			}
#endif
			if (echo_size == 0) {
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
				break;
			}

			/* Generate the buffer pattern */
			targ->dv_echo_size = echo_size;
			ahd_linux_generate_dv_pattern(targ);
			/*
			 * Setup initial negotiation values.
			 */
			ahd_linux_filter_inquiry(ahd, devinfo);
			break;
		}
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		case SS_RETRY:
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ)
				targ->dv_state_retry--;
			if (targ->dv_state_retry <= 10)
				break;
#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				ahd_print_devinfo(ahd, devinfo);
				printf("DV REBD reties exhausted\n");
			}
#endif
			/* FALLTHROUGH */
		case SS_FATAL:
		default:
			/*
			 * Setup initial negotiation values
			 * and try level 1 DV.
			 */
			ahd_linux_filter_inquiry(ahd, devinfo);
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_INQ_VERIFY);
			targ->dv_echo_size = 0;
			break;
		}
		break;

	case AHD_DV_STATE_WEB:
		switch (status & SS_MASK) {
		case SS_NOP:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_REB);
			break;
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		case SS_RETRY:
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ) {
				targ->dv_state_retry--;
			} else if ((status & SSQ_FALLBACK) != 0) {
				if (ahd_linux_dv_fallback(ahd, devinfo) != 0) {
					AHD_SET_DV_STATE(ahd, targ,
							 AHD_DV_STATE_EXIT);
					break;
				}
				/*
				 * Do not count "falling back"
				 * against our retries.
				 */
				targ->dv_state_retry = 0;
			}
			if (targ->dv_state_retry <= 10)
				break;
			/* FALLTHROUGH */
#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				ahd_print_devinfo(ahd, devinfo);
				printf("DV WEB reties exhausted\n");
			}
#endif
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			break;
		}
		break;

	case AHD_DV_STATE_REB:
		switch (status & SS_MASK) {
		case SS_NOP:
			if (memcmp(targ->dv_buffer, targ->dv_buffer1,
				   targ->dv_echo_size) != 0) {
				if (ahd_linux_dv_fallback(ahd, devinfo) != 0)
					AHD_SET_DV_STATE(ahd, targ,
							 AHD_DV_STATE_EXIT);
				else
					AHD_SET_DV_STATE(ahd, targ,
							 AHD_DV_STATE_WEB);
				break;
			}
			
			if (targ->dv_buffer != NULL) {
				free(targ->dv_buffer, M_DEVBUF);
				targ->dv_buffer = NULL;
			}
			if (targ->dv_buffer1 != NULL) {
				free(targ->dv_buffer1, M_DEVBUF);
				targ->dv_buffer1 = NULL;
			}
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			break;
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		case SS_RETRY:
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ) {
				targ->dv_state_retry--;
			} else if ((status & SSQ_FALLBACK) != 0) {
				if (ahd_linux_dv_fallback(ahd, devinfo) != 0) {
					AHD_SET_DV_STATE(ahd, targ,
							 AHD_DV_STATE_EXIT);
					break;
				}
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_WEB);
			}
			if (targ->dv_state_retry <= 10) {
				if ((status & (SSQ_DELAY_RANDOM|SSQ_DELAY))!= 0)
					msleep(ahd->our_id*1000/10);
				break;
			}
#ifdef AHD_DEBUG
			if (ahd_debug & AHD_SHOW_DV) {
				ahd_print_devinfo(ahd, devinfo);
				printf("DV REB reties exhausted\n");
			}
#endif
			/* FALLTHROUGH */
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			break;
		}
		break;

	case AHD_DV_STATE_SU:
		switch (status & SS_MASK) {
		case SS_NOP:
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			break;
		}
		break;

	case AHD_DV_STATE_BUSY:
		switch (status & SS_MASK) {
		case SS_NOP:
		case SS_INQ_REFRESH:
			AHD_SET_DV_STATE(ahd, targ,
					 AHD_DV_STATE_INQ_SHORT_ASYNC);
			break;
		case SS_TUR:
		case SS_RETRY:
			AHD_SET_DV_STATE(ahd, targ, targ->dv_state);
			if (ahd_cmd_get_transaction_status(cmd)
			 == CAM_REQUEUE_REQ) {
				targ->dv_state_retry--;
			} else if (targ->dv_state_retry < 60) {
				if ((status & SSQ_DELAY) != 0)
					ssleep(1);
			} else {
#ifdef AHD_DEBUG
				if (ahd_debug & AHD_SHOW_DV) {
					ahd_print_devinfo(ahd, devinfo);
					printf("DV BUSY reties exhausted\n");
				}
#endif
				AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			}
			break;
		default:
			AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
			break;
		}
		break;

	default:
		printf("%s: Invalid DV completion state %d\n", ahd_name(ahd),
		       targ->dv_state);
		AHD_SET_DV_STATE(ahd, targ, AHD_DV_STATE_EXIT);
		break;
	}
}

static void
ahd_linux_dv_fill_cmd(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
		      struct ahd_devinfo *devinfo)
{
	memset(cmd, 0, sizeof(struct scsi_cmnd));
	cmd->device = ahd->platform_data->dv_scsi_dev;
	cmd->scsi_done = ahd_linux_dv_complete;
}

/*
 * Synthesize an inquiry command.  On the return trip, it'll be
 * sniffed and the device transfer settings set for us.
 */
static void
ahd_linux_dv_inq(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
		 struct ahd_devinfo *devinfo, struct ahd_linux_target *targ,
		 u_int request_length)
{

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Sending INQ\n");
	}
#endif
	if (targ->inq_data == NULL)
		targ->inq_data = malloc(AHD_LINUX_DV_INQ_LEN,
					M_DEVBUF, M_WAITOK);
	if (targ->dv_state > AHD_DV_STATE_INQ_ASYNC) {
		if (targ->dv_buffer != NULL)
			free(targ->dv_buffer, M_DEVBUF);
		targ->dv_buffer = malloc(AHD_LINUX_DV_INQ_LEN,
					 M_DEVBUF, M_WAITOK);
	}

	ahd_linux_dv_fill_cmd(ahd, cmd, devinfo);
	cmd->sc_data_direction = SCSI_DATA_READ;
	cmd->cmd_len = 6;
	cmd->cmnd[0] = INQUIRY;
	cmd->cmnd[4] = request_length;
	cmd->request_bufflen = request_length;
	if (targ->dv_state > AHD_DV_STATE_INQ_ASYNC)
		cmd->request_buffer = targ->dv_buffer;
	else
		cmd->request_buffer = targ->inq_data;
	memset(cmd->request_buffer, 0, AHD_LINUX_DV_INQ_LEN);
}

static void
ahd_linux_dv_tur(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
		 struct ahd_devinfo *devinfo)
{

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Sending TUR\n");
	}
#endif
	/* Do a TUR to clear out any non-fatal transitional state */
	ahd_linux_dv_fill_cmd(ahd, cmd, devinfo);
	cmd->sc_data_direction = SCSI_DATA_NONE;
	cmd->cmd_len = 6;
	cmd->cmnd[0] = TEST_UNIT_READY;
}

#define AHD_REBD_LEN 4

static void
ahd_linux_dv_rebd(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
		 struct ahd_devinfo *devinfo, struct ahd_linux_target *targ)
{

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Sending REBD\n");
	}
#endif
	if (targ->dv_buffer != NULL)
		free(targ->dv_buffer, M_DEVBUF);
	targ->dv_buffer = malloc(AHD_REBD_LEN, M_DEVBUF, M_WAITOK);
	ahd_linux_dv_fill_cmd(ahd, cmd, devinfo);
	cmd->sc_data_direction = SCSI_DATA_READ;
	cmd->cmd_len = 10;
	cmd->cmnd[0] = READ_BUFFER;
	cmd->cmnd[1] = 0x0b;
	scsi_ulto3b(AHD_REBD_LEN, &cmd->cmnd[6]);
	cmd->request_bufflen = AHD_REBD_LEN;
	cmd->underflow = cmd->request_bufflen;
	cmd->request_buffer = targ->dv_buffer;
}

static void
ahd_linux_dv_web(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
		 struct ahd_devinfo *devinfo, struct ahd_linux_target *targ)
{

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Sending WEB\n");
	}
#endif
	ahd_linux_dv_fill_cmd(ahd, cmd, devinfo);
	cmd->sc_data_direction = SCSI_DATA_WRITE;
	cmd->cmd_len = 10;
	cmd->cmnd[0] = WRITE_BUFFER;
	cmd->cmnd[1] = 0x0a;
	scsi_ulto3b(targ->dv_echo_size, &cmd->cmnd[6]);
	cmd->request_bufflen = targ->dv_echo_size;
	cmd->underflow = cmd->request_bufflen;
	cmd->request_buffer = targ->dv_buffer;
}

static void
ahd_linux_dv_reb(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
		 struct ahd_devinfo *devinfo, struct ahd_linux_target *targ)
{

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Sending REB\n");
	}
#endif
	ahd_linux_dv_fill_cmd(ahd, cmd, devinfo);
	cmd->sc_data_direction = SCSI_DATA_READ;
	cmd->cmd_len = 10;
	cmd->cmnd[0] = READ_BUFFER;
	cmd->cmnd[1] = 0x0a;
	scsi_ulto3b(targ->dv_echo_size, &cmd->cmnd[6]);
	cmd->request_bufflen = targ->dv_echo_size;
	cmd->underflow = cmd->request_bufflen;
	cmd->request_buffer = targ->dv_buffer1;
}

static void
ahd_linux_dv_su(struct ahd_softc *ahd, struct scsi_cmnd *cmd,
		struct ahd_devinfo *devinfo,
		struct ahd_linux_target *targ)
{
	u_int le;

	le = SID_IS_REMOVABLE(targ->inq_data) ? SSS_LOEJ : 0;

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Sending SU\n");
	}
#endif
	ahd_linux_dv_fill_cmd(ahd, cmd, devinfo);
	cmd->sc_data_direction = SCSI_DATA_NONE;
	cmd->cmd_len = 6;
	cmd->cmnd[0] = START_STOP_UNIT;
	cmd->cmnd[4] = le | SSS_START;
}

static int
ahd_linux_fallback(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	struct	ahd_linux_target *targ;
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_transinfo *goal;
	struct	ahd_tmode_tstate *tstate;
	u_int	width;
	u_int	period;
	u_int	offset;
	u_int	ppr_options;
	u_int	cur_speed;
	u_int	wide_speed;
	u_int	narrow_speed;
	u_int	fallback_speed;

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		ahd_print_devinfo(ahd, devinfo);
		printf("Trying to fallback\n");
	}
#endif
	targ = ahd->platform_data->targets[devinfo->target_offset];
	tinfo = ahd_fetch_transinfo(ahd, devinfo->channel,
				    devinfo->our_scsiid,
				    devinfo->target, &tstate);
	goal = &tinfo->goal;
	width = goal->width;
	period = goal->period;
	offset = goal->offset;
	ppr_options = goal->ppr_options;
	if (offset == 0)
		period = AHD_ASYNC_XFER_PERIOD;
	if (targ->dv_next_narrow_period == 0)
		targ->dv_next_narrow_period = MAX(period, AHD_SYNCRATE_ULTRA2);
	if (targ->dv_next_wide_period == 0)
		targ->dv_next_wide_period = period;
	if (targ->dv_max_width == 0)
		targ->dv_max_width = width;
	if (targ->dv_max_ppr_options == 0)
		targ->dv_max_ppr_options = ppr_options;
	if (targ->dv_last_ppr_options == 0)
		targ->dv_last_ppr_options = ppr_options;

	cur_speed = aic_calc_speed(width, period, offset, AHD_SYNCRATE_MIN);
	wide_speed = aic_calc_speed(MSG_EXT_WDTR_BUS_16_BIT,
					  targ->dv_next_wide_period,
					  MAX_OFFSET, AHD_SYNCRATE_MIN);
	narrow_speed = aic_calc_speed(MSG_EXT_WDTR_BUS_8_BIT,
					    targ->dv_next_narrow_period,
					    MAX_OFFSET, AHD_SYNCRATE_MIN);
	fallback_speed = aic_calc_speed(width, period+1, offset,
					      AHD_SYNCRATE_MIN);
#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		printf("cur_speed= %d, wide_speed= %d, narrow_speed= %d, "
		       "fallback_speed= %d\n", cur_speed, wide_speed,
		       narrow_speed, fallback_speed);
	}
#endif

	if (cur_speed > 160000) {
		/*
		 * Paced/DT/IU_REQ only transfer speeds.  All we
		 * can do is fallback in terms of syncrate.
		 */
		period++;
	} else if (cur_speed > 80000) {
		if ((ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {
			/*
			 * Try without IU_REQ as it may be confusing
			 * an expander.
			 */
			ppr_options &= ~MSG_EXT_PPR_IU_REQ;
		} else {
			/*
			 * Paced/DT only transfer speeds.  All we
			 * can do is fallback in terms of syncrate.
			 */
			period++;
			ppr_options = targ->dv_max_ppr_options;
		}
	} else if (cur_speed > 3300) {

		/*
		 * In this range we the following
		 * options ordered from highest to
		 * lowest desireability:
		 *
		 * o Wide/DT
		 * o Wide/non-DT
		 * o Narrow at a potentally higher sync rate.
		 *
		 * All modes are tested with and without IU_REQ
		 * set since using IUs may confuse an expander.
		 */
		if ((ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {

			ppr_options &= ~MSG_EXT_PPR_IU_REQ;
		} else if ((ppr_options & MSG_EXT_PPR_DT_REQ) != 0) {
			/*
			 * Try going non-DT.
			 */
			ppr_options = targ->dv_max_ppr_options;
			ppr_options &= ~MSG_EXT_PPR_DT_REQ;
		} else if (targ->dv_last_ppr_options != 0) {
			/*
			 * Try without QAS or any other PPR options.
			 * We may need a non-PPR message to work with
			 * an expander.  We look at the "last PPR options"
			 * so we will perform this fallback even if the
			 * target responded to our PPR negotiation with
			 * no option bits set.
			 */
			ppr_options = 0;
		} else if (width == MSG_EXT_WDTR_BUS_16_BIT) {
			/*
			 * If the next narrow speed is greater than
			 * the next wide speed, fallback to narrow.
			 * Otherwise fallback to the next DT/Wide setting.
			 * The narrow async speed will always be smaller
			 * than the wide async speed, so handle this case
			 * specifically.
			 */
			ppr_options = targ->dv_max_ppr_options;
			if (narrow_speed > fallback_speed
			 || period >= AHD_ASYNC_XFER_PERIOD) {
				targ->dv_next_wide_period = period+1;
				width = MSG_EXT_WDTR_BUS_8_BIT;
				period = targ->dv_next_narrow_period;
			} else {
				period++;
			}
		} else if ((ahd->features & AHD_WIDE) != 0
			&& targ->dv_max_width != 0
			&& wide_speed >= fallback_speed
			&& (targ->dv_next_wide_period <= AHD_ASYNC_XFER_PERIOD
			 || period >= AHD_ASYNC_XFER_PERIOD)) {

			/*
			 * We are narrow.  Try falling back
			 * to the next wide speed with 
			 * all supported ppr options set.
			 */
			targ->dv_next_narrow_period = period+1;
			width = MSG_EXT_WDTR_BUS_16_BIT;
			period = targ->dv_next_wide_period;
			ppr_options = targ->dv_max_ppr_options;
		} else {
			/* Only narrow fallback is allowed. */
			period++;
			ppr_options = targ->dv_max_ppr_options;
		}
	} else {
		return (-1);
	}
	offset = MAX_OFFSET;
	ahd_find_syncrate(ahd, &period, &ppr_options, AHD_SYNCRATE_PACED);
	ahd_set_width(ahd, devinfo, width, AHD_TRANS_GOAL, FALSE);
	if (period == 0) {
		period = 0;
		offset = 0;
		ppr_options = 0;
		if (width == MSG_EXT_WDTR_BUS_8_BIT)
			targ->dv_next_narrow_period = AHD_ASYNC_XFER_PERIOD;
		else
			targ->dv_next_wide_period = AHD_ASYNC_XFER_PERIOD;
	}
	ahd_set_syncrate(ahd, devinfo, period, offset,
			 ppr_options, AHD_TRANS_GOAL, FALSE);
	targ->dv_last_ppr_options = ppr_options;
	return (0);
}

static void
ahd_linux_dv_timeout(struct scsi_cmnd *cmd)
{
	struct	ahd_softc *ahd;
	struct	scb *scb;
	u_long	flags;

	ahd = *((struct ahd_softc **)cmd->device->host->hostdata);
	ahd_lock(ahd, &flags);

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV) {
		printf("%s: Timeout while doing DV command %x.\n",
		       ahd_name(ahd), cmd->cmnd[0]);
		ahd_dump_card_state(ahd);
	}
#endif
	
	/*
	 * Guard against "done race".  No action is
	 * required if we just completed.
	 */
	if ((scb = (struct scb *)cmd->host_scribble) == NULL) {
		ahd_unlock(ahd, &flags);
		return;
	}

	/*
	 * Command has not completed.  Mark this
	 * SCB as having failing status prior to
	 * resetting the bus, so we get the correct
	 * error code.
	 */
	if ((scb->flags & SCB_SENSE) != 0)
		ahd_set_transaction_status(scb, CAM_AUTOSENSE_FAIL);
	else
		ahd_set_transaction_status(scb, CAM_CMD_TIMEOUT);
	ahd_reset_channel(ahd, cmd->device->channel + 'A', /*initiate*/TRUE);

	/*
	 * Add a minimal bus settle delay for devices that are slow to
	 * respond after bus resets.
	 */
	ahd_freeze_simq(ahd);
	init_timer(&ahd->platform_data->reset_timer);
	ahd->platform_data->reset_timer.data = (u_long)ahd;
	ahd->platform_data->reset_timer.expires = jiffies + HZ / 2;
	ahd->platform_data->reset_timer.function =
	    (ahd_linux_callback_t *)ahd_release_simq;
	add_timer(&ahd->platform_data->reset_timer);
	if (ahd_linux_next_device_to_run(ahd) != NULL)
		ahd_schedule_runq(ahd);
	ahd_linux_run_complete_queue(ahd);
	ahd_unlock(ahd, &flags);
}

static void
ahd_linux_dv_complete(struct scsi_cmnd *cmd)
{
	struct ahd_softc *ahd;

	ahd = *((struct ahd_softc **)cmd->device->host->hostdata);

	/* Delete the DV timer before it goes off! */
	scsi_delete_timer(cmd);

#ifdef AHD_DEBUG
	if (ahd_debug & AHD_SHOW_DV)
		printf("%s:%c:%d: Command completed, status= 0x%x\n",
		       ahd_name(ahd), cmd->device->channel, cmd->device->id,
		       cmd->result);
#endif

	/* Wake up the state machine */
	up(&ahd->platform_data->dv_cmd_sem);
}

static void
ahd_linux_generate_dv_pattern(struct ahd_linux_target *targ)
{
	uint16_t b;
	u_int	 i;
	u_int	 j;

	if (targ->dv_buffer != NULL)
		free(targ->dv_buffer, M_DEVBUF);
	targ->dv_buffer = malloc(targ->dv_echo_size, M_DEVBUF, M_WAITOK);
	if (targ->dv_buffer1 != NULL)
		free(targ->dv_buffer1, M_DEVBUF);
	targ->dv_buffer1 = malloc(targ->dv_echo_size, M_DEVBUF, M_WAITOK);

	i = 0;

	b = 0x0001;
	for (j = 0 ; i < targ->dv_echo_size; j++) {
		if (j < 32) {
			/*
			 * 32bytes of sequential numbers.
			 */
			targ->dv_buffer[i++] = j & 0xff;
		} else if (j < 48) {
			/*
			 * 32bytes of repeating 0x0000, 0xffff.
			 */
			targ->dv_buffer[i++] = (j & 0x02) ? 0xff : 0x00;
		} else if (j < 64) {
			/*
			 * 32bytes of repeating 0x5555, 0xaaaa.
			 */
			targ->dv_buffer[i++] = (j & 0x02) ? 0xaa : 0x55;
		} else {
			/*
			 * Remaining buffer is filled with a repeating
			 * patter of:
			 *
			 *	 0xffff
			 *	~0x0001 << shifted once in each loop.
			 */
			if (j & 0x02) {
				if (j & 0x01) {
					targ->dv_buffer[i++] = ~(b >> 8) & 0xff;
					b <<= 1;
					if (b == 0x0000)
						b = 0x0001;
				} else {
					targ->dv_buffer[i++] = (~b & 0xff);
				}
			} else {
				targ->dv_buffer[i++] = 0xff;
			}
		}
	}
}

static u_int
ahd_linux_user_tagdepth(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	static int warned_user;
	u_int tags;

	tags = 0;
	if ((ahd->user_discenable & devinfo->target_mask) != 0) {
		if (ahd->unit >= NUM_ELEMENTS(aic79xx_tag_info)) {

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

static u_int
ahd_linux_user_dv_setting(struct ahd_softc *ahd)
{
	static int warned_user;
	int dv;

	if (ahd->unit >= NUM_ELEMENTS(aic79xx_dv_settings)) {

		if (warned_user == 0) {
			printf(KERN_WARNING
"aic79xx: WARNING: Insufficient dv settings instances\n"
"aic79xx: for installed controllers. Using defaults\n"
"aic79xx: Please update the aic79xx_dv_settings array in"
"aic79xx: the aic79xx_osm.c source file.\n");
			warned_user++;
		}
		dv = -1;
	} else {

		dv = aic79xx_dv_settings[ahd->unit];
	}

	if (dv < 0) {
		/*
		 * Apply the default.
		 */
		dv = 1;
		if (ahd->seep_config != 0)
			dv = (ahd->seep_config->bios_control & CFENABLEDV);
	}
	return (dv);
}

static void
ahd_linux_setup_user_rd_strm_settings(struct ahd_softc *ahd)
{
	static	int warned_user;
	u_int	rd_strm_mask;
	u_int	target_id;

	/*
	 * If we have specific read streaming info for this controller,
	 * apply it.  Otherwise use the defaults.
	 */
	 if (ahd->unit >= NUM_ELEMENTS(aic79xx_rd_strm_info)) {

		if (warned_user == 0) {

			printf(KERN_WARNING
"aic79xx: WARNING: Insufficient rd_strm instances\n"
"aic79xx: for installed controllers. Using defaults\n"
"aic79xx: Please update the aic79xx_rd_strm_info array\n"
"aic79xx: in the aic79xx_osm.c source file.\n");
			warned_user++;
		}
		rd_strm_mask = AIC79XX_CONFIGED_RD_STRM;
	} else {

		rd_strm_mask = aic79xx_rd_strm_info[ahd->unit];
	}
	for (target_id = 0; target_id < 16; target_id++) {
		struct ahd_devinfo devinfo;
		struct ahd_initiator_tinfo *tinfo;
		struct ahd_tmode_tstate *tstate;

		tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
					    target_id, &tstate);
		ahd_compile_devinfo(&devinfo, ahd->our_id, target_id,
				    CAM_LUN_WILDCARD, 'A', ROLE_INITIATOR);
		tinfo->user.ppr_options &= ~MSG_EXT_PPR_RD_STRM;
		if ((rd_strm_mask & devinfo.target_mask) != 0)
			tinfo->user.ppr_options |= MSG_EXT_PPR_RD_STRM;
	}
}

/*
 * Determines the queue depth for a given device.
 */
static void
ahd_linux_device_queue_depth(struct ahd_softc *ahd,
			     struct ahd_linux_device *dev)
{
	struct	ahd_devinfo devinfo;
	u_int	tags;

	ahd_compile_devinfo(&devinfo,
			    ahd->our_id,
			    dev->target->target, dev->lun,
			    dev->target->channel == 0 ? 'A' : 'B',
			    ROLE_INITIATOR);
	tags = ahd_linux_user_tagdepth(ahd, &devinfo);
	if (tags != 0
	 && dev->scsi_device != NULL
	 && dev->scsi_device->tagged_supported != 0) {

		ahd_set_tags(ahd, &devinfo, AHD_QUEUE_TAGGED);
		ahd_print_devinfo(ahd, &devinfo);
		printf("Tagged Queuing enabled.  Depth %d\n", tags);
	} else {
		ahd_set_tags(ahd, &devinfo, AHD_QUEUE_NONE);
	}
}

static void
ahd_linux_run_device_queue(struct ahd_softc *ahd, struct ahd_linux_device *dev)
{
	struct	 ahd_cmd *acmd;
	struct	 scsi_cmnd *cmd;
	struct	 scb *scb;
	struct	 hardware_scb *hscb;
	struct	 ahd_initiator_tinfo *tinfo;
	struct	 ahd_tmode_tstate *tstate;
	u_int	 col_idx;
	uint16_t mask;

	if ((dev->flags & AHD_DEV_ON_RUN_LIST) != 0)
		panic("running device on run list");

	while ((acmd = TAILQ_FIRST(&dev->busyq)) != NULL
	    && dev->openings > 0 && dev->qfrozen == 0) {

		/*
		 * Schedule us to run later.  The only reason we are not
		 * running is because the whole controller Q is frozen.
		 */
		if (ahd->platform_data->qfrozen != 0
		 && AHD_DV_SIMQ_FROZEN(ahd) == 0) {

			TAILQ_INSERT_TAIL(&ahd->platform_data->device_runq,
					  dev, links);
			dev->flags |= AHD_DEV_ON_RUN_LIST;
			return;
		}

		cmd = &acmd_scsi_cmd(acmd);

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
			TAILQ_INSERT_TAIL(&ahd->platform_data->device_runq,
					 dev, links);
			dev->flags |= AHD_DEV_ON_RUN_LIST;
			ahd->flags |= AHD_RESOURCE_SHORTAGE;
			return;
		}
		TAILQ_REMOVE(&dev->busyq, acmd, acmd_links.tqe);
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

	 	if (AHD_DV_CMD(cmd) != 0)
			scb->flags |= SCB_SILENT;

		if ((tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ) != 0)
			scb->flags |= SCB_PACKETIZED;

		if ((tstate->auto_negotiate & mask) != 0) {
			scb->flags |= SCB_AUTO_NEGOTIATE;
			scb->hscb->control |= MK_MESSAGE;
		}

		if ((dev->flags & (AHD_DEV_Q_TAGGED|AHD_DEV_Q_BASIC)) != 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
			int	msg_bytes;
			uint8_t tag_msgs[2];

			msg_bytes = scsi_populate_tag_msg(cmd, tag_msgs);
			if (msg_bytes && tag_msgs[0] != MSG_SIMPLE_TASK) {
				hscb->control |= tag_msgs[0];
				if (tag_msgs[0] == MSG_ORDERED_TASK)
					dev->commands_since_idle_or_otag = 0;
			} else
#endif
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

		scb->sg_count = 0;
		ahd_set_residual(scb, 0);
		ahd_set_sense_residual(scb, 0);
		if (cmd->use_sg != 0) {
			void	*sg;
			struct	 scatterlist *cur_seg;
			u_int	 nseg;
			int	 dir;

			cur_seg = (struct scatterlist *)cmd->request_buffer;
			dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
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
			dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
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

		/* Update the error counting bucket and dump if needed */
		if (dev->target->cmds_since_error) {
			dev->target->cmds_since_error++;
			if (dev->target->cmds_since_error >
			    AHD_LINUX_ERR_THRESH)
				dev->target->cmds_since_error = 0;
		}

		if ((dev->flags & AHD_DEV_PERIODIC_OTAG) != 0)
			dev->commands_since_idle_or_otag++;
		scb->flags |= SCB_ACTIVE;
		ahd_queue_scb(ahd, scb);
	}
}

/*
 * SCSI controller interrupt handler.
 */
irqreturn_t
ahd_linux_isr(int irq, void *dev_id, struct pt_regs * regs)
{
	struct	ahd_softc *ahd;
	u_long	flags;
	int	ours;

	ahd = (struct ahd_softc *) dev_id;
	ahd_lock(ahd, &flags); 
	ours = ahd_intr(ahd);
	if (ahd_linux_next_device_to_run(ahd) != NULL)
		ahd_schedule_runq(ahd);
	ahd_linux_run_complete_queue(ahd);
	ahd_unlock(ahd, &flags);
	return IRQ_RETVAL(ours);
}

void
ahd_platform_flushwork(struct ahd_softc *ahd)
{

	while (ahd_linux_run_complete_queue(ahd) != NULL)
		;
}

static struct ahd_linux_target*
ahd_linux_alloc_target(struct ahd_softc *ahd, u_int channel, u_int target)
{
	struct ahd_linux_target *targ;

	targ = malloc(sizeof(*targ), M_DEVBUF, M_NOWAIT);
	if (targ == NULL)
		return (NULL);
	memset(targ, 0, sizeof(*targ));
	targ->channel = channel;
	targ->target = target;
	targ->ahd = ahd;
	targ->flags = AHD_DV_REQUIRED;
	ahd->platform_data->targets[target] = targ;
	return (targ);
}

static void
ahd_linux_free_target(struct ahd_softc *ahd, struct ahd_linux_target *targ)
{
	struct ahd_devinfo devinfo;
	struct ahd_initiator_tinfo *tinfo;
	struct ahd_tmode_tstate *tstate;
	u_int our_id;
	u_int target_offset;
	char channel;

	/*
	 * Force a negotiation to async/narrow on any
	 * future command to this device unless a bus
	 * reset occurs between now and that command.
	 */
	channel = 'A' + targ->channel;
	our_id = ahd->our_id;
	target_offset = targ->target;
	tinfo = ahd_fetch_transinfo(ahd, channel, our_id,
				    targ->target, &tstate);
	ahd_compile_devinfo(&devinfo, our_id, targ->target, CAM_LUN_WILDCARD,
			    channel, ROLE_INITIATOR);
	ahd_set_syncrate(ahd, &devinfo, 0, 0, 0,
			 AHD_TRANS_GOAL, /*paused*/FALSE);
	ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
		      AHD_TRANS_GOAL, /*paused*/FALSE);
	ahd_update_neg_request(ahd, &devinfo, tstate, tinfo, AHD_NEG_ALWAYS);
 	ahd->platform_data->targets[target_offset] = NULL;
	if (targ->inq_data != NULL)
		free(targ->inq_data, M_DEVBUF);
	if (targ->dv_buffer != NULL)
		free(targ->dv_buffer, M_DEVBUF);
	if (targ->dv_buffer1 != NULL)
		free(targ->dv_buffer1, M_DEVBUF);
	free(targ, M_DEVBUF);
}

static struct ahd_linux_device*
ahd_linux_alloc_device(struct ahd_softc *ahd,
		 struct ahd_linux_target *targ, u_int lun)
{
	struct ahd_linux_device *dev;

	dev = malloc(sizeof(*dev), M_DEVBUG, M_NOWAIT);
	if (dev == NULL)
		return (NULL);
	memset(dev, 0, sizeof(*dev));
	init_timer(&dev->timer);
	TAILQ_INIT(&dev->busyq);
	dev->flags = AHD_DEV_UNCONFIGURED;
	dev->lun = lun;
	dev->target = targ;

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
	
	targ->refcount++;
	targ->devices[lun] = dev;
	return (dev);
}

static void
ahd_linux_free_device(struct ahd_softc *ahd, struct ahd_linux_device *dev)
{
	struct ahd_linux_target *targ;

	del_timer(&dev->timer);
	targ = dev->target;
	targ->devices[dev->lun] = NULL;
	free(dev, M_DEVBUF);
	targ->refcount--;
	if (targ->refcount == 0
	 && (targ->flags & AHD_DV_REQUIRED) == 0)
		ahd_linux_free_target(ahd, targ);
}

void
ahd_send_async(struct ahd_softc *ahd, char channel,
	       u_int target, u_int lun, ac_code code, void *arg)
{
	switch (code) {
	case AC_TRANSFER_NEG:
	{
		char	buf[80];
		struct	ahd_linux_target *targ;
		struct	info_str info;
		struct	ahd_initiator_tinfo *tinfo;
		struct	ahd_tmode_tstate *tstate;

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
		targ = ahd->platform_data->targets[target];
		if (targ == NULL)
			break;
		if (tinfo->curr.period == targ->last_tinfo.period
		 && tinfo->curr.width == targ->last_tinfo.width
		 && tinfo->curr.offset == targ->last_tinfo.offset
		 && tinfo->curr.ppr_options == targ->last_tinfo.ppr_options)
			if (bootverbose == 0)
				break;

		targ->last_tinfo.period = tinfo->curr.period;
		targ->last_tinfo.width = tinfo->curr.width;
		targ->last_tinfo.offset = tinfo->curr.offset;
		targ->last_tinfo.ppr_options = tinfo->curr.ppr_options;

		printf("(%s:%c:", ahd_name(ahd), channel);
		if (target == CAM_TARGET_WILDCARD)
			printf("*): ");
		else
			printf("%d): ", target);
		ahd_format_transinfo(&info, &tinfo->curr);
		if (info.pos < info.length)
			*info.buffer = '\0';
		else
			buf[info.length - 1] = '\0';
		printf("%s", buf);
		break;
	}
        case AC_SENT_BDR:
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		WARN_ON(lun != CAM_LUN_WILDCARD);
		scsi_report_device_reset(ahd->platform_data->host,
					 channel - 'A', target);
#else
		Scsi_Device *scsi_dev;

		/*
		 * Find the SCSI device associated with this
		 * request and indicate that a UA is expected.
		 */
		for (scsi_dev = ahd->platform_data->host->host_queue;
		     scsi_dev != NULL; scsi_dev = scsi_dev->next) {
			if (channel - 'A' == scsi_dev->channel
			 && target == scsi_dev->id
			 && (lun == CAM_LUN_WILDCARD
			  || lun == scsi_dev->lun)) {
				scsi_dev->was_reset = 1;
				scsi_dev->expecting_cc_ua = 1;
			}
		}
#endif
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
	Scsi_Cmnd *cmd;
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
		ahd_linux_handle_scsi_status(ahd, dev, scb);
	} else if (ahd_get_transaction_status(scb) == CAM_SEL_TIMEOUT) {
		dev->flags |= AHD_DEV_UNCONFIGURED;
		if (AHD_DV_CMD(cmd) == FALSE)
			dev->target->flags &= ~AHD_DV_REQUIRED;
	}
	/*
	 * Start DV for devices that require it assuming the first command
	 * sent does not result in a selection timeout.
	 */
	if (ahd_get_transaction_status(scb) != CAM_SEL_TIMEOUT
	 && (dev->target->flags & AHD_DV_REQUIRED) != 0)
		ahd_linux_start_dv(ahd);

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

	if (TAILQ_EMPTY(&dev->busyq)) {
		if ((dev->flags & AHD_DEV_UNCONFIGURED) != 0
		 && dev->active == 0
		 && (dev->flags & AHD_DEV_TIMER_ACTIVE) == 0)
			ahd_linux_free_device(ahd, dev);
	} else if ((dev->flags & AHD_DEV_ON_RUN_LIST) == 0) {
		TAILQ_INSERT_TAIL(&ahd->platform_data->device_runq, dev, links);
		dev->flags |= AHD_DEV_ON_RUN_LIST;
	}

	if ((scb->flags & SCB_RECOVERY_SCB) != 0) {
		printf("Recovery SCB completes\n");
		if (ahd_get_transaction_status(scb) == CAM_BDR_SENT
		 || ahd_get_transaction_status(scb) == CAM_REQ_ABORTED)
			ahd_set_transaction_status(scb, CAM_CMD_TIMEOUT);
		if ((scb->platform_data->flags & AHD_SCB_UP_EH_SEM) != 0) {
			scb->platform_data->flags &= ~AHD_SCB_UP_EH_SEM;
			up(&ahd->platform_data->eh_sem);
		}
	}

	ahd_free_scb(ahd, scb);
	ahd_linux_queue_cmd_complete(ahd, cmd);

	if ((ahd->platform_data->flags & AHD_DV_WAIT_SIMQ_EMPTY) != 0
	 && LIST_FIRST(&ahd->pending_scbs) == NULL) {
		ahd->platform_data->flags &= ~AHD_DV_WAIT_SIMQ_EMPTY;
		up(&ahd->platform_data->dv_sem);
	}
}

static void
ahd_linux_handle_scsi_status(struct ahd_softc *ahd,
			     struct ahd_linux_device *dev, struct scb *scb)
{
	struct	ahd_devinfo devinfo;

	ahd_compile_devinfo(&devinfo,
			    ahd->our_id,
			    dev->target->target, dev->lun,
			    dev->target->channel == 0 ? 'A' : 'B',
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
		Scsi_Cmnd *cmd;

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
				sense_size = MIN(sizeof(struct scsi_sense_data)
					       - ahd_get_sense_residual(scb),
						 sizeof(cmd->sense_buffer));
				sense_offset = 0;
			} else {
				/*
				 * Copy only the sense data into the provided
				 * buffer.
				 */
				siu = (struct scsi_status_iu_header *)
				    scb->sense_data;
				sense_size = MIN(scsi_4btoul(siu->sense_length),
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
			ahd_platform_set_tags(ahd, &devinfo,
				     (dev->flags & AHD_DEV_Q_BASIC)
				   ? AHD_QUEUE_BASIC : AHD_QUEUE_TAGGED);
			break;
		}
		/*
		 * Drop down to a single opening, and treat this
		 * as if the target returned BUSY SCSI status.
		 */
		dev->openings = 1;
		ahd_platform_set_tags(ahd, &devinfo,
			     (dev->flags & AHD_DEV_Q_BASIC)
			   ? AHD_QUEUE_BASIC : AHD_QUEUE_TAGGED);
		ahd_set_scsi_status(scb, SCSI_STATUS_BUSY);
		/* FALLTHROUGH */
	}
	case SCSI_STATUS_BUSY:
		/*
		 * Set a short timer to defer sending commands for
		 * a bit since Linux will not delay in this case.
		 */
		if ((dev->flags & AHD_DEV_TIMER_ACTIVE) != 0) {
			printf("%s:%c:%d: Device Timer still active during "
			       "busy processing\n", ahd_name(ahd),
				dev->target->channel, dev->target->target);
			break;
		}
		dev->flags |= AHD_DEV_TIMER_ACTIVE;
		dev->qfrozen++;
		init_timer(&dev->timer);
		dev->timer.data = (u_long)dev;
		dev->timer.expires = jiffies + (HZ/2);
		dev->timer.function = ahd_linux_dev_timed_unfreeze;
		add_timer(&dev->timer);
		break;
	}
}

static void
ahd_linux_queue_cmd_complete(struct ahd_softc *ahd, Scsi_Cmnd *cmd)
{
	/*
	 * Typically, the complete queue has very few entries
	 * queued to it before the queue is emptied by
	 * ahd_linux_run_complete_queue, so sorting the entries
	 * by generation number should be inexpensive.
	 * We perform the sort so that commands that complete
	 * with an error are retuned in the order origionally
	 * queued to the controller so that any subsequent retries
	 * are performed in order.  The underlying ahd routines do
	 * not guarantee the order that aborted commands will be
	 * returned to us.
	 */
	struct ahd_completeq *completeq;
	struct ahd_cmd *list_cmd;
	struct ahd_cmd *acmd;

	/*
	 * Map CAM error codes into Linux Error codes.  We
	 * avoid the conversion so that the DV code has the
	 * full error information available when making
	 * state change decisions.
	 */
	if (AHD_DV_CMD(cmd) == FALSE) {
		uint32_t status;
		u_int new_status;

		status = ahd_cmd_get_transaction_status(cmd);
		if (status != CAM_REQ_CMP) {
			struct ahd_linux_device *dev;
			struct ahd_devinfo devinfo;
			cam_status cam_status;
			uint32_t action;
			u_int scsi_status;

			dev = ahd_linux_get_device(ahd, cmd->device->channel,
						   cmd->device->id,
						   cmd->device->lun,
						   /*alloc*/FALSE);

			if (dev == NULL)
				goto no_fallback;

			ahd_compile_devinfo(&devinfo,
					    ahd->our_id,
					    dev->target->target, dev->lun,
					    dev->target->channel == 0 ? 'A':'B',
					    ROLE_INITIATOR);

			scsi_status = ahd_cmd_get_scsi_status(cmd);
			cam_status = ahd_cmd_get_transaction_status(cmd);
			action = aic_error_action(cmd, dev->target->inq_data,
						  cam_status, scsi_status);
			if ((action & SSQ_FALLBACK) != 0) {

				/* Update stats */
				dev->target->errors_detected++;
				if (dev->target->cmds_since_error == 0)
					dev->target->cmds_since_error++;
				else {
					dev->target->cmds_since_error = 0;
					ahd_linux_fallback(ahd, &devinfo);
				}
			}
		}
no_fallback:
		switch (status) {
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
			/*
			 * If we want the request requeued, make sure there
			 * are sufficent retries.  In the old scsi error code,
			 * we used to be able to specify a result code that
			 * bypassed the retry count.  Now we must use this
			 * hack.  We also "fake" a check condition with
			 * a sense code of ABORTED COMMAND.  This seems to
			 * evoke a retry even if this command is being sent
			 * via the eh thread.  Ick!  Ick!  Ick!
			 */
			if (cmd->retries > 0)
				cmd->retries--;
			new_status = DID_OK;
			ahd_cmd_set_scsi_status(cmd, SCSI_STATUS_CHECK_COND);
			cmd->result |= (DRIVER_SENSE << 24);
			memset(cmd->sense_buffer, 0,
			       sizeof(cmd->sense_buffer));
			cmd->sense_buffer[0] = SSD_ERRCODE_VALID
					     | SSD_CURRENT_ERROR;
			cmd->sense_buffer[2] = SSD_KEY_ABORTED_COMMAND;
			break;
		default:
			/* We should never get here */
			new_status = DID_ERROR;
			break;
		}

		ahd_cmd_set_transaction_status(cmd, new_status);
	}

	completeq = &ahd->platform_data->completeq;
	list_cmd = TAILQ_FIRST(completeq);
	acmd = (struct ahd_cmd *)cmd;
	while (list_cmd != NULL
	    && acmd_scsi_cmd(list_cmd).serial_number
	     < acmd_scsi_cmd(acmd).serial_number)
		list_cmd = TAILQ_NEXT(list_cmd, acmd_links.tqe);
	if (list_cmd != NULL)
		TAILQ_INSERT_BEFORE(list_cmd, acmd, acmd_links.tqe);
	else
		TAILQ_INSERT_TAIL(completeq, acmd, acmd_links.tqe);
}

static void
ahd_linux_filter_inquiry(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	struct	scsi_inquiry_data *sid;
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_transinfo *user;
	struct	ahd_transinfo *goal;
	struct	ahd_transinfo *curr;
	struct	ahd_tmode_tstate *tstate;
	struct	ahd_linux_device *dev;
	u_int	width;
	u_int	period;
	u_int	offset;
	u_int	ppr_options;
	u_int	trans_version;
	u_int	prot_version;

	/*
	 * Determine if this lun actually exists.  If so,
	 * hold on to its corresponding device structure.
	 * If not, make sure we release the device and
	 * don't bother processing the rest of this inquiry
	 * command.
	 */
	dev = ahd_linux_get_device(ahd, devinfo->channel - 'A',
				   devinfo->target, devinfo->lun,
				   /*alloc*/TRUE);

	sid = (struct scsi_inquiry_data *)dev->target->inq_data;
	if (SID_QUAL(sid) == SID_QUAL_LU_CONNECTED) {

		dev->flags &= ~AHD_DEV_UNCONFIGURED;
	} else {
		dev->flags |= AHD_DEV_UNCONFIGURED;
		return;
	}

	/*
	 * Update our notion of this device's transfer
	 * negotiation capabilities.
	 */
	tinfo = ahd_fetch_transinfo(ahd, devinfo->channel,
				    devinfo->our_scsiid,
				    devinfo->target, &tstate);
	user = &tinfo->user;
	goal = &tinfo->goal;
	curr = &tinfo->curr;
	width = user->width;
	period = user->period;
	offset = user->offset;
	ppr_options = user->ppr_options;
	trans_version = user->transport_version;
	prot_version = MIN(user->protocol_version, SID_ANSI_REV(sid));

	/*
	 * Only attempt SPI3/4 once we've verified that
	 * the device claims to support SPI3/4 features.
	 */
	if (prot_version < SCSI_REV_2)
		trans_version = SID_ANSI_REV(sid);
	else
		trans_version = SCSI_REV_2;

	if ((sid->flags & SID_WBus16) == 0)
		width = MSG_EXT_WDTR_BUS_8_BIT;
	if ((sid->flags & SID_Sync) == 0) {
		period = 0;
		offset = 0;
		ppr_options = 0;
	}
	if ((sid->spi3data & SID_SPI_QAS) == 0)
		ppr_options &= ~MSG_EXT_PPR_QAS_REQ;
	if ((sid->spi3data & SID_SPI_CLOCK_DT) == 0)
		ppr_options &= MSG_EXT_PPR_QAS_REQ;
	if ((sid->spi3data & SID_SPI_IUS) == 0)
		ppr_options &= (MSG_EXT_PPR_DT_REQ
			      | MSG_EXT_PPR_QAS_REQ);

	if (prot_version > SCSI_REV_2
	 && ppr_options != 0)
		trans_version = user->transport_version;

	ahd_validate_width(ahd, /*tinfo limit*/NULL, &width, ROLE_UNKNOWN);
	ahd_find_syncrate(ahd, &period, &ppr_options, AHD_SYNCRATE_MAX);
	ahd_validate_offset(ahd, /*tinfo limit*/NULL, period,
			    &offset, width, ROLE_UNKNOWN);
	if (offset == 0 || period == 0) {
		period = 0;
		offset = 0;
		ppr_options = 0;
	}
	/* Apply our filtered user settings. */
	curr->transport_version = trans_version;
	curr->protocol_version = prot_version;
	ahd_set_width(ahd, devinfo, width, AHD_TRANS_GOAL, /*paused*/FALSE);
	ahd_set_syncrate(ahd, devinfo, period, offset, ppr_options,
			 AHD_TRANS_GOAL, /*paused*/FALSE);
}

void
ahd_freeze_simq(struct ahd_softc *ahd)
{
	ahd->platform_data->qfrozen++;
	if (ahd->platform_data->qfrozen == 1) {
		scsi_block_requests(ahd->platform_data->host);
		ahd_platform_abort_scbs(ahd, CAM_TARGET_WILDCARD, ALL_CHANNELS,
					CAM_LUN_WILDCARD, SCB_LIST_NULL,
					ROLE_INITIATOR, CAM_REQUEUE_REQ);
	}
}

void
ahd_release_simq(struct ahd_softc *ahd)
{
	u_long s;
	int    unblock_reqs;

	unblock_reqs = 0;
	ahd_lock(ahd, &s);
	if (ahd->platform_data->qfrozen > 0)
		ahd->platform_data->qfrozen--;
	if (ahd->platform_data->qfrozen == 0) {
		unblock_reqs = 1;
	}
	if (AHD_DV_SIMQ_FROZEN(ahd)
	 && ((ahd->platform_data->flags & AHD_DV_WAIT_SIMQ_RELEASE) != 0)) {
		ahd->platform_data->flags &= ~AHD_DV_WAIT_SIMQ_RELEASE;
		up(&ahd->platform_data->dv_sem);
	}
	ahd_schedule_runq(ahd);
	ahd_unlock(ahd, &s);
	/*
	 * There is still a race here.  The mid-layer
	 * should keep its own freeze count and use
	 * a bottom half handler to run the queues
	 * so we can unblock with our own lock held.
	 */
	if (unblock_reqs)
		scsi_unblock_requests(ahd->platform_data->host);
}

static void
ahd_linux_sem_timeout(u_long arg)
{
	struct	scb *scb;
	struct	ahd_softc *ahd;
	u_long	s;

	scb = (struct scb *)arg;
	ahd = scb->ahd_softc;
	ahd_lock(ahd, &s);
	if ((scb->platform_data->flags & AHD_SCB_UP_EH_SEM) != 0) {
		scb->platform_data->flags &= ~AHD_SCB_UP_EH_SEM;
		up(&ahd->platform_data->eh_sem);
	}
	ahd_unlock(ahd, &s);
}

static void
ahd_linux_dev_timed_unfreeze(u_long arg)
{
	struct ahd_linux_device *dev;
	struct ahd_softc *ahd;
	u_long s;

	dev = (struct ahd_linux_device *)arg;
	ahd = dev->target->ahd;
	ahd_lock(ahd, &s);
	dev->flags &= ~AHD_DEV_TIMER_ACTIVE;
	if (dev->qfrozen > 0)
		dev->qfrozen--;
	if (dev->qfrozen == 0
	 && (dev->flags & AHD_DEV_ON_RUN_LIST) == 0)
		ahd_linux_run_device_queue(ahd, dev);
	if ((dev->flags & AHD_DEV_UNCONFIGURED) != 0
	 && dev->active == 0)
		ahd_linux_free_device(ahd, dev);
	ahd_unlock(ahd, &s);
}

void
ahd_platform_dump_card_state(struct ahd_softc *ahd)
{
	struct ahd_linux_device *dev;
	int target;
	int maxtarget;
	int lun;
	int i;

	maxtarget = (ahd->features & AHD_WIDE) ? 15 : 7;
	for (target = 0; target <=maxtarget; target++) {

		for (lun = 0; lun < AHD_NUM_LUNS; lun++) {
			struct ahd_cmd *acmd;

			dev = ahd_linux_get_device(ahd, 0, target,
						   lun, /*alloc*/FALSE);
			if (dev == NULL)
				continue;

			printf("DevQ(%d:%d:%d): ", 0, target, lun);
			i = 0;
			TAILQ_FOREACH(acmd, &dev->busyq, acmd_links.tqe) {
				if (i++ > AHD_SCB_MAX)
					break;
			}
			printf("%d waiting\n", i);
		}
	}
}

static int __init
ahd_linux_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	return ahd_linux_detect(&aic79xx_driver_template);
#else
	scsi_register_module(MODULE_SCSI_HA, &aic79xx_driver_template);
	if (aic79xx_driver_template.present == 0) {
		scsi_unregister_module(MODULE_SCSI_HA,
				       &aic79xx_driver_template);
		return (-ENODEV);
	}

	return (0);
#endif
}

static void __exit
ahd_linux_exit(void)
{
	struct ahd_softc *ahd;

	/*
	 * Shutdown DV threads before going into the SCSI mid-layer.
	 * This avoids situations where the mid-layer locks the entire
	 * kernel so that waiting for our DV threads to exit leads
	 * to deadlock.
	 */
	TAILQ_FOREACH(ahd, &ahd_tailq, links) {

		ahd_linux_kill_dv_thread(ahd);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	/*
	 * In 2.4 we have to unregister from the PCI core _after_
	 * unregistering from the scsi midlayer to avoid dangling
	 * references.
	 */
	scsi_unregister_module(MODULE_SCSI_HA, &aic79xx_driver_template);
#endif
	ahd_linux_pci_exit();
}

module_init(ahd_linux_init);
module_exit(ahd_linux_exit);
