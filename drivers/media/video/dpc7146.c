/*
    dpc7146.c - v4l2 driver for the dpc7146 demonstration board

    Copyright (C) 2000-2003 Michael Hunold <michael@mihu.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define DEBUG_VARIABLE debug

#include <media/saa7146_vv.h>
#include <linux/video_decoder.h>	/* for saa7111a */

#define I2C_SAA7111A            0x24

/* All unused bytes are reserverd. */
#define SAA711X_CHIP_VERSION            0x00
#define SAA711X_ANALOG_INPUT_CONTROL_1  0x02
#define SAA711X_ANALOG_INPUT_CONTROL_2  0x03
#define SAA711X_ANALOG_INPUT_CONTROL_3  0x04
#define SAA711X_ANALOG_INPUT_CONTROL_4  0x05
#define SAA711X_HORIZONTAL_SYNC_START   0x06
#define SAA711X_HORIZONTAL_SYNC_STOP    0x07
#define SAA711X_SYNC_CONTROL            0x08
#define SAA711X_LUMINANCE_CONTROL       0x09
#define SAA711X_LUMINANCE_BRIGHTNESS    0x0A
#define SAA711X_LUMINANCE_CONTRAST      0x0B
#define SAA711X_CHROMA_SATURATION       0x0C
#define SAA711X_CHROMA_HUE_CONTROL      0x0D
#define SAA711X_CHROMA_CONTROL          0x0E
#define SAA711X_FORMAT_DELAY_CONTROL    0x10
#define SAA711X_OUTPUT_CONTROL_1        0x11
#define SAA711X_OUTPUT_CONTROL_2        0x12
#define SAA711X_OUTPUT_CONTROL_3        0x13
#define SAA711X_V_GATE_1_START          0x15
#define SAA711X_V_GATE_1_STOP           0x16
#define SAA711X_V_GATE_1_MSB            0x17
#define SAA711X_TEXT_SLICER_STATUS      0x1A
#define SAA711X_DECODED_BYTES_OF_TS_1   0x1B
#define SAA711X_DECODED_BYTES_OF_TS_2   0x1C
#define SAA711X_STATUS_BYTE             0x1F

#define DPC_BOARD_CAN_DO_VBI(dev)   (dev->revision != 0)

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "debug verbosity");

static int dpc_num;

#define DPC_INPUTS	2
static struct v4l2_input dpc_inputs[DPC_INPUTS] = {
	{ 0, "Port A",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 1, "Port B",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
};

#define DPC_AUDIOS	0

static struct saa7146_extension_ioctls ioctls[] = {
	{ VIDIOC_G_INPUT,	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_INPUT,	SAA7146_EXCLUSIVE },
	{ VIDIOC_ENUMINPUT, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_STD,		SAA7146_AFTER },
	{ 0,			0 }
};

struct dpc
{
	struct video_device	*video_dev;
	struct video_device	*vbi_dev;

	struct i2c_adapter	i2c_adapter;
	struct i2c_client	*saa7111a;

	int cur_input;	/* current input */
};

static int dpc_check_clients(struct device *dev, void *data)
{
	struct dpc* dpc = data;
	struct i2c_client *client = i2c_verify_client(dev);

	if( !client )
		return 0;

	if( I2C_SAA7111A == client->addr )
		dpc->saa7111a = client;

	return 0;
}

/* fixme: add vbi stuff here */
static int dpc_probe(struct saa7146_dev* dev)
{
	struct dpc* dpc = NULL;

	dpc = kzalloc(sizeof(struct dpc), GFP_KERNEL);
	if( NULL == dpc ) {
		printk("dpc_v4l2.o: dpc_probe: not enough kernel memory.\n");
		return -ENOMEM;
	}

	/* FIXME: enable i2c-port pins, video-port-pins
	   video port pins should be enabled here ?! */
	saa7146_write(dev, MC1, (MASK_08 | MASK_24 | MASK_10 | MASK_26));

	dpc->i2c_adapter = (struct i2c_adapter) {
		.class = I2C_CLASS_TV_ANALOG,
		.name = "dpc7146",
	};
	saa7146_i2c_adapter_prepare(dev, &dpc->i2c_adapter, SAA7146_I2C_BUS_BIT_RATE_480);
	if(i2c_add_adapter(&dpc->i2c_adapter) < 0) {
		DEB_S(("cannot register i2c-device. skipping.\n"));
		kfree(dpc);
		return -EFAULT;
	}

	/* loop through all i2c-devices on the bus and look who is there */
	device_for_each_child(&dpc->i2c_adapter.dev, dpc, dpc_check_clients);

	/* check if all devices are present */
	if (!dpc->saa7111a) {
		DEB_D(("dpc_v4l2.o: dpc_attach failed for this device.\n"));
		i2c_del_adapter(&dpc->i2c_adapter);
		kfree(dpc);
		return -ENODEV;
	}

	/* all devices are present, probe was successful */
	DEB_D(("dpc_v4l2.o: dpc_probe succeeded for this device.\n"));

	/* we store the pointer in our private data field */
	dev->ext_priv = dpc;

	return 0;
}

/* bring hardware to a sane state. this has to be done, just in case someone
   wants to capture from this device before it has been properly initialized.
   the capture engine would badly fail, because no valid signal arrives on the
   saa7146, thus leading to timeouts and stuff. */
static int dpc_init_done(struct saa7146_dev* dev)
{
	struct dpc* dpc = (struct dpc*)dev->ext_priv;

	DEB_D(("dpc_v4l2.o: dpc_init_done called.\n"));

	/* initialize the helper ics to useful values */
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x00, 0x11);

	i2c_smbus_write_byte_data(dpc->saa7111a, 0x02, 0xc0);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x03, 0x30);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x04, 0x00);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x05, 0x00);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x06, 0xde);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x07, 0xad);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x08, 0xa8);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x09, 0x00);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x0a, 0x80);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x0b, 0x47);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x0c, 0x40);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x0d, 0x00);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x0e, 0x03);

	i2c_smbus_write_byte_data(dpc->saa7111a, 0x10, 0xd0);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x11, 0x1c);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x12, 0xc1);
	i2c_smbus_write_byte_data(dpc->saa7111a, 0x13, 0x30);

	i2c_smbus_write_byte_data(dpc->saa7111a, 0x1f, 0x81);

	return 0;
}

static struct saa7146_ext_vv vv_data;

/* this function only gets called when the probing was successful */
static int dpc_attach(struct saa7146_dev* dev, struct saa7146_pci_extension_data *info)
{
	struct dpc* dpc = (struct dpc*)dev->ext_priv;

	DEB_D(("dpc_v4l2.o: dpc_attach called.\n"));

	/* checking for i2c-devices can be omitted here, because we
	   already did this in "dpc_vl42_probe" */

	saa7146_vv_init(dev,&vv_data);
	if( 0 != saa7146_register_device(&dpc->video_dev, dev, "dpc", VFL_TYPE_GRABBER)) {
		ERR(("cannot register capture v4l2 device. skipping.\n"));
		return -1;
	}

	/* initialization stuff (vbi) (only for revision > 0 and for extensions which want it)*/
	if( 0 != DPC_BOARD_CAN_DO_VBI(dev)) {
		if( 0 != saa7146_register_device(&dpc->vbi_dev, dev, "dpc", VFL_TYPE_VBI)) {
			ERR(("cannot register vbi v4l2 device. skipping.\n"));
		}
	}

	i2c_use_client(dpc->saa7111a);

	printk("dpc: found 'dpc7146 demonstration board'-%d.\n",dpc_num);
	dpc_num++;

	/* the rest */
	dpc->cur_input = 0;
	dpc_init_done(dev);

	return 0;
}

static int dpc_detach(struct saa7146_dev* dev)
{
	struct dpc* dpc = (struct dpc*)dev->ext_priv;

	DEB_EE(("dev:%p\n",dev));

	i2c_release_client(dpc->saa7111a);

	saa7146_unregister_device(&dpc->video_dev,dev);
	if( 0 != DPC_BOARD_CAN_DO_VBI(dev)) {
		saa7146_unregister_device(&dpc->vbi_dev,dev);
	}
	saa7146_vv_release(dev);

	dpc_num--;

	i2c_del_adapter(&dpc->i2c_adapter);
	kfree(dpc);
	return 0;
}

#ifdef axa
int dpc_vbi_bypass(struct saa7146_dev* dev)
{
	struct dpc* dpc = (struct dpc*)dev->ext_priv;

	int i = 1;

	/* switch bypass in saa7111a */
	if ( 0 != dpc->saa7111a->driver->command(dpc->saa7111a,SAA711X_VBI_BYPASS, &i)) {
		printk("dpc_v4l2.o: VBI_BYPASS: could not address saa7111a.\n");
		return -1;
	}

	return 0;
}
#endif

static int dpc_ioctl(struct saa7146_fh *fh, unsigned int cmd, void *arg)
{
	struct saa7146_dev *dev = fh->dev;
	struct dpc* dpc = (struct dpc*)dev->ext_priv;
/*
	struct saa7146_vv *vv = dev->vv_data;
*/
	switch(cmd)
	{
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;
		DEB_EE(("VIDIOC_ENUMINPUT %d.\n",i->index));

		if( i->index < 0 || i->index >= DPC_INPUTS) {
			return -EINVAL;
		}

		memcpy(i, &dpc_inputs[i->index], sizeof(struct v4l2_input));

		DEB_D(("dpc_v4l2.o: v4l2_ioctl: VIDIOC_ENUMINPUT %d.\n",i->index));
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *input = (int *)arg;
		*input = dpc->cur_input;

		DEB_D(("dpc_v4l2.o: VIDIOC_G_INPUT: %d\n",*input));
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		int	input = *(int *)arg;

		if (input < 0 || input >= DPC_INPUTS) {
			return -EINVAL;
		}

		dpc->cur_input = input;

		/* fixme: switch input here, switch audio, too! */
//		saa7146_set_hps_source_and_sync(dev, input_port_selection[input].hps_source, input_port_selection[input].hps_sync);
		printk("dpc_v4l2.o: VIDIOC_S_INPUT: fixme switch input.\n");

		return 0;
	}
	default:
/*
		DEB_D(("dpc_v4l2.o: v4l2_ioctl does not handle this ioctl.\n"));
*/
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int std_callback(struct saa7146_dev* dev, struct saa7146_standard *std)
{
	return 0;
}

static struct saa7146_standard standard[] = {
	{
		.name	= "PAL", 	.id	= V4L2_STD_PAL,
		.v_offset	= 0x17,	.v_field 	= 288,
		.h_offset	= 0x14,	.h_pixels 	= 680,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC", 	.id	= V4L2_STD_NTSC,
		.v_offset	= 0x16,	.v_field 	= 240,
		.h_offset	= 0x06,	.h_pixels 	= 708,
		.v_max_out	= 480,	.h_max_out	= 640,
	}, {
		.name	= "SECAM", 	.id	= V4L2_STD_SECAM,
		.v_offset	= 0x14,	.v_field 	= 288,
		.h_offset	= 0x14,	.h_pixels 	= 720,
		.v_max_out	= 576,	.h_max_out	= 768,
	}
};

static struct saa7146_extension extension;

static struct saa7146_pci_extension_data dpc = {
	.ext_priv = "Multimedia eXtension Board",
	.ext = &extension,
};

static struct pci_device_id pci_tbl[] = {
	{
		.vendor    = PCI_VENDOR_ID_PHILIPS,
		.device	   = PCI_DEVICE_ID_PHILIPS_SAA7146,
		.subvendor = 0x0000,
		.subdevice = 0x0000,
		.driver_data = (unsigned long)&dpc,
	}, {
		.vendor = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_ext_vv vv_data = {
	.inputs		= DPC_INPUTS,
	.capabilities	= V4L2_CAP_VBI_CAPTURE,
	.stds		= &standard[0],
	.num_stds	= sizeof(standard)/sizeof(struct saa7146_standard),
	.std_callback	= &std_callback,
	.ioctls		= &ioctls[0],
	.ioctl		= dpc_ioctl,
};

static struct saa7146_extension extension = {
	.name		= "dpc7146 demonstration board",
	.flags		= SAA7146_USE_I2C_IRQ,

	.pci_tbl	= &pci_tbl[0],
	.module		= THIS_MODULE,

	.probe		= dpc_probe,
	.attach		= dpc_attach,
	.detach		= dpc_detach,

	.irq_mask	= 0,
	.irq_func	= NULL,
};

static int __init dpc_init_module(void)
{
	if( 0 != saa7146_register_extension(&extension)) {
		DEB_S(("failed to register extension.\n"));
		return -ENODEV;
	}

	return 0;
}

static void __exit dpc_cleanup_module(void)
{
	saa7146_unregister_extension(&extension);
}

module_init(dpc_init_module);
module_exit(dpc_cleanup_module);

MODULE_DESCRIPTION("video4linux-2 driver for the 'dpc7146 demonstration board'");
MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_LICENSE("GPL");
