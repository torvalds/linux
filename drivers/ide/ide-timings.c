// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *  Copyright (c) 2007-2008 Bartlomiej Zolnierkiewicz
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/ide.h>
#include <linux/module.h>

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

	{ XFER_MW_DMA_4,  25,   0,   0,   0,  55,  20,  80,   0 },
	{ XFER_MW_DMA_3,  25,   0,   0,   0,  65,  25, 100,   0 },
	{ XFER_MW_DMA_2,  25,   0,   0,   0,  70,  25, 120,   0 },
	{ XFER_MW_DMA_1,  45,   0,   0,   0,  80,  50, 150,   0 },
	{ XFER_MW_DMA_0,  60,   0,   0,   0, 215, 215, 480,   0 },

	{ XFER_SW_DMA_2,  60,   0,   0,   0, 120, 120, 240,   0 },
	{ XFER_SW_DMA_1,  90,   0,   0,   0, 240, 240, 480,   0 },
	{ XFER_SW_DMA_0, 120,   0,   0,   0, 480, 480, 960,   0 },

	{ XFER_PIO_6,     10,  55,  20,  80,  55,  20,  80,   0 },
	{ XFER_PIO_5,     15,  65,  25, 100,  65,  25, 100,   0 },
	{ XFER_PIO_4,     25,  70,  25, 120,  70,  25, 120,   0 },
	{ XFER_PIO_3,     30,  80,  70, 180,  80,  70, 180,   0 },

	{ XFER_PIO_2,     30, 290,  40, 330, 100,  90, 240,   0 },
	{ XFER_PIO_1,     50, 290,  93, 383, 125, 100, 383,   0 },
	{ XFER_PIO_0,     70, 290, 240, 600, 165, 150, 600,   0 },

	{ XFER_PIO_SLOW, 120, 290, 240, 960, 290, 240, 960,   0 },

	{ 0xff }
};

struct ide_timing *ide_timing_find_mode(u8 speed)
{
	struct ide_timing *t;

	for (t = ide_timing; t->mode != speed; t++)
		if (t->mode == 0xff)
			return NULL;
	return t;
}
EXPORT_SYMBOL_GPL(ide_timing_find_mode);

u16 ide_pio_cycle_time(ide_drive_t *drive, u8 pio)
{
	u16 *id = drive->id;
	struct ide_timing *t = ide_timing_find_mode(XFER_PIO_0 + pio);
	u16 cycle = 0;

	if (id[ATA_ID_FIELD_VALID] & 2) {
		if (ata_id_has_iordy(drive->id))
			cycle = id[ATA_ID_EIDE_PIO_IORDY];
		else
			cycle = id[ATA_ID_EIDE_PIO];

		/* conservative "downgrade" for all pre-ATA2 drives */
		if (pio < 3 && cycle < t->cycle)
			cycle = 0; /* use standard timing */

		/* Use the standard timing for the CF specific modes too */
		if (pio > 4 && ata_id_is_cfa(id))
			cycle = 0;
	}

	return cycle ? cycle : t->cycle;
}
EXPORT_SYMBOL_GPL(ide_pio_cycle_time);

#define ENOUGH(v, unit)		(((v) - 1) / (unit) + 1)
#define EZ(v, unit)		((v) ? ENOUGH((v) * 1000, unit) : 0)

static void ide_timing_quantize(struct ide_timing *t, struct ide_timing *q,
				int T, int UT)
{
	q->setup   = EZ(t->setup,   T);
	q->act8b   = EZ(t->act8b,   T);
	q->rec8b   = EZ(t->rec8b,   T);
	q->cyc8b   = EZ(t->cyc8b,   T);
	q->active  = EZ(t->active,  T);
	q->recover = EZ(t->recover, T);
	q->cycle   = EZ(t->cycle,   T);
	q->udma    = EZ(t->udma,    UT);
}

void ide_timing_merge(struct ide_timing *a, struct ide_timing *b,
		      struct ide_timing *m, unsigned int what)
{
	if (what & IDE_TIMING_SETUP)
		m->setup   = max(a->setup,   b->setup);
	if (what & IDE_TIMING_ACT8B)
		m->act8b   = max(a->act8b,   b->act8b);
	if (what & IDE_TIMING_REC8B)
		m->rec8b   = max(a->rec8b,   b->rec8b);
	if (what & IDE_TIMING_CYC8B)
		m->cyc8b   = max(a->cyc8b,   b->cyc8b);
	if (what & IDE_TIMING_ACTIVE)
		m->active  = max(a->active,  b->active);
	if (what & IDE_TIMING_RECOVER)
		m->recover = max(a->recover, b->recover);
	if (what & IDE_TIMING_CYCLE)
		m->cycle   = max(a->cycle,   b->cycle);
	if (what & IDE_TIMING_UDMA)
		m->udma    = max(a->udma,    b->udma);
}
EXPORT_SYMBOL_GPL(ide_timing_merge);

int ide_timing_compute(ide_drive_t *drive, u8 speed,
		       struct ide_timing *t, int T, int UT)
{
	u16 *id = drive->id;
	struct ide_timing *s, p;

	/*
	 * Find the mode.
	 */
	s = ide_timing_find_mode(speed);
	if (s == NULL)
		return -EINVAL;

	/*
	 * Copy the timing from the table.
	 */
	*t = *s;

	/*
	 * If the drive is an EIDE drive, it can tell us it needs extended
	 * PIO/MWDMA cycle timing.
	 */
	if (id[ATA_ID_FIELD_VALID] & 2) {	/* EIDE drive */
		memset(&p, 0, sizeof(p));

		if (speed >= XFER_PIO_0 && speed < XFER_SW_DMA_0) {
			if (speed <= XFER_PIO_2)
				p.cycle = p.cyc8b = id[ATA_ID_EIDE_PIO];
			else if ((speed <= XFER_PIO_4) ||
				 (speed == XFER_PIO_5 && !ata_id_is_cfa(id)))
				p.cycle = p.cyc8b = id[ATA_ID_EIDE_PIO_IORDY];
		} else if (speed >= XFER_MW_DMA_0 && speed <= XFER_MW_DMA_2)
			p.cycle = id[ATA_ID_EIDE_DMA_MIN];

		ide_timing_merge(&p, t, t, IDE_TIMING_CYCLE | IDE_TIMING_CYC8B);
	}

	/*
	 * Convert the timing to bus clock counts.
	 */
	ide_timing_quantize(t, t, T, UT);

	/*
	 * Even in DMA/UDMA modes we still use PIO access for IDENTIFY,
	 * S.M.A.R.T and some other commands. We have to ensure that the
	 * DMA cycle timing is slower/equal than the current PIO timing.
	 */
	if (speed >= XFER_SW_DMA_0) {
		ide_timing_compute(drive, drive->pio_mode, &p, T, UT);
		ide_timing_merge(&p, t, t, IDE_TIMING_ALL);
	}

	/*
	 * Lengthen active & recovery time so that cycle time is correct.
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
EXPORT_SYMBOL_GPL(ide_timing_compute);
