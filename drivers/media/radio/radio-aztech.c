/*
 * radio-aztech.c - Aztech radio card driver
 *
 * Converted to the radio-isa framework by Hans Verkuil <hans.verkuil@xs4all.nl>
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
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

#include <linux/module.h>	/* Modules 			*/
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
static const int radio_wait_time = 1000;

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "I/O addresses of the Aztech card (0x350 or 0x358)");
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

struct aztech {
	struct radio_isa_card isa;
	int curvol;
};

static void send_0_byte(struct aztech *az)
{
	udelay(radio_wait_time);
	outb_p(2 + az->curvol, az->isa.io);
	outb_p(64 + 2 + az->curvol, az->isa.io);
}

static void send_1_byte(struct aztech *az)
{
	udelay(radio_wait_time);
	outb_p(128 + 2 + az->curvol, az->isa.io);
	outb_p(128 + 64 + 2 + az->curvol, az->isa.io);
}

static struct radio_isa_card *aztech_alloc(void)
{
	struct aztech *az = kzalloc(sizeof(*az), GFP_KERNEL);

	return az ? &az->isa : NULL;
}

static int aztech_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	struct aztech *az = container_of(isa, struct aztech, isa);
	int  i;

	freq += 171200;			/* Add 10.7 MHz IF		*/
	freq /= 800;			/* Convert to 50 kHz units	*/

	send_0_byte(az);		/*  0: LSB of frequency       */

	for (i = 0; i < 13; i++)	/*   : frequency bits (1-13)  */
		if (freq & (1 << i))
			send_1_byte(az);
		else
			send_0_byte(az);

	send_0_byte(az);		/* 14: test bit - always 0    */
	send_0_byte(az);		/* 15: test bit - always 0    */
	send_0_byte(az);		/* 16: band data 0 - always 0 */
	if (isa->stereo)		/* 17: stereo (1 to enable)   */
		send_1_byte(az);
	else
		send_0_byte(az);

	send_1_byte(az);		/* 18: band data 1 - unknown  */
	send_0_byte(az);		/* 19: time base - always 0   */
	send_0_byte(az);		/* 20: spacing (0 = 25 kHz)   */
	send_1_byte(az);		/* 21: spacing (1 = 25 kHz)   */
	send_0_byte(az);		/* 22: spacing (0 = 25 kHz)   */
	send_1_byte(az);		/* 23: AM/FM (FM = 1, always) */

	/* latch frequency */

	udelay(radio_wait_time);
	outb_p(128 + 64 + az->curvol, az->isa.io);

	return 0;
}

/* thanks to Michael Dwyer for giving me a dose of clues in
 * the signal strength department..
 *
 * This card has a stereo bit - bit 0 set = mono, not set = stereo
 */
static u32 aztech_g_rxsubchans(struct radio_isa_card *isa)
{
	if (inb(isa->io) & 1)
		return V4L2_TUNER_SUB_MONO;
	return V4L2_TUNER_SUB_STEREO;
}

static int aztech_s_stereo(struct radio_isa_card *isa, bool stereo)
{
	return aztech_s_frequency(isa, isa->freq);
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
	.s_stereo = aztech_s_stereo,
	.g_rxsubchans = aztech_g_rxsubchans,
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
	.region_size = 2,
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
