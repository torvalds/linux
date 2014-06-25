/*
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
 * String handling code courtesy of Gerard Roudier's <groudier@club-internet.fr>
 * sym driver.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic79xx_proc.c#19 $
 */
#include "aic79xx_osm.h"
#include "aic79xx_inline.h"

static void	ahd_dump_target_state(struct ahd_softc *ahd,
				      struct seq_file *m,
				      u_int our_id, char channel,
				      u_int target_id);
static void	ahd_dump_device_state(struct seq_file *m,
				      struct scsi_device *sdev);

/*
 * Table of syncrates that don't follow the "divisible by 4"
 * rule. This table will be expanded in future SCSI specs.
 */
static const struct {
	u_int period_factor;
	u_int period;	/* in 100ths of ns */
} scsi_syncrates[] = {
	{ 0x08, 625 },	/* FAST-160 */
	{ 0x09, 1250 },	/* FAST-80 */
	{ 0x0a, 2500 },	/* FAST-40 40MHz */
	{ 0x0b, 3030 },	/* FAST-40 33MHz */
	{ 0x0c, 5000 }	/* FAST-20 */
};

/*
 * Return the frequency in kHz corresponding to the given
 * sync period factor.
 */
static u_int
ahd_calc_syncsrate(u_int period_factor)
{
	int i;

	/* See if the period is in the "exception" table */
	for (i = 0; i < ARRAY_SIZE(scsi_syncrates); i++) {

		if (period_factor == scsi_syncrates[i].period_factor) {
			/* Period in kHz */
			return (100000000 / scsi_syncrates[i].period);
		}
	}

	/*
	 * Wasn't in the table, so use the standard
	 * 4 times conversion.
	 */
	return (10000000 / (period_factor * 4 * 10));
}

static void
ahd_format_transinfo(struct seq_file *m, struct ahd_transinfo *tinfo)
{
	u_int speed;
	u_int freq;
	u_int mb;

	if (tinfo->period == AHD_PERIOD_UNKNOWN) {
		seq_printf(m, "Renegotiation Pending\n");
		return;
	}
        speed = 3300;
        freq = 0;
	if (tinfo->offset != 0) {
		freq = ahd_calc_syncsrate(tinfo->period);
		speed = freq;
	}
	speed *= (0x01 << tinfo->width);
        mb = speed / 1000;
        if (mb > 0)
		seq_printf(m, "%d.%03dMB/s transfers", mb, speed % 1000);
        else
		seq_printf(m, "%dKB/s transfers", speed);

	if (freq != 0) {
		int	printed_options;

		printed_options = 0;
		seq_printf(m, " (%d.%03dMHz", freq / 1000, freq % 1000);
		if ((tinfo->ppr_options & MSG_EXT_PPR_RD_STRM) != 0) {
			seq_printf(m, " RDSTRM");
			printed_options++;
		}
		if ((tinfo->ppr_options & MSG_EXT_PPR_DT_REQ) != 0) {
			seq_printf(m, "%s", printed_options ? "|DT" : " DT");
			printed_options++;
		}
		if ((tinfo->ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {
			seq_printf(m, "%s", printed_options ? "|IU" : " IU");
			printed_options++;
		}
		if ((tinfo->ppr_options & MSG_EXT_PPR_RTI) != 0) {
			seq_printf(m, "%s",
				  printed_options ? "|RTI" : " RTI");
			printed_options++;
		}
		if ((tinfo->ppr_options & MSG_EXT_PPR_QAS_REQ) != 0) {
			seq_printf(m, "%s",
				  printed_options ? "|QAS" : " QAS");
			printed_options++;
		}
	}

	if (tinfo->width > 0) {
		if (freq != 0) {
			seq_printf(m, ", ");
		} else {
			seq_printf(m, " (");
		}
		seq_printf(m, "%dbit)", 8 * (0x01 << tinfo->width));
	} else if (freq != 0) {
		seq_printf(m, ")");
	}
	seq_printf(m, "\n");
}

static void
ahd_dump_target_state(struct ahd_softc *ahd, struct seq_file *m,
		      u_int our_id, char channel, u_int target_id)
{
	struct  scsi_target *starget;
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	int	lun;

	tinfo = ahd_fetch_transinfo(ahd, channel, our_id,
				    target_id, &tstate);
	seq_printf(m, "Target %d Negotiation Settings\n", target_id);
	seq_printf(m, "\tUser: ");
	ahd_format_transinfo(m, &tinfo->user);
	starget = ahd->platform_data->starget[target_id];
	if (starget == NULL)
		return;

	seq_printf(m, "\tGoal: ");
	ahd_format_transinfo(m, &tinfo->goal);
	seq_printf(m, "\tCurr: ");
	ahd_format_transinfo(m, &tinfo->curr);

	for (lun = 0; lun < AHD_NUM_LUNS; lun++) {
		struct scsi_device *dev;

		dev = scsi_device_lookup_by_target(starget, lun);

		if (dev == NULL)
			continue;

		ahd_dump_device_state(m, dev);
	}
}

static void
ahd_dump_device_state(struct seq_file *m, struct scsi_device *sdev)
{
	struct ahd_linux_device *dev = scsi_transport_device_data(sdev);

	seq_printf(m, "\tChannel %c Target %d Lun %d Settings\n",
		  sdev->sdev_target->channel + 'A',
		   sdev->sdev_target->id, (u8)sdev->lun);

	seq_printf(m, "\t\tCommands Queued %ld\n", dev->commands_issued);
	seq_printf(m, "\t\tCommands Active %d\n", dev->active);
	seq_printf(m, "\t\tCommand Openings %d\n", dev->openings);
	seq_printf(m, "\t\tMax Tagged Openings %d\n", dev->maxtags);
	seq_printf(m, "\t\tDevice Queue Frozen Count %d\n", dev->qfrozen);
}

int
ahd_proc_write_seeprom(struct Scsi_Host *shost, char *buffer, int length)
{
	struct	ahd_softc *ahd = *(struct ahd_softc **)shost->hostdata;
	ahd_mode_state saved_modes;
	int have_seeprom;
	u_long s;
	int paused;
	int written;

	/* Default to failure. */
	written = -EINVAL;
	ahd_lock(ahd, &s);
	paused = ahd_is_paused(ahd);
	if (!paused)
		ahd_pause(ahd);

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	if (length != sizeof(struct seeprom_config)) {
		printk("ahd_proc_write_seeprom: incorrect buffer size\n");
		goto done;
	}

	have_seeprom = ahd_verify_cksum((struct seeprom_config*)buffer);
	if (have_seeprom == 0) {
		printk("ahd_proc_write_seeprom: cksum verification failed\n");
		goto done;
	}

	have_seeprom = ahd_acquire_seeprom(ahd);
	if (!have_seeprom) {
		printk("ahd_proc_write_seeprom: No Serial EEPROM\n");
		goto done;
	} else {
		u_int start_addr;

		if (ahd->seep_config == NULL) {
			ahd->seep_config = kmalloc(sizeof(*ahd->seep_config), GFP_ATOMIC);
			if (ahd->seep_config == NULL) {
				printk("aic79xx: Unable to allocate serial "
				       "eeprom buffer.  Write failing\n");
				goto done;
			}
		}
		printk("aic79xx: Writing Serial EEPROM\n");
		start_addr = 32 * (ahd->channel - 'A');
		ahd_write_seeprom(ahd, (u_int16_t *)buffer, start_addr,
				  sizeof(struct seeprom_config)/2);
		ahd_read_seeprom(ahd, (uint16_t *)ahd->seep_config,
				 start_addr, sizeof(struct seeprom_config)/2,
				 /*ByteStream*/FALSE);
		ahd_release_seeprom(ahd);
		written = length;
	}

done:
	ahd_restore_modes(ahd, saved_modes);
	if (!paused)
		ahd_unpause(ahd);
	ahd_unlock(ahd, &s);
	return (written);
}
/*
 * Return information to handle /proc support for the driver.
 */
int
ahd_linux_show_info(struct seq_file *m, struct Scsi_Host *shost)
{
	struct	ahd_softc *ahd = *(struct ahd_softc **)shost->hostdata;
	char	ahd_info[256];
	u_int	max_targ;
	u_int	i;

	seq_printf(m, "Adaptec AIC79xx driver version: %s\n",
		  AIC79XX_DRIVER_VERSION);
	seq_printf(m, "%s\n", ahd->description);
	ahd_controller_info(ahd, ahd_info);
	seq_printf(m, "%s\n", ahd_info);
	seq_printf(m, "Allocated SCBs: %d, SG List Length: %d\n\n",
		  ahd->scb_data.numscbs, AHD_NSEG);

	max_targ = 16;

	if (ahd->seep_config == NULL)
		seq_printf(m, "No Serial EEPROM\n");
	else {
		seq_printf(m, "Serial EEPROM:\n");
		for (i = 0; i < sizeof(*ahd->seep_config)/2; i++) {
			if (((i % 8) == 0) && (i != 0)) {
				seq_printf(m, "\n");
			}
			seq_printf(m, "0x%.4x ",
				  ((uint16_t*)ahd->seep_config)[i]);
		}
		seq_printf(m, "\n");
	}
	seq_printf(m, "\n");

	if ((ahd->features & AHD_WIDE) == 0)
		max_targ = 8;

	for (i = 0; i < max_targ; i++) {

		ahd_dump_target_state(ahd, m, ahd->our_id, 'A',
				      /*target_id*/i);
	}
	return 0;
}
