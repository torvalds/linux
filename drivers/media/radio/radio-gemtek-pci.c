/*
 ***************************************************************************
 *
 *     radio-gemtek-pci.c - Gemtek PCI Radio driver
 *     (C) 2001 Vladimir Shebordaev <vshebordaev@mail.ru>
 *
 ***************************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 ***************************************************************************
 *
 *     Gemtek Corp still silently refuses to release any specifications
 *     of their multimedia devices, so the protocol still has to be
 *     reverse engineered.
 *
 *     The v4l code was inspired by Jonas Munsin's  Gemtek serial line
 *     radio device driver.
 *
 *     Please, let me know if this piece of code was useful :)
 *
 *     TODO: multiple device support and portability were not tested
 *
 *     Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 *
 ***************************************************************************
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/videodev2.h>
#include <linux/errno.h>
#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#include <linux/io.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("Vladimir Shebordaev <vshebordaev@mail.ru>");
MODULE_DESCRIPTION("The video4linux driver for the Gemtek PCI Radio Card");
MODULE_LICENSE("GPL");

static int nr_radio = -1;
static int mx = 1;

module_param(mx, bool, 0);
MODULE_PARM_DESC(mx, "single digit: 1 - turn off the turner upon module exit (default), 0 - do not");
module_param(nr_radio, int, 0);
MODULE_PARM_DESC(nr_radio, "video4linux device number to use");

#define RADIO_VERSION KERNEL_VERSION(0, 0, 2)

#ifndef PCI_VENDOR_ID_GEMTEK
#define PCI_VENDOR_ID_GEMTEK 0x5046
#endif

#ifndef PCI_DEVICE_ID_GEMTEK_PR103
#define PCI_DEVICE_ID_GEMTEK_PR103 0x1001
#endif

#ifndef GEMTEK_PCI_RANGE_LOW
#define GEMTEK_PCI_RANGE_LOW (87*16000)
#endif

#ifndef GEMTEK_PCI_RANGE_HIGH
#define GEMTEK_PCI_RANGE_HIGH (108*16000)
#endif

struct gemtek_pci {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct mutex lock;
	struct pci_dev *pdev;

	u32 iobase;
	u32 length;

	u32 current_frequency;
	u8  mute;
};

static inline struct gemtek_pci *to_gemtek_pci(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct gemtek_pci, v4l2_dev);
}

static inline u8 gemtek_pci_out(u16 value, u32 port)
{
	outw(value, port);

	return (u8)value;
}

#define _b0(v) (*((u8 *)&v))

static void __gemtek_pci_cmd(u16 value, u32 port, u8 *last_byte, int keep)
{
	u8 byte = *last_byte;

	if (!value) {
		if (!keep)
			value = (u16)port;
		byte &= 0xfd;
	} else
		byte |= 2;

	_b0(value) = byte;
	outw(value, port);
	byte |= 1;
	_b0(value) = byte;
	outw(value, port);
	byte &= 0xfe;
	_b0(value) = byte;
	outw(value, port);

	*last_byte = byte;
}

static inline void gemtek_pci_nil(u32 port, u8 *last_byte)
{
	__gemtek_pci_cmd(0x00, port, last_byte, false);
}

static inline void gemtek_pci_cmd(u16 cmd, u32 port, u8 *last_byte)
{
	__gemtek_pci_cmd(cmd, port, last_byte, true);
}

static void gemtek_pci_setfrequency(struct gemtek_pci *card, unsigned long frequency)
{
	int i;
	u32 value = frequency / 200 + 856;
	u16 mask = 0x8000;
	u8 last_byte;
	u32 port = card->iobase;

	mutex_lock(&card->lock);
	card->current_frequency = frequency;
	last_byte = gemtek_pci_out(0x06, port);

	i = 0;
	do {
		gemtek_pci_nil(port, &last_byte);
		i++;
	} while (i < 9);

	i = 0;
	do {
		gemtek_pci_cmd(value & mask, port, &last_byte);
		mask >>= 1;
		i++;
	} while (i < 16);

	outw(0x10, port);
	mutex_unlock(&card->lock);
}


static void gemtek_pci_mute(struct gemtek_pci *card)
{
	mutex_lock(&card->lock);
	outb(0x1f, card->iobase);
	card->mute = true;
	mutex_unlock(&card->lock);
}

static void gemtek_pci_unmute(struct gemtek_pci *card)
{
	if (card->mute) {
		gemtek_pci_setfrequency(card, card->current_frequency);
		card->mute = false;
	}
}

static int gemtek_pci_getsignal(struct gemtek_pci *card)
{
	int sig;

	mutex_lock(&card->lock);
	sig = (inb(card->iobase) & 0x08) ? 0 : 1;
	mutex_unlock(&card->lock);
	return sig;
}

static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	struct gemtek_pci *card = video_drvdata(file);

	strlcpy(v->driver, "radio-gemtek-pci", sizeof(v->driver));
	strlcpy(v->card, "GemTek PCI Radio", sizeof(v->card));
	snprintf(v->bus_info, sizeof(v->bus_info), "PCI:%s", pci_name(card->pdev));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct gemtek_pci *card = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = GEMTEK_PCI_RANGE_LOW;
	v->rangehigh = GEMTEK_PCI_RANGE_HIGH;
	v->rxsubchans = V4L2_TUNER_SUB_MONO;
	v->capability = V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xffff * gemtek_pci_getsignal(card);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	return v->index ? -EINVAL : 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct gemtek_pci *card = video_drvdata(file);

	if (f->frequency < GEMTEK_PCI_RANGE_LOW ||
	    f->frequency > GEMTEK_PCI_RANGE_HIGH)
		return -EINVAL;
	gemtek_pci_setfrequency(card, f->frequency);
	card->mute = false;
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct gemtek_pci *card = video_drvdata(file);

	f->type = V4L2_TUNER_RADIO;
	f->frequency = card->current_frequency;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
	case V4L2_CID_AUDIO_VOLUME:
		return v4l2_ctrl_query_fill(qc, 0, 65535, 65535, 65535);
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct gemtek_pci *card = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = card->mute;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		if (card->mute)
			ctrl->value = 0;
		else
			ctrl->value = 65535;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct gemtek_pci *card = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			gemtek_pci_mute(card);
		else
			gemtek_pci_unmute(card);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		if (ctrl->value)
			gemtek_pci_unmute(card);
		else
			gemtek_pci_mute(card);
		return 0;
	}
	return -EINVAL;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int vidioc_g_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	a->index = 0;
	strlcpy(a->name, "Radio", sizeof(a->name));
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	return a->index ? -EINVAL : 0;
}

enum {
	GEMTEK_PR103
};

static char *card_names[] __devinitdata = {
	"GEMTEK_PR103"
};

static struct pci_device_id gemtek_pci_id[] =
{
	{ PCI_VENDOR_ID_GEMTEK, PCI_DEVICE_ID_GEMTEK_PR103,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, GEMTEK_PR103 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, gemtek_pci_id);

static const struct v4l2_file_operations gemtek_pci_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops gemtek_pci_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
};

static int __devinit gemtek_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct gemtek_pci *card;
	struct v4l2_device *v4l2_dev;
	int res;

	card = kzalloc(sizeof(struct gemtek_pci), GFP_KERNEL);
	if (card == NULL) {
		dev_err(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	v4l2_dev = &card->v4l2_dev;
	mutex_init(&card->lock);
	card->pdev = pdev;

	strlcpy(v4l2_dev->name, "gemtek_pci", sizeof(v4l2_dev->name));

	res = v4l2_device_register(&pdev->dev, v4l2_dev);
	if (res < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		kfree(card);
		return res;
	}

	if (pci_enable_device(pdev))
		goto err_pci;

	card->iobase = pci_resource_start(pdev, 0);
	card->length = pci_resource_len(pdev, 0);

	if (request_region(card->iobase, card->length, card_names[pci_id->driver_data]) == NULL) {
		v4l2_err(v4l2_dev, "i/o port already in use\n");
		goto err_pci;
	}

	strlcpy(card->vdev.name, v4l2_dev->name, sizeof(card->vdev.name));
	card->vdev.v4l2_dev = v4l2_dev;
	card->vdev.fops = &gemtek_pci_fops;
	card->vdev.ioctl_ops = &gemtek_pci_ioctl_ops;
	card->vdev.release = video_device_release_empty;
	video_set_drvdata(&card->vdev, card);

	if (video_register_device(&card->vdev, VFL_TYPE_RADIO, nr_radio) < 0)
		goto err_video;

	gemtek_pci_mute(card);

	v4l2_info(v4l2_dev, "Gemtek PCI Radio (rev. %d) found at 0x%04x-0x%04x.\n",
		pdev->revision, card->iobase, card->iobase + card->length - 1);

	return 0;

err_video:
	release_region(card->iobase, card->length);

err_pci:
	v4l2_device_unregister(v4l2_dev);
	kfree(card);
	return -ENODEV;
}

static void __devexit gemtek_pci_remove(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct gemtek_pci *card = to_gemtek_pci(v4l2_dev);

	video_unregister_device(&card->vdev);
	v4l2_device_unregister(v4l2_dev);

	release_region(card->iobase, card->length);

	if (mx)
		gemtek_pci_mute(card);

	kfree(card);
}

static struct pci_driver gemtek_pci_driver = {
	.name		= "gemtek_pci",
	.id_table	= gemtek_pci_id,
	.probe		= gemtek_pci_probe,
	.remove		= __devexit_p(gemtek_pci_remove),
};

static int __init gemtek_pci_init(void)
{
	return pci_register_driver(&gemtek_pci_driver);
}

static void __exit gemtek_pci_exit(void)
{
	pci_unregister_driver(&gemtek_pci_driver);
}

module_init(gemtek_pci_init);
module_exit(gemtek_pci_exit);
