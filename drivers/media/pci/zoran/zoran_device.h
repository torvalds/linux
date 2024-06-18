/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * This part handles card-specific data and detection
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 */

#ifndef __ZORAN_DEVICE_H__
#define __ZORAN_DEVICE_H__

/* general purpose I/O */
void GPIO(struct zoran *zr, int bit, unsigned int value);

/* codec (or actually: guest bus) access */
int post_office_wait(struct zoran *zr);
int post_office_write(struct zoran *zr, unsigned int guest, unsigned int reg,
		      unsigned int value);
int post_office_read(struct zoran *zr, unsigned int guest, unsigned int reg);

void jpeg_codec_sleep(struct zoran *zr, int sleep);
int jpeg_codec_reset(struct zoran *zr);

/* zr360x7 access to raw capture */
void zr36057_set_memgrab(struct zoran *zr, int mode);
int wait_grab_pending(struct zoran *zr);

/* interrupts */
void print_interrupts(struct zoran *zr);
void clear_interrupt_counters(struct zoran *zr);
irqreturn_t zoran_irq(int irq, void *dev_id);

/* JPEG codec access */
void jpeg_start(struct zoran *zr);
void zr36057_enable_jpg(struct zoran *zr, enum zoran_codec_mode mode);
void zoran_feed_stat_com(struct zoran *zr);

/* general */
void zoran_set_pci_master(struct zoran *zr, int set_master);
void zoran_init_hardware(struct zoran *zr);
void zr36057_restart(struct zoran *zr);

extern const struct zoran_format zoran_formats[];

extern int pass_through;

/* i2c */
#define decoder_call(zr, o, f, args...) \
	v4l2_subdev_call((zr)->decoder, o, f, ##args)
#define encoder_call(zr, o, f, args...) \
	v4l2_subdev_call((zr)->encoder, o, f, ##args)

#endif				/* __ZORAN_DEVICE_H__ */
