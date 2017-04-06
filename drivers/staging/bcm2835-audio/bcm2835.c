/*****************************************************************************
 * Copyright 2011 Broadcom Corporation.  All rights reserved.
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available at
 * http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *****************************************************************************/

#include <linux/platform_device.h>

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>

#include "bcm2835.h"

/* HACKY global pointers needed for successive probes to work : ssp
 * But compared against the changes we will have to do in VC audio_ipc code
 * to export 8 audio_ipc devices as a single IPC device and then monitor all
 * four devices in a thread, this gets things done quickly and should be easier
 * to debug if we run into issues
 */

static struct snd_card *g_card;
static struct bcm2835_chip *g_chip;

static int snd_bcm2835_free(struct bcm2835_chip *chip)
{
	kfree(chip);
	return 0;
}

/* component-destructor
 * (see "Management of Cards and Components")
 */
static int snd_bcm2835_dev_free(struct snd_device *device)
{
	return snd_bcm2835_free(device->device_data);
}

/* chip-specific constructor
 * (see "Management of Cards and Components")
 */
static int snd_bcm2835_create(struct snd_card *card,
			      struct platform_device *pdev,
			      struct bcm2835_chip **rchip)
{
	struct bcm2835_chip *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free = snd_bcm2835_dev_free,
	};

	*rchip = NULL;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->card = card;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		snd_bcm2835_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

static int snd_bcm2835_alsa_probe_dt(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm2835_chip *chip;
	struct snd_card *card;
	u32 numchans;
	int err, i;

	err = of_property_read_u32(dev->of_node, "brcm,pwm-channels",
				   &numchans);
	if (err) {
		dev_err(dev, "Failed to get DT property 'brcm,pwm-channels'");
		return err;
	}

	if (numchans == 0 || numchans > MAX_SUBSTREAMS) {
		numchans = MAX_SUBSTREAMS;
		dev_warn(dev, "Illegal 'brcm,pwm-channels' value, will use %u\n",
			 numchans);
	}

	err = snd_card_new(&pdev->dev, -1, NULL, THIS_MODULE, 0, &card);
	if (err) {
		dev_err(dev, "Failed to create soundcard structure\n");
		return err;
	}

	snd_card_set_dev(card, dev);
	strcpy(card->driver, "bcm2835");
	strcpy(card->shortname, "bcm2835 ALSA");
	sprintf(card->longname, "%s", card->shortname);

	err = snd_bcm2835_create(card, pdev, &chip);
	if (err < 0) {
		dev_err(dev, "Failed to create bcm2835 chip\n");
		goto err_free;
	}

	err = snd_bcm2835_new_pcm(chip);
	if (err < 0) {
		dev_err(dev, "Failed to create new bcm2835 pcm device\n");
		goto err_free;
	}

	err = snd_bcm2835_new_spdif_pcm(chip);
	if (err < 0) {
		dev_err(dev, "Failed to create new bcm2835 spdif pcm device\n");
		goto err_free;
	}

	err = snd_bcm2835_new_ctl(chip);
	if (err < 0) {
		dev_err(dev, "Failed to create new bcm2835 ctl\n");
		goto err_free;
	}

	for (i = 0; i < numchans; i++) {
		chip->avail_substreams |= (1 << i);
		chip->pdev[i] = pdev;
	}

	err = snd_card_register(card);
	if (err) {
		dev_err(dev, "Failed to register bcm2835 ALSA card\n");
		goto err_free;
	}

	g_card = card;
	g_chip = chip;
	platform_set_drvdata(pdev, card);
	audio_info("bcm2835 ALSA card created with %u channels\n", numchans);

	return 0;

err_free:
	snd_card_free(card);

	return err;
}

static int snd_bcm2835_alsa_remove(struct platform_device *pdev)
{
	int idx;
	void *drv_data;

	drv_data = platform_get_drvdata(pdev);

	if (drv_data == (void *)g_card) {
		/* This is the card device */
		snd_card_free((struct snd_card *)drv_data);
		g_card = NULL;
		g_chip = NULL;
	} else {
		idx = (int)(long)drv_data;
		if (g_card) {
			BUG_ON(!g_chip);
			/* We pass chip device numbers in audio ipc devices
			 * other than the one we registered our card with
			 */
			idx = (int)(long)drv_data;
			BUG_ON(!idx || idx > MAX_SUBSTREAMS);
			g_chip->avail_substreams &= ~(1 << idx);
			/* There should be atleast one substream registered
			 * after we are done here, as it wil be removed when
			 * the *remove* is called for the card device
			 */
			BUG_ON(!g_chip->avail_substreams);
		}
	}

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM

static int snd_bcm2835_alsa_suspend(struct platform_device *pdev,
				    pm_message_t state)
{
	return 0;
}

static int snd_bcm2835_alsa_resume(struct platform_device *pdev)
{
	return 0;
}

#endif

static const struct of_device_id snd_bcm2835_of_match_table[] = {
	{ .compatible = "brcm,bcm2835-audio",},
	{},
};
MODULE_DEVICE_TABLE(of, snd_bcm2835_of_match_table);

static struct platform_driver bcm2835_alsa0_driver = {
	.probe = snd_bcm2835_alsa_probe_dt,
	.remove = snd_bcm2835_alsa_remove,
#ifdef CONFIG_PM
	.suspend = snd_bcm2835_alsa_suspend,
	.resume = snd_bcm2835_alsa_resume,
#endif
	.driver = {
		.name = "bcm2835_AUD0",
		.owner = THIS_MODULE,
		.of_match_table = snd_bcm2835_of_match_table,
	},
};

static int bcm2835_alsa_device_init(void)
{
	int retval;

	retval = platform_driver_register(&bcm2835_alsa0_driver);
	if (retval)
		pr_err("Error registering bcm2835_alsa0_driver %d .\n", retval);

	return retval;
}

static void bcm2835_alsa_device_exit(void)
{
	platform_driver_unregister(&bcm2835_alsa0_driver);
}

late_initcall(bcm2835_alsa_device_init);
module_exit(bcm2835_alsa_device_exit);

MODULE_AUTHOR("Dom Cobley");
MODULE_DESCRIPTION("Alsa driver for BCM2835 chip");
MODULE_LICENSE("GPL");
