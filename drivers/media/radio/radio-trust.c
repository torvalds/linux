/* radio-trust.c - Trust FM Radio card driver for Linux 2.2
 * by Eric Lammerts <eric@scintilla.utwente.nl>
 *
 * Based on radio-aztech.c. Original notes:
 *
 * Adapted to support the Video for Linux API by
 * Russell Kroll <rkroll@exploits.org>.  Based on original tuner code by:
 *
 * Quay Ly
 * Donald Song
 * Jason Lewis      (jlewis@twilight.vtc.vsc.edu)
 * Scott McGrath    (smcgrath@twilight.vtc.vsc.edu)
 * William McGrath  (wmcgrath@twilight.vtc.vsc.edu)
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@kernel.org>
 */

#include <stdarg.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include "radio-isa.h"

MODULE_AUTHOR("Eric Lammerts, Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the Trust FM Radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.99");

/* acceptable ports: 0x350 (JP3 shorted), 0x358 (JP3 open) */

#ifndef CONFIG_RADIO_TRUST_PORT
#define CONFIG_RADIO_TRUST_PORT -1
#endif

#define TRUST_MAX 2

static int io[TRUST_MAX] = { [0] = CONFIG_RADIO_TRUST_PORT,
			      [1 ... (TRUST_MAX - 1)] = -1 };
static int radio_nr[TRUST_MAX] = { [0 ... (TRUST_MAX - 1)] = -1 };

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "I/O addresses of the Trust FM Radio card (0x350 or 0x358)");
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

struct trust {
	struct radio_isa_card isa;
	int ioval;
};

static struct radio_isa_card *trust_alloc(void)
{
	struct trust *tr = kzalloc(sizeof(*tr), GFP_KERNEL);

	return tr ? &tr->isa : NULL;
}

/* i2c addresses */
#define TDA7318_ADDR 0x88
#define TSA6060T_ADDR 0xc4

#define TR_DELAY do { inb(tr->isa.io); inb(tr->isa.io); inb(tr->isa.io); } while (0)
#define TR_SET_SCL outb(tr->ioval |= 2, tr->isa.io)
#define TR_CLR_SCL outb(tr->ioval &= 0xfd, tr->isa.io)
#define TR_SET_SDA outb(tr->ioval |= 1, tr->isa.io)
#define TR_CLR_SDA outb(tr->ioval &= 0xfe, tr->isa.io)

static void write_i2c(struct trust *tr, int n, ...)
{
	unsigned char val, mask;
	va_list args;

	va_start(args, n);

	/* start condition */
	TR_SET_SDA;
	TR_SET_SCL;
	TR_DELAY;
	TR_CLR_SDA;
	TR_CLR_SCL;
	TR_DELAY;

	for (; n; n--) {
		val = va_arg(args, unsigned);
		for (mask = 0x80; mask; mask >>= 1) {
			if (val & mask)
				TR_SET_SDA;
			else
				TR_CLR_SDA;
			TR_SET_SCL;
			TR_DELAY;
			TR_CLR_SCL;
			TR_DELAY;
		}
		/* acknowledge bit */
		TR_SET_SDA;
		TR_SET_SCL;
		TR_DELAY;
		TR_CLR_SCL;
		TR_DELAY;
	}

	/* stop condition */
	TR_CLR_SDA;
	TR_DELAY;
	TR_SET_SCL;
	TR_DELAY;
	TR_SET_SDA;
	TR_DELAY;

	va_end(args);
}

static int trust_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	struct trust *tr = container_of(isa, struct trust, isa);

	tr->ioval = (tr->ioval & 0xf7) | (mute << 3);
	outb(tr->ioval, isa->io);
	write_i2c(tr, 2, TDA7318_ADDR, vol ^ 0x1f);
	return 0;
}

static int trust_s_stereo(struct radio_isa_card *isa, bool stereo)
{
	struct trust *tr = container_of(isa, struct trust, isa);

	tr->ioval = (tr->ioval & 0xfb) | (!stereo << 2);
	outb(tr->ioval, isa->io);
	return 0;
}

static u32 trust_g_signal(struct radio_isa_card *isa)
{
	int i, v;

	for (i = 0, v = 0; i < 100; i++)
		v |= inb(isa->io);
	return (v & 1) ? 0 : 0xffff;
}

static int trust_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	struct trust *tr = container_of(isa, struct trust, isa);

	freq /= 160;	/* Convert to 10 kHz units	*/
	freq += 1070;	/* Add 10.7 MHz IF		*/
	write_i2c(tr, 5, TSA6060T_ADDR, (freq << 1) | 1,
			freq >> 7, 0x60 | ((freq >> 15) & 1), 0);
	return 0;
}

static int basstreble2chip[15] = {
	0, 1, 2, 3, 4, 5, 6, 7, 14, 13, 12, 11, 10, 9, 8
};

static int trust_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct radio_isa_card *isa =
		container_of(ctrl->handler, struct radio_isa_card, hdl);
	struct trust *tr = container_of(isa, struct trust, isa);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_BASS:
		write_i2c(tr, 2, TDA7318_ADDR, 0x60 | basstreble2chip[ctrl->val]);
		return 0;
	case V4L2_CID_AUDIO_TREBLE:
		write_i2c(tr, 2, TDA7318_ADDR, 0x70 | basstreble2chip[ctrl->val]);
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops trust_ctrl_ops = {
	.s_ctrl = trust_s_ctrl,
};

static int trust_initialize(struct radio_isa_card *isa)
{
	struct trust *tr = container_of(isa, struct trust, isa);

	tr->ioval = 0xf;
	write_i2c(tr, 2, TDA7318_ADDR, 0x80);	/* speaker att. LF = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0xa0);	/* speaker att. RF = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0xc0);	/* speaker att. LR = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0xe0);	/* speaker att. RR = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0x40);	/* stereo 1 input, gain = 18.75 dB */

	v4l2_ctrl_new_std(&isa->hdl, &trust_ctrl_ops,
				V4L2_CID_AUDIO_BASS, 0, 15, 1, 8);
	v4l2_ctrl_new_std(&isa->hdl, &trust_ctrl_ops,
				V4L2_CID_AUDIO_TREBLE, 0, 15, 1, 8);
	return isa->hdl.error;
}

static const struct radio_isa_ops trust_ops = {
	.init = trust_initialize,
	.alloc = trust_alloc,
	.s_mute_volume = trust_s_mute_volume,
	.s_frequency = trust_s_frequency,
	.s_stereo = trust_s_stereo,
	.g_signal = trust_g_signal,
};

static const int trust_ioports[] = { 0x350, 0x358 };

static struct radio_isa_driver trust_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-trust",
		},
	},
	.io_params = io,
	.radio_nr_params = radio_nr,
	.io_ports = trust_ioports,
	.num_of_io_ports = ARRAY_SIZE(trust_ioports),
	.region_size = 2,
	.card = "Trust FM Radio",
	.ops = &trust_ops,
	.has_stereo = true,
	.max_volume = 31,
};

static int __init trust_init(void)
{
	return isa_register_driver(&trust_driver.driver, TRUST_MAX);
}

static void __exit trust_exit(void)
{
	isa_unregister_driver(&trust_driver.driver);
}

module_init(trust_init);
module_exit(trust_exit);
