/*
 * driver for the SAA7146 based AV110 cards (like the Fujitsu-Siemens DVB)
 * av7110.c: initialization and demux stuff
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org/ 
 */


#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/poll.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/crc32.h>
#include <linux/i2c.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>


#include <linux/dvb/frontend.h>

#include "dvb_frontend.h"

#include "ttpci-eeprom.h"
#include "av7110.h"
#include "av7110_hw.h"
#include "av7110_av.h"
#include "av7110_ca.h"
#include "av7110_ipack.h"

#include "bsbe1.h"
#include "lnbp21.h"
#include "bsru6.h"

#define TS_WIDTH  376
#define TS_HEIGHT 512
#define TS_BUFLEN (TS_WIDTH*TS_HEIGHT)
#define TS_MAX_PACKETS (TS_BUFLEN/TS_SIZE)


int av7110_debug;

static int vidmode = CVBS_RGB_OUT;
static int pids_off;
static int adac = DVB_ADAC_TI;
static int hw_sections;
static int rgb_on;
static int volume = 255;
static int budgetpatch;
static int wss_cfg_4_3 = 0x4008;
static int wss_cfg_16_9 = 0x0007;
static int tv_standard;
static int full_ts;

module_param_named(debug, av7110_debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (bitmask, default 0)");
module_param(vidmode, int, 0444);
MODULE_PARM_DESC(vidmode,"analog video out: 0 off, 1 CVBS+RGB (default), 2 CVBS+YC, 3 YC");
module_param(pids_off, int, 0444);
MODULE_PARM_DESC(pids_off,"clear video/audio/PCR PID filters when demux is closed");
module_param(adac, int, 0444);
MODULE_PARM_DESC(adac,"audio DAC type: 0 TI, 1 CRYSTAL, 2 MSP (use if autodetection fails)");
module_param(hw_sections, int, 0444);
MODULE_PARM_DESC(hw_sections, "0 use software section filter, 1 use hardware");
module_param(rgb_on, int, 0444);
MODULE_PARM_DESC(rgb_on, "For Siemens DVB-C cards only: Enable RGB control"
		" signal on SCART pin 16 to switch SCART video mode from CVBS to RGB");
module_param(volume, int, 0444);
MODULE_PARM_DESC(volume, "initial volume: default 255 (range 0-255)");
module_param(budgetpatch, int, 0444);
MODULE_PARM_DESC(budgetpatch, "use budget-patch hardware modification: default 0 (0 no, 1 autodetect, 2 always)");
module_param(full_ts, int, 0444);
MODULE_PARM_DESC(full_ts, "enable code for full-ts hardware modification: 0 disable (default), 1 enable");
module_param(wss_cfg_4_3, int, 0444);
MODULE_PARM_DESC(wss_cfg_4_3, "WSS 4:3 - default 0x4008 - bit 15: disable, 14: burst mode, 13..0: wss data");
module_param(wss_cfg_16_9, int, 0444);
MODULE_PARM_DESC(wss_cfg_16_9, "WSS 16:9 - default 0x0007 - bit 15: disable, 14: burst mode, 13..0: wss data");
module_param(tv_standard, int, 0444);
MODULE_PARM_DESC(tv_standard, "TV standard: 0 PAL (default), 1 NTSC");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static void restart_feeds(struct av7110 *av7110);
static int budget_start_feed(struct dvb_demux_feed *feed);
static int budget_stop_feed(struct dvb_demux_feed *feed);

static int av7110_num;

#define FE_FUNC_OVERRIDE(fe_func, av7110_copy, av7110_func) \
{\
	if (fe_func != NULL) { \
		av7110_copy = fe_func; \
		fe_func = av7110_func; \
	} \
}


static void init_av7110_av(struct av7110 *av7110)
{
	int ret;
	struct saa7146_dev *dev = av7110->dev;

	/* set internal volume control to maximum */
	av7110->adac_type = DVB_ADAC_TI;
	ret = av7110_set_volume(av7110, av7110->mixer.volume_left, av7110->mixer.volume_right);
	if (ret < 0)
		printk("dvb-ttpci:cannot set internal volume to maximum:%d\n",ret);

	ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetMonitorType,
			    1, (u16) av7110->display_ar);
	if (ret < 0)
		printk("dvb-ttpci: unable to set aspect ratio\n");
	ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetPanScanType,
			    1, av7110->display_panscan);
	if (ret < 0)
		printk("dvb-ttpci: unable to set pan scan\n");

	ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetWSSConfig, 2, 2, wss_cfg_4_3);
	if (ret < 0)
		printk("dvb-ttpci: unable to configure 4:3 wss\n");
	ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetWSSConfig, 2, 3, wss_cfg_16_9);
	if (ret < 0)
		printk("dvb-ttpci: unable to configure 16:9 wss\n");

	ret = av7710_set_video_mode(av7110, vidmode);
	if (ret < 0)
		printk("dvb-ttpci:cannot set video mode:%d\n",ret);

	/* handle different card types */
	/* remaining inits according to card and frontend type */
	av7110->analog_tuner_flags = 0;
	av7110->current_input = 0;
	if (dev->pci->subsystem_vendor == 0x13c2 && dev->pci->subsystem_device == 0x000a)
		av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, 0); // SPDIF on
	if (i2c_writereg(av7110, 0x20, 0x00, 0x00) == 1) {
		printk ("dvb-ttpci: Crystal audio DAC @ card %d detected\n",
			av7110->dvb_adapter.num);
		av7110->adac_type = DVB_ADAC_CRYSTAL;
		i2c_writereg(av7110, 0x20, 0x01, 0xd2);
		i2c_writereg(av7110, 0x20, 0x02, 0x49);
		i2c_writereg(av7110, 0x20, 0x03, 0x00);
		i2c_writereg(av7110, 0x20, 0x04, 0x00);

		/**
		 * some special handling for the Siemens DVB-C cards...
		 */
	} else if (0 == av7110_init_analog_module(av7110)) {
		/* done. */
	}
	else if (dev->pci->subsystem_vendor == 0x110a) {
		printk("dvb-ttpci: DVB-C w/o analog module @ card %d detected\n",
			av7110->dvb_adapter.num);
		av7110->adac_type = DVB_ADAC_NONE;
	}
	else {
		av7110->adac_type = adac;
		printk("dvb-ttpci: adac type set to %d @ card %d\n",
			av7110->adac_type, av7110->dvb_adapter.num);
	}

	if (av7110->adac_type == DVB_ADAC_NONE || av7110->adac_type == DVB_ADAC_MSP34x0) {
		// switch DVB SCART on
		ret = av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, MainSwitch, 1, 0);
		if (ret < 0)
			printk("dvb-ttpci:cannot switch on SCART(Main):%d\n",ret);
		ret = av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, 1);
		if (ret < 0)
			printk("dvb-ttpci:cannot switch on SCART(AD):%d\n",ret);
		if (rgb_on &&
		    ((av7110->dev->pci->subsystem_vendor == 0x110a) ||
		     (av7110->dev->pci->subsystem_vendor == 0x13c2)) &&
		     (av7110->dev->pci->subsystem_device == 0x0000)) {
			saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI); // RGB on, SCART pin 16
			//saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO); // SCARTpin 8
		}
	}

	if (dev->pci->subsystem_vendor == 0x13c2 && dev->pci->subsystem_device == 0x000e)
		av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, SpdifSwitch, 1, 0); // SPDIF on

	ret = av7110_set_volume(av7110, av7110->mixer.volume_left, av7110->mixer.volume_right);
	if (ret < 0)
		printk("dvb-ttpci:cannot set volume :%d\n",ret);
}

static void recover_arm(struct av7110 *av7110)
{
	dprintk(4, "%p\n",av7110);

	av7110_bootarm(av7110);
	msleep(100);

	init_av7110_av(av7110);

	/* card-specific recovery */
	if (av7110->recover)
		av7110->recover(av7110);

	restart_feeds(av7110);

#if IS_ENABLED(CONFIG_INPUT_EVDEV)
	av7110_check_ir_config(av7110, true);
#endif
}

static void av7110_arm_sync(struct av7110 *av7110)
{
	if (av7110->arm_thread)
		kthread_stop(av7110->arm_thread);

	av7110->arm_thread = NULL;
}

static int arm_thread(void *data)
{
	struct av7110 *av7110 = data;
	u16 newloops = 0;
	int timeout;

	dprintk(4, "%p\n",av7110);

	for (;;) {
		timeout = wait_event_interruptible_timeout(av7110->arm_wait,
			kthread_should_stop(), 5 * HZ);

		if (-ERESTARTSYS == timeout || kthread_should_stop()) {
			/* got signal or told to quit*/
			break;
		}

		if (!av7110->arm_ready)
			continue;

#if IS_ENABLED(CONFIG_INPUT_EVDEV)
		av7110_check_ir_config(av7110, false);
#endif

		if (mutex_lock_interruptible(&av7110->dcomlock))
			break;
		newloops = rdebi(av7110, DEBINOSWAP, STATUS_LOOPS, 0, 2);
		mutex_unlock(&av7110->dcomlock);

		if (newloops == av7110->arm_loops || av7110->arm_errors > 3) {
			printk(KERN_ERR "dvb-ttpci: ARM crashed @ card %d\n",
			       av7110->dvb_adapter.num);

			recover_arm(av7110);

			if (mutex_lock_interruptible(&av7110->dcomlock))
				break;
			newloops = rdebi(av7110, DEBINOSWAP, STATUS_LOOPS, 0, 2) - 1;
			mutex_unlock(&av7110->dcomlock);
		}
		av7110->arm_loops = newloops;
		av7110->arm_errors = 0;
	}

	return 0;
}


/****************************************************************************
 * IRQ handling
 ****************************************************************************/

static int DvbDmxFilterCallback(u8 *buffer1, size_t buffer1_len,
				u8 *buffer2, size_t buffer2_len,
				struct dvb_demux_filter *dvbdmxfilter,
				enum dmx_success success,
				struct av7110 *av7110)
{
	if (!dvbdmxfilter->feed->demux->dmx.frontend)
		return 0;
	if (dvbdmxfilter->feed->demux->dmx.frontend->source == DMX_MEMORY_FE)
		return 0;

	switch (dvbdmxfilter->type) {
	case DMX_TYPE_SEC:
		if ((((buffer1[1] << 8) | buffer1[2]) & 0xfff) + 3 != buffer1_len)
			return 0;
		if (dvbdmxfilter->doneq) {
			struct dmx_section_filter *filter = &dvbdmxfilter->filter;
			int i;
			u8 xor, neq = 0;

			for (i = 0; i < DVB_DEMUX_MASK_MAX; i++) {
				xor = filter->filter_value[i] ^ buffer1[i];
				neq |= dvbdmxfilter->maskandnotmode[i] & xor;
			}
			if (!neq)
				return 0;
		}
		return dvbdmxfilter->feed->cb.sec(buffer1, buffer1_len,
						  buffer2, buffer2_len,
						  &dvbdmxfilter->filter,
						  DMX_OK);
	case DMX_TYPE_TS:
		if (!(dvbdmxfilter->feed->ts_type & TS_PACKET))
			return 0;
		if (dvbdmxfilter->feed->ts_type & TS_PAYLOAD_ONLY)
			return dvbdmxfilter->feed->cb.ts(buffer1, buffer1_len,
							 buffer2, buffer2_len,
							 &dvbdmxfilter->feed->feed.ts,
							 DMX_OK);
		else
			av7110_p2t_write(buffer1, buffer1_len,
					 dvbdmxfilter->feed->pid,
					 &av7110->p2t_filter[dvbdmxfilter->index]);
	default:
		return 0;
	}
}


//#define DEBUG_TIMING
static inline void print_time(char *s)
{
#ifdef DEBUG_TIMING
	struct timeval tv;
	do_gettimeofday(&tv);
	printk("%s: %d.%d\n", s, (int)tv.tv_sec, (int)tv.tv_usec);
#endif
}

#define DEBI_READ 0
#define DEBI_WRITE 1
static inline void start_debi_dma(struct av7110 *av7110, int dir,
				  unsigned long addr, unsigned int len)
{
	dprintk(8, "%c %08lx %u\n", dir == DEBI_READ ? 'R' : 'W', addr, len);
	if (saa7146_wait_for_debi_done(av7110->dev, 0)) {
		printk(KERN_ERR "%s: saa7146_wait_for_debi_done timed out\n", __func__);
		return;
	}

	SAA7146_ISR_CLEAR(av7110->dev, MASK_19); /* for good measure */
	SAA7146_IER_ENABLE(av7110->dev, MASK_19);
	if (len < 5)
		len = 5; /* we want a real DEBI DMA */
	if (dir == DEBI_WRITE)
		iwdebi(av7110, DEBISWAB, addr, 0, (len + 3) & ~3);
	else
		irdebi(av7110, DEBISWAB, addr, 0, len);
}

static void debiirq(unsigned long cookie)
{
	struct av7110 *av7110 = (struct av7110 *)cookie;
	int type = av7110->debitype;
	int handle = (type >> 8) & 0x1f;
	unsigned int xfer = 0;

	print_time("debi");
	dprintk(4, "type 0x%04x\n", type);

	if (type == -1) {
		printk("DEBI irq oops @ %ld, psr:0x%08x, ssr:0x%08x\n",
		       jiffies, saa7146_read(av7110->dev, PSR),
		       saa7146_read(av7110->dev, SSR));
		goto debi_done;
	}
	av7110->debitype = -1;

	switch (type & 0xff) {

	case DATA_TS_RECORD:
		dvb_dmx_swfilter_packets(&av7110->demux,
					 (const u8 *) av7110->debi_virt,
					 av7110->debilen / 188);
		xfer = RX_BUFF;
		break;

	case DATA_PES_RECORD:
		if (av7110->demux.recording)
			av7110_record_cb(&av7110->p2t[handle],
					 (u8 *) av7110->debi_virt,
					 av7110->debilen);
		xfer = RX_BUFF;
		break;

	case DATA_IPMPE:
	case DATA_FSECTION:
	case DATA_PIPING:
		if (av7110->handle2filter[handle])
			DvbDmxFilterCallback((u8 *)av7110->debi_virt,
					     av7110->debilen, NULL, 0,
					     av7110->handle2filter[handle],
					     DMX_OK, av7110);
		xfer = RX_BUFF;
		break;

	case DATA_CI_GET:
	{
		u8 *data = av7110->debi_virt;

		if ((data[0] < 2) && data[2] == 0xff) {
			int flags = 0;
			if (data[5] > 0)
				flags |= CA_CI_MODULE_PRESENT;
			if (data[5] > 5)
				flags |= CA_CI_MODULE_READY;
			av7110->ci_slot[data[0]].flags = flags;
		} else
			ci_get_data(&av7110->ci_rbuffer,
				    av7110->debi_virt,
				    av7110->debilen);
		xfer = RX_BUFF;
		break;
	}

	case DATA_COMMON_INTERFACE:
		CI_handle(av7110, (u8 *)av7110->debi_virt, av7110->debilen);
#if 0
	{
		int i;

		printk("av7110%d: ", av7110->num);
		printk("%02x ", *(u8 *)av7110->debi_virt);
		printk("%02x ", *(1+(u8 *)av7110->debi_virt));
		for (i = 2; i < av7110->debilen; i++)
			printk("%02x ", (*(i+(unsigned char *)av7110->debi_virt)));
		for (i = 2; i < av7110->debilen; i++)
			printk("%c", chtrans(*(i+(unsigned char *)av7110->debi_virt)));

		printk("\n");
	}
#endif
		xfer = RX_BUFF;
		break;

	case DATA_DEBUG_MESSAGE:
		((s8*)av7110->debi_virt)[Reserved_SIZE - 1] = 0;
		printk("%s\n", (s8 *) av7110->debi_virt);
		xfer = RX_BUFF;
		break;

	case DATA_CI_PUT:
		dprintk(4, "debi DATA_CI_PUT\n");
	case DATA_MPEG_PLAY:
		dprintk(4, "debi DATA_MPEG_PLAY\n");
	case DATA_BMP_LOAD:
		dprintk(4, "debi DATA_BMP_LOAD\n");
		xfer = TX_BUFF;
		break;
	default:
		break;
	}
debi_done:
	spin_lock(&av7110->debilock);
	if (xfer)
		iwdebi(av7110, DEBINOSWAP, xfer, 0, 2);
	ARM_ClearMailBox(av7110);
	spin_unlock(&av7110->debilock);
}

/* irq from av7110 firmware writing the mailbox register in the DPRAM */
static void gpioirq(unsigned long cookie)
{
	struct av7110 *av7110 = (struct av7110 *)cookie;
	u32 rxbuf, txbuf;
	int len;

	if (av7110->debitype != -1)
		/* we shouldn't get any irq while a debi xfer is running */
		printk("dvb-ttpci: GPIO0 irq oops @ %ld, psr:0x%08x, ssr:0x%08x\n",
		       jiffies, saa7146_read(av7110->dev, PSR),
		       saa7146_read(av7110->dev, SSR));

	if (saa7146_wait_for_debi_done(av7110->dev, 0)) {
		printk(KERN_ERR "%s: saa7146_wait_for_debi_done timed out\n", __func__);
		BUG(); /* maybe we should try resetting the debi? */
	}

	spin_lock(&av7110->debilock);
	ARM_ClearIrq(av7110);

	/* see what the av7110 wants */
	av7110->debitype = irdebi(av7110, DEBINOSWAP, IRQ_STATE, 0, 2);
	av7110->debilen  = irdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
	rxbuf = irdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
	txbuf = irdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
	len = (av7110->debilen + 3) & ~3;

	print_time("gpio");
	dprintk(8, "GPIO0 irq 0x%04x %d\n", av7110->debitype, av7110->debilen);

	switch (av7110->debitype & 0xff) {

	case DATA_TS_PLAY:
	case DATA_PES_PLAY:
		break;

	case DATA_MPEG_VIDEO_EVENT:
	{
		u32 h_ar;
		struct video_event event;

		av7110->video_size.w = irdebi(av7110, DEBINOSWAP, STATUS_MPEG_WIDTH, 0, 2);
		h_ar = irdebi(av7110, DEBINOSWAP, STATUS_MPEG_HEIGHT_AR, 0, 2);

		iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
		iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);

		av7110->video_size.h = h_ar & 0xfff;

		event.type = VIDEO_EVENT_SIZE_CHANGED;
		event.u.size.w = av7110->video_size.w;
		event.u.size.h = av7110->video_size.h;
		switch ((h_ar >> 12) & 0xf)
		{
		case 3:
			av7110->video_size.aspect_ratio = VIDEO_FORMAT_16_9;
			event.u.size.aspect_ratio = VIDEO_FORMAT_16_9;
			av7110->videostate.video_format = VIDEO_FORMAT_16_9;
			break;
		case 4:
			av7110->video_size.aspect_ratio = VIDEO_FORMAT_221_1;
			event.u.size.aspect_ratio = VIDEO_FORMAT_221_1;
			av7110->videostate.video_format = VIDEO_FORMAT_221_1;
			break;
		default:
			av7110->video_size.aspect_ratio = VIDEO_FORMAT_4_3;
			event.u.size.aspect_ratio = VIDEO_FORMAT_4_3;
			av7110->videostate.video_format = VIDEO_FORMAT_4_3;
		}

		dprintk(8, "GPIO0 irq: DATA_MPEG_VIDEO_EVENT: w/h/ar = %u/%u/%u\n",
			av7110->video_size.w, av7110->video_size.h,
			av7110->video_size.aspect_ratio);

		dvb_video_add_event(av7110, &event);
		break;
	}

	case DATA_CI_PUT:
	{
		int avail;
		struct dvb_ringbuffer *cibuf = &av7110->ci_wbuffer;

		avail = dvb_ringbuffer_avail(cibuf);
		if (avail <= 2) {
			iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
			break;
		}
		len = DVB_RINGBUFFER_PEEK(cibuf, 0) << 8;
		len |= DVB_RINGBUFFER_PEEK(cibuf, 1);
		if (avail < len + 2) {
			iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
			break;
		}
		DVB_RINGBUFFER_SKIP(cibuf, 2);

		dvb_ringbuffer_read(cibuf, av7110->debi_virt, len);

		iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
		iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
		dprintk(8, "DMA: CI\n");
		start_debi_dma(av7110, DEBI_WRITE, DPRAM_BASE + txbuf, len);
		spin_unlock(&av7110->debilock);
		wake_up(&cibuf->queue);
		return;
	}

	case DATA_MPEG_PLAY:
		if (!av7110->playing) {
			iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
			break;
		}
		len = 0;
		if (av7110->debitype & 0x100) {
			spin_lock(&av7110->aout.lock);
			len = av7110_pes_play(av7110->debi_virt, &av7110->aout, 2048);
			spin_unlock(&av7110->aout.lock);
		}
		if (len <= 0 && (av7110->debitype & 0x200)
		    &&av7110->videostate.play_state != VIDEO_FREEZED) {
			spin_lock(&av7110->avout.lock);
			len = av7110_pes_play(av7110->debi_virt, &av7110->avout, 2048);
			spin_unlock(&av7110->avout.lock);
		}
		if (len <= 0) {
			iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
			break;
		}
		dprintk(8, "GPIO0 PES_PLAY len=%04x\n", len);
		iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
		iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
		dprintk(8, "DMA: MPEG_PLAY\n");
		start_debi_dma(av7110, DEBI_WRITE, DPRAM_BASE + txbuf, len);
		spin_unlock(&av7110->debilock);
		return;

	case DATA_BMP_LOAD:
		len = av7110->debilen;
		dprintk(8, "gpio DATA_BMP_LOAD len %d\n", len);
		if (!len) {
			av7110->bmp_state = BMP_LOADED;
			iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
			iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
			wake_up(&av7110->bmpq);
			dprintk(8, "gpio DATA_BMP_LOAD done\n");
			break;
		}
		if (len > av7110->bmplen)
			len = av7110->bmplen;
		if (len > 2 * 1024)
			len = 2 * 1024;
		iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
		iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
		memcpy(av7110->debi_virt, av7110->bmpbuf+av7110->bmpp, len);
		av7110->bmpp += len;
		av7110->bmplen -= len;
		dprintk(8, "gpio DATA_BMP_LOAD DMA len %d\n", len);
		start_debi_dma(av7110, DEBI_WRITE, DPRAM_BASE+txbuf, len);
		spin_unlock(&av7110->debilock);
		return;

	case DATA_CI_GET:
	case DATA_COMMON_INTERFACE:
	case DATA_FSECTION:
	case DATA_IPMPE:
	case DATA_PIPING:
		if (!len || len > 4 * 1024) {
			iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
			break;
		}
		/* fall through */

	case DATA_TS_RECORD:
	case DATA_PES_RECORD:
		dprintk(8, "DMA: TS_REC etc.\n");
		start_debi_dma(av7110, DEBI_READ, DPRAM_BASE+rxbuf, len);
		spin_unlock(&av7110->debilock);
		return;

	case DATA_DEBUG_MESSAGE:
		if (!len || len > 0xff) {
			iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
			break;
		}
		start_debi_dma(av7110, DEBI_READ, Reserved, len);
		spin_unlock(&av7110->debilock);
		return;

	case DATA_IRCOMMAND:
		if (av7110->ir.ir_handler)
			av7110->ir.ir_handler(av7110,
				swahw32(irdebi(av7110, DEBINOSWAP, Reserved, 0, 4)));
		iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
		break;

	default:
		printk("dvb-ttpci: gpioirq unknown type=%d len=%d\n",
		       av7110->debitype, av7110->debilen);
		break;
	}
	av7110->debitype = -1;
	ARM_ClearMailBox(av7110);
	spin_unlock(&av7110->debilock);
}


#ifdef CONFIG_DVB_AV7110_OSD
static int dvb_osd_ioctl(struct file *file,
			 unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;

	dprintk(4, "%p\n", av7110);

	if (cmd == OSD_SEND_CMD)
		return av7110_osd_cmd(av7110, (osd_cmd_t *) parg);
	if (cmd == OSD_GET_CAPABILITY)
		return av7110_osd_capability(av7110, (osd_cap_t *) parg);

	return -EINVAL;
}


static const struct file_operations dvb_osd_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= dvb_generic_ioctl,
	.open		= dvb_generic_open,
	.release	= dvb_generic_release,
	.llseek		= noop_llseek,
};

static struct dvb_device dvbdev_osd = {
	.priv		= NULL,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_osd_fops,
	.kernel_ioctl	= dvb_osd_ioctl,
};
#endif /* CONFIG_DVB_AV7110_OSD */


static inline int SetPIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid,
			  u16 subpid, u16 pcrpid)
{
	u16 aflags = 0;

	dprintk(4, "%p\n", av7110);

	if (vpid == 0x1fff || apid == 0x1fff ||
	    ttpid == 0x1fff || subpid == 0x1fff || pcrpid == 0x1fff) {
		vpid = apid = ttpid = subpid = pcrpid = 0;
		av7110->pids[DMX_PES_VIDEO] = 0;
		av7110->pids[DMX_PES_AUDIO] = 0;
		av7110->pids[DMX_PES_TELETEXT] = 0;
		av7110->pids[DMX_PES_PCR] = 0;
	}

	if (av7110->audiostate.bypass_mode)
		aflags |= 0x8000;

	return av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, MultiPID, 6,
			     pcrpid, vpid, apid, ttpid, subpid, aflags);
}

int ChangePIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid,
		u16 subpid, u16 pcrpid)
{
	int ret = 0;
	dprintk(4, "%p\n", av7110);

	if (mutex_lock_interruptible(&av7110->pid_mutex))
		return -ERESTARTSYS;

	if (!(vpid & 0x8000))
		av7110->pids[DMX_PES_VIDEO] = vpid;
	if (!(apid & 0x8000))
		av7110->pids[DMX_PES_AUDIO] = apid;
	if (!(ttpid & 0x8000))
		av7110->pids[DMX_PES_TELETEXT] = ttpid;
	if (!(pcrpid & 0x8000))
		av7110->pids[DMX_PES_PCR] = pcrpid;

	av7110->pids[DMX_PES_SUBTITLE] = 0;

	if (av7110->fe_synced) {
		pcrpid = av7110->pids[DMX_PES_PCR];
		ret = SetPIDs(av7110, vpid, apid, ttpid, subpid, pcrpid);
	}

	mutex_unlock(&av7110->pid_mutex);
	return ret;
}


/******************************************************************************
 * hardware filter functions
 ******************************************************************************/

static int StartHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
	struct dvb_demux_feed *dvbdmxfeed = dvbdmxfilter->feed;
	struct av7110 *av7110 = dvbdmxfeed->demux->priv;
	u16 buf[20];
	int ret, i;
	u16 handle;
//	u16 mode = 0x0320;
	u16 mode = 0xb96a;

	dprintk(4, "%p\n", av7110);

	if (av7110->full_ts)
		return 0;

	if (dvbdmxfilter->type == DMX_TYPE_SEC) {
		if (hw_sections) {
			buf[4] = (dvbdmxfilter->filter.filter_value[0] << 8) |
				dvbdmxfilter->maskandmode[0];
			for (i = 3; i < 18; i++)
				buf[i + 4 - 2] =
					(dvbdmxfilter->filter.filter_value[i] << 8) |
					dvbdmxfilter->maskandmode[i];
			mode = 4;
		}
	} else if ((dvbdmxfeed->ts_type & TS_PACKET) &&
		   !(dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)) {
		av7110_p2t_init(&av7110->p2t_filter[dvbdmxfilter->index], dvbdmxfeed);
	}

	buf[0] = (COMTYPE_PID_FILTER << 8) + AddPIDFilter;
	buf[1] = 16;
	buf[2] = dvbdmxfeed->pid;
	buf[3] = mode;

	ret = av7110_fw_request(av7110, buf, 20, &handle, 1);
	if (ret != 0 || handle >= 32) {
		printk("dvb-ttpci: %s error  buf %04x %04x %04x %04x  "
				"ret %d  handle %04x\n",
				__func__, buf[0], buf[1], buf[2], buf[3],
				ret, handle);
		dvbdmxfilter->hw_handle = 0xffff;
		if (!ret)
			ret = -1;
		return ret;
	}

	av7110->handle2filter[handle] = dvbdmxfilter;
	dvbdmxfilter->hw_handle = handle;

	return ret;
}

static int StopHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
	struct av7110 *av7110 = dvbdmxfilter->feed->demux->priv;
	u16 buf[3];
	u16 answ[2];
	int ret;
	u16 handle;

	dprintk(4, "%p\n", av7110);

	if (av7110->full_ts)
		return 0;

	handle = dvbdmxfilter->hw_handle;
	if (handle >= 32) {
		printk("%s tried to stop invalid filter %04x, filter type = %x\n",
				__func__, handle, dvbdmxfilter->type);
		return -EINVAL;
	}

	av7110->handle2filter[handle] = NULL;

	buf[0] = (COMTYPE_PID_FILTER << 8) + DelPIDFilter;
	buf[1] = 1;
	buf[2] = handle;
	ret = av7110_fw_request(av7110, buf, 3, answ, 2);
	if (ret != 0 || answ[1] != handle) {
		printk("dvb-ttpci: %s error  cmd %04x %04x %04x  ret %x  "
				"resp %04x %04x  pid %d\n",
				__func__, buf[0], buf[1], buf[2], ret,
				answ[0], answ[1], dvbdmxfilter->feed->pid);
		if (!ret)
			ret = -1;
	}
	return ret;
}


static int dvb_feed_start_pid(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct av7110 *av7110 = dvbdmx->priv;
	u16 *pid = dvbdmx->pids, npids[5];
	int i;
	int ret = 0;

	dprintk(4, "%p\n", av7110);

	npids[0] = npids[1] = npids[2] = npids[3] = npids[4] = 0xffff;
	i = dvbdmxfeed->pes_type;
	npids[i] = (pid[i]&0x8000) ? 0 : pid[i];
	if ((i == 2) && npids[i] && (dvbdmxfeed->ts_type & TS_PACKET)) {
		npids[i] = 0;
		ret = ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
		if (!ret)
			ret = StartHWFilter(dvbdmxfeed->filter);
		return ret;
	}
	if (dvbdmxfeed->pes_type <= 2 || dvbdmxfeed->pes_type == 4) {
		ret = ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
		if (ret)
			return ret;
	}

	if (dvbdmxfeed->pes_type < 2 && npids[0])
		if (av7110->fe_synced)
		{
			ret = av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, Scan, 0);
			if (ret)
				return ret;
		}

	if ((dvbdmxfeed->ts_type & TS_PACKET) && !av7110->full_ts) {
		if (dvbdmxfeed->pes_type == 0 && !(dvbdmx->pids[0] & 0x8000))
			ret = av7110_av_start_record(av7110, RP_AUDIO, dvbdmxfeed);
		if (dvbdmxfeed->pes_type == 1 && !(dvbdmx->pids[1] & 0x8000))
			ret = av7110_av_start_record(av7110, RP_VIDEO, dvbdmxfeed);
	}
	return ret;
}

static int dvb_feed_stop_pid(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct av7110 *av7110 = dvbdmx->priv;
	u16 *pid = dvbdmx->pids, npids[5];
	int i;

	int ret = 0;

	dprintk(4, "%p\n", av7110);

	if (dvbdmxfeed->pes_type <= 1) {
		ret = av7110_av_stop(av7110, dvbdmxfeed->pes_type ?  RP_VIDEO : RP_AUDIO);
		if (ret)
			return ret;
		if (!av7110->rec_mode)
			dvbdmx->recording = 0;
		if (!av7110->playing)
			dvbdmx->playing = 0;
	}
	npids[0] = npids[1] = npids[2] = npids[3] = npids[4] = 0xffff;
	i = dvbdmxfeed->pes_type;
	switch (i) {
	case 2: //teletext
		if (dvbdmxfeed->ts_type & TS_PACKET)
			ret = StopHWFilter(dvbdmxfeed->filter);
		npids[2] = 0;
		break;
	case 0:
	case 1:
	case 4:
		if (!pids_off)
			return 0;
		npids[i] = (pid[i]&0x8000) ? 0 : pid[i];
		break;
	}
	if (!ret)
		ret = ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
	return ret;
}

static int av7110_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct av7110 *av7110 = demux->priv;
	int ret = 0;

	dprintk(4, "%p\n", av7110);

	if (!demux->dmx.frontend)
		return -EINVAL;

	if (!av7110->full_ts && feed->pid > 0x1fff)
		return -EINVAL;

	if (feed->type == DMX_TYPE_TS) {
		if ((feed->ts_type & TS_DECODER) &&
		    (feed->pes_type <= DMX_TS_PES_PCR)) {
			switch (demux->dmx.frontend->source) {
			case DMX_MEMORY_FE:
				if (feed->ts_type & TS_DECODER)
				       if (feed->pes_type < 2 &&
					   !(demux->pids[0] & 0x8000) &&
					   !(demux->pids[1] & 0x8000)) {
					       dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
					       dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
					       ret = av7110_av_start_play(av7110,RP_AV);
					       if (!ret)
						       demux->playing = 1;
					}
				break;
			default:
				ret = dvb_feed_start_pid(feed);
				break;
			}
		} else if ((feed->ts_type & TS_PACKET) &&
			   (demux->dmx.frontend->source != DMX_MEMORY_FE)) {
			ret = StartHWFilter(feed->filter);
		}
	}

	if (av7110->full_ts) {
		budget_start_feed(feed);
		return ret;
	}

	if (feed->type == DMX_TYPE_SEC) {
		int i;

		for (i = 0; i < demux->filternum; i++) {
			if (demux->filter[i].state != DMX_STATE_READY)
				continue;
			if (demux->filter[i].type != DMX_TYPE_SEC)
				continue;
			if (demux->filter[i].filter.parent != &feed->feed.sec)
				continue;
			demux->filter[i].state = DMX_STATE_GO;
			if (demux->dmx.frontend->source != DMX_MEMORY_FE) {
				ret = StartHWFilter(&demux->filter[i]);
				if (ret)
					break;
			}
		}
	}

	return ret;
}


static int av7110_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct av7110 *av7110 = demux->priv;
	int i, rc, ret = 0;
	dprintk(4, "%p\n", av7110);

	if (feed->type == DMX_TYPE_TS) {
		if (feed->ts_type & TS_DECODER) {
			if (feed->pes_type >= DMX_TS_PES_OTHER ||
			    !demux->pesfilter[feed->pes_type])
				return -EINVAL;
			demux->pids[feed->pes_type] |= 0x8000;
			demux->pesfilter[feed->pes_type] = NULL;
		}
		if (feed->ts_type & TS_DECODER &&
		    feed->pes_type < DMX_TS_PES_OTHER) {
			ret = dvb_feed_stop_pid(feed);
		} else
			if ((feed->ts_type & TS_PACKET) &&
			    (demux->dmx.frontend->source != DMX_MEMORY_FE))
				ret = StopHWFilter(feed->filter);
	}

	if (av7110->full_ts) {
		budget_stop_feed(feed);
		return ret;
	}

	if (feed->type == DMX_TYPE_SEC) {
		for (i = 0; i<demux->filternum; i++) {
			if (demux->filter[i].state == DMX_STATE_GO &&
			    demux->filter[i].filter.parent == &feed->feed.sec) {
				demux->filter[i].state = DMX_STATE_READY;
				if (demux->dmx.frontend->source != DMX_MEMORY_FE) {
					rc = StopHWFilter(&demux->filter[i]);
					if (!ret)
						ret = rc;
					/* keep going, stop as many filters as possible */
				}
			}
		}
	}

	return ret;
}


static void restart_feeds(struct av7110 *av7110)
{
	struct dvb_demux *dvbdmx = &av7110->demux;
	struct dvb_demux_feed *feed;
	int mode;
	int feeding;
	int i, j;

	dprintk(4, "%p\n", av7110);

	mode = av7110->playing;
	av7110->playing = 0;
	av7110->rec_mode = 0;

	feeding = av7110->feeding1; /* full_ts mod */

	for (i = 0; i < dvbdmx->feednum; i++) {
		feed = &dvbdmx->feed[i];
		if (feed->state == DMX_STATE_GO) {
			if (feed->type == DMX_TYPE_SEC) {
				for (j = 0; j < dvbdmx->filternum; j++) {
					if (dvbdmx->filter[j].type != DMX_TYPE_SEC)
						continue;
					if (dvbdmx->filter[j].filter.parent != &feed->feed.sec)
						continue;
					if (dvbdmx->filter[j].state == DMX_STATE_GO)
						dvbdmx->filter[j].state = DMX_STATE_READY;
				}
			}
			av7110_start_feed(feed);
		}
	}

	av7110->feeding1 = feeding; /* full_ts mod */

	if (mode)
		av7110_av_start_play(av7110, mode);
}

static int dvb_get_stc(struct dmx_demux *demux, unsigned int num,
		       uint64_t *stc, unsigned int *base)
{
	int ret;
	u16 fwstc[4];
	u16 tag = ((COMTYPE_REQUEST << 8) + ReqSTC);
	struct dvb_demux *dvbdemux;
	struct av7110 *av7110;

	/* pointer casting paranoia... */
	BUG_ON(!demux);
	dvbdemux = demux->priv;
	BUG_ON(!dvbdemux);
	av7110 = dvbdemux->priv;

	dprintk(4, "%p\n", av7110);

	if (num != 0)
		return -EINVAL;

	ret = av7110_fw_request(av7110, &tag, 0, fwstc, 4);
	if (ret) {
		printk(KERN_ERR "%s: av7110_fw_request error\n", __func__);
		return ret;
	}
	dprintk(2, "fwstc = %04hx %04hx %04hx %04hx\n",
		fwstc[0], fwstc[1], fwstc[2], fwstc[3]);

	*stc =	(((uint64_t) ((fwstc[3] & 0x8000) >> 15)) << 32) |
		(((uint64_t)  fwstc[1]) << 16) | ((uint64_t) fwstc[0]);
	*base = 1;

	dprintk(4, "stc = %lu\n", (unsigned long)*stc);

	return 0;
}


/******************************************************************************
 * SEC device file operations
 ******************************************************************************/


static int av7110_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct av7110* av7110 = fe->dvb->priv;

	switch (tone) {
	case SEC_TONE_ON:
		return Set22K(av7110, 1);

	case SEC_TONE_OFF:
		return Set22K(av7110, 0);

	default:
		return -EINVAL;
	}
}

static int av7110_diseqc_send_master_cmd(struct dvb_frontend* fe,
					 struct dvb_diseqc_master_cmd* cmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	return av7110_diseqc_send(av7110, cmd->msg_len, cmd->msg, -1);
}

static int av7110_diseqc_send_burst(struct dvb_frontend* fe,
				    fe_sec_mini_cmd_t minicmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	return av7110_diseqc_send(av7110, 0, NULL, minicmd);
}

/* simplified code from budget-core.c */
static int stop_ts_capture(struct av7110 *budget)
{
	dprintk(2, "budget: %p\n", budget);

	if (--budget->feeding1)
		return budget->feeding1;
	saa7146_write(budget->dev, MC1, MASK_20);	/* DMA3 off */
	SAA7146_IER_DISABLE(budget->dev, MASK_10);
	SAA7146_ISR_CLEAR(budget->dev, MASK_10);
	return 0;
}

static int start_ts_capture(struct av7110 *budget)
{
	dprintk(2, "budget: %p\n", budget);

	if (budget->feeding1)
		return ++budget->feeding1;
	memset(budget->grabbing, 0x00, TS_BUFLEN);
	budget->ttbp = 0;
	SAA7146_ISR_CLEAR(budget->dev, MASK_10);  /* VPE */
	SAA7146_IER_ENABLE(budget->dev, MASK_10); /* VPE */
	saa7146_write(budget->dev, MC1, (MASK_04 | MASK_20)); /* DMA3 on */
	return ++budget->feeding1;
}

static int budget_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct av7110 *budget = demux->priv;
	int status;

	dprintk(2, "av7110: %p\n", budget);

	spin_lock(&budget->feedlock1);
	feed->pusi_seen = 0; /* have a clean section start */
	status = start_ts_capture(budget);
	spin_unlock(&budget->feedlock1);
	return status;
}

static int budget_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct av7110 *budget = demux->priv;
	int status;

	dprintk(2, "budget: %p\n", budget);

	spin_lock(&budget->feedlock1);
	status = stop_ts_capture(budget);
	spin_unlock(&budget->feedlock1);
	return status;
}

static void vpeirq(unsigned long cookie)
{
	struct av7110 *budget = (struct av7110 *)cookie;
	u8 *mem = (u8 *) (budget->grabbing);
	u32 olddma = budget->ttbp;
	u32 newdma = saa7146_read(budget->dev, PCI_VDP3);
	struct dvb_demux *demux = budget->full_ts ? &budget->demux : &budget->demux1;

	/* nearest lower position divisible by 188 */
	newdma -= newdma % 188;

	if (newdma >= TS_BUFLEN)
		return;

	budget->ttbp = newdma;

	if (!budget->feeding1 || (newdma == olddma))
		return;

	/* Ensure streamed PCI data is synced to CPU */
	pci_dma_sync_sg_for_cpu(budget->dev->pci, budget->pt.slist, budget->pt.nents, PCI_DMA_FROMDEVICE);

#if 0
	/* track rps1 activity */
	printk("vpeirq: %02x Event Counter 1 0x%04x\n",
	       mem[olddma],
	       saa7146_read(budget->dev, EC1R) & 0x3fff);
#endif

	if (newdma > olddma)
		/* no wraparound, dump olddma..newdma */
		dvb_dmx_swfilter_packets(demux, mem + olddma, (newdma - olddma) / 188);
	else {
		/* wraparound, dump olddma..buflen and 0..newdma */
		dvb_dmx_swfilter_packets(demux, mem + olddma, (TS_BUFLEN - olddma) / 188);
		dvb_dmx_swfilter_packets(demux, mem, newdma / 188);
	}
}

static int av7110_register(struct av7110 *av7110)
{
	int ret, i;
	struct dvb_demux *dvbdemux = &av7110->demux;
	struct dvb_demux *dvbdemux1 = &av7110->demux1;

	dprintk(4, "%p\n", av7110);

	if (av7110->registered)
		return -1;

	av7110->registered = 1;

	dvbdemux->priv = (void *) av7110;

	for (i = 0; i < 32; i++)
		av7110->handle2filter[i] = NULL;

	dvbdemux->filternum = (av7110->full_ts) ? 256 : 32;
	dvbdemux->feednum = (av7110->full_ts) ? 256 : 32;
	dvbdemux->start_feed = av7110_start_feed;
	dvbdemux->stop_feed = av7110_stop_feed;
	dvbdemux->write_to_decoder = av7110_write_to_decoder;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING | DMX_SECTION_FILTERING |
				      DMX_MEMORY_BASED_FILTERING);

	dvb_dmx_init(&av7110->demux);
	av7110->demux.dmx.get_stc = dvb_get_stc;

	av7110->dmxdev.filternum = (av7110->full_ts) ? 256 : 32;
	av7110->dmxdev.demux = &dvbdemux->dmx;
	av7110->dmxdev.capabilities = 0;

	dvb_dmxdev_init(&av7110->dmxdev, &av7110->dvb_adapter);

	av7110->hw_frontend.source = DMX_FRONTEND_0;

	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &av7110->hw_frontend);

	if (ret < 0)
		return ret;

	av7110->mem_frontend.source = DMX_MEMORY_FE;

	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &av7110->mem_frontend);

	if (ret < 0)
		return ret;

	ret = dvbdemux->dmx.connect_frontend(&dvbdemux->dmx,
					     &av7110->hw_frontend);
	if (ret < 0)
		return ret;

	av7110_av_register(av7110);
	av7110_ca_register(av7110);

#ifdef CONFIG_DVB_AV7110_OSD
	dvb_register_device(&av7110->dvb_adapter, &av7110->osd_dev,
			    &dvbdev_osd, av7110, DVB_DEVICE_OSD);
#endif

	dvb_net_init(&av7110->dvb_adapter, &av7110->dvb_net, &dvbdemux->dmx);

	if (budgetpatch) {
		/* initialize software demux1 without its own frontend
		 * demux1 hardware is connected to frontend0 of demux0
		 */
		dvbdemux1->priv = (void *) av7110;

		dvbdemux1->filternum = 256;
		dvbdemux1->feednum = 256;
		dvbdemux1->start_feed = budget_start_feed;
		dvbdemux1->stop_feed = budget_stop_feed;
		dvbdemux1->write_to_decoder = NULL;

		dvbdemux1->dmx.capabilities = (DMX_TS_FILTERING | DMX_SECTION_FILTERING |
					       DMX_MEMORY_BASED_FILTERING);

		dvb_dmx_init(&av7110->demux1);

		av7110->dmxdev1.filternum = 256;
		av7110->dmxdev1.demux = &dvbdemux1->dmx;
		av7110->dmxdev1.capabilities = 0;

		dvb_dmxdev_init(&av7110->dmxdev1, &av7110->dvb_adapter);

		dvb_net_init(&av7110->dvb_adapter, &av7110->dvb_net1, &dvbdemux1->dmx);
		printk("dvb-ttpci: additional demux1 for budget-patch registered\n");
	}
	return 0;
}


static void dvb_unregister(struct av7110 *av7110)
{
	struct dvb_demux *dvbdemux = &av7110->demux;
	struct dvb_demux *dvbdemux1 = &av7110->demux1;

	dprintk(4, "%p\n", av7110);

	if (!av7110->registered)
		return;

	if (budgetpatch) {
		dvb_net_release(&av7110->dvb_net1);
		dvbdemux->dmx.close(&dvbdemux1->dmx);
		dvb_dmxdev_release(&av7110->dmxdev1);
		dvb_dmx_release(&av7110->demux1);
	}

	dvb_net_release(&av7110->dvb_net);

	dvbdemux->dmx.close(&dvbdemux->dmx);
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->hw_frontend);
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->mem_frontend);

	dvb_dmxdev_release(&av7110->dmxdev);
	dvb_dmx_release(&av7110->demux);

	if (av7110->fe != NULL) {
		dvb_unregister_frontend(av7110->fe);
		dvb_frontend_detach(av7110->fe);
	}
	dvb_unregister_device(av7110->osd_dev);
	av7110_av_unregister(av7110);
	av7110_ca_unregister(av7110);
}


/****************************************************************************
 * I2C client commands
 ****************************************************************************/

int i2c_writereg(struct av7110 *av7110, u8 id, u8 reg, u8 val)
{
	u8 msg[2] = { reg, val };
	struct i2c_msg msgs;

	msgs.flags = 0;
	msgs.addr = id / 2;
	msgs.len = 2;
	msgs.buf = msg;
	return i2c_transfer(&av7110->i2c_adap, &msgs, 1);
}

u8 i2c_readreg(struct av7110 *av7110, u8 id, u8 reg)
{
	u8 mm1[] = {0x00};
	u8 mm2[] = {0x00};
	struct i2c_msg msgs[2];

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = id / 2;
	mm1[0] = reg;
	msgs[0].len = 1; msgs[1].len = 1;
	msgs[0].buf = mm1; msgs[1].buf = mm2;
	i2c_transfer(&av7110->i2c_adap, msgs, 2);

	return mm2[0];
}

/****************************************************************************
 * INITIALIZATION
 ****************************************************************************/


static int check_firmware(struct av7110* av7110)
{
	u32 crc = 0, len = 0;
	unsigned char *ptr;

	/* check for firmware magic */
	ptr = av7110->bin_fw;
	if (ptr[0] != 'A' || ptr[1] != 'V' ||
	    ptr[2] != 'F' || ptr[3] != 'W') {
		printk("dvb-ttpci: this is not an av7110 firmware\n");
		return -EINVAL;
	}
	ptr += 4;

	/* check dpram file */
	crc = get_unaligned_be32(ptr);
	ptr += 4;
	len = get_unaligned_be32(ptr);
	ptr += 4;
	if (len >= 512) {
		printk("dvb-ttpci: dpram file is way too big.\n");
		return -EINVAL;
	}
	if (crc != crc32_le(0, ptr, len)) {
		printk("dvb-ttpci: crc32 of dpram file does not match.\n");
		return -EINVAL;
	}
	av7110->bin_dpram = ptr;
	av7110->size_dpram = len;
	ptr += len;

	/* check root file */
	crc = get_unaligned_be32(ptr);
	ptr += 4;
	len = get_unaligned_be32(ptr);
	ptr += 4;

	if (len <= 200000 || len >= 300000 ||
	    len > ((av7110->bin_fw + av7110->size_fw) - ptr)) {
		printk("dvb-ttpci: root file has strange size (%d). aborting.\n", len);
		return -EINVAL;
	}
	if( crc != crc32_le(0, ptr, len)) {
		printk("dvb-ttpci: crc32 of root file does not match.\n");
		return -EINVAL;
	}
	av7110->bin_root = ptr;
	av7110->size_root = len;
	return 0;
}

static void put_firmware(struct av7110* av7110)
{
	vfree(av7110->bin_fw);
}

static int get_firmware(struct av7110* av7110)
{
	int ret;
	const struct firmware *fw;

	/* request the av7110 firmware, this will block until someone uploads it */
	ret = request_firmware(&fw, "dvb-ttpci-01.fw", &av7110->dev->pci->dev);
	if (ret) {
		if (ret == -ENOENT) {
			printk(KERN_ERR "dvb-ttpci: could not load firmware,"
			       " file not found: dvb-ttpci-01.fw\n");
			printk(KERN_ERR "dvb-ttpci: usually this should be in "
			       "/usr/lib/hotplug/firmware or /lib/firmware\n");
			printk(KERN_ERR "dvb-ttpci: and can be downloaded from"
			       " http://www.linuxtv.org/download/dvb/firmware/\n");
		} else
			printk(KERN_ERR "dvb-ttpci: cannot request firmware"
			       " (error %i)\n", ret);
		return -EINVAL;
	}

	if (fw->size <= 200000) {
		printk("dvb-ttpci: this firmware is way too small.\n");
		release_firmware(fw);
		return -EINVAL;
	}

	/* check if the firmware is available */
	av7110->bin_fw = vmalloc(fw->size);
	if (NULL == av7110->bin_fw) {
		dprintk(1, "out of memory\n");
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(av7110->bin_fw, fw->data, fw->size);
	av7110->size_fw = fw->size;
	if ((ret = check_firmware(av7110)))
		vfree(av7110->bin_fw);

	release_firmware(fw);
	return ret;
}

static int alps_bsrv2_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct av7110* av7110 = fe->dvb->priv;
	u8 pwr = 0;
	u8 buf[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	u32 div = (p->frequency + 479500) / 125;

	if (p->frequency > 2000000)
		pwr = 3;
	else if (p->frequency > 1800000)
		pwr = 2;
	else if (p->frequency > 1600000)
		pwr = 1;
	else if (p->frequency > 1200000)
		pwr = 0;
	else if (p->frequency >= 1100000)
		pwr = 1;
	else
		pwr = 2;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = ((div & 0x18000) >> 10) | 0x95;
	buf[3] = (pwr << 6) | 0x30;

	// NOTE: since we're using a prescaler of 2, we set the
	// divisor frequency to 62.5kHz and divide by 125 above

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1x93_config alps_bsrv2_config = {
	.demod_address = 0x08,
	.xin = 90100000UL,
	.invert_pwm = 0,
};

static int alps_tdbe2_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x62, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (p->frequency + 35937500 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85 | ((div >> 10) & 0x60);
	data[3] = (p->frequency < 174000000 ? 0x88 : p->frequency < 470000000 ? 0x84 : 0x81);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1820_config alps_tdbe2_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
};




static int grundig_29504_451_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = p->frequency / 125;
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = 0x00;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct tda8083_config grundig_29504_451_config = {
	.demod_address = 0x68,
};



static int philips_cd1516_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u32 f = p->frequency;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (f + 36125000 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = (f < 174000000 ? 0xa1 : f < 470000000 ? 0x92 : 0x34);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1820_config philips_cd1516_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
};



static int alps_tdlb7_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct av7110* av7110 = fe->dvb->priv;
	u32 div, pwr;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x60, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (p->frequency + 36200000) / 166666;

	if (p->frequency <= 782000000)
		pwr = 1;
	else
		pwr = 2;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85;
	data[3] = pwr << 6;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static int alps_tdlb7_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
#if IS_ENABLED(CONFIG_DVB_SP8870)
	struct av7110* av7110 = fe->dvb->priv;

	return request_firmware(fw, name, &av7110->dev->pci->dev);
#else
	return -EINVAL;
#endif
}

static struct sp8870_config alps_tdlb7_config = {

	.demod_address = 0x71,
	.request_firmware = alps_tdlb7_request_firmware,
};


static u8 nexusca_stv0297_inittab[] = {
	0x80, 0x01,
	0x80, 0x00,
	0x81, 0x01,
	0x81, 0x00,
	0x00, 0x09,
	0x01, 0x69,
	0x03, 0x00,
	0x04, 0x00,
	0x07, 0x00,
	0x08, 0x00,
	0x20, 0x00,
	0x21, 0x40,
	0x22, 0x00,
	0x23, 0x00,
	0x24, 0x40,
	0x25, 0x88,
	0x30, 0xff,
	0x31, 0x00,
	0x32, 0xff,
	0x33, 0x00,
	0x34, 0x50,
	0x35, 0x7f,
	0x36, 0x00,
	0x37, 0x20,
	0x38, 0x00,
	0x40, 0x1c,
	0x41, 0xff,
	0x42, 0x29,
	0x43, 0x00,
	0x44, 0xff,
	0x45, 0x00,
	0x46, 0x00,
	0x49, 0x04,
	0x4a, 0x00,
	0x4b, 0x7b,
	0x52, 0x30,
	0x55, 0xae,
	0x56, 0x47,
	0x57, 0xe1,
	0x58, 0x3a,
	0x5a, 0x1e,
	0x5b, 0x34,
	0x60, 0x00,
	0x63, 0x00,
	0x64, 0x00,
	0x65, 0x00,
	0x66, 0x00,
	0x67, 0x00,
	0x68, 0x00,
	0x69, 0x00,
	0x6a, 0x02,
	0x6b, 0x00,
	0x70, 0xff,
	0x71, 0x00,
	0x72, 0x00,
	0x73, 0x00,
	0x74, 0x0c,
	0x80, 0x00,
	0x81, 0x00,
	0x82, 0x00,
	0x83, 0x00,
	0x84, 0x04,
	0x85, 0x80,
	0x86, 0x24,
	0x87, 0x78,
	0x88, 0x10,
	0x89, 0x00,
	0x90, 0x01,
	0x91, 0x01,
	0xa0, 0x04,
	0xa1, 0x00,
	0xa2, 0x00,
	0xb0, 0x91,
	0xb1, 0x0b,
	0xc0, 0x53,
	0xc1, 0x70,
	0xc2, 0x12,
	0xd0, 0x00,
	0xd1, 0x00,
	0xd2, 0x00,
	0xd3, 0x00,
	0xd4, 0x00,
	0xd5, 0x00,
	0xde, 0x00,
	0xdf, 0x00,
	0x61, 0x49,
	0x62, 0x0b,
	0x53, 0x08,
	0x59, 0x08,
	0xff, 0xff,
};

static int nexusca_stv0297_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x63, .flags = 0, .buf = data, .len = sizeof(data) };
	struct i2c_msg readmsg = { .addr = 0x63, .flags = I2C_M_RD, .buf = data, .len = 1 };
	int i;

	div = (p->frequency + 36150000 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0xce;

	if (p->frequency < 45000000)
		return -EINVAL;
	else if (p->frequency < 137000000)
		data[3] = 0x01;
	else if (p->frequency < 403000000)
		data[3] = 0x02;
	else if (p->frequency < 860000000)
		data[3] = 0x04;
	else
		return -EINVAL;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&av7110->i2c_adap, &msg, 1) != 1) {
		printk("nexusca: pll transfer failed!\n");
		return -EIO;
	}

	// wait for PLL lock
	for(i = 0; i < 20; i++) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		if (i2c_transfer(&av7110->i2c_adap, &readmsg, 1) == 1)
			if (data[0] & 0x40) break;
		msleep(10);
	}

	return 0;
}

static struct stv0297_config nexusca_stv0297_config = {

	.demod_address = 0x1C,
	.inittab = nexusca_stv0297_inittab,
	.invert = 1,
	.stop_during_read = 1,
};



static int grundig_29504_401_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u8 cfg, cpump, band_select;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (36125000 + p->frequency) / 166666;

	cfg = 0x88;

	if (p->frequency < 175000000)
		cpump = 2;
	else if (p->frequency < 390000000)
		cpump = 1;
	else if (p->frequency < 470000000)
		cpump = 2;
	else if (p->frequency < 750000000)
		cpump = 1;
	else
		cpump = 3;

	if (p->frequency < 175000000)
		band_select = 0x0e;
	else if (p->frequency < 470000000)
		band_select = 0x05;
	else
		band_select = 0x03;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = ((div >> 10) & 0x60) | cfg;
	data[3] = (cpump << 6) | band_select;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct l64781_config grundig_29504_401_config = {
	.demod_address = 0x55,
};



static int av7110_fe_lock_fix(struct av7110* av7110, fe_status_t status)
{
	int ret = 0;
	int synced = (status & FE_HAS_LOCK) ? 1 : 0;

	av7110->fe_status = status;

	if (av7110->fe_synced == synced)
		return 0;

	if (av7110->playing) {
		av7110->fe_synced = synced;
		return 0;
	}

	if (mutex_lock_interruptible(&av7110->pid_mutex))
		return -ERESTARTSYS;

	if (synced) {
		ret = SetPIDs(av7110, av7110->pids[DMX_PES_VIDEO],
			av7110->pids[DMX_PES_AUDIO],
			av7110->pids[DMX_PES_TELETEXT], 0,
			av7110->pids[DMX_PES_PCR]);
		if (!ret)
			ret = av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, Scan, 0);
	} else {
		ret = SetPIDs(av7110, 0, 0, 0, 0, 0);
		if (!ret) {
			ret = av7110_fw_cmd(av7110, COMTYPE_PID_FILTER, FlushTSQueue, 0);
			if (!ret)
				ret = av7110_wait_msgstate(av7110, GPMQBusy);
		}
	}

	if (!ret)
		av7110->fe_synced = synced;

	mutex_unlock(&av7110->pid_mutex);
	return ret;
}

static int av7110_fe_set_frontend(struct dvb_frontend *fe)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret)
		ret = av7110->fe_set_frontend(fe);

	return ret;
}

static int av7110_fe_init(struct dvb_frontend* fe)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret)
		ret = av7110->fe_init(fe);
	return ret;
}

static int av7110_fe_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct av7110* av7110 = fe->dvb->priv;

	/* call the real implementation */
	int ret = av7110->fe_read_status(fe, status);
	if (!ret)
		if (((*status ^ av7110->fe_status) & FE_HAS_LOCK) && (*status & FE_HAS_LOCK))
			ret = av7110_fe_lock_fix(av7110, *status);
	return ret;
}

static int av7110_fe_diseqc_reset_overload(struct dvb_frontend* fe)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret)
		ret = av7110->fe_diseqc_reset_overload(fe);
	return ret;
}

static int av7110_fe_diseqc_send_master_cmd(struct dvb_frontend* fe,
					    struct dvb_diseqc_master_cmd* cmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret) {
		av7110->saved_master_cmd = *cmd;
		ret = av7110->fe_diseqc_send_master_cmd(fe, cmd);
	}
	return ret;
}

static int av7110_fe_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret) {
		av7110->saved_minicmd = minicmd;
		ret = av7110->fe_diseqc_send_burst(fe, minicmd);
	}
	return ret;
}

static int av7110_fe_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret) {
		av7110->saved_tone = tone;
		ret = av7110->fe_set_tone(fe, tone);
	}
	return ret;
}

static int av7110_fe_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret) {
		av7110->saved_voltage = voltage;
		ret = av7110->fe_set_voltage(fe, voltage);
	}
	return ret;
}

static int av7110_fe_dishnetwork_send_legacy_command(struct dvb_frontend* fe, unsigned long cmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	int ret = av7110_fe_lock_fix(av7110, 0);
	if (!ret)
		ret = av7110->fe_dishnetwork_send_legacy_command(fe, cmd);
	return ret;
}

static void dvb_s_recover(struct av7110* av7110)
{
	av7110_fe_init(av7110->fe);

	av7110_fe_set_voltage(av7110->fe, av7110->saved_voltage);
	if (av7110->saved_master_cmd.msg_len) {
		msleep(20);
		av7110_fe_diseqc_send_master_cmd(av7110->fe, &av7110->saved_master_cmd);
	}
	msleep(20);
	av7110_fe_diseqc_send_burst(av7110->fe, av7110->saved_minicmd);
	msleep(20);
	av7110_fe_set_tone(av7110->fe, av7110->saved_tone);

	av7110_fe_set_frontend(av7110->fe);
}

static u8 read_pwm(struct av7110* av7110)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { { .addr = 0x50,.flags = 0,.buf = &b,.len = 1 },
				 { .addr = 0x50,.flags = I2C_M_RD,.buf = &pwm,.len = 1} };

	if ((i2c_transfer(&av7110->i2c_adap, msg, 2) != 2) || (pwm == 0xff))
		pwm = 0x48;

	return pwm;
}

static int frontend_init(struct av7110 *av7110)
{
	int ret;

	if (av7110->dev->pci->subsystem_vendor == 0x110a) {
		switch(av7110->dev->pci->subsystem_device) {
		case 0x0000: // Fujitsu/Siemens DVB-Cable (ves1820/Philips CD1516(??))
			av7110->fe = dvb_attach(ves1820_attach, &philips_cd1516_config,
						    &av7110->i2c_adap, read_pwm(av7110));
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = philips_cd1516_tuner_set_params;
			}
			break;
		}

	} else if (av7110->dev->pci->subsystem_vendor == 0x13c2) {
		switch(av7110->dev->pci->subsystem_device) {
		case 0x0000: // Hauppauge/TT WinTV DVB-S rev1.X
		case 0x0003: // Hauppauge/TT WinTV Nexus-S Rev 2.X
		case 0x1002: // Hauppauge/TT WinTV DVB-S rev1.3SE

			// try the ALPS BSRV2 first of all
			av7110->fe = dvb_attach(ves1x93_attach, &alps_bsrv2_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = alps_bsrv2_tuner_set_params;
				av7110->fe->ops.diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops.diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops.set_tone = av7110_set_tone;
				av7110->recover = dvb_s_recover;
				break;
			}

			// try the ALPS BSRU6 now
			av7110->fe = dvb_attach(stv0299_attach, &alps_bsru6_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = alps_bsru6_tuner_set_params;
				av7110->fe->tuner_priv = &av7110->i2c_adap;

				av7110->fe->ops.diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops.diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops.set_tone = av7110_set_tone;
				av7110->recover = dvb_s_recover;
				break;
			}

			// Try the grundig 29504-451
			av7110->fe = dvb_attach(tda8083_attach, &grundig_29504_451_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = grundig_29504_451_tuner_set_params;
				av7110->fe->ops.diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops.diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops.set_tone = av7110_set_tone;
				av7110->recover = dvb_s_recover;
				break;
			}

			/* Try DVB-C cards */
			switch(av7110->dev->pci->subsystem_device) {
			case 0x0000:
				/* Siemens DVB-C (full-length card) VES1820/Philips CD1516 */
				av7110->fe = dvb_attach(ves1820_attach, &philips_cd1516_config, &av7110->i2c_adap,
							read_pwm(av7110));
				if (av7110->fe) {
					av7110->fe->ops.tuner_ops.set_params = philips_cd1516_tuner_set_params;
				}
				break;
			case 0x0003:
				/* Hauppauge DVB-C 2.1 VES1820/ALPS TDBE2 */
				av7110->fe = dvb_attach(ves1820_attach, &alps_tdbe2_config, &av7110->i2c_adap,
							read_pwm(av7110));
				if (av7110->fe) {
					av7110->fe->ops.tuner_ops.set_params = alps_tdbe2_tuner_set_params;
				}
				break;
			}
			break;

		case 0x0001: // Hauppauge/TT Nexus-T premium rev1.X
			// try ALPS TDLB7 first, then Grundig 29504-401
			av7110->fe = dvb_attach(sp8870_attach, &alps_tdlb7_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = alps_tdlb7_tuner_set_params;
				break;
			}
			/* fall-thru */

		case 0x0008: // Hauppauge/TT DVB-T
			// Grundig 29504-401
			av7110->fe = dvb_attach(l64781_attach, &grundig_29504_401_config, &av7110->i2c_adap);
			if (av7110->fe)
				av7110->fe->ops.tuner_ops.set_params = grundig_29504_401_tuner_set_params;
			break;

		case 0x0002: // Hauppauge/TT DVB-C premium rev2.X

			av7110->fe = dvb_attach(ves1820_attach, &alps_tdbe2_config, &av7110->i2c_adap, read_pwm(av7110));
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = alps_tdbe2_tuner_set_params;
			}
			break;

		case 0x0004: // Galaxis DVB-S rev1.3
			/* ALPS BSRV2 */
			av7110->fe = dvb_attach(ves1x93_attach, &alps_bsrv2_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = alps_bsrv2_tuner_set_params;
				av7110->fe->ops.diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops.diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops.set_tone = av7110_set_tone;
				av7110->recover = dvb_s_recover;
			}
			break;

		case 0x0006: /* Fujitsu-Siemens DVB-S rev 1.6 */
			/* Grundig 29504-451 */
			av7110->fe = dvb_attach(tda8083_attach, &grundig_29504_451_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = grundig_29504_451_tuner_set_params;
				av7110->fe->ops.diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops.diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops.set_tone = av7110_set_tone;
				av7110->recover = dvb_s_recover;
			}
			break;

		case 0x000A: // Hauppauge/TT Nexus-CA rev1.X

			av7110->fe = dvb_attach(stv0297_attach, &nexusca_stv0297_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = nexusca_stv0297_tuner_set_params;

				/* set TDA9819 into DVB mode */
				saa7146_setgpio(av7110->dev, 1, SAA7146_GPIO_OUTLO); // TDA9819 pin9(STD)
				saa7146_setgpio(av7110->dev, 3, SAA7146_GPIO_OUTLO); // TDA9819 pin30(VIF)

				/* tuner on this needs a slower i2c bus speed */
				av7110->dev->i2c_bitrate = SAA7146_I2C_BUS_BIT_RATE_240;
				break;
			}
			break;

		case 0x000E: /* Hauppauge/TT Nexus-S rev 2.3 */
			/* ALPS BSBE1 */
			av7110->fe = dvb_attach(stv0299_attach, &alps_bsbe1_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops.tuner_ops.set_params = alps_bsbe1_tuner_set_params;
				av7110->fe->tuner_priv = &av7110->i2c_adap;

				if (dvb_attach(lnbp21_attach, av7110->fe, &av7110->i2c_adap, 0, 0) == NULL) {
					printk("dvb-ttpci: LNBP21 not found!\n");
					if (av7110->fe->ops.release)
						av7110->fe->ops.release(av7110->fe);
					av7110->fe = NULL;
				} else {
					av7110->fe->ops.dishnetwork_send_legacy_command = NULL;
					av7110->recover = dvb_s_recover;
				}
			}
			break;
		}
	}

	if (!av7110->fe) {
		/* FIXME: propagate the failure code from the lower layers */
		ret = -ENOMEM;
		printk("dvb-ttpci: A frontend driver was not found for device [%04x:%04x] subsystem [%04x:%04x]\n",
		       av7110->dev->pci->vendor,
		       av7110->dev->pci->device,
		       av7110->dev->pci->subsystem_vendor,
		       av7110->dev->pci->subsystem_device);
	} else {
		FE_FUNC_OVERRIDE(av7110->fe->ops.init, av7110->fe_init, av7110_fe_init);
		FE_FUNC_OVERRIDE(av7110->fe->ops.read_status, av7110->fe_read_status, av7110_fe_read_status);
		FE_FUNC_OVERRIDE(av7110->fe->ops.diseqc_reset_overload, av7110->fe_diseqc_reset_overload, av7110_fe_diseqc_reset_overload);
		FE_FUNC_OVERRIDE(av7110->fe->ops.diseqc_send_master_cmd, av7110->fe_diseqc_send_master_cmd, av7110_fe_diseqc_send_master_cmd);
		FE_FUNC_OVERRIDE(av7110->fe->ops.diseqc_send_burst, av7110->fe_diseqc_send_burst, av7110_fe_diseqc_send_burst);
		FE_FUNC_OVERRIDE(av7110->fe->ops.set_tone, av7110->fe_set_tone, av7110_fe_set_tone);
		FE_FUNC_OVERRIDE(av7110->fe->ops.set_voltage, av7110->fe_set_voltage, av7110_fe_set_voltage);
		FE_FUNC_OVERRIDE(av7110->fe->ops.dishnetwork_send_legacy_command, av7110->fe_dishnetwork_send_legacy_command, av7110_fe_dishnetwork_send_legacy_command);
		FE_FUNC_OVERRIDE(av7110->fe->ops.set_frontend, av7110->fe_set_frontend, av7110_fe_set_frontend);

		ret = dvb_register_frontend(&av7110->dvb_adapter, av7110->fe);
		if (ret < 0) {
			printk("av7110: Frontend registration failed!\n");
			dvb_frontend_detach(av7110->fe);
			av7110->fe = NULL;
		}
	}
	return ret;
}

/* Budgetpatch note:
 * Original hardware design by Roberto Deza:
 * There is a DVB_Wiki at
 * http://www.linuxtv.org/
 *
 * New software triggering design by Emard that works on
 * original Roberto Deza's hardware:
 *
 * rps1 code for budgetpatch will copy internal HS event to GPIO3 pin.
 * GPIO3 is in budget-patch hardware connectd to port B VSYNC
 * HS is an internal event of 7146, accessible with RPS
 * and temporarily raised high every n lines
 * (n in defined in the RPS_THRESH1 counter threshold)
 * I think HS is raised high on the beginning of the n-th line
 * and remains high until this n-th line that triggered
 * it is completely received. When the receiption of n-th line
 * ends, HS is lowered.
 *
 * To transmit data over DMA, 7146 needs changing state at
 * port B VSYNC pin. Any changing of port B VSYNC will
 * cause some DMA data transfer, with more or less packets loss.
 * It depends on the phase and frequency of VSYNC and
 * the way of 7146 is instructed to trigger on port B (defined
 * in DD1_INIT register, 3rd nibble from the right valid
 * numbers are 0-7, see datasheet)
 *
 * The correct triggering can minimize packet loss,
 * dvbtraffic should give this stable bandwidths:
 *   22k transponder = 33814 kbit/s
 * 27.5k transponder = 38045 kbit/s
 * by experiment it is found that the best results
 * (stable bandwidths and almost no packet loss)
 * are obtained using DD1_INIT triggering number 2
 * (Va at rising edge of VS Fa = HS x VS-failing forced toggle)
 * and a VSYNC phase that occurs in the middle of DMA transfer
 * (about byte 188*512=96256 in the DMA window).
 *
 * Phase of HS is still not clear to me how to control,
 * It just happens to be so. It can be seen if one enables
 * RPS_IRQ and print Event Counter 1 in vpeirq(). Every
 * time RPS_INTERRUPT is called, the Event Counter 1 will
 * increment. That's how the 7146 is programmed to do event
 * counting in this budget-patch.c
 * I *think* HPS setting has something to do with the phase
 * of HS but I can't be 100% sure in that.
 *
 * hardware debug note: a working budget card (including budget patch)
 * with vpeirq() interrupt setup in mode "0x90" (every 64K) will
 * generate 3 interrupts per 25-Hz DMA frame of 2*188*512 bytes
 * and that means 3*25=75 Hz of interrupt freqency, as seen by
 * watch cat /proc/interrupts
 *
 * If this frequency is 3x lower (and data received in the DMA
 * buffer don't start with 0x47, but in the middle of packets,
 * whose lengths appear to be like 188 292 188 104 etc.
 * this means VSYNC line is not connected in the hardware.
 * (check soldering pcb and pins)
 * The same behaviour of missing VSYNC can be duplicated on budget
 * cards, by seting DD1_INIT trigger mode 7 in 3rd nibble.
 */
static int av7110_attach(struct saa7146_dev* dev,
			 struct saa7146_pci_extension_data *pci_ext)
{
	const int length = TS_WIDTH * TS_HEIGHT;
	struct pci_dev *pdev = dev->pci;
	struct av7110 *av7110;
	struct task_struct *thread;
	int ret, count = 0;

	dprintk(4, "dev: %p\n", dev);

	/* Set RPS_IRQ to 1 to track rps1 activity.
	 * Enabling this won't send any interrupt to PC CPU.
	 */
#define RPS_IRQ 0

	if (budgetpatch == 1) {
		budgetpatch = 0;
		/* autodetect the presence of budget patch
		 * this only works if saa7146 has been recently
		 * reset with with MASK_31 to MC1
		 *
		 * will wait for VBI_B event (vertical blank at port B)
		 * and will reset GPIO3 after VBI_B is detected.
		 * (GPIO3 should be raised high by CPU to
		 * test if GPIO3 will generate vertical blank signal
		 * in budget patch GPIO3 is connected to VSYNC_B
		 */

		/* RESET SAA7146 */
		saa7146_write(dev, MC1, MASK_31);
		/* autodetection success seems to be time-dependend after reset */

		/* Fix VSYNC level */
		saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
		/* set vsync_b triggering */
		saa7146_write(dev, DD1_STREAM_B, 0);
		/* port B VSYNC at rising edge */
		saa7146_write(dev, DD1_INIT, 0x00000200);
		saa7146_write(dev, BRS_CTRL, 0x00000000);  // VBI
		saa7146_write(dev, MC2,
			      1 * (MASK_08 | MASK_24)  |   // BRS control
			      0 * (MASK_09 | MASK_25)  |   // a
			      1 * (MASK_10 | MASK_26)  |   // b
			      0 * (MASK_06 | MASK_22)  |   // HPS_CTRL1
			      0 * (MASK_05 | MASK_21)  |   // HPS_CTRL2
			      0 * (MASK_01 | MASK_15)      // DEBI
		);

		/* start writing RPS1 code from beginning */
		count = 0;
		/* Disable RPS1 */
		saa7146_write(dev, MC1, MASK_29);
		/* RPS1 timeout disable */
		saa7146_write(dev, RPS_TOV1, 0);
		WRITE_RPS1(CMD_PAUSE | EVT_VBI_B);
		WRITE_RPS1(CMD_WR_REG_MASK | (GPIO_CTRL>>2));
		WRITE_RPS1(GPIO3_MSK);
		WRITE_RPS1(SAA7146_GPIO_OUTLO<<24);
#if RPS_IRQ
		/* issue RPS1 interrupt to increment counter */
		WRITE_RPS1(CMD_INTERRUPT);
#endif
		WRITE_RPS1(CMD_STOP);
		/* Jump to begin of RPS program as safety measure               (p37) */
		WRITE_RPS1(CMD_JUMP);
		WRITE_RPS1(dev->d_rps1.dma_handle);

#if RPS_IRQ
		/* set event counter 1 source as RPS1 interrupt (0x03)          (rE4 p53)
		 * use 0x03 to track RPS1 interrupts - increase by 1 every gpio3 is toggled
		 * use 0x15 to track VPE  interrupts - increase by 1 every vpeirq() is called
		 */
		saa7146_write(dev, EC1SSR, (0x03<<2) | 3 );
		/* set event counter 1 threshold to maximum allowed value        (rEC p55) */
		saa7146_write(dev, ECT1R,  0x3fff );
#endif
		/* Set RPS1 Address register to point to RPS code               (r108 p42) */
		saa7146_write(dev, RPS_ADDR1, dev->d_rps1.dma_handle);
		/* Enable RPS1,                                                 (rFC p33) */
		saa7146_write(dev, MC1, (MASK_13 | MASK_29 ));

		mdelay(10);
		/* now send VSYNC_B to rps1 by rising GPIO3 */
		saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
		mdelay(10);
		/* if rps1 responded by lowering the GPIO3,
		 * then we have budgetpatch hardware
		 */
		if ((saa7146_read(dev, GPIO_CTRL) & 0x10000000) == 0) {
			budgetpatch = 1;
			printk("dvb-ttpci: BUDGET-PATCH DETECTED.\n");
		}
		/* Disable RPS1 */
		saa7146_write(dev, MC1, ( MASK_29 ));
#if RPS_IRQ
		printk("dvb-ttpci: Event Counter 1 0x%04x\n", saa7146_read(dev, EC1R) & 0x3fff );
#endif
	}

	/* prepare the av7110 device struct */
	av7110 = kzalloc(sizeof(struct av7110), GFP_KERNEL);
	if (!av7110) {
		dprintk(1, "out of memory\n");
		return -ENOMEM;
	}

	av7110->card_name = (char*) pci_ext->ext_priv;
	av7110->dev = dev;
	dev->ext_priv = av7110;

	ret = get_firmware(av7110);
	if (ret < 0)
		goto err_kfree_0;

	ret = dvb_register_adapter(&av7110->dvb_adapter, av7110->card_name,
				   THIS_MODULE, &dev->pci->dev, adapter_nr);
	if (ret < 0)
		goto err_put_firmware_1;

	/* the Siemens DVB needs this if you want to have the i2c chips
	   get recognized before the main driver is fully loaded */
	saa7146_write(dev, GPIO_CTRL, 0x500000);

	strlcpy(av7110->i2c_adap.name, pci_ext->ext_priv, sizeof(av7110->i2c_adap.name));

	saa7146_i2c_adapter_prepare(dev, &av7110->i2c_adap, SAA7146_I2C_BUS_BIT_RATE_120); /* 275 kHz */

	ret = i2c_add_adapter(&av7110->i2c_adap);
	if (ret < 0)
		goto err_dvb_unregister_adapter_2;

	ttpci_eeprom_parse_mac(&av7110->i2c_adap,
			       av7110->dvb_adapter.proposed_mac);
	ret = -ENOMEM;

	/* full-ts mod? */
	if (full_ts)
		av7110->full_ts = true;

	/* check for full-ts flag in eeprom */
	if (i2c_readreg(av7110, 0xaa, 0) == 0x4f && i2c_readreg(av7110, 0xaa, 1) == 0x45) {
		u8 flags = i2c_readreg(av7110, 0xaa, 2);
		if (flags != 0xff && (flags & 0x01))
			av7110->full_ts = true;
	}

	if (av7110->full_ts) {
		printk(KERN_INFO "dvb-ttpci: full-ts mode enabled for saa7146 port B\n");
		spin_lock_init(&av7110->feedlock1);
		av7110->grabbing = saa7146_vmalloc_build_pgtable(pdev, length,
								 &av7110->pt);
		if (!av7110->grabbing)
			goto err_i2c_del_3;

		saa7146_write(dev, DD1_STREAM_B, 0x00000000);
		saa7146_write(dev, MC2, (MASK_10 | MASK_26));

		saa7146_write(dev, DD1_INIT, 0x00000600);
		saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

		saa7146_write(dev, BRS_CTRL, 0x60000000);
		saa7146_write(dev, MC2, MASK_08 | MASK_24);

		/* dma3 */
		saa7146_write(dev, PCI_BT_V1, 0x001c0000 | (saa7146_read(dev, PCI_BT_V1) & ~0x001f0000));
		saa7146_write(dev, BASE_ODD3, 0);
		saa7146_write(dev, BASE_EVEN3, 0);
		saa7146_write(dev, PROT_ADDR3, TS_WIDTH * TS_HEIGHT);
		saa7146_write(dev, PITCH3, TS_WIDTH);
		saa7146_write(dev, BASE_PAGE3, av7110->pt.dma | ME1 | 0x90);
		saa7146_write(dev, NUM_LINE_BYTE3, (TS_HEIGHT << 16) | TS_WIDTH);
		saa7146_write(dev, MC2, MASK_04 | MASK_20);

		tasklet_init(&av7110->vpe_tasklet, vpeirq, (unsigned long) av7110);

	} else if (budgetpatch) {
		spin_lock_init(&av7110->feedlock1);
		av7110->grabbing = saa7146_vmalloc_build_pgtable(pdev, length,
								 &av7110->pt);
		if (!av7110->grabbing)
			goto err_i2c_del_3;

		saa7146_write(dev, PCI_BT_V1, 0x1c1f101f);
		saa7146_write(dev, BCS_CTRL, 0x80400040);
		/* set dd1 stream a & b */
		saa7146_write(dev, DD1_STREAM_B, 0x00000000);
		saa7146_write(dev, DD1_INIT, 0x03000200);
		saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));
		saa7146_write(dev, BRS_CTRL, 0x60000000);
		saa7146_write(dev, BASE_ODD3, 0);
		saa7146_write(dev, BASE_EVEN3, 0);
		saa7146_write(dev, PROT_ADDR3, TS_WIDTH * TS_HEIGHT);
		saa7146_write(dev, BASE_PAGE3, av7110->pt.dma | ME1 | 0x90);

		saa7146_write(dev, PITCH3, TS_WIDTH);
		saa7146_write(dev, NUM_LINE_BYTE3, (TS_HEIGHT << 16) | TS_WIDTH);

		/* upload all */
		saa7146_write(dev, MC2, 0x077c077c);
		saa7146_write(dev, GPIO_CTRL, 0x000000);
#if RPS_IRQ
		/* set event counter 1 source as RPS1 interrupt (0x03)          (rE4 p53)
		 * use 0x03 to track RPS1 interrupts - increase by 1 every gpio3 is toggled
		 * use 0x15 to track VPE  interrupts - increase by 1 every vpeirq() is called
		 */
		saa7146_write(dev, EC1SSR, (0x03<<2) | 3 );
		/* set event counter 1 threshold to maximum allowed value        (rEC p55) */
		saa7146_write(dev, ECT1R,  0x3fff );
#endif
		/* Setup BUDGETPATCH MAIN RPS1 "program" (p35) */
		count = 0;

		/* Wait Source Line Counter Threshold                           (p36) */
		WRITE_RPS1(CMD_PAUSE | EVT_HS);
		/* Set GPIO3=1                                                  (p42) */
		WRITE_RPS1(CMD_WR_REG_MASK | (GPIO_CTRL>>2));
		WRITE_RPS1(GPIO3_MSK);
		WRITE_RPS1(SAA7146_GPIO_OUTHI<<24);
#if RPS_IRQ
		/* issue RPS1 interrupt */
		WRITE_RPS1(CMD_INTERRUPT);
#endif
		/* Wait reset Source Line Counter Threshold                     (p36) */
		WRITE_RPS1(CMD_PAUSE | RPS_INV | EVT_HS);
		/* Set GPIO3=0                                                  (p42) */
		WRITE_RPS1(CMD_WR_REG_MASK | (GPIO_CTRL>>2));
		WRITE_RPS1(GPIO3_MSK);
		WRITE_RPS1(SAA7146_GPIO_OUTLO<<24);
#if RPS_IRQ
		/* issue RPS1 interrupt */
		WRITE_RPS1(CMD_INTERRUPT);
#endif
		/* Jump to begin of RPS program                                 (p37) */
		WRITE_RPS1(CMD_JUMP);
		WRITE_RPS1(dev->d_rps1.dma_handle);

		/* Fix VSYNC level */
		saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
		/* Set RPS1 Address register to point to RPS code               (r108 p42) */
		saa7146_write(dev, RPS_ADDR1, dev->d_rps1.dma_handle);
		/* Set Source Line Counter Threshold, using BRS                 (rCC p43)
		 * It generates HS event every TS_HEIGHT lines
		 * this is related to TS_WIDTH set in register
		 * NUM_LINE_BYTE3. If NUM_LINE_BYTE low 16 bits
		 * are set to TS_WIDTH bytes (TS_WIDTH=2*188),
		 * then RPS_THRESH1 should be set to trigger
		 * every TS_HEIGHT (512) lines.
		 */
		saa7146_write(dev, RPS_THRESH1, (TS_HEIGHT*1) | MASK_12 );

		/* Enable RPS1                                                  (rFC p33) */
		saa7146_write(dev, MC1, (MASK_13 | MASK_29));

		/* end of budgetpatch register initialization */
		tasklet_init (&av7110->vpe_tasklet,  vpeirq,  (unsigned long) av7110);
	} else {
		saa7146_write(dev, PCI_BT_V1, 0x1c00101f);
		saa7146_write(dev, BCS_CTRL, 0x80400040);

		/* set dd1 stream a & b */
		saa7146_write(dev, DD1_STREAM_B, 0x00000000);
		saa7146_write(dev, DD1_INIT, 0x03000000);
		saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

		/* upload all */
		saa7146_write(dev, MC2, 0x077c077c);
		saa7146_write(dev, GPIO_CTRL, 0x000000);
	}

	tasklet_init (&av7110->debi_tasklet, debiirq, (unsigned long) av7110);
	tasklet_init (&av7110->gpio_tasklet, gpioirq, (unsigned long) av7110);

	mutex_init(&av7110->pid_mutex);

	/* locks for data transfers from/to AV7110 */
	spin_lock_init(&av7110->debilock);
	mutex_init(&av7110->dcomlock);
	av7110->debitype = -1;

	/* default OSD window */
	av7110->osdwin = 1;
	mutex_init(&av7110->osd_mutex);

	/* TV standard */
	av7110->vidmode = tv_standard == 1 ? AV7110_VIDEO_MODE_NTSC
					   : AV7110_VIDEO_MODE_PAL;

	/* ARM "watchdog" */
	init_waitqueue_head(&av7110->arm_wait);
	av7110->arm_thread = NULL;

	/* allocate and init buffers */
	av7110->debi_virt = pci_alloc_consistent(pdev, 8192, &av7110->debi_bus);
	if (!av7110->debi_virt)
		goto err_saa71466_vfree_4;


	av7110->iobuf = vmalloc(AVOUTLEN+AOUTLEN+BMPLEN+4*IPACKS);
	if (!av7110->iobuf)
		goto err_pci_free_5;

	ret = av7110_av_init(av7110);
	if (ret < 0)
		goto err_iobuf_vfree_6;

	/* init BMP buffer */
	av7110->bmpbuf = av7110->iobuf+AVOUTLEN+AOUTLEN;
	init_waitqueue_head(&av7110->bmpq);

	ret = av7110_ca_init(av7110);
	if (ret < 0)
		goto err_av7110_av_exit_7;

	/* load firmware into AV7110 cards */
	ret = av7110_bootarm(av7110);
	if (ret < 0)
		goto err_av7110_ca_exit_8;

	ret = av7110_firmversion(av7110);
	if (ret < 0)
		goto err_stop_arm_9;

	if (FW_VERSION(av7110->arm_app)<0x2501)
		printk ("dvb-ttpci: Warning, firmware version 0x%04x is too old. "
			"System might be unstable!\n", FW_VERSION(av7110->arm_app));

	thread = kthread_run(arm_thread, (void *) av7110, "arm_mon");
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		goto err_stop_arm_9;
	}
	av7110->arm_thread = thread;

	/* set initial volume in mixer struct */
	av7110->mixer.volume_left  = volume;
	av7110->mixer.volume_right = volume;

	ret = av7110_register(av7110);
	if (ret < 0)
		goto err_arm_thread_stop_10;

	init_av7110_av(av7110);

	/* special case DVB-C: these cards have an analog tuner
	   plus need some special handling, so we have separate
	   saa7146_ext_vv data for these... */
	ret = av7110_init_v4l(av7110);
	if (ret < 0)
		goto err_av7110_unregister_11;

	av7110->dvb_adapter.priv = av7110;
	ret = frontend_init(av7110);
	if (ret < 0)
		goto err_av7110_exit_v4l_12;

	mutex_init(&av7110->ioctl_mutex);

#if IS_ENABLED(CONFIG_INPUT_EVDEV)
	av7110_ir_init(av7110);
#endif
	printk(KERN_INFO "dvb-ttpci: found av7110-%d.\n", av7110_num);
	av7110_num++;
out:
	return ret;

err_av7110_exit_v4l_12:
	av7110_exit_v4l(av7110);
err_av7110_unregister_11:
	dvb_unregister(av7110);
err_arm_thread_stop_10:
	av7110_arm_sync(av7110);
err_stop_arm_9:
	/* Nothing to do. Rejoice. */
err_av7110_ca_exit_8:
	av7110_ca_exit(av7110);
err_av7110_av_exit_7:
	av7110_av_exit(av7110);
err_iobuf_vfree_6:
	vfree(av7110->iobuf);
err_pci_free_5:
	pci_free_consistent(pdev, 8192, av7110->debi_virt, av7110->debi_bus);
err_saa71466_vfree_4:
	if (av7110->grabbing)
		saa7146_vfree_destroy_pgtable(pdev, av7110->grabbing, &av7110->pt);
err_i2c_del_3:
	i2c_del_adapter(&av7110->i2c_adap);
err_dvb_unregister_adapter_2:
	dvb_unregister_adapter(&av7110->dvb_adapter);
err_put_firmware_1:
	put_firmware(av7110);
err_kfree_0:
	kfree(av7110);
	goto out;
}

static int av7110_detach(struct saa7146_dev* saa)
{
	struct av7110 *av7110 = saa->ext_priv;
	dprintk(4, "%p\n", av7110);

#if IS_ENABLED(CONFIG_INPUT_EVDEV)
	av7110_ir_exit(av7110);
#endif
	if (budgetpatch || av7110->full_ts) {
		if (budgetpatch) {
			/* Disable RPS1 */
			saa7146_write(saa, MC1, MASK_29);
			/* VSYNC LOW (inactive) */
			saa7146_setgpio(saa, 3, SAA7146_GPIO_OUTLO);
		}
		saa7146_write(saa, MC1, MASK_20);	/* DMA3 off */
		SAA7146_IER_DISABLE(saa, MASK_10);
		SAA7146_ISR_CLEAR(saa, MASK_10);
		msleep(50);
		tasklet_kill(&av7110->vpe_tasklet);
		saa7146_vfree_destroy_pgtable(saa->pci, av7110->grabbing, &av7110->pt);
	}
	av7110_exit_v4l(av7110);

	av7110_arm_sync(av7110);

	tasklet_kill(&av7110->debi_tasklet);
	tasklet_kill(&av7110->gpio_tasklet);

	dvb_unregister(av7110);

	SAA7146_IER_DISABLE(saa, MASK_19 | MASK_03);
	SAA7146_ISR_CLEAR(saa, MASK_19 | MASK_03);

	av7110_ca_exit(av7110);
	av7110_av_exit(av7110);

	vfree(av7110->iobuf);
	pci_free_consistent(saa->pci, 8192, av7110->debi_virt,
			    av7110->debi_bus);

	i2c_del_adapter(&av7110->i2c_adap);

	dvb_unregister_adapter (&av7110->dvb_adapter);

	av7110_num--;

	put_firmware(av7110);

	kfree(av7110);

	saa->ext_priv = NULL;

	return 0;
}


static void av7110_irq(struct saa7146_dev* dev, u32 *isr)
{
	struct av7110 *av7110 = dev->ext_priv;

	//print_time("av7110_irq");

	/* Note: Don't try to handle the DEBI error irq (MASK_18), in
	 * intel mode the timeout is asserted all the time...
	 */

	if (*isr & MASK_19) {
		//printk("av7110_irq: DEBI\n");
		/* Note 1: The DEBI irq is level triggered: We must enable it
		 * only after we started a DMA xfer, and disable it here
		 * immediately, or it will be signalled all the time while
		 * DEBI is idle.
		 * Note 2: You would think that an irq which is masked is
		 * not signalled by the hardware. Not so for the SAA7146:
		 * An irq is signalled as long as the corresponding bit
		 * in the ISR is set, and disabling irqs just prevents the
		 * hardware from setting the ISR bit. This means a) that we
		 * must clear the ISR *after* disabling the irq (which is why
		 * we must do it here even though saa7146_core did it already),
		 * and b) that if we were to disable an edge triggered irq
		 * (like the gpio irqs sadly are) temporarily we would likely
		 * loose some. This sucks :-(
		 */
		SAA7146_IER_DISABLE(av7110->dev, MASK_19);
		SAA7146_ISR_CLEAR(av7110->dev, MASK_19);
		tasklet_schedule(&av7110->debi_tasklet);
	}

	if (*isr & MASK_03) {
		//printk("av7110_irq: GPIO\n");
		tasklet_schedule(&av7110->gpio_tasklet);
	}

	if (*isr & MASK_10)
		tasklet_schedule(&av7110->vpe_tasklet);
}


static struct saa7146_extension av7110_extension_driver;

#define MAKE_AV7110_INFO(x_var,x_name) \
static struct saa7146_pci_extension_data x_var = { \
	.ext_priv = x_name, \
	.ext = &av7110_extension_driver }

MAKE_AV7110_INFO(tts_1_X_fsc,"Technotrend/Hauppauge WinTV DVB-S rev1.X or Fujitsu Siemens DVB-C");
MAKE_AV7110_INFO(ttt_1_X,    "Technotrend/Hauppauge WinTV DVB-T rev1.X");
MAKE_AV7110_INFO(ttc_1_X,    "Technotrend/Hauppauge WinTV Nexus-CA rev1.X");
MAKE_AV7110_INFO(ttc_2_X,    "Technotrend/Hauppauge WinTV DVB-C rev2.X");
MAKE_AV7110_INFO(tts_2_X,    "Technotrend/Hauppauge WinTV Nexus-S rev2.X");
MAKE_AV7110_INFO(tts_2_3,    "Technotrend/Hauppauge WinTV Nexus-S rev2.3");
MAKE_AV7110_INFO(tts_1_3se,  "Technotrend/Hauppauge WinTV DVB-S rev1.3 SE");
MAKE_AV7110_INFO(ttt,        "Technotrend/Hauppauge DVB-T");
MAKE_AV7110_INFO(fsc,        "Fujitsu Siemens DVB-C");
MAKE_AV7110_INFO(fss,        "Fujitsu Siemens DVB-S rev1.6");
MAKE_AV7110_INFO(gxs_1_3,    "Galaxis DVB-S rev1.3");

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(fsc,         0x110a, 0x0000),
	MAKE_EXTENSION_PCI(tts_1_X_fsc, 0x13c2, 0x0000),
	MAKE_EXTENSION_PCI(ttt_1_X,     0x13c2, 0x0001),
	MAKE_EXTENSION_PCI(ttc_2_X,     0x13c2, 0x0002),
	MAKE_EXTENSION_PCI(tts_2_X,     0x13c2, 0x0003),
	MAKE_EXTENSION_PCI(gxs_1_3,     0x13c2, 0x0004),
	MAKE_EXTENSION_PCI(fss,         0x13c2, 0x0006),
	MAKE_EXTENSION_PCI(ttt,         0x13c2, 0x0008),
	MAKE_EXTENSION_PCI(ttc_1_X,     0x13c2, 0x000a),
	MAKE_EXTENSION_PCI(tts_2_3,     0x13c2, 0x000e),
	MAKE_EXTENSION_PCI(tts_1_3se,   0x13c2, 0x1002),

/*	MAKE_EXTENSION_PCI(???, 0x13c2, 0x0005), UNDEFINED CARD */ // Technisat SkyStar1
/*	MAKE_EXTENSION_PCI(???, 0x13c2, 0x0009), UNDEFINED CARD */ // TT/Hauppauge WinTV Nexus-CA v????

	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);


static struct saa7146_extension av7110_extension_driver = {
	.name		= "av7110",
	.flags		= SAA7146_USE_I2C_IRQ,

	.module		= THIS_MODULE,
	.pci_tbl	= &pci_tbl[0],
	.attach		= av7110_attach,
	.detach		= av7110_detach,

	.irq_mask	= MASK_19 | MASK_03 | MASK_10,
	.irq_func	= av7110_irq,
};


static int __init av7110_init(void)
{
	int retval;
	retval = saa7146_register_extension(&av7110_extension_driver);
	return retval;
}


static void __exit av7110_exit(void)
{
	saa7146_unregister_extension(&av7110_extension_driver);
}

module_init(av7110_init);
module_exit(av7110_exit);

MODULE_DESCRIPTION("driver for the SAA7146 based AV110 PCI DVB cards by "
		   "Siemens, Technotrend, Hauppauge");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, others");
MODULE_LICENSE("GPL");
