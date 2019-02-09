/* Typhoon Radio Card driver for radio support
 * (c) 1999 Dr. Henrik Seidel <Henrik.Seidel@gmx.de>
 *
 * Notes on the hardware
 *
 * This card has two output sockets, one for speakers and one for line.
 * The speaker output has volume control, but only in four discrete
 * steps. The line output has neither volume control nor mute.
 *
 * The card has auto-stereo according to its manual, although it all
 * sounds mono to me (even with the Win/DOS drivers). Maybe it's my
 * antenna - I really don't know for sure.
 *
 * Frequency control is done digitally.
 *
 * Volume control is done digitally, but there are only four different
 * possible values. So you should better always turn the volume up and
 * use line control. I got the best results by connecting line output
 * to the sound card microphone input. For such a configuration the
 * volume control has no effect, since volume control only influences
 * the speaker output.
 *
 * There is no explicit mute/unmute. So I set the radio frequency to a
 * value where I do expect just noise and turn the speaker volume down.
 * The frequency change is necessary since the card never seems to be
 * completely silent.
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@kernel.org>
 */

#include <linux/module.h>	/* Modules                        */
#include <linux/init.h>		/* Initdata                       */
#include <linux/ioport.h>	/* request_region		  */
#include <linux/videodev2.h>	/* kernel radio structs           */
#include <linux/io.h>		/* outb, outb_p                   */
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include "radio-isa.h"

#define DRIVER_VERSION "0.1.2"

MODULE_AUTHOR("Dr. Henrik Seidel");
MODULE_DESCRIPTION("A driver for the Typhoon radio card (a.k.a. EcoRadio).");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.99");

#ifndef CONFIG_RADIO_TYPHOON_PORT
#define CONFIG_RADIO_TYPHOON_PORT -1
#endif

#ifndef CONFIG_RADIO_TYPHOON_MUTEFREQ
#define CONFIG_RADIO_TYPHOON_MUTEFREQ 87000
#endif

#define TYPHOON_MAX 2

static int io[TYPHOON_MAX] = { [0] = CONFIG_RADIO_TYPHOON_PORT,
			      [1 ... (TYPHOON_MAX - 1)] = -1 };
static int radio_nr[TYPHOON_MAX]	= { [0 ... (TYPHOON_MAX - 1)] = -1 };
static unsigned long mutefreq = CONFIG_RADIO_TYPHOON_MUTEFREQ;

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "I/O addresses of the Typhoon card (0x316 or 0x336)");
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");
module_param(mutefreq, ulong, 0);
MODULE_PARM_DESC(mutefreq, "Frequency used when muting the card (in kHz)");

struct typhoon {
	struct radio_isa_card isa;
	int muted;
};

static struct radio_isa_card *typhoon_alloc(void)
{
	struct typhoon *ty = kzalloc(sizeof(*ty), GFP_KERNEL);

	return ty ? &ty->isa : NULL;
}

static int typhoon_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	unsigned long outval;
	unsigned long x;

	/*
	 * The frequency transfer curve is not linear. The best fit I could
	 * get is
	 *
	 * outval = -155 + exp((f + 15.55) * 0.057))
	 *
	 * where frequency f is in MHz. Since we don't have exp in the kernel,
	 * I approximate this function by a third order polynomial.
	 *
	 */

	x = freq / 160;
	outval = (x * x + 2500) / 5000;
	outval = (outval * x + 5000) / 10000;
	outval -= (10 * x * x + 10433) / 20866;
	outval += 4 * x - 11505;

	outb_p((outval >> 8) & 0x01, isa->io + 4);
	outb_p(outval >> 9, isa->io + 6);
	outb_p(outval & 0xff, isa->io + 8);
	return 0;
}

static int typhoon_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	struct typhoon *ty = container_of(isa, struct typhoon, isa);

	if (mute)
		vol = 0;
	vol >>= 14;			/* Map 16 bit to 2 bit */
	vol &= 3;
	outb_p(vol / 2, isa->io);	/* Set the volume, high bit. */
	outb_p(vol % 2, isa->io + 2);	/* Set the volume, low bit. */

	if (vol == 0 && !ty->muted) {
		ty->muted = true;
		return typhoon_s_frequency(isa, mutefreq << 4);
	}
	if (vol && ty->muted) {
		ty->muted = false;
		return typhoon_s_frequency(isa, isa->freq);
	}
	return 0;
}

static const struct radio_isa_ops typhoon_ops = {
	.alloc = typhoon_alloc,
	.s_mute_volume = typhoon_s_mute_volume,
	.s_frequency = typhoon_s_frequency,
};

static const int typhoon_ioports[] = { 0x316, 0x336 };

static struct radio_isa_driver typhoon_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-typhoon",
		},
	},
	.io_params = io,
	.radio_nr_params = radio_nr,
	.io_ports = typhoon_ioports,
	.num_of_io_ports = ARRAY_SIZE(typhoon_ioports),
	.region_size = 8,
	.card = "Typhoon Radio",
	.ops = &typhoon_ops,
	.has_stereo = true,
	.max_volume = 3,
};

static int __init typhoon_init(void)
{
	if (mutefreq < 87000 || mutefreq > 108000) {
		printk(KERN_ERR "%s: You must set a frequency (in kHz) used when muting the card,\n",
				typhoon_driver.driver.driver.name);
		printk(KERN_ERR "%s: e.g. with \"mutefreq=87500\" (87000 <= mutefreq <= 108000)\n",
				typhoon_driver.driver.driver.name);
		return -ENODEV;
	}
	return isa_register_driver(&typhoon_driver.driver, TYPHOON_MAX);
}

static void __exit typhoon_exit(void)
{
	isa_unregister_driver(&typhoon_driver.driver);
}


module_init(typhoon_init);
module_exit(typhoon_exit);

