// SPDX-License-Identifier: GPL-2.0-or-later
/*
    hexium_orion.c - v4l2 driver for the Hexium Orion frame grabber cards

    Visit http://www.mihu.de/linux/saa7146/ and follow the link
    to "hexium" for further details about this card.

    Copyright (C) 2003 Michael Hunold <michael@mihu.de>

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DEBUG_VARIABLE debug

#include <media/drv-intf/saa7146_vv.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "debug verbosity");

/* global variables */
static int hexium_num;

#define HEXIUM_HV_PCI6_ORION		1
#define HEXIUM_ORION_1SVHS_3BNC		2
#define HEXIUM_ORION_4BNC		3

#define HEXIUM_STD (V4L2_STD_PAL | V4L2_STD_SECAM | V4L2_STD_NTSC)
#define HEXIUM_INPUTS	9
static struct v4l2_input hexium_inputs[HEXIUM_INPUTS] = {
	{ 0, "CVBS 1",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 1, "CVBS 2",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 2, "CVBS 3",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 3, "CVBS 4",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 4, "CVBS 5",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 5, "CVBS 6",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 6, "Y/C 1",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 7, "Y/C 2",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
	{ 8, "Y/C 3",	V4L2_INPUT_TYPE_CAMERA,	0, 0, HEXIUM_STD, 0, V4L2_IN_CAP_STD },
};

#define HEXIUM_AUDIOS	0

struct hexium_data
{
	s8 adr;
	u8 byte;
};

struct hexium
{
	int type;
	struct video_device	video_dev;
	struct i2c_adapter	i2c_adapter;

	int cur_input;	/* current input */
};

/* Philips SAA7110 decoder default registers */
static u8 hexium_saa7110[53]={
/*00*/ 0x4C,0x3C,0x0D,0xEF,0xBD,0xF0,0x00,0x00,
/*08*/ 0xF8,0xF8,0x60,0x60,0x40,0x86,0x18,0x90,
/*10*/ 0x00,0x2C,0x40,0x46,0x42,0x1A,0xFF,0xDA,
/*18*/ 0xF0,0x8B,0x00,0x00,0x00,0x00,0x00,0x00,
/*20*/ 0xD9,0x17,0x40,0x41,0x80,0x41,0x80,0x4F,
/*28*/ 0xFE,0x01,0x0F,0x0F,0x03,0x01,0x81,0x03,
/*30*/ 0x44,0x75,0x01,0x8C,0x03
};

static struct {
	struct hexium_data data[8];
} hexium_input_select[] = {
{
	{ /* cvbs 1 */
		{ 0x06, 0x00 },
		{ 0x20, 0xD9 },
		{ 0x21, 0x17 }, // 0x16,
		{ 0x22, 0x40 },
		{ 0x2C, 0x03 },
		{ 0x30, 0x44 },
		{ 0x31, 0x75 }, // ??
		{ 0x21, 0x16 }, // 0x03,
	}
}, {
	{ /* cvbs 2 */
		{ 0x06, 0x00 },
		{ 0x20, 0x78 },
		{ 0x21, 0x07 }, // 0x03,
		{ 0x22, 0xD2 },
		{ 0x2C, 0x83 },
		{ 0x30, 0x60 },
		{ 0x31, 0xB5 }, // ?
		{ 0x21, 0x03 },
	}
}, {
	{ /* cvbs 3 */
		{ 0x06, 0x00 },
		{ 0x20, 0xBA },
		{ 0x21, 0x07 }, // 0x05,
		{ 0x22, 0x91 },
		{ 0x2C, 0x03 },
		{ 0x30, 0x60 },
		{ 0x31, 0xB5 }, // ??
		{ 0x21, 0x05 }, // 0x03,
	}
}, {
	{ /* cvbs 4 */
		{ 0x06, 0x00 },
		{ 0x20, 0xD8 },
		{ 0x21, 0x17 }, // 0x16,
		{ 0x22, 0x40 },
		{ 0x2C, 0x03 },
		{ 0x30, 0x44 },
		{ 0x31, 0x75 }, // ??
		{ 0x21, 0x16 }, // 0x03,
	}
}, {
	{ /* cvbs 5 */
		{ 0x06, 0x00 },
		{ 0x20, 0xB8 },
		{ 0x21, 0x07 }, // 0x05,
		{ 0x22, 0x91 },
		{ 0x2C, 0x03 },
		{ 0x30, 0x60 },
		{ 0x31, 0xB5 }, // ??
		{ 0x21, 0x05 }, // 0x03,
	}
}, {
	{ /* cvbs 6 */
		{ 0x06, 0x00 },
		{ 0x20, 0x7C },
		{ 0x21, 0x07 }, // 0x03
		{ 0x22, 0xD2 },
		{ 0x2C, 0x83 },
		{ 0x30, 0x60 },
		{ 0x31, 0xB5 }, // ??
		{ 0x21, 0x03 },
	}
}, {
	{ /* y/c 1 */
		{ 0x06, 0x80 },
		{ 0x20, 0x59 },
		{ 0x21, 0x17 },
		{ 0x22, 0x42 },
		{ 0x2C, 0xA3 },
		{ 0x30, 0x44 },
		{ 0x31, 0x75 },
		{ 0x21, 0x12 },
	}
}, {
	{ /* y/c 2 */
		{ 0x06, 0x80 },
		{ 0x20, 0x9A },
		{ 0x21, 0x17 },
		{ 0x22, 0xB1 },
		{ 0x2C, 0x13 },
		{ 0x30, 0x60 },
		{ 0x31, 0xB5 },
		{ 0x21, 0x14 },
	}
}, {
	{ /* y/c 3 */
		{ 0x06, 0x80 },
		{ 0x20, 0x3C },
		{ 0x21, 0x27 },
		{ 0x22, 0xC1 },
		{ 0x2C, 0x23 },
		{ 0x30, 0x44 },
		{ 0x31, 0x75 },
		{ 0x21, 0x21 },
	}
}
};

static struct saa7146_standard hexium_standards[] = {
	{
		.name	= "PAL",	.id	= V4L2_STD_PAL,
		.v_offset	= 16,	.v_field	= 288,
		.h_offset	= 1,	.h_pixels	= 680,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC",	.id	= V4L2_STD_NTSC,
		.v_offset	= 16,	.v_field	= 240,
		.h_offset	= 1,	.h_pixels	= 640,
		.v_max_out	= 480,	.h_max_out	= 640,
	}, {
		.name	= "SECAM",	.id	= V4L2_STD_SECAM,
		.v_offset	= 16,	.v_field	= 288,
		.h_offset	= 1,	.h_pixels	= 720,
		.v_max_out	= 576,	.h_max_out	= 768,
	}
};

/* this is only called for old HV-PCI6/Orion cards
   without eeprom */
static int hexium_probe(struct saa7146_dev *dev)
{
	struct hexium *hexium = NULL;
	union i2c_smbus_data data;
	int err = 0;

	DEB_EE("\n");

	/* there are no hexium orion cards with revision 0 saa7146s */
	if (0 == dev->revision) {
		return -EFAULT;
	}

	hexium = kzalloc(sizeof(*hexium), GFP_KERNEL);
	if (!hexium)
		return -ENOMEM;

	/* enable i2c-port pins */
	saa7146_write(dev, MC1, (MASK_08 | MASK_24 | MASK_10 | MASK_26));

	saa7146_write(dev, DD1_INIT, 0x01000100);
	saa7146_write(dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	strscpy(hexium->i2c_adapter.name, "hexium orion",
		sizeof(hexium->i2c_adapter.name));
	saa7146_i2c_adapter_prepare(dev, &hexium->i2c_adapter, SAA7146_I2C_BUS_BIT_RATE_480);
	if (i2c_add_adapter(&hexium->i2c_adapter) < 0) {
		DEB_S("cannot register i2c-device. skipping.\n");
		kfree(hexium);
		return -EFAULT;
	}

	/* set SAA7110 control GPIO 0 */
	saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTHI);
	/*  set HWControl GPIO number 2 */
	saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTHI);

	mdelay(10);

	/* detect newer Hexium Orion cards by subsystem ids */
	if (0x17c8 == dev->pci->subsystem_vendor && 0x0101 == dev->pci->subsystem_device) {
		pr_info("device is a Hexium Orion w/ 1 SVHS + 3 BNC inputs\n");
		/* we store the pointer in our private data field */
		dev->ext_priv = hexium;
		hexium->type = HEXIUM_ORION_1SVHS_3BNC;
		return 0;
	}

	if (0x17c8 == dev->pci->subsystem_vendor && 0x2101 == dev->pci->subsystem_device) {
		pr_info("device is a Hexium Orion w/ 4 BNC inputs\n");
		/* we store the pointer in our private data field */
		dev->ext_priv = hexium;
		hexium->type = HEXIUM_ORION_4BNC;
		return 0;
	}

	/* check if this is an old hexium Orion card by looking at
	   a saa7110 at address 0x4e */
	err = i2c_smbus_xfer(&hexium->i2c_adapter, 0x4e, 0, I2C_SMBUS_READ,
			     0x00, I2C_SMBUS_BYTE_DATA, &data);
	if (err == 0) {
		pr_info("device is a Hexium HV-PCI6/Orion (old)\n");
		/* we store the pointer in our private data field */
		dev->ext_priv = hexium;
		hexium->type = HEXIUM_HV_PCI6_ORION;
		return 0;
	}

	i2c_del_adapter(&hexium->i2c_adapter);
	kfree(hexium);
	return -EFAULT;
}

/* bring hardware to a sane state. this has to be done, just in case someone
   wants to capture from this device before it has been properly initialized.
   the capture engine would badly fail, because no valid signal arrives on the
   saa7146, thus leading to timeouts and stuff. */
static int hexium_init_done(struct saa7146_dev *dev)
{
	struct hexium *hexium = (struct hexium *) dev->ext_priv;
	union i2c_smbus_data data;
	int i = 0;

	DEB_D("hexium_init_done called\n");

	/* initialize the helper ics to useful values */
	for (i = 0; i < sizeof(hexium_saa7110); i++) {
		data.byte = hexium_saa7110[i];
		if (0 != i2c_smbus_xfer(&hexium->i2c_adapter, 0x4e, 0, I2C_SMBUS_WRITE, i, I2C_SMBUS_BYTE_DATA, &data)) {
			pr_err("failed for address 0x%02x\n", i);
		}
	}

	return 0;
}

static int hexium_set_input(struct hexium *hexium, int input)
{
	union i2c_smbus_data data;
	int i = 0;

	DEB_D("\n");

	for (i = 0; i < 8; i++) {
		int adr = hexium_input_select[input].data[i].adr;
		data.byte = hexium_input_select[input].data[i].byte;
		if (0 != i2c_smbus_xfer(&hexium->i2c_adapter, 0x4e, 0, I2C_SMBUS_WRITE, adr, I2C_SMBUS_BYTE_DATA, &data)) {
			return -1;
		}
		pr_debug("%d: 0x%02x => 0x%02x\n", input, adr, data.byte);
	}

	return 0;
}

static int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	DEB_EE("VIDIOC_ENUMINPUT %d\n", i->index);

	if (i->index >= HEXIUM_INPUTS)
		return -EINVAL;

	memcpy(i, &hexium_inputs[i->index], sizeof(struct v4l2_input));

	DEB_D("v4l2_ioctl: VIDIOC_ENUMINPUT %d\n", i->index);
	return 0;
}

static int vidioc_g_input(struct file *file, void *fh, unsigned int *input)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct hexium *hexium = (struct hexium *) dev->ext_priv;

	*input = hexium->cur_input;

	DEB_D("VIDIOC_G_INPUT: %d\n", *input);
	return 0;
}

static int vidioc_s_input(struct file *file, void *fh, unsigned int input)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct hexium *hexium = (struct hexium *) dev->ext_priv;

	if (input >= HEXIUM_INPUTS)
		return -EINVAL;

	hexium->cur_input = input;
	hexium_set_input(hexium, input);

	return 0;
}

static struct saa7146_ext_vv vv_data;

/* this function only gets called when the probing was successful */
static int hexium_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct hexium *hexium = (struct hexium *) dev->ext_priv;
	int ret;

	DEB_EE("\n");

	ret = saa7146_vv_init(dev, &vv_data);
	if (ret) {
		pr_err("Error in saa7146_vv_init()\n");
		return ret;
	}

	vv_data.vid_ops.vidioc_enum_input = vidioc_enum_input;
	vv_data.vid_ops.vidioc_g_input = vidioc_g_input;
	vv_data.vid_ops.vidioc_s_input = vidioc_s_input;
	if (0 != saa7146_register_device(&hexium->video_dev, dev, "hexium orion", VFL_TYPE_VIDEO)) {
		pr_err("cannot register capture v4l2 device. skipping.\n");
		return -1;
	}

	pr_err("found 'hexium orion' frame grabber-%d\n", hexium_num);
	hexium_num++;

	/* the rest */
	hexium->cur_input = 0;
	hexium_init_done(dev);
	hexium_set_input(hexium, 0);

	return 0;
}

static int hexium_detach(struct saa7146_dev *dev)
{
	struct hexium *hexium = (struct hexium *) dev->ext_priv;

	DEB_EE("dev:%p\n", dev);

	saa7146_unregister_device(&hexium->video_dev, dev);
	saa7146_vv_release(dev);

	hexium_num--;

	i2c_del_adapter(&hexium->i2c_adapter);
	kfree(hexium);
	return 0;
}

static int std_callback(struct saa7146_dev *dev, struct saa7146_standard *std)
{
	return 0;
}

static struct saa7146_extension extension;

static struct saa7146_pci_extension_data hexium_hv_pci6 = {
	.ext_priv = "Hexium HV-PCI6 / Orion",
	.ext = &extension,
};

static struct saa7146_pci_extension_data hexium_orion_1svhs_3bnc = {
	.ext_priv = "Hexium HV-PCI6 / Orion (1 SVHS/3 BNC)",
	.ext = &extension,
};

static struct saa7146_pci_extension_data hexium_orion_4bnc = {
	.ext_priv = "Hexium HV-PCI6 / Orion (4 BNC)",
	.ext = &extension,
};

static const struct pci_device_id pci_tbl[] = {
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x0000,
	 .subdevice = 0x0000,
	 .driver_data = (unsigned long) &hexium_hv_pci6,
	 },
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x17c8,
	 .subdevice = 0x0101,
	 .driver_data = (unsigned long) &hexium_orion_1svhs_3bnc,
	 },
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x17c8,
	 .subdevice = 0x2101,
	 .driver_data = (unsigned long) &hexium_orion_4bnc,
	 },
	{
	 .vendor = 0,
	 }
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_ext_vv vv_data = {
	.inputs = HEXIUM_INPUTS,
	.capabilities = 0,
	.stds = &hexium_standards[0],
	.num_stds = ARRAY_SIZE(hexium_standards),
	.std_callback = &std_callback,
};

static struct saa7146_extension extension = {
	.name = "hexium HV-PCI6 Orion",
	.flags = 0,		// SAA7146_USE_I2C_IRQ,

	.pci_tbl = &pci_tbl[0],
	.module = THIS_MODULE,

	.probe = hexium_probe,
	.attach = hexium_attach,
	.detach = hexium_detach,

	.irq_mask = 0,
	.irq_func = NULL,
};

static int __init hexium_init_module(void)
{
	if (0 != saa7146_register_extension(&extension)) {
		DEB_S("failed to register extension\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit hexium_cleanup_module(void)
{
	saa7146_unregister_extension(&extension);
}

module_init(hexium_init_module);
module_exit(hexium_cleanup_module);

MODULE_DESCRIPTION("video4linux-2 driver for Hexium Orion frame grabber cards");
MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_LICENSE("GPL");
