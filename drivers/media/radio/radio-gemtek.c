// SPDX-License-Identifier: GPL-2.0-only
/*
 * GemTek radio card driver
 *
 * Copyright 1998 Jonas Munsin <jmunsin@iki.fi>
 *
 * GemTek hasn't released any specs on the card, so the protocol had to
 * be reverse engineered with dosemu.
 *
 * Besides the protocol changes, this is mostly a copy of:
 *
 *    RadioTrack II driver for Linux radio support (C) 1998 Ben Pfaff
 *
 *    Based on RadioTrack I/RadioReveal (C) 1997 M. Kirkwood
 *    Converted to new API by Alan Cox <alan@lxorguk.ukuu.org.uk>
 *    Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
 *
 * Converted to the radio-isa framework by Hans Verkuil <hans.verkuil@cisco.com>
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@kernel.org>
 *
 * Note: this card seems to swap the left and right audio channels!
 *
 * Fully tested with the Keene USB FM Transmitter and the v4l2-compliance tool.
 */

#include <linux/module.h>	/* Modules			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/mutex.h>
#include <linux/io.h>		/* outb, outb_p			*/
#include <linux/pnp.h>
#include <linux/slab.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include "radio-isa.h"

/*
 * Module info.
 */

MODULE_AUTHOR("Jonas Munsin, Pekka Seppänen <pexu@kapsi.fi>");
MODULE_DESCRIPTION("A driver for the GemTek Radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

/*
 * Module params.
 */

#ifndef CONFIG_RADIO_GEMTEK_PORT
#define CONFIG_RADIO_GEMTEK_PORT -1
#endif
#ifndef CONFIG_RADIO_GEMTEK_PROBE
#define CONFIG_RADIO_GEMTEK_PROBE 1
#endif

#define GEMTEK_MAX 4

static bool probe = CONFIG_RADIO_GEMTEK_PROBE;
static bool hardmute;
static int io[GEMTEK_MAX] = { [0] = CONFIG_RADIO_GEMTEK_PORT,
			      [1 ... (GEMTEK_MAX - 1)] = -1 };
static int radio_nr[GEMTEK_MAX]	= { [0 ... (GEMTEK_MAX - 1)] = -1 };

module_param(probe, bool, 0444);
MODULE_PARM_DESC(probe, "Enable automatic device probing.");

module_param(hardmute, bool, 0644);
MODULE_PARM_DESC(hardmute, "Enable 'hard muting' by shutting down PLL, may reduce static noise.");

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "Force I/O ports for the GemTek Radio card if automatic probing is disabled or fails. The most common I/O ports are: 0x20c 0x30c, 0x24c or 0x34c (0x20c, 0x248 and 0x28c have been reported to work for the combined sound/radiocard).");

module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

/*
 * Frequency calculation constants.  Intermediate frequency 10.52 MHz (nominal
 * value 10.7 MHz), reference divisor 6.39 kHz (nominal 6.25 kHz).
 */
#define FSCALE		8
#define IF_OFFSET	((unsigned int)(10.52 * 16000 * (1<<FSCALE)))
#define REF_FREQ	((unsigned int)(6.39 * 16 * (1<<FSCALE)))

#define GEMTEK_CK		0x01	/* Clock signal			*/
#define GEMTEK_DA		0x02	/* Serial data			*/
#define GEMTEK_CE		0x04	/* Chip enable			*/
#define GEMTEK_NS		0x08	/* No signal			*/
#define GEMTEK_MT		0x10	/* Line mute			*/
#define GEMTEK_STDF_3_125_KHZ	0x01	/* Standard frequency 3.125 kHz	*/
#define GEMTEK_PLL_OFF		0x07	/* PLL off			*/

#define BU2614_BUS_SIZE	32	/* BU2614 / BU2614FS bus size		*/

#define SHORT_DELAY 5		/* usec */
#define LONG_DELAY 75		/* usec */

struct gemtek {
	struct radio_isa_card isa;
	bool muted;
	u32 bu2614data;
};

#define BU2614_FREQ_BITS	16 /* D0..D15, Frequency data		*/
#define BU2614_PORT_BITS	3 /* P0..P2, Output port control data	*/
#define BU2614_VOID_BITS	4 /* unused				*/
#define BU2614_FMES_BITS	1 /* CT, Frequency measurement beginning data */
#define BU2614_STDF_BITS	3 /* R0..R2, Standard frequency data	*/
#define BU2614_SWIN_BITS	1 /* S, Switch between FMIN / AMIN	*/
#define BU2614_SWAL_BITS        1 /* PS, Swallow counter division (AMIN only)*/
#define BU2614_VOID2_BITS	1 /* unused				*/
#define BU2614_FMUN_BITS	1 /* GT, Frequency measurement time & unlock */
#define BU2614_TEST_BITS	1 /* TS, Test data is input		*/

#define BU2614_FREQ_SHIFT	0
#define BU2614_PORT_SHIFT	(BU2614_FREQ_BITS + BU2614_FREQ_SHIFT)
#define BU2614_VOID_SHIFT	(BU2614_PORT_BITS + BU2614_PORT_SHIFT)
#define BU2614_FMES_SHIFT	(BU2614_VOID_BITS + BU2614_VOID_SHIFT)
#define BU2614_STDF_SHIFT	(BU2614_FMES_BITS + BU2614_FMES_SHIFT)
#define BU2614_SWIN_SHIFT	(BU2614_STDF_BITS + BU2614_STDF_SHIFT)
#define BU2614_SWAL_SHIFT	(BU2614_SWIN_BITS + BU2614_SWIN_SHIFT)
#define BU2614_VOID2_SHIFT	(BU2614_SWAL_BITS + BU2614_SWAL_SHIFT)
#define BU2614_FMUN_SHIFT	(BU2614_VOID2_BITS + BU2614_VOID2_SHIFT)
#define BU2614_TEST_SHIFT	(BU2614_FMUN_BITS + BU2614_FMUN_SHIFT)

#define MKMASK(field)	(((1UL<<BU2614_##field##_BITS) - 1) << \
			BU2614_##field##_SHIFT)
#define BU2614_PORT_MASK	MKMASK(PORT)
#define BU2614_FREQ_MASK	MKMASK(FREQ)
#define BU2614_VOID_MASK	MKMASK(VOID)
#define BU2614_FMES_MASK	MKMASK(FMES)
#define BU2614_STDF_MASK	MKMASK(STDF)
#define BU2614_SWIN_MASK	MKMASK(SWIN)
#define BU2614_SWAL_MASK	MKMASK(SWAL)
#define BU2614_VOID2_MASK	MKMASK(VOID2)
#define BU2614_FMUN_MASK	MKMASK(FMUN)
#define BU2614_TEST_MASK	MKMASK(TEST)

/*
 * Set data which will be sent to BU2614FS.
 */
#define gemtek_bu2614_set(dev, field, data) ((dev)->bu2614data = \
	((dev)->bu2614data & ~field##_MASK) | ((data) << field##_SHIFT))

/*
 * Transmit settings to BU2614FS over GemTek IC.
 */
static void gemtek_bu2614_transmit(struct gemtek *gt)
{
	struct radio_isa_card *isa = &gt->isa;
	int i, bit, q, mute;

	mute = gt->muted ? GEMTEK_MT : 0x00;

	outb_p(mute | GEMTEK_CE | GEMTEK_DA | GEMTEK_CK, isa->io);
	udelay(LONG_DELAY);

	for (i = 0, q = gt->bu2614data; i < 32; i++, q >>= 1) {
		bit = (q & 1) ? GEMTEK_DA : 0;
		outb_p(mute | GEMTEK_CE | bit, isa->io);
		udelay(SHORT_DELAY);
		outb_p(mute | GEMTEK_CE | bit | GEMTEK_CK, isa->io);
		udelay(SHORT_DELAY);
	}

	outb_p(mute | GEMTEK_DA | GEMTEK_CK, isa->io);
	udelay(SHORT_DELAY);
}

/*
 * Calculate divisor from FM-frequency for BU2614FS (3.125 KHz STDF expected).
 */
static unsigned long gemtek_convfreq(unsigned long freq)
{
	return ((freq << FSCALE) + IF_OFFSET + REF_FREQ / 2) / REF_FREQ;
}

static struct radio_isa_card *gemtek_alloc(void)
{
	struct gemtek *gt = kzalloc(sizeof(*gt), GFP_KERNEL);

	if (gt)
		gt->muted = true;
	return gt ? &gt->isa : NULL;
}

/*
 * Set FM-frequency.
 */
static int gemtek_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	struct gemtek *gt = container_of(isa, struct gemtek, isa);

	if (hardmute && gt->muted)
		return 0;

	gemtek_bu2614_set(gt, BU2614_PORT, 0);
	gemtek_bu2614_set(gt, BU2614_FMES, 0);
	gemtek_bu2614_set(gt, BU2614_SWIN, 0);	/* FM-mode	*/
	gemtek_bu2614_set(gt, BU2614_SWAL, 0);
	gemtek_bu2614_set(gt, BU2614_FMUN, 1);	/* GT bit set	*/
	gemtek_bu2614_set(gt, BU2614_TEST, 0);
	gemtek_bu2614_set(gt, BU2614_STDF, GEMTEK_STDF_3_125_KHZ);
	gemtek_bu2614_set(gt, BU2614_FREQ, gemtek_convfreq(freq));
	gemtek_bu2614_transmit(gt);
	return 0;
}

/*
 * Set mute flag.
 */
static int gemtek_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	struct gemtek *gt = container_of(isa, struct gemtek, isa);
	int i;

	gt->muted = mute;
	if (hardmute) {
		if (!mute)
			return gemtek_s_frequency(isa, isa->freq);

		/* Turn off PLL, disable data output */
		gemtek_bu2614_set(gt, BU2614_PORT, 0);
		gemtek_bu2614_set(gt, BU2614_FMES, 0);	/* CT bit off	*/
		gemtek_bu2614_set(gt, BU2614_SWIN, 0);	/* FM-mode	*/
		gemtek_bu2614_set(gt, BU2614_SWAL, 0);
		gemtek_bu2614_set(gt, BU2614_FMUN, 0);	/* GT bit off	*/
		gemtek_bu2614_set(gt, BU2614_TEST, 0);
		gemtek_bu2614_set(gt, BU2614_STDF, GEMTEK_PLL_OFF);
		gemtek_bu2614_set(gt, BU2614_FREQ, 0);
		gemtek_bu2614_transmit(gt);
		return 0;
	}

	/* Read bus contents (CE, CK and DA). */
	i = inb_p(isa->io);
	/* Write it back with mute flag set. */
	outb_p((i >> 5) | (mute ? GEMTEK_MT : 0), isa->io);
	udelay(SHORT_DELAY);
	return 0;
}

static u32 gemtek_g_rxsubchans(struct radio_isa_card *isa)
{
	if (inb_p(isa->io) & GEMTEK_NS)
		return V4L2_TUNER_SUB_MONO;
	return V4L2_TUNER_SUB_STEREO;
}

/*
 * Check if requested card acts like GemTek Radio card.
 */
static bool gemtek_probe(struct radio_isa_card *isa, int io)
{
	int i, q;

	q = inb_p(io);	/* Read bus contents before probing. */
	/* Try to turn on CE, CK and DA respectively and check if card responds
	   properly. */
	for (i = 0; i < 3; ++i) {
		outb_p(1 << i, io);
		udelay(SHORT_DELAY);

		if ((inb_p(io) & ~GEMTEK_NS) != (0x17 | (1 << (i + 5))))
			return false;
	}
	outb_p(q >> 5, io);	/* Write bus contents back. */
	udelay(SHORT_DELAY);
	return true;
}

static const struct radio_isa_ops gemtek_ops = {
	.alloc = gemtek_alloc,
	.probe = gemtek_probe,
	.s_mute_volume = gemtek_s_mute_volume,
	.s_frequency = gemtek_s_frequency,
	.g_rxsubchans = gemtek_g_rxsubchans,
};

static const int gemtek_ioports[] = { 0x20c, 0x30c, 0x24c, 0x34c, 0x248, 0x28c };

#ifdef CONFIG_PNP
static const struct pnp_device_id gemtek_pnp_devices[] = {
	/* AOpen FX-3D/Pro Radio */
	{.id = "ADS7183", .driver_data = 0},
	{.id = ""}
};

MODULE_DEVICE_TABLE(pnp, gemtek_pnp_devices);
#endif

static struct radio_isa_driver gemtek_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-gemtek",
		},
	},
#ifdef CONFIG_PNP
	.pnp_driver = {
		.name		= "radio-gemtek",
		.id_table	= gemtek_pnp_devices,
		.probe		= radio_isa_pnp_probe,
		.remove		= radio_isa_pnp_remove,
	},
#endif
	.io_params = io,
	.radio_nr_params = radio_nr,
	.io_ports = gemtek_ioports,
	.num_of_io_ports = ARRAY_SIZE(gemtek_ioports),
	.region_size = 1,
	.card = "GemTek Radio",
	.ops = &gemtek_ops,
	.has_stereo = true,
};

static int __init gemtek_init(void)
{
	gemtek_driver.probe = probe;
#ifdef CONFIG_PNP
	pnp_register_driver(&gemtek_driver.pnp_driver);
#endif
	return isa_register_driver(&gemtek_driver.driver, GEMTEK_MAX);
}

static void __exit gemtek_exit(void)
{
	hardmute = true;	/* Turn off PLL */
#ifdef CONFIG_PNP
	pnp_unregister_driver(&gemtek_driver.pnp_driver);
#endif
	isa_unregister_driver(&gemtek_driver.driver);
}

module_init(gemtek_init);
module_exit(gemtek_exit);
