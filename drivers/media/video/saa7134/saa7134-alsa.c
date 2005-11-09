/*
 *
 *  Support for audio capture
 *  PCI function #1 of the saa7134
 *
 *  (c) 2005 Mauro Carvalho Chehab <mchehab@brturbo.com.br>
 *  (c) 2004 Gerd Knorr <kraxel@bytesex.org>
 *  (c) 2003 Clemens Ladisch <clemens@ladisch.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <asm/delay.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>

#include "saa7134.h"
#include "saa7134-reg.h"

#define dprintk(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/1: " fmt, chip->core->name , ## arg)


/****************************************************************************
	Data type declarations - Can be moded to a header file later
 ****************************************************************************/

#define ANALOG_CLOCK 1792000
#define CLOCK_DIV_MIN 4
#define CLOCK_DIV_MAX 15
#define MAX_PCM_DEVICES		4
#define MAX_PCM_SUBSTREAMS	16

enum { DEVICE_DIGITAL, DEVICE_ANALOG };

/* These can be replaced after done */
#define MIXER_ADDR_LAST MAX_saa7134_INPUT

struct saa7134_audio_dev {
	struct saa7134_core           *core;
        struct saa7134_buffer         *buf;
	struct saa7134_dmaqueue       q;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;

	/* audio controls */
	int                        irq;
	int                        dig_rate;		/* Digital sampling rate */

	snd_card_t                 *card;

	spinlock_t                 reg_lock;

	unsigned int               dma_size;
	unsigned int               period_size;

	int                        mixer_volume[MIXER_ADDR_LAST+1][2];
	int                        capture_source[MIXER_ADDR_LAST+1][2];

	long opened;
	snd_pcm_substream_t *substream;
};
typedef struct saa7134_audio_dev snd_saa7134_card_t;

/****************************************************************************
			Module global static vars
 ****************************************************************************/

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 1};

module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable saa7134x soundcard. default enabled.");

/****************************************************************************
				Module macros
 ****************************************************************************/

MODULE_DESCRIPTION("ALSA driver module for saa7134 based TV cards");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@brturbo.com.br>");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Philips, saa7131E},"
			"{{Philips, saa7134},"
			"{{Philips, saa7133}");
static unsigned int debug = 0;
module_param(debug,int,0644);
MODULE_PARM_DESC(debug,"enable debug messages");

/****************************************************************************
			Module specific funtions
 ****************************************************************************/

/*
 * BOARD Specific: Sets audio DMA
 */

int saa7134_start_audio_dma(snd_saa7134_card_t *chip)
{
	struct saa7134_core *core = chip->core;

	return 0;
}

/*
 * BOARD Specific: Resets audio DMA
 */
int saa7134_stop_audio_dma(snd_saa7134_card_t *chip)
{
	struct saa7134_core *core=chip->core;
	return 0;
}

#define MAX_IRQ_LOOP 10

static void saa713401_timeout(unsigned long data)
{
	snd_saa7134_card_t *chip = (snd_saa7134_card_t *)data;
}

/* FIXME: Wrong values*/
static char *saa7134_aud_irqs[32] = {
	"y_risci1", "u_risci1", "v_risci1", "vbi_risc1",
	"y_risci2", "u_risci2", "v_risci2", "vbi_risc2",
	"y_oflow",  "u_oflow",  "v_oflow",  "vbi_oflow",
	"y_sync",   "u_sync",   "v_sync",   "vbi_sync",
	"opc_err",  "par_err",  "rip_err",  "pci_abort",
};


static void saa713401_aud_irq(snd_saa7134_card_t *chip)
{
	struct saa7134_core *core = chip->core;
}

static irqreturn_t saa7134_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	snd_saa7134_card_t *chip = dev_id;
	struct saa7134_core *core = chip->core;
}

/****************************************************************************
				ALSA PCM Interface
 ****************************************************************************/

/*
 * Digital hardware definition
 */
static snd_pcm_hardware_t snd_saa7134_digital_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = 0, /* set at runtime */
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 255 * 4092,
	.period_bytes_min = 32,
	.period_bytes_max = 4092,
	.periods_min = 2,
	.periods_max = 255,
};

/*
 * Sets board to provide digital audio
 */
static int snd_saa7134_set_digital_hw(snd_saa7134_card_t *chip, snd_pcm_runtime_t *runtime)
{
	return 0;
}

/*
 * audio open callback
 */
static int snd_saa7134_pcm_open(snd_pcm_substream_t *substream)
{
	snd_saa7134_card_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if (test_and_set_bit(0, &chip->opened))
		return -EBUSY;

	err = snd_saa7134_set_digital_hw(chip, runtime);

	if (err < 0)
		goto _error;

	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		goto _error;

	chip->substream = substream;
	return 0;

_error:
	clear_bit(0, &chip->opened);
	smp_mb__after_clear_bit();
	return err;
}

/*
 * audio close callback
 */
static int snd_saa7134_close(snd_pcm_substream_t *substream)
{
	snd_saa7134_card_t *chip = snd_pcm_substream_chip(substream);

	chip->substream = NULL;
	clear_bit(0, &chip->opened);
	smp_mb__after_clear_bit();
	return 0;
}

/*
 * hw_params callback
 */
static int snd_saa7134_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

/*
 * hw free callback
 */
static int snd_saa7134_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/*
 * prepare callback
 */
static int snd_saa7134_prepare(snd_pcm_substream_t *substream)
{
	snd_saa7134_card_t *chip = snd_pcm_substream_chip(substream);
	return 0;
}


/*
 * trigger callback
 */
static int snd_saa7134_card_trigger(snd_pcm_substream_t *substream, int cmd)
{
	snd_saa7134_card_t *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		return snd_saa7134_start(chip);
	case SNDRV_PCM_TRIGGER_STOP:
		return snd_saa7134_stop(chip);
	default:
		return -EINVAL;
	}
}

/*
 * pointer callback
 */
static snd_pcm_uframes_t snd_saa7134_pointer(snd_pcm_substream_t *substream)
{
//	snd_saa7134_card_t *chip = snd_pcm_substream_chip(substream);
//	snd_pcm_runtime_t *runtime = substream->runtime;

//	return (snd_pcm_uframes_t)bytes_to_frames(runtime, chip->current_line * chip->line_bytes);
}

/*
 * operators
 */
static snd_pcm_ops_t snd_saa7134_pcm_ops = {
	.open = snd_saa7134_pcm_open,
	.close = snd_saa7134_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_saa7134_hw_params,
	.hw_free = snd_saa7134_hw_free,
	.prepare = snd_saa7134_prepare,
	.trigger = snd_saa7134_card_trigger,
	.pointer = snd_saa7134_pointer,
	.page = snd_pcm_sgbuf_ops_page,
};

/*
 * create a PCM device
 */
static int __devinit snd_saa7134_pcm(snd_saa7134_card_t *chip, int device, char *name)
{
	int err;
	snd_pcm_t *pcm;

	err = snd_pcm_new(chip->card, name, device, 0, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	strcpy(pcm->name, name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_saa7134_pcm_ops);
	return snd_pcm_lib_preallocate_pages_for_all(pcm,
						     SNDRV_DMA_TYPE_DEV_SG,
						     snd_dma_pci_data(chip->pci),
							128 * 1024,
							(255 * 4092 + 1023) & ~1023);
}

/****************************************************************************
				CONTROL INTERFACE
 ****************************************************************************/
static int snd_saa7134_capture_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 1;
	info->value.integer.min = 0;
	info->value.integer.max = 0x3f;

	return 0;
}

/* OK - TODO: test it */
static int snd_saa7134_capture_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *value)
{
	snd_saa7134_card_t *chip = snd_kcontrol_chip(kcontrol);
	struct saa7134_core *core=chip->core;

	return 0;
}

/* OK - TODO: test it */
static int snd_saa7134_capture_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *value)
{
	snd_saa7134_card_t *chip = snd_kcontrol_chip(kcontrol);
	struct saa7134_core *core=chip->core;

	return 0;
}

static snd_kcontrol_new_t snd_saa7134_capture_volume = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Volume",
	.info = snd_saa7134_capture_volume_info,
	.get = snd_saa7134_capture_volume_get,
	.put = snd_saa7134_capture_volume_put,
};

/*
 ***************************************
 */

/****************************************************************************
			Basic Flow for Sound Devices
 ****************************************************************************/

/*
 * PCI ID Table - 14f1:8801 and 14f1:8811 means function 1: Audio
 * Only boards with eeprom and byte 1 at eeprom=1 have it
 */

struct pci_device_id saa7134_audio_pci_tbl[] = {
	{0x14f1,0x8801,PCI_ANY_ID,PCI_ANY_ID,0,0,0},
	{0x14f1,0x8811,PCI_ANY_ID,PCI_ANY_ID,0,0,0},
	{0, }
};
MODULE_DEVICE_TABLE(pci, saa7134_audio_pci_tbl);

/*
 * Chip-specific destructor
 */

static int snd_saa7134_free(snd_saa7134_card_t *chip)
{
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);

	/* free memory */
	saa7134_core_put(chip->core,chip->pci);

	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);

	kfree(chip);
	return 0;
}

/*
 * Component Destructor
 */
static int snd_saa7134_dev_free(snd_device_t *device)
{
	snd_saa7134_card_t *chip = device->device_data;
	return snd_saa7134_free(chip);
}


/*
 * Alsa Constructor - Component probe
 */

static int devno=0;
static int __devinit snd_saa7134_create(snd_card_t *card, struct pci_dev *pci,
				    snd_saa7134_card_t **rchip)
{
	snd_saa7134_card_t   *chip;
	struct saa7134_core  *core;
	return 0;
}

static int __devinit saa7134_audio_initdev(struct pci_dev *pci,
				    const struct pci_device_id *pci_id)
{
	snd_card_t       *card;
	snd_saa7134_card_t  *chip;
	int              err;

	if (devno >= SNDRV_CARDS)
		return (-ENODEV);

	if (!enable[devno]) {
		++devno;
		return (-ENOENT);
	}

	card = snd_card_new(index[devno], id[devno], THIS_MODULE, 0);
	if (!card)
		return (-ENOMEM);

	err = snd_saa7134_create(card, pci, &chip);
	if (err < 0)
		return (err);

/*
	err = snd_saa7134_pcm(chip, DEVICE_DIGITAL, "saa7134 Digital");
	if (err < 0)
		goto fail_free;
*/
	err = snd_ctl_add(card, snd_ctl_new1(&snd_saa7134_capture_volume, chip));
	if (err < 0) {
		snd_card_free(card);
		return (err);
	}

	strcpy (card->driver, "saa7134_ALSA");
	sprintf(card->shortname, "Saa7134 %x", pci->device);
	sprintf(card->longname, "%s at %#lx",
		card->shortname, pci_resource_start(pci, 0));
	strcpy (card->mixername, "saa7134");

	dprintk (0, "%s/%i: Alsa support for saa7134x boards\n",
	       card->driver,devno);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return (err);
	}

	pci_set_drvdata(pci,card);

	devno++;
	return 0;
}
/*
 * ALSA destructor
 */
static void __devexit saa7134_audio_finidev(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);

	devno--;
}

/*
 * PCI driver definition
 */

static struct pci_driver saa7134_audio_pci_driver = {
        .name     = "saa7134_audio",
        .id_table = saa7134_audio_pci_tbl,
        .probe    = saa7134_audio_initdev,
        .remove   = saa7134_audio_finidev,
	SND_PCI_PM_CALLBACKS
};

/****************************************************************************
				LINUX MODULE INIT
 ****************************************************************************/

/*
 * module init
 */
static int saa7134_audio_init(void)
{
	printk(KERN_INFO "saa7134x alsa driver version %d.%d.%d loaded\n",
	       (saa7134_VERSION_CODE >> 16) & 0xff,
	       (saa7134_VERSION_CODE >>  8) & 0xff,
	        saa7134_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "saa7134x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return pci_module_init(&saa7134_audio_pci_driver);
}

/*
 * module remove
 */
static void saa7134_audio_fini(void)
{
	pci_unregister_driver(&saa7134_audio_pci_driver);
}

module_init(saa7134_audio_init);
module_exit(saa7134_audio_fini);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
