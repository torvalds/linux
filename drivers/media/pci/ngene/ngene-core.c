/*
 * ngene.c: nGene PCIe bridge driver
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * Copyright (C) 2008-2009 Ralph Metzler <rjkm@metzlerbros.de>
 *                         Modifications for new nGene firmware,
 *                         support for EEPROM-copying,
 *                         support for new dual DVB-S2 card prototype
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <asm/div64.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/byteorder/generic.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>

#include "ngene.h"

static int one_adapter;
module_param(one_adapter, int, 0444);
MODULE_PARM_DESC(one_adapter, "Use only one adapter.");

static int shutdown_workaround;
module_param(shutdown_workaround, int, 0644);
MODULE_PARM_DESC(shutdown_workaround, "Activate workaround for shutdown problem with some chipsets.");

static int debug;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "Print debugging information.");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define ngwriteb(dat, adr)         writeb((dat), dev->iomem + (adr))
#define ngwritel(dat, adr)         writel((dat), dev->iomem + (adr))
#define ngwriteb(dat, adr)         writeb((dat), dev->iomem + (adr))
#define ngreadl(adr)               readl(dev->iomem + (adr))
#define ngreadb(adr)               readb(dev->iomem + (adr))
#define ngcpyto(adr, src, count)   memcpy_toio(dev->iomem + (adr), (src), (count))
#define ngcpyfrom(dst, adr, count) memcpy_fromio((dst), dev->iomem + (adr), (count))

/****************************************************************************/
/* nGene interrupt handler **************************************************/
/****************************************************************************/

static void event_tasklet(unsigned long data)
{
	struct ngene *dev = (struct ngene *)data;

	while (dev->EventQueueReadIndex != dev->EventQueueWriteIndex) {
		struct EVENT_BUFFER Event =
			dev->EventQueue[dev->EventQueueReadIndex];
		dev->EventQueueReadIndex =
			(dev->EventQueueReadIndex + 1) & (EVENT_QUEUE_SIZE - 1);

		if ((Event.UARTStatus & 0x01) && (dev->TxEventNotify))
			dev->TxEventNotify(dev, Event.TimeStamp);
		if ((Event.UARTStatus & 0x02) && (dev->RxEventNotify))
			dev->RxEventNotify(dev, Event.TimeStamp,
					   Event.RXCharacter);
	}
}

static void demux_tasklet(unsigned long data)
{
	struct ngene_channel *chan = (struct ngene_channel *)data;
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct SBufferHeader *Cur = chan->nextBuffer;

	spin_lock_irq(&chan->state_lock);

	while (Cur->ngeneBuffer.SR.Flags & 0x80) {
		if (chan->mode & NGENE_IO_TSOUT) {
			u32 Flags = chan->DataFormatFlags;
			if (Cur->ngeneBuffer.SR.Flags & 0x20)
				Flags |= BEF_OVERFLOW;
			if (chan->pBufferExchange) {
				if (!chan->pBufferExchange(chan,
							   Cur->Buffer1,
							   chan->Capture1Length,
							   Cur->ngeneBuffer.SR.
							   Clock, Flags)) {
					/*
					   We didn't get data
					   Clear in service flag to make sure we
					   get called on next interrupt again.
					   leave fill/empty (0x80) flag alone
					   to avoid hardware running out of
					   buffers during startup, we hold only
					   in run state ( the source may be late
					   delivering data )
					*/

					if (chan->HWState == HWSTATE_RUN) {
						Cur->ngeneBuffer.SR.Flags &=
							~0x40;
						break;
						/* Stop processing stream */
					}
				} else {
					/* We got a valid buffer,
					   so switch to run state */
					chan->HWState = HWSTATE_RUN;
				}
			} else {
				dev_err(pdev, "OOPS\n");
				if (chan->HWState == HWSTATE_RUN) {
					Cur->ngeneBuffer.SR.Flags &= ~0x40;
					break;	/* Stop processing stream */
				}
			}
			if (chan->AudioDTOUpdated) {
				dev_info(pdev, "Update AudioDTO = %d\n",
					 chan->AudioDTOValue);
				Cur->ngeneBuffer.SR.DTOUpdate =
					chan->AudioDTOValue;
				chan->AudioDTOUpdated = 0;
			}
		} else {
			if (chan->HWState == HWSTATE_RUN) {
				u32 Flags = chan->DataFormatFlags;
				IBufferExchange *exch1 = chan->pBufferExchange;
				IBufferExchange *exch2 = chan->pBufferExchange2;
				if (Cur->ngeneBuffer.SR.Flags & 0x01)
					Flags |= BEF_EVEN_FIELD;
				if (Cur->ngeneBuffer.SR.Flags & 0x20)
					Flags |= BEF_OVERFLOW;
				spin_unlock_irq(&chan->state_lock);
				if (exch1)
					exch1(chan, Cur->Buffer1,
						chan->Capture1Length,
						Cur->ngeneBuffer.SR.Clock,
						Flags);
				if (exch2)
					exch2(chan, Cur->Buffer2,
						chan->Capture2Length,
						Cur->ngeneBuffer.SR.Clock,
						Flags);
				spin_lock_irq(&chan->state_lock);
			} else if (chan->HWState != HWSTATE_STOP)
				chan->HWState = HWSTATE_RUN;
		}
		Cur->ngeneBuffer.SR.Flags = 0x00;
		Cur = Cur->Next;
	}
	chan->nextBuffer = Cur;

	spin_unlock_irq(&chan->state_lock);
}

static irqreturn_t irq_handler(int irq, void *dev_id)
{
	struct ngene *dev = (struct ngene *)dev_id;
	struct device *pdev = &dev->pci_dev->dev;
	u32 icounts = 0;
	irqreturn_t rc = IRQ_NONE;
	u32 i = MAX_STREAM;
	u8 *tmpCmdDoneByte;

	if (dev->BootFirmware) {
		icounts = ngreadl(NGENE_INT_COUNTS);
		if (icounts != dev->icounts) {
			ngwritel(0, FORCE_NMI);
			dev->cmd_done = 1;
			wake_up(&dev->cmd_wq);
			dev->icounts = icounts;
			rc = IRQ_HANDLED;
		}
		return rc;
	}

	ngwritel(0, FORCE_NMI);

	spin_lock(&dev->cmd_lock);
	tmpCmdDoneByte = dev->CmdDoneByte;
	if (tmpCmdDoneByte &&
	    (*tmpCmdDoneByte ||
	    (dev->ngenetohost[0] == 1 && dev->ngenetohost[1] != 0))) {
		dev->CmdDoneByte = NULL;
		dev->cmd_done = 1;
		wake_up(&dev->cmd_wq);
		rc = IRQ_HANDLED;
	}
	spin_unlock(&dev->cmd_lock);

	if (dev->EventBuffer->EventStatus & 0x80) {
		u8 nextWriteIndex =
			(dev->EventQueueWriteIndex + 1) &
			(EVENT_QUEUE_SIZE - 1);
		if (nextWriteIndex != dev->EventQueueReadIndex) {
			dev->EventQueue[dev->EventQueueWriteIndex] =
				*(dev->EventBuffer);
			dev->EventQueueWriteIndex = nextWriteIndex;
		} else {
			dev_err(pdev, "event overflow\n");
			dev->EventQueueOverflowCount += 1;
			dev->EventQueueOverflowFlag = 1;
		}
		dev->EventBuffer->EventStatus &= ~0x80;
		tasklet_schedule(&dev->event_tasklet);
		rc = IRQ_HANDLED;
	}

	while (i > 0) {
		i--;
		spin_lock(&dev->channel[i].state_lock);
		/* if (dev->channel[i].State>=KSSTATE_RUN) { */
		if (dev->channel[i].nextBuffer) {
			if ((dev->channel[i].nextBuffer->
			     ngeneBuffer.SR.Flags & 0xC0) == 0x80) {
				dev->channel[i].nextBuffer->
					ngeneBuffer.SR.Flags |= 0x40;
				tasklet_schedule(
					&dev->channel[i].demux_tasklet);
				rc = IRQ_HANDLED;
			}
		}
		spin_unlock(&dev->channel[i].state_lock);
	}

	/* Request might have been processed by a previous call. */
	return IRQ_HANDLED;
}

/****************************************************************************/
/* nGene command interface **************************************************/
/****************************************************************************/

static void dump_command_io(struct ngene *dev)
{
	struct device *pdev = &dev->pci_dev->dev;
	u8 buf[8], *b;

	ngcpyfrom(buf, HOST_TO_NGENE, 8);
	dev_err(pdev, "host_to_ngene (%04x): %*ph\n", HOST_TO_NGENE, 8, buf);

	ngcpyfrom(buf, NGENE_TO_HOST, 8);
	dev_err(pdev, "ngene_to_host (%04x): %*ph\n", NGENE_TO_HOST, 8, buf);

	b = dev->hosttongene;
	dev_err(pdev, "dev->hosttongene (%p): %*ph\n", b, 8, b);

	b = dev->ngenetohost;
	dev_err(pdev, "dev->ngenetohost (%p): %*ph\n", b, 8, b);
}

static int ngene_command_mutex(struct ngene *dev, struct ngene_command *com)
{
	struct device *pdev = &dev->pci_dev->dev;
	int ret;
	u8 *tmpCmdDoneByte;

	dev->cmd_done = 0;

	if (com->cmd.hdr.Opcode == CMD_FWLOAD_PREPARE) {
		dev->BootFirmware = 1;
		dev->icounts = ngreadl(NGENE_INT_COUNTS);
		ngwritel(0, NGENE_COMMAND);
		ngwritel(0, NGENE_COMMAND_HI);
		ngwritel(0, NGENE_STATUS);
		ngwritel(0, NGENE_STATUS_HI);
		ngwritel(0, NGENE_EVENT);
		ngwritel(0, NGENE_EVENT_HI);
	} else if (com->cmd.hdr.Opcode == CMD_FWLOAD_FINISH) {
		u64 fwio = dev->PAFWInterfaceBuffer;

		ngwritel(fwio & 0xffffffff, NGENE_COMMAND);
		ngwritel(fwio >> 32, NGENE_COMMAND_HI);
		ngwritel((fwio + 256) & 0xffffffff, NGENE_STATUS);
		ngwritel((fwio + 256) >> 32, NGENE_STATUS_HI);
		ngwritel((fwio + 512) & 0xffffffff, NGENE_EVENT);
		ngwritel((fwio + 512) >> 32, NGENE_EVENT_HI);
	}

	memcpy(dev->FWInterfaceBuffer, com->cmd.raw8, com->in_len + 2);

	if (dev->BootFirmware)
		ngcpyto(HOST_TO_NGENE, com->cmd.raw8, com->in_len + 2);

	spin_lock_irq(&dev->cmd_lock);
	tmpCmdDoneByte = dev->ngenetohost + com->out_len;
	if (!com->out_len)
		tmpCmdDoneByte++;
	*tmpCmdDoneByte = 0;
	dev->ngenetohost[0] = 0;
	dev->ngenetohost[1] = 0;
	dev->CmdDoneByte = tmpCmdDoneByte;
	spin_unlock_irq(&dev->cmd_lock);

	/* Notify 8051. */
	ngwritel(1, FORCE_INT);

	ret = wait_event_timeout(dev->cmd_wq, dev->cmd_done == 1, 2 * HZ);
	if (!ret) {
		/*ngwritel(0, FORCE_NMI);*/

		dev_err(pdev, "Command timeout cmd=%02x prev=%02x\n",
			com->cmd.hdr.Opcode, dev->prev_cmd);
		dump_command_io(dev);
		return -1;
	}
	if (com->cmd.hdr.Opcode == CMD_FWLOAD_FINISH)
		dev->BootFirmware = 0;

	dev->prev_cmd = com->cmd.hdr.Opcode;

	if (!com->out_len)
		return 0;

	memcpy(com->cmd.raw8, dev->ngenetohost, com->out_len);

	return 0;
}

int ngene_command(struct ngene *dev, struct ngene_command *com)
{
	int result;

	mutex_lock(&dev->cmd_mutex);
	result = ngene_command_mutex(dev, com);
	mutex_unlock(&dev->cmd_mutex);
	return result;
}


static int ngene_command_load_firmware(struct ngene *dev,
				       u8 *ngene_fw, u32 size)
{
#define FIRSTCHUNK (1024)
	u32 cleft;
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_FWLOAD_PREPARE;
	com.cmd.hdr.Length = 0;
	com.in_len = 0;
	com.out_len = 0;

	ngene_command(dev, &com);

	cleft = (size + 3) & ~3;
	if (cleft > FIRSTCHUNK) {
		ngcpyto(PROGRAM_SRAM + FIRSTCHUNK, ngene_fw + FIRSTCHUNK,
			cleft - FIRSTCHUNK);
		cleft = FIRSTCHUNK;
	}
	ngcpyto(DATA_FIFO_AREA, ngene_fw, cleft);

	memset(&com, 0, sizeof(struct ngene_command));
	com.cmd.hdr.Opcode = CMD_FWLOAD_FINISH;
	com.cmd.hdr.Length = 4;
	com.cmd.FWLoadFinish.Address = DATA_FIFO_AREA;
	com.cmd.FWLoadFinish.Length = (unsigned short)cleft;
	com.in_len = 4;
	com.out_len = 0;

	return ngene_command(dev, &com);
}


static int ngene_command_config_buf(struct ngene *dev, u8 config)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_CONFIGURE_BUFFER;
	com.cmd.hdr.Length = 1;
	com.cmd.ConfigureBuffers.config = config;
	com.in_len = 1;
	com.out_len = 0;

	if (ngene_command(dev, &com) < 0)
		return -EIO;
	return 0;
}

static int ngene_command_config_free_buf(struct ngene *dev, u8 *config)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_CONFIGURE_FREE_BUFFER;
	com.cmd.hdr.Length = 6;
	memcpy(&com.cmd.ConfigureBuffers.config, config, 6);
	com.in_len = 6;
	com.out_len = 0;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	return 0;
}

int ngene_command_gpio_set(struct ngene *dev, u8 select, u8 level)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_SET_GPIO_PIN;
	com.cmd.hdr.Length = 1;
	com.cmd.SetGpioPin.select = select | (level << 7);
	com.in_len = 1;
	com.out_len = 0;

	return ngene_command(dev, &com);
}


/*
 02000640 is sample on rising edge.
 02000740 is sample on falling edge.
 02000040 is ignore "valid" signal

 0: FD_CTL1 Bit 7,6 must be 0,1
    7   disable(fw controlled)
    6   0-AUX,1-TS
    5   0-par,1-ser
    4   0-lsb/1-msb
    3,2 reserved
    1,0 0-no sync, 1-use ext. start, 2-use 0x47, 3-both
 1: FD_CTL2 has 3-valid must be hi, 2-use valid, 1-edge
 2: FD_STA is read-only. 0-sync
 3: FD_INSYNC is number of 47s to trigger "in sync".
 4: FD_OUTSYNC is number of 47s to trigger "out of sync".
 5: FD_MAXBYTE1 is low-order of bytes per packet.
 6: FD_MAXBYTE2 is high-order of bytes per packet.
 7: Top byte is unused.
*/

/****************************************************************************/

static u8 TSFeatureDecoderSetup[8 * 5] = {
	0x42, 0x00, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,
	0x40, 0x06, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,	/* DRXH */
	0x71, 0x07, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,	/* DRXHser */
	0x72, 0x00, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,	/* S2ser */
	0x40, 0x07, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00, /* LGDT3303 */
};

/* Set NGENE I2S Config to 16 bit packed */
static u8 I2SConfiguration[] = {
	0x00, 0x10, 0x00, 0x00,
	0x80, 0x10, 0x00, 0x00,
};

static u8 SPDIFConfiguration[10] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Set NGENE I2S Config to transport stream compatible mode */

static u8 TS_I2SConfiguration[4] = { 0x3E, 0x18, 0x00, 0x00 };

static u8 TS_I2SOutConfiguration[4] = { 0x80, 0x04, 0x00, 0x00 };

static u8 ITUDecoderSetup[4][16] = {
	{0x1c, 0x13, 0x01, 0x68, 0x3d, 0x90, 0x14, 0x20,  /* SDTV */
	 0x00, 0x00, 0x01, 0xb0, 0x9c, 0x00, 0x00, 0x00},
	{0x9c, 0x03, 0x23, 0xC0, 0x60, 0x0E, 0x13, 0x00,
	 0x00, 0x00, 0x00, 0x01, 0xB0, 0x00, 0x00, 0x00},
	{0x9f, 0x00, 0x23, 0xC0, 0x60, 0x0F, 0x13, 0x00,  /* HDTV 1080i50 */
	 0x00, 0x00, 0x00, 0x01, 0xB0, 0x00, 0x00, 0x00},
	{0x9c, 0x01, 0x23, 0xC0, 0x60, 0x0E, 0x13, 0x00,  /* HDTV 1080i60 */
	 0x00, 0x00, 0x00, 0x01, 0xB0, 0x00, 0x00, 0x00},
};

/*
 * 50 48 60 gleich
 * 27p50 9f 00 22 80 42 69 18 ...
 * 27p60 93 00 22 80 82 69 1c ...
 */

/* Maxbyte to 1144 (for raw data) */
static u8 ITUFeatureDecoderSetup[8] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x04, 0x00
};

void FillTSBuffer(void *Buffer, int Length, u32 Flags)
{
	u32 *ptr = Buffer;

	memset(Buffer, TS_FILLER, Length);
	while (Length > 0) {
		if (Flags & DF_SWAP32)
			*ptr = 0x471FFF10;
		else
			*ptr = 0x10FF1F47;
		ptr += (188 / 4);
		Length -= 188;
	}
}


static void flush_buffers(struct ngene_channel *chan)
{
	u8 val;

	do {
		msleep(1);
		spin_lock_irq(&chan->state_lock);
		val = chan->nextBuffer->ngeneBuffer.SR.Flags & 0x80;
		spin_unlock_irq(&chan->state_lock);
	} while (val);
}

static void clear_buffers(struct ngene_channel *chan)
{
	struct SBufferHeader *Cur = chan->nextBuffer;

	do {
		memset(&Cur->ngeneBuffer.SR, 0, sizeof(Cur->ngeneBuffer.SR));
		if (chan->mode & NGENE_IO_TSOUT)
			FillTSBuffer(Cur->Buffer1,
				     chan->Capture1Length,
				     chan->DataFormatFlags);
		Cur = Cur->Next;
	} while (Cur != chan->nextBuffer);

	if (chan->mode & NGENE_IO_TSOUT) {
		chan->nextBuffer->ngeneBuffer.SR.DTOUpdate =
			chan->AudioDTOValue;
		chan->AudioDTOUpdated = 0;

		Cur = chan->TSIdleBuffer.Head;

		do {
			memset(&Cur->ngeneBuffer.SR, 0,
			       sizeof(Cur->ngeneBuffer.SR));
			FillTSBuffer(Cur->Buffer1,
				     chan->Capture1Length,
				     chan->DataFormatFlags);
			Cur = Cur->Next;
		} while (Cur != chan->TSIdleBuffer.Head);
	}
}

static int ngene_command_stream_control(struct ngene *dev, u8 stream,
					u8 control, u8 mode, u8 flags)
{
	struct device *pdev = &dev->pci_dev->dev;
	struct ngene_channel *chan = &dev->channel[stream];
	struct ngene_command com;
	u16 BsUVI = ((stream & 1) ? 0x9400 : 0x9300);
	u16 BsSDI = ((stream & 1) ? 0x9600 : 0x9500);
	u16 BsSPI = ((stream & 1) ? 0x9800 : 0x9700);
	u16 BsSDO = 0x9B00;

	memset(&com, 0, sizeof(com));
	com.cmd.hdr.Opcode = CMD_CONTROL;
	com.cmd.hdr.Length = sizeof(struct FW_STREAM_CONTROL) - 2;
	com.cmd.StreamControl.Stream = stream | (control ? 8 : 0);
	if (chan->mode & NGENE_IO_TSOUT)
		com.cmd.StreamControl.Stream |= 0x07;
	com.cmd.StreamControl.Control = control |
		(flags & SFLAG_ORDER_LUMA_CHROMA);
	com.cmd.StreamControl.Mode = mode;
	com.in_len = sizeof(struct FW_STREAM_CONTROL);
	com.out_len = 0;

	dev_dbg(pdev, "Stream=%02x, Control=%02x, Mode=%02x\n",
		com.cmd.StreamControl.Stream, com.cmd.StreamControl.Control,
		com.cmd.StreamControl.Mode);

	chan->Mode = mode;

	if (!(control & 0x80)) {
		spin_lock_irq(&chan->state_lock);
		if (chan->State == KSSTATE_RUN) {
			chan->State = KSSTATE_ACQUIRE;
			chan->HWState = HWSTATE_STOP;
			spin_unlock_irq(&chan->state_lock);
			if (ngene_command(dev, &com) < 0)
				return -1;
			/* clear_buffers(chan); */
			flush_buffers(chan);
			return 0;
		}
		spin_unlock_irq(&chan->state_lock);
		return 0;
	}

	if (mode & SMODE_AUDIO_CAPTURE) {
		com.cmd.StreamControl.CaptureBlockCount =
			chan->Capture1Length / AUDIO_BLOCK_SIZE;
		com.cmd.StreamControl.Buffer_Address = chan->RingBuffer.PAHead;
	} else if (mode & SMODE_TRANSPORT_STREAM) {
		com.cmd.StreamControl.CaptureBlockCount =
			chan->Capture1Length / TS_BLOCK_SIZE;
		com.cmd.StreamControl.MaxLinesPerField =
			chan->Capture1Length / TS_BLOCK_SIZE;
		com.cmd.StreamControl.Buffer_Address =
			chan->TSRingBuffer.PAHead;
		if (chan->mode & NGENE_IO_TSOUT) {
			com.cmd.StreamControl.BytesPerVBILine =
				chan->Capture1Length / TS_BLOCK_SIZE;
			com.cmd.StreamControl.Stream |= 0x07;
		}
	} else {
		com.cmd.StreamControl.BytesPerVideoLine = chan->nBytesPerLine;
		com.cmd.StreamControl.MaxLinesPerField = chan->nLines;
		com.cmd.StreamControl.MinLinesPerField = 100;
		com.cmd.StreamControl.Buffer_Address = chan->RingBuffer.PAHead;

		if (mode & SMODE_VBI_CAPTURE) {
			com.cmd.StreamControl.MaxVBILinesPerField =
				chan->nVBILines;
			com.cmd.StreamControl.MinVBILinesPerField = 0;
			com.cmd.StreamControl.BytesPerVBILine =
				chan->nBytesPerVBILine;
		}
		if (flags & SFLAG_COLORBAR)
			com.cmd.StreamControl.Stream |= 0x04;
	}

	spin_lock_irq(&chan->state_lock);
	if (mode & SMODE_AUDIO_CAPTURE) {
		chan->nextBuffer = chan->RingBuffer.Head;
		if (mode & SMODE_AUDIO_SPDIF) {
			com.cmd.StreamControl.SetupDataLen =
				sizeof(SPDIFConfiguration);
			com.cmd.StreamControl.SetupDataAddr = BsSPI;
			memcpy(com.cmd.StreamControl.SetupData,
			       SPDIFConfiguration, sizeof(SPDIFConfiguration));
		} else {
			com.cmd.StreamControl.SetupDataLen = 4;
			com.cmd.StreamControl.SetupDataAddr = BsSDI;
			memcpy(com.cmd.StreamControl.SetupData,
			       I2SConfiguration +
			       4 * dev->card_info->i2s[stream], 4);
		}
	} else if (mode & SMODE_TRANSPORT_STREAM) {
		chan->nextBuffer = chan->TSRingBuffer.Head;
		if (stream >= STREAM_AUDIOIN1) {
			if (chan->mode & NGENE_IO_TSOUT) {
				com.cmd.StreamControl.SetupDataLen =
					sizeof(TS_I2SOutConfiguration);
				com.cmd.StreamControl.SetupDataAddr = BsSDO;
				memcpy(com.cmd.StreamControl.SetupData,
				       TS_I2SOutConfiguration,
				       sizeof(TS_I2SOutConfiguration));
			} else {
				com.cmd.StreamControl.SetupDataLen =
					sizeof(TS_I2SConfiguration);
				com.cmd.StreamControl.SetupDataAddr = BsSDI;
				memcpy(com.cmd.StreamControl.SetupData,
				       TS_I2SConfiguration,
				       sizeof(TS_I2SConfiguration));
			}
		} else {
			com.cmd.StreamControl.SetupDataLen = 8;
			com.cmd.StreamControl.SetupDataAddr = BsUVI + 0x10;
			memcpy(com.cmd.StreamControl.SetupData,
			       TSFeatureDecoderSetup +
			       8 * dev->card_info->tsf[stream], 8);
		}
	} else {
		chan->nextBuffer = chan->RingBuffer.Head;
		com.cmd.StreamControl.SetupDataLen =
			16 + sizeof(ITUFeatureDecoderSetup);
		com.cmd.StreamControl.SetupDataAddr = BsUVI;
		memcpy(com.cmd.StreamControl.SetupData,
		       ITUDecoderSetup[chan->itumode], 16);
		memcpy(com.cmd.StreamControl.SetupData + 16,
		       ITUFeatureDecoderSetup, sizeof(ITUFeatureDecoderSetup));
	}
	clear_buffers(chan);
	chan->State = KSSTATE_RUN;
	if (mode & SMODE_TRANSPORT_STREAM)
		chan->HWState = HWSTATE_RUN;
	else
		chan->HWState = HWSTATE_STARTUP;
	spin_unlock_irq(&chan->state_lock);

	if (ngene_command(dev, &com) < 0)
		return -1;

	return 0;
}

void set_transfer(struct ngene_channel *chan, int state)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	u8 control = 0, mode = 0, flags = 0;
	struct ngene *dev = chan->dev;
	int ret;

	/*
	dev_info(pdev, "st %d\n", state);
	msleep(100);
	*/

	if (state) {
		if (chan->running) {
			dev_info(pdev, "already running\n");
			return;
		}
	} else {
		if (!chan->running) {
			dev_info(pdev, "already stopped\n");
			return;
		}
	}

	if (dev->card_info->switch_ctrl)
		dev->card_info->switch_ctrl(chan, 1, state ^ 1);

	if (state) {
		spin_lock_irq(&chan->state_lock);

		/* dev_info(pdev, "lock=%08x\n",
			  ngreadl(0x9310)); */
		dvb_ringbuffer_flush(&dev->tsout_rbuf);
		control = 0x80;
		if (chan->mode & (NGENE_IO_TSIN | NGENE_IO_TSOUT)) {
			chan->Capture1Length = 512 * 188;
			mode = SMODE_TRANSPORT_STREAM;
		}
		if (chan->mode & NGENE_IO_TSOUT) {
			chan->pBufferExchange = tsout_exchange;
			/* 0x66666666 = 50MHz *2^33 /250MHz */
			chan->AudioDTOValue = 0x80000000;
			chan->AudioDTOUpdated = 1;
		}
		if (chan->mode & NGENE_IO_TSIN)
			chan->pBufferExchange = tsin_exchange;
		spin_unlock_irq(&chan->state_lock);
	}
		/* else dev_info(pdev, "lock=%08x\n",
			   ngreadl(0x9310)); */

	mutex_lock(&dev->stream_mutex);
	ret = ngene_command_stream_control(dev, chan->number,
					   control, mode, flags);
	mutex_unlock(&dev->stream_mutex);

	if (!ret)
		chan->running = state;
	else
		dev_err(pdev, "%s %d failed\n", __func__, state);
	if (!state) {
		spin_lock_irq(&chan->state_lock);
		chan->pBufferExchange = NULL;
		dvb_ringbuffer_flush(&dev->tsout_rbuf);
		spin_unlock_irq(&chan->state_lock);
	}
}


/****************************************************************************/
/* nGene hardware init and release functions ********************************/
/****************************************************************************/

static void free_ringbuffer(struct ngene *dev, struct SRingBufferDescriptor *rb)
{
	struct SBufferHeader *Cur = rb->Head;
	u32 j;

	if (!Cur)
		return;

	for (j = 0; j < rb->NumBuffers; j++, Cur = Cur->Next) {
		if (Cur->Buffer1)
			pci_free_consistent(dev->pci_dev,
					    rb->Buffer1Length,
					    Cur->Buffer1,
					    Cur->scList1->Address);

		if (Cur->Buffer2)
			pci_free_consistent(dev->pci_dev,
					    rb->Buffer2Length,
					    Cur->Buffer2,
					    Cur->scList2->Address);
	}

	if (rb->SCListMem)
		pci_free_consistent(dev->pci_dev, rb->SCListMemSize,
				    rb->SCListMem, rb->PASCListMem);

	pci_free_consistent(dev->pci_dev, rb->MemSize, rb->Head, rb->PAHead);
}

static void free_idlebuffer(struct ngene *dev,
		     struct SRingBufferDescriptor *rb,
		     struct SRingBufferDescriptor *tb)
{
	int j;
	struct SBufferHeader *Cur = tb->Head;

	if (!rb->Head)
		return;
	free_ringbuffer(dev, rb);
	for (j = 0; j < tb->NumBuffers; j++, Cur = Cur->Next) {
		Cur->Buffer2 = NULL;
		Cur->scList2 = NULL;
		Cur->ngeneBuffer.Address_of_first_entry_2 = 0;
		Cur->ngeneBuffer.Number_of_entries_2 = 0;
	}
}

static void free_common_buffers(struct ngene *dev)
{
	u32 i;
	struct ngene_channel *chan;

	for (i = STREAM_VIDEOIN1; i < MAX_STREAM; i++) {
		chan = &dev->channel[i];
		free_idlebuffer(dev, &chan->TSIdleBuffer, &chan->TSRingBuffer);
		free_ringbuffer(dev, &chan->RingBuffer);
		free_ringbuffer(dev, &chan->TSRingBuffer);
	}

	if (dev->OverflowBuffer)
		pci_free_consistent(dev->pci_dev,
				    OVERFLOW_BUFFER_SIZE,
				    dev->OverflowBuffer, dev->PAOverflowBuffer);

	if (dev->FWInterfaceBuffer)
		pci_free_consistent(dev->pci_dev,
				    4096,
				    dev->FWInterfaceBuffer,
				    dev->PAFWInterfaceBuffer);
}

/****************************************************************************/
/* Ring buffer handling *****************************************************/
/****************************************************************************/

static int create_ring_buffer(struct pci_dev *pci_dev,
		       struct SRingBufferDescriptor *descr, u32 NumBuffers)
{
	dma_addr_t tmp;
	struct SBufferHeader *Head;
	u32 i;
	u32 MemSize = SIZEOF_SBufferHeader * NumBuffers;
	u64 PARingBufferHead;
	u64 PARingBufferCur;
	u64 PARingBufferNext;
	struct SBufferHeader *Cur, *Next;

	descr->Head = NULL;
	descr->MemSize = 0;
	descr->PAHead = 0;
	descr->NumBuffers = 0;

	if (MemSize < 4096)
		MemSize = 4096;

	Head = pci_alloc_consistent(pci_dev, MemSize, &tmp);
	PARingBufferHead = tmp;

	if (!Head)
		return -ENOMEM;

	memset(Head, 0, MemSize);

	PARingBufferCur = PARingBufferHead;
	Cur = Head;

	for (i = 0; i < NumBuffers - 1; i++) {
		Next = (struct SBufferHeader *)
			(((u8 *) Cur) + SIZEOF_SBufferHeader);
		PARingBufferNext = PARingBufferCur + SIZEOF_SBufferHeader;
		Cur->Next = Next;
		Cur->ngeneBuffer.Next = PARingBufferNext;
		Cur = Next;
		PARingBufferCur = PARingBufferNext;
	}
	/* Last Buffer points back to first one */
	Cur->Next = Head;
	Cur->ngeneBuffer.Next = PARingBufferHead;

	descr->Head       = Head;
	descr->MemSize    = MemSize;
	descr->PAHead     = PARingBufferHead;
	descr->NumBuffers = NumBuffers;

	return 0;
}

static int AllocateRingBuffers(struct pci_dev *pci_dev,
			       dma_addr_t of,
			       struct SRingBufferDescriptor *pRingBuffer,
			       u32 Buffer1Length, u32 Buffer2Length)
{
	dma_addr_t tmp;
	u32 i, j;
	u32 SCListMemSize = pRingBuffer->NumBuffers
		* ((Buffer2Length != 0) ? (NUM_SCATTER_GATHER_ENTRIES * 2) :
		    NUM_SCATTER_GATHER_ENTRIES)
		* sizeof(struct HW_SCATTER_GATHER_ELEMENT);

	u64 PASCListMem;
	struct HW_SCATTER_GATHER_ELEMENT *SCListEntry;
	u64 PASCListEntry;
	struct SBufferHeader *Cur;
	void *SCListMem;

	if (SCListMemSize < 4096)
		SCListMemSize = 4096;

	SCListMem = pci_alloc_consistent(pci_dev, SCListMemSize, &tmp);

	PASCListMem = tmp;
	if (SCListMem == NULL)
		return -ENOMEM;

	memset(SCListMem, 0, SCListMemSize);

	pRingBuffer->SCListMem = SCListMem;
	pRingBuffer->PASCListMem = PASCListMem;
	pRingBuffer->SCListMemSize = SCListMemSize;
	pRingBuffer->Buffer1Length = Buffer1Length;
	pRingBuffer->Buffer2Length = Buffer2Length;

	SCListEntry = SCListMem;
	PASCListEntry = PASCListMem;
	Cur = pRingBuffer->Head;

	for (i = 0; i < pRingBuffer->NumBuffers; i += 1, Cur = Cur->Next) {
		u64 PABuffer;

		void *Buffer = pci_alloc_consistent(pci_dev, Buffer1Length,
						    &tmp);
		PABuffer = tmp;

		if (Buffer == NULL)
			return -ENOMEM;

		Cur->Buffer1 = Buffer;

		SCListEntry->Address = PABuffer;
		SCListEntry->Length  = Buffer1Length;

		Cur->scList1 = SCListEntry;
		Cur->ngeneBuffer.Address_of_first_entry_1 = PASCListEntry;
		Cur->ngeneBuffer.Number_of_entries_1 =
			NUM_SCATTER_GATHER_ENTRIES;

		SCListEntry += 1;
		PASCListEntry += sizeof(struct HW_SCATTER_GATHER_ELEMENT);

#if NUM_SCATTER_GATHER_ENTRIES > 1
		for (j = 0; j < NUM_SCATTER_GATHER_ENTRIES - 1; j += 1) {
			SCListEntry->Address = of;
			SCListEntry->Length = OVERFLOW_BUFFER_SIZE;
			SCListEntry += 1;
			PASCListEntry +=
				sizeof(struct HW_SCATTER_GATHER_ELEMENT);
		}
#endif

		if (!Buffer2Length)
			continue;

		Buffer = pci_alloc_consistent(pci_dev, Buffer2Length, &tmp);
		PABuffer = tmp;

		if (Buffer == NULL)
			return -ENOMEM;

		Cur->Buffer2 = Buffer;

		SCListEntry->Address = PABuffer;
		SCListEntry->Length  = Buffer2Length;

		Cur->scList2 = SCListEntry;
		Cur->ngeneBuffer.Address_of_first_entry_2 = PASCListEntry;
		Cur->ngeneBuffer.Number_of_entries_2 =
			NUM_SCATTER_GATHER_ENTRIES;

		SCListEntry   += 1;
		PASCListEntry += sizeof(struct HW_SCATTER_GATHER_ELEMENT);

#if NUM_SCATTER_GATHER_ENTRIES > 1
		for (j = 0; j < NUM_SCATTER_GATHER_ENTRIES - 1; j++) {
			SCListEntry->Address = of;
			SCListEntry->Length = OVERFLOW_BUFFER_SIZE;
			SCListEntry += 1;
			PASCListEntry +=
				sizeof(struct HW_SCATTER_GATHER_ELEMENT);
		}
#endif

	}

	return 0;
}

static int FillTSIdleBuffer(struct SRingBufferDescriptor *pIdleBuffer,
			    struct SRingBufferDescriptor *pRingBuffer)
{
	/* Copy pointer to scatter gather list in TSRingbuffer
	   structure for buffer 2
	   Load number of buffer
	*/
	u32 n = pRingBuffer->NumBuffers;

	/* Point to first buffer entry */
	struct SBufferHeader *Cur = pRingBuffer->Head;
	int i;
	/* Loop thru all buffer and set Buffer 2 pointers to TSIdlebuffer */
	for (i = 0; i < n; i++) {
		Cur->Buffer2 = pIdleBuffer->Head->Buffer1;
		Cur->scList2 = pIdleBuffer->Head->scList1;
		Cur->ngeneBuffer.Address_of_first_entry_2 =
			pIdleBuffer->Head->ngeneBuffer.
			Address_of_first_entry_1;
		Cur->ngeneBuffer.Number_of_entries_2 =
			pIdleBuffer->Head->ngeneBuffer.Number_of_entries_1;
		Cur = Cur->Next;
	}
	return 0;
}

static u32 RingBufferSizes[MAX_STREAM] = {
	RING_SIZE_VIDEO,
	RING_SIZE_VIDEO,
	RING_SIZE_AUDIO,
	RING_SIZE_AUDIO,
	RING_SIZE_AUDIO,
};

static u32 Buffer1Sizes[MAX_STREAM] = {
	MAX_VIDEO_BUFFER_SIZE,
	MAX_VIDEO_BUFFER_SIZE,
	MAX_AUDIO_BUFFER_SIZE,
	MAX_AUDIO_BUFFER_SIZE,
	MAX_AUDIO_BUFFER_SIZE
};

static u32 Buffer2Sizes[MAX_STREAM] = {
	MAX_VBI_BUFFER_SIZE,
	MAX_VBI_BUFFER_SIZE,
	0,
	0,
	0
};


static int AllocCommonBuffers(struct ngene *dev)
{
	int status = 0, i;

	dev->FWInterfaceBuffer = pci_alloc_consistent(dev->pci_dev, 4096,
						     &dev->PAFWInterfaceBuffer);
	if (!dev->FWInterfaceBuffer)
		return -ENOMEM;
	dev->hosttongene = dev->FWInterfaceBuffer;
	dev->ngenetohost = dev->FWInterfaceBuffer + 256;
	dev->EventBuffer = dev->FWInterfaceBuffer + 512;

	dev->OverflowBuffer = pci_zalloc_consistent(dev->pci_dev,
						    OVERFLOW_BUFFER_SIZE,
						    &dev->PAOverflowBuffer);
	if (!dev->OverflowBuffer)
		return -ENOMEM;

	for (i = STREAM_VIDEOIN1; i < MAX_STREAM; i++) {
		int type = dev->card_info->io_type[i];

		dev->channel[i].State = KSSTATE_STOP;

		if (type & (NGENE_IO_TV | NGENE_IO_HDTV | NGENE_IO_AIN)) {
			status = create_ring_buffer(dev->pci_dev,
						    &dev->channel[i].RingBuffer,
						    RingBufferSizes[i]);
			if (status < 0)
				break;

			if (type & (NGENE_IO_TV | NGENE_IO_AIN)) {
				status = AllocateRingBuffers(dev->pci_dev,
							     dev->
							     PAOverflowBuffer,
							     &dev->channel[i].
							     RingBuffer,
							     Buffer1Sizes[i],
							     Buffer2Sizes[i]);
				if (status < 0)
					break;
			} else if (type & NGENE_IO_HDTV) {
				status = AllocateRingBuffers(dev->pci_dev,
							     dev->
							     PAOverflowBuffer,
							     &dev->channel[i].
							     RingBuffer,
							   MAX_HDTV_BUFFER_SIZE,
							     0);
				if (status < 0)
					break;
			}
		}

		if (type & (NGENE_IO_TSIN | NGENE_IO_TSOUT)) {

			status = create_ring_buffer(dev->pci_dev,
						    &dev->channel[i].
						    TSRingBuffer, RING_SIZE_TS);
			if (status < 0)
				break;

			status = AllocateRingBuffers(dev->pci_dev,
						     dev->PAOverflowBuffer,
						     &dev->channel[i].
						     TSRingBuffer,
						     MAX_TS_BUFFER_SIZE, 0);
			if (status)
				break;
		}

		if (type & NGENE_IO_TSOUT) {
			status = create_ring_buffer(dev->pci_dev,
						    &dev->channel[i].
						    TSIdleBuffer, 1);
			if (status < 0)
				break;
			status = AllocateRingBuffers(dev->pci_dev,
						     dev->PAOverflowBuffer,
						     &dev->channel[i].
						     TSIdleBuffer,
						     MAX_TS_BUFFER_SIZE, 0);
			if (status)
				break;
			FillTSIdleBuffer(&dev->channel[i].TSIdleBuffer,
					 &dev->channel[i].TSRingBuffer);
		}
	}
	return status;
}

static void ngene_release_buffers(struct ngene *dev)
{
	if (dev->iomem)
		iounmap(dev->iomem);
	free_common_buffers(dev);
	vfree(dev->tsout_buf);
	vfree(dev->tsin_buf);
	vfree(dev->ain_buf);
	vfree(dev->vin_buf);
	vfree(dev);
}

static int ngene_get_buffers(struct ngene *dev)
{
	if (AllocCommonBuffers(dev))
		return -ENOMEM;
	if (dev->card_info->io_type[4] & NGENE_IO_TSOUT) {
		dev->tsout_buf = vmalloc(TSOUT_BUF_SIZE);
		if (!dev->tsout_buf)
			return -ENOMEM;
		dvb_ringbuffer_init(&dev->tsout_rbuf,
				    dev->tsout_buf, TSOUT_BUF_SIZE);
	}
	if (dev->card_info->io_type[2]&NGENE_IO_TSIN) {
		dev->tsin_buf = vmalloc(TSIN_BUF_SIZE);
		if (!dev->tsin_buf)
			return -ENOMEM;
		dvb_ringbuffer_init(&dev->tsin_rbuf,
				    dev->tsin_buf, TSIN_BUF_SIZE);
	}
	if (dev->card_info->io_type[2] & NGENE_IO_AIN) {
		dev->ain_buf = vmalloc(AIN_BUF_SIZE);
		if (!dev->ain_buf)
			return -ENOMEM;
		dvb_ringbuffer_init(&dev->ain_rbuf, dev->ain_buf, AIN_BUF_SIZE);
	}
	if (dev->card_info->io_type[0] & NGENE_IO_HDTV) {
		dev->vin_buf = vmalloc(VIN_BUF_SIZE);
		if (!dev->vin_buf)
			return -ENOMEM;
		dvb_ringbuffer_init(&dev->vin_rbuf, dev->vin_buf, VIN_BUF_SIZE);
	}
	dev->iomem = ioremap(pci_resource_start(dev->pci_dev, 0),
			     pci_resource_len(dev->pci_dev, 0));
	if (!dev->iomem)
		return -ENOMEM;

	return 0;
}

static void ngene_init(struct ngene *dev)
{
	struct device *pdev = &dev->pci_dev->dev;
	int i;

	tasklet_init(&dev->event_tasklet, event_tasklet, (unsigned long)dev);

	memset_io(dev->iomem + 0xc000, 0x00, 0x220);
	memset_io(dev->iomem + 0xc400, 0x00, 0x100);

	for (i = 0; i < MAX_STREAM; i++) {
		dev->channel[i].dev = dev;
		dev->channel[i].number = i;
	}

	dev->fw_interface_version = 0;

	ngwritel(0, NGENE_INT_ENABLE);

	dev->icounts = ngreadl(NGENE_INT_COUNTS);

	dev->device_version = ngreadl(DEV_VER) & 0x0f;
	dev_info(pdev, "Device version %d\n", dev->device_version);
}

static int ngene_load_firm(struct ngene *dev)
{
	struct device *pdev = &dev->pci_dev->dev;
	u32 size;
	const struct firmware *fw = NULL;
	u8 *ngene_fw;
	char *fw_name;
	int err, version;

	version = dev->card_info->fw_version;

	switch (version) {
	default:
	case 15:
		version = 15;
		size = 23466;
		fw_name = "ngene_15.fw";
		dev->cmd_timeout_workaround = true;
		break;
	case 16:
		size = 23498;
		fw_name = "ngene_16.fw";
		dev->cmd_timeout_workaround = true;
		break;
	case 17:
		size = 24446;
		fw_name = "ngene_17.fw";
		dev->cmd_timeout_workaround = true;
		break;
	case 18:
		size = 0;
		fw_name = "ngene_18.fw";
		break;
	}

	if (request_firmware(&fw, fw_name, &dev->pci_dev->dev) < 0) {
		dev_err(pdev, "Could not load firmware file %s.\n", fw_name);
		dev_info(pdev, "Copy %s to your hotplug directory!\n",
			 fw_name);
		return -1;
	}
	if (size == 0)
		size = fw->size;
	if (size != fw->size) {
		dev_err(pdev, "Firmware %s has invalid size!", fw_name);
		err = -1;
	} else {
		dev_info(pdev, "Loading firmware file %s.\n", fw_name);
		ngene_fw = (u8 *) fw->data;
		err = ngene_command_load_firmware(dev, ngene_fw, size);
	}

	release_firmware(fw);

	return err;
}

static void ngene_stop(struct ngene *dev)
{
	mutex_destroy(&dev->cmd_mutex);
	i2c_del_adapter(&(dev->channel[0].i2c_adapter));
	i2c_del_adapter(&(dev->channel[1].i2c_adapter));
	ngwritel(0, NGENE_INT_ENABLE);
	ngwritel(0, NGENE_COMMAND);
	ngwritel(0, NGENE_COMMAND_HI);
	ngwritel(0, NGENE_STATUS);
	ngwritel(0, NGENE_STATUS_HI);
	ngwritel(0, NGENE_EVENT);
	ngwritel(0, NGENE_EVENT_HI);
	free_irq(dev->pci_dev->irq, dev);
#ifdef CONFIG_PCI_MSI
	if (dev->msi_enabled)
		pci_disable_msi(dev->pci_dev);
#endif
}

static int ngene_buffer_config(struct ngene *dev)
{
	int stat;

	if (dev->card_info->fw_version >= 17) {
		u8 tsin12_config[6]   = { 0x60, 0x60, 0x00, 0x00, 0x00, 0x00 };
		u8 tsin1234_config[6] = { 0x30, 0x30, 0x00, 0x30, 0x30, 0x00 };
		u8 tsio1235_config[6] = { 0x30, 0x30, 0x00, 0x28, 0x00, 0x38 };
		u8 *bconf = tsin12_config;

		if (dev->card_info->io_type[2]&NGENE_IO_TSIN &&
		    dev->card_info->io_type[3]&NGENE_IO_TSIN) {
			bconf = tsin1234_config;
			if (dev->card_info->io_type[4]&NGENE_IO_TSOUT &&
			    dev->ci.en)
				bconf = tsio1235_config;
		}
		stat = ngene_command_config_free_buf(dev, bconf);
	} else {
		int bconf = BUFFER_CONFIG_4422;

		if (dev->card_info->io_type[3] == NGENE_IO_TSIN)
			bconf = BUFFER_CONFIG_3333;
		stat = ngene_command_config_buf(dev, bconf);
	}
	return stat;
}


static int ngene_start(struct ngene *dev)
{
	int stat;
	int i;

	pci_set_master(dev->pci_dev);
	ngene_init(dev);

	stat = request_irq(dev->pci_dev->irq, irq_handler,
			   IRQF_SHARED, "nGene",
			   (void *)dev);
	if (stat < 0)
		return stat;

	init_waitqueue_head(&dev->cmd_wq);
	init_waitqueue_head(&dev->tx_wq);
	init_waitqueue_head(&dev->rx_wq);
	mutex_init(&dev->cmd_mutex);
	mutex_init(&dev->stream_mutex);
	sema_init(&dev->pll_mutex, 1);
	mutex_init(&dev->i2c_switch_mutex);
	spin_lock_init(&dev->cmd_lock);
	for (i = 0; i < MAX_STREAM; i++)
		spin_lock_init(&dev->channel[i].state_lock);
	ngwritel(1, TIMESTAMPS);

	ngwritel(1, NGENE_INT_ENABLE);

	stat = ngene_load_firm(dev);
	if (stat < 0)
		goto fail;

#ifdef CONFIG_PCI_MSI
	/* enable MSI if kernel and card support it */
	if (pci_msi_enabled() && dev->card_info->msi_supported) {
		struct device *pdev = &dev->pci_dev->dev;
		unsigned long flags;

		ngwritel(0, NGENE_INT_ENABLE);
		free_irq(dev->pci_dev->irq, dev);
		stat = pci_enable_msi(dev->pci_dev);
		if (stat) {
			dev_info(pdev, "MSI not available\n");
			flags = IRQF_SHARED;
		} else {
			flags = 0;
			dev->msi_enabled = true;
		}
		stat = request_irq(dev->pci_dev->irq, irq_handler,
					flags, "nGene", dev);
		if (stat < 0)
			goto fail2;
		ngwritel(1, NGENE_INT_ENABLE);
	}
#endif

	stat = ngene_i2c_init(dev, 0);
	if (stat < 0)
		goto fail;

	stat = ngene_i2c_init(dev, 1);
	if (stat < 0)
		goto fail;

	return 0;

fail:
	ngwritel(0, NGENE_INT_ENABLE);
	free_irq(dev->pci_dev->irq, dev);
#ifdef CONFIG_PCI_MSI
fail2:
	if (dev->msi_enabled)
		pci_disable_msi(dev->pci_dev);
#endif
	return stat;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void release_channel(struct ngene_channel *chan)
{
	struct dvb_demux *dvbdemux = &chan->demux;
	struct ngene *dev = chan->dev;

	if (chan->running)
		set_transfer(chan, 0);

	tasklet_kill(&chan->demux_tasklet);

	if (chan->ci_dev) {
		dvb_unregister_device(chan->ci_dev);
		chan->ci_dev = NULL;
	}

	if (chan->fe2)
		dvb_unregister_frontend(chan->fe2);

	if (chan->fe) {
		dvb_unregister_frontend(chan->fe);

		/* release I2C client (tuner) if needed */
		if (chan->i2c_client_fe) {
			dvb_module_release(chan->i2c_client[0]);
			chan->i2c_client[0] = NULL;
		}

		dvb_frontend_detach(chan->fe);
		chan->fe = NULL;
	}

	if (chan->has_demux) {
		dvb_net_release(&chan->dvbnet);
		dvbdemux->dmx.close(&dvbdemux->dmx);
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx,
					      &chan->hw_frontend);
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx,
					      &chan->mem_frontend);
		dvb_dmxdev_release(&chan->dmxdev);
		dvb_dmx_release(&chan->demux);
		chan->has_demux = false;
	}

	if (chan->has_adapter) {
		dvb_unregister_adapter(&dev->adapter[chan->number]);
		chan->has_adapter = false;
	}
}

static int init_channel(struct ngene_channel *chan)
{
	int ret = 0, nr = chan->number;
	struct dvb_adapter *adapter = NULL;
	struct dvb_demux *dvbdemux = &chan->demux;
	struct ngene *dev = chan->dev;
	struct ngene_info *ni = dev->card_info;
	int io = ni->io_type[nr];

	tasklet_init(&chan->demux_tasklet, demux_tasklet, (unsigned long)chan);
	chan->users = 0;
	chan->type = io;
	chan->mode = chan->type;	/* for now only one mode */
	chan->i2c_client_fe = 0;	/* be sure this is set to zero */

	if (io & NGENE_IO_TSIN) {
		chan->fe = NULL;
		if (ni->demod_attach[nr]) {
			ret = ni->demod_attach[nr](chan);
			if (ret < 0)
				goto err;
		}
		if (chan->fe && ni->tuner_attach[nr]) {
			ret = ni->tuner_attach[nr](chan);
			if (ret < 0)
				goto err;
		}
	}

	if (!dev->ci.en && (io & NGENE_IO_TSOUT))
		return 0;

	if (io & (NGENE_IO_TSIN | NGENE_IO_TSOUT)) {
		if (nr >= STREAM_AUDIOIN1)
			chan->DataFormatFlags = DF_SWAP32;

		if (nr == 0 || !one_adapter || dev->first_adapter == NULL) {
			adapter = &dev->adapter[nr];
			ret = dvb_register_adapter(adapter, "nGene",
						   THIS_MODULE,
						   &chan->dev->pci_dev->dev,
						   adapter_nr);
			if (ret < 0)
				goto err;
			if (dev->first_adapter == NULL)
				dev->first_adapter = adapter;
			chan->has_adapter = true;
		} else
			adapter = dev->first_adapter;
	}

	if (dev->ci.en && (io & NGENE_IO_TSOUT)) {
		dvb_ca_en50221_init(adapter, dev->ci.en, 0, 1);
		set_transfer(chan, 1);
		chan->dev->channel[2].DataFormatFlags = DF_SWAP32;
		set_transfer(&chan->dev->channel[2], 1);
		dvb_register_device(adapter, &chan->ci_dev,
				    &ngene_dvbdev_ci, (void *) chan,
				    DVB_DEVICE_SEC, 0);
		if (!chan->ci_dev)
			goto err;
	}

	if (chan->fe) {
		if (dvb_register_frontend(adapter, chan->fe) < 0)
			goto err;
		chan->has_demux = true;
	}
	if (chan->fe2) {
		if (dvb_register_frontend(adapter, chan->fe2) < 0)
			goto err;
		if (chan->fe) {
			chan->fe2->tuner_priv = chan->fe->tuner_priv;
			memcpy(&chan->fe2->ops.tuner_ops,
			       &chan->fe->ops.tuner_ops,
			       sizeof(struct dvb_tuner_ops));
		}
	}

	if (chan->has_demux) {
		ret = my_dvb_dmx_ts_card_init(dvbdemux, "SW demux",
					      ngene_start_feed,
					      ngene_stop_feed, chan);
		ret = my_dvb_dmxdev_ts_card_init(&chan->dmxdev, &chan->demux,
						 &chan->hw_frontend,
						 &chan->mem_frontend, adapter);
		ret = dvb_net_init(adapter, &chan->dvbnet, &chan->demux.dmx);
	}

	return ret;

err:
	if (chan->fe) {
		dvb_frontend_detach(chan->fe);
		chan->fe = NULL;
	}
	release_channel(chan);
	return 0;
}

static int init_channels(struct ngene *dev)
{
	int i, j;

	for (i = 0; i < MAX_STREAM; i++) {
		dev->channel[i].number = i;
		if (init_channel(&dev->channel[i]) < 0) {
			for (j = i - 1; j >= 0; j--)
				release_channel(&dev->channel[j]);
			return -1;
		}
	}
	return 0;
}

static const struct cxd2099_cfg cxd_cfgtmpl = {
	.bitrate = 62000,
	.polarity = 0,
	.clock_mode = 0,
};

static void cxd_attach(struct ngene *dev)
{
	struct device *pdev = &dev->pci_dev->dev;
	struct ngene_ci *ci = &dev->ci;
	struct cxd2099_cfg cxd_cfg = cxd_cfgtmpl;
	struct i2c_client *client;
	int ret;
	u8 type;

	/* check for CXD2099AR presence before attaching */
	ret = ngene_port_has_cxd2099(&dev->channel[0].i2c_adapter, &type);
	if (!ret) {
		dev_dbg(pdev, "No CXD2099AR found\n");
		return;
	}

	if (type != 1) {
		dev_warn(pdev, "CXD2099AR is uninitialized!\n");
		return;
	}

	cxd_cfg.en = &ci->en;
	client = dvb_module_probe("cxd2099", NULL,
				  &dev->channel[0].i2c_adapter,
				  0x40, &cxd_cfg);
	if (!client)
		goto err;

	ci->dev = dev;
	dev->channel[0].i2c_client[0] = client;
	return;

err:
	dev_err(pdev, "CXD2099AR attach failed\n");
	return;
}

static void cxd_detach(struct ngene *dev)
{
	struct ngene_ci *ci = &dev->ci;

	dvb_ca_en50221_release(ci->en);

	dvb_module_release(dev->channel[0].i2c_client[0]);
	dev->channel[0].i2c_client[0] = NULL;
	ci->en = NULL;
}

/***********************************/
/* workaround for shutdown failure */
/***********************************/

static void ngene_unlink(struct ngene *dev)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_MEM_WRITE;
	com.cmd.hdr.Length = 3;
	com.cmd.MemoryWrite.address = 0x910c;
	com.cmd.MemoryWrite.data = 0xff;
	com.in_len = 3;
	com.out_len = 1;

	mutex_lock(&dev->cmd_mutex);
	ngwritel(0, NGENE_INT_ENABLE);
	ngene_command_mutex(dev, &com);
	mutex_unlock(&dev->cmd_mutex);
}

void ngene_shutdown(struct pci_dev *pdev)
{
	struct ngene *dev = pci_get_drvdata(pdev);

	if (!dev || !shutdown_workaround)
		return;

	dev_info(&pdev->dev, "shutdown workaround...\n");
	ngene_unlink(dev);
	pci_disable_device(pdev);
}

/****************************************************************************/
/* device probe/remove calls ************************************************/
/****************************************************************************/

void ngene_remove(struct pci_dev *pdev)
{
	struct ngene *dev = pci_get_drvdata(pdev);
	int i;

	tasklet_kill(&dev->event_tasklet);
	for (i = MAX_STREAM - 1; i >= 0; i--)
		release_channel(&dev->channel[i]);
	if (dev->ci.en)
		cxd_detach(dev);
	ngene_stop(dev);
	ngene_release_buffers(dev);
	pci_disable_device(pdev);
}

int ngene_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	struct ngene *dev;
	int stat = 0;

	if (pci_enable_device(pci_dev) < 0)
		return -ENODEV;

	dev = vzalloc(sizeof(struct ngene));
	if (dev == NULL) {
		stat = -ENOMEM;
		goto fail0;
	}

	dev->pci_dev = pci_dev;
	dev->card_info = (struct ngene_info *)id->driver_data;
	dev_info(&pci_dev->dev, "Found %s\n", dev->card_info->name);

	pci_set_drvdata(pci_dev, dev);

	/* Alloc buffers and start nGene */
	stat = ngene_get_buffers(dev);
	if (stat < 0)
		goto fail1;
	stat = ngene_start(dev);
	if (stat < 0)
		goto fail1;

	cxd_attach(dev);

	stat = ngene_buffer_config(dev);
	if (stat < 0)
		goto fail1;


	dev->i2c_current_bus = -1;

	/* Register DVB adapters and devices for both channels */
	stat = init_channels(dev);
	if (stat < 0)
		goto fail2;

	return 0;

fail2:
	ngene_stop(dev);
fail1:
	ngene_release_buffers(dev);
fail0:
	pci_disable_device(pci_dev);
	return stat;
}
