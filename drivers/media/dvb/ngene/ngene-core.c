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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/smp_lock.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/byteorder/generic.h>
#include <linux/firmware.h>

#include "ngene.h"

#ifdef NGENE_COMMAND_API
#include "ngene-ioctls.h"
#endif

#define FW_INC 1
#ifdef FW_INC
#include "ngene_fw_15.h"
#include "ngene_fw_16.h"
#include "ngene_fw_17.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int load_firmware;
module_param(load_firmware, int, 0444);
MODULE_PARM_DESC(load_firmware, "Try to load firmware from file.");
#endif

static int copy_eeprom;
module_param(copy_eeprom, int, 0444);
MODULE_PARM_DESC(copy_eeprom, "Copy eeprom.");

static int ngene_fw_debug;
module_param(ngene_fw_debug, int, 0444);
MODULE_PARM_DESC(ngene_fw_debug, "Debug firmware.");

static int debug;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "Print debugging information.");

#define dprintk	if (debug) printk

#define DEVICE_NAME "ngene"

#define ngwriteb(dat, adr)         writeb((dat), (char *)(dev->iomem + (adr)))
#define ngwritel(dat, adr)         writel((dat), (char *)(dev->iomem + (adr)))
#define ngwriteb(dat, adr)         writeb((dat), (char *)(dev->iomem + (adr)))
#define ngreadl(adr)               readl(dev->iomem + (adr))
#define ngreadb(adr)               readb(dev->iomem + (adr))
#define ngcpyto(adr, src, count)   memcpy_toio((char *) \
				   (dev->iomem + (adr)), (src), (count))
#define ngcpyfrom(dst, adr, count) memcpy_fromio((dst), (char *) \
				   (dev->iomem + (adr)), (count))

/****************************************************************************/
/* Functions with missing kernel exports ************************************/
/****************************************************************************/

/* yeah, let's throw out all exports which are not used in kernel ... */

void my_dvb_ringbuffer_flush(struct dvb_ringbuffer *rbuf)
{
	rbuf->pread = rbuf->pwrite;
	rbuf->error = 0;
}

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
						/* Stop proccessing stream */
					}
				} else {
					/* We got a valid buffer,
					   so switch to run state */
					chan->HWState = HWSTATE_RUN;
				}
			} else {
				printk(KERN_ERR DEVICE_NAME ": OOPS\n");
				if (chan->HWState == HWSTATE_RUN) {
					Cur->ngeneBuffer.SR.Flags &= ~0x40;
					break;	/* Stop proccessing stream */
				}
			}
			if (chan->AudioDTOUpdated) {
				printk(KERN_INFO DEVICE_NAME
				       ": Update AudioDTO = %d\n",
				       chan->AudioDTOValue);
				Cur->ngeneBuffer.SR.DTOUpdate =
					chan->AudioDTOValue;
				chan->AudioDTOUpdated = 0;
			}
		} else {
			if (chan->HWState == HWSTATE_RUN) {
				u32 Flags = 0;
				if (Cur->ngeneBuffer.SR.Flags & 0x01)
					Flags |= BEF_EVEN_FIELD;
				if (Cur->ngeneBuffer.SR.Flags & 0x20)
					Flags |= BEF_OVERFLOW;
				if (chan->pBufferExchange)
					chan->pBufferExchange(chan,
							      Cur->Buffer1,
							      chan->
							      Capture1Length,
							      Cur->ngeneBuffer.
							      SR.Clock, Flags);
				if (chan->pBufferExchange2)
					chan->pBufferExchange2(chan,
							       Cur->Buffer2,
							       chan->
							       Capture2Length,
							       Cur->ngeneBuffer.
							       SR.Clock, Flags);
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
			printk(KERN_ERR DEVICE_NAME ": event overflow\n");
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

	return rc;
}

/****************************************************************************/
/* nGene command interface **************************************************/
/****************************************************************************/

static int ngene_command_mutex(struct ngene *dev, struct ngene_command *com)
{
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

		printk(KERN_ERR DEVICE_NAME
		       ": Command timeout cmd=%02x prev=%02x\n",
		       com->cmd.hdr.Opcode, dev->prev_cmd);
		return -1;
	}
	if (com->cmd.hdr.Opcode == CMD_FWLOAD_FINISH)
		dev->BootFirmware = 0;

	dev->prev_cmd = com->cmd.hdr.Opcode;
	msleep(10);

	if (!com->out_len)
		return 0;

	memcpy(com->cmd.raw8, dev->ngenetohost, com->out_len);

	return 0;
}

static int ngene_command(struct ngene *dev, struct ngene_command *com)
{
	int result;

	down(&dev->cmd_mutex);
	result = ngene_command_mutex(dev, com);
	up(&dev->cmd_mutex);
	return result;
}

int ngene_command_nop(struct ngene *dev)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_NOP;
	com.cmd.hdr.Length = 0;
	com.in_len = 0;
	com.out_len = 0;

	return ngene_command(dev, &com);
}

int ngene_command_i2c_read(struct ngene *dev, u8 adr,
			   u8 *out, u8 outlen, u8 *in, u8 inlen, int flag)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_I2C_READ;
	com.cmd.hdr.Length = outlen + 3;
	com.cmd.I2CRead.Device = adr << 1;
	memcpy(com.cmd.I2CRead.Data, out, outlen);
	com.cmd.I2CRead.Data[outlen] = inlen;
	com.cmd.I2CRead.Data[outlen + 1] = 0;
	com.in_len = outlen + 3;
	com.out_len = inlen + 1;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	if ((com.cmd.raw8[0] >> 1) != adr)
		return -EIO;

	if (flag)
		memcpy(in, com.cmd.raw8, inlen + 1);
	else
		memcpy(in, com.cmd.raw8 + 1, inlen);
	return 0;
}

int ngene_command_i2c_write(struct ngene *dev, u8 adr, u8 *out, u8 outlen)
{
	struct ngene_command com;


	com.cmd.hdr.Opcode = CMD_I2C_WRITE;
	com.cmd.hdr.Length = outlen + 1;
	com.cmd.I2CRead.Device = adr << 1;
	memcpy(com.cmd.I2CRead.Data, out, outlen);
	com.in_len = outlen + 1;
	com.out_len = 1;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	if (com.cmd.raw8[0] == 1)
		return -EIO;

	return 0;
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
	ngene_fw[FW_DEBUG_DEFAULT - PROGRAM_SRAM] = ngene_fw_debug;
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

int ngene_command_imem_read(struct ngene *dev, u8 adr, u8 *data, int type)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = type ? CMD_SFR_READ : CMD_IRAM_READ;
	com.cmd.hdr.Length = 1;
	com.cmd.SfrIramRead.address = adr;
	com.in_len = 1;
	com.out_len = 2;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	*data = com.cmd.raw8[1];
	return 0;
}

int ngene_command_imem_write(struct ngene *dev, u8 adr, u8 data, int type)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = type ? CMD_SFR_WRITE : CMD_IRAM_WRITE;
	com.cmd.hdr.Length = 2;
	com.cmd.SfrIramWrite.address = adr;
	com.cmd.SfrIramWrite.data = data;
	com.in_len = 2;
	com.out_len = 1;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	return 0;
}

static int ngene_command_config_uart(struct ngene *dev, u8 config,
				     tx_cb_t *tx_cb, rx_cb_t *rx_cb)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_CONFIGURE_UART;
	com.cmd.hdr.Length = sizeof(struct FW_CONFIGURE_UART) - 2;
	com.cmd.ConfigureUart.UartControl = config;
	com.in_len = sizeof(struct FW_CONFIGURE_UART);
	com.out_len = 0;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	dev->TxEventNotify = tx_cb;
	dev->RxEventNotify = rx_cb;

	dprintk(KERN_DEBUG DEVICE_NAME ": Set UART config %02x.\n", config);

	return 0;
}

static void tx_cb(struct ngene *dev, u32 ts)
{
	dev->tx_busy = 0;
	wake_up_interruptible(&dev->tx_wq);
}

static void rx_cb(struct ngene *dev, u32 ts, u8 c)
{
	int rp = dev->uart_rp;
	int nwp, wp = dev->uart_wp;

	/* dprintk(KERN_DEBUG DEVICE_NAME ": %c\n", c); */
	nwp = (wp + 1) % (UART_RBUF_LEN);
	if (nwp == rp)
		return;
	dev->uart_rbuf[wp] = c;
	dev->uart_wp = nwp;
	wake_up_interruptible(&dev->rx_wq);
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

static int ngene_command_gpio_set(struct ngene *dev, u8 select, u8 level)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_SET_GPIO_PIN;
	com.cmd.hdr.Length = 1;
	com.cmd.SetGpioPin.select = select | (level << 7);
	com.in_len = 1;
	com.out_len = 0;

	return ngene_command(dev, &com);
}

/* The reset is only wired to GPIO4 on MicRacer Revision 1.10 !
   Also better set bootdelay to 1 in nvram or less. */
static void ngene_reset_decypher(struct ngene *dev)
{
	printk(KERN_INFO DEVICE_NAME ": Resetting Decypher.\n");
	ngene_command_gpio_set(dev, 4, 0);
	msleep(1);
	ngene_command_gpio_set(dev, 4, 1);
	msleep(2000);
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

static u8 TSFeatureDecoderSetup[8 * 4] = {
	0x42, 0x00, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,
	0x40, 0x06, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,	/* DRXH */
	0x71, 0x07, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,	/* DRXHser */
	0x72, 0x06, 0x00, 0x02, 0x02, 0xbc, 0x00, 0x00,	/* S2ser */
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

static u8 TS_I2SConfiguration[4] = { 0x3E, 0x1A, 0x00, 0x00 }; /*3e 18 00 00 ?*/

static u8 TS_I2SOutConfiguration[4] = { 0x80, 0x20, 0x00, 0x00 };

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

static void FillTSBuffer(void *Buffer, int Length, u32 Flags)
{
	u32 *ptr = Buffer;

	memset(Buffer, Length, 0xff);
	while (Length > 0) {
		if (Flags & DF_SWAP32)
			*ptr = 0x471FFF10;
		else
			*ptr = 0x10FF1F47;
		ptr += (188 / 4);
		Length -= 188;
	}
}

static void clear_tsin(struct ngene_channel *chan)
{
	struct SBufferHeader *Cur = chan->nextBuffer;

	do {
		memset(&Cur->ngeneBuffer.SR, 0, sizeof(Cur->ngeneBuffer.SR));
		Cur = Cur->Next;
	} while (Cur != chan->nextBuffer);
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

int ngene_command_stream_control(struct ngene *dev, u8 stream, u8 control,
				 u8 mode, u8 flags)
{
	struct ngene_channel *chan = &dev->channel[stream];
	struct ngene_command com;
	u16 BsUVI = ((stream & 1) ? 0x9400 : 0x9300);
	u16 BsSDI = ((stream & 1) ? 0x9600 : 0x9500);
	u16 BsSPI = ((stream & 1) ? 0x9800 : 0x9700);
	u16 BsSDO = 0x9B00;

	/* down(&dev->stream_mutex); */
	while (down_trylock(&dev->stream_mutex)) {
		printk(KERN_INFO DEVICE_NAME ": SC locked\n");
		msleep(1);
	}
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

	printk(KERN_INFO DEVICE_NAME ": Stream=%02x, Control=%02x, Mode=%02x\n",
	       com.cmd.StreamControl.Stream, com.cmd.StreamControl.Control,
	       com.cmd.StreamControl.Mode);
	chan->Mode = mode;

	if (!(control & 0x80)) {
		spin_lock_irq(&chan->state_lock);
		if (chan->State == KSSTATE_RUN) {
			chan->State = KSSTATE_ACQUIRE;
			chan->HWState = HWSTATE_STOP;
			spin_unlock_irq(&chan->state_lock);
			if (ngene_command(dev, &com) < 0) {
				up(&dev->stream_mutex);
				return -1;
			}
			/* clear_buffers(chan); */
			flush_buffers(chan);
			up(&dev->stream_mutex);
			return 0;
		}
		spin_unlock_irq(&chan->state_lock);
		up(&dev->stream_mutex);
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

	if (ngene_command(dev, &com) < 0) {
		up(&dev->stream_mutex);
		return -1;
	}
	up(&dev->stream_mutex);
	return 0;
}

int ngene_stream_control(struct ngene *dev, u8 stream, u8 control, u8 mode,
			 u16 lines, u16 bpl, u16 vblines, u16 vbibpl)
{
	if (!(mode & SMODE_TRANSPORT_STREAM))
		return -EINVAL;

	if (lines * bpl > MAX_VIDEO_BUFFER_SIZE)
		return -EINVAL;

	if ((mode & SMODE_TRANSPORT_STREAM) && (((bpl * lines) & 0xff) != 0))
		return -EINVAL;

	if ((mode & SMODE_VIDEO_CAPTURE) && (bpl & 7) != 0)
		return -EINVAL;

	return ngene_command_stream_control(dev, stream, control, mode, 0);
}

/****************************************************************************/
/* I2C **********************************************************************/
/****************************************************************************/

static void ngene_i2c_set_bus(struct ngene *dev, int bus)
{
	if (!(dev->card_info->i2c_access & 2))
		return;
	if (dev->i2c_current_bus == bus)
		return;

	switch (bus) {
	case 0:
		ngene_command_gpio_set(dev, 3, 0);
		ngene_command_gpio_set(dev, 2, 1);
		break;

	case 1:
		ngene_command_gpio_set(dev, 2, 0);
		ngene_command_gpio_set(dev, 3, 1);
		break;
	}
	dev->i2c_current_bus = bus;
}

static int ngene_i2c_master_xfer(struct i2c_adapter *adapter,
				 struct i2c_msg msg[], int num)
{
	struct ngene_channel *chan =
		(struct ngene_channel *)i2c_get_adapdata(adapter);
	struct ngene *dev = chan->dev;

	down(&dev->i2c_switch_mutex);
	ngene_i2c_set_bus(dev, chan->number);

	if (num == 2 && msg[1].flags & I2C_M_RD && !(msg[0].flags & I2C_M_RD))
		if (!ngene_command_i2c_read(dev, msg[0].addr,
					    msg[0].buf, msg[0].len,
					    msg[1].buf, msg[1].len, 0))
			goto done;

	if (num == 1 && !(msg[0].flags & I2C_M_RD))
		if (!ngene_command_i2c_write(dev, msg[0].addr,
					     msg[0].buf, msg[0].len))
			goto done;
	if (num == 1 && (msg[0].flags & I2C_M_RD))
		if (!ngene_command_i2c_read(dev, msg[0].addr, 0, 0,
					    msg[0].buf, msg[0].len, 0))
			goto done;

	up(&dev->i2c_switch_mutex);
	return -EIO;

done:
	up(&dev->i2c_switch_mutex);
	return num;
}


static int ngene_i2c_algo_control(struct i2c_adapter *adap,
				  unsigned int cmd, unsigned long arg)
{
	struct ngene_channel *chan =
		(struct ngene_channel *)i2c_get_adapdata(adap);

	switch (cmd) {
	case IOCTL_MIC_TUN_RDY:
		chan->tun_rdy = 1;
		if (chan->dec_rdy == 1)
			chan->tun_dec_rdy = 1;
		break;

	case IOCTL_MIC_DEC_RDY:
		chan->dec_rdy = 1;
		if (chan->tun_rdy == 1)
			chan->tun_dec_rdy = 1;
		break;

	case IOCTL_MIC_TUN_DETECT:
		{
			int *palorbtsc = (int *)arg;
			*palorbtsc = chan->dev->card_info->ntsc;
			break;
		}

	default:
		break;
	}
	return 0;
}

static u32 ngene_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

struct i2c_algorithm ngene_i2c_algo = {
	.master_xfer = ngene_i2c_master_xfer,
	.functionality = ngene_i2c_functionality,
};

static int ngene_i2c_attach(struct i2c_client *client)
{
	return 0;
}

static int ngene_i2c_detach(struct i2c_client *client)
{
	return 0;
}

static int ngene_i2c_init(struct ngene *dev, int dev_nr)
{
	struct i2c_adapter *adap = &(dev->channel[dev_nr].i2c_adapter);

	i2c_set_adapdata(adap, &(dev->channel[dev_nr]));
#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	adap->class = I2C_ADAP_CLASS_TV_DIGITAL | I2C_CLASS_TV_ANALOG;
#else
	adap->class = I2C_CLASS_TV_ANALOG;
#endif

	strcpy(adap->name, "nGene");

	adap->id = I2C_HW_SAA7146;
	adap->client_register = ngene_i2c_attach;
	adap->client_unregister = ngene_i2c_detach;
	adap->algo = &ngene_i2c_algo;
	adap->algo_data = (void *)&(dev->channel[dev_nr]);

	mutex_init(&adap->bus_lock);
	return i2c_add_adapter(adap);
}

int i2c_write(struct i2c_adapter *adapter, u8 adr, u8 data)
{
	u8 m[1] = {data};
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = m, .len = 1};

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		printk(KERN_ERR DEVICE_NAME
		       ": Failed to write to I2C adr %02x!\n", adr);
		return -1;
	}
	return 0;
}

static int i2c_write_register(struct i2c_adapter *adapter,
			      u8 adr, u8 reg, u8 data)
{
	u8 m[2] = {reg, data};
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = m, .len = 2};

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		printk(KERN_ERR DEVICE_NAME
		       ": Failed to write to I2C register %02x@%02x!\n",
		       reg, adr);
		return -1;
	}
	return 0;
}

static int i2c_write_read(struct i2c_adapter *adapter,
			  u8 adr, u8 *w, u8 wlen, u8 *r, u8 rlen)
{
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf = w, .len = wlen},
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf = r, .len = rlen} };

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		printk(KERN_ERR DEVICE_NAME ": error in i2c_write_read\n");
		return -1;
	}
	return 0;
}

static int test_dec_i2c(struct i2c_adapter *adapter, int reg)
{
	u8 data[256] = { reg, 0x00, 0x93, 0x78, 0x43, 0x45 };
	u8 data2[256];
	int i;

	memset(data2, 0, 256);
	i2c_write_read(adapter, 0x66, data, 2, data2, 4);
	for (i = 0; i < 4; i++)
		printk("%02x ", data2[i]);
	printk("\n");

	return 0;
}


/****************************************************************************/
/* EEPROM TAGS **************************************************************/
/****************************************************************************/

#define MICNG_EE_START      0x0100
#define MICNG_EE_END        0x0FF0

#define MICNG_EETAG_END0    0x0000
#define MICNG_EETAG_END1    0xFFFF

/* 0x0001 - 0x000F reserved for housekeeping */
/* 0xFFFF - 0xFFFE reserved for housekeeping */

/* Micronas assigned tags
   EEProm tags for hardware support */

#define MICNG_EETAG_DRXD1_OSCDEVIATION  0x1000  /* 2 Bytes data */
#define MICNG_EETAG_DRXD2_OSCDEVIATION  0x1001  /* 2 Bytes data */

#define MICNG_EETAG_MT2060_1_1STIF      0x1100  /* 2 Bytes data */
#define MICNG_EETAG_MT2060_2_1STIF      0x1101  /* 2 Bytes data */

/* Tag range for OEMs */

#define MICNG_EETAG_OEM_FIRST  0xC000
#define MICNG_EETAG_OEM_LAST   0xFFEF

static int i2c_write_eeprom(struct i2c_adapter *adapter,
			    u8 adr, u16 reg, u8 data)
{
	u8 m[3] = {(reg >> 8), (reg & 0xff), data};
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = m,
			      .len = sizeof(m)};

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		dprintk(KERN_DEBUG DEVICE_NAME ": Error writing EEPROM!\n");
		return -EIO;
	}
	return 0;
}

static int i2c_read_eeprom(struct i2c_adapter *adapter,
			   u8 adr, u16 reg, u8 *data, int len)
{
	u8 msg[2] = {(reg >> 8), (reg & 0xff)};
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf = msg, .len = 2 },
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf = data, .len = len} };

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		dprintk(KERN_DEBUG DEVICE_NAME ": Error reading EEPROM\n");
		return -EIO;
	}
	return 0;
}

static int ReadEEProm(struct i2c_adapter *adapter,
		      u16 Tag, u32 MaxLen, u8 *data, u32 *pLength)
{
	int status = 0;
	u16 Addr = MICNG_EE_START, Length, tag = 0;
	u8  EETag[3];

	while (Addr + sizeof(u16) + 1 < MICNG_EE_END) {
		if (i2c_read_eeprom(adapter, 0x50, Addr, EETag, sizeof(EETag)))
			return -1;
		tag = (EETag[0] << 8) | EETag[1];
		if (tag == MICNG_EETAG_END0 || tag == MICNG_EETAG_END1)
			return -1;
		if (tag == Tag)
			break;
		Addr += sizeof(u16) + 1 + EETag[2];
	}
	if (Addr + sizeof(u16) + 1 + EETag[2] > MICNG_EE_END) {
		printk(KERN_ERR DEVICE_NAME
		       ": Reached EOEE @ Tag = %04x Length = %3d\n",
		       tag, EETag[2]);
		return -1;
	}
	Length = EETag[2];
	if (Length > MaxLen)
		Length = (u16) MaxLen;
	if (Length > 0) {
		Addr += sizeof(u16) + 1;
		status = i2c_read_eeprom(adapter, 0x50, Addr, data, Length);
		if (!status) {
			*pLength = EETag[2];
			if (Length < EETag[2])
				; /*status=STATUS_BUFFER_OVERFLOW; */
		}
	}
	return status;
}

static int WriteEEProm(struct i2c_adapter *adapter,
		       u16 Tag, u32 Length, u8 *data)
{
	int status = 0;
	u16 Addr = MICNG_EE_START;
	u8 EETag[3];
	u16 tag = 0;
	int retry, i;

	while (Addr + sizeof(u16) + 1 < MICNG_EE_END) {
		if (i2c_read_eeprom(adapter, 0x50, Addr, EETag, sizeof(EETag)))
			return -1;
		tag = (EETag[0] << 8) | EETag[1];
		if (tag == MICNG_EETAG_END0 || tag == MICNG_EETAG_END1)
			return -1;
		if (tag == Tag)
			break;
		Addr += sizeof(u16) + 1 + EETag[2];
	}
	if (Addr + sizeof(u16) + 1 + EETag[2] > MICNG_EE_END) {
		printk(KERN_ERR DEVICE_NAME
		       ": Reached EOEE @ Tag = %04x Length = %3d\n",
		       tag, EETag[2]);
		return -1;
	}

	if (Length > EETag[2])
		return -EINVAL;
	/* Note: We write the data one byte at a time to avoid
	   issues with page sizes. (which are different for
	   each manufacture and eeprom size)
	 */
	Addr += sizeof(u16) + 1;
	for (i = 0; i < Length; i++, Addr++) {
		status = i2c_write_eeprom(adapter, 0x50, Addr, data[i]);

		if (status)
			break;

		/* Poll for finishing write cycle */
		retry = 10;
		while (retry) {
			u8 Tmp;

			msleep(50);
			status = i2c_read_eeprom(adapter, 0x50, Addr, &Tmp, 1);
			if (status)
				break;
			if (Tmp != data[i])
				printk(KERN_ERR DEVICE_NAME
				       "eeprom write error\n");
			retry -= 1;
		}
		if (status) {
			printk(KERN_ERR DEVICE_NAME
			       ": Timeout polling eeprom\n");
			break;
		}
	}
	return status;
}

static void i2c_init_eeprom(struct i2c_adapter *adapter)
{
	u8 tags[] = {0x10, 0x00, 0x02, 0x00, 0x00,
		     0x10, 0x01, 0x02, 0x00, 0x00,
		     0x00, 0x00, 0x00};

	int i;

	for (i = 0; i < sizeof(tags); i++)
		i2c_write_eeprom(adapter, 0x50, 0x0100 + i, tags[i]);
}

static int eeprom_read_ushort(struct i2c_adapter *adapter, u16 tag, u16 *data)
{
	int stat;
	u8 buf[2];
	u32 len = 0;

	stat = ReadEEProm(adapter, tag, 2, buf, &len);
	if (stat)
		return stat;
	if (len != 2)
		return -EINVAL;

	*data = (buf[0] << 8) | buf[1];
	return 0;
}

static int eeprom_write_ushort(struct i2c_adapter *adapter, u16 tag, u16 data)
{
	int stat;
	u8 buf[2];

	buf[0] = data >> 8;
	buf[1] = data & 0xff;
	stat = WriteEEProm(adapter, tag, 2, buf);
	if (stat)
		return stat;
	return 0;
}

static int i2c_dump_eeprom(struct i2c_adapter *adapter, u8 adr)
{
	u8 buf[64];
	int i;

	if (i2c_read_eeprom(adapter, adr, 0x0000, buf, sizeof(buf))) {
		printk(KERN_ERR DEVICE_NAME ": No EEPROM?\n");
		return -1;
	}
	for (i = 0; i < sizeof(buf); i++) {
		if (!(i & 15))
			printk("\n");
		printk("%02x ", buf[i]);
	}
	printk("\n");

	return 0;
}

static int i2c_copy_eeprom(struct i2c_adapter *adapter, u8 adr, u8 adr2)
{
	u8 buf[64];
	int i;

	if (i2c_read_eeprom(adapter, adr, 0x0000, buf, sizeof(buf))) {
		printk(KERN_ERR DEVICE_NAME ": No EEPROM?\n");
		return -1;
	}
	buf[36] = 0xc3;
	buf[39] = 0xab;
	for (i = 0; i < sizeof(buf); i++) {
		i2c_write_eeprom(adapter, adr2, i, buf[i]);
		msleep(10);
	}
	return 0;
}


/****************************************************************************/
/* COMMAND API interface ****************************************************/
/****************************************************************************/

#ifdef NGENE_COMMAND_API

static int command_do_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	int err = 0;

	switch (cmd) {
	case IOCTL_MIC_NO_OP:
		err = ngene_command_nop(dev);
		break;

	case IOCTL_MIC_DOWNLOAD_FIRMWARE:
		break;

	case IOCTL_MIC_I2C_READ:
	{
		MIC_I2C_READ *msg = parg;

		err = ngene_command_i2c_read(dev, msg->I2CAddress >> 1,
					     msg->OutData, msg->OutLength,
					     msg->OutData, msg->InLength, 1);
		break;
	}

	case IOCTL_MIC_I2C_WRITE:
	{
		MIC_I2C_WRITE *msg = parg;

		err = ngene_command_i2c_write(dev, msg->I2CAddress >> 1,
					      msg->Data, msg->Length);
		break;
	}

	case IOCTL_MIC_TEST_GETMEM:
	{
		MIC_MEM *m = parg;

		if (m->Length > 64 * 1024 || m->Start + m->Length > 64 * 1024)
			return -EINVAL;

		/* WARNING, only use this on x86,
		   other archs may not swallow this  */
		err = copy_to_user(m->Data, dev->iomem + m->Start, m->Length);
		break;
	}

	case IOCTL_MIC_TEST_SETMEM:
	{
		MIC_MEM *m = parg;

		if (m->Length > 64 * 1024 || m->Start + m->Length > 64 * 1024)
			return -EINVAL;

		err = copy_from_user(dev->iomem + m->Start, m->Data, m->Length);
		break;
	}

	case IOCTL_MIC_SFR_READ:
	{
		MIC_IMEM *m = parg;

		err = ngene_command_imem_read(dev, m->Address, &m->Data, 1);
		break;
	}

	case IOCTL_MIC_SFR_WRITE:
	{
		MIC_IMEM *m = parg;

		err = ngene_command_imem_write(dev, m->Address, m->Data, 1);
		break;
	}

	case IOCTL_MIC_IRAM_READ:
	{
		MIC_IMEM *m = parg;

		err = ngene_command_imem_read(dev, m->Address, &m->Data, 0);
		break;
	}

	case IOCTL_MIC_IRAM_WRITE:
	{
		MIC_IMEM *m = parg;

		err = ngene_command_imem_write(dev, m->Address, m->Data, 0);
		break;
	}

	case IOCTL_MIC_STREAM_CONTROL:
	{
		MIC_STREAM_CONTROL *m = parg;

		err = ngene_stream_control(dev, m->Stream, m->Control, m->Mode,
					   m->nLines, m->nBytesPerLine,
					   m->nVBILines, m->nBytesPerVBILine);
		break;
	}

	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static int command_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	void *parg = (void *)arg, *pbuf = NULL;
	char  buf[64];
	int   res = -EFAULT;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		parg = buf;
		if (_IOC_SIZE(cmd) > sizeof(buf)) {
			pbuf = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
			if (!pbuf)
				return -ENOMEM;
			parg = pbuf;
		}
		if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
			goto error;
	}
	res = command_do_ioctl(inode, file, cmd, parg);
	if (res < 0)
		goto error;
	if (_IOC_DIR(cmd) & _IOC_READ)
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			res = -EFAULT;
error:
	kfree(pbuf);
	return res;
}

struct page *ngene_nopage(struct vm_area_struct *vma,
			  unsigned long address, int *type)
{
	return 0;
}

static int ngene_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;

	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long padr = pci_resource_start(dev->pci_dev, 0) + off;
	unsigned long psize = pci_resource_len(dev->pci_dev, 0) - off;

	if (size > psize)
		return -EINVAL;

	if (io_remap_pfn_range(vma, vma->vm_start, padr >> PAGE_SHIFT, size,
			       vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static int write_uart(struct ngene *dev, u8 *data, int len)
{
	struct ngene_command com;

	com.cmd.hdr.Opcode = CMD_WRITE_UART;
	com.cmd.hdr.Length = len;
	memcpy(com.cmd.WriteUart.Data, data, len);
	com.cmd.WriteUart.Data[len] = 0;
	com.cmd.WriteUart.Data[len + 1] = 0;
	com.in_len = len;
	com.out_len = 0;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	return 0;
}

static int send_cli(struct ngene *dev, char *cmd)
{
	/* printk(KERN_INFO DEVICE_NAME ": %s", cmd); */
	return write_uart(dev, cmd, strlen(cmd));
}

static int send_cli_val(struct ngene *dev, char *cmd, u32 val)
{
	char s[32];

	snprintf(s, 32, "%s %d\n", cmd, val);
	/* printk(KERN_INFO DEVICE_NAME ": %s", s); */
	return write_uart(dev, s, strlen(s));
}

static int ngene_command_write_uart_user(struct ngene *dev,
					 const u8 *data, int len)
{
	struct ngene_command com;

	dev->tx_busy = 1;
	com.cmd.hdr.Opcode = CMD_WRITE_UART;
	com.cmd.hdr.Length = len;

	if (copy_from_user(com.cmd.WriteUart.Data, data, len))
		return -EFAULT;
	com.in_len = len;
	com.out_len = 0;

	if (ngene_command(dev, &com) < 0)
		return -EIO;

	return 0;
}

static ssize_t uart_write(struct file *file, const char *buf,
			  size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	int len, ret = 0;
	size_t left = count;

	while (left) {
		len = left;
		if (len > 250)
			len = 250;
		ret = wait_event_interruptible(dev->tx_wq, dev->tx_busy == 0);
		if (ret < 0)
			return ret;
		ngene_command_write_uart_user(dev, buf, len);
		left -= len;
		buf += len;
	}
	return count;
}

static ssize_t ts_write(struct file *file, const char *buf,
			size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;

	if (wait_event_interruptible(dev->tsout_rbuf.queue,
				     dvb_ringbuffer_free
				     (&dev->tsout_rbuf) >= count) < 0)
		return 0;

	dvb_ringbuffer_write(&dev->tsout_rbuf, buf, count);

	return count;
}

static ssize_t uart_read(struct file *file, char *buf,
			 size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	int left;
	int wp, rp, avail, len;

	if (!dev->uart_rbuf)
		return -EINVAL;
	if (count > 128)
		count = 128;
	left = count;
	while (left) {
		if (wait_event_interruptible(dev->rx_wq,
					     dev->uart_wp != dev->uart_rp) < 0)
			return -EAGAIN;
		wp = dev->uart_wp;
		rp = dev->uart_rp;
		avail = (wp - rp);

		if (avail < 0)
			avail += UART_RBUF_LEN;
		if (avail > left)
			avail = left;
		if (wp < rp) {
			len = UART_RBUF_LEN - rp;
			if (len > avail)
				len = avail;
			if (copy_to_user(buf, dev->uart_rbuf + rp, len))
				return -EFAULT;
			if (len < avail)
				if (copy_to_user(buf + len, dev->uart_rbuf,
						 avail - len))
					return -EFAULT;
		} else {
			if (copy_to_user(buf, dev->uart_rbuf + rp, avail))
				return -EFAULT;
		}
		dev->uart_rp = (rp + avail) % UART_RBUF_LEN;
		left -= avail;
		buf += avail;
	}
	return count;
}

static const struct file_operations command_fops = {
	.owner   = THIS_MODULE,
	.read    = uart_read,
	.write   = ts_write,
	.ioctl   = command_ioctl,
	.open    = dvb_generic_open,
	.release = dvb_generic_release,
	.poll    = 0,
	.mmap    = ngene_mmap,
};

static struct dvb_device dvbdev_command = {
	.priv    = 0,
	.readers = -1,
	.writers = -1,
	.users   = -1,
	.fops    = &command_fops,
};

#endif

/****************************************************************************/
/* DVB functions and API interface ******************************************/
/****************************************************************************/

static void swap_buffer(u32 *p, u32 len)
{
	while (len) {
		*p = swab32(*p);
		p++;
		len -= 4;
	}
}

static void *ain_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags)
{
	struct ngene_channel *chan = priv;
	struct ngene *dev = chan->dev;

	if (dvb_ringbuffer_free(&dev->ain_rbuf) >= len) {
		dvb_ringbuffer_write(&dev->ain_rbuf, buf, len);
		wake_up_interruptible(&dev->ain_rbuf.queue);
	} else
		printk(KERN_INFO DEVICE_NAME ": Dropped ain packet.\n");

	return 0;
}

static void *vcap_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags)
{

	struct ngene_channel *chan = priv;
	struct ngene *dev = chan->dev;

	if (len >= 1920 * 1080)
		len = 1920 * 1080;
	if (dvb_ringbuffer_free(&dev->vin_rbuf) >= len) {
		dvb_ringbuffer_write(&dev->vin_rbuf, buf, len);
		wake_up_interruptible(&dev->vin_rbuf.queue);
	} else {
		;/*printk(KERN_INFO DEVICE_NAME ": Dropped vcap packet.\n"); */
	}
	return 0;
}

static void *tsin_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags)
{
	struct ngene_channel *chan = priv;


	dvb_dmx_swfilter(&chan->demux, buf, len);
	return 0;
}

u8 fill_ts[188] = { 0x47, 0x1f, 0xff, 0x10 };

static void *tsout_exchange(void *priv, void *buf, u32 len,
			    u32 clock, u32 flags)
{
	struct ngene_channel *chan = priv;
	struct ngene *dev = chan->dev;
	u32 alen;

	alen = dvb_ringbuffer_avail(&dev->tsout_rbuf);
	alen -= alen % 188;

	if (alen < len)
		FillTSBuffer(buf + alen, len - alen, flags);
	else
		alen = len;
	dvb_ringbuffer_read(&dev->tsout_rbuf, buf, alen);
	if (flags & DF_SWAP32)
		swap_buffer((u32 *)buf, alen);
	wake_up_interruptible(&dev->tsout_rbuf.queue);
	return buf;
}

static void set_dto(struct ngene_channel *chan, u32 rate)
{
	u64 val = rate * 0x89705f41ULL; /* times val for 2^26 Hz */

	val = ((val >> 25) + 1) >> 1;
	chan->AudioDTOValue = (u32) val;
	/* chan->AudioDTOUpdated=1; */
	/* printk(KERN_INFO DEVICE_NAME ": Setting DTO to %08x\n", val); */
}

static void set_transfer(struct ngene_channel *chan, int state)
{
	u8 control = 0, mode = 0, flags = 0;
	struct ngene *dev = chan->dev;
	int ret;

	/*
	if (chan->running)
		return;
	*/

	/*
	printk(KERN_INFO DEVICE_NAME ": st %d\n", state);
	msleep(100);
	*/

	if (state) {
		if (chan->running) {
			printk(KERN_INFO DEVICE_NAME ": already running\n");
			return;
		}
	} else {
		if (!chan->running) {
			printk(KERN_INFO DEVICE_NAME ": already stopped\n");
			return;
		}
	}

	if (dev->card_info->switch_ctrl)
		dev->card_info->switch_ctrl(chan, 1, state ^ 1);

	if (state) {
		spin_lock_irq(&chan->state_lock);

		/* printk(KERN_INFO DEVICE_NAME ": lock=%08x\n",
			  ngreadl(0x9310)); */
		my_dvb_ringbuffer_flush(&dev->tsout_rbuf);
		control = 0x80;
		if (chan->mode & (NGENE_IO_TSIN | NGENE_IO_TSOUT)) {
			chan->Capture1Length = 512 * 188;
			mode = SMODE_TRANSPORT_STREAM;
		}
		if (chan->mode & NGENE_IO_TSOUT) {
			chan->pBufferExchange = tsout_exchange;
			/* 0x66666666 = 50MHz *2^33 /250MHz */
			chan->AudioDTOValue = 0x66666666;
			/* set_dto(chan, 38810700+1000); */
			/* set_dto(chan, 19392658); */
		}
		if (chan->mode & NGENE_IO_TSIN)
			chan->pBufferExchange = tsin_exchange;
		/* ngwritel(0, 0x9310); */
		spin_unlock_irq(&chan->state_lock);
	} else
		;/* printk(KERN_INFO DEVICE_NAME ": lock=%08x\n",
			   ngreadl(0x9310)); */

	ret = ngene_command_stream_control(dev, chan->number,
					   control, mode, flags);
	if (!ret)
		chan->running = state;
	else
		printk(KERN_ERR DEVICE_NAME ": set_transfer %d failed\n",
		       state);
	if (!state) {
		spin_lock_irq(&chan->state_lock);
		chan->pBufferExchange = 0;
		my_dvb_ringbuffer_flush(&dev->tsout_rbuf);
		spin_unlock_irq(&chan->state_lock);
	}
}

static int ngene_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct ngene_channel *chan = dvbdmx->priv;
	struct ngene *dev = chan->dev;

	if (dev->card_info->io_type[chan->number] & NGENE_IO_TSOUT) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
			send_cli_val(dev, "vpid", dvbdmxfeed->pid);
			send_cli(dev, "res 1080i50\n");
			/* send_cli(dev, "vdec mpeg2\n"); */
			break;

		case DMX_TS_PES_AUDIO:
			send_cli_val(dev, "apid", dvbdmxfeed->pid);
			send_cli(dev, "start\n");
			break;

		case DMX_TS_PES_PCR:
			send_cli_val(dev, "pcrpid", dvbdmxfeed->pid);
			break;

		default:
			break;
		}

	}

	if (chan->users == 0) {
		set_transfer(chan, 1);
		/* msleep(10); */
	}

	return ++chan->users;
}

static int ngene_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct ngene_channel *chan = dvbdmx->priv;
	struct ngene *dev = chan->dev;

	if (dev->card_info->io_type[chan->number] & NGENE_IO_TSOUT) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
			send_cli(dev, "stop\n");
			break;

		case DMX_TS_PES_AUDIO:
			break;

		case DMX_TS_PES_PCR:
			break;

		default:
			break;
		}

	}

	if (--chan->users)
		return chan->users;

	set_transfer(chan, 0);

	return 0;
}

static int write_demod(struct i2c_adapter *adapter, u8 adr, u16 reg, u16 data)
{
	u8 mm[5] = { 0x10, (reg >> 8) & 0xff, reg & 0xff,
		    (data >> 8) & 0xff, data & 0xff};
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = mm, .len = 5 };

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		printk(KERN_ERR DEVICE_NAME ": error in write_demod\n");
		return -1;
	}
	return 0;
}


static int ngene_drxd_pll_set(struct ngene_channel *chan,
			      u8 *pll, u8 *aux, u8 plladr)
{
	struct i2c_adapter *adap = &chan->i2c_adapter;
	struct i2c_msg msg_pll = {.addr = plladr, .flags = 0, .buf = pll,
				  .len = 4};
	struct i2c_msg msg_aux = {.addr = plladr, .flags = 0, .buf = aux,
				  .len = 2};
	int err = 0;

	if (chan->dev->card_info->i2c_access & 1)
		down(&chan->dev->pll_mutex);

	chan->fe->ops.i2c_gate_ctrl(chan->fe, 1);
	err = i2c_transfer(adap, &msg_pll, 1);
	if (err != 1)
		goto error;
	if (aux)
		err = i2c_transfer(adap, &msg_aux, 1);
error:
	chan->fe->ops.i2c_gate_ctrl(chan->fe, 0);
	if (chan->dev->card_info->i2c_access & 1)
		up(&chan->dev->pll_mutex);
	return err;
}

static int ngene_pll_set_th_dtt7520x(void *priv, void *priv_params,
				     u8 plladr, u8 dadr, s32 *off)
{
	struct dvb_frontend_parameters *params = priv_params;
	struct ngene_channel *chan = priv;

	u32 freq = params->frequency;
	u8 pll[4], aux[2];
	u8 c1, c2;
	u32 div;

	if (freq < 185000000 || freq > 900000000)
		return -EINVAL;

	if (freq < 465000000)
		c2 = 0x12;
	else
		c2 = 0x18;

	if (freq < 305000000)
		c1 = 0xb4;
	else if (freq < 405000000)
		c1 = 0xbc;
	else if (freq < 445000000)
		c1 = 0xf4;
	else if (freq < 465000000)
		c1 = 0xfc;
	else if (freq < 735000000)
		c1 = 0xbc;
	else if (freq < 835000000)
		c1 = 0xf4;
	else
		c1 = 0xfc;

	if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ)
		c2 ^= 0x10;

	div    = (freq + 36000000 + 166667 / 2) / 166667;
	*off   = ((s32) div) * 166667 - (s32) freq - 36000000;

	pll[0] = (div >> 8) & 0x7f;
	pll[1] = div & 0xff;
	pll[2] = c1;
	pll[3] = c2;

	aux[0] = (c1 & 0xc7) | 0x98;
	aux[1] = 0x30;

	return ngene_drxd_pll_set(chan, pll, aux, plladr);
}

static int ngene_pll_set_mt_3x0823(void *priv,
				   void *priv_params,
				   u8 plladr, u8 dadr, s32 *off)
{
	struct dvb_frontend_parameters *params = priv_params;
	struct ngene_channel *chan = priv;
	struct i2c_adapter *adap = &chan->i2c_adapter;
	u32 freq = params->frequency;
	u8 pll[4];
	u8 aux[2];
	u8 c1, c2;
	u32 div;

	if (freq < 47125000 || freq > 855250000)
		return -EINVAL;
	else if (freq < 120000000) {
		c1 = 0xcc;
		c2 = 0x01;
	} else if (freq < 155500000) {
		c1 = 0xfc;
		c2 = 0x01;
	} else if (freq < 300000000) {
		c1 = 0xbc;
		c2 = 0x02;
	} else if (freq < 467000000) {
		c1 = 0xcc;
		c2 = 0x02;
	} else {
		c1 = 0xcc;
		c2 = 0x04;
	}

	if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ)
		c2 |= 0x08;

#define INTERFREQ (36000000)

	div = (freq + INTERFREQ + 166667 / 2) / 166667;

	*off = ((s32) div) * 166667 - (s32) freq - INTERFREQ;

	pll[0] = (div >> 8) & 0x7f;
	pll[1] = div & 0xff;
	pll[2] = c1;
	pll[3] = c2;

	aux[0] = (c1 & 0xc7) | 0x98;
	aux[1] = 0x20;

	write_demod(adap, dadr, 0x1007, 0xc27);

	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_7_MHZ:
		write_demod(adap, dadr, 0x0020, 0x103);
		break;
	case BANDWIDTH_AUTO:
	case BANDWIDTH_8_MHZ:
		write_demod(adap, dadr, 0x0020, 0x003);
		break;
	case BANDWIDTH_6_MHZ:
		write_demod(adap, dadr, 0x0020, 0x002);
		/*write_demod(adap, dadr, 0x1022, 397);*/
		break;
	}

	return ngene_drxd_pll_set(chan, pll, aux, plladr);

}

static s16 osc_deviation(void *priv, s16 deviation, int flag)
{
	struct ngene_channel *chan = priv;
	struct i2c_adapter *adap = &chan->i2c_adapter;
	u16 data = 0;

	if (flag) {
		data = (u16) deviation;
		printk(KERN_INFO DEVICE_NAME ": write deviation %d\n",
		       deviation);
		eeprom_write_ushort(adap, 0x1000 + chan->number, data);
	} else {
		if (eeprom_read_ushort(adap, 0x1000 + chan->number, &data))
			data = 0;
		printk(KERN_INFO DEVICE_NAME ": read deviation %d\n",
		       (s16) data);
	}

	return (s16) data;
}

static int write_to_decoder(struct dvb_demux_feed *feed,
			    const u8 *buf, size_t len)
{
	struct dvb_demux *dvbdmx = feed->demux;
	struct ngene_channel *chan = dvbdmx->priv;
	struct ngene *dev = chan->dev;

	if (wait_event_interruptible(dev->tsout_rbuf.queue,
				     dvb_ringbuffer_free
				     (&dev->tsout_rbuf) >= len) < 0)
		return 0;

	dvb_ringbuffer_write(&dev->tsout_rbuf, buf, len);

	return len;
}

static int my_dvb_dmx_ts_card_init(struct dvb_demux *dvbdemux, char *id,
				   int (*start_feed)(struct dvb_demux_feed *),
				   int (*stop_feed)(struct dvb_demux_feed *),
				   void *priv)
{
	dvbdemux->priv = priv;

	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	dvbdemux->start_feed = start_feed;
	dvbdemux->stop_feed = stop_feed;
	dvbdemux->write_to_decoder = 0;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING |
				      DMX_SECTION_FILTERING |
				      DMX_MEMORY_BASED_FILTERING);
	return dvb_dmx_init(dvbdemux);
}

static int my_dvb_dmxdev_ts_card_init(struct dmxdev *dmxdev,
				      struct dvb_demux *dvbdemux,
				      struct dmx_frontend *hw_frontend,
				      struct dmx_frontend *mem_frontend,
				      struct dvb_adapter *dvb_adapter)
{
	int ret;

	dmxdev->filternum = 256;
	dmxdev->demux = &dvbdemux->dmx;
	dmxdev->capabilities = 0;
	ret = dvb_dmxdev_init(dmxdev, dvb_adapter);
	if (ret < 0)
		return ret;

	hw_frontend->source = DMX_FRONTEND_0;
	dvbdemux->dmx.add_frontend(&dvbdemux->dmx, hw_frontend);
	mem_frontend->source = DMX_MEMORY_FE;
	dvbdemux->dmx.add_frontend(&dvbdemux->dmx, mem_frontend);
	return dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, hw_frontend);
}

/****************************************************************************/
/* Decypher firmware loading ************************************************/
/****************************************************************************/

#define DECYPHER_FW "decypher.fw"

static int dec_ts_send(struct ngene *dev, u8 *buf, u32 len)
{
	while (dvb_ringbuffer_free(&dev->tsout_rbuf) < len)
		msleep(1);


	dvb_ringbuffer_write(&dev->tsout_rbuf, buf, len);

	return len;
}

u8 dec_fw_fill_ts[188] = { 0x47, 0x09, 0x0e, 0x10, 0xff, 0xff, 0x00, 0x00 };

int dec_fw_send(struct ngene *dev, u8 *fw, u32 size)
{
	struct ngene_channel *chan = &dev->channel[4];
	u32 len = 180, cc = 0;
	u8 buf[8] = { 0x47, 0x09, 0x0e, 0x10, 0x00, 0x00, 0x00, 0x00 };

	set_transfer(chan, 1);
	msleep(100);
	while (size) {
		len = 180;
		if (len > size)
			len = size;
		buf[3] = 0x10 | (cc & 0x0f);
		buf[4] = (cc >> 8);
		buf[5] = cc & 0xff;
		buf[6] = len;

		dec_ts_send(dev, buf, 8);
		dec_ts_send(dev, fw, len);
		if (len < 180)
			dec_ts_send(dev, dec_fw_fill_ts + len + 8, 180 - len);
		cc++;
		size -= len;
		fw += len;
	}
	for (len = 0; len < 512; len++)
		dec_ts_send(dev, dec_fw_fill_ts, 188);
	while (dvb_ringbuffer_avail(&dev->tsout_rbuf))
		msleep(10);
	msleep(100);
	set_transfer(chan, 0);
	return 0;
}

int dec_fw_boot(struct ngene *dev)
{
	u32 size;
	const struct firmware *fw = NULL;
	u8 *dec_fw;
	char *fw_name;
	int err, version;

	if (request_firmware(&fw, DECYPHER_FW, &dev->pci_dev->dev) < 0) {
		printk(KERN_ERR DEVICE_NAME
		       ": %s not found. Check hotplug directory.\n",
		       DECYPHER_FW);
		return -1;
	}
	printk(KERN_INFO DEVICE_NAME ": Booting decypher firmware file %s\n",
	       DECYPHER_FW);

	size = fw->size;
	dec_fw = fw->data;
	dec_fw_send(dev, dec_fw, size);
	release_firmware(fw);
	return 0;
}

/****************************************************************************/
/* nGene hardware init and release functions ********************************/
/****************************************************************************/

void free_ringbuffer(struct ngene *dev, struct SRingBufferDescriptor *rb)
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

void free_idlebuffer(struct ngene *dev,
		     struct SRingBufferDescriptor *rb,
		     struct SRingBufferDescriptor *tb)
{
	int j;
	struct SBufferHeader *Cur = tb->Head;

	if (!rb->Head)
		return;
	free_ringbuffer(dev, rb);
	for (j = 0; j < tb->NumBuffers; j++, Cur = Cur->Next) {
		Cur->Buffer2 = 0;
		Cur->scList2 = 0;
		Cur->ngeneBuffer.Address_of_first_entry_2 = 0;
		Cur->ngeneBuffer.Number_of_entries_2 = 0;
	}
}

void free_common_buffers(struct ngene *dev)
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

int create_ring_buffer(struct pci_dev *pci_dev,
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

	descr->Head = 0;
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
	int status = 0;
	u32 SCListMemSize = pRingBuffer->NumBuffers
		* ((Buffer2Length != 0) ? (NUM_SCATTER_GATHER_ENTRIES * 2) :
		    NUM_SCATTER_GATHER_ENTRIES)
		* sizeof(struct HW_SCATTER_GATHER_ELEMENT);

	u64 PASCListMem;
	PHW_SCATTER_GATHER_ELEMENT SCListEntry;
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

	SCListEntry = (PHW_SCATTER_GATHER_ELEMENT) SCListMem;
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

	return status;
}

static int FillTSIdleBuffer(struct SRingBufferDescriptor *pIdleBuffer,
			    struct SRingBufferDescriptor *pRingBuffer)
{
	int status = 0;

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
	return status;
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

static int allocate_buffer(struct pci_dev *pci_dev, dma_addr_t of,
			   struct SRingBufferDescriptor *rbuf,
			   u32 entries, u32 size1, u32 size2)
{
	if (create_ring_buffer(pci_dev, rbuf, entries) < 0)
		return -ENOMEM;

	if (AllocateRingBuffers(pci_dev, of, rbuf, size1, size2) < 0)
		return -ENOMEM;

	return 0;
}

static int channel_allocate_buffers(struct ngene_channel *chan)
{
	struct ngene *dev = chan->dev;
	int type = dev->card_info->io_type[chan->number];
	int status;

	chan->State = KSSTATE_STOP;

	if (type & (NGENE_IO_TV | NGENE_IO_HDTV | NGENE_IO_AIN)) {
		status = create_ring_buffer(dev->pci_dev,
					    &chan->RingBuffer,
					    RingBufferSizes[chan->number]);
		if (status < 0)
			return -ENOMEM;

		if (type & (NGENE_IO_TV | NGENE_IO_AIN)) {
			status = AllocateRingBuffers(dev->pci_dev,
						     dev->PAOverflowBuffer,
						     &chan->RingBuffer,
						     Buffer1Sizes[chan->number],
						     Buffer2Sizes[chan->
								  number]);
			if (status < 0)
				return -ENOMEM;
		} else if (type & NGENE_IO_HDTV) {
			status = AllocateRingBuffers(dev->pci_dev,
						     dev->PAOverflowBuffer,
						     &chan->RingBuffer,
						     MAX_HDTV_BUFFER_SIZE, 0);
			if (status < 0)
				return -ENOMEM;
		}
	}

	if (type & (NGENE_IO_TSIN | NGENE_IO_TSOUT)) {

		status = create_ring_buffer(dev->pci_dev,
					    &chan->TSRingBuffer, RING_SIZE_TS);
		if (status < 0)
			return -ENOMEM;

		status = AllocateRingBuffers(dev->pci_dev,
					     dev->PAOverflowBuffer,
					     &chan->TSRingBuffer,
					     MAX_TS_BUFFER_SIZE, 0);
		if (status)
			return -ENOMEM;
	}

	if (type & NGENE_IO_TSOUT) {
		status = create_ring_buffer(dev->pci_dev,
					    &chan->TSIdleBuffer, 1);
		if (status < 0)
			return -ENOMEM;
		status = AllocateRingBuffers(dev->pci_dev,
					     dev->PAOverflowBuffer,
					     &chan->TSIdleBuffer,
					     MAX_TS_BUFFER_SIZE, 0);
		if (status)
			return -ENOMEM;
		FillTSIdleBuffer(&chan->TSIdleBuffer, &chan->TSRingBuffer);
	}
	return 0;
}

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

	dev->OverflowBuffer = pci_alloc_consistent(dev->pci_dev,
						   OVERFLOW_BUFFER_SIZE,
						   &dev->PAOverflowBuffer);
	if (!dev->OverflowBuffer)
		return -ENOMEM;
	memset(dev->OverflowBuffer, 0, OVERFLOW_BUFFER_SIZE);

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
	printk(KERN_INFO DEVICE_NAME ": Device version %d\n",
	       dev->device_version);
}

static int ngene_load_firm(struct ngene *dev)
{
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
		ngene_fw = FW15;
		size = sizeof(FW15);
		fw_name = "ngene_15.fw";
		break;
	case 16:
		ngene_fw = FW16;
		size = sizeof(FW16);
		fw_name = "ngene_16.fw";
		break;
	case 17:
		ngene_fw = FW17;
		size = sizeof(FW17);
		fw_name = "ngene_17.fw";
		break;
	}
#ifdef FW_INC
	if (load_firmware &&
	    request_firmware(&fw, fw_name, &dev->pci_dev->dev) >= 0) {
		printk(KERN_INFO DEVICE_NAME
		       ": Loading firmware file %s.\n", fw_name);
		size = fw->size;
		ngene_fw = fw->data;
	} else
		printk(KERN_INFO DEVICE_NAME
		       ": Loading built-in firmware version %d.\n", version);
	err = ngene_command_load_firmware(dev, ngene_fw, size);

	if (fw)
		release_firmware(fw);
#else
	if (request_firmware(&fw, fw_name, &dev->pci_dev->dev) < 0) {
		printk(KERN_ERR DEVICE_NAME
			": Could not load firmware file %s. \n", fw_name);
		printk(KERN_INFO DEVICE_NAME
			": Copy %s to your hotplug directory!\n", fw_name);
		return -1;
	}
	printk(KERN_INFO DEVICE_NAME ": Loading firmware file %s.\n", fw_name);
	size = fw->size;
	ngene_fw = fw->data;
	err = ngene_command_load_firmware(dev, ngene_fw, size);
	release_firmware(fw);
#endif
	return err;
}

static void ngene_stop(struct ngene *dev)
{
	down(&dev->cmd_mutex);
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
	sema_init(&dev->cmd_mutex, 1);
	sema_init(&dev->stream_mutex, 1);
	sema_init(&dev->pll_mutex, 1);
	sema_init(&dev->i2c_switch_mutex, 1);
	spin_lock_init(&dev->cmd_lock);
	for (i = 0; i < MAX_STREAM; i++)
		spin_lock_init(&dev->channel[i].state_lock);
	ngwritel(1, TIMESTAMPS);

	ngwritel(1, NGENE_INT_ENABLE);

	stat = ngene_load_firm(dev);
	if (stat < 0)
		goto fail;

	stat = ngene_i2c_init(dev, 0);
	if (stat < 0)
		goto fail;

	stat = ngene_i2c_init(dev, 1);
	if (stat < 0)
		goto fail;

	if (dev->card_info->fw_version == 17) {
		u8 hdtv_config[6] =
			{6144 / 64, 0, 0, 2048 / 64, 2048 / 64, 2048 / 64};
		u8 tsin4_config[6] =
			{3072 / 64, 3072 / 64, 0, 3072 / 64, 3072 / 64, 0};
		u8 ts5_config[6] =
			{2048 / 64, 2048 / 64, 0, 2048 / 64, 2048 / 64,
			 2048 / 64};
		u8 default_config[6] =
			{4096 / 64, 4096 / 64, 0, 2048 / 64, 2048 / 64, 0};
		u8 *bconf = default_config;

		if (dev->card_info->io_type[3] == NGENE_IO_TSIN)
			bconf = tsin4_config;
		if (dev->card_info->io_type[0] == NGENE_IO_HDTV) {
			bconf = hdtv_config;
			ngene_reset_decypher(dev);
		}
		printk(KERN_INFO DEVICE_NAME ": FW 17 buffer config\n");
		stat = ngene_command_config_free_buf(dev, bconf);
	} else {
		int bconf = BUFFER_CONFIG_4422;

		if (dev->card_info->io_type[0] == NGENE_IO_HDTV) {
			bconf = BUFFER_CONFIG_8022;
			ngene_reset_decypher(dev);
		}
		if (dev->card_info->io_type[3] == NGENE_IO_TSIN)
			bconf = BUFFER_CONFIG_3333;
		stat = ngene_command_config_buf(dev, bconf);
	}

	if (dev->card_info->io_type[0] == NGENE_IO_HDTV) {
		ngene_command_config_uart(dev, 0xc1, tx_cb, rx_cb);
		test_dec_i2c(&dev->channel[0].i2c_adapter, 0);
		test_dec_i2c(&dev->channel[0].i2c_adapter, 1);
	}

	return stat;
fail:
	ngwritel(0, NGENE_INT_ENABLE);
	free_irq(dev->pci_dev->irq, dev);
	return stat;
}

/****************************************************************************/
/* DVB audio/video device functions *****************************************/
/****************************************************************************/

static ssize_t audio_write(struct file *file,
			   const char *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

ssize_t audio_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	int left;
	int avail;

	left = count;
	while (left) {
		if (wait_event_interruptible(
				dev->ain_rbuf.queue,
				dvb_ringbuffer_avail(&dev->ain_rbuf) > 0) < 0)
			return -EAGAIN;
		avail = dvb_ringbuffer_avail(&dev->ain_rbuf);
		if (avail > left)
			avail = left;
		dvb_ringbuffer_read_user(&dev->ain_rbuf, buf, avail);
		left -= avail;
		buf += avail;
	}
	return count;
}

static int audio_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	struct ngene_channel *chan2 = &chan->dev->channel[2];
	int ret;

	ret = dvb_generic_open(inode, file);
	if (ret < 0)
		return ret;
	my_dvb_ringbuffer_flush(&dev->ain_rbuf);

	chan2->Capture1Length = MAX_AUDIO_BUFFER_SIZE;
	chan2->pBufferExchange = ain_exchange;
	ngene_command_stream_control(chan2->dev, chan2->number, 0x80,
				     SMODE_AUDIO_CAPTURE, 0);
	return ret;
}

static int audio_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	struct ngene_channel *chan2 = &chan->dev->channel[2];

	ngene_command_stream_control(dev, 2, 0, 0, 0);
	chan2->pBufferExchange = 0;

	return dvb_generic_release(inode, file);
}

static const struct file_operations audio_fops = {
	.owner   = THIS_MODULE,
	.read    = audio_read,
	.write   = audio_write,
	.open    = audio_open,
	.release = audio_release,
};

static struct dvb_device dvbdev_audio = {
	.priv    = 0,
	.readers = -1,
	.writers = 1,
	.users   = 1,
	.fops    = &audio_fops,
};

static int video_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	struct ngene_channel *chan0 = &chan->dev->channel[0];
	int ret;

	ret = dvb_generic_open(inode, file);
	if (ret < 0)
		return ret;
	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		return ret;
	my_dvb_ringbuffer_flush(&dev->vin_rbuf);

	chan0->nBytesPerLine = 1920 * 2;
	chan0->nLines = 540;
	chan0->Capture1Length = 1920 * 2 * 540;
	chan0->pBufferExchange = vcap_exchange;
	chan0->itumode = 2;
	ngene_command_stream_control(chan0->dev, chan0->number,
				     0x80, SMODE_VIDEO_CAPTURE, 0);
	return ret;
}

static int video_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	struct ngene_channel *chan0 = &chan->dev->channel[0];

	ngene_command_stream_control(dev, 0, 0, 0, 0);
	chan0->pBufferExchange = 0;

	return dvb_generic_release(inode, file);
}

static ssize_t video_write(struct file *file,
			   const char *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

ssize_t video_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	int left, avail;

	left = count;
	while (left) {
		if (wait_event_interruptible(
				dev->vin_rbuf.queue,
				dvb_ringbuffer_avail(&dev->vin_rbuf) > 0) < 0)
			return -EAGAIN;
		avail = dvb_ringbuffer_avail(&dev->vin_rbuf);
		if (avail > left)
			avail = left;
		dvb_ringbuffer_read_user(&dev->vin_rbuf, buf, avail);
		left -= avail;
		buf += avail;
	}
	return count;
}

/* Why is this not exported from dvb_core ?!?! */

static int dvb_usercopy2(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg,
			 int (*func)(struct inode *inode, struct file *file,
				     unsigned int cmd, void *arg))
{
	char sbuf[128];
	void *mbuf = NULL;
	void *parg = NULL;
	int  err   = -EINVAL;

	/*  Copy arguments into temp kernel buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:
		/*
		 * For this command, the pointer is actually an integer
		 * argument.
		 */
		parg = (void *)arg;
		break;
	case _IOC_READ: /* some v4l ioctls are marked wrong ... */
	case _IOC_WRITE:
	case (_IOC_WRITE | _IOC_READ):
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
			goto out;
		break;
	}

	/* call driver */
	err = func(inode, file, cmd, parg);
	if (err == -ENOIOCTLCMD)
		err = -EINVAL;

	if (err < 0)
		goto out;

	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	}

out:
	kfree(mbuf);
	return err;
}

static int video_do_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	int ret = 0;
	unsigned long arg = (unsigned long)parg;

	switch (cmd) {
	case VIDEO_SET_STREAMTYPE:
		switch (arg) {
		case VIDEO_CAP_MPEG2:
			/* printk(KERN_INFO DEVICE_NAME ": setting MPEG2\n"); */
			send_cli(dev, "vdec mpeg2\n");
			break;
		case VIDEO_CAP_AVC:
			/* printk(KERN_INFO DEVICE_NAME ": setting H264\n"); */
			send_cli(dev, "vdec h264\n");
			break;
		case VIDEO_CAP_VC1:
			/* printk(KERN_INFO DEVICE_NAME ": setting VC1\n"); */
			send_cli(dev, "vdec vc1\n");
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		return -EINVAL;
	}
	return ret;
}

static int video_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy2(inode, file, cmd, arg, video_do_ioctl);
}

static const struct file_operations video_fops = {
	.owner   = THIS_MODULE,
	.read    = video_read,
	.write   = video_write,
	.open    = video_open,
	.release = video_release,
	.ioctl   = video_ioctl,
};

static struct dvb_device dvbdev_video = {
	.priv    = 0,
	.readers = -1,
	.writers = 1,
	.users   = -1,
	.fops    = &video_fops,
};

/****************************************************************************/
/* LNBH21 *******************************************************************/
/****************************************************************************/

static int lnbh21_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct ngene_channel *chan =
		*(struct ngene_channel **) fe->demodulator_priv;

	switch (voltage) {
	case SEC_VOLTAGE_OFF:
		chan->lnbh &= 0xf3;
		break;
	case SEC_VOLTAGE_13:
		chan->lnbh |= 0x04;
		chan->lnbh &= ~0x08;
		break;
	case SEC_VOLTAGE_18:
		chan->lnbh |= 0x0c;
		break;
	default:
		return -EINVAL;
	};
	chan->lnbh |= 0x10;
	return i2c_write(&chan->i2c_adapter,
			 chan->dev->card_info->lnb[chan->number], chan->lnbh);
}

static int lnbh21_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct ngene_channel *chan =
		*(struct ngene_channel **)fe->demodulator_priv;

	switch (tone) {
	case SEC_TONE_ON:
		chan->lnbh |= 0x20;
		break;
	case SEC_TONE_OFF:
		chan->lnbh &= 0xdf;
		break;
	default:
		return -EINVAL;
	}
	return i2c_write(&chan->i2c_adapter,
			 chan->dev->card_info->lnb[chan->number], chan->lnbh);
}

/****************************************************************************/
/* Switch control (I2C gates, etc.) *****************************************/
/****************************************************************************/

static int avf_output(struct ngene_channel *chan, int state)
{
	if (chan->dev->card_info->avf[chan->number])
		i2c_write_register(&chan->i2c_adapter,
				   chan->dev->card_info->avf[chan->number],
				   0xf2, state ? 0x89 : 0x80);
	return 0;
}

/* Viper expander: sw11,sw12,sw21,sw22,i2csw1,i2csw2,tsen1,tsen2 */

static int exp_set(struct ngene *dev)
{
	return i2c_write(&dev->channel[0].i2c_adapter,
			 dev->card_info->exp, dev->exp_val);
}

static int exp_init(struct ngene *dev)
{
	if (!dev->card_info->exp)
		return 0;
	dev->exp_val = dev->card_info->exp_init;
	return exp_set(dev);
}

static int exp_set_bit(struct ngene *dev, int bit, int val)
{
	if (val)
		set_bit(bit, &dev->exp_val);
	else
		clear_bit(bit, &dev->exp_val);
	return exp_set(dev);
}

static int viper_switch_ctrl(struct ngene_channel *chan, int type, int val)
{
	switch (type) {
	case 0: /* I2C tuner gate on/off */
		return exp_set_bit(chan->dev, 4 + chan->number, val);
	case 1: /* Stream: 0=TS 1=ITU */
		avf_output(chan, val);
		return exp_set_bit(chan->dev, 6 + chan->number, val);
	case 2: /* Input: 0=digital 1=analog antenna input */
		exp_set_bit(chan->dev, 0 + chan->number * 2, val ? 0 : 1);
		exp_set_bit(chan->dev, 1 + chan->number * 2, val ? 1 : 0);
		break;
	}
	return 0;
}

static int viper_switch_ctrl2(struct ngene_channel *chan, int type, int val)
{
	switch (type) {
	case 0: /* I2C tuner gate on/off */
		return exp_set_bit(chan->dev, 4 + chan->number, val);
	case 1: /* Stream: 0=TS 1=ITU */
		avf_output(chan, val);
		return exp_set_bit(chan->dev, 6 + chan->number, val);
	case 2: /* Input: 0=digital 1=analog antenna input */
		exp_set_bit(chan->dev, 0 + chan->number * 2, val ? 0 : 1);
		exp_set_bit(chan->dev, 1 + chan->number * 2, 0);
		break;
	}
	return 0;
}

static int viper_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	/* Well, just abuse sec :-) */
	struct ngene_channel *chan = fe->sec_priv;
	struct ngene *dev = chan->dev;

	return dev->card_info->switch_ctrl(chan, 0, enable);
}

static int python_switch_ctrl(struct ngene_channel *chan, int type, int val)
{
	switch (type) {
	case 0: /* I2C tuner gate on/off */
		if (chan->number > 1)
			return -EINVAL;
		return ngene_command_gpio_set(chan->dev, 3 + chan->number, val);
	case 1: /* Stream: 0=TS 1=ITU */
		avf_output(chan, val);
		return 0;
	}
	return 0;
}

static int viper_reset_xc(struct dvb_frontend *fe)
{
	struct ngene_channel *chan = fe->sec_priv;
	struct ngene *dev = chan->dev;

	printk(KERN_INFO DEVICE_NAME ": Reset XC3028\n");

	if (chan->number > 1)
		return -EINVAL;

	ngene_command_gpio_set(dev, 3 + chan->number, 0);
	msleep(150);
	ngene_command_gpio_set(dev, 3 + chan->number, 1);
	return 0;
}

static int python_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct ngene_channel *chan = fe->sec_priv;
	struct ngene *dev = chan->dev;

	if (chan->number == 0)
		return ngene_command_gpio_set(dev, 3, enable);
	if (chan->number == 1)
		return ngene_command_gpio_set(dev, 4, enable);
	return -EINVAL;
}

/****************************************************************************/
/* Demod/tuner attachment ***************************************************/
/****************************************************************************/

static int tuner_attach_mt2060(struct ngene_channel *chan)
{
	struct ngene *dev = chan->dev;
	void *tconf = dev->card_info->tuner_config[chan->number];
	u8 drxa = dev->card_info->demoda[chan->number];
	struct dvb_frontend *fe = chan->fe, *fe2;

	fe->sec_priv = chan;
	fe->ops.i2c_gate_ctrl = dev->card_info->gate_ctrl;

	dev->card_info->gate_ctrl(fe, 1);
	fe2 = mt2060_attach(fe, &chan->i2c_adapter, tconf, 1220);
	dev->card_info->gate_ctrl(fe, 0);

	i2c_write_register(&chan->i2c_adapter, drxa, 3, 4);
	write_demod(&chan->i2c_adapter, drxa, 0x1012, 15);
	write_demod(&chan->i2c_adapter, drxa, 0x1007, 0xc27);
	write_demod(&chan->i2c_adapter, drxa, 0x0020, 0x003);

	return fe2 ? 0 : -ENODEV;
}

static int tuner_attach_xc3028(struct ngene_channel *chan)
{
	struct ngene *dev = chan->dev;
	void *tconf = dev->card_info->tuner_config[chan->number];
	struct dvb_frontend *fe = chan->fe, *fe2;

	fe->sec_priv = chan;
	fe->ops.i2c_gate_ctrl = dev->card_info->gate_ctrl;

	dev->card_info->gate_ctrl(fe, 1);
	fe2 = xc3028_attach(fe, &chan->i2c_adapter, tconf);
	dev->card_info->gate_ctrl(fe, 0);

	/*chan->fe->ops.tuner_ops.set_frequency(chan->fe,231250000);*/

	return fe2 ? 0 : -ENODEV;
}

static int demod_attach_drxd(struct ngene_channel *chan)
{
	void *feconf = chan->dev->card_info->fe_config[chan->number];

	chan->fe = drxd_attach(feconf,
			       chan, &chan->i2c_adapter,
			       &chan->dev->pci_dev->dev);
	return (chan->fe) ? 0 : -ENODEV;
}

static int demod_attach_drxh(struct ngene_channel *chan)
{
	void *feconf = chan->dev->card_info->fe_config[chan->number];

	chan->fe = drxh_attach(feconf, chan,
			       &chan->i2c_adapter, &chan->dev->pci_dev->dev);
	return (chan->fe) ? 0 : -ENODEV;
}

static int demod_attach_stb0899(struct ngene_channel *chan)
{
	void *feconf = chan->dev->card_info->fe_config[chan->number];

	chan->fe = stb0899_attach(feconf,
				  chan, &chan->i2c_adapter,
				  &chan->dev->pci_dev->dev);
	if (chan->fe) {
		chan->set_tone = chan->fe->ops.set_tone;
		chan->fe->ops.set_tone = lnbh21_set_tone;
		chan->fe->ops.set_voltage = lnbh21_set_voltage;
	}

	return (chan->fe) ? 0 : -ENODEV;
}

static int demod_attach_stv0900(struct ngene_channel *chan)
{
	void *feconf = chan->dev->card_info->fe_config[chan->number];

	chan->fe = stv0900_attach(feconf,
				  chan, &chan->i2c_adapter,
				  &chan->dev->pci_dev->dev);

	if (chan->fe) {
		chan->set_tone = chan->fe->ops.set_tone;
		chan->fe->ops.set_tone = lnbh21_set_tone;
		chan->fe->ops.set_voltage = lnbh21_set_voltage;
	}

	return (chan->fe) ? 0 : -ENODEV;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void release_channel(struct ngene_channel *chan)
{
	struct dvb_demux *dvbdemux = &chan->demux;
	struct ngene *dev = chan->dev;
	struct ngene_info *ni = dev->card_info;
	int io = ni->io_type[chan->number];

	tasklet_kill(&chan->demux_tasklet);

	if (io & (NGENE_IO_TSIN | NGENE_IO_TSOUT)) {
#ifdef NGENE_COMMAND_API
		if (chan->command_dev)
			dvb_unregister_device(chan->command_dev);
#endif
		if (chan->audio_dev)
			dvb_unregister_device(chan->audio_dev);
		if (chan->video_dev)
			dvb_unregister_device(chan->video_dev);
		if (chan->fe) {
			dvb_unregister_frontend(chan->fe);
			/*dvb_frontend_detach(chan->fe); */
			chan->fe = 0;
		}
		dvbdemux->dmx.close(&dvbdemux->dmx);
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx,
					      &chan->hw_frontend);
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx,
					      &chan->mem_frontend);
		dvb_dmxdev_release(&chan->dmxdev);
		dvb_dmx_release(&chan->demux);
#ifndef ONE_ADAPTER
		dvb_unregister_adapter(&chan->dvb_adapter);
#endif
	}

	if (io & (NGENE_IO_AIN)) {
		ngene_snd_exit(chan);
		kfree(chan->soundbuffer);
	}
}

static int init_channel(struct ngene_channel *chan)
{
	int ret = 0, nr = chan->number;
	struct dvb_adapter *adapter = 0;
	struct dvb_demux *dvbdemux = &chan->demux;
	struct ngene *dev = chan->dev;
	struct ngene_info *ni = dev->card_info;
	int io = ni->io_type[nr];

	tasklet_init(&chan->demux_tasklet, demux_tasklet, (unsigned long)chan);
	chan->users = 0;
	chan->type = io;
	chan->mode = chan->type;	/* for now only one mode */

	if (io & (NGENE_IO_TSIN | NGENE_IO_TSOUT)) {
		if (nr >= STREAM_AUDIOIN1)
			chan->DataFormatFlags = DF_SWAP32;

		if (io & NGENE_IO_TSOUT)
			dec_fw_boot(dev);

#ifdef ONE_ADAPTER
		adapter = &chan->dev->dvb_adapter;
#else
		ret = dvb_register_adapter(&chan->dvb_adapter, "nGene",
					   THIS_MODULE,
					   &chan->dev->pci_dev->dev);
		if (ret < 0)
			return ret;
		adapter = &chan->dvb_adapter;
#endif
		ret = my_dvb_dmx_ts_card_init(dvbdemux, "SW demux",
					      ngene_start_feed,
					      ngene_stop_feed, chan);
		ret = my_dvb_dmxdev_ts_card_init(&chan->dmxdev, &chan->demux,
						 &chan->hw_frontend,
						 &chan->mem_frontend, adapter);
		if (io & NGENE_IO_TSOUT) {
			dvbdemux->write_to_decoder = write_to_decoder;
			dvb_register_device(adapter, &chan->audio_dev,
					    &dvbdev_audio, (void *)chan,
					    DVB_DEVICE_AUDIO);
			dvb_register_device(adapter, &chan->video_dev,
					    &dvbdev_video, (void *)chan,
					    DVB_DEVICE_VIDEO);

		}
#ifdef NGENE_COMMAND_API
		dvb_register_device(adapter, &chan->command_dev,
				    &dvbdev_command, (void *)chan,
				    DVB_DEVICE_SEC);
#endif
	}

	if (io & NGENE_IO_TSIN) {
		chan->fe = NULL;
		if (ni->demod_attach[nr])
			ni->demod_attach[nr](chan);
		if (chan->fe) {
			if (dvb_register_frontend(adapter, chan->fe) < 0) {
				if (chan->fe->ops.release)
					chan->fe->ops.release(chan->fe);
				chan->fe = NULL;
			}
		}
		if (chan->fe && ni->tuner_attach[nr])
			if (ni->tuner_attach[nr] (chan) < 0) {
				printk(KERN_ERR DEVICE_NAME
				       ": Tuner attach failed on channel %d!\n",
				       nr);
			}
	}

	if (io & (NGENE_IO_AIN)) {
		ngene_snd_init(chan);
#ifdef NGENE_V4L
		spin_lock_init(&chan->s_lock);
		init_MUTEX(&chan->reslock);
		INIT_LIST_HEAD(&chan->capture);
#endif

		chan->soundbuffer = kmalloc(MAX_AUDIO_BUFFER_SIZE, GFP_KERNEL);
		if (!chan->soundbuffer)
			return -ENOMEM;
		memset(chan->soundbuffer, 0, MAX_AUDIO_BUFFER_SIZE);
	}
	return ret;
}

static int init_channels(struct ngene *dev)
{
	int i, j;

	for (i = 0; i < MAX_STREAM; i++) {
		if (init_channel(&dev->channel[i]) < 0) {
			for (j = 0; j < i; j++)
				release_channel(&dev->channel[j]);
			return -1;
		}
	}
	return 0;
}

/****************************************************************************/
/* device probe/remove calls ************************************************/
/****************************************************************************/

static void __devexit ngene_remove(struct pci_dev *pdev)
{
	struct ngene *dev = (struct ngene *)pci_get_drvdata(pdev);
	int i;

	tasklet_kill(&dev->event_tasklet);
	for (i = 0; i < MAX_STREAM; i++)
		release_channel(&dev->channel[i]);
#ifdef ONE_ADAPTER
	dvb_unregister_adapter(&dev->dvb_adapter);
#endif
	ngene_stop(dev);
	ngene_release_buffers(dev);
	pci_set_drvdata(pdev, 0);
	pci_disable_device(pdev);
}

static int __devinit ngene_probe(struct pci_dev *pci_dev,
				 const struct pci_device_id *id)
{
	struct ngene *dev;
	int stat = 0;

	if (pci_enable_device(pci_dev) < 0)
		return -ENODEV;

	dev = vmalloc(sizeof(struct ngene));
	if (dev == NULL)
		return -ENOMEM;
	memset(dev, 0, sizeof(struct ngene));

	dev->pci_dev = pci_dev;
	dev->card_info = (struct ngene_info *)id->driver_data;
	printk(KERN_INFO DEVICE_NAME ": Found %s\n", dev->card_info->name);

	pci_set_drvdata(pci_dev, dev);

	/* Alloc buffers and start nGene */
	stat = ngene_get_buffers(dev);
	if (stat < 0)
		goto fail1;
	stat = ngene_start(dev);
	if (stat < 0)
		goto fail1;

	dev->i2c_current_bus = -1;
	exp_init(dev);

	/* Disable analog TV decoder chips if present */
	if (copy_eeprom) {
		i2c_copy_eeprom(&dev->channel[0].i2c_adapter, 0x50, 0x52);
		i2c_dump_eeprom(&dev->channel[0].i2c_adapter, 0x52);
	}
	/*i2c_check_eeprom(&dev->i2c_adapter);*/

	/* Register DVB adapters and devices for both channels */
#ifdef ONE_ADAPTER
	if (dvb_register_adapter(&dev->dvb_adapter, "nGene", THIS_MODULE,
				 &dev->pci_dev->dev, adapter_nr) < 0)
		goto fail2;
#endif
	if (init_channels(dev) < 0)
		goto fail2;

	return 0;

fail2:
	ngene_stop(dev);
fail1:
	ngene_release_buffers(dev);
	pci_set_drvdata(pci_dev, 0);
	return stat;
}

/****************************************************************************/
/* Card configs *************************************************************/
/****************************************************************************/

static struct drxd_config fe_terratec_dvbt_0 = {
	.index          = 0,
	.demod_address  = 0x70,
	.demod_revision = 0xa2,
	.demoda_address = 0x00,
	.pll_address    = 0x60,
	.pll_type       = DRXD_PLL_DTT7520X,
	.clock          = 20000,
	.pll_set        = ngene_pll_set_th_dtt7520x,
	.osc_deviation  = osc_deviation,
};

static struct drxd_config fe_terratec_dvbt_1 = {
	.index          = 1,
	.demod_address  = 0x71,
	.demod_revision = 0xa2,
	.demoda_address = 0x00,
	.pll_address    = 0x60,
	.pll_type       = DRXD_PLL_DTT7520X,
	.clock          = 20000,
	.pll_set        = ngene_pll_set_th_dtt7520x,
	.osc_deviation  = osc_deviation,
};

static struct ngene_info ngene_info_terratec = {
	.type           = NGENE_TERRATEC,
	.name           = "Terratec Integra/Cinergy2400i Dual DVB-T",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_drxd, demod_attach_drxd},
	.fe_config      = {&fe_terratec_dvbt_0, &fe_terratec_dvbt_1},
	.i2c_access     = 1,
};

/****************************************************************************/

static struct mt2060_config tuner_python_0 = {
	.i2c_address    = 0x60,
	.clock_out      = 3,
	.input          = 0
};

static struct mt2060_config tuner_python_1 = {
	.i2c_address    = 0x61,
	.clock_out      = 3,
	.input          = 1
};

static struct drxd_config fe_python_0 = {
	.index          = 0,
	.demod_address  = 0x71,
	.demod_revision = 0xb1,
	.demoda_address = 0x41,
	.clock          = 16000,
	.osc_deviation  = osc_deviation,
};

static struct drxd_config fe_python_1 = {
	.index          = 1,
	.demod_address  = 0x70,
	.demod_revision = 0xb1,
	.demoda_address = 0x45,
	.clock          = 16000,
	.osc_deviation  = osc_deviation,
};

static struct ngene_info ngene_info_python = {
	.type           = NGENE_PYTHON,
	.name           = "Micronas MicPython/Hedgehog Dual DVB-T",
	.io_type        = {NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_AIN, NGENE_IO_AIN},
	.demod_attach   = {demod_attach_drxd, demod_attach_drxd},
	.tuner_attach   = {tuner_attach_mt2060, tuner_attach_mt2060},
	.fe_config      = {&fe_python_0, &fe_python_1},
	.tuner_config   = {&tuner_python_0, &tuner_python_1},
	.avf            = {0x43, 0x47},
	.msp            = {0x40, 0x42},
	.demoda         = {0x41, 0x45},
	.gate_ctrl      = python_gate_ctrl,
	.switch_ctrl    = python_switch_ctrl,
};

/****************************************************************************/

static struct drxd_config fe_appb_dvbt_0 = {
	.index          = 0,
	.demod_address  = 0x71,
	.demod_revision = 0xa2,
	.demoda_address = 0x41,
	.pll_address    = 0x63,
	.pll_type       = DRXD_PLL_MT3X0823,
	.clock          = 20000,
	.pll_set        = ngene_pll_set_mt_3x0823,
	.osc_deviation  = osc_deviation,
};

static struct drxd_config fe_appb_dvbt_1 = {
	.index          = 1,
	.demod_address  = 0x70,
	.demod_revision = 0xa2,
	.demoda_address = 0x45,
	.pll_address    = 0x60,
	.pll_type       = DRXD_PLL_MT3X0823,
	.clock          = 20000,
	.pll_set        = ngene_pll_set_mt_3x0823,
	.osc_deviation  = osc_deviation,
};

static struct ngene_info ngene_info_appboard = {
	.type           = NGENE_APP,
	.name           = "Micronas Application Board Dual DVB-T",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_drxd, demod_attach_drxd},
	.fe_config      = {&fe_appb_dvbt_0, &fe_appb_dvbt_1},
	.avf            = {0x43, 0x47},
};

static struct ngene_info ngene_info_appboard_ntsc = {
	.type           = NGENE_APP,
	.name           = "Micronas Application Board Dual DVB-T",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_drxd, demod_attach_drxd},
	.fe_config      = {&fe_appb_dvbt_0, &fe_appb_dvbt_1},
	.avf            = {0x43, 0x47},
	.ntsc           = 1,
};

/****************************************************************************/

static struct stb0899_config fe_sidewinder_0 = {
	.demod_address  = 0x68,
	.pll_address    = 0x63,
};

static struct stb0899_config fe_sidewinder_1 = {
	.demod_address  = 0x6b,
	.pll_address    = 0x60,
};

static struct ngene_info ngene_info_sidewinder = {
	.type           = NGENE_SIDEWINDER,
	.name           = "Micronas MicSquirrel/Sidewinder Dual DVB-S2",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_stb0899, demod_attach_stb0899},
	.fe_config      = {&fe_sidewinder_0, &fe_sidewinder_1},
	.lnb            = {0x0b, 0x08},
};

/****************************************************************************/
/* Yet unnamed S2 card with dual DVB-S2 demod                               */
/****************************************************************************/

static struct stv0900_config fe_s2_0 = {
	.addr           = 0x68,
	.pll            = 0x63,
	.pll_type       = 0,
	.nr             = 0,
};

static struct stv0900_config fe_s2_1 = {
	.addr           = 0x68,
	.pll            = 0x60,
	.pll_type       = 0,
	.nr             = 1,
};

static struct ngene_info ngene_info_s2 = {
	.type           = NGENE_SIDEWINDER,
	.name           = "S2",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN,
			   NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_stv0900, demod_attach_stv0900},
	.fe_config      = {&fe_s2_0, &fe_s2_1},
	.lnb            = {0x0b, 0x08},
	.tsf            = {3, 3},
	.fw_version     = 15,
};

static struct stv0900_config fe_s2b_0 = {
	.addr           = 0x68,
	.pll            = 0x60,
	.pll_type       = 0x10,
	.nr             = 0,
};

static struct stv0900_config fe_s2b_1 = {
	.addr           = 0x68,
	.pll            = 0x63,
	.pll_type       = 0x10,
	.nr             = 1,
};

static struct ngene_info ngene_info_s2_b = {
	.type           = NGENE_SIDEWINDER,
	.name           = "S2 V2",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN,
			   NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_stv0900, demod_attach_stv0900},
	.fe_config      = {&fe_s2b_0, &fe_s2b_1},
	.lnb            = {0x0b, 0x08},
	.tsf            = {3, 3},
	.fw_version     = 17,
};

/****************************************************************************/

static struct xc3028_config tuner_viper_0 = {
	.adr            = 0x61,
	.reset          = viper_reset_xc
};

static struct xc3028_config tuner_viper_1 = {
	.adr            = 0x64,
	.reset          = viper_reset_xc
};

static struct drxh_config fe_viper_h_0 = {.adr = 0x2b};

static struct drxh_config fe_viper_h_1 = {.adr = 0x29};

static struct drxh_config fe_viper_l_0 = {.adr = 0x2b, .type = 3931};

static struct drxh_config fe_viper_l_1 = {.adr = 0x29, .type = 3931};

static struct ngene_info ngene_info_viper_v1 = {
	.type           = NGENE_VIPER,
	.name           = "Micronas MicViper Dual ATSC DRXH",
	.io_type        = {NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_AIN, NGENE_IO_AIN},
	.demod_attach   = {demod_attach_drxh, demod_attach_drxh},
	.fe_config      = {&fe_viper_h_0, &fe_viper_h_1},
	.tuner_config   = {&tuner_viper_0, &tuner_viper_1},
	.tuner_attach   = {tuner_attach_xc3028, tuner_attach_xc3028},
	.avf            = {0x43, 0x47},
	.msp            = {0x40, 0x42},
	.exp            = 0x20,
	.exp_init       = 0xf5,
	.gate_ctrl      = viper_gate_ctrl,
	.switch_ctrl    = viper_switch_ctrl,
	.tsf            = {2, 2},
};

static struct ngene_info ngene_info_viper_v2 = {
	.type           = NGENE_VIPER,
	.name           = "Micronas MicViper Dual ATSC DRXL",
	.io_type        = {NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_AIN, NGENE_IO_AIN},
	.demod_attach   = {demod_attach_drxh, demod_attach_drxh},
	.fe_config      = {&fe_viper_l_0, &fe_viper_l_1},
	.tuner_config   = {&tuner_viper_0, &tuner_viper_1},
	.tuner_attach   = {tuner_attach_xc3028, tuner_attach_xc3028},
	.avf            = {0x43, 0x47},
	.msp            = {0x40, 0x42},
	.exp            = 0x38,
	.exp_init       = 0xf5,
	.gate_ctrl      = viper_gate_ctrl,
	.switch_ctrl    = viper_switch_ctrl,
	.tsf            = {2, 2},
};

/****************************************************************************/

static struct ngene_info ngene_info_vbox_v1 = {
	.type           = NGENE_VBOX_V1,
	.name           = "VBox Cat's Eye 164E",
	.io_type        = {NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_AIN, NGENE_IO_AIN},
	.demod_attach   = {demod_attach_drxh, demod_attach_drxh},
	.fe_config      = {&fe_viper_h_0, &fe_viper_h_1},
	.tuner_config   = {&tuner_viper_0, &tuner_viper_1},
	.tuner_attach   = {tuner_attach_xc3028, tuner_attach_xc3028},
	.avf            = {0x43, 0x47},
	.msp            = {0x40, 0x42},
	.exp            = 0x20,
	.exp_init       = 0xf5,
	.gate_ctrl      = viper_gate_ctrl,
	.switch_ctrl    = viper_switch_ctrl,
	.tsf            = {2, 2},
};

/****************************************************************************/

static struct ngene_info ngene_info_vbox_v2 = {
	.type           = NGENE_VBOX_V2,
	.name           = "VBox Cat's Eye 164E",
	.io_type        = {NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_TSIN | NGENE_IO_TV,
			   NGENE_IO_AIN, NGENE_IO_AIN},
	.demod_attach   = {demod_attach_drxh, demod_attach_drxh},
	.fe_config      = {&fe_viper_h_0, &fe_viper_h_1},
	.tuner_config   = {&tuner_viper_0, &tuner_viper_1},
	.tuner_attach   = {tuner_attach_xc3028, tuner_attach_xc3028},
	.avf            = {0x43, 0x47},
	.msp            = {0x40, 0x42},
	.exp            = 0x20,
	.exp_init       = 0xf5,
	.gate_ctrl      = viper_gate_ctrl,
	.switch_ctrl    = viper_switch_ctrl2,
	.tsf            = {2, 2},
};

/****************************************************************************/

static struct ngene_info ngene_info_racer = {
	.type           = NGENE_RACER,
	.name           = "Micronas MicRacer HDTV Decoder Card",
	.io_type        = {NGENE_IO_HDTV, NGENE_IO_NONE,
			   NGENE_IO_AIN, NGENE_IO_NONE,
			   NGENE_IO_TSOUT},
	.i2s            = {0, 0, 1, 0},
	.fw_version     = 17,
};


/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

#define NGENE_ID(_subvend, _subdev, _driverdata) { \
	.vendor = NGENE_VID, .device = NGENE_PID, \
	.subvendor = _subvend, .subdevice = _subdev, \
	.driver_data = (unsigned long) &_driverdata }

/****************************************************************************/

static const struct pci_device_id ngene_id_tbl[] __devinitdata = {
	NGENE_ID(0x18c3, 0x0000, ngene_info_appboard),
	NGENE_ID(0x18c3, 0x0004, ngene_info_appboard),
	NGENE_ID(0x18c3, 0x8011, ngene_info_appboard),
	NGENE_ID(0x18c3, 0x8015, ngene_info_appboard_ntsc),
	NGENE_ID(0x153b, 0x1167, ngene_info_terratec),
	NGENE_ID(0x18c3, 0x0030, ngene_info_python),
	NGENE_ID(0x18c3, 0x0052, ngene_info_sidewinder),
	NGENE_ID(0x18c3, 0x8f00, ngene_info_racer),
	NGENE_ID(0x18c3, 0x0041, ngene_info_viper_v1),
	NGENE_ID(0x18c3, 0x0042, ngene_info_viper_v2),
	NGENE_ID(0x14f3, 0x0041, ngene_info_vbox_v1),
	NGENE_ID(0x14f3, 0x0043, ngene_info_vbox_v2),
	NGENE_ID(0x18c3, 0xabcd, ngene_info_s2),
	NGENE_ID(0x18c3, 0xabc2, ngene_info_s2_b),
	NGENE_ID(0x18c3, 0xabc3, ngene_info_s2_b),
	{0}
};

/****************************************************************************/
/* Init/Exit ****************************************************************/
/****************************************************************************/

static pci_ers_result_t ngene_error_detected(struct pci_dev *dev,
					     enum pci_channel_state state)
{
	printk(KERN_ERR DEVICE_NAME ": PCI error\n");
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;
	if (state == pci_channel_io_frozen)
		return PCI_ERS_RESULT_NEED_RESET;
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static pci_ers_result_t ngene_link_reset(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": link reset\n");
	return 0;
}

static pci_ers_result_t ngene_slot_reset(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": slot reset\n");
	return 0;
}

static void ngene_resume(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": resume\n");
}

static struct pci_error_handlers ngene_errors = {
	.error_detected = ngene_error_detected,
	.link_reset = ngene_link_reset,
	.slot_reset = ngene_slot_reset,
	.resume = ngene_resume,
};

static struct pci_driver ngene_pci_driver = {
	.name        = "ngene",
	.id_table    = ngene_id_tbl,
	.probe       = ngene_probe,
	.remove      = ngene_remove,
	.err_handler = &ngene_errors,
};

static __init int module_init_ngene(void)
{
	printk(KERN_INFO
	       "nGene PCIE bridge driver, Copyright (C) 2005-2007 Micronas\n");
	return pci_register_driver(&ngene_pci_driver);
}

static __exit void module_exit_ngene(void)
{
	pci_unregister_driver(&ngene_pci_driver);
}

module_init(module_init_ngene);
module_exit(module_exit_ngene);

MODULE_DESCRIPTION("nGene");
MODULE_AUTHOR("Micronas, Ralph Metzler, Manfred Voelkel");
MODULE_LICENSE("GPL");
