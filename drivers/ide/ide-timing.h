#ifndef _IDE_TIMING_H
#define _IDE_TIMING_H

/*
 * $Id: ide-timing.h,v 1.6 2001/12/23 22:47:56 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/hdreg.h>

#define XFER_PIO_5		0x0d
#define XFER_UDMA_SLOW		0x4f

struct ide_timing {
	short mode;
	short setup;	/* t1 */
	short act8b;	/* t2 for 8-bit io */
	short rec8b;	/* t2i for 8-bit io */
	short cyc8b;	/* t0 for 8-bit io */
	short active;	/* t2 or tD */
	short recover;	/* t2i or tK */
	short cycle;	/* t0 */
	short udma;	/* t2CYCTYP/2 */
};

/*
 * PIO 0-5, MWDMA 0-2 and UDMA 0-6 timings (in nanoseconds).
 * These were taken from ATA/ATAPI-6 standard, rev 0a, except
 * for PIO 5, which is a nonstandard extension and UDMA6, which
 * is currently supported only by Maxtor drives. 
 */

static struct ide_timing ide_timing[] = {

	{ XFER_UDMA_6,     0,   0,   0,   0,   0,   0,   0,  15 },
	{ XFER_UDMA_5,     0,   0,   0,   0,   0,   0,   0,  20 },
	{ XFER_UDMA_4,     0,   0,   0,   0,   0,   0,   0,  30 },
	{ XFER_UDMA_3,     0,   0,   0,   0,   0,   0,   0,  45 },

	{ XFER_UDMA_2,     0,   0,   0,   0,   0,   0,   0,  60 },
	{ XFER_UDMA_1,     0,   0,   0,   0,   0,   0,   0,  80 },
	{ XFER_UDMA_0,     0,   0,   0,   0,   0,   0,   0, 120 },

	{ XFER_UDMA_SLOW,  0,   0,   0,   0,   0,   0,   0, 150 },
                                          
	{ XFER_MW_DMA_2,  25,   0,   0,   0,  70,  25, 120,   0 },
	{ XFER_MW_DMA_1,  45,   0,   0,   0,  80,  50, 150,   0 },
	{ XFER_MW_DMA_0,  60,   0,   0,   0, 215, 215, 480,   0 },
                                          
	{ XFER_SW_DMA_2,  60,   0,   0,   0, 120, 120, 240,   0 },
	{ XFER_SW_DMA_1,  90,   0,   0,   0, 240, 240, 480,   0 },
	{ XFER_SW_DMA_0, 120,   0,   0,   0, 480, 480, 960,   0 },

	{ XFER_PIO_5,     20,  50,  30, 100,  50,  30, 100,   0 },
	{ XFER_PIO_4,     25,  70,  25, 120,  70,  25, 120,   0 },
	{ XFER_PIO_3,     30,  80,  70, 180,  80,  70, 180,   0 },

	{ XFER_PIO_2,     30, 290,  40, 330, 100,  90, 240,   0 },
	{ XFER_PIO_1,     50, 290,  93, 383, 125, 100, 383,   0 },
	{ XFER_PIO_0,     70, 290, 240, 600, 165, 150, 600,   0 },

	{ XFER_PIO_SLOW, 120, 290, 240, 960, 290, 240, 960,   0 },

	{ -1 }
};

#define IDE_TIMING_SETUP	0x01
#define IDE_TIMING_ACT8B	0x02
#define IDE_TIMING_REC8B	0x04
#define IDE_TIMING_CYC8B	0x08
#define IDE_TIMING_8BIT		0x0e
#define IDE_TIMING_ACTIVE	0x10
#define IDE_TIMING_RECOVER	0x20
#define IDE_TIMING_CYCLE	0x40
#define IDE_TIMING_UDMA		0x80
#define IDE_TIMING_ALL		0xff

#define FIT(v,vmin,vmax)	max_t(short,min_t(short,v,vmax),vmin)
#define ENOUGH(v,unit)		(((v)-1)/(unit)+1)
#define EZ(v,unit)		((v)?ENOUGH(v,unit):0)

#define XFER_MODE	0xf0
#define XFER_UDMA_133	0x48
#define XFER_UDMA_100	0x44
#define XFER_UDMA_66	0x42
#define XFER_UDMA	0x40
#define XFER_MWDMA	0x20
#define XFER_SWDMA	0x10
#define XFER_EPIO	0x01
#define XFER_PIO	0x00

static short ide_find_best_mode(ide_drive_t *drive, int map)
{
	struct hd_driveid *id = drive->id;
	short best = 0;

	if (!id)
		return XFER_PIO_SLOW;

	if ((map & XFER_UDMA) && (id->field_valid & 4)) {	/* Want UDMA and UDMA bitmap valid */

		if ((map & XFER_UDMA_133) == XFER_UDMA_133)
			if ((best = (id->dma_ultra & 0x0040) ? XFER_UDMA_6 : 0)) return best;

		if ((map & XFER_UDMA_100) == XFER_UDMA_100)
			if ((best = (id->dma_ultra & 0x0020) ? XFER_UDMA_5 : 0)) return best;

		if ((map & XFER_UDMA_66) == XFER_UDMA_66)
			if ((best = (id->dma_ultra & 0x0010) ? XFER_UDMA_4 :
                	    	    (id->dma_ultra & 0x0008) ? XFER_UDMA_3 : 0)) return best;

                if ((best = (id->dma_ultra & 0x0004) ? XFER_UDMA_2 :
                	    (id->dma_ultra & 0x0002) ? XFER_UDMA_1 :
                	    (id->dma_ultra & 0x0001) ? XFER_UDMA_0 : 0)) return best;
	}

	if ((map & XFER_MWDMA) && (id->field_valid & 2)) {	/* Want MWDMA and drive has EIDE fields */

		if ((best = (id->dma_mword & 0x0004) ? XFER_MW_DMA_2 :
                	    (id->dma_mword & 0x0002) ? XFER_MW_DMA_1 :
                	    (id->dma_mword & 0x0001) ? XFER_MW_DMA_0 : 0)) return best;
	}

	if (map & XFER_SWDMA) {					/* Want SWDMA */

 		if (id->field_valid & 2) {			/* EIDE SWDMA */

			if ((best = (id->dma_1word & 0x0004) ? XFER_SW_DMA_2 :
      				    (id->dma_1word & 0x0002) ? XFER_SW_DMA_1 :
				    (id->dma_1word & 0x0001) ? XFER_SW_DMA_0 : 0)) return best;
		}

		if (id->capability & 1) {			/* Pre-EIDE style SWDMA */

			if ((best = (id->tDMA == 2) ? XFER_SW_DMA_2 :
				    (id->tDMA == 1) ? XFER_SW_DMA_1 :
				    (id->tDMA == 0) ? XFER_SW_DMA_0 : 0)) return best;
		}
	}


	if ((map & XFER_EPIO) && (id->field_valid & 2)) {	/* EIDE PIO modes */

		if ((best = (drive->id->eide_pio_modes & 4) ? XFER_PIO_5 :
			    (drive->id->eide_pio_modes & 2) ? XFER_PIO_4 :
			    (drive->id->eide_pio_modes & 1) ? XFER_PIO_3 : 0)) return best;
	}
	
	return  (drive->id->tPIO == 2) ? XFER_PIO_2 :
		(drive->id->tPIO == 1) ? XFER_PIO_1 :
		(drive->id->tPIO == 0) ? XFER_PIO_0 : XFER_PIO_SLOW;
}

static void ide_timing_quantize(struct ide_timing *t, struct ide_timing *q, int T, int UT)
{
	q->setup   = EZ(t->setup   * 1000,  T);
	q->act8b   = EZ(t->act8b   * 1000,  T);
	q->rec8b   = EZ(t->rec8b   * 1000,  T);
	q->cyc8b   = EZ(t->cyc8b   * 1000,  T);
	q->active  = EZ(t->active  * 1000,  T);
	q->recover = EZ(t->recover * 1000,  T);
	q->cycle   = EZ(t->cycle   * 1000,  T);
	q->udma    = EZ(t->udma    * 1000, UT);
}

static void ide_timing_merge(struct ide_timing *a, struct ide_timing *b, struct ide_timing *m, unsigned int what)
{
	if (what & IDE_TIMING_SETUP  ) m->setup   = max(a->setup,   b->setup);
	if (what & IDE_TIMING_ACT8B  ) m->act8b   = max(a->act8b,   b->act8b);
	if (what & IDE_TIMING_REC8B  ) m->rec8b   = max(a->rec8b,   b->rec8b);
	if (what & IDE_TIMING_CYC8B  ) m->cyc8b   = max(a->cyc8b,   b->cyc8b);
	if (what & IDE_TIMING_ACTIVE ) m->active  = max(a->active,  b->active);
	if (what & IDE_TIMING_RECOVER) m->recover = max(a->recover, b->recover);
	if (what & IDE_TIMING_CYCLE  ) m->cycle   = max(a->cycle,   b->cycle);
	if (what & IDE_TIMING_UDMA   ) m->udma    = max(a->udma,    b->udma);
}

static struct ide_timing* ide_timing_find_mode(short speed)
{
	struct ide_timing *t;

	for (t = ide_timing; t->mode != speed; t++)
		if (t->mode < 0)
			return NULL;
	return t; 
}

static int ide_timing_compute(ide_drive_t *drive, short speed, struct ide_timing *t, int T, int UT)
{
	struct hd_driveid *id = drive->id;
	struct ide_timing *s, p;

/*
 * Find the mode.
 */

	if (!(s = ide_timing_find_mode(speed)))
		return -EINVAL;

/*
 * If the drive is an EIDE drive, it can tell us it needs extended
 * PIO/MWDMA cycle timing.
 */

	if (id && id->field_valid & 2) {	/* EIDE drive */

		memset(&p, 0, sizeof(p));

		switch (speed & XFER_MODE) {

			case XFER_PIO:
				if (speed <= XFER_PIO_2) p.cycle = p.cyc8b = id->eide_pio;
						    else p.cycle = p.cyc8b = id->eide_pio_iordy;
				break;

			case XFER_MWDMA:
				p.cycle = id->eide_dma_min;
				break;
		}

		ide_timing_merge(&p, t, t, IDE_TIMING_CYCLE | IDE_TIMING_CYC8B);
	}

/*
 * Convert the timing to bus clock counts.
 */

	ide_timing_quantize(s, t, T, UT);

/*
 * Even in DMA/UDMA modes we still use PIO access for IDENTIFY, S.M.A.R.T
 * and some other commands. We have to ensure that the DMA cycle timing is
 * slower/equal than the fastest PIO timing.
 */

	if ((speed & XFER_MODE) != XFER_PIO) {
		ide_timing_compute(drive, ide_find_best_mode(drive, XFER_PIO | XFER_EPIO), &p, T, UT);
		ide_timing_merge(&p, t, t, IDE_TIMING_ALL);
	}

/*
 * Lenghten active & recovery time so that cycle time is correct.
 */

	if (t->act8b + t->rec8b < t->cyc8b) {
		t->act8b += (t->cyc8b - (t->act8b + t->rec8b)) / 2;
		t->rec8b = t->cyc8b - t->act8b;
	}

	if (t->active + t->recover < t->cycle) {
		t->active += (t->cycle - (t->active + t->recover)) / 2;
		t->recover = t->cycle - t->active;
	}

	return 0;
}

#endif
