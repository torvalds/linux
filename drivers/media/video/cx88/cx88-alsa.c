/*
 *
 *  Support for audio capture
 *  PCI function #1 of the cx2388x.
 *
 *    (c) 2005,2006 Ricardo Cerqueira <v4l@cerqueira.org>
 *    (c) 2005 Mauro Carvalho Chehab <mchehab@infradead.org>
 *    Based on a dummy cx88 module by Gerd Knorr <kraxel@bytesex.org>
 *    Based on dummy.c by Jaroslav Kysela <perex@suse.cz>
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

#include "cx88.h"
#include "cx88-reg.h"

#define dprintk(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_INFO "%s/1: " fmt, chip->core->name , ## arg)

#define dprintk_core(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/1: " fmt, chip->core->name , ## arg)


/****************************************************************************
	Data type declarations - Can be moded to a header file later
 ****************************************************************************/

/* These can be replaced after done */
#define MIXER_ADDR_LAST MAX_CX88_INPUT

struct cx88_audio_dev {
	struct cx88_core           *core;
	struct cx88_dmaqueue       q;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;

	/* audio controls */
	int                        irq;

	struct snd_card            *card;

	spinlock_t                 reg_lock;

	unsigned int               dma_size;
	unsigned int               period_size;
	unsigned int               num_periods;

	struct videobuf_dmabuf dma_risc;

	int                        mixer_volume[MIXER_ADDR_LAST+1][2];
	int                        capture_source[MIXER_ADDR_LAST+1][2];

	long int read_count;
	long int read_offset;

	struct cx88_buffer   *buf;

	long opened;
	struct snd_pcm_substream *substream;

};
typedef struct cx88_audio_dev snd_cx88_card_t;



/****************************************************************************
			Module global static vars
 ****************************************************************************/

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 1};
static struct snd_card *snd_cx88_cards[SNDRV_CARDS];

module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable cx88x soundcard. default enabled.");

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for cx88x capture interface(s).");


/****************************************************************************
				Module macros
 ****************************************************************************/

MODULE_DESCRIPTION("ALSA driver module for cx2388x based TV cards");
MODULE_AUTHOR("Ricardo Cerqueira");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Conexant,23881},"
			"{{Conexant,23882},"
			"{{Conexant,23883}");
static unsigned int debug;
module_param(debug,int,0644);
MODULE_PARM_DESC(debug,"enable debug messages");

/****************************************************************************
			Module specific funtions
 ****************************************************************************/

/*
 * BOARD Specific: Sets audio DMA
 */

static int _cx88_start_audio_dma(snd_cx88_card_t *chip)
{
	struct cx88_buffer   *buf = chip->buf;
	struct cx88_core *core=chip->core;
	struct sram_channel *audio_ch = &cx88_sram_channels[SRAM_CH25];


	dprintk(1, "Starting audio DMA for %i bytes/line and %i (%i) lines at address %08x\n",buf->bpl, chip->num_periods, audio_ch->fifo_size / buf->bpl, audio_ch->fifo_start);

	/* setup fifo + format - out channel */
	cx88_sram_channel_setup(chip->core, &cx88_sram_channels[SRAM_CH25],
				buf->bpl, buf->risc.dma);

	/* sets bpl size */
	cx_write(MO_AUDD_LNGTH, buf->bpl);

	/* reset counter */
	cx_write(MO_AUDD_GPCNTRL,GP_COUNT_CONTROL_RESET);

	dprintk(1,"Enabling IRQ, setting mask from 0x%x to 0x%x\n",chip->core->pci_irqmask,(chip->core->pci_irqmask | 0x02));
	/* enable irqs */
	cx_set(MO_PCI_INTMSK, chip->core->pci_irqmask | 0x02);


	/* Enables corresponding bits at AUD_INT_STAT */
	cx_write(MO_AUD_INTMSK,
			(1<<16)|
			(1<<12)|
			(1<<4)|
			(1<<0)
			);

	/* start dma */
	cx_set(MO_DEV_CNTRL2, (1<<5)); /* Enables Risc Processor */
	cx_set(MO_AUD_DMACNTRL, 0x11); /* audio downstream FIFO and RISC enable */

	if (debug)
		cx88_sram_channel_dump(chip->core, &cx88_sram_channels[SRAM_CH25]);

	return 0;
}

/*
 * BOARD Specific: Resets audio DMA
 */
static int _cx88_stop_audio_dma(snd_cx88_card_t *chip)
{
	struct cx88_core *core=chip->core;
	dprintk(1, "Stopping audio DMA\n");

	/* stop dma */
	cx_clear(MO_AUD_DMACNTRL, 0x11);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, 0x02);
	cx_clear(MO_AUD_INTMSK,
			(1<<16)|
			(1<<12)|
			(1<<4)|
			(1<<0)
			);

	if (debug)
		cx88_sram_channel_dump(chip->core, &cx88_sram_channels[SRAM_CH25]);

	return 0;
}

#define MAX_IRQ_LOOP 10

/*
 * BOARD Specific: IRQ dma bits
 */
static char *cx88_aud_irqs[32] = {
	"dn_risci1", "up_risci1", "rds_dn_risc1", /* 0-2 */
	NULL,					  /* reserved */
	"dn_risci2", "up_risci2", "rds_dn_risc2", /* 4-6 */
	NULL,					  /* reserved */
	"dnf_of", "upf_uf", "rds_dnf_uf",	  /* 8-10 */
	NULL,					  /* reserved */
	"dn_sync", "up_sync", "rds_dn_sync",	  /* 12-14 */
	NULL,					  /* reserved */
	"opc_err", "par_err", "rip_err",	  /* 16-18 */
	"pci_abort", "ber_irq", "mchg_irq"	  /* 19-21 */
};

/*
 * BOARD Specific: Threats IRQ audio specific calls
 */
static void cx8801_aud_irq(snd_cx88_card_t *chip)
{
	struct cx88_core *core = chip->core;
	u32 status, mask;
	u32 count;

	status = cx_read(MO_AUD_INTSTAT);
	mask   = cx_read(MO_AUD_INTMSK);
	if (0 == (status & mask)) {
		spin_unlock(&chip->reg_lock);
		return;
	}
	cx_write(MO_AUD_INTSTAT, status);
	if (debug > 1  ||  (status & mask & ~0xff))
		cx88_print_irqbits(core->name, "irq aud",
				   cx88_aud_irqs, status, mask);
	/* risc op code error */
	if (status & (1 << 16)) {
		printk(KERN_WARNING "%s/0: audio risc op code error\n",core->name);
		cx_clear(MO_AUD_DMACNTRL, 0x11);
		cx88_sram_channel_dump(core, &cx88_sram_channels[SRAM_CH25]);
	}

	/* risc1 downstream */
	if (status & 0x01) {
		spin_lock(&chip->reg_lock);
		count = cx_read(MO_AUDD_GPCNT);
		spin_unlock(&chip->reg_lock);
		if (chip->read_count == 0)
			chip->read_count += chip->dma_size;
	}

	if  (chip->read_count >= chip->period_size) {
		dprintk(2, "Elapsing period\n");
		snd_pcm_period_elapsed(chip->substream);
	}

	dprintk(3,"Leaving audio IRQ handler...\n");

	/* FIXME: Any other status should deserve a special handling? */
}

/*
 * BOARD Specific: Handles IRQ calls
 */
static irqreturn_t cx8801_irq(int irq, void *dev_id)
{
	snd_cx88_card_t *chip = dev_id;
	struct cx88_core *core = chip->core;
	u32 status;
	int loop, handled = 0;

	for (loop = 0; loop < MAX_IRQ_LOOP; loop++) {
		status = cx_read(MO_PCI_INTSTAT) & (core->pci_irqmask | 0x02);
		if (0 == status)
			goto out;
		dprintk( 3, "cx8801_irq\n" );
		dprintk( 3, "    loop: %d/%d\n", loop, MAX_IRQ_LOOP );
		dprintk( 3, "    status: %d\n", status );
		handled = 1;
		cx_write(MO_PCI_INTSTAT, status);

		if (status & 0x02)
		{
			dprintk( 2, "    ALSA IRQ handling\n" );
			cx8801_aud_irq(chip);
		}
	};

	if (MAX_IRQ_LOOP == loop) {
		dprintk( 0, "clearing mask\n" );
		dprintk(1,"%s/0: irq loop -- clearing mask\n",
		       core->name);
		cx_clear(MO_PCI_INTMSK,0x02);
	}

 out:
	return IRQ_RETVAL(handled);
}


static int dsp_buffer_free(snd_cx88_card_t *chip)
{
	BUG_ON(!chip->dma_size);

	dprintk(2,"Freeing buffer\n");
	videobuf_pci_dma_unmap(chip->pci, &chip->dma_risc);
	videobuf_dma_free(&chip->dma_risc);
	btcx_riscmem_free(chip->pci,&chip->buf->risc);
	kfree(chip->buf);

	chip->dma_size = 0;

       return 0;
}

/****************************************************************************
				ALSA PCM Interface
 ****************************************************************************/

/*
 * Digital hardware definition
 */
static struct snd_pcm_hardware snd_cx88_digital_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = (2*2048),
	.period_bytes_min = 2048,
	.period_bytes_max = 2048,
	.periods_min = 2,
	.periods_max = 2,
};

/*
 * audio pcm capture runtime free
 */
static void snd_card_cx88_runtime_free(struct snd_pcm_runtime *runtime)
{
}
/*
 * audio pcm capture open callback
 */
static int snd_cx88_pcm_open(struct snd_pcm_substream *substream)
{
	snd_cx88_card_t *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	if (test_and_set_bit(0, &chip->opened))
		return -EBUSY;

	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		goto _error;

	chip->substream = substream;

	chip->read_count = 0;
	chip->read_offset = 0;

	runtime->private_free = snd_card_cx88_runtime_free;
	runtime->hw = snd_cx88_digital_hw;

	return 0;
_error:
	dprintk(1,"Error opening PCM!\n");
	clear_bit(0, &chip->opened);
	smp_mb__after_clear_bit();
	return err;
}

/*
 * audio close callback
 */
static int snd_cx88_close(struct snd_pcm_substream *substream)
{
	snd_cx88_card_t *chip = snd_pcm_substream_chip(substream);

	clear_bit(0, &chip->opened);
	smp_mb__after_clear_bit();

	return 0;
}

/*
 * hw_params callback
 */
static int snd_cx88_hw_params(struct snd_pcm_substream * substream,
			      struct snd_pcm_hw_params * hw_params)
{
	snd_cx88_card_t *chip = snd_pcm_substream_chip(substream);
	struct cx88_buffer *buf;

	if (substream->runtime->dma_area) {
		dsp_buffer_free(chip);
		substream->runtime->dma_area = NULL;
	}


	chip->period_size = params_period_bytes(hw_params);
	chip->num_periods = params_periods(hw_params);
	chip->dma_size = chip->period_size * params_periods(hw_params);

	BUG_ON(!chip->dma_size);

	dprintk(1,"Setting buffer\n");

	buf = kmalloc(sizeof(*buf),GFP_KERNEL);
	if (NULL == buf)
		return -ENOMEM;
	memset(buf,0,sizeof(*buf));


	buf->vb.memory = V4L2_MEMORY_MMAP;
	buf->vb.width  = chip->period_size;
	buf->vb.height = chip->num_periods;
	buf->vb.size   = chip->dma_size;
	buf->vb.field  = V4L2_FIELD_NONE;

	videobuf_dma_init(&buf->vb.dma);
	videobuf_dma_init_kernel(&buf->vb.dma,PCI_DMA_FROMDEVICE,
			(PAGE_ALIGN(buf->vb.size) >> PAGE_SHIFT));

	videobuf_pci_dma_map(chip->pci,&buf->vb.dma);


	cx88_risc_databuffer(chip->pci, &buf->risc,
			buf->vb.dma.sglist,
			buf->vb.width, buf->vb.height);

	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(buf->risc.dma);

	buf->vb.state = STATE_PREPARED;

	buf->bpl = chip->period_size;
	chip->buf = buf;
	chip->dma_risc = buf->vb.dma;

	dprintk(1,"Buffer ready at %u\n",chip->dma_risc.nr_pages);
	substream->runtime->dma_area = chip->dma_risc.vmalloc;
	return 0;
}

/*
 * hw free callback
 */
static int snd_cx88_hw_free(struct snd_pcm_substream * substream)
{

	snd_cx88_card_t *chip = snd_pcm_substream_chip(substream);

	if (substream->runtime->dma_area) {
		dsp_buffer_free(chip);
		substream->runtime->dma_area = NULL;
	}

	return 0;
}

/*
 * prepare callback
 */
static int snd_cx88_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}


/*
 * trigger callback
 */
static int snd_cx88_card_trigger(struct snd_pcm_substream *substream, int cmd)
{
	snd_cx88_card_t *chip = snd_pcm_substream_chip(substream);
	int err;

	spin_lock(&chip->reg_lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		err=_cx88_start_audio_dma(chip);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		err=_cx88_stop_audio_dma(chip);
		break;
	default:
		err=-EINVAL;
		break;
	}

	spin_unlock(&chip->reg_lock);

	return err;
}

/*
 * pointer callback
 */
static snd_pcm_uframes_t snd_cx88_pointer(struct snd_pcm_substream *substream)
{
	snd_cx88_card_t *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (chip->read_count) {
		chip->read_count -= snd_pcm_lib_period_bytes(substream);
		chip->read_offset += snd_pcm_lib_period_bytes(substream);
		if (chip->read_offset == chip->dma_size)
			chip->read_offset = 0;
	}

	dprintk(2, "Pointer time, will return %li, read %li\n",chip->read_offset,chip->read_count);
	return bytes_to_frames(runtime, chip->read_offset);

}

/*
 * operators
 */
static struct snd_pcm_ops snd_cx88_pcm_ops = {
	.open = snd_cx88_pcm_open,
	.close = snd_cx88_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_cx88_hw_params,
	.hw_free = snd_cx88_hw_free,
	.prepare = snd_cx88_prepare,
	.trigger = snd_cx88_card_trigger,
	.pointer = snd_cx88_pointer,
};

/*
 * create a PCM device
 */
static int __devinit snd_cx88_pcm(snd_cx88_card_t *chip, int device, char *name)
{
	int err;
	struct snd_pcm *pcm;

	err = snd_pcm_new(chip->card, name, device, 0, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	strcpy(pcm->name, name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cx88_pcm_ops);

	return 0;
}

/****************************************************************************
				CONTROL INTERFACE
 ****************************************************************************/
static int snd_cx88_capture_volume_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 1;
	info->value.integer.min = 0;
	info->value.integer.max = 0x3f;

	return 0;
}

/* OK - TODO: test it */
static int snd_cx88_capture_volume_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *value)
{
	snd_cx88_card_t *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core=chip->core;

	value->value.integer.value[0] = 0x3f - (cx_read(AUD_VOL_CTL) & 0x3f);

	return 0;
}

/* OK - TODO: test it */
static int snd_cx88_capture_volume_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *value)
{
	snd_cx88_card_t *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core=chip->core;
	int v;
	u32 old_control;

	spin_lock_irq(&chip->reg_lock);
	old_control = 0x3f - (cx_read(AUD_VOL_CTL) & 0x3f);
	v = 0x3f - (value->value.integer.value[0] & 0x3f);
	cx_andor(AUD_VOL_CTL, 0x3f, v);
	spin_unlock_irq(&chip->reg_lock);

	return v != old_control;
}

static struct snd_kcontrol_new snd_cx88_capture_volume = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Volume",
	.info = snd_cx88_capture_volume_info,
	.get = snd_cx88_capture_volume_get,
	.put = snd_cx88_capture_volume_put,
};


/****************************************************************************
			Basic Flow for Sound Devices
 ****************************************************************************/

/*
 * PCI ID Table - 14f1:8801 and 14f1:8811 means function 1: Audio
 * Only boards with eeprom and byte 1 at eeprom=1 have it
 */

static struct pci_device_id cx88_audio_pci_tbl[] __devinitdata = {
	{0x14f1,0x8801,PCI_ANY_ID,PCI_ANY_ID,0,0,0},
	{0x14f1,0x8811,PCI_ANY_ID,PCI_ANY_ID,0,0,0},
	{0, }
};
MODULE_DEVICE_TABLE(pci, cx88_audio_pci_tbl);

/*
 * Chip-specific destructor
 */

static int snd_cx88_free(snd_cx88_card_t *chip)
{

	if (chip->irq >= 0){
		synchronize_irq(chip->irq);
		free_irq(chip->irq, chip);
	}

	cx88_core_put(chip->core,chip->pci);

	pci_disable_device(chip->pci);
	return 0;
}

/*
 * Component Destructor
 */
static void snd_cx88_dev_free(struct snd_card * card)
{
	snd_cx88_card_t *chip = card->private_data;

	snd_cx88_free(chip);
}


/*
 * Alsa Constructor - Component probe
 */

static int devno;
static int __devinit snd_cx88_create(struct snd_card *card,
				     struct pci_dev *pci,
				     snd_cx88_card_t **rchip)
{
	snd_cx88_card_t   *chip;
	struct cx88_core  *core;
	int               err;

	*rchip = NULL;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	pci_set_master(pci);

	chip = (snd_cx88_card_t *) card->private_data;

	core = cx88_core_get(pci);
	if (NULL == core) {
		err = -EINVAL;
		kfree (chip);
		return err;
	}

	if (!pci_dma_supported(pci,0xffffffff)) {
		dprintk(0, "%s/1: Oops: no 32bit PCI DMA ???\n",core->name);
		err = -EIO;
		cx88_core_put(core,pci);
		return err;
	}


	/* pci init */
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	spin_lock_init(&chip->reg_lock);

	chip->core = core;

	/* get irq */
	err = request_irq(chip->pci->irq, cx8801_irq,
			  IRQF_SHARED | IRQF_DISABLED, chip->core->name, chip);
	if (err < 0) {
		dprintk(0, "%s: can't get IRQ %d\n",
		       chip->core->name, chip->pci->irq);
		return err;
	}

	/* print pci info */
	pci_read_config_byte(pci, PCI_CLASS_REVISION, &chip->pci_rev);
	pci_read_config_byte(pci, PCI_LATENCY_TIMER,  &chip->pci_lat);

	dprintk(1,"ALSA %s/%i: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%llx\n", core->name, devno,
	       pci_name(pci), chip->pci_rev, pci->irq,
	       chip->pci_lat,(unsigned long long)pci_resource_start(pci,0));

	chip->irq = pci->irq;
	synchronize_irq(chip->irq);

	snd_card_set_dev(card, &pci->dev);

	*rchip = chip;

	return 0;
}

static int __devinit cx88_audio_initdev(struct pci_dev *pci,
				    const struct pci_device_id *pci_id)
{
	struct snd_card  *card;
	snd_cx88_card_t  *chip;
	int              err;

	if (devno >= SNDRV_CARDS)
		return (-ENODEV);

	if (!enable[devno]) {
		++devno;
		return (-ENOENT);
	}

	card = snd_card_new(index[devno], id[devno], THIS_MODULE, sizeof(snd_cx88_card_t));
	if (!card)
		return (-ENOMEM);

	card->private_free = snd_cx88_dev_free;

	err = snd_cx88_create(card, pci, &chip);
	if (err < 0)
		return (err);

	err = snd_cx88_pcm(chip, 0, "CX88 Digital");

	if (err < 0) {
		snd_card_free(card);
		return (err);
	}

	err = snd_ctl_add(card, snd_ctl_new1(&snd_cx88_capture_volume, chip));
	if (err < 0) {
		snd_card_free(card);
		return (err);
	}

	strcpy (card->driver, "CX88x");
	sprintf(card->shortname, "Conexant CX%x", pci->device);
	sprintf(card->longname, "%s at %#llx",
		card->shortname,(unsigned long long)pci_resource_start(pci, 0));
	strcpy (card->mixername, "CX88");

	dprintk (0, "%s/%i: ALSA support for cx2388x boards\n",
	       card->driver,devno);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return (err);
	}
	snd_cx88_cards[devno] = card;

	pci_set_drvdata(pci,card);

	devno++;
	return 0;
}
/*
 * ALSA destructor
 */
static void __devexit cx88_audio_finidev(struct pci_dev *pci)
{
	struct cx88_audio_dev *card = pci_get_drvdata(pci);

	snd_card_free((void *)card);

	pci_set_drvdata(pci, NULL);

	devno--;
}

/*
 * PCI driver definition
 */

static struct pci_driver cx88_audio_pci_driver = {
	.name     = "cx88_audio",
	.id_table = cx88_audio_pci_tbl,
	.probe    = cx88_audio_initdev,
	.remove   = cx88_audio_finidev,
};

/****************************************************************************
				LINUX MODULE INIT
 ****************************************************************************/

/*
 * module init
 */
static int cx88_audio_init(void)
{
	printk(KERN_INFO "cx2388x alsa driver version %d.%d.%d loaded\n",
	       (CX88_VERSION_CODE >> 16) & 0xff,
	       (CX88_VERSION_CODE >>  8) & 0xff,
	       CX88_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "cx2388x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return pci_register_driver(&cx88_audio_pci_driver);
}

/*
 * module remove
 */
static void cx88_audio_fini(void)
{

	pci_unregister_driver(&cx88_audio_pci_driver);
}

module_init(cx88_audio_init);
module_exit(cx88_audio_fini);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
