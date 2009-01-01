/*
 * dm1105.c - driver for DVB cards based on SDMC DM1105 PCI chip
 *
 * Copyright (C) 2008 Igor M. Liplianin <liplianin@me.by>
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
 *
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/input.h>
#include <media/ir-common.h>

#include "demux.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"
#include "dvbdev.h"
#include "dvb-pll.h"

#include "stv0299.h"
#include "stv0288.h"
#include "stb6000.h"
#include "si21xx.h"
#include "cx24116.h"
#include "z0194a.h"

/* ----------------------------------------------- */
/*
 * PCI ID's
 */
#ifndef PCI_VENDOR_ID_TRIGEM
#define PCI_VENDOR_ID_TRIGEM	0x109f
#endif
#ifndef PCI_DEVICE_ID_DM1105
#define PCI_DEVICE_ID_DM1105	0x036f
#endif
#ifndef PCI_DEVICE_ID_DW2002
#define PCI_DEVICE_ID_DW2002	0x2002
#endif
#ifndef PCI_DEVICE_ID_DW2004
#define PCI_DEVICE_ID_DW2004	0x2004
#endif
/* ----------------------------------------------- */
/* sdmc dm1105 registers */

/* TS Control */
#define DM1105_TSCTR				0x00
#define DM1105_DTALENTH				0x04

/* GPIO Interface */
#define DM1105_GPIOVAL				0x08
#define DM1105_GPIOCTR				0x0c

/* PID serial number */
#define DM1105_PIDN				0x10

/* Odd-even secret key select */
#define DM1105_CWSEL				0x14

/* Host Command Interface */
#define DM1105_HOST_CTR				0x18
#define DM1105_HOST_AD				0x1c

/* PCI Interface */
#define DM1105_CR				0x30
#define DM1105_RST				0x34
#define DM1105_STADR				0x38
#define DM1105_RLEN				0x3c
#define DM1105_WRP				0x40
#define DM1105_INTCNT				0x44
#define DM1105_INTMAK				0x48
#define DM1105_INTSTS				0x4c

/* CW Value */
#define DM1105_ODD				0x50
#define DM1105_EVEN				0x58

/* PID Value */
#define DM1105_PID				0x60

/* IR Control */
#define DM1105_IRCTR				0x64
#define DM1105_IRMODE				0x68
#define DM1105_SYSTEMCODE			0x6c
#define DM1105_IRCODE				0x70

/* Unknown Values */
#define DM1105_ENCRYPT				0x74
#define DM1105_VER				0x7c

/* I2C Interface */
#define DM1105_I2CCTR				0x80
#define DM1105_I2CSTS				0x81
#define DM1105_I2CDAT				0x82
#define DM1105_I2C_RA				0x83
/* ----------------------------------------------- */
/* Interrupt Mask Bits */

#define INTMAK_TSIRQM				0x01
#define INTMAK_HIRQM				0x04
#define INTMAK_IRM				0x08
#define INTMAK_ALLMASK				(INTMAK_TSIRQM | \
						INTMAK_HIRQM | \
						INTMAK_IRM)
#define INTMAK_NONEMASK				0x00

/* Interrupt Status Bits */
#define INTSTS_TSIRQ				0x01
#define INTSTS_HIRQ				0x04
#define INTSTS_IR				0x08

/* IR Control Bits */
#define DM1105_IR_EN				0x01
#define DM1105_SYS_CHK				0x02
#define DM1105_REP_FLG				0x08

/* EEPROM addr */
#define IIC_24C01_addr				0xa0
/* Max board count */
#define DM1105_MAX				0x04

#define DRIVER_NAME				"dm1105"

#define DM1105_DMA_PACKETS			47
#define DM1105_DMA_PACKET_LENGTH		(128*4)
#define DM1105_DMA_BYTES			(128 * 4 * DM1105_DMA_PACKETS)

/* GPIO's for LNB power control */
#define DM1105_LNB_MASK				0x00000000
#define DM1105_LNB_13V				0x00010100
#define DM1105_LNB_18V				0x00000100

static int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debugging information for IR decoding");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static u16 ir_codes_dm1105_nec[128] = {
	[0x0a] = KEY_Q,		/*power*/
	[0x0c] = KEY_M,		/*mute*/
	[0x11] = KEY_1,
	[0x12] = KEY_2,
	[0x13] = KEY_3,
	[0x14] = KEY_4,
	[0x15] = KEY_5,
	[0x16] = KEY_6,
	[0x17] = KEY_7,
	[0x18] = KEY_8,
	[0x19] = KEY_9,
	[0x10] = KEY_0,
	[0x1c] = KEY_PAGEUP,	/*ch+*/
	[0x0f] = KEY_PAGEDOWN,	/*ch-*/
	[0x1a] = KEY_O,		/*vol+*/
	[0x0e] = KEY_Z,		/*vol-*/
	[0x04] = KEY_R,		/*rec*/
	[0x09] = KEY_D,		/*fav*/
	[0x08] = KEY_BACKSPACE,	/*rewind*/
	[0x07] = KEY_A,		/*fast*/
	[0x0b] = KEY_P,		/*pause*/
	[0x02] = KEY_ESC,	/*cancel*/
	[0x03] = KEY_G,		/*tab*/
	[0x00] = KEY_UP,	/*up*/
	[0x1f] = KEY_ENTER,	/*ok*/
	[0x01] = KEY_DOWN,	/*down*/
	[0x05] = KEY_C,		/*cap*/
	[0x06] = KEY_S,		/*stop*/
	[0x40] = KEY_F,		/*full*/
	[0x1e] = KEY_W,		/*tvmode*/
	[0x1b] = KEY_B,		/*recall*/
};

/* infrared remote control */
struct infrared {
	u16	key_map[128];
	struct input_dev	*input_dev;
	char			input_phys[32];
	struct tasklet_struct	ir_tasklet;
	u32			ir_command;
};

struct dm1105dvb {
	/* pci */
	struct pci_dev *pdev;
	u8 __iomem *io_mem;

	/* ir */
	struct infrared ir;

	/* dvb */
	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;
	struct dmxdev dmxdev;
	struct dvb_adapter dvb_adapter;
	struct dvb_demux demux;
	struct dvb_frontend *fe;
	struct dvb_net dvbnet;
	unsigned int full_ts_users;

	/* i2c */
	struct i2c_adapter i2c_adap;

	/* dma */
	dma_addr_t dma_addr;
	unsigned char *ts_buf;
	u32 wrp;
	u32 buffer_size;
	unsigned int	PacketErrorCount;
	unsigned int dmarst;
	spinlock_t lock;

};

#define dm_io_mem(reg)	((unsigned long)(&dm1105dvb->io_mem[reg]))

static struct dm1105dvb *dm1105dvb_local;

static int dm1105_i2c_xfer(struct i2c_adapter *i2c_adap,
			    struct i2c_msg *msgs, int num)
{
	struct dm1105dvb *dm1105dvb ;

	int addr, rc, i, j, k, len, byte, data;
	u8 status;

	dm1105dvb = i2c_adap->algo_data;
	for (i = 0; i < num; i++) {
		outb(0x00, dm_io_mem(DM1105_I2CCTR));
		if (msgs[i].flags & I2C_M_RD) {
			/* read bytes */
			addr  = msgs[i].addr << 1;
			addr |= 1;
			outb(addr, dm_io_mem(DM1105_I2CDAT));
			for (byte = 0; byte < msgs[i].len; byte++)
				outb(0, dm_io_mem(DM1105_I2CDAT + byte + 1));

			outb(0x81 + msgs[i].len, dm_io_mem(DM1105_I2CCTR));
			for (j = 0; j < 55; j++) {
				mdelay(10);
				status = inb(dm_io_mem(DM1105_I2CSTS));
				if ((status & 0xc0) == 0x40)
					break;
			}
			if (j >= 55)
				return -1;

			for (byte = 0; byte < msgs[i].len; byte++) {
				rc = inb(dm_io_mem(DM1105_I2CDAT + byte + 1));
				if (rc < 0)
					goto err;
				msgs[i].buf[byte] = rc;
			}
		} else {
			if ((msgs[i].buf[0] == 0xf7) && (msgs[i].addr == 0x55)) {
				/* prepaired for cx24116 firmware */
				/* Write in small blocks */
				len = msgs[i].len - 1;
				k = 1;
				do {
					outb(msgs[i].addr << 1, dm_io_mem(DM1105_I2CDAT));
					outb(0xf7, dm_io_mem(DM1105_I2CDAT + 1));
					for (byte = 0; byte < (len > 48 ? 48 : len); byte++) {
						data = msgs[i].buf[k+byte];
						outb(data, dm_io_mem(DM1105_I2CDAT + byte + 2));
					}
					outb(0x82 + (len > 48 ? 48 : len), dm_io_mem(DM1105_I2CCTR));
					for (j = 0; j < 25; j++) {
						mdelay(10);
						status = inb(dm_io_mem(DM1105_I2CSTS));
						if ((status & 0xc0) == 0x40)
							break;
					}

					if (j >= 25)
						return -1;

					k += 48;
					len -= 48;
				} while (len > 0);
			} else {
				/* write bytes */
				outb(msgs[i].addr<<1, dm_io_mem(DM1105_I2CDAT));
				for (byte = 0; byte < msgs[i].len; byte++) {
					data = msgs[i].buf[byte];
					outb(data, dm_io_mem(DM1105_I2CDAT + byte + 1));
				}
				outb(0x81 + msgs[i].len, dm_io_mem(DM1105_I2CCTR));
				for (j = 0; j < 25; j++) {
					mdelay(10);
					status = inb(dm_io_mem(DM1105_I2CSTS));
					if ((status & 0xc0) == 0x40)
						break;
				}

				if (j >= 25)
					return -1;
			}
		}
	}
	return num;
 err:
	return rc;
}

static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm dm1105_algo = {
	.master_xfer   = dm1105_i2c_xfer,
	.functionality = functionality,
};

static inline struct dm1105dvb *feed_to_dm1105dvb(struct dvb_demux_feed *feed)
{
	return container_of(feed->demux, struct dm1105dvb, demux);
}

static inline struct dm1105dvb *frontend_to_dm1105dvb(struct dvb_frontend *fe)
{
	return container_of(fe->dvb, struct dm1105dvb, dvb_adapter);
}

static int dm1105dvb_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct dm1105dvb *dm1105dvb = frontend_to_dm1105dvb(fe);

		if (voltage == SEC_VOLTAGE_18) {
			outl(DM1105_LNB_MASK, dm_io_mem(DM1105_GPIOCTR));
			outl(DM1105_LNB_18V, dm_io_mem(DM1105_GPIOVAL));
		} else	{
		/*LNB ON-13V by default!*/
			outl(DM1105_LNB_MASK, dm_io_mem(DM1105_GPIOCTR));
			outl(DM1105_LNB_13V, dm_io_mem(DM1105_GPIOVAL));
		}

	return 0;
}

static void dm1105dvb_set_dma_addr(struct dm1105dvb *dm1105dvb)
{
	outl(cpu_to_le32(dm1105dvb->dma_addr), dm_io_mem(DM1105_STADR));
}

static int __devinit dm1105dvb_dma_map(struct dm1105dvb *dm1105dvb)
{
	dm1105dvb->ts_buf = pci_alloc_consistent(dm1105dvb->pdev, 6*DM1105_DMA_BYTES, &dm1105dvb->dma_addr);

	return !dm1105dvb->ts_buf;
}

static void dm1105dvb_dma_unmap(struct dm1105dvb *dm1105dvb)
{
	pci_free_consistent(dm1105dvb->pdev, 6*DM1105_DMA_BYTES, dm1105dvb->ts_buf, dm1105dvb->dma_addr);
}

static void dm1105dvb_enable_irqs(struct dm1105dvb *dm1105dvb)
{
	outb(INTMAK_ALLMASK, dm_io_mem(DM1105_INTMAK));
	outb(1, dm_io_mem(DM1105_CR));
}

static void dm1105dvb_disable_irqs(struct dm1105dvb *dm1105dvb)
{
	outb(INTMAK_IRM, dm_io_mem(DM1105_INTMAK));
	outb(0, dm_io_mem(DM1105_CR));
}

static int dm1105dvb_start_feed(struct dvb_demux_feed *f)
{
	struct dm1105dvb *dm1105dvb = feed_to_dm1105dvb(f);

	if (dm1105dvb->full_ts_users++ == 0)
		dm1105dvb_enable_irqs(dm1105dvb);

	return 0;
}

static int dm1105dvb_stop_feed(struct dvb_demux_feed *f)
{
	struct dm1105dvb *dm1105dvb = feed_to_dm1105dvb(f);

	if (--dm1105dvb->full_ts_users == 0)
		dm1105dvb_disable_irqs(dm1105dvb);

	return 0;
}

/* ir tasklet */
static void dm1105_emit_key(unsigned long parm)
{
	struct infrared *ir = (struct infrared *) parm;
	u32 ircom = ir->ir_command;
	u8 data;
	u16 keycode;

	data = (ircom >> 8) & 0x7f;

	input_event(ir->input_dev, EV_MSC, MSC_RAW, (0x0000f8 << 16) | data);
	input_event(ir->input_dev, EV_MSC, MSC_SCAN, data);
	keycode = ir->key_map[data];

	if (!keycode)
		return;

	input_event(ir->input_dev, EV_KEY, keycode, 1);
	input_sync(ir->input_dev);
	input_event(ir->input_dev, EV_KEY, keycode, 0);
	input_sync(ir->input_dev);

}

static irqreturn_t dm1105dvb_irq(int irq, void *dev_id)
{
	struct dm1105dvb *dm1105dvb = dev_id;
	unsigned int piece;
	unsigned int nbpackets;
	u32 command;
	u32 nextwrp;
	u32 oldwrp;

	/* Read-Write INSTS Ack's Interrupt for DM1105 chip 16.03.2008 */
	unsigned int intsts = inb(dm_io_mem(DM1105_INTSTS));
	outb(intsts, dm_io_mem(DM1105_INTSTS));

	switch (intsts) {
	case INTSTS_TSIRQ:
	case (INTSTS_TSIRQ | INTSTS_IR):
		nextwrp = inl(dm_io_mem(DM1105_WRP)) -
			inl(dm_io_mem(DM1105_STADR)) ;
		oldwrp = dm1105dvb->wrp;
		spin_lock(&dm1105dvb->lock);
		if (!((dm1105dvb->ts_buf[oldwrp] == 0x47) &&
				(dm1105dvb->ts_buf[oldwrp + 188] == 0x47) &&
				(dm1105dvb->ts_buf[oldwrp + 188 * 2] == 0x47))) {
			dm1105dvb->PacketErrorCount++;
			/* bad packet found */
			if ((dm1105dvb->PacketErrorCount >= 2) &&
					(dm1105dvb->dmarst == 0)) {
				outb(1, dm_io_mem(DM1105_RST));
				dm1105dvb->wrp = 0;
				dm1105dvb->PacketErrorCount = 0;
				dm1105dvb->dmarst = 0;
				spin_unlock(&dm1105dvb->lock);
				return IRQ_HANDLED;
			}
		}
		if (nextwrp < oldwrp) {
			piece = dm1105dvb->buffer_size - oldwrp;
			memcpy(dm1105dvb->ts_buf + dm1105dvb->buffer_size, dm1105dvb->ts_buf, nextwrp);
			nbpackets = (piece + nextwrp)/188;
		} else	{
			nbpackets = (nextwrp - oldwrp)/188;
		}
		dvb_dmx_swfilter_packets(&dm1105dvb->demux, &dm1105dvb->ts_buf[oldwrp], nbpackets);
		dm1105dvb->wrp = nextwrp;
		spin_unlock(&dm1105dvb->lock);
		break;
	case INTSTS_IR:
		command = inl(dm_io_mem(DM1105_IRCODE));
		if (ir_debug)
			printk("dm1105: received byte 0x%04x\n", command);

		dm1105dvb->ir.ir_command = command;
		tasklet_schedule(&dm1105dvb->ir.ir_tasklet);
		break;
	}
	return IRQ_HANDLED;


}

/* register with input layer */
static void input_register_keys(struct infrared *ir)
{
	int i;

	memset(ir->input_dev->keybit, 0, sizeof(ir->input_dev->keybit));

	for (i = 0; i < ARRAY_SIZE(ir->key_map); i++)
			set_bit(ir->key_map[i], ir->input_dev->keybit);

	ir->input_dev->keycode = ir->key_map;
	ir->input_dev->keycodesize = sizeof(ir->key_map[0]);
	ir->input_dev->keycodemax = ARRAY_SIZE(ir->key_map);
}

int __devinit dm1105_ir_init(struct dm1105dvb *dm1105)
{
	struct input_dev *input_dev;
	int err;

	dm1105dvb_local = dm1105;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	dm1105->ir.input_dev = input_dev;
	snprintf(dm1105->ir.input_phys, sizeof(dm1105->ir.input_phys),
		"pci-%s/ir0", pci_name(dm1105->pdev));

	input_dev->evbit[0] = BIT(EV_KEY);
	input_dev->name = "DVB on-card IR receiver";

	input_dev->phys = dm1105->ir.input_phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 2;
	if (dm1105->pdev->subsystem_vendor) {
		input_dev->id.vendor = dm1105->pdev->subsystem_vendor;
		input_dev->id.product = dm1105->pdev->subsystem_device;
	} else {
		input_dev->id.vendor = dm1105->pdev->vendor;
		input_dev->id.product = dm1105->pdev->device;
	}
	input_dev->dev.parent = &dm1105->pdev->dev;
	/* initial keymap */
	memcpy(dm1105->ir.key_map, ir_codes_dm1105_nec, sizeof dm1105->ir.key_map);
	input_register_keys(&dm1105->ir);
	err = input_register_device(input_dev);
	if (err) {
		input_free_device(input_dev);
		return err;
	}

	tasklet_init(&dm1105->ir.ir_tasklet, dm1105_emit_key, (unsigned long) &dm1105->ir);

	return 0;
}


void __devexit dm1105_ir_exit(struct dm1105dvb *dm1105)
{
	tasklet_kill(&dm1105->ir.ir_tasklet);
	input_unregister_device(dm1105->ir.input_dev);

}

static int __devinit dm1105dvb_hw_init(struct dm1105dvb *dm1105dvb)
{
	dm1105dvb_disable_irqs(dm1105dvb);

	outb(0, dm_io_mem(DM1105_HOST_CTR));

	/*DATALEN 188,*/
	outb(188, dm_io_mem(DM1105_DTALENTH));
	/*TS_STRT TS_VALP MSBFIRST TS_MODE ALPAS TSPES*/
	outw(0xc10a, dm_io_mem(DM1105_TSCTR));

	/* map DMA and set address */
	dm1105dvb_dma_map(dm1105dvb);
	dm1105dvb_set_dma_addr(dm1105dvb);
	/* big buffer */
	outl(5*DM1105_DMA_BYTES, dm_io_mem(DM1105_RLEN));
	outb(47, dm_io_mem(DM1105_INTCNT));

	/* IR NEC mode enable */
	outb((DM1105_IR_EN | DM1105_SYS_CHK), dm_io_mem(DM1105_IRCTR));
	outb(0, dm_io_mem(DM1105_IRMODE));
	outw(0, dm_io_mem(DM1105_SYSTEMCODE));

	return 0;
}

static void dm1105dvb_hw_exit(struct dm1105dvb *dm1105dvb)
{
	dm1105dvb_disable_irqs(dm1105dvb);

	/* IR disable */
	outb(0, dm_io_mem(DM1105_IRCTR));
	outb(INTMAK_NONEMASK, dm_io_mem(DM1105_INTMAK));

	dm1105dvb_dma_unmap(dm1105dvb);
}

static struct stv0299_config sharp_z0194a_config = {
	.demod_address = 0x68,
	.inittab = sharp_z0194a_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.skip_reinit = 0,
	.lock_output = STV0299_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = sharp_z0194a_set_symbol_rate,
};

static struct stv0288_config earda_config = {
	.demod_address = 0x68,
	.min_delay_ms = 100,
};

static struct si21xx_config serit_config = {
	.demod_address = 0x68,
	.min_delay_ms = 100,

};

static struct cx24116_config serit_sp2633_config = {
	.demod_address = 0x55,
};

static int __devinit frontend_init(struct dm1105dvb *dm1105dvb)
{
	int ret;

	switch (dm1105dvb->pdev->subsystem_device) {
	case PCI_DEVICE_ID_DW2002:
		dm1105dvb->fe = dvb_attach(
			stv0299_attach, &sharp_z0194a_config,
			&dm1105dvb->i2c_adap);

		if (dm1105dvb->fe) {
			dm1105dvb->fe->ops.set_voltage =
							dm1105dvb_set_voltage;
			dvb_attach(dvb_pll_attach, dm1105dvb->fe, 0x60,
					&dm1105dvb->i2c_adap, DVB_PLL_OPERA1);
		}

		if (!dm1105dvb->fe) {
			dm1105dvb->fe = dvb_attach(
				stv0288_attach, &earda_config,
				&dm1105dvb->i2c_adap);
			if (dm1105dvb->fe) {
				dm1105dvb->fe->ops.set_voltage =
							dm1105dvb_set_voltage;
				dvb_attach(stb6000_attach, dm1105dvb->fe, 0x61,
						&dm1105dvb->i2c_adap);
			}
		}

		if (!dm1105dvb->fe) {
			dm1105dvb->fe = dvb_attach(
				si21xx_attach, &serit_config,
				&dm1105dvb->i2c_adap);
			if (dm1105dvb->fe)
				dm1105dvb->fe->ops.set_voltage =
							dm1105dvb_set_voltage;
		}
		break;
	case PCI_DEVICE_ID_DW2004:
		dm1105dvb->fe = dvb_attach(
			cx24116_attach, &serit_sp2633_config,
			&dm1105dvb->i2c_adap);
		if (dm1105dvb->fe)
			dm1105dvb->fe->ops.set_voltage = dm1105dvb_set_voltage;
		break;
	}

	if (!dm1105dvb->fe) {
		dev_err(&dm1105dvb->pdev->dev, "could not attach frontend\n");
		return -ENODEV;
	}

	ret = dvb_register_frontend(&dm1105dvb->dvb_adapter, dm1105dvb->fe);
	if (ret < 0) {
		if (dm1105dvb->fe->ops.release)
			dm1105dvb->fe->ops.release(dm1105dvb->fe);
		dm1105dvb->fe = NULL;
		return ret;
	}

	return 0;
}

static void __devinit dm1105dvb_read_mac(struct dm1105dvb *dm1105dvb, u8 *mac)
{
	static u8 command[1] = { 0x28 };

	struct i2c_msg msg[] = {
		{ .addr = IIC_24C01_addr >> 1, .flags = 0,
				.buf = command, .len = 1 },
		{ .addr = IIC_24C01_addr >> 1, .flags = I2C_M_RD,
				.buf = mac, .len = 6 },
	};

	dm1105_i2c_xfer(&dm1105dvb->i2c_adap, msg , 2);
	dev_info(&dm1105dvb->pdev->dev, "MAC %pM\n", mac);
}

static int __devinit dm1105_probe(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct dm1105dvb *dm1105dvb;
	struct dvb_adapter *dvb_adapter;
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;
	int ret = -ENOMEM;

	dm1105dvb = kzalloc(sizeof(struct dm1105dvb), GFP_KERNEL);
	if (!dm1105dvb)
		goto out;

	dm1105dvb->pdev = pdev;
	dm1105dvb->buffer_size = 5 * DM1105_DMA_BYTES;
	dm1105dvb->PacketErrorCount = 0;
	dm1105dvb->dmarst = 0;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		goto err_kfree;

	ret = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (ret < 0)
		goto err_pci_disable_device;

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, DRIVER_NAME);
	if (ret < 0)
		goto err_pci_disable_device;

	dm1105dvb->io_mem = pci_iomap(pdev, 0, pci_resource_len(pdev, 0));
	if (!dm1105dvb->io_mem) {
		ret = -EIO;
		goto err_pci_release_regions;
	}

	spin_lock_init(&dm1105dvb->lock);
	pci_set_drvdata(pdev, dm1105dvb);

	ret = request_irq(pdev->irq, dm1105dvb_irq, IRQF_SHARED, DRIVER_NAME, dm1105dvb);
	if (ret < 0)
		goto err_pci_iounmap;

	ret = dm1105dvb_hw_init(dm1105dvb);
	if (ret < 0)
		goto err_free_irq;

	/* i2c */
	i2c_set_adapdata(&dm1105dvb->i2c_adap, dm1105dvb);
	strcpy(dm1105dvb->i2c_adap.name, DRIVER_NAME);
	dm1105dvb->i2c_adap.owner = THIS_MODULE;
	dm1105dvb->i2c_adap.class = I2C_CLASS_TV_DIGITAL;
	dm1105dvb->i2c_adap.dev.parent = &pdev->dev;
	dm1105dvb->i2c_adap.algo = &dm1105_algo;
	dm1105dvb->i2c_adap.algo_data = dm1105dvb;
	ret = i2c_add_adapter(&dm1105dvb->i2c_adap);

	if (ret < 0)
		goto err_dm1105dvb_hw_exit;

	/* dvb */
	ret = dvb_register_adapter(&dm1105dvb->dvb_adapter, DRIVER_NAME,
					THIS_MODULE, &pdev->dev, adapter_nr);
	if (ret < 0)
		goto err_i2c_del_adapter;

	dvb_adapter = &dm1105dvb->dvb_adapter;

	dm1105dvb_read_mac(dm1105dvb, dvb_adapter->proposed_mac);

	dvbdemux = &dm1105dvb->demux;
	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	dvbdemux->start_feed = dm1105dvb_start_feed;
	dvbdemux->stop_feed = dm1105dvb_stop_feed;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING |
			DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING);
	ret = dvb_dmx_init(dvbdemux);
	if (ret < 0)
		goto err_dvb_unregister_adapter;

	dmx = &dvbdemux->dmx;
	dm1105dvb->dmxdev.filternum = 256;
	dm1105dvb->dmxdev.demux = dmx;
	dm1105dvb->dmxdev.capabilities = 0;

	ret = dvb_dmxdev_init(&dm1105dvb->dmxdev, dvb_adapter);
	if (ret < 0)
		goto err_dvb_dmx_release;

	dm1105dvb->hw_frontend.source = DMX_FRONTEND_0;

	ret = dmx->add_frontend(dmx, &dm1105dvb->hw_frontend);
	if (ret < 0)
		goto err_dvb_dmxdev_release;

	dm1105dvb->mem_frontend.source = DMX_MEMORY_FE;

	ret = dmx->add_frontend(dmx, &dm1105dvb->mem_frontend);
	if (ret < 0)
		goto err_remove_hw_frontend;

	ret = dmx->connect_frontend(dmx, &dm1105dvb->hw_frontend);
	if (ret < 0)
		goto err_remove_mem_frontend;

	ret = frontend_init(dm1105dvb);
	if (ret < 0)
		goto err_disconnect_frontend;

	dvb_net_init(dvb_adapter, &dm1105dvb->dvbnet, dmx);
	dm1105_ir_init(dm1105dvb);
out:
	return ret;

err_disconnect_frontend:
	dmx->disconnect_frontend(dmx);
err_remove_mem_frontend:
	dmx->remove_frontend(dmx, &dm1105dvb->mem_frontend);
err_remove_hw_frontend:
	dmx->remove_frontend(dmx, &dm1105dvb->hw_frontend);
err_dvb_dmxdev_release:
	dvb_dmxdev_release(&dm1105dvb->dmxdev);
err_dvb_dmx_release:
	dvb_dmx_release(dvbdemux);
err_dvb_unregister_adapter:
	dvb_unregister_adapter(dvb_adapter);
err_i2c_del_adapter:
	i2c_del_adapter(&dm1105dvb->i2c_adap);
err_dm1105dvb_hw_exit:
	dm1105dvb_hw_exit(dm1105dvb);
err_free_irq:
	free_irq(pdev->irq, dm1105dvb);
err_pci_iounmap:
	pci_iounmap(pdev, dm1105dvb->io_mem);
err_pci_release_regions:
	pci_release_regions(pdev);
err_pci_disable_device:
	pci_disable_device(pdev);
err_kfree:
	pci_set_drvdata(pdev, NULL);
	kfree(dm1105dvb);
	goto out;
}

static void __devexit dm1105_remove(struct pci_dev *pdev)
{
	struct dm1105dvb *dm1105dvb = pci_get_drvdata(pdev);
	struct dvb_adapter *dvb_adapter = &dm1105dvb->dvb_adapter;
	struct dvb_demux *dvbdemux = &dm1105dvb->demux;
	struct dmx_demux *dmx = &dvbdemux->dmx;

	dm1105_ir_exit(dm1105dvb);
	dmx->close(dmx);
	dvb_net_release(&dm1105dvb->dvbnet);
	if (dm1105dvb->fe)
		dvb_unregister_frontend(dm1105dvb->fe);

	dmx->disconnect_frontend(dmx);
	dmx->remove_frontend(dmx, &dm1105dvb->mem_frontend);
	dmx->remove_frontend(dmx, &dm1105dvb->hw_frontend);
	dvb_dmxdev_release(&dm1105dvb->dmxdev);
	dvb_dmx_release(dvbdemux);
	dvb_unregister_adapter(dvb_adapter);
	if (&dm1105dvb->i2c_adap)
		i2c_del_adapter(&dm1105dvb->i2c_adap);

	dm1105dvb_hw_exit(dm1105dvb);
	synchronize_irq(pdev->irq);
	free_irq(pdev->irq, dm1105dvb);
	pci_iounmap(pdev, dm1105dvb->io_mem);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	kfree(dm1105dvb);
}

static struct pci_device_id dm1105_id_table[] __devinitdata = {
	{
		.vendor = PCI_VENDOR_ID_TRIGEM,
		.device = PCI_DEVICE_ID_DM1105,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_DEVICE_ID_DW2002,
	}, {
		.vendor = PCI_VENDOR_ID_TRIGEM,
		.device = PCI_DEVICE_ID_DM1105,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_DEVICE_ID_DW2004,
	}, {
		/* empty */
	},
};

MODULE_DEVICE_TABLE(pci, dm1105_id_table);

static struct pci_driver dm1105_driver = {
	.name = DRIVER_NAME,
	.id_table = dm1105_id_table,
	.probe = dm1105_probe,
	.remove = __devexit_p(dm1105_remove),
};

static int __init dm1105_init(void)
{
	return pci_register_driver(&dm1105_driver);
}

static void __exit dm1105_exit(void)
{
	pci_unregister_driver(&dm1105_driver);
}

module_init(dm1105_init);
module_exit(dm1105_exit);

MODULE_AUTHOR("Igor M. Liplianin <liplianin@me.by>");
MODULE_DESCRIPTION("SDMC DM1105 DVB driver");
MODULE_LICENSE("GPL");
