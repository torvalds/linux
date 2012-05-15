/* SF16-FMR2 radio driver for Linux
 * Copyright (c) 2011 Ondrej Zary
 *
 * Original driver was (c) 2000-2002 Ziglio Frediano, freddy77@angelfire.com
 * but almost nothing remained here after conversion to generic TEA575x
 * implementation
 */

#include <linux/delay.h>
#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/slab.h>
#include <linux/ioport.h>	/* request_region		*/
#include <linux/io.h>		/* outb, outb_p			*/
#include <linux/isa.h>
#include <sound/tea575x-tuner.h>

MODULE_AUTHOR("Ondrej Zary");
MODULE_DESCRIPTION("MediaForte SF16-FMR2 FM radio card driver");
MODULE_LICENSE("GPL");

static int radio_nr = -1;
module_param(radio_nr, int, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device number");

struct fmr2 {
	int io;
	struct v4l2_device v4l2_dev;
	struct snd_tea575x tea;
	struct v4l2_ctrl *volume;
	struct v4l2_ctrl *balance;
};

/* the port is hardwired so no need to support multiple cards */
#define FMR2_PORT	0x384

/* TEA575x tuner pins */
#define STR_DATA	(1 << 0)
#define STR_CLK		(1 << 1)
#define STR_WREN	(1 << 2)
#define STR_MOST	(1 << 3)
/* PT2254A/TC9154A volume control pins */
#define PT_ST		(1 << 4)
#define PT_CK		(1 << 5)
#define PT_DATA		(1 << 6)
/* volume control presence pin */
#define FMR2_HASVOL	(1 << 7)

static void fmr2_tea575x_set_pins(struct snd_tea575x *tea, u8 pins)
{
	struct fmr2 *fmr2 = tea->private_data;
	u8 bits = 0;

	bits |= (pins & TEA575X_DATA) ? STR_DATA : 0;
	bits |= (pins & TEA575X_CLK)  ? STR_CLK  : 0;
	/* WRITE_ENABLE is inverted, DATA must be high during read */
	bits |= (pins & TEA575X_WREN) ? 0 : STR_WREN | STR_DATA;

	outb(bits, fmr2->io);
}

static u8 fmr2_tea575x_get_pins(struct snd_tea575x *tea)
{
	struct fmr2 *fmr2 = tea->private_data;
	u8 bits = inb(fmr2->io);

	return  (bits & STR_DATA) ? TEA575X_DATA : 0 |
		(bits & STR_MOST) ? TEA575X_MOST : 0;
}

static void fmr2_tea575x_set_direction(struct snd_tea575x *tea, bool output)
{
}

static struct snd_tea575x_ops fmr2_tea_ops = {
	.set_pins = fmr2_tea575x_set_pins,
	.get_pins = fmr2_tea575x_get_pins,
	.set_direction = fmr2_tea575x_set_direction,
};

/* TC9154A/PT2254A volume control */

/* 18-bit shift register bit definitions */
#define TC9154A_ATT_MAJ_0DB	(1 << 0)
#define TC9154A_ATT_MAJ_10DB	(1 << 1)
#define TC9154A_ATT_MAJ_20DB	(1 << 2)
#define TC9154A_ATT_MAJ_30DB	(1 << 3)
#define TC9154A_ATT_MAJ_40DB	(1 << 4)
#define TC9154A_ATT_MAJ_50DB	(1 << 5)
#define TC9154A_ATT_MAJ_60DB	(1 << 6)

#define TC9154A_ATT_MIN_0DB	(1 << 7)
#define TC9154A_ATT_MIN_2DB	(1 << 8)
#define TC9154A_ATT_MIN_4DB	(1 << 9)
#define TC9154A_ATT_MIN_6DB	(1 << 10)
#define TC9154A_ATT_MIN_8DB	(1 << 11)
/* bit 12 is ignored */
#define TC9154A_CHANNEL_LEFT	(1 << 13)
#define TC9154A_CHANNEL_RIGHT	(1 << 14)
/* bits 15, 16, 17 must be 0 */

#define	TC9154A_ATT_MAJ(x)	(1 << x)
#define TC9154A_ATT_MIN(x)	(1 << (7 + x))

static void tc9154a_set_pins(struct fmr2 *fmr2, u8 pins)
{
	if (!fmr2->tea.mute)
		pins |= STR_WREN;

	outb(pins, fmr2->io);
}

static void tc9154a_set_attenuation(struct fmr2 *fmr2, int att, u32 channel)
{
	int i;
	u32 reg;
	u8 bit;

	reg = TC9154A_ATT_MAJ(att / 10) | TC9154A_ATT_MIN((att % 10) / 2);
	reg |= channel;
	/* write 18-bit shift register, LSB first */
	for (i = 0; i < 18; i++) {
		bit = reg & (1 << i) ? PT_DATA : 0;
		tc9154a_set_pins(fmr2, bit);
		udelay(5);
		tc9154a_set_pins(fmr2, bit | PT_CK);
		udelay(5);
		tc9154a_set_pins(fmr2, bit);
	}

	/* latch register data */
	udelay(5);
	tc9154a_set_pins(fmr2, PT_ST);
	udelay(5);
	tc9154a_set_pins(fmr2, 0);
}

static int fmr2_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct snd_tea575x *tea = container_of(ctrl->handler, struct snd_tea575x, ctrl_handler);
	struct fmr2 *fmr2 = tea->private_data;
	int volume, balance, left, right;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		volume = ctrl->val;
		balance = fmr2->balance->cur.val;
		break;
	case V4L2_CID_AUDIO_BALANCE:
		balance = ctrl->val;
		volume = fmr2->volume->cur.val;
		break;
	default:
		return -EINVAL;
	}

	left = right = volume;
	if (balance < 0)
		right = max(0, right + balance);
	if (balance > 0)
		left = max(0, left - balance);

	tc9154a_set_attenuation(fmr2, abs(left - 68), TC9154A_CHANNEL_LEFT);
	tc9154a_set_attenuation(fmr2, abs(right - 68), TC9154A_CHANNEL_RIGHT);

	return 0;
}

static const struct v4l2_ctrl_ops fmr2_ctrl_ops = {
	.s_ctrl = fmr2_s_ctrl,
};

static int fmr2_tea_ext_init(struct snd_tea575x *tea)
{
	struct fmr2 *fmr2 = tea->private_data;

	if (inb(fmr2->io) & FMR2_HASVOL) {
		fmr2->volume = v4l2_ctrl_new_std(&tea->ctrl_handler, &fmr2_ctrl_ops, V4L2_CID_AUDIO_VOLUME, 0, 68, 2, 56);
		fmr2->balance = v4l2_ctrl_new_std(&tea->ctrl_handler, &fmr2_ctrl_ops, V4L2_CID_AUDIO_BALANCE, -68, 68, 2, 0);
		if (tea->ctrl_handler.error) {
			printk(KERN_ERR "radio-sf16fmr2: can't initialize controls\n");
			return tea->ctrl_handler.error;
		}
	}

	return 0;
}

static int __devinit fmr2_probe(struct device *pdev, unsigned int dev)
{
	struct fmr2 *fmr2;
	int err;

	fmr2 = kzalloc(sizeof(*fmr2), GFP_KERNEL);
	if (fmr2 == NULL)
		return -ENOMEM;

	strlcpy(fmr2->v4l2_dev.name, dev_name(pdev),
			sizeof(fmr2->v4l2_dev.name));
	fmr2->io = FMR2_PORT;

	if (!request_region(fmr2->io, 2, fmr2->v4l2_dev.name)) {
		printk(KERN_ERR "radio-sf16fmr2: I/O port 0x%x already in use\n", fmr2->io);
		kfree(fmr2);
		return -EBUSY;
	}

	dev_set_drvdata(pdev, fmr2);
	err = v4l2_device_register(pdev, &fmr2->v4l2_dev);
	if (err < 0) {
		v4l2_err(&fmr2->v4l2_dev, "Could not register v4l2_device\n");
		release_region(fmr2->io, 2);
		kfree(fmr2);
		return err;
	}
	fmr2->tea.v4l2_dev = &fmr2->v4l2_dev;
	fmr2->tea.private_data = fmr2;
	fmr2->tea.radio_nr = radio_nr;
	fmr2->tea.ops = &fmr2_tea_ops;
	fmr2->tea.ext_init = fmr2_tea_ext_init;
	strlcpy(fmr2->tea.card, "SF16-FMR2", sizeof(fmr2->tea.card));
	snprintf(fmr2->tea.bus_info, sizeof(fmr2->tea.bus_info), "ISA:%s",
			fmr2->v4l2_dev.name);

	if (snd_tea575x_init(&fmr2->tea)) {
		printk(KERN_ERR "radio-sf16fmr2: Unable to detect TEA575x tuner\n");
		release_region(fmr2->io, 2);
		kfree(fmr2);
		return -ENODEV;
	}

	printk(KERN_INFO "radio-sf16fmr2: SF16-FMR2 radio card at 0x%x.\n", fmr2->io);
	return 0;
}

static int __exit fmr2_remove(struct device *pdev, unsigned int dev)
{
	struct fmr2 *fmr2 = dev_get_drvdata(pdev);

	snd_tea575x_exit(&fmr2->tea);
	release_region(fmr2->io, 2);
	v4l2_device_unregister(&fmr2->v4l2_dev);
	kfree(fmr2);
	return 0;
}

struct isa_driver fmr2_driver = {
	.probe		= fmr2_probe,
	.remove		= fmr2_remove,
	.driver		= {
		.name	= "radio-sf16fmr2",
	},
};

static int __init fmr2_init(void)
{
	return isa_register_driver(&fmr2_driver, 1);
}

static void __exit fmr2_exit(void)
{
	isa_unregister_driver(&fmr2_driver);
}

module_init(fmr2_init);
module_exit(fmr2_exit);
