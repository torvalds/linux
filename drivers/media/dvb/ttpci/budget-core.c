/*
 * budget-core.c: driver for the SAA7146 based Budget DVB cards
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de>
 *
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *			 & Marcus Metzler for convergence integrated media GmbH
 *
 * 26feb2004 Support for FS Activy Card (Grundig tuner) by
 *	     Michael Dreher <michael@5dot1.de>,
 *	     Oliver Endriss <o.endriss@gmx.de>,
 *	     Andreas 'randy' Weinberger
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include <linux/moduleparam.h>

#include "budget.h"
#include "ttpci-eeprom.h"

int budget_debug;
module_param_named(debug, budget_debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off budget debugging (default:off).");

/****************************************************************************
 * TT budget / WinTV Nova
 ****************************************************************************/

static int stop_ts_capture(struct budget *budget)
{
	dprintk(2, "budget: %p\n", budget);

	if (--budget->feeding)
		return budget->feeding;

	saa7146_write(budget->dev, MC1, MASK_20);	// DMA3 off
	SAA7146_IER_DISABLE(budget->dev, MASK_10);
	return 0;
}

static int start_ts_capture(struct budget *budget)
{
	struct saa7146_dev *dev = budget->dev;

	dprintk(2, "budget: %p\n", budget);

	if (budget->feeding)
		return ++budget->feeding;

	saa7146_write(dev, MC1, MASK_20);	// DMA3 off

	memset(budget->grabbing, 0x00, TS_HEIGHT * TS_WIDTH);

	saa7146_write(dev, PCI_BT_V1, 0x001c0000 | (saa7146_read(dev, PCI_BT_V1) & ~0x001f0000));

	budget->tsf = 0xff;
	budget->ttbp = 0;

	/*
	 *  Signal path on the Activy:
	 *
	 *  tuner -> SAA7146 port A -> SAA7146 BRS -> SAA7146 DMA3 -> memory
	 *
	 *  Since the tuner feeds 204 bytes packets into the SAA7146,
	 *  DMA3 is configured to strip the trailing 16 FEC bytes:
	 *      Pitch: 188, NumBytes3: 188, NumLines3: 1024
	 */

        switch(budget->card->type) {
	case BUDGET_FS_ACTIVY:
		saa7146_write(dev, DD1_INIT, 0x04000000);
		saa7146_write(dev, MC2, (MASK_09 | MASK_25));
		saa7146_write(dev, BRS_CTRL, 0x00000000);
		break;
	case BUDGET_PATCH:
		saa7146_write(dev, DD1_INIT, 0x00000200);
		saa7146_write(dev, MC2, (MASK_10 | MASK_26));
		saa7146_write(dev, BRS_CTRL, 0x60000000);
		break;
	default:
		if (budget->video_port == BUDGET_VIDEO_PORTA) {
			saa7146_write(dev, DD1_INIT, 0x06000200);
			saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));
			saa7146_write(dev, BRS_CTRL, 0x00000000);
		} else {
			saa7146_write(dev, DD1_INIT, 0x02000600);
			saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));
			saa7146_write(dev, BRS_CTRL, 0x60000000);
		}
	}

	saa7146_write(dev, MC2, (MASK_08 | MASK_24));
	mdelay(10);

	saa7146_write(dev, BASE_ODD3, 0);
	saa7146_write(dev, BASE_EVEN3, 0);
	saa7146_write(dev, PROT_ADDR3, TS_WIDTH * TS_HEIGHT);
	saa7146_write(dev, BASE_PAGE3, budget->pt.dma | ME1 | 0x90);

	if (budget->card->type == BUDGET_FS_ACTIVY) {
		saa7146_write(dev, PITCH3, TS_WIDTH / 2);
		saa7146_write(dev, NUM_LINE_BYTE3, ((TS_HEIGHT * 2) << 16) | (TS_WIDTH / 2));
	} else {
		saa7146_write(dev, PITCH3, TS_WIDTH);
		saa7146_write(dev, NUM_LINE_BYTE3, (TS_HEIGHT << 16) | TS_WIDTH);
	}

	saa7146_write(dev, MC2, (MASK_04 | MASK_20));

	SAA7146_ISR_CLEAR(budget->dev, MASK_10);	/* VPE */
	SAA7146_IER_ENABLE(budget->dev, MASK_10);	/* VPE */
	saa7146_write(dev, MC1, (MASK_04 | MASK_20));	/* DMA3 on */

	return ++budget->feeding;
}

static void vpeirq(unsigned long data)
{
	struct budget *budget = (struct budget *) data;
	u8 *mem = (u8 *) (budget->grabbing);
	u32 olddma = budget->ttbp;
	u32 newdma = saa7146_read(budget->dev, PCI_VDP3);

	/* nearest lower position divisible by 188 */
	newdma -= newdma % 188;

	if (newdma >= TS_BUFLEN)
		return;

	budget->ttbp = newdma;

	if (budget->feeding == 0 || newdma == olddma)
		return;

	if (newdma > olddma) {	/* no wraparound, dump olddma..newdma */
		dvb_dmx_swfilter_packets(&budget->demux, mem + olddma, (newdma - olddma) / 188);
	} else {		/* wraparound, dump olddma..buflen and 0..newdma */
		dvb_dmx_swfilter_packets(&budget->demux, mem + olddma, (TS_BUFLEN - olddma) / 188);
		dvb_dmx_swfilter_packets(&budget->demux, mem, newdma / 188);
	}
}


int ttpci_budget_debiread(struct budget *budget, u32 config, int addr, int count,
			  int uselocks, int nobusyloop)
{
	struct saa7146_dev *saa = budget->dev;
	int result = 0;
	unsigned long flags = 0;

	if (count > 4 || count <= 0)
		return 0;

	if (uselocks)
		spin_lock_irqsave(&budget->debilock, flags);

	if ((result = saa7146_wait_for_debi_done(saa, nobusyloop)) < 0) {
		if (uselocks)
			spin_unlock_irqrestore(&budget->debilock, flags);
		return result;
	}

	saa7146_write(saa, DEBI_COMMAND, (count << 17) | 0x10000 | (addr & 0xffff));
	saa7146_write(saa, DEBI_CONFIG, config);
	saa7146_write(saa, DEBI_PAGE, 0);
	saa7146_write(saa, MC2, (2 << 16) | 2);

	if ((result = saa7146_wait_for_debi_done(saa, nobusyloop)) < 0) {
		if (uselocks)
			spin_unlock_irqrestore(&budget->debilock, flags);
		return result;
	}

	result = saa7146_read(saa, DEBI_AD);
	result &= (0xffffffffUL >> ((4 - count) * 8));

	if (uselocks)
		spin_unlock_irqrestore(&budget->debilock, flags);

	return result;
}

int ttpci_budget_debiwrite(struct budget *budget, u32 config, int addr,
			   int count, u32 value, int uselocks, int nobusyloop)
{
	struct saa7146_dev *saa = budget->dev;
	unsigned long flags = 0;
	int result;

	if (count > 4 || count <= 0)
		return 0;

	if (uselocks)
		spin_lock_irqsave(&budget->debilock, flags);

	if ((result = saa7146_wait_for_debi_done(saa, nobusyloop)) < 0) {
		if (uselocks)
			spin_unlock_irqrestore(&budget->debilock, flags);
		return result;
	}

	saa7146_write(saa, DEBI_COMMAND, (count << 17) | 0x00000 | (addr & 0xffff));
	saa7146_write(saa, DEBI_CONFIG, config);
	saa7146_write(saa, DEBI_PAGE, 0);
	saa7146_write(saa, DEBI_AD, value);
	saa7146_write(saa, MC2, (2 << 16) | 2);

	if ((result = saa7146_wait_for_debi_done(saa, nobusyloop)) < 0) {
		if (uselocks)
			spin_unlock_irqrestore(&budget->debilock, flags);
		return result;
	}

	if (uselocks)
		spin_unlock_irqrestore(&budget->debilock, flags);
	return 0;
}


/****************************************************************************
 * DVB API SECTION
 ****************************************************************************/

static int budget_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct budget *budget = (struct budget *) demux->priv;
	int status;

	dprintk(2, "budget: %p\n", budget);

	if (!demux->dmx.frontend)
		return -EINVAL;

	spin_lock(&budget->feedlock);
	feed->pusi_seen = 0; /* have a clean section start */
	status = start_ts_capture(budget);
	spin_unlock(&budget->feedlock);
	return status;
}

static int budget_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct budget *budget = (struct budget *) demux->priv;
	int status;

	dprintk(2, "budget: %p\n", budget);

	spin_lock(&budget->feedlock);
	status = stop_ts_capture(budget);
	spin_unlock(&budget->feedlock);
	return status;
}

static int budget_register(struct budget *budget)
{
	struct dvb_demux *dvbdemux = &budget->demux;
	int ret;

	dprintk(2, "budget: %p\n", budget);

	dvbdemux->priv = (void *) budget;

	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	dvbdemux->start_feed = budget_start_feed;
	dvbdemux->stop_feed = budget_stop_feed;
	dvbdemux->write_to_decoder = NULL;

	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING | DMX_SECTION_FILTERING |
				      DMX_MEMORY_BASED_FILTERING);

	dvb_dmx_init(&budget->demux);

	budget->dmxdev.filternum = 256;
	budget->dmxdev.demux = &dvbdemux->dmx;
	budget->dmxdev.capabilities = 0;

	dvb_dmxdev_init(&budget->dmxdev, &budget->dvb_adapter);

	budget->hw_frontend.source = DMX_FRONTEND_0;

	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &budget->hw_frontend);

	if (ret < 0)
		return ret;

	budget->mem_frontend.source = DMX_MEMORY_FE;
	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &budget->mem_frontend);
	if (ret < 0)
		return ret;

	ret = dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, &budget->hw_frontend);
	if (ret < 0)
		return ret;

	dvb_net_init(&budget->dvb_adapter, &budget->dvb_net, &dvbdemux->dmx);

	return 0;
}

static void budget_unregister(struct budget *budget)
{
	struct dvb_demux *dvbdemux = &budget->demux;

	dprintk(2, "budget: %p\n", budget);

	dvb_net_release(&budget->dvb_net);

	dvbdemux->dmx.close(&dvbdemux->dmx);
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &budget->hw_frontend);
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &budget->mem_frontend);

	dvb_dmxdev_release(&budget->dmxdev);
	dvb_dmx_release(&budget->demux);
}

int ttpci_budget_init(struct budget *budget, struct saa7146_dev *dev,
		      struct saa7146_pci_extension_data *info,
		      struct module *owner)
{
	int length = TS_WIDTH * TS_HEIGHT;
	int ret = 0;
	struct budget_info *bi = info->ext_priv;

	memset(budget, 0, sizeof(struct budget));

	dprintk(2, "dev: %p, budget: %p\n", dev, budget);

	budget->card = bi;
	budget->dev = (struct saa7146_dev *) dev;

	dvb_register_adapter(&budget->dvb_adapter, budget->card->name, owner);

	/* set dd1 stream a & b */
	saa7146_write(dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(dev, MC2, (MASK_09 | MASK_25));
	saa7146_write(dev, MC2, (MASK_10 | MASK_26));
	saa7146_write(dev, DD1_INIT, 0x02000000);
	saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	if (bi->type != BUDGET_FS_ACTIVY)
		budget->video_port = BUDGET_VIDEO_PORTB;
	else
		budget->video_port = BUDGET_VIDEO_PORTA;
	spin_lock_init(&budget->feedlock);
	spin_lock_init(&budget->debilock);

	/* the Siemens DVB needs this if you want to have the i2c chips
	   get recognized before the main driver is loaded */
	if (bi->type != BUDGET_FS_ACTIVY)
		saa7146_write(dev, GPIO_CTRL, 0x500000);	/* GPIO 3 = 1 */

#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	budget->i2c_adap.class = I2C_ADAP_CLASS_TV_DIGITAL;
#else
	budget->i2c_adap.class = I2C_CLASS_TV_DIGITAL;
#endif

	strlcpy(budget->i2c_adap.name, budget->card->name, sizeof(budget->i2c_adap.name));

	saa7146_i2c_adapter_prepare(dev, &budget->i2c_adap, SAA7146_I2C_BUS_BIT_RATE_120);
	strcpy(budget->i2c_adap.name, budget->card->name);

	if (i2c_add_adapter(&budget->i2c_adap) < 0) {
		dvb_unregister_adapter(&budget->dvb_adapter);
		return -ENOMEM;
	}

	ttpci_eeprom_parse_mac(&budget->i2c_adap, budget->dvb_adapter.proposed_mac);

	if (NULL ==
	    (budget->grabbing = saa7146_vmalloc_build_pgtable(dev->pci, length, &budget->pt))) {
		ret = -ENOMEM;
		goto err;
	}

	saa7146_write(dev, PCI_BT_V1, 0x001c0000);
	/* upload all */
	saa7146_write(dev, GPIO_CTRL, 0x000000);

	tasklet_init(&budget->vpe_tasklet, vpeirq, (unsigned long) budget);

	/* frontend power on */
	if (bi->type == BUDGET_FS_ACTIVY)
		saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI);
	else
		saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTHI);

	if (budget_register(budget) == 0) {
		return 0;
	}
err:
	i2c_del_adapter(&budget->i2c_adap);

	vfree(budget->grabbing);

	dvb_unregister_adapter(&budget->dvb_adapter);

	return ret;
}

int ttpci_budget_deinit(struct budget *budget)
{
	struct saa7146_dev *dev = budget->dev;

	dprintk(2, "budget: %p\n", budget);

	budget_unregister(budget);

	i2c_del_adapter(&budget->i2c_adap);

	dvb_unregister_adapter(&budget->dvb_adapter);

	tasklet_kill(&budget->vpe_tasklet);

	saa7146_pgtable_free(dev->pci, &budget->pt);

	vfree(budget->grabbing);

	return 0;
}

void ttpci_budget_irq10_handler(struct saa7146_dev *dev, u32 * isr)
{
	struct budget *budget = (struct budget *) dev->ext_priv;

	dprintk(8, "dev: %p, budget: %p\n", dev, budget);

	if (*isr & MASK_10)
		tasklet_schedule(&budget->vpe_tasklet);
}

void ttpci_budget_set_video_port(struct saa7146_dev *dev, int video_port)
{
	struct budget *budget = (struct budget *) dev->ext_priv;

	spin_lock(&budget->feedlock);
	budget->video_port = video_port;
	if (budget->feeding) {
		int oldfeeding = budget->feeding;
		budget->feeding = 1;
		stop_ts_capture(budget);
		start_ts_capture(budget);
		budget->feeding = oldfeeding;
	}
	spin_unlock(&budget->feedlock);
}

EXPORT_SYMBOL_GPL(ttpci_budget_debiread);
EXPORT_SYMBOL_GPL(ttpci_budget_debiwrite);
EXPORT_SYMBOL_GPL(ttpci_budget_init);
EXPORT_SYMBOL_GPL(ttpci_budget_deinit);
EXPORT_SYMBOL_GPL(ttpci_budget_irq10_handler);
EXPORT_SYMBOL_GPL(ttpci_budget_set_video_port);
EXPORT_SYMBOL_GPL(budget_debug);

MODULE_LICENSE("GPL");
