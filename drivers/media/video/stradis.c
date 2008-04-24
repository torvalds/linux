/*
 * stradis.c - stradis 4:2:2 mpeg decoder driver
 *
 * Stradis 4:2:2 MPEG-2 Decoder Driver
 * Copyright (C) 1999 Nathan Laredo <laredo@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/types.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>

#include "saa7146.h"
#include "saa7146reg.h"
#include "ibmmpeg2.h"
#include "saa7121.h"
#include "cs8420.h"

#define DEBUG(x)		/* debug driver */
#undef  IDEBUG			/* debug irq handler */
#undef  MDEBUG			/* debug memory management */

#define SAA7146_MAX 6

static struct saa7146 saa7146s[SAA7146_MAX];

static int saa_num;		/* number of SAA7146s in use */

static int video_nr = -1;
module_param(video_nr, int, 0);
MODULE_LICENSE("GPL");

#define nDebNormal	0x00480000
#define nDebNoInc	0x00480000
#define nDebVideo	0xd0480000
#define nDebAudio	0xd0400000
#define nDebDMA		0x02c80000

#define oDebNormal	0x13c80000
#define oDebNoInc	0x13c80000
#define oDebVideo	0xd1080000
#define oDebAudio	0xd1080000
#define oDebDMA		0x03080000

#define NewCard		(saa->boardcfg[3])
#define ChipControl	(saa->boardcfg[1])
#define NTSCFirstActive	(saa->boardcfg[4])
#define PALFirstActive	(saa->boardcfg[5])
#define NTSCLastActive	(saa->boardcfg[54])
#define PALLastActive	(saa->boardcfg[55])
#define Have2MB		(saa->boardcfg[18] & 0x40)
#define HaveCS8420	(saa->boardcfg[18] & 0x04)
#define IBMMPEGCD20	(saa->boardcfg[18] & 0x20)
#define HaveCS3310	(saa->boardcfg[18] & 0x01)
#define CS3310MaxLvl	((saa->boardcfg[30] << 8) | saa->boardcfg[31])
#define HaveCS4341	(saa->boardcfg[40] == 2)
#define SDIType		(saa->boardcfg[27])
#define CurrentMode	(saa->boardcfg[2])

#define debNormal	(NewCard ? nDebNormal : oDebNormal)
#define debNoInc	(NewCard ? nDebNoInc : oDebNoInc)
#define debVideo	(NewCard ? nDebVideo : oDebVideo)
#define debAudio	(NewCard ? nDebAudio : oDebAudio)
#define debDMA		(NewCard ? nDebDMA : oDebDMA)

#ifdef USE_RESCUE_EEPROM_SDM275
static unsigned char rescue_eeprom[64] = {
	0x00, 0x01, 0x04, 0x13, 0x26, 0x0f, 0x10, 0x00, 0x00, 0x00, 0x43, 0x63,
	0x22, 0x01, 0x29, 0x15, 0x73, 0x00, 0x1f,  'd',  'e',  'c',  'x',  'l',
	 'd',  'v',  'a', 0x02, 0x00, 0x01, 0x00, 0xcc, 0xa4, 0x63, 0x09, 0xe2,
	0x10, 0x00, 0x0a, 0x00, 0x02, 0x02,  'd',  'e',  'c',  'x',  'l',  'a',
	0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};
#endif

/* ----------------------------------------------------------------------- */
/* Hardware I2C functions */
static void I2CWipe(struct saa7146 *saa)
{
	int i;
	/* set i2c to ~=100kHz, abort transfer, clear busy */
	saawrite(0x600 | SAA7146_I2C_ABORT, SAA7146_I2C_STATUS);
	saawrite((SAA7146_MC2_UPLD_I2C << 16) |
		 SAA7146_MC2_UPLD_I2C, SAA7146_MC2);
	/* wait for i2c registers to be programmed */
	for (i = 0; i < 1000 &&
	     !(saaread(SAA7146_MC2) & SAA7146_MC2_UPLD_I2C); i++)
		schedule();
	saawrite(0x600, SAA7146_I2C_STATUS);
	saawrite((SAA7146_MC2_UPLD_I2C << 16) |
		 SAA7146_MC2_UPLD_I2C, SAA7146_MC2);
	/* wait for i2c registers to be programmed */
	for (i = 0; i < 1000 &&
	     !(saaread(SAA7146_MC2) & SAA7146_MC2_UPLD_I2C); i++)
		schedule();
	saawrite(0x600, SAA7146_I2C_STATUS);
	saawrite((SAA7146_MC2_UPLD_I2C << 16) |
		 SAA7146_MC2_UPLD_I2C, SAA7146_MC2);
	/* wait for i2c registers to be programmed */
	for (i = 0; i < 1000 &&
	     !(saaread(SAA7146_MC2) & SAA7146_MC2_UPLD_I2C); i++)
		schedule();
}

/* read I2C */
static int I2CRead(struct saa7146 *saa, unsigned char addr,
		   unsigned char subaddr, int dosub)
{
	int i;

	if (saaread(SAA7146_I2C_STATUS) & 0x3c)
		I2CWipe(saa);
	for (i = 0;
		i < 1000 && (saaread(SAA7146_I2C_STATUS) & SAA7146_I2C_BUSY);
		i++)
		schedule();
	if (i == 1000)
		I2CWipe(saa);
	if (dosub)
		saawrite(((addr & 0xfe) << 24) | (((addr | 1) & 0xff) << 8) |
			((subaddr & 0xff) << 16) | 0xed, SAA7146_I2C_TRANSFER);
	else
		saawrite(((addr & 0xfe) << 24) | (((addr | 1) & 0xff) << 16) |
			0xf1, SAA7146_I2C_TRANSFER);
	saawrite((SAA7146_MC2_UPLD_I2C << 16) |
		 SAA7146_MC2_UPLD_I2C, SAA7146_MC2);
	/* wait for i2c registers to be programmed */
	for (i = 0; i < 1000 &&
	     !(saaread(SAA7146_MC2) & SAA7146_MC2_UPLD_I2C); i++)
		schedule();
	/* wait for valid data */
	for (i = 0; i < 1000 &&
	     (saaread(SAA7146_I2C_STATUS) & SAA7146_I2C_BUSY); i++)
		schedule();
	if (saaread(SAA7146_I2C_STATUS) & SAA7146_I2C_ERR)
		return -1;
	if (i == 1000)
		printk("i2c setup read timeout\n");
	saawrite(0x41, SAA7146_I2C_TRANSFER);
	saawrite((SAA7146_MC2_UPLD_I2C << 16) |
		 SAA7146_MC2_UPLD_I2C, SAA7146_MC2);
	/* wait for i2c registers to be programmed */
	for (i = 0; i < 1000 &&
	     !(saaread(SAA7146_MC2) & SAA7146_MC2_UPLD_I2C); i++)
		schedule();
	/* wait for valid data */
	for (i = 0; i < 1000 &&
	     (saaread(SAA7146_I2C_TRANSFER) & SAA7146_I2C_BUSY); i++)
		schedule();
	if (saaread(SAA7146_I2C_TRANSFER) & SAA7146_I2C_ERR)
		return -1;
	if (i == 1000)
		printk("i2c read timeout\n");
	return ((saaread(SAA7146_I2C_TRANSFER) >> 24) & 0xff);
}

/* set both to write both bytes, reset it to write only b1 */

static int I2CWrite(struct saa7146 *saa, unsigned char addr, unsigned char b1,
		    unsigned char b2, int both)
{
	int i;
	u32 data;

	if (saaread(SAA7146_I2C_STATUS) & 0x3c)
		I2CWipe(saa);
	for (i = 0; i < 1000 &&
	     (saaread(SAA7146_I2C_STATUS) & SAA7146_I2C_BUSY); i++)
		schedule();
	if (i == 1000)
		I2CWipe(saa);
	data = ((addr & 0xfe) << 24) | ((b1 & 0xff) << 16);
	if (both)
		data |= ((b2 & 0xff) << 8) | 0xe5;
	else
		data |= 0xd1;
	saawrite(data, SAA7146_I2C_TRANSFER);
	saawrite((SAA7146_MC2_UPLD_I2C << 16) | SAA7146_MC2_UPLD_I2C,
		 SAA7146_MC2);
	return 0;
}

static void attach_inform(struct saa7146 *saa, int id)
{
	int i;

	DEBUG(printk(KERN_DEBUG "stradis%d: i2c: device found=%02x\n", saa->nr,
		id));
	if (id == 0xa0) {	/* we have rev2 or later board, fill in info */
		for (i = 0; i < 64; i++)
			saa->boardcfg[i] = I2CRead(saa, 0xa0, i, 1);
#ifdef USE_RESCUE_EEPROM_SDM275
		if (saa->boardcfg[0] != 0) {
			printk("stradis%d: WARNING: EEPROM STORED VALUES HAVE "
				"BEEN IGNORED\n", saa->nr);
			for (i = 0; i < 64; i++)
				saa->boardcfg[i] = rescue_eeprom[i];
		}
#endif
		printk("stradis%d: config =", saa->nr);
		for (i = 0; i < 51; i++) {
			printk(" %02x", saa->boardcfg[i]);
		}
		printk("\n");
	}
}

static void I2CBusScan(struct saa7146 *saa)
{
	int i;
	for (i = 0; i < 0xff; i += 2)
		if ((I2CRead(saa, i, 0, 0)) >= 0)
			attach_inform(saa, i);
}

static int debiwait_maxwait;

static int wait_for_debi_done(struct saa7146 *saa)
{
	int i;

	/* wait for registers to be programmed */
	for (i = 0; i < 100000 &&
	     !(saaread(SAA7146_MC2) & SAA7146_MC2_UPLD_DEBI); i++)
		saaread(SAA7146_MC2);
	/* wait for transfer to complete */
	for (i = 0; i < 500000 &&
	     (saaread(SAA7146_PSR) & SAA7146_PSR_DEBI_S); i++)
		saaread(SAA7146_MC2);

	if (i > debiwait_maxwait)
		printk("wait-for-debi-done maxwait: %d\n",
			debiwait_maxwait = i);

	if (i == 500000)
		return -1;

	return 0;
}

static int debiwrite(struct saa7146 *saa, u32 config, int addr,
	u32 val, int count)
{
	u32 cmd;
	if (count <= 0 || count > 32764)
		return -1;
	if (wait_for_debi_done(saa) < 0)
		return -1;
	saawrite(config, SAA7146_DEBI_CONFIG);
	if (count <= 4)		/* immediate transfer */
		saawrite(val, SAA7146_DEBI_AD);
	else			/* block transfer */
		saawrite(virt_to_bus(saa->dmadebi), SAA7146_DEBI_AD);
	saawrite((cmd = (count << 17) | (addr & 0xffff)), SAA7146_DEBI_COMMAND);
	saawrite((SAA7146_MC2_UPLD_DEBI << 16) | SAA7146_MC2_UPLD_DEBI,
		 SAA7146_MC2);
	return 0;
}

static u32 debiread(struct saa7146 *saa, u32 config, int addr, int count)
{
	u32 result = 0;

	if (count > 32764 || count <= 0)
		return 0;
	if (wait_for_debi_done(saa) < 0)
		return 0;
	saawrite(virt_to_bus(saa->dmadebi), SAA7146_DEBI_AD);
	saawrite((count << 17) | 0x10000 | (addr & 0xffff),
		 SAA7146_DEBI_COMMAND);
	saawrite(config, SAA7146_DEBI_CONFIG);
	saawrite((SAA7146_MC2_UPLD_DEBI << 16) | SAA7146_MC2_UPLD_DEBI,
		 SAA7146_MC2);
	if (count > 4)		/* not an immediate transfer */
		return count;
	wait_for_debi_done(saa);
	result = saaread(SAA7146_DEBI_AD);
	if (count == 1)
		result &= 0xff;
	if (count == 2)
		result &= 0xffff;
	if (count == 3)
		result &= 0xffffff;
	return result;
}

static void do_irq_send_data(struct saa7146 *saa)
{
	int split, audbytes, vidbytes;

	saawrite(SAA7146_PSR_PIN1, SAA7146_IER);
	/* if special feature mode in effect, disable audio sending */
	if (saa->playmode != VID_PLAY_NORMAL)
		saa->audtail = saa->audhead = 0;
	if (saa->audhead <= saa->audtail)
		audbytes = saa->audtail - saa->audhead;
	else
		audbytes = 65536 - (saa->audhead - saa->audtail);
	if (saa->vidhead <= saa->vidtail)
		vidbytes = saa->vidtail - saa->vidhead;
	else
		vidbytes = 524288 - (saa->vidhead - saa->vidtail);
	if (audbytes == 0 && vidbytes == 0 && saa->osdtail == saa->osdhead) {
		saawrite(0, SAA7146_IER);
		return;
	}
	/* if at least 1 block audio waiting and audio fifo isn't full */
	if (audbytes >= 2048 && (debiread(saa, debNormal, IBM_MP2_AUD_FIFO, 2)
			& 0xff) < 60) {
		if (saa->audhead > saa->audtail)
			split = 65536 - saa->audhead;
		else
			split = 0;
		audbytes = 2048;
		if (split > 0 && split < 2048) {
			memcpy(saa->dmadebi, saa->audbuf + saa->audhead, split);
			saa->audhead = 0;
			audbytes -= split;
		} else
			split = 0;
		memcpy(saa->dmadebi + split, saa->audbuf + saa->audhead,
			audbytes);
		saa->audhead += audbytes;
		saa->audhead &= 0xffff;
		debiwrite(saa, debAudio, (NewCard ? IBM_MP2_AUD_FIFO :
			IBM_MP2_AUD_FIFOW), 0, 2048);
		wake_up_interruptible(&saa->audq);
		/* if at least 1 block video waiting and video fifo isn't full */
	} else if (vidbytes >= 30720 && (debiread(saa, debNormal,
						  IBM_MP2_FIFO, 2)) < 16384) {
		if (saa->vidhead > saa->vidtail)
			split = 524288 - saa->vidhead;
		else
			split = 0;
		vidbytes = 30720;
		if (split > 0 && split < 30720) {
			memcpy(saa->dmadebi, saa->vidbuf + saa->vidhead, split);
			saa->vidhead = 0;
			vidbytes -= split;
		} else
			split = 0;
		memcpy(saa->dmadebi + split, saa->vidbuf + saa->vidhead,
			vidbytes);
		saa->vidhead += vidbytes;
		saa->vidhead &= 0x7ffff;
		debiwrite(saa, debVideo, (NewCard ? IBM_MP2_FIFO :
					  IBM_MP2_FIFOW), 0, 30720);
		wake_up_interruptible(&saa->vidq);
	}
	saawrite(SAA7146_PSR_DEBI_S | SAA7146_PSR_PIN1, SAA7146_IER);
}

static void send_osd_data(struct saa7146 *saa)
{
	int size = saa->osdtail - saa->osdhead;
	if (size > 30720)
		size = 30720;
	/* ensure some multiple of 8 bytes is transferred */
	size = 8 * ((size + 8) >> 3);
	if (size) {
		debiwrite(saa, debNormal, IBM_MP2_OSD_ADDR,
			  (saa->osdhead >> 3), 2);
		memcpy(saa->dmadebi, &saa->osdbuf[saa->osdhead], size);
		saa->osdhead += size;
		/* block transfer of next 8 bytes to ~32k bytes */
		debiwrite(saa, debNormal, IBM_MP2_OSD_DATA, 0, size);
	}
	if (saa->osdhead >= saa->osdtail) {
		saa->osdhead = saa->osdtail = 0;
		debiwrite(saa, debNormal, IBM_MP2_MASK0, 0xc00c, 2);
	}
}

static irqreturn_t saa7146_irq(int irq, void *dev_id)
{
	struct saa7146 *saa = dev_id;
	u32 stat, astat;
	int count;
	int handled = 0;

	count = 0;
	while (1) {
		/* get/clear interrupt status bits */
		stat = saaread(SAA7146_ISR);
		astat = stat & saaread(SAA7146_IER);
		if (!astat)
			break;
		handled = 1;
		saawrite(astat, SAA7146_ISR);
		if (astat & SAA7146_PSR_DEBI_S) {
			do_irq_send_data(saa);
		}
		if (astat & SAA7146_PSR_PIN1) {
			int istat;
			/* the following read will trigger DEBI_S */
			istat = debiread(saa, debNormal, IBM_MP2_HOST_INT, 2);
			if (istat & 1) {
				saawrite(0, SAA7146_IER);
				send_osd_data(saa);
				saawrite(SAA7146_PSR_DEBI_S |
					 SAA7146_PSR_PIN1, SAA7146_IER);
			}
			if (istat & 0x20) {	/* Video Start */
				saa->vidinfo.frame_count++;
			}
			if (istat & 0x400) {	/* Picture Start */
				/* update temporal reference */
			}
			if (istat & 0x200) {	/* Picture Resolution Change */
				/* read new resolution */
			}
			if (istat & 0x100) {	/* New User Data found */
				/* read new user data */
			}
			if (istat & 0x1000) {	/* new GOP/SMPTE */
				/* read new SMPTE */
			}
			if (istat & 0x8000) {	/* Sequence Start Code */
				/* reset frame counter, load sizes */
				saa->vidinfo.frame_count = 0;
				saa->vidinfo.h_size = 704;
				saa->vidinfo.v_size = 480;
#if 0
				if (saa->endmarkhead != saa->endmarktail) {
					saa->audhead =
						saa->endmark[saa->endmarkhead];
					saa->endmarkhead++;
					if (saa->endmarkhead >= MAX_MARKS)
						saa->endmarkhead = 0;
				}
#endif
			}
			if (istat & 0x4000) {	/* Sequence Error Code */
				if (saa->endmarkhead != saa->endmarktail) {
					saa->audhead =
						saa->endmark[saa->endmarkhead];
					saa->endmarkhead++;
					if (saa->endmarkhead >= MAX_MARKS)
						saa->endmarkhead = 0;
				}
			}
		}
#ifdef IDEBUG
		if (astat & SAA7146_PSR_PPEF) {
			IDEBUG(printk("stradis%d irq: PPEF\n", saa->nr));
		}
		if (astat & SAA7146_PSR_PABO) {
			IDEBUG(printk("stradis%d irq: PABO\n", saa->nr));
		}
		if (astat & SAA7146_PSR_PPED) {
			IDEBUG(printk("stradis%d irq: PPED\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_I1) {
			IDEBUG(printk("stradis%d irq: RPS_I1\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_I0) {
			IDEBUG(printk("stradis%d irq: RPS_I0\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_LATE1) {
			IDEBUG(printk("stradis%d irq: RPS_LATE1\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_LATE0) {
			IDEBUG(printk("stradis%d irq: RPS_LATE0\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_E1) {
			IDEBUG(printk("stradis%d irq: RPS_E1\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_E0) {
			IDEBUG(printk("stradis%d irq: RPS_E0\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_TO1) {
			IDEBUG(printk("stradis%d irq: RPS_TO1\n", saa->nr));
		}
		if (astat & SAA7146_PSR_RPS_TO0) {
			IDEBUG(printk("stradis%d irq: RPS_TO0\n", saa->nr));
		}
		if (astat & SAA7146_PSR_UPLD) {
			IDEBUG(printk("stradis%d irq: UPLD\n", saa->nr));
		}
		if (astat & SAA7146_PSR_DEBI_E) {
			IDEBUG(printk("stradis%d irq: DEBI_E\n", saa->nr));
		}
		if (astat & SAA7146_PSR_I2C_S) {
			IDEBUG(printk("stradis%d irq: I2C_S\n", saa->nr));
		}
		if (astat & SAA7146_PSR_I2C_E) {
			IDEBUG(printk("stradis%d irq: I2C_E\n", saa->nr));
		}
		if (astat & SAA7146_PSR_A2_IN) {
			IDEBUG(printk("stradis%d irq: A2_IN\n", saa->nr));
		}
		if (astat & SAA7146_PSR_A2_OUT) {
			IDEBUG(printk("stradis%d irq: A2_OUT\n", saa->nr));
		}
		if (astat & SAA7146_PSR_A1_IN) {
			IDEBUG(printk("stradis%d irq: A1_IN\n", saa->nr));
		}
		if (astat & SAA7146_PSR_A1_OUT) {
			IDEBUG(printk("stradis%d irq: A1_OUT\n", saa->nr));
		}
		if (astat & SAA7146_PSR_AFOU) {
			IDEBUG(printk("stradis%d irq: AFOU\n", saa->nr));
		}
		if (astat & SAA7146_PSR_V_PE) {
			IDEBUG(printk("stradis%d irq: V_PE\n", saa->nr));
		}
		if (astat & SAA7146_PSR_VFOU) {
			IDEBUG(printk("stradis%d irq: VFOU\n", saa->nr));
		}
		if (astat & SAA7146_PSR_FIDA) {
			IDEBUG(printk("stradis%d irq: FIDA\n", saa->nr));
		}
		if (astat & SAA7146_PSR_FIDB) {
			IDEBUG(printk("stradis%d irq: FIDB\n", saa->nr));
		}
		if (astat & SAA7146_PSR_PIN3) {
			IDEBUG(printk("stradis%d irq: PIN3\n", saa->nr));
		}
		if (astat & SAA7146_PSR_PIN2) {
			IDEBUG(printk("stradis%d irq: PIN2\n", saa->nr));
		}
		if (astat & SAA7146_PSR_PIN0) {
			IDEBUG(printk("stradis%d irq: PIN0\n", saa->nr));
		}
		if (astat & SAA7146_PSR_ECS) {
			IDEBUG(printk("stradis%d irq: ECS\n", saa->nr));
		}
		if (astat & SAA7146_PSR_EC3S) {
			IDEBUG(printk("stradis%d irq: EC3S\n", saa->nr));
		}
		if (astat & SAA7146_PSR_EC0S) {
			IDEBUG(printk("stradis%d irq: EC0S\n", saa->nr));
		}
#endif
		count++;
		if (count > 15)
			printk(KERN_WARNING "stradis%d: irq loop %d\n",
			       saa->nr, count);
		if (count > 20) {
			saawrite(0, SAA7146_IER);
			printk(KERN_ERR
			       "stradis%d: IRQ loop cleared\n", saa->nr);
		}
	}
	return IRQ_RETVAL(handled);
}

static int ibm_send_command(struct saa7146 *saa,
			    int command, int data, int chain)
{
	int i;

	if (chain)
		debiwrite(saa, debNormal, IBM_MP2_COMMAND, (command << 1)| 1,2);
	else
		debiwrite(saa, debNormal, IBM_MP2_COMMAND, command << 1, 2);
	debiwrite(saa, debNormal, IBM_MP2_CMD_DATA, data, 2);
	debiwrite(saa, debNormal, IBM_MP2_CMD_STAT, 1, 2);
	for (i = 0; i < 100 &&
	     (debiread(saa, debNormal, IBM_MP2_CMD_STAT, 2) & 1); i++)
		schedule();
	if (i == 100)
		return -1;
	return 0;
}

static void cs4341_setlevel(struct saa7146 *saa, int left, int right)
{
	I2CWrite(saa, 0x22, 0x03, left > 94 ? 94 : left, 2);
	I2CWrite(saa, 0x22, 0x04, right > 94 ? 94 : right, 2);
}

static void initialize_cs4341(struct saa7146 *saa)
{
	int i;
	for (i = 0; i < 200; i++) {
		/* auto mute off, power on, no de-emphasis */
		/* I2S data up to 24-bit 64xFs internal SCLK */
		I2CWrite(saa, 0x22, 0x01, 0x11, 2);
		/* ATAPI mixer settings */
		I2CWrite(saa, 0x22, 0x02, 0x49, 2);
		/* attenuation left 3db */
		I2CWrite(saa, 0x22, 0x03, 0x00, 2);
		/* attenuation right 3db */
		I2CWrite(saa, 0x22, 0x04, 0x00, 2);
		I2CWrite(saa, 0x22, 0x01, 0x10, 2);
		if (I2CRead(saa, 0x22, 0x02, 1) == 0x49)
			break;
		schedule();
	}
	printk("stradis%d: CS4341 initialized (%d)\n", saa->nr, i);
	return;
}

static void initialize_cs8420(struct saa7146 *saa, int pro)
{
	int i;
	u8 *sequence;
	if (pro)
		sequence = mode8420pro;
	else
		sequence = mode8420con;
	for (i = 0; i < INIT8420LEN; i++)
		I2CWrite(saa, 0x20, init8420[i * 2], init8420[i * 2 + 1], 2);
	for (i = 0; i < MODE8420LEN; i++)
		I2CWrite(saa, 0x20, sequence[i * 2], sequence[i * 2 + 1], 2);
	printk("stradis%d: CS8420 initialized\n", saa->nr);
}

static void initialize_saa7121(struct saa7146 *saa, int dopal)
{
	int i, mod;
	u8 *sequence;
	if (dopal)
		sequence = init7121pal;
	else
		sequence = init7121ntsc;
	mod = saaread(SAA7146_PSR) & 0x08;
	/* initialize PAL/NTSC video encoder */
	for (i = 0; i < INIT7121LEN; i++) {
		if (NewCard) {	/* handle new card encoder differences */
			if (sequence[i * 2] == 0x3a)
				I2CWrite(saa, 0x88, 0x3a, 0x13, 2);
			else if (sequence[i * 2] == 0x6b)
				I2CWrite(saa, 0x88, 0x6b, 0x20, 2);
			else if (sequence[i * 2] == 0x6c)
				I2CWrite(saa, 0x88, 0x6c,
					 dopal ? 0x09 : 0xf5, 2);
			else if (sequence[i * 2] == 0x6d)
				I2CWrite(saa, 0x88, 0x6d,
					 dopal ? 0x20 : 0x00, 2);
			else if (sequence[i * 2] == 0x7a)
				I2CWrite(saa, 0x88, 0x7a,
					 dopal ? (PALFirstActive - 1) :
					 (NTSCFirstActive - 4), 2);
			else if (sequence[i * 2] == 0x7b)
				I2CWrite(saa, 0x88, 0x7b,
					 dopal ? PALLastActive :
					 NTSCLastActive, 2);
			else
				I2CWrite(saa, 0x88, sequence[i * 2],
					 sequence[i * 2 + 1], 2);
		} else {
			if (sequence[i * 2] == 0x6b && mod)
				I2CWrite(saa, 0x88, 0x6b,
					 (sequence[i * 2 + 1] ^ 0x09), 2);
			else if (sequence[i * 2] == 0x7a)
				I2CWrite(saa, 0x88, 0x7a,
					 dopal ? (PALFirstActive - 1) :
					 (NTSCFirstActive - 4), 2);
			else if (sequence[i * 2] == 0x7b)
				I2CWrite(saa, 0x88, 0x7b,
					 dopal ? PALLastActive :
					 NTSCLastActive, 2);
			else
				I2CWrite(saa, 0x88, sequence[i * 2],
					 sequence[i * 2 + 1], 2);
		}
	}
}

static void set_genlock_offset(struct saa7146 *saa, int noffset)
{
	int nCode;
	int PixelsPerLine = 858;
	if (CurrentMode == VIDEO_MODE_PAL)
		PixelsPerLine = 864;
	if (noffset > 500)
		noffset = 500;
	else if (noffset < -500)
		noffset = -500;
	nCode = noffset + 0x100;
	if (nCode == 1)
		nCode = 0x401;
	else if (nCode < 1)
		nCode = 0x400 + PixelsPerLine + nCode;
	debiwrite(saa, debNormal, XILINX_GLDELAY, nCode, 2);
}

static void set_out_format(struct saa7146 *saa, int mode)
{
	initialize_saa7121(saa, (mode == VIDEO_MODE_NTSC ? 0 : 1));
	saa->boardcfg[2] = mode;
	/* do not adjust analog video parameters here, use saa7121 init */
	/* you will affect the SDI output on the new card */
	if (mode == VIDEO_MODE_PAL) {	/* PAL */
		debiwrite(saa, debNormal, XILINX_CTL0, 0x0808, 2);
		mdelay(50);
		saawrite(0x012002c0, SAA7146_NUM_LINE_BYTE1);
		if (NewCard) {
			debiwrite(saa, debNormal, IBM_MP2_DISP_MODE, 0xe100, 2);
			mdelay(50);
		}
		debiwrite(saa, debNormal, IBM_MP2_DISP_MODE,
			  NewCard ? 0xe500 : 0x6500, 2);
		debiwrite(saa, debNormal, IBM_MP2_DISP_DLY,
			  (1 << 8) |
			  (NewCard ? PALFirstActive : PALFirstActive - 6), 2);
	} else {		/* NTSC */
		debiwrite(saa, debNormal, XILINX_CTL0, 0x0800, 2);
		mdelay(50);
		saawrite(0x00f002c0, SAA7146_NUM_LINE_BYTE1);
		debiwrite(saa, debNormal, IBM_MP2_DISP_MODE,
			  NewCard ? 0xe100 : 0x6100, 2);
		debiwrite(saa, debNormal, IBM_MP2_DISP_DLY,
			  (1 << 8) |
			  (NewCard ? NTSCFirstActive : NTSCFirstActive - 6), 2);
	}
}

/* Intialize bitmangler to map from a byte value to the mangled word that
 * must be output to program the Xilinx part through the DEBI port.
 * Xilinx Data Bit->DEBI Bit: 0->15 1->7 2->6 3->12 4->11 5->2 6->1 7->0
 * transfer FPGA code, init IBM chip, transfer IBM microcode
 * rev2 card mangles: 0->7 1->6 2->5 3->4 4->3 5->2 6->1 7->0
 */
static u16 bitmangler[256];

static int initialize_fpga(struct video_code *bitdata)
{
	int i, num, startindex, failure = 0, loadtwo, loadfile = 0;
	u16 *dmabuf;
	u8 *newdma;
	struct saa7146 *saa;

	/* verify fpga code */
	for (startindex = 0; startindex < bitdata->datasize; startindex++)
		if (bitdata->data[startindex] == 255)
			break;
	if (startindex == bitdata->datasize) {
		printk(KERN_INFO "stradis: bad fpga code\n");
		return -1;
	}
	/* initialize all detected cards */
	for (num = 0; num < saa_num; num++) {
		saa = &saa7146s[num];
		if (saa->boardcfg[0] > 20)
			continue;	/* card was programmed */
		loadtwo = (saa->boardcfg[18] & 0x10);
		if (!NewCard)	/* we have an old board */
			for (i = 0; i < 256; i++)
				bitmangler[i] = ((i & 0x01) << 15) |
					((i & 0x02) << 6) | ((i & 0x04) << 4) |
					((i & 0x08) << 9) | ((i & 0x10) << 7) |
					((i & 0x20) >> 3) | ((i & 0x40) >> 5) |
					((i & 0x80) >> 7);
		else		/* else we have a new board */
			for (i = 0; i < 256; i++)
				bitmangler[i] = ((i & 0x01) << 7) |
					((i & 0x02) << 5) | ((i & 0x04) << 3) |
					((i & 0x08) << 1) | ((i & 0x10) >> 1) |
					((i & 0x20) >> 3) | ((i & 0x40) >> 5) |
					((i & 0x80) >> 7);

		dmabuf = (u16 *) saa->dmadebi;
		newdma = (u8 *) saa->dmadebi;
		if (NewCard) {	/* SDM2xxx */
			if (!strncmp(bitdata->loadwhat, "decoder2", 8))
				continue;	/* fpga not for this card */
			if (!strncmp(&saa->boardcfg[42], bitdata->loadwhat, 8))
				loadfile = 1;
			else if (loadtwo && !strncmp(&saa->boardcfg[19],
				       bitdata->loadwhat, 8))
				loadfile = 2;
			else if (!saa->boardcfg[42] && !strncmp("decxl",
					bitdata->loadwhat, 8))
				loadfile = 1;	/* special */
			else
				continue;	/* fpga not for this card */
			if (loadfile != 1 && loadfile != 2)
				continue;	/* skip to next card */
			if (saa->boardcfg[0] && loadfile == 1)
				continue;	/* skip to next card */
			if (saa->boardcfg[0] != 1 && loadfile == 2)
				continue;	/* skip to next card */
			saa->boardcfg[0]++;	/* mark fpga handled */
			printk("stradis%d: loading %s\n", saa->nr,
				bitdata->loadwhat);
			if (loadtwo && loadfile == 2)
				goto send_fpga_stuff;
			/* turn on the Audio interface to set PROG low */
			saawrite(0x00400040, SAA7146_GPIO_CTRL);
			saaread(SAA7146_PSR);	/* ensure posted write */
			/* wait for everyone to reset */
			mdelay(10);
			saawrite(0x00400000, SAA7146_GPIO_CTRL);
		} else {	/* original card */
			if (strncmp(bitdata->loadwhat, "decoder2", 8))
				continue;	/* fpga not for this card */
			/* Pull the Xilinx PROG signal WS3 low */
			saawrite(0x02000200, SAA7146_MC1);
			/* Turn on the Audio interface so can set PROG low */
			saawrite(0x000000c0, SAA7146_ACON1);
			/* Pull the Xilinx INIT signal (GPIO2) low */
			saawrite(0x00400000, SAA7146_GPIO_CTRL);
			/* Make sure everybody resets */
			saaread(SAA7146_PSR);	/* ensure posted write */
			mdelay(10);
			/* Release the Xilinx PROG signal */
			saawrite(0x00000000, SAA7146_ACON1);
			/* Turn off the Audio interface */
			saawrite(0x02000000, SAA7146_MC1);
		}
		/* Release Xilinx INIT signal (WS2) */
		saawrite(0x00000000, SAA7146_GPIO_CTRL);
		/* Wait for the INIT to go High */
		for (i = 0;
			i < 10000 && !(saaread(SAA7146_PSR) & SAA7146_PSR_PIN2);
			i++)
			schedule();
		if (i == 1000) {
			printk(KERN_INFO "stradis%d: no fpga INIT\n", saa->nr);
			return -1;
		}
send_fpga_stuff:
		if (NewCard) {
			for (i = startindex; i < bitdata->datasize; i++)
				newdma[i - startindex] =
				    bitmangler[bitdata->data[i]];
			debiwrite(saa, 0x01420000, 0, 0,
				((bitdata->datasize - startindex) + 5));
			if (loadtwo && loadfile == 1) {
				printk("stradis%d: awaiting 2nd FPGA bitfile\n",
				       saa->nr);
				continue;	/* skip to next card */
			}
		} else {
			for (i = startindex; i < bitdata->datasize; i++)
				dmabuf[i - startindex] =
					bitmangler[bitdata->data[i]];
			debiwrite(saa, 0x014a0000, 0, 0,
				((bitdata->datasize - startindex) + 5) * 2);
		}
		for (i = 0;
			i < 1000 && !(saaread(SAA7146_PSR) & SAA7146_PSR_PIN2);
			i++)
			schedule();
		if (i == 1000) {
			printk(KERN_INFO "stradis%d: FPGA load failed\n",
			       saa->nr);
			failure++;
			continue;
		}
		if (!NewCard) {
			/* Pull the Xilinx INIT signal (GPIO2) low */
			saawrite(0x00400000, SAA7146_GPIO_CTRL);
			saaread(SAA7146_PSR);	/* ensure posted write */
			mdelay(2);
			saawrite(0x00000000, SAA7146_GPIO_CTRL);
			mdelay(2);
		}
		printk(KERN_INFO "stradis%d: FPGA Loaded\n", saa->nr);
		saa->boardcfg[0] = 26;	/* mark fpga programmed */
		/* set VXCO to its lowest frequency */
		debiwrite(saa, debNormal, XILINX_PWM, 0, 2);
		if (NewCard) {
			/* mute CS3310 */
			if (HaveCS3310)
				debiwrite(saa, debNormal, XILINX_CS3310_CMPLT,
					0, 2);
			/* set VXCO to PWM mode, release reset, blank on */
			debiwrite(saa, debNormal, XILINX_CTL0, 0xffc4, 2);
			mdelay(10);
			/* unmute CS3310 */
			if (HaveCS3310)
				debiwrite(saa, debNormal, XILINX_CTL0,
					0x2020, 2);
		}
		/* set source Black */
		debiwrite(saa, debNormal, XILINX_CTL0, 0x1707, 2);
		saa->boardcfg[4] = 22;	/* set NTSC First Active Line */
		saa->boardcfg[5] = 23;	/* set PAL First Active Line */
		saa->boardcfg[54] = 2;	/* set NTSC Last Active Line - 256 */
		saa->boardcfg[55] = 54;	/* set PAL Last Active Line - 256 */
		set_out_format(saa, VIDEO_MODE_NTSC);
		mdelay(50);
		/* begin IBM chip init */
		debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL, 4, 2);
		saaread(SAA7146_PSR);	/* wait for reset */
		mdelay(5);
		debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL, 0, 2);
		debiread(saa, debNormal, IBM_MP2_CHIP_CONTROL, 2);
		debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL, 0x10, 2);
		debiwrite(saa, debNormal, IBM_MP2_CMD_ADDR, 0, 2);
		debiwrite(saa, debNormal, IBM_MP2_CHIP_MODE, 0x2e, 2);
		if (NewCard) {
			mdelay(5);
			/* set i2s rate converter to 48KHz */
			debiwrite(saa, debNormal, 0x80c0, 6, 2);
			/* we must init CS8420 first since rev b pulls i2s */
			/* master clock low and CS4341 needs i2s master to */
			/* run the i2c port. */
			if (HaveCS8420)
				/* 0=consumer, 1=pro */
				initialize_cs8420(saa, 0);

			mdelay(5);
			if (HaveCS4341)
				initialize_cs4341(saa);
		}
		debiwrite(saa, debNormal, IBM_MP2_INFC_CTL, 0x48, 2);
		debiwrite(saa, debNormal, IBM_MP2_BEEP_CTL, 0xa000, 2);
		debiwrite(saa, debNormal, IBM_MP2_DISP_LBOR, 0, 2);
		debiwrite(saa, debNormal, IBM_MP2_DISP_TBOR, 0, 2);
		if (NewCard)
			set_genlock_offset(saa, 0);
		debiwrite(saa, debNormal, IBM_MP2_FRNT_ATTEN, 0, 2);
#if 0
		/* enable genlock */
		debiwrite(saa, debNormal, XILINX_CTL0, 0x8000, 2);
#else
		/* disable genlock */
		debiwrite(saa, debNormal, XILINX_CTL0, 0x8080, 2);
#endif
	}

	return failure;
}

static int do_ibm_reset(struct saa7146 *saa)
{
	/* failure if decoder not previously programmed */
	if (saa->boardcfg[0] < 37)
		return -EIO;
	/* mute CS3310 */
	if (HaveCS3310)
		debiwrite(saa, debNormal, XILINX_CS3310_CMPLT, 0, 2);
	/* disable interrupts */
	saawrite(0, SAA7146_IER);
	saa->audhead = saa->audtail = 0;
	saa->vidhead = saa->vidtail = 0;
	/* tristate debi bus, disable debi transfers */
	saawrite(0x00880000, SAA7146_MC1);
	/* ensure posted write */
	saaread(SAA7146_MC1);
	mdelay(50);
	/* re-enable debi transfers */
	saawrite(0x00880088, SAA7146_MC1);
	/* set source Black */
	debiwrite(saa, debNormal, XILINX_CTL0, 0x1707, 2);
	/* begin IBM chip init */
	set_out_format(saa, CurrentMode);
	debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL, 4, 2);
	saaread(SAA7146_PSR);	/* wait for reset */
	mdelay(5);
	debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL, 0, 2);
	debiread(saa, debNormal, IBM_MP2_CHIP_CONTROL, 2);
	debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL, ChipControl, 2);
	debiwrite(saa, debNormal, IBM_MP2_CHIP_MODE, 0x2e, 2);
	if (NewCard) {
		mdelay(5);
		/* set i2s rate converter to 48KHz */
		debiwrite(saa, debNormal, 0x80c0, 6, 2);
		/* we must init CS8420 first since rev b pulls i2s */
		/* master clock low and CS4341 needs i2s master to */
		/* run the i2c port. */
		if (HaveCS8420)
			/* 0=consumer, 1=pro */
			initialize_cs8420(saa, 1);

		mdelay(5);
		if (HaveCS4341)
			initialize_cs4341(saa);
	}
	debiwrite(saa, debNormal, IBM_MP2_INFC_CTL, 0x48, 2);
	debiwrite(saa, debNormal, IBM_MP2_BEEP_CTL, 0xa000, 2);
	debiwrite(saa, debNormal, IBM_MP2_DISP_LBOR, 0, 2);
	debiwrite(saa, debNormal, IBM_MP2_DISP_TBOR, 0, 2);
	if (NewCard)
		set_genlock_offset(saa, 0);
	debiwrite(saa, debNormal, IBM_MP2_FRNT_ATTEN, 0, 2);
	debiwrite(saa, debNormal, IBM_MP2_OSD_SIZE, 0x2000, 2);
	debiwrite(saa, debNormal, IBM_MP2_AUD_CTL, 0x4552, 2);
	if (ibm_send_command(saa, IBM_MP2_CONFIG_DECODER,
			(ChipControl == 0x43 ? 0xe800 : 0xe000), 1)) {
		printk(KERN_ERR "stradis%d: IBM config failed\n", saa->nr);
	}
	if (HaveCS3310) {
		int i = CS3310MaxLvl;
		debiwrite(saa, debNormal, XILINX_CS3310_CMPLT, ((i << 8)| i),2);
	}
	/* start video decoder */
	debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL, ChipControl, 2);
	/* 256k vid, 3520 bytes aud */
	debiwrite(saa, debNormal, IBM_MP2_RB_THRESHOLD, 0x4037, 2);
	debiwrite(saa, debNormal, IBM_MP2_AUD_CTL, 0x4573, 2);
	ibm_send_command(saa, IBM_MP2_PLAY, 0, 0);
	/* enable buffer threshold irq */
	debiwrite(saa, debNormal, IBM_MP2_MASK0, 0xc00c, 2);
	/* clear pending interrupts */
	debiread(saa, debNormal, IBM_MP2_HOST_INT, 2);
	debiwrite(saa, debNormal, XILINX_CTL0, 0x1711, 2);

	return 0;
}

/* load the decoder microcode */
static int initialize_ibmmpeg2(struct video_code *microcode)
{
	int i, num;
	struct saa7146 *saa;

	for (num = 0; num < saa_num; num++) {
		saa = &saa7146s[num];
		/* check that FPGA is loaded */
		debiwrite(saa, debNormal, IBM_MP2_OSD_SIZE, 0xa55a, 2);
		i = debiread(saa, debNormal, IBM_MP2_OSD_SIZE, 2);
		if (i != 0xa55a) {
			printk(KERN_INFO "stradis%d: %04x != 0xa55a\n",
				saa->nr, i);
#if 0
			return -1;
#endif
		}
		if (!strncmp(microcode->loadwhat, "decoder.vid", 11)) {
			if (saa->boardcfg[0] > 27)
				continue;	/* skip to next card */
			/* load video control store */
			saa->boardcfg[1] = 0x13;	/* no-sync default */
			debiwrite(saa, debNormal, IBM_MP2_WR_PROT, 1, 2);
			debiwrite(saa, debNormal, IBM_MP2_PROC_IADDR, 0, 2);
			for (i = 0; i < microcode->datasize / 2; i++)
				debiwrite(saa, debNormal, IBM_MP2_PROC_IDATA,
					(microcode->data[i * 2] << 8) |
					microcode->data[i * 2 + 1], 2);
			debiwrite(saa, debNormal, IBM_MP2_PROC_IADDR, 0, 2);
			debiwrite(saa, debNormal, IBM_MP2_WR_PROT, 0, 2);
			debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL,
				ChipControl, 2);
			saa->boardcfg[0] = 28;
		}
		if (!strncmp(microcode->loadwhat, "decoder.aud", 11)) {
			if (saa->boardcfg[0] > 35)
				continue;	/* skip to next card */
			/* load audio control store */
			debiwrite(saa, debNormal, IBM_MP2_WR_PROT, 1, 2);
			debiwrite(saa, debNormal, IBM_MP2_AUD_IADDR, 0, 2);
			for (i = 0; i < microcode->datasize; i++)
				debiwrite(saa, debNormal, IBM_MP2_AUD_IDATA,
					microcode->data[i], 1);
			debiwrite(saa, debNormal, IBM_MP2_AUD_IADDR, 0, 2);
			debiwrite(saa, debNormal, IBM_MP2_WR_PROT, 0, 2);
			debiwrite(saa, debNormal, IBM_MP2_OSD_SIZE, 0x2000, 2);
			debiwrite(saa, debNormal, IBM_MP2_AUD_CTL, 0x4552, 2);
			if (ibm_send_command(saa, IBM_MP2_CONFIG_DECODER,
					0xe000, 1)) {
				printk(KERN_ERR "stradis%d: IBM config "
					"failed\n", saa->nr);
				return -1;
			}
			/* set PWM to center value */
			if (NewCard) {
				debiwrite(saa, debNormal, XILINX_PWM,
					saa->boardcfg[14] +
					(saa->boardcfg[13] << 8), 2);
			} else
				debiwrite(saa, debNormal, XILINX_PWM, 0x46, 2);

			if (HaveCS3310) {
				i = CS3310MaxLvl;
				debiwrite(saa, debNormal, XILINX_CS3310_CMPLT,
					(i << 8) | i, 2);
			}
			printk(KERN_INFO "stradis%d: IBM MPEGCD%d Inited\n",
				saa->nr, 18 + (debiread(saa, debNormal,
				IBM_MP2_CHIP_CONTROL, 2) >> 12));
			/* start video decoder */
			debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL,
				ChipControl, 2);
			debiwrite(saa, debNormal, IBM_MP2_RB_THRESHOLD, 0x4037,
				2);	/* 256k vid, 3520 bytes aud */
			debiwrite(saa, debNormal, IBM_MP2_AUD_CTL, 0x4573, 2);
			ibm_send_command(saa, IBM_MP2_PLAY, 0, 0);
			/* enable buffer threshold irq */
			debiwrite(saa, debNormal, IBM_MP2_MASK0, 0xc00c, 2);
			debiread(saa, debNormal, IBM_MP2_HOST_INT, 2);
			/* enable gpio irq */
			saawrite(0x00002000, SAA7146_GPIO_CTRL);
			/* enable decoder output to HPS */
			debiwrite(saa, debNormal, XILINX_CTL0, 0x1711, 2);
			saa->boardcfg[0] = 37;
		}
	}

	return 0;
}

static u32 palette2fmt[] = {	/* some of these YUV translations are wrong */
	0xffffffff, 0x86000000, 0x87000000, 0x80000000, 0x8100000, 0x82000000,
	0x83000000, 0x00000000, 0x03000000, 0x03000000, 0x0a00000, 0x03000000,
	0x06000000, 0x00000000, 0x03000000, 0x0a000000, 0x0300000
};
static int bpp2fmt[4] = {
	VIDEO_PALETTE_HI240, VIDEO_PALETTE_RGB565, VIDEO_PALETTE_RGB24,
	VIDEO_PALETTE_RGB32
};

/* I wish I could find a formula to calculate these... */
static u32 h_prescale[64] = {
	0x10000000, 0x18040202, 0x18080000, 0x380c0606, 0x38100204, 0x38140808,
	0x38180000, 0x381c0000, 0x3820161c, 0x38242a3b, 0x38281230, 0x382c4460,
	0x38301040, 0x38340080, 0x38380000, 0x383c0000, 0x3840fefe, 0x3844ee9f,
	0x3848ee9f, 0x384cee9f, 0x3850ee9f, 0x38542a3b, 0x38581230, 0x385c0000,
	0x38600000, 0x38640000, 0x38680000, 0x386c0000, 0x38700000, 0x38740000,
	0x38780000, 0x387c0000, 0x30800000, 0x38840000, 0x38880000, 0x388c0000,
	0x38900000, 0x38940000, 0x38980000, 0x389c0000, 0x38a00000, 0x38a40000,
	0x38a80000, 0x38ac0000, 0x38b00000, 0x38b40000, 0x38b80000, 0x38bc0000,
	0x38c00000, 0x38c40000, 0x38c80000, 0x38cc0000, 0x38d00000, 0x38d40000,
	0x38d80000, 0x38dc0000, 0x38e00000, 0x38e40000, 0x38e80000, 0x38ec0000,
	0x38f00000, 0x38f40000, 0x38f80000, 0x38fc0000,
};
static u32 v_gain[64] = {
	0x016000ff, 0x016100ff, 0x016100ff, 0x016200ff, 0x016200ff, 0x016200ff,
	0x016200ff, 0x016300ff, 0x016300ff, 0x016300ff, 0x016300ff, 0x016300ff,
	0x016300ff, 0x016300ff, 0x016300ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
	0x016400ff, 0x016400ff, 0x016400ff, 0x016400ff,
};

static void saa7146_set_winsize(struct saa7146 *saa)
{
	u32 format;
	int offset, yacl, ysci;
	saa->win.color_fmt = format =
	    (saa->win.depth == 15) ? palette2fmt[VIDEO_PALETTE_RGB555] :
	    palette2fmt[bpp2fmt[(saa->win.bpp - 1) & 3]];
	offset = saa->win.x * saa->win.bpp + saa->win.y * saa->win.bpl;
	saawrite(saa->win.vidadr + offset, SAA7146_BASE_EVEN1);
	saawrite(saa->win.vidadr + offset + saa->win.bpl, SAA7146_BASE_ODD1);
	saawrite(saa->win.bpl * 2, SAA7146_PITCH1);
	saawrite(saa->win.vidadr + saa->win.bpl * saa->win.sheight,
		 SAA7146_PROT_ADDR1);
	saawrite(0, SAA7146_PAGE1);
	saawrite(format | 0x60, SAA7146_CLIP_FORMAT_CTRL);
	offset = (704 / (saa->win.width - 1)) & 0x3f;
	saawrite(h_prescale[offset], SAA7146_HPS_H_PRESCALE);
	offset = (720896 / saa->win.width) / (offset + 1);
	saawrite((offset << 12) | 0x0c, SAA7146_HPS_H_SCALE);
	if (CurrentMode == VIDEO_MODE_NTSC) {
		yacl = /*(480 / saa->win.height - 1) & 0x3f */ 0;
		ysci = 1024 - (saa->win.height * 1024 / 480);
	} else {
		yacl = /*(576 / saa->win.height - 1) & 0x3f */ 0;
		ysci = 1024 - (saa->win.height * 1024 / 576);
	}
	saawrite((1 << 31) | (ysci << 21) | (yacl << 15), SAA7146_HPS_V_SCALE);
	saawrite(v_gain[yacl], SAA7146_HPS_V_GAIN);
	saawrite(((SAA7146_MC2_UPLD_DMA1 | SAA7146_MC2_UPLD_HPS_V |
		SAA7146_MC2_UPLD_HPS_H) << 16) | (SAA7146_MC2_UPLD_DMA1 |
		SAA7146_MC2_UPLD_HPS_V | SAA7146_MC2_UPLD_HPS_H), SAA7146_MC2);
}

/* clip_draw_rectangle(cm,x,y,w,h) -- handle clipping an area
 * bitmap is fixed width, 128 bytes (1024 pixels represented)
 * arranged most-sigificant-bit-left in 32-bit words
 * based on saa7146 clipping hardware, it swaps bytes if LE
 * much of this makes up for egcs brain damage -- so if you
 * are wondering "why did he do this?" it is because the C
 * was adjusted to generate the optimal asm output without
 * writing non-portable __asm__ directives.
 */

static void clip_draw_rectangle(u32 *clipmap, int x, int y, int w, int h)
{
	register int startword, endword;
	register u32 bitsleft, bitsright;
	u32 *temp;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (w <= 0 || h <= 0 || x > 1023 || y > 639)
		return;		/* throw away bad clips */
	if (x + w > 1024)
		w = 1024 - x;
	if (y + h > 640)
		h = 640 - y;
	startword = (x >> 5);
	endword = ((x + w) >> 5);
	bitsleft = (0xffffffff >> (x & 31));
	bitsright = (0xffffffff << (~((x + w) - (endword << 5))));
	temp = &clipmap[(y << 5) + startword];
	w = endword - startword;
	if (!w) {
		bitsleft |= bitsright;
		for (y = 0; y < h; y++) {
			*temp |= bitsleft;
			temp += 32;
		}
	} else {
		for (y = 0; y < h; y++) {
			*temp++ |= bitsleft;
			for (x = 1; x < w; x++)
				*temp++ = 0xffffffff;
			*temp |= bitsright;
			temp += (32 - w);
		}
	}
}

static void make_clip_tab(struct saa7146 *saa, struct video_clip *cr, int ncr)
{
	int i, width, height;
	u32 *clipmap;

	clipmap = saa->dmavid2;
	if ((width = saa->win.width) > 1023)
		width = 1023;	/* sanity check */
	if ((height = saa->win.height) > 640)
		height = 639;	/* sanity check */
	if (ncr > 0) {		/* rectangles pased */
		/* convert rectangular clips to a bitmap */
		memset(clipmap, 0, VIDEO_CLIPMAP_SIZE);	/* clear map */
		for (i = 0; i < ncr; i++)
			clip_draw_rectangle(clipmap, cr[i].x, cr[i].y,
				cr[i].width, cr[i].height);
	}
	/* clip against viewing window AND screen
	   so we do not have to rely on the user program
	 */
	clip_draw_rectangle(clipmap, (saa->win.x + width > saa->win.swidth) ?
		(saa->win.swidth - saa->win.x) : width, 0, 1024, 768);
	clip_draw_rectangle(clipmap, 0,
		(saa->win.y + height > saa->win.sheight) ?
		(saa->win.sheight - saa->win.y) : height, 1024, 768);
	if (saa->win.x < 0)
		clip_draw_rectangle(clipmap, 0, 0, -saa->win.x, 768);
	if (saa->win.y < 0)
		clip_draw_rectangle(clipmap, 0, 0, 1024, -saa->win.y);
}

static int saa_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long argl)
{
	struct saa7146 *saa = file->private_data;
	void __user *arg = (void __user *)argl;

	switch (cmd) {
	case VIDIOCGCAP:
		{
			struct video_capability b;
			strcpy(b.name, saa->video_dev.name);
			b.type = VID_TYPE_CAPTURE | VID_TYPE_OVERLAY |
				VID_TYPE_CLIPPING | VID_TYPE_FRAMERAM |
				VID_TYPE_SCALES;
			b.channels = 1;
			b.audios = 1;
			b.maxwidth = 768;
			b.maxheight = 576;
			b.minwidth = 32;
			b.minheight = 32;
			if (copy_to_user(arg, &b, sizeof(b)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCGPICT:
		{
			struct video_picture p = saa->picture;
			if (saa->win.depth == 8)
				p.palette = VIDEO_PALETTE_HI240;
			if (saa->win.depth == 15)
				p.palette = VIDEO_PALETTE_RGB555;
			if (saa->win.depth == 16)
				p.palette = VIDEO_PALETTE_RGB565;
			if (saa->win.depth == 24)
				p.palette = VIDEO_PALETTE_RGB24;
			if (saa->win.depth == 32)
				p.palette = VIDEO_PALETTE_RGB32;
			if (copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCSPICT:
		{
			struct video_picture p;
			u32 format;
			if (copy_from_user(&p, arg, sizeof(p)))
				return -EFAULT;
			if (p.palette < ARRAY_SIZE(palette2fmt)) {
				format = palette2fmt[p.palette];
				saa->win.color_fmt = format;
				saawrite(format | 0x60,
					SAA7146_CLIP_FORMAT_CTRL);
			}
			saawrite(((p.brightness & 0xff00) << 16) |
				((p.contrast & 0xfe00) << 7) |
				((p.colour & 0xfe00) >> 9), SAA7146_BCS_CTRL);
			saa->picture = p;
			/* upload changed registers */
			saawrite(((SAA7146_MC2_UPLD_HPS_H |
				SAA7146_MC2_UPLD_HPS_V) << 16) |
				SAA7146_MC2_UPLD_HPS_H |
				SAA7146_MC2_UPLD_HPS_V, SAA7146_MC2);
			return 0;
		}
	case VIDIOCSWIN:
		{
			struct video_window vw;
			struct video_clip *vcp = NULL;

			if (copy_from_user(&vw, arg, sizeof(vw)))
				return -EFAULT;

			/* stop capture */
			if (vw.flags || vw.width < 16 || vw.height < 16) {
				saawrite((SAA7146_MC1_TR_E_1 << 16),
					SAA7146_MC1);
				return -EINVAL;
			}
			/* 32-bit align start and adjust width */
			if (saa->win.bpp < 4) {
				int i = vw.x;
				vw.x = (vw.x + 3) & ~3;
				i = vw.x - i;
				vw.width -= i;
			}
			saa->win.x = vw.x;
			saa->win.y = vw.y;
			saa->win.width = vw.width;
			if (saa->win.width > 768)
				saa->win.width = 768;
			saa->win.height = vw.height;
			if (CurrentMode == VIDEO_MODE_NTSC) {
				if (saa->win.height > 480)
					saa->win.height = 480;
			} else {
				if (saa->win.height > 576)
					saa->win.height = 576;
			}

			/* stop capture */
			saawrite((SAA7146_MC1_TR_E_1 << 16), SAA7146_MC1);
			saa7146_set_winsize(saa);

			/*
			 *    Do any clips.
			 */
			if (vw.clipcount < 0) {
				if (copy_from_user(saa->dmavid2, vw.clips,
						VIDEO_CLIPMAP_SIZE))
					return -EFAULT;
			} else if (vw.clipcount > 16384) {
				return -EINVAL;
			} else if (vw.clipcount > 0) {
				vcp = vmalloc(sizeof(struct video_clip) *
					vw.clipcount);
				if (vcp == NULL)
					return -ENOMEM;
				if (copy_from_user(vcp, vw.clips,
						sizeof(struct video_clip) *
						vw.clipcount)) {
					vfree(vcp);
					return -EFAULT;
				}
			} else	/* nothing clipped */
				memset(saa->dmavid2, 0, VIDEO_CLIPMAP_SIZE);

			make_clip_tab(saa, vcp, vw.clipcount);
			if (vw.clipcount > 0)
				vfree(vcp);

			/* start capture & clip dma if we have an address */
			if ((saa->cap & 3) && saa->win.vidadr != 0)
				saawrite(((SAA7146_MC1_TR_E_1 |
					SAA7146_MC1_TR_E_2) << 16) | 0xffff,
					SAA7146_MC1);
			return 0;
		}
	case VIDIOCGWIN:
		{
			struct video_window vw;
			vw.x = saa->win.x;
			vw.y = saa->win.y;
			vw.width = saa->win.width;
			vw.height = saa->win.height;
			vw.chromakey = 0;
			vw.flags = 0;
			if (copy_to_user(arg, &vw, sizeof(vw)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCCAPTURE:
		{
			int v;
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if (v == 0) {
				saa->cap &= ~1;
				saawrite((SAA7146_MC1_TR_E_1 << 16),
					SAA7146_MC1);
			} else {
				if (saa->win.vidadr == 0 || saa->win.width == 0
						|| saa->win.height == 0)
					return -EINVAL;
				saa->cap |= 1;
				saawrite((SAA7146_MC1_TR_E_1 << 16) | 0xffff,
					SAA7146_MC1);
			}
			return 0;
		}
	case VIDIOCGFBUF:
		{
			struct video_buffer v;
			v.base = (void *)saa->win.vidadr;
			v.height = saa->win.sheight;
			v.width = saa->win.swidth;
			v.depth = saa->win.depth;
			v.bytesperline = saa->win.bpl;
			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			return 0;

		}
	case VIDIOCSFBUF:
		{
			struct video_buffer v;
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if (v.depth != 8 && v.depth != 15 && v.depth != 16 &&
			    v.depth != 24 && v.depth != 32 && v.width > 16 &&
			    v.height > 16 && v.bytesperline > 16)
				return -EINVAL;
			if (v.base)
				saa->win.vidadr = (unsigned long)v.base;
			saa->win.sheight = v.height;
			saa->win.swidth = v.width;
			saa->win.bpp = ((v.depth + 7) & 0x38) / 8;
			saa->win.depth = v.depth;
			saa->win.bpl = v.bytesperline;

			DEBUG(printk("Display at %p is %d by %d, bytedepth %d, "
					"bpl %d\n", v.base, v.width, v.height,
					saa->win.bpp, saa->win.bpl));
			saa7146_set_winsize(saa);
			return 0;
		}
	case VIDIOCKEY:
		{
			/* Will be handled higher up .. */
			return 0;
		}

	case VIDIOCGAUDIO:
		{
			struct video_audio v;
			v = saa->audio_dev;
			v.flags &= ~(VIDEO_AUDIO_MUTE | VIDEO_AUDIO_MUTABLE);
			v.flags |= VIDEO_AUDIO_MUTABLE | VIDEO_AUDIO_VOLUME;
			strcpy(v.name, "MPEG");
			v.mode = VIDEO_SOUND_STEREO;
			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCSAUDIO:
		{
			struct video_audio v;
			int i;
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			i = (~(v.volume >> 8)) & 0xff;
			if (!HaveCS4341) {
				if (v.flags & VIDEO_AUDIO_MUTE)
					debiwrite(saa, debNormal,
						IBM_MP2_FRNT_ATTEN, 0xffff, 2);
				if (!(v.flags & VIDEO_AUDIO_MUTE))
					debiwrite(saa, debNormal,
						IBM_MP2_FRNT_ATTEN, 0x0000, 2);
				if (v.flags & VIDEO_AUDIO_VOLUME)
					debiwrite(saa, debNormal,
						IBM_MP2_FRNT_ATTEN,
						(i << 8) | i, 2);
			} else {
				if (v.flags & VIDEO_AUDIO_MUTE)
					cs4341_setlevel(saa, 0xff, 0xff);
				if (!(v.flags & VIDEO_AUDIO_MUTE))
					cs4341_setlevel(saa, 0, 0);
				if (v.flags & VIDEO_AUDIO_VOLUME)
					cs4341_setlevel(saa, i, i);
			}
			saa->audio_dev = v;
			return 0;
		}

	case VIDIOCGUNIT:
		{
			struct video_unit vu;
			vu.video = saa->video_dev.minor;
			vu.vbi = VIDEO_NO_UNIT;
			vu.radio = VIDEO_NO_UNIT;
			vu.audio = VIDEO_NO_UNIT;
			vu.teletext = VIDEO_NO_UNIT;
			if (copy_to_user(arg, &vu, sizeof(vu)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCSPLAYMODE:
		{
			struct video_play_mode pmode;
			if (copy_from_user((void *)&pmode, arg,
					sizeof(struct video_play_mode)))
				return -EFAULT;
			switch (pmode.mode) {
			case VID_PLAY_VID_OUT_MODE:
				if (pmode.p1 != VIDEO_MODE_NTSC &&
						pmode.p1 != VIDEO_MODE_PAL)
					return -EINVAL;
				set_out_format(saa, pmode.p1);
				return 0;
			case VID_PLAY_GENLOCK:
				debiwrite(saa, debNormal, XILINX_CTL0,
					pmode.p1 ? 0x8000 : 0x8080, 2);
				if (NewCard)
					set_genlock_offset(saa, pmode.p2);
				return 0;
			case VID_PLAY_NORMAL:
				debiwrite(saa, debNormal,
					IBM_MP2_CHIP_CONTROL, ChipControl, 2);
				ibm_send_command(saa, IBM_MP2_PLAY, 0, 0);
				saa->playmode = pmode.mode;
				return 0;
			case VID_PLAY_PAUSE:
				/* IBM removed the PAUSE command */
				/* they say use SINGLE_FRAME now */
			case VID_PLAY_SINGLE_FRAME:
				ibm_send_command(saa, IBM_MP2_SINGLE_FRAME,0,0);
				if (saa->playmode == pmode.mode) {
					debiwrite(saa, debNormal,
						IBM_MP2_CHIP_CONTROL,
						ChipControl, 2);
				}
				saa->playmode = pmode.mode;
				return 0;
			case VID_PLAY_FAST_FORWARD:
				ibm_send_command(saa, IBM_MP2_FAST_FORWARD,0,0);
				saa->playmode = pmode.mode;
				return 0;
			case VID_PLAY_SLOW_MOTION:
				ibm_send_command(saa, IBM_MP2_SLOW_MOTION,
					pmode.p1, 0);
				saa->playmode = pmode.mode;
				return 0;
			case VID_PLAY_IMMEDIATE_NORMAL:
				/* ensure transfers resume */
				debiwrite(saa, debNormal,
					IBM_MP2_CHIP_CONTROL, ChipControl, 2);
				ibm_send_command(saa, IBM_MP2_IMED_NORM_PLAY,
					0, 0);
				saa->playmode = VID_PLAY_NORMAL;
				return 0;
			case VID_PLAY_SWITCH_CHANNELS:
				saa->audhead = saa->audtail = 0;
				saa->vidhead = saa->vidtail = 0;
				ibm_send_command(saa, IBM_MP2_FREEZE_FRAME,0,1);
				ibm_send_command(saa, IBM_MP2_RESET_AUD_RATE,
					0, 1);
				debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL,
					0, 2);
				ibm_send_command(saa, IBM_MP2_CHANNEL_SWITCH,
					0, 1);
				debiwrite(saa, debNormal, IBM_MP2_CHIP_CONTROL,
					ChipControl, 2);
				ibm_send_command(saa, IBM_MP2_PLAY, 0, 0);
				saa->playmode = VID_PLAY_NORMAL;
				return 0;
			case VID_PLAY_FREEZE_FRAME:
				ibm_send_command(saa, IBM_MP2_FREEZE_FRAME,0,0);
				saa->playmode = pmode.mode;
				return 0;
			case VID_PLAY_STILL_MODE:
				ibm_send_command(saa, IBM_MP2_SET_STILL_MODE,
					0, 0);
				saa->playmode = pmode.mode;
				return 0;
			case VID_PLAY_MASTER_MODE:
				if (pmode.p1 == VID_PLAY_MASTER_NONE)
					saa->boardcfg[1] = 0x13;
				else if (pmode.p1 == VID_PLAY_MASTER_VIDEO)
					saa->boardcfg[1] = 0x23;
				else if (pmode.p1 == VID_PLAY_MASTER_AUDIO)
					saa->boardcfg[1] = 0x43;
				else
					return -EINVAL;
				debiwrite(saa, debNormal,
					  IBM_MP2_CHIP_CONTROL, ChipControl, 2);
				return 0;
			case VID_PLAY_ACTIVE_SCANLINES:
				if (CurrentMode == VIDEO_MODE_PAL) {
					if (pmode.p1 < 1 || pmode.p2 > 625)
						return -EINVAL;
					saa->boardcfg[5] = pmode.p1;
					saa->boardcfg[55] = (pmode.p1 +
						(pmode.p2 / 2) - 1) & 0xff;
				} else {
					if (pmode.p1 < 4 || pmode.p2 > 525)
						return -EINVAL;
					saa->boardcfg[4] = pmode.p1;
					saa->boardcfg[54] = (pmode.p1 +
						(pmode.p2 / 2) - 4) & 0xff;
				}
				set_out_format(saa, CurrentMode);
			case VID_PLAY_RESET:
				return do_ibm_reset(saa);
			case VID_PLAY_END_MARK:
				if (saa->endmarktail < saa->endmarkhead) {
					if (saa->endmarkhead -
							saa->endmarktail < 2)
						return -ENOSPC;
				} else if (saa->endmarkhead <=saa->endmarktail){
					if (saa->endmarktail - saa->endmarkhead
							> (MAX_MARKS - 2))
						return -ENOSPC;
				} else
					return -ENOSPC;
				saa->endmark[saa->endmarktail] = saa->audtail;
				saa->endmarktail++;
				if (saa->endmarktail >= MAX_MARKS)
					saa->endmarktail = 0;
			}
			return -EINVAL;
		}
	case VIDIOCSWRITEMODE:
		{
			int mode;
			if (copy_from_user((void *)&mode, arg, sizeof(int)))
				return -EFAULT;
			if (mode == VID_WRITE_MPEG_AUD ||
					mode == VID_WRITE_MPEG_VID ||
					mode == VID_WRITE_CC ||
					mode == VID_WRITE_TTX ||
					mode == VID_WRITE_OSD) {
				saa->writemode = mode;
				return 0;
			}
			return -EINVAL;
		}
	case VIDIOCSMICROCODE:
		{
			struct video_code ucode;
			__u8 *udata;
			int i;
			if (copy_from_user(&ucode, arg, sizeof(ucode)))
				return -EFAULT;
			if (ucode.datasize > 65536 || ucode.datasize < 1024 ||
					strncmp(ucode.loadwhat, "dec", 3))
				return -EINVAL;
			if ((udata = vmalloc(ucode.datasize)) == NULL)
				return -ENOMEM;
			if (copy_from_user(udata, ucode.data, ucode.datasize)) {
				vfree(udata);
				return -EFAULT;
			}
			ucode.data = udata;
			if (!strncmp(ucode.loadwhat, "decoder.aud", 11) ||
				!strncmp(ucode.loadwhat, "decoder.vid", 11))
				i = initialize_ibmmpeg2(&ucode);
			else
				i = initialize_fpga(&ucode);
			vfree(udata);
			if (i)
				return -EINVAL;
			return 0;

		}
	case VIDIOCGCHAN:	/* this makes xawtv happy */
		{
			struct video_channel v;
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			v.flags = VIDEO_VC_AUDIO;
			v.tuners = 0;
			v.type = VID_TYPE_MPEG_DECODER;
			v.norm = CurrentMode;
			strcpy(v.name, "MPEG2");
			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCSCHAN:	/* this makes xawtv happy */
		{
			struct video_channel v;
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			/* do nothing */
			return 0;
		}
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int saa_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct saa7146 *saa = file->private_data;
	printk(KERN_DEBUG "stradis%d: saa_mmap called\n", saa->nr);
	return -EINVAL;
}

static ssize_t saa_read(struct file *file, char __user * buf,
	size_t count, loff_t * ppos)
{
	return -EINVAL;
}

static ssize_t saa_write(struct file *file, const char __user * buf,
	size_t count, loff_t * ppos)
{
	struct saa7146 *saa = file->private_data;
	unsigned long todo = count;
	int blocksize, split;
	unsigned long flags;

	while (todo > 0) {
		if (saa->writemode == VID_WRITE_MPEG_AUD) {
			spin_lock_irqsave(&saa->lock, flags);
			if (saa->audhead <= saa->audtail)
				blocksize = 65536 -
					(saa->audtail - saa->audhead);
			else
				blocksize = saa->audhead - saa->audtail;
			spin_unlock_irqrestore(&saa->lock, flags);
			if (blocksize < 16384) {
				saawrite(SAA7146_PSR_DEBI_S |
					SAA7146_PSR_PIN1, SAA7146_IER);
				saawrite(SAA7146_PSR_PIN1, SAA7146_PSR);
				/* wait for buffer space to open */
				interruptible_sleep_on(&saa->audq);
			}
			spin_lock_irqsave(&saa->lock, flags);
			if (saa->audhead <= saa->audtail) {
				blocksize = 65536 -
					(saa->audtail - saa->audhead);
				split = 65536 - saa->audtail;
			} else {
				blocksize = saa->audhead - saa->audtail;
				split = 65536;
			}
			spin_unlock_irqrestore(&saa->lock, flags);
			blocksize--;
			if (blocksize > todo)
				blocksize = todo;
			/* double check that we really have space */
			if (!blocksize)
				return -ENOSPC;
			if (split < blocksize) {
				if (copy_from_user(saa->audbuf +
						saa->audtail, buf, split))
					return -EFAULT;
				buf += split;
				todo -= split;
				blocksize -= split;
				saa->audtail = 0;
			}
			if (copy_from_user(saa->audbuf + saa->audtail, buf,
					blocksize))
				return -EFAULT;
			saa->audtail += blocksize;
			todo -= blocksize;
			buf += blocksize;
			saa->audtail &= 0xffff;
		} else if (saa->writemode == VID_WRITE_MPEG_VID) {
			spin_lock_irqsave(&saa->lock, flags);
			if (saa->vidhead <= saa->vidtail)
				blocksize = 524288 -
					(saa->vidtail - saa->vidhead);
			else
				blocksize = saa->vidhead - saa->vidtail;
			spin_unlock_irqrestore(&saa->lock, flags);
			if (blocksize < 65536) {
				saawrite(SAA7146_PSR_DEBI_S |
					SAA7146_PSR_PIN1, SAA7146_IER);
				saawrite(SAA7146_PSR_PIN1, SAA7146_PSR);
				/* wait for buffer space to open */
				interruptible_sleep_on(&saa->vidq);
			}
			spin_lock_irqsave(&saa->lock, flags);
			if (saa->vidhead <= saa->vidtail) {
				blocksize = 524288 -
					(saa->vidtail - saa->vidhead);
				split = 524288 - saa->vidtail;
			} else {
				blocksize = saa->vidhead - saa->vidtail;
				split = 524288;
			}
			spin_unlock_irqrestore(&saa->lock, flags);
			blocksize--;
			if (blocksize > todo)
				blocksize = todo;
			/* double check that we really have space */
			if (!blocksize)
				return -ENOSPC;
			if (split < blocksize) {
				if (copy_from_user(saa->vidbuf +
						saa->vidtail, buf, split))
					return -EFAULT;
				buf += split;
				todo -= split;
				blocksize -= split;
				saa->vidtail = 0;
			}
			if (copy_from_user(saa->vidbuf + saa->vidtail, buf,
					blocksize))
				return -EFAULT;
			saa->vidtail += blocksize;
			todo -= blocksize;
			buf += blocksize;
			saa->vidtail &= 0x7ffff;
		} else if (saa->writemode == VID_WRITE_OSD) {
			if (count > 131072)
				return -ENOSPC;
			if (copy_from_user(saa->osdbuf, buf, count))
				return -EFAULT;
			buf += count;
			saa->osdhead = 0;
			saa->osdtail = count;
			debiwrite(saa, debNormal, IBM_MP2_OSD_ADDR, 0, 2);
			debiwrite(saa, debNormal, IBM_MP2_OSD_LINK_ADDR, 0, 2);
			debiwrite(saa, debNormal, IBM_MP2_MASK0, 0xc00d, 2);
			debiwrite(saa, debNormal, IBM_MP2_DISP_MODE,
				debiread(saa, debNormal,
					IBM_MP2_DISP_MODE, 2) | 1, 2);
			/* trigger osd data transfer */
			saawrite(SAA7146_PSR_DEBI_S |
				 SAA7146_PSR_PIN1, SAA7146_IER);
			saawrite(SAA7146_PSR_PIN1, SAA7146_PSR);
		}
	}
	return count;
}

static int saa_open(struct inode *inode, struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7146 *saa = container_of(vdev, struct saa7146, video_dev);

	file->private_data = saa;

	saa->user++;
	if (saa->user > 1)
		return 0;	/* device open already, don't reset */
	saa->writemode = VID_WRITE_MPEG_VID;	/* default to video */
	return 0;
}

static int saa_release(struct inode *inode, struct file *file)
{
	struct saa7146 *saa = file->private_data;
	saa->user--;

	if (saa->user > 0)	/* still someone using device */
		return 0;
	saawrite(0x007f0000, SAA7146_MC1);	/* stop all overlay dma */
	return 0;
}

static const struct file_operations saa_fops = {
	.owner = THIS_MODULE,
	.open = saa_open,
	.release = saa_release,
	.ioctl = saa_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = v4l_compat_ioctl32,
#endif
	.read = saa_read,
	.llseek = no_llseek,
	.write = saa_write,
	.mmap = saa_mmap,
};

/* template for video_device-structure */
static struct video_device saa_template = {
	.name = "SAA7146A",
	.type = VID_TYPE_CAPTURE | VID_TYPE_OVERLAY,
	.fops = &saa_fops,
	.minor = -1,
};

static int __devinit configure_saa7146(struct pci_dev *pdev, int num)
{
	int retval;
	struct saa7146 *saa = pci_get_drvdata(pdev);

	saa->endmarkhead = saa->endmarktail = 0;
	saa->win.x = saa->win.y = 0;
	saa->win.width = saa->win.cropwidth = 720;
	saa->win.height = saa->win.cropheight = 480;
	saa->win.cropx = saa->win.cropy = 0;
	saa->win.bpp = 2;
	saa->win.depth = 16;
	saa->win.color_fmt = palette2fmt[VIDEO_PALETTE_RGB565];
	saa->win.bpl = 1024 * saa->win.bpp;
	saa->win.swidth = 1024;
	saa->win.sheight = 768;
	saa->picture.brightness = 32768;
	saa->picture.contrast = 38768;
	saa->picture.colour = 32768;
	saa->cap = 0;
	saa->nr = num;
	saa->playmode = VID_PLAY_NORMAL;
	memset(saa->boardcfg, 0, 64);	/* clear board config area */
	saa->saa7146_mem = NULL;
	saa->dmavid1 = saa->dmavid2 = saa->dmavid3 = saa->dmaa1in =
	    saa->dmaa1out = saa->dmaa2in = saa->dmaa2out =
	    saa->pagevid1 = saa->pagevid2 = saa->pagevid3 = saa->pagea1in =
	    saa->pagea1out = saa->pagea2in = saa->pagea2out =
	    saa->pagedebi = saa->dmaRPS1 = saa->dmaRPS2 = saa->pageRPS1 =
	    saa->pageRPS2 = NULL;
	saa->audbuf = saa->vidbuf = saa->osdbuf = saa->dmadebi = NULL;
	saa->audhead = saa->vidtail = 0;

	init_waitqueue_head(&saa->i2cq);
	init_waitqueue_head(&saa->audq);
	init_waitqueue_head(&saa->debiq);
	init_waitqueue_head(&saa->vidq);
	spin_lock_init(&saa->lock);

	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "%d: pci_enable_device failed!\n", num);
		goto err;
	}

	saa->id = pdev->device;
	saa->irq = pdev->irq;
	saa->video_dev.minor = -1;
	saa->saa7146_adr = pci_resource_start(pdev, 0);
	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &saa->revision);

	saa->saa7146_mem = ioremap(saa->saa7146_adr, 0x200);
	if (saa->saa7146_mem == NULL) {
		dev_err(&pdev->dev, "%d: ioremap failed!\n", num);
		retval = -EIO;
		goto err;
	}

	memcpy(&saa->video_dev, &saa_template, sizeof(saa_template));
	saawrite(0, SAA7146_IER);	/* turn off all interrupts */

	retval = request_irq(saa->irq, saa7146_irq, IRQF_SHARED | IRQF_DISABLED,
		"stradis", saa);
	if (retval == -EINVAL)
		dev_err(&pdev->dev, "%d: Bad irq number or handler\n", num);
	else if (retval == -EBUSY)
		dev_err(&pdev->dev, "%d: IRQ %ld busy, change your PnP config "
			"in BIOS\n", num, saa->irq);
	if (retval < 0)
		goto errio;

	pci_set_master(pdev);
	retval = video_register_device(&saa->video_dev, VFL_TYPE_GRABBER,
		video_nr);
	if (retval < 0) {
		dev_err(&pdev->dev, "%d: error in registering video device!\n",
			num);
		goto errio;
	}

	return 0;
errio:
	iounmap(saa->saa7146_mem);
err:
	return retval;
}

static int __devinit init_saa7146(struct pci_dev *pdev)
{
	struct saa7146 *saa = pci_get_drvdata(pdev);

	saa->user = 0;
	/* reset the saa7146 */
	saawrite(0xffff0000, SAA7146_MC1);
	mdelay(5);
	/* enable debi and i2c transfers and pins */
	saawrite(((SAA7146_MC1_EDP | SAA7146_MC1_EI2C |
		   SAA7146_MC1_TR_E_DEBI) << 16) | 0xffff, SAA7146_MC1);
	/* ensure proper state of chip */
	saawrite(0x00000000, SAA7146_PAGE1);
	saawrite(0x00f302c0, SAA7146_NUM_LINE_BYTE1);
	saawrite(0x00000000, SAA7146_PAGE2);
	saawrite(0x01400080, SAA7146_NUM_LINE_BYTE2);
	saawrite(0x00000000, SAA7146_DD1_INIT);
	saawrite(0x00000000, SAA7146_DD1_STREAM_B);
	saawrite(0x00000000, SAA7146_DD1_STREAM_A);
	saawrite(0x00000000, SAA7146_BRS_CTRL);
	saawrite(0x80400040, SAA7146_BCS_CTRL);
	saawrite(0x0000e000 /*| (1<<29) */ , SAA7146_HPS_CTRL);
	saawrite(0x00000060, SAA7146_CLIP_FORMAT_CTRL);
	saawrite(0x00000000, SAA7146_ACON1);
	saawrite(0x00000000, SAA7146_ACON2);
	saawrite(0x00000600, SAA7146_I2C_STATUS);
	saawrite(((SAA7146_MC2_UPLD_D1_B | SAA7146_MC2_UPLD_D1_A |
		SAA7146_MC2_UPLD_BRS | SAA7146_MC2_UPLD_HPS_H |
		SAA7146_MC2_UPLD_HPS_V | SAA7146_MC2_UPLD_DMA2 |
		SAA7146_MC2_UPLD_DMA1 | SAA7146_MC2_UPLD_I2C) << 16) | 0xffff,
		SAA7146_MC2);
	/* setup arbitration control registers */
	saawrite(0x1412121a, SAA7146_PCI_BT_V1);

	/* allocate 32k dma buffer + 4k for page table */
	if ((saa->dmadebi = kmalloc(32768 + 4096, GFP_KERNEL)) == NULL) {
		dev_err(&pdev->dev, "%d: debi kmalloc failed\n", saa->nr);
		goto err;
	}
#if 0
	saa->pagedebi = saa->dmadebi + 32768;	/* top 4k is for mmu */
	saawrite(virt_to_bus(saa->pagedebi) /*|0x800 */ , SAA7146_DEBI_PAGE);
	for (i = 0; i < 12; i++)	/* setup mmu page table */
		saa->pagedebi[i] = virt_to_bus((saa->dmadebi + i * 4096));
#endif
	saa->audhead = saa->vidhead = saa->osdhead = 0;
	saa->audtail = saa->vidtail = saa->osdtail = 0;
	if (saa->vidbuf == NULL && (saa->vidbuf = vmalloc(524288)) == NULL) {
		dev_err(&pdev->dev, "%d: malloc failed\n", saa->nr);
		goto err;
	}
	if (saa->audbuf == NULL && (saa->audbuf = vmalloc(65536)) == NULL) {
		dev_err(&pdev->dev, "%d: malloc failed\n", saa->nr);
		goto errfree;
	}
	if (saa->osdbuf == NULL && (saa->osdbuf = vmalloc(131072)) == NULL) {
		dev_err(&pdev->dev, "%d: malloc failed\n", saa->nr);
		goto errfree;
	}
	/* allocate 81920 byte buffer for clipping */
	if ((saa->dmavid2 = kzalloc(VIDEO_CLIPMAP_SIZE, GFP_KERNEL)) == NULL) {
		dev_err(&pdev->dev, "%d: clip kmalloc failed\n", saa->nr);
		goto errfree;
	}
	/* setup clipping registers */
	saawrite(virt_to_bus(saa->dmavid2), SAA7146_BASE_EVEN2);
	saawrite(virt_to_bus(saa->dmavid2) + 128, SAA7146_BASE_ODD2);
	saawrite(virt_to_bus(saa->dmavid2) + VIDEO_CLIPMAP_SIZE,
		 SAA7146_PROT_ADDR2);
	saawrite(256, SAA7146_PITCH2);
	saawrite(4, SAA7146_PAGE2);	/* dma direction: read, no byteswap */
	saawrite(((SAA7146_MC2_UPLD_DMA2) << 16) | SAA7146_MC2_UPLD_DMA2,
		 SAA7146_MC2);
	I2CBusScan(saa);

	return 0;
errfree:
	vfree(saa->osdbuf);
	vfree(saa->audbuf);
	vfree(saa->vidbuf);
	saa->audbuf = saa->osdbuf = saa->vidbuf = NULL;
err:
	return -ENOMEM;
}

static void stradis_release_saa(struct pci_dev *pdev)
{
	u8 command;
	struct saa7146 *saa = pci_get_drvdata(pdev);

	/* turn off all capturing, DMA and IRQs */
	saawrite(0xffff0000, SAA7146_MC1);	/* reset chip */
	saawrite(0, SAA7146_MC2);
	saawrite(0, SAA7146_IER);
	saawrite(0xffffffffUL, SAA7146_ISR);

	/* disable PCI bus-mastering */
	pci_read_config_byte(pdev, PCI_COMMAND, &command);
	command &= ~PCI_COMMAND_MASTER;
	pci_write_config_byte(pdev, PCI_COMMAND, command);

	/* unmap and free memory */
	saa->audhead = saa->audtail = saa->osdhead = 0;
	saa->vidhead = saa->vidtail = saa->osdtail = 0;
	vfree(saa->vidbuf);
	vfree(saa->audbuf);
	vfree(saa->osdbuf);
	kfree(saa->dmavid2);
	saa->audbuf = saa->vidbuf = saa->osdbuf = NULL;
	saa->dmavid2 = NULL;
	kfree(saa->dmadebi);
	kfree(saa->dmavid1);
	kfree(saa->dmavid3);
	kfree(saa->dmaa1in);
	kfree(saa->dmaa1out);
	kfree(saa->dmaa2in);
	kfree(saa->dmaa2out);
	kfree(saa->dmaRPS1);
	kfree(saa->dmaRPS2);
	free_irq(saa->irq, saa);
	if (saa->saa7146_mem)
		iounmap(saa->saa7146_mem);
	if (saa->video_dev.minor != -1)
		video_unregister_device(&saa->video_dev);
}

static int __devinit stradis_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	int retval = -EINVAL;

	if (saa_num >= SAA7146_MAX)
		goto err;

	if (!pdev->subsystem_vendor)
		dev_info(&pdev->dev, "%d: rev1 decoder\n", saa_num);
	else
		dev_info(&pdev->dev, "%d: SDM2xx found\n", saa_num);

	pci_set_drvdata(pdev, &saa7146s[saa_num]);

	retval = configure_saa7146(pdev, saa_num);
	if (retval) {
		dev_err(&pdev->dev, "%d: error in configuring\n", saa_num);
		goto err;
	}

	if (init_saa7146(pdev) < 0) {
		dev_err(&pdev->dev, "%d: error in initialization\n", saa_num);
		retval = -EIO;
		goto errrel;
	}

	saa_num++;

	return 0;
errrel:
	stradis_release_saa(pdev);
err:
	return retval;
}

static void __devexit stradis_remove(struct pci_dev *pdev)
{
	stradis_release_saa(pdev);
}

static struct pci_device_id stradis_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PHILIPS, PCI_DEVICE_ID_PHILIPS_SAA7146) },
	{ 0 }
};


static struct pci_driver stradis_driver = {
	.name = "stradis",
	.id_table = stradis_pci_tbl,
	.probe = stradis_probe,
	.remove = __devexit_p(stradis_remove)
};

static int __init stradis_init(void)
{
	int retval;

	saa_num = 0;

	retval = pci_register_driver(&stradis_driver);
	if (retval)
		printk(KERN_ERR "stradis: Unable to register pci driver.\n");

	return retval;
}

static void __exit stradis_exit(void)
{
	pci_unregister_driver(&stradis_driver);
	printk(KERN_INFO "stradis: module cleanup complete\n");
}

module_init(stradis_init);
module_exit(stradis_exit);
