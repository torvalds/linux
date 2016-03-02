/*
 * Guillemot Maxi Radio FM 2000 PCI radio card driver for Linux
 * (C) 2001 Dimitromanolakis Apostolos <apdim@grecian.net>
 *
 * Based in the radio Maestro PCI driver. Actually it uses the same chip
 * for radio but different pci controller.
 *
 * I didn't have any specs I reversed engineered the protocol from
 * the windows driver (radio.dll).
 *
 * The card uses the TEA5757 chip that includes a search function but it
 * is useless as I haven't found any way to read back the frequency. If
 * anybody does please mail me.
 *
 * For the pdf file see:
 * http://www.nxp.com/acrobat_download2/expired_datasheets/TEA5757_5759_3.pdf 
 *
 *
 * CHANGES:
 *   0.75b
 *     - better pci interface thanks to Francois Romieu <romieu@cogenit.fr>
 *
 *   0.75      Sun Feb  4 22:51:27 EET 2001
 *     - tiding up
 *     - removed support for multiple devices as it didn't work anyway
 *
 * BUGS:
 *   - card unmutes if you change frequency
 *
 * (c) 2006, 2007 by Mauro Carvalho Chehab <mchehab@infradead.org>:
 *	- Conversion to V4L2 API
 *      - Uses video_ioctl2 for parsing and to add debug support
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <media/drv-intf/tea575x.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>

MODULE_AUTHOR("Dimitromanolakis Apostolos, apdim@grecian.net");
MODULE_DESCRIPTION("Radio driver for the Guillemot Maxi Radio FM2000.");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

static int radio_nr = -1;
module_param(radio_nr, int, 0644);
MODULE_PARM_DESC(radio_nr, "Radio device number");

/* TEA5757 pin mappings */
static const int clk = 1, data = 2, wren = 4, mo_st = 8, power = 16;

static atomic_t maxiradio_instance = ATOMIC_INIT(0);

#define PCI_VENDOR_ID_GUILLEMOT 0x5046
#define PCI_DEVICE_ID_GUILLEMOT_MAXIRADIO 0x1001

struct maxiradio
{
	struct snd_tea575x tea;
	struct v4l2_device v4l2_dev;
	struct pci_dev *pdev;

	u16	io;	/* base of radio io */
};

static inline struct maxiradio *to_maxiradio(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct maxiradio, v4l2_dev);
}

static void maxiradio_tea575x_set_pins(struct snd_tea575x *tea, u8 pins)
{
	struct maxiradio *dev = tea->private_data;
	u8 bits = 0;

	bits |= (pins & TEA575X_DATA) ? data : 0;
	bits |= (pins & TEA575X_CLK)  ? clk  : 0;
	bits |= (pins & TEA575X_WREN) ? wren : 0;
	bits |= power;

	outb(bits, dev->io);
}

/* Note: this card cannot read out the data of the shift registers,
   only the mono/stereo pin works. */
static u8 maxiradio_tea575x_get_pins(struct snd_tea575x *tea)
{
	struct maxiradio *dev = tea->private_data;
	u8 bits = inb(dev->io);

	return  ((bits & data) ? TEA575X_DATA : 0) |
		((bits & mo_st) ? TEA575X_MOST : 0);
}

static void maxiradio_tea575x_set_direction(struct snd_tea575x *tea, bool output)
{
}

static const struct snd_tea575x_ops maxiradio_tea_ops = {
	.set_pins = maxiradio_tea575x_set_pins,
	.get_pins = maxiradio_tea575x_get_pins,
	.set_direction = maxiradio_tea575x_set_direction,
};

static int maxiradio_probe(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	struct maxiradio *dev;
	struct v4l2_device *v4l2_dev;
	int retval = -ENOMEM;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&pdev->dev, "not enough memory\n");
		return -ENOMEM;
	}

	v4l2_dev = &dev->v4l2_dev;
	v4l2_device_set_name(v4l2_dev, "maxiradio", &maxiradio_instance);

	retval = v4l2_device_register(&pdev->dev, v4l2_dev);
	if (retval < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		goto errfr;
	}
	dev->tea.private_data = dev;
	dev->tea.ops = &maxiradio_tea_ops;
	/* The data pin cannot be read. This may be a hardware limitation, or
	   we just don't know how to read it. */
	dev->tea.cannot_read_data = true;
	dev->tea.v4l2_dev = v4l2_dev;
	dev->tea.radio_nr = radio_nr;
	strlcpy(dev->tea.card, "Maxi Radio FM2000", sizeof(dev->tea.card));
	snprintf(dev->tea.bus_info, sizeof(dev->tea.bus_info),
			"PCI:%s", pci_name(pdev));

	retval = -ENODEV;

	if (!request_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0), v4l2_dev->name)) {
		dev_err(&pdev->dev, "can't reserve I/O ports\n");
		goto err_hdl;
	}

	if (pci_enable_device(pdev))
		goto err_out_free_region;

	dev->io = pci_resource_start(pdev, 0);
	if (snd_tea575x_init(&dev->tea, THIS_MODULE)) {
		printk(KERN_ERR "radio-maxiradio: Unable to detect TEA575x tuner\n");
		goto err_out_free_region;
	}
	return 0;

err_out_free_region:
	release_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
err_hdl:
	v4l2_device_unregister(v4l2_dev);
errfr:
	kfree(dev);
	return retval;
}

static void maxiradio_remove(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct maxiradio *dev = to_maxiradio(v4l2_dev);

	snd_tea575x_exit(&dev->tea);
	/* Turn off power */
	outb(0, dev->io);
	v4l2_device_unregister(v4l2_dev);
	release_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
}

static struct pci_device_id maxiradio_pci_tbl[] = {
	{ PCI_VENDOR_ID_GUILLEMOT, PCI_DEVICE_ID_GUILLEMOT_MAXIRADIO,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, maxiradio_pci_tbl);

static struct pci_driver maxiradio_driver = {
	.name		= "radio-maxiradio",
	.id_table	= maxiradio_pci_tbl,
	.probe		= maxiradio_probe,
	.remove		= maxiradio_remove,
};

module_pci_driver(maxiradio_driver);
