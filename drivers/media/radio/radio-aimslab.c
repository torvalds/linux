/*
 * AimsLab RadioTrack (aka RadioVeveal) driver
 *
 * Copyright 1997 M. Kirkwood
 *
 * Converted to the radio-isa framework by Hans Verkuil <hans.verkuil@cisco.com>
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 * Converted to new API by Alan Cox <alan@lxorguk.ukuu.org.uk>
 * Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
 *
 * Notes on the hardware (reverse engineered from other peoples'
 * reverse engineering of AIMS' code :-)
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *
 *  The signal strength query is unsurprisingly inaccurate.  And it seems
 *  to indicate that (on my card, at least) the frequency setting isn't
 *  too great.  (I have to tune up .025MHz from what the freq should be
 *  to get a report that the thing is tuned.)
 *
 *  Volume control is (ugh) analogue:
 *   out(port, start_increasing_volume);
 *   wait(a_wee_while);
 *   out(port, stop_changing_the_volume);
 *
 * Fully tested with the Keene USB FM Transmitter and the v4l2-compliance tool.
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* msleep			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/io.h>		/* outb, outb_p			*/
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include "radio-isa.h"

MODULE_AUTHOR("M. Kirkwood");
MODULE_DESCRIPTION("A driver for the RadioTrack/RadioReveal radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

#ifndef CONFIG_RADIO_RTRACK_PORT
#define CONFIG_RADIO_RTRACK_PORT -1
#endif

#define RTRACK_MAX 2

static int io[RTRACK_MAX] = { [0] = CONFIG_RADIO_RTRACK_PORT,
			      [1 ... (RTRACK_MAX - 1)] = -1 };
static int radio_nr[RTRACK_MAX]	= { [0 ... (RTRACK_MAX - 1)] = -1 };

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "I/O addresses of the RadioTrack card (0x20f or 0x30f)");
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

struct rtrack {
	struct radio_isa_card isa;
	int curvol;
};

static struct radio_isa_card *rtrack_alloc(void)
{
	struct rtrack *rt = kzalloc(sizeof(struct rtrack), GFP_KERNEL);

	if (rt)
		rt->curvol = 0xff;
	return rt ? &rt->isa : NULL;
}

/* The 128+64 on these outb's is to keep the volume stable while tuning.
 * Without them, the volume _will_ creep up with each frequency change
 * and bit 4 (+16) is to keep the signal strength meter enabled.
 */

static void send_0_byte(struct radio_isa_card *isa, int on)
{
	outb_p(128+64+16+on+1, isa->io);	/* wr-enable + data low */
	outb_p(128+64+16+on+2+1, isa->io);	/* clock */
	msleep(1);
}

static void send_1_byte(struct radio_isa_card *isa, int on)
{
	outb_p(128+64+16+on+4+1, isa->io);	/* wr-enable+data high */
	outb_p(128+64+16+on+4+2+1, isa->io);	/* clock */
	msleep(1);
}

static int rtrack_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	int on = v4l2_ctrl_g_ctrl(isa->mute) ? 0 : 8;
	int i;

	freq += 171200;			/* Add 10.7 MHz IF 		*/
	freq /= 800;			/* Convert to 50 kHz units	*/

	send_0_byte(isa, on);		/*  0: LSB of frequency		*/

	for (i = 0; i < 13; i++)	/*   : frequency bits (1-13)	*/
		if (freq & (1 << i))
			send_1_byte(isa, on);
		else
			send_0_byte(isa, on);

	send_0_byte(isa, on);		/* 14: test bit - always 0    */
	send_0_byte(isa, on);		/* 15: test bit - always 0    */

	send_0_byte(isa, on);		/* 16: band data 0 - always 0 */
	send_0_byte(isa, on);		/* 17: band data 1 - always 0 */
	send_0_byte(isa, on);		/* 18: band data 2 - always 0 */
	send_0_byte(isa, on);		/* 19: time base - always 0   */

	send_0_byte(isa, on);		/* 20: spacing (0 = 25 kHz)   */
	send_1_byte(isa, on);		/* 21: spacing (1 = 25 kHz)   */
	send_0_byte(isa, on);		/* 22: spacing (0 = 25 kHz)   */
	send_1_byte(isa, on);		/* 23: AM/FM (FM = 1, always) */

	outb(0xd0 + on, isa->io);	/* volume steady + sigstr */
	return 0;
}

static u32 rtrack_g_signal(struct radio_isa_card *isa)
{
	/* bit set = no signal present */
	return 0xffff * !(inb(isa->io) & 2);
}

static int rtrack_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	struct rtrack *rt = container_of(isa, struct rtrack, isa);
	int curvol = rt->curvol;

	if (mute) {
		outb(0xd0, isa->io);	/* volume steady + sigstr + off	*/
		return 0;
	}
	if (vol == 0) {			/* volume = 0 means mute the card */
		outb(0x48, isa->io);	/* volume down but still "on"	*/
		msleep(curvol * 3);	/* make sure it's totally down	*/
	} else if (curvol < vol) {
		outb(0x98, isa->io);	/* volume up + sigstr + on	*/
		for (; curvol < vol; curvol++)
			udelay(3000);
	} else if (curvol > vol) {
		outb(0x58, isa->io);	/* volume down + sigstr + on	*/
		for (; curvol > vol; curvol--)
			udelay(3000);
	}
	outb(0xd8, isa->io);		/* volume steady + sigstr + on	*/
	rt->curvol = vol;
	return 0;
}

/* Mute card - prevents noisy bootups */
static int rtrack_initialize(struct radio_isa_card *isa)
{
	/* this ensures that the volume is all the way up  */
	outb(0x90, isa->io);	/* volume up but still "on"	*/
	msleep(3000);		/* make sure it's totally up	*/
	outb(0xc0, isa->io);	/* steady volume, mute card	*/
	return 0;
}

static const struct radio_isa_ops rtrack_ops = {
	.alloc = rtrack_alloc,
	.init = rtrack_initialize,
	.s_mute_volume = rtrack_s_mute_volume,
	.s_frequency = rtrack_s_frequency,
	.g_signal = rtrack_g_signal,
};

static const int rtrack_ioports[] = { 0x20f, 0x30f };

static struct radio_isa_driver rtrack_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-aimslab",
		},
	},
	.io_params = io,
	.radio_nr_params = radio_nr,
	.io_ports = rtrack_ioports,
	.num_of_io_ports = ARRAY_SIZE(rtrack_ioports),
	.region_size = 2,
	.card = "AIMSlab RadioTrack/RadioReveal",
	.ops = &rtrack_ops,
	.has_stereo = true,
	.max_volume = 0xff,
};

static int __init rtrack_init(void)
{
	return isa_register_driver(&rtrack_driver.driver, RTRACK_MAX);
}

static void __exit rtrack_exit(void)
{
	isa_unregister_driver(&rtrack_driver.driver);
}

module_init(rtrack_init);
module_exit(rtrack_exit);
