// SPDX-License-Identifier: GPL-2.0-only
/*
 * radio-aztech.c - Aztech radio card driver
 *
 * Converted to the radio-isa framework by Hans Verkuil <hans.verkuil@xs4all.nl>
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@kernel.org>
 * Adapted to support the Video for Linux API by
 * Russell Kroll <rkroll@exploits.org>.  Based on original tuner code by:
 *
 * Quay Ly
 * Donald Song
 * Jason Lewis      (jlewis@twilight.vtc.vsc.edu)
 * Scott McGrath    (smcgrath@twilight.vtc.vsc.edu)
 * William McGrath  (wmcgrath@twilight.vtc.vsc.edu)
 *
 * Fully tested with the Keene USB FM Transmitter and the v4l2-compliance tool.
*/

#include <linux/module.h>	/* Modules			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/io.h>		/* outb, outb_p			*/
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include "radio-isa.h"
#include "lm7000.h"

MODULE_AUTHOR("Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the Aztech radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

/* acceptable ports: 0x350 (JP3 shorted), 0x358 (JP3 open) */
#ifndef CONFIG_RADIO_AZTECH_PORT
#define CONFIG_RADIO_AZTECH_PORT -1
#endif

#define AZTECH_MAX 2

static int io[AZTECH_MAX] = { [0] = CONFIG_RADIO_AZTECH_PORT,
			      [1 ... (AZTECH_MAX - 1)] = -1 };
static int radio_nr[AZTECH_MAX]	= { [0 ... (AZTECH_MAX - 1)] = -1 };

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "I/O addresses of the Aztech card (0x350 or 0x358)");
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

struct aztech {
	struct radio_isa_card isa;
	int curvol;
};

/* bit definitions for register read */
#define AZTECH_BIT_NOT_TUNED	(1 << 0)
#define AZTECH_BIT_MONO		(1 << 1)
/* bit definitions for register write */
#define AZTECH_BIT_TUN_CE	(1 << 1)
#define AZTECH_BIT_TUN_CLK	(1 << 6)
#define AZTECH_BIT_TUN_DATA	(1 << 7)
/* bits 0 and 2 are volume control, bits 3..5 are not connected */

static void aztech_set_pins(void *handle, u8 pins)
{
	struct radio_isa_card *isa = handle;
	struct aztech *az = container_of(isa, struct aztech, isa);
	u8 bits = az->curvol;

	if (pins & LM7000_DATA)
		bits |= AZTECH_BIT_TUN_DATA;
	if (pins & LM7000_CLK)
		bits |= AZTECH_BIT_TUN_CLK;
	if (pins & LM7000_CE)
		bits |= AZTECH_BIT_TUN_CE;

	outb_p(bits, az->isa.io);
}

static struct radio_isa_card *aztech_alloc(void)
{
	struct aztech *az = kzalloc(sizeof(*az), GFP_KERNEL);

	return az ? &az->isa : NULL;
}

static int aztech_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	lm7000_set_freq(freq, isa, aztech_set_pins);

	return 0;
}

static u32 aztech_g_rxsubchans(struct radio_isa_card *isa)
{
	if (inb(isa->io) & AZTECH_BIT_MONO)
		return V4L2_TUNER_SUB_MONO;
	return V4L2_TUNER_SUB_STEREO;
}

static u32 aztech_g_signal(struct radio_isa_card *isa)
{
	return (inb(isa->io) & AZTECH_BIT_NOT_TUNED) ? 0 : 0xffff;
}

static int aztech_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	struct aztech *az = container_of(isa, struct aztech, isa);

	if (mute)
		vol = 0;
	az->curvol = (vol & 1) + ((vol & 2) << 1);
	outb(az->curvol, isa->io);
	return 0;
}

static const struct radio_isa_ops aztech_ops = {
	.alloc = aztech_alloc,
	.s_mute_volume = aztech_s_mute_volume,
	.s_frequency = aztech_s_frequency,
	.g_rxsubchans = aztech_g_rxsubchans,
	.g_signal = aztech_g_signal,
};

static const int aztech_ioports[] = { 0x350, 0x358 };

static struct radio_isa_driver aztech_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-aztech",
		},
	},
	.io_params = io,
	.radio_nr_params = radio_nr,
	.io_ports = aztech_ioports,
	.num_of_io_ports = ARRAY_SIZE(aztech_ioports),
	.region_size = 8,
	.card = "Aztech Radio",
	.ops = &aztech_ops,
	.has_stereo = true,
	.max_volume = 3,
};

static int __init aztech_init(void)
{
	return isa_register_driver(&aztech_driver.driver, AZTECH_MAX);
}

static void __exit aztech_exit(void)
{
	isa_unregister_driver(&aztech_driver.driver);
}

module_init(aztech_init);
module_exit(aztech_exit);
