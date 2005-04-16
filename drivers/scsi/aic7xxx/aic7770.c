/*
 * Product specific probe and attach routines for:
 * 	27/284X and aic7770 motherboard SCSI controllers
 *
 * Copyright (c) 1994-1998, 2000, 2001 Justin T. Gibbs.
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
 * $Id: //depot/aic7xxx/aic7xxx/aic7770.c#32 $
 *
 * $FreeBSD$
 */

#ifdef __linux__
#include "aic7xxx_osm.h"
#include "aic7xxx_inline.h"
#include "aic7xxx_93cx6.h"
#else
#include <dev/aic7xxx/aic7xxx_osm.h>
#include <dev/aic7xxx/aic7xxx_inline.h>
#include <dev/aic7xxx/aic7xxx_93cx6.h>
#endif

#define ID_AIC7770	0x04907770
#define ID_AHA_274x	0x04907771
#define ID_AHA_284xB	0x04907756 /* BIOS enabled */
#define ID_AHA_284x	0x04907757 /* BIOS disabled*/
#define	ID_OLV_274x	0x04907782 /* Olivetti OEM */
#define	ID_OLV_274xD	0x04907783 /* Olivetti OEM (Differential) */

static int aic7770_chip_init(struct ahc_softc *ahc);
static int aic7770_suspend(struct ahc_softc *ahc);
static int aic7770_resume(struct ahc_softc *ahc);
static int aha2840_load_seeprom(struct ahc_softc *ahc);
static ahc_device_setup_t ahc_aic7770_VL_setup;
static ahc_device_setup_t ahc_aic7770_EISA_setup;
static ahc_device_setup_t ahc_aic7770_setup;

struct aic7770_identity aic7770_ident_table[] =
{
	{
		ID_AHA_274x,
		0xFFFFFFFF,
		"Adaptec 274X SCSI adapter",
		ahc_aic7770_EISA_setup
	},
	{
		ID_AHA_284xB,
		0xFFFFFFFE,
		"Adaptec 284X SCSI adapter",
		ahc_aic7770_VL_setup
	},
	{
		ID_AHA_284x,
		0xFFFFFFFE,
		"Adaptec 284X SCSI adapter (BIOS Disabled)",
		ahc_aic7770_VL_setup
	},
	{
		ID_OLV_274x,
		0xFFFFFFFF,
		"Adaptec (Olivetti OEM) 274X SCSI adapter",
		ahc_aic7770_EISA_setup
	},
	{
		ID_OLV_274xD,
		0xFFFFFFFF,
		"Adaptec (Olivetti OEM) 274X Differential SCSI adapter",
		ahc_aic7770_EISA_setup
	},
	/* Generic chip probes for devices we don't know 'exactly' */
	{
		ID_AIC7770,
		0xFFFFFFFF,
		"Adaptec aic7770 SCSI adapter",
		ahc_aic7770_EISA_setup
	}
};
const int ahc_num_aic7770_devs = NUM_ELEMENTS(aic7770_ident_table);

struct aic7770_identity *
aic7770_find_device(uint32_t id)
{
	struct	aic7770_identity *entry;
	int	i;

	for (i = 0; i < ahc_num_aic7770_devs; i++) {
		entry = &aic7770_ident_table[i];
		if (entry->full_id == (id & entry->id_mask))
			return (entry);
	}
	return (NULL);
}

int
aic7770_config(struct ahc_softc *ahc, struct aic7770_identity *entry, u_int io)
{
	u_long	l;
	int	error;
	int	have_seeprom;
	u_int	hostconf;
	u_int   irq;
	u_int	intdef;

	error = entry->setup(ahc);
	have_seeprom = 0;
	if (error != 0)
		return (error);

	error = aic7770_map_registers(ahc, io);
	if (error != 0)
		return (error);

	/*
	 * Before we continue probing the card, ensure that
	 * its interrupts are *disabled*.  We don't want
	 * a misstep to hang the machine in an interrupt
	 * storm.
	 */
	ahc_intr_enable(ahc, FALSE);

	ahc->description = entry->name;
	error = ahc_softc_init(ahc);
	if (error != 0)
		return (error);

	ahc->bus_chip_init = aic7770_chip_init;
	ahc->bus_suspend = aic7770_suspend;
	ahc->bus_resume = aic7770_resume;

	error = ahc_reset(ahc, /*reinit*/FALSE);
	if (error != 0)
		return (error);

	/* Make sure we have a valid interrupt vector */
	intdef = ahc_inb(ahc, INTDEF);
	irq = intdef & VECTOR;
	switch (irq) {
	case 9:
	case 10:
	case 11:
	case 12:
	case 14:
	case 15:
		break;
	default:
		printf("aic7770_config: invalid irq setting %d\n", intdef);
		return (ENXIO);
	}

	if ((intdef & EDGE_TRIG) != 0)
		ahc->flags |= AHC_EDGE_INTERRUPT;

	switch (ahc->chip & (AHC_EISA|AHC_VL)) {
	case AHC_EISA:
	{
		u_int biosctrl;
		u_int scsiconf;
		u_int scsiconf1;

		biosctrl = ahc_inb(ahc, HA_274_BIOSCTRL);
		scsiconf = ahc_inb(ahc, SCSICONF);
		scsiconf1 = ahc_inb(ahc, SCSICONF + 1);

		/* Get the primary channel information */
		if ((biosctrl & CHANNEL_B_PRIMARY) != 0)
			ahc->flags |= 1;

		if ((biosctrl & BIOSMODE) == BIOSDISABLED) {
			ahc->flags |= AHC_USEDEFAULTS;
		} else {
			if ((ahc->features & AHC_WIDE) != 0) {
				ahc->our_id = scsiconf1 & HWSCSIID;
				if (scsiconf & TERM_ENB)
					ahc->flags |= AHC_TERM_ENB_A;
			} else {
				ahc->our_id = scsiconf & HSCSIID;
				ahc->our_id_b = scsiconf1 & HSCSIID;
				if (scsiconf & TERM_ENB)
					ahc->flags |= AHC_TERM_ENB_A;
				if (scsiconf1 & TERM_ENB)
					ahc->flags |= AHC_TERM_ENB_B;
			}
		}
		if ((ahc_inb(ahc, HA_274_BIOSGLOBAL) & HA_274_EXTENDED_TRANS))
			ahc->flags |= AHC_EXTENDED_TRANS_A|AHC_EXTENDED_TRANS_B;
		break;
	}
	case AHC_VL:
	{
		have_seeprom = aha2840_load_seeprom(ahc);
		break;
	}
	default:
		break;
	}
	if (have_seeprom == 0) {
		free(ahc->seep_config, M_DEVBUF);
		ahc->seep_config = NULL;
	}

	/*
	 * Ensure autoflush is enabled
	 */
	ahc_outb(ahc, SBLKCTL, ahc_inb(ahc, SBLKCTL) & ~AUTOFLUSHDIS);

	/* Setup the FIFO threshold and the bus off time */
	hostconf = ahc_inb(ahc, HOSTCONF);
	ahc_outb(ahc, BUSSPD, hostconf & DFTHRSH);
	ahc_outb(ahc, BUSTIME, (hostconf << 2) & BOFF);

	ahc->bus_softc.aic7770_softc.busspd = hostconf & DFTHRSH;
	ahc->bus_softc.aic7770_softc.bustime = (hostconf << 2) & BOFF;

	/*
	 * Generic aic7xxx initialization.
	 */
	error = ahc_init(ahc);
	if (error != 0)
		return (error);

	error = aic7770_map_int(ahc, irq);
	if (error != 0)
		return (error);

	ahc_list_lock(&l);
	/*
	 * Link this softc in with all other ahc instances.
	 */
	ahc_softc_insert(ahc);

	/*
	 * Enable the board's BUS drivers
	 */
	ahc_outb(ahc, BCTL, ENABLE);

	ahc_list_unlock(&l);

	return (0);
}

static int
aic7770_chip_init(struct ahc_softc *ahc)
{
	ahc_outb(ahc, BUSSPD, ahc->bus_softc.aic7770_softc.busspd);
	ahc_outb(ahc, BUSTIME, ahc->bus_softc.aic7770_softc.bustime);
	ahc_outb(ahc, SBLKCTL, ahc_inb(ahc, SBLKCTL) & ~AUTOFLUSHDIS);
	ahc_outb(ahc, BCTL, ENABLE);
	return (ahc_chip_init(ahc));
}

static int
aic7770_suspend(struct ahc_softc *ahc)
{
	return (ahc_suspend(ahc));
}

static int
aic7770_resume(struct ahc_softc *ahc)
{
	return (ahc_resume(ahc));
}

/*
 * Read the 284x SEEPROM.
 */
static int
aha2840_load_seeprom(struct ahc_softc *ahc)
{
	struct	seeprom_descriptor sd;
	struct	seeprom_config *sc;
	int	have_seeprom;
	uint8_t scsi_conf;

	sd.sd_ahc = ahc;
	sd.sd_control_offset = SEECTL_2840;
	sd.sd_status_offset = STATUS_2840;
	sd.sd_dataout_offset = STATUS_2840;		
	sd.sd_chip = C46;
	sd.sd_MS = 0;
	sd.sd_RDY = EEPROM_TF;
	sd.sd_CS = CS_2840;
	sd.sd_CK = CK_2840;
	sd.sd_DO = DO_2840;
	sd.sd_DI = DI_2840;
	sc = ahc->seep_config;

	if (bootverbose)
		printf("%s: Reading SEEPROM...", ahc_name(ahc));
	have_seeprom = ahc_read_seeprom(&sd, (uint16_t *)sc,
					/*start_addr*/0, sizeof(*sc)/2);

	if (have_seeprom) {

		if (ahc_verify_cksum(sc) == 0) {
			if(bootverbose)
				printf ("checksum error\n");
			have_seeprom = 0;
		} else if (bootverbose) {
			printf("done.\n");
		}
	}

	if (!have_seeprom) {
		if (bootverbose)
			printf("%s: No SEEPROM available\n", ahc_name(ahc));
		ahc->flags |= AHC_USEDEFAULTS;
	} else {
		/*
		 * Put the data we've collected down into SRAM
		 * where ahc_init will find it.
		 */
		int	 i;
		int	 max_targ;
		uint16_t discenable;

		max_targ = (ahc->features & AHC_WIDE) != 0 ? 16 : 8;
		discenable = 0;
		for (i = 0; i < max_targ; i++){
			uint8_t target_settings;

			target_settings = (sc->device_flags[i] & CFXFER) << 4;
			if (sc->device_flags[i] & CFSYNCH)
				target_settings |= SOFS;
			if (sc->device_flags[i] & CFWIDEB)
				target_settings |= WIDEXFER;
			if (sc->device_flags[i] & CFDISC)
				discenable |= (0x01 << i);
			ahc_outb(ahc, TARG_SCSIRATE + i, target_settings);
		}
		ahc_outb(ahc, DISC_DSB, ~(discenable & 0xff));
		ahc_outb(ahc, DISC_DSB + 1, ~((discenable >> 8) & 0xff));

		ahc->our_id = sc->brtime_id & CFSCSIID;

		scsi_conf = (ahc->our_id & 0x7);
		if (sc->adapter_control & CFSPARITY)
			scsi_conf |= ENSPCHK;
		if (sc->adapter_control & CFRESETB)
			scsi_conf |= RESET_SCSI;

		if (sc->bios_control & CF284XEXTEND)		
			ahc->flags |= AHC_EXTENDED_TRANS_A;
		/* Set SCSICONF info */
		ahc_outb(ahc, SCSICONF, scsi_conf);

		if (sc->adapter_control & CF284XSTERM)
			ahc->flags |= AHC_TERM_ENB_A;
	}
	return (have_seeprom);
}

static int
ahc_aic7770_VL_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7770_setup(ahc);
	ahc->chip |= AHC_VL;
	return (error);
}

static int
ahc_aic7770_EISA_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7770_setup(ahc);
	ahc->chip |= AHC_EISA;
	return (error);
}

static int
ahc_aic7770_setup(struct ahc_softc *ahc)
{
	ahc->channel = 'A';
	ahc->channel_b = 'B';
	ahc->chip = AHC_AIC7770;
	ahc->features = AHC_AIC7770_FE;
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG;
	ahc->flags |= AHC_PAGESCBS;
	ahc->instruction_ram_size = 448;
	return (0);
}
