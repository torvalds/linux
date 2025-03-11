// SPDX-License-Identifier: GPL-2.0-only
/*
 * RadioTrack II driver
 * Copyright 1998 Ben Pfaff
 *
 * Based on RadioTrack I/RadioReveal (C) 1997 M. Kirkwood
 * Converted to new API by Alan Cox <alan@lxorguk.ukuu.org.uk>
 * Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
 *
 * Converted to the radio-isa framework by Hans Verkuil <hansverk@cisco.com>
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@kernel.org>
 *
 * Fully tested with actual hardware and the v4l2-compliance tool.
 */

#include <linux/module.h>	/* Modules			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/mutex.h>
#include <linux/io.h>		/* outb, outb_p			*/
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include "radio-isa.h"

MODULE_AUTHOR("Ben Pfaff");
MODULE_DESCRIPTION("A driver for the RadioTrack II radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.99");

#ifndef CONFIG_RADIO_RTRACK2_PORT
#define CONFIG_RADIO_RTRACK2_PORT -1
#endif

#define RTRACK2_MAX 2

static int io[RTRACK2_MAX] = { [0] = CONFIG_RADIO_RTRACK2_PORT,
			      [1 ... (RTRACK2_MAX - 1)] = -1 };
static int radio_nr[RTRACK2_MAX] = { [0 ... (RTRACK2_MAX - 1)] = -1 };

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "I/O addresses of the RadioTrack card (0x20f or 0x30f)");
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

static struct radio_isa_card *rtrack2_alloc(void)
{
	return kzalloc(sizeof(struct radio_isa_card), GFP_KERNEL);
}

static void zero(struct radio_isa_card *isa)
{
	outb_p(1, isa->io);
	outb_p(3, isa->io);
	outb_p(1, isa->io);
}

static void one(struct radio_isa_card *isa)
{
	outb_p(5, isa->io);
	outb_p(7, isa->io);
	outb_p(5, isa->io);
}

static int rtrack2_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	int i;

	freq = freq / 200 + 856;

	outb_p(0xc8, isa->io);
	outb_p(0xc9, isa->io);
	outb_p(0xc9, isa->io);

	for (i = 0; i < 10; i++)
		zero(isa);

	for (i = 14; i >= 0; i--)
		if (freq & (1 << i))
			one(isa);
		else
			zero(isa);

	outb_p(0xc8, isa->io);
	outb_p(v4l2_ctrl_g_ctrl(isa->mute), isa->io);
	return 0;
}

static u32 rtrack2_g_signal(struct radio_isa_card *isa)
{
	/* bit set = no signal present	*/
	return (inb(isa->io) & 2) ? 0 : 0xffff;
}

static int rtrack2_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	outb(mute, isa->io);
	return 0;
}

static const struct radio_isa_ops rtrack2_ops = {
	.alloc = rtrack2_alloc,
	.s_mute_volume = rtrack2_s_mute_volume,
	.s_frequency = rtrack2_s_frequency,
	.g_signal = rtrack2_g_signal,
};

static const int rtrack2_ioports[] = { 0x20f, 0x30f };

static struct radio_isa_driver rtrack2_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-rtrack2",
		},
	},
	.io_params = io,
	.radio_nr_params = radio_nr,
	.io_ports = rtrack2_ioports,
	.num_of_io_ports = ARRAY_SIZE(rtrack2_ioports),
	.region_size = 4,
	.card = "AIMSlab RadioTrack II",
	.ops = &rtrack2_ops,
	.has_stereo = true,
};

static int __init rtrack2_init(void)
{
	return isa_register_driver(&rtrack2_driver.driver, RTRACK2_MAX);
}

static void __exit rtrack2_exit(void)
{
	isa_unregister_driver(&rtrack2_driver.driver);
}

module_init(rtrack2_init);
module_exit(rtrack2_exit);
