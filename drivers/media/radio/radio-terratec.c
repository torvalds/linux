/* Terratec ActiveRadio ISA Standalone card driver for Linux radio support
 * (c) 1999 R. Offermanns (rolf@offermanns.de)
 * based on the aimslab radio driver from M. Kirkwood
 * many thanks to Michael Becker and Friedhelm Birth (from TerraTec)
 *
 *
 * History:
 * 1999-05-21	First preview release
 *
 *  Notes on the hardware:
 *  There are two "main" chips on the card:
 *  - Philips OM5610 (http://www-us.semiconductors.philips.com/acrobat/datasheets/OM5610_2.pdf)
 *  - Philips SAA6588 (http://www-us.semiconductors.philips.com/acrobat/datasheets/SAA6588_1.pdf)
 *  (you can get the datasheet at the above links)
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *  Volume Control is done digitally
 *
 * Converted to the radio-isa framework by Hans Verkuil <hans.verkuil@cisco.com>
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/mutex.h>
#include <linux/io.h>		/* outb, outb_p			*/
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include "radio-isa.h"

MODULE_AUTHOR("R. Offermans & others");
MODULE_DESCRIPTION("A driver for the TerraTec ActiveRadio Standalone radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.99");

/* Note: there seems to be only one possible port (0x590), but without
   hardware this is hard to verify. For now, this is the only one we will
   support. */
static int io = 0x590;
static int radio_nr = -1;

module_param(radio_nr, int, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device number");

#define WRT_DIS 	0x00
#define CLK_OFF		0x00
#define IIC_DATA	0x01
#define IIC_CLK		0x02
#define DATA		0x04
#define CLK_ON 		0x08
#define WRT_EN		0x10

static struct radio_isa_card *terratec_alloc(void)
{
	return kzalloc(sizeof(struct radio_isa_card), GFP_KERNEL);
}

static int terratec_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	int i;

	if (mute)
		vol = 0;
	vol = vol + (vol * 32); /* change both channels */
	for (i = 0; i < 8; i++) {
		if (vol & (0x80 >> i))
			outb(0x80, isa->io + 1);
		else
			outb(0x00, isa->io + 1);
	}
	return 0;
}


/* this is the worst part in this driver */
/* many more or less strange things are going on here, but hey, it works :) */

static int terratec_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	int i;
	int p;
	int temp;
	long rest;
	unsigned char buffer[25];		/* we have to bit shift 25 registers */

	freq = freq / 160;			/* convert the freq. to a nice to handle value */
	memset(buffer, 0, sizeof(buffer));

	rest = freq * 10 + 10700;	/* I once had understood what is going on here */
					/* maybe some wise guy (friedhelm?) can comment this stuff */
	i = 13;
	p = 10;
	temp = 102400;
	while (rest != 0) {
		if (rest % temp  == rest)
			buffer[i] = 0;
		else {
			buffer[i] = 1;
			rest = rest - temp;
		}
		i--;
		p--;
		temp = temp / 2;
	}

	for (i = 24; i > -1; i--) {	/* bit shift the values to the radiocard */
		if (buffer[i] == 1) {
			outb(WRT_EN | DATA, isa->io);
			outb(WRT_EN | DATA | CLK_ON, isa->io);
			outb(WRT_EN | DATA, isa->io);
		} else {
			outb(WRT_EN | 0x00, isa->io);
			outb(WRT_EN | 0x00 | CLK_ON, isa->io);
		}
	}
	outb(0x00, isa->io);
	return 0;
}

static u32 terratec_g_signal(struct radio_isa_card *isa)
{
	/* bit set = no signal present	*/
	return (inb(isa->io) & 2) ? 0 : 0xffff;
}

static const struct radio_isa_ops terratec_ops = {
	.alloc = terratec_alloc,
	.s_mute_volume = terratec_s_mute_volume,
	.s_frequency = terratec_s_frequency,
	.g_signal = terratec_g_signal,
};

static const int terratec_ioports[] = { 0x590 };

static struct radio_isa_driver terratec_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-terratec",
		},
	},
	.io_params = &io,
	.radio_nr_params = &radio_nr,
	.io_ports = terratec_ioports,
	.num_of_io_ports = ARRAY_SIZE(terratec_ioports),
	.region_size = 2,
	.card = "TerraTec ActiveRadio",
	.ops = &terratec_ops,
	.has_stereo = true,
	.max_volume = 10,
};

static int __init terratec_init(void)
{
	return isa_register_driver(&terratec_driver.driver, 1);
}

static void __exit terratec_exit(void)
{
	isa_unregister_driver(&terratec_driver.driver);
}

module_init(terratec_init);
module_exit(terratec_exit);

